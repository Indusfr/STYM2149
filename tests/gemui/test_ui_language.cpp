// STYM2149 — interface language, chosen apart from the tooltip language (Indus, 28/10/2026 03:06): the texts
// of the interface translated by the "ui" entries of the language files, the user's names left as they are, a change
// of language laying every window out again as a window built in that language.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "Png.h"
#include "Scenes.h"
#include "UiHost.h"
#include "gemui/Language.h"
#include "gemui/Menu.h"
#include "pluginui/TipLanguage.h"

using namespace stym;
using namespace stym::ui_testing;

namespace {

using pluginui::MainWindow;

// An installed language read as the plugin reads it.
pluginui::TipFile installed(const std::string& code) {
    for (const pluginui::Host::TextFile& f : gemshot::installedLanguages())
        if (f.name == code + ".po") {
            auto file = pluginui::parseTipFile(f.text, code);
            REQUIRE(file);
            return *file;
        }
    FAIL("no installed " << code);
    return {};
}

// The windows of the scene compared, opened in front of the main window, then laid out by a refresh (a change of the
// state, as the windows' menu entries make one).
void openWindows(UiHost& h, MainWindow& w) {
    w.settingsWindow().show();
    w.sequencer().open();
    w.mixer().open();
    w.songWindow().open();
    h.editExtras([](plugin::NonParamState&) {});
    w.tick(0);
}

std::vector<uint32_t> pixels(MainWindow& w) {
    w.tick(0);
    w.desktop().paint();
    return w.desktop().surface().pixels();
}

// Pixels that differ, and the box around them (printed when a check fails).
struct Diff {
    int count = 0, x0 = MainWindow::kWidth, y0 = MainWindow::kHeight, x1 = -1, y1 = -1;
};
Diff diff(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, int width) {
    Diff d;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        if (a[i] != b[i]) {
            const int x = int(i) % width, y = int(i) / width;
            ++d.count;
            d.x0 = std::min(d.x0, x), d.y0 = std::min(d.y0, y), d.x1 = std::max(d.x1, x), d.y1 = std::max(d.y1, y);
        }
    return d;
}

}  // namespace

TEST_CASE("Interface language: English by default, chosen apart from the tooltip language", "[ui][language]") {
    UiHost h;
    h.languages = gemshot::installedLanguages();
    h.systemLang = "fr";  // the tooltips follow the system; the interface stays in its base language
    auto w = std::make_unique<MainWindow>(h);
    CHECK(w->uiLanguage() == "en");
    CHECK(w->tipLanguage() == "fr");
    CHECK_FALSE(gemui::uiTranslated());
    CHECK(gemui::uiString("FLOPPY DISK") == "FLOPPY DISK");
    CHECK(w->desktop().translateTip("Click to type a value") == "Cliquer pour saisir une valeur");

    // French interface, English tooltips: each kept in its own preference.
    w->setUiLanguage("fr");
    w->setTipLanguage("en");
    CHECK(h.prefs["uiLanguage"] == "fr");
    CHECK(h.prefs["tipLanguage"] == "en");
    CHECK(gemui::uiTranslated());
    CHECK(gemui::uiString("FLOPPY DISK") == "DISQUETTE");
    CHECK(w->desktop().translateTip("Click to type a value") == "Click to type a value");

    // The next editor opens in the interface language chosen.
    w.reset();
    gemui::setUiLanguage({});
    MainWindow again(h);
    CHECK(again.uiLanguage() == "fr");
    CHECK(gemui::uiString("FLOPPY DISK") == "DISQUETTE");
    again.setUiLanguage("en");
    CHECK_FALSE(gemui::uiTranslated());
}

TEST_CASE("Interface language: the Settings window has its own menu, each language named in its own language",
          "[ui][language]") {
    UiHost h;
    h.languages = gemshot::installedLanguages();
    MainWindow w(h);
    w.settingsWindow().show();
    int checked = -1;
    const std::vector<gemui::MenuItem> items = w.settingsWindow().items("setUiLanguage", checked);
    REQUIRE(items.size() == 6);
    CHECK(checked == 0);
    CHECK(items[0].label == "English");
    for (const gemui::MenuItem& e : items) CHECK(e.raw);  // never translated
    CHECK(w.settingsWindow().shownLabel("setUiLanguage") == "Interface language");
    CHECK(w.settingsWindow().shownLabel("setLanguage") == "Tooltip language");
    // Picking French: the interface in French, the tooltips unchanged, a change Cancel would put back.
    auto pick = [&w](const std::string& name) {
        int c = -1;
        for (const gemui::MenuItem& e : w.settingsWindow().items("setUiLanguage", c))
            if (e.label == name) e.pick();
    };
    pick("Français");
    CHECK(w.uiLanguage() == "fr");
    CHECK(w.tipLanguage() == "en");
    CHECK(w.settingsWindow().changed());
    CHECK(w.settingsWindow().shownLabel("setUiLanguage") == "Interface language");  // the text kept, drawn translated
    pick("English");
    CHECK(w.uiLanguage() == "en");
    CHECK_FALSE(w.settingsWindow().changed());
    CHECK_FALSE(gemui::uiTranslated());
}

TEST_CASE("Interface language: lines with placeholders, their parts translated too, names left as they are",
          "[ui][language]") {
    using Line = std::pair<std::string, std::string>;
    pluginui::TipFile nl;
    nl.id = "nl";
    nl.uiLines = {Line{"Channel {1} [{2}]", "Kanaal {1} [{2}]"}, Line{"Off", "Uitgeschakeld"},
                  Line{"Track {1}", "Spoor {1}"}, Line{"Mute {1}", "Dempen {1}"},
                  Line{"Delete bank \"{1}\"?", "Bank \"{1}\" verwijderen?"}};
    const pluginui::TipBook book = pluginui::TipBook::interfaceBook(nl);
    CHECK(book.line("Channel Off [Chip Voice]") == "Kanaal Uitgeschakeld [Chip Voice]");
    CHECK(book.line("Channel Track 3 [Chip Voice]") == "Kanaal Spoor 3 [Chip Voice]");
    // Between brackets or quotes, a name (a generator, a bank, a sample, a control): never looked up.
    CHECK(book.line("Channel 3 [Off]") == "Kanaal 3 [Off]");
    CHECK(book.line("Delete bank \"Off\"?") == "Bank \"Off\" verwijderen?");
    CHECK(pluginui::uiPlaceholderParts("Delete bank \"Set A\" from the library?") == std::vector<std::string>{});
    CHECK(pluginui::uiPlaceholderParts("SCOPE - Bass") == std::vector<std::string>{"Bass"});
    CHECK(book.line("Mute Track 2") == "Dempen Spoor 2");                    // one level of patterns in a part
    // As much fixed text: the line with fewer placeholders wins, its part translated by the other.
    pluginui::TipFile it;
    it.id = "it";
    it.uiLines = {Line{"{1} to {2}", "Da {1} a {2}"}, Line{"• {1}", "• {1}"},
                  Line{"Rows cut to {1}: {2}.", "Righe ridotte a {1}: {2}."}};
    const pluginui::TipBook bullets = pluginui::TipBook::interfaceBook(it);
    CHECK(bullets.line("• Rows cut to 255: 3.") == "• Righe ridotte a 255: 3.");
    CHECK(bullets.line("• C-1 to B-8") == "• Da C-1 a B-8");
    CHECK(bullets.line("C-1 to B-8") == "Da C-1 a B-8");
    CHECK_FALSE(book.line("No such line"));
    // The tooltip book of the same file has none of these lines.
    CHECK_FALSE(pluginui::TipBook(nl).line("Off"));

    // In the interface, a line is looked up without the spaces around it, which stay.
    gemui::setUiLanguage([book](std::string_view l) { return book.line(l); });
    CHECK(gemui::uiString("  Off ") == "  Uitgeschakeld ");
    CHECK(gemui::uiString("Off\nTrack 4") == "Uitgeschakeld\nSpoor 4");
    CHECK(gemui::uiString("1234") == "1234");  // no letter: not looked up
    {
        const gemui::RawText raw;
        CHECK(gemui::uiString("Off") == "Off");
    }
    // A name the user gave stays as it is on a button or a label.
    gemui::Button named("Off"), plain("Off");
    named.setRaw(true);
    CHECK(named.naturalWidth() < plain.naturalWidth());
    gemui::setUiLanguage({});
    CHECK(named.naturalWidth() == plain.naturalWidth());
}

TEST_CASE("Interface language: a change of language lays the windows out as a window built in it",
          "[ui][language]") {
    const pluginui::TipFile fr = installed("fr");
    REQUIRE(fr.uiLines.size() > 10);

    UiHost english;
    english.languages = gemshot::installedLanguages();
    MainWindow live(english);
    openWindows(english, live);
    const std::vector<uint32_t> before = pixels(live);
    live.setUiLanguage("fr");
    const std::vector<uint32_t> switched = pixels(live);

    UiHost french;
    french.languages = gemshot::installedLanguages();
    french.prefs["uiLanguage"] = "fr";
    MainWindow built(french);
    openWindows(french, built);
    const std::vector<uint32_t> fresh = pixels(built);
    const int width = live.desktop().surface().width();
    CHECK(diff(switched, before, width).count > 0);
    const Diff d = diff(switched, fresh, width);
    if (const char* dir = std::getenv("STYM_LANG_DUMP")) {
        const int h = live.desktop().surface().height();
        for (const auto& [n, px] : {std::pair<const char*, const std::vector<uint32_t>*>{"before", &before},
                                    {"switched", &switched}, {"fresh", &fresh}}) {
            tools::Image im;
            im.width = width;
            im.height = h;
            im.pixels = *px;
            tools::writePng(std::string(dir) + "/" + n + ".png", im);
        }
    }
    INFO("differing " << d.count << " in x " << d.x0 << "-" << d.x1 << ", y " << d.y0 << "-" << d.y1);
    CHECK(d.count == 0);
    for (const char* id : {"settingsWin", "setUiLanguage", "setLanguage", "setCancel", "setOk"})
        CHECK(live.settingsWindow().rectOf(id) == built.settingsWindow().rectOf(id));

    // And back to English: as before the change.
    live.setUiLanguage("en");
    const Diff back = diff(pixels(live), before, width);
    INFO("differing " << back.count << " in x " << back.x0 << "-" << back.x1 << ", y " << back.y0 << "-" << back.y1);
    CHECK(back.count == 0);
    gemui::setUiLanguage({});
}

TEST_CASE("Interface language: another editor of the process follows a change at its next tick",
          "[ui][language]") {
    UiHost h;
    h.languages = gemshot::installedLanguages();
    MainWindow first(h), second(h);  // both editors read and write the same preferences
    first.setUiLanguage("de");
    CHECK(second.uiLanguage() == "en");
    second.tick(0);
    CHECK(second.uiLanguage() == "de");
    first.setUiLanguage("en");
    second.tick(0);
    CHECK(second.uiLanguage() == "en");
}
