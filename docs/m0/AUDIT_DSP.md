# DSP audit and measured trial §3.0 (M0)

Date: 12/10/2026. Sources read:
- `slippyex/ym2149-rs` commit `5e42228` (28/01/2026), crate `ym2149` 0.9.1, MIT.
- AtariAudio 1.26 (`arnaud-carre/AtariAudio`, commit `8471e84`), MIT, which ym2149-rs claims to port.
- YM2149 datasheet (Yamaha, 1992, bitsavers copy).
- `YM2149_Prototype.html` (AudioWorklet engine, l. 1100-2580).

Code and outputs of the trial: folder `essai/` (C++ bench, Rust C ABI layer, analysis scripts, raw reports `report.txt` and `analyse.txt`).
Measurement machine: Linux container, Intel Xeon 2.1 GHz, g++ 13.3 -O2, rustc 1.97 (release, LTO, panic=abort).

## 1. Critical point: envelope frequency

| Source | Formula | EP = 400 at 2 MHz |
|---|---|---|
| Datasheet (p. 6-7) | fE = fMaster / (256 × EP), 32 steps per cycle | 19.53 Hz |
| AtariAudio 1.26 and ym2149-rs | one step every EP ticks at clk/8, i.e. clk / (8 × EP) per step | 19.53 Hz |
| Measurement (render of both, FFT) | | 19.41 Hz (resolution 0.37 Hz) |
| Prototype (l. 2310, 2388, 2509; display l. 4144) | 2 000 000 / (256 × EP) steps/s, i.e. 2 000 000 / (256 × 32 × EP) per cycle | 0.61 Hz |

**Conclusion**: the prototype is indeed 32 times too slow, and it is consistent with itself (engine and display). The plugin follows the datasheet (D-010). Consequence for the prototype's presets: question Q1. The triangle ($A, $E) lasts 64 steps: clk / (512 × EP), measured 130.37 Hz for EP = 30 (expected 130.21 Hz).

## 2. Audit of the ym2149-rs core

The core is small (`crates/ym2149-core`, 2 223 lines with tests). It claims to be a cycle-exact port of AtariAudio, but diverges from AtariAudio 1.26 on four points:

| Point | AtariAudio 1.26 | ym2149-rs 0.9.1 | Measured effect |
|---|---|---|---|
| Fixed volume → 5 bits | 2v + 1 | 2v (`mixer.rs`, `compute_levels`) | volume 15 at −1.51 dB from the envelope maximum |
| DAC and mixing | measured 32×32×32 table (Paulo Simoes) | linear sum of `YM2149_LOG_LEVELS`, formula 1/√2^(n/2); the measured table `VOLUME_TABLE` (ST-Sound) is not used by the mixer | ideal DAC up to 11.5 dB too loud at low levels; 3 channels = 3× instead of 1.889× |
| Decimation | average of all the ticks of the sample | logical OR of the gates over the interval, level read once; corrected by a "half level" if TP ≤ 1 | noise NP = 1: −10.8 dB RMS compared with AtariAudio (the hi-hat loses its high-frequency energy); aliasing −12 dB |
| Noise | Fibonacci bit 0 ⊕ bit 2: period 114 681 (not maximal) | Galois $12000: period 131 071, sequence identical to MAME (bit 0 ⊕ bit 3) | on this point, the crate is more accurate than AtariAudio (D-012) |

Other findings:
- Timestamped writes: `write_port` queues them in a `VecDeque` (possible allocation on the audio thread). `cpu_cycles_per_sample` is an integer division (8 000 000 / 44 100 = 181 instead of 181.4): drift of 2.2 ms per second on timestamped writes. The immediate `write_register` API does not allocate (checked: 0 allocations over 3 × 60 s).
- `DcFilter` allocates at construction (`Box`), not during rendering.
- The "Sync Buzzer" is a special case (`in_timer_irq` + TP ≤ 1) inherited from AtariAudio; it does not emulate the MFP.
- The crate has neither 16 channels, nor Chipdrum, nor alias-free resampling, nor a measured DAC table: all of this remains to be written, as §3.0 plans.
- AtariAudio leaves three members uninitialised by `Reset()` (`m_noiseCounter`, `m_currentNoiseMask`, `m_edgeNeedReset`): to be set to zero (D-019).
- AtariAudio's MFP (`Mk68901.cpp`): clock 2 457 600 Hz, timers clocked at the host rate, so quantised to the host sample and not to the cycle. §3.3 asks for better: to do in M2.

## 3. DSP audit of the prototype (deviations to fix, §3.6)

| Point | Prototype | Plugin |
|---|---|---|
| Waveforms | floating-point phase at the host rate, without band limiting | integer core at clk/8 then sinc |
| DAC | 10^(−(31−n)·1.5/20), ideal (l. 1105) | measured table (D-013) |
| Mixing | sum + constant-power pan + `tanh` (l. 2457-2527) | measured 3-channel table in 3ch; in 16ch, documented sum per bus (studio mode, not authentic) |
| Noise | LFSR bit 0 ⊕ bit 3, rate 125 000 / NP (l. 1343) | same sequence (D-012), integer rate clk/16 |
| SID / Sync / Digidrum timers | continuous floating point at the host rate (l. 2408) | quantised MFP 68901 (prescalers 4…200, data 1-256), "Free" option |
| Register writes | AudioWorklet quantum of 128 samples, VBL frame to the sample | timestamped to the cycle |
| Volume 0 | index 1, ≈ −45 dB, not quite silent | index 1 = 0 in the measured table |

## 4. Measured trial §3.0: A (Rust crate via C ABI) versus B (C++ port)

### Port fidelity (sample by sample)

| Comparison | Scenarios | Deviations |
|---|---|---|
| C++ port of ym2149-rs versus Rust crate via FFI | 6 scenarios (3 tones, sawtooth, 10 kHz square, noise NP 1, buzzer, full mix) × 44.1 and 48 kHz, 2 210 400 samples | **0** |
| C++ "tick" port of AtariAudio versus compiled AtariAudio 1.26 | same scenarios | **0** |

A C++ port therefore reproduces the oracle bit for bit; the "deviations to track down" risk of option B is removed by the method.

### Quality (square TP = 12, 10 416.7 Hz, 48 kHz output)

| Chain | Worst aliased component below 20 kHz |
|---|---|
| ym2149-rs (logical OR) | −12.0 dB |
| AtariAudio (box average) | −15.5 dB |
| Proposed B: 250 kHz tick + 100 dB Kaiser sinc | **−114.4 dB** (target §3.4: −90 dB) |

### CPU (60 s of audio, full scenario, 48 kHz, best of 3 runs)

| Chain | Cost | Allocations during rendering |
|---|---|---|
| A. Rust crate via C ABI | 0.133 % of one core | 0 |
| B. C++ port of the crate | 0.197 % | 0 |
| Original AtariAudio 1.26 | 0.161 % | 0 |
| B'. C++ tick port (proposed base) | 0.158 % | 0 |
| Sinc resampler → 48 kHz (202 taps) | 0.69 % per mono channel | 0 |
| same → 44.1 kHz (392 taps) | 1.31 % per mono channel | 0 |
| same → 96 kHz (202 taps) | 1.42 % per mono channel | 0 |
| 16 channels × 6 voices (32 tick cores, without resampling) | 4.39 % | 0 |

Reading: the core of one chip costs less than 0.2 % in both options; the A/B difference is not significant. What weighs is the resampling (to be done per bus, in two half-band stages + polyphase in M1) and the 16-channel mode, above the 3 % budget with naive ticking (D-018).

### Build complexity

| | A. Rust via C ABI | B. C++ |
|---|---|---|
| Toolchain | Cargo + Corrosion in CMake, toolchain pinned to 1.88 by the repository, targets x86_64-pc-windows-msvc, aarch64 and x86_64-apple-darwin then `lipo` | CMake alone |
| Clean build of the core | 7.3 s (Cargo, crates.io dependencies: thiserror, syn…) | < 1 s |
| Size | `libymffi.a` 7.5 MB before linking (Rust std included) | a few tens of KB |
| Real-time safety | `panic=abort` mandatory across the ABI; `VecDeque` on the timestamped path | under control |
| 16-channel extension, Chipdrum, cycle-accurate timers | modify or fork the crate | in our code |

### Verdict

**Option B** for all the real-time code, based on the tick model of AtariAudio 1.26 (and not on the crate), with the deviations D-011 to D-014 recorded. For import/export (M7), AtariAudio already provides in C++ (MIT) the YM1-6/YMT readers, SNDH with 68000 (Musashi), LZH and ICE: the Rust crate is then no longer needed in the binary (question Q4). It remains useful as a secondary oracle, through its bit-exact C++ port, without Cargo in the CI.

## 5. Proposed fidelity thresholds for M1 (to be approved, §10)

Without a recording of a real ST (D-006), no threshold can apply to the hardware. The thresholds apply to the software references and to the properties in the datasheet.

| Id | Test | Threshold |
|---|---|---|
| S1 | Internal state of ymcore versus the AtariAudio 1.26 tick model (corrected for D-012 and D-019), 250 kHz stream, all reference dumps | 0 deviations |
| S2 | "Raw-box" mode versus `Ym2149c::ComputeNextSample`, 44.1 / 48 / 96 kHz | 0 deviations (except noise scenarios, where the deviation comes from D-012 only) |
| S3 | "Raw-crate" mode (test only) versus ym2149-rs | 0 deviations |
| S4 | HQ chain versus a double-precision reference resampling (scipy) of the same stream | correlation ≥ 0.9999; log-spectral distance ≤ 0.1 dB (20 Hz-20 kHz); max error ≤ −90 dBFS |
| S5 | Aliasing, square TP = 12, at 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz | ≤ −90 dB below 20 kHz |
| S6 | Measured frequencies: tone clk/(16·TP), noise clk/(16·NP), envelope clk/(256·EP) and clk/(512·EP) | ±0.1 % (or ±1 FFT bin) |
| S7 | LFSR | period 131 071, sequence identical to MAME |
| S8 | 10 envelope shapes × 32 steps, restart by writing the same value to R13, width of each register, period 0 = 1, inverted mixer, tone off = constant level | 100 % of the unit tests |
| S9 | Reference dumps (≥ 5: hi-hat, buzzer, SID, Sync Buzzer, Digidrum) versus AtariAudio's YM/SNDH player | correlation ≥ 0.99 and log-spectral distance ≤ 1 dB without noise; energy per third octave within ±1 dB with noise |
| S10 | Determinism: two offline renders | identical bit for bit |
| S11 | Real time: allocations, locks, system calls during `process` | 0 (RealtimeSanitizer or malloc interception) |
