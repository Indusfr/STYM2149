# STYM2149

*[Version française](README.fr.md)*

Software instrument (VST3, CLAP, LV2 and Standalone; AU and AAX to come) that emulates the YM2149F sound chip of the Atari ST, with an Atari GEM interface.

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3.

- Up to 16 tracks: one chip with 3 voices, two chips (2 × 3 voices) or 16 tracks; Atari ST, ZX Spectrum and Amstrad CPC clocks; Raw, ST and STE output chains.
- Timer effects of the Atari ST (SID, Sync Buzzer, Digidrums), tuned buzzer, STE DMA sound, ADSR, vibrato, an LFO per track, arpeggios synchronised to the tempo, slides, an echo of the notes in every mode and a delay in 16 channels.
- 16-track step sequencer with Song, mixer, 32 instruments and instrument sequences, MIDI learn.
- 483 host parameters, with stable IDs; patterns, songs and settings are saved with the project.
- Files: YM and SNDH player, MIDI import and export, WAV and stems export, VBL, YM3, YM6 and SNDH export for a real ST or STE, XM import.
- No network access, no telemetry: STYM2149 never connects to anything.

The [user manual](docs/MANUAL.md) explains installation and every window.

## Install

Version 0.9.0 is the first public build, for Windows and Linux (x64). macOS and AAX come with 1.0.0.

### Windows 10 or 11 (x64)

Run `STYM2149-0.9.0-windows-x64-setup.exe`. It installs for all users and asks for administrator rights:

| Component | Folder |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\STYM2149.vst3` |
| CLAP | `C:\Program Files\Common Files\CLAP\STYM2149.clap` |
| Standalone application | `C:\Program Files\PRISM\STYM2149` (can be changed during installation) |

These are the standard plugin folders that DAWs scan by default; if STYM2149 does not appear, rescan the plugins.

The installer is not code-signed. Windows SmartScreen may show "Windows protected your PC": click **More info**, then **Run anyway**. Uninstall from Settings > Apps.

### Linux (x86_64)

```sh
tar xzf STYM2149-0.9.0-linux-x86_64.tar.gz
cd STYM2149-0.9.0-linux-x86_64
./install.sh               # copies to ~/.vst3, ~/.lv2, ~/.clap and ~/.local/bin
./install.sh --uninstall   # removes them
```

## Build and test

Requirements: CMake 3.22 or later, a C++20 compiler (GCC 11 or later, Clang 16, MSVC 2022 or Xcode 15), Git. Ninja for the `linux` preset. Cargo only for the ym2149-rs oracle.

```sh
tools/fetch_refdata.sh                    # reference tunes (ym2149-rs, pinned commit) in refdata/
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release [-DSTYM_ORACLE_CRATE=ON]
cmake --build build --parallel
ctest --test-dir build --output-on-failure   # on Windows: add -C Release
```

With presets: `cmake --preset linux` (Ninja, Release, folder `build-linux`) or `cmake --preset windows` (Visual Studio 2022 x64, folder `build-windows`), then `cmake --build --preset linux` or `windows`.

| CTest test | Content |
|---|---|
| `unit` | Chip, output chains, MFP timers, voice engine and sequencer, patterns, Song and undo, parity with the prototype, plugin core, every file format (YM, LHA, JSON, MIDI, WAV, ZIP, YM3, YM6, SNDH, XM) |
| `ui` | GEM interface: text and fonts, alerts, menus, tooltips, floating windows, every editor window; visual regression of the prototype scenes (under 1 % of pixels different per scene) and of the STYM2149 scenes (no pixel different) |
| `realtime` | No allocation while rendering (`-DSTYM_RTSAN=ON` with Clang 20: RealtimeSanitizer) |
| `oracle` | Chip and timer effects against AtariAudio 1.26, LHA against AtariAudio's unpacker |
| `oracle_s9` | HQ chain against AtariAudio's YM player |
| `oracle_crate` | Against the ym2149-rs 0.9.1 crate (`-DSTYM_ORACLE_CRATE=ON`) |
| `plugin` | JUCE processor and editor, tooltip translations, stress test (`-DSTYM_BUILD_PLUGIN=ON`) |

Without `refdata/`, the tests on tunes are skipped, unless `STYM_REQUIRE_REFDATA=1` is set (as in CI).

### Plugin

```sh
cmake -S . -B build-plugin -DCMAKE_BUILD_TYPE=Release -DSTYM_BUILD_PLUGIN=ON
cmake --build build-plugin --parallel --target STYM2149_All STYM2149_CLAP stym_plugin_tests
ctest --test-dir build-plugin --output-on-failure -R '^plugin$'   # Linux without a display: xvfb-run -a ctest ...
```

CMake downloads JUCE 8.0.15 and clap-juce-extensions at verified commits. Formats: VST3, LV2, CLAP and Standalone on Linux; VST3, CLAP and Standalone on Windows (AAX is compiled but not shipped yet); AU, VST3, CLAP and Standalone on macOS. Output in `build-plugin/src/plugin/STYM2149_artefacts/Release/`. On Linux, JUCE needs the development packages of ALSA, JACK, FreeType, Fontconfig and X11; the editor tests need Xvfb without a display.

Packages: `packaging/linux/make_tarball.sh` builds the Linux archive; `packaging/windows/STYM2149.iss` is the Inno Setup 6 script of the Windows installer.

### Tools

- `ym2wav`: renders a YM file (LHA-compressed or not) to WAV.
- `ymbench`: CPU load of the voice engine (`--seq --notes 6 --noise` for the worst case, `--hash` for a fingerprint of the output).
- `gemshot`: renders the interface to PNG without a host or JUCE (visual tests).
- `tools/gemfont`: GEM fonts taken from the prototype and EmuTOS.

## Documentation

- [User manual](docs/MANUAL.md) ([français](docs/MANUAL.fr.md))
- [Architecture guide](docs/ARCHITECTURE.md)
- [Changelog](CHANGELOG.md)

## Licence

GNU Affero General Public License v3 (see [LICENSE](LICENSE)). The emulation builds on AtariAudio by Leonard/Oxygene (Arnaud Carré) and ym2149-rs by slippyex, both MIT; SNDH files are played by AtariAudio's Atari machine with Karl Stenerud's Musashi 68000 emulator (MIT); LHA decompression is ported from pylzh by Hirofumi Ukawa (Apache-2.0); the GEM fonts and the desktop icons come from EmuTOS (GPL); the plugin uses JUCE 8 under the AGPLv3. Every component and its licence: [THIRD_PARTY.md](THIRD_PARTY.md); licence texts in [third_party/licenses](third_party/licenses).
