// STYM2149 — the demo of the Information box, after the title screen of ST BLIX: draws, logo, background,
// rasters, scroller.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "pluginui/DemoData.h"
#include "pluginui/InfoDemo.h"

using namespace stym;
using pluginui::InfoDemo;

namespace {

constexpr int kW = InfoDemo::kW, kH = InfoDemo::kH;

std::vector<gemui::Color> picture(const InfoDemo& d) { return {d.pixels(), d.pixels() + kW * kH}; }

gemui::Color crate(const InfoDemo& d, int x, int y) {
    namespace demo = pluginui::demo;
    return InfoDemo::colour(
        demo::kWoodPalette[size_t(demo::kCrate.at((x - d.backgroundX()) & 31, (y + d.backgroundY()) & 31))]);
}

}  // namespace

TEST_CASE("Information demo: the data of ST BLIX", "[ui][demo]") {
    namespace demo = pluginui::demo;
    CHECK(demo::kCrate.w == 32);
    CHECK(demo::kCrate.h == 32);
    CHECK(demo::kPrismLogo.h == 29);
    CHECK(demo::kStymLogo.h == 29);
    CHECK(demo::kStymLogo.w <= kW - 6 * 8);  // its widest wave (3 × 2^3 each way) stays on the screen
    CHECK(demo::kFontChars.size() == demo::kFont.size());
    for (const char c : std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.!?\","))
        CHECK(demo::kFontChars.find(c) != std::string_view::npos);
    // sin(0) and sin(90°) of the table.
    CHECK(demo::kSine[0] == 16384);
    CHECK(demo::kSine[90] >= 32767);
    // STE colours: the fourth bit is the lowest.
    CHECK(InfoDemo::colour(0x000) == gemui::rgb(0, 0, 0));
    CHECK(InfoDemo::colour(0x777) == gemui::rgb(238, 238, 238));
    CHECK(InfoDemo::colour(0xFFF) == gemui::rgb(255, 255, 255));
    CHECK(InfoDemo::colour(0x800) == gemui::rgb(17, 0, 0));
}

TEST_CASE("Information demo: the same seed draws the same demo, another seed another one", "[ui][demo]") {
    InfoDemo a(7), b(7), c(8);
    for (const uint64_t v : {1u, 50u, 333u}) {
        CHECK(a.advanceTo(v));
        b.advanceTo(v);
        c.advanceTo(v);
    }
    CHECK(picture(a) == picture(b));
    CHECK(picture(a) != picture(c));
    // Steps back and the same VBL change nothing.
    CHECK_FALSE(a.advanceTo(333));
    CHECK_FALSE(a.advanceTo(100));
    CHECK(a.vbl() == 333);
}

TEST_CASE("Information demo: STYM2149 comes down in 2 s, stays 20 s, goes up in 2 s, PRISM comes 2 s later",
          "[ui][demo]") {
    InfoDemo d(1);
    CHECK(d.logo() == 0);
    CHECK(d.logoTop() == -35);
    d.advanceTo(50);  // a line every other VBL
    CHECK(d.logoTop() == -10);
    d.advanceTo(100);
    CHECK(d.logoTop() == 15);
    d.advanceTo(1100);
    CHECK(d.logoTop() == 15);
    d.advanceTo(1150);
    CHECK(d.logoTop() == -10);
    d.advanceTo(1200);
    CHECK(d.logoTop() == -35);
    CHECK(d.logo() == 0);
    d.advanceTo(1202);  // hidden: the other logo is drawn
    CHECK(d.logo() == 1);
    d.advanceTo(1300);
    CHECK(d.logoTop() == -35);
    d.advanceTo(1400);
    CHECK(d.logoTop() == 15);
    CHECK(d.logo() == 1);
    // And back to STYM2149 after the next 24 s.
    d.advanceTo(1400 + 1100 + 2);
    CHECK(d.logo() == 0);
}

TEST_CASE("Information demo: the crate fills the background, moved along curves that loop in 720 VBLs",
          "[ui][demo]") {
    InfoDemo d(3);
    // At the start, the logo hidden and the text on the right of the screen (its lines moved by 2 pixels at most):
    // only the crate.
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW - 2; ++x) REQUIRE(d.pixels()[y * kW + x] == crate(d, x, y));
    std::vector<std::pair<int, int>> places;
    for (uint64_t v = 2; v <= 720; v += 2) {
        d.advanceTo(v);
        places.emplace_back(d.backgroundX(), d.backgroundY());
    }
    // It moves...
    CHECK(std::count(places.begin(), places.end(), places.front()) < 20);
    // ... and comes back to the same places 360 steps later.
    InfoDemo e(3);
    e.advanceTo(2);
    const std::pair<int, int> first{e.backgroundX(), e.backgroundY()};
    e.advanceTo(722);
    CHECK(std::pair<int, int>{e.backgroundX(), e.backgroundY()} == first);
}

TEST_CASE("Information demo: the rasters move up a line every VBL and colour the face of the logo",
          "[ui][demo]") {
    InfoDemo d(5);
    d.advanceTo(100);  // the logo at its place, lines 15 to 43
    const gemui::Color line20 = d.raster(20);
    d.advanceTo(101);
    CHECK(d.raster(19) == line20);
    // Colour 15 of the logo takes each line's raster.
    int lines = 0;
    for (int y = 17; y < 41; ++y) {
        int n = 0;
        for (int x = 0; x < kW; ++x) n += d.pixels()[y * kW + x] == d.raster(y);
        lines += n > 10;
    }
    CHECK(lines >= 20);
}

TEST_CASE("Information demo: the text scrolls 2 pixels a VBL in from the right, with its black shadow",
          "[ui][demo]") {
    InfoDemo d(9);
    d.advanceTo(10);
    CHECK(d.scrolled() == 20);
    // The first 20 columns of the text are on the screen's right edge (its lines moved by 2 pixels at most): the
    // rest of the text band is crate.
    int changed = 0;
    for (int y = 60; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const bool crateHere = d.pixels()[y * kW + x] == crate(d, x, y);
            if (x < kW - 22) REQUIRE(crateHere);
            else changed += !crateHere;
        }
    CHECK(changed > 50);
    // Later, letters, their rasters and their shadow across the screen.
    d.advanceTo(400);
    int black = 0, raster = 0;
    for (int y = 60; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            black += d.pixels()[y * kW + x] == InfoDemo::colour(0);
            raster += d.pixels()[y * kW + x] == d.raster(y);
        }
    CHECK(black > 500);
    CHECK(raster > 500);
}
