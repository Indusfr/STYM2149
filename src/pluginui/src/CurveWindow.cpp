// STYM2149 — pluginui: Slide Curve window ("Custom" curve of the glissandos)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype: buildCurveGrid, setCurve, refreshCurveWarn, refreshCurve, handlers of
// #curveReset, #curveSmooth, #curveInvert and #curveClose, openCurveWin.
#include "pluginui/CurveWindow.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Rect;
namespace colors = gemui::colors;

namespace {

constexpr double kGap = 8, kPad = 8, kHintTop = 6;  // gap of the buttons, padding of .mixbody, margin-top of a .hint
constexpr int kN = CurveGrid::kPoints;

}  // namespace

// ------------------------------------------------------------------------------------------- CurveGrid

void CurveGrid::setTable(const std::array<float, kPoints>& t) {
    if (t == table_) return;
    table_ = t;
    repaint();
}

Rect CurveGrid::columnRect(int i) const noexcept {
    const Rect r = bounds();
    return {r.x + kBorder + i * kColW, r.y + kBorder, kColW, kColH};
}

void CurveGrid::paint(gemui::Painter& p) {
    const Rect r = bounds();
    p.fill(r, colors::kBlack);
    p.fill({r.x + kBorder, r.y + kBorder, kPoints * kColW, kColH}, colors::kWhite);
    for (int i = 0; i < kPoints; ++i) {
        const Rect c = columnRect(i);
        p.fill({c.right() - 1, c.y, 1, c.h}, i % 8 == 7 ? kLine8 : kLine);  // .cc:nth-child(8n)
        // .fillv: height in % of the column, rounded down to 1/64 of a pixel then rounded to the pixel like the browser
        // (top edge at the nearest pixel, bottom stuck to the bottom of the column).
        const double h = std::floor(double(std::clamp(table_[size_t(i)], 0.0f, 1.0f)) * kColH * 64) / 64;
        const int top = int(std::floor(c.bottom() - h + 0.5));
        if (top < c.bottom()) p.fill({c.x, top, c.w - 1, c.bottom() - top}, colors::kBlack);
    }
}

int CurveGrid::columnAt(gemui::Point p) const noexcept {
    // Column under the pointer, rule included; the border of the grid is not part of it.
    const Rect r = bounds();
    const int rx = p.x - (r.x + kBorder), ry = p.y - (r.y + kBorder);
    if (rx < 0 || ry < 0 || rx >= kPoints * kColW || ry >= kColH) return -1;
    return rx / kColW;
}

void CurveGrid::draw(gemui::Point p) {
    // lvlAt: 1 at the top of the column, 0 at the bottom.
    const int i = columnAt(p);
    if (i < 0 || !onDraw) return;
    const double lvl = std::clamp(1 - double(p.y - (bounds().y + kBorder)) / kColH, 0.0, 1.0);
    onDraw(i, float(lvl));
}

void CurveGrid::mouseDown(const gemui::MouseEvent& e) {
    // pointerdown on a column (left button): drawing starts; a press on the border draws nothing.
    if (e.button != gemui::MouseButton::Left || columnAt(e.pos) < 0) return;
    painting_ = true;
    draw(e.pos);
}

void CurveGrid::mouseDrag(const gemui::MouseEvent& e) {
    // pointerover while drawing: each column passed over takes the height of the pointer.
    if (painting_) draw(e.pos);
}

void CurveGrid::mouseUp(const gemui::MouseEvent&) { painting_ = false; }

gemui::Pointer CurveGrid::pointerAt(gemui::Point p) const noexcept {
    return columnAt(p) >= 0 ? gemui::Pointer::Crosshair : gemui::Pointer::Arrow;  // .cc: cursor:crosshair
}

// ------------------------------------------------------------------------------------------- CurveWindow

CurveWindow::CurveWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 593, 238});
}

void CurveWindow::build() {
    // Prototype ids of the sliders and buttons that MIDI learn can learn (D-146).
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&reset_, "curveReset"}, {&smooth_, "curveSmooth"}, {&invert_, "curveInvert"}})
        w->setId(id);
    setTitle("SLIDE CURVE");  // #curveTrk stays empty: the curve is shared by all tracks
    closesOnEscape = false;   // not in the Escape CLOSERS
    reset_.setTip("Reset the custom curve to a straight line");
    smooth_.setTip("Smooth out a jagged curve");
    invert_.setTip("Turn the whole curve upside down: the slide runs from the target note back to the start");
    // 12 px .hint: 6×6 font, at the width of the prototype's 12 px font (D-071).
    for (gemui::Label* l : {&hint_, &warn_, &foot_}) {
        l->setFace(gemui::Face::kSmall);
        l->setColor(colors::kHint);
    }
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&reset_, &smooth_, &invert_, &hint_, &sep_, &grid_,
                                                                   &warn_, &foot_})
        add(*w);
}

void CurveWindow::wire() {
    reset_.onClick = [this] {
        edit([](std::array<float, kN>& t) { t = ymengine::GlobalParams{}.slideTable; });  // straight line by default
    };
    smooth_.onClick = [this] {
        // Moving average (1, 2, 1) over the inner points; the two ends do not move.
        edit([](std::array<float, kN>& t) {
            const std::array<float, kN> o = t;
            for (size_t i = 1; i + 1 < size_t(kN); ++i) t[i] = (o[i - 1] + 2 * o[i] + o[i + 1]) / 4;
        });
    };
    invert_.onClick = [this] {
        // The whole curve upside down (sixth DAW test, item 65, D-263): the prototype's point symmetry left the
        // straight parts where they were, so only the drawn parts seemed to change.
        edit([](std::array<float, kN>& t) {
            for (float& v : t) v = 1 - v;
        });
    };
    grid_.onDraw = [this](int i, float v) {
        if (table()[size_t(i)] == v) return;  // same height: nothing to publish
        edit([i, v](std::array<float, kN>& t) { t[size_t(i)] = v; });
    };
}

const std::array<float, CurveGrid::kPoints>& CurveWindow::table() const noexcept {
    return m_.extras_.global.slideTable;
}

// The curve is global non-automatable state: changed through the host (published to the audio thread), then read
// back, which refreshes this window. No undo step (D-130).
void CurveWindow::edit(const std::function<void(std::array<float, CurveGrid::kPoints>&)>& fn) {
    m_.host_.editExtras([&fn](plugin::NonParamState& s) { fn(s.global.slideTable); });
    m_.pullExtras();
}

void CurveWindow::show() {
    // openCurveWin: the window comes to the front.
    open();
    refresh();
}

void CurveWindow::refresh() {
    // refreshCurve, refreshCurveWarn; also after a change of the state coming from elsewhere (preset, undo), which the
    // prototype did not carry into an open window (D-130).
    if (!isOpen()) return;
    const auto& t = table();
    grid_.setTable(t);
    const double last = t[size_t(kN) - 1];
    const bool off = std::abs(last - 1) > 0.02;  // the curve misses the target note
    warn_.setVisible(off);
    if (off)
        warn_.setText("Ends at " + std::to_string(int(std::floor(last * 100 + 0.5))) +
                      "% of the interval: the slide stops short of the target note.");
    layout();
}

void CurveWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // .mixbody content (border 2, margin 4, padding 8)
    double y = oy + 32 + kPad;

    // Row of buttons (flex, gap 8, centred); the .hint (margin-top 6) has a 14 px line because of the em dash, hence
    // its letters 3 px below the top of its box.
    double bx = x;
    for (gemui::Button* b : {&reset_, &smooth_, &invert_}) {
        b->setBox({bx, y, b->naturalWidth(), double(b->naturalHeight())});
        bx += b->naturalWidth() + kGap;
    }
    hint_.setBox({bx, y + 9, hint_.naturalWidth(), 12});
    const double contentW = std::max({bx + hint_.naturalWidth() - x, double(CurveGrid::kWidth),
                                      warn_.visible() ? warn_.naturalWidth() : 0.0, foot_.naturalWidth()});
    y += 24 + 8;
    // 2 px rule across the whole width of .mixbody (margin 8 -8), then the grid.
    sep_.setBox({ox + 6, y, contentW + 2 * kPad, 2});
    y += 2 + 8;
    grid_.setBox({x, y, double(CurveGrid::kWidth), double(CurveGrid::kHeight)});
    y += CurveGrid::kHeight;
    if (warn_.visible()) {
        warn_.setBox({x, y + kHintTop, warn_.naturalWidth(), 12});
        y += kHintTop + 12;
    }
    foot_.setBox({x, y + kHintTop, foot_.naturalWidth(), 12});
    const double bottom = y + kHintTop + 12;

    // Window: content, padding of 8, border and margin of .gwin (6 on each side).
    const double w = contentW + 2 * kPad + 12;
    const double h = bottom + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

std::optional<Rect> CurveWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "curveWin") return bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"curveReset", &reset_}, {"curveSmooth", &smooth_}, {"curveInvert", &invert_},
                           {"curvegrid", &grid_},   {"curveWarn", &warn_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

}  // namespace stym::pluginui
