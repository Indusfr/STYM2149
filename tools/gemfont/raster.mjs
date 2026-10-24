// STYM2149 — rasterisation of the glyphs of the prototype's GEM font, shared by extract.mjs and verify.mjs
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Fill rules: see extract.mjs.
import { readFileSync } from 'node:fs';

export const SIZES = [8, 9, 10, 11, 12, 13, 14, 15, 16];
// Printable ASCII, then the GEM glyphs of the TOS (private area E001-E008 of the prototype's font), then the
// characters that the prototype takes from its fallback font and that gemui draws with the glyph of the Atari ST
// encoding (FALLBACK, D-127).
export const CODES = [];
for (let c = 0x20; c <= 0x7e; c++) CODES.push(c);
for (let c = 0xe001; c <= 0xe008; c++) CODES.push(c);
// Code point → character of the Atari ST encoding: musical note (0x0B), bullet (0xFA, middle dot), then the
// engravings of the AZERTY and QWERTZ layouts of the keyboard drawing (D-131): ù (0x97), ² (0xFD), Ö (0x99),
// Ä (0x8E), Ü (0x9A), ß (0x9E), ´ (0xBA); then the accented letters and signs of the Atari ST encoding used by the
// tooltips in French, German, Spanish, Portuguese and Italian (D-185). The accented capitals missing from the
// encoding (È, Ê, Á, Í, Ó, Ú, Ô...) are not there: the translations write them without an accent.
export const FALLBACK = new Map([[0x266a, 0x0b], [0x2022, 0xfa], [0xf9, 0x97], [0xb2, 0xfd], [0xd6, 0x99],
  [0xc4, 0x8e], [0xdc, 0x9a], [0xdf, 0x9e], [0xb4, 0xba],
  [0xc7, 0x80], [0xfc, 0x81], [0xe9, 0x82], [0xe2, 0x83], [0xe4, 0x84], [0xe0, 0x85], [0xe7, 0x87], [0xea, 0x88],
  [0xeb, 0x89], [0xe8, 0x8a], [0xef, 0x8b], [0xee, 0x8c], [0xec, 0x8d], [0xc9, 0x90], [0xf4, 0x93], [0xf6, 0x94],
  [0xf2, 0x95], [0xfb, 0x96], [0xff, 0x98], [0xe1, 0xa0], [0xed, 0xa1], [0xf3, 0xa2], [0xfa, 0xa3], [0xf1, 0xa4],
  [0xd1, 0xa5], [0xaa, 0xa6], [0xba, 0xa7], [0xbf, 0xa8], [0xa1, 0xad], [0xab, 0xae], [0xbb, 0xaf], [0xe3, 0xb0],
  [0xf5, 0xb1], [0x153, 0xb4], [0x152, 0xb5], [0xc0, 0xb6], [0xc3, 0xb7], [0xd5, 0xb8], [0xb0, 0xf8]]);
for (const c of FALLBACK.keys()) CODES.push(c);
// Characters of the prototype's TrueType: all but FALLBACK. Atari ST code of each one (E001-E008: 0x01-0x08).
export const TTF_CODES = CODES.filter((c) => !FALLBACK.has(c));
export const atariCode = (c) => (FALLBACK.has(c) ? FALLBACK.get(c) : c >= 0xe001 ? c - 0xe000 : c);

// EmuTOS 8×16 font (bios/fnt_st_8x16.c, GEM form: 256 characters of 8 columns, 16 rows, baseline `top` = 13) as
// contours, like the prototype's TrueType: a square of 64 units per lit pixel, row r between (12 - r) × 64 and
// (13 - r) × 64 units above the baseline. extract.mjs checks that these contours, rasterised, give the same glyphs
// as the TrueType for all its characters.
export function emutos8x16(path) {
  const src = readFileSync(path, 'utf8');
  const field = (name) => {
    const m = src.match(new RegExp('(\\d+),\\s*/\\*\\s*(?:U?WORD)\\s+' + name + '\\b'));
    if (!m) throw new Error(path + ': missing field: ' + name);
    return Number(m[1]);
  };
  if (field('form_width') !== 256 || field('form_height') !== 16 || field('top') !== 13 || field('max_cell_width') !== 8 ||
    field('first_ade') !== 0 || field('last_ade') !== 255 || !/F_MONOSPACE/.test(src)) throw new Error(path + ': unexpected form');
  const table = src.slice(src.indexOf('dat_table['), src.indexOf('};', src.indexOf('dat_table[')));
  const words = [...table.matchAll(/0x([0-9a-fA-F]{4})/g)].map((m) => parseInt(m[1], 16));
  if (words.length !== (256 * 16) / 2) throw new Error(path + ': dat_table: ' + words.length + ' words');
  const bytes = words.flatMap((w) => [w >> 8, w & 0xff]);
  return (code) => {
    const contours = [];
    for (let r = 0; r < 16; r++)
      for (let x = 0; x < 8; x++)
        if ((bytes[r * 256 + code] >> (7 - x)) & 1) {
          const x0 = x * 64, y1 = (13 - r) * 64;
          contours.push([[x0, y1 - 64], [x0, y1], [x0 + 64, y1], [x0 + 64, y1 - 64]]);
        }
    return contours;
  };
}
// Sub-pixel positions of the origin: quarter pixels, as in Skia (horizontal text).
export const QUARTERS = 4;

// Rows from `top` (negative, above the baseline) down to the descent rounded up to the next pixel.
// `glyph`: glyph index in the TrueType, or contours already read (emutos8x16). `dx`: horizontal shift of the contour
// before rasterisation (sub-pixel position, Skia: quarter pixels). `bearing`: the contour keeps its place in the cell
// (EmuTOS's left side bearing, D-266) instead of starting at the origin as in the prototype's TrueType. Returns, for
// each row, an integer whose bit (15 - x) is column x (0 to 15).
export function rasterizeAt(font, glyph, size, dx, bearing = false) {
  const k = size / font.upem;
  const contours = Array.isArray(glyph) ? glyph : font.contours(glyph);
  let xMin = Infinity;
  for (const c of contours) for (const p of c) xMin = Math.min(xMin, p[0]);
  if (!Number.isFinite(xMin) || bearing) xMin = 0;
  const edges = [];
  for (const c of contours)
    for (let i = 0; i < c.length; i++) {
      const a = c[i], b = c[(i + 1) % c.length];
      if (a[1] !== b[1]) edges.push([(a[0] - xMin) * k + dx, -a[1] * k, (b[0] - xMin) * k + dx, -b[1] * k]);
    }
  const top = -Math.ceil(font.ascent * k), bottom = Math.ceil(font.descent * k);
  const rows = [];
  for (let y = top; y < bottom; y++) {
    let bits = 0;
    for (let x = 0; x < 16; x++) {
      const cx = x + 0.5, cy = y + 0.5;
      let winding = 0;
      for (const [x0, y0, x1, y1] of edges) {
        const lo = Math.min(y0, y1), hi = Math.max(y0, y1);
        if (!(cy > lo && cy <= hi)) continue;
        const xi = x0 + ((cy - y0) * (x1 - x0)) / (y1 - y0);
        if (xi >= cx) winding += y0 > y1 ? 1 : -1;
      }
      if (winding !== 0) bits |= 0x8000 >> x;
    }
    rows.push(bits);
  }
  return { top, rows };
}
