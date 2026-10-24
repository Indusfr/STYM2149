// STYM2149 — plugincore: engine status for the interface
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/EngineStatus.h"

#include <algorithm>
#include <cmath>

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

    bool solo = false;
    for (int k = 0; k < kTracks; ++k) solo = solo || value(values, k, Track::Solo) >= 0.5f;
    auto audible = [&](int k) {
        return value(values, k, Track::On) >= 0.5f && value(values, k, Track::Level) > 0 &&
               value(values, k, Track::Mute) < 0.5f && (!solo || value(values, k, Track::Solo) >= 0.5f);
    };
    const int focus = std::clamp(intValue(values, Global::FocusTrack), 1, kTracks) - 1;

    // One pass over the voices: LEDs of the audible tracks, the most recent attack (Note readout) and the voice whose
    // envelope is drawn.
    t.active.fill(false);
    int newest = -1, followed = -1;
    uint64_t newestAge = 0, followedAge = 0;
    for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
        const auto v = engine.voice(i);
        if (!v.on) continue;
        if (v.track >= 0 && v.track < kTracks && audible(v.track)) t.active[size_t(v.track)] = true;
        if (newest < 0 || v.age > newestAge) {
            newest = i;
            newestAge = v.age;
        }
        // The curve follows the notes of the track, not their echo repeats, which restart an envelope at each
        // repeat (ninth DAW test, item 113).
        const bool own = v.tag != ymengine::VoiceTag::Echo;
        if (v.track == focus && own && !v.fading && (followed < 0 || v.age > followedAge)) {
            followed = i;
            followedAge = v.age;
        }
    }
    if (newest >= 0 && newestAge + 1 != lastAge_) {  // lastAge_: rank + 1 (0: no note seen)
        lastAge_ = newestAge + 1;
        ++noteSerial_;
        notePeriod_ = engine.voice(newest).notePeriod;
    }
    t.noteSerial = noteSerial_;
    t.notePeriod = notePeriod_;
    if (followed >= 0) {
        const auto v = engine.voice(followed);
        // Stage 0 (no ADSR running) shows as a sustain at full level.
        t.envStage = int8_t(v.adsrStage ? v.adsrStage : 3);
        t.envLevel = v.adsrStage ? float(v.adsrNow) : 1.0f;
    } else {
        t.envStage = -1;
        t.envLevel = 0;
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
    const size_t pos = meters_.scopePos;
    for (int k = 0; k < kTracks; ++k) {
        const auto& from = meters_.scope[size_t(k)];
        auto& to = t.trackScope[size_t(k)];
        std::copy(from.begin() + std::ptrdiff_t(pos), from.end(), to.begin());
        std::copy(from.begin(), from.begin() + std::ptrdiff_t(pos), to.end() - std::ptrdiff_t(pos));
    }
    t.dmaSlots = engine.dmaPlayingSlots();
    for (int k = 0; k < kTracks; ++k) t.program[size_t(k)] = uint8_t(engine.program(k));
    buffer_.publish();
}

}  // namespace stym::plugin
