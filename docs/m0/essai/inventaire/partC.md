# Parity — Part C (JS lines 7000-10541)

Note: the slice does **not** contain the code of the AudioWorklet (the `WORKLET` string starts at line 1100). It holds the song editor (end), the presets/files, all the exports/imports, the chip conformity check, the GEM menus, the sequencer toolbar, the transport, the scale, refreshUI, the register monitor and the scope, the on-screen keyboard and TKL, the global shortcuts, Web MIDI and MIDI learn, and the direct editing of values.

| Domain | Control / function | Element (id or JS function) | Behaviour, range, default | Lines |
|---|---|---|---|---|
| Patterns/Song | Drag and drop of Song blocks | pointerdown/pointerover on `#songList .btn`, global pointerup | Moves a block, or the multiple selection if the block is part of the selection, to the `drop` target. One undo step (`pushSongUndo`). The selection follows the moved blocks. | 7000-7017, 7033-7042 |
| Patterns/Song | Block label | `songLabel` | `A..D` + no. 1-8 + `x`rep + `♪bpm` if the block has its own tempo | 7018-7021 |
| Patterns/Song | Song list (chips) | `refreshSong`, `#songList` | Block playing = `.on`, selection = `.ssel`, keyboard cursor = `.cur`. Rebuilds `seqTbar` when the nav mode changes. | 7022-7116 |
| Patterns/Song | Click / Shift+click on a block | `b.onclick` | Click: single selection. Shift: adds to or removes from the selection. Sets `songCur`. | 7043-7049 |
| Patterns/Song | Block context menu | `contextmenu` → `openGemMenu` | Entries: Repeat x1/x2/x4/x8/x16, Duplicate(n), Cut(n), Copy(n), Paste, Move left, Move right, Remove(n), Tempo... Acts on the selection, one undo per action. Paste inserts after the clicked block. | 7050-7106 |
| Patterns/Song | Tempo per block | "Tempo..." entry → `gemPrompt` | Block `bpm` bounded to 20..300. Empty field = Song tempo (removes `bpm`). Applied at once if playing in Song mode. | 7065-7078 |
| Patterns/Song | Song keyboard navigation | `songKey` (songWin window in front) | ←/→: cyclic cursor. Home/End. Insert or A: inserts the current pattern after the cursor. Delete/Backspace: deletes the selection or the block at the cursor. D: duplicates. `=`/Num+: rep+1. `-`/Num-: rep-1 (min 1). X/C: cut/copy. V: paste after the cursor. Enter: jumps to the block (loads the pattern, applies its tempo). | 7117-7186 |
| Windows/UI | Song window | `openSongWin`/`closeSongWin`, `#songMode`, `#songClose`, `mkWinDrag('songWin','songBar')` | Opening turns Song mode on (`songOn=1`, remembers `songBaseBpm`). Closing leaves it. Draggable window. | 7187-7193 |
| Patterns/Song | Add to Song | `#songAdd` | If the last block is the same pattern: rep++. Shift+Add: new entry `{pat,rep:1}`. | 7194-7200 |
| Patterns/Song | Block tempo during playback | `applySongBpm` | tempo = `SONG[i].bpm` ‖ `songBaseBpm` ‖ `GLOB.bpm`. Updates #bpm and sends `sendSeq`. | 7201-7208 |
| Patterns/Song | Chaining during playback | `songQueue`, `songArm`, `songOnWrap`, `songOnPa` | Queues the next pattern (`patQueueMsg`) after `rep-1` loops. At the end of the Song without Loop: stop and back to 0. Loops if `loopOn`. | 7209-7239 |
| Sequencer | Rec (window button) | `#seqRec` | Toggles `recOn` | 7242 |
| Sequencer | Figure (length of the written note) | `#seqFig`, `figItems`, `setFig` | Menu of the `FIGS` with icons and separators `FIG_SEPS`. Shows the duration in steps = round(duration×steps per beat of the resolution), min 1. Disabled on percussion tracks. | 7243-7254, 9115-9120 |
| Timing | Grid resolution | `#seqReso` | `RESOS` menu (separator before the triplets). Sets `GLOB.reso`, rebuilds the grid, `sendSeq`. | 7255-7263 |
| Sequencer | Ghost notes | `#seqGhost` | Toggles `ghostOn` (display) | 7264-7266 |
| Sequencer | Overview | `#seqOver` → `#overRow` | Toggles `overOn`. Shows the overview row (`buildOverview`). | 7267-7271 |
| Timing | Time signature | `#seqSig` | `SIGS` menu, sets `GLOB.sig` | 7272-7278 |
| Sequencer | Step recording | `#seqStepRec`, `stepRecord` | Writes the note at the cursor then advances by 1 (modulo plen or steps). Percussion: slot = (note%12)%8. Otherwise applies `snapToScale`, oct 0..9 (0 if equal to seqOct). vol = max(1, round(vel/127×15)). Takes priority over real-time Rec. | 7279, 7298-7316 |
| Sequencer | Metronome | `#seqMetro`, `metroBeep` | Toggles `GLOB.metro`. Web Audio sine oscillator, outside the chip. 1760 Hz at gain 0.4 on the 1st beat of the bar, 880 Hz at gain 0.24 on the other beats. Exponential decay down to 0.0001 in 45 ms, stop at 50 ms. | 7280-7297 |
| Sequencer | Real-time recording | `recordNote` | If Rec + playback: writes at the step being played (`lastStep`, or `lastSa % plen`), scale snap, vol from the velocity (/127×15, min 1) | 7317-7329 |
| Patches/Presets | Save a preset | `savePreset` | JSON `{app:'STYM2149', v:1, glob, curPat, chans[], pats[], song[], songOn, kit}`. Resets `workDirty` to 0. File `prism-ym2149-preset.json`. | 7332-7351 |
| Patches/Presets | Load a preset | `loadPresetObj`, `#presetFile` | Refuses if `app≠'STYM2149'`. Migrates the old kit (hat.dec in %): hat=45×s, ohat=9×s, chat=90×s. Defaults: level 1, seqOct 3, gate 0.5, arpeggio table padded to 16. pats limited to 32. Block bpm bounded to 20..300. Resyncs the whole UI. | 7352-7397, 7477-7483 |
| Import/Export | Default file names | `fileNames` | preset/pattern/song/kit: `prism-ym2149-{preset,pattern,song,kit}.json` | 7398-7403 |
| Chipdrum | Save the kit | `saveKit` | `{kind:'drumkit', kit:KIT}` (indented JSON) | 7404-7407 |
| Windows/UI | GEM prompt box | `gemPrompt`, `#promptWin/#promptMsg/#promptInput/#promptOk/#promptCancel` | Enter = OK, Esc = Cancel, optional `maxLen` | 7413-7429 |
| Import/Export | Save as | `ensureJson` + `gemPrompt` | Adds `.json` if missing. The name is remembered in `fileNames`. | 7408-7412, 8479-8482 |
| Patterns/Song | Save / load a pattern | `savePattern`, `#patternFile` | `{app, kind:'pattern', v:1, pattern}`. Loading replaces the current pattern (with undo). | 7430-7434, 7441-7456 |
| Patterns/Song | Save / load a Song | `saveSong`, `#songFile` | `{app, kind:'song', v:1, song, songOn, pats}`. Loading keeps only pat and rep: the **bpm per block is lost**. | 7435-7440, 7457-7476 |
| Audio engine | Offline engine | `makeOfflineEngine` | Instantiates the WORKLET class through `new Function`. Pushes the patch (+master, mode3ch, digiCh), the 16 channels, the Digidrum samples and seq `{on:1,bpm,gate,len}` (without swing). | 7492-7504 |
| Timing | Length of a pattern in samples | `patternSamples` | `round(SR×60/(bpm×4)×steps)`. Always 4 steps per beat, the resolution is ignored. | 7505-7507 |
| Import/Export | YM3 export | `ym3Blob`, `exportYm3`, `exportSongYm3` | `'YM3!'` + 14 registers × N frames, ordered register by register (interleaved). Requires `ctx`. `prism-ym2149-{pattern,song}.ym`. | 7508-7526 |
| Import/Export | Offline rendering | `renderBuffer` | Blocks of 2048 samples at most | 7527-7537 |
| Import/Export | WAV export | `wavBlobDv`, `exportWav`, `exportSongWav` | 16-bit stereo PCM at `ctx.sampleRate`, clipping at ±1, ×32767. The Song is rendered segment by segment × rep. `prism-ym2149-{pattern,song}.wav`. | 7538-7560, 8323-8345 |
| Control/Monitor | Chip conformity indicator | `chipIssues`, `refreshChip`, `#chipTxt` | Shows "CHIP: OK" or "CHIP: n" (warn class). A click opens the list. Checks: more than 3 tracks outside 3ch; dual chip; chords; TP>4095 or <1; ADSR Smooth; ADSR on Sync Buzzer; Digidrum; Chipdrum outside 3ch; SID ratio>4; steps/s > VBL; arp > VBL; orn; vibrato; non-linear slides; pan, PAN row, levels; master≠0.7. | 7639-7746 |
| Windows/UI | Message box | `gemInfo`, `#msgWin/#msgTxt/#msgOk`, `msgKey` | Enter or Esc close it | 7629-7633, 8464-8473 |
| Windows/UI | Busy cursor (bee) | `gemBusy`, `busyWrap`, `withBusy` | `busy` class on body. Wrapped functions: exportWav, exportMidi, exportYm (**does not exist**), exportSndh, exportStems, savePreset, loadPresetObj, renderOffline (**does not exist**). | 7608-7628, 9642-9643 |
| Windows/UI | GEM underline of editable fields | `charWidth`, `gemUnderMark`, `gemFillMark`, MutationObservers | CSS variables `--cw` and `--typed` computed from the width of the character "0" | 7562-7607 |
| Options/Menus | Register exports submenu | `regSub` | VBL, YM3 and SNDH are greyed out outside "Authentic 3ch" (explanatory tooltip) | 7634-7638 |
| Import/Export | YM3 import | `parseYM3` | Accepts `YM3!` and `YM3b` (4 loop bytes at the end). Interleaved data, 14 registers. | 7749-7762 |
| Import/Export | SNDH import (PRISM only) | `parseSndh` | Requires `SNDH` in the first 4 KB and the `PRSM` marker. Reads subsong 1: 16-bit mask then changed bytes; R13=$FF unless written. An ordinary SNDH is refused (it would need a 68000 interpreter). | 7763-7787, 7827-7832 |
| Import/Export | YM5/YM6 import | `parseYM56` | Checks `LeOnArD!`. Reads the number of frames, the attributes (bit 0 = interleaved), skips the digidrums, reads clock and VBL frequency, title/author. Not interleaved: 16 bytes per frame. | 7788-7813 |
| Import/Export | LHA detection | `isLha` | `-lh` bytes detected: message asking to decompress first | 7814-7824 |
| Import/Export | YM player | `ymLoad`, `#ymWin`, `#ymName`, `#ymPos`, `#ymPlay`, `#ymRew`, `#ymClose`, `#ymFile` | The VBL of the file sets `GLOB.vbl`: 60→60, 70 or 71→70, otherwise 50. Messages `ymload{rows,loop:1}` and `ymplay{on,rewind}`. Position shown as "n / total (s)" with s = frames/vbl. | 7817-7869 |
| MIDI | MIDI import (SMF 0/1) | `parseMidiFile`, `midiImport`, `#midiInFile` | SMPTE refused. First tempo meta event → bpm bounded to 20..300. 1 step = div/4 ticks. At most 32 patterns of `GLOB.steps`. Melodic channels then channel 10 → tracks (3×chips in 3ch, otherwise 16). Channel 10 becomes Chipdrum (effect 4) through `GM_SLOT`. Held notes → `tie`. vol = vel/127×15 (1..15). Replaces the Song (one entry per pattern). | 7872-8012 |
| MIDI | GM → kit slot mapping | `GM_SLOT`, `GM_DRUM=[36,38,39,42,46,44,45,41]` | Kick, Snare, Clap, Hat, OHat, CHat, Tom1, Tom2. Fallback: ≤37 kick, 39/40 clap/snare, ≤41 snare, 42 hat, 44 CHat, 46 OHat, ≤50 tom1, otherwise tom2. | 7923-7933, 8015 |
| MIDI | MIDI export (SMF 1) | `midiBlob`, `exportMidi`, `exportSongMidi` | PPQ 96, 1 step = 24 ticks. Track 0 = tempo. One track per channel used, named "PRISM chN". Percussion on channel 10, GM_DRUM notes, duration 0.5 step. Melodic: duration len×(gate+0.5) steps, held notes extended. vel = vol/15×127. Channel 9 is skipped. `.mid` | 8014-8097 |
| Import/Export | SNDH export | `packFrames`, `sndhBlob`, `exportSndh`, `exportSongSndh` | Header: 3×BRA, SNDH, TITL, COMM/CONV "STYM2149 from PRISM", `##NN`, `TC<vbl>`, TIME, HDNS. Position-independent 68k player (writes $FFFF8800/8802, loops). exit: R7=$3F, R8-10=0. `PRSM` marker. Song: subsong 1 = song, 2 = current pattern. Compressed data: 14-bit mask + changed bytes (R13 written only if ≠$FF). | 8099-8231 |
| Import/Export | Stems export (ZIP) | `zipStore`, `crc32`, `exportStems`, `exportSongStems`, `stemName`, `isolate` | ZIP in store mode. One WAV per track that contains notes (`mgain` isolated). Names `prism-track-{A,B,C,Digi}` in 3ch, otherwise `01..16`. `prism-ym2149-{pattern,song}-stems.zip` | 8233-8322 |
| Import/Export | Register capture per frame | `ymRegsFrame`, `collectYmFrames`, `collectYmFramesSong` | One frame = round(SR/vbl) samples. R0-R5 = tpEff‖TP. R6 = NP of the last voice with noise. R7 = mixer. R8-10 = vol4, or $10 in envelope mode (except SID). Non-chip Digidrum and Chipdrum → 0. R11/12 = EP of the voice with the envelope. R13 written **only once** (first shape), $FF afterwards. | 8346-8430 |
| Import/Export | VBL export (.s) | `vblSource`, `exportVbl`, `exportSongVbl` | ASM source `dc.b` of 14 bytes per frame, `ym_frames: dc.w n`, `ym_data:`. The header says "(50 Hz)" hard-coded. | 8431-8455 |
| Import/Export | Song segments | `songSegs`, `loadSegIntoEng`, `requireSong` | smp = SR×60/(GLOB.bpm×4)×steps. The **tempo per block is ignored** in the exports. An empty Song raises an alert. | 8389-8408 |
| Options/Menus | Fuji menu | `#mFuji` | Information (`#infoWin`), Shortcuts (^?) → `openHelp` | 8458-8463 |
| Options/Menus | Menus open on hover | `mouseenter/mouseleave` listeners on mFuji/mFile/mEdit/mOptions | Open on hover. Closing delayed by 260 ms (menu) and 220 ms (submenu). | 8474-8477, 8622-8627 |
| Options/Menus | File menu | `#mFile` | Load / Save / Save as (Preset, Pattern, Song, Drum kit). Import (YM dump, MIDI file). Export pattern (MIDI, WAV, Stems). Export song (VBL dump, YM3, SNDH \| MIDI, WAV, Stems). Demo pattern ^D. Quit ^Q. | 8478-8504 |
| Options/Menus | Options menu | `#mOptions` | Zoom (50-200 %). Audio output. MIDI input. MIDI learn / Stop. Clear MIDI map. VBL frequency 50 (PAL) / 60 (NTSC) / 70 Hz (Mono). Keyboard layout Auto/QWERTY/AZERTY/QWERTZ. Note names (`NOTE_LANGS`). Tooltips On/Off. | 8505-8530 |
| Windows/UI | Zoom of the interface | `setZoom`, `autoZoom`, `ZOOMS=[50..200 pas 25]` | Applies `scale` on `#zoomRoot`. Stored in localStorage `prismYmZoom`. Automatic value: <1100×700 → 50, <1400×850 → 75, >2400 → 125, otherwise 100. | 8531-8559 |
| Audio engine | Audio output | `pickAudioOut` | Lists the `audiooutput` devices then `ctx.setSinkId`. First entry "System default (no ASIO in browsers)". | 8562-8577 |
| MIDI | MIDI input | `pickMidiIn`, `hookMidiInputs`, `midiOff` | All inputs or one given input. Inputs greyed out without access (denied / unsupported / not yet). Status shown in `#miditxt`. | 8578-8594, 10264-10274 |
| Keyboard | Keyboard layouts | `LAYOUTS` | qwerty, azerty, qwertz: 18 note keys + 2 octave keys | 8595-8599 |
| Options/Menus | Generic GEM menu | `openGemMenu`, `openGemSub`, `closeGemMenu` | Check mark ✓ in front of the active entry, shortcut aligned right, icons, greyed-out entries with a reason (title), cascading submenus, columns (`chordCols`), repositioning inside the window. Closes on a click outside. | 8602-8748, 8782-8788 |
| Step rows | Step volume popup | `openVolMenu` | Vertical slider 0..15 with live listening | 8750-8781 |
| Sequencer | Grid tools | `TOOLS`, `buildSeqTools`, `setSeqTool`, `#seqTools` | move, draw (default), erase, split, join, select. The cursor changes with the tool. | 8789-8822, 8840-8889 |
| Editing/Undo | Clipboard buttons | `CLIPS`, `editClip`, `refreshClipBtns` | Copy, Cut, Paste (Ctrl+C/X/V). Greyed out when there is nothing to do. | 8823-8839, 8857-8877 |
| Sequencer | Transport bars | `buildTbars`, `wireTbars`, `#seqTbar`, `#songTbar` | trPlay (Play/Stop), trRec (if armed while stopped, starts playback), trLoop (`loopOn`). In Song mode: trStart, trPrev, trNext. | 8890-8955 |
| Patterns/Song | Play from the start / previous / next block | `transportStart`, `songStep` | Goes back to block 0 and applies its tempo. Cyclic Prev/Next. | 8956-8978 |
| Sequencer | Play / Stop | `#seqPlay` | Toggles `GLOB.seqOn`. In Song mode, starts at block 0. Stop: disarms Rec and clears the step shown. | 8979-8999 |
| Editing/Undo | Block copy / clear / paste | `blockCopy`, `blockClear`, `blockPaste` | All the rows, or only `selRow`. Cleared values: pat=-1, vol=15, 0 elsewhere. | 9001-9024 |
| Sequencer | Track rotation | `#rotL`, `#rotR`, `rotateLane` | Rotation by one step of the rows pat, tie, slide, afl, vol, mix, fx, slt, cut, oct, gat (**not** pnr/rtg/cnd/slc/ton nor the chord voices). With a selection, the selection is shifted instead (`shiftSel`). | 9025-9041, 9058-9085, 9155-9156 |
| Sequencer | Transposition | `#trDn`, `#trUp`, `#trOctDn`, `#trOctUp`, `transposeLane`, `transposeSel` | ±1 and ±12 semitones. All or nothing: the track must stay within 24..108, the selection within PLAY_LO..MAP_HI. Snap to the scale if on. No effect on percussion tracks. | 9042-9057, 9086-9110 |
| Scales | Root, scale, constraint | `#scaleRoot`, `#scaleSel`, `#scaleSnap`, `refreshScale`, `refreshScaleEnable` | Root among the 12 note names, scale among `SCALE_NAMES`. Snap toggles `scaleSnap`. Disabled on percussion tracks. `sendPatch` (Scale-locked). | 9111-9153 |
| Sequencer | Playback follow | `#seqFollow` | Toggles `followOn` | 9154 |
| Sequencer | Number of steps | `#seqSteps` | 1..256, default 16 | 9157-9161 |
| Sequencer | Vertical scrolling | `#seqTr` | `viewAbsBase` 23..108, default 48 | 9162-9165 |
| Sequencer | Clear the track | `#seqClear`, `clearLane`, `laneHasNotes` | Confirmation if the track contains notes. Empties all the rows and the chord voices 2..POLYMAX. | 9166-9197 |
| Other | Protection of unsaved work | `beforeunload`, `workDirty` | The browser asks for confirmation as long as the work is not saved (only savePreset resets it to 0) | 9198-9205 |
| Other | Quit | `#mainClose`, `quitApp`, `gemConfirm` | Confirmation, playback stopped, `allOff`, windows closed, "Program terminated" screen with a Restart button (reload) | 9206-9226 |
| Windows/UI | Confirmation box | `gemConfirm`, `#confirmWin` | **Enter and Esc cancel**. Tab moves from one button to the other. Default focus on Cancel. | 9227-9247 |
| Other | Demo pattern | `demoPattern`, `doDemo` | Confirmation if channels 1-4 contain data. Ch1 bass arp Table [0,3,7]. Ch2 SID lead. Ch3 Chipdrum. Ch4 Sync Buzzer stabs (EP 1, shape 8), outside 3ch only. seqOct 4/2/4/3, levels 1/0.8/1/0.5. | 9248-9328 |
| Sequencer | Track length (polymetry) | `#plenBtn` popup | Slider and field 1..64. "Global" resets plen=0 (follows the pattern), "Set" applies. Label "Len n" or "Len =". | 9329-9364, 9416 |
| Timing | BPM | `#bpm` | 60..240 in steps of 0.01, default 120 | 9365-9367 |
| Timing | Swing | `#swing` | 50..75 %, default 50 | 9368-9372 |
| Sequencer | Gate | `#gate` | 5..95 %, default 50. Value specific to each channel (`CH[cur].gate`). | 9373-9377 |
| Windows/UI | Channel panel refresh | `refreshUI` | Source, volume mode, NP, vol, vmin, ratio, ef (from EP), shape (hex + SVG), effect. Arp disabled on percussion. Vibrato depth/rate. chLevel. ADSR (4-bit reg or audio gain mode, forced in 3ch). Shape disabled with a fixed volume. digi/ndrum/sid panels. Keyboard ↔ pads. digiCh line in 3ch. | 9380-9457 |
| Control/Monitor | Voice LEDs | `#voices` .vled ×16 | 16 LEDs | 9460-9464 |
| Control/Monitor | Register monitor | `#regs`, `#reginfo` | Shows R0, R1, R6, R7, R8, R11, R12, R13 in hex every 120 ms, for the current patch (initial TP 284). Info: channel, effect, "3CH AUTH (shared env+noise)" or "16CH". | 9465-9487 |
| Control/Monitor | Main oscilloscope | `drawScope`, `#scope` | Trigger on a rising zero crossing, 1024 samples, amplitude ×0.45 | 9489-9507 |
| Keyboard | On-screen keyboard | `buildKbd`, `#kbd`, `#octv` | 4 octaves (28 white keys) from min(baseOct,6). Outside B0..C9: key marked `nope`. | 9510-9546 |
| Keyboard | Keyboard destination | contextmenu on `#kbd`/`#pads` → `chanMenu` | Choice of the voice played (Voice A/B/C/Digi in 3ch). Disabled channels greyed out. | 9547-9562 |
| Keyboard | Keys lit during playback | `seqKbdLight`, `seqKbdClear`, `litSwap` | Keys or pads lit, polyphony included. Held notes keep their state. | 9563-9603 |
| Chipdrum | Percussion pads | `buildPads`, `#pads` | 8 pads, note 48+i, velocity 118. Chipdrum: `DRUMFULL` names. Digidrum: "Slot n" (empty ones marked), the pad selects the slot. | 9604-9635 |
| Windows/UI | Window opening buttons | `#openKitBtn`, `#openArpBtn`, `#openOrnBtn`, `#openSeqBtn` | Open kit, arp, modulation, sequencer | 9636-9640 |
| Keyboard | Note playing | `press`, `release`, `forceRelease` | Out of range ignored (except percussion). Latch: a new press releases; with the mouse, mono. Arp on → `sendHeld`. | 9645-9670 |
| Keyboard | Mouse glissando | pointerover on `#kbd` | Button held: the note follows the cursor | 9671-9690 |
| Keyboard | Layer / Latch | `#kbLayer`, `#kbLatch` | Layer: `kbmode{focus,layer}` + panic. Latch: toggle, releases everything when switched off. | 9691-9702 |
| Keyboard | Octave − / + (panel) | `#octDown`, `#octUp` | baseOct bounded to **0..6** | 9703-9704 |
| Keyboard | Key mapping | `CODEMAP`, `updateKbdHint` | A W S E D F T G Y H U J K O L P ; ' → 0..17 (C to F+1). Automatic layout through `navigator.keyboard.getLayoutMap`. | 9706-9731 |
| Keyboard | TKL drawing | `buildTkl`, `TKL_ROWS`, `TKL_LABELS`, `TKL_OTHER`, `#tkl`, `#kbdLay` | Note keys (clickable), Oct-/Oct+ (Z/X), hints Play/Stop, Panic, Metro, Channel, Pattern | 9732-9860 |
| Keyboard | Numeric keypad | `buildTklNum`, `#tklnum` | Num0..9 = direct octave | 9861-9892 |
| Keyboard | TKL keyboard octave | `#kbdOctDn`, `#kbdOctUp`, `kbdOctShift`, `#kbdOctV`, `#kbdRange` | baseOct 0..9. Range shown with "partly out of the chip range". | 9893-9917 |
| Keyboard | Held notes on octave change | `#kbdHeldSel`, `retuneHeld` | Keep (default) or Follow (held notes transposed again) | 9911-9940 |
| Windows/UI | Keyboard window | `openKbdWin`, `#kbdMapBtn`, `#kbdClose`, `mkWinDrag('kbdWin','kbdBar')` | Close = panic + everything off | 9973-9984 |
| Sequencer | Scissors cursor (Alt) | `setCutCursor` | `cut` class on the grid while Alt is held (except percussion) | 9988-9994 |
| Editing/Undo | Note editing at the cursor (keyboard) | keydown, seqWin in front | Ctrl+C/X: copy/cut the note. Ctrl+V: paste. `]`: lengthen by one step. `[`: shorten. `\`: split at the cursor. J: join to the next one. | 10003-10031 |
| Editing/Undo | Rectangular selection (keyboard) | `rectSel` | ←/→: moves by one step. ↑/↓: ±1 semitone, Shift = ±12. Del/Backspace: clears. Esc: cancels the selection. Ctrl+C/X/V (rectClip: vol, slide, slc, lengths). | 10032-10071 |
| Editing/Undo | Undo / Redo | Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y | `doUndo`/`doRedo` | 10073-10074 |
| Windows/UI | Window shortcuts | Ctrl+E seq, Ctrl+B arp, Ctrl+M mixer, Ctrl+G song, Ctrl+R modulation, Ctrl+P library, Ctrl+K kit | Ctrl+B and Ctrl+R have no effect on percussion tracks | 10075-10083 |
| Source | Effect shortcuts | `FX_KEYS` (Ctrl+V=0, S=1 SID, I=2 Digi, U=3 Sync, C=4 Chipdrum) | `setEffect`. Ctrl+C/V are only taken by the clipboard if there is a note or a clip at the cursor. | 10084-10085 |
| Options/Menus | Help, demo, quit | Ctrl+/ (or ?), Ctrl+D, Ctrl+Q | openHelp, demoPattern, quitApp | 10086-10088 |
| Sequencer | Space bar | Space | Play/Stop | 10092 |
| Control/Monitor | Esc | Escape | Closes the menu, then the top window (mix, ym, help, kit, arp, lib, orn, song, seq, scWinN). Otherwise panic (`allOff`). | 10093-10114 |
| Patterns/Song | Digits | Digit1-8, Shift+Digit1-4 | Selects the pattern slot 1-8. Shift: bank A-D. | 10115-10122 |
| Arpeggiator | Arpeggio table from the keyboard | arpWin in front | Arrows: 16×16 cursor. Enter: table[c] = 15-row, extends tlen. | 10124-10141 |
| Modulation | Modulation table from the keyboard | ornWin in front | Arrows: 64 columns, rows 0..24. Enter: ornTable = 12-row, extends ornLen. Del: resets to 0, nse 0, vol 15. N: toggles NSE. | 10143-10175 |
| Sequencer | ←/→ | ArrowLeft/Right | Grid open: cyclic step cursor. Otherwise (or Shift): previous or next channel (modulo 4 in 3ch, 16 otherwise). | 10176-10187 |
| Sequencer | ↑/↓ in the grid | ArrowUp/Down | Next row. The view scrolls between 24 and 84; percussion limited to the 8 rows. | 10193-10210 |
| Sequencer | Place / remove a note | Enter, NumpadEnter, Shift+Space | Toggles the note at the cursor, with preview | 10211-10226 |
| Sequencer | Clear a step | Delete/Backspace | pat=-1, vol=15, other rows 0 | 10227-10234 |
| Keyboard | Octave on the physical keyboard | Z/X (0..9), Numpad0-9 (keyboard window open), M = metronome | | 10236-10242 |
| Keyboard | Playing on the physical keyboard | `CODEMAP` + `kbdHeld` | Active only if the keyboard window is open. Velocity 127. Auto-repeat ignored. On release, frees the note actually played. | 10243-10260 |
| MIDI | Web MIDI initialisation | `initMIDI` | requestMIDIAccess; states ok, denied, unsupported. Hot reconnection through `onstatechange`. | 10275-10289 |
| MIDI | Incoming messages | `onMIDI` | F8 clock, FA start, FB continue, FC stop, F2 SPP → worklet (`mclk/mstart/mcont/mstop/mspp`). Note On/Off → press/release. Pitch bend ±2 semitones. CC → onCC. Notes learnt by learn → control (the note is not played). | 10290-10311 |
| MIDI | MIDI learn | `setLearn`, `midiBind`, `applyMidiMap`, `#learnBadge` | Click a slider or a button (with an id) then move a CC or play a note. Slider: min + v/127×(max−min). Button: click on a rising edge ≥64. Stored in localStorage `prismYmMidiMap`. | 10312-10352 |
| MIDI | Default CCs | `onCC` | 70 tone on/off, 71 noise, 72 NP 1..31, 73 envelope mode, 74 ef 0..4000, 75 shape v>>3, 76 effect floor(v/26) 0..4, 77 ratio 25..800, 78 vmin 0..15, 80 arp on/off, 81 arpRate 4..200, 82 play/stop, 83 bpm 60..240, 84 channel v>>3, 7 master 0..100 | 10353-10374 |
| Windows/UI | Direct entry of values | `makeEditable`, `maskEdit`, `sliderApply` | Click on the value → GEM-style masked field, rounded to the step then bounded. Masks: ratio 9.99, ef 999.9, arpRate 999, arpLen 99, seqSteps 999, bpm 999.99, gate 99, vibDepth 999, ornLen 99, vibRate 99.9, swing 99. No mask: np, vol, vmin, master, chLevel. | 10376-10528 |
| Envelope | Envelope frequency entry | `efv` | f in Hz → EP = round(2e6/(256×32×f)) bounded to 1..65535 | 10510-10517 |
| Envelope | Direct entry of EP | `epv` | Decimal or `$hex`, bounded to 1..65535 | 10530-10536 |

## DSP

The slice does not contain the AudioWorklet (`const WORKLET =` line 1100). Formulas found:

1. **Envelope frequency and step**
   - L7390: `const fCycle = 2000000 / (256 * 32 * p.EP);` then `$('ef').value = hzToEf(fCycle);`
   - L10513-10514: `P().EP = Math.max(1, Math.min(65535, Math.round(2000000 / (256 * 32 * f))));` then `$('ef').value = hzToEf(2000000 / (256 * 32 * P().EP));`
   - **To check**: the usual formula for the full cycle is `clock/(256·EP)`; the extra ×32 factor gives a displayed frequency 32 times lower. It must be compared with the worklet.
   - L8363/8372: EP exported raw `EP & 0xFF, (EP >> 8) & 0xFF`
   - L9482: monitor `p.EP & 0xFF, (p.EP>>8)&0xFF`

2. **Volume / DAC**
   - No DAC table and no dB formula in the slice.
   - Register volume (L8359-8362): `v.chip ? v.vol4 : (effect 2|4) ? 0 : (v.volMode && v.effect !== 1) ? 0x10 : v.vol4`
   - Monitor (L9480): `volReg = (p.volMode<<4) | p.vol4`
   - Velocity → 4-bit volume (L7311, 7327, 7969): `Math.max(1, Math.round(vel / 127 * 15))`
   - MIDI export (L8061): `Math.round(vol / 15 * 127)`

3. **Mixing**
   - No sum and no tanh in the slice.
   - WAV (L7556-7557): hard clipping `Math.max(-1, Math.min(1, x)) * 32767`
   - Stems (L8285): `c.mgain = j === i ? 1 : 0`
   - Metronome outside the chip (L7288-7295): sine at 1760 or 880 Hz, gain 0.4 or 0.24, `exponentialRampToValueAtTime(0.0001, t+0.045)`

4. **Noise LFSR**: absent from the slice. Only R6 = `np & 0x1F` (L8370) and CC72 `1 + round(v/127*30)` (L10360) appear.

5. **Timer, SID, Sync Buzzer, Digidrum**
   - No implementation in the slice.
   - Conformity check: `p.effect === 1 && (p.sidRatio||1) > 4` → timer too fast (L7690-7692). Digidrum "approximated at VBL rate" (L7686-7687). ADSR on Sync Buzzer (L7684-7685).
   - Comment of the VBL export (L8438-8439): "Timer effects (SID/Digidrum/Sync) run faster than the VBL and are frozen to their base state in this dump."
   - Effect codes (L9485): `['-','SID VOICE','DIGIDRUM','SYNC BUZZER','NOISE DRUM']`
   - Tone period (L7669-7670): `f = 440*2^((abs-69)/12)`; `tp = Math.round(125000 / f)` (2 MHz/16), limits 1..4095

6. **JSON schemas**
   - Preset (L7335-7346): `{app:'STYM2149', v:1, glob:{...GLOB}, curPat, chans:[{patch, enabled, level, name, seqOct, gate, mute, solo, pan, plen, arp:{...,table[16]}, lane:{LANEKEYS...}}], pats[32|null], song:[{pat,rep,bpm?}], songOn, kit}`
   - Pattern (L7432): `{app:'STYM2149', kind:'pattern', v:1, pattern}`
   - Song (L7437-7439): `{app:'STYM2149', kind:'song', v:1, song, songOn, pats}`
   - Kit (L7405): `{kind:'drumkit', kit:KIT}`
   - Internal pattern produced by the MIDI import (L7991): `{steps:N, chans:[{pat[64],vol[64],...LANEKEYS}]}`
   - Song entry: `{pat, rep, bpm?}` with bpm bounded to 20..300

7. **VBL rate**
   - Menu (L8515-8517): `GLOB.vbl = [50,60,70][i]; sendPatch();`
   - On import (L7833-7835): `GLOB.vbl = res.vbl === 60 ? 60 : (res.vbl === 71 || res.vbl === 70 ? 70 : 50)`
   - Frame (L8379, 8412): `frSamp = Math.max(1, Math.round(ctx.sampleRate / GLOB.vbl))`
   - SNDH `TC`+vbl and `TIME = round(frames/vbl)` (L8131-8133)
   - Conformity (L7696-7700): `stepHz = bpm/60*4 > vbl` → steps would be lost
   - Duration in the YM player (L7845): `ymFrames / GLOB.vbl`
   - The header of the VBL .s says "(50 Hz)" hard-coded (L8435)

8. **File formats**
   - Written: JSON (preset, pattern, song, kit); YM3 `YM3!` interleaved register by register (L7508-7517); SNDH with a 68k player and the `PRSM` marker (L8116-8220); VBL .s ASM (L8431-8455); MIDI SMF type 1, PPQ 96 (L8035-8078); 16-bit stereo PCM WAV (L7545-7560); store ZIP of stems (L8248-8274).
   - Read: JSON; YM3 / YM3b; uncompressed YM5! / YM6! (LHA refused); PRISM SNDH only; MIDI SMF 0/1 except SMPTE.
   - WAV header bug, L7550: `dv.setUint32(4, 36 + total * 2, true)` while the data size is `total*4` (L7554). The RIFF size is therefore wrong.

9. **Granularity of register writes**
   - Offline rendering in blocks of 2048 samples (L7530).
   - Register capture once per VBL frame of round(SR/vbl) samples, in a single `process()` (L8382-8385).
   - R13 is emitted only once per export (`st.firstShape`, L8365): the following envelope retriggers are lost.
   - UI monitor refreshed every 120 ms (L9487).
   - No trace of 128-sample blocks in the slice (that is the quantum of the worklet, outside the slice).
