// STYM2149 — gemui: GEM font
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Font.h"

#include <algorithm>
#include <cmath>

namespace stym::gemui {

GlyphPos glyphPos(double x) noexcept {
    // Rounding to the nearest quarter: add half a quarter, then the integer part is the column and the fraction, in
    // quarters, the quarter (min() guards against a fraction that rounds to 4/4).
    const double p = x + 0.5 / fontdata::kQuarters;
    const double f = std::floor(p);
    return {int(f), std::min(fontdata::kQuarters - 1, int((p - f) * fontdata::kQuarters))};
}

// The lead byte gives the length of the sequence (110xxxxx: 2 bytes, 1110xxxx: 3, 11110xxx: 4) and its first bits;
// each continuation byte (10xxxxxx) adds 6 bits. Overlong forms and surrogates are not rejected: they decode to the
// value they spell, and a character the font lacks draws as "?".
char32_t nextCodepoint(std::string_view s, size_t& pos) noexcept {
    const auto b = [&](size_t i) { return uint32_t(uint8_t(s[i])); };
    const uint32_t c0 = b(pos);
    int extra = 0;
    uint32_t cp = 0;
    if (c0 < 0x80) {
        ++pos;
        return c0;
    }
    if ((c0 & 0xE0) == 0xC0) extra = 1, cp = c0 & 0x1F;
    else if ((c0 & 0xF0) == 0xE0) extra = 2, cp = c0 & 0x0F;
    else if ((c0 & 0xF8) == 0xF0) extra = 3, cp = c0 & 0x07;
    else {
        ++pos;
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; ++i) {
        if (pos + size_t(i) >= s.size() || (b(pos + size_t(i)) & 0xC0) != 0x80) {
            ++pos;
            return 0xFFFD;
        }
        cp = cp << 6 | (b(pos + size_t(i)) & 0x3F);
    }
    pos += size_t(extra) + 1;
    return cp;
}

namespace {
// Glyph sets of the 8×16 font in use (useSizes); a process-wide setting, changed only by gemshot.
const fontdata::Size* gSizes = fontdata::kSizes;
}  // namespace

void Font::useSizes(const fontdata::Size* sizes) noexcept { gSizes = sizes ? sizes : fontdata::kSizes; }

const fontdata::Size& Font::size(int px) noexcept {
    // The table holds 8 to 16 px in order; other sizes take the nearest end.
    const int i = px < 8 ? 0 : px > 16 ? 8 : px - 8;
    return gSizes[i];
}

const uint16_t* Font::rows(int px, int index, int quarter, int* shift) noexcept {
    const fontdata::Size& f = size(px);
    *shift = f.shift[quarter];
    return f.bits[quarter] + size_t(index) * size_t(f.rows);
}

Font::Raster Font::raster(int px, int index, int quarter, Face face) noexcept {
    Raster r;
    switch (cell(px, face)) {
    case 6:
        r.rows8 = fontdata::k6x6[index];
        r.count = fontdata::k6x6Rows;
        r.top = fontdata::k6x6Top;
        return r;
    case 8:
        r.rows8 = fontdata::k8x8[index];
        r.count = fontdata::k8x8Rows;
        r.top = fontdata::k8x8Top;
        return r;
    default: break;
    }
    r.rows16 = rows(px, index, quarter, &r.shift);
    r.count = size(px).rows;
    r.top = size(px).top;
    return r;
}

Font::LineBox Font::lineBox(std::string_view utf8, int px, Face face) noexcept {
    const fontdata::Size& f = size(px);
    LineBox box{int(std::floor(f.asc64 / 64.0 + 0.5)), int(std::floor(f.desc64 / 64.0 + 0.5))};
    if (cell(px, face)) return box;
    // One character outside the GEM font is enough: the line also takes the fallback font's ascent and descent
    // (1901 and 483 units of its 2048-unit em, see the header), each rounded, where they are larger.
    for (size_t pos = 0; pos < utf8.size();) {
        const char32_t c = nextCodepoint(utf8, pos);
        if (!native(c)) {
            box.ascent = std::max(box.ascent, int(std::floor(px * 1901 / 2048.0 + 0.5)));
            box.descent = std::max(box.descent, int(std::floor(px * 483 / 2048.0 + 0.5)));
            break;
        }
    }
    return box;
}

// Index layout of the glyph tables: FontData.h (ASCII from 0, the GEM glyphs U+E001-U+E008 from 95, then the
// fallback characters from kFallbackFirst).
int Font::glyph(char32_t c) noexcept {
    if (c >= 0x20 && c <= 0x7E) return int(c - 0x20);
    if (c >= 0xE001 && c <= 0xE008) return 95 + int(c - 0xE001);
    switch (c) {
    case 0x00A0: return 0;          // non-breaking space: space
    case 0x2191: return 95;         // ↑
    case 0x2193: return 96;         // ↓
    case 0x2192: return 97;         // →
    case 0x2190: return 98;         // ←
    case 0x2713: return 102;        // ✓ (GEM check mark)
    case 0x2014: return '-' - 0x20; // — (the Atari ST encoding has no em dash: hyphen)
    default: break;
    }
    // Fallback font (FontData.h): ♪, •, keyboard key legends, accented letters of the tooltips.
    for (int i = 0; i < fontdata::kFallbackCount; ++i)
        if (fontdata::kFallbackCodes[i] == c) return fontdata::kFallbackFirst + i;
    return '?' - 0x20;
}

int Font::length(std::string_view utf8) noexcept {
    int n = 0;
    for (size_t pos = 0; pos < utf8.size(); ++n) nextCodepoint(utf8, pos);
    return n;
}

// 1233 / 2048: advance of the fallback font (DejaVu Sans Mono), rounded to the pixel (see the header).
double Font::advance(char32_t c, int px, Face face) noexcept {
    return native(c) || cell(px, face) == 8 ? advance(px, face) : std::floor(px * 1233 / 2048.0 + 0.5);
}

double Font::width(std::string_view utf8, int px, double letterSpacing, Face face) noexcept {
    double w = 0;
    for (size_t pos = 0; pos < utf8.size();) w += advance(nextCodepoint(utf8, pos), px, face) + letterSpacing;
    return w;
}

}  // namespace stym::gemui
