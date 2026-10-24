// STYM2149 — gemui: GEM alert and prompt boxes
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Alert.h"

#include <algorithm>
#include <cmath>

#include "gemui/Font.h"

namespace stym::gemui {
namespace {

constexpr int kPadX = 32, kPadY = 24;
constexpr int kParagraphGap = 16;  // .alert p: margin-bottom
constexpr int kButtonsGap = 18;    // .alertbtns: margin-top (collapsed with the paragraph's margin: the larger wins)
constexpr int kButtonSpacing = 14; // .alertbtns: gap
// #promptWin: buttons inline below the field's paragraph, without .alertbtns, separated by a space (8 px).
constexpr int kInlineButtonSpacing = 8;
// Choice rows: radio buttons one space apart, label followed by a space, 10 px between the rows.
constexpr int kOptionSpacing = 8, kLabelGap = 8, kChoiceRowGap = 10;

// Wrapping at spaces (fixed-pitch font: a number of characters per line).
// Greedy; a word longer than a line stays whole on its own line.
std::vector<std::string> wrap(const std::string& text, int maxChars) {
    std::vector<std::string> out;
    if (maxChars <= 0 || Font::length(text) <= maxChars) {
        out.push_back(text);
        return out;
    }
    std::string line, word;
    auto flush = [&] {
        if (!line.empty()) out.push_back(line);
        line.clear();
    };
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t sp = text.find(' ', pos);
        word = text.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && Font::length(tryLine) > maxChars) {
            flush();
            line = word;
        } else {
            line = tryLine;
        }
        if (sp == std::string::npos) break;
        pos = sp + 1;
    }
    flush();
    return out;
}

}  // namespace

Alert& Alert::open(Desktop& d, Spec spec, Done done) {
    const bool over = spec.overPopups;
    auto a = std::make_unique<Alert>(std::move(spec), std::move(done));
    a->layout(d);
    Alert& ref = *a;
    d.openModal(std::move(a), over);
    return ref;
}

Alert& Alert::open(Desktop& d, Spec spec, Picked picked) {
    const bool over = spec.overPopups;
    auto a = std::make_unique<Alert>(std::move(spec), Done{});
    a->picked_ = std::move(picked);
    a->layout(d);
    Alert& ref = *a;
    d.openModal(std::move(a), over);
    return ref;
}

// The buttons (the row at the bottom, then the options of each choice row) are children owned by the box.
Alert::Alert(Spec spec, Done done) : spec_(std::move(spec)), done_(std::move(done)) {
    focus_ = spec_.focusButton >= 0 && spec_.focusButton < int(spec_.buttons.size()) ? spec_.focusButton : -1;
    for (int i = 0; i < int(spec_.buttons.size()); ++i) {
        auto b = std::make_unique<Button>(spec_.buttons[size_t(i)],
                                          i == spec_.defaultButton && spec_.markDefault ? Button::Style::Default : Button::Style::Normal);
        b->onClick = [this, i] { finish(i); };
        add(*b);
        buttons_.push_back(std::move(b));
    }
    for (size_t r = 0; r < spec_.choices.size(); ++r) {
        const Choice& c = spec_.choices[r];
        const int sel = std::clamp(c.selected, 0, std::max(0, int(c.options.size()) - 1));
        picks_.push_back(sel);
        std::vector<std::unique_ptr<Button>> row;
        for (int i = 0; i < int(c.options.size()); ++i) {
            auto b = std::make_unique<Button>(c.options[size_t(i)]);
            b->setOn(i == sel);
            b->onClick = [this, r, i] { pick(r, i); };
            add(*b);
            row.push_back(std::move(b));
        }
        options_.push_back(std::move(row));
    }
}

Alert::~Alert() {
    // The buttons are destroyed before the widget: detach them first.
    for (auto& b : buttons_) remove(*b);
    for (auto& row : options_)
        for (auto& b : row) remove(*b);
}

void Alert::layout(const Desktop& d) {
    // The prototype's box is placed at left:50% then re-centred by a translation: its "shrink-to-fit" width never
    // exceeds half of the desktop. As soon as a line wraps, the paragraph takes all of that width.
    // Widths are those of the content: the box adds the margins and its 2 px border on each side (+ 2 × kPadX + 4).
    double maxContent = d.width() / 2.0 - 2 * kPadX - 4;
    if (spec_.maxWidth > 0) maxContent = std::min(maxContent, double(spec_.maxWidth) - 2 * kPadX - 4);
    wrapped_.clear();
    double content = 0;
    bool folded = false;
    for (const Line& l : spec_.lines) {
        const int chars = int(maxContent / Font::advance(l.px));
        std::vector<std::string> parts = wrap(l.text, chars);
        folded = folded || parts.size() > 1;
        for (std::string& s : parts) {
            content = std::max(content, Font::width(s, l.px));
            wrapped_.push_back({std::move(s), l.px, l.color});
        }
    }
    if (folded) content = maxContent;
    // The content is as wide as its widest part: text, button row, input field, choice rows or picture.
    const double spacing = spec_.input ? kInlineButtonSpacing : kButtonSpacing;
    double row = 0;
    for (auto& b : buttons_) row += std::ceil(b->naturalWidth());
    row += spacing * double(std::max<size_t>(1, buttons_.size()) - 1);
    content = std::max(content, row);
    if (spec_.input) content = std::max(content, spec_.inputChars * Font::advance(16));
    // Choice rows: column of the labels, then the options.
    labelWidth_ = 0;
    for (const Choice& c : spec_.choices) labelWidth_ = std::max(labelWidth_, Font::width(c.label, 16));
    double choicesW = 0;
    optionH_ = 0;
    for (auto& r : options_) {
        double w = labelWidth_ + kLabelGap;
        for (size_t i = 0; i < r.size(); ++i) {
            w += std::ceil(r[i]->naturalWidth()) + (i ? kOptionSpacing : 0);
            optionH_ = std::max(optionH_, r[i]->naturalHeight());
        }
        choicesW = std::max(choicesW, w);
    }
    content = std::max(content, std::ceil(choicesW));
    const bool picture = spec_.picture && spec_.pictureW > 0 && spec_.pictureH > 0 && spec_.pictureScale > 0;
    if (picture) content = std::max(content, double(spec_.pictureW * spec_.pictureScale));
    content = std::ceil(content);

    // Vertical margins: each paragraph is followed by 16 px; the top margin of .alertbtns (18 px) collapses with that
    // of the preceding paragraph (adjacent margins in CSS: the larger wins).
    const double pictureH = picture ? double(spec_.pictureH * spec_.pictureScale) + kParagraphGap : 0;
    const double textH = wrapped_.empty() ? 0 : double(wrapped_.size()) * kLineHeight + kParagraphGap;
    const double choicesH = options_.empty() ? 0
                                             : double(options_.size()) * optionH_ +
                                                   double(options_.size() - 1) * kChoiceRowGap + kParagraphGap;
    const double inputH = spec_.input ? kLineHeight + kParagraphGap : 0;
    const int buttonH = buttons_.empty() ? 0 : buttons_.front()->naturalHeight();
    const double above = pictureH + textH + choicesH + inputH;
    double buttonsGap = 0;
    if (!buttons_.empty() && !spec_.input) buttonsGap = above > 0 ? kButtonsGap - kParagraphGap : kButtonsGap;
    // Height: border, margin, the blocks, the buttons (without buttons, the last 16 px paragraph margin is dropped),
    // margin, border. The box is centred horizontally and its centre placed at 38 % of the desktop's height.
    const double w = content + 2 * kPadX + 4;
    const double h = 2 + kPadY + above + (buttons_.empty() ? (above > 0 ? -kParagraphGap : 0) : buttonsGap + buttonH) +
                     kPadY + 2;
    const double x = d.width() / 2.0 - w / 2, y = d.height() * 0.38 - h / 2;
    setBox({x, y, w, h});
    pictureTop_ = y + 2 + kPadY;
    textTop_ = pictureTop_ + pictureH;
    choicesTop_ = textTop_ + textH;
    choicesX_ = x + 2 + kPadX + std::floor((content - choicesW) / 2);
    for (size_t r = 0; r < options_.size(); ++r) {
        double ox = choicesX_ + labelWidth_ + kLabelGap;
        const double oy = choicesTop_ + double(r) * (optionH_ + kChoiceRowGap);
        for (auto& b : options_[r]) {
            const double bw = std::ceil(b->naturalWidth());
            b->setBox({ox, oy, bw, double(optionH_)});
            ox += bw + kOptionSpacing;
        }
    }
    inputTop_ = choicesTop_ + choicesH;
    // Button row centred in the content, each button rounded up to whole pixels.
    double bx = x + 2 + kPadX + (content - row) / 2;
    const double by = inputTop_ + inputH + buttonsGap;
    for (auto& b : buttons_) {
        const double bw = std::ceil(b->naturalWidth());
        b->setBox({bx, by, bw, double(buttonH)});
        bx += bw + spacing;
    }
}

void Alert::paint(Painter& p) {
    WindowFrame::paint(p);
    const Box& b = box();
    const double contentX = b.x + 2 + kPadX, contentW = b.w - 4 - 2 * kPadX;
    if (spec_.picture && spec_.pictureW > 0 && spec_.pictureH > 0 && spec_.pictureScale > 0) {
        const int k = spec_.pictureScale;
        const int px = snap(contentX + (contentW - spec_.pictureW * k) / 2), py = snap(pictureTop_);
        for (int j = 0; j < spec_.pictureH; ++j)
            for (int i = 0; i < spec_.pictureW; ++i) {
                const Color c = spec_.picture[size_t(j * spec_.pictureW + i)];
                if (c >> 24) p.fill({px + i * k, py + j * k, k, k}, c);
            }
    }
    for (size_t i = 0; i < wrapped_.size(); ++i) {
        const Line& l = wrapped_[i];
        const double lineTop = textTop_ + double(i) * kLineHeight;
        // 25.6 px line: the baseline of the 16 px font is 4.8 + 13 px from the top.
        const int baseline = snap(lineTop + (kLineHeight - 16) / 2 + Font::ascent(16));
        const double x = contentX + (contentW - Font::width(l.text, l.px)) / 2;
        p.textAt(l.text, x, baseline, l.px, l.color);
    }
    for (size_t r = 0; r < spec_.choices.size() && r < options_.size(); ++r) {
        const double rowTop = choicesTop_ + double(r) * (optionH_ + kChoiceRowGap);
        const int baseline = snap(rowTop + (optionH_ - 16) / 2.0 + Font::ascent(16));
        p.textAt(spec_.choices[r].label, choicesX_, baseline, 16, colors::kBlack);
    }
    // Input field, centred: underscores at the free positions counted from the right, the text right-aligned, the
    // text cursor at the right edge (typing only adds at the end).
    if (spec_.input) {
        const double cw = Font::advance(16);
        const double fw = spec_.inputChars * cw;
        const double fx = contentX + (contentW - fw) / 2;
        const double top = inputTop_ + (kLineHeight - 16) / 2;
        // A text longer than the field shows its last characters, the cursor staying at the right edge.
        std::string shown = spec_.inputText;
        int typed = Font::length(shown);
        if (typed > spec_.inputChars) {
            size_t i = shown.size();
            for (int k = 0; k < spec_.inputChars && i > 0;) {
                --i;
                if ((uint8_t(shown[i]) & 0xC0) != 0x80) ++k;  // first byte of a UTF-8 character
            }
            shown.erase(0, i);
            typed = spec_.inputChars;
        }
        const Rect field = Box{fx, top, fw, 16}.snapped();
        for (int k = typed; k < spec_.inputChars; ++k) {
            const int a = snap(fx + fw - (k + 1) * cw);
            p.fill({a, field.bottom() - 3, int(cw) - 1, 2}, colors::kBlack);
        }
        p.text(shown, fx + fw - typed * cw, top, 16, colors::kBlack);
        p.fill({field.right() - 1, field.y, 1, field.h}, colors::kBlack);  // text cursor
    }
}

// Every key is taken (a modal box keeps the keyboard); in a prompt box, printable ASCII and Backspace edit the text.
bool Alert::keyDown(const KeyEvent& e) {
    if (e.key == Key::Tab) {
        moveFocus(e.mods.shift ? -1 : 1);
        return true;
    }
    if (e.key == Key::Enter) {
        finish(spec_.enterOnFocus && focus_ >= 0 ? focus_ : spec_.defaultButton);
        return true;
    }
    if (e.key == Key::Escape) {
        finish(spec_.escapeButton);
        return true;
    }
    if (spec_.input) {
        if (e.key == Key::Backspace && !spec_.inputText.empty()) {
            // Last UTF-8 character.
            size_t n = spec_.inputText.size() - 1;
            while (n > 0 && (uint8_t(spec_.inputText[n]) & 0xC0) == 0x80) --n;
            spec_.inputText.resize(n);
            repaint();
        } else if (e.key == Key::Character && e.ch >= 0x20 && e.ch < 0x7F &&
                   (spec_.inputMax < 0 ||
                    Font::length(spec_.inputText) < (spec_.inputMax > 0 ? spec_.inputMax : spec_.inputChars))) {
            spec_.inputText += char(e.ch);
            repaint();
        }
    }
    return true;
}

void Alert::moveFocus(int dir) {
    // gemConfirm: Tab, with or without Shift, moves from one button to the next; gemPrompt: field, Cancel, OK (the
    // browser's cycle, kept inside the box since it keeps the keyboard).
    const int n = int(buttons_.size());
    if (n == 0) return;
    // Prompt box: n + 1 stops, the field (-1) then the buttons; shifted by one to cycle in 0..n, then back.
    if (spec_.input) focus_ = (focus_ + 1 + dir + n + 1) % (n + 1) - 1;
    else if (focus_ < 0) focus_ = dir > 0 ? 0 : n - 1;
    else focus_ = (focus_ + dir + n) % n;
    ring_ = true;
    repaint();
}

void Alert::paintOver(Painter& p) {
    // Chromium's focus ring (outline:auto): it rounds the corners of the button's border by one pixel, in black and
    // white.
    if (!focusRing()) return;
    const Rect r = buttons_[size_t(focus_)]->bounds();
    for (const Point c : {Point{r.x, r.y}, Point{r.right() - 1, r.y}, Point{r.x, r.bottom() - 1},
                          Point{r.right() - 1, r.bottom() - 1}})
        p.fill({c.x, c.y, 1, 1}, colors::kWhite);
}

void Alert::pick(size_t row, int index) {
    if (row >= options_.size()) return;
    picks_[row] = index;
    for (size_t i = 0; i < options_[row].size(); ++i) options_[row][i]->setOn(int(i) == index);
}

// Results and callbacks are moved out first: closeModal destroys the box, and the callback may open another one.
void Alert::finish(int button) {
    Done done = std::move(done_);
    Picked picked = std::move(picked_);
    const std::string input = spec_.inputText;
    const std::vector<int> picks = picks_;
    Desktop* d = desktop();
    if (d) d->closeModal(*this);  // destroys the box
    if (done) done(button, input);
    if (picked) picked(button, picks);
}

}  // namespace stym::gemui
