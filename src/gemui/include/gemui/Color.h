// STYM2149 — gemui: colours (32-bit ARGB) and the prototype's palette (STYM2149_SPEC.md §6.2)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>

namespace stym::gemui {

using Color = uint32_t;  // 0xAARRGGBB; alpha 0: transparent (patterns)

constexpr Color rgb(uint32_t r, uint32_t g, uint32_t b) noexcept { return 0xFF000000u | r << 16 | g << 8 | b; }
constexpr uint32_t red(Color c) noexcept { return c >> 16 & 0xFF; }
constexpr uint32_t green(Color c) noexcept { return c >> 8 & 0xFF; }
constexpr uint32_t blue(Color c) noexcept { return c & 0xFF; }
// Any non-zero alpha counts as opaque: the framebuffer has no partial transparency, alpha 0 only marks a pattern cell
// or a picture pixel that is left out.
constexpr bool opaque(Color c) noexcept { return c >> 24 != 0; }

namespace detail {
// mix() for an alpha outside [0, 1] (or NaN), out of line: the opacity takes the nearest bound, NaN 0.
Color mixAnyAlpha(Color src, Color dst, double alpha) noexcept;
}

// Blend of a group at opacity `alpha` over what it covers: rounded to nearest per channel (measured in Chromium on
// the prototype's opacities, 0.25 to 0.5), floor(s × alpha + d × (1 − alpha) + 0.5). For alpha in [0, 1], the sum is
// positive and truncation gives the same integer as floor, without a library call. Outside [0, 1], the opacity takes
// the nearest bound.
inline Color mix(Color src, Color dst, double alpha) noexcept {
    if (!(alpha >= 0 && alpha <= 1)) return detail::mixAnyAlpha(src, dst, alpha);
    const double beta = 1 - alpha;
    const auto ch = [alpha, beta](uint32_t s, uint32_t d) {
        return uint32_t(double(s) * alpha + double(d) * beta + 0.5);
    };
    return rgb(ch(red(src), red(dst)), ch(green(src), green(dst)), ch(blue(src), blue(dst)));
}

// The prototype's palette: its CSS variables (--gem-green, --gem-white, --gem-black, --gem-grey, --led, --led-off)
// and colours written in its rules.
namespace colors {
inline constexpr Color kTransparent = 0;
inline constexpr Color kWhite = rgb(0xFF, 0xFF, 0xFF);
inline constexpr Color kBlack = rgb(0, 0, 0);
inline constexpr Color kGreen = rgb(0x00, 0xA8, 0x00);      // desktop, playhead, current choice
inline constexpr Color kDesktopDark = rgb(0x00, 0xA0, 0x00);  // desktop checkerboard
inline constexpr Color kGrey = rgb(0xAA, 0xAA, 0xAA);
inline constexpr Color kGreyDisabled = rgb(0x8A, 0x8A, 0x8A);  // greyed-out label
inline constexpr Color kHint = rgb(0x22, 0x22, 0x22);          // .hint
// Text of a <button disabled>: the browser's colour rgba(16, 16, 16, 0.3), laid over the button's white background.
inline constexpr Color kButtonTextOff = rgb(0xB7, 0xB7, 0xB7);
inline constexpr Color kLed = rgb(0xCC, 0x00, 0x00);
inline constexpr Color kLedOff = rgb(0x44, 0x00, 0x00);
inline constexpr Color kClip = rgb(0xB0, 0x00, 0x00);
inline constexpr Color kCurveGuide = rgb(0xC4, 0xC4, 0xC4);   // guides of the ADSR curve
inline constexpr Color kNopeStripe = rgb(0xC0, 0xC0, 0xC0);   // key out of range
inline constexpr Color kNopeStripeDark = rgb(0x58, 0x58, 0x58);
}  // namespace colors

}  // namespace stym::gemui
