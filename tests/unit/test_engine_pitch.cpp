// STYM2149 — pitch of every generator: the note of the key or of the sequencer, whatever the generator (Indus,
// 25/10/2026 17:39, D-426).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "ymengine/Engine.h"
#include "ymengine/Pitch.h"

using namespace stym;
using ymengine::Generator;
using ymengine::SidForm;
using ymengine::TimerMode;
using ymengine::VoiceMode;

namespace {

constexpr double kRate = 48000;

// 0.5 s of the left output of a track, after 0.2 s.
std::vector<float> play(VoiceMode mode, TimerMode timer, const ymengine::TrackParams& t, int note,
                        const std::function<void(ymengine::Engine&, ymengine::GlobalParams&)>& setup = {}) {
    ymengine::Engine e;
    ymengine::EngineConfig cfg;
    cfg.render.hostRate = uint32_t(kRate);
    e.prepare(cfg);
    ymengine::GlobalParams g;
    g.mode = mode;
    g.timer = timer;
    if (setup) setup(e, g);
    e.setGlobal(g);
    e.setTrack(0, t);
    e.noteOn(0, note, 127, 0);
    std::vector<float> l(4800), r(4800), out;
    for (int k = 0; k < 2; ++k) e.render(l.data(), r.data(), 4800);
    for (int k = 0; k < 5; ++k) {
        e.render(l.data(), r.data(), 4800);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}

// Frequency at which the signal repeats: the shortest lag whose normalised autocorrelation comes within 0.03 of the
// best one between an octave and a half below and three octaves above `near`, refined by a parabola.
double periodHz(const std::vector<float>& x, double near) {
    const int lo = int(kRate / (near * 8)), hi = int(kRate * 3 / near);
    const size_t n = x.size() - size_t(hi) - 2;
    std::vector<double> r(size_t(hi) + 2, 0);
    double e0 = 0;
    for (size_t i = 0; i < n; ++i) e0 += double(x[i]) * x[i];
    double best = 0;
    for (int lag = lo; lag <= hi + 1; ++lag) {
        double s = 0, e = 0;
        for (size_t i = 0; i < n; ++i) {
            s += double(x[i]) * x[i + size_t(lag)];
            e += double(x[i + size_t(lag)]) * x[i + size_t(lag)];
        }
        r[size_t(lag)] = s / std::sqrt(e0 * e + 1e-30);
        best = std::max(best, r[size_t(lag)]);
    }
    for (int lag = lo + 1; lag <= hi; ++lag) {
        const double a = r[size_t(lag) - 1], b = r[size_t(lag)], c = r[size_t(lag) + 1];
        if (b >= best - 0.03 && b >= a && b >= c) {
            const double d = (a - c) / (2 * (a - 2 * b + c));
            return kRate / (lag + (std::isfinite(d) ? d : 0));
        }
    }
    return 0;
}

// Distance in semitones between the pitch of the output and hz.
double semitones(const std::vector<float>& x, double hz) {
    const double p = periodHz(x, hz);
    return p > 0 ? 12 * std::log2(p / hz) : 99;
}

ymengine::TrackParams track(Generator g) {
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.mix.level = 1;
    t.patch.generator = g;
    t.patch.sidMinVolume = 3;
    return t;
}

// The SID Voice with a form of its wave, as the menu sets it from Base: 8 Steps, the Width half of them, the Sine and
// the Bounce over their Period of 8 bars (D-411, D-413, D-426), Custom one cycle of a square drawn on 8 bars.
ymengine::TrackParams sid(SidForm form) {
    ymengine::TrackParams t = track(Generator::Sid);
    t.patch.sidForm = form;
    t.patch.sidWaveLen = 8;
    if (ymengine::sineForm(form))
        t.patch.sidWave = ymengine::sineSidWave(3, 15, ymengine::kSidWavePhase, 8, form == SidForm::Bounce);
    if (form == SidForm::Custom) t.patch.sidWave = {15, 15, 15, 15, 3, 3, 3, 3};
    return t;
}

constexpr VoiceMode kModes[] = {VoiceMode::Studio16, VoiceMode::Authentic3, VoiceMode::Dual3};

}  // namespace

TEST_CASE("Pitch: Chip Voice, SID Voice in every form and Sync Buzzer play the note of the key (D-426)",
          "[engine][pitch][d426]") {
    struct Case {
        const char* name;
        ymengine::TrackParams t;
    };
    std::vector<Case> cases = {{"Chip Voice", track(Generator::Chip)},
                               {"SID Base", sid(SidForm::Base)},
                               {"SID Square", sid(SidForm::Square)},
                               {"SID Sine", sid(SidForm::Sine)},
                               {"SID Bounce", sid(SidForm::Bounce)},
                               {"SID Custom", sid(SidForm::Custom)},
                               {"Sync Buzzer", track(Generator::SyncBuzzer)}};
    Case tuned{"Sync Buzzer tuned", track(Generator::SyncBuzzer)};
    tuned.t.patch.envTuned = true;
    cases.push_back(tuned);
    Case steps{"Sync Buzzer 4 steps", track(Generator::SyncBuzzer)};
    steps.t.patch.buzzLen = 4;
    steps.t.patch.buzzShapes = {0xC, 0xA, 0x8, 0xE};
    cases.push_back(steps);
    Case toneEnv{"Chip Voice Tone+Env", track(Generator::Chip)};
    toneEnv.t.patch.envelopeVolume = true;
    cases.push_back(toneEnv);

    for (const VoiceMode mode : kModes)
        for (const TimerMode timer : {TimerMode::Free, TimerMode::Mfp})
            for (const auto& c : cases)
                for (const int note : {36, 48, 60, 72, 84}) {
                    CAPTURE(int(mode), int(timer), c.name, note);
                    // The MFP rounds the timer to its dividers (an ST): a few hundredths of a semitone up to C6.
                    const double off = semitones(play(mode, timer, c.t, note), ymengine::noteHz(note));
                    CHECK(std::fabs(off) < (timer == TimerMode::Mfp ? 0.15 : 0.08));
                }
}

TEST_CASE("Pitch: the SID ratio moves the note by its factor, 2 one octave higher (D-426)", "[engine][pitch][d426]") {
    for (const VoiceMode mode : kModes)
        for (const double ratio : {0.5, 1.0, 2.0, 1.5}) {
            CAPTURE(int(mode), ratio);
            ymengine::TrackParams t = sid(SidForm::Square);
            t.patch.toneOn = false;  // the SID wave alone: with the tone, ratio 1.5 repeats at the note / 2
            t.patch.sidRatio = ratio;
            const double off = semitones(play(mode, TimerMode::Free, t, 60), ymengine::noteHz(60) * ratio);
            CHECK(std::fabs(off) < 0.08);
        }
}

TEST_CASE("Pitch: the Steps of a Sine hold its cycles, two cycles an octave higher (D-406, D-426)",
          "[engine][pitch][d426]") {
    ymengine::TrackParams t = sid(SidForm::Sine);
    t.patch.toneOn = false;
    t.patch.sidWave = ymengine::sineSidWave(3, 15, ymengine::kSidWavePhase, 8);
    t.patch.sidWaveLen = 16;
    CHECK(std::fabs(semitones(play(VoiceMode::Studio16, TimerMode::Free, t, 60), 2 * ymengine::noteHz(60))) < 0.08);
    t.patch.sidWave = ymengine::sineSidWave(3, 15, ymengine::kSidWavePhase, 16);
    CHECK(std::fabs(semitones(play(VoiceMode::Studio16, TimerMode::Free, t, 60), ymengine::noteHz(60))) < 0.08);
}

TEST_CASE("Pitch: Digidrum and DMA transpose their sample from its root note (D-154, D-153)", "[engine][pitch][d426]") {
    // Digidrum: a sine of 30 samples at a rate of 30 times C4, root C4, looped over whole cycles: the note itself.
    std::vector<uint8_t> nibbles(3000);
    for (size_t i = 0; i < nibbles.size(); ++i)
        nibbles[i] = uint8_t(std::lround(7.5 + 7 * std::sin(2 * 3.141592653589793 * double(i) / 30)));
    ymengine::DigidrumSlot slot;
    slot.rateHz = 30 * ymengine::noteHz(60);
    slot.root = 60;
    slot.loop = true;
    slot.end = uint32_t(nibbles.size());
    const auto digi = [&](ymengine::Engine& e, ymengine::GlobalParams&) {
        e.setUserSample(0, nibbles);
        e.setDigidrumSlot(0, slot);
    };
    for (const VoiceMode mode : kModes)
        for (const TimerMode timer : {TimerMode::Free, TimerMode::Mfp})
            for (const int note : {48, 60, 72}) {
                CAPTURE(int(mode), int(timer), note);
                const double off = semitones(play(mode, timer, track(Generator::Digidrum), note, digi),
                                             ymengine::noteHz(note));
                CHECK(std::fabs(off) < (timer == TimerMode::Mfp ? 0.15 : 0.08));
            }

    // DMA (16 channels, STE): a sine of 96 bytes at 25 033 Hz, root C4; an octave read twice or half as fast.
    std::vector<int8_t> bytes(9600);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = int8_t(std::lround(100 * std::sin(2 * 3.141592653589793 * double(i) / 96)));
    const auto dma = [&](ymengine::Engine& e, ymengine::GlobalParams& g) {
        g.ste = true;
        ymengine::DmaParams p;
        p.slots[0].loop = true;
        e.setDma(p);
        e.setDmaSample(0, bytes);
    };
    const double own = ymengine::kDmaRateHz[2] / 96;
    for (const int note : {48, 60, 72}) {
        CAPTURE(note);
        const double off = semitones(play(VoiceMode::Studio16, TimerMode::Free, track(Generator::Dma), note, dma),
                                     own * std::exp2((note - 60) / 12.0));
        CHECK(std::fabs(off) < 0.08);
    }
}

TEST_CASE("Pitch: every generator follows the Tuning A4, samples included (D-429)", "[engine][pitch][d429]") {
    // At 2 MHz, A4 (440 Hz) gets the period 284: 125 000 / 284 = 440.14 Hz, the nearest the chip can play.
    CHECK(ymengine::tonePeriodFor(ymengine::noteHz(69), ymcore::clocks::kDefault) == 284);
    CHECK(ymengine::noteHz(69, 432) == 432.0);
    CHECK(ymengine::noteHz(60, 432) == ymengine::noteHz(60) * 432 / 440);

    std::vector<uint8_t> nibbles(3000);
    for (size_t i = 0; i < nibbles.size(); ++i)
        nibbles[i] = uint8_t(std::lround(7.5 + 7 * std::sin(2 * 3.141592653589793 * double(i) / 30)));
    ymengine::DigidrumSlot slot;
    slot.rateHz = 30 * ymengine::noteHz(60);
    slot.root = 60;
    slot.loop = true;
    slot.end = uint32_t(nibbles.size());
    std::vector<int8_t> bytes(9600);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = int8_t(std::lround(100 * std::sin(2 * 3.141592653589793 * double(i) / 96)));

    for (const double a4 : {432.0, 446.0}) {
        const double k = a4 / 440;
        for (const auto& [name, t] : {std::pair{"Chip Voice", track(Generator::Chip)},
                                      std::pair{"SID Square", sid(SidForm::Square)},
                                      std::pair{"Sync Buzzer", track(Generator::SyncBuzzer)}}) {
            CAPTURE(a4, name);
            const auto tune = [a4](ymengine::Engine&, ymengine::GlobalParams& g) { g.a4Hz = a4; };
            const double off = semitones(play(VoiceMode::Studio16, TimerMode::Free, t, 60, tune),
                                         ymengine::noteHz(60) * k);
            CHECK(std::fabs(off) < 0.08);
        }
        CAPTURE(a4);
        const auto digi = [&](ymengine::Engine& e, ymengine::GlobalParams& g) {
            g.a4Hz = a4;
            e.setUserSample(0, nibbles);
            e.setDigidrumSlot(0, slot);
        };
        CHECK(std::fabs(semitones(play(VoiceMode::Studio16, TimerMode::Free, track(Generator::Digidrum), 60, digi),
                                  ymengine::noteHz(60) * k)) < 0.08);
        const auto dma = [&](ymengine::Engine& e, ymengine::GlobalParams& g) {
            g.a4Hz = a4;
            g.ste = true;
            ymengine::DmaParams p;
            p.slots[0].loop = true;
            e.setDma(p);
            e.setDmaSample(0, bytes);
        };
        CHECK(std::fabs(semitones(play(VoiceMode::Studio16, TimerMode::Free, track(Generator::Dma), 60, dma),
                                  ymengine::kDmaRateHz[2] / 96 * k)) < 0.08);
    }
}
