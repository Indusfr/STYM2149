// STYM2149 — gemui: GEM desktop
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Desktop.h"

#include <algorithm>

#include "gemui/Controls.h"
#include "gemui/Font.h"

namespace stym::gemui {
namespace {

// Tooltip: 2 px frame, 12 px text (size of the prototype's .hint) in the 8×8 font (D-072), one line per "\n", wrapped
// at spaces beyond half of the desktop (like the alerts).
constexpr int kTipPx = 12;
constexpr int kTipLine = 14;
constexpr int kTipPadX = 6, kTipPadY = 4;

std::vector<std::string_view> splitLines(std::string_view s) {
    std::vector<std::string_view> out;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find('\n', a);
        out.push_back(s.substr(a, b == std::string_view::npos ? std::string_view::npos : b - a));
        if (b == std::string_view::npos) break;
        a = b + 1;
    }
    return out;
}

// Greedy wrapping at spaces by measured width; a word wider than maxWidth stays whole on a line of its own.
std::vector<std::string> wrapTip(std::string_view s, double maxWidth) {
    std::vector<std::string> out;
    for (std::string_view l : splitLines(s)) {
        std::string line;
        for (size_t a = 0;;) {
            const size_t b = l.find(' ', a);
            const std::string_view word = l.substr(a, b == std::string_view::npos ? std::string_view::npos : b - a);
            std::string longer = line.empty() ? std::string(word) : line + ' ' + std::string(word);
            if (!line.empty() && Font::width(longer, kTipPx) > maxWidth) {
                out.push_back(std::move(line));
                line = std::string(word);
            } else {
                line = std::move(longer);
            }
            if (b == std::string_view::npos) break;
            a = b + 1;
        }
        out.push_back(std::move(line));
    }
    return out;
}

// True when w is `ancestor` or one of its descendants (false for a null w).
bool within(const Widget* w, const Widget* ancestor) noexcept {
    for (; w; w = w->parent())
        if (w == ancestor) return true;
    return false;
}

}  // namespace

Desktop::Desktop(int width, int height) : surface_(width, height, colors::kBlack) {
    dirty_.reserve(kMaxDirty);
    painted_.reserve(kMaxDirty);
    dirty_.push_back(surface_.bounds());  // the first paint covers the whole desktop
}

Desktop::~Desktop() {
    // The owned widgets (menus, boxes) go away before the rest of the desktop, the last opened first: a submenu still
    // reads its parent menu when it is destroyed.
    while (!modals_.empty()) {
        std::unique_ptr<Widget> w = std::move(modals_.back());
        modals_.pop_back();
    }
    while (!popups_.empty()) {
        std::unique_ptr<Widget> w = std::move(popups_.back());
        popups_.pop_back();
    }
    if (root_) root_->owner_ = nullptr;
}

void Desktop::adopt(Widget& w) {
    w.owner_ = this;
    invalidate(w.bounds());
}

void Desktop::setRoot(Widget* root) {
    if (root_) root_->owner_ = nullptr;
    root_ = root;
    hover_ = capture_ = focus_ = nullptr;
    if (root_) adopt(*root_);
    invalidateAll();
}

Widget& Desktop::openPopup(std::unique_ptr<Widget> popup) {
    hideTip();
    adopt(*popup);
    popups_.push_back(std::move(popup));
    updateHover(mouse_);
    return *popups_.back();
}

void Desktop::closePopup(Widget& popup) {
    const auto it = std::find_if(popups_.begin(), popups_.end(), [&](const auto& p) { return p.get() == &popup; });
    if (it == popups_.end()) return;
    // By index: popping the element that `it` points at invalidates it, and comparing it afterwards is undefined
    // (a checked standard library, such as MSVC's in Debug, stops there).
    const auto keep = size_t(it - popups_.begin());
    while (popups_.size() > keep) {
        // Destroy outside the vector: the destructor calls forget() and invalidate() back.
        std::unique_ptr<Widget> w = std::move(popups_.back());
        popups_.pop_back();
        overlays_.erase(std::remove(overlays_.begin(), overlays_.end(), w.get()), overlays_.end());
        w.reset();
    }
    updateHover(mouse_);
    if (popups_.empty() && onPopupsClosed) onPopupsClosed();
}

void Desktop::closePopups() {
    if (popups_.empty()) return;
    closePopup(*popups_.front());
}

void Desktop::markOverlay(Widget& popup) { overlays_.push_back(&popup); }

Widget& Desktop::openModal(std::unique_ptr<Widget> modal, bool keepPopups) {
    if (!keepPopups) closePopups();
    hideTip();
    adopt(*modal);
    modals_.push_back(std::move(modal));
    invalidateAll();  // the veil covers the whole desktop
    if (Widget* c = capture_) {
        capture_ = nullptr;
        c->mouseCancel();
    }
    lastPressed_ = nullptr;
    updateHover(mouse_);
    return *modals_.back();
}

void Desktop::closeModal(Widget& modal) {
    const auto it = std::find_if(modals_.begin(), modals_.end(), [&](const auto& p) { return p.get() == &modal; });
    if (it == modals_.end()) return;
    std::unique_ptr<Widget> w = std::move(*it);
    modals_.erase(it);
    w.reset();
    invalidateAll();
    updateHover(mouse_);
}

Desktop::TimerId Desktop::after(double ms, std::function<void()> fn) {
    const TimerId id = nextTimer_++;
    if (nextTimer_ == 0) nextTimer_ = 1;  // on wrap-around, skip 0 ("no timer")
    timers_.push_back({id, now_ + ms, std::move(fn)});
    return id;
}

void Desktop::cancel(TimerId& id) noexcept {
    if (!id) return;
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [&](const Timer& t) { return t.id == id; }),
                  timers_.end());
    id = 0;
}

void Desktop::tick(double nowMs) {
    now_ = nowMs;
    // A timer can create or cancel others: each due timer is removed before it is called.
    for (;;) {
        auto it = std::find_if(timers_.begin(), timers_.end(), [&](const Timer& t) { return t.due <= now_; });
        if (it == timers_.end()) break;
        std::function<void()> fn = std::move(it->fn);
        timers_.erase(it);
        fn();
    }
}

// -------------------------------------------------------------------------------------------------- Mouse

// A hidden, disabled or inert widget hides its whole subtree from the mouse. A widget that blocks the mouse (a window)
// is returned even if it does not accept it, so that nothing it covers gets the press.
Widget* Desktop::find(Widget& w, Point p) const {
    if (!w.visible_ || !w.enabled_ || w.inert_) return nullptr;
    if (!w.clipsChildren() || w.childArea().contains(p))
        for (auto it = w.children_.rbegin(); it != w.children_.rend(); ++it)
            if (Widget* f = find(**it, p)) return f;
    return (w.acceptsMouse() || w.blocksMouse()) && w.hit(p) ? &w : nullptr;
}

bool Desktop::insidePopups(Point p) const {
    return std::any_of(popups_.begin(), popups_.end(), [&](const auto& w) { return w->visible() && w->hit(p); });
}

Widget* Desktop::target(Point p) const {
    if (!modals_.empty()) return find(*modals_.back(), p);
    for (auto it = popups_.rbegin(); it != popups_.rend(); ++it)
        if (Widget* f = find(**it, p)) return f;
    if (insidePopups(p)) return nullptr;  // background of a menu: nothing below
    return root_ ? find(*root_, p) : nullptr;
}

void Desktop::updateHover(Point p) {
    Widget* t = capture_ ? capture_ : target(p);
    if (t != hover_) {
        Widget* old = hover_;
        hover_ = t;
        if (old) old->mouseExit();
        if (hover_) hover_->mouseEnter();
    }
}

void Desktop::mouseMove(Point p, Modifiers m) {
    const bool moved = !(p == mouse_);
    mouse_ = p;
    updateHover(p);
    if (hover_) hover_->mouseMove({p, MouseButton::Left, m});
    if (moved) armTip();
}

void Desktop::mouseDown(Point p, MouseButton b, Modifiers m) {
    mouse_ = p;
    hideTip();
    cancel(tipTimer_);
    // A press outside the menus closes them; the press then goes to what is below (as in the prototype). A value field
    // of a popup being typed into confirms first, as a press elsewhere does (closing would only forget it).
    if (modals_.empty() && !popups_.empty() && !insidePopups(p)) {
        if (std::any_of(popups_.begin(), popups_.end(), [this](const auto& w) { return within(focus_, w.get()); }))
            setKeyFocus(nullptr);
        closePopups();
    }
    // A press on a popup below an overlay closes the overlay (and what was opened after it).
    for (size_t i = popups_.size(); modals_.empty() && i-- > 0;) {
        if (std::find(overlays_.begin(), overlays_.end(), popups_[i].get()) == overlays_.end()) continue;
        bool hit = false;
        for (size_t j = i; j < popups_.size() && !hit; ++j) hit = popups_[j]->visible() && popups_[j]->hit(p);
        if (!hit) closePopup(*popups_[i]);
        break;
    }
    Widget* t = target(p);
    // Then, in this order: interception, loss of focus, window brought to the front, onPress, the widget's mouseDown.
    if (b == MouseButton::Left && modals_.empty() && popups_.empty() && interceptPress && interceptPress(t)) {
        capture_ = nullptr;
        lastPressed_ = nullptr;
        updateHover(p);
        return;
    }
    // A press outside the focused widget takes the focus away (a value field being typed into confirms: focusLost).
    if (focus_ && t != focus_ && !within(t, focus_)) setKeyFocus(nullptr);
    // Window hit: in front of the others, before it receives the press (the prototype's capture listener).
    if (modals_.empty() && root_)
        for (Widget* w = t; w; w = w->parent_)
            if (w->parent_ == root_) {
                if (dynamic_cast<WindowFrame*>(w)) w->toFront();
                break;
            }
    capture_ = t;
    updateHover(p);
    if (onPress) onPress(t);
    if (t) t->mouseDown({p, b, m});
}

void Desktop::mouseDrag(Point p, Modifiers m) {
    mouse_ = p;
    if (capture_) capture_->mouseDrag({p, MouseButton::Left, m});
}

void Desktop::mouseUp(Point p, Modifiers m) {
    mouse_ = p;
    Widget* t = capture_;
    capture_ = nullptr;
    lastPressed_ = t;  // a double click that follows goes to it
    if (t) t->mouseUp({p, MouseButton::Left, m});
    updateHover(p);
}

void Desktop::mouseDoubleClick(Point p, Modifiers m) {
    // The second click has already gone through (press, release): the double click goes to the widget that received
    // it, if it is still under the pointer (the browser's dblclick, on the element of both clicks).
    mouse_ = p;
    Widget* t = lastPressed_;
    if (t && target(p) == t) t->mouseDoubleClick({p, MouseButton::Left, m});
}

void Desktop::mouseExit() {
    mouse_ = {-1, -1};
    hideTip();
    cancel(tipTimer_);
    // During a press, the widget that holds the mouse keeps its hover until the release.
    if (!capture_ && hover_) {
        Widget* old = hover_;
        hover_ = nullptr;
        old->mouseExit();
    }
}

void Desktop::mouseWheel(Point p, double dx, double dy, Modifiers m) {
    mouse_ = p;
    hideTip();
    cancel(tipTimer_);
    if (capture_) return;  // button held down: the wheel waits
    for (Widget* w = target(p); w; w = w->parent_)
        if (w->mouseWheel({p, dx, dy, m})) break;
    updateHover(p);
}

Pointer Desktop::pointer() const noexcept {
    if (busy_) return Pointer::Busy;
    const Widget* w = capture_ ? capture_ : hover_;
    return w ? w->pointerAt(mouse_) : Pointer::Arrow;
}

// ----------------------------------------------------------------------------------------------- Keyboard

bool Desktop::keyDown(const KeyEvent& e) {
    hideTip();
    if (!modals_.empty()) {
        Widget& top = *modals_.back();
        for (Widget* w = focus_ && within(focus_, &top) ? focus_ : &top; w; w = w->parent())
            if (w->keyDown(e)) return true;
        return true;  // a modal box keeps the keyboard
    }
    if (!popups_.empty() && e.key == Key::Escape) {
        closePopups();
        return true;
    }
    for (Widget* w = focus_; w; w = w->parent())
        if (w->keyDown(e)) return true;
    // With a focus, the walk up from it has already offered the key to the root (when the focus is in its tree).
    return root_ && !focus_ ? root_->keyDown(e) : false;
}

void Desktop::keyUp(const KeyEvent& e) {
    if (root_) root_->keyUp(e);
}

void Desktop::setKeyFocus(Widget* w) {
    if (w == focus_) return;
    Widget* old = focus_;
    focus_ = w;
    if (old) old->focusLost();
}

void Desktop::release(const Widget* w) {
    if (within(capture_, w)) {
        Widget* c = capture_;
        capture_ = nullptr;
        c->mouseCancel();
    }
    if (within(focus_, w)) {
        Widget* f = focus_;
        focus_ = nullptr;
        f->focusDropped();
    }
}

void Desktop::forget(const Widget* w) noexcept {
    if (root_ == w) root_ = nullptr;  // root destroyed before the desktop: ~Desktop must not touch it any more
    if (within(hover_, w)) hover_ = nullptr;
    if (within(capture_, w)) capture_ = nullptr;
    if (within(lastPressed_, w)) lastPressed_ = nullptr;
    if (within(focus_, w)) focus_ = nullptr;
}

// ------------------------------------------------------------------------------------------------- Tooltips

void Desktop::setTooltips(bool on) {
    tipsOn_ = on;
    if (!on) {
        hideTip();
        cancel(tipTimer_);
    }
}

// Unlike find(), the enabled state does not count: a greyed-out control still shows its tooltip.
Widget* Desktop::tipTarget(Widget& w, Point p) const {
    if (w.inert_) return nullptr;
    const bool inChildren = !w.clipsChildren() || w.childArea().contains(p);
    if (!w.visible_ || !w.bounds_.contains(p)) {
        // A child can overflow its parent: look for it anyway.
        for (auto it = w.children_.rbegin(); w.visible_ && inChildren && it != w.children_.rend(); ++it)
            if (Widget* f = tipTarget(**it, p)) return f;
        return nullptr;
    }
    for (auto it = w.children_.rbegin(); inChildren && it != w.children_.rend(); ++it)
        if (Widget* f = tipTarget(**it, p)) return f;
    // A window stops the search even without a tooltip: nothing it covers shows one.
    return w.hit(p) && (w.blocksMouse() || !w.tipAt(p).empty()) ? &w : nullptr;
}

// Called on each move of the pointer: a shown tooltip stays while the text under the pointer is the same; otherwise
// the delay starts again, so a tooltip appears after kTipDelayMs without movement (D-072). The translation is made
// when it appears, the comparison on the widget's own text.
void Desktop::armTip() {
    std::string text;
    if (tipsOn_ && !capture_) {
        Widget* t = nullptr;
        if (!modals_.empty()) t = tipTarget(*modals_.back(), mouse_);
        else {
            for (auto it = popups_.rbegin(); !t && it != popups_.rend(); ++it) t = tipTarget(**it, mouse_);
            if (!t && !insidePopups(mouse_) && root_) t = tipTarget(*root_, mouse_);
        }
        if (t) text = t->tipAt(mouse_);
    }
    if (tipShown_ && text == tipKey_) return;  // same tooltip: it stays
    hideTip();
    cancel(tipTimer_);
    if (text.empty()) return;
    tipTimer_ = after(kTipDelayMs, [this, text] {
        tipTimer_ = 0;
        tipKey_ = text;
        tipText_ = translateTip ? translateTip(text) : text;
        tipAt_ = mouse_;
        tipShown_ = true;
        invalidate(tipRect());
    });
}

void Desktop::hideTip() {
    if (!tipShown_) return;
    invalidate(tipRect());
    tipShown_ = false;
    tipKey_.clear();
    tipText_.clear();
}

std::vector<std::string> Desktop::tipLines() const {
    return wrapTip(tipText_, width() / 2 - 2 * (kTipPadX + 2));
}

// Box of the tooltip: its lines (wrapped so that the box fits in half the desktop's width), padding and 2 px frame;
// 12 px right of and 20 px below the pointer, pushed left to stay 4 px inside the desktop, or put above the pointer
// (8 px gap) when it would run past the bottom.
Rect Desktop::tipRect() const {
    int w = 0, n = 0;
    for (const std::string& l : tipLines()) {
        w = std::max(w, int(Font::width(l, kTipPx)));
        ++n;
    }
    Rect r{tipAt_.x + 12, tipAt_.y + 20, w + 2 * (kTipPadX + 2), n * kTipLine + 2 * (kTipPadY + 2)};
    if (r.right() > width() - 4) r.x = std::max(4, width() - 4 - r.w);
    if (r.bottom() > height() - 4) r.y = std::max(4, tipAt_.y - 8 - r.h);
    return r;
}

void Desktop::paintTooltip(Painter& p) {
    const Rect r = tipRect();
    p.fill(r, colors::kWhite);
    p.frame(r, 2, colors::kBlack);
    // Each 12 px line of text centred in its 14 px row.
    int y = r.y + 2 + kTipPadY + (kTipLine - kTipPx) / 2;
    for (const std::string& l : tipLines()) {
        p.text(l, r.x + 2 + kTipPadX, y, kTipPx, colors::kBlack);
        y += kTipLine;
    }
}

// ----------------------------------------------------------------------------------------------- Painting

void Desktop::invalidate(const Rect& r) noexcept {
    Rect c = r.translated(shift_.x, shift_.y).intersect(bounds());
    if (c.empty()) return;
    // Merge with each area touched (shared edge included); the union can touch others: start again from the beginning.
    for (size_t i = 0; i < dirty_.size();) {
        const Rect& d = dirty_[i];
        if (c.x <= d.right() && d.x <= c.right() && c.y <= d.bottom() && d.y <= c.bottom()) {
            c = c.unite(d);
            dirty_[i] = dirty_.back();
            dirty_.pop_back();
            i = 0;
        } else {
            ++i;
        }
    }
    if (dirty_.size() == kMaxDirty) {
        for (const Rect& d : dirty_) c = c.unite(d);
        dirty_.clear();
    }
    dirty_.push_back(c);
}

// paint(), the children (clipped to childArea() when the widget clips them), then paintOver(). A widget outside the
// clip is not painted, but its children are still visited: they can overflow it.
void Desktop::paintTree(Painter& p, Widget& w) {
    if (!w.visible_) return;
    const double a = w.opacity();
    // The group blends the children that overflow the widget too (CSS opacity, D-220).
    const Rect area = a < 1 ? w.extent().intersect(p.clip()) : Rect{};
    const bool group = !area.empty();
    if (group) p.beginOpacity(area, a);
    if (!w.bounds_.intersect(p.clip()).empty()) w.paint(p);
    const Rect clip = p.clip();
    if (w.clipsChildren()) p.setClip(clip.intersect(w.childArea()));
    if (!p.clip().empty())
        for (Widget* c : w.children_) paintTree(p, *c);
    p.setClip(clip);
    if (!w.bounds_.intersect(p.clip()).empty()) w.paintOver(p);
    if (group) p.endOpacity();
}

Rect Desktop::paint() {
    Rect all;
    // One Painter per area: each starts with its own clip and no open opacity group.
    for (const Rect& r : takePending()) {
        Painter p(surface_);
        draw(p, r);
        all = all.unite(r);
    }
    return all;
}

const std::vector<Rect>& Desktop::takePending() noexcept {
    painted_.clear();
    painted_.swap(dirty_);  // invalidations made during painting go to the next frame
    return painted_;
}

// Layers from the bottom: the root and its windows, the menus, then each modal box over its own veil, and the
// tooltip on top of everything.
void Desktop::draw(Painter& p, const Rect& r) {
    p.setClip(r);
    if (root_) paintTree(p, *root_);
    else p.fill(r, colors::kBlack);
    for (auto& w : popups_) paintTree(p, *w);
    for (auto& w : modals_) {
        p.shade(r, colors::kBlack, 0.25);  // veil rgba(0, 0, 0, 0.25) below each box
        paintTree(p, *w);
    }
    if (tipShown_) paintTooltip(p);
}

}  // namespace stym::gemui
