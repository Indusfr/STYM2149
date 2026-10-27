// STYM2149 — Ticks cadence of the sequencer: rows of Speed replay ticks (50 to 200 Hz) plus the
// groove, without swing; MIDI clock (one pulse per tick) and Song Position Pointer; Host sync keeps the BPM
// cadence; speed changed during playback.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

struct Rig {
    Engine e;
    SeqData data;
    std::vector<Engine::SeqLogRecord> log;
    GlobalParams g;

    explicit Rig(SeqClock clock = SeqClock::Internal) {
        EngineConfig cfg;
        cfg.render.hostRate = 48000;
        e.prepare(cfg);
        g.seqClock = clock;
        g.tempo = 120;
        g.timing = SeqTiming::Ticks;
        e.setGlobal(g);
        TrackParams t;
        t.mix.enabled = true;
        e.setTrack(0, t);
        log.reserve(100000);
        e.setSeqLog(&log);
        auto l = std::make_shared<SeqLane>();
        for (int s = 0; s < 16; ++s) l->pat[size_t(s)] = int16_t(s % 12);
        data.patterns[0].steps = 16;
        data.patterns[0].lanes[0] = l;
        e.setSequence(&data);
    }
    void global() { e.setGlobal(g); }
    void render(uint64_t n) {
        static std::vector<float> l(512), r(512);
        while (n > 0) {
            const uint32_t k = uint32_t(std::min<uint64_t>(n, l.size()));
            e.render(l.data(), r.data(), k);
            n -= k;
        }
    }
    // Starts (sample) and durations (samples) of the rows played.
    std::vector<std::pair<uint64_t, double>> lines() const {
        std::vector<std::pair<uint64_t, double>> out;
        for (const auto& r : log)
            if (r.op == '@') out.emplace_back(r.sample, r.y);
        return out;
    }
};

}  // namespace

TEST_CASE("Ticks: a row lasts Speed replay frames, at 50, 100 and 200 Hz, without swing", "[engine][seq][ticks]") {
    for (const uint32_t hz : {50u, 100u, 200u}) {
        CAPTURE(hz);
        Rig r;
        r.g.vblHz = hz;
        r.g.speed = 6;
        r.g.swing = 66;  // ignored in Ticks
        r.global();
        r.e.seqPlay(0);
        r.render(48000);
        const auto ln = r.lines();
        const double line = 48000.0 * 6 / hz;
        REQUIRE(ln.size() >= 4);
        CHECK(ln.size() == size_t(std::ceil(48000 / line)));
        for (size_t k = 0; k < 4; ++k) {
            CHECK(std::fabs(ln[k].second - line) < 1e-6);
            CHECK(std::fabs(double(ln[k].first) - double(k) * line) <= 1);
        }
    }
}

TEST_CASE("Ticks: groove, ticks added to the rows in a loop; speed changed during playback",
          "[engine][seq][ticks]") {
    Rig r;
    r.g.speed = 5;
    r.g.grooveLength = 2;
    r.g.groove = {1, 0};  // 6 then 5 ticks
    r.global();
    r.e.seqPlay(0);
    r.render(48000);
    auto ln = r.lines();
    REQUIRE(ln.size() >= 5);
    CHECK(std::fabs(ln[0].second - 960 * 6) < 1e-6);
    CHECK(std::fabs(ln[1].second - 960 * 5) < 1e-6);
    CHECK(std::fabs(ln[2].second - 960 * 6) < 1e-6);
    CHECK(ln[1].first == 960 * 6);
    CHECK(ln[2].first == 960 * 11);
    CHECK(ln[3].first == 960 * 17);

    // Speed 3: the following rows last 3 (+1) ticks.
    const size_t before = ln.size();
    r.g.speed = 3;
    r.global();
    r.render(48000);
    ln = r.lines();
    REQUIRE(ln.size() > before + 4);
    for (size_t k = before + 1; k < before + 4; ++k) {
        CAPTURE(k);
        CHECK(std::fabs(ln[k].second - 960.0 * (3 + (k % 2 == 0 ? 1 : 0))) < 1e-6);
    }
}

TEST_CASE("Ticks: MIDI clock, a row of Speed pulses, SPP through the groove", "[engine][seq][ticks]") {
    Rig r(SeqClock::Midi);
    r.g.speed = 4;
    r.g.grooveLength = 2;
    r.g.groove = {2, 0};  // 6 then 4 pulses
    r.global();
    r.e.seqPlay(0);
    r.e.midiStart(0);
    std::vector<int> at;  // pulse of each row
    for (int k = 0; k < 30; ++k) {
        const size_t n = r.lines().size();
        r.e.midiClock(0);
        r.render(500);
        if (r.lines().size() > n) at.push_back(k);
    }
    CHECK(at == std::vector<int>{0, 6, 10, 16, 20, 26});

    // SPP: 4 sixteenth notes = 24 pulses: row 4 (pulses 20 to 25) begun; row 5 comes at 26.
    r.e.midiStop(0);
    r.e.midiSongPosition(0, 4);
    r.e.midiContinue(0);
    const size_t n = r.lines().size();
    for (int k = 24; k < 26; ++k) {
        r.e.midiClock(0);
        r.render(500);
    }
    CHECK(r.lines().size() == n);
    r.e.midiClock(0);
    r.render(500);
    REQUIRE(r.lines().size() == n + 1);
    const auto last = std::find_if(r.log.rbegin(), r.log.rend(), [](const auto& x) { return x.op == '@'; });
    CHECK(last->b == 5);  // absolute step
}

TEST_CASE("Ticks: Host sync keeps the BPM cadence of the host", "[engine][seq][ticks]") {
    Rig r(SeqClock::Host);
    Engine::HostTransport t;
    t.playing = true;
    t.bpm = 120;
    std::vector<float> l(480), rr(480);
    for (int k = 0; k < 100; ++k) {  // the host advances by 480 samples per block
        r.e.setHostTransport(t);
        r.e.render(l.data(), rr.data(), 480);
        t.ppq += 480 * t.bpm / 60 / 48000;
    }
    const auto ln = r.lines();
    REQUIRE(ln.size() >= 3);
    CHECK(std::fabs(ln[1].second - 6000) < 1e-6);  // sixteenth notes at 120 BPM, not 6 frames
}
