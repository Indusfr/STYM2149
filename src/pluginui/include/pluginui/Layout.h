// STYM2149 — pluginui: flex row of the prototype (.row: 8 px gap, centred items, wrapping)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// CSS layout of the rows of the floating windows (#seqWin .row{flex-wrap:wrap}): items from left to right, separated
// by the gap; an item that no longer fits goes to the next line (lines separated by the same gap); in each line, the
// free space goes first to the growable items (flex-grow, sliders with flex:1) in proportion, otherwise to the "auto"
// left margins in equal shares; each item is centred in the height of its line.
#pragma once
#include <span>
#include <vector>

#include "gemui/Widget.h"

namespace stym::gemui {
class Button;
class Label;
}  // namespace stym::gemui

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

// The two rows at the top of the Digidrums and DMA windows: "Slot", the arrow before, the eight slots of the
// page (a hidden one keeps its place), the arrow after; then "Memory", the gauge and its text, only while the gauge is
// visible (the 3-channel modes; the RAM is chosen in Settings). The labels `lw` wide, the rows
// `width` wide from (x, y); returns the height taken, the gaps after them included.
struct SlotRows {
    gemui::Label& labSlot;
    gemui::Button& prev;
    std::span<gemui::Button> slots;
    gemui::Button& next;
    gemui::Label& labMemory;
    gemui::Widget& gauge;
    gemui::Label& memText;
};
double placeSlotRows(const SlotRows& r, double x, double y, double width, double lw);

}  // namespace stym::pluginui
