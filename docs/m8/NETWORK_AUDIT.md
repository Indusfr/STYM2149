# No network access, no telemetry (M8, B5)

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).

`STYM2149_SPEC.md` §10 asks that the plugin never access the network and send no telemetry. This note records how version 0.9.0 was checked, on 16/10/2026, at the M8 branch.

## Result

STYM2149 opens no network connection and sends nothing anywhere. Its only exchanges are with the host (audio, MIDI, parameters, state), with files the user picks, and with its own preferences file. The code that moved meter and status data from the audio thread to the editor was called "telemetry"; it never left the process, and it is now named `EngineStatus` (`src/plugincore/include/plugincore/EngineStatus.h`) to avoid any confusion.

## Checks

| Check | Method | Result |
|---|---|---|
| Project code | Search of `src`, `tools` and `third_party` for sockets, `juce::URL`, `WebInputStream`, `StreamingSocket`, `DatagramSocket`, `IPAddress`, `URL::launchInDefaultBrowser`, `Process::openDocument`, `ShellExecute`, `system`, `popen`, `xdg-open`, `juce_analytics`, `juce_osc` | None used. The Information window shows text only, with no link to open |
| Project libraries | `nm --undefined-only` on every static library of the project (`ymcore`, `ymengine`, `ymseq`, `ymio`, `gemui`, `stym_plugincore`, `stym_pluginui`, AtariAudio's machine) for `socket`, `connect`, `bind`, `listen`, `accept`, `getaddrinfo`, `sendto`, `recvfrom`, `getifaddrs` | No reference |
| JUCE options | `src/plugin/CMakeLists.txt` | `JUCE_WEB_BROWSER=0` (no web view), `JUCE_USE_CURL=0` (no libcurl). JUCE 8 has no splash screen and no usage reporting; the `juce_analytics` and `juce_osc` modules are not linked |
| Plugin binaries (Linux) | `nm -D --undefined-only` on the VST3, CLAP and LV2 libraries and the Standalone | They import `socket`, `connect` and a few others, all from `juce_core.cpp.o`: JUCE's `URL`, `WebInputStream` and `StreamingSocket` classes are compiled with the module but never called by STYM2149. No library such as libcurl is linked (`ldd`: fontconfig, freetype, ALSA, the C and C++ runtimes and their dependencies) |
| Standalone at run time (Linux) | `strace -f -e trace=socket,connect` for 8 seconds under Xvfb | One socket, `AF_UNIX`, connected to the X server (`/tmp/.X11-unix/X99`); no `AF_INET` or `AF_INET6` socket |
| Plugin tests at run time (Linux) | `strace -f -e trace=socket,connect` on `stym_plugin_tests` under Xvfb (26 test cases: processor, state, editor with every window open, tooltips in six languages, stress test with the editor opened and closed while playing) | 10 sockets, all `AF_UNIX` to the X server; no `AF_INET` or `AF_INET6` socket |
| Windows | Same sources and JUCE options; JUCE's `juce_core` links `wininet` and `ws2_32` for the same unused classes | Run-time check **TO CONFIRM**: during the DAW protocol (`docs/m8/TESTS_DAW.md`), Windows Resource Monitor > Network should list no TCP connection for the DAW process that comes from STYM2149 |

## What stays local

- Preferences: one settings file per user (D-069).
- Plugin state: saved by the host in its project.
- Files: only those the user opens or saves through the File menu and the windows that load samples.
- Build and CI only (never in the product): CMake downloads JUCE, clap-juce-extensions and Catch2 at pinned commits; `tools/fetch_refdata.sh` downloads the reference tunes for the tests; the CI downloads pluginval and builds clap-validator and the VST3 validator.

## Keeping it so

A new use of `juce::URL`, `WebInputStream`, sockets or a browser launch in `src` should be refused in review. The checks above can be repeated with the commands in the table.
