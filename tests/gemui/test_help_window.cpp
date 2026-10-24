// STYM2149 — pluginui tests: SHORTCUTS help window (M8, D-189)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Opened by ^/, ^? and the Fuji menu, closed by Esc and its close box; the prototype's nine sections then STYM2149's
// keys, laid out as Chromium lays out the page; own scroll bar; place and scroll kept.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "pluginui/MainWindow.h"

#include "FileHost.h"

using namespace stym;
using namespace stym::testing;
using gemui::Rect;
using pluginui::HelpWindow;
using pluginui::MainWindow;

namespace {

// Ctrl and a key: `code` is the key's character without Shift (0 when the host gives none), `ch` what it types.
bool ctrlKey(MainWindow& w, char32_t ch, int code, bool shift = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = ch;
    e.code = code;
    e.mods.ctrl = true;
    e.mods.shift = shift;
    return w.desktop().keyDown(e);
}

void press(MainWindow& w, gemui::Point p) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(p, {});
}

std::vector<std::string> headings(const HelpWindow::Layout& l) {
    std::vector<std::string> out;
    for (const HelpWindow::Line& line : l.lines)
        if (line.heading) out.emplace_back(line.text);
    return out;
}

// Text lines of the table drawn at height y (content coordinates), left to right.
std::vector<std::string> rowAt(const HelpWindow::Layout& l, double y) {
    std::vector<std::string> out;
    for (const HelpWindow::Line& line : l.lines)
        if (!line.heading && line.y == y) out.emplace_back(line.text);
    return out;
}

}  // namespace

TEST_CASE("Help window: ^/ and ^? open it, Esc and its close box close it", "[ui][help]") {
    FileHost h;
    MainWindow w(h);
    HelpWindow& help = w.help();
    REQUIRE_FALSE(help.isOpen());
    CHECK_FALSE(w.rectOf("helpWin"));

    // ^/ (key Slash): the window, not an alert; #helpWin at (120, 70), 560 px wide, body at 70vh of the desktop.
    CHECK(ctrlKey(w, U'/', '/'));
    REQUIRE(help.isOpen());
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(help.titleBar().title() == "SHORTCUTS");
    CHECK(w.rectOf("helpWin") == Rect{120, 70, 560, 699});
    CHECK(w.rectOf("helpBody") == Rect{126, 102, 548, 661});
    CHECK(help.body().hasVertical());
    CHECK_FALSE(help.body().hasHorizontal());
    CHECK(gemui::FloatWindow::topmost(w, true) == &help);
    pressKey(w, gemui::Key::Escape);
    CHECK_FALSE(help.isOpen());

    // ^? : Shift + Slash on a US keyboard, or the character alone when the host gives no key code.
    CHECK(ctrlKey(w, U'?', '/', true));
    REQUIRE(help.isOpen());
    pressKey(w, gemui::Key::Escape);
    CHECK_FALSE(help.isOpen());
    CHECK(ctrlKey(w, U'?', 0, true));
    REQUIRE(help.isOpen());

    // An open menu closes first; then the topmost window (CLOSERS): the help window, then the one behind it.
    w.mixer().show();
    CHECK(ctrlKey(w, U'/', '/'));  // brought back in front, the menu closed
    CHECK(gemui::FloatWindow::topmost(w, true) == &help);
    w.openMenu(1);
    REQUIRE(w.desktop().hasPopups());
    pressKey(w, gemui::Key::Escape);
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK(help.isOpen());
    pressKey(w, gemui::Key::Escape);
    CHECK_FALSE(help.isOpen());
    CHECK(w.mixer().isOpen());
    pressKey(w, gemui::Key::Escape);
    CHECK_FALSE(w.mixer().isOpen());

    // Close box (left of the title bar).
    CHECK(ctrlKey(w, U'/', '/'));
    const Rect bar = help.titleBar().bounds();
    press(w, {bar.x + 10, bar.y + 10});
    CHECK_FALSE(help.isOpen());
}

TEST_CASE("Help window: Fuji menu entry Shortcuts ^?", "[ui][help]") {
    FileHost h;
    MainWindow w(h);
    w.openMenu(0);
    const auto* menu = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(menu);
    REQUIRE(menu->items().size() == 2);
    CHECK(menu->items()[1].label == "Shortcuts");
    CHECK(menu->items()[1].key == "^?");
    const Rect row = menu->rowsFrom(1);
    press(w, {row.x + 20, row.y + 8});
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(w.help().isOpen());
}

TEST_CASE("Help window: the prototype's nine sections, then STYM2149's keys", "[ui][help]") {
    const auto sections = HelpWindow::sections();
    REQUIRE(sections.size() == 10);
    const std::vector<std::string> proto = {"Windows",        "Generator",      "File",
                                            "Transport and edit", "Writing notes", "Sequencer rows",
                                            "Sequencer view", "Song window",    "Mouse"};
    // Rows of the prototype's HELP; STYM2149 adds one row at the end of Writing notes, and ^D leaves File (the demo
    // pattern) for the end of Generator (the DMA generator, D-301).
    const std::vector<size_t> rows = {8, 6, 2, 8, 9, 7, 6, 8, 9};
    for (size_t i = 0; i < proto.size(); ++i) {
        CAPTURE(i);
        CHECK(sections[i].title == proto[i]);
        CHECK(sections[i].rows.size() == rows[i]);
    }
    CHECK(sections[4].rows.back().key == "Alt + click a note");
    CHECK(sections[1].rows.back().key == "^D");
    CHECK(sections[1].rows.back().text == "DMA (STE)");
    CHECK(sections[9].title == "More keys");
    CHECK(sections[9].rows.front().key == "^T");
    CHECK(sections[9].rows[1].key == "^F");  // Effects (D-313)
    CHECK(sections[9].rows[1].text == "Effects");
    CHECK(sections[9].rows.back().key == "^N");  // File > New (D-225)

    FileHost h;
    MainWindow w(h);
    w.help().show();
    const HelpWindow::Layout& l = w.help().layout();
    std::vector<std::string> titles = proto;
    titles.push_back("More keys");
    CHECK(headings(l) == titles);
    // Positions measured in Chromium 141 with the same list (scrollHeight 2182, 522 px wide inside the scroll bar):
    // heading tops, first column 160 px or the longest key plus 10 px, second column wrapped at spaces. The row of
    // File > New, added since at the end of More keys (D-225), adds one 18 px row; ^L Sequences, gone with its window
    // (D-241), takes one away; ^D, moved from File to Generator (D-301), moves the heading of File down one row; ^F
    // Effects (D-313) adds one.
    CHECK(l.width == 522);
    CHECK(l.height == 2182 + 18 - 18 + 18);
    std::vector<double> tops;
    for (const HelpWindow::Line& line : l.lines)
        if (line.heading) tops.push_back(line.y);
    CHECK(tops == std::vector<double>{8, 200, 322 + 18, 408, 600, 906, 1128, 1348, 1524, 1766});
    CHECK(l.rules.front() == 24);
    CHECK(rowAt(l, 157) == std::vector<std::string>{"Esc", "close the topmost window, then panic (all"});
    CHECK(rowAt(l, 173) == std::vector<std::string>{"notes off)"});
    CHECK(rowAt(l, 573) == std::vector<std::string>{"M", "metronome on / off"});
    CHECK(rowAt(l, 657) == std::vector<std::string>{"Click a note", "resize it to the current Note"});
    for (const HelpWindow::Line& line : l.lines) {
        if (line.text == "Step Sequencer") CHECK(line.x == 168);           // 8 + 160
        if (line.text == "erase it (no menu)") CHECK(line.x == 282);       // 8 + 264 + 10
        if (line.text == "recentre it") CHECK(line.x == 226);              // 8 + 208 + 10
    }
}

TEST_CASE("Help window: scrolled by its bar, reopened where it was left with its scroll, place kept", "[ui][help]") {
    FileHost h;
    {
        MainWindow w(h);
        HelpWindow& help = w.help();
        help.show();
        gemui::ScrollPane& body = help.body();
        CHECK(body.maxScroll() == gemui::Point{0, 2182 + 18 - 18 + 18 - 661});
        // Down arrow at the bottom of the bar: 40 px.
        const Rect down = body.partRect(gemui::ScrollPane::Part::Down);
        press(w, {down.x + 13, down.y + 13});
        CHECK(body.scroll().y == 40);
        // Wheel over the text.
        w.desktop().mouseWheel({300, 400}, 0, 100, {});
        CHECK(body.scroll().y == 140);
        // Dragged by its title bar, closed, reopened: same place, same scroll (as the prototype).
        const Rect bar = help.titleBar().bounds();
        w.desktop().mouseMove({bar.x + 200, bar.y + 10}, {});
        w.desktop().mouseDown({bar.x + 200, bar.y + 10}, gemui::MouseButton::Left, {});
        w.desktop().mouseDrag({bar.x + 240, bar.y + 30}, {});
        w.desktop().mouseUp({bar.x + 240, bar.y + 30}, {});
        CHECK(w.rectOf("helpWin") == Rect{160, 90, 560, 699});
        CHECK(w.rectOf("helpBody") == Rect{166, 122, 548, 661});
        pressKey(w, gemui::Key::Escape);
        help.show();
        CHECK(w.rectOf("helpWin") == Rect{160, 90, 560, 699});
        CHECK(body.scroll().y == 140);
    }
    // A new editor (the plugin keeps the session): the window reopens at its place.
    MainWindow w(h);
    CHECK(w.help().isOpen());
    CHECK(w.rectOf("helpWin") == Rect{160, 90, 560, 699});
}
