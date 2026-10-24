// STYM2149 — gemui: GEM alert and prompt boxes
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The prototype's .alert (#msgWin, #confirmWin, #promptWin): double frame like a window, 24 px / 32 px margins, text
// centred on lines of 1.6 × 16 px (paragraph followed by 16 px), buttons centred 18 px below the text (the two
// margins collapse), 14 px apart, the default button with a 4 px border; prompt box: buttons just below the field's
// paragraph, one space apart. The box is centred at 38 % of the desktop height, over a 25 % black veil (Desktop).
// Enter activates the default button, Escape the cancel button.
// Keyboard (gemConfirm, gemPrompt): Tab and Shift+Tab move the focus from one button to the next (the field is part
// of the cycle in a prompt box); the browser's focus ring, rounded corners on the button's border, only shows after
// a Tab (:focus-visible). Confirmation: Enter and Escape cancel, whatever has the focus; prompt box: Enter activates
// the button that has the focus, OK from the field.
// Picture (STYM2149, Information box, D-235): an image centred above the text, each pixel drawn as a square of
// `pictureScale` pixels (transparent pixels, alpha 0, left out), followed by the 16 px of a paragraph.
// Choice rows (STYM2149, WAV dialogue of the File menu, D-164): below the text, one row per setting, its label
// followed by radio buttons (.btn, the chosen option in reverse video, .on), one space apart; 10 px between the rows;
// the labels form a column, the block is centred.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Desktop.h"

namespace stym::gemui {

// A modal box owned by the desktop: open() lays it out and hands it to Desktop::openModal; a button, Enter or Escape
// closes it (the box is destroyed), then its callback receives the result.
class Alert : public WindowFrame {
public:
    struct Line {
        std::string text;  // empty: blank line (<br><br>)
        int px = 16;       // 12: note (.hint)
        Color color = colors::kBlack;
    };
    // A choice row: its label and its options, one of them picked (selected: the one picked at opening).
    struct Choice {
        std::string label;
        std::vector<std::string> options;
        int selected = 0;
    };
    // Everything the box shows, filled by the caller; the box keeps its own copy (the typed text goes into it).
    struct Spec {
        std::vector<Line> lines;
        std::vector<Choice> choices;
        std::vector<std::string> buttons{"OK"};  // from left to right; their index is the result
        int defaultButton = 0;  // Enter
        int escapeButton = 0;   // Escape
        bool markDefault = true;  // 4 px border on the default button (the prototype's #infoOk has none)
        // max-width of the box, border and margins included (the prototype's box-sizing:border-box; 0: none). The box
        // never exceeds half of the desktop either.
        int maxWidth = 520;
        // Input field (#promptInput): free positions shown as underscores.
        bool input = false;
        std::string inputText;
        int inputChars = 30;  // width of the field in characters
        int inputMax = 0;     // maximum length of the text (0: inputChars; < 0: none, a longer text shows its end)
        // Picture above the text (0xAARRGGBB, pictureW × pictureH, row by row; nullptr: none).
        const Color* picture = nullptr;
        int pictureW = 0, pictureH = 0, pictureScale = 1;
        // Button that has the keyboard focus on opening (-1: none, or the input field).
        int focusButton = -1;
        // Enter activates the button that has the focus (browser button, #promptWin), otherwise always defaultButton.
        bool enterOnFocus = false;
        // The popups already open (a box such as the Euclidean generator) stay open under the veil.
        bool overPopups = false;

        // Confirmation of a destructive action (#confirmWin, gemConfirm): Cancel, the default button, which has the
        // focus, then the action's button (kConfirmOk); Enter and Escape cancel.
        void confirmButtons(std::string ok = "OK") {
            buttons = {"Cancel", std::move(ok)};
            defaultButton = escapeButton = focusButton = 0;
            markDefault = true;
            enterOnFocus = false;
        }
        // Prompt box (#promptWin, gemPrompt): Cancel then OK (kPromptOk), neither of them marked; Enter confirms from
        // the field, Escape cancels.
        void promptButtons() {
            buttons = {"Cancel", "OK"};
            defaultButton = 1;
            escapeButton = 0;
            focusButton = -1;
            markDefault = false;
            enterOnFocus = true;
            input = true;
        }
    };
    static constexpr int kConfirmOk = 1, kPromptOk = 1;
    // Chosen button and typed text.
    using Done = std::function<void(int button, const std::string& input)>;
    // Chosen button and the option picked in each choice row.
    using Picked = std::function<void(int button, const std::vector<int>& picks)>;

    // Opens the box on d and returns it (valid until it closes). The callback runs once the box is destroyed, so it
    // may open another one.
    static Alert& open(Desktop& d, Spec spec, Done done = {});
    static Alert& open(Desktop& d, Spec spec, Picked picked);

    Alert(Spec spec, Done done);
    ~Alert() override;

    // Choice rows: the button of an option (tests, screenshots) and the picked options.
    Button* option(size_t row, size_t index) noexcept {
        return row < options_.size() && index < options_[row].size() ? options_[row][index].get() : nullptr;
    }
    const std::vector<int>& picks() const noexcept { return picks_; }
    // Buttons (tests), the button that has the focus (-1: none or the field) and whether the focus ring shows.
    const Button& button(size_t i) const noexcept { return *buttons_[i]; }
    size_t buttonCount() const noexcept { return buttons_.size(); }
    int focusedButton() const noexcept { return focus_; }
    bool focusRing() const noexcept { return ring_ && focus_ >= 0; }
    // Text of the box, one line per line of the spec (tests).
    std::string text() const {
        std::string t;
        for (size_t i = 0; i < spec_.lines.size(); ++i) t += (i ? "\n" : "") + spec_.lines[i].text;
        return t;
    }

    void paint(Painter& p) override;
    void paintOver(Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    bool keyDown(const KeyEvent& e) override;

    static constexpr double kLineHeight = 25.6;  // 1.6 × 16 px

private:
    // Sizes and places the box and its buttons for the desktop (centred horizontally, its centre at 38 % of the
    // desktop's height).
    void layout(const Desktop& d);
    // Closes the box, then reports `button` (and the typed text or the picks).
    void finish(int button);
    void pick(size_t row, int index);
    void moveFocus(int dir);  // Tab (+1) or Shift+Tab (-1)

    Spec spec_;
    Done done_;
    Picked picked_;
    std::vector<std::vector<std::unique_ptr<Button>>> options_;  // option buttons of each choice row
    std::vector<int> picks_;
    // Choice rows: top of the first row, width of the labels' column and its left edge, height of a row (an option
    // button's).
    double choicesTop_ = 0, labelWidth_ = 0, choicesX_ = 0;
    int optionH_ = 0;
    std::vector<Line> wrapped_;  // spec_.lines after wrapping, one entry per line drawn
    std::vector<std::unique_ptr<Button>> buttons_;
    double pictureTop_ = 0, textTop_ = 0, inputTop_ = 0;  // top of each block, from the top of the desktop
    int focus_ = -1;
    bool ring_ = false;  // focus ring shown: set by the first Tab
};

}  // namespace stym::gemui
