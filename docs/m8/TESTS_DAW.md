# STYM2149 — DAW test protocol (M8, B2)

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).

Spec §10 asks for STYM2149 to be checked in real hosts. Indus runs this protocol on Windows (D-177) in the DAWs he owns; the results go in the tables at the end and into the M8 test report. Anything that fails is reported in the M8 thread with the DAW, the step and what happened; it is fixed, then the step is run again.

**TO CONFIRM** marks a fact about a DAW that was not checked against its documentation or a real install.

## 1. Scope

| DAW | Version | Formats to test | Notes |
|---|---|---|---|
| FL Studio | 26 | VST3, CLAP | CLAP support in FL Studio: **TO CONFIRM** (skip the CLAP column if the browser does not list it) |
| Ableton Live | 11 | VST3 | No CLAP in Live 11 |
| Cubase Pro | 15 | VST3 | |
| ACID Pro | 10 | VST3 | VST3 support: **TO CONFIRM** |
| Maschine | 3 | VST3 | VST3 support in Maschine: **TO CONFIRM**. STYM2149 has no VST2 build: if Maschine only loads VST2 there, write "not loadable" |
| Reason | 13 | VST3 | Rack Extension format not built; VST3 support: **TO CONFIRM** |

Not tested (no copy available, D-177): Reaper, Bitwig Studio, Logic Pro, Studio One. macOS (AU, auval) and AAX are out of M8 (D-175).

Plugin files: `STYM2149.vst3` (folder) and `STYM2149.clap`, from the Windows installer built by the CI (D-181). It installs for all users, with administrator rights, into `C:\Program Files\Common Files\VST3` and `C:\Program Files\Common Files\CLAP` (D-199), the standard folders that DAWs scan; the standalone application goes to `C:\Program Files\PRISM\STYM2149` (D-269). If a DAW does not list STYM2149 at step 1, note it with its plugin folder settings. Note the build (CI run or commit) in the results.

## 2. Steps

Run every step in every DAW and format. Mark each one **OK**, **FAIL** (with a short note), or **N/A** (the DAW has no such feature).

| # | Step | Expected |
|---|---|---|
| 1 | **Scan.** Rescan plugins. | STYM2149 by Indus (vendor name, D-060) is listed as an instrument, with no scan error or blacklisting. |
| 2 | **Instance.** Insert STYM2149 on an instrument track, play notes from the MIDI keyboard or the piano roll. | Sound on the main stereo output, no clicks at note on and off. The window opens at a zoom that fits the screen. |
| 3 | **Two instances.** Insert a second STYM2149 on another track, open both windows, play both. | Each instance plays and draws on its own; closing one window does not affect the other. |
| 4 | **Automation.** Record a parameter move (Volume of track 1, then Chip Clock), play it back with the window open and closed. | The control follows the automation in the window; the sound follows it. |
| 5 | **Window to host.** Move a knob in the window while the DAW records automation. | The DAW records the move as one gesture (touch and release), with no stray points. |
| 6 | **State.** Write a pattern, a Song, an instrument and a mixer setting; save the project, close it, reopen it. | Everything comes back: parameters, patterns, Song, instruments, mixer, window positions. |
| 7 | **Host sync.** Open the Step Sequencer (its clock is Host sync by default in a DAW, D-114), start the DAW transport at bar 1, then at bar 3, loop two bars, change the tempo. | The sequencer starts with the transport, at the right step, follows loops and tempo, stops with the transport. |
| 8 | **Multiple outputs.** Enable the 16 track outputs and the 2 chip outputs (DAW-specific routing), route tracks 1 and 2 to their own mixer channels. | Each routed track sounds only on its own channel; the main output keeps the others. |
| 9 | **Voice mode and machine.** During playback, switch Voices to Authentic 3ch, then Dual chip 2x3ch, then back to 16 ch; switch Computer to STE and back to ST. | No crash, no lasting silence; the new mode sounds after the switch. |
| 10 | **Buffer and sample rate.** In the DAW audio settings, change the buffer size (smallest and largest offered) and the sample rate (44.1, 48, 96 kHz) with the project open. | Playback resumes with the same sound and tempo. |
| 11 | **Offline render.** Export the project to WAV (offline bounce), then record the same passage in real time. | Both files sound the same, no dropouts in the offline file. |
| 12 | **Window and zoom.** Use Options > Zoom > 50 %, 100 %, 200 %, and resize the window by its corner if the DAW allows it. Close and reopen the window during playback. | Sharp pixels at each zoom, mouse clicks land where expected, the zoom is kept for the next window. No crash when the window closes during playback. |
| 13 | **Keyboard.** With the window focused, use the computer keyboard (Computer Keyboard window, sequencer grid shortcuts). | Keys reach STYM2149 and are not taken by the DAW (or note which DAW option gives them to the plugin). |
| 14 | **Files.** File > Save > Preset, then File > Load > Preset; File > Export pattern > WAV; in Authentic 3ch, File > Export song > YM6; File > Import > MIDI file. | The DAW's file dialog opens; files are written and read back. |
| 15 | **MIDI learn.** Options > MIDI learn, move a control, turn a hardware knob. | The knob drives the control; the map is kept after reopening the project. |
| 16 | **CPU.** Play the demo pattern on all 16 tracks with 6-note chords (or the heaviest project at hand) and note the DAW's CPU meter. | No dropouts at the DAW's default buffer size. Write down the meter reading. |
| 17 | **Close.** Close the project with the window open and playback running, then quit the DAW. | No crash, no hang, no error message. |

### Standalone app (Windows)

Run once, with `STYM2149.exe` from the same installer.

| # | Step | Expected |
|---|---|---|
| S1 | **Devices.** Options > Audio/MIDI settings: pick the sound card and a MIDI keyboard, play. | Sound and MIDI work; after quitting and relaunching, the same devices are selected. |
| S2 | **Session kept.** Write a pattern, resize the window and set Language to Français in Options > Settings. Quit with File > Quit > OK, relaunch. Then do the same with the window's close box. | Both times the pattern, the window size and the tooltip language come back: the app saves its state when it quits and reopens it (P415), and the preferences are not lost. |
| S3 | **Files.** Look in `%APPDATA%\STYM2149`. | `Preferences.settings` (zoom, tooltip language...) and `STYM2149.settings` (devices and session of the app) are both there. |
| S4 | **MIDI clock** (needs a program or device that sends MIDI clock, for example a DAW's MIDI clock output through a virtual MIDI port; skip if none). Pick that port as MIDI input, set Clock to MIDI ext (main window, SYSTEM), press Play in the Step Sequencer, then start, stop and continue the clock source at two tempos. | The BPM slider is greyed; the sequencer waits for Start, follows the source's tempo, stops and continues with it (D-188). |

## 3. Results

Copy one table per DAW and format.

### DAW: ……… version ……… format ……… build ………

| Step | Result | Notes |
|---|---|---|
| 1 Scan | | |
| 2 Instance | | |
| 3 Two instances | | |
| 4 Automation | | |
| 5 Window to host | | |
| 6 State | | |
| 7 Host sync | | |
| 8 Multiple outputs | | |
| 9 Voice mode and machine | | |
| 10 Buffer and sample rate | | |
| 11 Offline render | | |
| 12 Window and zoom | | |
| 13 Keyboard | | |
| 14 Files | | |
| 15 MIDI learn | | |
| 16 CPU | | |
| 17 Close | | |

### Standalone app: build ………

| Step | Result | Notes |
|---|---|---|
| S1 Devices | | |
| S2 Session kept | | |
| S3 Files | | |
| S4 MIDI clock | | |
