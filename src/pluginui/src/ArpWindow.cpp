// STYM2149 — pluginui: Arpeggiator window (on/off, mode, sync, rate, division, octaves, table)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype: handlers of #arpOn, #arpModeSel, #chordSel, #arpOcts, #arpGlideRate, #arpLand,
// #arpRate, #arpLen, #arpgrid and #arpDivs, refreshArpGrid, markArpStep, the Arpeggiator part of refreshUI, setClkMode,
// openArpWin, closeArpWin, the table keys of the keydown handler. Specific to STYM2149: the Sync button (Arp Sync,
// sixth DAW test, D-256), Division in place of Rate when the arpeggio follows the tempo, in a menu button with 1/32T
// and 1/64, without Tick with Sync (seventh DAW test, D-287 to D-290), on the row of the mode (eighth DAW test, D-295).
#include "pluginui/ArpWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Pitch.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::Color;
using gemui::MenuItem;
using gemui::Rect;
using plugin::Global;
using plugin::Track;
namespace colors = gemui::colors;

namespace {

constexpr std::array<const char*, 6> kArpModes = {"Up", "Down", "UpDn", "Order", "Random", "Table"};  // ARPMODES
constexpr int kTableMode = int(ymengine::ArpMode::Table);

// #arpDivs in a menu button (D-290): the choices of ArpDivision in musical order (kArpDivisionOrder); divisions of
// the MIDI clock, or of the tempo with Sync (D-256), where Tick is not offered (D-287).
constexpr const char* kDivisionTip = "Arpeggio step: a division of the tempo, or of the MIDI clock (Tick: one clock pulse)";
constexpr const char* kSyncTip =
    "Sync to the tempo: one step per Division, at the host's tempo in a DAW, the sequencer's otherwise; "
    "off: Rate in Hz";
constexpr const char* kSyncMidiTip = "MIDI ext: the arpeggio follows the incoming MIDI clock";

constexpr std::array<const char*, 3> kOctaveTips = {
    "One octave: the arpeggio stays within the chord as written",
    "Two octaves: the chord repeats an octave higher before looping",
    "Three octaves: the widest sweep, at the cost of very high notes",
};

constexpr double kSliderW = 129;  // input[type=range]: default width, counted in the content width
// Gap between the items of a row, between the octave buttons, margin of a row, padding of .mixbody.
constexpr double kGap = 8, kGroupGap = 6, kRowMargin = 6, kPad = 8;
constexpr double kDivMargin = 8;  // between Sync and Division on the row of the mode (D-295)

std::optional<double> parseValue(const gemui::Slider& s, const std::string& txt) {
    size_t used = 0;
    const double raw = gemui::readDecimal(txt, &used);
    if (used == 0 || !std::isfinite(raw)) return std::nullopt;
    return std::clamp(std::round(raw), s.min(), s.max());
}

}  // namespace

// ------------------------------------------------------------------------------------------- ArpGrid

void ArpGrid::setState(const std::array<int8_t, kSteps>& table, int length, int play, int cursorStep, int cursorRow) {
    if (table == table_ && length == length_ && play == play_ && cursorStep == curStep_ && cursorRow == curRow_)
        return;
    table_ = table;
    length_ = length;
    play_ = play;
    curStep_ = cursorStep;
    curRow_ = cursorRow;
    repaint();
}

Rect ArpGrid::cellRect(int step, int row) const noexcept {
    const Rect r = bounds();
    return {r.x + kBorder + kLabelW + 1 + step * (kCellW + 1), r.y + kBorder + row * (kCellH + 1), kCellW, kCellH};
}

void ArpGrid::paint(gemui::Painter& p) {
    // Black background: border, rules between the cells, label column. Row 0 at the top is +24 semitones; a step is lit
    // on the row of its offset.
    const Rect r = bounds();
    p.fill(r, colors::kBlack);
    for (int row = 0; row < kRows; ++row) {
        const Rect lab{r.x + kBorder, r.y + kBorder + row * (kCellH + 1), kLabelW, kCellH};
        const std::string t = "+" + std::to_string(kRows - 1 - row);  // semitones in decimal
        const double tw = gemui::Font::width(t, 10);
        const double lh = gemui::Font::lineBox(t, 10).height();
        p.text(t, lab.x + (lab.w - tw) / 2, lab.y + (lab.h - lh) / 2, 10, colors::kWhite);
        for (int c = 0; c < kSteps; ++c) {
            const Rect cr = cellRect(c, row);
            const bool dim = c >= length_;
            const bool onc = !dim && table_[size_t(c)] == kRows - 1 - row;
            const bool play = c == play_;
            const bool cur = c == curStep_ && row == curRow_;
            // .dim: the whole cell at 35 % over the black background of the grid.
            auto shade = [dim](Color k) { return dim ? gemui::mix(k, colors::kBlack, 0.35) : k; };
            const Color bg = play ? (onc ? kPlayNote : colors::kGreen) : onc ? colors::kBlack
                                                                          : c % 4 == 0 ? kQtr : colors::kWhite;
            p.fill(cr, shade(bg));
            Rect inner = cr;  // padding box (inside the border)
            if (onc) {
                p.fill({cr.x, cr.y, cr.w, 1}, colors::kWhite);
                p.fill({cr.x, cr.bottom() - 1, cr.w, 1}, colors::kWhite);
                p.fill({cr.x, cr.y, 1, cr.h}, colors::kWhite);
                inner = {cr.x + 1, cr.y + 1, cr.w - 1, cr.h - 2};
            }
            if (cur) {
                // box-shadow inset 4 px: green frame inside the padding box (nothing in the middle if it is short).
                const int k = 4;
                const Color g = shade(colors::kGreen);
                p.fill({inner.x, inner.y, inner.w, std::min(k, inner.h)}, g);
                p.fill({inner.x, std::max(inner.y, inner.bottom() - k), inner.w, std::min(k, inner.h)}, g);
                p.fill({inner.x, inner.y, std::min(k, inner.w), inner.h}, g);
                p.fill({std::max(inner.x, inner.right() - k), inner.y, std::min(k, inner.w), inner.h}, g);
            }
        }
    }
}

int ArpGrid::stepAt(gemui::Point p, int* row) const noexcept {
    // Cell under the pointer; the rules and the label column are not cells (no dataset.col).
    const Rect r = bounds();
    const int rx = int(std::floor(p.x)) - (r.x + kBorder + kLabelW + 1);
    const int ry = int(std::floor(p.y)) - (r.y + kBorder);
    if (rx < 0 || ry < 0) return -1;
    const int c = rx / (kCellW + 1), k = ry / (kCellH + 1);
    if (c >= kSteps || k >= kRows || rx % (kCellW + 1) >= kCellW || ry % (kCellH + 1) >= kCellH) return -1;
    *row = k;
    return c;
}

void ArpGrid::mouseDown(const gemui::MouseEvent& e) {
    // pointerdown: writes the step if it is within the length.
    int row = 0;
    const int c = stepAt(e.pos, &row);
    if (c < 0 || c >= length_) return;
    if (onWrite) onWrite(c, kRows - 1 - row);
}

gemui::Pointer ArpGrid::pointerAt(gemui::Point p) const noexcept {
    int row = 0;
    const int c = stepAt(p, &row);
    return c >= 0 && c < length_ ? gemui::Pointer::Hand : gemui::Pointer::Arrow;  // .dim: cursor:default
}

// ------------------------------------------------------------------------------------------- construction

ArpWindow::ArpWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 177, 90});
}

void ArpWindow::build() {
    // Prototype ids of the sliders and buttons that MIDI learn can learn (D-146).
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&on_, "arpOn"}, {&mode_, "arpModeSel"}, {&sync_, "arpSync"}, {&rate_, "arpRate"},
            {&track_, "arpGlideRate"}, {&land_, "arpLand"}, {&len_, "arpLen"}, {&chord_, "chordSel"}})
        w->setId(id);
    setTitle("ARPEGGIATOR");
    // 12 px .hint: 6×6 font, at the width of the prototype's 12 px font (D-071).
    for (gemui::Label* l : {&hint_, &gridHint_}) {
        l->setFace(gemui::Face::kSmall);
        l->setColor(colors::kHint);
    }
    mode_.setTip("Order in which the table is played");
    rate_.setRange(4, 200, 1);
    rate_.setTip("Arpeggio speed in Hz");
    ratev_.setMinChars(3);  // #arpRatev{min-width:3ch}
    ratev_.setMask("999");
    track_.setTip("Slide follows: during a glissando, the arpeggio rate rises with the pitch");
    land_.setTip(
        "Land: on the last tick of a glissando, fall back to the root so the target note joins without a jump");
    div_.setTip(kDivisionTip);
    for (size_t i = 0; i < octs_.size(); ++i) {
        octs_[i].setText(std::to_string(i + 1));
        octs_[i].setTip(kOctaveTips[i]);
    }
    len_.setRange(1, ArpGrid::kSteps, 1);
    len_.setTip("Number of steps in the arpeggio table");
    lenv_.setMinChars(2);  // #arpLenv{min-width:2ch}
    lenv_.setMask("99");
    chord_.setTip("Fill the arpeggio table with a chord");

    std::vector<gemui::Widget*> ws = {&on_,    &hint_,  &labMode_, &mode_,   &sync_,   &labRate_, &rate_,
                                      &ratev_, &hz_,    &track_,   &land_,   &labDiv_, &labOct_,  &labSteps_,
                                      &len_,   &lenv_,  &chord_,   &grid_,   &gridHint_, &div_};
    for (Button& b : octs_) ws.push_back(&b);
    for (gemui::Widget* w : ws) add(*w);
}

void ArpWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };  // closeArpWin
    on_.onClick = [this] {
        if (drum()) return;  // drums: not applicable
        const bool now = !on();
        m_.set(Track::ArpOn, now ? 1.0f : 0.0f);
        if (!now) m_.host_.screenMidi(0xB0, 123, 0);  // allOff: the stopped arpeggio leaves nothing sounding
    };
    mode_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < int(kArpModes.size()); ++i) {
            MenuItem m;
            m.label = kArpModes[size_t(i)];
            m.pick = [this, i] { editArp([i](ymengine::TrackArp& a) { a.mode = ymengine::ArpMode(i); }); };
            items.push_back(std::move(m));
        }
        m_.trackPopup(mode_, std::move(items), int(arp().mode));
    };
    sync_.onClick = [this] {
        if (m_.iv(Global::ArpClock) == 1) return;  // MIDI ext: the MIDI clock
        m_.set(Global::ArpSync, m_.iv(Global::ArpSync) == 1 ? 0.0f : 1.0f);
    };
    m_.bindSlider(rate_, [this](double v) { m_.set(Track::ArpRate, float(v)); });
    ratev_.onCommit = [this](const std::string& t) {
        const auto v = parseValue(rate_, t);
        if (!v) return false;
        m_.set(Track::ArpRate, float(*v));
        return true;
    };
    track_.onClick = [this] { editArp([](ymengine::TrackArp& a) { a.track = !a.track; }); };
    land_.onClick = [this] { editArp([](ymengine::TrackArp& a) { a.land = !a.land; }); };
    div_.onClick = [this] {
        // sendClk: the musical order; Tick only on the MIDI clock (D-287), shown unchecked if a project kept it.
        const bool midi = m_.iv(Global::ArpClock) == 1;
        const int now = m_.iv(Global::ArpDivision);
        const plugin::ParamSpec& s = plugin::paramSpec(plugin::paramIndex(Global::ArpDivision));
        std::vector<MenuItem> items;
        int checked = -1;
        for (const int i : plugin::kArpDivisionOrder) {
            if (i == plugin::kArpDivisionTick && !midi) continue;
            if (i == now) checked = int(items.size());
            MenuItem m;
            m.label = std::string(s.choices[size_t(i)]);
            m.pick = [this, i] { m_.set(Global::ArpDivision, float(i)); };
            items.push_back(std::move(m));
        }
        m_.popup(div_, std::move(items), checked);
    };
    for (int k = 0; k < int(octs_.size()); ++k)
        octs_[size_t(k)].onClick = [this, k] {
            editArp([k](ymengine::TrackArp& a) { a.octaves = uint8_t(k + 1); });
        };

    // Step count: state that cannot be automated (the held slider is not read back).
    len_.onGestureStart = [this] { m_.dragging_ = &len_; };
    len_.onGestureEnd = [this] {
        m_.dragging_ = nullptr;
        m_.refresh();
    };
    len_.onChange = [this](double v) { setLength(int(v)); };
    lenv_.onCommit = [this](const std::string& t) {
        const auto v = parseValue(len_, t);
        if (!v) return false;
        setLength(int(*v));
        return true;
    };
    chord_.onClick = [this] {
        // An action, not a state: the menu has no checked entry; the table takes the chord, the length its number of
        // notes.
        std::vector<MenuItem> items;
        for (int i = 0; i < int(ymengine::kChords.size()); ++i) {
            MenuItem m;
            m.label = std::string(ymengine::kChords[size_t(i)].name);
            m.pick = [this, i] {
                const ymengine::Chord& c = ymengine::kChords[size_t(i)];
                editArp([&c](ymengine::TrackArp& a) {
                    a.table.fill(0);
                    for (int j = 0; j < c.count; ++j) a.table[size_t(j)] = c.offsets[size_t(j)];
                    a.length = c.count;
                });
            };
            items.push_back(std::move(m));
        }
        gemui::pickOnlyWhile(items, m_.stillFocused());  // the table of the track the menu was opened on
        // chordCols = 1: the 39 chords in as many columns as needed.
        gemui::MenuList::open(m_.desk_, std::move(items), -1, chord_.box(), false, true);
    };
    grid_.onWrite = [this](int step, int offset) {
        editArp([step, offset](ymengine::TrackArp& a) { a.table[size_t(step)] = int8_t(offset); });
    };
}

// ------------------------------------------------------------------------------------------- state

bool ArpWindow::drum() const noexcept { return m_.drumTrack(); }
bool ArpWindow::on() const noexcept { return m_.iv(Track::ArpOn) != 0 && !drum(); }
const ymengine::TrackArp& ArpWindow::arp() const noexcept { return m_.extras_.tracks[size_t(m_.cur_)].arp; }

// Undo (D-129): a press in this window arms a step of the focused track (MainWindow::pressed, armCur); pushCur pushes
// it after the first setting that changes the track, and the other settings of the same gesture add nothing.
void ArpWindow::editArp(const std::function<void(ymengine::TrackArp&)>& fn) {
    const int t = m_.cur_;
    m_.host_.editExtras([t, &fn](plugin::NonParamState& s) { fn(s.tracks[size_t(t)].arp); });
    m_.pullExtras();
    m_.pushCur();
}

void ArpWindow::setLength(int n) {
    n = std::clamp(n, 1, ArpGrid::kSteps);
    if (n == arp().length) return;
    editArp([n](ymengine::TrackArp& a) { a.length = uint8_t(n); });
}

void ArpWindow::show() {
    // openArpWin: the window comes to the front.
    open();
    refresh();
    m_.syncWindowButtons();
}

void ArpWindow::refresh() {
    // refreshWinTitles, Arpeggiator part of refreshUI.
    setTitle("ARPEGGIATOR - " + m_.trackName(m_.cur_));
    if (!isOpen()) return;
    const bool dr = drum(), o = on();
    const ymengine::TrackArp& a = arp();
    on_.setOn(o);
    on_.setText(o ? "On" : "Off");
    on_.setEnabled(!dr);
    on_.setTip(dr ? "Percussion tracks do not use the arpeggiator" : "Enable the arpeggiator for this channel");
    mode_.setText(kArpModes[size_t(std::min<int>(int(a.mode), kTableMode))]);
    // Sync: lit when the arpeggio follows the tempo; with MIDI ext, lit and greyed out (the MIDI clock).
    const bool midi = m_.iv(Global::ArpClock) == 1;
    sync_.setOn(midi || m_.iv(Global::ArpSync) == 1);
    sync_.setEnabled(!midi);
    sync_.setTip(midi ? kSyncMidiTip : kSyncTip);
    const int rate = m_.iv(Track::ArpRate);
    if (m_.dragging_ != &rate_) rate_.setValue(rate);
    if (!ratev_.editing()) ratev_.setText(std::to_string(rate));
    track_.setOn(a.track);
    land_.setOn(a.land);
    const plugin::ParamSpec& divSpec = plugin::paramSpec(plugin::paramIndex(Global::ArpDivision));
    const int div = std::clamp(m_.iv(Global::ArpDivision), 0, int(divSpec.choices.size()) - 1);
    div_.setText(std::string(divSpec.choices[size_t(div)]));
    for (int k = 0; k < int(octs_.size()); ++k) octs_[size_t(k)].setOn(k + 1 == a.octaves);
    if (m_.dragging_ != &len_) len_.setValue(a.length);
    if (!lenv_.editing()) lenv_.setText(std::to_string(a.length));
    refreshGrid();
    layout();
}

void ArpWindow::refreshGrid() {
    const ymengine::TrackArp& a = arp();
    const EditorSession& s = m_.host_.session();
    grid_.setState(a.table, a.length, play_, s.arpCur, s.arpRow);
}

void ArpWindow::refreshEngineStatus(const plugin::EngineStatus& t) {
    // markArpStep: played step of the table (astep).
    if (t.arpStep == play_) return;
    play_ = t.arpStep;
    if (isOpen()) refreshGrid();
}

bool ArpWindow::tableKey(const gemui::KeyEvent& e) {
    if (gemui::FloatWindow::topmost(m_) != this || !on()) return false;
    EditorSession& s = m_.host_.session();
    int dc = 0, dr = 0;
    switch (e.key) {
    case gemui::Key::Left: dc = -1; break;
    case gemui::Key::Right: dc = 1; break;
    case gemui::Key::Up: dr = -1; break;
    case gemui::Key::Down: dr = 1; break;
    case gemui::Key::Enter: {
        // The step under the cursor takes the row; the length grows up to it. One undo step (the prototype pushed the
        // undo step before writing: the key could not be undone; D-129).
        const int c = s.arpCur, off = ArpGrid::kRows - 1 - s.arpRow;
        m_.armCur();
        editArp([c, off](ymengine::TrackArp& a) {
            a.table[size_t(c)] = int8_t(off);
            if (c >= a.length) a.length = uint8_t(c + 1);
        });
        return true;
    }
    default: return false;
    }
    if (e.mods.shift) return false;  // Shift + arrow: channel change
    s.arpCur = std::clamp(s.arpCur + dc, 0, ArpGrid::kSteps - 1);
    s.arpRow = std::clamp(s.arpRow + dr, 0, ArpGrid::kRows - 1);
    refreshGrid();
    return true;
}

// ------------------------------------------------------------------------------------------- layout

void ArpWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // .mixbody content (border 2, margin 4, padding 8)
    const bool body = on();
    const bool table = arp().mode == ymengine::ArpMode::Table;
    // setClkMode: Division in place of Rate with the MIDI clock, and with Sync (D-256).
    const bool divided = m_.iv(Global::ArpClock) == 1 || m_.iv(Global::ArpSync) == 1;

    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labMode_, &mode_, &sync_}) w->setVisible(body);
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labRate_, &rate_, &ratev_, &hz_, &track_, &land_})
        w->setVisible(body && !divided);
    labDiv_.setVisible(body && divided);
    div_.setVisible(body && divided);
    labOct_.setVisible(body && !table);
    for (Button& b : octs_) b.setVisible(body && !table);
    for (gemui::Widget* w :
         std::initializer_list<gemui::Widget*>{&labSteps_, &len_, &lenv_, &chord_, &grid_, &gridHint_})
        w->setVisible(body && table);

    auto lab = [](gemui::Label& l) { return FlexItem{&l, l.naturalWidth(), 16}; };
    auto btn = [](Button& b) { return FlexItem{&b, b.naturalWidth(), double(b.naturalHeight())}; };
    auto group = [](const auto& bs) {
        double w = 0;
        for (const Button& b : bs) w += b.naturalWidth();
        return w + kGroupGap * double(bs.size() - 1);
    };
    auto slider = [](gemui::Slider& s) {
        FlexItem it{&s, 0, 16};
        it.grow = 1;  // input[type=range]: flex 1
        return it;
    };

    // Content width (width:max-content): the widest of the rows shown, sliders at their default width; in Table mode,
    // the grid. Each row: its items and kGap between them.
    const double rateValW = ratev_.naturalWidth() + hz_.naturalWidth();
    double contentW = on_.naturalWidth() + kGap + hint_.naturalWidth();
    if (body) {
        // Division on the row of the mode (eighth DAW test, item 94, D-295), 8 px further from Sync.
        const double divW = divided ? kGap + kDivMargin + labDiv_.naturalWidth() + kGap + div_.naturalWidth() : 0;
        contentW = std::max(contentW, labMode_.naturalWidth() + kGap + mode_.naturalWidth() + kGap +
                                          sync_.naturalWidth() + divW);
        if (!divided)
            contentW = std::max(contentW, labRate_.naturalWidth() + kGap + kSliderW + kGap + rateValW + kGap +
                                              track_.naturalWidth() + kGap + land_.naturalWidth());
        if (table) {
            contentW = std::max({contentW, labSteps_.naturalWidth() + kGap + kSliderW + kGap + lenv_.naturalWidth(),
                                 chord_.naturalWidth(), double(ArpGrid::kWidth), gridHint_.naturalWidth()});
        } else {
            contentW = std::max(contentW, labOct_.naturalWidth() + kGap + group(octs_));
        }
    }

    // Rows (.row: 6 px margins collapsed between neighbouring rows).
    double y = oy + 32 + kPad + kRowMargin;
    std::vector<FlexItem> r1 = {btn(on_), {&hint_, hint_.naturalWidth(), 12 + 6}};
    const double h1 = flexRow(x, y, contentW, r1);
    hint_.setBox({r1[1].box.x, r1[1].box.y + 6, r1[1].box.w, 12});  // .hint: margin-top 6
    y += h1 + kRowMargin;
    double bottom = y;  // bottom of the content, bottom margin of the last row included
    if (body) {
        std::vector<FlexItem> rm = {lab(labMode_), btn(mode_), btn(sync_)};
        if (divided) {
            FlexItem l = lab(labDiv_);
            l.marginLeft = kDivMargin;
            rm.push_back(l);
            rm.push_back(btn(div_));
        }
        y += flexRow(x, y, contentW, rm) + kRowMargin;
        if (!divided) {
            std::vector<FlexItem> rr = {lab(labRate_), slider(rate_), {nullptr, rateValW, 16}, btn(track_), btn(land_)};
            const double h = flexRow(x, y, contentW, rr);
            ratev_.setBox({rr[2].box.x, rr[2].box.y, ratev_.naturalWidth(), 16});
            hz_.setBox({rr[2].box.x + ratev_.naturalWidth(), rr[2].box.y, hz_.naturalWidth(), 16});
            y += h + kRowMargin;
        }
        if (!table) {
            std::vector<FlexItem> ro = {lab(labOct_), {nullptr, group(octs_), 24}};
            const double h = flexRow(x, y, contentW, ro);
            double bx = ro[1].box.x;
            for (Button& b : octs_) {
                b.setBox({bx, ro[1].box.y, b.naturalWidth(), double(b.naturalHeight())});
                bx += b.naturalWidth() + kGroupGap;
            }
            y += h + kRowMargin;
            bottom = y;
        } else {
            // #arpTablePanel: Steps (16 px row), Chord... (.btngroup without margin), grid (margin-top 8), .hint
            // (margin-top 6); no margin under the panel.
            std::vector<FlexItem> rs = {lab(labSteps_), slider(len_), {&lenv_, lenv_.naturalWidth(), 16}};
            y += flexRow(x, y, contentW, rs) + kRowMargin;
            chord_.setBox({x, y, chord_.naturalWidth(), double(chord_.naturalHeight())});
            y += chord_.naturalHeight() + 8;
            grid_.setBox({x, y, double(ArpGrid::kWidth), double(ArpGrid::kHeight)});
            y += ArpGrid::kHeight + 6;
            gridHint_.setBox({x, y, gridHint_.naturalWidth(), 12});
            bottom = y + 12;
        }
    }

    // Window: content, padding of 8, border and margin of .gwin (6 on each side).
    const double w = contentW + 2 * kPad + 12;
    const double h = bottom + kPad + 6 - oy;
    if (std::abs(h - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, h});
    repaint();
}

std::optional<Rect> ArpWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "arpWin") return bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"arpOn", &on_},         {"arpModeSel", &mode_},   {"arpSync", &sync_},
                           {"arpRate", &rate_},     {"arpRatev", &ratev_},    {"arpGlideRate", &track_},
                           {"arpLand", &land_},     {"arpLen", &len_},        {"arpLenv", &lenv_},
                           {"chordSel", &chord_},   {"arpgrid", &grid_},      {"arpDiv", &div_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    auto indexed = [&](std::string_view prefix, int first, int count) -> int {
        if (id.substr(0, prefix.size()) != prefix || id.size() != prefix.size() + 1) return -1;
        const int i = id.back() - '0' - first;
        return i >= 0 && i < count ? i : -1;
    };
    if (const int k = indexed("arpOct", 1, int(octs_.size())); k >= 0)
        return octs_[size_t(k)].visible() ? std::optional<Rect>(octs_[size_t(k)].bounds()) : std::nullopt;
    return std::nullopt;
}

}  // namespace stym::pluginui
