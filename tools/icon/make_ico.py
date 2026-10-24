#!/usr/bin/env python3
# STYM2149 — application icon: packaging/icon/STYM2149.ico from the drawn PNG sizes (D-218).
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).
#
# Usage: python3 tools/icon/make_ico.py [--check]
# The icon is pixel art drawn at 16 and 32 pixels (packaging/icon/STYM2149-16.png and -32.png); 48 is the 32 drawing
# scaled down from 256, 256 the 32 drawing with each pixel made an 8 × 8 block. The .ico holds the four sizes as they
# are, so that Windows never scales the 16 px drawing: 16, 32 and 48 as 32-bit bitmaps, 256 as PNG. The installer
# (packaging/windows/STYM2149.iss) uses it; JUCE builds the application's own icon from the 16 and 256 PNG files. With
# --check, nothing is written and the exit status says whether the .ico is up to date. Standard library only.
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ICON = os.path.join(HERE, '..', '..', 'packaging', 'icon')
SIZES = [16, 32, 48, 256]


def read_png(path):
    """Width, height and RGBA rows of an 8-bit RGBA or RGB PNG without interlacing."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG file')
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, colour, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or colour not in (2, 6) or interlace:
                raise ValueError(f'{path}: 8-bit RGB or RGBA without interlacing expected')
            bpp = 4 if colour == 6 else 3
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        filt, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if filt == 1:
                line[i] = (line[i] + a) & 0xFF
            elif filt == 2:
                line[i] = (line[i] + b) & 0xFF
            elif filt == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(bytes(line) if bpp == 4 else
                    b''.join(bytes(line[i:i + 3]) + b'\xff' for i in range(0, stride, 3)))
        prev = line
    return w, h, rows


def bitmap_entry(w, h, rows):
    """Icon image as a 32-bit BITMAPINFOHEADER bitmap: BGRA rows bottom-up, then the AND mask (all zero)."""
    header = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = b''.join(b''.join(bytes((r[i + 2], r[i + 1], r[i], r[i + 3])) for i in range(0, 4 * w, 4))
                      for r in reversed(rows))
    mask = b'\x00' * (((w + 31) // 32) * 4 * h)
    return header + pixels + mask


def build():
    images = []
    for s in SIZES:
        path = os.path.join(ICON, f'STYM2149-{s}.png')
        w, h, rows = read_png(path)
        if (w, h) != (s, s):
            raise ValueError(f'{path}: {w} × {h}, {s} × {s} expected')
        if s >= 256:
            with open(path, 'rb') as f:
                images.append((s, f.read()))
        else:
            images.append((s, bitmap_entry(w, h, rows)))
    out = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, blob in images:
        out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    return out + b''.join(blob for _, blob in images)


def main():
    ico, target = build(), os.path.join(ICON, 'STYM2149.ico')
    if '--check' in sys.argv[1:]:
        with open(target, 'rb') as f:
            same = f.read() == ico
        print('STYM2149.ico is up to date' if same else 'STYM2149.ico differs: run tools/icon/make_ico.py')
        return 0 if same else 1
    with open(target, 'wb') as f:
        f.write(ico)
    print(f'{len(ico)} bytes written to packaging/icon/STYM2149.ico')
    return 0


if __name__ == '__main__':
    sys.exit(main())
