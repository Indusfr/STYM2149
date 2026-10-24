// STYM2149 — Recorder window (M6, B11, D-157): Edit menu entry, chip chosen (2 in dual chip only), Rec that arms and
// stops, states shown (16 voices, armed, in progress), take kept and its deletion after confirmation, session.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using pluginui::RecorderWindow;
using plugin::Global;
using plugin::paramIndex;
using RecState = pluginui::Host::Recorder::State;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    pluginui::EditorSession editor;
    Recorder rec;
    std::vector<int> arms;
    int disarms = 0, clears = 0;

    float param(int i) const override { return values[size_t(i)]; }
    void beginGesture(int) override {}
    void setParam(int i, float v) override { values[size_t(i)] = plugin::clampToSpec(plugin::paramSpec(i), v); }
    void endGesture(int) override {}
    uint64_t extrasVersion() const override { return 1; }
    plugin::NonParamState extras() const override { return state; }
    void editExtras(const std::function<void(plugin::NonParamState&)>& edit) override { edit(state); }
    void screenMidi(uint8_t, uint8_t, uint8_t) override {}
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void seqCommand(SeqCommand, int, int, int) override {}
    const plugin::EngineStatus* engineStatus() override { return nullptr; }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return false; }
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
    Recorder recorder() const override { return rec; }
    void armRecorder(int chip) override {
        arms.push_back(chip);
        rec.state = RecState::Armed;
        rec.chip = chip;
    }
    void disarmRecorder() override {
        ++disarms;
        rec.state = RecState::Idle;
    }
    void clearRecording() override {
        ++clears;
        rec.takeFrames = 0;
    }
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

// Button `index` of the open alert.
void alertButton(MainWindow& w, size_t index) {
    gemui::Widget* m = w.desktop().topModal();
    REQUIRE(m);
    REQUIRE(m->children().size() > index);
    press(w, centre(m->children()[index]->bounds()));
}

}  // namespace

TEST_CASE("Recorder: File menu, chip, Rec that arms and stops, states shown", "[gemui][recorder]") {
    TestHost h;
    MainWindow w(h);
    w.tick(0);

    // Entry of the File menu, below the exports.
    w.openMenu(1);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    int found = -1;
    for (size_t i = 0; i < list->items().size(); ++i)
        if (list->items()[i].label == "Recorder") found = int(i);
    REQUIRE(found >= 0);
    list->items()[size_t(found)].pick();
    w.desktop().closePopups();
    RecorderWindow& r = w.recorderWindow();
    REQUIRE(r.isOpen());

    // 16 voices: no registers, Rec greyed out.
    CHECK(r.status() == "16 ch has no YM registers: switch to 3ch or 2x3ch.");
    CHECK(r.takeText() == "none");
    click(w, "recRec");
    CHECK(h.arms.empty());

    // 3 ch: one chip, the second greyed out.
    h.values[size_t(paramIndex(Global::VoiceMode))] = 1;
    w.tick(10);
    CHECK(r.status() == "Ready.");
    click(w, "recChip1");
    CHECK(r.chip() == 0);
    click(w, "recRec");
    REQUIRE(h.arms == std::vector<int>{0});
    CHECK(r.status() == "Armed: the take starts when the host plays.");
    // Armed: a click stops it.
    click(w, "recRec");
    CHECK(h.disarms == 1);
    CHECK(r.status() == "Ready.");

    // Dual chip: the second can be chosen.
    h.values[size_t(paramIndex(Global::VoiceMode))] = 2;
    w.tick(20);
    click(w, "recChip1");
    CHECK(r.chip() == 1);
    click(w, "recRec");
    CHECK(h.arms.back() == 1);

    // In progress: duration and frames at the replay tick, read again at each tick.
    h.rec.state = RecState::Rolling;
    h.rec.frames = 125;
    w.tick(30);
    CHECK(r.status() == "Recording chip 2: 0:02.50, 125 frames.");
    h.rec.frames = 7525;
    w.tick(40);
    CHECK(r.status() == "Recording chip 2: 2:30.50, 7525 frames.");
    click(w, "recChip0");  // chip frozen during the take
    CHECK(r.chip() == 1);

    // End: the kept take is shown.
    h.rec = {};
    h.rec.takeFrames = 7525;
    h.rec.takeHz = 50;
    h.rec.takeChip = 1;
    w.tick(50);
    CHECK(r.status() == "Ready.");
    CHECK(r.takeText() == "7525 frames, 2:30.50 at 50 Hz, chip 2");
}

TEST_CASE("Recorder: Clear asks for confirmation; duration; window kept by the session", "[gemui][recorder]") {
    CHECK(RecorderWindow::duration(0, 50) == "0:00.00");
    CHECK(RecorderWindow::duration(3001, 200) == "0:15.01");  // 15.005 s rounded to the hundredth
    CHECK(RecorderWindow::duration(90000, 50) == "30:00.00");

    TestHost h;
    h.values[size_t(paramIndex(Global::VoiceMode))] = 1;
    h.rec.takeFrames = 100;
    {
        MainWindow w(h);
        w.tick(0);
        w.recorderWindow().show();
        click(w, "recClear");
        REQUIRE(w.desktop().hasModal());
        alertButton(w, 0);  // Cancel
        CHECK_FALSE(w.desktop().hasModal());
        CHECK(h.clears == 0);
        click(w, "recClear");
        alertButton(w, 1);  // Delete
        CHECK(h.clears == 1);
        CHECK(w.recorderWindow().takeText() == "none");
    }
    MainWindow again(h);
    again.tick(0);
    CHECK(again.recorderWindow().isOpen());
}
