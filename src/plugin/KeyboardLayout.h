// STYM2149 — plugin: layout of the system keyboard, for Auto in Settings > Keyboard layout (D-215)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The prototype read the characters of the physical keys (navigator.keyboard.getLayoutMap). The editor asks the
// system which character the keys at the places of Q and Y of a QWERTY keyboard carry in the current layout: A at the
// place of Q is AZERTY, Z at the place of Y is QWERTZ, anything else counts as QWERTY.
#pragma once
#include <string>

namespace stym::plugin {

// "qwerty", "azerty" or "qwertz".
std::string systemKeyboardLayout();
// The layout named by the characters at the places of Q and Y (lower or upper case; 0: unknown).
std::string layoutOfKeys(char32_t atQ, char32_t atY);

}  // namespace stym::plugin
