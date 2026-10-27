// STYM2149 — pluginui: instrument sequences of the focused track (M6, B2, D-148), a part of the Modulation window
// (fifth DAW test, D-240)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 addition (the prototype has none), inside the Modulation table that it extends, between its pitch rows
// and its NSE and VOL rows (sixth DAW test, item 72, D-270): same columns (steps of 10 px spaced by 1 px), played
// with it (on/off, length, loop and rate of the table). Its settings below the table.
// Rows: PER (tone period offset), NP (noise period), MIX (channel mode 0 to 7), GEN (generator), FIX (fixed note),
// then OUT, the pitch played at each step by a C4 (YM-Tracker preview, Y43). Settings: fixed note, detune, analog
// randomness, hard sync, linked instrument, pitch matrix, vibrato table generator (written into PER). Each setting is
// one undo step of the track. Until the fifth DAW test this was the Sequences window (Edit > Sequences, Ctrl+L).
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "pluginui/Widgets.h"
#include "ymengine/Params.h"

namespace stym::pluginui {

class MainWindow;

// Rows of the panel: 20 px label column then 64 columns of 10 px spaced by 1 px (those of #orngrid). PER: bars on
// either side of the middle (41 px); NP: bar rising from the bottom (23 px); MIX, GEN and FIX: 11 px cells; OUT
// (23 px, read only): a line at the step's pitch offset, ±12 semitones over the height of the cell. Beyond the
// length of the table, the cells are faded as in #orngrid.
// Like OrnGrid, the grid draws a copy of the rows (State, given by SequencesPanel::refresh) and writes nothing itself:
// the mouse calls the callbacks below.
class SeqRowsGrid : public gemui::Widget {
public:
    static constexpr int kSteps = ymengine::kOrnamentSteps;
    static constexpr int kLabelW = 20, kCellW = 10, kGap = 1;
    static constexpr int kPerH = 41, kNpH = 23, kCellH = 11;
    static constexpr int kWidth = kLabelW + kSteps * (kCellW + kGap);  // 724
    enum class Row : uint8_t { Period, Noise, Mix, Gen, Fix, Out };  // from top to bottom
    static constexpr int kRowCount = 6;
    static constexpr std::array<int, kRowCount> kRowH = {kPerH, kNpH, kCellH, kCellH, kCellH, kNpH};  // by Row
    static constexpr int kHeight = kPerH + 2 * kNpH + 3 * kCellH + 5 * kGap;  // 125

    struct State {
        ymengine::SeqRows rows;
        int length = 8;
        bool live = true;  // table on (otherwise everything is faded)
        // Preview (OUT): period played at each step by the reference note, that of the note without a table, clock.
        // Defaults until the first refresh: C4 (MIDI 60) at a 2 MHz clock, tone period 478.
        std::array<uint16_t, kSteps> periods{};
        uint16_t notePeriod = 478;
        int refNote = 60;
        double clockHz = 2'000'000;
        double a4Hz = 440;  // Tuning A4 (D-429)
        bool latin = false;  // Do Ré Mi note names
        // Why a row moves nothing on this track (NP, MIX and GEN on a DMA track, D-463), empty when it applies: the
        // row is drawn faded, takes no click nor wheel, and its tooltip gives the reason.
        std::array<std::string, kRowCount> off{};
        bool operator==(const State&) const = default;
    };
    void setState(const State& s);
    const State& state() const noexcept { return st_; }

    // Cell of a step of a row, on the desktop.
    gemui::Rect cellRect(Row row, int step) const noexcept;

    // Writes requested by the mouse; one wheel notch is one on its own (onWheel, otherwise onSet).
    std::function<void(Row row, int step, int value)> onSet, onWheel;
    // Click on a MIX or GEN cell: menu of the values. Right click on a PER or NP cell: setting with a slider (sixth
    // DAW test, item 74, D-272; the other rows go back to 0).
    std::function<void(Row row, int step, const gemui::Rect& cell)> onMenu, onSliderMenu;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override { mouseUp({}); }  // the stroke stops where it is
    bool mouseWheel(const gemui::WheelEvent& e) override;
    gemui::Pointer pointerAt(gemui::Point p) const noexcept override;
    std::string tipAt(gemui::Point p) const override;

    // Value of a row and its bounds (OUT has none: 0). PER -127 to +127; NP 0 (the track's) to 31; MIX 0 (the track's
    // mixer) or channel mode + 1 (1 to 8); GEN 0 (the note's generator) to 3; FIX 0 or 1.
    static int value(const ymengine::SeqRows& r, Row row, int step) noexcept;
    static int minValue(Row row) noexcept;
    static int maxValue(Row row) noexcept;

private:
    int rowTop(Row row) const noexcept;  // from the top of the grid
    bool cellAt(gemui::Point p, Row* row, int* step) const noexcept;
    int valueAt(Row row, int step, double y) const noexcept;  // PER and NP: vertical position -> value

    State st_;
    std::optional<Row> paint_;  // stroke in progress (PER, NP, FIX)
    int fixPaint_ = 1;          // value a FIX stroke paints: the opposite of the cell pressed
    int lastStep_ = -1;         // step of the stroke's last write (-1: outside its row)
};

// The rows and settings, held by the Modulation window: the panel adds its widgets to it and places them below the
// table. The rows (and their notes) show while the table is on, like the table itself; the settings always show.
class SequencesPanel {
public:
    explicit SequencesPanel(MainWindow& main);
    SequencesPanel(const SequencesPanel&) = delete;
    SequencesPanel& operator=(const SequencesPanel&) = delete;

    // Adds the widgets to the window that holds them (once).
    void addTo(gemui::Widget& window);
    // Values of the focused track (the window's refresh).
    void refresh();
    // Places the rows at (x, y), shown when `show` (the table on), inside the table (D-270).
    void placeRows(double x, double y, bool show);
    // Places a rule from edge to edge of the content (x - pad to x + width + pad), then the settings, from `y` down;
    // returns the bottom of the settings.
    double layout(double x, double y, double width, double pad);
    // Content width that the settings need.
    static double settingsWidth() noexcept { return SeqRowsGrid::kWidth; }

    // Rectangle of an element (captures, tests): "seqxGrid", "seqxFix", "seqxDetune", "seqxAnalog", "seqxSync",
    // "seqxLink", "seqxMx0" to "seqxMx8" (source × 3 + target), "seqxVibMin", "seqxVibMax", "seqxVibCycles",
    // "seqxVibRamp", "seqxVibWrite"; the typed values: the slider's name followed by V ("seqxFixV").
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    const SeqRowsGrid& grid() const noexcept { return grid_; }
    // Slider of a setting (tests): "seqxFix", "seqxDetune", "seqxAnalog", "seqxVibMin", "seqxVibMax",
    // "seqxVibCycles", "seqxVibRamp".
    const gemui::Slider* slider(std::string_view id) const noexcept;
    // Button of a setting (tests): "seqxSync", "seqxLink", "seqxMx0" to "seqxMx8", "seqxVibWrite".
    const gemui::Button* button(std::string_view id) const noexcept;

    // Vibrato table (X12) written into PER over the length of the table: sine of `cycles` periods between `lo` and
    // `hi`, amplitude rising from 0 to full over the first `ramp` steps.
    static std::array<int8_t, ymengine::kOrnamentSteps> vibratoTable(int lo, int hi, int cycles, int ramp, int length);

private:
    void build();
    void wire();
    bool drum() const noexcept;
    bool dma() const noexcept;  // a DMA track: what moves no sample is greyed (Indus, 26/10/2026, D-463)
    const ymengine::TrackPatch& patch() const noexcept;  // the focused track's patch, as MainWindow last read it
    // Changes it in the state; one undo step of the track per gesture (MainWindow::pushCur).
    void editPatch(const std::function<void(ymengine::TrackPatch&)>& fn);
    void cellMenu(SeqRowsGrid::Row row, int step, const gemui::Rect& cell);  // values of a MIX or GEN cell
    void linkMenu();                                                         // instrument chained by Link
    std::string noteName(int note) const;  // MIDI note in the chosen naming (noteNames preference)

    MainWindow& m_;
    Rule sep_;
    SeqRowsGrid grid_;

    // The values right of the sliders are typed too (sixth DAW test, item 77, D-275).
    gemui::Label labFix_{"Fix", 14}, labDet_{"Detune", 14}, labAna_{"Analog", 14};
    gemui::ValueField fixv_, detv_, anav_;
    gemui::Slider fix_, det_, ana_;
    gemui::Button sync_{"Hard sync"}, link_{"Link: none"};

    gemui::Label labMx_{"Pitch matrix", 14};
    std::array<gemui::Label, 3> labSrc_{gemui::Label{"Table", 14}, gemui::Label{"Slide", 14},
                                        gemui::Label{"Vibrato", 14}};
    std::array<gemui::Button, 9> mx_;  // index source × 3 + target, the bit of TrackPatch::pitchMatrix it toggles

    // The vibrato generator's settings are the panel's own (not saved); only the PER row it writes is.
    gemui::Label labVib_{"Vibrato table", 14}, labMin_{"Min", 14}, labMax_{"Max", 14}, labCyc_{"Cycles", 14},
        labRamp_{"Ramp", 14};
    gemui::ValueField minv_, maxv_, cycv_, rampv_;
    gemui::Slider vibMin_, vibMax_, vibCyc_, vibRamp_;
    gemui::Button vibWrite_{"Write PER"};
};

}  // namespace stym::pluginui
