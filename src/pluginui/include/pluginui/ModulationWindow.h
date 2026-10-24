// STYM2149 — pluginui: Modulation window (#ornWin of the prototype), per-VBL-frame table of the focused track
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// On / Off (not applicable on a percussion track), length (1 to 64 steps), Loop / Hold, rate (1 to 4 frames per
// step), Clear, and the #orngrid grid: pitch in semitones (+12 to -12), NSE row (noise on the step) and VOL row
// (4-bit level), to draw with the mouse or the keyboard. Positions and sizes: the prototype's CSS boxes at 100 %
// (window at 140, 140, as wide as its content). Between the pitch and NSE, the sequence rows (sixth DAW test, item
// 72, D-270), their notes with those of the table above it (item 73, D-271).
//
// The table lives in the non-automatable state (ornament* fields of ymengine::TrackPatch). Each setting is one undo
// step of the track (pushCur of the prototype); a mouse stroke makes only one.
//
// The sequences (PER, NP, MIX, GEN, FIX and OUT rows inside the table, pitch settings and vibrato table below it:
// SequencesPanel), formerly a window of their own (fifth DAW test, D-240). Clear empties the sequence rows too.
// Below them, the LFO of the track (sixth DAW test, item 59, D-257: LfoPanel).
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "pluginui/LfoPanel.h"
#include "pluginui/SequencesPanel.h"
#include "pluginui/Widgets.h"
#include "ymengine/Params.h"

namespace stym::pluginui {

class MainWindow;

// #orngrid: 20 px label column and 64 columns of 10 px, spaced by 1 px. Rows: 25 of pitch (9 px, +12 at the top),
// 3 px black separator (2 px margins), the place of the sequence rows (SeqRowsGrid, a widget of its own laid over
// this one: D-270), a second separator, NSE (11 px), VOL (34 px), spaced by 1 px.
// Cell (.oc): 1 px #999 border, white, #DDD on row 0, black on the value; keyboard cursor: 4 px green frame inside
// the border; beyond the length (.dead): the whole cell at 30 % over the white background.
// VOL: black bar rising from the bottom (level / 15 of the height), 8 px digit at the top, or in white at the bottom
// when the bar exceeds 70 %.
// The grid draws a copy of the table (State, given by ModulationWindow::refreshGrid) and writes nothing itself: a
// press or a stroke calls the callbacks below, which edit the state and give the grid its new copy.
class OrnGrid : public gemui::Widget {
public:
    static constexpr int kSteps = ymengine::kOrnamentSteps, kSpan = 12, kRows = 2 * kSpan + 1;
    static constexpr int kLabelW = 20, kCellW = 10, kCellH = 9, kGap = 1;
    static constexpr int kSepH = 3 + 2 * 2, kNseH = 11, kVolH = 34;
    static constexpr int kWidth = kLabelW + kSteps * (kCellW + kGap);                                   // 724
    static constexpr int kSepY = kRows * (kCellH + kGap), kSeqY = kSepY + kSepH + kGap;                 // 250, 258
    static constexpr int kSep2Y = kSeqY + SeqRowsGrid::kHeight + kGap, kNseY = kSep2Y + kSepH + kGap;  // 384, 392
    static constexpr int kVolY = kNseY + kNseH + kGap, kHeight = kVolY + kVolH;                         // 404, 438
    static constexpr gemui::Color kBorder = gemui::rgb(0x99, 0x99, 0x99);
    static constexpr gemui::Color kZero = gemui::rgb(0xDD, 0xDD, 0xDD);

    struct State {
        std::array<int8_t, kSteps> pitch{};    // semitones, -12 to +12
        std::array<uint8_t, kSteps> noise{};   // 0 or 1
        std::array<uint8_t, kSteps> volume{};  // 0 to 15
        int length = 8;                        // steps played; the others are drawn faded
        int curStep = 0, curRow = kSpan;  // keyboard cursor (row 0 at the top = +12)
        bool operator==(const State&) const = default;
    };
    void setState(const State& s);
    const State& state() const noexcept { return st_; }

    // Cells on the desktop: pitch (step, row), NSE and VOL of a step.
    gemui::Rect cellRect(int step, int row) const noexcept;
    gemui::Rect noiseRect(int step) const noexcept;
    gemui::Rect volumeRect(int step) const noexcept;

    // Writes requested by the mouse (the cursor follows the press: onCursor).
    std::function<void(int step, int semitones)> onPitch;
    std::function<void(int step, bool on)> onNoise;
    std::function<void(int step, int level)> onVolume;
    std::function<void(int step, int row)> onCursor;  // row < 0: the row does not change
    // Right click on a VOL cell: setting with a slider (openVolMenu).
    std::function<void(int step, const gemui::Rect& cell)> onVolumeMenu;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override { mouseUp({}); }  // the stroke stops where it is
    gemui::Pointer pointerAt(gemui::Point p) const noexcept override;
    std::string tipAt(gemui::Point p) const override;

private:
    enum class Part : uint8_t { None, Pitch, Noise, Volume };
    // Part and step under the pointer; *row: pitch row. The gaps between cells are not part of them.
    Part partAt(gemui::Point p, int* step, int* row) const noexcept;
    int levelAt(int step, double y) const noexcept;  // vertical position in a VOL cell -> 0 to 15

    State st_;
    Part paint_ = Part::None;  // stroke in progress (ornPaint)
    bool noisePaint_ = true;   // value drawn on the NSE row (ornNsePaint)
    // Cell under the pointer at the last press or drag (-1: none): a stroke writes a pitch or NSE cell on entering it.
    int lastStep_ = -1, lastRow_ = -1;
};

// Owned by MainWindow, used on the message thread. The table lives in the focused track's patch, in MainWindow's
// non-automatable state; the window holds the grid, the sequences (SequencesPanel) and the LFO (LfoPanel).
class ModulationWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 140, kY = 140;  // #ornWin: left 140, top 140

    explicit ModulationWindow(MainWindow& main);

    // Opens the window in front of the others (openOrn).
    void show();
    // Follows the focused track (called by MainWindow::refresh); the title is kept up to date even while closed.
    void refresh();

    // Table from the keyboard, with the window in front and the modulation on (the prototype's keydown handler):
    // arrows (without Shift) for the cursor, Enter writes the pitch (the length grows up to the step), Delete and
    // Backspace reset the step to zero, N toggles the noise. true = key taken.
    bool tableKey(const gemui::KeyEvent& e);

    // Rectangle of a control by the prototype's id (captures, tests): "ornWin", "ornOn", "ornLen", "ornLenv",
    // "ornLoop", "ornRate", "ornClear", "orngrid"; those of the sequences ("seqx..." ids, SequencesPanel::rectOf) and
    // of the LFO ("lfo..." ids, LfoPanel::rectOf).
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    const OrnGrid& grid() const noexcept { return grid_; }
    const SequencesPanel& sequences() const noexcept { return seqx_; }
    const LfoPanel& lfo() const noexcept { return lfo_; }

private:
    void build();
    void wire();
    void layout();
    bool drum() const noexcept;
    bool live() const noexcept;  // On state shown (Off on a percussion track)
    const ymengine::TrackPatch& patch() const noexcept;
    // Changes the focused track's table in the state, one undo step of the track.
    void editPatch(const std::function<void(ymengine::TrackPatch&)>& fn);
    void setLength(int n);  // clamped to 1-64; no change, no undo step
    void refreshGrid();     // the grid's copy from the patch and the keyboard cursor (EditorSession::ornCur, ornRow)

    MainWindow& m_;
    gemui::Button on_{"Off"};
    gemui::Label labLen_{"Len"};
    gemui::Slider len_;
    gemui::ValueField lenv_;
    gemui::Button loop_{"Loop"}, rate_{"1/frame"}, clear_{"Clear"};
    // Notes of the table and of the sequence rows (D-271).
    gemui::Label hint_{
        "Per-VBL-frame tables (the tracker \"ornament\"): pitch in semitones, NSE row toggles noise, VOL row sets "
        "level. Click / drag to draw.",
        12},
        hint2_{"Sequence rows, played with the table tick by tick. PER: tone period offset (+ lower). NP: noise "
               "period. MIX: channel mode 0-7.",
               12},
        hint3_{"GEN: generator. FIX: fixed note. OUT: pitch of C4 (+-12). Right click: a slider on VOL, PER and NP; "
               "clears MIX, GEN, FIX.",
               12};
    Rule sep_;
    OrnGrid grid_;
    SequencesPanel seqx_{m_};
    LfoPanel lfo_{m_};
};

}  // namespace stym::pluginui
