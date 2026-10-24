// STYM2149 — gemui: framebuffer
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Surface.h"

#include <algorithm>

namespace stym::gemui {

Surface::Surface(int width, int height, Color fill) { resize(width, height, fill); }

void Surface::resize(int width, int height, Color fill) {
    w_ = std::max(0, width);
    h_ = std::max(0, height);
    px_.assign(size_t(w_) * size_t(h_), fill);
}

void Surface::copyTo(Surface& dst, const Rect& src, Point to) const {
    // Clip the source to this surface, move the destination by what was cut, clip it to dst, then cut the source
    // again by what that second clip removed: s and dc end up the same size.
    Rect s = src.intersect(bounds());
    Rect d{to.x + (s.x - src.x), to.y + (s.y - src.y), s.w, s.h};
    const Rect dc = d.intersect(dst.bounds());
    if (dc.empty()) return;
    s = {s.x + (dc.x - d.x), s.y + (dc.y - d.y), dc.w, dc.h};
    for (int y = 0; y < dc.h; ++y) std::copy_n(row(s.y + y) + s.x, dc.w, dst.row(dc.y + y) + dc.x);
}

}  // namespace stym::gemui
