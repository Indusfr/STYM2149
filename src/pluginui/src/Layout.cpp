// STYM2149 — pluginui: flex row of the prototype
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/Layout.h"

#include <algorithm>

namespace stym::pluginui {

double flexRow(double x, double y, double width, std::vector<FlexItem>& items, double gap, bool wrap) {
    // Lines: an item moves to the next line if it would overflow (auto margins counted as 0), except the first of a
    // line. The 1e-6 tolerance keeps an item that fits exactly, up to rounding, on its line.
    std::vector<std::vector<FlexItem*>> lines(1);
    double used = 0;
    for (FlexItem& it : items) {
        const double outer = it.width + (it.autoMargin ? 0 : it.marginLeft);
        auto& line = lines.back();
        if (wrap && !line.empty() && used + gap + outer > width + 1e-6) {
            lines.emplace_back();
            used = 0;
        }
        used += (lines.back().empty() ? 0 : gap) + outer;
        lines.back().push_back(&it);
    }
    // Each line: its height is that of its tallest item; the free space (width less the base widths, fixed margins
    // and gaps) goes to the growable items in proportion to grow, or, if there are none, in equal shares to the
    // auto margins; with neither, it stays on the right.
    double top = y;
    for (size_t li = 0; li < lines.size(); ++li) {
        const auto& line = lines[li];
        double fixed = 0, h = 0, grow = 0;
        int autos = 0;
        for (size_t i = 0; i < line.size(); ++i) {
            const FlexItem& it = *line[i];
            fixed += it.width + (it.autoMargin ? 0 : it.marginLeft) + (i ? gap : 0);
            autos += it.autoMargin ? 1 : 0;
            grow += it.grow;
            h = std::max(h, it.height);
        }
        const double free = std::max(0.0, width - fixed);
        const double share = grow > 0 || !autos ? 0 : free / autos;
        double cx = x;
        for (size_t i = 0; i < line.size(); ++i) {
            FlexItem& it = *line[i];
            if (i) cx += gap;
            cx += it.autoMargin ? share : it.marginLeft;
            const double w = it.width + (grow > 0 ? free * it.grow / grow : 0);
            it.box = {cx, top + (h - it.height) / 2, w, it.height};
            if (it.w) it.w->setBox(it.box);
            cx += w;
        }
        top += h + (li + 1 < lines.size() ? gap : 0);
    }
    return top - y;
}

}  // namespace stym::pluginui
