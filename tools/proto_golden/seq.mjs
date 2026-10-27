// STYM2149 — parity data of sequencer playback: voice operations (triggers, releases, slides, MIX, FX, step
// panning, pattern switches) timed to the sample, taken from the prototype's worklet.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Usage: node tools/proto_golden/seq.mjs YM2149_Prototype.html > tests/data/proto/seq.txt
//
// Each case draws a mode (16 voices, 3 channels, dual chip), a clock (internal with swing and resolution, or MIDI
// clock), tracks (generator, gate, polymeter, ADSR, arpeggio) and three patterns, then plays the worklet in blocks
// of 128 samples while sending it Start, pattern switches, Stop (and Start/Stop/SPP/Continue on MIDI clock). The
// worklet is instrumented: each sequencer operation on a voice is logged with its position. The C++ test replays
// the same cases on the engine (tests/unit/test_proto_seq.cpp).
//
// The decided deviations (DECISIONS.md, D-097 to D-106, D-205, D-281, D-343, D-345 and D-421) are applied to the
// worklet source by checked replacements; the prototype's random draws (CND, Random arpeggio, slide seeds) are replaced
// by the draws tied to the engine's playback position (D-104).
import { loadPrototype } from './load.mjs';
import { patch, rng } from './page.mjs';

const SR = 48000, BLOCK = 128;

function transform(src) {
  const P = (from, to, why) => { src = patch(src, from, to, why); };
  // Chords: gate of the voices of a step, randomness tied to the position.
  P('polyLeft: new Int16Array(8),', 'polyLeft: new Int16Array(8), polyOff: new Float64Array(8), rand: 1,', 'D-102');
  // D-100: the pattern switch applies all the rows.
  P(`      t.pat = e.pat;
      for (const k of ['tie','slide','afl','vol','mix','slotRow','cut','fx','gat','pnr'])
        if (e[k]) t[k] = Uint8Array.from(e[k]);`, `      for (const k in e) if (k !== 'i') t[k] = e[k].slice();`, 'D-100');
  P('    this.patApplied = q.idx;\n', '    this.patApplied = q.idx;\n    this.curPat = q.idx; this.log(\'Q\', -1, q.idx);\n',
    'log Q');
  // D-098: OCT and TON also transpose the Digidrum.
  P('v.timerFreq = this.drumRate * Math.pow(2, (v.note - 60) / 12);',
    'v.timerFreq = this.drumRate * Math.pow(2, (v.note - 60) / 12) * (v.drumTr || 1);', 'D-098');
  // D-102: the chord voices of a Digidrum play their row; the CRV curve does not survive a retrigger.
  P("? (tag === 'seq' ? (note % 12) % 8", "? ((tag === 'seq' || tag === 'poly') ? (note % 12) % 8", 'D-102 slot');
  P('      slot: note % 8\n    });', '      slot: note % 8, slcOv: 0\n    });', 'slcOv');
  // D-099: step duration and MIDI pulses per step according to the resolution.
  P(`    const stepSamp = (this.clk.midi ? 6 * this.clk.spt : this.SR * 60 / (q.bpm * 4))`,
    `    const stepSamp = (this.clk.midi ? this.cps() * this.clk.spt : this.SR * 60 / (q.bpm * (q.spb || 4)))`, 'D-099');
  P(`    for (let i = 0; i < max; i++) this.laneStep(i, stepSamp);
  }`, `    this.log('@', -1, q.step, q.stepAbs, this.curPat | 0, stepSamp);
    for (let i = 0; i < max; i++) this.laneStep(i, stepSamp);
  }`, 'log @');
  // Start of the step: step randomness (D-104), drum transposition (D-098), ageing of the chords (D-102).
  P(`    const st = ((this.seq.stepAbs % L2) + L2) % L2;
    let n = c.pat[st];`, `    const st = ((this.seq.stepAbs % L2) + L2) % L2;
    c.rand = (this.hash(this.seq.stepAbs, i, 2) | 1) >>> 0;
    const dr0 = c.effect === 2 || c.effect === 4;
    c.drumTrPend = dr0 ? Math.pow(2, ((c.oct2r ? c.oct2r[st] : 0) * 12 + (c.ton ? c.ton[st] : 0)) / 12) : 1;
    if (!dr0) this.polyStep(i);
    let n = c.pat[st];`, 'D-098 D-102 D-104');
  P(`      this.polyTrigger(i, c, st, c.level, c.mix ? c.mix[st] : 0,`,
    `      this.polyTrigger(i, c, st, c.level * c.vol[st] / 15, c.mix ? c.mix[st] : 0,`, 'D-102 chord without root');
  P(`      const cyc = Math.floor(this.seq.stepAbs / L2);
      if`, `      const cyc = Math.floor(this.seq.stepAbs / L2);
      this.cndI = i;
      if`, 'D-104 CND');
  for (const [k, pr] of [[5, '0.25'], [6, '0.5'], [7, '0.75']])
    P(`case ${k}: return Math.random() < ${pr};`, `case ${k}: return this.hashU(this.seq.stepAbs, this.cndI, 1) < ${pr};`, 'D-104');
  // D-101: repeated hits keep the sound of the step.
  P(`               clk: 0, note: n, i };`, `               clk: 0, note: n, i, lvl: c.level * c.vol[st] / 15 * 0.92,
               eff: (dr0 || !c.fx[st]) ? -1 : [ -1, 0, 1, 3, 4 ][c.fx[st]], mix: dr0 ? 0 : c.mix[st] };`, 'D-101');
  P(`    c.panEff = pvv ? (pvv - 101) / 100 : null;`, `    this.setPan(i, pvv ? (pvv - 101) / 100 : null);`, 'log P');
  // D-102: an empty step does not cut the chord voices (they last their steps).
  P(`      if (n < 0 && !c.tie[st] && !c.slide[st]) this.killPoly(i);   // pas vide : l'accord tombe\n`, '', 'D-102');
  // Ends of slides; D-105: the note that has arrived settles on its target.
  P(`      if (arpHere) v.glide = 0;`, `      if (arpHere) { v.glide = 0; this.log('G', vi, 0); }`, 'log G');
  P(`        v.glide = 0;
        this.killLane(vi, 1);`, `        v.glide = 0; this.log('G', vi, 0);
        this.killLane(vi, 1);`, 'log G');
  P(`      else v.glide = 0;                                // deja arrivee : on tient la note`, `      else {
        v.glide = 0;
        if (v.fTarget > 0 && v.fCur !== v.fTarget) {
          v.fCur = v.fTarget;
          v.TP = Math.max(1, Math.min(4095, Math.round(125000 / v.fCur)));
          if (v.effect === 1) v.timerFreq = v.fCur * v.sidRatio;
          else if (v.effect === 3) v.timerFreq = v.fCur;
        }
        this.log('G', vi, 1);
      }`, 'D-105');
  P(`      v.level = this.patch.mode3ch ? 1 : lvl;`, `      v.level = this.patch.mode3ch ? 1 : lvl;
      this.log('L', vi, 0, 0, v.level);`, 'log L');
  P(`      this.polyStep(i);                              // les voix d'accord vieillissent\n`, '', 'D-102');
  P(`        if (v && v.on) this.relVoice(v, 64);
      }
    }
  }
  polyAny`, `        if (v && v.on) { this.relVoice(v, 64); this.log('R', this.polyVoice(i, k)); }
      }
    }
  }
  polyAny`, 'log R');
  P(`    if (!drum) this.killPoly(i);                     // un kit laisse resonner ses frappes\n`, '', 'D-102');
  // D-281, D-421: a chord note whose rank is still held by an earlier note, or in its release, takes a silent rank,
  // otherwise the oldest rank in its release.
  P(`    for (let k = 1; k <= 5; k++){
      const pa = c['pat' + (k + 1)];
      if (!pa || pa[st] < 0) continue;
      const note = pa[st];                           // deja absolue
      const v = this.voices[this.polyVoice(i, k)];`, `    const rank = [0, 0, 0, 0, 0, 0], taken = [0, 0, 0, 0, 0, 0];
    const has = (k) => { const pa = c['pat' + (k + 1)]; return pa && pa[st] >= 0; };
    const pv = (k) => this.voices[this.polyVoice(i, k)];
    for (let k = 1; k <= 5; k++)
      if (has(k) && (drum || (c.polyLeft[k] <= 0 && !pv(k).on))) { rank[k] = k; taken[k] = 1; }
    for (let k = 1; k <= 5; k++){
      if (!has(k) || rank[k]) continue;
      let best = 0;
      for (let j = 1; j <= 5; j++) {
        if (taken[j] || c.polyLeft[j] > 0) continue;
        if (!pv(j).on) { best = j; break; }
        if (!best || pv(j).age < pv(best).age) best = j;
      }
      rank[k] = best || k;
      taken[rank[k]] = 1;
    }
    for (let r = 1; r <= 5; r++){
      const pa = c['pat' + (r + 1)];
      if (!pa || pa[st] < 0) continue;
      const k = rank[r];
      const note = pa[st];
      const v = this.voices[this.polyVoice(i, k)];`, 'D-281 D-421');
  P(`      const dk = c['dur' + (k + 1)];
      c.polyLeft[k]`, `      const dk = c['dur' + (r + 1)];
      c.polyLeft[k]`, 'D-281');
  P(`      c.polyLeft[k] = Math.max(1, (dk && dk[st]) || 1);   // duree propre a la voix
      if (!drum) v.offIn = gateHold;                 // les frappes vont au bout`,
  `      const d = Math.max(1, (dk && dk[st]) || 1);
      c.polyLeft[k] = drum ? 0 : d;
      c.polyOff[k] = (!drum && d === 1) ? gateHold : 0;`, 'D-102');
  P(`      if (v) { v.on = 0; v.fade = 0; }
      c.polyLeft[k] = 0;`, `      if (v) { if (v.on) this.log('O', this.polyVoice(i, k)); v.on = 0; v.fade = 0; }
      c.polyLeft[k] = 0; c.polyOff[k] = 0;`, 'log O');
  // Per-sample loop: log position, tracks of the second chip, repeated hits (D-101), gate of the chord voices
  // (D-102), track arpeggio (D-097).
  P(`    for (let s = 0; s < L.length; s++) {\n`, `    for (let s = 0; s < L.length; s++) {\n      this.curS = this.samplePos + s;\n`,
    'log');
  P(`      const laneMax = m3 ? 4 : this.NV;`, `      const laneMax = m3 ? 4 * (p.chips || 1) : this.NV;`, 'D-106');
  P(`          this.laneTrigger(c.rt.i, c.rt.note, c.level * 0.92);`,
    `          this.laneTrigger(c.rt.i, c.rt.note, c.rt.lvl, c.rt.eff);
          this.applyMix(this.voices[this.laneVoice(i)], c.rt.mix);`, 'D-101');
  P(`          if (ve !== 2 && ve !== 4) this.killLane(vi);   // un drum va au bout
          c.arpNote = -1;
        }`, `          if (ve !== 2 && ve !== 4) this.killLane(vi);   // un drum va au bout
          c.arpNote = -1;
        }
        for (let k = 1; k <= 5; k++)
          if (c.polyOff[k] > 0 && --c.polyOff[k] === 0) {
            const pv = this.voices[this.polyVoice(i, k)];
            if (pv && pv.on) { this.relVoice(pv, 64); this.log('R', this.polyVoice(i, k)); }
          }`, 'D-102');
  P(`        if ((arpRun || (c.arpGl && c.arpGlLeft > 0)) && !this.clk.midi) {`,
    `        if (arpRun || (c.arpGl && c.arpGlLeft > 0)) {`, 'D-097');
  P(`          if (!arpRun) return;`, `          if (!arpRun || this.clk.midi) continue;`, 'D-097');
  P(`  msg(d) {\n    const p = this.patch;`, `  msg(d) {\n    this.curS = this.samplePos;\n    const p = this.patch;`, 'log');
  // Stop: queue applied, chords released, tracks reset (D-102), mixer panning.
  P(`      if (d.seq && !d.seq.on) for (const c of this.chans) c.panEff = null;\n`, '', 'log P');
  P(`        this.seq.step = this.seq.len - 1; this.seq.stepAbs = -1; this.seq.phase = 1; this.clk.seqTick = 5;`,
    `        this.seq.step = this.seq.len - 1; this.seq.stepAbs = -1; this.seq.phase = 1; this.seq.dur = 1;
        this.clk.seqTick = this.cps() - 1;`, 'D-099');
  P(`      if (!this.seq.on) {
        if (this.patQ) this.applyPatQ();
        this.killTag('seq');
        for (const c of this.chans) { c.offIn = 0; c.arpNote = -1; c.cutIn = 0; c.tiedHold = 0; c.slidePend = 0; }
      }`, `      if (!this.seq.on && wasOn) {
        if (this.patQ) this.applyPatQ();
        this.killTag('seq'); this.killTag('poly');
        this.clearLanes(true);
        for (let i = 0; i < this.NV; i++) this.setPan(i, null);
        this.log('!', -1);
      }`, 'D-102');
  // MIDI clock (D-099).
  P(`        if (++c.seqTick >= 6) { c.seqTick = 0; this.seqTick(); }`,
    `        if (++c.seqTick >= this.cps()) { c.seqTick = 0; this.seqTick(); }`, 'D-099');
  P(`      c.run = 1; c.seqTick = 5; c.arpTick = c.arpDiv - 1;`, `      c.run = 1; c.seqTick = this.cps() - 1; c.arpTick = c.arpDiv - 1;`,
    'D-099');
  P(`      for (const ch of this.chans) { ch.arpIdx = -1; ch.arpNote = -1; ch.offIn = 0; ch.cutIn = 0; ch.tiedHold = 0; ch.slidePend = 0; }`,
    `      this.clearLanes(false);
      for (const ch of this.chans) ch.arpIdx = -1;`, 'D-102');
  P(`      this.clk.run = 0; this.killTag('seq');
      for (const c of this.chans) { c.offIn = 0; c.arpNote = -1; c.cutIn = 0; c.tiedHold = 0; c.slidePend = 0; }`,
    `      this.clk.run = 0; this.killTag('seq'); this.killTag('poly');
      this.clearLanes(true);`, 'D-102');
  P(`      const L2 = this.seq.len;
      this.seq.step = ((d.pos % L2) + L2 - 1) % L2;
      this.clk.seqTick = 5;`, `      const L2 = this.seq.len, cps = this.cps(), q = Math.max(0, d.pos) * 6;
      this.seq.stepAbs = Math.floor((q - 1) / cps);
      this.clk.seqTick = (((q - 1) % cps) + cps) % cps;
      this.seq.step = ((this.seq.stepAbs % L2) + L2) % L2;`, 'D-099');
  // D-104: slide seeds and Random arpeggio tied to the playback position.
  P(`    v.slideSeed = (Math.random() * 4294967295) >>> 0;\n  }\n  slideTo`,
    `    v.slideSeed = this.hash(this.seq.stepAbs, this.voices.indexOf(v), 4);\n  }\n  slideTo`, 'D-104');
  P(`    v.slideSeed = (Math.random() * 4294967295) >>> 0;   // trajectoire propre a chaque glisse`,
    `    v.slideSeed = this.hash(this.seq.stepAbs, this.voices.indexOf(v), 3);`, 'D-104');
  P(`    if (c.arpMode === 4) off = L[(this.rand() * n) | 0];`, `    if (c.arpMode === 4) off = L[(this.laneRand(c) * n) | 0];`,
    'D-104');
  // D-343, D-345: the steps of a held note apply their VOL, RTG and ARP (heldStep); VOL also during a slide.
  P(`    const mix = c.mix[st];
    // Pas vide`, `    const mix = c.mix[st];
    if (n >= 0) c.heldNote = n;
    // Pas vide`, 'D-345');
  P(`        this.applyMixDef(v, c, mix);
        this.liveFx(v, c, c.fx[st]);
        return;
      }
      if ((c.tie[st] && c.tiedHold) && live) {`, `        this.applyMixDef(v, c, mix);
        this.liveFx(v, c, c.fx[st]);
        this.relevelLane(i, lvl); c.arpLvl = lvl;
        return;
      }
      if ((c.tie[st] && c.tiedHold) && live) {`, 'D-345');
  P(`        this.liveFx(v, c, c.fx[st]);                 // FX applique meme sur tenue
        return;`, `        this.liveFx(v, c, c.fx[st]);                 // FX applique meme sur tenue
        this.heldStep(i, c, st, lvl, stepSamp);
        return;`, 'D-343 D-345');
  // D-205: the ADSR of the track does not apply to a drum hit (Digidrum, Chipdrum).
  P(`    v.adsrOn = (v.atk > 0 || v.dcy > 0 || v.sus < 1 || v.rel > 0) ? 1 : 0;`,
    `    v.adsrOn = (v.effect !== 2 && v.effect !== 4 && (v.atk > 0 || v.dcy > 0 || v.sus < 1 || v.rel > 0)) ? 1 : 0;`,
    'D-205');
  // D-421: in 16 channels, the note before a new note of a polyphonic track goes on to its release on a chord voice.
  P(`    } else {
      this.killLane(vi);
      c.arpNote = -1;
`, `    } else {
      if (!this.laneTail(i)) this.killLane(vi);
      c.arpNote = -1;
`, 'D-421');
  return src;
}

const { Proc } = loadPrototype(process.argv[2], SR, transform);
const W = Proc.prototype;

// ---- helpers added to the worklet
// Six decimals, trailing zeros and trailing zero fields omitted.
const f6 = (x) => (Math.abs(x) < 5e-7 ? 0 : x).toFixed(6).replace(/\.?0+$/, '');
W.log = function (op, v, a = 0, b = 0, x = 0, y = 0, z = 0) {
  if (!this.out) return;
  const f = [this.curS, op, v, +a, +b, f6(+x), f6(+y), f6(+z)].map(String);
  while (f.length > 3 && f[f.length - 1] === '0') f.pop();
  this.out.push(f.join(' '));
};
// lowbias32 of the position (D-104), seed 1: identical to Engine::Impl::seqHash.
W.hash = function (stepAbs, a, salt) {
  let x = (Math.imul(stepAbs >>> 0, 0x9E3779B1) + Math.imul(a >>> 0, 0x85EBCA77) + Math.imul(salt, 0xC2B2AE3D) + 1) >>> 0;
  x ^= x >>> 16; x = Math.imul(x, 0x7feb352d) >>> 0;
  x ^= x >>> 15; x = Math.imul(x, 0x846ca68b) >>> 0;
  x ^= x >>> 16;
  return x >>> 0;
};
W.hashU = function (stepAbs, a, salt) { return this.hash(stepAbs, a, salt) / 4294967296; };
W.laneRand = function (c) {
  let x = c.rand >>> 0;
  x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0;
  c.rand = x;
  return x / 4294967296;
};
W.cps = function () { return Math.round(24 / (this.seq.spb || 4)); };
W.setPan = function (i, val) {
  const c = this.chans[i];
  const old = c.panEff === undefined ? null : c.panEff;
  if (old === val) return;
  c.panEff = val;
  this.log('P', i, 0, 0, val === null ? 9 : val);
};
W.clearLanes = function (full) {
  for (const c of this.chans) {
    c.offIn = 0; c.arpNote = -1; c.cutIn = 0; c.tiedHold = 0; c.slidePend = 0; c.rt = null;
    if (full) { c.arpGl = 0; c.arpGlN = 0; c.arpGlLeft = 0; c.arpGlNow = 0; c.polyLeft.fill(0); c.polyOff.fill(0); }
  }
};
// D-345: VOL of a held step, to the sounding voices of the track (Impl::relevel; log A: level).
W.relevelLane = function (i, lvl) {
  const visit = (vi) => {
    const v = this.voices[vi];
    if (!v || !v.on || v.ch !== i || (v.tag !== 'seq' && v.tag !== 'poly') || v.fade || v.aStage === 4) return;
    if (!this.patch.mode3ch) v.level = lvl;
    this.log('A', vi, 0, 0, lvl);
  };
  visit(this.laneVoice(i));
  if (!this.patch.mode3ch) for (let k = 1; k <= 5; k++) visit(this.polyVoice(i, k));
};
// D-343, D-345: a step that a held note crosses (Impl::heldStep): GAT of the last step, VOL, then ARP starts or resumes
// the arpeggio, then RTG strikes the held note again from the start of the step.
W.heldStep = function (i, c, st, lvl, stepSamp) {
  const nx = (st + 1) % Math.max(1, c.plen || this.seq.len);
  if (c.gat[st] && !(c.tie[nx] && c.pat[nx] < 0)) c.offIn = Math.max(1, Math.floor(c.gat[st] / 100 * stepSamp));
  this.relevelLane(i, lvl);
  c.arpLvl = lvl;
  const fxv = c.fx[st] | 0, eff = fxv ? [-1, 0, 1, 3, 4][fxv] : -1;
  if (c.arpOn && c.afl[st]) {
    if (c.arpNote < 0) {
      c.arpNote = c.heldNote; c.arpIdx = -1; c.arpDir = 1; c.arpMix = c.mix[st]; c.arpFx = eff;
      c.arpHold = 0; c.arpPhase = 1; c.arpClkT = this.clk.arpDiv - 1;
    } else if (c.arpHold) { c.arpHold = 0; c.arpIdx = -1; c.arpDir = 1; c.arpPhase = 1; }
  }
  c.rt = null;
  if (c.rtg && c.rtg[st] > 0 && c.arpNote < 0) {
    const hits = c.rtg[st] + 1;
    c.rt = { left: hits - 1, period: Math.max(1, Math.floor(stepSamp / hits)), clk: 0, note: c.heldNote, i,
             lvl: lvl * 0.92, eff, mix: c.mix[st] };
    c.rt.clk = c.rt.period;
    this.laneTrigger(i, c.heldNote, lvl, eff);
    this.applyMix(this.voices[this.laneVoice(i)], c.rt.mix);
  }
};

// D-421 (Impl::laneTail): the note of the lane voice, if it has a release, moves to a chord voice of the track that no
// chord note holds, a silent one, otherwise the oldest in its release, and goes on to its release there.
W.laneTail = function (i) {
  const c = this.chans[i], lv = this.voices[this.laneVoice(i)];
  if (this.patch.mode3ch || c.mono || !lv.on || lv.tag !== 'seq' || lv.fade || lv.effect === 2 || lv.effect === 4 ||
      !lv.adsrOn || !(lv.rel > 0)) return false;
  let ti = -1, tk = -1;
  for (let k = 1; k <= 5; k++) {
    if (c.polyLeft[k] > 0) continue;
    const j = this.polyVoice(i, k), t = this.voices[j];
    if (!t.on) { ti = j; tk = k; break; }
    if ((t.aStage === 4 || t.fade) && (ti < 0 || t.age < this.voices[ti].age)) { ti = j; tk = k; }
  }
  if (ti < 0) return false;
  const t = this.voices[ti];
  if (t.on) { t.on = 0; this.log('O', ti); }
  Object.assign(t, lv);
  t.tag = 'poly';
  lv.on = 0;
  c.polyLeft[tk] = 0; c.polyOff[tk] = 0;
  this.relVoice(t, 32);
  this.log('R', ti);
  return true;
};

// ---- log of the voice operations (same points as Engine::Impl)
const orig = {};
for (const k of ['setupVoice', 'killLane', 'slideTo', 'reshapeSlide', 'retune', 'liveFx', 'applyMix', 'applyMixDef'])
  orig[k] = W[k];
W.setupVoice = function (v, note, vol, tag, src, lvl) {
  const r = orig.setupVoice.call(this, v, note, vol, tag, src, lvl);
  if (tag === 'seq' || tag === 'poly')
    this.log('T', this.voices.indexOf(v), v.effect, tag === 'poly' ? 1 : 0, note, lvl === undefined ? 1 : lvl,
      v.drumTr || 1);
  return r;
};
W.killLane = function (i, hard) {
  const v = this.voices[i];
  if (v.on && v.tag === 'seq' && !v.fade && (hard || v.aStage !== 4)) this.log('K', i, hard ? 1 : 0);
  return orig.killLane.call(this, i, hard);
};
W.slideTo = function (v, note, stepSamp) {
  orig.slideTo.call(this, v, note, stepSamp);
  this.log('S', this.voices.indexOf(v), v.glideN, v.slideCv, note);
};
W.reshapeSlide = function (v, cv, stepSamp) {
  const ok = v.fCur > 0 && v.fTarget > 0, left = Math.max(1, v.glide);
  orig.reshapeSlide.call(this, v, cv, stepSamp);
  if (ok) this.log('H', this.voices.indexOf(v), left, cv);
};
W.retune = function (v, note) {
  orig.retune.call(this, v, note);
  this.log('N', this.voices.indexOf(v), 0, 0, note);
};
W.liveFx = function (v, c, fxv) {
  const eff = fxv ? [-1, 0, 1, 3, 4][fxv] : -1;
  const ok = fxv && !(eff < 0 || eff === 4 || eff === v.effect || v.effect === 2 || v.effect === 4);
  orig.liveFx.call(this, v, c, fxv);
  if (ok) this.log('F', this.voices.indexOf(v), eff);
};
W.applyMix = function (v, m) {
  orig.applyMix.call(this, v, m);
  if (m && v.effect !== 2 && v.effect !== 3 && v.effect !== 4) this.log('M', this.voices.indexOf(v), v.toneEn, v.noiseEn);
};
W.applyMixDef = function (v, c, m) {
  orig.applyMixDef.call(this, v, c, m);
  if (!m && v.effect !== 2 && v.effect !== 3 && v.effect !== 4) this.log('M', this.voices.indexOf(v), v.toneEn, v.noiseEn);
};

// ---- data of a pattern track (rows of ymengine::SeqLane, file order)
const KEYS = ['pat', 'oct', 'tie', 'slide', 'afl', 'vol', 'mix', 'slt', 'cut', 'fx', 'gat', 'pnr', 'rtg', 'cnd', 'slc', 'ton'];
const NSTEP = 256, NV = 5;
function emptyLane() {
  const l = {};
  for (const k of KEYS) l[k] = new Array(NSTEP).fill(k === 'pat' ? -1 : k === 'vol' ? 15 : 0);
  l.vpat = Array.from({ length: NV }, () => new Array(NSTEP).fill(-1));
  l.voct = Array.from({ length: NV }, () => new Array(NSTEP).fill(0));
  l.vdur = Array.from({ length: NV }, () => new Array(NSTEP).fill(0));
  return l;
}
function laneRows(l) {
  return [...KEYS.map((k) => l[k]), ...l.vpat, ...l.voct, ...l.vdur];
}
const DEFAULTS = laneRows(emptyLane()).map((r) => r[0]);

// Random content of a track (values like those the editor writes).
function makeLane(R, tr, steps, mode3) {
  const l = emptyLane();
  const drum = tr.effect === 2 || tr.effect === 4;
  const L = tr.plen || steps;
  for (let s = 0; s < L; s++) {
    const note = R.chance(drum ? 0.42 : 0.5);
    if (note) {
      l.pat[s] = drum ? R.int(0, 7) : R.int(0, 11);
      if (drum) {
        if (R.chance(0.3)) l.oct[s] = R.int(-2, 2);
        if (R.chance(0.25)) l.ton[s] = R.int(-12, 12);
      } else if (R.chance(0.3)) l.oct[s] = R.pick([-1, 1, 2, 3, 4, 5, 6]);
      if (R.chance(0.25)) l.vol[s] = R.int(2, 14);
      if (R.chance(0.1)) l.rtg[s] = R.int(1, 3);
      if (R.chance(0.12)) l.cnd[s] = R.int(1, 7);
      if (!drum) {
        if (R.chance(0.2)) l.afl[s] = 1;
        if (R.chance(0.2)) l.slide[s] = 1;
        else if (R.chance(0.12)) l.tie[s] = 1;
        if (l.slide[s] && R.chance(0.5)) l.slc[s] = R.int(0, 12);
        if (R.chance(0.2)) l.mix[s] = R.int(1, 3);
        if (R.chance(0.1)) l.fx[s] = R.int(1, 4);
        if (l.fx[s] === 4 && l.afl[s]) l.fx[s] = 3;   // Chipdrum FX on an arpeggiated step: outside the compared domain
        if (R.chance(0.15)) l.gat[s] = R.int(5, 100);
      } else if (tr.effect === 4 && R.chance(0.1)) l.tie[s] = 1;
      if (R.chance(0.12)) l.pnr[s] = R.int(1, 201);
      if (!mode3 && R.chance(drum ? 0.12 : 0.22)) {
        const nv = R.int(1, NV);
        for (let k = 0; k < nv; k++) {
          l.vpat[k][s] = drum ? R.int(0, 7) : R.int(0, 11);
          if (!drum && R.chance(0.3)) l.voct[k][s] = R.pick([-1, 2, 3, 4, 5]);
          if (!drum) l.vdur[k][s] = R.pick([0, 0, 1, 2, 3, 5]);
        }
      }
    } else {
      if (!drum && R.chance(0.2)) l.tie[s] = 1;
      else if (!drum && R.chance(0.08)) l.slide[s] = 1;
      if (drum && R.chance(0.15)) l.cut[s] = 1;
      if (drum && R.chance(0.12)) l.tie[s] = 1;
      if (l.tie[s] || l.slide[s]) {
        if (R.chance(0.2)) l.mix[s] = R.int(0, 3);
        if (R.chance(0.1)) l.fx[s] = R.int(1, 3);
        if (R.chance(0.2)) l.slc[s] = R.int(1, 12);
        if (R.chance(0.3)) l.afl[s] = 1;
      }
      if (!mode3 && R.chance(0.05)) {
        l.vpat[0][s] = drum ? R.int(0, 7) : R.int(0, 11);
        if (!drum) l.vdur[0][s] = R.pick([0, 1, 2]);
      }
      if (R.chance(0.06)) l.pnr[s] = R.int(1, 201);
    }
  }
  return l;
}

// Worklet payload (chanPayload of the page): absolute notes, D-098 for the drums.
function payload(l, tr) {
  const drum = tr.effect === 2 || tr.effect === 4;
  const octOf = (code) => (code === -1 ? 0 : (code || tr.seqOct));
  const abs = (rel, code) => (rel < 0 ? -1 : drum ? (tr.seqOct + 1) * 12 + rel : (octOf(code) + 1) * 12 + rel);
  const o = {
    pat: Int16Array.from(l.pat.map((r, s) => abs(r, l.oct[s]))),
    tie: Uint8Array.from(l.tie), slide: Uint8Array.from(l.slide), afl: Uint8Array.from(l.afl),
    vol: Uint8Array.from(l.vol), mix: Uint8Array.from(l.mix), slotRow: Uint8Array.from(l.slt),
    cut: Uint8Array.from(l.cut), fx: Uint8Array.from(l.fx), gat: Uint8Array.from(l.gat), pnr: Int16Array.from(l.pnr),
    rtg: Uint8Array.from(l.rtg), cnd: Uint8Array.from(l.cnd), slc: Uint8Array.from(l.slc),
    oct2r: Int16Array.from(l.oct), ton: Int16Array.from(l.ton),
  };
  for (let k = 0; k < NV; k++) {
    o['pat' + (k + 2)] = Int16Array.from(l.vpat[k].map((r, s) => abs(r, l.voct[k][s])));
    o['dur' + (k + 2)] = Int16Array.from(l.vdur[k]);
  }
  return o;
}

const SCALES = [0, 0xAB5, 0x5AD, 0x295, 0x891];
const RESOS = [1, 2, 4, 8, 1.5, 3, 6];
const out = [];
out.push('# Generated by tools/proto_golden/seq.mjs from YM2149_Prototype.html (YMProc worklet, deviations ' +
  'D-097 to D-106, D-205, D-281, D-343, D-345, D-421).');

function runCase(id, mode, midi) {
  const R = rng(0x5E0 + id * 7919);
  const mode3 = mode > 0, chips = mode === 2 ? 2 : 1;
  const vbl = R.pick([50, 50, 60]);
  const bpm = R.int(80, 200);
  const swing = R.chance(0.5) ? 50 : R.int(50, 75);
  const res = midi ? R.pick([1, 2, 2, 3, 5, 6]) : R.pick([0, 1, 2, 2, 2, 3, 4, 5, 6]);
  const spb = RESOS[res];
  const scaleMask = R.pick(SCALES), scaleRoot = R.int(0, 11);
  const table = [];
  const shape = R.int(0, 2);
  for (let i = 0; i < 32; i++) {
    const t = i / 31;
    table.push(Math.fround(shape === 0 ? t : shape === 1 ? t * t : 0.5 - 0.5 * Math.cos(Math.PI * t)));
  }
  const smp = [];
  for (let k = 0; k < 8; k++) smp.push(R.int(300, 4000));

  const p = new Proc();
  p.out = null;
  p.curS = 0;
  p.curPat = 0;
  p.msg({ type: 'patch', patch: { mode3ch: mode3 ? 1 : 0, chips, vbl, scaleMask, scaleRoot, slideTable: table } });
  p.buildChipDrums();
  for (let k = 0; k < 8; k++) {
    const d = new Uint8Array(smp[k]);
    for (let i = 0; i < d.length; i++) d[i] = (i * (k + 3)) & 15;
    p.user[k] = d;
  }
  // Tracks
  const ntr = mode3 ? 4 * chips : 16;
  const tracks = [];
  for (let i = 0; i < ntr; i++) {
    const on = mode3 ? R.chance(0.85) : R.chance(0.45);
    const effect = mode3 && (i & 3) === 3 ? R.pick([2, 2, 4, 0]) : R.pick(mode3 ? [0, 0, 1, 3, 4] : [0, 0, 0, 1, 2, 3, 4]);
    const env = R.int(0, 9);
    const tr = {
      i, on, effect,
      vol4: R.int(10, 15),
      tone: 1, noise: 0,
      mono: !mode3 && R.chance(0.15) ? 1 : 0,
      level: mode3 ? R.pick([1, 1, 0.5, 0.75]) : 1,
      gate: R.int(5, 95) / 100,
      plen: R.chance(0.2) ? R.int(3, 20) : 0,
      seqOct: R.int(2, 5),
      atk: env >= 8 ? R.int(2, 20) : 0,
      dcy: env >= 8 ? R.int(10, 80) : 0,
      sus: env >= 8 ? R.int(40, 90) : 100,
      rel: env >= 6 ? R.int(80, 400) : 0,
      adsrChip: R.chance(0.3) ? 1 : 0,
      sidRatio: R.pick([1, 1, 2, 0.5]),
      arpOn: R.chance(0.8) ? 1 : 0,
      arpRate: R.int(8, 60),
      arpMode: R.int(0, 5),
      arpOct: R.int(1, 3),
      arpTlen: R.int(1, 6),
      arpTable: [0],
      arpTrack: R.chance(0.3) ? 1 : 0,
      arpLand: R.chance(0.3) ? 1 : 0,
    };
    for (let k = 1; k < 16; k++) tr.arpTable.push(k < tr.arpTlen ? R.int(-12, 12) : 0);
    const nm = R.int(0, 9);
    if (nm === 8) { tr.tone = 1; tr.noise = 1; } else if (nm === 9) { tr.tone = 0; tr.noise = 1; }
    tracks.push(tr);
  }
  // Patterns 0 to 2
  const pats = [];
  for (let q = 0; q < 3; q++) {
    const steps = R.pick([8, 12, 16, 16, 16, 24, 32]);
    const lanes = tracks.map((tr) => (tr.on && R.chance(0.9) ? makeLane(R, tr, steps, mode3) : emptyLane()));
    pats.push({ steps, lanes });
  }
  const chan = (tr, l) => Object.assign({
    toneEn: tr.tone, noiseEn: tr.noise, mono: tr.mono, vol4: tr.vol4, effect: tr.effect, sidRatio: tr.sidRatio,
    atk: tr.atk, dcy: tr.dcy, sus: tr.sus, rel: tr.rel, adsrChip: tr.adsrChip,
    enabled: tr.on ? 1 : 0, laneOn: tr.on ? 1 : 0, level: tr.level, gate: tr.gate, plen: tr.plen,
    arpOn: tr.arpOn, arpRate: tr.arpRate, arpMode: tr.arpMode, arpOct: tr.arpOct, arpTlen: tr.arpTlen,
    arpTable: tr.arpTable.slice(), arpTrack: tr.arpTrack, arpLand: tr.arpLand,
  }, payload(l, tr));
  for (const tr of tracks) p.msg({ type: 'chan', i: tr.i, ch: chan(tr, pats[0].lanes[tr.i]) });
  if (midi) p.msg({ type: 'clock', clk: { midi: 1 } });

  // Header of the case
  out.push(`case ${id} mode=${mode} vbl=${vbl} bpm=${bpm} swing=${swing} res=${res} midi=${midi ? 1 : 0} ` +
    `scale=${scaleMask} root=${scaleRoot}`);
  out.push('table ' + table.map(String).join(' '));
  out.push('smp ' + smp.join(' '));
  for (const tr of tracks) {
    out.push(`track ${tr.i} on=${tr.on ? 1 : 0} gen=${tr.effect} vol=${tr.vol4} tone=${tr.tone} noise=${tr.noise} ` +
      `mono=${tr.mono} level=${tr.level} gate=${tr.gate} plen=${tr.plen} oct=${tr.seqOct} atk=${tr.atk} ` +
      `dcy=${tr.dcy} sus=${tr.sus} rel=${tr.rel} chip=${tr.adsrChip} sid=${tr.sidRatio} arp=${tr.arpOn} ` +
      `rate=${tr.arpRate} amode=${tr.arpMode} aoct=${tr.arpOct} alen=${tr.arpTlen} track=${tr.arpTrack} ` +
      `land=${tr.arpLand} atable=${tr.arpTable.join(',')}`);
  }
  pats.forEach((pt, q) => {
    out.push(`pattern ${q} ${pt.steps}`);
    pt.lanes.forEach((l, t) => {
      const cells = [];
      laneRows(l).forEach((row, k) => row.forEach((v, s) => { if (v !== DEFAULTS[k]) cells.push(`${k}:${s}:${v}`); }));
      if (cells.length) out.push(`lane ${q} ${t} ${cells.join(' ')}`);
    });
  });

  // Run
  const stepSamp = midi ? 0 : SR * 60 / (bpm * spb);
  const clockEvery = R.int(5, 7);
  const blocks = Math.min(2600, midi ? 2600 : Math.ceil(44 * stepSamp / BLOCK) + 8);
  const restart = R.chance(0.6);
  const L = new Float32Array(BLOCK), Rr = new Float32Array(BLOCK);
  p.out = out;
  let nextClock = 2, cur = 0;
  const send = (b, d, line) => { out.push(`msg ${b} ${line}`); p.msg(d); };
  const queue = (b) => {
    if (!p.seq.on) return;
    let q = R.int(0, 2);
    if (q === p.curPat) q = (q + 1) % 3;
    if (p.patQ && p.patQ.idx === q) return;
    send(b, { type: 'patQueue', idx: q, len: pats[q].steps,
      chans: tracks.map((tr) => Object.assign({ i: tr.i }, payload(pats[q].lanes[tr.i], tr))) }, `queue ${q}`);
  };
  const start = (b) => send(b, { type: 'seq', seq: { on: 1, bpm, swing, spb, len: pats[p.curPat | 0].steps } }, 'play');
  const at = (f) => Math.floor(blocks * f);
  for (let b = 0; b < blocks; b++) {
    if (b === 1) start(b);
    if (midi) {
      if (b === 2) send(b, { type: 'mstart' }, 'mstart');
      if (b === at(0.5)) send(b, { type: 'mstop' }, 'mstop');
      if (b === at(0.55)) { const pos = R.int(0, 40); send(b, { type: 'mspp', pos }, `mspp ${pos}`); }
      if (b === at(0.6)) send(b, { type: 'mcont' }, 'mcont');
      if (b === nextClock) {
        send(b, { type: 'mclk' }, 'mclk');
        nextClock += R.chance(0.7) ? clockEvery : R.int(4, 8);
      }
    }
    if (b === at(0.33) || b === at(0.66)) queue(b);
    if (b === at(0.85)) send(b, { type: 'seq', seq: { on: 0 } }, 'stop');
    if (restart && b === at(0.9)) start(b);
    p.process([], [[L, Rr]]);
    cur++;
  }
  p.out = null;
  out.push(`end ${blocks}`);
}

let id = 0;
for (const [mode, midi, n] of [[0, 0, 16], [0, 1, 6], [1, 0, 7], [1, 1, 3], [2, 0, 5], [2, 1, 2]])
  for (let k = 0; k < n; k++) runCase(id++, mode, midi);
process.stdout.write(out.join('\n') + '\n');
