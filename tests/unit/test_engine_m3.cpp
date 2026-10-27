// STYM2149 — voice engine, additions of the plugin (M3): separate outputs of the tracks and chips, notes, bend and
// wheel per track, effect of the velocity, bass and treble setting of the STE chain during playback.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "ymengine/Engine.h"
#include "ymengine/Pitch.h"

using namespace stym;
using ymengine::Engine;

namespace {

ymengine::EngineConfig config(uint16_t trackOutputs = 0) {
    ymengine::EngineConfig c;
    c.render.hostRate = 48000;
    c.trackOutputs = trackOutputs;
    return c;
}

ymengine::TrackParams track(double pan = 0) {
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.mix.level = 1;
    t.mix.pan = pan;
    return t;
}

void setMode(Engine& e, ymengine::VoiceMode mode, bool softClip = true) {
    ymengine::GlobalParams g = e.global();
    g.mode = mode;
    g.softClip = softClip;
    e.setGlobal(g);
}

// All the outputs of a render.
struct Multi {
    std::vector<float> l, r;
    std::array<std::vector<float>, ymengine::kTracks> t;
    std::array<std::vector<float>, 2> c;
    explicit Multi(uint32_t n) : l(n), r(n) {
        for (auto& x : t) x.assign(n, -1.0f);
        for (auto& x : c) x.assign(n, -1.0f);
    }
};

// Renders n samples in blocks of 512 (all the requested outputs); fire(e, start, size) places the
// events.
Multi renderAll(Engine& e, uint32_t n, const std::function<void(Engine&, uint32_t, uint32_t)>& fire = {}) {
    Multi m(n);
    for (uint32_t at = 0; at < n; at += 512) {
        const uint32_t len = std::min<uint32_t>(512, n - at);
        if (fire) fire(e, at, len);
        ymengine::Outputs o;
        o.left = m.l.data() + at;
        o.right = m.r.data() + at;
        for (size_t i = 0; i < m.t.size(); ++i) o.track[i] = m.t[i].data() + at;
        for (size_t i = 0; i < m.c.size(); ++i) o.chip[i] = m.c[i].data() + at;
        e.render(o, len);
    }
    return m;
}

double peak(const std::vector<float>& x) {
    double p = 0;
    for (float v : x) p = std::max(p, double(std::fabs(v)));
    return p;
}

double maxDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double d = 0;
    for (size_t i = 0; i < a.size(); ++i) d = std::max(d, double(std::fabs(a[i] - b[i])));
    return d;
}

// Event at the first block that contains sample `at`.
std::function<void(Engine&, uint32_t, uint32_t)> at(uint32_t when, std::function<void(Engine&, uint32_t)> f) {
    return [when, f](Engine& e, uint32_t start, uint32_t len) {
        if (when >= start && when < start + len) f(e, when - start);
    };
}
std::function<void(Engine&, uint32_t, uint32_t)> all(std::vector<std::function<void(Engine&, uint32_t, uint32_t)>> fs) {
    return [fs](Engine& e, uint32_t start, uint32_t len) {
        for (const auto& f : fs) f(e, start, len);
    };
}

}  // namespace

TEST_CASE("16-channel separate outputs: one centred track alone = main output", "[engine][outputs]") {
    for (const bool softClip : {true, false}) {
        Engine e;
        e.prepare(config(0b11));
        setMode(e, ymengine::VoiceMode::Studio16, softClip);
        auto t0 = track();
        t0.patch.attackMs = 30;  // slopes of the smooth ADSR on the bus of the track too
        t0.patch.decayMs = 100;
        t0.patch.sustainPct = 50;
        t0.patch.releaseMs = 200;
        e.setTrack(0, t0);
        const Multi m = renderAll(e, 48000, all({at(100, [](Engine& x, uint32_t o) { x.noteOn(o, 60, 100, 0); }),
                                                 at(9000, [](Engine& x, uint32_t o) { x.noteOn(o, 67, 80, 0); }),
                                                 at(30000, [](Engine& x, uint32_t o) { x.noteOff(o, 60, 0); }),
                                                 at(30000, [](Engine& x, uint32_t o) { x.noteOff(o, 67, 0); })}));
        REQUIRE(peak(m.l) > 0.05);
        REQUIRE(maxDiff(m.t[0], m.l) < 1e-6);
        REQUIRE(maxDiff(m.l, m.r) < 1e-6);
        REQUIRE(peak(m.t[1]) == 0.0);
        for (size_t i = 2; i < m.t.size(); ++i) REQUIRE(peak(m.t[i]) == 0.0);  // not prepared: zeros
        REQUIRE(peak(m.c[0]) == 0.0);  // no chip in 16-channel mode
        REQUIRE(peak(m.c[1]) == 0.0);
    }
}

TEST_CASE("16-channel separate outputs: each track only receives its voices, mixer included", "[engine][outputs]") {
    auto scene = [](bool withTrack1, bool mute0) {
        Engine e;
        e.prepare(config(0b11));
        setMode(e, ymengine::VoiceMode::Studio16);
        auto t0 = track();
        t0.patch.attackMs = 15;
        t0.patch.releaseMs = 50;
        e.setTrack(0, t0);
        auto t1 = track(-1);
        t1.patch.noiseOn = true;
        e.setTrack(1, t1);
        return renderAll(e, 24000, [withTrack1, mute0, t0](Engine& x, uint32_t start, uint32_t len) {
            auto in = [&](uint32_t w) { return w >= start && w < start + len; };
            if (in(0)) x.noteOn(0, 57, 127, 0);
            if (withTrack1 && in(700)) x.noteOn(700 - start, 45, 90, 1);
            if (withTrack1 && in(5000)) x.noteOn(5000 - start, 64, 60, 1);  // vol of the voices of track 1
            if (in(8000)) x.noteOff(8000 - start, 57, 0);
            if (mute0 && in(2000)) {
                auto mix = t0.mix;
                mix.mute = true;
                x.setTrackMix(2000 - start, 0, mix);
            }
        });
    };
    const Multi alone = scene(false, false);
    const Multi both = scene(true, false);
    REQUIRE(peak(alone.t[0]) > 0.05);
    REQUIRE(both.t[0] == alone.t[0]);  // bit for bit: the bus of track 0 does not see track 1
    REQUIRE(peak(both.t[1]) > 0.01);
    REQUIRE(peak(alone.t[1]) == 0.0);
    // Mute: the output of the track falls silent too (outputs after the mixer).
    const Multi muted = scene(true, true);
    const uint32_t settle = 2000 + 512 + 2048 + 64;  // moving average (2 048) and resampler elapsed
    // Remainder of the integration of the steps (the voice still sounds, at zero gain), as on the main bus.
    const double residue = peak(std::vector<float>(muted.t[0].begin() + settle, muted.t[0].end()));
    REQUIRE(residue < 1e-12);
    REQUIRE(muted.t[1] == both.t[1]);
}

TEST_CASE("3-channel separate outputs: the track alone = the chip with only its channels", "[engine][outputs][3ch]") {
    // With both tracks: the output of track 0 must be exactly the chip where only track 0 plays.
    auto scene = [](bool withTrack1) {
        Engine e;
        e.prepare(config(0b1111));
        setMode(e, ymengine::VoiceMode::Authentic3);
        auto t0 = track();
        t0.patch.envelopeVolume = true;  // envelope shared by the chip
        t0.patch.envShape = 10;
        t0.patch.envPeriod = 300;
        e.setTrack(0, t0);
        auto t1 = track();
        t1.patch.toneOn = false;
        t1.patch.noiseOn = true;
        e.setTrack(1, t1);
        return renderAll(e, 30000, [withTrack1](Engine& x, uint32_t start, uint32_t len) {
            auto in = [&](uint32_t w) { return w >= start && w < start + len; };
            if (in(0)) x.noteOn(0, 57, 127, 0);
            if (withTrack1 && in(3000)) x.noteOn(3000 - start, 60, 127, 1);
            if (withTrack1 && in(20000)) x.noteOff(20000 - start, 60, 1);
        });
    };
    const Multi alone = scene(false);
    const Multi both = scene(true);
    REQUIRE(peak(both.t[0]) > 0.05);
    REQUIRE(both.t[0] == alone.t[0]);
    REQUIRE(alone.t[0] == alone.l);  // a single track: its chip copy = the chip
    REQUIRE(peak(both.t[1]) > 0.01);
    REQUIRE(peak(both.t[2]) == 0.0);
    REQUIRE(both.c[0] == both.l);    // chip 1 = main output in 3-channel mode
    REQUIRE(peak(both.c[1]) == 0.0); // a single chip
}

TEST_CASE("Dual-chip separate outputs: bus of each chip, sum = main output", "[engine][outputs][3ch]") {
    Engine e;
    e.prepare(config(0x00ff));
    setMode(e, ymengine::VoiceMode::Dual3, false);  // linear: main = chip 1 + chip 2
    e.setTrack(0, track());
    e.setTrack(4, track());
    const Multi m = renderAll(e, 20000, all({at(0, [](Engine& x, uint32_t o) { x.noteOn(o, 57, 127, 0); }),
                                             at(1000, [](Engine& x, uint32_t o) { x.noteOn(o, 64, 127, 4); })}));
    REQUIRE(peak(m.c[0]) > 0.05);
    REQUIRE(peak(m.c[1]) > 0.05);
    double d = 0;
    for (size_t i = 0; i < m.l.size(); ++i) d = std::max(d, std::fabs(double(m.l[i]) - (double(m.c[0][i]) + m.c[1][i])));
    REQUIRE(d < 1e-6);
    REQUIRE(m.t[0] == m.c[0]);  // only track of chip 1
    REQUIRE(m.t[4] == m.c[1]);  // only track of chip 2
    REQUIRE(peak(m.t[8]) == 0.0);  // beyond the 8 tracks of the mode
}

TEST_CASE("Notes per track: the targeted track alone, released by its own track only", "[engine][routing]") {
    Engine e;
    e.prepare(config());
    setMode(e, ymengine::VoiceMode::Studio16);
    e.setTrack(0, track());
    ymengine::TrackParams t3;  // inactive track: played anyway by the routing by channel
    t3.patch.volume = 9;
    e.setTrack(3, t3);
    float l[256], r[256];
    e.noteOn(0, 60, 100, 3);
    e.noteOn(0, 60, 100, 0);
    e.render(l, r, 256);
    REQUIRE(e.activeVoices() == 2);
    int onTrack3 = -1;
    for (int i = 0; i < Engine::kVoices; ++i)
        if (e.voice(i).on && e.voice(i).track == 3) onTrack3 = i;
    REQUIRE(onTrack3 >= 0);
    REQUIRE(e.voice(onTrack3).volume == 9);
    e.noteOff(0, 60, 0);  // only releases track 0
    e.render(l, r, 256);
    REQUIRE(e.voice(onTrack3).on);
    REQUIRE(!e.voice(onTrack3).fading);
    REQUIRE(e.voice(onTrack3).adsrStage != 4);
    e.noteOff(0, 60, 3);
    for (int k = 0; k < 20; ++k) e.render(l, r, 256);
    REQUIRE(e.activeVoices() == 0);

    // 3-channel: 4 tracks per chip, a note for track 5 is ignored.
    setMode(e, ymengine::VoiceMode::Authentic3);
    e.noteOn(0, 60, 100, 5);
    e.render(l, r, 256);
    REQUIRE(e.activeVoices() == 0);
}

TEST_CASE("Pitch bend and wheel per track", "[engine][routing]") {
    Engine e;
    e.prepare(config());
    setMode(e, ymengine::VoiceMode::Studio16);
    e.setTrack(0, track());
    e.setTrack(1, track());
    float l[512], r[512];
    e.noteOn(0, 69, 127, 0);
    e.noteOn(0, 69, 127, 1);
    e.render(l, r, 512);
    auto period = [&](int tr) {
        for (int i = 0; i < Engine::kVoices; ++i)
            if (e.voice(i).on && e.voice(i).track == tr) return int(e.voice(i).tonePeriod);
        return -1;
    };
    REQUIRE(period(0) == 284);
    REQUIRE(period(1) == 284);
    e.pitchBend(0, 2, 1);
    e.render(l, r, 512);
    REQUIRE(period(0) == 284);
    REQUIRE(period(1) == int(ymengine::tonePeriodFor(ymengine::noteHz(71), ymcore::clocks::kDefault)));
    e.pitchBend(0, -1);  // all the tracks
    e.render(l, r, 512);
    const int down = int(ymengine::tonePeriodFor(ymengine::noteHz(68), ymcore::clocks::kDefault));
    REQUIRE(period(0) == down);
    REQUIRE(period(1) == down);
}

TEST_CASE("Wheel: vibrato added at the VBL cadence, exact return to the pitch when it drops back", "[engine][wheel]") {
    for (const auto gen : {ymengine::Generator::Chip, ymengine::Generator::Sid}) {
        Engine e;
        e.prepare(config());
        setMode(e, ymengine::VoiceMode::Studio16);
        auto t = track();
        t.patch.generator = gen;
        t.patch.vibratoHz = 6;
        e.setTrack(0, t);
        float l[480], r[480];
        e.noteOn(0, 69, 127);
        e.render(l, r, 480);
        const auto base = e.voice(0);
        REQUIRE(base.tonePeriod == 284);
        // Without the wheel: fixed pitch for one second.
        for (int k = 0; k < 100; ++k) {
            e.render(l, r, 480);
            REQUIRE(e.voice(0).tonePeriod == 284);
        }
        ymengine::GlobalParams g = e.global();
        g.expressCents = 100;  // Wheel depth
        e.setGlobal(g);
        e.modWheel(0, 1);
        int lo = 4096, hi = 0;
        for (int k = 0; k < 100; ++k) {
            e.render(l, r, 480);
            lo = std::min<int>(lo, e.voice(0).tonePeriod);
            hi = std::max<int>(hi, e.voice(0).tonePeriod);
        }
        // ±100 cents around 284: 284 × 2^(∓1/12).
        REQUIRE(lo <= 269);
        REQUIRE(hi >= 300);
        e.modWheel(0, 0);
        e.render(l, r, 480);
        e.render(l, r, 480);
        REQUIRE(e.voice(0).tonePeriod == 284);
        REQUIRE(e.voice(0).timerHz == base.timerHz);
    }
}

TEST_CASE("Velocity: accent, 4-bit volume or ignored", "[engine][velocity]") {
    using ymengine::VelocityMode;
    auto voiceAfter = [](ymengine::VoiceMode mode, VelocityMode vm, int vel) {
        Engine e;
        e.prepare(config());
        ymengine::GlobalParams g;
        g.mode = mode;
        g.velocity = vm;
        e.setGlobal(g);
        e.setTrack(0, track());
        float l[4800], r[4800];
        e.noteOn(0, 69, vel);
        e.render(l, r, 4800);
        return std::make_pair(int(e.voice(0).volume), peak(std::vector<float>(l, l + 4800)));
    };
    using ymengine::VoiceMode;
    // 16-channel, accent: 4-bit volume intact, audio gain.
    const auto acc64 = voiceAfter(VoiceMode::Studio16, VelocityMode::Accent, 64);
    const auto acc127 = voiceAfter(VoiceMode::Studio16, VelocityMode::Accent, 127);
    REQUIRE(acc64.first == 13);
    REQUIRE(acc64.second < acc127.second * 0.7);
    // 16-channel, volume: the 4-bit level nearest to volume 13 × 64/127 (−13.4 dB): 11, without accent (D-200).
    REQUIRE(voiceAfter(VoiceMode::Studio16, VelocityMode::Volume, 64).first == 11);
    // Ignored: like velocity 127.
    const auto off = voiceAfter(VoiceMode::Studio16, VelocityMode::Off, 10);
    REQUIRE(off.first == 13);
    REQUIRE(off.second == acc127.second);
    // 3-channel: accent and volume go through the 4-bit volume; ignored: volume of the track.
    REQUIRE(voiceAfter(VoiceMode::Authentic3, VelocityMode::Accent, 64).first == 11);
    REQUIRE(voiceAfter(VoiceMode::Authentic3, VelocityMode::Volume, 64).first == 11);
    REQUIRE(voiceAfter(VoiceMode::Authentic3, VelocityMode::Off, 10).first == 13);
}

TEST_CASE("STE chain: bass and treble set without reallocation = prepared from the start", "[engine][board]") {
    for (const auto mode : {ymengine::VoiceMode::Studio16, ymengine::VoiceMode::Authentic3}) {
        auto run = [mode](bool live) {
            auto cfg = config(0b1);
            cfg.render.board.chain = ymcore::BoardChain::STE;
            if (!live) {
                cfg.render.board.bassDb = 12;
                cfg.render.board.trebleDb = -6;
            }
            Engine e;
            e.prepare(cfg);
            if (live) e.setBoardTone(12, -6);
            setMode(e, mode);
            e.setTrack(0, track());
            return renderAll(e, 9600, at(0, [](Engine& x, uint32_t o) { x.noteOn(o, 45, 127, 0); }));
        };
        const Multi a = run(true), b = run(false);
        REQUIRE(peak(a.l) > 0.05);
        REQUIRE(a.l == b.l);
        REQUIRE(a.t[0] == b.t[0]);
    }
}
