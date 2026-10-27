// STYM2149 — pluginui: pictures of the GEM desktop's icons (floppy disk, trash), from EmuTOS
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Ninth DAW test, item 102: the floppy disks A and B and the trash of a GEM desktop, decorative. The 32 × 32
// pictures of the EmuTOS desktop (© The EmuTOS development team, GPL), which replace those of TOS 1.04 for the AGPLv3
// at Indus's request (25/10/2026 16:09; DeskIcons.cpp is written by tools/icon/make_desk_icons.py from EmuTOS's
// desk/icon.rsc): the data in black, the mask in white under it. MainWindow gives them to its gemui::DeskIcon widgets
// (setPicture, setLetter).
#pragma once

#include "gemui/Painter.h"

namespace stym::pluginui::icons {

extern const gemui::Bitmap kFloppy, kFloppyMask;  // the drive letter goes on the label (kFloppyLetterX, Y)
extern const gemui::Bitmap kTrash, kTrashMask;
// Top-left corner of the drive letter's 6×6 cell in the floppy picture, on the disk's label: ib_xchar and ib_ychar of
// the EmuTOS icon (DeskIcon::setLetter).
inline constexpr int kFloppyLetterX = 13, kFloppyLetterY = 22;

}  // namespace stym::pluginui::icons
