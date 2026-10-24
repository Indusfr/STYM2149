// STYM2149 — pluginui: elements specific to the main window
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Geometry follows the browser's so that the drawings match the prototype's captures: canvases are drawn at their own
// size and scaled into their box (Canvas), and CSS boxes keep the browser's layout units (1/64 px, floor64) before
// being snapped to whole pixels. Most setters repaint only when what the widget shows changes.
#include "pluginui/Widgets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "gemui/Font.h"

namespace stym::pluginui {

using gemui::Box;
using gemui::Color;
using gemui::Font;
using gemui::Painter;
using gemui::Point;
using gemui::Rect;
using gemui::snap;
namespace colors = gemui::colors;

namespace {

// Edge of a layout box in 1/64 of a pixel, truncated (Chromium layout units).
double floor64(double v) noexcept { return std::floor(v * 64) / 64; }

}  // namespace

// ------------------------------------------------------------------------------------------------ Canvas

Canvas::Canvas(int w, int h) : w_(w), h_(h), px_(size_t(w) * size_t(h), colors::kWhite) {}

void Canvas::clear(Color c) { std::fill(px_.begin(), px_.end(), c); }

void Canvas::pixel(int x, int y, Color c) noexcept {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    px_[size_t(y * w_ + x)] = c;
}

void Canvas::fill(int x, int y, int w, int h, Color c) noexcept {
    for (int j = std::max(0, y); j < std::min(h_, y + h); ++j)
        for (int i = std::max(0, x); i < std::min(w_, x + w); ++i) px_[size_t(j * w_ + i)] = c;
}

// Integer Bresenham for every octant: err tracks the distance to the ideal line, and each step moves in x, in y or in
// both. Both ends are drawn.
void Canvas::line(int x0, int y0, int x1, int y1, Color c) noexcept {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Same walk as line(), with a 2 × 2 square at each point.
void Canvas::line2(int x0, int y0, int x1, int y1, Color c) noexcept {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fill(x0 - 1, y0 - 1, 2, 2, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Monospaced: character i starts at x + i × advance, spaces included. Each glyph row is a 16-bit mask whose most
// significant bit is the leftmost pixel; gl.top places the rows relative to the baseline.
void Canvas::text(std::string_view s, double x, int baseline, int px, Color c, gemui::Face face) {
    int i = 0;
    for (size_t pos = 0; pos < s.size(); ++i) {
        const char32_t cp = gemui::nextCodepoint(s, pos);
        if (cp == ' ') continue;
        const gemui::GlyphPos g = gemui::glyphPos(x + i * Font::advance(px, face));
        const Font::Raster gl = Font::raster(px, Font::glyph(cp), g.quarter, face);
        for (int r = 0; r < gl.count; ++r)
            for (int b = 0; b < 16; ++b)
                if (gl.bits(r) & (0x8000 >> b)) pixel(g.x + gl.shift + b, baseline + gl.top + r, c);
    }
}

void Canvas::blit(Painter& p, const Rect& r) const { p.image(r, px_.data(), w_, h_); }

// ------------------------------------------------------------------------------------------------- Scope

Scope::Scope() { canvas_.fill(0, 44, 920, 2, colors::kBlack); }  // flat trace on the centre line until samples come

void Scope::setSamples(std::span<const float> buf) {
    canvas_.clear(colors::kWhite);
    const int n = int(buf.size());
    // Trigger: the first rising zero crossing in the first half, so that a steady wave stands still from one
    // update to the next; none found, the trace starts at sample 0.
    int start = 0;
    for (int i = 1; i < n / 2; ++i)
        if (buf[size_t(i - 1)] <= 0 && buf[size_t(i)] > 0) {
            start = i;
            break;
        }
    const int count = std::min(1024, n - start);
    int px0 = 0, py0 = 0;
    for (int i = 0; i < count; ++i) {
        // Full scale (±1) spans ±40.5 px around the centre (y = 45); larger peaks are clamped just off the canvas.
        const double x = double(i) / count * 920;
        const double y = 45 - double(buf[size_t(start + i)]) * 90 * 0.45;
        const int px1 = int(std::floor(x)), py1 = int(std::floor(std::clamp(y, -4.0, 94.0)));
        if (i == 0) {
            px0 = px1;
            py0 = py1;
        }
        canvas_.line2(px0, py0, px1, py1, colors::kBlack);
        px0 = px1;
        py0 = py1;
    }
    repaint();
}

void Scope::paint(Painter& p) {
    const Rect r = bounds();
    p.frame(r, 2, colors::kBlack);
    canvas_.blit(p, r.inset(2));
}

// ---------------------------------------------------------------------------------------- RegisterMonitor

void RegisterMonitor::setValues(const std::array<uint8_t, 8>& v) {
    if (v == values_) return;
    values_ = v;
    repaint();
}

void RegisterMonitor::setInfo(std::string info) {
    if (info == info_) return;
    info_ = std::move(info);
    repaint();
}

// Columns of the grid (14 × 1fr in 940 px), edges in 1/64 of a pixel. Left edge of column k (0 to 14), inside the
// 2 px border.
double RegisterMonitor::columnX(int k) const noexcept {
    const double inner = box().w - 4;
    return box().x + 2 + floor64(inner * k / 14);
}

Rect RegisterMonitor::infoRect() const noexcept {
    const double x0 = columnX(8), x1 = columnX(14);
    return Box{x0, box().y + 2 + 16, x1 - x0, 20}.snapped();
}

void RegisterMonitor::paint(Painter& p) {
    static constexpr const char* kHeads[8] = {"R0 TPlo", "R1 TPhi", "R6 NP",    "R7 MIX",
                                              "R8 VOL",  "R11 EPlo", "R12 EPhi", "R13 SHP"};
    const Rect r = bounds();
    p.fill(r, colors::kWhite);
    p.frame(r, 2, colors::kBlack);
    const double top = box().y + 2;
    const double headH = 16, valH = 20;
    // Cell over columns k0 to k1: black for a header, a 2 px rule on its right unless it is the last, text centred.
    auto cell = [&](int k0, int k1, double y, double h, bool head, bool last, const std::string& text) {
        const double x0 = columnX(k0), x1 = columnX(k1);
        const Rect c = Box{x0, y, x1 - x0, h}.snapped();
        if (head) p.fill(c, colors::kBlack);
        if (!last) p.fill({c.right() - 2, c.y, 2, c.h}, colors::kBlack);
        // Headers in the 6×6 font: "R11 EPlo" in 8×8 would fill the whole cell (D-071).
        const int px = head ? 10 : 14;
        const gemui::Face face = head ? gemui::Face::kSmall : gemui::Face::kSystem;
        const double content = x1 - x0 - (last ? 0 : 2);
        const double tx = x0 + (content - Font::width(text, px, 0, face)) / 2;
        p.text(text, tx, y + 3, px, head ? colors::kWhite : colors::kBlack, 0, face);
    };
    char hex[4];
    for (int k = 0; k < 8; ++k) {
        cell(k, k + 1, top, headH, true, false, kHeads[k]);
        std::snprintf(hex, sizeof hex, "%02X", unsigned(values_[size_t(k)]));
        cell(k, k + 1, top + headH, valH, false, false, hex);
    }
    cell(8, 14, top, headH, true, false, "YM2149F REGISTER MONITOR");
    cell(8, 14, top + headH, valH, false, true, info_);
}

// -------------------------------------------------------------------------------------------- VoiceLeds

void VoiceLeds::setState(const std::array<bool, 16>& on, int available) {
    if (on == on_ && available == available_) return;
    on_ = on;
    available_ = available;
    repaint();
}

void VoiceLeds::paint(Painter& p) {
    // Two rows of eight, voices 1-8 then 9-16, every 16 px (12 px LED, 4 px gap).
    for (int i = 0; i < 16; ++i) {
        const double x = box().x + (i % 8) * 16, y = box().y + (i / 8) * 16;
        const Rect c = Box{x, y, 12, 12}.snapped();
        const bool off = i >= available_;
        if (off) p.beginOpacity(c, 0.25);
        p.fill(c, on_[size_t(i)] ? colors::kLed : colors::kLedOff);
        p.frame(c, 2, colors::kBlack);
        if (off) p.endOpacity();
    }
}

// --------------------------------------------------------------------------------------------- Keyboard

void Keyboard::setBaseOctave(int oct) {
    oct = std::clamp(oct, 0, 9);
    if (oct == oct_) return;
    oct_ = oct;
    repaint();
}

void Keyboard::setHeld(int note, bool held) {
    if (note < 0 || note > 127 || held_[size_t(note)] == held) return;
    held_[size_t(note)] = held;
    repaint();
}

void Keyboard::clearHeld() {
    held_.fill(false);
    repaint();
}

void Keyboard::setNoteNames(const std::array<std::string, 12>& names) {
    names_ = names;
    repaint();
}

std::string Keyboard::noteName(int note) const {
    // MIDI numbering: note 60 is C4.
    return names_[size_t(note % 12)] + std::to_string(note / 12 - 1);
}

// The 48 keys of the four octaves shown, each white key followed by the black key after it, with their boxes in
// desktop pixels.
std::vector<Keyboard::Key> Keyboard::keys() const {
    // Semitones of the white keys in an octave, and of the black key that follows each one (-1: none after E and B).
    static constexpr int kWhite[7] = {0, 2, 4, 5, 7, 9, 11};
    static constexpr int kBlackAfter[7] = {1, 3, -1, 6, 8, 10, -1};
    const int kbOct = std::min(oct_, 6);  // usable window: C6-B9 at most
    const int base = (kbOct + 1) * 12;
    // flex:1: the 28 white keys have the same width, to the nearest 1/64 of a pixel (Blink); the black ones are placed
    // in percent, truncated to 1/64.
    const double w = box().w, keyW = std::round(w / 28 * 64) / 64;
    std::vector<Key> out;
    for (int o = 0; o < 4; ++o)
        for (int k = 0; k < 7; ++k) {
            const int i = o * 7 + k;
            out.push_back({base + o * 12 + kWhite[k], false, {box().x + i * keyW, box().y, keyW, box().h}});
            if (kBlackAfter[k] >= 0) {
                // Black key from 68 % of its white key, 62 % of a white key wide (buildKbd's inline style, which
                // overrides the 60 % of the .bk rule), 58 % of the height.
                const double left = floor64(w * (i + 0.68) / 28);
                out.push_back({base + o * 12 + kBlackAfter[k], true,
                               {box().x + left, box().y, floor64(w * 0.62 / 28), floor64(box().h * 0.58)}});
            }
        }
    return out;
}

void Keyboard::paint(Painter& p) {
    const auto ks = keys();
    auto nope = [](int note) { return note < kPlayLo || note > kPlayHi; };
    // 3 px hatching at 135° (repeating-linear-gradient of the prototype), without anti-aliasing, starting from the
    // corner of the padding box (background-origin): under the 1 and 2 px borders of a white key.
    auto stripes = [&](const Rect& r, Point origin, Color a, Color b) {
        for (int y = r.y; y < r.bottom(); ++y)
            for (int x = r.x; x < r.right(); ++x) {
                // Distance of the pixel centre along the gradient's axis (x + y) / √2; colour a for the first 3 px of
                // each 6 px period (+ 600, a multiple of 6, keeps the operand of fmod positive).
                const double d = (x - origin.x + 0.5 + y - origin.y + 0.5) / std::sqrt(2.0);
                p.pixel(x, y, std::fmod(d + 600, 6.0) < 3 ? a : b);
            }
    };
    for (const Key& k : ks) {
        if (k.black) continue;
        const Rect r = k.box.snapped();
        if (nope(k.note)) stripes(r, {r.x + 1, r.y + 2}, colors::kWhite, colors::kNopeStripe);
        else if (held_[size_t(k.note)])  // halftone anchored in the padding box (borders of 1 and 2 px)
            p.tile(r, gemui::tiles::halftone(colors::kBlack, colors::kWhite), {r.x + 1, r.y + 2});
        else p.fill(r, colors::kWhite);
        p.fill({r.x, r.y, r.w, 2}, colors::kBlack);
        p.fill({r.x, r.bottom() - 2, r.w, 2}, colors::kBlack);
        p.fill({r.x, r.y, 1, r.h}, colors::kBlack);
        p.fill({r.right() - 1, r.y, 1, r.h}, colors::kBlack);
        const std::string name = noteName(k.note);
        const double tw = Font::width(name, 9);
        const Color ink = nope(k.note) ? colors::kGreyDisabled : colors::kBlack;
        // Name centred, its 9 px line above the 2 px bottom border and a 3 px margin.
        p.text(name, k.box.x + (k.box.w - tw) / 2, k.box.bottom() - 2 - 3 - 9, 9, ink);
    }
    // Black keys after all the white ones: they are drawn over them.
    for (const Key& k : ks) {
        if (!k.black) continue;
        const Rect r = k.box.snapped();
        if (nope(k.note)) stripes(r, {r.x, r.y}, colors::kBlack, colors::kNopeStripeDark);
        else if (held_[size_t(k.note)]) p.tile(r, gemui::tiles::halftone(colors::kWhite, colors::kBlack), {r.x, r.y});
        else p.fill(r, colors::kBlack);
    }
}

std::optional<Keyboard::Key> Keyboard::keyAt(Point pt) const {
    const auto ks = keys();
    for (const Key& k : ks)
        if (k.black && k.box.snapped().contains(pt)) return k;
    for (const Key& k : ks)
        if (!k.black && k.box.snapped().contains(pt)) return k;
    return std::nullopt;
}

int Keyboard::noteAt(Point pt) const {
    const auto k = keyAt(pt);
    return k ? k->note : -1;
}

Box Keyboard::keyBoxAt(Point pt) const {
    const auto k = keyAt(pt);
    return k ? k->box : box();
}

std::string Keyboard::tipAt(Point pt) const {
    const int n = noteAt(pt);
    if (n < 0) return {};
    const std::string nm = noteName(n);
    if (n < kPlayLo || n > kPlayHi) return nm + ": out of the chip range (B0 to C9)";
    return nm;
}

void Keyboard::mouseDown(const gemui::MouseEvent& e) {
    if (e.button == gemui::MouseButton::Right) {
        if (onContext) onContext(e.pos);
        return;
    }
    if (e.button != gemui::MouseButton::Left) return;
    dragNote_ = noteAt(e.pos);
    if (dragNote_ >= 0 && onPress) onPress(dragNote_);
}

void Keyboard::mouseDrag(const gemui::MouseEvent& e) {
    if (dragNote_ < 0) return;
    const int n = noteAt(e.pos);
    if (n < 0 || n == dragNote_) return;
    // Glissando: the note follows the pointer.
    if (onRelease) onRelease(dragNote_);
    dragNote_ = n;
    if (onPress) onPress(n);
}

void Keyboard::mouseUp(const gemui::MouseEvent&) {
    if (dragNote_ >= 0 && onRelease) onRelease(dragNote_);
    dragNote_ = -1;
}

// ------------------------------------------------------------------------------------------------- Pads

void Pads::setLabels(const std::array<std::string, 8>& labels) {
    if (labels == labels_) return;
    labels_ = labels;
    repaint();
}

void Pads::setTips(const std::array<std::string, 8>& tips) { tips_ = tips; }

void Pads::setEmpty(const std::array<bool, 8>& empty) {
    if (empty == empty_) return;
    empty_ = empty;
    repaint();
}

void Pads::setHeld(int pad, bool held) {
    if (pad < 0 || pad >= 8 || held_[size_t(pad)] == held) return;
    held_[size_t(pad)] = held;
    repaint();
}

void Pads::clearHeld() {
    held_.fill(false);
    repaint();
}

Box Pads::padBox(int i) const noexcept {
    // Eight equal columns with seven 4 px gaps.
    const double w = (box().w - 7 * 4) / 8;
    return {box().x + i * (w + 4), box().y, w, box().h};
}

int Pads::padAt(Point pt) const noexcept {
    for (int i = 0; i < 8; ++i)
        if (padBox(i).snapped().contains(pt)) return i;
    return -1;
}

Box Pads::padBoxAt(Point pt) const noexcept {
    const int i = padAt(pt);
    return i >= 0 ? padBox(i) : box();
}

void Pads::paint(Painter& p) {
    for (int i = 0; i < 8; ++i) {
        const Box b = padBox(i);
        const Rect r = b.snapped();
        const bool dim = empty_[size_t(i)];
        if (dim) p.beginOpacity(r, 0.45);
        p.fill(r, held_[size_t(i)] ? colors::kGrey : colors::kWhite);
        p.frame(r, 2, colors::kBlack);
        const std::string& t = labels_[size_t(i)];
        p.text(t, b.x + (b.w - Font::width(t, 11)) / 2, b.y + (b.h - 11) / 2, 11, colors::kBlack);
        if (dim) p.endOpacity();
    }
}

std::string Pads::tipAt(Point pt) const {
    const int i = padAt(pt);
    return i < 0 ? std::string() : tips_[size_t(i)];
}

void Pads::mouseDown(const gemui::MouseEvent& e) {
    if (e.button == gemui::MouseButton::Right) {
        if (onContext) onContext(e.pos);
        return;
    }
    if (e.button != gemui::MouseButton::Left) return;
    down_ = padAt(e.pos);
    if (down_ < 0) return;
    setHeld(down_, true);
    if (onPress) onPress(down_);
}

void Pads::mouseDrag(const gemui::MouseEvent& e) {
    // The pad is released when the pointer leaves it (pointerleave of the prototype).
    if (down_ >= 0 && !padBox(down_).snapped().contains(e.pos)) mouseUp(e);
}

void Pads::mouseUp(const gemui::MouseEvent&) {
    if (down_ < 0) return;
    const int pad = down_;
    down_ = -1;
    setHeld(pad, false);
    if (onRelease) onRelease(pad);
}

// ------------------------------------------------------------------------------------------- AdsrCurve

void AdsrCurve::setShape(const Shape& s) {
    if (s == shape_) return;
    shape_ = s;
    repaint();
}

void AdsrCurve::setLive(int stage, double level) {
    // Level changes of 2 % or less within the same stage do not repaint.
    if (stage == stage_ && (stage < 0 || std::fabs(level - level_) <= 0.02)) return;
    stage_ = stage;
    level_ = level;
    repaint();
}

void AdsrCurve::paint(Painter& p) {
    // Canvas of the rounded size of the box (cv.width = round(width)): 52 px high in the prototype, 74 under the ADSR
    // frame (sixth DAW test, D-268).
    const int W = int(std::lround(box().w)), H = int(std::lround(box().h));
    const int ox = snap(box().x), oy = snap(box().y);
    // Margins in canvas pixels (top and bottom, left and right, the letters' strip at the bottom) and the time drawn
    // for the sustain, in ms. The time axis spans A + D + HOLD + R; the level axis runs from BOT (0) up to PAD (1).
    constexpr double PAD = 5, PADX = 12, LAB = 11, HOLD = 400;
    const Shape& s = shape_;
    const double a = s.attackMs, d0 = s.decayMs, r = s.releaseMs;
    const double sus = std::clamp(s.sustain, 0.0, 1.0);
    const double total = a + d0 + HOLD + r;
    const double BOT = H - PAD - LAB;
    auto X = [&](double ms) { return PADX + (W - 2 * PADX) * ms / total; };
    auto Y = [&](double l) { return BOT - (BOT - PAD) * l; };
    const double t1 = a, t2 = a + d0, t3 = a + d0 + HOLD, t4 = total;
    auto px = [](double v) { return int(std::floor(v)); };  // Math.round(v) + 0.5: the pixel of v rounded
    auto rnd = [](double v) { return int(std::floor(v + 0.5)); };
    auto line = [&](int x0, int y0, int x1, int y1, Color c) {
        p.line({ox + x0, oy + y0}, {ox + x1, oy + y1}, c);
    };

    // Guides: sustain level and end of each segment.
    line(px(PADX), px(Y(sus)), px(W - PADX) - 1, px(Y(sus)), colors::kCurveGuide);
    for (double t : {t1, t2, t3}) {
        if (t <= 0 || t >= total) continue;
        line(px(X(t)), int(PAD), px(X(t)), int(BOT) - 1, colors::kCurveGuide);
    }

    // Not applicable: the ADSR ENVELOPE frame dims the whole drawing (D-220).
    const Color ink = colors::kBlack;
    int cx = rnd(X(0)), cy = rnd(Y(0));
    auto to = [&](double x, double y) {
        const int nx = rnd(x), ny = rnd(y);
        line(cx, cy, nx, ny, ink);
        cx = nx;
        cy = ny;
    };
    if (s.chip) {
        // Steps: the register takes only sixteen levels. From level l0 at time ta to l1 at tb: one riser per level
        // crossed, evenly spaced in time, as a staircase of vertical and horizontal lines.
        auto steps = [&](double ta, double tb, double l0, double l1) {
            const int n0 = int(std::lround(l0 * 15)), n1 = int(std::lround(l1 * 15));
            const int count = std::abs(n1 - n0);
            if (count == 0 || tb - ta <= 0) {
                to(X(tb), Y(l1));
                return;
            }
            const int dir = n1 > n0 ? 1 : -1;
            for (int k = 1; k <= count; ++k) {
                const int n = n0 + dir * k;
                const double t = ta + (tb - ta) * k / count;
                to(X(t), Y((n - dir) / 15.0));
                to(X(t), Y(n / 15.0));
            }
        };
        steps(0, t1, 0, 1);
        steps(t1, t2, 1, sus);
        to(X(t3), Y(sus));
        steps(t3, t4, sus, 0);
    } else {
        to(X(t1), Y(1));
        to(X(t2), Y(sus));
        to(X(t3), Y(sus));
        to(X(t4), Y(0));
    }

    // Letters under each segment wide enough (9 px GEM font; the prototype uses the system's monospaced font).
    auto letter = [&](double ta, double tb, const char* c) {
        if (X(tb) - X(ta) < 8) return;
        p.textAt(c, ox + (X(ta) + X(tb)) / 2 - 2, oy + H - 2, 9, ink);
    };
    letter(0, t1, "A");
    letter(t1, t2, "D");
    letter(t2, t3, "S");
    letter(t3, t4, "R");

    // Position of the followed voice: the point stops on the plateau as long as the note holds.
    // The time is found back from the level, as if each segment were a straight line: attack 0 → 1, decay 1 → S,
    // the middle of the plateau for the sustain, release S → 0.
    if (stage_ > 0 && !s.off) {
        const double lv = std::clamp(level_, 0.0, 1.0);
        double t = -1;
        if (stage_ == 1) t = a > 0 ? lv * a : 0;
        else if (stage_ == 2) t = t1 + (d0 > 0 && sus < 1 ? (1 - lv) / (1 - sus) * d0 : 0);
        else if (stage_ == 3) t = t2 + HOLD / 2;
        else if (stage_ == 4) t = t3 + (r > 0 && sus > 0 ? (1 - lv / sus) * r : 0);
        if (t >= 0) {
            const int x = rnd(X(std::clamp(t, 0.0, total))), y = rnd(Y(lv));
            p.fill({ox + x - 1, oy + int(PAD), 3, int(BOT - PAD)}, colors::kGreen);
            // Disc of radius 3.5 centred on pixel (x, y).
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                    if (dx * dx + dy * dy <= 12) p.pixel(ox + x + dx, oy + y + dy, colors::kGreen);
        }
    }
}

// --------------------------------------------------------------------------------------------- DrumWave

namespace {

// Line of text of a waveform, in the GEM 6×6 font at the screen's pixels (third DAW test, items 23 and 30, D-222):
// written over the canvas, which is stretched to the widget, at the place and on the baseline it had in the canvas,
// on a white band so that the waveform under it does not bite into the letters. While it is too wide, the clause after
// its last ", " or " / " goes, then the characters that do not fit.
void drawCaption(Painter& p, const Rect& r, const Canvas& canvas, std::string text, int baseline) {
    const Rect in = r.inset(2);
    const double x = in.x + 8.0 * in.w / canvas.width();
    const double advance = Font::advance(10, gemui::Face::kSmall);
    const size_t fit = size_t(std::max(0.0, std::floor((in.right() - 2 - x) / advance)));
    while (text.size() > fit) {
        size_t cut = 0;
        for (const char* sep : {", ", " / "})
            if (const size_t at = text.rfind(sep); at != std::string::npos) cut = std::max(cut, at);
        if (cut == 0) {
            text.resize(fit);
            break;
        }
        text.resize(cut);
    }
    const int y = in.y + int(std::lround(double(baseline) * in.h / canvas.height()));
    // Glyphs: the 6 rows up to the baseline; the band leaves 2 px around them, 1 px under.
    const int left = int(std::floor(x)) - 2;
    const int w = int(std::ceil(x + Font::width(text, 10, 0, gemui::Face::kSmall))) + 1 - left;
    p.fill(Rect{left, y - 7, w, 9}.intersect(in), colors::kWhite);
    p.textAt(text, x, y, 10, colors::kBlack, 0, gemui::Face::kSmall);
}

}  // namespace

DrumWave::DrumWave() { setSample(0, {}); }

void DrumWave::setSample(int slot, std::span<const uint8_t> data, double rateHz) {
    canvas_.clear(colors::kWhite);
    const std::string head = "slot " + std::to_string(slot + 1) + ": ";
    if (data.empty()) {
        // 352 × 8 frame (strokeRect(4, 20, 352, 8)) and prompt. An empty slot plays nothing: the "built-in DrumNoise"
        // text of the prototype was wrong (D-029).
        const int x0 = 4, y0 = 20, x1 = 4 + 352, y1 = 20 + 8;
        canvas_.line(x0, y0, x1, y0, colors::kBlack);
        canvas_.line(x0, y1, x1, y1, colors::kBlack);
        canvas_.line(x0, y0, x0, y1, colors::kBlack);
        canvas_.line(x1, y0, x1, y1, colors::kBlack);
        caption_ = head + "empty";
        baseline_ = 14;
    } else {
        // One value every n / 360 samples (at least every sample), 0-15 mapped from y 46 up to y 2; each value drawn
        // as a 2 px horizontal step, joined to the previous one.
        const int n = int(data.size());
        const int step = std::max(1, n / 360);
        int px = 0, py = 0;
        for (int x = 0; x < 360; ++x) {
            const int v = data[size_t(std::min(n - 1, x * step))];
            const int y = int(std::floor(48 - 2 - v * (48 - 4) / 15.0));
            if (x > 0) canvas_.line(px, py, x, y, colors::kBlack);
            canvas_.line(x, y, x + 1, y, colors::kBlack);
            px = x + 1;
            py = y;
        }
        caption_ = head + std::to_string(n) + " smp @ " + std::to_string(std::lround(rateHz)) + " Hz / 4-bit";
        baseline_ = 10;
    }
    repaint();
}

void DrumWave::paint(Painter& p) {
    const Rect r = bounds();
    p.frame(r, 2, colors::kBlack);
    canvas_.blit(p, r.inset(2));
    drawCaption(p, r, canvas_, caption_, baseline_);
}

// ------------------------------------------------------------------------------------------- DmaWave

DmaWave::DmaWave(bool loads) : loads_(loads) {
    if (loads_) setTip("Click to load a file into this slot");
    setSample(0, {}, 25033, false);
}

// Called at each refresh of the main window or the DMA window: the drawing is redone only when the sample, its slot,
// rate or format changed (the sample is kept for that comparison).
void DmaWave::setSample(int slot, std::span<const int8_t> data, double rateHz, bool stereo) {
    if (slot == slot_ && rateHz == rate_ && stereo == stereo_ && std::equal(data.begin(), data.end(), bytes_.begin(), bytes_.end()))
        return;
    slot_ = slot;
    rate_ = rateHz;
    stereo_ = stereo;
    bytes_.assign(data.begin(), data.end());
    repaint();
    canvas_.clear(colors::kWhite);
    const std::string head = "sample " + std::to_string(slot + 1) + ": ";
    if (data.empty()) {
        const int x0 = 4, y0 = 24, x1 = 4 + 352, y1 = 24 + 8;
        canvas_.line(x0, y0, x1, y0, colors::kBlack);
        canvas_.line(x0, y1, x1, y1, colors::kBlack);
        canvas_.line(x0, y0, x0, y1, colors::kBlack);
        canvas_.line(x1, y0, x1, y1, colors::kBlack);
        caption_ = head + (loads_ ? "empty - click to load a file" : "empty");
        baseline_ = 16;
        return;
    }
    const int ch = stereo ? 2 : 1;
    const size_t frames = data.size() / size_t(ch);
    // One row per channel under the line of text: from 14 to 54 (mono) or two of 20 px.
    const int top = 14, h = 40 / ch;
    // Each column covers frames [a, b) of the sample (at least one) and draws a vertical line from its lowest to its
    // highest byte; ±128 spans half the row less a pixel. Stereo bytes are interleaved, left first.
    for (int c = 0; c < ch; ++c) {
        const int mid = top + c * h + h / 2;
        canvas_.line(0, mid, 359, mid, colors::kHint);
        for (int x = 0; x < 360 && frames > 0; ++x) {
            const size_t a = frames * size_t(x) / 360, b = std::max(a + 1, frames * size_t(x + 1) / 360);
            int lo = 127, hi = -128;
            for (size_t i = a; i < b && i < frames; ++i) {
                const int v = data[i * size_t(ch) + size_t(c)];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            const int ya = mid - int(std::lround(hi * (h / 2 - 1) / 128.0));
            const int yb = mid - int(std::lround(lo * (h / 2 - 1) / 128.0));
            canvas_.line(x, ya, x, yb, colors::kBlack);
        }
    }
    caption_ = head + std::to_string(data.size()) + " bytes, " +
               gemui::formatDecimal("%.2f s", double(frames) / rateHz) + gemui::formatDecimal(" at %.0f Hz, ", rateHz) +
               (stereo ? "stereo" : "mono");
    baseline_ = 10;
}

void DmaWave::paint(Painter& p) {
    const Rect r = bounds();
    p.frame(r, 2, colors::kBlack);
    canvas_.blit(p, r.inset(2));
    drawCaption(p, r, canvas_, caption_, baseline_);
}

void DmaWave::mouseUp(const gemui::MouseEvent& e) {
    if (loads_ && e.button == gemui::MouseButton::Left && bounds().contains(e.pos) && onClick) onClick();
}

// ------------------------------------------------------------------------------------------- DigiWave

DigiWave::DigiWave() {
    setTip("Click to load a file into this slot");
    setSample(0, {}, 7800, false, 0, 0);
}

void DigiWave::setSample(int slot, std::span<const uint8_t> data, double rateHz, bool loop, uint32_t loopStart,
                         uint32_t loopEnd) {
    if (slot == slot_ && rateHz == rate_ && loop == loop_ && loopStart == loopStart_ && loopEnd == loopEnd_ &&
        std::equal(data.begin(), data.end(), nibbles_.begin(), nibbles_.end()))
        return;
    slot_ = slot;
    rate_ = rateHz;
    loop_ = loop;
    loopStart_ = loopStart;
    loopEnd_ = loopEnd;
    nibbles_.assign(data.begin(), data.end());
    repaint();
    canvas_.clear(colors::kWhite);
    const std::string head = "sample " + std::to_string(slot + 1) + ": ";
    if (data.empty()) {
        const int x0 = 4, y0 = 32, x1 = 4 + 352, y1 = 32 + 8;
        canvas_.line(x0, y0, x1, y0, colors::kBlack);
        canvas_.line(x0, y1, x1, y1, colors::kBlack);
        canvas_.line(x0, y0, x0, y1, colors::kBlack);
        canvas_.line(x1, y0, x1, y1, colors::kBlack);
        caption_ = head + "empty - click to load a file";
        baseline_ = 24;
        return;
    }
    const size_t n = data.size();
    // Values from 0 (y 69) to 15 (y 15), 54 px for 15 levels; the loop first, under the sample.
    auto yOf = [](int v) { return 69 - v * 54 / 15; };
    auto xOf = [n](uint32_t i) { return int(std::min<size_t>(359, size_t(i) * 360 / n)); };
    if (loop && loopEnd > loopStart && loopEnd <= n) {
        const int xa = xOf(loopStart), xb = xOf(loopEnd - 1);
        canvas_.line(xa, 14, xa, 70, colors::kHint);
        canvas_.line(xb, 14, xb, 70, colors::kHint);
        canvas_.line(xa, 14, xb, 14, colors::kHint);
    }
    for (int x = 0; x < 360; ++x) {
        const size_t a = n * size_t(x) / 360, b = std::max(a + 1, n * size_t(x + 1) / 360);
        int lo = 15, hi = 0;
        for (size_t i = a; i < b && i < n; ++i) {
            lo = std::min<int>(lo, data[i]);
            hi = std::max<int>(hi, data[i]);
        }
        if (a >= n) break;
        canvas_.line(x, yOf(hi), x, yOf(lo), colors::kBlack);
    }
    caption_ = head + std::to_string(n) + " values, " + gemui::formatDecimal("%.2f s", double(n) / rateHz) +
               gemui::formatDecimal(" at %.0f Hz", rateHz) +
               (loop && loopEnd > loopStart && loopEnd <= n ? ", loop" : "");
    baseline_ = 10;
}

void DigiWave::paint(Painter& p) {
    const Rect r = bounds();
    p.frame(r, 2, colors::kBlack);
    canvas_.blit(p, r.inset(2));
    drawCaption(p, r, canvas_, caption_, baseline_);
}

void DigiWave::mouseUp(const gemui::MouseEvent& e) {
    if (e.button == gemui::MouseButton::Left && bounds().contains(e.pos) && onClick) onClick();
}

// ------------------------------------------------------------- Rule, LearnBadge, Layer, Paragraph: after SidWaveGrid

// ------------------------------------------------------------------------------------------------ SidWaveGrid

namespace {
constexpr double kWaveDeadAlpha = 0.3;  // like #orngrid
constexpr Color kWaveBorder = gemui::rgb(0x99, 0x99, 0x99);
constexpr const char* kWaveHex = "0123456789ABCDEF";
}  // namespace

void SidWaveGrid::setState(const State& s) {
    if (s == st_) return;
    st_ = s;
    repaint();
}

Rect SidWaveGrid::cellRect(int step) const noexcept {
    const Rect r = bounds();
    return {r.x + step * (kCellW + kGap), r.y, kCellW, kBarH};
}

void SidWaveGrid::paint(Painter& p) {
    const Rect r = bounds();
    // Baseline of the digits: centred in the kDigitH strip under the bars.
    const Font::LineBox lb = Font::lineBox("0", 9);
    const int baseline = snap(r.y + kBarH + kGap + std::floor((kDigitH - lb.height()) / 2) + lb.ascent);
    for (int c = 0; c < kSteps; ++c) {
        const bool dead = c >= st_.length;
        auto shade = [dead](Color k) { return dead ? gemui::mix(k, colors::kWhite, kWaveDeadAlpha) : k; };
        const Rect cr = cellRect(c);
        p.fill(cr, colors::kWhite);
        p.frame(cr, 1, shade(kWaveBorder));
        const int v = std::min<int>(st_.wave[size_t(c)], 15);
        const Rect in{cr.x + 1, cr.y + 1, cr.w - 2, cr.h - 2};
        if (v > 0) p.fill({in.x, in.bottom() - 3 * v, in.w, 3 * v}, shade(colors::kBlack));
        // Volume in hexadecimal under the column (centred 6×6 text).
        const std::string t(1, kWaveHex[v]);
        const double dw = Font::width(t, 9, 0, gemui::Face::kSmall);
        p.textAt(t, cr.x + (cr.w - dw) / 2, baseline, 9, shade(colors::kBlack), 0, gemui::Face::kSmall);
    }
}

// Column under the point, the digit strip included; the 1 px gaps between columns belong to none.
int SidWaveGrid::stepAt(Point p) const noexcept {
    const Rect r = bounds();
    const int rx = int(std::floor(p.x)) - r.x;
    const int ry = int(std::floor(p.y)) - r.y;
    if (rx < 0 || ry < 0 || ry >= kHeight) return -1;
    const int c = rx / (kCellW + kGap);
    if (c >= kSteps || rx % (kCellW + kGap) >= kCellW) return -1;
    return c;
}

int SidWaveGrid::valueAt(double y) const noexcept {
    // 3 px per notch from the bottom of the bar: the top of the column is 15.
    const Rect r = bounds();
    const double fromBottom = (r.y + kBarH - 1) - y;
    return std::clamp(int(std::ceil(fromBottom / 3)), 0, 15);
}

void SidWaveGrid::mouseDown(const gemui::MouseEvent& e) {
    const int c = stepAt(e.pos);
    if (c < 0) return;
    if (e.button == gemui::MouseButton::Right) {  // its slider (D-285), or the step returns to 0
        if (onMenu) onMenu(c, cellRect(c));
        else if (onSet) onSet(c, 0);
        return;
    }
    if (e.button != gemui::MouseButton::Left) return;
    painting_ = true;
    if (onSet) onSet(c, valueAt(e.pos.y));
}

void SidWaveGrid::mouseDrag(const gemui::MouseEvent& e) {
    if (!painting_) return;
    const int c = stepAt(e.pos);
    if (c < 0) return;
    const int v = valueAt(e.pos.y);
    if (v != st_.wave[size_t(c)] && onSet) onSet(c, v);
}

void SidWaveGrid::mouseUp(const gemui::MouseEvent&) { painting_ = false; }

bool SidWaveGrid::mouseWheel(const gemui::WheelEvent& e) {
    const int c = stepAt(e.pos);
    if (c < 0 || e.dy == 0) return false;
    const int v = std::clamp(st_.wave[size_t(c)] + (e.dy < 0 ? 1 : -1), 0, 15);
    if (v == st_.wave[size_t(c)]) return true;
    if (onWheel) onWheel(c, v);
    else if (onSet) onSet(c, v);
    return true;
}

gemui::Pointer SidWaveGrid::pointerAt(Point p) const noexcept {
    return stepAt(p) >= 0 ? gemui::Pointer::Hand : gemui::Pointer::Arrow;
}

std::string SidWaveGrid::tipAt(Point p) const {
    const int c = stepAt(p);
    if (c < 0) return {};
    const std::string head = "Step " + std::to_string(c + 1) + ": volume " + std::to_string(st_.wave[size_t(c)]);
    if (st_.length == 0) return head + ". Square wave: set Steps to 2 or more to play this wave.";
    if (c >= st_.length) return head + " (after the wave end). Drag to draw, wheel to adjust.";
    return head + ". Drag to draw, wheel to adjust, right click for a slider.";
}

void Rule::paint(Painter& p) { p.fill(bounds(), colors::kBlack); }

LearnBadge::LearnBadge() {
    setOnTop(true);
    setVisible(false);
    add(label_);
}

// Sized to its text, 8 px from the left and bottom edges of the desktop, and brought in front of the windows.
void LearnBadge::show(std::string text, int deskH) {
    constexpr int kBorder = 2, kPadY = 4, kPadX = 10;
    label_.setText(std::move(text));
    const int w = int(std::ceil(label_.naturalWidth())) + 2 * (kBorder + kPadX);
    const int h = label_.naturalHeight() + 2 * (kBorder + kPadY);
    setBounds({8, deskH - 8 - h, w, h});
    label_.setBounds({8 + kBorder + kPadX, deskH - 8 - h + kBorder + kPadY, w - 2 * (kBorder + kPadX),
                      label_.naturalHeight()});
    setVisible(true);
    toFront();
    repaint();
}

void LearnBadge::paint(Painter& p) {
    p.fill(bounds(), colors::kWhite);
    p.frame(bounds(), 2, colors::kBlack);
}

void Layer::setOpacity(double a) {
    if (a == opacity_) return;
    opacity_ = a;
    repaint();
}

void Paragraph::setText(std::string t) {
    if (t == text_) return;
    text_ = std::move(t);
    repaint();
}

void Paragraph::setLineHeight(double h) {
    lineHeight_ = h;
    repaint();
}

std::vector<std::string> Paragraph::lines() const {
    // Chromium line breaks: at spaces (removed at the end of a line), after a hyphen between two letters ("per-step")
    // and at line breaks (<br>).
    // The font is monospaced: the width is counted in characters. Each paragraph between two "\n" is cut into
    // segments (up to a space, or up to and including such a hyphen), added to the line while they fit.
    const int maxChars = std::max(1, int(box().w / Font::advance(12, gemui::Face::kSmall)));
    auto letter = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; };
    std::vector<std::string> out;
    size_t from = 0;
    while (from <= text_.size()) {
        const size_t nl = std::min(text_.find('\n', from), text_.size());
        const std::string_view para = std::string_view(text_).substr(from, nl - from);
        std::string line, sep;
        const size_t n = para.size();
        size_t pos = 0;
        while (pos < n) {
            size_t end = pos;
            bool space = false;
            for (; end < n; ++end) {
                if (para[end] == ' ') {
                    space = true;
                    break;
                }
                if (para[end] == '-' && end > pos && letter(para[end - 1]) && end + 1 < n && letter(para[end + 1])) {
                    ++end;
                    break;
                }
            }
            const std::string seg(para.substr(pos, end - pos));
            const std::string tryLine = line + sep + seg;
            if (!line.empty() && Font::length(tryLine) > maxChars) {
                out.push_back(line);
                line = seg;
            } else {
                line = tryLine;
            }
            sep = space ? " " : "";
            pos = space ? end + 1 : end;
        }
        if (!line.empty()) out.push_back(line);
        from = nl + 1;
    }
    return out;
}

double Paragraph::naturalWidth() const {
    // Width without wrapping (max-content): the longest line between two breaks.
    double w = 0;
    size_t from = 0;
    while (from <= text_.size()) {
        const size_t nl = std::min(text_.find('\n', from), text_.size());
        w = std::max(w, Font::width(std::string_view(text_).substr(from, nl - from), 12, 0, gemui::Face::kSmall));
        from = nl + 1;
    }
    return w;
}

int Paragraph::lineCount() const { return int(lines().size()); }

void Paragraph::paint(Painter& p) {
    // 12 px text centred in each line (half-leading of the browser).
    const auto ls = lines();
    const double lead = (lineHeight_ - 12) / 2;
    for (size_t i = 0; i < ls.size(); ++i)
        p.text(ls[i], box().x, box().y + lead + lineHeight_ * double(i), 12, colors::kHint, 0, gemui::Face::kSmall);
}

}  // namespace stym::pluginui
