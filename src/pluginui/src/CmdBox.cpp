// STYM2149 — pluginui: box of the step commands (CMD row of the Step Sequencer)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/CmdBox.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "gemui/Alert.h"
#include "gemui/Font.h"

namespace stym::pluginui {

using gemui::Font;
using ymengine::SeqCmd;

namespace {

// Margins of the alert boxes (Alert.cpp): text of 16 px, paragraph followed by 16 px, buttons 18 px below, 14 px apart.
constexpr int kPadX = 32, kPadY = 24, kParagraphGap = 16, kButtonsGap = 18, kButtonSpacing = 14;
constexpr int kRowGap = 10, kColGap = 12, kSliderW = 208, kSliderGap = 8;

// Form of a row: one slider and a decimal field; two half sliders (the two digits of xx) and a hexadecimal field.
enum class Form { Number, Pair };
struct Kind {
    const char* label;
    const char* tip;
    int min, max;  // of the slider (Pair: of each digit)
    Form form = Form::Number;
    const char* tipA = nullptr;  // Pair: the two sliders
    const char* tipB = nullptr;
};
// Rows in the order of CmdBox::kOrder.
constexpr std::array<Kind, CmdBox::kOrder.size()> kKinds = {{
    {"Ticks per Line", "Ticks per Line (Sxx): replay ticks of each line from this step (Ticks timing)", 1,
     ymengine::kSpeedMax},
    {"Sync Mark", "Sync Mark (Zxx): silent mark written in the SNDH export for a demo", 0, 255},
    {"Volume Slide", "Volume Slide (Axy): the volume goes up x or down y quarter steps per replay tick, until A00", 0,
     15, Form::Pair, "Up: x quarter steps of volume per tick", "Down: y quarter steps of volume per tick"},
    {"Buzzer Slide", "Buzzer Slide (Bxy): the envelope goes up x or down y 1/16 semitones per replay tick, until B00",
     0, 15, Form::Pair, "Up: x sixteenths of a semitone per tick", "Down: y sixteenths of a semitone per tick"},
    {"Noise Slide", "Noise Slide (Nxy): the noise goes up x or down y quarter period steps per replay tick, until N00",
     0, 15, Form::Pair, "Up: higher noise, x quarter steps of its period per tick",
     "Down: lower noise, y quarter steps of its period per tick"},
}};

int hexDigit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string hex2(int v) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    return {kHex[(v >> 4) & 15], kHex[v & 15]};
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
        add(r.label);
        const bool pair = kind.form == Form::Pair;
        for (int p = 0; p < (pair ? 2 : 1); ++p) {
            gemui::Slider& s = r.sliders[size_t(p)];
            s.setRange(kind.min, kind.max, 1);
            s.setTip(pair ? (p ? kind.tipB : kind.tipA) : kind.tip);
            s.onChange = [this, k, p, pair](double v) {
                int value = int(v);
                if (pair) {
                    const int xx = entries_[slot(k)].value;
                    value = p ? (xx & 0xF0) | value : (value << 4) | (xx & 15);
                }
                change(k, value);
            };
            add(s);
        }
        add(r.field);
        if (pair) {
            r.field.setMinChars(2);
            r.field.setWholeText(true);
            r.field.setTip("Type xy in hexadecimal: up x or down y per tick (0F: down 15)");
            r.field.onCommit = [this, k](const std::string& t) {
                if (t.empty() || t.size() > 2) return false;
                int xx = 0;
                for (const char c : t) {
                    const int d = hexDigit(c);
                    if (d < 0) return false;
                    xx = xx * 16 + d;
                }
                change(k, xx);
                return true;
            };
        } else {
            const int chars = int(std::to_string(kind.max).size());
            r.field.setMinChars(chars);
            r.field.setMask(std::string(size_t(chars), '9'));
            r.field.onCommit = [this, k, kind](const std::string& t) {
                const auto v = number(t);
                if (!v) return false;
                change(k, std::clamp(*v, kind.min, kind.max));
                return true;
            };
        }
        r.onOff.setTip("Put this command in the step, or take it out");
        r.onOff.onClick = [this, k] {
            entries_[slot(k)].on = !entries_[slot(k)].on;
            show(k);
        };
        add(r.onOff);
        // Values of an absent command: those of the box, in range (a slide shows its two digits).
        Entry& e = entries_[slot(k)];
        e.value = kind.form == Form::Pair ? e.value & 0xFF : std::clamp(e.value, kind.min, kind.max);
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
        for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&r.label, &r.sliders[0], &r.sliders[1], &r.field,
                                                                      &r.onOff})
            if (w->parent() == this) remove(*w);
    for (gemui::Button* b : {&cancel_, &ok_}) remove(*b);
}

void CmdBox::show(SeqCmd k) {
    const Entry& e = entries_[slot(k)];
    const Kind& kind = kKinds[row(k)];
    Row& r = rows_[row(k)];
    r.onOff.setText(e.on ? "On" : "Off");
    r.onOff.setOn(e.on);
    switch (kind.form) {
    case Form::Pair:
        r.sliders[0].setValue(e.value >> 4);
        r.sliders[1].setValue(e.value & 15);
        r.field.setText(hex2(e.value));
        break;
    case Form::Number:
        r.sliders[0].setValue(e.value);
        r.field.setText(std::to_string(e.value));
        break;
    }
}

void CmdBox::change(SeqCmd k, int value) {
    Entry& e = entries_[slot(k)];
    e.value = value;
    e.on = true;  // a value chosen: the command goes into the step
    show(k);
}

void CmdBox::layout(const gemui::Desktop& d) {
    const double lh = Font::lineBox("0", 16).height();
    double labelW = 0;
    for (const Row& r : rows_) labelW = std::max(labelW, r.label.naturalWidth());
    double fieldW = 0;
    for (const Row& r : rows_) fieldW = std::max(fieldW, r.field.naturalWidth());
    const double onOffW = std::ceil(std::max(gemui::Button("Off").naturalWidth(), gemui::Button("On").naturalWidth()));
    const int optionH = rows_[0].onOff.naturalHeight();
    const double rowH = std::max(lh, double(optionH));
    const double rowsW = labelW + kColGap + kSliderW + kColGap + fieldW + kColGap + onOffW;
    const double buttonsW = std::ceil(cancel_.naturalWidth()) + kButtonSpacing + std::ceil(ok_.naturalWidth());
    const double content = std::ceil(std::max({rowsW, buttonsW, Font::width(title_, 16)}));
    const int buttonH = cancel_.naturalHeight();
    const double rowsH = double(rows_.size()) * rowH + double(rows_.size() - 1) * kRowGap;
    const double w = content + 2 * kPadX + 4;
    const double h = 2 + kPadY + gemui::Alert::kLineHeight + kParagraphGap + rowsH + kButtonsGap + buttonH + kPadY + 2;
    const double x = std::floor(d.width() / 2.0 - w / 2), y = std::floor(d.height() * 0.38 - h / 2);
    setBox({x, y, w, h});
    titleTop_ = y + 2 + kPadY;
    const double x0 = x + 2 + kPadX + std::floor((content - rowsW) / 2);
    double ry = titleTop_ + gemui::Alert::kLineHeight + kParagraphGap;
    for (size_t i = 0; i < rows_.size(); ++i, ry += rowH + kRowGap) {
        Row& r = rows_[i];
        r.label.setBox({x0, ry + (rowH - lh) / 2, labelW, lh});
        const double sx = x0 + labelW + kColGap, sy = ry + (rowH - 16) / 2;
        if (kKinds[i].form == Form::Pair) {
            const double half = (kSliderW - kSliderGap) / 2.0;
            r.sliders[0].setBox({sx, sy, half, 16});
            r.sliders[1].setBox({sx + half + kSliderGap, sy, half, 16});
        } else {
            r.sliders[0].setBox({sx, sy, double(kSliderW), 16});
        }
        const double fx = sx + kSliderW + kColGap;
        r.field.setBox({fx + fieldW - r.field.naturalWidth(), ry + (rowH - lh) / 2, r.field.naturalWidth(), lh});
        r.onOff.setBox({fx + fieldW + kColGap, ry + (rowH - optionH) / 2, onOffW, double(optionH)});
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
    const auto ok = spec_.ok;
    const Entries entries = entries_;
    if (gemui::Desktop* d = desktop()) d->closeModal(*this);  // destroys the box
    if (button == kOk && ok) ok(entries);
}

}  // namespace stym::pluginui
