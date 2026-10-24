// STYM2149 — effect commands of the CMD row of the sequencer (M6, B4, D-150): Sxx (ticks per row), Rxx (envelope
// retrigger at tick xx), Zxx (silent), Txx (tempo, D-243), Vxx (vibrato, D-254); one of each kind on a row (D-330);
// the effects of D-338: Axy, Bxy and Nxy slides, Dxx note delay, Cxx note cut, Exx envelope shape, Oxx DMA sample
// offset, Fxx fine tune. Xyz and Pxy are retired (D-340, D-344). Twelfth DAW test: Lxx, Mxx and Jxx switches (items 142
// to 144); the tempo in the BPM row of the pattern (item 150).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

constexpr uint32_t kTick = 960;  // one 50 Hz replay tick at 48 kHz

struct Rig {
    Engine e;
    EngineConfig cfg;
    SeqData data;
    std::vector<Engine::SeqLogRecord> log;
    std::vector<Engine::WriteRecord> writes;
    GlobalParams g;

    explicit Rig(VoiceMode mode = VoiceMode::Studio16, int tracks = 1) {
        cfg.render.hostRate = 48000;
        e.prepare(cfg);
        g.mode = mode;
        g.tempo = 120;
        g.timing = SeqTiming::Ticks;
        g.vblHz = 50;
        g.speed = 6;
        e.setGlobal(g);
        for (int t = 0; t < tracks; ++t) {
            TrackParams p;
            p.mix.enabled = true;
            e.setTrack(t, p);
        }
        log.reserve(100000);
        e.setSeqLog(&log);
        writes.reserve(100000);
        e.setWriteLog(&writes);
        data.patterns[0].steps = 16;
    }
    SeqLane& lane(int t) {
        auto& p = data.patterns[0].lanes[size_t(t)];
        if (!p) p = std::make_shared<SeqLane>();
        return const_cast<SeqLane&>(*p);
    }
    void start() {
        e.setGlobal(g);
        e.setSequence(&data);
        e.seqPlay(0);
    }
    void render(uint64_t n) {
        static std::vector<float> l(kTick), r(kTick);
        while (n > 0) {
            const uint32_t k = uint32_t(std::min<uint64_t>(n, l.size()));
            e.render(l.data(), r.data(), k);
            n -= k;
        }
    }
    std::vector<double> lineLengths() const {
        std::vector<double> out;
        for (const auto& x : log)
            if (x.op == '@') out.push_back(x.y);
        return out;
    }
    // Samples of the R13 rewrites (3-channel).
    std::vector<double> r13() const {
        std::vector<double> out;
        const double perSample = cfg.render.chip.clock.hz() / 48000;
        for (const auto& w : writes)
            if (w.reg == 13) out.push_back(double(w.cycle) / perSample);
        return out;
    }
};

}  // namespace

TEST_CASE("Commands: Sxx changes the ticks per row in Ticks cadence; S00 has no effect", "[engine][seq][commands]") {
    Rig r;
    r.lane(0).setCmd(2, seqCmd(SeqCmd::Speed, 3));
    r.lane(0).setCmd(4, seqCmd(SeqCmd::Speed, 0));
    r.lane(0).setCmd(5, seqCmd(SeqCmd::Speed, 0x40));  // above 31: 31
    r.start();
    r.render(48000 * 3);
    const auto ln = r.lineLengths();
    REQUIRE(ln.size() >= 7);
    const double want[] = {6, 6, 3, 3, 3, 31, 31};
    for (size_t k = 0; k < 7; ++k) {
        CAPTURE(k);
        CHECK(std::fabs(ln[k] - kTick * want[k]) < 1e-6);
    }
    // The next start begins again from the speed of the song.
    r.e.seqStop(0);
    r.log.clear();
    r.e.seqPlay(0);
    r.render(kTick * 7);
    REQUIRE(r.lineLengths().size() >= 1);
    CHECK(std::fabs(r.lineLengths()[0] - kTick * 6) < 1e-6);
}

TEST_CASE("Commands: Sxx, the last track wins; ignored in BPM cadence", "[engine][seq][commands]") {
    Rig r(VoiceMode::Studio16, 2);
    r.lane(0).setCmd(1, seqCmd(SeqCmd::Speed, 4));
    r.lane(1).setCmd(1, seqCmd(SeqCmd::Speed, 8));
    r.start();
    r.render(kTick * 30);
    auto ln = r.lineLengths();
    REQUIRE(ln.size() >= 3);
    CHECK(std::fabs(ln[1] - kTick * 8) < 1e-6);
    CHECK(std::fabs(ln[2] - kTick * 8) < 1e-6);

    Rig b(VoiceMode::Studio16, 2);
    b.g.timing = SeqTiming::Bpm;
    b.lane(0).setCmd(1, seqCmd(SeqCmd::Speed, 4));
    b.start();
    b.render(48000);
    ln = b.lineLengths();
    REQUIRE(ln.size() >= 3);
    for (const double d : ln) CHECK(std::fabs(d - 6000) < 1e-6);  // sixteenth notes at 120 BPM
    CHECK(std::none_of(b.log.begin(), b.log.end(), [](const auto& x) { return x.op == 'V'; }));
}

TEST_CASE("Commands: the BPM row sets the tempo from its step on in BPM cadence; below 32, no effect (D-243, item 150)",
          "[engine][seq][commands]") {
    Rig r;
    r.g.timing = SeqTiming::Bpm;
    r.data.patterns[0].bpm[2] = 0x3C;  // 60 BPM
    r.data.patterns[0].bpm[4] = 0x10;  // below $20: no effect
    r.start();
    r.render(6000 * 2 + 12000 * 16);
    const auto ln = r.lineLengths();
    REQUIRE(ln.size() >= 17);
    // Sixteenth notes: 6000 samples at 120 BPM, 12000 at 60 BPM; the tempo holds when the pattern loops.
    for (size_t k = 0; k < 17; ++k) {
        CAPTURE(k);
        CHECK(std::fabs(ln[k] - (k < 2 ? 6000 : 12000)) < 1e-6);
    }
    // Nominal tempo of the rows, for the MIDI export.
    std::vector<double> bpm;
    for (const auto& x : r.log)
        if (x.op == '@') bpm.push_back(x.bpm);
    CHECK(bpm[1] == 120);
    CHECK(bpm[2] == 60);
    CHECK(r.e.seqStatus().bpm == 60);
    // Play starts again from the tempo of the song.
    r.e.seqStop(0);
    r.log.clear();
    r.e.seqPlay(0);
    r.render(6000 * 2);
    REQUIRE(r.lineLengths().size() >= 1);
    CHECK(std::fabs(r.lineLengths()[0] - 6000) < 1e-6);
}

TEST_CASE("Commands: BPM row; a change of the song's tempo takes over; ignored in Ticks cadence; Txx of a track "
          "ignored (item 150)",
          "[engine][seq][commands]") {
    Rig r(VoiceMode::Studio16, 2);
    r.g.timing = SeqTiming::Bpm;
    r.lane(0).setCmd(1, seqCmd(SeqCmd::Tempo, 0x3C));  // a track no longer holds the tempo
    r.data.patterns[0].bpm[1] = 0xF0;                  // 240 BPM
    r.start();
    r.render(6000 + 3000 * 2 + 100);
    auto ln = r.lineLengths();
    REQUIRE(ln.size() >= 3);
    CHECK(std::fabs(ln[1] - 3000) < 1e-6);
    CHECK(std::fabs(ln[2] - 3000) < 1e-6);
    r.g.tempo = 90;  // the song's tempo moved: 8000 samples per sixteenth note from the next row
    r.e.setGlobal(r.g);
    r.log.clear();
    r.render(8000 * 3);
    ln = r.lineLengths();
    REQUIRE(ln.size() >= 2);
    CHECK(std::fabs(ln[0] - 8000) < 1e-6);
    CHECK(std::fabs(ln[1] - 8000) < 1e-6);

    Rig t;  // Ticks: Sxx and the speed set the cadence
    t.data.patterns[0].bpm[1] = 0x3C;
    t.start();
    t.render(kTick * 6 * 4);
    ln = t.lineLengths();
    REQUIRE(ln.size() >= 3);
    for (const double d : ln) CHECK(std::fabs(d - kTick * 6) < 1e-6);
}

TEST_CASE("Commands: BPM row in a Song; an entry with its own tempo starts again at it on each pass (D-243)",
          "[engine][seq][commands]") {
    Rig r;
    r.g.timing = SeqTiming::Bpm;
    r.g.songMode = true;
    r.data.patterns[0].steps = 4;
    r.data.patterns[1].steps = 4;
    r.data.patterns[0].bpm[2] = 0x3C;  // 60 BPM from row 2 of pattern 1
    r.data.song = {{0, 2, 150, SeqSongKind::Pattern, 0}, {1, 1, 0, SeqSongKind::Pattern, 0}};
    r.start();
    r.render(4800 * 4 + 12000 * 8 + 100);
    const auto ln = r.lineLengths();
    REQUIRE(ln.size() >= 13);
    // Entry 1 at 150 BPM (4800 samples), twice; entry 2 without a tempo keeps that of the BPM row; then entry 1 again.
    const double want[] = {4800, 4800, 12000, 12000, 4800, 4800, 12000, 12000, 12000, 12000, 12000, 12000, 4800};
    for (size_t k = 0; k < 13; ++k) {
        CAPTURE(k);
        CHECK(std::fabs(ln[k] - want[k]) < 1e-6);
    }
}

TEST_CASE("Commands: Xyz and Pxy are retired, a lane that still holds them plays its note unchanged (D-340, D-344)",
          "[engine][seq][commands]") {
    Rig r;
    SeqLane& l = r.lane(0);
    l.pat[0] = 0;
    l.tie[0] = l.tie[1] = l.tie[2] = 1;  // note held for three rows
    l.setCmd(0, seqCmd(SeqCmd::Arp, 0x47));  // dropped
    CHECK(l.cmdAt(SeqCmd::Arp, 0) == 0);
    // Written past setCmd (a lane of an older build): the engine ignores them.
    l.cmd[size_t(int(SeqCmd::Arp) - 1)][0] = seqCmd(SeqCmd::Arp, 0x47);
    l.cmd[size_t(int(SeqCmd::PitchSlide) - 1)][1] = seqCmd(SeqCmd::PitchSlide, 0xF0);
    r.start();
    r.render(kTick / 2);
    const uint16_t tp = r.e.voice(0).tonePeriod;
    for (int k = 0; k < 18; ++k) {
        CAPTURE(k);
        REQUIRE(r.e.voice(0).on);
        CHECK(r.e.voice(0).tonePeriod == tp);
        r.render(kTick);
    }
    CHECK(std::count_if(r.log.begin(), r.log.end(), [](const auto& x) { return x.op == 'X'; }) == 0);
}

TEST_CASE("Commands: several on a row play together, Axy and Sxx (tenth DAW test, item 122, D-330)",
          "[engine][seq][commands]") {
    Rig r;
    SeqLane& l = r.lane(0);
    l.pat[0] = 0;
    l.tie[0] = l.tie[1] = 1;
    l.setCmd(0, seqCmd(SeqCmd::VolSlide, 0x04));  // down one 4-bit step per tick
    l.setCmd(0, seqCmd(SeqCmd::Speed, 3));
    l.setCmd(0, seqCmd(SeqCmd::Sync, 0x12));  // silent
    r.start();
    r.render(kTick / 2);
    const double v0 = r.e.voice(0).volume;
    REQUIRE(v0 >= 6);
    // Row 0 of 3 ticks, then row 1 (held), 3 ticks too: the slide goes on.
    for (int k = 0; k < 6; ++k) {
        CAPTURE(k);
        REQUIRE(r.e.voice(0).on);
        CHECK(r.e.voice(0).volume == v0 - k);
        r.render(kTick);
    }
    const auto ln = r.lineLengths();
    REQUIRE(ln.size() >= 2);
    CHECK(std::fabs(ln[0] - kTick * 3) < 1e-6);
    CHECK(std::fabs(ln[1] - kTick * 3) < 1e-6);
    CHECK(std::count_if(r.log.begin(), r.log.end(), [](const auto& x) { return x.op == 'V'; }) == 1);
}

TEST_CASE("Commands: Rxx rewrites R13 at tick xx of the row, R00 at once, ignored beyond the row",
          "[engine][seq][commands]") {
    Rig r(VoiceMode::Authentic3);
    TrackParams p;
    p.mix.enabled = true;
    p.patch.envelopeVolume = true;
    r.e.setTrack(0, p);
    SeqLane& l = r.lane(0);
    l.pat[0] = 0;
    l.tie[0] = l.tie[1] = l.tie[2] = 1;
    l.setCmd(0, seqCmd(SeqCmd::Retrig, 3));
    l.setCmd(1, seqCmd(SeqCmd::Retrig, 0));
    l.setCmd(2, seqCmd(SeqCmd::Retrig, 6));  // row of 6 ticks: tick 6 is that of the next row
    l.setCmd(3, seqCmd(SeqCmd::Sync, 0x12));  // silent
    r.start();
    r.render(kTick * 6 * 4);
    const auto w = r.r13();
    REQUIRE(w.size() == 3);  // attack, R03, R00
    CHECK(w[0] < 2);
    CHECK(std::fabs(w[1] - kTick * 3) < 2);
    CHECK(std::fabs(w[2] - kTick * 6) < 2);
    CHECK(std::count_if(r.log.begin(), r.log.end(), [](const auto& x) { return x.op == 'E'; }) == 2);
}

namespace {
// Tone period of voice 0 at the middle of each of `ticks` replay ticks, from the start of the sequencer.
std::vector<uint16_t> periodsPerTick(Rig& r, int ticks) {
    std::vector<uint16_t> out;
    r.render(kTick / 2);
    for (int k = 0; k < ticks; ++k) {
        out.push_back(r.e.voice(0).on ? r.e.voice(0).tonePeriod : 0);
        r.render(kTick);
    }
    return out;
}
TrackParams vibrato(bool on) {
    TrackParams p;
    p.mix.enabled = true;
    p.patch.vibratoCents = 100;
    p.patch.vibratoHz = 5;
    p.patch.vibratoOn = on;
    return p;
}
// Distinct values of a row (6 ticks) of the periods.
size_t distinct(const std::vector<uint16_t>& v, int row) {
    std::vector<uint16_t> r(v.begin() + row * 6, v.begin() + row * 6 + 6);
    std::sort(r.begin(), r.end());
    return size_t(std::unique(r.begin(), r.end()) - r.begin());
}
}  // namespace

TEST_CASE("Commands: V00 holds the track's vibrato from its row on, Vxx brings it back, Play resets (D-254)",
          "[engine][seq][commands]") {
    Rig plain;
    plain.e.setTrack(0, vibrato(false));  // Vibrato Off: the note's own period
    plain.lane(0).pat[0] = 0;
    for (int k = 0; k < 5; ++k) plain.lane(0).tie[size_t(k)] = 1;  // held beyond the four rows checked
    plain.start();
    const auto flat = periodsPerTick(plain, 24);
    REQUIRE(flat[0] > 0);
    CHECK(std::all_of(flat.begin(), flat.end(), [&](uint16_t tp) { return tp == flat[0]; }));

    Rig r;
    r.e.setTrack(0, vibrato(true));
    SeqLane& l = r.lane(0);
    l.pat[0] = 0;
    for (int k = 0; k < 5; ++k) l.tie[size_t(k)] = 1;
    l.setCmd(1, seqCmd(SeqCmd::Vibrato, 0));
    l.setCmd(3, seqCmd(SeqCmd::Vibrato, 0x05));
    r.start();
    const auto tp = periodsPerTick(r, 24);
    CHECK(distinct(tp, 0) > 1);  // vibrato
    for (int k = 6; k < 18; ++k) {
        CAPTURE(k);
        CHECK(tp[size_t(k)] == flat[0]);  // V00: rows 1 and 2 at the note's period
    }
    CHECK(distinct(tp, 3) > 1);  // V05: vibrato again

    // Stopped after a V00: Play starts with the track's vibrato.
    r.e.seqStop(0);
    r.render(kTick);
    l.clearCmd(SeqCmd::Vibrato, 3);
    r.e.seqPlay(0);
    r.render(kTick * 6 * 2);  // rows 0 and 1 (V00)
    r.e.seqStop(0);
    r.render(kTick);
    r.e.seqPlay(0);
    CHECK(distinct(periodsPerTick(r, 6), 0) > 1);
}

// Twelfth DAW test, items 142 to 144: LFO, MOD and ARP rows, switches like VIB.
TEST_CASE("Commands: L00 and M00 hold the track's LFO and modulation table from their row on, L01 and M01 bring "
          "them back (items 142, 143)",
          "[engine][seq][commands]") {
    auto held = [](Rig& r) {
        SeqLane& l = r.lane(0);
        l.pat[0] = 0;
        for (int k = 0; k < 5; ++k) l.tie[size_t(k)] = 1;
        return std::ref(l);
    };
    SECTION("LFO on the pitch") {
        TrackParams p;
        p.mix.enabled = true;
        p.patch.lfo.on = true;
        p.patch.lfo.target = LfoTarget::Pitch;
        p.patch.lfo.depth = 100;
        p.patch.lfo.hz = 5;
        Rig r;
        r.e.setTrack(0, p);
        SeqLane& l = held(r);
        l.setCmd(1, seqCmd(SeqCmd::Lfo, 0));
        l.setCmd(3, seqCmd(SeqCmd::Lfo, 1));
        r.start();
        const auto tp = periodsPerTick(r, 24);
        CHECK(distinct(tp, 0) > 1);
        CHECK(distinct(tp, 1) == 1);  // L00: rows 1 and 2 at one period
        CHECK(distinct(tp, 2) == 1);
        CHECK(distinct(tp, 3) > 1);  // L01: the LFO again
    }
    SECTION("Modulation table") {
        TrackParams p;
        p.mix.enabled = true;
        p.patch.ornamentOn = true;
        p.patch.ornamentLength = 2;
        p.patch.ornamentPitch[1] = 12;
        Rig r;
        r.e.setTrack(0, p);
        SeqLane& l = held(r);
        l.setCmd(1, seqCmd(SeqCmd::Mod, 0));
        l.setCmd(3, seqCmd(SeqCmd::Mod, 1));
        r.start();
        const auto tp = periodsPerTick(r, 24);
        CHECK(distinct(tp, 0) > 1);
        CHECK(distinct(tp, 1) == 1);  // M00: the note alone
        CHECK(distinct(tp, 2) == 1);
        CHECK(distinct(tp, 3) > 1);  // M01: the table again
    }
}

TEST_CASE("Commands: each pattern starts with the track's settings, the switches before the step taking over "
          "(item 144)",
          "[engine][seq][commands]") {
    Rig r;
    r.e.setTrack(0, vibrato(true));
    r.g.songMode = true;
    r.data.patterns[0].steps = 2;
    r.data.patterns[1].steps = 2;
    SeqLane& a = r.lane(0);
    a.pat[0] = 0;
    a.tie[0] = a.tie[1] = 1;
    a.setCmd(0, seqCmd(SeqCmd::Vibrato, 0));  // V00 in pattern 1 only
    auto b = std::make_shared<SeqLane>(a);
    b->clearCmd(SeqCmd::Vibrato, 0);
    r.data.patterns[1].lanes[0] = b;
    r.data.song = {{0, 1, 0, SeqSongKind::Pattern, 0}, {1, 1, 0, SeqSongKind::Pattern, 0}};
    r.start();
    const auto tp = periodsPerTick(r, 24);
    CHECK(distinct(tp, 0) == 1);  // V00
    CHECK(distinct(tp, 1) == 1);
    CHECK(distinct(tp, 2) > 1);  // pattern 2 starts with the track's vibrato
    CHECK(distinct(tp, 3) > 1);
}

namespace {
// Per replay tick, at its middle, from the start of the sequencer: a value read from voice 0.
template <class F> std::vector<double> perTick(Rig& r, int ticks, F&& f) {
    std::vector<double> out;
    r.render(kTick / 2);
    for (int k = 0; k < ticks; ++k) {
        out.push_back(double(f(r.e.voice(0))));
        r.render(kTick);
    }
    return out;
}
// Period moved by a pitch in semitones (the rounding of the engine may differ by one).
double periodAt(double tp, double semis) { return tp * std::pow(2.0, -semis / 12.0); }
// Rank of the most recent attack of the sequencer.
uint64_t lastAttack(const Engine& e) {
    uint64_t a = 0;
    for (int i = 0; i < Engine::kVoices; ++i)
        if (const Engine::VoiceInfo v = e.voice(i); v.tag == VoiceTag::Sequencer) a = std::max(a, v.age);
    return a;
}
}  // namespace

TEST_CASE("Commands: Axy slides the 4-bit volume per tick until A00; a new note starts from its own volume (D-338)",
          "[engine][seq][commands]") {
    Rig r;
    SeqLane& l = r.lane(0);
    l.pat[0] = l.pat[2] = 0;
    l.tie[0] = l.tie[1] = 1;  // rows 0 and 1: one note
    l.setCmd(0, seqCmd(SeqCmd::VolSlide, 0x04));  // down 4 × 1/4 step per tick
    l.setCmd(2, seqCmd(SeqCmd::VolSlide, 0x00));  // stops
    r.start();
    const auto v = perTick(r, 18, [](const Engine::VoiceInfo& x) { return x.volume; });
    const double v0 = v[0];
    REQUIRE(v0 >= 12);
    for (size_t k = 0; k < 12; ++k) {  // from tick 1 (none at the attack), row 1 included: the speed holds
        CAPTURE(k);
        CHECK(v[k] == std::max(0.0, v0 - double(k)));
    }
    for (size_t k = 12; k < 18; ++k) CHECK(v[k] == v0);  // new note, A00

    Rig u;  // up: bounded at 15
    u.lane(0).pat[0] = 0;
    u.lane(0).setCmd(0, seqCmd(SeqCmd::VolSlide, 0x40));
    u.start();
    for (const double x : perTick(u, 6, [](const Engine::VoiceInfo& y) { return y.volume; })) CHECK(x <= 15);
}

TEST_CASE("Commands: Nxy slides the noise period, Bxy the envelope period (D-338)", "[engine][seq][commands]") {
    Rig r;
    TrackParams p;
    p.mix.enabled = true;
    p.patch.noiseOn = true;
    p.patch.noisePeriod = 8;
    p.patch.envelopeVolume = true;
    p.patch.envPeriod = 1000;
    r.e.setTrack(0, p);
    SeqLane& l = r.lane(0);
    l.pat[0] = 0;
    l.tie[0] = l.tie[1] = 1;
    l.setCmd(0, seqCmd(SeqCmd::NoiseSlide, 0x40));  // up: one period step shorter per tick
    l.setCmd(0, seqCmd(SeqCmd::EnvSlide, 0x0C));    // down 12/16 semitone per tick: longer period
    l.setCmd(1, seqCmd(SeqCmd::NoiseSlide, 0x02));  // down 1/2 step per tick
    r.start();
    std::vector<double> ep;
    const auto np = perTick(r, 12, [&](const Engine::VoiceInfo& x) {
        ep.push_back(x.envPeriod);
        return x.noisePeriod;
    });
    for (size_t k = 0; k < 6; ++k) {
        CAPTURE(k);
        CHECK(np[k] == std::max(1.0, 8.0 - double(k)));
        CHECK(std::fabs(ep[k] - 1000 * std::pow(2.0, double(k) * 0.75 / 12)) <= 1);
    }
    // Row 1, from its first tick: down half a step per tick (rounded to the nearest, halves upwards).
    for (size_t k = 6; k < 12; ++k) {
        CAPTURE(k);
        CHECK(np[k] == 8 - std::floor(5 - 0.5 * double(k - 5) + 0.5));
    }
}

TEST_CASE("Commands: Dxx delays the notes of the row to tick xx; beyond the row, ignored (D-338)",
          "[engine][seq][commands]") {
    Rig r;
    SeqLane& l = r.lane(0);
    l.pat[0] = l.pat[1] = l.pat[2] = 0;
    l.setCmd(1, seqCmd(SeqCmd::Delay, 3));
    l.setCmd(2, seqCmd(SeqCmd::Delay, 6));  // row of 6 ticks: no delay
    r.start();
    std::vector<uint64_t> at;
    r.render(kTick / 2);
    for (int k = 0; k < 18; ++k) {
        at.push_back(lastAttack(r.e));
        r.render(kTick);
    }
    for (int k = 1; k < 18; ++k) {
        CAPTURE(k);
        CHECK((at[size_t(k)] != at[size_t(k - 1)]) == (k == 9 || k == 12));  // row 1 at tick 3, row 2 at tick 0
    }
}

TEST_CASE("Commands: Cxx cuts the notes of the row at tick xx, C00 at once (D-338)", "[engine][seq][commands]") {
    Rig r;
    SeqLane& l = r.lane(0);
    l.pat[0] = l.pat[2] = 0;
    l.tie[0] = l.tie[1] = 1;
    l.setCmd(0, seqCmd(SeqCmd::Cut, 2));  // held note cut at tick 2 of its first row
    l.setCmd(2, seqCmd(SeqCmd::Cut, 0));
    r.start();
    const auto on = perTick(r, 18, [](const Engine::VoiceInfo& x) { return x.on; });
    for (size_t k = 0; k < 18; ++k) {
        CAPTURE(k);
        CHECK(on[k] == (k < 2));  // row 1 (tied) plays nothing; row 2 cut at its attack
    }
}

TEST_CASE("Commands: Exx sets R13 from its row on, held note and next notes; out of 8-15, ignored (D-338)",
          "[engine][seq][commands]") {
    Rig r(VoiceMode::Authentic3);
    TrackParams p;
    p.mix.enabled = true;
    p.patch.envelopeVolume = true;
    r.e.setTrack(0, p);
    SeqLane& l = r.lane(0);
    l.pat[0] = l.pat[2] = 0;
    l.tie[0] = l.tie[1] = 1;
    l.setCmd(1, seqCmd(SeqCmd::Shape, 0x0A));
    l.setCmd(2, seqCmd(SeqCmd::Shape, 0x05));  // ignored
    l.setCmd(3, seqCmd(SeqCmd::Sync, 0x12));   // silent
    r.start();
    r.render(kTick * 6 * 4);
    std::vector<std::pair<double, int>> w;
    const double perSample = r.cfg.render.chip.clock.hz() / 48000;
    for (const auto& x : r.writes)
        if (x.reg == 13) w.emplace_back(double(x.cycle) / perSample, x.value);
    REQUIRE(w.size() == 3);  // attack, E0A, attack of row 2
    CHECK(w[0].second != 0x0A);
    CHECK(std::fabs(w[1].first - kTick * 6) < 2);
    CHECK(w[1].second == 0x0A);
    CHECK(w[2].second == 0x0A);

    // Play starts again with the shape of the sound.
    r.e.seqStop(0);
    r.render(kTick);
    r.writes.clear();
    l.clearCmd(SeqCmd::Shape, 1);
    r.e.seqPlay(0);
    r.render(kTick * 6 * 3);
    REQUIRE(!r.writes.empty());
    for (const auto& x : r.writes)
        if (x.reg == 13) CHECK(x.value == w[0].second);
}

TEST_CASE("Commands: Oxx starts the DMA samples of the row xx × 256 frames in; the loop resumes there (D-338)",
          "[engine][seq][commands][dma]") {
    Rig r;
    r.g.ste = true;
    TrackParams p;
    p.mix.enabled = true;
    p.patch.generator = Generator::Dma;
    p.patch.dmaSlot = 0;
    r.e.setTrack(0, p);
    DmaParams d;
    d.slots[0].rate = 2;
    d.slots[0].loop = true;
    r.e.setDma(d);
    r.e.setDmaSample(0, std::vector<int8_t>(1000, 50));
    SeqLane& l = r.lane(0);
    l.pat[0] = l.pat[2] = 0;
    l.tie[0] = l.tie[1] = 1;  // rows 0 and 1: one note, long enough to loop
    l.setCmd(0, seqCmd(SeqCmd::Offset, 2));
    l.setCmd(2, seqCmd(SeqCmd::Offset, 4));  // 1024: past the end, silence
    r.start();
    auto dma = [&] {
        for (int i = 0; i < Engine::kVoices; ++i)
            if (const Engine::DmaInfo x = r.e.dmaVoice(i); x.on) return x;
        return Engine::DmaInfo{};
    };
    r.render(48);
    REQUIRE(dma().on);
    CHECK(dma().position >= 512);
    CHECK(dma().position < 600);
    for (int k = 0; k < 11; ++k) {  // the loop goes back to frame 512, not 0
        r.render(kTick);
        CAPTURE(k);
        REQUIRE(dma().on);
        CHECK(dma().position >= 512);
    }
    r.render(kTick);  // row 2
    CHECK_FALSE(dma().on);
}

TEST_CASE("Commands: Fxx detunes the track by xx - 100 cents from its row on, held note included (D-338)",
          "[engine][seq][commands]") {
    Rig plain;
    plain.lane(0).pat[0] = 0;
    plain.start();
    plain.render(kTick / 2);
    const double tp = plain.e.voice(0).tonePeriod;

    Rig r;
    SeqLane& l = r.lane(0);
    l.pat[0] = l.pat[2] = 0;
    l.tie[0] = l.tie[1] = 1;
    l.setCmd(0, seqCmd(SeqCmd::Fine, 150));  // +50 cents
    l.setCmd(1, seqCmd(SeqCmd::Fine, 50));   // -50 cents, the held note too
    l.pat[3] = 0;
    l.setCmd(3, seqCmd(SeqCmd::Fine, 0xFF));  // beyond 200: +100 cents
    r.start();
    const auto t = perTick(r, 24, [](const Engine::VoiceInfo& x) { return x.tonePeriod; });
    CHECK(std::fabs(t[0] - periodAt(tp, 0.5)) <= 1);
    CHECK(std::fabs(t[6] - periodAt(tp, -0.5)) <= 1);
    CHECK(std::fabs(t[12] - periodAt(tp, -0.5)) <= 1);  // new note: the tune of the track holds
    CHECK(std::fabs(t[18] - periodAt(tp, 1)) <= 1);
    // Play starts without it.
    r.e.seqStop(0);
    r.render(kTick);
    r.e.seqPlay(0);
    l.clearCmd(SeqCmd::Fine, 0);
    r.render(kTick / 2);
    CHECK(r.e.voice(0).tonePeriod == tp);
}
