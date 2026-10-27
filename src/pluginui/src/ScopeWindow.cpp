// STYM2149 — pluginui: scope of one track
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype functions openScope, closeScope, refreshScopeTitle and drawBigScope.
#include "pluginui/ScopeWindow.h"

#include <algorithm>
#include <cmath>

namespace stym::pluginui {

using gemui::Rect;
namespace colors = gemui::colors;

TrackScope::TrackScope() { setPoints({}); }

void TrackScope::setPoints(std::span<const float> points) {
    // drawBigScope: point j at x = j / (n - 1) × W, y = H/2 - v × H × 0.9, 2 px stroke without anti-aliasing (like the
    // scope of the main window).
    canvas_.clear(colors::kWhite);
    const size_t n = points.size();
    // Canvas position of point j; y is clamped to 4 px beyond the top and the bottom of the canvas.
    auto at = [&](size_t j, int& x, int& y) {
        const double v = n ? double(points[std::min(j, n - 1)]) : 0.0;  // one point: a flat line at its value
        x = n > 1 ? int(std::floor(double(j) / double(n - 1) * kW)) : (j ? kW : 0);
        y = int(std::floor(std::clamp(kH / 2.0 - v * kH * 0.9, -4.0, kH + 4.0)));
    };
    const size_t count = std::max<size_t>(n, 2);  // no points: the flat line from 0 to W
    int x0 = 0, y0 = 0;
    at(0, x0, y0);
    for (size_t j = 1; j < count; ++j) {
        int x1 = 0, y1 = 0;
        at(n ? j : 1, x1, y1);
        canvas_.line2(x0, y0, x1, y1, kTrace);
        x0 = x1;
        y0 = y1;
    }
    repaint();
}

void TrackScope::paint(gemui::Painter& p) {
    const Rect r = bounds();
    p.frame(r, 2, colors::kBlack);
    canvas_.blit(p, r.inset(2));
}

ScopeWindow::ScopeWindow(int track) : track_(track) {
    add(scope_);
    place({double(kX), double(kY), kW, kH});
}

void ScopeWindow::setName(const std::string& name) { setTitle("SCOPE - " + name); }

void ScopeWindow::openAt(int n) {
    const double x = kX + kStep * n, y = kY + kStep * n;
    // .mixbody: canvas inline (margin of 8, 2 px border included in its 364 × 144). 14 = border and margin of the
    // window (6) + 8; 40 = title bar (32) + 8. Then the window, which place() keeps on the desktop.
    scope_.setBox({x + 14, y + 40, TrackScope::kW + 4, TrackScope::kH + 4});
    place({x, y, kW, kH});
    open();
}

}  // namespace stym::pluginui
