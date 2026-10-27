// STYM2149 — buzzer: envelope tuned to the note (N / D ratio, fine tune), YM Pad, envelope period
// driven by the pitch written in 3-channel mode too. The tuning and YM Pad are Sync Buzzer settings.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ymengine/Engine.h"
#include "ymengine/Pitch.h"

using namespace stym;
using ymengine::Engine;
using ymengine::Generator;
using ymengine::PitchSource;
using ymengine::PitchTarget;

namespace {

using Log = std::vector<Engine::WriteRecord>;
const ymcore::MasterClock kClock = ymcore::clocks::kDefault;

struct Rig {
    Engine e;
    Log log;
    std::vector<float> l = std::vector<float>(1 << 16), r = std::vector<float>(1 << 16);
    std::vector<float> out;

    explicit Rig(ymengine::VoiceMode mode) {
        ymengine::EngineConfig cfg;
        cfg.render.hostRate = 48000;
        e.prepare(cfg);
        ymengine::GlobalParams g;
        g.mode = mode;
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
    Log writes(uint8_t reg) const {
        Log w;
        for (const auto& x : log)
            if (x.reg == reg && x.chip == 0) w.push_back(x);
        return w;
    }
    // Last R11-R12 period written.
    int lastEnvPeriod() const {
        const Log lo = writes(11), hi = writes(12);
        if (lo.empty() || hi.empty()) return -1;
        return lo.back().value | hi.back().value << 8;
    }
};

ymengine::TrackParams buzz(int n = 1, int d = 1, double fine = 0, int shape = 8) {
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.mix.level = 1;
    t.patch.generator = Generator::SyncBuzzer;
    t.patch.envelopeVolume = true;
    t.patch.toneOn = false;
    t.patch.envShape = uint8_t(shape);
    t.patch.envPeriod = 5000;  // ignored by the tuned envelope
    t.patch.envTuned = true;
    t.patch.envRatioN = uint8_t(n);
    t.patch.envRatioD = uint8_t(d);
    t.patch.envFineCents = fine;
    return t;
}

uint16_t tuned(double note, double ratio = 1, int shape = 8) {
    return ymengine::tunedEnvPeriod(ymengine::noteHz(note) * ratio, shape, kClock);
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

double rms(const std::vector<float>& x) {
    double s = 0;
    for (float v : x) s += double(v) * v;
    return std::sqrt(s / double(std::max<size_t>(1, x.size())));
}

}  // namespace

TEST_CASE("Buzzer: tuned envelope period, sawtooths and triangles", "[engine][buzzer]") {
    // 2 MHz: a sawtooth cycle lasts 256 · EP clock periods, a triangle twice as long.
    CHECK(ymengine::envelopeRamps(8) == 1);
    CHECK(ymengine::envelopeRamps(12) == 1);
    CHECK(ymengine::envelopeRamps(10) == 2);
    CHECK(ymengine::envelopeRamps(14) == 2);
    CHECK(ymengine::tunedEnvPeriod(220, 8, kClock) == 36);   // 35.5
    CHECK(ymengine::tunedEnvPeriod(220, 10, kClock) == 18);  // 17.8
    CHECK(ymengine::tunedEnvPeriod(0, 8, kClock) == 0);
    CHECK(ymengine::tunedEnvPeriod(0.01, 8, kClock) == 65535);
    CHECK(ymengine::tunedEnvPeriod(1e6, 8, kClock) == 1);
}

TEST_CASE("Buzzer: the tuned envelope follows the note, its ratio and its fine tune", "[engine][buzzer]") {
    struct Case {
        int n, d;
        double fine;
        int shape;
        double ratio;
    };
    for (const Case k : {Case{1, 1, 0, 8, 1}, Case{1, 2, 0, 8, 0.5}, Case{3, 2, 0, 12, 1.5}, Case{2, 1, 0, 10, 2},
                         Case{1, 1, 100, 8, std::pow(2.0, 1 / 12.0)}, Case{1, 1, -50, 14, std::pow(2.0, -0.5 / 12)}}) {
        CAPTURE(k.n, k.d, k.fine, k.shape);
        Rig r(ymengine::VoiceMode::Studio16);
        r.e.setTrack(0, buzz(k.n, k.d, k.fine, k.shape));
        r.e.noteOn(0, 57, 127);
        r.run(480);
        CHECK(r.e.voice(0).envPeriod == tuned(57, k.ratio, k.shape));
    }

    // The sound (Sync Buzzer): the timer restarts the sawtooth of 1.5 times A3 (330 Hz) at the note, so the
    // sound repeats at A3: harmonics of 220 Hz only, the third (660 Hz, the second of the sawtooth) strong where a
    // sawtooth at the note (1 / 1) gives about a third of the fundamental.
    auto spectrum = [](int n, int d) {
        Rig s(ymengine::VoiceMode::Studio16);
        s.e.setTrack(0, buzz(n, d));
        s.e.noteOn(0, 57, 127);
        s.run(4800);
        s.run(24000, true);
        const double fe = kClock.hz() / (256.0 * s.e.voice(0).envPeriod);
        CHECK(std::fabs(fe - 220.0 * n / d) < 220.0 * n / d * 0.02);
        return std::array<double, 4>{component(s.out, 110), component(s.out, 220), component(s.out, 330),
                                     component(s.out, 660)};
    };
    const auto fifth = spectrum(3, 2), unison = spectrum(1, 1);
    CAPTURE(fifth, unison);
    CHECK(fifth[1] > 100 * fifth[0]);
    CHECK(fifth[1] > 100 * fifth[2]);
    CHECK(fifth[3] > 0.6 * fifth[1]);
    CHECK(unison[3] < 0.45 * unison[1]);

    // Without a tuned envelope: the period of the track, like the prototype (under 3200, which a Sync Buzzer keeps).
    Rig p(ymengine::VoiceMode::Studio16);
    ymengine::TrackParams t = buzz();
    t.patch.envTuned = false;
    t.patch.envPeriod = 3000;
    p.e.setTrack(0, t);
    p.e.noteOn(0, 57, 127);
    p.run(480);
    CHECK(p.e.voice(0).envPeriod == 3000);
}

TEST_CASE("Buzzer: new note, slide and bend retune the envelope", "[engine][buzzer]") {
    Rig r(ymengine::VoiceMode::Studio16);
    ymengine::TrackParams t = buzz();
    t.patch.mono = true;  // legato: 45 ms slide to the next note
    r.e.setTrack(0, t);
    r.e.noteOn(0, 57, 127);
    r.run(480);
    CHECK(r.e.voice(0).envPeriod == tuned(57));
    r.e.noteOn(0, 69, 127);
    r.run(48);
    // The voice slides: the envelope plays the target note at once (the matrix does not make it follow the slide).
    int v = -1;
    for (int i = 0; i < Engine::kVoices; ++i)
        if (r.e.voice(i).on && r.e.voice(i).note == 69) v = i;
    REQUIRE(v >= 0);
    CHECK(r.e.voice(v).envPeriod == tuned(69));
    // Bend of two semitones.
    r.e.pitchBend(0, 2.0);
    r.run(48);
    CHECK(r.e.voice(v).envPeriod == tuned(71));
}

TEST_CASE("Buzzer: in 3-channel mode, R11-R12 get the tuned period and that of the matrix", "[engine][buzzer][3ch]") {
    Rig r(ymengine::VoiceMode::Authentic3);
    r.e.setTrack(0, buzz(1, 2));
    r.e.noteOn(0, 57, 127);
    r.run(480);
    CHECK(r.lastEnvPeriod() == tuned(57, 0.5));

    // Matrix, table to the envelope: the period halved at the octave is written (16-channel and 3-channel).
    Rig m(ymengine::VoiceMode::Authentic3);
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.patch.envelopeVolume = true;
    t.patch.envPeriod = 640;
    t.patch.ornamentOn = true;
    t.patch.ornamentLength = 1;
    t.patch.ornamentPitch = {12};
    t.patch.pitchMatrix = ymengine::pitchBit(PitchSource::Table, PitchTarget::Square) |
                          ymengine::pitchBit(PitchSource::Table, PitchTarget::Buzzer);
    m.e.setTrack(0, t);
    m.e.noteOn(0, 60, 127);
    m.run(4800);
    CHECK(m.e.voice(0).envPeriod == 320);
    CHECK(m.lastEnvPeriod() == 320);
}

TEST_CASE("Buzzer: YM Pad, tone at period 0 under the envelope, about -6 dB", "[engine][buzzer]") {
    // 3-channel: tone of channel A open in R7, period 0.
    Rig r(ymengine::VoiceMode::Authentic3);
    ymengine::TrackParams t = buzz();
    t.patch.ymPad = true;
    r.e.setTrack(0, t);
    r.e.noteOn(0, 45, 127);
    r.run(480);
    const Log r7 = r.writes(7);
    REQUIRE_FALSE(r7.empty());
    CHECK((r7.back().value & 1) == 0);
    for (const auto& w : r.writes(0)) CHECK(w.value == 0);
    CHECK(r.e.chip(0).chip().readRegister(0) == 0);
    CHECK(r.e.chip(0).chip().readRegister(1) == 0);

    // 16-channel: same buzzer with and without YM Pad.
    auto level = [](bool pad) {
        Rig s(ymengine::VoiceMode::Studio16);
        ymengine::TrackParams b = buzz(1, 1, 0, 10);
        b.patch.ymPad = pad;
        s.e.setTrack(0, b);
        s.e.noteOn(0, 45, 127);
        s.run(4800);
        s.run(24000, true);
        return rms(s.out);
    };
    const double plain = level(false), pad = level(true);
    CAPTURE(plain, pad);
    REQUIRE(plain > 0.01);
    CHECK(pad / plain > 0.4);
    CHECK(pad / plain < 0.6);

    // Without an envelope (4-bit volume): no effect.
    Rig q(ymengine::VoiceMode::Authentic3);
    ymengine::TrackParams c;
    c.mix.enabled = true;
    c.patch.ymPad = true;
    q.e.setTrack(0, c);
    q.e.noteOn(0, 57, 127);
    q.run(480);
    CHECK(q.e.chip(0).chip().readRegister(0) == ymengine::tonePeriodFor(ymengine::noteHz(57), kClock) % 256);
}

TEST_CASE("Buzzer: tuning and YM Pad belong to the Sync Buzzer, Chip Voice keeps the track's period (fourth DAW test, "
          "item 33)",
          "[engine][buzzer]") {
    // Chip Voice with the Sync Buzzer settings left in the patch: the envelope period and the tone of the track.
    Rig r(ymengine::VoiceMode::Authentic3);
    ymengine::TrackParams t = buzz(3, 2);
    t.patch.generator = Generator::Chip;
    t.patch.toneOn = true;
    t.patch.envPeriod = 3000;
    t.patch.ymPad = true;
    r.e.setTrack(0, t);
    r.e.noteOn(0, 57, 127);
    r.run(480);
    CHECK(r.e.voice(0).envPeriod == 3000);
    CHECK(r.lastEnvPeriod() == 3000);
    CHECK(r.e.chip(0).chip().readRegister(0) == ymengine::tonePeriodFor(ymengine::noteHz(57), kClock) % 256);

    // GEN row of the sequences: Sync Buzzer, Chip Voice, Sync Buzzer. The Chip Voice step plays the track's period,
    // the Sync Buzzer steps the tuned one.
    Rig s(ymengine::VoiceMode::Studio16);
    ymengine::TrackParams b = buzz(1, 2);
    b.patch.envPeriod = 3000;
    b.patch.ornamentOn = true;
    b.patch.ornamentLength = 3;
    b.patch.ornamentLoop = false;
    b.patch.rows.gen = {0, uint8_t(ymengine::SeqGen::Chip), uint8_t(ymengine::SeqGen::SyncBuzzer)};
    s.e.setTrack(0, b);
    s.e.noteOn(0, 57, 127);
    s.run(480);
    CHECK(s.e.voice(0).gen == Generator::SyncBuzzer);
    CHECK(s.e.voice(0).envPeriod == tuned(57, 0.5));
    s.run(960);
    CHECK(s.e.voice(0).gen == Generator::Chip);
    CHECK(s.e.voice(0).envPeriod == 3000);
    s.run(960);
    CHECK(s.e.voice(0).gen == Generator::SyncBuzzer);
    CHECK(s.e.voice(0).envPeriod == tuned(57, 0.5));
}

TEST_CASE("Env source: neither square nor noise, the envelope's own waveform at its frequency, in 16 ch and "
          "Authentic 3ch (fifth DAW test, item 48)",
          "[engine][buzzer][3ch]") {
    for (const auto mode : {ymengine::VoiceMode::Studio16, ymengine::VoiceMode::Authentic3}) {
        CAPTURE(int(mode));
        Rig r(mode);
        ymengine::TrackParams t;
        t.mix.enabled = true;
        t.mix.level = 1;
        t.patch.toneOn = false;
        t.patch.noiseOn = false;
        t.patch.envelopeVolume = true;
        t.patch.envShape = 8;     // sawtooth
        t.patch.envPeriod = 100;  // 2 MHz / (256 × 100) = 78.125 Hz, whatever the note
        r.e.setTrack(0, t);
        r.e.noteOn(0, 81, 127);  // A5, 880 Hz
        r.run(4800);
        r.run(48000, true);
        const double env = kClock.hz() / (256.0 * 100);
        CHECK(rms(r.out) > 0.01);
        CHECK(component(r.out, env) > 10 * component(r.out, 880));
        CHECK((r.e.chip(0).chip().readRegister(7) & 0x09) == 0x09);  // channel A: square and noise off
    }
}
