// STYM2149 — File menu, MIDI files: Import > MIDI file (patterns, Song, tracks and tempo in one undo
// step, alerts of the prototype; notes replaced and drums asked first; the other tracks kept),
// drop of a MIDI file (a file with one channel on the focused track, as drums on a drum track),
// Export pattern > MIDI and Export song > MIDI (worker thread, bee, alert at the end; greyed without a Song).
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
    // The note of the current pattern is on a track that the file does not fill: no question about it;
    // the drums of channel 10 land on a Chip Voice track: Chipdrum, the default button.
    CHECK(alertText(w) == "The MIDI file holds drums (channel 10).\nPlay them with Chipdrum or Digidrum?");
    dismiss(w);
    CHECK(alertText(w) == "MIDI imported\n\nsong.mid\n2 tracks, 1 pattern of 16 steps, 150 BPM.\n"
                          "The other tracks and the tempo stay.");
    dismiss(w);

    // Channel 1 → track 1, which keeps its melodic generator (SID Voice), channel 10 → track 2 (Chipdrum); the other
    // tracks keep their switches, their notes, and the tempo stays.
    CHECK(h.p(0, Track::On) == 1.0f);
    CHECK(h.p(0, Track::Generator) == 1.0f);
    CHECK(h.p(1, Track::On) == 1.0f);
    CHECK(h.p(1, Track::Generator) == 4.0f);
    for (int t = 2; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == valuesBefore[size_t(paramIndex(t, Track::On))]);
    CHECK(h.p(4, Track::On) == 1.0f);
    CHECK(h.p(Global::FreeTempo) == valuesBefore[size_t(paramIndex(Global::FreeTempo))]);
    const ymseq::Lane& l0 = h.state.seq.patterns[0].lane(0);
    CHECK(l0.pat[0] == 0);
    CHECK(l0.tie[0] == 1);  // the held note carries TIE from its first step
    CHECK(l0.tie[1] == 1);
    CHECK(l0.tie[2] == 1);
    CHECK(l0.tie[3] == 0);
    CHECK(l0.vol[0] == 15);
    const ymseq::Lane& l1 = h.state.seq.patterns[0].lane(1);
    CHECK(l1.pat[4] == plugin::gmSlot(38));
    CHECK(l1.vol[4] == 8);
    CHECK(h.state.seq.patterns[0].lane(5).pat[3] == 7);  // kept
    CHECK(h.state.seq.song == before.seq.song);           // a Song already made stays
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

    // An empty project: the Song becomes the pattern, the tempo that of the file, the melodic track Chip Voice.
    FileHost e;
    e.disk["/m/song.mid"] = smallMidi(400000);
    e.p(0, Track::Generator) = 4;  // Chipdrum: a melodic channel makes it Chip Voice
    MainWindow we(e);
    we.tick(0);
    fileMenu(we, "Import", "MIDI file");
    e.pick("/m/song.mid");
    dismiss(we);  // Chipdrum
    CHECK(alertText(we) == "MIDI imported\n\nsong.mid\n2 tracks, 1 pattern of 16 steps, 150 BPM.");
    dismiss(we);
    CHECK(e.p(0, Track::Generator) == 0.0f);
    CHECK(e.p(Global::FreeTempo) == 150.0f);
    REQUIRE(e.state.seq.song.entries.size() == 1);
    CHECK(e.state.seq.song.entries[0].pat == 0);
    CHECK(e.state.seq.song.entries[0].rep == 1);

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

TEST_CASE("Import > MIDI file: chip tracks in 3 ch mode, from the current pattern", "[ui][midi]") {
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
    CHECK(alertText(w) == "The MIDI file holds drums (channel 10).\nPlay them with Chipdrum or Digidrum?\n"
                          "Digidrum: on track 4, the Digidrum track.");
    dismiss(w);  // Chipdrum, on the second channel of the chip
    CHECK(alertText(w) == "MIDI imported\n\na.mid\n2 tracks, 1 pattern of 16 steps, 120 BPM.");
    CHECK(h.p(0, Track::On) == 1.0f);
    CHECK(h.p(1, Track::Generator) == 4.0f);
    CHECK(h.p(1, Track::On) == 1.0f);
    for (int t = 2; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == 1.0f);  // kept
    // In pattern 10, the current one; pattern 1 stays empty, the Song plays pattern 10.
    CHECK(h.state.seq.current == 9);
    CHECK(h.state.seq.viewBank == 1);
    CHECK(h.state.seq.patterns[9].lane(0).pat[0] == 0);
    CHECK(h.state.seq.patterns[0].lane(0).empty());
    REQUIRE(h.state.seq.song.entries.size() == 1);
    CHECK(h.state.seq.song.entries[0].pat == 9);
    // Undone.
    dismiss(w);
    w.tick(10);
    undoKey(w);
    CHECK(h.state.seq.current == 9);
    CHECK(h.state.seq.patterns[9].lane(0).empty());
    CHECK(h.state.seq.song.entries.empty());
    for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == 1.0f);
}

TEST_CASE("Import > MIDI file: drums played by Chipdrum or Digidrum, asked unless their track plays one",
          "[ui][midi]") {
    FileHost h;
    h.disk["/m/a.mid"] = smallMidi(500000);
    MainWindow w(h);
    w.tick(0);
    // Escape cancels: nothing is imported.
    fileMenu(w, "Import", "MIDI file");
    h.pick("/m/a.mid");
    REQUIRE(topAlert(w));
    CHECK(topAlert(w)->buttonCount() == 3);
    CHECK(topAlert(w)->button(0).text() == "Cancel");
    CHECK(topAlert(w)->button(1).text() == "Chipdrum");
    CHECK(topAlert(w)->button(2).text() == "Digidrum");
    pressKey(w, gemui::Key::Escape);
    CHECK_FALSE(topAlert(w));
    CHECK(h.editor.undo.undoSize() == 0);
    CHECK(h.state.seq.patterns[0].lane(0).empty());
    CHECK(h.p(1, Track::On) == 0.0f);

    // Digidrum (32 ch): the channel 10 track becomes Digidrum, its notes the same rows.
    fileMenu(w, "Import", "MIDI file");
    h.pick("/m/a.mid");
    alertButton(w, 2);
    CHECK(alertText(w) == "MIDI imported\n\na.mid\n2 tracks, 1 pattern of 16 steps, 120 BPM.");
    dismiss(w);
    CHECK(h.p(0, Track::Generator) == 0.0f);
    CHECK(h.p(1, Track::On) == 1.0f);
    CHECK(h.p(1, Track::Generator) == 2.0f);
    CHECK(h.state.seq.patterns[0].lane(1).pat[4] == plugin::gmSlot(38));

    // A track that plays Digidrum keeps it without a question (only the notes replaced are asked about).
    fileMenu(w, "Import", "MIDI file");
    h.pick("/m/a.mid");
    CHECK(alertText(w) == "Tracks 1 and 2 hold notes. Replace them?\nThe MIDI file fills pattern 1.");
    alertButton(w, 1);
    CHECK(alertText(w).starts_with("MIDI imported"));
    dismiss(w);
    CHECK(h.p(1, Track::Generator) == 2.0f);
    // Chipdrum too.
    h.p(1, Track::Generator) = 4;
    w.tick(10);
    fileMenu(w, "Import", "MIDI file");
    h.pick("/m/a.mid");
    alertButton(w, 1);
    CHECK(alertText(w).starts_with("MIDI imported"));
    dismiss(w);
    CHECK(h.p(1, Track::Generator) == 4.0f);
}

TEST_CASE("Import > MIDI file: Digidrum in 3 ch puts the drums on the Digidrum track", "[ui][midi]") {
    FileHost h;
    h.disk["/m/a.mid"] = smallMidi(500000);
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch
    MainWindow w(h);
    w.tick(0);
    fileMenu(w, "Import", "MIDI file");
    h.pick("/m/a.mid");
    alertButton(w, 2);
    CHECK(alertText(w) == "MIDI imported\n\na.mid\n2 tracks, 1 pattern of 16 steps, 120 BPM.");
    dismiss(w);
    // Channel 1 on track 1, channel 10 on track 4 (the Digidrum track); the other channels of the chip stay free.
    CHECK(h.p(0, Track::On) == 1.0f);
    CHECK(h.p(1, Track::On) == 0.0f);
    CHECK(h.p(2, Track::On) == 0.0f);
    CHECK(h.p(3, Track::On) == 1.0f);
    CHECK(h.p(3, Track::Generator) == 2.0f);
    CHECK(h.state.seq.patterns[0].lane(1).empty());
    CHECK(h.state.seq.patterns[0].lane(3).pat[4] == plugin::gmSlot(38));
    CHECK(h.editor.undo.undoSize() == 1);
}

TEST_CASE("Drop of MIDI files: the first one imported like File > Import > MIDI file", "[ui][midi]") {
    CHECK(MainWindow::isMidiFile("/a/b.mid"));
    CHECK(MainWindow::isMidiFile("C:\\x\\SONG.MIDI"));
    CHECK(MainWindow::isMidiFile("drums.Mid"));
    CHECK_FALSE(MainWindow::isMidiFile("/a/b.mid.txt"));
    CHECK_FALSE(MainWindow::isMidiFile("/a/mid"));
    CHECK_FALSE(MainWindow::isMidiFile("/a/b.xm"));

    FileHost h;
    h.disk["/m/Song.MID"] = smallMidi(400000);
    h.disk["/m/other.mid"] = smallMidi(500000);
    h.disk["/m/notes.txt"] = "text";
    h.p(1, Track::Generator) = 4;  // Chipdrum: no question for the drums
    MainWindow w(h);
    w.tick(0);
    CHECK_FALSE(w.dropFiles({"/m/notes.txt"}));
    CHECK_FALSE(topAlert(w));
    CHECK(w.dropFiles({"/m/notes.txt", "/m/Song.MID", "/m/other.mid"}));
    CHECK(h.editor.midiFile == "/m/Song.MID");
    CHECK(alertText(w) == "MIDI imported\n\nSong.MID\n2 tracks, 1 pattern of 16 steps, 150 BPM.");
    dismiss(w);
    CHECK(h.p(Global::FreeTempo) == 150.0f);
    CHECK(h.editor.undo.undoSize() == 1);

    // Notes in place: Cancel, the default button, keeps them.
    const plugin::NonParamState before = h.state;
    CHECK(w.dropFiles({"/m/other.mid"}));
    CHECK(alertText(w) == "Tracks 1 and 2 hold notes. Replace them?\nThe MIDI file fills pattern 1.");
    dismiss(w);
    CHECK_FALSE(topAlert(w));
    CHECK(h.state.seq.patterns[0].sameContent(before.seq.patterns[0]));
    CHECK(h.p(Global::FreeTempo) == 150.0f);
    CHECK(h.editor.undo.undoSize() == 1);
    // Replace: imported.
    CHECK(w.dropFiles({"/m/other.mid"}));
    alertButton(w, 1);
    CHECK(alertText(w).starts_with("MIDI imported"));
    dismiss(w);
    CHECK(h.p(Global::FreeTempo) == 120.0f);
    CHECK(h.editor.undo.undoSize() == 2);

    // Unreadable file.
    CHECK(w.dropFiles({"/m/missing.mid"}));
    CHECK(alertText(w) == "The file could not be read.");
    dismiss(w);
}

TEST_CASE("Drop of a drum loop on channel 1: on the focused drum track, in its grid", "[ui][midi]") {
    // GM kick and snare on channel 1, as a DAW writes a drum loop.
    std::vector<std::vector<ymio::SmfEvent>> tracks(1);
    tracks[0] = {ymio::smfNoteOn(0, 0, 36, 100), ymio::smfNoteOff(12, 0, 36), ymio::smfNoteOn(24, 0, 38, 100),
                 ymio::smfNoteOff(36, 0, 38)};
    const std::vector<uint8_t> b = ymio::writeSmf(96, tracks);
    FileHost h;
    h.disk["/m/loop.mid"] = {b.begin(), b.end()};
    h.p(Global::FocusTrack) = 3;
    h.p(2, Track::On) = 1;
    h.p(2, Track::Generator) = 4;  // Chipdrum
    const float on0 = h.p(0, Track::On);
    MainWindow w(h);
    w.tick(0);
    CHECK(w.dropFiles({"/m/loop.mid"}));
    CHECK(alertText(w) == "MIDI imported\n\nloop.mid\n1 track, 1 pattern of 16 steps, 120 BPM.");
    dismiss(w);
    CHECK(h.p(2, Track::Generator) == 4.0f);
    CHECK(h.p(2, Track::On) == 1.0f);
    CHECK(h.p(0, Track::On) == on0);  // untouched
    CHECK(h.state.seq.patterns[0].lane(2).pat[0] == 0);  // Kick
    CHECK(h.state.seq.patterns[0].lane(2).pat[1] == 1);  // Snare
    CHECK(h.state.seq.patterns[0].lane(0).empty());

    // The focused track melodic: the loop is notes of that track, its notes replaced after the question.
    h.p(2, Track::Generator) = 0;
    w.tick(10);
    CHECK(w.dropFiles({"/m/loop.mid"}));
    CHECK(alertText(w) == "Track 3 holds notes. Replace them?\nThe MIDI file fills pattern 1.");
    alertButton(w, 1);  // replace the notes
    dismiss(w);
    CHECK(h.p(2, Track::On) == 1.0f);
    CHECK(h.p(2, Track::Generator) == 0.0f);
    CHECK(h.state.seq.patterns[0].lane(2).pat[0] == 0);  // C
    CHECK(h.state.seq.patterns[0].lane(2).pat[1] == 2);  // D
    CHECK(h.state.seq.patterns[0].lane(0).empty());
}

TEST_CASE("Import on an empty track: no question, the other tracks keep their notes", "[ui][midi]") {
    // A bass line on channel 1, dropped on track 3 while tracks 1 and 2 hold notes (Indus, 28/10/2026 02:01-02:06).
    std::vector<std::vector<ymio::SmfEvent>> tracks(1);
    tracks[0] = {ymio::smfNoteOn(0, 0, 48, 100), ymio::smfNoteOff(24, 0, 48), ymio::smfNoteOn(96, 0, 50, 100),
                 ymio::smfNoteOff(120, 0, 50)};
    const std::vector<uint8_t> b = ymio::writeSmf(96, tracks);
    FileHost h;
    h.disk["/m/bass.mid"] = {b.begin(), b.end()};
    h.state.seq.cur().edit(0).pat[0] = 0;
    h.state.seq.cur().edit(1).pat[4] = 4;
    h.p(0, Track::On) = 1;
    h.p(1, Track::On) = 1;
    h.p(Global::FocusTrack) = 3;
    const float tempo = h.p(Global::FreeTempo);
    const plugin::NonParamState before = h.state;
    MainWindow w(h);
    w.tick(0);
    CHECK(w.dropFiles({"/m/bass.mid"}));
    CHECK(alertText(w) == "MIDI imported\n\nbass.mid\n1 track, 1 pattern of 16 steps, 120 BPM.\n"
                          "The other tracks and the tempo stay.");
    dismiss(w);
    CHECK(h.p(0, Track::On) == 1.0f);
    CHECK(h.p(1, Track::On) == 1.0f);
    CHECK(h.p(2, Track::On) == 1.0f);
    CHECK(h.p(Global::FreeTempo) == tempo);
    const ymseq::Pattern& q = h.state.seq.patterns[0];
    CHECK(q.lane(0) == before.seq.patterns[0].lane(0));
    CHECK(q.lane(1) == before.seq.patterns[0].lane(1));
    CHECK(q.lane(2).pat[0] == 0);  // C
    CHECK(q.lane(2).pat[4] == 2);  // D
    CHECK(h.editor.undo.undoSize() == 1);
    // Undone: track 3 empty again, the others as they were.
    w.tick(10);
    undoKey(w);
    CHECK(h.state.seq.patterns[0].sameContent(before.seq.patterns[0]));
    CHECK(h.p(2, Track::On) == 0.0f);
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

TEST_CASE("Export song > MIDI: greyed without a Song, Song rendered, editor closed during the export", "[ui][midi]") {
    FileHost h;
    h.state.seq.cur().edit(0).pat[0] = 0;
    auto w = std::make_unique<MainWindow>(h);
    double now = 0;
    w->tick(now);
    // Empty Song: the entry is greyed with its reason and opens no submenu.
    w->openMenu(1);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w->desktop().topPopup());
    REQUIRE(list);
    int song = -1;
    for (size_t i = 0; i < list->items().size(); ++i)
        if (list->items()[i].label == "Export song") song = int(i);
    REQUIRE(song >= 0);
    CHECK(list->items()[size_t(song)].disabled);
    CHECK(list->items()[size_t(song)].why == "The Song is empty: add entries in the Song window");
    w->desktop().mouseMove(centre(list->itemRect(song)), {});
    w->tick(now += 1000);
    CHECK(w->desktop().topPopup() == list);
    w->desktop().closePopups();
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
