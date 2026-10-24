// STYM2149 — SID wave and Sync Buzzer steps (M6, B6, D-151), in the GENERATOR frame since the sixth DAW test (D-250 to
// D-252, D-278; the Timer Wave window before): SID wave drawn with the mouse, wheel and right click, Steps (starting
// from the square wave), Square and Sine; Sync Buzzer steps and their shapes; one undo step per gesture.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/IconData.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Pitch.h"

using namespace stym;
using pluginui::MainWindow;
using plugin::paramIndex;

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
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}

    const ymengine::TrackPatch& patch(int track = 0) const { return state.tracks[size_t(track)].patch; }
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

void wheel(MainWindow& w, gemui::Point p, double dy) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseWheel(p, 0, dy, {});
}

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

bool undoKey(MainWindow& w) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = U'z';
    e.code = 'Z';
    e.mods.ctrl = true;
    return w.desktop().keyDown(e);
}

const gemui::MenuList& menu(MainWindow& w) {
    const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(m);
    return *m;
}

std::vector<std::string> labels(MainWindow& w) {
    std::vector<std::string> out;
    for (const gemui::MenuItem& it : menu(w).items()) out.push_back(it.separator ? "---" : it.label);
    return out;
}

void pick(MainWindow& w, int index) {
    const gemui::Rect r = menu(w).rowsFrom(index);
    REQUIRE(r.h > 0);
    press(w, {r.x + 20, r.y + 10});
}

// Drag of a slider's thumb to the right (or left) end of its track.
void dragSlider(MainWindow& w, const char* id, bool toMax) {
    const gemui::Slider* s = w.stepsSlider(id);
    REQUIRE(s);
    const gemui::Rect t = s->thumbRect(), r = s->bounds();
    drag(w, {centre(t), {r.x + r.w / 2, t.y + 8}, {toMax ? r.right() + 20 : r.x - 20, t.y + 8}});
}

gemui::Rect col(MainWindow& w, int step) { return w.sidWave().cellRect(step); }

void generator(TestHost& h, ymengine::Generator g) {
    h.values[size_t(paramIndex(0, plugin::Track::Generator))] = float(g);
}

}  // namespace

TEST_CASE("SID wave and Sync Buzzer steps in the GENERATOR frame, no Timer Wave window (sixth DAW test, items 52 to 54 "
          "and 80, D-250 to D-252, D-278); not on percussion",
          "[ui][timerwave]") {
    TestHost h;
    generator(h, ymengine::Generator::Sid);
    MainWindow w(h);
    w.tick(0);
    CHECK_FALSE(w.rectOf("waveWin"));
    CHECK_FALSE(w.rectOf("timerWave"));

    // Edit menu: Instruments, then Samples (Timer wave and Buzzer left for the GENERATOR frame, Sequences for the
    // Modulation window, D-241).
    w.openMenu(2);
    const std::vector<std::string> edit = labels(w);
    CHECK(std::find(edit.begin(), edit.end(), "Timer wave") == edit.end());
    CHECK(std::find(edit.begin(), edit.end(), "Buzzer") == edit.end());
    CHECK(std::find(edit.begin(), edit.end(), "Sequences") == edit.end());
    const size_t at = size_t(std::find(edit.begin(), edit.end(), "Instruments") - edit.begin());
    REQUIRE(at + 1 < edit.size());
    CHECK(edit[at + 1] == "Samples");
    key(w, gemui::Key::Escape);

    // SID Voice: the SID wave, square by default (faded grid); no buzzer steps.
    for (const char* id : {"waveGrid", "waveSteps", "waveSquare", "waveSine"}) {
        INFO(id);
        CHECK(w.rectOf(id));
    }
    CHECK_FALSE(w.rectOf("buzz0"));
    CHECK(w.sidWave().state().length == 0);

    // Sync Buzzer: its steps, the track's shape by default; no SID wave.
    generator(h, ymengine::Generator::SyncBuzzer);
    w.tick(10);
    CHECK_FALSE(w.rectOf("waveGrid"));
    for (const char* id : {"buzz0", "buzz7", "buzzSteps"}) {
        INFO(id);
        CHECK(w.rectOf(id));
    }
    CHECK_FALSE(w.rectOf("buzz8"));
    CHECK(w.buzzStep(0).text() == "Trk");
    CHECK(w.buzzStep(0).icon() == nullptr);

    // Percussion track: neither (the generator is Chipdrum).
    w.pickGenerator(int(ymengine::Generator::Chipdrum));
    w.tick(20);
    CHECK_FALSE(w.rectOf("waveGrid"));
    CHECK_FALSE(w.rectOf("buzz0"));
}

TEST_CASE("SID wave: drawn, Steps from the square wave, Square and Sine; one undo step per gesture",
          "[ui][timerwave][undo]") {
    TestHost h;
    generator(h, ymengine::Generator::Sid);
    h.values[size_t(paramIndex(0, plugin::Track::SidVmin))] = 4;
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();

    // Steps dragged to the end: 16 steps, starting from the track's square wave (8 at full volume, 8 at low volume).
    dragSlider(w, "waveSteps", true);
    CHECK(p.sidWaveLen == 16);
    for (int k = 0; k < 16; ++k) CHECK(p.sidWave[size_t(k)] == (k < 8 ? 15 : 4));
    CHECK(w.sidWave().state().length == 16);
    CHECK(h.editor.undo.undoSize() == 1);

    // Stroke: top of the column 15, bottom 0, a single undo step for three columns.
    const gemui::Rect c0 = col(w, 0);
    drag(w, {{c0.x + 5, c0.bottom() - 1}, {col(w, 1).x + 5, c0.y + 1}, {col(w, 2).x + 5, c0.y + 23}});
    CHECK(p.sidWave[0] == 0);
    CHECK(p.sidWave[1] == 15);
    CHECK(p.sidWave[2] == 8);
    CHECK(h.editor.undo.undoSize() == 2);
    // Wheel: one undo step per notch.
    wheel(w, centre(col(w, 2)), -1);
    wheel(w, centre(col(w, 2)), -1);
    CHECK(p.sidWave[2] == 10);
    CHECK(h.editor.undo.undoSize() == 4);
    // Right click: the slider of the Modulation window's cells (seventh DAW test, item 84, D-285), 96 px below the
    // step; its bottom is worth 0, one undo step.
    const gemui::Rect c9 = col(w, 9);
    press(w, centre(c9), gemui::MouseButton::Right);
    REQUIRE(w.desktop().hasPopups());
    CHECK(p.sidWave[9] == 4);
    const gemui::Point bottom{c9.x + 2 + 4 + 2 + 8, c9.bottom() + 2 + 8 + 95};
    press(w, bottom, gemui::MouseButton::Left);
    CHECK(p.sidWave[9] == 0);
    CHECK(h.editor.undo.undoSize() == 5);
    w.desktop().closePopups();
    undoKey(w);
    CHECK(p.sidWave[9] == 4);

    // Square: the square wave comes back, the volumes are kept; Steps plays them again without overwriting them.
    click(w, "waveSquare");
    CHECK(p.sidWaveLen == 0);
    CHECK(p.sidWave[1] == 15);
    CHECK(w.sidWave().state().length == 0);
    dragSlider(w, "waveSteps", true);
    CHECK(p.sidWaveLen == 16);
    CHECK(p.sidWave[0] == 0);
    CHECK(p.sidWave[2] == 10);
    // Steps dragged to the left end: the square wave.
    dragSlider(w, "waveSteps", false);
    CHECK(p.sidWaveLen == 0);

    // Sine: the 8 volumes of the Sinus-SID, the rest at 0.
    click(w, "waveSine");
    CHECK(p.sidWaveLen == 8);
    const auto sine = ymengine::sineSidWave();
    for (int k = 0; k < 16; ++k) CHECK(p.sidWave[size_t(k)] == (k < 8 ? sine[size_t(k)] : 0));
    undoKey(w);
    CHECK(p.sidWaveLen == 0);
    CHECK(p.sidWave[1] == 15);
}

TEST_CASE("Sync Buzzer steps: R13 shapes from the menu, shown by their icon, Trk on right click", "[ui][timerwave]") {
    TestHost h;
    generator(h, ymengine::Generator::SyncBuzzer);
    h.values[size_t(paramIndex(0, plugin::Track::EnvShape))] = 12;
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();

    // Without steps, the buttons are off; Steps at the end: 8 steps.
    click(w, "buzz0");
    CHECK_FALSE(w.desktop().hasPopups());
    dragSlider(w, "buzzSteps", true);
    CHECK(p.buzzLen == 8);
    for (int k = 0; k < 8; ++k) CHECK(w.buzzStep(k).text() == "Trk");

    // Menu of step 2: the track's shape, then shapes 8 to F.
    click(w, "buzz1");
    CHECK(labels(w) == std::vector<std::string>{"Track (C)", "8", "9", "A", "B", "C", "D", "E", "F"});
    CHECK(menu(w).checked() == 0);
    pick(w, 3);
    CHECK(p.buzzShapes[1] == 0xA);
    CHECK(w.buzzStep(1).text().empty());
    CHECK(w.buzzStep(1).icon() == &pluginui::icons::kShapes[0xA]);
    click(w, "buzz1");
    CHECK(menu(w).checked() == 3);
    key(w, gemui::Key::Escape);
    click(w, "buzz1", gemui::MouseButton::Right);
    CHECK(p.buzzShapes[1] == 0);
    CHECK(w.buzzStep(1).text() == "Trk");

    // Steps at the left end: no more steps, the shapes are kept.
    click(w, "buzz7");
    pick(w, 8);
    CHECK(p.buzzShapes[7] == 0xF);
    dragSlider(w, "buzzSteps", false);
    CHECK(p.buzzLen == 0);
    CHECK(p.buzzShapes[7] == 0xF);
}

TEST_CASE("Timer wave: the window of an older session is forgotten", "[ui][timerwave]") {
    TestHost h;
    pluginui::WindowPlace place;
    place.at = {300, 600};
    place.open = true;
    place.order = 1;
    h.editor.windows["wave"] = place;
    MainWindow w(h);
    w.tick(0);
    CHECK_FALSE(w.rectOf("waveWin"));
    CHECK(gemui::FloatWindow::topmost(w) == nullptr);
}
