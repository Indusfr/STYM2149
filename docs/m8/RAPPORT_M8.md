# M8 report: quality, installers, documentation

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).

Decisions cited: [DECISIONS.md](../../DECISIONS.md) (D-175 to D-197). Plan approved by Indus on 16/10/2026 ("Go M8",
20:39 UTC); his answers to its ten questions are D-175 to D-185. Version 0.9.0 (D-179): Windows and Linux; 1.0.0 will be
the complete release with macOS and AAX.

## In short

- **Done**:
  - **Performance**: the CPU worst case drops from 2.93 % to 2.51 % of one core (budget 2.7 %, D-183), with output
    identical to the bit on 25 scenes (D-180). An editor frame with every window open drops from 8.0 to 3.6 ms.
  - **Robustness**: stress test of the plugin (block sizes, sample rates, modes and editor changed while playing);
    AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer in the CI; the crash of pluginval after its
    tests is understood and fixed (D-187).
  - **Validators**: pluginval level 10 (VST3 on Linux and Windows, LV2), clap-validator, and now the Steinberg VST3
    validator (D-184).
  - **Packages**: a Windows installer for the current user (Inno Setup, D-181) and a Linux archive with `install.sh`
    (D-182), built by the CI.
  - **Parity with the prototype**: all 466 rows of the M0 matrix are done and tested, or deviate or are left out by a
    decision ([PARITY.md](PARITY.md)). The audit found 18 rows missing or wrong, built or fixed in M8 (SHORTCUTS
    window, Clock selector driving the sequencer, menu columns, confirmation boxes...), and 28 rows without a test;
    their new tests found bugs in the sequencer's overview map and the keyboard destination menu, fixed.
  - **Tooltips** in English, French, German, Spanish, Portuguese and Italian (D-185).
  - **Documentation**: user manual in English and French, README in English and French, architecture guide, DAW test
    protocol, network audit, changelog, licence texts in the packages (D-178, D-186).
- **Waiting for Indus**: the DAW tests ([TESTS_DAW.md](TESTS_DAW.md)). Inno Setup is on the Windows runner since
  17/10/2026, so the CI builds the Windows installer.
- **Not done**: macOS (AU, auval, universal binary, pkg) and AAX (D-175); signing (D-176). Details below.

## Results

Measured on 17/10/2026: Linux x86-64 (Xeon 2.1 GHz), GCC 11.5 (the compiler of Indus's runners) and Clang 18.1,
Release, unless stated.

| Check | Threshold | Measurement | State |
|---|---|---|---|
| Unit tests (`unit`: chip, engine, sequencer, plugin core, file formats) | all pass | 372 test cases, 615 392 assertions | OK |
| Interface tests (`ui`) | all pass | 182 test cases, 9 299 assertions | OK |
| Plugin tests (`plugin`: processor, editor, tooltips, stress test) | all pass | 27 test cases, 2 788 assertions | OK |
| S1 to S11 (§10: fidelity, aliasing, determinism, real time) | as in M1 to M7 | unchanged, run by the CI on every push | OK |
| Visual test, 46 scenes of the prototype at 100 % | < 1 % of the pixels (2 % for four sequencer and Song scenes, D-121) | every scene under its threshold; new scene `help` 0.618 % | OK |
| STYM2149 scenes (19) | identical | identical | OK |
| CPU worst case (`ymbench --seq --notes 6 --noise`, 96 voices, 48 kHz) | < 2.7 % of one core (D-183) | 2.51 % (Xeon); 1.717 % on Indus's PC (CI step) | OK |
| Editor frame, every common window open | none (A2) | 3.6 ms at 100 %, 4.2 ms at 200 % | — |
| pluginval level 10 | no failure | VST3 (Linux, Windows), LV2 | OK |
| clap-validator 0.4.1 | no failure | CLAP (Linux, Windows) | OK |
| Steinberg VST3 validator | no failure | VST3 (Linux, Windows) | OK |
| ASan + UBSan (core and interface tests), TSan (plugin tests), RealtimeSanitizer (audio thread) | no report | none | OK |
| Network access, telemetry (§10) | none | none ([NETWORK_AUDIT.md](NETWORK_AUDIT.md)) | OK |
| Parity matrix (§10) | 100 % | 466 rows: 362 done, 102 deviations, 2 not kept ([PARITY.md](PARITY.md)) | OK |
| DAW tests (§10) | protocol run | protocol written; results due from Indus | Waiting |

## Performance (A1, A2)

**Engine (A1).** Each optimisation kept the output identical to the bit: `ymbench --hash` (new in M8) prints an FNV-1a
fingerprint of everything rendered, track and chip outputs included, on 25 scenes (modes, generators, outputs,
sample rates, block sizes), compared with `main` before each commit (D-180). Gains were counted in instructions
(callgrind, deterministic) and in time (best of 3, scenes alternated).

| Change | Effect |
|---|---|
| Hot steps of the virtual YM channel inlined; chip voices update the buses in local variables, as the DMA fast path | worst case 2.89 % → 2.58 %; chip voices alone 1.97 % → 1.70 % |
| Oscilloscope bucket bounds computed once per block | fewer instructions per change of bucket |
| One loop per combination of audible tone, noise and envelope | 3 to 9 % fewer instructions |
| Whole hot loop of the virtual channel flattened | 1 to 4.5 % fewer instructions |
| One DMA loop per case (ADSR ramp, mono or stereo) | 9 % fewer instructions on the DMA scene |

Altogether, 12 % to 21 % fewer instructions depending on the scene, and the worst case at 2.51 % of one core.

**Editor (A2).** The desktop keeps up to 16 separate dirty zones instead of their union, blends with integer
arithmetic, and the editor keeps an image already scaled to its window. One editor frame with every common window
open drops from 8.0 to 3.6 ms at 100 % (7.9 to 4.2 ms at 200 %, scaling included); a full JUCE paint at 200 % from 27
to 8.4 ms. Tests check that repainting a zone gives the same image as repainting everything.

## Robustness (A3 to A5)

- **Stress test** (`plugin` tests): random block sizes on an audio thread while the message thread switches voice
  mode, machine, chip clock, output chain and resampling, starts and stops the sequencer, and opens and closes the
  editor, at 44.1, 48, 96 and 192 kHz in real time, then offline. Every sample must be finite and the main output stay
  within [-1, 1].
- **Sanitizers** (D-184): a job on Indus's WSL runner. They found two defects, fixed: the desktop's root widget left a
  dangling pointer when destroyed first, and the allocation count of the real-time test blamed `processBlock` for
  allocations made by another thread.
- **pluginval crash after "SUCCESS"** (seen once in M7): pluginval now runs under gdb on Linux. The stack showed a bug
  of the VST3 host of pluginval's JUCE (its Linux run loop reads a table already destroyed); JUCE 9.0.2 fixes it, and the
  CI builds pluginval 1.0.4 with that fix (D-187). The plugin is unchanged.

## Packages (C1 to C4)

| Package | Contents | Installs into | State |
|---|---|---|---|
| `STYM2149-0.9.0-windows-x64-setup.exe` (Inno Setup 6, D-181) | VST3, CLAP, Standalone, licences | `%LOCALAPPDATA%\Programs\Common\VST3` and `…\CLAP`, `%LOCALAPPDATA%\Programs\STYM2149`; no administrator rights | Built by the CI on every run since Inno Setup is on the Windows runner (17/10/2026) |
| `STYM2149-0.9.0-linux-x86_64.tar.gz` (D-182) | VST3, LV2, CLAP, Standalone, licences, `install.sh` | `~/.vst3`, `~/.lv2`, `~/.clap`, `~/.local/bin` | Built by the CI on every run |

Neither is signed (D-176): the manual explains the SmartScreen warning. No AAX in the Windows installer (D-175).

## Parity with the prototype (B4)

The audit read the prototype again against the code, the tests and the decisions, row by row, for the 466 rows of
[docs/m0/PARITE.md](../m0/PARITE.md). It found 18 rows missing or wrong, 9 implemented but untested, and 19
whose only evidence was the code. All were closed in M8:

| Rows | What was done |
|---|---|
| P033, P248 | The SID timer ratio's tooltip described a Digidrum rate (D-029); fixed, tested |
| P168, P212, P213, P215, P216, P217, P392, P448 | SHORTCUTS window of the prototype, with its nine sections, then STYM2149's own keys (D-189) |
| P170, P189, P281 | The Clock selector sets the sequencer's clock as well as the arpeggiator's (D-188) |
| P270 | The Digidrum frame printed 7800 Hz whatever the slot's rate; fixed, tested |
| P400 | GEM menus in columns (the Arpeggiator's Chord... menu) |
| P415 | Unsaved work: the Standalone app keeps its session, File > Quit included (D-197) |
| P417, P006 | Confirmations show Cancel first; Enter and Esc cancel; Tab moves between buttons |
| P147, P153, P183, P230, P231, P294, P311, P313, P314 | Implemented but untested: tests added. They found that the Step Sequencer's overview map did not follow playback, the scroll bar or the transpose slider (P313); fixed |
| P229, P232, P261, P263, P355, P356, P411, P428, P430, P433, P434, P442, P451, P454, P455, P456, P457, P465, P466 | Only the code as evidence: behaviour tests added. They found that the keyboard destination menu opened at the pointer instead of under the key or pad, and read past the tables of voice names for the DMA tracks (P428); fixed. Delete at the cursor also empties the step's chord voices, where the prototype wrote a C in each (P457, D-194) |

Quantize to VBL (spec §11, D-111) had no entry in the interface; it is in Options > Sound engine (D-196).

42 rows are tested on their logic (sequencer edits, engine, state) but not through every mouse or keyboard gesture
of the interface: a popup, a context menu or a shortcut that leads to that logic. PARITY.md names the gap in the
note of each of these rows.

## Behaviour to know

- **Preferences file.** The editor's preferences are in `Preferences.settings`. Before 0.9.0 they shared the
  `STYM2149.settings` file of the Standalone app on Windows, which put back the preferences it had read at launch each
  time it saved; the first launch of 0.9.0 copies them over (D-197).
- **Clock on MIDI ext** (D-188): Play arms the sequencer and MIDI Start runs it; offline exports keep the internal
  clock, so an export is never silent waiting for a clock.
- **Lowest note** (P314): as in the prototype, the Step Sequencer's view never scrolls below B0, so the hatched
  rows under B0 are only reached by the test.
- **Drum preview** (P153): a drum hit written in the grid is previewed as the note the engine receives for that step,
  where the prototype sent the row number; the drum heard is the same.
- **Zxx in SNDH** (D-195): the replay routine keeps the last `Zxx` value in a byte whose address the init routine
  returns in A0. Where maxYMiser keeps its own sync byte is **TO CONFIRM**.

## Not done in M8

- **macOS and AAX** (D-175): no AU, auval, universal binary, pkg or AAX package. The code of M4 to M8 has never been
  compiled on macOS. 1.0.0.
- **Signing** (D-176): no code signing on any system.
- **DAW tests**: the protocol is written ([TESTS_DAW.md](TESTS_DAW.md)); Indus runs it with the Windows installer.
- **Still TO CONFIRM** without hardware or files: values measured on a real ST or STE (D-044, D-047, D-138, D-151,
  D-153), XM import of real modules (D-173), SNDH tags of an STE tune (D-162, D-172), licence of the SNDH durations
  database (D-169), the sync byte of maxYMiser (D-195).
- **Interface gestures of 42 rows** whose logic is tested (see Parity above).
- **Translation** of code comments, test names and tool messages into English (D-178): a separate pull request
  after M8.

## Decisions accepted by Indus

Indus accepted these choices of Claude on 17/10/2026 (status DECIDED in DECISIONS.md); the rest of M8 follows his
answers to the plan (D-175 to D-185) and his choice of writing Zxx (D-195). No M8 decision is left open.

| Decision | Choice |
|---|---|
| D-179 (part) | End of M8 is version 0.9.0 |
| D-180 | CPU optimisations kept only when the output stays identical to the bit (`ymbench --hash` on 25 scenes) |
| D-185 (details) | Options > Tooltip language, system language at first launch, accented letters of the Atari ST font; translations written by Claude, review by native speakers TO CONFIRM |
| D-186 | Licence texts shipped with the packages; libraries compiled in by JUCE listed in THIRD_PARTY.md |
| D-187 | pluginval 1.0.4 built in the CI with the JUCE fix of the VST3 host's Linux run loop |
| D-188 | The Clock selector drives the sequencer as well as the arpeggiator, as in the prototype |
| D-189 | SHORTCUTS window: the prototype's nine sections, then STYM2149's own keys |
| D-190 | The Arpeggiator's Chord... menu flows into columns as the prototype's chordCols (one column on the 944 px desktop) |
| D-191 | Keyboard of GEM boxes: in a confirmation, Cancel first with the focus, Tab and Shift+Tab move it, Enter and Esc cancel; in a text box, Cancel then OK and Enter in the field is OK |
| D-192 | Destructive actions of STYM2149 (Recorder Clear, Digidrum banks, Clear track) follow the prototype's confirmation: Cancel first and by default |
| D-193 | Menus show whole, without the prototype's scroll bar at 70 % of the window height; the longest (806 px) fits the 944 px desktop |
| D-194 | Delete at the cursor empties the chord voices of the step too |
| D-195 (form) | Zxx writes a sync byte of the replay routine, whose address the init routine returns in A0 |
| D-196 | Quantize to VBL in Options > Sound engine |
| D-197 | Preferences in `Preferences.settings`; File > Quit keeps the session of the Standalone app |
