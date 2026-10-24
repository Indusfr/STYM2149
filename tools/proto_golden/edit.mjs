// STYM2149 — parity data of the sequencer's track edits (notes, ties, chord voices, selections, rotation,
// transposition, blocks, step recording, Euclidean generator), taken from the functions of the prototype's
// page.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Usage: node tools/proto_golden/edit.mjs YM2149_Prototype.html > tests/data/proto/edit.txt
//
// Each case draws a context (track octave, drums, mono, 3 channels, figure, resolution, chord, scale, Snap,
// length) then a sequence of operations; after each operation, the track is summed up by an FNV-1a hash of all
// its rows (LANEKEYS order, 256 steps, 32-bit integers). The C++ test replays the same operations
// (tests/unit/test_ymseq.cpp).
import { readPage, pageFunction, pageConst, patch, rng } from './page.mjs';

const page = readPage(process.argv[2]);
const F = (n) => pageFunction(page, n);

let src = [
  'octCode', 'octOf', 'relNote', 'rectBounds', 'rectNotes', 'rectClear', 'rectMove', 'slideRunsAt', 'noteStart',
  'figLenAt', 'clearFigure', 'stretchNote', 'writeFigure', 'writeNoteAt', 'polyKeys', 'voiceDur',
  'voiceStartCovering', 'voiceRow', 'voiceAt', 'voiceCount', 'promoteVoice', 'stepBusy', 'freeVoice',
  'writeVoiceAt', 'writeChord', 'addVoiceAt', 'delVoiceAt', 'noteLenAt', 'stepBusyAt', 'eraseNoteAt', 'demoteMain',
  'setNoteLen', 'setNoteStart', 'splitNote', 'nextNoteAfter', 'canJoin', 'joinNote', 'copyNoteAt', 'pasteNoteAt',
  'moveVoiceTo', 'moveNoteTo', 'eraseAt', 'blockCopy', 'blockClear', 'blockPaste', 'rotateLane', 'selKey',
  'selNotes', 'transposeSel', 'shiftSel', 'transposeLane', 'clearLane', 'laneHasNotes', 'stepRecord',
  'inScale', 'snapToScale', 'figSteps', 'bjorklund',
].map(F).join('\n');

// ---- decided deviations (DECISIONS.md)
// D-090: Snap does not apply to drum rows.
src = patch(src, '  if (scaleSnap && SCALES[scaleName]){               // mode contraignant : ramener au degre',
  '  if (scaleSnap && SCALES[scaleName] && !(P().effect === 2 || P().effect === 4)){', 'D-090');
// D-091: the stop of the left edge is the nearest previous note.
src = patch(src, '  for (let k = ns; k < s; k++)                       // butee : note precedente\n' +
  '    if (l.pat[k] >= 0){ ns = k + 1; break; }',
  '  for (let k = s - 1; k >= ns; k--)\n    if (l.pat[k] >= 0){ ns = k + 1; break; }', 'D-091');
// API normalisation (not a deviation): "nothing changes" returns -1 (0 is also a valid start).
src = patch(src, '  if (ns === s) return 0;\n  const row = relNote(l, s), vol', '  if (ns === s) return -1;\n  const row = relNote(l, s), vol',
  'API setNoteStart');
// D-092: track transposition, octave 0 and chord voices.
src = patch(src, F('transposeLane'), `function transposeLane(semis){
  if (transposeSel(semis)) return;
  const c = CH[cur];
  if (c.patch.effect === 2 || c.patch.effect === 4) return;
  const l = c.lane, N = c.plen || GLOB.steps;
  const next = [];
  for (let v = 1; v <= POLYMAX; v++){
    const [pk, ok2] = polyKeys(v), row = [];
    for (let st = 0; st < N; st++){
      if (l[pk][st] < 0) { row.push(-1); continue; }
      let abs = (octOf(l[ok2][st]) + 1) * 12 + l[pk][st] + semis;
      if (scaleSnap && SCALES[scaleName]) abs = snapToScale(abs);
      if (abs < 24 || abs > 108) return;
      row.push(abs);
    }
    next.push(row);
  }
  for (let v = 1; v <= POLYMAX; v++){
    const [pk, ok2] = polyKeys(v);
    for (let st = 0; st < N; st++){
      const a = next[v - 1][st];
      if (a < 0) continue;
      const no = Math.max(0, Math.min(9, Math.floor(a / 12) - 1));
      l[pk][st] = a - (no + 1) * 12;
      l[ok2][st] = octCode(no);
    }
  }
}`, 'D-092');
// D-093: Clear also empties the TON row.
src = patch(src, 'l.pnr.fill(0); l.slc.fill(0); l.rtg.fill(0); l.cnd.fill(0);',
  'l.pnr.fill(0); l.slc.fill(0); l.rtg.fill(0); l.cnd.fill(0); l.ton.fill(0);', 'D-093');
// D-094: a cleared block has no chord voices left (pat2-pat6 at -1).
src = patch(src, "l[k][s] = (k === 'pat') ? -1 : (k === 'vol') ? 15 : 0;",
  "l[k][s] = k.startsWith('pat') ? -1 : (k === 'vol') ? 15 : 0;", 'D-094');
// D-095: a recorded octave 0 is written -1.
src = patch(src, '    c.lane.oct[curStep] = oct === c.seqOct ? 0 : oct;', '    c.lane.oct[curStep] = octCode(oct);', 'D-095');
src = patch(src, '    const nn = snapToScale(note);', '    const nn = Math.max(0, Math.min(127, snapToScale(note)));', 'D-095');
// D-280: Snap applies to every note written (added chord voice, chord of the input, moved note or voice, chord voices
// of a transposed selection); a chord voice that lands on a pitch already written at its step is left out.
src += `
function snapRow(off){
  if (!(scaleSnap && SCALES[scaleName]) || P().effect === 2 || P().effect === 4) return off;
  const abs = (CH[cur].seqOct + 1) * 12 + off;
  return off + snapToScale(abs) - abs;
}`;
src = patch(src, '  if (!v) return 0;\n  writeVoiceAt(l, s, row, v);', '  if (!v) return 0;\n  writeVoiceAt(l, s, snapRow(row), v);',
  'D-280 addVoiceAt');
src = patch(src, '  const offs = CHORDS[chordIdx][1];\n  let n = 0;',
  '  const offs = CHORDS[chordIdx][1];\n  const snaps = !!(scaleSnap && SCALES[scaleName]);\n  row = snapRow(row);\n  let n = 0;',
  'D-280 writeChord');
src = patch(src, '    addVoiceAt(l, s, row + iv);',
  '    if (snaps && voiceAt(l, s, snapRow(row + iv))) continue;\n    addVoiceAt(l, s, row + iv);', 'D-280 writeChord');
for (const next of ['  const [pk, ok2] = polyKeys(v);\n  const dk', '  // la place est-elle libre ?'])
  src = patch(src, '  if (c2 === moving.from && off2 === moving.row) return;\n' + next,
    '  if (c2 === moving.from && off2 === moving.row) return;\n  off2 = snapRow(off2);\n' +
    '  if (c2 === moving.from && off2 === moving.row) return;\n' + next, 'D-280 move');
src = patch(src, '    else writeVoiceAt(l, it.s, it.row + semis, it.v);',
  '    else writeVoiceAt(l, it.s, snapRow(it.row + semis), it.v);', 'D-280 transposeSel');

// Item 81: piano roll fixes. A function of the patched source, to replace it whole.
const own = (name) => pageFunction({ html: src, pageFrom: 0 }, name);
// The hold of a note stops at the next note (figLenAt, clearFigure).
src = patch(src, '  while (c + n < N && l.tie[c + n]) n++;',
  '  while (c + n < N && l.pat[c + n] < 0 && l.tie[c + n]) n++;', 'D-282 figLenAt');
src = patch(src, '    l.tie[k] = 0;\n    if (k > c && l.pat[k] >= 0) break;',
  '    if (k > c && l.pat[k] >= 0) break;\n    l.tie[k] = 0;', 'D-282 clearFigure');
// Left edge: the held part of the previous note stops it too; the pitch is kept as it is (no Snap).
src = patch(src, '  for (let k = s - 1; k >= ns; k--)\n    if (l.pat[k] >= 0){ ns = k + 1; break; }',
  '  for (let k = s - 1; k >= ns; k--)\n' +
  '    if (l.pat[k] >= 0 || (!(P().effect === 2 || P().effect === 4) && l.tie[k] && noteStart(k) >= 0)){\n' +
  '      ns = k + 1; break;\n    }',
  'D-282 setNoteStart');
src = patch(src, '  if (ns === s) return -1;\n  const row = relNote(l, s), vol',
  '  if (ns === s) return -1;\n  const p0 = l.pat[s], o0 = l.oct[s], vol', 'D-282 setNoteStart');
src = patch(src, '  writeNoteAt(l, ns, row);', '  l.pat[ns] = p0; l.oct[ns] = o0;', 'D-282 setNoteStart');
// Split: the second half keeps the pitch as it is.
src = patch(src, '  const row = relNote(l, s), vol = l.vol[s], mx = l.mix[s], fx = l.fx[s];\n' +
  '  setNoteLen(s, at - s);\n  writeNoteAt(l, at, row);',
  '  const p0 = l.pat[s], o0 = l.oct[s], vol = l.vol[s], mx = l.mix[s], fx = l.fx[s];\n  setNoteLen(s, at - s);\n' +
  '  l.pat[at] = p0; l.oct[at] = o0;', 'D-282 splitNote');
// Promoted voice: its pitch is copied as it is.
src = patch(src, '  const newRow = voiceRow(l, s0, v), newOct = l[ok2][s0], newLen = voiceDur(l, s0, v);',
  '  const newPat = l[pk][s0], newOct = l[ok2][s0], newLen = voiceDur(l, s0, v);', 'D-282 promoteVoice');
src = patch(src, "  if (drum){ l.pat[s0] = newRow; l.oct[s0] = newOct; }\n  else {\n" +
  '    writeNoteAt(l, s0, newRow);                      // la voix choisie devient fondamentale\n' +
  '    l.oct[s0] = newOct;\n    setNoteLen(s0, newLen);\n  }',
  '  l.pat[s0] = newPat; l.oct[s0] = newOct;\n  if (!drum) setNoteLen(s0, newLen);', 'D-282 promoteVoice');
// Chord voices held more than 64 steps.
src = patch(src, '  for (let k = s; k >= 0 && k > s - 64; k--){', '  for (let k = s; k >= 0; k--){',
  'D-282 voiceStartCovering');
src = patch(src, '    for (let k = Math.max(0, s - 64); k <= s; k++)', '    for (let k = 0; k <= s; k++)',
  'D-282 stepBusy');
// Move: the note's own start is not in the way, the held part of another note is; a held Digidrum hit keeps its
// length; the rows of the note's start go with it, the step it leaves gets back what it held.
src = patch(src, '  for (let k = 0; k < n && c2 + k < N; k++){\n' +
  '    if (l.pat[c2 + k] >= 0 && c2 + k !== moving.from) return;\n' +
  '    if (voiceAt(l, c2 + k, off2)) return;             // une voix occupe deja cette hauteur\n' +
  '  }\n  // liberer l\'emplacement d\'origine\n  if (!moving.drum) clearFigure(moving.from);\n' +
  '  l.pat[moving.from] = -1; l.oct[moving.from] = 0;\n  writeNoteAt(l, c2, off2);\n  if (!moving.drum){',
  `  const drumT = P().effect === 2 || P().effect === 4;
  for (let k = 0; k < n && c2 + k < N; k++){
    const t = c2 + k;
    if (l.pat[t] >= 0 && t !== moving.from) return;
    for (let v = 2; v <= POLYMAX; v++) if (voiceRow(l, t, v) === off2) return;
    if (!drumT && l.pat[t] < 0 && l.tie[t]){ const ns = noteStart(t); if (ns >= 0 && ns !== moving.from) return; }
  }
  const ties = !moving.drum || n > 1;
  const rows = drumT ? ['vol', 'mix', 'fx', 'slide', 'slc', 'oct', 'ton'] : ['vol', 'mix', 'fx', 'slide', 'slc'];
  if (!moving.under) moving.under = { vol: 15, mix: 0, fx: 0, slide: 0, slc: 0, oct: 0, ton: 0 };
  const carried = rows.map((k) => l[k][moving.from]);
  if (ties) clearFigure(moving.from);
  l.pat[moving.from] = -1; l.oct[moving.from] = 0;
  for (const k of rows) l[k][moving.from] = moving.under[k];
  for (const k of rows) moving.under[k] = l[k][c2];
  writeNoteAt(l, c2, off2);
  rows.forEach((k, i) => { l[k][c2] = carried[i]; });
  if (ties){`, 'D-282 moveNoteTo');
// A note lengthened over the next one turns it into a chord voice only where chord voices are played (not in the
// 3-channel modes).
src = patch(src, '  const poly = !P().mono && P().effect !== 2 && P().effect !== 4;',
  '  const poly = !P().mono && !GLOB.mode3ch && P().effect !== 2 && P().effect !== 4;', 'D-282 setNoteLen');
// Under Snap, a transposed note never stays on its pitch (the nearest degree, or the next one the way of the
// transposition); the whole track goes from B0 to C9 like the grid and transposeSel.
src += `
function snapTransposed(abs, semis){
  const to = snapToScale(abs + semis);
  if (to !== abs || !semis) return to;
  const dir = Math.sign(semis);
  for (let d = 1; d <= 11; d++) if (inScale(abs + semis + dir * d)) return abs + semis + dir * d;
  return to;
}
function transposedRow(off, semis){
  if (!(scaleSnap && SCALES[scaleName]) || P().effect === 2 || P().effect === 4) return off + semis;
  const abs = (CH[cur].seqOct + 1) * 12 + off;
  return off + snapTransposed(abs, semis) - abs;
}`;
src = patch(src, '      let abs = (octOf(l[ok2][st]) + 1) * 12 + l[pk][st] + semis;\n' +
  '      if (scaleSnap && SCALES[scaleName]) abs = snapToScale(abs);\n      if (abs < 24 || abs > 108) return;',
  '      let abs = (octOf(l[ok2][st]) + 1) * 12 + l[pk][st];\n' +
  '      abs = scaleSnap && SCALES[scaleName] ? snapTransposed(abs, semis) : abs + semis;\n' +
  '      if (abs < PLAY_LO || abs > MAP_HI) return;', 'D-282 transposeLane');
src = patch(src, own('transposeSel'), `function transposeSel(semis){
  const l = LN(), items = selNotes();
  if (!items.length) return 0;
  const base = (CH[cur].seqOct + 1) * 12;
  const rows = items.map((it) => transposedRow(it.row, semis));
  for (const r of rows) if (r + base < PLAY_LO || r + base > MAP_HI) return 1;
  items.forEach((it, i) => {
    if (it.v === 1) writeNoteAt(l, it.s, rows[i]);
    else writeVoiceAt(l, it.s, rows[i], it.v);
  });
  return 1;
}`, 'D-282 transposeSel');
// Note selection shifted: all or nothing, no note lands on another one, no voice is lost.
src = patch(src, own('shiftSel'), `function shiftSel(dir){
  const l = LN(), items = selNotes(), N = CH[cur].plen || GLOB.steps;
  if (!items.length) return 0;
  for (const it of items){ const ns = it.s + dir; if (ns < 0 || ns >= N) return 1; }
  const drumT = P().effect === 2 || P().effect === 4, before = {};
  for (const k of Object.keys(l)) before[k] = l[k].slice();
  const cache = items.map(it=>({ row: it.row, v: it.v, s: it.s, len: voiceDur(l, it.s, it.v),
                                 ln1: it.v === 1 ? noteLenAt(it.s) : 0 }));
  for (const it of cache){
    if (it.v === 1){
      for (let k = 0; k < it.ln1; k++){ l.pat[it.s + k] = -1; l.oct[it.s + k] = 0; l.tie[it.s + k] = 0; }
    } else delVoiceAt(l, it.s, it.v);
  }
  const next = [];
  for (const it of cache){
    const ns = it.s + dir;
    let free = true;
    if (it.v === 1){
      const row = snapRow(it.row);
      for (let k = 0; k < Math.max(1, it.ln1) && ns + k < N && free; k++){
        const t = ns + k;
        if (l.pat[t] >= 0 || (!drumT && l.tie[t] && noteStart(t) >= 0)) free = false;
        for (let v = 2; v <= POLYMAX && free; v++) if (voiceRow(l, t, v) === row) free = false;
      }
      if (free){ writeNoteAt(l, ns, it.row); setNoteLen(ns, it.ln1); next.push(selKey(ns, 1)); }
    } else if (voiceAt(l, ns, snapRow(it.row))) free = false;
    else {
      const v2 = addVoiceAt(l, ns, it.row);
      if (v2){ l['dur' + v2][ns] = it.len; next.push(selKey(ns, v2)); } else free = false;
    }
    if (!free){ for (const k of Object.keys(before)) l[k] = before[k]; return 1; }
  }
  noteSel = next;
  return 1;
}`, 'D-282 shiftSel');
// Selection box: the chord voices that start in it are part of it; a move is all or nothing (inside the pattern and
// the playable range, no landing on a note outside the box or on its held part), goes the way of the move under
// Snap, and the box follows the notes; a pasted hold stops before the next root.
src = patch(src, own('rectNotes'), `function rectNotes(){
  const R = rectBounds();
  if (!R) return [];
  const l = LN(), out = [];
  for (let s = R.a; s <= R.b; s++){
    if (l.pat[s] >= 0){
      const row = relNote(l, s);
      if (row >= R.r1 && row <= R.r2){
        let d = 1;
        while (l.tie[s + d] && l.pat[s + d] < 0) d++;
        out.push({ s, row, len: l.tie[s] ? d : 1, vol: l.vol[s], slide: l.slide[s], slc: l.slc[s], v: 1 });
      }
    }
    for (let v = 2; v <= POLYMAX; v++){
      const row = voiceRow(l, s, v);
      if (row === null || row < R.r1 || row > R.r2) continue;
      out.push({ s, row, len: voiceDur(l, s, v), vol: 15, slide: 0, slc: 0, v });
    }
  }
  return out;
}`, 'D-282 rectNotes');
src = patch(src, own('rectClear'), `function rectClear(){
  const l = LN();
  for (const n of rectNotes()){
    if (n.v >= 2){ delVoiceAt(l, n.s, n.v); continue; }
    for (let k = 0; k < n.len; k++){ l.pat[n.s + k] = -1; l.oct[n.s + k] = 0; l.tie[n.s + k] = 0; }
  }
}
function placeRectNote(l, s, n){
  if (n.v >= 2){
    const row = snapRow(n.row);
    const v = voiceAt(l, s, row) ? 0 : freeVoice(l, s);
    if (!v) return 0;
    writeVoiceAt(l, s, row, v);
    l['dur' + v][s] = Math.max(1, n.len);
    return 1;
  }
  writeNoteAt(l, s, n.row);
  l.vol[s] = n.vol; l.slide[s] = n.slide; l.slc[s] = n.slc;
  let len = n.len;
  for (let k = 1; k < len; k++) if (s + k >= NSTEP || l.pat[s + k] >= 0){ len = k; break; }
  if (len > 1)
    for (let k = 0; k < len && s + k < NSTEP; k++){ if (k){ l.pat[s + k] = -1; l.oct[s + k] = 0; } l.tie[s + k] = 1; }
  return 1;
}`, 'D-282 rectClear');
src = patch(src, own('rectMove'), `function rectMove(ds, dr){
  const R = rectBounds();
  if (!R || (!ds && !dr)) return;
  const N = CH[cur].plen || GLOB.steps, l = LN();
  const src = rectNotes();
  if (!src.length) return;
  const drumT = P().effect === 2 || P().effect === 4, base = (CH[cur].seqOct + 1) * 12;
  const dst = src.map((n) => ({ ...n, s: n.s + ds, row: dr ? transposedRow(n.row, dr) : snapRow(n.row) }));
  for (const n of dst){
    if (n.s < 0 || n.s + (n.v >= 2 ? 1 : n.len) > N) return;
    if (drumT ? n.row < 0 || n.row > 7 : n.row + base < PLAY_LO || n.row + base > MAP_HI) return;
  }
  const before = {};
  for (const k of Object.keys(l)) before[k] = l[k].slice();
  rectClear();
  for (const n of dst){
    let free = true;
    if (n.v >= 2) free = !voiceAt(l, n.s, n.row) && !!freeVoice(l, n.s);
    else
      for (let k = 0; k < n.len && free; k++){
        const t = n.s + k;
        if (l.pat[t] >= 0 || (!drumT && l.tie[t] && noteStart(t) >= 0)) free = false;
        for (let v = 2; v <= POLYMAX && free; v++) if (voiceRow(l, t, v) === n.row) free = false;
      }
    if (!free || !placeRectNote(l, n.s, n)){ for (const k of Object.keys(before)) l[k] = before[k]; return; }
  }
  let lo = R.r1 + dr, hi = R.r2 + dr;
  for (const n of dst){ lo = Math.min(lo, n.row); hi = Math.max(hi, n.row); }
  rectSel = { a: R.a + ds, b: R.b + ds, r1: lo, r2: hi };
}`, 'D-282 rectMove');

// Left edge of a chord voice (pointerover handler of the grid), with deviation D-096: the voice goes to a free
// voice if its number is taken at the new start.
src += `
function voiceSizeL(sizing, c2){
  const l = LN(), v = sizing.voice, dk = 'dur' + v;
  const ns = Math.max(0, Math.min(sizing.from + sizing.len - 1, c2));
  if (ns === sizing.from) return 0;
  const end = sizing.from + voiceDur(l, sizing.from, v);
  if (voiceAt(l, ns, sizing.row) !== 0) return 0;
  const v2 = l['pat' + v][ns] < 0 ? v : freeVoice(l, ns);
  if (!v2) return 0;
  const [pk, ok2] = polyKeys(v), [pk2, ok22] = polyKeys(v2);
  const pv = l[pk][sizing.from], ov = l[ok2][sizing.from];
  l[pk][sizing.from] = -1; l[ok2][sizing.from] = 0; l[dk][sizing.from] = 0;
  l[pk2][ns] = pv; l[ok22][ns] = ov; l['dur' + v2][ns] = end - ns;
  sizing.from = ns; sizing.len = end - ns; sizing.voice = v2;
  return 1;
}
function voiceSizeR(sizing, c2){
  const l = LN(), v = sizing.voice, dk = 'dur' + v;
  const n2 = Math.max(1, c2 - sizing.from + 1);
  if (n2 === voiceDur(l, sizing.from, v)) return 0;
  l[dk][sizing.from] = n2;
  return 1;
}`;

const consts = ['FIGS', 'RESOS', 'SCALES', 'CHORDS', 'POLYKEYS', 'LANEKEYS'].map((n) => pageConst(page, n)).join('\n');
const sandbox = new Function(`
  const NSTEP = 256, POLYMAX = 6, PLAY_LO = 23, MAP_HI = 120;
  ${consts}
  let CH = [], cur = 0, GLOB = { steps: 16, reso: 2, mode3ch: 0 };
  let figIdx = 0, chordIdx = -1, scaleRoot = 0, scaleName = 'Off', scaleSnap = 0;
  let noteSel = [], rectSel = null, rectClip = null, noteClip = null, moving = null, blockClip = null;
  let selA = -1, selB = -1, selRow = null, stepRecOn = 1, curStep = 0;
  const selLo = ()=>Math.min(selA, selB), selHi = ()=>Math.max(selA, selB);
  const SELKEYS = ()=>selRow ? [selRow] : LANEKEYS;
  const P = () => CH[cur].patch, LN = () => CH[cur].lane;
  const $ = () => ({ style: { display: 'block' } });
  function pushUndo(){} function refreshSeqGrid(){} function sendChan(){} function armChan(){}
  function previewNote(){} function markDirty(){} function ensureVisible(){}
  ${src}
  return {
    LANEKEYS, CHORDS,
    set: (o) => { if ('CH' in o) CH = o.CH; if ('cur' in o) cur = o.cur; if ('GLOB' in o) GLOB = o.GLOB;
      if ('figIdx' in o) figIdx = o.figIdx; if ('chordIdx' in o) chordIdx = o.chordIdx;
      if ('scaleRoot' in o) scaleRoot = o.scaleRoot; if ('scaleName' in o) scaleName = o.scaleName;
      if ('scaleSnap' in o) scaleSnap = o.scaleSnap; if ('noteSel' in o) noteSel = o.noteSel;
      if ('rectSel' in o) rectSel = o.rectSel; if ('moving' in o) moving = o.moving;
      if ('selA' in o) selA = o.selA; if ('selB' in o) selB = o.selB; if ('selRow' in o) selRow = o.selRow;
      if ('curStep' in o) curStep = o.curStep; if ('noteClip' in o) noteClip = o.noteClip; },
    get: () => ({ noteSel, moving, rectSel, noteClip, blockClip, curStep }),
    fn: { relNote, rectNotes, rectClear, rectMove, placeRectNote, noteStart, figLenAt, clearFigure, stretchNote,
      writeFigure, writeNoteAt, voiceDur, voiceStartCovering, voiceRow, voiceAt, voiceCount, promoteVoice, stepBusy,
      freeVoice, writeVoiceAt, writeChord, addVoiceAt, delVoiceAt, noteLenAt, stepBusyAt, eraseNoteAt,
      demoteMain, setNoteLen, setNoteStart, splitNote, nextNoteAfter, joinNote, copyNoteAt, pasteNoteAt,
      moveNoteTo, eraseAt, blockCopy, blockClear, blockPaste, rotateLane, transposeLane, clearLane,
      laneHasNotes, stepRecord, slideRunsAt, voiceSizeL, voiceSizeR, bjorklund, snapToScale, figSteps },
  };
`)();
const { LANEKEYS, fn } = sandbox;
const SCALE_NAMES = Object.keys(new Function(pageConst(page, 'SCALES') + ' return SCALES;')());

function newLane() {
  const l = {};
  for (const k of LANEKEYS) l[k] = new Array(256).fill(k.startsWith('pat') ? -1 : k === 'vol' ? 15 : 0);
  return l;
}
function hash(l) {
  let h = 0x811c9dc5;
  for (const k of LANEKEYS)
    for (let s = 0; s < 256; s++) {
      const v = l[k][s] | 0;
      for (let b = 0; b < 4; b++) { h ^= (v >>> (8 * b)) & 0xff; h = Math.imul(h, 0x01000193) >>> 0; }
    }
  return h.toString(16);
}
function dump(l) {
  const parts = [];
  LANEKEYS.forEach((k, ki) => {
    const d = k.startsWith('pat') ? -1 : k === 'vol' ? 15 : 0;
    for (let s = 0; s < 256; s++) if ((l[k][s] | 0) !== d) parts.push(ki + ':' + s + ':' + (l[k][s] | 0));
  });
  return parts.join(' ');
}

const out = ['# Generated by tools/proto_golden/edit.mjs from YM2149_Prototype.html (the page\'s edit functions).',
  '# Deviations applied: D-090 to D-096, D-280 and the piano roll fixes of D-282 (DECISIONS.md).'];
const R = rng(0x5eed2149);
const NCASES = 160, NOPS = 36;
for (let c = 0; c < NCASES; c++) {
  const drum = R.chance(0.2), effect = drum ? R.pick([2, 4]) : R.pick([0, 0, 1, 3]);
  const mono = R.chance(0.2) ? 1 : 0, three = R.chance(0.1) ? 1 : 0;
  const T = R.int(1, 7), N = R.pick([16, 16, 16, 32, 64, 7, 12, 128, 256, 3]);
  const fig = R.int(0, 12), reso = R.int(0, 6), chord = R.chance(0.5) ? -1 : R.int(0, 38);
  const scale = R.chance(0.5) ? 0 : R.int(1, 25), root = R.int(0, 11), snap = scale && R.chance(0.6) ? 1 : 0;
  const lane = newLane();
  sandbox.set({ CH: [{ patch: { effect, mono }, lane, seqOct: T, plen: 0 }], cur: 0,
    GLOB: { steps: N, reso, mode3ch: three }, figIdx: fig, chordIdx: chord, scaleRoot: root,
    scaleName: SCALE_NAMES[scale], scaleSnap: snap, noteSel: [], rectSel: null, moving: null });
  out.push(`case ${c} ${T} ${drum ? 1 : 0} ${mono} ${three} ${fig} ${reso} ${chord} ${scale} ${root} ${snap} ${N}`);
  const S = () => R.int(0, N - 1);
  const ROW = () => drum ? R.int(0, 7) : R.int(-30, 50);
  // seed: a few notes placed as by a click
  for (let k = R.int(2, 10); k > 0; k--) {
    const s = S(), row = ROW();
    fn.writeNoteAt(lane, s, row); fn.writeChord(lane, s, row); fn.writeFigure(s);
    out.push(`op click ${s} ${row} = 0 ${hash(lane)}`);
  }
  const notes = () => { const a = []; for (let s = 0; s < N; s++) if (lane.pat[s] >= 0) a.push(s); return a; };
  const anyNote = () => { const a = notes(); return a.length ? R.pick(a) : S(); };
  const voices = () => {
    const a = [];
    for (let s = 0; s < N; s++) for (let v = 1; v <= 6; v++) if (lane[v === 1 ? 'pat' : 'pat' + v][s] >= 0) a.push([s, v]);
    return a;
  };
  for (let o = 0; o < NOPS; o++) {
    const kind = R.int(0, 33);
    let line = '', ret = 0;
    if (kind === 0) { const s = S(), row = ROW(); fn.writeNoteAt(lane, s, row); fn.writeChord(lane, s, row); fn.writeFigure(s); line = `click ${s} ${row}`; }
    else if (kind === 1) { const s = S(), row = ROW(); fn.writeNoteAt(lane, s, row); line = `note ${s} ${row}`; }
    else if (kind === 2) { const s = anyNote(), row = ROW(); ret = fn.addVoiceAt(lane, s, row); line = `voice ${s} ${row}`; }
    else if (kind === 3) { const s = S(), v = R.int(1, 6); fn.delVoiceAt(lane, s, v); line = `delv ${s} ${v}`; }
    else if (kind === 4) { const s = anyNote(); fn.writeFigure(s); line = `fig ${s}`; }
    else if (kind === 5) { const s = S(); fn.clearFigure(s); line = `clrfig ${s}`; }
    else if (kind === 6) { const s = anyNote(), to = R.int(0, N + 3); fn.stretchNote(s, to); line = `stretch ${s} ${to}`; }
    else if (kind === 7) { const s = anyNote(), n = R.int(-1, 20); fn.setNoteLen(s, n); line = `len ${s} ${n}`; }
    else if (kind === 8) { const s = anyNote(), ns = R.int(-2, N); ret = fn.setNoteStart(s, ns); line = `start ${s} ${ns}`; }
    else if (kind === 9) { const s = anyNote(), at = s + R.int(-1, 5); ret = fn.splitNote(s, at); line = `split ${s} ${at}`; }
    else if (kind === 10) { const s = anyNote(); ret = fn.joinNote(s); line = `join ${s}`; }
    else if (kind === 11) { const s = S(); fn.eraseAt(s); line = `erase ${s}`; }
    else if (kind === 12) {
      const vs = voices(); let s = S(), row = ROW();
      if (vs.length && R.chance(0.7)) { const [s0, v] = R.pick(vs); s = s0; row = fn.voiceRow(lane, s0, v); }
      ret = fn.eraseNoteAt(lane, s, row, drum); line = `erasenote ${s} ${row}`;
    }
    else if (kind === 13) {
      const vs = voices(); let s = S(), row = ROW();
      if (vs.length && R.chance(0.6)) { const [s0, v] = R.pick(vs); s = s0 + R.int(0, 2); row = fn.voiceRow(lane, s0, v); }
      s = Math.min(s, N - 1);
      ret = fn.stepBusyAt(lane, s, row, drum); line = `busyat ${s} ${row}`;
    }
    else if (kind === 14) { const s = S(); ret = fn.stepBusy(lane, s); line = `busy ${s}`; }
    else if (kind === 15) { const a = notes(); if (!a.length) continue; const s = R.pick(a); ret = fn.demoteMain(lane, s); line = `demote ${s}`; }
    else if (kind === 16) {
      const vs = voices().filter(([, v]) => v >= 2); if (!vs.length) continue;
      const [s0, v] = R.pick(vs), row = fn.voiceRow(lane, s0, v), c2 = Math.min(N - 1, s0 + R.int(0, 2));
      const hit = fn.voiceStartCovering(lane, c2, row); if (!hit) continue;
      const before = hash(lane); fn.promoteVoice(c2, row, hit); ret = hash(lane) !== before ? 1 : 0;
      line = `promote ${c2} ${row} ${hit.start} ${hit.voice}`;
    }
    else if (kind === 17 || kind === 18) {
      const a = notes(); if (!a.length) continue;
      const s = R.pick(a), cut = kind === 18 ? 1 : 0;
      fn.copyNoteAt(s, cut);
      const s2 = R.int(-1, N), useRow = R.chance(0.5), row = useRow ? ROW() : undefined;
      ret = fn.pasteNoteAt(s2, row);
      line = `cp ${s} ${cut} ${s2} ${useRow ? row : 'x'}`;
    }
    else if (kind === 19) {
      // move: root note (or chord voice), sequence of hovered positions
      const vs = voices(); if (!vs.length) continue;
      const [s0, v] = R.pick(vs);
      let m;
      if (v === 1) {
        const row = drum ? lane.pat[s0] : fn.relNote(lane, s0);
        m = { from: s0, row, len: drum ? 1 : fn.noteLenAt(s0), moved: 0, drum: drum ? 1 : 0, grab: 0 };
      } else {
        if (drum) continue;  // drums: the grid does not grab the voices (drumRows)
        m = { from: s0, row: fn.voiceRow(lane, s0, v), len: fn.voiceDur(lane, s0, v), moved: 0, drum: 0, voice: v, grab: 0 };
      }
      line = `move ${m.from} ${m.row} ${m.len} ${m.drum} ${m.voice || 0}`;
      sandbox.set({ moving: m });
      for (let k = R.int(1, 4); k > 0; k--) {
        const c2 = R.int(-2, N + 1), r2 = m.row + (drum ? R.int(-2, 2) : R.int(-5, 5));
        fn.moveNoteTo(c2, r2);
        line += ` ${c2} ${r2}`;
      }
      const mm = sandbox.get().moving;
      ret = [mm.from, mm.row, mm.moved ? 1 : 0, mm.voice || 0].join(',');
    }
    else if (kind === 20) {
      const vs = voices().filter(([, v]) => v >= 2); if (!vs.length || drum) continue;
      const [s0, v] = R.pick(vs), sz = { from: s0, row: fn.voiceRow(lane, s0, v), len: fn.voiceDur(lane, s0, v), voice: v };
      const c2 = R.int(0, N - 1), left = R.chance(0.5);
      ret = left ? fn.voiceSizeL(sz, c2) : fn.voiceSizeR(sz, c2);
      line = `vsize ${left ? 'L' : 'R'} ${s0} ${v} ${sz.row} ${c2}`;
      ret = ret + ',' + sz.from + ',' + sz.voice;
    }
    else if (kind === 21 || kind === 22 || kind === 23) {
      const a = R.int(0, N - 1), b = R.int(0, N - 1), r1 = ROW(), r2 = ROW();
      sandbox.set({ rectSel: { a, b, r1, r2 } });
      if (kind === 21) {
        const ds = R.int(-3, 3), dr = R.int(-3, 3);
        const before = hash(lane); fn.rectMove(ds, dr); ret = hash(lane) !== before ? 1 : 0;
        const rs = sandbox.get().rectSel;
        line = `rmove ${a} ${b} ${r1} ${r2} ${ds} ${dr}`;
        ret += ',' + [Math.min(rs.a, rs.b), Math.max(rs.a, rs.b), Math.min(rs.r1, rs.r2), Math.max(rs.r1, rs.r2)].join(',');
      } else if (kind === 22) { fn.rectClear(); line = `rclear ${a} ${b} ${r1} ${r2}`; }
      else {
        const lo = Math.min(a, b), clip = fn.rectNotes().map((x) => ({ ...x, s: x.s - lo }));
        const at = R.int(0, N - 1), N2 = N;
        for (const x of clip) {  // paste (editClip 'paste', same loop as the menu and Ctrl+V)
          const s = at + x.s;
          if (s < 0 || s + (x.v >= 2 ? 1 : x.len) > N2) continue;
          fn.placeRectNote(lane, s, x);
        }
        line = `rpaste ${a} ${b} ${r1} ${r2} ${at}`;
        ret = clip.length;
      }
    }
    else if (kind === 24 || kind === 25) {
      const vs = voices(); if (!vs.length) continue;
      const keys = [];
      for (let k = R.int(1, 4); k > 0; k--) { const [s, v] = R.pick(vs); keys.push(fn.selKey ? s + ':' + v : s + ':' + v); }
      if (R.chance(0.2)) keys.push(S() + ':' + R.int(1, 6));  // stale key
      const uniq = [...new Set(keys)];
      sandbox.set({ noteSel: uniq.slice() });
      const arg = kind === 24 ? R.pick([1, -1, 12, -12, 7]) : R.pick([1, -1]);
      if (kind === 24) { fn.transposeLane(arg); line = `tsel ${arg} ${uniq.join(',')}`; }
      else { fn.rotateLane(arg); line = `ssel ${arg} ${uniq.join(',')}`; ret = sandbox.get().noteSel.join(',') || '-'; }
      sandbox.set({ noteSel: [] });
    }
    else if (kind === 26) { const d = R.pick([1, -1]); fn.rotateLane(d); line = `rot ${d}`; }
    else if (kind === 27) { const t = R.pick([1, -1, 12, -12, 5, -24]); fn.transposeLane(t); line = `tr ${t}`; }
    else if (kind === 28) { if (!R.chance(0.15)) continue; fn.clearLane(); line = 'clear'; }
    else if (kind === 29 || kind === 30) {
      const a = R.int(0, N - 1), b = R.int(0, N - 1), rowIdx = R.chance(0.6) ? -1 : R.int(0, 15);
      sandbox.set({ selA: a, selB: b, selRow: rowIdx < 0 ? null : LANEKEYS[rowIdx] });
      if (kind === 29) { const at = R.int(0, N - 1); fn.blockCopy(0); fn.blockPaste(at); line = `bpaste ${a} ${b} ${rowIdx} ${at}`; }
      else { fn.blockClear(); line = `bclear ${a} ${b} ${rowIdx}`; }
      sandbox.set({ selRow: null });
    }
    else if (kind === 31) { const s = S(), note = R.int(0, 127), vel = R.int(0, 127); sandbox.set({ curStep: s }); fn.stepRecord(note, vel); line = `rec ${s} ${note} ${vel}`; }
    else if (kind === 32) { const s = S(); ret = (fn.slideRunsAt(s) ? 1 : 0) + ',' + fn.noteStart(s) + ',' + fn.figLenAt(s) + ',' + fn.noteLenAt(s) + ',' + fn.nextNoteAfter(s) + ',' + fn.voiceCount(lane, s) + ',' + fn.freeVoice(lane, s); line = `query ${s}`; }
    else if (kind === 33) {
      // values of free rows (painted in the grid) so that rotation, blocks and erasing move them
      const s = S(), k = R.pick(['slide', 'afl', 'mix', 'fx', 'slt', 'cut', 'gat', 'pnr', 'rtg', 'cnd', 'slc', 'ton', 'vol', 'tie']);
      const v = { slide: 1, afl: 1, mix: R.int(0, 3), fx: R.int(0, 4), slt: R.int(0, 8), cut: 1, gat: R.int(0, 100),
        pnr: R.int(-10, 10) * 10 + 101, rtg: R.int(0, 3), cnd: R.int(0, 7), slc: R.int(0, 12), ton: R.int(-12, 12),
        vol: R.int(0, 15), tie: R.int(0, 1) }[k];
      lane[k][s] = v; line = `set ${LANEKEYS.indexOf(k)} ${s} ${v}`;
    }
    out.push(`op ${line} = ${ret === true ? 1 : ret === false ? 0 : ret} ${hash(lane)}`);
  }
  out.push('lane ' + dump(lane));
}

// Euclidean generator: Bjorklund patterns.
for (let n = 1; n <= 32; n++)
  for (let k = 0; k <= n; k++) out.push(`bjork ${k} ${n} ${fn.bjorklund(k, n).join('')}`);

process.stdout.write(out.join('\n') + '\n');
