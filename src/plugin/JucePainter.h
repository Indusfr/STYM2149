// STYM2149 — plugin: GEM drawing with juce::Graphics at the screen's resolution
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The editor draws the GEM desktop into an image at the screen's resolution, d screen pixels per desktop pixel (zoom
// × display scale), with the primitives of gemui::Painter:
// - a rectangle covers the screen pixels between its edges rounded to the nearest pixel (at least one pixel);
// - the border of frame() keeps the same thickness everywhere, t × d rounded down (at least one pixel), as a browser
//   draws a CSS border;
// - a pattern is drawn once per cell at the screen's resolution then repeated: every cell looks the same;
// - a glyph or an icon is a mask computed once, placed on whole screen pixels: every occurrence looks the same.
// - an oblique line (the Smooth ADSR curve) is a stroke through the centres of its end pixels, d pixels wide, with
//   anti-aliasing at any d (second DAW test, item 13); a horizontal or vertical line stays a rectangle.
// - a bitmap with an outline (the Atari logo of the menu bar) is that outline filled with anti-aliasing at any
//   d but 1; with sharp pixels (at any d), the screen pixels whose centre lies inside.
// At a whole d, each desktop pixel becomes a d × d block (glyphs from the framebuffer's rasters): the image is the
// framebuffer enlarged with nearest-neighbour scaling, oblique lines and outlines aside. Otherwise glyphs and icons
// are the contours of their pixel squares (the prototype's TrueType "GEM 8x16") filled with anti-aliasing, as the
// browser shows the prototype on a 125 or 150 % display; with sharp pixels each square goes to whole screen
// pixels instead, its edges rounded on the grid of the glyph: crisp edges, squares of unequal sizes.
#pragma once
#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "gemui/Painter.h"

namespace stym::plugin {

// One drawing pass, on the message thread: StymEditor::draw builds a painter for each changed area and lets the
// desktop draw into it. The Cache belongs to the editor and outlives the painters.
class JucePainter final : public gemui::Painter {
public:
    // Masks of the glyphs and icons and cells of the patterns at one value of d.
    class Cache {
    public:
        // Empties the cache if d changes.
        void setScale(double d);
        double scale() const noexcept { return d_; }
        // Sharp pixels (tenth DAW test, item 123): at a scale that is not whole, the pixel squares of the
        // glyphs and icons on whole screen pixels, without anti-aliasing. Empties the cache if it changes.
        void setSharp(bool sharp);
        bool sharp() const noexcept { return sharp_; }

        struct Mask {
            juce::Image image;  // single channel; invalid: nothing to draw
            int x = 0, y = 0;   // top-left corner, relative to the origin of the glyph or icon
        };

    private:
        friend class JucePainter;
        double d_ = 0;
        bool sharp_ = false;
        std::unordered_map<uint64_t, Mask> glyphs_;          // key: glyph, size, face and quarter (glyphMask)
        std::unordered_map<const uint8_t*, Mask> bitmaps_;   // key: address of the bitmap's bits (static icon data)
        std::unordered_map<uint64_t, juce::Image> tiles_;    // key: FNV-1a hash of the cell's size and pixels
    };

    // Draws the desktop of `area` (desktop pixels) into `image` (ARGB, software, opaque) at the scale of `cache`,
    // without writing outside `limit` (pixels of the image).
    JucePainter(juce::Image& image, const gemui::Rect& area, Cache& cache, const juce::Rectangle<int>& limit);

    // Screen pixels of a rectangle of the desktop: edges rounded to the nearest pixel, at least one pixel.
    static juce::Rectangle<int> onScreen(const gemui::Rect& r, double d) noexcept;

    void fill(const gemui::Rect& r, gemui::Color c) noexcept override;
    void frame(const gemui::Rect& r, int t, gemui::Color c) noexcept override;
    void tile(const gemui::Rect& r, const gemui::Tile& t, gemui::Point anchor) noexcept override;
    void line(gemui::Point a, gemui::Point b, gemui::Color c, int thickness = 1) noexcept override;
    void glyph(int index, int x, int baseline, int px, gemui::Color c, int quarter = 0,
               gemui::Face face = gemui::Face::kSystem) noexcept override;
    void bitmap(const gemui::Bitmap& b, int x, int y, gemui::Color c) noexcept override;
    void image(const gemui::Rect& r, const gemui::Color* px, int w, int h) noexcept override;
    void shade(const gemui::Rect& r, gemui::Color c, double alpha) noexcept override;
    void beginOpacity(const gemui::Rect& r, double alpha) override;
    void endOpacity() override;

private:
    int at(double v) const noexcept;  // screen pixel of the desktop coordinate v
    juce::Rectangle<int> screenClip() const noexcept;  // the Painter's clip in screen pixels, within limit_
    // Colour c laid through mask m whose origin falls on screen pixel (x, y).
    void drawMask(const Cache::Mask& m, int x, int y, gemui::Color c) noexcept;
    const Cache::Mask& glyphMask(int index, int px, int quarter, gemui::Face face);
    const juce::Image& tileCell(const gemui::Tile& t);  // one cell of the pattern at the screen's resolution

    // Opacity group in progress (beginOpacity): its screen rectangle, its alpha and the pixels it covered before.
    struct Group {
        juce::Rectangle<int> r;
        double alpha;
        std::vector<gemui::Color> under;
    };
    juce::Image& image_;
    juce::Graphics g_;
    Cache& cache_;
    juce::Rectangle<int> limit_;  // screen pixels this pass may change (the area being redrawn)
    double d_;                    // screen pixels per desktop pixel
    int whole_;   // d if it is a whole number, otherwise 0
    bool sharp_;  // pixel squares on whole screen pixels (Cache::setSharp), d not whole
    std::vector<Group> groups_;
};

}  // namespace stym::plugin
