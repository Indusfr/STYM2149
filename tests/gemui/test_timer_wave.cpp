// STYM2149 — SID wave and Sync Buzzer steps, in the GENERATOR frame since the sixth DAW test (
// the Timer Wave window before): SID wave drawn with the mouse, wheel and right click, the menu of its
// forms (Base, Square, Sine, Bounce, Custom), Steps, Period and Width; Sync Buzzer steps and their
// shapes; one undo step per gesture.
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

// Picks a form of the SID wave in its menu.
void form(MainWindow& w, const std::string& name) {
    click(w, "waveForm");
    const std::vector<std::string> l = labels(w);
    const auto it = std::find(l.begin(), l.end(), name);
    REQUIRE(it != l.end());
    pick(w, int(it - l.begin()));
}

// Drag of a slider's thumb to the right (or left) end of its track.
void dragSlider(MainWindow& w, const char* id, bool toMax) {
    const gemui::Slider* s = w.stepsSlider(id);
    REQUIRE(s);
    const gemui::Rect t = s->thumbRect(), r = s->bounds();
    drag(w, {centre(t), {r.x + r.w / 2, t.y + 8}, {toMax ? r.right() + 20 : r.x - 20, t.y + 8}});
}

gemui::Rect col(MainWindow& w, int step) { return w.sidWave().cellRect(step); }

const gemui::ValueField& field(MainWindow& w, std::string_view id) {
    const auto* f = dynamic_cast<const gemui::ValueField*>(w.widgetOf(id));
    REQUIRE(f);
    return *f;
}

// Clicks a typed value, empties it, types `text` and confirms with Enter.
void typeValue(MainWindow& w, const std::string& id, std::string_view text) {
    click(w, id);
    REQUIRE(field(w, id).editing());
    for (int i = 0; i < 8; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) {
        gemui::KeyEvent e;
        e.key = gemui::Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
    key(w, gemui::Key::Enter);
    CHECK_FALSE(field(w, id).editing());
}

void generator(TestHost& h, ymengine::Generator g) {
    h.values[size_t(paramIndex(0, plugin::Track::Generator))] = float(g);
}

}  // namespace

TEST_CASE("SID wave and Sync Buzzer steps in the GENERATOR frame, no Timer Wave window (sixth DAW test, items 52 to 54 "
          "and 80); not on percussion",
          "[ui][timerwave]") {
    TestHost h;
    generator(h, ymengine::Generator::Sid);
    MainWindow w(h);
    w.tick(0);
    CHECK_FALSE(w.rectOf("waveWin"));
    CHECK_FALSE(w.rectOf("timerWave"));

    // Edit menu: Instruments, then Samples (Timer wave and Buzzer left for the GENERATOR frame, Sequences for the
    // Modulation window).
    w.openMenu(2);
    const std::vector<std::string> edit = labels(w);
    CHECK(std::find(edit.begin(), edit.end(), "Timer wave") == edit.end());
    CHECK(std::find(edit.begin(), edit.end(), "Buzzer") == edit.end());
    CHECK(std::find(edit.begin(), edit.end(), "Sequences") == edit.end());
    const size_t at = size_t(std::find(edit.begin(), edit.end(), "Instruments") - edit.begin());
    REQUIRE(at + 1 < edit.size());
    CHECK(edit[at + 1] == "Samples");
    key(w, gemui::Key::Escape);

    // SID Voice: the SID wave, Base by default (the square of the prototype, its two halves), its menu alone;
    // no buzzer steps.
    CHECK(w.rectOf("waveForm"));
    CHECK_FALSE(w.rectOf("waveGrid"));
    CHECK_FALSE(w.rectOf("waveSteps"));
    CHECK_FALSE(w.rectOf("buzz0"));
    CHECK(w.sidWave().state().length == 2);
    CHECK(w.sidWave().state().square);

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

TEST_CASE("SID wave: the menu of its forms, Base by default; Custom drawn, Square of Steps steps (Indus, 24/10/2026 "
          "17:40 to 17:47); one undo step per gesture",
          "[ui][timerwave][undo]") {
    TestHost h;
    generator(h, ymengine::Generator::Sid);
    h.values[size_t(paramIndex(0, plugin::Track::SidVmin))] = 4;
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();

    // The menu after the name of the SID wave, on its line, Base checked; with Base, the plain square, neither the
    // bars nor Steps (Indus, 24/10/2026 20:30), its two steps kept behind: the note's volume, then SID min vol
    // (4).
    const gemui::Button* menuButton = &w.waveFormButton();
    CHECK(menuButton->text() == "Base");
    CHECK_FALSE(w.rectOf("waveGrid"));
    CHECK_FALSE(w.rectOf("waveSteps"));
    const gemui::Slider* steps = w.stepsSlider("waveSteps");
    REQUIRE(steps);
    CHECK(steps->value() == 2);
    CHECK(w.sidWave().state().square);
    CHECK(w.sidWave().state().wave[0] == 15);
    CHECK(w.sidWave().state().wave[1] == 4);
    click(w, "waveForm");
    CHECK(labels(w) == std::vector<std::string>{"Base", "Square", "Sine", "Bounce", "Custom"});
    CHECK(menu(w).checked() == 0);
    key(w, gemui::Key::Escape);
    CHECK(h.editor.undo.undoSize() == 0);

    // Custom from Base, the bars never drawn: the square played, over the 8 Steps by default (4 at the note's volume,
    // 4 at SID min vol), to draw from; Steps then sets the bars played, dragged to the end 16.
    form(w, "Custom");
    w.tick(0);
    CHECK(p.sidForm == ymengine::SidForm::Custom);
    CHECK(p.sidWaveLen == 8);
    for (int k = 0; k < 16; ++k) CHECK(p.sidWave[size_t(k)] == (k < 4 ? 15 : k < 8 ? 4 : 0));
    CHECK(menuButton->text() == "Custom");
    CHECK(steps->enabled());
    CHECK_FALSE(w.sidWave().state().square);
    CHECK(h.editor.undo.undoSize() == 1);
    dragSlider(w, "waveSteps", true);
    CHECK(p.sidWaveLen == 16);
    CHECK(w.sidWave().state().length == 16);
    CHECK(h.editor.undo.undoSize() == 2);

    // Stroke: top of the column 15, bottom 0, a single undo step for three columns.
    const gemui::Rect c0 = col(w, 0);
    drag(w, {{c0.x + 5, c0.bottom() - 1}, {col(w, 1).x + 5, c0.y + 1}, {col(w, 2).x + 5, c0.y + 23}});
    CHECK(p.sidWave[0] == 0);
    CHECK(p.sidWave[1] == 15);
    CHECK(p.sidWave[2] == 8);
    CHECK(h.editor.undo.undoSize() == 3);
    // Wheel: one undo step per notch.
    wheel(w, centre(col(w, 2)), -1);
    wheel(w, centre(col(w, 2)), -1);
    CHECK(p.sidWave[2] == 10);
    CHECK(h.editor.undo.undoSize() == 5);
    // Right click: the slider of the Modulation window's cells (seventh DAW test, item 84), 96 px below the
    // step; its bottom is worth 0, one undo step.
    const gemui::Rect c5 = col(w, 5);
    press(w, centre(c5), gemui::MouseButton::Right);
    REQUIRE(w.desktop().hasPopups());
    CHECK(p.sidWave[5] == 4);
    const gemui::Point bottom{c5.x + 2 + 4 + 2 + 8, c5.bottom() + 2 + 8 + 95};
    press(w, bottom, gemui::MouseButton::Left);
    CHECK(p.sidWave[5] == 0);
    CHECK(h.editor.undo.undoSize() == 6);
    w.desktop().closePopups();
    undoKey(w);
    CHECK(p.sidWave[5] == 4);

    // Square: its Steps kept (16), the bars drawn kept for Custom. The bars show the square played (Indus, 24/10/2026
    // 15:43): the note's volume over the first Width (4) steps, SID min vol over the others; the mouse does
    // nothing on them.
    form(w, "Square");
    w.tick(0);
    CHECK(p.sidForm == ymengine::SidForm::Square);
    CHECK(p.sidWaveLen == 16);
    CHECK(p.sidWave[1] == 15);
    CHECK(w.sidWave().state().square);
    CHECK(w.sidWave().state().length == 16);
    for (int k = 0; k < 16; ++k) CHECK(w.sidWave().state().wave[size_t(k)] == (k < 4 ? 15 : 4));
    const size_t undos = h.editor.undo.undoSize();
    const auto kept = p.sidWave;
    press(w, centre(col(w, 3)));
    wheel(w, centre(col(w, 12)), -1);
    press(w, centre(col(w, 12)), gemui::MouseButton::Right);
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK(h.editor.undo.undoSize() == undos);
    CHECK(p.sidWave == kept);
    CHECK(w.sidWave().tipAt(centre(col(w, 12))).find("Square wave: the note's volume, then SID min vol") !=
          std::string::npos);
    // Its Steps work (Indus, 17:47): down to 2, the square of Base; typed 8.
    CHECK(steps->enabled());
    dragSlider(w, "waveSteps", false);
    CHECK(p.sidWaveLen == 2);
    w.tick(0);
    CHECK(w.sidWave().state().length == 2);
    typeValue(w, "waveStepsv", "8");
    CHECK(p.sidWaveLen == 8);

    // Custom again: the bars drawn come back, over the Steps of the Square.
    form(w, "Custom");
    w.tick(0);
    CHECK_FALSE(w.sidWave().state().square);
    CHECK(w.sidWave().state().wave == p.sidWave);
    CHECK(p.sidWave == kept);
    CHECK(p.sidWaveLen == 8);

    // Base: the bars and Steps hidden, the Steps kept for the other forms.
    form(w, "Base");
    w.tick(0);
    CHECK(p.sidForm == ymengine::SidForm::Base);
    CHECK(p.sidWaveLen == 8);
    CHECK_FALSE(w.rectOf("waveGrid"));
    CHECK_FALSE(w.rectOf("waveSteps"));
    CHECK(steps->value() == 2);

    // Sine from Base: the Sine-SID on the 16 bars, cycles of 8, as many played as its Period: one
    // cycle in a period of the note, the note played; undone, Base and the bars back.
    form(w, "Sine");
    CHECK(p.sidForm == ymengine::SidForm::Sine);
    CHECK(p.sidWaveLen == 8);
    CHECK(p.sidWave == ymengine::sineSidWave());
    undoKey(w);
    CHECK(p.sidForm == ymengine::SidForm::Base);
    CHECK(p.sidWaveLen == 8);
    CHECK(p.sidWave == kept);
}

TEST_CASE("SID wave: Sine, a bar moved shifts the sine; Period, 8 by default, shown with Sine, under Steps; Custom "
          "frees the bars (Indus, 24/10/2026 11:29, 14:07, 15:41, 16:19, 17:43)",
          "[ui][timerwave][undo]") {
    TestHost h;
    generator(h, ymengine::Generator::Sid);
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();
    // Point of column `step` worth volume v (3 px per volume from the bottom of the bar).
    auto at = [&](int step, int v) {
        const gemui::Rect c = col(w, step);
        return gemui::Point{c.x + 5, c.bottom() - 3 * v};
    };
    // Period, in place of the former Phase slider: hidden without Sine, at 8.
    CHECK_FALSE(w.stepsSlider("wavePhase"));
    const gemui::Slider* period = w.stepsSlider("wavePeriod");
    REQUIRE(period);
    CHECK_FALSE(period->showing());
    CHECK_FALSE(w.rectOf("wavePeriodv"));
    CHECK(period->value() == 8);
    CHECK(field(w, "wavePeriodv").text() == "8");

    // Sine: two cycles of 8 bars; Period right under Steps, its slider lined up with Steps' (Indus, 17:42, 17:43).
    form(w, "Sine");
    w.tick(0);
    CHECK(p.sidForm == ymengine::SidForm::Sine);
    CHECK(period->showing());
    CHECK(period->enabled());
    const gemui::Rect stepsBox = w.stepsSlider("waveSteps")->bounds();
    CHECK(period->bounds().y > stepsBox.bottom());
    CHECK(period->bounds().y - stepsBox.bottom() < 12);
    CHECK(period->bounds().x == stepsBox.x);
    CHECK(period->bounds().right() == stepsBox.right());
    CHECK(p.sidWavePeriod == 8);
    CHECK(p.sidWave == ymengine::sineSidWave());
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 8, 8));
    // The slider and its field set the period; the sine follows, between the same volumes.
    dragSlider(w, "wavePeriod", false);
    CHECK(p.sidWavePeriod == 2);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 8, 2));
    typeValue(w, "wavePeriodv", "5");
    CHECK(p.sidWavePeriod == 5);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 8, 5));
    typeValue(w, "wavePeriodv", "1");  // brought back into the range of the slider
    CHECK(p.sidWavePeriod == 2);
    typeValue(w, "wavePeriodv", "40");
    CHECK(p.sidWavePeriod == 16);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 8, 16));  // one cycle over the 16 bars, the former sine
    w.tick(0);
    CHECK(field(w, "wavePeriodv").text() == "16");

    // At period 16: the 10th bar (6, falling) raised to 8: the sine one bar to the right, its volumes kept.
    const size_t before = h.editor.undo.undoSize();
    press(w, at(9, 8));
    CHECK(p.sidWavePhase == 9);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 9, 16));
    // The 12th (4 now) dragged down to the trough then to 12: one undo step per gesture.
    drag(w, {at(11, 4), at(11, 3), at(11, 12)});
    CHECK(p.sidWave[11] == 12);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, p.sidWavePhase, 16));
    CHECK(*std::min_element(p.sidWave.begin(), p.sidWave.end()) == 3);
    CHECK(*std::max_element(p.sidWave.begin(), p.sidWave.end()) == 15);
    CHECK(h.editor.undo.undoSize() == before + 2);
    undoKey(w);
    CHECK(p.sidWavePhase == 9);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 9, 16));
    // A notch of the wheel moves a bar to the next volume the sine gives it: the 10th from 8 down to 6 (no phase gives
    // it 7), one bar back to the left.
    wheel(w, centre(col(w, 9)), 1);
    CHECK(p.sidWavePhase == 8);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 8, 16));
    // At period 8, a bar moved shifts the two cycles: the 2nd bar (15) lowered to the volume of the 1st, one bar to
    // the right.
    typeValue(w, "wavePeriodv", "8");
    CHECK(p.sidWave == ymengine::sineSidWave());
    press(w, at(1, ymengine::sineSidWave()[0]));
    CHECK(p.sidWavePhase == 9);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 9, 8));
    CHECK(p.sidWavePeriod == 8);

    // Custom frees the bars as they are: a bar then moves alone, the period hidden and kept.
    const auto lit = p.sidWave;
    form(w, "Custom");
    w.tick(0);
    CHECK(p.sidForm == ymengine::SidForm::Custom);
    CHECK_FALSE(period->showing());
    CHECK_FALSE(w.stepsSlider("waveWidth")->showing());  // nor the width of the Square
    CHECK(p.sidWave == lit);
    press(w, at(0, 2));
    CHECK(p.sidWave[0] == 2);
    CHECK(p.sidWave[1] == lit[1]);
    CHECK(p.sidWavePhase == 9);
    CHECK(p.sidWavePeriod == 8);
    // Sine again: the sine from volume 3 to 15 at the phase and period kept; Square hides the period.
    form(w, "Sine");
    CHECK(p.sidWave == lit);
    CHECK(p.sidForm == ymengine::SidForm::Sine);
    form(w, "Square");
    w.tick(0);
    CHECK(p.sidForm == ymengine::SidForm::Square);
    CHECK_FALSE(period->showing());
}

TEST_CASE("SID wave: Bounce, the sine of the chip's levels, after Sine in the menu; Period shown with it, a bar moved "
          "shifts it (Indus, 24/10/2026 18:41)",
          "[ui][timerwave][undo]") {
    TestHost h;
    generator(h, ymengine::Generator::Sid);
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();
    auto at = [&](int step, int v) {
        const gemui::Rect c = col(w, step);
        return gemui::Point{c.x + 5, c.bottom() - 3 * v};
    };
    // From Base, like Sine: one cycle of 8 bars played, from volume 3 to 15 on the chip's levels; Period
    // under Steps; one undo step.
    form(w, "Bounce");
    w.tick(0);
    CHECK(p.sidForm == ymengine::SidForm::Bounce);
    CHECK(w.waveFormButton().text() == "Bounce");
    CHECK(p.sidWaveLen == 8);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 8, 8, true));
    CHECK(p.sidWave != ymengine::sineSidWave());
    CHECK(w.stepsSlider("wavePeriod")->showing());
    CHECK_FALSE(w.stepsSlider("waveWidth")->showing());
    CHECK_FALSE(w.sidWave().state().square);
    CHECK(h.editor.undo.undoSize() == 1);
    click(w, "waveForm");
    CHECK(menu(w).checked() == 3);
    key(w, gemui::Key::Escape);
    // A bar moved shifts the bounce, as it does the sine: the 2nd (15) lowered to the volume of the 1st, one bar to the
    // right; Period redraws it.
    press(w, at(1, p.sidWave[0]));
    CHECK(p.sidWavePhase == 9);
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 9, 8, true));
    typeValue(w, "wavePeriodv", "16");
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 9, 16, true));
    // Sine and Bounce draw their own curve at the phase and period kept.
    form(w, "Sine");
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 9, 16));
    form(w, "Bounce");
    CHECK(p.sidWave == ymengine::sineSidWave(3, 15, 9, 16, true));
    CHECK(p.sidWaveLen == 8);  // the Steps set from Base, kept
}

TEST_CASE("SID wave: Width of the Square, in its Steps, half and half by default, drawn in the bars, shown with Square "
          "where Period is with Sine (Indus, 24/10/2026 16:00 to 17:47)",
          "[ui][timerwave][undo]") {
    TestHost h;
    generator(h, ymengine::Generator::Sid);
    h.values[size_t(paramIndex(0, plugin::Track::SidVmin))] = 4;
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();
    // Hidden at Base; with Square, right under Steps, its label before it like Steps', its slider lined up with
    // Steps'; 4 of the 8 Steps.
    const gemui::Slider* width = w.stepsSlider("waveWidth");
    const gemui::Slider* period = w.stepsSlider("wavePeriod");
    REQUIRE(width);
    REQUIRE(period);
    CHECK_FALSE(width->showing());
    form(w, "Square");
    w.tick(0);
    CHECK(width->showing());
    CHECK_FALSE(period->showing());
    const gemui::Rect stepsBox = w.stepsSlider("waveSteps")->bounds();
    CHECK(width->bounds().y > stepsBox.bottom());
    CHECK(width->bounds().x == stepsBox.x);
    CHECK(width->bounds().right() == stepsBox.right());
    const auto widthField = w.rectOf("waveWidthv");
    CHECK(widthField);
    CHECK(widthField->x == w.rectOf("waveStepsv")->x);
    const gemui::Rect place = width->bounds();
    CHECK(width->enabled());
    CHECK(width->value() == 4);
    CHECK(width->max() == 7);
    CHECK(field(w, "waveWidthv").text() == "4");
    CHECK(p.sidWidth == 4);
    CHECK(p.sidWaveLen == 8);
    for (int k = 0; k < 16; ++k) CHECK(w.sidWave().state().wave[size_t(k)] == (k < 4 ? 15 : 4));
    // The field and the slider set it, one undo step each, the bars follow: the note's volume over Width steps, up to
    // Steps - 1.
    const size_t before = h.editor.undo.undoSize();
    typeValue(w, "waveWidthv", "3");
    CHECK(p.sidWidth == 3);
    w.tick(0);
    for (int k = 0; k < 16; ++k) CHECK(w.sidWave().state().wave[size_t(k)] == (k < 3 ? 15 : 4));
    dragSlider(w, "waveWidth", true);
    CHECK(p.sidWidth == 7);
    typeValue(w, "waveWidthv", "0");  // brought back into the range of the slider
    CHECK(p.sidWidth == 1);
    CHECK(h.editor.undo.undoSize() == before + 3);
    undoKey(w);
    CHECK(p.sidWidth == 7);
    typeValue(w, "waveWidthv", "40");
    CHECK(p.sidWidth == 7);
    w.tick(0);
    CHECK(field(w, "waveWidthv").text() == "7");
    // 16 Steps: the width kept, its range up to 15; at 2 Steps, one step at the note's volume, the width greyed.
    dragSlider(w, "waveSteps", true);
    w.tick(0);
    CHECK(width->max() == 15);
    CHECK(p.sidWidth == 7);
    dragSlider(w, "waveWidth", true);
    CHECK(p.sidWidth == 15);
    w.tick(0);
    for (int k = 0; k < 16; ++k) CHECK(w.sidWave().state().wave[size_t(k)] == (k < 15 ? 15 : 4));
    dragSlider(w, "waveSteps", false);
    w.tick(0);
    CHECK_FALSE(width->enabled());
    CHECK(width->value() == 1);
    CHECK(p.sidWidth == 15);
    CHECK(w.sidWave().state().wave[0] == 15);
    CHECK(w.sidWave().state().wave[1] == 4);
    dragSlider(w, "waveSteps", true);
    // Custom: neither Width nor Period shown, the width kept; Sine: Period in the place of Width.
    form(w, "Custom");
    w.tick(0);
    CHECK_FALSE(width->showing());
    CHECK_FALSE(period->showing());
    CHECK_FALSE(w.rectOf("waveWidthv"));
    CHECK(p.sidWidth == 15);
    form(w, "Sine");
    w.tick(0);
    CHECK(period->showing());
    CHECK_FALSE(width->showing());
    CHECK(period->bounds() == place);
    CHECK(w.rectOf("wavePeriodv") == widthField);
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
