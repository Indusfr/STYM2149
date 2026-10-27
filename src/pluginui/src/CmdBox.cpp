// STYM2149 — pluginui: box of the step commands (CMD row of the Step Sequencer)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/CmdBox.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

#include "gemui/Alert.h"
#include "gemui/Font.h"
#include "gemui/Menu.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using gemui::Font;
using ymengine::SeqCmd;

namespace {

// Margins of the alert boxes (Alert.cpp): text of 16 px, paragraph followed by 16 px, buttons 18 px below, 14 px apart.
constexpr int kPadX = 32, kPadY = 24, kParagraphGap = 16, kButtonsGap = 18, kButtonSpacing = 14;
constexpr int kRowGap = 10, kColGap = 12, kSliderW = 208;

// Form of a row: one slider and a decimal field; a slide adds its Up/Down button, its slider and field giving the
// speed from 0 to 15 (24/10/2026, D-391; before: one slider over xy from 0 to 255 and a hexadecimal field, D-385).
enum class Form { Number, Slide };
struct Kind {
    const char* label;
    const char* tip;
    int min, max;  // of the slider
    Form form = Form::Number;
};
// Rows in the order of CmdBox::kOrder.
constexpr std::array<Kind, CmdBox::kOrder.size()> kKinds = {{
    {"Ticks per Line", "Ticks per Line (Sxx): replay ticks of each line from this step (Ticks timing)", 1,
     ymengine::kSpeedMax},
    {"Sync Mark", "Sync Mark (Zxx): silent mark written in the SNDH export for a demo", 0, 255},
    {"Volume Slide",
     "Volume Slide (Axy) of an older project: the volume goes up or down this many quarter steps per replay tick; 0 "
     "stops it. The Slide of the VOL cell replaces it",
     0, 15, Form::Slide},
    {"Buzzer Slide",
     "Buzzer Slide (Bxy): the envelope goes up or down this many 1/16 semitones per replay tick; 0 stops it", 0, 15,
     Form::Slide},
    {"Noise Slide",
     "Noise Slide (Nxy): the noise goes up or down this many quarter period steps per replay tick; 0 stops it", 0, 15,
     Form::Slide},
}};

// A slide's xy: x up, y down; one speed of 15 at most in one direction (an older xy with both: their difference).
int slideSpeedOf(int xy) noexcept { return std::min(std::abs((xy >> 4) - (xy & 15)), 15); }
bool slideUp(int xy) noexcept { return (xy >> 4) >= (xy & 15); }
int slideXy(int speed, bool up) noexcept { return up ? speed << 4 : speed; }

constexpr int kCurves = int(ymseq::kSlideCurveNames.size());
const char* const kCurveTip =
    "Curve (16 channels): Up, Down and the shapes of the CRV row, from this step to the next command of its kind";

// Text of a slide's button: its direction, then the short name of its shape (CRV row) when it is not Linear.
std::string dirText(bool up, int curve) {
    std::string t = up ? "Up" : "Down";
    if (curve > 0) t += " " + std::string(ymseq::kSlideCurveShort[size_t(curve)]);
    return t;
}

// Typed number: digits only (the masks of the fields); nothing for an empty text.
std::optional<int> number(const std::string& t) {
    if (t.empty()) return std::nullopt;
    int v = 0;
    for (const char c : t) {
        if (c < '0' || c > '9') return std::nullopt;
        v = std::min(v * 10 + (c - '0'), 100000);
    }
    return v;
}

}  // namespace

size_t CmdBox::row(SeqCmd k) noexcept {
    for (size_t i = 0; i < kOrder.size(); ++i)
        if (kOrder[i] == k) return i;
    return 0;
}

CmdBox& CmdBox::open(gemui::Desktop& d, Spec spec) {
    auto box = std::make_unique<CmdBox>(std::move(spec));
    box->layout(d);
    CmdBox& ref = *box;
    d.openModal(std::move(box));
    return ref;
}

CmdBox::CmdBox(Spec spec) : spec_(std::move(spec)), entries_(spec_.entries) {
    title_ = "Commands - step " + std::to_string(spec_.step + 1);
    for (size_t i = 0; i < kOrder.size(); ++i) {
        const SeqCmd k = kOrder[i];
        const Kind& kind = kKinds[i];
        Row& r = rows_[i];
        r.label.setText(kind.label);
        r.label.setTip(kind.tip);
        r.slider.setRange(kind.min, kind.max, 1);
        r.slider.setTip(kind.tip);
        r.slider.onChange = [this, k](double v) { change(k, int(v)); };
        if (spec_.shown[i])
            for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&r.label, &r.slider, &r.field, &r.onOff})
                add(*w);
        const int chars = int(std::to_string(kind.max).size());
        r.field.setMinChars(chars);
        r.field.setMask(std::string(size_t(chars), '9'));
        r.field.onCommit = [this, k, kind](const std::string& t) {
            const auto v = number(t);
            if (!v) return false;
            if (kind.form == Form::Slide) changeSpeed(k, std::clamp(*v, kind.min, kind.max));
            else change(k, std::clamp(*v, kind.min, kind.max));
            return true;
        };
        if (kind.form == Form::Slide) {
            r.slider.onChange = [this, k](double v) { changeSpeed(k, int(v)); };
            r.up = slideUp(entries_[slot(k)].value);
            if (hasCurve(k)) {
                const Entry& c = entries_[slot(ymengine::seqSlideCurve(k))];
                r.curve = c.on ? std::clamp(c.value, 0, kCurves - 1) : 0;
            }
            r.dir.setTip(hasCurve(k) ? kCurveTip : "Direction of the slide: up or down");
            r.dir.onClick = [this, k] {
                if (hasCurve(k)) return curveMenu(k);
                Row& rw = rows_[row(k)];
                rw.up = !rw.up;
                changeSpeed(k, slideSpeedOf(entries_[slot(k)].value));
            };
            if (spec_.shown[i]) add(r.dir);
        }
        r.onOff.setTip("Put this command in the step, or take it out");
        r.onOff.onClick = [this, k] {
            entries_[slot(k)].on = !entries_[slot(k)].on;
            show(k);
        };
        // Values of an absent command: those of the box, in range.
        Entry& e = entries_[slot(k)];
        if (spec_.shown[i] && kind.form == Form::Number) e.value = std::clamp(e.value, kind.min, kind.max);
    }
    cancel_.setTip("Close without changing the step");
    ok_.setTip("Write these commands into the step");
    cancel_.onClick = [this] { finish(kCancel); };
    ok_.onClick = [this] { finish(kOk); };
    add(cancel_);
    add(ok_);
    for (const SeqCmd k : kOrder) show(k);
}

CmdBox::~CmdBox() {
    // The controls are members: detach them before the widget goes.
    for (Row& r : rows_)
        for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&r.label, &r.slider, &r.field, &r.onOff, &r.dir})
            if (w->parent() == this) remove(*w);
    for (gemui::Button* b : {&cancel_, &ok_}) remove(*b);
}

void CmdBox::show(SeqCmd k) {
    const Entry& e = entries_[slot(k)];
    const Kind& kind = kKinds[row(k)];
    Row& r = rows_[row(k)];
    r.onOff.setText(e.on ? "On" : "Off");
    r.onOff.setOn(e.on);
    const int v = kind.form == Form::Slide ? slideSpeedOf(e.value) : e.value;
    r.slider.setValue(v);
    r.field.setText(std::to_string(v));
    if (kind.form == Form::Slide) r.dir.setText(dirText(r.up, r.curve));
}

void CmdBox::change(SeqCmd k, int value) {
    Entry& e = entries_[slot(k)];
    e.value = value;
    e.on = true;  // a value chosen: the command goes into the step
    show(k);
}

void CmdBox::changeSpeed(SeqCmd k, int speed) { change(k, slideXy(speed, rows_[row(k)].up)); }

void CmdBox::curveMenu(SeqCmd k) {
    gemui::Desktop* d = desktop();
    if (!d) return;
    const Row& r = rows_[row(k)];
    // A choice keeps the speed and turns the slide on, as the Up/Down button does.
    const auto choose = [this, k](bool up, int curve) {
        Row& rw = rows_[row(k)];
        rw.up = up;
        rw.curve = curve;
        changeSpeed(k, slideSpeedOf(entries_[slot(k)].value));
    };
    std::vector<gemui::MenuItem> items;
    for (const bool up : {true, false}) {
        gemui::MenuItem m;
        m.label = up ? "Up" : "Down";
        m.pick = [this, k, up, choose] { choose(up, rows_[row(k)].curve); };
        items.push_back(std::move(m));
    }
    items.push_back(gemui::MenuItem::separatorItem());
    gemui::MenuItem shapes;
    shapes.label = "Curve";
    shapes.subMarks = true;
    shapes.subCur = r.curve;
    for (int i = 0; i < kCurves; ++i) {
        if (std::find(ymseq::kSlideCurveSeps.begin(), ymseq::kSlideCurveSeps.end(), i) != ymseq::kSlideCurveSeps.end())
            shapes.sub.push_back(gemui::MenuItem::separatorItem());
        gemui::MenuItem m;
        m.label = std::string(ymseq::kSlideCurveNames[size_t(i)]);
        m.pick = [this, k, i, choose] { choose(rows_[row(k)].up, i); };
        shapes.sub.push_back(std::move(m));
    }
    items.push_back(std::move(shapes));
    gemui::MenuList::open(*d, std::move(items), r.up ? 0 : 1, r.dir.box(), false);
}

void CmdBox::layout(const gemui::Desktop& d) {
    const double lh = Font::lineBox("0", 16).height();
    double labelW = 0, fieldW = 0;
    size_t rows = 0;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!spec_.shown[i]) continue;
        labelW = std::max(labelW, rows_[i].label.naturalWidth());
        fieldW = std::max(fieldW, rows_[i].field.naturalWidth());
        ++rows;
    }
    const double onOffW = std::ceil(std::max(gemui::Button("Off").naturalWidth(), gemui::Button("On").naturalWidth()));
    double dirW = 0;  // the widest text the button can show
    for (const bool up : {true, false})
        for (int c = 0; c < (spec_.curves ? kCurves : 1); ++c)
            dirW = std::max(dirW, std::ceil(gemui::Button(dirText(up, c)).naturalWidth()));
    const int optionH = rows_[0].onOff.naturalHeight();
    const double rowH = std::max(lh, double(optionH));
    const double rowsW = labelW + kColGap + kSliderW + kColGap + fieldW + kColGap + dirW + kColGap + onOffW;
    const double buttonsW = std::ceil(cancel_.naturalWidth()) + kButtonSpacing + std::ceil(ok_.naturalWidth());
    const double content = std::ceil(std::max({rowsW, buttonsW, Font::width(title_, 16)}));
    const int buttonH = cancel_.naturalHeight();
    const double rowsH = double(rows) * rowH + double(rows - 1) * kRowGap;
    const double w = content + 2 * kPadX + 4;
    const double h = 2 + kPadY + gemui::Alert::kLineHeight + kParagraphGap + rowsH + kButtonsGap + buttonH + kPadY + 2;
    const double x = std::floor(d.width() / 2.0 - w / 2), y = std::floor(d.height() * 0.38 - h / 2);
    setBox({x, y, w, h});
    titleTop_ = y + 2 + kPadY;
    const double x0 = x + 2 + kPadX + std::floor((content - rowsW) / 2);
    double ry = titleTop_ + gemui::Alert::kLineHeight + kParagraphGap;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!spec_.shown[i]) continue;
        Row& r = rows_[i];
        r.label.setBox({x0, ry + (rowH - lh) / 2, labelW, lh});
        const double sx = x0 + labelW + kColGap, sy = ry + (rowH - 16) / 2;
        r.slider.setBox({sx, sy, double(kSliderW), 16});
        const double fx = sx + kSliderW + kColGap;
        r.field.setBox({fx + fieldW - r.field.naturalWidth(), ry + (rowH - lh) / 2, r.field.naturalWidth(), lh});
        const double dx = fx + fieldW + kColGap;  // the slides' Up/Down, then On/Off
        r.dir.setBox({dx, ry + (rowH - optionH) / 2, dirW, double(optionH)});
        r.onOff.setBox({dx + dirW + kColGap, ry + (rowH - optionH) / 2, onOffW, double(optionH)});
        ry += rowH + kRowGap;
    }
    double bx = x + 2 + kPadX + std::floor((content - buttonsW) / 2);
    const double by = ry - kRowGap + kButtonsGap;
    for (gemui::Button* b : {&cancel_, &ok_}) {
        const double bw = std::ceil(b->naturalWidth());
        b->setBox({bx, by, bw, double(buttonH)});
        bx += bw + kButtonSpacing;
    }
}

void CmdBox::paint(gemui::Painter& p) {
    WindowFrame::paint(p);
    const gemui::Box& b = box();
    const double contentX = b.x + 2 + kPadX, contentW = b.w - 4 - 2 * kPadX;
    const int baseline = gemui::snap(titleTop_ + (gemui::Alert::kLineHeight - 16) / 2 + Font::ascent(16));
    p.textAt(title_, contentX + (contentW - Font::width(title_, 16)) / 2, baseline, 16, gemui::colors::kBlack);
}

bool CmdBox::keyDown(const gemui::KeyEvent& e) {
    // Enter and Escape: Cancel, the default button (tenth DAW test, item 121, D-329).
    if (e.key == gemui::Key::Enter || e.key == gemui::Key::Escape) finish(kCancel);
    return true;  // a modal box keeps the keyboard
}

void CmdBox::finish(int button) {
    // The curves of the slides shown: with their slide, a shape other than Linear (D-394).
    for (size_t i = 0; i < kOrder.size(); ++i) {
        const SeqCmd k = kOrder[i];
        if (!spec_.shown[i] || !hasCurve(k)) continue;
        Entry& c = entries_[slot(ymengine::seqSlideCurve(k))];
        c.value = rows_[i].curve;
        c.on = entries_[slot(k)].on && c.value > 0;
    }
    const auto ok = spec_.ok;
    const Entries entries = entries_;
    if (gemui::Desktop* d = desktop()) d->closeModal(*this);  // destroys the box
    if (button == kOk && ok) ok(entries);
}

}  // namespace stym::pluginui
