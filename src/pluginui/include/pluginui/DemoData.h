// STYM2149 — pluginui: pictures and colours of the Information box's demo, from ST BLIX
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Written by tools/demo/make_demo_data.py (DemoData.cpp) from the files of ST BLIX that Indus gave for the demo on
// 29/10/2026: the wooden crate of its background, its PRISM logo, the 32 × 32 font, its palettes, rasters and sine
// table. Colours are Atari palette words: 3 bits per component, the STE's fourth (its lowest) in bit 3 of each nibble.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace stym::pluginui::demo {

// A picture: h rows of w characters, a colour index ('0'-'9', 'A'-'F') per pixel, '.' where it is transparent.
struct Picture {
    int w = 0, h = 0;
    const char* const* rows = nullptr;

    // Colour index of a pixel inside the picture, -1 where it is transparent.
    int at(int x, int y) const noexcept {
        const char c = rows[y][x];
        return c == '.' ? -1 : c <= '9' ? c - '0' : c - 'A' + 10;
    }
};

extern const Picture kCrate;      // 32 × 32, in kWoodPalette
extern const Picture kPrismLogo;  // 207 × 29, in a logo palette
extern const Picture kStymLogo;   // 255 × 29, in a logo palette

// The font: 32 × 32 glyphs, the letters on the bottom row; kFontChars[i] is drawn by kFont[i], in kFontPalette.
extern const std::string_view kFontChars;
extern const std::array<Picture, 41> kFont;

extern const std::array<uint16_t, 16> kWoodPalette;
extern const std::array<uint16_t, 16> kFontPalette;
extern const std::array<std::array<uint16_t, 16>, 8> kLogoPalettes;
// Colour 15, which the face of the logos and of the font shows, line after line (16 gradients of 32 lines).
extern const std::array<uint16_t, 512> kRasters;
// sin(i°) × 16384 + 16384 for i from 0 to 359.
extern const std::array<uint16_t, 360> kSine;

}  // namespace stym::pluginui::demo
