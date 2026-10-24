// STYM2149 — gemui: scrolling area
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Scroll.h"

#include <algorithm>
#include <cmath>

#include "gemui/Controls.h"
#include "gemui/Painter.h"

namespace stym::gemui {

namespace {

constexpr int kRule = 2;  // rules of the parts

bool vertical(ScrollPane::Part p) noexcept {
    using P = ScrollPane::Part;
    return p == P::Up || p == P::Down || p == P::TrackV || p == P::ThumbV;
}

}  // namespace

ScrollPane::~ScrollPane() {
    if (Desktop* d = desktop()) d->cancel(repeat_);
}

bool ScrollPane::hasVertical() const noexcept {
    const Rect r = bounds();
    if (contentH_ > r.h) return true;
    return contentW_ > r.w && contentH_ > r.h - kBar;  // the horizontal scrollbar takes 26 px of height
}

bool ScrollPane::hasHorizontal() const noexcept { return contentW_ > bounds().w - (hasVertical() ? kBar : 0); }

Rect ScrollPane::viewport() const noexcept {
    const Rect r = bounds();
    return {r.x, r.y, std::max(0, r.w - (hasVertical() ? kBar : 0)), std::max(0, r.h - (hasHorizontal() ? kBar : 0))};
}

Point ScrollPane::maxScroll() const noexcept {
    const Rect v = viewport();
    return {std::max(0, contentW_ - v.w), std::max(0, contentH_ - v.h)};
}

void ScrollPane::setContentSize(int w, int h) {
    if (w == contentW_ && h == contentH_) return;
    contentW_ = std::max(0, w);
    contentH_ = std::max(0, h);
    repaint();
    scrollTo(scroll_);
}

// The children are laid out in desktop coordinates: a change of offset moves them all by the opposite amount.
void ScrollPane::scrollTo(Point p) {
    const Point m = maxScroll();
    const Point s{std::clamp(p.x, 0, m.x), std::clamp(p.y, 0, m.y)};
    if (s == scroll_) return;
    const int dx = scroll_.x - s.x, dy = scroll_.y - s.y;
    scroll_ = s;
    for (Widget* c : children()) c->moveBy(dx, dy);
    repaint();
    if (onScroll) onScroll();
}

// ------------------------------------------------------------------------------------------------- Geometry

int ScrollPane::trackLength(bool v) const noexcept {
    const Rect vp = viewport();
    return std::max(0, (v ? vp.h : vp.w) - 2 * kBar);
}

// Chromium's rule: the visible fraction of the content times the track, rounded, at least kMinThumb, at most the
// track.
int ScrollPane::thumbLength(bool v) const noexcept {
    const int total = v ? contentH_ : contentW_, track = trackLength(v);
    if (total <= 0) return 0;
    const Rect vp = viewport();
    const double proportion = double(v ? vp.h : vp.w) / double(total);
    const int len = std::max(int(std::lround(proportion * track)), kMinThumb);
    return std::min(len, track);
}

int ScrollPane::thumbOffset(bool v) const noexcept {
    const Point m = maxScroll();
    const int size = v ? m.y : m.x;
    if (size <= 0) return 0;
    const double pos = double(v ? scroll_.y : scroll_.x) * double(trackLength(v) - thumbLength(v)) / double(size);
    return pos > 0 && pos < 1 ? 1 : int(pos);  // Chromium's ThumbPosition: truncated, at least 1 px
}

// The vertical bar runs down the right edge and the horizontal one along the bottom, each as long as the viewport:
// with both, the down and right arrows stop at the corner square.
Rect ScrollPane::partRect(Part p) const noexcept {
    const Rect r = bounds(), vp = viewport();
    const bool v = hasVertical(), h = hasHorizontal();
    const int bx = r.right() - kBar, by = r.bottom() - kBar;
    switch (p) {
    case Part::Up: return v ? Rect{bx, r.y, kBar, kBar} : Rect{};
    case Part::Down: return v ? Rect{bx, vp.bottom() - kBar, kBar, kBar} : Rect{};
    case Part::TrackV: return v ? Rect{bx, r.y + kBar, kBar, trackLength(true)} : Rect{};
    case Part::ThumbV: return v ? Rect{bx, r.y + kBar + thumbOffset(true), kBar, thumbLength(true)} : Rect{};
    case Part::Left: return h ? Rect{r.x, by, kBar, kBar} : Rect{};
    case Part::Right: return h ? Rect{vp.right() - kBar, by, kBar, kBar} : Rect{};
    case Part::TrackH: return h ? Rect{r.x + kBar, by, trackLength(false), kBar} : Rect{};
    case Part::ThumbH: return h ? Rect{r.x + kBar + thumbOffset(false), by, thumbLength(false), kBar} : Rect{};
    case Part::Corner: return v && h ? Rect{bx, by, kBar, kBar} : Rect{};
    case Part::None: break;
    }
    return {};
}

ScrollPane::Part ScrollPane::partAt(Point pt) const noexcept {
    // The thumb first: it lies on the track.
    for (Part p : {Part::ThumbV, Part::ThumbH, Part::Up, Part::Down, Part::TrackV, Part::Left, Part::Right,
                   Part::TrackH, Part::Corner})
        if (partRect(p).contains(pt)) return p;
    return Part::None;
}

// -------------------------------------------------------------------------------------------------- Painting

void ScrollPane::paintButton(Painter& p, Part part) const {
    // Rules: all except the one on the side of the frame (right of a vertical scrollbar, bottom of a horizontal one).
    // The prototype's horizontal buttons point inwards (read in Chromium).
    const bool v = vertical(part);
    const Arrow a = part == Part::Up     ? Arrow::Up
                    : part == Part::Down ? Arrow::Down
                    : part == Part::Left ? Arrow::Right
                                         : Arrow::Left;
    paintArrowCell(p, partRect(part), a, {true, true, v, !v});
}

void ScrollPane::paintTrack(Painter& p, bool v) const {
    const Rect t = partRect(v ? Part::TrackV : Part::TrackH);
    if (t.w <= 0 || t.h <= 0) return;
    p.tile(t, tiles::sliderDots(), {t.x, t.y});
    if (v) p.fill({t.x, t.y, kRule, t.h}, colors::kBlack);
    else p.fill({t.x, t.y, t.w, kRule}, colors::kBlack);
    const Rect th = partRect(v ? Part::ThumbV : Part::ThumbH);
    if (th.empty()) return;
    p.fill(th, colors::kWhite);
    p.fill({th.x, th.y, th.w, kRule}, colors::kBlack);
    p.fill({th.x, th.y, kRule, th.h}, colors::kBlack);
    if (v) p.fill({th.x, th.bottom() - kRule, th.w, kRule}, colors::kBlack);
    else p.fill({th.right() - kRule, th.y, kRule, th.h}, colors::kBlack);
}

void ScrollPane::paintOver(Painter& p) {
    if (hasVertical()) {
        paintButton(p, Part::Up);
        paintTrack(p, true);
        paintButton(p, Part::Down);
    }
    if (hasHorizontal()) {
        paintButton(p, Part::Left);
        paintTrack(p, false);
        paintButton(p, Part::Right);
    }
    const Rect c = partRect(Part::Corner);
    if (!c.empty()) {
        p.tile(c, tiles::sliderDots(), {c.x, c.y});
        p.fill({c.x, c.y, c.w, kRule}, colors::kBlack);
        p.fill({c.x, c.y, kRule, c.h}, colors::kBlack);
    }
}

// ----------------------------------------------------------------------------------------------------- Mouse

// A step happens only while the pointer is over the pressed part. Since partAt() finds the thumb before the track,
// the track's steps stop once the thumb has come under the pointer.
void ScrollPane::stepPress() {
    const Rect vp = viewport();
    const int pageV = std::max(1, int(vp.h * kPageFraction)), pageH = std::max(1, int(vp.w * kPageFraction));
    const Part under = partAt(pointer_);
    Point s = scroll_;
    switch (pressed_) {
    case Part::Up: if (under == Part::Up) s.y -= kLineStep; break;
    case Part::Down: if (under == Part::Down) s.y += kLineStep; break;
    case Part::Left: if (under == Part::Left) s.x -= kLineStep; break;
    case Part::Right: if (under == Part::Right) s.x += kLineStep; break;
    case Part::TrackV:
        // Towards the pointer, until the thumb reaches it.
        if (under == Part::TrackV) s.y += pointer_.y < partRect(Part::ThumbV).y ? -pageV : pageV;
        break;
    case Part::TrackH:
        if (under == Part::TrackH) s.x += pointer_.x < partRect(Part::ThumbH).x ? -pageH : pageH;
        break;
    default: return;
    }
    scrollTo(s);
}

void ScrollPane::armRepeat(double ms) {
    Desktop* d = desktop();
    if (!d) return;
    d->cancel(repeat_);
    repeat_ = d->after(ms, [this] {
        repeat_ = 0;
        if (pressed_ == Part::None) return;
        stepPress();
        armRepeat(kRepeatMs);
    });
}

void ScrollPane::mouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return;
    pressed_ = partAt(e.pos);
    pointer_ = e.pos;
    switch (pressed_) {
    case Part::ThumbV:
    case Part::ThumbH:
        dragFrom_ = e.pos;
        dragScroll_ = scroll_;
        break;
    case Part::Up:
    case Part::Down:
    case Part::Left:
    case Part::Right:
    case Part::TrackV:
    case Part::TrackH:
        stepPress();
        armRepeat(kRepeatDelayMs);
        break;
    default: pressed_ = Part::None; break;
    }
}

// Thumb drag: the pointer's travel since the press, scaled from the thumb's room on the track to the scroll range.
void ScrollPane::mouseDrag(const MouseEvent& e) {
    pointer_ = e.pos;
    if (pressed_ != Part::ThumbV && pressed_ != Part::ThumbH) return;
    const bool v = pressed_ == Part::ThumbV;
    const int room = trackLength(v) - thumbLength(v);
    if (room <= 0) return;
    const Point m = maxScroll();
    const double moved = v ? e.pos.y - dragFrom_.y : e.pos.x - dragFrom_.x;
    const int to = int(std::lround(moved * double(v ? m.y : m.x) / double(room)));
    scrollTo(v ? Point{scroll_.x, dragScroll_.y + to} : Point{dragScroll_.x + to, scroll_.y});
}

void ScrollPane::mouseUp(const MouseEvent&) {
    pressed_ = Part::None;
    if (Desktop* d = desktop()) d->cancel(repeat_);
}

// dx, dy are already in pixels (the editor converts the host's wheel units, D-118). Not taken when the pane has no
// bar in that direction, so that the wheel goes on to the parents.
bool ScrollPane::mouseWheel(const WheelEvent& e) {
    double dx = e.dx, dy = e.dy;
    if (e.mods.shift && dx == 0) std::swap(dx, dy);  // Shift + wheel: horizontal scroll
    const bool v = hasVertical() && dy != 0, h = hasHorizontal() && dx != 0;
    if (!v && !h) return false;
    scrollTo({scroll_.x + (h ? int(std::lround(dx)) : 0), scroll_.y + (v ? int(std::lround(dy)) : 0)});
    return true;
}

// ------------------------------------------------------------------------------------------------ ScrollBar

// A vertical Slider underneath (range, step, value, onChange and the gestures), so that MIDI learn keeps working on
// it (D-294); only the drawing and the mouse are ScrollPane's.
ScrollBar::ScrollBar() { setVertical(true); }

ScrollBar::~ScrollBar() {
    if (Desktop* d = desktop()) d->cancel(repeat_);
}

void ScrollBar::setPage(double page, double jump) {
    page_ = std::max(0.0, page);
    jump_ = std::max(0.0, jump);
    repaint();
}

int ScrollBar::trackLength() const noexcept { return std::max(0, bounds().h - 2 * ScrollPane::kBar); }

int ScrollBar::thumbLength() const noexcept {
    // ScrollPane's: in proportion to the visible part (the page) of the whole (the range plus the page).
    const double total = max() - min() + page_, track = trackLength();
    if (total <= 0) return int(track);
    const int len = std::max(int(std::lround(page_ / total * track)), ScrollPane::kMinThumb);
    return std::min(len, int(track));
}

int ScrollBar::thumbOffset() const noexcept {
    // The maximum at the top; truncated, at least 1 px off the end (Chromium's ThumbPosition).
    const double range = max() - min();
    if (range <= 0) return 0;
    const double pos = (max() - value()) * double(trackLength() - thumbLength()) / range;
    return pos > 0 && pos < 1 ? 1 : int(pos);
}

Rect ScrollBar::partRect(Part part) const noexcept {
    const Rect r = bounds();
    constexpr int kBar = ScrollPane::kBar;
    switch (part) {
    case Part::Up: return {r.x, r.y, r.w, kBar};
    case Part::Down: return {r.x, r.bottom() - kBar, r.w, kBar};
    case Part::Track: return {r.x, r.y + kBar, r.w, trackLength()};
    case Part::Thumb: return {r.x, r.y + kBar + thumbOffset(), r.w, thumbLength()};
    case Part::None: break;
    }
    return {};
}

ScrollBar::Part ScrollBar::partAt(Point pt) const noexcept {
    for (Part p : {Part::Thumb, Part::Up, Part::Down, Part::Track})
        if (partRect(p).contains(pt)) return p;
    return Part::None;
}

void ScrollBar::paint(Painter& p) {
    // ScrollPane's vertical bar: its right side against a window frame, without a rule.
    paintArrowCell(p, partRect(Part::Up), Arrow::Up, {true, true, true, false});
    const Rect t = partRect(Part::Track);
    if (t.w > 0 && t.h > 0) {
        p.tile(t, tiles::sliderDots(), {t.x, t.y});
        p.fill({t.x, t.y, kRule, t.h}, colors::kBlack);
        const Rect th = partRect(Part::Thumb);
        if (!th.empty()) {
            p.fill(th, colors::kWhite);
            p.fill({th.x, th.y, th.w, kRule}, colors::kBlack);
            p.fill({th.x, th.y, kRule, th.h}, colors::kBlack);
            p.fill({th.x, th.bottom() - kRule, th.w, kRule}, colors::kBlack);
        }
    }
    paintArrowCell(p, partRect(Part::Down), Arrow::Down, {true, true, true, false});
}

void ScrollBar::move(double v) {
    // input() clamps and snaps the same way, then calls onChange.
    if (clampStep(v) != value()) input(v);
}

void ScrollBar::stepPress() {
    const Part under = partAt(pointer_);
    if (under != pressed_) return;  // the pointer left the part: the repeat waits for it
    switch (pressed_) {
    case Part::Up: move(value() + 1); break;  // the maximum is at the top
    case Part::Down: move(value() - 1); break;
    case Part::Track:
        // Towards the pointer, until the thumb reaches it.
        move(value() + (pointer_.y < partRect(Part::Thumb).y ? jump_ : -jump_));
        break;
    default: break;
    }
}

void ScrollBar::armRepeat(double ms) {
    Desktop* d = desktop();
    if (!d) return;
    d->cancel(repeat_);
    repeat_ = d->after(ms, [this] {
        repeat_ = 0;
        if (pressed_ == Part::None) return;
        stepPress();
        armRepeat(ScrollPane::kRepeatMs);
    });
}

void ScrollBar::mouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return;
    pressed_ = partAt(e.pos);
    pointer_ = e.pos;
    if (pressed_ == Part::None) return;
    if (onGestureStart) onGestureStart();
    if (pressed_ == Part::Thumb) {
        dragFrom_ = e.pos.y;
        dragValue_ = value();
        return;
    }
    stepPress();
    armRepeat(ScrollPane::kRepeatDelayMs);
}

void ScrollBar::mouseDrag(const MouseEvent& e) {
    pointer_ = e.pos;
    if (pressed_ != Part::Thumb) return;
    // The pointer's travel scaled from the thumb's room to the range; downwards lowers the value.
    const int room = trackLength() - thumbLength();
    if (room <= 0) return;
    move(dragValue_ - (e.pos.y - dragFrom_) * (max() - min()) / room);
}

void ScrollBar::mouseUp(const MouseEvent&) {
    if (pressed_ == Part::None) return;
    pressed_ = Part::None;
    if (Desktop* d = desktop()) d->cancel(repeat_);
    if (onGestureEnd) onGestureEnd();
}

bool ScrollBar::mouseWheel(const WheelEvent& e) {
    if (!enabled() || e.dy == 0) return false;
    // One per wheel event, whatever its size; towards the bottom (dy > 0) lowers the value.
    move(value() + (e.dy > 0 ? -1 : 1));
    return true;
}

}  // namespace stym::gemui
