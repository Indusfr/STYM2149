// STYM2149 — parity data of the MIDI import (parseMidiFile, GM_SLOT and midiImport of the prototype's page)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Usage: node tools/proto_golden/midi.mjs YM2149_Prototype.html > tests/data/proto/midi.txt
//
// Each case draws a MIDI file (format 0 or 1, division, tempo, melodic channels and channel 10, running status,
// note on with velocity 0, controllers, meta, SysEx) and a context (length of the current pattern, 3-channel mode,
// chips, octave of the tracks), then imports it through the functions of the page. It writes the file in hexadecimal
// and the result: tempo, filled tracks, patterns, truncation, enabled tracks and generators, Song, cells of the
// created patterns and text of the alert. tests/unit/test_midi_files.cpp imports the same files and compares.
//
// Decided deviations (D-170), applied to the text of the page:
// - the octave of a note is written as octCode (0: octave of the track, -1: octave 0); the prototype wrote the
//   octave as is, and a note of octave 0 played at the octave of the track;
// - a held note also carries TIE on its first step, like a note stretched in the grid (stretchNote); the
//   prototype only wrote it on the following steps, and the imported note did not hold;
// - in dual chip (2x3ch), the MIDI channels go to the chip channels of both chips (tracks 1-3 and 5-7); the
//   prototype took tracks 1 to 6, including the Digidrum track of the first chip (track 4).
// And D-345: the VOL of each held step applies, so a held note carries its velocity on all its steps (the prototype
// wrote it on the first one only, the others keeping 15).
import { readPage, pageFunction, pageConst, patch, rng } from './page.mjs';

const page = readPage(process.argv[2]);
const F = (n) => pageFunction(page, n);
const C = (n) => pageConst(page, n);

let importSrc = F('midiImport');
importSrc = patch(importSrc, '      L.oct[a] = oc;',
  '      L.oct[a] = oc === CH[TRK(i)].seqOct ? 0 : (oc === 0 ? -1 : oc);', 'D-170 octave');
importSrc = patch(importSrc,
  '      for (let s = a + 1; s < Math.min(b, T); s++) if (L.pat[s] < 0) L.tie[s] = 1;',
  '      for (let s = a + 1; s < Math.min(b, T); s++) if (L.pat[s] < 0) L.tie[s] = 1;\n' +
  '      if (Math.min(b, T) > a + 1) L.tie[a] = 1;', 'D-170 tie');
importSrc = patch(importSrc,
  '    L.vol[a] = Math.max(1, Math.min(15, Math.round(n.vel / 127 * 15)));\n',
  '    L.vol[a] = Math.max(1, Math.min(15, Math.round(n.vel / 127 * 15)));\n' +
  '    if (!drum) for (let s = a + 1; s < Math.min(b, T); s++) if (L.pat[s] < 0 && L.tie[s]) L.vol[s] = L.vol[a];\n',
  'D-345');
importSrc = patch(importSrc,
  '    for (let ci = 0; ci < NCH; ci++){\n      const o = {};',
  '    for (let tr = 0; tr < NCH; tr++){\n      const ci = LANE(tr);\n      const o = {};', 'D-170 tracks (patterns)');
importSrc = patch(importSrc,
  '      if (ci < order.length){',
  '      if (ci >= 0 && ci < order.length){', 'D-170 tracks (patterns)');
importSrc = patch(importSrc,
  '  for (let i = 0; i < NCH; i++){\n    CH[i].enabled = i < order.length ? 1 : 0;\n    if (i < order.length) CH[i].patch.effect = order[i] === 9 ? 4 : 0;',
  '  for (let i = 0; i < NCH; i++){\n    const li = LANE(i);\n    CH[i].enabled = li >= 0 && li < order.length ? 1 : 0;\n' +
  '    if (li >= 0 && li < order.length) CH[i].patch.effect = order[li] === 9 ? 4 : 0;', 'D-170 tracks (settings)');

const body = [
  'const NCH = 16;',
  C('POLYKEYS'), C('LANEKEYS'), C('GM_DRUM'), C('GM_SLOT'),
  F('parseMidiFile'), importSrc,
  `const GLOB = { steps: 16, mode3ch: 0, chips: 1, bpm: 120 };
   const CH = Array.from({length: NCH}, ()=>({ enabled: 0, seqOct: 3, patch: { effect: 0 } }));
   const PATS = Array.from({length: 32}, ()=>null);
   const SONG = [];
   let curPat = 0, info = '';
   // Tracks of the mode: import rank → track (TRK), track → rank (LANE, -1: none).
   const TRACKS = ()=>GLOB.mode3ch && GLOB.chips > 1 ? [0, 1, 2, 4, 5, 6] : [...Array(NCH).keys()];
   const TRK = i => TRACKS()[i];
   const LANE = t => TRACKS().indexOf(t);
   const els = { bpm: {}, bpmv: {} };
   const $ = id => els[id];
   const pushUndo = ()=>{}, loadPattern = ()=>{}, sendAllChans = ()=>{}, sendSeq = ()=>{};
   const refreshPatBtns = ()=>{}, refreshSong = ()=>{}, refreshUI = ()=>{};
   const gemInfo = h => { info = h; };
   return { GLOB, CH, PATS, SONG, midiImport, parseMidiFile, info: ()=>info, clearInfo(){ info = ''; } };`,
].join('\n');
const make = () => new Function(body)();

// ---- fichiers MIDI
const vlq = (v) => {
  const out = [v & 0x7f];
  v >>>= 7;
  while (v > 0) { out.unshift((v & 0x7f) | 0x80); v >>>= 7; }
  return out;
};
const be = (v, n) => Array.from({length: n}, (_, i) => (v >>> (8 * (n - 1 - i))) & 255);
const chunk = (id, data) => [...[...id].map(c => c.charCodeAt(0)), ...be(data.length, 4), ...data];

function randomTrack(R, opts) {
  // Absolute events, then deltas; running status possible between messages of the same status.
  const ev = [];
  const n = R.int(0, opts.notes);
  const chans = opts.chans;
  for (let k = 0; k < n; k++) {
    const ch = R.pick(chans);
    const t = R.int(0, opts.span);
    const len = R.chance(0.1) ? 0 : R.int(1, opts.div);
    const note = ch === 9 ? R.int(30, 60) : R.int(R.chance(0.1) ? 0 : 20, R.chance(0.1) ? 127 : 100);
    const vel = R.int(1, 127);
    ev.push({ t, d: [0x90 | ch, note, vel] });
    ev.push({ t: t + len, d: R.chance(0.5) ? [0x80 | ch, note, R.int(0, 127)] : [0x90 | ch, note, 0] });
  }
  if (opts.tempo !== undefined) ev.push({ t: R.int(0, opts.div), d: [0xff, 0x51, 0x03, ...be(opts.tempo, 3)], first: 1 });
  if (R.chance(0.5)) ev.push({ t: 0, d: [0xff, 0x03, 4, 0x54, 0x65, 0x73, 0x74] });       // track name
  if (R.chance(0.3)) ev.push({ t: R.int(0, opts.span), d: [0xf0, 3, 0x7e, 0x09, 0xf7] });  // SysEx
  for (let k = R.int(0, 3); k > 0; k--) {
    const ch = R.pick(chans);
    ev.push({ t: R.int(0, opts.span), d: R.pick([[0xb0 | ch, 7, 100], [0xc0 | ch, 5], [0xe0 | ch, 0, 64],
                                                 [0xd0 | ch, 30], [0xa0 | ch, 60, 10]]) });
  }
  ev.sort((a, b) => a.t - b.t || (b.first || 0) - (a.first || 0));
  const out = [];
  let last = 0, run = -1;
  for (const e of ev) {
    out.push(...vlq(e.t - last));
    last = e.t;
    const st = e.d[0];
    if (st < 0xf0 && st === run && opts.running) out.push(...e.d.slice(1));
    else out.push(...e.d);
    run = st < 0xf0 ? st : -1;
  }
  out.push(0x00, 0xff, 0x2f, 0x00);
  return out;
}

function randomFile(R) {
  const div = R.pick([96, 120, 192, 480, 384, 100, 97, 24]);
  const fmt = R.chance(0.3) ? 0 : 1;
  const ntrk = fmt === 0 ? 1 : R.int(1, 4);
  const chans = [];
  for (let k = R.int(1, 5); k > 0; k--) chans.push(R.chance(0.25) ? 9 : R.int(0, 15));
  const tempos = [500000, 400000, 600000, 250000, 3000000, 150000, 1000000, 428571];
  const opts = {
    div, chans, notes: R.int(1, 40), running: R.chance(0.5),
    span: div * R.pick([4, 8, 16, 64, 200]),
  };
  const tracks = [];
  for (let t = 0; t < ntrk; t++)
    tracks.push(randomTrack(R, { ...opts, tempo: R.chance(0.6) ? R.pick(tempos) : undefined,
                                 chans: R.chance(0.5) ? chans : [R.pick(chans)] }));
  const head = chunk('MThd', [...be(fmt, 2), ...be(ntrk, 2), ...be(div, 2)]);
  return [...head, ...tracks.flatMap(t => chunk('MTrk', t))];
}

const hex = (b) => b.map(v => v.toString(16).padStart(2, '0')).join('');
const lines = [];
function runCase(id, bytes, ctx) {
  const P = make();
  Object.assign(P.GLOB, { steps: ctx.steps, mode3ch: ctx.mode3ch, chips: ctx.chips });
  ctx.oct.forEach((o, i) => { P.CH[i].seqOct = o; });
  P.midiImport(Uint8Array.from(bytes).buffer, 'test.mid');
  lines.push('case ' + id);
  lines.push(`ctx ${ctx.steps} ${ctx.mode3ch} ${ctx.chips} ${ctx.oct.join(',')}`);
  lines.push('file ' + hex(bytes));
  const m = P.parseMidiFile(Uint8Array.from(bytes));
  if (!m || !m.notes.length) {
    lines.push('result null');
    lines.push('end');
    return;
  }
  lines.push('bpm ' + P.GLOB.bpm);
  lines.push('song ' + P.SONG.map(e => e.pat + ':' + e.rep).join(' '));
  lines.push('enabled ' + P.CH.map(c => c.enabled).join(','));
  lines.push('effect ' + P.CH.map(c => c.patch.effect).join(','));
  P.SONG.forEach(({ pat }) => {
    const p = P.PATS[pat];
    p.chans.forEach((c, t) => {
      const cells = [];
      for (let s = 0; s < p.steps; s++)
        if (c.pat[s] >= 0 || c.oct[s] || c.tie[s] || c.vol[s] !== 15) cells.push(`${s}:${c.pat[s]}:${c.oct[s]}:${c.tie[s]}:${c.vol[s]}`);
      if (cells.length) lines.push(`lane ${pat} ${t} ${cells.join(' ')}`);
    });
  });
  lines.push('msg ' + P.info().replace(/<br>/g, '|').replace(/<\/?b>/g, ''));
  lines.push('end');
}

const R = rng(2149);
const ctxOf = () => ({
  steps: R.pick([16, 16, 32, 64, 13, 8]),
  mode3ch: R.chance(0.3) ? 1 : 0,
  chips: R.chance(0.5) ? 2 : 1,
  oct: Array.from({length: 16}, () => R.int(1, 6)),
});
for (let k = 0; k < 120; k++) runCase(k, randomFile(R), ctxOf());
// Chosen cases: SMPTE, no note, file longer than 32 patterns, note of octave 0.
const one = (div, track) => [...chunk('MThd', [0, 0, 0, 1, ...be(div, 2)]), ...chunk('MTrk', track)];
runCase('smpte', one(0xe728, [0, 0x90, 60, 100, 10, 0x80, 60, 0, 0, 0xff, 0x2f, 0]), ctxOf());
runCase('empty', one(96, [0, 0xff, 0x51, 3, ...be(500000, 3), 0, 0xff, 0x2f, 0]), ctxOf());
runCase('long', one(96, [0, 0x90, 60, 100, ...vlq(96 * 4 * 64), 0x80, 60, 0, 0, 0xff, 0x2f, 0]),
        { steps: 16, mode3ch: 0, chips: 1, oct: Array(16).fill(3) });
runCase('oct0', one(96, [0, 0x90, 14, 100, 24, 0x80, 14, 0, 0, 0x90, 5, 90, 24, 0x80, 5, 0, 0, 0xff, 0x2f, 0]),
        { steps: 16, mode3ch: 0, chips: 1, oct: Array(16).fill(3) });
runCase('dual', one(96, [0, 0x90, 60, 100, 0, 0x91, 62, 100, 0, 0x92, 64, 100, 0, 0x93, 65, 100, 0, 0x94, 67, 100,
                         0, 0x99, 36, 100, 24, 0x80, 60, 0, 0, 0x81, 62, 0, 0, 0x82, 64, 0, 0, 0x83, 65, 0,
                         0, 0x84, 67, 0, 0, 0x89, 36, 0, 0, 0xff, 0x2f, 0]),
        { steps: 16, mode3ch: 1, chips: 2, oct: Array(16).fill(3) });
process.stdout.write(lines.join('\n') + '\n');
