# tools/gemfont — GEM font of the interface

The interface (STYM2149_SPEC.md §6) writes its text with the EmuTOS 8×16 GEM system font, the one the prototype
embeds as TrueType (`@font-face` "GEM", square contours on a grid of 64 units per original pixel), and its small
texts with the GEM 8×8 and 6×6 system fonts, read from the EmuTOS sources. Glyphs under GPL v2 or later
(THIRD_PARTY.md).

| File | Role |
|---|---|
| `ttf.mjs` | Reads the TrueType embedded in base64 in `YM2149_Prototype.html` (tables `cmap`, `glyf`, `hmtx`, `hhea`). |
| `raster.mjs` | Rasterises a glyph at a given size, like monochrome FreeType (rules in `extract.mjs`). |
| `extract.mjs` | Writes `src/gemui/src/FontData.cpp` (with `--prototype`, `tools/gemshot/PrototypeFont.cpp`): 151 characters (printable ASCII, E001-E008, then ♪ and • from `bios/fnt_st_8x16.c`, ù ² Ö Ä Ü ß ´, and the accented letters and signs of the Atari ST encoding for the translated tooltips), from 8 to 16 px, origin at 0, 1/4, 1/2 and 3/4 of a pixel, and the table of the code points of the fallback font. |
| `verify.mjs` | Compares the rasterisation with Chromium without antialiasing, pixel by pixel. |
| `emutos_small.mjs` | Writes `src/gemui/src/FontDataSmall.cpp`: the same 151 characters in the EmuTOS 8×8 (`bios/fnt_st_8x8.c`) and 6×6 (`bios/fnt_st_6x6.c`) fonts. |

## Regenerate

```sh
node tools/gemfont/extract.mjs YM2149_Prototype.html emutos/bios/fnt_st_8x16.c > src/gemui/src/FontData.cpp
node tools/gemfont/extract.mjs --prototype YM2149_Prototype.html emutos/bios/fnt_st_8x16.c > tools/gemshot/PrototypeFont.cpp
```

Place of the glyphs (sixth DAW test, item 68). The TrueType's `hmtx` table gives every glyph a left side
bearing of 0, so the browser starts the ink of each one at the origin: the EmuTOS bearings are lost, i, l, `.`, `:`,
`!`, `|` stick to the left of their cell, I and most letters move one column to the left, M, W, K and the 8-column
glyphs stay. Indus saw i and I off-centre. `FontData.cpp` therefore keeps each glyph at its place in the EmuTOS cell
(the contours of `bios/fnt_st_8x16.c` rasterised as they are, same rules); `PrototypeFont.cpp`, written with
`--prototype`, keeps the TrueType's placement, with which gemshot draws the scenes compared with the prototype's
screenshots (`gemui::Font::useSizes`). The STYM2149 scenes, compared with their own references, use gemui's.

The prototype's TrueType is the EmuTOS 8×16 font made of pixel squares: `extract.mjs` checks this (the squares of
`bios/fnt_st_8x16.c`, rasterised, give back the 103 characters of the TrueType at every size and at the four
quarters) before taking from it the characters the TrueType lacks (`FALLBACK` in `raster.mjs`).

Small fonts, from the EmuTOS sources (commit 02c2db75fc875265c1449efea014450466cf20b7, the one in
THIRD_PARTY.md):

```sh
node tools/gemfont/emutos_small.mjs emutos/bios/fnt_st_8x8.c emutos/bios/fnt_st_6x6.c > src/gemui/src/FontDataSmall.cpp
```

The script checks the form of both fonts (cell, height, baseline, fixed pitch, 256 characters) before writing. The
glyphs U+E001 to U+E008 are the characters 0x01 to 0x08 of the Atari ST encoding; ♪ and • (U+266A, U+2022) its
characters 0x0B and 0xFA; ù, ², Ö, Ä, Ü, ß and ´, engravings of the AZERTY and QWERTZ layouts of the
keyboard drawing, its characters 0x97, 0xFD, 0x99, 0x8E, 0x9A, 0x9E and 0xBA; the accented letters and
signs of the tooltips in French, German, Spanish, Portuguese and Italian, its characters 0x80 to 0xB8 and 0xF8
(`FALLBACK` list in `raster.mjs`).

`FontData.cpp` and `FontDataSmall.cpp` are versioned: the build depends neither on Node, nor on the prototype, nor
on EmuTOS. They only change if one of the fonts changes.

## Verify

```sh
PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node tools/gemfont/verify.mjs YM2149_Prototype.html
```

This checks the placement of the prototype (`--prototype`): each glyph is placed alone in a Chromium page with the
prototype's font, without antialiasing (fontconfig
`tools/proto_capture/fonts-noaa.conf`, the Linux equivalent of the prototype's `-webkit-font-smoothing:none`), at a
whole left position plus 0, 1/4, 1/2 and 3/4 of a pixel, then each pixel of its cell is compared with the
rasterisation of the same quarter, placed on the expected baseline (top of the box plus the ascent rounded to the
nearest pixel, a half rounded up).

Result of 15/10/2026 (Playwright's Chromium, Linux): 3,708 glyphs checked (103 characters × 9 sizes × 4 quarters),
0 pixels different. The `FALLBACK` characters (♪, •, ù...) are not checked this way: Chromium takes them from the
fallback font.

## Metrics used by gemui

Advance of a character: half the size (8 px to 16 px). Ascent and descent are each rounded to the nearest pixel, as
Chromium does for the `normal` line height: 16 px → 13 + 3, 14 px → 11 + 3, 12 px → 10 + 2, 10 px → 8 + 2,
9 px → 7 + 2. The baseline of a text is therefore at the top of its box plus the rounded ascent.

Position of a character: Skia (Chromium's drawing engine) places the origin of each glyph at the quarter pixel
nearest to its exact position (advance and `letter-spacing` included): column `floor(x + 1/8)`, quarter
`floor(4 × frac(x + 1/8))`. The contour is rasterised shifted by that quarter, which changes the shape of the glyph
at every size except 16 px (contours on the pixel grid: quarters 2 and 3 only shift by one column). `FontData.cpp`
therefore keeps up to four sets per size.

Characters outside the font (arrows ←→↑↓, check mark ✓, note ♪, bullet •, em dash —, engravings ù ² Ö Ä Ü ß ´ of
the keyboard drawing): Chromium takes them from the next font in the prototype's stack
(`ui-monospace, "Consolas", monospace`: DejaVu Sans Mono on the capture machine). gemui draws them with the
matching GEM glyph (the hyphen for the em dash, missing from the Atari ST encoding), but uses the measured
metrics of that fallback font, which change the layout: whole advance `round(0.602 × size)` (8 px to 14 px), ascent
`round(0.928 × size)` and descent `round(0.236 × size)` in the line box (a checked menu entry is 22 px high instead
of 20). The small fonts have these characters: a small text never uses the fallback font; in 6×6, whose widths are
those of the prototype, they still advance like it (7 px at 12 px), in 8×8 by the cell.

## Small texts

The prototype also draws its small texts (12 px and less) with the scaled-down 8×16 font. gemui writes them with
the GEM bitmap fonts, without scaling: the 8×8 (8 px advance) where it fits, the 6×6 (6 px advance, SMALL font of
the AES, picked by `aes/gemgraf.c` as the smallest height of the workstation) in the narrow places of the
prototype, on request (`Face::kSmall`). Each glyph is placed on a whole column (the quarter pixel does not apply),
its last row (descenders) on the baseline of the prototype's box, which keeps its height: the vertical layout does
not change. The 8×16 sets from 8 to 12 px in `FontData.cpp` now only serve for the metrics.
