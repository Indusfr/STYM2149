# Deviation table maxYMiser / TTrak → STYM2149 (M0, §5)

Rule: functions only, from the available documentation; no code, screen, sample or data taken over.

**Sources used**
- [MX] Third-party "maxYMiser" guide by xia (Per Almered), `preromanbritain.com/maxymiser/maxymiser-by_xia.pdf`, read through an automatic summary (the PDF could not be downloaded as is from this container). The guide calls itself "not a complete manual": jam mode, detailed MIDI and file formats are not described in it.
- [SP] "Confirmed" facts of §5 of STYM2149_SPEC.md (alive.atari.org, st-computer.org, AtariUpToDate).
- [TT] Archive `ttrak_v1.zip` provided by Indus (TTrak beta v1 "SOMMARTTRAK", Damo/RG, 2014-2020). The `READ.ME` holds only 6 lines; the only documentation is the built-in help (help pages and tooltips), whose text was read in `TTRAK.PRG`. No code was disassembled. See question Q8.
- The binary formats `.TPM` (module), `.TPI` (instrument, 1 055 bytes; 1 705 with sample) and `.4BI` (4-bit sample) are documented nowhere: not read.

Prototype status: **Abs** = absent, **Part** = partial, **Yes** = present. Milestone: M6 unless stated.

## maxYMiser

| # | Function | Source | Prototype | Proposed action |
|---|---|---|---|---|
| X1 | Vertical hexadecimal pattern editor: 64 rows, columns Note-Octave, Instrument, Volume, FX1, FX2 per YM channel | MX, SP | Abs (piano roll) | Tracker view, second view of the same data model |
| X2 | One pattern per channel, Song order per channel | MX | Abs (pattern = all the tracks) | Keep the prototype's model; the tracker view shows the current pattern channel by channel (choice to record in DECISIONS.md) |
| X3 | Volume column = negative offset, `f` = note-off; instrument `00` = no retrigger (hammer-on, portamento target) | MX | Part (absolute VOL 0-15, TIE) | Add note-off and "no retrigger" to the tracker view |
| X4 | Commands: `Bxx` break, `Hxx` glissando ±, `Lxx` volume sequence, `Pxx` portamento, `Vxx` vibrato sequence, `Axx` arpeggio sequence, `Xyz` simple arpeggio, `Sxx` speed, `Zxx` demo sync | MX | Part (SLD, CRV, ARP) | Effect column interpreter; `Zxx` with no effect on the sound (SNDH export only) |
| X5 | Reusable instruments up to $20, named | MX | Abs (settings per track, patch library) | Instrument list; a patch becomes an instrument |
| X6 | 7 sequences per instrument: Vol, Arp, Vib, Mix, Noi, Fix, Tim; length ≤ $1F; Repeat field; common sequence speed | MX | Part (64-step modulation table: pitch, NSE, VOL) | Generic sequences with loop; the modulation table becomes three of them |
| X7 | Mix sequence: choice of the generator per tick (noise, square, buzz, SID, SyncBuzz, custom SID, DigiSample) | MX | Part (GEN per step) | Generator per instrument tick |
| X8 | Fix sequence: fixed frequency | MX | Abs | Add |
| X9 | Tim sequence: SyncBuzz up to 8 steps, SID with a custom shape up to 16 steps | MX, SP | Abs (square SID) | SID waveform editor (§5) |
| X10 | Up to 3 simultaneous SID voices | SP | Part (one SID voice per track) | Three independent MFP timers in 3ch (A, B, D; C reserved for the system, TO CONFIRM) |
| X11 | Buzz Wave $8/$A/$C/$E; buzz and SyncBuzz always at max volume; only one buzz active, priority to the highest channel | MX | Part (Sync Buzzer, buzzer priority) | Fine tuning of the buzzer, choice of shape, same priority rule |
| X12 | Vibrato generator: Minimum, Maximum, Oscillations $01-$0F, ramp, length | MX | Part (vibrato depth/speed) | Vibrato table generator |
| X13 | Detune matrix: Square / Buzz / Timer generators × Arp / Port-Slide / Vib | MX | Abs | Add (same idea as TTrak's "Tuning Matrix", T9) |
| X14 | Instrument volume (negative offset) | MX | Part (track level) | Add at instrument level |
| X15 | STE DMA channels 4 and 5: native 6 258 / 12 517 / 25 033 / 50 066 Hz, 1ch/2ch modes tuned to the semitone; 8 samples of 32 KB, 8-bit signed; Sign/Unsign tools and destructive gain | MX, SP | Abs | Optional DMA channel; STE rates and LMC1992 filter **TO CONFIRM** (ym2149-rs `lmc1992.rs` gives "measured" values to audit) |
| X16 | Mutes Chan 1-3, STe, DMA | MX | Part (mute per track) | Extend to the DMA channel |
| X17 | Interrupt field (tick rate) and Speed (ticks per row) | MX, SP | Part (global VBL 50/60/70) | Replay rate and speed per row (`Sxx`) |
| X18 | Song: special patterns empty, note-off, loop; `n` = next free | MX | Part (linear Song with repeats) | Extend the Song: jumps, loop, special entries |
| X19 | Keyboard: rows Z/X/C and Q/W/E as a piano, F1-F8 octave, Caps Lock = note-off, keypad +/- = instrument | MX | Part (computer keyboard window) | Shortcuts of the tracker view, explicit focus (§7) |
| X20 | Full MIDI (channels, Program Change, velocity, clock) | SP (not described in MX) | Part | Routing per channel and mapping table (§7); details **TO CONFIRM** for lack of an official manual |
| X21 | SNDH v2 load / save | SP, MX ("SNDH" button) | Part (PRISM SNDH only) | Standard SNDH v2 export (M7) |
| X22 | Jam mode | MX (mentioned, not described) | ? | Nothing as long as it is not documented (Q8 / official manual) |

## TTrak

| # | Function | Source | Prototype | Proposed action |
|---|---|---|---|---|
| T1 | Song: position, pattern per position, length, restart pattern, insert/delete a position | TT | Part | Covered by X18 (restart point) |
| T2 | Engine rate 50 / 100 / 200 Hz | TT | Part (VBL 50/60/70) | Replay rate 50-200 Hz (with X17) |
| T3 | Groove editor: extra ticks per step | TT | Part (swing 50-75 %) | Groove table in ticks |
| T4 | Editing: scroll lock, edit step, octave, pattern loop, play from the step, block start/end (F1/F2), paste over/under, transpose by semitone/octave, select track/pattern | TT | Part | Tracker view (X1) with these commands |
| T5 | Amplitude sequence of the instrument: length, loop, speed 0-FF; flags per step T (timer), N (noise), M (square muted), B (buzzer) | TT | Part | Covered by X6/X7 |
| T6 | Timer effects: 1 SID, 2 user wave of 2 steps, 3 wave of 16 steps, 4 morphed wavetable (2 × 16 steps + morphing envelope), 5 Sync Buzzer of 4 steps, 6 octave FM, 7 Digidrum | TT | Part (SID, Sync, Digidrum) | Add 2, 3, 4, 6 as timer generators (with X9) |
| T7 | Arpeggio and noise sequences (pitch 0-1F, 20 = mute), loop | TT | Part | Covered by X6 |
| T8 | Fixed frequency, detune ±, random "analog" pitch | TT | Abs | With X8, X13 |
| T9 | Tuning Matrix: pitch modulation sources to Square, Buzzer, Timer | TT | Abs | With X13 |
| T10 | Instrument chaining (Link to) at the end of the amplitude sequence | TT | Abs | Add (option) |
| T11 | Hard sync: square/timer restarted at each note | TT | Abs | Add (instrument option) |
| T12 | Sidechain: pulse send and ducking of the "Key" instruments | TT | Abs | Proposed as an option, outside strict parity |
| T13 | 4-bit sample slot per instrument, shareable | TT | Part (8 global slots) | With the Digidrum editor (§5) |
| T14 | Freqmod: speed, depth, range ±8 octaves, phase, shape, cycles, node editor, loop | TT | Abs (sine vibrato only) | Pitch LFO with a drawn envelope |
| T15 | YM Pad (period 0 to soften the buzzers) | TT | Abs | Buzzer option |
| T16 | Temporary slow-down (slow motion), CPU limiter | TT | Abs | Not kept (specific to the ST) |
| T17 | Commands: 1/2 glissando up/down, 3 portamento, 4 vibrato, D detune, F fixed frequency, S speed, A arpeggio, J jump, B break, V volume, L link, 7/8 freqmod, W analog, K sidechain, I instrument speed | TT | Part | Merge with X4 into a single command set, mapping table to be approved (Q8) |
| T18 | Named instrument presets | TT | Yes (patch library) | Nothing |
| T19 | Module, instrument, 4-bit sample files; SNDH export | TT | Part | SNDH export (M7); TTrak formats not read (not documented) |
| T20 | Tooltips that can be switched on and off | TT | Yes | Nothing |
