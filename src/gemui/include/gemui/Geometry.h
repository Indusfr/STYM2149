// STYM2149 — gemui: points, rectangles and rounding of layout positions
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Everything is in logical pixels (100 % zoom). The prototype's layout sometimes falls between two pixels (columns of
// 228.5 px, keys of 33.72 px): the browser then rounds each edge separately, half a pixel up. snap() and
// Box::snapped() do the same.
#pragma once
#include <algorithm>
#include <cmath>

namespace stym::gemui {

struct Point {
    int x = 0, y = 0;
    friend bool operator==(const Point&, const Point&) = default;
};

// Rectangle of whole pixels: top-left corner (x, y) and size; right() and bottom() are the first column and row
// outside it.
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    int right() const noexcept { return x + w; }
    int bottom() const noexcept { return y + h; }
    bool empty() const noexcept { return w <= 0 || h <= 0; }
    bool contains(Point p) const noexcept { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    // Common part; when the two do not overlap, an empty rectangle (size 0) at the corner where they would meet.
    Rect intersect(const Rect& o) const noexcept {
        const int l = std::max(x, o.x), t = std::max(y, o.y);
        const int r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        return r > l && b > t ? Rect{l, t, r - l, b - t} : Rect{l, t, 0, 0};
    }
    // Smallest rectangle that contains both (an empty rectangle does not count).
    Rect unite(const Rect& o) const noexcept {
        if (empty()) return o;
        if (o.empty()) return *this;
        const int l = std::min(x, o.x), t = std::min(y, o.y);
        return {l, t, std::max(right(), o.right()) - l, std::max(bottom(), o.bottom()) - t};
    }
    // Shrunk by d on every side (grown when d is negative).
    Rect inset(int d) const noexcept { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect translated(int dx, int dy) const noexcept { return {x + dx, y + dy, w, h}; }
    friend bool operator==(const Rect&, const Rect&) = default;
};

// Layout position → pixel: half up, as the browser does.
inline int snap(double v) noexcept { return int(std::floor(v + 0.5)); }

// Box in fractional coordinates (CSS pixels), rounded edge by edge: the size of snapped() can differ from round(w)
// by one pixel, depending on where the box starts.
struct Box {
    double x = 0, y = 0, w = 0, h = 0;

    double right() const noexcept { return x + w; }
    double bottom() const noexcept { return y + h; }
    Rect snapped() const noexcept {
        const int l = snap(x), t = snap(y);
        return {l, t, snap(x + w) - l, snap(y + h) - t};
    }
};

inline Box boxOf(const Rect& r) noexcept { return {double(r.x), double(r.y), double(r.w), double(r.h)}; }

}  // namespace stym::gemui
