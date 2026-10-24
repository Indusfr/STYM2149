#!/usr/bin/env python3
# STYM2149 — application icon as pixels for the editor: src/pluginui/src/AppIcon.cpp from the 32 px drawing (D-235).
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).
#
# Usage: python3 tools/icon/make_icon_data.py [--check]
# Reads packaging/icon/STYM2149-32.png (the icon chosen by Indus, D-218) and writes its 32 × 32 pixels as 0xAARRGGBB
# colours (alpha 0: transparent), which the Information box draws (Fuji > Information). With --check, nothing is
# written and the exit status says whether the file is up to date. Standard library only.
import os
import sys

from make_ico import ICON, read_png

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(HERE, '..', '..', 'src', 'pluginui', 'src', 'AppIcon.cpp')

HEADER = '''// STYM2149 — pluginui: the application icon (D-218) as pixels (file written by tools/icon/make_icon_data.py)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Do not edit by hand: run `python3 tools/icon/make_icon_data.py` again (it reads packaging/icon/STYM2149-32.png).
#include "pluginui/AppIcon.h"

namespace stym::pluginui::icons {
namespace {

'''


def build():
    w, h, rows = read_png(os.path.join(ICON, 'STYM2149-32.png'))
    if (w, h) != (32, 32):
        raise ValueError(f'STYM2149-32.png: {w} × {h}, 32 × 32 expected')
    out = HEADER + f'constexpr gemui::Color kPixels[{w * h}] = {{\n'
    for r in rows:
        cols = []
        for i in range(0, 4 * w, 4):
            red, green, blue, alpha = r[i], r[i + 1], r[i + 2], r[i + 3]
            cols.append('0x00000000' if alpha == 0 else f'0x{alpha:02x}{red:02x}{green:02x}{blue:02x}')
        for k in range(0, len(cols), 8):
            out += '    ' + ', '.join(cols[k:k + 8]) + ',\n'
    out += '};\n\n}  // namespace\n\n'
    out += f'const AppIcon kApp{{{w}, {h}, kPixels}};\n\n}}  // namespace stym::pluginui::icons\n'
    return out


def main():
    text, target = build(), os.path.normpath(TARGET)
    if '--check' in sys.argv[1:]:
        with open(target, encoding='utf-8') as f:
            same = f.read() == text
        print('AppIcon.cpp is up to date' if same else 'AppIcon.cpp differs: run tools/icon/make_icon_data.py')
        return 0 if same else 1
    with open(target, 'w', encoding='utf-8') as f:
        f.write(text)
    print(f'{len(text)} bytes written to src/pluginui/src/AppIcon.cpp')
    return 0


if __name__ == '__main__':
    sys.exit(main())
