// STYM2149 — File menu, Import > Tracker song of an XM module, a MOD module, an S3M
// module or an OKT module: dialogue, format from the content, STE machine, tracks, instruments and DMA
// slots, timing, alert, one undo step for the whole import, refusal without change.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "../unit/ModBuild.h"
#include "../unit/OktBuild.h"
#include "../unit/S3mBuild.h"
#include "../unit/XmBuild.h"
#include "gemui/Alert.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

#include "FileHost.h"

using namespace stym;
using namespace stym::testing;
using pluginui::MainWindow;
using plugin::Global;
using plugin::paramIndex;
using plugin::Track;

namespace {

// Two channels, one sampled instrument looping, at the given tempo.
std::string smallXm(int bpm) {
    ymio::XmModule m;
    m.name = "xm test";
    m.channels = 2;
    m.bpm = bpm;
    m.order = {0};
    ymio::XmPattern p;
    p.rows = 8;
    p.cells.assign(16, ymio::XmCell{});
    p.cells[0] = {49, 1, 0, 0, 0};
    p.cells[2 * 2 + 1] = {53, 1, 0, 0, 0};  // line 2, channel 2
    m.patterns = {p};
    ymio::XmInstrument ins;
    ins.name = "lead";
    ymio::XmSample s;
    s.loopType = 1;
    s.loopLength = 400;
    for (int i = 0; i < 400; ++i) s.data.push_back(int16_t((i / 10 % 2 ? 50 : -50) * 256));
    ins.samples = {s};
    m.instruments = {ins};
    const std::vector<uint8_t> b = test::buildXm(m);
    return {b.begin(), b.end()};
}

// `channels` channels, the first ten playing a note each.
std::string wideXm(int channels, int playing = 10) {
    ymio::XmModule m;
    m.name = "wide";
    m.channels = channels;
    m.order = {0};
    ymio::XmPattern p;
    p.rows = 4;
    p.cells.assign(size_t(4 * channels), ymio::XmCell{});
    for (int c = 0; c < std::min(channels, playing); ++c) p.cells[size_t(c)] = {49, 1, 0, 0, 0};
    m.patterns = {p};
    ymio::XmInstrument ins;
    ins.name = "lead";
    ymio::XmSample s;
    for (int i = 0; i < 400; ++i) s.data.push_back(int16_t((i / 10 % 2 ? 50 : -50) * 256));
    ins.samples = {s};
    m.instruments = {ins};
    const std::vector<uint8_t> b = test::buildXm(m);
    return {b.begin(), b.end()};
}

}  // namespace

TEST_CASE("Import > Tracker song, XM module: dialogue, STE, DMA tracks, instruments, timing, alert, one undo step",
          "[ui][xm]") {
    FileHost h;
    h.disk["/x/song.xm"] = smallXm(125);
    h.disk["/x/bad.xm"] = "Extended Module: cut short";
    h.disk["/x/text.xm"] = "not a module";
    // Before the import: ST machine, an instrument and a DMA sample, a timing, tracks with settings.
    h.state.instruments[0].used = true;
    h.state.instruments[0].name = "mine";
    h.state.dma.slots[0].rate = 3;
    h.state.dmaSamples[0] = {1, 2, 3};
    h.state.global.speed = 4;
    h.state.seq.song.entries = {{0, 2, 0}};
    h.p(Global::VblRate) = 2;
    h.p(5, Track::On) = 1;
    const plugin::NonParamState before = h.state;
    const plugin::ParamValues valuesBefore = h.values;
    MainWindow w(h);
    w.tick(0);

    fileMenu(w, "Import", "Tracker song");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Import a tracker song");
    CHECK(h.opens[0].patterns ==
          "*.xm;*.mod;*.s3m;*.okt;*.mys;*.myv;*.ymt;*.tpm;*.XM;*.MOD;*.S3M;*.OKT;*.MYS;*.MYV;*.YMT;*.TPM");
    h.pick("/x/song.xm");
    CHECK(h.editor.trackerFile == "/x/song.xm");
    CHECK(alertText(w) == "XM imported\n\nsong.xm\nxm test\n2 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.\n"
                          "Ticks: speed 6 at 50 Hz, STE machine.");
    dismiss(w);

    CHECK(h.p(Global::Computer) == 1.0f);
    CHECK(h.p(Global::VblRate) == 0.0f);
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(h.p(t, Track::On) == (t == 4 || t == 5 ? 1.0f : 0.0f));  // tracks 5 and 6
    }
    CHECK(h.p(4, Track::Generator) == float(int(ymengine::Generator::Dma)));
    CHECK(h.state.instruments[0].name == "lead");
    CHECK(h.state.instruments[0].patch.generator == ymengine::Generator::Dma);
    CHECK(h.state.dma.slots[0] == ymengine::DmaSlot{0, false, 60, true});
    CHECK(h.state.dmaSamples[0].size() == 299);  // 400 frames from 8363 Hz to 6258 Hz
    CHECK(h.state.global.timing == ymengine::SeqTiming::Ticks);
    CHECK(h.state.global.speed == 6);
    CHECK(h.state.seq.patterns[0].lane(4).ins[0] == 1);
    CHECK(h.state.seq.patterns[0].lane(5).pat[2] == 4);
    CHECK(h.state.seq.song.size() == 1);
    CHECK(h.editor.undo.undoSize() == 1);

    // A single undo step: everything comes back together; then Redo.
    const plugin::NonParamState after = h.state;
    w.tick(10);
    undoKey(w);
    CHECK(h.state.seq.patterns[0].sameContent(before.seq.patterns[0]));
    CHECK(h.state.seq.song == before.seq.song);
    CHECK(h.state.instruments == before.instruments);
    CHECK(h.state.dma == before.dma);
    CHECK(h.state.dmaSamples == before.dmaSamples);
    CHECK(h.state.global.timing == before.global.timing);
    CHECK(h.state.global.speed == 4);
    for (int i = 0; i < plugin::kParamCount; ++i) {
        CAPTURE(i);
        CHECK(h.values[size_t(i)] == valuesBefore[size_t(i)]);
    }
    undoKey(w, true);
    CHECK(h.state.seq.patterns[0].sameContent(after.seq.patterns[0]));
    CHECK(h.state.instruments == after.instruments);
    CHECK(h.state.dmaSamples == after.dmaSamples);
    CHECK(h.state.global.speed == 6);
    CHECK(h.p(Global::Computer) == 1.0f);

    // Refusal: alert with the reason, nothing changes, no undo step; unreadable file.
    const size_t steps = h.editor.undo.undoSize();
    fileMenu(w, "Import", "Tracker song");
    CHECK(h.opens.back().path == "/x/song.xm");
    h.pick("/x/bad.xm");
    CHECK(alertText(w) == "XM not imported\n\nbad.xm\nNot an XM module.");
    dismiss(w);
    CHECK(h.state.instruments == after.instruments);
    CHECK(h.editor.undo.undoSize() == steps);
    // A file of no tracker format.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/text.xm");
    CHECK(alertText(w) == "Tracker song not imported\n\ntext.xm\nExpected an XM, MOD, S3M, OKT, maxYMiser,\n"
                          "YM-Tracker or TTrak file.");
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == steps);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/missing.xm");
    CHECK(alertText(w) == "The file could not be read.");
    dismiss(w);
}

TEST_CASE("Import > Tracker song, XM module: DMA tracks in Authentic 3ch, free tempo in BPM timing", "[ui][xm]") {
    FileHost h;
    h.disk["/x/a.xm"] = smallXm(140);
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch
    for (int t = 0; t < plugin::kTracks; ++t) h.p(t, Track::On) = 1;
    MainWindow w(h);
    w.tick(0);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/a.xm");
    CHECK(alertText(w) == "XM imported\n\na.xm\nxm test\n2 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.\n"
                          "Tempo 140 BPM, STE machine.");
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(h.p(t, Track::On) == (t == 4 || t == 5 ? 1.0f : 0.0f));
        CHECK(h.p(t, Track::Generator) == 0.0f);  // DMA tracks automatically in 3 ch mode
    }
    CHECK(h.p(Global::FreeTempo) == 140.0f);
    CHECK(h.state.global.timing == ymengine::SeqTiming::Bpm);
    CHECK(h.state.seq.patterns[0].lane(4).ins[0] == 1);
}

TEST_CASE("Import > Tracker song, XM module of more than 8 channels: 3ch or 16 ch asked, 16 ch by default",
          "[ui][xm]") {
    FileHost h;
    h.disk["/x/wide.xm"] = wideXm(12);
    h.disk["/x/song.xm"] = smallXm(125);
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch
    MainWindow w(h);
    w.tick(0);
    const plugin::ParamValues before = h.values;

    // Cancel (Escape): nothing imported.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/wide.xm");
    CHECK(alertText(w) == "The module plays 10 channels.\n16 ch plays up to 16 of them on DMA tracks;\n"
                          "Authentic 3ch plays 8 and leaves the others out.");
    const gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    REQUIRE(a->buttonCount() == 3);
    CHECK(a->button(0).text() == "Cancel");
    CHECK(a->button(1).text() == "3ch");
    CHECK(a->button(2).text() == "16 ch");
    pressKey(w, gemui::Key::Escape);
    CHECK(alertText(w).empty());
    CHECK(h.values == before);
    CHECK(h.editor.undo.undoSize() == 0);

    // 3ch: the mode stays, eight DMA tracks.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/wide.xm");
    alertButton(w, 1);
    CHECK(alertText(w).find("8 tracks") != std::string::npos);
    CHECK(alertText(w).find("Channels beyond the 8 DMA tracks: 2 left out.") != std::string::npos);
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 1.0f);
    for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == (t >= 4 && t < 12 ? 1.0f : 0.0f));
    REQUIRE(undoKey(w));
    CHECK(h.values == before);

    // 16 ch, the default (Enter): ten DMA tracks; one undo step gives Authentic 3ch back.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/wide.xm");
    dismiss(w);
    CHECK(alertText(w).find("10 tracks") != std::string::npos);
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 0.0f);
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(h.p(t, Track::On) == (t < 10 ? 1.0f : 0.0f));
        if (t < 10) CHECK(h.p(t, Track::Generator) == float(int(ymengine::Generator::Dma)));
    }
    CHECK(h.state.seq.patterns[0].lane(9).pat[0] >= 0);
    REQUIRE(undoKey(w));
    CHECK(h.p(Global::VoiceMode) == 1.0f);

    // Eight channels or fewer: no question.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/song.xm");
    CHECK(alertText(w).rfind("XM imported", 0) == 0);
    dismiss(w);

    // File > Play: the copy goes to 16 ch without asking, the project stays in Authentic 3ch.
    fileMenuPath(w, {"Play..."});
    h.pick("/x/wide.xm");
    CHECK(alertText(w).empty());
    REQUIRE(h.played.size() == 1);
    CHECK(h.played[0].values[size_t(paramIndex(Global::VoiceMode))] == 0.0f);
    CHECK(h.played[0].values[size_t(paramIndex(9, Track::On))] == 1.0f);
    CHECK(h.p(Global::VoiceMode) == 1.0f);
}

TEST_CASE("Import > Tracker song, XM module of more than 16 channels: 16 ch or 32 ch asked, 32 ch by default",
          "[ui][xm]") {
    FileHost h;
    h.disk["/x/wider.xm"] = wideXm(20, 20);
    MainWindow w(h);
    w.tick(0);
    REQUIRE(h.p(Global::VoiceMode) == 0.0f);  // 16 ch

    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/wider.xm");
    CHECK(alertText(w) == "The module plays 20 channels.\n32 ch plays up to 32 of them on DMA tracks;\n"
                          "16 ch plays 16 and leaves the others out.");
    const gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    REQUIRE(a->buttonCount() == 3);
    CHECK(a->button(1).text() == "16 ch");
    CHECK(a->button(2).text() == "32 ch");
    // 16 ch: the mode stays, 16 tracks.
    alertButton(w, 1);
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 0.0f);
    for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == (t < 16 ? 1.0f : 0.0f));
    REQUIRE(undoKey(w));

    // 32 ch, the default (Enter): twenty tracks.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/wider.xm");
    dismiss(w);
    CHECK(alertText(w).find("20 tracks") != std::string::npos);
    dismiss(w);
    CHECK(h.p(Global::VoiceMode) == 3.0f);
    for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == (t < 20 ? 1.0f : 0.0f));

    // In 32 ch, no question.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/x/wider.xm");
    CHECK(alertText(w).rfind("XM imported", 0) == 0);
    dismiss(w);

    // File > Play from 16 ch: the copy goes to 32 ch without asking.
    h.p(Global::VoiceMode) = 0;
    w.tick(0);
    fileMenuPath(w, {"Play..."});
    h.pick("/x/wider.xm");
    CHECK(alertText(w).empty());
    REQUIRE(h.played.size() == 1);
    CHECK(h.played[0].values[size_t(paramIndex(Global::VoiceMode))] == 3.0f);
    CHECK(h.p(Global::VoiceMode) == 0.0f);
}

TEST_CASE("Import > Tracker song, MOD module: Amiga pan, STE DMA, alert, one undo step, refusal",
          "[ui][xm][mod]") {
    test::ModSpec m;
    m.title = "amiga";
    m.patterns = {test::modPattern(4)};
    m.patterns[0][0] = {428, 1, 0, 0};          // row 0, channel 1: C4
    m.patterns[0][1 * 4 + 1] = {214, 1, 0, 0};  // row 1, channel 2: C5
    m.samples = {{"lead", test::modSquare(400), 0, 64, 0, 200}};
    const std::vector<uint8_t> b = test::buildMod(m);
    FileHost h;
    h.disk["/m/amiga.mod"] = std::string(b.begin(), b.end());
    h.disk["/m/bad.mod"] = "not a mod module";
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch: DMA tracks 5 to 12
    MainWindow w(h);
    w.tick(0);

    fileMenu(w, "Import", "Tracker song");
    REQUIRE(h.opens.size() == 1);
    h.pick("/m/amiga.mod");
    CHECK(h.editor.trackerFile == "/m/amiga.mod");
    CHECK(alertText(w) == "MOD imported\n\namiga.mod\namiga\n2 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.\n"
                          "Ticks: speed 6 at 50 Hz, STE machine.");
    dismiss(w);
    CHECK(h.p(Global::Computer) == 1.0f);
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(h.p(t, Track::On) == (t == 4 || t == 5 ? 1.0f : 0.0f));
    }
    CHECK(h.state.seq.patterns[0].lane(4).pnr[0] == 1);    // channel 1: left
    CHECK(h.state.seq.patterns[0].lane(5).pnr[1] == 201);  // channel 2: right
    CHECK(h.state.dmaNames[0] == "lead");
    CHECK(h.editor.undo.undoSize() == 1);

    const size_t steps = h.editor.undo.undoSize();
    fileMenu(w, "Import", "Tracker song");
    CHECK(h.opens.back().path == "/m/amiga.mod");
    h.pick("/m/bad.mod");  // no signature and no MOD that reads: of no format
    CHECK(alertText(w).rfind("Tracker song not imported\n\nbad.mod\n", 0) == 0);
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == steps);
}

TEST_CASE("File > Play... of an XM or a MOD module: a converted copy played apart, the project untouched",
          "[ui][xm][mod]") {
    test::ModSpec m;
    m.title = "amiga";
    m.patterns = {test::modPattern(4)};
    m.patterns[0][0] = {428, 1, 0, 0};
    m.samples = {{"lead", test::modSquare(400), 0, 64, 0, 200}};
    const std::vector<uint8_t> mod = test::buildMod(m);
    FileHost h;
    h.disk["/x/song.xm"] = smallXm(125);
    h.disk["/m/amiga.mod"] = std::string(mod.begin(), mod.end());
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch
    h.state.instruments[0].used = true;
    h.state.instruments[0].name = "mine";
    const plugin::NonParamState before = h.state;
    const plugin::ParamValues valuesBefore = h.values;
    MainWindow w(h);
    w.tick(0);

    fileMenuPath(w, {"Play..."});
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Play a file");
    h.pick("/x/song.xm");
    CHECK(alertText(w).empty());
    REQUIRE(h.played.size() == 1);
    const FileHost::Played& xm = h.played[0];
    CHECK(xm.name == "song.xm  -  XM");
    CHECK(xm.song);  // the module's order list
    CHECK(xm.values[size_t(paramIndex(Global::Computer))] == 1.0f);  // STE machine, as the import
    CHECK(xm.values[size_t(paramIndex(Global::VoiceMode))] == 1.0f);
    CHECK(xm.values[size_t(paramIndex(4, Track::On))] == 1.0f);
    CHECK(xm.extras.instruments[0].name == "lead");
    CHECK(xm.extras.seq.patterns[0].lane(4).ins[0] == 1);
    CHECK(h.ym.loaded);
    CHECK(w.ymPlayerWindow().isOpen());

    // MOD: no signature at its start, recognised after the YM Player refused it.
    fileMenuPath(w, {"Play..."});
    CHECK(h.opens.back().path == "/x/song.xm");
    h.pick("/m/amiga.mod");
    REQUIRE(h.played.size() == 2);
    CHECK(h.played[1].name == "amiga.mod  -  MOD");
    CHECK(h.played[1].extras.dmaNames[0] == "lead");
    CHECK(h.ymLoads.empty());  // refused by the YM Player first

    // Nothing of the project changed, no undo step, no import path remembered.
    CHECK(h.state.instruments == before.instruments);
    CHECK(h.state.seq.patterns[0].sameContent(before.seq.patterns[0]));
    CHECK(h.values == valuesBefore);
    CHECK(h.editor.undo.undoSize() == 0);
    CHECK(h.editor.trackerFile.empty());
    CHECK(h.editor.playFile == "/m/amiga.mod");
}

TEST_CASE("Import > Tracker song and File > Play of S3M and OKT modules, told by their content",
          "[ui][xm][s3m][okt]") {
    test::S3mSpec s;
    s.title = "scream";
    s.channels = {0, 8};
    s.patterns = {{test::s3mNote(0, 0, 4, 0, 1), test::s3mNote(1, 1, 5, 0, 1)}};
    s.samples = {{"lead", test::s3mSquare(400), 64, 8363, true, 0, 400}};
    const std::vector<uint8_t> s3m = test::buildS3m(s);
    test::OktSpec o;
    o.patterns = {test::oktPattern(16, 4)};
    o.patterns[0].cells[0] = {13, 0};
    o.samples = {{"bass", test::oktSquare(400), 64, 0, 200}};
    const std::vector<uint8_t> okt = test::buildOkt(o);
    FileHost h;
    h.disk["/t/song.s3m"] = std::string(s3m.begin(), s3m.end());
    h.disk["/t/cut.s3m"] = std::string(s3m.begin(), s3m.begin() + 0x61);
    h.disk["/t/song.okt"] = std::string(okt.begin(), okt.end());
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch: DMA tracks 5 to 12
    MainWindow w(h);
    w.tick(0);

    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/song.s3m");
    CHECK(alertText(w) == "S3M imported\n\nsong.s3m\nscream\n2 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.\n"
                          "Ticks: speed 6 at 50 Hz, STE machine.");
    dismiss(w);
    CHECK(h.state.dmaNames[0] == "lead");
    CHECK(h.state.seq.patterns[0].lane(5).oct[1] == 5);
    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/song.okt");
    CHECK(alertText(w) == "OKT imported\n\nsong.okt\n1 track, 1 pattern, 1 DMA instrument, 1 Song entry.\n"
                          "Ticks: speed 6 at 50 Hz, STE machine.");
    dismiss(w);
    CHECK(h.state.dmaNames[0] == "bass");
    CHECK(h.editor.undo.undoSize() == 2);
    // A cut S3M: its signature tells the format, the reader refuses it.
    fileMenu(w, "Import", "Tracker song");
    h.pick("/t/cut.s3m");
    CHECK(alertText(w) == "S3M not imported\n\ncut.s3m\nCorrupt S3M module.");
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == 2);

    fileMenuPath(w, {"Play..."});
    CHECK(h.opens.back().patterns.find("*.s3m;*.okt") != std::string::npos);
    h.pick("/t/song.s3m");
    fileMenuPath(w, {"Play..."});
    h.pick("/t/song.okt");
    REQUIRE(h.played.size() == 2);
    CHECK(h.played[0].name == "song.s3m  -  S3M");
    CHECK(h.played[1].name == "song.okt  -  OKT");
    CHECK(h.played[1].extras.dmaNames[0] == "bass");
}
