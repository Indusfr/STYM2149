# M2 report: ymengine

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

Decisions cited: [DECISIONS.md](../../DECISIONS.md). Thresholds S1 to S11: [AUDIT_DSP.md §5](../m0/AUDIT_DSP.md) and [M1 report](../m1/RAPPORT_FIDELITE.md).

## In short

- **Done**: `ymengine` voice engine playable from a MIDI keyboard in the three modes (3 channels, dual chip, 16 channels), with MFP timers, SID, Sync Buzzer, Digidrum, Chipdrum, ADSR (smooth or "Chip"), vibrato, ornaments (modulation), arpeggio, slides and legato; timer effects of YM files played by the player; ST and STE output chains. The YM player stays identical to AtariAudio on 22 tunes, 11 of them with effects (S1, S2); S9 passes on all 22. The engine follows the prototype where the prototype is the reference (pitches, slides, chords, arpeggio, VBL frames), and every intended deviation is recorded (D-049 to D-052).
- **Not done**: sequencer and chord voices (M5), per-track outputs (M3), programmable SID (M6), syncing the square waves to the SNDH timer (M7). The CPU budget for 96 voices is met by projection (≈ 2.3 %); the direct measurement in a single engine awaits the sequencer's chord voices (M5).

## Results

Measured on 13/10/2026: Linux x86-64, GCC 13.3, Release, one core of a Xeon 2.1 GHz (virtual machine). Every test also passes under Clang 18; the engine tests pass under ASan and UBSan. The CI repeats everything under Linux, Windows (MSVC) and universal macOS, plus RealtimeSanitizer (clang 20).

| Id | Test | Threshold | Measurement | State |
|---|---|---|---|---|
| S1 | Internal state against AtariAudio 1.26, step by step, timer effects included (AtariAudio model, D-044) | 0 deviations | 0 deviations over 323 320 004 steps (22 tunes of at most 60 s, 11 of them with effects) and 10 000 000 synthetic steps | OK |
| S2 | RawBox against AtariAudio's YM player, 44.1 / 48 / 96 kHz | 0 deviations | 0 deviations over 243 263 496 samples (the same 22 tunes) | OK |
| S4 | HQ chain against the double-precision reference, exact phases (D-053) | error ≤ −90 dBFS | −135.7 (44.1 kHz), −123.5 (48), −110.0 (96), −124.6 dBFS (48 kHz, STF PAL clock); correlation 1; passband flat within 0.00043 dB | OK |
| S5 | Aliasing of a TP = 12 square wave below 20 kHz | ≤ −90 dB | unchanged: −122.2 to −110.5 dB from 44.1 to 192 kHz | OK |
| S9 | 22 tunes, 48 kHz (D-042, D-046) | correlation ≥ 0.99; distance ≤ 1 dB over 20 Hz-15 kHz; third octave ±1 dB between LFSRs | correlation 0.9919 to 0.9997; distance 0.179 to 0.605 dB; third octave 0 to 0.203 dB | OK |
| S11 | Allocations in the audio thread: chip, YM player, engine (3 voice modes × 3 outputs × 3 rates × 2 timer modes, settings and events during rendering, full queue) | 0 | 0; RealtimeSanitizer in the CI | OK |
| LZH | Port of pylzh against AtariAudio | 0 deviations | 0 deviations over 22 tunes (3 645 350 bytes) | OK |
| CPU | 16 tracks × 6 voices with effects, 48 kHz (§10) | < 3 % of one core | projection 2.3 % (2.5 % with noise everywhere); upper bound with 3 engines 3.2 to 3.3 % (details below) | see "CPU" |

## S9 in detail

Method unchanged since M1; log-spectral distance measured over 20 Hz-15 kHz (D-046). The two diagnostic columns, without a threshold, measure the deviation from AtariAudio above 15 kHz, where its box averaging sags and folds back the treble of fast effect writes.

| Tune | Exercises | Correlation | Distance 20 Hz-15 kHz (dB) | Third octave, noise (dB) | Diagnostic 15-20 kHz / full band (dB) |
|---|---|---|---|---|---|
| Decade Where's my willy | YM5, hi-hat, buzzer | 0.99948 | 0.511 | 0.087 | 1.20 / 0.746 |
| DHS Intro | YM5, hi-hat, buzzer | 0.99792 | 0.468 | 0.144 | 0.886 / 0.600 |
| Digizak | YM5, hi-hat, buzzer | 0.99695 | 0.397 | 0.079 | 0.945 / 0.585 |
| Gritty | YM5, buzzer | 0.99941 | 0.522 | 0.050 | 1.16 / 0.736 |
| Scout | YM5, hi-hat | 0.99881 | 0.605 | 0.108 | 1.55 / 0.937 |
| ST News 61 | YM6 without effects | 0.99936 | 0.530 | 0.092 | 1.41 / 0.843 |
| Mon Tune 8 | YM5 | 0.99924 | 0.556 | 0.203 | 1.44 / 0.868 |
| Ashtray | YM5, hi-hat | 0.99939 | 0.529 | 0.046 | 1.37 / 0.823 |
| Prehistoric Tale (title) | YM2, buzzer | 0.99902 | 0.497 | 0.183 | 1.48 / 0.855 |
| Simpsons | YM3, buzzer | 0.99194 | 0.275 | 0.038 | 1.23 / 0.659 |
| jimpowr5 | YM3b | 0.99795 | 0.513 | 0.104 | 1.24 / 0.764 |
| Credits | YM6, effects | 0.99970 | 0.179 | 0.097 | 0.636 / 0.354 |
| Iceage (digi) | YM5, digidrums | 0.99931 | 0.295 | 0.035 | 0.700 / 0.433 |
| Prelude | YM6, effects | 0.99945 | 0.255 | 0.062 | 0.536 / 0.347 |
| Steps | YM6, effects | 0.99952 | 0.249 | 0.121 | 0.616 / 0.376 |
| ND-Toxygene | YM6, Sync Buzzer | 0.99857 | 0.441 | 0.091 | 2.71 / 1.41 |
| LethalXcess1 | YM5, effects | 0.99838 | 0.376 | 0.031 | 4.29 / 2.17 |
| Synth Dream 5 | YM5, effects | 0.99963 | 0.230 | 0 | 1.09 / 0.580 |
| Wings2 | YM2, digidrums | 0.99968 | 0.288 | 0.025 | 0.691 / 0.427 |
| WingsLeveltune3 | YM2, digidrums | 0.99968 | 0.288 | 0.022 | 0.799 / 0.471 |
| WingsLeveltune5 | YM2, digidrums | 0.99960 | 0.266 | 0.051 | 0.578 / 0.369 |
| Amazine 2 | YM6, effects | 0.99974 | 0.545 | 0.169 | 1.20 / 0.763 |

## Voice engine

Tests `[engine]` (18 cases), `[proto]` (7), `[kit]`, `[mfp]`, `[channel]`, `[board]`, `[effects]` in `stym_unit`.

| Check | Result |
|---|---|
| A single voice in 16 channels against channel A of the chip (Raw, RawBox, Retro × Raw, ST, STE, with and without tanh) | identical up to the gain (deviation ≤ 2·10⁻⁶ of the peak, plus 2 steps of 1/32 768 in AtariAudio's integer paths) |
| Event-driven virtual YM channel against channel A of the chip, step by step (24 random scenarios of 400 000 steps, two LFSRs) | 0 deviations in state or output |
| Block size (1, 37, 256, 480, 1 024, 4 096 samples), three voice modes | bit-identical output |
| Note released in 16 channels | voice freed, output exactly zero |
| Registers in 3 channels: writes of a note, fade, velocity, R6, R11-R13 (D-050), Digidrum, dual chip, mute | as specified, to the cycle |
| SID on the MFP timer or continuous, mode frozen at the attack (D-052) | period of the R8 writes within 1 cycle |
| Smooth ADSR, stages and sustain; pitch bend; arpeggio (release, MIDI clock); mode change during rendering; full event queue | as specified |
| Chipdrum kit, 13 banks × VBL 50/60/70 | identical to the prototype |

**Parity with the prototype** (`tests/data/proto/engine.txt`, produced by running the prototype's worklet in Node with `tools/proto_golden/engine.mjs`):

| Element | Deviation |
|---|---|
| Tone periods, 128 notes × 4 pitch bend values | 0 |
| 13 slide curves, 4 seeds, 47 positions | ≤ 4 units in the last place (pow, exp, sin of V8 against the C library) |
| Slide quantisation (Chromatic, Scale-locked) | ≤ 2 units in the last place |
| 39 chords | 0 |
| Arpeggio played from the keyboard, 6 modes, 1 to 3 octaves, free table | 0 (40 steps per case) |
| VBL frames: ornament (pitch, noise, volume, loop, rate), vibrato, "Chip" ADSR, SID, Sync Buzzer, VBL 50/60/70 | 0 (period, volume, noise, timer frequency within 10⁻¹², 40 frames per case) |
| Legato of a monophonic track (45 ms slide) | 0 (period at each step) |

Intended deviations from the prototype: D-049 (measured 16-channel levels, output), D-050 (shared registers in 3 channels), D-051 (anomalies fixed), D-052 (time base in master cycles, timer mode frozen at the attack).

## CPU

`build/tools/ymbench/ymbench`: the 16 tracks play in layers, each with a generator using a timer effect (SID, Sync Buzzer, Digidrum, Chipdrum) or a modulated chip channel (ornament, vibrato, "Chip" ADSR), with noise on every other track; two notes retriggered every 250 ms. Duration 10 s, best of 5 measurements, blocks of 256, 48 kHz, Raw output, MFP timer, tanh on.

| Scene | Voices (mean / max) | GCC 13 | Clang 18 |
|---|---|---|---|
| Idle engine, 16 channels (`--notes 0`) | 0 | 0.47 % | 0.47 % |
| Idle engine, 3 channels | 0 | 0.38 % | — |
| 1 engine, 16 channels | 30 / 32 | 1.05 % | 1.10 % |
| 3 engines, 16 channels | 88 / 96 | 3.19 to 3.31 % | 3.30 % |
| 3 engines, noise on every track | 88 / 96 | 3.47 % | 3.67 % |
| 3 engines, continuous timer | 88 / 96 | 3.31 % | — |
| 3 engines, blocks of 64 | 88 / 96 | 3.44 % | — |
| 3 engines, Retro output | 88 / 96 | 2.44 % | — |
| 1 engine, 44.1 kHz / 96 kHz | 30 / 32 | 1.38 % / 1.42 % | — |
| 1 engine, RawBox output | 30 / 32 | 0.80 % | — |
| 3 channels / dual chip | 3 / 6 | 0.44 % / 0.81 % | 0.45 % / 0.86 % |

Reading: one engine plays only 32 voices from the keyboard (16 for the tracks and 16 for manual playing); the 80 chord voices come with the sequencer (M5). The 96 voices are therefore measured with three engines, which counts the fixed cost of an engine (two resamplers, 0.47 %) three times. A single engine with 96 voices would cost about 3.25 − 2 × 0.47 ≈ 2.3 % (2.5 % with noise everywhere, 2.4 and 2.7 % under Clang), under the 3 % of §10. The upper bound with three engines exceeds 3 % by 0.2 to 0.7 points. The prototype's output tanh (D-049) costs about 0.15 points per engine; `softClip = false` removes it. These figures come from a Xeon 2.1 GHz in a virtual machine, not from a recent desktop core: the reference measurement on target hardware remains to be done in M8.

Optimisations that brought the cost from 4.39 % (naive tick core, D-018) down to these values: virtual YM channels that jump from one edge to the next, one bus per side integrated as steps and ramps, sampled percussion outside the virtual channel, resampling only once per bus, exact filter phases (D-053).

## Not done in M2

- Sequencer track voices, lane arpeggios, chord voices (80 reserved voices): M5, with the CPU measurement of 96 voices in a single engine.
- Per-track outputs and automatable parameters: M3.
- SID with a programmable waveform (maxYMiser): M6.
- Syncing the square waves to the timer (AtariAudio's `InsideTimerIrq`): only SNDH files use it, no YM effect or prototype feature does; M7.
- "Monitor" chain: removed in v1 (D-048).
- Phase of the MFP prescaler at start-up and real amplitude of the interrupt jitter: **TO CONFIRM** (D-044); values of the ST and STE filters: **TO CONFIRM** on hardware (D-047).
- TSan: the engine has only one thread; it applies to the plugin (M3).
- Sample rate change during playback: `prepare` allocates and runs outside the audio thread; the full stress test belongs to the plugin (M3).
