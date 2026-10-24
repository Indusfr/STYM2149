// STYM2149 — instrument sequences of the engine (M6, B2, D-148): PER, NP, MIX, GEN and FIX rows of the Modulation
// table, detune, "analog" randomness, pitch matrix, hard sync, instrument chained at the end of the table, table
// under the arpeggio.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "ymengine/Engine.h"
#include "ymengine/Pitch.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

constexpr uint32_t kFrame = 960;  // one 50 Hz VBL frame at 48 kHz

struct Rig {
    Engine e;
    EngineConfig cfg;
    InstrumentBank bank{};
    TrackParams t;

    explicit Rig(VoiceMode mode = VoiceMode::Studio16) {
        cfg.render.hostRate = 48000;
        e.prepare(cfg);
        GlobalParams g;
        g.mode = mode;
        e.setGlobal(g);
        t.mix.enabled = true;
        t.patch.ornamentOn = true;
        t.patch.ornamentLength = 4;
        e.setInstruments(&bank);
    }
    void apply() { e.setTrack(0, t); }
    void render(uint32_t n) {
        static std::vector<float> l(kFrame), r(kFrame);
        while (n > 0) {
            const uint32_t k = std::min<uint32_t>(n, kFrame);
            e.render(l.data(), r.data(), k);
            n -= k;
        }
    }
    // Note played, then the middle of its first frame.
    void play(int note) {
        e.noteOn(0, note, 100, 0);
        render(kFrame / 2);
    }
    Engine::VoiceInfo v() const { return e.voice(0); }
    uint16_t period(double note) const { return tonePeriodFor(noteHz(note), cfg.render.chip.clock); }
};

}  // namespace

TEST_CASE("Sequences: PER, NP and MIX rows frame by frame, the sound of the note when they are at 0",
          "[engine][sequences]") {
    Rig r;
    r.t.patch.noisePeriod = 9;
    r.t.patch.rows.period = {0, 10, -10, 0};
    r.t.patch.rows.noise = {0, 5, 0, 31};
    r.t.patch.rows.mix = {0, 3, 8, 1};  // Noise, Tone+Noise+Env, Off
    r.apply();
    r.play(60);
    const uint16_t tp = r.period(60);
    struct Frame {
        int delta, np;
        bool tone, noise, env;
    };
    const Frame want[] = {{0, 9, true, false, false},
                          {10, 5, false, true, false},
                          {-10, 9, true, true, true},
                          {0, 31, false, false, false},
                          {0, 9, true, false, false}};  // looped
    for (const Frame& f : want) {
        const auto v = r.v();
        REQUIRE(v.on);
        CHECK(v.tonePeriod == tp + f.delta);
        CHECK(v.noisePeriod == f.np);
        CHECK(v.tone == f.tone);
        CHECK(v.noise == f.noise);
        CHECK(v.envelope == f.env);
        r.render(kFrame);
    }

    // Table off: the rows do not play.
    r.t.patch.ornamentOn = false;
    r.apply();
    r.e.allNotesOff(0);
    r.render(kFrame);
    r.play(60);
    r.render(kFrame);
    CHECK(r.v().tonePeriod == tp);
    CHECK(r.v().noisePeriod == 9);
    CHECK_FALSE(r.v().noise);
}

TEST_CASE("Sequences: GEN row, SID and Sync Buzzer for the duration of a step", "[engine][sequences]") {
    Rig r;
    r.t.patch.rows.gen = {0, uint8_t(SeqGen::Sid), uint8_t(SeqGen::SyncBuzzer), 0};
    r.t.patch.ornamentLoop = false;  // Hold: the last step stays
    r.apply();
    r.play(57);
    CHECK(r.v().gen == Generator::Chip);
    CHECK(r.v().timerHz == 0);
    r.render(kFrame);
    CHECK(r.v().gen == Generator::Sid);
    CHECK(r.v().timerHz > 0);
    CHECK(r.v().tone);
    r.render(kFrame);
    CHECK(r.v().gen == Generator::SyncBuzzer);
    CHECK(r.v().envelope);
    CHECK_FALSE(r.v().tone);
    CHECK(std::abs(r.v().timerHz - noteHz(57)) < 0.01);
    r.render(kFrame);
    CHECK(r.v().gen == Generator::Chip);
    CHECK(r.v().timerHz == 0);
    CHECK(r.v().tone);
    CHECK_FALSE(r.v().envelope);
}

TEST_CASE("Sequences: FIX plays the fixed note; detune and analog randomness", "[engine][sequences]") {
    Rig r;
    r.t.patch.fixedNote = 84;
    r.t.patch.rows.fix = {1, 0, 0, 0};
    r.apply();
    r.play(48);
    CHECK(r.v().tonePeriod == r.period(84));
    CHECK(r.v().notePeriod == r.period(48));
    r.render(kFrame);
    CHECK(r.v().tonePeriod == r.period(48));

    // Detune: the whole note, the fixed note included.
    r.t.patch.detuneCents = 100;
    r.apply();
    r.e.allNotesOff(0);
    r.render(kFrame);
    r.play(48);
    CHECK(r.v().tonePeriod == r.period(85));
    r.render(kFrame);
    CHECK(r.v().notePeriod == r.period(49));

    // Analog: one offset per note, within ±50 cents, different from one note to the next.
    r.t.patch.detuneCents = 0;
    r.t.patch.ornamentOn = false;
    r.t.patch.analogCents = 50;
    r.apply();
    r.e.allNotesOff(0);
    r.render(kFrame);
    std::vector<uint16_t> seen;
    for (int k = 0; k < 8; ++k) {
        r.play(48);
        const uint16_t p = r.v().notePeriod;
        CHECK(p <= r.period(47.5));
        CHECK(p >= r.period(48.5));
        seen.push_back(p);
        r.e.allNotesOff(0);
        r.render(kFrame);
    }
    std::sort(seen.begin(), seen.end());
    CHECK(seen.front() != seen.back());
}

TEST_CASE("Sequences: pitch matrix, square, timer and envelope", "[engine][sequences]") {
    Rig r;
    r.t.patch.generator = Generator::Sid;
    r.t.patch.ornamentLength = 2;
    r.t.patch.ornamentPitch[1] = 12;
    r.t.patch.envPeriod = 640;
    r.apply();
    // By default (prototype): square and timer rise by one octave, the envelope does not move.
    r.play(60);
    const double hz = r.v().timerHz;
    r.render(kFrame);
    CHECK(std::abs(r.v().timerHz - 2 * hz) < 0.01);
    CHECK(std::abs(int(r.v().tonePeriod) - int(r.period(60)) / 2) <= 1);
    CHECK(r.v().envPeriod == 640);

    // Table to the square alone: the timer stays; to the envelope: its period follows (half at the octave).
    r.t.patch.pitchMatrix = pitchBit(PitchSource::Table, PitchTarget::Square) |
                            pitchBit(PitchSource::Table, PitchTarget::Buzzer);
    r.apply();
    r.e.allNotesOff(0);
    r.render(kFrame);
    r.play(60);
    CHECK(r.v().envPeriod == 640);
    r.render(kFrame);
    CHECK(std::abs(r.v().timerHz - hz) < 0.01);
    CHECK(std::abs(int(r.v().tonePeriod) - int(r.period(60)) / 2) <= 1);
    CHECK(r.v().envPeriod == 320);

    // Nothing to the square: the table does not touch the period.
    r.t.patch.pitchMatrix = pitchBit(PitchSource::Table, PitchTarget::Timer);
    r.apply();
    r.e.allNotesOff(0);
    r.render(kFrame);
    r.play(60);
    r.render(kFrame);
    CHECK(r.v().tonePeriod == r.period(60));
    CHECK(std::abs(r.v().timerHz - 2 * hz) < 0.01);
}

TEST_CASE("Sequences: instrument chained at the end of a table in Hold", "[engine][sequences][instruments]") {
    Rig r;
    r.t.patch.ornamentLength = 2;
    r.t.patch.ornamentLoop = false;
    r.t.patch.volume = 11;
    r.t.patch.link = 3;
    Instrument& x = r.bank[2];
    x.used = true;
    x.patch.generator = Generator::Sid;
    x.patch.ornamentOn = true;
    x.patch.ornamentLength = 1;
    x.patch.rows.noise = {7};
    x.patch.toneOn = false;
    x.patch.noiseOn = true;
    r.apply();
    r.play(60);
    CHECK(r.v().gen == Generator::Chip);
    r.render(kFrame);
    CHECK(r.v().gen == Generator::Chip);  // second and last step
    // Next frame: the sound of instrument 3 (its table at the first step), the volume of the note.
    r.render(kFrame);
    CHECK(r.v().gen == Generator::Sid);
    CHECK(r.v().timerHz > 0);
    CHECK_FALSE(r.v().tone);
    CHECK(r.v().noise);
    CHECK(r.v().noisePeriod == 7);
    CHECK(r.v().volume == 11);

    // Missing instrument: the table stays in Hold.
    r.t.patch.link = 9;
    r.apply();
    r.e.allNotesOff(0);
    r.render(kFrame);
    r.play(60);
    r.render(4 * kFrame);
    CHECK(r.v().on);
    CHECK(r.v().gen == Generator::Chip);
}

TEST_CASE("Sequences: hard sync restarts the square on the note reached by a slide", "[engine][sequences]") {
    // Step 1 slid to step 2: the target note keeps the phase of the voice without Hard sync, restarts it with it.
    auto run = [](bool sync) {
        Rig r;
        GlobalParams g;
        g.tempo = 120;
        r.e.setGlobal(g);
        r.t.patch.ornamentOn = false;
        r.t.patch.hardSync = sync;
        r.apply();
        SeqData data;
        auto lane = std::make_shared<SeqLane>();
        lane->pat[0] = 0;
        lane->pat[1] = 7;
        lane->slide[0] = 1;
        data.patterns[0].steps = 2;
        data.patterns[0].lanes[0] = lane;
        r.e.setSequence(&data);
        r.e.seqPlay(0);
        std::vector<float> out, l(kFrame), rr(kFrame);
        for (int k = 0; k < 12; ++k) {
            r.e.render(l.data(), rr.data(), kFrame);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    };
    const std::vector<float> a = run(false), b = run(true);
    REQUIRE(a.size() == b.size());
    size_t first = 0;
    while (first < a.size() && a[first] == b[first]) ++first;
    CAPTURE(first);
    CHECK(first >= 6000);  // the second step (120 BPM, sixteenth notes)
    CHECK(first < 6000 + kFrame);
}

TEST_CASE("Sequences: preview of the table, the period the engine plays at each step", "[engine][sequences]") {
    // tablePeriod (Y43) redoes the computation of the engine: semitones of the table, PER, FIX, detune, matrix.
    for (const uint16_t matrix : {ymengine::kPitchMatrixDefault, uint16_t(pitchBit(PitchSource::Table, PitchTarget::Timer)),
                                  uint16_t(ymengine::kPitchMatrixAll)}) {
        CAPTURE(matrix);
        Rig r;
        r.t.patch.ornamentLength = 6;
        r.t.patch.ornamentPitch = {0, 12, -5, 7, 0, 3};
        r.t.patch.rows.period = {0, 0, 9, -20, 3, 0};
        r.t.patch.rows.fix = {0, 0, 0, 0, 1, 1};
        r.t.patch.fixedNote = 72;
        r.t.patch.detuneCents = 33;
        r.t.patch.pitchMatrix = matrix;
        r.apply();
        r.play(55);
        for (int s = 0; s < 8; ++s) {  // looped
            CAPTURE(s);
            CHECK(r.v().tonePeriod == tablePeriod(r.t.patch, 55, s % 6, r.cfg.render.chip.clock));
            r.render(kFrame);
        }
    }
    TrackPatch off;
    off.ornamentPitch[0] = 12;
    CHECK(tablePeriod(off, 60, 0, ymcore::clocks::kDefault) == tonePeriodFor(noteHz(60), ymcore::clocks::kDefault));
    off.ornamentOn = true;
    CHECK(tablePeriod(off, 60, 0, ymcore::clocks::kDefault) == 239);  // 478 / 2, rounded
    CHECK(tablePeriod(off, 60, 40, ymcore::clocks::kDefault) == tablePeriod(off, 60, 7, ymcore::clocks::kDefault));
    off.generator = Generator::Chipdrum;
    CHECK(tablePeriod(off, 60, 0, ymcore::clocks::kDefault) == tonePeriodFor(noteHz(60), ymcore::clocks::kDefault));
}

namespace {

// Pitch row of the arpeggio tests: one ratio of periods per step of the table.
constexpr std::array<int8_t, 5> kArpPitch = {0, 12, 7, 5, 3};

// Step of the table that the voice plays, from its period and that of its note (-1: none).
int tableStep(const Engine::VoiceInfo& v) {
    if (!v.on || v.notePeriod == 0) return -1;
    for (size_t k = 0; k < kArpPitch.size(); ++k)
        if (std::abs(double(v.tonePeriod) / v.notePeriod - std::pow(2.0, -kArpPitch[k] / 12.0)) < 0.01) return int(k);
    return -1;
}
// Voice of the keyboard arpeggio (-1: none).
int arpVoice(const Engine& e) {
    for (int i = 0; i < Engine::kVoices; ++i)
        if (const Engine::VoiceInfo v = e.voice(i); v.on && !v.fading && v.tag == VoiceTag::Arp) return i;
    return -1;
}
// Arpeggio of the track at hz (Table 0, 4, 7) on a table of five steps, looped; 60 BPM: a step of 12000 samples,
// 12.5 frames.
void arpRig(Rig& r, VoiceMode mode, double hz) {
    GlobalParams g;
    g.mode = mode;
    g.tempo = 60;
    r.e.setGlobal(g);
    r.t.patch.ornamentLength = 5;
    std::copy(kArpPitch.begin(), kArpPitch.end(), r.t.patch.ornamentPitch.begin());
    r.t.arp.on = true;
    r.t.arp.mode = ArpMode::Table;
    r.t.arp.rateHz = hz;
    r.t.seq.gate = 0.9;
    r.apply();
}
// Pattern of four steps, the first played by the arpeggio of track 1.
void arpStep(Rig& r, SeqData& data) {
    auto lane = std::make_shared<SeqLane>();
    lane->pat[0] = 0;
    lane->setCmd(0, seqCmd(SeqCmd::Arpeggiator, 1));  // the arpeggio on step 0 alone
    lane->setCmd(1, seqCmd(SeqCmd::Arpeggiator, 0));
    data.patterns[0].steps = 4;
    data.patterns[0].lanes[0] = lane;
    r.e.setSequence(&data);
    r.e.seqPlay(0);
}

}  // namespace

TEST_CASE("Sequences: the Modulation table runs on from one note of the arpeggio to the next",
          "[engine][sequences][arp]") {
    // First DAW test, item 3: each tick of the arpeggio retriggers the note, which restarted its table; at 50 Hz,
    // the rate of the frames, an ARP step stayed on the first step of the table. The table, the vibrato and the
    // cadence of the frames now go on like the envelope (grabEnv / putEnv): one step of the table per frame.
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3}) {
        for (const double hz : {50.0, 37.0, 200.0}) {
            CAPTURE(int(mode), hz);
            {  // ARP step of the sequencer
                Rig r(mode);
                arpRig(r, mode, hz);
                SeqData data;
                arpStep(r, data);
                r.render(kFrame / 2);
                std::vector<int> notes;
                for (int k = 0; k < 10; ++k) {
                    CAPTURE(k);
                    CHECK(tableStep(r.v()) == k % 5);
                    if (std::find(notes.begin(), notes.end(), r.v().note) == notes.end()) notes.push_back(r.v().note);
                    r.render(kFrame);
                }
                CHECK(notes.size() > 1);  // the arpeggio plays
            }
            {  // keyboard arpeggio
                Rig r(mode);
                arpRig(r, mode, hz);
                r.e.noteOn(0, 60, 100, 0);
                int vi = -1;
                for (int s = 0; s < 4 * int(kFrame) && vi < 0; ++s) {
                    r.render(1);
                    vi = arpVoice(r.e);
                }
                REQUIRE(vi >= 0);
                r.render(kFrame / 2);
                std::vector<int> notes;
                for (int k = 0; k < 10; ++k) {
                    CAPTURE(k);
                    vi = arpVoice(r.e);
                    REQUIRE(vi >= 0);
                    CHECK(tableStep(r.e.voice(vi)) == k % 5);
                    if (std::find(notes.begin(), notes.end(), r.e.voice(vi).note) == notes.end())
                        notes.push_back(r.e.voice(vi).note);
                    r.render(kFrame);
                }
                CHECK(notes.size() > 1);
            }
        }
        CAPTURE(int(mode));
        {  // Modulation switched on during playback: the table starts at the next note of the arpeggio
            Rig r(mode);
            r.t.patch.ornamentOn = false;
            arpRig(r, mode, 50);
            SeqData data;
            arpStep(r, data);
            r.render(3 * kFrame + kFrame / 2);
            CHECK(tableStep(r.v()) == 0);
            r.t.patch.ornamentOn = true;
            r.apply();
            std::vector<int> steps;
            for (int k = 0; k < 6; ++k) {
                r.render(kFrame);
                steps.push_back(tableStep(r.v()));
            }
            CHECK(steps == std::vector<int>{0, 1, 2, 3, 4, 0});
        }
        {  // instrument chained at the end of the table: it stays from one note of the arpeggio to the next
            Rig r(mode);
            r.t.patch.ornamentLoop = false;
            r.t.patch.link = 3;
            arpRig(r, mode, 50);
            r.t.patch.ornamentLength = 2;
            r.apply();
            Instrument& x = r.bank[2];
            x.used = true;
            x.patch.ornamentOn = true;
            x.patch.ornamentLength = 3;
            x.patch.ornamentPitch = {7, 5, 3};
            x.patch.noiseOn = true;
            x.patch.noisePeriod = 7;
            SeqData data;
            arpStep(r, data);
            r.render(kFrame / 2);
            for (int k = 0; k < 10; ++k) {
                CAPTURE(k);
                CHECK(tableStep(r.v()) == (k < 2 ? k : 2 + (k - 2) % 3));
                CHECK(r.v().noise == (k >= 2));
                r.render(kFrame);
            }
        }
    }
}
