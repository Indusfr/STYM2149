// STYM2149 — gemui: GEM controls
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Controls.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <clocale>
#include <cstdio>
#include <cstdlib>

#include "gemui/Desktop.h"
#include "gemui/Font.h"

namespace stym::gemui {
namespace {

// Indices in the glyph tables (FontData.h: U+E001 is index 95).
constexpr int kGlyphClose = 99;  // E005: close box
constexpr int kGlyphFull = 101;  // E007: full-screen box

bool isDigit(char32_t c) noexcept { return c >= '0' && c <= '9'; }

// A glyph of the 8×8 font at twice its size, each pixel a 2×2 square, its ink centred in a: the close and
// full-screen boxes of a title bar, twice as big (Indus, 24/10/2026 20:40, D-418) and square like those of the ST's
// desktop in low resolution, where the AES draws them from the 8×8 font (25/10/2026 16:28, D-424); the 8×16 glyphs
// are 7 × 11.
void doubledGlyph(Painter& p, int index, const Rect& a, Color c) {
    const Font::Raster g = Font::raster(8, index, 0, Face::kSystem);
    int r0 = g.count, r1 = -1;
    uint16_t cols = 0;
    for (int r = 0; r < g.count; ++r)
        if (const uint16_t bits = g.bits(r)) {
            r0 = std::min(r0, r);
            r1 = r;
            cols |= bits;
        }
    if (r1 < 0) return;
    const int c0 = std::countl_zero(cols), c1 = 15 - std::countr_zero(cols);
    const int x = a.x + (a.w - 2 * (c1 - c0 + 1)) / 2, y = a.y + (a.h - 2 * (r1 - r0 + 1)) / 2;
    for (int r = r0; r <= r1; ++r)
        for (int k = c0; k <= c1; ++k)
            if (g.bits(r) & (0x8000 >> k)) p.fill({x + 2 * (k - c0), y + 2 * (r - r0), 2, 2}, c);
}

// First number of a text (the prototype's `-?[0-9]+([.,][0-9]+)?`), comma included.
std::string firstNumber(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        size_t j = i;
        if (s[j] == '-') ++j;
        if (j >= s.size() || !isDigit(char32_t(s[j]))) continue;
        while (j < s.size() && isDigit(char32_t(s[j]))) ++j;
        if (j + 1 < s.size() && (s[j] == '.' || s[j] == ',') && isDigit(char32_t(s[j + 1]))) {
            ++j;
            while (j < s.size() && isDigit(char32_t(s[j]))) ++j;
        }
        return s.substr(i, j - i);
    }
    return {};
}

}  // namespace

std::optional<double> typedNumber(std::string_view text) {
    const std::string n = firstNumber(std::string(text));
    if (n.empty()) return std::nullopt;
    const double v = readDecimal(n);
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

namespace {

char localePoint() {
    const std::lconv* lc = std::localeconv();
    return lc && lc->decimal_point && lc->decimal_point[0] ? lc->decimal_point[0] : '.';
}

}  // namespace

double readDecimal(std::string_view text, size_t* used) {
    std::string t(text);
    const char point = localePoint();
    for (char& c : t)
        if (c == '.' || c == ',') c = point;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (used) *used = size_t(end - t.c_str());
    return v;
}

std::string formatDecimal(const char* format, double v) {
    char b[64];
    std::snprintf(b, sizeof b, format, v);
    std::string out = b;
    const char point = localePoint();
    if (point != '.') std::replace(out.begin(), out.end(), point, '.');
    return out;
}

// ------------------------------------------------------------------------------------------------- Label

void Label::setText(std::string t) {
    if (t == text_) return;
    text_ = std::move(t);
    repaint();
}

void Label::setPx(int px) {
    if (px == px_) return;
    px_ = px;
    repaint();
}

void Label::setFace(Face f) {
    if (f == face_) return;
    face_ = f;
    repaint();
}

void Label::setColor(Color c) {
    if (c == color_) return;
    color_ = c;
    repaint();
}

void Label::setLetterSpacing(double s) {
    spacing_ = s;
    repaint();
}

void Label::setAlign(Align a) {
    align_ = a;
    repaint();
}

void Label::setDim(bool dim) {
    if (dim == dim_) return;
    dim_ = dim;
    repaint();
}

void Label::setOpacity(double a) {
    if (a == opacity_) return;
    opacity_ = a;
    repaint();
}

void Label::setBackground(Color c) {
    background_ = c;
    repaint();
}

double Label::naturalWidth() const noexcept { return Font::width(text_, px_, spacing_, face_); }

double Label::opacity() const noexcept { return dim_ ? opacity_ * kOpacityLabelOff : opacity_; }

// The text starts from the box's fractional position (as in the browser) and is centred vertically on a line of px.
void Label::paint(Painter& p) {
    if (opaque(background_)) p.fill(bounds(), background_);
    const Box& b = box();
    double x = b.x;
    if (align_ == Align::Center) x = b.x + (b.w - naturalWidth()) / 2;
    else if (align_ == Align::Right) x = b.x + b.w - naturalWidth();
    const double top = b.y + (b.h - px_) / 2;
    p.text(text_, x, top, px_, dim_ ? colors::kGreyDisabled : color_, spacing_, face_);
}

// ------------------------------------------------------------------------------------------------ Button

void Button::setText(std::string t) {
    if (t == text_) return;
    text_ = std::move(t);
    repaint();
}

void Button::setStyle(Style s) {
    style_ = s;
    repaint();
}

void Button::setPx(int px) {
    px_ = px;
    repaint();
}

void Button::setFace(Face f) {
    if (f == face_) return;
    face_ = f;
    repaint();
}

void Button::setOn(bool on) {
    if (on == on_) return;
    on_ = on;
    repaint();
}

void Button::setSelected(bool sel) {
    if (sel == sel_) return;
    sel_ = sel;
    repaint();
}

void Button::setIcon(const Bitmap* icon) {
    icon_ = icon;
    repaint();
}

void Button::setPending(bool p) {
    if (p == pending_) return;
    pending_ = p;
    repaint();
}

void Button::setPadding(int padY, int padX) {
    padY_ = padY;
    padX_ = padX;
    repaint();
}

void Button::setMinWidth(double w) {
    minWidth_ = w;
    repaint();
}

// Border and padding of the classes quoted at the top of the class (.btn, .dflt, .chtabs .btn), unless setPadding
// set others.
int Button::border() const noexcept { return style_ == Style::Default ? 4 : 2; }
int Button::padX() const noexcept {
    if (padX_ >= 0) return padX_;
    return style_ == Style::Default ? 8 : style_ == Style::Tab ? 7 : 10;
}
int Button::padY() const noexcept {
    if (padY_ >= 0) return padY_;
    return style_ == Style::Default ? 1 : style_ == Style::Tab ? 2 : 3;
}

// Text, then the icon after an 8 px gap (no gap without text).
double Button::contentWidth() const noexcept {
    double w = Font::width(text_, px_, 0, face_);
    if (icon_) w += (text_.empty() ? 0 : 8) + icon_->w;
    return w;
}

double Button::naturalWidth() const noexcept { return std::max(minWidth_, contentWidth() + 2 * (border() + padX())); }

// Height of the line: that of the text, or of the icon if it is taller; a button without text only has its icon (the
// prototype's svg in display:block: 11 px in .tbar). A glyph outside the font ("♪" of a Song block) enlarges the
// line to the measurements of the fallback font, as in the browser (26 px instead of 24 for a 14 px button).
int Button::lineHeight() const noexcept {
    if (text_.empty() && icon_) return icon_->h;
    return std::max(px_ + fallbackGrowth().height(), icon_ ? icon_->h : 0);
}

Font::LineBox Button::fallbackGrowth() const noexcept {
    const Font::LineBox lb = Font::lineBox(text_, px_), nb = Font::lineBox({}, px_);
    return {lb.ascent - nb.ascent, lb.descent - nb.descent};
}

int Button::naturalHeight() const noexcept {
    const int line = lineHeight();
    const int bottom = style_ == Style::Tab && sel_ ? 4 : border();
    return line + 2 * padY() + border() + bottom;
}

void Button::paint(Painter& p) {
    const Rect r = bounds();
    // Reverse video while on, or while pressed (until the release, wherever the pointer is).
    const bool inverse = on_ || pressed_;
    const Color ink = inverse ? colors::kWhite : !enabled() && !classOff_ ? colors::kButtonTextOff : colors::kBlack;
    p.fill(r, inverse ? colors::kBlack : colors::kWhite);
    const int bw = border();
    p.frame(r, bw, colors::kBlack);
    int bottom = bw;
    if (style_ == Style::Tab && sel_) {
        bottom = 4;
        p.fill({r.x, r.bottom() - 4, r.w, 4}, colors::kGreen);
    }
    if (pending_) {
        // outline: 2px dashed green, offset -2 px (on the border): 6 px dashes.
        // Every 12 px from the top-left corner along each side: a 6 px dash, then a 6 px gap.
        for (int x = r.x; x < r.right(); x += 12) {
            p.fill({x, r.y, std::min(6, r.right() - x), 2}, colors::kGreen);
            p.fill({x, r.bottom() - 2, std::min(6, r.right() - x), 2}, colors::kGreen);
        }
        for (int y = r.y; y < r.bottom(); y += 12) {
            p.fill({r.x, y, 2, std::min(6, r.bottom() - y)}, colors::kGreen);
            p.fill({r.right() - 2, y, 2, std::min(6, r.bottom() - y)}, colors::kGreen);
        }
    }
    // Content centred in the inner box (like a <button> larger than its text).
    const Box& b = box();
    const int line = lineHeight();
    const double inner = b.h - bw - bottom - 2 * padY();
    const double lineTop = b.y + bw + padY() + (inner - line) / 2;
    const double x = b.x + (b.w - contentWidth()) / 2;
    // Fallback glyph: the line grows to its measurements and the baseline moves down with its ascent (Painter::text
    // places it below the whole line box).
    p.text(text_, x, lineTop + (line - px_ - fallbackGrowth().height()) / 2.0, px_, ink, 0, face_);
    if (icon_) {
        const double ix = x + Font::width(text_, px_, 0, face_) + (text_.empty() ? 0 : 8);
        p.bitmap(*icon_, snap(ix), snap(lineTop + (line - icon_->h) / 2.0), ink);
    }
}

void Button::mouseDown(const MouseEvent& e) {
    if (e.button == MouseButton::Right) {
        if (onContext) onContext(e.pos);
        return;
    }
    if (e.button != MouseButton::Left) return;
    pressed_ = true;
    repaint();
}

void Button::click() {
    if (!enabled() || !onClick) return;
    const std::function<void()> f = onClick;  // the action may destroy the button
    f();
}

void Button::mouseUp(const MouseEvent& e) {
    if (!pressed_) return;
    pressed_ = false;
    repaint();
    // Copy: the action may destroy the button (button of an alert box).
    if (bounds().contains(e.pos) && onClick) {
        const std::function<void()> f = onClick;
        f();
    }
}

void Button::mouseCancel() {
    if (!pressed_) return;
    pressed_ = false;
    repaint();
}

// ------------------------------------------------------------------------------------------------- Arrows

namespace {

// Glyphs of the prototype's ::-webkit-scrollbar-button (8 × 10 SVG, one rectangle per segment), by the way they
// point: those of its horizontal buttons point inwards (decrement: right, increment: left).
// Ten rows of one byte, bit 7 = column 0 (Bitmap); indexed by Arrow.
constexpr uint8_t kArrowGlyphs[4][10] = {
    {0x18, 0x3C, 0x66, 0xC3, 0x81, 0xE7, 0x24, 0x24, 0x24, 0x3C},  // up
    {0x3C, 0x24, 0x24, 0x24, 0xE7, 0x81, 0xC3, 0x66, 0x3C, 0x18},  // down
    {0x0C, 0x1C, 0x34, 0x67, 0xC1, 0xC1, 0x67, 0x34, 0x1C, 0x0C},  // left
    {0x30, 0x38, 0x2C, 0xE6, 0x83, 0x83, 0xE6, 0x2C, 0x38, 0x30},  // right
};
constexpr int kArrowRule = 2;

}  // namespace

void paintArrowCell(Painter& p, const Rect& r, Arrow arrow, ArrowRules rules) {
    if (r.empty()) return;
    p.fill(r, colors::kWhite);
    if (rules.top) p.fill({r.x, r.y, r.w, kArrowRule}, colors::kBlack);
    if (rules.left) p.fill({r.x, r.y, kArrowRule, r.h}, colors::kBlack);
    if (rules.bottom) p.fill({r.x, r.bottom() - kArrowRule, r.w, kArrowRule}, colors::kBlack);
    if (rules.right) p.fill({r.right() - kArrowRule, r.y, kArrowRule, r.h}, colors::kBlack);
    // Centred inside the rules (24 × 22 in a vertical scroll bar's cell, 22 × 24 in a horizontal one's).
    const int x0 = r.x + (rules.left ? kArrowRule : 0), x1 = r.right() - (rules.right ? kArrowRule : 0);
    const int y0 = r.y + (rules.top ? kArrowRule : 0), y1 = r.bottom() - (rules.bottom ? kArrowRule : 0);
    p.bitmap({8, 10, kArrowGlyphs[int(arrow)]}, x0 + (x1 - x0 - 8) / 2, y0 + (y1 - y0 - 10) / 2, colors::kBlack);
}

// ------------------------------------------------------------------------------------------------ Slider

Slider::~Slider() {
    if (Desktop* d = desktop()) d->cancel(repeat_);
}

void Slider::setArrows(bool on) {
    arrows_ = on;
    repaint();
}

Rect Slider::arrowRect(bool right) const noexcept {
    if (!arrows()) return {};
    const Rect r = bounds();
    return {right ? r.right() - kArrow : r.x, r.y, kArrow, r.h};
}

// With arrows, the thumb runs between the arrow cells' inner rules, kArrowInset (14 px) from each end: the 16 px cell
// and the track share that 2 px rule.
double Slider::trackStart() const noexcept {
    if (vertical_) return box().y;
    return box().x + (arrows() ? kArrowInset : 0);
}

double Slider::trackLength() const noexcept {
    if (vertical_) return box().h;
    return box().w - (arrows() ? 2 * kArrowInset : 0);
}

void Slider::setRange(double min, double max, double step) {
    min_ = min;
    max_ = std::max(min, max);
    step_ = step;
    value_ = clampStep(value_);
    repaint();
}

double Slider::clampStep(double v) const noexcept {
    // The negated test also sends NaN to the minimum. Rounding to a step can land past max: one step back then.
    if (!(v >= min_)) v = min_;
    if (v > max_) v = max_;
    if (step_ > 0) {
        v = min_ + std::round((v - min_) / step_) * step_;
        if (v > max_) v -= step_;
    }
    return v;
}

void Slider::setValue(double v) {
    v = clampStep(v);
    if (v == value_) return;
    value_ = v;
    repaint();
}

void Slider::input(double v) {
    setValue(v);
    if (onChange) onChange(value_);
}

void Slider::setVertical(bool v) {
    vertical_ = v;
    repaint();
}

// The thumb's run is the track minus the thumb; a vertical slider has its minimum at the bottom.
int Slider::thumbPos() const noexcept {
    const double ratio = max_ > min_ ? (value_ - min_) / (max_ - min_) : 0;
    return snap(trackStart() + (trackLength() - kThumb) * (vertical_ ? 1 - ratio : ratio));
}

Rect Slider::thumbRect() const noexcept {
    const Rect r = bounds();
    return vertical_ ? Rect{r.x, thumbPos(), kThumb, kThumb} : Rect{thumbPos(), r.y, kThumb, kThumb};
}

void Slider::paint(Painter& p) {
    const Rect r = bounds();
    p.fill(r, colors::kWhite);
    // Vertical: the track of length L rotated by -90° around its centre; pixel u of its length falls on row L - 1 - u,
    // hence the dots at (1, L - 2) and (3, L) modulo 4.
    const int shift = vertical_ ? ((r.h - 3) % 4 + 4) % 4 : 0;
    p.tile(r, tiles::sliderDots(), {r.x, r.y + shift});
    p.frame(r, 2, colors::kBlack);
    if (arrows()) {
        paintArrowCell(p, arrowRect(false), Arrow::Left);
        paintArrowCell(p, arrowRect(true), Arrow::Right);
    }
    const Rect t = thumbRect();
    p.fill(t, colors::kWhite);
    p.frame(t, 2, colors::kBlack);
}

// Inverse of thumbPos: the pointer minus the grab offset gives the thumb's edge, hence the value.
void Slider::setFromPos(int pos) {
    const double span = trackLength() - kThumb;
    double ratio = span > 0 ? (pos - grab_ - trackStart()) / span : 0;
    ratio = std::clamp(ratio, 0.0, 1.0);
    if (vertical_) ratio = 1 - ratio;
    const double v = clampStep(min_ + ratio * (max_ - min_));
    if (v == value_) return;
    value_ = v;
    repaint();
    if (onChange) onChange(v);
}

void Slider::arrowStep() {
    // Only while the pointer stays on the pressed arrow, as the scroll bars' buttons.
    if (arrowDir_ == 0 || !arrowRect(arrowDir_ > 0).contains(pointer_)) return;
    // The slider's step, or a hundredth of its range when it has none (D-297).
    const double step = step_ > 0 ? step_ : (max_ - min_) / 100;
    const double v = clampStep(value_ + arrowDir_ * step);
    if (v == value_) return;
    value_ = v;
    repaint();
    if (onChange) onChange(v);
}

// Arrow held: a step after kRepeatDelayMs, then every kRepeatMs, until mouseUp cancels the timer.
void Slider::armRepeat(double ms) {
    Desktop* d = desktop();
    if (!d) return;
    d->cancel(repeat_);
    repeat_ = d->after(ms, [this] {
        repeat_ = 0;
        if (arrowDir_ == 0) return;
        arrowStep();
        armRepeat(kRepeatMs);
    });
}

void Slider::mouseDown(const MouseEvent& e) {
    if (e.button == MouseButton::Right) {
        if (onContext) onContext(e.pos);
        return;
    }
    if (e.button != MouseButton::Left) return;
    if (onGestureStart) onGestureStart();
    if (arrows()) {
        // The cell up to the track's frame, which shares its inner rule.
        const Rect r = bounds();
        arrowDir_ = e.pos.x < r.x + kArrowInset ? -1 : e.pos.x >= r.right() - kArrowInset ? 1 : 0;
        if (arrowDir_ != 0) {
            pointer_ = e.pos;
            arrowStep();
            armRepeat(kRepeatDelayMs);
            return;
        }
    }
    // A press on the thumb keeps the point where it was grabbed; a press on the track centres the thumb on the
    // pointer at once.
    dragging_ = true;
    const int tp = thumbPos(), pos = vertical_ ? e.pos.y : e.pos.x;
    if (pos >= tp && pos < tp + kThumb) {
        grab_ = pos - tp;
    } else {
        grab_ = kThumb / 2;
        setFromPos(pos);
    }
}

void Slider::mouseDrag(const MouseEvent& e) {
    pointer_ = e.pos;
    if (dragging_) setFromPos(vertical_ ? e.pos.y : e.pos.x);
}

void Slider::mouseUp(const MouseEvent&) {
    if (arrowDir_ != 0) {
        arrowDir_ = 0;
        if (Desktop* d = desktop()) d->cancel(repeat_);
        if (onGestureEnd) onGestureEnd();
        return;
    }
    if (!dragging_) return;
    dragging_ = false;
    if (onGestureEnd) onGestureEnd();
}

// The release ignores where it happens (ScrollBar's too): it ends the gesture and stops the repeat of an arrow.
void Slider::mouseCancel() { mouseUp({}); }

// -------------------------------------------------------------------------------------------- ValueField

void ValueField::setText(std::string t) {
    if (t == text_) return;
    text_ = std::move(t);
    repaint();
}

void ValueField::setMask(std::string mask) {
    if (mask == mask_) return;
    // The typed digits fill the old mask's positions: with fewer positions, maskDisplay would place them outside it.
    endEdit(false);
    mask_ = std::move(mask);
    repaint();
}

void ValueField::setPx(int px) {
    px_ = px;
    repaint();
}

void ValueField::setMinChars(int n) {
    minChars_ = n;
    repaint();
}

void ValueField::setPadChars(int n) {
    padChars_ = n;
    repaint();
}

void ValueField::setFace(Face f) {
    if (f == face_) return;
    face_ = f;
    repaint();
}

void ValueField::setCentered(bool c) {
    if (c == centered_) return;
    centered_ = c;
    repaint();
}

void ValueField::setEditable(bool e) {
    if (e == editable_) return;
    if (!e && editing_) endEdit(false);
    editable_ = e;
    repaint();
}

void ValueField::setDim(bool d) {
    if (d == dim_) return;
    if (d && editing_) endEdit(false);
    dim_ = d;
    hover_ = hover_ && !d;
    repaint();
}

double ValueField::charWidth() const noexcept { return Font::advance(px_, face_); }

double ValueField::naturalWidth() const noexcept {
    return (std::max(minChars_, Font::length(text_)) + padChars_) * charWidth();
}

std::string ValueField::tipAt(Point) const {
    if (!tip().empty()) return tip();
    return editable_ && !dim_ ? "Click to type a value" : std::string();
}

void ValueField::mouseEnter() {
    hover_ = true;
    repaint();
}

void ValueField::mouseExit() {
    hover_ = false;
    repaint();
}

void ValueField::mouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left || editing_) return;
    beginEdit();
}

void ValueField::beginEdit() {
    if (!editable_ || dim_ || editing_) return;
    editing_ = true;
    const std::string init = wholeText_ && mask_.empty() ? text_ : firstNumber(text_);
    // With a mask: the current number split at its point (or comma) into integer and decimal digits, each part cut
    // to the mask's '9' positions on its side of the point (the last integer digits, the first decimals). Without a
    // mask: the old text (or its first number), selected, so that the first character typed replaces it.
    if (!mask_.empty()) {
        std::string e0 = init, d0;
        for (char& c : e0)
            if (c == ',') c = '.';
        const size_t dot = e0.find('.');
        if (dot != std::string::npos) {
            d0 = e0.substr(dot + 1);
            e0 = e0.substr(0, dot);
        }
        int left = 0, right = 0;
        const size_t mdot = mask_.find('.');
        for (size_t i = 0; i < mask_.size(); ++i)
            if (mask_[i] == '9') (mdot == std::string::npos || i < mdot ? left : right)++;
        ent_.clear();
        dec_.clear();
        for (char c : e0)
            if (isDigit(char32_t(c))) ent_ += c;
        for (char c : d0)
            if (isDigit(char32_t(c))) dec_ += c;
        if (int(ent_.size()) > left) ent_ = ent_.substr(ent_.size() - size_t(left));
        if (int(dec_.size()) > right) dec_.resize(size_t(right));
        afterDot_ = false;
        cols_ = int(mask_.size());
    } else {
        free_ = init;
        selected_ = !free_.empty();
        cols_ = std::max(1, int(std::lround(bounds().w / charWidth())));
    }
    hover_ = false;
    repaint();
    if (Desktop* d = desktop()) d->setKeyFocus(this);
}

// The integer digits fill the positions left of the point from the right, the decimals those right of it from the
// left: "999.9" with 12 and 5 shows "_12.5".
std::string ValueField::maskDisplay() const {
    std::string buf = mask_;
    std::vector<size_t> left, right;
    const size_t dot = mask_.find('.');
    for (size_t i = 0; i < mask_.size(); ++i) {
        if (mask_[i] != '9') continue;
        buf[i] = '_';
        (dot == std::string::npos || i < dot ? left : right).push_back(i);
    }
    // At most as many digits as positions on each side (beginEdit and keyDown keep it so; setMask ends the input).
    const size_t ne = std::min(ent_.size(), left.size()), nd = std::min(dec_.size(), right.size());
    for (size_t i = 0; i < ne; ++i) buf[left[left.size() - ne + i]] = ent_[ent_.size() - ne + i];
    for (size_t i = 0; i < nd; ++i) buf[right[i]] = dec_[i];
    return buf;
}

std::string ValueField::shownText() const {
    if (!editing_) return text_;
    return mask_.empty() ? free_ : maskDisplay();
}

std::string ValueField::maskRead() const {
    std::string s = ent_.empty() ? "0" : ent_;
    if (mask_.find('.') != std::string::npos && !dec_.empty()) s += "." + dec_;
    return s;
}

// Masked input, as the prototype's maskEdit places it: with a point, at the point while the integer part is typed,
// then after the last decimal; without a point, at the n-th free position for n digits typed; after the mask's last
// digit position once that part is full (before a unit that follows it, " Hz", D-434).
int ValueField::caretChar() const {
    if (mask_.empty()) return Font::length(free_);
    std::vector<int> left, right;
    const size_t dot = mask_.find('.');
    for (size_t i = 0; i < mask_.size(); ++i)
        if (mask_[i] == '9') (dot == std::string::npos || i < dot ? left : right).push_back(int(i));
    const size_t last = mask_.find_last_of('9');
    const int end = last == std::string::npos ? int(mask_.size()) : int(last) + 1;
    if (dot == std::string::npos) return ent_.size() < left.size() ? left[ent_.size()] : end;
    if (afterDot_) return dec_.size() < right.size() ? right[dec_.size()] : end;
    return int(dot);
}

bool ValueField::keyDown(const KeyEvent& e) {
    if (!editing_) return false;
    if (e.key == Key::Enter) {
        endEdit(true);
        return true;
    }
    if (e.key == Key::Escape) {
        endEdit(false);
        return true;
    }
    if (!mask_.empty()) {
        // Digits go to the integer part until '.' or ',' is typed, then to the decimals, each up to its number of
        // positions; Backspace erases the decimals, then the point, then the integer digits.
        int left = 0, right = 0;
        const size_t dot = mask_.find('.');
        for (size_t i = 0; i < mask_.size(); ++i)
            if (mask_[i] == '9') (dot == std::string::npos || i < dot ? left : right)++;
        if (e.key == Key::Backspace) {
            if (!dec_.empty()) dec_.pop_back();
            else if (afterDot_) afterDot_ = false;
            else if (!ent_.empty()) ent_.pop_back();
        } else if (e.key == Key::Character && isDigit(e.ch)) {
            if (afterDot_) {
                if (int(dec_.size()) < right) dec_ += char(e.ch);
            } else if (int(ent_.size()) < left) {
                ent_ += char(e.ch);
            }
        } else if (e.key == Key::Character && (e.ch == '.' || e.ch == ',') && dot != std::string::npos) {
            afterDot_ = true;
            dec_.clear();
        }
        repaint();
        return true;  // the rest is rejected
    }
    // Without a mask: printable ASCII at the end, up to the input's width; a key replaces a selected old value.
    if (e.key == Key::Backspace) {
        if (selected_) free_.clear();
        else if (!free_.empty()) free_.pop_back();
        selected_ = false;
    } else if (e.key == Key::Character && e.ch >= 0x20 && e.ch < 0x7F) {
        if (selected_) free_.clear();
        selected_ = false;
        if (Font::length(free_) < cols_) free_ += char(e.ch);
    }
    repaint();
    return true;
}

void ValueField::endEdit(bool apply) {
    if (!editing_) return;
    editing_ = false;
    std::string txt = mask_.empty() ? free_ : maskRead();
    while (!txt.empty() && txt.back() == ' ') txt.pop_back();
    while (!txt.empty() && txt.front() == ' ') txt.erase(txt.begin());
    // editing_ is already false, so the focusLost this triggers does not end the input a second time.
    if (Desktop* d = desktop(); d && d->keyFocus() == this) d->setKeyFocus(nullptr);
    repaint();
    if (apply && onCommit) onCommit(txt);
}

void ValueField::focusLost() {
    if (editing_) endEdit(true);
}

void ValueField::focusDropped() { endEdit(false); }

void ValueField::paint(Painter& p) {
    const Rect r = bounds();
    const double cw = charWidth();
    const Box& b = box();
    const double top = b.y + (b.h - px_) / 2;
    // While typing: white field, the input drawn from the left of the last cols_ character cells (a masked input
    // exactly as wide as its mask; centred in a centred field), the selected old value in reverse video, otherwise a
    // 1 px text cursor.
    if (editing_) {
        const std::string shown = mask_.empty() ? free_ : maskDisplay();
        const double tw = mask_.empty() ? Font::width(shown, px_, 0, face_) : cols_ * cw;
        const double x = centered_      ? b.x + (b.w - tw) / 2
                         : mask_.empty() ? b.x + b.w - cols_ * cw
                                         : b.x + b.w - tw;
        p.fill(r, colors::kWhite);
        if (mask_.empty() && selected_) {
            const Rect sel = Box{x, b.y, Font::width(shown, px_, 0, face_), double(px_)}.snapped();
            p.fill(sel, colors::kBlack);
            p.text(shown, x, top, px_, colors::kWhite, 0, face_);
        } else {
            p.text(shown, x, top, px_, colors::kBlack, 0, face_);
            const int cx = snap(x + caretChar() * cw);
            p.fill({cx, r.y, 1, r.h}, colors::kBlack);
        }
        return;
    }
    const bool inverse = hover_ && editable_ && !dim_;
    const double tw = Font::width(text_, px_, 0, face_);
    const double x = centered_ ? b.x + (b.w - tw) / 2 : b.x + b.w - tw;
    // The unit that a mask puts after its last digit (" Hz"), or the one set without a mask, is not typed: the pointer
    // leaves it in normal video (Indus, 26/10/2026 03:51 and 15:25, D-438, D-451).
    const size_t last = mask_.find_last_of('9');
    std::string unit = last != std::string::npos ? mask_.substr(last + 1) : mask_.empty() ? unit_ : std::string();
    if (unit.size() > text_.size() || text_.compare(text_.size() - unit.size(), unit.size(), unit) != 0) unit.clear();
    const std::string value = text_.substr(0, text_.size() - unit.size());
    // Background and dashes on the box without the left padding (#susv, #kitSliders .val: origin and clip at the
    // content).
    const int left = snap(b.x + padChars_ * cw);
    const int inverseRight = unit.empty() ? r.right() : snap(x + Font::width(value, px_, 0, face_));
    if (inverse) p.fill({left, r.y, inverseRight - left, r.h}, colors::kBlack);
    if (editable_ && !inverse && !centered_) {
        // Dashes aligned right.
        // One per character cell, counted from the right edge; the cells under the text get none. Each dash is its
        // cell less 1 px of gap, 2 px high, 1 px above the bottom, cut at the left padding.
        const int right = r.right();
        const int typed = Font::length(text_);
        const int y = r.bottom() - 3;
        for (int k = typed;; ++k) {
            const int tileL = snap(right - (k + 1) * cw);
            const int tileR = snap(right - k * cw);
            if (tileR <= left) break;
            const int a = std::max(tileL, left), z = std::min(tileR - 1, right);
            if (z > a) p.fill({a, y, z - a, 2}, colors::kBlack);
        }
    }
    p.text(value, x, top, px_, inverse ? colors::kWhite : colors::kBlack, 0, face_);
    if (!unit.empty()) p.text(unit, x + Font::width(value, px_, 0, face_), top, px_, colors::kBlack, 0, face_);
}

void ValueField::setUnit(std::string u) {
    if (u == unit_) return;
    unit_ = std::move(u);
    repaint();
}

// ---------------------------------------------------------------------------------------------- TextField

void TextField::setText(std::string t) {
    if (t == text_) return;
    text_ = std::move(t);
    repaint();
}

void TextField::setPx(int px) {
    px_ = px;
    repaint();
}

double TextField::naturalHeight() const noexcept { return Font::lineBox("0", px_).height(); }

void TextField::mouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return;
    focused_ = true;
    repaint();
    if (Desktop* d = desktop()) d->setKeyFocus(this);
}

bool TextField::keyDown(const KeyEvent& e) {
    if (!focused_) return false;
    if (e.key == Key::Enter) {
        if (onEnter) onEnter();
        return true;
    }
    if (e.key == Key::Escape) return false;  // the box that holds it closes
    if (e.key == Key::Backspace) {
        if (text_.empty()) return true;
        size_t n = text_.size() - 1;  // last UTF-8 character
        while (n > 0 && (uint8_t(text_[n]) & 0xC0) == 0x80) --n;
        text_.resize(n);
    } else if (e.key == Key::Character && e.ch >= 0x20 && e.ch < 0x7F && !e.mods.ctrl) {
        text_ += char(e.ch);
    } else {
        return true;  // the field keeps the keys (the prototype's stopPropagation)
    }
    repaint();
    if (onChange) onChange();
    return true;
}

void TextField::focusLost() {
    focused_ = false;
    repaint();
}

void TextField::paint(Painter& p) {
    const Rect r = bounds();
    const Box& b = box();
    p.fill(r, colors::kWhite);
    // Underscore cell: one character wide, in whole pixels (at least 4).
    const double cw = std::max(4.0, std::round(Font::advance(px_)));
    const int typed = Font::length(text_);
    const int y = r.bottom() - 3;
    for (int k = typed;; ++k) {  // pattern repeated from the left, covered over the length of the text
        const int a = snap(b.x + k * cw);
        if (a >= r.right()) break;
        const int z = std::min(snap(b.x + (k + 1) * cw) - 1, r.right());
        if (z > a) p.fill({a, y, z - a, 2}, colors::kBlack);
    }
    const double lh = naturalHeight();
    p.text(text_, b.x + b.w - Font::width(text_, px_), b.y + (b.h - lh) / 2, px_, colors::kBlack);
    if (focused_) p.fill({r.right() - 1, r.y, 1, r.h}, colors::kBlack);  // text cursor
}

// ---------------------------------------------------------------------------------------------- GroupBox

void GroupBox::setLegend(std::string l) {
    legend_ = std::move(l);
    repaint();
}

void GroupBox::setHint(std::string h) {
    if (h == hint_) return;
    hint_ = std::move(h);
    repaint();
}

void GroupBox::setOpacity(double a) {
    if (a == opacity_) return;
    opacity_ = a;
    repaint(extent());
}

double GroupBox::legendWidth() const noexcept {
    const std::string text = hint_.empty() ? legend_ : legend_ + " ";
    return 12 + Font::width(text, 16, 1) + Font::width(hint_, 12, 1, Face::kSmall);
}

void GroupBox::setLegendInset(int px) {
    inset_ = px;
    repaint();
}

Box GroupBox::legendBox() const noexcept { return {box().x + inset_, box().y, legendWidth(), double(kLegendHeight)}; }

void GroupBox::paint(Painter& p) {
    const Rect r = bounds();
    // f: the frame, 7 px below the box's top so that its top rule crosses the middle of the legend's line; gap: the
    // legend's box, which interrupts that rule.
    const Rect f{r.x, r.y + 7, r.w, r.h - 7};
    const Rect gap = legendBox().snapped();
    // Top rule interrupted by the legend, then the other three sides.
    if (legend_.empty()) {
        p.fill({f.x, f.y, f.w, 2}, colors::kBlack);
    } else {
        p.fill({f.x, f.y, gap.x - f.x, 2}, colors::kBlack);
        p.fill({gap.right(), f.y, f.right() - gap.right(), 2}, colors::kBlack);
    }
    p.fill({f.x, f.bottom() - 2, f.w, 2}, colors::kBlack);
    p.fill({f.x, f.y, 2, f.h}, colors::kBlack);
    p.fill({f.right() - 2, f.y, 2, f.h}, colors::kBlack);
    if (legend_.empty()) return;
    const std::string text = hint_.empty() ? legend_ : legend_ + " ";
    const double x = box().x + inset_ + 6;  // the legend's 6 px left padding
    const int baseline = snap(box().y + Font::ascent(16));
    p.textAt(text, x, baseline, 16, colors::kBlack, 1);
    // Note of the legend ("(per channel)"): 6×6 font, the 8×8 would bite into the rule (D-071).
    if (!hint_.empty()) p.textAt(hint_, x + Font::width(text, 16, 1), baseline, 12, colors::kHint, 1, Face::kSmall);
}

// ------------------------------------------------------------------------------------------- WindowFrame

// 2 px black border, 2 px of the white background, then the 2 px black inner rule: kInset (6 px) in all.
void WindowFrame::paint(Painter& p) {
    const Rect r = bounds();
    p.fill(r, colors::kWhite);
    p.frame(r, 2, colors::kBlack);
    p.frame(r.inset(4), 2, colors::kBlack);
}

// ---------------------------------------------------------------------------------------------- TitleBar

void TitleBar::setTitle(std::string t) {
    if (t == title_) return;
    title_ = std::move(t);
    repaint();
}

void TitleBar::setBoxes(bool close, bool full) {
    close_ = close;
    full_ = full;
    repaint();
}

Rect TitleBar::closeRect() const noexcept {
    const Rect r = bounds();
    return close_ ? Rect{r.x, r.y, kBox, kBox} : Rect{r.x, r.y, 0, 0};
}

Rect TitleBar::fullRect() const noexcept {
    const Rect r = bounds();
    return full_ ? Rect{r.right() - kBox, r.y, kBox, kBox} : Rect{r.right(), r.y, 0, 0};
}

void TitleBar::paint(Painter& p) {
    const Rect r = bounds();
    p.fill({r.x, r.y, r.w, kBox}, colors::kWhite);
    p.fill({r.x, r.y + kBox, r.w, 2}, colors::kBlack);
    const Rect c = closeRect(), f = fullRect();
    // Each box: a 2 px rule on its inner side; the 8×8 glyph at twice its size (D-418, D-424), 16 × 14 px, centred in
    // the 22 px left beside it.
    if (close_) {
        p.fill({c.right() - 2, c.y, 2, kBox}, colors::kBlack);
        doubledGlyph(p, kGlyphClose, {c.x, c.y, kBox - 2, kBox}, colors::kBlack);
    }
    if (full_) {
        p.fill({f.x, f.y, 2, kBox}, colors::kBlack);
        doubledGlyph(p, kGlyphFull, {f.x + 2, f.y, kBox - 2, kBox}, colors::kBlack);
    }
    const Rect mid{c.right(), r.y, f.x - c.right(), kBox};
    p.tile(mid, tiles::titleHatch(), {mid.x, mid.y});
    // Title on a white box with 12 px of padding on each side, centred in the hatched area.
    if (!title_.empty()) {
        const double w = Font::width(title_, 16) + 24;
        const double x = mid.x + (mid.w - w) / 2;
        p.fill(Box{x, double(mid.y), w, double(kBox)}.snapped(), colors::kWhite);
        p.text(title_, x + 12, mid.y + 4, 16, colors::kBlack);
    }
}

void TitleBar::mouseDown(const MouseEvent& e) {
    pressed_ = 0;
    if (e.button != MouseButton::Left) return;
    if (closeRect().contains(e.pos)) pressed_ = 1;
    else if (fullRect().contains(e.pos)) pressed_ = 2;
    else if (movable_) {
        pressed_ = 3;
        if (onGrab) onGrab(e.pos);
    }
}

void TitleBar::mouseDrag(const MouseEvent& e) {
    if (pressed_ == 3 && onDrag) onDrag(e.pos);
}

void TitleBar::mouseUp(const MouseEvent& e) {
    const int was = pressed_;
    pressed_ = 0;
    if (was == 1 && closeRect().contains(e.pos) && onClose) onClose();
    else if (was == 2 && fullRect().contains(e.pos) && onFull) onFull();
}

Pointer TitleBar::pointerAt(Point p) const noexcept {
    if (pressed_ == 3) return Pointer::Move;
    if (closeRect().contains(p)) return Pointer::Hand;
    return movable_ && !fullRect().contains(p) ? Pointer::Move : Pointer::Arrow;
}

std::string TitleBar::tipAt(Point p) const {
    if (closeRect().contains(p)) return closeTip_;
    if (fullRect().contains(p)) return fullTip_;
    return tip();
}

// -------------------------------------------------------------------------------------------- FloatWindow

FloatWindow::FloatWindow() {
    setVisible(false);
    add(bar_);
    bar_.setBoxes(true, false);
    bar_.setMovable(true);
    bar_.onClose = [this] { requestClose(); };
    bar_.onGrab = [this](Point p) { grab_ = {p.x - bounds().x, p.y - bounds().y}; };
    // The window follows the pointer at the offset where its bar was grabbed, its left edge never negative and its
    // top never above the work area (mkWinDrag, D-302), its right and bottom edges never past the desktop's (D-460).
    bar_.onDrag = [this](Point p) {
        const Point at = onDesktop(p.x - grab_.x, p.y - grab_.y);
        moveBy(at.x - bounds().x, at.y - bounds().y);
    };
}

Point FloatWindow::onDesktop(int x, int y) const noexcept {
    const Desktop* d = desktop();
    if (!d) return {std::max(0, x), std::max(0, y)};
    const Rect r = bounds();
    return {std::max(0, std::min(x, d->width() - r.w)), std::max(d->workTop(), std::min(y, d->height() - r.h))};
}

void FloatWindow::keepOnDesktop() {
    const Rect r = bounds();
    const Point at = onDesktop(r.x, r.y);
    if (at.x != r.x || at.y != r.y) moveBy(at.x - r.x, at.y - r.y);
}

void FloatWindow::place(const Box& b) {
    setBox(b);
    const Rect r = bounds();
    bar_.setBounds({r.x + kInset, r.y + kInset, r.w - 2 * kInset, TitleBar::kHeight});
    keepOnDesktop();  // grown near the right or bottom edge: moved back inside (D-460)
}

void FloatWindow::open() {
    keepOnDesktop();
    setVisible(true);
    toFront();
}

void FloatWindow::close() {
    if (!visible()) return;
    setVisible(false);
    if (onClose) onClose();
}

FloatWindow* FloatWindow::topmost(const Widget& parent, bool escapeOnly) noexcept {
    const auto& v = parent.children();
    for (auto it = v.rbegin(); it != v.rend(); ++it) {
        auto* w = dynamic_cast<FloatWindow*>(*it);
        if (w && w->visible() && (!escapeOnly || w->closesOnEscape)) return w;
    }
    return nullptr;
}

// ------------------------------------------------------------------------------------------------ DeskIcon

void DeskIcon::setPicture(const Bitmap* data, const Bitmap* mask) noexcept {
    data_ = data;
    mask_ = mask;
    repaint();
}

void DeskIcon::setLetter(char c, int x, int y) noexcept {
    letter_ = c;
    letterAt_ = {x, y};
    repaint();
}

void DeskIcon::setName(std::string name) {
    if (name == name_) return;
    name_ = std::move(name);
    repaint();
}

bool DeskIcon::hit(Point p) const noexcept {
    // The picture or the name box, as the AES finds an icon (objc_find): not the desktop beside the picture.
    const Rect b = bounds();
    const Rect picture{b.x + (kW - kPicture) / 2, b.y, kPicture, kPicture};
    return picture.contains(p) || Rect{b.x, b.y + kPicture, kW, kNameH}.contains(p);
}

// The picture centred at the top of the 72 px box, the name in the 8 px box below it, centred on a whole pixel.
void DeskIcon::paint(Painter& p) {
    const Rect b = bounds();
    const int px = b.x + (kW - kPicture) / 2;
    if (mask_) p.bitmap(*mask_, px, b.y, colors::kWhite);
    if (data_) p.bitmap(*data_, px, b.y, colors::kBlack);
    if (letter_) {  // the top of the 6×6 cell at letterAt_ (D-419)
        const int g = Font::glyph(char32_t(uint8_t(letter_)));
        const int top = Font::raster(8, g, 0, Face::kSmall).top;
        p.glyph(g, px + letterAt_.x, b.y + letterAt_.y - top, 8, colors::kBlack, 0, Face::kSmall);
    }
    p.fill({b.x, b.y + kPicture, kW, kNameH}, colors::kWhite);
    const double w = Font::width(name_, 8, 0, Face::kSmall);
    p.text(name_, b.x + std::floor((kW - w) / 2), b.y + kPicture, 8, colors::kBlack, 0, Face::kSmall);
}

Point DeskIcon::landing(Point pointer) const noexcept { return snapped({pointer.x - grab_.x, pointer.y - grab_.y}); }

Point DeskIcon::snapped(Point topLeft) const noexcept {
    if (!gridOn_) {
        const int x = std::clamp(topLeft.x, limits_.x, std::max(limits_.x, limits_.right() - kW));
        const int y = std::clamp(topLeft.y, limits_.y, std::max(limits_.y, limits_.bottom() - kH));
        return {x, y};
    }
    // Nearest cell (a half towards the right or the bottom), within the first and last cells wholly inside the limits.
    auto cell = [](int v, int origin, int size, int lo, int hi) {
        const int first = -static_cast<int>(std::floor(double(origin - lo) / size));
        const int last = std::max(first, static_cast<int>(std::floor(double(hi - size - origin) / size)));
        const int k = static_cast<int>(std::floor(double(v - origin) / size + 0.5));
        return origin + std::clamp(k, first, last) * size;
    };
    return {cell(topLeft.x, grid_.x, kW, limits_.x, limits_.right()),
            cell(topLeft.y, grid_.y, kH, limits_.y, limits_.bottom())};
}

Rect DeskIcon::dragRect() const noexcept { return {at_.x, at_.y, kW, kH}; }

void DeskIcon::repaintDrag() noexcept { repaint(dragRect()); }

void DeskIcon::paintDrag(Painter& p) const {
    if (!drag_) return;
    // Dotted outlines of the picture and of the name box, as the GEM desktop drags an icon (one pixel in two).
    auto dotted = [&p](const Rect& r) {
        for (int x = r.x; x < r.right(); ++x)
            for (const int y : {r.y, r.bottom() - 1})
                if ((x + y) % 2 == 0) p.pixel(x, y, colors::kBlack);
        for (int y = r.y + 1; y < r.bottom() - 1; ++y)
            for (const int x : {r.x, r.right() - 1})
                if ((x + y) % 2 == 0) p.pixel(x, y, colors::kBlack);
    };
    dotted({at_.x + (kW - kPicture) / 2, at_.y, kPicture, kPicture});
    dotted({at_.x, at_.y + kPicture, kW, kNameH});
}

void DeskIcon::mouseDown(const MouseEvent& e) {
    if (e.button != MouseButton::Left) return;
    pressed_ = true;
    drag_ = false;
    wandered_ = false;
    pressAt_ = e.pos;
    grab_ = {e.pos.x - bounds().x, e.pos.y - bounds().y};
    at_ = {bounds().x, bounds().y};
}

void DeskIcon::mouseDrag(const MouseEvent& e) {
    if (!pressed_) return;
    // Beyond a few pixels, the press is a drag even if the icon stays in its cell: its release is not a click.
    wandered_ = wandered_ || std::abs(e.pos.x - pressAt_.x) > 2 || std::abs(e.pos.y - pressAt_.y) > 2;
    const Point to = landing(e.pos);
    if (!drag_ && to.x == bounds().x && to.y == bounds().y) return;  // not moved yet: maybe a click
    if (drag_) repaintDrag();
    drag_ = true;
    at_ = to;
    repaintDrag();
}

// The drop: the outline goes, the icon moves to its landing place and onMoved tells the owner.
void DeskIcon::mouseUp(const MouseEvent&) {
    const bool moved = drag_, was = pressed_;
    pressed_ = false;
    if (!moved) {
        if (was && !wandered_ && onClick) onClick();
        return;
    }
    repaintDrag();
    drag_ = false;
    if (at_.x == bounds().x && at_.y == bounds().y) return;
    if (canLand && !canLand(at_)) return;
    moveBy(at_.x - bounds().x, at_.y - bounds().y);
    if (onMoved) onMoved(at_);
}

// No drop: the outline goes and the icon stays at its place.
void DeskIcon::mouseCancel() {
    pressed_ = false;
    if (!drag_) return;
    repaintDrag();
    drag_ = false;
}

}  // namespace stym::gemui
