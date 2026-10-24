# M5 report: sequencer

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

Decisions cited: [DECISIONS.md](../../DECISIONS.md) (D-090 to D-134). Third-party components: [THIRD_PARTY.md](../../THIRD_PARTY.md).

## In short

- **Done**: the prototype's sequencer plays in the engine, on the audio thread: 32 patterns of 1 to 256 steps over 16 tracks, chord voices 2 to 6, track arpeggios and slides, retriggers, conditions, step panning and effects, global gate (D-134), Song (repeats, per-block tempo, loop), metronome, Quantize to VBL. Three clocks: Free run (tempo, swing, 7 resolutions), MIDI clock (Start, Stop, Continue, Song Position Pointer) and Host sync, the default in a host (D-110, D-114). Patterns, Song and settings are kept in the plugin state (D-115). The interface opens six floating windows of the prototype, movable and stacked: Step Sequencer, Song, Mixer (track meters and oscilloscopes), Arpeggiator, Slide Curve and Computer Keyboard (playing from the computer keyboard). The items deferred from M4 are done: scroll bars, SYNC and STOP (D-084), CHIP rules on patterns (D-124), complete help line (D-122), Undo and Redo (D-123), pencil, scissors and move cursors. 96 voices played by the sequencer cost **2.64 %** of one core (budget: 3 %, D-133). Visual test: 35 scenes, all under their threshold.
- **Not done**: Modulation, Patches and Drum Kit windows (M6), files (M7), MIDI learn. Their buttons and menus still open the "not available in this version yet" alert (D-070). Details below.

## Architecture

| Layer | Role | JUCE |
|---|---|---|
| `src/ymseq` (new) | Editing model: the prototype's patterns, tracks and rows (LANEKEYS), Song, grid edits (notes, chord voices, blocks, transposition, Snap, step input), euclidean generator, tables, undo history; immutable snapshot published to the engine (shared tracks, copy on write) | no |
| `src/ymengine` | Playback: steps, gates, slides, arpeggios and chord voices of the tracks (`Sequencer.cpp`), three clocks, Song, metronome, Quantize to VBL; track meters and oscilloscopes; decision log for parity with the prototype | no |
| `src/plugincore` | SEQS, PATN and SONG state blocks; commands from the interface to the sequencer (Play, Stop, pattern choice, audition, jump in the Song); telemetry of the transport and the meters | no |
| `src/plugin` | Host transport given to the engine at each block; host MIDI notes passed to the editor through a lock-free queue (D-132) | yes |
| `src/gemui` | Floating windows, scroll bars, double click, key release and repeat, numeric keypad, cursors | no |
| `src/pluginui` | Step Sequencer, Song, Mixer, track oscilloscope, Arpeggiator, Slide Curve and Computer Keyboard windows; desktop widened to 1 272 × 944 px (D-067) | no |

## Results

Measured on 15/10/2026: Linux x86-64 (Xeon 2.1 GHz), GCC 11.5 (the one on Indus's runners) and Clang 18, Release.

| Check | Threshold | Measurement | State |
|---|---|---|---|
| Visual deviation, 35 scenes at 100 % (tolerance 4/255 per component, D-086), relative to the M4 desktop (1016 × 944) | < 1 % per scene; 2 % for `seq`, `seq_demo`, `song`, `song_demo` (D-121) | 0.598 % to 0.973 %; 1.031 % to 1.651 % for the four scenes at 2 % (table below) | OK |
| Parity with the prototype: the sequencer's voice log (triggers, releases, slides, arpeggios) compared with the worklet's, 16 channels, 3 channels, MIDI clock | identical | identical (`[proto]`: 10 test cases, 80 829 assertions) | OK |
| Deadline-based track countdowns (D-133): rendering in blocks of 1 and of 512 samples, internal clock and Host sync | same log, same sound to the bit | identical | OK |
| CPU load, 16 tracks of 6 voices played by the sequencer (`ymbench --seq --notes 6`, 48 kHz, blocks of 256) | < 3 % of one core | 2.64 % (varied scene); 2.90 % in the worst case (noise everywhere, or 96 Sync Buzzer); 2.05 % with chip channels only | OK |
| No allocation in `processBlock` (400 blocks: MIDI, automation, sequencer in Host sync then Free run, interface commands, screen and host notes, telemetry) | 0 | 0 | OK |
| Interface tests (`stym_ui`) | all pass | 50 test cases, 4 017 assertions | OK |
| Unit tests (`stym_unit`), including 34 `[seq]` cases and 10 `[ymseq]` cases | all pass | 167 test cases, 318 537 assertions | OK |
| JUCE processor and editor tests (`stym_plugin_tests`) | all pass | 18 test cases, 2 364 assertions (with and without a display) | OK |
| CI on Indus's runners: Linux and Windows, pluginval 1.0.4 level 10 (VST3, LV2), clap-validator, RealtimeSanitizer (S11) | pass | pass | OK |
| Compiler warnings in the project's code | 0 | 0 (GCC 11 and Clang 18) | OK |

| Scene | Deviation | Scene | Deviation | Scene | Deviation |
|---|---|---|---|---|---|
| default | 0.713 % | menu_shape | 0.868 % | mixer_scope | 0.730 % |
| sid | 0.705 % | menu_source | 0.715 % | arp | 0.733 % |
| sync | 0.696 % | alert_chip | 0.825 % | arp_on | 0.706 % |
| chipdrum | 0.728 % | seq ² | 1.563 % | arp_table | 0.893 % |
| digidrum | 0.663 % ¹ | seq_demo ² | 1.651 % | arp_midi | 0.598 % |
| envelope | 0.771 % | song ² | 1.031 % | curve | 0.754 % |
| smooth | 0.754 % | song_demo ² | 1.040 % | curve_custom | 0.866 % |
| mode3 | 0.694 % | mixer | 0.760 % | kbd | 0.929 % |
| dual | 0.793 % | mixer_3ch | 0.733 % | kbd_held | 0.921 % |
| tabs | 0.713 % | menu_fuji | 0.712 % | kbd_azerty | 0.942 % |
| menu_file | 0.690 % | menu_generator | 0.715 % | kbd_high | 0.973 % |
| menu_edit | 0.692 % | menu_options | 0.919 % | | |

¹ Excluding the waveform and the frame's note, rewritten by D-078 (D-086). ² 2 % threshold (D-121): small texts of the rows, the legend and the blocks in the GEM system fonts (D-071, D-119).

The remaining deviation comes from the intended differences of M4 (8×8 and 6×6 fonts, drawings without anti-aliasing, rewritten texts) and from those of the M5 windows: small texts of the Step Sequencer and the Song in 8×8 and 6×6 (D-119), All button of the global gate (D-134), Mixer strip labels in 6×6 and oscilloscopes without anti-aliasing (D-128), legend and keys of the Computer Keyboard (D-131). Frames, fill patterns, scroll bars and positions are identical to the captures.

Reproduce: `node tools/proto_capture/capture.mjs <folder>` then `gemshot <output> --ref <folder>`; `ctest -R ui` compares with the captures versioned in `tests/data/visual`. CPU load: `ymbench --seq --notes 6 [--gen chip|sid|buzzer|digidrum|chipdrum] [--noise]`.

## CPU load

D-054 waited for the sequencer to measure 96 voices in a single engine. The first measurement gave 3.55 %: a quarter of the time went into the track countdowns (gates, chord voices, CUT, retriggers), counted down at every sample as in the worklet. They now run by deadline, without changing a single bit of the output (D-133).

| Scene (96 voices, 48 kHz) | Before D-133 | After |
|---|---|---|
| Varied (SID, Sync Buzzer, Digidrum, Chipdrum, modulated chip channel; 93 voices on average) | 3.55 % | 2.64 % |
| Noise on every track | | 2.90 % |
| 96 Sync Buzzer | 3.52 % | 2.90 % |
| 96 SID | 3.35 % | 2.41 % |
| 96 chip channels | 2.70 % | 2.05 % |
| Varied at 44.1 kHz / 96 kHz | | 2.74 % / 2.93 % |
| Idle engine | | 0.54 % |

The rest is the cost of the M2 voices (bus update at each change of a voice's output). The margin is thin in the worst case; the cost of the open editor is not counted (M8).

## Behaviour to know

- **Clock**: Host sync in a host, Free run in Standalone (D-114). In Host sync, the plugin's Play and Stop, MIDI Start and Stop and the MIDI clock have no effect: the host leads playback, tempo and time signature (D-110).
- **Computer keyboard**: it only plays while the Computer Keyboard window is open. A plugin receives characters, not key positions: the layout is chosen in the Options > Keyboard layout menu, and Auto means QWERTY (D-131). A host may keep some keys (D-079).
- **Host notes**: they light up the piano and the keyboard drawing and are written into the sequencer (step input, or Rec during playback), on the focused track only (D-132).
- **Global gate**: All button after the Gate label of the Step Sequencer (Gate and Gate All parameters). On, every track plays the global gate and the slider sets it; off, each track gets its own back. A step's GAT keeps priority (D-134).
- **Randomness**: CND conditions, the Random arpeggio and Random walk slides are drawn from the playback position; two renders, offline included, are identical (D-104).
- **Undo**: Undo and Redo cover the history of the sequencer (patterns and Song), the mixer and the arpeggio of each track (D-123, D-128, D-129).
- **New engine** (chip clock, output chain, resampling): in Free run, playback restarts at the start of the pattern (D-116).

## Not done in M5

| Item | Where | Why |
|---|---|---|
| Modulation, Patches and Drum Kit windows | M6 | Outside §11 for M5 (sequencer, Song, mixer, host sync); buttons and menus show the alert (D-070) |
| Load, save, import, export (presets, patterns, Song, kit; MIDI, WAV, stems, YM3, SNDH), YM Player window | M7 (`ymio`) | Formats of §8; menus show the alert. Patterns and Song are already kept in the plugin state |
| MIDI learn | M6 (advanced MIDI) | Menu greyed out with the reason |
| Busy bee cursor | with the files (M7) | No long task in M5 |
| CPU cost of the interface, compatibility with DAWs | M8 | Not measured |
| Reference captures at zooms other than 100 % | to be decided | Unchanged since M4 (D-068) |

## Indus's decisions

Decided: D-121 (2 % threshold for the Step Sequencer and Song windows, « 2 % fenêtres » ("2 % windows")), the per-track gate of D-113 (« Par piste seulement » ("Per track only")) and the 1 272 px desktop of D-067 (« Bureau 1272 px » ("1272 px desktop")). D-090 to D-133, except D-113 and D-121, accepted by Indus on 16/10/2026 (line-by-line review of DECISIONS.md). Global gate added at his request, option « Prioritaire » ("Takes priority") (D-134). **Remaining**: D-113 (sequencer parameters) is still PROPOSED.
