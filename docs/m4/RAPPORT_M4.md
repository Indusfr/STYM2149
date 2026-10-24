# M4 report: GEM interface

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

Decisions cited: [DECISIONS.md](../../DECISIONS.md) (D-067 to D-089, decided by Indus on 15/10/2026). Third-party components: [THIRD_PARTY.md](../../THIRD_PARTY.md).

## In short

- **Done**: the plugin's editor is the prototype's main window, drawn by `gemui` in a GEM desktop of 1016 × 944 logical pixels, scaled by nearest neighbour from 50 to 200 % (Options > Zoom menu or resizing by the host). Every control of the main window works: tabs of the 16 tracks, SOURCE, ENVELOPE, VIBRATO, ADSR, GENERATOR, ADSR curve, SYSTEM, oscilloscope, register monitor, 4-octave keyboard or pads, Latch, Layer, octaves, Fuji, File, Edit and Options menus, CHIP and MIDI indicators, alerts and input boxes. Small texts in the GEM's 8×8 and 6×6 bitmap fonts (D-071). Visual regression test on 18 scenes of the prototype: **0.72 to 0.97 %** of pixels differ (threshold: 1 %).
- **Not done**: floating windows (Mixer, Step Sequencer, Song, Arpeggiator, Modulation, Patches, Drum Kit, computer keyboard…), scroll bars, busy bee cursor, playing from the computer keyboard. Their buttons and menus open a "not available in this version yet" alert (D-070). Details below.

## Architecture

| Layer | Role | JUCE |
|---|---|---|
| `src/gemui` | GEM desktop: 32-bit surface, drawing without anti-aliasing, EmuTOS 8×16 font at quarter-pixel positions, 8×8 and 6×6 for small texts, widgets (window, title bar, menu bar, menus and submenus, buttons, sliders with text entry, labelled frames, alerts and input boxes, tooltips), opacity groups, timers, mouse and keyboard | no |
| `src/pluginui` | The prototype's main window on a `Host` interface (parameters with gestures, non-automatable state, on-screen playing, telemetry, preferences, zoom); rasterised icons | no |
| `src/plugincore` | Telemetry published by the audio thread (`TelemetryTap`, triple buffer), track names (NAME state block), conversion of an audio file into a Digidrum | no |
| `src/plugin` | JUCE editor (`StymEditor`): host of the window, scaled image, mouse, keyboard, 30 Hz timer, preferences, file chooser; lock-free queue of screen notes in the processor | yes |
| `tools/gemfont`, `tools/proto_capture`, `tools/gemshot` | Font rasterised and checked against Chromium; captures of the prototype; rendering of the same scenes and comparison | tools |

`gemui` and `pluginui` have no dependency on JUCE: the window can be tested and rendered to PNG without a host (`gemshot`, `stym_ui`).

## Results

Measured on 15/10/2026: Linux x86-64, GCC 11.5 (the one on Indus's runners) and Clang 18, Release.

| Check | Threshold | Measurement | State |
|---|---|---|---|
| Visual deviation, 18 scenes at 100 % (tolerance 4/255 per component, D-086) | < 1 % per scene | 0.724 % to 0.967 % (table below) | OK |
| Deterministic rendering (same scene rendered twice) | identical | identical | OK |
| 8×16 font: rasterised glyphs compared with Chromium (9 sizes, 4 quarter pixels, 103 glyphs) | 0 pixels differ | 3 708 glyphs, 0 pixels differ | OK |
| 8×8 and 6×6 fonts: shape checked at conversion, glyphs placed on the baseline, wrapped tooltip | as in EmuTOS | as in EmuTOS (`[gemui]` tests) | OK |
| Interface tests (`stym_ui`) | all pass | 10 test cases, 2 970 assertions | OK |
| Unit tests (`stym_unit`), including 23 `[plugin]` cases | all pass | 116 test cases, 243 831 assertions | OK |
| JUCE processor and editor tests (`stym_plugin_tests`), including 6 `[editor]` cases | all pass | 15 test cases, 2 242 assertions (with and without a display) | OK |
| Editor: image identical to the GEM desktop at 100 %, blocks of 2 × 2 at 200 % | identical | identical | OK |
| No allocation in `processBlock` (400 blocks, MIDI, automation, screen notes, published telemetry) | 0 | 0 | OK |
| pluginval 1.0.4 level 10, VST3 and LV2, with the GEM editor (Editor, Editor Automation) | pass | pass | OK |
| Compiler warnings in the project's code | 0 | 0 (GCC 11; Clang 18 outside the JUCE layer, which is compiled by GCC locally and by MSVC in the CI) | OK |

| Scene | Deviation | Scene | Deviation | Scene | Deviation |
|---|---|---|---|---|---|
| default | 0.761 % | envelope | 0.818 % | menu_edit | 0.740 % |
| sid | 0.753 % | smooth | 0.802 % | menu_options | 0.967 % |
| sync | 0.743 % | mode3 | 0.742 % | menu_fuji | 0.760 % |
| chipdrum | 0.776 % | dual | 0.841 % | menu_generator | 0.763 % |
| digidrum | 0.724 % ¹ | tabs | 0.761 % | menu_shape | 0.916 % |
| menu_file | 0.738 % | menu_source | 0.763 % | alert_chip | 0.873 % |

¹ Excluding the waveform and the frame's note, whose texts are rewritten by D-078 (1.02 % with them, D-086).

The remaining deviation comes from the intended differences: small texts in the GEM's 8×8 and 6×6 fonts instead of the reduced 8×16 font (D-071: 0.44 points in the default scene), icons and drawings without anti-aliasing (D-075), EP as the chip value (D-073), Digidrum texts (D-078), shortened help line (D-079), "Sound engine" entry of the Options menu (D-080), MIDI indicator (D-074). The 13 to 16 px text, the frames, the fill patterns and the positions are identical to the captures.

Reproduce: `node tools/proto_capture/capture.mjs <folder>` (captures of the prototype, Playwright and Chromium on the development machine), then `gemshot <output> --ref <folder>`; the `ctest -R ui` test compares with the captures versioned in `tests/data/visual`.

## Behaviour to know

- **Zoom**: 100 and 200 % give regular pixels; the other factors, and HiDPI screens with a non-integer scale (Windows at 125 or 150 %), give irregular blocks, by nearest neighbour and without smoothing (§6.6, D-068).
- **Preferences**: zoom, tooltips, note names and keyboard layout are shared by all instances and do not go into the project (D-069).
- **Screen notes**: played on the focused track at the start of the next block; closing the editor releases them, Latch included (D-087).
- **Keyboard focus**: the editor takes it on a click; a host may keep some keys (space bar, host shortcuts) (D-079).
- **Chipdrum kit**: still rebuilt on the audio thread when it changes (limit of M3); with no Drum Kit window in M4, it only changes when a state is read back or with the VBL frequency.

## Not done in M4

| Item | Where | Why |
|---|---|---|
| Mixer, Step Sequencer, Song windows | M5 (`ymseq`) | They serve the sequencer; their buttons and menus show the alert (D-070) |
| Arpeggiator, Modulation, Patches, Drum Kit, Slide Curve, YM Player windows, per-track oscilloscopes | to be planned with Indus (M5 to M7) | Outside the M4 criterion (« fenêtre principale » ("main window")); meanwhile, arpeggio and modulation are set through the host parameters, and ornaments, arpeggio tables and kit can only be changed through the state |
| Movable and stacked floating windows, `Esc` on the top one (§6.5) | with the first floating window | A single window in M4 |
| Computer keyboard (playing, octave with Z / X, window), Keyboard layout setting | M5 | The preference is kept (D-069); playing comes with its window |
| Scroll bars (§6.4) | with the lists of the floating windows | No list to scroll in the main window |
| Busy bee cursor; pencil, scissors and move cursors (§6.4) | M5 and later | No long task in M4; sequencer tools in M5 |
| SYNC and STOP of the MIDI clock in SYSTEM | M5 | They follow the sequencer's transport (D-084) |
| Rules of the CHIP indicator on pattern notes | M5 | They apply to patterns (D-076) |
| Files: load, save, import, export, demo pattern, Undo, Redo, MIDI learn | M5 to M7 | Menus show the alert, or are greyed out with the reason (D-070) |
| CPU cost of the interface | M8 | Not measured; only the repainted zone is copied, 30 times per second at most |
| Reference captures at zooms other than 100 % | to be decided | §6 sets the criterion at 100 %; the other zooms are checked against the desktop itself (D-068) |

## Indus's decisions

D-067 to D-089 are decided (15/10/2026): D-085 keeps Attack and Decay at 1 000 ms in Chip ADSR; D-071 and D-072 write the small texts and the tooltips in the GEM's 8×8 and 6×6 fonts, at his request; D-086 lets the Digidrum scene ignore the waveform and the frame's note, rewritten by D-078. No M4 decision is pending.
