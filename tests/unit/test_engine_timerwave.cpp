// STYM2149 — timer waves (M6, B6, D-151): programmable SID wave of 2 to 16 steps, Sine-SID, Sync Buzzer steps,
// three simultaneous timers on a chip in 3-channel mode.
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
using ymengine::Generator;

namespace {

using Log = std::vector<Engine::WriteRecord>;

struct Rig {
    Engine e;
    Log log;
    std::vector<float> l = std::vector<float>(1 << 16), r = std::vector<float>(1 << 16);
    std::vector<float> out;

    explicit Rig(ymengine::VoiceMode mode, ymengine::TimerMode timer = ymengine::TimerMode::Free) {
        ymengine::EngineConfig cfg;
        cfg.render.hostRate = 48000;
        e.prepare(cfg);
        ymengine::GlobalParams g;
        g.mode = mode;
        g.timer = timer;
        e.setGlobal(g);
        log.reserve(1 << 20);
        e.setWriteLog(&log);
    }
    void run(uint32_t n, bool keep = false) {
        while (n > 0) {
            const uint32_t k = std::min<uint32_t>(n, uint32_t(l.size()));
            e.render(l.data(), r.data(), k);
            if (keep) out.insert(out.end(), l.begin(), l.begin() + k);
            n -= k;
        }
    }
    Log writes(uint8_t reg, uint8_t chip = 0) const {
        Log w;
        for (const auto& x : log)
            if (x.reg == reg && x.chip == chip) w.push_back(x);
        return w;
    }
};

ymengine::TrackParams sid(int len = 0, std::initializer_list<uint8_t> wave = {}) {
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.mix.level = 1;
    t.patch.generator = Generator::Sid;
    t.patch.sidMinVolume = 3;
    std::copy(wave.begin(), wave.end(), t.patch.sidWave.begin());
    t.patch.sidWaveLen = uint8_t(len);
    return t;
}

// Amplitude of the component at frequency hz (Hann window).
double component(const std::vector<float>& x, double hz) {
    const double kPi = 3.141592653589793;
    double re = 0, im = 0;
    const size_t n = x.size();
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n - 1));
        const double a = 2 * kPi * hz * double(i) / 48000;
        re += w * x[i] * std::cos(a);
        im += w * x[i] * std::sin(a);
    }
    return std::hypot(re, im);
}

}  // namespace

TEST_CASE("SID wave: one volume per interrupt, scaled by the volume of the note, at the pitch of the square wave",
          "[engine][3ch][timer][timerwave]") {
    for (const auto mode : {ymengine::TimerMode::Free, ymengine::TimerMode::Mfp}) {
        CAPTURE(int(mode));
        // Square wave of the prototype: volume of the note (13), low volume, one toggle per interrupt.
        Rig sq(ymengine::VoiceMode::Authentic3, mode);
        sq.e.setTrack(0, sid());
        sq.e.noteOn(0, 57, 127);
        sq.run(24000);
        const double sqHz = sq.e.voice(0).timerHz;
        CHECK(sqHz == ymengine::noteHz(57));
        const Log s8 = sq.writes(8);
        REQUIRE(s8.size() >= 60);
        const double sqPeriod = double(s8[s8.size() - 1].cycle - s8[s8.size() - 61].cycle) / 30;  // two toggles

        // Four steps: the timer goes twice as fast, the wave keeps the period of the square wave.
        Rig w(ymengine::VoiceMode::Authentic3, mode);
        w.e.setTrack(0, sid(4, {15, 0, 9, 4}));
        w.e.noteOn(0, 57, 127);
        w.run(24000);
        CHECK(w.e.voice(0).timerHz == sqHz * 2);
        const Log w8 = w.writes(8);
        REQUIRE(w8.size() >= 121);
        const uint8_t want[] = {13, 0, 8, 3};  // (step × 13 + 7) / 15
        for (size_t i = 0; i < w8.size(); ++i) {
            CAPTURE(i);
            REQUIRE(w8[i].value == want[i % 4]);
        }
        const double wPeriod = double(w8[w8.size() - 1].cycle - w8[w8.size() - 121].cycle) / 30;  // four steps
        CHECK(std::fabs(wPeriod - sqPeriod) / sqPeriod < (mode == ymengine::TimerMode::Mfp ? 0.02 : 1e-6));
    }

    // A single step does not make a wave: length 1 keeps the square wave; above 16, 16 steps.
    Rig one(ymengine::VoiceMode::Authentic3);
    one.e.setTrack(0, sid(1, {9}));
    one.e.noteOn(0, 57, 127);
    one.run(4800);
    const Log o8 = one.writes(8);
    REQUIRE(o8.size() >= 20);
    for (size_t i = 0; i < o8.size(); ++i) REQUIRE(o8[i].value == (i % 2 ? 3 : 13));
    Rig big(ymengine::VoiceMode::Authentic3);
    big.e.setTrack(0, sid(40, {15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 0, 15, 1}));
    big.e.noteOn(0, 57, 127);
    big.run(4800);
    CHECK(big.e.voice(0).timerHz == ymengine::noteHz(57) * 8);
}

TEST_CASE("SID wave: Sine-SID volumes, harmonic 3 much weaker than that of the square wave",
          "[engine][timer][timerwave]") {
    const auto sine = ymengine::sineSidWave();
    // From the middle (13: the half-way level of the mixing table) to the top (15) then to the trough (0).
    CHECK(sine == std::array<uint8_t, 8>{13, 15, 15, 15, 13, 10, 0, 10});

    // Without tone or noise, the channel only plays the volume wave: harmonics 1 and 3 are compared.
    auto ratio = [](int len, const std::array<uint8_t, 8>& wave, uint8_t low) {
        Rig r(ymengine::VoiceMode::Studio16);
        ymengine::TrackParams t = sid(len);
        std::copy(wave.begin(), wave.end(), t.patch.sidWave.begin());
        t.patch.sidMinVolume = low;
        t.patch.volume = 15;
        t.patch.toneOn = false;
        t.patch.noiseOn = false;
        r.e.setTrack(0, t);
        r.e.noteOn(0, 69, 127);
        r.run(4800);
        r.run(48000, true);
        const double f = r.e.voice(0).timerActualHz / (len ? len : 2);
        return component(r.out, 3 * f) / component(r.out, f);
    };
    const double square = ratio(0, {}, 0);
    const double sinus = ratio(8, sine, 0);
    CAPTURE(square, sinus);
    CHECK(square > 0.3);  // 1/3 for a perfect square wave (-9.5 dB)
    CHECK(sinus < 0.1);   // -20 dB and lower (0.08 measured: the volumes of the chip are not linear)
}

TEST_CASE("Sync Buzzer: the steps write their R13 shapes in turn, 0: the shape of the track",
          "[engine][3ch][timer][timerwave]") {
    Rig r(ymengine::VoiceMode::Authentic3);
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.patch.generator = Generator::SyncBuzzer;
    t.patch.envShape = 0xC;
    t.patch.buzzShapes = {0x8, 0xA, 0, 0xE, 0x8, 0x8, 0x8, 0x8};
    t.patch.buzzLen = 4;
    r.e.setTrack(0, t);
    r.e.noteOn(0, 45, 127);  // 110 Hz: one restart every 9 ms
    r.run(24000);
    const Log w = r.writes(13);
    REQUIRE(w.size() >= 40);
    const uint8_t want[] = {0x8, 0xA, 0xC, 0xE};
    for (size_t i = 0; i < w.size(); ++i) {
        CAPTURE(i);
        REQUIRE(w[i].value == want[i % 4]);
    }
    // A new note starts again from the first step.
    r.log.clear();
    r.e.noteOn(0, 47, 127);
    r.run(480);
    REQUIRE(r.writes(13).size() >= 2);
    CHECK(r.writes(13)[0].value == 0x8);
    CHECK(r.writes(13)[1].value == 0xA);

    // Without steps: the shape of the track at each restart, like the prototype.
    Rig p(ymengine::VoiceMode::Authentic3);
    t.patch.buzzLen = 0;
    p.e.setTrack(0, t);
    p.e.noteOn(0, 45, 127);
    p.run(24000);
    const Log q = p.writes(13);
    REQUIRE(q.size() >= 40);
    for (const auto& x : q) REQUIRE(x.value == 0xC);
}

TEST_CASE("Timers: three SID voices on one 3-channel chip, one timer each (D-138)", "[engine][3ch][timer][timerwave]") {
    Rig r(ymengine::VoiceMode::Authentic3, ymengine::TimerMode::Mfp);
    r.e.setTrack(0, sid());
    r.e.setTrack(1, sid(4, {15, 0, 9, 4}));
    r.e.setTrack(2, sid());
    r.e.noteOn(0, 57, 127, 0);
    r.e.noteOn(0, 64, 127, 1);
    r.e.noteOn(0, 69, 127, 2);
    r.run(48000);
    int seen = 0;
    for (int i = 0; i < 3; ++i) {
        const auto v = r.e.voice(i);
        REQUIRE(v.on);
        REQUIRE(v.gen == Generator::Sid);
        const Log vol = r.writes(uint8_t(8 + i));
        CAPTURE(i, v.track, v.timerActualHz, vol.size());
        // One volume write per interrupt, at the pace of the timer of the voice.
        CHECK(std::fabs(double(vol.size()) - v.timerActualHz) < 0.01 * v.timerActualHz + 2);
        seen |= 1 << v.track;
    }
    CHECK(seen == 7);
}
