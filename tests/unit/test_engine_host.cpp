// STYM2149 — sequencer of the engine: synchronisation to the host transport (start, position already begun, loop,
// tempo, Song relocated, end without loop, stop), arpeggio on the grid of the host or at its tempo (Arp Sync, D-256),
// Quantize to VBL and metronome.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
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
    double samples;
};

// Engine rendered in blocks; in Host sync, the transport advances at the tempo of the host while it plays.
struct Rig {
    Engine e;
    SeqData data;
    std::vector<Engine::SeqLogRecord> log;
    GlobalParams g;
    Engine::HostTransport t;
    bool barFromSig = false;  // the host gives the start of its bar (signature t.sigNum/t.sigDen from quarter note 0)
    std::vector<float> out;
    uint64_t at = 0;

    explicit Rig(SeqClock clock) {
        EngineConfig cfg;
        cfg.render.hostRate = uint32_t(kRate);
        e.prepare(cfg);
        g.seqClock = clock;
        g.tempo = 90;  // Host sync: ignored, the tempo is that of the host
        e.setGlobal(g);
        TrackParams tp;
        tp.mix.enabled = true;
        e.setTrack(0, tp);
        log.reserve(100000);
        e.setSeqLog(&log);
        t.bpm = 120;  // 1/16: 6000 samples per step
    }
    void pattern(int q, int n) {
        auto l = std::make_shared<SeqLane>();
        for (int s = 0; s < n; ++s) l->pat[size_t(s)] = int16_t(s % 12);
        data.patterns[size_t(q)].steps = n;
        data.patterns[size_t(q)].lanes[0] = l;
        e.setSequence(&data);
    }
    void global() { e.setGlobal(g); }
    void render(uint64_t n, uint32_t block = 480) {
        std::vector<float> l(block), r(block);
        while (n > 0) {
            const uint32_t k = uint32_t(std::min<uint64_t>(n, block));
            if (barFromSig) {
                t.hasBar = true;
                const double bar = t.sigNum * 4.0 / t.sigDen;
                t.barPpq = std::floor(t.ppq / bar) * bar;
            }
            e.setHostTransport(t);
            e.render(l.data(), r.data(), k);
            out.insert(out.end(), l.begin(), l.begin() + k);
            if (t.playing) t.ppq += k * t.bpm / 60 / kRate;
            at += k;
            n -= k;
        }
    }
    std::vector<Step> steps() const {
        std::vector<Step> v;
        for (const auto& r : log)
            if (r.op == '@') v.push_back({r.sample, r.a, int(r.x), r.b, r.y});
        return v;
    }
    int count(char op) const {
        int n = 0;
        for (const auto& r : log) n += r.op == op;
        return n;
    }
};

// Metronome beep expected j samples after its start (decaying sine, see EngineImpl.h).
double clickAt(bool first, int j) {
    const double gain = first ? 0.4 : 0.24;
    const double ramp = std::round(0.045 * kRate);
    const double mul = std::pow(0.0001 / gain, 1.0 / ramp);
    return gain * std::pow(mul, j) * std::sin(2 * 3.141592653589793 * (first ? 1760 : 880) / kRate * j);
}

struct Click {
    size_t at;
    bool first;
};

// Beeps found in the difference between a render with metronome and the same without.
std::vector<Click> clicks(const std::vector<float>& on, const std::vector<float>& off) {
    std::vector<Click> v;
    REQUIRE(on.size() == off.size());
    for (size_t i = 0; i + 8 < on.size(); ++i) {
        if (on[i + 1] == off[i + 1]) continue;
        const double d = double(on[i + 1]) - double(off[i + 1]);
        const bool first = std::fabs(d - clickAt(true, 1)) < 1e-6;
        REQUIRE((first || std::fabs(d - clickAt(false, 1)) < 1e-6));
        for (int j = 2; j < 8; ++j) REQUIRE(std::fabs(double(on[i + size_t(j)]) - off[i + size_t(j)] - clickAt(first, j)) < 1e-6);
        v.push_back({i, first});
        i += size_t(std::round(0.05 * kRate));  // end of the beep
    }
    return v;
}

}  // namespace

TEST_CASE("Host sync: start at 0, one step per sixteenth note at the tempo of the host", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.pattern(0, 16);
    r.render(1000);
    CHECK(r.steps().empty());  // the host is stopped
    CHECK_FALSE(r.e.seqStatus().playing);
    r.log.clear();
    r.at = 0;
    r.e.reset();
    r.t.playing = true;
    r.render(6000 * 4 + 100);
    auto st = r.steps();
    REQUIRE(st.size() == 5);
    for (int k = 0; k < 5; ++k) {
        CHECK(st[size_t(k)].stepAbs == k);
        CHECK(st[size_t(k)].sample == uint64_t(6000 * k));
        CHECK(std::fabs(st[size_t(k)].samples - 6000) < 1e-9);
    }
    CHECK(r.e.seqStatus().playing);
    CHECK(r.e.seqStatus().bpm == 120);
    // Tempo of the host changed: 150 BPM, the next step comes at the new pace.
    r.t.bpm = 150;
    r.render(5000);
    st = r.steps();
    REQUIRE(st.size() == 6);
    const double left = (1.25 - (6000 * 4 + 100) / 24000.0) * 60 / 150 * kRate;
    CHECK(std::fabs(double(st[5].sample) - (6000 * 4 + 100 + left)) <= 1);
    CHECK(std::fabs(st[5].samples - 4800) < 1e-9);
}

TEST_CASE("Host sync: 1/64, sixteen steps per quarter note of the host (D-422)", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.g.resolution = 7;
    r.global();
    r.pattern(0, 64);
    r.t.playing = true;
    r.render(24000 + 100);
    const auto st = r.steps();
    REQUIRE(st.size() == 17);
    for (int k = 0; k < 17; ++k) {
        INFO("step " << k);
        CHECK(st[size_t(k)].stepAbs == k);
        CHECK(st[size_t(k)].sample == uint64_t(1500 * k));
        CHECK(std::fabs(st[size_t(k)].samples - 1500) < 1e-9);
    }
}

TEST_CASE("Host sync: 1/32T, twelve steps per quarter note of the host (D-433)", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.g.resolution = 8;
    r.global();
    r.pattern(0, 64);
    r.t.playing = true;
    r.render(24000 + 100);
    const auto st = r.steps();
    REQUIRE(st.size() == 13);
    for (int k = 0; k < 13; ++k) {
        INFO("step " << k);
        CHECK(st[size_t(k)].stepAbs == k);
        CHECK(st[size_t(k)].sample == uint64_t(2000 * k));
        CHECK(std::fabs(st[size_t(k)].samples - 2000) < 1e-9);
    }
}

TEST_CASE("Host sync: start in the middle of a step, playback resumes at the next step", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.pattern(0, 16);
    r.t.playing = true;
    r.t.ppq = 0.1;
    r.render(10);
    CHECK(r.steps().empty());
    CHECK(r.e.seqStatus().playing);
    CHECK(r.e.seqStatus().stepAbs == 0);
    r.render(6000);
    const auto st = r.steps();
    REQUIRE(st.size() == 1);
    CHECK(st[0].stepAbs == 1);
    CHECK(st[0].step == 1);
    CHECK(std::llabs(int64_t(st[0].sample) - 3600) <= 1);  // quarter note 0.25
}

TEST_CASE("Host sync: loop of the host, back to the first step", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.pattern(0, 16);
    r.t.playing = true;
    r.render(24000);  // one quarter note: steps 0 to 3
    r.t.ppq = 0;      // loop of the host
    r.render(6100);
    const auto st = r.steps();
    REQUIRE(st.size() == 6);
    for (int k = 0; k < 4; ++k) CHECK(st[size_t(k)].stepAbs == k);
    CHECK(st[4].stepAbs == 0);
    CHECK(st[4].step == 0);
    CHECK(st[4].sample == 24000);
    CHECK(st[5].stepAbs == 1);
    CHECK(st[5].sample == 30000);
    CHECK(r.count('!') == 0);
}

TEST_CASE("Host sync: a position reported slightly backwards does not replay the step", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.pattern(0, 16);
    r.t.playing = true;
    r.render(6001, 6001);  // step 1 falls on the last sample of the block
    REQUIRE(r.steps().size() == 2);
    r.t.ppq -= 2.0 / 24000;  // jitter: two samples backwards
    r.render(6000, 6000);
    const auto st = r.steps();
    REQUIRE(st.size() == 2);
    CHECK(st[1].stepAbs == 1);
    CHECK(r.e.seqStatus().stepAbs == 1);
}

TEST_CASE("Host sync: swing on the position of the host", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.g.swing = 66;
    r.global();
    r.pattern(0, 16);
    r.t.playing = true;
    r.render(24000);
    auto st = r.steps();
    REQUIRE(st.size() == 4);
    CHECK(st[0].sample == 0);
    CHECK(std::llabs(int64_t(st[1].sample) - 7920) <= 1);
    CHECK(std::llabs(int64_t(st[2].sample) - 12000) <= 1);
    CHECK(std::llabs(int64_t(st[3].sample) - 19920) <= 1);
    CHECK(std::fabs(st[0].samples - 6000 * 1.32) < 1e-6);
    CHECK(std::fabs(st[1].samples - 6000 * 0.68) < 1e-6);
    // Start on an odd step: it keeps its short duration.
    Rig s(SeqClock::Host);
    s.g.swing = 66;
    s.global();
    s.pattern(0, 16);
    s.t.playing = true;
    s.t.ppq = 0.33;
    s.render(100);
    st = s.steps();
    REQUIRE(st.size() == 1);
    CHECK(st[0].stepAbs == 1);
    CHECK(std::fabs(st[0].samples - 6000 * 0.68) < 1e-6);
}

TEST_CASE("Host sync: Song relocated to the position of the host, at the tempo of the host", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.pattern(0, 4);
    r.pattern(1, 2);
    r.data.song = {{0, 2, 0}, {1, 1, 150}};  // 10 steps; the own tempo of block 2 gives way to that of the host
    r.e.setSequence(&r.data);
    r.g.songMode = true;
    r.global();
    r.t.playing = true;
    r.t.ppq = 9 / 4.0;  // step 9: block 2, second step
    r.render(6000 * 12 - 100);
    const auto st = r.steps();
    const std::vector<int> pats = {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0};
    const std::vector<int> steps = {1, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 0};
    REQUIRE(st.size() == pats.size());
    for (size_t k = 0; k < pats.size(); ++k) {
        INFO("step " << k);
        CHECK(st[k].stepAbs == int64_t(9 + k));
        CHECK(st[k].pattern == pats[k]);
        CHECK(st[k].step == steps[k]);
        CHECK(std::fabs(st[k].samples - 6000) < 1e-9);
    }
    // Jump of the host to the middle of the second repeat of block 1.
    r.log.clear();
    r.t.ppq = 6 / 4.0;
    r.render(100);
    const auto jump = r.steps();
    REQUIRE(jump.size() == 1);
    CHECK(jump[0].pattern == 0);
    CHECK(jump[0].step == 2);
    CHECK(r.e.seqStatus().songPos == 0);
}

TEST_CASE("Host sync: without loop, silence beyond the end until the host comes back", "[engine][seq][host]") {
    for (const bool song : {false, true}) {
        INFO((song ? "Song" : "pattern"));
        Rig r(SeqClock::Host);
        r.pattern(0, 4);
        r.pattern(1, 2);
        if (song) r.data.song = {{1, 1, 0}, {0, 1, 0}};  // 6 steps
        r.e.setSequence(&r.data);
        r.g.songMode = song;
        r.g.loop = false;
        r.global();
        const int len = song ? 6 : 4;
        r.t.playing = true;
        r.render(6000 * 10);
        auto st = r.steps();
        REQUIRE(st.size() == size_t(len));
        for (int k = 0; k < len; ++k) CHECK(st[size_t(k)].stepAbs == k);
        CHECK(r.count('!') == 1);
        CHECK_FALSE(r.e.seqStatus().playing);
        // The host goes back to the start: playback resumes.
        r.t.ppq = 0;
        r.render(100);
        st = r.steps();
        REQUIRE(st.size() == size_t(len + 1));
        CHECK(st.back().stepAbs == 0);
        CHECK(st.back().pattern == (song ? 1 : 0));
        // Start of the host beyond the end: nothing plays.
        r.t.ppq = 3;
        r.render(6000 * 3);
        CHECK(r.steps().size() == size_t(len + 1));
        CHECK_FALSE(r.e.seqStatus().playing);
    }
}

TEST_CASE("Host sync: stop and resume of the host; Play, Start and MIDI clock without effect", "[engine][seq][host]") {
    Rig r(SeqClock::Host);
    r.pattern(0, 16);
    r.e.seqPlay(0);
    r.e.midiStart(0);
    for (int k = 0; k < 24; ++k) {
        r.e.midiClock(0);
        r.render(1000);
    }
    CHECK(r.steps().empty());
    CHECK_FALSE(r.e.seqStatus().playing);
    r.t.playing = true;
    r.render(6000 * 2 + 100);
    CHECK(r.steps().size() == 3);
    r.e.seqStop(0);  // no effect: the host transport decides
    r.render(100);
    CHECK(r.e.seqStatus().playing);
    r.t.playing = false;
    r.render(100);
    CHECK_FALSE(r.e.seqStatus().playing);
    CHECK(r.count('!') == 1);
    // Resume where the host had stopped, in the middle of step 2: step 3 comes at its start.
    r.t.playing = true;
    r.render(6000);
    const auto st = r.steps();
    REQUIRE(st.size() == 4);
    CHECK(st[3].stepAbs == 3);
    CHECK(std::llabs(int64_t(st[3].sample) - int64_t(r.at - 6000 + (18000 - (6000 * 2 + 200)))) <= 1);
}

TEST_CASE("Sequencer: clock changed during playback, stop then new clock", "[engine][seq][host]") {
    Rig r(SeqClock::Internal);
    r.pattern(0, 16);
    r.e.seqPlay(0);
    r.render(6000 * 2);
    CHECK(r.e.seqStatus().playing);
    r.g.seqClock = SeqClock::Host;
    r.global();
    r.render(100);
    CHECK_FALSE(r.e.seqStatus().playing);
    CHECK(r.count('!') == 1);
    const size_t before = r.steps().size();
    r.t.playing = true;
    r.render(100);
    const auto st = r.steps();
    REQUIRE(st.size() == before + 1);
    CHECK(st.back().stepAbs == 0);
}

TEST_CASE("Host sync: arpeggio on the clock, notes on the grid of the host", "[engine][seq][host][arp]") {
    for (const double start : {0.0, 0.1}) {
        INFO("start at quarter note " << start);
        Rig r(SeqClock::Host);
        r.g.arpClock = ArpClock::MidiClock;
        r.g.arpDivision = 12;  // sixteenth note (half pulses, D-289)
        r.global();
        TrackParams tp;
        tp.mix.enabled = true;
        tp.arp.on = true;
        tp.arp.mode = ArpMode::Up;
        r.e.setTrack(0, tp);
        r.e.noteOn(0, 60, 100);
        r.e.noteOn(0, 64, 100);
        r.render(2000);
        auto arpNote = [&] {
            for (int i = 0; i < Engine::kVoices; ++i) {
                const auto v = r.e.voice(i);
                if (v.on && !v.fading && v.tag == VoiceTag::Arp) return v.note;
            }
            return -1;
        };
        CHECK(arpNote() == -1);  // host stopped: no pulse
        r.t.playing = true;
        r.t.ppq = start;
        // Quarter notes 0.25, 0.5 and 0.75: notes 64, 60, 64; with a start at 0, the first one (60) falls at once.
        const double first = (0.25 - start) * 24000;
        r.render(uint64_t(first) - 10);
        CHECK(arpNote() == (start == 0 ? 60 : -1));
        r.render(20);
        const int a = arpNote();
        r.render(6000);
        const int b = arpNote();
        r.render(6000);
        const int c = arpNote();
        if (start == 0) {
            CHECK(a == 64);
            CHECK(b == 60);
            CHECK(c == 64);
        } else {
            CHECK(a == 60);
            CHECK(b == 64);
            CHECK(c == 60);
        }
    }
}

TEST_CASE("Arp Sync: one step per division of the tempo, on the grid of the host while it plays (D-256)",
          "[engine][seq][host][arp]") {
    // Note played by the keyboard arpeggio, and the samples between its changes.
    auto arpNote = [](Engine& e) {
        for (int i = 0; i < Engine::kVoices; ++i) {
            const auto v = e.voice(i);
            if (v.on && !v.fading && v.tag == VoiceTag::Arp) return v.note;
        }
        return -1;
    };
    auto intervals = [&](auto& r, uint64_t n) {
        std::vector<uint64_t> changes;
        int last = arpNote(r.e);
        for (uint64_t k = 0; k < n; k += 10) {
            r.render(10, 10);
            const int now = arpNote(r.e);
            if (now != last) changes.push_back(r.at);
            last = now;
        }
        std::vector<int64_t> d;
        for (size_t i = 1; i < changes.size(); ++i) d.push_back(int64_t(changes[i] - changes[i - 1]));
        return d;
    };
    TrackParams tp;
    tp.mix.enabled = true;
    tp.arp.on = true;
    tp.arp.mode = ArpMode::Up;
    tp.arp.rateHz = 50;  // Rate: ignored with the tempo
    SECTION("Free run: the sequencer's tempo") {
        Rig r(SeqClock::Internal);  // 90 BPM
        r.g.arpClock = ArpClock::Tempo;
        r.g.arpDivision = 12;  // sixteenth note: 90 × 48 / 60 / 12 = 6 steps per second, 8000 samples
        r.global();
        r.e.setTrack(0, tp);
        r.e.noteOn(0, 60, 100);
        r.e.noteOn(0, 64, 100);
        const auto d = intervals(r, 48000);
        REQUIRE(d.size() >= 4);
        for (const int64_t x : d) CHECK(std::abs(x - 8000) <= 10);
    }
    SECTION("Free run: 1/64, three half pulses (D-289)") {
        Rig r(SeqClock::Internal);  // 90 BPM
        r.g.arpClock = ArpClock::Tempo;
        r.g.arpDivision = 3;  // 90 × 48 / 60 / 3 = 24 steps per second, 2000 samples
        r.global();
        r.e.setTrack(0, tp);
        r.e.noteOn(0, 60, 100);
        r.e.noteOn(0, 64, 100);
        const auto d = intervals(r, 24000);
        REQUIRE(d.size() >= 8);
        for (const int64_t x : d) CHECK(std::abs(x - 2000) <= 10);
    }
    SECTION("Host sync: the host's tempo when it is stopped, its grid while it plays") {
        Rig r(SeqClock::Host);  // 120 BPM
        r.g.arpClock = ArpClock::Tempo;
        r.g.arpDivision = 24;  // eighth note: 120 × 48 / 60 / 24 = 4 steps per second, 12000 samples
        r.global();
        r.e.setTrack(0, tp);
        r.e.noteOn(0, 60, 100);
        r.e.noteOn(0, 64, 100);
        const auto d = intervals(r, 48000);
        REQUIRE(d.size() >= 2);
        for (const int64_t x : d) CHECK(std::abs(x - 12000) <= 10);
        // Playback from quarter note 0.1: the steps fall on the eighth notes of the host (0.5, 1, 1.5), the
        // arpeggio starting again from its first note.
        r.t.playing = true;
        r.t.ppq = 0.1;
        r.render(uint64_t((0.5 - 0.1) * 24000) + 10);
        CHECK(arpNote(r.e) == 60);
        r.render(12000);
        CHECK(arpNote(r.e) == 64);
        r.render(12000);
        CHECK(arpNote(r.e) == 60);
        const auto grid = intervals(r, 36000);
        REQUIRE(grid.size() >= 2);
        for (const int64_t x : grid) CHECK(std::abs(x - 12000) <= 10);
        // 1/64 on the host's grid: three half pulses, 1500 samples at 120 BPM (D-289).
        r.g.arpDivision = 3;
        r.global();
        const auto fine = intervals(r, 12000);
        REQUIRE(fine.size() >= 6);
        for (const int64_t x : fine) CHECK(std::abs(x - 1500) <= 10);
    }
}

TEST_CASE("Quantize to VBL: sequencer and events on frame boundaries", "[engine][seq][vbl]") {
    Rig r(SeqClock::Internal);
    r.g.tempo = 120;
    r.g.quantizeVbl = true;  // 50 Hz at 48 kHz: one frame every 960 samples
    r.global();
    r.pattern(0, 16);
    r.e.seqPlay(0);
    r.render(6000 * 4 + 100, 512);
    const auto st = r.steps();
    REQUIRE(st.size() == 5);
    // Steps of the internal clock (6000 k − 1) moved to the next frame, without drift.
    for (int k = 0; k < 5; ++k) {
        const uint64_t nominal = k ? uint64_t(6000 * k - 1) : 0;
        CHECK(st[size_t(k)].sample == (nominal + 959) / 960 * 960);
    }
    // All the operations of the sequencer (notes, releases) fall on a frame boundary.
    for (const auto& rec : r.log) CHECK(rec.sample % 960 == 0);
    CHECK(r.count('R') + r.count('K') > 0);
}

TEST_CASE("Quantize to VBL: an event after the last frame of the block waits for the next block", "[engine][vbl]") {
    for (const bool quant : {false, true}) {
        INFO("quantize " << quant);
        Rig r(SeqClock::Internal);
        r.g.quantizeVbl = quant;
        r.global();
        r.pattern(0, 16);
        r.render(1024, 512);
        r.e.seqPlay(100);  // sample 1 124; frames at 960 and 1 920
        r.e.noteOn(100, 60, 100);
        r.render(512, 512);
        CHECK((r.e.activeVoices() > 0) == !quant);
        r.render(512, 512);
        const auto st = r.steps();
        REQUIRE(!st.empty());
        CHECK(st[0].sample == (quant ? 1920u : 1124u));
        CHECK(r.e.activeVoices() > 0);
    }
}

TEST_CASE("Metronome: one beep per beat, higher on the first beat", "[engine][seq][metronome]") {
    SECTION("Free run: beats counted from the start of the pattern") {
        auto run = [](bool metro) {
            Rig r(SeqClock::Internal);
            r.g.tempo = 120;
            r.g.metronome = metro;
            r.g.sigNum = 3;
            r.g.sigDen = 4;
            r.global();
            r.data.patterns[0].steps = 16;  // empty pattern
            r.e.setSequence(&r.data);
            r.e.seqPlay(0);
            r.render(6000 * 20);
            return r.out;
        };
        const auto c = clicks(run(true), run(false));
        // Steps 0, 4, 8, 12, then 0 (16) and 4 (20): first beats at steps 0 and 12 (bar of 3 beats), and at the
        // restart.
        REQUIRE(c.size() == 5);
        const std::vector<bool> first = {true, false, false, true, true};
        for (size_t k = 0; k < c.size(); ++k) {
            INFO("beep " << k);
            CHECK(c[k].first == first[k]);
            CHECK(std::llabs(int64_t(c[k].at) - int64_t(k ? 24000 * k - 1 : 0)) <= 1);
        }
    }
    SECTION("Host sync: bars of the host") {
        auto run = [](bool metro) {
            Rig r(SeqClock::Host);
            r.g.metronome = metro;
            r.global();
            r.data.patterns[0].steps = 16;
            r.e.setSequence(&r.data);
            r.t.sigNum = 3;
            r.t.sigDen = 4;
            r.barFromSig = true;
            r.t.playing = true;
            r.t.ppq = 1;  // second beat of the first bar
            r.render(24000 * 4 + 100);
            return r.out;
        };
        const auto c = clicks(run(true), run(false));
        // Quarter notes 1, 2, 3 (first of bar 2), 4, 5.
        REQUIRE(c.size() == 5);
        const std::vector<bool> first = {false, false, true, false, false};
        for (size_t k = 0; k < c.size(); ++k) {
            INFO("beep " << k);
            CHECK(c[k].first == first[k]);
            CHECK(c[k].at == 24000 * k);
        }
    }
}

TEST_CASE("Metronome: its level scales the beeps, 0 mutes them (D-288)", "[engine][seq][metronome]") {
    auto run = [](bool metro, uint8_t level) {
        Rig r(SeqClock::Internal);
        r.g.tempo = 120;
        r.g.metronome = metro;
        r.g.metronomeLevel = level;
        r.global();
        r.data.patterns[0].steps = 16;  // empty pattern
        r.e.setSequence(&r.data);
        r.e.seqPlay(0);
        r.render(24000);
        return r.out;
    };
    const auto off = run(false, 100), full = run(true, 100), half = run(true, 50), mute = run(true, 0);
    REQUIRE(clicks(full, off).size() == 1);
    double peak = 0;
    for (size_t i = 0; i < off.size(); ++i) {
        const double d = double(full[i]) - off[i];
        peak = std::max(peak, std::fabs(d));
        CHECK(std::fabs(double(half[i]) - off[i] - d / 2) < 1e-6);
        CHECK(mute[i] == off[i]);
    }
    CHECK(peak > 0.3);
}
