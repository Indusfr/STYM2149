# STYM2149 — M0 report (audit)

12/10/2026. In the repository, DECISIONS.md and THIRD_PARTY.md are at the root; the other files are in `docs/m0/`. Basis: STYM2149_SPEC.md (identical to the « Prompt maître » ("master prompt")), YM2149_Prototype.html, ym2149-rs, AtariAudio 1.26, YM2149 datasheet, xia's maxYMiser guide, TTrak v1 archive.

## Done

- **Critical point decided**: the prototype computes the envelope 32 times too slowly. The datasheet (fE = fMaster / 256·EP, 32 steps), AtariAudio and ym2149-rs agree, and the measurement confirms it (19.41 Hz for EP = 400, expected 19.53 Hz; prototype 0.61 Hz). See AUDIT_DSP.md §1.
- **Audit of ym2149-rs**: the core is well tested but is not a faithful port of AtariAudio 1.26. Four measured deviations: volume 2v instead of 2v + 1 (−1.5 dB), ideal DAC and linear sum instead of the measured table, decimation by logical OR (hi-hat −10.8 dB, aliasing −12 dB), noise more accurate than AtariAudio's (whose LFSR is not maximal: period 114 681). AUDIT_DSP.md §2.
- **Measured trial §3.0**: the C++ port reproduces the crate and AtariAudio with 0 deviations over 2.2 M samples; CPU of one chip < 0.2 % of one core in both options, without allocation; alias-free chain at −114 dB. **Recommendation: option B (C++), on the AtariAudio model, without Rust in the binary.** AUDIT_DSP.md §4.
- **Parity matrix**: 466 rows (all the HTML and all the JS read), each with module, parameter and test. PARITE.md.
- **maxYMiser / TTrak deviation table**: 22 maxYMiser functions, 20 TTrak functions, with source and action. ECARTS_MAXYMISER_TTRAK.md.
- **Quantified fidelity thresholds** for M1 (S1 to S11). AUDIT_DSP.md §5.
- **DECISIONS.md** (your 6 decisions, the envelope formula decided by the measurement, and the proposed decisions) and **THIRD_PARTY.md** (draft).
- **9 anomalies of the prototype** found and checked (wrong WAV header, R13 written only once in the exports, tempo per block lost, etc.), with a proposed fix. DECISIONS.md D-022 to D-030.

## Not done (and why)

- Official maxYMiser manual not read: only the third-party guide was available; jam mode and detailed MIDI remain "TO CONFIRM".
- TTrak file formats (.TPM, .TPI, .4BI) not read: no documentation.
- Reference captures of the prototype for the visual diff (§6): planned in M4.
- Reference dumps (§10): not chosen yet (question Q5).
- Nothing is pushed to the repository (empty): the deliverables are in this `m0/` folder of the project (question Q10).

## Blocking

Nothing. M1 can start as soon as you agree.

## Decisions awaited (10 questions)

1. **EP of the prototype presets**: when importing a v1 file, convert EP × 32 to keep the sound as heard (beyond EP 2 047, capped at 65 535), or keep the raw value (sound 32 times faster)? ★ convert, with a warning if capped.
2. **Test oracle**: bit-for-bit reference = AtariAudio 1.26 rather than ym2149-rs (which diverges on 4 points)? ★ yes, ym2149-rs as secondary oracle.
3. **Noise**: maximal LFSR bit 0 ⊕ bit 3 (MAME, ym2149-rs) rather than AtariAudio's? ★ yes, to be confirmed the day a recording exists.
4. **Rust**: no Rust in the shipped binary (YM/SNDH/LZH imports through AtariAudio's MIT C++ code)? ★ yes.
5. **Reference dumps**: do I download 5 to 8 public YM/SNDH tunes (hi-hat, buzzer, SID, Sync Buzzer, Digidrum) for the tests, kept outside the repository, or do you provide them? ★ I download them.
6. **Default clock**: 2 000 000 Hz (YM files, AtariAudio) with an "STF PAL" preset at 2 005 312 Hz? ★ yes.
7. **Prototype anomalies** (D-022 to D-030): fix them rather than reproduce them? ★ fix, each one recorded.
8. **TTrak**: the only documentation is the help built into the program (text read in TTRAK.PRG, without disassembled code). Use it as public documentation? ★ yes, functions only.
9. **Information window**: the text "may not be given away, copied, sold or distributed" is incompatible with the AGPLv3. Replace it with the AGPLv3 notice and keep the credits? ★ yes.
10. **Repository**: first commit on `main` = skeleton (step 2) with these documents in `docs/`? ★ yes.

## Chosen architecture (proposed)

| Module | Content | Depends on |
|---|---|---|
| ymcore | chip at clk/8 in integers, measured mixing table, writes timestamped to the cycle, sinc resampler per bus, Raw / Raw-box / ST / STE / Monitor / Retro modes | nothing (pure C++20) |
| ymengine | voices, 3ch / dual chip / 16ch (event-driven counters), quantised MFP 68901, SID, Sync Buzzer, Digidrum, Chipdrum, ADSR, arpeggio, modulation | ymcore |
| ymseq | patterns, Song, instruments, sequences, clock and host sync, undo | ymengine (types) |
| ymio | prototype-compatible JSON, YM, SNDH v2, VBL, MIDI, WAV, stems | ymseq, AtariAudio (import) |
| gemui | GEM widgets in a framebuffer, EmuTOS bitmap font, integer scaling | JUCE (graphics only) |
| plugin | processor, editor, parameters, state, MIDI | all |

Audio thread: no allocation, no lock; exchanges through lock-free FIFOs and immutable snapshots.

## Files

| File | Content |
|---|---|
| RAPPORT_M0.md | this report |
| AUDIT_DSP.md | envelope, audit of ym2149-rs and of the prototype, A/B trial, thresholds |
| PARITE.md | parity matrix (466 rows) |
| ECARTS_MAXYMISER_TTRAK.md | deviation table §5 |
| DECISIONS.md | decided and proposed decisions |
| THIRD_PARTY.md | licences of the components (draft) |
| essai/ | code of the trial (C++, Rust C ABI layer, Python) and raw reports |
