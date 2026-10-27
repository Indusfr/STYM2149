// STYM2149 — plugincore: engine status for the interface
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/EngineStatus.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "plugincore/Mapping.h"

namespace stym::plugin {

void EngineStatusTap::prepare(uint32_t sampleRate) noexcept {
    period_ = std::max<uint32_t>(1, sampleRate / 30);
    elapsed_ = 0;
    ring_.fill(0);
    ringPos_ = 0;
    outPeak_.fill(0);
}

void EngineStatusTap::capture(ymengine::Engine& engine, const ParamValues& values, const float* left,
                           const float* right, uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i) {
        ring_[ringPos_] = 0.5f * (left[i] + right[i]);
        ringPos_ = (ringPos_ + 1) % kScopeSamples;
        outPeak_[0] = std::max(outPeak_[0], std::fabs(left[i]));
        outPeak_[1] = std::max(outPeak_[1], std::fabs(right[i]));
    }
    // Publication when a period has passed, the remainder carried over; a block longer than a period still publishes
    // only once.
    elapsed_ += n;
    if (elapsed_ < period_) return;
    elapsed_ %= period_;
    publish(engine, values);
}

void EngineStatusTap::publish(ymengine::Engine& engine, const ParamValues& values) noexcept {
    EngineStatus& t = buffer_.back();
    t.serial = ++serial_;

    // The right side of a stereo DMA sample has its own fader, mute and solo.
    auto on = [&](int k, Track f) { return value(values, k, f) >= 0.5f; };
    bool solo = false;
    for (int k = 0; k < kTracks; ++k)
        solo = solo || on(k, Track::Solo) || (on(k, Track::SoloR) && engine.trackStereo(k));
    auto side = [&](int k, Track level, Track mute, Track soloed) {
        return value(values, k, level) > 0 && !on(k, mute) && (!solo || on(k, soloed));
    };
    auto audible = [&](int k) {
        const Track levelR = on(k, Track::Link) ? Track::Level : Track::LevelR;
        return on(k, Track::On) && (side(k, Track::Level, Track::Mute, Track::Solo) ||
                                    (engine.trackStereo(k) && side(k, levelR, Track::MuteR, Track::SoloR)));
    };
    const int focus = std::clamp(intValue(values, Global::FocusTrack), 1, kTracks) - 1;

    // One pass over the voices: LEDs of the audible tracks, the most recent attack (Note readout) and the voices whose
    // envelope is drawn.
    t.active.fill(false);
    int newest = -1, drawnCount = 0;
    uint64_t newestAge = 0;
    std::array<std::pair<uint64_t, int>, ymengine::Engine::kVoices> drawn;  // age and index
    for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
        const auto v = engine.voice(i);
        if (!v.on) continue;
        if (v.track >= 0 && v.track < kTracks && audible(v.track)) t.active[size_t(v.track)] = true;
        if (newest < 0 || v.age > newestAge) {
            newest = i;
            newestAge = v.age;
        }
        // The curve follows each note of the track, not their echo repeats, which restart an envelope at
        // each repeat (ninth DAW test, item 113).
        const bool own = v.tag != ymengine::VoiceTag::Echo;
        if (v.track == focus && own && !v.fading) drawn[size_t(drawnCount++)] = {v.age, i};
    }
    if (newest >= 0 && newestAge + 1 != lastAge_) {  // lastAge_: rank + 1 (0: no note seen)
        lastAge_ = newestAge + 1;
        ++noteSerial_;
        notePeriod_ = engine.voice(newest).notePeriod;
    }
    t.noteSerial = noteSerial_;
    t.notePeriod = notePeriod_;
    t.noteSounding = newest >= 0;
    // The newest kEnvPoints, newest first. Several points are polyphony: a monophonic track (Voicing Monophonic, or
    // any track of the 3-channel modes, one voice per channel) shows its newest note alone, though the one before may
    // still end its release (Indus, 24/10/2026 14:51).
    const bool mono = intValue(values, Global::VoiceMode) != 0 || intValue(values, focus, Track::Voicing) == 1;
    const int shown = std::min(drawnCount, mono ? 1 : kEnvPoints);
    std::partial_sort(drawn.begin(), drawn.begin() + shown, drawn.begin() + drawnCount,
                      [](const auto& a, const auto& b) { return a.first > b.first; });
    t.envCount = uint8_t(shown);
    for (int k = 0; k < kEnvPoints; ++k) {
        EnvPoint& e = t.env[size_t(k)];
        e = {};
        if (k >= shown) continue;
        const auto v = engine.voice(drawn[size_t(k)].second);
        // Stage 0 (no ADSR running) shows as a sustain at full level.
        e.stage = int8_t(v.adsrStage ? v.adsrStage : 3);
        e.level = v.adsrStage ? float(v.adsrNow) : 1.0f;
    }
    t.seq = engine.seqStatus();
    t.arpStep = int8_t(engine.arpStep());
    // The rings start at their write position: copied in two parts, oldest sample first.
    const auto split = ring_.begin() + ringPos_;
    std::copy(split, ring_.end(), t.scope.begin());
    std::copy(ring_.begin(), split, t.scope.begin() + (ring_.end() - split));
    t.outPeak = outPeak_;
    outPeak_.fill(0);
    engine.takeMeters(meters_);
    t.peak = meters_.peak;
    t.peakR = meters_.peakR;
    const size_t pos = meters_.scopePos;
    auto oldestFirst = [pos](const auto& from, auto& to) {
        std::copy(from.begin() + std::ptrdiff_t(pos), from.end(), to.begin());
        std::copy(from.begin(), from.begin() + std::ptrdiff_t(pos), to.end() - std::ptrdiff_t(pos));
    };
    for (int k = 0; k < kTracks; ++k) {
        oldestFirst(meters_.scope[size_t(k)], t.trackScope[size_t(k)]);
        oldestFirst(meters_.side[size_t(k)], t.trackSide[size_t(k)]);
    }
    t.dmaSlots = engine.dmaPlayingSlots();
    t.digiPreview = int8_t(engine.digidrumPreviewSlot());
    t.dmaPreview = int8_t(engine.dmaPreviewSlot());
    for (int k = 0; k < kTracks; ++k) t.program[size_t(k)] = uint8_t(engine.program(k));
    t.manualCut = engine.manualCuts();
    buffer_.publish();
}

}  // namespace stym::plugin
