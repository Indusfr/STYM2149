// STYM2149 — pluginui: sequences of the Modulation window (PER, NP, MIX, GEN, FIX rows; pitch settings; vibrato table)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/SequencesPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <vector>

#include "gemui/Desktop.h"
#include "plugincore/Instruments.h"
#include "plugincore/Mapping.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Pitch.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::Color;
using gemui::Face;
using gemui::Font;
using gemui::MenuItem;
using gemui::Rect;
using ymengine::PitchSource;
using ymengine::PitchTarget;
using Row = SeqRowsGrid::Row;
namespace colors = gemui::colors;

namespace {

constexpr double kGap = 8;
constexpr double kDeadAlpha = 0.3;  // like #orngrid
constexpr Color kBorder = gemui::rgb(0x99, 0x99, 0x99), kZero = gemui::rgb(0xDD, 0xDD, 0xDD);
constexpr std::array<const char*, SeqRowsGrid::kRowCount> kRowNames = {"PER", "NP", "MIX", "GEN", "FIX", "OUT"};
// Channel modes (YM-Tracker, Y40) and generators of the GEN row.
constexpr std::array<const char*, ymengine::kChannelModes> kModes = {
    "Off", "Tone", "Noise", "Tone+Noise", "Env", "Tone+Env", "Noise+Env", "Tone+Noise+Env"};
constexpr std::array<const char*, ymengine::kSeqGens> kGenNames = {"Note's generator", "Chip Voice", "SID Voice",
                                                                   "Sync Buzzer"};
constexpr std::array<const char*, ymengine::kSeqGens> kGenChars = {"", "C", "S", "B"};  // drawn in a GEN cell
// Pitch matrix: button labels and tooltip names of the targets (columns) and of the sources (rows).
constexpr std::array<const char*, 3> kTargets = {"Sq", "Tm", "Bz"};
constexpr std::array<const char*, 3> kTargetNames = {"the square (tone period)", "the timer (SID, Sync Buzzer)",
                                                     "the envelope period (buzzer)"};
constexpr std::array<const char*, 3> kSourceNames = {"Table pitch (ARP, PER, FIX)", "Slides", "Vibrato"};

// Baseline of a line of text of `px` in a line box of `lineH`, as ModulationWindow.cpp computes it.
int baselineIn(double lineTop, double lineH, int px) {
    const Font::LineBox lb = Font::lineBox("0", px);
    return gemui::snap(lineTop + std::floor((lineH - lb.height()) / 2) + lb.ascent);
}

std::string signedText(int v) { return v > 0 ? "+" + std::to_string(v) : std::to_string(v); }

}  // namespace

// ------------------------------------------------------------------------------------------- SeqRowsGrid

void SeqRowsGrid::setState(const State& s) {
    if (s == st_) return;
    st_ = s;
    repaint();
}

// Heights of the rows above, each followed by a 1 px gap.
int SeqRowsGrid::rowTop(Row row) const noexcept {
    int y = 0;
    for (int k = 0; k < int(row); ++k) y += kRowH[size_t(k)] + kGap;
    return y;
}

Rect SeqRowsGrid::cellRect(Row row, int step) const noexcept {
    const Rect r = bounds();
    return {r.x + kLabelW + kGap + step * (kCellW + kGap), r.y + rowTop(row), kCellW, kRowH[size_t(row)]};
}

int SeqRowsGrid::value(const ymengine::SeqRows& r, Row row, int step) noexcept {
    const size_t s = size_t(step);
    switch (row) {
    case Row::Period: return r.period[s];
    case Row::Noise: return r.noise[s];
    case Row::Mix: return r.mix[s];
    case Row::Gen: return r.gen[s];
    case Row::Fix: return r.fix[s];
    case Row::Out: return 0;
    }
    return 0;
}

int SeqRowsGrid::minValue(Row row) noexcept { return row == Row::Period ? -ymengine::kPeriodDeltaMax : 0; }

int SeqRowsGrid::maxValue(Row row) noexcept {
    switch (row) {
    case Row::Period: return ymengine::kPeriodDeltaMax;
    case Row::Noise: return 31;
    case Row::Mix: return ymengine::kChannelModes;
    case Row::Gen: return ymengine::kSeqGens - 1;
    case Row::Fix: return 1;
    case Row::Out: return 0;
    }
    return 0;
}

void SeqRowsGrid::paint(gemui::Painter& p) {
    const Rect r = bounds();
    auto shadeOf = [](bool dead) {
        return [dead](Color k) { return dead ? gemui::mix(k, colors::kWhite, kDeadAlpha) : k; };
    };
    // Small 6×6 text centred in a box (like the VOL digits).
    auto small = [&](const std::string& t, const Rect& in, int top, Color k) {
        const double sp = Font::length(t) > 2 ? -1 : 0;
        const double tw = Font::width(t, 9, sp, Face::kSmall);
        p.textAt(t, in.x + (in.w - tw) / 2, baselineIn(top, 8, 8), 9, k, sp, Face::kSmall);
    };
    for (int k = 0; k < kRowCount; ++k) {
        const Row row = Row(k);
        const int top = r.y + rowTop(row);
        const double tw = Font::width(kRowNames[size_t(k)], 9, 0, Face::kSmall);
        p.textAt(kRowNames[size_t(k)], r.x + kLabelW - 2 - tw, baselineIn(top, 9, 9), 9, colors::kBlack, 0, Face::kSmall);
        for (int c = 0; c < kSteps; ++c) {
            const bool dead = c >= st_.length || !st_.live;
            const auto shade = shadeOf(dead);
            const Rect cr = cellRect(row, c);
            const int v = value(st_.rows, row, c);
            const bool black = row == Row::Fix && v;
            p.fill(cr, shade(black ? colors::kBlack : colors::kWhite));
            p.frame(cr, 1, shade(kBorder));
            const Rect in{cr.x + 1, cr.y + 1, cr.w - 2, cr.h - 2};
            switch (row) {
            case Row::Period: {
                // Grey middle, bar upwards (+) or downwards (-), |v| / 127 of the half height.
                const int mid = in.y + in.h / 2;
                p.fill({in.x, mid, in.w, 1}, shade(kZero));
                const int half = in.h / 2;
                const int bh = int(std::ceil(half * std::abs(v) / double(ymengine::kPeriodDeltaMax) - 0.5));
                if (bh > 0) p.fill(v > 0 ? Rect{in.x, mid - bh, in.w, bh} : Rect{in.x, mid + 1, in.w, bh}, shade(colors::kBlack));
                break;
            }
            case Row::Noise: {
                const int bh = int(std::ceil(in.h * v / 31.0 - 0.5));
                if (bh > 0) p.fill({in.x, in.bottom() - bh, in.w, bh}, shade(colors::kBlack));
                break;
            }
            case Row::Mix:  // the channel mode (v - 1); nothing for the track's mixer
                if (v > 0) small(std::to_string(v - 1), in, in.y, shade(colors::kBlack));
                break;
            case Row::Gen:
                if (v > 0) small(kGenChars[size_t(std::min(v, ymengine::kSeqGens - 1))], in, in.y, shade(colors::kBlack));
                break;
            case Row::Fix: break;
            case Row::Out: {
                // Stroke at the pitch offset of the step (high at the top), ±12 semitones over the cell, in the table.
                // Offset = 12 × log2(note period / step period): a shorter period is a higher pitch.
                const int mid = in.y + in.h / 2;
                p.fill({in.x, mid, in.w, 1}, shade(kZero));
                if (c >= st_.length) break;
                const double semis =
                    12 * std::log2(double(st_.notePeriod) / std::max<int>(1, st_.periods[size_t(c)]));
                const int dy = int(std::lround(std::clamp(semis, -12.0, 12.0) / 12 * (in.h / 2 - 1)));
                p.fill({in.x + 1, mid - dy - 1, in.w - 2, 3}, shade(colors::kBlack));
                break;
            }
            }
        }
    }
}

// Cell under a point: the columns of OrnGrid, then the row whose band holds it; the 1 px gaps belong to no cell.
bool SeqRowsGrid::cellAt(gemui::Point p, Row* row, int* step) const noexcept {
    const Rect r = bounds();
    const int rx = int(std::floor(p.x)) - (r.x + kLabelW + kGap);
    const int ry = int(std::floor(p.y)) - r.y;
    if (rx < 0 || ry < 0) return false;
    const int c = rx / (kCellW + kGap);
    if (c >= kSteps || rx % (kCellW + kGap) >= kCellW) return false;
    for (int k = 0; k < kRowCount; ++k) {
        const int top = rowTop(Row(k));
        if (ry >= top && ry < top + kRowH[size_t(k)]) {
            *row = Row(k);
            *step = c;
            return true;
        }
    }
    return false;
}

int SeqRowsGrid::valueAt(Row row, int step, double y) const noexcept {
    const Rect cr = cellRect(row, step);
    const double t = std::clamp(1 - (y - cr.y) / (cr.h - 1), 0.0, 1.0);  // 1 on the top line, 0 on the bottom one
    if (row == Row::Period) {
        const int v = int(std::round((t * 2 - 1) * ymengine::kPeriodDeltaMax));
        return std::clamp(v, -ymengine::kPeriodDeltaMax, ymengine::kPeriodDeltaMax);
    }
    return std::clamp(int(std::round(t * 31)), 0, 31);
}

void SeqRowsGrid::mouseDown(const gemui::MouseEvent& e) {
    Row row{};
    int c = 0;
    if (!cellAt(e.pos, &row, &c)) return;
    if (row == Row::Out) return;
    if (e.button == gemui::MouseButton::Right) {
        // PER and NP: a slider (D-272); the other rows return to 0, the sound of the note.
        if (row == Row::Period || row == Row::Noise) {
            if (onSliderMenu) onSliderMenu(row, c, cellRect(row, c));
        } else if (onSet) {
            onSet(row, c, 0);
        }
        return;
    }
    if (e.button != gemui::MouseButton::Left) return;
    switch (row) {
    case Row::Out: break;  // read-only
    case Row::Period:
    case Row::Noise:
        paint_ = row;
        lastStep_ = c;
        if (onSet) onSet(row, c, valueAt(row, c, e.pos.y));
        break;
    case Row::Fix:
        paint_ = row;
        lastStep_ = c;
        fixPaint_ = st_.rows.fix[size_t(c)] ? 0 : 1;
        if (onSet) onSet(row, c, fixPaint_);
        break;
    case Row::Mix:
    case Row::Gen:
        if (onMenu) onMenu(row, c, cellRect(row, c));
        break;
    }
}

void SeqRowsGrid::mouseDrag(const gemui::MouseEvent& e) {
    // Drawing in the row where it started: PER and NP follow the pointer inside the cell, FIX paints the value of the
    // press.
    if (!paint_) return;
    Row row{};
    int c = 0;
    if (!cellAt(e.pos, &row, &c) || row != *paint_) {
        lastStep_ = -1;
        return;
    }
    const bool moved = c != lastStep_;
    lastStep_ = c;
    if (row == Row::Fix) {
        if (moved && onSet) onSet(row, c, fixPaint_);
        return;
    }
    const int v = valueAt(row, c, e.pos.y);
    if (v != value(st_.rows, row, c) && onSet) onSet(row, c, v);
}

void SeqRowsGrid::mouseUp(const gemui::MouseEvent&) {
    paint_.reset();
    lastStep_ = -1;
}

bool SeqRowsGrid::mouseWheel(const gemui::WheelEvent& e) {
    // Wheel: one unit (ten with Shift on PER) up or down; MIX and GEN step through their values.
    Row row{};
    int c = 0;
    if (!cellAt(e.pos, &row, &c) || e.dy == 0 || row == Row::Fix || row == Row::Out) return false;
    const int dir = e.dy < 0 ? 1 : -1;
    const int stepBy = row == Row::Period && e.mods.shift ? 10 : 1;
    const int v = std::clamp(value(st_.rows, row, c) + dir * stepBy, minValue(row), maxValue(row));
    if (v == value(st_.rows, row, c)) return true;
    if (onWheel) onWheel(row, c, v);
    else if (onSet) onSet(row, c, v);
    return true;
}

gemui::Pointer SeqRowsGrid::pointerAt(gemui::Point p) const noexcept {
    Row row{};
    int c = 0;
    return cellAt(p, &row, &c) && row != Row::Out ? gemui::Pointer::Hand : gemui::Pointer::Arrow;
}

std::string SeqRowsGrid::tipAt(gemui::Point p) const {
    Row row{};
    int c = 0;
    if (!cellAt(p, &row, &c)) return {};
    const int v = value(st_.rows, row, c);
    const std::string step = std::to_string(c + 1);
    switch (row) {
    case Row::Period:
        return "PER step " + step + ": " +
               (v ? signedText(v) + " tone period (" + (v > 0 ? "lower" : "higher") + ")" : "no offset") +
               ". Drag to draw, wheel to adjust (Shift: 10), right click for a slider.";
    case Row::Noise:
        return "NP step " + step + ": " + (v ? "noise period " + std::to_string(v) : "the track's noise period") +
               ". Drag to draw, right click for a slider.";
    case Row::Mix:
        return "MIX step " + step + ": " + (v ? std::to_string(v - 1) + " " + kModes[size_t(v - 1)] : std::string("the track's mixer")) +
               ". Click to choose.";
    case Row::Gen: return "GEN step " + step + ": " + kGenNames[size_t(std::min(v, ymengine::kSeqGens - 1))] + ". Click to choose.";
    case Row::Fix: return "FIX step " + step + ": " + (v ? "plays the fixed note" : "plays the note") + ". Click to toggle.";
    case Row::Out: {
        // YM-Tracker preview: period, nearest note and deviation in cents, frequency. The frequency is clock / 16 /
        // period (the inverse of ymengine::tonePeriodFor), the note 69 + 12 × log2(f / 440) (A4 = MIDI 69 at 440 Hz).
        const ymseq::NoteNames names = st_.latin ? ymseq::NoteNames::Latin : ymseq::NoteNames::English;
        const int tp = std::max<int>(1, st_.periods[size_t(c)]);
        const double hz = st_.clockHz / 16 / tp;
        const double n = 69 + 12 * std::log2(hz / 440);
        const int near = int(std::lround(n));
        char buf[64];
        std::snprintf(buf, sizeof buf, "period %d ($%03X), ", tp, unsigned(tp));
        std::string out = "OUT step " + step + ", " + ymseq::absNoteName(st_.refNote, names) + " played: " + buf +
                          ymseq::absNoteName(near, names) + " " + signedText(int(std::lround((n - near) * 100))) + "c, ";
        return out + gemui::formatDecimal("%.1f Hz", hz) + (c >= st_.length ? " (after the table end)" : "");
    }
    }
    return {};
}

// ------------------------------------------------------------------------------------------- panel

SequencesPanel::SequencesPanel(MainWindow& main) : m_(main) {
    build();
    wire();
}

void SequencesPanel::build() {
    fix_.setRange(0, 127, 1);
    fix_.setTip("Fixed note played by the FIX steps");
    det_.setRange(-ymengine::kDetuneMaxCents, ymengine::kDetuneMaxCents, 1);
    det_.setTip("Detune the whole sound, in cents");
    ana_.setRange(0, ymengine::kAnalogMaxCents, 1);
    ana_.setTip("Analog: random pitch offset drawn at each note, up to this many cents");
    sync_.setTip("Hard sync: restart the square (16 voices) and the timer on tied and slid notes too");
    link_.setTip("Instrument that takes over when a Hold table ends");
    for (int s = 0; s < 3; ++s)
        for (int t = 0; t < 3; ++t) {
            Button& b = mx_[size_t(s * 3 + t)];
            b.setText(kTargets[size_t(t)]);
            b.setPadding(2, 6);
            b.setTip(std::string(kSourceNames[size_t(s)]) + " moves " + kTargetNames[size_t(t)]);
        }
    vibMin_.setRange(-ymengine::kPeriodDeltaMax, ymengine::kPeriodDeltaMax, 1);
    vibMax_.setRange(-ymengine::kPeriodDeltaMax, ymengine::kPeriodDeltaMax, 1);
    vibCyc_.setRange(1, 15, 1);
    vibRamp_.setRange(0, ymengine::kOrnamentSteps, 1);
    vibMin_.setValue(-8);
    vibMax_.setValue(8);
    vibCyc_.setValue(2);
    vibRamp_.setValue(0);
    vibMin_.setTip("Lowest tone period offset of the vibrato table");
    vibMax_.setTip("Highest tone period offset of the vibrato table");
    vibCyc_.setTip("Oscillations over the table length");
    vibRamp_.setTip("Steps for the vibrato to reach its full depth");
    vibWrite_.setTip("Write the vibrato table into the PER row, over the table length");
    // Typed values (sixth DAW test, item 77, D-275); the fixed note by its name.
    for (gemui::ValueField* f : {&fixv_, &detv_, &anav_, &minv_, &maxv_, &cycv_, &rampv_}) f->setPx(14);
    fixv_.setWholeText(true);
    fixv_.setTip("Click to type a note (C#4, Sol3) or a MIDI note number");
}

void SequencesPanel::addTo(gemui::Widget& window) {
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &sep_, &grid_, &labFix_, &fix_, &fixv_, &labDet_, &det_, &detv_, &labAna_, &ana_, &anav_,
             &sync_, &link_, &labMx_, &labVib_, &labMin_, &vibMin_, &minv_, &labMax_, &vibMax_, &maxv_, &labCyc_,
             &vibCyc_, &cycv_, &labRamp_, &vibRamp_, &rampv_, &vibWrite_})
        window.add(*w);
    for (auto& l : labSrc_) window.add(l);
    for (auto& b : mx_) window.add(b);
}

void SequencesPanel::wire() {
    // Every write of the grid, its menus and its slider popups ends here: one editPatch, the stroke's undo step taken
    // by its first change (armed by the press in the window).
    grid_.onSet = [this](Row row, int step, int v) {
        editPatch([row, step, v](ymengine::TrackPatch& p) {
            const size_t s = size_t(step);
            switch (row) {
            case Row::Period: p.rows.period[s] = int8_t(v); break;
            case Row::Noise: p.rows.noise[s] = uint8_t(v); break;
            case Row::Mix: p.rows.mix[s] = uint8_t(v); break;
            case Row::Gen: p.rows.gen[s] = uint8_t(v); break;
            case Row::Fix: p.rows.fix[s] = uint8_t(v); break;
            case Row::Out: break;  // read-only
            }
        });
    };
    grid_.onWheel = [this](Row row, int step, int v) {
        m_.armCur();  // one undo step per wheel notch, like a key
        grid_.onSet(row, step, v);
    };
    grid_.onMenu = [this](Row row, int step, const Rect& cell) { cellMenu(row, step, cell); };
    grid_.onSliderMenu = [this](Row row, int step, const Rect& cell) {
        // PER and NP like VOL in the table (D-272): the value applies live (the undo step was taken on press); a right
        // click on the slider gives 0 back, the sound of the note (D-273).
        gemui::Desktop& d = m_.desktop();
        d.closePopups();
        SliderPopupSpec spec;
        spec.min = SeqRowsGrid::minValue(row);
        spec.max = SeqRowsGrid::maxValue(row);
        spec.value = SeqRowsGrid::value(patch().rows, row, step);
        spec.reset = 0;
        spec.set = [this, row, step, t0 = m_.cur_](int v) {
            if (m_.cur_ == t0) grid_.onSet(row, step, v);  // not into another track focused under the popup
        };
        d.openPopup(makeSliderPopup(std::move(spec), cell, d.width(), d.height()));
    };

    // Sliders: every move applies, one undo step per gesture (armed on press). While a slider is held,
    // MainWindow::dragging_ points to it so that refresh() does not move it under the pointer.
    auto slider = [this](gemui::Slider& s, std::function<void(ymengine::TrackPatch&, double)> set) {
        s.onGestureStart = [this, &s] { m_.dragging_ = &s; };
        s.onGestureEnd = [this] {
            m_.dragging_ = nullptr;
            refresh();
        };
        s.onChange = [this, set](double v) { editPatch([&](ymengine::TrackPatch& p) { set(p, v); }); };
    };
    slider(fix_, [](ymengine::TrackPatch& p, double v) { p.fixedNote = uint8_t(v); });
    slider(det_, [](ymengine::TrackPatch& p, double v) { p.detuneCents = v; });
    slider(ana_, [](ymengine::TrackPatch& p, double v) { p.analogCents = v; });
    auto typeInto = [](gemui::Slider& sl) {
        return [&sl](const std::string& t) {
            const auto v = gemui::typedNumber(t);
            if (!v) return false;
            sl.input(*v);
            return true;
        };
    };
    fixv_.onCommit = [this](const std::string& t) {
        const auto n = ymseq::parseAbsNote(t);
        if (!n) return false;
        fix_.input(*n);
        return true;
    };
    detv_.onCommit = typeInto(det_);
    anav_.onCommit = typeInto(ana_);
    minv_.onCommit = typeInto(vibMin_);
    maxv_.onCommit = typeInto(vibMax_);
    cycv_.onCommit = typeInto(vibCyc_);
    rampv_.onCommit = typeInto(vibRamp_);
    sync_.onClick = [this] { editPatch([](ymengine::TrackPatch& p) { p.hardSync = !p.hardSync; }); };
    link_.onClick = [this] { linkMenu(); };
    // Button k toggles bit k of the matrix: source k / 3 drives target k % 3 (ymengine::pitchBit).
    for (int k = 0; k < 9; ++k)
        mx_[size_t(k)].onClick = [this, k] {
            editPatch([k](ymengine::TrackPatch& p) { p.pitchMatrix = uint16_t(p.pitchMatrix ^ (1u << k)); });
        };
    // Settings of the vibrato generator: specific to the window, kept as long as it lives.
    for (gemui::Slider* s : {&vibMin_, &vibMax_, &vibCyc_, &vibRamp_}) s->onChange = [this](double) { refresh(); };
    vibWrite_.onClick = [this] {
        const auto table = vibratoTable(int(vibMin_.value()), int(vibMax_.value()), int(vibCyc_.value()),
                                        int(vibRamp_.value()), patch().ornamentLength);
        editPatch([&table](ymengine::TrackPatch& p) { p.rows.period = table; });
    };
}

std::array<int8_t, ymengine::kOrnamentSteps> SequencesPanel::vibratoTable(int lo, int hi, int cycles, int ramp,
                                                                         int length) {
    // Step i of `length`: mid + half × sin(2π × cycles × i / length) × the ramp's share (i / ramp, at most 1),
    // rounded and brought within the PER range; the steps past the length stay at 0.
    std::array<int8_t, ymengine::kOrnamentSteps> out{};
    length = std::clamp(length, 1, ymengine::kOrnamentSteps);
    const double mid = (lo + hi) / 2.0, half = (hi - lo) / 2.0;
    for (int i = 0; i < length; ++i) {
        const double s = std::sin(2 * std::numbers::pi * cycles * i / length);
        const double amp = ramp > 0 ? std::min(1.0, double(i) / ramp) : 1.0;
        const double v = mid + half * s * amp;
        out[size_t(i)] = int8_t(std::clamp(int(std::lround(v)), -ymengine::kPeriodDeltaMax, ymengine::kPeriodDeltaMax));
    }
    return out;
}

// ------------------------------------------------------------------------------------------- state

bool SequencesPanel::drum() const noexcept { return m_.drumTrack(); }
const ymengine::TrackPatch& SequencesPanel::patch() const noexcept { return m_.extras_.tracks[size_t(m_.cur_)].patch; }

// No test of a percussion track here: on one, the settings are disabled and the rows hidden (refresh, placeRows).
void SequencesPanel::editPatch(const std::function<void(ymengine::TrackPatch&)>& fn) {
    const int t = m_.cur_;
    m_.host_.editExtras([t, &fn](plugin::NonParamState& s) { fn(s.tracks[size_t(t)].patch); });
    m_.pullExtras();
    m_.pushCur();
}

std::string SequencesPanel::noteName(int note) const {
    const bool latin = m_.host_.preference("noteNames", "0") == "1";
    return ymseq::absNoteName(note, latin ? ymseq::NoteNames::Latin : ymseq::NoteNames::English);
}

void SequencesPanel::cellMenu(Row row, int step, const Rect& cell) {
    const int cur = SeqRowsGrid::value(patch().rows, row, step);
    std::vector<MenuItem> items;
    auto add = [&](std::string label, int v) {
        MenuItem m;
        m.label = std::move(label);
        m.pick = [this, row, step, v] { grid_.onSet(row, step, v); };
        items.push_back(std::move(m));
    };
    // The item index equals the stored value, so `cur` is also the checked item.
    if (row == Row::Mix) {
        add("Channel (track mixer)", 0);
        for (int k = 0; k < ymengine::kChannelModes; ++k) add(std::to_string(k) + " " + kModes[size_t(k)], k + 1);
    } else {
        for (int k = 0; k < ymengine::kSeqGens; ++k) add(kGenNames[size_t(k)], k);
    }
    gemui::pickOnlyWhile(items, m_.stillFocused());  // as the PER/NP box: the sequence of the track it was opened on
    gemui::MenuList::open(m_.desktop(), std::move(items), cur, gemui::boxOf(cell), false);
}

void SequencesPanel::linkMenu() {
    const int cur = patch().link;
    std::vector<MenuItem> items;
    int checked = 0;
    auto add = [&](std::string label, int v) {
        if (v == cur) checked = int(items.size());
        MenuItem m;
        m.label = std::move(label);
        m.pick = [this, v] { editPatch([v](ymengine::TrackPatch& p) { p.link = uint8_t(v); }); };
        items.push_back(std::move(m));
    };
    // None, then the used instruments by their number ($01 to $20 = link 1 to 32); an empty slot still linked is listed
    // too, so that it shows as checked.
    add("None", 0);
    const auto& ins = m_.extras_.instruments;
    for (int k = 0; k < plugin::kInstruments; ++k) {
        const plugin::Instrument& x = ins[size_t(k)];
        const std::string hex = std::string(1, "0123456789ABCDEF"[(k + 1) >> 4]) + "0123456789ABCDEF"[(k + 1) & 15];
        if (x.used) add(hex + " " + plugin::instrumentLabel(x, k), k + 1);
        else if (k + 1 == cur) add(hex + " (empty)", k + 1);
    }
    m_.trackPopup(link_, std::move(items), checked);
}

void SequencesPanel::refresh() {
    const bool dr = drum();
    const ymengine::TrackPatch& p = patch();
    const bool lv = p.ornamentOn && !dr;
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&fix_, &det_, &ana_, &sync_, &link_, &vibMin_,
                                                                  &vibMax_, &vibCyc_, &vibRamp_, &vibWrite_})
        w->setEnabled(!dr);
    for (gemui::ValueField* f : {&fixv_, &detv_, &anav_, &minv_, &maxv_, &cycv_, &rampv_}) f->setEditable(!dr);
    for (auto& b : mx_) b.setEnabled(!dr);

    SeqRowsGrid::State st;
    st.rows = p.rows;
    st.length = p.ornamentLength;
    st.live = lv;
    // OUT preview: the period that each step of the table gives the reference note, and that of the note alone, at
    // the chip clock of the parameters.
    const ymcore::MasterClock clock = plugin::chipClock(m_.values_);
    for (int s = 0; s < SeqRowsGrid::kSteps; ++s)
        st.periods[size_t(s)] = ymengine::tablePeriod(p, st.refNote, s, clock);
    st.notePeriod = ymengine::tonePeriodFor(ymengine::noteHz(st.refNote), clock);
    st.clockHz = clock.hz();
    st.latin = m_.host_.preference("noteNames", "0") == "1";
    grid_.setState(st);

    if (m_.dragging_ != &fix_) fix_.setValue(p.fixedNote);
    if (m_.dragging_ != &det_) det_.setValue(p.detuneCents);
    if (m_.dragging_ != &ana_) ana_.setValue(p.analogCents);
    if (!fixv_.editing()) fixv_.setText(noteName(p.fixedNote));
    if (!detv_.editing()) detv_.setText(signedText(int(std::lround(p.detuneCents))) + "c");
    if (!anav_.editing()) anav_.setText(std::to_string(int(std::lround(p.analogCents))) + "c");
    sync_.setOn(p.hardSync);
    // Link: the instrument's number in two hexadecimal digits (1 to 32: $01 to $20).
    link_.setText(p.link ? "Link: " + std::string(1, "0123456789ABCDEF"[p.link >> 4]) + "0123456789ABCDEF"[p.link & 15]
                         : "Link: none");
    for (int k = 0; k < 9; ++k) mx_[size_t(k)].setOn((p.pitchMatrix >> k) & 1);
    // The vibrato generator's values come from its own sliders, not from the track.
    if (!minv_.editing()) minv_.setText(signedText(int(vibMin_.value())));
    if (!maxv_.editing()) maxv_.setText(signedText(int(vibMax_.value())));
    if (!cycv_.editing()) cycv_.setText(std::to_string(int(vibCyc_.value())));
    if (!rampv_.editing()) rampv_.setText(std::to_string(int(vibRamp_.value())));
}

// ------------------------------------------------------------------------------------------- layout

void SequencesPanel::placeRows(double x, double y, bool show) {
    grid_.setVisible(show);
    grid_.setBox({x, y, double(SeqRowsGrid::kWidth), double(SeqRowsGrid::kHeight)});
}

double SequencesPanel::layout(double x, double y, double width, double pad) {
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto lab = [](gemui::Label& l, double w = 0) { return FlexItem{&l, w > 0 ? w : l.naturalWidth(), 16}; };
    auto sld = [](gemui::Slider& s, double w) { return FlexItem{&s, w, 16}; };
    const double num = Font::width("+100c", 14);  // fixed-width values: the row does not move

    // Rule up to the edges of the window, like the one above the table (the rows and their notes are in the table).
    y += 8;
    sep_.setBox({x - pad, y, width + 2 * pad, 2});
    y += 2 + 8;

    // Three columns: pitch of the note (fixed note, detune, analog, hard sync, chaining), pitch matrix (one row per
    // source, one button per target), vibrato table generator. They start at x, x + 260 and x + 452 (the LFO below
    // keeps the same columns); the panel ends at the bottom of the tallest.
    // Label, slider and typed value at fixed widths on one line at (cx, cy); returns the line's height.
    auto sliderRow = [&](double cx, double cy, gemui::Label& l, double lw, gemui::Slider& s, double sw,
                         gemui::ValueField& v, double vw) {
        std::vector<FlexItem> r = {lab(l, lw), sld(s, sw), FlexItem{&v, vw, 16}};
        return flexRow(cx, cy, lw + sw + vw + 2 * kGap, r, kGap, false);
    };
    const double y0 = y;
    const double lw1 = Font::width("Detune", 14);
    double y1 = y0;
    y1 += sliderRow(x, y1, labFix_, lw1, fix_, 120, fixv_, num) + 6;
    y1 += sliderRow(x, y1, labDet_, lw1, det_, 120, detv_, num) + 6;
    y1 += sliderRow(x, y1, labAna_, lw1, ana_, 120, anav_, num) + 6;
    std::vector<FlexItem> pitch = {btn(sync_), btn(link_)};
    y1 += flexRow(x, y1, lw1 + 120 + num + 2 * kGap, pitch, kGap, false);

    const double x2 = x + 260, srcW = Font::width("Vibrato", 14);
    double y2 = y0;
    labMx_.setBox({x2, y2, labMx_.naturalWidth(), 16});
    y2 += 16 + 6;
    for (int s = 0; s < 3; ++s) {
        std::vector<FlexItem> mx = {lab(labSrc_[size_t(s)], srcW)};
        for (int t = 0; t < 3; ++t) mx.push_back(btn(mx_[size_t(s * 3 + t)]));
        y2 += flexRow(x2, y2, 180, mx, 4, false) + 4;
    }

    const double x3 = x + 452, lw3 = Font::width("Cycles", 14), vw3 = Font::width("+127", 14);
    double y3 = y0;
    labVib_.setBox({x3, y3, labVib_.naturalWidth(), 16});
    y3 += 16 + 6;
    y3 += sliderRow(x3, y3, labMin_, lw3, vibMin_, 150, minv_, vw3) + 6;
    y3 += sliderRow(x3, y3, labMax_, lw3, vibMax_, 150, maxv_, vw3) + 6;
    y3 += sliderRow(x3, y3, labCyc_, lw3, vibCyc_, 150, cycv_, vw3) + 6;
    y3 += sliderRow(x3, y3, labRamp_, lw3, vibRamp_, 150, rampv_, vw3) + 6;
    std::vector<FlexItem> wr = {btn(vibWrite_)};
    y3 += flexRow(x3, y3, 272, wr, kGap, false);
    return std::max({y1, y2, y3});
}

std::optional<Rect> SequencesPanel::rectOf(std::string_view id) const {
    // "seqxMx" and one digit 0-8: a button of the pitch matrix.
    if (id.substr(0, 6) == "seqxMx" && id.size() == 7 && id[6] >= '0' && id[6] <= '8') return mx_[size_t(id[6] - '0')].bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"seqxGrid", &grid_},      {"seqxFix", &fix_},           {"seqxDetune", &det_},
                           {"seqxAnalog", &ana_},     {"seqxSync", &sync_},         {"seqxLink", &link_},
                           {"seqxVibMin", &vibMin_},  {"seqxVibMax", &vibMax_},     {"seqxVibCycles", &vibCyc_},
                           {"seqxVibRamp", &vibRamp_}, {"seqxVibWrite", &vibWrite_}, {"seqxFixV", &fixv_},
                           {"seqxDetuneV", &detv_},   {"seqxAnalogV", &anav_},      {"seqxVibMinV", &minv_},
                           {"seqxVibMaxV", &maxv_},   {"seqxVibCyclesV", &cycv_},   {"seqxVibRampV", &rampv_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

const gemui::Slider* SequencesPanel::slider(std::string_view id) const noexcept {
    if (id == "seqxFix") return &fix_;
    if (id == "seqxDetune") return &det_;
    if (id == "seqxAnalog") return &ana_;
    if (id == "seqxVibMin") return &vibMin_;
    if (id == "seqxVibMax") return &vibMax_;
    if (id == "seqxVibCycles") return &vibCyc_;
    if (id == "seqxVibRamp") return &vibRamp_;
    return nullptr;
}

}  // namespace stym::pluginui
