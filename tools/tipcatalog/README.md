# Tooltip catalogue

Programming: Pascal "Indus" Nowak (with Claude's help) — Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).

The editor writes its tooltips in English, the only language built into the plugin. Every other language is a gettext PO file, the format of translation tools such as Poedit: French, German, Spanish, Portuguese (Brazil) and Italian are `fr.po`, `de.po`, `es.po`, `pt.po` and `it.po` in `languages/` at the root of the sources. The installers put them in a folder of the computer, where the plugins and the application read them each time the Language menu of Options > Settings opens:

| System | Installed languages | Written by |
|---|---|---|
| Windows | `C:\ProgramData\STYM2149\Languages` | the Inno Setup installer |
| Linux | `~/.local/share/STYM2149/Languages` (also `STYM2149/Languages` in `$XDG_DATA_DIRS`) | `install.sh` |
| macOS | `/Library/Application Support/STYM2149/Languages` | no installer yet: copy the files by hand (TO DO) |

`src/pluginui/src/TipLanguage.cpp` translates a tooltip line by line through the file of the chosen language. The tool of this folder keeps the sources in step:

| File | Role |
|---|---|
| `en.txt` | every English tooltip line, one per line: the source of `src/pluginui/src/TipCatalog.cpp` (template and tests) |
| `../../languages/*.po` | one language each: header `Language` (code) and `X-Language-Name` (name in the menu), then an entry per English line (`msgid`) with its translation (`msgstr`) |
| `../../languages/template.pot` | every English line with an empty translation, to start a new language |

A part of a line that varies (a number, a name) is written `{1}` to `{9}` in the English line and moved where it belongs in each translation. When several lines match, the one with the most fixed text wins. Only characters of the GEM font are allowed: printable ASCII and the accented letters of the Atari ST character set (capitals such as È, Ê, Á, Í, Ó, Ú and Ô do not exist there and are written without the accent; Italian "È" is written "E'").

## Add or change a tooltip

1. Write the English line in `en.txt` and run `python3 tools/tipcatalog/tipcatalog.py`. It rewrites `TipCatalog.cpp` and `template.pot`, adds the line to each PO file of `languages/` with an empty translation and drops the lines no longer used.
2. Write the translation in the `msgstr` of the line in each PO file, then run the tool again: it checks placeholders, characters and that every line is translated, and puts the French non-breaking spaces before `:` `;` `?` `!` (`--check` only checks).
3. Build and run the plugin tests: `tests/plugin/test_tooltips.cpp` opens the editor's windows in several states and fails with "not translated in …" for every tooltip line missing from a PO file. With `STYM_TIPS_DUMP=lines.txt`, it also writes every English line it saw to `lines.txt`.

A new language shipped with STYM2149 is one more PO file in `languages/` (a copy of `template.pot` with its header filled in): the installers take every `.po` file of the folder.

The translations were written without a native speaker's review (TO CONFIRM).

## Languages of the users

Users add or correct a language without this tool: the plugin also reads the PO files of the `Languages` folder next to its preferences (`%APPDATA%\STYM2149\Languages`, `~/Library/Application Support/STYM2149/Languages`, `~/.config/STYM2149/Languages`) and lists them in the menu by name, after English. The menu entry Languages folder... writes `template.pot` and a `README.txt` there, then opens the folder. A user's file whose code is an installed one (`fr.po`) corrects it line by line. A language sent back for inclusion goes into `languages/`.
