// STYM2149 — gemui: GEM menu bar and drop-down menus
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Follows the prototype's openGemMenu / openGemSub:
// - .gemmenu: white background, 2 px border; 14 px entries, 3 px / 14 px padding; hover in reverse video; 2 px
//   separator with 3 px on either side; unavailable entry at 35 % with its reason as a tooltip;
// - check mark "✓" (or non-breaking space) before each entry when the menu has a current entry; ^X shortcut aligned
//   right (at least 32 px after the label, 8 px from the edge); submenu marked by the 8×8 GEM arrow;
// - cascading submenus on hover, straddling the border of the parent menu;
// - closes 260 ms after the pointer leaves (220 ms for a submenu), or on a press elsewhere;
// - a menu of the bar opens when its title is hovered, and the title stays in reverse video;
// - long list in columns (chordCols, .gemmenu.cols): flex-direction:column and flex-wrap:wrap within a height of
//   max(120, desktop height - 120) px, frame included; each column as wide as its widest entry.
// On the keyboard, the menu only takes Escape (it closes): the other keys go to the page, as in the prototype.
// The menus are popups owned by the desktop: a MenuList lives from open() until it closes, and its entries are copies
// (MenuItem::pick is called after the menu is destroyed, so it may open another menu or a box).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "gemui/Desktop.h"
#include "gemui/Painter.h"
#include "gemui/Widget.h"

namespace stym::gemui {

// One entry: a label and its action (pick), a separator, or a submenu (sub not empty).
struct MenuItem {
    std::string label;
    std::string key;                // shortcut shown on the right (^X)
    bool separator = false;
    bool disabled = false;
    std::string why;                // reason for an unavailable entry (tooltip)
    const Bitmap* icon = nullptr;   // icon before the label (envelope shapes)
    std::function<void()> pick;
    // Submenu: entries and checked entry (subMarks: check marks shown; -1: none checked).
    std::vector<MenuItem> sub;
    bool subMarks = false;
    int subCur = -1;

    static MenuItem separatorItem() {
        MenuItem m;
        m.separator = true;
        return m;
    }
};

// Makes every entry of `items`, submenus included, act only while `still()` holds when it is picked: a menu left open
// while its target changes under it (another track or pattern, an undo, a state set by the host) then does nothing.
void pickOnlyWhile(std::vector<MenuItem>& items, const std::function<bool()>& still);

// No check marks (kNoMarks), or index of the checked entry among the plain entries (no separators or submenus).
inline constexpr int kNoMarks = -2;

// Submenu arrow of the GEM desktop (read pixel by pixel from the prototype, GEM_ARROW).
const Bitmap& gemArrow() noexcept;

// A drop-down menu or a submenu. Built and shown by open() (a submenu by its parent on hover); the desktop owns it.
class MenuList : public Widget {
public:
    MenuList(std::vector<MenuItem> items, int checked);
    ~MenuList() override;

    // Opens a menu below an anchor box (layout position, fractional like the button's: the menu's text starts from
    // the exact position). Below the menu bar, the menu overlaps the bar's rule (y = bottom of the bar - 2); below a
    // button, it is 6 px lower. It stays inside the desktop (4 px margin).
    // columns (the prototype's chordCols): entries in as many columns as needed to fit within kColumnsMargin px less
    // than the desktop height (at least kColumnsMin px).
    // checked: index of the checked entry among the plain entries, or a negative value for no check marks. Any menu
    // already open is closed first.
    static MenuList& open(Desktop& d, std::vector<MenuItem> items, int checked, const Box& anchor, bool fromBar,
                          bool columns = false);
    static constexpr int kColumnsMargin = 120, kColumnsMin = 120;
    // Opens a menu below an anchor box over the popups already open, which stay open: picking an entry or leaving the
    // menu closes the menu alone (a menu of a box such as the Euclidean generator).
    static MenuList& openOver(Desktop& d, std::vector<MenuItem> items, int checked, const Box& anchor);

    // Called when the top-level menu closes (bar title to put back in normal video).
    std::function<void()> onClosed;

    // Delayed closing of the whole cascade (260 ms after the pointer leaves), cancelled when the pointer comes back
    // over a menu or over the bar's titles.
    void armClose();
    void cancelClose();

    // Bottom of the menu from entry `item` (following rows and frame), over its whole width; empty beyond.
    Rect rowsFrom(int item) const noexcept;
    // Entries and checked entry (screenshots, tests).
    const std::vector<MenuItem>& items() const noexcept { return items_; }
    int checked() const noexcept { return checked_; }
    // Rectangle of an entry (index in items()); empty if it does not exist (tests).
    Rect itemRect(int item) const noexcept;

    void paint(Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseEnter() override;
    void mouseExit() override;
    void mouseMove(const MouseEvent& e) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    std::string tipAt(Point p) const override;

private:
    // An entry laid out: its rectangle (on the desktop once the menu is placed), its index in items_ and its index
    // among the plain entries.
    struct Row {
        Rect r;
        int item;
        int pickIndex;  // -1: separator or submenu
        int colX = 0;   // column: left edge from inside the frame, and width
        int colW = 0;
    };
    struct Size {
        int w, h;
    };

    int contentWidth(const Row& row) const noexcept;
    void layoutRows();
    // Arranges the entries in columns of at most maxH px (a column always takes at least one entry); returns the size
    // of the menu, frame included. The rectangles of the entries are relative to the inside of the frame.
    Size arrange(int maxH);
    int rowAt(Point p) const noexcept;
    std::string shownLabel(const Row& row) const;
    void openSub(int row);
    void closeSub();
    void armSubClose();
    void cancelSubClose();
    MenuList* root() noexcept;
    void activate(int row);
    void closeAll();
    static MenuList& place(Desktop& d, std::vector<MenuItem> items, int checked, const Box& anchor, bool fromBar,
                           bool columns);

    std::vector<MenuItem> items_;
    std::vector<Row> rows_;
    int checked_ = kNoMarks;
    int hover_ = -1, down_ = -1;     // rows hovered and pressed (a click: press and release on the same row)
    MenuList* parentMenu_ = nullptr;  // menu this submenu comes from; nullptr for the top-level menu
    MenuList* sub_ = nullptr;         // open submenu (owned by the desktop), its row shown in reverse video
    int subRow_ = -1;
    Desktop::TimerId closeTimer_ = 0, subTimer_ = 0;  // closing of the cascade (top-level menu), of the submenu
    Desktop* desk_ = nullptr;
    bool over_ = false;  // opened over other popups (openOver)
};

// .menubar: white background, 2 px bottom rule, 8 px padding; titles (4 px / 10 px padding) on the left, indicators
// (CHIP:, MIDI:) on the right. A title opens its menu on hover or on click; an indicator is clicked.
class MenuBar : public Widget {
public:
    static constexpr int kHeight = 26;

    MenuBar() = default;
    ~MenuBar() override;

    // Titles, from left to right (text, or an icon such as the Fuji logo).
    int addTitle(std::string text, const Bitmap* icon = nullptr);
    // Indicators, from left to right, aligned to the right edge. Titles and indicators share the same numbering (order
    // of addition).
    int addIndicator(std::string text);
    // New text, in reverse video when `inverse` (the prototype's #chipTxt.warn); the entries are laid out again.
    void setIndicator(int index, std::string text, bool inverse);
    void setIndicatorTip(int index, std::string tip);
    // Places the titles from the left and the indicators from the right; to call again after setBounds.
    void layout();

    // Entries of a title's menu (built on opening).
    std::function<std::vector<MenuItem>(int title)> onTitle;
    std::function<void(int entry)> onIndicator;

    // Rectangle of a title or of an indicator (numbering by addition).
    Rect entryRect(int index) const noexcept { return entries_[size_t(index)].r; }

    // Opens the menu of a title below it (onTitle gives its entries), closing any other menu.
    void openTitle(int title);
    int openTitleIndex() const noexcept { return open_; }

    void paint(Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseMove(const MouseEvent& e) override;
    void mouseExit() override;
    void mouseDown(const MouseEvent& e) override;
    std::string tipAt(Point p) const override;
    Pointer pointer() const noexcept override { return Pointer::Arrow; }

private:
    struct Entry {
        std::string text;
        const Bitmap* icon = nullptr;
        bool indicator = false;
        bool inverse = false;
        std::string tip;
        Rect r;
    };
    int entryAt(Point p) const noexcept;

    std::vector<Entry> entries_;
    int open_ = -1;  // entry whose menu is open
    MenuList* menu_ = nullptr;  // that menu (owned by the desktop; set back to nullptr when it closes)
};

}  // namespace stym::gemui
