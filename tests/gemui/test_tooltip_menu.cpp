// STYM2149 — Options > Tooltips: On, Off, a separator, then the Language submenu (sixth DAW test, item 67, D-265):
// English, built in, then the languages of the PO files, installed or the user's (seventh DAW test, item 82, D-283).
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

namespace {

const gemui::MenuItem* find(const std::vector<gemui::MenuItem>& items, const std::string& label) {
    for (const gemui::MenuItem& m : items)
        if (m.label == label) return &m;
    return nullptr;
}

// Options > Tooltips, as the menu builds it.
gemui::MenuItem tooltips(pluginui::MainWindow& w) {
    w.openMenu(3);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    REQUIRE_FALSE(find(list->items(), "Tooltip language"));
    const gemui::MenuItem* t = find(list->items(), "Tooltips");
    REQUIRE(t);
    gemui::MenuItem copy = *t;
    w.desktop().closePopups();
    return copy;
}

}  // namespace

TEST_CASE("Options: the tooltip language is a submenu of Tooltips, after a separator (sixth DAW test, item 67, D-265)",
          "[ui][tips]") {
    UiHost h;
    h.languages = gemshot::installedLanguages();
    h.prefs["tipLanguage"] = "de";
    pluginui::MainWindow w(h);

    gemui::MenuItem t = tooltips(w);
    REQUIRE(t.sub.size() == 4);
    CHECK(t.sub[0].label == "On");
    CHECK(t.sub[1].label == "Off");
    CHECK(t.sub[2].separator);
    CHECK(t.sub[3].label == "Language");
    CHECK(t.subMarks);
    CHECK(t.subCur == 0);  // On, among the plain entries

    const gemui::MenuItem& lang = t.sub[3];
    REQUIRE(lang.sub.size() == 8);  // English, the five installed languages, a separator, Languages folder (D-283)
    CHECK(lang.sub[0].label == "English");
    CHECK(lang.sub[6].separator);
    CHECK(lang.sub[7].label == "Languages folder...");
    CHECK(lang.subMarks);
    CHECK(lang.subCur == 1);  // Deutsch

    // Picking a language from the nested submenu sets the preference; the next menu marks it.
    lang.sub[3].pick();
    CHECK(h.prefs["tipLanguage"] == "fr");
    CHECK(tooltips(w).sub[3].subCur == 3);

    // Off is still the second entry.
    tooltips(w).sub[1].pick();
    CHECK(h.prefs["tooltips"] == "0");
    CHECK(tooltips(w).subCur == 1);
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

gemui::MenuItem languages(pluginui::MainWindow& w) { return tooltips(w).sub[3]; }

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

std::vector<std::string> labels(const gemui::MenuItem& m) {
    std::vector<std::string> out;
    for (const gemui::MenuItem& e : m.sub) out.push_back(e.separator ? "-" : e.label);
    return out;
}

}  // namespace

TEST_CASE("Tooltip language files: a PO file read with its header, quoted lines, escapes and fuzzy entries (D-283)",
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

TEST_CASE("Tooltip language files: the five installed languages hold every English line, the template none "
          "(D-283)",
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

TEST_CASE("Options > Tooltips > Language: English alone without files; the installed languages, the user's files "
          "over them, the folder opened (D-283)",
          "[ui][tips]") {
    {
        // No language file (the plugin not installed): English, and nothing to translate.
        UiHost h;
        h.prefs["tipLanguage"] = "de";
        pluginui::MainWindow w(h);
        CHECK(labels(languages(w)) == std::vector<std::string>{"English", "-", "Languages folder..."});
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

    gemui::MenuItem lang = languages(w);
    CHECK(labels(lang) == std::vector<std::string>{"English", "Afrikaans", "Deutsch", "Español", "Français",
                                                   "Italiano", "Nederlands", "Português", "-",
                                                   "Languages folder..."});
    CHECK(lang.subCur == 6);

    // French corrected by the user's fr.po: its line first, the installed French for the others.
    lang.sub[4].pick();
    CHECK(h.prefs["tipLanguage"] == "fr");
    CHECK(w.desktop().translateTip("Click to type a value") == "Saisir une valeur");
    CHECK(w.desktop().translateTip("Track 3") == "Piste 3");
    CHECK(languages(w).subCur == 4);

    // The files of a language taken away: read again at the next opening of the menu.
    lang.sub[6].pick();
    CHECK(h.prefs["tipLanguage"] == "nl-nl");
    std::erase_if(h.languages, [](const pluginui::Host::TextFile& f) { return f.name.rfind("nl", 0) == 0; });
    lang = languages(w);
    CHECK(lang.sub.size() == 9);
    CHECK(lang.subCur == -1);
    CHECK(w.desktop().translateTip("Click to type a value") == "Click to type a value");

    // Languages folder: the read-me and the template written, the folder shown.
    lang.sub.back().pick();
    CHECK(h.foldersOpened == 1);
    REQUIRE(h.folderFiles.size() == 2);
    CHECK(h.folderFiles[0].name == "README.txt");
    CHECK(h.folderFiles[1].name == "template.pot");
}

TEST_CASE("Options > Tooltips > Language: a chosen language kept, a regional system language (D-185, D-283)",
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

TEST_CASE("Tooltip languages: the ADSR tips name each setting in English in parentheses (ninth DAW test, item 106, "
          "D-307)",
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
