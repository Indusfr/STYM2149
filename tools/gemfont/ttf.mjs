// STYM2149 — reading of the prototype's GEM font (TrueType embedded in base64 in YM2149_Prototype.html).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The font is made of pixel squares: straight contours, all points on the grid of 64 units (one pixel unit of
// the 8x16 cell at 16 px). Only what is needed to rasterise it is read: head, hhea, hmtx, cmap (format 4), loca
// and glyf (simple glyphs, on-curve points only).
import fs from 'fs';

export function extractTtf(htmlPath) {
  const html = fs.readFileSync(htmlPath, 'latin1');
  const m = html.match(/font-family:'GEM';src:url\(data:font\/ttf;base64,([A-Za-z0-9+/=]+)\)/);
  if (!m) throw new Error('GEM font not found in ' + htmlPath);
  return Buffer.from(m[1], 'base64');
}

export function parseTtf(buf) {
  const u16 = (o) => buf.readUInt16BE(o), i16 = (o) => buf.readInt16BE(o), u32 = (o) => buf.readUInt32BE(o);
  const tables = {};
  for (let i = 0; i < u16(4); i++) {
    const o = 12 + i * 16;
    tables[buf.toString('latin1', o, o + 4)] = u32(o + 8);
  }
  for (const t of ['head', 'hhea', 'hmtx', 'cmap', 'loca', 'glyf', 'maxp'])
    if (tables[t] === undefined) throw new Error('table ' + t + ' missing');
  const upem = u16(tables.head + 18);
  const longLoca = i16(tables.head + 50) === 1;
  const ascent = i16(tables.hhea + 4), descent = -i16(tables.hhea + 6);
  const numHMetrics = u16(tables.hhea + 34);
  const numGlyphs = u16(tables.maxp + 4);
  const advance = (g) => u16(tables.hmtx + 4 * Math.min(g, numHMetrics - 1));
  const loca = (g) => (longLoca ? u32(tables.loca + 4 * g) : u16(tables.loca + 2 * g) * 2);

  // cmap: Windows Unicode subtable (3, 1), format 4.
  let sub = -1;
  for (let i = 0; i < u16(tables.cmap + 2); i++) {
    const r = tables.cmap + 4 + i * 8;
    if (u16(r) === 3 && u16(r + 2) === 1) sub = tables.cmap + u32(r + 4);
  }
  if (sub < 0 || u16(sub) !== 4) throw new Error('cmap (3, 1) format 4 missing');
  const segX2 = u16(sub + 6);
  const endO = sub + 14, startO = endO + segX2 + 2, deltaO = startO + segX2, rangeO = deltaO + segX2;
  const cmap = new Map();
  for (let s = 0; s < segX2 / 2; s++) {
    const end = u16(endO + 2 * s), start = u16(startO + 2 * s), delta = i16(deltaO + 2 * s);
    const ro = u16(rangeO + 2 * s);
    for (let c = start; c <= end && c !== 0xffff; c++) {
      let g = ro === 0 ? (c + delta) & 0xffff : u16(rangeO + 2 * s + ro + 2 * (c - start));
      if (ro !== 0 && g) g = (g + delta) & 0xffff;
      if (g) cmap.set(c, g);
    }
  }

  // Contours of a glyph: lists of points [x, y] in units, y upwards.
  const contours = (g) => {
    const o = tables.glyf + loca(g), len = loca(g + 1) - loca(g);
    if (!len) return [];
    const nc = i16(o);
    if (nc < 0) throw new Error('composite glyph ' + g + ' not supported');
    const ends = [];
    for (let k = 0; k < nc; k++) ends.push(u16(o + 10 + 2 * k));
    const np = ends[nc - 1] + 1;
    let p = o + 12 + 2 * nc + u16(o + 10 + 2 * nc);
    const flags = [];
    while (flags.length < np) {
      const f = buf[p++];
      flags.push(f);
      if (f & 8) for (let r = buf[p++]; r > 0; r--) flags.push(f);
    }
    if (flags.some((f) => !(f & 1))) throw new Error('glyph ' + g + ': off-curve point');
    const xs = [], ys = [];
    let x = 0;
    for (const f of flags) {
      if (f & 2) { const d = buf[p++]; x += f & 16 ? d : -d; }
      else if (!(f & 16)) { x += buf.readInt16BE(p); p += 2; }
      xs.push(x);
    }
    let y = 0;
    for (const f of flags) {
      if (f & 4) { const d = buf[p++]; y += f & 32 ? d : -d; }
      else if (!(f & 32)) { y += buf.readInt16BE(p); p += 2; }
      ys.push(y);
    }
    const out = [];
    let first = 0;
    for (const e of ends) {
      const c = [];
      for (let k = first; k <= e; k++) c.push([xs[k], ys[k]]);
      out.push(c);
      first = e + 1;
    }
    return out;
  };
  return { upem, ascent, descent, numGlyphs, cmap, advance, contours };
}
