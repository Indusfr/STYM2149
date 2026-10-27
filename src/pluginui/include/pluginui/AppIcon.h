// STYM2149 — pluginui: the application icon as pixels, for the Information box
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The 32 × 32 drawing of packaging/icon/STYM2149-32.png, written into AppIcon.cpp by tools/icon/make_icon_data.py:
// 0xAARRGGBB colours row by row, alpha 0 for a transparent pixel.
#pragma once
#include "gemui/Color.h"

namespace stym::pluginui::icons {

// A picture of w × h pixels in static storage: `px` holds w × h colours, row by row from the top left.
struct AppIcon {
    int w, h;
    const gemui::Color* px;
};
// The 32 × 32 icon. MainWindow::showInfo hands it to the Information box (Alert::Spec::picture), each pixel drawn as
// a 2 × 2 square.
extern const AppIcon kApp;

}  // namespace stym::pluginui::icons
