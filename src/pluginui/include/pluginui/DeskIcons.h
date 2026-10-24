// STYM2149 — pluginui: pictures of the GEM desktop's icons (floppy disk, trash), drawn for STYM2149
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Ninth DAW test, item 102 (D-303): the floppy disks A and B and the trash of a GEM desktop, decorative. 32 × 32
// pictures drawn after the style of the Atari ST's desktop icons (not copied from TOS or EmuTOS): the data in black,
// the mask in white under it. MainWindow gives them to its gemui::DeskIcon widgets (setPicture, setLetter).
#pragma once

#include "gemui/Painter.h"

namespace stym::pluginui::icons {

extern const gemui::Bitmap kFloppy, kFloppyMask;  // the drive letter goes in the label (kFloppyLetterX, Y)
extern const gemui::Bitmap kTrash, kTrashMask;
// Top-left corner of the letter's 8×8 cell in the floppy picture, inside the label (DeskIcon::setLetter).
inline constexpr int kFloppyLetterX = 12, kFloppyLetterY = 19;

}  // namespace stym::pluginui::icons
