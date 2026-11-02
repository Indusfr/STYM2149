// STYM2149 — plugincore: export of the Song as a YM-Tracker song: notes, gates and note offs, the volume
// column, SLD and SID effects, Ticks and BPM timing, the Song unrolled, digidrum hits, instruments, refused projects
// and the song read back by the import.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterImport.h"
#include "plugincore/YmtExport.h"
#include "plugincore/YmtImport.h"
#include "ymengine/Pitch.h"
#include "ymio/YmTracker.h"

using namespace stym;
using plugin::NonParamState;
using ymengine::Generator;
using ymengine::SeqCmd;
using ymengine::seqCmd;
using ymio::YmtCell;

namespace {

constexpr uint8_t kOff = ymio::kYmtNoteOff;

// Authentic 3ch at 50 Hz, Ticks timing at speed 6, tracks 1 to 3 on Chip Voice and track 4 on Digidrum (Level 100 %,
// volume 15), the grid at octave 4, the Song: pattern 0 once, looped.
struct Project {
    plugin::ParamValues values{};
    NonParamState state;
};
Project project() {
    Project p;
    p.values = plugin::defaultValues();
    auto set = [&](int k, plugin::Track f, float v) { p.values[size_t(plugin::paramIndex(k, f))] = v; };
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    p.values[size_t(plugin::paramIndex(plugin::Global::VblRate))] = 0;
    for (int k = 0; k < plugin::kTracks; ++k) {
        set(k, plugin::Track::On, k < 4 ? 1.0f : 0.0f);
        set(k, plugin::Track::Generator, float(int(k == 3 ? Generator::Digidrum : Generator::Chip)));
        set(k, plugin::Track::Level, 100);
        set(k, plugin::Track::Volume, 15);
        p.state.seq.tracks[size_t(k)].octave = 4;
    }
    p.state.global.timing = ymengine::SeqTiming::Ticks;
    p.state.global.speed = 6;
    p.state.global.loop = true;
    p.state.seq.patterns[0].steps = 16;
    p.state.seq.song.entries = {ymseq::SongEntry{}};
    return p;
}

// Note `midi` at step s of track t in pattern pat.
ymseq::Lane& note(Project& p, int pat, int t, int s, int midi) {
    ymseq::Lane& l = p.state.seq.patterns[size_t(pat)].edit(t);
    l.pat[size_t(s)] = int16_t(midi % 12);
    l.oct[size_t(s)] = int8_t(ymseq::octaveCode(midi / 12 - 1, p.state.seq.tracks[size_t(t)].octave));
    return l;
}

plugin::YmtExport exported(const Project& p) {
    const plugin::YmtExportResult r = plugin::exportYmt(p.values, p.state, "Test");
    INFO(r.error);
    REQUIRE(r.value);
    return *r.value;
}

// Cell of channel c at row `row` of position `pos`.
const YmtCell& at(const plugin::YmtExport& x, int pos, int row, int c) {
    return x.song.cell(x.song.order[size_t(pos)], row, c);
}

int ymtNote(int midi) { return midi - 23; }
int period(int midi) { return ymengine::tonePeriodFor(ymengine::noteHz(midi), ymcore::clocks::kDefault); }
// Qxx, Sxx and Uxx for a timer at hz (INFERRED: 20 × 2^(x + y / 16) Hz).
int timerParam(double hz) { return int(std::lround(16 * std::log2(hz / 20))); }

}  // namespace

TEST_CASE("YM-Tracker export: notes, gates, note offs and the volume column", "[ymt-export]") {
    Project p = project();
    ymseq::Lane& a = note(p, 0, 0, 0, 60);  // C4 tied over steps 0 to 3
    for (int s = 0; s < 4; ++s) a.tie[size_t(s)] = 1;
    note(p, 0, 0, 8, 64);                    // E4 at the gate of the track (50 %)
    note(p, 0, 0, 12, 67).gat[12] = 100;     // G4 over the whole step
    note(p, 0, 1, 0, 48).vol[0] = 8;         // C3 at VOL 8
    const plugin::YmtExport x = exported(p);
    CHECK(x.positions == 1);
    CHECK(x.patterns == 1);
    CHECK(x.instruments == 2);  // the sounds of tracks 1 and 2
    CHECK(x.notes == 4);
    CHECK(x.loops);
    CHECK(x.song.restart == 0);
    CHECK(x.song.bpm == 125);  // a tick at 50 Hz
    CHECK(x.song.speed == 6);
    CHECK(x.song.chips == 1);
    CHECK(x.song.channels() == 4);
    CHECK(x.song.channelNames[0] == "Ch 1");
    CHECK(x.song.instruments.size() == 17);  // "--" and 16 slots
    CHECK(x.song.instruments[1].name == "Track 1");
    CHECK(x.song.instruments[2].name == "Track 2");
    CHECK(x.remarks.empty());
    // Channel 1: C-5 held until the note off of row 4; E-5 released at tick 3 of row 8; G-5 cut on the next row.
    CHECK(at(x, 0, 0, 0).note == 37);
    CHECK(at(x, 0, 0, 0).instrument == 1);
    CHECK(at(x, 0, 0, 0).volume == 0);
    CHECK(at(x, 0, 0, 0).effect == 0);
    for (int r = 1; r < 4; ++r) CHECK(at(x, 0, r, 0) == YmtCell{});
    CHECK(at(x, 0, 4, 0).note == kOff);
    CHECK(at(x, 0, 8, 0).note == ymtNote(64));
    CHECK(at(x, 0, 8, 0).effect == 'K');
    CHECK(at(x, 0, 8, 0).param == 3);
    CHECK(at(x, 0, 9, 0) == YmtCell{});
    CHECK(at(x, 0, 12, 0).note == ymtNote(67));
    CHECK(at(x, 0, 12, 0).effect == 0);
    CHECK(at(x, 0, 13, 0).note == kOff);
    // Channel 2: VOL 8 plays the 4-bit volume 14 (Impl::attenuated), 60 of 64 in the volume column.
    const int level = plugin::attenuatedVolume(15, 8 / 15.0);
    CHECK(level == 14);
    CHECK(at(x, 0, 0, 1).note == ymtNote(48));
    CHECK(at(x, 0, 0, 1).instrument == 2);
    CHECK(at(x, 0, 0, 1).volume == 0x10 + std::lround(level * 64 / 15.0));
    CHECK(at(x, 0, 0, 1).effect == 'K');
    // Channel 3 and the digidrum channel stay empty (no tempo change).
    for (int r = 0; r < 16; ++r) {
        CAPTURE(r);
        CHECK(at(x, 0, r, 2) == YmtCell{});
        CHECK(at(x, 0, r, 3) == YmtCell{});
    }
    const std::vector<std::string> lines = plugin::ymtExportMessage(x, "test.ymt");
    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "YM-Tracker song exported");
    CHECK(lines[2] == "test.ymt");
    CHECK(lines[3] == "1 position, 1 pattern, 2 instruments.");
    CHECK(lines[4] == "Speed 6 at 125 BPM, loops at position 1.");
}

TEST_CASE("YM-Tracker export: SLD becomes 3xx, a SID note Qxx or Sxx at the rate of the note",
          "[ymt-export]") {
    Project p = project();
    note(p, 0, 0, 0, 60).slide[0] = 1;
    note(p, 0, 0, 1, 64).gat[1] = 100;
    p.values[size_t(plugin::paramIndex(1, plugin::Track::Generator))] = float(int(Generator::Sid));
    p.values[size_t(plugin::paramIndex(2, plugin::Track::Generator))] = float(int(Generator::Sid));
    p.state.tracks[2].patch.sidForm = ymengine::SidForm::Sine;
    note(p, 0, 1, 0, 60).gat[0] = 100;
    note(p, 0, 2, 0, 67).gat[0] = 100;
    const plugin::YmtExport x = exported(p);
    // The slide from C4 to E4 over the five ticks after the first of row 1.
    CHECK(at(x, 0, 1, 0).note == ymtNote(64));
    CHECK(at(x, 0, 1, 0).instrument == 0);
    CHECK(at(x, 0, 1, 0).effect == '3');
    CHECK(at(x, 0, 1, 0).param == (std::abs(period(60) - period(64)) + 4) / 5);
    CHECK(at(x, 0, 2, 0).note == kOff);
    CHECK(x.notes == 3);  // the slide target is not an attack
    // The SID notes: the timer at the note × the SID ratio of the track.
    const double ratio1 = plugin::toTrack(p.values, 1, p.state.tracks[1]).patch.sidRatio;
    const double ratio2 = plugin::toTrack(p.values, 2, p.state.tracks[2]).patch.sidRatio;
    CHECK(at(x, 0, 0, 1).effect == 'Q');
    CHECK(at(x, 0, 0, 1).param == timerParam(ymengine::noteHz(60) * ratio1));
    CHECK(at(x, 0, 0, 2).effect == 'S');
    CHECK(at(x, 0, 0, 2).param == timerParam(ymengine::noteHz(67) * ratio2));
    CHECK(at(x, 0, 1, 1).note == kOff);
    CHECK(x.remarks.empty());
}

TEST_CASE("YM-Tracker export: Sxx and the groove of Ticks timing become Fxx on channel 4", "[ymt-export]") {
    Project p = project();
    p.state.global.grooveLength = 2;
    p.state.global.groove[1] = 2;
    note(p, 0, 0, 0, 60).setCmd(4, seqCmd(SeqCmd::Speed, 3));
    const plugin::YmtExport x = exported(p);
    CHECK(x.song.speed == 6);
    CHECK(x.song.bpm == 125);
    // Rows of 6 and 8 ticks, then of 3 and 5 from the S03 of row 4; the first row restates the speed of the song,
    // which the end of the song leaves at 5.
    const std::vector<int> want = {6, 8, 6, 8, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5, 3, 5};
    for (int r = 0; r < 16; ++r) {
        CAPTURE(r);
        CHECK(at(x, 0, r, 3).effect == 'F');
        CHECK(at(x, 0, r, 3).param == want[size_t(r)]);
    }
}

TEST_CASE("YM-Tracker export: the tempo of BPM timing, the BPM row and the tempo of an entry",
          "[ymt-export]") {
    Project p = project();
    p.state.global.timing = ymengine::SeqTiming::Bpm;
    p.values[size_t(plugin::paramIndex(plugin::Global::FreeTempo))] = 120;
    p.state.seq.patterns[0].bpm[8] = 150;
    p.state.seq.patterns[1].steps = 4;
    note(p, 0, 0, 0, 60);
    note(p, 1, 0, 0, 62);
    p.state.seq.song.entries = {ymseq::SongEntry{0}, ymseq::SongEntry{1, 1, 90}};
    const plugin::YmtExport x = exported(p);
    // 120 BPM in sixteenths at 50 Hz: rows of 6 ticks (6.25), at 120 × 4 × 6 / 24 = 120 BPM.
    CHECK(x.song.speed == 6);
    CHECK(x.song.bpm == 120);
    REQUIRE(x.positions == 2);
    CHECK(at(x, 0, 0, 3).effect == 'F');
    CHECK(at(x, 0, 0, 3).param == 120);
    CHECK(at(x, 0, 8, 3).effect == 'F');
    CHECK(at(x, 0, 8, 3).param == 150);
    CHECK(at(x, 1, 0, 3).effect == 'F');
    CHECK(at(x, 1, 0, 3).param == 90);
    int tempos = 0;
    for (int r = 0; r < 16; ++r) tempos += at(x, 0, r, 3).effect == 'F';
    CHECK(tempos == 2);
}

TEST_CASE("YM-Tracker export: the Song unrolled, its restart position and its end without Loop",
          "[ymt-export]") {
    Project p = project();
    p.state.seq.patterns[0].steps = 4;
    p.state.seq.patterns[1].steps = 4;
    note(p, 0, 0, 0, 60);
    note(p, 1, 0, 0, 67);
    // A twice, B (the restart point), back over the two entries once.
    p.state.seq.song.entries = {ymseq::SongEntry{0, 2}, ymseq::SongEntry{1, 1, 0, ymseq::SongKind::Pattern, 0, true},
                                ymseq::SongEntry::loop(2, 1)};
    plugin::YmtExport x = exported(p);
    CHECK(x.song.order == std::vector<uint8_t>{0, 0, 1, 0, 0, 1});
    CHECK(x.patterns == 2);
    CHECK(x.song.restart == 2);
    CHECK(at(x, 2, 0, 0).note == ymtNote(67));

    p.state.global.loop = false;
    x = exported(p);
    REQUIRE(x.song.order == std::vector<uint8_t>{0, 0, 1, 0, 0, 1, 2});
    CHECK_FALSE(x.loops);
    CHECK(x.patterns == 3);
    CHECK(x.song.restart == 6);
    // The silent pattern that ends the song: its notes released, a jump to itself.
    CHECK(x.song.patterns[2].rows == 16);
    for (int c = 0; c < 3; ++c) CHECK(at(x, 6, 0, c).note == kOff);
    CHECK(at(x, 6, 15, 3).effect == 'B');
    CHECK(at(x, 6, 15, 3).param == 6);
    const std::vector<std::string> lines = plugin::ymtExportMessage(x, "test.ymt");
    REQUIRE(lines.size() == 5);
    CHECK(lines[3] == "7 positions, 3 patterns, 1 instrument.");
    CHECK(lines[4] == "Speed 6 at 125 BPM, plays once.");

    // A Note-off entry: a pattern of its length that releases the note held from the entry before.
    for (int s = 0; s < 4; ++s) p.state.seq.patterns[0].edit(0).tie[size_t(s)] = 1;
    p.state.seq.song.entries = {ymseq::SongEntry{0}, ymseq::SongEntry::rest(ymseq::SongKind::NoteOff, 8)};
    x = exported(p);
    REQUIRE(x.song.order.size() == 3);
    CHECK(x.song.patterns[x.song.order[1]].rows == 8);
    CHECK(at(x, 1, 0, 0).note == kOff);
    for (int r = 1; r < 4; ++r) CHECK(at(x, 0, r, 0).note == 0);
}

TEST_CASE("YM-Tracker export: digidrum hits as Zxx on the channel that the Digidrum track takes",
          "[ymt-export]") {
    Project p = project();
    p.state.digidrums[0] = std::vector<uint8_t>(1000, 9);
    p.state.digiSlots[0].name = "Kick";
    p.state.digiSlots[0].play.rateHz = 8000;
    p.state.digidrums[1] = std::vector<uint8_t>(100, 20);
    p.state.digiSlots[1].name = "Hat";
    p.state.digiSlots[1].play.rateHz = 16000;
    for (int k = 0; k < 2; ++k) p.state.digiSlots[size_t(k)].play.root = uint8_t(60 + k);
    ymseq::Lane& d = p.state.seq.patterns[0].edit(3);
    d.pat[0] = 0;   // Kick: 0.125 s, cut by the end of its step (0.12 s)
    d.pat[4] = 1;   // Hat at VOL 8
    d.vol[4] = 8;
    d.pat[8] = 0;   // Kick an octave up: another sample of the bank
    d.ton[8] = 12;
    note(p, 0, 0, 0, 60);
    note(p, 0, 2, 4, 60);  // under the Hat: the sequencer leaves it out
    const plugin::YmtExport x = exported(p);
    REQUIRE(x.samples == 3);
    CHECK(x.instruments == 2);
    const int dc = p.state.global.digidrumChannel[0];
    REQUIRE(dc == 2);
    const YmtCell& kick = at(x, 0, 0, dc);
    CHECK(kick.note == 37);
    CHECK(kick.effect == 'Z');
    CHECK(kick.param == 1);
    REQUIRE(kick.instrument >= 1);
    CHECK(at(x, 0, 1, dc).note == kOff);
    CHECK(at(x, 0, 4, dc).effect == 'Z');
    CHECK(at(x, 0, 4, dc).param == 2);
    CHECK(at(x, 0, 4, dc).instrument == kick.instrument);
    CHECK(at(x, 0, 4, dc).volume == 0x10 + std::lround(plugin::attenuatedVolume(15, 8 / 15.0) * 64 / 15.0));
    CHECK(at(x, 0, 5, dc) == YmtCell{});  // the Hat ends within its step
    CHECK(at(x, 0, 8, dc).param == 3);
    CHECK(at(x, 0, 9, dc) == YmtCell{});
    // The bank of the silent Digidrums instrument: each sample at the rate that its hits play.
    const ymio::YmtInstrument& kit = x.song.instruments[kick.instrument];
    CHECK(kit.name == "Digidrums");
    CHECK_FALSE(kit.tone);
    REQUIRE(kit.drums.size() == 3);
    CHECK(kit.drums[0].name == "Kick");
    CHECK(kit.drums[0].rate == 8000);
    CHECK(kit.drums[0].levels.size() == 1000);
    CHECK(kit.drums[1].name == "Hat");
    CHECK(kit.drums[1].rate == 16000);
    CHECK(kit.drums[1].levels[0] == 15);
    CHECK(kit.drums[2].name == "Kick 2");
    CHECK(kit.drums[2].rate == 16000);
    REQUIRE(x.remarks.size() == 1);
    CHECK(x.remarks[0] == "Notes under the Digidrum hits, left out as the sequencer does: 1.");
    CHECK(plugin::ymtExportMessage(x, "t.ymt")[3] == "1 position, 1 pattern, 2 instruments, 3 digidrums.");
}

TEST_CASE("YM-Tracker export: an instrument's ADSR, envelope, vibrato and table", "[ymt-export]") {
    Project p = project();
    plugin::Instrument& bass = p.state.instruments[0];
    bass.used = true;
    bass.name = "Bass";
    ymengine::TrackPatch& b = bass.patch;
    b.volume = 15;
    b.toneOn = true;
    b.noiseOn = true;
    b.noisePeriod = 7;
    b.attackMs = 40;
    b.decayMs = 100;
    b.sustainPct = 60;
    b.releaseMs = 200;
    b.envelopeVolume = true;
    b.envTuned = true;
    b.envShape = 10;
    b.envRatioN = 1;
    b.envRatioD = 2;
    b.vibratoOn = true;
    b.vibratoHz = 6.25;
    b.vibratoCents = 50;
    b.ornamentOn = true;
    b.ornamentLength = 3;
    b.ornamentLoop = true;
    b.ornamentRate = 1;
    b.ornamentPitch[1] = 4;
    b.ornamentPitch[2] = 7;
    b.ornamentVolume.fill(15);
    b.lfo.on = true;
    note(p, 0, 0, 0, 48).ins[0] = 1;
    const plugin::YmtExport x = exported(p);
    REQUIRE(x.instruments == 1);
    REQUIRE(at(x, 0, 0, 0).instrument == 1);
    const ymio::YmtInstrument& in = x.song.instruments[1];
    CHECK(in.name == "Bass");
    // ADSR in ticks of 20 ms.
    CHECK(in.useAdsr);
    CHECK(in.attack == 2);
    CHECK(in.decay == 5);
    CHECK(in.sustain == 9);
    CHECK(in.release == 10);
    CHECK(in.tone);
    CHECK(in.noise);
    CHECK(in.noisePeriod == 7);
    // The looped triangle tuned to half the note: the period from the note, octave 1.
    CHECK(in.hardEnv);
    CHECK_FALSE(in.syncBuzzer);
    CHECK(in.shape == 10);
    CHECK(in.autoPeriod);
    CHECK(in.octave == 1);
    // Vibrato: 8/64 of a cycle per tick, 4/8 of a semitone.
    CHECK(in.vibSpeed == 8);
    CHECK(in.vibDepth == 4);
    CHECK(in.arp == std::vector<int8_t>{0, 4, 7});
    CHECK(in.arpSpeed == 1);
    CHECK(in.pitch == std::vector<int16_t>{0});  // no pitch list: that of the demo songs
    REQUIRE(x.remarks.size() == 1);
    CHECK(x.remarks[0] == "Left out: LFO (1).");
}

TEST_CASE("YM-Tracker export: 16 instruments at most, the others played as the most used one",
          "[ymt-export]") {
    Project p = project();
    p.state.seq.patterns[0].steps = 20;
    for (int k = 0; k < 18; ++k) {
        plugin::Instrument& in = p.state.instruments[size_t(k)];
        in.used = true;
        in.name = "I" + std::to_string(k + 1);
        note(p, 0, 0, k, 60).ins[size_t(k)] = uint8_t(k + 1);
    }
    note(p, 0, 0, 18, 60).ins[18] = 2;
    note(p, 0, 0, 19, 60).ins[19] = 2;
    const plugin::YmtExport x = exported(p);
    CHECK(x.instruments == 16);
    REQUIRE(x.song.instruments.size() == 17);
    CHECK(x.song.instruments[1].name == "I1");
    // I2, played three times, is the most used: I17 and I18 are played with it.
    CHECK(at(x, 0, 16, 0).instrument == 2);
    CHECK(at(x, 0, 17, 0).instrument == 2);
    CHECK(at(x, 0, 15, 0).instrument == 16);
    REQUIRE_FALSE(x.remarks.empty());
    CHECK(x.remarks[0] == "Notes played with the closest of the 16 instruments: 2.");
}

TEST_CASE("YM-Tracker export: projects that YM-Tracker cannot play are refused", "[ymt-export]") {
    Project p = project();
    note(p, 0, 0, 0, 60);
    for (const ymengine::VoiceMode m : {ymengine::VoiceMode::Studio16, ymengine::VoiceMode::Dual3}) {
        p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = float(int(m));
        const plugin::YmtExportResult r = plugin::exportYmt(p.values, p.state, "Test");
        CHECK_FALSE(r.value);
        CHECK(r.error == "YM-Tracker plays the three channels of one chip: switch to Authentic 3ch");
    }
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    p.state.seq.song.entries.clear();
    const plugin::YmtExportResult r = plugin::exportYmt(p.values, p.state, "Test");
    CHECK_FALSE(r.value);
    CHECK(r.error == "The Song is empty.");
}

TEST_CASE("YM-Tracker export: the file read back by the import gives the notes and their levels",
          "[ymt-export]") {
    Project p = project();
    ymseq::Lane& a = note(p, 0, 0, 0, 60);
    for (int s = 0; s < 4; ++s) a.tie[size_t(s)] = 1;
    note(p, 0, 0, 8, 64);
    note(p, 0, 1, 0, 48).vol[0] = 8;
    note(p, 0, 2, 2, 55).gat[2] = 25;
    const plugin::YmtExport x = exported(p);
    const ymio::YmtParseResult back = ymio::parseYmt(ymio::writeYmt(x.song));
    INFO(back.error);
    REQUIRE(back.song);
    CHECK(back.song->title == "Test");
    NonParamState state;
    const plugin::RegisterImportResult r = plugin::importYmt(*back.song, state);
    INFO(r.error);
    REQUIRE(r.import);
    CHECK(r.import->notes == x.notes);
    CHECK(r.import->speed == 6);
    CHECK(plugin::kVblRates[size_t(r.import->vblRate)] == 50);
    const ymseq::Pattern& pat = state.seq.patterns[0];
    REQUIRE(pat.steps == 16);
    auto midi = [&](int t, int s) { return ymseq::absoluteNote(pat.lane(t), 1, s, state.seq.tracks[size_t(t)].octave); };
    CHECK(midi(0, 0) == 60);
    CHECK(midi(0, 8) == 64);
    CHECK(midi(1, 0) == 48);
    CHECK(midi(2, 2) == 55);
    for (int s = 0; s < 16; ++s) {
        CAPTURE(s);
        CHECK(bool(pat.lane(0).tie[size_t(s)]) == (s <= 3 || s == 8));
    }
    CHECK(pat.lane(0).gat[8] == 50);
    CHECK(pat.lane(2).gat[2] == 33);  // GAT 25 % of 6 ticks: the release at tick 2
    // The level that VOL 8 plays, read back as the VOL that plays it.
    CHECK(plugin::attenuatedVolume(15, pat.lane(1).vol[0] / 15.0) == plugin::attenuatedVolume(15, 8 / 15.0));
    CHECK(pat.lane(0).vol[0] == 15);
}

TEST_CASE("YM-Tracker export: an FM Voice note plays as the note, its steps named as left out",
          "[ymt-export][fm]") {
    Project p = project();
    p.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(int(Generator::Fm));
    note(p, 0, 0, 0, 60).gat[0] = 100;
    const plugin::YmtExport x = exported(p);
    CHECK(at(x, 0, 0, 0).note == ymtNote(60));
    CHECK(x.notes == 1);
    CHECK(std::find(x.remarks.begin(), x.remarks.end(), "Left out: FM Voice steps (the note plays alone) (1).") !=
          x.remarks.end());
}
