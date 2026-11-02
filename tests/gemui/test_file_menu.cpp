// STYM2149 — File menu: Load, Save and Save as of a preset, a pattern, a Song and a kit (binary,
// no JSON), Import and Export of the Patches library, through the host's dialogues; alerts of the
// prototype; undo steps of the loads; Play....
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "gemui/Alert.h"
#include "plugincore/Parameters.h"
#include "plugincore/Patches.h"
#include "plugincore/ProjectFiles.h"
#include "pluginui/MainWindow.h"

#include "FileHost.h"

using namespace stym;
using namespace stym::testing;
using pluginui::MainWindow;
using plugin::FileKind;
using plugin::Global;
using plugin::paramIndex;
using plugin::Track;

namespace {
void ctrlN(MainWindow& w) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = U'n';
    e.code = 'N';
    e.mods.ctrl = true;
    w.desktop().keyDown(e);
}
}  // namespace

namespace {
std::string native(FileKind k, const plugin::ParamValues& v, const plugin::NonParamState& x) {
    const std::vector<uint8_t> b = plugin::encodeNativeFile(k, v, x);
    return {b.begin(), b.end()};
}
}  // namespace

TEST_CASE("File: Save and Save as in the binary format, names, extension added, Save rewrites the same file",
          "[ui][files]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);

    fileMenu(w, "Save", "Preset");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Save preset as");
    CHECK(h.saves[0].patterns == "*.stym");
    CHECK(h.saves[0].path.empty());
    CHECK(h.saves[0].name == "stym2149.stym");
    h.pick("/home/u/my preset");
    REQUIRE(h.disk.count("/home/u/my preset.stym"));
    CHECK(h.disk["/home/u/my preset.stym"] == native(FileKind::Preset, h.values, h.state));
    CHECK(h.disk["/home/u/my preset.stym"].substr(0, 4) == "STYM");
    CHECK(h.editor.files[size_t(FileKind::Preset)] == "/home/u/my preset.stym");

    // Save: same file, without a dialogue.
    h.p(0, Track::Volume) = 4;
    w.tick(10);
    fileMenu(w, "Save", "Preset");
    CHECK(h.saves.size() == 1);
    CHECK(h.disk["/home/u/my preset.stym"] == native(FileKind::Preset, h.values, h.state));
    // Save as: always the dialogue, on the current file; the extension in capitals is kept.
    fileMenu(w, "Save as", "Preset");
    REQUIRE(h.saves.size() == 2);
    CHECK(h.saves[1].path == "/home/u/my preset.stym");
    h.pick("/home/u/b.STYM");
    CHECK(h.disk.count("/home/u/b.STYM"));
    CHECK(h.editor.files[size_t(FileKind::Preset)] == "/home/u/b.STYM");

    // Pattern, Song and kit: each its own file, name and extension.
    h.state.seq.cur().edit(1).pat[2] = 5;
    h.state.seq.song.entries = {{0, 3, 0}};
    h.state.kit.inst[0].a = 60;
    ++h.serial;
    w.tick(20);
    const std::pair<const char*, FileKind> kinds[] = {
        {"Pattern", FileKind::Pattern}, {"Song", FileKind::Song}, {"Drum kit", FileKind::Kit}};
    for (const auto& [label, kind] : kinds) {
        fileMenu(w, "Save", label);
        CHECK(h.saves.back().name == plugin::nativeFileName(kind));
        CHECK(h.saves.back().patterns == "*" + std::string(plugin::nativeExtension(kind)));
        h.pick(std::string("/d/") + label);
    }
    CHECK(h.disk["/d/Pattern.stympat"] == native(FileKind::Pattern, h.values, h.state));
    CHECK(h.disk["/d/Song.stymsng"] == native(FileKind::Song, h.values, h.state));
    CHECK(h.disk["/d/Drum kit.stymkit"] == native(FileKind::Kit, h.values, h.state));

    // Write refused: alert, file not kept; dialogue cancelled: nothing.
    fileMenu(w, "Save as", "Song");
    h.pick("/ro/song.stymsng");
    CHECK(alertText(w) == "The file could not be written:\n/ro/song.stymsng");
    dismiss(w);
    CHECK(h.editor.files[size_t(FileKind::Song)] == "/d/Song.stymsng");
    fileMenu(w, "Save as", "Song");
    h.pending = nullptr;  // cancelled
    CHECK_FALSE(w.desktop().hasModal());
}

TEST_CASE("File: Load of the binary files, kind checked, newer version refused", "[ui][files][undo]") {
    FileHost h;
    plugin::ParamValues v = plugin::defaultValues();
    plugin::NonParamState x;
    v[size_t(paramIndex(Global::VoiceMode))] = 1;
    v[size_t(paramIndex(2, Track::Volume))] = 5;
    x.trackNames[2] = "Lead";
    x.seq.cur().edit(2).pat[0] = 7;
    x.seq.cur().steps = 24;
    x.seq.patterns[4].edit(1).pat[0] = 2;
    x.seq.used.set(4);
    x.seq.song.entries = {{4, 2, 0}, {0, 1, 150}};
    x.global.songMode = true;
    x.kit.inst[1].a = 300;
    plugin::sanitize(x);
    for (FileKind k : {FileKind::Preset, FileKind::Pattern, FileKind::Song, FileKind::Kit})
        h.disk["/b/f" + std::string(plugin::nativeExtension(k))] = native(k, v, x);
    std::string newer = h.disk["/b/f.stympat"];
    newer[4] = char(plugin::kStateVersion + 1);
    h.disk["/b/newer.stympat"] = newer;
    MainWindow w(h);
    w.tick(0);

    fileMenu(w, "Load", "Pattern");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Load pattern");
    CHECK(h.opens[0].patterns == "*.stympat");
    h.pick("/b/f.stympat");
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.state.seq.cur().sameContent(x.seq.cur()));
    fileMenu(w, "Load", "Song");
    h.pick("/b/f.stymsng");
    CHECK(h.state.seq.song == x.seq.song);
    CHECK(h.state.global.songMode);
    CHECK(h.state.seq.patterns[4].lane(1).pat[0] == 2);
    fileMenu(w, "Load", "Drum kit");
    h.pick("/b/f.stymkit");
    CHECK(h.state.kit == x.kit);
    CHECK(h.editor.undo.undoSize() == 3);

    // A file of another kind: said, nothing changed; a newer file: refused.
    fileMenu(w, "Load", "Pattern");
    h.pick("/b/f.stymkit");
    CHECK(alertText(w) == "Pattern read error.\nThis file holds a drum kit.");
    dismiss(w);
    fileMenu(w, "Load", "Drum kit");
    h.pick("/b/f.stym");
    CHECK(alertText(w) == "Not a valid drum kit file.\nThis file holds a project.");
    dismiss(w);
    fileMenu(w, "Load", "Pattern");
    h.pick("/b/newer.stympat");
    CHECK(alertText(w) == "This file comes from a newer STYM2149.\nIt cannot be read by this version.");
    dismiss(w);
    CHECK(h.editor.undo.undoSize() == 3);

    // The whole project, without an undo step; Load does not change the file of Save.
    fileMenu(w, "Load", "Preset");
    h.pick("/b/f.stym");
    CHECK(h.values == v);
    plugin::PluginState a, b;
    a.values = v;
    a.extras = x;
    b.values = h.values;
    b.extras = h.state;
    CHECK(plugin::encodeState(a) == plugin::encodeState(b));
    CHECK(h.editor.files[size_t(FileKind::Preset)].empty());
}

TEST_CASE("File: Load of a preset, refusals and alerts of the prototype", "[ui][files]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);
    plugin::ParamValues v = plugin::defaultValues();
    plugin::NonParamState x;
    v[size_t(paramIndex(Global::VoiceMode))] = 1;
    v[size_t(paramIndex(2, Track::Volume))] = 5;
    x.trackNames[2] = "Lead";
    x.seq.cur().edit(2).pat[0] = 7;
    x.instruments[0].used = true;
    x.instruments[0].name = "Pad";
    plugin::sanitize(x);
    h.disk["/p/a.stym"] = native(FileKind::Preset, v, x);
    h.disk["/p/bad.stym"] = "STYM";
    // A JSON file is not read: STYM2149 never wrote one that a user keeps.
    h.disk["/p/a.json"] = R"({"app":"STYM2149","v":1})";

    fileMenu(w, "Load", "Preset");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Load preset");
    CHECK(h.opens[0].patterns == "*.stym");
    h.pick("/p/a.stym");
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.values == v);
    CHECK(h.editor.undo.undoSize() == 0);  // loadPresetObj: no undo step
    // Load does not change the file of Save.
    CHECK(h.editor.files[size_t(FileKind::Preset)].empty());

    for (const char* path : {"/p/bad.stym", "/p/a.json", "/p/missing.stym"}) {
        CAPTURE(path);
        fileMenu(w, "Load", "Preset");
        h.pick(path);
        CHECK(alertText(w) == "Preset read error.");
        dismiss(w);
        CHECK(h.values == v);
    }
}

TEST_CASE("File: Load of a pattern, a Song and a kit, one undo step each", "[ui][files][undo]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);
    const plugin::ParamValues v = plugin::defaultValues();
    plugin::NonParamState x;
    x.seq.cur().steps = 24;
    x.seq.cur().edit(0).pat[3] = 9;
    x.seq.patterns[4].edit(1).pat[0] = 2;
    x.seq.used.set(4);
    x.seq.song.entries = {{4, 2, 0}, {0, 1, 150}};
    x.global.songMode = true;
    x.kit = h.state.kit;
    x.kit.inst[1].a = 300;
    plugin::sanitize(x);
    h.disk["/f/p.stympat"] = native(FileKind::Pattern, v, x);
    h.disk["/f/s.stymsng"] = native(FileKind::Song, v, x);
    h.disk["/f/k.stymkit"] = native(FileKind::Kit, v, x);
    h.disk["/f/x.json"] = R"({"kind":"pattern"})";

    fileMenu(w, "Load", "Pattern");
    CHECK(h.opens.back().title == "Load pattern");
    h.pick("/f/p.stympat");
    CHECK(h.state.seq.cur().sameContent(x.seq.cur()));
    CHECK(h.state.seq.steps() == 24);
    fileMenu(w, "Load", "Song");
    h.pick("/f/s.stymsng");
    CHECK(h.state.seq.song == x.seq.song);
    CHECK(h.state.global.songMode);
    CHECK(h.state.seq.used[4]);
    CHECK(h.state.seq.patterns[4].lane(1).pat[0] == 2);
    fileMenu(w, "Load", "Drum kit");
    CHECK(h.opens.back().title == "Load drum kit");
    h.pick("/f/k.stymkit");
    CHECK(h.state.kit == x.kit);
    CHECK(h.editor.undo.undoSize() == 3);

    w.tick(10);
    undoKey(w);
    CHECK(h.state.kit == ymengine::kKitBanks[0].kit);
    undoKey(w);
    CHECK(h.state.seq.song.empty());
    undoKey(w);
    CHECK(h.state.seq.cur().lanes[0] == nullptr);
    CHECK(h.state.seq.steps() == ymseq::kDefaultSteps);

    // A JSON file is refused like any file that is not one of STYM2149.
    const std::pair<const char*, const char*> refused[] = {
        {"Pattern", "Pattern read error."}, {"Song", "Song read error."}, {"Drum kit", "Not a valid drum kit file."}};
    for (const auto& [label, why] : refused) {
        CAPTURE(label);
        fileMenu(w, "Load", label);
        h.pick("/f/x.json");
        CHECK(alertText(w) == why);
        dismiss(w);
    }
}

TEST_CASE("Patches: a library from a newer version, read and reported", "[ui][files][patches]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);
    w.patches().show();
    // "stym" key of version 2 at the end of the file.
    std::string text = plugin::dumpPatchLibrary({plugin::snapPatch("Lead", h.values, h.state, 0)});
    const size_t end = text.find_last_of('}');
    h.disk["/f/l.json"] = text.substr(0, end) + ",\"stym\":{\"v\":2}" + text.substr(end);
    h.disk["/f/now.json"] = text;

    click(w, "libImport");
    h.pick("/f/l.json");
    CHECK(alertText(w) == "This file comes from a newer STYM2149.\nSettings this version does not know are not used.");
    dismiss(w);
    CHECK(w.patches().library().size() == 1);
    // File of this version: no alert.
    click(w, "libImport");
    h.pick("/f/now.json");
    CHECK(alertText(w).empty());
    CHECK(w.patches().library().size() == 2);
}

TEST_CASE("Patches: Import adds the patches of the file, Export writes the library", "[ui][files][patches]") {
    FileHost h;
    const std::vector<plugin::PatchEntry> mine = {plugin::snapPatch("Bass", h.values, h.state, 0)};
    h.patches = plugin::dumpPatchLibrary(mine);
    const std::vector<plugin::PatchEntry> theirs = {plugin::snapPatch("Lead", h.values, h.state, 0),
                                                    plugin::snapPatch("Pad", h.values, h.state, 0)};
    h.disk["/l/lib.json"] = plugin::dumpPatchLibrary(theirs);
    h.disk["/l/kit.json"] = R"({"kind":"drumkit","kit":{}})";
    MainWindow w(h);
    w.tick(0);
    w.patches().show();

    click(w, "libImport");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Import a patch library");
    h.pick("/l/lib.json");
    REQUIRE(w.patches().library().size() == 3);
    CHECK(plugin::patchName(w.patches().library()[2]) == "Pad");
    CHECK(plugin::parsePatchLibrary(h.patches)->size() == 3);

    click(w, "libImport");
    h.pick("/l/kit.json");
    CHECK(alertText(w) == "Not a valid patch library file.");
    dismiss(w);
    CHECK(w.patches().library().size() == 3);

    click(w, "libExport");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Export the patch library");
    CHECK(h.saves[0].name == "stym2149-patches.json");
    h.pick("/l/out");
    CHECK(h.disk["/l/out.json"] == h.patches);
}

TEST_CASE("File: New, at once on a new project, after a confirm otherwise (third DAW test, item 26)",
          "[ui][files]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);
    w.openMenu(1);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    REQUIRE_FALSE(list->items().empty());
    CHECK(list->items()[0].label == "New");
    CHECK(list->items()[0].key == "^N");
    CHECK(list->items()[1].label == "Load");
    w.desktop().closePopups();

    // Nothing to lose (another focused track is not a change): no box.
    h.values[size_t(paramIndex(Global::FocusTrack))] = 3;
    w.tick(10);
    ctrlN(w);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.values[size_t(paramIndex(Global::FocusTrack))] == 1.0f);

    // A setting, a note, a file to rewrite and an undo step: the box, Cancel first.
    h.p(0, Track::Volume) = 4;
    h.state.seq.cur().edit(1).pat[2] = 5;
    h.state.global.seqClock = ymengine::SeqClock::Midi;
    ++h.serial;
    h.editor.files[size_t(FileKind::Preset)] = "/home/u/song.json";
    h.editor.undo.push({});
    w.tick(20);
    ctrlN(w);
    CHECK(alertText(w) == "New project?\nUnsaved work will be lost.");
    pressKey(w, gemui::Key::Enter);  // Cancel
    CHECK(h.p(0, Track::Volume) == 4.0f);
    CHECK(h.state.seq.cur().lane(1).pat[2] == 5);

    w.openMenu(1);
    list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    const gemui::Rect row = list->itemRect(0);
    w.desktop().mouseMove({row.x + 10, row.y + row.h / 2}, {});
    w.desktop().mouseDown({row.x + 10, row.y + row.h / 2}, gemui::MouseButton::Left, {});
    w.desktop().mouseUp({row.x + 10, row.y + row.h / 2}, {});
    gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    const gemui::Rect ok = a->button(1).bounds();
    w.desktop().mouseMove({ok.x + 4, ok.y + 4}, {});
    w.desktop().mouseDown({ok.x + 4, ok.y + 4}, gemui::MouseButton::Left, {});
    w.desktop().mouseUp({ok.x + 4, ok.y + 4}, {});
    CHECK_FALSE(w.desktop().hasModal());
    // Every parameter and the whole non-automatable state at their defaults (the clock the host starts with);
    // nothing for Save to rewrite, no undo step back into the old project.
    CHECK(h.values == plugin::defaultValues());
    CHECK(h.state.seq.cur().lane(1).pat[2] < 0);
    CHECK(h.state.global.seqClock == plugin::NonParamState{}.global.seqClock);
    CHECK(h.editor.files[size_t(FileKind::Preset)].empty());
    CHECK_FALSE(h.editor.undo.canUndo());
    // A new project again: no box.
    w.tick(30);
    ctrlN(w);
    CHECK_FALSE(w.desktop().hasModal());
}

TEST_CASE("File: New stops the sequencer and disarms Rec, except in Host sync (sixth DAW test, item 51)",
          "[ui][files]") {
    FileHost h;
    h.state.global.seqClock = ymengine::SeqClock::Internal;
    ++h.serial;
    MainWindow w(h);
    w.tick(0);
    h.editor.record = true;
    ctrlN(w);  // nothing to lose: no box
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.stops == 1);
    CHECK_FALSE(h.editor.record);

    // Host sync: the host's transport plays the sequencer, New leaves it alone (after the box: Host sync is not the
    // clock of a new project here).
    h.state.global.seqClock = ymengine::SeqClock::Host;
    ++h.serial;
    w.tick(10);
    h.editor.record = true;
    ctrlN(w);
    gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    const gemui::Rect ok = a->button(1).bounds();
    w.desktop().mouseMove({ok.x + 4, ok.y + 4}, {});
    w.desktop().mouseDown({ok.x + 4, ok.y + 4}, gemui::MouseButton::Left, {});
    w.desktop().mouseUp({ok.x + 4, ok.y + 4}, {});
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.stops == 1);
    CHECK_FALSE(h.editor.record);
}

TEST_CASE("File: Import holds MIDI file, Tracker song and SNDH / YM file, no JSON entry, no Export entry",
          "[ui][files]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);
    std::vector<std::string> labels;
    for (const gemui::MenuItem& m : fileEntry(w, "Import").sub) labels.push_back(m.label);
    CHECK(labels == std::vector<std::string>{"MIDI file", "Tracker song", "SNDH / YM file"});
    w.openMenu(1);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    for (const gemui::MenuItem& m : list->items()) CHECK(m.label != "Export");
    w.desktop().closePopups();
}

TEST_CASE("File: Play... of a STYM2149 file, played apart from the project", "[ui][files]") {
    FileHost h;
    plugin::ParamValues v = plugin::defaultValues();
    plugin::NonParamState x;
    v[size_t(paramIndex(2, Track::Volume))] = 5;
    x.seq.cur().edit(2).pat[0] = 7;
    x.seq.song.entries = {{0, 2, 0}};
    plugin::sanitize(x);
    for (FileKind k : {FileKind::Preset, FileKind::Pattern, FileKind::Song, FileKind::Kit})
        h.disk["/s/f" + std::string(plugin::nativeExtension(k))] = native(k, v, x);
    h.disk["/s/p.json"] = R"({"app":"STYM2149","kind":"pattern","v":1})";
    h.disk["/s/t.ym"] = "YM3!";
    h.disk["/s/junk.bin"] = "junk";
    std::string newer = h.disk["/s/f.stym"];
    newer[4] = char(plugin::kStateVersion + 1);
    h.disk["/s/new.stym"] = newer;
    h.state.seq.cur().edit(0).pat[1] = 3;  // the project of the plugin
    ++h.serial;
    const plugin::NonParamState before = h.state;
    const plugin::ParamValues valuesBefore = h.values;
    MainWindow w(h);
    w.tick(0);

    // One entry, one dialogue for every format.
    const gemui::MenuItem play = fileEntry(w, "Play...");
    CHECK(play.sub.empty());
    fileMenuPath(w, {"Play..."});
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Play a file");
    CHECK(h.opens[0].patterns ==
          "*.ym;*.ym2;*.ym3;*.ym5;*.ym6;*.sndh;*.snd;*.mys;*.myv;*.ymt;*.tpm;*.xm;*.mod;*.s3m;*.okt;*.stym;*.stympat;"
          "*.stymsng");
    h.pick("/s/f.stym");
    CHECK(h.editor.playFile == "/s/f.stym");
    // A project: played as it is, from the start of its Song.
    REQUIRE(h.played.size() == 1);
    CHECK(h.played[0].values == v);
    CHECK(h.played[0].extras.seq.cur().sameContent(x.seq.cur()));
    CHECK(h.played[0].song);
    CHECK(h.played[0].name == "f.stym  -  STYM2149");
    CHECK(w.ymPlayerWindow().isOpen());
    CHECK(h.values == valuesBefore);
    CHECK(h.state.seq.cur().sameContent(before.seq.cur()));

    // A pattern: the current project with that pattern, played once.
    fileMenuPath(w, {"Play..."});
    CHECK(h.opens.back().path == h.editor.playFile);
    h.pick("/s/f.stympat");
    REQUIRE(h.played.size() == 2);
    CHECK(h.played.back().values == valuesBefore);
    CHECK(h.played.back().extras.seq.cur().sameContent(x.seq.cur()));
    CHECK_FALSE(h.played.back().song);
    // A Song: over the current project.
    fileMenuPath(w, {"Play..."});
    h.pick("/s/f.stymsng");
    CHECK(h.played.back().extras.seq.song == x.seq.song);
    CHECK(h.played.back().song);
    CHECK(h.played.size() == 3);

    // A YM dump goes to the player as before.
    fileMenuPath(w, {"Play..."});
    h.pick("/s/t.ym");
    CHECK(h.ymLoads == std::vector<std::string>{"t.ym"});
    CHECK(h.played.size() == 3);

    // Nothing to play, newer, unknown.
    const std::pair<const char*, const char*> refused[] = {
        {"/s/f.stymkit", "Nothing to play in this file.\nThis file holds a drum kit."},
        {"/s/p.json", "Unsupported file\n\nExpected a YM, SNDH, maxYMiser, YM-Tracker,\n"
                      "TTrak, XM, MOD, S3M, OKT or STYM2149 file."},
        {"/s/new.stym", "The file cannot be played.\n\nnew.stym\nIt comes from a newer STYM2149."},
        {"/s/junk.bin", "Unsupported file\n\nExpected a YM, SNDH, maxYMiser, YM-Tracker,\n"
                        "TTrak, XM, MOD, S3M, OKT or STYM2149 file."},
        {"/s/missing", "The file could not be read."}};
    for (const auto& [f, why] : refused) {
        CAPTURE(f);
        fileMenuPath(w, {"Play..."});
        h.pick(f);
        CHECK(alertText(w) == why);
        dismiss(w);
    }
    CHECK(h.played.size() == 3);
    CHECK(h.state.seq.cur().sameContent(before.seq.cur()));
}

// Configuration of the standalone application (Indus, 01/11/2026 18:25): File > Configuration > Load, Save and
// Save as, above Quit; the file written or read becomes the configuration reopened at the next start.
TEST_CASE("File: Configuration Load, Save and Save as in the standalone application, none in a host",
          "[ui][files]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);
    std::vector<std::string> labels;
    const gemui::MenuItem config = fileEntry(w, "Configuration");
    for (const gemui::MenuItem& m : config.sub) labels.push_back(m.label);
    CHECK(labels == std::vector<std::string>{"Load", "Save", "Save as"});
    {
        // Just above Quit.
        w.openMenu(1);
        const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
        REQUIRE(list);
        const std::vector<gemui::MenuItem>& items = list->items();
        REQUIRE(items.size() >= 2);
        CHECK(items[items.size() - 2].label == "Configuration");
        CHECK(items.back().label == "Quit");
        w.desktop().closePopups();
    }

    // Save: no file yet, the dialogue; the extension added; the file kept as the configuration.
    h.p(0, Track::Volume) = 9;
    w.tick(10);
    fileMenu(w, "Configuration", "Save");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Save configuration as");
    CHECK(h.saves[0].patterns == "*.stymcfg");
    CHECK(h.saves[0].name == "stym2149.stymcfg");
    h.pick("/home/u/studio");
    REQUIRE(h.disk.count("/home/u/studio.stymcfg"));
    CHECK(h.disk["/home/u/studio.stymcfg"] == h.configuration());
    CHECK(h.configFile == "/home/u/studio.stymcfg");
    CHECK(h.keptConfig == h.disk["/home/u/studio.stymcfg"]);
    // Save again: the same file, without a dialogue.
    h.p(0, Track::Volume) = 5;
    w.tick(20);
    fileMenu(w, "Configuration", "Save");
    CHECK(h.saves.size() == 1);
    CHECK(h.disk["/home/u/studio.stymcfg"] == h.configuration());
    // Save as: the dialogue, on the current file.
    fileMenu(w, "Configuration", "Save as");
    REQUIRE(h.saves.size() == 2);
    CHECK(h.saves[1].path == "/home/u/studio.stymcfg");
    h.pick("/home/u/other.stymcfg");
    CHECK(h.configFile == "/home/u/other.stymcfg");
    // Write refused: alert, the configuration stays the former one.
    fileMenu(w, "Configuration", "Save as");
    h.pick("/ro/x.stymcfg");
    CHECK(alertText(w) == "The file could not be written:\n/ro/x.stymcfg");
    dismiss(w);
    CHECK(h.configFile == "/home/u/other.stymcfg");

    // Load: the whole state replaced, the file kept as the configuration.
    const std::string saved = h.disk["/home/u/studio.stymcfg"];
    h.p(0, Track::Volume) = 1;
    w.tick(30);
    fileMenu(w, "Configuration", "Load");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Load configuration");
    CHECK(h.opens[0].patterns == "*.stymcfg;*.stym");
    CHECK(h.opens[0].path == "/home/u/other.stymcfg");
    h.pick("/home/u/studio.stymcfg");
    CHECK(h.p(0, Track::Volume) == 5);
    CHECK(h.configFile == "/home/u/studio.stymcfg");
    CHECK(h.keptConfig == saved);
    // A pattern file or an unreadable one: refused, nothing changed.
    h.disk["/d/p.stympat"] = native(FileKind::Pattern, h.values, h.state);
    fileMenu(w, "Configuration", "Load");
    h.pick("/d/p.stympat");
    CHECK(alertText(w).rfind("Configuration read error.\n", 0) == 0);
    dismiss(w);
    h.disk["/d/junk.stymcfg"] = "junk";
    fileMenu(w, "Configuration", "Load");
    h.pick("/d/junk.stymcfg");
    CHECK(alertText(w) == "Configuration read error.");
    dismiss(w);
    CHECK(h.configFile == "/home/u/studio.stymcfg");

    // In a host: no Configuration entry, the host keeps the state with its project.
    struct PluginHost : FileHost {
        bool standalone() const override { return false; }
    } inHost;
    MainWindow pw(inHost);
    pw.tick(0);
    pw.openMenu(1);
    const auto* list = dynamic_cast<const gemui::MenuList*>(pw.desktop().topPopup());
    REQUIRE(list);
    for (const gemui::MenuItem& m : list->items()) CHECK(m.label != "Configuration");
    pw.desktop().closePopups();
}
