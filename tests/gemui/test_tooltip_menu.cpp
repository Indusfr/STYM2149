// STYM2149 — Tooltips and their language, in the Settings window (Options > Tooltips before, its Language
// submenu after a separator, sixth DAW test, item 67): English, built in, then the languages of the PO files,
// installed or the user's (seventh DAW test, item 82), and the Languages folder button.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "Scenes.h"
#include "UiHost.h"
#include "gemui/Menu.h"
#include "pluginui/TipLanguage.h"

using namespace stym;
using namespace stym::ui_testing;

TEST_CASE("Settings: Tooltips On or Off, then their Language, out of the Options menu", "[ui][tips]") {
    UiHost h;
    h.languages = gemshot::installedLanguages();
    h.prefs["tipLanguage"] = "de";
    pluginui::MainWindow w(h);

    w.openMenu(3);
    for (const std::string& label : menuLabels(w)) {
        CHECK(label != "Tooltips");
        CHECK(label != "Language");
    }
    w.desktop().closePopups();

    gemui::MenuItem t = setting(w, "setTips");
    REQUIRE(t.sub.size() == 2);
    CHECK(t.sub[0].label == "On");
    CHECK(t.sub[1].label == "Off");
    CHECK(t.subCur == 0);
    gemui::MenuItem lang = setting(w, "setLanguage");
    REQUIRE(lang.sub.size() == 6);  // English and the five installed languages; the folder has its own button
    CHECK(lang.sub[0].label == "English");
    CHECK(lang.subCur == 1);  // Deutsch
    CHECK(w.settingsWindow().button("setLanguage")->text() == "Deutsch");

    // Picking a language sets the preference; the button and the next menu show it.
    lang.sub[3].pick();
    CHECK(h.prefs["tipLanguage"] == "fr");
    CHECK(setting(w, "setLanguage").subCur == 3);
    CHECK(w.settingsWindow().button("setLanguage")->text() == "Fran\u00e7ais");

    t.sub[1].pick();
    CHECK(h.prefs["tooltips"] == "0");
    CHECK_FALSE(w.desktop().tooltips());
    CHECK(setting(w, "setTips").subCur == 1);
    CHECK(w.settingsWindow().button("setTips")->text() == "Off");
}

namespace {

// A Dutch file, as a translator would write it: comments, a header over several lines, a fuzzy entry, a line with a
// placeholder, a translation over two quoted lines, escapes, an untranslated entry.
const char* const kDutch = "\xEF\xBB\xBF# Dutch tooltips\r\n"
                           "msgid \"\"\r\n"
                           "msgstr \"\"\r\n"
                           "\"Language: nl_NL\\n\"\r\n"
                           "\"X-Language-Name: Nederlands\\n\"\r\n"
                           "\r\n"
                           "#: somewhere\r\n"
                           "msgid \"Click to type a value\"\r\n"
                           "msgstr \"Klik om een \"\r\n"
                           "\"waarde te typen\"\r\n"
                           "\r\n"
                           "msgid \"Track {1}\"\r\n"
                           "msgstr \"Spoor {1}\"\r\n"
                           "\r\n"
                           "#, fuzzy\r\n"
                           "msgid \"Pan\"\r\n"
                           "msgstr \"Panorama?\"\r\n"
                           "\r\n"
                           "msgid \"Volume\"\r\n"
                           "msgstr \"\"\r\n"
                           "\r\n"
                           "msgctxt \"x\"\r\n"
                           "msgid \"Say \\\"hi\\\"\"\r\n"
                           "msgstr \"Zeg \\\"hoi\\\"\"\r\n";

// Settings > Language: its menu opened by a click, which reads the language files again, then closed.
struct LanguageMenu {
    std::vector<std::string> labels;
    int checked;
    std::vector<gemui::MenuItem> items;
};
LanguageMenu languages(pluginui::MainWindow& w) {
    w.settingsWindow().show();
    click(w, "setLanguage");
    const gemui::MenuList* m = topMenu(w);
    REQUIRE(m);
    LanguageMenu out{{}, m->checked(), m->items()};
    for (const gemui::MenuItem& e : out.items) out.labels.push_back(e.label);
    w.desktop().closePopups();
    return out;
}

// An installed language read as the plugin reads it.
pluginui::TipFile installed(const std::string& name) {
    for (const pluginui::Host::TextFile& f : gemshot::installedLanguages())
        if (f.name == name) {
            auto file = pluginui::parseTipFile(f.text, name.substr(0, name.size() - 3));
            REQUIRE(file);
            return *file;
        }
    FAIL("no installed " << name);
    return {};
}

}  // namespace

TEST_CASE("Tooltip language files: a PO file read with its header, quoted lines, escapes and fuzzy entries",
          "[ui][tips]") {
    using pluginui::parseTipFile;
    const auto nl = parseTipFile(kDutch, "whatever");
    REQUIRE(nl);
    CHECK(nl->id == "nl-nl");
    CHECK(nl->name == "Nederlands");
    using Line = std::pair<std::string, std::string>;
    CHECK(nl->lines == std::vector<Line>{{"Click to type a value", "Klik om een waarde te typen"},
                                          {"Track {1}", "Spoor {1}"},
                                          {"Say \"hi\"", "Zeg \"hoi\""}});
    const pluginui::TipBook book(*nl);
    CHECK(book.translate("Click to type a value\nTrack 12\nNo such line") ==
          "Klik om een waarde te typen\nSpoor 12\nNo such line");

    // Without a Language header, the file name gives the code; a name that is no code refuses the file.
    const auto sv = parseTipFile("msgid \"Pan\"\nmsgstr \"Panorering\"\n", "sv");
    REQUIRE(sv);
    CHECK(sv->id == "sv");
    CHECK(sv->name == "sv");
    CHECK_FALSE(parseTipFile("msgid \"Pan\"\nmsgstr \"x\"\n", "my file"));
    CHECK(pluginui::tipLanguageCode(" pt_BR ") == "pt-br");
    CHECK(pluginui::tipLanguageCode("x").empty());

    // A file over another (fr-CA over the installed French): its lines first, those of the other for the rest.
    const auto ca = parseTipFile("msgid \"\"\nmsgstr \"Language: fr_CA\\n\"\n\nmsgid \"Click to type a value\"\n"
                                 "msgstr \"Cliquer pour taper une valeur\"\n",
                                 "fr-ca");
    REQUIRE(ca);
    const pluginui::TipFile fr = installed("fr.po");
    const pluginui::TipBook canadian({&*ca, &fr});
    CHECK(canadian.translate("Click to type a value") == "Cliquer pour taper une valeur");
    CHECK(canadian.translate("Track 3") == "Piste 3");
    CHECK(pluginui::TipBook().translate("Track 3") == "Track 3");  // English: nothing to translate
}

TEST_CASE("Tooltip language files: the five installed languages hold every English line, the template none ",
          "[ui][tips]") {
    // The PO files of languages/: one entry per line of the catalogue, in its order, each translated.
    const std::vector<std::pair<std::string, std::string>> expected{
        {"de", "Deutsch"}, {"es", "Español"}, {"fr", "Français"}, {"it", "Italiano"}, {"pt", "Português"}};
    const auto files = gemshot::installedLanguages();
    REQUIRE(files.size() == expected.size());
    for (size_t k = 0; k < files.size(); ++k) {
        INFO(files[k].name);
        CHECK(files[k].installed);
        CHECK(files[k].name == expected[k].first + ".po");
        const pluginui::TipFile file = installed(files[k].name);
        CHECK(file.id == expected[k].first);
        CHECK(file.name == expected[k].second);
        REQUIRE(file.lines.size() == pluginui::tipcatalog::kLineCount);
        for (size_t e = 0; e < pluginui::tipcatalog::kLineCount; ++e)
            CHECK(file.lines[e].first == pluginui::tipcatalog::kLines[e]);
    }

    // The user's folder gets the read-me, which names the installed folder, and the template, every line untranslated.
    const auto folder = pluginui::tipFolderFiles("1.2.3", "/data/STYM2149/Languages");
    REQUIRE(folder.size() == 2);
    CHECK(folder[0].first == "README.txt");
    CHECK(folder[0].second.find("\n/data/STYM2149/Languages\n") != std::string::npos);
    CHECK(pluginui::tipFolderFiles("1.2.3", "")[0].second.find("in a folder of the computer.\n") != std::string::npos);
    CHECK(folder[1].first == "template.pot");
    const auto tpl = pluginui::parseTipFile(folder[1].second, "template");
    REQUIRE(tpl);
    CHECK(tpl->lines.empty());
    CHECK(folder[1].second.find("Project-Id-Version: STYM2149 1.2.3") != std::string::npos);
    size_t entries = 0;  // after the header, an entry per line, each after a blank line
    for (size_t at = folder[1].second.find("\n\nmsgid \""); at != std::string::npos;
         at = folder[1].second.find("\n\nmsgid \"", at + 1))
        ++entries;
    CHECK(entries == pluginui::tipcatalog::kLineCount);
}

TEST_CASE("Settings > Language: English alone without files; the installed languages, the user's files over them, "
          "the folder opened",
          "[ui][tips]") {
    {
        // No language file (the plugin not installed): English, and nothing to translate.
        UiHost h;
        h.prefs["tipLanguage"] = "de";
        pluginui::MainWindow w(h);
        CHECK(languages(w).labels == std::vector<std::string>{"English"});
        CHECK(w.tipLanguage() == "en");
        CHECK(w.desktop().translateTip("Click to type a value") == "Click to type a value");
    }
    UiHost h;
    h.languages = {{"nl.po", kDutch},
                   {"af.po", "msgid \"\"\nmsgstr \"X-Language-Name: Afrikaans\\n\"\n"},
                   {"fr.po", "msgid \"Click to type a value\"\nmsgstr \"Saisir une valeur\"\n"},
                   {"bad name.po", "msgid \"Pan\"\nmsgstr \"x\"\n"},
                   {"nl-copy.po", "msgid \"\"\nmsgstr \"Language: nl-NL\\nX-Language-Name: Copy\\n\"\n"}};
    for (pluginui::Host::TextFile& f : gemshot::installedLanguages()) h.languages.push_back(std::move(f));
    h.systemLang = "nl-NL";  // nothing chosen yet: the system's language, found in the folder
    pluginui::MainWindow w(h);
    CHECK(w.tipLanguage() == "nl-nl");
    CHECK(w.desktop().translateTip("Click to type a value") == "Klik om een waarde te typen");

    LanguageMenu lang = languages(w);
    CHECK(lang.labels == std::vector<std::string>{"English", "Afrikaans", "Deutsch", "Español", "Français",
                                                  "Italiano", "Nederlands", "Português"});
    CHECK(lang.checked == 6);

    // French corrected by the user's fr.po: its line first, the installed French for the others.
    lang.items[4].pick();
    CHECK(h.prefs["tipLanguage"] == "fr");
    CHECK(w.desktop().translateTip("Click to type a value") == "Saisir une valeur");
    CHECK(w.desktop().translateTip("Track 3") == "Piste 3");
    CHECK(languages(w).checked == 4);

    // The files of a language taken away: read again at the next opening of the menu.
    lang.items[6].pick();
    CHECK(h.prefs["tipLanguage"] == "nl-nl");
    std::erase_if(h.languages, [](const pluginui::Host::TextFile& f) { return f.name.rfind("nl", 0) == 0; });
    lang = languages(w);
    CHECK(lang.items.size() == 7);
    CHECK(lang.checked < 0);
    CHECK(w.desktop().translateTip("Click to type a value") == "Click to type a value");

    // Languages folder: the read-me and the template written, the folder shown.
    click(w, "setFolder");
    CHECK(h.foldersOpened == 1);
    REQUIRE(h.folderFiles.size() == 2);
    CHECK(h.folderFiles[0].name == "README.txt");
    CHECK(h.folderFiles[1].name == "template.pot");
}

TEST_CASE("Settings > Language: a chosen language kept, a regional system language",
          "[ui][tips]") {
    {
        UiHost h;
        h.languages = gemshot::installedLanguages();
        h.systemLang = "de_AT";
        pluginui::MainWindow w(h);
        CHECK(w.tipLanguage() == "de");
    }
    {
        UiHost h;
        h.languages = gemshot::installedLanguages();
        h.systemLang = "ja-JP";
        pluginui::MainWindow w(h);
        CHECK(w.tipLanguage() == "en");
    }
    {
        UiHost h;
        h.languages = gemshot::installedLanguages();
        h.prefs["tipLanguage"] = "es";
        h.systemLang = "fr";
        pluginui::MainWindow w(h);
        CHECK(w.tipLanguage() == "es");
        CHECK(w.desktop().translateTip("Click to type a value") ==
              pluginui::TipBook(installed("es.po")).translate("Click to type a value"));
    }
    {
        // The system's language without its file: English.
        UiHost h;
        h.systemLang = "fr-FR";
        pluginui::MainWindow w(h);
        CHECK(w.tipLanguage() == "en");
    }
}

TEST_CASE("Tooltip languages: the ADSR tips name each setting in English in parentheses (ninth DAW test, item 106)",
          "[ui][tips]") {
    // The sliders show A, D, S and R only: a translated tip names the setting in its language, then in English.
    const std::pair<const char*, const char*> tips[] = {
        {"Attack: time to reach full level after the note starts", "(Attack)"},
        {"Decay: time to fall from full level down to the sustain level", "(Decay)"},
        {"Sustain: level held while the note lasts", "(Sustain)"},
        {"Release: time to fade out once the note ends", "(Release)"}};
    for (const char* name : {"de.po", "es.po", "fr.po", "it.po", "pt.po"}) {
        INFO(name);
        const pluginui::TipFile file = installed(name);
        const pluginui::TipBook book(file);
        for (const auto& [en, english] : tips) {
            INFO(en);
            const std::string t = book.translate(en);
            const size_t colon = t.find(':');
            REQUIRE(colon != std::string::npos);
            CHECK(t.substr(0, colon).find(english) != std::string::npos);
            CHECK(t.find(english) > 0);  // after the name in the language
        }
    }
    const pluginui::TipFile fr = installed("fr.po");
    CHECK(pluginui::TipBook(fr).translate(tips[0].first).rfind("Attaque (Attack)", 0) == 0);
}
