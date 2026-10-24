// STYM2149 — reference JSON files of the File menu (preset, pattern, Song, kit), written by the functions of the
// prototype's page (savePreset, savePattern, saveSong, saveKit).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Usage: node tools/proto_golden/files.mjs YM2149_Prototype.html tests/data/files
//
// Two projects: the one of a fresh page (preset_default.json) and a project with settings (preset_custom.json,
// pattern_custom.json, song_custom.json, kit_custom.json), which tests/unit/test_project_files.cpp rebuilds in the
// STYM2149 model to compare byte for byte what it writes and check what it reads back.
import { writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { readPage, pageFunction, pageConst } from './page.mjs';

const page = readPage(process.argv[2]);
const outDir = process.argv[3];
const F = (n) => pageFunction(page, n);
const C = (n) => pageConst(page, n);

// Functions of the page as they are; the download link (a.click) returns the text of the file.
const body = [
  'const NCH = 16;', 'const NSTEP = 256;',
  C('POLYKEYS'), C('LANEKEYS'), C('KIT'), C('GLOB'),
  F('mkCh'), F('snapPattern'), F('emptyPattern'), F('padLane'), F('clonePattern'),
  F('savePreset'), F('savePattern'), F('saveSong'), F('saveKit'), F('dlBlob'),
  `let workDirty = 0, curPat = 0, SONG = [], songOn = 0;
   const PATS = Array.from({length:32}, ()=>null);
   const fileNames = { preset: 'p', pattern: 'p', song: 's', 'drum kit': 'k' };
   const CH = Array.from({length:NCH}, mkCh);
   CH[0].enabled = 1;
   return { CH, GLOB, KIT, PATS, emptyPattern,
            setCurPat(v){ curPat = v; }, setSong(v, on){ SONG = v; songOn = on; },
            savePreset, savePattern, saveSong, saveKit };`,
].join('\n');

function make() {
  const out = { text: null };
  class Blob { constructor(parts) { this.text = parts.join(''); } }
  const URL = { createObjectURL: (b) => b };
  const document = { createElement: () => ({ click() { out.text = this.href.text; } }) };
  const P = new Function('Blob', 'URL', 'document', body)(Blob, URL, document);
  P.save = (fn) => { out.text = null; fn(); if (out.text === null) throw new Error('no output'); return out.text; };
  return P;
}

const write = (name, text) => writeFileSync(join(outDir, name), text);

// Project with settings: the same settings are rebuilt in test_project_files.cpp.
function custom(P) {
  const G = P.GLOB;
  G.swing = 62; G.metro = 1;                         // keys added by the Swing slider, then the Metro button
  G.master = 0.42; G.mode3ch = 1; G.chips = 2; G.bpm = 133.33; G.steps = 32;
  G.digiCh = 1; G.digiCh2 = 0; G.vbl = 60; G.sig = 3; G.reso = 4;
  const c2 = P.CH[2];
  c2.enabled = 1; c2.name = 'Bass'; c2.mute = 1; c2.pan = -30 / 100; c2.plen = 12; c2.seqOct = 4;
  c2.level = 60 / 100; c2.gate = 35 / 100;
  Object.assign(c2.patch, { vol4: 9, EP: 1234, shape: 10, effect: 1, sidRatio: 2.5, noiseEn: 1, NP: 20,
    vibDepth: 30, vibRate: 7.5, ornOn: 1, ornLen: 4 });
  c2.patch.ornTable[1] = 7;
  Object.assign(c2.arp, { on: 1, rate: 25, mode: 2, oct: 2, tlen: 4 });
  c2.arp.table[3] = 5;
  P.CH[5].solo = 1;
  const l2 = c2.lane;
  l2.pat[0] = 0; l2.pat[4] = 7; l2.oct[4] = 5; l2.vol[4] = 12; l2.tie[1] = 1; l2.slide[4] = 1;
  l2.pat2[0] = 4; l2.dur2[0] = 2; l2.gat[8] = 60; l2.pnr[8] = 81; l2.cnd[12] = 3; l2.slc[4] = 6;
  l2.mix[2] = 2; l2.slt[2] = 4; l2.cut[6] = 1; l2.fx[6] = 3; l2.rtg[10] = 2; l2.afl[3] = 1;
  const l3 = P.CH[3].lane;
  l3.pat[0] = 3; l3.ton[0] = -5; l3.oct[0] = -2;
  P.setCurPat(5);
  const p0 = P.emptyPattern(); p0.steps = 16; p0.chans[0].pat[0] = 0;
  const p9 = P.emptyPattern(); p9.steps = 64; p9.chans[1].pat[63] = 11; p9.chans[1].vol[63] = 3;
  P.PATS[0] = p0; P.PATS[9] = p9;
  P.setSong([{ pat: 0, rep: 2 }, { pat: 5, rep: 1, bpm: 140 }, { pat: 9, rep: 4 }], 1);
  const K = P.KIT;
  K.kick.pitch = 50; K.snare.tone = 40; K.clap.tail = 30; K.chat.dec = 120; K.tom2.noise = 25;
}

{
  const P = make();
  write('preset_default.json', P.save(P.savePreset));
}
{
  const P = make();
  custom(P);
  write('preset_custom.json', P.save(P.savePreset));
  write('pattern_custom.json', P.save(P.savePattern));
  write('song_custom.json', P.save(P.saveSong));
  write('kit_custom.json', P.save(P.saveKit));
}
