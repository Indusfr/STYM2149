// STYM2149 — extended Song of the engine (M6, B12, D-158): Rest and Note-off entries, backward jumps (Loop), restart
// point, end without loop, jumps without nesting, host position (Host sync) in a Song with jumps.
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

constexpr double kRate = 48000;

struct Step {
    uint64_t sample;
    int step, pattern;
    int64_t stepAbs;
};

SeqSongEntry pat(int p, int rep = 1) { return {uint8_t(p), uint16_t(rep), 0, SeqSongKind::Pattern, 0}; }
SeqSongEntry rest(int steps, int rep = 1) { return {0, uint16_t(rep), 0, SeqSongKind::Rest, uint16_t(steps)}; }
SeqSongEntry off(int steps, int rep = 1) { return {0, uint16_t(rep), 0, SeqSongKind::NoteOff, uint16_t(steps)}; }
SeqSongEntry loop(int back, int times) { return {0, uint16_t(times), 0, SeqSongKind::Loop, uint16_t(back)}; }

// Engine at 120 BPM in 1/16 (6000 samples per step), internal or host clock.
struct Rig {
    Engine e;
    SeqData data;
    std::vector<Engine::SeqLogRecord> log;
    GlobalParams g;
    Engine::HostTransport t;
    std::vector<float> out;
    explicit Rig(SeqClock clock = SeqClock::Internal) {
        EngineConfig cfg;
        cfg.render.hostRate = uint32_t(kRate);
        e.prepare(cfg);
        g.seqClock = clock;
        g.tempo = 120;
        g.songMode = true;
        e.setGlobal(g);
        TrackParams tp;
        tp.mix.enabled = true;
        e.setTrack(0, tp);
        log.reserve(200000);
        e.setSeqLog(&log);
        t.bpm = 120;
    }
    // Pattern q of n steps, one note per step on track 1.
    void pattern(int q, int n) {
        auto l = std::make_shared<SeqLane>();
        for (int s = 0; s < n; ++s) l->pat[size_t(s)] = int16_t(s % 12);
        data.patterns[size_t(q)].steps = n;
        data.patterns[size_t(q)].lanes[0] = l;
    }
    void song(std::vector<SeqSongEntry> entries, int restart = 0) {
        data.song = std::move(entries);
        data.songRestart = uint16_t(restart);
        e.setSequence(&data);
    }
    void global() { e.setGlobal(g); }
    void render(uint64_t n) {
        std::vector<float> l(480), r(480);
        while (n > 0) {
            const uint32_t k = uint32_t(std::min<uint64_t>(n, l.size()));
            e.setHostTransport(t);
            e.render(l.data(), r.data(), k);
            out.insert(out.end(), l.begin(), l.begin() + k);
            if (t.playing) t.ppq += k * t.bpm / 60 / kRate;
            n -= k;
        }
    }
    std::vector<Step> steps() const {
        std::vector<Step> v;
        for (const auto& r : log)
            if (r.op == '@') v.push_back({r.sample, r.a, int(r.x), r.b});
        return v;
    }
    std::vector<int> patterns() const {
        std::vector<int> v;
        for (const Step& s : steps()) v.push_back(s.pattern);
        return v;
    }
    int count(char op) const {
        return int(std::count_if(log.begin(), log.end(), [op](const Engine::SeqLogRecord& r) { return r.op == op; }));
    }
};

constexpr int R(int steps) { return kSeqRestPattern + steps; }

// Expected patterns step by step: n steps of each pattern of the list, in order.
std::vector<int> expand(const std::vector<std::pair<int, int>>& runs) {
    std::vector<int> v;
    for (const auto& [p, n] : runs) v.insert(v.end(), size_t(n), p);
    return v;
}

}  // namespace

TEST_CASE("Extended Song: Rest and Note-off play a silence of their length, without a pattern", "[engine][seq][song]") {
    Rig r;
    r.pattern(0, 4);
    r.pattern(1, 2);
    r.song({pat(0), rest(3, 2), off(2), pat(1)});
    r.e.seqPlay(0);
    r.render(6000 * 2 + 100);
    CHECK(r.e.seqStatus().pattern == 0);
    r.render(6000 * 3);  // in the silence
    CHECK(r.e.seqStatus().pattern == -1);
    CHECK(r.e.seqStatus().songPos == 1);
    r.render(6000 * 14);
    const std::vector<int> want = expand({{0, 4}, {R(3), 6}, {R(2), 2}, {1, 2}, {0, 4}});
    const auto got = r.patterns();
    REQUIRE(got.size() >= want.size());
    for (size_t k = 0; k < want.size(); ++k) {
        INFO("step " << k);
        CHECK(got[k] == want[k]);
    }
    // No note triggered during the silence; Note-off releases at the first step of its entry.
    const auto st = r.steps();
    for (const auto& rec : r.log)
        if (rec.op == 'T') {
            const bool silent = rec.sample >= st[4].sample && rec.sample < st[12].sample;
            CHECK_FALSE(silent);
        }
    REQUIRE(r.count('U') >= 1);
    const auto u = std::find_if(r.log.begin(), r.log.end(), [](const auto& rec) { return rec.op == 'U'; });
    CHECK(u->sample == st[10].sample);
    CHECK(u->a == 2);
}

TEST_CASE("Extended Song: Note-off cuts the chord voices that a Rest leaves sounding", "[engine][seq][song]") {
    auto render = [](SeqSongEntry silence) {
        Rig r;
        auto l = std::make_shared<SeqLane>();
        l->pat[0] = 0;
        l->vpat[0][0] = 7;    // chord voice: a fifth
        l->vdur[0][0] = 16;   // held over 16 steps
        r.data.patterns[0].steps = 1;
        r.data.patterns[0].lanes[0] = l;
        r.song({pat(0), silence});
        r.e.seqPlay(0);
        r.render(6000 * 5);
        // Peak-to-peak deviation between the 2nd and the 4th step of the silence.
        const auto first = r.out.begin() + 6000 * 2, last = r.out.begin() + 6000 * 4;
        const auto [lo, hi] = std::minmax_element(first, last);
        return double(*hi - *lo);
    };
    const double rested = render(rest(4)), released = render(off(4));
    CHECK(rested > 0.01);
    CHECK(released < 1e-4);
}

TEST_CASE("Extended Song: backward jump repeats times, then the rest; restart at the restart point",
          "[engine][seq][song]") {
    Rig r;
    for (int q = 0; q < 3; ++q) r.pattern(q, 2);
    // A B [Back 2 x2] C, restart on B: A B A B A B C | B A B A B C | B ...
    r.song({pat(0), pat(1), loop(2, 2), pat(2)}, 1);
    r.e.seqPlay(0);
    r.render(6000 * 28 - 100);
    const std::vector<int> want = expand({{0, 2}, {1, 2}, {0, 2}, {1, 2}, {0, 2}, {1, 2}, {2, 2},
                                          {1, 2}, {0, 2}, {1, 2}, {0, 2}, {1, 2}, {2, 2}, {1, 2}});
    const auto got = r.patterns();
    REQUIRE(got.size() == want.size());
    for (size_t k = 0; k < want.size(); ++k) {
        INFO("step " << k);
        CHECK(got[k] == want[k]);
    }
    CHECK(r.e.seqStatus().playing);
}

TEST_CASE("Extended Song: without loop, stop after the last entry, jumps included", "[engine][seq][song]") {
    Rig r;
    r.pattern(0, 2);
    r.pattern(1, 3);
    r.g.loop = false;
    r.global();
    r.song({pat(0), loop(1, 1), pat(1), rest(2)}, 2);  // the restart point only matters when looping
    r.e.seqPlay(0);
    r.render(6000 * 20);
    CHECK(r.patterns() == expand({{0, 4}, {1, 3}, {R(2), 2}}));
    CHECK(r.count('!') == 1);
    CHECK_FALSE(r.e.seqStatus().playing);
}

TEST_CASE("Extended Song: a jump does not replay past the previous jump; jump without a section ignored",
          "[engine][seq][song]") {
    Rig r;
    r.pattern(0, 1);
    r.pattern(1, 1);
    // A [Back 1 x1] B [Back 5 x1: B alone] [Back 1: nothing to replay]
    r.song({pat(0), loop(1, 1), pat(1), loop(5, 1), loop(1, 3)});
    r.e.seqPlay(0);
    r.render(6000 * 8 - 100);
    CHECK(r.patterns() == std::vector<int>{0, 0, 1, 1, 0, 0, 1, 1});

    // Song of jumps only: nothing to play, the current pattern loops alone.
    Rig s;
    s.pattern(0, 2);
    s.song({loop(1, 1)});
    s.e.seqPlay(0);
    s.render(6000 * 4 - 100);
    CHECK(s.patterns() == std::vector<int>{0, 0, 0, 0});
}

TEST_CASE("Extended Song: jump to an entry (songStep), Loop skipped, jumps reset", "[engine][seq][song]") {
    Rig r;
    for (int q = 0; q < 3; ++q) r.pattern(q, 2);
    r.song({pat(0), pat(1), loop(2, 1), pat(2)});
    r.e.seqPlay(0);
    r.render(6000 * 3 - 100);  // A, then B
    r.e.seqSongJump(0, 2);     // on the jump: the entry that follows it
    r.render(100);
    CHECK(r.e.seqStatus().songPos == 3);
    CHECK(r.e.seqStatus().pattern == 2);
}

TEST_CASE("Extended Song: the host position finds the walk of the Song again (jumps, restart, silences)",
          "[engine][seq][song][host]") {
    auto setup = [](Rig& r) {
        r.pattern(0, 3);
        r.pattern(1, 2);
        r.pattern(2, 4);
        // A x2, B, [Back 2 x2], Rest 3, C, Note-off 2, [Back 2 x1], restart on B.
        r.song({pat(0, 2), pat(1), loop(2, 2), rest(3), pat(2), off(2), loop(2, 1)}, 1);
    };
    // Reference walk, internal clock: 160 steps (two passes and more).
    Rig ref;
    setup(ref);
    ref.e.seqPlay(0);
    ref.render(6000 * 160 - 100);
    const auto want = ref.steps();
    REQUIRE(want.size() == 160);
    for (const int k : {0, 1, 5, 8, 9, 17, 23, 24, 26, 31, 38, 41, 42, 45, 50, 63, 64, 77, 90, 101, 133, 150}) {
        INFO("host step " << k);
        Rig h(SeqClock::Host);
        setup(h);
        h.t.playing = true;
        h.t.ppq = k / 4.0;
        h.render(6000 * 3 - 100);
        const auto got = h.steps();
        REQUIRE(got.size() == 3);
        for (size_t j = 0; j < got.size(); ++j) {
            CHECK(got[j].stepAbs == k + int64_t(j));
            CHECK(got[j].pattern == want[size_t(k) + j].pattern);
            CHECK(got[j].step == want[size_t(k) + j].step);
        }
    }
}

TEST_CASE("Extended Song: a Song shortened during a repeat keeps its position in range", "[engine][seq][song]") {
    Rig r;
    for (int q = 0; q < 3; ++q) r.pattern(q, 2);
    r.song({pat(0), pat(1), pat(2, 4)});
    r.e.seqPlay(0);
    r.render(6000 * 5 - 100);  // A, B, then the first repeat of C: nothing queued yet
    REQUIRE(r.e.seqStatus().songPos == 2);
    // Song entries deleted, then a pattern picked by hand before the repeat ends: at its step 0 the engine used to
    // read entry 2 of a one-entry Song (out of bounds, seen by ASan).
    r.song({pat(0)});
    r.e.seqSelectPattern(0, 1);
    r.render(6000 * 3);
    CHECK(r.e.seqStatus().songPos == 0);
    CHECK(r.e.seqStatus().playing);
}
