// STYM2149 — gemui: GEM desktop (layers, mouse, keyboard, tooltips, timers, area to repaint)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Three layers, from bottom to top:
// - the root (desktop background and windows), provided and owned by the caller; its children that are windows
//   (WindowFrame) are stacked: a press in one brings it in front of the others (the prototype's focusWin);
// - the drop-down menus, owned by the desktop, closed by a press outside them;
// - the modal boxes (alerts), owned by the desktop, each on a 25 % black veil that covers everything below it (the
//   prototype's #msgWin); only the last one receives mouse and keyboard.
// The desktop only knows logical pixels and a time in milliseconds given by the caller (tick): it does not depend on
// any window system.
// Cycle of the host (StymEditor, gemshot, the tests), all on one thread: it forwards the input events, calls tick()
// regularly (timers and tooltips), then either paint() into the framebuffer, or takePending() and draw() each area
// with its own Painter backend.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gemui/Surface.h"
#include "gemui/Widget.h"

namespace stym::gemui {

class Desktop {
public:
    Desktop(int width, int height);
    ~Desktop();
    Desktop(const Desktop&) = delete;
    Desktop& operator=(const Desktop&) = delete;

    int width() const noexcept { return surface_.width(); }
    int height() const noexcept { return surface_.height(); }
    Rect bounds() const noexcept { return surface_.bounds(); }
    const Surface& surface() const noexcept { return surface_; }

    // Root widget (not owned; nullptr: none): forgets hover, mouse capture and focus, and repaints everything.
    void setRoot(Widget* root);
    Widget* root() const noexcept { return root_; }

    // Top of the work area: the windows are not moved above it (the GEM desktop's menu bar is there, ninth DAW test,
    // item 101, D-302).
    void setWorkTop(int y) noexcept { workTop_ = y; }
    int workTop() const noexcept { return workTop_; }

    // Drop-down menus: the desktop takes the menu and shows it above those already open; closing destroys it.
    Widget& openPopup(std::unique_ptr<Widget> popup);
    void closePopup(Widget& popup);  // also closes those opened after it
    void closePopups();
    // An overlay is a popup opened over others that stay open (MenuList::openOver): a press on a popup below it closes
    // it and those opened after it.
    void markOverlay(Widget& popup);
    bool hasPopups() const noexcept { return !popups_.empty(); }
    Widget* topPopup() const noexcept { return popups_.empty() ? nullptr : popups_.back().get(); }
    // First open menu (below its submenus).
    Widget* firstPopup() const noexcept { return popups_.empty() ? nullptr : popups_.front().get(); }
    // Called when the menus close (menu bar title to put back in normal video).
    std::function<void()> onPopupsClosed;

    // Modal boxes: opening one closes the menus, ends a press in progress (the widget that had it gets mouseCancel)
    // and repaints the whole desktop (veil); closing destroys the box, whichever it is in the stack. With keepPopups,
    // the menus stay under the veil and come back once the box is closed.
    Widget& openModal(std::unique_ptr<Widget> modal, bool keepPopups = false);
    void closeModal(Widget& modal);
    bool hasModal() const noexcept { return !modals_.empty(); }
    Widget* topModal() const noexcept { return modals_.empty() ? nullptr : modals_.back().get(); }

    // Timers, run by tick(). after() calls fn once, at the first tick() at or after now() + ms; its id is never 0, so
    // 0 can mean "no timer". cancel() removes a pending timer (nothing for 0 or a timer already run) and sets id to 0.
    // tick() sets the time (milliseconds from any origin) and runs the due timers in the order they were created.
    using TimerId = uint32_t;
    TimerId after(double ms, std::function<void()> fn);
    void cancel(TimerId& id) noexcept;
    void tick(double nowMs);
    double now() const noexcept { return now_; }

    // Mouse (position in logical pixels). Moves with a button held go through mouseDrag.
    // A press goes to the widget under the pointer, which keeps the mouse (drags, release) until the release, wherever
    // the pointer goes.
    void mouseMove(Point p, Modifiers m);
    void mouseDown(Point p, MouseButton b, Modifiers m);
    void mouseDrag(Point p, Modifiers m);
    void mouseUp(Point p, Modifiers m);
    // Called on each press, before the widget hit receives it (and after the change of focus and of front window):
    // the widget hit, nullptr outside any widget.
    std::function<void(Widget*)> onPress;
    // Left press taken before everything else, with no menu or box open (pointerdown listener in the document's
    // capture phase, MIDI learn): true = the press stops there (no focus, no window brought forward, no widget hit).
    std::function<bool(Widget*)> interceptPress;
    // Double click, after the second release (the host decides on the delay and the distance: system settings).
    void mouseDoubleClick(Point p, Modifiers m);
    void mouseExit();
    // Wheel: to the widget under the pointer, then to its parents.
    void mouseWheel(Point p, double dx, double dy, Modifiers m);
    // Pointer to show: the bee when busy, otherwise that of the widget holding the mouse or under the pointer.
    Pointer pointer() const noexcept;
    Point mousePos() const noexcept { return mouse_; }
    // Long processing in progress (the prototype's body.busy): the bee replaces any other pointer.
    void setBusy(bool on) noexcept { busy_ = on; }
    bool busy() const noexcept { return busy_; }

    // Keyboard: modal box, otherwise focus (then its parents), otherwise root. true = key taken.
    bool keyDown(const KeyEvent& e);
    // Release: always to the root, modal box open or not (a note played on the keyboard must stop).
    void keyUp(const KeyEvent& e);
    // Keyboard focus (nullptr: none); the widget that loses it is told (focusLost). A press outside the focused
    // widget also takes the focus away.
    void setKeyFocus(Widget* w);
    Widget* keyFocus() const noexcept { return focus_; }

    // Tooltips (the prototype's Options > Tooltips menu).
    void setTooltips(bool on);
    bool tooltips() const noexcept { return tipsOn_; }
    // Translation of a tooltip when it is shown (language chosen by the host, D-185); without it, the widget's text as
    // it is.
    std::function<std::string(const std::string&)> translateTip;
    static constexpr double kTipDelayMs = 700;

    // Painting: the invalidated rectangles stay separate, disjoint areas (two areas that touch or overlap are merged),
    // at most kMaxDirty; beyond that, they are reduced to their union. Distant animated windows (oscilloscopes, VU
    // meters, playing step) thus do not make the whole desktop repaint.
    static constexpr size_t kMaxDirty = 16;
    void invalidate(const Rect& r) noexcept;
    void invalidateAll() noexcept { invalidate(bounds()); }
    // Offset of the invalidated rectangles: widgets laid out at another place than the one they are moved to (the
    // moved main window, laid out at its home place: D-224).
    void setInvalidateShift(Point d) noexcept { shift_ = d; }
    bool needsPaint() const noexcept { return !dirty_.empty(); }
    // Repaints the pending areas and returns their union (empty if there is nothing to do); the repainted areas are in
    // painted() until the next call.
    Rect paint();
    const std::vector<Rect>& painted() const noexcept { return painted_; }
    // Drawing with another backend (the plugin's juce::Graphics, D-201): takes the pending areas without painting the
    // framebuffer (they are in painted() until the next call); the caller then draws each one with draw().
    const std::vector<Rect>& takePending() noexcept;
    // Draws area r of the desktop with p, whose drawing area becomes r.
    void draw(Painter& p, const Rect& r);

    // Called by ~Widget, Widget::remove and Widget::setVisible(false): forgets a destroyed, detached or hidden widget
    // and its descendants (hover, mouse, focus).
    void forget(const Widget* w) noexcept;
    // Called by Widget::remove and Widget::setVisible(false) before forget, on a widget still alive: a press it holds
    // ends (mouseCancel), an input it has the focus for is cancelled (focusDropped).
    void release(const Widget* w);

private:
    struct Timer {
        TimerId id;
        double due;
        std::function<void()> fn;
    };

    void paintTree(Painter& p, Widget& w);
    void paintTooltip(Painter& p);
    // Deepest widget of w's tree at p that takes the mouse (front first), nullptr if none.
    Widget* find(Widget& w, Point p) const;
    // Widget that receives the mouse at p, layer by layer: the top modal box alone, else the menus, else the root.
    Widget* target(Point p) const;
    // Widget whose tooltip shows at p (a disabled widget still shows its own).
    Widget* tipTarget(Widget& w, Point p) const;
    bool insidePopups(Point p) const;
    // Sends mouseExit / mouseEnter when the widget under the pointer (or holding the mouse) changes.
    void updateHover(Point p);
    void hideTip();
    // After a move: starts the tooltip delay for what is under the pointer.
    void armTip();
    std::vector<std::string> tipLines() const;
    Rect tipRect() const;
    // Makes w a root of this desktop (its desktop() is this one) and repaints it.
    void adopt(Widget& w);

    Surface surface_;
    Widget* root_ = nullptr;
    int workTop_ = 0;
    std::vector<std::unique_ptr<Widget>> popups_, modals_;
    std::vector<const Widget*> overlays_;
    std::vector<Timer> timers_;
    TimerId nextTimer_ = 1;
    double now_ = 0;

    Point mouse_{-1, -1};  // last pointer position; (-1, -1) once it has left the desktop
    bool busy_ = false;
    Widget* hover_ = nullptr;    // widget under the pointer (the one holding the mouse during a press)
    Widget* capture_ = nullptr;  // widget that took the press: drags and release go to it
    Widget* lastPressed_ = nullptr;  // widget of the last released press (double click)
    Widget* focus_ = nullptr;

    bool tipsOn_ = true;
    TimerId tipTimer_ = 0;
    bool tipShown_ = false;
    std::string tipKey_, tipText_;  // text of the widget, text shown
    Point tipAt_;                   // pointer position when the tooltip appeared: it stays there

    std::vector<Rect> dirty_, painted_;  // kMaxDirty capacity reserved: invalidate does not allocate
    Point shift_{};
};

}  // namespace stym::gemui
