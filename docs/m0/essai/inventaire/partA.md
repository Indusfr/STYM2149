# Part A — HTML inventory (553-1091) + JS (1092-3400)

File: /mnt/project-files/YM2149_Prototype.html. The handlers of most HTML controls are beyond line 3400 (outside the slice); for those, the behaviour given comes from the HTML attributes (min/max/value/title) and from the engine (worklet) of the slice.

| Domain | Control / function | Element (id or JS function) | Behaviour, range, default | Lines |
|---|---|---|---|---|
| Windows/UI | Global zoom container | `#zoomRoot` | Root of the whole UI (zoom applied elsewhere) | 554 |
| Windows/UI | Message box | `#msgWin`, `#msgTxt`, `#msgOk` | Modal alert, OK = Enter | 557-562 |
| Windows/UI | About box | `#infoWin`, `#infoOk` | "STYM2149 from PRISM", Pascal "Indus" Nowak, Prototype 1, © 2026 PRISM, alpha licence text | 563-577 |
| MIDI | MIDI learn badge | `#learnBadge` | Fixed badge at the bottom left, visible in learn mode | 578-580 |
| Windows/UI | Input box | `#promptWin`, `#promptMsg`, `#promptInput`, `#promptCancel`, `#promptOk` | Modal text prompt (30 chars) | 884-891 |
| Windows/UI | Confirmation box | `#confirmWin`, `#confirmMsg`, `#confirmCancel` (default, Enter), `#confirmOk` | Modal confirmation, Cancel by default | 892-900 |
| Windows/UI | Boot screen | `#boot`, `#bootbtn` "Run program" | Starts the AudioContext; text "16 channels / Digidrums / SID Voice / Sync Buzzer"; on error shows "Startup error" + message | 905-910, 3040-3048 |
| Windows/UI | GEM main window | `#mainWin`, `#mainBar` (draggable), `#mainClose`, `.fullbox` | Atari GEM style window, title STYM2149 | 912-917 |
| Options/Menus | Menu bar | `#mFuji` (Atari logo), `#mFile` File, `#mEdit` Edit, `#mOptions` Options | Drop-down menus (content built outside the slice) | 918-920 |
| Control/Monitor | CHIP indicator | `#chipTxt` "CHIP: -" | Clickable, "Chip conformity" | 919 |
| MIDI | MIDI indicator | `#miditxt` "MIDI: -" | MIDI status in the menu bar | 919 |
| Tracks | Channel tabs | `#chtabs` | Tabs of the 16 channels (built outside the slice) | 923 |
| Source | Source | `#srcSel` (+`#labSrc`) | Tone / Noise / Tone+Noise; at least one active; default Tone (toneEn=1, noiseEn=0) | 929-933, 2587 |
| Source | Voicing (polyphony) | `#polySel` (+`#labPoly`) | Polyphonic / Mono (patch.mono, default 0); Mono = legato with a 45 ms slide | 934-936, 1453-1479 |
| Source | Noise period | `#np` / `#npv` | 1-31, default 8 (R6) | 938-939 |
| Source | 4-bit volume | `#vol` / `#volv` | 0-15, default 13 | 940-941 |
| Source | Volume mode | `#vmSel` | "Vol 4-bit" / hardware envelope (volMode 0/1) | 942-944 |
| Source | Channel level | `#chLevel` / `#chLevelv` | 0-100, default 75 (level 0.75) | 945-946, 2586 |
| Envelope | Env freq | `#ef` / `#efv` | Slider 0-4000 (mapped by efToHz outside the slice), default 1840; Hz display = 2e6/(256·32·EP) | 952-953 |
| Envelope | EP readout | `#epv` | Shows EP (dec + hex); default EP=400; editable (outside the slice) | 954, 2587 |
| Envelope | R13 shape | `#shapeSel`, `#shapeLab` | 16 shapes (0-15), default 8; icons `SHAPE_SVG`, labels `SHAPES` | 956-957, 2822-2842 |
| Envelope | Shared indicator | `#envShared` | "(per channel)" / shared in 3ch | 951 |
| Vibrato/ADSR | Vibrato Depth | `#vibDepth` | 0-200 cents, default 0 | 962-963 |
| Vibrato/ADSR | Vibrato Rate | `#vibRate` | 1-20 Hz step 0.5, default 6; computed at the VBL rate | 964-965, 2363-2366 |
| Vibrato/ADSR | Attack | `#atk` | 0-1000 ms, default 0 | 970-971 |
| Vibrato/ADSR | Decay | `#dcy` | 0-1000 ms, default 0 | 972-973 |
| Vibrato/ADSR | Sustain | `#sus` | 0-100 %, default 100 | 974-975 |
| Vibrato/ADSR | Release | `#rel` | 0-2000 ms, default 0 | 976-977 |
| Vibrato/ADSR | ADSR mode | `#adsrMode`, `#adsrHint` | Smooth (linear audio gain per sample) / Chip (4-bit volume quantised at the VBL rate); 3ch forces Chip | 978-980, 1409, 2359-2362 |
| Vibrato/ADSR | Transparent ADSR | `setupVoice` | 0/0/100/0 = ADSR disabled (adsrOn=0) | 1402-1410 |
| Vibrato/ADSR | ADSR curve | `#adsrCurve` (228×52), `#adsrCurveLab` | Drawing of the ADSR + live tracking (stage/level of the focused channel, 20 Hz telemetry) | 1026-1030, 2549-2559, 2983-2990 |
| Generators | Generator type | `#fxSel` | Chip Voice (0), SID Voice (1), Digidrum (2), Sync Buzzer (3), Chipdrum (4) | 987-989, 2821 |
| Generators | Timer ratio (SID) | `#ratio` / `#ratiov` | 25-800 → x0.25-x8.00, default 100 (x1.00); SID timerFreq = f·ratio. Wrong tooltip ("Digidrum playback rate") | 992-994, 1361 |
| Generators | SID min vol | `#vmin` / `#vminv` | 0-15, default 0; low level of the SID wave | 995-997, 2431 |
| Generators | SID Voice | worklet `effect===1` | Timer at f·sidRatio toggles sidState; vol = sidState ? vol4 : sidVmin; ignores volMode | 2408-2411, 2431-2434 |
| Generators | Sync Buzzer | `setupVoice`/`liveFx` `effect===3` | volMode=1, tone/noise off, EP>100 → EP=1; a timer at f restarts the envelope at each period | 1390-1393, 1505-1508, 2416 |
| Chipdrum | Chipdrum panel | `#ndrumPanel`, `#openKitBtn` (Ctrl+K), `#genKitName` | Opens the Drum Kit window, kit name | 999-1002 |
| Chipdrum | Drum Kit window | `#kitWin`, `#kitBar`, `#kitClose`, `#kitOff` | Message if the track is not Chipdrum (Ctrl+C to switch) | 863-870 |
| Chipdrum | Instrument selector | `#kitInst` | 8 instruments: kick, snare, clap, hat, ohat, chat, tom, tom2 (slots 0-7) | 872, 3455 |
| Chipdrum | Kit sliders | `#kitSliders`, `KITSPEC` | kick: Pitch 25-90, Sweep 20-300, Decay 4-20, Noise 0-100, Vol 0-100; snare: Body 80-400, Tone 0-70, Decay 6-30, Vol; clap: Space 6-20, Color 15-80, Tail 10-60, Vol; hat: Metal 0-100, Tune 60-160, Decay 15-120, Vol; ohat: same, Decay 3-30; chat: same, Decay 30-250; tom: Pitch 60-200, Sweep 20-150, Decay 4-20, Noise, Vol; tom2: Pitch 80-260, same | 873, 3382-3392 |
| Chipdrum | Kit default values | `KIT` / worklet `buildDrums` | kick{45,115,9,0,100} snare{180,35,16,100} clap{10,35,26,100} hat{78,108,45,100} ohat{72,112,9,100} chat{90,134,95,100} tom{110,75,9,0,100} tom2{150,85,11,0,100} | 1230-1239, 3371-3380 |
| Chipdrum | Audition | `#kitPlay` | Preview (msg `drumprev`, builtin sample at 7800 Hz, gain 0.55) | 876, 2105-2107, 2475-2484 |
| Chipdrum | Reset instrument | `#kitReset` | Restores the factory values (KIT_DEF) of the instrument | 877, 3381 |
| Chipdrum | Kit banks | `#kitBank` "Kits", `KIT_BANKS` | 13 kits: Classic, TR-606, TR-909-ish, Hard EBM, Industrial, Lo-Fi 4bit, Chiptune, TR-808, House, Techno, Hip-Hop, Trap, Drum'n'Bass | 878, 3393-3454 |
| Chipdrum | Kit name | `#kitName` | Display | 880 |
| Chipdrum | Kit import | `#kitFile` (.json) | Loads a kit from a file | 708 |
| Chipdrum | Live rebuild | msg `kit` → `buildDrums`+`buildChipDrums` | Recomputes the samples during playback | 2097-2100, 879 |
| Chipdrum | 16ch synthesis (samples) | `buildDrums` | 8 samples (4-bit) at R=7800 Hz: kick 0.30 s (swept sine f=pitch+sweep·e^(-14t), drive+grit), snare 0.18 s (noise·0.75 + sine body·tone), clap 0.16 s (3 bursts space ms + gap 4 ms, noise filtered by color, tail set by tail), hats (3 inharmonic squares 1583/2145/2808 Hz·tune + derived noise; 0.09/0.30/0.045 s), toms 0.22/0.20 s; RNG LCG 1103515245 | 1229-1304 |
| Chipdrum | 3ch synthesis (registers) | `buildChipDrums` | Tables per VBL frame (2-48 frames, nf=round(F·3/dec)): TP=125000/f, NP, mixer, vol 0-15; snare NP=12; clap alternating bursts; hats NP=31-tune·26/100 | 1195-1227 |
| Chipdrum | Chipdrum pitch | `applyPitch`, `laneStep` | timerFreq = 7800·drumTr; drumTr = 2^((OCT·12+TON)/12) per step; in 3ch frame TP / drumTr | 1364, 1935-1936, 2399-2401 |
| Chipdrum | Chipdrum TIE | `laneStep` | TIE on a Chipdrum step: no retrigger while it sounds | 1929 |
| Digidrum | Slots | `#drumSlots` buttons `data-ds` 0-7 | 8 user slots; default 1 | 1004-1014 |
| Digidrum | Load sample | `#drumFile` (audio/*) | Loads a sample into the selected slot (msg `drum`, Uint8 0-15) | 1015, 2082-2084 |
| Digidrum | ST channel (3ch) | `#digiChSel`, `#digiChRow` | Channel A/B/C stolen by the drum in 3ch, default C (digiCh=2; digiCh2 for the 2nd chip) | 1017-1019, 2615 |
| Digidrum | Waveform | `#drumwave` (360×48) | Display of the sample | 1020 |
| Digidrum | Playback | worklet `effect===2` | timerFreq = 7800·2^((note-60)/12); slot = (note%12)%8 in the sequencer, src.slot in manual play; missing sample = silence; gain ×5 (makes up for the log DAC); 3ch: the accent multiplies the sample (digiLvl) | 1363, 1394-1396, 2404-2427, 2438 |
| Digidrum | Inconsistent hint | text | Says "Slot = note mod 4", the code does (note%12)%8 | 1021 |
| Digidrum | 3ch replay priority | `laneStep` | If a digidrum holds the channel, the music track gives up its step | 1923-1925 |
| Arpeggiator | Window | `#arpWin`, `#arpBar`, `#arpClose`, `#arpTrk`; opened by `#openArpBtn` (Ctrl+B) | Floating, draggable | 709-713, 1056 |
| Arpeggiator | On/Off | `#arpOn` | Enables the channel's arp (default off); forced off for drum tracks | 716, 2894 |
| Arpeggiator | Mode | `#arpModeSel` | 0 Up, 1 Down, 2 UpDn, 3 Order (order played), 4 Random, 5 Table; default Up (engine channel: 5) | 721, 1590-1612, 1654-1665 |
| Arpeggiator | Rate | `#arpRate` / `#arpRatev` | 4-200 Hz, default 50 (= VBL) | 722-723 |
| Arpeggiator | Track (follows the slide) | `#arpGlideRate` | During a slide, rate·(1+(min(2^(gl/12), 200/rate)-1)·t) | 724-725, 2281-2289 |
| Arpeggiator | Land | `#arpLand` | On the last tick of a slide, back to the root note | 726-727, 1638-1645 |
| Arpeggiator | Division (MIDI clock) | `#arpDivs` `data-ad` | 1/4=24, 1/8=12, 1/8T=8, 1/16=6 (default), 1/16T=4, 1/32=3, Tick=1 (MIDI ticks); visible with MIDI Clock | 728-737, 2177-2185 |
| Arpeggiator | Octaves | `#arpOcts` `data-ao` | 1 (default), 2, 3; ignored in Table mode | 738-746, 1633 |
| Arpeggiator | Steps (table) | `#arpLen` / `#arpLenv` | 1-16, default 3 | 747-749 |
| Arpeggiator | Chord… | `#chordSel` | Fills the table with a chord (CHORDS list outside the slice) | 751 |
| Arpeggiator | Table grid | `#arpgrid` | Offsets in semitones (hex), default [0,4,7]; applied to the last note | 753-754, 2594 |
| Arpeggiator | Live arp | `arpTick` | Held notes; goes on during the release (retune only); keeps the ADSR state between steps (grabEnv/putEnv); velocity per note | 1580-1624 |
| Arpeggiator | Track arp | `laneArpTick` | Sequencer ARP flag, literal Table; suspended (ARP=0) during a slide; the base slides under the arpeggio | 1627-1676, 1744-1760 |
| Modulation | Window | `#ornWin`, `#ornClose`, `#ornTrk`; `#openOrnBtn` (Ctrl+R) | "Ornament" table per VBL frame | 670-689, 1057, 3344-3349 |
| Modulation | On/Off | `#ornOn` | Disabled for Digidrum/Chipdrum | 677, 3350-3353, 3302-3317 |
| Modulation | Len | `#ornLen` / `#ornLenv` | 1-64, default 8 | 678, 3365-3368 |
| Modulation | Loop/Hold | `#ornLoop` | Loop (default) / Hold on the last step | 679, 3354, 2355-2356 |
| Modulation | Rate | `#ornRate` | Menu 1/frame…4/frame (VBL ticks per step), default 1 | 680, 3355-3359 |
| Modulation | Clear | `#ornClear` | Pitch=0, NSE=0, VOL=15 on 64 steps | 681, 3360-3364 |
| Modulation | Pitch grid | `#orngrid` `buildOrnGrid` | 25 rows (+12…-12 semitones) × 64 columns, click/drag to draw | 3192-3219 |
| Modulation | NSE row | cells `.oc.nse` | Enables the noise on this tick (toggle painted by dragging) | 3223-3239, 2348, 2429 |
| Modulation | VOL row | cells `.oc.vcell` | Level 0-15 by vertical position (drag); right-click = precise slider menu (`openVolMenu`) | 3240-3273 |
| Modulation | Engine | `process` | On each VBL frame: semis = ornTable[pos]; vol4 = round(vol4Base·ornVol/15); tpEff = TP·2^(-semis/12); SID/Buzzer follow | 2337-2377 |
| Sequencer | Window | `#seqWin`, `#seqClose`, `#seqTrk`; `#openSeqBtn` (Ctrl+E) | Step sequencer per channel | 759-763, 1058 |
| Sequencer | Tools | `#seqTools` | Toolbar (built outside the slice) | 766 |
| Sequencer | Play / Rec | `#seqPlay`, `#seqRec` | Hidden by default; Play/Stop, live recording | 768-769 |
| Sequencer | Step input | `#seqStepRec` | Step-by-step input at the cursor | 770 |
| Sequencer | Clear | `#seqClear` | Clears the current track | 771 |
| Generators | Euclid | `#euclidBtn` | Euclidean rhythm generator | 773 |
| Editing/Undo | Rotation | `#rotL`, `#rotR` | Rotates the pattern left/right | 774-775 |
| Editing/Undo | Transposition | `#trDn`, `#trUp`, `#trOctDn`, `#trOctUp` | ±1 semitone, ±1 octave | 776-779 |
| Sequencer | Follow | `#seqFollow` | Follows the playhead, default on (paging NCOLS=64 if steps>16) | 781, 2635, 2998-3006 |
| Sequencer | Track length | `#plenBtn` "Len =" | Polymeter: own length (plen, 0 = global length) | 782, 1901-1902 |
| Patterns/Song | Song | `#songMode` | Opens the Song window | 783 |
| Sequencer | Steps | `#seqSteps` / `#seqStepsv` | 1-256, default 16 | 786-787 |
| Timing | Tempo | `#bpm` / `#bpmv` | 60-240 BPM step 0.01, default 120; with MIDI shows "ext xx.xx" | 788-789, 3028 |
| Timing | Gate | `#gate` / `#gatev` | 5-95 %, default 50 | 790-791 |
| Timing | Swing | `#swing` / `#swingv` | 50-75 %, default 50; even ×S/50, odd ×(2-S/50); ignored with MIDI clock | 792-793, 1807-1813 |
| Scales | Root | `#scaleRoot` | C by default | 795 |
| Scales | Scale | `#scaleSel` | Off by default; 12-bit mask sent to the engine (scaleMask) | 796, 2846-2852 |
| Scales | Snap | `#scaleSnap` | Moves the placed notes to the scale degree (writeNoteAt→snapToScale) | 797, 2810-2814 |
| Timing | Time signature | `#seqSig` | 4/4 by default (GLOB.sig=2); accents and metronome | 799-800 |
| Timing | Resolution | `#seqReso` | 1/16 by default (GLOB.reso=2); spb sent to the engine | 801-802, 2905 |
| Sequencer | Figure (duration) | `#seqFig` | Length written when a note is placed (writeFigure, through TIE); ignored for drums | 803-804, 2788-2804 |
| Sequencer | Chord | `#seqChord` | Voices added above each placed note; default Off | 805-806 |
| Sequencer | Sequencer voicing | `#seqPolySel` | Polyphonic / Mono | 807-808 |
| Sequencer | Ghost | `#seqGhost` | Shows the other channels behind the grid | 810 |
| Sequencer | Overview | `#seqOver`, `#overRow`, `#overMap` (canvas h104) | Overview strip; click/drag moves the view | 811, 830-833 |
| Sequencer | Grid | `#seqgrid` | Piano roll (built outside the slice) | 813 |
| Sequencer | Vertical scrolling | `#seqTr` | 23-108 (B0-C9), default 48; the mouse wheel does the same | 814 |
| Sequencer | Horizontal scrolling | `#seqScroll`, `#seqScrollv`, `#seqScrollRow` | For a pattern > grid; readout "1-64" | 816-819, 2717-2726 |
| Sequencer | Transport bar | `#seqTbar` | Transport (content outside the slice) | 822 |
| Timing | Metronome | `#seqMetro` | Click on each beat, bell on the 1st beat; M key | 823-824, 2994 |
| Patterns/Song | Banks / patterns | `#patBanks`, `#patSlots` | Banks (Shift+1-4) and 8 slots (1-8); queued change applied at step 0 (patQueue) | 825-828, 1306-1318, 2078-2081, 3009-3015 |
| Sequencer | Hint | `#seqHint` | Contextual help | 834 |
| Sequencer | Step engine | `seqTick` | step=(step+1)%len; stepSamp = SR·60/(bpm·4)·swing (or 6·spt with MIDI); lanes: 4·chips (3ch) or 16 | 1796-1805 |
| Sequencer | Internal clock | `process` | phase += bpm/60·spb/SR; threshold = swing duration | 2222-2229 |
| Sequencer | Polymeter | `laneStep` | st = stepAbs mod plen | 1901-1902 |
| Sequencer | Chord voices | `polyTrigger` etc. | Voices 2-6 (pat2..pat6, dur2..dur6) per step, 16ch only, not in mono; drums: choke if mono | 1828-1896 |
| Step rows | NOTE/OCT | lane.pat, lane.oct | pat 0-12 relative + octave code (0 = channel octave, -1 = oct 0); absolute = (oct+1)·12+v | 2595, 2624-2634, 2879 |
| Step rows | TIE | lane.tie | Tie; note duration (figures) | 1972-1976, 2036 |
| Step rows | SLD | lane.slide | Slide to the next note of the track (VBL rate in 3ch, VBL·16 in 16ch) | 1761-1793, 2035 |
| Step rows | CRV | lane.slc | Slide curve: 0 Linear, 1 Exponential, 2 Logarithmic, 3 S-curve, 4 Stepped, 5 Sawtooth, 6 Wobble, 7 Chromatic, 8 Scale-locked, 9 Overshoot, 10 Bounce, 11 Random walk, 12 Custom; a change in the middle of a slide = reshape | 1678-1743, 1958-1961 |
| Step rows | ARP | lane.afl | Step played through the channel's arp; ARP=0 on a tie suspends the arp | 1963-1967, 2019-2024 |
| Step rows | VOL | lane.vol | 0-15, default 15; accent = level·vol/15 | 1926, 2597 |
| Step rows | MIX | lane.mix | 0 = channel, 1 Tone, 2 Noise, 3 Tone+Noise (no effect on drum/sync) | 1486-1497 |
| Step rows | FX | lane.fx | 0 channel, 1 Chip (off), 2 SID, 3 Sync, 4 Chipdrum; switches without retrigger on a tie | 1499-1513, 2017-2018 |
| Step rows | GAT | lane.gat | Step gate in % (0 = channel gate) | 1952-1953 |
| Step rows | PAN | lane.pnr | Pan per step: (v-101)/100, 0 = mixer default | 1921-1922 |
| Step rows | RTG | lane.rtg | Retrig: rtg+1 hits spread over the step, level ×0.92 | 1913-1920, 2236-2240 |
| Step rows | CND | lane.cnd | Conditions: 1 = 1:2, 2 = 2:2, 3 = 1:4, 4 = 4:4, 5 = 25 %, 6 = 50 %, 7 = 75 % | 1815-1826, 1908-1912 |
| Step rows | CUT | lane.cut | Cuts the playing sample (drums) | 1930-1933 |
| Step rows | SLOT | lane.slt → slotRow | Drum slot (sent to the engine) | 2598, 2882 |
| Step rows | TON | lane.ton | Tuning of the drums in semitones | 2601, 1935-1936 |
| Patterns/Song | Song window | `#songWin`, `#songBar`, `#songClose`, `#songTbar` | Chain of patterns | 594-602 |
| Patterns/Song | Add | `#songAdd` | Adds the current pattern; again = +1 repeat; Shift = new entry | 603-607 |
| Patterns/Song | Bank/pattern selection | `#songBanks`, `#songSlots` | Choice of the pattern to add | 609-614 |
| Patterns/Song | List | `#songList` | Click selects (Shift = several), right-click = actions | 616, 606 |
| Keyboard | Song keys | hint | Left/Right move, Ins/A add, Del delete, D duplicate, +/- repeats, X/C/V cut/copy/paste, Home/End | 617-618 |
| Patterns/Song | Chaining | handler `st` | End of pattern: songOnPa/songOnWrap; without loop (loopOn=0) and without song → stop | 3016-3021 |
| Sequencer | Slide Curve window | `#curveWin`, `#curveClose`, `#curveTrk`, `#curvegrid`, `#curveWarn` | Drawing of the Custom curve (CRV); drag to draw | 621-638 |
| Sequencer | Linear / Smooth / Invert | `#curveReset`, `#curveSmooth`, `#curveInvert` | Resets to a straight line, smooths, mirrors accel./decel. | 628-630 |
| Keyboard | Computer keyboard window | `#kbdWin`, `#kbdClose`, `#kbdLay`, `#tkl`, `#tklnum`; `#kbdMapBtn` | Clickable keyboard; typing plays only while the window is open; legend note/sharp/octave/transport | 640-668, 1082-1083 |
| Keyboard | Computer keyboard octave | `#kbdOctDn` (Z), `#kbdOctUp` (X), `#kbdOctV` | Default "Oct 3" | 646-652 |
| Keyboard | Held notes | `#kbdHeldSel` "Keep", `#labHeld` | Behaviour of the held notes on an octave change (kbdFollow 0/1) | 653-654, 2619 |
| Keyboard | Range | `#kbdRange` | Display of the played range | 655 |
| Keyboard | Virtual keyboard | `#kbd` | On-screen piano | 1066 |
| Keyboard | Pads | `#pads` (duplicated) | Hidden; **id `pads` defined twice** | 1067-1069 |
| Keyboard | Latch | `#kbLatch` | Keys held until pressed again | 1072-1073 |
| Keyboard | Layer | `#kbLayer` | Plays all the active tracks instead of the focused one (msg kbmode) | 1074, 1440-1452, 2101-2104 |
| Keyboard | Virtual keyboard octave | `#octDown` (Z), `#octUp` (X), `#octv` | Default "Oct 4" | 1075-1079 |
| Keyboard | Global shortcuts | hint | Space Play/Stop, 1-8 Pattern, Shift+1-4 Bank, ←→ channel, M metro, Ctrl+Z/Y, Esc Panic | 1085 |
| Keyboard | Window shortcuts | titles | Ctrl+M Mixer, Ctrl+B Arp, Ctrl+R Modulation, Ctrl+E Sequencer, Ctrl+P Patches, Ctrl+K Drum Kit, Ctrl+C → Chipdrum | 1055-1059, 869, 1000 |
| Keyboard | Velocity | `trigger` | level = clamp(vel/127, 0.05, 1) × channel level | 1444 |
| Keyboard | Preview of a written note | `previewNote` | noteOn vel 110, cut after 240 ms; drums through seqprev | 2727-2743 |
| Mixer | Window | `#mixWin`, `#mixBar`, `#mixClose`, `#openMixBtn` (Ctrl+M) | Floating mixer | 581-593, 3179-3189 |
| Mixer | Channel strip | `buildMixer` | Label button (click = channel focus), mini-scope 26×18, fader 0-100 (default 100, = level), VU, pan -100..100 (double-click = 0), M, S; 3ch: fader disabled, no pan, voice names | 3066-3112 |
| Mixer | Mute/Solo | `audible`, `sendMix` | mgain = !mute && (!anySolo || solo) | 3057-3064 |
| Mixer | VU meters | `vuPct`, `drawMeters` | 48 dB scale; the held peak decays 2.5 %/update; clip ≥ 0.999 | 3131-3163 |
| Mixer | Outputs | `#outL`/`#outLpk`/`#vuhL`, `#outR`/`#outRpk`/`#vuhR`, `#outLab`, `#outRrow` | Out L/R; 3ch: mono "Out" | 588-591, 3174-3178 |
| Mixer | Mixer undo | `pushMixUndo` | One step per drag / M/S click | 3091, 3099, 3107-3108 |
| Patches/Presets | Patches window | `#libWin`, `#libClose`, `#openLibBtn` (Ctrl+P) | Patch library | 690-706, 1059 |
| Patches/Presets | Save current | `#libSave` | Saves the sound of the focused channel | 697 |
| Patches/Presets | List | `#libList`, `#libHint` | Click = loads into the focused channel; right-click = actions | 700-704 |
| Import/Export | Library import/export | `#libImport`, `#libExport`, `#patchLibFile` (.json) | Reads/writes the patch library | 698-699, 707 |
| Import/Export | Preset/pattern/song files | `#presetFile`, `#patternFile`, `#songFile` (.json) | Opens a preset, pattern, song | 901-903 |
| Import/Export | MIDI file | `#midiInFile` (.mid/.midi) | MIDI import | 853 |
| Import/Export | YM file | `#ymFile` (.ym,.ym2,.ym3,.ym5,.ym6,.snd,.sndh) | Import of a YM tune | 852 |
| Import/Export | YM Player | `#ymWin`, `#ymClose`, `#ymName`, `#ymPlay`, `#ymRew`, `#ymPos` | Plays a register stream at the VBL rate, loops by default; Play stops man/seq; position sent every 8 frames | 837-851, 2085-2096, 2485-2524 |
| Control/Monitor | Shortcuts window | `#helpWin`, `#helpClose`, `#helpBody` | List of the shortcuts | 854-862 |
| Control/Monitor | Voices (System) | `#modeSel` | 16 ch (pan + polyphony) / 3 ch (authentic YM, mono) | 1035-1038 |
| MIDI | Clock | `#clkSel`, `#clkStatus` | Internal / MIDI; status SYNC/STOP | 1039-1043, 3026-3029 |
| Control/Monitor | Master | `#master` / `#masterv` | 0-100, default 70 | 1044-1045 |
| Control/Monitor | Voice LEDs | `#voices` | One LED per active audible channel | 1046, 2536-2544, 2982 |
| Control/Monitor | Note | `#tpread` | "TP=xxx -> f Hz" (f = 125000/TP) | 1047, 2980 |
| Control/Monitor | Oscilloscope | `#scope` (920×90) | Main scope (drawBigScope) | 1062 |
| Control/Monitor | Registers | `#regs` | Strip of the YM registers | 1064 |
| Control/Monitor | Telemetry | `process` | 20 Hz: LEDs, peaks of the 16 channels + output, env, TP, step, scope 16×128 (decimation 8) | 2532-2570 |
| Windows/UI | Button bar | `#winBtnBar` | Mixer, Arpeggiator, Modulation, Step Sequencer, Patches | 1054-1060 |
| Windows/UI | Floating windows | `topFloat`, `focusWin`, `syncWinBtns` | Z order (winOrder), state of the buttons | 2710-2716 |
| Editing/Undo | Patch undo step | `armCur`, `pushCur` | Snapshot on pointerdown/keydown in main/orn/arp; stack of 50; redo cleared | 2913-2934 |
| Editing/Undo | Rectangular selection | `rectSel`, `rectNotes`, `rectMove`, `rectClear` | Move/clear a step×pitch block (cancelled if outside the pattern) | 2637-2703 |
| Editing/Undo | Note-by-note selection | `noteSel`, `selToggle` | Shift+click | 2639-2655 |
| Editing/Undo | Block selection / right-click | `selA/selB`, `blockClip`, `rightSel` | Step selection, clipboard | 2636-2638, 2708-2709 |
| Sequencer | Track arming | `armChan` | The first placed note enables the channel | 2805-2809 |
| Audio engine | Startup | `boot` | AudioWorklet (Blob), else ScriptProcessor 2048; Analyser fft 2048; sends patch/arp/chans/seq/clk; applyTips; initMIDI | 2938-3039 |
| Audio engine | Voices | worklet | 16 channels, 112 voices (16 + 16·5 poly + 16 manual pool); voice stealing | 1107-1111, 1422-1438 |
| Audio engine | 3ch mode / 2 chips | `chipOf`, `laneVoice` | 3 channels per chip + Digidrum track (4 tracks/chip); chips=1|2; noise/envelope shared per chip | 1417-1421, 1515-1524, 2295-2313 |
| Audio engine | Pitch bend | msg `bend` | Semitones added to all the voices | 2111-2114, 1354 |
| Audio engine | Panic | msg `allOff` | 64-sample fade of all the voices | 2127-2130 |
| MIDI | MIDI clock | msgs `mclk`, `mstart`, `mcont`, `mstop`, `mspp` | 24 ppqn; seq tick every 6 clocks; spt smoothed 0.9/0.1; ext BPM = 60·SR/(spt·24); SPP | 2167-2207, 2564 |
| Other | Model constants | `NCH=16`, `NSTEP=256`, `POLYMAX=6` | Limits | 2580-2582 |
| Other | Global defaults | `GLOB` | master 0.7, mode3ch 0, chips 1, bpm 120, gate 0.5, steps 16, digiCh 2, digiCh2 2, vbl 50, sig 2, reso 2; CLK {midi 0, arpDiv 6} | 2614-2616 |

## DSP

### (1) Envelope — step rate (32 levels, 5 bits)
- 16ch engine, line 2388: `v.env.phase += 2000000 / (256 * Math.max(1, v.EP)) / this.SR;`
- Shared 3ch engine (per chip), line 2310: `e.phase += 2000000 / (256 * Math.max(1, epUse)) / this.SR;` (EP/shape set by the Sync Buzzer voice first, else by a voice in volMode, else by the patch, l. 2301-2308)
- YM player, line 2509: `Y.env.phase = (Y.env.phase || 0) + 2000000 / (256 * Math.max(1, Y.ep)) / this.SR;`
- Envelope step (l. 1320-1341): `envReset` vol = attack ? 0 : 31, inc ±1; `envTick`: continue/alt/hold from bits 8/2/1 of R13; when alternating `e.inc = -e.inc; e.vol += 2 * e.inc;`.
- UI display (outside the slice, for reference) l. 4144: `2000000/(256*32*P().EP)` Hz per cycle. **Note**: on a real YM2149 (2 MHz), the cycle frequency is 2 MHz/(256·EP), that is 32 steps at 2 MHz/(8·EP). Here each step is clocked at 2 MHz/(256·EP): the cycle is 32× slower than the hardware (to be checked).
- Tone (l. 2379): `v.tonePhase += 250000 / ((v.tpEff || v.TP || 1) * this.SR);` (toggles at 2·f, f = 125000/TP); TP = round(125000/f), clamped to 1-4095 (l. 1353-1356).

### (2) Volume table / DAC
- Lines 1105-1106: `this.tab = new Float32Array(32); for (let n = 1; n < 32; n++) this.tab[n] = Math.pow(10, -(31 - n) * 1.5 / 20);` → 1.5 dB per step, tab[0]=0, tab[31]=1.
- Fixed volume → 5-bit index, l. 2432-2434: `n5 = (v.volMode && v.effect !== 1) ? (m3 ? this.envOf(v.ch).vol : v.env.vol) : (2 * vol4 + 1);` (vol 0 → index 1 ≈ -45 dB, not quite silent).
- Digidrum, l. 2427: `n5 = 2 * Math.max(0, Math.min(15, sv)) + 1;`
- Gain, l. 2438: `let a = gate * tab[n5] * v.level * mg * (v.effect === 2 ? 5 : digi ? 2.5 : 1);`
- Mixer AND gate, l. 2429: `gate = (v.toneBit | !v.toneEn) & (nBit | !(v.noiseEn || v.ornNoise));`

### (3) Mixing
- Constant-power pan, l. 2457-2462: `const ang = (pp + 1) * 0.7853981633974483; mix += a * Math.cos(ang); mixR += a * Math.sin(ang);` (3ch: pp=0, mono).
- Output, l. 2525-2527: `const g = p.master * 1.2 * 1.4142135623730951; L[s] = Math.tanh(mix * g); R[s] = R === L ? L[s] : Math.tanh(mixR * g);`
- YM player, l. 2520-2522: `const gy = p.master * 1.2 * 1.4142135623730951; L[s] = Math.tanh(acc * gy * 0.7071067811865476);` R = L (mono).
- Kit preview, l. 2480: `(b[k] - 7.5) / 7.5 * 0.55` added to both channels.
- Voice fade: `a *= v.fadeAmt; v.fadeAmt -= 1/64` (l. 2456).

### (4) Noise LFSR
- Lines 1343-1351:
  ```
  n.phase += 125000 / (Math.max(1, NP) * this.SR);
  const fb = (n.lfsr & 1) ^ ((n.lfsr >> 3) & 1);
  n.lfsr = (n.lfsr >>> 1) | (fb << 16);
  n.bit = n.lfsr & 1;
  ```
  17-bit LFSR, taps 0 and 3, seed 1; one LFSR per voice in 16ch, shared per chip in 3ch (l. 2296-2297, 2384-2385). The YM player uses the same LFSR (l. 2508).
- Other RNG: xorshift 13/17/5, seed 0x1234 (arp Random, l. 1572-1577).

### (5) Timers: SID / Sync Buzzer / Digidrum / Chipdrum
- `applyPitch`, l. 1361-1364:
  ```
  if (v.effect === 1) v.timerFreq = f * v.sidRatio;
  else if (v.effect === 3) v.timerFreq = f;
  else if (v.effect === 2) v.timerFreq = this.drumRate * Math.pow(2, (v.note - 60) / 12);
  else if (v.effect === 4) v.timerFreq = this.drumRate * (v.drumTr || 1);
  ```
  drumRate = 7800 (l. 1240, 1302).
- Timer loop, l. 2408-2417: `v.timerPhase += v.timerFreq / this.SR;` then SID `v.sidState ^= 1`; digi `v.dpos++` (end → voice off); buzzer `this.envReset(m3 ? this.envOf(v.ch) : v.env, v.shape)`.
- SID volume, l. 2431: `if (v.effect === 1) vol4 = v.sidState ? v.vol4 : v.sidVmin;`
- Chipdrum 3ch: frame read at each VBL, l. 2393-2394: `v.cdClk = Math.max(1, Math.round(this.SR / (p.vbl || 50)));`
- Vibrato/slide also modulate timerFreq (l. 2332-2333, 2371-2375).

### (6) Data schema (patch / arp / track / global)
- Channel patch (`mkCh().patch`, l. 2587-2592): toneEn, noiseEn, mono, NP, volMode, vol4, EP, shape, effect, sidRatio, sidVmin, slot, vibDepth, vibRate, ornOn, ornLen, ornLoop, ornRate, ornTable[64], ornNse[64], ornVol[64], atk, dcy, sus, rel, adsrChip.
- Arp (l. 2593-2594): on, rate, track, land, mode, oct, table[16], tlen.
- Track `lane` (l. 2595-2607), arrays of 256: pat, tie, slide, afl, vol, mix, slt, cut, fx, oct, gat, pnr, rtg, cnd, slc, ton, pat2..pat6, oct2..oct6, dur2..dur6.
- Channel: enabled, level, seqOct (3), gate (0.5), plen (0), + mute, solo, pan, name (mixer).
- Global `GLOB` (l. 2614-2615): master, mode3ch, chips, bpm, gate, seqOn, steps, digiCh, digiCh2, vbl, sig, reso (+ swing).
- Kit (l. 3371-3380): 8 instruments × {pitch, sweep, dec, noise, vol} / {body, tone, dec, vol} / {space, color, tail, vol} / {metal, tune, dec, vol}.
- Message `chan` to the engine (chanPayload, l. 2868-2896): patch + arpRate/arpTrack/arpLand/arpTable/arpTlen/arpMode/arpOct, pan, plen, laneOn, absolute pat, tie, slide, afl, vol, mix, slotRow, cut, fx, ton, oct2r, gat, pnr, rtg, cnd, slc, pat2-6, dur2-6 (dur keys duplicated l. 2890-2893), arpOn.
- The JSON export formats (preset/pattern/song) are written outside the slice.
- YM stream (l. 2489-2497): row of 14 bytes r[0..13] (TP A/B/C, R6, R7, R8-R10, EP R11-R12, R13; 0xFF = no R13 write).

### (7) VBL rate
- `p.vbl || 50` everywhere; GLOB.vbl = 50 (l. 2615); comments "50/60/70 Hz" (l. 2318, 2336).
- Modulation/vibrato/ADSR chip: `v.vibClk = Math.max(1, Math.round(this.SR / (p.vbl || 50)));` (l. 2343); vibrato `v.vibPhase = (v.vibPhase + v.vibRate / (p.vbl || 50)) % 1;` (l. 2364).
- Slide: `v.glideHz = this.patch.mode3ch ? vbl : vbl * 16;` (l. 1787); arp+slide: `hz = mode3ch ? vbl : vbl * 16` (l. 1754).
- Chipdrum: tables generated with `F = this.patch.vbl || 50` (l. 1197) — rebuilt only when the kit changes.
- YM player: `Y.clk = Math.max(1, Math.round(this.SR / (p.vbl || 50)));` (l. 2488).

### Anomalies found in the slice
- **l. 2279** `if (!arpRun) return;` in the per-sample loop of `process()`: leaves `process` without `return true` (rest of the buffer silent; in an AudioWorklet, a falsy return can stop the node). Happens when an arp slide (arpGl) runs while the arp is stopped.
- **l. 2231** `laneMax = m3 ? 4 : this.NV`: in 3ch with 2 chips, the gates/CUT/RTG/arp of tracks 4-7 are not processed (while seqTick processes 4·chips).
- **l. 1801** stepSamp assumes 4 steps per quarter note (`bpm*4`) while the clock uses `spb` (RESOS): gate/slide/RTG durations are wrong outside 1/16.
- **l. 994** the tooltip of the SID Timer ratio talks about Digidrum; **l. 1021** hint "Slot = note mod 4" against `(note%12)%8` in the code.
- **l. 1067-1068** id `pads` duplicated; **l. 2890-2893** dur2-6 keys duplicated; **l. 1130/1137** focusCh initialised twice.
- **l. 3023** test `d.type === 'ymstate'` placed in the `'st'` branch: never true. It is the only `port.onmessage` on the UI side (grep), so the position and the end of playback of the YM player never come back to the UI (`#ymPos` is not updated by the worklet).
- Envelope 32× slower than the hardware (see DSP 1); volume 0 not silent (index 1).
