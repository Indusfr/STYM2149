# M7 report: `ymio`, import and export

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

Decisions cited: [DECISIONS.md](../../DECISIONS.md) (D-159 to D-174). Plan: questions Q1 to Q9 decided by Indus on 16/10/2026 from 14:26 to 14:33 (D-159 to D-167).

## In short

- **Done**: the whole File menu of the prototype, and the formats of §8.
  - **JSON files**: preset, pattern, Song, kit and Patches library. The prototype's fields come out byte for byte like its own, and the prototype reads our files back. STYM2149's additions go under a versioned "stym" key (D-165, D-168, D-174).
  - **YM Player**: YM2 to YM6, LHA-compressed or not, and every SNDH. SNDH files are played by AtariAudio's machine (68000 Musashi, MFP, STE DAC), with output bit-identical to AtariAudio on 12 tunes (D-159, D-169).
  - **MIDI**: SMF 0 and 1 import, SMF 1 export of what the sequencer plays (D-170).
  - **Audio**: 16-bit, 24-bit and 32-bit float WAV, at every rate, and stems in a ZIP, with a clean cut (D-164, D-171).
  - **Registers**: VBL, YM3, LHA-compressed YM6 with the timer effects, SNDH v2 for ST or STE (a 68000 routine that plays the timer effects and the DMA), and export of the recorder's take (D-160 to D-163, D-172).
  - **XM import** (1.04) with its samples, played by the STE's DMA sound (D-166, D-173).
  - No menu entry opens the "not available in this version yet" alert any more (D-070).
- **Measurements**: exports and imports run outside the audio thread; the engine's load does not change: 2.50 % of one core for 96 voices, 2.91 % in the worst case, the same as the M6 binary measured on the same day (budget 3 %). Visual test: 45 scenes of the prototype under their threshold, 16 STYM2149-specific scenes identical to their reference rendering.
- **Not done**: MOD import (D-056), TTrak, YMT and MIX1 formats, SNDH durations database (no licence), SNDH tag of an STE tune (to confirm), register exports outside "Authentic 3ch". Details below.

## Architecture

| Layer | Additions in M7 | JUCE |
|---|---|---|
| `src/ymio` | Reading of SMF, XM 1.04 and SNDH (tags, ICE decompressor rewritten with bounds); writing of SMF, WAV, uncompressed ZIP, VBL `.s`, YM3, YM6 and SNDH v2 (small 68000 assembler); LHA `-lh5-` encoder; streamed byte output (`ByteSink`) | no |
| `src/ymcore` | `kToneEdgeReset` command: resets the edges for the "square-sync" of SNDH files | no |
| `src/ymengine` | Offline capture of the registers and the DMA sound (`startCapture`, `DmaFrame`); track and nominal tempo in the sequencer log (MIDI export); release when the YM Player starts | no |
| `src/plugincore` | JSON files (`ProjectFiles`, `Patches`); YM and SNDH tunes and their playback (`YmTune`, `Driver`); MIDI files (`MidiFiles`); offline rendering by a second instance of the engine (`Offline`); WAV and stem exports (`AudioExport`) and register exports (`RegisterExport`, `RegisterFiles`); XM import (`XmImport`) | no |
| `src/plugin` | The host's asynchronous file dialogs (`FileChooser`), YM Player playback in the processor | yes |
| `src/pluginui` | Complete File menu; YM Player window; WAV format dialog; Export row of the Recorder window; worker thread for the exports (`BackgroundJob`) and busy bee cursor; end-of-import and end-of-export alerts | no |
| `src/gemui` | Rows of radio buttons in an alert (`Alert::Choice`); busy bee pointer for long tasks (`Pointer::Busy`, `Desktop::setBusy`) | no |
| `third_party/AtariAudio` | Atari machine and Musashi compiled into the plugin (D-159); an observer of the writes to the chip added, marked "STYM2149" (PROVENANCE.md) | no |

## Results

Measured on 16/10/2026: Linux x86-64 (Xeon 2.1 GHz), GCC 11.5 (the one on Indus's runners) and Clang 18.1, Release.

| Check | Threshold | Measurement | State |
|---|---|---|---|
| Visual deviation, 45 scenes of the prototype at 100 % (tolerance 4/255 per component, D-086), relative to the M4 desktop (1016 × 944) | < 1 %; 2 % for `seq`, `seq_demo`, `song`, `song_demo` (D-121) | 0.598 % to 0.999 %; 1.031 % to 1.648 % for the four scenes at 2 % (table below) | OK |
| STYM2149-specific scenes (the 15 of M6, `rec_window` redone with its Export row, and `wav_dialog`), reference renderings checked by eye | identical | identical (0 pixels) | OK |
| JSON files written like the prototype's: files of its page (`tests/data/files`) read and written back | identical | identical byte for byte | OK |
| SNDH files read by AtariAudio's machine and `ymcore`, compared with AtariAudio's rendering (12 reference tunes) | identical | 0 samples differ out of 10 584 000 | OK |
| SNDH files written by the plugin, played by AtariAudio's machine: registers of each frame, timer effects (timers A, B and D), STE DMA (`[sndhwriter]`); init and exit (`[sndh]` oracle) | those of the capture; MFP restored, sound off | as expected | OK |
| Compressed YM6 files read back by our decompressor and by AtariAudio's (`[lzh]`) | identical | identical | OK |
| MIDI import compared with the prototype's `midiImport` (`tests/data/proto/midi.txt`) | identical, apart from the intended deviations of D-170 | 125 cases identical | OK |
| CPU load, 16 tracks of 6 voices played by the sequencer (`ymbench --seq --notes 6`, 48 kHz, blocks of 256) | < 3 % of one core | 2.50 % (varied scene); 2.91 % in the worst case (noise everywhere); 96 DMA voices: 2.91 %; the same as the M6 binary on the same machine (table below) | OK |
| No allocation in the audio thread: `processBlock` (400 blocks, M6 test) and plugin driver (real-time test: YM Player loaded, played, rewound, stopped and closed while the chain and the resampling change) | 0 | 0 (and RealtimeSanitizer in the CI, S11) | OK |
| Interface tests (`stym_ui`) | all pass | 129 test cases, 7 219 assertions | OK |
| Unit tests (`stym_unit`) | all pass | 367 test cases, 615 029 assertions | OK |
| JUCE processor and editor tests (`stym_plugin_tests`) | all pass | 22 test cases, 2 404 assertions (with and without a display) | OK |
| Compiler warnings | 0 | 0 in the plugin's code (GCC 11 and Clang 18). GCC 11 keeps its false positive `-Wmismatched-new-delete` on the allocation counter of `tests/realtime` (replaced operator delete), already there before M7 | OK |
| CI on Indus's runners: Linux and Windows, pluginval (VST3, LV2), clap-validator, RealtimeSanitizer (S11) | pass | at the PR | pending |

| Scene | Deviation | Scene | Deviation | Scene | Deviation |
|---|---|---|---|---|---|
| default | 0.713 % | menu_shape | 0.868 % | curve_custom | 0.866 % |
| sid | 0.705 % | menu_source | 0.715 % | kbd | 0.906 % |
| sync | 0.696 % | alert_chip ⁵ | 0.999 % | kbd_held | 0.898 % |
| chipdrum | 0.728 % | seq ² | 1.559 % | kbd_azerty | 0.918 % |
| digidrum ¹ | 0.663 % | seq_demo ² | 1.648 % | kbd_high | 0.950 % |
| envelope | 0.771 % | song ² | 1.031 % | orn | 0.650 % |
| smooth | 0.754 % | song_demo ² | 1.037 % | orn_on | 0.890 % |
| mode3 | 0.694 % | mixer | 0.751 % | kit_off | 0.818 % |
| dual | 0.793 % | mixer_3ch | 0.733 % | kit | 0.690 % |
| tabs | 0.713 % | mixer_scope | 0.720 % | kit_clap | 0.706 % |
| menu_file ³ | 0.690 % | arp | 0.733 % | lib | 0.877 % |
| menu_edit ⁴ | 0.691 % | arp_on | 0.706 % | lib_list | 0.754 % |
| menu_options | 0.990 % | arp_table | 0.893 % | learn | 0.713 % |
| menu_fuji | 0.712 % | arp_midi | 0.598 % | learn_bound | 0.713 % |
| menu_generator | 0.715 % | curve | 0.754 % | ym ⁶ | 0.747 % |

¹ Excluding the waveform and the frame's note (D-078, D-086). ² 2 % threshold (D-121). ³ Recorder entry below the exports; the following rows are compared one row lower (D-157). ⁴ Instruments and Sequences entries below the prototype's, ignored (D-147, D-148). ⁵ 0.825 % in M6: the alert's line now names YM6 ("VBL, YM3, YM6 and SNDH exports will match what you hear.", D-172); margin of 6 pixels under the threshold. ⁶ YM PLAYER window, scene added in M7 (D-169). Every scene compares the rows of the SYSTEM frame below the Computer row with their place in the capture (D-155).

Reproduce: `gemshot <output> --ref tests/data/visual`; `ctest -R ui` compares with the versioned captures. CPU load: `ymbench --seq --notes 6 [--gen chip|sid|buzzer|digidrum|chipdrum|dma] [--noise]`.

## CPU load

| Scene (96 voices, 48 kHz) | M6 report | M6, same day | M7, same day |
|---|---|---|---|
| Varied (SID, Sync Buzzer, Digidrum, Chipdrum, modulated chip channel) | 2.37 % | 2.50 % | 2.50 % |
| Noise on every track | 2.94 % | 2.93 % | 2.91 % |
| 96 Sync Buzzer | 2.65 % | 2.89 % | 2.86 % |
| 96 SID | 2.28 % | 2.50 % | 2.49 % |
| 96 chip channels | 2.00 % | 2.05 % | 2.00 % |
| 96 voices of the STE's DMA | 2.85 % | 2.98 % | 2.91 % |
| Idle engine | 0.58 % | 0.53 % | 0.53 % |

"Same day": the M6 (86349da) and M7 binaries compiled by GCC 11 and run alternately on the same machine, best of 3 to 15 runs. The machine is slower than on the day of the M6 report, hence the deviation from that column. M7 does not touch the audio loop (capture and log only serve the exports): the two binaries are within measurement noise of each other. The worst case stays at the limit: over 15 runs, the median with noise on every track is 3.05 % for M6 and 3.03 % for M7 that day. The margin is to be worked on again in M8.

Imports and exports never go through the audio thread: they run in a worker thread, on a copy of the project, with a second instance of the engine. The YM Player plays its own chip; the engine keeps running, muted, as in the prototype. The cost of the open editor is not counted (M8).

## Behaviour to know

- **File menu**: Load, Save and Save as for Preset, Pattern, Song and Drum kit; Import > YM dump, MIDI file, XM module; Export pattern > MIDI, WAV, Stems (ZIP); Export song > VBL dump, YM3, YM6, SNDH, MIDI, WAV, Stems (ZIP). Save rewrites the last file of that type; Save as asks for one (D-168).
- **Dialogs**: the host's. The folder of the last file and the chosen WAV format are kept in the preferences; the format is set in its GEM dialog (D-069, D-168, D-171).
- **Exports**: one at a time, under the bee, then an alert listing what the file does not render like the engine ("Not as heard:"). The editor stays usable; closing it abandons the export. A file is written next to the destination and only replaces it at the end (D-171).
- **Registers**: in "Authentic 3ch" only (greyed out otherwise, with the reason). SNDH: subsong 1 = Song, 2 = current pattern; STE machine: one DMA channel, which the most recent DMA note takes (D-162, D-172).
- **YM Player**: the tune replaces the main output; Play releases manual playing and the sequencer; closing the window stops playback (D-169).
- **Imports**: MIDI and XM each fit in one undo step. The XM import switches the project to the STE machine and replaces the patterns, the Song, the instruments and the DMA slots it fills (D-173).
- **File version**: a file from a newer version of STYM2149 is read for what this version knows of it, then reported (D-174).

## Not done in M7

| Item | Where | Why |
|---|---|---|
| MOD import | not kept | XM import only (D-056) |
| ST-Sound's TTrak, YMT and MIX1 formats | not kept | Undocumented (T19) or absent from §8 |
| SNDH durations database (AtariAudio's `timedb`) | to check | No licence text; the duration comes from the TIME or FRMS tags, otherwise "?" (D-169) |
| SNDH tag of an STE tune, FLAG tag | to confirm | Form to be checked again in the SNDH v2 documentation; nothing is written meanwhile (D-162, D-172) |
| Register exports in 2x3ch and in 16 channels | not kept | Reserved to "Authentic 3ch" as in the prototype (D-160) |
| SID wave tables, sidMin and Sync Buzzer steps in the SNDH; timer effects in VBL and YM3 | format | The SNDH plays the basic case; VBL and YM3 carry no effects, and the alert says so (D-172) |
| DMA sound in the recorder's take | not done | The recorder does not capture it (D-157); reported at export (D-172) |
| XM effects without an equivalent, ModPlug ADPCM samples; envelopes, auto-vibrato and fadeout of XM instruments | not rendered | The former are listed in the end-of-import alert, the latter are not (D-173) |
| CPU cost of the interface, compatibility with DAWs | M8 | Not measured |

**TO CONFIRM** (tests planned in DECISIONS.md): XM import on real modules, none being reachable from the working environment (D-173); SNDH tag of an STE tune and form of FLAG (D-162, D-172); licence of the SNDH durations database (D-169). Those of M6 remain: rate of the STE's DMA, its filter and its level against the YM (D-153).

## Indus's decisions

Decided: Q1 to Q9 of the plan, on 16/10/2026 from 14:26 to 14:33 (D-159 to D-167). Accepted by Indus on 16/10/2026 from 20:01 to 20:13, at the PR review: D-168 (JSON files), D-169 (YM Player), D-170 (MIDI), D-171 (WAV and stems), D-172 (register exports), D-173 (XM import) and D-174 (versioned files). No M7 decision is left PROPOSED.
