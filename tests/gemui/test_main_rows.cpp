// STYM2149 — main window rows changed by the ninth DAW test: sliders as wide as their neighbours in VIBRATO and SOURCE
// (items 103, 104, D-304, D-305), Chip Voice named in the register monitor (item 105, D-306), ADSR by its initials
// (item 106, D-307), an Effects button among the window buttons (item 110, D-311). Rows the twelfth aligned: wider
// Depth and Rate sliders (item 158, D-367), the Source, Voicing and Mode buttons and the Noise period slider on the
// Volume and Level sliders (items 159, 160, D-368, D-369).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <string_view>

#include "UiHost.h"

using namespace stym;
using namespace stym::ui_testing;

namespace {

using pluginui::MainWindow;

const gemui::Widget* byId(const gemui::Widget& root, std::string_view id) {
    if (root.id() == id) return &root;
    for (const gemui::Widget* c : root.children())
        if (const gemui::Widget* f = byId(*c, id)) return f;
    return nullptr;
}

gemui::Rect boundsOf(const MainWindow& w, std::string_view id) {
    const gemui::Widget* found = byId(w.mainFrame(), id);
    REQUIRE(found);
    return found->bounds();
}

}  // namespace

TEST_CASE("Main window: the sliders of VIBRATO, SOURCE and ADSR as wide as their neighbours (items 103, 104, 106)",
          "[ui][main-rows]") {
    UiHost h;
    MainWindow w(h);
    // Vibrato: Depth and Rate start and end at the same columns, like the ADSR rows.
    const gemui::Rect depth = boundsOf(w, "vibDepth"), rate = boundsOf(w, "vibRate");
    CHECK(depth.x == rate.x);
    CHECK(depth.w == rate.w);
    // Source: Volume and Level the same way.
    const gemui::Rect vol = boundsOf(w, "vol"), level = boundsOf(w, "chLevel");
    CHECK(vol.x == level.x);
    CHECK(vol.w == level.w);
    // ADSR: one letter before each slider, the four sliders as wide, wider than with the words.
    const char* ids[] = {"atk", "dcy", "sus", "rel"};
    const gemui::Rect atk = boundsOf(w, "atk");
    for (const char* id : ids) {
        CAPTURE(id);
        CHECK(boundsOf(w, id).x == atk.x);
        CHECK(boundsOf(w, id).w == atk.w);
    }
    const gemui::Rect rows = *w.rectOf("adsrRows");
    CHECK(atk.x - rows.x <= 8 + 8);  // the initial and the gap only
    CHECK(atk.w >= 130);  // the initials leave the sliders most of the row
    CHECK(byId(w.mainFrame(), "atk")->tip().rfind("Attack: ", 0) == 0);  // the word in the tip
    // The rows of the visual test: from the label to the value or the unit.
    CHECK(w.rectOf("vibRows")->contains({depth.x, depth.y}));
    CHECK(w.rectOf("vibRows")->contains({rate.right() - 1, rate.bottom() - 1}));
    CHECK(w.rectOf("volRow")->contains({vol.x, vol.y}));
    CHECK(w.rectOf("levelRow")->contains({level.x, level.y}));
}

TEST_CASE("Main window: SOURCE aligned on the Volume and Level sliders, wider VIBRATO sliders (items 158 to 160)",
          "[ui][main-rows]") {
    UiHost h;
    MainWindow w(h);
    const gemui::Rect vol = boundsOf(w, "vol"), level = boundsOf(w, "chLevel");
    // Source, Voicing and Mode start with the sliders of Volume and Level.
    for (const char* id : {"srcSel", "polySel", "vmSel"}) {
        CAPTURE(id);
        CHECK(boundsOf(w, id).x == vol.x);
    }
    // The longest source keeps its button inside the frame.
    h.p(0, plugin::Track::Tone) = 1;
    h.p(0, plugin::Track::Noise) = 1;
    h.p(0, plugin::Track::VolMode) = 1;
    w.tick(0);
    const auto* src = dynamic_cast<const gemui::Button*>(byId(w.mainFrame(), "srcSel"));
    REQUIRE(src);
    CHECK(src->text() == "Tone+Noise+Env");
    CHECK(src->bounds().w >= src->naturalWidth());
    CHECK(src->bounds().right() <= w.rectOf("srcFrame")->right() - 12);
    // The Noise period slider ends with them.
    const gemui::Rect np = boundsOf(w, "np");
    CHECK(np.right() == vol.right());
    CHECK(np.right() == level.right());
    // Depth and Rate: as wide as each other, wider than with the 58 px label column, and still before their values.
    const gemui::Rect depth = boundsOf(w, "vibDepth"), rate = boundsOf(w, "vibRate");
    CHECK(depth.x == rate.x);
    CHECK(depth.w == rate.w);
    CHECK(depth.w >= 100);
    CHECK(depth.x < boundsOf(w, "vibOn").x);
    CHECK(w.rectOf("vibRows")->contains({rate.right() - 1, rate.bottom() - 1}));
    // The rows the visual test does not compare with the prototype.
    for (const char* id : {"srcRow", "polyRow", "vmRow", "npRow"}) {
        CAPTURE(id);
        REQUIRE(w.rectOf(id));
        CHECK(w.rectOf(id)->w <= 206);
    }
}

TEST_CASE("Register monitor: Chip Voice named like the other generators (item 105)", "[ui][main-rows]") {
    UiHost h;
    MainWindow w(h);
    w.tick(0);
    CHECK(w.registerInfo() == "CH 1 / CHIP VOICE / 16CH");
    h.p(0, plugin::Track::Generator) = float(ymengine::Generator::Sid);
    w.tick(0);
    CHECK(w.registerInfo() == "CH 1 / SID VOICE / 16CH");
    const gemui::Rect info = *w.rectOf("regInfo"), regs = *w.rectOf("regs");
    CHECK(info.right() <= regs.right());
    CHECK(info.bottom() <= regs.bottom());
    CHECK(info.x > regs.x + regs.w / 2);
}

TEST_CASE("Window buttons: Effects after Modulation opens the Effects window (item 110)", "[ui][main-rows]") {
    UiHost h;
    MainWindow w(h);
    const gemui::Rect mod = *w.rectOf("openOrnBtn"), fx = *w.rectOf("openFxBtn"), seq = *w.rectOf("openSeqBtn");
    CHECK(fx.y == mod.y);
    CHECK(fx.x > mod.right());
    CHECK(seq.x > fx.right());
    // Still centred over the oscilloscope.
    const gemui::Rect row = *w.rectOf("winButtons"), scope = *w.rectOf("scope");
    CHECK(std::abs((row.x - scope.x) - (scope.right() - row.right())) <= 1);
    CHECK(byId(w.mainFrame(), "openFxBtn")->tip() ==
          "Effects: echo of the notes, and the audio delay in 16 ch (Ctrl+F)");
    REQUIRE_FALSE(w.echoWindow().isOpen());
    click(w, "openFxBtn");
    CHECK(w.echoWindow().isOpen());
}
