// STYM2149 — Arpeggiator window (M5): opening (button, Ctrl+B, Edit menu), on/off, mode, rate, Track, Land, MIDI
// clock division, octaves, table (grid, number of steps, chords, keyboard), playing step, one track undo step per
// setting (D-129), SYNC / STOP of the MIDI clock (D-084), Sync to the tempo (D-256).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Pitch.h"

using namespace stym;
using pluginui::ArpGrid;
using pluginui::MainWindow;
using plugin::Global;
using plugin::paramIndex;
using plugin::Track;
using ymengine::ArpMode;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::optional<plugin::EngineStatus> pending;
    plugin::EngineStatus last;
    std::vector<std::array<uint8_t, 3>> midi;  // messages from the screen

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
    void screenMidi(uint8_t s, uint8_t d1, uint8_t d2) override { midi.push_back({s, d1, d2}); }
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void seqCommand(SeqCommand, int, int, int) override {}
    const plugin::EngineStatus* engineStatus() override {
        if (!pending) return nullptr;
        last = *pending;
        pending.reset();
        return &last;
    }
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

    float track(int t, Track f) const { return values[size_t(paramIndex(t, f))]; }
    float global(Global g) const { return values[size_t(paramIndex(g))]; }
    const ymengine::TrackArp& arp(int t = 0) const { return state.tracks[size_t(t)].arp; }
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void press(MainWindow& w, gemui::Point p) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(p, {});
}

void click(MainWindow& w, const std::string& id) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, centre(*r));
}

// Entry of a menu opened below a button (its frame on the button's bottom rule, or on its top rule above it when it
// does not fit; 20 px entries inside a 2 px border, D-396).
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

bool key(MainWindow& w, gemui::Key k, bool shift = false) {
    gemui::KeyEvent e;
    e.key = k;
    e.mods.shift = shift;
    return w.desktop().keyDown(e);
}

bool ctrl(MainWindow& w, char32_t ch, bool shift = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = ch;
    e.code = int(ch - U'a' + U'A');
    e.mods = {shift, true, false};
    return w.desktop().keyDown(e);
}

// Masked input: the shown value is erased digit by digit, then the new one is typed, then Enter.
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

void publish(TestHost& h, MainWindow& w, const std::function<void(plugin::EngineStatus&)>& fill) {
    plugin::EngineStatus t;
    t.serial = ++h.serial;
    fill(t);
    h.pending = t;
    w.tick(0);
}

}  // namespace

TEST_CASE("Arpeggiator: button, Ctrl+B and Edit menu open the window; not applicable on percussion", "[ui][arp]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.arpeggiator().isOpen());
    click(w, "openArpBtn");
    REQUIRE(w.arpeggiator().isOpen());
    CHECK_FALSE(w.desktop().hasModal());  // no more "not available yet" alert
    // Stopped: Off button and its note, 177 × 90 window at (80, 100) (boxes of the prototype).
    CHECK(w.rectOf("arpWin") == gemui::Rect{80, 100, 177, 90});
    CHECK(w.rectOf("arpOn") == gemui::Rect{94, 146, 45, 24});
    CHECK_FALSE(w.rectOf("arpModeSel"));
    CHECK(w.arpeggiator().titleBar().title() == "ARPEGGIATOR - 1");
    key(w, gemui::Key::Escape);
    CHECK_FALSE(w.arpeggiator().isOpen());
    CHECK(ctrl(w, U'b'));
    CHECK(w.arpeggiator().isOpen());
    key(w, gemui::Key::Escape);
    w.openMenu(2);  // Edit > Arpeggiator (second entry, below the bar)
    const gemui::Rect edit = *w.rectOf("mEdit");
    press(w, {edit.x + 20, edit.bottom() + 2 + 20 + 10});
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK(w.arpeggiator().isOpen());

    // Percussion track: button of the main window disabled, Ctrl+B has no effect, Off disabled.
    w.pickGenerator(int(ymengine::Generator::Chipdrum));
    w.tick(0);
    click(w, "arpOn");
    CHECK(h.track(0, Track::ArpOn) == 0.0f);
    key(w, gemui::Key::Escape);
    CHECK_FALSE(w.arpeggiator().isOpen());
    CHECK(ctrl(w, U'b'));
    CHECK_FALSE(w.arpeggiator().isOpen());
}

TEST_CASE("Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division", "[ui][arp]") {
    TestHost h;
    MainWindow w(h);
    w.arpeggiator().show();
    click(w, "arpOn");
    CHECK(h.track(0, Track::ArpOn) == 1.0f);
    CHECK(w.rectOf("arpWin") == gemui::Rect{80, 100, 380, 180});
    CHECK(w.rectOf("arpModeSel") == gemui::Rect{134, 176, 38, 24});
    CHECK(w.rectOf("arpRate") == gemui::Rect{142, 210, 129, 16});  // under Steps (item 162, D-371)
    CHECK(w.rectOf("arpGlideRate") == gemui::Rect{327, 206, 59, 24});
    CHECK(w.rectOf("arpLand") == gemui::Rect{394, 206, 52, 24});
    CHECK(w.rectOf("arpOct2") == gemui::Rect{195, 236, 31, 24});
    CHECK_FALSE(w.rectOf("arpDiv"));

    // Mode: menu of the six orders; Down.
    click(w, "arpModeSel");
    pick(w, "arpModeSel", 6, 1);
    CHECK(h.arp().mode == ArpMode::Down);
    // Rate on the keyboard (mask 999), Track, Land, octaves.
    type(w, "arpRatev", "120");
    CHECK(h.track(0, Track::ArpRate) == 120.0f);
    click(w, "arpGlideRate");
    click(w, "arpLand");
    click(w, "arpOct3");
    CHECK(h.arp().track);
    CHECK(h.arp().land);
    CHECK(h.arp().octaves == 3);

    // MIDI clock: the division replaces the rate (global parameter ArpDivision), in a menu button (D-290) whose
    // entries follow the musical order, Tick last: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64, Tick.
    h.values[size_t(paramIndex(Global::ArpClock))] = 1;
    w.tick(0);
    // Division on the row of the mode (D-295), the Rate row gone.
    CHECK(w.rectOf("arpWin") == gemui::Rect{80, 100, 320, 150});
    CHECK_FALSE(w.rectOf("arpRate"));
    CHECK(w.rectOf("arpDiv") == gemui::Rect{334, 176, 52, 24});
    CHECK(w.rectOf("arpDiv")->x > w.rectOf("arpSync")->right() + 8);
    click(w, "arpDiv");
    pick(w, "arpDiv", 9, 1);
    CHECK(h.global(Global::ArpDivision) == 1.0f);
    click(w, "arpDiv");
    pick(w, "arpDiv", 9, 7);
    CHECK(h.global(Global::ArpDivision) == 8.0f);  // 1/64, after the choices of the first versions (D-289)
    click(w, "arpDiv");
    pick(w, "arpDiv", 9, 8);
    CHECK(h.global(Global::ArpDivision) == 6.0f);  // Tick

    // Stop: the arpeggio leaves nothing sounding (allOff, controller 123).
    h.midi.clear();
    click(w, "arpOn");
    CHECK(h.track(0, Track::ArpOn) == 0.0f);
    REQUIRE(h.midi.size() == 1);
    CHECK(h.midi[0] == std::array<uint8_t, 3>{0xB0, 123, 0});
    CHECK(w.rectOf("arpWin") == gemui::Rect{80, 100, 177, 90});
}

TEST_CASE("Arpeggiator: Sync to the tempo, Division in place of Rate, lit and greyed with MIDI ext (D-256)",
          "[ui][arp]") {
    TestHost h;
    MainWindow w(h);
    w.arpeggiator().show();
    click(w, "arpOn");
    // On the Mode row, after the mode; the window keeps its width (the Rate row is wider).
    const auto mode = w.rectOf("arpModeSel"), sync = w.rectOf("arpSync");
    REQUIRE(sync);
    CHECK(sync->y == mode->y);
    CHECK(sync->x > mode->right());
    CHECK(w.rectOf("arpWin") == gemui::Rect{80, 100, 380, 180});

    // Sync: the global parameter Arp Sync, Division in place of Rate.
    click(w, "arpSync");
    CHECK(h.global(Global::ArpSync) == 1.0f);
    CHECK_FALSE(w.rectOf("arpRate"));
    CHECK(w.rectOf("arpDiv")->y == mode->y);  // on the row of the mode (D-295)
    // Tick is not offered with Sync (D-287): eight entries, 1/32T and 1/64 included (D-289).
    click(w, "arpDiv");
    pick(w, "arpDiv", 8, 3);
    CHECK(h.global(Global::ArpDivision) == 3.0f);
    click(w, "arpDiv");
    pick(w, "arpDiv", 8, 6);
    CHECK(h.global(Global::ArpDivision) == 7.0f);  // 1/32T
    click(w, "arpSync");
    CHECK(h.global(Global::ArpSync) == 0.0f);
    CHECK(w.rectOf("arpRate") == gemui::Rect{142, 210, 129, 16});  // under Steps (item 162, D-371)
    CHECK_FALSE(w.rectOf("arpDiv"));

    // MIDI ext: the MIDI clock decides; the button stays lit and does nothing.
    h.values[size_t(paramIndex(Global::ArpClock))] = 1;
    w.tick(0);
    click(w, "arpSync");
    CHECK(h.global(Global::ArpSync) == 0.0f);
    CHECK(w.rectOf("arpDiv"));
}

TEST_CASE("Arpeggiator: division menu with Sync, a Tick kept from the MIDI clock shown but not offered (D-287)",
          "[ui][arp]") {
    TestHost h;
    h.values[size_t(paramIndex(0, Track::ArpOn))] = 1;
    h.values[size_t(paramIndex(Global::ArpSync))] = 1;
    h.values[size_t(paramIndex(Global::ArpDivision))] = 6;  // Tick
    MainWindow w(h);
    w.arpeggiator().show();
    REQUIRE(w.rectOf("arpDiv"));
    click(w, "arpDiv");
    pick(w, "arpDiv", 8, 7);  // the last entry: 1/64
    CHECK(h.global(Global::ArpDivision) == 8.0f);
}

TEST_CASE("Arpeggiator: table (grid, number of steps, chords, keyboard, playing step)", "[ui][arp]") {
    TestHost h;
    h.values[size_t(paramIndex(0, Track::ArpOn))] = 1;
    h.state.tracks[0].arp.mode = ArpMode::Table;
    MainWindow w(h);
    w.arpeggiator().show();
    CHECK(w.rectOf("arpWin") == gemui::Rect{80, 100, 394, 500});
    CHECK(w.rectOf("arpgrid") == gemui::Rect{94, 290, 366, 278});  // +0 to +24 (D-322)
    CHECK(w.rectOf("arpLen") == gemui::Rect{142, 236, 294, 16});
    // Rate starts with Steps (thirteenth DAW test, item 162, D-371).
    CHECK(w.rectOf("arpRate")->x == 142);
    CHECK(w.rectOf("chordSel") == gemui::Rect{94, 258, 80, 24});
    CHECK_FALSE(w.rectOf("arpOct1"));
    const ArpGrid& g = w.arpeggiator().grid();
    constexpr int top = ArpGrid::kRows - 1;  // offset of the top row, +24

    // Press on a cell within the length: the step takes the row; beyond it (.dim), nothing.
    press(w, centre(g.cellRect(1, top - 9)));
    CHECK(h.arp().table[1] == 9);
    press(w, centre(g.cellRect(5, 0)));
    CHECK(h.arp().table[5] == 0);
    // Number of steps on the keyboard (mask 99, 1 to 16).
    type(w, "arpLenv", "6");
    CHECK(h.arp().length == 6);
    press(w, centre(g.cellRect(5, 0)));
    CHECK(h.arp().table[5] == 24);
    // Chord: the table and the length (Major 7: 0, 4, 7, 11).
    click(w, "chordSel");
    pick(w, "chordSel", int(ymengine::kChords.size()), 6);
    CHECK(h.arp().length == 4);
    CHECK(h.arp().table == std::array<int8_t, 16>{0, 4, 7, 11});

    // Keyboard, window in front: arrows for the cursor (Shift: the channel), Enter writes and extends the table.
    CHECK(key(w, gemui::Key::Right));
    CHECK(key(w, gemui::Key::Right));
    CHECK(key(w, gemui::Key::Right));
    CHECK(key(w, gemui::Key::Right));
    CHECK(key(w, gemui::Key::Right));
    CHECK(key(w, gemui::Key::Up));
    CHECK(key(w, gemui::Key::Up));
    CHECK(h.editor.arpCur == 5);
    CHECK(h.editor.arpRow == top - 2);
    CHECK(key(w, gemui::Key::Enter));
    CHECK(h.arp().table[5] == 2);
    CHECK(h.arp().length == 6);
    CHECK(h.values[size_t(paramIndex(Global::FocusTrack))] == 1.0f);
    key(w, gemui::Key::Right, true);
    CHECK(h.values[size_t(paramIndex(Global::FocusTrack))] == 2.0f);
    CHECK(h.editor.arpCur == 5);
    key(w, gemui::Key::Left, true);

    // Playing step (engine state: astep): green column, #005500 on the note.
    publish(h, w, [](plugin::EngineStatus& t) { t.arpStep = 2; });
    w.desktop().paint();
    const gemui::Surface& s = w.desktop().surface();
    const gemui::Point onNote = centre(g.cellRect(2, top - 7)), empty = centre(g.cellRect(2, 0));
    CHECK(s.at(onNote.x, onNote.y) == ArpGrid::kPlayNote);
    CHECK(s.at(empty.x, empty.y) == gemui::colors::kGreen);
    const gemui::Point other = centre(g.cellRect(1, 0));
    CHECK(s.at(other.x, other.y) == gemui::colors::kWhite);

    // Window behind another one: the arrows change the channel.
    w.mixer().show();
    key(w, gemui::Key::Right);
    CHECK(h.values[size_t(paramIndex(Global::FocusTrack))] == 2.0f);
    CHECK(h.editor.arpCur == 5);
}

TEST_CASE("Arpeggiator: the grid shows the offsets up to +24 of the chords (D-322)", "[ui][arp]") {
    TestHost h;
    h.values[size_t(paramIndex(0, Track::ArpOn))] = 1;
    h.state.tracks[0].arp.mode = ArpMode::Table;
    h.state.tracks[0].arp.table = {0, 12, 24};  // Double octave
    h.state.tracks[0].arp.length = 3;
    MainWindow w(h);
    w.arpeggiator().show();
    w.desktop().paint();
    const ArpGrid& g = w.arpeggiator().grid();
    const gemui::Surface& s = w.desktop().surface();
    auto on = [&](int step, int offset) {
        const gemui::Point p = centre(g.cellRect(step, ArpGrid::kRows - 1 - offset));
        return s.at(p.x, p.y) == gemui::colors::kBlack;
    };
    CHECK(on(1, 12));  // step 1 (step 0 has the keyboard cursor)
    CHECK(on(2, 24));  // top row
    CHECK_FALSE(on(2, 23));
    // A press on the top row writes +24.
    press(w, centre(g.cellRect(0, 0)));
    CHECK(h.arp().table[0] == 24);
}

TEST_CASE("Arpeggiator: each setting is a track undo step (D-129)", "[ui][arp][undo]") {
    TestHost h;
    MainWindow w(h);
    w.arpeggiator().show();
    click(w, "arpOn");
    click(w, "arpGlideRate");
    click(w, "arpOct2");
    CHECK(h.editor.undo.undoSize() == 3);
    CHECK(ctrl(w, U'z'));
    CHECK(h.arp().octaves == 1);
    CHECK(ctrl(w, U'z'));
    CHECK_FALSE(h.arp().track);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(0, Track::ArpOn) == 0.0f);
    CHECK(ctrl(w, U'y'));
    CHECK(h.track(0, Track::ArpOn) == 1.0f);

    // One drag of the rate: a single step.
    const gemui::Rect rate = *w.rectOf("arpRate");
    const size_t before = h.editor.undo.undoSize();
    w.desktop().mouseMove(centre(rate), {});
    w.desktop().mouseDown(centre(rate), gemui::MouseButton::Left, {});
    w.desktop().mouseDrag({rate.x + 20, rate.y + 8}, {});
    w.desktop().mouseDrag({rate.x + 90, rate.y + 8}, {});
    w.desktop().mouseUp({rate.x + 90, rate.y + 8}, {});
    CHECK(h.editor.undo.undoSize() == before + 1);
    CHECK(h.track(0, Track::ArpRate) != 50.0f);
    ctrl(w, U'z');
    CHECK(h.track(0, Track::ArpRate) == 50.0f);

    // Table: press on the grid, Enter on the keyboard.
    click(w, "arpModeSel");
    pick(w, "arpModeSel", 6, 5);
    press(w, centre(w.arpeggiator().grid().cellRect(0, ArpGrid::kRows - 1 - 12)));
    CHECK(h.arp().table[0] == 12);
    key(w, gemui::Key::Enter);  // cursor at (0, +0)
    CHECK(h.arp().table[0] == 0);
    ctrl(w, U'z');
    CHECK(h.arp().table[0] == 12);
    ctrl(w, U'z');
    CHECK(h.arp().table[0] == 0);
    ctrl(w, U'z');
    CHECK(h.arp().mode == ArpMode::Up);
}

TEST_CASE("Undo: settings of the main window, one step per gesture on the focused track", "[ui][undo]") {
    TestHost h;
    MainWindow w(h);
    // Source menu: Tone+Noise (three parameters with Mode, one step; D-246).
    click(w, "srcSel");
    pick(w, "srcSel", 7, 2);
    CHECK(h.track(0, Track::Noise) == 1.0f);
    CHECK(h.editor.undo.undoSize() == 1);
    // Generator on the keyboard (Ctrl+S: SID).
    CHECK(ctrl(w, U's'));
    CHECK(h.track(0, Track::Generator) == float(ymengine::Generator::Sid));
    CHECK(h.editor.undo.undoSize() == 2);
    ctrl(w, U'z');
    CHECK(h.track(0, Track::Generator) == float(ymengine::Generator::Chip));
    ctrl(w, U'z');
    CHECK(h.track(0, Track::Noise) == 0.0f);
    CHECK(h.track(0, Track::Tone) == 1.0f);
    // A press elsewhere (Mixer) does not arm a track step: its fader makes a mixer step.
    ctrl(w, U'y');
    ctrl(w, U'y');
    w.mixer().show();
    click(w, "mixMute0");
    CHECK(h.editor.undo.undoSize() == 3);
    ctrl(w, U'z');
    CHECK(h.track(0, Track::Mute) == 0.0f);
    CHECK(h.track(0, Track::Generator) == float(ymengine::Generator::Sid));
}

TEST_CASE("MIDI clock: waiting, then SYNC or STOP at each publication (D-084)", "[ui][clock]") {
    TestHost h;
    MainWindow w(h);
    CHECK(w.rectOf("clkStatus")->w == 0);
    h.values[size_t(paramIndex(Global::ArpClock))] = 1;
    w.tick(0);
    CHECK(w.rectOf("clkStatus")->w == 20 * 8);  // waiting for clock...
    publish(h, w, [](plugin::EngineStatus& t) { t.seq.clockRunning = true; });
    CHECK(w.rectOf("clkStatus")->w == 4 * 8);   // SYNC
    publish(h, w, [](plugin::EngineStatus& t) { t.seq.clockRunning = false; });
    CHECK(w.rectOf("clkStatus")->w == 4 * 8);   // STOP
    // Back to the internal clock, then MIDI: the waiting comes back.
    h.values[size_t(paramIndex(Global::ArpClock))] = 0;
    w.tick(0);
    h.values[size_t(paramIndex(Global::ArpClock))] = 1;
    w.tick(0);
    CHECK(w.rectOf("clkStatus")->w == 20 * 8);
}

TEST_CASE("Arpeggiator: the reopened window gets its place and the table cursor back", "[ui][arp]") {
    TestHost h;
    {
        MainWindow w(h);
        w.arpeggiator().show();
        // Moved by its title bar.
        const gemui::Point bar{150, 112};
        w.desktop().mouseMove(bar, {});
        w.desktop().mouseDown(bar, gemui::MouseButton::Left, {});
        w.desktop().mouseDrag({190, 152}, {});
        w.desktop().mouseUp({190, 152}, {});
        h.editor.arpCur = 7;
    }
    MainWindow w(h);
    CHECK(w.arpeggiator().isOpen());
    CHECK(w.rectOf("arpWin") == gemui::Rect{120, 140, 177, 90});
    CHECK(h.editor.arpCur == 7);
}
