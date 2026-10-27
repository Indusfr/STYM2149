// STYM2149 — timer waves: programmable SID wave of 2 to 16 steps, Sine-SID, Sync Buzzer steps,
// three simultaneous timers on a chip in 3-channel mode.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
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

// A SID Voice track: Base below 2 steps (the prototype's square), the Custom form of len steps otherwise.
ymengine::TrackParams sid(int len = 0, std::initializer_list<uint8_t> wave = {}) {
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.mix.level = 1;
    t.patch.generator = Generator::Sid;
    t.patch.sidMinVolume = 3;
    std::copy(wave.begin(), wave.end(), t.patch.sidWave.begin());
    if (len >= 2) {
        t.patch.sidForm = ymengine::SidForm::Custom;
        t.patch.sidWaveLen = uint8_t(len);
    }
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
        // Square wave of the prototype: volume of the note (13), low volume, one toggle per interrupt, two in a period
        // of the note.
        Rig sq(ymengine::VoiceMode::Authentic3, mode);
        sq.e.setTrack(0, sid());
        sq.e.noteOn(0, 57, 127);
        sq.run(24000);
        const double sqHz = sq.e.voice(0).timerHz;
        CHECK(sqHz == 2 * ymengine::noteHz(57));
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

    // Base, the square wave, whatever its bars; above 16, 16 steps.
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
    CHECK(big.e.voice(0).timerHz == ymengine::noteHz(57) * 16);
}

TEST_CASE("SID wave: Sine-SID volumes, harmonic 3 weaker than that of the square wave",
          "[engine][timer][timerwave]") {
    // At period 16, one cycle over the 16 bars, half a step after the middle, up to the top (15) then down
    // near the trough: no step at 0. A sine on the scale of the volumes, as the bars draw it: the bars
    // of each half mirror those of the other about volume 9.
    const auto sine = ymengine::sineSidWave(3, 15, 8, 16);
    CHECK(sine == std::array<uint8_t, 16>{10, 12, 14, 15, 15, 14, 12, 10, 8, 6, 4, 3, 3, 4, 6, 8});
    for (int k = 0; k < 8; ++k) CHECK(sine[size_t(k)] + sine[size_t(k + 8)] == 18);
    CHECK(std::find(sine.begin(), sine.end(), 0) == sine.end());
    CHECK(*std::max_element(sine.begin(), sine.end()) == 15);

    // Without tone or noise, the channel only plays the volume wave: harmonics 1 and 3 are compared.
    auto ratio = [](int len, const std::array<uint8_t, ymengine::kSidWaveSteps>& wave, uint8_t low) {
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
    const double sinus = ratio(16, sine, 0);
    CAPTURE(square, sinus);
    CHECK(square > 0.3);  // 1/3 for a perfect square wave (-9.5 dB)
    // 0.20 measured: the volumes follow the sine, and the chip's level, logarithmic in the volume, peaks at the
    // top (0.013 while the volumes followed a sine of the levels; 0.08 formerly, with a step on the trough).
    CHECK(sinus < 0.25);
}

TEST_CASE("Sine-SID between two volumes (Indus, 24/10/2026 11:29)",
          "[engine][timer][timerwave]") {
    using Wave = std::array<uint8_t, ymengine::kSidWaveSteps>;
    // Its highest bars (the 4th and 5th) at the highest volume, its lowest (the 12th and 13th) at the lowest; the
    // others between them, rising then falling with the sine.
    for (const auto& [low, high] : {std::pair{0, 15}, std::pair{3, 12}, std::pair{6, 9}, std::pair{14, 15}}) {
        CAPTURE(low, high);
        const Wave w = ymengine::sineSidWave(low, high, 8, 16);  // one cycle over the 16 bars
        CHECK(w[3] == high);
        CHECK(w[4] == high);
        CHECK(w[11] == low);
        CHECK(w[12] == low);
        CHECK(*std::min_element(w.begin(), w.end()) == low);
        CHECK(*std::max_element(w.begin(), w.end()) == high);
        for (int k = 0; k < 3; ++k) CHECK(w[size_t(k)] <= w[size_t(k + 1)]);
        for (int k = 4; k < 11; ++k) CHECK(w[size_t(k)] >= w[size_t(k + 1)]);
        for (int k = 12; k < 15; ++k) CHECK(w[size_t(k)] <= w[size_t(k + 1)]);
    }
}

TEST_CASE("Sine-SID at a phase, a bar moved shifts it (Indus, 24/10/2026 14:07-14:08)",
          "[engine][timer][timerwave]") {
    using Wave = std::array<uint8_t, ymengine::kSidWaveSteps>;
    // At period 16 (one cycle over the 16 bars), phase 8, the default, is the former sine; each phase moves its bars
    // one place to the right, all around.
    const Wave sine = ymengine::sineSidWave(3, 15, 8, 16);
    CHECK(ymengine::kSidWavePhase == 8);
    for (const auto& [low, high] : {std::pair{3, 15}, std::pair{0, 15}, std::pair{6, 9}}) {
        const Wave at8 = ymengine::sineSidWave(low, high, 8, 16);
        for (int phase = 0; phase < ymengine::kSidWaveSteps; ++phase) {
            CAPTURE(low, high, phase);
            const Wave w = ymengine::sineSidWave(low, high, phase, 16);
            for (int k = 0; k < ymengine::kSidWaveSteps; ++k)
                CHECK(w[size_t(k)] == at8[size_t((k - (phase - 8) + 16) % 16)]);
            // Its volumes stay: a phase moves the sine, never its height.
            CHECK(ymengine::sineSidWaveAt(sine, phase, 16) == ymengine::sineSidWave(3, 15, phase, 16));
        }
    }
    CHECK(ymengine::sineSidWave(3, 15, 9, 16)[4] == 15);  // the peak on the 5th and 6th bars
    CHECK(ymengine::sineSidWave(3, 15, 9, 16)[5] == 15);
    CHECK(ymengine::sineSidWave(3, 15, 0, 16) == ymengine::sineSidWave(3, 15, 16, 16));

    // A bar moved: the phase that brings it nearest the volume asked for, the nearest the present one on a tie.
    for (int k = 0; k < ymengine::kSidWaveSteps; ++k) {
        CAPTURE(k);
        CHECK(ymengine::sineSidWavePhase(sine, 8, 16, k, sine[size_t(k)]) == 8);  // put back: nothing moves
    }
    // The bars of phase 8: 10 12 14 15 15 14 12 10 8 6 4 3 3 4 6 8. The 10th bar, falling, raised to the
    // volume of the 9th: the sine one bar to the right; lowered to the volume of the 11th: one bar to the left.
    CHECK(sine == Wave{10, 12, 14, 15, 15, 14, 12, 10, 8, 6, 4, 3, 3, 4, 6, 8});
    CHECK(ymengine::sineSidWavePhase(sine, 8, 16, 9, sine[8]) == 9);
    CHECK(ymengine::sineSidWavePhase(sine, 8, 16, 9, sine[10]) == 7);
    // The 12th, at the trough, raised to the 11th's volume: one bar to the right; the 13th raised to the 14th's, one
    // bar to the left.
    CHECK(ymengine::sineSidWavePhase(sine, 8, 16, 11, sine[10]) == 9);
    CHECK(ymengine::sineSidWavePhase(sine, 8, 16, 12, sine[13]) == 7);
    // Raised to the peak: the nearest of the phases that put a volume 15 there (7 bars to the right).
    CHECK(ymengine::sineSidWavePhase(sine, 8, 16, 11, 15) == 15);
    CHECK(ymengine::sineSidWaveAt(sine, 15, 16)[11] == 15);
    // A volume no sine reaches: the bar nearest it, at its extreme.
    CHECK(ymengine::sineSidWaveAt(sine, ymengine::sineSidWavePhase(sine, 8, 16, 6, 0), 16)[6] == 3);
    // Around the 16: from phase 0, one bar to the left is phase 15.
    CHECK(ymengine::sineSidWavePhase(sine, 0, 16, 0, ymengine::sineSidWave(3, 15, 15, 16)[0]) == 15);
}

TEST_CASE("Sine-SID period, 8 bars by default (Indus, 24/10/2026 15:41)", "[engine][timer][timerwave]") {
    using Wave = std::array<uint8_t, ymengine::kSidWaveSteps>;
    // By default, two cycles of 8 bars from volume 3 to 15, peaks on the 2nd and 3rd bars, no step at 0.
    CHECK(ymengine::kSidWavePeriod == 8);
    const Wave sine = ymengine::sineSidWave();
    CHECK(sine == ymengine::sineSidWave(3, 15, 8, 8));
    CHECK(sine == Wave{11, 15, 15, 11, 7, 3, 3, 7, 11, 15, 15, 11, 7, 3, 3, 7});
    for (int period = ymengine::kSidWavePeriodMin; period <= ymengine::kSidWaveSteps; ++period)
        for (const auto& [low, high] : {std::pair{3, 15}, std::pair{0, 15}, std::pair{6, 9}}) {
            CAPTURE(period, low, high);
            const Wave w = ymengine::sineSidWave(low, high, 8, period);
            // A cycle every `period` bars, between the two volumes.
            CHECK(*std::min_element(w.begin(), w.end()) == low);
            CHECK(*std::max_element(w.begin(), w.end()) == high);
            for (int k = 0; k + period < ymengine::kSidWaveSteps; ++k) CHECK(w[size_t(k)] == w[size_t(k + period)]);
            // A phase moves it one bar per step; `period` bars further, the same wave.
            for (int phase = 0; phase + period < ymengine::kSidWaveSteps; ++phase)
                CHECK(ymengine::sineSidWave(low, high, phase, period) ==
                      ymengine::sineSidWave(low, high, phase + period, period));
            CHECK(ymengine::sineSidWaveAt(w, 8, period) == w);
        }
    // Out of range, the nearest period.
    CHECK(ymengine::sineSidWave(3, 15, 8, 1) == ymengine::sineSidWave(3, 15, 8, 2));
    CHECK(ymengine::sineSidWave(3, 15, 8, 40) == ymengine::sineSidWave(3, 15, 8, 16));
    // A bar put back: nothing moves; the 2nd bar (a peak) lowered to the volume of the 1st: one bar to the right.
    for (int k = 0; k < ymengine::kSidWaveSteps; ++k) {
        CAPTURE(k);
        CHECK(ymengine::sineSidWavePhase(sine, 8, 8, k, sine[size_t(k)]) == 8);
    }
    CHECK(ymengine::sineSidWavePhase(sine, 8, 8, 1, sine[0]) == 9);

    // Played: with Steps 8, one cycle per period of the note, at its pitch; with Steps 16, two cycles, the octave
    // above (the n steps of a SID wave last one period of the note).
    auto play = [](int len, const Wave& wave) {
        Rig r(ymengine::VoiceMode::Studio16);
        ymengine::TrackParams t = sid(len);
        std::copy(wave.begin(), wave.end(), t.patch.sidWave.begin());
        t.patch.sidMinVolume = 0;
        t.patch.volume = 15;
        t.patch.toneOn = false;
        t.patch.noiseOn = false;
        r.e.setTrack(0, t);
        r.e.noteOn(0, 69, 127);
        r.run(4800);
        r.run(48000, true);
        const double f = r.e.voice(0).timerActualHz / len;  // the note
        return std::pair{component(r.out, f), component(r.out, 2 * f)};
    };
    const auto [one1, one2] = play(8, sine);
    const auto [two1, two2] = play(16, sine);
    CAPTURE(one1, one2, two1, two2);
    CHECK(one2 < 0.6 * one1);  // 0.51 measured, the level peaking at the top
    CHECK(two1 < 0.05 * two2);
}

TEST_CASE("Bounce-SID: the Sine-SID on the chip's levels (Indus, 24/10/2026 18:41)",
          "[engine][timer][timerwave]") {
    using Wave = std::array<uint8_t, ymengine::kSidWaveSteps>;
    // The former Sine-SID: the volumes whose levels best follow the sine, packed at the top by the logarithmic
    // level, which the bars draw as a bounce; no step at 0.
    const Wave one = ymengine::sineSidWave(3, 15, 8, 16, true);
    CHECK(one == Wave{14, 14, 15, 15, 15, 15, 14, 14, 13, 11, 8, 3, 3, 8, 11, 13});
    const Wave bounce = ymengine::sineSidWave(3, 15, ymengine::kSidWavePhase, ymengine::kSidWavePeriod, true);
    CHECK(bounce == Wave{14, 15, 15, 14, 12, 3, 3, 12, 14, 15, 15, 14, 12, 3, 3, 12});
    CHECK(bounce != ymengine::sineSidWave());
    // Its phase, period and volumes as those of the Sine: a cycle every `period` bars, between the two volumes, a bar
    // put back moving nothing, the peak lowered to the volume of the bar before it shifting it one bar to the right.
    for (int period = ymengine::kSidWavePeriodMin; period <= ymengine::kSidWaveSteps; ++period)
        for (const auto& [low, high] : {std::pair{3, 15}, std::pair{0, 15}, std::pair{6, 9}}) {
            CAPTURE(period, low, high);
            const Wave w = ymengine::sineSidWave(low, high, 8, period, true);
            CHECK(*std::min_element(w.begin(), w.end()) == low);
            CHECK(*std::max_element(w.begin(), w.end()) == high);
            for (int k = 0; k + period < ymengine::kSidWaveSteps; ++k) CHECK(w[size_t(k)] == w[size_t(k + period)]);
            CHECK(ymengine::sineSidWaveAt(w, 8, period, true) == w);
        }
    for (int k = 0; k < ymengine::kSidWaveSteps; ++k) {
        CAPTURE(k);
        CHECK(ymengine::sineSidWavePhase(one, 8, 16, k, one[size_t(k)], true) == 8);
    }
    CHECK(ymengine::sineSidWavePhase(one, 8, 16, 9, one[8], true) == 9);
    CHECK(ymengine::sineSidWaveAt(one, 9, 16, true) == ymengine::sineSidWave(3, 15, 9, 16, true));

    // Played as its bars, like Sine and Custom: the timer Steps times the note. Heard, a sine: harmonic 3 at
    // 0.013 of the fundamental over 16 Steps (0.20 for the former Sine), harmonic 2 under 5 % over 8 (0.51).
    auto play = [](int len, const Wave& wave, int harmonic) {
        Rig r(ymengine::VoiceMode::Studio16);
        ymengine::TrackParams t = sid(len);
        t.patch.sidForm = ymengine::SidForm::Bounce;
        t.patch.sidWave = wave;
        t.patch.sidMinVolume = 0;
        t.patch.volume = 15;
        t.patch.toneOn = false;
        t.patch.noiseOn = false;
        r.e.setTrack(0, t);
        r.e.noteOn(0, 69, 127);
        r.run(4800);
        CHECK(r.e.voice(0).timerHz == ymengine::noteHz(69) * len);
        r.run(48000, true);
        const double f = r.e.voice(0).timerActualHz / len;  // the note
        return component(r.out, harmonic * f) / component(r.out, f);
    };
    const double third = play(16, one, 3), second = play(8, bounce, 2);
    CAPTURE(third, second);
    CHECK(third < 0.03);
    CHECK(second < 0.05);
}

TEST_CASE("SID Square: the note's volume over Width of its Steps (Indus, 24/10/2026 16:00 and 17:47)",
          "[engine][3ch][timer][timerwave]") {
    const ymengine::TrackPatch def;
    CHECK(def.sidForm == ymengine::SidForm::Base);
    CHECK(def.sidWaveLen == 8);
    CHECK(def.sidWidth == 4);
    for (const auto mode : {ymengine::TimerMode::Free, ymengine::TimerMode::Mfp}) {
        CAPTURE(int(mode));
        // Base: the prototype's square, one toggle per interrupt, two in a period of the note, whatever its
        // Steps and Width.
        Rig sq(ymengine::VoiceMode::Authentic3, mode);
        ymengine::TrackParams base = sid();
        base.patch.sidWaveLen = 12;
        base.patch.sidWidth = 3;
        sq.e.setTrack(0, base);
        sq.e.noteOn(0, 57, 127);
        sq.run(24000);
        const double sqHz = sq.e.voice(0).timerHz;
        CHECK(sqHz == 2 * ymengine::noteHz(57));
        const Log s8 = sq.writes(8);
        REQUIRE(s8.size() >= 61);
        for (size_t i = 0; i < 61; ++i) REQUIRE(s8[i].value == (i % 2 ? 3 : 13));
        const double sqPeriod = double(s8[60].cycle - s8[0].cycle) / 30;
        // Square: Steps interrupts per period of the square, the note's volume (13) over the first Width of them,
        // brought to 1 to Steps - 1, SID min vol (3) over the others.
        struct Case {
            int steps, width, played;
        };
        for (const Case c : {Case{16, 1, 1}, Case{16, 4, 4}, Case{16, 12, 12}, Case{16, 15, 15}, Case{16, 0, 1},
                             Case{16, 40, 15}, Case{8, 4, 4}, Case{8, 40, 7}, Case{5, 2, 2}, Case{2, 9, 1}}) {
            CAPTURE(c.steps, c.width);
            Rig w(ymengine::VoiceMode::Authentic3, mode);
            ymengine::TrackParams t = sid();
            t.patch.sidForm = ymengine::SidForm::Square;
            t.patch.sidWaveLen = uint8_t(c.steps);
            t.patch.sidWidth = uint8_t(c.width);
            w.e.setTrack(0, t);
            w.e.noteOn(0, 57, 127);
            w.run(24000);
            CHECK(w.e.voice(0).timerHz == sqHz * c.steps / 2);
            const Log w8 = w.writes(8);
            REQUIRE(w8.size() >= 61);
            for (size_t i = 0; i < 61; ++i) {
                CAPTURE(i);
                REQUIRE(w8[i].value == (i % 2 ? 3 : 13));
            }
            double high = 0;
            for (size_t i = 0; i < 60; i += 2) high += double(w8[i + 1].cycle - w8[i].cycle);
            const double period = double(w8[60].cycle - w8[0].cycle);
            CHECK(std::fabs(high / period - double(c.played) / c.steps) < 0.005);
            CHECK(std::fabs(period / 30 - sqPeriod) / sqPeriod < (mode == ymengine::TimerMode::Mfp ? 0.02 : 1e-6));
        }
    }
    // Custom plays its bars whatever the width.
    Rig wave(ymengine::VoiceMode::Authentic3);
    ymengine::TrackParams t = sid(4, {15, 0, 9, 4});
    t.patch.sidWidth = 3;
    wave.e.setTrack(0, t);
    wave.e.noteOn(0, 57, 127);
    wave.run(4800);
    CHECK(wave.e.voice(0).timerHz == ymengine::noteHz(57) * 4);
    const Log v8 = wave.writes(8);
    REQUIRE(v8.size() >= 8);
    for (size_t i = 0; i < 8; ++i) CHECK(v8[i].value == std::array<uint8_t, 4>{13, 0, 8, 3}[i % 4]);
}

TEST_CASE("Recorder: every form of the SID wave keeps the timer of its square",
          "[engine][3ch][timer][timerwave][recorder]") {
    using ymengine::SidForm;
    for (const auto mode : {ymengine::TimerMode::Free, ymengine::TimerMode::Mfp}) {
        CAPTURE(int(mode));
        auto taken = [mode](SidForm form, int len, int width) {
            Rig r(ymengine::VoiceMode::Authentic3, mode);
            ymengine::TrackParams t = sid(0, {15, 0, 9, 4, 15, 0, 9, 4, 15, 0, 9, 4, 15, 0, 9, 4});
            t.patch.sidForm = form;
            t.patch.sidWaveLen = uint8_t(len);
            t.patch.sidWidth = uint8_t(width);
            r.e.setTrack(0, t);
            std::vector<ymengine::RegisterFrame> frames(16);
            r.e.startCapture(0, frames);
            r.e.noteOn(0, 57, 127);
            r.run(4800);
            REQUIRE(r.e.capturedFrames() >= 4);
            const ymengine::RegisterFrame::Effect e = frames[3].fx[0];
            CHECK(e.on);
            CHECK(e.gen == Generator::Sid);
            return e.timer;
        };
        const ymengine::MfpSetting square = taken(SidForm::Base, 8, 4);
        CHECK(std::fabs(square.hz() - 2 * ymengine::noteHz(57)) / ymengine::noteHz(57) < 0.02);
        struct Case {
            SidForm form;
            int len, width;
        };
        for (const Case c : {Case{SidForm::Square, 16, 4}, Case{SidForm::Square, 16, 15}, Case{SidForm::Square, 8, 4},
                             Case{SidForm::Custom, 16, 8}, Case{SidForm::Custom, 4, 8}, Case{SidForm::Sine, 16, 4},
                             Case{SidForm::Bounce, 8, 4}}) {
            CAPTURE(int(c.form), c.len, c.width);
            CHECK(std::fabs(taken(c.form, c.len, c.width).hz() - square.hz()) / square.hz() < 0.01);
        }
    }
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
    r.e.noteOn(0, 45, 127);  // 110 Hz: four restarts every 9 ms
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

TEST_CASE("Sync Buzzer: the steps fit in one period of the note, which keeps its pitch",
          "[engine][timer][timerwave]") {
    for (const auto vm : {ymengine::VoiceMode::Authentic3, ymengine::VoiceMode::Studio16}) {
        for (const auto mode : {ymengine::TimerMode::Free, ymengine::TimerMode::Mfp}) {
            CAPTURE(int(vm), int(mode));
            auto buzz = [&](int len, bool tuned) {
                ymengine::TrackParams t;
                t.mix.enabled = true;
                t.patch.generator = Generator::SyncBuzzer;
                t.patch.envShape = 0xC;
                t.patch.buzzShapes = {0xC, 0xA, 0x8, 0xE, 0x8, 0x8, 0x8, 0x8};
                t.patch.buzzLen = uint8_t(len);
                t.patch.envTuned = tuned;
                return t;
            };
            // C#4 held: four steps, the timer goes four times faster than without steps, and the cycle of the
            // four shapes lasts one period of the note.
            Rig one(vm, mode);
            one.e.setTrack(0, buzz(0, false));
            one.e.noteOn(0, 61, 127);
            one.run(24000);
            const double hz = one.e.voice(0).timerHz;
            CHECK(hz == ymengine::noteHz(61));
            Rig four(vm, mode);
            four.e.setTrack(0, buzz(4, false));
            four.e.noteOn(0, 61, 127);
            four.run(24000);
            CHECK(four.e.voice(0).timerHz == hz * 4);
            if (vm == ymengine::VoiceMode::Authentic3) {  // the write log holds the registers of the 3-channel chip
                const Log q = one.writes(13), w = four.writes(13);
                REQUIRE(q.size() >= 31);
                REQUIRE(w.size() >= 121);
                const double period = double(q[q.size() - 1].cycle - q[q.size() - 31].cycle) / 30;  // one restart
                const double cycle = double(w[w.size() - 1].cycle - w[w.size() - 121].cycle) / 30;  // four steps
                CHECK(std::fabs(cycle - period) / period < (mode == ymengine::TimerMode::Mfp ? 0.02 : 1e-6));
            }
            // A tuned envelope follows the step: its period is four times shorter.
            Rig t1(vm, mode), t4(vm, mode);
            t1.e.setTrack(0, buzz(0, true));
            t4.e.setTrack(0, buzz(4, true));
            t1.e.noteOn(0, 61, 127);
            t4.e.noteOn(0, 61, 127);
            t1.run(480);
            t4.run(480);
            const double ep1 = t1.e.voice(0).envPeriod, ep4 = t4.e.voice(0).envPeriod;
            CAPTURE(ep1, ep4);
            CHECK(std::fabs(ep1 / ep4 - 4) < 0.1);
        }
    }
}

TEST_CASE("Timers: three SID voices on one 3-channel chip, one timer each", "[engine][3ch][timer][timerwave]") {
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
