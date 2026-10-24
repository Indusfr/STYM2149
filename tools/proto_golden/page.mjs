// STYM2149 — extraction of functions and constants from the prototype's page (outside the worklet), for the
// parity data of the sequencer.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The functions are taken as they are (source text) and assembled in a sandbox whose caller provides the state
// (CH, GLOB…) and the neutralised display functions. The decided deviations (DECISIONS.md) are applied by explicit,
// checked text replacements: a replacement that no longer finds its text stops the generation.
import { readFileSync } from 'node:fs';

export function readPage(path) {
  const html = readFileSync(path, 'utf8');
  const w = html.indexOf('const WORKLET = `');
  const wEnd = html.indexOf('`;', w);
  if (w < 0 || wEnd < 0) throw new Error('WORKLET string not found');
  return { html, pageFrom: wEnd + 2 };
}

// End of the balanced block that starts at the opening brace/bracket/parenthesis at `i` (strings and comments
// skipped).
function blockEnd(src, i) {
  let depth = 0;
  for (let j = i; j < src.length; j++) {
    const ch = src[j];
    if (ch === '/' && src[j + 1] === '/') { j = src.indexOf('\n', j); continue; }
    if (ch === '/' && src[j + 1] === '*') { j = src.indexOf('*/', j) + 1; continue; }
    if (ch === '"' || ch === "'" || ch === '`') {
      for (j++; j < src.length && src[j] !== ch; j++) if (src[j] === '\\') j++;
      continue;
    }
    if (ch === '{' || ch === '[' || ch === '(') depth++;
    else if (ch === '}' || ch === ']' || ch === ')') { depth--; if (depth === 0) return j + 1; }
  }
  throw new Error('unclosed block');
}

export function pageFunction(page, name) {
  const re = new RegExp('\\bfunction ' + name + '\\s*\\(', 'g');
  re.lastIndex = page.pageFrom;
  const m = re.exec(page.html);
  if (!m) throw new Error('function ' + name + ' not found');
  const params = page.html.indexOf('(', m.index);
  const body = page.html.indexOf('{', blockEnd(page.html, params));
  return page.html.slice(m.index, blockEnd(page.html, body));
}

export function pageConst(page, name) {
  const re = new RegExp('\\bconst ' + name + '\\s*=\\s*', 'g');
  re.lastIndex = page.pageFrom;
  const m = re.exec(page.html);
  if (!m) throw new Error('constant ' + name + ' not found');
  const from = m.index + m[0].length;
  let to = blockEnd(page.html, from);
  // possible continuation: .concat(...)
  const rest = page.html.slice(to).match(/^\s*\.concat\(/);
  if (rest) to = blockEnd(page.html, to + rest[0].length - 1);
  return 'const ' + name + ' = ' + page.html.slice(from, to) + ';';
}

// Checked replacement: the original text must be present once.
export function patch(src, from, to, why) {
  const i = src.indexOf(from);
  if (i < 0 || src.indexOf(from, i + 1) >= 0) throw new Error('replacement not found or ambiguous (' + why + ')');
  return src.slice(0, i) + to + src.slice(i + from.length);
}

// Reproducible pseudo-random generator (xorshift32).
export function rng(seed) {
  let x = seed >>> 0 || 1;
  const next = () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x; };
  return {
    int: (lo, hi) => lo + (next() % (hi - lo + 1)),
    chance: (p) => next() / 4294967296 < p,
    pick: (a) => a[next() % a.length],
  };
}
