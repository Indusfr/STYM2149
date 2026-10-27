# M3 report: minimal plugin

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM.

Decisions cited: [DECISIONS.md](../../DECISIONS.md) (D-057 to D-066). Third-party components: [THIRD_PARTY.md](../../THIRD_PARTY.md).

## In short

- **Done**: STYM2149 loads as a MIDI instrument in a host. Linux: VST3, LV2, CLAP, Standalone; macOS: AU, VST3, AAX, CLAP, Standalone; Windows: VST3, AAX, CLAP, Standalone with ASIO (D-059). 417 automatable parameters with stable identifiers (D-057), MIDI notes in the three voice modes, the prototype's controllers (D-061), state saved and read back in the v1 format (D-065), separate outputs per track and per chip (D-062). pluginval level 10 passed on the VST3 and the LV2 under Linux, clap-validator on the CLAP (D-066); the CI runs them again on the three systems, with the AU and auval under macOS.
- **Not done**: Program Change and MIDI learn (M6), sustain pedal, sample-accurate automation, GEM interface (M4, JUCE's generic editor until then), PACE signing of the AAX (M8).

## Architecture

| Layer | Role | JUCE |
|---|---|---|
| `src/plugincore` | Parameter table, mapping to the engine, v1 state, engine driver for the audio thread (MIDI, CC, fades, new engine prepared outside the audio thread) | no |
| `src/plugin` | JUCE processor: host parameters built from the table, buses, MIDI conversion, state, message-thread timer | yes (8.0.15) |
| clap-juce-extensions | CLAP format of the same processor | yes |

The core without JUCE is tested like the rest of the engine (`stym_unit`, S11); the JUCE layer has its own tests (`stym_plugin_tests`), pluginval and clap-validator.

## Results

Measured on 13/10/2026: Linux x86-64, GCC 13.3, Release, virtual machine with 4 cores.

| Check | Threshold | Measurement | State |
|---|---|---|---|
| Unit tests (`stym_unit`), including 18 `[plugin]` cases | all pass | 110 test cases, 243 723 assertions | OK |
| Plugin core under ASan and UBSan | no error | 18 `[plugin]` cases, 5 292 assertions, no error | OK |
| JUCE processor tests (`stym_plugin_tests`) | all pass | 9 test cases, 2 204 assertions (with and without a display) | OK |
| S11: allocations in the audio thread, plugin driver included (44.1 and 96 kHz, 1 500 blocks, MIDI, CC, automation, changes of mode, clock and resampling) | 0 | 0 | OK |
| No allocation in `processBlock` (JUCE processor, 400 blocks) | 0 | 0 | OK |
| Oracles S1, S2, S3, LZH | unchanged | 0 deviations | OK |
| pluginval 1.0.4 level 10, VST3 | pass | pass | OK |
| pluginval 1.0.4 level 10, LV2 | pass | passed 8 times out of 8 (4 with the seed that made it crash before the fix) | OK |
| clap-validator 0.4.1, CLAP | 0 failures | 44 tests: 33 passed, 0 failures, 1 accepted warning (D-066), 10 not applicable; 3 identical runs | OK |
| Compiler warnings in the project's code | 0 | 0 (GCC 13, options of `cmake/Warnings.cmake`) | OK |

The processor tests cover: parameters as seen by the host (identifiers, names, groups, automatable, value text), output buses, MIDI note to audio and the separate output of the channel's track, state read back from `tests/data/state_v1.bin`, the request to the CLAP host (and to it alone) to re-read the values after a state is read back, the prototype's CCs to the parameter and the host (full gesture), clock change in real time then offline, rendering independent of the block size (32 against 512), no allocation in `processBlock`.

## Defects found by the validators

**pluginval, LV2.** Under LV2, pluginval crashed intermittently during "Editor Automation" (2 runs out of 3 before the fix) (segmentation fault in `Engine::~Engine`, called by `Driver::service` on the plugin's message thread). Cause: an LV2 host creates and destroys the plugin on its own thread, while JUCE's timers run on the plugin's message thread; stopping a timer from another thread does not wait for a call in progress, which carried on with a destroyed processor and corrupted the heap. Fix: the timer is a separate object, detached on destruction (stopped, then waiting for a call in progress under a lock), and destroyed on the message thread. The other formats can meet the same case with some hosts; the fix covers them.

**clap-validator, CLAP.** The three state reproducibility tests failed: after `state.load`, the parameter values changed without the plugin asking the host to re-read them (`CLAP_PARAM_RESCAN_VALUES`), so a CLAP host could display and automate the old values. Fix: after a state is read back, the processor of a CLAP calls `updateHostDisplay` with `programChanged`, which clap-juce-extensions translates into that request; the other formats already follow the values, and an AU would wrongly take it for a preset. One accepted warning remains: given 3 MB of random bytes, clap-juce-extensions answers "loaded", because JUCE's `setStateInformation` returns no result; the processor nevertheless refuses that state and keeps its own (D-065).

## Behaviour to know

- **Automation**: applied at the start of each block. The prototype's CCs, on the other hand, take effect at their sample (D-061).
- **Voice mode change**: panic fade, then the new mode; held voices do not survive and notes received during the fade are ignored (D-064).
- **Clock, chain or resampling change**: the new engine is prepared on the message thread, then the audio thread fades and switches over. Offline, the switch happens at once, at the same sample in every render. The reported latency follows the engine (sinc: 20 samples at 48 kHz; Box and Retro: 0).
- **Separate outputs**: read at prepare time; an output enabled during playback stays silent until the next prepare (D-062).
- **Chipdrum kit and VBL**: a change rebuilds the kit on the audio thread, without allocation (a few hundred µs, D-052). Known limit, to be moved off the audio thread when the interface (M4) allows the kit to be edited.

## Not done in M3

| Item | Where | Why |
|---|---|---|
| Program Change, MIDI learn | M6 | Tied to presets and the interface |
| Sustain pedal (CC 64) | to be decided | Absent from the prototype |
| Sample-accurate automation | to be decided | The prototype has none; cost of splitting blocks |
| GEM interface | M4 | JUCE's generic editor in the meantime |
| PACE signing of the AAX | M8 | By Indus; the AAX is compiled in the CI, not validated |
| Steinberg VST3 validator | to be decided | pluginval can run it (`--vst3validator`) if it is built from the VST3 SDK (MIT) |

## Reproduce

```sh
cmake -S . -B build-plugin -DCMAKE_BUILD_TYPE=Release -DSTYM_BUILD_PLUGIN=ON
cmake --build build-plugin --parallel --target STYM2149_All STYM2149_CLAP stym_plugin_tests
ctest --test-dir build-plugin --output-on-failure -R '^plugin$'
pluginval --strictness-level 10 --validate build-plugin/src/plugin/STYM2149_artefacts/Release/VST3/STYM2149.vst3
cargo install --locked clap-validator --git https://github.com/free-audio/clap-validator --rev 152b9823e992d782c5c1fd33bca0295478b919aa
clap-validator validate build-plugin/src/plugin/STYM2149_artefacts/Release/CLAP/STYM2149.clap
```

Under Linux, JUCE needs the development packages of ALSA, JACK, FreeType, Fontconfig and X11; the processor tests, pluginval and clap-validator run under `xvfb-run` in the CI; clap-validator needs Rust 1.95 or newer.
