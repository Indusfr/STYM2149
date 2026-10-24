// STYM2149 — gemui: widget tree
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A widget has a rectangle in desktop coordinates (logical pixels), children it does not own (the code that creates
// them keeps them, usually as members), a visible / enabled state and a tooltip. The desktop (Desktop) paints the
// tree, dispatches the mouse and the keyboard and keeps the area to repaint.
// gemui is single-threaded: widgets and their desktop are created, changed and painted on one thread (the plugin's
// message thread); nothing here locks. Changing a widget only marks areas to repaint, painting comes later
// (Desktop::paint or takePending).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gemui/Geometry.h"
#include "gemui/Painter.h"

namespace stym::gemui {

class Desktop;

struct Modifiers {
    bool shift = false;
    bool ctrl = false;  // Ctrl on Windows and Linux, Cmd on macOS (the prototype's ^X shortcuts)
    bool alt = false;
};

enum class MouseButton : uint8_t { Left, Right, Middle };

// Position in desktop coordinates (logical pixels). Only mouseDown carries the real button: the desktop reports
// moves, drags, releases and double clicks as Left.
struct MouseEvent {
    Point pos;
    MouseButton button = MouseButton::Left;
    Modifiers mods;
};

// Wheel: wanted scroll in pixels (dy > 0: towards the bottom of the content, dx > 0: towards its right).
struct WheelEvent {
    Point pos;
    double dx = 0, dy = 0;
    Modifiers mods;
};

// Key of a KeyEvent: Character for a key that produces a character (in KeyEvent::ch), otherwise the named key.
enum class Key : uint8_t {
    None, Character, Enter, Escape, Backspace, Delete, Tab, Left, Right, Up, Down, Home, End, PageUp, PageDown, Insert
};

struct KeyEvent {
    // Digits of the numeric keypad (code): kNumpad0 + digit, distinct from the digits of the top row.
    static constexpr int kNumpad0 = 0x110000;

    Key key = Key::None;
    char32_t ch = 0;  // Key::Character: character produced (keyboard layout applied)
    // Key when the host gives it, otherwise 0: the character it bears without Shift (letters in upper case, including
    // ù → Ù, ö → Ö...), or a digit of the numeric keypad (kNumpad0 + n).
    int code = 0;
    bool repeat = false;  // auto-repeat of a held key
    Modifiers mods;
};

// Wanted mouse pointer (the plugin translates it into a system cursor): the prototype's cursor:default, pointer,
// move, col-resize, crosshair, cell, not-allowed and context-menu, and its drawn cursors (pencil of the Draw tool,
// scissors of Alt on a note, GEM bee for long operations).
enum class Pointer : uint8_t {
    Arrow, Hand, Move, ResizeH, Crosshair, Cell, NotAllowed, ContextMenu, Pencil, Scissors, Busy
};

// Base of every element of the desktop. Not copyable: the tree holds plain pointers. Its destructor detaches it from
// its parent and its children and makes the desktop forget it, so a widget can be destroyed while in a tree.
class Widget {
public:
    Widget() = default;
    virtual ~Widget();
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    const Rect& bounds() const noexcept { return bounds_; }
    // Whole-pixel box (setBox with integer edges); repaints the old and the new place. The children do not move.
    void setBounds(const Rect& r);
    // Layout box in fractional pixels (edges rounded separately for bounds()): the text of a widget starts from its
    // exact position, as in the browser.
    const Box& box() const noexcept { return box_; }
    void setBox(const Box& b);
    // Moves the widget and its descendants.
    void moveBy(int dx, int dy);
    // Same, without marking anything to repaint (the caller repaints what changed).
    void translate(int dx, int dy) noexcept;

    bool visible() const noexcept { return visible_; }
    void setVisible(bool v);
    // Enabled: the widget and all its parents (a disabled widget receives neither mouse nor keyboard).
    bool enabled() const noexcept;
    // The widget's own flag, whatever its parents.
    bool enabledFlag() const noexcept { return enabled_; }
    void setEnabled(bool e);
    // Visible itself and all its parents.
    bool showing() const noexcept;
    // Inert (the prototype's pointer-events:none): the widget and its descendants receive neither mouse nor tooltip,
    // without changing their look.
    bool inert() const noexcept { return inert_; }
    void setInert(bool i) noexcept { inert_ = i; }

    const std::string& tip() const noexcept { return tip_; }
    void setTip(std::string t) { tip_ = std::move(t); }

    // Identifier of the prototype's element (DOM id): controls that MIDI learn can learn.
    const std::string& id() const noexcept { return id_; }
    void setId(std::string id) { id_ = std::move(id); }

    // Adds a child in front of the others (behind those kept in front), taking it from its former parent; the child is
    // not owned. remove() detaches it without destroying it.
    void add(Widget& child);
    void remove(Widget& child);
    // Moves in front of its siblings (last child of its parent: painted last, hit first), but behind those kept in
    // front (the prototype's higher z-index, #learnBadge in front of the windows).
    void toFront();
    void setOnTop(bool top) noexcept { onTop_ = top; }
    Widget* parent() const noexcept { return parent_; }
    const std::vector<Widget*>& children() const noexcept { return children_; }
    // Box covered by the widget and its visible children, which can overflow it (a hint text past its frame, as in
    // CSS); its own box if it clips them. The area of its opacity group (D-220).
    Rect extent() const noexcept;

    // Desktop of the tree's root; nullptr while the widget is not in a desktop's tree.
    Desktop* desktop() const noexcept;
    // Marks the widget's bounds (or r, in desktop coordinates) to repaint; nothing outside a desktop.
    void repaint() noexcept;
    void repaint(const Rect& r) noexcept;

    // Painting: paint(), the children, then paintOver(). An opacity < 1 paints the whole as a CSS group.
    virtual void paint(Painter&) {}
    virtual void paintOver(Painter&) {}
    virtual double opacity() const noexcept { return 1; }

    // Mouse: only the widgets that accept it receive it (the deepest one under the pointer). After a press, the widget
    // keeps the mouse until the release.
    virtual bool acceptsMouse() const noexcept { return false; }
    virtual bool hit(Point p) const noexcept { return bounds_.contains(p); }
    // Nothing below receives the mouse or shows a tooltip (windows).
    virtual bool blocksMouse() const noexcept { return false; }
    // Children clipped to childArea() (painting, mouse and tooltips): visible area of a scrolling content.
    virtual bool clipsChildren() const noexcept { return false; }
    virtual Rect childArea() const noexcept { return bounds_; }
    virtual void mouseDown(const MouseEvent&) {}
    virtual void mouseDrag(const MouseEvent&) {}
    virtual void mouseUp(const MouseEvent&) {}
    // The press ends without a release (a modal box opened, the widget hidden or detached): the widget drops what the
    // press started (gesture, repeat timer, held note) without acting as a release would.
    virtual void mouseCancel() {}
    // Double click (the browser's dblclick: after the second release, to the widget that had received the press).
    virtual void mouseDoubleClick(const MouseEvent&) {}
    virtual void mouseMove(const MouseEvent&) {}
    virtual void mouseEnter() {}
    virtual void mouseExit() {}
    virtual Pointer pointer() const noexcept { return Pointer::Arrow; }
    // Pointer depending on the position (pointer() by default): close box and move area of a bar.
    virtual Pointer pointerAt(Point) const noexcept { return pointer(); }
    // Wheel: the widget under the pointer first, then its parents; true = scroll taken.
    virtual bool mouseWheel(const WheelEvent&) { return false; }
    // Tooltip under the pointer (tip() by default): a menu list gives one per entry.
    virtual std::string tipAt(Point) const { return tip_; }

    // Keyboard: the widget that has the focus first, then its parents; true = key taken.
    virtual bool keyDown(const KeyEvent&) { return false; }
    // Release of a key (same code as its press), at the root only: nothing is "taken" there.
    virtual void keyUp(const KeyEvent&) {}
    virtual void focusLost() {}
    // The focus taken away because the widget is hidden or detached, not by a press elsewhere: an input is cancelled,
    // not confirmed (its owner may be changing the window).
    virtual void focusDropped() {}

private:
    friend class Desktop;
    Rect bounds_;  // box_ rounded edge by edge
    Box box_;
    bool visible_ = true, enabled_ = true, inert_ = false, onTop_ = false;
    std::string tip_, id_;
    Widget* parent_ = nullptr;
    std::vector<Widget*> children_;
    Desktop* owner_ = nullptr;  // roots only
};

}  // namespace stym::gemui
