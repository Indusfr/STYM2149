// STYM2149 — pluginui: oscilloscope of a track (windows scWin0 to scWin15 of the prototype), opened from the Mixer
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// One window per track, several open at the same time (openScope, closeScope): 360 × 140 canvas inside a 2 px line,
// 2 px green (#0A0) trace of the last 128 points of the track's oscilloscope, y = 70 - v × 126 (drawBigScope).
// Title "SCOPE - " followed by the track name (D-212; scopeName, refreshScopeTitle: "TRACK 3" without a name, A, B, C,
// Digi in 3-channel mode). An opened window goes to (240 + 24 n, 200 + 24 n), n being the number of oscilloscope
// windows already open; once closed, it is forgotten (the prototype removed it from the page).
#pragma once
#include <span>
#include <string>

#include "gemui/Controls.h"
#include "pluginui/Widgets.h"

namespace stym::pluginui {

// Canvas of the window: 360 × 140, 2 px line.
class TrackScope : public gemui::Widget {
public:
    static constexpr int kW = 360, kH = 140;
    static constexpr gemui::Color kTrace = gemui::rgb(0x00, 0xAA, 0x00);  // #0a0

    TrackScope();
    // Points of the track's oscilloscope, from the oldest to the newest; none: flat trace (silence).
    void setPoints(std::span<const float> points);
    void paint(gemui::Painter& p) override;

private:
    Canvas canvas_{kW, kH};
};

// One track's oscilloscope window, created on demand by MainWindow (MainWindow::openScope), which keeps it once made
// (message thread).
class ScopeWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 240, kY = 200, kStep = 24;  // place of the first window, offset of each further one
    static constexpr double kW = 392, kH = 201;  // .mixbody (8 + 144 + 3 of descender + 8), frame and bar

    explicit ScopeWindow(int track);
    int track() const noexcept { return track_; }
    // Title: "SCOPE - " and the track name (D-212; the prototype showed "SCOPE-" with the name right after it, the
    // text of .tt being a flex container that drops the space).
    void setName(const std::string& name);
    void setPoints(std::span<const float> points) { scope_.setPoints(points); }
    // Opens the window at the place of a new one (n: oscilloscope windows already open), in front of the others.
    void openAt(int n);

private:
    int track_;
    TrackScope scope_;
};

}  // namespace stym::pluginui
