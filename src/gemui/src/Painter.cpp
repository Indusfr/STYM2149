// STYM2149 — gemui: drawing
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Painter.h"

#include <algorithm>
#include <cstdlib>

#include "gemui/Font.h"

namespace stym::gemui {

namespace tiles {

Tile sliderDots() noexcept {
    Tile t{4, 4, {}};
    t.px.fill(colors::kWhite);
    t.px[1 * 4 + 1] = colors::kBlack;
    t.px[3 * 4 + 3] = colors::kBlack;
    return t;
}

Tile titleHatch() noexcept {
    Tile t{4, 4, {}};
    t.px.fill(colors::kWhite);
    for (int y = 0; y < 2; ++y)
        for (int x = 2; x < 4; ++x) t.px[size_t(y * 4 + x)] = colors::kBlack;
    return t;
}

Tile desktop() noexcept {
    // repeating-conic-gradient(green 0 25 %, #00A000 0 50 %) on 4x4: from noon clockwise, top-right quarter green,
    // bottom-right dark, bottom-left green, top-left dark.
    Tile t{4, 4, {}};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            t.px[size_t(y * 4 + x)] = (x < 2) == (y < 2) ? colors::kDesktopDark : colors::kGreen;
    return t;
}

Tile halftone(Color ink, Color paper) noexcept {
    Tile t{2, 2, {}};
    t.px[0] = ink;
    t.px[1] = paper;
    t.px[2] = paper;
    t.px[3] = ink;
    return t;
}

}  // namespace tiles

void Painter::fill(const Rect& r, Color c) noexcept {
    const Rect d = r.intersect(clip_);
    if (d.empty()) return;
    for (int y = d.y; y < d.bottom(); ++y) {
        Color* p = s_->row(y) + d.x;
        for (int x = 0; x < d.w; ++x) p[x] = c;
    }
}

void Painter::frame(const Rect& r, int t, Color c) noexcept {
    if (r.empty() || t <= 0) return;
    fill({r.x, r.y, r.w, std::min(t, r.h)}, c);
    fill({r.x, r.bottom() - t, r.w, std::min(t, r.h)}, c);
    fill({r.x, r.y + t, std::min(t, r.w), r.h - 2 * t}, c);
    fill({r.right() - t, r.y + t, std::min(t, r.w), r.h - 2 * t}, c);
}

void Painter::tile(const Rect& r, const Tile& t, Point anchor) noexcept {
    const Rect d = r.intersect(clip_);
    if (d.empty()) return;
    // Phase of the pattern at the clipped area's corner; the double modulo stays positive left of or above the
    // anchor. Repainting any part of r thus gives the same pixels as painting r whole.
    const int tx0 = ((d.x - anchor.x) % t.w + t.w) % t.w;
    for (int y = d.y; y < d.bottom(); ++y) {
        const int ty = ((y - anchor.y) % t.h + t.h) % t.h;
        const Color* row = t.px.data() + size_t(ty * t.w);
        Color* p = s_->row(y);
        for (int x = d.x, tx = tx0; x < d.right(); ++x) {
            if (opaque(row[tx])) p[x] = row[tx];
            if (++tx == t.w) tx = 0;
        }
    }
}

// Integer Bresenham with one error term for both axes (every octant), from a to b, both ends drawn.
void Painter::line(Point a, Point b, Color c, int thickness) noexcept {
    int x = a.x, y = a.y;
    const int dx = std::abs(b.x - a.x), sx = a.x < b.x ? 1 : -1;
    const int dy = -std::abs(b.y - a.y), sy = a.y < b.y ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (thickness <= 1) pixel(x, y, c);
        else fill({x, y, thickness, thickness}, c);
        if (x == b.x && y == b.y) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
    }
}

void Painter::glyph(int index, int x, int baseline, int px, Color c, int quarter, Face face) noexcept {
    // Each row is 16 bits, bit 15 at column 0 (8×8 and 6×6 rows are widened to that layout by Raster::bits); shift:
    // extra columns to the right when an 8×16 quarter reuses the glyph set of another (FontData.h).
    const Font::Raster g = Font::raster(px, index, quarter, face);
    for (int r = 0; r < g.count; ++r) {
        const uint16_t bits = g.bits(r);
        if (!bits) continue;
        for (int b = 0; b < 16; ++b)
            if (bits & (0x8000 >> b)) put(x + g.shift + b, baseline + g.top + r, c);
    }
}

// Baseline: top of the line plus the ascent of its line box, rounded to a pixel edge (Chromium's line-height:
// normal); a character outside the GEM font makes the line box taller and moves the baseline down (Font::lineBox).
void Painter::text(std::string_view utf8, double x, double top, int px, Color c, double letterSpacing,
                   Face face) noexcept {
    textAt(utf8, x, snap(top + Font::lineBox(utf8, px, face).ascent), px, c, letterSpacing, face);
}

bool Outline::inside(double x, double y) const noexcept {
    // Winding number of the point: each edge crossing the horizontal half-line to its right counts +1 or -1 by its
    // direction; an edge includes its end of smaller y only, so a vertex on the half-line counts once.
    int winding = 0;
    size_t first = 0;
    for (const uint16_t end : ends) {
        for (size_t k = first; k < end; ++k) {
            const size_t j = k == first ? end - 1 : k - 1;
            const double x0 = xy[2 * j], y0 = xy[2 * j + 1], x1 = xy[2 * k], y1 = xy[2 * k + 1];
            if ((y0 <= y && y < y1) || (y1 <= y && y < y0))
                if (x0 + (y - y0) * (x1 - x0) / (y1 - y0) > x) winding += y1 > y0 ? 1 : -1;
        }
        first = end;
    }
    return winding != 0;
}

void Painter::bitmap(const Bitmap& b, int x, int y, Color c) noexcept {
    for (int r = 0; r < b.h; ++r)
        for (int k = 0; k < b.w; ++k)
            if (b.at(k, r)) put(x + k, y + r, c);
}

void Painter::image(const Rect& r, const Color* px, int w, int h) noexcept {
    if (r.empty() || w <= 0 || h <= 0) return;
    const Rect d = r.intersect(clip_);
    if (d.empty()) return;
    std::vector<int> cols(size_t(d.w));  // image column for each column of d
    for (int x = d.x; x < d.right(); ++x) cols[size_t(x - d.x)] = std::min(w - 1, int((x - r.x + 0.5) * w / r.w));
    for (int y = d.y; y < d.bottom(); ++y) {
        const Color* src = px + size_t(std::min(h - 1, int((y - r.y + 0.5) * h / r.h)) * w);
        Color* dst = s_->row(y) + d.x;
        for (int k = 0; k < d.w; ++k) dst[k] = src[cols[size_t(k)]];
    }
}

void Painter::shade(const Rect& r, Color c, double alpha) noexcept {
    const Rect d = r.intersect(clip_);
    for (int y = d.y; y < d.bottom(); ++y) {
        Color* row = s_->row(y);
        for (int x = d.x; x < d.right(); ++x) row[x] = mix(c, row[x], alpha);
    }
}

// The pen advances in fractional layout pixels and each glyph is rounded on its own (glyphPos), so the rounding does
// not add up along the line. Spaces only advance.
void Painter::textAt(std::string_view utf8, double x, int baseline, int px, Color c, double letterSpacing,
                     Face face) noexcept {
    double pen = x;
    for (size_t pos = 0; pos < utf8.size();) {
        const char32_t cp = nextCodepoint(utf8, pos);
        const double at = pen;
        pen += Font::advance(cp, px, face) + letterSpacing;
        if (cp == ' ' || cp == 0xA0) continue;
        const GlyphPos g = glyphPos(at);
        glyph(Font::glyph(cp), g.x, baseline, px, c, g.quarter, face);
    }
}

// An opacity group saves what lies under its area; what is painted until endOpacity is then blended over that copy,
// pixel by pixel, as a browser composites an element with opacity < 1.
void Painter::beginOpacity(const Rect& r, double alpha) {
    Group g;
    g.r = r.intersect(clip_);  // outside the drawing area, nothing changes: nothing to blend
    g.alpha = alpha;
    g.under.resize(size_t(std::max(0, g.r.w)) * size_t(std::max(0, g.r.h)));
    for (int y = 0; y < g.r.h; ++y)
        std::copy_n(s_->row(g.r.y + y) + g.r.x, g.r.w, g.under.data() + size_t(y) * size_t(g.r.w));
    groups_.push_back(std::move(g));
}

void Painter::endOpacity() {
    if (groups_.empty()) return;
    const Group g = std::move(groups_.back());
    groups_.pop_back();
    for (int y = 0; y < g.r.h; ++y) {
        Color* p = s_->row(g.r.y + y) + g.r.x;
        const Color* u = g.under.data() + size_t(y) * size_t(g.r.w);
        // A pixel the group left as it was is skipped: blended with itself it would not change.
        for (int x = 0; x < g.r.w; ++x)
            if (p[x] != u[x]) p[x] = mix(p[x], u[x], g.alpha);
    }
}

}  // namespace stym::gemui
