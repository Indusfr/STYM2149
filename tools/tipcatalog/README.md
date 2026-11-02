# Tooltip and interface catalogue

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).

The editor writes its tooltips and its interface (menus, buttons, labels, window titles, alerts) in English, the only language built into the plugin. Every other language is a gettext PO file, the format of translation tools such as Poedit: French, German, Spanish, Portuguese (Brazil) and Italian are `fr.po`, `de.po`, `es.po`, `pt.po` and `it.po` in `languages/` at the root of the sources. Each file holds the tooltip lines (entries without context), then the interface lines (entries of context "ui", `msgctxt "ui"`). The installers put them in a folder of the computer, where the plugins and the application read them each time the Interface language or Tooltip language menu of Options > Settings opens:

| System | Installed languages | Written by |
|---|---|---|
| Windows | `C:\ProgramData\STYM2149\Languages` | the Inno Setup installer |
| Linux | `~/.local/share/STYM2149/Languages` (also `STYM2149/Languages` in `$XDG_DATA_DIRS`) | `install.sh` |
| macOS | `/Library/Application Support/STYM2149/Languages` | no installer yet: copy the files by hand (TO DO) |

`src/pluginui/src/TipLanguage.cpp` translates a tooltip line by line through the file of the tooltip language, and `src/gemui/src/Language.cpp` every text the interface measures or draws through the "ui" entries of the interface language (the spaces around a line are kept, a text without a letter is not looked up). The tool of this folder keeps the sources in step:

| File | Role |
|---|---|
| `en.txt` | every English tooltip line, one per line: the source of `src/pluginui/src/TipCatalog.cpp` (template and tests) |
| `ui.txt` | every English line of the interface, one per line, written into `TipCatalog.cpp` too and into the PO files as entries of context "ui" |
| `ui_kept.txt` | the lines of the interface kept in English on purpose (names of the product, its features, machines, keys and kits): not translated, read by the tests only |
| `../../languages/*.po` | one language each: header `Language` (code) and `X-Language-Name` (name in the menu), then an entry per English line (`msgid`) with its translation (`msgstr`), the interface lines with `msgctxt "ui"` |
| `../../languages/template.pot` | every English line with an empty translation, to start a new language |

A part of a line that varies (a number, a name) is written `{1}` to `{9}` in the English line and moved where it belongs in each translation. When several lines match, the one with the most fixed text wins. In the interface, the parts are translated too when a line of their own matches them ("Mute {1}" with "Track {1}" gives "Mute Track 2" in the language), one level deep. Only characters of the GEM font are allowed: printable ASCII and the accented letters of the Atari ST character set (capitals such as È, Ê, Á, Í, Ó, Ú and Ô do not exist there and are written without the accent; Italian "È" is written "E'").

## Add or change a tooltip

1. Write the English line in `en.txt` and run `python3 tools/tipcatalog/tipcatalog.py`. It rewrites `TipCatalog.cpp` and `template.pot`, adds the line to each PO file of `languages/` with an empty translation and drops the lines no longer used.
2. Write the translation in the `msgstr` of the line in each PO file, then run the tool again: it checks placeholders, characters and that every line is translated, and puts the French non-breaking spaces before `:` `;` `?` `!` (`--check` only checks).
3. Build and run the plugin tests: `tests/plugin/test_tooltips.cpp` opens the editor's windows in several states and fails with "not translated in …" for every tooltip line missing from a PO file. With `STYM_TIPS_DUMP=lines.txt`, it also writes every English line it saw to `lines.txt`.

## Add or change a text of the interface

1. Write the English line in `ui.txt` (`{1}` to `{9}` for the parts that vary; a name that must stay in English goes into `ui_kept.txt` instead) and run the tool, then translate it in each PO file as for a tooltip.
2. A name the user gives (a patch, a sample, a track, an instrument) is never looked up: the widget that shows it is marked raw (`setRaw`), a menu entry has `raw` set, and a text already translated is drawn inside a `gemui::RawText` scope.
3. `tests/plugin/test_ui_language.cpp` opens the editor's windows and menus in several states, records every text drawn and fails with "not in the catalogue" for a line that is neither in `ui.txt` nor in `ui_kept.txt` and has words to translate, and with "not translated in …" for a line missing from a PO file. With `STYM_UI_TEXTS=texts.txt`, the UI and plugin tests also write every text they drew (`tests/gemui/ui_texts_listener.cpp`); `STYM_UI_TEXTS_CHECK=texts.txt` then has the plugin test check those texts too, which reach states it does not draw itself (alerts, MIDI monitor, chip conformity). The file names and names given by the tests show up there as "not in the catalogue": only the others are missing lines.
4. A part between quotes or brackets (`Delete bank "{1}" from the library?`, `MIDI learn: [{1}] armed...`) is a name, never looked up; the other parts are translated by a line of their own when one matches them.

A new language shipped with STYM2149 is one more PO file in `languages/` (a copy of `template.pot` with its header filled in): the installers take every `.po` file of the folder.

The translations were written without a native speaker's review (TO CONFIRM).

## Languages of the users

Users add or correct a language without this tool: the plugin also reads the PO files of the `Languages` folder next to its preferences (`%APPDATA%\STYM2149\Languages`, `~/Library/Application Support/STYM2149/Languages`, `~/.config/STYM2149/Languages`) and lists them in the menu by name, after English. The menu entry Languages folder... writes `template.pot` and a `README.txt` there, then opens the folder. A user's file whose code is an installed one (`fr.po`) corrects it line by line. A language sent back for inclusion goes into `languages/`.
