// STYM2149 — File menu: Load, Save and Save as of a preset, a pattern, a Song and a kit, Import and Export
// of the Patches library, through the host's dialogues; alerts of the prototype; undo steps of the loads.
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

TEST_CASE("File: Save and Save as, proposed name, .json added, Save that rewrites the same file", "[ui][files]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);

    fileMenu(w, "Save", "Preset");
    REQUIRE(h.saves.size() == 1);
    CHECK(h.saves[0].title == "Save preset as");
    CHECK(h.saves[0].patterns == "*.json");
    CHECK(h.saves[0].path.empty());
    CHECK(h.saves[0].name == "stym2149-preset.json");
    h.pick("/home/u/my preset");
    REQUIRE(h.disk.count("/home/u/my preset.json"));
    CHECK(h.disk["/home/u/my preset.json"] == plugin::dumpPreset(h.values, h.state));
    CHECK(h.editor.files[size_t(FileKind::Preset)] == "/home/u/my preset.json");

    // Save: same file, without a dialogue.
    h.p(0, Track::Volume) = 4;
    w.tick(10);
    fileMenu(w, "Save", "Preset");
    CHECK(h.saves.size() == 1);
    CHECK(h.disk["/home/u/my preset.json"] == plugin::dumpPreset(h.values, h.state));
    // Save as: always the dialogue, on the current file.
    fileMenu(w, "Save as", "Preset");
    REQUIRE(h.saves.size() == 2);
    CHECK(h.saves[1].path == "/home/u/my preset.json");
    h.pick("/home/u/b.JSON");
    CHECK(h.disk.count("/home/u/b.JSON"));
    CHECK(h.editor.files[size_t(FileKind::Preset)] == "/home/u/b.JSON");

    // Pattern, Song and kit: each its own file and name.
    h.state.seq.cur().edit(1).pat[2] = 5;
    h.state.seq.song.entries = {{0, 3, 0}};
    h.state.kit.inst[0].a = 60;
    ++h.serial;
    w.tick(20);
    const std::pair<const char*, FileKind> kinds[] = {
        {"Pattern", FileKind::Pattern}, {"Song", FileKind::Song}, {"Drum kit", FileKind::Kit}};
    for (const auto& [label, kind] : kinds) {
        fileMenu(w, "Save", label);
        CHECK(h.saves.back().name == plugin::defaultFileName(kind));
        h.pick(std::string("/d/") + label);
    }
    CHECK(h.disk["/d/Pattern.json"] == plugin::dumpPatternFile(h.state.seq.cur()));
    CHECK(h.disk["/d/Song.json"] == plugin::dumpSongFile(h.state.seq, false));
    CHECK(h.disk["/d/Drum kit.json"] == plugin::dumpKitFile(h.state.kit));

    // Write refused: alert, file not kept; dialogue cancelled: nothing.
    fileMenu(w, "Save as", "Song");
    h.pick("/ro/song.json");
    CHECK(alertText(w) == "The file could not be written:\n/ro/song.json");
    dismiss(w);
    CHECK(h.editor.files[size_t(FileKind::Song)] == "/d/Song.json");
    fileMenu(w, "Save as", "Song");
    h.pending = nullptr;  // cancelled
    CHECK_FALSE(w.desktop().hasModal());
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
    h.disk["/p/a.json"] = plugin::dumpPreset(v, x);
    h.disk["/p/bad.json"] = "{";
    h.disk["/p/other.json"] = R"({"app":"PRISM","v":1})";

    fileMenu(w, "Load", "Preset");
    REQUIRE(h.opens.size() == 1);
    CHECK(h.opens[0].title == "Load preset");
    CHECK(h.opens[0].patterns == "*.json");
    h.pick("/p/a.json");
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.values == v);
    plugin::PluginState a, b;
    a.values = v;
    a.extras = x;
    plugin::sanitize(a.extras);
    b.values = h.values;
    b.extras = h.state;
    CHECK(plugin::encodeState(a) == plugin::encodeState(b));
    CHECK(h.editor.undo.undoSize() == 0);  // loadPresetObj: no undo step
    // Load does not change the file of Save.
    CHECK(h.editor.files[size_t(FileKind::Preset)].empty());

    const std::pair<const char*, const char*> refused[] = {{"/p/bad.json", "Preset read error."},
                                                           {"/p/other.json", "Not a STYM2149 preset."},
                                                           {"/p/missing.json", "Preset read error."}};
    for (const auto& [path, why] : refused) {
        CAPTURE(path);
        fileMenu(w, "Load", "Preset");
        h.pick(path);
        CHECK(alertText(w) == why);
        dismiss(w);
        CHECK(h.values == v);
    }
}

TEST_CASE("File: Load of a pattern, a Song and a kit, one undo step each", "[ui][files][undo]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);
    ymseq::Project src;
    src.cur().steps = 24;
    src.cur().edit(0).pat[3] = 9;
    src.patterns[4].edit(1).pat[0] = 2;
    src.used.set(4);
    src.song.entries = {{4, 2, 0}, {0, 1, 150}};
    ymengine::KitParams kit = h.state.kit;
    kit.inst[1].a = 300;
    h.disk["/f/p.json"] = plugin::dumpPatternFile(src.cur());
    h.disk["/f/s.json"] = plugin::dumpSongFile(src, true);
    h.disk["/f/k.json"] = plugin::dumpKitFile(kit);
    h.disk["/f/x.json"] = R"({"kind":"other"})";

    fileMenu(w, "Load", "Pattern");
    CHECK(h.opens.back().title == "Load pattern");
    h.pick("/f/p.json");
    CHECK(h.state.seq.cur().sameContent(src.cur()));
    CHECK(h.state.seq.steps() == 24);
    fileMenu(w, "Load", "Song");
    h.pick("/f/s.json");
    CHECK(h.state.seq.song == src.song);
    CHECK(h.state.global.songMode);
    CHECK(h.state.seq.used[4]);
    CHECK(h.state.seq.patterns[4].lane(1).pat[0] == 2);
    fileMenu(w, "Load", "Drum kit");
    CHECK(h.opens.back().title == "Load drum kit");
    h.pick("/f/k.json");
    CHECK(h.state.kit == kit);
    CHECK(h.editor.undo.undoSize() == 3);

    w.tick(10);
    undoKey(w);
    CHECK(h.state.kit == ymengine::kKitBanks[0].kit);
    undoKey(w);
    CHECK(h.state.seq.song.empty());
    undoKey(w);
    CHECK(h.state.seq.cur().lanes[0] == nullptr);
    CHECK(h.state.seq.steps() == ymseq::kDefaultSteps);

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

TEST_CASE("File: files from a newer version, read and reported", "[ui][files]") {
    FileHost h;
    MainWindow w(h);
    w.tick(0);
    w.patches().show();
    const std::string newer = "This file comes from a newer STYM2149.\nSettings this version does not know are not used.";
    // "stym" key of version 2 at the end of each file.
    auto later = [](std::string text) {
        const size_t end = text.find_last_of('}');
        return text.substr(0, end) + ",\"stym\":{\"v\":2}" + text.substr(end);
    };
    ymseq::Project src;
    src.cur().edit(0).pat[3] = 9;
    src.song.entries = {{0, 3, 0}};
    ymengine::KitParams kit = h.state.kit;
    kit.inst[1].a = 300;
    h.disk["/f/p.json"] = later(plugin::dumpPatternFile(src.cur()));
    h.disk["/f/s.json"] = later(plugin::dumpSongFile(src, false));
    h.disk["/f/k.json"] = later(plugin::dumpKitFile(kit));
    h.disk["/f/l.json"] = later(plugin::dumpPatchLibrary({plugin::snapPatch("Lead", h.values, h.state, 0)}));
    h.disk["/f/now.json"] = plugin::dumpPatternFile(src.cur());

    fileMenu(w, "Load", "Pattern");
    h.pick("/f/p.json");
    CHECK(alertText(w) == newer);
    dismiss(w);
    CHECK(h.state.seq.cur().sameContent(src.cur()));
    fileMenu(w, "Load", "Song");
    h.pick("/f/s.json");
    CHECK(alertText(w) == newer);
    dismiss(w);
    CHECK(h.state.seq.song == src.song);
    fileMenu(w, "Load", "Drum kit");
    h.pick("/f/k.json");
    CHECK(alertText(w) == newer);
    dismiss(w);
    CHECK(h.state.kit == kit);
    click(w, "libImport");
    h.pick("/f/l.json");
    CHECK(alertText(w) == newer);
    dismiss(w);
    CHECK(w.patches().library().size() == 1);
    // File of this version: no alert.
    fileMenu(w, "Load", "Pattern");
    h.pick("/f/now.json");
    CHECK(alertText(w).empty());
}

TEST_CASE("Patches: Import adds the patches of the file, Export writes the library", "[ui][files][patches]") {
    FileHost h;
    const std::vector<plugin::PatchEntry> mine = {plugin::snapPatch("Bass", h.values, h.state, 0)};
    h.patches = plugin::dumpPatchLibrary(mine);
    const std::vector<plugin::PatchEntry> theirs = {plugin::snapPatch("Lead", h.values, h.state, 0),
                                                    plugin::snapPatch("Pad", h.values, h.state, 0)};
    h.disk["/l/lib.json"] = plugin::dumpPatchLibrary(theirs);
    h.disk["/l/kit.json"] = plugin::dumpKitFile(h.state.kit);
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
