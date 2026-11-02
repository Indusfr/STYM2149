// STYM2149 — plugin with JUCE: every text of the interface is in the catalogue and translated (Indus, 28/10/2026
// 03:06).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The editor's windows are opened and drawn in several states (voice modes, machines, generators of track 1), its
// menus opened, the entries of every menu read: each text measured or drawn in the interface language is recorded
// (gemui::recordUiTexts). Each of its lines must be a line of the catalogue (tools/tipcatalog/ui.txt), parts of its
// placeholders included, or have nothing to translate (numbers, units, note names, codes of up to four capitals), or be
// kept in English on purpose (tools/tipcatalog/ui_kept.txt: product and preset names). Each catalogue line must be
// translated by the five PO files installed with STYM2149 (languages/ in the sources), with characters of the GEM font.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gemui/Controls.h"
#include "gemui/Font.h"
#include "gemui/Language.h"
#include "gemui/Menu.h"
#include "plugincore/MaxYmExport.h"
#include "plugincore/TTrakExport.h"
#include "plugincore/TTrakImport.h"
#include "plugincore/XmImport.h"
#include "plugincore/YmtExport.h"
#include "plugincore/YmtImport.h"
#include "pluginui/TipLanguage.h"

using namespace stym;
using namespace stym::plugin;

namespace {

const char* const kInstalled[] = {"fr", "de", "es", "pt", "it"};

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

pluginui::TipFile language(const std::string& code) {
    auto file = pluginui::parseTipFile(readFile(std::string(STYM_LANGUAGES_DIR) + "/" + code + ".po"), code);
    REQUIRE(file);
    return *file;
}

// The lines kept in English on purpose, matched as the catalogue's lines are ({1} to {9} stand for any part).
pluginui::TipBook keptBook() {
    pluginui::TipFile kept;
    kept.id = "kept";
    const std::string text = readFile(std::string(STYM_TIPCATALOG_DIR) + "/ui_kept.txt");
    for (size_t from = 0; from < text.size();) {
        size_t nl = text.find('\n', from);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(from, nl - from);
        if (!line.empty() && line.back() == '\r') line.pop_back();  // a checkout with CRLF line ends (Windows)
        if (!line.empty() && line[0] != '#') kept.uiLines.emplace_back(line, line);
        from = nl + 1;
    }
    REQUIRE(kept.uiLines.size() > 10);
    return pluginui::TipBook::interfaceBook(kept);
}

// Nothing to translate: every word is a letter, a unit, a note name or a code of up to four capitals.
bool inert(const std::string& line) {
    static const std::set<std::string, std::less<>> words = {"Hz", "kHz", "MHz", "dB", "ms", "st", "ct", "ch", "tk",
                                                             "Do", "Re", "Mi", "Fa", "Sol", "La", "Si"};
    static const std::regex word("[A-Za-z]+");
    for (auto it = std::sregex_iterator(line.begin(), line.end(), word); it != std::sregex_iterator(); ++it) {
        const std::string w = it->str();
        if (w.size() == 1 || words.count(w)) continue;
        bool caps = w.size() <= 4;
        for (const char c : w) caps = caps && c >= 'A' && c <= 'Z';
        if (!caps) return false;
    }
    return true;
}

std::string trimmed(std::string_view s) {
    const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string_view::npos ? std::string() : std::string(s.substr(a, b + 1 - a));
}

void addLines(std::set<std::string>& lines, std::string_view text) {
    for (size_t from = 0; from <= text.size();) {
        const size_t nl = text.find('\n', from);
        const std::string line = trimmed(text.substr(from, nl == std::string_view::npos ? nl : nl - from));
        if (!line.empty()) lines.insert(line);
        if (nl == std::string_view::npos) break;
        from = nl + 1;
    }
}

void setPlain(StymProcessor& p, const std::string& id, float v) {
    const int i = findParam(id);
    REQUIRE(i >= 0);
    auto& param = p.parameter(i);
    param.setValueNotifyingHost(param.convertTo0to1(v));
}

// The entries of a menu, submenus included, as the menu would show them.
void readMenu(const std::vector<gemui::MenuItem>& items) {
    for (const gemui::MenuItem& m : items) {
        if (!m.raw) gemui::uiText(m.label);
        readMenu(m.sub);
    }
}

void collect(StymEditor& editor) {
    pluginui::MainWindow& w = editor.window();
    editor.frame();
    w.desktop().paint();
    for (int title = 0; title < 4; ++title) {
        w.openMenu(title);
        w.desktop().paint();
        if (const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().firstPopup())) readMenu(list->items());
        w.desktop().closePopups();
    }
    for (const std::string_view id : pluginui::SettingsWindow::menuIds()) {
        int checked = -1;
        readMenu(w.settingsWindow().items(id, checked));
    }
    if (const auto* fig = dynamic_cast<const gemui::Button*>(w.sequencer().widgetOf("seqFig")); fig && fig->onClick) {
        fig->onClick();
        w.desktop().paint();
        w.desktop().closePopups();
    }
}

}  // namespace

TEST_CASE("Interface language: every text is in the catalogue and translated, with characters of the GEM font",
          "[editor][language]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    std::set<std::string> lines;
    // The texts go to the sink set before too (the listener of STYM_UI_TEXTS).
    auto before = std::make_shared<std::function<void(std::string_view)>>();
    *before = gemui::recordUiTexts([&lines, before](std::string_view s) {
        addLines(lines, s);
        if (*before) (*before)(s);
    });
    {
        StymProcessor p;
        p.prepareToPlay(48000, 512);
        StymEditor editor(p, std::make_unique<juce::PropertiesFile>(prefs.getFile(), juce::PropertiesFile::Options{}));
        pluginui::MainWindow& w = editor.window();
        for (auto* win : std::initializer_list<gemui::Widget*>{
                 &w.sequencer(), &w.songWindow(), &w.mixer(), &w.arpeggiator(), &w.modulation(), &w.drumKit(),
                 &w.patches(), &w.instruments(), &w.dmaWindow(), &w.recorderWindow(), &w.ymPlayerWindow(),
                 &w.digidrums(), &w.slideCurve(), &w.keyboardWindow(), &w.echoWindow()})
            static_cast<gemui::FloatWindow*>(win)->open();
        w.settingsWindow().show();
        w.openScope(0);
        p.editExtras([](plugin::NonParamState& s) {
            s.tracks[0].patch.ornamentOn = true;
            s.tracks[0].patch.echo.on = true;
            s.tracks[0].patch.envTuned = true;  // "tuned x1/1" in place of EP= with the Sync Buzzer
        });
        setPlain(p, "delayOn", 1.0f);
        for (int mode = 0; mode < 4; ++mode)  // 16 ch, 3ch, 2x3ch, 32 ch
            for (int computer = 0; computer < 2; ++computer) {
                setPlain(p, "voiceMode", float(mode));
                setPlain(p, "computer", float(computer));
                for (int g = 0; g < 6; ++g) {
                    setPlain(p, "t01_generator", float(g));
                    collect(editor);
                }
            }
        setPlain(p, "voiceMode", 0.0f);
        setPlain(p, "delaySync", 0.0f);
        collect(editor);
        w.doDemo();
        collect(editor);
        // The chip conformity alert of the demo pattern in 32 ch (its compliance lines).
        w.showChipAlert();
        w.desktop().paint();
        while (gemui::Widget* modal = w.desktop().topModal()) w.desktop().closeModal(*modal);
    }
    gemui::recordUiTexts(*before);
    // With STYM_UI_TEXTS_CHECK=<file>, the texts of a STYM_UI_TEXTS file are checked too (that of a run of the UI tests
    // has states this test does not draw: tools/tipcatalog/README.md).
    int limit = 40;
    if (const char* extra = std::getenv("STYM_UI_TEXTS_CHECK"); extra && *extra) {
        const std::string text = readFile(extra);
        std::string s;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                s += text[++i] == 'n' ? '\n' : text[i];
            } else if (text[i] == '\n') {
                addLines(lines, s);
                s.clear();
            } else {
                s += text[i];
            }
        }
        addLines(lines, s);
        limit = 1000;
    }
    INFO(lines.size() << " lines drawn");
    REQUIRE(lines.size() > 200);

    // Each line: in the catalogue (with the parts of its placeholders), nothing to translate, or kept on purpose.
    const pluginui::TipBook kept = keptBook();
    const auto known = [&kept](const std::string& line) {
        return inert(line) || kept.line(line).has_value() || pluginui::uiPlaceholderParts(line).has_value();
    };
    std::set<std::string> catalogued;
    int unknown = 0;
    for (const std::string& line : lines) {
        if (inert(line) || kept.line(line)) continue;
        const auto parts = pluginui::uiPlaceholderParts(line);
        if (!parts) {
            if (++unknown <= limit) UNSCOPED_INFO("not in the catalogue: " << line);
            continue;
        }
        catalogued.insert(line);
        for (const std::string& part : *parts) {
            const std::string t = trimmed(part);
            if (t.empty() || known(t)) continue;
            if (++unknown <= limit) UNSCOPED_INFO("part not in the catalogue: \"" << t << "\" in: " << line);
        }
    }
    CHECK(unknown == 0);

    std::vector<pluginui::TipBook> books;
    std::vector<pluginui::TipFile> files;
    for (const char* code : kInstalled) files.push_back(language(code));
    for (const pluginui::TipFile& f : files) books.push_back(pluginui::TipBook::interfaceBook(f));
    int missing = 0, badGlyphs = 0;
    for (const std::string& line : catalogued)
        for (size_t l = 0; l < books.size(); ++l) {
            const auto t = books[l].line(line);
            if (!t) {
                if (++missing <= 20) UNSCOPED_INFO("not translated in " << kInstalled[l] << ": " << line);
                continue;
            }
            for (size_t pos = 0; pos < t->size();) {
                const char32_t c = gemui::nextCodepoint(*t, pos);
                if (c != '?' && gemui::Font::glyph(c) == '?' - 0x20 && ++badGlyphs <= 20)
                    UNSCOPED_INFO("no glyph U+" << std::hex << uint32_t(c) << std::dec << " in: " << *t);
            }
        }
    CHECK(missing == 0);
    CHECK(badGlyphs == 0);
}

TEST_CASE("Interface language: each catalogue line has a translation in every installed language",
          "[language]") {
    for (const char* code : kInstalled) {
        const pluginui::TipFile f = language(code);
        INFO(code);
        CHECK(f.uiLines.size() == pluginui::tipcatalog::kUiLineCount);
    }
}

TEST_CASE("Interface language: the maxYMiser export alert is translated, its remark bullets included",
          "[language]") {
    plugin::MaxYmExport x;
    x.steps = 3;
    x.patterns = 4;
    x.instruments = 5;
    x.samples = 1;
    x.loops = true;
    x.song.speed = 6;
    x.song.repeat = 1;
    x.remarks = {"Frames played with the closest of the 32 instruments: 12.", "STE DMA sound left out.",
                 "Digidrum 2 played once, without its loop."};
    for (const char* code : kInstalled) {
        const pluginui::TipFile f = language(code);
        const pluginui::TipBook book = pluginui::TipBook::interfaceBook(f);
        for (const std::string& line : plugin::maxYmExportMessage(x, "A.MYS", "A.MYV")) {
            if (inert(line)) continue;  // the files named
            INFO(code << ": " << line);
            const auto t = book.line(line);
            REQUIRE(t);
            CHECK(*t != line);
            if (line.rfind("• ", 0) == 0) CHECK(t->rfind("• ", 0) == 0);
        }
    }
}

TEST_CASE("Interface language: the YM-Tracker import and export alerts are translated, their remarks included",
          "[language]") {
    plugin::RegisterImport r;
    r.title = "Demo";
    r.patterns = 3;
    r.entries = 4;
    r.instruments = 5;
    r.digidrums = true;
    r.samples = 2;
    r.remarks = {"Channels beyond the first three (one chip, Authentic 3ch): 3.",
                 "Patterns beyond 32: 2 Song positions left out.",
                 "Pattern breaks to a later row (played from row 0): 1.",
                 "Notes beyond 127 instruments, played without their effects: 4.",
                 "Arpeggios and envelope periods that start after their note's attack, played from it: 2.",
                 "Note effects on rows without a note of this pattern: 6.",
                 "3xx without a note before it in the pattern: 1.",
                 "Pitch macros beyond an octave and 127 period steps, clamped: 1 instrument.",
                 "Macros that do not repeat within 64 ticks, held at the 64th: 2 instruments.",
                 "SID rates beyond 1/4 to 8 times the note, clamped: 1 instrument.",
                 "Sync-Buzzer (Uxx) rates following the note: 2 instruments.",
                 "Digidrum hits of other channels, played on channel C: 7.",
                 "Digidrum hits on the same row as another: 1.",
                 "Digidrum hits beyond 8 samples: 3.",
                 "Digidrum hits without a sample in the bank: 2.",
                 "Digidrum samples cut to their 3 seconds: 1.",
                 "Tempos clamped to 20-300 BPM: 1.",
                 "BPM row tempos clamped to 32-255 BPM: 2.",
                 "Speed changes without a free CMD cell: 1.",
                 "Volume column effects left out: 4.",
                 "Volumes below the one of VOL 1 (8 of 15), played at it: 12 rows.",
                 "Unknown effects left out: 2.",
                 "Effects left out: 1xx (3), E9x (2)."};
    plugin::YmtExport x;
    x.positions = 6;
    x.patterns = 3;
    x.instruments = 4;
    x.samples = 2;
    x.loops = true;
    x.song.speed = 6;
    x.song.bpm = 125;
    x.song.restart = 2;
    x.remarks = {"The Song stops after 256 positions.",
                 "Notes played with the closest of the 16 instruments: 3.",
                 "Notes under the Digidrum hits, left out as the sequencer does: 2.",
                 "Notes beyond C-1 to B-8 moved by octaves: 1.",
                 "Note releases moved to the end of their row (the cell holds another effect): 4.",
                 "Effects without room in their cell: 5.",
                 "ADSR stages beyond 15 ticks, clamped: 1 instrument.",
                 "Buzzer ratios played at the nearest octave: 2 instruments.",
                 "Replay at 200 Hz written at 100 Hz (ticks of twice the length).",
                 "Rows longer than 31 ticks, shortened: 2.",
                 "Tempos beyond 32-255 BPM once in ticks, clamped: 1.",
                 "Swing left out.",
                 "Digidrum hits beyond 255 samples: 1.",
                 "Digidrum rates beyond 65535 Hz, clamped: 2.",
                 "Looped Digidrum slots played once: 1.",
                 "Tuning A4 left out (YM-Tracker plays A4 at 440 Hz).",
                 "STE DMA tracks left out.",
                 "Left out: GEN on held steps (2).",
                 "Left out: Chipdrum and DMA notes (3).",
                 "Left out: GEN Chip Voice on a Sync Buzzer (1).",
                 "Left out: chord notes (4).",
                 "Left out: Exx without the envelope (1).",
                 "Left out: buzzer fine tune (1).",
                 "Left out: volume tables under an ADSR (2).",
                 "Left out: noise steps of tables (1).",
                 "Left out: NP, MIX, GEN and FIX table rows (1).",
                 "Left out: Detune and Analog (1).",
                 "Left out: Digidrum hits on empty slots (2).",
                 "Left out: Echo (1)."};
    std::vector<std::string> lines = plugin::ymtImportMessage(r, "A.YMT");
    r.tempo = 140;
    for (const std::string& l : plugin::ymtImportMessage(r, "A.YMT")) lines.push_back(l);
    for (const std::string& l : plugin::ymtExportMessage(x, "A.YMT")) lines.push_back(l);
    x.loops = false;
    x.positions = 1;
    x.samples = 0;
    for (const std::string& l : plugin::ymtExportMessage(x, "A.YMT")) lines.push_back(l);
    // The refusals of the reader, of the gzip stream and of the conversion.
    for (const char* why : {"YM-Tracker song not imported", "Not a YM-Tracker song.", "The song is cut.",
                            "Unsupported number of channels.", "The song is damaged.", "Unsupported pattern length.",
                            "A pattern of the song is damaged.", "An instrument of the song is damaged.",
                            "Not a gzip stream.", "Unknown gzip compression method.", "Unknown gzip header flags.",
                            "The gzip header is cut.", "The compressed data are damaged or too large.",
                            "The gzip stream is cut.", "The gzip checksum does not match.",
                            "The gzip length does not match.", "Nothing to import: the song has no pattern.",
                            "Nothing to import: no notes on the first three channels.",
                            "YM-Tracker plays the three channels of one chip: switch to Authentic 3ch"})
        lines.emplace_back(why);
    for (const char* code : kInstalled) {
        const pluginui::TipFile f = language(code);
        const pluginui::TipBook book = pluginui::TipBook::interfaceBook(f);
        for (const std::string& line : lines) {
            if (line.empty() || inert(line) || line == "Demo") continue;  // the file and the song named
            INFO(code << ": " << line);
            const auto t = book.line(line);
            REQUIRE(t);
            CHECK(*t != line);
            if (line.rfind("• ", 0) == 0) CHECK(t->rfind("• ", 0) == 0);
        }
    }
}

TEST_CASE("Interface language: the TTrak import and export alerts are translated, their remarks included",
          "[language]") {
    plugin::RegisterImport r;
    r.title = "Demo";
    r.patterns = 3;
    r.entries = 4;
    r.instruments = 5;
    r.digidrums = true;
    r.samples = 2;
    std::vector<std::string> lines = plugin::ttrakImportMessage(r, "A.TPM");
    plugin::TtrakExport x;
    x.positions = 6;
    x.patterns = 3;
    x.instruments = 4;
    x.samples = 2;
    x.loops = true;
    x.module.rate = 50;
    x.module.restart = 2;
    x.remarks = {"Frames that TTrak plays otherwise: 12 of 400.",
                 "Frames beyond TTrak's pitch tables (or a timer too slow): 3.",
                 "The song stops where TTrak runs out of positions (127) or patterns (128).",
                 "The song was longer than one hour and has been cut.",
                 "Replayed at 50 Hz instead of 60 Hz (TTrak's rates: 50, 100, 150, 200 Hz).",
                 "Notes played with the closest of the 32 instruments: 7.",
                 "Notes longer than an instrument's tables, played approximately: 2.",
                 "Notes played louder (their row has no room for the volume effect): 1.",
                 "Envelope restarts within a row left out: 4.",
                 "Digidrums past the 32 samples of TTrak left out: 1.",
                 "Digidrum 3 played once, without its loop.",
                 "STE DMA sound left out."};
    for (const std::string& l : plugin::ttrakExportMessage(x, "A.TPM")) lines.push_back(l);
    x.loops = false;
    x.positions = 1;
    x.samples = 1;
    for (const std::string& l : plugin::ttrakExportMessage(x, "A.TPM")) lines.push_back(l);
    // The refusals of the reader and of the export.
    for (const char* why : {"TTrak module not imported", "Not a TTrak module (.TPM).", "The TTrak module is cut.",
                            "Corrupt TTrak module header.", "Corrupt TTrak sample table.", "The Song is empty.",
                            "The Song plays nothing.", "Export the song for TTrak",
                            "TTrak plays the three channels of one chip: switch to Authentic 3ch"})
        lines.emplace_back(why);
    for (const char* code : kInstalled) {
        const pluginui::TipFile f = language(code);
        const pluginui::TipBook book = pluginui::TipBook::interfaceBook(f);
        for (const std::string& line : lines) {
            if (line.empty() || inert(line) || line == "Demo") continue;  // the file and the module named
            INFO(code << ": " << line);
            const auto t = book.line(line);
            REQUIRE(t);
            CHECK(*t != line);
            if (line.rfind("• ", 0) == 0) CHECK(t->rfind("• ", 0) == 0);
        }
    }
}

TEST_CASE("Interface language: the XM, MOD, S3M and OKT import alerts are translated, refusals included",
          "[language]") {
    plugin::XmImport r;
    r.title = "Demo";
    r.channels = 4;
    r.patterns = 3;
    r.instruments = 2;
    r.entries = 5;
    r.ticks = true;
    r.notes = {"Channels beyond the 8 DMA tracks: 2 left out.",
               "Instruments without a DMA slot (8 at most): 3, 12 notes left out.",
               "Notes without a sample: 1.",
               "Sample data cut by the end of the file.",
               "Samples cut to 32 KB: 1.",
               "Loops unrolled to 32 KB (the STE loops whole samples): 2.",
               "Effects left out: 3xx (4), E0x (1).",
               "AdLib channels (no OPL chip): 2 left out.",
               "AdLib instruments (no OPL chip): 3 left out."};
    std::vector<std::string> lines;
    for (plugin::ModuleFormat f : {plugin::ModuleFormat::Xm, plugin::ModuleFormat::Mod, plugin::ModuleFormat::S3m,
                                   plugin::ModuleFormat::Okt}) {
        r.format = f;
        for (const std::string& l : plugin::xmImportMessage(r, "A.MOD")) lines.push_back(l);
    }
    for (const char* why : {"MOD not imported", "Not a MOD module.", "Corrupt MOD module.", "XM not imported",
                            "Not an XM module.", "Unsupported XM version (1.04 only).", "Corrupt XM header.",
                            "Corrupt XM pattern.", "Corrupt XM instrument.", "S3M not imported", "Not an S3M module.",
                            "Corrupt S3M module.", "OKT not imported", "Not an OKT module.", "Corrupt OKT module.",
                            "Tracker song", "Import a tracker song", "Tracker song not imported",
                            "Expected an XM, MOD, S3M, OKT, maxYMiser,", "YM-Tracker or TTrak file.", "SNDH / YM file",
                            "Import an SNDH or YM file"})
        lines.emplace_back(why);
    for (const char* code : kInstalled) {
        const pluginui::TipFile f = language(code);
        const pluginui::TipBook book = pluginui::TipBook::interfaceBook(f);
        for (const std::string& line : lines) {
            if (line.empty() || inert(line) || line == "Demo") continue;  // the file and the module named
            INFO(code << ": " << line);
            const auto t = book.line(line);
            REQUIRE(t);
            CHECK(*t != line);
            if (line.rfind("• ", 0) == 0) CHECK(t->rfind("• ", 0) == 0);
        }
    }
}

TEST_CASE("Interface language: File > Play... and the STYM2149 files are translated", "[language]") {
    const std::vector<std::string> lines = {
        "Play...", "Play a file", "The file cannot be played.", "Nothing to play in this file.",
        "It comes from a newer STYM2149.", "Not a valid STYM2149 file.", "Expected a YM, SNDH, maxYMiser, YM-Tracker,",
        "TTrak, XM, MOD, S3M, OKT or STYM2149 file.", "This file holds a project.", "This file holds a pattern.",
        "This file holds a song.", "This file holds a drum kit.", "It cannot be read by this version."};
    for (const char* code : kInstalled) {
        const pluginui::TipFile f = language(code);
        const pluginui::TipBook book = pluginui::TipBook::interfaceBook(f);
        for (const std::string& line : lines) {
            INFO(code << ": " << line);
            const auto t = book.line(line);
            REQUIRE(t);
            CHECK(*t != line);
        }
    }
}
