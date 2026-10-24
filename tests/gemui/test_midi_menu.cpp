// STYM2149 — File menu, MIDI files (M7, A3, D-170): Import > MIDI file (patterns, Song, tracks and tempo in one undo
// step, alerts of the prototype), Export pattern > MIDI and Export song > MIDI (worker thread, bee, alert at the end).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "gemui/Alert.h"
#include "plugincore/MidiFiles.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymio/Smf.h"

#include "FileHost.h"

using namespace stym;
using namespace stym::testing;
using pluginui::MainWindow;
using plugin::Global;
using plugin::paramIndex;
using plugin::Track;

namespace {

// SMF 1 file: tempo, a note held for 3 steps on channel 1 (C4, velocity 127) and a GM snare drum on channel 10 at
// step 4.
std::string smallMidi(uint32_t usPerQuarter) {
    std::vector<std::vector<ymio::SmfEvent>> tracks(2);
    tracks[0].push_back(ymio::smfTempo(0, usPerQuarter));
    tracks[1] = {ymio::smfNoteOn(0, 0, 60, 127), ymio::smfNoteOff(72, 0, 60), ymio::smfNoteOn(96, 9, 38, 64),
                 ymio::smfNoteOff(100, 9, 38)};
    const std::vector<uint8_t> b = ymio::writeSmf(96, tracks);
    return {b.begin(), b.end()};
}

}  // namespace

TEST_CASE("Import > MIDI file: dialogue, tracks, patterns, Song, tempo, alert, one undo step", "[ui][midi]") {
    FileHost h;
    h.disk["/m/song.mid"] = smallMidi(400000);  // 150 BPM
    h.disk["/m/text.mid"] = "not a midi file";
    // Before the import: a note in pattern 1, a Song, tracks with settings.
    h.state.seq.cur().edit(5).pat[3] = 7;
    h.state.seq.song.entries = {{0, 2, 0}};
    h.p(0, Track::Generator) = 1;
    h.p(4, Track::On) = 1;
    const plugin::NonParamState before = h.state;
    const plugin::ParamValues valuesBefore = h.values;
    MainWindow w(h);
    w.tick(0);

    fileMenu(w, "Import", "MIDI file");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Import a MIDI file");
    CHECK(h.opens[0].patterns == "*.mid;*.midi");
    CHECK(h.opens[0].path.empty());
    h.pick("/m/song.mid");
    CHECK(h.editor.midiFile == "/m/song.mid");
    CHECK(alertText(w) == "MIDI imported\n\nsong.mid\n2 tracks, 1 pattern of 16 steps, 150 BPM.");
    dismiss(w);

    // Channel 1 → track 1 (Chip Voice), channel 10 → track 2 (Chipdrum); the other tracks are switched off.
    CHECK(h.p(0, Track::On) == 1.0f);
    CHECK(h.p(0, Track::Generator) == 0.0f);
    CHECK(h.p(1, Track::On) == 1.0f);
    CHECK(h.p(1, Track::Generator) == 4.0f);
    for (int t = 2; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == 0.0f);
    CHECK(h.p(Global::FreeTempo) == 150.0f);
    const ymseq::Lane& l0 = h.state.seq.patterns[0].lane(0);
    CHECK(l0.pat[0] == 0);
    CHECK(l0.tie[0] == 1);  // D-170: the held note carries TIE from its first step
    CHECK(l0.tie[1] == 1);
    CHECK(l0.tie[2] == 1);
    CHECK(l0.tie[3] == 0);
    CHECK(l0.vol[0] == 15);
    const ymseq::Lane& l1 = h.state.seq.patterns[0].lane(1);
    CHECK(l1.pat[4] == plugin::gmSlot(38));
    CHECK(l1.vol[4] == 8);
    CHECK(h.state.seq.patterns[0].lane(5).empty());  // the pattern is replaced whole
    REQUIRE(h.state.seq.song.entries.size() == 1);
    CHECK(h.state.seq.song.entries[0].pat == 0);
    CHECK(h.state.seq.song.entries[0].rep == 1);
    CHECK(h.editor.undo.undoSize() == 1);

    // A single step: patterns, Song, tracks and tempo come back together; then Redo.
    const plugin::NonParamState after = h.state;
    w.tick(10);
    undoKey(w);
    CHECK(h.state.seq.patterns[0].sameContent(before.seq.patterns[0]));
    CHECK(h.state.seq.song == before.seq.song);
    for (int t = 0; t < plugin::kTracks; ++t) {
        CAPTURE(t);
        CHECK(h.p(t, Track::On) == valuesBefore[size_t(paramIndex(t, Track::On))]);
        CHECK(h.p(t, Track::Generator) == valuesBefore[size_t(paramIndex(t, Track::Generator))]);
    }
    CHECK(h.p(Global::FreeTempo) == valuesBefore[size_t(paramIndex(Global::FreeTempo))]);
    undoKey(w, true);
    CHECK(h.state.seq.patterns[0].sameContent(after.seq.patterns[0]));
    CHECK(h.state.seq.song == after.seq.song);
    CHECK(h.p(1, Track::Generator) == 4.0f);
    CHECK(h.p(Global::FreeTempo) == 150.0f);

    // Refusal: alert of the prototype, nothing changes, no undo step; unreadable file.
    const size_t steps = h.editor.undo.undoSize();
    fileMenu(w, "Import", "MIDI file");
    CHECK(h.opens.back().path == "/m/song.mid");
    h.pick("/m/text.mid");
    CHECK(alertText(w) == "Unsupported MIDI file\n\nExpected a standard MIDI file (format 0 or 1) with notes.");
    dismiss(w);
    CHECK(h.state.seq.patterns[0].sameContent(after.seq.patterns[0]));
    CHECK(h.editor.undo.undoSize() == steps);
    fileMenu(w, "Import", "MIDI file");
    h.pick("/m/missing.mid");
    CHECK(alertText(w) == "The file could not be read.");
    dismiss(w);
}

TEST_CASE("Import > MIDI file: chip tracks in 3 ch mode, first pattern chosen", "[ui][midi]") {
    FileHost h;
    h.disk["/m/a.mid"] = smallMidi(500000);
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch
    for (int t = 0; t < plugin::kTracks; ++t) h.p(t, Track::On) = 1;
    h.state.seq.ensure(9);
    h.state.seq.current = 9;
    MainWindow w(h);
    w.tick(0);
    fileMenu(w, "Import", "MIDI file");
    h.pick("/m/a.mid");
    CHECK(alertText(w) == "MIDI imported\n\na.mid\n2 tracks, 1 pattern of 16 steps, 120 BPM.");
    CHECK(h.p(0, Track::On) == 1.0f);
    CHECK(h.p(1, Track::On) == 1.0f);
    for (int t = 2; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == 0.0f);
    CHECK(h.state.seq.current == 0);
    CHECK(h.selects == std::vector<int>{0});
    // Undone: the previous pattern becomes current again.
    dismiss(w);
    w.tick(10);
    undoKey(w);
    CHECK(h.state.seq.current == 9);
    CHECK(h.selects == std::vector<int>{0, 9});
    for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == 1.0f);
}

TEST_CASE("Export pattern > MIDI: dialogue, worker thread under the bee, file and alert", "[ui][midi]") {
    FileHost h;
    h.state.seq.cur().edit(0).pat[0] = 0;
    h.state.seq.cur().edit(0).pat[4] = 4;
    MainWindow w(h);
    double now = 0;
    w.tick(now);
    CHECK(w.desktop().pointer() != gemui::Pointer::Busy);

    fileMenu(w, "Export pattern", "MIDI");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the pattern as MIDI");
    CHECK(h.saves[0].patterns == "*.mid;*.midi");
    CHECK(h.saves[0].name == "stym2149-pattern.mid");
    h.pick("/x/out");
    CHECK(w.exporting());
    CHECK(w.desktop().pointer() == gemui::Pointer::Busy);
    // A second export waits for the end of the first (the thread stays counted until the tick that collects it).
    fileMenu(w, "Export pattern", "MIDI");
    CHECK(alertText(w) == "An export is already running.");
    dismiss(w);
    CHECK(h.saves.size() == 1);
    waitExport(w, now);
    CHECK(w.desktop().pointer() != gemui::Pointer::Busy);
    CHECK(alertText(w) == "MIDI exported\n\nout.mid\n1 track, 0:02.");
    dismiss(w);
    const auto expected = plugin::exportMidi(plugin::OfflineRender::Source::Pattern, h.values, h.state);
    REQUIRE(expected);
    REQUIRE(h.disk.count("/x/out.mid"));
    CHECK(h.disk["/x/out.mid"] == std::string(expected->bytes.begin(), expected->bytes.end()));
    // ".midi" kept; write refused: alert at the end of the thread.
    fileMenu(w, "Export pattern", "MIDI");
    h.pick("/x/b.MIDI");
    waitExport(w, now);
    dismiss(w);
    CHECK(h.disk.count("/x/b.MIDI"));
    fileMenu(w, "Export pattern", "MIDI");
    h.pick("/ro/c.mid");
    waitExport(w, now);
    CHECK(alertText(w) == "The file could not be written:\n/ro/c.mid");
    dismiss(w);
    // Dialogue cancelled (empty name): nothing goes out.
    fileMenu(w, "Export pattern", "MIDI");
    h.pick("  ");
    CHECK_FALSE(w.exporting());
}

TEST_CASE("Export song > MIDI: empty Song refused, Song rendered, editor closed during the export", "[ui][midi]") {
    FileHost h;
    h.state.seq.cur().edit(0).pat[0] = 0;
    auto w = std::make_unique<MainWindow>(h);
    double now = 0;
    w->tick(now);
    fileMenu(*w, "Export song", "MIDI");
    CHECK(alertText(*w) == "Song is empty. Add entries first.");
    dismiss(*w);
    CHECK(h.saves.empty());

    h.state.seq.song.entries = {{0, 2, 0}};
    ++h.serial;
    w->tick(now += 10);
    fileMenu(*w, "Export song", "MIDI");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the song as MIDI");
    CHECK(h.saves[0].name == "stym2149-song.mid");
    h.pick("/x/song");
    waitExport(*w, now);
    CHECK(alertText(*w) == "MIDI exported\n\nsong.mid\n1 track, 0:04.");
    dismiss(*w);
    const auto expected = plugin::exportMidi(plugin::OfflineRender::Source::Song, h.values, h.state);
    REQUIRE(expected);
    CHECK(h.disk["/x/song.mid"] == std::string(expected->bytes.begin(), expected->bytes.end()));

    // Editor closed during an export: the thread stops, nothing is written.
    h.state.seq.song.entries = {{0, 99, 0}};
    ++h.serial;
    w->tick(now += 10);
    fileMenu(*w, "Export song", "MIDI");
    h.pick("/x/long.mid");
    CHECK(w->exporting());
    w.reset();
    CHECK_FALSE(h.disk.count("/x/long.mid"));
}
