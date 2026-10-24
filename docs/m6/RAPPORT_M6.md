# M6 report: features of maxYMiser, TTrak and YM-Tracker

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

Decisions cited: [DECISIONS.md](../../DECISIONS.md) (D-135 to D-158). Plan: questions Q1 to Q10 decided by Indus on 16/10/2026 (D-135 to D-142). Deviation tables: [maxYMiser and TTrak](../m0/ECARTS_MAXYMISER_TTRAK.md), [YM-Tracker](../m0/ECARTS_YM_TRACKER.md).

## In short

- **Done**: the prototype features deferred so far (Modulation, Drum Kit and Patches windows, MIDI learn: D-143 to D-146) and the tracker features kept in §5, in a plugin (« plugin d'abord » ("plugin first")): 32 instruments played by an INS row and by Program Change (D-147, D-156), per-tick instrument sequences (D-148), replay tick at 100 and 200 Hz and Ticks timing with groove (D-149), `Xyz`, `Sxx`, `Rxx` and `Zxx` commands in a CMD row (D-150), programmable SID wave, Sinus-SID and Sync Buzzer steps, three timers per chip (D-151), tuned buzzer, fine tuning, YM Pad and presets (D-152), ST/STE machine and the STE's DMA sound on 8 tracks (D-153, D-155), Digidrum editor (D-154), register recorder (D-157) and extended Song: rests, note-off, jumps and restart point (D-158). 96 voices played by the sequencer cost **2.37 %** of one core, 2.94 % in the worst case (budget: 3 %). Visual test: 44 scenes of the prototype under their threshold, 15 STYM2149-specific scenes identical to their reference rendering.
- **Not done**: Tracker view (refused by Indus, Q4), commands that host automation can do (Q2), excluded table rows (Q9), per-track MIDI learn (left to the host, D-156), YM6 and SNDH export of the take (M7), files (M7). Details below.

## Architecture

| Layer | Additions in M6 | JUCE |
|---|---|---|
| `src/ymseq` | INS and CMD rows and instrument sequences in the track model; extended Song (Rest, Note-off, Loop, restart point) | no |
| `src/ymengine` | Instruments and their per-tick sequences, CMD commands, Ticks timing and groove, timer waves and three timers per chip, tuned buzzer, the STE's DMA sound (bank of 8 samples, one voice per DMA track), Program Change, register capture (one frame per replay tick), stepping through the extended Song | no |
| `src/plugincore` | INST, TSEQ, TIMG, DMAS, DIGS, RECD and SNGX state blocks, each written only when used (an M5 state stays identical byte for byte); Patches library in JSON; MIDI learn table; recorder take armed by the interface and rendered without allocation by the audio thread | no |
| `src/plugin` | MIDI learn (learned CCs and notes captured for the editor), take kept with the project | yes |
| `src/pluginui` | Modulation, Drum Kit, Patches, Instruments, Sequences, Timer Wave, Buzzer, DMA (STE), Digidrums and Recorder windows; INS and CMD rows of the Step Sequencer; Computer row under Voices; menu of the Song blocks | no |

## Results

Measured on 16/10/2026: Linux x86-64 (Xeon 2.1 GHz), GCC 11.5 (the one on Indus's runners) and Clang 18.1, Release.

| Check | Threshold | Measurement | State |
|---|---|---|---|
| Visual deviation, 44 scenes of the prototype at 100 % (tolerance 4/255 per component, D-086), relative to the M4 desktop (1016 × 944) | < 1 %; 2 % for `seq`, `seq_demo`, `song`, `song_demo` (D-121) | 0.598 % to 0.990 %; 1.031 % to 1.648 % for the four scenes at 2 % (table below) | OK |
| STYM2149-specific scenes (`ins_window`, `seq_ins`, `seq_ins_menu`, `seq_cmd`, `seqx_window`, `seqx_menu`, `wave_window`, `buzzer_window`, `dma_window`, `digi_window`, `dma_mixer`, `dma_track`, `rec_window`, `song_special`, `dma_menu`), reference renderings checked by eye | identical | identical (0 pixels) | OK |
| Parity with the prototype: the sequencer's voice log compared with the worklet's | identical | identical (`[proto]`: 10 test cases, 80 829 assertions) | OK |
| CPU load, 16 tracks of 6 voices played by the sequencer (`ymbench --seq --notes 6`, 48 kHz, blocks of 256) | < 3 % of one core | 2.37 % (varied scene); 2.94 % in the worst case (noise everywhere); 96 DMA voices: 2.85 % (D-153) | OK |
| No allocation in `processBlock` (400 blocks: MIDI, automation, Host sync then Free run, extended Song, recorder takes armed and re-armed) | 0 | 0 | OK |
| Interface tests (`stym_ui`) | all pass | 108 test cases, 5 964 assertions | OK |
| Unit tests (`stym_unit`), including 56 `[seq]` cases and 14 `[ymseq]` cases | all pass | 282 test cases, 375 047 assertions | OK |
| JUCE processor and editor tests (`stym_plugin_tests`) | all pass | 21 test cases, 2 389 assertions (with and without a display) | OK |
| Compiler warnings in the project's code | 0 | 0 (GCC 11 and Clang 18) | OK |
| CI on Indus's runners: Linux and Windows, pluginval (VST3, LV2), clap-validator, RealtimeSanitizer (S11) | pass | at the PR | pending |

| Scene | Deviation | Scene | Deviation | Scene | Deviation |
|---|---|---|---|---|---|
| default | 0.713 % | menu_source | 0.715 % | curve | 0.754 % |
| sid | 0.705 % | alert_chip | 0.825 % | curve_custom | 0.866 % |
| sync | 0.696 % | seq ² | 1.559 % | kbd | 0.906 % |
| chipdrum | 0.728 % | seq_demo ² | 1.648 % | kbd_held | 0.898 % |
| digidrum | 0.663 % ¹ | song ² | 1.031 % | kbd_azerty | 0.918 % |
| envelope | 0.771 % | song_demo ² | 1.037 % | kbd_high | 0.950 % |
| smooth | 0.754 % | mixer | 0.751 % | orn | 0.650 % |
| mode3 | 0.694 % | mixer_3ch | 0.733 % | orn_on | 0.890 % |
| dual | 0.793 % | mixer_scope | 0.720 % | kit_off | 0.818 % |
| tabs | 0.713 % | arp | 0.733 % | kit | 0.690 % |
| menu_file | 0.690 % ³ | arp_on | 0.706 % | kit_clap | 0.706 % |
| menu_edit | 0.691 % ⁴ | arp_table | 0.893 % | lib | 0.877 % |
| menu_options | 0.990 % | arp_midi | 0.598 % | lib_list | 0.754 % |
| menu_fuji | 0.712 % | | | learn | 0.713 % |
| menu_generator | 0.715 % | | | learn_bound | 0.713 % |
| menu_shape | 0.868 % | | | | |

¹ Excluding the waveform and the frame's note (D-078, D-086). ² 2 % threshold (D-121). ³ Recorder entry below the exports; the following rows are compared one row lower (D-157). ⁴ Instruments and Sequences entries below the prototype's, ignored (D-147, D-148). Every scene compares the rows of the SYSTEM frame below the Computer row with their place in the capture (D-155).

Reproduce: `gemshot <output> --ref tests/data/visual`; `ctest -R ui` compares with the versioned captures. CPU load: `ymbench --seq --notes 6 [--gen chip|sid|buzzer|digidrum|chipdrum] [--noise]`.

## CPU load

| Scene (96 voices, 48 kHz) | M5 | M6 |
|---|---|---|
| Varied (SID, Sync Buzzer, Digidrum, Chipdrum, modulated chip channel; 93 voices on average) | 2.64 % | 2.37 % |
| Noise on every track | 2.90 % | 2.94 % |
| 96 Sync Buzzer | 2.90 % | 2.65 % |
| 96 SID | 2.41 % | 2.28 % |
| 96 chip channels | 2.05 % | 2.00 % |
| 96 voices of the STE's DMA | | 2.85 % (D-153) |
| Idle engine | 0.54 % | 0.58 % |

The M6 features only cost on the tracks that use them (sequences, timer waves, DMA). The margin stays thin in the worst case; the cost of the open editor is not counted (M8).

## Behaviour to know

- **Instruments**: INS cell `00` keeps the track's sound (the prototype's behaviour); `01` to `20` play the instrument on the note. Program Change n selects instrument n on the channel's track, or on the focused track in Omni (D-156).
- **Commands**: four only (Q2). The rest goes through host automation, the existing rows or the Song (D-150).
- **Ticks timing**: in Host sync, the steps follow the host; Ticks only applies in Free run and MIDI clock (D-149).
- **ST/STE machine**: Computer row under Voices. The DMA sound only exists with the STE: tracks 5 to 12 in 3ch, 9 to 16 in 2x3ch, DMA sound type in 16 channels (D-153).
- **Recorder**: File > Recorder. Rec arms a take on the chosen chip; it starts with the host's playback (in Standalone, the sequencer's) and stops with it. One take per arming. The last take is kept with the project; nothing exports it before M7. In 16 channels there are no registers: the take waits for the 3ch or 2x3ch mode (D-157).
- **Song**: right click on a block for Insert rest, Insert note-off, Insert loop, Length..., Loop back... and Restart here. A jump replays the blocks before it, back to the previous jump, Repeat times. The restart point is used when Loop is on (D-158).
- **Patches library**: one file shared by all projects, next to the preferences (D-141, D-145).

## Not done in M6

| Item | Where | Why |
|---|---|---|
| Tracker view | never | Refused by Indus (Q4): « essentiellement un plugin » ("essentially a plugin") |
| Commands `E`, `Y`, `M`, `N`, `D`, `H`, `P`, `G`, `A`, `L`, `V`, `F`, `W`, `I`, `B`, `J` | never | Done by host automation, the existing rows or the Song (Q2) |
| Per-track MIDI learn | host | Hosts already do it (D-156) |
| Sidechain, slow-down and CPU limiter, YM-Tracker configuration, jam mode | never | Excluded (Q9) |
| Phase, Duty, Noise AND and OR of YM-Tracker | to confirm | No audible effect in YM-Tracker v0.1.0 |
| YM6 and SNDH export of the take, XM import, files (presets, patterns, Song, kits, samples) | M7 (`ymio`) | Formats of §8 |
| "Next free pattern" (maxYMiser's `n`) | not kept | The prototype's Add takes the current pattern (D-158) |
| CPU cost of the interface, compatibility with DAWs | M8 | Not measured |

**TO CONFIRM** (public values taken over, tests planned in DECISIONS.md): timer C reserved for the system (D-138); rate of the STE's DMA, its filter and its level against the YM (D-153); volumes of the Sinus-SID wave (D-151); accumulation of YM-Tracker's pitch steps and maxYMiser's sequence speed (D-148); direction of maxYMiser's Sync Buzzer steps (D-151); hardware origin of the YM Pad (D-152).

## Indus's decisions

Decided: Q1 to Q10 of the plan (D-135 to D-142), the ST/STE machine and the Computer row « Sous Voices » ("Under Voices") (D-153, D-155), Program Change and the recorder (Q1, Q7). Accepted by Indus on 16/10/2026 at the PR review: D-143 to D-158. D-113 (sequencer parameters, M5) is accepted.
