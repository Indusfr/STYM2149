# Changelog

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).

Version 1.0.0 is kept for the complete release: Windows, macOS, Linux and AAX.

## [0.9.0] — unreleased (quality, installers, documentation)

### Changed
- Engine: the worst case of the CPU benchmark (96 voices, noise on every track) drops from 2.93 % to 2.51 % of one core on the reference machine, with output identical to the bit on 25 benchmark scenes. The virtual YM channel runs one loop per combination of audible tone, noise and envelope; chip and DMA voices write to the buses from local variables; the oscilloscope buckets come from a table built once per block.
- Editor: the desktop repaints its dirty zones separately, blends faster and keeps an image already scaled to the window. A full frame of the editor image drops from 8.0 to 3.6 ms at 100 %, and a full JUCE paint at 200 % from 27 to 8.4 ms.
- Version 0.9.0.
- Editor: the desktop is drawn with juce::Graphics at the screen's resolution instead of being enlarged by the system: on a display at 125 or 150 %, columns of pixels no longer have irregular widths, borders keep one thickness and text is smoothed as in the browser; at whole scales the drawing is unchanged.
- Editor: each zoom gives half its former size; 200 % is the former 100 %.
- Editor: each zoom is 25 % larger again: 100 % shows the desktop at 5/8 of its size (795 × 590), 160 % at its size, 200 % at 1590 × 1180.
- Options: Audio output and MIDI input become one entry, Audio/MIDI settings, since the standalone application sets both in one window.
- Edit > Drum kit and Ctrl+K are greyed out unless the focused track is Chipdrum.
- When the ADSR does not apply (Sync Buzzer in Chip mode, Chipdrum and Digidrum tracks), the ADSR and ADSR ENVELOPE frames are greyed out whole, frame lines and titles included; the buzzer's hint reads "envelope only" and stays inside its frame.
- The Shape R13 menu lists the shapes 8 to F; 0 to 3 sound as 9 and 4 to 7 as F, and such a value from a YM import or a MIDI CC is kept, the menu marking the shape heard.
- The captions of the Digidrum and DMA sample displays ("Slot 1: empty...", "Sample 1: empty...") use the 6×6 GEM font at its own size, on a white band, instead of being scaled down with the waveform.
- GENERATOR frame: a Sync Buzzer track shows the Buzzer window's Tuning, N, D, Fine, YM Pad and Presets; a SID Voice track has a Timer wave button that opens the Timer wave window.
- The main window moves by its title bar like the floating windows, and keeps its place while the plugin instance lives.
- GENERATOR frame: a Digidrum samples button under slots 5 to 8 opens the Digidrums window on the track's slot, and the frame's waveform no longer opens a file when clicked; the DMA samples button moves under samples 5 to 8, and the DMA frame shows the waveform of the selected sample.
- Window titles put a space before the dash: "STEP SEQUENCER - 1", "DMA - STE".
- Chipdrum and Digidrum tracks: the ADSR sliders, its Mode button and its curve are greyed out in every voice mode ("drums: off"), and the engine ignores the ADSR of a drum track: each hit plays to its end with its own shape.
- Options > Sound engine: with Computer ST, the STE output chain, STE bass and STE treble are greyed out, with the reason in their tooltip; a value chosen before is kept.
- Options > Sound engine: in Authentic 3ch, the ZX Spectrum and Amstrad CPC chip clocks and the Free timer are greyed out; a value chosen before is kept. The CHIP monitor lists a non-Atari clock, and a Free timer used by a timer effect, in every voice mode.
- Step Sequencer: Ghost (the other tracks' notes behind the grid) is on when the plugin starts; the prototype started with it off. The button still switches it for as long as the plugin lives.
- Authentic 3ch and Dual chip 2x3ch: the track level and the velocity lower the 4-bit volume to the level nearest in decibels, as the audio gain does in 16 channels, instead of scaling the volume index. At the default settings a note was 10.6 dB quieter than in 16 channels; the modes now differ by 1.6 dB at most.
- The code comments, test names and tool messages are in English; the README and the manual stay in French and in English.
- The Clock selector (Internal, MIDI ext) sets the sequencer's clock as well as the arpeggiator's, as in the prototype: on MIDI ext the sequencer follows the MIDI clock and the BPM slider is greyed.
- Confirmations before something that cannot be undone (Clear track, Demo pattern, Quit, Recorder Clear, Digidrum banks) show Cancel first, framed as the default; Enter and Esc cancel. Text boxes show Cancel then OK, and Enter means OK.
- The editor's preferences move to `Preferences.settings` in the STYM2149 folder; the keys of an older `STYM2149.settings` are copied once.
- Sync Buzzer presets keep the Sync Buzzer: eight new presets set the shape, the tuning (N / D) and YM Pad, without switching the track to Chip Voice.
- The ENVELOPE frame greys out its EP = and Hz labels too when the envelope is tuned; the Tuning button reads On or Off.
- Edit > Samples > DMA (STE) is greyed out with Computer ST.
- Fuji > Information shows the STYM2149 icon above the text, and its OK button has the bold border of the default button, since Enter closes the box.
- Options > Sound engine > Chip clock: the 2 MHz entry becomes Custom, whose frequency (100 kHz to 8 MHz, 2 MHz by default) is typed in a dialog box; it plays in 16 ch only, 2 MHz in the other modes. New host parameter Chip Clock Hz, not automatable: 439 parameters.
- Edit menu: Timer wave and Buzzer leave it; the Buzzer window is removed, its settings being in the GENERATOR frame of a Sync Buzzer track, which also gets a Timer wave button.
- Options > Sound engine > Chip clock: the Custom frequency is typed and shown in MHz with four decimals, 0.1000 to 8.0000 MHz.
- The Sequences window becomes part of the Modulation window: its rows (PER, NP, MIX, GEN, FIX, OUT) below the table while it is on, its settings below them; one On, one length and one Clear for both. Edit > Sequences and Ctrl+L are removed.
- Step Sequencer: the Tempo field is named BPM.
- Drum kit: the TR-606 and TR-808 kits are named TR-606-ish and TR-808-ish, like TR-909-ish.
- The Mixer, Arpeggiator, Modulation, Step Sequencer and Patches buttons are centred in the main window.
- Author and copyright: "Programming: Pascal "Indus" Nowak (with Claude's help)" and "Copyright © 2026 Indus / PRISM" in the Information window, the source headers, the plugin metadata and the Windows installer.
- XM import in BPM timing: a tempo change within a pattern becomes a Txx command at its line instead of moving to the next Song entry.
- Main window: VIBRATO under SOURCE, then ADSR and its curve under ENVELOPE (the curve 74 px high), GENERATOR over the whole height; the Shape R13 row moves up; the line of keyboard shortcuts is gone and the window is 32 px shorter.
- GENERATOR: a SID Voice shows its SID wave (16 bars, Steps, Square and Sine) and a Sync Buzzer its 8 steps; the Timer Wave buttons and window are gone; YM Pad and Presets are centred.
- SOURCE: what the sound does not use is greyed with its reason in the tooltip: Noise period without noise, Source, Noise, Volume and Mode on a Sync Buzzer, Mode and the Env sources on a SID Voice.
- Modulation window: the sequence rows sit inside the table, between its pitch rows and NSE / VOL, and their notes join the table's at the top; a right click on PER or NP opens a slider, as on VOL, and a right click on that slider sets the value back.
- Mixer in 3 channels: the fader of a chip channel is its 4-bit Volume, the same as in the SOURCE frame.
- Slide Curve: Invert turns the whole curve upside down.
- Options > Tooltips is a submenu: On, Off, then Language and its six languages.
- File > New stops the sequencer and disarms Rec.
- Chip clock: the Atari entry is named "Atari ST".
- Windows installer: the standalone application goes to `C:\Program Files\PRISM\STYM2149`; an update removes the former `C:\Program Files\Indus` folder.
- Arpeggiator: the divisions are in a menu button; Tick (one MIDI clock pulse) is offered on the MIDI clock only, since with Sync it was the rate of the arpeggiator without Sync.
- LFO: the Shape button and its menu show a drawing of each shape instead of its name.
- Generator: a right click on a step of the SID wave opens its slider, as in the Modulation window.
- Engine: a Digidrum in 16 channels plays at the level of the same hit in 3 channels (it was 10 to 13 dB louder, its gain of 5 compensating the DAC a second time), and the 16-channel Chipdrum sample is 2.9 dB lower, at the level of the 3-channel register drums; 16-channel projects with drums play lower.
- LFO: the Triangle drawing comes to a point and the Sine one is round, each on two cycles like the other shapes.
- Step Sequencer: the notes scroll with a scroll bar at the right edge of the window, as high as the grid, with an arrow at each end (a semitone), a click beside the thumb moving by an octave.
- Arpeggiator: Division sits on the row of the mode.
- ENVELOPE: the frequency value moves to the row of EP =, and the frequency slider runs over its whole row.
- Modulation window: an LFO set to Off shows only its label and its button.
- Ctrl+D picks the DMA generator, like Ctrl+V, S, U, C and I for the other generators; File > Demo pattern keeps its entry, without a shortcut.
- Desktop: the menu bar runs across the top of the desktop, as on a GEM desktop; the main window starts below it.
- Main window: Vibrato's Depth and Rate, and Source's Volume and Level, have sliders of one width, as in ADSR; ADSR shows A, D, S and R with wider sliders; the register monitor names CHIP VOICE.
- Mixer: the value under a pan reads "Pan +25", like "Send 50".
- Step Sequencer: the CMD row of the step commands is shown by default.
- LFO: with Sync, the divisions are in a menu button instead of the rate slider; a target that the track cannot move is greyed out in the To menu with its reason.
- Tooltip languages: the ADSR tips name each setting in the language, then in English in parentheses.
- Arpeggiator: the table grid shows the offsets +0 to +24, the reach of the chords (Double octave), instead of +0 to +15; the window is 9 rows taller.
- Patches: a name has no length limit, as in the prototype (the field of the prompt shows its end); a name wider than the window is cut with "..." on its button, the tooltip giving it whole.
- Pitch bend during a slide: the voice stays at its place in the slide, bent, instead of jumping to the bent target note until the next slide step; the timer of SID Voice and Sync Buzzer frames follows the bend.
- Step Sequencer: a step holds one command of each kind (arpeggio, vibrato, BPM, envelope restart, ticks per line, sync mark), played together; its CMD cell shows ARP, VIB, BPM, ENV, TIK or SYN, MUL for several. A click opens the Commands box, a row per command with its value and an On/Off button; Cancel is the default button; in a plugin the BPM row is greyed out, the DAW setting the tempo. Saved projects load each command into its row; an XM speed or tempo change now shares a line with an arpeggio.
- Step Sequencer: nine more step commands in the Commands box, each made of writes per tick that a real ST plays, so the register exports carry them: volume slide `Axy`, pitch slide `Pxy`, note delay `Dxx`, note cut `Cxx`, buzzer slide `Bxy` (envelope period), noise slide `Nxy`, envelope shape `Exx`, DMA sample offset `Oxx` and fine tune `Fxx` (cells VSL, PSL, DLY, CUT, BSL, NSL, SHP, OFS, FIN; state rows 38 to 46, JSON keys `cmdA` to `cmdF`). The XM import turns `Axy`, `1xx`, `2xx`, `9xx`, `EDx`, `ECx` and `E5x` into them; a looped DMA sample started at an offset loops back to that offset, in the engine and the SNDH export.
- Editor: at a scale that is not whole, Options > Pixels > Sharp draws the letters and icons on whole screen pixels without anti-aliasing; Smooth, the default again, keeps the former rendering.
- Desktop: the floppy disks A and B sit side by side at the top left, the trash at the bottom right.
- Plugin vendor: PRISM alone in VST3, CLAP, LV2, AU, AAX and the Windows installer; the plugin IDs and the manufacturer code do not change.
- Options: no Zoom entry; the size follows the window, resized in the host or by the system, from 50 to 200 %, and is remembered.
- Step Sequencer: the arpeggio `Xyz` and pitch slide `Pxy` commands are retired, the ARP and SLD rows doing their work; a value read for them (saved project, pattern file, state) is dropped, and the XM import leaves `0xy`, `1xx` and `2xx` out.
- Step Sequencer: vibrato, BPM, envelope shape, note delay and note cut leave the Commands box for rows of their own: VIB (No change, On, Off) while the track's vibrato is on, BPM under CMD in the standalone application only, ENV (the shapes 8 to F) when the envelope plays (Env source, Sync Buzzer, an Env entry of SRC or a Sync Buzzer step of GEN), DLY and CUT (a tick of the line, None). They follow the Resolution menu's "Step command rows" preference, formerly "CMD row", and stay shown while one of their commands is written. The Commands box keeps eight commands; Envelope Restart shows as RST.
- Step Sequencer: VOL, RTG and ARP act on every step of a held note, as SRC, GEN, PAN and CRV did: the volume follows the VOL row step by step, RTG strikes again from the start of its step, ARP starts or resumes the arpeggio there; a GAT on the last step of a held note releases it there. The MIDI and XM imports write the note's volume on its held steps.
- Step Sequencer: ARP becomes a switch like VIB: No change, On (`J01`) or Off (`J00`) from its step on; each pattern starts with the arpeggiator off, and the ARP flags of older projects and pattern files become switches that play the same. New LFO (`Lxx`) and MOD (`Mxx`) rows switch the track's LFO and modulation table the same way; each pattern starts with them as set in the Modulation window.
- Step Sequencer: envelope restart, sample offset and fine tune leave the Commands box, which keeps ticks per line, sync mark and the three slides: ENV gets a Restart submenu (None, At once, At tick...) beside the shapes, its cell showing the shape and R; SMP (DMA tracks only, 0 to 255 steps of 256 frames) and TUN (−99 to +99 cents) are rows of their own.
- Step Sequencer: the BPM row moves under VOL, behind a separator, and becomes one row for every track of the pattern: a tempo written on one track shows on all of them. It is saved with the pattern (JSON key `bpm`); the `Txx` of older projects and files move into it.
- Step Sequencer: a value written where it cannot act (vibrato, LFO, arpeggiator or modulation table off, a drum track, no envelope playing, not a DMA track, no note starting on the step, no slide running, BPM in Ticks timing or on an external clock) is greyed and crossed; its tooltip says why.
- Step Sequencer: the edge of a note resizes it with the Move tool too, with the resize pointer; a drag from a selected note moves the whole selection.
- Step Sequencer: with the Overview shown, only its "Whole pattern" line stays under the grid.
- Options > Sound engine: no Arp division entry; the Division button of the Arpeggiator window sets it.
- Main window: in SOURCE, the Source, Voicing and Mode buttons start with the Volume and Level sliders and the Noise period slider ends with them; in VIBRATO, the Depth and Rate sliders are wider, both as wide.
- Main window: in SYSTEM, the Voices, Computer and Clock buttons start together, and Computer becomes a menu button (ST, STE) instead of two buttons; in the GENERATOR frame of a SID Voice, the SID min vol slider ends with Timer ratio; in the Arpeggiator window, the Rate slider starts with Steps.
- Main window: on a DMA, Digidrum or Chipdrum track, SOURCE (but Voicing and Level) and the ENVELOPE frame are greyed out, with the reason in the tooltips: these tracks play their own sound.
- Step Sequencer: the control rows follow what they do to the note: its pitch (OCT, TON, TUN), its sound (INS, GEN, SRC, ENV, SMP), its modulations (ARP, VIB, LFO, MOD), its slide (SLD, CRV), its timing (CND, DLY, RTG, GAT, HLD, CUT), CMD, then PAN, VOL and BPM.
- Step Sequencer: a cell where nothing can act because of the sound of its step is crossed even when empty, and a click there opens nothing: ENV under a SID Voice, SRC under a Sync Buzzer, SRC and ENV under DMA and drum sounds, SMP outside DMA notes, INS and CND on held notes, INS on the note a slide reaches, GAT and RTG during a slide, GAT on a step the note goes on after, GEN on the DMA tracks of the 3-channel modes; its tooltip says why.
- Desktop: the icons move on an invisible grid of cells of their size, as on a GEM desktop, and a cell another icon holds refuses the drop; a click on the trash is File > New.
- Step Sequencer: ARP, VIB, LFO and MOD go while the track's arpeggiator, vibrato, LFO or modulation table is off, and come back with their values; CRV shows only while an SLD cell is lit.
- Step Sequencer: the Note length button draws its note, Step the note of the Resolution; while the Draw tool holds a note that can move, the pointer is a hand; the PAN box shows 0 at the centre.
- Commands box: each slide has one slider from 0 to 255 over its value xy, and a slide shows only where it can move a note from its step on, by the generator and the source of each note (GEN, SRC, instrument, track); one written on the step stays.
- Commands box: a slide's slider and field give its speed from 0 (stop) to 15, and an Up/Down button its direction. In 16 channels the button of the Buzzer and Noise slides opens their Curve menu: Up, Down and the shapes of the CRV row, from the step to the next command of its kind (`Kxx`, `Qxx`). The Volume Slide shows only where an older project wrote one: the Slide of the VOL cell replaces it.
- Step Sequencer: a right click on a GEN, SRC or ENV cell opens the GENERATOR, SOURCE or ENVELOPE settings of the track that the cell brings into play, instead of erasing it.
- SID Voice: the Steps slider sits under its label without the unit, and the SID min vol and Steps sliders end with Timer ratio's. Sine samples its volumes half a step later, so no step is at 0, and draws one period over the 16 bars without changing Steps: the bars beyond Steps stay greyed; from the square wave it sets 16 Steps. While Sine is lit, a bar moved shifts the sine along the bars instead of changing its height; a click on the lit Sine frees the bars. A Period slider under Square and Sine (2 to 16 bars, 8 by default, shown while Sine is lit) sets the bars of one cycle of the sine, in the main window and in the GENERATOR window of a GEN cell. While Square is lit, the bars show the square wave played: the note's volume, then SID min vol. A Width slider in the same place (1 to 15 sixteenths of the period, 8 by default, shown while Square is lit) sets the share of the square at the note's volume, in both windows; Period and Width have their label before the slider.
- Menu bar: the Atari logo takes the shape of the real one, its side prongs sweeping out to the edges, and the editor draws it from its outline at the screen's resolution, smooth at 125 % or 150 % instead of enlarged pixels (with Options > Pixels > Sharp, crisp without anti-aliasing). The framebuffer keeps its raster at 17 × 14 pixels.
- SID Voice: Sine draws a sine on the bars, its volumes following the sine instead of the chip's level, which drew a bounce; it sounds brighter.
- Desktop: the FLOPPY DISK and TRASH icons are those of the EmuTOS desktop (GPL, THIRD_PARTY.md): a 3.5-inch floppy disk, its drive letter in the 6×6 font where the AES puts it, on its label, and the can. They replace the pictures of TOS 1.04 (© Atari), which could not be distributed under the AGPLv3.
- SID Voice: the Square and Sine buttons give way to a menu after the name of the SID wave: Base (the plain square, without settings), Square, Sine and Custom (the bars drawn); each form keeps the settings of the others, in the main window and in the GENERATOR window of a GEN cell. Steps sits on the line of its label, Period (Sine) or Width (Square) right under it, the three sliders lined up. The Square has its own Steps, 8 by default, and its Width counts its steps. Older projects and patches keep their sound.
- SID Voice: a Bounce entry after Sine in the menu of the SID wave gives back the sine of the chip's level, the former Sine: a pure sine heard, a bounce in the bars; Period, the phase and the bars moved work as with Sine. Older projects and patches with Sine lit read as Bounce, their bars having been drawn so.
- Arpeggiator: an arpeggio is one note per key pressed, MIDI note or note of the sequencer, not one per step: its ADSR and its point on the ADSR ENVELOPE curve run on through the steps, and each key pressed starts a new note from the attack. In 16 channels, on a track whose Voicing is Polyphonic, the note before then goes on to its release, so that a long Release overlaps them (Monophonic and the 3-channel modes cut it); keys up, the last note keeps cycling through the arpeggio during its release.
- Windows: the glyphs of the close and full-screen boxes are drawn twice as big, from the 8×8 font so that they are square (16 × 14 pixels) as on the ST's desktop in low resolution, instead of the 8×16 font's tall ones; the title bars keep their height.
- SID Voice: with the Base form, the plain square wave, the SID wave's bars and its Steps slider are hidden, in the GENERATOR frame and in the window of a GEN cell; the window keeps its size.
- Mixer: a click on the value under a fader, a pan, a Send, a Master or the balance types it in place, Enter confirms, Escape cancels, as Value... does: one undo step, clamped to the slider's range.
- Menus: a drop-down menu lies on the bottom rule of its button, without the prototype's 6 px gap (GEM convention). The Voices menu lists the three modes only: Computer is set in its own row and in Options.
- Tooltips: the Vibrato On/Off button sends to the VIB row of the Step Sequencer; the Sine and YM Pad tips no longer name a tracker.
- The development repository is github.com/Indusfr/STYM2149-build; the product keeps the address of the future public repository, github.com/Indusfr/STYM2149.
- Sources: English comments for a reader new to the code (file overviews, the role, owner, thread and units of the types and functions, the subtle blocks of the emulation, the state formats, the window layouts and the undo); the code itself is unchanged.
- Options menu: Sound engine, Keyboard layout, Note names, Renderer, Pixels and Tooltips leave it for the Settings window, which keeps the greying of their entries; the Languages folder becomes a button under Language.
- Digidrums window: Start and End set the part played with or without Loop; Loop repeats it while the note is held, then the part plays on to End. Play previews the part, and the YM6 and SNDH exports write it.
- DMA window: laid out like the Digidrums window, the slot on the left (Slot, Name, Rate, Root) and its sample on the right (Load, Play and Clear over the waveform, now 360 × 72 pixels, then Loop, Start, End, Format and Tools); the tooltips of the eight buttons say "Slot".
- Sample drawings (Digidrums and DMA windows, GENERATOR frame): the waveform is drawn at the pixels of its box instead of a 360-pixel image stretched to it, each column spanning the values that fall in it and joined to the next one, so a sample no longer looks chopped; a sample shorter than the box is drawn as lines between its values.
- Digidrums and DMA windows: with Loop on, Play repeats the part played until Play or Loop is clicked again or the window closes, Play staying lit meanwhile; with Loop off it plays the part once, as before.
- Step Sequencer: the lines between the grid's columns and rows are 2 pixels thick instead of 1. The grid is 64 pixels wider, so the window opens 4 pixels from the left of the desktop; when its rows make it taller than the desktop, it moves up until its bottom shows, never above the menu bar.
- Floating windows never go past the right or bottom edge of the desktop: moved by their title bar, placed, grown by their content (a Mixer strip turning stereo, a row switched on) or opened at a place saved on a larger desktop, they move back inside; the left edge and the top of the work area win for a window larger than the desktop. The main window has the same bounds (it could leave all but 96 × 48 pixels of its title bar off the desktop).
- Editor: the desktop is larger by a desktop icon in width and height, 1344 × 984 logical pixels instead of 1272 × 944; the main window and the alerts stay centred, and the editor at 100 % is 840 × 615 pixels.
- Modulation window of a DMA track: what moves no sample is greyed out, with the reason in its tooltip: the NSE, NP, MIX and GEN rows, Hard sync and the Sq and Bz columns of the pitch matrix. The table pitch, PER, FIX, the slides and the vibrato still move the sample through the Tm column, VOL its level; the LFO already greyed its Noise period, Env period and SID ratio targets .

### Added
- Step Sequencer: a 1/64 resolution, 16 steps per quarter note, after 1/32 in the Resolution menu, and a 1/64 note length drawn with four flags. On the MIDI clock a step lasts a pulse and a half: every other step starts on the half pulse between two clocks, as the arpeggiator's 1/64.
- Step Sequencer: a 1/32T resolution, 12 steps per quarter note, after 1/16T in the Resolution menu, and a 1/32T note length, the thirty-second note with the 3 of the triplets, after 1/16T in the Note length menu. On the MIDI clock a step lasts two pulses.
- Step Sequencer: in the Note length menu, the values that are not a whole number of steps of the resolution are greyed, with their length unrounded ("1/32 0.5 step", "1/16. 1.5 steps" in 1/16) and the reason in their tooltip. A value chosen before that no longer fits stays chosen, but notes are written one step long (Step) until the resolution fits it again; it was rounded before.
- An LFO per track in the Modulation window: Triangle, Sine, Square, Saw up, Saw down or Random, in Hz (0.1 to 20) or one cycle per division of the tempo, on the volume, the noise period, the envelope period, the SID ratio, the pitch or the pan (16 ch); restarted at each note, or free with Retrig Off. It writes register values only, so it plays in Authentic 3ch and in the exports; it is kept by the project, the patches and the instruments.
- Arpeggiator: a Sync button follows the tempo, the host's in a DAW (on its grid while it plays) or the sequencer's, one step per Division; host parameter "Arp Sync".
- VIBRATO: an On/Off button per track (host parameter "Vibrato On"), and a `Vxx` command in the CMD row: V00 holds the vibrato from its line, V01 to VFF bring it back.
- Mixer: a Master fader after the tracks, the main level of the SYSTEM frame.
- Step Sequencer: the SRC row adds Env, Tone+Env, Noise+Env and Tone+Noise+Env.
- Every number next to a slider can be typed: click it, type, Enter, in the GENERATOR frame, the Digidrum and DMA windows, the Modulation window and its sliders.
- CMD row: `Txx` sets the tempo to xx BPM ($20 to $FF) from its line on, in BPM timing with the sequencer's own clock and in the exports.
- SOURCE: the Source menu adds Env, Tone+Env, Noise+Env and Tone+Noise+Env, which also set Mode; Env alone plays the envelope's own waveform.
- Computer Keyboard: on a Chipdrum or Digidrum track, the middle row of keys (A to K by position) plays the 8 pads, and the drawing names them.
- File > New (Ctrl+N): back to a new project, after a confirmation if anything is already defined.
- Application icon: a YM2149 DIP package with "YM" in the GEM font, for the standalone application and the Windows installer; on Linux, `install.sh` adds a menu entry with the icon.
- Build number: Fuji > Information shows the number of the CI run that compiled the plugin and its commit after the version (`0.9.0 build 412 (fe71c0d)`, `build dev` for a local build); the Windows installer and the Linux archive carry it in their names.
- `ymbench --hash` (FNV-1a fingerprint of the rendered output) and `--tracks` (separate track and chip outputs).
- DAW test protocol for FL Studio 26, Ableton Live 11, Cubase Pro 15, ACID Pro 10, Maschine 3 and Reason 13 on Windows.
- CI: on Linux, pluginval runs under gdb when the runner has it, to print every thread's stack if it crashes.
- Tooltips in English, French, German, Spanish, Portuguese and Italian, chosen in Options > Tooltip language; by default the system language when it is one of the six, otherwise English. The GEM font gains the accented letters of the Atari ST character set, from EmuTOS. Menus and labels stay in English. The translations still need a review by native speakers.
- Windows installer for all users, with administrator rights: VST3 and CLAP in `C:\Program Files\Common Files`, the standalone application in `C:\Program Files\Indus` (Inno Setup), and a Linux archive with `install.sh` for the home folder. The CI builds both on every run.
- CI: AddressSanitizer and UndefinedBehaviorSanitizer on the core and interface tests, ThreadSanitizer on the plugin tests, the Steinberg VST3 validator on Linux and Windows, and the CPU worst case of `ymbench` in the Linux job summary.
- SHORTCUTS window of the prototype (Ctrl+/, Ctrl+?, Fuji menu > Shortcuts): its nine sections, then the keys STYM2149 adds.
- Options > Sound engine > Quantize to VBL.
- SNDH export: `Zxx` commands write xx into a sync byte of the replay routine, whose address the init routine returns in A0, for demos.
- GEM menus can flow into columns, as the prototype's Chord... menu of the Arpeggiator.
- Tab and Shift+Tab move between the buttons of a dialog box.
- Licence texts of every component shipped with the packages, and the libraries that JUCE compiles into the plugin listed in `THIRD_PARTY.md`.
- Written check that STYM2149 makes no network access; the internal "telemetry" between engine and editor is renamed engine status.
- Parity with the prototype, row by row; user manual in English and French (`docs/MANUAL.md`, `docs/MANUAL.fr.md`); README in English and French; architecture guide (`docs/ARCHITECTURE.md`).
- Mixer: with a stereo output (16 channels, 3 channels with the STE), Master has a left and a right fader, a balance slider and a Link button; in 2x3 channels, a fader per chip and Link; new host parameters Master 2, Master Pan and Master Link.
- Arpeggiator: 1/32T and 1/64 divisions.
- Step Sequencer: a right click on the metronome button opens its volume, kept with the project.
- Tooltip languages in files: English is the only language built in; French, German, Spanish, Portuguese and Italian are gettext PO files (`languages/`) that the installers put in a folder of the computer (`C:\ProgramData\STYM2149\Languages`, `~/.local/share/STYM2149/Languages`). The PO files of the Languages folder next to the preferences add languages to Options > Tooltips > Language or correct the installed ones; Languages folder... writes a template and opens the folder.
- Every horizontal slider but the Mixer's pans has an arrow at each end: a press moves the value by one step, repeated while held.
- Mixer: each fader, pan and Master shows its value; a right click on one opens Value... to type it and Reset.
- Tab brings the open windows to the front in turn, the main window included, Shift+Tab the other way.
- Step Sequencer: the box of a VOL cell (right click) has a Slide button: the step glides to the volume of the next step (`Gxx`), drawn as a ramp; in the 3-channel modes the 4-bit volume glides, so the register exports keep it.
- Desktop: decorative icons of floppy disks A and B and of the trash, named in the tooltip language, moved by a held press.
- LFO: 1/32T and 1/64 divisions.
- Effects window (Effects button, Edit menu, Ctrl+F): an echo of the notes of each sound in every mode, played by the chip, so Authentic 3ch, the Recorder and the register exports keep it (Division, To, Repeats, Drop); an audio delay in 16 ch only, fed by a new Send of each track in the Mixer (Sync or Time, Feedback, Hi cut, Level, Ping-pong). Nine new host parameters, version 6; ymbench `--echo`. A Sync Buzzer gets no echo in the 3-channel modes, where a repeat would restart the shared hardware envelope; Ping-pong shows while the delay is on.
- Options > Oscilloscope: shows or hides the oscilloscope of the main window; hidden, the register monitor and the keyboard move up and the window is 100 px shorter. A preference.
- Options > Register monitor: shows or hides the register monitor of the main window, as the oscilloscope; hidden, the keyboard or pads move up and the window is 50 px shorter. A preference.
- Euclidean generator: a Rhythms menu of the 23 named rhythms (Tresillo, Bossa-Nova, Brazilian samba...), sorted by length and greyed when longer than the track, sets Steps and Pulses; Apply asks before it replaces or removes notes already written.
- Tuning A4: the frequency of A4, 350 to 480 Hz (440 Hz by default), from which every note is tuned, the slides and the Digidrum and DMA samples included; host parameter Tuning A4, not automatable. At 2 MHz the chip plays A4 at 440.14 Hz, the nearest whole period.
- Settings window (Options > Settings...): SOUND ENGINE, MIDI and INTERFACE frames, each setting a button showing its value that opens the list of values; the window stays where it was put and opens again with the editor. Changes apply at once; OK keeps them, Cancel (the default button), the close box and Escape put back the settings changed since the window opened; when a setting differs from its value at the opening, Cancel first asks in a GEM box whether the changes may be lost. The VBL frequency moved there from the Options menu, in SOUND ENGINE before Quantize to VBL.
- Standalone application: the close box of the system's window (and Alt+F4) opens the Quit box of File > Quit instead of quitting at once; the custom application adapts JUCE's.
- Options > Note monitor: shows or hides the Note line of the SYSTEM frame (TP and Hz), as the oscilloscope and the register monitor. A preference.
- MIDI expression (Settings > MIDI): Bend chooses what the pitch bend drives (Pitch, Volume, Envelope), Wheel what the modulation wheel drives (Vibrato, Volume, Envelope, LFO, Off), Aftertouch what the channel and polyphonic pressure drive (the same choices); MPE (16 ch only, lower zone) gives each note of channels 2 to 16 its own pitch bend (MPE bend range, 48 semitones by default), pressure and CC 74 (MPE slide: Volume, Envelope or Vibrato). Six new host parameters, not automatable; drums ignore the expression.
- DMA window: Start and End, in frames, set the part of each sample played, with or without Loop; Loop repeats it while the note is held. `Oxx` counts from Start, Play previews the part, Clear and a new file give the whole sample, and the SNDH export writes the sample up to End. Kept in the state after the eight samples.
- Digidrum and DMA samples: the name of the sample opens the line of text over its waveform (Digidrums and DMA windows, GENERATOR frame) and follows its number in the tooltips. A DMA sample gets a name like a Digidrum slot: taken from the file loaded (or the XM sample, else its instrument), typed with the Name button of the DMA window (one undo step), cleared with the sample, kept in the state.
- Step Sequencer: choosing another resolution while the patterns hold notes asks whether they follow it. Fit moves each note to the step of the new grid nearest its time and scales its length, the pattern lengths, the BPM row, the tracks' own lengths and the Song rests with it (one undo step); Keep leaves each note on its step, which now lasts the new value, as before; Cancel, the default, keeps the resolution.
- DMA and Digidrums windows: the same tools. The DMA window gains Normalize, Reverse and Fade out, the Digidrums window Sign; Reverse mirrors Start and End, so the part played is the same sound read backwards. With a stereo DMA sample, L and R choose the channels the tools work on (both by default); Normalize on both channels keeps their balance.
- DMA window: Resample under Rate (a preference, off by default). Lit, a new rate takes the sample again at it by the interpolation of loading: it keeps its length and its pitch, Start and End stay on the same moments, 32 KB at most, one undo step. Unlit, the bytes are kept and the pitch moves, as on the STE.
- DMA window: Fine, −100 to +100 cents per sample under Root, as in the Digidrums window; it tunes the voices, the preview and the SNDH export. Kept in the state after the names; older projects read 0.
- DMA: the drawing of a stereo sample marks its two channels L and R at their top right, in the DMA window and the GENERATOR frame.
- Mixer: a DMA track playing a stereo sample shows two oscilloscopes in its frame and two meters, left and right; the engine publishes the right peak and the half difference of the sides next to the mean oscilloscope. Mono strips are unchanged.
- Mixer: a DMA track playing a stereo sample has a fader, a value, M and S for each side, and Link under them: lit (by default), one Volume moves both faders; unlit, the right side has its own level, which starts at the Volume. Mute and Solo of each side are independent. New host parameters per track Level R, Mute R, Solo R and Link (parameter version 9), mixer settings like Send: not in patches nor copied with a track. Register exports skip a voice only when both of its sides are silent.
- DMA window: Banks, under Fine, as in the Digidrums window: Save bank... keeps the eight samples (settings, bytes and names) under a name, Load bank replaces them after a confirmation, Delete bank removes one. The banks are kept in `DMA.json` next to the preferences, shared by all projects; an unreadable file is never overwritten.

### Fixed
- Digidrums: the preview of Play no longer clicks at its end, nor at its start once reversed: it centred the chip levels halfway between 0 and 15 instead of on the level of the slot's silence.
- DMA and Digidrums: −3 dB after a +3 dB that clipped gives the clipped peaks back; each step of a run of gains is computed from the bytes before its first step.
- Digidrums: the "Hz" of Rate and the "ct" of Fine are no longer shown in reverse video under the pointer, as they are not typed.
- The other units of typed values are no longer shown in reverse video under the pointer either: the "c" of Detune and Analog (Modulation) and of the Buzzer Fine, the "Hz" of the LFO Rate and the unit of its Depth, the "ms", "%" and "Hz" of the Delay; the Delay's units stay shown while a value is typed, as the A4 "Hz".
- SYSTEM frame: the Note line (TP and Hz) goes back to "Note -", as at launch, once no voice sounds; it kept the last note shown.
- Settings: the "Hz" after the Tuning A4 value is no longer shown in reverse video under the pointer, as it is not typed.
- Keyboard: a note held live before Play (Latch on or not) no longer stays lit once the sequencer plays a note of its pattern. The sequencer took the voice of the held note for its own: in 16 channels the held note now moves to a free voice and goes on sounding, as a note played during playback does; in the 3-channel modes, or with no free voice left, it stops and its key lets go.
- Step Sequencer: the check mark of the Resolution menu was one entry too low for the triplets (1/4T marked 1/8T) and on Groove for the Ticks timing.
- Step Sequencer: on a polyphonic track in 16 channels, the next note no longer cuts the decay or the release of the note before: that note goes on to its release on another voice of the track, and a chord takes the silent voices of the track before those of the chord before, still in its release; Monophonic and the 3-channel modes cut it as before.
- Windows: the window reaches the screen through JUCE's software renderer, which copies only what changes, instead of Direct2D, which sent the whole image of the desktop to the graphics card at each repaint; with Direct2D, a copy of the standalone application's window flickered at the top right of the Windows desktop for a few seconds, with nothing touched. Options > Renderer (Windows only) gives Direct2D back.
- Register exports (YM, SNDH, VBL): a SID wave of Steps, or a square of another width, plays at the note's pitch; it played Steps / 2 times too high, the timer of the wave instead of that of its square.
- ADSR curve: a point for each note sounding on a polyphonic track, so a note in its release keeps its point while the next ones are played; a monophonic track (and any track of the 3-channel modes) shows the newest note's alone.
- ADSR curve: a note released before its sustain goes on down the decay line; its point jumped to the start of the release segment, away from the curve. With S at 0 %, it goes down to the end of the decay, where the note stops; it flashed at the end of the release segment for its last milliseconds.
- Sync Buzzer with steps: its shapes fit in one period of the note, so it plays the note of a Chip Voice; a cycle of n shapes lasted n periods and sounded log2(n) octaves lower.
- SID Voice plays the note of the key at Timer ratio 1, as Chip Voice and Sync Buzzer do, in every form of its wave; like the prototype, it sounded an octave lower (ratio 2 played the note). A project that set ratio 2 to make up for it now sounds an octave higher. Sine and Bounce chosen from Base set as many Steps as their Period (8), one cycle that plays the note, instead of 16.
- ADSR in Chip mode: the Attack and Decay sliders go to their end; they stopped at 50 ticks at 50 Hz (1 000 ms). Attack, Decay and Release now go to 100 VBL ticks in Chip mode and 2 000 ms in Smooth, the same length at 50 Hz: the prototype stopped Attack and Decay at 64 ticks and 1 000 ms, Release at 64 ticks.
- Step Sequencer: ARP Off on a step of a held note stops its arpeggio there; the arpeggio ran to the end of the note. With the Move tool, the edge of a note no longer stretches it: only Draw does.
- Step Sequencer piano roll: 27 editing bugs, among them a click that shortened the next note instead of removing a note, drags over held notes that cut them, moved notes that left their accent and slide behind, the right click in a selection box, boxes and < > that erased notes, +1 under Snap that left notes in place, and Redo lost after a click that changed nothing.
- Engine: in 3 channels, a Chipdrum hit follows the track level and the velocity.
- Sequencer: overlapping notes of one track all sound to their end and all light the keyboard. A chord voice written under a held note no longer cuts an earlier voice of the same rank, and the piano and the keyboard drawing show every note that sounds at the played step, not only the notes starting there.
- Step Sequencer: with a scale and Snap on, no note lands on a greyed row any more. A chord voice added to a step that already holds a note, the voices of the input chord, a dragged note or voice and the chord voices of a transposed selection go to the nearest degree, as the root already did; a press on a greyed row whose degree already sounds at that step changes nothing.
- GEM font: the 8×16 glyphs are back at their place in the EmuTOS cell, so i, I and l are centred; the prototype's TrueType font had moved 65 of them one column to the left.
- Engine: the tuned envelope and YM Pad of a Sync Buzzer no longer change a Chip Voice: set on a track that later plays Chip Voice (its generator, a GEN step or an instrument), they made its envelope follow the note.
- The metronome no longer shows on the oscilloscope or the Mixer's output meters.
- Step Sequencer: the bar line of step 1 no longer widens the note-name column over the first column.
- Options > Keyboard layout > Auto follows the system's keyboard layout (AZERTY, QWERTZ, otherwise QWERTY) on Windows and Linux, instead of always QWERTY; macOS is TO CONFIRM.
- Editor: oblique lines, such as the ADSR curve in Smooth mode, are drawn anti-aliased at every zoom and display scale instead of in steps.
- Engine: an arpeggiated note (ARP row of the sequencer or keyboard arpeggio) keeps its Modulation table, vibrato and frame cadence from one note of the arpeggio to the next. At the default rate of 50 Hz the table no longer stays on its first step, so ARP steps (steps 1 and 5 of the Demo pattern) now play their modulation.
- Sequencer: switching a track off during playback (its tab, All off, or the host automating its On parameter) releases its notes at once, as in the prototype. A note held by TIE or SLD, chord voices of several steps, RTG retriggers and the track arpeggio used to ring on until Stop: with the Demo pattern in Authentic 3ch, the tied Sync Buzzer of track A or B kept droning at full envelope level over the notes of the track left on.
- Sequencer: in Authentic 3ch, Dual chip 2x3ch or on a monophonic track, the chord voices the grid greys out no longer light the drum pads, the piano or the keyboard drawing during playback.
- GEM menus: a greyed-out entry no longer opens its submenu on hover.
- Windows installer: the per-user installation of earlier test builds is uninstalled first. Its application stayed in `%LOCALAPPDATA%\Programs\STYM2149` with its Start menu shortcut and desktop icon, which kept starting the old application after an update.
- CHIP monitor: the ADSR of a Sync Buzzer is no longer listed in Authentic 3ch, where it is inert.
- Desktop: destroying the root widget first no longer leaves a dangling pointer (found by AddressSanitizer).
- Plugin tests: allocations are counted on the audio thread only, so that another thread allocating meanwhile is not blamed on `processBlock` (found under ThreadSanitizer).
- Standalone app on Windows: every save by the app put back the editor preferences it had read at launch, because both used the same file.
- Standalone app: File > Quit now keeps the session like the window's close box.
- The SID timer ratio's tooltip described a Digidrum rate; five tooltips were translated word by word through a generic line.
- The Digidrum frame of the main window always printed 7800 Hz; it prints the slot's rate.
- Step Sequencer: the overview map now follows playback, the scroll bar and the transpose slider.
- Step Sequencer: Enter in the Clear track confirmation no longer clears the track.
- Keyboard destination menu (right click on the keyboard or the pads): it opens under the key or pad clicked, as in the prototype, not at the pointer; in 3ch and 2x3ch on the STE it names the DMA tracks DMA1 to DMA8 instead of reading past the tables of voice names.
- CI: pluginval no longer crashes after its tests on Linux, built with the JUCE fix of the VST3 host run loop.
- Plugin: the message thread no longer reads the engine that the audio thread replaces when the clock, the output chain or the resampling changes (data race found by ThreadSanitizer).
- Step Sequencer: with the Move tool, a click on a note no longer removes it or cuts it; it places the cursor.
- ADSR ENVELOPE: with the echo on, the cursor of the curve no longer goes back to zero at each repeat.
- MIDI export: in 16 channels, the arrival note of a slide takes its level instead of velocity 1.
- Voices menu: a change of voice mode keeps the focus on a DMA track of the STE that the new mode still has.
- Closing a menu no longer compares an invalidated iterator (undefined behaviour, stopped by checked standard libraries); the track oscilloscope no longer reads past a single point.
- Track names: a name cut to 8 characters no longer keeps a space at its end.
- Step Sequencer: a drag that resizes or moves a note and that the lane refuses no longer leaves an empty undo step that cleared Redo.
- Patches: Save current, Rename, Overwrite and Delete read the shared library file again first, so that a change made by another instance is kept, and act only on the patch the menu was opened on.
- SNDH export: 99 subsongs at most, the reach of the two digits of the `##` tag.
- Colour blend: an opacity outside 0 to 1 takes the nearest bound; Euclidean generator: 2 to 32 steps, like its slider; code without effect removed.
- Desktop: a dragged icon no longer leaves dots of its outline below 100 % of real scale.
- Euclidean generator: picking the note in its menu closed the box and lost the pick.
- Track length box: its field and slider show the length the track plays, its own or the pattern's.
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
- macOS (AU, auval, universal binary, pkg) and AAX delivery. Installers are not signed.

## `ymio`, import and export (16/10/2026)

- Whole File menu of the prototype: JSON preset, pattern, Song, drum kit and Patches library files, readable by the prototype.
- YM Player: YM2 to YM6 (LHA or plain) and SNDH, played by AtariAudio's Atari machine, bit-identical to AtariAudio on 12 tunes.
- MIDI: SMF 0 and 1 import, SMF 1 export of what the sequencer plays.
- Audio export: WAV 16-bit, 24-bit and 32-bit float at any rate, and stems in a ZIP.
- Register export: VBL, YM3, YM6 (LHA, with timer effects), SNDH v2 for ST or STE, and the recorder's take.
- XM 1.04 import with samples, played by the STE DMA sound.

## maxYMiser, TTrak and YM-Tracker features (16/10/2026)

- Modulation, Drum Kit and Patches windows; MIDI learn.
- 32 instruments played by an INS row and Program Change; instrument sequences per tick.
- Replay tick at 100 and 200 Hz, Ticks timing with groove; `Xyz`, `Sxx`, `Rxx`, `Zxx` commands in a CMD row.
- Programmable SID wave, Sine-SID, Sync Buzzer steps, three timers per chip; tuned buzzer, fine tuning, YM Pad and presets.
- ST/STE machine and the STE DMA sound on 8 tracks; Digidrum editor; register recorder; extended Song with rests, note-offs, jumps and restart point.

## Sequencer (16/10/2026)

- The prototype's step sequencer in the engine: 32 patterns of 1 to 256 steps on 16 tracks, chord voices, arpeggios, slides, retriggers, conditions, step effects, global gate, Song, metronome, Quantize to VBL.
- Free run, MIDI clock and Host sync clocks (Host sync by default in a DAW).
- Step Sequencer, Song, Mixer, Arpeggiator, Slide Curve and Computer Keyboard windows.

## GEM interface (15/10/2026)

- The prototype's main window drawn by `gemui` in a 1016 × 944 GEM desktop, scaled with nearest-neighbour from 50 to 200 %.
- Visual regression test against the prototype's captures.

## Minimal plugin (13/10/2026)

- VST3, CLAP, LV2 (Linux), AU (macOS), AAX (compiled only) and Standalone; 417 parameters with stable IDs; separate track and chip outputs; versioned state.
- pluginval level 10 (VST3, LV2) and clap-validator (CLAP) in the CI.

## `ymengine` (13/10/2026)

- Voice engine in three modes (16 voices, Authentic 3ch, Dual chip 2x3ch) with MFP timers, SID, Sync Buzzer, Digidrum, Chipdrum, ADSR, vibrato, ornaments, arpeggio, slides and legato; ST and STE output chains.

## `ymcore` (13/10/2026)

- YM2149 core following AtariAudio 1.26 step by step (thresholds S1 to S11 met), RawBox mode bit-identical to AtariAudio's player, LHA decompression ported from pylzh.

## Audit

- DSP audit, parity matrix of the prototype, gap tables against maxYMiser, TTrak and YM-Tracker.
