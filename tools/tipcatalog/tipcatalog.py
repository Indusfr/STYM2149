#!/usr/bin/env python3
# STYM2149 — catalogue of the tooltips and of the interface texts: checks the translations, writes
# src/pluginui/src/TipCatalog.cpp and keeps the PO files of languages/ up to date.
# Programming: Pascal "Indus" Nowak (with Claude's help).
# Copyright © 2026 Indus / PRISM. Licence: AGPLv3 (see LICENSE).
#
# Usage: python3 tools/tipcatalog/tipcatalog.py [--check]
# en.txt holds one English tooltip line per line, ui.txt one English line of the interface (menus, labels, buttons,
# window titles, alerts) per line: English is the only language built into the plugin. Every other language is a
# gettext PO file of languages/ (fr.po, de.po, es.po, pt.po, it.po), installed with STYM2149; its interface
# lines are the entries of context "ui" (msgctxt), after the tooltips. The tool writes TipCatalog.cpp (the
# English lines, sorted) and languages/template.pot, then rewrites each PO file with one entry per English line, in the
# same order: lines no longer used are dropped, new ones come with an empty translation. Each
# translation must keep the placeholders {1} to {9} of its English line, use only characters of the GEM font and not
# be empty. French gets a non-breaking space before : ; ? ! and inside guillemets (tooltips wrap at spaces only).
# With --check, nothing is written, and a file that is not up to date is a problem.
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'src', 'pluginui', 'src', 'TipCatalog.cpp')
LANGUAGES = os.path.join(ROOT, 'languages')
# Printable ASCII, the non-breaking space and the accented letters and signs of the Atari ST character set present in
# the GEM font.
ALLOWED = set(chr(c) for c in range(0x20, 0x7F)) | set('\u00a0') | \
    set('♪•ù²ÖÄÜß´ÇüéâäàçêëèïîìÉôöòûÿáíóúñÑªº¿¡«»ãõœŒÀÃÕ°')


def read_lines(path):
    with open(path, encoding='utf-8') as f:
        lines = f.read().split('\n')
    return lines[:-1] if lines and lines[-1] == '' else lines


def unquote(s):
    s = s.strip()
    if len(s) < 2 or s[0] != '"' or s[-1] != '"':
        raise ValueError(f'not a quoted string: {s}')
    out, i, s = [], 0, s[1:-1]
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            i += 1
            out.append({'n': '\n', 't': '\t', 'r': ''}.get(s[i], s[i]))
        else:
            out.append(c)
        i += 1
    return ''.join(out)


def quote(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t') + '"'


def read_po(path):
    # Header fields and entries (msgctxt, msgid, msgstr) of a PO file, in file order, the context '' when the entry has
    # none; comments are dropped.
    entries, ctxt, msgid, msgstr, field = [], '', None, None, None

    def flush():
        if msgid is not None and msgstr is not None:
            entries.append((ctxt, msgid, msgstr))

    for raw in read_lines(path):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('msgctxt '):
            flush()
            ctxt, msgid, msgstr, field = unquote(line[8:]), None, None, 'ctxt'
        elif line.startswith('msgid '):
            if msgstr is not None:
                flush()
                ctxt = ''
            msgid, msgstr, field = unquote(line[6:]), None, 'id'
        elif line.startswith('msgstr '):
            msgstr, field = unquote(line[7:]), 'str'
        elif line.startswith('"'):
            if field == 'ctxt':
                ctxt += unquote(line)
            elif field == 'id':
                msgid += unquote(line)
            elif field == 'str':
                msgstr += unquote(line)
        else:
            raise ValueError(f'{path}: unexpected line: {raw}')
    flush()
    header = {}
    for c, msgid, msgstr in entries:
        if msgid == '' and c == '':
            for h in msgstr.split('\n'):
                if ':' in h:
                    k, v = h.split(':', 1)
                    header[k.strip()] = v.strip()
    return header, [(c, i, s) for c, i, s in entries if i != '']


def french(t):
    t = re.sub(r' ([:;?!])', '\u00a0\\1', t)
    return t.replace('« ', '«\u00a0').replace(' »', '\u00a0»')


def po_text(code, name, rows, ui_rows, template=False):
    if template:
        head = ('# STYM2149 tooltips and interface: template of a new language.\n'
                '# Programming: Pascal "Indus" Nowak (with Claude\'s help).\n'
                '# Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).\n'
                '#\n'
                '# Written by tools/tipcatalog/tipcatalog.py: every English line, with an empty translation; the\n'
                '# tooltips first, then the interface texts (context "ui").\n')
    else:
        head = (f'# STYM2149 tooltips and interface: {name} ({code}), installed with STYM2149.\n'
                '# Programming: Pascal "Indus" Nowak (with Claude\'s help).\n'
                '# Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).\n'
                '#\n'
                '# Kept up to date by tools/tipcatalog/tipcatalog.py, which gives it one entry per English line of\n'
                '# tools/tipcatalog/en.txt (the tooltips), then one per line of tools/tipcatalog/ui.txt (the interface,\n'
                '# context "ui"). Translations by Claude, to be reviewed by a native speaker (TO CONFIRM).\n')
    out = [head, 'msgid ""\nmsgstr ""\n', '"Project-Id-Version: STYM2149\\n"\n', f'"Language: {code}\\n"\n',
           f'"X-Language-Name: {name}\\n"\n', '"MIME-Version: 1.0\\n"\n',
           '"Content-Type: text/plain; charset=UTF-8\\n"\n', '"Content-Transfer-Encoding: 8bit\\n"\n']
    for en, tr in rows:
        out.append(f'\nmsgid {quote(en)}\nmsgstr {quote(tr)}\n')
    for en, tr in ui_rows:
        out.append(f'\nmsgctxt "ui"\nmsgid {quote(en)}\nmsgstr {quote(tr)}\n')
    return ''.join(out)


def esc(t):
    return t.replace('\\', '\\\\').replace('"', '\\"').replace('\u00a0', '\\u00A0')


def literal(t, indent, width=118):
    # One C++ string literal, cut at spaces into adjacent literals so that no source line passes the width.
    s = esc(t)
    room = width - indent - 3
    pieces = []
    while len(s) > room:
        cut = s.rfind(' ', 0, room)
        if cut <= 0:
            break
        pieces.append(s[:cut + 1])
        s = s[cut + 1:]
    pieces.append(s)
    return ('\n' + ' ' * indent).join(f'"{p}"' for p in pieces)


HEAD = '''// STYM2149 — pluginui: catalogue of the tooltips and of the interface texts
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// GENERATED by tools/tipcatalog/tipcatalog.py from tools/tipcatalog/en.txt and ui.txt: edit those files, not this one.
// Every English tooltip line, then every English line of the interface, sorted: the only language built in.
// Placeholders {1} to {9} stand for parts of the line that vary (TipLanguage.h). The other languages are the PO files
// of languages/, installed with STYM2149. tests/plugin/test_tooltips.cpp checks that every tooltip the editor
// shows is here and in each of those files, tests/plugin/test_ui_language.cpp the same for the interface texts.
#include "pluginui/TipLanguage.h"

#include <iterator>

namespace stym::pluginui::tipcatalog {

const char* const kLines[] = {
'''
MIDDLE = '''};
const size_t kLineCount = std::size(kLines);

const char* const kUiLines[] = {
'''
TAIL = '''};
const size_t kUiLineCount = std::size(kUiLines);

}  // namespace stym::pluginui::tipcatalog
'''


def check_line(where, en, tr):
    problems = []
    pe, pt = sorted(set(re.findall(r'\{\d\}', en))), sorted(set(re.findall(r'\{\d\}', tr)))
    if pe != pt:
        problems.append(f'{where}: placeholders {pt} instead of {pe}: {tr}')
    chars = set(tr) - ALLOWED
    if chars:
        problems.append(f'{where}: characters outside the GEM font {sorted(chars)}: {tr}')
    if not tr.strip():
        problems.append(f'{where}: not translated: {en}')
    return problems


def main():
    write = '--check' not in sys.argv[1:]
    en = read_lines(os.path.join(HERE, 'en.txt'))
    ui = read_lines(os.path.join(HERE, 'ui.txt'))
    problems = []
    for name_, lines in (('en.txt', en), ('ui.txt', ui)):
        if len(set(lines)) != len(lines):
            problems.append(f'{name_}: duplicate lines')
        if any(t != t.strip(' ') or not t for t in lines):
            problems.append(f'{name_}: a line is empty or starts or ends with a space (looked up without them)')
    english, interface = sorted(set(en)), sorted(set(ui))
    outputs = {}

    body = '\n'.join(f'    {literal(t, 4)},' for t in english)
    ui_body = '\n'.join(f'    {literal(t, 4)},' for t in interface)
    outputs[OUT] = HEAD + body + '\n' + MIDDLE + ui_body + '\n' + TAIL
    outputs[os.path.join(LANGUAGES, 'template.pot')] = po_text('', '', [(t, '') for t in english],
                                                               [(t, '') for t in interface], template=True)

    for path in sorted(glob.glob(os.path.join(LANGUAGES, '*.po'))):
        name_ = os.path.basename(path)
        header, entries = read_po(path)
        code = header.get('Language') or name_[:-3]
        name = header.get('X-Language-Name') or code
        known = {}
        for c, i, s in entries:
            known.setdefault((c, i), s)
        used = {('', t) for t in english} | {('ui', t) for t in interface}
        for c, i in known:
            if (c, i) not in used:
                print(f'{name_}: line no longer used, dropped: {"[" + c + "] " if c else ""}{i}')

        def rows_of(ctxt, lines):
            rows = []
            for t in lines:
                tr = known.get((ctxt, t), '')
                if code.split('_')[0].split('-')[0] == 'fr':
                    tr = french(tr)
                problems.extend(check_line(f'{name_}{" [ui]" if ctxt else ""}', t, tr))
                rows.append((t, tr))
            return rows

        outputs[path] = po_text(code, name, rows_of('', english), rows_of('ui', interface))

    for path, text in outputs.items():
        try:
            with open(path, encoding='utf-8', newline='') as f:
                same = f.read() == text
        except FileNotFoundError:
            same = False
        if same:
            continue
        rel = os.path.relpath(path, ROOT)
        if write:
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
            print(f'{rel} written')
        else:
            problems.append(f'{rel} is not up to date: run tools/tipcatalog/tipcatalog.py')
    for p in problems:
        print(p)
    if problems:
        print(f'{len(problems)} problems')
        return 1
    print(f'{len(english)} English tooltip lines, {len(interface)} interface lines, {len(outputs) - 2} languages OK')
    return 0


if __name__ == '__main__':
    sys.exit(main())
