# Parity with the prototype: state at the end of M8 (B4)

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).

This is the state of each of the 466 rows of the parity matrix of M0 ([docs/m0/PARITE.md](../m0/PARITE.md)), which
lists every control and function of `YM2149_Prototype.html` with its lines in the prototype. Read the full
description of a row there; this table gives its short name, its state in STYM2149 0.9.0, and what proves it.

The audit read the prototype again row by row against the code, the tests and the decisions of DECISIONS.md, in
M8 (milestone plan, B4). Rows it found missing or untested were fixed or tested in M8 before this table was closed.

## States

| State | Meaning | Rows |
|---|---|---|
| Done | Works as in the prototype; the evidence names the tests (Catch2 test cases, by file and name) or the visual scenes that check it | 357 |
| Deviation | Works differently on purpose; the evidence names the decision (D-nnn) and, when there is one, the test | 107 |
| Not kept | Not reproduced; the evidence names the decision | 2 |

Every row is in one of these three states: no row is missing or untested, and every deviation names its decision
(all decided or accepted by Indus, see [RAPPORT_M8.md](RAPPORT_M8.md)). What only a
person can check in a DAW (host transport, MIDI devices, the Standalone app) is in the manual protocol
[TESTS_DAW.md](TESTS_DAW.md).

Test files are under `tests/`; visual scenes are the `tests/data/visual/*.png` captures compared by
`gemui/test_visual.cpp` (under 1 % of the pixels for a prototype scene, none for a STYM2149 scene). Test names in
French will be translated with the rest of the code comments after M8 (D-178).

## Rows

| Id | Feature | Status | Evidence | Note |
|---|---|---|---|---|
| P001 | Global UI zoom root | Done | `plugin/test_editor.cpp` "Editor: GEM desktop enlarged with nearest-neighbour scaling (100 and 200 %)", "Editor: zoom chosen in the menu or by resizing, kept in the preferences"; D-068 | Zoom 50-200 % via Options > Zoom and host resize (D-068) |
| P002 | Message box (OK = Enter) | Done | `gemui/test_gemui.cpp` "Alert: shrink-to-fit width, limited to half of the desktop", "Alert: choice rows below the text, one option picked per row" (Enter = default button); scene `alert_chip` |  |
| P003 | About / Information box | Deviation | D-031, D-060, D-081, D-186, D-235, D-236; STYM scene `info`; `gemui/test_confirm.cpp` "Information box: the STYM2149 icon above the text, OK framed as the default button, Enter closes it (D-235, D-236)" | Prototype text (former author credit, alpha, licence) replaced by name, author, version, AGPLv3, credits; the application icon above the text, OK framed as the default button |
| P004 | MIDI learn badge | Done | scenes `learn`, `learn_bound`; `gemui/test_midi_learn.cpp` "MIDI learn: Options menu, banner, a click arms the control without acting"; D-146 | Badge at (8, 908) per D-146 |
| P005 | Text prompt box | Done | `gemui/test_confirm.cpp` "Prompt box: Cancel then OK; Tab goes field, Cancel, OK; Enter presses the focused button (P365)", "Prompts of the editor follow gemPrompt: Cancel then OK, none framed, Enter is OK (P365)"; `gemui/test_gemui.cpp` "Alert: shrink-to-fit width, limited to half of the desktop" (section Boîte de saisie); D-191 |  |
| P006 | Confirm box, Cancel default | Done | `gemui/test_confirm.cpp` "Confirm box: Enter and Esc cancel, even with the focus on OK; a click on OK confirms (P417)", "Demo pattern and Quit confirms: Cancel by default, only OK goes on (P417)"; `gemui/test_recorder_window.cpp` "Recorder: Clear asks for confirmation; duration; window kept by the session"; D-191 |  |
| P007 | Boot screen Run program | Not kept | D-067 (replaces D-020) | No boot screen, in the plugin or in the Standalone app; the editor opens on the main window |
| P008 | GEM main window | Deviation | D-067; scene `default` | Fixed (not draggable); close = Quit confirm in Standalone, no effect in host; fullbox inert |
| P009 | Menu bar (Fuji, File, Edit, Options) | Done | scenes `menu_fuji`, `menu_file`, `menu_edit`, `menu_options` | Extra STYM entries masked per D-080, D-147, D-148, D-185 |
| P010 | CHIP conformity indicator | Done | scenes `alert_chip`, `seq_demo` (CHIP: 4); D-124 | No unit test of the individual chipIssues rules, only visual scenes |
| P011 | MIDI status indicator | Deviation | D-074 | Host shows "MIDI: host"; Standalone lists inputs; no automated test of the text and no TESTS_DAW step for it |
| P012 | Channel tabs (16) | Done | scenes `tabs`, `mode3`, `dual` |  |
| P013 | Source Tone/Noise/Tone+Noise | Done | scene `menu_source` (a STYM2149 scene since the envelope entries, D-246); `gemui/test_main_controls.cpp` "Source menu: the envelope entries set Mode too, one undo step per entry; Mode follows (fifth DAW test, item 48, D-246)"; `unit/test_plugincore.cpp` "Mapping: parameter defaults = defaults of the engine and of the prototype" |  |
| P014 | Voicing Poly/Mono legato 45 ms | Done | `unit/test_proto_engine.cpp` "Prototype: legato of a monophonic track (45 ms slide)"; scene `default` |  |
| P015 | Noise period 1-31 (R6) | Done | `unit/test_engine.cpp` "3-channel: R6 follows the last noise switched on, then returns to the remaining noise"; `unit/test_plugincore.cpp` "Parameters: bounds, step and normalised position", "Mapping: parameter defaults = defaults of the engine and of the prototype" |  |
| P016 | 4-bit volume 0-15 | Done | `unit/test_engine.cpp` "3-channel: the velocity and the track level go through the 4-bit volume"; `unit/test_plugincore.cpp` "Mapping: parameter defaults = defaults of the engine and of the prototype" |  |
| P017 | Volume mode 4-bit/envelope | Done | scenes `envelope`, `menu_shape`; `unit/test_plugincore.cpp` "Mapping: parameter defaults = defaults of the engine and of the prototype" |  |
| P018 | Channel level 0-100 | Done | `unit/test_plugincore.cpp` "Mapping: parameter defaults = defaults of the engine and of the prototype"; `gemui/test_mixer_window.cpp` "Mixer: fader and panning linked to the track's level and pan, one step per drag" |  |
| P019 | Env freq slider and Hz | Deviation | D-010, D-073; `unit/test_plugincore.cpp` "Mapping: CC 74 like the Env freq slider of the prototype"; `unit/test_frequency.cpp` "S6 envelope: clk / (256·EP) as a sawtooth, clk / (512·EP) as a triangle" | Geometric slider over the real 16-bit EP range; Hz = clk/(256·EP) (prototype was 32x too slow); the value on the row of EP =, the slider over its whole row (D-296) |
| P020 | EP readout (dec + hex) | Deviation | D-073, D-021; scenes `default`, `envelope` | Shows chip EP "12800 ($3200)" instead of 400; editable |
| P021 | R13 shape (16 shapes) | Done | scene `menu_shape`; `unit/test_envelope.cpp` "S8 envelope shapes: 16 values of R13 × 4 cycles of 32 steps" |  |
| P022 | Shared envelope indicator | Done | scenes `default`, `mode3` | Text "(per channel)" / "(shared)" |
| P023 | Vibrato depth 0-200 cents | Done | `unit/test_proto_engine.cpp` "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)"; `unit/test_plugincore.cpp` "Parameters: bounds, step and normalised position"; scene `smooth` |  |
| P024 | Vibrato rate 1-20 Hz at VBL | Done | `unit/test_proto_engine.cpp` "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)"; `unit/test_plugincore.cpp` "Parameters: bounds, step and normalised position" |  |
| P025 | ADSR attack 0-1000 ms | Deviation | `unit/test_engine.cpp` "16-channel: smooth ADSR, stages and sustain level"; scenes `smooth`, `envelope` | 0-2000 ms in both modes, 100 ticks at 50 Hz in Chip mode (D-428; 1280 ms by D-427, capped at 1000 ms by D-085 before) |
| P026 | ADSR decay 0-1000 ms | Deviation | `unit/test_engine.cpp` "16-channel: smooth ADSR, stages and sustain level"; scenes `smooth`, `envelope` | 0-2000 ms in both modes, 100 ticks at 50 Hz in Chip mode (D-428; 1280 ms by D-427, capped at 1000 ms by D-085 before) |
| P027 | ADSR sustain 0-100 % | Done | `unit/test_engine.cpp` "16-channel: smooth ADSR, stages and sustain level"; scenes `smooth`, `envelope` |  |
| P028 | ADSR release 0-2000 ms | Done | `unit/test_engine.cpp` "16-channel: smooth ADSR, stages and sustain level"; scenes `smooth`, `envelope` | 0-100 ticks in Chip mode, 2000 ms at 50 Hz, like Attack and Decay (D-428; 64 ticks in the prototype) |
| P029 | ADSR mode Smooth/Chip, 3ch forces Chip | Done | `unit/test_proto_engine.cpp` "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)"; `unit/test_engine.cpp` "16-channel: smooth ADSR, stages and sustain level"; scene `envelope` | D-085 (tick sliders, 1000 ms cap) |
| P030 | Transparent ADSR 0/0/100/0 | Done | `unit/test_engine.cpp` "16-channel engine: one voice alone = channel A of the chip, up to the gain" (default ADSR) | Implicit coverage through default patch (adsrOn false) |
| P031 | ADSR curve with live follow | Deviation | D-088, D-075; scenes `smooth`, `envelope`; `unit/test_plugincore.cpp` "Engine status: LEDs of audible tracks, attacked note, followed envelope, oscilloscope" | Telemetry at 30 Hz instead of 20 Hz; aliased drawing |
| P032 | Generator type (5 types) | Done | scenes `menu_generator`, `sid`, `sync`, `digidrum`, `chipdrum` |  |
| P033 | SID timer ratio x0.25-x8 | Done | `gemui/test_digidrum_window.cpp` "Main window: the SID timer ratio explains the ratio, not a Digidrum rate (P033, D-029)"; `plugin/test_tooltips.cpp` "Tooltips: every line is translated, with characters of the GEM font (D-185)"; `unit/test_engine.cpp` "3-channel: SID effect with the MFP timer or continuous"; D-029 | The tooltip says what the ratio does (D-029), with its own catalogue line in the six languages |
| P034 | SID minimum volume 0-15 | Done | `unit/test_engine.cpp` "3-channel: SID effect with the MFP timer or continuous" (vmin 3); scene `sid` |  |
| P035 | SID Voice timer toggling | Deviation | D-044, D-052; `unit/test_engine.cpp` "3-channel: SID effect with the MFP timer or continuous"; `unit/test_proto_engine.cpp` "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)" | Timer quantised as MFP by default (or continuous) |
| P036 | Sync Buzzer envelope restart | Deviation | D-052, D-021; `unit/test_engine.cpp` "3-channel: R11-R13 to the Sync Buzzer first, then back to the envelope voice"; `unit/test_proto_engine.cpp` "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)" | EP threshold scaled x32 (EP>3200 gives 32); MFP timer |
| P037 | Chipdrum panel, open kit | Done | `gemui/test_kit_window.cpp` "Drum Kit: button, Ctrl+K and Edit menu open the window; boxes of the prototype"; scene `chipdrum` |  |
| P038 | Drum Kit window, off message | Done | `gemui/test_kit_window.cpp` "Drum Kit: outside Chipdrum, a note and the content inert"; scene `kit_off`; D-144 |  |
| P039 | Kit instrument selector (8) | Done | `gemui/test_kit_window.cpp` "Drum Kit: instrument, settings by slider and typed input, kit name, one step per gesture"; scene `kit` |  |
| P040 | Kit sliders (KITSPEC ranges) | Done | `gemui/test_kit_window.cpp` "Drum Kit: instrument, settings by slider and typed input, kit name, one step per gesture"; `unit/test_kit.cpp` "Chipdrum kit: bounds"; scene `kit_clap` |  |
| P041 | Kit default values | Done | `unit/test_kit.cpp` "Chipdrum kit: identical to the prototype, 13 banks" [kit] |  |
| P042 | Kit audition | Done | `unit/test_kit.cpp` "Chipdrum kit: audition of an instrument (Audition, drumprev)"; `gemui/test_kit_window.cpp` "Drum Kit: Audition, Reset of the instrument, banks of the Kits menu"; `unit/test_plugincore.cpp` "Driver: Drum Kit Audition, audition of an instrument outside the voices (UiCommand::DrumPreview)" |  |
| P043 | Kit instrument reset | Done | `gemui/test_kit_window.cpp` "Drum Kit: Audition, Reset of the instrument, banks of the Kits menu" |  |
| P044 | Kit banks (13) | Done | `unit/test_kit.cpp` "Chipdrum kit: identical to the prototype, 13 banks"; scene `kit_clap` |  |
| P045 | Kit name display | Done | `gemui/test_kit_window.cpp` "Drum Kit: kit name, the first identical bank, otherwise Custom" |  |
| P046 | Kit import (.json) | Done | `gemui/test_file_menu.cpp` "File: Load of a pattern, a Song and a kit, one undo step each"; `unit/test_project_files.cpp` "Kit: file read back, merge per instrument, old format, refusal"; D-168 | Load adds one undo step (D-168) |
| P047 | Live kit rebuild | Done | `gemui/test_kit_window.cpp` "Drum Kit: instrument, settings by slider and typed input, kit name, one step per gesture"; `unit/test_kit.cpp` "Chipdrum kit: identical to the prototype, 13 banks" |  |
| P048 | 16ch drum sample synthesis | Done | `unit/test_kit.cpp` "Chipdrum kit: identical to the prototype, 13 banks" (kit.txt samples) [proto] |  |
| P049 | 3ch drum register frames | Done | `unit/test_kit.cpp` "Chipdrum kit: identical to the prototype, 13 banks" (kit.txt frames), "Chipdrum kit: bounds" |  |
| P050 | Chipdrum pitch (OCT/TON) | Deviation | D-098; `unit/test_proto_seq.cpp` "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" [proto] | Hit note = (track oct + 1) x 12 + row; OCT read differently from prototype |
| P051 | Chipdrum TIE no retrigger | Done | `unit/test_proto_seq.cpp` "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" [proto] |  |
| P052 | Digidrum slots (8) | Done | scene `digidrum`; `unit/test_plugincore.cpp` "Driver: Digidrum definition kept from 16 to 3 channels and back (D-063)" |  |
| P053 | Load sample into slot | Done | `unit/test_plugincore.cpp` "Digidrum: audio file brought to 7800 Hz on 4 bits (loading of the prototype)"; `gemui/test_digidrum_window.cpp` "Digidrums: loading, listening, Clear and its undo step"; D-078 | Loaded by clicking the wave (D-078) |
| P054 | Digidrum ST channel (3ch) | Done | `unit/test_engine.cpp` "3-channel: Digidrum track steals the designated channel, plays the sample on the timer"; scene `mode3` |  |
| P055 | Digidrum waveform display | Deviation | D-078, D-086; scene `digidrum` (drumwave masked) | Empty slot text rewritten; wave area excluded from the visual diff |
| P056 | Digidrum playback (rate, slot) | Deviation | D-098, D-154, D-052; `unit/test_engine.cpp` "3-channel: Digidrum track steals the designated channel, plays the sample on the timer"; `unit/test_digidrum_editor.cpp` "Digidrum: rate, root note and fine tune of the slot" | Empty slot silent; per-slot rate and base note editor (D-154) |
| P057 | Wrong Digidrum hint text | Deviation | D-029, D-078; scene `digidrum` (digiHint masked) | Hint rewritten ("4-bit samples at the slot rate...") |
| P058 | 3ch digidrum replay priority | Done | `unit/test_proto_seq.cpp` "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (3ch cases) [proto] |  |
| P059 | Arpeggiator window | Done | `gemui/test_arp_window.cpp` "Arpeggiator: button, Ctrl+B and Edit menu open the window; not applicable on percussion"; scene `arp`; D-129 |  |
| P060 | Arp on/off, off for drums | Done | `gemui/test_arp_window.cpp` "Arpeggiator: button, Ctrl+B and Edit menu open the window; not applicable on percussion", "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; scene `arp_on` |  |
| P061 | Arp mode (6 modes) | Done | `unit/test_proto_engine.cpp` "Prototype: arpeggio played on the keyboard, 6 modes"; `gemui/test_arp_window.cpp` "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division" |  |
| P062 | Arp rate 4-200 Hz | Done | `gemui/test_arp_window.cpp` "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; `unit/test_plugincore.cpp` "Parameters: bounds, step and normalised position" |  |
| P063 | Arp Track (glide follow) | Done | `gemui/test_arp_window.cpp` "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; `unit/test_proto_seq.cpp` (arp track) [proto] |  |
| P064 | Arp Land | Done | `gemui/test_arp_window.cpp` "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; `unit/test_proto_seq.cpp` (arp land) [proto] |  |
| P065 | Arp MIDI clock division | Done | `unit/test_engine.cpp` "Arpeggio: MIDI clock, one step every `division` pulses"; scene `arp_midi` |  |
| P066 | Arp octaves 1-3 | Done | `gemui/test_arp_window.cpp` "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division" |  |
| P067 | Arp table steps 1-16 | Done | `gemui/test_arp_window.cpp` "Arpeggiator: table (grid, number of steps, chords, keyboard, playing step)" |  |
| P068 | Arp chord fill | Done | `gemui/test_arp_window.cpp` "Arpeggiator: table (grid, number of steps, chords, keyboard, playing step)"; `unit/test_proto_engine.cpp` "Prototype: 39 chords" |  |
| P069 | Arp table grid | Done | scene `arp_table`; `gemui/test_arp_window.cpp` "Arpeggiator: table (grid, number of steps, chords, keyboard, playing step)" |  |
| P070 | Live arp (release, ADSR, velocity) | Done | `unit/test_engine.cpp` "Arpeggio: the last note keeps cycling during the release"; `unit/test_proto_engine.cpp` "Prototype: arpeggio played on the keyboard, 6 modes" |  |
| P071 | Sequencer track arp | Done | `unit/test_proto_seq.cpp` "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (arp tracks) [proto]; D-097 | D-097 fixes a prototype bug only |
| P072 | Modulation window | Done | `gemui/test_mod_window.cpp` "Modulation: button, Ctrl+R and Edit menu open the window; not applicable on percussion"; scene `orn`; D-143 |  |
| P073 | Modulation on/off, off for drums | Done | `gemui/test_mod_window.cpp` "Modulation: button, Ctrl+R and Edit menu open the window; not applicable on percussion", "Modulation: on/off, length, Loop / Hold, rate, Clear"; scene `orn_on` |  |
| P074 | Modulation length 1-64 | Done | `gemui/test_mod_window.cpp` "Modulation: on/off, length, Loop / Hold, rate, Clear" |  |
| P075 | Modulation Loop/Hold | Done | `gemui/test_mod_window.cpp` "Modulation: on/off, length, Loop / Hold, rate, Clear"; scene `orn_on` |  |
| P076 | Modulation rate 1-4 frames | Done | `gemui/test_mod_window.cpp` "Modulation: on/off, length, Loop / Hold, rate, Clear" |  |
| P077 | Modulation Clear | Done | `gemui/test_mod_window.cpp` "Modulation: on/off, length, Loop / Hold, rate, Clear" |  |
| P078 | Modulation pitch grid | Done | `gemui/test_mod_window.cpp` "Modulation: drawing pitch, noise and volume, one undo step per stroke"; scene `orn_on` |  |
| P079 | Modulation NSE row | Done | `gemui/test_mod_window.cpp` "Modulation: drawing pitch, noise and volume, one undo step per stroke"; scene `orn_on` |  |
| P080 | Modulation VOL row, right-click | Done | `gemui/test_mod_window.cpp` "Modulation: right click on a VOL cell, setting by slider" |  |
| P081 | Modulation engine per VBL | Done | `unit/test_proto_engine.cpp` "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)" [proto] |  |
| P082 | Step sequencer window | Done | scenes `seq`, `seq_demo` | Ctrl+E opening has no automated test |
| P083 | Sequencer toolbar | Done | scenes `seq`, `seq_demo` |  |
| P084 | Seq Play / Rec | Deviation | D-110, D-114; `plugin/test_processor.cpp` "Processor: Host sync on the playhead, commands in Free run"; TESTS_DAW #7 | Host sync / Free run transport |
| P085 | Step input | Done | `gemui/test_kbd_window.cpp` "Host notes: keys lit, step recording, nothing sent back to the engine"; `unit/test_ymseq_edit.cpp` (rec op) [proto]; D-095 |  |
| P086 | Clear track | Done | `unit/test_ymseq_edit.cpp` "Prototype: sequencer track edits" (clear op) [proto]; D-093 | D-093 also clears TON row |
| P087 | Euclid generator | Done | `unit/test_ymseq.cpp` "ymseq: Euclidean generator"; `unit/test_ymseq_edit.cpp` "Prototype: Bjorklund patterns" |  |
| P088 | Pattern rotation | Done | `unit/test_ymseq_edit.cpp` "Prototype: sequencer track edits" (rot op) [proto] |  |
| P089 | Transpose semitone/octave | Done | `unit/test_ymseq_edit.cpp` "Prototype: sequencer track edits" (tr op) [proto]; D-092 | D-092 fixes octave 0 and chord voices |
| P090 | Follow playhead | Done | scene `seq` (button) | Follow and 64-column paging behaviour has no automated test |
| P091 | Track length (polymeter) | Done | `unit/test_proto_seq.cpp` "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (plen) [proto]; scene `seq` | Len button UI has no automated test |
| P092 | Song button | Done | `gemui/test_song_window.cpp` "Song: opening the window switches Song mode on, closing it switches it off" |  |
| P093 | Steps 1-256 | Done | scene `seq`; `unit/test_engine_seq.cpp` "Sequencer: length changed during playback, the current step is kept" |  |
| P094 | Tempo 60-240 BPM | Done | `unit/test_plugincore.cpp` "Driver: CC 83 sets the free tempo like the BPM slider of the prototype", "Parameters: bounds, step and normalised position"; D-113 | "ext xx.xx" display under MIDI clock has no automated test |
| P095 | Gate 5-95 % | Deviation | D-113, D-134; `gemui/test_seq_window.cpp` "Step Sequencer: All button of the global gate, on the row of the Gate slider (D-134)"; `unit/test_plugincore.cpp` "Driver: Gate All on, off or global gate changed: all tracks take it up (D-134)" | Adds global Gate + Gate All |
| P096 | Swing 50-75 % | Done | `unit/test_engine_seq.cpp` "Sequencer: swing and triplet resolutions"; `unit/test_proto_seq.cpp` [proto] |  |
| P097 | Scale root | Done | `unit/test_ymseq_edit.cpp` (root context) [proto]; `unit/test_proto_seq.cpp` [proto] |  |
| P098 | Scale select (mask) | Done | `unit/test_ymseq.cpp` "ymseq: scales, figures, bars and names"; `unit/test_proto_engine.cpp` "Prototype: slide quantisation (Chromatic, Scale-locked)" |  |
| P099 | Scale snap | Done | `unit/test_ymseq_edit.cpp` (snap context) [proto]; D-090 | No snap on drum tracks (D-090) |
| P100 | Time signature | Done | `unit/test_ymseq.cpp` "ymseq: scales, figures, bars and names"; `unit/test_engine_host.cpp` "Metronome: one beep per beat, higher on the first beat" |  |
| P101 | Step resolution | Deviation | D-099, D-025; `unit/test_engine_seq.cpp` "Sequencer: swing and triplet resolutions"; `gemui/test_seq_ticks.cpp` "Ticks: Resolution menu, Ticks timing, typed speed and groove" | Step length follows resolution (prototype always 1/16) |
| P102 | Note figure length | Done | `unit/test_ymseq_edit.cpp` (fig op) [proto]; `unit/test_ymseq.cpp` "ymseq: scales, figures, bars and names" |  |
| P103 | Chord on note entry | Done | `unit/test_ymseq_edit.cpp` (chord context) [proto] |  |
| P104 | Sequencer voicing Poly/Mono | Done | `unit/test_proto_seq.cpp` (mono tracks) [proto]; scene `seq` |  |
| P105 | Ghost other channels | Done | scene `seq` (button) | Ghost rendering has no automated test (default off in all scenes) |
| P106 | Overview strip | Done | scene `seq` (button) | Overview map and click/drag have no automated test (default off) |
| P107 | Piano-roll grid | Deviation | D-120, D-119; scenes `seq`, `seq_demo`; `unit/test_ymseq_edit.cpp` [proto] | Gesture and text deviations |
| P108 | Vertical scroll 23-108 | Done | `gemui/test_windows.cpp` "Vertical slider: minimum at the bottom, checkerboard rotated with the track"; scene `seq` |  |
| P109 | Horizontal scroll | Done | scene `seq` | Scroll row and "1-64" readout behaviour have no automated test |
| P110 | Transport bar | Deviation | D-110, D-114, D-126; `unit/test_engine_host.cpp` "Host sync" cases; TESTS_DAW #7 | Host sync / Free run |
| P111 | Metronome | Done | `unit/test_engine_host.cpp` "Metronome: one beep per beat, higher on the first beat"; `gemui/test_kbd_window.cpp` "Computer Keyboard: keys according to the layout (kbdAction)"; D-112 |  |
| P112 | Pattern banks and slots | Done | `unit/test_engine_seq.cpp` "Sequencer: a queued pattern applies at the next step 0"; `unit/test_ymseq.cpp` "ymseq: pattern bank, wait during playback, cut and transposition"; D-100 | Digit / Shift+digit shortcuts have no automated test |
| P113 | Sequencer hint | Done | scenes `seq`, `seq_demo`; D-119 |  |
| P114 | Step engine (seqTick) | Deviation | D-099, D-106; `unit/test_proto_seq.cpp` "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" [proto] | Resolution-aware step length; dual-chip lanes counted |
| P115 | Internal clock | Done | `unit/test_proto_seq.cpp` [proto]; `unit/test_engine_seq.cpp` "Sequencer: countdowns by deadline, same log and same sound in blocks of 1 and of 512 samples" |  |
| P116 | Polymeter per lane | Done | `unit/test_proto_seq.cpp` "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (plen) [proto] |  |
| P117 | Chord voices 2-6 | Deviation | D-102; `unit/test_proto_seq.cpp` [proto]; `unit/test_engine_instruments.cpp` "Instruments: chord voices and RTG hits take the instrument of the step" | Each chord voice keeps its own duration |
| P118 | Step note and octave code (NOTE/OCT) | Done | tests/unit/test_ymseq.cpp "ymseq: absolute notes and octave 0"; [proto][ymseq]; [proto][seq]; scene seq_demo | Octave code handling per D-092, D-095 |
| P119 | Tie and note duration (TIE) | Done | [proto][seq] tests/unit/test_proto_seq.cpp; [proto][ymseq]; test_ymseq.cpp "ymseq: scales, figures, bars and names" |  |
| P120 | Slide to next note (SLD) | Done | [proto][seq]; tests/unit/test_proto_engine.cpp slide cases | Slide lands on exact pitch (D-105) |
| P121 | Slide curve per step (CRV) | Done | test_proto_engine.cpp "Prototype: 13 slide curves"; [proto][seq] (H op); tests/gemui/test_curve_window.cpp | Random walk seeded by hash (D-104); CRV for sequencer slides only (D-109) |
| P122 | Step played through track arp (ARP) | Done | [proto][seq] tests/unit/test_proto_seq.cpp | Suspended arp fix D-097 |
| P123 | Step volume and accent (VOL) | Done | [proto][seq] |  |
| P124 | Step Tone/Noise mix (MIX) | Done | [proto][seq] (M op) |  |
| P125 | Step generator switch (FX) | Done | [proto][seq] (F op) |  |
| P126 | Step gate percent (GAT) | Done | [proto][seq] | Step GAT keeps priority over global gate (D-134) |
| P127 | Step pan (PAN) | Done | [proto][seq] (P op) |  |
| P128 | Step retrigger (RTG) | Deviation | D-101; [proto][seq] | Level x0.92 kept with step FX/MIX |
| P129 | Step conditions (CND) | Done | [proto][seq] | 25/50/75 % drawn from deterministic hash (D-104) |
| P130 | Step sample cut (CUT) | Done | [proto][seq] (K op) | Second chip tracks counted too (D-106) |
| P131 | Drum slot row (SLT) | Done | [proto][ymseq]; tests/unit/test_plugincore.cpp "State: sequencer, cells clamped, unknown rows ignored"; scene seq | Stored and edited; playback ignores it as in prototype (D-103) |
| P132 | Drum tuning in semitones (TON) | Done | [proto][seq] (T op transposition z) | Applies to Digidrum too (D-098) |
| P133 | Song window | Done | tests/gemui/test_song_window.cpp; scenes song, song_demo | D-126 |
| P134 | Song Add button | Done | test_song_window.cpp "Song: Add, Shift+Add and Add to song in a pattern's menu"; test_ymseq.cpp "ymseq: Song, additions, repeats and tempo" |  |
| P135 | Song bank and pattern picker | Done | scenes song, song_demo; test_ymseq.cpp bank case | Bank/slot clicks not covered by a UI test |
| P136 | Song list click and context menu | Done | test_song_window.cpp "Song: click, Shift+click, block menu and drag and drop" |  |
| P137 | Song window keys | Done | test_song_window.cpp "Song: keyboard control when the window is in front"; TESTS_DAW #13 | =/- read by character, Ins added (D-126) |
| P138 | Pattern chaining and stop | Deviation | D-108; tests/unit/test_engine_seq.cpp "Sequencer: without loop, the pattern stops before replaying its first step", "Sequencer: Song without loop, stop after the last entry" | Song on audio thread, stops one step earlier |
| P139 | Slide Curve window | Done | tests/gemui/test_curve_window.cpp; scenes curve, curve_custom | D-130 |
| P140 | Curve Linear / Smooth / Invert | Done | test_curve_window.cpp "Slide Curve: Linear, Smooth, Invert and warning" |  |
| P141 | Computer keyboard window | Done | tests/gemui/test_kbd_window.cpp; scenes kbd, kbd_held, kbd_azerty, kbd_high; TESTS_DAW #13 | Layout by character (D-131) |
| P142 | Computer keyboard octave (Z/X) | Done | test_kbd_window.cpp "Computer Keyboard: octave by the buttons, Z and X, the keypad and the drawing"; scene kbd | Plugin and capture show Oct 2; PARITE says Oct 3 |
| P143 | Held notes Keep/Follow | Done | test_kbd_window.cpp "Computer Keyboard: notes held in Keep and in Follow"; scene kbd_held |  |
| P144 | Played range readout | Done | test_kbd_window.cpp (asserts C2 to F3 and out-of-range text); scene kbd_high |  |
| P145 | On-screen piano | Done | tests/plugin/test_editor.cpp "Editor: click on the on-screen keyboard, note played then released by the processor"; scene default |  |
| P146 | Drum pads | Done | scenes chipdrum, digidrum | No UI click test; duplicate id was a prototype defect |
| P147 | Latch | Done | `gemui/test_main_tracks.cpp` "Latch: keys stay held until pressed again, one mouse note at a time, released when Latch goes off (P147)" |  |
| P148 | Layer (play all active tracks) | Done | test_plugincore.cpp "Driver: Program Change (Omni, Layer, per channel), replayed on a new engine"; test_engine.cpp 3ch Digidrum case (layer); `gemui/test_main_controls.cpp` "Layer: the button switches MIDI routing between Omni and Layer and silences the notes (P434, P148, D-061)" | Button toggles MIDI Routing Omni/Layer (D-061) |
| P149 | On-screen keyboard octave | Done | test_kbd_window.cpp octave case (octUp/octDown); scene default | Capture shows Oct 2-5; PARITE says Oct 4 |
| P150 | Global shortcuts | Done | scene default (help line, D-122); test_arp_window.cpp (Ctrl+Z/Y, arrows); test_mixer_window.cpp (Esc); TESTS_DAW #13; `gemui/test_seq_keys.cpp` "Digit keys: 1-8 pick a pattern of the bank shown, Shift+1-4 a bank, never the numeric keypad (P451)" | Space and M not covered by tests |
| P151 | Window shortcuts | Done | test_mixer/arp/mod/patches/kit_window.cpp (Ctrl+M/B/R/P/K); TESTS_DAW #13 | Ctrl+E, Ctrl+G, Ctrl+C not covered by tests |
| P152 | Velocity scaling | Done | tests/unit/test_engine_m3.cpp "Velocity: accent, 4-bit volume or ignored" | Range 0.05-1 per D-049 |
| P153 | Written-note preview | Done | `gemui/test_seq_grid.cpp` "Preview of a written note: velocity 110, cut after 240 ms or by the next one (P153)", "Preview of a drum hit goes through the sequencer path (seqprev), not a note (P153)" | A drum hit is previewed as the note the engine receives for that drum step (track octave and row), where the prototype sends the row index; the drum heard is the same |
| P154 | Mixer window | Done | test_mixer_window.cpp "Mixer: Mixer button and Ctrl+M open the window, Escape closes it"; scene mixer | D-128; scene mixer a STYM2149 scene since the value rows of D-300 |
| P155 | Mixer channel strip | Deviation | test_mixer_window.cpp "Mixer: fader and panning linked to the track's level and pan, one step per drag", "Mixer: M and S, label of the strip", "Mixer: 3 ch, strips A B C Digi without panning, faders disabled, mono output"; scenes mixer, mixer_3ch; "Mixer: values under the faders and the pans; right click: Value... and Reset (D-300)" | D-128; values under the fader and the pan, a right click to type or reset them (D-300) |
| P156 | Mute and Solo | Done | test_mixer_window.cpp "Mixer: M and S, label of the strip"; test_engine.cpp "3-channel: live mute of a track"; test_audio_export.cpp stems |  |
| P157 | VU meters | Done | test_mixer_window.cpp "Mixer: VU meters (48 dB scale, peak hold, clipping), outputs"; test_engine_meters.cpp | Decay 50 %/s at 30 Hz, same rate (D-128) |
| P158 | Output meters, mono in 3ch | Done | test_mixer_window.cpp "Mixer: VU meters (48 dB scale, peak hold, clipping), outputs", "Mixer: 3 ch, strips A B C Digi without panning, faders disabled, mono output"; scene mixer_3ch |  |
| P159 | Mixer undo per gesture | Done | test_mixer_window.cpp "Mixer: fader and panning linked to the track's level and pan, one step per drag" | Pan double-click leaves an undo step (D-128) |
| P160 | Patches window | Done | tests/gemui/test_patches_window.cpp "Patches: button, Ctrl+P and Edit menu open the window; empty library"; scenes lib, lib_list | D-145 |
| P161 | Patches Save current | Done | test_patches_window.cpp "Patches: Save current, loading into the focused track, one undo step" |  |
| P162 | Patch list click and right click | Done | test_patches_window.cpp "Patches: menu of a patch, Load, Rename, Overwrite, Delete" |  |
| P163 | Patch library import/export | Done | tests/gemui/test_file_menu.cpp "Patches: Import adds the patches of the file, Export writes the library" | D-168 |
| P164 | Preset, pattern, song files | Done | tests/unit/test_project_files.cpp; test_file_menu.cpp "File: Load of a preset, refusals and alerts of the prototype", "File: Load of a pattern, a Song and a kit, one undo step each" | D-165, D-168 |
| P165 | MIDI file import | Done | tests/unit/test_midi_files.cpp [midi][proto]; test_midi_menu | Deviations in D-170 |
| P166 | YM file import | Done | tests/gemui/test_ym_window.cpp "Import > YM dump..." (file filter); tests/unit/test_ym_player.cpp | D-159 (all SNDH), D-043 (LZH) |
| P167 | YM Player window | Deviation | D-169, D-027; test_ym_player.cpp; test_ym_window.cpp "YM Player: position of the prototype (ymSetPos)"; scene ym | Plays at file clock and rate, loops at loop frame |
| P168 | Shortcuts window (helpWin) | Done | `gemui/test_help_window.cpp` "Help window: ^/ and ^? open it, Esc and its close box close it", "Help window: scrolled by its bar, reopened where it was left with its scroll, place kept"; scene `help` (0.618 %); D-189 | Floating SHORTCUTS window of 560 px at (120, 70), as #helpWin |
| P169 | Voices mode selector | Done | scenes mode3, dual; tests/gemui/test_dma_window.cpp; test_plugincore.cpp mode change after fade | Computer row added below (D-155) |
| P170 | Clock Internal/MIDI and SYNC/STOP | Done | `unit/test_plugincore.cpp` "Mapping: Clock selector, clock of the arpeggio and of the sequencer (D-188)", "Driver: Clock selector on MIDI ext, the sequencer follows the MIDI clock (D-188)"; `gemui/test_arp_window.cpp` "MIDI clock: waiting, then SYNC or STOP at each publication (D-084)"; scene `arp_midi`; D-188 | The Clock selector sets both the arpeggiator and the sequencer clock, as setClkMode |
| P171 | Master volume | Done | test_plugincore.cpp "Mapping: parameter defaults = defaults of the engine and of the prototype"; scene default |  |
| P172 | Voice LEDs | Done | test_plugincore.cpp "Engine status: LEDs of audible tracks, attacked note, followed envelope, oscilloscope"; scene tabs | D-088 |
| P173 | Note TP readout | Done | test_plugincore.cpp "Engine status: LEDs of audible tracks, attacked note, followed envelope, oscilloscope" (note period); scene default | TP= text untested; uses chosen chip clock |
| P174 | Main oscilloscope | Done | test_plugincore.cpp "Engine status: LEDs of audible tracks, attacked note, followed envelope, oscilloscope"; scene default | D-088, D-075 |
| P175 | Register strip | Done | scenes default, sid, sync, chipdrum; test_instruments_window.cpp "Program Change: the track's instrument shown in the register monitor" | Monitor named CHIPDRUM (D-082) |
| P176 | Engine status updates (telemetry) | Deviation | D-088, D-128; tests/plugin/test_editor.cpp "Processor: engine status published for the editor at 30 Hz" | 30 Hz instead of 20 Hz |
| P177 | Window button bar | Done | button clicks in test_mixer/arp/mod/patches/kit_window.cpp; scene default |  |
| P178 | Floating windows Z order | Done | tests/gemui/test_windows.cpp "Stacked windows: a press brings the window to the front, what it covers receives nothing" | D-117 |
| P179 | Patch undo step (armCur/pushCur) | Done | test_arp_window.cpp "Undo: settings of the main window, one step per gesture on the focused track"; test_ymseq.cpp "ymseq: undo over 50 levels, identical levels skipped" | D-129 |
| P180 | Rectangle selection move/clear | Done | [proto][ymseq] (rmove, rclear, rpaste) | D-120; grid mouse gestures not UI-tested |
| P181 | Note selection with Shift+click | Done | [proto][ymseq] (tsel, ssel) | D-120; UI gesture not tested |
| P182 | Step block selection and clipboard | Done | [proto][ymseq] (bpaste, bclear) | Menu on release (D-120); UI gesture not tested |
| P183 | Track arming on first note | Done | `gemui/test_seq_grid.cpp` "First note written on a disabled track enables it (armChan, P183)" |  |
| P184 | Audio engine boot | Not kept | D-020, D-067 | Host drives audio; no Run program screen; host MIDI (D-074) |
| P185 | 112 voices and voice stealing | Done | tests/unit/test_engine.cpp "Engine: beyond kMaxEvents per block, events are counted and ignored" (every track voice and free voice taken) | Engine::kVoices=112; reserved voices D-107 |
| P186 | 3ch mode and dual chip | Done | test_engine.cpp "Dual chip: tracks 5 to 8 play on the second chip", "3-channel: Digidrum track steals the designated channel, plays the sample on the timer" | Shared registers D-050; track roles D-063 |
| P187 | Pitch bend | Done | test_engine.cpp "16-channel: pitch bend on the sounding voices"; test_proto_engine.cpp periods with bend; test_plugincore.cpp "Driver: pitch bend according to the set range" |  |
| P188 | Panic fade | Done | test_engine.cpp "Engine: change of mode during the render"; test_plugincore.cpp "Driver: on-screen notes on the keyboard track, CC 120 and 123 from the screen" | Fade per D-051 |
| P189 | External MIDI clock for sequencer | Done | `unit/test_engine_seq.cpp` "Sequencer: MIDI clock, steps according to the resolution, Stop, SPP and Continue"; `unit/test_plugincore.cpp` "Driver: Clock selector on MIDI ext, the sequencer follows the MIDI clock (D-188)"; D-188 | Play arms the sequencer, MIDI Start runs it |
| P190 | Model constants | Done | test_plugincore.cpp "Parameters: complete table, stable and unique ids", "State: sequencer, cells clamped, unknown rows ignored" | 16 tracks, 256 steps, 6 voices |
| P191 | Global defaults | Done | test_plugincore.cpp "Mapping: parameter defaults = defaults of the engine and of the prototype" |  |
| P192 | Factory kit banks | Done | tests/unit/test_kit.cpp "Chipdrum kit: identical to the prototype, 13 banks" (kit.txt) |  |
| P193 | Kit instrument parameters | Done | test_kit.cpp bounds case; tests/gemui/test_kit_window.cpp |  |
| P194 | Kit slot and note mapping | Done | test_kit.cpp; [proto][seq] chipdrum tracks | OCT/TON transpose drums (D-098) |
| P195 | Kit sent to engine | Done | test_plugincore.cpp "Driver: non-automatable settings published during playback" | D-144 |
| P196 | Kit parameter tooltips | Done | test_kit_window.cpp (tooltip "(6 to 20)"); test_tooltips |  |
| P197 | Kit sliders, one undo per drag | Done | test_kit_window.cpp "Drum Kit: instrument, settings by slider and typed input, kit name, one step per gesture" |  |
| P198 | Kit direct value entry | Done | test_kit_window.cpp same case | Entry makes an undo step and updates name (D-144) |
| P199 | Detected kit name | Done | test_kit_window.cpp "Drum Kit: kit name, the first identical bank, otherwise Custom"; scene chipdrum |  |
| P200 | Kit instrument selector | Done | test_kit_window.cpp; scene kit, kit_clap |  |
| P201 | Kit enabled by generator | Done | test_kit_window.cpp "Drum Kit: outside Chipdrum, a note and the content inert"; scene kit_off |  |
| P202 | Open and close Kit window | Done | test_kit_window.cpp "Drum Kit: button, Ctrl+K and Edit menu open the window; boxes of the prototype" |  |
| P203 | Kit Audition | Done | test_kit.cpp "Chipdrum kit: audition of an instrument (Audition, drumprev)"; test_plugincore.cpp "Driver: Drum Kit Audition, audition of an instrument outside the voices (UiCommand::DrumPreview)" |  |
| P204 | Kit Reset instrument | Done | test_kit_window.cpp "Drum Kit: Audition, Reset of the instrument, banks of the Kits menu" [undo] |  |
| P205 | Kit Bank menu | Done | test_kit_window.cpp same case | Sorting per D-144 |
| P206 | Load kit .json | Done | test_project_files.cpp "Kit: file read back, merge per instrument, old format, refusal"; test_file_menu.cpp (alert "Not a valid drum kit file.") |  |
| P207 | Window dragging | Deviation | D-067, D-117; test_windows.cpp "Floating window: dragged by its bar, bounded at the top and left, closed by its box" | Main window is fixed; floating windows drag |
| P208 | Floating window list and Z stack | Done | test_windows.cpp "Stacked windows: a press brings the window to the front, what it covers receives nothing" | D-117; helpWin absent (see P168) |
| P209 | Per-track scope windows | Done | test_mixer_window.cpp "Oscilloscopes: one per track, opened from the Mixer, offset by 24 px, forgotten on closing"; scene mixer_scope | D-128 |
| P210 | Big scope drawing | Done | test_mixer_window.cpp oscilloscope case (2 px green trace, y formula) |  |
| P211 | Window titles follow track | Done | test_arp_window.cpp, test_mod_window.cpp titles; scenes seq, seq_demo |  |
| P212 | Help window, 9 sections | Done | `gemui/test_help_window.cpp` "Help window: the prototype's nine sections, then STYM2149's keys"; scene `help`; D-189 | The prototype's HELP list word for word, then a tenth section More keys for the keys STYM2149 adds (D-189) |
| P213 | Window shortcuts in help | Done | `gemui/test_help_window.cpp` "Help window: the prototype's nine sections, then STYM2149's keys"; scene `help`; D-189 |  |
| P214 | Generator shortcuts in help | Done | scene menu_generator (hints); test_arp_window.cpp (Ctrl+S); `gemui/test_help_window.cpp` "Help window: the prototype's nine sections, then STYM2149's keys"; D-189 | Listed in the SHORTCUTS window |
| P215 | File shortcuts in help | Deviation | `gemui/test_help_window.cpp` "Help window: the prototype's nine sections, then STYM2149's keys"; `gemui/test_confirm.cpp` "Demo pattern and Quit confirms: Cancel by default, only OK goes on (P417)" (^Q); `gemui/test_main_controls.cpp` "Ctrl+D: the DMA generator on the STE, the demo pattern left to the File menu (D-301)"; D-189, D-301 | ^D picks the DMA generator; Demo pattern keeps its File menu entry, without a shortcut (D-301) |
| P216 | Transport and edit shortcuts doc | Deviation | `gemui/test_help_window.cpp` "Help window: the prototype's nine sections, then STYM2149's keys"; scene `help`; D-189 | M is listed as metronome on / off: the prototype's list said mute the focused channel, but its code (and STYM2149's) toggles the metronome |
| P217 | Song window shortcuts doc | Done | `gemui/test_help_window.cpp` "Help window: the prototype's nine sections, then STYM2149's keys"; `gemui/test_song_window.cpp` "Song: keyboard control when the window is in front"; D-189 |  |
| P218 | Stateless window open buttons | Done | scenes mixer, arp, orn, lib, seq (buttons off) | MainWindow::syncWindowButtons |
| P219 | Open/close Arp and Sequencer | Done | test_arp_window.cpp "Arpeggiator: button, Ctrl+B and Edit menu open the window; not applicable on percussion"; scenes seq, seq_demo | fitTrSlider implemented, not separately tested |
| P220 | Patch library storage | Deviation | D-141, D-145; test_editor.cpp "Editor: Patches library in Patches.json, next to the preferences (D-141)" | Patches.json next to prefs instead of localStorage |
| P221 | Patch snapshot (libSnap) | Done | tests/unit/test_patches.cpp "Patches: the sound of a new track is written like the export of the prototype" |  |
| P222 | Apply patch (libApply) | Done | test_patches.cpp "Patches: missing keys, out-of-range values, short arrays" |  |
| P223 | Patch list click loads | Done | test_patches_window.cpp "Patches: Save current, loading into the focused track, one undo step" | Load makes an undo step (D-145) |
| P224 | Patch context menu | Done | test_patches_window.cpp "Patches: menu of a patch, Load, Rename, Overwrite, Delete" |  |
| P225 | Save patch prompt | Done | test_patches_window.cpp (asserts "Patch 1") |  |
| P226 | Export patch library | Deviation | D-060, D-165; test_file_menu.cpp (name stym2149-patches.json) | File name without the prototype's former author prefix |
| P227 | Import patch library | Done | test_file_menu.cpp "Patches: Import adds the patches of the file, Export writes the library"; test_patches.cpp "Patches: library file, bare array, invalid entries, renaming" | Bare array also accepted (D-145) |
| P228 | Open/close Patches | Done | test_patches_window.cpp "Patches: button, Ctrl+P and Edit menu open the window; empty library" |  |
| P229 | Track tabs | Done | `gemui/test_main_controls.cpp` "Track tabs: a click focuses the track, a click on the focused tab turns it on or off (P229)"; test_dma_window.cpp (click tab5 changes focus); scene tabs |  |
| P230 | Track tab context menu | Done | `gemui/test_main_tracks.cpp` "Track tab context menu: Copy settings, Paste settings, Rename...; disabled in 3ch (P230)"; D-077 |  |
| P231 | Copy/paste track settings | Done | `gemui/test_main_tracks.cpp` "Copy settings and Paste settings: patch, arpeggio, level and gate, not on/pan/mute/solo (P231)" |  |
| P232 | All on / All off | Deviation | D-083, D-153; `gemui/test_main_controls.cpp` "All on / All off: every track of the mode, 4 in 3ch, 8 with two chips (P232, D-083)" | 8 tracks in Dual chip (prototype 4); in 3ch on the STE the DMA tracks count too |
| P233 | Rename track | Done | test_plugincore.cpp "State: track names (NAME chunk, written only if a track is named)" | D-077; rename prompt UI untested |
| P234 | Track display name | Done | test_plugincore "State: track names (NAME chunk, written only if a track is named)"; test_dma_window "DMA: DMA tracks in 3 ch mode with the STE (tabs, enforced role, Mixer)"; scenes mode3, dual, tabs | A,B,C,Digi / A1..Digi2 in 3ch; user names per D-077 |
| P235 | Track focus | Done | test_dma_window "DMA: DMA tracks in 3 ch mode with the STE (tabs, enforced role, Mixer)" (tab click focuses track); test_plugincore "Driver: controllers of the prototype → parameters, host notified" (CC84); test_mixer_window "Mixer: M and S, label of the strip"; scene tabs |  |
| P236 | Tab labels in 3ch mode | Done | scenes mode3, dual; test_dma_window "DMA: DMA tracks in 3 ch mode with the STE (tabs, enforced role, Mixer)" (trackTitle, disabled tabs) | Focus reset to track 0 on mode change coded in MainWindow mode menu, not directly tested |
| P237 | Button focus release | Done | src/gemui/src/Desktop.cpp (only ValueField/TextField take key focus); test_arp_window "Undo: settings of the main window, one step per gesture on the focused track" (keys still work after clicks) | By design; no dedicated test |
| P238 | Tone/noise source | Done | test_arp_window "Undo: settings of the main window, one step per gesture on the focused track" (srcSel Tone+Noise); test_plugincore "Mapping: parameter defaults = defaults of the engine and of the prototype"; scene menu_source |  |
| P239 | Poly/mono voicing | Done | test_proto_engine "Prototype: legato of a monophonic track (45 ms slide)"; test_proto_seq [proto][seq] (mono); test_ymseq_edit [proto][ymseq] (mono, drum contexts); scenes default, mode3, seq | polySel/seqPolySel menus (mono cancels chord, disabled in 3ch) not UI-tested |
| P240 | Volume mode | Done | scene envelope (Envelope volume mode); test_engine "3-channel: R11-R13 to the Sync Buzzer first, then back to the envelope voice"; test_plugincore "Mapping: parameter defaults = defaults of the engine and of the prototype" |  |
| P241 | Chip mode 16ch/3ch/dual | Deviation | D-063, D-064; test_plugincore "Mapping: roles imposed in 3 channels (fxAllowed), setting kept", "Driver: Digidrum definition kept from 16 to 3 channels and back (D-063)", "Driver: voice mode change after a fade, notes ignored meanwhile"; test_engine "Engine: change of mode during the render"; scenes mode3, dual | No ch3Saved: roles imposed at render, user settings kept; panic fade on switch; absent LEDs at 25% (D-088) |
| P242 | 3ch voice names | Done | test_dma_window "DMA: DMA tracks in 3 ch mode with the STE (tabs, enforced role, Mixer)"; test_mixer_window "Mixer: 3 ch, strips A B C Digi without panning, faders disabled, mono output"; test_audio_export "Stems: channel names in 3 channels, dual chip and DMA tracks"; scenes mode3, dual, mixer_3ch | DMA tracks added by D-153 |
| P243 | GEM submenu arrow | Done | scenes menu_file, menu_options (src/gemui/src/Menu.cpp 8x8 arrow) |  |
| P244 | Shared envelope label | Done | scenes mode3 "(shared)" and default "(per channel)" |  |
| P245 | Noise period | Done | test_midi_learn "MIDI learn: learnt CCs and notes drive sliders and buttons; shared map" (1-31, default 8); test_plugincore "Parameters: complete table, stable and unique ids"; test_engine "3-channel: R6 follows the last noise switched on, then returns to the remaining noise" | 3ch propagation to all tracks coded in MainWindow, not UI-tested; R6 rule D-050 |
| P246 | 4-bit volume | Done | test_plugincore "Mapping: parameter defaults = defaults of the engine and of the prototype" (13); test_proto_engine "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)"; scene default |  |
| P247 | SID volume floor | Done | test_engine "3-channel: SID effect with the MFP timer or continuous" (sidMinVolume); test_engine_timerwave; scene sid | 0-15, default 0 |
| P248 | SID playback ratio | Done | test_plugincore "Parameters: bounds, step and normalised position" (sidRatio); test_proto_engine "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)" (SID); scene sid; `gemui/test_digidrum_window.cpp` "Main window: the SID timer ratio explains the ratio, not a Digidrum rate (P033, D-029)" | Acts on the SID timer only; its tooltip says so (D-029) |
| P249 | Envelope frequency | Deviation | D-073, D-010; test_plugincore "Mapping: CC 74 like the Env freq slider of the prototype", "Parameters: bounds, step and normalised position" (envPeriod log); scene envelope | Chip EP units, geometric slider, default 12800 ($3200) instead of 400 |
| P250 | Envelope shapes | Deviation | scene menu_shape; test_envelope "S8 envelope shapes: 16 values of R13 × 4 cycles of 32 steps"; test_main_controls "Shape R13 menu: shapes 8 to F, a value below 8 marks the shape it plays as (third DAW test, item 22, D-221)" | D-221: the menu lists 8 to F, the engine still plays the 16 values |
| P251 | Slide curves | Done | test_proto_engine "Prototype: 13 slide curves", "Prototype: slide quantisation (Chromatic, Scale-locked)"; test_curve_window "Slide Curve: the Custom entry of the CRV row opens the window; Escape does not close it" (13 entries, separators) |  |
| P252 | Custom curve editor | Done | test_curve_window "Slide Curve: drawing with the mouse, without an undo step"; scenes curve, curve_custom | Drawing details per D-130 |
| P253 | Curve end warning | Done | test_curve_window "Slide Curve: Linear, Smooth, Invert and warning" |  |
| P254 | Curve Reset/Smooth/Invert | Done | test_curve_window "Slide Curve: Linear, Smooth, Invert and warning" | Reset labelled Linear (D-130) |
| P255 | Curve window | Done | test_curve_window "Slide Curve: the Custom entry of the CRV row opens the window; Escape does not close it", "Slide Curve: the reopened window gets its place back"; scene curve |  |
| P256 | Vibrato depth | Done | test_proto_engine "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)" (vibrato); test_plugincore "Parameters: complete table, stable and unique ids"; scene smooth | 0-200 cents, default 0 |
| P257 | Vibrato rate | Done | test_proto_engine "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)" (vibrato); test_plugincore "Mapping: parameter defaults = defaults of the engine and of the prototype" | 1-20 Hz step 0.5, default 6 |
| P258 | VBL rate | Done | test_proto_engine "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)" (50/60/70); test_ticks_state "Ticks: replay tick from 50 to 200 Hz (VBL Rate parameter)"; test_seq_ticks "Ticks: Settings > VBL frequency, 50 to 200 Hz, out of the Options menu (D-441)" | Global VBL Rate (PARITE target); 100/200 Hz added by D-149 |
| P259 | ADSR mode | Done | test_proto_engine "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)" (ADSR Chip); test_proto_seq [proto][seq] (adsrChip); scenes envelope, smooth, mode3 | Chip forced in 3ch except DMA tracks |
| P260 | A/D/S/R sliders | Deviation | D-085, D-427, D-428; test_engine "16-channel: smooth ADSR, stages and sustain level"; test_proto_engine "Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)"; scenes envelope, smooth | A, D and R 0-100 ticks in Chip mode and 0-2000 ms in Smooth (D-428), where the prototype stopped A and D at 64 ticks and 1000 ms, R at 64 ticks |
| P261 | ADSR direct value entry | Done | `gemui/test_main_controls.cpp` "ADSR values typed in: rounded and clamped in the unit of the mode (P261)"; D-085 | Chip Attack and Decay stop at 1000 ms (D-085) |
| P262 | ADSR curve display | Done | scenes envelope, smooth; test_plugincore "Engine status: LEDs of audible tracks, attacked note, followed envelope, oscilloscope" | Live dot (D-088) and greyed Sync Buzzer curve not tested |
| P263 | ADSR inert on Sync Buzzer | Deviation | `gemui/test_main_controls.cpp` "ADSR on Sync Buzzer in Chip mode: the frame is inert and the mouse goes through it (P263)" and "ADSR not applicable: the ADSR and ADSR ENVELOPE frames greyed out with their titles (third DAW test, item 21, D-220)"; test_tooltips "Tooltips: every line is translated" visits the state | D-220: the ADSR ENVELOPE frame is greyed out too, and the hint reads "envelope only" (the prototype's "buzzer: envelope only" ran past the frame); the explanation is in the tooltip of the mode button, out of reach while the frame is inert, as in the prototype (pointer-events: none) |
| P264 | Track level | Done | test_mixer_window "Mixer: fader and panning linked to the track's level and pan, one step per drag"; test_plugincore "Mapping: parameter defaults = defaults of the engine and of the prototype" (75) |  |
| P265 | Master volume | Done | test_plugincore "Mapping: parameter defaults = defaults of the engine and of the prototype" (70), "Driver: controllers of the prototype → parameters, host notified" (CC7); scene default |  |
| P266 | Generator choice | Done | scene menu_generator; test_plugincore "Mapping: roles imposed in 3 channels (fxAllowed), setting kept", "Driver: controllers of the prototype → parameters, host notified" (CC76, Sync Buzzer EP); test_arp_window "Arpeggiator: button, Ctrl+B and Edit menu open the window; not applicable on percussion" | Buzzer EP>3200 to 32 is proto 100 to 1 in chip units (D-073); 3ch roles D-063; DMA type D-153 |
| P267 | Manual play panic | Done | test_kbd_window "Computer Keyboard: opening, playing only with the window open, closing that cuts everything"; test_plugincore "Driver: on-screen notes on the keyboard track, CC 120 and 123 from the screen" |  |
| P268 | 8 sample slots | Done | scene digidrum; test_plugincore "Driver: Digidrum definition kept from 16 to 3 channels and back (D-063)"; test_patches (slot) |  |
| P269 | Load a sample | Deviation | D-078, D-154; test_plugincore "Digidrum: audio file brought to 7800 Hz on 4 bits (loading of the prototype)"; test_digidrum_editor "Digidrum: loading rate and maximum length" | JUCE file formats; no "audio started" requirement |
| P270 | Waveform display | Deviation | D-078, D-029 (empty slot text), D-154; `gemui/test_digidrum_window.cpp` "Main window: the Digidrum frame prints the slot's own rate, not always 7800 Hz (P270, D-154)" | Waveform masked in scene digidrum (D-086); the text gives the slot's rate set in the Digidrums window, where the prototype always wrote 7800 Hz |
| P271 | Digidrum ST channel 3ch | Done | test_engine "3-channel: Digidrum track steals the designated channel, plays the sample on the timer"; test_plugincore "State: round trip, reference v1 file" (digidrumChannel) | digiChSel popup has no test or scene |
| P272 | Arp On/Off | Done | test_arp_window "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division" (off sends allOff), "Arpeggiator: button, Ctrl+B and Edit menu open the window; not applicable on percussion"; scenes arp, arp_on |  |
| P273 | Arp mode | Done | test_arp_window "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; test_proto_engine "Prototype: arpeggio played on the keyboard, 6 modes" |  |
| P274 | Arp fill from chord | Done | test_arp_window "Arpeggiator: table (grid, number of steps, chords, keyboard, playing step)", "Arpeggiator: each setting is a track undo step (D-129)"; test_proto_engine "Prototype: 39 chords" |  |
| P275 | Arp octaves | Done | test_arp_window "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; test_proto_engine "Prototype: arpeggio played on the keyboard, 6 modes" |  |
| P276 | Arp glide rate tracking | Done | test_arp_window "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; test_proto_seq [proto][seq] (arp track) |  |
| P277 | Arp Land | Done | test_arp_window "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; test_proto_seq [proto][seq] (arp land) |  |
| P278 | Arp rate | Done | test_arp_window "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division" (120); test_plugincore "Parameters: complete table, stable and unique ids"; test_proto_seq [proto][seq] | 4-200 Hz, default 50 |
| P279 | Arp table length | Done | test_arp_window "Arpeggiator: table (grid, number of steps, chords, keyboard, playing step)" | 1-16, default 3 |
| P280 | Arp table grid | Done | test_arp_window "Arpeggiator: table (grid, number of steps, chords, keyboard, playing step)"; test_engine "Arpeggio: step of the table played (astep), -1 outside Table mode or without a held note"; scene arp_table |  |
| P281 | Clock source Internal/MIDI ext | Done | `gemui/test_seq_window.cpp` "Step Sequencer: Clock on MIDI ext, BPM slider greyed out (D-188)"; `unit/test_audio_export.cpp` "WAV export: Clock on MIDI ext, the sequencer plays at the free tempo (D-188)"; `gemui/test_arp_window.cpp` "MIDI clock: waiting, then SYNC or STOP at each publication (D-084)"; D-188 | MIDI ext greys the BPM slider; offline exports keep the internal clock |
| P282 | Arp division on MIDI clock | Deviation | test_arp_window "Arpeggiator: on/off, mode, rate, Track, Land, octaves, MIDI division"; test_engine "Arpeggio: MIDI clock, one step every `division` pulses"; scene arp_midi | Default 1/16 in test_plugincore "Parameters: complete table, stable and unique ids"; no DAW step in TESTS_DAW.md; the divisions in a menu button (D-290), on the row of Mode after Sync (D-295) |
| P283 | Factory chords | Done | test_proto_engine "Prototype: 39 chords" |  |
| P284 | Chord on note entry | Done | test_ymseq_edit [proto][ymseq] (chord, mono, drum, 3ch contexts); scenes seq, seq_demo | seqChord menu itself not UI-tested |
| P285 | Note figures | Done | test_ymseq "ymseq: scales, figures, bars and names" (figureSteps); test_ymseq_edit [proto][ymseq] (figure context) |  |
| P286 | Step resolutions | Done | test_ymseq "ymseq: scales, figures, bars and names" (beatSteps); test_engine_seq "Sequencer: swing and triplet resolutions" |  |
| P287 | Time signatures | Done | test_ymseq "ymseq: scales, figures, bars and names" (beatSteps, barSteps); test_engine_host "Metronome: one beep per beat, higher on the first beat" |  |
| P288 | Note name language | Done | test_ymseq "ymseq: scales, figures, bars and names" (English/Latin); test_kbd_window "Computer Keyboard: octave by the buttons, Z and X, the keypad and the drawing" (Do2 to Fa3); test_editor "Editor: zoom chosen in the menu or by resizing, kept in the preferences" | User preference, not plugin state (D-069) |
| P289 | Tooltips on/off | Done | scene menu_options (entry); src/gemui/src/Desktop.cpp setTooltips | D-069, D-072; toggle has no behavioural test |
| P290 | Factory scales | Done | test_ymseq "ymseq: scales, figures, bars and names" (26 entries, masks); test_ymseq_edit [proto][ymseq] (scale contexts) |  |
| P291 | Scale root and snap | Done | test_ymseq "ymseq: scales, figures, bars and names" (inScale, snapToScale); test_ymseq_edit [proto][ymseq] (snap) | No snap on percussion rows (D-090); greying of out-of-scale rows has no test or scene |
| P292 | Sequencer row labels | Done | scenes seq, seq_demo (melodic labels) | Kick..Tom2 and "Sl 1-8" labels (SeqGrid.cpp) have no test or scene |
| P293 | Step grid: 64 columns, 13 note rows, step header | Done | scenes `seq`, `seq_demo` (tests/gemui/test_visual.cpp) | SeqGrid kCols=64, kRows=13; small texts D-119; 2 % budget D-121 |
| P294 | Horizontal scroll for patterns over 64 steps | Done | `gemui/test_seq_grid.cpp` "Horizontal scroll: shown above 64 steps, a-b readout, the grid follows (P294)" |  |
| P295 | Vertical pitch scroll slider and wheel | Deviation | `gemui/test_seq_grid.cpp` "Notes scrolled by a bar at the right edge of the window, with arrows (D-294)"; scene `seq` (default 48); D-294 | A scroll bar at the window's right edge with arrows (a semitone) and a track press (an octave); range 23-108; wheel 1 semitone per notch (SeqGrid::mouseWheel) |
| P296 | SRC row: per-step tone/noise mix | Done | `tests/unit/test_proto_seq.cpp` "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (M op); scene `seq_demo` | Menu Channel/Tone/Noise/Tone+Noise (SeqGrid::laneMenu) not clicked by a test |
| P297 | GEN row: per-step generator | Done | test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (F op); scene `seq_demo` | Shows - CV SID SB CD; 3ch roles D-063; GEN before instrument generator D-147 |
| P298 | ARP row: per-step arpeggio flag | Done | test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)"; scenes `seq` (hidden, arp off), `seq_demo` (shown) | Engine fix D-097 for suspended track arpeggio |
| P299 | HLD row: Chipdrum no-retrigger hold | Done | test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (Chipdrum lanes with tie) | Row shown on Chipdrum only (SeqGrid::refresh); no scene of a Chipdrum grid |
| P300 | SLD row: slide to next note | Deviation | D-105; test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (S/G ops); scene `seq_demo` | Slide lands exactly on target pitch (prototype stopped short) |
| P301 | CRV row: slide curve, Custom opens editor | Done | `tests/gemui/test_curve_window.cpp` "Slide Curve: the Custom entry of the CRV row opens the window; Escape does not close it"; scene `seq_demo` (dimmed cells); test_proto_seq.cpp | 13 curves; D-109 CRV only for sequencer glides |
| P302 | SLT row: Digidrum slot per step (hidden) | Done | `tests/unit/test_plugincore.cpp` "State: round trip, reference v1 file" (slt in state_v1_m5); `tests/unit/test_project_files.cpp` pattern files; test_ymseq.cpp "ymseq: Euclidean generator" (slt) | Row hidden as in prototype, slot from grid row; playback ignores SLT like prototype (D-103); SLT absent from seq.txt lanes |
| P303 | CUT row (hidden, note length instead) | Done | test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (K op) | Hidden as in prototype; CUT counted on second chip (D-106) |
| P304 | OCT row: per-step octave shift | Deviation | D-098; test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" | OCT/TON transpose drum hits by 2^((OCT*12+TON)/12); prototype read OCT as absolute octave; menus -3..+3 and Channel/1-7 untested |
| P305 | TON row: per-step semitone tune (drums) | Deviation | D-098; test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" | Digidrum now transposed too (prototype did not) |
| P306 | GAT row: per-step gate popup | Done | test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)"; scene `seq_demo` | Popup Channel/Set (SeqPopups openGate) untested; step GAT beats global gate (D-134) |
| P307 | PAN row: per-step pan | Done | test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)" (P op); scene `seq_demo` | Hidden in 3ch except DMA tracks (D-153); panPopup untested |
| P308 | RTG row: ratchet 1-4 hits | Deviation | D-101; test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)"; scene `seq_demo` | Repeated hits keep step level x VOL x 0.92, FX and MIX |
| P309 | CND row: trigger conditions | Deviation | D-104; test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)"; scene `seq_demo` | 25/50/75 % drawn from position hash, not Math.random |
| P310 | VOL row: accent draw and slider | Done | test_proto_seq.cpp "Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)"; scene `seq_demo` (VOL bars) | Drag drawing and right-click slider (openVol) untested |
| P311 | Copy/paste a row across tracks | Done | `gemui/test_seq_grid.cpp` "Row labels: Copy row and Paste row across tracks, same row only, one undo step (P311)" |  |
| P312 | Ghost notes of other tracks | Deviation | D-120 | Only playable tracks of the mode (SeqGrid::buildGhosts); no automated test or scene with Ghost on |
| P313 | Overview map of the pattern | Done | `gemui/test_seq_grid.cpp` "Overview map: notes, out-of-pattern area, view frame and play head on a 256-step scale (P313)", "Overview map with a drum track: 26 px band, separator, the pitch of the view kept (P313)" | These tests found that the map did not follow playback, the scroll bar or the transpose slider; fixed in M8 |
| P314 | Lowest playable note B0 (PLAY_LO) | Done | `gemui/test_seq_grid.cpp` "Below B0 (PLAY_LO): cells hatched, not-allowed pointer, a click writes nothing (P314)"; `unit/test_ymseq_edit.cpp` (tsel ops) | As in the prototype, the view never scrolls below B0 from the interface; the test sets the view lower |
| P315 | Write a note by click | Deviation | D-090, D-098; `tests/unit/test_ymseq_edit.cpp` "Prototype: sequencer track edits" (click, fig, clrfig) | Snap ignored on drum rows; drum step preview plays the playback note; click dispatch (pressNote) untested |
| P316 | Drag painting: stretch or place notes | Done | test_ymseq_edit.cpp "Prototype: sequencer track edits" (stretch, note) | Drag routing (SeqGrid::over) untested |
| P317 | Move a note in pitch and time | Done | test_ymseq_edit.cpp "Prototype: sequencer track edits" (move) | Edge auto-scroll untested; preview at new start (D-120) |
| P318 | Resize note by left/right edge | Deviation | D-091, D-096; test_ymseq_edit.cpp "Prototype: sequencer track edits" (len, start, vsize) | Left stop = nearest previous note; chord voice start goes to a free voice |
| P319 | Chord voices in the grid | Done | test_ymseq_edit.cpp "Prototype: sequencer track edits" (voice, delv, vsize); test_proto_seq.cpp (chord voice ops) | Up to 6 voices; playback durations D-102 |
| P320 | Mono voice promotion | Done | test_ymseq_edit.cpp "Prototype: sequencer track edits" (promote, demote) | Click dispatch untested |
| P321 | Split note with Alt+click | Done | test_ymseq_edit.cpp "Prototype: sequencer track edits" (split) | Alt+click gesture untested |
| P322 | Grid tools: select, split, join, erase, move, draw | Done | scene `seq` (tool bar); test_ymseq_edit.cpp (split, join, erasenote, rmove) | Tool dispatch untested; D-120 select-tool frame no longer suppresses next menu |
| P323 | Shift+click selection (notes, rect, block) | Deviation | D-120; test_ymseq_edit.cpp "Prototype: sequencer track edits" (ssel, tsel, rmove) | Shift+click keeps the clicked note (prototype added view start twice); gesture untested |
| P324 | Right-click: note menu, frame, row reset | Deviation | D-120; `tests/gemui/test_seq_cmd.cpp` "CMD row: preference of the Resolution menu, input, eraser" (right-click reset) | Context menu opens on button release (Windows order); frame drag untested |
| P325 | Note context menu | Done | test_ymseq_edit.cpp "Prototype: sequencer track edits" (cp, len, split, join) | Menu (SeqGrid::noteMenu) untested; shortcuts ] [ backslash J per D-123 |
| P326 | Rectangle selection menu | Done | test_ymseq_edit.cpp "Prototype: sequencer track edits" (rpaste, rclear, rmove) | Menu (SeqGrid::rectMenu) untested |
| P327 | Block/channel menu on empty cell | Deviation | D-094; test_ymseq_edit.cpp (bpaste, bclear); test_ymseq.cpp "ymseq: INS row (M6), empty at 0, follows the note like GEN (D-147)" (row-scoped blocks) | Clear block removes chord voices; Rename greyed in 3ch (D-077); menu untested |
| P328 | Edit menu Cut/Copy/Paste | Done | scene `menu_edit`; test_ymseq_edit.cpp (cp, rpaste) | Routing editCutState/editClip (D-123) untested |
| P329 | Euclidean rhythm generator | Deviation | D-103; `tests/unit/test_ymseq.cpp` "ymseq: Euclidean generator", "ymseq: scales, figures, bars and names" (euclidName); test_ymseq_edit.cpp "Prototype: Bjorklund patterns" | Digidrum target writes slot as row; dialog (SeqPopups EuclidBox) untested |
| P330 | Pattern banks: 4x8 patterns, 256 steps | Done | test_ymseq.cpp "ymseq: pattern bank, wait during playback, cut and transposition"; test_plugincore.cpp "State: sequencer, cells clamped, unknown rows ignored" | kPatterns 32, kSteps 256 |
| P331 | Pattern schema (LANEKEYS) | Done | test_project_files.cpp "Preset, pattern, Song, kit: tweaked project written like the prototype", "Pattern: file read back, STYM2149 rows, refusal"; test_plugincore.cpp "State: sequencer, cells clamped, unknown rows ignored" | State block PATN (D-115); ins/cmd keys only when used (D-168, D-174) |
| P332 | Old 64-step pattern compatibility | Done | test_project_files.cpp "Pattern: file read back, STYM2149 rows, refusal", "Prototype preset: partial keys and out-of-range values" | padLane in plugincore ProjectFiles.cpp |
| P333 | Pattern queue at loop end | Deviation | D-100; `tests/unit/test_engine_seq.cpp` "Sequencer: a queued pattern applies at the next step 0"; test_proto_seq.cpp (queue msgs); test_ymseq.cpp "ymseq: pattern bank, wait during playback, cut and transposition" | Whole pattern applied (RTG, CND, CRV, OCT, TON, chord voices) |
| P334 | Bank and slot selectors | Done | scenes `seq`, `song`; test_ymseq.cpp "ymseq: pattern bank, wait during playback, cut and transposition" (select, bankLast); `gemui/test_seq_keys.cpp` "Digit keys: 1-8 pick a pattern of the bank shown, Shift+1-4 a bank, never the numeric keypad (P451)" | Slot clicks of the Step Sequencer not covered by a UI test |
| P335 | Pattern slot context menu | Done | test_ymseq.cpp "ymseq: pattern bank, wait during playback, cut and transposition" | Copy/cut/paste and transposePattern (drums excluded, octave 0 and chord voices); menu (patternMenu) untested |
| P336 | Banks and slots in Song window | Done | scene `song`; test_ymseq.cpp "ymseq: pattern bank, wait during playback, cut and transposition" (copy/cut/paste), "ymseq: Song, blocks copied, moved and dragged" (append) | Slot menu untested; test "Song: Add, Shift+Add and Add to song in a pattern's menu" only clicks Add |
| P337 | Undo/redo stack, 50 levels | Done | test_ymseq.cpp "ymseq: undo over 50 levels, identical levels skipped"; Ctrl+Z/Y in test_song_window.cpp, test_mixer_window.cpp, test_arp_window.cpp, test_kit_window.cpp, test_seq_cmd.cpp | Kinds Pattern, Song, Track, Mix, Kit (+Dma, Digi, Import); D-129 |
| P338 | Edit menu entries and shortcuts | Done | scene `menu_edit`; test_arp_window.cpp (Ctrl+B, Edit), test_mod_window.cpp (Ctrl+R, Edit), test_kit_window.cpp (Ctrl+K), test_patches_window.cpp (Ctrl+P), test_mixer_window.cpp (Ctrl+M) | ^E and ^G not tested; STYM entries added (D-147, D-148, D-151, D-152, D-154) |
| P339 | Song model, selection, drag threshold | Done | test_ymseq.cpp "ymseq: Song, additions, repeats and tempo", "ymseq: Song, blocks copied, moved and dragged"; test_engine_seq.cpp Song cases; test_song_window.cpp "Song: click, Shift+click, block menu and drag and drop" (5 px) | D-108 song walk on audio thread; D-115 rep max 65535; D-158 extended entries |
| P340 | Context hint under the grid | Done | scenes `seq`, `seq_demo` | Drum and 3ch variants (SeqWindow::refreshHint) not in a scene |
| P341 | Drag and drop of Song blocks | Done | test_song_window.cpp "Song: click, Shift+click, block menu and drag and drop"; test_ymseq.cpp "ymseq: Song, blocks copied, moved and dragged" | Opacity restored after a drag without move (D-126) |
| P342 | Song block label | Done | test_ymseq.cpp "ymseq: Song, additions, repeats and tempo" (songLabel); scene `song_demo` (tempo note) | Special labels D-158 (test_song_state.cpp) |
| P343 | Song chip list states | Done | scenes `song_demo` (selection, cursor), `song_special`; test_song_window.cpp "Song: Add, Shift+Add and Add to song in a pattern's menu" (nav buttons) | Playing-block highlight untested |
| P344 | Click and Shift+click on a block | Done | test_song_window.cpp "Song: click, Shift+click, block menu and drag and drop" |  |
| P345 | Song block context menu | Done | test_song_window.cpp "Song: click, Shift+click, block menu and drag and drop" (Repeat x4), "Extended Song: Rest, Note-off and Loop entries, length, jumps, restart point"; scene `song_special`; test_ymseq.cpp "ymseq: Song, blocks copied, moved and dragged" | D-158 adds Insert rest/note-off/loop, Length, Loop back, Restart here |
| P346 | Per-block tempo prompt | Done | test_ymseq.cpp "ymseq: Song, additions, repeats and tempo" (setTempo 20-300, blank); test_engine_seq.cpp "Sequencer: Song, repeats and own tempo of the blocks, looped" | Prompt dialog untested; engine reads block tempo each step (seqBpm) |
| P347 | Song keyboard navigation | Deviation | D-126; test_song_window.cpp "Song: keyboard control when the window is in front", "Song: Enter and the transport arrows jump from one block to another" | Enter jumps via player command (pattern replaced at once, entry rearmed); = and - read by character; Backspace untested |
| P348 | Song window toggles Song mode | Done | test_song_window.cpp "Song: opening the window switches Song mode on, closing it switches it off"; scenes `song`, `song_demo`; test_windows.cpp "Floating window: dragged by its bar, bounded at the top and left, closed by its box" | Floating window D-117; reload in Song mode opens it (D-126) |
| P349 | Add to Song button | Done | test_song_window.cpp "Song: Add, Shift+Add and Add to song in a pattern's menu"; test_ymseq.cpp "ymseq: Song, additions, repeats and tempo" |  |
| P350 | Block tempo during playback | Deviation | D-108, D-110; test_engine_seq.cpp "Sequencer: Song, repeats and own tempo of the blocks, looped"; test_engine_host.cpp "Host sync: Song relocated to the position of the host, at the tempo of the host" | Block tempo only during the block, Tempo parameter not changed; host tempo wins in Host sync |
| P351 | Song chaining during playback | Deviation | D-108; `tests/unit/test_engine_seq.cpp` "Sequencer: Song, repeats and own tempo of the blocks, looped", "Sequencer: Song without loop, stop after the last entry" | Block repeats and per-block tempo per D-108 |
| P352 | Rec button (sequencer window) | Done | scene `seq`; `SeqWindow::toggleRecord` | No behavioural test of the button itself |
| P353 | Figure (written note length) | Done | `tests/unit/test_ymseq.cpp` "ymseq: scales, figures, bars and names"; `tests/unit/test_ymseq_edit.cpp` "Prototype: sequencer track edits" [proto]; scene `seq` | Figure menu disabled on drum tracks |
| P354 | Grid resolution | Deviation | D-099, D-149; `tests/unit/test_engine_seq.cpp` "Sequencer: swing and triplet resolutions"; `tests/gemui/test_seq_ticks.cpp` "Ticks: Resolution menu, Ticks timing, typed speed and groove" |  |
| P355 | Ghost notes | Done | `gemui/test_seq_keys.cpp` "Ghost notes: the other melodic tracks behind the grid, at the pitch of the focused track (P355)"; D-120 |  |
| P356 | Overview row | Done | `gemui/test_seq_keys.cpp` "Overview button: shows and hides the overview row (P356)"; `gemui/test_seq_grid.cpp` (P313) |  |
| P357 | Time signature | Done | `tests/unit/test_ymseq.cpp` "ymseq: scales, figures, bars and names" (barSteps); `tests/unit/test_engine_host.cpp` "Metronome: one beep per beat, higher on the first beat" |  |
| P358 | Step recording | Deviation | D-095; `tests/unit/test_ymseq_edit.cpp` [proto] rec ops; `tests/gemui/test_kbd_window.cpp` "Host notes: keys lit, step recording, nothing sent back to the engine" |  |
| P359 | Metronome | Deviation | D-112; `tests/unit/test_engine_host.cpp` "Metronome: one beep per beat, higher on the first beat"; `tests/plugin/test_plugincore.cpp` "Driver: metronome muted outside real time (bounce, export)"; `tests/plugin/test_processor.cpp` "Processor: the metronome sounds, but not on the oscilloscope or the output meters (third DAW test, item 27, D-226)" | D-226: not drawn by the oscilloscope or the output meters |
| P360 | Realtime recording | Done | `tests/unit/test_ymseq_edit.cpp` [proto] rec (recordAt) | UI realtime path (grid playStep) untested |
| P361 | Save preset | Deviation | D-060, D-165, D-168; `tests/plugin/test_project_files.cpp` "Preset: new page written like the prototype"; `tests/gemui/test_file_menu.cpp` "File: Save and Save as, proposed name, .json added, Save that rewrites the same file" |  |
| P362 | Load preset | Done | D-168; `tests/plugin/test_project_files.cpp` "Prototype preset: read field by field, it rewrites the same file", "Preset: refusal, unreadable or more recent state"; `tests/gemui/test_file_menu.cpp` "File: Load of a preset, refusals and alerts of the prototype" |  |
| P363 | Default file names | Deviation | D-060, D-165; `tests/plugin/test_project_files.cpp` "Files: names offered without PRISM, ensureJson" |  |
| P364 | Save drum kit | Done | `tests/plugin/test_project_files.cpp` "Preset, pattern, Song, kit: tweaked project written like the prototype", "Kit: file read back, merge per instrument, old format, refusal" |  |
| P365 | GEM input box | Done | `gemui/test_confirm.cpp` "Prompt box: Cancel then OK; Tab goes field, Cancel, OK; Enter presses the focused button (P365)", "Prompts of the editor follow gemPrompt: Cancel then OK, none framed, Enter is OK (P365)"; `gemui/test_seq_cmd.cpp` prompt with Enter; D-191 |  |
| P366 | Save as | Deviation | D-168; `tests/gemui/test_file_menu.cpp` "File: Save and Save as, proposed name, .json added, Save that rewrites the same file" | Native file dialog |
| P367 | Save / load pattern | Done | D-168, D-174; `tests/plugin/test_project_files.cpp` "Pattern: file read back, STYM2149 rows, refusal"; `tests/gemui/test_file_menu.cpp` "File: Load of a pattern, a Song and a kit, one undo step each" |  |
| P368 | Save / load Song | Deviation | D-024, D-168; `tests/plugin/test_project_files.cpp` "Song: file read back, STYM2149 entries, patterns replaced" | Block bpm kept (D-024) |
| P369 | Offline engine | Deviation | D-160, D-163; `tests/unit/test_audio_export.cpp` "WAV export of a pattern: exact length, resampling delay compensated"; `tests/plugin/test_midi_files.cpp` "MIDI export: swing and tempo off the prototype's grid" |  |
| P370 | Pattern length in samples | Deviation | D-025; `tests/unit/test_audio_export.cpp` "WAV export of a pattern: exact length, resampling delay compensated" |  |
| P371 | YM3 export | Deviation | D-023, D-060, D-172; `tests/unit/test_ym_writer.cpp` "VBL and YM3: timer effects frozen, R13 restarted only when needed"; `tests/unit/test_register_export.cpp`; `tests/gemui/test_register_menu.cpp` | Song only (D-172) |
| P372 | Offline render | Done | `tests/unit/test_audio_export.cpp` | Block size internal |
| P373 | WAV export | Deviation | D-022, D-164, D-171; `tests/unit/test_wav_zip.cpp` "16-bit WAV: header and the prototype's conversion (wavBlobDv)"; `tests/gemui/test_audio_menu.cpp` "Export pattern > WAV: Format and Rate dialogue, preferences kept, file and alert" |  |
| P374 | Chip compliance indicator | Deviation | D-124, D-172; scenes `alert_chip`, `seq_demo` | Rules not unit-tested |
| P375 | Message box | Done | `tests/gemui/test_gemui.cpp` "Alert: shrink-to-fit width, limited to half of the desktop"; scene `alert_chip` |  |
| P376 | Busy cursor (bee) | Deviation | D-170; `tests/gemui/test_midi_menu.cpp` (Pointer::Busy); `tests/gemui/test_audio_menu.cpp`; `tests/gemui/test_register_menu.cpp` | Exports only; preset save/load not under the bee |
| P377 | GEM underline of editable fields | Done | scenes `default`, `kit` | D-144 |
| P378 | Register export submenu | Deviation | D-160, D-172; `tests/gemui/test_register_menu.cpp` "Export song > VBL dump, YM3, YM6, SNDH: Authentic 3ch, files, alerts" | YM6 added; greyed outside Authentic 3ch |
| P379 | YM3 import | Done | `tests/unit/test_ymfile.cpp` "YM2/YM3/YM3b: de-interleaving, loop" |  |
| P380 | SNDH import | Deviation | D-159, D-162; `tests/unit/test_sndh.cpp`; `tests/unit/test_oracle_sndh.cpp`; `tests/unit/test_ym_player.cpp` "YM Player: SNDH, subsong and crashed routine" | All SNDH files through 68000 emulation, not only those the prototype wrote |
| P381 | YM5/YM6 import | Done | D-044; `tests/unit/test_ymfile.cpp` "YM5/YM6: header, digidrums, strings, interleaved or not" |  |
| P382 | LHA detection | Deviation | D-043, D-169; `tests/unit/test_lzh.cpp` "LZH: parseYm decompresses an archived YM file" | Archives decompressed instead of refused |
| P383 | YM player | Deviation | D-169; `tests/gemui/test_ym_window.cpp` "YM Player: position of the prototype (ymSetPos)", "YM Player: Import > YM dump, Play and Stop, Rewind, closing"; scene `ym` | File clock and rate, project VBL untouched |
| P384 | MIDI import (SMF 0/1) | Deviation | D-170; `tests/plugin/test_midi_files.cpp` "MIDI import: patterns, Song, tracks and warning like the prototype (deviations of D-170)" [proto]; `tests/gemui/test_midi_menu.cpp`; `tests/unit/test_smf.cpp` |  |
| P385 | GM to kit slot mapping | Done | `tests/plugin/test_midi_files.cpp` [midi][proto] import and GM percussion export |  |
| P386 | MIDI export (SMF 1) | Deviation | D-060, D-170; `tests/unit/test_smf.cpp` "SMF: format 1 writing like the prototype's midiBlob"; `tests/plugin/test_midi_files.cpp`; `tests/gemui/test_midi_menu.cpp` |  |
| P387 | SNDH export | Deviation | D-162, D-172; `tests/unit/test_sndh_writer.cpp`; `tests/unit/test_oracle_sndh.cpp` "Exported SNDH: init (out-of-range subsong) and exit (MFP restored, sound cut)"; `tests/gemui/test_register_menu.cpp` |  |
| P388 | Stems export (ZIP) | Deviation | D-171; `tests/unit/test_audio_export.cpp` "Stems: ..."; `tests/unit/test_wav_zip.cpp` "ZipWriter"; `tests/gemui/test_audio_menu.cpp` "Export song > Stems (ZIP)" |  |
| P389 | Per-frame register capture | Deviation | D-023, D-157, D-160; `tests/unit/test_register_export.cpp` "Register capture: ..." |  |
| P390 | VBL export (.s) | Deviation | D-030, D-172; `tests/unit/test_ym_writer.cpp`; `tests/unit/test_register_export.cpp` "Register exports: files and alert notes" |  |
| P391 | Song segments | Deviation | D-024, D-163; `tests/unit/test_audio_export.cpp` "WAV export: formats, two-pass Song, cancel requested, write refused"; `tests/plugin/test_midi_files.cpp` | Empty Song refused (test_midi_menu, test_audio_menu) |
| P392 | Fuji menu | Done | `gemui/test_help_window.cpp` "Help window: Fuji menu entry Shortcuts ^?"; scenes `menu_fuji`, `info`; D-081, D-189 | Information and Shortcuts ^?, which opens the SHORTCUTS window |
| P393 | Menus open on hover | Done | scene `menu_file` | 260/220 ms close delays (Menu.cpp) untested |
| P394 | File menu | Deviation | D-070, D-157, D-161, D-173; scene `menu_file`; `tests/gemui/test_file_menu.cpp`, `test_midi_menu.cpp`, `test_audio_menu.cpp`, `test_register_menu.cpp`, `test_xm_menu.cpp` |  |
| P395 | Options menu | Deviation | D-074, D-080, D-149, D-185, D-430, D-441, D-442; scenes `menu_options`, `settings`, `settings_language`, `note_off`; `tests/gemui/test_midi_learn.cpp`; `tests/gemui/test_seq_ticks.cpp` "Ticks: Settings > VBL frequency, 50 to 200 Hz, out of the Options menu (D-441)"; `tests/gemui/test_settings_window.cpp` | The sound engine, MIDI and interface settings are in the Settings window (Options > Settings..., D-430), the VBL frequency too (D-441); Note monitor added (D-442) |
| P396 | UI zoom | Deviation | D-068; `tests/plugin/test_editor.cpp` "Editor: GEM desktop enlarged with nearest-neighbour scaling (100 and 200 %)", "Editor: zoom chosen in the menu or by resizing, kept in the preferences" |  |
| P397 | Audio output | Deviation | D-074, D-217; scene `menu_options` | One Audio/MIDI settings entry with MIDI input (D-217): JUCE audio and MIDI settings in Standalone, greyed in host; `[ui][menu]` test; TESTS_DAW step 10 |
| P398 | MIDI input | Deviation | D-061, D-074, D-217 | Host MIDI, JUCE devices in Standalone, set in Options > Audio/MIDI settings (D-217); no test of status text; TESTS_DAW step 2 |
| P399 | Keyboard layouts | Deviation | D-131, D-215; `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: keys according to the layout (kbdAction)" and "Computer Keyboard: Auto takes the system's layout (second DAW test, item 14, D-215)"; scene `kbd_azerty` | Auto: the system's layout (Windows, Linux; macOS TO CONFIRM) instead of the browser's keyboard map |
| P400 | Generic GEM menu | Deviation | `gemui/test_menu_columns.cpp` "Menu columns: the 39 chords fill as many columns as the desktop height needs (P400)", "Menu columns: the menu stays inside the desktop, every entry inside the menu (P400)", "Arpeggiator: Chord... opens the 39 chords with chordCols, the sequencer's Chord menu does not (P400)"; `gemui/test_gemui.cpp` "Menu: a checked entry is taller"; scenes `menu_*`; D-190, D-193 | Columns (chordCols) measured against Chromium at six desktop heights (D-190). Long menus are shown whole instead of stopping at 70 % of the height with a scroll bar; the longest, the sequencer's Chord menu (806 px), fits the 944 px desktop (D-193) |
| P401 | Step volume popup | Done | `tests/gemui/test_mod_window.cpp` "Modulation: right click on a VOL cell, setting by slider" | Shared popup; sequencer VOL path untested |
| P402 | Grid tools | Done | D-120; `tests/unit/test_ymseq_edit.cpp` [proto] (move, erase, split, join, rect); scene `seq` | Tool buttons untested |
| P403 | Clipboard buttons | Done | D-123; `tests/unit/test_ymseq_edit.cpp` [proto] cp and rect ops; scene `menu_edit` | Buttons untested |
| P404 | Transport bar | Done | D-110; `tests/gemui/test_song_window.cpp` "Song: Enter and the transport arrows jump from one block to another"; `tests/plugin/test_plugincore.cpp` "Driver: sequencer, interface commands, CC 82 and pattern of the state"; scenes `seq`, `song` |  |
| P405 | Start / previous / next block | Deviation | D-108, D-126; `tests/unit/test_engine_seq.cpp` "Sequencer: jump to a Song entry (songStep)"; `tests/plugin/test_plugincore.cpp` "Driver: jump to a Song entry, number on two bytes" |  |
| P406 | Play / Stop | Deviation | D-110, D-114; `tests/plugin/test_processor.cpp` "Processor: Host sync on the playhead, commands in Free run"; `tests/unit/test_engine_host.cpp` "Host sync: stop and resume of the host; Play, Start and MIDI clock without effect" | TESTS_DAW step 7 |
| P407 | Block copy / clear / paste | Deviation | D-094; `tests/unit/test_ymseq_edit.cpp` [proto] bpaste, bclear |  |
| P408 | Track rotation | Done | `tests/unit/test_ymseq_edit.cpp` [proto] rot, ssel |  |
| P409 | Transposition | Deviation | D-092; `tests/unit/test_ymseq_edit.cpp` [proto] tr, tsel; `tests/unit/test_ymseq.cpp` "ymseq: pattern bank, wait during playback, cut and transposition" |  |
| P410 | Root, scale, snap | Done | D-090; `tests/unit/test_ymseq.cpp` "ymseq: scales, figures, bars and names"; `tests/unit/test_proto_engine.cpp` "Prototype: slide quantisation (Chromatic, Scale-locked)" |  |
| P411 | Follow playback | Done | `gemui/test_seq_keys.cpp` "Follow: the view pages with the play head when on, stays when off (P411)" |  |
| P412 | Step count 1..256 | Done | `tests/unit/test_ymseq.cpp` (setSteps); `tests/unit/test_engine_seq.cpp` "Sequencer: length changed during playback, the current step is kept" |  |
| P413 | Vertical scroll | Done | `tests/gemui/test_windows.cpp` "Vertical slider: minimum at the bottom, checkerboard rotated with the track"; scene `seq` |  |
| P414 | Clear track | Deviation | D-093; `tests/unit/test_ymseq_edit.cpp` [proto] clear; `gemui/test_confirm.cpp` "Clear track confirm: Cancel by default; Enter, Tab then Enter, and Esc keep the notes (P417)"; D-192 | The confirm defaults to Cancel, as gemConfirm |
| P415 | Unsaved work protection | Deviation | D-197; `plugin/test_editor.cpp` "Editor: Preferences.settings takes over the old STYM2149.settings once, without the JUCE keys"; TESTS_DAW.md steps S1 to S3 | No confirmation on close: the host keeps the project, and the Standalone app saves its session on close and on File > Quit, then reopens it |
| P416 | Quit | Deviation | D-067, D-070, D-197; `gemui/test_confirm.cpp` "Demo pattern and Quit confirms: Cancel by default, only OK goes on (P417)" | Standalone confirm then quit, keeping the session (D-197); no "Program terminated" screen; inert in a host |
| P417 | Confirm box | Done | `gemui/test_confirm.cpp` "Confirm box: Tab and Shift+Tab move the focus between Cancel and OK (P417)", "Confirm box: the focus ring follows Tab, the 4 px frame stays on Cancel (P417)", "Clear track confirm: Cancel by default; Enter, Tab then Enter, and Esc keep the notes (P417)", "Digidrum bank confirms follow gemConfirm: Cancel first, framed and focused (P417)"; D-191, D-192 | Every confirm of the editor builds its buttons with confirmButtons() (Cancel first, default and focused) |
| P418 | Demo pattern | Deviation | D-125; scenes `seq_demo`, `song_demo`; `gemui/test_confirm.cpp` "Demo pattern and Quit confirms: Cancel by default, only OK goes on (P417)" |  |
| P419 | Track length (polymeter) | Done | `tests/unit/test_ymseq.cpp` (trackLength); state tests | Popup untested |
| P420 | BPM | Done | D-113; `tests/plugin/test_plugincore.cpp` "Parameters: complete table, stable and unique ids", "Parameters: bounds, step and normalised position" |  |
| P421 | Swing | Done | D-113; `tests/unit/test_engine_seq.cpp` "Sequencer: swing and triplet resolutions"; `tests/unit/test_engine_host.cpp` "Host sync: swing on the position of the host" |  |
| P422 | Gate | Done | D-134; `tests/gemui/test_seq_window.cpp` "Step Sequencer: All button of the global gate, on the row of the Gate slider (D-134)"; `tests/plugin/test_plugincore.cpp` "Mapping: global gate takes priority when Gate All is on (D-134)" | Global gate added |
| P423 | Channel panel refresh | Done | D-073; scenes `default`, `sid`, `sync`, `chipdrum`, `digidrum`, `envelope`, `smooth`, `mode3`, `dual` |  |
| P424 | Voice LEDs | Done | D-088; `tests/plugin/test_plugincore.cpp` "Engine status: LEDs of audible tracks, attacked note, followed envelope, oscilloscope"; scene `tabs` |  |
| P425 | Register monitor | Deviation | D-082, D-156, D-169; `tests/gemui/test_instruments_window.cpp` "Program Change: the track's instrument shown in the register monitor"; scenes `default`, `mode3`, `dual` |  |
| P426 | Main oscilloscope | Done | D-075, D-088; `tests/plugin/test_editor.cpp` "Processor: engine status published for the editor at 30 Hz"; scene `default` | Trigger logic untested |
| P427 | On-screen keyboard | Done | `tests/plugin/test_editor.cpp` "Editor: click on the on-screen keyboard, note played then released by the processor"; scenes `default`, `kbd_high` |  |
| P428 | Keyboard destination | Done | `gemui/test_main_controls.cpp` "Keyboard destination: the tracks in a menu under the key or pad right-clicked (P428)" | The test found the menu opening at the pointer, one space too many in its labels and the DMA tracks of 3ch on the STE read past the voice names; fixed in M8 (DMA tracks named DMA1 to DMA8, as their tabs, D-153) |
| P429 | Key lighting during playback | Deviation | D-132; `tests/gemui/test_kbd_window.cpp` "Sequencer: the notes of the playing step light the keyboard drawing (tklNotes)" |  |
| P430 | Drum pads | Done | `gemui/test_main_controls.cpp` "Drum pads: Chipdrum names, note 48 + pad at velocity 118, released on leaving; Digidrum slots (P430)"; scenes `chipdrum`, `digidrum` |  |
| P431 | Window open buttons | Done | open tests in `tests/gemui/test_kit_window.cpp`, `test_arp_window.cpp`, `test_mod_window.cpp`, `test_mixer_window.cpp`, `test_patches_window.cpp` |  |
| P432 | Note play | Done | D-087; `tests/plugin/test_editor.cpp` "Editor: click on the on-screen keyboard, note played then released by the processor" | Latch and arp paths untested |
| P433 | Mouse glissando | Done | `gemui/test_main_controls.cpp` "Mouse glissando: with the button held the note follows the pointer, released at the end (P433)"; `gemui/test_main_tracks.cpp` (P147, with Latch) |  |
| P434 | Layer / Latch | Done | `gemui/test_main_controls.cpp` "Layer: the button switches MIDI routing between Omni and Layer and silences the notes (P434, P148, D-061)"; `gemui/test_main_tracks.cpp` "Latch: keys stay held until pressed again, one mouse note at a time, released when Latch goes off (P147)" | Layer maps to the MIDI Routing parameter (D-061) |
| P435 | Octave - / + (panel) | Done | `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: octave by the buttons, Z and X, the keypad and the drawing" |  |
| P436 | Key mapping | Deviation | D-131; `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: keys according to the layout (kbdAction)" |  |
| P437 | TKL drawing | Done | D-131; scenes `kbd`, `kbd_held`, `kbd_azerty`, `kbd_high`; `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: the drawing plays with the mouse; AZERTY layout" |  |
| P438 | Numeric keypad | Done | `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: octave by the buttons, Z and X, the keypad and the drawing" |  |
| P439 | TKL keyboard octave | Done | `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: octave by the buttons, Z and X, the keypad and the drawing" |  |
| P440 | Held notes on octave change | Done | `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: notes held in Keep and in Follow" |  |
| P441 | Keyboard window | Done | D-131; `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: opening, playing only with the window open, closing that cuts everything"; scene `kbd` |  |
| P442 | Scissors cursor (Alt) | Done | `gemui/test_seq_keys.cpp` "Alt held over the grid: scissors on the notes that can be split, not on a drum track (P442)" |  |
| P443 | Note edit at cursor (keys) | Done | D-123; `tests/unit/test_ymseq_edit.cpp` [proto] cp, len, split, join | Keyboard path untested |
| P444 | Rectangle selection (keys) | Done | D-123; `tests/unit/test_ymseq_edit.cpp` [proto] rmove, rclear, rpaste | Keyboard path untested |
| P445 | Undo / Redo | Done | D-123; `tests/unit/test_ymseq.cpp` "ymseq: undo over 50 levels, identical levels skipped"; `tests/gemui/test_seq_cmd.cpp`; `test_kit_window.cpp`; `test_arp_window.cpp` |  |
| P446 | Window shortcuts | Done | Ctrl+M/B/R/P/K tests in `tests/gemui/test_mixer_window.cpp`, `test_arp_window.cpp`, `test_mod_window.cpp`, `test_patches_window.cpp`, `test_kit_window.cpp` | Ctrl+E and Ctrl+G untested |
| P447 | Effect shortcuts | Done | `tests/gemui/test_arp_window.cpp` "Undo: settings of the main window, one step per gesture on the focused track" (Ctrl+S) |  |
| P448 | Help, demo, quit keys | Deviation | `gemui/test_help_window.cpp` "Help window: ^/ and ^? open it, Esc and its close box close it"; `gemui/test_confirm.cpp` "Demo pattern and Quit confirms: Cancel by default, only OK goes on (P417)" (^Q); `gemui/test_main_controls.cpp` "Ctrl+D: the DMA generator on the STE, the demo pattern left to the File menu (D-301)"; D-079, D-125, D-067, D-301 | ^D picks the DMA generator (D-301) |
| P449 | Space bar | Deviation | D-110, D-123, D-079 | Play/Stop in Free run, inert in Host sync; host may keep the key; untested; TESTS_DAW step 13 |
| P450 | Escape | Done | D-117; Escape tests in `tests/gemui/test_arp_window.cpp`, `test_mixer_window.cpp`, `test_mod_window.cpp`, `test_kit_window.cpp`; panic in `test_kbd_window.cpp` |  |
| P451 | Digits (pattern, bank) | Done | `gemui/test_seq_keys.cpp` "Digit keys: 1-8 pick a pattern of the bank shown, Shift+1-4 a bank, never the numeric keypad (P451)"; D-123, D-131 |  |
| P452 | Arpeggio table keys | Deviation | D-129; `tests/gemui/test_arp_window.cpp` "Arpeggiator: table (grid, number of steps, chords, keyboard, playing step)" |  |
| P453 | Modulation table keys | Deviation | D-143; `tests/gemui/test_mod_window.cpp` "Modulation: keyboard, window in front and on" |  |
| P454 | Left / Right keys | Done | `gemui/test_seq_keys.cpp` "Left and Right: the grid cursor when the grid is open, else the channel, modulo the tracks (P454)"; D-079, D-123 |  |
| P455 | Up / Down in grid | Done | `gemui/test_seq_keys.cpp` "Up and Down in the grid: the cursor row, the view scrolls between 24 and 84, eight drum rows (P455)" |  |
| P456 | Toggle note at cursor | Done | `gemui/test_seq_keys.cpp` "Enter toggles the note at the cursor with a preview; Shift+Space stays Play / Stop (P456)"; D-123 | Shift+Space is Play/Stop, as the prototype in practice (its note toggle on Shift+Space never runs) |
| P457 | Clear a step | Deviation | D-194; `gemui/test_seq_keys.cpp` "Delete and Backspace clear the step at the cursor, every row and chord voice, one undo step (P457)" | Chord voices are removed; the prototype wrote 0 in them, a C in each chord voice of the cleared step |
| P458 | Octave on physical keyboard | Done | D-131; `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: octave by the buttons, Z and X, the keypad and the drawing" |  |
| P459 | Play on physical keyboard | Deviation | D-131; `tests/gemui/test_kbd_window.cpp` "Computer Keyboard: opening, playing only with the window open, closing that cuts everything" | Only while the keyboard window is open |
| P460 | Web MIDI initialisation | Deviation | D-061, D-074 | Host MIDI; JUCE devices in Standalone; no test; TESTS_DAW step 2 |
| P461 | Incoming MIDI messages | Deviation | D-061, D-099, D-146; `tests/plugin/test_plugincore.cpp` (Start, Stop, Continue, SPP, pitch bend, notes); `tests/plugin/test_processor.cpp` |  |
| P462 | MIDI learn | Deviation | D-146; `tests/gemui/test_midi_learn.cpp`; `tests/unit/test_midilearn.cpp`; `tests/plugin/test_editor.cpp`; scenes `learn`, `learn_bound` | TESTS_DAW step 15 |
| P463 | Default CCs | Done | D-061, D-113; `tests/plugin/test_plugincore.cpp` "Driver: controllers of the prototype → parameters, host notified", "CC 74", "CC 83", "CC 82" |  |
| P464 | Direct value entry | Done | `tests/gemui/test_arp_window.cpp` (arpRatev, arpLenv); `tests/gemui/test_mod_window.cpp` (ornLenv); `tests/gemui/test_kit_window.cpp` |  |
| P465 | Envelope frequency entry | Deviation | D-010, D-073; `tests/unit/test_frequency.cpp` "S6 envelope: clk / (256·EP) as a sawtooth, clk / (512·EP) as a triangle"; `gemui/test_main_controls.cpp` "Envelope: EP typed in decimal or $hex, frequency typed in Hz, clamped to 1..65535 (P465, P466)" | Frequency from the period without the prototype's x32 factor (D-010) |
| P466 | Direct EP entry | Done | D-073; scene `envelope`; `gemui/test_main_controls.cpp` "Envelope: EP typed in decimal or $hex, frequency typed in Hz, clamped to 1..65535 (P465, P466)" |  |
