// STYM2149 — Sync Buzzer settings of the GENERATOR frame, which replaced the Buzzer window (
// fourth DAW test, item 40): tuned envelope (N, D, fine tuning), YM Pad, presets as one undo step, the Sync
// Buzzer steps of the former Timer Wave window; Env freq greyed out when the envelope is tuned.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/BuzzerPresets.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Pitch.h"

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

// Drag of a slider of the GENERATOR frame from its middle past the right (or left) end of its track.
void dragSlider(MainWindow& w, const char* id, bool toMax) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    const int y = r->y + r->h / 2, mid = r->x + r->w / 2;
    drag(w, {{mid, y}, {mid + 2, y}, {toMax ? r->right() + 20 : r->x - 20, y}});
}

int param(const TestHost& h, Track f, int track = 0) { return int(h.values[size_t(paramIndex(track, f))]); }

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

// A Sync Buzzer on track 1.
// Picks a form of the SID wave in its menu.
void form(MainWindow& w, const std::string& name) {
    click(w, "waveForm");
    const std::vector<std::string> l = labels(w);
    const auto it = std::find(l.begin(), l.end(), name);
    REQUIRE(it != l.end());
    pick(w, int(it - l.begin()));
}

void syncBuzzer(TestHost& h) {
    h.values[size_t(paramIndex(0, Track::Generator))] = float(ymengine::Generator::SyncBuzzer);
}

}  // namespace

TEST_CASE("GENERATOR frame of a Sync Buzzer: the settings and presets of the former Buzzer window, then its steps "
          "(third DAW test, item 28; sixth DAW test, items 54 and 80)",
          "[ui][buzzer][generator]") {
    TestHost h;
    syncBuzzer(h);
    MainWindow w(h);
    w.tick(0);
    for (const char* id : {"genTuned", "genN", "genD", "genFine", "genPad", "genPresets", "genSep", "buzz0", "buzz7",
                           "buzzSteps"}) {
        CAPTURE(id);
        CHECK(w.rectOf(id));
    }
    // Steps under YM Pad and Presets, inside the frame; no Timer wave button any more.
    const gemui::Rect frame = *w.rectOf("genFrame"), pad = *w.rectOf("genPad");
    for (const char* id : {"buzz0", "buzz3", "buzz4", "buzz7", "buzzSteps"}) {
        CAPTURE(id);
        const gemui::Rect r = *w.rectOf(id);
        CHECK(r.y > pad.bottom());
        CHECK(r.x > frame.x);
        CHECK(r.right() < frame.right());
        CHECK(r.bottom() < frame.bottom());
    }
    CHECK(w.rectOf("buzz3")->y == w.rectOf("buzz0")->y);
    CHECK(w.rectOf("buzz4")->y > w.rectOf("buzz0")->bottom());
    CHECK_FALSE(w.rectOf("timerWave"));
    // No Buzzer window any more, nor its Edit menu entry.
    CHECK_FALSE(w.rectOf("buzWin"));
    w.openMenu(2);
    const std::vector<std::string> edit = labels(w);
    CHECK(std::find(edit.begin(), edit.end(), "Buzzer") == edit.end());
    key(w, gemui::Key::Escape);
    const ymengine::TrackPatch& p = h.patch();

    // N, D and Fine: no effect while the envelope is not tuned.
    const auto n = w.rectOf("genN");
    REQUIRE(n);
    press(w, {n->right() - 2, n->y + n->h / 2});
    CHECK(p.envRatioN == 1);
    // Tuned: one undo step; then N follows the slider.
    const size_t undo = h.editor.undo.undoSize();
    click(w, "genTuned");
    CHECK(p.envTuned);
    CHECK(h.editor.undo.undoSize() == undo + 1);
    w.tick(0);
    press(w, {n->right() - 2, n->y + n->h / 2});
    CHECK(p.envRatioN > 1);
    click(w, "genPad");
    CHECK(p.ymPad);

    // Another generator: the settings leave the frame.
    h.values[size_t(paramIndex(0, Track::Generator))] = float(ymengine::Generator::Chip);
    w.tick(10);
    CHECK_FALSE(w.rectOf("genTuned"));
    CHECK_FALSE(w.rectOf("genPresets"));
    CHECK_FALSE(w.rectOf("timerWave"));
}

TEST_CASE("Buzzer: tuned envelope, N, D, fine tuning; Env freq greyed out on a Sync Buzzer only (fourth DAW test, "
          "item 34)",
          "[ui][buzzer][undo]") {
    TestHost h;
    syncBuzzer(h);
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();

    click(w, "genTuned");
    CHECK(p.envTuned);
    CHECK(h.editor.undo.undoSize() == 1);
    w.tick(0);
    dragSlider(w, "genN", true);
    CHECK(p.envRatioN == 16);
    dragSlider(w, "genD", true);
    CHECK(p.envRatioD == 16);
    dragSlider(w, "genFine", false);
    CHECK(p.envFineCents == -100);
    CHECK(h.editor.undo.undoSize() == 4);
    undoKey(w);
    CHECK(p.envFineCents == 0);

    // Main window: Env freq no longer sets anything while the envelope is tuned.
    const int ep = param(h, Track::EnvPeriod);
    const gemui::Rect ef = *w.rectOf("ef");
    press(w, {ef.x + 4, ef.y + ef.h / 2});
    CHECK(param(h, Track::EnvPeriod) == ep);
    click(w, "genTuned");
    CHECK_FALSE(p.envTuned);
    w.tick(0);
    press(w, {ef.x + 4, ef.y + ef.h / 2});
    CHECK(param(h, Track::EnvPeriod) != ep);

    // Chip Voice: the tuning kept in the patch no longer applies, Env freq sets the period.
    click(w, "genTuned");
    CHECK(p.envTuned);
    w.pickGenerator(int(ymengine::Generator::Chip));
    w.tick(0);
    const int ep2 = param(h, Track::EnvPeriod);
    press(w, {ef.right() - 4, ef.y + ef.h / 2});
    CHECK(param(h, Track::EnvPeriod) != ep2);
    CHECK(p.envTuned);

    // Percussion: settings without effect.
    w.pickGenerator(int(ymengine::Generator::Chipdrum));
    w.tick(0);
    CHECK_FALSE(w.rectOf("genTuned"));
}

TEST_CASE("Buzzer: presets in a menu keep the Sync Buzzer, a single undo step (fourth DAW test, item 32)",
          "[ui][buzzer][undo]") {
    TestHost h;
    syncBuzzer(h);
    h.values[size_t(paramIndex(0, Track::Noise))] = 1;
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();
    click(w, "genPresets");
    std::vector<std::string> names;
    for (const pluginui::BuzzerPreset& b : pluginui::kBuzzerPresets) names.push_back(b.name);
    CHECK(labels(w) == names);
    CHECK(names == std::vector<std::string>{"Saw", "Triangle", "Third", "Fifth", "Seventh", "Tenth", "Tri fifth",
                                            "Soft saw"});
    const size_t undo = h.editor.undo.undoSize();
    pick(w, 6);  // Tri fifth
    CHECK_FALSE(w.desktop().hasPopups());
    // The generator, the volume mode, the tone and the noise stay.
    CHECK(param(h, Track::Generator) == int(ymengine::Generator::SyncBuzzer));
    CHECK(param(h, Track::VolMode) == 0);
    CHECK(param(h, Track::Tone) == 1);
    CHECK(param(h, Track::Noise) == 1);
    CHECK(param(h, Track::EnvShape) == 10);
    CHECK(p.envTuned);
    CHECK(p.envRatioN == 3);
    CHECK(p.envRatioD == 2);
    CHECK(p.envFineCents == 0);
    CHECK_FALSE(p.ymPad);
    CHECK(h.editor.undo.undoSize() == undo + 1);
    undoKey(w);
    CHECK_FALSE(p.envTuned);
    CHECK(p.envRatioN == 1);
    CHECK(param(h, Track::EnvShape) == 8);

    // Every preset: a Sync Buzzer with its shape, its ratio and its YM Pad.
    for (size_t k = 0; k < pluginui::kBuzzerPresets.size(); ++k) {
        CAPTURE(k);
        const pluginui::BuzzerPreset& b = pluginui::kBuzzerPresets[k];
        click(w, "genPresets");
        pick(w, int(k));
        CHECK(param(h, Track::Generator) == int(ymengine::Generator::SyncBuzzer));
        CHECK(param(h, Track::EnvShape) == b.shape);
        CHECK(p.envRatioN == b.n);
        CHECK(p.envRatioD == b.d);
        CHECK(p.ymPad == b.pad);
    }
}

TEST_CASE("GENERATOR frame of a SID Voice: the SID wave under its settings (sixth DAW test, items 53 and 80)",
          "[ui][generator]") {
    TestHost h;
    h.values[size_t(paramIndex(0, Track::Generator))] = float(ymengine::Generator::Sid);
    MainWindow w(h);
    w.tick(0);
    CHECK_FALSE(w.rectOf("genTuned"));
    CHECK_FALSE(w.rectOf("buzz0"));
    CHECK_FALSE(w.rectOf("timerWave"));
    // Base, the plain square: the menu alone, no bars and no Steps (Indus, 24/10/2026 20:30).
    CHECK(w.rectOf("waveForm"));
    CHECK_FALSE(w.rectOf("waveGrid"));
    CHECK_FALSE(w.rectOf("waveSteps"));
    form(w, "Custom");
    w.tick(0);
    const gemui::Rect frame = *w.rectOf("genFrame"), grid = *w.rectOf("waveGrid");
    CHECK(grid.w == pluginui::SidWaveGrid::kWidth);
    CHECK(grid.h == pluginui::SidWaveGrid::kHeight);
    // SID min vol ends with Timer ratio (thirteenth DAW test, item 163); both start at the frame's inner edge.
    const gemui::Rect ratio = *w.rectOf("ratio"), vmin = *w.rectOf("vmin");
    CHECK(vmin.x == ratio.x);
    CHECK(vmin.right() == ratio.right());
    // The menu of the SID wave's forms above the grid, after its name, Steps under it.
    for (const char* id : {"waveGrid", "waveSteps", "waveForm"}) {
        CAPTURE(id);
        const gemui::Rect r = *w.rectOf(id);
        CHECK(r.x > frame.x);
        CHECK(r.right() < frame.right());
        CHECK(r.bottom() < frame.bottom());
    }
    CHECK(w.rectOf("waveForm")->bottom() < grid.y);
    CHECK(w.rectOf("waveSteps")->y > grid.bottom());
    // A Chip Voice: no SID wave.
    h.values[size_t(paramIndex(0, Track::Generator))] = float(ymengine::Generator::Chip);
    w.tick(10);
    CHECK_FALSE(w.rectOf("waveGrid"));
}

TEST_CASE("GENERATOR frame: N, D, Fine and the Steps of the Sync Buzzer and of the SID wave typed (sixth DAW test, "
          "item 66)",
          "[ui][buzzer][undo]") {
    TestHost h;
    syncBuzzer(h);
    MainWindow w(h);
    w.tick(0);
    const ymengine::TrackPatch& p = h.patch();

    // Greyed out with their sliders while the envelope is not tuned.
    click(w, "genNv");
    CHECK_FALSE(field(w, "genNv").editing());
    click(w, "genTuned");
    w.tick(0);
    typeValue(w, "genNv", "5");
    CHECK(p.envRatioN == 5);
    typeValue(w, "genDv", "40");  // brought back into the range of the slider
    CHECK(p.envRatioD == 16);
    typeValue(w, "genFinev", "-25");
    CHECK(p.envFineCents == -25);
    w.tick(0);
    CHECK(field(w, "genFinev").text() == "-25c");
    CHECK(field(w, "genNv").text() == "5");
    typeValue(w, "genFinev", "x");  // no number: nothing changes
    CHECK(p.envFineCents == -25);
    CHECK(h.editor.undo.undoSize() == 4);  // Tuning, then one step per typed value

    typeValue(w, "buzzStepsv", "3");
    CHECK(p.buzzLen == 3);
    undoKey(w);
    CHECK(p.buzzLen == 0);

    // SID wave: Steps hidden at Base; with Custom, typed, from 2 to 16.
    w.pickGenerator(int(ymengine::Generator::Sid));
    w.tick(0);
    CHECK_FALSE(w.rectOf("waveStepsv"));
    form(w, "Custom");
    w.tick(0);
    typeValue(w, "waveStepsv", "6");
    CHECK(p.sidWaveLen == 6);
    typeValue(w, "waveStepsv", "1");
    CHECK(p.sidWaveLen == 2);

    // Sine keeps the number of Steps and fills the 16 bars, those beyond the Steps faded (Indus, 24/10/2026 11:24);
    // from Base, as many as its Period, 8.
    typeValue(w, "waveStepsv", "6");
    form(w, "Sine");
    w.tick(0);
    CHECK(p.sidWaveLen == 6);
    CHECK(p.sidWave == ymengine::sineSidWave());
    CHECK(w.sidWave().state().length == 6);
    CHECK(w.waveFormButton().text() == "Sine");
    typeValue(w, "waveStepsv", "12");
    w.tick(0);
    CHECK(p.sidWaveLen == 12);
    CHECK(p.sidWave == ymengine::sineSidWave());  // Steps play more of the same sine
    form(w, "Base");
    form(w, "Sine");
    CHECK(p.sidWaveLen == 8);
}
