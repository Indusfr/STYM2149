#!/usr/bin/env python3
# STYM2149 — Atari logo of the menu bar: src/pluginui/src/FujiData.cpp, its outline and its bits (D-404).
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).
#
# Usage: python3 tools/icon/make_fuji_data.py [--check]
# The logo is the outline of the Atari logo of Simple Icons (icons/atari.svg, 24 × 24 box, CC0 1.0), fitted to the
# 17 × 14 pixels of the menu bar (24/10/2026, Indus: "plus net ... et ressemble au vrai logo de l'Atari ST"):
# - 14 / 19.307 pixel per unit, the height of the logo (its curves undistorted);
# - the centre bar 2.1 pixels wide and the gaps beside it 1 pixel (0.44 at that scale), so that the bar keeps three
#   pixels and the gaps one at scale 1; the side prongs moved towards the edges by as much;
# - the side prongs stretched to reach 14.3 pixels, then cut at the edges of the box (x 0 and 17, y 14), so that their
#   feet keep two pixels on the bottom row at scale 1.
# The bits are the raster of the outline at scale 1 (a pixel lit when its centre lies inside), which the framebuffer
# draws; the editor fills the outline at its own resolution (JucePainter::bitmap). With --check, nothing is written
# and the exit status says whether the file is up to date. Standard library only.
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(HERE, '..', '..', 'src', 'pluginui', 'src', 'FujiData.cpp')

HEADER = '''// STYM2149 — pluginui: the Atari logo of the menu bar (file written by tools/icon/make_fuji_data.py, D-404)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Do not edit by hand: run `python3 tools/icon/make_fuji_data.py` again. The outline of the Atari logo of Simple Icons
// (CC0 1.0) fitted to 17 × 14 pixels; the bits are its raster at scale 1, a pixel lit when its centre lies inside.
#include "pluginui/IconData.h"

namespace stym::pluginui::icons {
namespace {

'''

W, H = 17, 14
TOP, BOTTOM = 2.346, 21.653  # top and bottom of the logo in the 24 × 24 box
SCALE = H / (BOTTOM - TOP)
CENTRE_HALF = 1.05  # half width of the centre bar, pixels
PRONG_INNER = 6.45  # x of the inner edge of the left prong at the top, pixels
PRONG_REACH = 14.3  # height of the side prongs before the cut, pixels
SEGMENTS = 10       # straight segments per cubic curve


def cubic(p0, p1, p2, p3):
    out = []
    for i in range(1, SEGMENTS + 1):
        t = i / SEGMENTS
        u = 1 - t
        out.append(tuple(u * u * u * p0[k] + 3 * u * u * t * p1[k] + 3 * u * t * t * p2[k] + t * t * t * p3[k]
                         for k in (0, 1)))
    return out


def left_prong():
    # Path of Simple Icons, absolute coordinates: M0 21.653 s3.154-.355 5.612-2.384 c2.339-1.93 3.185-3.592 3.77-5.476
    # .584-1.885.671-6.419.671-7.764 V2.346 H8.598 v1.365 c-.024 2.041-.2 5.918-1.135 8.444
    # C5.203 18.242 0 18.775 0 18.775 z
    pts = [(0.0, 21.653)]
    pts += cubic((0.0, 21.653), (0.0, 21.653), (3.154, 21.298), (5.612, 19.269))
    pts += cubic((5.612, 19.269), (7.951, 17.339), (8.797, 15.677), (9.382, 13.793))
    pts += cubic((9.382, 13.793), (9.966, 11.908), (10.053, 7.374), (10.053, 6.029))
    pts += [(10.053, 2.346), (8.598, 2.346), (8.598, 3.711)]
    pts += cubic((8.598, 3.711), (8.574, 5.752), (8.398, 9.629), (7.463, 12.155))
    pts += cubic((7.463, 12.155), (5.203, 18.242), (0.0, 18.775), (0.0, 18.775))
    return pts


def clip(poly, axis, limit, keep_above):
    # Sutherland-Hodgman against the line coordinate[axis] = limit.
    def keep(p):
        return p[axis] >= limit if keep_above else p[axis] <= limit
    out = []
    for i, b in enumerate(poly):
        a = poly[i - 1]
        if keep(a) != keep(b):
            t = (limit - a[axis]) / (b[axis] - a[axis])
            out.append(tuple(limit if k == axis else a[k] + t * (b[k] - a[k]) for k in (0, 1)))
        if keep(b):
            out.append(b)
    return out


def f32(v):
    # The value the C++ float holds, written with three decimals.
    return struct.unpack('f', struct.pack('f', round(v, 3)))[0]


def polygons():
    reach = PRONG_REACH / (BOTTOM - TOP)
    left = [(PRONG_INNER + (x - 10.053) * SCALE, (y - TOP) * reach) for x, y in left_prong()]
    left = clip(clip(left, 0, 0.0, True), 1, float(H), False)
    left = [p for i, p in enumerate(left) if p != left[i - 1]]
    c0, c1 = W / 2 - CENTRE_HALF, W / 2 + CENTRE_HALF
    centre = [(c0, 0.0), (c1, 0.0), (c1, float(H)), (c0, float(H))]
    right = [(W - x, y) for x, y in reversed(left)]
    return [[(f32(x), f32(y)) for x, y in p] for p in (left, centre, right)]


def inside(polys, x, y):
    # Non-zero winding, as gemui::Outline::inside.
    wn = 0
    for poly in polys:
        for i, (x1, y1) in enumerate(poly):
            x0, y0 = poly[i - 1]
            if (y0 <= y < y1 or y1 <= y < y0) and x0 + (y - y0) * (x1 - x0) / (y1 - y0) > x:
                wn += 1 if y1 > y0 else -1
    return wn != 0


def margin(polys, x, y):
    # Distance from (x, y) to the nearest edge.
    best = float('inf')
    for poly in polys:
        for i, (x1, y1) in enumerate(poly):
            x0, y0 = poly[i - 1]
            dx, dy = x1 - x0, y1 - y0
            t = max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy)))
            best = min(best, ((x0 + t * dx - x) ** 2 + (y0 + t * dy - y) ** 2) ** 0.5)
    return best


def build():
    polys = polygons()
    rows = [[inside(polys, x + 0.5, y + 0.5) for x in range(W)] for y in range(H)]
    if any(r != r[::-1] for r in rows):
        raise ValueError('the raster is not symmetric')
    m = min(margin(polys, x + 0.5, y + 0.5) for x in range(W) for y in range(H))
    if m < 0.02:
        raise ValueError(f'a pixel centre lies {m:.3f} pixel from an edge')
    out = HEADER
    out += ''.join('//   ' + ''.join('#' if v else '.' for v in r) + '\n' for r in rows)
    per = (W + 7) // 8
    out += f'const uint8_t kFujiBits[{per * H}] = {{\n'
    for r in rows:
        bytes_ = [sum(0x80 >> k for k in range(8) if b * 8 + k < W and r[b * 8 + k]) for b in range(per)]
        out += '    ' + ', '.join(f'0x{v:02x}' for v in bytes_) + ',\n'
    out += '};\n\n'
    out += '// Left prong, centre bar, right prong: x, y of each vertex.\n'
    points = [v for p in polys for v in p]
    out += f'const float kFujiXy[{2 * len(points)}] = {{\n'
    for k in range(0, len(points), 4):
        out += '    ' + ' '.join(f'{x:.3f}f, {y:.3f}f,' for x, y in points[k:k + 4]) + '\n'
    out += '};\n'
    ends, n = [], 0
    for p in polys:
        n += len(p)
        ends.append(n)
    out += f'const uint16_t kFujiEnds[{len(ends)}] = {{{", ".join(str(e) for e in ends)}}};\n'
    out += 'const gemui::Outline kFujiOutline{kFujiXy, kFujiEnds};\n\n}  // namespace\n\n'
    out += f'const gemui::Bitmap kFuji{{{W}, {H}, kFujiBits, &kFujiOutline}};\n\n'
    out += '}  // namespace stym::pluginui::icons\n'
    return out


def main():
    text, target = build(), os.path.normpath(TARGET)
    if '--check' in sys.argv[1:]:
        with open(target, encoding='utf-8') as f:
            same = f.read() == text
        print('FujiData.cpp is up to date' if same else 'FujiData.cpp differs: run tools/icon/make_fuji_data.py')
        return 0 if same else 1
    with open(target, 'w', encoding='utf-8') as f:
        f.write(text)
    print(f'{len(text)} bytes written to src/pluginui/src/FujiData.cpp')
    return 0


if __name__ == '__main__':
    sys.exit(main())
