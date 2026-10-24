// STYM2149 — Effects window (eighth DAW test, item 98, D-299; renamed by the ninth, items 110 to 112, D-311 to
// D-313): Edit > Effects, ^F or the Effects button; the echo of the focused track's sound in every mode (On / Off,
// Division, To, Repeats, Drop, one undo step each, in the state), greyed out for a Sync Buzzer in 3 channels (D-316);
// the delay of the 16 channels from host parameters (On / Off, Ping-pong, Sync and Division or Time, Feedback, Hi cut,
// Level), greyed out in the 3-channel modes, Ping-pong hidden while it is off (D-315).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include "UiHost.h"
#include "gemui/Controls.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using namespace stym::ui_testing;
using plugin::Global;
using pluginui::EchoWindow;
using pluginui::MainWindow;
using ymengine::TrackEcho;

namespace {

const TrackEcho& echo(const UiHost& h, int track = 0) { return h.state.tracks[size_t(track)].patch.echo; }

// Drag of a slider's thumb past one end of its track.
void dragTo(MainWindow& w, const char* id, bool right) {
    const gemui::Slider* s = w.echoWindow().slider(id);
    REQUIRE(s);
    const gemui::Rect t = s->thumbRect(), r = s->bounds();
    const gemui::Point from = centre(t), mid{r.x + r.w / 2, t.y + 8}, to{right ? r.right() + 20 : r.x - 20, t.y + 8};
    w.desktop().mouseMove(from, {});
    w.desktop().mouseDown(from, gemui::MouseButton::Left, {});
    for (const gemui::Point& p : {mid, to}) w.desktop().mouseDrag(p, {});
    w.desktop().mouseUp(to, {});
}

void type(MainWindow& w, const std::string& id, std::string_view text) {
    click(w, id);
    for (int i = 0; i < 10; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) w.desktop().keyDown(charKey(char32_t(c)));
    key(w, gemui::Key::Enter);
}

std::vector<std::string> divisionNames() {
    const plugin::ParamSpec& s = plugin::paramSpec(plugin::paramIndex(Global::DelayDivision));
    return {s.choices.begin(), s.choices.end()};
}

}  // namespace

TEST_CASE("Effects: opened by Edit > Effects between Modulation and Patches; Echo off, its row only; on, its settings",
          "[ui][echo]") {
    UiHost h;
    MainWindow w(h);
    CHECK_FALSE(w.rectOf("echoWin"));
    w.openMenu(2);
    const std::vector<std::string> edit = menuLabels(w);
    const auto at = std::find(edit.begin(), edit.end(), "Effects");
    REQUIRE(at != edit.end());
    CHECK(at[-1] == "Modulation");
    CHECK(at[1] == "Patches");
    CHECK(std::find(edit.begin(), edit.end(), "Echo / Delay") == edit.end());
    CHECK(topMenu(w)->items()[size_t(at - edit.begin())].key == "^F");
    pickItem(w, int(at - edit.begin()));
    REQUIRE(w.rectOf("echoWin"));
    CHECK(w.echoWindow().titleBar().title() == "EFFECTS - 1");

    const char* const echoIds[] = {"echoDiv", "echoTo", "echoRep", "echoRepv", "echoDrop", "echoDropv"};
    const char* const delayIds[] = {"delaySync", "delayDiv", "delayTime", "delayFb", "delayCut", "delayLevel"};
    const gemui::Rect off = *w.rectOf("echoWin");
    for (const char* id : {"echoOn", "delaySep", "delayOn"}) {
        INFO(id);
        CHECK(w.rectOf(id));
    }
    CHECK_FALSE(w.rectOf("delayPing"));  // the delay off (D-315)
    for (const char* id : echoIds) {
        INFO(id);
        CHECK_FALSE(w.rectOf(id));
    }
    for (const char* id : delayIds) {
        INFO(id);
        CHECK_FALSE(w.rectOf(id));
    }
    CHECK(w.echoWindow().button("echoOn")->text() == "Off");
    CHECK(w.rectOf("delayOn")->y > w.rectOf("delaySep")->bottom());
    CHECK(w.rectOf("delaySep")->y > w.rectOf("echoOn")->bottom());

    // On: one undo step of the track; Division 1/8, the same track, 3 repeats losing 3 steps each.
    click(w, "echoOn");
    CHECK(echo(h).on);
    CHECK(h.editor.undo.undoSize() == 1);
    CHECK(w.echoWindow().button("echoOn")->text() == "On");
    CHECK(w.echoWindow().button("echoDiv")->text() == divisionNames()[size_t(TrackEcho{}.division)]);
    CHECK(w.echoWindow().button("echoTo")->text() == "Same track");
    CHECK(w.echoWindow().slider("echoRep")->value() == 3);
    CHECK(w.echoWindow().slider("echoDrop")->value() == 3);
    const gemui::Rect win = *w.rectOf("echoWin");
    CHECK(win.y == off.y);
    CHECK(win.bottom() > off.bottom());
    for (const char* id : echoIds) {
        INFO(id);
        const auto r = w.rectOf(id);
        REQUIRE(r);
        CHECK(r->y > w.rectOf("echoOn")->bottom());
        CHECK(r->bottom() < w.rectOf("delaySep")->y);
        CHECK(r->right() < win.right());
    }
    // The sliders of Repeats and Drop line up.
    CHECK(w.rectOf("echoRep")->x == w.rectOf("echoDrop")->x);
    CHECK(w.rectOf("echoRep")->w == w.rectOf("echoDrop")->w);
    CHECK(w.rectOf("echoTo")->y == w.rectOf("echoDiv")->y);
    ctrlKey(w, U'z');
    CHECK_FALSE(echo(h).on);
    CHECK_FALSE(w.rectOf("echoRep"));
    CHECK(w.rectOf("echoWin")->bottom() == off.bottom());
}

TEST_CASE("Echo: Division and To menus, Repeats and Drop dragged and typed, in the state (D-299)", "[ui][echo]") {
    UiHost h;
    h.state.tracks[0].patch.echo.on = true;
    MainWindow w(h);
    w.echoWindow().show();

    click(w, "echoDiv");
    CHECK(menuLabels(w) == divisionNames());
    pickItem(w, 2);
    CHECK(echo(h).division == 2);
    CHECK(w.echoWindow().button("echoDiv")->text() == divisionNames()[2]);

    // To: the same track, or one of the sixteen tracks.
    click(w, "echoTo");
    const std::vector<std::string> to = menuLabels(w);
    REQUIRE(to.size() == 1 + 16);
    CHECK(to[0] == "Same track");
    pickItem(w, 3);
    CHECK(echo(h).target == 2);
    CHECK(w.echoWindow().button("echoTo")->text() == to[3]);
    click(w, "echoTo");
    pickItem(w, 0);
    CHECK(echo(h).target == -1);
    CHECK(h.editor.undo.undoSize() == 3);

    dragTo(w, "echoRep", true);
    CHECK(echo(h).repeats == ymengine::kEchoRepeatsMax);
    CHECK(h.editor.undo.undoSize() == 4);
    dragTo(w, "echoRep", false);
    CHECK(echo(h).repeats == 1);
    type(w, "echoRepv", "5");
    CHECK(echo(h).repeats == 5);
    type(w, "echoRepv", "0");  // clamped
    CHECK(echo(h).repeats == 1);
    dragTo(w, "echoDrop", false);
    CHECK(echo(h).drop == 0);
    type(w, "echoDropv", "99");
    CHECK(echo(h).drop == ymengine::kEchoDropMax);
    ctrlKey(w, U'z');
    CHECK(echo(h).drop == 0);

    // The focused track's: another track has its own (the tab under the window: closed, then opened again).
    w.echoWindow().close();
    click(w, "tab1");
    w.echoWindow().show();
    CHECK_FALSE(w.rectOf("echoRep"));
    click(w, "echoOn");
    CHECK(echo(h, 1).on);
    CHECK(echo(h, 0).repeats == 1);
    CHECK(echo(h, 1).repeats == 3);
}

TEST_CASE("Echo: in 3 channels too, To lists its tracks; a percussion track has its echo (D-299)",
          "[ui][echo][3ch]") {
    UiHost h;
    h.p(Global::VoiceMode) = 1;
    MainWindow w(h);
    w.echoWindow().show();
    REQUIRE(w.echoWindow().button("echoOn")->enabled());
    click(w, "echoOn");
    CHECK(echo(h).on);
    click(w, "echoTo");
    const std::vector<std::string> to = menuLabels(w);
    CHECK(to.size() < 1 + 16);
    CHECK(to[0] == "Same track");
    w.desktop().closePopups();

    w.pickGenerator(int(ymengine::Generator::Digidrum));
    type(w, "echoDropv", "7");
    CHECK(echo(h).drop == 7);
}

TEST_CASE("Delay: 16 channels, host parameters; Sync and Division, or Time in ms (D-299)", "[ui][echo][delay]") {
    CHECK(EchoWindow::timeOfPosition(0) == int(ymengine::kDelayMinMs));
    CHECK(EchoWindow::timeOfPosition(EchoWindow::kLogPositions) == int(ymengine::kDelayMaxMs));
    CHECK(EchoWindow::cutOfPosition(0) == int(ymengine::kDelayHiCutMin));
    CHECK(EchoWindow::cutOfPosition(EchoWindow::kLogPositions) == int(ymengine::kDelayHiCutMax));
    for (double ms : {10.0, 50.0, 375.0, 1000.0, 2000.0}) {
        INFO(ms);
        CHECK(std::abs(EchoWindow::timeOfPosition(EchoWindow::positionOfTime(ms)) - ms) <= ms * 0.02 + 0.5);
    }

    UiHost h;
    MainWindow w(h);
    w.echoWindow().show();
    REQUIRE(w.echoWindow().button("delayOn")->enabled());
    CHECK(w.echoWindow().button("delayOn")->tip() ==
          "Delay: echoes of the sound of every track, as much as its Send in the Mixer");
    click(w, "delayOn");
    CHECK(h.p(Global::DelayOn) == 1.0f);
    // Sync by default: Division, Feedback, Hi cut and Level; no Time.
    for (const char* id : {"delaySync", "delayDiv", "delayFb", "delayFbv", "delayCut", "delayCutv", "delayLevel",
                           "delayLevelv"}) {
        INFO(id);
        CHECK(w.rectOf(id));
    }
    CHECK_FALSE(w.rectOf("delayTime"));
    CHECK(w.echoWindow().button("delaySync")->on());
    CHECK(w.rectOf("delayFb")->x == w.rectOf("delayCut")->x);
    CHECK(w.rectOf("delayCut")->x == w.rectOf("delayLevel")->x);

    click(w, "delayDiv");
    CHECK(menuLabels(w) == divisionNames());
    pickItem(w, 6);
    CHECK(h.p(Global::DelayDivision) == 6.0f);

    // Sync off: Time in ms in place of the Division, on a logarithmic slider.
    click(w, "delaySync");
    CHECK(h.p(Global::DelaySync) == 0.0f);
    CHECK_FALSE(w.rectOf("delayDiv"));
    REQUIRE(w.rectOf("delayTime"));
    CHECK(std::abs(centre(*w.rectOf("delayTime")).y - centre(*w.rectOf("delaySync")).y) <= 1);
    dragTo(w, "delayTime", true);
    CHECK(h.p(Global::DelayTime) == float(ymengine::kDelayMaxMs));
    dragTo(w, "delayTime", false);
    CHECK(h.p(Global::DelayTime) == float(ymengine::kDelayMinMs));
    type(w, "delayTimev", "375");
    CHECK(h.p(Global::DelayTime) == 375.0f);
    type(w, "delayTimev", "5000");
    CHECK(h.p(Global::DelayTime) == float(ymengine::kDelayMaxMs));

    type(w, "delayFbv", "50");
    CHECK(h.p(Global::DelayFeedback) == 50.0f);
    type(w, "delayFbv", "99");
    CHECK(h.p(Global::DelayFeedback) == float(ymengine::kDelayFeedbackMax * 100));
    dragTo(w, "delayCut", false);
    CHECK(h.p(Global::DelayHiCut) == float(ymengine::kDelayHiCutMin));
    type(w, "delayCutv", "8000");
    CHECK(h.p(Global::DelayHiCut) == 8000.0f);
    type(w, "delayLevelv", "80");
    CHECK(h.p(Global::DelayLevel) == 80.0f);
    click(w, "delayPing");
    CHECK(h.p(Global::DelayPingPong) == 1.0f);

    // Off: its On / Off only, Ping-pong hidden too (D-315), the window as wide.
    const int wide = w.rectOf("echoWin")->w;
    click(w, "delayOn");
    CHECK(h.p(Global::DelayOn) == 0.0f);
    CHECK_FALSE(w.rectOf("delaySync"));
    CHECK_FALSE(w.rectOf("delayLevel"));
    CHECK_FALSE(w.rectOf("delayPing"));
    CHECK(w.rectOf("echoWin")->w == wide);
}

TEST_CASE("Delay: greyed out in the 3-channel modes, nothing else of it shows (D-299)", "[ui][echo][delay][3ch]") {
    for (float mode : {1.0f, 2.0f}) {
        INFO(mode);
        UiHost h;
        h.p(Global::VoiceMode) = mode;
        h.p(Global::DelayOn) = 1;
        MainWindow w(h);
        w.echoWindow().show();
        const gemui::Button* on = w.echoWindow().button("delayOn");
        CHECK_FALSE(on->enabled());
        CHECK(on->tip() == "Delay: 16 ch only. In 3 ch, the Echo repeats the notes on the chip");
        CHECK_FALSE(w.rectOf("delayPing"));  // hidden (D-315)
        CHECK_FALSE(w.rectOf("delaySync"));
        CHECK_FALSE(w.rectOf("delayFb"));
        // Back to 16 channels: its settings.
        h.p(Global::VoiceMode) = 0;
        w.tick(0);
        CHECK(on->enabled());
        CHECK(w.rectOf("delayFb"));
        CHECK(w.rectOf("delayPing"));
    }
}

TEST_CASE("Effects: Ctrl+F opens it, in front of the others (ninth DAW test, item 112)", "[ui][echo]") {
    UiHost h;
    MainWindow w(h);
    w.mixer().show();
    REQUIRE_FALSE(w.echoWindow().isOpen());
    CHECK(ctrlKey(w, U'f'));
    CHECK(w.echoWindow().isOpen());
    CHECK(gemui::FloatWindow::topmost(w) == &w.echoWindow());
}

TEST_CASE("Effects: no echo for a Sync Buzzer in 3 channels, greyed out; nor a Send in the Mixer (items 115, 116)",
          "[ui][echo][3ch]") {
    for (float mode : {1.0f, 2.0f}) {
        INFO(mode);
        UiHost h;
        h.p(Global::VoiceMode) = mode;
        h.p(0, plugin::Track::Generator) = float(ymengine::Generator::SyncBuzzer);
        h.state.tracks[0].patch.echo.on = true;
        MainWindow w(h);
        w.echoWindow().show();
        const gemui::Button* on = w.echoWindow().button("echoOn");
        CHECK_FALSE(on->enabled());
        CHECK(on->tip() ==
              "Echo: none for Sync Buzzer in 3 ch, a repeat would restart the shared hardware envelope at full level");
        CHECK(on->text() == "On");  // the setting kept, for another sound or 16 channels
        CHECK_FALSE(w.echoWindow().slider("echoRep")->enabled());
        CHECK_FALSE(w.echoWindow().button("echoDiv")->enabled());
        click(w, "echoOn");
        CHECK(echo(h).on);
        // The Mixer has no Send in the 3-channel modes: the delay is a 16-channel effect.
        w.mixer().show();
        CHECK_FALSE(w.rectOf("mixSend0"));
        // Chip Voice: its echo.
        h.p(0, plugin::Track::Generator) = 0;
        w.tick(0);
        CHECK(on->enabled());
        CHECK(w.echoWindow().slider("echoRep")->enabled());
    }
    // 16 channels: a Sync Buzzer has its echo, and its Send.
    UiHost h;
    h.p(0, plugin::Track::Generator) = float(ymengine::Generator::SyncBuzzer);
    MainWindow w(h);
    w.echoWindow().show();
    CHECK(w.echoWindow().button("echoOn")->enabled());
    w.mixer().show();
    CHECK(w.rectOf("mixSend0"));
}
