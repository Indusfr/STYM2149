// STYM2149 — Mixer window (M5): opening (button, Ctrl+M), faders, panning, mute and solo linked to the track
// parameters, one undo step per gesture, strips of the 3 ch mode, VU meters (vuPct, peak hold, clipping).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Alert.h"
#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using plugin::Global;
using plugin::paramIndex;
using plugin::Track;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::optional<plugin::EngineStatus> pending;
    plugin::EngineStatus last;

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
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void click(MainWindow& w, const std::string& id) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    const gemui::Point p = centre(*r);
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(p, {});
}

void drag(MainWindow& w, gemui::Point from, std::initializer_list<gemui::Point> path) {
    w.desktop().mouseMove(from, {});
    w.desktop().mouseDown(from, gemui::MouseButton::Left, {});
    gemui::Point last = from;
    for (gemui::Point p : path) {
        w.desktop().mouseDrag(p, {});
        last = p;
    }
    w.desktop().mouseUp(last, {});
}

bool ctrl(MainWindow& w, char32_t ch) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = ch;
    e.code = int(ch - U'a' + U'A');
    e.mods = {false, true, false};
    return w.desktop().keyDown(e);
}

// Right click on a slider, then the entry `item` of its menu (Value... or Reset); for Value..., `typed` in the box,
// confirmed with Enter.
std::vector<std::string> sliderMenu(MainWindow& w, const std::string& id, int item, const std::string& typed = {}) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    const gemui::Point p = centre(*r);
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Right, {});
    w.desktop().mouseUp(p, {});
    const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(m);
    std::vector<std::string> labels;
    for (const gemui::MenuItem& it : m->items()) labels.push_back(it.label);
    const gemui::Point at = centre(m->itemRect(item));
    w.desktop().mouseMove(at, {});
    w.desktop().mouseDown(at, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(at, {});
    if (item == 0) {
        REQUIRE(dynamic_cast<const gemui::Alert*>(w.desktop().topModal()));
        labels.push_back(dynamic_cast<const gemui::Alert*>(w.desktop().topModal())->text());
        gemui::KeyEvent e;
        e.key = gemui::Key::Backspace;
        for (int i = 0; i < 8; ++i) w.desktop().keyDown(e);
        for (const char c : typed) {
            gemui::KeyEvent ch;
            ch.key = gemui::Key::Character;
            ch.ch = char32_t(c);
            w.desktop().keyDown(ch);
        }
        e.key = gemui::Key::Enter;
        w.desktop().keyDown(e);
        REQUIRE_FALSE(w.desktop().topModal());
    }
    w.tick(0);
    return labels;
}

// Clicks a value under its slider, types `text` and ends with `end` (Enter or Escape) (D-415).
void typeUnder(MainWindow& w, const std::string& id, std::string_view text, gemui::Key end = gemui::Key::Enter) {
    click(w, id);
    const gemui::ValueField* f = w.mixer().field(id);
    REQUIRE(f);
    REQUIRE(f->editing());
    for (char c : text) {
        gemui::KeyEvent e;
        e.key = gemui::Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
    gemui::KeyEvent e;
    e.key = end;
    w.desktop().keyDown(e);
    CHECK_FALSE(f->editing());
    w.tick(0);
}

void publish(TestHost& h, MainWindow& w, uint64_t serial, const std::function<void(plugin::EngineStatus&)>& fill) {
    plugin::EngineStatus t;
    t.serial = serial;
    fill(t);
    h.pending = t;
    w.tick(0);
}

}  // namespace

TEST_CASE("Mixer: Mixer button and Ctrl+M open the window, Escape closes it", "[ui][mixer]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.mixer().isOpen());
    click(w, "openMixBtn");
    CHECK(w.mixer().isOpen());
    CHECK_FALSE(w.desktop().hasModal());  // no more "not available yet" alert
    // + the Master faders (D-276, D-284), the values (D-300), the Send (D-299).
    CHECK(w.rectOf("mixWin") == gemui::Rect{110, 110, 1048 + 78, 325 + 24 + 32});
    CHECK(w.rectOf("mixFad15"));
    CHECK(w.rectOf("vuhR"));
    gemui::KeyEvent esc;
    esc.key = gemui::Key::Escape;
    w.desktop().keyDown(esc);
    CHECK_FALSE(w.mixer().isOpen());
    CHECK(ctrl(w, U'm'));
    CHECK(w.mixer().isOpen());
}

TEST_CASE("Mixer: fader and panning linked to the track's level and pan, one step per drag", "[ui][mixer]") {
    TestHost h;
    MainWindow w(h);
    w.mixer().show();
    CHECK(h.track(2, Track::Level) == 75.0f);

    // Vertical fader: minimum at the bottom. Press at the top of the track, then drag to the middle: a single step.
    const gemui::Rect fad = *w.rectOf("mixFad2");
    drag(w, {fad.x + 8, fad.y + 2}, {{fad.x + 8, fad.y + 10}, {fad.x + 8, fad.y + 48}});
    CHECK(h.track(2, Track::Level) == 50.0f);
    CHECK(h.editor.undo.undoSize() == 1);
    // The fader follows the parameter (level changed elsewhere: Level slider of the main window, host).
    h.values[size_t(paramIndex(2, Track::Level))] = 100;
    w.tick(0);
    CHECK(w.rectOf("mixFad2") == fad);

    // Panning: press on the right of the track, then double click: back to the centre, with one step.
    const gemui::Rect pan = *w.rectOf("mixPan2");
    drag(w, {pan.right() - 2, pan.y + 8}, {});
    CHECK(h.track(2, Track::Pan) == 100.0f);
    CHECK(h.editor.undo.undoSize() == 2);
    w.desktop().mouseDoubleClick({pan.right() - 2, pan.y + 8}, {});
    CHECK(h.track(2, Track::Pan) == 0.0f);
    CHECK(h.editor.undo.undoSize() == 3);

    // Undo: the panning goes back to the right, then to the centre, then the initial level.
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(2, Track::Pan) == 100.0f);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(2, Track::Pan) == 0.0f);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(2, Track::Level) == 75.0f);
    CHECK(ctrl(w, U'y'));
    CHECK(h.track(2, Track::Level) == 100.0f);
}

TEST_CASE("Mixer: values under the faders and the pans, the pans named (D-310); right click: Value... and Reset "
          "(D-300)",
          "[ui][mixer]") {
    TestHost h;
    MainWindow w(h);
    w.mixer().show();
    pluginui::MixerWindow& mix = w.mixer();
    // 8 px rows in the 6×6 font under the fader row and under the pan, 4 px apart like the rest of the strip.
    const gemui::Rect fad = *w.rectOf("mixFad2"), pan = *w.rectOf("mixPan2");
    const gemui::Rect fv = *w.rectOf("mixFadVal2"), pv = *w.rectOf("mixPanVal2");
    CHECK(fv.y == fad.bottom() + 4);
    CHECK(fv.h == 8);
    CHECK(pan.y == fv.bottom() + 4);
    CHECK(pv.y == pan.bottom() + 4);
    CHECK(w.rectOf("mixSend2")->y == pv.bottom() + 4);  // the Send under the pan (D-299)
    CHECK(mix.valueText("mixFadVal2") == "75");
    CHECK(mix.valueText("mixPanVal2") == "Pan 0");  // named like the send (ninth DAW test, item 109, D-310)
    // The Masters' values on the row of the faders' values, the balance's under it.
    CHECK(w.rectOf("mixMasterVal")->y == fv.y);
    CHECK(w.rectOf("mixMaster2Val")->y == fv.y);
    CHECK(w.rectOf("mixMasterPanVal")->y == pv.y);
    CHECK(w.rectOf("mixLink")->y == w.rectOf("mixMute2")->y);
    CHECK(mix.valueText("mixMasterVal") == "70");
    CHECK(mix.valueText("mixMaster2Val") == "70");
    CHECK(mix.valueText("mixMasterPanVal") == "0");
    // They follow the parameters; a pan signed.
    h.values[size_t(paramIndex(2, Track::Pan))] = 25;
    h.values[size_t(paramIndex(2, Track::Level))] = 100;
    w.tick(0);
    CHECK(mix.valueText("mixFadVal2") == "100");
    CHECK(mix.valueText("mixPanVal2") == "Pan +25");
    h.values[size_t(paramIndex(2, Track::Pan))] = -25;
    w.tick(0);
    CHECK(mix.valueText("mixPanVal2") == "Pan -25");

    // Fader: a typed value, one undo step; out of range, clamped; nothing typed, nothing; Reset: the default.
    CHECK(sliderMenu(w, "mixFad2", 0, "40") ==
          std::vector<std::string>{"Value...", "Reset", "Volume (0 to 100):"});
    CHECK(h.track(2, Track::Level) == 40.0f);
    CHECK(mix.valueText("mixFadVal2") == "40");
    CHECK(h.editor.undo.undoSize() == 1);
    sliderMenu(w, "mixFad2", 0, "300");
    CHECK(h.track(2, Track::Level) == 100.0f);
    sliderMenu(w, "mixFad2", 0, "");
    CHECK(h.track(2, Track::Level) == 100.0f);
    CHECK(h.editor.undo.undoSize() == 2);
    sliderMenu(w, "mixFad2", 1);
    CHECK(h.track(2, Track::Level) == 75.0f);
    CHECK(h.editor.undo.undoSize() == 3);
    sliderMenu(w, "mixFad2", 1);  // already there: no step
    CHECK(h.editor.undo.undoSize() == 3);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(2, Track::Level) == 100.0f);

    // Pan: signed values, Reset to the centre.
    CHECK(sliderMenu(w, "mixPan2", 0, "-60").back() == "Pan (-100 to 100):");
    CHECK(h.track(2, Track::Pan) == -60.0f);
    sliderMenu(w, "mixPan2", 1);
    CHECK(h.track(2, Track::Pan) == 0.0f);

    // Masters, balance: no undo step, like their drag; Link: both Masters.
    const size_t steps = h.editor.undo.undoSize();
    sliderMenu(w, "mixMaster2", 0, "55");
    CHECK(h.values[size_t(paramIndex(Global::Master))] == 55.0f);
    CHECK(mix.valueText("mixMasterVal") == "55");
    CHECK(mix.valueText("mixMaster2Val") == "55");
    sliderMenu(w, "mixMaster", 1);
    CHECK(h.values[size_t(paramIndex(Global::Master))] == 70.0f);
    CHECK(sliderMenu(w, "mixMasterPan", 0, "30").back() == "Balance (-100 to 100):");
    CHECK(h.values[size_t(paramIndex(Global::MasterPan))] == 30.0f);
    CHECK(mix.valueText("mixMasterPanVal") == "+30");
    sliderMenu(w, "mixMasterPan", 1);
    CHECK(h.values[size_t(paramIndex(Global::MasterPan))] == 0.0f);
    CHECK(h.editor.undo.undoSize() == steps);

    // 3 channels: a chip channel's fader is its 4-bit Volume (0 to 15), one track undo step; no pan values.
    h.values[size_t(paramIndex(Global::VoiceMode))] = 1;
    w.tick(0);
    CHECK_FALSE(w.rectOf("mixPanVal1"));
    CHECK(w.rectOf("mixMute1")->y == w.rectOf("mixFadVal1")->bottom() + 4);
    CHECK(sliderMenu(w, "mixFad1", 0, "9").back() == "Volume (0 to 15):");
    CHECK(h.track(1, Track::Volume) == 9.0f);
    CHECK(mix.valueText("mixFadVal1") == "9");
    sliderMenu(w, "mixFad1", 1);
    CHECK(h.track(1, Track::Volume) == plugin::paramSpec(paramIndex(1, Track::Volume)).def);
    CHECK(h.track(1, Track::Level) == 75.0f);
}

TEST_CASE("Mixer: a click on a value under its slider types it in place, Enter confirms, Escape cancels (24/10/2026, "
          "D-415)",
          "[ui][mixer]") {
    TestHost h;
    MainWindow w(h);
    w.mixer().show();
    pluginui::MixerWindow& mix = w.mixer();
    // Each field as wide as the widest value of its slider (3 characters for a level, 4 for a pan) or as its text,
    // plus a pixel on each side, centred on the strip.
    const gemui::Rect lab = *w.rectOf("mixLab2"), fv = *w.rectOf("mixFadVal2");
    CHECK(fv.w == 3 * 6 + 2);
    CHECK(std::abs((2 * fv.x + fv.w) - (2 * lab.x + lab.w)) <= 1);
    CHECK(w.rectOf("mixPanVal2")->w == 5 * 6 + 2);   // "Pan 0"
    CHECK(w.rectOf("mixSendVal2")->w == 6 * 6 + 2);  // "Send 0"
    CHECK(w.rectOf("mixMasterPanVal")->w == 4 * 6 + 2);
    // The two Masters' values side by side, without overlapping.
    CHECK(w.rectOf("mixMasterVal")->right() <= w.rectOf("mixMaster2Val")->x);

    // Fader: one undo step, as with Value...; out of range, clamped; no number, refused, no step; Escape, nothing.
    typeUnder(w, "mixFadVal2", "40");
    CHECK(h.track(2, Track::Level) == 40.0f);
    CHECK(mix.valueText("mixFadVal2") == "40");
    CHECK(h.editor.undo.undoSize() == 1);
    typeUnder(w, "mixFadVal2", "250");
    CHECK(h.track(2, Track::Level) == 100.0f);
    CHECK(h.editor.undo.undoSize() == 2);
    typeUnder(w, "mixFadVal2", "abc");
    typeUnder(w, "mixFadVal2", "100");  // the same value: no step
    typeUnder(w, "mixFadVal2", "7", gemui::Key::Escape);
    CHECK(h.track(2, Track::Level) == 100.0f);
    CHECK(mix.valueText("mixFadVal2") == "100");
    CHECK(h.editor.undo.undoSize() == 2);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(2, Track::Level) == 40.0f);

    // Pan and Send: the number only is typed, the name stays.
    typeUnder(w, "mixPanVal2", "-30");
    CHECK(h.track(2, Track::Pan) == -30.0f);
    CHECK(mix.valueText("mixPanVal2") == "Pan -30");
    typeUnder(w, "mixPanVal2", "+45");
    CHECK(h.track(2, Track::Pan) == 45.0f);
    CHECK(mix.valueText("mixPanVal2") == "Pan +45");
    typeUnder(w, "mixSendVal2", "35");
    CHECK(h.track(2, Track::Send) == 35.0f);
    CHECK(mix.valueText("mixSendVal2") == "Send 35");

    // While a value is typed, a change from elsewhere (automation) does not end the input; cancelled, the field
    // shows the new value.
    click(w, "mixFadVal3");
    REQUIRE(mix.field("mixFadVal3")->editing());
    h.values[size_t(paramIndex(3, Track::Level))] = 12;
    w.tick(0);
    CHECK(mix.field("mixFadVal3")->editing());
    gemui::KeyEvent esc;
    esc.key = gemui::Key::Escape;
    w.desktop().keyDown(esc);
    w.tick(0);
    CHECK(mix.valueText("mixFadVal3") == "12");
    CHECK(h.track(3, Track::Level) == 12.0f);

    // Masters and balance: no undo step, like their drag; with Link, both Masters.
    const size_t steps = h.editor.undo.undoSize();
    typeUnder(w, "mixMaster2Val", "55");
    CHECK(h.values[size_t(paramIndex(Global::Master))] == 55.0f);
    CHECK(mix.valueText("mixMasterVal") == "55");
    typeUnder(w, "mixMasterPanVal", "-100");
    CHECK(h.values[size_t(paramIndex(Global::MasterPan))] == -100.0f);
    CHECK(mix.valueText("mixMasterPanVal") == "-100");
    CHECK(h.editor.undo.undoSize() == steps);

    // 3 channels: a chip channel's 4-bit Volume (0 to 15), one track step; greyed out on a Sync Buzzer, no input.
    h.values[size_t(paramIndex(Global::VoiceMode))] = 1;
    w.tick(0);
    CHECK(w.rectOf("mixFadVal1")->w == 3 * 6 + 2);
    typeUnder(w, "mixFadVal1", "22");
    CHECK(h.track(1, Track::Volume) == 15.0f);
    CHECK(h.editor.undo.undoSize() == steps + 1);
    h.values[size_t(paramIndex(1, Track::Generator))] = float(int(ymengine::Generator::SyncBuzzer));
    w.tick(0);
    click(w, "mixFadVal1");
    CHECK_FALSE(mix.field("mixFadVal1")->editing());
}

TEST_CASE("Mixer: Send of each track to the delay, under the pan, 16 channels only (D-299)", "[ui][mixer]") {
    TestHost h;
    MainWindow w(h);
    w.mixer().show();
    pluginui::MixerWindow& mix = w.mixer();
    const gemui::Rect pv = *w.rectOf("mixPanVal2"), send = *w.rectOf("mixSend2"), sv = *w.rectOf("mixSendVal2");
    CHECK(send.y == pv.bottom() + 4);
    CHECK(send.x == w.rectOf("mixPan2")->x);
    CHECK(send.w == w.rectOf("mixPan2")->w);
    CHECK(sv.y == send.bottom() + 4);
    CHECK(sv.h == 8);
    CHECK(w.rectOf("mixMute2")->y == sv.bottom() + 4);
    CHECK(w.rectOf("mixLink")->y == w.rectOf("mixMute2")->y);
    CHECK(mix.valueText("mixSendVal2") == "Send 0");

    // Dragged to its right end: one undo step of the track.
    drag(w, {send.right() - 2, send.y + 8}, {});
    CHECK(h.track(2, Track::Send) == 100.0f);
    CHECK(h.editor.undo.undoSize() == 1);
    CHECK(mix.valueText("mixSendVal2") == "Send 100");
    // Right click: a typed value, Reset to 0.
    CHECK(sliderMenu(w, "mixSend2", 0, "30").back() == "Send (0 to 100):");
    CHECK(h.track(2, Track::Send) == 30.0f);
    CHECK(h.editor.undo.undoSize() == 2);
    sliderMenu(w, "mixSend2", 1);
    CHECK(h.track(2, Track::Send) == 0.0f);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(2, Track::Send) == 30.0f);
    // It follows the parameter.
    h.values[size_t(paramIndex(2, Track::Send))] = 64;
    w.tick(0);
    CHECK(mix.valueText("mixSendVal2") == "Send 64");

    // The 3-channel modes have no delay: no Send.
    for (float mode : {1.0f, 2.0f}) {
        h.values[size_t(paramIndex(Global::VoiceMode))] = mode;
        w.tick(0);
        CHECK_FALSE(w.rectOf("mixSend1"));
    }
}

TEST_CASE("Mixer: M and S, label of the strip", "[ui][mixer]") {
    TestHost h;
    MainWindow w(h);
    w.mixer().show();
    click(w, "mixMute4");
    CHECK(h.track(4, Track::Mute) == 1.0f);
    click(w, "mixSolo7");
    CHECK(h.track(7, Track::Solo) == 1.0f);
    click(w, "mixSolo7");
    CHECK(h.track(7, Track::Solo) == 0.0f);
    CHECK(h.editor.undo.undoSize() == 3);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(7, Track::Solo) == 1.0f);
    CHECK(h.track(4, Track::Mute) == 1.0f);

    // Label: the track becomes the one being edited (focusCh).
    click(w, "mixLab9");
    CHECK(h.values[size_t(paramIndex(Global::FocusTrack))] == 10.0f);

}

TEST_CASE("Oscilloscopes: one per track, opened from the Mixer, offset by 24 px, forgotten on closing", "[ui][mixer]") {
    TestHost h;
    MainWindow w(h);
    w.mixer().show();
    click(w, "mixScope2");
    CHECK_FALSE(w.desktop().hasModal());
    REQUIRE(w.scopeWindow(2));
    CHECK(w.rectOf("scWin2") == gemui::Rect{240, 200, 392, 201});
    CHECK(w.scopeWindow(2)->titleBar().title() == "SCOPE - TRACK 3");
    click(w, "mixScope5");
    CHECK(w.rectOf("scWin5") == gemui::Rect{264, 224, 392, 201});
    // Already open: brought to the front, in its place.
    w.mixer().show();
    click(w, "mixScope2");
    CHECK(w.rectOf("scWin2") == gemui::Rect{240, 200, 392, 201});
    CHECK(gemui::FloatWindow::topmost(w) == w.scopeWindow(2));

    // Track name and 3 ch mode in the title (refreshScopeTitle).
    h.editExtras([](plugin::NonParamState& s) { s.trackNames[5] = "Bass"; });
    w.tick(0);
    CHECK(w.scopeWindow(5)->titleBar().title() == "SCOPE - Bass");

    // Trace: 128 points at 0.5: y = 70 - 0.5 × 126 = 7, 2 px green line on rows 6 and 7 of the canvas.
    plugin::EngineStatus t;
    t.serial = 1;
    t.trackScope[2].fill(0.5f);
    h.pending = t;
    w.tick(0);
    w.desktop().paint();
    const gemui::Rect r = *w.rectOf("scWin2");
    const int cx = r.x + 14 + 2 + 100, cy = r.y + 40 + 2;
    CHECK(w.desktop().surface().at(cx, cy + 5) == gemui::colors::kWhite);
    CHECK(w.desktop().surface().at(cx, cy + 6) == pluginui::TrackScope::kTrace);
    CHECK(w.desktop().surface().at(cx, cy + 7) == pluginui::TrackScope::kTrace);
    CHECK(w.desktop().surface().at(cx, cy + 8) == gemui::colors::kWhite);
    CHECK(w.desktop().surface().at(cx, cy + 70) == gemui::colors::kWhite);
    // A single point (no caller sends one yet): a flat line at its value, without reading past it.
    const std::array<float, 1> one = {0.5f};
    w.scopeWindow(2)->setPoints(one);
    w.desktop().paint();
    CHECK(w.desktop().surface().at(cx, cy + 6) == pluginui::TrackScope::kTrace);
    CHECK(w.desktop().surface().at(cx + 200, cy + 7) == pluginui::TrackScope::kTrace);

    // Escape closes the top window: the oscilloscope brought to the front, the Mixer, the other oscilloscope; when
    // reopened, an oscilloscope takes the first place again.
    gemui::KeyEvent esc;
    esc.key = gemui::Key::Escape;
    w.desktop().keyDown(esc);
    CHECK_FALSE(w.rectOf("scWin2"));
    w.desktop().keyDown(esc);
    CHECK_FALSE(w.mixer().isOpen());
    CHECK(w.rectOf("scWin5"));
    w.desktop().keyDown(esc);
    CHECK_FALSE(w.rectOf("scWin5"));
    w.mixer().show();
    click(w, "mixScope5");
    CHECK(w.rectOf("scWin5") == gemui::Rect{240, 200, 392, 201});

    // 3 ch: name of the chip channel.
    h.values[size_t(paramIndex(Global::VoiceMode))] = 1;
    w.tick(0);
    w.mixer().show();
    click(w, "mixScope2");
    CHECK(w.scopeWindow(2)->titleBar().title() == "SCOPE - C");
}

TEST_CASE("Mixer: 3 ch, strips A B C Digi without panning, faders on the 4-bit volume, mono output",
          "[ui][mixer][3ch]") {
    TestHost h;
    h.values[size_t(paramIndex(Global::VoiceMode))] = 1;
    MainWindow w(h);
    w.mixer().show();
    // + the Master fader (D-276), the values (D-300).
    CHECK(w.rectOf("mixWin") == gemui::Rect{110, 110, 230 + 20, 283 + 12});
    CHECK(w.rectOf("mixFad3"));
    CHECK_FALSE(w.rectOf("mixFad4"));
    CHECK_FALSE(w.rectOf("mixPan0"));
    CHECK_FALSE(w.rectOf("vuhR"));
    // The fader of a chip channel is its 4-bit Volume, the one of the SOURCE frame (sixth DAW test, item 62, D-260):
    // one undo step per drag, the level untouched.
    const float vol = h.track(1, Track::Volume);
    const gemui::Rect fad = *w.rectOf("mixFad1");
    drag(w, {fad.x + 8, fad.y + 2}, {{fad.x + 8, fad.y + 10}, {fad.x + 8, fad.bottom() - 1}});
    CHECK(h.track(1, Track::Volume) == 0.0f);
    CHECK(h.track(1, Track::Level) == 75.0f);
    CHECK(h.editor.undo.undoSize() == 1);
    drag(w, {fad.x + 8, fad.y + 1}, {});
    CHECK(h.track(1, Track::Volume) == 15.0f);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(1, Track::Volume) == 0.0f);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(1, Track::Volume) == vol);
    h.values[size_t(paramIndex(1, Track::Volume))] = 8;
    w.tick(0);
    // A Sync Buzzer has no fixed volume: the fader is greyed out with the SOURCE frame's Volume (D-261).
    h.values[size_t(paramIndex(1, Track::Generator))] = float(int(ymengine::Generator::SyncBuzzer));
    w.tick(0);
    drag(w, {fad.x + 8, fad.y + 2}, {{fad.x + 8, fad.bottom() - 1}});
    CHECK(h.track(1, Track::Volume) == 8.0f);
    // M and S stay enabled.
    click(w, "mixMute3");
    CHECK(h.track(3, Track::Mute) == 1.0f);

    // Two chips: eight strips; back to 16 voices: sixteen strips and the panning.
    h.values[size_t(paramIndex(Global::VoiceMode))] = 2;
    w.tick(0);
    CHECK(w.rectOf("mixFad7"));
    CHECK_FALSE(w.rectOf("mixFad8"));
    h.values[size_t(paramIndex(Global::VoiceMode))] = 0;
    w.tick(0);
    CHECK(w.rectOf("mixPan15"));
    // + the Master faders (D-276, D-284), the values (D-300), the Send (D-299).
    CHECK(w.rectOf("mixWin") == gemui::Rect{110, 110, 1048 + 78, 325 + 24 + 32});
}

TEST_CASE("Mixer: Master fader after a rule, the main level of the SYSTEM frame (D-276)", "[ui][mixer]") {
    for (const int mode : {0, 1}) {
        CAPTURE(mode);
        TestHost h;
        h.values[size_t(paramIndex(Global::VoiceMode))] = float(mode);
        MainWindow w(h);
        w.mixer().show();
        const int last = mode ? 3 : 15;
        const gemui::Rect sep = *w.rectOf("mixSep"), lab = *w.rectOf("mixMasterLab"), fad = *w.rectOf("mixMaster");
        const gemui::Rect strip = *w.rectOf("mixFad" + std::to_string(last));
        CHECK(sep.x > w.rectOf("mixVu" + std::to_string(last))->right());
        CHECK(sep.w == 2);
        CHECK(sep.y == w.rectOf("mixLab0")->y);
        CHECK(sep.bottom() >= w.rectOf("mixSolo0")->bottom());
        CHECK(lab.x > sep.right());
        CHECK(fad.y == strip.y);
        CHECK(fad.h == strip.h);
        CHECK(fad.right() < w.rectOf("mixWin")->right());
        CHECK(w.rectOf("vuhL")->right() > fad.right());
        // Linked to the main level, both ways, without an undo step like the SYSTEM frame's.
        CHECK(h.values[size_t(paramIndex(Global::Master))] == 70.0f);
        drag(w, {fad.x + 8, fad.y + 40}, {{fad.x + 8, fad.y + 60}, {fad.x + 8, fad.bottom() - 1}});
        CHECK(h.values[size_t(paramIndex(Global::Master))] == 0.0f);
        CHECK(h.editor.undo.undoSize() == 0);
        h.values[size_t(paramIndex(Global::Master))] = 50;
        w.tick(0);
        drag(w, {fad.x + 8, fad.y + fad.h / 2}, {});
        CHECK(h.values[size_t(paramIndex(Global::Master))] == 50.0f);
    }
}

TEST_CASE("Mixer: two Masters with Link, left and right with a balance for stereo outputs, chips 1 and 2 in 2x3 "
          "channels, one Master in 3-channel mono (D-284)",
          "[ui][mixer]") {
    // 16 channels and 3 channels with the STE (DMA tracks): stereo, left and right; two chips (ST or STE): a Master per
    // chip, no balance; 3 channels with the ST: mono, one Master.
    struct Mode {
        int voices, computer;
        bool two, chips;
    };
    for (const Mode m : {Mode{0, 0, true, false}, Mode{1, 1, true, false}, Mode{2, 1, true, true},
                         Mode{1, 0, false, false}, Mode{2, 0, true, true}}) {
        CAPTURE(m.voices, m.computer);
        TestHost h;
        h.values[size_t(paramIndex(Global::VoiceMode))] = float(m.voices);
        h.values[size_t(paramIndex(Global::Computer))] = float(m.computer);
        MainWindow w(h);
        w.mixer().show();
        for (const char* id : {"mixMaster2", "mixLink", "mixMaster1Lab", "mixMaster2Lab"}) {
            INFO(id);
            CHECK(bool(w.rectOf(id)) == m.two);
        }
        CHECK(bool(w.rectOf("mixMasterPan")) == (m.two && !m.chips));
        const gemui::Rect l = *w.rectOf("mixMaster"), area = *w.rectOf("mixMasterArea");
        CHECK(l.y == w.rectOf("mixFad0")->y);
        CHECK(area.x == w.rectOf("mixSep")->x);
        CHECK(area.right() >= l.right());
        CHECK(area.right() < w.rectOf("mixWin")->right());
        if (!m.two) {
            CHECK(w.mixer().slider("mixMaster")->tip() == "Master output level");
            continue;
        }
        CHECK(w.mixer().slider("mixMaster")->tip() == (m.chips ? "Master: level of chip 1 (both with Link)"
                                                               : "Master: level of the left output (both with Link)"));
        CHECK(w.mixer().slider("mixMaster2")->tip() == (m.chips ? "Master: level of chip 2 (both with Link)"
                                                                : "Master: level of the right output (both with Link)"));
        const gemui::Rect r = *w.rectOf("mixMaster2"), link = *w.rectOf("mixLink");
        CHECK(r.y == l.y);
        CHECK(r.h == l.h);
        CHECK(r.x == l.right() + 4);
        CHECK(link.y == w.rectOf("mixMute0")->y);
        const std::optional<gemui::Rect> pan = w.rectOf("mixMasterPan");
        if (pan) {
            const std::string lastPan = "mixPan" + std::to_string(m.voices == 1 ? 11 : 15);  // a DMA track in 3 ch
            REQUIRE(w.rectOf(lastPan));
            CHECK(pan->y == w.rectOf(lastPan)->y);
            CHECK(pan->w == w.rectOf(lastPan)->w);
            CHECK(area.right() >= pan->right());
        }

        // Linked (default): either fader sets Master, the right one shows it.
        CHECK(h.values[size_t(paramIndex(Global::MasterLink))] == 1.0f);
        drag(w, {r.x + 8, r.y + 40}, {{r.x + 8, r.y + 60}, {r.x + 8, r.bottom() - 1}});
        CHECK(h.values[size_t(paramIndex(Global::Master))] == 0.0f);
        CHECK(h.values[size_t(paramIndex(Global::Master2))] == 70.0f);
        h.values[size_t(paramIndex(Global::Master))] = 60;
        w.tick(0);
        CHECK(w.mixer().slider("mixMaster2")->value() == 60);
        // Unlinked: the second Master keeps Master's level, then each fader its own; no undo step.
        click(w, "mixLink");
        CHECK(h.values[size_t(paramIndex(Global::MasterLink))] == 0.0f);
        CHECK(h.values[size_t(paramIndex(Global::Master2))] == 60.0f);
        w.tick(0);
        drag(w, {r.x + 8, r.y + r.h / 2}, {{r.x + 8, r.y + 1}});
        CHECK(h.values[size_t(paramIndex(Global::Master2))] == 100.0f);
        CHECK(h.values[size_t(paramIndex(Global::Master))] == 60.0f);
        // Balance: dragged to the right end, a double click centres it.
        if (pan) {
            drag(w, {pan->x + pan->w / 2, pan->y + 8}, {{pan->right() + 20, pan->y + 8}});
            CHECK(h.values[size_t(paramIndex(Global::MasterPan))] == 100.0f);
            w.tick(0);
            w.desktop().mouseDoubleClick({pan->x + pan->w / 2, pan->y + 8}, {});
            CHECK(h.values[size_t(paramIndex(Global::MasterPan))] == 0.0f);
        }
        CHECK(h.editor.undo.undoSize() == 0);
        // Linked again: the second Master follows Master.
        click(w, "mixLink");
        CHECK(h.values[size_t(paramIndex(Global::MasterLink))] == 1.0f);
        w.tick(0);
        CHECK(w.mixer().slider("mixMaster2")->value() == 60);
    }
}

TEST_CASE("Mixer: VU meters (48 dB scale, peak hold, clipping), outputs", "[ui][mixer]") {
    using pluginui::MixerWindow;
    CHECK(MixerWindow::vuPct(0) == 0);
    CHECK(MixerWindow::vuPct(0.0001) == 0);
    CHECK(MixerWindow::vuPct(1) == 100);
    CHECK(MixerWindow::vuPct(2) == 100);
    CHECK(MixerWindow::vuPct(0.5) == Catch::Approx((20 * std::log10(0.5) + 48) / 48 * 100));
    CHECK(MixerWindow::vuPct(std::pow(10.0, -48.0 / 20)) == Catch::Approx(0).margin(1e-9));

    TestHost h;
    MainWindow w(h);
    w.mixer().show();
    const auto& vu = w.mixer().meter(3);
    publish(h, w, 10, [](plugin::EngineStatus& t) {
        t.peak[3] = 1.0f;
        t.outPeak = {0.5f, 0.25f};
    });
    CHECK(vu.fill() == 100);
    CHECK(vu.hold() == 100);
    CHECK(vu.clip());
    CHECK(w.mixer().meter(-1).fill() == Catch::Approx(MixerWindow::vuPct(0.5)));
    CHECK(w.mixer().meter(-2).fill() == Catch::Approx(MixerWindow::vuPct(0.25)));
    // Nothing any more: the fill drops, the peak hold falls by 50 % per second (30 publications).
    publish(h, w, 11, [](plugin::EngineStatus&) {});
    CHECK(vu.fill() == 0);
    CHECK(vu.hold() == Catch::Approx(100 - 50.0 / 30));
    CHECK_FALSE(vu.clip());
    publish(h, w, 26, [](plugin::EngineStatus&) {});  // 15 publications later (only one read)
    CHECK(vu.hold() == Catch::Approx(100 - 16 * 50.0 / 30));

    // 3 ch: a single output, the louder of the two.
    h.values[size_t(paramIndex(Global::VoiceMode))] = 1;
    w.tick(0);
    publish(h, w, 27, [](plugin::EngineStatus& t) { t.outPeak = {0.25f, 0.5f}; });
    CHECK(w.mixer().meter(-1).fill() == Catch::Approx(MixerWindow::vuPct(0.5)));

    // Window reopened: peak holds at zero (buildMixer).
    w.mixer().close();
    w.mixer().show();
    CHECK(w.mixer().meter(0).hold() == 0);
}

TEST_CASE("Mixer: place and state kept when the plugin window reopens", "[ui][mixer]") {
    TestHost h;
    {
        MainWindow w(h);
        w.mixer().show();
        click(w, "mixScope1");
        click(w, "mixScope4");
        REQUIRE(w.scopeWindow(4));
        w.scopeWindow(4)->moveBy(100, 10);
        w.mixer().moveBy(20, 30);
        w.mixer().show();  // in front of the oscilloscopes
        w.openScope(6);
        w.scopeWindow(6)->close();
    }
    MainWindow w(h);
    CHECK(w.mixer().isOpen());
    // + the values (D-300), the Send (D-299)
    CHECK(w.rectOf("mixWin") == gemui::Rect{130, 140, 1048 + 78, 325 + 24 + 32});
    CHECK(w.rectOf("scWin1") == gemui::Rect{240, 200, 392, 201});
    CHECK(w.rectOf("scWin4") == gemui::Rect{364, 234, 392, 201});
    CHECK_FALSE(w.rectOf("scWin6"));
    CHECK(gemui::FloatWindow::topmost(w) == &w.mixer());
}

TEST_CASE("Mixer: a DMA track playing a stereo sample has two oscilloscopes and two meters (Indus, 26/10/2026, D-457)",
          "[ui][mixer][dma]") {
    using pluginui::MixerWindow;
    TestHost h;
    // 16 channels on the STE: track 3 plays slot 2, stereo; track 4 slot 3, mono.
    h.values[size_t(paramIndex(Global::Computer))] = 1;
    for (int t : {3, 4}) h.values[size_t(paramIndex(t, Track::Generator))] = float(ymengine::Generator::Dma);
    h.state.tracks[3].patch.dmaSlot = 2;
    h.state.tracks[4].patch.dmaSlot = 3;
    h.state.dma.slots[2].stereo = true;
    MainWindow w(h);
    w.mixer().show();
    // Stereo: the left and right oscilloscopes side by side, sharing their frame's line, centred on the strip; each
    // side its fader and its meter (D-459), the right side's 4 px after the left meter.
    const auto l = w.rectOf("mixScope3"), r = w.rectOf("mixScopeR3"), vl = w.rectOf("mixVu3"),
               vr = w.rectOf("mixVuR3"), fad = w.rectOf("mixFad3"), pan = w.rectOf("mixPan3");
    REQUIRE(l);
    REQUIRE(r);
    REQUIRE(vl);
    REQUIRE(vr);
    CHECK(r->x == l->right() - 2);
    CHECK(r->y == l->y);
    CHECK(l->x + r->right() == 2 * (pan->x + pan->w / 2));
    CHECK(w.rectOf("mixFadR3")->x == vl->right() + 4);
    CHECK(vr->x == w.rectOf("mixFadR3")->right() + 3);
    CHECK(vl->x == fad->right() + 3);
    // Mono: one oscilloscope and one meter, as before.
    CHECK(w.rectOf("mixScope4"));
    CHECK_FALSE(w.rectOf("mixScopeR4"));
    CHECK_FALSE(w.rectOf("mixVuR4"));
    CHECK(w.rectOf("mixScope4")->w == 30);
    // Tips name the sides: the text of the tooltip shown under p (seen by translateTip).
    double now = 1000;
    auto tipAt = [&](gemui::Point p) {
        std::string seen;
        auto keep = w.desktop().translateTip;
        w.desktop().translateTip = [&seen](const std::string& t) { return seen = t; };
        w.desktop().mouseMove(p, {});
        w.desktop().tick(now);
        now += gemui::Desktop::kTipDelayMs + 1;
        w.desktop().tick(now);
        w.desktop().paint();
        now += 1000;
        w.desktop().translateTip = keep;
        return seen;
    };
    CHECK(tipAt({l->x + 10, l->y + 10}) == "Left side of the stereo sample");
    CHECK(tipAt({r->x + 20, r->y + 10}) == "Right side of the stereo sample");

    // Peaks: the left side's on the left meter, the right side's on the right one; mono, the larger of the two.
    publish(h, w, 10, [](plugin::EngineStatus& t) {
        t.peak[3] = 0.25f;
        t.peakR[3] = 1.0f;
        t.peak[4] = t.peakR[4] = 0.5f;
        for (size_t j = 0; j < t.trackScope[3].size(); ++j) {
            t.trackScope[3][j] = 0.5f;  // mean of 0 on the left and 1 on the right
            t.trackSide[3][j] = 0.5f;
        }
    });
    CHECK(w.mixer().meter(3).fill() == Catch::Approx(MixerWindow::vuPct(0.25)));
    CHECK(w.mixer().meter(3, true).fill() == 100);
    CHECK(w.mixer().meter(3, true).clip());
    CHECK_FALSE(w.mixer().meter(3).clip());
    CHECK(w.mixer().meter(4).fill() == Catch::Approx(MixerWindow::vuPct(0.5)));
    // The left oscilloscope draws its trace at the zero line, the right one high (y = H/2 − v × H × 0.85).
    w.desktop().paint();
    auto darkRow = [&](const gemui::Rect& s) {
        for (int y = s.y + 2; y < s.bottom() - 2; ++y)
            if (w.desktop().surface().at(s.x + 2 + 13, y) != w.desktop().surface().at(s.x + 2 + 13, s.y + 2))
                return y - s.y;
        return -1;
    };
    CHECK(darkRow(*r) < darkRow(*l));

    // The slot turns mono: one oscilloscope, one meter.
    h.editExtras([](plugin::NonParamState& s) { s.dma.slots[2].stereo = false; });
    w.tick(0);
    CHECK_FALSE(w.rectOf("mixScopeR3"));
    CHECK_FALSE(w.rectOf("mixVuR3"));
    CHECK(w.rectOf("mixScope3")->w == 30);
    CHECK(tipAt({w.rectOf("mixScope3")->x + 12, w.rectOf("mixScope3")->y + 8}).empty());
}

TEST_CASE("Mixer: a stereo sample's right side has its fader, M and S, and Link (Indus, 26/10/2026, D-459)",
          "[ui][mixer][dma]") {
    TestHost h;
    // 16 channels on the STE: track 3 plays slot 2, stereo; track 4 slot 3, mono.
    h.values[size_t(paramIndex(Global::Computer))] = 1;
    for (int t : {3, 4}) h.values[size_t(paramIndex(t, Track::Generator))] = float(ymengine::Generator::Dma);
    h.state.tracks[3].patch.dmaSlot = 2;
    h.state.tracks[4].patch.dmaSlot = 3;
    h.state.dma.slots[2].stereo = true;
    MainWindow w(h);
    pluginui::MixerWindow& mix = w.mixer();
    mix.show();
    // Each side its fader and value, M and S under them; Link on a row of its own, under the S.
    const gemui::Rect fl = *w.rectOf("mixFad3"), fr = *w.rectOf("mixFadR3");
    CHECK(fr.y == fl.y);
    CHECK(fr.h == fl.h);
    CHECK(w.rectOf("mixFadRVal3")->y == w.rectOf("mixFadVal3")->y);
    const gemui::Rect ml = *w.rectOf("mixMute3"), mr = *w.rectOf("mixMuteR3");
    const gemui::Rect sl = *w.rectOf("mixSolo3"), sr = *w.rectOf("mixSoloR3"), link = *w.rectOf("mixLink3");
    CHECK(mr.y == ml.y);
    CHECK(sr.y == sl.y);
    CHECK(ml.x < fl.right());
    CHECK(mr.x > fl.right());
    CHECK(link.y == sl.bottom() + 4);
    CHECK(link.x + link.w / 2 == (ml.x + mr.right()) / 2);
    // The mono strip: none of them; its M aligned with the stereo strip's.
    for (const char* id : {"mixFadR4", "mixFadRVal4", "mixMuteR4", "mixSoloR4", "mixLink4"}) CHECK_FALSE(w.rectOf(id));
    CHECK(w.rectOf("mixMute4")->y == ml.y);
    // The Masters' Link stays on the row of M.
    CHECK(w.rectOf("mixLink")->y == ml.y);
    CHECK(mix.valueText("mixFadRVal3") == "75");

    // Linked (the default): the right fader moves the Volume, the left fader with it.
    drag(w, {fr.x + 8, fr.y + 2}, {{fr.x + 8, fr.y + 10}, {fr.x + 8, fr.y + 48}});
    CHECK(h.track(3, Track::Level) == 50.0f);
    CHECK(h.track(3, Track::LevelR) == 75.0f);
    CHECK(mix.valueText("mixFadVal3") == "50");
    CHECK(mix.valueText("mixFadRVal3") == "50");
    CHECK(h.editor.undo.undoSize() == 1);
    // Link off: the right side keeps the level, then moves alone. One undo step each.
    click(w, "mixLink3");
    CHECK(h.track(3, Track::Link) == 0.0f);
    CHECK(h.track(3, Track::LevelR) == 50.0f);
    CHECK(h.editor.undo.undoSize() == 2);
    drag(w, {fr.x + 8, fr.y + 26}, {{fr.x + 8, fr.y + 40}, {fr.x + 8, fr.y + 70}});
    CHECK(h.track(3, Track::Level) == 50.0f);
    CHECK(h.track(3, Track::LevelR) < 50.0f);
    CHECK(h.editor.undo.undoSize() == 3);
    CHECK(mix.field("mixFadRVal3"));  // typed in place like the others (D-415)
    // M and S of each side.
    click(w, "mixMuteR3");
    CHECK(h.track(3, Track::MuteR) == 1.0f);
    CHECK(h.track(3, Track::Mute) == 0.0f);
    click(w, "mixSoloR3");
    CHECK(h.track(3, Track::SoloR) == 1.0f);
    CHECK(h.track(3, Track::Solo) == 0.0f);
    CHECK(h.editor.undo.undoSize() == 5);
    // Undo: Solo R, Mute R, the right fader, then Link back on.
    for (int k = 0; k < 3; ++k) CHECK(ctrl(w, U'z'));
    CHECK(h.track(3, Track::SoloR) == 0.0f);
    CHECK(h.track(3, Track::MuteR) == 0.0f);
    CHECK(h.track(3, Track::LevelR) == 50.0f);
    CHECK(ctrl(w, U'z'));
    CHECK(h.track(3, Track::Link) == 1.0f);

    // The slot turns mono: the strip loses them, and the Link row goes.
    const int tall = w.rectOf("mixWin")->h;
    h.editExtras([](plugin::NonParamState& s) { s.dma.slots[2].stereo = false; });
    w.tick(0);
    for (const char* id : {"mixFadR3", "mixMuteR3", "mixSoloR3", "mixLink3"}) CHECK_FALSE(w.rectOf(id));
    CHECK(w.rectOf("mixWin")->h == tall - 21);
}
