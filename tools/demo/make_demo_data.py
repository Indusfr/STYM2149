#!/usr/bin/env python3
# STYM2149 — pictures and colours of the Information box's demo: src/pluginui/src/DemoData.cpp, from ST BLIX.
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).
#
# Usage: python3 tools/demo/make_demo_data.py STBLIX_FOLDER [--check]
# Indus, 29/10/2026 04:20: « Pour la demo dans la fenêtre d'info, reprend de ST BLIX, le principe du fond qui bouge
# avec le dessin de la caisse en bois, le logo PRISM/STY2149 avec le même principe d'ondulation et d'échange de logo
# ainsi que les mêmes raster pour replacer les partie noire de la police et du logo » ("For the demo in the
# Information window, take from ST BLIX the moving background with the wooden crate, the PRISM/STYM2149 logo with the
# same undulation and logo swap, and the same rasters in place of the black parts of the font and the logo"). 04:25:
# the ST BLIX folder (STBLIX6.FIX, Indus / PRISM), not kept in the repository, like EmuTOS's icon.rsc. Read from it:
# - fontes5.neo: the 32 × 32 font (NEOchrome, 10 glyphs a row: A-Z, 0-9, . ! ? "), its face in colour 15, the one the
#   rasters replace, and its palette;
# - PRISMTIT.4P and PRISMTIT.MSK: the PRISM logo, 4 planes and its mask, 208 × 29;
# - SPR.4P: the 32 × 32 background tiles, the wooden crate first;
# - STBLIX.S: the crate's palette (pal_base2), the 8 palettes of the logo (title_pal1-8) and the rasters (pal_title,
#   512 lines of colour 15, a gradient of 32 lines each);
# - SINTAB.BIN: the sine table of the logo's waves, sin(i°) × 16384 + 16384.
# Made here:
# - the STYM2149 logo, as ST BLIX's own logo was made: the font's glyphs without their 3 blank top rows, 32 px apart,
#   colours 4 to 7 moved to the logo's 5, 6, 7 and 9 (checked: the font gives the S, T, I and X of TITLE.4P's
#   "ST BLIX" again);
# - a comma (the font has none): the period raised, its face going on lower, leaning to the left.
# With --check, nothing is written and the exit status says whether the file is up to date. Standard library only.
import hashlib
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TARGET = os.path.join(ROOT, 'src', 'pluginui', 'src', 'DemoData.cpp')

FONT_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.!?"'  # fontes5.neo, 10 cells of 32 × 32 a row
FONT_CLEAR = 14     # the font's background colour: transparent
LOGO_TOP = 3        # glyph rows above the letters, left out of the logos
LOGO_COLOURS = {1: 1, 2: 2, 3: 3, 4: 5, 5: 6, 6: 7, 7: 9, 15: 15}  # font colour -> logo colour (TITLE.4P)
DIGITS = '0123456789ABCDEF'

HEADER = '''// STYM2149 — pluginui: pictures and colours of the Information box's demo, from ST BLIX
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Do not edit by hand: run `python3 tools/demo/make_demo_data.py STBLIX_FOLDER` again. The pictures, palettes,
// rasters and sine table are those of ST BLIX (Indus / PRISM), given by Indus for this demo on 29/10/2026: {source}.
// A row is a string of colour indices, '.' for a transparent pixel; colours are ST/STE palette words.
#include "pluginui/DemoData.h"

namespace stym::pluginui::demo {{
namespace {{

'''


def planar(data, width, height):
    """Colour indices of a 4-plane ST picture, rows of width pixels (width a multiple of 16)."""
    groups = width // 16
    out = []
    for y in range(height):
        row = []
        for g in range(groups):
            off = (y * groups + g) * 8
            w = struct.unpack('>4H', data[off:off + 8])
            for b in range(15, -1, -1):
                row.append(sum(((w[p] >> b) & 1) << p for p in range(4)))
        out.append(row)
    return out


def mask_rows(data, width, height):
    groups = width // 16
    out = []
    for y in range(height):
        row = []
        for g in range(groups):
            w = struct.unpack('>H', data[(y * groups + g) * 2:(y * groups + g) * 2 + 2])[0]
            row.extend((w >> b) & 1 for b in range(15, -1, -1))
        out.append(row)
    return out


def source_words(source, label, count):
    """count dc.w values from the line of `label` in an assembler source."""
    lines = source.replace('\r', '').split('\n')
    for i, line in enumerate(lines):
        if re.match(rf'{label}\s', line):
            values = []
            for l in lines[i:]:
                m = re.search(r'dc\.w\s+(.*)', l)
                if not m:
                    break
                values += [int(v.strip().lstrip('$'), 16) for v in m.group(1).split(';')[0].split(',')]
                if len(values) >= count:
                    return values[:count]
            sys.exit(f'{label}: {len(values)} values, {count} expected')
    sys.exit(f'no {label} in STBLIX.S')


def glyphs(font):
    """{character: 32 rows of colour indices, -1 transparent}."""
    out = {}
    for i, ch in enumerate(FONT_CHARS):
        cx, cy = (i % 10) * 32, (i // 10) * 32
        out[ch] = [[-1 if font[cy + y][cx + x] == FONT_CLEAR else font[cy + y][cx + x] for x in range(32)]
                   for y in range(32)]
    return out


def comma(period):
    """The period raised by 4 rows, its face going on 4 rows lower as a tail, one pixel more to the left and two
    narrower each row, with the period's bevels."""
    rows = [[-1] * 32 for _ in range(32)]
    top = next(y for y in range(32) if any(c >= 0 for c in period[y]))  # 24: two bevel rows, four of face, two
    for y in range(6):  # top bevel and face
        rows[top - 4 + y] = list(period[top + y])
    left, right = 11, 20  # the period's face
    for k in range(1, 5):  # the tail: the face k pixels to the left and 2k narrower, between the same bevels
        src = period[top + 1 + k]
        r = [-1] * 32
        r[left - k - 2:left - k] = src[left - 2:left]
        r[left - k:right - 2 * k + 1] = [15] * (right - left - k + 1)
        r[right - 2 * k + 1:right - 2 * k + 3] = src[right + 1:right + 3]
        rows[top + 1 + k] = r
    for k in range(2):  # bottom bevel, under the tail's last row
        src = period[top + 6 + k]
        rows[top + 6 + k][left - 6:left + 2] = src[left - 2:left] + src[right - 3:right + 3]
    return rows


def logo(glyph, text):
    """Glyphs of text without their top rows, 32 px apart (a space 16 px), in the logo's colours."""
    width = sum(16 if ch == ' ' else 32 for ch in text) - 1
    rows = [[-1] * width for _ in range(32 - LOGO_TOP)]
    x0 = 0
    for ch in text:
        if ch == ' ':
            x0 += 16
            continue
        for y in range(32 - LOGO_TOP):
            for x in range(31):
                c = glyph[ch][y + LOGO_TOP][x]
                if c >= 0:
                    rows[y][x0 + x] = LOGO_COLOURS[c]
        x0 += 32
    return rows


def text_rows(rows):
    return [''.join('.' if c < 0 else DIGITS[c] for c in r) for r in rows]


def picture(name, comment, rows):
    body = ''.join(f'    "{r}",\n' for r in text_rows(rows))
    return f'{comment}\nconst char* const {name}[] = {{\n{body}}};\n'


def words(name, comment, values, per_line=8):
    lines = [', '.join(f'0x{v:03x}' for v in values[i:i + per_line]) for i in range(0, len(values), per_line)]
    body = ''.join(f'    {l},\n' for l in lines)
    return f'{comment}\nconst std::array<uint16_t, {len(values)}> {name} = {{{{\n{body}}}}};\n'


def main():
    args = [a for a in sys.argv[1:] if a != '--check']
    if len(args) != 1:
        sys.exit('usage: make_demo_data.py STBLIX_FOLDER [--check]')
    folder = args[0]

    def read(name):
        for f in os.listdir(folder):
            if f.lower() == name.lower():
                return open(os.path.join(folder, f), 'rb').read()
        sys.exit(f'no {name} in {folder}')

    neo = read('fontes5.neo')
    font_palette = list(struct.unpack('>16H', neo[4:36]))
    glyph = glyphs(planar(neo[128:], 320, 200))
    glyph[','] = comma(glyph['.'])

    # ST BLIX's logo, made from the font as the STYM2149 logo is: the check that the recipe is the same. Its S, T, I
    # and X have the font's shapes and faces (their bevels are shaded a little differently); its B and L were drawn
    # otherwise.
    title = planar(read('TITLE.4P'), 208, 29)
    made = logo(glyph, 'ST BLIX')
    kind = lambda c: 'clear' if c <= 0 else 'face' if c == 15 else 'bevel'
    for x0 in (0, 32, 144, 176):
        for y in range(29):
            for x in range(x0, x0 + 31):
                if kind(title[y][x]) != kind(made[y][x]):
                    sys.exit(f'the font does not give the letters of TITLE.4P again at ({x}, {y})')
    stym = logo(glyph, 'STYM2149')

    prism_pix = planar(read('PRISMTIT.4P'), 208, 29)
    prism_mask = mask_rows(read('PRISMTIT.MSK'), 208, 29)
    prism = [[prism_pix[y][x] if prism_mask[y][x] else -1 for x in range(207)] for y in range(29)]

    tiles = read('SPR.4P')
    crate = planar(tiles[:512], 32, 32)

    source = read('STBLIX.S').decode('latin-1')
    wood = source_words(source, 'pal_base2', 16)
    logo_palettes = [source_words(source, f'title_pal{i}', 16) for i in range(1, 9)]
    rasters = source_words(source, 'pal_title', 512)
    sine_bin = read('SINTAB.BIN')
    sine = list(struct.unpack('>360H', sine_bin[:720]))

    digest = hashlib.sha256(b''.join(read(n) for n in ('fontes5.neo', 'PRISMTIT.4P', 'PRISMTIT.MSK', 'SPR.4P',
                                                        'STBLIX.S', 'SINTAB.BIN'))).hexdigest()
    text = HEADER.format(source=f'SHA-256 {digest[:16]}… of fontes5.neo, PRISMTIT.4P/.MSK, SPR.4P, STBLIX.S and '
                                'SINTAB.BIN')
    text += picture('kCrateRows', '// The wooden crate, ST BLIX\'s first background tile (SPR.4P), in kWoodPalette.',
                    crate) + '\n'
    text += picture('kPrismRows', '// The PRISM logo (PRISMTIT.4P, its mask PRISMTIT.MSK), in a logo palette.',
                    prism) + '\n'
    text += picture('kStymRows', '// The STYM2149 logo, from the font\'s glyphs, in a logo palette.', stym) + '\n'
    chars = FONT_CHARS + ','
    for i, ch in enumerate(chars):
        what = 'made from the period' if ch == ',' else 'fontes5.neo'
        text += picture(f'kGlyph{i}Rows', f'// \'{ch}\' ({what}), in kFontPalette.', glyph[ch]) + '\n'
    text += '}  // namespace\n\n'
    text += 'const Picture kCrate{32, 32, kCrateRows};\n'
    text += 'const Picture kPrismLogo{207, 29, kPrismRows};\n'
    text += f'const Picture kStymLogo{{{len(stym[0])}, {len(stym)}, kStymRows}};\n\n'
    text += f'const std::string_view kFontChars = R"({chars})";\n'
    text += f'const std::array<Picture, {len(chars)}> kFont = {{{{\n'
    text += ''.join(f'    {{32, 32, kGlyph{i}Rows}},\n' for i in range(len(chars)))
    text += '}};\n\n'
    text += words('kWoodPalette', '// The crate\'s colours (pal_base2 of STBLIX.S).', wood) + '\n'
    text += words('kFontPalette', '// The font\'s colours (fontes5.neo); 15, its face, takes the rasters.',
                  font_palette) + '\n'
    text += '// The logo\'s 8 palettes (title_pal1 to title_pal8 of STBLIX.S), one drawn at random each time a logo\n'
    text += '// comes down; 15 takes the rasters.\n'
    text += 'const std::array<std::array<uint16_t, 16>, 8> kLogoPalettes = {{\n'
    for p in logo_palettes:
        text += '    {{' + ', '.join(f'0x{v:03x}' for v in p) + '}},\n'
    text += '}};\n\n'
    text += words('kRasters', '// Colour 15 of each line (pal_title of STBLIX.S): 16 gradients of 32 lines.', rasters)
    text += '\n' + words('kSine', '// sin(i°) × 16384 + 16384 for i from 0 to 359 (SINTAB.BIN).', sine, 10)
    text += '\n}  // namespace stym::pluginui::demo\n'

    if '--check' in sys.argv:
        current = open(TARGET, encoding='utf-8').read() if os.path.exists(TARGET) else ''
        if current != text:
            sys.exit(f'{TARGET} is not up to date')
        return
    with open(TARGET, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(f'written {TARGET}')


if __name__ == '__main__':
    main()
