// STYM2149 — plugin with JUCE: every tooltip line has its translations (M8, E1, D-185, D-283).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The editor's windows are opened in several states (voice modes, machines, generators of track 1); the tooltip of
// every widget is read (tip(), and tipAt() over the widget, every 2 pixels), with the reasons of the disabled menu
// entries. Each line must be in the catalogue and translated by the five PO files installed with STYM2149 (languages/
// in the sources, D-283), written only with characters of the GEM font. With STYM_TIPS_DUMP=<file>, the English lines
// are also written to that file, one per line.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <functional>
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
#include "gemui/Menu.h"
#include "pluginui/TipLanguage.h"

using namespace stym;
using namespace stym::plugin;

namespace {

// An installed language, read from languages/ as the editor reads it from the installed folder.
pluginui::TipFile language(const std::string& code) {
    std::ifstream in(std::string(STYM_LANGUAGES_DIR) + "/" + code + ".po", std::ios::binary);
    REQUIRE(in);
    const std::string text(std::istreambuf_iterator<char>(in), {});
    auto file = pluginui::parseTipFile(text, code);
    REQUIRE(file);
    CHECK(file->id == code);
    return *file;
}

const char* const kInstalled[] = {"fr", "de", "es", "pt", "it"};

void setPlain(StymProcessor& p, const std::string& id, float v) {
    const int i = findParam(id);
    REQUIRE(i >= 0);
    auto& param = p.parameter(i);
    param.setValueNotifyingHost(param.convertTo0to1(v));
}

void addLines(std::set<std::string>& lines, const std::string& tip) {
    for (size_t from = 0; from <= tip.size();) {
        const size_t nl = tip.find('\n', from);
        const std::string line = tip.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        if (!line.empty()) lines.insert(line);
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
}

void collectWidget(std::set<std::string>& lines, const gemui::Widget& w) {
    addLines(lines, w.tip());
    const gemui::Rect b = w.bounds();
    for (int y = b.y; y < b.bottom(); y += 2)
        for (int x = b.x; x < b.right(); x += 2) addLines(lines, w.tipAt({x, y}));
    for (const gemui::Widget* c : w.children()) collectWidget(lines, *c);
}

void collectMenu(std::set<std::string>& lines, const std::vector<gemui::MenuItem>& items) {
    for (const gemui::MenuItem& m : items) {
        addLines(lines, m.why);
        collectMenu(lines, m.sub);
    }
}

void collect(std::set<std::string>& lines, StymEditor& editor) {
    pluginui::MainWindow& w = editor.window();
    editor.frame();
    collectWidget(lines, w);
    for (int title = 0; title < 4; ++title) {
        w.openMenu(title);
        if (const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().firstPopup())) {
            collectMenu(lines, list->items());
            collectWidget(lines, *list);
        }
        w.desktop().closePopups();
    }
    // The menus of the Settings window (D-430): their greyed-out values give a reason.
    for (const std::string_view id : pluginui::SettingsWindow::menuIds()) {
        int checked = -1;
        collectMenu(lines, w.settingsWindow().items(id, checked));
    }
    // The Note length menu of the Step Sequencer: the values that do not fit the resolution give a reason (D-425).
    if (const auto* fig = dynamic_cast<const gemui::Button*>(w.sequencer().widgetOf("seqFig")); fig && fig->onClick) {
        fig->onClick();
        if (const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().firstPopup()))
            collectMenu(lines, list->items());
        w.desktop().closePopups();
    }
}

}  // namespace

TEST_CASE("Tooltips: every line is translated, with characters of the GEM font (D-185)", "[editor][tooltips]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    StymEditor editor(p, std::make_unique<juce::PropertiesFile>(prefs.getFile(), juce::PropertiesFile::Options{}));
    pluginui::MainWindow& w = editor.window();
    for (auto* win : std::initializer_list<gemui::Widget*>{
             &w.sequencer(), &w.songWindow(), &w.mixer(), &w.arpeggiator(), &w.modulation(), &w.drumKit(),
             &w.patches(), &w.instruments(), &w.dmaWindow(),
             &w.recorderWindow(), &w.ymPlayerWindow(), &w.digidrums(), &w.slideCurve(), &w.keyboardWindow(),
             &w.echoWindow(), &w.settingsWindow()})
        static_cast<gemui::FloatWindow*>(win)->open();
    w.openScope(0);
    // Modulation table on for track 1: the sequence rows show below it (D-240). Echo and delay on: their rows show
    // (D-299).
    p.editExtras([](plugin::NonParamState& s) {
        s.tracks[0].patch.ornamentOn = true;
        s.tracks[0].patch.echo.on = true;
    });
    setPlain(p, "delayOn", 1.0f);
    std::set<std::string> lines;
    for (int mode = 0; mode < 3; ++mode)
        for (int computer = 0; computer < 2; ++computer) {
            setPlain(p, "voiceMode", float(mode));
            setPlain(p, "computer", float(computer));
            for (int g = 0; g < 6; ++g) {
                setPlain(p, "t01_generator", float(g));
                collect(lines, editor);
            }
        }
    setPlain(p, "voiceMode", 0.0f);
    setPlain(p, "delaySync", 0.0f);  // 16 ch, Time in ms instead of the Division
    collect(lines, editor);
    w.doDemo();
    collect(lines, editor);
    INFO(lines.size() << " English lines");
    REQUIRE(lines.size() > 100);

    if (const char* path = std::getenv("STYM_TIPS_DUMP")) {
        std::ofstream out(path, std::ios::binary);
        for (const std::string& l : lines) out << l << '\n';
    }

    // Note names (the keys of the keyboards) read the same in every language.
    const std::regex noteName("(?:[A-G]|Do|Re|Mi|Fa|Sol|La|Si)#?-?[0-9]");
    std::vector<pluginui::TipBook> books;
    for (const char* code : kInstalled) books.emplace_back(language(code));
    int missing = 0, badGlyphs = 0;
    for (const std::string& line : lines)
        for (size_t l = 0; l < books.size() && !std::regex_match(line, noteName); ++l) {
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

    // A line without an entry of its own must not be taken by a generic one ("Track {1}" translating "Track length
    // (polymeter)" word for word): a placeholder captures a number or a name, never a phrase.
    const std::regex phrase("[\\s:(][a-z]{3,}");
    int swallowed = 0;
    for (const std::string& line : lines)
        if (const auto parts = pluginui::tipPlaceholderParts(line))
            for (const std::string& part : *parts)
                if (std::regex_search(part, phrase) && ++swallowed <= 20)
                    UNSCOPED_INFO("taken by a generic line: " << line);
    CHECK(swallowed == 0);
}

TEST_CASE("Tooltips: lookup in an installed language, placeholders and language codes (D-185, D-283)",
          "[tooltips]") {
    const pluginui::TipBook french(language("fr")), german(language("de"));
    CHECK(pluginui::tipLanguageCode("fr_FR") == "fr-fr");
    CHECK(pluginui::tipLanguageCode("DE") == "de");
    CHECK(pluginui::tipLanguageCode("french language").empty());
    CHECK(french.translate("Click to type a value") == "Cliquer pour saisir une valeur");
    CHECK(pluginui::TipBook().translate("Click to type a value") == "Click to type a value");  // English, built in
    // Placeholders: the varying parts move into the translation; the most specific pattern wins.
    CHECK(german.translate("Track 12 - Bass") == "Spur 12 - Bass");
    CHECK(french.translate("Bank B: eight patterns (B1 to B8)") == "Banque B\u00A0: huit motifs (B1 à B8)");
    CHECK(german.line("GEN step 3: Note's generator. Click to choose.") !=
          german.line("GEN step 3: SID Voice. Click to choose."));
    // A line missing from the file stays in English; the others are translated.
    CHECK(german.translate("No such line\nClick to type a value") == "No such line\nKlicken, um einen Wert einzugeben");
}
