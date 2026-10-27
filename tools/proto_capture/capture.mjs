// STYM2149 — reference captures of the prototype for the visual regression test (STYM2149_SPEC.md §6)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Usage: node tools/proto_capture/capture.mjs YM2149_Prototype.html tests/data/visual [scene...]
// Needs Playwright and its Chromium (NODE_PATH, or PLAYWRIGHT_MODULE pointing to the playwright module).
//
// Each scene puts the prototype in a state (functions of the page), then captures the whole page in a browser
// window the size of the editor's desktop: 1344 × 984 px (1272 × 944, D-067, larger by a desktop icon, D-461), the
// main window (980 × 902) centred 18 px from the top. The alert boxes (centred at 38 % of the height) and the menus
// (kept inside the page) thus fall at the same places as in the plugin.
// The text is rendered without antialiasing: the prototype asks for it (-webkit-font-smoothing:none), but Chromium
// on Linux only applies it with a fontconfig configuration without antialiasing (fonts-noaa.conf, passed to the
// browser). SVG and canvas drawings stay antialiased, as in the prototype.
import { existsSync, mkdirSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const [,, html, outDir, ...only] = process.argv;
if (!html || !outDir) {
  console.error('usage: node capture.mjs YM2149_Prototype.html output_folder [scene...]');
  process.exit(2);
}

async function loadPlaywright() {
  const spec = process.env.PLAYWRIGHT_MODULE;
  if (spec) return import(pathToFileURL(spec).href);
  return createRequire(import.meta.url)('playwright');
}

const VIEW = { width: 1344, height: 984 };

// Scenes: name → code run in the page (functions and global variables of the prototype).
const SCENES = {
  default: () => {},
  sid: () => { setEffect(1); },
  sync: () => { setEffect(3); },
  chipdrum: () => { setEffect(4); },
  digidrum: () => { setEffect(2); },
  envelope: () => {
    P().volMode = 1; P().adsrChip = 1;
    P().atk = 200; P().dcy = 300; P().sus = 60; P().rel = 500;
    refreshAdsrScale(); refreshUI();
  },
  smooth: () => {
    P().atk = 120; P().dcy = 260; P().sus = 45; P().rel = 700; P().vibDepth = 35;
    refreshUI();
  },
  mode3: () => { setMode3(1); },
  dual: () => { setMode3(2); },
  tabs: () => {
    CH[2].enabled = 1; CH[5].enabled = 1; CH[11].enabled = 1;
    focusCh(5); syncTabs();
    baseOct = 3; buildKbd();
    keyEl(60).classList.add('held'); keyEl(63).classList.add('held');
    leds[0].classList.add('on'); leds[5].classList.add('on');
  },
  menu_file: () => { $('mFile').onclick(); },
  menu_edit: () => { $('mEdit').onclick(); },
  menu_options: () => { $('mOptions').onclick(); },
  menu_fuji: () => { $('mFuji').onclick(); },
  menu_generator: () => { $('fxSel').onclick(); },
  menu_shape: () => { P().volMode = 1; refreshUI(); $('shapeSel').onclick(); },
  menu_source: () => { $('srcSel').onclick(); },
  alert_chip: () => { $('chipTxt').onclick(); },
  // Floating windows (M5), at the prototype's places.
  seq: () => { openSeqWin(); },
  seq_demo: () => { doDemo(); openSeqWin(); },
  song: () => { openSongWin(); },
  // Written Song: repeats, own tempo, selection (Shift) and keyboard cursor on two different blocks.
  song_demo: () => {
    doDemo();
    SONG.push({ pat: 0, rep: 2 }, { pat: 1, rep: 1, bpm: 140 }, { pat: 9, rep: 4 }, { pat: 0, rep: 1 });
    songSel.add(1); songCur = 2;
    openSongWin();
  },
  mixer: () => { openMixer(); },
  // 3 channels: channels A, B, C and Digidrum, inactive faders, no panning, mono output.
  mixer_3ch: () => { setMode3(true); openMixer(); },
  // Oscilloscope of track 3, opened from the Mixer (first one opened: 240, 200).
  mixer_scope: () => { openMixer(); openScope(2); },
  // Arpeggiator: stopped, then running (Up), in Table mode (grid, keyboard cursor), on the MIDI clock.
  arp: () => { openArpWin(); },
  arp_on: () => { $('arpOn').click(); openArpWin(); },
  arp_table: () => { $('arpOn').click(); A().mode = 5; refreshUI(); refreshArpGrid(); openArpWin(); },
  arp_midi: () => { $('arpOn').click(); setClkMode(1); openArpWin(); },
  // Slide Curve (Custom curve of the CRV row): straight line by default, then a drawn curve that stops at 80 %.
  curve: () => { openCurveWin(); },
  curve_custom: () => {
    for (let i = 0; i < CURVE_N; i++) slideTable[i] = 0.8 * Math.sin(i / (CURVE_N - 1) * Math.PI / 2);
    openCurveWin();
  },
  // Computer keyboard: auto layout (QWERTY without navigator.keyboard), two held notes (C and D#),
  // AZERTY, octave 8 (keys out of the chip's range).
  kbd: () => { openKbdWin(); },
  // C and D# held (A and E): the visible state of press(), without the sound (oscilloscope and registers idle), like
  // the tabs scene.
  kbd_held: () => {
    openKbdWin();
    for (const [n, code] of [[36, 'KeyA'], [39, 'KeyE']]){ keyEl(n).classList.add('held'); tklLight(code, 1); }
  },
  kbd_azerty: () => { kbLayout = 'azerty'; updateKbdHint(); openKbdWin(); },
  kbd_high: () => { baseOct = 8; buildKbd(); openKbdWin(); },
  // Modulation (M6): stopped, then running with a drawn table (pitch, NSE, VOL), length 12, Hold,
  // 2 frames per step, keyboard cursor on step 4.
  orn: () => { openOrn(); },
  orn_on: () => {
    const p = P();
    p.ornOn = 1; ornNorm(p);
    [0, 12, 7, 3, 0, -5, -12, 4, 0, 0, 2, -1].forEach((v, i) => { p.ornTable[i] = v; });
    [2, 5, 9].forEach(i => { p.ornNse[i] = 1; });
    [15, 13, 11, 9, 7, 5, 3, 1, 0, 8, 12, 15].forEach((v, i) => { p.ornVol[i] = v; });
    p.ornLen = 12; p.ornLoop = 0; p.ornRate = 2;
    ornCur = 3; ornRow = 9;
    openOrn();
  },
  // Drum Kit (M6): on a track that is not a Chipdrum (note, inert content), on a Chipdrum (Kick, Classic
  // kit), then the TR-808 bank with the Clap tail at 55 (Custom kit, settings on two columns).
  kit_off: () => { openKit(); },
  kit: () => { setEffect(4); openKit(); },
  kit_clap: () => {
    setEffect(4);
    for (const k in KIT) Object.assign(KIT[k], KIT_BANKS['TR-808'][k]);
    KIT.clap.tail = 55; kitInst = 'clap';
    openKit();
  },
  // Patches (M6): empty library (note), then eight patches on three lines.
  lib: () => { patchLib = []; openLib(); },
  lib_list: () => {
    patchLib = [];
    ['Bass', 'Lead 1', 'Snare hit', 'Pad', 'Arp chords', 'Kick', 'Bell', 'Hi hat long name']
      .forEach(n => patchLib.push({ name: n, ...libSnap(0) }));
    openLib();
  },
  // MIDI learn (M6): banner of the learn mode, then after a CC learnt for the Noise period slider.
  learn: () => { setLearn(1); },
  learn_bound: () => {
    setLearn(1);
    learnTarget = { id: 'np', kind: 'range' };
    midiBind('CC21');
  },
  // YM Player (M7): YM3 dump of 250 frames loaded, stopped (ymLoad of the prototype).
  ym: () => {
    const n = 250, u8 = new Uint8Array(4 + 14 * n);
    u8.set([89, 77, 51, 33]);  // "YM3!"
    ymLoad(u8.buffer, 'tune.ym');
  },
  // SHORTCUTS window (M8): opened by ^? or the Fuji menu, at its place in the prototype, scroll bar at the top.
  help: () => { openHelp(); },
};

// Hovers to do after setting the state (cascading submenus).
const HOVERS = {
  menu_file: '.gemmenu .gmi.gmrow >> nth=0',
};

const pw = await loadPlaywright();
const browser = await pw.chromium.launch({
  // Visible scroll bars: Playwright hides them by default (--hide-scrollbars).
  ignoreDefaultArgs: ['--hide-scrollbars'],
  env: { ...process.env, FONTCONFIG_FILE: join(here, 'fonts-noaa.conf') },
});
mkdirSync(outDir, { recursive: true });
const names = only.length ? only : Object.keys(SCENES);
let failed = 0;
for (const name of names) {
  const scene = SCENES[name];
  if (!scene) { console.error('unknown scene: ' + name); failed++; continue; }
  const page = await browser.newPage({ viewport: VIEW, deviceScaleFactor: 1 });
  await page.addInitScript(() => {
    // Zoom 100 %: storage key as the prototype names it (setZoom).
    try { localStorage.setItem('prismYmZoom', '100'); } catch (e) {}
  });
  page.on('pageerror', e => { console.error(name + ': page error: ' + e.message); failed++; });
  await page.goto(pathToFileURL(resolve(html)).href);
  await page.waitForTimeout(300);
  await page.click('#bootbtn');
  await page.waitForTimeout(800);
  // No text cursor and no stray hover: the mouse goes to a corner of the desktop.
  await page.mouse.move(2, VIEW.height - 2);
  await page.evaluate(`(${scene.toString()})()`);
  if (HOVERS[name]) await page.hover(HOVERS[name]);
  await page.waitForTimeout(300);
  const out = join(outDir, name + '.png');
  await page.screenshot({ path: out });
  console.log(out);
  await page.close();
}
await browser.close();
if (!existsSync(outDir) || failed) process.exit(1);
