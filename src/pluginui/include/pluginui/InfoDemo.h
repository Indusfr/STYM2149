// STYM2149 — pluginui: the demo of the Information box, after the title screen of ST BLIX
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Indus, 29/10/2026 04:20: the moving background with the wooden crate, the PRISM / STYM2149 logo that waves and swaps,
// the same rasters in the black parts of the font and of the logo (colour 15), and a text scrolling in waves with a
// slight undulation of the vertical axis, with the same rasters. As ST BLIX does it (STBLIX.S, AFF_FOND.S, AFF_TITL.S,
// GENCURVE.S), counted in VBLs of 50 Hz:
// - background: the crate tiled, moved every other VBL along two random curves (one for x, one for y) of 360 points,
//   each the sum of three sines of whole harmonics, so that it loops;
// - logo, every other VBL: hidden for 2 s, it comes down from above the screen in 2 s (a line per step), stays 20 s,
//   goes up in 2 s; each time it is hidden, the other logo, a palette of the 8 and a wave are drawn at random: each line
//   moves sideways by a sine of its own phase (amplitude 3 × 2^s pixels, s from 0 to 4, and a phase step of 0 to 15° a
//   line, turning as much a step); a black shadow 5 pixels to the right and below;
// - rasters: colour 15 of each line from a table of 512 lines that moves up a line every VBL.
// The scroller is this demo's own: 2 pixels a VBL, each column of letters up and down along a sine wave that moves,
// each line moved sideways by a sine of at most 2 pixels, the same black shadow; space 16 pixels, the glyphs 32.
// The demo starts as the first logo (STYM2149) comes down, the text entering from the right.
#pragma once
#include <cstdint>
#include <vector>

#include "gemui/Color.h"

namespace stym::pluginui {

class InfoDemo {
public:
    // A strip of an ST low-resolution screen, each pixel drawn 2 × 2 in the box.
    static constexpr int kW = 320, kH = 128;
    // seed: the random draws (curves of the background, waves, palettes), so that a test sees the same demo.
    explicit InfoDemo(uint32_t seed);

    // Brings the demo to VBL `vbl` from its start (50 a second; steps back are ignored); true when the picture
    // changed.
    bool advanceTo(uint64_t vbl);
    // The picture, kW × kH opaque pixels, row by row.
    const gemui::Color* pixels() const noexcept { return px_.data(); }
    uint64_t vbl() const noexcept { return vbl_; }

    // State (tests): the logo shown (0 STYM2149, 1 PRISM) and the top line of the logo, -35 when hidden; the
    // background's offsets in the crate; the text's position (pixels scrolled).
    int logo() const noexcept { return logo_; }
    int logoTop() const noexcept { return logoY_; }
    int backgroundX() const noexcept;
    int backgroundY() const noexcept;
    int scrolled() const noexcept { return int(scroll_); }
    // Colour of line y's rasters at the current VBL (the face of the logo and of the letters).
    gemui::Color raster(int y) const noexcept;
    // An Atari palette word as a colour (STE: 4 bits per component, its lowest in bit 3 of the nibble).
    static gemui::Color colour(uint16_t word) noexcept;

private:
    enum class Phase { Hidden, Down, Shown, Up };
    uint32_t random() noexcept;
    void newLogo();       // the next logo, its palette and its wave
    void makeCurves();    // the background's two curves
    void step();          // what ST BLIX moves every other VBL: background and logo
    void render();
    void drawLogo();
    void drawText();

    uint32_t seed_;
    uint64_t vbl_ = 0;
    std::vector<gemui::Color> px_;
    std::vector<int16_t> curveX_, curveY_;
    int curve_ = 0;  // point of the curves
    Phase phase_ = Phase::Down;
    int timer_ = 0;  // steps in the phase
    int logo_ = 1;   // newLogo() swaps it: STYM2149 first
    int logoY_ = -35;
    int palette_ = 0, shift_ = 0, speed_ = 0, angle_ = 0;
    uint64_t scroll_ = 0;
};

}  // namespace stym::pluginui
