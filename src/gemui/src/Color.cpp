// STYM2149 — gemui: colours
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Color.h"

#include <algorithm>

namespace stym::gemui {

Color detail::mixAnyAlpha(Color src, Color dst, double alpha) noexcept {
    // An opacity outside [0, 1] takes the nearest bound, NaN 0: each channel stays within 0 to 255.
    return mix(src, dst, alpha > 0 ? std::min(alpha, 1.0) : 0.0);
}

}  // namespace stym::gemui
