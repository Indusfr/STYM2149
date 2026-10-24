// STYM2149 — pluginui: Modulation window (on/off, length, Loop / Hold, rate, Clear, grid)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype: buildOrnGrid, ornNorm, ornSet, ornNseSet, ornVolSet, refreshOrn, openOrn,
// closeOrn, the handlers of #ornOn, #ornLoop, #ornRate, #ornClear and #ornLen, the Modulation part of the keydown
// handler.
#include "pluginui/ModulationWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "gemui/Desktop.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::Color;
using gemui::Face;
using gemui::Font;
using gemui::MenuItem;
using gemui::Rect;
namespace colors = gemui::colors;

namespace {

constexpr double kGap = 8, kPad = 8;
constexpr double kLenW = 110;  // #ornLen: style width:110px (flex:1 keeps that width within the content width)
constexpr double kDeadAlpha = 0.3;  // .oc.dead{opacity:.3}

// Baseline of a line of text of `px` in a line box of `lineH` (line-height smaller than the font: negative
// half-leading, like the browser).
int baselineIn(double lineTop, double lineH, int px) {
    const Font::LineBox lb = Font::lineBox("0", px);
    return gemui::snap(lineTop + std::floor((lineH - lb.height()) / 2) + lb.ascent);
}

// Number at the start of a typed value, rounded and brought within the slider's range; nullopt when there is none.
std::optional<double> parseValue(const gemui::Slider& s, const std::string& txt) {
    size_t used = 0;
    const double raw = gemui::readDecimal(txt, &used);
    if (used == 0 || !std::isfinite(raw)) return std::nullopt;
    return std::clamp(std::round(raw), s.min(), s.max());
}

std::string signedSemis(int semi) { return semi > 0 ? "+" + std::to_string(semi) : std::to_string(semi); }

}  // namespace

// ------------------------------------------------------------------------------------------- OrnGrid

void OrnGrid::setState(const State& s) {
    if (s == st_) return;
    st_ = s;
    repaint();
}

Rect OrnGrid::cellRect(int step, int row) const noexcept {
    const Rect r = bounds();
    return {r.x + kLabelW + kGap + step * (kCellW + kGap), r.y + row * (kCellH + kGap), kCellW, kCellH};
}

Rect OrnGrid::noiseRect(int step) const noexcept {
    const Rect c = cellRect(step, 0);
    return {c.x, bounds().y + kNseY, kCellW, kNseH};
}

Rect OrnGrid::volumeRect(int step) const noexcept {
    const Rect c = cellRect(step, 0);
    return {c.x, bounds().y + kVolY, kCellW, kVolH};
}

void OrnGrid::paint(gemui::Painter& p) {
    const Rect r = bounds();
    const Color ink = colors::kBlack;
    // .olab: 10 px (9 px for NSE and VOL), 9 px line box, right-aligned 2 px from the edge; 6×6 font in the 20 px
    // column (D-071).
    auto label = [&](const std::string& t, int top, int px) {
        const double tw = Font::width(t, px, 0, Face::kSmall);
        p.textAt(t, r.x + kLabelW - 2 - tw, baselineIn(top, 9, px), px, ink, 0, Face::kSmall);
    };
    // .oc cell: background, #999 border; beyond the length, the whole cell (content included) at 30 % over the white
    // background of the window.
    auto cell = [&](const Rect& c, Color bg, bool dead) {
        auto shade = [dead](Color k) { return dead ? gemui::mix(k, colors::kWhite, kDeadAlpha) : k; };
        p.fill(c, shade(bg));
        p.frame(c, 1, shade(kBorder));
    };

    // Pitch rows, +12 at the top; a label every fourth row (+12, +8 ... -12).
    for (int row = 0; row < kRows; ++row) {
        const int semi = kSpan - row;
        if (row % 4 == 0 || semi == 0) label(signedSemis(semi), r.y + row * (kCellH + kGap), 10);
        for (int c = 0; c < kSteps; ++c) {
            const Rect cr = cellRect(c, row);
            const bool dead = c >= st_.length;
            const bool on = st_.pitch[size_t(c)] == semi;
            cell(cr, on ? colors::kBlack : semi == 0 ? kZero : colors::kWhite, dead);
            if (c == st_.curStep && row == st_.curRow) {
                // box-shadow inset 4 px inside the border: the whole interior (8 × 7 px) turns green.
                const Color g = dead ? gemui::mix(colors::kGreen, colors::kWhite, kDeadAlpha) : colors::kGreen;
                p.fill({cr.x + 1, cr.y + 1, cr.w - 2, cr.h - 2}, g);
            }
        }
    }
    // Full-width separators: 3 black px, 2 px margins; between them, the sequence rows (D-270).
    p.fill({r.x, r.y + kSepY + 2, kWidth, 3}, colors::kBlack);
    p.fill({r.x, r.y + kSep2Y + 2, kWidth, 3}, colors::kBlack);

    label("NSE", r.y + kNseY, 9);
    label("VOL", r.y + kVolY, 9);
    for (int c = 0; c < kSteps; ++c) {
        const bool dead = c >= st_.length;
        cell(noiseRect(c), st_.noise[size_t(c)] ? colors::kBlack : colors::kWhite, dead);

        const Rect vr = volumeRect(c);
        cell(vr, colors::kWhite, dead);
        auto shade = [dead](Color k) { return dead ? gemui::mix(k, colors::kWhite, kDeadAlpha) : k; };
        const Rect in{vr.x + 1, vr.y + 1, vr.w - 2, vr.h - 2};  // padding box (padding 0)
        const int v = std::min<int>(st_.volume[size_t(c)], 15);
        const int bh = int(std::ceil(in.h * v / 15.0 - 0.5));  // .vbar: height v/15 × 100 %
        if (bh > 0) p.fill({in.x, in.bottom() - bh, in.w, bh}, shade(colors::kBlack));
        // .vnum: 8 px, at the top (top 1 px), or in white at the bottom (bottom 1 px) when the bar goes above 70 %; 6×6
        // font centred, tightened by one pixel per letter beyond two characters (D-119).
        const bool low = v * 100 > 70 * 15;  // v / 15 > 70 %, in whole numbers
        const std::string t = std::to_string(v);
        const int base = low ? baselineIn(in.bottom() - 1 - 8, 8, 8) : baselineIn(in.y + 1, 8, 8);
        const double sp = Font::length(t) > 2 ? -1 : 0;
        const double tw = Font::width(t, 9, sp, Face::kSmall);
        p.textAt(t, in.x + (in.w - tw) / 2, base, 9, shade(low ? colors::kWhite : colors::kBlack), sp, Face::kSmall);
    }
}

// Columns start after the label column and repeat every kCellW + kGap px; rows likewise, by part. The 1 px gaps, the
// separators and the place of the sequence rows (a widget of their own) belong to no part.
OrnGrid::Part OrnGrid::partAt(gemui::Point p, int* step, int* row) const noexcept {
    const Rect r = bounds();
    const int rx = int(std::floor(p.x)) - (r.x + kLabelW + kGap);
    const int ry = int(std::floor(p.y)) - r.y;
    if (rx < 0 || ry < 0) return Part::None;
    const int c = rx / (kCellW + kGap);
    if (c >= kSteps || rx % (kCellW + kGap) >= kCellW) return Part::None;
    *step = c;
    if (ry < kSepY) {
        if (ry % (kCellH + kGap) >= kCellH) return Part::None;
        *row = ry / (kCellH + kGap);
        return Part::Pitch;
    }
    if (ry >= kNseY && ry < kNseY + kNseH) return Part::Noise;
    if (ry >= kVolY && ry < kVolY + kVolH) return Part::Volume;
    return Part::None;
}

int OrnGrid::levelAt(int step, double y) const noexcept {
    // lvlOf: vertical position in the box of the cell (border included) -> level 0 to 15.
    const Rect vr = volumeRect(step);
    const double t = 1 - (y - vr.y) / vr.h;
    return std::clamp(int(std::round(t * 15)), 0, 15);
}

void OrnGrid::mouseDown(const gemui::MouseEvent& e) {
    int c = 0, row = 0;
    const Part part = partAt(e.pos, &c, &row);
    if (part == Part::None) return;
    if (e.button == gemui::MouseButton::Right) {
        // contextmenu of a VOL cell: precise setting with a slider; the keyboard cursor moves to the step.
        if (part != Part::Volume) return;
        if (onCursor) onCursor(c, -1);
        if (onVolumeMenu) onVolumeMenu(c, volumeRect(c));
        return;
    }
    if (e.button != gemui::MouseButton::Left) return;
    // A left press starts a stroke in its part; an NSE stroke paints the opposite of the cell pressed.
    paint_ = part;
    lastStep_ = c;
    lastRow_ = row;
    switch (part) {
    case Part::Pitch:
        if (onCursor) onCursor(c, row);
        if (onPitch) onPitch(c, kSpan - row);
        break;
    case Part::Noise:
        noisePaint_ = !st_.noise[size_t(c)];
        if (onNoise) onNoise(c, noisePaint_);
        break;
    case Part::Volume:
        if (onCursor) onCursor(c, -1);
        if (onVolume) onVolume(c, levelAt(c, e.pos.y));
        break;
    case Part::None: break;
    }
}

void OrnGrid::mouseDrag(const gemui::MouseEvent& e) {
    // pointerover of the cells while drawing: the pitch is drawn whatever drawing was started, NSE and VOL only in
    // their own. VOL follows the pointer inside the cell (the prototype read the position again only on entering an
    // element of the cell).
    if (paint_ == Part::None) return;
    int c = 0, row = 0;
    const Part part = partAt(e.pos, &c, &row);
    const bool moved = c != lastStep_ || row != lastRow_;
    if (part == Part::None) {
        lastStep_ = lastRow_ = -1;
        return;
    }
    lastStep_ = c;
    lastRow_ = part == Part::Pitch ? row : -1;
    if (part == Part::Pitch && moved) {
        if (onPitch) onPitch(c, kSpan - row);
    } else if (part == Part::Noise && paint_ == Part::Noise && moved) {
        if (onNoise) onNoise(c, noisePaint_);
    } else if (part == Part::Volume && paint_ == Part::Volume) {
        const int v = levelAt(c, e.pos.y);
        if (v != st_.volume[size_t(c)] && onVolume) onVolume(c, v);
    }
}

void OrnGrid::mouseUp(const gemui::MouseEvent&) {
    paint_ = Part::None;
    lastStep_ = lastRow_ = -1;
}

gemui::Pointer OrnGrid::pointerAt(gemui::Point p) const noexcept {
    int c = 0, row = 0;
    return partAt(p, &c, &row) == Part::None ? gemui::Pointer::Arrow : gemui::Pointer::Hand;
}

std::string OrnGrid::tipAt(gemui::Point p) const {
    int c = 0, row = 0;
    switch (partAt(p, &c, &row)) {
    case Part::Pitch: {
        const int semi = kSpan - row;
        const std::string what = semi == 0 ? "no pitch shift"
                                           : signedSemis(semi) + " semitone" + (std::abs(semi) > 1 ? "s" : "");
        return "Step " + std::to_string(c + 1) + ": " + what + ". Drag to draw the pitch table.";
    }
    case Part::Noise: return "NSE step " + std::to_string(c + 1) + ": add noise on this tick";
    case Part::Volume:
        return "VOL step " + std::to_string(c + 1) +
               ": 4-bit level on this tick. Drag up or down to draw, right click for a slider.";
    case Part::None: break;
    }
    return {};
}

// ------------------------------------------------------------------------------------------- construction

ModulationWindow::ModulationWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 300, 90});  // provisional size: layout() sets the real one at the first refresh
}

void ModulationWindow::build() {
    // Prototype ids of the sliders and buttons that MIDI learn can learn (D-146).
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&on_, "ornOn"}, {&len_, "ornLen"}, {&loop_, "ornLoop"}, {&rate_, "ornRate"}, {&clear_, "ornClear"}})
        w->setId(id);
    setTitle("MODULATION");
    // 12 px .hint: 6×6 font, at the width of the prototype's 12 px font (D-071).
    for (gemui::Label* l : {&hint_, &hint2_, &hint3_}) {
        l->setFace(Face::kSmall);
        l->setColor(colors::kHint);
    }
    len_.setRange(1, OrnGrid::kSteps, 1);
    len_.setTip("Number of steps in the modulation table");
    lenv_.setMinChars(2);  // #ornLenv{min-width:2ch}
    lenv_.setMask("99");
    loop_.setTip("Loop the table instead of playing it once");
    rate_.setTip("Table speed, in VBL ticks per step");
    clear_.setTip("Clear the modulation table and its sequence rows");
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&on_, &labLen_, &len_, &lenv_, &loop_, &rate_,
                                                                  &clear_, &hint_, &hint2_, &hint3_, &sep_, &grid_})
        add(*w);
    seqx_.addTo(*this);  // the sequences (D-240): their rows over the table (D-270), their settings below it
    lfo_.addTo(*this);   // the LFO below them (D-257)
}

void ModulationWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };  // closeOrn
    on_.onClick = [this] {
        if (drum()) return;  // drums: not applicable
        editPatch([](ymengine::TrackPatch& p) { p.ornamentOn = !p.ornamentOn; });
    };
    loop_.onClick = [this] { editPatch([](ymengine::TrackPatch& p) { p.ornamentLoop = !p.ornamentLoop; }); };
    // Rate: 1 to 4 VBL frames per step; item k - 1 is checked.
    rate_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int k = 1; k <= 4; ++k) {
            MenuItem m;
            m.label = std::to_string(k) + "/frame";
            m.pick = [this, k] { editPatch([k](ymengine::TrackPatch& p) { p.ornamentRate = uint8_t(k); }); };
            items.push_back(std::move(m));
        }
        m_.trackPopup(rate_, std::move(items), patch().ornamentRate - 1);
    };
    clear_.onClick = [this] {
        // The whole table: pitch, NSE and VOL, and the sequence rows that play with it (D-240), one undo step.
        editPatch([](ymengine::TrackPatch& p) {
            p.ornamentPitch.fill(0);
            p.ornamentNoise.fill(0);
            p.ornamentVolume.fill(15);
            p.rows = {};
        });
    };
    // Length: state that cannot be automated (the held slider is not read back); every move applies.
    len_.onGestureStart = [this] { m_.dragging_ = &len_; };
    len_.onGestureEnd = [this] {
        m_.dragging_ = nullptr;
        m_.refresh();
    };
    len_.onChange = [this](double v) { setLength(int(v)); };
    lenv_.onCommit = [this](const std::string& t) {  // makeEditable('ornLenv', sliderApply('ornLen'), '99')
        const auto v = parseValue(len_, t);
        if (!v) return false;
        setLength(int(*v));
        return true;
    };

    // Grid: the cursor is editor state (EditorSession); each write is one editPatch, and the stroke's undo step, armed
    // by the press, is pushed by its first change only (MainWindow::pushCur).
    grid_.onCursor = [this](int step, int row) {
        EditorSession& s = m_.host_.session();
        s.ornCur = step;
        if (row >= 0) s.ornRow = row;
        refreshGrid();
    };
    grid_.onPitch = [this](int step, int semi) {
        editPatch([step, semi](ymengine::TrackPatch& p) { p.ornamentPitch[size_t(step)] = int8_t(semi); });
    };
    grid_.onNoise = [this](int step, bool on) {
        editPatch([step, on](ymengine::TrackPatch& p) { p.ornamentNoise[size_t(step)] = on ? 1 : 0; });
    };
    grid_.onVolume = [this](int step, int v) {
        editPatch([step, v](ymengine::TrackPatch& p) { p.ornamentVolume[size_t(step)] = uint8_t(v); });
    };
    grid_.onVolumeMenu = [this](int step, const Rect& cell) {
        // openVolMenu: the value applies live (the undo step was taken on press).
        gemui::Desktop& d = m_.desktop();
        d.closePopups();
        const int v = std::min<int>(patch().ornamentVolume[size_t(step)], 15);
        auto set = [this, step, t0 = m_.cur_](int nv) {
            if (m_.cur_ != t0) return;  // another track focused under the popup (automation): not its table
            editPatch([step, nv](ymengine::TrackPatch& p) { p.ornamentVolume[size_t(step)] = uint8_t(nv); });
        };
        d.openPopup(makeVolumePopup(v, set, cell, d.width(), d.height()));
    };
}

// ------------------------------------------------------------------------------------------- state

bool ModulationWindow::drum() const noexcept { return m_.drumTrack(); }
bool ModulationWindow::live() const noexcept { return patch().ornamentOn && !drum(); }
const ymengine::TrackPatch& ModulationWindow::patch() const noexcept {
    return m_.extras_.tracks[size_t(m_.cur_)].patch;
}

// Unlike MainWindow::editPatch, no test of a percussion track here: the controls that would edit one are disabled or
// hidden (On is refused, the table and its rows show only while live()).
void ModulationWindow::editPatch(const std::function<void(ymengine::TrackPatch&)>& fn) {
    const int t = m_.cur_;
    m_.host_.editExtras([t, &fn](plugin::NonParamState& s) { fn(s.tracks[size_t(t)].patch); });
    m_.pullExtras();
    m_.pushCur();
}

void ModulationWindow::setLength(int n) {
    n = std::clamp(n, 1, OrnGrid::kSteps);
    if (n == patch().ornamentLength) return;
    editPatch([n](ymengine::TrackPatch& p) { p.ornamentLength = uint8_t(n); });
}

void ModulationWindow::show() {
    // openOrn: the window comes to the front.
    open();
    refresh();
    m_.syncWindowButtons();
}

void ModulationWindow::refresh() {
    // refreshWinTitles, refreshOrn. When off, the row keeps the values of the track (the prototype left there those of
    // the last track shown while on).
    setTitle("MODULATION - " + m_.trackName(m_.cur_));
    if (!isOpen()) return;
    const bool dr = drum(), lv = live();
    const ymengine::TrackPatch& p = patch();
    on_.setEnabled(!dr);
    on_.setTip(dr ? "Percussion tracks do not use modulation tables"
                  : "Enable the modulation table for this channel");
    on_.setOn(lv);
    on_.setText(lv ? "On" : "Off");
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&len_, &loop_, &rate_, &clear_}) w->setEnabled(lv);
    lenv_.setDim(!lv);
    if (m_.dragging_ != &len_) len_.setValue(p.ornamentLength);
    if (!lenv_.editing()) lenv_.setText(std::to_string(p.ornamentLength));
    loop_.setText(p.ornamentLoop ? "Loop" : "Hold");
    rate_.setText(std::to_string(std::max<int>(p.ornamentRate, 1)) + "/frame");
    refreshGrid();
    seqx_.refresh();
    lfo_.refresh();
    layout();
}

void ModulationWindow::refreshGrid() {
    const ymengine::TrackPatch& p = patch();
    const EditorSession& s = m_.host_.session();
    OrnGrid::State st;
    st.pitch = p.ornamentPitch;
    st.noise = p.ornamentNoise;
    st.volume = p.ornamentVolume;
    st.length = p.ornamentLength;
    st.curStep = s.ornCur;
    st.curRow = s.ornRow;
    grid_.setState(st);
}

bool ModulationWindow::tableKey(const gemui::KeyEvent& e) {
    if (gemui::FloatWindow::topmost(m_) != this || !live()) return false;
    EditorSession& s = m_.host_.session();
    int dc = 0, dr = 0;
    switch (e.key) {
    case gemui::Key::Left: dc = -1; break;
    case gemui::Key::Right: dc = 1; break;
    case gemui::Key::Up: dr = -1; break;
    case gemui::Key::Down: dr = 1; break;
    case gemui::Key::Enter: {
        // The step under the cursor takes the pitch of the row; the length grows up to it.
        const int c = s.ornCur, semi = OrnGrid::kSpan - s.ornRow;
        editPatch([c, semi](ymengine::TrackPatch& p) {
            p.ornamentPitch[size_t(c)] = int8_t(semi);
            if (c >= p.ornamentLength) p.ornamentLength = uint8_t(c + 1);
        });
        return true;
    }
    case gemui::Key::Delete:
    case gemui::Key::Backspace: {
        const int c = s.ornCur;
        editPatch([c](ymengine::TrackPatch& p) {
            p.ornamentPitch[size_t(c)] = 0;
            p.ornamentNoise[size_t(c)] = 0;
            p.ornamentVolume[size_t(c)] = 15;
        });
        return true;
    }
    case gemui::Key::Character: {
        // KeyN: the physical key when the host gives it, otherwise the character.
        const int code = e.code ? e.code : (e.ch < 128 ? std::toupper(int(e.ch)) : 0);
        if (code != 'N' || e.mods.ctrl || e.mods.alt) return false;
        const int c = s.ornCur;
        editPatch([c](ymengine::TrackPatch& p) { p.ornamentNoise[size_t(c)] = p.ornamentNoise[size_t(c)] ? 0 : 1; });
        return true;
    }
    default: return false;
    }
    if (e.mods.shift) return false;  // Shift + arrow: channel change
    s.ornCur = std::clamp(s.ornCur + dc, 0, OrnGrid::kSteps - 1);
    s.ornRow = std::clamp(s.ornRow + dr, 0, OrnGrid::kRows - 1);
    refreshGrid();
    return true;
}

// ------------------------------------------------------------------------------------------- layout

void ModulationWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // .mixbody content (border 2, margin 4, padding 8)
    const bool body = live();
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&hint_, &hint2_, &hint3_, &sep_, &grid_})
        w->setVisible(body);

    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    FlexItem slider{&len_, 0, 16};
    slider.grow = 1;  // input[type=range]: flex 1

    // Content width: the row (slider at 110 px), the settings of the sequences, and when on, the note and the grid.
    double contentW = on_.naturalWidth() + labLen_.naturalWidth() + kLenW + lenv_.naturalWidth() +
                      loop_.naturalWidth() + rate_.naturalWidth() + clear_.naturalWidth() + 6 * kGap;
    contentW = std::max(contentW, SequencesPanel::settingsWidth());
    if (body)
        contentW = std::max({contentW, hint_.naturalWidth(), hint2_.naturalWidth(), hint3_.naturalWidth(),
                             double(OrnGrid::kWidth)});

    double y = oy + 32 + kPad;
    std::vector<FlexItem> row = {btn(on_),  {&labLen_, labLen_.naturalWidth(), 16}, slider,
                                 {&lenv_, lenv_.naturalWidth(), 16}, btn(loop_), btn(rate_), btn(clear_)};
    y += flexRow(x, y, contentW, row);
    double bottom = y;
    if (body) {
        // #ornBody: .hint (margin-top 6), here with the notes of the sequence rows (D-271), 2 px rule up to the edges
        // of the window (margin 8px -8px), grid and the sequence rows inside it (D-270).
        y += 6;
        for (gemui::Label* l : {&hint_, &hint2_, &hint3_}) {
            l->setBox({x, y, l->naturalWidth(), 12});
            y += 12;
        }
        y += 8;
        sep_.setBox({x - kPad, y, contentW + 2 * kPad, 2});
        y += 2 + 8;
        grid_.setBox({x, y, double(OrnGrid::kWidth), double(OrnGrid::kHeight)});
        bottom = y + OrnGrid::kHeight;
    }
    seqx_.placeRows(x, grid_.box().y + OrnGrid::kSeqY, body);
    // Their settings always, then the LFO (D-257).
    bottom = seqx_.layout(x, bottom, contentW, kPad);
    bottom = lfo_.layout(x, bottom, contentW, kPad);

    // Window: content, padding of 8, border and margin of .gwin (6 on each side). Grown past the bottom of the
    // desktop (moved low, then switched on): raised to fit.
    const double w = contentW + 2 * kPad + 12;
    const double h = bottom + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) {
        place({ox, oy, w, h});
        const double over = oy + h - MainWindow::kHeight;
        if (over > 0) moveBy(0, -int(std::ceil(std::min(over, oy))));
    }
    repaint();
}

std::optional<Rect> ModulationWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "ornWin") return bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"ornOn", &on_},     {"ornLen", &len_},     {"ornLenv", &lenv_}, {"ornLoop", &loop_},
                           {"ornRate", &rate_}, {"ornClear", &clear_}, {"orngrid", &grid_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    if (id.substr(0, 3) == "lfo") return lfo_.rectOf(id);
    return seqx_.rectOf(id);
}

}  // namespace stym::pluginui
