// STYM2149 — pluginui: Step Sequencer grid (#seqgrid) and overview (canvas#overMap)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Rendering: classes of refreshSeqGrid and CSS cascade of the prototype (.cell, .qtr, .bar, .offsc, .nope, .dead, .sel,
// .onc, .held, .play, .mute, .psel, .ghost, .rz…), cell by cell in DOM order. Gestures: pointerdown, pointerover,
// pointerup and contextmenu of #seqgrid, in the same order of priority. Deviations.
//
// Coordinates: a shown column col (0-63) is the absolute step viewStart + col (SeqWindow::absCol); a visual note row
// r (0 at the top) is the relative row off = viewSemi + 12 - r (rowOff), in semitones above the C of the track's
// octave. On a percussion track viewSemi is 0 and off is the instrument (0-7): only rows 5 to 12 are shown.
// refresh() reads the track once and caches the classes of every shown cell; paint() and the pointer read the cache.
//
// Gestures: mouseDown calls press() on a cell, which arms one undo step and starts at most one gesture:
//   sizing_   an edge of a note or chord voice is dragged (left: its start, otherwise its end);
//   moving_   a note or chord voice is grabbed: a drag moves it, a release without a drag acts on it (release());
//   selDrag_  1: a block of steps (Shift on a control row or a percussion track); 2: a box (rect_) drawn with Shift,
//             the Select tool or the right button (the last two also set rightSel_: a click that does not move drops
//             the box);
//   paint_    the drag goes on with what the press did: write notes, erase, give a toggle row the same value, or
//             draw VOL.
// mouseDrag calls over() only when the pointer enters another cell (pointerover); over() serves the gesture in that
// order. mouseUp calls release(), which ends it, then the context menu of a right click (contextMenu), unless that
// click erased or drew a box (erasedByRight_). Tools (EditorSession::tool): 0 Move, 1 Draw, 2 Erase, 3 Split, 4 Join,
// 5 Select.
//
// Undo: press() calls SeqWindow::armUndo, and SeqWindow::editLane pushes the pattern as it was before the gesture's
// first change, or nothing if the gesture changes nothing; the drags of sizing_ and moving_ use that step too.
// Some paths also push their own step: release() before the action of a click, the eraser drag once per
// erased note, the menus before their action. UndoHistory drops a step equal to the last one, so a pushUndo
// followed by the armed editLane still leaves a single step.
#include <algorithm>
#include <cmath>

#include "plugincore/Instruments.h"
#include "plugincore/Mapping.h"
#include "pluginui/IconData.h"
#include "pluginui/MainWindow.h"
#include "pluginui/SeqWindow.h"
#include "ymengine/Pitch.h"
#include "ymengine/Sequence.h"
#include "ymseq/Commands.h"
#include "ymseq/Tables.h"

namespace stym::pluginui {

using gemui::Color;
using gemui::Font;
using gemui::MenuItem;
using gemui::Painter;
using gemui::Pointer;
using gemui::Rect;
using gemui::rgb;
namespace colors = gemui::colors;

namespace {

// Colours of the prototype's cell classes (.qtr beat, .offsc out of the scale, .play playhead, .sel selection, .onc
// note), of the ghost band, of the step numbers and of the VOL bar under the playhead.
constexpr Color kQtr = rgb(0xE8, 0xE8, 0xE8), kOffsc = rgb(0x9C, 0x9C, 0x9C), kOffscQtr = rgb(0x8A, 0x8A, 0x8A),
                kOffscPlay = rgb(0x00, 0x6B, 0x00), kOffscOncPlay = rgb(0x00, 0x33, 0x00),
                kSelBg = rgb(0xBF, 0xE8, 0xBF), kOncPlay = rgb(0x00, 0x55, 0x00), kGhost = rgb(0x8C, 0x8C, 0x8C),
                kStepNum = rgb(0x60, 0x60, 0x60), kVolPlayBar = rgb(0x00, 0x44, 0x00);
// Label out of scale (.rl.dis, white at opacity .45 on black).
constexpr Color kLabelDis = rgb(0x73, 0x73, 0x73);
// Overview.
constexpr Color kMapOut = rgb(0xC8, 0xC8, 0xC8), kMapBar = rgb(0xE0, 0xE0, 0xE0), kMapOther = rgb(0xA8, 0xA8, 0xA8),
                kMapView = rgb(0x00, 0x00, 0xA0);

constexpr int kMapDrum = 26;  // MAP_DRUM: height of the overview's percussion band, in pixels

// Labels and help of the control rows (buildSeqGrid, FLAGHELP).
constexpr std::array<const char*, kSeqLanes> kLaneNames = {"SLT", "OCT", "TON", "INS", "CMD", "SRC", "GEN", "ARP",
                                                           "HLD", "CUT", "SLD", "CRV", "GAT", "PAN", "RTG", "CND",
                                                           "VOL", "VIB", "BPM", "ENV", "DLY", "CUT", "LFO", "MOD",
                                                           "SMP", "TUN"};
constexpr std::array<const char*, kSeqLanes> kLaneHelp = {
    "SLT: Digidrum slot for this step",
    "OCT: shift this step by one or more octaves",
    "TON: tune this step by semitones",
    "INS: play this note with a project instrument (Instruments window, Ctrl+T)",
    "CMD: commands of this step, click to set them (ticks per line, sync mark, slides); MUL: several",
    "SRC: tone, noise or both on this step; the Env entries put its volume on the envelope. Right click: its settings",
    "GEN: use another generator on this step only. Right click: its settings",
    "ARP: switch the arpeggiator of the track on or off from this step",
    "HLD: do not retrigger the drum while it still rings",
    "CUT: stop the sound on this step",
    "SLD: glide from this note to the next one",
    "CRV: glissando shape. Set it where a slide starts, or mid-slide to change it",
    "GAT: note length on this step, as a share of it",
    "PAN: stereo position of this step",
    "RTG: repeat the hit 2, 3 or 4 times inside the step",
    "CND: play this step only when the condition is met",
    "VOL: accent. Drag to draw, right click for a slider",
    "VIB: switch the vibrato of the track on or off from this step",
    "BPM: tempo of every track from this step on, until the next BPM",
    "ENV: hardware envelope shape of the track from this step on; R: the envelope restarts. Right click: its "
    "settings",
    "DLY: the notes of this step start at this tick of the line",
    "CUT: the notes of this step stop at this tick of the line",
    "LFO: switch the LFO of the track on or off from this step",
    "MOD: switch the modulation table of the track on or off from this step",
    "SMP: the DMA sample of this step starts further in",
    "TUN: fine tune of the track in cents from this step on",
};

bool isFlag(SeqLane l) noexcept {  // toggle rows (tie, slide, cut; afl was one until the twelfth DAW test, item 144)
    return l == SeqLane::Tie || l == SeqLane::Slide || l == SeqLane::Cut;
}

// Switch of a row (VIB, LFO, MOD, ARP): its text, and the command of each menu entry (No change, On, Off).
std::string switchText(uint16_t cmd) { return cmd ? (ymengine::seqCmdParam(cmd) ? "On" : "Off") : "-"; }

int clampIdx(int v, int n) noexcept { return std::clamp(v, 0, n - 1); }

// Instrument number in two-digit hexadecimal ($01 to $20, like the trackers).
std::string hex2(int v) {
    static const char* const kHex = "0123456789ABCDEF";
    return {kHex[(v >> 4) & 15], kHex[v & 15]};
}

// Baseline of a text in a line box of height lineH (half-leading rounded down, like Chromium), metrics of the GEM font.
int baselineIn(double lineTop, double lineH, int px) {
    const Font::LineBox lb = Font::lineBox("0", px);
    return gemui::snap(lineTop + std::floor((lineH - lb.height()) / 2) + lb.ascent);
}

// Small text of a cell: 6×6 font, tightened by one pixel per letter beyond two characters; bold of the
// prototype (font-weight:bold) rendered by a second pass offset by one pixel.
void smallText(Painter& p, std::string_view t, double x0, double w, int baseline, Color c, bool bold = false) {
    if (t.empty()) return;
    const double sp = Font::length(t) > 2 ? -1 : 0;
    const double tw = Font::width(t, 9, sp, gemui::Face::kSmall);
    const double x = x0 + (w - tw) / 2;
    p.textAt(t, x, baseline, 9, c, sp, gemui::Face::kSmall);
    if (bold) p.textAt(t, x + 1, baseline, 9, c, sp, gemui::Face::kSmall);
}

// 3 px hatching at 135° (repeating-linear-gradient of .cell.nope), anchored in the cell: d is the distance of the
// pixel's centre from the anchor along the diagonal, white for the first 3 px of each 6 px period.
void stripes(Painter& p, const Rect& r, const Rect& anchor) {
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) {
            const double d = (x - anchor.x + 0.5 + y - anchor.y + 0.5) / std::sqrt(2.0);
            p.pixel(x, y, std::fmod(d, 6.0) < 3 ? colors::kWhite : colors::kNopeStripe);
        }
}

// 2 px checkerboard (.cell.mute), anchored in the padding box.
void checker(Painter& p, const Rect& r, gemui::Point anchor) {
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) {
            const int u = ((x - anchor.x) % 4 + 4) % 4, v = ((y - anchor.y) % 4 + 4) % 4;
            p.pixel(x, y, (u < 2) == (v < 2) ? colors::kBlack : colors::kWhite);
        }
}

// Background of a cell: cascade of the prototype's rules (specificity, then file order). Each bit is one class of the
// cell; a rule applies when the cell has all the classes it needs.
enum : unsigned { kNope = 1, kQtrBit = 2, kOffscBit = 4, kPlay = 8, kOnc = 16, kDead = 32, kSel = 64, kHeld = 128 };
struct Bg {
    Color c;
    bool stripes;
};
Bg cellBackground(unsigned f) {
    struct Rule {
        unsigned need;  // classes of the selector besides .cell
        int spec;       // specificity: number of classes of the selector, .cell included
        Bg bg;
    };
    static const Rule rules[] = {
        {0, 1, {colors::kWhite, false}},
        {kNope, 2, {colors::kWhite, true}},
        {kQtrBit, 2, {kQtr, false}},
        {kOffscBit, 2, {kOffsc, false}},
        {kOffscBit | kQtrBit, 3, {kOffscQtr, false}},
        {kOffscBit | kPlay, 3, {kOffscPlay, false}},
        {kOffscBit | kQtrBit | kPlay, 4, {kOffscPlay, false}},
        {kOffscBit | kOnc, 3, {colors::kBlack, false}},
        {kOffscBit | kQtrBit | kOnc, 4, {colors::kBlack, false}},
        {kOffscBit | kOnc | kPlay, 4, {kOffscOncPlay, false}},
        {kOffscBit | kQtrBit | kOnc | kPlay, 5, {kOffscOncPlay, false}},
        {kDead, 2, {colors::kGrey, false}},
        {kSel, 2, {kSelBg, false}},
        {kOnc, 2, {colors::kBlack, false}},
        {kPlay, 2, {colors::kGreen, false}},
        {kOnc | kPlay, 3, {kOncPlay, false}},
        {kHeld, 2, {colors::kBlack, false}},
        {kHeld | kPlay, 3, {kOncPlay, false}},
    };
    Bg bg = rules[0].bg;
    int best = 0;
    // The most specific matching rule wins; ">=" lets a later rule of the same specificity win, like the file order.
    for (const Rule& r : rules)
        if ((f & r.need) == r.need && r.spec >= best) {
            bg = r.bg;
            best = r.spec;
        }
    return bg;
}

// Root or chord voice that covers step c on row off of a melodic track: its start and voice (1 = root).
std::optional<ymseq::SelKey> noteCovering(const ymseq::LaneEditor& ed, const ymseq::Lane& l, int c, int off) {
    if (const auto v = ed.voiceStartCovering(c, off)) return ymseq::SelKey{v->start, v->voice};
    const int st = l.pat[size_t(c)] >= 0 ? c : ed.noteStart(c);
    if (st < 0 || ed.relNote(st) != off) return std::nullopt;
    return ymseq::SelKey{st, 1};
}

// Inner shadow: 4 px green ring (cursor, selected note) or 4 px black stroke on the left (bar).
void insetShadow(Painter& p, const Rect& pad, bool ring, bool bar) {
    if (ring) p.frame(pad, 4, colors::kGreen);
    else if (bar) p.fill({pad.x, pad.y, std::min(4, pad.w), pad.h}, colors::kBlack);
}

}  // namespace

// ------------------------------------------------------------------------------------------- geometry

// Rows from the top of the grid: the header on the 2 px border, then each shown row after a 1 px gap (the black
// background of the grid shows through the gaps). Hidden rows take no room.
std::vector<SeqGrid::RowBox> SeqGrid::rows() const {
    std::vector<RowBox> out;
    int y = kBorder;
    out.push_back({RowBox::Kind::Header, 0, y, kHeaderH});
    y += kHeaderH;
    for (int r = 0; r < kRows; ++r) {
        if (!rowShown_[size_t(r)]) continue;
        y += kGap;
        out.push_back({RowBox::Kind::Note, r, y, kNoteH});
        y += kNoteH;
    }
    y += kGap;
    out.push_back({RowBox::Kind::Sep, 0, y, kSepH});
    y += kSepH;
    for (const SeqLane lane : kSeqLaneOrder) {
        if (!laneShown_[size_t(lane)]) continue;
        if (lane == SeqLane::Bpm) {  // shared by the tracks: behind a separator (twelfth DAW test, item 150)
            y += kGap;
            out.push_back({RowBox::Kind::Sep, 1, y, kSepH});
            y += kSepH;
        }
        const int h = lane == SeqLane::Vol ? kVolH : kLaneH;
        y += kGap;
        out.push_back({RowBox::Kind::Lane, int(lane), y, h});
        y += h;
    }
    return out;
}

int SeqGrid::naturalHeight() const {
    const auto rs = rows();
    return rs.back().y + rs.back().h + kBorder;
}

std::optional<std::pair<int, int>> SeqGrid::noteSpan() const {
    // fitTrSlider: rows 0 and 12 shown only (drums: the previous measurement stays).
    if (!rowShown_[0] || !rowShown_[kRows - 1]) return std::nullopt;
    int first = -1, last = -1;
    for (const RowBox& b : rows())
        if (b.kind == RowBox::Kind::Note) {
            if (b.index == 0) first = b.y;
            if (b.index == kRows - 1) last = b.y + b.h;
        }
    if (first < 0 || last < 0) return std::nullopt;
    return std::pair{first, last};
}

Rect SeqGrid::noteRect(int col, int row) const {
    for (const RowBox& b : rows())
        if (b.kind == RowBox::Kind::Note && b.index == row) return {colX(col), bounds().y + b.y, kCellW, b.h};
    return {};
}

Rect SeqGrid::laneRect(int col, SeqLane lane) const {
    for (const RowBox& b : rows())
        if (b.kind == RowBox::Kind::Lane && b.index == int(lane)) return {colX(col), bounds().y + b.y, kCellW, b.h};
    return {};
}

// What a point hits: header (Corner, Step), row name (NoteLabel, LaneLabel) or cell (Note, Lane); Kind::None for the
// border, the gaps, the separator and the cells beyond 256 steps or the track's length.
SeqGrid::Hit SeqGrid::hitAt(gemui::Point p) const {
    Hit h;
    const Rect gb = bounds();
    const int lx = gb.x + kBorder;
    for (const RowBox& b : rows()) {
        const int top = gb.y + b.y;
        if (p.y < top || p.y >= top + b.h) continue;
        if (b.kind == RowBox::Kind::Sep) return h;
        // Column: labels (38 px) or cell (16 px); the one-pixel gaps are nothing.
        int col = -1;
        bool label = false;
        if (p.x >= lx && p.x < lx + kLabelW) label = true;
        else {
            const int dx = p.x - colX(0);
            if (dx < 0 || dx >= kCols * (kCellW + kGap)) return h;
            if (dx % (kCellW + kGap) >= kCellW) return h;
            col = dx / (kCellW + kGap);
        }
        const Rect r = label ? Rect{lx, top, kLabelW, b.h} : Rect{colX(col), top, kCellW, b.h};
        h.r = r;
        h.col = col;
        switch (b.kind) {
        case RowBox::Kind::Header: h.kind = label ? Hit::Kind::Corner : Hit::Kind::Step; return h;
        case RowBox::Kind::Note:
            h.row = b.index;
            if (label) {
                h.kind = Hit::Kind::NoteLabel;
                return h;
            }
            // .dead: pointer-events:none, the press goes to the grid.
            if (cells_[size_t(col)][size_t(b.index)].dead) return Hit{};
            h.kind = Hit::Kind::Note;
            return h;
        case RowBox::Kind::Lane:
            h.lane = SeqLane(b.index);
            if (label) {
                h.kind = Hit::Kind::LaneLabel;
                return h;
            }
            if (lanes_[size_t(b.index)][size_t(col)].dead) return Hit{};
            h.kind = Hit::Kind::Lane;
            return h;
        case RowBox::Kind::Sep: return Hit{};
        }
    }
    return Hit{};
}

// ------------------------------------------------------------------------------------------- state

void SeqGrid::refresh() {
    // refreshSeqGrid (with updateSeqLabels and buildGhostIndex).
    const EditorSession& s = w_.ses();
    const int gen = w_.generator();
    const bool digid = gen == int(ymengine::Generator::Digidrum), ndrum = gen == int(ymengine::Generator::Chipdrum);
    drum_ = digid || ndrum;
    viewSemi_ = w_.viewSemi();
    const int VS = viewSemi_;
    const int trackOct = w_.trackOct();
    const int octBase = (trackOct + 1) * 12;  // absolute note of relative row 0 (C of the track's octave; 60 = C4)
    const int effLen = w_.length();
    const int bStep = std::max(1, w_.beatSteps()), bBar = std::max(1, w_.barSteps());
    // Held notes are drawn on every track but a Chipdrum one, whose tie row is HLD (no retrigger), not a hold.
    const bool barRows = !ndrum;
    const bool latinNames = w_.latin();
    const int sc = w_.scale(), root = w_.scaleRoot();
    const bool m3 = w_.mode3(), chords = w_.chordsPlay();
    // Deviation: the notes picked one by one and the selection box belong to the track they were made on
    // (they stayed when another track was shown, and -1/+1, < and > then edited that track at the same places).
    if (w_.cur() != selTrack_) {
        if (selTrack_ >= 0) {
            noteSel_.clear();
            rect_.reset();
        }
        selTrack_ = w_.cur();
    }

    ymseq::Lane lane = w_.lane();
    const ymseq::LaneEditor ed(lane, w_.context());

    // Note rows (updateSeqLabels): drums, 8 usable rows.
    for (int r = 0; r < kRows; ++r) {
        const int off = VS + 12 - r;
        std::string& lab = noteLabels_[size_t(r)];
        labelDis_[size_t(r)] = false;
        if (ndrum) lab = off >= 0 && off < 8 ? std::string(ymseq::kDrumNames[size_t(off)]) : "";
        else if (digid) lab = off < 8 ? "Sl " + std::to_string(off + 1) : "";
        else {
            const int pc = ((off % 12) + 12) % 12;
            const int oc = trackOct + int(std::floor(off / 12.0));
            lab = std::string(ymseq::noteName(pc, latinNames ? ymseq::NoteNames::Latin : ymseq::NoteNames::English)) +
                  std::to_string(oc);
            labelDis_[size_t(r)] = !ymseq::inScale(off + octBase, sc, root);
        }
        rowShown_[size_t(r)] = drum_ ? off < 8 : true;
    }
    // Control rows (rowVis).
    auto show = [&](SeqLane l, bool v) { laneShown_[size_t(l)] = v; };
    show(SeqLane::Mix, !drum_);
    show(SeqLane::Fx, !drum_);
    show(SeqLane::Slide, !drum_);
    show(SeqLane::Slc, w_.crvShown());
    show(SeqLane::Afl, w_.arpShown());
    show(SeqLane::Tie, ndrum);
    show(SeqLane::Slt, false);
    show(SeqLane::Cut, false);
    show(SeqLane::Oct, drum_);
    show(SeqLane::Ton, drum_);
    show(SeqLane::Ins, w_.insShown());
    show(SeqLane::Cmd, w_.cmdShown());
    show(SeqLane::Vib, w_.vibShown());  // eleventh and twelfth DAW tests, items 132, 133, 138 to 140, 142 to 153
    show(SeqLane::Lfo, w_.lfoShown());
    show(SeqLane::Mod, w_.modShown());
    show(SeqLane::Tun, w_.tunShown());
    show(SeqLane::Bpm, w_.bpmShown());
    show(SeqLane::Env, w_.envShown());
    show(SeqLane::Dly, w_.ownRowShown(ymengine::SeqCmd::Delay));
    show(SeqLane::NoteCut, w_.ownRowShown(ymengine::SeqCmd::Cut));
    show(SeqLane::Smp, w_.smpShown());
    show(SeqLane::Gat, !drum_);
    show(SeqLane::Pnr, !m3 || w_.m_.dmaTrack(w_.cur()));  // DMA track in 3-channel mode: pan
    show(SeqLane::Rtg, true);
    show(SeqLane::Cnd, true);
    show(SeqLane::Vol, true);

    buildGhosts(ed);

    // Notes picked one by one (selNotes).
    const std::vector<ymseq::SelNote> picked = noteSel_.empty() ? std::vector<ymseq::SelNote>{} : ed.resolve(noteSel_);
    const bool hasRb = rect_.has_value();
    const ymseq::RectBounds rb = hasRb ? rectBounds() : ymseq::RectBounds{};

    // Each shown column c (absolute step a): its step number, then the classes of its 13 note cells and of its control
    // cells.
    for (int c = 0; c < kCols; ++c) {
        const int a = s.viewStart + c;
        const bool q = a % bStep == 0, dead = a >= effLen, barHit = a % bBar == 0;
        StepNum& sn = stepNums_[size_t(c)];
        sn.bar = barHit;
        sn.qtr = !barHit && q;
        sn.text = q ? std::to_string(a + 1) : "";
        if (a >= ymseq::kSteps) {  // beyond 256 steps: nothing (the view does not go that far)
            NoteCell deadNote;
            deadNote.dead = true;
            LaneCell deadLane;
            deadLane.dead = true;
            for (auto& nc : cells_[size_t(c)]) nc = deadNote;
            for (auto& ln : lanes_) ln[size_t(c)] = deadLane;
            continue;
        }
        const size_t A = size_t(a);
        // Relative row of the root note written at this step (-999: none). Chord voices: rows of the voices that start
        // at this step (polyRows), that an earlier voice holds over it (polyHeld), whose last step it is (polyLast).
        const int noteRow = lane.pat[A] >= 0 ? (drum_ ? lane.pat[A] : ed.relNote(a)) : -999;
        std::vector<int> polyRows, polyHeld, polyLast;
        for (int v = 2; v <= ymseq::kPolyMax; ++v)
            if (const auto rr = ed.voiceRow(a, v)) polyRows.push_back(*rr);
        if (!drum_)
            for (int k = 0; k <= a; ++k)  // a voice can be held up to 256 steps (64 steps back only)
                for (int v = 2; v <= ymseq::kPolyMax; ++v) {
                    const auto rr = ed.voiceRow(k, v);
                    if (!rr) continue;
                    const int end = k + ed.voiceDur(k, v) - 1;
                    if (end < a) continue;
                    if (k < a) polyHeld.push_back(*rr);
                    if (end == a) polyLast.push_back(*rr);
                }
        auto has = [](const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); };
        // The root note goes on at the next step (tied, no new note there): this step is not its last.
        const bool tieNext = a + 1 < effLen && lane.tie[A + 1] && lane.pat[A + 1] < 0;
        for (int r = 0; r < kRows; ++r) {
            const int row = VS + 12 - r;
            NoteCell& nc = cells_[size_t(c)][size_t(r)];
            nc = NoteCell{};
            const bool on = noteRow == row || has(polyRows, row);
            const bool polyOn = has(polyHeld, row);
            nc.nope = !drum_ && row + octBase < ymseq::kPlayLow;  // below B0: the chip no longer follows
            bool held = false, last = false, rootHeld = false;
            if (barRows && !on && lane.pat[A] < 0 && lane.tie[A]) {  // step covered by a held note
                const int st = ed.noteStart(a);
                if (st >= 0 && ed.relNote(st) == row) {
                    held = rootHeld = true;
                    last = !tieNext;
                }
            }
            if (polyOn && !on) held = true;
            if (has(polyLast, row)) last = true;
            if (on && barRows) last = !tieNext;
            if (!on && !held && !drum_) {
                const Ghost& g = ghosts_[size_t(c)][size_t(r)];
                nc.ghost = g.on;
                nc.gstart = g.on && g.start;
                nc.gend = g.on && g.end;
            }
            nc.held = held;
            // .rz: resize pointer (end of a note); .nend: right end of a note, which gets a white right border.
            nc.rz = !drum_ && (((on || held) && last) || (on && lane.tie[A]));
            nc.nend = last && (on || held);
            // Chord voice not played. Deviation: its held part is greyed too (it was drawn as played).
            nc.mute = !chords && (has(polyRows, row) || polyOn) && noteRow != row && !rootHeld;
            if (!picked.empty() && (on || held))
                for (const ymseq::SelNote& it : picked)
                    if (it.row == row && a >= it.s && a < it.s + ed.voiceDur(it.s, it.v)) {
                        nc.psel = true;
                        break;
                    }
            nc.offsc = !drum_ && !ymseq::inScale(row + octBase, sc, root);
            nc.sel = (!selRow_ && selA_ >= 0 && a >= blockLo() && a <= blockHi()) ||
                     (hasRb && a >= rb.a && a <= rb.b && row >= rb.r1 && row <= rb.r2);
            nc.cur = a == s.curStep && r == s.curRow;
            nc.on = on;
            nc.qtr = q;
            nc.bar = barHit && c > 0;  // first column: the black name column already marks it
            nc.dead = dead;
            nc.octave = ymseq::octaveOf(lane.oct[A], trackOct);
        }

        // Control rows.
        const bool inSel = selA_ >= 0 && a >= blockLo() && a <= blockHi();
        for (int k = 0; k < kSeqLanes; ++k) {
            LaneCell& lc = lanes_[size_t(k)][size_t(c)];
            lc = LaneCell{};
            lc.qtr = q;
            lc.dead = dead;
            lc.sel = inSel && (!selRow_ || int(*selRow_) == k) && SeqLane(k) != SeqLane::Bpm;  // BPM: not a track's
        }
        auto lc = [&](SeqLane l) -> LaneCell& { return lanes_[size_t(l)][size_t(c)]; };
        lc(SeqLane::Tie).on = lane.tie[A];
        lc(SeqLane::Slide).on = lane.slide[A];
        lc(SeqLane::Cut).on = lane.cut[A];
        vol_[size_t(c)] = std::clamp<int>(lane.vol[A], 0, 15);
        volTo_[size_t(c)] = lane.cmdAt(ymengine::SeqCmd::VolGlide, int(A))
                                ? std::clamp<int>(lane.vol[a + 1 < effLen ? A + 1 : 0], 0, 15)
                                : -1;
        static const std::array<const char*, 8> mixT = {"-", "T", "N", "TN", "E", "TE", "NE", "TNE"};
        static const std::array<const char*, 5> fxT = {"-", "CV", "SID", "SB", "CD"};
        static const std::array<const char*, 4> rtgT = {"-", "2", "3", "4"};
        static const std::array<const char*, 8> cndT = {"-", "1:2", "2:2", "1:4", "4:4", "25", "50", "75"};
        lc(SeqLane::Mix).text = mixT[size_t(clampIdx(lane.mix[A], 8))];
        lc(SeqLane::Fx).text = fxT[size_t(clampIdx(lane.fx[A], 5))];
        lc(SeqLane::Slt).text = lane.slt[A] ? std::to_string(std::clamp<int>(lane.slt[A], 1, 8)) : "-";
        {
            const int sv = lane.slc[A];
            const bool act = (lane.pat[A] >= 0 && lane.slide[A]) ||
                             (lane.pat[A] < 0 && (lane.slide[A] || lane.tie[A]) && ed.slideRunsAt(a));
            lc(SeqLane::Slc).text = sv ? std::string(ymseq::kSlideCurveShort[size_t(clampIdx(sv, 13))]) : "-";
            lc(SeqLane::Slc).dim = !act && !sv;  // a curve written there is crossed (item 145)
        }
        lc(SeqLane::Rtg).text = rtgT[size_t(clampIdx(lane.rtg[A], 4))];
        lc(SeqLane::Cnd).text = cndT[size_t(clampIdx(lane.cnd[A], 8))];
        auto sg = [](int v) { return v > 0 ? "+" + std::to_string(v) : std::to_string(v); };
        const int o = lane.oct[A];
        lc(SeqLane::Oct).text = o ? (drum_ ? sg(o) : std::to_string(o)) : "-";
        lc(SeqLane::Ton).text = lane.ton[A] ? sg(lane.ton[A]) : "-";
        lc(SeqLane::Ins).text = lane.ins[A] ? hex2(lane.ins[A]) : "-";
        lc(SeqLane::Cmd).text = ymseq::cmdCellText(lane, int(A));
        {  // rows of their own: On or Off, the tempo of the pattern, the
           // shape (8 to F) and R for a restart, a tick, the offset, the cents
            using ymengine::SeqCmd;
            auto param = [&](SeqCmd k) { return int(ymengine::seqCmdParam(lane.cmdAt(k, int(A)))); };
            auto number = [&](SeqCmd k) { return lane.cmdAt(k, int(A)) ? std::to_string(param(k)) : "-"; };
            lc(SeqLane::Afl).text = switchText(lane.cmdAt(SeqCmd::Arpeggiator, int(A)));
            lc(SeqLane::Vib).text = switchText(lane.cmdAt(SeqCmd::Vibrato, int(A)));
            lc(SeqLane::Lfo).text = switchText(lane.cmdAt(SeqCmd::Lfo, int(A)));
            lc(SeqLane::Mod).text = switchText(lane.cmdAt(SeqCmd::Mod, int(A)));
            const int bpm = w_.proj().cur().bpm[A];
            lc(SeqLane::Bpm).text = bpm ? std::to_string(bpm) : "-";
            std::string env;
            if (lane.cmdAt(SeqCmd::Shape, int(A))) env += "0123456789ABCDEF"[param(SeqCmd::Shape) & 15];
            if (lane.cmdAt(SeqCmd::Retrig, int(A))) env += 'R';
            lc(SeqLane::Env).text = env.empty() ? "-" : env;
            lc(SeqLane::Dly).text = number(SeqCmd::Delay);
            lc(SeqLane::NoteCut).text = number(SeqCmd::Cut);
            lc(SeqLane::Smp).text = number(SeqCmd::Offset);
            if (lane.cmdAt(SeqCmd::Fine, int(A))) {
                const int ct = std::clamp(param(SeqCmd::Fine) - ymengine::kFineCmdCenter, -kFineShown, kFineShown);
                lc(SeqLane::Tun).text = ct > 0 ? "+" + std::to_string(ct) : std::to_string(ct);
            } else lc(SeqLane::Tun).text = "-";
        }
        lc(SeqLane::Gat).text = lane.gat[A] ? std::to_string(lane.gat[A]) : "-";
        // PAN: 0 = the channel's pan, otherwise v·10 + 101 (v from -10 to +10); C for both 0 and the centre, else
        // L1-L10 or R1-R10.
        const int pnv = lane.pnr[A];
        lc(SeqLane::Pnr).text =
            !pnv || pnv == ymseq::kPanCenter
                ? "C"
                : std::string(pnv < ymseq::kPanCenter ? "L" : "R") +
                      std::to_string(std::max(1, int(std::lround(std::abs(pnv - ymseq::kPanCenter) / 10.0))));
        // Values that cannot act there: greyed and crossed (twelfth DAW test, item 145); empty cells where
        // nothing written could act, from the sound of the step, crossed too in the length of the track (thirteenth,
        // item 165).
        for (const SeqLane r : {SeqLane::Mix, SeqLane::Fx, SeqLane::Ins, SeqLane::Env, SeqLane::Smp, SeqLane::Gat,
                                SeqLane::Rtg, SeqLane::Cnd, SeqLane::Oct, SeqLane::Ton, SeqLane::Afl, SeqLane::Vib,
                                SeqLane::Lfo, SeqLane::Mod, SeqLane::Tun, SeqLane::Dly, SeqLane::Slc, SeqLane::Bpm}) {
            if (!laneShown_[size_t(r)] || w_.cantAct(r, a).empty()) continue;
            const bool empty = lc(r).text == "-";
            lc(r).nope = !empty || !dead;
            lc(r).blocked = lc(r).nope && empty;
        }
    }
    repaint();
    if (w_.ses().overview) w_.overMap_.repaint();  // the overview follows the grid (refreshOverview)
}

void SeqGrid::buildGhosts(const ymseq::LaneEditor& ed) {
    // buildGhostIndex: notes of the other melodic tracks, at the pitch of the current track. Only the playable tracks
    // count (the prototype went through the 16 tracks even in 3-channel mode).
    for (auto& col : ghosts_) col.fill(Ghost{});
    if (!w_.ses().ghost || drum_) return;
    (void)ed;
    const ymseq::Project& pr = w_.proj();
    const int cur = w_.cur(), base = (w_.trackOct() + 1) * 12, start = w_.ses().viewStart;
    for (int j = 0; j < w_.m_.voices(); ++j) {
        if (j == cur) continue;
        const int g = int(plugin::effectiveGenerator(w_.m_.values_, j));
        if (g == int(ymengine::Generator::Digidrum) || g == int(ymengine::Generator::Chipdrum)) continue;
        const ymseq::Lane& l = pr.cur().lane(j);
        const int len = pr.trackLength(j), oct = pr.tracks[size_t(j)].octave;
        for (int s = 0; s < len; ++s) {
            if (l.pat[size_t(s)] < 0) continue;
            // Absolute note of the other track's root, as a relative row of the current track; d: its length in
            // steps (the tied steps that follow it).
            const int row = (ymseq::octaveOf(l.oct[size_t(s)], oct) + 1) * 12 + l.pat[size_t(s)] - base;
            int d = 1;
            while (s + d < len && l.pat[size_t(s + d)] < 0 && l.tie[size_t(s + d)]) ++d;
            const int r = viewSemi_ + 12 - row;
            if (r < 0 || r >= kRows) continue;
            for (int k = 0; k < d; ++k) {
                const int col = s + k - start;
                if (col < 0 || col >= kCols) continue;
                ghosts_[size_t(col)][size_t(r)] = {true, k == 0, k == d - 1};
            }
        }
    }
}

void SeqGrid::markStep(int step) {
    if (step == playStep_) return;
    playStep_ = step;
    repaint();
    if (w_.ses().overview) w_.overMap_.repaint();  // playhead of the overview
}

void SeqGrid::clearRect() {
    if (!rect_) return;
    rect_.reset();
    refresh();
}

void SeqGrid::clearBlock() {
    selA_ = selB_ = -1;
    selRow_.reset();
    refresh();
}

void SeqGrid::clearNoteSel() {
    if (noteSel_.empty()) return;
    noteSel_.clear();
    refresh();
}

void SeqGrid::clearSelections() {
    rect_.reset();
    rightSel_.reset();
    selA_ = selB_ = -1;
    selRow_.reset();
    noteSel_.clear();
}

ymseq::RectBounds SeqGrid::rectBounds() const {
    if (!rect_) return {};
    return {std::min(rect_->a, rect_->b), std::max(rect_->a, rect_->b), std::min(rect_->r1, rect_->r2),
            std::max(rect_->r1, rect_->r2)};
}

void SeqGrid::setNoteSel(std::vector<ymseq::SelKey> keys) {
    noteSel_ = std::move(keys);
    refresh();
}

void SeqGrid::setBlock(int a, int b) {
    selA_ = a;
    selB_ = b;
    refresh();
}

void SeqGrid::setAlt(bool alt) {
    if (alt == alt_) return;
    alt_ = alt;
}

// ------------------------------------------------------------------------------------------- drawing

void SeqGrid::paint(Painter& p) {
    const Rect gb = bounds();
    p.fill(gb, colors::kBlack);  // border, gaps and labels
    const auto rs = rows();
    const int lx = gb.x + kBorder;
    // Content without position (labels, step numbers), then the cells (position:relative) in DOM order.
    for (const RowBox& b : rs) {
        const Rect lab{lx, gb.y + b.y, kLabelW, b.h};
        switch (b.kind) {
        case RowBox::Kind::Header: paintHeader(p, b); break;
        case RowBox::Kind::Note: paintLabel(p, lab, noteLabels_[size_t(b.index)], labelDis_[size_t(b.index)]); break;
        case RowBox::Kind::Lane: paintLabel(p, lab, kLaneNames[size_t(b.index)], false); break;
        case RowBox::Kind::Sep: break;
        }
    }
    const int start = w_.ses().viewStart;
    // Cell far from the drawing area: nothing visible to paint (margin of one cell for the .psel outline and a text
    // that would overflow).
    const auto away = [&p](const Rect& r) {
        return Rect{r.x - kCellW, r.y - kCellW, r.w + 2 * kCellW, r.h + 2 * kCellW}.intersect(p.clip()).empty();
    };
    for (const RowBox& b : rs) {
        if (b.kind == RowBox::Kind::Note)
            for (int c = 0; c < kCols; ++c) {
                const Rect r{colX(c), gb.y + b.y, kCellW, b.h};
                if (away(r)) continue;
                const NoteCell& nc = cells_[size_t(c)][size_t(b.index)];
                const bool play = start + c == playStep_;
                if (nc.dead) p.beginOpacity(r, 0.35);
                paintNoteCell(p, r, nc, play);
                if (nc.dead) p.endOpacity();
                // .psel: 4 px outline at -2 px, painted with the cell (the following cells cover it).
                if (nc.psel) p.frame({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 4, colors::kBlack);
            }
        else if (b.kind == RowBox::Kind::Lane)
            for (int c = 0; c < kCols; ++c) {
                const Rect r{colX(c), gb.y + b.y, kCellW, b.h};
                if (away(r)) continue;
                const bool dead = lanes_[size_t(b.index)][size_t(c)].dead;
                if (dead) p.beginOpacity(r, 0.35);
                paintLaneCell(p, r, SeqLane(b.index), c, start + c == playStep_);
                if (dead) p.endOpacity();
            }
    }
}

void SeqGrid::paintHeader(Painter& p, const RowBox& b) const {
    // .snum: 13 px including a 3 px bottom rule, 8 px font on an 11 px line; .bar in bold with a 4 px stroke on the
    // left; .play in green.
    const int top = bounds().y + b.y;
    const int start = w_.ses().viewStart;
    const int base = baselineIn(top, 11, 8);
    for (int c = 0; c < kCols; ++c) {
        const StepNum& sn = stepNums_[size_t(c)];
        const Rect r{colX(c), top, kCellW, b.h - 3};
        const bool play = start + c == playStep_;
        p.fill(r, play ? colors::kGreen : colors::kWhite);
        // No stroke on the first column: against the black name column, it made that column look 4 px wider (third
        // DAW test, item 24).
        if (sn.bar && c > 0) p.fill({r.x, r.y, 4, r.h}, colors::kBlack);
        const Color ink = play ? colors::kWhite : sn.bar || sn.qtr ? colors::kBlack : kStepNum;
        smallText(p, sn.text, r.x, r.w, base, ink, sn.bar);
    }
}

void SeqGrid::paintLabel(Painter& p, const Rect& r, const std::string& text, bool dis) const {
    // .rl: 10 px white text centred on black; .dis (degree out of scale) at 45 %.
    if (text.empty()) return;
    const double tw = Font::width(text, 10);
    const double lh = Font::lineBox(text, 10).height();
    p.text(text, r.x + (r.w - tw) / 2, r.y + (r.h - lh) / 2, 10, dis ? kLabelDis : colors::kWhite);
}

// Note cell in the CSS paint order: background of the class cascade (or the checkerboard of a muted voice), inset
// shadow, white borders of the note, octave digit, then the ghost band (::before).
void SeqGrid::paintNoteCell(Painter& p, const Rect& r, const NoteCell& c, bool play) const {
    unsigned f = 0;
    if (c.nope) f |= kNope;
    if (c.qtr) f |= kQtrBit;
    if (c.offsc) f |= kOffscBit;
    if (play) f |= kPlay;
    if (c.on) f |= kOnc;
    if (c.dead) f |= kDead;
    if (c.sel) f |= kSel;
    if (c.held) f |= kHeld;
    // 1 px white borders of the notes (border-box): top and bottom, left for a start, right for an end.
    const bool bt = c.on || c.held, bl = c.on, br = c.nend && (c.on || c.held);
    const Rect pad{r.x + (bl ? 1 : 0), r.y + (bt ? 1 : 0), r.w - (bl ? 1 : 0) - (br ? 1 : 0), r.h - (bt ? 2 : 0)};
    if (c.mute) {  // voice kept but muted: checkerboard on white, !important
        checker(p, r, {pad.x, pad.y});
    } else {
        const Bg bg = cellBackground(f);
        if (bg.stripes) stripes(p, r, pad);
        else p.fill(r, bg.c);
    }
    insetShadow(p, pad, (c.on && c.sel) || c.cur, c.bar);
    if (bt) {
        p.fill({r.x, r.y, r.w, 1}, colors::kWhite);
        p.fill({r.x, r.bottom() - 1, r.w, 1}, colors::kWhite);
    }
    if (bl) p.fill({r.x, r.y, 1, r.h}, colors::kWhite);
    if (br) p.fill({r.right() - 1, r.y, 1, r.h}, colors::kWhite);
    if (c.on) {  // octave of the note: black on black, except while playing or on the checkerboard
        const Color ink = play ? colors::kWhite : colors::kBlack;
        const std::string t = std::to_string(c.octave);
        const double lh = Font::lineBox(t, 9).height();
        p.text(t, pad.x + (pad.w - Font::width(t, 9, 0, gemui::Face::kSmall)) / 2, pad.y + (pad.h - lh) / 2, 9, ink, 0,
               gemui::Face::kSmall);
    }
    if (c.ghost) {  // ::before: 12 px grey band edged with white
        Rect g{r.x, r.y + 2, r.w, r.h - 4};
        if (c.gstart) g = {g.x + 1, g.y, g.w - 1, g.h};
        if (c.gend) g = {g.x, g.y, g.w - 1, g.h};
        p.fill(g, kGhost);
        p.fill({g.x, g.y, g.w, 1}, colors::kWhite);
        p.fill({g.x, g.bottom() - 1, g.w, 1}, colors::kWhite);
        if (c.gstart) p.fill({g.x, g.y, 1, g.h}, colors::kWhite);
        if (c.gend) p.fill({g.right() - 1, g.y, 1, g.h}, colors::kWhite);
    }
}

void SeqGrid::paintLaneCell(Painter& p, const Rect& r, SeqLane lane, int col, bool play) const {
    const LaneCell& c = lanes_[size_t(lane)][size_t(col)];
    if (lane == SeqLane::Vol) {
        // .volbar: white background (green while playing), 11 px bar from the bottom, 8 px value at the top, or in
        // white inside the bar when it goes above 70 %. A step that slides (Gxx) draws a ramp from its volume to
        // that of the next step, its value inside when the whole ramp goes above 70 %.
        const int v = vol_[size_t(col)], to = volTo_[size_t(col)];
        p.fill(r, play ? colors::kGreen : colors::kWhite);
        const Color bar = play ? kVolPlayBar : colors::kBlack;
        auto barH = [&](double x) { return int(std::ceil(r.h * x / 15.0 - 0.5)); };  // to the nearest pixel, halves down
        if (to < 0) {
            const int bh = barH(v);
            p.fill({r.x + 3, r.bottom() - bh, 11, bh}, bar);
        } else {
            for (int k = 0; k < 11; ++k) {
                const int bh = barH(v + (to - v) * (k + 0.5) / 11);
                p.fill({r.x + 3 + k, r.bottom() - bh, 1, bh}, bar);
            }
        }
        const bool low = std::min(v, to < 0 ? v : to) * 100 > 70 * 15;
        const std::string t = std::to_string(v);
        const int base = low ? baselineIn(r.bottom() - 1 - 8, 8, 8) : baselineIn(r.y + 1, 8, 8);
        smallText(p, t, r.x, r.w, base, low ? colors::kWhite : colors::kBlack);
        return;
    }
    if (c.nope) {
        // A value that cannot act there (twelfth DAW test, item 145): grey, crossed by a 9-pixel X; the value
        // is in the help of the cell.
        p.fill(r, play ? kOffscPlay : colors::kGrey);
        const int x0 = r.x + (r.w - 9) / 2, y0 = r.y + (r.h - 9) / 2;
        const Color ink = play ? colors::kWhite : colors::kBlack;
        for (int i = 0; i < 9; ++i) {
            p.pixel(x0 + i, y0 + i, ink);
            p.pixel(x0 + 8 - i, y0 + i, ink);
        }
        return;
    }
    unsigned f = 0;
    if (c.qtr) f |= kQtrBit;
    if (play) f |= kPlay;
    if (c.on) f |= kOnc;
    if (c.dead) f |= kDead;
    if (c.sel) f |= kSel;
    p.fill(r, cellBackground(f).c);
    const Rect pad{r.x + (c.on ? 1 : 0), r.y + (c.on ? 1 : 0), r.w - (c.on ? 1 : 0), r.h - (c.on ? 2 : 0)};
    insetShadow(p, pad, c.on && c.sel, false);
    if (c.on) {
        p.fill({r.x, r.y, r.w, 1}, colors::kWhite);
        p.fill({r.x, r.bottom() - 1, r.w, 1}, colors::kWhite);
        p.fill({r.x, r.y, 1, r.h}, colors::kWhite);
    }
    // .vcell: 9 px on a centred 9 px line; .dim in grey (wins over the white of playback).
    const Color ink = c.dim ? colors::kGreyDisabled : play ? colors::kWhite : colors::kBlack;
    smallText(p, c.text, r.x, r.w, baselineIn(r.y + (r.h - 9) / 2.0, 9, 9) - 1, ink);
}

// ------------------------------------------------------------------------------------------- pointer, help

gemui::Pointer SeqGrid::pointerAt(gemui::Point pt) const noexcept {
    // Grid cursor (setSeqTool): applies to the gaps, the labels and the inert cells. Over a cell, the pointer tells
    // what a press would do there (see pressNote): move, draw, cut (Alt), resize (.rz) or nothing (.nope).
    const int tool = w_.ses().tool;
    const Pointer grid = tool == 2 ? Pointer::Crosshair
                         : tool == 3 || tool == 4 ? Pointer::ResizeH
                         : tool == 5 ? Pointer::Cell
                         : tool == 0 ? Pointer::Move
                                     : Pointer::Arrow;
    // Draw tool, a note held under the button: it can be dragged, so the hand until the release (24/10/2026).
    if (moving_ && tool == 1) return Pointer::Hand;
    const Hit h = hitAt(pt);
    if (drawing_ && h.cell()) return Pointer::Pencil;  // #seqgrid.drawing .cell
    switch (h.kind) {
    case Hit::Kind::LaneLabel: return Pointer::ContextMenu;
    case Hit::Kind::Lane: {
        const LaneCell& c = lanes_[size_t(h.lane)][size_t(h.col)];
        if (c.on && tool == 0) return Pointer::Move;
        if (c.on && alt_ && !drum_) return Pointer::Scissors;
        return Pointer::Hand;
    }
    case Hit::Kind::Note: {
        const NoteCell& c = cells_[size_t(h.col)][size_t(h.row)];
        if (edgeAt(h, pt)) return Pointer::ResizeH;  // the edge of a note can be pulled (item 141)
        if (tool == 1) return Pointer::Pencil;  // #seqgrid.tool-draw .cell[data-off]
        if (tool == 0) {
            if (c.rz || c.on || c.held) return Pointer::Move;
            return c.nope ? Pointer::NotAllowed : Pointer::Hand;
        }
        if (alt_ && !drum_ && (c.on || c.held)) return c.rz ? grid : Pointer::Scissors;
        if (c.rz) return Pointer::ResizeH;
        return c.nope ? Pointer::NotAllowed : Pointer::Hand;
    }
    default: return grid;
    }
}

std::string SeqGrid::tipAt(gemui::Point pt) const {
    const Hit h = hitAt(pt);
    switch (h.kind) {
    case Hit::Kind::Step: return "Step " + std::to_string(w_.ses().viewStart + h.col + 1);
    case Hit::Kind::LaneLabel:
        if (h.lane == SeqLane::Bpm) return "BPM: one row for every track of the pattern";
        return std::string(h.lane == SeqLane::Tie ? "HLD: do not retrigger the drum while it still rings\n" : "") +
               "Right click: copy / paste this row across tracks";
    case Hit::Kind::Lane: {
        if (h.lane == SeqLane::Vol) {
            const int to = volTo_[size_t(h.col)];
            return "Volume " + std::to_string(vol_[size_t(h.col)]) +
                   (to >= 0 ? ", slides to " + std::to_string(to) : std::string()) +
                   "\nRight click: a slider and the Slide to the next step";
        }
        const LaneCell& c = lanes_[size_t(h.lane)][size_t(h.col)];
        if (c.blocked)  // why nothing can act there (item 165)
            return std::string(kLaneHelp[size_t(h.lane)]) + "\n" + w_.cantAct(h.lane, w_.absCol(h.col));
        if (c.nope)  // what is written, and why it cannot act (item 145)
            return std::string(kLaneHelp[size_t(h.lane)]) + "\n" + c.text + ": " +
                   w_.cantAct(h.lane, w_.absCol(h.col));
        return kLaneHelp[size_t(h.lane)];
    }
    default: return {};
    }
}

bool SeqGrid::mouseWheel(const gemui::WheelEvent& e) {
    // Wheel: one semitone per notch (drums: fixed rows).
    if (drum_ || e.dy == 0) return false;
    w_.setViewBase(w_.ses().viewAbsBase + (e.dy > 0 ? -1 : 1));
    return true;
}

// ------------------------------------------------------------------------------------------- gestures

std::optional<SeqGrid::Edge> SeqGrid::edgeAt(const Hit& h, gemui::Point p) const {
    const int tool = w_.ses().tool;
    // The Draw tool alone (bug of 24/10/2026): with the Move tool, the edge moves the note as its middle does.
    if (h.kind != Hit::Kind::Note || drum_ || alt_ || tool != 1) return std::nullopt;
    const int c = w_.absCol(h.col);
    ymseq::Lane copy = w_.lane();
    const ymseq::LaneEditor ed(copy, w_.context());
    const auto k = noteCovering(ed, copy, c, rowOff(h.row));
    if (!k) return std::nullopt;
    const int len = k->v >= 2 ? ed.voiceDur(k->s, k->v) : ed.noteLenAt(k->s);
    const int voice = k->v >= 2 ? k->v : 0;
    // The outer 40 % of the end steps, as in the prototype, which kept them for the Draw tool alone behind a pencil
    // pointer: the edge could not be found (twelfth DAW test, item 141). The middle of a step still moves the note.
    const int x = 5 * (p.x - h.r.x);
    if (x > 3 * h.r.w && c == k->s + len - 1) return Edge{k->s, len, voice, false};
    if (x < 2 * h.r.w && c == k->s && len > 1) return Edge{k->s, len, voice, true};
    return std::nullopt;
}

bool SeqGrid::selectedAt(int c, int off) const {
    const int tool = w_.ses().tool;
    if (drum_ || (tool != 0 && tool != 1) || (!rect_ && noteSel_.empty())) return false;
    ymseq::Lane copy = w_.lane();
    const ymseq::LaneEditor ed(copy, w_.context());
    const auto k = noteCovering(ed, copy, c, off);
    if (!k) return false;
    if (!rect_) return std::find(noteSel_.begin(), noteSel_.end(), *k) != noteSel_.end();
    const std::vector<ymseq::RectNote> in = ed.rectNotes(rectBounds());
    return std::any_of(in.begin(), in.end(), [&](const ymseq::RectNote& n) { return n.s == k->s && n.v == k->v; });
}

// VOL level (0-15) at height y of a VOL cell: 15 at the top, 0 at the bottom, rounded to the nearest level.
int SeqGrid::volAt(const Rect& r, int y) const noexcept {
    return std::clamp(int(std::lround((1 - double(y - r.y) / std::max(1, r.h)) * 15)), 0, 15);
}

void SeqGrid::mouseDown(const gemui::MouseEvent& e) {
    // A right press on a control row's name opens its menu at once; otherwise only a cell starts a gesture, and the
    // cell pressed becomes the last one entered.
    const Hit h = hitAt(e.pos);
    if (h.kind == Hit::Kind::LaneLabel && e.button == gemui::MouseButton::Right) {
        if (h.lane != SeqLane::Bpm) laneLabelMenu(h);  // BPM: one row for every track already
        return;
    }
    if (!h.cell()) return;
    pressed_ = true;
    pressAt_ = w_.stepTarget();
    button_ = e.button;
    last_ = h;
    press(h, e);
}

void SeqGrid::mouseDrag(const gemui::MouseEvent& e) {
    if (!pressed_) return;
    // The pattern or the track changed under the press: the gesture ends there, as if cut short, and goes on in
    // neither of them.
    if (!w_.stillOn(pressAt_)) {
        mouseCancel();
        return;
    }
    const Hit h = hitAt(e.pos);
    // pointerover: only on entering another cell. Deviation: a drag on VOL follows the pointer inside the
    // cell too, the level being read from the height.
    const bool volDrag = paint_ && paint_->vol && h.kind == Hit::Kind::Lane && h.lane == SeqLane::Vol;
    if (h == last_ && !volDrag) return;
    last_ = h;
    over(h, e.pos);
}

void SeqGrid::mouseUp(const gemui::MouseEvent& e) {
    if (!pressed_) return;
    if (!w_.stillOn(pressAt_)) {  // a click on a note would act on the note at that step of the other pattern
        mouseCancel();
        return;
    }
    pressed_ = false;
    release();
    // Context menu on release of the right button (Windows order: pointerup then contextmenu).
    if (button_ == gemui::MouseButton::Right) contextMenu(hitAt(e.pos));
}

// The press ends without a release (an alert opened, the window closed): the gesture stops where it is. What a drag
// already wrote stays; a press that has not moved does nothing (release() would act on the note as a click), and no
// state of it is left for the next press to finish.
void SeqGrid::mouseCancel() {
    if (!pressed_) return;
    pressed_ = false;
    drawing_ = false;
    selDrag_ = 0;
    if (rightSel_ && !rightSel_->moved) rect_.reset();  // the one-cell box of the press, as release() drops it
    rightSel_.reset();
    group_.reset();
    sizing_.reset();
    moving_.reset();
    paint_.reset();
    refresh();
}

void SeqGrid::resetFlag(SeqLane lane, int c) {
    // Reset of a control row (VOL: 15; CMD: all its commands).
    // BPM: the row of the pattern; ENV: the shape and the restart (twelfth DAW test, items 146, 150).
    if (lane == SeqLane::Bpm) {
        w_.setBpm(c, 0);
        return;
    }
    const ymseq::Row row = laneRow(lane);
    w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
        if (lane == SeqLane::Cmd)
            l.clearBoxCmds(c);
        else {
            l.set(row, c, lane == SeqLane::Vol ? 15 : 0);
            l.set(ymseq::rowPartner(row), c, ymseq::Lane::emptyValue(ymseq::rowPartner(row)));
        }
        return true;
    });
}

// pointerdown on a cell: the right button first, then Shift (selections), then a plain press (pressLane, pressNote).
void SeqGrid::press(const Hit& h, const gemui::MouseEvent& e) {
    w_.armUndo();  // one undo step per gesture, taken at its first change
    const int c = w_.absCol(h.col);
    const bool right = e.button == gemui::MouseButton::Right;
    if (right && h.kind == Hit::Kind::Lane && h.lane == SeqLane::Vol && selA_ < 0) {  // VOL: slider
        w_.openVol(h.r, c);
        return;
    }
    if (right && h.kind == Hit::Kind::Lane && selA_ < 0 && w_.openStepSound(h.lane, c)) return;
    if (right && selA_ >= 0 && c >= blockLo() && c <= blockHi()) return;  // block menu on release
    if (right && rect_ && h.kind == Hit::Kind::Note) {
        // Deviation: the box menu on release, the box kept (the right button drew a new one-cell box over
        // it, which the release dropped: the box menu never came).
        const ymseq::RectBounds rb = rectBounds();
        const int off = rowOff(h.row);
        if (c >= rb.a && c <= rb.b && off >= rb.r1 && off <= rb.r2) return;
    }
    // Any other right press: the menu, if any, comes on release (contextMenu).
    if (right) {
        if (h.kind == Hit::Kind::Lane) {  // eraser (slide possible)
            resetFlag(h.lane, c);
            paint_ = Paint::erasing();
            erasedByRight_ = true;  // no menu after a reset
            refresh();
            return;
        }
        if (!drum_) {  // mark out an area, without erasing anything
            const int off = rowOff(h.row);
            rect_ = RectSel{c, c, off, off};
            selA_ = selB_ = -1;
            selRow_.reset();
            selDrag_ = 2;
            rightSel_ = RightSel{c, off};
            refresh();
        }
        return;
    }
    // Shift: on a note, add it to (or take it from) the notes picked one by one; on an empty note cell, start a box;
    // on a control row or a percussion track, start a block of steps.
    if (e.mods.shift) {
        if (h.kind == Hit::Kind::Lane && h.lane == SeqLane::Bpm) return;  // not a row of the track: no block
        if (h.kind == Hit::Kind::Note && !drum_) {
            const int off = rowOff(h.row);
            ymseq::Lane copy = w_.lane();
            const ymseq::LaneEditor ed(copy, w_.context());
            int sS = -1, vS = 0;  // start step and voice (1 = root) of the note under the pointer
            if (const auto hv = ed.voiceStartCovering(c, off)) {  // chord voice
                sS = hv->start;
                vS = hv->voice;
            } else {
                const int s0 = copy.pat[size_t(c)] >= 0 ? c : ed.noteStart(c);
                if (s0 >= 0 && ed.relNote(s0) == off) {
                    sS = s0;
                    vS = 1;
                }
            }
            if (sS >= 0) {  // on a note: multiple selection
                rect_.reset();
                const ymseq::SelKey k{sS, vS};
                const auto it = std::find(noteSel_.begin(), noteSel_.end(), k);
                if (it != noteSel_.end()) noteSel_.erase(it);
                else noteSel_.push_back(k);
                refresh();
                return;
            }
            // The prototype added viewStart a second time (ABS(c)): fixed.
            rect_ = RectSel{c, c, off, off};
            selA_ = selB_ = -1;
            selRow_.reset();
            selDrag_ = 2;
            refresh();
            return;
        }
        selA_ = selB_ = c;  // control rows (and drums): block of steps
        selDrag_ = 1;
        selRow_ = h.kind == Hit::Kind::Lane ? std::optional(h.lane) : std::nullopt;
        refresh();
        return;
    }
    // A drag from a note of the selection moves the whole selection (twelfth DAW test, item 147: the note pressed went
    // alone); a click without a drag still ends the selection, on release.
    const bool grab = h.kind == Hit::Kind::Note && !e.mods.alt && !edgeAt(h, e.pos) && selectedAt(c, rowOff(h.row));
    if (!grab) {
        rect_.reset();  // click without Shift: end of selection
        noteSel_.clear();
    }
    if (h.kind == Hit::Kind::Lane) pressLane(h, c, e);
    else pressNote(h, c, e);
    if (!grab) return;
    if (moving_) group_ = GroupMove{c, rowOff(h.row)};
    else {
        rect_.reset();
        noteSel_.clear();
        refresh();
    }
}

// Plain press on a control cell: VOL and the toggle rows change at once and keep painting while dragged; the value
// rows open their menu or box, whose choice later takes the undo step that press() armed.
void SeqGrid::pressLane(const Hit& h, int c, const gemui::MouseEvent& e) {
    EditorSession& s = w_.ses();
    s.curStep = c;
    const SeqLane lane = h.lane;
    if (lanes_[size_t(lane)][size_t(h.col)].blocked) return;  // nothing written there could act (item 165)
    if (lane == SeqLane::Vol) {  // left click: drawing by dragging
        const int v = volAt(h.r, e.pos.y);
        w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            l.vol[size_t(c)] = uint8_t(v);
            return true;
        });
        paint_ = Paint::volume();
        refresh();
        return;
    }
    if (isFlag(lane)) {  // toggle, drawing by dragging
        const ymseq::Row row = laneRow(lane);
        const int val = w_.lane().get(row, c) ? 0 : 1;
        w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            l.set(row, c, val);
            return true;
        });
        paint_ = Paint::flagged(lane, val);
        refresh();
        return;
    }
    refresh();
    laneMenu(h, c);
}

void SeqGrid::laneMenu(const Hit& h, int c) {
    // Menus of the value rows (mix, fx, slt, oct, ton, slc, rtg, cnd) and dialogue boxes (gat, pnr).
    const SeqLane lane = h.lane;
    if (lane == SeqLane::Gat) {
        w_.openGate(h.r, c);
        return;
    }
    if (lane == SeqLane::Pnr) {
        w_.openPan(h.r, c);
        return;
    }
    if (lane == SeqLane::Ins) {
        insMenu(h, c);
        return;
    }
    if (lane == SeqLane::Cmd) {
        w_.askCmd(c);
        return;
    }
    if (lane == SeqLane::Bpm) {
        w_.openBpm(h.r, c);
        return;
    }
    if (lane == SeqLane::Dly || lane == SeqLane::NoteCut || lane == SeqLane::Smp || lane == SeqLane::Tun) {
        w_.openCmdValue(h.r, c, ymseq::cmdKind(laneRow(lane)));
        return;
    }
    if (lane == SeqLane::Env) {
        envMenu(h, c);
        return;
    }
    const ymseq::Row row = laneRow(lane);
    const ymseq::Lane& l = w_.lane();
    std::vector<std::string> labels;
    int base = 0;  // value of the first entry
    std::vector<int> seps;
    std::vector<int> values;  // values of the entries when they do not follow from base
    std::vector<const gemui::Bitmap*> pics;  // icons of the entries
    switch (lane) {
    case SeqLane::Vib:  // V01 starts the vibrato of the track, V00 stops it
    case SeqLane::Lfo:  // likewise the LFO (Lxx), the modulation table (Mxx) and the arpeggiator (Jxx): items 142-144
    case SeqLane::Mod:
    case SeqLane::Afl: {
        const ymengine::SeqCmd k = ymseq::cmdKind(row);
        labels = {"No change", "On", "Off"};
        values = {0, ymengine::seqCmd(k, 1), ymengine::seqCmd(k, 0)};
        break;
    }
    case SeqLane::Mix:  // the Env entries put the volume on the envelope (sixth DAW test, item 79)
        labels = {"Channel", "Tone", "Noise", "Tone+Noise", "Env", "Tone+Env", "Noise+Env", "Tone+Noise+Env"};
        seps = {ymengine::kSeqMixEnv};
        break;
    case SeqLane::Fx: labels = {"Channel", "Chip Voice", "SID Voice", "Sync Buzzer"}; break;
    case SeqLane::Slt:
        labels = {"Channel", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8"};
        break;
    case SeqLane::Oct:
        if (drum_) {  // tuning of the instrument, by octaves
            labels = {"-3", "-2", "-1", "0", "+1", "+2", "+3"};
            base = -3;
        } else labels = {"Channel", "1", "2", "3", "4", "5", "6", "7"};
        break;
    case SeqLane::Ton:  // tuning by the semitone
        for (int k = -12; k <= 12; ++k) labels.push_back(k > 0 ? "+" + std::to_string(k) : std::to_string(k));
        base = -12;
        break;
    case SeqLane::Slc:
        for (const auto& n : ymseq::kSlideCurveNames) labels.emplace_back(n);
        seps.assign(ymseq::kSlideCurveSeps.begin(), ymseq::kSlideCurveSeps.end());
        break;
    case SeqLane::Rtg: labels = {"1", "2", "3", "4"}; break;  // hits within the step
    case SeqLane::Cnd: labels = {"Always", "1:2", "2:2", "1:4", "4:4", "25%", "50%", "75%"}; break;
    default: return;
    }
    std::vector<MenuItem> items;
    for (int k = 0; k < int(labels.size()); ++k) {
        if (std::find(seps.begin(), seps.end(), k) != seps.end()) items.push_back(MenuItem::separatorItem());
        MenuItem m;
        m.label = labels[size_t(k)];
        if (size_t(k) < pics.size()) m.icon = pics[size_t(k)];
        const int v = values.empty() ? base + k : values[size_t(k)];
        m.pick = [this, row, c, v, lane] {
            w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& ln) {
                ln.set(row, c, v);
                return true;
            });
            if (lane == SeqLane::Slc && v == 12) w_.m_.openCurve();  // Custom: open the editor
        };
        items.push_back(std::move(m));
    }
    int checked = l.get(row, c) - base;
    if (!values.empty()) {
        const auto it = std::find(values.begin(), values.end(), l.get(row, c));
        checked = it == values.end() ? -1 : int(it - values.begin());
    }
    w_.popupAt(h.r, std::move(items), checked);
}

void SeqGrid::envMenu(const Hit& h, int c) {
    // ENV (eleventh and twelfth DAW tests, items 138 and 146): the shapes of the Shape R13 menu,
    // No change keeping the shape of the track; Restart: the envelope restarts at once (R00) or at a tick of the line.
    using ymengine::SeqCmd;
    const ymseq::Lane& l = w_.lane();
    const int shape = l.cmdAt(SeqCmd::Shape, c) ? int(ymengine::seqCmdParam(l.cmdAt(SeqCmd::Shape, c))) : -1;
    const uint16_t rst = l.cmdAt(SeqCmd::Retrig, c);
    const auto write = [this, c](SeqCmd k, int v) {  // v < 0: none
        w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& ln) {
            if (v < 0) ln.clearCmd(k, c);
            else ln.setCmd(c, ymengine::seqCmd(k, uint8_t(v)));
            return true;
        });
    };
    std::vector<MenuItem> items;
    MenuItem none;
    none.label = "No change";
    none.pick = [write] { write(SeqCmd::Shape, -1); };
    items.push_back(std::move(none));
    items.push_back(MenuItem::separatorItem());
    for (int k = ymengine::kShapeCmdMin; k <= ymengine::kShapeCmdMax; ++k) {
        MenuItem m;
        m.label = std::string(1, "0123456789ABCDEF"[k]);
        m.icon = &icons::kShapes[size_t(k)];
        m.pick = [write, k] { write(SeqCmd::Shape, k); };
        items.push_back(std::move(m));
    }
    items.push_back(MenuItem::separatorItem());
    MenuItem restart;
    restart.label = "Restart";
    restart.subMarks = true;
    restart.subCur = !rst ? 0 : ymengine::seqCmdParam(rst) ? 2 : 1;
    const std::string tick = rst && ymengine::seqCmdParam(rst) ? " " + std::to_string(ymengine::seqCmdParam(rst)) : "";
    const std::array<std::string, 3> labels = {"None", "At once", "At tick" + tick + "..."};
    for (int k = 0; k < 3; ++k) {
        MenuItem m;
        m.label = labels[size_t(k)];
        if (k < 2) m.pick = [write, k] { write(SeqCmd::Retrig, k ? 0 : -1); };
        else m.pick = [this, r = h.r, c] { w_.openCmdValue(r, c, SeqCmd::Retrig); };
        restart.sub.push_back(std::move(m));
    }
    items.push_back(std::move(restart));
    const int checked = shape < 0 ? 0 : 1 + shape - ymengine::kShapeCmdMin;
    w_.popupAt(h.r, std::move(items), checked);
}

void SeqGrid::insMenu(const Hit& h, int c) {
    // "Channel" (the sound of the track), then the instruments of the project; an emptied slot that the step still
    // points to stays in the list, checked, to be seen.
    const int cur = w_.lane().ins[size_t(c)];
    const auto& ins = w_.st().instruments;
    std::vector<MenuItem> items;
    int checked = 0;
    auto add = [&](std::string label, int v) {
        if (v == cur) checked = int(items.size());
        MenuItem m;
        m.label = std::move(label);
        m.pick = [this, c, v] {
            w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& ln) {
                ln.ins[size_t(c)] = uint8_t(v);
                return true;
            });
        };
        items.push_back(std::move(m));
    };
    add("Channel", 0);
    for (int k = 0; k < plugin::kInstruments; ++k) {
        const plugin::Instrument& x = ins[size_t(k)];
        if (x.used) add(hex2(k + 1) + " " + plugin::instrumentLabel(x, k), k + 1);
        else if (k + 1 == cur) add(hex2(k + 1) + " (empty)", k + 1);
    }
    items.push_back(MenuItem::separatorItem());
    MenuItem open;
    open.label = "Instruments...";
    open.pick = [this] { w_.m_.instruments().show(); };
    items.push_back(std::move(open));
    w_.popupAt(h.r, std::move(items), checked);
}

// Plain press on a note cell, in this order: the tools that act at once (Select, Split, Join, Erase, Move on an empty
// cell), a greyed voice promoted on a Monophonic track, a chord voice or a note grabbed by an edge (sizing_) or as a
// whole (moving_), then a write: new note, chord voice added or removed, new figure length, or the note removed.
// `copy` and `ed` read the track as it was at the press; changes go through SeqWindow::editLane.
void SeqGrid::pressNote(const Hit& h, int c, const gemui::MouseEvent& e) {
    EditorSession& s = w_.ses();
    const int keepStep = s.curStep, keepRow = s.curRow;  // restored if the gesture resizes
    s.curStep = c;
    s.curRow = h.row;
    const int off = rowOff(h.row);
    const int tool = s.tool;
    const bool dr = drum_;
    const bool ndrum = w_.chipdrum();
    ymseq::Lane copy = w_.lane();
    const ymseq::LaneEditor ed(copy, w_.context());
    const std::optional<Edge> edge = edgeAt(h, e.pos);

    // Active tool: it wins over the modifier gestures.
    if (tool == 5 && !dr) {  // selection frame
        rect_ = RectSel{c, c, off, off};
        selA_ = selB_ = -1;
        selRow_.reset();
        selDrag_ = 2;
        rightSel_ = RightSel{c, off};
        refresh();
        return;
    }
    if (tool == 3 && !dr) {  // scissors
        const int st = copy.pat[size_t(c)] >= 0 ? c : ed.noteStart(c);
        if (st >= 0 && ed.relNote(st) == off) w_.splitNote(st, c);
        refresh();
        return;
    }
    if (tool == 4 && !dr) {  // weld with the next note
        const int st = copy.pat[size_t(c)] >= 0 ? c : ed.noteStart(c);
        if (st >= 0 && ed.relNote(st) == off && ed.canJoin(st)) w_.joinNote(st);
        refresh();
        return;
    }
    if (tool == 2) {  // eraser: armed from the press, even on empty space
        paint_ = Paint::erasing();
        w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) { return e2.eraseNoteAt(c, off); });
        refresh();
        return;
    }
    if (tool == 0 && !ed.stepBusyAt(c, off)) {  // nothing to move
        refresh();
        return;
    }
    // Mono: promote a greyed voice.
    if (const auto hitM = ed.voiceStartCovering(c, off); hitM && w_.mono() && !e.mods.shift) {
        if (w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) { return e2.promoteVoice(c, *hitM); })) {
            w_.armChan();
            w_.previewNote(dr ? off : (w_.trackOct() + 1) * 12 + off);
        }
        return;
    }
    if (!dr)
        if (const auto hit = ed.voiceStartCovering(c, off)) {  // chord voice: like the main note
            const int s2 = hit->start, v2 = hit->voice, n2 = ed.voiceDur(s2, v2);
            if (edge && edge->voice == v2) {
                sizing_ = Sizing{s2, off, n2, v2, edge->left};
                s.curStep = keepStep;  // the cursor does not follow the handle
                s.curRow = keepRow;
                paint_.reset();
                refresh();
                return;
            }
            moving_ = ymseq::LaneEditor::Moving{s2, off, n2, false, false, v2, c - s2};
            paint_.reset();
            refresh();
            return;
        }
    // On an existing note: prepare a move, act only on release.
    {
        const int st = copy.pat[size_t(c)] >= 0 ? c : (!ndrum ? ed.noteStart(c) : -1);
        const bool onNote = st >= 0 && (dr ? (st == c && copy.pat[size_t(c)] == off) : ed.relNote(st) == off);
        if (onNote) {
            if (e.mods.alt && !dr) {  // Alt: split at the clicked step
                ymseq::Lane probe = copy;
                if (ymseq::LaneEditor(probe, w_.context()).splitNote(st, c)) {
                    w_.splitNote(st, c);
                    refresh();
                    return;
                }
            }
            const int n = ndrum ? 1 : ed.noteLenAt(st);  // a held Digidrum hit moves whole
            if (edge && !edge->voice) {
                sizing_ = Sizing{st, off, n, 0, edge->left};
                s.curStep = keepStep;
                s.curRow = keepRow;
                paint_.reset();
                refresh();
                return;
            }
            moving_ = ymseq::LaneEditor::Moving{st, off, n, false, dr, 0, c - st};
            paint_.reset();
            refresh();
            return;
        }
    }
    // Snap: a press on a greyed row writes on the nearest degree of the scale; if that degree already sounds
    // at this step, the press changes nothing (it does not take the note away).
    const int put = ed.snapRow(off);
    if (put != off && ed.stepBusyAt(c, put)) {
        refresh();
        return;
    }
    // setting: the press writes (the root of the step is not on this row); otherwise it takes the root away, or gives
    // it the length of the input figure.
    const bool setting = !(copy.pat[size_t(c)] >= 0 && (dr ? copy.pat[size_t(c)] : ed.relNote(c)) == put);
    if (!dr && (w_.trackOct() + 1) * 12 + put < ymseq::kPlayLow) {  // out of the chip's range
        refresh();
        return;
    }
    std::optional<int> mainRow;
    if (copy.pat[size_t(c)] >= 0) mainRow = dr ? copy.pat[size_t(c)] : ed.relNote(c);
    else if (!dr) {  // step crossed by a held note
        const int ns = ed.noteStart(c);
        if (ns >= 0) mainRow = ed.relNote(ns);
    }
    // Polyphonic track, step already sounding at another pitch: the press toggles a chord voice on this row.
    if (setting && !w_.mono() && ed.stepBusy(c) && mainRow != put) {
        const int ex = ed.voiceAt(c, put);
        if (ex >= 2) {  // already present: remove it
            w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) {
                e2.delVoiceAt(c, ex);
                return true;
            });
            return;
        }
        if (ed.voiceCount(c) >= ymseq::kPolyMax || !ed.freeVoice(c)) {  // six voices at most, five besides the root
            refresh();
            return;
        }
        w_.armChan();
        w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) { return e2.addVoiceAt(c, put) != 0; });
        w_.previewNote(dr ? put : (w_.trackOct() + 1) * 12 + put);
        paint_ = Paint::notes(c, put);  // the drag lengthens this voice
        refresh();
        return;
    }
    if (setting) {
        w_.armChan();
        int pre = -1;
        w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) {
            e2.writeNoteAt(c, put);
            e2.writeChord(c, put);  // voices of the chosen chord
            e2.writeFigure(c);      // hold according to the chosen figure
            pre = e2.previewNote(c);
            return true;
        });
        w_.previewNote(pre);
    } else if (!ndrum && ed.figLenAt(c) != w_.figSteps()) {  // same note, other figure: resize
        w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) {
            e2.clearFigure(c);
            e2.writeFigure(c);
            return true;
        });
        return;
    } else {
        w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane& l) {
            l.pat[size_t(c)] = -1;
            l.oct[size_t(c)] = 0;
            e2.clearFigure(c);  // the hold goes away with the note
            return true;
        });
    }
    paint_ = setting ? Paint::notes(c, put) : Paint::erasing();
    if (tool == 1) drawing_ = true;
    refresh();
}

// pointerover: the cell entered during a press serves the gesture in progress, in this order: an edge being dragged,
// a note being moved, a box, a block of steps, then the painting started by the press.
void SeqGrid::over(const Hit& h, gemui::Point pos) {
    EditorSession& s = w_.ses();
    if (sizing_) {
        if (!h.cell()) return;
        const int c2 = w_.absCol(h.col);
        Sizing& z = *sizing_;
        z.moved = true;
        // The undo step armed by press() is pushed by the first edit that changes the lane: an edit that the lane
        // refuses no longer leaves an empty step that clears Redo.
        if (z.voice) {  // chord voice: its own duration
            if (z.left) {  // drag the start
                const int ns = std::clamp(c2, 0, z.from + z.len - 1);
                if (ns == z.from) return;
                std::optional<ymseq::VoiceHit> moved;
                const int end = z.from + z.len;
                w_.editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
                    moved = ed.setVoiceStart(z.from, z.voice, z.row, ns);
                    return moved.has_value();
                });
                if (moved) {  // the voice may land in another voice slot: follow it, its end stays put
                    z.from = moved->start;
                    z.voice = moved->voice;
                    z.len = end - moved->start;
                }
                return;
            }
            const int n2 = std::max(1, c2 - z.from + 1);  // drag the end: the voice ends on the column entered
            ymseq::Lane copy = w_.lane();
            if (n2 == ymseq::LaneEditor(copy, w_.context()).voiceDur(z.from, z.voice)) return;
            w_.editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) { return ed.setVoiceLen(z.from, z.voice, n2); });
            return;
        }
        if (z.left) {  // drag the start
            if (c2 == z.from) return;
            int ns = -1;
            w_.editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
                ns = ed.setNoteStart(z.from, c2);
                return ns >= 0;
            });
            if (ns >= 0) z.from = ns;
            return;
        }
        const int n = std::max(1, c2 - z.from + 1);  // drag the end: the note ends on the column entered
        ymseq::Lane copy = w_.lane();
        if (n == ymseq::LaneEditor(copy, w_.context()).noteLenAt(z.from)) return;
        w_.setNoteLen(z.from, n);
        return;
    }
    if (group_) {  // the whole selection follows the pointer, all or nothing (item 147)
        if (h.kind != Hit::Kind::Note) return;
        GroupMove& g = *group_;
        g.moved = true;  // a drag, even refused: the selection stays
        const int ds = w_.absCol(h.col) - g.col, dr = rowOff(h.row) - g.row;
        if (ds == g.ds && dr == g.dr) return;
        bool ok = false;
        if (rect_) {
            ymseq::RectBounds r = rectBounds();
            w_.editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) { return ok = ed.rectMove(r, ds - g.ds, dr - g.dr); });
            if (ok) rect_ = RectSel{r.a, r.b, r.r1, r.r2};  // the box follows the notes
        } else {
            std::vector<ymseq::SelKey> keys = noteSel_;
            w_.editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
                return ok = ed.moveSel(keys, ds - g.ds, dr - g.dr);
            });
            if (ok) noteSel_ = std::move(keys);
        }
        if (!ok) return;
        g.ds = ds;
        g.dr = dr;
        refresh();
        return;
    }
    if (moving_) {
        if (h.kind != Hit::Kind::Note) return;
        ymseq::LaneEditor::Moving& m = *moving_;  // m.from, m.row: where the note is now; m.grab: step grabbed in it
        const int dcol = h.col;
        int c2 = w_.absCol(dcol);
        int off2 = rowOff(h.row);
        auto move = [&](int to, int row) {
            m.moved = true;  // gesture under way: no more deletion (one undo step for the move, armed by press())
            w_.editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
                ed.moveNoteTo(m, to, row);
                return true;
            });
        };
        if (w_.steps() > kCols) {  // pattern wider than the grid: the view follows at the edge
            // Deviation: only when the note is pulled toward that edge (a note dragged up or down in the
            // first or last column went one step sideways at each row).
            const int want0 = c2 - m.grab;
            const int hdir = dcol == 0 && want0 < m.from ? -1 : dcol == kCols - 1 && want0 > m.from ? 1 : 0;
            if (hdir) {
                const int was = s.viewStart;
                w_.setViewStart(was + hdir);
                if (s.viewStart != was) {
                    c2 = w_.absCol(dcol);  // the note follows the scrolling
                    move(c2 - m.grab, off2);
                    return;
                }
            }
        }
        if (!drum_) {  // top or bottom edge: the view follows by a semitone
            // Deviation: only when the note is pulled toward that edge (a note dragged along the top or
            // bottom row went a semitone further at each column).
            const int dir = h.row == 0 && off2 > m.row ? 1 : h.row == kRows - 1 && off2 < m.row ? -1 : 0;
            if (dir) {
                const int was = s.viewAbsBase;
                w_.setViewBase(was + dir);
                if (s.viewAbsBase != was) {
                    off2 += dir;
                    move(c2 - m.grab, off2);
                    return;
                }
            }
        }
        const int want = c2 - m.grab;  // the grabbed step follows the pointer
        if (want != m.from || off2 != m.row) {
            move(want, off2);
            // Listening to the moved note, at its new start (the prototype read the cell passed over); a chord
            // voice at its own pitch (the root of its step was heard).
            ymseq::Lane copy = w_.lane();
            w_.previewNote(m.voice ? ymseq::absoluteNote(copy, m.voice, m.from, w_.trackOct())
                                   : ymseq::LaneEditor(copy, w_.context()).previewNote(m.from));
        }
        return;
    }
    // Box: its moving corner follows the pointer (rightSel_: the pointer has left the first cell). Block: its end.
    if (selDrag_ == 2 && rect_ && h.kind == Hit::Kind::Note) {
        const int nb = w_.absCol(h.col), nr = rowOff(h.row);
        if (rightSel_ && (nb != rightSel_->from || nr != rightSel_->row)) rightSel_->moved = true;
        rect_->b = nb;
        rect_->r2 = nr;
        refresh();
        return;
    }
    if (selDrag_ && h.cell()) {
        selB_ = w_.absCol(h.col);
        refresh();
        return;
    }
    if (!paint_ || !h.cell()) return;
    Paint& pt = *paint_;
    const int c = w_.absCol(h.col);
    if (pt.vol && h.kind == Hit::Kind::Lane && h.lane == SeqLane::Vol) {  // drag on VOL
        const int v = volAt(h.r, pos.y);
        w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            l.vol[size_t(c)] = uint8_t(v);
            return true;
        });
        return;
    }
    if (pt.erase) {
        if (h.kind == Hit::Kind::Lane) resetFlag(h.lane, c);
        else {
            const int off = rowOff(h.row);
            ymseq::Lane copy = w_.lane();
            if (ymseq::LaneEditor(copy, w_.context()).stepBusyAt(c, off)) {
                w_.pushUndo();  // eraseNoteAt of the prototype: one undo step per erased note
                w_.editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) { return ed.eraseNoteAt(c, off); });
            }
        }
        return;
    }
    // Drawing notes (pt.from, pt.row: the last note written, the anchor). To the right of the anchor on its row, the
    // drag lengthens it (a chord voice up to the next note on that row); elsewhere it writes a note, or adds a chord
    // voice to a step that already sounds, and a written note becomes the new anchor.
    if (pt.note && h.kind == Hit::Kind::Note) {
        ymseq::Lane copy = w_.lane();
        const ymseq::LaneEditor ed(copy, w_.context());
        const int off2 = ed.snapRow(rowOff(h.row));  // Snap: a greyed row paints on its degree
        if (!w_.chipdrum() && off2 == pt.row && c > pt.from) {
            const int pv = ed.voiceAt(pt.from, off2);
            if (pv >= 2) {  // chord voice: its own duration
                int end = std::min(c, w_.length() - 1);
                for (int k = pt.from + 1; k <= end; ++k)
                    if (ed.voiceAt(k, off2) >= 2 || (copy.pat[size_t(k)] >= 0 && ed.relNote(k) == off2)) {
                        end = k - 1;
                        break;
                    }
                const int from = pt.from;
                w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane& l) {
                    l.vdur[size_t(pv - 2)][size_t(from)] = uint16_t(std::max(1, end - from + 1));
                    (void)e2;
                    return true;
                });
                return;
            }
            const int from = pt.from;
            w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) {  // drag to the right: lengthen the note
                e2.stretchNote(from, c);
                return true;
            });
            return;
        }
        std::optional<int> here;
        if (copy.pat[size_t(c)] >= 0) here = ed.relNote(c);
        else if (!drum_) {
            const int ns = ed.noteStart(c);
            if (ns >= 0) here = ed.relNote(ns);
        }
        if (!w_.mono() && ed.stepBusy(c) && here != off2) {
            if (ed.voiceAt(c, off2) || ed.voiceCount(c) >= ymseq::kPolyMax) return;  // already there, or full
            w_.armChan();
            w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) { return e2.addVoiceAt(c, off2) != 0; });
            return;
        }
        w_.armChan();
        int pre = -1;
        w_.editLane([&](ymseq::LaneEditor& e2, ymseq::Lane&) {
            e2.writeNoteAt(c, off2);
            e2.writeFigure(c);
            pre = e2.previewNote(c);
            return true;
        });
        pt.from = c;  // new note: new anchor point
        pt.row = off2;
        w_.previewNote(pre);
        return;
    }
    if (pt.flag && h.kind == Hit::Kind::Lane && h.lane == *pt.flag) {
        const ymseq::Row row = laneRow(h.lane);
        const int val = pt.val;
        w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            l.set(row, c, val);
            return true;
        });
    }
}

// pointerup: ends the gesture. A box drawn with the right button or the Select tool is kept only if it was dragged;
// a grabbed note that was not dragged gets the action of a click.
void SeqGrid::release() {
    drawing_ = false;  // end of drawing: normal cursors
    selDrag_ = 0;
    if (rightSel_) {
        // Single click: no area. Area drawn with the right button: no menu (the prototype also raised this flag after a
        // frame of the Select tool, which deprived the next right click of its menu).
        if (!rightSel_->moved) rect_.reset();
        else if (button_ == gemui::MouseButton::Right) erasedByRight_ = true;
        rightSel_.reset();
        refresh();
        return;
    }
    if (group_) {
        const bool moved = group_->moved;
        group_.reset();
        if (moved) {
            moving_.reset();
            paint_.reset();
            refresh();
            return;
        }
        rect_.reset();  // a click without a drag: end of selection, then the click acts on the note
        noteSel_.clear();
    }
    if (sizing_) {
        const Sizing z = *sizing_;
        sizing_.reset();
        paint_.reset();
        if (z.moved || w_.ses().tool != 1) return;
        // Draw tool, a click on the edge without a drag: it acts as on the rest of the note (the edge used to swallow
        // it, item 141).
        moving_ = ymseq::LaneEditor::Moving{z.from, z.row, z.len, false, false, z.voice, 0};
    }
    if (moving_ && !moving_->moved && w_.ses().tool == 0) {
        // Move tool, a click without a drag (eighth DAW test, item 92): only the cursor goes there. It used to
        // act like the Draw tool, removing the note or cutting it to the length of the figure.
        moving_.reset();
        paint_.reset();
        refresh();
        return;
    }
    if (moving_ && !moving_->moved) {  // single click: action on the note
        // A chord voice or a percussion hit is removed; a note already at the input figure's length is removed,
        // another one gets that length.
        const ymseq::LaneEditor::Moving m = *moving_;
        moving_.reset();
        paint_.reset();
        w_.pushUndo();
        const int fig = w_.figSteps();
        w_.editLane([&](ymseq::LaneEditor& ed, ymseq::Lane& l) {
            const int s = m.from;
            if (m.voice) ed.delVoiceAt(s, m.voice);  // chord voice: the click removes it
            else if (m.drum && m.len > 1) ed.cutNoteAt(s);  // held Digidrum hit: its hold with it
            else if (m.drum) l.pat[size_t(s)] = -1;
            else if (ed.figLenAt(s) == fig) {
                ed.clearFigure(s);
                l.pat[size_t(s)] = -1;
                l.oct[size_t(s)] = 0;
            } else {
                ed.clearFigure(s);
                ed.writeFigure(s);
            }
            return true;
        });
        return;
    }
    moving_.reset();
    paint_.reset();
    refresh();
}

// ------------------------------------------------------------------------------------------- menus

// contextmenu, after the release of a right click: nothing after a right-button erase or box; on a control row, the
// block menu if there is a block; without a box, on a note its menu (on an empty cell, Paste if a note was copied);
// then the box menu, else the block menu.
void SeqGrid::contextMenu(const Hit& h) {
    const bool gommed = erasedByRight_;  // "gommé": erased (French)
    erasedByRight_ = false;
    if (gommed) return;  // the right click has just erased
    if (!h.cell()) return;
    if (h.kind == Hit::Kind::Lane && selA_ < 0) return;  // control rows: menu if there is a block
    const int c = w_.absCol(h.col);
    if (!rect_ && h.kind == Hit::Kind::Note && !drum_) {
        const int off = rowOff(h.row);
        ymseq::Lane copy = w_.lane();
        const ymseq::LaneEditor ed(copy, w_.context());
        const int ns = ed.noteStart(c);
        const int s = copy.pat[size_t(c)] >= 0 && ed.relNote(c) == off ? c
                      : ns >= 0 && ed.relNote(ns) == off                 ? ns
                                                                         : -1;
        if (s >= 0) {
            noteMenu(h, c, s, off);
            return;
        }
        if (w_.ses().noteClip) {  // empty cell: paste here
            MenuItem m;
            m.label = "Paste note here";
            m.pick = [this, c, off] { w_.pasteNote(c, off); };
            w_.popupAt(h.r, {std::move(m)}, -1);
            return;
        }
    }
    if (rect_) {
        rectMenu(h);
        return;
    }
    blockMenu(h, c);
}

void SeqGrid::noteMenu(const Hit& h, int c, int s, int off) {
    ymseq::Lane copy = w_.lane();
    const ymseq::LaneEditor ed(copy, w_.context());
    const int n = ed.noteLenAt(s);
    const bool clip = w_.ses().noteClip.has_value(), join = ed.canJoin(s);
    std::vector<MenuItem> items;
    auto add = [&](std::string label, std::function<void()> fn) {
        MenuItem m;
        m.label = std::move(label);
        m.pick = std::move(fn);
        items.push_back(std::move(m));
    };
    // Labels of the prototype ("Longer  ]": repeated spaces are collapsed on display).
    add("Copy", [this, s] { w_.copyNote(s, false); });
    add("Cut", [this, s] { w_.copyNote(s, true); });
    add(clip ? "Paste here" : "Paste (n/a)", [this, c, off] { w_.pasteNote(c, off); });
    items.push_back(MenuItem::separatorItem());
    add("Longer ]", [this, s] {
        w_.pushUndo();
        ymseq::Lane cp = w_.lane();
        w_.setNoteLen(s, ymseq::LaneEditor(cp, w_.context()).noteLenAt(s) + 1);
    });
    add("Shorter [", [this, s] {
        w_.pushUndo();
        ymseq::Lane cp = w_.lane();
        w_.setNoteLen(s, ymseq::LaneEditor(cp, w_.context()).noteLenAt(s) - 1);
    });
    add("Split here \\", [this, s, c] { w_.splitNote(s, c); });
    add(join ? "Join with next J" : "Join with next (n/a)", [this, s] { w_.joinNote(s); });
    items.push_back(MenuItem::separatorItem());
    add("Length: " + std::to_string(n) + (n > 1 ? " steps" : " step"), [] {});
    add("Delete", [this, s] {
        w_.pushUndo();
        w_.editLane([s](ymseq::LaneEditor& e2, ymseq::Lane&) {
            e2.cutNoteAt(s);
            return true;
        });
    });
    w_.popupAt(h.r, std::move(items), -1);
}

void SeqGrid::rectMenu(const Hit& h) {
    ymseq::Lane copy = w_.lane();
    const int nb = int(ymseq::LaneEditor(copy, w_.context()).rectNotes(rectBounds()).size());
    const auto& clip = w_.ses().rectClip;
    std::vector<MenuItem> items;
    auto add = [&](std::string label, std::function<void()> fn) {
        MenuItem m;
        m.label = std::move(label);
        m.pick = std::move(fn);
        items.push_back(std::move(m));
    };
    add("Copy (" + std::to_string(nb) + " notes)", [this] { w_.rectCopy(false); });
    add("Cut (" + std::to_string(nb) + ")", [this] { w_.rectCopy(true); });
    add(clip ? "Paste (" + std::to_string(clip->size()) + ")" : "Paste (n/a)", [this] { w_.rectPaste(); });
    add("Clear", [this] {
        w_.pushUndo();
        w_.rectClear();
    });
    items.push_back(MenuItem::separatorItem());
    const std::pair<const char*, int> moves[] = {{"+1 semitone", 1}, {"-1 semitone", -1}, {"+1 octave", 12},
                                                 {"-1 octave", -12}};
    for (const auto& [label, n] : moves) add(label, [this, dr = n] { w_.rectMove(0, dr); });
    items.push_back(MenuItem::separatorItem());
    add("Deselect", [this] { clearRect(); });
    w_.popupAt(h.r, std::move(items), -1);
}

void SeqGrid::blockMenu(const Hit& h, int c) {
    const bool has = selA_ >= 0;
    const int n = has ? blockHi() - blockLo() + 1 : 0;
    auto upper = [](SeqLane l) {
        // Name of the row in capitals, like selRow.toUpperCase() (keys of the prototype).
        static const std::array<const char*, kSeqLanes> keys = {
            "SLT", "OCT", "TON", "INS", "CMD", "MIX", "FX",  "ARP",  "TIE", "CUT", "SLIDE", "SLC", "GAT",
            "PNR", "RTG", "CND", "VOL", "VIB", "BPM", "ENV", "DLY", "NCUT", "LFO", "MOD", "SMP", "TUN"};
        return std::string(keys[size_t(l)]);
    };
    const std::string sc = selRow_ ? " " + upper(*selRow_) : "";
    const auto& clip = w_.ses().blockClip;
    std::string pc;
    if (clip && clip->row)
        for (int k = 0; k < kSeqLanes; ++k)
            if (laneRow(SeqLane(k)) == *clip->row) pc = " " + upper(SeqLane(k));
    std::vector<MenuItem> items;
    auto add = [&](std::string label, std::function<void()> fn) {
        MenuItem m;
        m.label = std::move(label);
        m.pick = std::move(fn);
        items.push_back(std::move(m));
    };
    const std::string cnt = " (" + std::to_string(n) + ")";
    add(has ? "Copy block" + sc + cnt : "Copy block (n/a)", [this, has] {
        if (has) w_.blockCopy(false);
    });
    add(has ? "Cut block" + sc + cnt : "Cut block (n/a)", [this, has] {
        if (has) w_.blockCopy(true);
    });
    add(clip ? "Paste block" + pc + " (" + std::to_string(clip->n) + ")" : "Paste block (n/a)", [this, has, c] {
        if (w_.ses().blockClip) w_.blockPaste(has ? blockLo() : c);
    });
    add(has ? "Clear block" : "Clear block (n/a)", [this, has] {
        if (has) w_.blockClear();
    });
    items.push_back(MenuItem::separatorItem());
    add("Deselect", [this] { clearBlock(); });
    items.push_back(MenuItem::separatorItem());
    {
        MenuItem m;
        m.label = "Note length";
        m.sub = w_.figItems();
        m.subMarks = true;
        m.subCur = w_.inputFigure();
        items.push_back(std::move(m));
    }
    {
        MenuItem m;
        m.label = "Channel";
        const int maxCh = w_.m_.voices();
        for (int j = 0; j < maxCh; ++j) {
            MenuItem it;
            it.label = w_.m_.trackName(j) + " [" + w_.fxName(j) + "]";
            it.pick = [this, j] { w_.m_.focusTrack(j); };
            m.sub.push_back(std::move(it));
        }
        m.subMarks = true;
        m.subCur = w_.cur();
        items.push_back(std::move(m));
    }
    {
        MenuItem m;
        m.label = "Rename channel...";
        if (w_.mode3()) {
            m.disabled = true;
            m.why = "The three YM voices are named by the chip";
        } else m.pick = [this] { w_.m_.renameTrack(w_.cur()); };
        items.push_back(std::move(m));
    }
    w_.popupAt(h.r, std::move(items), -1);
}

void SeqGrid::laneLabelMenu(const Hit& h) {
    // Copy or paste the whole row from one track to the other (rowClip).
    const ymseq::Row row = laneRow(h.lane);
    const auto& clip = w_.ses().rowClip;
    const bool same = clip && clip->first == row;
    std::vector<MenuItem> items;
    MenuItem copy;
    copy.label = "Copy row";
    copy.pick = [this, row] { w_.ses().rowClip = std::pair{row, w_.lane()}; };
    items.push_back(std::move(copy));
    MenuItem paste;
    paste.label = same ? "Paste row" : "Paste row (n/a)";
    paste.pick = [this, row, same] {
        if (!same) return;
        w_.pushUndo();
        const ymseq::Lane src = w_.ses().rowClip->second;
        w_.editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            if (ymseq::isCmdBoxRow(row))  // every kind of the CMD row, one row on screen
                for (int s = 0; s < ymseq::kSteps; ++s) l.copyBoxCmds(s, src, s);
            else
                for (int s = 0; s < ymseq::kSteps; ++s) {
                    l.set(row, s, src.get(row, s));
                    l.set(ymseq::rowPartner(row), s, src.get(ymseq::rowPartner(row), s));  // ENV: Exx and Rxx
                }
            return true;
        });
    };
    items.push_back(std::move(paste));
    w_.popupAt(h.r, std::move(items), -1);
}

// ------------------------------------------------------------------------------------------- overview

namespace {

// fillRect of a 2D canvas at fractional coordinates: coverage of each pixel, blended with what is below.
void fillF(Canvas& cv, double x, double y, double w, double h, Color c) {
    if (w <= 0 || h <= 0) return;
    const int x0 = std::max(0, int(std::floor(x))), x1 = std::min(cv.width(), int(std::ceil(x + w)));
    const int y0 = std::max(0, int(std::floor(y))), y1 = std::min(cv.height(), int(std::ceil(y + h)));
    for (int py = y0; py < y1; ++py) {
        const double cy = std::min(y + h, py + 1.0) - std::max(y, double(py));
        for (int px = x0; px < x1; ++px) {
            const double cx = std::min(x + w, px + 1.0) - std::max(x, double(px));
            const double a = std::clamp(cx * cy, 0.0, 1.0);
            if (a <= 0) continue;
            cv.pixel(px, py, a >= 1 ? c : gemui::mix(c, cv.at(px, py), a));
        }
    }
}

}  // namespace

void SeqOverview::paint(Painter& p) {
    // refreshOverview: fixed scale of 256 steps, B0 to C9, drum band at the bottom.
    const ymseq::Project& pr = w_.proj();
    MainWindow& m = w_.m_;
    const double W = kCanvasW, H = kCanvasH;
    const int N = pr.steps();
    canvas_.clear(colors::kWhite);
    auto isDrum = [&](int t) {
        const int g = int(plugin::effectiveGenerator(m.values_, t));
        return g == int(ymengine::Generator::Digidrum) || g == int(ymengine::Generator::Chipdrum);
    };
    const int maxCh = m.voices();
    bool anyDrum = false;
    for (int t = 0; t < maxCh; ++t) anyDrum = anyDrum || isDrum(t);
    // DH: percussion band at the bottom (when a track is percussion), MH: height of the melodic part. x: step to pixel
    // (256 steps across); y: absolute note to pixel (B0 at the bottom of the melodic part, C9 at the top); yd: line of
    // drum instrument k in the band.
    const double DH = anyDrum ? kMapDrum : 0, MH = H - DH;
    auto x = [&](double s) { return s / ymseq::kSteps * W; };
    auto y = [&](double n) { return MH - (n - ymseq::kPlayLow) / double(ymseq::kMapHigh - ymseq::kPlayLow) * MH; };
    auto yd = [&](int k) { return H - DH + 3 + (k % 8) * ((DH - 6) / 7); };
    const double bw = std::max(1.0, W / ymseq::kSteps);
    if (N < ymseq::kSteps) fillF(canvas_, x(N), 0, W - x(N), H, kMapOut);  // outside the pattern
    const int bBar = std::max(1, w_.barSteps());
    for (int s = 0; s < N; s += bBar) fillF(canvas_, x(s), 0, 1, H, kMapBar);  // bar marks
    const int cur = w_.cur();
    for (int pass = 0; pass < 2; ++pass)  // the other tracks first, the current one next
        for (int i = 0; i < maxCh; ++i) {
            if ((pass == 0) == (i == cur)) continue;
            const ymseq::Lane& l = pr.cur().lane(i);
            const bool drum = isDrum(i);
            const int len = pr.trackLength(i), oct = pr.tracks[size_t(i)].octave;
            const Color ink = i == cur ? colors::kBlack : kMapOther;
            for (int s = 0; s < len; ++s) {
                if (l.pat[size_t(s)] < 0) continue;
                int d = 1;  // duration in steps
                while (s + d < len && l.pat[size_t(s + d)] < 0 && l.tie[size_t(s + d)]) ++d;
                if (drum) {  // dedicated band, one line per instrument
                    if (DH <= 0) continue;
                    fillF(canvas_, x(s), yd(l.pat[size_t(s)]), std::max(1.0, bw * d), 2, ink);
                    continue;
                }
                const int n = (ymseq::octaveOf(l.oct[size_t(s)], oct) + 1) * 12 + l.pat[size_t(s)];
                if (n < ymseq::kPlayLow || n > ymseq::kMapHigh) continue;
                fillF(canvas_, x(s), y(n) - 1, std::max(1.0, bw * d), 2, ink);
            }
        }
    if (DH > 0) fillF(canvas_, 0, MH, W, 1, colors::kBlack);  // separation of the drums
    // Portion shown in the grid: steps across, pitches down (2 px blue frame).
    double vy = 1, vh = H - 2;
    if (!w_.drum()) {
        const int base = w_.ses().viewAbsBase;
        const int top = std::min(ymseq::kMapHigh, base + 12), bot = std::max(ymseq::kPlayLow, base);
        vy = std::max(1.0, y(top) - 1);
        vh = std::max(4.0, y(bot) - y(top) + 2);
        if (vy + vh > MH) vh = std::max(4.0, MH - vy);
    }
    {
        const double rx = x(w_.ses().viewStart) + 1, rw = std::max(4.0, x(std::min(SeqGrid::kCols, N)) - 2);
        fillF(canvas_, rx - 1, vy - 1, rw + 2, 2, kMapView);
        fillF(canvas_, rx - 1, vy + vh - 1, rw + 2, 2, kMapView);
        fillF(canvas_, rx - 1, vy + 1, 2, vh - 2, kMapView);
        fillF(canvas_, rx + rw - 1, vy + 1, 2, vh - 2, kMapView);
    }
    const int ls = w_.grid_.playStep();  // playhead
    if (ls >= 0) fillF(canvas_, x(ls), 0, std::max(1.0, bw), H, colors::kGreen);
    const Rect b = bounds();
    p.frame(b, 2, colors::kBlack);
    canvas_.blit(p, {b.x + 2, b.y + 2, b.w - 4, b.h - 4});
}

void SeqOverview::mouseDown(const gemui::MouseEvent& e) { jump(e.pos); }
void SeqOverview::mouseDrag(const gemui::MouseEvent& e) { jump(e.pos); }

void SeqOverview::jump(gemui::Point pt) {
    // Click or drag: the view centres on the point (step, and pitch for a melodic track).
    const Rect r = bounds();
    const double f = double(pt.x - r.x) / std::max(1, r.w);
    EditorSession& s = w_.ses();
    const int want = int(std::lround(f * ymseq::kSteps - SeqGrid::kCols / 2.0));
    s.viewStart = std::max(0, std::min(want, std::max(0, w_.steps() - SeqGrid::kCols)));
    if (!w_.drum()) {
        bool anyD = false;
        for (int t = 0; t < w_.m_.voices(); ++t) {
            const int g = int(plugin::effectiveGenerator(w_.m_.values_, t));
            anyD = anyD || g == int(ymengine::Generator::Digidrum) || g == int(ymengine::Generator::Chipdrum);
        }
        const double mh2 = std::max(1, r.h) - (anyD ? kMapDrum : 0);
        const double fy = std::clamp((pt.y - r.y) / mh2, 0.0, 1.0);
        const double note = ymseq::kMapHigh - fy * (ymseq::kMapHigh - ymseq::kPlayLow);
        // The 13 rows centred on the note: the bottom row 6 semitones below it (bounds of the view's scroll bar).
        s.viewAbsBase = std::clamp(int(std::lround(note)) - 6, ymseq::kPlayLow, 108);
    }
    w_.refresh();
}

}  // namespace stym::pluginui
