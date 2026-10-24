// STYM2149 — loading of the prototype's engine (YM2149_Prototype.html) in Node, for the parity data.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The prototype is not versioned: its path is passed as an argument. The source of the AudioWorklet is extracted
// as is (WORKLET string) and instantiated with a fake message port, as the prototype does for its offline engine
// (makeOfflineEngine).
import { readFileSync } from 'node:fs';

// transform: decided deviations applied to the worklet source (checked replacements, see page.mjs).
export function loadPrototype(path, sampleRate, transform = (src) => src) {
  const html = readFileSync(path, 'utf8');
  const start = html.indexOf('const WORKLET = `');
  const end = html.indexOf('`;', start);
  if (start < 0 || end < 0) throw new Error('WORKLET string not found');
  const src = transform(html.slice(start + 'const WORKLET = `'.length, end));
  class AudioWorkletProcessor {
    constructor() { this.port = { postMessage: () => {}, onmessage: null }; }
  }
  let Proc = null;
  new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', src)(
    AudioWorkletProcessor, (name, cls) => { Proc = cls; }, sampleRate);
  // Constants of the page: kit banks and chords.
  const grab = (name) => {
    const i = html.indexOf('const ' + name + ' = ');
    if (i < 0) throw new Error(name + ' not found');
    let depth = 0, j = html.indexOf('=', i) + 1;
    for (; j < html.length; j++) {
      const ch = html[j];
      if (ch === '{' || ch === '[') depth++;
      else if (ch === '}' || ch === ']') { depth--; if (depth === 0) { j++; break; } }
    }
    return html.slice(html.indexOf('=', i) + 1, j);
  };
  const KIT = new Function('return ' + grab('KIT'))();
  const KIT_DEF = JSON.parse(JSON.stringify(KIT));
  const KIT_BANKS = new Function('KIT_DEF', 'return ' + grab('KIT_BANKS'))(KIT_DEF);
  const CHORDS = new Function('return ' + grab('CHORDS'))();
  return { Proc, KIT_DEF, KIT_BANKS, CHORDS };
}
