// STYM2149 — plugincore: import of MOD modules, the way of the XM import: notes from the Amiga periods, the
// Amiga pan of the channels, DMA samples from the PAL rate, the MOD fine tune, DMA tracks of the mode, alert.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "ModBuild.h"
#include "plugincore/XmImport.h"

using namespace stym;
using plugin::importMod;
using plugin::NonParamState;
using plugin::XmImport;
using test::ModCell;
using test::ModSpec;
using ymengine::SeqCmd;
using ymengine::VoiceMode;

namespace {

ModCell& at(ModSpec& m, int pattern, int row, int channel) {
    return m.patterns[size_t(pattern)][size_t(row * m.channels + channel)];
}

ModSpec song() {
    ModSpec m;
    m.title = "amiga";
    m.order = {0};
    m.patterns = {test::modPattern(4)};
    at(m, 0, 0, 0) = {428, 1, 0, 0};        // C-2: C4
    at(m, 0, 1, 1) = {856, 1, 0xC, 0x20};   // C-1: C3, volume 32
    at(m, 0, 2, 2) = {428, 1, 0xE, 0x5F};   // fine tune -1 (1/8 semitone down)
    at(m, 0, 3, 3) = {214, 1, 0x0, 0x00};   // C-3: C5
    at(m, 0, 4, 1) = {0, 1, 0, 0};          // the instrument again: volume back to 48, pan kept
    at(m, 0, 7, 0) = {0, 0, 0xD, 0x00};     // pattern break: 8 rows
    m.samples = {{"lead", test::modSquare(1000), 0, 48, 0, 500}};
    return m;
}

XmImport run(const ModSpec& m, VoiceMode mode, NonParamState& st) {
    plugin::XmImportResult r = importMod(test::buildMod(m), mode, st);
    INFO(r.error);
    REQUIRE(r.import);
    return *r.import;
}

}  // namespace

TEST_CASE("MOD import: notes, Amiga pan, PAL rate, fine tune, alert", "[xmimport][mod]") {
    NonParamState st;
    const XmImport r = run(song(), VoiceMode::Studio16, st);
    CHECK(r.format == plugin::ModuleFormat::Mod);
    CHECK(r.title == "amiga");
    CHECK(r.channels == 4);
    CHECK(r.patterns == 1);
    CHECK(r.instruments == 1);
    CHECK(r.ticks);
    CHECK(r.vblRate == 0);  // 125 BPM: 50 Hz
    CHECK(r.notes.empty());

    const ymseq::Pattern& p = st.seq.patterns[0];
    CHECK(p.steps == 8);
    const ymseq::Lane& a = p.lane(4);  // tracks 5 to 8: 8 channels or fewer
    CHECK(a.pat[0] == 0);
    CHECK(a.oct[0] == 4);  // C4, the track at octave 3
    CHECK(a.vol[0] == 11);  // 48 × 15 / 64
    CHECK(a.pnr[0] == 1);   // channel 1: hard left
    const ymseq::Lane& b = p.lane(5);
    CHECK(b.pat[1] == 0);
    CHECK(b.oct[1] == 0);  // C3: the octave of the track
    CHECK(b.vol[1] == 8);  // C20
    CHECK(b.pnr[1] == 201);  // channel 2: hard right
    CHECK(b.vol[4] == 11);   // the instrument alone sets its sample's volume again
    CHECK(b.pnr[4] == 201);  // and leaves the pan of the channel (a MOD sample has none)
    CHECK(p.lane(6).pnr[2] == 201);
    CHECK(p.lane(7).pnr[3] == 1);
    CHECK(p.lane(7).oct[3] == 5);
    // E5F: -1/8 semitone from the sample's tune, -12.5 cents.
    CHECK(p.lane(6).cmdAt(SeqCmd::Fine, 2) == ymengine::seqCmd(SeqCmd::Fine, 100 - 13));

    // The DMA slot: 1000 bytes at the PAL rate of C-2 (3546895 / 428 = 8287 Hz), to the nearest STE rate (6258 Hz),
    // whole loop.
    CHECK(st.instruments[0].name == "lead");
    CHECK(st.instruments[0].patch.generator == ymengine::Generator::Dma);
    CHECK(st.dma.slots[0] == ymengine::DmaSlot{0, false, 60, true});
    CHECK(st.dmaSamples[0].size() == 755);

    const std::vector<std::string> msg = plugin::xmImportMessage(r, "a.mod");
    REQUIRE(msg.size() == 6);
    CHECK(msg[0] == "MOD imported");
    CHECK(msg[2] == "a.mod");
    CHECK(msg[3] == "amiga");
    CHECK(msg[4] == "4 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.");
    CHECK(msg[5] == "Ticks: speed 6 at 50 Hz, STE machine.");
}

TEST_CASE("MOD import: DMA tracks of Authentic 3ch, refusals", "[xmimport][mod]") {
    NonParamState st;
    const XmImport r = run(song(), VoiceMode::Authentic3, st);
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(r.on[size_t(t)] == (t >= 4 && t < 8));
    }
    CHECK(st.seq.patterns[0].lane(4).ins[0] == 1);

    NonParamState none;
    CHECK(importMod(std::vector<uint8_t>(64, 1), VoiceMode::Studio16, none).error == "Not a MOD module.");
    ModSpec silent = song();
    silent.samples.clear();
    CHECK(importMod(test::buildMod(silent), VoiceMode::Studio16, none).error ==
          "Nothing to import: no notes with a sample.");
    CHECK_FALSE(none.instruments[0].used);
}
