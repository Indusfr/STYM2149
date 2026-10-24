// STYM2149 — pluginui: flex row of the prototype (.row: 8 px gap, centred items, wrapping)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// CSS layout of the rows of the floating windows (#seqWin .row{flex-wrap:wrap}): items from left to right, separated
// by the gap; an item that no longer fits goes to the next line (lines separated by the same gap); in each line, the
// free space goes first to the growable items (flex-grow, sliders with flex:1) in proportion, otherwise to the "auto"
// left margins in equal shares; each item is centred in the height of its line.
#pragma once
#include <vector>

#include "gemui/Widget.h"

namespace stym::pluginui {

// One item of a flex row, in logical pixels of the desktop. The caller builds the list for each layout pass; the
// widget is not owned.
struct FlexItem {
    gemui::Widget* w = nullptr;  // nullptr: empty space (fixed-width span or auto margin alone)
    double width = 0, height = 0;  // base width (flex-basis; 0 for a slider with flex:1)
    double marginLeft = 0;       // fixed left margin, ignored when autoMargin is set
    bool autoMargin = false;     // margin-left:auto
    double grow = 0;             // flex-grow
    gemui::Box box{};            // result: place of the item
};

// Places the items in a row of width `width` starting at (x, y); returns its height. gap separates the items of a
// line and the lines; without wrap, all the items stay on one line, even if it overflows. Fills each item's box and
// sets the box of its widget.
double flexRow(double x, double y, double width, std::vector<FlexItem>& items, double gap = 8, bool wrap = true);

}  // namespace stym::pluginui
