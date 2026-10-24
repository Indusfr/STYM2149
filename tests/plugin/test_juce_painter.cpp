// STYM2149 — plugin: GEM drawing with juce::Graphics at the screen's resolution (D-201)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

#include "JucePainter.h"
#include "gemui/Font.h"

using namespace stym;
using namespace stym::plugin;
namespace colors = gemui::colors;

namespace {

constexpr int kW = 96, kH = 48;

// The same drawing for any painter: rectangles, borders, a pattern, text at several quarters, the 8×8 and 6×6 fonts,
// an icon, an image, an opacity group, a veil.
void scene(gemui::Painter& p) {
    p.fill({0, 0, kW, kH}, colors::kWhite);
    p.tile({2, 2, 40, 12}, gemui::tiles::sliderDots(), {2, 2});
    p.frame({4, 16, 30, 14}, 2, colors::kBlack);
    p.frame({40, 16, 17, 9}, 1, colors::kBlack);
    p.textAt("Ab1", 6.25, 28, 14, colors::kBlack);
    p.textAt("Ab1", 44.5, 44, 14, colors::kBlack);
    p.textAt("Tip", 60.75, 12, 10, colors::kBlack);
    p.textAt("Sm", 60, 24, 12, colors::kBlack, 0, gemui::Face::kSmall);
    static const uint8_t bits[] = {0x18, 0x3C, 0x7E, 0xFF};
    p.bitmap({8, 4, bits}, 80, 28, colors::kBlack);
    static const std::array<gemui::Color, 6> img{colors::kLed, colors::kGreen, colors::kBlack,
                                                 colors::kGrey, colors::kWhite, colors::kLedOff};
    p.image({70, 36, 20, 10}, img.data(), 3, 2);
    p.beginOpacity({2, 32, 30, 14}, 0.35);
    p.fill({4, 34, 10, 10}, colors::kLed);
    p.textAt("x", 18, 44, 14, colors::kBlack);
    p.endOpacity();
    p.shade({60, 0, 30, 10}, colors::kBlack, 0.25);
}

juce::Image canvas(double d, int w = kW, int h = kH) {
    juce::Image image(juce::Image::ARGB, int(std::lround(w * d)), int(std::lround(h * d)), false,
                      juce::SoftwareImageType());
    image.clear(image.getBounds(), juce::Colours::black);
    return image;
}

uint32_t argb(const juce::Image& image, int x, int y) { return image.getPixelAt(x, y).getARGB(); }

}  // namespace

TEST_CASE("JucePainter: at a whole scale, the framebuffer in blocks of d × d screen pixels (D-201)", "[jucepainter]") {
    gemui::Surface s(kW, kH, colors::kBlack);
    gemui::Painter fb(s);
    scene(fb);
    for (const int d : {1, 2, 3}) {
        juce::Image image = canvas(d);
        JucePainter::Cache cache;
        cache.setScale(d);
        {
            JucePainter p(image, {0, 0, kW, kH}, cache, image.getBounds());
            scene(p);
        }
        int differ = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (argb(image, x, y) != (s.row(y / d)[x / d] | 0xFF000000u)) ++differ;
        INFO("d = " << d);
        CHECK(differ == 0);
    }
}

TEST_CASE("JucePainter: at 1.25 and 0.625, every occurrence of a glyph alike, borders of one thickness, no gap",
          "[jucepainter]") {
    for (const double d : {1.25, 0.625}) {
        INFO("d = " << d);
        constexpr int w = 240, h = 60;
        juce::Image image = canvas(d, w, h);
        JucePainter::Cache cache;
        cache.setScale(d);
        const auto at = [d](double v) { return int(std::floor(v * d + 0.5)); };
        {
            JucePainter p(image, {0, 0, w, h}, cache, image.getBounds());
            p.fill({0, 0, w, h}, colors::kWhite);
            // Glyphs alone, at layout positions of every fraction of a pixel.
            for (int k = 0; k < 8; ++k) p.textAt("A", 4 + 21.37 * k, 18, 14, colors::kBlack);
            // Borders of 1 and 2 pixels at every position.
            for (int k = 0; k < 12; ++k) {
                p.frame({3 + 19 * k, 30, 11, 11}, 1, colors::kBlack);
                p.frame({3 + 19 * k + 1, 44, 13, 13}, 2, colors::kBlack);
            }
        }
        // Each "A": the same pixels around its origin placed on a screen pixel.
        const auto cell = [&](int k) {
            const gemui::GlyphPos g = gemui::glyphPos(4 + 21.37 * k);
            const int x0 = at(g.x + g.quarter / 4.0) - 2, y0 = at(18) - int(std::ceil(14 * d)) - 2;
            std::vector<uint32_t> px;
            for (int y = y0; y < y0 + int(std::ceil(18 * d)) + 4; ++y)
                for (int x = x0; x < x0 + int(std::ceil(8 * d)) + 4; ++x) px.push_back(argb(image, x, y));
            return px;
        };
        const auto first = cell(0);
        bool alike = true, inked = false;
        for (const uint32_t v : first) inked = inked || v != 0xFFFFFFFFu;
        for (int k = 1; k < 8; ++k) alike = alike && cell(k) == first;
        CHECK(inked);
        CHECK(alike);
        // Thickness of the borders: t × d rounded down, at least one pixel, on every side.
        const auto run = [&](int x, int y, int dx, int dy) {
            int n = 0;
            while (argb(image, x + n * dx, y + n * dy) == 0xFF000000u) ++n;
            return n;
        };
        for (int k = 0; k < 12; ++k)
            for (const auto& [r, t] : {std::pair{gemui::Rect{3 + 19 * k, 30, 11, 11}, 1},
                                       std::pair{gemui::Rect{3 + 19 * k + 1, 44, 13, 13}, 2}}) {
                const int want = std::max(1, int(std::floor(t * d)));
                const int midX = at(r.x + r.w / 2.0), midY = at(r.y + r.h / 2.0);
                CHECK(run(at(r.x), midY, 1, 0) == want);
                CHECK(run(at(r.right()) - 1, midY, -1, 0) == want);
                CHECK(run(midX, at(r.y), 0, 1) == want);
                CHECK(run(midX, at(r.bottom()) - 1, 0, -1) == want);
            }
    }
    // Rectangles side by side cover every screen pixel, one pixel wide each or two.
    for (const double d : {1.25, 0.625, 1.75}) {
        juce::Image image = canvas(d, 100, 4);
        JucePainter::Cache cache;
        cache.setScale(d);
        {
            JucePainter p(image, {0, 0, 100, 4}, cache, image.getBounds());
            for (int x = 0; x < 100; ++x) p.fill({x, 0, 1, 4}, x % 2 ? colors::kWhite : colors::kLed);
        }
        int black = 0;
        for (int x = 0; x < image.getWidth(); ++x) black += argb(image, x, 1) == 0xFF000000u;
        CHECK(black == 0);
    }
}

TEST_CASE("JucePainter: sharp pixels, letters and icons without anti-aliasing at any scale (tenth DAW test, item 123, "
          "D-331)",
          "[jucepainter]") {
    static const uint8_t bits[] = {0x18, 0x3C, 0x7E, 0xFF};
    for (const double d : {1.25, 0.625, 1.75}) {
        INFO("d = " << d);
        constexpr int w = 240, h = 40;
        juce::Image image = canvas(d, w, h);
        JucePainter::Cache cache;
        cache.setScale(d);
        cache.setSharp(true);
        {
            JucePainter p(image, {0, 0, w, h}, cache, image.getBounds());
            p.fill({0, 0, w, h}, colors::kWhite);
            for (int k = 0; k < 8; ++k) p.textAt("Ab", 4 + 21.37 * k, 18, 14, colors::kBlack);
            p.textAt("Sm", 4, 34, 12, colors::kBlack, 0, gemui::Face::kSmall);
            p.textAt("Tip", 60.75, 34, 10, colors::kBlack);
            p.bitmap({8, 4, bits}, 120, 26, colors::kBlack);
        }
        int black = 0, grey = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x) {
                const uint32_t v = argb(image, x, y);
                black += v == 0xFF000000u;
                grey += v != 0xFF000000u && v != 0xFFFFFFFFu;
            }
        CHECK(black > 0);
        CHECK(grey == 0);
        // Smooth again: the letters have grey edges.
        cache.setSharp(false);
        {
            JucePainter p(image, {0, 0, w, h}, cache, image.getBounds());
            p.fill({0, 0, w, h}, colors::kWhite);
            p.textAt("Ab", 4, 18, 14, colors::kBlack);
        }
        grey = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x) {
                const uint32_t v = argb(image, x, y);
                grey += v != 0xFF000000u && v != 0xFFFFFFFFu;
            }
        CHECK(grey > 0);
    }
}

TEST_CASE("JucePainter: nothing written outside the limit", "[jucepainter]") {
    juce::Image image = canvas(1.25);
    JucePainter::Cache cache;
    cache.setScale(1.25);
    const juce::Rectangle<int> limit(30, 20, 40, 25);
    {
        JucePainter p(image, {0, 0, kW, kH}, cache, limit);
        scene(p);
    }
    bool outside = false, inside = false;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x) {
            const bool black = argb(image, x, y) == 0xFF000000u;
            if (limit.contains(x, y)) inside = inside || !black;
            else outside = outside || !black;
        }
    CHECK(inside);
    CHECK_FALSE(outside);
}

TEST_CASE("JucePainter: an oblique line is smoothed at any scale, a straight one stays in pixels (D-214)",
          "[jucepainter]") {
    for (const double d : {1.0, 1.25, 2.0}) {
        INFO("d = " << d);
        juce::Image image = canvas(d, 40, 20);
        JucePainter::Cache cache;
        cache.setScale(d);
        {
            JucePainter p(image, {0, 0, 40, 20}, cache, image.getBounds());
            p.fill({0, 0, 40, 20}, colors::kWhite);
            p.line({2, 2}, {30, 12}, colors::kBlack);  // rising part of a Smooth ADSR curve
            p.line({2, 16}, {30, 16}, colors::kBlack);
        }
        // Oblique: intermediate greys along the stroke (anti-aliasing), no pixel of the framebuffer's stairs only.
        int greys = 0, blacks = 0;
        for (int y = 0; y < int(std::lround(14 * d)); ++y)
            for (int x = 0; x < image.getWidth(); ++x) {
                const juce::Colour c = image.getPixelAt(x, y);
                if (c.getRed() == 0) ++blacks;
                else if (c.getRed() < 255) ++greys;
            }
        CHECK(greys > 20);
        CHECK(blacks + greys > 0);
        // Straight: the screen pixels of the framebuffer's row 16, fully black, the rows around it white.
        const juce::Rectangle<int> row = JucePainter::onScreen({2, 16, 29, 1}, d);
        for (int x = row.getX(); x < row.getRight(); ++x) {
            CHECK(image.getPixelAt(x, row.getY()).getRed() == 0);
            CHECK(image.getPixelAt(x, row.getY() - 1).getRed() == 255);
            CHECK(image.getPixelAt(x, row.getBottom()).getRed() == 255);
        }
    }
}
