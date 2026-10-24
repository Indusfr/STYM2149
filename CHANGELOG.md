# Changelog

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).

STYM2149 is built in milestones (M0 to M8, see `STYM2149_SPEC.md` §11). Each milestone has a report under `docs/` and its decisions in `DECISIONS.md` (D-nnn). Version 1.0.0 is kept for the complete release: Windows, macOS, Linux and AAX (D-179).

## [0.9.0] — unreleased (M8: quality, installers, documentation)

### Changed
- Engine: the worst case of the CPU benchmark (96 voices, noise on every track) drops from 2.93 % to 2.51 % of one core on the reference machine, with output identical to the bit on 25 benchmark scenes (D-180). The virtual YM channel runs one loop per combination of audible tone, noise and envelope; chip and DMA voices write to the buses from local variables; the oscilloscope buckets come from a table built once per block.
- Editor: the desktop repaints its dirty zones separately, blends faster and keeps an image already scaled to the window. A full frame of the editor image drops from 8.0 to 3.6 ms at 100 %, and a full JUCE paint at 200 % from 27 to 8.4 ms (A2).
- Version 0.9.0 (D-179).
- Editor: the desktop is drawn with juce::Graphics at the screen's resolution instead of being enlarged by the system: on a display at 125 or 150 %, columns of pixels no longer have irregular widths, borders keep one thickness and text is smoothed as in the browser; at whole scales the drawing is unchanged (D-201).
- Editor: each zoom gives half its former size; 200 % is the former 100 % (D-202).
- Editor: each zoom is 25 % larger again: 100 % shows the desktop at 5/8 of its size (795 × 590), 160 % at its size, 200 % at 1590 × 1180 (D-216).
- Options: Audio output and MIDI input become one entry, Audio/MIDI settings, since the standalone application sets both in one window (D-217).
- Edit > Drum kit and Ctrl+K are greyed out unless the focused track is Chipdrum (D-219).
- When the ADSR does not apply (Sync Buzzer in Chip mode, Chipdrum and Digidrum tracks), the ADSR and ADSR ENVELOPE frames are greyed out whole, frame lines and titles included; the buzzer's hint reads "envelope only" and stays inside its frame (D-220).
- The Shape R13 menu lists the shapes 8 to F; 0 to 3 sound as 9 and 4 to 7 as F, and such a value from a YM import or a MIDI CC is kept, the menu marking the shape heard (D-221).
- The captions of the Digidrum and DMA sample displays ("Slot 1: empty...", "Sample 1: empty...") use the 6×6 GEM font at its own size, on a white band, instead of being scaled down with the waveform (D-222).
- GENERATOR frame: a Sync Buzzer track shows the Buzzer window's Tuning, N, D, Fine, YM Pad and Presets (D-227); a SID Voice track has a Timer wave button that opens the Timer wave window (D-228).
- The main window moves by its title bar like the floating windows, and keeps its place while the plugin instance lives (D-224).
- GENERATOR frame: a Digidrum samples button under slots 5 to 8 opens the Digidrums window on the track's slot, and the frame's waveform no longer opens a file when clicked; the DMA samples button moves under samples 5 to 8, and the DMA frame shows the waveform of the selected sample (D-213).
- Window titles put a space before the dash: "STEP SEQUENCER - 1", "DMA - STE" (D-212).
- Chipdrum and Digidrum tracks: the ADSR sliders, its Mode button and its curve are greyed out in every voice mode ("drums: off"), and the engine ignores the ADSR of a drum track: each hit plays to its end with its own shape (D-205).
- Options > Sound engine: with Computer ST, the STE output chain, STE bass and STE treble are greyed out, with the reason in their tooltip; a value chosen before is kept (D-206).
- Options > Sound engine: in Authentic 3ch, the ZX Spectrum and Amstrad CPC chip clocks and the Free timer are greyed out; a value chosen before is kept. The CHIP monitor lists a non-Atari clock, and a Free timer used by a timer effect, in every voice mode (D-207).
- Step Sequencer: Ghost (the other tracks' notes behind the grid) is on when the plugin starts; the prototype started with it off. The button still switches it for as long as the plugin lives (D-209).
- Authentic 3ch and Dual chip 2x3ch: the track level and the velocity lower the 4-bit volume to the level nearest in decibels, as the audio gain does in 16 channels, instead of scaling the volume index. At the default settings a note was 10.6 dB quieter than in 16 channels; the modes now differ by 1.6 dB at most (D-200).
- The code comments, test names, tool and CI messages, `DECISIONS.md` and the milestone reports are in English; the README and the manual stay in French and in English (D-178, D-198).
- The Clock selector (Internal, MIDI ext) sets the sequencer's clock as well as the arpeggiator's, as in the prototype: on MIDI ext the sequencer follows the MIDI clock and the BPM slider is greyed (D-188).
- Confirmations before something that cannot be undone (Clear track, Demo pattern, Quit, Recorder Clear, Digidrum banks) show Cancel first, framed as the default; Enter and Esc cancel. Text boxes show Cancel then OK, and Enter means OK (P417, D-191, D-192).
- The editor's preferences move to `Preferences.settings` in the STYM2149 folder; the keys of an older `STYM2149.settings` are copied once (D-197).
- Sync Buzzer presets keep the Sync Buzzer: eight new presets set the shape, the tuning (N / D) and YM Pad, without switching the track to Chip Voice (D-230).
- The ENVELOPE frame greys out its EP = and Hz labels too when the envelope is tuned; the Tuning button reads On or Off (D-232, D-233).
- Edit > Samples > DMA (STE) is greyed out with Computer ST (D-234).
- Fuji > Information shows the STYM2149 icon above the text, and its OK button has the bold border of the default button, since Enter closes the box (D-235, D-236).
- Options > Sound engine > Chip clock: the 2 MHz entry becomes Custom, whose frequency (100 kHz to 8 MHz, 2 MHz by default) is typed in a dialog box; it plays in 16 ch only, 2 MHz in the other modes. New host parameter Chip Clock Hz, not automatable: 439 parameters (D-237).
- Edit menu: Timer wave and Buzzer leave it; the Buzzer window is removed, its settings being in the GENERATOR frame of a Sync Buzzer track, which also gets a Timer wave button (D-238).
- Options > Sound engine > Chip clock: the Custom frequency is typed and shown in MHz with four decimals, 0.1000 to 8.0000 MHz (D-239).
- The Sequences window becomes part of the Modulation window: its rows (PER, NP, MIX, GEN, FIX, OUT) below the table while it is on, its settings below them; one On, one length and one Clear for both. Edit > Sequences and Ctrl+L are removed (D-240, D-241).
- Step Sequencer: the Tempo field is named BPM (D-244).
- Drum kit: the TR-606 and TR-808 kits are named TR-606-ish and TR-808-ish, like TR-909-ish (D-245).
- The Mixer, Arpeggiator, Modulation, Step Sequencer and Patches buttons are centred in the main window (D-247).
- Author and copyright: "Programming: Pascal "Indus" Nowak (with Claude's help)" and "Copyright © 2026 Indus / PRISM" in the Information window, the source headers, the plugin metadata and the Windows installer (D-248).
- XM import in BPM timing: a tempo change within a pattern becomes a Txx command at its line instead of moving to the next Song entry (D-243).
- Main window: VIBRATO under SOURCE, then ADSR and its curve under ENVELOPE (the curve 74 px high), GENERATOR over the whole height; the Shape R13 row moves up; the line of keyboard shortcuts is gone and the window is 32 px shorter (D-268, D-255, D-267).
- GENERATOR: a SID Voice shows its SID wave (16 bars, Steps, Square and Sine) and a Sync Buzzer its 8 steps; the Timer Wave buttons and window are gone; YM Pad and Presets are centred (D-250 to D-252, D-278, D-259).
- SOURCE: what the sound does not use is greyed with its reason in the tooltip: Noise period without noise, Source, Noise, Volume and Mode on a Sync Buzzer, Mode and the Env sources on a SID Voice (D-253, D-261, D-262).
- Modulation window: the sequence rows sit inside the table, between its pitch rows and NSE / VOL, and their notes join the table's at the top; a right click on PER or NP opens a slider, as on VOL, and a right click on that slider sets the value back (D-270 to D-273).
- Mixer in 3 channels: the fader of a chip channel is its 4-bit Volume, the same as in the SOURCE frame (D-260).
- Slide Curve: Invert turns the whole curve upside down (D-263).
- Options > Tooltips is a submenu: On, Off, then Language and its six languages (D-265).
- File > New stops the sequencer and disarms Rec (D-249).
- Chip clock: the Atari entry is named "Atari ST" (D-258).
- Windows installer: the standalone application goes to `C:\Program Files\PRISM\STYM2149`; an update removes the former `C:\Program Files\Indus` folder (D-269).
- Arpeggiator: the divisions are in a menu button; Tick (one MIDI clock pulse) is offered on the MIDI clock only, since with Sync it was the rate of the arpeggiator without Sync (D-287, D-290).
- LFO: the Shape button and its menu show a drawing of each shape instead of its name (D-291).
- Generator: a right click on a step of the SID wave opens its slider, as in the Modulation window (D-285).
- Engine: a Digidrum in 16 channels plays at the level of the same hit in 3 channels (it was 10 to 13 dB louder, its gain of 5 compensating the DAC a second time), and the 16-channel Chipdrum sample is 2.9 dB lower, at the level of the 3-channel register drums; 16-channel projects with drums play lower (D-286).
- LFO: the Triangle drawing comes to a point and the Sine one is round, each on two cycles like the other shapes (D-292, D-319).
- Step Sequencer: the notes scroll with a scroll bar at the right edge of the window, as high as the grid, with an arrow at each end (a semitone), a click beside the thumb moving by an octave (D-294, D-309).
- Arpeggiator: Division sits on the row of the mode (D-295).
- ENVELOPE: the frequency value moves to the row of EP =, and the frequency slider runs over its whole row (D-296).
- Modulation window: an LFO set to Off shows only its label and its button (D-298).
- Ctrl+D picks the DMA generator, like Ctrl+V, S, U, C and I for the other generators; File > Demo pattern keeps its entry, without a shortcut (D-301).
- Desktop: the menu bar runs across the top of the desktop, as on a GEM desktop; the main window starts below it (D-302).
- Main window: Vibrato's Depth and Rate, and Source's Volume and Level, have sliders of one width, as in ADSR; ADSR shows A, D, S and R with wider sliders; the register monitor names CHIP VOICE (D-304 to D-307).
- Mixer: the value under a pan reads "Pan +25", like "Send 50" (D-310).
- Step Sequencer: the CMD row of the step commands is shown by default (D-318).
- LFO: with Sync, the divisions are in a menu button instead of the rate slider; a target that the track cannot move is greyed out in the To menu with its reason (D-320, D-321).
- Tooltip languages: the ADSR tips name each setting in the language, then in English in parentheses (D-307).
- Arpeggiator: the table grid shows the offsets +0 to +24, the reach of the chords (Double octave), instead of +0 to +15; the window is 9 rows taller (D-322).
- Patches: a name has no length limit, as in the prototype (the field of the prompt shows its end); a name wider than the window is cut with "..." on its button, the tooltip giving it whole (D-326).
- Pitch bend during a slide: the voice stays at its place in the slide, bent, instead of jumping to the bent target note until the next slide step; the timer of SID Voice and Sync Buzzer frames follows the bend (D-328).
- Step Sequencer: a step holds one command of each kind (arpeggio, vibrato, BPM, envelope restart, ticks per line, sync mark), played together; its CMD cell shows ARP, VIB, BPM, ENV, TIK or SYN, MUL for several. A click opens the Commands box, a row per command with its value and an On/Off button; Cancel is the default button; in a plugin the BPM row is greyed out, the DAW setting the tempo. Saved projects load each command into its row; an XM speed or tempo change now shares a line with an arpeggio (D-329, D-330, D-336).
- Step Sequencer: nine more step commands in the Commands box, each made of writes per tick that a real ST plays, so the register exports carry them: volume slide `Axy`, pitch slide `Pxy`, note delay `Dxx`, note cut `Cxx`, buzzer slide `Bxy` (envelope period), noise slide `Nxy`, envelope shape `Exx`, DMA sample offset `Oxx` and fine tune `Fxx` (cells VSL, PSL, DLY, CUT, BSL, NSL, SHP, OFS, FIN; state rows 38 to 46, JSON keys `cmdA` to `cmdF`). The XM import turns `Axy`, `1xx`, `2xx`, `9xx`, `EDx`, `ECx` and `E5x` into them; a looped DMA sample started at an offset loops back to that offset, in the engine and the SNDH export (D-338).
- Editor: at a scale that is not whole, Options > Pixels > Sharp draws the letters and icons on whole screen pixels without anti-aliasing; Smooth, the default again, keeps the former rendering (D-331, D-339).
- Desktop: the floppy disks A and B sit side by side at the top left, the trash at the bottom right (D-332).
- Plugin vendor: PRISM alone in VST3, CLAP, LV2, AU, AAX and the Windows installer; the plugin IDs and the manufacturer code do not change (D-334).
- Options: no Zoom entry; the size follows the window, resized in the host or by the system, from 50 to 200 %, and is remembered (D-335).
- Step Sequencer: the arpeggio `Xyz` and pitch slide `Pxy` commands are retired, the ARP and SLD rows doing their work; a value read for them (saved project, pattern file, state) is dropped, and the XM import leaves `0xy`, `1xx` and `2xx` out (D-340, D-344).
- Step Sequencer: vibrato, BPM, envelope shape, note delay and note cut leave the Commands box for rows of their own: VIB (No change, On, Off) while the track's vibrato is on, BPM under CMD in the standalone application only, ENV (the shapes 8 to F) when the envelope plays (Env source, Sync Buzzer, an Env entry of SRC or a Sync Buzzer step of GEN), DLY and CUT (a tick of the line, None). They follow the Resolution menu's "Step command rows" preference, formerly "CMD row", and stay shown while one of their commands is written. The Commands box keeps eight commands; Envelope Restart shows as RST (D-341, D-342, D-347 to D-349).
- Step Sequencer: VOL, RTG and ARP act on every step of a held note, as SRC, GEN, PAN and CRV did: the volume follows the VOL row step by step, RTG strikes again from the start of its step, ARP starts or resumes the arpeggio there; a GAT on the last step of a held note releases it there. The MIDI and XM imports write the note's volume on its held steps (D-343, D-345).
- Step Sequencer: ARP becomes a switch like VIB: No change, On (`J01`) or Off (`J00`) from its step on; each pattern starts with the arpeggiator off, and the ARP flags of older projects and pattern files become switches that play the same. New LFO (`Lxx`) and MOD (`Mxx`) rows switch the track's LFO and modulation table the same way; each pattern starts with them as set in the Modulation window (D-351 to D-353).
- Step Sequencer: envelope restart, sample offset and fine tune leave the Commands box, which keeps ticks per line, sync mark and the three slides: ENV gets a Restart submenu (None, At once, At tick...) beside the shapes, its cell showing the shape and R; SMP (DMA tracks only, 0 to 255 steps of 256 frames) and TUN (−99 to +99 cents) are rows of their own (D-355, D-361, D-362).
- Step Sequencer: the BPM row moves under VOL, behind a separator, and becomes one row for every track of the pattern: a tempo written on one track shows on all of them. It is saved with the pattern (JSON key `bpm`); the `Txx` of older projects and files move into it (D-359).
- Step Sequencer: a value written where it cannot act (vibrato, LFO, arpeggiator or modulation table off, a drum track, no envelope playing, not a DMA track, no note starting on the step, no slide running, BPM in Ticks timing or on an external clock) is greyed and crossed; its tooltip says why (D-354).
- Step Sequencer: the edge of a note resizes it with the Move tool too, with the resize pointer (D-350); a drag from a selected note moves the whole selection (D-356).
- Step Sequencer: with the Overview shown, only its "Whole pattern" line stays under the grid (D-360).
- Options > Sound engine: no Arp division entry; the Division button of the Arpeggiator window sets it (D-357).
- Main window: in SOURCE, the Source, Voicing and Mode buttons start with the Volume and Level sliders and the Noise period slider ends with them; in VIBRATO, the Depth and Rate sliders are wider, both as wide (D-367 to D-369).
- Sources: English comments for a reader new to the code (file overviews, the role, owner, thread and units of the types and functions, the subtle blocks of the emulation, the state formats, the window layouts and the undo); the code itself is unchanged (D-308).

### Added
- An LFO per track in the Modulation window: Triangle, Sine, Square, Saw up, Saw down or Random, in Hz (0.1 to 20) or one cycle per division of the tempo, on the volume, the noise period, the envelope period, the SID ratio, the pitch or the pan (16 ch); restarted at each note, or free with Retrig Off (D-279). It writes register values only, so it plays in Authentic 3ch and in the exports; it is kept by the project, the patches and the instruments (D-257).
- Arpeggiator: a Sync button follows the tempo, the host's in a DAW (on its grid while it plays) or the sequencer's, one step per Division; host parameter "Arp Sync" (D-256).
- VIBRATO: an On/Off button per track (host parameter "Vibrato On"), and a `Vxx` command in the CMD row: V00 holds the vibrato from its line, V01 to VFF bring it back (D-254).
- Mixer: a Master fader after the tracks, the main level of the SYSTEM frame (D-276).
- Step Sequencer: the SRC row adds Env, Tone+Env, Noise+Env and Tone+Noise+Env (D-277).
- Every number next to a slider can be typed: click it, type, Enter, in the GENERATOR frame, the Digidrum and DMA windows, the Modulation window and its sliders (D-264, D-274, D-275).
- CMD row: `Txx` sets the tempo to xx BPM ($20 to $FF) from its line on, in BPM timing with the sequencer's own clock and in the exports (D-243).
- SOURCE: the Source menu adds Env, Tone+Env, Noise+Env and Tone+Noise+Env, which also set Mode; Env alone plays the envelope's own waveform (D-246).
- Computer Keyboard: on a Chipdrum or Digidrum track, the middle row of keys (A to K by position) plays the 8 pads, and the drawing names them (D-229).
- File > New (Ctrl+N): back to a new project, after a confirmation if anything is already defined (D-225).
- Application icon: a YM2149 DIP package with "YM" in the GEM font, for the standalone application and the Windows installer; on Linux, `install.sh` adds a menu entry with the icon (D-218).
- Build number: Fuji > Information shows the number of the CI run that compiled the plugin and its commit after the version (`0.9.0 build 412 (fe71c0d)`, `build dev` for a local build); the Windows installer and the Linux archive carry it in their names (D-211).
- `ymbench --hash` (FNV-1a fingerprint of the rendered output) and `--tracks` (separate track and chip outputs).
- DAW test protocol (`docs/m8/TESTS_DAW.md`) for FL Studio 26, Ableton Live 11, Cubase Pro 15, ACID Pro 10, Maschine 3 and Reason 13 on Windows (D-177).
- CI: on Linux, pluginval runs under gdb when the runner has it, to print every thread's stack if it crashes.
- Tooltips in English, French, German, Spanish, Portuguese and Italian, chosen in Options > Tooltip language; by default the system language when it is one of the six, otherwise English (D-185). The GEM font gains the accented letters of the Atari ST character set, from EmuTOS. Menus and labels stay in English. The translations still need a review by native speakers.
- Windows installer for all users, with administrator rights: VST3 and CLAP in `C:\Program Files\Common Files`, the standalone application in `C:\Program Files\Indus` (Inno Setup, D-181, D-199), and a Linux archive with `install.sh` for the home folder (D-182). The CI builds both on every run.
- CI: AddressSanitizer and UndefinedBehaviorSanitizer on the core and interface tests, ThreadSanitizer on the plugin tests, the Steinberg VST3 validator on Linux and Windows, and the CPU worst case of `ymbench` in the Linux job summary (D-183, D-184).
- SHORTCUTS window of the prototype (Ctrl+/, Ctrl+?, Fuji menu > Shortcuts): its nine sections, then the keys STYM2149 adds (D-189).
- Options > Sound engine > Quantize to VBL (D-196).
- SNDH export: `Zxx` commands write xx into a sync byte of the replay routine, whose address the init routine returns in A0, for demos (D-195).
- GEM menus can flow into columns, as the prototype's Chord... menu of the Arpeggiator (P400, D-190).
- Tab and Shift+Tab move between the buttons of a dialog box (D-191).
- Licence texts of every component shipped with the packages, and the libraries that JUCE compiles into the plugin listed in `THIRD_PARTY.md` (D-186).
- Written check that STYM2149 makes no network access (`docs/m8/NETWORK_AUDIT.md`); the internal "telemetry" between engine and editor is renamed engine status.
- Parity with the prototype, row by row (`docs/m8/PARITY.md`); user manual in English and French (`docs/MANUAL.md`, `docs/MANUAL.fr.md`); README in English and French; architecture guide (`docs/ARCHITECTURE.md`).
- Mixer: with a stereo output (16 channels, 3 channels with the STE), Master has a left and a right fader, a balance slider and a Link button; in 2x3 channels, a fader per chip and Link; new host parameters Master 2, Master Pan and Master Link (D-284).
- Arpeggiator: 1/32T and 1/64 divisions (D-289).
- Step Sequencer: a right click on the metronome button opens its volume, kept with the project (D-288).
- Tooltip languages in files: English is the only language built in; French, German, Spanish, Portuguese and Italian are gettext PO files (`languages/`) that the installers put in a folder of the computer (`C:\ProgramData\STYM2149\Languages`, `~/.local/share/STYM2149/Languages`). The PO files of the Languages folder next to the preferences add languages to Options > Tooltips > Language or correct the installed ones; Languages folder... writes a template and opens the folder (D-283).
- Every horizontal slider but the Mixer's pans has an arrow at each end: a press moves the value by one step, repeated while held (D-297).
- Mixer: each fader, pan and Master shows its value; a right click on one opens Value... to type it and Reset (D-300).
- Desktop: decorative icons of floppy disks A and B and of the trash, named in the tooltip language, moved by a held press (D-303).
- LFO: 1/32T and 1/64 divisions (D-320).
- Effects window (Effects button, Edit menu, Ctrl+F): an echo of the notes of each sound in every mode, played by the chip, so Authentic 3ch, the Recorder and the register exports keep it (Division, To, Repeats, Drop); an audio delay in 16 ch only, fed by a new Send of each track in the Mixer (Sync or Time, Feedback, Hi cut, Level, Ping-pong). Nine new host parameters, version 6; ymbench `--echo` (D-299, D-311 to D-313). A Sync Buzzer gets no echo in the 3-channel modes, where a repeat would restart the shared hardware envelope (D-316); Ping-pong shows while the delay is on (D-315).
- Options > Oscilloscope: shows or hides the oscilloscope of the main window; hidden, the register monitor and the keyboard move up and the window is 100 px shorter. A preference (D-358).
- Euclidean generator: a Rhythms menu of the 23 named rhythms (Tresillo, Bossa-Nova, Brazilian samba...), sorted by length and greyed when longer than the track, sets Steps and Pulses (D-366); Apply asks before it replaces or removes notes already written (D-364).

### Fixed
- Step Sequencer piano roll: 27 editing bugs, among them a click that shortened the next note instead of removing a note, drags over held notes that cut them, moved notes that left their accent and slide behind, the right click in a selection box, boxes and < > that erased notes, +1 under Snap that left notes in place, and Redo lost after a click that changed nothing (D-282).
- Engine: in 3 channels, a Chipdrum hit follows the track level and the velocity (D-286).
- Sequencer: overlapping notes of one track all sound to their end and all light the keyboard. A chord voice written under a held note no longer cuts an earlier voice of the same rank, and the piano and the keyboard drawing show every note that sounds at the played step, not only the notes starting there (D-281).
- Step Sequencer: with a scale and Snap on, no note lands on a greyed row any more. A chord voice added to a step that already holds a note, the voices of the input chord, a dragged note or voice and the chord voices of a transposed selection go to the nearest degree, as the root already did; a press on a greyed row whose degree already sounds at that step changes nothing (D-280).
- GEM font: the 8×16 glyphs are back at their place in the EmuTOS cell, so i, I and l are centred; the prototype's TrueType font had moved 65 of them one column to the left (D-266).
- Engine: the tuned envelope and YM Pad of a Sync Buzzer no longer change a Chip Voice: set on a track that later plays Chip Voice (its generator, a GEN step or an instrument), they made its envelope follow the note (D-231).
- The metronome no longer shows on the oscilloscope or the Mixer's output meters (D-226).
- Step Sequencer: the bar line of step 1 no longer widens the note-name column over the first column (D-223).
- Options > Keyboard layout > Auto follows the system's keyboard layout (AZERTY, QWERTZ, otherwise QWERTY) on Windows and Linux, instead of always QWERTY; macOS is TO CONFIRM (D-215).
- Editor: oblique lines, such as the ADSR curve in Smooth mode, are drawn anti-aliased at every zoom and display scale instead of in steps (D-214).
- Engine: an arpeggiated note (ARP row of the sequencer or keyboard arpeggio) keeps its Modulation table, vibrato and frame cadence from one note of the arpeggio to the next. At the default rate of 50 Hz the table no longer stays on its first step, so ARP steps (steps 1 and 5 of the Demo pattern) now play their modulation (D-203).
- Sequencer: switching a track off during playback (its tab, All off, or the host automating its On parameter) releases its notes at once, as in the prototype. A note held by TIE or SLD, chord voices of several steps, RTG retriggers and the track arpeggio used to ring on until Stop: with the Demo pattern in Authentic 3ch, the tied Sync Buzzer of track A or B kept droning at full envelope level over the notes of the track left on (D-208).
- Sequencer: in Authentic 3ch, Dual chip 2x3ch or on a monophonic track, the chord voices the grid greys out no longer light the drum pads, the piano or the keyboard drawing during playback (D-204).
- GEM menus: a greyed-out entry no longer opens its submenu on hover.
- Windows installer: the per-user installation of earlier test builds is uninstalled first. Its application stayed in `%LOCALAPPDATA%\Programs\STYM2149` with its Start menu shortcut and desktop icon, which kept starting the old application after an update (D-210).
- CHIP monitor: the ADSR of a Sync Buzzer is no longer listed in Authentic 3ch, where it is inert.
- Desktop: destroying the root widget first no longer leaves a dangling pointer (found by AddressSanitizer).
- Plugin tests: allocations are counted on the audio thread only, so that another thread allocating meanwhile is not blamed on `processBlock` (found under ThreadSanitizer).
- Standalone app on Windows: every save by the app put back the editor preferences it had read at launch, because both used the same file (D-197).
- Standalone app: File > Quit now keeps the session like the window's close box (D-197).
- The SID timer ratio's tooltip described a Digidrum rate (P033); five tooltips were translated word by word through a generic line.
- The Digidrum frame of the main window always printed 7800 Hz; it prints the slot's rate (P270).
- Step Sequencer: the overview map now follows playback, the scroll bar and the transpose slider (P313).
- Step Sequencer: Enter in the Clear track confirmation no longer clears the track (P417).
- Keyboard destination menu (right click on the keyboard or the pads): it opens under the key or pad clicked, as in the prototype, not at the pointer; in 3ch and 2x3ch on the STE it names the DMA tracks DMA1 to DMA8 instead of reading past the tables of voice names (P428).
- CI: pluginval no longer crashes after its tests on Linux, built with the JUCE fix of the VST3 host run loop (D-187).
- Plugin: the message thread no longer reads the engine that the audio thread replaces when the clock, the output chain or the resampling changes (data race found by ThreadSanitizer).
- Step Sequencer: with the Move tool, a click on a note no longer removes it or cuts it; it places the cursor (D-293).
- ADSR ENVELOPE: with the echo on, the cursor of the curve no longer goes back to zero at each repeat (D-314).
- MIDI export: in 16 channels, the arrival note of a slide takes its level instead of velocity 1.
- Voices menu: a change of voice mode keeps the focus on a DMA track of the STE that the new mode still has.
- Closing a menu no longer compares an invalidated iterator (undefined behaviour, stopped by checked standard libraries); the track oscilloscope no longer reads past a single point.
- Track names: a name cut to 8 characters no longer keeps a space at its end (D-323).
- Step Sequencer: a drag that resizes or moves a note and that the lane refuses no longer leaves an empty undo step that cleared Redo (D-325).
- Patches: Save current, Rename, Overwrite and Delete read the shared library file again first, so that a change made by another instance is kept, and act only on the patch the menu was opened on (D-326).
- SNDH export: 99 subsongs at most, the reach of the two digits of the `##` tag (D-327).
- Colour blend: an opacity outside 0 to 1 takes the nearest bound; Euclidean generator: 2 to 32 steps, like its slider; code without effect removed (D-324).
- Desktop: a dragged icon no longer leaves dots of its outline below 100 % of real scale (D-337).
- Euclidean generator: picking the note in its menu closed the box and lost the pick (D-363).
- Track length box: its field and slider show the length the track plays, its own or the pattern's (D-365).
- Code audit (several review passes over every module, and 40 minutes of fuzzing of the file readers with no crash):
  - Resampler: with a chip clock of 320 kHz or less (a Custom clock from 100 kHz in 16 channels, a YM or SNDH tune with such a clock), the Sinc output no longer comes out about 10^5 times too loud; clocks of 400 kHz and above are unchanged.
  - Song: after Song entries were deleted during a repeated entry, a pattern picked by hand no longer reads past the end of the Song on the audio thread.
  - Plugin: a host that prepares at 0 Hz no longer crashes on a division by zero; blocks processed before the first preparation are silent instead of crashing; the latency report no longer hangs a host that calls back into the plugin; a block of 0 samples no longer drops the notes and transport commands of the editor.
  - YM Player: a change of output chain or resampling made while the host is suspended goes on from the tune's position instead of starting it again.
  - SNDH export: the SID interrupt gives D0 back whole to the program it interrupts (its high word was cleared, which can crash a real ST or Hatari).
  - Digidrums: Load and Delete act on the bank named in the confirmation, even when another instance changed the bank library meanwhile.
  - Patches: after Export, the patch buttons follow the library read again; Delete or Overwrite could reach the next patch.
  - Song and Step Sequencer: a key that changes the Song, the pattern or the focused track closes an open menu or value popup, which used to act on the old entry, track or pattern (a read past the end of the Song, a Euclidean figure written into another kind of track). When the host changes the focused track or the pattern under an open popup, the Step Sequencer's value popups, the Euclidean box (its confirmation included), the Modulation table's VOL slider and the PER and NP sliders write nothing instead of editing another track.
  - A slider, scroll bar, keyboard key, pad or key of the Computer Keyboard drawing held when an alert opens or its window closes ends its gesture, its repeat or its note; a press on the Step Sequencer grid cut short that way no longer removes or cuts the note at the next press, and a dragged Song block no longer stays faded. A value or text field hidden while being typed into cancels its input; a value typed in a popup counts when a press outside closes it; a new range while typing ends the input instead of writing outside the field.
  - Desktop icons: a number too large in the settings file is refused; the desktop frees an open submenu before its menu when the editor closes.
- Second audit pass, by running the code (half an hour of coverage-guided fuzzing of the engine's operations, an hour of random save and load round trips, an hour of random input on the editor, five static analysers):
  - Keyboard arpeggio: with per-channel MIDI routing, a key released after another track was focused stops the arpeggio of the track it started on; it used to go on, or start again when that track was focused again.
  - MIDI learn: a note that was sounding when its key became learned is still released by its note off instead of ringing until a panic.
  - File > Load > Preset of a prototype preset: the current pattern gets the same migrations as the others (its step arpeggios play and show at once; a BPM row is kept).
  - Step Sequencer: a note pressed or dragged when Song playback moves on to the next pattern, or when the host focuses another track, no longer writes into that pattern or track; the CMD box and the menus of the grid cells write only on the step they were opened on.
  - Song window: a block menu left open across Ctrl+Z or Ctrl+Y, and the Tempo, Length and Loop back prompts under a Song set by the host, act on nothing instead of on other blocks.
  - The menus that set the focused track (Generator, Source, Shape, Arpeggiator, Effects, LFO, sequences, Instruments, Patches and others) write nothing once the host has focused another track.
  - Values typed with decimals (Env freq, SID ratio, Vibrato rate, BPM, LFO rate and the others) and the values shown no longer depend on the C locale of the host: with a decimal comma, the fractions were lost and a field touched without typing changed its value.

### Not done in this version
- macOS (AU, auval, universal binary, pkg) and AAX delivery (D-175). Installers are not signed (D-176).

## M7 — `ymio`, import and export (merged 16/10/2026, PR #14)

- Whole File menu of the prototype: JSON preset, pattern, Song, drum kit and Patches library files, readable by the prototype (D-165, D-168, D-174).
- YM Player: YM2 to YM6 (LHA or plain) and SNDH, played by AtariAudio's Atari machine, bit-identical to AtariAudio on 12 tunes (D-159, D-169).
- MIDI: SMF 0 and 1 import, SMF 1 export of what the sequencer plays (D-170).
- Audio export: WAV 16-bit, 24-bit and 32-bit float at any rate, and stems in a ZIP (D-164, D-171).
- Register export: VBL, YM3, YM6 (LHA, with timer effects), SNDH v2 for ST or STE, and the recorder's take (D-160 to D-163, D-172).
- XM 1.04 import with samples, played by the STE DMA sound (D-166, D-173).

## M6 — maxYMiser, TTrak and YM-Tracker features (merged 16/10/2026, PR #13)

- Modulation, Drum Kit and Patches windows; MIDI learn (D-143 to D-146).
- 32 instruments played by an INS row and Program Change; instrument sequences per tick (D-147, D-148, D-156).
- Replay tick at 100 and 200 Hz, Ticks timing with groove; `Xyz`, `Sxx`, `Rxx`, `Zxx` commands in a CMD row (D-149, D-150).
- Programmable SID wave, Sine-SID, Sync Buzzer steps, three timers per chip; tuned buzzer, fine tuning, YM Pad and presets (D-151, D-152).
- ST/STE machine and the STE DMA sound on 8 tracks; Digidrum editor; register recorder; extended Song with rests, note-offs, jumps and restart point (D-153 to D-155, D-157, D-158).

## M5 — sequencer (merged 16/10/2026, PR #12)

- The prototype's step sequencer in the engine: 32 patterns of 1 to 256 steps on 16 tracks, chord voices, arpeggios, slides, retriggers, conditions, step effects, global gate, Song, metronome, Quantize to VBL (D-090 to D-134).
- Free run, MIDI clock and Host sync clocks (Host sync by default in a DAW, D-110, D-114).
- Step Sequencer, Song, Mixer, Arpeggiator, Slide Curve and Computer Keyboard windows.

## M4 — GEM interface (merged 15/10/2026, PR #10)

- The prototype's main window drawn by `gemui` in a 1016 × 944 GEM desktop, scaled with nearest-neighbour from 50 to 200 % (D-067 to D-089).
- Visual regression test against the prototype's captures.

## M3 — minimal plugin (merged 13/10/2026, PR #6)

- VST3, CLAP, LV2 (Linux), AU (macOS), AAX (compiled only) and Standalone; 417 parameters with stable IDs; separate track and chip outputs; versioned state (D-057 to D-066).
- pluginval level 10 (VST3, LV2) and clap-validator (CLAP) in the CI.

## M2 — `ymengine` (merged 13/10/2026, PR #2)

- Voice engine in three modes (16 voices, Authentic 3ch, Dual chip 2x3ch) with MFP timers, SID, Sync Buzzer, Digidrum, Chipdrum, ADSR, vibrato, ornaments, arpeggio, slides and legato; ST and STE output chains (D-035 to D-054).

## M1 — `ymcore` (merged 13/10/2026, PR #1)

- YM2149 core following AtariAudio 1.26 step by step (thresholds S1 to S11 met), RawBox mode bit-identical to AtariAudio's player, LHA decompression ported from pylzh (D-042, D-043).

## M0 — audit

- DSP audit, parity matrix of the prototype, gap tables against maxYMiser, TTrak and YM-Tracker.
