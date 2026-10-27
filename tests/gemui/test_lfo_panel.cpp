// STYM2149 — LFO of the focused track in the Modulation window (sixth DAW test, item 59): below the settings
// of the sequences, On / Off, shape and target menus, Sync and division, Retrig, rate and depth sliders and
// typed values, one undo step per setting; nothing on a percussion track; Pan greyed in the 3-channel modes; off, only
// "LFO" and its button.
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
using pluginui::LfoPanel;
using pluginui::MainWindow;
using ymengine::LfoShape;
using ymengine::LfoTarget;
using ymengine::TrackLfo;

namespace {

const TrackLfo& lfo(const UiHost& h, int track = 0) { return h.state.tracks[size_t(track)].patch.lfo; }

// Drag of a slider's thumb past one end of its track.
void dragTo(MainWindow& w, const char* id, bool right) {
    const gemui::Slider* s = w.modulation().lfo().slider(id);
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

}  // namespace

TEST_CASE("LFO: below the settings of the sequences, off by default, its settings shown once on",
          "[ui][lfo]") {
    UiHost h;
    MainWindow w(h);
    CHECK_FALSE(w.rectOf("lfoOn"));
    w.modulation().show();
    const char* const ids[] = {"lfoOn",    "lfoShape", "lfoTo",    "lfoSync",  "lfoRetrig",
                               "lfoRate",  "lfoDepth", "lfoRateV", "lfoDepthV"};
    // Off: "LFO" and its button only (eighth DAW test, item 97); the window ends below them.
    const gemui::Rect off = *w.rectOf("ornWin");
    REQUIRE(w.rectOf("lfoOn"));
    CHECK(w.rectOf("lfoOn")->y > w.rectOf("seqxVibWrite")->bottom());
    CHECK(off.bottom() - w.rectOf("lfoOn")->bottom() < 24);
    for (const char* id : ids) {
        INFO(id);
        if (std::string_view(id) != "lfoOn") CHECK_FALSE(w.rectOf(id));
    }
    const gemui::Slider* rate = w.modulation().lfo().slider("lfoRate");
    CHECK_FALSE(rate->enabled());
    CHECK_FALSE(w.modulation().lfo().slider("lfoDepth")->enabled());
    click(w, "lfoOn");
    CHECK(lfo(h).on);
    CHECK(rate->enabled());
    CHECK(h.editor.undo.undoSize() == 1);

    // On: every setting, the window grown to hold them.
    const gemui::Rect win = *w.rectOf("ornWin");
    CHECK(win.y == off.y);
    CHECK(win.bottom() > off.bottom());
    CHECK(win.bottom() <= MainWindow::kHeight);
    for (const char* id : ids) {
        INFO(id);
        const auto r = w.rectOf(id);
        REQUIRE(r);
        CHECK(r->y > w.rectOf("seqxVibWrite")->bottom());
        CHECK(r->bottom() < win.bottom());
        CHECK(r->right() < win.right());
    }
    // Columns of the settings above: On / Off under the fixed note slider, the rate under the vibrato table's
    // sliders; Sync under On / Off, the target under the shape, the depth under the rate.
    CHECK(w.rectOf("lfoOn")->x == w.rectOf("seqxFix")->x);
    CHECK(w.rectOf("lfoRate")->x == w.rectOf("seqxVibMin")->x);
    CHECK(w.rectOf("lfoRate")->w == w.rectOf("seqxVibMin")->w);
    CHECK(w.rectOf("lfoSync")->x == w.rectOf("lfoOn")->x);
    CHECK(w.rectOf("lfoRetrig")->y == w.rectOf("lfoSync")->y);
    CHECK(w.rectOf("lfoRetrig")->x > w.rectOf("lfoSync")->right());
    CHECK(w.rectOf("lfoRetrig")->right() < w.rectOf("lfoTo")->x);
    CHECK(w.rectOf("lfoTo")->y > w.rectOf("lfoShape")->bottom());
    CHECK(w.rectOf("lfoDepth")->x == w.rectOf("lfoRate")->x);
    CHECK(w.rectOf("lfoDepth")->y > w.rectOf("lfoRate")->bottom());

    // Retrig: lit by default, restarted at each note; off, the LFO runs free.
    const gemui::Button* retrig = w.modulation().lfo().button("lfoRetrig");
    REQUIRE(retrig);
    CHECK(retrig->enabled());
    CHECK(retrig->on());
    click(w, "lfoRetrig");
    CHECK_FALSE(lfo(h).retrig);
    CHECK_FALSE(retrig->on());
    CHECK(h.editor.undo.undoSize() == 2);
    ctrlKey(w, U'z');
    CHECK(lfo(h).retrig);
    ctrlKey(w, U'z');
    CHECK_FALSE(lfo(h).on);
    CHECK_FALSE(retrig->enabled());
    CHECK_FALSE(w.rectOf("lfoRetrig"));
    CHECK(w.rectOf("ornWin")->bottom() == off.bottom());
}

TEST_CASE("LFO: shape and target menus, the depth kept in proportion", "[ui][lfo]") {
    UiHost h;
    h.state.tracks[0].patch.lfo.on = true;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Noise))] = 1;  // a noise period to move
    MainWindow w(h);
    w.modulation().show();

    click(w, "lfoShape");
    CHECK(menuLabels(w) == std::vector<std::string>{"Triangle", "Sine", "Square", "Saw up", "Saw down", "Random"});
    pickItem(w, 2);
    CHECK(lfo(h).shape == LfoShape::Square);

    click(w, "lfoTo");
    CHECK(menuLabels(w) ==
          std::vector<std::string>{"Volume", "Noise period", "Env period", "SID ratio", "Pitch", "Pan"});
    CHECK_FALSE(topMenu(w)->items()[5].disabled);  // 16 channels
    pickItem(w, 1);
    CHECK(lfo(h).target == LfoTarget::NoisePeriod);
    CHECK(lfo(h).depth == 8);  // 4 of 15 → 8 of 31
    CHECK(w.modulation().lfo().slider("lfoDepth")->max() == 31);
    click(w, "lfoTo");
    pickItem(w, 4);
    CHECK(lfo(h).target == LfoTarget::Pitch);
    CHECK(lfo(h).depth == 619);  // 8 of 31 → 619 cents of 2400
    CHECK(h.editor.undo.undoSize() == 3);

    // Depth: dragged to its maximum, typed (brought back into the range of the target).
    dragTo(w, "lfoDepth", true);
    CHECK(lfo(h).depth == 2400);
    type(w, "lfoDepthV", "250");
    CHECK(lfo(h).depth == 250);
    type(w, "lfoDepthV", "9999");
    CHECK(lfo(h).depth == 2400);
}

TEST_CASE("LFO: rate in Hz or a division of the tempo, dragged and typed", "[ui][lfo]") {
    CHECK(LfoPanel::rateOfPosition(0) == ymengine::kLfoMinHz);
    CHECK(LfoPanel::rateOfPosition(LfoPanel::kRatePositions) == ymengine::kLfoMaxHz);
    for (double hz : {0.1, 0.5, 1.0, 4.0, 7.5, 12.5, 20.0}) {
        INFO(hz);
        CHECK(std::abs(LfoPanel::rateOfPosition(LfoPanel::positionOfRate(hz)) - hz) <= hz * 0.03);
    }
    CHECK(LfoPanel::rateText(0.25) == "0.25 Hz");
    CHECK(LfoPanel::rateText(4) == "4.0 Hz");
    CHECK(LfoPanel::divisionOf(" 1/16t") == 8);
    CHECK(LfoPanel::divisionOf("1/32T") == 10);
    CHECK(LfoPanel::divisionOf("1/64") == 11);
    CHECK(LfoPanel::divisionOf("1/5") == -1);

    UiHost h;
    h.state.tracks[0].patch.lfo.on = true;
    MainWindow w(h);
    w.modulation().show();
    dragTo(w, "lfoRate", true);
    CHECK(lfo(h).hz == ymengine::kLfoMaxHz);
    dragTo(w, "lfoRate", false);
    CHECK(lfo(h).hz == ymengine::kLfoMinHz);
    type(w, "lfoRateV", "7.5");
    CHECK(lfo(h).hz == 7.5);
    type(w, "lfoRateV", "0.123");
    CHECK(lfo(h).hz == 0.12);
    type(w, "lfoRateV", "50 Hz");
    CHECK(lfo(h).hz == ymengine::kLfoMaxHz);
    const size_t undo = h.editor.undo.undoSize();
    type(w, "lfoRateV", "fast");  // rejected
    CHECK(h.editor.undo.undoSize() == undo);

    // Sync: a menu button in place of the slider and its value (ninth DAW test, item 119), on the same row, at
    // the place of the slider; 1/4 by default, from 4/1 to 1/64.
    const gemui::Rect slider = *w.rectOf("lfoRate");
    click(w, "lfoSync");
    CHECK(lfo(h).sync);
    CHECK_FALSE(w.rectOf("lfoRate"));
    CHECK_FALSE(w.rectOf("lfoRateV"));
    REQUIRE(w.rectOf("lfoDiv"));
    CHECK(w.rectOf("lfoDiv")->x == slider.x);
    CHECK(w.rectOf("lfoDiv")->y < slider.bottom());
    CHECK(w.rectOf("lfoDiv")->bottom() > slider.y);
    const gemui::Button* div = w.modulation().lfo().button("lfoDiv");
    REQUIRE(div);
    CHECK(div->text() == "1/4");
    CHECK(div->tip() == "LFO cycle: a division of the tempo");
    click(w, "lfoDiv");
    CHECK(menuLabels(w) == std::vector<std::string>{"4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16",
                                                    "1/16T", "1/32", "1/32T", "1/64"});
    pickItem(w, 11);
    CHECK(lfo(h).division == 11);
    CHECK(div->text() == "1/64");
    CHECK(lfo(h).hz == ymengine::kLfoMaxHz);  // the rate in Hz is kept
    click(w, "lfoDiv");
    pickItem(w, 10);
    CHECK(div->text() == "1/32T");
    // Off: the slider and its value back; a typed division turns Sync on, a number turns it off.
    click(w, "lfoSync");
    CHECK_FALSE(lfo(h).sync);
    CHECK_FALSE(w.rectOf("lfoDiv"));
    CHECK(*w.rectOf("lfoRate") == slider);
    type(w, "lfoRateV", "2/1");
    CHECK(lfo(h).sync);
    CHECK(lfo(h).division == 1);
    CHECK(div->text() == "2/1");
    click(w, "lfoSync");
    type(w, "lfoRateV", "3");
    CHECK_FALSE(lfo(h).sync);
    CHECK(lfo(h).hz == 3);
}

TEST_CASE("LFO: nothing on a percussion track, Pan greyed in the 3-channel modes", "[ui][lfo]") {
    UiHost h;
    h.state.tracks[0].patch.lfo.on = true;
    MainWindow w(h);
    w.modulation().show();
    w.pickGenerator(int(ymengine::Generator::Digidrum));
    const size_t undo = h.editor.undo.undoSize();
    click(w, "lfoOn");
    CHECK(lfo(h).on);
    // Only "LFO" and its greyed button.
    CHECK_FALSE(w.rectOf("lfoSync"));
    CHECK_FALSE(w.modulation().lfo().slider("lfoDepth")->enabled());
    CHECK(h.editor.undo.undoSize() == undo);

    UiHost h3;
    h3.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    h3.state.tracks[0].patch.lfo.on = true;
    MainWindow w3(h3);
    w3.modulation().show();
    click(w3, "lfoTo");
    const gemui::MenuItem& pan = topMenu(w3)->items()[5];
    CHECK(pan.disabled);
    CHECK(pan.why == "Pan: 16 channels only; the 3-channel modes are mono");
    pickItem(w3, 5);
    CHECK(lfo(h3).target == LfoTarget::Volume);
}

TEST_CASE("LFO: the targets that move nothing on the track greyed, with the reason (ninth DAW test, item 120)",
          "[ui][lfo]") {
    using ymengine::Generator;
    UiHost h;
    h.state.tracks[0].patch.lfo.on = true;
    MainWindow w(h);
    w.modulation().show();
    auto whys = [&w] {  // reason of each entry of the To menu, empty when it can be picked
        click(w, "lfoTo");
        std::vector<std::string> out;
        for (const gemui::MenuItem& m : topMenu(w)->items()) {
            CHECK(m.disabled == !m.why.empty());
            out.push_back(m.why);
        }
        w.desktop().closePopups();
        return out;
    };
    const std::string noNoise = "Noise period: no noise in this track's source";
    const std::string fixedVol = "Env period: the source uses the fixed volume, not the envelope";
    const std::string sidOnly = "SID ratio: SID Voice only";
    // Chip Voice, the square alone at the fixed volume (by default): no noise, no envelope, no SID timer.
    CHECK(whys() == std::vector<std::string>{"", noNoise, fixedVol, sidOnly, "", ""});
    // The noise and the envelope in the source: their periods, the 4-bit volume no longer.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Noise))] = 1;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VolMode))] = 1;
    w.tick(0);
    CHECK(whys() == std::vector<std::string>{
                        "Volume: the source plays the hardware envelope, the 4-bit volume is ignored", "", "", sidOnly,
                        "", ""});
    // SID Voice: its ratio; it writes its own volumes.
    w.pickGenerator(int(Generator::Sid));
    CHECK(whys() == std::vector<std::string>{
                        "", "", "Env period: a SID Voice writes its own volumes, the envelope is not used", "", "",
                        ""});
    // Sync Buzzer: the envelope alone, in every mode.
    w.pickGenerator(int(Generator::SyncBuzzer));
    const std::string buzzVol =
        "Volume: a Sync Buzzer takes its level from the hardware envelope, the 4-bit volume is ignored";
    CHECK(whys() == std::vector<std::string>{
                        buzzVol, "Noise period: a Sync Buzzer plays the hardware envelope alone, without noise", "",
                        sidOnly, "", ""});

    // The target chosen before (Volume): To left to pick another one, with the reason; the other settings greyed.
    const LfoPanel& panel = w.modulation().lfo();
    CHECK(panel.button("lfoTo")->enabled());
    CHECK(panel.button("lfoTo")->tip() == buzzVol);
    CHECK_FALSE(panel.button("lfoSync")->enabled());
    CHECK_FALSE(panel.button("lfoRetrig")->enabled());
    CHECK_FALSE(panel.slider("lfoRate")->enabled());
    CHECK_FALSE(panel.slider("lfoDepth")->enabled());
    CHECK(panel.button("lfoOn")->enabled());
    click(w, "lfoTo");
    pickItem(w, 0);  // greyed: nothing
    CHECK(lfo(h).target == LfoTarget::Volume);
    click(w, "lfoTo");
    pickItem(w, 2);
    CHECK(lfo(h).target == LfoTarget::EnvPeriod);
    CHECK(panel.slider("lfoDepth")->enabled());
    CHECK(panel.button("lfoSync")->enabled());
    CHECK(panel.button("lfoTo")->tip() ==
          "What the LFO moves: volume, noise period, envelope period, SID ratio, pitch, or pan (16 channels)");

    // 3 channels: Pan as well.
    h.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    w.tick(0);
    CHECK(whys().back() == "Pan: 16 channels only; the 3-channel modes are mono");
    CHECK(whys().front() == buzzVol);
}

TEST_CASE("LFO: the Triangle drawing comes to a point, the Sine one is round (ninth DAW test, item 118)",
          "[ui][lfo]") {
    // Each over two cycles, two crests on its top row. The apex of the triangle is no wider than its 2 px stroke and
    // one more pixel, its sides are straight steep runs; the crest of the sine is wider, its sides fall straight down
    // over most of the height (the rounded wave).
    auto top = [](const gemui::Bitmap& b) {
        for (int y = 0; y < b.h; ++y)
            for (int x = 0; x < b.w; ++x)
                if (b.at(x, y)) return y;
        return -1;
    };
    auto runs = [](const gemui::Bitmap& b, int y) {  // widths of the runs of a row
        std::vector<int> out;
        int n = 0;
        for (int x = 0; x <= b.w; ++x) {
            if (x < b.w && b.at(x, y)) ++n;
            else if (n > 0) {
                out.push_back(n);
                n = 0;
            }
        }
        return out;
    };
    auto tallest = [](const gemui::Bitmap& b) {  // longest vertical run of a column
        int best = 0;
        for (int x = 0; x < b.w; ++x)
            for (int y = 0, n = 0; y < b.h; ++y) {
                n = b.at(x, y) ? n + 1 : 0;
                best = std::max(best, n);
            }
        return best;
    };
    const gemui::Bitmap& tri = LfoPanel::shapeIcon(LfoShape::Triangle);
    const gemui::Bitmap& sine = LfoPanel::shapeIcon(LfoShape::Sine);
    const int tt = top(tri), ts = top(sine);
    REQUIRE(tt >= 0);
    REQUIRE(ts >= 0);
    CHECK(tt <= 1);
    CHECK(ts <= 1);
    const std::vector<int> apexes = runs(tri, tt), crests = runs(sine, ts);
    REQUIRE(apexes.size() == 2);
    REQUIRE(crests.size() == 2);
    for (int w : apexes) CHECK(w <= 3);
    for (int w : crests) CHECK(w >= 4);
    CHECK(tallest(tri) <= 5);
    CHECK(tallest(sine) >= 8);
}
