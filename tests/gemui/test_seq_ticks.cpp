// STYM2149 — Ticks timing in the Step Sequencer window (M6, B5, D-149): entries of the Resolution menu, typed speed
// and groove, button and tempo shown; 100 and 200 Hz replay tick in Options > VBL frequency.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using plugin::paramIndex;
using ymengine::SeqTiming;

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

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

// Prompt box open: the proposed text erased, `text` typed, Enter.
void answer(MainWindow& w, const std::string& text) {
    REQUIRE(w.desktop().hasModal());
    for (int i = 0; i < 60; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) {
        gemui::KeyEvent e;
        e.key = gemui::Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
    key(w, gemui::Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
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

std::string text(MainWindow& w, const char* id) {
    const gemui::Widget* x = w.sequencer().widgetOf(id);
    REQUIRE(x);
    if (const auto* b = dynamic_cast<const gemui::Button*>(x)) return b->text();
    if (const auto* v = dynamic_cast<const gemui::ValueField*>(x)) return v->text();
    FAIL("no text");
    return {};
}

bool enabled(MainWindow& w, const char* id) {
    const gemui::Widget* x = w.sequencer().widgetOf(id);
    REQUIRE(x);
    return x->enabled();
}

}  // namespace

TEST_CASE("Ticks: Resolution menu, Ticks timing, typed speed and groove", "[ui][seq][ticks]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    CHECK(text(w, "seqReso") == "1/16");
    CHECK(enabled(w, "bpm"));

    // Resolutions of the prototype (1/16 checked), then below a rule the entries of the Ticks timing.
    click(w, "seqReso");
    std::vector<std::string> l = labels(w);
    REQUIRE(l.size() == 14);  // then the CMD row preference (D-150)
    CHECK(l[8] == "---");
    CHECK(l[9] == "Ticks: Speed ticks per line");
    CHECK(l[10] == "Speed: 6 ticks...");
    CHECK(l[11] == "Groove: 0...");
    CHECK(menu(w).checked() == 2);
    pick(w, 9);
    CHECK(h.state.global.timing == SeqTiming::Ticks);
    CHECK(h.state.global.resolution == 2);  // kept for the grid and for going back to BPM
    CHECK(text(w, "seqReso") == "Ticks 6");
    // Free run: the tempo no longer leads, the value shows that of the lines (6 ticks at 50 Hz in 1/16: 125 BPM).
    CHECK_FALSE(enabled(w, "bpm"));
    CHECK_FALSE(enabled(w, "swing"));
    CHECK(text(w, "bpmVal") == "ticks 125.00");

    // Speed: brought into 1-31; a text that is not a number changes nothing.
    click(w, "seqReso");
    CHECK(menu(w).checked() == 9);
    pick(w, 10);
    answer(w, "3");
    CHECK(h.state.global.speed == 3);
    click(w, "seqReso");
    pick(w, 10);
    answer(w, "99");
    CHECK(h.state.global.speed == 31);
    click(w, "seqReso");
    pick(w, 10);
    answer(w, "x");
    CHECK(h.state.global.speed == 31);
    click(w, "seqReso");
    pick(w, 10);
    answer(w, "6");

    // Groove: up to 16 values from 0 to 15; the button marks it with a "+".
    click(w, "seqReso");
    pick(w, 11);
    answer(w, "1 0, 2 20");
    CHECK(h.state.global.grooveLength == 4);
    CHECK(h.state.global.groove[0] == 1);
    CHECK(h.state.global.groove[2] == 2);
    CHECK(h.state.global.groove[3] == 15);
    CHECK(text(w, "seqReso") == "Ticks 6+");
    click(w, "seqReso");
    CHECK(labels(w)[11] == "Groove: 1 0 2 15...");
    pick(w, 11);
    answer(w, "");
    CHECK(h.state.global.grooveLength == 1);
    CHECK(h.state.global.groove[0] == 0);
    CHECK(text(w, "seqReso") == "Ticks 6");

    // Replay tick at 200 Hz: 6 ticks make lines of 30 ms (500 BPM in 1/16).
    h.values[size_t(paramIndex(plugin::Global::VblRate))] = 4;
    w.tick(0);
    CHECK(text(w, "bpmVal") == "ticks 500.00");

    // A resolution sets the BPM timing back.
    click(w, "seqReso");
    pick(w, 3);
    CHECK(h.state.global.timing == SeqTiming::Bpm);
    CHECK(h.state.global.resolution == 3);
    CHECK(text(w, "seqReso") == "1/32");
    CHECK(enabled(w, "bpm"));
    CHECK(enabled(w, "swing"));
}

TEST_CASE("Ticks: Options > VBL frequency, 50 to 200 Hz", "[ui][ticks]") {
    TestHost h;
    MainWindow w(h);
    w.openMenu(3);
    const auto& items = menu(w).items();
    const auto it = std::find_if(items.begin(), items.end(), [](const gemui::MenuItem& m) { return m.label == "VBL frequency"; });
    REQUIRE(it != items.end());
    std::vector<std::string> sub;
    for (const auto& m : it->sub) sub.push_back(m.label);
    CHECK(sub == std::vector<std::string>{"50 Hz (PAL)", "60 Hz (NTSC)", "70 Hz (Monochrome)", "100 Hz", "200 Hz"});
}
