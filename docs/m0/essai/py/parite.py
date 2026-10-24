"""Builds the parity matrix (§4) from the three inventories of the prototype.
Usage: python3 parite.py PARITE.md [inventory folder]"""
import re, sys
SP = sys.argv[2] if len(sys.argv) > 2 else 'inventaire'
rows = []
for part in 'ABC':
    for line in open(f'{SP}/part{part}.md', encoding='utf-8'):
        if not line.startswith('| ') or line.startswith('| Domain') or line.startswith('|---'): continue
        cells = [c.strip() for c in line.strip().strip('|').split(' | ')]
        if len(cells) != 5: 
            cells = [c.strip() for c in line.strip()[1:-1].split('|')]
            cells = cells[:4] + [' '.join(cells[4:])] if len(cells) > 5 else cells
        if len(cells) == 5: rows.append(cells)
# Modules and tests per domain
MOD = {
 'Tracks': ('ymseq + gemui', 'S', 'V'), 'Source': ('ymengine', 'U', 'V'), 'Envelope': ('ymcore + ymengine', 'U', 'V'),
 'Vibrato/ADSR': ('ymengine', 'U', 'V'), 'Generators': ('ymengine', 'U', 'V'), 'Chipdrum': ('ymengine', 'G', 'V'),
 'Digidrum': ('ymengine + ymio', 'U', 'V'), 'Arpeggiator': ('ymengine', 'U', 'V'), 'Modulation': ('ymengine', 'U', 'V'),
 'Sequencer': ('ymseq + gemui', 'U', 'V'), 'Step rows': ('ymseq', 'U', 'V'), 'Timing': ('ymseq', 'U', 'V'),
 'Scales': ('ymseq', 'U', 'V'), 'Patterns/Song': ('ymseq', 'U', 'V'), 'Mixer': ('ymengine + gemui', 'U', 'V'),
 'Patches/Presets': ('ymio', 'S', 'V'), 'MIDI': ('plugin', 'U', 'M'), 'Keyboard': ('gemui + plugin', 'V', 'M'),
 'Editing/Undo': ('ymseq', 'U', 'V'), 'Control/Monitor': ('gemui + ymengine', 'V', 'U'), 'Options/Menus': ('gemui', 'V', 'S'),
 'Windows/UI': ('gemui', 'V', ''), 'Import/Export': ('ymio', 'G', 'S'), 'Audio engine': ('ymcore + ymengine', 'G', 'U'), 'Other': ('plugin', 'M', ''),
}
# Automatable parameters (§7), found by element id
PARAMS = [
 (r'#np\b', 'track[n].noisePeriod'), (r'#vol\b', 'track[n].vol4'), (r'#vmSel', 'track[n].volMode'), (r'#chLevel', 'track[n].level'),
 (r'#ef\b|#epv|EP\b', 'track[n].envPeriod'), (r'#shapeSel', 'track[n].envShape'), (r'#vibDepth', 'track[n].vibDepth'),
 (r'#vibRate', 'track[n].vibRate'), (r'#atk', 'track[n].attack'), (r'#dcy', 'track[n].decay'), (r'#sus', 'track[n].sustain'),
 (r'#rel', 'track[n].release'), (r'#ratio', 'track[n].sidRatio'), (r'#vmin', 'track[n].sidVmin'), (r'#arpRate', 'track[n].arpRate'),
 (r'#gate\b', 'track[n].gate (+ global gate)'), (r'#srcSel', 'track[n].source'), (r'#master', 'global.master'), (r'#modeSel', 'global.voiceMode'),
 (r'#bpm', 'global.freeTempo'), (r'#swing', 'global.swing'), (r'VBL|vbl', 'global.vblRate'), (r'Mute/Solo|mute|solo', 'track[n].mute / solo'),
]
# Plugin adaptations (§4 "Plugin adaptations") and deviations: keywords -> note
ADAPT = [
 (r'#boot|Startup|Boot screen', 'Adaptation: no "Run program" screen in the plugin; kept in Standalone (DECISIONS D-020)'),
 (r'pickAudioOut|Audio output|setSinkId', 'Adaptation: GEM menu of the JUCE devices in Standalone (§7)'),
 (r'Web MIDI|initMIDI|MIDI input', 'Adaptation: MIDI from the host; Standalone = JUCE devices'),
 (r'localStorage', 'Adaptation: JSON file in the user folder (AppData / Application Support)'),
 (r'Quit|quitApp|beforeunload', 'Adaptation: Standalone only; in the plugin, the host handles closing'),
 (r'#seqPlay|Play / Stop|Space bar|Transport bar\b', 'Adaptation: Host sync / Free run (§4)'),
 (r'Zoom', 'Adaptation: integer zoom first, nearest neighbour (§6.6)'),
]
OVR = {
 'P190': 'state', 'P198': 'state', 'P390': 'state', 'P423': 'UI', 'P435': 'UI', 'P307': 'state (PAN row per step)',
 'P260': 'track[n].attack / decay / sustain / release', 'P155': 'track[n].level / pan / mute / solo',
}
out = []
out.append('# Parity matrix prototype → plugin (M0)\n')
out.append('Source: `YM2149_Prototype.html` read in full (HTML 553-1091, JS 1092-10541) in three slices, then merged. '
           'One row per visible control or function. No row without a module and a test.\n')
out.append('**Test types**: U = unit (Catch2); G = golden render (audio comparison); S = state round trip / prototype-compatible JSON; '
           'V = visual non-regression (capture diff < 1 %); M = documented manual test. '
           'The test id is `T-<id>`; it will be created at the milestone given by the module.\n')
out.append('**Parameter**: name of the automatable parameter (§7) if there is one, otherwise "state" (saved in the project, not automatable) or "UI".\n')
out.append('Rows duplicated between slices are kept on purpose: they trace the same control on the HTML side and on the code side; one test covers them together.\n')
out.append('| Id | Domain | Control / function (prototype) | Element | Behaviour, range, default | Lines | Target module | Parameter | Test | Note |')
out.append('|---|---|---|---|---|---|---|---|---|---|')
n = 0
for d, c, e, b, l in rows:
    n += 1
    pid = f'P{n:03d}'
    mod, t1, t2 = MOD.get(d, ('plugin', 'M', ''))
    hay = f'{c} {e}'
    param = next((p for rx, p in PARAMS if re.search(rx, hay)), None)
    if param is None:
        param = 'UI' if d in ('Windows/UI', 'Options/Menus', 'Control/Monitor', 'Keyboard') else 'state'
    param = OVR.get(pid, param)
    note = next((a for rx, a in ADAPT if re.search(rx, hay)), '')
    tests = '+'.join(t for t in (t1, t2) if t)
    out.append(f'| {pid} | {d} | {c} | {e} | {b} | {l} | {mod} | {param} | {tests} T-{pid} | {note} |')
out.append(f'\n**Total: {n} rows**, all linked to a module and to at least one test.\n')
open(sys.argv[1], 'w', encoding='utf-8').write('\n'.join(out) + '\n')
print(n)
