// STYM2149 — sequencer of the engine: end of a pattern without loop, walk of the Song (repeats, tempo of the blocks,
// end without loop, jump to an entry), pattern switch, MIDI clock (Start, Stop, Continue, Song Position Pointer),
// length changed during playback, countdowns of the tracks by deadline (D-133), track switched off during playback, Env
// entries of the SRC row (D-277), rows of the steps of a held note (D-343, D-345), ARP Off on a held note (D-379), one
// envelope per note of the ARP row (D-416), releases that the next notes leave on a polyphonic track (D-421), a note
// held live before Play (D-431), the 1/32T resolution (D-433).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

struct Step {
    uint64_t sample;
    int step, pattern;
    int64_t stepAbs;
    double samples;
};

struct Rig {
    Engine e;
    SeqData data;
    std::vector<Engine::SeqLogRecord> log;
    GlobalParams g;
    uint64_t at = 0;

    explicit Rig(SeqClock clock = SeqClock::Internal) {
        EngineConfig cfg;
        cfg.render.hostRate = 48000;
        e.prepare(cfg);
        g.seqClock = clock;
        g.tempo = 120;  // 1/16: 6000 samples per step
        e.setGlobal(g);
        TrackParams t;
        t.mix.enabled = true;
        e.setTrack(0, t);
        log.reserve(100000);
        e.setSeqLog(&log);
    }
    // Pattern q of n steps, one note per step on track 1.
    void pattern(int q, int n) {
        auto l = std::make_shared<SeqLane>();
        for (int s = 0; s < n; ++s) l->pat[size_t(s)] = int16_t(s % 12);
        data.patterns[size_t(q)].steps = n;
        data.patterns[size_t(q)].lanes[0] = l;
        e.setSequence(&data);
    }
    void global() { e.setGlobal(g); }
    void render(uint64_t n) {
        static std::vector<float> l(512), r(512);
        while (n > 0) {
            const uint32_t k = uint32_t(std::min<uint64_t>(n, l.size()));
            e.render(l.data(), r.data(), k);
            at += k;
            n -= k;
        }
    }
    std::vector<Step> steps() const {
        std::vector<Step> out;
        for (const auto& r : log)
            if (r.op == '@') out.push_back({r.sample, r.a, int(r.x), r.b, r.y});
        return out;
    }
    int count(char op) const {
        int n = 0;
        for (const auto& r : log) n += r.op == op;
        return n;
    }
};

}  // namespace

TEST_CASE("Sequencer: without loop, the pattern stops before replaying its first step", "[engine][seq]") {
    Rig r;
    r.g.loop = false;
    r.global();
    r.pattern(0, 4);
    r.e.seqPlay(0);
    r.render(60000);
    const auto st = r.steps();
    REQUIRE(st.size() == 4);
    // Start: the phase of the clock is full, the first step falls on the first sample and lasts one sample
    // less (prototype).
    for (int k = 0; k < 4; ++k) {
        CHECK(st[size_t(k)].step == k);
        CHECK(std::llabs(int64_t(st[size_t(k)].sample) - 6000 * k) <= 1);
        CHECK(std::fabs(st[size_t(k)].samples - 6000) < 1e-9);
    }
    CHECK(r.count('!') == 1);
    CHECK_FALSE(r.e.seqStatus().playing);
    CHECK(r.e.seqStatus().step == -1);
}

TEST_CASE("Sequencer: with loop, the pattern resumes at step 0", "[engine][seq]") {
    Rig r;
    r.pattern(0, 3);
    r.e.seqPlay(0);
    r.render(6000 * 7 - 10);
    const auto st = r.steps();
    REQUIRE(st.size() == 7);
    for (int k = 0; k < 7; ++k) {
        CHECK(st[size_t(k)].step == k % 3);
        CHECK(st[size_t(k)].stepAbs == k);
    }
    CHECK(r.e.seqStatus().playing);
}

TEST_CASE("Sequencer: a queued pattern applies at the next step 0", "[engine][seq]") {
    Rig r;
    r.pattern(0, 4);
    r.pattern(1, 2);
    r.e.seqPlay(0);
    r.render(7000);
    r.e.seqSelectPattern(0, 1);
    r.render(1);
    CHECK(r.e.seqStatus().queued == 1);
    CHECK(r.e.seqStatus().pattern == 0);
    r.render(6000 * 6);
    const auto st = r.steps();
    REQUIRE(st.size() >= 7);
    for (int k = 0; k < 4; ++k) CHECK(st[size_t(k)].pattern == 0);
    CHECK(st[4].pattern == 1);
    CHECK(st[4].step == 0);
    CHECK(st[5].step == 1);
    CHECK(st[6].step == 0);
    CHECK(r.count('Q') == 1);
    CHECK(r.e.seqStatus().queued == -1);
    // The same pattern as the one playing: the queue empties.
    r.e.seqSelectPattern(0, 0);
    r.e.seqSelectPattern(0, 1);
    r.render(1);
    CHECK(r.e.seqStatus().queued == -1);
}

TEST_CASE("Sequencer: when stopped, the choice of a pattern is immediate", "[engine][seq]") {
    Rig r;
    r.pattern(0, 4);
    r.pattern(2, 5);
    r.e.seqSelectPattern(0, 2);
    r.render(10);
    CHECK(r.e.seqStatus().pattern == 2);
    r.e.seqPlay(0);
    r.render(6000 * 5 - 10);
    const auto st = r.steps();
    REQUIRE(st.size() == 5);
    for (const auto& s : st) CHECK(s.pattern == 2);
    CHECK(r.count('Q') == 0);
}

TEST_CASE("Sequencer: Song, repeats and own tempo of the blocks, looped", "[engine][seq]") {
    Rig r;
    r.pattern(0, 4);
    r.pattern(1, 2);
    r.pattern(2, 3);
    r.data.song = {{0, 2, 0}, {1, 1, 0}, {2, 1, 150}};
    r.e.setSequence(&r.data);
    r.g.songMode = true;
    r.global();
    r.e.seqSelectPattern(0, 1);  // the Song starts from its first entry, whatever the pattern shown
    r.e.seqPlay(0);
    r.render(6000 * 20);
    const auto st = r.steps();
    // p0 ×2 (8 steps), p1 (2 steps), p2 at 150 BPM (3 steps of 4800), then p0 again.
    std::vector<int> want = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 0, 0, 0, 0};
    REQUIRE(st.size() >= want.size());
    for (size_t k = 0; k < want.size(); ++k) {
        INFO("step " << k);
        CHECK(st[k].pattern == want[k]);
        CHECK(std::fabs(st[k].samples - (want[k] == 2 ? 4800 : 6000)) < 1e-9);
    }
    CHECK(std::llabs(int64_t(st[10].sample) - 6000 * 10) <= 2);
    CHECK(std::llabs(int64_t(st[13].sample) - (6000 * 10 + 4800 * 3)) <= 2);
    CHECK(r.e.seqStatus().songPos == 0);
}

TEST_CASE("Sequencer: Song without loop, stop after the last entry", "[engine][seq]") {
    Rig r;
    r.pattern(0, 2);
    r.pattern(1, 3);
    r.data.song = {{1, 1, 0}, {0, 2, 0}};
    r.e.setSequence(&r.data);
    r.g.songMode = true;
    r.g.loop = false;
    r.global();
    r.e.seqPlay(0);
    r.render(6000 * 12);
    const auto st = r.steps();
    std::vector<int> want = {1, 1, 1, 0, 0, 0, 0};
    REQUIRE(st.size() == want.size());
    for (size_t k = 0; k < want.size(); ++k) CHECK(st[k].pattern == want[k]);
    CHECK(r.count('!') == 1);
    CHECK_FALSE(r.e.seqStatus().playing);
    CHECK(r.e.seqStatus().songPos == 0);
}

TEST_CASE("Sequencer: jump to a Song entry (songStep)", "[engine][seq]") {
    Rig r;
    r.pattern(0, 4);
    r.pattern(1, 2);
    r.pattern(2, 3);
    r.data.song = {{0, 1, 0}, {1, 3, 0}, {2, 1, 0}};
    r.e.setSequence(&r.data);
    r.g.songMode = true;
    r.global();

    SECTION("when stopped: the pattern of the entry becomes current, Play starts again from the first entry") {
        r.e.seqSongJump(0, 2);
        r.render(64);
        CHECK(r.e.seqStatus().pattern == 2);
        CHECK(r.e.seqStatus().songPos == 2);
        r.e.seqPlay(0);
        r.render(6000);
        const auto st = r.steps();
        REQUIRE(st.size() == 1);
        CHECK(st[0].pattern == 0);
        CHECK(r.e.seqStatus().songPos == 0);
    }

    SECTION("during playback: pattern replaced at once, step kept, repeats counted again") {
        r.e.seqPlay(0);
        r.render(6000 * 2 - 100);  // step 1 of pattern 0 in progress, entry 1 queued
        r.e.seqSongJump(0, 1);
        r.render(64);
        CHECK(r.e.seqStatus().pattern == 1);
        CHECK(r.e.seqStatus().songPos == 1);
        CHECK(r.e.seqStatus().step == 1);
        CHECK(r.e.seqStatus().queued == -1);  // the entry repeats: nothing queued (songArm)
        r.render(6000 * 7);
        const auto st = r.steps();
        // Pattern 0 (steps 0 and 1), then pattern 1 twice in full (the pass already begun counts as the
        // first repeat, as in the prototype), then entry 2.
        const std::vector<std::pair<int, int>> want = {{0, 0}, {0, 1}, {1, 0}, {1, 1}, {1, 0},
                                                       {1, 1}, {2, 0}, {2, 1}, {2, 2}};
        REQUIRE(st.size() == want.size());
        for (size_t k = 0; k < want.size(); ++k) {
            INFO("step " << k);
            CHECK(st[k].pattern == want[k].first);
            CHECK(st[k].step == want[k].second);
        }
        CHECK(r.e.seqStatus().songPos == 2);
    }

    SECTION("entry outside the Song: clamped to the last one") {
        r.e.seqSongJump(0, 99);
        r.render(64);
        CHECK(r.e.seqStatus().songPos == 2);
        CHECK(r.e.seqStatus().pattern == 2);
    }
}

TEST_CASE("Sequencer: length changed during playback, the current step is kept", "[engine][seq]") {
    Rig r;
    r.pattern(0, 8);
    r.e.seqPlay(0);
    r.render(6000 * 5 + 10);  // step 5 in progress
    r.pattern(0, 4);
    CHECK(r.e.seqStatus().step == 3);
    CHECK(r.e.seqStatus().stepAbs == 3);
    r.render(6000);
    const auto st = r.steps();
    REQUIRE(st.size() == 7);
    CHECK(st[6].step == 0);
    CHECK(st[6].stepAbs == 4);
}

TEST_CASE("Sequencer: MIDI clock, steps according to the resolution, Stop, SPP and Continue", "[engine][seq]") {
    Rig r(SeqClock::Midi);
    r.g.resolution = 1;  // 1/8: 12 pulses per step
    r.global();
    r.pattern(0, 16);
    r.e.seqPlay(0);
    r.render(100);
    CHECK(r.steps().empty());  // no Start: the clock does not advance
    r.e.midiClock(0);
    r.render(100);
    CHECK(r.steps().empty());
    r.e.midiStart(0);
    for (int k = 0; k < 36; ++k) {  // three steps, at 120 BPM
        r.e.midiClock(0);
        r.render(1000);
    }
    auto st = r.steps();
    REQUIRE(st.size() == 3);
    CHECK(st[0].stepAbs == 0);
    CHECK(st[2].stepAbs == 2);
    CHECK(r.e.seqStatus().clockRunning);
    CHECK(std::fabs(r.e.seqStatus().externalBpm - 120) < 1);
    r.e.midiStop(0);
    for (int k = 0; k < 24; ++k) {
        r.e.midiClock(0);
        r.render(1000);
    }
    CHECK(r.steps().size() == 3);
    CHECK_FALSE(r.e.seqStatus().clockRunning);
    r.e.midiSongPosition(0, 16);  // 16 sixteenth notes = 8 eighth notes
    r.e.midiContinue(0);
    r.e.midiClock(0);
    r.render(1000);
    st = r.steps();
    REQUIRE(st.size() == 4);
    CHECK(st[3].stepAbs == 8);
    CHECK(st[3].step == 8);
    // Position in the middle of a step: the next step comes at its first pulse.
    r.e.midiStop(0);
    r.e.midiSongPosition(0, 21);  // 126 pulses: step 10 (pulses 120 to 131) begun
    r.e.midiContinue(0);
    for (int k = 0; k < 6; ++k) {  // pulses 126 to 131
        r.e.midiClock(0);
        r.render(1000);
    }
    CHECK(r.steps().size() == 4);
    r.e.midiClock(0);  // pulse 132: step 11
    r.render(1000);
    st = r.steps();
    REQUIRE(st.size() == 5);
    CHECK(st[4].stepAbs == 11);
}

// D-422: 1/64, the eighth resolution, 16 steps per quarter note. On the MIDI clock a step lasts a pulse and a half:
// every other step starts on the half pulse between two clocks, as the arpeggiator's 1/64 (D-289).
TEST_CASE("Sequencer: 1/64 resolution, internal clock and MIDI clock in half pulses, SPP (D-422)", "[engine][seq]") {
    SECTION("internal clock: 1500 samples per step at 120 BPM") {
        Rig r;
        r.g.resolution = 7;
        r.global();
        r.pattern(0, 32);
        r.e.seqPlay(0);
        r.render(24000 - 10);  // one quarter note
        const auto st = r.steps();
        REQUIRE(st.size() == 16);
        for (int k = 0; k < 16; ++k) {
            INFO("step " << k);
            CHECK(st[size_t(k)].stepAbs == k);
            CHECK(std::fabs(double(st[size_t(k)].sample) - 1500.0 * k) <= 1);
            CHECK(std::fabs(st[size_t(k)].samples - 1500) < 1e-6);
        }
    }
    SECTION("MIDI clock: a step every pulse and a half, then SPP") {
        Rig r(SeqClock::Midi);
        r.g.resolution = 7;
        r.global();
        r.pattern(0, 32);
        r.e.seqPlay(0);
        r.e.midiStart(0);
        for (int k = 0; k < 6; ++k) {  // pulses 0 to 5 at 120 BPM, 1000 samples apart
            r.e.midiClock(0);
            r.render(1000);
        }
        auto st = r.steps();
        REQUIRE(st.size() == 4);  // on pulse 0, between pulses 1 and 2, on pulse 3, between pulses 4 and 5
        for (int k = 0; k < 4; ++k) {
            INFO("step " << k);
            CHECK(st[size_t(k)].stepAbs == k);
            CHECK(std::fabs(double(st[size_t(k)].sample) - 1500.0 * k) <= 1);
            CHECK(std::fabs(st[size_t(k)].samples - 1500) < 1e-6);
        }
        // SPP: 1 sixteenth note = 6 pulses = 4 steps of 1/64; the next clock plays step 4, step 5 comes at the half
        // pulse after the clock that follows.
        r.e.midiStop(0);
        r.e.midiSongPosition(0, 1);
        r.e.midiContinue(0);
        const uint64_t from = r.at;
        for (int k = 0; k < 2; ++k) {
            r.e.midiClock(0);
            r.render(1000);
        }
        st = r.steps();
        REQUIRE(st.size() == 6);
        CHECK(st[4].stepAbs == 4);
        CHECK(st[4].sample == from);
        CHECK(st[5].stepAbs == 5);
        CHECK(std::fabs(double(st[5].sample) - double(from + 1500)) <= 1);
        // SPP 3: 18 pulses, 12 steps, on a clock as well.
        r.e.midiStop(0);
        r.e.midiSongPosition(0, 3);
        r.e.midiContinue(0);
        r.e.midiClock(0);
        r.render(1000);
        st = r.steps();
        REQUIRE(st.size() == 7);
        CHECK(st[6].stepAbs == 12);
        // Without the next clock, only the half pulse after the last one: step 13 waits for the clock.
        r.render(3000);
        CHECK(r.steps().size() == 7);
    }
    SECTION("the arpeggiator's 1/64 and the sequencer's share the half pulse without counting it twice") {
        Rig r(SeqClock::Midi);
        r.g.resolution = 2;  // 1/16: the half pulses only for the arpeggio
        r.g.arpDivision = 3;  // 1/64
        r.global();
        r.pattern(0, 32);
        r.e.seqPlay(0);
        r.e.midiStart(0);
        for (int k = 0; k < 13; ++k) {
            r.e.midiClock(0);
            r.render(1000);
        }
        auto st = r.steps();
        REQUIRE(st.size() == 3);  // pulses 0, 6, 12
        CHECK(st[1].sample == 6000);
        CHECK(st[2].sample == 12000);
        r.g.resolution = 7;  // both on the half pulses
        r.global();
        r.e.midiStop(0);
        r.e.midiStart(0);
        const uint64_t from = r.at;
        for (int k = 0; k < 3; ++k) {
            r.e.midiClock(0);
            r.render(1000);
        }
        st = r.steps();
        REQUIRE(st.size() == 5);
        CHECK(st[3].sample == from);
        CHECK(std::fabs(double(st[4].sample) - double(from + 1500)) <= 1);
    }
}

// D-433: 1/32T, the ninth resolution, 12 steps per quarter note. On the MIDI clock a step lasts two pulses.
TEST_CASE("Sequencer: 1/32T resolution, internal clock, MIDI clock and SPP (D-433)", "[engine][seq]") {
    SECTION("internal clock: 2000 samples per step at 120 BPM") {
        Rig r;
        r.g.resolution = 8;
        r.global();
        r.pattern(0, 32);
        r.e.seqPlay(0);
        r.render(24000 - 10);  // one quarter note
        const auto st = r.steps();
        REQUIRE(st.size() == 12);
        for (int k = 0; k < 12; ++k) {
            INFO("step " << k);
            CHECK(st[size_t(k)].stepAbs == k);
            CHECK(std::fabs(double(st[size_t(k)].sample) - 2000.0 * k) <= 1);
            CHECK(std::fabs(st[size_t(k)].samples - 2000) < 1e-6);
        }
    }
    SECTION("MIDI clock: a step every two pulses, then SPP") {
        Rig r(SeqClock::Midi);
        r.g.resolution = 8;
        r.global();
        r.pattern(0, 32);
        r.e.seqPlay(0);
        r.e.midiStart(0);
        for (int k = 0; k < 6; ++k) {  // pulses 0 to 5 at 120 BPM, 1000 samples apart
            r.e.midiClock(0);
            r.render(1000);
        }
        auto st = r.steps();
        REQUIRE(st.size() == 3);  // on pulses 0, 2 and 4, none between two clocks
        for (int k = 0; k < 3; ++k) {
            INFO("step " << k);
            CHECK(st[size_t(k)].stepAbs == k);
            CHECK(st[size_t(k)].sample == uint64_t(2000 * k));
            CHECK(std::fabs(st[size_t(k)].samples - 2000) < 1e-6);
        }
        // SPP 2: 2 sixteenth notes = 12 pulses = 6 steps of 1/32T; the next clock plays step 6, the third one step 7.
        r.e.midiStop(0);
        r.e.midiSongPosition(0, 2);
        r.e.midiContinue(0);
        const uint64_t from = r.at;
        for (int k = 0; k < 3; ++k) {
            r.e.midiClock(0);
            r.render(1000);
        }
        st = r.steps();
        REQUIRE(st.size() == 5);
        CHECK(st[3].stepAbs == 6);
        CHECK(st[3].sample == from);
        CHECK(st[4].stepAbs == 7);
        CHECK(st[4].sample == from + 2000);
    }
}

TEST_CASE("Sequencer: MIDI Start and SPP without effect with the internal clock", "[engine][seq]") {
    Rig r;
    r.pattern(0, 16);
    r.e.seqPlay(0);
    r.render(6000 * 3 - 10);
    r.e.midiStart(0);
    r.e.midiSongPosition(0, 40);
    r.render(6000 * 2);
    const auto st = r.steps();
    REQUIRE(st.size() == 5);
    for (int k = 0; k < 5; ++k) CHECK(st[size_t(k)].stepAbs == k);
}

TEST_CASE("Sequencer: swing and triplet resolutions", "[engine][seq]") {
    Rig r;
    r.g.swing = 66;
    r.g.resolution = 5;  // 1/8T: 3 steps per quarter note
    r.global();
    r.pattern(0, 6);
    r.e.seqPlay(0);
    r.render(48000);
    const auto st = r.steps();
    REQUIRE(st.size() >= 4);
    const double base = 48000.0 * 60 / (120 * 3);
    CHECK(std::fabs(st[0].samples - base * 66 / 50) < 1e-6);
    CHECK(std::fabs(st[1].samples - base * (2 - 66.0 / 50)) < 1e-6);
    // The starts of the steps follow the swung durations (within one sample).
    CHECK(std::fabs(double(st[1].sample) - base * 66 / 50) <= 1);
    CHECK(std::fabs(double(st[2].sample) - 2 * base) <= 1);
}

// D-133: the countdowns of the tracks only run at their deadline. Render in blocks of one sample (tracks caught up
// at the end of each block, so at each sample, as before D-133) and of 512: same log, same sound.
TEST_CASE("Sequencer: countdowns by deadline, same log and same sound in blocks of 1 and of 512 samples",
          "[engine][seq]") {
    SeqData data;
    auto melo = std::make_shared<SeqLane>();  // track 1: varied gates, TIE, SLD, chord voices of several steps
    auto drum = std::make_shared<SeqLane>();  // track 2: Chipdrum, CUT, retriggers, TIE
    auto arp = std::make_shared<SeqLane>();   // track 3: track arpeggio (ARP), slide under the arpeggio (ARP + SLD)
    for (int s = 0; s < 16; ++s) {
        const size_t k = size_t(s);
        melo->pat[k] = s % 4 == 3 ? -1 : int16_t((s * 5) % 12);
        melo->gat[k] = uint8_t(s % 3 == 0 ? 0 : 20 + 25 * (s % 4));
        melo->tie[k] = s % 4 == 3;
        melo->slide[k] = s == 5 || s == 9;
        if (s % 2 == 0) {
            melo->vpat[0][k] = int16_t((s + 4) % 12);
            melo->vdur[0][k] = uint16_t(1 + s % 3);
            melo->vpat[2][k] = 7;
        }
        drum->pat[k] = s % 3 == 2 ? -1 : int16_t(s % 8);
        drum->cut[k] = s % 3 == 2;
        drum->rtg[k] = uint8_t(s % 5 == 1 ? 3 : 0);
        drum->tie[k] = s == 7;
        arp->pat[k] = s % 4 < 2 ? int16_t(s % 12) : -1;
        if (s % 8 == 0 || s % 8 == 2) arp->setCmd(s, seqCmd(SeqCmd::Arpeggiator, s % 8 == 0));  // J01 over 2 steps
        arp->slide[k] = s == 4;
    }
    data.patterns[0].steps = 16;
    data.patterns[0].lanes[0] = melo;
    data.patterns[0].lanes[1] = drum;
    data.patterns[0].lanes[2] = arp;

    struct Out {
        std::vector<Engine::SeqLogRecord> log;
        std::vector<float> left, right;
    };
    constexpr uint32_t kTotal = 48000 * 3;
    for (const SeqClock clock : {SeqClock::Internal, SeqClock::Host}) {
        CAPTURE(int(clock));
        auto run = [&](uint32_t block) {
            Out o;
            Engine e;
            EngineConfig cfg;
            cfg.render.hostRate = 48000;
            e.prepare(cfg);
            GlobalParams g;
            g.seqClock = clock;
            g.tempo = 140;
            g.swing = 58;
            e.setGlobal(g);
            TrackParams t;
            t.mix.enabled = true;
            e.setTrack(0, t);
            t.patch.generator = Generator::Chipdrum;
            e.setTrack(1, t);
            t.patch.generator = Generator::Chip;
            t.arp.on = true;
            t.arp.rateHz = 37;
            e.setTrack(2, t);
            o.log.reserve(200000);
            e.setSeqLog(&o.log);
            e.setSequence(&data);
            if (clock == SeqClock::Internal) e.seqPlay(0);
            o.left.resize(kTotal);
            o.right.resize(kTotal);
            for (uint32_t at = 0; at < kTotal; at += block) {
                const uint32_t n = std::min(block, kTotal - at);
                if (clock == SeqClock::Host) {
                    Engine::HostTransport h;
                    h.playing = true;
                    h.bpm = 140;
                    h.ppq = double(at) / 48000 * 140 / 60;
                    e.setHostTransport(h);
                }
                e.render(o.left.data() + at, o.right.data() + at, n);
            }
            return o;
        };
        const Out a = run(1), b = run(512);
        REQUIRE(a.log.size() > 200);
        REQUIRE(a.log.size() == b.log.size());
        size_t same = 0;
        for (size_t k = 0; k < a.log.size(); ++k) {
            const auto &x = a.log[k], &y = b.log[k];
            if (x.sample != y.sample || x.op != y.op || x.voice != y.voice || x.a != y.a || x.b != y.b || x.x != y.x ||
                x.y != y.y || x.z != y.z) {
                CAPTURE(k, x.sample, x.op, y.sample, y.op);
                FAIL_CHECK("different logs");
                break;
            }
            ++same;
        }
        CHECK(same == a.log.size());
        // The scenario goes through the hits (T), the gates of the chord voices (R), the cuts (K) and the slides (S).
        std::string ops;
        for (const auto& x : a.log)
            if (ops.find(x.op) == std::string::npos) ops += x.op;
        for (const char op : {'T', 'R', 'K', 'S'}) CHECK(ops.find(op) != std::string::npos);
        CHECK(a.left == b.left);
        CHECK(a.right == b.right);
    }
}

// A track switched off falls silent, as laneOn 1 → 0 of the prototype's chan message (killLane): no step of the
// track runs any more, so a note held by TIE or SLD, the chord voices of several steps, the retriggers (RTG) and the
// track arpeggio of the step are released at once, through the mixer (setTrackMix) as through the sound of the track
// (setTrack). Demo pattern in Authentic 3ch: a track switched off in the middle of its tied Sync Buzzer droned over
// the notes of the others (DAW test, item 9).
TEST_CASE("Sequencer: a track switched off releases its held, slid, chord, retriggered and arpeggiated notes",
          "[engine][seq]") {
    SeqData data;
    auto held = std::make_shared<SeqLane>();  // track 1: note tied over the 4 steps, chord voice of 4 steps
    auto slid = std::make_shared<SeqLane>();  // track 2: SLD from step 0 to step 3
    auto rtg = std::make_shared<SeqLane>();   // track 3: 8 hits in step 0
    auto arp = std::make_shared<SeqLane>();   // track 4 (16 channels): arpeggio of the track over a tie
    held->pat[0] = 0;
    held->vpat[0][0] = 4;
    held->vdur[0][0] = 4;
    slid->pat[0] = 0;
    slid->pat[3] = 7;
    rtg->pat[0] = 4;
    rtg->rtg[0] = 7;
    arp->pat[0] = 2;
    arp->setCmd(0, seqCmd(SeqCmd::Arpeggiator, 1));  // the arpeggio on step 0 alone
    arp->setCmd(1, seqCmd(SeqCmd::Arpeggiator, 0));
    for (int s = 0; s < 4; ++s) {
        held->tie[size_t(s)] = arp->tie[size_t(s)] = 1;
        slid->slide[size_t(s)] = s < 3;
    }
    data.patterns[0].steps = 4;
    data.patterns[0].lanes[0] = held;
    data.patterns[0].lanes[1] = slid;
    data.patterns[0].lanes[2] = rtg;
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3})
        for (const bool viaMixer : {true, false}) {
            CAPTURE(int(mode), viaMixer);
            const bool m3 = mode == VoiceMode::Authentic3;
            data.patterns[0].lanes[3] = m3 ? nullptr : arp;  // 3 channels: track 4 is the Digidrum track
            Rig r;
            r.g.mode = mode;
            r.global();
            const int tracks = m3 ? 3 : 4;
            std::array<TrackParams, 4> tp{};
            for (int t = 0; t < tracks; ++t) {
                tp[size_t(t)].mix.enabled = true;
                tp[size_t(t)].arp.on = t == 3;
                tp[size_t(t)].arp.rateHz = 40;
                r.e.setTrack(t, tp[size_t(t)]);
            }
            r.e.setSequence(&data);
            r.e.seqPlay(0);
            r.render(1000);  // step 0: the first two retriggers have played
            auto sounding = [&](int t) {
                for (int i = 0; i < Engine::kVoices; ++i) {
                    const Engine::VoiceInfo v = r.e.voice(i);
                    if (v.on && v.track == t && !v.fading) return true;
                }
                return false;
            };
            for (int t = 0; t < tracks; ++t) REQUIRE(sounding(t));
            const uint64_t off = r.at;
            for (int t = 0; t < tracks; ++t) {
                tp[size_t(t)].mix.enabled = false;
                if (viaMixer) r.e.setTrackMix(0, t, tp[size_t(t)].mix);
                else r.e.setTrack(t, tp[size_t(t)]);
            }
            r.render(6000 * 6);  // past the end of the pattern and its loop
            for (int t = 0; t < tracks; ++t) {
                CAPTURE(t);
                CHECK_FALSE(sounding(t));
                int later = 0;  // notes attacked after the switch-off
                for (const auto& x : r.log) later += x.op == 'T' && x.track == t && x.sample > off;
                CHECK(later == 0);
            }
            CHECK(r.e.activeVoices() == 0);
        }
}

TEST_CASE("Sequencer: overlapping chord voices of one rank all sound to their end (D-281)", "[engine][seq]") {
    // Indus's case (21/10/2026): A3 held from step 4 to 8, G3 from step 6 to 10 and F3 from step 8 to 12, the two
    // last written as voice 2 (the first free voice of the grid at their step).
    SeqData data;
    auto l = std::make_shared<SeqLane>();
    l->pat[4] = 9;
    for (int s = 4; s <= 8; ++s) l->tie[size_t(s)] = 1;
    l->vpat[0][6] = 7;
    l->vdur[0][6] = 5;
    l->vpat[0][8] = 5;
    l->vdur[0][8] = 5;
    data.patterns[0].steps = 16;
    data.patterns[0].lanes[0] = l;
    Rig r;
    r.e.setSequence(&data);
    r.e.seqPlay(0);
    auto notes = [&r] {
        std::vector<int> out;
        for (int i = 0; i < Engine::kVoices; ++i) {
            const Engine::VoiceInfo v = r.e.voice(i);
            if (v.on && v.track == 0 && !v.fading) out.push_back(v.note);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    const std::vector<std::vector<int>> want = {{},       {},           {},       {},       {57}, {57},
                                                {55, 57}, {55, 57},     {53, 55, 57},       {53, 55},
                                                {53, 55}, {53},         {53},     {},       {},   {}};
    for (int s = 0; s < 16; ++s) {
        r.render(s == 0 ? 3000 : 6000);  // middle of each step
        CAPTURE(s);
        CHECK(notes() == want[size_t(s)]);
    }
}

TEST_CASE("Sequencer: the Env entries of the SRC row put the volume on the envelope, the others keep the track's",
          "[engine][seq]") {
    // Sixth DAW test, item 79 (D-277): one note tied over four steps, SRC Tone+Env, Tone, Env, then the track's.
    for (const bool trackEnv : {false, true}) {
        CAPTURE(trackEnv);
        Rig r;
        TrackParams t;
        t.mix.enabled = true;
        t.patch.envelopeVolume = trackEnv;
        r.e.setTrack(0, t);
        auto l = std::make_shared<SeqLane>();
        l->pat[0] = 0;
        const uint8_t mix[4] = {5, 1, 4, 0};
        for (size_t s = 0; s < 4; ++s) {
            l->tie[s] = 1;
            l->mix[s] = mix[s];
        }
        r.data.patterns[0].steps = 4;
        r.data.patterns[0].lanes[0] = l;
        r.e.setSequence(&r.data);
        r.e.seqPlay(0);
        struct Want {
            bool tone, noise, env;
        };
        const Want want[4] = {
            {true, false, true}, {true, false, trackEnv}, {false, false, true}, {true, false, trackEnv}};
        r.render(3000);  // middle of step 0 (6000 samples per step)
        for (const Want& w : want) {
            const auto v = r.e.voice(0);
            CHECK(v.on);
            CHECK(v.tone == w.tone);
            CHECK(v.noise == w.noise);
            CHECK(v.envelope == w.env);
            r.render(6000);
        }
    }
}


// Eleventh DAW test, items 134 and 136 (D-343, D-345): VOL, RTG, ARP and GAT act on each step that a held note
// crosses, not only on its first step.
TEST_CASE("Sequencer: VOL, RTG, ARP and GAT of the steps of a held note act at each step (D-343, D-345)",
          "[engine][seq]") {
    auto held = [](const std::array<uint8_t, 4>& vol) {
        auto l = std::make_shared<SeqLane>();
        l->pat[0] = 0;
        for (size_t s = 0; s < 4; ++s) {
            l->tie[s] = 1;
            l->vol[s] = vol[s];
        }
        return l;
    };
    auto attacks = [](const Rig& r, uint64_t from, uint64_t to) {
        std::vector<double> out;
        for (const auto& x : r.log)
            if (x.op == 'T' && x.track == 0 && x.sample >= from && x.sample < to) out.push_back(x.x);
        return out;
    };
    SECTION("VOL: the 4-bit volume in Authentic 3ch, the level of the voice in Studio 16ch") {
        for (const VoiceMode mode : {VoiceMode::Authentic3, VoiceMode::Studio16}) {
            CAPTURE(int(mode));
            Rig r;
            r.g.mode = mode;
            r.global();
            r.data.patterns[0].steps = 4;
            r.data.patterns[0].lanes[0] = held({15, 8, 4, 15});
            r.e.setSequence(&r.data);
            r.e.seqPlay(0);
            std::array<int, 4> vol{};
            for (size_t s = 0; s < 4; ++s) {
                r.render(s == 0 ? 3000 : 6000);  // middle of each step
                CHECK(r.e.voice(0).on);
                vol[s] = r.e.voice(0).volume;
            }
            CHECK(attacks(r, 0, r.at).size() == 1);  // held: one attack
            std::vector<double> levels;
            for (const auto& x : r.log)
                if (x.op == 'A' && x.voice == 0) levels.push_back(x.x);
            const double base = mode == VoiceMode::Authentic3 ? TrackMix{}.level : 1.0;  // the track's level in 3ch
            REQUIRE(levels.size() == 3);
            CHECK(levels[0] == base * 8 / 15.0);
            CHECK(levels[1] == base * 4 / 15.0);
            CHECK(levels[2] == base * 15 / 15.0);
            if (mode == VoiceMode::Authentic3) {
                CHECK(vol[1] < vol[0]);
                CHECK(vol[2] < vol[1]);
                CHECK(vol[3] == vol[0]);
            }
        }
    }
    SECTION("RTG: the held note struck again from the start of its step") {
        Rig r;
        auto l = held({15, 15, 15, 15});
        l->rtg[2] = 2;  // 3 hits in step 2
        r.data.patterns[0].steps = 4;
        r.data.patterns[0].lanes[0] = l;
        r.e.setSequence(&r.data);
        r.e.seqPlay(0);
        r.render(6000 * 4 - 100);
        const std::vector<double> first = attacks(r, 0, 6000);
        REQUIRE(first.size() == 1);
        CHECK(attacks(r, 6000, 12000).empty());
        CHECK(attacks(r, 12000, 18000) == std::vector<double>(3, first[0]));
        CHECK(attacks(r, 18000, 24000).empty());
    }
    SECTION("GAT on the last step of a held note: released at that share of the step") {
        Rig r;
        auto l = held({15, 15, 15, 15});
        l->gat[1] = 25;  // not the last step: no effect
        l->gat[3] = 50;
        r.data.patterns[0].steps = 8;
        r.data.patterns[0].lanes[0] = l;
        r.e.setSequence(&r.data);
        r.e.seqPlay(0);
        r.render(6000 * 5);
        std::vector<uint64_t> cuts;
        for (const auto& x : r.log)
            if (x.op == 'K' && x.voice == 0) cuts.push_back(x.sample);
        REQUIRE_FALSE(cuts.empty());
        CHECK(std::llabs(int64_t(cuts[0]) - (18000 + 3000)) <= 1);  // middle of step 3 (countdown rounding)
    }
    SECTION("ARP lit on the third step of a held note: the arpeggio starts there") {
        Rig r;
        TrackParams t;
        t.mix.enabled = true;
        t.arp.on = true;
        t.arp.rateHz = 40;
        r.e.setTrack(0, t);
        auto l = held({15, 15, 15, 15});
        l->setCmd(2, seqCmd(SeqCmd::Arpeggiator, 1));  // the arpeggio on step 2 alone
        l->setCmd(3, seqCmd(SeqCmd::Arpeggiator, 0));
        r.data.patterns[0].steps = 4;
        r.data.patterns[0].lanes[0] = l;
        r.e.setSequence(&r.data);
        r.e.seqPlay(0);
        r.render(6000 * 4 - 100);
        CHECK(attacks(r, 0, 12000).size() == 1);  // the held note alone
        const std::vector<double> arp = attacks(r, 12000, 18000);
        CHECK(arp.size() >= 4);  // 40 Hz over a step of 1/8 s
        CHECK(std::any_of(arp.begin(), arp.end(), [&](double n) { return n != arp[0]; }));
        CHECK(attacks(r, 18000, 24000).empty());  // ARP Off on step 3 (D-379)
    }
}

// Bug of 24/10/2026 (D-379): ARP On on the first step of a note held from step 4 to step 12, ARP Off on step 8. The
// arpeggio stops on step 8 and the note goes on at its own pitch to its end, instead of arpeggiating to its end.
TEST_CASE("Sequencer: ARP Off on a step of a held note stops its arpeggio there (D-379)", "[engine][seq]") {
    constexpr int16_t kF3 = 5;  // F of the track octave
    auto run = [&](bool off, VoiceMode mode) {
        auto r = std::make_unique<Rig>();
        r->g.mode = mode;
        r->global();
        TrackParams t;
        t.mix.enabled = true;
        t.arp.on = true;
        t.arp.rateHz = 40;
        r->e.setTrack(0, t);
        auto l = std::make_shared<SeqLane>();
        l->pat[3] = kF3;  // steps 4 to 12 of the grid
        for (size_t s = 3; s <= 11; ++s) l->tie[s] = 1;
        l->setCmd(3, seqCmd(SeqCmd::Arpeggiator, 1));
        if (off) l->setCmd(7, seqCmd(SeqCmd::Arpeggiator, 0));
        r->data.patterns[0].steps = 16;
        r->data.patterns[0].lanes[0] = l;
        r->e.setSequence(&r->data);
        r->e.seqPlay(0);
        return r;
    };
    auto ops = [](const Rig& r, char op, uint64_t from, uint64_t to) {
        std::vector<double> out;
        for (const auto& x : r.log)
            if (x.op == op && x.voice == 0 && x.sample >= from && x.sample < to) out.push_back(x.x);
        return out;
    };
    for (const VoiceMode mode : {VoiceMode::Authentic3, VoiceMode::Studio16}) {
        CAPTURE(int(mode));
        auto r = run(true, mode);
        r->render(6000 * 7 + 100);  // just after the start of step 8
        const std::vector<double> arp = ops(*r, 'T', 18000, 42000);
        REQUIRE(arp.size() >= 16);  // 40 Hz over four steps of 1/8 s
        CHECK(std::any_of(arp.begin(), arp.end(), [&](double n) { return n != arp[0]; }));
        const std::vector<double> back = ops(*r, 'N', 42000, r->at);
        REQUIRE(back.size() == 1);  // at once on step 8, back to the root
        CHECK(back[0] == arp[0]);
        r->render(6000 * 5 - 200);  // to the end of step 12
        CHECK(ops(*r, 'T', 42000, r->at).empty());  // no more attacks
        CHECK(ops(*r, 'N', 42000, r->at).size() == 1);
        CHECK(ops(*r, 'K', 42000, r->at).empty());
        CHECK(r->e.voice(0).on);
        CHECK(r->e.voice(0).note == int(arp[0]));
        r->render(400);  // step 13: the note ends
        CHECK(ops(*r, 'K', 72000, r->at).size() == 1);
        // Without ARP Off, the arpeggio runs to the end of the note, as before.
        auto on = run(false, mode);
        on->render(6000 * 12 - 200);
        CHECK(ops(*on, 'T', 42000, on->at).size() >= 20);
    }
}

// Indus, 24/10/2026 20:33 (D-416): an arpeggio is one note per note of the sequencer, not one per step of the
// arpeggio: in Polyphonic as in Monophonic, the envelope of the note runs on through the steps of its ARP row, and the
// next note of the sequencer starts it again from the attack; at the end of the note, the note walks on through the
// arpeggio during its release (19:32).
TEST_CASE("Sequencer: ARP row, one ADSR per note of the sequencer, running on through the steps of the arpeggio "
          "(D-416)",
          "[engine][seq][arp]") {
    for (const bool mono : {false, true}) {
        CAPTURE(mono);
        Rig r;
        r.g.mode = VoiceMode::Studio16;
        r.global();
        TrackParams t;
        t.mix.enabled = true;
        t.arp.on = true;
        t.arp.rateHz = 40;  // five steps of the arpeggio per step of 1/8 s
        t.patch.attackMs = 5;
        t.patch.sustainPct = 100;
        t.patch.releaseMs = 300;
        t.patch.mono = mono;
        r.e.setTrack(0, t);
        auto l = std::make_shared<SeqLane>();
        l->pat[0] = 0;  // held over steps 1 to 4, two empty steps, a second note over steps 7 and 8
        for (size_t s = 0; s < 4; ++s) l->tie[s] = 1;
        l->pat[6] = 0;
        l->tie[6] = 1;
        l->setCmd(0, seqCmd(SeqCmd::Arpeggiator, 1));
        l->setCmd(6, seqCmd(SeqCmd::Arpeggiator, 1));
        r.data.patterns[0].steps = 8;
        r.data.patterns[0].lanes[0] = l;
        r.e.setSequence(&r.data);
        r.e.seqPlay(0);
        auto ops = [&](char op, int v0, int v1, uint64_t from, uint64_t to) {
            int n = 0;
            for (const auto& x : r.log)
                n += x.op == op && x.voice >= v0 && x.voice < v1 && x.sample >= from && x.sample < to;
            return n;
        };
        constexpr int kChord0 = 16, kChord1 = 21;  // chord voices of track 1 (kLaneVoices, kSeqChordVoices)
        int attacks = 0;
        while (r.at < 24000 - 64) {  // the held note: each step of the arpeggio carries its envelope on
            r.render(32);
            const int a = ops('T', 0, 1, 0, r.at);
            if (a > attacks && attacks > 0) CHECK(r.e.voice(0).adsrStage != 1);
            attacks = a;
        }
        CHECK(attacks >= 18);
        CHECK(ops('K', 0, 1, 0, r.at) == attacks - 1);
        CHECK(ops('R', kChord0, kChord1, 0, r.at) == 0);
        CHECK(r.e.voice(0).adsrStage == 3);
        CHECK(r.e.voice(0).adsrNow > 0.99);
        for (int v = kChord0; v < kChord1; ++v) CHECK_FALSE(r.e.voice(v).on);
        // End of the note: no new attack, the note walks on through the arpeggio in its release.
        r.render(24000 + 6016 - r.at);
        CHECK(ops('T', 0, 1, 24000 + 32, r.at) == 0);
        CHECK(r.e.voice(0).on);
        CHECK(r.e.voice(0).adsrStage == 4);
        CHECK(ops('N', 0, 1, 24000, r.at) >= 4);
        // The next note of the sequencer: from the attack.
        r.render(36000 + 32 - r.at);
        CHECK(ops('T', 0, 1, 36000 - 32, r.at) == 1);
        CHECK(r.e.voice(0).adsrStage == 1);
        r.render(1600);
        CHECK(r.e.voice(0).adsrStage == 3);
    }
}

TEST_CASE("Sequencer: the next note does not cut the release of a note on a polyphonic track in 16 channels (D-421)",
          "[engine][seq]") {
    // Indus's case (24/10/2026 22:41): C#4 held over steps 4 to 12, B3 over steps 13 and 14, ADSR 1000 ms, 1000 ms,
    // 0 %, 2000 ms. Polyphonic: C#4 goes on to its release on a chord voice of the track while B3 starts; Monophonic
    // cuts it, as before.
    for (const bool mono : {false, true}) {
        CAPTURE(mono);
        Rig r;
        r.g.mode = VoiceMode::Studio16;
        r.global();
        TrackParams t;
        t.mix.enabled = true;
        t.patch.attackMs = 1000;
        t.patch.decayMs = 1000;
        t.patch.sustainPct = 0;
        t.patch.releaseMs = 2000;
        t.patch.mono = mono;
        r.e.setTrack(0, t);
        auto l = std::make_shared<SeqLane>();
        l->pat[3] = 1;  // C#4
        l->oct[3] = 4;
        for (size_t s = 3; s <= 11; ++s) l->tie[s] = 1;
        l->pat[12] = 11;  // B3
        l->tie[12] = l->tie[13] = 1;
        r.data.patterns[0].steps = 16;
        r.data.patterns[0].lanes[0] = l;
        r.e.setSequence(&r.data);
        r.e.seqPlay(0);
        std::vector<float> out;
        auto play = [&](uint64_t to) {
            std::vector<float> a(512), b(512);
            while (r.at < to) {
                const uint32_t k = uint32_t(std::min<uint64_t>(to - r.at, a.size()));
                r.e.render(a.data(), b.data(), k);
                for (uint32_t j = 0; j < k; ++j) out.push_back(a[j] + b[j]);
                r.at += k;
            }
        };
        auto rms = [&](uint64_t from, uint64_t to) {
            double e = 0;
            for (uint64_t j = from; j < to; ++j) e += double(out[size_t(j)]) * out[size_t(j)];
            return std::sqrt(e / double(to - from));
        };
        auto sounding = [&](int note) {
            for (int i = 0; i < Engine::kVoices; ++i) {
                const Engine::VoiceInfo v = r.e.voice(i);
                if (v.on && v.track == 0 && v.note == note && !v.fading) return i;
            }
            return -1;
        };
        constexpr uint64_t kStep = 6000, kB3 = 12 * kStep;
        play(kB3 + kStep / 2);  // middle of step 13: B3 in its attack
        const int b3 = sounding(59);
        REQUIRE(b3 == 0);
        CHECK(r.e.voice(b3).adsrStage == 1);
        const int cs4 = sounding(61);
        const double held = rms(kB3 - kStep, kB3), after = rms(kB3 + 64, kB3 + kStep / 2);
        CAPTURE(held, after);
        REQUIRE(held > 0.01);
        if (mono) {
            CHECK(cs4 < 0);
            CHECK(after < held * 0.6);  // B3 alone
            continue;
        }
        REQUIRE(cs4 >= 16);  // a chord voice of track 1 (kLaneVoices)
        CHECK(cs4 < 21);
        CHECK(r.e.voice(cs4).tag == VoiceTag::Chord);
        CHECK(r.e.voice(cs4).adsrStage == 4);
        CHECK(r.e.voice(cs4).adsrNow > 0.6);
        CHECK(after > held * 0.8);  // no break in the sound: C#4 carries on, B3 starts
        // End of B3 (step 15): both release, C#4 down to its end two seconds after B3's attack.
        play(kB3 + 3 * kStep);
        CHECK(sounding(61) == cs4);
        CHECK(r.e.voice(cs4).adsrStage == 4);
        CHECK(r.e.voice(b3).on);
        CHECK(r.e.voice(b3).adsrStage == 4);
        play(kB3 + 2 * 48000 + 600);
        CHECK_FALSE(r.e.voice(cs4).on);
    }
}

TEST_CASE("Sequencer: a chord does not cut the release of the chord before while a voice of the track is silent "
          "(D-421)",
          "[engine][seq]") {
    // C, E, G on step 1, then D, F, A on step 2, a release of one second: the six notes sound together on step 2,
    // the first chord in its release on the voices of the track that were silent.
    Rig r;
    r.g.mode = VoiceMode::Studio16;
    r.global();
    TrackParams t;
    t.mix.enabled = true;
    t.patch.attackMs = 5;
    t.patch.releaseMs = 1000;
    r.e.setTrack(0, t);
    auto l = std::make_shared<SeqLane>();
    l->pat[0] = 0;
    l->vpat[0][0] = 4;
    l->vpat[1][0] = 7;
    l->pat[1] = 2;
    l->vpat[0][1] = 5;
    l->vpat[1][1] = 9;
    for (int k = 0; k < 2; ++k) l->vdur[size_t(k)][0] = l->vdur[size_t(k)][1] = 1;
    r.data.patterns[0].steps = 16;
    r.data.patterns[0].lanes[0] = l;
    r.e.setSequence(&r.data);
    r.e.seqPlay(0);
    auto notes = [&r](bool released) {
        std::vector<int> out;
        for (int i = 0; i < Engine::kVoices; ++i) {
            const Engine::VoiceInfo v = r.e.voice(i);
            if (v.on && v.track == 0 && !v.fading && (v.adsrStage == 4) == released) out.push_back(v.note);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    r.render(6000 + 1000);  // step 2, before its gate
    CHECK(notes(false) == std::vector<int>{50, 53, 57});
    CHECK(notes(true) == std::vector<int>{48, 52, 55});
}

TEST_CASE("Sequencer: a note held live before Play goes on sounding in 16 channels, is cut and counted in 3 (D-431)",
          "[engine][seq]") {
    // Latch (Indus, 25/10/2026 19:41): a key held before Play sits on the voice of track 1, which the sequencer
    // reserves once it plays. Its first note took that voice and cut the held note, whose key stayed lit.
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3}) {
        CAPTURE(int(mode));
        Rig r;
        r.g.mode = mode;
        r.global();
        r.pattern(0, 16);
        r.e.noteOn(0, 72, 100);
        r.render(4800);
        const auto held = r.e.voice(0);
        REQUIRE(held.on);
        REQUIRE(held.tag == VoiceTag::Manual);
        REQUIRE(held.note == 72);
        r.e.seqPlay(0);
        r.render(3000);  // into the first step
        CHECK(r.e.voice(0).tag == VoiceTag::Sequencer);
        int live = -1;
        for (int i = 0; i < Engine::kVoices; ++i) {
            const auto v = r.e.voice(i);
            if (v.on && v.tag == VoiceTag::Manual && !v.fading) live = i;
        }
        if (mode == VoiceMode::Studio16) {
            // Moved to a voice of the pool, the same note at the same pitch, not counted as cut.
            REQUIRE(live >= Engine::kLaneVoices + Engine::kChordVoices);
            CHECK(r.e.voice(live).note == 72);
            CHECK(r.e.voice(live).notePeriod == held.notePeriod);
            CHECK(r.e.voice(live).volume == held.volume);
            CHECK(r.e.manualCuts()[72] == 0);
            // It still holds after the sequencer's next steps, and its release lets it go.
            r.render(30000);
            CHECK(r.e.voice(live).on);
            CHECK(r.e.voice(live).tag == VoiceTag::Manual);
            r.e.noteOff(0, 72);
            r.render(48000);
            CHECK_FALSE(r.e.voice(live).on);
        } else {
            // Three channels: the chip channel is the sequencer's; the note is cut and counted for the interface.
            CHECK(live < 0);
            CHECK(r.e.manualCuts()[72] == 1);
        }
        for (int n = 0; n < 128; ++n)
            if (n != 72) CHECK(r.e.manualCuts()[size_t(n)] == 0);
    }
}
