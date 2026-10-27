// STYM2149 — pluginui: Recorder window (register recorder)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/RecorderWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "gemui/Alert.h"
#include "plugincore/Parameters.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::Face;
using gemui::Font;
using gemui::Rect;
using plugin::Global;
namespace colors = gemui::colors;

namespace {

constexpr double kPad = 8, kGap = 8;  // padding of the content and gap between the items of a row (px)

// Export row: YM6, SNDH and VBL dump.
constexpr std::array<plugin::RegisterFormat, 3> kExports = {plugin::RegisterFormat::Ym6, plugin::RegisterFormat::Sndh,
                                                            plugin::RegisterFormat::Vbl};
constexpr std::array<const char*, 3> kExportNames = {"YM6", "SNDH", "VBL"};
constexpr std::array<const char*, 3> kExportTips = {"Export the take as a compressed YM6 file",
                                                    "Export the take as an SNDH file for the Atari ST",
                                                    "Export the take as a VBL register dump (assembler source)"};

}  // namespace

// `frames` replay ticks at `hz` ticks per second, in hundredths of a second rounded to the nearest (64-bit: no
// overflow of frames × 100), then written m:ss.cc.
std::string RecorderWindow::duration(uint32_t frames, uint32_t hz) {
    const uint64_t cs = hz ? (uint64_t(frames) * 100 + hz / 2) / hz : 0;  // hundredths, rounded
    char buf[32];
    std::snprintf(buf, sizeof buf, "%u:%02u.%02u", unsigned(cs / 6000), unsigned(cs / 100 % 60), unsigned(cs % 100));
    return buf;
}

RecorderWindow::RecorderWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 300, 90});  // provisional size: layout() sets the real one at the first refresh
}

void RecorderWindow::build() {
    setTitle("RECORDER");
    for (gemui::Label* l : {&hint_, &hint2_}) {
        l->setFace(Face::kSmall);
        l->setColor(colors::kHint);
    }
    for (int k = 0; k < 2; ++k) {
        Button& b = chips_[size_t(k)];
        b.setText(std::to_string(k + 1));
        b.setPadding(2, 6);
    }
    chips_[0].setTip("Record the first chip");
    rec_.setTip("Arm a take: it starts and stops with the host's playback (click again to stop)");
    clear_.setTip("Delete the take saved with the project");
    for (size_t k = 0; k < exports_.size(); ++k) {
        exports_[k].setText(kExportNames[k]);
        exports_[k].setTip(kExportTips[k]);
    }
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&status_, &hint_, &hint2_, &sep_, &labChip_, &chips_[0],
                                                                  &chips_[1], &rec_, &clear_, &labTake_, &takev_,
                                                                  &labExport_, &exports_[0], &exports_[1], &exports_[2]})
        add(*w);
}

void RecorderWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };
    for (int k = 0; k < 2; ++k)
        chips_[size_t(k)].onClick = [this, k] {
            chip_ = k;
            refresh();
        };
    // Rec: idle, arms a take on the chosen chip; armed or rolling, stops it. The state is read back at once.
    rec_.onClick = [this] {
        if (m_.host_.recorder().state == Host::Recorder::State::Idle) m_.host_.armRecorder(chip_);
        else m_.host_.disarmRecorder();
        poll();
    };
    clear_.onClick = [this] { confirmClear(); };
    for (size_t k = 0; k < exports_.size(); ++k) exports_[k].onClick = [this, k] { m_.exportTakeFile(kExports[k]); };
}

void RecorderWindow::confirmClear() {
    gemui::Alert::Spec spec;
    spec.lines = {{"Delete the recorded take?"}, {"It cannot be undone."}};
    spec.confirmButtons("Delete");  // the safest choice (Cancel) is the default
    gemui::Alert::open(m_.desk_, std::move(spec), [this](int button, const std::string&) {
        if (button != gemui::Alert::kConfirmOk) return;
        m_.host_.clearRecording();
        poll();
    });
}

void RecorderWindow::show() {
    open();
    last_ = m_.host_.recorder();
    refresh();
    m_.syncWindowButtons();
}

void RecorderWindow::poll() {
    if (!isOpen()) return;
    const Host::Recorder r = m_.host_.recorder();
    if (r == last_) return;
    last_ = r;
    refresh();
}

void RecorderWindow::refresh() {
    if (!isOpen()) return;
    using State = Host::Recorder::State;
    const Host::Recorder& r = last_;
    const bool regs = m_.mode3();
    const bool dual = m_.chips() > 1;
    if (!dual) chip_ = 0;
    // Idle, the chip buttons show the choice for the next take; armed or rolling, the take's chip, locked.
    const int shown = r.state == State::Idle ? chip_ : r.chip;
    for (int k = 0; k < 2; ++k) {
        chips_[size_t(k)].setOn(k == shown);
        chips_[size_t(k)].setEnabled(r.state == State::Idle && (k == 0 || dual));
    }
    chips_[1].setTip(dual ? "Record the second chip" : "Dual chip (2x3ch) only");
    // A take rolling is timed at the current replay rate (VBL Rate parameter); a kept take at its own (takeHz).
    const int vbl = plugin::kVblRates[size_t(std::clamp(m_.iv(Global::VblRate), 0, int(plugin::kVblRates.size()) - 1))];
    const char* who = m_.host_.standalone() ? "the sequencer plays" : "the host plays";
    std::string text;
    if (r.state == State::Rolling)
        text = "Recording chip " + std::to_string(r.chip + 1) + ": " + duration(r.frames, uint32_t(vbl)) + ", " +
               std::to_string(r.frames) + " frames.";
    else if (!regs)
        text = "16 ch has no YM registers: switch to 3ch or 2x3ch.";
    else if (r.state == State::Armed)
        text = std::string("Armed: the take starts when ") + who + ".";
    else
        text = "Ready.";
    status_.setText(text);
    rec_.setOn(r.state != State::Idle);
    rec_.setEnabled(regs || r.state != State::Idle);  // in 16 ch, still able to stop a take armed before
    takev_.setText(r.takeFrames ? std::to_string(r.takeFrames) + " frames, " + duration(r.takeFrames, r.takeHz) +
                                      " at " + std::to_string(r.takeHz) + " Hz, chip " + std::to_string(r.takeChip + 1)
                                : std::string("none"));
    clear_.setEnabled(r.takeFrames > 0);
    for (Button& b : exports_) b.setEnabled(r.takeFrames > 0);
    layout();
}

// ------------------------------------------------------------------------------------------- layout

void RecorderWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // content (border 2, margin 4, padding 8)
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto lab = [](gemui::Label& l, double w = 0) { return FlexItem{&l, w > 0 ? w : l.naturalWidth(), 16}; };

    // Fixed width: the status text changes during the take without moving the window.
    const double col = std::max({364.0, hint_.naturalWidth(), hint2_.naturalWidth()});
    double y = oy + 32 + kPad;  // below the frame (6) and the title bar (26)
    status_.setBox({x, y, col, 16});
    y += 16 + 6;
    hint_.setBox({x, y, hint_.naturalWidth(), 12});
    y += 12;
    hint2_.setBox({x, y, hint2_.naturalWidth(), 12});
    y += 12 + 8;
    sep_.setBox({x - kPad, y, col + 2 * kPad, 2});
    y += 2 + 8;

    const double lw = std::max({Font::width("Chip", 14), Font::width("Take", 14), Font::width("Export", 14)});
    std::vector<FlexItem> ch = {lab(labChip_, lw), btn(chips_[0]), btn(chips_[1])};
    std::vector<FlexItem> act = {btn(rec_), btn(clear_)};
    act.front().marginLeft = 24;  // Rec and Clear on the chip row, set apart from the chip buttons
    for (FlexItem& f : act) ch.push_back(f);
    y += flexRow(x, y, col, ch, 4, false) + 6;
    std::vector<FlexItem> tk = {lab(labTake_, lw), lab(takev_)};
    y += flexRow(x, y, col, tk, kGap, false) + 6;
    std::vector<FlexItem> ex = {lab(labExport_, lw), btn(exports_[0]), btn(exports_[1]), btn(exports_[2])};
    y += flexRow(x, y, col, ex, 4, false);

    // Window: content, padding of 8, border and margin (6 on each side); placed again only when its size changes.
    const double w = col + 2 * kPad + 12;
    const double h = y + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

std::optional<Rect> RecorderWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "recWin") return bounds();
    if (id == "recChip0") return chips_[0].bounds();
    if (id == "recChip1") return chips_[1].bounds();
    if (id == "recRec") return rec_.bounds();
    if (id == "recClear") return clear_.bounds();
    for (size_t k = 0; k < exports_.size(); ++k)
        if (id == std::string("rec") + (k == 0 ? "Ym6" : k == 1 ? "Sndh" : "Vbl")) return exports_[k].bounds();
    return std::nullopt;
}

}  // namespace stym::pluginui
