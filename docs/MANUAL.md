# STYM2149 user manual

Version 0.9.0. Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3.

STYM2149 is a software instrument that emulates the YM2149F sound chip of the Atari ST (and the DMA sound of the Atari STE), with an interface drawn in the style of the Atari GEM desktop. It is essentially a plugin: you load it on an instrument track of your DAW, play it from MIDI and drive its built-in step sequencer from the DAW's transport. A standalone application is also included.

Version 0.9.0 ships for Windows (VST3, CLAP, Standalone) and Linux (VST3, CLAP, LV2, Standalone).

Notes in this manual marked **TO CONFIRM** describe points that have not been verified yet.

## Contents

1. [Installation](#1-installation)
2. [Getting started](#2-getting-started)
3. [The main window](#3-the-main-window)
4. [Floating windows](#4-floating-windows)
5. [The sequencer](#5-the-sequencer)
6. [Instruments, Patches, Drum Kit, Digidrums and DMA](#6-instruments-patches-drum-kit-digidrums-and-dma)
7. [MIDI](#7-midi)
8. [Files](#8-files)
9. [Preferences](#9-preferences)
10. [Keyboard shortcuts and mouse gestures](#10-keyboard-shortcuts-and-mouse-gestures)
11. [Privacy](#11-privacy)
12. [Troubleshooting](#12-troubleshooting)
13. [Licences and credits](#13-licences-and-credits)

---

## 1. Installation

### 1.1 Windows 10 or 11 (x64)

Run `STYM2149-0.9.0-windows-x64-setup.exe`. The installer installs for all users and asks for administrator rights. It speaks English, French, German, Spanish, Portuguese (Brazil) and Italian, shows the licence (AGPLv3), and lets you choose the components:

| Component | Installed to |
|---|---|
| VST3 plugin | `C:\Program Files\Common Files\VST3\STYM2149.vst3` |
| CLAP plugin | `C:\Program Files\Common Files\CLAP\STYM2149.clap` |
| Standalone application | `C:\Program Files\PRISM\STYM2149\STYM2149.exe` (the folder can be changed during installation; an update removes the former `C:\Program Files\Indus`) |

The application folder also receives `LICENSE.txt`, `README.md`, `README.fr.md`, `THIRD_PARTY.md`, this manual in English and French (`docs` folder) and a `licenses` folder. The installer adds a Start menu group (STYM2149 and its uninstaller); a desktop icon is optional. Installing a new version replaces the previous VST3 bundle as a whole, and removes what the per-user installer of earlier test builds left: its plugin copies in `%LOCALAPPDATA%\Programs\Common`, its application in `%LOCALAPPDATA%\Programs\STYM2149`, its Start menu group and its desktop icon.

**Which build is installed.** Each build has a number, that of the CI run that compiled it: Fuji > Information shows it after the version with its commit (for example `0.9.0 build 412 (fe71c0d)`), and the installer's name carries it (`STYM2149-0.9.0-build412-windows-x64-setup.exe`). A build compiled on your own computer shows `build dev`.

**Make your DAW find the plugin.** `C:\Program Files\Common Files\VST3` and `…\CLAP` are the standard plugin folders of the VST3 and CLAP specifications, which DAWs scan by default. If STYM2149 does not appear, rescan the plugins; if it still does not, check that your DAW's plugin folder settings include these folders.

| DAW (Windows) | Format to use | Notes |
|---|---|---|
| FL Studio | VST3 (CLAP: **TO CONFIRM**) | |
| Ableton Live 11 | VST3 | Live 11 has no CLAP support |
| Cubase | VST3 | |
| ACID Pro | VST3 (**TO CONFIRM**) | |
| Maschine | VST3 (**TO CONFIRM**) | STYM2149 has no VST2 version |
| Reason | VST3 (**TO CONFIRM**) | No Rack Extension version |

In the plugin list, STYM2149 appears as an instrument from the vendor **PRISM**.

**Windows SmartScreen.** The installer is not code-signed. Windows may show "Windows protected your PC". Click **More info**, then **Run anyway**. This warning is expected for unsigned open-source software.

**Uninstall** from Settings > Apps (STYM2149), or with "Uninstall STYM2149" in the Start menu. The uninstaller removes the installed files; your settings and libraries (section 9) are not part of the installation and stay.

### 1.2 Linux (x86_64)

```sh
tar xzf STYM2149-0.9.0-linux-x86_64.tar.gz
cd STYM2149-0.9.0-linux-x86_64
./install.sh
```

`install.sh` needs no root access and touches nothing outside your home folder:

| Component | Installed to |
|---|---|
| VST3 | `~/.vst3/STYM2149.vst3` |
| LV2 | `~/.lv2/STYM2149.lv2` |
| CLAP | `~/.clap/STYM2149.clap` |
| Standalone | `~/.local/bin/STYM2149` |
| Menu entry and icon | `~/.local/share/applications/stym2149.desktop`, `~/.local/share/icons/hicolor/*/apps/stym2149.png` |

Extract the whole archive first: the script stops if a file is missing next to it. If `~/.local/bin` is not in your `PATH`, the script says so; start the application with its full path. Then let your host rescan its plugins.

`./install.sh --uninstall` removes what `install.sh` copied. Your settings file stays.

The archive also contains `README.md`, `README.fr.md`, `LICENSE`, `THIRD_PARTY.md`, this manual in English and French (`docs` folder) and a `licenses` folder.

Runtime libraries: the plugins and the application use ALSA, FreeType, Fontconfig and the C and C++ runtimes, and need an X11 display for the window. The minimum distribution and glibc version are **TO CONFIRM**.

### 1.3 macOS and AAX

macOS (AU, VST3, CLAP, Standalone) and AAX are not part of version 0.9.0. They are planned for version 1.0.0.

---

## 2. Getting started

### 2.1 First sound

1. Insert STYM2149 as an instrument on a MIDI or instrument track.
2. Open its window. A new instance starts in **16 ch** mode, with track 1 on and the others off, Chip Voice generator, square tone, volume 13.
3. Play notes from your MIDI keyboard or the DAW's piano roll. By default (MIDI routing **Omni**), incoming notes play the focused track, which is track 1.
4. You can also click the on-screen keyboard at the bottom of the window.

Notes outside the chip range (B0 to C9) do not play on melodic tracks.

### 2.2 The demo pattern

File > Demo pattern writes a short pattern into the current pattern: an arpeggiated bass on track 1, a SID lead on track 2, a Chipdrum beat on track 3 (voice C in Authentic 3ch), and Sync Buzzer stabs on track 4 (16 ch only). If tracks 1 to 4 already contain data, STYM2149 asks before overwriting them.

In a DAW the sequencer follows the host transport: start playback in your DAW to hear the pattern (section 5.9). In the standalone application, press Space or the Play button of the Step Sequencer.

### 2.3 Voice modes

The **Voices** selector in the SYSTEM frame (also a host parameter) chooses how tracks map to chips:

| Mode | Tracks | Typical use |
|---|---|---|
| **16 ch** | 16 tracks, each with its own panning and polyphony (up to 6 voices per track) | Production; YM sound without the 3-voice limit |
| **Authentic 3ch** | One YM chip: voices A, B, C plus a Digidrum track (tracks 1 to 4); mono output | Music that a real Atari ST can play; required for register exports (VBL, YM3, YM6, SNDH) |
| **Dual chip 2x3ch** | Two chips: A1, B1, C1, Digi1, A2, B2, C2, Digi2 (tracks 1 to 8) | Two YM chips |

The **Computer** setting (ST or STE) adds the STE DMA sound: with STE, Authentic 3ch gains eight DMA tracks (tracks 5 to 12), Dual chip gains eight DMA tracks (9 to 16), and in 16 ch any track can use the DMA generator.

Changing the voice mode or the computer stops all notes.

### 2.4 The standalone application

The standalone application works like the plugin. Differences:

- Options > Audio/MIDI settings opens the audio and MIDI device settings.
- The MIDI indicator in the menu bar shows "no inputs", the name of the input, or "n inputs".
- The sequencer runs on its own clock (Free run): Space, or the Play button, starts and stops it, and the BPM slider sets its speed.
- File > Quit (Ctrl+Q) closes the application after a confirmation; the close box of the system's window (and Alt+F4 on Windows) asks the same question. Quitting from the macOS application menu, or at the end of the session, saves and quits without asking.

### 2.5 Saving your work

- **In a DAW**, everything that makes the sound is saved in the DAW project: parameters, patterns, Song, instruments, sequences, Drum Kit, Digidrum and DMA samples, track names, the recorder's last take.
- **Not saved in the project**: window positions, the active tool, undo history and clipboards. They last as long as the plugin instance (closing and reopening the plugin window keeps them). Preferences (window size, tooltips, MIDI learn map, etc.) are per user, shared by every instance (section 9).
- **New**: File > New (Ctrl+N) starts again from a new project: every parameter, pattern, instrument, sample and the recorder's take back to their defaults. If anything is already defined, STYM2149 asks first. Preferences and window positions stay.
- **Files**: the File menu saves and loads presets, patterns, songs and drum kits as JSON files, and imports and exports other formats (section 8).

---

## 3. The main window

### 3.1 The GEM desktop

The window is a GEM desktop of 1344 × 984 logical pixels, drawn at your screen's resolution: at 100 % it is 840 × 615 pixels (5/8 of the desktop), at 160 % 1344 × 984, at 200 % 1680 × 1230, multiplied by your system's display scale. When a desktop pixel falls on a whole number of screen pixels, the drawing is pixel-exact. Otherwise the Pixels setting (Options > Settings, section 3.12) decides: **Sharp** puts each pixel of the letters and icons on whole screen pixels, crisp but some a little wider than others; **Smooth** (default) draws them with anti-aliasing, as a browser shows the prototype. If frames and lines look blurred too, not only the letters, the host is stretching the plugin window: look in its settings for the display scaling of plugins. To change the size, resize the plugin window in the host (or the standalone window), from 50 to 200 %; the aspect ratio stays fixed and the size you reach is remembered. There is no zoom menu. If you never resized the window, STYM2149 picks a size from your main screen (50 %, 75 %, 100 % or 125 %).

The menu bar runs across the top of the desktop, as on a GEM desktop (section 3.2); the main window starts below it, and no window goes above it. The icons of floppy disks A and B lie side by side at the top left of the desktop, the trash at its bottom right, named in the tooltip language and with the pictures of the EmuTOS desktop: a 3.5-inch floppy disk with the drive letter on its label, a trash can. A click on the trash is File > New, with its confirmation; A and B are only decorative, a click or a double click on them does nothing. Hold the button on an icon to drag its dotted outline elsewhere: as on a GEM desktop, it jumps from cell to cell of an invisible grid of the icons' size, and an icon does not land on the cell of another (its place is a preference, section 9).

Floating windows open on top of the main window. Drag a window by its title bar (the main window too, which keeps its place while the plugin instance lives); no window goes past the edges of the desktop, and one that grows near its right or bottom edge, or opens at a place saved on a larger desktop, moves back inside; close it with the box in its title bar or with Esc (Esc closes the window in front, except the Computer Keyboard and Slide Curve windows). Most windows show the name of the focused track in their title and follow the focused track. **Tab** brings the window at the back to the front, so that the open windows come forward in turn (the main window takes its turn); Shift+Tab goes the other way. A menu opened from a button lies on the button's bottom rule, with no gap, as GEM menus do.

Tooltips appear when you rest the pointer on a control. The Tooltips and Language settings of Options > Settings turn them on or off and choose their language (sections 3.12 and 9). Menus and labels are always in English.

Sliders are dragged with the mouse. The value next to every slider can be clicked and typed into (underscores mark its free positions); press Enter to apply it, Esc to leave it as it was. A value out of range is brought back to the slider's end. A note (a root note, the fixed note of the sequences) is typed by its name, in either naming (`C#4`, `Sol3`), or as a MIDI note number. A horizontal slider (except the Mixer's pans) has an arrow at each end: a press moves the value by one step, again and again while you hold it.

Dialog boxes keep the keyboard while they are open. A confirmation before something that cannot be undone (Clear, Demo pattern, New, Quit, deleting a take or a bank) shows **Cancel** first, framed as the default: Enter and Esc both cancel, whichever button has the focus, and only a click on the action confirms. In a box where you type a name or a number, Enter means OK and Esc means Cancel. Tab and Shift+Tab move between the buttons (and the text field).

### 3.2 Menu bar

| Menu | Entries |
|---|---|
| Atari (Fuji) menu | **Information** (icon, version, build number and credits; Enter closes it); **Shortcuts** (^?): the SHORTCUTS window, a scrolling list of every shortcut |
| **File** | New (^N); Load, Save, Save as (each: Preset, Pattern, Song, Drum kit); Import (YM dump, MIDI file, XM module); Export pattern (MIDI, WAV, Stems (ZIP)); Export song (VBL dump, YM3, YM6, SNDH, MIDI, WAV, Stems (ZIP)); Recorder; Demo pattern; Quit (^Q, standalone only) |
| **Edit** | Step Sequencer (^E), Arpeggiator (^B), Mixer (^M), Song (^G), Modulation (^R), Effects (^F), Patches (^P), Drum kit (^K); Cut (^X), Copy (^C), Paste (^V); Undo (^Z), Redo (^Y); Instruments (^T), Samples (Digidrums, DMA (STE)) |
| **Options** | Audio/MIDI settings (standalone only); MIDI learn / Stop MIDI learn; Clear MIDI map; Oscilloscope (On, Off); Register monitor (On, Off); Note monitor (On, Off); Settings... (the SETTINGS window, section 3.12) |

Greyed entries tell why in their tooltip. Arpeggiator and Modulation are greyed on percussion tracks (Digidrum, Chipdrum). Drum kit (and Ctrl+K) works on Chipdrum tracks only. Samples > DMA (STE) is greyed with Computer ST. The SID wave and the Sync Buzzer steps are set in the GENERATOR frame (section 3.8). The register exports (VBL dump, YM3, YM6, SNDH) are greyed unless the voice mode is Authentic 3ch. Edit > Cut and Copy act on the sequencer selection, or on the note under the sequencer cursor.

Two indicators sit at the right end of the menu bar:

- **CHIP: OK** or **CHIP: n** — chip conformity of the current pattern. Click it to list what VBL, YM3, YM6 or SNDH exports would not reproduce as you hear it (section 8.4).
- **MIDI: host** in a plugin (the host sends MIDI); in the standalone application, the state of the MIDI inputs.

### 3.3 Track tabs

Sixteen tabs select the **focused track**, the one the panels below edit and the on-screen keyboard plays. The panels stand in four columns: SOURCE and VIBRATO; ENVELOPE, ADSR and ADSR ENVELOPE; GENERATOR; SYSTEM.

- Click a tab to focus its track. Click the focused tab again to switch the track on or off. A track that is off does not sound.
- **All on** and **All off** switch every track.
- Right click a tab: **Copy settings**, **Paste settings** (the track's sound, arpeggio and level; not On, Pan, Mute or Solo), **Rename...** (8 characters; an empty name shows the track number). In Authentic 3ch and Dual chip, the chip voices are named by the chip (A, B, C, Digi…) and cannot be renamed.
- In Authentic 3ch, the tabs after the four chip tracks are unused (or are the DMA tracks, DMA1 to DMA8, with Computer STE).
- ← and → move the focus to the previous or next track (section 10).

### 3.4 SOURCE (per channel)

| Control | Values | Effect |
|---|---|---|
| Source | Tone, Noise, Tone+Noise, Env, Tone+Env, Noise+Env, Tone+Noise+Env | Square wave, noise, both, or neither, with the 4-bit volume or the hardware envelope: an entry with "Env" also sets Mode to Envelope, the others to Vol 4-bit. **Env** alone plays the envelope's own waveform at its frequency (ENVELOPE frame), without following the note; for a buzzer that follows the note, use the Sync Buzzer generator. In Authentic 3ch these are the chip's own settings (R7 and bit 4 of R8 to R10); the chip has a single envelope, so tracks in Env share its shape and period |
| Voicing | Polyphonic, Monophonic | Polyphonic: chords up to 6 voices; a note goes on to its release while the next ones play, the keyboard's and the arpeggiator's (4.2) as the Step Sequencer's (6 voices per track there, the oldest release giving way). Monophonic: one note at a time; a new key held glides to the new note instead of retriggering. Not available in Authentic 3ch (one voice per channel) |
| Noise period | 1 to 31 | Low is bright, high is rumbling. In Authentic 3ch the chip has one noise generator: the value applies to all tracks |
| Volume | 0 to 15 | The chip's 4-bit volume |
| Mode | Vol 4-bit, Envelope | Fixed 4-bit volume, or volume driven by the hardware envelope; Source follows (Tone becomes Tone+Env, and so on). Vol 4-bit on Env alone also turns the square wave on |
| Level | 0 to 100 | Channel level in the mix (in Authentic 3ch, the level comes from the 4-bit volume) |

On a DMA, Digidrum or Chipdrum track, Source, Noise period, Volume and Mode are greyed out, and so is the whole ENVELOPE frame: these tracks play their own sound (a sample, or the frames of the Drum Kit), without the square wave, the noise, the fixed volume or the hardware envelope of this frame. Voicing and Level still act.

### 3.5 ENVELOPE (per channel; "shared" in Authentic 3ch)

The YM2149 has one hardware envelope. In Authentic 3ch it is shared by the three voices.

| Control | Effect |
|---|---|
| Env freq | Envelope repeat rate. The slider is geometric and runs over its whole row; its value, in Hz, is at the end of the row of EP = |
| EP = | The envelope period register (R11–R12), 1 to 65535. Type a decimal number or a hexadecimal one with `$` (default 12800, `$3200`) |
| Shape R13 | The shapes `$8` to `$F` of register R13 (`$0` to `$3` sound as `$9`, `$4` to `$7` as `$F`; a lower value from a YM import or a MIDI CC is kept, and the menu marks the shape it plays as). Used only when Mode is Envelope, or with the Sync Buzzer generator |

When **Tuning** is on in the GENERATOR frame of a Sync Buzzer track, the frame shows "tuned xN/D" and the envelope period follows the note; Env freq, EP = and their values are then greyed out (section 3.8). The tuning only acts on a Sync Buzzer: on a Chip Voice track the envelope plays EP.

### 3.6 VIBRATO (per channel)

**Vibrato** On or Off (default On; a host parameter, automatable): Off silences the track's vibrato and greys Depth and Rate out. The VIB row of the sequencer (`Vxx`) switches it off and on from a line of the pattern (section 5.5). Depth 0 to 200 cents; Rate 1 to 20 Hz (steps of 0.5 Hz, default 6 Hz). The MIDI modulation wheel adds vibrato depth too, even with Vibrato Off (section 7.2). Percussion tracks (Chipdrum, Digidrum) play no vibrato: the frame is greyed out.

### 3.7 ADSR (per channel)

| Control | Smooth mode | Chip mode |
|---|---|---|
| A (Attack) | 0 to 2000 ms | VBL ticks (0 to 100) |
| D (Decay) | 0 to 2000 ms | VBL ticks (0 to 100) |
| S (Sustain) | 0 to 100 % | 4-bit level (0 to 15) |
| R (Release) | 0 to 2000 ms | VBL ticks (0 to 100) |

At 50 Hz, the 100 ticks of A, D and R in Chip mode are the 2000 ms of Smooth.

The sliders show the initials; their tooltips name each setting (in a translated tooltip, followed by its English name in parentheses).

**Mode**: Smooth gives a continuous curve (an audio gain); Chip writes 4-bit volume steps at the VBL rate, as Atari players do. In Authentic 3ch, Chip is forced (except on DMA tracks). With the Sync Buzzer generator in Chip mode the level comes from the hardware envelope, and percussion tracks play each hit to its end: the ADSR has no effect there, and the ADSR and ADSR ENVELOPE frames are greyed out. The **ADSR ENVELOPE** frame draws the curve and shows a point for each note sounding on the track: in Polyphonic, a note still in its release keeps its point while the next ones get theirs. In Monophonic, and on every track of the 3-channel modes, there is one point, the newest note's. A note released before its sustain falls from the level it reached: its point goes on down the decay line to the sustain level, then along the release line. With the sustain at 0 %, it goes down the decay line to its end, where the note stops.

### 3.8 GENERATOR (per channel)

**Type** chooses the sound generator of the track:

| Generator | Key | What it does |
|---|---|---|
| Chip Voice | Ctrl+V | The plain YM voice: square, noise, envelope |
| SID Voice | Ctrl+S | Timer effect that switches the volume between the note volume and a low volume (Atari "SID" sound). **Timer ratio** 0.25 to 8.00 × (1 plays the note, 2 an octave higher); **SID min vol** 0 to 15. Under them, the **SID wave**, its form chosen in the menu after its name: **Base**, the plain square wave, without settings: its bars and Steps are hidden, the menu alone in their frame; **Square**, a square of Steps steps, the note's volume over the first **Width** of them (1 to Steps - 1, half and half by default), SID min vol over the rest; **Sine**, a sine of **Period** bars (2 to 16, 8 by default) repeated along the 16 bars, none of them at 0 (a sine of the volumes, as the bars show them; the chip's level, logarithmic, makes it brighter than a pure sine); **Bounce**, the same with Period, a sine of the chip's level instead, which sounds as a pure sine and which the bars show as a bounce, flat at the top and sharp at the bottom; **Custom**, the bars drawn with the mouse. The bars of Square show the square played and do not answer the mouse. Each form keeps the settings and bars of the others. **Steps** (2 to 16, 8 by default), under the bars, its label before its slider, sets the number of bars played, one per timer interrupt, scaled by the note volume: the timer runs Steps/2 times faster, so the pitch stays; the bars beyond it stay greyed and are not played. The Steps play one period of the note, so the pitch follows the cycles they hold: Period 8 with 16 Steps sounds an octave above, 8 Steps at the note's pitch. Right under Steps, in the same place, Period with Sine or Bounce and Width with Square, the three sliders lined up. Sine or Bounce from Base sets as many Steps as their Period, one cycle that plays the note; Custom from Base or Square, its bars never drawn, starts from the square played. With Sine or Bounce, a bar moved shifts the sine along the bars to the place that brings that bar nearest the volume asked for, its volumes unchanged (a notch of the wheel goes to the next volume the sine can give the bar); Custom frees the bars as they are. Drag or use the mouse wheel on the bars; a right click opens the bar's slider (a right click on the slider sets it back to 0) |
| Sync Buzzer | Ctrl+U | Timer effect that restarts the hardware envelope at the note frequency. Choosing it sets EP to 32 if EP was above 3200. **Tuning** On or Off: the envelope period follows the note, and the envelope plays the note × **N** / **D** (1 to 16 each) plus **Fine** (−100 to +100 cents). **YM Pad**: the tone runs at period 0 under the envelope, about −6 dB. **Presets** set the shape, the tuning and YM Pad in one click, the track staying a Sync Buzzer: Saw, Triangle, Third, Fifth, Seventh, Tenth, Tri fifth, Soft saw. Under them, the **Sync Buzzer steps**: 0 to 8 envelope shapes (R13) written in turn at each restart (**Steps** at 0: the track's shape every time). The timer runs as many times faster as there are steps, so that they all fit in one period of the note and the track keeps the pitch of the note. Each button shows its shape, or "Trk" for the track's shape: click it to choose a shape, right click to set it back to the track's shape. These settings act only while the track plays a Sync Buzzer |
| Chipdrum | Ctrl+C | Synthesised drums: the eight instruments of the Drum Kit (button **Drum Kit**, kit name shown) |
| Digidrum | Ctrl+I | 4-bit samples played by a timer through a chip voice. **Slot** 1 to 8; **ST channel** A, B or C (Authentic 3ch: the voice the Digidrum track borrows); **Digidrum samples**, under slots 5 to 8, opens the Digidrums window to load and edit the samples; the waveform of the slot below |
| DMA | Ctrl+D | The STE DMA sound (Computer STE only). **Sample** 1 to 8; **DMA samples**, under samples 5 to 8, opens the DMA window; the waveform of the sample below |

In Authentic 3ch and Dual chip, track roles are fixed: the fourth track of each chip plays Digidrums only, DMA tracks play DMA only, and the chip voices cannot be Digidrum tracks. The generator keys only switch to a type the track may use.

### 3.9 SYSTEM

| Control | Effect |
|---|---|
| Voices | 16 ch, Authentic 3ch, Dual chip 2x3ch. Changing it stops all notes |
| Computer | A menu: **ST** (the YM alone) or **STE** (the YM plus the DMA sound) |
| Clock | Clock of the arpeggiators and the Step Sequencer: **Internal** (arpeggio rate in Hz; the sequencer follows the host in a DAW, its own tempo in the standalone application) or **MIDI ext** (both follow MIDI clock: arpeggio steps at the chosen division, sequencer started by MIDI Start, see 5.9). With MIDI ext, a status shows "waiting for clock...", then SYNC or STOP |
| Master | Output level, 0 to 100 (default 70) |
| Voices (LEDs) | One LED per voice, lit while it sounds |
| Note | Tone period and frequency of the last note played ("TP=n -> f Hz"), while a voice sounds; "-" once every note has ended, its release included, as at launch. Options > Note monitor > Off hides the line (a preference); nothing moves |

### 3.10 Window buttons, scope and register monitor

The buttons **Mixer** (Ctrl+M), **Arpeggiator** (Ctrl+B), **Modulation** (Ctrl+R), **Effects** (Ctrl+F), **Step Sequencer** (Ctrl+E) and **Patches** (Ctrl+P) open their windows. The row of buttons is centred in the window.

The **oscilloscope** shows the output waveform; Options > Oscilloscope > Off hides it (a preference), and what lies under it moves up, the window becoming 100 pixels shorter. The **register monitor** shows the YM registers R0 to R13 in hexadecimal and, on the right, the focused track and its generator (CHIP VOICE, SID VOICE, SYNC BUZZER, DIGIDRUM, CHIPDRUM or DMA; or "INS nn" when a Program Change has selected an instrument), with "3CH AUTH (shared env+noise)" or "16CH". While the YM Player plays, it shows "YM PLAYER". Options > Register monitor > Off hides it (a preference), as the oscilloscope: the keyboard or the pads and their buttons move up, the window becoming 50 pixels shorter.

### 3.11 On-screen keyboard and pads

- The piano plays the focused track. Click to play; drag with the button held to slide to other keys. Right click to choose which track the keyboard plays.
- On a percussion track (Chipdrum, Digidrum) the piano is replaced by **8 pads**. On a Digidrum track, a pad also selects its slot.
- **Latch**: keys stay held until you press them again.
- **Layer**: play every enabled track instead of the focused one (sets MIDI routing to Layer; off returns to Omni).
- **Oct -** / **Oct +**: octave of the piano (0 to 6); keys Z and X do the same.
- **Computer keyboard**: opens the Computer Keyboard window (section 4.6). Your computer keyboard plays notes (or the pads, on a percussion track) only while that window is open.
- The help line under the keyboard recalls the main keys: Space = Play/Stop, 1-8 = Pattern, Shift+1-4 = Bank, ←→ = Channel, M = Metro, Ctrl+Z/Y = Undo/Redo, Esc = Panic.

### 3.12 Settings window

Options > **Settings...** opens the SETTINGS window. Each setting is a label and a button that shows its value: click the button for the list of values, the current one checked. A value that does not apply is greyed out, and its tooltip says why. The window keeps its place, and it is open again at the next opening of the editor if you left it open.

A change applies at once, so that you hear it. **OK** keeps the changes and closes the window. **Cancel**, the default button (bold border, also pressed by Enter), puts back every setting changed since the window opened, to the value it had before, then closes the window; the close box and Escape do the same. When a setting differs from its value at the opening, a box first asks "Discard the changes?": its Cancel, the default (Enter, Escape), keeps the window open with the changes, OK puts them back and closes it. A setting changed and then set back to its value asks nothing. Closing the editor keeps the changes.

**SOUND ENGINE** (left frame). These settings are saved with the project; all but Quantize to VBL are also host parameters (the Custom clock's frequency and Tuning A4 are ones the host cannot automate).

| Setting | Values | Notes |
|---|---|---|
| Computer | ST, STE | Same as the SYSTEM frame |
| Chip clock | Custom (default, 2.0000 MHz), Atari ST 2.0053 MHz (PAL), ZX Spectrum 1.7734 MHz, Amstrad CPC 1 MHz | The button shows the clock in use. The first entry, **Custom** with its frequency, opens a box where you type the frequency in MHz, 0.1000 to 8.0000 (up to four decimals; a comma is read as a point). It applies in 16 ch only: in Authentic 3ch and Dual chip the entry is greyed out and the chip runs at 2 MHz. ZX Spectrum and Amstrad CPC are greyed out in Authentic 3ch. Only changes the chip frequency; the ZX Spectrum and Amstrad CPC are not emulated. The Atari ST value (a PAL machine) comes from emulator sources: **TO CONFIRM** on hardware |
| Tuning A4 | 350.00 to 480.00 Hz (default 440.00 Hz) | The frequency of A4 (MIDI note 69), from which every note is tuned: Chip Voice, SID Voice, Sync Buzzer, the slides, and the Digidrum and DMA samples; the Chipdrum sounds keep their pitch. Click the value and type it in Hz, two decimals at most (a comma is read as a point); a value out of range is brought back to 350 or 480 Hz. The chip plays whole periods only, so a note sounds at the nearest period: at 2 MHz, A4 at 440 Hz gets the period 284, which plays 440.14 Hz |
| Output chain | Raw (default), ST, STE | Raw: the chip alone. ST: the analogue filter of the Atari ST board. STE: the STE output with bass and treble controls; greyed out with Computer ST. Filter component values: **TO CONFIRM** on hardware |
| Resampling | Sinc (default), Box (AtariAudio), Retro | How the chip's internal rate is brought to the host rate. Box and Retro fall back to Sinc when the host rate is above clk/8 (for example 176.4 or 192 kHz with the Amstrad CPC clock) |
| STE bass, STE treble | −12 to +12 dB, steps of 2 dB | STE output chain only; greyed out with Computer ST |
| Soft clip | On (default), Off | A tanh saturation on the output |
| Timer | MFP (default), Free | Timer effects quantised by the Atari MFP 68901, or continuous. Free is greyed out in Authentic 3ch |
| VBL frequency | 50 Hz (PAL, default), 60 Hz (NTSC), 70 Hz (Monochrome), 100 Hz, 200 Hz | The replay tick: it paces the Chip ADSR, the Modulation tables, the Ticks timing of the sequencer and the register exports. It was in the Options menu before |
| Quantize to VBL | Off (default), On | Notes, MIDI events, sequencer steps and arpeggios take effect only at the start of each replay frame (VBL frequency), as an Atari replay routine would play them; the tempo does not drift |

**MIDI** (middle frame). Saved with the project, and host parameters (not automatable).

| Setting | Values | Notes |
|---|---|---|
| MIDI routing | Omni, Layer, Per channel | Section 7.1 |
| Velocity | Accent, Vol 4-bit, Off | Section 7.2 |
| Bend | Pitch (default), Volume, Envelope | What the pitch bend drives, section 7.2 |
| Bend range | 0 to 24 semitones (default 2) | |
| Wheel | Off, Vibrato (default), Volume, Envelope, LFO | What the modulation wheel drives, section 7.2 |
| Wheel depth | 0 to 200 cents (menu in steps of 25; default 50) | The vibrato of the wheel, the aftertouch and the MPE slide at their top |
| Aftertouch | Off, Vibrato (default), Volume, Envelope, LFO | What the key pressure drives, section 7.2 |
| MPE | Off (default), On | 16 ch only, section 7.2 |
| MPE bend range | 0 to 96 semitones (menu: 1, 2, 3, 4, 5, 7, 12, then steps of 12; default 48) | Greyed out while MPE is off |
| MPE slide (CC74) | Volume (default), Envelope, Vibrato | Greyed out while MPE is off |

**INTERFACE** (right frame). Your preferences, shared by every STYM2149 instance (section 9): Pixels (section 3.1), Renderer (Windows only, greyed out elsewhere), Note names, Keyboard layout (section 4.6), Tooltips (On, Off), Language, and the **Languages folder...** button.

---

## 4. Floating windows

| Window | Open with | Section |
|---|---|---|
| Step Sequencer | Step Sequencer button, Edit menu, Ctrl+E | 5 |
| Song | Song button of the Step Sequencer, Edit menu, Ctrl+G | 5.11 |
| Mixer | Mixer button, Edit menu, Ctrl+M | 4.1 |
| Arpeggiator | Arpeggiator button, Edit menu, Ctrl+B | 4.2 |
| Modulation | Modulation button, Edit menu, Ctrl+R | 4.3 |
| Recorder | File menu | 4.4 |
| YM Player | File > Import > YM dump | 4.5 |
| Computer Keyboard | Computer keyboard button | 4.6 |
| Slide Curve | Custom entry of the CRV row | 4.7 |
| Effects | Effects button, Edit menu, Ctrl+F | 4.8 |
| Scope | Click a track's mini-scope in the Mixer | 4.1 |
| Instruments, Patches, Drum Kit, Digidrums, DMA | Edit menu, Ctrl+T, Ctrl+P, Ctrl+K | 6 |

### 4.1 Mixer

One strip per track (in Authentic 3ch: A, B, C and Digi, eight strips with two chips, plus the DMA tracks with Computer STE). Each strip has:

- the track name (click to focus that track);
- a mini-oscilloscope (click to open a larger **Scope** window for that track);
- a vertical fader, 0 to 100; for chip voices in Authentic 3ch and Dual chip, whose level is the 4-bit volume, the fader is that **Volume** of the SOURCE frame, 0 to 15, and moves with it (greyed out on a Sync Buzzer, which has no fixed volume);
- a level meter. A DMA track playing a stereo sample has two oscilloscopes in the frame, left then right (a click on either opens the Scope), and two meters, left and right;
- a pan slider, −100 to +100 (double click centres it; absent for chip voices in Authentic 3ch, since the ST is mono);
- in 16 ch, a **Send** slider, 0 to 100 %: how much of the track, after its fader and pan, goes to the delay (section 4.8); like the pan, Paste settings does not copy it. The 3-channel modes have no Send row, the delay being a 16-channel effect;
- **M** (mute) and **S** (solo).

A DMA track playing a stereo sample has two of each: two faders, each with its meter and its value, the left side's then the right side's, an **M** and an **S** under each, and a **Link** button on a row under them (the window grows by that row). Linked (the default), either fader sets the track's Volume and both move together; unlinked, the right fader is the right side's own level (host parameter Level R), which starts at the Volume. The M and S of each side are independent, linked or not: mute the left side alone, or solo the right side alone (host parameters Mute R, Solo R and Link). These settings belong to the Mixer, like Send: a patch, Paste settings and the preset files leave them out. On a mono sample they do nothing and are not shown.

After a vertical rule, the **Master** fader is the main level, the same as Master in the SYSTEM frame. Where the output is stereo (16 channels, and Authentic 3ch with the STE), Master has two faders, **L** and **R**, a balance slider on the row of the pans (double click centres it) and a **Link** button on the row of M. Linked (the default), either fader sets both sides; unlinked, each fader sets its side (host parameters Master and Master 2). The balance lowers the opposite side only. In Dual chip 2x3ch, the two faders are **1** and **2**, one per chip, with Link and without balance: unlinked, Master sets chip 1 and Master 2 chip 2; the DMA tracks of the STE follow Master. In Authentic 3ch with the ST, the output is mono and Master has one fader.

Under each fader, pan, Send and Master, its value is shown in small figures, named under a pan or a Send ("Pan +25", "Send 50"; pans and balance signed, + to the right). A right click on one of these sliders opens a menu: **Value...** to type a number within its range, **Reset** to bring back its default. A click on the value itself types it in place: type the number, Enter confirms, Escape cancels (under a pan or a Send, only the number is typed).

Output meters: Out L and Out R, or a single Out in Authentic 3ch without the STE. Each fader, pan or Send drag, typed value or reset, and each M, S or track Link click, is one undo step (the Master faders, the balance and Link, like the SYSTEM frame's Master, have none).

### 4.2 Arpeggiator

The arpeggio of the focused track (not available on percussion tracks).

| Control | Effect |
|---|---|
| Off / On | Enables the arpeggiator for this channel (50 Hz = VBL rate) |
| Mode | Up, Down, UpDn, Order, Random, Table |
| Sync | Lit: the arpeggio follows the tempo, one step per Division: in a DAW, the host's tempo, on its beat grid while it plays; standalone or in Free run, the sequencer's tempo. Unlit: Rate. Host parameter Arp Sync, automatable. With Clock MIDI ext, lit and greyed: the MIDI clock |
| Rate | 4 to 200 Hz (Clock Internal, Sync unlit) |
| Track | "Slide follows": during a glissando, the arpeggio rate rises with the pitch |
| Land | On the last tick of a glissando, fall back to the root so the target note joins without a jump |
| Division | Menu button: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64, and Tick (one MIDI clock pulse) on Clock MIDI ext only (Sync lit or Clock MIDI ext, replaces Rate; on the row of Mode, after Sync) |
| Octaves | 1, 2 or 3 (not in Table mode) |
| Steps | Table mode: 1 to 16 steps |
| Chord... | Table mode: fills the table with one of 39 chords |
| Grid | Table mode: 16 steps × semitone offsets +0 to +24, applied to the last played note |

With the window in front and the arpeggiator on, the arrow keys move the table cursor and Enter writes the step.

The keys you hold are arpeggiated as one note per key pressed (or MIDI note), not one per step: the envelope of that note runs on through the steps of the arpeggio, and its point on the ADSR ENVELOPE curve with it; each key pressed starts a new note from the attack. In 16 channels with a Polyphonic **Voicing**, the note before then goes on to its release while the new one plays: with a long Release they overlap, each with its point on the curve. With Monophonic, and on every track of the 3-channel modes, the note before is cut. Keys up, the last note keeps cycling through the arpeggio during its release, the ones before ending theirs at their pitch. In the Step Sequencer, each note of the pattern is one note of the arpeggio in the same way (ARP row, 5.5). The Modulation table, the vibrato and the LFO run on from one step of the arpeggio to the next.

### 4.3 Modulation

Per-VBL-frame tables of the focused track (the tracker "ornament"), and the sequence rows played with them. Not available on percussion tracks.

| Control | Effect |
|---|---|
| Off / On | Enables the table and its sequence rows for this channel |
| Len | 1 to 64 steps |
| Loop / Hold | Loop the table, or hold its last step |
| 1/frame … 4/frame | Table speed: VBL ticks per step |
| Clear | Clears the table and its sequence rows (one undo step) |
| Pitch rows | +12 to −12 semitones per step |
| NSE row | Adds noise on that tick |
| VOL row | 4-bit level on that tick (right click: precise slider) |

Click or drag to draw. With the window in front and the table on: arrows move the cursor, Enter writes the pitch, Delete resets the step, N toggles noise.

While the table is on, the sequence rows inside it, between the pitch rows and NSE / VOL, play with it, tick by tick (same length, loop and speed):

| Row | Content |
|---|---|
| PER | Tone period offset, −127 to +127 (+ is lower). Drag to draw, mouse wheel to adjust (Shift: by 10), right click for a precise slider |
| NP | Noise period, 0 to 31. Same handling as PER |
| MIX | Channel mode 0 to 7 (Off, Tone, Noise, Tone+Noise, Env, Tone+Env, Noise+Env, Tone+Noise+Env), or the track mixer. Click to choose |
| GEN | Note's generator, Chip Voice, SID Voice or Sync Buzzer. Click to choose |
| FIX | The step plays the fixed note. Click to toggle |
| OUT | Read only: the pitch a C4 plays at each step |

Right click clears a MIX, GEN or FIX cell. The slider that a right click opens on VOL, PER or NP applies each move at once (one undo step for the whole slider); right click the slider to set the value back (15 on VOL, 0 on PER and NP), or click the value under it to type one. The notes at the top of the window describe all the rows. The settings below the table are always shown, since Detune, Analog and the pitch matrix also apply with the table off:

- **Fix**: the fixed note (0 to 127). **Detune**: −100 to +100 cents. **Analog**: random pitch offset drawn at each note, up to 50 cents.
- **Hard sync**: restart the square (16 ch) and the timer on tied and slid notes too.
- **Link**: the instrument that takes over when a Hold table ends.
- **Pitch matrix**: which targets (Sq: the square, Tm: the timer of SID and Sync Buzzer and the pitch of a DMA sample, Bz: the envelope period of buzzers) are moved by each source (Table pitch, Slides, Vibrato).
- **Vibrato table**: Min and Max period offsets, Cycles (1 to 15), Ramp (0 to 64 steps); **Write PER** writes it into the PER row.

On a DMA track, the table pitch, PER, FIX, the slides and the vibrato move the sample through the Tm column, and VOL sets its level. What moves nothing of a sample is greyed, its tooltip telling why: the NSE, NP, MIX and GEN rows, Hard sync and the Sq and Bz columns of the pitch matrix. Their values are kept for another generator.

**LFO** (below the settings): a slow wave that moves one setting of the track's sound, restarted at each note or running free (Retrig); it goes on across the notes of an arpeggio and into a linked instrument. It changes only the values written to the YM registers, so it also plays in Authentic 3ch and in the register exports. Not available on percussion tracks.

| Control | Effect |
|---|---|
| Off / On | Enables the LFO of the track; while it is Off, only the LFO label and this button are shown |
| Shape | Triangle, Sine, Square, Saw up, Saw down, Random (a new value held for each cycle), drawn on the button and in its menu |
| To | Volume (steps of 0 to 15), Noise period (0 to 31), Env period, SID ratio and Pitch (in cents, up to 2400), Pan (percent, 16 channels only). A target the track cannot move is greyed in the menu, its tooltip telling why: Volume for a Sync Buzzer or a source on the envelope (the 4-bit volume is ignored), Noise period without noise in the source (or for a Sync Buzzer or a DMA track), Env period without the envelope (Vol 4-bit, SID Voice, DMA), SID ratio outside SID Voice, Pan in the 3-channel modes. If the target chosen earlier no longer applies (the generator or the source changed since), the other settings are greyed and To tells why |
| Retrig | On (default): the LFO restarts at each note. Off: it runs free and each note takes it where it is; synced to the tempo while the host plays, it follows the host's beat grid. Free LFOs start again with the sequencer's playback |
| Sync | Off: Rate in Hz, 0.1 to 20. On: one cycle per division of the tempo, 4/1 to 1/64 (4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T, 1/64), at the host's tempo in a DAW, the sequencer's otherwise. At a VBL frequency of 50 Hz, a cycle faster than 25 Hz (1/64 from 94 BPM) folds back to a slower one: choose 100 or 200 Hz for the fastest divisions |
| Rate | Sync off: a slider in Hz; click its value to type `7.5`, or a division such as `1/8T`, which turns Sync on. Sync on: a menu button of the divisions |
| Depth | How far the setting moves on either side of its value, in the unit of the target; changing the target keeps its share of the range |

The Pitch target follows the Vibrato row of the pitch matrix (Sq, Tm, Bz). Each setting is one undo step; the LFO is saved with the project, the patches and the instruments.

### 4.4 Recorder

Records the YM registers of a chip while it plays: one frame of R0 to R13 per replay tick (VBL frequency).

- **Chip** 1 or 2 (2 in Dual chip only). **Rec** arms a take: it starts and stops with the host's playback (standalone: with the sequencer). Click again to stop.
- **Take** shows the frames, duration, rate and chip of the last take. The last take is saved with the project. **Clear** deletes it after a confirmation.
- **Export**: YM6, SNDH or VBL dump of the take (section 8.3). The recorder keeps no DMA sound.
- 16 ch has no YM registers: switch to Authentic 3ch or Dual chip. A take holds at most 131,072 frames (43 min 41 s at 50 Hz).

### 4.5 YM Player

File > Import > YM dump opens a tune (YM2 to YM6, LHA-compressed or not, or SNDH) in the YM Player window. It plays the tune instead of the instrument's output (the register monitor and scope follow it).

- **Play** / **Stop**, **Rewind**, position "frame / total (seconds)".
- **Subsong** − / + for SNDH files with several subsongs (restarts the tune).
- Closing the window stops playback and forgets the tune; it is not saved in the project.

### 4.6 Computer Keyboard

Plays the focused track from your computer keyboard. **Typing plays notes only while this window is open.**

On a percussion track (Chipdrum, Digidrum) the keys of the middle row, A S D F G H J K by position, play the 8 pads like the pads of the main window (a Digidrum pad also selects its slot). The drawing names each pad (Kick to Tom 2, or Slot 1 to Slot 8); the other keys, the octave buttons and Held notes do nothing there.

- Keys, by position on a QWERTY keyboard: A W S E D F T G Y H U J K O L P ; ' play from C to F of the next octave. Choose your keyboard with Keyboard layout in Options > Settings (Auto, QWERTY, AZERTY, QWERTZ; Auto follows the layout of your system, and counts as QWERTY for any other layout) so that STYM2149 maps the characters your keys type to these positions.
- **Oct -** / **Oct +** (keys Z and X): octave 0 to 9. The numeric keypad digits 0 to 9 choose the octave directly.
- **Held notes**: **Keep** (a held note keeps its pitch when you change octave) or **Follow** (it jumps to the new octave).
- The drawing shows which keys play, which change octave, which are shortcuts, and the range played. You can also click the drawn keys.
- Esc does not close this window. Closing it silences notes held from the screen.

### 4.7 Slide Curve

The **Custom** curve of the CRV row (section 5.6), common to all tracks: drag to draw (left = start of the slide, right = arrival). **Linear** resets it to a straight line, **Smooth** smooths it, **Invert** turns the whole curve upside down: the slide then runs from the target note back to the start. A warning appears when the curve does not reach the target note. Esc does not close this window, and its edits are not undoable.

### 4.8 Effects

Two effects in one window, EFFECTS (the Effects button, Edit > Effects or Ctrl+F).

**Echo**, in every voice mode: each note of the focused track's sound is played again, quieter each time. The repeats are notes of the chip, so Authentic 3ch plays them and the Recorder and the register exports keep them. The echo belongs to the sound: it is saved with the project, the patches and the instruments, and each setting is one undo step of the track.

| Control | What it does |
|---|---|
| Echo Off / On | Enables the echo of the track; while it is Off, only this row is shown |
| Division | Time between two repeats, a division of the tempo (1/8 by default) |
| To | Track that plays the repeats: the same track (default) or another one, with that track's sound (or the instrument the note played) |
| Repeats | 1 to 8 (3 by default) |
| Drop | Steps of the 4-bit volume lost at each repeat, 0 to 15 (3 by default); the repeats stop at volume 0 |

Each repeat lasts as long as the note was held. A Sync Buzzer gets no echo in Authentic 3ch and Dual chip: it plays the chip's single hardware envelope, which a repeat would restart at full level; the echo settings are greyed there (in 16 ch each voice has its own envelope, and the echo plays). In Authentic 3ch and Dual chip, a channel plays a repeat only while it plays no note of its own: a repeat of the same track waits for the note's release and takes its channel; a repeat for another track takes that track's channel when it is free (a Digidrum takes the digidrum channel); otherwise the repeat is skipped. In 16 ch, a repeat takes a free voice, or the oldest repeat; a new note takes a repeat's voice before an older note's.

**Delay**, in 16 ch only: an audio delay fed by the **Send** of each track in the Mixer (0 by default, so nothing is heard until a track sends to it). No ST could do it: in Authentic 3ch and Dual chip its row is greyed out and the sound is unchanged. Its settings are host parameters.

| Control | What it does |
|---|---|
| Delay Off / On | Enables the delay; while it is Off, only this row is shown |
| Ping-pong | The echoes go from one side to the other (shown while the delay is on) |
| Sync / Division | Synced (default): one echo per Division of the tempo (1/8D by default), at the host's tempo in a DAW, the sequencer's otherwise |
| Sync off / Time | Time in milliseconds, 10 to 2000 (375 by default) |
| Feedback | How much of each echo comes back in the next one, 0 to 90 % (35 %) |
| Hi cut | Each echo loses the treble above this frequency, 500 to 20,000 Hz (6000 Hz) |
| Level | Level of the echoes in the mix, 0 to 100 % (100 %) |

The echoes are added before the Master; the track outputs do not carry them.

---

## 5. The sequencer

### 5.1 Patterns, banks and tracks

- 32 patterns in 4 banks (A to D) of 8. Each pattern has 1 to 256 steps (**Steps** slider) and holds all 16 tracks.
- Click a bank, then a pattern (1 to 8). Keys 1 to 8 choose a pattern of the current bank; Shift+1 to Shift+4 choose bank A to D. While playing, a new pattern starts at the end of the current loop.
- Right click a pattern button: Copy, Cut, Paste, +1 semitone, −1 semitone, +1 octave, −1 octave.
- The grid edits the **focused track** (main window tabs).

### 5.2 The Step Sequencer window

The title shows "STEP SEQUENCER - track name [generator]". The window opens near the left edge of the desktop; when its rows make it taller than the desktop, it moves up so that its bottom shows, never above the menu bar. The window contains:

- **Tools**: Move, Draw (default), Erase, Split, Join, Select; **Copy**, **Cut**, **Paste**.
- **Step** (step input: a note you play is written at the cursor, which then advances), **Clear** (clears the current track after a confirmation), **Euclid** (Euclidean generator, 5.7), **<** and **>** (rotate the track), **-1** / **+1** (transpose by a semitone), **Oct-** / **Oct+** (transpose by an octave), **Follow** (the view follows the playhead), **Len =** (track length, 5.7), **Song** (opens the Song window).
- **Steps** (1 to 256), **BPM** (60 to 240, Free run only; a `Txx` command changes the tempo during playback, 5.5), **Gate** (5 to 95 %) and **All** (one gate for all tracks), **Swing** (50 to 75 %: 50 is straight, 66 a triplet shuffle).
- **Scale**: root, scale (Off and 25 scales, from Major and Minor modes to Blues, Whole tone and Chromatic) and **Snap** (notes are forced into the scale: a note or a chord voice written on a greyed row goes to the nearest degree). Out-of-scale rows are greyed.
- **Signature** (2/4 to 7/4, 5/8 to 12/8: accents and metronome follow it), **Resolution** (value of one step, and the Ticks timing, 5.8).
- **Note length** (figure written when you place a note, drawn on its button: Step, one step long, drawn as the note of the Resolution; 1/1 to 1/64, dotted values and triplets from 1/4T to 1/32T; the values that are not a whole number of steps of the Resolution are greyed, with their length unrounded, and a value chosen before that no longer fits writes one step until the Resolution fits it again), **Chord** (Off or one of 39 chords added above the note you place), **Voicing** (Polyphonic or Monophonic).
- **Ghost** (shows the other channels behind the grid; on by default) and **Overview** (a strip with one line per channel; under it, only the "Whole pattern" line replaces the legend of the grid).
- The **grid**: 13 note rows (one octave and one note) and 64 visible steps, separated by lines 2 pixels thick, with a scroll bar at the right edge of the window, as high as the whole grid, to scroll the notes from B0 to C9 (an arrow: a semitone; a click beside the thumb: an octave; the mouse wheel over the grid: a semitone) and a scroll bar for patterns longer than 64 steps. Under the note rows are the control rows (5.4).
- The **transport**: Play/Stop, Rec, Loop, Play from start, Previous and Next song entry, Metronome (click on every beat, bell on the first beat of the bar; heard, but not drawn by the oscilloscope or the Mixer's output meters; a right click opens its volume, 0 to 100 %, kept with the project); then the banks and patterns.

On percussion tracks the note rows become the eight instruments (Chipdrum: Kick, Snr, Clap, Hat, OHat, CHat, Tom1, Tom2; Digidrum: Sl 1 to Sl 8) and hits are one step long.

### 5.3 Writing notes

With the Draw tool:

- **Click** an empty cell to write a note (with the chosen Note length and Chord). Drag to the right on the same row to lengthen it; drag elsewhere to write more notes.
- **Click a note** to remove it (if its length is the current Note length) or to give it the current Note length.
- **Drag a note** to move it; a note of a selection (box or Shift+click) moves the whole selection. While the button is held on a note that can move, the pointer is a hand; it is the pencil again when you release it. Drag its **left or right edge** (the outer part of its first or last step, where the pointer becomes a double arrow) to resize it.
- On a polyphonic track, click another row of a step that already has a note to add a chord voice (up to 6 voices per step); click a chord voice to remove it.
- **Alt+click** a note splits it at that step.
- **Shift+click** a note adds it to (or removes it from) a multiple selection; **Shift+drag** on empty cells draws a selection box. On the control rows (and on percussion tracks), Shift+click selects a block of steps.
- **Right click** a note: Copy, Cut, Paste here, Longer ], Shorter [, Split here `\`, Join with next J, Length, Delete. Right click an empty cell when a note is in the clipboard: Paste note here.
- **Right drag** draws a selection box; right click it for Copy, Cut, Paste, Clear, Deselect. Right click a selected block of steps for Copy block, Cut block, Paste block, Clear block, Deselect, Note length, Channel, Rename channel....

The other tools: **Move** drags notes or a selection, by their edge too, since only Draw resizes a note (a click without a drag only places the cursor); **Erase** removes notes you click or drag over; **Split** cuts the clicked note in two at that step; **Join** merges the clicked note with the next one on the same row; **Select** draws a selection box.

With the Step Sequencer open, the keyboard edits too: arrows move the cursor (the view scrolls when you leave the top or bottom), Enter writes or removes a note at the cursor, Delete clears the step. With the window in front and a note under the cursor: `]` lengthens it, `[` shortens it, `\` splits it at the cursor, J joins it with the next note, Ctrl+C / Ctrl+X copy or cut it, Ctrl+V pastes at the cursor. With a selection box: arrows move it (Shift+↑/↓ by an octave), Delete clears it, Esc deselects it, Ctrl+C / X / V.

Each gesture is one undo step (Ctrl+Z, Ctrl+Y).

### 5.4 Control rows

Under the note rows, control rows set per-step options, in the order of what they do to the note: its pitch, its sound (GEN decides what SRC, ENV and SMP can do), its modulations, its slide, its timing in the step, the other commands, its place in the mix. The rows shown depend on the track:

| Row | Shown on | Values | Effect |
|---|---|---|---|
| OCT | Percussion | −3 to +3 | Tunes the drum by octaves |
| TON | Percussion | −12 to +12 | Tunes the drum by semitones |
| TUN | Melodic | −99 to +99 | Tuning of the track in cents from this step on (`Fxx`, 5.5) |
| INS | Melodic, when the project has instruments | Channel, $01 to $20 | Plays the note with a project instrument (6.1) |
| GEN | Melodic | Channel, Chip Voice, SID Voice, Sync Buzzer | Another generator on this step only |
| SRC | Melodic | Channel, Tone, Noise, Tone+Noise, Env, Tone+Env, Noise+Env, Tone+Noise+Env | Tone, noise or both on this step. Tone, Noise and Tone+Noise keep the volume mode of the track; the Env entries put the volume on the envelope (Env alone: neither square nor noise) |
| ENV | Melodic, when the envelope plays: Env source, Sync Buzzer generator, an Env entry in SRC or Sync Buzzer in GEN | No change, shapes 8 to F; Restart: None, At once, At tick | Hardware envelope shape of the track from this step on (`Exx`), and a restart of the envelope at a tick of the line (`Rxx`); the cell shows the shape and R (5.5) |
| SMP | DMA tracks | 0 to 255 | The DMA sample of this step starts that many steps of 256 frames after its Start (`Oxx`, 5.5) |
| ARP | Melodic, when the track's arpeggiator is on | No change, On, Off | Switches the track's arpeggiator on or off from this step on (`Jxx`, 5.5) |
| VIB | Melodic, with Vibrato On in the VIBRATO frame | No change, On, Off | Switches the track's vibrato on or off from this step on (`Vxx`, 5.5) |
| LFO | Melodic, with the track's LFO on (Modulation window) | No change, On, Off | Switches the track's LFO on or off from this step on (`Lxx`, 5.5) |
| MOD | Melodic, with the track's modulation table on (Modulation window) | No change, On, Off | Switches the track's modulation table on or off from this step on (`Mxx`, 5.5) |
| SLD | Melodic | on/off | Glides from this note to the next one |
| CRV | Melodic, while an SLD cell is lit (or a curve is written) | Linear, Exponential, Logarithmic, S-curve, Stepped, Sawtooth, Wobble, Chromatic, Scale-locked, Overshoot, Bounce, Random walk, Custom | Glissando shape; set it where a slide starts, or mid-slide to change it. Custom opens the Slide Curve window |
| CND | All | Always, 1:2, 2:2, 1:4, 4:4, 25%, 50%, 75% | Plays the step only on the 1st or 2nd of every 2 passes, the 1st or 4th of every 4, or with that probability |
| DLY | All | 1 to 255 | The notes of this step start at this tick of the line (`Dxx`, 5.5) |
| RTG | All | 1 to 4 | Repeats the hit 2, 3 or 4 times inside the step |
| GAT | Melodic | Channel, or 0 to 100 % | Note length on this step, as a share of it |
| HLD | Chipdrum | on/off | Does not retrigger the drum while it still rings |
| CUT | All | 0 to 255 | The notes of this step stop at this tick of the line (`Cxx`, 5.5) |
| CMD | Shown by default (Resolution menu > Step command rows hides it), and whenever a command of its box is written | TIK, SYN, VSL, BSL, NSL; MUL for several | Step commands (5.5) |
| PAN | 16 ch, and DMA tracks | Center, −10 to +10 | Stereo position of this step |
| VOL | All | 0 to 15 | Accent (4-bit level). Drag to draw; right click for a slider (right click on it: back to 15; click its value to type one) and its **Slide** button: the level glides to the VOL of the next step (`Gxx`, 5.5), drawn as a ramp |
| BPM | Standalone application, under VOL after a separator | 32 to 255 | Tempo of every track from this step on: one row for the whole pattern, the same on every track (`Txx`, 5.5) |

ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP and BPM are shown with the CMD row (Resolution > Step command rows), each also while one of its values is written; ARP is shown whenever the track's arpeggiator is on. ARP, VIB, LFO and MOD go while the track's arpeggiator, vibrato, LFO or modulation table is off, and come back with their values when it is on again.

A value written where it cannot act is greyed and crossed, and its tooltip says why: the LFO of the track without depth (LFO), a drum track (TUN), no envelope playing (ENV), not a DMA track or no note starting on the step (SMP, DLY), no slide running (CRV), Ticks timing or an external clock (BPM). It can still be changed or erased.

A cell where nothing could act because of the sound of its step is crossed even when empty, and a click there opens nothing; its tooltip says why. That sound is the generator of the note (GEN, else its instrument, else the track): ENV under a SID Voice, which writes its own volumes; SRC under a Sync Buzzer; SRC and ENV under DMA or a drum sound; SMP outside DMA notes; GEN on the DMA tracks of Authentic 3ch and Dual chip, and on a held DMA or drum note; INS and CND on a held note, INS on the note a slide reaches (it keeps the instrument of the note the slide starts from); GAT and RTG during a slide; GAT on a step after which the note goes on (only the GAT of its last step acts). On a step where no note sounds, an empty cell stays open, since a note may be written there later; a value already written there is crossed.

A note drawn over several steps is held: SRC, GEN, CRV, PAN, VOL, RTG and ARP act on each of its steps. VOL sets its level step by step; RTG strikes it again from the start of its step; ARP starts or resumes the arpeggio there (RTG waits while the arpeggio runs, and during a slide only VOL acts). GAT acts on the last step of the note, where it releases it; OCT, TON, INS, CND and SLD belong to its first step.

Click an on/off row to toggle it (drag to paint). Click a value row for its menu; GAT, PAN, BPM, TUN, DLY, CUT and SMP open a small box (PAN shows 0 at the centre); CMD opens the box of the step's commands. Right click a control cell to reset it (drag to reset more), except a GEN, SRC or ENV cell that holds a value: its right click opens a window with the settings of the focused track that the cell brings into play, which act as the same controls of the main window. GEN: those of SID Voice (Timer ratio, SID min vol, the SID wave, the menu of its forms, its Steps, Period with Sine or Bounce, Width with Square, laid out as in the main window) or of Sync Buzzer (Tuning, N, D, Fine, YM Pad, Presets, the steps); Chip Voice has none. SRC: Noise period and Volume, greyed when the cell's source does not use them, and Level. ENV: Env freq and EP. One window opens at a time, showing the last cell right-clicked. Right click a row's label for **Copy row** / **Paste row**, which copies a whole row from one track to another (not BPM, which every track shares).

### 5.5 Step commands (CMD and the command rows)

The CMD row and the rows of the commands of their own (ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP, BPM) are shown by default; Resolution > Step command rows > Off hides them (a preference) while none of their values is written. ARP is shown with the arpeggiator. A step holds up to fifteen commands per track, one of each kind, which play together, and the tempo of the pattern. The CMD cell shows the short name of the command of its box (TIK, SYN, VSL, BSL, NSL), or MUL when there are several; BPM, TUN, DLY, CUT and SMP show their number, ENV the shape (8 to F) and R for a restart, ARP, VIB, LFO and MOD On or Off.

Click a CMD cell to open the **Commands** box: one row per command, in the order Ticks per Line, Sync Mark, Volume Slide, Buzzer Slide, Noise Slide, each with its value and an **On**/**Off** button. A slide has a speed from 0 to 15 (0 stops it) and a button for its direction, **Up** or **Down** (written `x0` or `0y`). In 16 channels, the button of the Buzzer and Noise slides opens a menu: Up, Down and a **Curve** submenu with the 13 shapes of the CRV row; the button shows the direction and the short name of the shape (Down EXP). A shape other than Linear (`Kxx`, `Qxx`) moves the slide along it from its step to the next command of its kind (to the end of the track if there is none), covering there what its speed covers in a straight line; Linear keeps the speed per frame. The other commands have a slider and a field typed in decimal. A slide is in the box only where it can move a note from its step to the next command of its kind, the track looping: Buzzer Slide a note whose envelope is heard (Env source or SRC entry, Sync Buzzer); Noise Slide a note whose noise is heard (Source or SRC with noise, Chip Voice or SID Voice); the Volume Slide, which the Slide of a VOL cell replaces, only where an older project wrote one. Each note has the generator of its GEN cell, else of its instrument, else of the track, and the source of its SRC cell, else of its instrument or of the track; its modulation table counts too, and an empty step counts as the note that could be written there. No slide on a drum track. A slide written on the step stays in the box, so that it can be switched off. Choosing a value turns its row On. **OK** writes every row into the step; **Cancel**, the default button (Enter, Escape), closes without changing it. A command absent from the step shows a value to start from: the song's ticks per line, mark 1, volume down a quarter step per tick (`01`), the other slides up one unit (`10`). Right click a CMD cell to clear the commands of its box.

The other commands have rows of their own. **ARP**, **VIB**, **LFO** and **MOD**: a menu, No change, On (`J01`, `V01`, `L01`, `M01`) or Off (`J00`, `V00`, `L00`, `M00`). **BPM** (standalone application only, since in a plugin the DAW sets the tempo; a tempo written stays in the project): a box with a slider from 32 to 255, starting from the BPM field; the row belongs to the pattern, so a tempo written on one track shows on all of them. **ENV** (when the envelope plays): a menu of the shapes 8 to F with their pictures, and a **Restart** submenu: None, At once (tick 0) or At tick..., which opens a box over the ticks of a line. **DLY** and **CUT**: a box with a slider over the ticks of a line at the current tempo, from 1 for DLY and from 0 for CUT; a larger tick, up to 255, can be typed and is ignored while the line is shorter. **TUN**: a box in cents, −99 to +99. **SMP** (DMA tracks): a box from 0 to 255, in steps of 256 frames. **None** in a box, and No change in a menu, take the command out of the step; so does a right click on the cell (on ENV, the shape and the restart).

| Command (row of the box, or its own row) | Effect |
|---|---|
| `Sxx` (Ticks per Line) | Ticks per line from this line on (Ticks timing only; 1 to 31). Ignored in BPM timing and in Host sync |
| `Txx` (BPM row) | Tempo of every track from this line on, 32 to 255 BPM (a smaller value typed becomes 32), one row for the pattern. BPM timing with the sequencer's own clock (Free run) and in the exports; ignored in Ticks timing, in Host sync and with MIDI ext. A project or file of an earlier version that had a tempo on several tracks of one line keeps the last one. The tempo holds until the next `Txx`: Play starts again from the BPM field, a Song entry that has its own tempo starts again at it on each pass, and moving the BPM field takes over |
| `Vxx` (VIB row) | Vibrato of the track from this line on: Off (`V00`) holds it, On (`V01`) brings it back (the vibrato of the VIBRATO frame; the modulation wheel still adds its own). Holds until the next `Vxx`; Play and Stop give the track its vibrato again. With Vibrato Off in the frame, `Vxx` brings nothing back |
| `Jxx` (ARP row) | Arpeggiator of the track from this line on: On (`J01`) runs it on the notes of the pattern, Off (`J00`) stops it, also on a step of a held note, which goes on without arpeggio. Holds until the next `Jxx`. Each pattern starts with it off, as Play does; started in the middle of a pattern, the track takes the last `Jxx` before that step. With the arpeggiator Off in its window, `J01` does nothing. The ARP steps of an earlier version become `J01` and `J00` and play the same |
| `Lxx` (LFO row) | LFO of the track (Modulation window) from this line on: Off (`L00`) holds it, On (`L01`) brings it back. Holds until the next `Lxx`; each pattern starts with the LFO as set in the window |
| `Mxx` (MOD row) | Modulation table of the track (Modulation window) from this line on: Off (`M00`) holds it, On (`M01`) brings it back. Holds until the next `Mxx`; each pattern starts with the table as set in the window |
| `Rxx` (ENV row, Restart) | Restarts the hardware envelope (rewrites R13) at tick xx of the line (0 to 255), without retriggering the note. In Authentic 3ch only if the track drives the shared envelope |
| `Zxx` (Sync Mark) | Demo sync; plays nothing. In an SNDH export, the routine writes xx (0 to 255) into its sync byte on the frame of the line (several tracks on one line: the last one wins). A demo finds that byte at the address that the SNDH init routine returns in register A0; it holds 0 after init. Where maxYMiser keeps its own sync byte is **TO CONFIRM** |
| `Gxx` (VOL row, Slide) | The level of the step glides frame after frame, on a straight line, from its VOL to the VOL of the next step: the accent in 16 channels and on DMA tracks, the 4-bit volume in the 3-channel modes (in the exported registers). Not on drum tracks |
| `Axy` (Volume Slide, older projects) | The volume of the notes goes up x or down y quarter steps (of the 16 levels) per replay tick, from the second tick of a note; with both set it moves by x − y. Holds until the next `Axy`: `A00` stops it. Each new note starts from its own volume. No effect on a volume driven by the envelope. The Slide of a VOL cell replaces it; the box shows it where a project has one |
| `Dxx` (DLY row) | The notes of this step start at tick xx of the line; the rest of the line is their length. A tick beyond the line is ignored (the notes start on time) |
| `Cxx` (CUT row) | The notes of the track stop at tick xx of the line (`C00`: at once, the note of this step does not sound). A tick beyond the line is ignored. Only this line |
| `Bxy` (Buzzer Slide) | The hardware envelope period (R11–R12) glides: up x (shorter period, higher) or down y sixteenths of a semitone per replay tick. Holds until `B00`; each new note starts from its own period. Bends the Sync Buzzer and envelope basses |
| `Kxx` (Buzzer Slide, Curve) | 16 channels: the shape of the `Bxy` of the step (1 to 12: the shapes of the CRV row after Linear). The slide follows it up to the next `Bxy` of the track; the 3-channel modes ignore it |
| `Nxy` (Noise Slide) | The noise period (R6) glides: up x (shorter period, higher noise) or down y quarter steps per replay tick, within 1 to 31. Holds until `N00`; each new note starts from its own noise |
| `Qxx` (Noise Slide, Curve) | 16 channels: the shape of the `Nxy` of the step, as `Kxx` |
| `Exx` (ENV row) | Hardware envelope shape R13 of the track from this line on, 8 to 15 (the shapes of the Shape R13 menu); a held note gets it at once, which restarts the envelope. Holds until the next `Exx`; Play starts again with the shape of the sound. Other values are ignored |
| `Oxx` (SMP row) | The DMA samples of this step start xx × 256 frames after the slot's Start (STE machine). A start at or beyond End gives silence; a looped sample loops back to that start, as the STE reloads its frame start address. Only this line. The exact STE address granularity is **TO CONFIRM** |
| `Fxx` (TUN row) | Tune of the track from this line on, xx − 100 cents (−99 to +99 in the box; ±100 from an earlier file shows as ±99 and plays as written); a held note follows. Holds until the next `Fxx`; Play starts again at 0 |

Commands belong to the line, not to the note: deleting or moving a note leaves them in place. The arpeggio `Xyz` and the pitch slide `Pxy` are retired, the ARP and SLD rows doing their work; a file that holds them loads without them.

### 5.6 Arpeggios, slides and gate

- **Arpeggios**: the track's arpeggiator (4.2) plays on the notes of the pattern from a step whose ARP row is On until a step whose ARP row is Off (each pattern starts with it off), and on notes you play live. On a step of a held note, ARP On starts or resumes the arpeggio. As on the keyboard (4.2), each note of the pattern is one note of the arpeggio: its envelope runs on through the steps of the arpeggio, the next note of the pattern starts it again from the attack; at the end of the note, it keeps cycling through the arpeggio during its release.
- **Slides**: SLD on a note glides to the next note; CRV sets the shape (Custom uses the Slide Curve window). The Arpeggiator's Track and Land options adjust arpeggios during glissandos. The pitch matrix of the Modulation window decides what slides move (square, timer, envelope).
- **Gate**: the Gate slider sets the note length as a share of the step for the focused track; with **All**, one gate applies to every track. The GAT row overrides it per step.

### 5.7 Track length, Euclid, rotate and transpose

- **Len =** gives the focused track its own length (1 to 64 steps): it loops on its own while the others keep going (polymeters). Its box starts from the length the track plays. **Global** follows the pattern length again.
- **Euclid** opens the Euclidean generator: **Steps** (2 to 32), **Pulses** (hits spread as evenly as possible), **Rotate**, **Accents** (a second figure raises the volume of those hits), the note, slot or drum to write, **Merge** (keep what is already written), **Rhythms** (a menu of 23 named rhythms, from E(2,3) Rumba to E(13,24) Aka pygmies, which sets Steps and Pulses; a rhythm longer than the track is greyed) and **Apply**. When the figure would replace or remove notes already written, Apply asks first; the generator stays open.
- **<** / **>** rotate the track; **-1** / **+1** and **Oct-** / **Oct+** transpose it.

### 5.8 Timing

- **BPM timing** (default): the Resolution menu sets the value of one step: 1/4, 1/8, 1/16 (default), 1/32, 1/64, 1/4T, 1/8T, 1/16T, 1/32T. Swing shifts every other step. At 1/32T a step lasts two pulses of the MIDI clock. At 1/64 a step lasts a pulse and a half of the MIDI clock: with Clock MIDI ext, every other step starts halfway between two pulses, at half the measured pulse length; with Quantize to VBL, the steps fall on the replay frames (at 120 BPM and 50 Hz, a 1/64 step lasts 31 ms, one or two frames).
- **Ticks timing** (Resolution > Ticks: Speed ticks per line): each line lasts **Speed** replay ticks (1 to 31, at the VBL frequency) plus the **Groove** (up to 16 values, 0 to 15 ticks added to successive lines), as in trackers. In Free run the BPM field shows "ticks" and the resulting tempo; Swing is replaced by the groove. In Host sync the lines follow the host's beats with the resolution.

Choosing another resolution while the patterns hold notes opens a box that asks whether the notes follow it. **Fit** moves each note to the step of the new grid nearest its time (half a step rounds up) and scales its length, the step values written on it, the chords, the pattern lengths, the BPM row, the tracks' own lengths and the Song rests with it; when two notes of a row land on one step, the first one stays. Fit is one undo step. **Keep** leaves each note on its step, which now lasts the new value. **Cancel**, the default (Enter and Esc), keeps the resolution. Without notes, or with the same resolution, no box opens.

### 5.9 Clocks and host sync

| Clock | Where | Behaviour |
|---|---|---|
| **Host sync** | Plugin (VST3, CLAP, LV2) | The host's transport starts and stops the sequencer and sets its position, tempo and signature. The Play button and Space do nothing; the BPM field shows "host" and the host's tempo |
| **Free run** | Standalone | The sequencer's own clock: Play/Stop (Space), BPM 60 to 240, `Txx` commands |

In Host sync, step k of the pattern starts at quarter note k / (steps per quarter) from the start of the host's timeline; the pattern repeats every pattern length; the Song chains its entries from the start. When you start or jump (loop, relocate), held sequencer notes are released and the sequencer is placed again. **With Loop off, the pattern (or Song) plays once from the start of the timeline, then stays silent** until the host goes back. The host's tempo overrides the per-entry tempo of the Song. Arpeggios set to Clock MIDI ext follow the host's beat (24 pulses per quarter note).

MIDI Start, Stop, Continue, Song Position Pointer and MIDI clock do not drive the sequencer in Host sync. To make the sequencer follow MIDI clock, set **Clock** (Arpeggiator window) to **MIDI ext**, as in the prototype: the BPM slider is greyed, Play arms the sequencer, MIDI Start starts it from the beginning, Continue and Song Position Pointer resume where asked, MIDI Stop stops it, and the BPM field shows "ext" and the measured tempo. Changing Clock during playback stops it. Offline exports (WAV, stems, register exports) have no MIDI clock to follow: they play at the sequencer's own tempo.

### 5.10 Recording

- **Rec** (transport) records the notes you play on the focused track into the pattern, at the playing step. In the standalone application, arming Rec also starts playback; stopping disarms it.
- **Step** (step input) writes each note you play at the cursor, which then advances. It takes priority over Rec.

Notes from your MIDI keyboard and from the on-screen keyboard are recorded. With MIDI routing Per channel, only notes on the focused track's channel count.

### 5.11 Song

Opening the Song window (Song button, Edit > Song, Ctrl+G) turns **Song mode** on: the sequencer plays the Song instead of the current pattern. Closing the window turns Song mode off.

- **Add** appends the current pattern (again: one more repeat; Shift+Add: a new entry).
- Click an entry to select it (Shift: several); drag it to move it.
- Entries read like `A1x2` (pattern A1, two repeats); `|:` marks the restart point; `♪n` a tempo of its own.
- Right click an entry: Repeat x1, x2, x4, x8, x16; Duplicate; Cut; Copy; Paste; Move left; Move right; Remove; Tempo... (20 to 300 BPM, empty = song tempo); Insert rest; Insert note-off; Insert loop; Length... (rest and note-off entries, 1 to 256 steps); Loop back... (how many entries a loop entry plays again, 1 to 255; Repeat sets how many times it jumps); Restart here / Restart from start (where the Song goes back when Loop is on).
- **Rest** plays silence; **Note-off** releases all sequencer notes, then silence; **Loop** (shown `BackNxR`) jumps back N entries R times.
- Keys (Song window in front): ←/→ move, Home/End jump, Ins or A adds the current pattern after the cursor, Del removes, D duplicates, + and − change repeats, X / C / V cut, copy and paste, Enter jumps playback to that entry.
- The transport buttons are the same as in the Step Sequencer; Previous and Next move between Song entries (not in Host sync).

---

## 6. Instruments, Patches, Drum Kit, Digidrums and DMA

### 6.1 Instruments (Ctrl+T)

32 instruments ($01 to $20) stored in the project. An instrument is a track's sound: source, volume, envelope, generator, vibrato, ADSR, Modulation table and sequences, arpeggio. Level, pan and gate stay those of the track that plays it.

- Click a used slot to load it into the focused track; click an empty slot to store the focused track there (you are asked for a name, 16 characters at most).
- Right click: Store channel, Load into channel, Rename, Save to Patches, Load from Patches, Clear.
- In the Step Sequencer, the **INS** row plays a note with an instrument (its menu ends with Instruments... to open this window). The row appears once the project has an instrument.
- **Program Change** n (1 to 32) selects instrument n for live notes (section 7.4).

### 6.2 Patches (Ctrl+P)

A library of named track sounds shared by all your projects (the file `Patches.json`, section 9).

- **Save current** stores the focused track's sound under a name.
- Click a patch to load it into the focused track (one undo step). Right click: Load, Rename, Overwrite, Delete.
- **Import** adds the patches of a library file to the end of yours; **Export** writes your library to a file.
- If the library file cannot be read, STYM2149 leaves it unchanged and tells you.

### 6.3 Drum Kit (Ctrl+K)

The eight instruments of the **Chipdrum** generator, common to all tracks and saved in the project. On a Chipdrum track each instrument answers to a note:

| Instrument | Note | Settings |
|---|---|---|
| Kick | C | Pitch (25–90), Sweep (20–300), Decay (4–20), Noise (0–100), Vol (0–100) |
| Snare | C# | Body (80–400), Tone (0–70), Decay (6–30), Vol |
| Clap | D | Space (6–20), Color (15–80), Tail (10–60), Vol |
| Hat | D# | Metal (0–100), Tune (60–160), Decay (15–120), Vol |
| Open Hat | E | Metal, Tune, Decay (3–30), Vol |
| Closed Hat | F | Metal, Tune, Decay (30–250), Vol |
| Tom 1 | F# | Pitch (60–200), Sweep (20–150), Decay (4–20), Noise, Vol |
| Tom 2 | G | Pitch (80–260), Sweep (20–150), Decay (4–20), Noise, Vol |

Notes G# to B play Kick to Hat again. **Audition** previews the selected drum, **Reset** restores its factory values, **Kits** loads one of 13 factory kits (Classic, TR-606-ish, TR-909-ish, Hard EBM, Industrial, Lo-Fi 4bit, Chiptune, TR-808-ish, House, Techno, Hip-Hop, Trap, Drum'n'Bass). The kit name shows "Custom" once you change it. The kit rebuilds live while playing. File > Load / Save > Drum kit reads and writes kit files.

### 6.4 Digidrums (Edit > Samples > Digidrums)

Eight 4-bit sample slots played by the timer of a Digidrum track, at their rate from their root note. In the sequencer, the row you write on chooses the slot; from MIDI, the track's Slot setting does.

- **Slot** 1 to 8 (click: play it and select it). **Name** (16 characters, taken from the file loaded; click to type it). The name opens the line of text over the waveform, here and in the GENERATOR frame, and follows the slot number in the tooltips.
- **Rate** 2,000 to 20,000 Hz at the root note (presets 7800, 8000, 11025). Changing it keeps the values, so the pitch moves; a file loads at this rate.
- **Root** (the note that plays the slot at its rate) and **Fine** (−100 to +100 cents).
- **Load...** (or click the waveform): an audio file (WAV, AIFF, FLAC, Ogg Vorbis), first channel only, resampled to the rate, 3 seconds at most. **Play** plays the slot once, from Start to End, through the chip's volume table; with Loop on, it repeats that part until you click Play or Loop again or close the window, Play staying lit meanwhile. **Clear** empties the slot.
- **Start** and **End**: the part of the sample played (End is the value after the last one played), with or without Loop; loading a file sets them to the whole sample. The waveform greys what lies outside. The YM6 and SNDH exports write that part.
- **Loop**: repeats Start to End while the note is held, then plays on to End.
- **4-bit**: **Linear** or **YM levels** (values matched to the chip's measured output levels, so the sample keeps its shape). The 8-bit source is kept, so you can convert again.
- **Tools** on the 8-bit source: Normalize, Reverse, Fade out, **Sign** (flip the top bit of each byte: signed ↔ unsigned), −3 dB, +3 dB. Reverse also mirrors Start and End, so the part played is the same sound read backwards. −3 dB right after a +3 dB that clipped gives the clipped peaks back: the steps in a row are computed from the values before the first one.
- **Banks**: save the 8 slots as a named bank, load or delete a bank. The bank library (`Digidrums.json`) is shared by all projects.

Loading a file or a bank cannot be undone; the other settings can.

### 6.5 DMA (Edit > Samples > DMA (STE))

The STE DMA sound plays eight 8-bit signed samples of 32 KB each. It needs Computer STE: with Computer ST the menu entry is greyed out, and a window left open says so and offers **Use STE**. With STE, DMA tracks (Authentic 3ch, Dual chip) or tracks of the DMA type (16 ch) play these samples, each track choosing its sample in the GENERATOR frame.

The window is laid out like the Digidrums window: the slot on the left, its sample on the right.

- **Slot** 1 to 8. **Name** (16 characters, taken from the file loaded or, after an XM import, from the sample or its instrument; click to type it). The name opens the line of text over the waveform, here and in the GENERATOR frame, and follows the sample number in the tooltips and in the text of the GENERATOR frame.
- **Rate**: 6.3k, 12.5k, 25k, 50k (6258, 12517, 25033, 50066 Hz). Changing it keeps the bytes, so the pitch moves and the length changes, as on the STE. With **Resample** lit (under Rate; a preference, unlit by default), a new rate takes the sample again at it instead: it keeps its length and its pitch, Start and End stay on the same moments, 32 KB at most; one undo step.
- **Root**: the note that plays the sample at its rate, and **Fine** (−100 to +100 cents), which tunes the sample from it, the preview and the SNDH export included.
- **Load...** (or click the waveform): an audio file (resampled to the rate) or raw 8-bit bytes (`.raw`, `.spl`, `.sam`). **Play** plays the sample once from Start to End; with Loop on, it repeats that part until you click Play or Loop again or close the window, Play staying lit meanwhile. **Clear** empties the slot: sample, name, Start and End.
- **Start** and **End** (in frames): the part of the sample played, with or without Loop (End is the frame after the last one played); loading a file sets them to the whole sample. The waveform greys what lies outside. The SNDH export writes the sample up to End.
- **Loop**: repeats Start to End while the note is held; otherwise the part plays once, to End.
- **Format**: **Mono** (one byte per frame; a stereo file is mixed down) or **Stereo** (bytes interleaved left, right; 16 KB per side at most). The waveform of a stereo sample shows its two channels, marked **L** and **R** at their top right.
- **Tools**: Normalize, Reverse, Fade out, **Sign** (flip the top bit of each byte: signed ↔ unsigned), −3 dB, +3 dB, as in the Digidrums window. With a stereo sample, **L** and **R** choose the channels they work on (both lit by default, one at least); Normalize on both channels raises them by one gain, keeping their balance.
- **Banks** (under Fine): save the 8 samples (settings, bytes and names) as a named bank, load or delete a bank. The bank library (`DMA.json`) is shared by all projects.

Loading a file or a bank cannot be undone.

---

## 7. MIDI

### 7.1 Routing

MIDI routing, in the MIDI frame of Options > Settings:

| Routing | Incoming notes play |
|---|---|
| **Omni** (default) | The focused track (host parameter Focus Track, 1 to 16) |
| **Layer** | Every enabled track at once (also the Layer button) |
| **Per channel** | MIDI channel n plays track n |

The on-screen keyboard always plays the focused track. A DMA track's channel goes only to that track, whatever the routing.

### 7.2 Velocity, pitch bend, modulation wheel, aftertouch and MPE

These settings are in the MIDI frame of Options > Settings (section 3.12). Drums (Chipdrum, Digidrum) ignore the wheel, the aftertouch and the MPE slide.

- **Velocity**: **Accent** (default: an audio gain in 16 ch, the 4-bit volume in Authentic 3ch), **Vol 4-bit** (the 4-bit volume in all modes) or **Off**.
- **Pitch bend**: ± Bend range (0 to 24 semitones, default 2). **Bend** chooses what it drives: **Pitch** (default), **Volume** (the centre leaves the 4-bit volume as it is; the top raises it towards 15, the bottom lowers it towards 0) or **Envelope** (the envelope frequency moves by the semitones of the bend, the note does not: a sweep of the timbre in Sync Buzzer and with the Env sources).
- **Modulation wheel** (CC 1): **Wheel** chooses what it drives, from nothing at the bottom to all of it at the top: **Vibrato** (default, up to Wheel depth, 0 to 200 cents, default 50), **Volume** (the 4-bit volume raised towards 15), **Envelope** (the envelope frequency, up to an octave higher), **LFO** (the depth of the track's LFO: none at the bottom, the depth set in the Modulation window at the top) or **Off**.
- **Aftertouch**: the pressure of the keys, of the whole channel (channel pressure) or of each note (polyphonic aftertouch); a note takes the higher of the two. **Aftertouch** offers the same choices as Wheel (Vibrato by default). When the wheel and the aftertouch drive the same thing, the higher of the two plays.
- **MPE** (16 ch only): with MPE On, channel 1 plays as usual and its controllers act on every note, while channels 2 to 16 each play their own note, which their pitch bend (± **MPE bend range**, 48 semitones by default), their pressure (the Aftertouch choice) and their CC 74 (**MPE slide**) move alone. MPE slide drives the **Volume** (default: 64 leaves the 4-bit volume as it is, 127 gives 15, 0 gives 0), the **Envelope** (an octave either way) or the **Vibrato** (from 64 up to Wheel depth at 127). The notes go to the focused track (or every track in Layer); the Per channel routing does not apply.

### 7.3 Controllers

Controllers act on the focused track (or, in Per channel routing, on the track of the channel):

| CC | Effect |
|---|---|
| 1 | Modulation wheel (Wheel: vibrato by default) |
| 7 | Master level |
| 70 | Tone on/off (≥ 64 = on) |
| 71 | Noise on/off |
| 72 | Noise period |
| 73 | Volume mode (Vol 4-bit / Envelope) |
| 74 | Envelope period (with MPE on, on channels 2 to 16: the MPE slide of their note) |
| 75 | Envelope shape |
| 76 | Generator (the first five types) |
| 77 | Timer ratio |
| 78 | SID min vol |
| 80 | Arpeggiator on/off |
| 81 | Arpeggio rate |
| 82 | Sequencer Play (≥ 64) / Stop (no effect in Host sync) |
| 83 | Free tempo (60 to 240) |
| 84 | Focus track |
| 120, 123 | All notes off |
| 121 | Reset pitch bend, modulation and channel pressure (with MPE on, on channels 2 to 16: their bend, pressure and slide) |

A controller learned with MIDI learn (7.6) takes priority over this table. SysEx messages are ignored; the aftertouch is in section 7.2.

### 7.4 Program Change

Program Change n selects instrument n ($01 to $20, n = 1 to 32) for the notes played next, on the track of the channel (Per channel), the focused track (Omni) or all tracks (Layer). Program 0 gives the track its own sound back; values above 32 are ignored. Hosts that number programs from 1 show n + 1. A sequencer step whose INS row names an instrument keeps priority. The choice is not saved in the project (the host sends its Program Change again on playback). Percussion tracks have no instruments.

### 7.5 MIDI clock

- Arpeggiators with **Clock: MIDI ext** step on the MIDI clock at the chosen division (in a DAW, on the host's beat).
- The sequencer follows the host transport in a DAW and its own clock in the standalone application (5.9).

### 7.6 MIDI learn

1. Options > **MIDI learn**. A banner at the bottom left says "MIDI learn: click a slider or button, then move a control...".
2. Click a slider or button in STYM2149 (the click arms it without changing it).
3. Move a knob or press a pad or key on your controller. The banner shows the binding, for example `CC74 -> [ef]`.
4. Click another control, or leave with Options > **Stop MIDI learn**.

A learned slider follows the controller over its whole range; a learned button is pressed when the value rises past 64 (a note counts as 127, its release as 0). A learned note no longer plays a sound. Channel-mode messages (CC 120 to 127) are never learned. The channel is ignored.

The MIDI map is a preference shared by all instances and projects (section 9). **Clear MIDI map** empties it. Learned controls only respond while the plugin window is open; a learned Mixer volume works with the Mixer window closed.

### 7.7 Host automation and parameters

STYM2149 exposes 554 host parameters: 42 global ones (Master, Voice Mode, Chip Clock, Output Chain, Resampling, STE Bass, STE Treble, VBL Rate, Soft Clip, Timer, Focus Track, MIDI Routing, Velocity, Bend Range, Wheel Depth, Arp Clock, Arp Division, Free Tempo, Swing, Gate, Gate All, Computer, Chip Clock Hz, Arp Sync, Master 2, Master Pan, Master Link, Delay On, Delay Sync, Delay Division, Delay Time, Delay Feedback, Delay Hi Cut, Delay Level, Delay Ping-Pong, Tuning A4, Bend Target, Wheel Target, Aftertouch, MPE, MPE Bend Range, MPE Slide) and 32 per track, named "T1 Level", "T2 Env Period", and so on (On, Level, Pan, Mute, Solo, Tone, Noise, Noise Period, Volume, Vol Mode, Env Period, Env Shape, Generator, Timer Ratio, SID Min Vol, Vibrato Depth, Vibrato Rate, Attack, Decay, Sustain, Release, ADSR Mode, Voicing, Arp On, Arp Rate, Gate, Vibrato On, Send, Level R, Mute R, Solo R, Link). The twelve MIDI settings (MIDI Routing, Velocity, Bend Range, Wheel Depth, Arp Clock, Arp Division, Bend Target, Wheel Target, Aftertouch, MPE, MPE Bend Range, MPE Slide), Chip Clock Hz (the Custom clock's frequency) and Tuning A4 are not automatable.

Patterns, the Song, instruments, sequences, kit and samples are not parameters: they are saved in the project state.

### 7.8 Outputs

Besides the main stereo output, the plugin offers 16 mono track outputs ("Track 1" to "Track 16") and 2 mono chip outputs ("Chip 1", "Chip 2"), disabled by default. Enable them in your host. A track output carries the track alone after its mixer (level, mute, solo), without panning; a chip output carries each chip in the 3-voice modes and is silent in 16 ch. An output enabled while the plugin runs stays silent until the host prepares the plugin again. The plugin has no audio input. It reports a release tail of 2 seconds and a small latency with Sinc resampling.

---

## 8. Files

File dialogs are your system's own. They open in the last folder you used.

### 8.1 Project files (JSON)

| File | Default name | Content |
|---|---|---|
| Preset | `stym2149-preset.json` | The whole project, including instruments, sequences, Song, DMA and Digidrum samples. Loading it stops all notes and cannot be undone |
| Pattern | `stym2149-pattern.json` | One pattern (16 tracks); loads into the current pattern |
| Song | `stym2149-song.json` | The Song and the 32 patterns |
| Drum kit | `stym2149-kit.json` | The Chipdrum kit |
| Patch library | `stym2149-patches.json` | Patches window Import / Export |

**Save** writes again to the file last used for that kind (asks the first time); **Save as** always asks. A file written by a newer STYM2149 is read for what this version knows, and a message says so.

### 8.2 Import

| Import | Accepts | Result |
|---|---|---|
| YM dump | `.ym .ym2 .ym3 .ym5 .ym6 .snd .sndh`: YM2 to YM6 (LHA-compressed or not) and SNDH | Plays in the YM Player (4.5) |
| MIDI file | Standard MIDI file, format 0 or 1, 8 MB at most | Patterns and Song (below) |
| XM module | Fasttracker II XM 1.04, 64 MB at most | Patterns, Song, instruments and DMA samples (below) |

**MIDI import.** One step is a sixteenth note. Patterns have the length of the current pattern, as many as needed, 32 at most (the rest is dropped and reported). Channels go to the tracks of the mode (all 16 in 16 ch; tracks 1–3 in Authentic 3ch; 1–3 and 5–7 in Dual chip), melodic channels first, channel 10 last. Channel 10 becomes a Chipdrum track (General MIDI drums mapped to the kit); other channels become Chip Voice tracks; long notes are tied. VOL is the velocity scaled to 0–15, on every step of the note. Two notes starting together on one channel keep only one (no chords). Patterns 1 to n are replaced and the Song gets one entry per pattern; the file's tempo is applied. The whole import is one undo step.

**XM import.** The module's order table becomes the Song (with a restart point where it loops), its patterns become patterns 1 to n (32 at most). Computer is set to STE; channels go to the tracks that play the DMA sound (all 16 in 16 ch, set to the DMA generator; tracks 5–12 in Authentic 3ch; 9–16 in Dual chip). The 8 most played instruments with samples become instruments 1 to 8 and DMA samples 1 to 8 (8-bit, 32 KB, nearest STE rate). If the module's tempo is constant and matches 125, 150, 175 or 250 BPM, the import uses Ticks timing at 50, 60, 70 or 100 Hz; otherwise BPM timing, where each Song entry gets the tempo of its position and a tempo change within a pattern (`Fxx`) becomes a `Txx` command on its line. Effects that have a command become one on their line: `Axy` → `Axy` (up to 15; an XM slide lasts its line, so the next line gets `A00`); `9xx` → `Oxx` (scaled to the DMA rate of the sample); `EDx` → `Dxx`; `ECx` → `Cxx`; `E5x` → `Fxx` for its note. The arpeggio `0xy` and the portamentos `1xx` and `2xx` are left out. The steps of a held note carry its volume (VOL row). A message lists what could not be imported.

### 8.3 Export

| Export | Pattern | Song | Format |
|---|---|---|---|
| MIDI | ✓ | ✓ | SMF type 1, 96 ticks per quarter note: what the sequencer plays (chord voices, retriggers, arpeggio notes included). Percussion on channel 10 with General MIDI notes; one MIDI track per played track |
| WAV | ✓ | ✓ | 16-bit, 24-bit or 32-bit float; 44.1, 48, 88.2, 96, 176.4, 192 kHz or the host rate (your choice is remembered) |
| Stems (ZIP) | ✓ | ✓ | One WAV per track that plays at least one note, `stym2149-track-<track>.wav`, in a ZIP |
| VBL dump | | ✓ | Register frames as assembler source (`.s`, `.asm`) |
| YM3 | | ✓ | Uncompressed YM3 register dump |
| YM6 | | ✓ | LHA-compressed YM6, with timer effects |
| SNDH | | ✓ | SNDH for the Atari ST or STE: subsong 1 = the Song, subsong 2 = the current pattern; with Computer STE, the DMA sound is included |

- Song exports need Song entries ("Song is empty. Add entries first."). Exports run in the background; one at a time.
- Audio exports are rendered offline: they stop exactly when the sequencer stops, without a release tail, and are cut at one hour of music (or at the 4 GB limit of WAV and ZIP).
- **Register exports** (VBL, YM3, YM6, SNDH) need **Authentic 3ch** (one chip, three voices). They capture what you hear, one frame per replay tick. After the export, a message lists what the file does not reproduce, for example: timer effects frozen (VBL, YM3), YM3 players assuming 2 MHz and 50 Hz, periods rescaled to 2 MHz (SNDH), DMA sound missing (formats without it). A SID wave other than Base (Square, Sine, Bounce, Custom) goes into the file as the plain square wave, at the note's pitch.
- The Recorder (4.4) exports its take as YM6, SNDH or VBL dump.

### 8.4 Chip conformity

The **CHIP** indicator of the menu bar counts what in the current pattern a register export would not reproduce: more than three tracks with notes, Dual chip, chords on one voice, notes out of the chip's period range, panning and mixer levels (the ST is mono and only writes the 4-bit volume), a Master level other than 70, and Digidrum or DMA limits. Click it to read the list. "CHIP: OK" means VBL, YM3, YM6 and SNDH exports will match what you hear.

---

## 9. Preferences

Preferences belong to you, not to a project: every STYM2149 instance shares them, and each is set from the interface and saved at once.

| Preference | Set with |
|---|---|
| Window size | By resizing the window |
| Tooltips | Options > Settings > Tooltips (On, Off) |
| Tooltip language | Options > Settings > Language: English, built in, then the languages of the PO files by name (Deutsch, Español, Français, Italiano, Português with the installers). By default, your system language if the menu has it, otherwise English. The installers put the five languages in a folder of the computer (`C:\ProgramData\STYM2149\Languages` on Windows, `~/.local/share/STYM2149/Languages` on Linux, `/Library/Application Support/STYM2149/Languages` on macOS, where they are copied by hand until a macOS installer exists); a plugin copied without them shows English only. Your own Languages folder, next to the preferences file (`%APPDATA%\STYM2149\Languages`, `~/Library/Application Support/STYM2149/Languages`, `~/.config/STYM2149/Languages`), holds one gettext PO file per language (`nl.po`), made with a text editor or a translation tool such as Poedit; the menu reads both folders each time it opens. **Languages folder...**, under Language, writes a template with every English line and a README into your folder, then opens it. A file of yours named after an installed language (`fr.po`) corrects its lines. The translations still await review by native speakers (**TO CONFIRM**) |
| Keyboard layout | Options > Settings > Keyboard layout: Auto (detect), QWERTY, AZERTY, QWERTZ. Auto follows the layout of your system (Windows, Linux; macOS **TO CONFIRM**) |
| Note names | Options > Settings > Note names: English (C D E) or Latin (Do Re Mi) |
| Renderer | Options > Settings > Renderer (Windows only, greyed out elsewhere): Software (default) or Direct2D, how the window reaches the screen. Software copies only what changes; with Direct2D, a copy of the window could flicker at the top right of the Windows desktop for a few seconds |
| Pixels | Options > Settings > Pixels: Smooth (default) or Sharp (section 3.1) |
| Oscilloscope | Options > Oscilloscope: On (default) or Off (section 3.10) |
| Register monitor | Options > Register monitor: On (default) or Off (section 3.10) |
| Note monitor | Options > Note monitor: On (default) or Off, the Note line of the SYSTEM frame (section 3.9) |
| MIDI learn map | Options > MIDI learn, Clear MIDI map |
| Step command rows | Step Sequencer > Resolution > Step command rows: On (default) or Off (CMD, ENV, VIB, LFO, MOD, TUN, DLY, CUT, SMP, BPM) |
| Desktop icons | Their places on the desktop, by dragging them (section 3.1) |
| WAV format and rate | The WAV export dialog |
| Last folder | Remembered by the file dialogs |

**Settings file**:

| System | File |
|---|---|
| Windows | `%APPDATA%\STYM2149\Preferences.settings` |
| Linux | `~/.config/STYM2149/Preferences.settings` |
| macOS (from 1.0.0) | `~/Library/Application Support/STYM2149/Preferences.settings` |

The Patches library (`Patches.json`), the Digidrum banks (`Digidrums.json`) and the DMA banks (`DMA.json`) are stored next to it. Versions before 0.9.0 named this file `STYM2149.settings`; the first start of 0.9.0 copies your preferences from it.

The standalone application keeps its audio and MIDI devices and your last session in a file of its own, `STYM2149.settings`: in `%APPDATA%\STYM2149` on Windows, `~/.config` on Linux, `~/Library/Application Support` on macOS. When you quit (File > Quit or the window's close box), it saves the session there and reopens it at the next start.

To return to the defaults, close every STYM2149 instance and delete the settings file.

---

## 10. Keyboard shortcuts and mouse gestures

Shortcuts work while the STYM2149 window has the keyboard focus. Whether a given host passes every key (for example Space) to the plugin is **TO CONFIRM** for each DAW. ^X in menus means Ctrl+X.

### 10.1 Everywhere

| Keys | Action |
|---|---|
| Space | Play / Stop (Free run; in Host sync the host's transport decides) |
| Esc | Close the open menu; otherwise close the window in front; otherwise stop every note (panic) |
| Tab / Shift+Tab | Bring the next / previous open window to the front (with a single window open, the key goes to the host) |
| 1 to 8 | Pattern of the current bank |
| Shift+1 to Shift+4 | Bank A to D |
| ← / → | Previous / next track (with the Step Sequencer open: move the grid cursor; Shift+← / → still changes track) |
| M | Metronome on/off |
| Z / X | Keyboard octave down / up |
| Numeric keypad 0–9 | Keyboard octave (Computer Keyboard window open) |
| A W S E D F T G Y H U J K O L P ; ' | Play notes (Computer Keyboard window open; QWERTY positions) |
| Ctrl+Z | Undo |
| Ctrl+Y, Ctrl+Shift+Z | Redo |
| Ctrl+E / M / G / P / T / K / B / R / F | Step Sequencer / Mixer / Song / Patches / Instruments / Drum Kit / Arpeggiator / Modulation / Effects |
| Ctrl+V / S / U / C / I / D | Generator: Chip Voice / SID Voice / Sync Buzzer / Chipdrum / Digidrum / DMA (Computer STE) |
| Ctrl+/ or Ctrl+? | SHORTCUTS window: the prototype's list of shortcuts, then those STYM2149 adds (also in the Atari menu > Shortcuts) |
| Ctrl+N | New project |
| Ctrl+Q | Quit (standalone only) |

With the Step Sequencer in front, Ctrl+C, Ctrl+X and Ctrl+V act on the note under the cursor or on the selection. When there is nothing to copy or paste there, they fall through to the generator shortcuts above (Ctrl+C: Chipdrum, Ctrl+V: Chip Voice).

Key repeat (holding a key) works only for the arrows, Delete and Backspace, with the Step Sequencer open.

### 10.2 Step Sequencer open

| Keys | Action |
|---|---|
| ← → ↑ ↓ | Move the grid cursor |
| Enter | Write or remove a note at the cursor |
| Delete, Backspace | Clear the step at the cursor |
| `]` / `[` | Lengthen / shorten the note under the cursor (window in front) |
| `\` | Split the note under the cursor at the cursor |
| J | Join the note under the cursor with the next one |
| Ctrl+C / X / V | Copy / cut the note or selection; paste at the cursor |
| Arrows, Shift+↑/↓ | Move the selection box (by a semitone, by an octave) |
| Delete | Clear the selection box |
| Esc | Deselect the box |

### 10.3 Other windows in front

| Window | Keys |
|---|---|
| Song | ←/→ move, Home/End, Ins or A add, Del or Backspace remove, D duplicate, + / − repeats, X / C / V cut / copy / paste, Enter jump to the entry |
| Arpeggiator (On, Table) | Arrows move the cursor, Enter writes the step |
| Modulation (On) | Arrows move the cursor, Enter writes the pitch, Delete resets the step, N toggles noise |

### 10.4 Mouse gestures

| Where | Gesture | Action |
|---|---|---|
| Any slider | Drag | Change the value |
| Horizontal slider arrow | Click, hold | One step down or up, repeated while held |
| Value field | Click, type, Enter | Type a value |
| Floating window | Drag the title bar; click the close box | Move; close |
| Track tab | Click / click again / right click | Focus / On-Off / Copy, Paste settings, Rename |
| Pattern button | Right click | Copy, Cut, Paste, transpose |
| Piano | Click, drag / right click | Play / choose the played track |
| Mixer pan | Double click | Centre |
| Mixer fader, pan, Master | Right click | Value... (type it), Reset |
| Mixer value (under a fader, pan, Send, Master) | Click, type, Enter | Type the value in place |
| Mixer mini-scope | Click | Open the track's Scope |
| Step Sequencer grid | See 5.3 and 5.4 | Write, move, resize, select, menus |
| Step Sequencer grid | Mouse wheel | Scroll by one semitone |
| Control row label | Right click | Copy row / Paste row |
| GEN, SRC, ENV cell holding a value | Right click | The settings of the track it brings into play (5.4) |
| VOL cell | Right click | Slider and Slide to the next step |
| Song entry | Click, Shift+click, drag, right click | Select, multi-select, move, entry menu |
| Arpeggiator, Modulation grids | Click, drag | Draw |
| Modulation VOL, PER, NP cell | Right click | Precise slider (right click on it: back to 15 or 0) |
| Modulation PER, NP; SID wave bars (GENERATOR frame) | Drag, wheel (Shift: ×10 on PER) | Draw, adjust |
| SID wave bars (GENERATOR frame) | Right click | Precise slider (right click on it: back to 0) |
| Waveform of the Digidrums and DMA windows | Click | Load a file |
| Any control during MIDI learn | Click | Arm it for learning |

---

## 11. Privacy

STYM2149 opens no network connection and sends no telemetry. It exchanges data only with the host (audio, MIDI, parameters, project state), with the files you open or save through the File menu and the sample windows, and with its own settings files (section 9). This was checked on the source code and libraries, and at run time on Linux; the run-time check on Windows is **TO CONFIRM**.

---

## 12. Troubleshooting

**The DAW does not list STYM2149.**
- Check that the files are where section 1 says. On Windows, check that your DAW's plugin folders include `C:\Program Files\Common Files\VST3` (or `…\CLAP`), then rescan. On Linux, let the host rescan.
- Use a format your DAW supports: there is no VST2 build, no AU and no AAX in 0.9.0, and Ableton Live 11 has no CLAP.
- STYM2149 is listed under the vendor "PRISM", as an instrument.

**Windows blocks the installer.** SmartScreen shows "Windows protected your PC" because the installer is not signed: click **More info**, then **Run anyway**.

**No sound.**
- The focused track must be on (its tab), and notes must be within B0 to C9.
- In Omni routing, MIDI plays the focused track; in Per channel routing, channel n plays track n.
- Check the Mixer for Mute and Solo, and the Master level.
- While the YM Player plays a tune, it replaces the instrument's output: close the YM Player.

**The sequencer does not play in my DAW.** Start the DAW's transport (Play and Space in the plugin do nothing in Host sync). Make sure the tracks with notes are on. If the pattern plays once and stops, turn **Loop** on in the Step Sequencer transport.

**A separate output is silent.** Outputs enabled while the plugin runs stay silent until the host prepares the plugin again (stopping and starting the audio engine, or reloading the project). Chip outputs are silent in 16 ch.

**An export entry is greyed.** VBL dump, YM3, YM6 and SNDH need Authentic 3ch. Song exports need Song entries.

**Learned MIDI controls do nothing.** They respond only while the plugin window is open.

**CPU.** The engine's worst case measured on the reference machine (96 voices with noise on every track) is about 2.5 % of one core. The editor's drawing is separate from audio and costs more in a large window (an editor frame with the demo pattern playing measured 2.0 ms at 80 % on a 125 % display, 6 ms at 160 %): close the plugin window or make it smaller if the user interface feels slow. At host rates above clk/8, Box and Retro resampling fall back to Sinc.

**Linux.** The plugins and the application need ALSA, FreeType, Fontconfig and an X11 display. If the application does not start from a terminal by name, `~/.local/bin` is probably not in your `PATH`: use the full path. The exact minimum distribution is **TO CONFIRM**.

**Notes stop when I change a setting.** Changing the voice mode, the computer (ST/STE) or the generator stops the notes that are playing; this is expected.

**Reset preferences.** Close every instance and delete the settings file (section 9).

---

## 13. Licences and credits

STYM2149 is free software under the **GNU Affero General Public License version 3** (AGPLv3): you may share and modify it under its terms (see `LICENSE`).

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

STYM2149 builds on:

| Component | Author | Licence | Use |
|---|---|---|---|
| AtariAudio | Leonard/Oxygene (Arnaud Carré) | MIT | Basis of the chip emulation; Atari machine that plays SNDH files |
| ym2149-rs | slippyex | MIT | Reference for the emulation |
| Musashi | Karl Stenerud | MIT | 68000 emulator used to play SNDH files |
| ICE 2.4 unpacker | Hans Wessels | Public domain | Packed SNDH files |
| pylzh | Hirofumi Ukawa | Apache-2.0 | LHA decompression of YM files |
| EmuTOS system fonts and desktop icons | The EmuTOS team | GPL (the fonts: v2 or later) | GEM fonts of the interface; floppy disk and trash icons of the desktop |
| JUCE 8 | Raw Material Software | AGPLv3 | Plugin framework, audio, MIDI |
| zlib, libpng, libjpeg, HarfBuzz, SheenBidi, FLAC, Ogg Vorbis | (via JUCE) | Permissive licences | Compiled into the plugin by JUCE |
| VST3 SDK, CLAP, clap-helpers, clap-juce-extensions, LV2, ASIO SDK | Steinberg, free-audio, LV2 project | See their texts | Plugin formats and Windows audio drivers |

The Information window (Atari menu) shows these credits. The full list of components, versions and licences is in `THIRD_PARTY.md`; the licence texts are in the `licenses` folder installed with STYM2149.
