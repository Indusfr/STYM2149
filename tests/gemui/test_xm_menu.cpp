// STYM2149 — File menu, Import > XM module (M7, B5, D-166): dialogue, STE machine, tracks, instruments and DMA slots,
// timing, alert, one undo step for the whole import, refusal without change.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

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

}  // namespace

TEST_CASE("Import > XM module: dialogue, STE, DMA tracks, instruments, timing, alert, one undo step", "[ui][xm]") {
    FileHost h;
    h.disk["/x/song.xm"] = smallXm(125);
    h.disk["/x/bad.xm"] = "not an xm module";
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

    fileMenu(w, "Import", "XM module");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Import an XM module");
    CHECK(h.opens[0].patterns == "*.xm");
    h.pick("/x/song.xm");
    CHECK(h.editor.xmFile == "/x/song.xm");
    CHECK(alertText(w) == "XM imported\n\nsong.xm\nxm test\n2 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.\n"
                          "Ticks: speed 6 at 50 Hz, STE machine.");
    dismiss(w);

    CHECK(h.p(Global::Computer) == 1.0f);
    CHECK(h.p(Global::VblRate) == 0.0f);
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(h.p(t, Track::On) == (t < 2 ? 1.0f : 0.0f));
    }
    CHECK(h.p(0, Track::Generator) == float(int(ymengine::Generator::Dma)));
    CHECK(h.state.instruments[0].name == "lead");
    CHECK(h.state.instruments[0].patch.generator == ymengine::Generator::Dma);
    CHECK(h.state.dma.slots[0] == ymengine::DmaSlot{0, false, 60, true});
    CHECK(h.state.dmaSamples[0].size() == 299);  // 400 frames from 8363 Hz to 6258 Hz
    CHECK(h.state.global.timing == ymengine::SeqTiming::Ticks);
    CHECK(h.state.global.speed == 6);
    CHECK(h.state.seq.patterns[0].lane(0).ins[0] == 1);
    CHECK(h.state.seq.patterns[0].lane(1).pat[2] == 4);
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
    fileMenu(w, "Import", "XM module");
    CHECK(h.opens.back().path == "/x/song.xm");
    h.pick("/x/bad.xm");
    CHECK(alertText(w) == "XM not imported\n\nbad.xm\nNot an XM module.");
    dismiss(w);
    CHECK(h.state.instruments == after.instruments);
    CHECK(h.editor.undo.undoSize() == steps);
    fileMenu(w, "Import", "XM module");
    h.pick("/x/missing.xm");
    CHECK(alertText(w) == "The file could not be read.");
    dismiss(w);
}

TEST_CASE("Import > XM module: DMA tracks in Authentic 3ch, free tempo in BPM timing", "[ui][xm]") {
    FileHost h;
    h.disk["/x/a.xm"] = smallXm(140);
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch
    for (int t = 0; t < plugin::kTracks; ++t) h.p(t, Track::On) = 1;
    MainWindow w(h);
    w.tick(0);
    fileMenu(w, "Import", "XM module");
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
