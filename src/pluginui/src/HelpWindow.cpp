// STYM2149 — pluginui: SHORTCUTS help window (#helpWin of the prototype)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "pluginui/HelpWindow.h"

#include <algorithm>
#include <cctype>

#include "gemui/Font.h"
#include "pluginui/MainWindow.h"

namespace stym::pluginui {

using gemui::Rect;

namespace {

using Row = HelpWindow::Row;

// HELP of the prototype, verbatim, except for M (the prototype's key handler, like STYM2149's, toggles the
// metronome: its hint line says M=Metro) and ^D, which leaves File (the demo pattern) for the DMA generator.
// Rows STYM2149 adds come last in their section, below what the window shows when it opens.
constexpr Row kWindows[] = {
    {"^E", "Step Sequencer"}, {"^B", "Arpeggiator"}, {"^M", "Mixer"},
    {"^G", "Song"},           {"^R", "Modulation"},  {"^P", "Patches"},
    {"^K", "Drum kit"},       {"Esc", "close the topmost window, then panic (all notes off)"},
};
constexpr Row kGenerator[] = {
    {"^V", "Chip Voice"}, {"^S", "SID Voice"}, {"^U", "Sync Buzzer"}, {"^C", "Chipdrum"}, {"^I", "Digidrum"},
    {"^D", "DMA (STE)"},  // ^D no longer loads the demo pattern
};
constexpr Row kFile[] = {{"^Q", "quit"}, {"^?", "this window"}};
constexpr Row kTransport[] = {
    {"Space", "play / stop"},
    {"^Z / ^Y", "undo / redo"},
    {"1 - 8", "select a pattern slot"},
    {"Arrows", "move the edit cursor in the grid (Shift + Left/Right: change channel)"},
    {"Enter", "put or remove a note at the cursor"},
    {"Delete", "clear the step at the cursor"},
    {"Z / X", "keyboard octave down / up"},
    {"M", "metronome on / off"},
};
constexpr Row kWriting[] = {
    {"Click an empty cell", "write a note with the current Note value"},
    {"Click a note", "resize it to the current Note value, or remove it if already that long"},
    {"Drag from an empty cell", "draw the note and its length in one move"},
    {"Drag a note", "move it in pitch and time, keeping its length"},
    {"Drag past the top / bottom row", "the view scrolls one semitone at a time"},
    {"Drag past the first / last column", "the view scrolls through the pattern"},
    {"Right click a note", "erase it (no menu)"},
    {"Right click an empty cell", "block and channel menu"},
    {"Alt + click a note", "split it at the clicked step"},  // STYM2149
};
constexpr Row kSeqRows[] = {
    {"SRC / GEN", "tone-noise source and generator, per step"},
    {"SLD", "slide to the next note"},
    {"CRV", "glissando shape used by the slide starting on that step"},
    {"HLD", "Chipdrum only: do not retrigger while it still rings"},
    {"OCT / GAT / PAN", "octave, gate and pan, per step"},
    {"RTG / CND", "ratchet (hits inside the step) and trigger condition"},
    {"VOL", "accent; left click draws, right click opens a slider"},
};
constexpr Row kSeqView[] = {
    {"Signature", "bar length; accents and metronome follow it"},
    {"Resolution", "value of one step, from 1/4 down to 1/64, plus triplets"},
    {"Note", "length written when placing a note, from 1/1 to 1/64, dotted or triplet"},
    {"Ghost", "show the notes of the other channels behind the grid"},
    {"Overview", "whole pattern at a glance; click or drag it to move the view"},
    {"Scale + Snap", "grey out-of-scale rows, or force notes into the scale"},
};
constexpr Row kSong[] = {
    {"Left / Right", "move the cursor along the arrangement"},
    {"Home / End", "first / last block"},
    {"Ins or A", "insert the current pattern after the cursor"},
    {"Del", "remove the block or the selection"},
    {"D", "duplicate the block"},
    {"+ / -", "more / fewer repeats"},
    {"X / C / V", "cut, copy, paste blocks"},
    {"Enter", "jump to that block and load its pattern"},
};
constexpr Row kMouse[] = {
    {"Right click a channel tab", "copy / paste the whole patch"},
    {"Right click a pattern slot", "copy / cut / paste / transpose the pattern"},
    {"Right click a row label", "copy / paste that row to another track"},
    {"Right click a song block", "repeat / duplicate / cut / move / remove"},
    {"Drag a song block", "reorder the arrangement"},
    {"Shift + drag in the grid", "select a block of steps"},
    {"Mouse wheel over the grid", "scroll the view one semitone"},
    {"Double click a mixer pan", "recentre it"},
    {"Click a mixer scope", "open the large oscilloscope"},
};
// Keys of MainWindow::keyDown and of the windows that the prototype's list lacks: Instruments window of STYM2149
// (the ^L Sequences window went into the Modulation window), keys of the prototype's handler it did
// not list (redo, banks, channel, numeric keypad, note and block editing in the grid, Arpeggiator and Modulation
// tables), File > New.
constexpr Row kMore[] = {
    {"^T", "Instruments"},
    {"^F", "Effects"},  // ninth DAW test, item 112
    {"Shift + ^Z", "redo"},
    {"Shift + 1 - 4", "select a pattern bank"},
    {"Left / Right", "previous / next channel (grid closed)"},
    {"Numpad 0 - 9", "keyboard octave (Computer keyboard open)"},
    {"] / [", "lengthen / shorten the note at the grid cursor"},
    {"\\", "split the note at the grid cursor"},
    {"J", "join the note at the grid cursor to the next one"},
    {"^C / ^X / ^V", "copy, cut, paste the note at the grid cursor, or the selected block"},
    {"Arrows", "move the selected block (Shift + Up/Down: one octave)"},
    {"Delete", "clear the selected block"},
    {"Esc", "drop the block selection"},
    {"Arrows", "move the cursor of the Arpeggiator or Modulation table"},
    {"Enter", "write the step at the table cursor"},
    {"Delete", "clear the step at the Modulation cursor"},
    {"N", "toggle NSE at the Modulation cursor"},
    {"Tab / Shift + Tab", "next / previous open window to the front"},
    {"^N", "new project (File > New)"},
};

constexpr HelpWindow::Section kSections[] = {
    {"Windows", kWindows},
    {"Generator", kGenerator},
    {"File", kFile},
    {"Transport and edit", kTransport},
    {"Writing notes", kWriting},
    {"Sequencer rows", kSeqRows},
    {"Sequencer view", kSeqView},
    {"Song window", kSong},
    {"Mouse", kMouse},
    {"More keys", kMore},  // STYM2149
};

// Boxes of the page (#helpBody CSS), in px.
constexpr int kPx = 16;            // text of the body
constexpr double kLine = 16;       // line box of the 16 px GEM font (ASCII text)
constexpr double kPad = 8;         // .mixbody padding
constexpr double kHeadAbove = 10;  // h4 margin-top (0 for the first)
constexpr double kHeadBelow = 4;   // h4 margin-bottom
constexpr double kRule = 2;        // h4 border-bottom
constexpr double kSpacing = 1;     // h4 letter-spacing
constexpr double kCellPad = 1;     // td padding, top and bottom
constexpr double kKeyCol = 160;    // td:first-child width (padding included)
constexpr double kKeyPad = 10;     // td:first-child padding-right

// Lines of `text` in a column `width` px wide, broken like the page: at spaces (which end the line) and after a
// hyphen between two letters; a word wider than the column stays alone on its line.
// First the text is cut into segments that cannot be broken (words, each also cut after a hyphen between two
// letters); then each line takes segments as long as the text from its first segment to the end of the next one still
// fits.
std::vector<std::string_view> wrap(std::string_view text, double width) {
    auto letter = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; };
    struct Seg {
        size_t from, to;
    };
    std::vector<Seg> segs;
    size_t from = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ' ') {
            if (i > from) segs.push_back({from, i});
            from = i + 1;
        } else if (text[i] == '-' && i > from && letter(text[i - 1]) && i + 1 < text.size() && letter(text[i + 1])) {
            segs.push_back({from, i + 1});
            from = i + 1;
        }
    }
    if (from < text.size()) segs.push_back({from, text.size()});
    std::vector<std::string_view> out;
    if (segs.empty()) return out;
    size_t start = segs[0].from, end = segs[0].to;
    for (size_t k = 1; k < segs.size(); ++k) {
        if (gemui::Font::width(text.substr(start, segs[k].to - start), kPx) <= width) {
            end = segs[k].to;
            continue;
        }
        out.push_back(text.substr(start, end - start));
        start = segs[k].from;
        end = segs[k].to;
    }
    out.push_back(text.substr(start, end - start));
    return out;
}

}  // namespace

std::span<const HelpWindow::Section> HelpWindow::sections() noexcept { return kSections; }

HelpWindow::Layout HelpWindow::layOut(double width) {
    // buildHelp: per section an h4, then a table as wide as the content box.
    Layout l;
    l.width = width + 2 * kPad;
    double y = kPad;
    for (const Section& s : kSections) {
        if (&s != &kSections[0]) y += kHeadAbove;
        l.lines.push_back({s.title, kPad, y, true});
        y += kLine;
        l.rules.push_back(y);
        y += kRule + kHeadBelow;
        // First column: its width, or the longest key (white-space:nowrap) and its padding.
        double keys = 0;
        for (const Row& r : s.rows) keys = std::max(keys, gemui::Font::width(r.key, kPx));
        const double col = std::max(kKeyCol, keys + kKeyPad);
        for (const Row& r : s.rows) {
            y += kCellPad;
            l.lines.push_back({r.key, kPad, y});
            const std::vector<std::string_view> text = wrap(r.text, width - col);
            for (size_t i = 0; i < text.size(); ++i) l.lines.push_back({text[i], kPad + col, y + kLine * double(i)});
            y += kLine * double(std::max<size_t>(1, text.size())) + kCellPad;
        }
    }
    l.height = y + kPad;
    return l;
}

HelpWindow::HelpWindow() { build(); }

void HelpWindow::build() {
    setTitle("SHORTCUTS");
    add(pane_);
    pane_.add(text_);
    // .mixbody (display:block; max-height:70vh; overflow:auto): as tall as its content, at most 70 % of the desktop;
    // the vertical scroll bar, when the content overflows, takes its width from the table.
    const double maxH = 0.7 * MainWindow::kHeight;
    const double paneW = kW - 2 * kInset;
    // Laid out at the full width first; if it overflows, laid out again in the width the scroll bar leaves.
    Layout l = layOut(paneW - 2 * kPad);
    if (l.height > maxH) l = layOut(paneW - gemui::ScrollPane::kBar - 2 * kPad);
    const double paneH = std::min(l.height, maxH);
    pane_.setBox({double(kX + kInset), double(kY + kInset + gemui::TitleBar::kHeight), paneW, paneH});
    pane_.setContentSize(int(l.width), int(l.height));
    const gemui::Point o = pane_.contentOrigin();
    text_.setBox({double(o.x), double(o.y), l.width, l.height});
    text_.setLayout(std::move(l));
    // Last: place() may move the window and its content to keep it on the desktop.
    place({double(kX), double(kY), double(kW), kInset + gemui::TitleBar::kHeight + paneH + kInset});
}

void HelpWindow::show() { open(); }

std::optional<Rect> HelpWindow::rectOf(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    if (id == "helpWin") return bounds();
    if (id == "helpBody") return pane_.bounds();
    return std::nullopt;
}

void HelpWindow::Text::paint(gemui::Painter& p) {
    // Only the lines that cross the visible part of the body (the painter's clip).
    const Rect clip = p.clip();
    const double ox = box().x, oy = box().y;
    for (const double r : layout_.rules)
        p.fill(gemui::Box{ox + kPad, oy + r, layout_.width - 2 * kPad, kRule}.snapped(), gemui::colors::kBlack);
    for (const Line& l : layout_.lines) {
        const double top = oy + l.y;
        if (top + kLine <= clip.y || top >= clip.bottom()) continue;
        p.text(l.text, ox + l.x, top, kPx, gemui::colors::kBlack, l.heading ? kSpacing : 0);
    }
}

}  // namespace stym::pluginui
