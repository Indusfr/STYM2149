#!/usr/bin/env python3
# STYM2149 — icons of the GEM desktop: src/pluginui/src/DeskIcons.cpp, from the EmuTOS desktop's resource.
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).
#
# Usage: python3 tools/icon/make_desk_icons.py icon.rsc [--check]
# Indus, 25/10/2026 16:09: "Pour être compatible avec licences AGPLv3, peux-tu remplacer les icones des disques et de
# la corbeille par celle d'EmuTOS ?" icon.rsc is the resource of the EmuDesk desktop, desk/icon.rsc of EmuTOS (GPL;
# commit 02c2db75fc875265c1449efea014450466cf20b7, that of the fonts), not kept in the repository (Indus, 16:47); it
# holds 14 ICONBLKs of 32 × 32. This script finds the "floppy disk" and the "trash" by their text and writes their data
# and masks, rows of '#', checking the drive letter's place (ib_xchar, ib_ychar) against DeskIcons.h. They replace the
# TOS 1.04 pictures used before, which were Atari's. With --check, nothing is written and the exit status says whether the
# file is up to date. Standard library only.
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TARGET = os.path.join(ROOT, 'src', 'pluginui', 'src', 'DeskIcons.cpp')
FLOPPY, TRASH = b'floppy disk', b'trash'  # texts of the two ICONBLKs
SIDE = 32

HEADER = '''// STYM2149 — pluginui: the icons of the GEM desktop (floppy disk, trash), from EmuTOS
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Do not edit by hand: run `python3 tools/icon/make_desk_icons.py icon.rsc` again. The pictures and their masks are
// those of the EmuTOS desktop (© The EmuTOS development team, GPL; THIRD_PARTY.md), read from its desk/icon.rsc at
// Indus's request for the AGPLv3 (25/10/2026 16:09): {source}.
#include "pluginui/DeskIcons.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace stym::pluginui::icons {{
namespace {{

constexpr int kSide = 32;
using Rows = std::array<const char*, kSide>;
using Bits = std::array<uint8_t, kSide * kSide / 8>;  // 1 bit per pixel, 4 bytes per row; built at compile time

// One row of characters per pixel row, '#' lit (bit 7 = column 0, as gemui::Bitmap reads it).
constexpr Bits pack(const Rows& rows) {{
    Bits b{{}};
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x)
            if (rows[size_t(y)][x] == '#') b[size_t(y * kSide / 8 + x / 8)] |= uint8_t(0x80 >> (x % 8));
    return b;
}}

'''

FOOTER = '''}  // namespace

const gemui::Bitmap kFloppy{kSide, kSide, kFloppyBits.data()};
const gemui::Bitmap kFloppyMask{kSide, kSide, kFloppyMaskBits.data()};
const gemui::Bitmap kTrash{kSide, kSide, kTrashBits.data()};
const gemui::Bitmap kTrashMask{kSide, kSide, kTrashMaskBits.data()};

}  // namespace stym::pluginui::icons
'''


def icons(rsc):
    """{text: (index, data rows, mask rows, (ib_xchar, ib_ychar))} of the resource's 32 × 32 ICONBLKs."""
    hdr = struct.unpack('>18H', rsc[:36])
    table, count = hdr[3], hdr[13]  # rsh_iconblk, rsh_nib; the pointers are offsets in the file
    found = {}
    for k in range(count):
        o = table + 34 * k
        pmask, pdata, ptext = struct.unpack('>III', rsc[o:o + 12])
        xchar, ychar = struct.unpack('>hh', rsc[o + 14:o + 18])
        w, h = struct.unpack('>hh', rsc[o + 22:o + 26])
        if (w, h) == (SIDE, SIDE):
            found[rsc[ptext:ptext + 16].split(b'\0')[0]] = (k, rows(rsc, pdata), rows(rsc, pmask), (xchar, ychar))
    return found


def rows(rsc, at):
    """The 32 rows of a 32 × 32 picture at offset at, one long word per row, bit 31 = column 0."""
    words = [int.from_bytes(rsc[at + 4 * r:at + 4 * r + 4], 'big') for r in range(SIDE)]
    return [''.join('#' if word >> (SIDE - 1 - c) & 1 else '.' for c in range(SIDE)) for word in words]


def block(name, comment, rows):
    body = ''.join(f'    "{r}",\n' for r in rows)
    return f'{comment}\nconstexpr Bits {name} = pack({{\n{body}}});\n'


def main():
    args = [a for a in sys.argv[1:] if a != '--check']
    if len(args) != 1:
        sys.exit('usage: make_desk_icons.py icon.rsc [--check]')
    path = args[0]
    rsc = open(path, 'rb').read()
    found = icons(rsc)
    for name in (FLOPPY, TRASH):
        if name not in found:
            sys.exit(f'no 32 x 32 "{name.decode()}" icon in {path}')
    fk, floppy, floppy_mask, letter = found[FLOPPY]
    tk, trash, trash_mask, _ = found[TRASH]
    digest = hashlib.sha256(rsc).hexdigest()
    text = HEADER.format(source=f'SHA-256 {digest[:16]}…, ICONBLKs {fk} and {tk}')
    text += block('kFloppyBits',
                  '// The drive (floppy disk), a 3.5" disk; the drive letter goes on its label (DeskIcons.h).', floppy)
    text += block('kFloppyMaskBits',
                  '// Its mask, painted white under the data so that the icon hides the desktop pattern.', floppy_mask)
    text += '\n' + block('kTrashBits', '// The trash.', trash)
    text += block('kTrashMaskBits', '// Its mask.', trash_mask)
    text += '\n' + FOOTER
    header = open(os.path.join(ROOT, 'src', 'pluginui', 'include', 'pluginui', 'DeskIcons.h')).read()
    want = f'kFloppyLetterX = {letter[0]}, kFloppyLetterY = {letter[1]};'
    if want not in header:
        sys.exit(f'DeskIcons.h should read: {want}')
    if '--check' in sys.argv:
        current = open(TARGET, encoding='utf-8').read() if os.path.exists(TARGET) else ''
        if current != text:
            sys.exit('DeskIcons.cpp is out of date')
        print('DeskIcons.cpp is up to date')
        return
    with open(TARGET, 'w', encoding='utf-8') as f:
        f.write(text)
    print(f'wrote {os.path.relpath(TARGET)} (ICONBLKs {fk} and {tk} of {path})')


if __name__ == '__main__':
    main()
