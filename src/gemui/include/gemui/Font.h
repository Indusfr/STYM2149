// STYM2149 — gemui: GEM fonts (EmuTOS glyphs): 8×16 from 13 to 16 px, 8×8 and 6×6 for small texts
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// 8×16 font, metrics of the prototype: a line is the size of the font (ascent 13/16, descent 3/16), each character
// advances by half the size (8 px at 16 px), its origin placed at a quarter of a pixel as Skia does
// (tools/gemfont/README.md); each glyph keeps its place in the EmuTOS cell, which the prototype's TrueType lost
// (i and l stuck to the left: D-266). Small texts (12 px and less in the prototype): system fonts of the Atari ST's GEM
// (D-071), placed on the baseline of the prototype's line box: the 8×8 (8 px advance) where it fits (tooltips,
// alerts, key names, help line), the 6×6 (6 px advance, the AES's SMALL font) in the prototype's narrow spaces, on
// request (Face::kSmall). Outside the font, a few characters of the prototype take the corresponding GEM glyph
// (arrows, check mark, note, bullet; the em dash, the hyphen: D-127; ù, ², Ö, Ä, Ü, ß, ´ of the key legends in the
// keyboard drawing: D-131); the others become "?".
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "gemui/FontData.h"

namespace stym::gemui {

// Font of a text: the system font at its size (8×16, or 8×8 at 12 px and less), or the AES's SMALL font (6×6,
// whatever the size, which then only sets the line box).
enum class Face : uint8_t { kSystem, kSmall };

// Text metrics and glyph lookup over the generated tables of FontData.h; static functions only. Sizes are CSS pixels
// (the prototype's font-size), positions and widths are layout pixels (fractional for the 8×16 font).
class Font {
public:
    // Nearest available size (8 to 16 px).
    static const fontdata::Size& size(int px) noexcept;
    // Glyph sets of the 8×16 font: fontdata::kSizes, or another table of the same sizes (nullptr: back to kSizes).
    // gemshot draws the scenes of the prototype with the glyphs of its TrueType (D-266); not for the plugin.
    static void useSizes(const fontdata::Size* sizes) noexcept;
    // Glyph index of a code point ("?" if missing).
    static int glyph(char32_t c) noexcept;
    // Cell of the bitmap font of a small text: 8 (8×8), 6 (6×6), 0 for the 8×16 font at a quarter of a pixel.
    static int cell(int px, Face face = Face::kSystem) noexcept {
        return face == Face::kSmall ? 6 : px <= fontdata::k8x8MaxPx ? 8 : 0;
    }
    // Advance of a character of the GEM font: the cell of the 8×8 or 6×6 font, else the 8×16 advance of the size
    // (stored in 1/64 px).
    static double advance(int px, Face face = Face::kSystem) noexcept {
        const int c = cell(px, face);
        return c ? double(c) : size(px).adv64 / 64.0;
    }
    // Character of the GEM font (printable ASCII, glyphs E001-E008, non-breaking space); Chromium takes the others
    // from the fallback font (see lineBox). The accented letters of the translated tooltips (D-185), absent from the
    // prototype, keep the measurements of the GEM font.
    static bool native(char32_t c) noexcept {
        return (c >= 0x20 && c <= 0x7E) || (c >= 0xE001 && c <= 0xE008) || c == 0xA0 ||
               (c >= 0xA1 && glyph(c) >= fontdata::kLatinFirst);
    }
    // Advance of a character: half the size for the GEM font; whole advance of the fallback font (DejaVu Sans Mono,
    // 1233 units out of 2048, rounded to the pixel: 8 px at 14 px, 7 px at 12 px) for the others, in 8×16 as in 6×6
    // (font of the spaces where the prototype's widths matter: D-127); the cell for all of them in the 8×8 font.
    static double advance(char32_t c, int px, Face face = Face::kSystem) noexcept;
    // Ascent of the 8×16 metrics of the size, not rounded (lineBox rounds it).
    static double ascent(int px) noexcept { return size(px).asc64 / 64.0; }
    // `line-height: normal` line box of a text, as in Chromium: ascent and descent each rounded to the pixel. A
    // character absent from the GEM font (arrows, check mark, ♪, •, —) is taken by Chromium from the fallback font of
    // the prototype's stack (monospace: DejaVu Sans Mono on the screenshot machine, 1901 and 483 units out of 2048),
    // which is taller: the line grows (14 px → 16 px high, baseline 2 px lower). gemui draws these characters with
    // the corresponding GEM glyph, but keeps the line height of the screenshots. A small text keeps the line box of
    // its size (the 8×8 and 6×6 fonts fit in it): the vertical layout stays that of the prototype.
    struct LineBox {
        int ascent;
        int descent;
        int height() const noexcept { return ascent + descent; }
    };
    static LineBox lineBox(std::string_view utf8, int px, Face face = Face::kSystem) noexcept;
    // Rows of a glyph whose origin falls at `quarter`/4 of a pixel; *shift: columns to add (8×16 font).
    static const uint16_t* rows(int px, int index, int quarter, int* shift) noexcept;
    // Glyph to draw, whatever the font: row r at (x + shift, baseline + top + r), bit 15 of bits(r) = column 0. In
    // the 8×8 or 6×6 font, the quarter has no effect (cells on the pixel grid).
    struct Raster {
        const uint16_t* rows16 = nullptr;  // 8×16 font
        const uint8_t* rows8 = nullptr;    // 8×8 and 6×6 fonts
        int count = 0;
        int top = 0;
        int shift = 0;
        uint16_t bits(int r) const noexcept { return rows8 ? uint16_t(rows8[r] << 8) : rows16[r]; }
    };
    static Raster raster(int px, int index, int quarter, Face face = Face::kSystem) noexcept;
    // Number of characters of a UTF-8 text.
    static int length(std::string_view utf8) noexcept;
    // Width of a text: each character advances by advance(c) + letterSpacing (spacing after each letter, the last one
    // included, as in CSS).
    static double width(std::string_view utf8, int px, double letterSpacing = 0, Face face = Face::kSystem) noexcept;
};

// Position of a glyph as in Skia: origin rounded to the nearest quarter of a pixel, that is a whole column and a
// quarter (0 to 3).
struct GlyphPos {
    int x;
    int quarter;
};
GlyphPos glyphPos(double x) noexcept;

// Decodes the UTF-8 character at `pos` and advances `pos` (invalid sequence: U+FFFD, one byte consumed).
char32_t nextCodepoint(std::string_view s, size_t& pos) noexcept;

}  // namespace stym::gemui
