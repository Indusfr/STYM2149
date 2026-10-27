// STYM2149 — gemui: GEM menu bar and drop-down menus
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Menu.h"

#include <algorithm>
#include <climits>
#include <memory>

#include "gemui/Controls.h"
#include "gemui/Font.h"

namespace stym::gemui {
namespace {

constexpr int kPx = 14;            // .gemmenu .gmi: font-size 14px
constexpr int kPadX = 14, kPadY = 3;
constexpr int kFrame = 2;          // .gemmenu: 2 px black border, laid on the rule of the button it opens from
constexpr int kIconRowH = 16 + 2 * kPadY;
constexpr int kSepH = 3 + 2 + 3;   // .gmsep: 2 px rule, 3 px margins
constexpr int kKeyGap = 32;        // .gmrow: gap 32px
constexpr int kKeyPull = 6;        // .gmk: margin-right -6px
constexpr double kMenuCloseMs = 260, kSubCloseMs = 220;

// GEM_ARROW, the submenu arrow: 8 rows of one byte, bit 7 = column 0.
const uint8_t kArrowBits[8] = {0x18, 0x1C, 0xF6, 0x83, 0x83, 0xF6, 0x1C, 0x18};
const Bitmap kArrow{8, 8, kArrowBits};

int textW(std::string_view s, int px = kPx) { return int(Font::width(s, px)); }

}  // namespace

const Bitmap& gemArrow() noexcept { return kArrow; }

void pickOnlyWhile(std::vector<MenuItem>& items, const std::function<bool()>& still) {
    for (MenuItem& m : items) {
        if (m.pick)
            m.pick = [still, pick = std::move(m.pick)] {
                if (still()) pick();
            };
        pickOnlyWhile(m.sub, still);
    }
}

// ----------------------------------------------------------------------------------------------- MenuList

MenuList::MenuList(std::vector<MenuItem> items, int checked) : items_(std::move(items)), checked_(checked) {}

// Runs when the desktop closes the menu: its parent forgets it, and onClosed reports the closing (the bar's menu).
MenuList::~MenuList() {
    if (desk_) {
        desk_->cancel(closeTimer_);
        desk_->cancel(subTimer_);
    }
    if (parentMenu_ && parentMenu_->sub_ == this) {
        parentMenu_->sub_ = nullptr;
        parentMenu_->subRow_ = -1;
        parentMenu_->repaint();
    }
    if (onClosed) onClosed();
}

// With check marks, every plain entry starts with "✓ " or with two spaces, so that the labels stay aligned.
std::string MenuList::shownLabel(const Row& row) const {
    const MenuItem& it = items_[size_t(row.item)];
    if (checked_ == kNoMarks || row.pickIndex < 0) return it.label;
    return (row.pickIndex == checked_ ? "✓ " : "  ") + it.label;
}

// Width of an entry without its padding: label, then the submenu arrow or the shortcut after the 32 px gap, pulled
// 6 px into the right padding; an icon and its 8 px gap before the label.
int MenuList::contentWidth(const Row& row) const noexcept {
    const MenuItem& it = items_[size_t(row.item)];
    if (it.separator) return 0;
    int c = textW(shownLabel(row));
    if (!it.sub.empty()) c += kKeyGap + kArrow.w - kKeyPull;
    else if (!it.key.empty()) c += kKeyGap + textW(it.key) - kKeyPull;
    if (it.icon) c += it.icon->w + 8;
    return c;
}

MenuList::Size MenuList::arrange(int maxH) {
    // Wrapping column flex box: an entry that would exceed the height opens a column; the entries of a column take
    // its width (align-items:stretch), the menu takes the sum of the columns and the height of the tallest.
    int x = 0, tallest = 0;
    size_t first = 0;
    while (first < rows_.size()) {
        size_t end = first;
        int y = 0, content = 0;
        while (end < rows_.size() && (end == first || y + rows_[end].r.h <= maxH)) {
            rows_[end].r.y = y;
            y += rows_[end].r.h;
            content = std::max(content, contentWidth(rows_[end]));
            ++end;
        }
        const int colW = content + 2 * kPadX;
        for (size_t k = first; k < end; ++k) {
            rows_[k].colX = rows_[k].r.x = x;
            rows_[k].colW = rows_[k].r.w = colW;
        }
        x += colW;
        tallest = std::max(tallest, y);
        first = end;
    }
    if (rows_.empty()) x = 2 * kPadX;
    return {x + 4, tallest + 4};  // the 2 px frame on each side
}

// One row per item, stacked from y = 0 with its height; arrange() then sets the columns and the widths.
void MenuList::layoutRows() {
    rows_.clear();
    int pick = 0, y = 0;
    for (int i = 0; i < int(items_.size()); ++i) {
        const MenuItem& it = items_[size_t(i)];
        const int pi = it.separator || !it.sub.empty() ? -1 : pick++;
        rows_.push_back({{0, y, 0, 0}, i, pi});
        // A "✓" check mark (fallback font in the prototype) enlarges the line: 16 px instead of 14.
        const int h = it.separator ? kSepH : it.icon ? kIconRowH : Font::lineBox(shownLabel(rows_.back()), kPx).height() + 2 * kPadY;
        rows_.back().r.h = h;
        y += h;
    }
}

MenuList& MenuList::open(Desktop& d, std::vector<MenuItem> items, int checked, const Box& anchor, bool fromBar,
                         bool columns) {
    d.closePopups();
    return place(d, std::move(items), checked, anchor, fromBar, columns);
}

MenuList& MenuList::openOver(Desktop& d, std::vector<MenuItem> items, int checked, const Box& anchor) {
    // The box under the menu stays open (twelfth DAW test, item 154: the note menu of the Euclidean generator closed
    // the generator, whose pick then wrote into a box already destroyed).
    MenuList& m = place(d, std::move(items), checked, anchor, false, false);
    m.over_ = true;
    d.markOverlay(m);
    return m;
}

MenuList& MenuList::place(Desktop& d, std::vector<MenuItem> items, int checked, const Box& anchor, bool fromBar,
                          bool columns) {
    auto m = std::make_unique<MenuList>(std::move(items), checked < 0 ? kNoMarks : checked);
    m->desk_ = &d;
    m->layoutRows();
    // chordCols: max-height of max(120, zH() - 120) px, frame included (box-sizing:border-box).
    const int maxH = columns ? std::max(kColumnsMin, d.height() - kColumnsMargin) - 4 : INT_MAX;
    const Size size = m->arrange(maxH);
    const double w = size.w, h = size.h;
    // Below the anchor; moved left when it would leave the desktop's right edge, opened above the anchor when it
    // would leave the bottom. Under a button, as under the bar, the menu's frame lies on the button's rule, with no
    // gap (GEM; 6 px lower in the prototype, 24/10/2026).
    double x = anchor.x;
    double y = fromBar ? anchor.bottom() : anchor.bottom() - kFrame;
    if (x + w > d.width() - 4) x = d.width() - w - 4;
    if (y + h > d.height() - 4) y = std::max(4.0, anchor.y - h + kFrame);
    m->setBox({x, y, w, h});
    // The rows were laid out inside the frame: move them to the desktop, rounded edge by edge like the box.
    for (Row& r : m->rows_)
        r.r = Box{x + 2 + r.r.x, y + 2 + r.r.y, double(r.r.w), double(r.r.h)}.snapped();
    MenuList& ref = *m;
    d.openPopup(std::move(m));
    return ref;
}

Rect MenuList::itemRect(int item) const noexcept {
    for (const Row& row : rows_)
        if (row.item == item) return row.r;
    return {};
}

Rect MenuList::rowsFrom(int item) const noexcept {
    const Rect b = bounds();
    for (const Row& row : rows_)
        if (row.item >= item) return {b.x, row.r.y, b.w, b.bottom() - row.r.y};
    return {};
}

MenuList* MenuList::root() noexcept {
    MenuList* m = this;
    while (m->parentMenu_) m = m->parentMenu_;
    return m;
}

// One timer for the whole cascade, held by the top-level menu: it closes every menu.
void MenuList::armClose() {
    MenuList* r = root();
    r->desk_->cancel(r->closeTimer_);
    r->closeTimer_ = r->desk_->after(kMenuCloseMs, [r] { r->closeAll(); });
}

void MenuList::closeAll() {
    // Closes this menu and its submenus; a menu opened over other popups leaves them open.
    if (over_) desk_->closePopup(*this);
    else desk_->closePopups();
}

void MenuList::cancelClose() {
    MenuList* r = root();
    r->desk_->cancel(r->closeTimer_);
}

void MenuList::armSubClose() {
    if (!sub_) return;
    desk_->cancel(subTimer_);
    subTimer_ = desk_->after(kSubCloseMs, [this] {
        subTimer_ = 0;
        closeSub();
    });
}

void MenuList::cancelSubClose() { desk_->cancel(subTimer_); }

void MenuList::closeSub() {
    if (!sub_) return;
    desk_->closePopup(*sub_);  // sets sub_ back to null (destructor)
}

void MenuList::openSub(int row) {
    if (sub_ && subRow_ == row) {
        cancelSubClose();
        return;
    }
    closeSub();
    cancelSubClose();
    const MenuItem& it = items_[size_t(rows_[size_t(row)].item)];
    auto m = std::make_unique<MenuList>(it.sub, it.subMarks ? it.subCur : kNoMarks);
    m->desk_ = desk_;
    m->parentMenu_ = this;
    m->layoutRows();
    const Size size = m->arrange(INT_MAX);
    const int w = size.w, h = size.h;
    // To the right of the parent, over its border; on its left when there is no room on the right, and moved up to
    // stay inside the desktop.
    const Rect pr = bounds();
    int x = pr.right() - 2, y = rows_[size_t(row)].r.y - 1;  // the two rules merge
    if (x + w > desk_->width() - 4) x = std::max(4, pr.x - w + 2);
    if (y + h > desk_->height() - 4) y = std::max(4, desk_->height() - h - 4);
    m->setBox({double(x), double(y), double(w), double(h)});
    for (Row& r : m->rows_) r.r = {x + 2 + r.r.x, y + 2 + r.r.y, r.r.w, r.r.h};
    sub_ = m.get();
    subRow_ = row;
    repaint();
    desk_->openPopup(std::move(m));
}

int MenuList::rowAt(Point p) const noexcept {
    for (int i = 0; i < int(rows_.size()); ++i)
        if (rows_[size_t(i)].r.contains(p)) return i;
    return -1;
}

// Entering a menu keeps the cascade and the submenus up to it open; leaving it arms their closing (kSubCloseMs for a
// submenu, kMenuCloseMs for the whole cascade), which entering another menu of the cascade cancels.
void MenuList::mouseEnter() {
    cancelClose();
    for (MenuList* m = parentMenu_; m; m = m->parentMenu_) m->cancelSubClose();
}

void MenuList::mouseExit() {
    if (hover_ >= 0) repaint(rows_[size_t(hover_)].r);
    hover_ = -1;
    armSubClose();
    for (MenuList* m = parentMenu_; m; m = m->parentMenu_) m->armSubClose();
    armClose();
}

void MenuList::mouseMove(const MouseEvent& e) {
    const int row = rowAt(e.pos);
    if (row == hover_) return;
    if (hover_ >= 0) repaint(rows_[size_t(hover_)].r);
    hover_ = row;
    if (row < 0) return;
    repaint(rows_[size_t(row)].r);
    const MenuItem& it = items_[size_t(rows_[size_t(row)].item)];
    if (it.separator) return;
    if (!it.sub.empty() && !it.disabled) {
        openSub(row);
    } else {
        // Hovering another entry (or an unavailable submenu) closes the cascade.
        cancelSubClose();
        closeSub();
    }
}

void MenuList::mouseDown(const MouseEvent& e) { down_ = rowAt(e.pos); }

void MenuList::mouseUp(const MouseEvent& e) {
    const int row = rowAt(e.pos);
    const int was = down_;
    down_ = -1;
    if (row < 0 || row != was) return;
    activate(row);
}

void MenuList::activate(int row) {
    const MenuItem& it = items_[size_t(rows_[size_t(row)].item)];
    if (it.separator || it.disabled) return;
    if (!it.sub.empty()) {
        openSub(row);
        return;
    }
    // Close first (the menu is destroyed), then act.
    std::function<void()> pick = it.pick;
    root()->closeAll();
    if (pick) pick();
}

std::string MenuList::tipAt(Point p) const {
    const int row = rowAt(p);
    if (row < 0) return {};
    const MenuItem& it = items_[size_t(rows_[size_t(row)].item)];
    return it.disabled ? it.why : std::string();
}

void MenuList::paint(Painter& p) {
    const Rect b = bounds();
    p.fill(b, colors::kWhite);
    p.frame(b, kFrame, colors::kBlack);
    for (int i = 0; i < int(rows_.size()); ++i) {
        const Row& row = rows_[size_t(i)];
        const MenuItem& it = items_[size_t(row.item)];
        if (it.separator) {
            p.fill({row.r.x, row.r.y + 3, row.r.w, 2}, colors::kBlack);
            continue;
        }
        const bool inverse = !it.disabled && (i == hover_ || (sub_ && i == subRow_));
        if (it.disabled) p.beginOpacity(row.r.intersect(p.clip()), kOpacityButtonOff);
        if (inverse) p.fill(row.r, colors::kBlack);
        const Color ink = inverse ? colors::kWhite : colors::kBlack;
        // Text at the exact position of the box (fractional below a button at a half pixel), rounded frames; a column
        // starts from the sum of the widths of those before it.
        const double colX = box().x + 2 + row.colX;
        double x = colX + kPadX;
        int top = row.r.y + kPadY;
        if (it.icon) {
            p.bitmap(*it.icon, snap(x), top, ink);
            x += it.icon->w + 8;
            top += (it.icon->h - kPx) / 2;
        }
        p.text(shownLabel(row), x, top, kPx, ink);
        // Arrow or shortcut right-aligned 6 px into the right padding (kKeyPull); the arrow centred on the text line.
        const double right = colX + row.colW - kPadX + kKeyPull;
        if (!it.sub.empty()) p.bitmap(kArrow, snap(right) - kArrow.w, row.r.y + kPadY + (kPx - kArrow.h) / 2, ink);
        else if (!it.key.empty()) p.text(it.key, right - Font::width(it.key, kPx), row.r.y + kPadY, kPx, ink);
        if (it.disabled) p.endOpacity();
    }
}

// ------------------------------------------------------------------------------------------------ MenuBar

MenuBar::~MenuBar() {
    // The open menu belongs to the desktop and can outlive the bar: its callback must not reach the destroyed bar.
    if (menu_) menu_->onClosed = nullptr;
}

int MenuBar::addTitle(std::string text, const Bitmap* icon) {
    Entry e;
    e.text = std::move(text);
    e.icon = icon;
    entries_.push_back(std::move(e));
    layout();
    return int(entries_.size()) - 1;
}

int MenuBar::addIndicator(std::string text) {
    Entry e;
    e.text = std::move(text);
    e.indicator = true;
    entries_.push_back(std::move(e));
    layout();
    return int(entries_.size()) - 1;
}

void MenuBar::setIndicator(int index, std::string text, bool inverse) {
    Entry& e = entries_[size_t(index)];
    if (e.text == text && e.inverse == inverse) return;
    repaint(e.r);
    e.text = std::move(text);
    e.inverse = inverse;
    layout();
    repaint();
}

void MenuBar::setIndicatorTip(int index, std::string tip) { entries_[size_t(index)].tip = std::move(tip); }

// .menubar: 8 px padding at both ends; each entry is its 16 px text (or icon) with 10 px on either side, as high as
// the bar above its 2 px rule.
void MenuBar::layout() {
    const Rect b = bounds();
    int x = b.x + 8;
    for (Entry& e : entries_) {
        if (e.indicator) continue;
        const int w = (e.icon ? e.icon->w : textW(e.text, 16)) + 20;
        e.r = {x, b.y, w, kHeight - 2};
        x += w;
    }
    int right = b.right() - 8;
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (!it->indicator) continue;
        const int w = textW(it->text, 16) + 20;
        it->r = {right - w, b.y, w, kHeight - 2};
        right -= w;
    }
}

int MenuBar::entryAt(Point p) const noexcept {
    for (int i = 0; i < int(entries_.size()); ++i)
        if (entries_[size_t(i)].r.contains(p)) return i;
    return -1;
}

void MenuBar::openTitle(int title) {
    Desktop* d = desktop();
    if (!d || !onTitle || title < 0 || entries_[size_t(title)].indicator) return;
    std::vector<MenuItem> items = onTitle(title);
    MenuList& m = MenuList::open(*d, std::move(items), kNoMarks, boxOf(entries_[size_t(title)].r), true);
    // The previous menu (if there was one) has just been closed: its callback has set open_ back to -1.
    open_ = title;
    menu_ = &m;
    m.onClosed = [this] {
        if (open_ >= 0) repaint(entries_[size_t(open_)].r);
        open_ = -1;
        menu_ = nullptr;
    };
    repaint(entries_[size_t(title)].r);
}

// Hovering a title opens its menu (and switches from one menu to the next); leaving the titles arms the closing.
void MenuBar::mouseMove(const MouseEvent& e) {
    const int i = entryAt(e.pos);
    if (i >= 0 && !entries_[size_t(i)].indicator) {
        if (menu_) menu_->cancelClose();
        if (i != open_) openTitle(i);
    } else if (menu_) {
        menu_->armClose();
    }
}

void MenuBar::mouseExit() {
    if (menu_) menu_->armClose();
}

void MenuBar::mouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return;
    const int i = entryAt(e.pos);
    if (i < 0) return;
    if (entries_[size_t(i)].indicator) {
        if (onIndicator) onIndicator(i);
        return;
    }
    openTitle(i);
}

std::string MenuBar::tipAt(Point p) const {
    const int i = entryAt(p);
    return i < 0 ? std::string() : entries_[size_t(i)].tip;
}

void MenuBar::paint(Painter& p) {
    const Rect b = bounds();
    p.fill({b.x, b.y, b.w, kHeight - 2}, colors::kWhite);
    p.fill({b.x, b.y + kHeight - 2, b.w, 2}, colors::kBlack);
    for (int i = 0; i < int(entries_.size()); ++i) {
        const Entry& e = entries_[size_t(i)];
        const bool inverse = i == open_ || e.inverse;
        if (inverse) p.fill(e.r, colors::kBlack);
        const Color ink = inverse ? colors::kWhite : colors::kBlack;
        // After the 10 px left padding; text 4 px from the top for titles and indicators alike, an icon 5 px.
        if (e.icon) p.bitmap(*e.icon, e.r.x + 10, e.r.y + 5, ink);
        else p.text(e.text, e.r.x + 10, e.r.y + 4, 16, ink);
    }
}

}  // namespace stym::gemui
