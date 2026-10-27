// STYM2149 — visual regression (STYM2149_SPEC.md §11): each scene, rendered by gemui, differs from the
// prototype's screenshot at 100 % (tests/data/visual) on less than 1 % of the pixels of the original desktop (2 % for a
// floating window), outside the areas of an intended deviation that the scene declares. The band of
// the menu bar, moved to the top of the desktop, and the desktop icons are not compared there: the
// STYM2149 scene desk checks them. Nor are the rows of the main window that the ninth DAW test changed
// and the twelfth aligned, which the STYM2149 scenes check to the pixel.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "Png.h"
#include "Scenes.h"
#include "gemui/Controls.h"
#include "pluginui/MainWindow.h"

using namespace stym;

namespace {
// Tolerated deviation per channel (out of 255): absorbs the blending roundings of the opacity groups.
constexpr int kTolerance = 4;
// Reference area of the budgets: the original desktop (1016 × 944). The desktop widened to 1272 px, then to
// 1344 × 984, only adds background, always identical: measuring the deviations against the whole image would
// loosen the test by more than a third.
constexpr long kBudgetArea = 1016L * 944L;
}  // namespace

TEST_CASE("Windows match the prototype's screenshots (deviation < 1 %, 2 % for a floating window)", "[visual]") {
    // orn, orn_on, menu_source, arp_midi, sync, mixer, mixer_3ch and mixer_scope,
    // menu_file, menu_edit, menu_options and menu_fuji, seq and seq_demo in the STYM2149
    // scenes.
    REQUIRE(gemshot::sceneNames().size() == 31);
    for (const std::string& name : gemshot::sceneNames()) {
        CAPTURE(name);
        tools::Image ref;
        std::string error;
        REQUIRE(tools::readPng(std::string(STYM_TEST_DATA) + "/visual/" + name + ".png", ref, &error));
        // Drawn with the glyphs of the prototype's TrueType, from the origin (gemui keeps EmuTOS's place).
        const gemshot::Shot shot = gemshot::renderScene(name);
        const tools::Image& img = shot.image;
        REQUIRE(img.width == ref.width);
        REQUIRE(img.height == ref.height);
        // Ignored areas: visible controls, small, in front of the window. Apart: the band of the menu bar, across the
        // top of the desktop down to the main window's title bar, the visible parts of the three desktop
        // icons, and those of the rows of the main window changed by the ninth DAW test: a slider row or the
        // four of ADSR, the info cell of the register monitor, the row of the window
        // buttons across the oscilloscope's width, the rows of SOURCE aligned by the twelfth.
        long maskedArea = 0;
        int icons = 0, rows = 0;
        for (const gemshot::Mask& m : shot.masks) {
            CHECK(!m.decision.empty());
            if (m.decision == "menu-bar-top") {
                CHECK(m.x == 0);
                CHECK(m.y == 0);
                CHECK(m.w == img.width);
                CHECK(m.h <= pluginui::MainWindow::kWinY + 6 + gemui::TitleBar::kHeight);
                continue;
            }
            if (m.decision == "desk-icons") {
                ++icons;
                CHECK(m.w <= gemui::DeskIcon::kW);
                CHECK(m.h <= gemui::DeskIcon::kH);
                continue;
            }
            if (m.decision == "vibrato-widths" || m.decision == "source-widths" || m.decision == "register-monitor" ||
                m.decision == "adsr-sliders" || m.decision == "effects-button" || m.decision == "source-sliders-first" ||
                m.decision == "noise-slider-last") {
                ++rows;
                CHECK(m.w <= (m.decision == "effects-button" ? 944 : m.decision == "register-monitor" ? 404 : 206));
                const bool buttons = m.decision == "effects-button" || m.decision == "source-sliders-first";
                const int high = m.decision == "adsr-sliders"     ? 4 * 16 + 3 * 6  // four slider rows
                                 : m.decision == "vibrato-widths" ? 2 * 16 + 6      // two
                                 : buttons                        ? 36              // a button row
                                                                  : 20;             // a slider row, a cell
                CHECK(m.h <= high);
                continue;
            }
            maskedArea += long(m.w) * long(m.h);
        }
        CHECK(icons <= 3 * 4);  // three icons, cut by the windows in front of them in four parts at most
        CHECK(rows <= 10 * 4);  // ten rows, cut the same way
        CHECK(maskedArea * 50 < kBudgetArea);  // 2 %
        // Inserted row (Computer in the SYSTEM frame): one button row (36 px at most), checked to the pixel by
        // the STYM2149 scenes; the following rows compared at their former place, two rows lower at most. Removed rows
        // (the eight first shapes of the Shape R13 menu): the same, up.
        // Frames of the main window moved by the sixth DAW test: within the rows of the frames, by a column at
        // most sideways, a new row of one button at most not compared.
        // A moved frame whose former place a menu or an alert covers in the capture: compared with the capture of the
        // same window without it, a scene of the prototype.
        tools::Image alt;
        if (!shot.alt.empty()) {
            const auto& names = gemshot::sceneNames();
            CHECK(std::find(names.begin(), names.end(), shot.alt) != names.end());
            REQUIRE(tools::readPng(std::string(STYM_TEST_DATA) + "/visual/" + shot.alt + ".png", alt, &error));
            alt = gemshot::altCapture(shot, alt);
        }
        for (const gemshot::Shift& s : shot.shifts) {
            CHECK(!s.decision.empty());
            if (s.alt) CHECK((s.decision == "main-columns" && !shot.alt.empty()));
            if (s.decision == "main-columns") {
                CHECK(s.y >= 122);
                CHECK(s.y + s.h <= 532);
                CHECK(std::abs(s.dx) <= 240);
                CHECK(std::abs(s.dy) <= 180);
                CHECK(s.band >= 0);
                CHECK(s.band <= 36);
                continue;
            }
            CHECK((s.dy != 0) != (s.dx != 0));
            if (s.dx != 0) {
                CHECK(s.h <= 36);
                CHECK(std::abs(s.dx) * 4 <= 1016);
                CHECK(s.band == 0);
                continue;
            }
            CHECK(s.dy <= 2 * 36);
            CHECK(s.dy >= -8 * 36);
            const int band = s.band >= 0 ? s.band : std::max(s.dy, 0);  // ignored rows above the zone
            CHECK(band <= 36);
            CHECK(long(s.w) * band * 100 < kBudgetArea);  // band < 1 %
        }
        // Dimmed zones: compared under the veil of the prototype's greyed-out controls. Alpha 0: the inside of
        // the GENERATOR frame where it got longer, or the desktop where the prototype's help line was, one line
        // high. The Mixer's
        // scenes, once compared with the captures, are now STYM2149 scenes.
        for (const gemshot::Dim& m : shot.dims) {
            CHECK(!m.decision.empty());
            CHECK(m.alpha >= 0);
            CHECK(m.alpha < 1);
            if (m.desktopRow >= 0) {
                CHECK(m.decision == "no-shortcut-line");
                CHECK(m.h <= 36);
            } else if (m.alpha == 0)
                CHECK(m.decision == "main-columns");
        }
        const gemshot::Diff d = gemshot::compare(img, ref, kTolerance, shot.masks, shot.shifts, shot.dims,
                                                 shot.alt.empty() ? nullptr : &alt);
        CAPTURE(d.differing, d.percent(), shot.budget);
        CHECK(shot.budget >= 1);
        CHECK(shot.budget <= 2);
        CHECK(d.differing * 100 < kBudgetArea * shot.budget);
    }
}

TEST_CASE("Windows specific to STYM2149 identical to their reference rendering", "[visual]") {
    // Without a prototype screenshot: the reference is a rendering checked by eye (tests/data/visual/stym), any
    // deviation is a change to review.
    // arp_midi, lfo_shapes, mixer_dual, sync, mixer, mixer_3ch, mixer_scope,
    // echo_delay, echo_3ch, desk, menu_file, menu_edit, menu_options, menu_fuji, echo_buzzer3,
    // seq_cmd_box, seq_cmd_plugin, seq_env_menu, seq_step_sid, seq_step_buzzer,
    // seq_step_source, seq_step_env, seq_vol_slide, seq_cmd_curve, sid_square, sid_sine,
    // sid_form_menu, sid_bounce, mixer_type, regs_off,
    // settings_discard, note_off, seq_fit, seq and seq_demo, orn_dma
    REQUIRE(gemshot::stymSceneNames().size() == 66);
    for (const std::string& name : gemshot::stymSceneNames()) {
        CAPTURE(name);
        tools::Image ref;
        std::string error;
        REQUIRE(tools::readPng(std::string(STYM_TEST_DATA) + "/visual/stym/" + name + ".png", ref, &error));
        const gemshot::Shot shot = gemshot::renderScene(name);
        REQUIRE(shot.image.width == ref.width);
        REQUIRE(shot.image.height == ref.height);
        CHECK(shot.masks.empty());
        CHECK(shot.shifts.empty());
        CHECK(shot.dims.empty());
        CHECK(shot.alt.empty());
        const gemshot::Diff d = gemshot::compare(shot.image, ref, kTolerance);
        CHECK(d.differing == 0);
    }
}

TEST_CASE("A scene rendered twice is identical", "[visual]") {
    const tools::Image a = gemshot::renderScene("menu_file").image;
    const tools::Image b = gemshot::renderScene("menu_file").image;
    REQUIRE(a.width > 0);
    CHECK(a.pixels == b.pixels);
}
