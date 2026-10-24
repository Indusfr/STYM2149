// STYM2149 — pluginui: SHORTCUTS help window (#helpWin of the prototype)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Opened by ^/ (or ^?) and the Fuji menu entry Shortcuts (openHelp): a floating window 560 px wide at (120, 70),
// title SHORTCUTS, close box only. Its body (.mixbody, padding 8) is at most 70 % of the desktop's height (max-height
// 70vh) and scrolls with its own scroll bar; the scroll position is kept when the window is closed and reopened, as
// in the prototype. The text is the prototype's HELP list (buildHelp), drawn as the page lays it out: per section, a
// heading (h4: letter spacing 1 px, 2 px rule under its line, margins 10 px above and 4 px below, none above the first)
// then a two-column table (rows of one 16 px line per text line plus 1 px above and below; first column 160 px wide,
// padding included, or as wide as its longest key plus its 10 px padding; second column wrapped at spaces and after a
// hyphen between letters). STYM2149 adds the shortcuts the prototype's list lacks after its rows (D-189), and says
// "metronome" for M, which the list called "mute".
#pragma once
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Scroll.h"

namespace stym::pluginui {

// Owned by MainWindow, used on the message thread. Its text is constant: it is laid out once, by the constructor.
class HelpWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 120, kY = 70, kW = 560;  // #helpWin: left 120, top 70, width 560

    struct Row {
        std::string_view key, text;  // first column (never wrapped), second column (wrapped)
    };
    struct Section {
        std::string_view title;
        std::span<const Row> rows;
    };
    // Sections in their order: the nine of the prototype, then the one STYM2149 adds.
    static std::span<const Section> sections() noexcept;

    // Text laid out in a body whose content box (inside the padding) is `width` px wide: positions from the top
    // left corner of the body's scrolled content (padding included), content size padding included.
    struct Line {
        std::string_view text;
        double x = 0, y = 0;   // top left of the line's 16 px box
        bool heading = false;  // drawn with the h4 letter spacing
    };
    struct Layout {
        std::vector<Line> lines;
        std::vector<double> rules;  // top of each heading's 2 px rule
        double width = 0, height = 0;
    };
    static Layout layOut(double width);

    HelpWindow();

    // openHelp: opens the window in front of the others, where it was left.
    void show();
    gemui::ScrollPane& body() noexcept { return pane_; }
    const Layout& layout() const noexcept { return text_.layout(); }
    // Box of a part (captures, tests): "helpWin", "helpBody".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;

private:
    // Scrolled content: headings, rules and table text, drawn from the layout.
    class Text : public gemui::Widget {
    public:
        void setLayout(Layout l) { layout_ = std::move(l); }
        const Layout& layout() const noexcept { return layout_; }
        void paint(gemui::Painter& p) override;

    private:
        Layout layout_;
    };

    void build();

    gemui::ScrollPane pane_;
    Text text_;
};

}  // namespace stym::pluginui
