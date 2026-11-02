// STYM2149 — plugincore: import of YM-Tracker songs as editable patterns: notes, note offs and Kxx, the volume
// of each row, the instruments that the effects make, Ticks or BPM timing, the Song, digidrums on the Digidrum track,
// refused songs and the notes played back by the engine.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "plugincore/YmtImport.h"
#include "ymengine/Pitch.h"
#include "ymio/YmTracker.h"

using namespace stym;
using plugin::NonParamState;
using ymengine::Generator;
using ymengine::SeqCmd;
using ymengine::seqCmd;
using ymio::YmtCell;
using ymio::YmtSong;

namespace {

constexpr uint8_t kOff = ymio::kYmtNoteOff;

// A song of one chip (three tone channels and the digidrum channel), one pattern of `rows` rows per entry of `order`,
// the instrument "Lead" (1): a square, ADSR without release.
YmtSong blank(int patterns, int rows) {
    YmtSong s;
    s.title = "Test";
    s.channelNames = {"Ch 1", "Ch 2", "Ch 3", "Ch 4"};
    s.order = {0};
    s.patterns.resize(size_t(patterns));
    for (auto& p : s.patterns) {
        p.rows = rows;
        p.cells.assign(size_t(rows * 4), YmtCell{});
    }
    s.instruments.resize(2);
    s.instruments[0].name = "--";
    s.instruments[1].name = "Lead";
    s.instruments[1].release = 0;
    return s;
}

YmtCell& at(YmtSong& s, int p, int row, int c) { return s.patterns[size_t(p)].cells[size_t(row * 4 + c)]; }

struct Imported {
    plugin::RegisterImport import;
    NonParamState state;
};

Imported import(const YmtSong& s) {
    Imported i;
    const plugin::RegisterImportResult r = plugin::importYmt(s, i.state);
    INFO(r.error);
    REQUIRE(r.import);
    i.import = *r.import;
    return i;
}

int note(const Imported& i, int track, int step) {
    return ymseq::absoluteNote(i.state.seq.patterns[0].lane(track), 1, step, i.state.seq.tracks[size_t(track)].octave);
}

// Notes on channels 1 and 2: C-5 cut by a note off at row 4, E-5 released at tick 2 of row 10 by K02; C-4 at volume
// $30 with a volume slide A04 on row 1 and C20 on row 3.
YmtSong notesSong() {
    YmtSong s = blank(1, 16);
    at(s, 0, 0, 0) = {37, 1, 0x50, 0, 0};
    at(s, 0, 4, 0) = {kOff, 0, 0, 0, 0};
    at(s, 0, 8, 0) = {41, 1, 0, 0, 0};
    at(s, 0, 10, 0) = {0, 0, 0, 'K', 2};
    at(s, 0, 0, 1) = {25, 1, 0x30, 0, 0};
    at(s, 0, 1, 1) = {0, 0, 0, 'A', 0x04};
    at(s, 0, 3, 1) = {0, 0, 0, 'C', 0x20};
    return s;
}

}  // namespace

TEST_CASE("YM-Tracker import: notes, note offs, Kxx and the volume of each row", "[ymt-import]") {
    const Imported i = import(notesSong());
    const plugin::RegisterImport& r = i.import;
    CHECK(r.patterns == 1);
    CHECK(r.entries == 1);
    CHECK(r.notes == 3);
    CHECK_FALSE(r.digidrums);
    // 125 BPM: a tick at 50 Hz, Ticks timing at the song's speed.
    CHECK(i.state.global.timing == ymengine::SeqTiming::Ticks);
    CHECK(r.speed == 6);
    CHECK(i.state.global.speed == 6);
    CHECK(plugin::kVblRates[size_t(r.vblRate)] == 50);
    CHECK(r.tempo == 0.0f);
    // Channel 1: C4 held over rows 0 to 3 (the note off at row 4), E4 from row 8 to its K02 at row 10 (GAT 2/6).
    const ymseq::Lane& a = i.state.seq.patterns[0].lane(0);
    CHECK(i.state.seq.patterns[0].steps == 16);
    CHECK(note(i, 0, 0) == 60);
    CHECK(note(i, 0, 8) == 64);
    for (int s = 0; s < 16; ++s) {
        CAPTURE(s);
        CHECK((a.pat[size_t(s)] >= 0) == (s == 0 || s == 8));
        CHECK(bool(a.tie[size_t(s)]) == (s <= 3 || (s >= 8 && s <= 10)));
        CHECK(a.gat[size_t(s)] == (s == 10 ? 33 : 0));
    }
    CHECK(a.ins[0] >= 1);
    // Channel 2: C3 at the 4-bit volume 8 ($30: 32 of 64), which VOL 1 plays; A04 on row 1 as a volume slide, stopped
    // on row 2, which VOL leaves to the engine; C20 on row 3 gives 32 of 64 over the slide played since the attack.
    const ymseq::Lane& b = i.state.seq.patterns[0].lane(1);
    CHECK(note(i, 1, 0) == 48);
    CHECK(plugin::attenuatedVolume(15, 1 / 15.0) == 8);
    CHECK(b.vol[0] == 1);
    CHECK(b.vol[1] == 1);
    CHECK(b.vol[2] == 1);
    CHECK(b.cmdAt(SeqCmd::VolSlide, 1) == seqCmd(SeqCmd::VolSlide, 0x04));
    CHECK(b.cmdAt(SeqCmd::VolSlide, 2) == seqCmd(SeqCmd::VolSlide, 0));
    CHECK(b.vol[3] == plugin::volForLevel(12));  // (32 + 4 × 5) of 64: the 4-bit volume 12
    CHECK(plugin::attenuatedVolume(15, b.vol[3] / 15.0) == 12);
    CHECK(b.tie[15]);
}

TEST_CASE("YM-Tracker import: the effects of the notes make instruments", "[ymt-import]") {
    YmtSong s = blank(1, 8);
    at(s, 0, 0, 0) = {37, 1, 0, '0', 0x47};   // arpeggio C, E, G
    at(s, 0, 4, 0) = {37, 1, 0, 0, 0};        // the same instrument without it
    at(s, 0, 0, 1) = {37, 1, 0, '4', 0x84};   // vibrato
    at(s, 0, 0, 2) = {37, 1, 0, 'X', 0x0A};   // looped triangle at the note
    at(s, 0, 4, 2) = {37, 1, 0, 'Q', 0x34};   // SID
    const Imported i = import(s);
    REQUIRE(i.import.instruments == 5);
    auto patchOf = [&](int track, int step) -> const ymengine::TrackPatch& {
        const int ins = i.state.seq.patterns[0].lane(track).ins[size_t(step)];
        REQUIRE(ins >= 1);
        return i.state.instruments[size_t(ins - 1)].patch;
    };
    const ymengine::TrackPatch& arp = patchOf(0, 0);
    REQUIRE(arp.ornamentOn);
    CHECK(arp.ornamentLength == 3);
    CHECK(arp.ornamentLoop);
    CHECK(arp.ornamentPitch[0] == 0);
    CHECK(arp.ornamentPitch[1] == 4);
    CHECK(arp.ornamentPitch[2] == 7);
    CHECK_FALSE(patchOf(0, 4).ornamentOn);
    CHECK(patchOf(0, 4).generator == Generator::Chip);
    const ymengine::TrackPatch& vib = patchOf(1, 0);
    CHECK(vib.vibratoOn);
    CHECK(vib.vibratoHz == Catch::Approx(50.0 * 8 / 64));
    CHECK(vib.vibratoCents == Catch::Approx(50));
    const ymengine::TrackPatch& buzz = patchOf(2, 0);
    CHECK(buzz.generator == Generator::SyncBuzzer);
    CHECK(buzz.envelopeVolume);
    CHECK(buzz.envTuned);
    CHECK(buzz.envShape == 10);
    CHECK(buzz.envRatioN == 1);
    CHECK(buzz.envRatioD == 2);
    const ymengine::TrackPatch& sid = patchOf(2, 4);
    CHECK(sid.generator == Generator::Sid);
    CHECK(sid.sidRatio == Catch::Approx(20 * std::pow(2.0, 3 + 4 / 16.0) / ymengine::noteHz(60)).epsilon(1e-6));
    CHECK(i.import.remarks.empty());
}

TEST_CASE("YM-Tracker import: a fixed envelope period and an envelope that starts on a later row",
          "[ymt-import]") {
    YmtSong s = blank(1, 8);
    s.instruments[1].autoPeriod = false;
    s.instruments[1].envPeriod = 300;
    at(s, 0, 0, 0) = {37, 1, 0, 'X', 0x00};  // decay at the period of the instrument
    at(s, 0, 1, 0) = {0, 0, 0, 'Y', 0x02};   // its high byte
    at(s, 0, 0, 1) = {37, 1, 0, 0, 0};
    at(s, 0, 2, 1) = {0, 0, 0, 'X', 0x0D};   // the envelope from row 2
    const Imported i = import(s);
    const ymseq::Lane& a = i.state.seq.patterns[0].lane(0);
    const ymengine::TrackPatch& fixed = i.state.instruments[size_t(a.ins[0] - 1)].patch;
    CHECK(fixed.envelopeVolume);
    CHECK(fixed.envShape == 9);
    CHECK(fixed.envPeriod == (300 & 0xFF) + (2 << 8));
    CHECK_FALSE(fixed.envTuned);
    // Channel 2: a Chip Voice without the envelope, which SRC switches on from row 2 with the shape of the Xxx.
    const ymseq::Lane& b = i.state.seq.patterns[0].lane(1);
    const ymengine::TrackPatch& late = i.state.instruments[size_t(b.ins[0] - 1)].patch;
    CHECK_FALSE(late.envelopeVolume);
    CHECK(late.envShape == 13);
    CHECK(b.mix[0] == 0);
    CHECK(b.mix[1] == 0);
    CHECK(b.mix[2] == ymengine::kSeqMixEnv + 1);
    CHECK(b.mix[7] == ymengine::kSeqMixEnv + 1);
    REQUIRE_FALSE(i.import.remarks.empty());  // the Yxx after the attack, played from it
}

TEST_CASE("YM-Tracker import: BPM timing and the Song of the order table", "[ymt-import]") {
    YmtSong s = blank(2, 4);
    s.bpm = 140;
    s.speed = 4;
    s.order = {0, 1, 0};
    s.restart = 1;
    at(s, 0, 0, 0) = {37, 1, 0, 0, 0};
    at(s, 1, 0, 0) = {44, 1, 0, 0, 0};
    const Imported i = import(s);
    // 140 BPM: a tick at 56 Hz, no VBL rate: BPM timing at 24 × 140 / (4 ticks × 4 steps per beat) = 210 BPM.
    CHECK(i.state.global.timing == ymengine::SeqTiming::Bpm);
    CHECK(i.import.tempo == Catch::Approx(210));
    CHECK(plugin::kVblRates[size_t(i.import.vblRate)] == 60);
    CHECK(i.import.patterns == 2);
    const auto& song = i.state.seq.song.entries;
    REQUIRE(song.size() == 3);
    CHECK(song[0].pat == 0);
    CHECK(song[1].pat == 1);
    CHECK(song[2].pat == 0);
    CHECK_FALSE(song[0].restart);
    CHECK(song[1].restart);
    const std::vector<std::string> lines = plugin::ymtImportMessage(i.import, "test.ymt");
    REQUIRE(lines.size() >= 6);
    CHECK(lines[0] == "YM-Tracker song imported");
    CHECK(lines[5] == "Tempo 210 BPM, replay at 60 Hz, Authentic 3ch.");
}

TEST_CASE("YM-Tracker import: Dxx ends a pattern, Bxx jumps back to the restart point, Fxx sets the speed",
          "[ymt-import]") {
    YmtSong s = blank(2, 16);
    s.order = {0, 1};
    at(s, 0, 0, 0) = {37, 1, 0, 0, 0};
    at(s, 0, 3, 1) = {0, 0, 0, 'D', 0};
    at(s, 1, 0, 0) = {41, 1, 0, 'F', 3};
    at(s, 1, 5, 2) = {0, 0, 0, 'B', 0};
    const Imported i = import(s);
    CHECK(i.state.seq.patterns[0].steps == 4);
    CHECK(i.state.seq.patterns[1].steps == 6);
    const auto& song = i.state.seq.song.entries;
    REQUIRE(song.size() == 2);
    CHECK_FALSE(song[1].restart);  // the Song goes back to its start
    // Ticks timing: S03 on a track from row 0 of the second pattern.
    CHECK(i.state.global.timing == ymengine::SeqTiming::Ticks);
    int speeds = 0;
    for (int t = 0; t < 3; ++t) speeds += i.state.seq.patterns[1].lane(t).cmdAt(SeqCmd::Speed, 0) == seqCmd(SeqCmd::Speed, 3);
    CHECK(speeds == 1);
}

TEST_CASE("YM-Tracker import: digidrums on the Digidrum track at the rates of the file", "[ymt-import]") {
    YmtSong s = blank(1, 16);
    ymio::YmtInstrument& kit = s.instruments.emplace_back();
    kit.name = "Kit";
    kit.tone = false;  // silent: its notes on a tone channel only carry the Zxx
    kit.drums = {{"Kick", 8000, std::vector<uint8_t>(400, 9)}, {"Hat", 30000, std::vector<uint8_t>(100, 20)}};
    at(s, 0, 0, 3) = {1, 2, 0, 0, 0};          // drum channel: sample 1 of the bank
    at(s, 0, 4, 3) = {2, 2, 0x30, 0, 0};       // sample 2, at volume $30
    at(s, 0, 8, 2) = {37, 2, 0, 'Z', 0x02};    // channel 3: sample 2 by its Zxx
    at(s, 0, 0, 0) = {37, 1, 0, 0, 0};
    const Imported i = import(s);
    REQUIRE(i.import.digidrums);
    CHECK(i.import.samples == 2);
    CHECK(i.state.global.digidrumChannel[0] == 2);
    const ymseq::Lane& d = i.state.seq.patterns[0].lane(3);
    for (int st = 0; st < 16; ++st) {
        CAPTURE(st);
        CHECK(d.pat[size_t(st)] == (st == 0 ? 0 : st == 4 || st == 8 ? 1 : -1));
    }
    CHECK(d.vol[4] == 1);  // $30: the 4-bit volume 8
    // The silent note of channel 3 is not played.
    CHECK(i.state.seq.patterns[0].lane(2).pat[8] == -1);
    // The samples as they are (above 15, 15), each slot at the rate of the file on the note of its row.
    REQUIRE(i.state.digidrums[0].size() == 400);
    CHECK(i.state.digidrums[0][0] == 9);
    REQUIRE(i.state.digidrums[1].size() == 100);
    CHECK(i.state.digidrums[1][0] == 15);
    CHECK(i.state.digiSlots[0].name == "Kick");
    const int octave = i.state.seq.tracks[3].octave;
    for (int k = 0; k < 2; ++k) {
        CAPTURE(k);
        const plugin::DigidrumSlotState& slot = i.state.digiSlots[size_t(k)];
        CHECK(slot.play.rateHz >= ymengine::kDigidrumRateMin);
        CHECK(slot.play.rateHz <= ymengine::kDigidrumRateMax);
        const int played = (octave + 1) * 12 + k;
        CHECK(slot.play.rateHz * std::pow(2.0, (played - slot.play.root) / 12.0) ==
              Catch::Approx(k ? 30000 : 8000).epsilon(1e-9));
        CHECK(slot.play.end == i.state.digidrums[size_t(k)].size());
    }
}

TEST_CASE("YM-Tracker import: songs without notes are refused and change nothing", "[ymt-import]") {
    NonParamState state;
    const NonParamState before = state;
    YmtSong s = blank(1, 4);
    at(s, 0, 0, 0) = {kOff, 0, 0, 0, 0};
    plugin::RegisterImportResult r = plugin::importYmt(s, state);
    CHECK_FALSE(r.import);
    CHECK(r.error == "Nothing to import: no notes on the first three channels.");
    s.patterns.clear();
    r = plugin::importYmt(s, state);
    CHECK_FALSE(r.import);
    CHECK(r.error == "Nothing to import: the song has no pattern.");
    CHECK(state.seq.used == before.seq.used);
    CHECK(state.seq.song.entries == before.seq.song.entries);
    CHECK(state.instruments == before.instruments);
}

TEST_CASE("YM-Tracker import: the notes played back on the chip", "[ymt-import]") {
    const Imported i = import(notesSong());
    plugin::ParamValues v = plugin::defaultValues();
    v[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    v[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(i.import.vblRate);
    for (int k = 0; k < plugin::kTracks; ++k) {
        v[size_t(plugin::paramIndex(k, plugin::Track::On))] = k < 3 ? 1.0f : 0.0f;
        if (k >= 3) continue;
        v[size_t(plugin::paramIndex(k, plugin::Track::Generator))] = float(int(Generator::Chip));
        v[size_t(plugin::paramIndex(k, plugin::Track::Level))] = 100;
    }
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, v, i.state);
    REQUIRE(cap);
    const auto& frames = cap->take.frames;
    REQUIRE(frames.size() >= 16 * 6);
    auto heard = [&](int f, int c) {
        const auto& r = frames[size_t(f)].reg;
        return (r[size_t(8 + c)] & 15) > 0 && !(r[7] & (1 << c));
    };
    auto cents = [&](int f, int c, double hz) {
        const auto& r = frames[size_t(f)].reg;
        const int tp = r[size_t(2 * c)] | (r[size_t(2 * c + 1)] & 15) << 8;
        return 1200 * std::log2(2'000'000.0 / 16 / tp / hz);
    };
    // C4 on channel A until the note off of row 4, E4 from row 8; C3 on channel B.
    CHECK(heard(1, 0));
    CHECK(std::abs(cents(1, 0, 261.63)) < 10);
    CHECK(heard(3 * 6 + 5, 0));
    CHECK_FALSE(heard(4 * 6 + 2, 0));
    CHECK(heard(8 * 6 + 1, 0));
    CHECK(std::abs(cents(8 * 6 + 1, 0, 329.63)) < 10);
    CHECK_FALSE(heard(11 * 6 + 1, 0));
    CHECK(heard(1, 1));
    CHECK(std::abs(cents(1, 1, 130.81)) < 10);
}
