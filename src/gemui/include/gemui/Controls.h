// STYM2149 — gemui: GEM controls (text, button, slider, editable value, frame, window)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Each control reproduces a CSS rule of the prototype (STYM2149_SPEC.md §6.4), quoted at the top of its class. The
// sizes are those of the prototype at 100 %; the layout (position of each control) is up to the caller, the natural
// sizes are given by naturalWidth() / naturalHeight().
// The callbacks (onClick, onChange...) run on the UI thread, during an event or a timer of the desktop, or from
// click() / input() that stand for the user's action; the setters never call them.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Color.h"
#include "gemui/Font.h"
#include "gemui/Widget.h"

namespace stym::gemui {

// Opacities of the prototype.
inline constexpr double kOpacityButtonOff = 0.35;  // .btn:disabled
inline constexpr double kOpacityTabOff = 0.3;      // .chtabs .btn.dis, .btn.dis
inline constexpr double kOpacitySliderOff = 0.3;   // input[type=range]:disabled, .val.dis
inline constexpr double kOpacityLabelOff = 0.35;   // label.dis, .hint.dis
inline constexpr double kOpacityInert = 0.4;       // .inert (content not applicable)

// Text on one line (label, .hint, .readout). The box is the line: the text starts from its top-left corner (left
// alignment), from its centre or from its right edge.
class Label : public Widget {
public:
    enum class Align : uint8_t { Left, Center, Right };

    Label() = default;
    explicit Label(std::string text, int px = 16) : text_(std::move(text)), px_(px) {}

    void setText(std::string t);
    const std::string& text() const noexcept { return text_; }
    void setPx(int px);
    int px() const noexcept { return px_; }
    // Font of a small text (Font.h): 6×6 where the 8×8 does not fit in the prototype's space (D-071).
    void setFace(Face f);
    void setColor(Color c);
    void setLetterSpacing(double s);
    void setAlign(Align a);
    // label.dis: grey #8A8A8A at 35 % opacity.
    void setDim(bool dim);
    bool dim() const noexcept { return dim_; }
    // Opacity of the whole label (multiplied by 0.35 when dim).
    void setOpacity(double a);
    // Solid background behind the line (window title, monitor headers); transparent by default.
    void setBackground(Color c);

    // Width of the text (letter spacing included), in layout pixels; height: the font size.
    double naturalWidth() const noexcept;
    int naturalHeight() const noexcept { return px_; }

    void paint(Painter& p) override;
    double opacity() const noexcept override;

private:
    std::string text_;
    int px_ = 16;
    Face face_ = Face::kSystem;
    Color color_ = colors::kBlack;
    Color background_ = colors::kTransparent;
    double spacing_ = 0;
    Align align_ = Align::Left;
    bool dim_ = false;
    double opacity_ = 1;
};

// .btn: white background, 2 px border, 14 px text, 3 px / 10 px padding; .on and :active in reverse video;
// .dflt (default button): 4 px border, 1 px / 8 px padding; track tab (.chtabs .btn): 2 px / 7 px padding,
// .sel: 4 px green bottom rule. An icon can follow the text (#shapeSel: 8 px gap).
class Button : public Widget {
public:
    enum class Style : uint8_t { Normal, Default, Tab };

    Button() = default;
    explicit Button(std::string text, Style s = Style::Normal) : text_(std::move(text)), style_(s) {}

    void setText(std::string t);
    const std::string& text() const noexcept { return text_; }
    void setStyle(Style s);
    Style style() const noexcept { return style_; }
    void setPx(int px);
    // Font of a small text (Font.h): 6×6 in the prototype's narrow spaces (D-071).
    void setFace(Face f);
    // Reverse video (.on): a toggle that is on, the current choice of a group.
    void setOn(bool on);
    bool on() const noexcept { return on_; }
    void setSelected(bool sel);  // tab: green rule
    bool selected() const noexcept { return sel_; }
    void setIcon(const Bitmap* icon);
    const Bitmap* icon() const noexcept { return icon_; }
    // Opacity when the button is disabled: 0.35 and the browser's grey text (<button disabled>) by default; another
    // opacity (the prototype's .dis class, 0.3 for the tabs) keeps the text black.
    void setDisabledOpacity(double a) {
        offOpacity_ = a;
        classOff_ = true;
    }
    // Green dotted border (.btn.pend): setting pending.
    void setPending(bool p);
    // Padding and minimum width (border included) of a prototype class: .tools .btn (5 px / 8 px, 30 px),
    // .tbar .btn (2 px / 6 px, 26 px), .tbar.big .btn (7 px / 14 px, 46 px), bank buttons (6 px / 11 px).
    void setPadding(int padY, int padX);
    void setMinWidth(double w);

    // Content, padding and borders (at least the minimum width); the height counts the 4 px green rule of a selected
    // tab instead of its bottom border.
    double naturalWidth() const noexcept;
    int naturalHeight() const noexcept;

    // Press and release over the button (the release outside it cancels the click).
    std::function<void()> onClick;
    std::function<void(Point)> onContext;  // right click
    // Click without the mouse (the prototype's el.click(), MIDI learn): onClick if the button is enabled.
    void click();

    void paint(Painter& p) override;
    double opacity() const noexcept override { return enabled() ? 1.0 : offOpacity_; }
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseCancel() override;
    Pointer pointer() const noexcept override { return Pointer::Hand; }

private:
    int border() const noexcept;
    int lineHeight() const noexcept;
    // How much taller the line box is than the plain GEM font's (a character outside the font, Font::lineBox).
    Font::LineBox fallbackGrowth() const noexcept;
    int padX() const noexcept;
    int padY() const noexcept;
    double contentWidth() const noexcept;

    std::string text_;
    Style style_ = Style::Normal;
    int px_ = 14;
    Face face_ = Face::kSystem;
    bool on_ = false, sel_ = false, pressed_ = false, pending_ = false;
    const Bitmap* icon_ = nullptr;
    double offOpacity_ = kOpacityButtonOff;
    bool classOff_ = false;  // setDisabledOpacity called: the disabled text stays black
    int padY_ = -1, padX_ = -1;  // < 0: those of the style
    double minWidth_ = 0;
};

// Arrow cell of the scroll bars (::-webkit-scrollbar-button, D-117): white, 2 px rules on the sides `rules` names,
// the prototype's 8 × 10 glyph centred inside them, pointing the way `arrow` names. ScrollPane, ScrollBar (D-294),
// horizontal Slider (D-297).
enum class Arrow : uint8_t { Up, Down, Left, Right };
struct ArrowRules {
    bool top = true, left = true, bottom = true, right = true;
};
void paintArrowCell(Painter& p, const Rect& cell, Arrow arrow, ArrowRules rules = {});

// input[type=range]: 16 px track, 4×4 checkerboard (1 px dots at (1,1) and (3,3)) under a 2 px inner rule; 16 px
// square white thumb with a 2 px border. A press on the track brings the thumb's centre there, a press on the thumb
// grabs it where it is. Vertical: the prototype's track rotated by -90° (.trwrap), minimum at the bottom,
// checkerboard rotated with it.
// Horizontal: an arrow cell at each end (D-297), drawn like the scroll bars' (16 px, sharing its inner rule with the
// track): a press moves the value by one step, again after 250 ms and every 50 ms while held, in one gesture.
class Slider : public Widget {
public:
    static constexpr int kThumb = 16;
    static constexpr int kArrow = 16, kArrowInset = 14;  // arrow cell; track from its inner rule
    static constexpr double kRepeatDelayMs = 250, kRepeatMs = 50;

    ~Slider() override;

    void setVertical(bool v);
    bool vertical() const noexcept { return vertical_; }
    void setRange(double min, double max, double step);
    double min() const noexcept { return min_; }
    double max() const noexcept { return max_; }
    void setValue(double v);  // no callback; clamped and snapped like input()
    double value() const noexcept { return value_; }
    // Value from elsewhere (the prototype's el.value then input event, MIDI learn): clamped and snapped to the step,
    // set, then onChange, without a gesture.
    void input(double v);

    // Arrows at the ends of a horizontal slider: on by default; off for the Mixer's pans (D-297).
    void setArrows(bool on);
    bool arrows() const noexcept { return arrows_ && !vertical_; }
    // Arrow cell (tests, layout): left (false) or right (true); empty without arrows.
    Rect arrowRect(bool right) const noexcept;

    // onChange: each new value during a drag or an arrow press. The gesture brackets a whole press, from the press to
    // the release, arrow repeats included (one undo step in the editor, D-297).
    std::function<void(double)> onChange;
    std::function<void()> onGestureStart, onGestureEnd;
    std::function<void(Point)> onContext;  // right click

    void paint(Painter& p) override;
    double opacity() const noexcept override { return enabled() ? 1.0 : kOpacitySliderOff; }
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const MouseEvent& e) override;
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseCancel() override;
    Pointer pointer() const noexcept override { return Pointer::Hand; }

    // Thumb (tests, layout).
    Rect thumbRect() const noexcept;

protected:
    // v clamped to [min, max] and rounded to min + a whole number of steps, never above max (input[type=range]).
    double clampStep(double v) const noexcept;

private:
    int thumbPos() const noexcept;      // left edge (top when vertical) of the thumb
    void setFromPos(int pos);           // pointer x (y when vertical)
    double trackStart() const noexcept; // x (y when vertical) and length of the thumb's run
    double trackLength() const noexcept;
    void arrowStep();                   // one step towards the pressed arrow, while the pointer is on it
    void armRepeat(double ms);

    double min_ = 0, max_ = 100, step_ = 1, value_ = 0;
    bool vertical_ = false;
    bool arrows_ = true;
    bool dragging_ = false;
    int grab_ = kThumb / 2;  // offset of the pointer in the thumb during a drag (its centre after a press on the track)
    int arrowDir_ = 0;  // pressed arrow: -1 left, +1 right
    Point pointer_;  // last pointer position during an arrow press (no step while it is off the arrow)
    uint32_t repeat_ = 0;  // Desktop::TimerId
};

// First number of a typed value (sign, digits, a point or a comma before the decimals), as a ValueField starts its
// input; nothing when the text holds none.
std::optional<double> typedNumber(std::string_view text);

// strtod and printf follow the C locale of the process, which a host may have set to one with a decimal comma: strtod
// then stops at a '.', and a value shown with a ',' no longer reads back. These two take '.' or ',' as the decimal
// point and write '.', whatever the locale.
// Number at the start of `text` (strtod syntax); `used`: characters read, 0 when there is no number.
double readDecimal(std::string_view text, size_t* used = nullptr);
// One double formatted by `format` (printf syntax).
std::string formatDecimal(const char* format, double v);

// .val.editable: right-aligned value, free positions marked with underscores (one per character, 7 px of line and
// 1 px of gap, 2 px high, 1 px above the bottom); hover in reverse video; a click opens the input: the mask ("999.9",
// "9.99") shows its free positions as "_" and places the digits on either side of the point (the prototype's
// maskEdit); without a mask, the whole value is typed again. Enter confirms, Escape cancels, a click elsewhere
// confirms.
class ValueField : public Widget {
public:
    ValueField() = default;

    void setText(std::string t);
    const std::string& text() const noexcept { return text_; }
    void setPx(int px);
    // Minimum width in characters (min-width in ch) and left padding in characters (#susv).
    void setMinChars(int n);
    void setPadChars(int n);
    // Input mask: '9' marks a digit position, '.' the decimal point; the other characters are shown as they are, a
    // unit after the last digit ("999.99 Hz") staying in place while the value is typed.
    // A different mask while the value is being typed cancels the input (laid out for the old mask's positions).
    void setMask(std::string mask);
    // Without a mask: the input starts from the whole text (a note name such as C#4), not from its first number.
    void setWholeText(bool w) { wholeText_ = w; }
    // Without a mask: unit that ends the text (" Hz", " ct") and is not typed; like a mask's, the pointer leaves it in
    // normal video (Indus, 26/10/2026 15:25, D-451; every unit of the interface, 26/10/2026 20:41-20:44, D-464).
    void setUnit(std::string u);
    const std::string& unit() const noexcept { return unit_; }
    // Font of a small value (Font.h): the Mixer's, in the 6×6 font (D-300).
    void setFace(Face f);
    // Value centred in the field, without the dashes (the Mixer's, under their slider, D-415): reverse video over
    // the field on hover, the input centred too.
    void setCentered(bool c);
    void setEditable(bool e);
    // .val.dis: 30 % opacity, no input.
    void setDim(bool d);
    bool editing() const noexcept { return editing_; }
    // What the field shows: while it is typed into, the input (with a mask, the mask with the digits typed and "_"
    // at the free positions); otherwise its text.
    std::string shownText() const;

    double naturalWidth() const noexcept;

    // Typed text (read through the mask); false = rejected, the old value comes back.
    std::function<bool(const std::string&)> onCommit;

    void paint(Painter& p) override;
    double opacity() const noexcept override { return dim_ ? kOpacitySliderOff : 1.0; }
    bool acceptsMouse() const noexcept override { return editable_ && !dim_; }
    void mouseEnter() override;
    void mouseExit() override;
    void mouseDown(const MouseEvent& e) override;
    bool keyDown(const KeyEvent& e) override;
    void focusLost() override;
    void focusDropped() override;
    Pointer pointer() const noexcept override { return Pointer::Hand; }
    std::string tipAt(Point) const override;

    // Opens the input (as a click does) and takes the keyboard focus; endEdit closes it, and with apply passes the
    // typed text, trimmed, to onCommit.
    void beginEdit();
    void endEdit(bool apply);

private:
    double charWidth() const noexcept;
    // Mask with the typed digits in place and "_" at the free positions.
    std::string maskDisplay() const;
    // Typed value read through the mask: integer digits ("0" when none), then the decimals if any were typed.
    std::string maskRead() const;
    // Character position of the text cursor, from the input's left edge.
    int caretChar() const;

    std::string text_;
    int px_ = 16;
    int minChars_ = 0, padChars_ = 0;
    std::string mask_, unit_;
    bool wholeText_ = false;
    Face face_ = Face::kSystem;
    bool centered_ = false;
    bool editable_ = true, dim_ = false, hover_ = false;

    // Input in progress.
    bool editing_ = false;
    std::string ent_, dec_;  // mask: digits on either side of the point
    bool afterDot_ = false;
    std::string free_;       // no mask: retyped text
    bool selected_ = false;  // no mask: the old value is selected
    int cols_ = 0;           // width of the input in characters (the mask's length, or the field's width)
};

// The prototype's <input type="text"> of the boxes (Pan, Gate, Track length): inherited font, right-aligned text, no
// border; background of the input[type=text] rule: underscores one character wide (minus 1 px) repeated from the
// left, 1 px from the bottom, the first ones covered over the length of the text (--typed). The field takes the
// focus on click; it then shows a fixed text cursor after the last character. Typing at the end of the text only:
// printable characters and backspace.
class TextField : public Widget {
public:
    void setText(std::string t);  // no callback
    const std::string& text() const noexcept { return text_; }
    void setPx(int px);
    int px() const noexcept { return px_; }
    // Height of the line (the font's line-height: normal).
    double naturalHeight() const noexcept;

    std::function<void()> onChange;  // oninput
    std::function<void()> onEnter;   // Enter

    void paint(Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const MouseEvent& e) override;
    bool keyDown(const KeyEvent& e) override;
    void focusLost() override;
    void focusDropped() override { focusLost(); }  // the text cursor goes with the focus
    Pointer pointer() const noexcept override { return Pointer::Arrow; }

private:
    std::string text_;
    int px_ = 16;
    bool focused_ = false;
};

// fieldset + legend: 2 px border, legend 12 px from the edge (border and padding of the frame; 10 px for
// #adsrCurveFs), padding 0 6 px, 1 px letter spacing, followed by a 12 px note (.hint, #222). The top rule runs
// through the middle of the legend (7 px below the top of the box).
class GroupBox : public Widget {
public:
    static constexpr int kLegendHeight = 16;  // the legend's line (16 px text)

    explicit GroupBox(std::string legend = {}, std::string hint = {}) : legend_(std::move(legend)), hint_(std::move(hint)) {}

    void setLegend(std::string l);
    void setHint(std::string h);
    void setOpacity(double a);
    // Gap between the frame's edge and the legend's box (border + left padding of the fieldset).
    void setLegendInset(int px);
    // Width of the legend's box: legend and note with their 1 px letter spacing, and 6 px of padding on each side.
    double legendWidth() const noexcept;
    // Positions of the legend and of the note (tooltips, layout).
    Box legendBox() const noexcept;

    void paint(Painter& p) override;
    double opacity() const noexcept override { return opacity_; }

private:
    std::string legend_, hint_;
    double opacity_ = 1;
    int inset_ = 12;
};

// .gemwin / .gwin / .alert: 2 px black border, 2 px white rule then 2 px black rule inside, white background.
// A window stops the mouse: what it covers receives neither mouse nor tooltip.
class WindowFrame : public Widget {
public:
    static constexpr int kInset = 6;  // border + rules: the content starts 6 px from the edge
    void paint(Painter& p) override;
    bool blocksMouse() const noexcept override { return true; }
};

// .titlebar: 26 px (2 px bottom rule included); 24 px close box on the left and full-screen box on the right (GEM
// glyphs E005 and E007 of the 8×8 font, square, at twice their size: D-418, D-424), hatched central area (2×2
// squares on a 4×4 grid), title centred on a white background (12 px padding). Pointers of the prototype: hand on the
// close box, move on a movable bar.
class TitleBar : public Widget {
public:
    static constexpr int kHeight = 26;
    static constexpr int kBox = 24;

    void setTitle(std::string t);
    const std::string& title() const noexcept { return title_; }
    void setBoxes(bool close, bool full);

    std::function<void()> onClose, onFull;
    // Movable bar: a press outside the boxes grabs the window (onGrab), then each move gives the pointer's position
    // (onDrag).
    void setMovable(bool m) noexcept { movable_ = m; }
    std::function<void(Point)> onGrab, onDrag;

    void paint(Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const MouseEvent& e) override;
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseCancel() override { pressed_ = 0; }  // a move in progress stops where it is
    Pointer pointerAt(Point p) const noexcept override;
    std::string tipAt(Point p) const override;
    void setCloseTip(std::string t) { closeTip_ = std::move(t); }
    void setFullTip(std::string t) { fullTip_ = std::move(t); }

private:
    Rect closeRect() const noexcept;
    Rect fullRect() const noexcept;

    std::string title_;
    bool close_ = true, full_ = true, movable_ = false;
    int pressed_ = 0;  // 1: close, 2: full screen, 3: move (the boxes act on a release over the box pressed)
    std::string closeTip_, fullTip_;
};

// The prototype's .gwin: floating window, WindowFrame's frame and a title bar with a close box only, moved by its bar
// (left and top edges never negative, mkWinDrag; the top edge never above Desktop::workTop either, D-302). It never
// overflows the right and bottom edges of the desktop either (D-460): moved, placed or opened, it stays inside.
// Hidden when created; open() shows it in front of the other windows, where it was left.
class FloatWindow : public WindowFrame {
public:
    FloatWindow();

    TitleBar& titleBar() noexcept { return bar_; }
    void setTitle(std::string t) { bar_.setTitle(std::move(t)); }
    // Position and size, frame included; the title bar follows. Moved, with its content, to stay on the desktop.
    void place(const Box& b);
    // Moves the window, with its content, so that it is on the desktop (D-460): right and bottom edges inside it; the
    // left edge never negative and the top never above the work area win for a window larger than the desktop.
    void keepOnDesktop();
    // Top-left corner of the content: inside the frame, below the title bar.
    Point contentOrigin() const noexcept {
        return {bounds().x + kInset, bounds().y + kInset + TitleBar::kHeight};
    }

    void open();
    void close();  // hides the window then calls onClose
    // Closed by the user (its close box, Escape): close(), unless the window asks first (Settings, D-437).
    virtual void requestClose() { close(); }
    bool isOpen() const noexcept { return visible(); }
    std::function<void()> onClose;
    // Escape closes the frontmost open window that accepts it (the prototype's CLOSERS: neither the curve nor the
    // computer keyboard).
    bool closesOnEscape = true;

    // Frontmost open window among the children of `parent` (only those that Escape closes if `escapeOnly`), nullptr
    // if none.
    static FloatWindow* topmost(const Widget& parent, bool escapeOnly = false) noexcept;

private:
    Point onDesktop(int x, int y) const noexcept;  // top-left corner (x, y) brought inside the desktop

    TitleBar bar_;
    Point grab_;  // pointer offset in the window when its bar was grabbed
};

// GEM desktop icon (ICONBLK of the AES): a 32 × 32 picture, its mask in white then its data in black, a character
// in it (the drive letter), above its name in the 6×6 font on a white box 72 px wide. Decorative (ninth DAW test,
// item 102): a click or a double click does nothing; a press held and moved drags its dotted outline, which the
// root paints above the windows (paintDrag), and the release puts the icon there, inside limits().
class DeskIcon : public Widget {
public:
    static constexpr int kW = 72, kPicture = 32, kNameH = 8, kH = kPicture + kNameH;

    void setPicture(const Bitmap* data, const Bitmap* mask) noexcept;
    // Character drawn in the picture (0: none) in the AES's SMALL font (6×6), as gr_gicon draws ib_char, its cell's
    // top-left corner at (x, y) of the picture (D-419).
    void setLetter(char c, int x, int y) noexcept;
    void setName(std::string name);
    const std::string& name() const noexcept { return name_; }
    // Area the icon stays in (the desktop below the menu bar).
    void setLimits(const Rect& r) noexcept { limits_ = r; }
    // Invisible grid of cells of the icon's size, from `origin`, as on a GEM desktop: the icon lands in the nearest cell
    // inside limits(), and its outline moves from cell to cell.
    void setGrid(Point origin) noexcept {
        grid_ = origin;
        gridOn_ = true;
    }
    // Top-left corner of the cell nearest to `topLeft` (inside limits()); `topLeft` itself, kept inside limits(),
    // without a grid.
    Point snapped(Point topLeft) const noexcept;
    // Top-left corner after a drag (the icon is already there).
    std::function<void(Point)> onMoved;
    // A drop place may be refused (another icon's cell): the icon stays where it was.
    std::function<bool(Point)> canLand;
    // A press released without a move.
    std::function<void()> onClick;

    bool dragging() const noexcept { return drag_; }
    // Outline being dragged: where the icon would land.
    Rect dragRect() const noexcept;
    void paintDrag(Painter& p) const;

    void paint(Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    bool hit(Point p) const noexcept override;
    void mouseDown(const MouseEvent& e) override;
    void mouseDrag(const MouseEvent& e) override;
    void mouseUp(const MouseEvent& e) override;
    void mouseCancel() override;

private:
    // Top-left corner where the icon lands for a pointer position: same grab offset, kept inside limits().
    Point landing(Point pointer) const noexcept;
    void repaintDrag() noexcept;

    const Bitmap* data_ = nullptr;
    const Bitmap* mask_ = nullptr;
    char letter_ = 0;
    Point letterAt_;
    std::string name_;
    Rect limits_;
    Point grab_, at_;  // pointer in the icon at the press; landing place of the drag
    Point grid_, pressAt_;
    bool gridOn_ = false;
    bool pressed_ = false, drag_ = false, wandered_ = false;
};

}  // namespace stym::gemui
