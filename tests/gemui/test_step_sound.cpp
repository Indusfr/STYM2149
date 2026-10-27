// STYM2149 — window of the settings a step's GEN, SRC or ENV cell brings into play, opened by a right click on the
// cell (24/10/2026, D-392; before: the right button erased the cell)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>

#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "pluginui/StepSoundWindow.h"
#include "ymengine/Pitch.h"

using namespace stym;
using pluginui::MainWindow;
using pluginui::SeqLane;
using pluginui::StepSoundWindow;
using ymengine::Generator;

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
    std::map<std::string, std::string, std::less<>> prefs;
    std::string preference(std::string_view k, std::string_view fallback) const override {
        const auto it = prefs.find(k);
        return it == prefs.end() ? std::string(fallback) : it->second;
    }
    void setPreference(std::string_view k, std::string_view v) override { prefs[std::string(k)] = std::string(v); }
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}

    float& track(plugin::Track f, int t = 0) { return values[size_t(plugin::paramIndex(t, f))]; }
    const ymengine::TrackPatch& patch(int t = 0) const { return state.tracks[size_t(t)].patch; }
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void press(MainWindow& w, gemui::Point p, gemui::MouseButton b = gemui::MouseButton::Left) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, b, {});
    w.desktop().mouseUp(p, {});
}

const ymseq::Lane& lane0(TestHost& h) { return h.state.seq.cur().lane(0); }

// The sequencer open on track 1, the cells of `edit` written; a right click on the cell of `row` at step `s`.
struct Fixture {
    TestHost h;
    MainWindow w{h};
    double now = 0;

    Fixture() { w.sequencer().show(); }
    void tick() { w.tick(now += 10); }
    void edit(const std::function<void(ymseq::Lane&)>& f) {
        h.editExtras([&](plugin::NonParamState& st) { f(st.seq.cur().edit(0)); });
        tick();
    }
    gemui::Rect cell(SeqLane row, int s) { return w.sequencer().grid().laneRect(s, row); }
    void rightClick(SeqLane row, int s) {
        const gemui::Rect r = cell(row, s);
        REQUIRE(r.w > 0);
        press(w, centre(r), gemui::MouseButton::Right);
        tick();
    }
    StepSoundWindow& win() { return w.stepSoundWindow(); }
};

// Picks a form of the SID wave in the menu of the window's "waveForm" button (D-411).
void pickForm(Fixture& f, const std::string& name) {
    const gemui::Button* b = f.win().button("waveForm");
    REQUIRE(b);
    REQUIRE(b->onClick);
    b->onClick();
    const auto* m = dynamic_cast<const gemui::MenuList*>(f.w.desktop().topPopup());
    REQUIRE(m);
    const auto it = std::find_if(m->items().begin(), m->items().end(),
                                 [&](const gemui::MenuItem& i) { return i.label == name; });
    REQUIRE(it != m->items().end());
    const std::function<void()> pick = it->pick;
    f.w.desktop().closePopups();
    pick();
    f.tick();
}

}  // namespace

TEST_CASE("Right click on a GEN cell: the GENERATOR settings of its generator, the cell kept (D-392)",
          "[ui][seq][stepsound]") {
    Fixture f;
    f.h.track(plugin::Track::Generator) = float(Generator::Chip);
    f.edit([](ymseq::Lane& l) {
        l.fx[2] = 2;  // SID Voice
        l.fx[4] = 3;  // Sync Buzzer
        l.fx[6] = 1;  // Chip Voice
    });
    StepSoundWindow& win = f.win();
    CHECK_FALSE(win.visible());

    // SID Voice: Timer ratio, SID min vol, the SID wave, the menu of its forms and its Steps.
    f.rightClick(SeqLane::Fx, 2);
    REQUIRE(win.visible());
    CHECK(win.kind() == StepSoundWindow::Kind::Generator);
    CHECK(win.generator() == int(Generator::Sid));
    CHECK(win.titleBar().title() == "GENERATOR - 1 [SID Voice]");
    CHECK(lane0(f.h).fx[2] == 2);  // not erased
    CHECK(f.w.desktop().topPopup() == nullptr);  // no menu on release
    for (const char* id : {"ratio", "vmin", "waveSteps"}) CHECK(win.slider(id));
    REQUIRE(win.button("waveForm"));
    CHECK(win.button("waveForm")->text() == "Base");
    CHECK_FALSE(win.slider("buzN"));
    // Its controls are those of the main window: the same parameter, shown on both sides.
    REQUIRE(win.slider("ratio")->onChange);
    win.slider("ratio")->onChange(200);
    CHECK(std::abs(f.h.track(plugin::Track::SidRatio) - 2.0f) < 1e-4f);
    f.h.track(plugin::Track::SidRatio) = 3.0f;
    f.tick();
    CHECK(win.slider("ratio")->value() == 300);
    CHECK(win.field("ratiov")->text() == "3.00");
    REQUIRE(win.field("ratiov")->onCommit);
    CHECK(win.field("ratiov")->onCommit("1.50"));
    CHECK(std::abs(f.h.track(plugin::Track::SidRatio) - 1.5f) < 1e-4f);
    // As in the main window: with Base, the plain square, the menu alone, neither the bars nor Steps (Indus,
    // 24/10/2026 20:30, D-417), nor Width nor Period.
    REQUIRE(win.rectOf("waveForm"));
    for (const char* id : {"stepWave", "waveSteps", "waveStepsv", "waveWidth", "wavePeriod"}) {
        INFO(id);
        CHECK_FALSE(win.rectOf(id));
    }
    const gemui::Rect frame = *win.rectOf("stepWin");
    // Square: the menu on the line of the SID wave's name, above the bars; Steps on the line of its label, under the
    // bars (17:40 to 17:51, D-411, D-412), the window the same size; the width of the square right under Steps
    // (D-408), the sliders lined up, the period hidden; its Steps work, 8 by default.
    pickForm(f, "Square");
    CHECK(f.h.patch().sidForm == ymengine::SidForm::Square);
    CHECK(win.button("waveForm")->text() == "Square");
    CHECK(*win.rectOf("stepWin") == frame);
    REQUIRE(win.rectOf("stepWave"));
    REQUIRE(win.rectOf("waveSteps"));
    CHECK(win.rectOf("waveForm")->bottom() < win.rectOf("stepWave")->y);
    CHECK(win.rectOf("waveSteps")->y > win.rectOf("stepWave")->bottom());
    const gemui::Label* stepsLabel = win.label("waveSteps");
    REQUIRE(stepsLabel);
    CHECK(stepsLabel->bounds().y >= win.rectOf("waveSteps")->y - 4);
    CHECK(stepsLabel->bounds().right() < win.rectOf("waveSteps")->x);
    CHECK(win.slider("waveSteps")->enabled());
    CHECK(win.slider("waveSteps")->value() == 8);
    REQUIRE(win.slider("waveWidth"));
    REQUIRE(win.slider("wavePeriod"));
    REQUIRE(win.rectOf("waveWidth"));
    CHECK(win.rectOf("waveWidth")->y > win.rectOf("waveSteps")->bottom());
    CHECK(win.rectOf("waveWidth")->x == win.rectOf("waveSteps")->x);
    CHECK(win.rectOf("waveWidth")->right() == win.rectOf("waveSteps")->right());
    CHECK_FALSE(win.rectOf("wavePeriod"));
    CHECK_FALSE(win.rectOf("wavePeriodv"));
    CHECK(win.field("waveWidthv")->text() == "4");
    CHECK(win.slider("waveWidth")->max() == 7);
    REQUIRE(win.field("waveWidthv")->onCommit);
    CHECK(win.field("waveWidthv")->onCommit("5"));
    CHECK(f.h.patch().sidWidth == 5);
    f.tick();
    CHECK(win.field("waveWidthv")->text() == "5");
    CHECK(win.slider("waveWidth")->value() == 5);
    REQUIRE(win.field("waveStepsv")->onCommit);
    CHECK(win.field("waveStepsv")->onCommit("12"));
    CHECK(f.h.patch().sidWaveLen == 12);
    f.tick();
    CHECK(win.slider("waveWidth")->max() == 11);  // its range follows the main window's
    const gemui::Rect place = *win.rectOf("waveWidth");
    CHECK(*win.rectOf("stepWin") == frame);
    // Sine: the period of the sine in its place (Indus, 24/10/2026 15:50 and 16:19, D-406), the window the same size.
    pickForm(f, "Sine");
    CHECK(f.h.patch().sidForm == ymengine::SidForm::Sine);
    CHECK_FALSE(win.rectOf("waveWidth"));
    REQUIRE(win.rectOf("wavePeriod"));
    CHECK(*win.rectOf("wavePeriod") == place);
    CHECK(*win.rectOf("stepWin") == frame);
    CHECK(win.field("wavePeriodv")->text() == "8");
    REQUIRE(win.field("wavePeriodv")->onCommit);
    CHECK(win.field("wavePeriodv")->onCommit("4"));
    CHECK(f.h.patch().sidWavePeriod == 4);
    CHECK(f.h.patch().sidWave == ymengine::sineSidWave(3, 15, 8, 4));
    f.tick();
    CHECK(win.field("wavePeriodv")->text() == "4");
    // Bounce: the period too (D-414), the sine of the levels at that period.
    pickForm(f, "Bounce");
    CHECK(f.h.patch().sidForm == ymengine::SidForm::Bounce);
    CHECK(f.h.patch().sidWave == ymengine::sineSidWave(3, 15, 8, 4, true));
    REQUIRE(win.rectOf("wavePeriod"));
    CHECK(*win.rectOf("wavePeriod") == place);
    CHECK_FALSE(win.rectOf("waveWidth"));
    // Custom: neither, the window the same size; Square: the width again, kept.
    pickForm(f, "Custom");
    CHECK(f.h.patch().sidForm == ymengine::SidForm::Custom);
    CHECK_FALSE(win.rectOf("wavePeriod"));
    CHECK_FALSE(win.rectOf("waveWidth"));
    CHECK(*win.rectOf("stepWin") == frame);
    pickForm(f, "Square");
    CHECK(win.rectOf("waveWidth"));
    CHECK_FALSE(win.rectOf("wavePeriod"));
    CHECK(win.slider("waveWidth")->value() == 5);

    // Sync Buzzer: Tuning, N, D, Fine, YM Pad, Presets, its steps and their number.
    f.rightClick(SeqLane::Fx, 4);
    CHECK(win.generator() == int(Generator::SyncBuzzer));
    CHECK(win.titleBar().title() == "GENERATOR - 1 [Sync Buzzer]");
    for (const char* id : {"buzN", "buzD", "buzFine", "buzzLen"}) CHECK(win.slider(id));
    for (const char* id : {"buzTuned", "buzPad", "buzPresets", "buzzStep0", "buzzStep7"}) CHECK(win.button(id));
    CHECK_FALSE(win.slider("ratio"));
    CHECK_FALSE(win.rectOf("stepWave"));
    // Steps: the number drives the track's own, then a step button opens the menu of the main window's step.
    CHECK_FALSE(win.button("buzzStep0")->enabled());
    REQUIRE(win.slider("buzzLen")->onChange);
    win.slider("buzzLen")->onChange(4);
    CHECK(f.h.state.tracks[0].patch.buzzLen == 4);
    f.tick();
    CHECK(win.button("buzzStep3")->enabled());
    CHECK_FALSE(win.button("buzzStep4")->enabled());
    press(f.w, centre(*win.rectOf("buzzStep0")));
    CHECK(f.w.desktop().topPopup() != nullptr);
    f.w.desktop().closePopups();

    // Chip Voice: no settings of its own, a line says so.
    f.rightClick(SeqLane::Fx, 6);
    CHECK(win.generator() == int(Generator::Chip));
    CHECK(win.rectOf("stepNone"));
    CHECK_FALSE(win.slider("ratio"));
    CHECK_FALSE(win.button("buzTuned"));

    // An empty cell: the right button erases as before, no window.
    win.close();
    f.rightClick(SeqLane::Fx, 3);
    CHECK_FALSE(win.visible());
    CHECK(lane0(f.h).fx[2] == 2);
}

TEST_CASE("Right click on a SRC cell: the SOURCE settings, greyed when the cell's source does not use them (D-392)",
          "[ui][seq][stepsound]") {
    Fixture f;
    f.h.track(plugin::Track::Generator) = float(Generator::Chip);
    f.h.track(plugin::Track::Noise) = 1;  // the main window's Noise period is live
    f.edit([](ymseq::Lane& l) {
        l.mix[1] = 1;                          // Tone
        l.mix[3] = 2;                          // Noise
        l.mix[5] = ymengine::kSeqMixEnv + 1;   // Tone+Env
    });
    StepSoundWindow& win = f.win();

    f.rightClick(SeqLane::Mix, 1);
    REQUIRE(win.visible());
    CHECK(win.kind() == StepSoundWindow::Kind::Source);
    CHECK(win.titleBar().title() == "SOURCE - 1");
    CHECK(lane0(f.h).mix[1] == 1);
    CHECK_FALSE(win.slider("np")->enabled());  // Tone alone: no noise
    CHECK(win.label("np")->dim());
    CHECK(win.slider("vol")->enabled());
    CHECK(win.slider("level")->enabled());

    f.rightClick(SeqLane::Mix, 3);
    CHECK(win.slider("np")->enabled());
    CHECK(win.slider("vol")->enabled());
    // The slider drives the track's noise period.
    win.slider("np")->onChange(20);
    CHECK(f.h.track(plugin::Track::NoisePeriod) == 20);
    f.tick();
    CHECK(win.slider("np")->value() == 20);

    f.rightClick(SeqLane::Mix, 5);  // the envelope as volume: the fixed volume does not play
    CHECK_FALSE(win.slider("np")->enabled());
    CHECK_FALSE(win.slider("vol")->enabled());
    CHECK(win.slider("level")->enabled());
}

TEST_CASE("Right click on an ENV cell: Env freq and EP of the track (D-392)", "[ui][seq][stepsound]") {
    Fixture f;
    f.h.track(plugin::Track::Generator) = float(Generator::SyncBuzzer);  // the envelope plays: ENV row shown
    f.edit([](ymseq::Lane& l) { l.setCmd(2, ymengine::seqCmd(ymengine::SeqCmd::Shape, 10)); });
    StepSoundWindow& win = f.win();
    f.rightClick(SeqLane::Env, 2);
    REQUIRE(win.visible());
    CHECK(win.kind() == StepSoundWindow::Kind::Envelope);
    CHECK(win.titleBar().title() == "ENVELOPE - 1");
    CHECK(win.slider("ef"));
    REQUIRE(win.field("ep"));
    CHECK(lane0(f.h).cmdAt(ymengine::SeqCmd::Shape, 2) == ymengine::seqCmd(ymengine::SeqCmd::Shape, 10));
    // The EP field follows the track's envelope period.
    f.h.track(plugin::Track::EnvPeriod) = 1234;
    f.tick();
    CHECK(win.field("ep")->text().find("1234") != std::string::npos);
}

TEST_CASE("Step settings window: in front, its height fits its rows, closed with another track (D-392)",
          "[ui][seq][stepsound]") {
    Fixture f;
    f.h.track(plugin::Track::Generator) = float(Generator::Chip);
    f.edit([](ymseq::Lane& l) {
        l.fx[2] = 2;
        l.fx[6] = 1;
    });
    StepSoundWindow& win = f.win();
    f.rightClick(SeqLane::Fx, 2);
    const gemui::Rect sid = *win.rectOf("stepWin");
    f.rightClick(SeqLane::Fx, 6);
    const gemui::Rect chip = *win.rectOf("stepWin");
    CHECK(chip.h < sid.h);
    CHECK(chip.x == sid.x);
    CHECK(chip.y == sid.y);
    // In front of the sequencer when it opens.
    const auto& kids = f.w.children();
    const auto at = [&](const gemui::Widget* x) { return std::find(kids.begin(), kids.end(), x) - kids.begin(); };
    CHECK(at(&win) > at(&f.w.sequencer()));
    // Another track focused: the cell was the first track's, the window closes.
    f.w.sequencer().close();  // over the tabs
    const auto tab = f.w.rectOf("tab2");
    REQUIRE(tab);
    press(f.w, centre(*tab));
    f.tick();
    CHECK_FALSE(win.visible());
}
