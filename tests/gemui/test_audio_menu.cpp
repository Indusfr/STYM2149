// STYM2149 — File menu, audio exports: Export pattern > WAV and Stems (ZIP), Export song > WAV
// and Stems (ZIP); GEM format dialogue (choice rows of the alert), preferences, worker thread under the bee, file
// written as the rendering goes, alerts at the end.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "gemui/Alert.h"
#include "plugincore/AudioExport.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymio/ByteSink.h"

#include "FileHost.h"

using namespace stym;
using namespace stym::testing;
using pluginui::MainWindow;
using plugin::Global;
using plugin::OfflineRender;
using plugin::Track;
using ymio::WavFormat;

namespace {

void note(ymseq::Lane& l, int step, int midi) {
    const int oc = midi / 12 - 1;
    l.pat[size_t(step)] = int16_t(midi - (oc + 1) * 12);
    l.oct[size_t(step)] = int8_t(ymseq::octaveCode(oc, 3));
}

// 16-step pattern at 120 BPM (2 s): two notes on track 1, one on track 3.
void project(FileHost& h) {
    h.p(Global::FreeTempo) = 120;
    h.state.seq.cur().steps = 16;
    note(h.state.seq.cur().edit(0), 0, 60);
    note(h.state.seq.cur().edit(0), 8, 67);
    note(h.state.seq.cur().edit(2), 4, 48);
    h.p(2, Track::On) = 1;
}

void clickOption(MainWindow& w, size_t row, size_t index) {
    gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    const gemui::Button* b = a->option(row, index);
    REQUIRE(b);
    const gemui::Point at = centre(b->bounds());
    w.desktop().mouseMove(at, {});
    w.desktop().mouseDown(at, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(at, {});
}

std::vector<int> dialogPicks(MainWindow& w) {
    const gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    return a->picks();
}

uint32_t rd32(const std::string& b, size_t at) {
    return uint32_t(uint8_t(b[at])) | uint32_t(uint8_t(b[at + 1])) << 8 | uint32_t(uint8_t(b[at + 2])) << 16 |
           uint32_t(uint8_t(b[at + 3])) << 24;
}

std::string expectedWav(const FileHost& h, OfflineRender::Source s, plugin::WavSettings ws) {
    ymio::MemorySink sink;
    REQUIRE(plugin::exportWav(s, h.values, h.state, ws, sink));
    return {sink.bytes.begin(), sink.bytes.end()};
}

std::string expectedStems(const FileHost& h, OfflineRender::Source s, plugin::WavSettings ws) {
    ymio::MemorySink sink;
    REQUIRE(plugin::exportStems(s, h.values, h.state, ws, sink));
    return {sink.bytes.begin(), sink.bytes.end()};
}

}  // namespace

TEST_CASE("Export pattern > WAV: Format and Rate dialogue, preferences kept, file and alert", "[ui][wav]") {
    FileHost h;
    project(h);
    MainWindow w(h);
    double now = 0;
    w.tick(now);

    // First dialogue: 16 bits, host sample rate (the prototype's wavBlobDv and the rate of its audio context).
    fileMenu(w, "Export pattern", "WAV");
    CHECK(alertText(w) == "Export the pattern as WAV");
    CHECK(dialogPicks(w) == std::vector<int>{0, 6});
    REQUIRE(topAlert(w)->option(0, 2));
    CHECK(topAlert(w)->option(0, 2)->text() == "32-bit float");
    CHECK(topAlert(w)->option(1, 6)->text() == "Host");
    CHECK(topAlert(w)->option(0, 0)->on());
    clickOption(w, 0, 2);
    clickOption(w, 1, 1);
    CHECK(dialogPicks(w) == std::vector<int>{2, 1});
    CHECK_FALSE(topAlert(w)->option(0, 0)->on());
    CHECK(topAlert(w)->option(0, 2)->on());
    CHECK(h.saves.empty());
    dismiss(w);  // Enter: Export
    CHECK(h.prefs["wavFormat"] == "32f");
    CHECK(h.prefs["wavRate"] == "48000");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the pattern as WAV");
    CHECK(h.saves[0].patterns == "*.wav");
    CHECK(h.saves[0].name == "stym2149-pattern.wav");
    h.pick("/x/take");
    CHECK(w.exporting());
    CHECK(w.desktop().busy());
    waitExport(w, now);
    CHECK_FALSE(w.desktop().busy());
    CHECK(alertText(w) == "WAV exported\n\ntake.wav\n32-bit float, 48 kHz, 0:02.");
    dismiss(w);
    REQUIRE(h.disk.count("/x/take.wav"));
    CHECK(h.disk["/x/take.wav"] == expectedWav(h, OfflineRender::Source::Pattern, {WavFormat::Float32, 48000}));

    // The next dialogue offers the last choice; Escape cancels it without asking anything.
    fileMenu(w, "Export pattern", "WAV");
    CHECK(dialogPicks(w) == std::vector<int>{2, 1});
    pressKey(w, gemui::Key::Escape);
    CHECK_FALSE(topAlert(w));
    CHECK(h.saves.size() == 1);
    CHECK(h.prefs["wavRate"] == "48000");

    // Host: the host's sample rate, 48 kHz if it gives none.
    h.rate = 44100;
    fileMenu(w, "Export pattern", "WAV");
    clickOption(w, 0, 1);
    clickOption(w, 1, 6);
    dismiss(w);
    CHECK(h.prefs["wavFormat"] == "24");
    CHECK(h.prefs["wavRate"] == "host");
    h.pick("/x/host.wav");
    waitExport(w, now);
    CHECK(alertText(w) == "WAV exported\n\nhost.wav\n24-bit, 44.1 kHz, 0:02.");
    dismiss(w);
    CHECK(rd32(h.disk["/x/host.wav"], 24) == 44100);
    CHECK(h.disk["/x/host.wav"] == expectedWav(h, OfflineRender::Source::Pattern, {WavFormat::Pcm24, 44100}));
    h.rate = 0;
    fileMenu(w, "Export pattern", "WAV");
    CHECK(dialogPicks(w) == std::vector<int>{1, 6});
    dismiss(w);
    h.pick("/x/none.wav");
    waitExport(w, now);
    CHECK(alertText(w) == "WAV exported\n\nnone.wav\n24-bit, 48 kHz, 0:02.");
    dismiss(w);
    CHECK(rd32(h.disk["/x/none.wav"], 24) == 48000);

    // Unknown preferences: default settings.
    h.prefs["wavFormat"] = "8";
    h.prefs["wavRate"] = "22050";
    fileMenu(w, "Export pattern", "WAV");
    CHECK(dialogPicks(w) == std::vector<int>{0, 6});
    pressKey(w, gemui::Key::Escape);
}

TEST_CASE("Export pattern > WAV: Tab moves the focus to Cancel then Export, Enter activates that button",
          "[ui][wav][keyboard]") {
    FileHost h;
    project(h);
    MainWindow w(h);
    gemui::KeyEvent tab;
    tab.key = gemui::Key::Tab;
    // On opening, no button has the focus: Enter exports (previous test).
    fileMenu(w, "Export pattern", "WAV");
    REQUIRE(topAlert(w));
    CHECK(topAlert(w)->focusedButton() == -1);
    CHECK(topAlert(w)->button(0).text() == "Cancel");
    CHECK(topAlert(w)->button(1).text() == "Export");
    CHECK(topAlert(w)->button(1).style() == gemui::Button::Style::Default);
    // Tab: Cancel, Enter cancels.
    w.desktop().keyDown(tab);
    CHECK(topAlert(w)->focusedButton() == 0);
    dismiss(w);
    CHECK_FALSE(topAlert(w));
    CHECK(h.saves.empty());
    // Tab, Tab: Export; Shift+Tab goes back to Cancel, Tab again to Export.
    fileMenu(w, "Export pattern", "WAV");
    w.desktop().keyDown(tab);
    w.desktop().keyDown(tab);
    CHECK(topAlert(w)->focusedButton() == 1);
    gemui::KeyEvent back = tab;
    back.mods.shift = true;
    w.desktop().keyDown(back);
    CHECK(topAlert(w)->focusedButton() == 0);
    w.desktop().keyDown(tab);
    dismiss(w);
    CHECK_FALSE(topAlert(w));
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the pattern as WAV");
}

TEST_CASE("Export song > Stems (ZIP): empty Song refused, ZIP of the playing tracks, alert", "[ui][wav]") {
    FileHost h;
    project(h);
    MainWindow w(h);
    double now = 0;
    w.tick(now);
    fileMenu(w, "Export song", "Stems (ZIP)");
    CHECK(alertText(w) == "Song is empty. Add entries first.");
    dismiss(w);
    fileMenu(w, "Export song", "WAV");
    CHECK(alertText(w) == "Song is empty. Add entries first.");
    dismiss(w);

    h.state.seq.song.entries = {{0, 2, 0}};
    ++h.serial;
    w.tick(now += 10);
    fileMenu(w, "Export song", "Stems (ZIP)");
    CHECK(alertText(w) == "Export the song stems");
    dismiss(w);
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the song stems");
    CHECK(h.saves[0].patterns == "*.zip");
    CHECK(h.saves[0].name == "stym2149-song-stems.zip");
    h.pick("/x/stems");
    waitExport(w, now);
    CHECK(alertText(w) == "Stems exported\n\nstems.zip\n2 tracks, 16-bit, 48 kHz, 0:04.");
    dismiss(w);
    REQUIRE(h.disk.count("/x/stems.zip"));
    CHECK(h.disk["/x/stems.zip"] == expectedStems(h, OfflineRender::Source::Song, {WavFormat::Pcm16, 48000}));

    fileMenu(w, "Export song", "WAV");
    CHECK(alertText(w) == "Export the song as WAV");
    dismiss(w);
    CHECK(h.saves.back().name == "stym2149-song.wav");
    h.pick("/x/song.WAV");
    waitExport(w, now);
    CHECK(alertText(w) == "WAV exported\n\nsong.WAV\n16-bit, 48 kHz, 0:04.");
    dismiss(w);
    CHECK(h.disk["/x/song.WAV"] == expectedWav(h, OfflineRender::Source::Song, {WavFormat::Pcm16, 48000}));

    // A single track: "1 track".
    h.state.seq.cur().lanes[2] = nullptr;
    ++h.serial;
    w.tick(now += 10);
    fileMenu(w, "Export pattern", "Stems (ZIP)");
    CHECK(alertText(w) == "Export the pattern stems");
    dismiss(w);
    CHECK(h.saves.back().name == "stym2149-pattern-stems.zip");
    h.pick("/x/one.zip");
    waitExport(w, now);
    CHECK(alertText(w) == "Stems exported\n\none.zip\n1 track, 16-bit, 48 kHz, 0:02.");
    dismiss(w);
}

TEST_CASE("Audio exports: tracks without notes, write refused, one export at a time, editor closed", "[ui][wav]") {
    FileHost h;
    h.p(Global::FreeTempo) = 120;
    auto w = std::make_unique<MainWindow>(h);
    double now = 0;
    w->tick(now);

    // No note: the prototype's exportStems, nothing written.
    h.state.seq.cur().lanes[0] = nullptr;
    ++h.serial;
    w->tick(now += 10);
    fileMenu(*w, "Export pattern", "Stems (ZIP)");
    dismiss(*w);
    h.pick("/x/empty.zip");
    waitExport(*w, now);
    CHECK(alertText(*w) == "No tracks with notes.");
    dismiss(*w);
    CHECK_FALSE(h.disk.count("/x/empty.zip"));

    project(h);
    ++h.serial;
    w->tick(now += 10);
    // Destination refused at once, or full while writing: alert, no file.
    fileMenu(*w, "Export pattern", "WAV");
    dismiss(*w);
    h.pick("/ro/a.wav");
    CHECK_FALSE(w->exporting());
    CHECK(alertText(*w) == "The file could not be written:\n/ro/a.wav");
    dismiss(*w);
    fileMenu(*w, "Export pattern", "Stems (ZIP)");
    dismiss(*w);
    h.pick("/full/b");
    waitExport(*w, now);
    CHECK(alertText(*w) == "The file could not be written:\n/full/b.zip");
    dismiss(*w);
    CHECK_FALSE(h.disk.count("/full/b.zip"));

    // A long export: a second one is refused; closing the editor stops it, and the temporary copy is abandoned.
    h.state.seq.song.entries = {{0, 99, 0}};
    ++h.serial;
    w->tick(now += 10);
    fileMenu(*w, "Export song", "Stems (ZIP)");
    dismiss(*w);
    h.pick("/x/long.zip");
    CHECK(w->exporting());
    CHECK(w->desktop().busy());
    fileMenu(*w, "Export pattern", "WAV");
    CHECK(alertText(*w) == "An export is already running.");
    dismiss(*w);
    w.reset();
    CHECK_FALSE(h.disk.count("/x/long.zip"));
}
