// STYM2149 — pluginui: YM PLAYER window (#ymWin of the prototype)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/YmPlayerWindow.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::Rect;

namespace {

constexpr double kPad = 8, kGap = 8;  // padding of the content and gap between the items of a row (px)

// Notes of the prototype (register dump) and of STYM2149 (68000 routine of an SNDH).
constexpr const char* kHintYm = "Register stream played at the VBL rate. The register strip and scope follow.";
constexpr const char* kHintSndh = "68000 replay routine played at its timer rate. The register strip and scope follow.";

// textContent of the prototype shown with white-space normal: each run of spaces collapses to a single one.
std::string collapsed(std::string_view s) {
    std::string out;
    for (char c : s)
        if (c != ' ' || out.empty() || out.back() != ' ') out += c;
    return out;
}

}  // namespace

std::string YmPlayerWindow::positionText(uint32_t position, uint32_t frames, int rate) {
    // ymSetPos: (pos + 1) / frames   (frames / rate, one decimal), the three spaces collapsed to one by the page;
    // unknown duration of an SNDH: "?".
    std::string s = std::to_string(position + 1) + " / ";
    if (frames == 0) return s + "? (? s)";
    const double secs = double(frames) / double(std::max(rate, 1));
    return s + std::to_string(frames) + " (" + gemui::formatDecimal("%.1f", secs) + " s)";
}

YmPlayerWindow::YmPlayerWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), double(kW), 100});  // provisional height: layout() sets the real one
}

void YmPlayerWindow::build() {
    setTitle("YM PLAYER");
    play_.setTip("Play the loaded YM tune");
    rew_.setTip("Back to the start of the tune");
    subPrev_.setTip("Previous subsong (restarts the tune)");
    subNext_.setTip("Next subsong (restarts the tune)");
    hint_.setText(kHintYm);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&name_, &play_, &rew_, &pos_, &labSub_, &subPrev_,
                                                                  &subv_, &subNext_, &hint_})
        add(*w);
}

void YmPlayerWindow::wire() {
    // #ymClose: playback stops; the tune is forgotten (the prototype reopens it only by loading).
    onClose = [this] {
        m_.host_.ymClose();
        last_ = {};
    };
    // The buttons change last_ at once, so that the window answers before poll reads the host's state again.
    play_.onClick = [this] {
        const bool on = !last_.playing;
        m_.host_.ymPlay(on);
        last_.playing = on;
        refresh();
    };
    rew_.onClick = [this] {
        m_.host_.ymRewind();
        last_.playing = true;
        last_.position = 0;
        refresh();
    };
    auto step = [this](int d) {
        const int s = std::clamp(last_.subsong + d, 1, std::max(1, last_.subsongs));
        if (s == last_.subsong) return;
        m_.host_.ymSubsong(s);
        last_.subsong = s;
        last_.position = 0;
        refresh();
    };
    subPrev_.onClick = [step] { step(-1); };
    subNext_.onClick = [step] { step(1); };
}

void YmPlayerWindow::show() {
    last_ = m_.host_.ymPlayer();
    if (!last_.loaded) return;
    open();
    refresh();
}

void YmPlayerWindow::poll() {
    if (!isOpen()) return;
    Host::YmPlayer y = m_.host_.ymPlayer();
    if (!y.loaded) {
        close();
        return;
    }
    // Position sent every 8 frames by the prototype (ymstate): it advances in steps of 8. The registers change at
    // every frame and are not shown here: left out of the comparison.
    y.position &= ~7u;
    y.regs = {};
    if (y == last_) return;
    last_ = y;
    refresh();
}

void YmPlayerWindow::refresh() {
    if (!isOpen()) return;
    name_.setText(collapsed(last_.name));
    hint_.setText(last_.sndh ? kHintSndh : kHintYm);
    // toggleBtn(#ymPlay): button lit and "Stop" during playback.
    play_.setOn(last_.playing);
    play_.setText(last_.playing ? "Stop" : "Play");
    pos_.setText(positionText(last_.position, last_.frames, last_.rate));
    const bool subs = last_.sndh && last_.subsongs > 1;
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labSub_, &subPrev_, &subv_, &subNext_})
        w->setVisible(subs);
    subv_.setText(std::to_string(last_.subsong) + " / " + std::to_string(last_.subsongs));
    subPrev_.setEnabled(last_.subsong > 1);
    subNext_.setEnabled(last_.subsong < last_.subsongs);
    layout();
}

// ------------------------------------------------------------------------------------------- layout

void YmPlayerWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // .mixbody content (border, padding 8)
    const double contentW = kW - 12 - 2 * kPad;
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto lab = [](gemui::Label& l) { return FlexItem{&l, l.naturalWidth(), 16}; };

    // #ymName (.hint, margin-top 6), wrapped within the content width: given that width first, it counts its lines.
    double y = oy + 32 + kPad + 6;  // below the frame (6) and the title bar (26)
    name_.setBox({x, y, contentW, 12});
    const double nameH = 12.0 * name_.lineCount();
    name_.setBox({x, y, contentW, nameH});
    y += nameH;
    // .row (margin-top 8, margin-bottom 6, gap 8): Play, Rewind, #ymPos.
    y += 8;
    std::vector<FlexItem> row = {btn(play_), btn(rew_), lab(pos_)};
    y += flexRow(x, y, contentW, row, kGap);
    // Sub-tune of an SNDH (STYM2149): same kind of row.
    if (labSub_.visible()) {
        y += 6;
        std::vector<FlexItem> sub = {lab(labSub_), btn(subPrev_), lab(subv_), btn(subNext_)};
        y += flexRow(x, y, contentW, sub, kGap);
    }
    // Note (.hint, margin-top 6; the row's 6 merges into it).
    y += 6;
    hint_.setBox({x, y, contentW, 12});
    const double hintH = 12.0 * hint_.lineCount();
    hint_.setBox({x, y, contentW, hintH});
    y += hintH;

    // Height: content, padding of 8, border and margin (6); the width stays kW.
    const double h = y + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01) place({ox, oy, double(kW), h});
    repaint();
}

std::optional<Rect> YmPlayerWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "ymWin") return bounds();
    if (id == "ymPlay") return play_.bounds();
    if (id == "ymRew") return rew_.bounds();
    if (id == "ymPos") return pos_.bounds();
    if (id == "ymName") return name_.bounds();
    if (labSub_.visible()) {
        if (id == "ymSubPrev") return subPrev_.bounds();
        if (id == "ymSubNext") return subNext_.bounds();
    }
    return std::nullopt;
}

}  // namespace stym::pluginui
