// STYM2149 — pictograms of the prototype rasterised without antialiasing (R13 envelope shapes, tools, transport,
// note figures and cursors of the sequencer)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Usage: node tools/proto_capture/icons.mjs YM2149_Prototype.html > src/pluginui/src/IconData.cpp
// (Playwright and its Chromium, as for capture.mjs.)
//
// The prototype draws these pictograms as antialiased SVG (paths and 1.6 px strokes). The style guide
// (STYM2149_SPEC.md §6.1) forbids antialiasing: each SVG is rasterised by Chromium with shape-rendering="crispEdges"
// (a pixel is lit when its centre is inside the shape), at its display size, then written as bits (bit 7 = column 0).
// The Atari logo of the menu bar no longer comes from the prototype: tools/fuji/fuji.py writes it (24/10/2026, D-404).
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const [,, html] = process.argv;
if (!html) {
  console.error('usage: node icons.mjs YM2149_Prototype.html > IconData.cpp');
  process.exit(2);
}
const src = readFileSync(html, 'utf8');

// Envelope shapes (SHAPE_SVG array).
const shapeBlock = src.match(/const SHAPE_SVG = \[([^]*?)\];/);
if (!shapeBlock) throw new Error('SHAPE_SVG not found');
const shapes = [...shapeBlock[1].matchAll(/"(<svg[^"]*<\/svg>)"/g)].map(m => m[1]);
if (shapes.length !== 16) throw new Error('SHAPE_SVG: 16 shapes expected, ' + shapes.length + ' found');
// Sequencer: tools and clipboard (TOOLS, CLIPS, 15 × 15 in .tools), transport (ICONS: 20 × 20 in the bar of the
// Step Sequencer window, 11 × 11 in the one of the Song window). The prototype's arrays are only concatenated
// strings: they are evaluated as they are.
const literal = (re, what) => {
  const m = src.match(re);
  if (!m) throw new Error(what + ' not found');
  return new Function('return ' + m[1])();
};
const tools = literal(/const TOOLS = (\[[^]*?\n\]);/, 'TOOLS');
const clips = literal(/const CLIPS = (\[[^]*?\n\]);/, 'CLIPS');
const transport = literal(/const ICONS = (\{[^]*?\n\});/, 'ICONS');
// Note figures of the Note menu (figIcon): 22 × 18 SVGs placed in the pictogram box of a menu (.gmg svg: 36 × 16,
// ratio kept, scale 8/9). The prototype's function and its helpers (NG) are evaluated as they are, with one
// deviation: the digit 3 of the triplets is a stroke of 0.9 unit, i.e. 0.8 pixel, which Chromium draws antialiased
// even in crispEdges (a stroke thinner than a pixel is a hairline); it is widened to one pixel (1.125 unit) so that
// it is rasterised.
const figSrc = src.match(/const NG = \{[^]*?\n\};\nfunction figIcon\(kind\)\{[^]*?\n\}/);
if (!figSrc) throw new Error('figIcon not found');
// The sixty-fourth note of the 1/64 resolution (D-422), which the prototype does not draw: its four flags from the
// top of the box, the head and the stem one unit lower (14 instead of 13), the last flag clear of the head.
const { figIcon: protoFigIcon, NG } = new Function(figSrc[0] + '\nreturn { figIcon, NG };')();
// The thirty-second-note triplet of the 1/32T resolution (D-433), which it does not draw either: its 1/32 with the "3"
// of its triplets.
const figIcon = kind => kind === '1/32T' ? protoFigIcon('1/32').replace('</svg>', NG.tri() + '</svg>')
  : kind !== '1/64' ? protoFigIcon(kind)
  : '<svg viewBox="0 0 22 18" xmlns="http://www.w3.org/2000/svg">' + NG.head(7, 14) +
    '<rect x="9.4" y="0" width="1.3" height="14"/>' + NG.flag(7, 11, 4) + '</svg>';
const figNames = ['Step', '1/1', '1/2', '1/4', '1/8', '1/16', '1/32', '1/64', '1/4.', '1/8.', '1/16.', '1/4T', '1/8T',
                  '1/16T', '1/32T'];
const sized = (svg, n, h = n) => svg.replace('<svg', `<svg width='${n}' height='${h}'`);
const seqIcons = [
  ...tools.map(([id, , svg]) => ['kTool_' + id, sized(svg, 15)]),
  ...clips.map(([id, , svg]) => ['kClip_' + id, sized(svg, 15)]),
  ...['play', 'stop', 'rec', 'loop', 'metro', 'start', 'prev', 'next'].map(k => ['kBig_' + k, sized(transport[k], 20)]),
  ...['play', 'stop', 'rec', 'loop', 'start', 'prev', 'next'].map(k => ['kSmall_' + k, sized(transport[k], 11)]),
  ...figNames.map((n, i) => ['kFig_' + i, sized(figIcon(n).replace('stroke-width="0.9"', 'stroke-width="1.125"'), 36, 16)]),
];

// Drawn cursors of the prototype (CSS cursor:url): pencil of the Draw tool (18 × 18), scissors of Alt on a note
// (22 × 22), bee of long operations (24 × 24), with their hot spot. Rasterised on a transparent background: black,
// white or transparent pixels.
const cursorOf = (re, what) => {
  const m = src.match(re);
  if (!m) throw new Error(what + ' not found');
  return { svg: decodeURIComponent(m[1].replace(/\\\n/g, '')), hotX: +m[2], hotY: +m[3] };
};
const cursors = [
  ['kCursorPencil', cursorOf(/#seqgrid\.tool-draw \.cell\[data-off\]\{cursor:url\("data:image\/svg\+xml;utf8,([^"]*)"\) (\d+) (\d+)/,
                             'pencil cursor')],
  ['kCursorScissors', cursorOf(/\.seqgrid\.cut \.cell\.held\{\s*cursor: url\("data:image\/svg\+xml;utf8,([^"]*)"\) (\d+) (\d+)/,
                               'scissors cursor')],
  // GEM bee (body.busy): long operations, exports (M7).
  ['kCursorBee', cursorOf(/body\.busy, body\.busy \*\{cursor:url\("data:image\/svg\+xml;utf8,([^"]*)"\) (\d+) (\d+)/,
                          'bee cursor')],
];

async function loadPlaywright() {
  const spec = process.env.PLAYWRIGHT_MODULE;
  if (spec) return import(pathToFileURL(spec).href);
  return createRequire(import.meta.url)('playwright');
}

const pw = await loadPlaywright();
const browser = await pw.chromium.launch();
const page = await browser.newPage();
const rasters = await page.evaluate(async (svgs) => {
  const out = [];
  for (const s of svgs) {
    let svg = s.replace(/currentColor/g, '#000');
    if (!/xmlns=/.test(svg)) svg = svg.replace('<svg', "<svg xmlns='http://www.w3.org/2000/svg'");
    svg = svg.replace('<svg', "<svg shape-rendering='crispEdges'");
    const w = +svg.match(/width=['"](\d+)/)[1], h = +svg.match(/height=['"](\d+)/)[1];
    const img = new Image();
    img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
    await img.decode();
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    const g = c.getContext('2d');
    g.fillStyle = '#fff'; g.fillRect(0, 0, w, h);
    g.drawImage(img, 0, 0, w, h);
    const d = g.getImageData(0, 0, w, h).data;
    const rows = [];
    let grey = 0;
    for (let y = 0; y < h; y++) {
      const r = [];
      for (let x = 0; x < w; x++) {
        const v = d[(y * w + x) * 4];
        if (v !== 0 && v !== 255) grey++;
        r.push(v < 128 ? 1 : 0);
      }
      rows.push(r);
    }
    out.push({ w, h, rows, grey });
  }
  return out;
}, [...shapes, ...seqIcons.map(i => i[1])]);
const cursorRasters = await page.evaluate(async (svgs) => {
  const out = [];
  for (const s of svgs) {
    const svg = s.replace('<svg', "<svg shape-rendering='crispEdges'");
    const w = +svg.match(/width=['"](\d+)/)[1], h = +svg.match(/height=['"](\d+)/)[1];
    const img = new Image();
    img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
    await img.decode();
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    const g = c.getContext('2d');
    g.drawImage(img, 0, 0, w, h);
    const d = g.getImageData(0, 0, w, h).data;
    const ink = [], mask = [];
    let grey = 0;
    for (let y = 0; y < h; y++) {
      const ri = [], rm = [];
      for (let x = 0; x < w; x++) {
        const [v, a] = [d[(y * w + x) * 4], d[(y * w + x) * 4 + 3]];
        if ((a !== 0 && a !== 255) || (a === 255 && v !== 0 && v !== 255)) grey++;
        rm.push(a >= 128 ? 1 : 0);
        ri.push(a >= 128 && v < 128 ? 1 : 0);
      }
      ink.push(ri); mask.push(rm);
    }
    out.push({ ink: { w, h, rows: ink }, mask: { w, h, rows: mask }, grey });
  }
  return out;
}, cursors.map(([, c]) => c.svg));
await browser.close();

const names = [...shapes.map((_, i) => 'kShape' + i), ...seqIcons.map(i => i[0])];
rasters.forEach((r, i) => {
  if (r.grey) throw new Error(names[i] + ': antialiased (' + r.grey + ' grey pixels), crispEdges ignored');
});
cursorRasters.forEach((r, i) => {
  if (r.grey) throw new Error(cursors[i][0] + ': antialiased (' + r.grey + ' grey pixels), crispEdges ignored');
});

const bytesOf = (r) => {
  const per = Math.ceil(r.w / 8);
  const bytes = [];
  for (const row of r.rows)
    for (let b = 0; b < per; b++) {
      let v = 0;
      for (let k = 0; k < 8; k++) if (row[b * 8 + k]) v |= 0x80 >> k;
      bytes.push(v);
    }
  return bytes;
};
const hex = v => '0x' + v.toString(16).padStart(2, '0');
const art = r => r.rows.map(row => '//   ' + row.map(v => (v ? '#' : '.')).join(''));

const lines = [];
lines.push('// STYM2149 — pluginui: the prototype\'s icons (file produced by tools/proto_capture/icons.mjs)');
lines.push('// Programming: Pascal "Indus" Nowak (with Claude\'s help).');
lines.push('// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).');
lines.push('//');
lines.push('// Do not edit by hand: run `node tools/proto_capture/icons.mjs YM2149_Prototype.html` again.');
lines.push('// Prototype SVGs rasterised by Chromium with shape-rendering="crispEdges"; bit 7 = column 0.');
lines.push('#include "pluginui/IconData.h"');
lines.push('');
lines.push('namespace stym::pluginui::icons {');
lines.push('namespace {');
const emit = (name, r) => {
  lines.push('');
  lines.push(...art(r));
  const b = bytesOf(r);
  const per = Math.ceil(r.w / 8);
  lines.push(`const uint8_t ${name}[${b.length}] = {`);
  for (let y = 0; y < r.h; y++) lines.push('    ' + b.slice(y * per, (y + 1) * per).map(hex).join(', ') + ',');
  lines.push('};');
};
rasters.slice(0, 16).forEach((r, i) => emit('kShape' + i.toString(16).toUpperCase() + 'Bits', r));
const camel = id => id.split('_').map((w, i) => i ? w[0].toUpperCase() + w.slice(1) : w).join('');
seqIcons.forEach(([id], i) => emit(camel(id) + 'Bits', rasters[16 + i]));
cursors.forEach(([id], i) => {
  emit(id + 'Ink', cursorRasters[i].ink);
  emit(id + 'Mask', cursorRasters[i].mask);
});
lines.push('');
lines.push('}  // namespace');
lines.push('');
const s = rasters[0];
lines.push('const std::array<gemui::Bitmap, 16> kShapes = {{');
for (let i = 0; i < 16; i++) lines.push(`    {${s.w}, ${s.h}, kShape${i.toString(16).toUpperCase()}Bits},`);
lines.push('}};');
seqIcons.forEach(([id], i) => {
  if (id.startsWith('kFig_')) return;
  const r = rasters[16 + i];
  lines.push(`const gemui::Bitmap ${camel(id)}{${r.w}, ${r.h}, ${camel(id)}Bits};`);
});
const figs = seqIcons.map(([id], i) => [id, rasters[16 + i]]).filter(([id]) => id.startsWith('kFig_'));
lines.push(`const std::array<gemui::Bitmap, ${figs.length}> kFigures = {{`);
for (const [id, r] of figs) lines.push(`    {${r.w}, ${r.h}, ${camel(id)}Bits},`);
lines.push('}};');
cursors.forEach(([id, c], i) => {
  const { w, h } = cursorRasters[i].ink;
  lines.push(`const Cursor ${id}{{${w}, ${h}, ${id}Ink}, {${w}, ${h}, ${id}Mask}, ${c.hotX}, ${c.hotY}};`);
});
lines.push('');
lines.push('}  // namespace stym::pluginui::icons');
process.stdout.write(lines.join('\n') + '\n');
