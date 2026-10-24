// STYM2149 — Step Sequencer window (M5): All button of the gate (D-134), Gate slider, Clock selector (D-188);
// Options > Sound engine > Quantize to VBL (D-111).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;

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

    float v(const char* id) const { return values[size_t(plugin::findParam(id))]; }
};

void press(MainWindow& w, gemui::Point p) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(p, {});
}

void click(MainWindow& w, const char* id) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, {r->x + r->w / 2, r->y + r->h / 2});
}

// Press at the left or right edge of the Gate track: the thumb goes there, minimum or maximum.
// Ends of a slider's track, inside its arrows (D-297).
int trackEnd(const gemui::Rect& r, bool max) {
    return max ? r.right() - 1 - gemui::Slider::kArrow : r.x + gemui::Slider::kArrow;
}

void gateTo(MainWindow& w, bool max) {
    const auto r = w.rectOf("gate");
    REQUIRE(r);
    press(w, {trackEnd(*r, max), r->y + r->h / 2});
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

// Picks entry `label` of the open menu.
void pick(MainWindow& w, const std::string& label) {
    const std::vector<std::string> l = labels(w);
    const auto at = std::find(l.begin(), l.end(), label);
    REQUIRE(at != l.end());
    const gemui::Rect r = menu(w).rowsFrom(int(at - l.begin()));
    REQUIRE(r.h > 0);
    press(w, {r.x + 20, r.y + 10});
}

}  // namespace

TEST_CASE("Step Sequencer: All button of the global gate, on the row of the Gate slider (D-134)", "[ui][seq]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const auto all = w.rectOf("seqGateAll");
    const auto gate = w.rectOf("gate");
    const auto tempo = w.rectOf("bpm");
    REQUIRE(all);
    REQUIRE(gate);
    REQUIRE(tempo);
    // Between the Gate label and the track, without enlarging the row (16 px): the grid does not move.
    CHECK(all->h <= 16);
    CHECK(all->x + all->w <= gate->x);
    CHECK(all->x > tempo->x + tempo->w);
    CHECK(all->y >= tempo->y);
    CHECK(all->y + all->h <= tempo->y + 16);

    // Off: the slider sets the gate of the shown track.
    gateTo(w, true);
    CHECK(h.v("t01_gate") == 95.0f);
    CHECK(h.v("gate") == 50.0f);

    // On: the slider sets the global gate, that of the tracks stays.
    click(w, "seqGateAll");
    CHECK(h.v("gateAll") == 1.0f);
    w.tick(0);
    gateTo(w, false);
    CHECK(h.v("gate") == 5.0f);
    CHECK(h.v("t01_gate") == 95.0f);

    // Off again: the track takes over again.
    click(w, "seqGateAll");
    CHECK(h.v("gateAll") == 0.0f);
    w.tick(0);
    gateTo(w, false);
    CHECK(h.v("t01_gate") == 5.0f);
    CHECK(h.v("gate") == 5.0f);
}

TEST_CASE("Step Sequencer: Clock on MIDI ext, BPM slider greyed out (D-188)", "[ui][seq]") {
    TestHost h;
    h.state.global.seqClock = ymengine::SeqClock::Internal;
    MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    auto bpmTo = [&](bool max) {
        const auto r = w.rectOf("bpm");
        REQUIRE(r);
        press(w, {trackEnd(*r, max), r->y + r->h / 2});
    };
    bpmTo(true);
    CHECK(h.v("freeTempo") == 240.0f);
    // MIDI ext: the tempo comes from the MIDI clock, the slider no longer responds (the prototype's setClkMode).
    h.values[size_t(plugin::findParam("arpClock"))] = 1;
    w.tick(0);
    bpmTo(false);
    CHECK(h.v("freeTempo") == 240.0f);
    // Internal: the slider takes over again.
    h.values[size_t(plugin::findParam("arpClock"))] = 0;
    w.tick(0);
    bpmTo(false);
    CHECK(h.v("freeTempo") == 60.0f);
}

TEST_CASE("Options > Sound engine > Quantize to VBL, state setting (D-111)", "[ui][seq]") {
    TestHost h;
    MainWindow w(h);
    auto choose = [&](const std::string& value) {
        w.openMenu(3);  // Options
        pick(w, "Sound engine");
        pick(w, "Quantize to VBL");
        CHECK(labels(w) == std::vector<std::string>{"Off", "On"});
        pick(w, value);
        w.tick(0);
    };
    CHECK_FALSE(h.state.global.quantizeVbl);
    choose("On");
    CHECK(h.state.global.quantizeVbl);
    choose("Off");
    CHECK_FALSE(h.state.global.quantizeVbl);
}

TEST_CASE("Step Sequencer: right click on the metronome, its volume by slider (D-288)", "[ui][seq][metronome]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const gemui::Rect m = *w.rectOf("seqMetro");
    const gemui::Point c{m.x + m.w / 2, m.y + m.h / 2};
    w.desktop().mouseMove(c, {});
    w.desktop().mouseDown(c, gemui::MouseButton::Right, {});
    w.desktop().mouseUp(c, {});
    REQUIRE(w.desktop().hasPopups());
    CHECK_FALSE(h.state.global.metronome);  // the right click does not switch it on
    CHECK(h.state.global.metronomeLevel == 100);
    // 96 px vertical slider below the button: its bottom is worth 0, its middle about half.
    press(w, {m.x + 2 + 4 + 2 + 8, m.bottom() + 2 + 8 + 95});
    CHECK(h.state.global.metronomeLevel == 0);
    press(w, {m.x + 2 + 4 + 2 + 8, m.bottom() + 2 + 8 + 48});
    CHECK(h.state.global.metronomeLevel >= 45);
    CHECK(h.state.global.metronomeLevel <= 55);
}
