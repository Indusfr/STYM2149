// STYM2149 — gemui: widget tree
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Widget.h"

#include <algorithm>

#include "gemui/Desktop.h"

namespace stym::gemui {

// The children are owned elsewhere: they stay alive, without a parent, until their owner destroys them.
Widget::~Widget() {
    if (Desktop* d = desktop()) {
        d->invalidate(bounds_);
        d->forget(this);
    }
    for (Widget* c : children_) c->parent_ = nullptr;
    if (parent_) {
        auto& v = parent_->children_;
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
    }
}

void Widget::setBounds(const Rect& r) {
    setBox({double(r.x), double(r.y), double(r.w), double(r.h)});
}

void Widget::setBox(const Box& b) {
    if (b.x == box_.x && b.y == box_.y && b.w == box_.w && b.h == box_.h) return;
    repaint();
    box_ = b;
    bounds_ = b.snapped();
    repaint();
}

void Widget::moveBy(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    repaint();
    bounds_ = bounds_.translated(dx, dy);
    box_.x += dx;
    box_.y += dy;
    repaint();
    for (Widget* c : children_) c->moveBy(dx, dy);
}

// Used to lay a moved window out at its home place, then carry it back (MainWindow::layout, D-224).
void Widget::translate(int dx, int dy) noexcept {
    bounds_ = bounds_.translated(dx, dy);
    box_.x += dx;
    box_.y += dy;
    for (Widget* c : children_) c->translate(dx, dy);
}

void Widget::setVisible(bool v) {
    if (v == visible_) return;
    visible_ = v;
    if (Desktop* d = desktop()) {
        d->invalidate(bounds_);
        // A hidden widget keeps neither the hover, the press nor the focus (nor do its descendants: forget goes up).
        if (!v) {
            d->release(this);
            d->forget(this);
        }
    }
    for (Widget* c : children_) c->repaint();
}

bool Widget::enabled() const noexcept {
    for (const Widget* w = this; w; w = w->parent_)
        if (!w->enabled_) return false;
    return true;
}

void Widget::setEnabled(bool e) {
    if (e == enabled_) return;
    enabled_ = e;
    repaint();
    for (Widget* c : children_) c->repaint();
}

bool Widget::showing() const noexcept {
    for (const Widget* w = this; w; w = w->parent_)
        if (!w->visible_) return false;
    return true;
}

void Widget::add(Widget& child) {
    if (child.parent_ == this) return;
    if (child.parent_) child.parent_->remove(child);
    child.parent_ = this;
    // Behind the children kept in front (setOnTop), like toFront.
    const auto at = child.onTop_ ? children_.end()
                                 : std::find_if(children_.begin(), children_.end(), [](const Widget* w) { return w->onTop_; });
    children_.insert(at, &child);
    child.repaint();
}

void Widget::remove(Widget& child) {
    if (child.parent_ != this) return;
    child.repaint();
    if (Desktop* d = desktop()) {
        d->release(&child);
        d->forget(&child);
    }
    children_.erase(std::remove(children_.begin(), children_.end(), &child), children_.end());
    child.parent_ = nullptr;
}

// Same place rule as add(); repaints only when the widget actually changed place among its siblings.
void Widget::toFront() {
    if (!parent_) return;
    auto& v = parent_->children_;
    const auto was = std::find(v.begin(), v.end(), this) - v.begin();
    v.erase(v.begin() + was);
    const auto at = onTop_ ? v.end() : std::find_if(v.begin(), v.end(), [](const Widget* w) { return w->onTop_; });
    const auto now = at - v.begin();
    v.insert(at, this);
    if (now != was) repaint();
}

// Only a root knows its desktop (owner_): walk up to it.
Desktop* Widget::desktop() const noexcept {
    const Widget* w = this;
    while (w->parent_) w = w->parent_;
    return w->owner_;
}

void Widget::repaint() noexcept { repaint(bounds_); }

Rect Widget::extent() const noexcept {
    Rect r = bounds_;
    if (clipsChildren()) return r;
    for (const Widget* c : children_)
        if (c->visible_) r = r.unite(c->extent());
    return r;
}

void Widget::repaint(const Rect& r) noexcept {
    if (Desktop* d = desktop()) d->invalidate(r);
}

}  // namespace stym::gemui
