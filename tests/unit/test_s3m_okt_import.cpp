// STYM2149 — plugincore: import of S3M and OKT modules, the way of the XM import: the format told by
// the content, notes, pans, DMA samples at the rate of their C2Spd or of the Amiga, tracks of the mode, the
// effects kept out of the XM shape counted under their own names, the AdLib parts of an S3M reported, alerts.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "ModBuild.h"
#include "OktBuild.h"
#include "S3mBuild.h"
#include "XmBuild.h"
#include "plugincore/XmImport.h"

using namespace stym;
using plugin::ModuleFormat;
using plugin::NonParamState;
using plugin::XmImport;
using ymengine::VoiceMode;

namespace {

XmImport run(ModuleFormat f, const std::vector<uint8_t>& b, VoiceMode mode, NonParamState& st) {
    plugin::XmImportResult r = plugin::importModule(f, b, mode, st);
    INFO(r.error);
    REQUIRE(r.import);
    return *r.import;
}

test::S3mSpec s3m() {
    test::S3mSpec m;
    m.title = "scream";
    m.channels = {0, 8, 16};  // L1, R1, AdLib A1
    m.patterns = {{test::s3mNote(0, 0, 4, 0, 1),
                   {0, 0, -1, 0, -1, 8, 0x44},  // Hxx: vibrato, left out
                   test::s3mNote(1, 1, 5, 0, 1),
                   {1, 1, -1, 0, 32},
                   test::s3mNote(2, 2, 4, 0, 2),  // on the AdLib channel
                   {3, 0, -1, 0, -1, 4, 0x3F},  // DxF: a fine slide, left out
                   {7, 0, -1, 0, -1, 3, 0x00}}};  // break: 8 rows
    m.samples.resize(2);
    m.samples[0] = {"lead", test::s3mSquare(1000), 64, 16726, true, 0, 1000};
    m.samples[1].name = "organ";
    m.samples[1].type = 2;
    return m;
}

test::OktSpec okt() {
    test::OktSpec m;
    m.split = {0, 1, 0, 0};  // five channels: L R R R L
    m.patterns = {test::oktPattern(8, 5)};
    m.patterns[0].cells[0] = {13, 0};            // row 0, channel 1: C4
    m.patterns[0].cells[1 * 5 + 1] = {25, 0};    // row 1, channel 2 (split): C5 at full volume
    m.patterns[0].cells[2 * 5 + 2] = {13, 0, 31, 16};
    m.patterns[0].cells[3 * 5 + 0] = {0, 0, 10, 0x37};  // arpeggio: left out
    m.samples = {{"lead", test::oktSquare(1000), 32, 0, 500}};
    return m;
}

}  // namespace

TEST_CASE("Module format told by the content: XM, OKT, S3M signatures, a MOD that reads", "[xmimport][s3m]") {
    auto format = [](const std::vector<uint8_t>& b) { return plugin::moduleFormat(b); };
    ymio::XmModule x;
    x.channels = 1;
    x.order = {0};
    ymio::XmPattern p;
    p.cells.assign(64, ymio::XmCell{});
    x.patterns = {p};
    CHECK(format(test::buildXm(x)) == ModuleFormat::Xm);
    CHECK(format(test::buildS3m(s3m())) == ModuleFormat::S3m);
    CHECK(format(test::buildOkt(okt())) == ModuleFormat::Okt);
    test::ModSpec mod;
    mod.patterns = {test::modPattern(4)};
    CHECK(format(test::buildMod(mod)) == ModuleFormat::Mod);
    CHECK_FALSE(format(std::vector<uint8_t>(3000, 7)));
    CHECK_FALSE(format(std::vector<uint8_t>{}));
    CHECK(std::string(plugin::moduleFormatName(ModuleFormat::S3m)) == "S3M");
    CHECK(std::string(plugin::moduleFormatName(ModuleFormat::Okt)) == "OKT");
}

TEST_CASE("S3M import: notes, ST3 pans, the C2Spd rate, effects left out by name, AdLib reported",
          "[xmimport][s3m]") {
    NonParamState st;
    const XmImport r = run(ModuleFormat::S3m, test::buildS3m(s3m()), VoiceMode::Studio16, st);
    CHECK(r.format == ModuleFormat::S3m);
    CHECK(r.title == "scream");
    CHECK(r.channels == 2);
    CHECK(r.ticks);
    const ymseq::Pattern& p = st.seq.patterns[0];
    CHECK(p.steps == 8);
    const ymseq::Lane& a = p.lane(4);  // tracks 5 and up
    CHECK(a.pat[0] == 0);
    CHECK(a.oct[0] == 4);  // C-4: C4
    CHECK(a.vol[0] == 15);
    CHECK(a.pnr[0] == 41);  // ST3's left (3 of 15): -6
    const ymseq::Lane& b = p.lane(5);
    CHECK(b.oct[1] == 5);  // C-5: C5
    CHECK(b.vol[1] == 8);  // volume column 32
    CHECK(b.pnr[1] == 161);  // ST3's right (12 of 15): +6
    // The sample's C4 at its C2Spd, 16726 Hz: the nearest STE rate is 12517 Hz; the whole loop.
    CHECK(st.instruments[0].name == "lead");
    CHECK(st.dma.slots[0].rate == 1);
    CHECK(st.dma.slots[0].loop);

    const std::vector<std::string> msg = plugin::xmImportMessage(r, "a.s3m");
    REQUIRE(msg.size() >= 10);
    CHECK(msg[0] == "S3M imported");
    CHECK(msg[3] == "scream");
    CHECK(msg[4] == "2 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.");
    CHECK(msg[7] == "Not as in the module:");
    CHECK(msg[8] == "• AdLib channels (no OPL chip): 1 left out.");
    CHECK(msg[9] == "• AdLib instruments (no OPL chip): 1 left out.");
    CHECK(msg.back() == "• Effects left out: DxF (1), Hxx (1).");
}

TEST_CASE("OKT import: split channels, Amiga pans and rate, full volume of split notes, alert",
          "[xmimport][okt]") {
    NonParamState st;
    const XmImport r = run(ModuleFormat::Okt, test::buildOkt(okt()), VoiceMode::Authentic3, st);
    CHECK(r.format == ModuleFormat::Okt);
    CHECK(r.channels == 3);
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(r.on[size_t(t)] == (t >= 4 && t < 7));
    }
    const ymseq::Pattern& p = st.seq.patterns[0];
    CHECK(p.steps == 8);
    CHECK(p.lane(4).oct[0] == 4);
    CHECK(p.lane(4).vol[0] == 8);   // the sample's volume, 32
    CHECK(p.lane(4).pnr[0] == 1);   // Amiga channel 1: hard left
    CHECK(p.lane(5).oct[1] == 5);
    CHECK(p.lane(5).vol[1] == 15);  // a split channel plays at full volume
    CHECK(p.lane(5).pnr[1] == 201);  // Amiga channel 2: right
    CHECK(p.lane(6).vol[2] == 4);   // V16
    CHECK(p.lane(6).pnr[2] == 201);  // its second voice: right too
    CHECK(st.dma.slots[0].rate == 0);  // the PAL rate of C-2, 8287 Hz: 6258 is nearer than 12517
    const std::vector<std::string> msg = plugin::xmImportMessage(r, "a.okt");
    CHECK(msg[0] == "OKT imported");
    CHECK(msg[3] == "3 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.");
    CHECK(msg.back() == "• Effects left out: Axx (1).");

    NonParamState none;
    CHECK(plugin::importModule(ModuleFormat::Okt, std::vector<uint8_t>(16, 0), VoiceMode::Studio16, none).error ==
          "Not an OKT module.");
    CHECK(plugin::importModule(ModuleFormat::S3m, std::vector<uint8_t>(16, 0), VoiceMode::Studio16, none).error ==
          "Not an S3M module.");
}
