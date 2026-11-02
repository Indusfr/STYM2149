// STYM2149 — FM Voice generator: the effect timer rewrites the tone period with the steps of the FM wave, as
// TTrak's FM timers, or in semitones as maxYMiser's square FM; GEN row of the sequences; frames of the
// register recorder.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "ymengine/Engine.h"
#include "ymengine/Pitch.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

struct Rig {
    Engine e;
    std::vector<Engine::WriteRecord> log;
    std::vector<float> l = std::vector<float>(960), r = std::vector<float>(960);

    explicit Rig(VoiceMode mode = VoiceMode::Authentic3) {
        EngineConfig cfg;
        cfg.render.hostRate = 48000;  // 2 MHz clock
        e.prepare(cfg);
        GlobalParams g;
        g.mode = mode;
        e.setGlobal(g);
        log.reserve(1 << 20);
        e.setWriteLog(&log);
    }
    void run(uint32_t n) {
        while (n > 0) {
            const uint32_t k = std::min<uint32_t>(n, uint32_t(l.size()));
            e.render(l.data(), r.data(), k);
            n -= k;
        }
    }
    // Tone periods written to channel A of chip 0, in order; R0 and R1 written at the same cycle make one period.
    std::vector<uint16_t> tones() const {
        std::vector<uint16_t> out;
        uint8_t lo = 0, hi = 0;
        for (size_t i = 0; i < log.size(); ++i) {
            const Engine::WriteRecord& w = log[i];
            if (w.chip != 0 || w.reg > 1) continue;
            (w.reg == 0 ? lo : hi) = w.value;
            bool more = false;
            for (size_t k = i + 1; k < log.size() && log[k].cycle == w.cycle && !more; ++k)
                more = log[k].chip == 0 && log[k].reg <= 1;
            if (more) continue;
            const uint16_t tp = uint16_t(lo | (hi & 15) << 8);
            if (out.empty() || out.back() != tp) out.push_back(tp);
        }
        return out;
    }
};

TrackParams fmTrack(std::initializer_list<uint8_t> wave) {
    TrackParams t;
    t.mix.enabled = true;
    t.patch.generator = Generator::Fm;
    size_t i = 0;
    for (uint8_t s : wave) t.patch.fmWave[i++] = s;
    t.patch.fmWaveLen = uint8_t(wave.size());
    return t;
}

uint16_t divided(uint16_t tp, int step) {
    return uint16_t(std::clamp(std::lround(tp / kFmMultiples[size_t(step)]), 1L, 4095L));
}

}  // namespace

TEST_CASE("FM Voice: the note, then its octave, by default", "[engine][fm]") {
    const TrackPatch p;
    CHECK(p.fmWaveLen == 2);
    CHECK(kFmMultiples[size_t(p.fmWave[0])] == 1.0);
    CHECK(kFmMultiples[size_t(p.fmWave[1])] == 2.0);
}

TEST_CASE("FM Voice: each interrupt writes the next step, the note's period divided by its multiple",
          "[engine][fm][3ch]") {
    for (const auto& wave : {std::vector<uint8_t>{1, 3}, std::vector<uint8_t>{0, 2, 7}}) {
        CAPTURE(wave.size());
        Rig g;
        TrackParams t = fmTrack({});
        for (size_t i = 0; i < wave.size(); ++i) t.patch.fmWave[i] = wave[i];
        t.patch.fmWaveLen = uint8_t(wave.size());
        g.e.setTrack(0, t);
        g.e.noteOn(0, 57, 127, 0);  // A3, 220 Hz
        g.run(4800);
        const Engine::VoiceInfo v = g.e.voice(0);
        REQUIRE(v.gen == Generator::Fm);
        // The steps fit in one period of the note at Timer Ratio 1, like the SID wave.
        CHECK(std::abs(v.timerHz - 220.0 * double(wave.size())) < 0.5);
        const uint16_t tp = v.notePeriod;
        std::set<uint16_t> want;
        for (uint8_t s : wave) want.insert(divided(tp, s));
        const std::vector<uint16_t> seen = g.tones();
        REQUIRE(seen.size() > 20);
        std::set<uint16_t> got(seen.begin() + 2, seen.end());
        CHECK(got == want);
        // In the order of the wave.
        for (size_t i = 3; i < seen.size(); ++i) {
            CAPTURE(i);
            size_t a = 0, b = 0;
            for (size_t k = 0; k < wave.size(); ++k) {
                if (divided(tp, wave[k]) == seen[i - 1]) a = k;
                if (divided(tp, wave[k]) == seen[i]) b = k;
            }
            REQUIRE(b == (a + 1) % wave.size());
        }
    }
}

TEST_CASE("FM Voice: a one-step wave plays the note's multiple, without a timer heard", "[engine][fm][3ch]") {
    Rig g;
    g.e.setTrack(0, fmTrack({4}));  // ×3: the twelfth
    g.e.noteOn(0, 45, 127, 0);
    g.run(4800);
    const uint16_t tp = g.e.voice(0).notePeriod;
    const std::vector<uint16_t> seen = g.tones();
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.back() == divided(tp, 4));
}

TEST_CASE("FM Voice: the GEN row of the sequences switches to it for a step", "[engine][fm][sequences]") {
    Rig g(VoiceMode::Studio16);
    TrackParams t;
    t.mix.enabled = true;
    t.patch.ornamentOn = true;
    t.patch.ornamentLength = 2;
    t.patch.ornamentLoop = false;
    t.patch.rows.gen = {0, uint8_t(SeqGen::Fm)};
    g.e.setTrack(0, t);
    g.e.noteOn(0, 57, 100, 0);
    g.run(480);
    CHECK(g.e.voice(0).gen == Generator::Chip);
    g.run(960);
    CHECK(g.e.voice(0).gen == Generator::Fm);
    CHECK(std::abs(g.e.voice(0).timerHz - 440.0) < 0.5);  // two steps per period of A3
}

TEST_CASE("FM Voice: recorder frames carry the wave and the note's period", "[engine][fm][recorder]") {
    Rig g;
    g.e.setTrack(0, fmTrack({1, 3, 4}));
    std::vector<RegisterFrame> frames(64);
    g.e.startCapture(0, frames);
    g.e.noteOn(0, 57, 127, 0);
    g.run(9600);
    REQUIRE(g.e.capturedFrames() == 10);
    const RegisterFrame& f = frames[5];
    const uint16_t tp = g.e.voice(0).notePeriod;
    CHECK(f.fx[0].on);
    CHECK(f.fx[0].gen == Generator::Fm);
    CHECK(f.fx[0].value == 3);
    CHECK(f.fx[0].wave == std::array<uint8_t, 16>{1, 3, 4});
    CHECK_FALSE(f.fx[0].semis);
    CHECK(f.fx[0].timer == mfpQuantize(g.e.voice(0).timerActualHz));
    CHECK((f.reg[0] | (f.reg[1] & 15) << 8) == tp);  // the period the steps divide, whatever step was last written
    CHECK(frames[0].fx[0].restart);
    CHECK_FALSE(f.fx[0].restart);
}

TEST_CASE("FM Voice: in the Semitones scale, each step the note's period brought up by its semitones",
          "[engine][fm][3ch]") {
    Rig g;
    TrackParams t = fmTrack({1, 3, 4});  // the Ratios wave, not played
    t.patch.fmScale = FmScale::Semitones;
    t.patch.fmSemi[0] = 0;
    t.patch.fmSemi[1] = 12;
    t.patch.fmSemi[2] = 19;
    g.e.setTrack(0, t);
    std::vector<RegisterFrame> frames(64);
    g.e.startCapture(0, frames);
    g.e.noteOn(0, 57, 127, 0);  // A3, 220 Hz
    g.run(9600);
    const Engine::VoiceInfo v = g.e.voice(0);
    REQUIRE(v.gen == Generator::Fm);
    CHECK(std::abs(v.timerHz - 220.0 * 3) < 0.5);
    const uint16_t tp = v.notePeriod;
    const std::set<uint16_t> want = {tp, fmSemitonePeriod(tp, 12), fmSemitonePeriod(tp, 19)};
    const std::vector<uint16_t> seen = g.tones();
    REQUIRE(seen.size() > 20);
    CHECK(std::set<uint16_t>(seen.begin() + 2, seen.end()) == want);
    // The recorder frames say so.
    const RegisterFrame& f = frames[5];
    CHECK(f.fx[0].gen == Generator::Fm);
    CHECK(f.fx[0].semis);
    CHECK(f.fx[0].wave == std::array<uint8_t, 16>{0, 12, 19});
}
