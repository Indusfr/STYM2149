// STYM2149 — Patches window: opening (button, Ctrl+P, Edit menu), Save current, loading into the focused
// track (one undo step), menu of a patch (Load, Rename, Overwrite, Delete), shared library, unreadable file never
// overwritten, Import and Export without a host dialogue (test_file_menu.cpp).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string>

#include "plugincore/Parameters.h"
#include "plugincore/Patches.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using plugin::paramIndex;
using plugin::Track;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::string patches;
    int writes = 0;

    float param(int i) const override { return values[size_t(i)]; }
    void beginGesture(int) override {}
    void setParam(int i, float v) override { values[size_t(i)] = plugin::clampToSpec(plugin::paramSpec(i), v); }
    void endGesture(int) override {}
    uint64_t extrasVersion() const override { return serial; }
    plugin::NonParamState extras() const override { return state; }
    void editExtras(const std::function<void(plugin::NonParamState&)>& edit) override {
        edit(state);
        ++serial;
    }
    void screenMidi(uint8_t, uint8_t, uint8_t) override {}
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void seqCommand(SeqCommand, int, int, int) override {}
    const plugin::EngineStatus* engineStatus() override { return nullptr; }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return true; }
    std::string midiStatus() const override { return "none"; }
    std::string version() const override { return "test"; }
    void quit() override {}
    bool hasAudioSettings() const override { return false; }
    void openAudioSettings() override {}
    void loadDigidrum(int) override {}
    std::string preference(std::string_view, std::string_view fallback) const override { return std::string(fallback); }
    void setPreference(std::string_view, std::string_view) override {}
    std::string patchLibrary() const override { return patches; }
    void setPatchLibrary(const std::string& json) override {
        patches = json;
        ++writes;
    }

    float& p(int track, Track f) { return values[size_t(paramIndex(track, f))]; }
    std::vector<plugin::PatchEntry> library() const {
        const auto lib = plugin::parsePatchLibrary(patches);
        REQUIRE(lib);
        return *lib;
    }
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void press(MainWindow& w, gemui::Point p, gemui::MouseButton b = gemui::MouseButton::Left) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, b, {});
    w.desktop().mouseUp(p, {});
}

void click(MainWindow& w, const std::string& id, gemui::MouseButton b = gemui::MouseButton::Left) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, centre(*r), b);
}

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

bool letter(MainWindow& w, char32_t ch, bool ctrl = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = ch;
    e.code = int(ch - U'a' + U'A');
    e.mods.ctrl = ctrl;
    return w.desktop().keyDown(e);
}

// Prompt box open: the proposed text erased, `text` typed, Enter.
void answer(MainWindow& w, const std::string& text) {
    REQUIRE(w.desktop().hasModal());
    for (int i = 0; i < 40; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) {
        gemui::KeyEvent e;
        e.key = gemui::Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
    key(w, gemui::Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
}

// Entry `index` of a patch's menu (Load, Rename, Overwrite, ---, Delete), opened below its button.
void menuPick(MainWindow& w, int patch, int index) {
    const std::string id = "libPatch" + std::to_string(patch);
    click(w, id, gemui::MouseButton::Right);
    REQUIRE(w.desktop().hasPopups());
    const gemui::Rect r = *w.rectOf(id);
    const int top = r.bottom() - 2;  // the menu's frame on the button's bottom rule
    const int sep = index >= 3 ? 8 - 20 : 0;  // 8 px rule in place of an entry
    press(w, {r.x + 20, top + 2 + 20 * index + sep + 10});
    CHECK_FALSE(w.desktop().hasPopups());
}

}  // namespace

TEST_CASE("Patches: button, Ctrl+P and Edit menu open the window; empty library", "[ui][patches]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.patches().isOpen());
    click(w, "openLibBtn");
    REQUIRE(w.patches().isOpen());
    CHECK_FALSE(w.desktop().hasModal());  // no more "not available yet" alert
    CHECK(w.patches().titleBar().title() == "PATCHES");
    // CSS boxes of the prototype at 100 %: the note wraps below the buttons, the empty library says so.
    CHECK(w.rectOf("libWin") == gemui::Rect{160, 160, 360, 166});
    CHECK(w.rectOf("libSave") == gemui::Rect{174, 200, 108, 24});
    CHECK(w.rectOf("libExport") == gemui::Rect{364, 200, 66, 24});
    CHECK(w.rectOf("libHint")->y == 280);
    key(w, gemui::Key::Escape);
    CHECK_FALSE(w.patches().isOpen());
    CHECK(letter(w, U'p', true));
    CHECK(w.patches().isOpen());
    key(w, gemui::Key::Escape);
    w.openMenu(2);  // Edit > Patches (seventh entry since Effects, below the bar)
    const gemui::Rect edit = *w.rectOf("mEdit");
    press(w, {edit.x + 20, edit.bottom() + 2 + 6 * 20 + 10});
    CHECK(w.patches().isOpen());

    // Import and Export: host dialogues, which it has none of here (test_file_menu.cpp tries them).
    click(w, "libImport");
    CHECK_FALSE(w.desktop().hasModal());
    click(w, "libExport");
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.writes == 0);
}

TEST_CASE("Patches: Save current, loading into the focused track, one undo step", "[ui][patches][undo]") {
    TestHost h;
    h.p(0, Track::Volume) = 9;
    h.p(0, Track::Generator) = 1;
    h.p(0, Track::Level) = 40;
    h.state.tracks[0].patch.ornamentOn = true;
    h.state.tracks[0].patch.ornamentPitch[1] = 7;
    h.state.tracks[0].arp.length = 5;
    MainWindow w(h);
    w.patches().show();

    // Proposed name "Patch 1", kept as it is.
    click(w, "libSave");
    REQUIRE(w.desktop().hasModal());
    key(w, gemui::Key::Enter);
    REQUIRE(h.library().size() == 1);
    CHECK(plugin::patchName(h.library()[0]) == "Patch 1");
    REQUIRE(w.rectOf("libPatch0"));
    CHECK(w.rectOf("libPatch0")->y == 280);
    CHECK_FALSE(w.rectOf("libHint"));
    // An empty name saves nothing.
    click(w, "libSave");
    answer(w, "");
    CHECK(h.library().size() == 1);
    CHECK(h.editor.undo.undoSize() == 0);

    // Track 2 focused: the patch is loaded into it (parameters, Modulation, arpeggio), one undo step.
    h.values[size_t(paramIndex(plugin::Global::FocusTrack))] = 2;
    w.tick(0);
    click(w, "libPatch0");
    CHECK(h.p(1, Track::Volume) == 9.0f);
    CHECK(h.p(1, Track::Generator) == 1.0f);
    CHECK(h.p(1, Track::Level) == 40.0f);
    CHECK(h.state.tracks[1].patch.ornamentOn);
    CHECK(h.state.tracks[1].patch.ornamentPitch[1] == 7);
    CHECK(h.state.tracks[1].arp.length == 5);
    CHECK(h.p(0, Track::Volume) == 9.0f);  // track 1 does not move
    CHECK(h.editor.undo.undoSize() == 1);
    letter(w, U'z', true);
    CHECK(h.p(1, Track::Volume) == 13.0f);
    CHECK(h.p(1, Track::Level) == 75.0f);
    CHECK_FALSE(h.state.tracks[1].patch.ornamentOn);
    CHECK(h.state.tracks[1].arp.length == 3);
}

TEST_CASE("Patches: menu of a patch, Load, Rename, Overwrite, Delete", "[ui][patches]") {
    TestHost h;
    std::vector<plugin::PatchEntry> lib;
    for (const char* n : {"Bass", "Lead"}) lib.push_back(plugin::snapPatch(n, h.values, h.state, 0));
    h.patches = plugin::dumpPatchLibrary(lib);
    MainWindow w(h);
    w.patches().show();
    REQUIRE(w.patches().library().size() == 2);
    CHECK(w.rectOf("libPatch1")->x == w.rectOf("libPatch0")->right() + 6);

    menuPick(w, 1, 1);  // Rename
    answer(w, "Lead 2");
    CHECK(plugin::patchName(h.library()[1]) == "Lead 2");

    h.p(0, Track::NoisePeriod) = 21;
    ++h.serial;
    w.tick(0);
    menuPick(w, 0, 2);  // Overwrite: the sound of the focused track, under the same name
    CHECK(plugin::patchName(h.library()[0]) == "Bass");
    CHECK(h.library()[0].find("patch")->find("NP")->number() == 21);

    h.p(0, Track::NoisePeriod) = 3;
    w.tick(0);
    menuPick(w, 0, 0);  // Load
    CHECK(h.p(0, Track::NoisePeriod) == 21.0f);

    menuPick(w, 0, 4);  // Delete
    REQUIRE(h.library().size() == 1);
    CHECK(plugin::patchName(h.library()[0]) == "Lead 2");
    CHECK(w.rectOf("libPatch0"));
    CHECK_FALSE(w.rectOf("libPatch1"));
}

TEST_CASE("Patches: long names; a change reads the shared library again first", "[ui][patches]") {
    TestHost h;
    std::vector<plugin::PatchEntry> lib;
    for (const char* n : {"Bass", "Lead"}) lib.push_back(plugin::snapPatch(n, h.values, h.state, 0));
    h.patches = plugin::dumpPatchLibrary(lib);
    MainWindow w(h);
    w.patches().show();

    // No limit on the name (the prototype had none); its button is cut to the window with "...".
    const std::string longName = "A very long patch name, well over thirty characters";
    menuPick(w, 1, 1);  // Rename
    answer(w, longName);
    REQUIRE(h.library().size() == 2);
    CHECK(plugin::patchName(h.library()[1]) == longName);
    CHECK(w.rectOf("libPatch1")->right() <= w.rectOf("libWin")->right() - 14);

    // Another instance removes Bass: Delete on the stale second button changes nothing, the list follows the file.
    h.patches = plugin::dumpPatchLibrary({h.library()[1]});
    const int writes = h.writes;
    menuPick(w, 1, 4);  // Delete
    CHECK(h.writes == writes);
    REQUIRE(h.library().size() == 1);
    CHECK(plugin::patchName(h.library()[0]) == longName);
    CHECK_FALSE(w.rectOf("libPatch1"));

    // Another instance adds a patch: Save current keeps it.
    std::vector<plugin::PatchEntry> two = h.library();
    two.push_back(plugin::snapPatch("Other", h.values, h.state, 0));
    h.patches = plugin::dumpPatchLibrary(two);
    click(w, "libSave");
    answer(w, "New");
    REQUIRE(h.library().size() == 3);
    CHECK(plugin::patchName(h.library()[1]) == "Other");
    CHECK(plugin::patchName(h.library()[2]) == "New");
}

TEST_CASE("Patches: Export reads the shared library again and the buttons follow it", "[ui][patches]") {
    TestHost h;
    std::vector<plugin::PatchEntry> lib;
    for (const char* n : {"Lead", "Bass", "Pad"}) lib.push_back(plugin::snapPatch(n, h.values, h.state, 0));
    h.patches = plugin::dumpPatchLibrary(lib);
    MainWindow w(h);
    w.patches().show();
    REQUIRE(w.rectOf("libPatch2"));
    // Another instance removes Lead, then Export: the third button used to stay, every button one patch off, so a
    // Delete or Overwrite from its menu reached the next patch.
    h.patches = plugin::dumpPatchLibrary({lib[1], lib[2]});
    click(w, "libExport");
    CHECK_FALSE(w.rectOf("libPatch2"));
    menuPick(w, 1, 4);  // Delete on the second button: Pad
    REQUIRE(h.library().size() == 1);
    CHECK(plugin::patchName(h.library()[0]) == "Bass");
}

TEST_CASE("Patches: shared library, read again on opening; unreadable file never overwritten", "[ui][patches]") {
    TestHost h;
    {
        MainWindow a(h);
        a.patches().show();
        click(a, "libSave");
        answer(a, "Shared");
    }
    MainWindow b(h);
    REQUIRE(b.patches().isOpen());  // reopened with the session
    REQUIRE(b.patches().library().size() == 1);
    CHECK(plugin::patchName(b.patches().library()[0]) == "Shared");

    h.patches = "{ not json";
    const int writes = h.writes;
    key(b, gemui::Key::Escape);
    b.patches().show();
    CHECK(b.patches().library().empty());
    CHECK(b.rectOf("libHint"));
    click(b, "libSave");
    key(b, gemui::Key::Enter);  // proposed name
    CHECK(b.desktop().hasModal());  // "could not be read"
    key(b, gemui::Key::Enter);
    CHECK(h.patches == "{ not json");
    CHECK(h.writes == writes);
}
