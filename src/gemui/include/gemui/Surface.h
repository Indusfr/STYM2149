// STYM2149 — gemui: framebuffer in logical pixels
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <vector>

#include "gemui/Color.h"
#include "gemui/Geometry.h"

namespace stym::gemui {

// Framebuffer: width × height colours stored row by row (pixel (x, y) at y × width + x), one per logical pixel.
// The desktop owns the one it paints into (Desktop::surface); tools and tests make their own. at() and row() do not
// check their arguments: callers stay inside bounds() (Painter clips first).
class Surface {
public:
    Surface() = default;
    Surface(int width, int height, Color fill = colors::kBlack);

    // New size (negative sizes count as 0), every pixel set to `fill`: the old content is not kept.
    void resize(int width, int height, Color fill = colors::kBlack);
    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }
    Rect bounds() const noexcept { return {0, 0, w_, h_}; }

    Color at(int x, int y) const noexcept { return px_[size_t(y) * size_t(w_) + size_t(x)]; }
    Color* row(int y) noexcept { return px_.data() + size_t(y) * size_t(w_); }
    const Color* row(int y) const noexcept { return px_.data() + size_t(y) * size_t(w_); }
    const std::vector<Color>& pixels() const noexcept { return px_; }

    // Copy of an area (source area in this, destination corner in dst; what overflows is clipped).
    void copyTo(Surface& dst, const Rect& src, Point to) const;

private:
    int w_ = 0, h_ = 0;
    std::vector<Color> px_;
};

}  // namespace stym::gemui
