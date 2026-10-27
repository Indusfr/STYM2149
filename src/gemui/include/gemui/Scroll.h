// STYM2149 — gemui: scrolling area (the prototype's overflow:auto, GEM scrollbars of ::-webkit-scrollbar); scroll bar
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Content larger than the area scrolls. The scrollbars only appear if it overflows: vertical on the right,
// horizontal at the bottom, 26 px thick (like the title bar), halftoned corner when there are both. Drawing read in
// Chromium 141 (prototype at 100 %):
// - arrow at the top and at the bottom (on the left and on the right): white cell, 2 px rules except on the side of
//   the window frame, the prototype's 8 × 10 glyph centred in the cell; a single arrow at each end;
// - track: pattern of dots (1,1) and (3,3) on 4 × 4 from its corner, 2 px left (top) rule;
// - thumb: white cell, 2 px rules except on the side of the frame; length rounded in proportion to the visible part,
//   at least 15 px; position truncated (Chromium's ScrollbarTheme).
// Scrolling: 40 px per arrow, 87.5 % of the visible part per press on the track, repeated after 250 ms then every
// 50 ms as long as the button stays down; dragged thumb; wheel.
//
// The content is made of the area's children, placed from contentOrigin(); scrolling moves them all.
#pragma once
#include <functional>

#include "gemui/Controls.h"
#include "gemui/Desktop.h"
#include "gemui/Widget.h"

namespace stym::gemui {

// Scrolling area. The caller lays its children out from contentOrigin() and gives the content's size; scrolling then
// moves the children themselves (they stay in desktop coordinates), clipped to viewport(). The bars are painted over
// the content (paintOver).
class ScrollPane : public Widget {
public:
    static constexpr int kBar = 26;          // thickness of a scrollbar, length of an arrow
    static constexpr int kMinThumb = 15;     // shortest thumb (Chromium 141 on Linux, measured)
    static constexpr int kLineStep = 40;     // arrow (Chromium's kPixelsPerLineStep)
    static constexpr double kPageFraction = 0.875;  // track (Chromium's kMinFractionToStepWhenPaging)
    static constexpr double kRepeatDelayMs = 250, kRepeatMs = 50;

    ~ScrollPane() override;

    // Size of the content, inner padding included (scrollWidth / scrollHeight without scrollbars).
    void setContentSize(int w, int h);
    int contentWidth() const noexcept { return contentW_; }
    int contentHeight() const noexcept { return contentH_; }

    // Offset of the content (scrollLeft, scrollTop), limited to what overflows.
    Point scroll() const noexcept { return scroll_; }
    // Clamped to [0, maxScroll()]; moves the children and calls onScroll when the offset changes.
    void scrollTo(Point p);
    Point maxScroll() const noexcept;
    // Top-left corner of the content, offset included: where to place the children.
    Point contentOrigin() const noexcept { return {bounds().x - scroll_.x, bounds().y - scroll_.y}; }
    // Visible part of the content (without the scrollbars).
    Rect viewport() const noexcept;
    // Scrollbars present: the content overflows (overflow:auto; a scrollbar reduces the space of the other axis).
    bool hasVertical() const noexcept;
    bool hasHorizontal() const noexcept;
    // After a scroll (by the user or scrollTo).
    std::function<void()> onScroll;

    // Parts of the scrollbars (tests, layout). Up, Down, TrackV, ThumbV: vertical bar; Left, Right, TrackH, ThumbH:
    // horizontal bar; Corner: the square where both meet. A part absent from the pane has an empty rectangle.
    enum class Part { None, Up, Down, TrackV, ThumbV, Left, Right, TrackH, ThumbH, Corner };
    Rect partRect(Part p) const noexcept;
    Part partAt(Point p) const noexcept;

    bool acceptsMouse() const noexcept override { return true; }
    bool clipsChildren() const noexcept override { return true; }
    Rect childArea() const noexcept override { return viewport(); }
    void paintOver(Painter& p) override;  // scrollbars, over the content
    void mouseDown(const MouseEvent& e) override;
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseCancel() override { mouseUp({}); }  // the release only stops the repeat
    bool mouseWheel(const WheelEvent& e) override;

private:
    // One step of the pressed arrow or track (repeated by armRepeat while it stays pressed).
    void stepPress();
    void armRepeat(double ms);
    // Along the bar, in pixels: track between the two arrows, thumb, and thumb from the track's start.
    int trackLength(bool vertical) const noexcept;
    int thumbLength(bool vertical) const noexcept;
    int thumbOffset(bool vertical) const noexcept;
    void paintButton(Painter& p, Part part) const;
    void paintTrack(Painter& p, bool vertical) const;

    int contentW_ = 0, contentH_ = 0;
    Point scroll_;

    Part pressed_ = Part::None;
    Point pointer_;              // last point with the button down
    Point dragFrom_, dragScroll_;  // pointer and scroll offset when the thumb was pressed
    Desktop::TimerId repeat_ = 0;
};

// Scroll bar on its own (the notes of the Step Sequencer, eighth DAW test, item 93): a vertical slider drawn
// and driven like ScrollPane's vertical bar, its right side against a window frame. The maximum at the top: an arrow
// moves the value by one, a press on the track by `jump` towards the pointer (both repeated like ScrollPane's), the
// thumb, as long as `page` of the range plus the page, is dragged; the wheel moves by one per notch. Its gestures
// (onGestureStart / onGestureEnd) go from a press to its release.
class ScrollBar : public Slider {
public:
    ScrollBar();
    ~ScrollBar() override;

    // page: the part of the range shown at once, in value units (sets the thumb's length); jump: value step of a press
    // on the track.
    void setPage(double page, double jump);

    // Parts (tests, layout).
    enum class Part { None, Up, Down, Track, Thumb };
    Rect partRect(Part p) const noexcept;
    Part partAt(Point p) const noexcept;

    void paint(Painter& p) override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    bool mouseWheel(const WheelEvent& e) override;
    Pointer pointer() const noexcept override { return Pointer::Arrow; }

private:
    void move(double v);  // clamped and snapped; onChange if it changes
    void stepPress();
    void armRepeat(double ms);
    int trackLength() const noexcept;
    int thumbLength() const noexcept;
    int thumbOffset() const noexcept;

    double page_ = 1, jump_ = 1;
    Part pressed_ = Part::None;
    Point pointer_;
    int dragFrom_ = 0;       // pointer y when the thumb was pressed
    double dragValue_ = 0;   // value at that moment
    Desktop::TimerId repeat_ = 0;
};

}  // namespace stym::gemui
