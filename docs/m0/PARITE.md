# Parity matrix prototype → plugin (M0)

State of each row at the end of M8 (done and tested, deviation and decision, or not kept): [docs/m8/PARITY.md](../m8/PARITY.md).

Source: `YM2149_Prototype.html` read in full (HTML 553-1091, JS 1092-10541) in three slices, then merged. One row per visible control or function. No row without a module and a test.

**Test types**: U = unit (Catch2); G = golden render (audio comparison); S = state round trip / prototype-compatible JSON; V = visual non-regression (capture diff < 1 %); M = documented manual test. The test id is `T-<id>`; it will be created at the milestone given by the module.

**Parameter**: name of the automatable parameter (§7) if there is one, otherwise "state" (saved in the project, not automatable) or "UI".

Rows duplicated between slices are kept on purpose: they trace the same control on the HTML side and on the code side; one test covers them together.

| Id | Domain | Control / function (prototype) | Element | Behaviour, range, default | Lines | Target module | Parameter | Test | Note |
|---|---|---|---|---|---|---|---|---|---|
| P001 | Windows/UI | Global zoom container | `#zoomRoot` | Root of the whole UI (zoom applied elsewhere) | 554 | gemui | UI | V T-P001 |  |
| P002 | Windows/UI | Message box | `#msgWin`, `#msgTxt`, `#msgOk` | Modal alert, OK = Enter | 557-562 | gemui | UI | V T-P002 |  |
| P003 | Windows/UI | About box | `#infoWin`, `#infoOk` | "STYM2149 from PRISM", Pascal "Indus" Nowak, Prototype 1, © 2026 PRISM, alpha licence text | 563-577 | gemui | UI | V T-P003 |  |
| P004 | MIDI | MIDI learn badge | `#learnBadge` | Fixed badge at the bottom left, visible in learn mode | 578-580 | plugin | state | U+M T-P004 |  |
| P005 | Windows/UI | Input box | `#promptWin`, `#promptMsg`, `#promptInput`, `#promptCancel`, `#promptOk` | Modal text prompt (30 chars) | 884-891 | gemui | UI | V T-P005 |  |
| P006 | Windows/UI | Confirmation box | `#confirmWin`, `#confirmMsg`, `#confirmCancel` (default, Enter), `#confirmOk` | Modal confirmation, Cancel by default | 892-900 | gemui | UI | V T-P006 |  |
| P007 | Windows/UI | Boot screen | `#boot`, `#bootbtn` "Run program" | Starts the AudioContext; text "16 channels / Digidrums / SID Voice / Sync Buzzer"; on error shows "Startup error" + message | 905-910, 3040-3048 | gemui | UI | V T-P007 | Adaptation: no "Run program" screen in the plugin; kept in Standalone (DECISIONS D-020) |
| P008 | Windows/UI | GEM main window | `#mainWin`, `#mainBar` (draggable), `#mainClose`, `.fullbox` | Atari GEM style window, title STYM2149 | 912-917 | gemui | UI | V T-P008 |  |
| P009 | Options/Menus | Menu bar | `#mFuji` (Atari logo), `#mFile` File, `#mEdit` Edit, `#mOptions` Options | Drop-down menus (content built outside the slice) | 918-920 | gemui | UI | V+S T-P009 |  |
| P010 | Control/Monitor | CHIP indicator | `#chipTxt` "CHIP: -" | Clickable, "Chip conformity" | 919 | gemui + ymengine | UI | V+U T-P010 |  |
| P011 | MIDI | MIDI indicator | `#miditxt` "MIDI: -" | MIDI status in the menu bar | 919 | plugin | state | U+M T-P011 |  |
| P012 | Tracks | Channel tabs | `#chtabs` | Tabs of the 16 channels (built outside the slice) | 923 | ymseq + gemui | state | S+V T-P012 |  |
| P013 | Source | Source | `#srcSel` (+`#labSrc`) | Tone / Noise / Tone+Noise; at least one active; default Tone (toneEn=1, noiseEn=0) | 929-933, 2587 | ymengine | track[n].source | U+V T-P013 |  |
| P014 | Source | Voicing (polyphony) | `#polySel` (+`#labPoly`) | Polyphonic / Mono (patch.mono, default 0); Mono = legato with a 45 ms slide | 934-936, 1453-1479 | ymengine | state | U+V T-P014 |  |
| P015 | Source | Noise period | `#np` / `#npv` | 1-31, default 8 (R6) | 938-939 | ymengine | track[n].noisePeriod | U+V T-P015 |  |
| P016 | Source | 4-bit volume | `#vol` / `#volv` | 0-15, default 13 | 940-941 | ymengine | track[n].vol4 | U+V T-P016 |  |
| P017 | Source | Volume mode | `#vmSel` | "Vol 4-bit" / hardware envelope (volMode 0/1) | 942-944 | ymengine | track[n].volMode | U+V T-P017 |  |
| P018 | Source | Channel level | `#chLevel` / `#chLevelv` | 0-100, default 75 (level 0.75) | 945-946, 2586 | ymengine | track[n].level | U+V T-P018 |  |
| P019 | Envelope | Env freq | `#ef` / `#efv` | Slider 0-4000 (mapped by efToHz outside the slice), default 1840; Hz display = 2e6/(256·32·EP) | 952-953 | ymcore + ymengine | track[n].envPeriod | U+V T-P019 |  |
| P020 | Envelope | EP readout | `#epv` | Shows EP (dec + hex); default EP=400; editable (outside the slice) | 954, 2587 | ymcore + ymengine | track[n].envPeriod | U+V T-P020 |  |
| P021 | Envelope | R13 shape | `#shapeSel`, `#shapeLab` | 16 shapes (0-15), default 8; icons `SHAPE_SVG`, labels `SHAPES` | 956-957, 2822-2842 | ymcore + ymengine | track[n].envShape | U+V T-P021 |  |
| P022 | Envelope | Shared indicator | `#envShared` | "(per channel)" / shared in 3ch | 951 | ymcore + ymengine | state | U+V T-P022 |  |
| P023 | Vibrato/ADSR | Vibrato Depth | `#vibDepth` | 0-200 cents, default 0 | 962-963 | ymengine | track[n].vibDepth | U+V T-P023 |  |
| P024 | Vibrato/ADSR | Vibrato Rate | `#vibRate` | 1-20 Hz step 0.5, default 6; computed at the VBL rate | 964-965, 2363-2366 | ymengine | track[n].vibRate | U+V T-P024 |  |
| P025 | Vibrato/ADSR | Attack | `#atk` | 0-1000 ms, default 0 | 970-971 | ymengine | track[n].attack | U+V T-P025 |  |
| P026 | Vibrato/ADSR | Decay | `#dcy` | 0-1000 ms, default 0 | 972-973 | ymengine | track[n].decay | U+V T-P026 |  |
| P027 | Vibrato/ADSR | Sustain | `#sus` | 0-100 %, default 100 | 974-975 | ymengine | track[n].sustain | U+V T-P027 |  |
| P028 | Vibrato/ADSR | Release | `#rel` | 0-2000 ms, default 0 | 976-977 | ymengine | track[n].release | U+V T-P028 |  |
| P029 | Vibrato/ADSR | ADSR mode | `#adsrMode`, `#adsrHint` | Smooth (linear audio gain per sample) / Chip (4-bit volume quantised at the VBL rate); 3ch forces Chip | 978-980, 1409, 2359-2362 | ymengine | state | U+V T-P029 |  |
| P030 | Vibrato/ADSR | Transparent ADSR | `setupVoice` | 0/0/100/0 = ADSR disabled (adsrOn=0) | 1402-1410 | ymengine | state | U+V T-P030 |  |
| P031 | Vibrato/ADSR | ADSR curve | `#adsrCurve` (228×52), `#adsrCurveLab` | Drawing of the ADSR + live tracking (stage/level of the focused channel, 20 Hz telemetry) | 1026-1030, 2549-2559, 2983-2990 | ymengine | state | U+V T-P031 |  |
| P032 | Generators | Generator type | `#fxSel` | Chip Voice (0), SID Voice (1), Digidrum (2), Sync Buzzer (3), Chipdrum (4) | 987-989, 2821 | ymengine | state | U+V T-P032 |  |
| P033 | Generators | Timer ratio (SID) | `#ratio` / `#ratiov` | 25-800 → x0.25-x8.00, default 100 (x1.00); SID timerFreq = f·ratio. Wrong tooltip ("Digidrum playback rate") | 992-994, 1361 | ymengine | track[n].sidRatio | U+V T-P033 |  |
| P034 | Generators | SID min vol | `#vmin` / `#vminv` | 0-15, default 0; low level of the SID wave | 995-997, 2431 | ymengine | track[n].sidVmin | U+V T-P034 |  |
| P035 | Generators | SID Voice | worklet `effect===1` | Timer at f·sidRatio toggles sidState; vol = sidState ? vol4 : sidVmin; ignores volMode | 2408-2411, 2431-2434 | ymengine | state | U+V T-P035 |  |
| P036 | Generators | Sync Buzzer | `setupVoice`/`liveFx` `effect===3` | volMode=1, tone/noise off, EP>100 → EP=1; a timer at f restarts the envelope at each period | 1390-1393, 1505-1508, 2416 | ymengine | state | U+V T-P036 |  |
| P037 | Chipdrum | Chipdrum panel | `#ndrumPanel`, `#openKitBtn` (Ctrl+K), `#genKitName` | Opens the Drum Kit window, kit name | 999-1002 | ymengine | state | G+V T-P037 |  |
| P038 | Chipdrum | Drum Kit window | `#kitWin`, `#kitBar`, `#kitClose`, `#kitOff` | Message if the track is not Chipdrum (Ctrl+C to switch) | 863-870 | ymengine | state | G+V T-P038 |  |
| P039 | Chipdrum | Instrument selector | `#kitInst` | 8 instruments: kick, snare, clap, hat, ohat, chat, tom, tom2 (slots 0-7) | 872, 3455 | ymengine | state | G+V T-P039 |  |
| P040 | Chipdrum | Kit sliders | `#kitSliders`, `KITSPEC` | kick: Pitch 25-90, Sweep 20-300, Decay 4-20, Noise 0-100, Vol 0-100; snare: Body 80-400, Tone 0-70, Decay 6-30, Vol; clap: Space 6-20, Color 15-80, Tail 10-60, Vol; hat: Metal 0-100, Tune 60-160, Decay 15-120, Vol; ohat: same, Decay 3-30; chat: same, Decay 30-250; tom: Pitch 60-200, Sweep 20-150, Decay 4-20, Noise, Vol; tom2: Pitch 80-260, same | 873, 3382-3392 | ymengine | state | G+V T-P040 |  |
| P041 | Chipdrum | Kit default values | `KIT` / worklet `buildDrums` | kick{45,115,9,0,100} snare{180,35,16,100} clap{10,35,26,100} hat{78,108,45,100} ohat{72,112,9,100} chat{90,134,95,100} tom{110,75,9,0,100} tom2{150,85,11,0,100} | 1230-1239, 3371-3380 | ymengine | state | G+V T-P041 |  |
| P042 | Chipdrum | Audition | `#kitPlay` | Preview (msg `drumprev`, builtin sample at 7800 Hz, gain 0.55) | 876, 2105-2107, 2475-2484 | ymengine | state | G+V T-P042 |  |
| P043 | Chipdrum | Reset instrument | `#kitReset` | Restores the factory values (KIT_DEF) of the instrument | 877, 3381 | ymengine | state | G+V T-P043 |  |
| P044 | Chipdrum | Kit banks | `#kitBank` "Kits", `KIT_BANKS` | 13 kits: Classic, TR-606, TR-909-ish, Hard EBM, Industrial, Lo-Fi 4bit, Chiptune, TR-808, House, Techno, Hip-Hop, Trap, Drum'n'Bass | 878, 3393-3454 | ymengine | state | G+V T-P044 |  |
| P045 | Chipdrum | Kit name | `#kitName` | Display | 880 | ymengine | state | G+V T-P045 |  |
| P046 | Chipdrum | Kit import | `#kitFile` (.json) | Loads a kit from a file | 708 | ymengine | state | G+V T-P046 |  |
| P047 | Chipdrum | Live rebuild | msg `kit` → `buildDrums`+`buildChipDrums` | Recomputes the samples during playback | 2097-2100, 879 | ymengine | state | G+V T-P047 |  |
| P048 | Chipdrum | 16ch synthesis (samples) | `buildDrums` | 8 samples (4-bit) at R=7800 Hz: kick 0.30 s (swept sine f=pitch+sweep·e^(-14t), drive+grit), snare 0.18 s (noise·0.75 + sine body·tone), clap 0.16 s (3 bursts space ms + gap 4 ms, noise filtered by color, tail set by tail), hats (3 inharmonic squares 1583/2145/2808 Hz·tune + derived noise; 0.09/0.30/0.045 s), toms 0.22/0.20 s; RNG LCG 1103515245 | 1229-1304 | ymengine | state | G+V T-P048 |  |
| P049 | Chipdrum | 3ch synthesis (registers) | `buildChipDrums` | Tables per VBL frame (2-48 frames, nf=round(F·3/dec)): TP=125000/f, NP, mixer, vol 0-15; snare NP=12; clap alternating bursts; hats NP=31-tune·26/100 | 1195-1227 | ymengine | state | G+V T-P049 |  |
| P050 | Chipdrum | Chipdrum pitch | `applyPitch`, `laneStep` | timerFreq = 7800·drumTr; drumTr = 2^((OCT·12+TON)/12) per step; in 3ch frame TP / drumTr | 1364, 1935-1936, 2399-2401 | ymengine | state | G+V T-P050 |  |
| P051 | Chipdrum | Chipdrum TIE | `laneStep` | TIE on a Chipdrum step: no retrigger while it sounds | 1929 | ymengine | state | G+V T-P051 |  |
| P052 | Digidrum | Slots | `#drumSlots` buttons `data-ds` 0-7 | 8 user slots; default 1 | 1004-1014 | ymengine + ymio | state | U+V T-P052 |  |
| P053 | Digidrum | Load sample | `#drumFile` (audio/*) | Loads a sample into the selected slot (msg `drum`, Uint8 0-15) | 1015, 2082-2084 | ymengine + ymio | state | U+V T-P053 |  |
| P054 | Digidrum | ST channel (3ch) | `#digiChSel`, `#digiChRow` | Channel A/B/C stolen by the drum in 3ch, default C (digiCh=2; digiCh2 for the 2nd chip) | 1017-1019, 2615 | ymengine + ymio | state | U+V T-P054 |  |
| P055 | Digidrum | Waveform | `#drumwave` (360×48) | Display of the sample | 1020 | ymengine + ymio | state | U+V T-P055 |  |
| P056 | Digidrum | Playback | worklet `effect===2` | timerFreq = 7800·2^((note-60)/12); slot = (note%12)%8 in the sequencer, src.slot in manual play; missing sample = silence; gain ×5 (makes up for the log DAC); 3ch: the accent multiplies the sample (digiLvl) | 1363, 1394-1396, 2404-2427, 2438 | ymengine + ymio | state | U+V T-P056 |  |
| P057 | Digidrum | Inconsistent hint | text | Says "Slot = note mod 4", the code does (note%12)%8 | 1021 | ymengine + ymio | state | U+V T-P057 |  |
| P058 | Digidrum | 3ch replay priority | `laneStep` | If a digidrum holds the channel, the music track gives up its step | 1923-1925 | ymengine + ymio | state | U+V T-P058 |  |
| P059 | Arpeggiator | Window | `#arpWin`, `#arpBar`, `#arpClose`, `#arpTrk`; opened by `#openArpBtn` (Ctrl+B) | Floating, draggable | 709-713, 1056 | ymengine | state | U+V T-P059 |  |
| P060 | Arpeggiator | On/Off | `#arpOn` | Enables the channel's arp (default off); forced off for drum tracks | 716, 2894 | ymengine | state | U+V T-P060 |  |
| P061 | Arpeggiator | Mode | `#arpModeSel` | 0 Up, 1 Down, 2 UpDn, 3 Order (order played), 4 Random, 5 Table; default Up (engine channel: 5) | 721, 1590-1612, 1654-1665 | ymengine | state | U+V T-P061 |  |
| P062 | Arpeggiator | Rate | `#arpRate` / `#arpRatev` | 4-200 Hz, default 50 (= VBL) | 722-723 | ymengine | track[n].arpRate | U+V T-P062 |  |
| P063 | Arpeggiator | Track (follows the slide) | `#arpGlideRate` | During a slide, rate·(1+(min(2^(gl/12), 200/rate)-1)·t) | 724-725, 2281-2289 | ymengine | state | U+V T-P063 |  |
| P064 | Arpeggiator | Land | `#arpLand` | On the last tick of a slide, back to the root note | 726-727, 1638-1645 | ymengine | state | U+V T-P064 |  |
| P065 | Arpeggiator | Division (MIDI clock) | `#arpDivs` `data-ad` | 1/4=24, 1/8=12, 1/8T=8, 1/16=6 (default), 1/16T=4, 1/32=3, Tick=1 (MIDI ticks); visible with MIDI Clock | 728-737, 2177-2185 | ymengine | state | U+V T-P065 |  |
| P066 | Arpeggiator | Octaves | `#arpOcts` `data-ao` | 1 (default), 2, 3; ignored in Table mode | 738-746, 1633 | ymengine | state | U+V T-P066 |  |
| P067 | Arpeggiator | Steps (table) | `#arpLen` / `#arpLenv` | 1-16, default 3 | 747-749 | ymengine | state | U+V T-P067 |  |
| P068 | Arpeggiator | Chord… | `#chordSel` | Fills the table with a chord (CHORDS list outside the slice) | 751 | ymengine | state | U+V T-P068 |  |
| P069 | Arpeggiator | Table grid | `#arpgrid` | Offsets in semitones (hex), default [0,4,7]; applied to the last note | 753-754, 2594 | ymengine | state | U+V T-P069 |  |
| P070 | Arpeggiator | Live arp | `arpTick` | Held notes; goes on during the release (retune only); keeps the ADSR state between steps (grabEnv/putEnv); velocity per note | 1580-1624 | ymengine | state | U+V T-P070 |  |
| P071 | Arpeggiator | Track arp | `laneArpTick` | Sequencer ARP flag, literal Table; suspended (ARP=0) during a slide; the base slides under the arpeggio | 1627-1676, 1744-1760 | ymengine | state | U+V T-P071 |  |
| P072 | Modulation | Window | `#ornWin`, `#ornClose`, `#ornTrk`; `#openOrnBtn` (Ctrl+R) | "Ornament" table per VBL frame | 670-689, 1057, 3344-3349 | ymengine | state | U+V T-P072 |  |
| P073 | Modulation | On/Off | `#ornOn` | Disabled for Digidrum/Chipdrum | 677, 3350-3353, 3302-3317 | ymengine | state | U+V T-P073 |  |
| P074 | Modulation | Len | `#ornLen` / `#ornLenv` | 1-64, default 8 | 678, 3365-3368 | ymengine | state | U+V T-P074 |  |
| P075 | Modulation | Loop/Hold | `#ornLoop` | Loop (default) / Hold on the last step | 679, 3354, 2355-2356 | ymengine | state | U+V T-P075 |  |
| P076 | Modulation | Rate | `#ornRate` | Menu 1/frame…4/frame (VBL ticks per step), default 1 | 680, 3355-3359 | ymengine | state | U+V T-P076 |  |
| P077 | Modulation | Clear | `#ornClear` | Pitch=0, NSE=0, VOL=15 on 64 steps | 681, 3360-3364 | ymengine | state | U+V T-P077 |  |
| P078 | Modulation | Pitch grid | `#orngrid` `buildOrnGrid` | 25 rows (+12…-12 semitones) × 64 columns, click/drag to draw | 3192-3219 | ymengine | state | U+V T-P078 |  |
| P079 | Modulation | NSE row | cells `.oc.nse` | Enables the noise on this tick (toggle painted by dragging) | 3223-3239, 2348, 2429 | ymengine | state | U+V T-P079 |  |
| P080 | Modulation | VOL row | cells `.oc.vcell` | Level 0-15 by vertical position (drag); right-click = precise slider menu (`openVolMenu`) | 3240-3273 | ymengine | state | U+V T-P080 |  |
| P081 | Modulation | Engine | `process` | On each VBL frame: semis = ornTable[pos]; vol4 = round(vol4Base·ornVol/15); tpEff = TP·2^(-semis/12); SID/Buzzer follow | 2337-2377 | ymengine | state | U+V T-P081 |  |
| P082 | Sequencer | Window | `#seqWin`, `#seqClose`, `#seqTrk`; `#openSeqBtn` (Ctrl+E) | Step sequencer per channel | 759-763, 1058 | ymseq + gemui | state | U+V T-P082 |  |
| P083 | Sequencer | Tools | `#seqTools` | Toolbar (built outside the slice) | 766 | ymseq + gemui | state | U+V T-P083 |  |
| P084 | Sequencer | Play / Rec | `#seqPlay`, `#seqRec` | Hidden by default; Play/Stop, live recording | 768-769 | ymseq + gemui | state | U+V T-P084 | Adaptation: Host sync / Free run (§4) |
| P085 | Sequencer | Step input | `#seqStepRec` | Step-by-step input at the cursor | 770 | ymseq + gemui | state | U+V T-P085 |  |
| P086 | Sequencer | Clear | `#seqClear` | Clears the current track | 771 | ymseq + gemui | state | U+V T-P086 |  |
| P087 | Generators | Euclid | `#euclidBtn` | Euclidean rhythm generator | 773 | ymengine | state | U+V T-P087 |  |
| P088 | Editing/Undo | Rotation | `#rotL`, `#rotR` | Rotates the pattern left/right | 774-775 | ymseq | state | U+V T-P088 |  |
| P089 | Editing/Undo | Transposition | `#trDn`, `#trUp`, `#trOctDn`, `#trOctUp` | ±1 semitone, ±1 octave | 776-779 | ymseq | state | U+V T-P089 |  |
| P090 | Sequencer | Follow | `#seqFollow` | Follows the playhead, default on (paging NCOLS=64 if steps>16) | 781, 2635, 2998-3006 | ymseq + gemui | state | U+V T-P090 |  |
| P091 | Sequencer | Track length | `#plenBtn` "Len =" | Polymeter: own length (plen, 0 = global length) | 782, 1901-1902 | ymseq + gemui | state | U+V T-P091 |  |
| P092 | Patterns/Song | Song | `#songMode` | Opens the Song window | 783 | ymseq | state | U+V T-P092 |  |
| P093 | Sequencer | Steps | `#seqSteps` / `#seqStepsv` | 1-256, default 16 | 786-787 | ymseq + gemui | state | U+V T-P093 |  |
| P094 | Timing | Tempo | `#bpm` / `#bpmv` | 60-240 BPM step 0.01, default 120; with MIDI shows "ext xx.xx" | 788-789, 3028 | ymseq | global.freeTempo | U+V T-P094 |  |
| P095 | Timing | Gate | `#gate` / `#gatev` | 5-95 %, default 50 | 790-791 | ymseq | track[n].gate (+ global gate) | U+V T-P095 |  |
| P096 | Timing | Swing | `#swing` / `#swingv` | 50-75 %, default 50; even ×S/50, odd ×(2-S/50); ignored with MIDI clock | 792-793, 1807-1813 | ymseq | global.swing | U+V T-P096 |  |
| P097 | Scales | Root | `#scaleRoot` | C by default | 795 | ymseq | state | U+V T-P097 |  |
| P098 | Scales | Scale | `#scaleSel` | Off by default; 12-bit mask sent to the engine (scaleMask) | 796, 2846-2852 | ymseq | state | U+V T-P098 |  |
| P099 | Scales | Snap | `#scaleSnap` | Moves the placed notes to the scale degree (writeNoteAt→snapToScale) | 797, 2810-2814 | ymseq | state | U+V T-P099 |  |
| P100 | Timing | Time signature | `#seqSig` | 4/4 by default (GLOB.sig=2); accents and metronome | 799-800 | ymseq | state | U+V T-P100 |  |
| P101 | Timing | Resolution | `#seqReso` | 1/16 by default (GLOB.reso=2); spb sent to the engine | 801-802, 2905 | ymseq | state | U+V T-P101 |  |
| P102 | Sequencer | Figure (duration) | `#seqFig` | Length written when a note is placed (writeFigure, through TIE); ignored for drums | 803-804, 2788-2804 | ymseq + gemui | state | U+V T-P102 |  |
| P103 | Sequencer | Chord | `#seqChord` | Voices added above each placed note; default Off | 805-806 | ymseq + gemui | state | U+V T-P103 |  |
| P104 | Sequencer | Sequencer voicing | `#seqPolySel` | Polyphonic / Mono | 807-808 | ymseq + gemui | state | U+V T-P104 |  |
| P105 | Sequencer | Ghost | `#seqGhost` | Shows the other channels behind the grid | 810 | ymseq + gemui | state | U+V T-P105 |  |
| P106 | Sequencer | Overview | `#seqOver`, `#overRow`, `#overMap` (canvas h104) | Overview strip; click/drag moves the view | 811, 830-833 | ymseq + gemui | state | U+V T-P106 |  |
| P107 | Sequencer | Grid | `#seqgrid` | Piano roll (built outside the slice) | 813 | ymseq + gemui | state | U+V T-P107 |  |
| P108 | Sequencer | Vertical scrolling | `#seqTr` | 23-108 (B0-C9), default 48; the mouse wheel does the same | 814 | ymseq + gemui | state | U+V T-P108 |  |
| P109 | Sequencer | Horizontal scrolling | `#seqScroll`, `#seqScrollv`, `#seqScrollRow` | For a pattern > grid; readout "1-64" | 816-819, 2717-2726 | ymseq + gemui | state | U+V T-P109 |  |
| P110 | Sequencer | Transport bar | `#seqTbar` | Transport (content outside the slice) | 822 | ymseq + gemui | state | U+V T-P110 | Adaptation: Host sync / Free run (§4) |
| P111 | Timing | Metronome | `#seqMetro` | Click on each beat, bell on the 1st beat; M key | 823-824, 2994 | ymseq | state | U+V T-P111 |  |
| P112 | Patterns/Song | Banks / patterns | `#patBanks`, `#patSlots` | Banks (Shift+1-4) and 8 slots (1-8); queued change applied at step 0 (patQueue) | 825-828, 1306-1318, 2078-2081, 3009-3015 | ymseq | state | U+V T-P112 |  |
| P113 | Sequencer | Hint | `#seqHint` | Contextual help | 834 | ymseq + gemui | state | U+V T-P113 |  |
| P114 | Sequencer | Step engine | `seqTick` | step=(step+1)%len; stepSamp = SR·60/(bpm·4)·swing (or 6·spt with MIDI); lanes: 4·chips (3ch) or 16 | 1796-1805 | ymseq + gemui | state | U+V T-P114 |  |
| P115 | Sequencer | Internal clock | `process` | phase += bpm/60·spb/SR; threshold = swing duration | 2222-2229 | ymseq + gemui | state | U+V T-P115 |  |
| P116 | Sequencer | Polymeter | `laneStep` | st = stepAbs mod plen | 1901-1902 | ymseq + gemui | state | U+V T-P116 |  |
| P117 | Sequencer | Chord voices | `polyTrigger` etc. | Voices 2-6 (pat2..pat6, dur2..dur6) per step, 16ch only, not in mono; drums: choke if mono | 1828-1896 | ymseq + gemui | state | U+V T-P117 |  |
| P118 | Step rows | NOTE/OCT | lane.pat, lane.oct | pat 0-12 relative + octave code (0 = channel octave, -1 = oct 0); absolute = (oct+1)·12+v | 2595, 2624-2634, 2879 | ymseq | state | U+V T-P118 |  |
| P119 | Step rows | TIE | lane.tie | Tie; note duration (figures) | 1972-1976, 2036 | ymseq | state | U+V T-P119 |  |
| P120 | Step rows | SLD | lane.slide | Slide to the next note of the track (VBL rate in 3ch, VBL·16 in 16ch) | 1761-1793, 2035 | ymseq | state | U+V T-P120 |  |
| P121 | Step rows | CRV | lane.slc | Slide curve: 0 Linear, 1 Exponential, 2 Logarithmic, 3 S-curve, 4 Stepped, 5 Sawtooth, 6 Wobble, 7 Chromatic, 8 Scale-locked, 9 Overshoot, 10 Bounce, 11 Random walk, 12 Custom; a change in the middle of a slide = reshape | 1678-1743, 1958-1961 | ymseq | state | U+V T-P121 |  |
| P122 | Step rows | ARP | lane.afl | Step played through the channel's arp; ARP=0 on a tie suspends the arp | 1963-1967, 2019-2024 | ymseq | state | U+V T-P122 |  |
| P123 | Step rows | VOL | lane.vol | 0-15, default 15; accent = level·vol/15 | 1926, 2597 | ymseq | state | U+V T-P123 |  |
| P124 | Step rows | MIX | lane.mix | 0 = channel, 1 Tone, 2 Noise, 3 Tone+Noise (no effect on drum/sync) | 1486-1497 | ymseq | state | U+V T-P124 |  |
| P125 | Step rows | FX | lane.fx | 0 channel, 1 Chip (off), 2 SID, 3 Sync, 4 Chipdrum; switches without retrigger on a tie | 1499-1513, 2017-2018 | ymseq | state | U+V T-P125 |  |
| P126 | Step rows | GAT | lane.gat | Step gate in % (0 = channel gate) | 1952-1953 | ymseq | state | U+V T-P126 |  |
| P127 | Step rows | PAN | lane.pnr | Pan per step: (v-101)/100, 0 = mixer default | 1921-1922 | ymseq | state | U+V T-P127 |  |
| P128 | Step rows | RTG | lane.rtg | Retrig: rtg+1 hits spread over the step, level ×0.92 | 1913-1920, 2236-2240 | ymseq | state | U+V T-P128 |  |
| P129 | Step rows | CND | lane.cnd | Conditions: 1 = 1:2, 2 = 2:2, 3 = 1:4, 4 = 4:4, 5 = 25 %, 6 = 50 %, 7 = 75 % | 1815-1826, 1908-1912 | ymseq | state | U+V T-P129 |  |
| P130 | Step rows | CUT | lane.cut | Cuts the playing sample (drums) | 1930-1933 | ymseq | state | U+V T-P130 |  |
| P131 | Step rows | SLOT | lane.slt → slotRow | Drum slot (sent to the engine) | 2598, 2882 | ymseq | state | U+V T-P131 |  |
| P132 | Step rows | TON | lane.ton | Tuning of the drums in semitones | 2601, 1935-1936 | ymseq | state | U+V T-P132 |  |
| P133 | Patterns/Song | Song window | `#songWin`, `#songBar`, `#songClose`, `#songTbar` | Chain of patterns | 594-602 | ymseq | state | U+V T-P133 |  |
| P134 | Patterns/Song | Add | `#songAdd` | Adds the current pattern; again = +1 repeat; Shift = new entry | 603-607 | ymseq | state | U+V T-P134 |  |
| P135 | Patterns/Song | Bank/pattern selection | `#songBanks`, `#songSlots` | Choice of the pattern to add | 609-614 | ymseq | state | U+V T-P135 |  |
| P136 | Patterns/Song | List | `#songList` | Click selects (Shift = several), right-click = actions | 616, 606 | ymseq | state | U+V T-P136 |  |
| P137 | Keyboard | Song keys | hint | Left/Right move, Ins/A add, Del delete, D duplicate, +/- repeats, X/C/V cut/copy/paste, Home/End | 617-618 | gemui + plugin | UI | V+M T-P137 |  |
| P138 | Patterns/Song | Chaining | handler `st` | End of pattern: songOnPa/songOnWrap; without loop (loopOn=0) and without song → stop | 3016-3021 | ymseq | state | U+V T-P138 |  |
| P139 | Sequencer | Slide Curve window | `#curveWin`, `#curveClose`, `#curveTrk`, `#curvegrid`, `#curveWarn` | Drawing of the Custom curve (CRV); drag to draw | 621-638 | ymseq + gemui | state | U+V T-P139 |  |
| P140 | Sequencer | Linear / Smooth / Invert | `#curveReset`, `#curveSmooth`, `#curveInvert` | Resets to a straight line, smooths, mirrors accel./decel. | 628-630 | ymseq + gemui | state | U+V T-P140 |  |
| P141 | Keyboard | Computer keyboard window | `#kbdWin`, `#kbdClose`, `#kbdLay`, `#tkl`, `#tklnum`; `#kbdMapBtn` | Clickable keyboard; typing plays only while the window is open; legend note/sharp/octave/transport | 640-668, 1082-1083 | gemui + plugin | UI | V+M T-P141 |  |
| P142 | Keyboard | Computer keyboard octave | `#kbdOctDn` (Z), `#kbdOctUp` (X), `#kbdOctV` | Default "Oct 3" | 646-652 | gemui + plugin | UI | V+M T-P142 |  |
| P143 | Keyboard | Held notes | `#kbdHeldSel` "Keep", `#labHeld` | Behaviour of the held notes on an octave change (kbdFollow 0/1) | 653-654, 2619 | gemui + plugin | UI | V+M T-P143 |  |
| P144 | Keyboard | Range | `#kbdRange` | Display of the played range | 655 | gemui + plugin | UI | V+M T-P144 |  |
| P145 | Keyboard | Virtual keyboard | `#kbd` | On-screen piano | 1066 | gemui + plugin | UI | V+M T-P145 |  |
| P146 | Keyboard | Pads | `#pads` (duplicated) | Hidden; **id `pads` defined twice** | 1067-1069 | gemui + plugin | UI | V+M T-P146 |  |
| P147 | Keyboard | Latch | `#kbLatch` | Keys held until pressed again | 1072-1073 | gemui + plugin | UI | V+M T-P147 |  |
| P148 | Keyboard | Layer | `#kbLayer` | Plays all the active tracks instead of the focused one (msg kbmode) | 1074, 1440-1452, 2101-2104 | gemui + plugin | UI | V+M T-P148 |  |
| P149 | Keyboard | Virtual keyboard octave | `#octDown` (Z), `#octUp` (X), `#octv` | Default "Oct 4" | 1075-1079 | gemui + plugin | UI | V+M T-P149 |  |
| P150 | Keyboard | Global shortcuts | hint | Space Play/Stop, 1-8 Pattern, Shift+1-4 Bank, ←→ channel, M metro, Ctrl+Z/Y, Esc Panic | 1085 | gemui + plugin | UI | V+M T-P150 |  |
| P151 | Keyboard | Window shortcuts | titles | Ctrl+M Mixer, Ctrl+B Arp, Ctrl+R Modulation, Ctrl+E Sequencer, Ctrl+P Patches, Ctrl+K Drum Kit, Ctrl+C → Chipdrum | 1055-1059, 869, 1000 | gemui + plugin | UI | V+M T-P151 |  |
| P152 | Keyboard | Velocity | `trigger` | level = clamp(vel/127, 0.05, 1) × channel level | 1444 | gemui + plugin | UI | V+M T-P152 |  |
| P153 | Keyboard | Preview of a written note | `previewNote` | noteOn vel 110, cut after 240 ms; drums through seqprev | 2727-2743 | gemui + plugin | UI | V+M T-P153 |  |
| P154 | Mixer | Window | `#mixWin`, `#mixBar`, `#mixClose`, `#openMixBtn` (Ctrl+M) | Floating mixer | 581-593, 3179-3189 | ymengine + gemui | state | U+V T-P154 |  |
| P155 | Mixer | Channel strip | `buildMixer` | Label button (click = channel focus), mini-scope 26×18, fader 0-100 (default 100, = level), VU, pan -100..100 (double-click = 0), M, S; 3ch: fader disabled, no pan, voice names | 3066-3112 | ymengine + gemui | track[n].level / pan / mute / solo | U+V T-P155 |  |
| P156 | Mixer | Mute/Solo | `audible`, `sendMix` | mgain = !mute && (!anySolo || solo) | 3057-3064 | ymengine + gemui | track[n].mute / solo | U+V T-P156 |  |
| P157 | Mixer | VU meters | `vuPct`, `drawMeters` | 48 dB scale; the held peak decays 2.5 %/update; clip ≥ 0.999 | 3131-3163 | ymengine + gemui | state | U+V T-P157 |  |
| P158 | Mixer | Outputs | `#outL`/`#outLpk`/`#vuhL`, `#outR`/`#outRpk`/`#vuhR`, `#outLab`, `#outRrow` | Out L/R; 3ch: mono "Out" | 588-591, 3174-3178 | ymengine + gemui | state | U+V T-P158 |  |
| P159 | Mixer | Mixer undo | `pushMixUndo` | One step per drag / M/S click | 3091, 3099, 3107-3108 | ymengine + gemui | state | U+V T-P159 |  |
| P160 | Patches/Presets | Patches window | `#libWin`, `#libClose`, `#openLibBtn` (Ctrl+P) | Patch library | 690-706, 1059 | ymio | state | S+V T-P160 |  |
| P161 | Patches/Presets | Save current | `#libSave` | Saves the sound of the focused channel | 697 | ymio | state | S+V T-P161 |  |
| P162 | Patches/Presets | List | `#libList`, `#libHint` | Click = loads into the focused channel; right-click = actions | 700-704 | ymio | state | S+V T-P162 |  |
| P163 | Import/Export | Library import/export | `#libImport`, `#libExport`, `#patchLibFile` (.json) | Reads/writes the patch library | 698-699, 707 | ymio | state | G+S T-P163 |  |
| P164 | Import/Export | Preset/pattern/song files | `#presetFile`, `#patternFile`, `#songFile` (.json) | Opens a preset, pattern, song | 901-903 | ymio | state | G+S T-P164 |  |
| P165 | Import/Export | MIDI file | `#midiInFile` (.mid/.midi) | MIDI import | 853 | ymio | state | G+S T-P165 |  |
| P166 | Import/Export | YM file | `#ymFile` (.ym,.ym2,.ym3,.ym5,.ym6,.snd,.sndh) | Import of a YM tune | 852 | ymio | state | G+S T-P166 |  |
| P167 | Import/Export | YM Player | `#ymWin`, `#ymClose`, `#ymName`, `#ymPlay`, `#ymRew`, `#ymPos` | Plays a register stream at the VBL rate, loops by default; Play stops man/seq; position sent every 8 frames | 837-851, 2085-2096, 2485-2524 | ymio | state | G+S T-P167 |  |
| P168 | Control/Monitor | Shortcuts window | `#helpWin`, `#helpClose`, `#helpBody` | List of the shortcuts | 854-862 | gemui + ymengine | UI | V+U T-P168 |  |
| P169 | Control/Monitor | Voices (System) | `#modeSel` | 16 ch (pan + polyphony) / 3 ch (authentic YM, mono) | 1035-1038 | gemui + ymengine | global.voiceMode | V+U T-P169 |  |
| P170 | MIDI | Clock | `#clkSel`, `#clkStatus` | Internal / MIDI; status SYNC/STOP | 1039-1043, 3026-3029 | plugin | state | U+M T-P170 |  |
| P171 | Control/Monitor | Master | `#master` / `#masterv` | 0-100, default 70 | 1044-1045 | gemui + ymengine | global.master | V+U T-P171 |  |
| P172 | Control/Monitor | Voice LEDs | `#voices` | One LED per active audible channel | 1046, 2536-2544, 2982 | gemui + ymengine | UI | V+U T-P172 |  |
| P173 | Control/Monitor | Note | `#tpread` | "TP=xxx -> f Hz" (f = 125000/TP) | 1047, 2980 | gemui + ymengine | UI | V+U T-P173 |  |
| P174 | Control/Monitor | Oscilloscope | `#scope` (920×90) | Main scope (drawBigScope) | 1062 | gemui + ymengine | UI | V+U T-P174 |  |
| P175 | Control/Monitor | Registers | `#regs` | Strip of the YM registers | 1064 | gemui + ymengine | UI | V+U T-P175 |  |
| P176 | Control/Monitor | Telemetry | `process` | 20 Hz: LEDs, peaks of the 16 channels + output, env, TP, step, scope 16×128 (decimation 8) | 2532-2570 | gemui + ymengine | UI | V+U T-P176 |  |
| P177 | Windows/UI | Button bar | `#winBtnBar` | Mixer, Arpeggiator, Modulation, Step Sequencer, Patches | 1054-1060 | gemui | UI | V T-P177 |  |
| P178 | Windows/UI | Floating windows | `topFloat`, `focusWin`, `syncWinBtns` | Z order (winOrder), state of the buttons | 2710-2716 | gemui | UI | V T-P178 |  |
| P179 | Editing/Undo | Patch undo step | `armCur`, `pushCur` | Snapshot on pointerdown/keydown in main/orn/arp; stack of 50; redo cleared | 2913-2934 | ymseq | state | U+V T-P179 |  |
| P180 | Editing/Undo | Rectangular selection | `rectSel`, `rectNotes`, `rectMove`, `rectClear` | Move/clear a step×pitch block (cancelled if outside the pattern) | 2637-2703 | ymseq | state | U+V T-P180 |  |
| P181 | Editing/Undo | Note-by-note selection | `noteSel`, `selToggle` | Shift+click | 2639-2655 | ymseq | state | U+V T-P181 |  |
| P182 | Editing/Undo | Block selection / right-click | `selA/selB`, `blockClip`, `rightSel` | Step selection, clipboard | 2636-2638, 2708-2709 | ymseq | state | U+V T-P182 |  |
| P183 | Sequencer | Track arming | `armChan` | The first placed note enables the channel | 2805-2809 | ymseq + gemui | state | U+V T-P183 |  |
| P184 | Audio engine | Startup | `boot` | AudioWorklet (Blob), else ScriptProcessor 2048; Analyser fft 2048; sends patch/arp/chans/seq/clk; applyTips; initMIDI | 2938-3039 | ymcore + ymengine | state | G+U T-P184 | Adaptation: no "Run program" screen in the plugin; kept in Standalone (DECISIONS D-020) |
| P185 | Audio engine | Voices | worklet | 16 channels, 112 voices (16 + 16·5 poly + 16 manual pool); voice stealing | 1107-1111, 1422-1438 | ymcore + ymengine | state | G+U T-P185 |  |
| P186 | Audio engine | 3ch mode / 2 chips | `chipOf`, `laneVoice` | 3 channels per chip + Digidrum track (4 tracks/chip); chips=1|2; noise/envelope shared per chip | 1417-1421, 1515-1524, 2295-2313 | ymcore + ymengine | state | G+U T-P186 |  |
| P187 | Audio engine | Pitch bend | msg `bend` | Semitones added to all the voices | 2111-2114, 1354 | ymcore + ymengine | state | G+U T-P187 |  |
| P188 | Audio engine | Panic | msg `allOff` | 64-sample fade of all the voices | 2127-2130 | ymcore + ymengine | state | G+U T-P188 |  |
| P189 | MIDI | MIDI clock | msgs `mclk`, `mstart`, `mcont`, `mstop`, `mspp` | 24 ppqn; seq tick every 6 clocks; spt smoothed 0.9/0.1; ext BPM = 60·SR/(spt·24); SPP | 2167-2207, 2564 | plugin | state | U+M T-P189 |  |
| P190 | Other | Model constants | `NCH=16`, `NSTEP=256`, `POLYMAX=6` | Limits | 2580-2582 | plugin | state | M T-P190 |  |
| P191 | Other | Global defaults | `GLOB` | master 0.7, mode3ch 0, chips 1, bpm 120, gate 0.5, steps 16, digiCh 2, digiCh2 2, vbl 50, sig 2, reso 2; CLK {midi 0, arpDiv 6} | 2614-2616 | plugin | state | M T-P191 |  |
| P192 | Chipdrum | Factory kit banks | `KIT_BANKS` | 13 banks: Classic (=KIT_DEF), TR-606, TR-909-ish, Hard EBM, Industrial, Lo-Fi 4bit, Chiptune, TR-808, House, Techno, Hip-Hop, Trap, Drum'n'Bass. Each one sets the 8 instruments | 3393-3455 | ymengine | state | G+V T-P192 |  |
| P193 | Chipdrum | Parameters per instrument (reminder, defined before the slice) | `KIT`, `KITSPEC` | kick/tom/tom2: pitch, sweep, dec, noise, vol; snare: body, tone, dec, vol; clap: space, color, tail, vol; hat/ohat/chat: metal, tune, dec, vol. Ranges in KITSPEC (e.g. kick pitch 25-90, sweep 20-300, dec 4-20; snare body 80-400; chat dec 30-250; vol 0-100) | 3371-3392 | ymengine | state | G+V T-P193 |  |
| P194 | Chipdrum | Instrument → slot / note mapping | `KIT_SLOTS` | kick 0, snare 1, clap 2, hat 3, ohat 4, chat 5, tom 6, tom2 7. On a Chipdrum track: C, C#, D, D#, E, F, F#, G (modulo 8) | 3456, 3556-3561, 5146 | ymengine | state | G+V T-P194 |  |
| P195 | Chipdrum | Sending the kit to the engine | `sendKit()` | message `{type:'kit', kit}` (deep copy) on each change | 3458 | ymengine | state | G+V T-P195 |  |
| P196 | Chipdrum | Parameter tooltips | `KITHELP` | 12 help texts (pitch, sweep, dec, noise, body, tone, space, color, tail, metal, tune, vol) + range "(mn to mx)" | 3459-3472, 3483 | ymengine | state | G+V T-P196 |  |
| P197 | Chipdrum | Kit sliders | `buildKitSliders()` (`#kitSliders`) | One range per parameter of the current instrument; one undo step per drag (pointerdown arms `pushKitUndo`); updates the kit name and sends | 3475-3499 | ymengine | state | G+V T-P197 |  |
| P198 | Chipdrum | Direct entry of a value | span `.val.editable` → `input.valedit` | Click = text field; Enter confirms, Esc cancels, blur confirms; comma accepted; rounded + clamped [mn,mx] | 3500-3527 | ymengine | state | G+V T-P198 |  |
| P199 | Chipdrum | Detected kit name | `kitLabel()`, `refreshKitName()` (`#kitName`, `#genKitName`) | Compares KIT with each bank; otherwise "Custom". Shown as "Kit: name" | 3531-3548 | ymengine | state | G+V T-P199 |  |
| P200 | Chipdrum | Instrument selector | `#kitInst` (8 buttons) | Kick, Snare, Clap, Hat, Open Hat, Closed Hat, Tom 1, Tom 2; default `kick` | 3457, 3554-3565 | ymengine | state | G+V T-P200 |  |
| P201 | Chipdrum | Kit enabled by generator | `refreshKitEnable()` (`#kitBody`, `#kitOff`) | Kit inert (greyed) + message if the current effect ≠ 4 (Chipdrum) | 3566-3572 | ymengine | state | G+V T-P201 |  |
| P202 | Chipdrum | Open / close the Kit window | `openKit()`, `closeKit()`, `#kitClose` | Shows `#kitWin`, focus | 3573-3578 | ymengine | state | G+V T-P202 |  |
| P203 | Chipdrum | Audition | `#kitPlay` | message `{type:'drumprev', slot}` | 3579 | ymengine | state | G+V T-P203 |  |
| P204 | Chipdrum | Reset instrument | `#kitReset` | Resets the current instrument to the KIT_DEF values (undo) | 3580-3584 | ymengine | state | G+V T-P204 |  |
| P205 | Chipdrum | Bank menu | `#kitBank` | GEM menu of the banks sorted alphabetically; applies the 8 instruments (undo) | 3585-3593 | ymengine | state | G+V T-P205 |  |
| P206 | Import/Export | Load a kit (.json) | `#kitFile` change | Requires `{kind:'drumkit', kit:{…}}`; merges per instrument; otherwise alert "Not a valid drum kit file." | 3854-3869 | ymio | state | G+S T-P206 |  |
| P207 | Windows/UI | Moving the windows | `mkWinDrag(winId,barId,closeId,detach)` | Drag the title bar; position clamped ≥0, compensated for the zoom `zScale()`; `detach` takes the window out of the flow on the 1st drag (main window) | 3596-3617, 3630-3635, 3707-3709, 3885 | gemui | UI | V T-P207 |  |
| P208 | Windows/UI | List of floating windows / Z stack | `FLOATWINS`, `focusWin()` | seqWin, arpWin, mixWin, songWin, ornWin, libWin, kitWin, helpWin, ymWin, curveWin, kbdWin (+mainWin). Click = to the front; z-index 70+k (under the modals 200) | 3618-3629 | gemui | UI | V T-P208 |  |
| P209 | Control/Monitor | Oscilloscope per track | `openScope(i)`, `closeScope(i)` | "SCOPE - name" window 360×140 per track, several side by side, positions offset by 24 px; opened by a click on a `.mscope` of the mixer | 3637-3681 | gemui + ymengine | UI | V+U T-P209 |  |
| P210 | Control/Monitor | Drawing of the big scope | `drawBigScope(scope, spos)` | Green trace #0a0, width 2, y = H/2 − smp·H·0.9, circular buffer | 3690-3706 | gemui + ymengine | UI | V+U T-P210 |  |
| P211 | Windows/UI | Window titles follow the track | `refreshWinTitles()`, `refreshScopeTitle()` | "- name" in arpTrk/ornTrk; seqTrk adds "[generator]" | 3682-3689 | gemui | UI | V T-P211 |  |
| P212 | Windows/UI | Help (shortcuts) | `HELP`, `buildHelp()`, `openHelp()`, `#helpClose` | 9 sections: Windows, Generator, File, Transport and edit, Writing notes, Sequencer rows, Sequencer view, Song window, Mouse | 3710-3776 | gemui | UI | V T-P212 |  |
| P213 | Keyboard | Window shortcuts (doc) | `HELP` | ^E Sequencer, ^B Arpeggiator, ^M Mixer, ^G Song, ^R Modulation, ^P Patches, ^K Drum kit; Esc closes the top window, then panic | 3711-3714 | gemui + plugin | UI | V+M T-P213 |  |
| P214 | Keyboard | Generator shortcuts (doc) | `HELP`, `FX_KEYS` | ^V Chip Voice, ^S SID Voice, ^U Sync Buzzer, ^C Chipdrum, ^I Digidrum | 3715-3717, 4436-4437 | gemui + plugin | UI | V+M T-P214 |  |
| P215 | Keyboard | File shortcuts (doc) | `HELP` | ^D demo, ^Q quit, ^? help | 3718 | gemui + plugin | UI | V+M T-P215 |  |
| P216 | Keyboard | Transport / editing (doc) | `HELP` | Space play/stop; ^Z/^Y undo/redo; 1-8 pattern slot; arrows cursor (Shift+←/→: channel); Enter places/removes; Del clears the step; Z/X octave −/+; M mutes the focused channel | 3719-3724 | gemui + plugin | UI | V+M T-P216 |  |
| P217 | Keyboard | Song window (doc) | `HELP` | ←/→ cursor, Home/End, Ins or A insert, Del delete, D duplicate, +/− repeats, X/C/V cut/copy/paste, Enter jump to the block | 3749-3757 | gemui + plugin | UI | V+M T-P217 |  |
| P218 | Windows/UI | Stateless open buttons | `syncWinBtns()` | openArpBtn, openOrnBtn, openSeqBtn, openKitBtn, openMixBtn, openLibBtn never "on" | 3777-3784 | gemui | UI | V T-P218 |  |
| P219 | Windows/UI | Open/close Arp and Sequencer | `openArpWin`, `closeArpWin`, `openSeqWin`, `closeSeqWin` | openSeqWin recomputes the vertical slider `fitTrSlider` | 3785-3794 | gemui | UI | V T-P219 |  |
| P220 | Patches/Presets | Patch library | `patchLib`, `saveLib()` | Stored in localStorage `prismYmPatchLib` (JSON) | 3797-3799 | ymio | state | S+V T-P220 |  |
| P221 | Patches/Presets | Patch snapshot | `libSnap(i)` | `{patch, arp:{…,table}, level, gate}` | 3800-3805 | ymio | state | S+V T-P221 |  |
| P222 | Patches/Presets | Apply a patch | `libApply(entry,i)` | Copies the patch, normalises the modulation tables (`ornNorm`), arp table padded to 16, level default 0.75, gate default 0.5 | 3806-3815 | ymio | state | S+V T-P222 |  |
| P223 | Patches/Presets | Patch list (click) | `refreshLib()` (`#libList`, `#libHint`) | One button per patch; click = loads onto the current track | 3816-3826 | ymio | state | S+V T-P223 |  |
| P224 | Patches/Presets | Patch context menu | right-click on a patch button | Load, Rename (prompt), Overwrite (with the current track), Delete | 3826-3834 | ymio | state | S+V T-P224 |  |
| P225 | Patches/Presets | Save a patch | `#libSave` | Prompt "Patch name:", default "Patch N" | 3842-3848 | ymio | state | S+V T-P225 |  |
| P226 | Import/Export | Export the library | `#libExport` | File `prism-ym2149-patches.json`: `{kind:'patchlib', patches:[…]}` | 3849-3852 | ymio | state | G+S T-P226 |  |
| P227 | Import/Export | Import a library | `#libImport` → `#patchLibFile` | Requires `kind:'patchlib'` + array; appends; otherwise alert | 3853, 3870-3884 | ymio | state | G+S T-P227 |  |
| P228 | Windows/UI | Open/close Patches | `openLib()`, `closeLib()`, `#openLibBtn`, `#libClose` | — | 3838-3841 | gemui | UI | V T-P228 |  |
| P229 | Tracks | Track tabs | `#chtabs`, `chTabs` | NCH buttons; click = focus; another click on the focused tab = enable/disable the track (enabled) | 3911-3936 | ymseq + gemui | state | S+V T-P229 |  |
| P230 | Tracks | Tab context menu | right-click on a tab | Copy settings, Paste settings, Rename... (disabled in 3ch: "The three YM voices are named by the chip") | 3920-3930 | ymseq + gemui | state | S+V T-P230 |  |
| P231 | Tracks | Copy / paste track settings | `chanCopy(i)`, `chanPaste(i)` | Copies patch + tables ornTable/ornNse/ornVol + arp + level + gate | 3889-3910 | ymseq + gemui | state | S+V T-P231 |  |
| P232 | Tracks | All on / All off | buttons created in `#chtabs` | `setAllEnabled(1/0)` on NCH tracks (4 in 3ch) | 3938-3953 | ymseq + gemui | state | S+V T-P232 |  |
| P233 | Tracks | Rename a track | `renameTrack(i)` | Prompt, 8 characters max, empty = number; not allowed in 3ch | 3954-3961 | ymseq + gemui | state | S+V T-P233 |  |
| P234 | Tracks | Shown name | `trackName()`, `tabTitle()`, `syncTabs()` | 3ch: A,B,C,Digi (or A1..Digi2 with two chips); tracks ≥4 unused in 3ch | 3962-3982 | ymseq + gemui | state | S+V T-P234 |  |
| P235 | Tracks | Focus on a track | `focusCh(i)` / `selectCh` | Sends `{type:'kbmode', focus, layer}` and `{type:'focus', i}`; refreshes the UI, mixer, modulation | 3983-3997 | ymseq + gemui | state | S+V T-P235 |  |
| P236 | Tracks | Labels in 3ch mode | `retabs()` | Tabs A/B/C/Digi; >NVOICES greyed; if cur≥4 → focus 0 | 3998-4007 | ymseq + gemui | state | S+V T-P236 |  |
| P237 | Windows/UI | Buttons release the focus | listener pointerup | A focused BUTTON gets `blur()` (avoids capturing Enter/Space/arrows) | 4011-4015 | gemui | UI | V T-P237 |  |
| P238 | Source | Tone / noise source | `#srcSel`, `SRC_NAMES`, `srcIdx()` | Tone / Noise / Tone+Noise → toneEn/noiseEn | 4017-4021, 4053-4059 | ymengine | track[n].source | U+V T-P238 |  |
| P239 | Tracks | Voicing poly/mono | `#polySel`, `#seqPolySel`, `pickVoicing()`, `refreshPolyBtn()` | Polyphonic / Monophonic (P().mono); mono cancels the chord; disabled in 3ch; poly up to POLYMAX voices; melodic mono = glide on legato; drum mono = choke | 4022-4052 | ymseq + gemui | state | S+V T-P239 |  |
| P240 | Source | Volume mode | `#vmSel`, `VM_NAMES` | Vol 4-bit / Envelope (P().volMode) | 4060-4065 | ymengine | track[n].volMode | U+V T-P240 |  |
| P241 | Audio engine | Chip mode | `#modeSel`, `MODE_NAMES`, `setMode3()` | 16 ch / Authentic 3ch / Dual chip 2x3ch. 3ch: track 4 of each chip forced to Digidrum (silent without a sample), hardware channels that were Digidrum → Chip Voice; saves/restores the previous tracks (`ch3Saved`); LEDs > nv greyed; sends allOff | 4066, 4080-4120 | ymcore + ymengine | global.voiceMode | G+U T-P241 |  |
| P242 | Tracks | 3ch channel names | `VOICE_NAMES`, `VOICE_LONG`, `isDigiTrack`, `NVOICES` | 1 chip: A,B,C,Digi; 2 chips: A1,B1,C1,Digi1,A2,B2,C2,Digi2; NVOICES = 4×chips in 3ch, otherwise NCH | 4066, 4072-4079 | ymseq + gemui | state | S+V T-P242 |  |
| P243 | Windows/UI | GEM submenu arrow | `GEM_ARROW` | 8×8 pixel-art SVG | 4067-4071 | gemui | UI | V T-P243 |  |
| P244 | Envelope | Shared envelope label | `#envShared` | "(shared)" in 3ch, "(per channel)" otherwise | 4093 | ymcore + ymengine | state | U+V T-P244 |  |
| P245 | Source | Noise period | `#np` | 1-31, default 8; in 3ch, applied to all the tracks (a single noise generator) | 4126-4130 (HTML 939) | ymengine | track[n].noisePeriod | U+V T-P245 |  |
| P246 | Source | 4-bit volume | `#vol` | 0-15, default 13 (P().vol4) | 4131 (HTML 941) | ymengine | track[n].vol4 | U+V T-P246 |  |
| P247 | Generators | SID: floor | `#vmin` | 0-15, default 0 (P().sidVmin) | 4132 (HTML 997) | ymengine | track[n].sidVmin | U+V T-P247 |  |
| P248 | Digidrum | Playback speed | `#ratio` | 25-800 → ×0.25-×8.00, default 1.00 (P().sidRatio = v/100) | 4133 (HTML 994) | ymengine + ymio | track[n].sidRatio | U+V T-P248 |  |
| P249 | Envelope | Envelope frequency | `#ef`, `efToHz`, `hzToEf` | Logarithmic slider 0-4000, default 1840; EP = round(2e6/(256·32·Hz)) clamped to 1-65535; display in Hz (1 decimal) and EP in decimal + hex `$XXXX` | 4134-4146 (HTML 953) | ymcore + ymengine | track[n].envPeriod | U+V T-P249 |  |
| P250 | Envelope | Envelope shapes | `#shapeSel`, `SHAPE_ITEMS` | GEM menu of 16 entries 0-F with an SVG icon (P().shape) | 4423-4431 | ymcore + ymengine | track[n].envShape | U+V T-P250 |  |
| P251 | Sequencer | Slide curves | `SLIDE_CURVES`, `SLIDE_SHORT`, `SLIDE_SEPS` | 13: Linear, Exponential, Logarithmic, S-curve, Stepped, Sawtooth, Wobble, Chromatic, Scale-locked, Overshoot, Bounce, Random walk, Custom (LIN…CUS); separators before 4,7,9,12 | 4147-4153 | ymseq + gemui | state | U+V T-P251 |  |
| P252 | Sequencer | Custom curve editor | `#curvegrid`, `buildCurveGrid()`, `setCurve()` | 32 points 0-1 (default linear i/31); painted with the mouse (left click + hover) | 4154-4184 | ymseq + gemui | state | U+V T-P252 |  |
| P253 | Sequencer | Curve warning | `refreshCurveWarn()` (`#curveWarn`) | If the last point ≠1 (±0.02): "Ends at N% of the interval…" | 4185-4194 | ymseq + gemui | state | U+V T-P253 |  |
| P254 | Sequencer | Curve Reset / Smooth / Invert | `#curveReset`, `#curveSmooth`, `#curveInvert` | Linear reset; smoothing (t[i-1]+2t[i]+t[i+1])/4; inversion 1−t[31−i] | 4200-4214 | ymseq + gemui | state | U+V T-P254 |  |
| P255 | Windows/UI | Curve window | `openCurveWin()`, `#curveClose` | Also opened by choosing Custom in CRV | 4215-4219, 6175 | gemui | UI | V T-P255 |  |
| P256 | Vibrato/ADSR | Vibrato depth | `#vibDepth` | 0-200 cents, default 0 | 4220-4224 (HTML 963) | ymengine | track[n].vibDepth | U+V T-P256 |  |
| P257 | Vibrato/ADSR | Vibrato rate | `#vibRate` | 1-20 Hz step 0.5, default 6 | 4225-4229 (HTML 965) | ymengine | track[n].vibRate | U+V T-P257 |  |
| P258 | Timing | VBL rate | `VBLHZ()`, `msPerTick()` | P().vbl ‖ GLOB.vbl ‖ 50 Hz; ms/tick = 1000/VBL | 4230-4231 | ymseq | global.vblRate | U+V T-P258 |  |
| P259 | Vibrato/ADSR | ADSR mode | `#adsrMode`, `ADSR_MODES` | Smooth / Chip (P().adsrChip); Chip forced in 3ch | 4384-4392 | ymengine | state | U+V T-P259 |  |
| P260 | Vibrato/ADSR | A/D/S/R sliders | `#atk #dcy #sus #rel`, `adsrRange/Unit/Shown/Store` | Smooth: A,D 0-1000 ms, S 0-100 %, R 0-2000 ms (defaults 0/0/100/0). Chip: A,D,R 0-64 VBL ticks, S 0-15; conversion ticks = round(ms/msPerTick), S = round(%/100·15) | 4232-4251, 4365-4410 | ymengine | track[n].attack / decay / sustain / release | U+V T-P260 |  |
| P261 | Vibrato/ADSR | Direct ADSR entry | `makeEditable(k+'v')` | Value clamped in the unit of the mode | 4400-4409 | ymengine | state | U+V T-P261 |  |
| P262 | Vibrato/ADSR | ADSR curve | `drawAdsrCurve()` (`#adsrCurve`) | Real time axis A+D+400 ms (conventional hold)+R; Chip mode in steps of 16 levels; green dot of the live position (adsrLive st 1-4); greyed for Sync Buzzer in chip mode | 4252-4344 | ymengine | state | U+V T-P262 |  |
| P263 | Vibrato/ADSR | ADSR inactive on Sync Buzzer | `refreshAdsrEnable()` | Effect 3 + chip mode: sliders disabled, hint "buzzer: envelope only" | 4345-4364 | ymengine | state | U+V T-P263 |  |
| P264 | Mixer | Track level | `#chLevel` | 0-100, default 75 (CH.level = v/100), syncs the mixer fader | 4411-4416 (HTML 946) | ymengine + gemui | track[n].level | U+V T-P264 |  |
| P265 | Mixer | Master | `#master` | 0-100, default 70 (GLOB.master = v/100) | 4417-4421 (HTML 1045) | ymengine + gemui | global.master | U+V T-P265 |  |
| P266 | Generators | Generator choice | `#fxSel`, `FXNAMES`, `FX_ORDER`, `setEffect()`, `fxAllowed()` | Chip Voice(0), SID Voice(1), Sync Buzzer(3), --- , Chipdrum(4), Digidrum(2); in 3ch Digidrum only on the Digi track and the other way round; a change = manual panic; drums cut the arp; Sync Buzzer: EP>100 → EP=1 | 4433-4477 | ymengine | state | U+V T-P266 |  |
| P267 | Keyboard | Manual play panic | `manPanic()` | Clears the held keys, pads; `{type:'manOff'}` | 4438-4445 | gemui + plugin | UI | V+M T-P267 |  |
| P268 | Digidrum | 8 sample slots | `#drumSlots`, `userDrums` | Active slot = P().slot (0-7) | 4480-4488 | ymengine + ymio | state | U+V T-P268 |  |
| P269 | Digidrum | Load a sample | `#drumwave` click → `#drumFile` | Requires the audio to be started; decodeAudioData, channel 0, linear resampling to 7800 Hz, 4-bit quantisation, max 3 s (23400 samples); `{type:'drum', slot, data}` | 4489-4517 | ymengine + ymio | state | U+V T-P269 |  |
| P270 | Digidrum | Waveform display | `drawDrum()` | 4-bit steps; text "slot N: n smp @ 7800 Hz / 4-bit" or "built-in DrumNoise" | 4518-4539 | ymengine + ymio | state | U+V T-P270 |  |
| P271 | Digidrum | Digidrum ST channel (3ch) | `#digiChSel` | A/B/C (A1.. or A2.. with two chips) → GLOB.digiCh / digiCh2 | 4651-4665 | ymengine + ymio | state | U+V T-P271 |  |
| P272 | Arpeggiator | On/Off | `#arpOn` | No effect on drums; off → allOff; on → sendHeld | 4542-4549 | ymengine | state | U+V T-P272 |  |
| P273 | Arpeggiator | Mode | `#arpModeSel`, `ARPMODES` | Up, Down, UpDn, Order, Random, Table | 4550-4555 | ymengine | state | U+V T-P273 |  |
| P274 | Arpeggiator | Fill with a chord | `#chordSel` | 39 chords → table + tlen (undo `armCur`) | 4640-4649 | ymengine | state | U+V T-P274 |  |
| P275 | Arpeggiator | Octaves | `#arpOcts` | 1, 2, 3 (default 1) | 4666-4668 (HTML 739-744) | ymengine | state | U+V T-P275 |  |
| P276 | Arpeggiator | Glide rate (tracking) | `#arpGlideRate` | toggles A().track | 4669-4673 | ymengine | state | U+V T-P276 |  |
| P277 | Arpeggiator | Land | `#arpLand` | toggles A().land | 4674-4678 | ymengine | state | U+V T-P277 |  |
| P278 | Arpeggiator | Rate | `#arpRate` | 4-200 Hz, default 50 | 4679-4683 (HTML 723) | ymengine | track[n].arpRate | U+V T-P278 |  |
| P279 | Arpeggiator | Table length | `#arpLen` | 1-16, default 3 | 4684-4688 (HTML 749) | ymengine | state | U+V T-P279 |  |
| P280 | Arpeggiator | Table grid | `#arpgrid`, `refreshArpGrid()`, `markArpStep()` | 16 columns × 16 rows (+0…+15 semitones); a click sets the value; columns ≥ tlen greyed; played step highlighted | 4690-4728 | ymengine | state | U+V T-P280 |  |
| P281 | Timing | Clock source | `#clkSel`, `CLKMODES`, `setClkMode()` | Internal / MIDI ext; MIDI: BPM disabled, "waiting for clock...", arp division row shown | 4730-4745 | ymseq | state | U+V T-P281 |  |
| P282 | MIDI | Arp division on MIDI clock | `#arpDivs` | 1/4(24), 1/8(12), 1/8T(8), 1/16(6, default), 1/16T(4), 1/32(3), Tick(1) MIDI ticks | 4746-4752 (HTML 729-736) | plugin | state | U+M T-P282 |  |
| P283 | Scales | Factory chords | `CHORDS` | 39: 6 triads, 8 sevenths, 5 sixths/add, 7 extensions, 6 inversions/voicings, 7 intervals | 4558-4604 | ymseq | state | U+V T-P283 |  |
| P284 | Sequencer | Chord on input | `#seqChord`, `refreshChordBtn()`, `chordVoices()` | Off + 39 chords; disabled for drums / 3ch / mono; voices = min(POLYMAX, number of intervals) | 4605-4639 | ymseq + gemui | state | U+V T-P284 |  |
| P285 | Sequencer | Note figures | `FIGS`, `figIcon()`, `figSteps()` | 13: Step, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4., 1/8., 1/16., 1/4T, 1/8T, 1/16T; duration in steps = round(beats × steps/beat), min 1 | 4755-4797 | ymseq + gemui | state | U+V T-P285 |  |
| P286 | Sequencer | Resolution | `RESOS` | 7: 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T (1,2,4,8,1.5,3,6 steps per quarter note) | 4798-4802 | ymseq + gemui | state | U+V T-P286 |  |
| P287 | Sequencer | Time signature | `SIGS`, `beatSteps()`, `barSteps()` | 12: 2/4,3/4,4/4,5/4,6/4,7/4,5/8,6/8,7/8,9/8,11/8,12/8 | 4803-4813 | ymseq + gemui | state | U+V T-P287 |  |
| P288 | Options/Menus | Note language | `NOTE_LANGS`, `setNoteLang()` | English (C D E) / Latin (Do Re Mi); localStorage `stymNoteLang` | 4814-4826, 4867-4871 | gemui | UI | V+S T-P288 |  |
| P289 | Options/Menus | Tooltips on/off | `setTips()`, `applyTips()`, MutationObserver | localStorage `stymTips`; off = title moved to data-tip, including elements created later | 4827-4866 | gemui | UI | V+S T-P289 |  |
| P290 | Scales | Factory scales | `SCALES` | Off + 25 scales (major/minor 4, modes 5, dark 5, reduced 6, symmetric 5) | 4872-4905 | ymseq | state | U+V T-P290 |  |
| P291 | Scales | Root / Snap | `scaleRoot`, `scaleName`, `scaleSnap`, `inScale()`, `snapToScale()` | Greys the rows outside the scale; Snap moves to the nearest degree (±6, downwards first) | 4906-4920 | ymseq | state | U+V T-P291 |  |
| P292 | Sequencer | Row labels | `updateSeqLabels()` | Melodic: 13 note+octave rows; Chipdrum: Kick…Tom2 (8); Digidrum: Sl 1-8 | 4921-4943 | ymseq + gemui | state | U+V T-P292 |  |
| P293 | Sequencer | Grid | `buildSeqGrid()` (`#seqgrid`) | 64 visible columns (NCOLS), 13 note rows + control rows; step numbers at the top (bar/beat) | 4963-5060 | ymseq + gemui | state | U+V T-P293 |  |
| P294 | Sequencer | Horizontal scrolling | `#seqScroll` | If the pattern > 64 steps; shows "a-b" | 5061-5066, 4975-4977 | ymseq + gemui | state | U+V T-P294 |  |
| P295 | Sequencer | Vertical scrolling (pitch) | `#seqTr`, mouse wheel | 23-108 (B0-C9), default 48, 1 semitone per notch; slider fitted to the height of the grid (`fitTrSlider`) | 5070-5090, 6040-6051 | ymseq + gemui | state | U+V T-P295 |  |
| P296 | Step rows | SRC | `flag mix` | Channel / Tone / Noise / Tone+Noise (shows - T N TN) | 5287, 6129-6134 | ymseq | state | U+V T-P296 |  |
| P297 | Step rows | GEN | `flag fx` | Channel / Chip Voice / SID Voice / Sync Buzzer (- CV SID SB; CD can be shown) | 5288, 6135-6140 | ymseq | state | U+V T-P297 |  |
| P298 | Step rows | ARP | `flag afl` | Boolean, visible if arp on and melodic track | 5165, 6193-6199 | ymseq | state | U+V T-P298 |  |
| P299 | Step rows | HLD | `flag tie` | Chipdrum only: no retrigger while it sounds | 4949, 5166 | ymseq | state | U+V T-P299 |  |
| P300 | Step rows | SLD | `flag slide` | Slide to the next note | 4950 | ymseq | state | U+V T-P300 |  |
| P301 | Step rows | CRV | `flag slc` | Slide curve (13); greyed if no slide is active; Custom opens the editor | 5290-5297, 6170-6178 | ymseq | state | U+V T-P301 |  |
| P302 | Step rows | SLT | `flag slt` | Channel / Slot 1-8 (row hidden: slot given by the grid row) | 5167, 6141-6147 | ymseq | state | U+V T-P302 |  |
| P303 | Step rows | CUT | `flag cut` | Hidden (replaced by the note length) | 5168 | ymseq | state | U+V T-P303 |  |
| P304 | Step rows | OCT | `flag oct` | Drums: −3…+3; melodic: Channel / 1-7 (visible only for drums) | 5169, 6148-6161 | ymseq | state | U+V T-P304 |  |
| P305 | Step rows | TON | `flag ton` | −12…+12 semitones (drums) | 5170, 6162-6169 | ymseq | state | U+V T-P305 |  |
| P306 | Step rows | GAT | `flag gat`, `gatPopup` | 0-100 % (0 = Channel), popup with slider + field + Channel/Set | 6011-6039 | ymseq | state | U+V T-P306 |  |
| P307 | Step rows | PAN | `flag pnr`, `panPopup` | −10…+10 stored as v·10+101 (0 = centre/channel), shown as C / L n / R n; hidden in 3ch | 5306-5308, 5786-5816 | ymseq | state (PAN row per step) | U+V T-P307 |  |
| P308 | Step rows | RTG | `flag rtg` | 1,2,3,4 hits in the step | 5298, 6179-6184 | ymseq | state | U+V T-P308 |  |
| P309 | Step rows | CND | `flag cnd` | Always, 1:2, 2:2, 1:4, 4:4, 25 %, 50 %, 75 % | 5299, 6185-6190 | ymseq | state | U+V T-P309 |  |
| P310 | Step rows | VOL | `flag vol` | 0-15 (default 15); left click draws while dragging; right-click opens `openVolMenu` | 5274-5286, 6056-6058, 6119-6128 | ymseq | state | U+V T-P310 |  |
| P311 | Step rows | Copy/paste a row across tracks | right-click on a row label | Copy row / Paste row (if same type) | 5028-5039 | ymseq | state | U+V T-P311 |  |
| P312 | Sequencer | Ghost notes | `ghostOn`, `buildGhostIndex()` | Notes of the other melodic tracks in the background | 5104-5128 | ymseq + gemui | state | U+V T-P312 |  |
| P313 | Sequencer | Overview | `overOn`, `#overMap`, `refreshOverview()` | Fixed scale of 256 steps, pitches B0-C9 (23-120), drum strip 26 px, view frame, playhead; click/drag moves the view (step + pitch) | 5311-5416 | ymseq + gemui | state | U+V T-P313 |  |
| P314 | Sequencer | Lowest playable limit | `PLAY_LO` | 23 (B0): cells below marked `nope`, input refused | 5321, 5217, 6292 | ymseq + gemui | state | U+V T-P314 |  |
| P315 | Sequencer | Write a note (click) | pointerdown on the grid | Empty cell: note + chord + duration of the figure; same note, other duration: resizes; otherwise deletes; preview | 6309-6329 | ymseq + gemui | state | U+V T-P315 |  |
| P316 | Sequencer | Drag drawing | `paint.note` | Dragging right lengthens (`stretchNote`); elsewhere places notes | 6559-6599 | ymseq + gemui | state | U+V T-P316 |  |
| P317 | Sequencer | Move a note | `moving`, `moveNoteTo()` | Pitch and time, length kept; auto-scroll at the edges (h and v); single click = resize/delete | 6261-6288, 6480-6529, 6613-6626 | ymseq + gemui | state | U+V T-P317 |  |
| P318 | Sequencer | Resize by the edges | `sizing` | Right (>60 %) or left (<40 %) edge of the note; `setNoteLen`, `setNoteStart` | 6274-6283, 6434-6479, 5640-5672 | ymseq + gemui | state | U+V T-P318 |  |
| P319 | Sequencer | Polyphony in the grid | `addVoiceAt`, `delVoiceAt`, `voiceRow`, `writeChord` | Voices 2..POLYMAX (6) with their own pat/oct/dur; a click on a used step adds/removes a voice; a voice is extended by dragging | 5473-5598, 6298-6308 | ymseq + gemui | state | U+V T-P319 |  |
| P320 | Sequencer | Voice promotion (mono) | `promoteVoice()` | In mono, clicking a greyed voice swaps it with the root note | 5505-5532, 6233-6238 | ymseq + gemui | state | U+V T-P320 |  |
| P321 | Sequencer | Split (Alt+click) | `splitNote()` | Alt+click on a note cuts it at the clicked step | 5673-5684, 6268-6270 | ymseq + gemui | state | U+V T-P321 |  |
| P322 | Sequencer | Tools | `seqTool` | select (frame), split (scissors), join (weld), erase (dragged eraser), move, draw | 6203-6231 | ymseq + gemui | state | U+V T-P322 |  |
| P323 | Sequencer | Selection | Shift+click | On a note: multiple selection; on an empty cell: rectangle; on a control row: block of steps | 6083-6109 | ymseq + gemui | state | U+V T-P323 |  |
| P324 | Sequencer | Right-click | pointerdown button 2 | On a note: note menu; empty cell: rectangular frame (drag); control row: dragged reset (VOL=15) | 6060-6082 | ymseq + gemui | state | U+V T-P324 |  |
| P325 | Sequencer | Note menu (right-click) | contextmenu | Copy, Cut, Paste here, Longer ], Shorter [, Split here \, Join with next J, Length: n, Delete | 6339-6365 | ymseq + gemui | state | U+V T-P325 |  |
| P326 | Sequencer | Rectangular selection menu | contextmenu | Copy (n), Cut, Paste, Clear, ±1 semitone, ±1 octave, Deselect | 6372-6405 | ymseq + gemui | state | U+V T-P326 |  |
| P327 | Sequencer | Block / channel menu (empty cell) | contextmenu | Copy/Cut/Paste/Clear block (scope: row or all), Deselect, Note length submenu, Channel submenu, Rename channel... | 6406-6432 | ymseq + gemui | state | U+V T-P327 |  |
| P328 | Editing/Undo | Cut/Copy/Paste (Edit menu) | `editCutState()`, `editClip()` | On a rectangular selection or the note under the cursor | 5439-5472 | ymseq | state | U+V T-P328 |  |
| P329 | Sequencer | Euclidean generator | `#euclidBtn`, `bjorklund()` | Steps 2-min(32,len) (default 16), Pulses 1-Steps (default 4), Rotate 0-Steps−1, Accents 0-Pulses (accent VOL 15, otherwise 10); preview; name E(K,N) + 23 named rhythms; target Note (±12 around the cursor) / Instrument / Slot; Merge; Apply repeats the motif over the track | 5817-6010 | ymseq + gemui | state | U+V T-P329 |  |
| P330 | Patterns/Song | Pattern banks | `PATS` | 32 patterns = 4 banks A-D × 8; 256 steps max (NSTEP) | 6632-6639 | ymseq | state | U+V T-P330 |  |
| P331 | Patterns/Song | Pattern schema | `snapPattern()`, `LANEKEYS` | `{steps, chans:[{pat,tie,slide,afl,vol,mix,slt,cut,fx,oct,gat,pnr,rtg,cnd,slc,ton,pat2..6,oct2..6,dur2..6}]}`; defaults pat=-1, vol=15, others 0 | 6633-6648 | ymseq | state | U+V T-P331 |  |
| P332 | Import/Export | Compatibility with the old 64-step format | `padLane()`, `loadPattern()` | Pads to 256 steps | 6649-6664 | ymio | state | G+S T-P332 |  |
| P333 | Patterns/Song | Pattern queue (playback) | `selectPattern()`, `patQueueMsg()` | During playback: switches at the end of the loop (303 style), `{type:'patQueue', idx, len, chans:[{i, pat(note abs), tie, slide, afl, vol, mix, slotRow, cut, fx, gat, rtg, cnd}]}` | 6665-6675, 6769-6783 | ymseq | state | U+V T-P333 |  |
| P334 | Patterns/Song | Bank/slot selector | `#patBanks`, `#patSlots` | A-D (Shift+1..4), slots 1-8 (keys 1-8); remembers the last pattern per bank | 6784-6815 | ymseq | state | U+V T-P334 |  |
| P335 | Patterns/Song | Slot menu | right-click `#patSlots` | Copy, Cut, Paste, +1/−1 semitone, +1/−1 octave (`transposePattern`, drums excluded) | 6800-6811, 6700-6719 | ymseq | state | U+V T-P335 |  |
| P336 | Patterns/Song | Banks/slots in the Song window | `buildSongPat()` (`#songBanks`, `#songSlots`) | Slot menu: Copy, Cut, Paste, Add to song | 6720-6748 | ymseq | state | U+V T-P336 |  |
| P337 | Editing/Undo | Undo/redo stack | `undoStack`, `redoStack`, `doUndo()`, `doRedo()` | 50 levels; types pat, song, chan, kit, mix; duplicates ignored | 6817-6947 | ymseq | state | U+V T-P337 |  |
| P338 | Options/Menus | Edit menu | `#mEdit` | Step Sequencer ^E, Arpeggiator ^B, Mixer ^M, Song ^G, Modulation ^R, Patches ^P, Drum kit ^K, Cut ^X, Copy ^C, Paste ^V, Undo ^Z, Redo ^Y (Arp/Modulation disabled for drums) | 6948-6983 | gemui | UI | V+S T-P338 |  |
| P339 | Patterns/Song | Song: model | `SONG` | List `{pat:0-31, rep:n}`; state songOn/songPos/songRep; multiple selection; drag and drop of blocks (threshold 5 px) | 6985-7000 | ymseq | state | U+V T-P339 |  |
| P340 | Sequencer | Contextual hints | `#seqHint`, `RTG_HINT` | Help text by mode (melodic/Chipdrum/Digidrum), PAN removed in 3ch | 5067-5068, 5142-5175 | ymseq + gemui | state | U+V T-P340 |  |
| P341 | Patterns/Song | Drag and drop of Song blocks | pointerdown/pointerover on `#songList .btn`, global pointerup | Moves a block, or the multiple selection if the block is part of the selection, to the `drop` target. One undo step (`pushSongUndo`). The selection follows the moved blocks. | 7000-7017, 7033-7042 | ymseq | state | U+V T-P341 |  |
| P342 | Patterns/Song | Block label | `songLabel` | `A..D` + no. 1-8 + `x`rep + `♪bpm` if the block has its own tempo | 7018-7021 | ymseq | state | U+V T-P342 |  |
| P343 | Patterns/Song | Song list (chips) | `refreshSong`, `#songList` | Block playing = `.on`, selection = `.ssel`, keyboard cursor = `.cur`. Rebuilds `seqTbar` when the nav mode changes. | 7022-7116 | ymseq | state | U+V T-P343 |  |
| P344 | Patterns/Song | Click / Shift+click on a block | `b.onclick` | Click: single selection. Shift: adds to or removes from the selection. Sets `songCur`. | 7043-7049 | ymseq | state | U+V T-P344 |  |
| P345 | Patterns/Song | Block context menu | `contextmenu` → `openGemMenu` | Entries: Repeat x1/x2/x4/x8/x16, Duplicate(n), Cut(n), Copy(n), Paste, Move left, Move right, Remove(n), Tempo... Acts on the selection, one undo per action. Paste inserts after the clicked block. | 7050-7106 | ymseq | state | U+V T-P345 |  |
| P346 | Patterns/Song | Tempo per block | "Tempo..." entry → `gemPrompt` | Block `bpm` bounded to 20..300. Empty field = Song tempo (removes `bpm`). Applied at once if playing in Song mode. | 7065-7078 | ymseq | state | U+V T-P346 |  |
| P347 | Patterns/Song | Song keyboard navigation | `songKey` (songWin window in front) | ←/→: cyclic cursor. Home/End. Insert or A: inserts the current pattern after the cursor. Delete/Backspace: deletes the selection or the block at the cursor. D: duplicates. `=`/Num+: rep+1. `-`/Num-: rep-1 (min 1). X/C: cut/copy. V: paste after the cursor. Enter: jumps to the block (loads the pattern, applies its tempo). | 7117-7186 | ymseq | state | U+V T-P347 |  |
| P348 | Windows/UI | Song window | `openSongWin`/`closeSongWin`, `#songMode`, `#songClose`, `mkWinDrag('songWin','songBar')` | Opening turns Song mode on (`songOn=1`, remembers `songBaseBpm`). Closing leaves it. Draggable window. | 7187-7193 | gemui | UI | V T-P348 |  |
| P349 | Patterns/Song | Add to Song | `#songAdd` | If the last block is the same pattern: rep++. Shift+Add: new entry `{pat,rep:1}`. | 7194-7200 | ymseq | state | U+V T-P349 |  |
| P350 | Patterns/Song | Block tempo during playback | `applySongBpm` | tempo = `SONG[i].bpm` ‖ `songBaseBpm` ‖ `GLOB.bpm`. Updates #bpm and sends `sendSeq`. | 7201-7208 | ymseq | state | U+V T-P350 |  |
| P351 | Patterns/Song | Chaining during playback | `songQueue`, `songArm`, `songOnWrap`, `songOnPa` | Queues the next pattern (`patQueueMsg`) after `rep-1` loops. At the end of the Song without Loop: stop and back to 0. Loops if `loopOn`. | 7209-7239 | ymseq | state | U+V T-P351 |  |
| P352 | Sequencer | Rec (window button) | `#seqRec` | Toggles `recOn` | 7242 | ymseq + gemui | state | U+V T-P352 |  |
| P353 | Sequencer | Figure (length of the written note) | `#seqFig`, `figItems`, `setFig` | Menu of the `FIGS` with icons and separators `FIG_SEPS`. Shows the duration in steps = round(duration×steps per beat of the resolution), min 1. Disabled on percussion tracks. | 7243-7254, 9115-9120 | ymseq + gemui | state | U+V T-P353 |  |
| P354 | Timing | Grid resolution | `#seqReso` | `RESOS` menu (separator before the triplets). Sets `GLOB.reso`, rebuilds the grid, `sendSeq`. | 7255-7263 | ymseq | state | U+V T-P354 |  |
| P355 | Sequencer | Ghost notes | `#seqGhost` | Toggles `ghostOn` (display) | 7264-7266 | ymseq + gemui | state | U+V T-P355 |  |
| P356 | Sequencer | Overview | `#seqOver` → `#overRow` | Toggles `overOn`. Shows the overview row (`buildOverview`). | 7267-7271 | ymseq + gemui | state | U+V T-P356 |  |
| P357 | Timing | Time signature | `#seqSig` | `SIGS` menu, sets `GLOB.sig` | 7272-7278 | ymseq | state | U+V T-P357 |  |
| P358 | Sequencer | Step recording | `#seqStepRec`, `stepRecord` | Writes the note at the cursor then advances by 1 (modulo plen or steps). Percussion: slot = (note%12)%8. Otherwise applies `snapToScale`, oct 0..9 (0 if equal to seqOct). vol = max(1, round(vel/127×15)). Takes priority over real-time Rec. | 7279, 7298-7316 | ymseq + gemui | state | U+V T-P358 |  |
| P359 | Sequencer | Metronome | `#seqMetro`, `metroBeep` | Toggles `GLOB.metro`. Web Audio sine oscillator, outside the chip. 1760 Hz at gain 0.4 on the 1st beat of the bar, 880 Hz at gain 0.24 on the other beats. Exponential decay down to 0.0001 in 45 ms, stop at 50 ms. | 7280-7297 | ymseq + gemui | state | U+V T-P359 |  |
| P360 | Sequencer | Real-time recording | `recordNote` | If Rec + playback: writes at the step being played (`lastStep`, or `lastSa % plen`), scale snap, vol from the velocity (/127×15, min 1) | 7317-7329 | ymseq + gemui | state | U+V T-P360 |  |
| P361 | Patches/Presets | Save a preset | `savePreset` | JSON `{app:'STYM2149', v:1, glob, curPat, chans[], pats[], song[], songOn, kit}`. Resets `workDirty` to 0. File `prism-ym2149-preset.json`. | 7332-7351 | ymio | state | S+V T-P361 |  |
| P362 | Patches/Presets | Load a preset | `loadPresetObj`, `#presetFile` | Refuses if `app≠'STYM2149'`. Migrates the old kit (hat.dec in %): hat=45×s, ohat=9×s, chat=90×s. Defaults: level 1, seqOct 3, gate 0.5, arpeggio table padded to 16. pats limited to 32. Block bpm bounded to 20..300. Resyncs the whole UI. | 7352-7397, 7477-7483 | ymio | state | S+V T-P362 |  |
| P363 | Import/Export | Default file names | `fileNames` | preset/pattern/song/kit: `prism-ym2149-{preset,pattern,song,kit}.json` | 7398-7403 | ymio | state | G+S T-P363 |  |
| P364 | Chipdrum | Save the kit | `saveKit` | `{kind:'drumkit', kit:KIT}` (indented JSON) | 7404-7407 | ymengine | state | G+V T-P364 |  |
| P365 | Windows/UI | GEM prompt box | `gemPrompt`, `#promptWin/#promptMsg/#promptInput/#promptOk/#promptCancel` | Enter = OK, Esc = Cancel, optional `maxLen` | 7413-7429 | gemui | UI | V T-P365 |  |
| P366 | Import/Export | Save as | `ensureJson` + `gemPrompt` | Adds `.json` if missing. The name is remembered in `fileNames`. | 7408-7412, 8479-8482 | ymio | state | G+S T-P366 |  |
| P367 | Patterns/Song | Save / load a pattern | `savePattern`, `#patternFile` | `{app, kind:'pattern', v:1, pattern}`. Loading replaces the current pattern (with undo). | 7430-7434, 7441-7456 | ymseq | state | U+V T-P367 |  |
| P368 | Patterns/Song | Save / load a Song | `saveSong`, `#songFile` | `{app, kind:'song', v:1, song, songOn, pats}`. Loading keeps only pat and rep: the **bpm per block is lost**. | 7435-7440, 7457-7476 | ymseq | state | U+V T-P368 |  |
| P369 | Audio engine | Offline engine | `makeOfflineEngine` | Instantiates the WORKLET class through `new Function`. Pushes the patch (+master, mode3ch, digiCh), the 16 channels, the Digidrum samples and seq `{on:1,bpm,gate,len}` (without swing). | 7492-7504 | ymcore + ymengine | state | G+U T-P369 |  |
| P370 | Timing | Length of a pattern in samples | `patternSamples` | `round(SR×60/(bpm×4)×steps)`. Always 4 steps per beat, the resolution is ignored. | 7505-7507 | ymseq | state | U+V T-P370 |  |
| P371 | Import/Export | YM3 export | `ym3Blob`, `exportYm3`, `exportSongYm3` | `'YM3!'` + 14 registers × N frames, ordered register by register (interleaved). Requires `ctx`. `prism-ym2149-{pattern,song}.ym`. | 7508-7526 | ymio | state | G+S T-P371 |  |
| P372 | Import/Export | Offline rendering | `renderBuffer` | Blocks of 2048 samples at most | 7527-7537 | ymio | state | G+S T-P372 |  |
| P373 | Import/Export | WAV export | `wavBlobDv`, `exportWav`, `exportSongWav` | 16-bit stereo PCM at `ctx.sampleRate`, clipping at ±1, ×32767. The Song is rendered segment by segment × rep. `prism-ym2149-{pattern,song}.wav`. | 7538-7560, 8323-8345 | ymio | state | G+S T-P373 |  |
| P374 | Control/Monitor | Chip conformity indicator | `chipIssues`, `refreshChip`, `#chipTxt` | Shows "CHIP: OK" or "CHIP: n" (warn class). A click opens the list. Checks: more than 3 tracks outside 3ch; dual chip; chords; TP>4095 or <1; ADSR Smooth; ADSR on Sync Buzzer; Digidrum; Chipdrum outside 3ch; SID ratio>4; steps/s > VBL; arp > VBL; orn; vibrato; non-linear slides; pan, PAN row, levels; master≠0.7. | 7639-7746 | gemui + ymengine | UI | V+U T-P374 |  |
| P375 | Windows/UI | Message box | `gemInfo`, `#msgWin/#msgTxt/#msgOk`, `msgKey` | Enter or Esc close it | 7629-7633, 8464-8473 | gemui | UI | V T-P375 |  |
| P376 | Windows/UI | Busy cursor (bee) | `gemBusy`, `busyWrap`, `withBusy` | `busy` class on body. Wrapped functions: exportWav, exportMidi, exportYm (**does not exist**), exportSndh, exportStems, savePreset, loadPresetObj, renderOffline (**does not exist**). | 7608-7628, 9642-9643 | gemui | UI | V T-P376 |  |
| P377 | Windows/UI | GEM underline of editable fields | `charWidth`, `gemUnderMark`, `gemFillMark`, MutationObservers | CSS variables `--cw` and `--typed` computed from the width of the character "0" | 7562-7607 | gemui | UI | V T-P377 |  |
| P378 | Options/Menus | Register exports submenu | `regSub` | VBL, YM3 and SNDH are greyed out outside "Authentic 3ch" (explanatory tooltip) | 7634-7638 | gemui | UI | V+S T-P378 |  |
| P379 | Import/Export | YM3 import | `parseYM3` | Accepts `YM3!` and `YM3b` (4 loop bytes at the end). Interleaved data, 14 registers. | 7749-7762 | ymio | state | G+S T-P379 |  |
| P380 | Import/Export | SNDH import (PRISM only) | `parseSndh` | Requires `SNDH` in the first 4 KB and the `PRSM` marker. Reads subsong 1: 16-bit mask then changed bytes; R13=$FF unless written. An ordinary SNDH is refused (it would need a 68000 interpreter). | 7763-7787, 7827-7832 | ymio | state | G+S T-P380 |  |
| P381 | Import/Export | YM5/YM6 import | `parseYM56` | Checks `LeOnArD!`. Reads the number of frames, the attributes (bit 0 = interleaved), skips the digidrums, reads clock and VBL frequency, title/author. Not interleaved: 16 bytes per frame. | 7788-7813 | ymio | state | G+S T-P381 |  |
| P382 | Import/Export | LHA detection | `isLha` | `-lh` bytes detected: message asking to decompress first | 7814-7824 | ymio | state | G+S T-P382 |  |
| P383 | Import/Export | YM player | `ymLoad`, `#ymWin`, `#ymName`, `#ymPos`, `#ymPlay`, `#ymRew`, `#ymClose`, `#ymFile` | The VBL of the file sets `GLOB.vbl`: 60→60, 70 or 71→70, otherwise 50. Messages `ymload{rows,loop:1}` and `ymplay{on,rewind}`. Position shown as "n / total (s)" with s = frames/vbl. | 7817-7869 | ymio | state | G+S T-P383 |  |
| P384 | MIDI | MIDI import (SMF 0/1) | `parseMidiFile`, `midiImport`, `#midiInFile` | SMPTE refused. First tempo meta event → bpm bounded to 20..300. 1 step = div/4 ticks. At most 32 patterns of `GLOB.steps`. Melodic channels then channel 10 → tracks (3×chips in 3ch, otherwise 16). Channel 10 becomes Chipdrum (effect 4) through `GM_SLOT`. Held notes → `tie`. vol = vel/127×15 (1..15). Replaces the Song (one entry per pattern). | 7872-8012 | plugin | state | U+M T-P384 |  |
| P385 | MIDI | GM → kit slot mapping | `GM_SLOT`, `GM_DRUM=[36,38,39,42,46,44,45,41]` | Kick, Snare, Clap, Hat, OHat, CHat, Tom1, Tom2. Fallback: ≤37 kick, 39/40 clap/snare, ≤41 snare, 42 hat, 44 CHat, 46 OHat, ≤50 tom1, otherwise tom2. | 7923-7933, 8015 | plugin | state | U+M T-P385 |  |
| P386 | MIDI | MIDI export (SMF 1) | `midiBlob`, `exportMidi`, `exportSongMidi` | PPQ 96, 1 step = 24 ticks. Track 0 = tempo. One track per channel used, named "PRISM chN". Percussion on channel 10, GM_DRUM notes, duration 0.5 step. Melodic: duration len×(gate+0.5) steps, held notes extended. vel = vol/15×127. Channel 9 is skipped. `.mid` | 8014-8097 | plugin | state | U+M T-P386 |  |
| P387 | Import/Export | SNDH export | `packFrames`, `sndhBlob`, `exportSndh`, `exportSongSndh` | Header: 3×BRA, SNDH, TITL, COMM/CONV "STYM2149 from PRISM", `##NN`, `TC<vbl>`, TIME, HDNS. Position-independent 68k player (writes $FFFF8800/8802, loops). exit: R7=$3F, R8-10=0. `PRSM` marker. Song: subsong 1 = song, 2 = current pattern. Compressed data: 14-bit mask + changed bytes (R13 written only if ≠$FF). | 8099-8231 | ymio | state | G+S T-P387 |  |
| P388 | Import/Export | Stems export (ZIP) | `zipStore`, `crc32`, `exportStems`, `exportSongStems`, `stemName`, `isolate` | ZIP in store mode. One WAV per track that contains notes (`mgain` isolated). Names `prism-track-{A,B,C,Digi}` in 3ch, otherwise `01..16`. `prism-ym2149-{pattern,song}-stems.zip` | 8233-8322 | ymio | state | G+S T-P388 |  |
| P389 | Import/Export | Register capture per frame | `ymRegsFrame`, `collectYmFrames`, `collectYmFramesSong` | One frame = round(SR/vbl) samples. R0-R5 = tpEff‖TP. R6 = NP of the last voice with noise. R7 = mixer. R8-10 = vol4, or $10 in envelope mode (except SID). Non-chip Digidrum and Chipdrum → 0. R11/12 = EP of the voice with the envelope. R13 written **only once** (first shape), $FF afterwards. | 8346-8430 | ymio | state | G+S T-P389 |  |
| P390 | Import/Export | VBL export (.s) | `vblSource`, `exportVbl`, `exportSongVbl` | ASM source `dc.b` of 14 bytes per frame, `ym_frames: dc.w n`, `ym_data:`. The header says "(50 Hz)" hard-coded. | 8431-8455 | ymio | state | G+S T-P390 |  |
| P391 | Import/Export | Song segments | `songSegs`, `loadSegIntoEng`, `requireSong` | smp = SR×60/(GLOB.bpm×4)×steps. The **tempo per block is ignored** in the exports. An empty Song raises an alert. | 8389-8408 | ymio | state | G+S T-P391 |  |
| P392 | Options/Menus | Fuji menu | `#mFuji` | Information (`#infoWin`), Shortcuts (^?) → `openHelp` | 8458-8463 | gemui | UI | V+S T-P392 |  |
| P393 | Options/Menus | Menus open on hover | `mouseenter/mouseleave` listeners on mFuji/mFile/mEdit/mOptions | Open on hover. Closing delayed by 260 ms (menu) and 220 ms (submenu). | 8474-8477, 8622-8627 | gemui | UI | V+S T-P393 |  |
| P394 | Options/Menus | File menu | `#mFile` | Load / Save / Save as (Preset, Pattern, Song, Drum kit). Import (YM dump, MIDI file). Export pattern (MIDI, WAV, Stems). Export song (VBL dump, YM3, SNDH \| MIDI, WAV, Stems). Demo pattern ^D. Quit ^Q. | 8478-8504 | gemui | UI | V+S T-P394 |  |
| P395 | Options/Menus | Options menu | `#mOptions` | Zoom (50-200 %). Audio output. MIDI input. MIDI learn / Stop. Clear MIDI map. VBL frequency 50 (PAL) / 60 (NTSC) / 70 Hz (Mono). Keyboard layout Auto/QWERTY/AZERTY/QWERTZ. Note names (`NOTE_LANGS`). Tooltips On/Off. | 8505-8530 | gemui | UI | V+S T-P395 |  |
| P396 | Windows/UI | Zoom of the interface | `setZoom`, `autoZoom`, `ZOOMS=[50..200 pas 25]` | Applies `scale` on `#zoomRoot`. Stored in localStorage `prismYmZoom`. Automatic value: <1100×700 → 50, <1400×850 → 75, >2400 → 125, otherwise 100. | 8531-8559 | gemui | UI | V T-P396 | Adaptation: integer zoom first, nearest neighbour (§6.6) |
| P397 | Audio engine | Audio output | `pickAudioOut` | Lists the `audiooutput` devices then `ctx.setSinkId`. First entry "System default (no ASIO in browsers)". | 8562-8577 | ymcore + ymengine | state | G+U T-P397 | Adaptation: GEM menu of the JUCE devices in Standalone (§7) |
| P398 | MIDI | MIDI input | `pickMidiIn`, `hookMidiInputs`, `midiOff` | All inputs or one given input. Inputs greyed out without access (denied / unsupported / not yet). Status shown in `#miditxt`. | 8578-8594, 10264-10274 | plugin | state | U+M T-P398 | Adaptation: MIDI from the host; Standalone = JUCE devices |
| P399 | Keyboard | Keyboard layouts | `LAYOUTS` | qwerty, azerty, qwertz: 18 note keys + 2 octave keys | 8595-8599 | gemui + plugin | UI | V+M T-P399 |  |
| P400 | Options/Menus | Generic GEM menu | `openGemMenu`, `openGemSub`, `closeGemMenu` | Check mark ✓ in front of the active entry, shortcut aligned right, icons, greyed-out entries with a reason (title), cascading submenus, columns (`chordCols`), repositioning inside the window. Closes on a click outside. | 8602-8748, 8782-8788 | gemui | UI | V+S T-P400 |  |
| P401 | Step rows | Step volume popup | `openVolMenu` | Vertical slider 0..15 with live listening | 8750-8781 | ymseq | state | U+V T-P401 |  |
| P402 | Sequencer | Grid tools | `TOOLS`, `buildSeqTools`, `setSeqTool`, `#seqTools` | move, draw (default), erase, split, join, select. The cursor changes with the tool. | 8789-8822, 8840-8889 | ymseq + gemui | state | U+V T-P402 |  |
| P403 | Editing/Undo | Clipboard buttons | `CLIPS`, `editClip`, `refreshClipBtns` | Copy, Cut, Paste (Ctrl+C/X/V). Greyed out when there is nothing to do. | 8823-8839, 8857-8877 | ymseq | state | U+V T-P403 |  |
| P404 | Sequencer | Transport bars | `buildTbars`, `wireTbars`, `#seqTbar`, `#songTbar` | trPlay (Play/Stop), trRec (if armed while stopped, starts playback), trLoop (`loopOn`). In Song mode: trStart, trPrev, trNext. | 8890-8955 | ymseq + gemui | state | U+V T-P404 |  |
| P405 | Patterns/Song | Play from the start / previous / next block | `transportStart`, `songStep` | Goes back to block 0 and applies its tempo. Cyclic Prev/Next. | 8956-8978 | ymseq | state | U+V T-P405 |  |
| P406 | Sequencer | Play / Stop | `#seqPlay` | Toggles `GLOB.seqOn`. In Song mode, starts at block 0. Stop: disarms Rec and clears the step shown. | 8979-8999 | ymseq + gemui | state | U+V T-P406 | Adaptation: Host sync / Free run (§4) |
| P407 | Editing/Undo | Block copy / clear / paste | `blockCopy`, `blockClear`, `blockPaste` | All the rows, or only `selRow`. Cleared values: pat=-1, vol=15, 0 elsewhere. | 9001-9024 | ymseq | state | U+V T-P407 |  |
| P408 | Sequencer | Track rotation | `#rotL`, `#rotR`, `rotateLane` | Rotation by one step of the rows pat, tie, slide, afl, vol, mix, fx, slt, cut, oct, gat (**not** pnr/rtg/cnd/slc/ton nor the chord voices). With a selection, the selection is shifted instead (`shiftSel`). | 9025-9041, 9058-9085, 9155-9156 | ymseq + gemui | state | U+V T-P408 |  |
| P409 | Sequencer | Transposition | `#trDn`, `#trUp`, `#trOctDn`, `#trOctUp`, `transposeLane`, `transposeSel` | ±1 and ±12 semitones. All or nothing: the track must stay within 24..108, the selection within PLAY_LO..MAP_HI. Snap to the scale if on. No effect on percussion tracks. | 9042-9057, 9086-9110 | ymseq + gemui | state | U+V T-P409 |  |
| P410 | Scales | Root, scale, constraint | `#scaleRoot`, `#scaleSel`, `#scaleSnap`, `refreshScale`, `refreshScaleEnable` | Root among the 12 note names, scale among `SCALE_NAMES`. Snap toggles `scaleSnap`. Disabled on percussion tracks. `sendPatch` (Scale-locked). | 9111-9153 | ymseq | state | U+V T-P410 |  |
| P411 | Sequencer | Playback follow | `#seqFollow` | Toggles `followOn` | 9154 | ymseq + gemui | state | U+V T-P411 |  |
| P412 | Sequencer | Number of steps | `#seqSteps` | 1..256, default 16 | 9157-9161 | ymseq + gemui | state | U+V T-P412 |  |
| P413 | Sequencer | Vertical scrolling | `#seqTr` | `viewAbsBase` 23..108, default 48 | 9162-9165 | ymseq + gemui | state | U+V T-P413 |  |
| P414 | Sequencer | Clear the track | `#seqClear`, `clearLane`, `laneHasNotes` | Confirmation if the track contains notes. Empties all the rows and the chord voices 2..POLYMAX. | 9166-9197 | ymseq + gemui | state | U+V T-P414 |  |
| P415 | Other | Protection of unsaved work | `beforeunload`, `workDirty` | The browser asks for confirmation as long as the work is not saved (only savePreset resets it to 0) | 9198-9205 | plugin | state | M T-P415 | Adaptation: Standalone only; in the plugin, the host handles closing |
| P416 | Other | Quit | `#mainClose`, `quitApp`, `gemConfirm` | Confirmation, playback stopped, `allOff`, windows closed, "Program terminated" screen with a Restart button (reload) | 9206-9226 | plugin | state | M T-P416 | Adaptation: Standalone only; in the plugin, the host handles closing |
| P417 | Windows/UI | Confirmation box | `gemConfirm`, `#confirmWin` | **Enter and Esc cancel**. Tab moves from one button to the other. Default focus on Cancel. | 9227-9247 | gemui | UI | V T-P417 |  |
| P418 | Other | Demo pattern | `demoPattern`, `doDemo` | Confirmation if channels 1-4 contain data. Ch1 bass arp Table [0,3,7]. Ch2 SID lead. Ch3 Chipdrum. Ch4 Sync Buzzer stabs (EP 1, shape 8), outside 3ch only. seqOct 4/2/4/3, levels 1/0.8/1/0.5. | 9248-9328 | plugin | state | M T-P418 |  |
| P419 | Sequencer | Track length (polymetry) | `#plenBtn` popup | Slider and field 1..64. "Global" resets plen=0 (follows the pattern), "Set" applies. Label "Len n" or "Len =". | 9329-9364, 9416 | ymseq + gemui | state | U+V T-P419 |  |
| P420 | Timing | BPM | `#bpm` | 60..240 in steps of 0.01, default 120 | 9365-9367 | ymseq | global.freeTempo | U+V T-P420 |  |
| P421 | Timing | Swing | `#swing` | 50..75 %, default 50 | 9368-9372 | ymseq | global.swing | U+V T-P421 |  |
| P422 | Sequencer | Gate | `#gate` | 5..95 %, default 50. Value specific to each channel (`CH[cur].gate`). | 9373-9377 | ymseq + gemui | track[n].gate (+ global gate) | U+V T-P422 |  |
| P423 | Windows/UI | Channel panel refresh | `refreshUI` | Source, volume mode, NP, vol, vmin, ratio, ef (from EP), shape (hex + SVG), effect. Arp disabled on percussion. Vibrato depth/rate. chLevel. ADSR (4-bit reg or audio gain mode, forced in 3ch). Shape disabled with a fixed volume. digi/ndrum/sid panels. Keyboard ↔ pads. digiCh line in 3ch. | 9380-9457 | gemui | UI | V T-P423 |  |
| P424 | Control/Monitor | Voice LEDs | `#voices` .vled ×16 | 16 LEDs | 9460-9464 | gemui + ymengine | UI | V+U T-P424 |  |
| P425 | Control/Monitor | Register monitor | `#regs`, `#reginfo` | Shows R0, R1, R6, R7, R8, R11, R12, R13 in hex every 120 ms, for the current patch (initial TP 284). Info: channel, effect, "3CH AUTH (shared env+noise)" or "16CH". | 9465-9487 | gemui + ymengine | UI | V+U T-P425 |  |
| P426 | Control/Monitor | Main oscilloscope | `drawScope`, `#scope` | Trigger on a rising zero crossing, 1024 samples, amplitude ×0.45 | 9489-9507 | gemui + ymengine | UI | V+U T-P426 |  |
| P427 | Keyboard | On-screen keyboard | `buildKbd`, `#kbd`, `#octv` | 4 octaves (28 white keys) from min(baseOct,6). Outside B0..C9: key marked `nope`. | 9510-9546 | gemui + plugin | UI | V+M T-P427 |  |
| P428 | Keyboard | Keyboard destination | contextmenu on `#kbd`/`#pads` → `chanMenu` | Choice of the voice played (Voice A/B/C/Digi in 3ch). Disabled channels greyed out. | 9547-9562 | gemui + plugin | UI | V+M T-P428 |  |
| P429 | Keyboard | Keys lit during playback | `seqKbdLight`, `seqKbdClear`, `litSwap` | Keys or pads lit, polyphony included. Held notes keep their state. | 9563-9603 | gemui + plugin | UI | V+M T-P429 |  |
| P430 | Chipdrum | Percussion pads | `buildPads`, `#pads` | 8 pads, note 48+i, velocity 118. Chipdrum: `DRUMFULL` names. Digidrum: "Slot n" (empty ones marked), the pad selects the slot. | 9604-9635 | ymengine | state | G+V T-P430 |  |
| P431 | Windows/UI | Window opening buttons | `#openKitBtn`, `#openArpBtn`, `#openOrnBtn`, `#openSeqBtn` | Open kit, arp, modulation, sequencer | 9636-9640 | gemui | UI | V T-P431 |  |
| P432 | Keyboard | Note playing | `press`, `release`, `forceRelease` | Out of range ignored (except percussion). Latch: a new press releases; with the mouse, mono. Arp on → `sendHeld`. | 9645-9670 | gemui + plugin | UI | V+M T-P432 |  |
| P433 | Keyboard | Mouse glissando | pointerover on `#kbd` | Button held: the note follows the cursor | 9671-9690 | gemui + plugin | UI | V+M T-P433 |  |
| P434 | Keyboard | Layer / Latch | `#kbLayer`, `#kbLatch` | Layer: `kbmode{focus,layer}` + panic. Latch: toggle, releases everything when switched off. | 9691-9702 | gemui + plugin | UI | V+M T-P434 |  |
| P435 | Keyboard | Octave − / + (panel) | `#octDown`, `#octUp` | baseOct bounded to **0..6** | 9703-9704 | gemui + plugin | UI | V+M T-P435 |  |
| P436 | Keyboard | Key mapping | `CODEMAP`, `updateKbdHint` | A W S E D F T G Y H U J K O L P ; ' → 0..17 (C to F+1). Automatic layout through `navigator.keyboard.getLayoutMap`. | 9706-9731 | gemui + plugin | UI | V+M T-P436 |  |
| P437 | Keyboard | TKL drawing | `buildTkl`, `TKL_ROWS`, `TKL_LABELS`, `TKL_OTHER`, `#tkl`, `#kbdLay` | Note keys (clickable), Oct-/Oct+ (Z/X), hints Play/Stop, Panic, Metro, Channel, Pattern | 9732-9860 | gemui + plugin | UI | V+M T-P437 |  |
| P438 | Keyboard | Numeric keypad | `buildTklNum`, `#tklnum` | Num0..9 = direct octave | 9861-9892 | gemui + plugin | UI | V+M T-P438 |  |
| P439 | Keyboard | TKL keyboard octave | `#kbdOctDn`, `#kbdOctUp`, `kbdOctShift`, `#kbdOctV`, `#kbdRange` | baseOct 0..9. Range shown with "partly out of the chip range". | 9893-9917 | gemui + plugin | UI | V+M T-P439 |  |
| P440 | Keyboard | Held notes on octave change | `#kbdHeldSel`, `retuneHeld` | Keep (default) or Follow (held notes transposed again) | 9911-9940 | gemui + plugin | UI | V+M T-P440 |  |
| P441 | Windows/UI | Keyboard window | `openKbdWin`, `#kbdMapBtn`, `#kbdClose`, `mkWinDrag('kbdWin','kbdBar')` | Close = panic + everything off | 9973-9984 | gemui | UI | V T-P441 |  |
| P442 | Sequencer | Scissors cursor (Alt) | `setCutCursor` | `cut` class on the grid while Alt is held (except percussion) | 9988-9994 | ymseq + gemui | state | U+V T-P442 |  |
| P443 | Editing/Undo | Note editing at the cursor (keyboard) | keydown, seqWin in front | Ctrl+C/X: copy/cut the note. Ctrl+V: paste. `]`: lengthen by one step. `[`: shorten. `\`: split at the cursor. J: join to the next one. | 10003-10031 | ymseq | state | U+V T-P443 |  |
| P444 | Editing/Undo | Rectangular selection (keyboard) | `rectSel` | ←/→: moves by one step. ↑/↓: ±1 semitone, Shift = ±12. Del/Backspace: clears. Esc: cancels the selection. Ctrl+C/X/V (rectClip: vol, slide, slc, lengths). | 10032-10071 | ymseq | state | U+V T-P444 |  |
| P445 | Editing/Undo | Undo / Redo | Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y | `doUndo`/`doRedo` | 10073-10074 | ymseq | state | U+V T-P445 |  |
| P446 | Windows/UI | Window shortcuts | Ctrl+E seq, Ctrl+B arp, Ctrl+M mixer, Ctrl+G song, Ctrl+R modulation, Ctrl+P library, Ctrl+K kit | Ctrl+B and Ctrl+R have no effect on percussion tracks | 10075-10083 | gemui | UI | V T-P446 |  |
| P447 | Source | Effect shortcuts | `FX_KEYS` (Ctrl+V=0, S=1 SID, I=2 Digi, U=3 Sync, C=4 Chipdrum) | `setEffect`. Ctrl+C/V are only taken by the clipboard if there is a note or a clip at the cursor. | 10084-10085 | ymengine | state | U+V T-P447 |  |
| P448 | Options/Menus | Help, demo, quit | Ctrl+/ (or ?), Ctrl+D, Ctrl+Q | openHelp, demoPattern, quitApp | 10086-10088 | gemui | UI | V+S T-P448 |  |
| P449 | Sequencer | Space bar | Space | Play/Stop | 10092 | ymseq + gemui | state | U+V T-P449 | Adaptation: Host sync / Free run (§4) |
| P450 | Control/Monitor | Esc | Escape | Closes the menu, then the top window (mix, ym, help, kit, arp, lib, orn, song, seq, scWinN). Otherwise panic (`allOff`). | 10093-10114 | gemui + ymengine | UI | V+U T-P450 |  |
| P451 | Patterns/Song | Digits | Digit1-8, Shift+Digit1-4 | Selects the pattern slot 1-8. Shift: bank A-D. | 10115-10122 | ymseq | state | U+V T-P451 |  |
| P452 | Arpeggiator | Arpeggio table from the keyboard | arpWin in front | Arrows: 16×16 cursor. Enter: table[c] = 15-row, extends tlen. | 10124-10141 | ymengine | state | U+V T-P452 |  |
| P453 | Modulation | Modulation table from the keyboard | ornWin in front | Arrows: 64 columns, rows 0..24. Enter: ornTable = 12-row, extends ornLen. Del: resets to 0, nse 0, vol 15. N: toggles NSE. | 10143-10175 | ymengine | state | U+V T-P453 |  |
| P454 | Sequencer | ←/→ | ArrowLeft/Right | Grid open: cyclic step cursor. Otherwise (or Shift): previous or next channel (modulo 4 in 3ch, 16 otherwise). | 10176-10187 | ymseq + gemui | state | U+V T-P454 |  |
| P455 | Sequencer | ↑/↓ in the grid | ArrowUp/Down | Next row. The view scrolls between 24 and 84; percussion limited to the 8 rows. | 10193-10210 | ymseq + gemui | state | U+V T-P455 |  |
| P456 | Sequencer | Place / remove a note | Enter, NumpadEnter, Shift+Space | Toggles the note at the cursor, with preview | 10211-10226 | ymseq + gemui | state | U+V T-P456 |  |
| P457 | Sequencer | Clear a step | Delete/Backspace | pat=-1, vol=15, other rows 0 | 10227-10234 | ymseq + gemui | state | U+V T-P457 |  |
| P458 | Keyboard | Octave on the physical keyboard | Z/X (0..9), Numpad0-9 (keyboard window open), M = metronome |  | 10236-10242 | gemui + plugin | UI | V+M T-P458 |  |
| P459 | Keyboard | Playing on the physical keyboard | `CODEMAP` + `kbdHeld` | Active only if the keyboard window is open. Velocity 127. Auto-repeat ignored. On release, frees the note actually played. | 10243-10260 | gemui + plugin | UI | V+M T-P459 |  |
| P460 | MIDI | Web MIDI initialisation | `initMIDI` | requestMIDIAccess; states ok, denied, unsupported. Hot reconnection through `onstatechange`. | 10275-10289 | plugin | state | U+M T-P460 | Adaptation: MIDI from the host; Standalone = JUCE devices |
| P461 | MIDI | Incoming messages | `onMIDI` | F8 clock, FA start, FB continue, FC stop, F2 SPP → worklet (`mclk/mstart/mcont/mstop/mspp`). Note On/Off → press/release. Pitch bend ±2 semitones. CC → onCC. Notes learnt by learn → control (the note is not played). | 10290-10311 | plugin | state | U+M T-P461 |  |
| P462 | MIDI | MIDI learn | `setLearn`, `midiBind`, `applyMidiMap`, `#learnBadge` | Click a slider or a button (with an id) then move a CC or play a note. Slider: min + v/127×(max−min). Button: click on a rising edge ≥64. Stored in localStorage `prismYmMidiMap`. | 10312-10352 | plugin | state | U+M T-P462 |  |
| P463 | MIDI | Default CCs | `onCC` | 70 tone on/off, 71 noise, 72 NP 1..31, 73 envelope mode, 74 ef 0..4000, 75 shape v>>3, 76 effect floor(v/26) 0..4, 77 ratio 25..800, 78 vmin 0..15, 80 arp on/off, 81 arpRate 4..200, 82 play/stop, 83 bpm 60..240, 84 channel v>>3, 7 master 0..100 | 10353-10374 | plugin | state | U+M T-P463 |  |
| P464 | Windows/UI | Direct entry of values | `makeEditable`, `maskEdit`, `sliderApply` | Click on the value → GEM-style masked field, rounded to the step then bounded. Masks: ratio 9.99, ef 999.9, arpRate 999, arpLen 99, seqSteps 999, bpm 999.99, gate 99, vibDepth 999, ornLen 99, vibRate 99.9, swing 99. No mask: np, vol, vmin, master, chLevel. | 10376-10528 | gemui | UI | V T-P464 |  |
| P465 | Envelope | Envelope frequency entry | `efv` | f in Hz → EP = round(2e6/(256×32×f)) bounded to 1..65535 | 10510-10517 | ymcore + ymengine | state | U+V T-P465 |  |
| P466 | Envelope | Direct entry of EP | `epv` | Decimal or `$hex`, bounded to 1..65535 | 10530-10536 | ymcore + ymengine | track[n].envPeriod | U+V T-P466 |  |

**Total: 466 rows**, all linked to a module and to at least one test.

