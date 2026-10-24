// STYM2149 — GEM desktop: the menu bar across its top and the decorative icons of the floppy disks and of the trash
// (ninth DAW test, items 101 and 102, D-302, D-303).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Scenes.h"
#include "UiHost.h"

using namespace stym;
using namespace stym::ui_testing;

namespace {

using pluginui::MainWindow;

gemui::Point pictureOf(const gemui::DeskIcon& i) { return {i.bounds().x + gemui::DeskIcon::kW / 2, i.bounds().y + 16}; }

}  // namespace

TEST_CASE("Desktop: the menu bar across the top, the windows below it (ninth DAW test, item 101)", "[ui][desktop]") {
    UiHost h;
    MainWindow w(h);
    // Titles from the left edge of the desktop, indicators at its right edge.
    const gemui::Rect fuji = *w.rectOf("mFuji"), midi = *w.rectOf("miditxt");
    CHECK(fuji.y == 0);
    CHECK(fuji.x == 8);
    CHECK(midi.y == 0);
    CHECK(midi.right() == MainWindow::kWidth - 8);
    CHECK(w.desktop().workTop() == MainWindow::kMenuH);
    // The main window starts below the bar, without a menu row: its title bar, then its content.
    CHECK(w.mainPlace().y == MainWindow::kWinY);
    CHECK(MainWindow::kWinY == MainWindow::kMenuH + 18);
    // A title opens its menu below the bar.
    w.openMenu(1);
    REQUIRE(topMenu(w));
    CHECK(topMenu(w)->bounds().y >= MainWindow::kMenuH - 2);
    w.desktop().closePopups();

    // Neither the main window nor a floating window goes above the bar.
    w.moveMainTo(200, 0);
    CHECK(w.mainPlace().y == MainWindow::kMenuH);
    CHECK(w.mainPlace().x == 200);
    w.sequencer().show();
    const gemui::Rect bar = w.sequencer().titleBar().bounds();
    w.desktop().mouseDown({bar.x + 300, bar.y + 10}, gemui::MouseButton::Left, {});
    w.desktop().mouseDrag({bar.x + 300, 0}, {});
    w.desktop().mouseUp({bar.x + 300, 0}, {});
    CHECK(w.sequencer().bounds().y == MainWindow::kMenuH);
}

TEST_CASE("Desktop: icons A, B and trash, named in the tooltip language, moved by a held press only (item 102)",
          "[ui][desktop]") {
    UiHost h;
    h.languages = gemshot::installedLanguages();
    auto w = std::make_unique<MainWindow>(h);
    const auto& icons = w->deskIcons();
    REQUIRE(icons.size() == 3);
    CHECK(icons[0].name() == "FLOPPY DISK");
    CHECK(icons[1].name() == "FLOPPY DISK");
    CHECK(icons[2].name() == "TRASH");
    // A and B on one row at the top left, beside the main window, below the menu bar; the trash at the bottom right
    // (tenth DAW test, item 124).
    CHECK(icons[0].bounds().x == 0);
    CHECK(icons[0].bounds().y >= MainWindow::kMenuH);
    CHECK(icons[1].bounds().y == icons[0].bounds().y);
    CHECK(icons[1].bounds().x > icons[0].bounds().right());
    CHECK(icons[1].bounds().right() <= MainWindow::kWinX);
    CHECK(icons[2].bounds().right() == MainWindow::kWidth);
    CHECK(icons[2].bounds().bottom() < MainWindow::kHeight);
    CHECK(icons[2].bounds().y > MainWindow::kHeight / 2);

    // The names follow the tooltip language.
    w->setTipLanguage("fr");
    CHECK(icons[0].name() == "DISQUETTE");
    CHECK(icons[2].name() == "CORBEILLE");
    w->setTipLanguage("de");
    CHECK(icons[1].name() == "DISKETTE");
    CHECK(icons[2].name() == "PAPIERKORB");
    w->setTipLanguage("en");

    // A click or a double click does nothing: no menu, no window, no move, nothing written.
    const gemui::Rect home = icons[2].bounds();
    const gemui::Point p = pictureOf(icons[2]);
    press(*w, p);
    w->desktop().mouseDoubleClick(p, {});
    w->desktop().mouseUp(p, {});
    CHECK(icons[2].bounds() == home);
    CHECK_FALSE(w->desktop().hasPopups());
    CHECK_FALSE(w->desktop().hasModal());
    CHECK(h.prefs.count("deskIcons") == 0);

    // A held press moved: the outline follows the pointer, the icon stays until the release.
    w->desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w->desktop().mouseDrag({p.x - 20, p.y - 300}, {});
    CHECK(icons[2].dragging());
    CHECK(icons[2].dragRect() == home.translated(-20, -300));
    CHECK(icons[2].bounds() == home);
    w->desktop().mouseUp({p.x - 20, p.y - 300}, {});
    CHECK_FALSE(icons[2].dragging());
    CHECK(icons[2].bounds() == home.translated(-20, -300));
    const gemui::Rect a = icons[0].bounds(), b = icons[1].bounds();
    CHECK(h.prefs["deskIcons"] == std::to_string(a.x) + "," + std::to_string(a.y) + "," + std::to_string(b.x) + "," +
                                      std::to_string(b.y) + "," + std::to_string(home.x - 20) + "," +
                                      std::to_string(home.y - 300));

    // Kept on the desktop, below the menu bar.
    const gemui::Point q = pictureOf(icons[0]);
    w->desktop().mouseDown(q, gemui::MouseButton::Left, {});
    w->desktop().mouseDrag({-500, -500}, {});
    w->desktop().mouseUp({-500, -500}, {});
    CHECK(icons[0].bounds().x == 0);
    CHECK(icons[0].bounds().y == MainWindow::kMenuH);

    // The places come back with the preference.
    w = std::make_unique<MainWindow>(h);
    CHECK(w->deskIcons()[0].bounds().x == 0);
    CHECK(w->deskIcons()[0].bounds().y == MainWindow::kMenuH);
    CHECK(w->deskIcons()[2].bounds() == home.translated(-20, -300));
    // A damaged preference: the places of TOS.
    h.prefs["deskIcons"] = "12,x";
    w = std::make_unique<MainWindow>(h);
    CHECK(w->deskIcons()[0].bounds() == a);
    CHECK(w->deskIcons()[2].bounds() == home);
    // A number too large for an int (file edited by hand): refused, not undefined behaviour.
    h.prefs["deskIcons"] = "99999999999,0,0,0,0,0";
    w = std::make_unique<MainWindow>(h);
    CHECK(w->deskIcons()[0].bounds() == a);
    CHECK(w->deskIcons()[2].bounds() == home);
    // Spaces after the commas, as sscanf took them: still read.
    h.prefs["deskIcons"] = std::to_string(a.x) + ", " + std::to_string(a.y) + ", " + std::to_string(b.x) + ", " +
                           std::to_string(b.y) + ", " + std::to_string(home.x - 20) + ", " + std::to_string(home.y);
    w = std::make_unique<MainWindow>(h);
    CHECK(w->deskIcons()[2].bounds() == home.translated(-20, 0));
}

TEST_CASE("Desktop: a dragged icon leaves no trace (tenth DAW test, item 129)", "[ui][desktop]") {
    UiHost h;
    MainWindow w(h);
    gemui::Desktop& d = w.desktop();
    d.invalidateAll();
    d.paint();
    const auto& icons = w.deskIcons();
    const gemui::Point p = pictureOf(icons[1]);
    d.mouseDown(p, gemui::MouseButton::Left, {});
    for (int k = 1; k <= 40; ++k) {
        d.mouseDrag({p.x + 7 * k, p.y + 5 * k}, {});
        d.paint();
    }
    d.mouseUp({p.x + 280, p.y + 200}, {});
    d.paint();
    const std::vector<gemui::Color> step = d.surface().pixels();
    d.invalidateAll();
    d.paint();
    size_t diff = 0;
    for (size_t i = 0; i < step.size(); ++i) diff += step[i] != d.surface().pixels()[i];
    CHECK(diff == 0);
}
