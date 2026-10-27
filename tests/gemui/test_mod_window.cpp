// STYM2149 — Modulation window: opening (button, Ctrl+R, Edit menu), on/off, length, Loop / Hold, rate,
// Clear, drawing with the mouse (pitch, NSE, VOL), VOL setting by slider, keyboard, undo.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using pluginui::OrnGrid;
using plugin::paramIndex;
using plugin::Track;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;

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
    void setPatchLibrary(const std::string& json) override { patches = json; }

    std::string patches;

    const ymengine::TrackPatch& patch(int t = 0) const { return state.tracks[size_t(t)].patch; }
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void press(MainWindow& w, gemui::Point p, gemui::MouseButton b = gemui::MouseButton::Left) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, b, {});
    w.desktop().mouseUp(p, {});
}

void click(MainWindow& w, const std::string& id) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, centre(*r));
}

// Drag from a to b through the given points.
void drag(MainWindow& w, std::initializer_list<gemui::Point> path) {
    const gemui::Point first = *path.begin();
    w.desktop().mouseMove(first, {});
    w.desktop().mouseDown(first, gemui::MouseButton::Left, {});
    gemui::Point last = first;
    for (const gemui::Point& p : path) {
        w.desktop().mouseDrag(p, {});
        last = p;
    }
    w.desktop().mouseUp(last, {});
}

bool key(MainWindow& w, gemui::Key k, bool shift = false) {
    gemui::KeyEvent e;
    e.key = k;
    e.mods.shift = shift;
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

void type(MainWindow& w, const std::string& id, const std::string& text) {
    click(w, id);
    for (int i = 0; i < 4; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) {
        gemui::KeyEvent e;
        e.key = gemui::Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
    key(w, gemui::Key::Enter);
}

void pick(MainWindow& w, const std::string& anchor, int entries, int index) {
    const auto r = w.rectOf(anchor);
    REQUIRE(r);
    REQUIRE(w.desktop().hasPopups());
    const int h = 4 + 20 * entries;
    int top = r->bottom() - 2;
    if (top + h > MainWindow::kHeight - 4) top = std::max(4, r->y - h + 2);
    press(w, {r->x + 20, top + 2 + 20 * index + 10});
    CHECK_FALSE(w.desktop().hasPopups());
}

void turnOn(TestHost& h) { h.state.tracks[0].patch.ornamentOn = true; }

}  // namespace

TEST_CASE("Modulation: button, Ctrl+R and Edit menu open the window; not applicable on percussion", "[ui][orn]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.modulation().isOpen());
    click(w, "openOrnBtn");
    REQUIRE(w.modulation().isOpen());
    CHECK_FALSE(w.desktop().hasModal());  // no more "not available yet" alert
    CHECK(w.modulation().titleBar().title() == "MODULATION - 1");
    // Off: the row, controls disabled, at (140, 140); the place of the On button (boxes of the prototype). Below it,
    // the settings of the sequences, 152 px high, 724 px wide: the slider of the row takes that width; then
    // the LFO, off: 42 px high, its label and button only.
    CHECK(w.rectOf("ornWin")->x == 140);
    CHECK(w.rectOf("ornWin")->y == 140);
    CHECK(w.rectOf("ornWin")->h == 78 + 152 + 42);
    CHECK(w.rectOf("ornOn")->x == 154);
    CHECK(w.rectOf("ornOn")->y == 180);
    CHECK(w.rectOf("ornLen")->w > 110);
    CHECK_FALSE(w.rectOf("seqxGrid"));
    CHECK_FALSE(w.rectOf("orngrid"));
    click(w, "ornClear");  // disabled
    click(w, "ornLoop");
    CHECK(h.patch().ornamentLoop);
    key(w, gemui::Key::Escape);
    CHECK_FALSE(w.modulation().isOpen());
    CHECK(letter(w, U'r', true));
    CHECK(w.modulation().isOpen());
    key(w, gemui::Key::Escape);
    w.openMenu(2);  // Edit > Modulation (fifth entry, below the bar)
    const gemui::Rect edit = *w.rectOf("mEdit");
    press(w, {edit.x + 20, edit.bottom() + 2 + 4 * 20 + 10});
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK(w.modulation().isOpen());

    // Percussion track: On disabled, Ctrl+R has no effect.
    w.pickGenerator(int(ymengine::Generator::Digidrum));
    w.tick(0);
    click(w, "ornOn");
    CHECK_FALSE(h.patch().ornamentOn);
    key(w, gemui::Key::Escape);
    CHECK(letter(w, U'r', true));
    CHECK_FALSE(w.modulation().isOpen());
}

TEST_CASE("Modulation: on/off, length, Loop / Hold, rate, Clear", "[ui][orn]") {
    TestHost h;
    MainWindow w(h);
    w.modulation().show();
    click(w, "ornOn");
    CHECK(h.patch().ornamentOn);
    // On: the notes and the grid; window as wide as the notes (131 characters of 6 px). The notes of the sequence
    // rows join the table's, three lines; the rows are inside the table, between the pitch rows and NSE / VOL
    // (438 px). Below, the settings (152 px) and the LFO, off (42 px).
    CHECK(OrnGrid::kHeight == 438);
    CHECK(w.rectOf("ornWin") == gemui::Rect{140, 140, 814, 124 + 438 + 14 + 152 + 42});
    CHECK(w.rectOf("orngrid") == gemui::Rect{154, 264, OrnGrid::kWidth, OrnGrid::kHeight});
    CHECK(w.rectOf("ornLen")->w > 110);  // flex:1: the track takes the space

    type(w, "ornLenv", "12");
    CHECK(h.patch().ornamentLength == 12);
    type(w, "ornLenv", "99");
    CHECK(h.patch().ornamentLength == 64);
    click(w, "ornLoop");
    CHECK_FALSE(h.patch().ornamentLoop);
    click(w, "ornRate");
    pick(w, "ornRate", 4, 2);
    CHECK(h.patch().ornamentRate == 3);

    h.state.tracks[0].patch.ornamentPitch[4] = 7;
    h.state.tracks[0].patch.ornamentNoise[4] = 1;
    h.state.tracks[0].patch.ornamentVolume[4] = 3;
    ++h.serial;
    w.tick(0);
    click(w, "ornClear");
    CHECK(h.patch().ornamentPitch[4] == 0);
    CHECK(h.patch().ornamentNoise[4] == 0);
    CHECK(h.patch().ornamentVolume[4] == 15);

    click(w, "ornOn");
    CHECK_FALSE(h.patch().ornamentOn);
    CHECK_FALSE(w.rectOf("orngrid"));
}

TEST_CASE("Modulation: drawing pitch, noise and volume, one undo step per stroke", "[ui][orn][undo]") {
    TestHost h;
    turnOn(h);
    MainWindow w(h);
    w.modulation().show();
    const OrnGrid& g = w.modulation().grid();

    // Pitch: press on +5 at step 1, drag to step 4 on -3; the cursor comes to the press.
    drag(w, {centre(g.cellRect(0, 12 - 5)), centre(g.cellRect(1, 12 - 5)), centre(g.cellRect(2, 12 + 3)),
             centre(g.cellRect(3, 12 + 3))});
    CHECK(h.patch().ornamentPitch[0] == 5);
    CHECK(h.patch().ornamentPitch[1] == 5);
    CHECK(h.patch().ornamentPitch[2] == -3);
    CHECK(h.patch().ornamentPitch[3] == -3);
    CHECK(h.editor.ornCur == 0);
    CHECK(h.editor.ornRow == 12 - 5);
    CHECK(h.editor.undo.undoSize() == 1);
    // Beyond the length (.dead), drawing writes too.
    press(w, centre(g.cellRect(20, 0)));
    CHECK(h.patch().ornamentPitch[20] == 12);
    CHECK(h.patch().ornamentLength == 8);

    // NSE: the first step decides (switch on), the drag follows; then switch off.
    drag(w, {centre(g.noiseRect(2)), centre(g.noiseRect(3)), centre(g.noiseRect(4))});
    CHECK(h.patch().ornamentNoise[2] == 1);
    CHECK(h.patch().ornamentNoise[3] == 1);
    CHECK(h.patch().ornamentNoise[4] == 1);
    drag(w, {centre(g.noiseRect(3)), centre(g.noiseRect(4))});
    CHECK(h.patch().ornamentNoise[2] == 1);
    CHECK(h.patch().ornamentNoise[3] == 0);
    CHECK(h.patch().ornamentNoise[4] == 0);

    // VOL: the height of the pointer in the cell gives the level (top 15, bottom 0), the drag follows it.
    const gemui::Rect v5 = g.volumeRect(5);
    drag(w, {{v5.x + 4, v5.y + 1}, {v5.x + 4, v5.y + v5.h / 2}, {v5.x + 4, v5.bottom() - 1}});
    CHECK(h.patch().ornamentVolume[5] == 0);
    CHECK(h.editor.ornCur == 5);
    press(w, {v5.x + 4, v5.y + 1});
    CHECK(h.patch().ornamentVolume[5] == 15);
    press(w, {v5.x + 4, v5.y + v5.h / 2});
    CHECK(h.patch().ornamentVolume[5] == 8);

    // Undo: one step per stroke.
    const size_t n = h.editor.undo.undoSize();
    CHECK(n == 7);
    letter(w, U'z', true);
    CHECK(h.patch().ornamentVolume[5] == 15);
    for (int i = 0; i < 3; ++i) letter(w, U'z', true);
    CHECK(h.patch().ornamentNoise[2] == 1);
    CHECK(h.patch().ornamentNoise[3] == 1);
    letter(w, U'z', true);
    CHECK(h.patch().ornamentNoise[2] == 0);
    letter(w, U'z', true);
    CHECK(h.patch().ornamentPitch[20] == 0);
    letter(w, U'z', true);
    CHECK(h.patch().ornamentPitch == std::array<int8_t, 64>{});
}

TEST_CASE("Modulation: right click on a VOL cell, setting by slider", "[ui][orn]") {
    TestHost h;
    turnOn(h);
    MainWindow w(h);
    w.modulation().show();
    const gemui::Rect v = w.modulation().grid().volumeRect(2);
    press(w, centre(v), gemui::MouseButton::Right);
    REQUIRE(w.desktop().hasPopups());
    CHECK(h.editor.ornCur == 2);
    // 96 px vertical slider below the cell: its bottom is worth 0.
    const gemui::Point bottom{v.x + 2 + 4 + 2 + 8, v.bottom() + 2 + 8 + 95};
    w.desktop().mouseMove(bottom, {});
    w.desktop().mouseDown(bottom, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(bottom, {});
    CHECK(h.patch().ornamentVolume[2] == 0);
}

TEST_CASE("Modulation: keyboard, window in front and on", "[ui][orn]") {
    TestHost h;
    turnOn(h);
    MainWindow w(h);
    w.modulation().show();
    // Initial cursor: step 1, row 0.
    CHECK(h.editor.ornCur == 0);
    CHECK(h.editor.ornRow == 12);
    for (int i = 0; i < 10; ++i) CHECK(key(w, gemui::Key::Right));
    CHECK(key(w, gemui::Key::Up));
    CHECK(key(w, gemui::Key::Up));
    CHECK(h.editor.ornCur == 10);
    CHECK(h.editor.ornRow == 10);
    // Enter: +2 at step 11, the length extends up to it.
    CHECK(key(w, gemui::Key::Enter));
    CHECK(h.patch().ornamentPitch[10] == 2);
    CHECK(h.patch().ornamentLength == 11);
    // N: noise of the step; Delete: the step goes back to zero.
    CHECK(letter(w, U'n'));
    CHECK(h.patch().ornamentNoise[10] == 1);
    h.state.tracks[0].patch.ornamentVolume[10] = 4;
    ++h.serial;
    w.tick(0);
    CHECK(key(w, gemui::Key::Delete));
    CHECK(h.patch().ornamentPitch[10] == 0);
    CHECK(h.patch().ornamentNoise[10] == 0);
    CHECK(h.patch().ornamentVolume[10] == 15);
    // Cursor bounds.
    for (int i = 0; i < 70; ++i) key(w, gemui::Key::Right);
    for (int i = 0; i < 30; ++i) key(w, gemui::Key::Down);
    CHECK(h.editor.ornCur == 63);
    CHECK(h.editor.ornRow == 24);
    // Shift + arrow: change of channel (track 2), the cursor stays.
    CHECK(h.values[size_t(paramIndex(plugin::Global::FocusTrack))] == 1.0f);
    key(w, gemui::Key::Right, true);
    CHECK(h.values[size_t(paramIndex(plugin::Global::FocusTrack))] == 2.0f);
    CHECK(h.editor.ornCur == 63);
    // Track 2 off: the keys no longer go to the table.
    CHECK_FALSE(h.patch(1).ornamentOn);
    const int before = h.patch(1).ornamentLength;
    key(w, gemui::Key::Enter);
    CHECK(h.patch(1).ornamentLength == before);
}

TEST_CASE("Modulation: the reopened window gets its place back", "[ui][orn]") {
    TestHost h;
    {
        MainWindow w(h);
        w.modulation().show();
        const gemui::Point bar{220, 152};
        w.desktop().mouseMove(bar, {});
        w.desktop().mouseDown(bar, gemui::MouseButton::Left, {});
        w.desktop().mouseDrag({240, 182}, {});
        w.desktop().mouseUp({240, 182}, {});
    }
    MainWindow w(h);
    CHECK(w.modulation().isOpen());
    CHECK(w.rectOf("ornWin")->x == 160);
    CHECK(w.rectOf("ornWin")->y == 170);
}

TEST_CASE("Modulation: a VOL slider opened on one track leaves another track focused under it alone", "[ui][orn]") {
    TestHost h;
    turnOn(h);
    h.state.tracks[1].patch.ornamentVolume[2] = 9;
    MainWindow w(h);
    w.modulation().show();
    const gemui::Rect v = w.modulation().grid().volumeRect(2);
    press(w, centre(v), gemui::MouseButton::Right);
    REQUIRE(w.desktop().hasPopups());
    // The host focuses track 2 (automation) with the slider open, then the slider is moved.
    h.values[size_t(paramIndex(plugin::Global::FocusTrack))] = 2.0f;
    w.tick(0);
    REQUIRE(w.desktop().hasPopups());
    const gemui::Point bottom{v.x + 2 + 4 + 2 + 8, v.bottom() + 2 + 8 + 95};
    w.desktop().mouseMove(bottom, {});
    w.desktop().mouseDown(bottom, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(bottom, {});
    CHECK(h.state.tracks[1].patch.ornamentVolume[2] == 9);
    CHECK(h.state.tracks[0].patch.ornamentVolume[2] == 15);
}

TEST_CASE("Modulation: a stroke cut short by an alert does not go on under a later right-button drag", "[ui][orn]") {
    TestHost h;
    turnOn(h);
    MainWindow w(h);
    w.modulation().show();
    const auto& g = w.modulation().grid();
    // A pitch stroke starts on step 1, then an alert opens (end of a background export) before the release.
    const gemui::Point p = centre(g.cellRect(0, 3));
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    gemui::Widget& box = w.desktop().openModal(std::make_unique<gemui::Widget>());
    w.desktop().mouseUp(p, {});
    w.desktop().closeModal(box);
    // The right button on a VOL cell (its slider opens), dragged over the pitch cells: no pitch is drawn.
    const auto before = h.patch().ornamentPitch;
    const gemui::Point v = centre(g.volumeRect(6));
    w.desktop().mouseMove(v, {});
    w.desktop().mouseDown(v, gemui::MouseButton::Right, {});
    w.desktop().mouseDrag(centre(g.cellRect(7, 5)), {});
    w.desktop().mouseDrag(centre(g.cellRect(8, 5)), {});
    w.desktop().mouseUp(centre(g.cellRect(8, 5)), {});
    CHECK(h.patch().ornamentPitch == before);
}
