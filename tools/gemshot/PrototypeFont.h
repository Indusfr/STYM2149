// STYM2149 — gemshot: GEM 8×16 font as the prototype's TrueType draws it (tools/gemshot/PrototypeFont.cpp)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The TrueType starts the ink of every glyph at the origin, where gemui keeps EmuTOS's place in the cell (D-266):
// the scenes compared with the prototype's screenshots are drawn with these glyphs (gemui::Font::useSizes), the
// STYM2149 scenes with gemui's.
#pragma once
#include "gemui/FontData.h"

namespace stym::gemui::fontdata::prototype {

extern const Size kSizes[kSizeCount];

}  // namespace stym::gemui::fontdata::prototype
