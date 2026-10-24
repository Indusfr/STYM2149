// STYM2149 — checks the rasterisation of the GEM font against Chromium, glyph by glyph (tools/gemfont/README.md)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Usage: node tools/gemfont/verify.mjs YM2149_Prototype.html
// Needs Playwright and its Chromium (PLAYWRIGHT_MODULE pointing to the playwright module, or NODE_PATH).
//
// Places each glyph of the TrueType (103 characters, from 8 to 16 px, origin at 0, 1/4, 1/2 and 3/4 of a pixel; not
// those of FALLBACK, which Chromium takes from another font) alone in a page,
// with the prototype's font, in Chromium without antialiasing (tools/proto_capture/fonts-noaa.conf), and compares
// each pixel of the capture with the rasterisation of raster.mjs placed at the expected baseline: top of the box +
// rounded ascent (half up).
// Exits with an error at the first different pixel.
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { TTF_CODES as CODES, QUARTERS, SIZES, rasterizeAt } from './raster.mjs';
import { extractTtf, parseTtf } from './ttf.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const html = process.argv[2];
if (!html) {
  console.error('usage: node verify.mjs YM2149_Prototype.html');
  process.exit(2);
}

async function loadPlaywright() {
  const spec = process.env.PLAYWRIGHT_MODULE;
  if (spec) return import(pathToFileURL(spec).href);
  return createRequire(import.meta.url)('playwright');
}

const ttf = extractTtf(html);
const font = parseTtf(ttf);
const CELL_X = 20, CELL_Y = 30, MARGIN = 10;
let page = `<html><head><style>@font-face{font-family:'GEM';src:url(data:font/ttf;base64,${ttf.toString('base64')})}
body{margin:0;background:#fff} span{position:absolute;font-family:GEM;line-height:normal;color:#000;white-space:pre}
</style></head><body>`;
// One row of cells per size and per quarter pixel.
const cells = [];
SIZES.forEach((size) => { for (let q = 0; q < QUARTERS; q++) cells.push({ size, q }); });
cells.forEach(({ size, q }, ri) => CODES.forEach((c, ci) => {
  const left = MARGIN + ci * CELL_X + q / QUARTERS;
  page += `<span style="left:${left}px;top:${MARGIN + ri * CELL_Y}px;font-size:${size}px">&#${c};</span>`;
}));
page += '</body></html>';

const { chromium } = await loadPlaywright();
const browser = await chromium.launch({ env: { ...process.env, FONTCONFIG_FILE: join(here, '../proto_capture/fonts-noaa.conf') } });
const width = 2 * MARGIN + CODES.length * CELL_X, height = 2 * MARGIN + cells.length * CELL_Y;
const tab = await browser.newPage({ viewport: { width, height }, deviceScaleFactor: 1 });
await tab.setContent(page);
await tab.evaluate(() => document.fonts.ready);
// Pixels read in the page: a canvas would not see the text rendering, so the PNG capture is decoded in the page.
const png = await tab.screenshot({ type: 'png' });
const gray = await tab.evaluate(async (b64) => {
  const img = new Image();
  img.src = 'data:image/png;base64,' + b64;
  await img.decode();
  const cv = document.createElement('canvas');
  cv.width = img.width; cv.height = img.height;
  const cx = cv.getContext('2d');
  cx.drawImage(img, 0, 0);
  const d = cx.getImageData(0, 0, img.width, img.height).data;
  const out = [];
  for (let i = 0; i < d.length; i += 4) out.push(d[i] < 128 && d[i + 1] < 128 && d[i + 2] < 128 ? 1 : 0);
  return out;
}, png.toString('base64'));
await browser.close();

let checked = 0, errors = 0;
cells.forEach(({ size, q }, ri) => CODES.forEach((c, ci) => {
  const k = size / font.upem;
  const x0 = MARGIN + ci * CELL_X, y0 = MARGIN + ri * CELL_Y;
  const baseline = y0 + Math.floor(font.ascent * k + 0.5);
  const g = rasterizeAt(font, font.cmap.get(c), size, q / QUARTERS);
  // Window of the cell, margin included: an extra ink pixel around the glyph counts too.
  for (let y = y0 - 3; y < y0 + size + 4; y++)
    for (let x = x0 - 3; x < x0 + CELL_X - 3; x++) {
      const r = y - baseline - g.top, col = x - x0;
      const want = r >= 0 && r < g.rows.length && col >= 0 && col < 16 && (g.rows[r] & (0x8000 >> col)) !== 0;
      if (want !== (gray[y * width + x] === 1)) {
        if (errors++ < 20) console.error(`${size} px, ${q}/4, U+${c.toString(16)}: pixel (${col}, ${y - baseline}) differs`);
      }
    }
  ++checked;
}));
console.log(`${checked} glyphs checked, ${errors} pixels differ`);
process.exit(errors ? 1 : 0);
