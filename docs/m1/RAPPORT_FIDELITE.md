# M1 fidelity report: ymcore

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

Thresholds defined in M0 ([AUDIT_DSP.md §5](../m0/AUDIT_DSP.md)), approved on 12/10/2026. Decisions cited: [DECISIONS.md](../../DECISIONS.md).

## In short

- **Done**: S1 to S11 are met. The core follows AtariAudio 1.26 step by step, without a single deviation, over 158 million internal steps (11 tunes) and 10 million synthetic steps; RawBox mode is bit-identical to AtariAudio's player over 119 million samples; the ym2149-rs 0.9.1 crate is reproduced without deviation.
- **S9** is measured with the definition decided by Indus on 12/10/2026 (D-042, option A). Against the old definition, 7 tunes out of 11 exceeded ±1 dB at 16-20 kHz, because of AtariAudio's decimation and not of the chip (details below).
- **LHA decompression**: C++ port of pylzh (D-043), identical to AtariAudio's decompressor on the 1 962 archives of the ym2149-rs corpus.

## Results

Measured on 12/10/2026: Linux x86-64, GCC 13.3, Release, Xeon 2.1 GHz. The same build under Clang 18 passes every test except S9, which was not run there. The CI repeats everything under Linux, Windows (MSVC) and universal macOS, and publishes its measurements as an artefact (`mesures-*`).

| Id | Test | Threshold | Measurement | State |
|---|---|---|---|---|
| S1 | Internal state against the tick model of AtariAudio 1.26, at 250 kHz | 0 deviations | 0 deviations: 5 000 000 synthetic steps (AtariAudio LFSR, full state), 5 000 000 (maximal LFSR, state outside the LFSR), 158 320 004 steps over 11 tunes (60 s each) | OK |
| S2 | RawBox against `Ym2149c::ComputeNextSample` and AtariAudio's YM player, 44.1 / 48 / 96 kHz | 0 deviations | 0 deviations: 5 643 000 + 4 514 400 synthetic samples, 119 117 496 samples over 11 tunes; outside the CI, 1 717 files without effects from the ym2149-rs corpus (10 s each): 0 deviations | OK |
| S3 | "Raw-crate" port against the ym2149-rs 0.9.1 crate, 44.1 / 48 kHz | 0 deviations | 0 deviations over 2 763 000 samples (6 M0 scenarios and 4 random scenarios) | OK |
| S4 | HQ chain against a double-precision reference (D-040) | correlation ≥ 0.9999; distance ≤ 0.1 dB; error ≤ −90 dBFS | correlation 1; distance 2.5·10⁻⁷ dB; max error −135.5 (44.1 kHz), −122.7 (48 kHz), −111.5 (96 kHz), −124.3 dBFS (48 kHz, STF PAL clock); passband flat within 0.00043 dB | OK |
| S5 | Aliasing of a TP = 12 square wave below 20 kHz | ≤ −90 dB | −122.2 (44.1 kHz), −114.5 (48), −120.9 (88.2), −118.2 (96), −110.5 (176.4), −114.3 dB (192 kHz) | OK |
| S6 | Tone, noise and envelope frequencies | ±0.1 % | count of internal steps exact; on the HQ output, max deviation 0.0007 % (envelope $8, EP = 400: 19.53111 Hz for 19.53125 Hz) | OK |
| S7 | LFSR | period 131 071, MAME sequence | period 131 071; 0 deviations over 262 142 shifts; AtariAudio mode: 114 681 | OK |
| S8 | Registers, 16 shapes × 128 envelope steps, R13, period 0, mixer | 100 % | 100 % (unit) | OK |
| S9 | 11 tunes, 48 kHz (D-042) | correlation ≥ 0.99 and distance ≤ 1 dB against AtariAudio's player; third octave ±1 dB between maximal LFSR and AtariAudio LFSR | correlation 0.9919 to 0.9995; distance 0.585 to 0.937 dB; third octave 0.038 to 0.203 dB | OK |
| LZH | Port of pylzh against AtariAudio's decompressor | 0 deviations | 0 deviations over 22 tunes (3.6 MB) in the CI; outside the CI, 1 962 archives of the ym2149-rs corpus (208 MB): 0 deviations | OK |
| S10 | Two offline renders (3 modes × 2 noise LFSRs, new instance and `reset()`) | identical | bit-identical | OK |
| S11 | Allocations during `reset`, `write`, `render` (3 modes × 3 rates, queue overflow, YM player) | 0 | 0; RealtimeSanitizer (clang 20) in the CI only | OK |

## S9 in detail

Method: 60 s per tune (less for shorter tunes), the first 0.5 seconds discarded. Correlation and log-spectral distance compare HQ mode with AtariAudio's player using the same LFSR, so that only the decimation differs. The third octave (D-042, option A) compares the default HQ mode (maximal LFSR) with HQ mode using AtariAudio's LFSR, over the bands from 25 Hz to 20 kHz lying within 60 dB of the strongest: only the noise sequence differs. The last column, without a threshold, measures the deviation of the HQ chain from AtariAudio's player per third octave, with the same LFSR.

| Tune | Exercises | Correlation | Distance (dB) | Third octave, noise (dB) | Diagnostic: decimation (dB) |
|---|---|---|---|---|---|
| Decade Where's my willy | YM5, hi-hat, buzzer | 0.99948 | 0.746 | 0.087 | 1.11 (16 kHz) |
| DHS Intro | YM5, hi-hat, buzzer | 0.99792 | 0.600 | 0.144 | 0.91 |
| Digizak | YM5, hi-hat, buzzer | 0.99695 | 0.585 | 0.079 | 0.86 |
| Gritty | YM5, buzzer | 0.99941 | 0.736 | 0.050 | 1.11 (16 kHz) |
| Scout | YM5, hi-hat | 0.99881 | 0.937 | 0.108 | 0.85 |
| ST News 61 | YM6 without effects | 0.99936 | 0.843 | 0.092 | 1.38 (16 kHz) |
| Mon Tune 8 | YM5 | 0.99924 | 0.868 | 0.203 | 1.35 (16 kHz) |
| Ashtray | YM5, hi-hat | 0.99939 | 0.823 | 0.046 | 1.34 (16 kHz) |
| Prehistoric Tale (title) | YM2, buzzer | 0.99902 | 0.855 | 0.183 | 1.26 (16 kHz) |
| Simpsons | YM3, buzzer | 0.99194 | 0.659 | 0.038 | 0.49 |
| jimpowr5 | YM3b | 0.99795 | 0.764 | 0.104 | 1.19 (20 kHz) |

Why the definition changed: measured against AtariAudio's player, the energy per third octave exceeded ±1 dB on 7 tunes, always in the 16 and 20 kHz bands, and the same deviation appears with the same LFSR (last column). AtariAudio's box averaging (about 5.2 internal steps per sample at 48 kHz) attenuates by −0.6 dB at 10 kHz, −1.0 dB at 12.5 kHz and −1.6 dB at 16 kHz (computed as the discrete sinc of the box), and folds part of the spectrum above 24 kHz back; HQ mode keeps these bands flat within 0.00043 dB (S4). The criterion therefore measured the deviation intended by D-014, not an error of the chip: in RawBox mode, the render is bit-identical to AtariAudio (S2).

Coverage: §10 asks for at least 5 tunes covering hi-hat, buzzer, SID, Sync Buzzer and Digidrum. M1 covers hi-hat and buzzer. SID, Sync Buzzer and Digidrum are YM5/YM6 effects played in M2; the tunes that exercise them are already fetched (Credits, Iceage, Prelude, Steps, ND-Toxygene, LethalXcess1, Synth Dream 5, Wings2, WingsLeveltune3 and 5, Amazine 2) and will be added to S9 in M2.

## LHA decompression

C++ port of pylzh (D-043) in `src/ymio/src/Lzh.cpp`. On the 1 962 archives of the ym2149-rs corpus (all -lh5- at level 0), the result is identical byte for byte to AtariAudio's. 20 of these archives have a wrong header-size byte, which pylzh does not read; for them the port takes over AtariAudio's tolerance, and the CRC confirms the result. Once decompressed, 1 953 files are read by `parseYm`; the other 9 are in the MIX1 and YMT formats (M7) or are truncated YM5 files (D-041). The unit tests cover header levels 0 to 2 and the methods -lh0-, -lh5-, -lh6-, -lh7- (pylzh archives), as well as truncated or corrupt archives, without out-of-bounds reads (checked under AddressSanitizer).

## Delivered

- `src/ymcore`: YM2149 chip at clk/8 ported from AtariAudio 1.26 (D-016), maximal or AtariAudio LFSR (D-012), measured mixing table (D-013), deterministic edge seed (D-019); queue of writes timestamped to the cycle (D-037); Kaiser sinc resampler (D-039); three output modes Raw (HQ), RawBox, Retro; DC removal (D-035). No allocation or lock in `reset`, `write`, `render`.
- `src/ymio`: reading of YM2, YM3, YM3b, YM5, YM6 (D-041), LHA-compressed or not (D-043), and a frame player following AtariAudio's rules (D-036), with exact or AtariAudio timing (D-038).
- `tools/ym2wav`: renders a YM file (compressed or not) to a 16-bit or float WAV, at any rate and in the three modes. `tools/fetch_refdata.sh`: reference tunes (D-032).
- Tests S1 to S11 and LZH (Catch2) and GitHub Actions CI: Linux GCC with the ym2149-rs oracle and S9, Windows MSVC x64, universal macOS, Linux clang 20 with RealtimeSanitizer.

## Measured cost

Rendering 120 s of Scout.ym with `ym2wav` (reading the file and writing the WAV included), one chip, one core of the Xeon 2.1 GHz: RawBox 0.29 %, Retro 0.25 % at 48 kHz; HQ 2.48 % at 44.1 kHz (392 taps), 1.39 % at 48 kHz, 0.97 % at 96 kHz. The cost of HQ mode comes almost entirely from the filter; the 16-channel mode will have to share one resampler per output bus and use two stages (D-018, M2).

## Not done in M1

- YM5/YM6 effects (SID, Sync Buzzer, Digidrum) and YM2 digidrums: detected and counted, not played (M2). Their share of S9 comes with them.
- ST, STE and monitor output chains (analogue filters): M2.
- Syncing the square waves to the Timer (AtariAudio's `InsideTimerIrq`, used by SNDH files): M2 and M7.
- SNDH, MIX1 and YMT formats: M7. LHA decompression: the -lh1- method and level 3 headers are not ported (absent from known YM files).
- STE clock preset: no source found, **TO CONFIRM**. STF PAL (2 005 311.75 Hz) remains **TO CONFIRM** (D-017).
- Two-stage resampling and event-driven counters for the 16-channel mode (D-018): M2.
- Output headroom: in HQ mode, the filter's overshoot on a full-level square wave can clip (1 sample in 5 s of Scout.ym in 16 bits). Output gain and headroom belong to the engine (M2).
- JUCE plugin skeleton: M3, so as not to ship placeholder code.
- S4 reference in C++ rather than in scipy (D-040).
- RealtimeSanitizer could not be run locally (clang 20 is absent from the Ubuntu repositories): it runs in the CI only.
- Truncated YM files are refused, where AtariAudio reads out of bounds (D-041).
