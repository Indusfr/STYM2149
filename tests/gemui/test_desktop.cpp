// STYM2149 — GEM desktop: the menu bar across its top and the decorative icons of the floppy disks and of the trash
// (ninth DAW test, items 101 and 102, D-302, D-303), their pictures (those of EmuTOS: D-409, D-419, D-423); the Atari
// logo of the menu bar (D-404).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Scenes.h"
#include "UiHost.h"
#include "gemui/Font.h"
#include "pluginui/DeskIcons.h"
#include "pluginui/IconData.h"

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
    // (tenth DAW test, item 124). All three in cells of the icons' size, the first row level with the main window
    // (thirteenth DAW test, item 167): A and B in the first two, the trash in the last.
    constexpr int kW = gemui::DeskIcon::kW, kH = gemui::DeskIcon::kH;
    CHECK(icons[0].bounds().x == 0);
    CHECK(icons[0].bounds().y == MainWindow::kWinY);
    CHECK(icons[1].bounds().y == icons[0].bounds().y);
    CHECK(icons[1].bounds().x == kW);
    CHECK(icons[1].bounds().right() <= MainWindow::kWinX);
    CHECK(icons[2].bounds().x % kW == 0);
    CHECK((icons[2].bounds().y - MainWindow::kWinY) % kH == 0);
    CHECK(icons[2].bounds().right() <= MainWindow::kWidth);
    CHECK(icons[2].bounds().right() + kW > MainWindow::kWidth);
    CHECK(icons[2].bounds().bottom() <= MainWindow::kHeight);
    CHECK(icons[2].bounds().bottom() + kH > MainWindow::kHeight);

    // The names follow the tooltip language.
    w->setTipLanguage("fr");
    CHECK(icons[0].name() == "DISQUETTE");
    CHECK(icons[2].name() == "CORBEILLE");
    w->setTipLanguage("de");
    CHECK(icons[1].name() == "DISKETTE");
    CHECK(icons[2].name() == "PAPIERKORB");
    w->setTipLanguage("en");

    // A click or a double click on A or B does nothing: no menu, no window, no move, nothing written.
    for (size_t k : {size_t(0), size_t(1)}) {
        const gemui::Rect at = icons[k].bounds();
        const gemui::Point p = pictureOf(icons[k]);
        press(*w, p);
        w->desktop().mouseDoubleClick(p, {});
        w->desktop().mouseUp(p, {});
        CHECK(icons[k].bounds() == at);
        CHECK_FALSE(w->desktop().hasPopups());
        CHECK_FALSE(w->desktop().hasModal());
        CHECK(h.prefs.count("deskIcons") == 0);
    }

    // A held press moved: the outline goes from cell to cell with the pointer, the icon stays until the release and
    // lands in the cell nearest to where it was dropped.
    const gemui::Rect home = icons[2].bounds();
    const gemui::Point p = pictureOf(icons[2]);
    w->desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w->desktop().mouseDrag({p.x - 20, p.y - 300}, {});
    CHECK(icons[2].dragging());
    const gemui::Rect cell = home.translated(0, -7 * kH);  // -20: the same column; -7.5 rows: 7 up (a half rounds down)
    CHECK(icons[2].dragRect() == cell);
    w->desktop().mouseDrag({p.x - 50, p.y - 290}, {});
    CHECK(icons[2].dragRect() == cell.translated(-kW, 0));
    CHECK(icons[2].bounds() == home);
    w->desktop().mouseUp({p.x - 50, p.y - 290}, {});
    CHECK_FALSE(icons[2].dragging());
    const gemui::Rect moved = cell.translated(-kW, 0);
    CHECK(icons[2].bounds() == moved);
    const gemui::Rect a = icons[0].bounds(), b = icons[1].bounds();
    CHECK(h.prefs["deskIcons"] == std::to_string(a.x) + "," + std::to_string(a.y) + "," + std::to_string(b.x) + "," +
                                      std::to_string(b.y) + "," + std::to_string(moved.x) + "," +
                                      std::to_string(moved.y));
    // A small move that keeps the icon in its cell is neither a move nor a click.
    const gemui::Point q0 = pictureOf(icons[2]);
    w->desktop().mouseDown(q0, gemui::MouseButton::Left, {});
    w->desktop().mouseDrag({q0.x + 10, q0.y + 6}, {});
    w->desktop().mouseUp({q0.x + 10, q0.y + 6}, {});
    CHECK(icons[2].bounds() == moved);
    CHECK_FALSE(w->desktop().hasModal());

    // Kept on the desktop, below the menu bar, in the first row of cells.
    const gemui::Point q = pictureOf(icons[0]);
    w->desktop().mouseDown(q, gemui::MouseButton::Left, {});
    w->desktop().mouseDrag({-500, -500}, {});
    w->desktop().mouseUp({-500, -500}, {});
    CHECK(icons[0].bounds() == a);
    // A cell that another icon holds refuses the drop: the icon stays.
    const gemui::Point qb = pictureOf(icons[1]);
    w->desktop().mouseDown(qb, gemui::MouseButton::Left, {});
    w->desktop().mouseDrag({qb.x - kW, qb.y}, {});
    CHECK(icons[1].dragRect() == a);
    w->desktop().mouseUp({qb.x - kW, qb.y}, {});
    CHECK(icons[1].bounds() == b);

    // The places come back with the preference.
    w = std::make_unique<MainWindow>(h);
    CHECK(w->deskIcons()[0].bounds() == a);
    CHECK(w->deskIcons()[2].bounds() == moved);
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
    // Spaces after the commas, as sscanf took them: still read. A free place written before the grid: its nearest
    // cell.
    h.prefs["deskIcons"] = std::to_string(a.x) + ", " + std::to_string(a.y) + ", " + std::to_string(b.x) + ", " +
                           std::to_string(b.y) + ", " + std::to_string(home.x - 20) + ", " + std::to_string(home.y + 5);
    w = std::make_unique<MainWindow>(h);
    CHECK(w->deskIcons()[2].bounds() == home);
    // Two icons in one cell: the places of TOS.
    h.prefs["deskIcons"] = "0,0,0,0," + std::to_string(home.x) + "," + std::to_string(home.y);
    w = std::make_unique<MainWindow>(h);
    CHECK(w->deskIcons()[0].bounds() == a);
    CHECK(w->deskIcons()[1].bounds() == b);
}

TEST_CASE("Desktop: a click on the trash is File > New (thirteenth DAW test, item 168)", "[ui][desktop]") {
    UiHost h;
    MainWindow w(h);
    const gemui::Point p = pictureOf(w.deskIcons()[2]);
    // Nothing to lose: at once, no box.
    press(w, p);
    CHECK_FALSE(w.desktop().hasModal());
    // A setting changed: the confirm of File > New, Cancel first.
    h.p(0, plugin::Track::Volume) = 4;
    w.tick(10);
    press(w, p);
    const gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "New project?\nUnsaved work will be lost.");
    key(w, gemui::Key::Enter);  // Cancel
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.p(0, plugin::Track::Volume) == 4.0f);
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

TEST_CASE("Desktop: the Atari logo of the menu bar, the outline of the real logo and its raster (24/10/2026, D-404)",
          "[ui][desktop]") {
    const gemui::Bitmap& f = pluginui::icons::kFuji;
    REQUIRE(f.outline);
    CHECK(f.w == 17);
    CHECK(f.h == 14);
    // Three polygons: the left prong, the centre bar, the right prong; inside the 17 × 14 box.
    REQUIRE(f.outline->ends.size() == 3);
    CHECK(f.outline->xy.size() == 2 * size_t(f.outline->ends.back()));
    for (size_t k = 0; k < f.outline->xy.size(); k += 2) {
        CHECK(f.outline->xy[k] >= 0.f);
        CHECK(f.outline->xy[k] <= 17.f);
        CHECK(f.outline->xy[k + 1] >= 0.f);
        CHECK(f.outline->xy[k + 1] <= 14.f);
    }
    // The bits are the raster of the outline, a pixel lit when its centre lies inside; symmetric.
    int differ = 0, asymmetric = 0;
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            differ += f.at(x, y) != f.outline->inside(x + 0.5, y + 0.5);
            asymmetric += f.at(x, y) != f.at(f.w - 1 - x, y);
        }
    CHECK(differ == 0);
    CHECK(asymmetric == 0);
    // The centre bar three pixels wide from top to bottom, a gap of one pixel beside it at the top; the prongs one
    // pixel wide at the top, their feet on the bottom row at the edges of the box.
    for (int y = 0; y < f.h; ++y) CHECK((f.at(7, y) && f.at(8, y) && f.at(9, y)));
    CHECK(!f.at(6, 0));
    CHECK(f.at(5, 0));
    CHECK(!f.at(4, 0));
    CHECK(f.at(0, f.h - 1));
    CHECK(f.at(1, f.h - 1));
    CHECK(!f.at(2, f.h - 1));

    // The menu bar draws these bits in the framebuffer.
    UiHost h;
    MainWindow w(h);
    gemui::Desktop& d = w.desktop();
    d.invalidateAll();
    d.paint();
    const gemui::Rect title = *w.rectOf("mFuji");
    int same = 0;
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x) {
            const bool black = d.surface().row(title.y + 5 + y)[title.x + 10 + x] == gemui::colors::kBlack;
            same += black == f.at(x, y);
        }
    CHECK(same == f.w * f.h);
}

TEST_CASE("Desktop: the floppy disk and the trash of the EmuTOS desktop (D-409, D-419, D-423)", "[ui][desktop]") {
    namespace icons = pluginui::icons;
    // Every black pixel lies on the white of its mask, which hides the desktop pattern under the whole picture.
    using Picture = std::pair<const gemui::Bitmap*, const gemui::Bitmap*>;
    for (const auto& [data, mask] :
         {Picture{&icons::kFloppy, &icons::kFloppyMask}, Picture{&icons::kTrash, &icons::kTrashMask}}) {
        REQUIRE(data->w == 32);
        REQUIRE(data->h == 32);
        int outside = 0, lit = 0;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                lit += data->at(x, y);
                outside += data->at(x, y) && !mask->at(x, y);
            }
        CHECK(lit > 100);
        CHECK(outside == 0);
    }
    // The drive letter's 6 × 6 cell (the AES's SMALL font) lies on the white label of the disk, clear of its lines.
    int clash = 0, uncovered = 0;
    for (int y = icons::kFloppyLetterY; y < icons::kFloppyLetterY + 6; ++y)
        for (int x = icons::kFloppyLetterX; x < icons::kFloppyLetterX + 6; ++x) {
            clash += icons::kFloppy.at(x, y);
            uncovered += !icons::kFloppyMask.at(x, y);
        }
    CHECK(clash == 0);
    CHECK(uncovered == 0);
    // The letter in the 6 × 6 font, its cell's top-left corner there, as the AES draws ib_char (gr_gicon).
    gemui::DeskIcon icon;
    icon.setBounds({0, 0, gemui::DeskIcon::kW, gemui::DeskIcon::kH});
    icon.setPicture(&icons::kFloppy, &icons::kFloppyMask);
    icon.setLetter('A', icons::kFloppyLetterX, icons::kFloppyLetterY);
    gemui::Surface surface(gemui::DeskIcon::kW, gemui::DeskIcon::kH, gemui::colors::kGreen);
    gemui::Painter p(surface);
    icon.paint(p);
    const int g = gemui::Font::glyph(U'A');
    const gemui::Font::Raster a = gemui::Font::raster(8, g, 0, gemui::Face::kSmall);
    const int px = (gemui::DeskIcon::kW - gemui::DeskIcon::kPicture) / 2 + icons::kFloppyLetterX + a.shift;
    int wrong = 0, ink = 0;
    for (int r = 0; r < a.count; ++r)
        for (int b = 0; b < 6; ++b) {
            const bool lit = (a.bits(r) & (0x8000 >> b)) != 0;
            ink += lit;
            wrong += (surface.at(px + b, icons::kFloppyLetterY + r) == gemui::colors::kBlack) != lit;
        }
    CHECK(ink > 8);
    CHECK(wrong == 0);
}
