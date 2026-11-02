// STYM2149 — gemui: how the GEM elements are drawn, as on the Atari ST or as in the prototype
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The elements follow the GEM of the Atari ST, measured on Indus's screenshots of Devpac 3 (high resolution, 1 November
// 2026): menus in the 8×16 font with dotted separators, pop-ups with a shadow and the current choice
// over their button, outlined dialog boxes, greyed text dotted every other pixel. Indus then asked for 2 px borders on
// the buttons (3 px for the default one) and on the menus; the menu bar keeps its 1 px rule. The
// prototype drew them otherwise (2 px rules, grey at partial opacity, its own menus): gemshot keeps the prototype's
// look for the scenes compared with its screenshots, as it keeps its glyphs.
#pragma once

namespace stym::gemui {

// True while the prototype's look is in use; a process-wide setting, changed only by gemshot.
bool prototypeLook() noexcept;
void usePrototypeLook(bool on) noexcept;

// Desktop pixel kept by a GEM dotted grey (the AES's light text and its greyed objects): one pixel in two, as a
// checkerboard fixed on the screen.
constexpr bool ditherKeeps(int x, int y) noexcept { return ((x + y) & 1) != 0; }

}  // namespace stym::gemui
