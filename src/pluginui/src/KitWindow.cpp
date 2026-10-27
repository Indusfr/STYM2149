// STYM2149 — pluginui: Drum Kit window (instruments, settings, Audition, Reset, Kits, kit name)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype: KITSPEC, KITHELP, buildKitSliders, kitLabel, refreshKitName, refreshKit,
// refreshKitEnable, openKit, closeKit, the handlers of #kitInst, #kitPlay, #kitReset and #kitBank.
#include "pluginui/KitWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "plugincore/Parameters.h"
#include "pluginui/Layout.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Button;
using gemui::MenuItem;
using gemui::Rect;
using ymengine::DrumParams;
using ymengine::KitParams;
namespace colors = gemui::colors;

namespace {

// Padding of the content, gap between the instrument buttons, gap between the setting rows (px).
constexpr double kPad = 8, kInstGap = 6, kRowGap = 6;
constexpr double kLabelW = 52;   // label{min-width:52px}
constexpr double kRangeW = 129;  // natural width of a Chromium input[type=range] (max-content of the row)
constexpr int kCols = 3;         // KITCOLS: field of three characters

// KITSPEC: settings of each instrument (key, label, range) and the DrumParams field that holds them.
struct Spec {
    std::string_view key, label;          // key of KITHELP, label of the row
    int min = 0, max = 0;                 // slider range, whole numbers
    double DrumParams::*field = nullptr;  // member of DrumParams that holds the value
};
struct InstSpec {
    std::string_view name, note;  // button label; note name that plays it on a Chipdrum track (in its tooltip)
    int count = 0;                // rows used (4 or 5)
    std::array<Spec, KitWindow::kMaxRows> rows;
};
constexpr Spec kVol{"vol", "Vol", 0, 100, &DrumParams::vol};  // last row of every instrument
constexpr std::array<InstSpec, ymengine::kDrumSlots> kInst = {{
    {"Kick", "C", 5,
     {{{"pitch", "Pitch", 25, 90, &DrumParams::a},
       {"sweep", "Sweep", 20, 300, &DrumParams::b},
       {"dec", "Decay", 4, 20, &DrumParams::dec},
       {"noise", "Noise", 0, 100, &DrumParams::noise},
       kVol}}},
    {"Snare", "C#", 4,
     {{{"body", "Body", 80, 400, &DrumParams::a},
       {"tone", "Tone", 0, 70, &DrumParams::b},
       {"dec", "Decay", 6, 30, &DrumParams::dec},
       kVol}}},
    {"Clap", "D", 4,
     {{{"space", "Space", 6, 20, &DrumParams::a},
       {"color", "Color", 15, 80, &DrumParams::b},
       {"tail", "Tail", 10, 60, &DrumParams::dec},
       kVol}}},
    {"Hat", "D#", 4,
     {{{"metal", "Metal", 0, 100, &DrumParams::a},
       {"tune", "Tune", 60, 160, &DrumParams::b},
       {"dec", "Decay", 15, 120, &DrumParams::dec},
       kVol}}},
    {"Open Hat", "E", 4,
     {{{"metal", "Metal", 0, 100, &DrumParams::a},
       {"tune", "Tune", 60, 160, &DrumParams::b},
       {"dec", "Decay", 3, 30, &DrumParams::dec},
       kVol}}},
    {"Closed Hat", "F", 4,
     {{{"metal", "Metal", 0, 100, &DrumParams::a},
       {"tune", "Tune", 60, 160, &DrumParams::b},
       {"dec", "Decay", 30, 250, &DrumParams::dec},
       kVol}}},
    {"Tom 1", "F#", 5,
     {{{"pitch", "Pitch", 60, 200, &DrumParams::a},
       {"sweep", "Sweep", 20, 150, &DrumParams::b},
       {"dec", "Decay", 4, 20, &DrumParams::dec},
       {"noise", "Noise", 0, 100, &DrumParams::noise},
       kVol}}},
    {"Tom 2", "G", 5,
     {{{"pitch", "Pitch", 80, 260, &DrumParams::a},
       {"sweep", "Sweep", 20, 150, &DrumParams::b},
       {"dec", "Decay", 4, 20, &DrumParams::dec},
       {"noise", "Noise", 0, 100, &DrumParams::noise},
       kVol}}},
}};

// KITHELP: what each parameter sets.
std::string_view help(std::string_view key) {
    if (key == "pitch") return "Starting pitch of the tone burst";
    if (key == "sweep") return "How far the pitch falls during the hit: low is flat, high is a deep boom";
    if (key == "dec") return "Decay time: how long the hit rings";
    if (key == "noise") return "Amount of noise mixed with the tone";
    if (key == "body") return "Pitch of the tone part";
    if (key == "tone") return "Balance between tone and noise";
    if (key == "space") return "Gap between the bursts that make up the clap";
    if (key == "color") return "Noise colour: low is dull, high is crisp";
    if (key == "tail") return "Length of the tail after the last burst";
    if (key == "metal") return "Metallic content: high gives a ringing cymbal";
    if (key == "tune") return "Pitch of the metallic ring";
    if (key == "vol") return "Level of this drum in the kit";
    return {};
}

int digits(int v) { return int(std::to_string(v).size()); }  // characters of v, a minus sign included

// Order of the Kits menu: Chromium's localeCompare(…, 'en', { sensitivity: 'base' }), which for these names is the ICU
// root collation: spaces, punctuation, digits, then letters ignoring case.
bool bankLess(std::string_view a, std::string_view b) {
    auto key = [](char c) {
        const auto u = static_cast<unsigned char>(c);
        const int cls = c == ' ' ? 0 : std::isdigit(u) ? 2 : std::isalpha(u) ? 3 : 1;
        return std::pair<int, int>{cls, std::tolower(u)};
    };
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const auto ka = key(a[i]), kb = key(b[i]);
        if (ka != kb) return ka < kb;
    }
    return a.size() < b.size();
}

}  // namespace

std::string kitLabel(const KitParams& kit) {
    for (const auto& bank : ymengine::kKitBanks)
        if (bank.kit == kit) return std::string(bank.name);
    return "Custom";
}

// ------------------------------------------------------------------------------------------- construction

KitWindow::KitWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), 320, 90});  // provisional size: layout() sets the real one at the first refresh
}

void KitWindow::build() {
    // Prototype ids of the sliders and buttons that MIDI learn can learn.
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&play_, "kitPlay"}, {&reset_, "kitReset"}, {&bank_, "kitBank"}})
        w->setId(id);
    setTitle("DRUM KIT");
    hint_.setFace(gemui::Face::kSmall);
    hint_.setColor(colors::kHint);
    name_.setFace(gemui::Face::kSmall);
    name_.setColor(colors::kHint);
    play_.setTip("Preview this drum");
    reset_.setTip("Restore the factory values for this drum");
    bank_.setTip("Switch drum kit");
    add(off_);
    add(offRule_);
    add(body_);
    for (size_t k = 0; k < inst_.size(); ++k) {
        Button& b = inst_[k];
        b.setText(std::string(kInst[k].name));
        b.setTip(std::string(kInst[k].name) + ": edit this drum. It answers to " + std::string(kInst[k].note) +
                 " on a Chipdrum track.");
        body_.add(b);
    }
    for (Row& r : rows_) {
        r.value.setTip("Click to type a value");
        body_.add(r.label);
        body_.add(r.slider);
        body_.add(r.value);
    }
    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&sep_, &play_, &reset_, &bank_, &hint_, &name_})
        body_.add(*w);
}

void KitWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };  // closeKit
    for (size_t k = 0; k < inst_.size(); ++k)
        inst_[k].onClick = [this, k] {
            m_.host_.session().kitInst = int(k);
            refresh();
        };
    for (int i = 0; i < kMaxRows; ++i) {
        Row& r = rows_[size_t(i)];
        // One undo step per drag: the first move makes it; each move rebuilds the kit.
        r.slider.onGestureStart = [this, i] {
            rows_[size_t(i)].armed = true;
            m_.dragging_ = &rows_[size_t(i)].slider;
        };
        r.slider.onGestureEnd = [this, i] {
            rows_[size_t(i)].armed = false;
            m_.dragging_ = nullptr;
            m_.refresh();
        };
        r.slider.onChange = [this, i](double v) {
            Row& row = rows_[size_t(i)];
            const bool undo = row.armed;
            row.armed = false;
            setField(i, int(std::lround(v)), undo);
        };
        // Direct entry: number rounded and brought back into the range; one undo step (the prototype made none and left
        // the kit name lagging behind).
        r.value.onCommit = [this, i](const std::string& t) {
            size_t used = 0;
            const double raw = gemui::readDecimal(t, &used);
            if (used == 0 || !std::isfinite(raw)) return false;
            const Spec& s = kInst[size_t(inst())].rows[size_t(i)];
            setField(i, std::clamp(int(std::lround(raw)), s.min, s.max), true);
            return true;
        };
    }
    // Audition: a command to the engine on the audio thread, which plays the instrument once.
    play_.onClick = [this] { m_.host_.seqCommand(Host::SeqCommand::DrumPreview, inst()); };
    // Reset: the instrument of the default kit, the first factory bank ("Classic").
    reset_.onClick = [this] {
        const int k = inst();
        editKit(true, [k](KitParams& kit) { kit.inst[size_t(k)] = ymengine::kKitBanks[0].kit.inst[size_t(k)]; });
    };
    bank_.onClick = [this] {
        std::vector<const ymengine::KitBank*> banks;
        for (const auto& b : ymengine::kKitBanks) banks.push_back(&b);
        std::stable_sort(banks.begin(), banks.end(),
                         [](const ymengine::KitBank* a, const ymengine::KitBank* b) { return bankLess(a->name, b->name); });
        std::vector<MenuItem> items;
        for (const ymengine::KitBank* b : banks) {
            MenuItem m;
            m.label = std::string(b->name);
            m.pick = [this, b] { editKit(true, [b](KitParams& kit) { kit = b->kit; }); };
            items.push_back(std::move(m));
        }
        m_.popup(bank_, std::move(items), -1);
    };
}

// ------------------------------------------------------------------------------------------- state

bool KitWindow::chipdrum() const noexcept { return m_.generator() == int(ymengine::Generator::Chipdrum); }
int KitWindow::inst() const noexcept {
    return std::clamp(m_.host_.session().kitInst, 0, ymengine::kDrumSlots - 1);
}
int KitWindow::rows() const noexcept { return kInst[size_t(inst())].count; }

void KitWindow::editKit(bool undo, const std::function<void(KitParams&)>& fn) {
    if (undo) {  // pushKitUndo: the kit as it is now, before the change
        UndoEntry e;
        e.kind = UndoEntry::Kind::Kit;
        m_.host_.session().undo.push(m_.undoEntryLike(e));
    }
    // The host publishes the new kit to the audio thread, where the engine computes it again (Engine::setKit).
    m_.host_.editExtras([&fn](plugin::NonParamState& s) { fn(s.kit); });
    m_.pullExtras();
}

void KitWindow::setField(int row, int v, bool undo) {
    const int k = inst();
    const Spec& s = kInst[size_t(k)].rows[size_t(row)];
    v = std::clamp(v, s.min, s.max);
    if (m_.extras_.kit.inst[size_t(k)].*s.field == double(v)) return;  // nothing changes: no undo step
    editKit(undo, [k, &s, v](KitParams& kit) { kit.inst[size_t(k)].*s.field = double(v); });
}

void KitWindow::show() {
    // openKit: the window comes to the front.
    open();
    refresh();
    m_.syncWindowButtons();
}

void KitWindow::refresh() {
    // refreshKit, refreshKitEnable.
    if (!isOpen()) return;
    const int k = inst();
    const InstSpec& spec = kInst[size_t(k)];
    const DrumParams& p = m_.extras_.kit.inst[size_t(k)];
    const bool on = chipdrum();
    body_.setOpacity(on ? 1.0 : gemui::kOpacityInert);
    body_.setInert(!on);
    for (size_t i = 0; i < inst_.size(); ++i) inst_[i].setOn(int(i) == k);
    for (int i = 0; i < kMaxRows; ++i) {
        Row& r = rows_[size_t(i)];
        const bool used = i < spec.count;
        for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&r.label, &r.slider, &r.value})
            w->setVisible(used);
        if (!used) continue;
        const Spec& s = spec.rows[size_t(i)];
        const std::string tip =
            std::string(help(s.key)) + " (" + std::to_string(s.min) + " to " + std::to_string(s.max) + ")";
        r.label.setText(std::string(s.label));
        r.label.setTip(tip);
        r.slider.setTip(tip);
        r.slider.setRange(s.min, s.max, 1);
        // Field of three characters: the value is right-aligned, its dashes on the columns of the range.
        const int cols = std::max(digits(s.min), digits(s.max));
        r.value.setMinChars(cols);
        r.value.setPadChars(kCols - cols);
        const int v = int(std::lround(p.*s.field));
        if (m_.dragging_ != &r.slider) r.slider.setValue(v);
        if (!r.value.editing()) r.value.setText(std::to_string(v));
    }
    name_.setText("Kit: " + kitLabel(m_.extras_.kit));
    layout();
}

// ------------------------------------------------------------------------------------------- layout

void KitWindow::layout() {
    const double ox = box().x, oy = box().y;
    const double x = ox + 14;  // .mixbody content (border 2, margin 4, padding 8)
    const bool on = chipdrum();
    off_.setVisible(!on);
    offRule_.setVisible(!on);

    // Content width (width:max-content): the widest of the rows on one line. The instruments row wins (542 px);
    // min-width:320px of #kitWin therefore has no effect.
    double instW = kInstGap * double(inst_.size() - 1);
    for (const Button& b : inst_) instW += b.naturalWidth();
    const double bottomW = play_.naturalWidth() + reset_.naturalWidth() + bank_.naturalWidth() +
                           hint_.naturalWidth() + name_.naturalWidth() + 4 * kPad;
    const double rowW = kLabelW + kPad + kRangeW + kPad + kCols * gemui::Font::advance(16);
    double contentW = std::max({instW, bottomW, rowW});
    if (!on) contentW = std::max(contentW, off_.naturalWidth());

    double y = oy + 32;
    if (!on) {
        // #kitOff: .hint (margin-top 6), padding 8, 2 px bottom rule across the whole width.
        y += 6;
        off_.setBox({x, y + kPad, contentW, 12});
        const double h = 12.0 * off_.lineCount();
        off_.setBox({x, y + kPad, contentW, h});
        y += kPad + h + kPad;
        offRule_.setBox({ox + 6, y, contentW + 2 * kPad, 2});
        y += 2;
    }
    const double top = y;

    // #kitInst: buttons 6 px apart.
    y += kPad;
    std::vector<FlexItem> insts;
    for (Button& b : inst_) insts.push_back({&b, b.naturalWidth(), double(b.naturalHeight())});
    y += flexRow(x, y, contentW, insts, kInstGap);

    // #kitSliders (margin-top 8): 16 px rows 6 px apart; label, slider (rest of the space), value.
    y += kPad;
    const double valW = kCols * gemui::Font::advance(16);
    for (int i = 0; i < rows(); ++i) {
        Row& r = rows_[size_t(i)];
        if (i) y += kRowGap;
        r.label.setBox({x, y, std::max(kLabelW, r.label.naturalWidth()), 16});
        const double sx = x + std::max(kLabelW, r.label.naturalWidth()) + kPad;
        r.slider.setBox({sx, y, x + contentW - valW - kPad - sx, 16});
        r.value.setBox({x + contentW - valW, y, valW, 16});
        y += 16;
    }

    // 2 px rule up to the edges of the window (margin 8px -8px), then the row of buttons.
    y += kPad;
    sep_.setBox({ox + 6, y, contentW + 2 * kPad, 2});
    y += 2 + kPad;
    std::vector<FlexItem> bottom = {{&play_, play_.naturalWidth(), double(play_.naturalHeight())},
                                    {&reset_, reset_.naturalWidth(), double(reset_.naturalHeight())},
                                    {&bank_, bank_.naturalWidth(), double(bank_.naturalHeight())},
                                    {&hint_, hint_.naturalWidth(), 18},
                                    {&name_, name_.naturalWidth(), 18}};
    bottom[4].autoMargin = true;  // #kitName{margin-left:auto}
    const double h = flexRow(x, y, contentW, bottom, kPad, false);
    // .hint: margin-top 6 in its centred 18 px box.
    for (FlexItem* it : {&bottom[3], &bottom[4]}) it->w->setBox({it->box.x, it->box.y + 6, it->box.w, 12});
    y += h + kPad;
    body_.setBox({ox + 6, top, contentW + 2 * kPad, y - top});

    // Window: content, padding of 8, border and margin of .gwin (6 on each side).
    const double w = contentW + 2 * kPad + 12;
    const double wh = y + 6 - oy;
    if (std::abs(wh - box().h) > 0.01 || std::abs(w - box().w) > 0.01) place({ox, oy, w, wh});
    repaint();
}

std::optional<Rect> KitWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "kitWin") return bounds();
    if (id.substr(0, 7) == "kitInst" && id.size() == 8 && id[7] >= '0' && id[7] < '0' + ymengine::kDrumSlots)
        return inst_[size_t(id[7] - '0')].bounds();
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {{"kitOff", &off_},     {"kitBody", &body_}, {"kitPlay", &play_},
                           {"kitReset", &reset_}, {"kitBank", &bank_}, {"kitName", &name_}};
    for (const Named& n : named)
        if (id == n.id) return n.w->visible() ? std::optional<Rect>(n.w->bounds()) : std::nullopt;
    return std::nullopt;
}

}  // namespace stym::pluginui
