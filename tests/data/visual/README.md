# Prototype reference captures (visual regression test, M4)

The 46 PNGs in this folder (outside `stym/`) are captures of `YM2149_Prototype.html` (STYM2149_SPEC.md §6): 1272 × 944 px
(the editor desktop, D-067), 8-bit RGB, one per scene of `tools/proto_capture/capture.mjs`. `orn.png` and `orn_on.png`
(the prototype's Modulation window) are kept but no longer compared: the sequences joined that window below its table
in the fifth DAW test (D-240), so its scenes `orn` and `orn_on` are now STYM2149 scenes (`stym/`); likewise
`menu_source.png`, the Source menu having four more entries with the envelope (D-246), and `arp_table.png`, the
arpeggio grid having the offsets +16 to +24 (D-322). The `ui` test
(`tests/gemui/test_visual.cpp`) renders the same scenes with the GEM window (`tools/gemshot/Scenes.cpp`) and requires
fewer than 9591 differing pixels, that is 1 % of the M4 desktop (1016 × 944; the added columns are only
background; 2 % for the floating window scenes `seq`, `seq_demo`, `song` and `song_demo`, D-121; the Mixer,
scenes `mixer`, `mixer_3ch` and `mixer_scope`, the Arpeggiator, scenes `arp`, `arp_on` and `arp_midi`,
the Slide Curve, scenes `curve` and `curve_custom`, the Computer Keyboard, scenes `kbd`, `kbd_held`, `kbd_azerty` and
`kbd_high`, the YM Player, scene `ym` (M7), and the SHORTCUTS window, scene `help` (M8, D-189), fit within
1 %). A pixel differs when one of its channels is off by more than 4 out of 255, outside the areas that a scene ignores
by citing the decision that changes all their content (D-086: waveform and note of the frame in the `digidrum` scene).

Taken on 15/10/2026 with Chromium 141.0.7390.37 (Playwright 1.56.1) on Linux, without text smoothing
(`tools/proto_capture/fonts-noaa.conf`), prototype zoom at 100 %, without MIDI access; taken again the same day at
1272 px wide (M5); the `ym` (M7) and `help` (M8) scenes were added on 16/10/2026 with the same tools. The main
window is identical pixel for pixel there, shifted by 128 px; only the CHIP alert changes (its 520 px `max-width`
replaces half the desktop as the bound).

Regenerate (only if the prototype changes):

```sh
PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs \
  node tools/proto_capture/capture.mjs YM2149_Prototype.html tests/data/visual
```

See the differences of a scene:

```sh
gemshot /tmp/gemshot --ref tests/data/visual default   # writes default.png and default-diff.png (differences in red, ignored areas in blue)
```

The remaining differences are intended and listed in `docs/m4/RAPPORT_M4.md`: small texts in the GEM 8×8 and 6×6
fonts (D-071), icons and lines drawn without anti-aliasing, corrected texts (D-029), real range of the envelope
period, keyboard help line, added menu entries. Scene `help` (M8): a single arrow at each end of the scrollbar,
instead of the pair that Chromium draws (D-117), and the text of the M key, "metronome on / off" (D-189). The row of window
buttons (Mixer to Patches) is centred in STYM2149 (fifth DAW test, D-247): every scene compares it with its place in
the capture, at the left edge, and the part of that place it left with the background beside the new row.

## Windows specific to STYM2149 (`stym/`)

The windows and rows that the prototype does not have (M6: Instruments window, INS row of the Step Sequencer and its
menu, D-147; sequences and menu of a MIX cell, D-148, below the Modulation table since D-240, which also brought the
`orn` and `orn_on` scenes here; Source menu with the envelope entries, D-246; CMD row, D-150; M7: WAV export dialog, D-171; M8: tooltips in French and German,
D-185; Information window, D-186) have no capture. Their scenes (`stymSceneNames()`
in `tools/gemshot/Scenes.cpp`) are compared with a gemshot render checked by eye, stored in `stym/`; the test allows
no differing pixel there (beyond 4 out of 255 per channel). After an intended change, look at the new render, then
copy it:

```sh
gemshot /tmp/gemshot --ref tests/data/visual ins_window   # differences in ins_window-diff.png
cp /tmp/gemshot/ins_window.png tests/data/visual/stym/
```
