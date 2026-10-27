// STYM2149 — gemui: drawing into a framebuffer (rectangles, patterns, lines, text, opacity groups)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Never any anti-aliasing in the framebuffer (STYM2149_SPEC.md §6.1): each pixel takes a colour of the palette, or the
// blend of an opacity group (the prototype's greyed-out controls, opacity 0.25 to 0.45). Another backend can draw the
// same primitives at the screen's resolution (the plugin's juce::Graphics) by overriding the virtual ones.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "gemui/Color.h"
#include "gemui/Font.h"
#include "gemui/Geometry.h"
#include "gemui/Surface.h"

namespace stym::gemui {

// Repeated pattern (4x4 at most); a transparent cell lets what is below show through.
struct Tile {
    int w = 1, h = 1;
    std::array<Color, 16> px{};

    Color at(int x, int y) const noexcept { return px[size_t(y * w + x)]; }
};

// Vector outline of a bitmap (24/10/2026): closed polygons in the bitmap's pixels, filled with the non-zero
// winding rule.
struct Outline {
    std::span<const float> xy;       // x, y of each vertex, polygon after polygon
    std::span<const uint16_t> ends;  // for each polygon, the index of the vertex after its last

    bool inside(double x, double y) const noexcept;
};

// Monochrome image: rows of (w + 7) / 8 bytes, bit 7 = column 0. With an outline, the bits are its raster at scale 1
// (a pixel lit when its centre lies inside), and a painter at another scale may fill the outline instead of enlarging
// the pixels (the Atari logo of the menu bar).
struct Bitmap {
    int w = 0, h = 0;
    const uint8_t* bits = nullptr;
    const Outline* outline = nullptr;

    bool at(int x, int y) const noexcept { return bits[size_t(y * ((w + 7) / 8) + x / 8)] & (0x80 >> (x % 8)); }
};

namespace tiles {
// Slider tracks and scrollbars: black dots at (1,1) and (3,3) of a 4x4 cell, on white.
Tile sliderDots() noexcept;
// Hatched area of the title bar: 2x2 black square at (2,0) of a 4x4 cell, on white.
Tile titleHatch() noexcept;
// Desktop checkerboard: 2x2 quarters, dark green at the top left and the bottom right.
Tile desktop() noexcept;
// Halftone of a played key: every other pixel diagonally, `ink` on `paper`.
Tile halftone(Color ink, Color paper) noexcept;
}  // namespace tiles

// Draws into a Surface, inside a clip rectangle; every primitive clips, so a widget can paint its whole box whatever
// part of it is being repainted. The desktop makes one Painter per area to repaint (Desktop::paint) and sets the clip
// to that area. The virtual primitives are the ones a second backend overrides (JucePainter); text(), textAt()
// and pixel() are built on them. Painting happens on the UI thread only.
class Painter {
public:
    explicit Painter(Surface& surface) noexcept : s_(&surface), area_(surface.bounds()), clip_(area_) {}
    virtual ~Painter() = default;
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    // Drawing area (clipped to the painter's area: the framebuffer, or the desktop for another backend).
    void setClip(const Rect& r) noexcept { clip_ = r.intersect(area_); }
    const Rect& clip() const noexcept { return clip_; }

    void pixel(int x, int y, Color c) noexcept { fill({x, y, 1, 1}, c); }
    virtual void fill(const Rect& r, Color c) noexcept;
    // Border of `t` pixels inside r.
    virtual void frame(const Rect& r, int t, Color c) noexcept;
    // Pattern repeated over r, with the pattern's cell (0,0) at `anchor`.
    virtual void tile(const Rect& r, const Tile& t, Point anchor) noexcept;
    // 1 px line (Bresenham); `thickness` > 1: a square of that size at each step, top-left corner on the line.
    virtual void line(Point a, Point b, Color c, int thickness = 1) noexcept;

    // Text in the GEM font. (x, top): top-left corner of the line (layout positions, placed as the browser does:
    // baseline on a pixel edge, each glyph at the nearest quarter of a pixel); `face`: font of a small text
    // (Font.h).
    void text(std::string_view utf8, double x, double top, int px, Color c, double letterSpacing = 0,
              Face face = Face::kSystem) noexcept;
    // Text placed on a given baseline (x as a layout position).
    void textAt(std::string_view utf8, double x, int baseline, int px, Color c, double letterSpacing = 0,
                Face face = Face::kSystem) noexcept;
    // A single glyph (fontdata index), origin (x + quarter/4, baseline) in pixels.
    virtual void glyph(int index, int x, int baseline, int px, Color c, int quarter = 0,
                       Face face = Face::kSystem) noexcept;
    // Lit pixels of a monochrome image, top-left corner at (x, y).
    virtual void bitmap(const Bitmap& b, int x, int y, Color c) noexcept;
    // Image of w × h opaque pixels stretched over r with nearest-neighbour scaling (centre of the destination pixel
    // mapped back into the image), as a canvas with image-rendering: pixelated.
    virtual void image(const Rect& r, const Color* px, int w, int h) noexcept;
    // Colour c laid over r at opacity alpha (veil of a modal box).
    virtual void shade(const Rect& r, Color c, double alpha) noexcept;

    // Opacity group: what is painted in r between begin and end is blended with alpha over what was below. Groups
    // nest. Only the part of r inside the drawing area counts: between begin and end, the drawing area can shrink,
    // never grow.
    virtual void beginOpacity(const Rect& r, double alpha);
    virtual void endOpacity();

protected:
    // Backend without a framebuffer, drawing `area`.
    explicit Painter(const Rect& area) noexcept : s_(nullptr), area_(area), clip_(area) {}

private:
    // One pixel of the framebuffer backend, dropped outside the clip (glyphs and bitmaps).
    void put(int x, int y, Color c) noexcept {
        if (clip_.contains({x, y})) s_->row(y)[x] = c;
    }
    // Open opacity group: its area (already clipped) and a copy of the pixels it covered at beginOpacity.
    struct Group {
        Rect r;
        double alpha;
        std::vector<Color> under;  // r.w × r.h, row by row
    };
    Surface* s_;           // nullptr for a backend without a framebuffer
    Rect area_, clip_;     // whole drawable area; current clip, always inside area_
    std::vector<Group> groups_;  // nested groups, innermost last
};

}  // namespace stym::gemui
