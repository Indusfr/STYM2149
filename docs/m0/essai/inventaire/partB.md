# Part B: JS lines 3400-7000

| Domain | Control / function | Element (id or JS function) | Behaviour, range, default | Lines |
|---|---|---|---|---|
| Chipdrum | Factory kit banks | `KIT_BANKS` | 13 banks: Classic (=KIT_DEF), TR-606, TR-909-ish, Hard EBM, Industrial, Lo-Fi 4bit, Chiptune, TR-808, House, Techno, Hip-Hop, Trap, Drum'n'Bass. Each one sets the 8 instruments | 3393-3455 |
| Chipdrum | Parameters per instrument (reminder, defined before the slice) | `KIT`, `KITSPEC` | kick/tom/tom2: pitch, sweep, dec, noise, vol; snare: body, tone, dec, vol; clap: space, color, tail, vol; hat/ohat/chat: metal, tune, dec, vol. Ranges in KITSPEC (e.g. kick pitch 25-90, sweep 20-300, dec 4-20; snare body 80-400; chat dec 30-250; vol 0-100) | 3371-3392 |
| Chipdrum | Instrument → slot / note mapping | `KIT_SLOTS` | kick 0, snare 1, clap 2, hat 3, ohat 4, chat 5, tom 6, tom2 7. On a Chipdrum track: C, C#, D, D#, E, F, F#, G (modulo 8) | 3456, 3556-3561, 5146 |
| Chipdrum | Sending the kit to the engine | `sendKit()` | message `{type:'kit', kit}` (deep copy) on each change | 3458 |
| Chipdrum | Parameter tooltips | `KITHELP` | 12 help texts (pitch, sweep, dec, noise, body, tone, space, color, tail, metal, tune, vol) + range "(mn to mx)" | 3459-3472, 3483 |
| Chipdrum | Kit sliders | `buildKitSliders()` (`#kitSliders`) | One range per parameter of the current instrument; one undo step per drag (pointerdown arms `pushKitUndo`); updates the kit name and sends | 3475-3499 |
| Chipdrum | Direct entry of a value | span `.val.editable` → `input.valedit` | Click = text field; Enter confirms, Esc cancels, blur confirms; comma accepted; rounded + clamped [mn,mx] | 3500-3527 |
| Chipdrum | Detected kit name | `kitLabel()`, `refreshKitName()` (`#kitName`, `#genKitName`) | Compares KIT with each bank; otherwise "Custom". Shown as "Kit: name" | 3531-3548 |
| Chipdrum | Instrument selector | `#kitInst` (8 buttons) | Kick, Snare, Clap, Hat, Open Hat, Closed Hat, Tom 1, Tom 2; default `kick` | 3457, 3554-3565 |
| Chipdrum | Kit enabled by generator | `refreshKitEnable()` (`#kitBody`, `#kitOff`) | Kit inert (greyed) + message if the current effect ≠ 4 (Chipdrum) | 3566-3572 |
| Chipdrum | Open / close the Kit window | `openKit()`, `closeKit()`, `#kitClose` | Shows `#kitWin`, focus | 3573-3578 |
| Chipdrum | Audition | `#kitPlay` | message `{type:'drumprev', slot}` | 3579 |
| Chipdrum | Reset instrument | `#kitReset` | Resets the current instrument to the KIT_DEF values (undo) | 3580-3584 |
| Chipdrum | Bank menu | `#kitBank` | GEM menu of the banks sorted alphabetically; applies the 8 instruments (undo) | 3585-3593 |
| Import/Export | Load a kit (.json) | `#kitFile` change | Requires `{kind:'drumkit', kit:{…}}`; merges per instrument; otherwise alert "Not a valid drum kit file." | 3854-3869 |
| Windows/UI | Moving the windows | `mkWinDrag(winId,barId,closeId,detach)` | Drag the title bar; position clamped ≥0, compensated for the zoom `zScale()`; `detach` takes the window out of the flow on the 1st drag (main window) | 3596-3617, 3630-3635, 3707-3709, 3885 |
| Windows/UI | List of floating windows / Z stack | `FLOATWINS`, `focusWin()` | seqWin, arpWin, mixWin, songWin, ornWin, libWin, kitWin, helpWin, ymWin, curveWin, kbdWin (+mainWin). Click = to the front; z-index 70+k (under the modals 200) | 3618-3629 |
| Control/Monitor | Oscilloscope per track | `openScope(i)`, `closeScope(i)` | "SCOPE - name" window 360×140 per track, several side by side, positions offset by 24 px; opened by a click on a `.mscope` of the mixer | 3637-3681 |
| Control/Monitor | Drawing of the big scope | `drawBigScope(scope, spos)` | Green trace #0a0, width 2, y = H/2 − smp·H·0.9, circular buffer | 3690-3706 |
| Windows/UI | Window titles follow the track | `refreshWinTitles()`, `refreshScopeTitle()` | "- name" in arpTrk/ornTrk; seqTrk adds "[generator]" | 3682-3689 |
| Windows/UI | Help (shortcuts) | `HELP`, `buildHelp()`, `openHelp()`, `#helpClose` | 9 sections: Windows, Generator, File, Transport and edit, Writing notes, Sequencer rows, Sequencer view, Song window, Mouse | 3710-3776 |
| Keyboard | Window shortcuts (doc) | `HELP` | ^E Sequencer, ^B Arpeggiator, ^M Mixer, ^G Song, ^R Modulation, ^P Patches, ^K Drum kit; Esc closes the top window, then panic | 3711-3714 |
| Keyboard | Generator shortcuts (doc) | `HELP`, `FX_KEYS` | ^V Chip Voice, ^S SID Voice, ^U Sync Buzzer, ^C Chipdrum, ^I Digidrum | 3715-3717, 4436-4437 |
| Keyboard | File shortcuts (doc) | `HELP` | ^D demo, ^Q quit, ^? help | 3718 |
| Keyboard | Transport / editing (doc) | `HELP` | Space play/stop; ^Z/^Y undo/redo; 1-8 pattern slot; arrows cursor (Shift+←/→: channel); Enter places/removes; Del clears the step; Z/X octave −/+; M mutes the focused channel | 3719-3724 |
| Keyboard | Song window (doc) | `HELP` | ←/→ cursor, Home/End, Ins or A insert, Del delete, D duplicate, +/− repeats, X/C/V cut/copy/paste, Enter jump to the block | 3749-3757 |
| Windows/UI | Stateless open buttons | `syncWinBtns()` | openArpBtn, openOrnBtn, openSeqBtn, openKitBtn, openMixBtn, openLibBtn never "on" | 3777-3784 |
| Windows/UI | Open/close Arp and Sequencer | `openArpWin`, `closeArpWin`, `openSeqWin`, `closeSeqWin` | openSeqWin recomputes the vertical slider `fitTrSlider` | 3785-3794 |
| Patches/Presets | Patch library | `patchLib`, `saveLib()` | Stored in localStorage `prismYmPatchLib` (JSON) | 3797-3799 |
| Patches/Presets | Patch snapshot | `libSnap(i)` | `{patch, arp:{…,table}, level, gate}` | 3800-3805 |
| Patches/Presets | Apply a patch | `libApply(entry,i)` | Copies the patch, normalises the modulation tables (`ornNorm`), arp table padded to 16, level default 0.75, gate default 0.5 | 3806-3815 |
| Patches/Presets | Patch list (click) | `refreshLib()` (`#libList`, `#libHint`) | One button per patch; click = loads onto the current track | 3816-3826 |
| Patches/Presets | Patch context menu | right-click on a patch button | Load, Rename (prompt), Overwrite (with the current track), Delete | 3826-3834 |
| Patches/Presets | Save a patch | `#libSave` | Prompt "Patch name:", default "Patch N" | 3842-3848 |
| Import/Export | Export the library | `#libExport` | File `prism-ym2149-patches.json`: `{kind:'patchlib', patches:[…]}` | 3849-3852 |
| Import/Export | Import a library | `#libImport` → `#patchLibFile` | Requires `kind:'patchlib'` + array; appends; otherwise alert | 3853, 3870-3884 |
| Windows/UI | Open/close Patches | `openLib()`, `closeLib()`, `#openLibBtn`, `#libClose` | — | 3838-3841 |
| Tracks | Track tabs | `#chtabs`, `chTabs` | NCH buttons; click = focus; another click on the focused tab = enable/disable the track (enabled) | 3911-3936 |
| Tracks | Tab context menu | right-click on a tab | Copy settings, Paste settings, Rename... (disabled in 3ch: "The three YM voices are named by the chip") | 3920-3930 |
| Tracks | Copy / paste track settings | `chanCopy(i)`, `chanPaste(i)` | Copies patch + tables ornTable/ornNse/ornVol + arp + level + gate | 3889-3910 |
| Tracks | All on / All off | buttons created in `#chtabs` | `setAllEnabled(1/0)` on NCH tracks (4 in 3ch) | 3938-3953 |
| Tracks | Rename a track | `renameTrack(i)` | Prompt, 8 characters max, empty = number; not allowed in 3ch | 3954-3961 |
| Tracks | Shown name | `trackName()`, `tabTitle()`, `syncTabs()` | 3ch: A,B,C,Digi (or A1..Digi2 with two chips); tracks ≥4 unused in 3ch | 3962-3982 |
| Tracks | Focus on a track | `focusCh(i)` / `selectCh` | Sends `{type:'kbmode', focus, layer}` and `{type:'focus', i}`; refreshes the UI, mixer, modulation | 3983-3997 |
| Tracks | Labels in 3ch mode | `retabs()` | Tabs A/B/C/Digi; >NVOICES greyed; if cur≥4 → focus 0 | 3998-4007 |
| Windows/UI | Buttons release the focus | listener pointerup | A focused BUTTON gets `blur()` (avoids capturing Enter/Space/arrows) | 4011-4015 |
| Source | Tone / noise source | `#srcSel`, `SRC_NAMES`, `srcIdx()` | Tone / Noise / Tone+Noise → toneEn/noiseEn | 4017-4021, 4053-4059 |
| Tracks | Voicing poly/mono | `#polySel`, `#seqPolySel`, `pickVoicing()`, `refreshPolyBtn()` | Polyphonic / Monophonic (P().mono); mono cancels the chord; disabled in 3ch; poly up to POLYMAX voices; melodic mono = glide on legato; drum mono = choke | 4022-4052 |
| Source | Volume mode | `#vmSel`, `VM_NAMES` | Vol 4-bit / Envelope (P().volMode) | 4060-4065 |
| Audio engine | Chip mode | `#modeSel`, `MODE_NAMES`, `setMode3()` | 16 ch / Authentic 3ch / Dual chip 2x3ch. 3ch: track 4 of each chip forced to Digidrum (silent without a sample), hardware channels that were Digidrum → Chip Voice; saves/restores the previous tracks (`ch3Saved`); LEDs > nv greyed; sends allOff | 4066, 4080-4120 |
| Tracks | 3ch channel names | `VOICE_NAMES`, `VOICE_LONG`, `isDigiTrack`, `NVOICES` | 1 chip: A,B,C,Digi; 2 chips: A1,B1,C1,Digi1,A2,B2,C2,Digi2; NVOICES = 4×chips in 3ch, otherwise NCH | 4066, 4072-4079 |
| Windows/UI | GEM submenu arrow | `GEM_ARROW` | 8×8 pixel-art SVG | 4067-4071 |
| Envelope | Shared envelope label | `#envShared` | "(shared)" in 3ch, "(per channel)" otherwise | 4093 |
| Source | Noise period | `#np` | 1-31, default 8; in 3ch, applied to all the tracks (a single noise generator) | 4126-4130 (HTML 939) |
| Source | 4-bit volume | `#vol` | 0-15, default 13 (P().vol4) | 4131 (HTML 941) |
| Generators | SID: floor | `#vmin` | 0-15, default 0 (P().sidVmin) | 4132 (HTML 997) |
| Digidrum | Playback speed | `#ratio` | 25-800 → ×0.25-×8.00, default 1.00 (P().sidRatio = v/100) | 4133 (HTML 994) |
| Envelope | Envelope frequency | `#ef`, `efToHz`, `hzToEf` | Logarithmic slider 0-4000, default 1840; EP = round(2e6/(256·32·Hz)) clamped to 1-65535; display in Hz (1 decimal) and EP in decimal + hex `$XXXX` | 4134-4146 (HTML 953) |
| Envelope | Envelope shapes | `#shapeSel`, `SHAPE_ITEMS` | GEM menu of 16 entries 0-F with an SVG icon (P().shape) | 4423-4431 |
| Sequencer | Slide curves | `SLIDE_CURVES`, `SLIDE_SHORT`, `SLIDE_SEPS` | 13: Linear, Exponential, Logarithmic, S-curve, Stepped, Sawtooth, Wobble, Chromatic, Scale-locked, Overshoot, Bounce, Random walk, Custom (LIN…CUS); separators before 4,7,9,12 | 4147-4153 |
| Sequencer | Custom curve editor | `#curvegrid`, `buildCurveGrid()`, `setCurve()` | 32 points 0-1 (default linear i/31); painted with the mouse (left click + hover) | 4154-4184 |
| Sequencer | Curve warning | `refreshCurveWarn()` (`#curveWarn`) | If the last point ≠1 (±0.02): "Ends at N% of the interval…" | 4185-4194 |
| Sequencer | Curve Reset / Smooth / Invert | `#curveReset`, `#curveSmooth`, `#curveInvert` | Linear reset; smoothing (t[i-1]+2t[i]+t[i+1])/4; inversion 1−t[31−i] | 4200-4214 |
| Windows/UI | Curve window | `openCurveWin()`, `#curveClose` | Also opened by choosing Custom in CRV | 4215-4219, 6175 |
| Vibrato/ADSR | Vibrato depth | `#vibDepth` | 0-200 cents, default 0 | 4220-4224 (HTML 963) |
| Vibrato/ADSR | Vibrato rate | `#vibRate` | 1-20 Hz step 0.5, default 6 | 4225-4229 (HTML 965) |
| Timing | VBL rate | `VBLHZ()`, `msPerTick()` | P().vbl ‖ GLOB.vbl ‖ 50 Hz; ms/tick = 1000/VBL | 4230-4231 |
| Vibrato/ADSR | ADSR mode | `#adsrMode`, `ADSR_MODES` | Smooth / Chip (P().adsrChip); Chip forced in 3ch | 4384-4392 |
| Vibrato/ADSR | A/D/S/R sliders | `#atk #dcy #sus #rel`, `adsrRange/Unit/Shown/Store` | Smooth: A,D 0-1000 ms, S 0-100 %, R 0-2000 ms (defaults 0/0/100/0). Chip: A,D,R 0-64 VBL ticks, S 0-15; conversion ticks = round(ms/msPerTick), S = round(%/100·15) | 4232-4251, 4365-4410 |
| Vibrato/ADSR | Direct ADSR entry | `makeEditable(k+'v')` | Value clamped in the unit of the mode | 4400-4409 |
| Vibrato/ADSR | ADSR curve | `drawAdsrCurve()` (`#adsrCurve`) | Real time axis A+D+400 ms (conventional hold)+R; Chip mode in steps of 16 levels; green dot of the live position (adsrLive st 1-4); greyed for Sync Buzzer in chip mode | 4252-4344 |
| Vibrato/ADSR | ADSR inactive on Sync Buzzer | `refreshAdsrEnable()` | Effect 3 + chip mode: sliders disabled, hint "buzzer: envelope only" | 4345-4364 |
| Mixer | Track level | `#chLevel` | 0-100, default 75 (CH.level = v/100), syncs the mixer fader | 4411-4416 (HTML 946) |
| Mixer | Master | `#master` | 0-100, default 70 (GLOB.master = v/100) | 4417-4421 (HTML 1045) |
| Generators | Generator choice | `#fxSel`, `FXNAMES`, `FX_ORDER`, `setEffect()`, `fxAllowed()` | Chip Voice(0), SID Voice(1), Sync Buzzer(3), --- , Chipdrum(4), Digidrum(2); in 3ch Digidrum only on the Digi track and the other way round; a change = manual panic; drums cut the arp; Sync Buzzer: EP>100 → EP=1 | 4433-4477 |
| Keyboard | Manual play panic | `manPanic()` | Clears the held keys, pads; `{type:'manOff'}` | 4438-4445 |
| Digidrum | 8 sample slots | `#drumSlots`, `userDrums` | Active slot = P().slot (0-7) | 4480-4488 |
| Digidrum | Load a sample | `#drumwave` click → `#drumFile` | Requires the audio to be started; decodeAudioData, channel 0, linear resampling to 7800 Hz, 4-bit quantisation, max 3 s (23400 samples); `{type:'drum', slot, data}` | 4489-4517 |
| Digidrum | Waveform display | `drawDrum()` | 4-bit steps; text "slot N: n smp @ 7800 Hz / 4-bit" or "built-in DrumNoise" | 4518-4539 |
| Digidrum | Digidrum ST channel (3ch) | `#digiChSel` | A/B/C (A1.. or A2.. with two chips) → GLOB.digiCh / digiCh2 | 4651-4665 |
| Arpeggiator | On/Off | `#arpOn` | No effect on drums; off → allOff; on → sendHeld | 4542-4549 |
| Arpeggiator | Mode | `#arpModeSel`, `ARPMODES` | Up, Down, UpDn, Order, Random, Table | 4550-4555 |
| Arpeggiator | Fill with a chord | `#chordSel` | 39 chords → table + tlen (undo `armCur`) | 4640-4649 |
| Arpeggiator | Octaves | `#arpOcts` | 1, 2, 3 (default 1) | 4666-4668 (HTML 739-744) |
| Arpeggiator | Glide rate (tracking) | `#arpGlideRate` | toggles A().track | 4669-4673 |
| Arpeggiator | Land | `#arpLand` | toggles A().land | 4674-4678 |
| Arpeggiator | Rate | `#arpRate` | 4-200 Hz, default 50 | 4679-4683 (HTML 723) |
| Arpeggiator | Table length | `#arpLen` | 1-16, default 3 | 4684-4688 (HTML 749) |
| Arpeggiator | Table grid | `#arpgrid`, `refreshArpGrid()`, `markArpStep()` | 16 columns × 16 rows (+0…+15 semitones); a click sets the value; columns ≥ tlen greyed; played step highlighted | 4690-4728 |
| Timing | Clock source | `#clkSel`, `CLKMODES`, `setClkMode()` | Internal / MIDI ext; MIDI: BPM disabled, "waiting for clock...", arp division row shown | 4730-4745 |
| MIDI | Arp division on MIDI clock | `#arpDivs` | 1/4(24), 1/8(12), 1/8T(8), 1/16(6, default), 1/16T(4), 1/32(3), Tick(1) MIDI ticks | 4746-4752 (HTML 729-736) |
| Scales | Factory chords | `CHORDS` | 39: 6 triads, 8 sevenths, 5 sixths/add, 7 extensions, 6 inversions/voicings, 7 intervals | 4558-4604 |
| Sequencer | Chord on input | `#seqChord`, `refreshChordBtn()`, `chordVoices()` | Off + 39 chords; disabled for drums / 3ch / mono; voices = min(POLYMAX, number of intervals) | 4605-4639 |
| Sequencer | Note figures | `FIGS`, `figIcon()`, `figSteps()` | 13: Step, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4., 1/8., 1/16., 1/4T, 1/8T, 1/16T; duration in steps = round(beats × steps/beat), min 1 | 4755-4797 |
| Sequencer | Resolution | `RESOS` | 7: 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T (1,2,4,8,1.5,3,6 steps per quarter note) | 4798-4802 |
| Sequencer | Time signature | `SIGS`, `beatSteps()`, `barSteps()` | 12: 2/4,3/4,4/4,5/4,6/4,7/4,5/8,6/8,7/8,9/8,11/8,12/8 | 4803-4813 |
| Options/Menus | Note language | `NOTE_LANGS`, `setNoteLang()` | English (C D E) / Latin (Do Re Mi); localStorage `stymNoteLang` | 4814-4826, 4867-4871 |
| Options/Menus | Tooltips on/off | `setTips()`, `applyTips()`, MutationObserver | localStorage `stymTips`; off = title moved to data-tip, including elements created later | 4827-4866 |
| Scales | Factory scales | `SCALES` | Off + 25 scales (major/minor 4, modes 5, dark 5, reduced 6, symmetric 5) | 4872-4905 |
| Scales | Root / Snap | `scaleRoot`, `scaleName`, `scaleSnap`, `inScale()`, `snapToScale()` | Greys the rows outside the scale; Snap moves to the nearest degree (±6, downwards first) | 4906-4920 |
| Sequencer | Row labels | `updateSeqLabels()` | Melodic: 13 note+octave rows; Chipdrum: Kick…Tom2 (8); Digidrum: Sl 1-8 | 4921-4943 |
| Sequencer | Grid | `buildSeqGrid()` (`#seqgrid`) | 64 visible columns (NCOLS), 13 note rows + control rows; step numbers at the top (bar/beat) | 4963-5060 |
| Sequencer | Horizontal scrolling | `#seqScroll` | If the pattern > 64 steps; shows "a-b" | 5061-5066, 4975-4977 |
| Sequencer | Vertical scrolling (pitch) | `#seqTr`, mouse wheel | 23-108 (B0-C9), default 48, 1 semitone per notch; slider fitted to the height of the grid (`fitTrSlider`) | 5070-5090, 6040-6051 |
| Step rows | SRC | `flag mix` | Channel / Tone / Noise / Tone+Noise (shows - T N TN) | 5287, 6129-6134 |
| Step rows | GEN | `flag fx` | Channel / Chip Voice / SID Voice / Sync Buzzer (- CV SID SB; CD can be shown) | 5288, 6135-6140 |
| Step rows | ARP | `flag afl` | Boolean, visible if arp on and melodic track | 5165, 6193-6199 |
| Step rows | HLD | `flag tie` | Chipdrum only: no retrigger while it sounds | 4949, 5166 |
| Step rows | SLD | `flag slide` | Slide to the next note | 4950 |
| Step rows | CRV | `flag slc` | Slide curve (13); greyed if no slide is active; Custom opens the editor | 5290-5297, 6170-6178 |
| Step rows | SLT | `flag slt` | Channel / Slot 1-8 (row hidden: slot given by the grid row) | 5167, 6141-6147 |
| Step rows | CUT | `flag cut` | Hidden (replaced by the note length) | 5168 |
| Step rows | OCT | `flag oct` | Drums: −3…+3; melodic: Channel / 1-7 (visible only for drums) | 5169, 6148-6161 |
| Step rows | TON | `flag ton` | −12…+12 semitones (drums) | 5170, 6162-6169 |
| Step rows | GAT | `flag gat`, `gatPopup` | 0-100 % (0 = Channel), popup with slider + field + Channel/Set | 6011-6039 |
| Step rows | PAN | `flag pnr`, `panPopup` | −10…+10 stored as v·10+101 (0 = centre/channel), shown as C / L n / R n; hidden in 3ch | 5306-5308, 5786-5816 |
| Step rows | RTG | `flag rtg` | 1,2,3,4 hits in the step | 5298, 6179-6184 |
| Step rows | CND | `flag cnd` | Always, 1:2, 2:2, 1:4, 4:4, 25 %, 50 %, 75 % | 5299, 6185-6190 |
| Step rows | VOL | `flag vol` | 0-15 (default 15); left click draws while dragging; right-click opens `openVolMenu` | 5274-5286, 6056-6058, 6119-6128 |
| Step rows | Copy/paste a row across tracks | right-click on a row label | Copy row / Paste row (if same type) | 5028-5039 |
| Sequencer | Ghost notes | `ghostOn`, `buildGhostIndex()` | Notes of the other melodic tracks in the background | 5104-5128 |
| Sequencer | Overview | `overOn`, `#overMap`, `refreshOverview()` | Fixed scale of 256 steps, pitches B0-C9 (23-120), drum strip 26 px, view frame, playhead; click/drag moves the view (step + pitch) | 5311-5416 |
| Sequencer | Lowest playable limit | `PLAY_LO` | 23 (B0): cells below marked `nope`, input refused | 5321, 5217, 6292 |
| Sequencer | Write a note (click) | pointerdown on the grid | Empty cell: note + chord + duration of the figure; same note, other duration: resizes; otherwise deletes; preview | 6309-6329 |
| Sequencer | Drag drawing | `paint.note` | Dragging right lengthens (`stretchNote`); elsewhere places notes | 6559-6599 |
| Sequencer | Move a note | `moving`, `moveNoteTo()` | Pitch and time, length kept; auto-scroll at the edges (h and v); single click = resize/delete | 6261-6288, 6480-6529, 6613-6626 |
| Sequencer | Resize by the edges | `sizing` | Right (>60 %) or left (<40 %) edge of the note; `setNoteLen`, `setNoteStart` | 6274-6283, 6434-6479, 5640-5672 |
| Sequencer | Polyphony in the grid | `addVoiceAt`, `delVoiceAt`, `voiceRow`, `writeChord` | Voices 2..POLYMAX (6) with their own pat/oct/dur; a click on a used step adds/removes a voice; a voice is extended by dragging | 5473-5598, 6298-6308 |
| Sequencer | Voice promotion (mono) | `promoteVoice()` | In mono, clicking a greyed voice swaps it with the root note | 5505-5532, 6233-6238 |
| Sequencer | Split (Alt+click) | `splitNote()` | Alt+click on a note cuts it at the clicked step | 5673-5684, 6268-6270 |
| Sequencer | Tools | `seqTool` | select (frame), split (scissors), join (weld), erase (dragged eraser), move, draw | 6203-6231 |
| Sequencer | Selection | Shift+click | On a note: multiple selection; on an empty cell: rectangle; on a control row: block of steps | 6083-6109 |
| Sequencer | Right-click | pointerdown button 2 | On a note: note menu; empty cell: rectangular frame (drag); control row: dragged reset (VOL=15) | 6060-6082 |
| Sequencer | Note menu (right-click) | contextmenu | Copy, Cut, Paste here, Longer ], Shorter [, Split here \, Join with next J, Length: n, Delete | 6339-6365 |
| Sequencer | Rectangular selection menu | contextmenu | Copy (n), Cut, Paste, Clear, ±1 semitone, ±1 octave, Deselect | 6372-6405 |
| Sequencer | Block / channel menu (empty cell) | contextmenu | Copy/Cut/Paste/Clear block (scope: row or all), Deselect, Note length submenu, Channel submenu, Rename channel... | 6406-6432 |
| Editing/Undo | Cut/Copy/Paste (Edit menu) | `editCutState()`, `editClip()` | On a rectangular selection or the note under the cursor | 5439-5472 |
| Sequencer | Euclidean generator | `#euclidBtn`, `bjorklund()` | Steps 2-min(32,len) (default 16), Pulses 1-Steps (default 4), Rotate 0-Steps−1, Accents 0-Pulses (accent VOL 15, otherwise 10); preview; name E(K,N) + 23 named rhythms; target Note (±12 around the cursor) / Instrument / Slot; Merge; Apply repeats the motif over the track | 5817-6010 |
| Patterns/Song | Pattern banks | `PATS` | 32 patterns = 4 banks A-D × 8; 256 steps max (NSTEP) | 6632-6639 |
| Patterns/Song | Pattern schema | `snapPattern()`, `LANEKEYS` | `{steps, chans:[{pat,tie,slide,afl,vol,mix,slt,cut,fx,oct,gat,pnr,rtg,cnd,slc,ton,pat2..6,oct2..6,dur2..6}]}`; defaults pat=-1, vol=15, others 0 | 6633-6648 |
| Import/Export | Compatibility with the old 64-step format | `padLane()`, `loadPattern()` | Pads to 256 steps | 6649-6664 |
| Patterns/Song | Pattern queue (playback) | `selectPattern()`, `patQueueMsg()` | During playback: switches at the end of the loop (303 style), `{type:'patQueue', idx, len, chans:[{i, pat(note abs), tie, slide, afl, vol, mix, slotRow, cut, fx, gat, rtg, cnd}]}` | 6665-6675, 6769-6783 |
| Patterns/Song | Bank/slot selector | `#patBanks`, `#patSlots` | A-D (Shift+1..4), slots 1-8 (keys 1-8); remembers the last pattern per bank | 6784-6815 |
| Patterns/Song | Slot menu | right-click `#patSlots` | Copy, Cut, Paste, +1/−1 semitone, +1/−1 octave (`transposePattern`, drums excluded) | 6800-6811, 6700-6719 |
| Patterns/Song | Banks/slots in the Song window | `buildSongPat()` (`#songBanks`, `#songSlots`) | Slot menu: Copy, Cut, Paste, Add to song | 6720-6748 |
| Editing/Undo | Undo/redo stack | `undoStack`, `redoStack`, `doUndo()`, `doRedo()` | 50 levels; types pat, song, chan, kit, mix; duplicates ignored | 6817-6947 |
| Options/Menus | Edit menu | `#mEdit` | Step Sequencer ^E, Arpeggiator ^B, Mixer ^M, Song ^G, Modulation ^R, Patches ^P, Drum kit ^K, Cut ^X, Copy ^C, Paste ^V, Undo ^Z, Redo ^Y (Arp/Modulation disabled for drums) | 6948-6983 |
| Patterns/Song | Song: model | `SONG` | List `{pat:0-31, rep:n}`; state songOn/songPos/songRep; multiple selection; drag and drop of blocks (threshold 5 px) | 6985-7000 |
| Sequencer | Contextual hints | `#seqHint`, `RTG_HINT` | Help text by mode (melodic/Chipdrum/Digidrum), PAN removed in 3ch | 5067-5068, 5142-5175 |

## DSP

1. **Envelope frequency / EP** (the only envelope computation of the slice, on the UI side):
   - L.4134 `const EF_LO = 2000000 / (256 * 32 * 65535);          // EP maximal : 0.0037 Hz`
   - L.4135 `const EF_SPAN = 65535;`  L.4136 `const EF_STEPS = 4000;`
   - L.4137 `const efToHz = v => EF_LO * Math.pow(EF_SPAN, v / EF_STEPS);`
   - L.4138 `const hzToEf = f => Math.round(Math.log(Math.max(EF_LO, f) / EF_LO) / Math.log(EF_SPAN) * EF_STEPS);`
   - L.4140 `P().EP = Math.max(1, Math.min(65535, Math.round(2000000 / (256 * 32 * efToHz(v)))));`
   - L.4144 `$('efv').textContent = (2000000/(256*32*P().EP)).toFixed(1);`
   - So the displayed Hz = 2 MHz / (8192·EP). Note: the usual hardware formula of the YM cycle is clock/(256·EP); this extra ×32 factor must be checked against the engine (worklet) outside the slice.
   - L.4453 Sync Buzzer: `if (P().effect === 3 && P().EP > 100) P().EP = 1;`
2. **Volume / DAC / dB**: nothing in the slice (only UI levels: `CH.level = v/100` L.4413, `GLOB.master = v/100` L.4418; chip ADSR: `Math.round(v / 100 * 15)` L.4246).
3. **Mixing (sum, tanh)**: none in the slice.
4. **Noise LFSR**: none. Noise period `#np` 1-31 shared in 3ch (L.4126-4130).
5. **Timer / SID / Sync Buzzer / Digidrum**:
   - Digidrum, resampling and quantisation (L.4500-4506):
     `const ratio = 7800 / buf.sampleRate;` ... `const v = src[i0] * (1 - fr) + (src[i0 + 1] || 0) * fr;` `data[i] = Math.max(0, Math.min(15, Math.round(v * 7.5 + 7.5)));`; `DRUM_LEN_MAX = 3 * 7800` (L.4480).
   - Digidrum / SID speed: `P().sidRatio = v/100` (L.4133), SID floor `P().sidVmin` (L.4132).
   - ADSR chip mode: 1 register write per VBL tick; ticks = `Math.round(v / msPerTick())` (L.4246).
6. **JSON schemas**:
   - Patch library: `{ kind:'patchlib', patches:[{ name, patch, arp:{…, table[16]}, level, gate }] }` (L.3802-3804, 3850).
   - Kit: `{ kind:'drumkit', kit:{ kick:{pitch,sweep,dec,noise,vol}, snare:{body,tone,dec,vol}, clap:{space,color,tail,vol}, hat/ohat/chat:{metal,tune,dec,vol}, tom/tom2:{pitch,sweep,dec,noise,vol} } }` (L.3861).
   - Pattern: `{ steps, chans:[ {pat,tie,slide,afl,vol,mix,slt,cut,fx,oct,gat,pnr,rtg,cnd,slc,ton,pat2-6,oct2-6,dur2-6} ×NCH ] }`, arrays of 256 (L.6633-6648).
   - Song: `[{ pat:0-31, rep:n }]` (L.6986).
   - Undo: `{kind:'pat'|'song'|'chan'|'kit'|'mix', …}`; mix = `{level, pan, mute, solo}` per track (L.6819-6845).
7. **VBL**: L.4230 `const VBLHZ = ()=>(P().vbl || GLOB.vbl || 50);` L.4231 `const msPerTick = ()=>1000 / VBLHZ();` (default 50 Hz; no VBL selector in the slice).
8. **File formats**: in the slice, only the JSON patch library (writing `prism-ym2149-patches.json`, reading), the JSON drum kit (read only here) and audio reading (`audio/*` through decodeAudioData) for the Digidrum. No YM, SNDH, VBL .s, MIDI file, WAV or stems in 3400-7000.
