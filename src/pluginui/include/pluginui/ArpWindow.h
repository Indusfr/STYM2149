// STYM2149 — pluginui: Arpeggiator window (#arpWin of the prototype), arpeggio of the focused track
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// On / Off (not applicable on a percussion track), mode (Up, Down, UpDn, Order, Random, Table), rate in Hz with Track
// and Land, or a menu of divisions of the MIDI clock or of the tempo with "MIDI ext" or Sync (D-290), octaves (except
// in Table mode), and in Table
// mode: number of steps, menu of the 39 chords that fills the table, grid of 16 steps by 25 semitones (#arpgrid,
// D-322).
// Positions and sizes: the prototype's CSS boxes at 100 % (window at 80, 100, as wide as its content).
//
// On, rate and division are host parameters (ArpOn, the track's ArpRate, ArpDivision); mode, octaves, Track, Land
// and table live in the non-automatable state (ymengine::TrackArp). Each setting is one undo step of the track
// (D-129).
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "plugincore/Parameters.h"
#include "plugincore/EngineStatus.h"
#include "pluginui/Widgets.h"
#include "ymengine/Params.h"

namespace stym::pluginui {

class MainWindow;

// .seqgrid.arpgrid: 2 px line, 26 px label column ("+24" at the top to "+0" at the bottom, white on black, 10 px),
// 16 columns of 20 px and 25 rows of 10 px separated by a 1 px black rule. Cell: white, #E8E8E8 every 4 steps
// (.qtr); table note (.onc) black with a white edge at the top, bottom and left; played step (.play) green, #005500
// on a note; beyond the length (.dim) at 35 % over black; keyboard cursor (.cur): 4 px green frame inside the
// border.
class ArpGrid : public gemui::Widget {
public:
    // Offsets +0 to +24 (the reach of the chords, kept by the state); the prototype showed +0 to +15 (D-322).
    static constexpr int kSteps = ymengine::kArpSteps, kRows = 25;
    static constexpr int kLabelW = 26, kCellW = 20, kCellH = 10, kBorder = 2;
    static constexpr int kWidth = kBorder * 2 + kLabelW + kSteps * (kCellW + 1);    // 366
    static constexpr int kHeight = kBorder * 2 + kRows * kCellH + (kRows - 1);     // 278
    static constexpr gemui::Color kQtr = gemui::rgb(0xE8, 0xE8, 0xE8);
    static constexpr gemui::Color kPlayNote = gemui::rgb(0x00, 0x55, 0x00);

    // Table, length, played step (-1: none) and cursor (step, row 0 at the top = +24).
    void setState(const std::array<int8_t, kSteps>& table, int length, int play, int cursorStep, int cursorRow);
    // Rectangle of a cell (step, row) on the desktop.
    gemui::Rect cellRect(int step, int row) const noexcept;

    // Press on a cell of a step within the length: offset (24 - row) to write at that step.
    std::function<void(int step, int offset)> onWrite;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    gemui::Pointer pointerAt(gemui::Point p) const noexcept override;

private:
    int stepAt(gemui::Point p, int* row) const noexcept;

    std::array<int8_t, kSteps> table_{};  // offset of each step in semitones (rows show 0 to 24)
    int length_ = 0, play_ = -1, curStep_ = 0, curRow_ = kRows - 1;
};

// The Arpeggiator window, a member of MainWindow (message thread); it always shows the focused track.
class ArpWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 80, kY = 100;  // #arpWin: left 80, top 100

    explicit ArpWindow(MainWindow& main);

    // Opens the window in front of the others (openArpWin).
    void show();
    void refresh();
    void refreshEngineStatus(const plugin::EngineStatus& t);

    // Table from the keyboard, with the window in front and the arpeggio on (the prototype's keydown handler): arrows
    // (without Shift) for the cursor, Enter to write the step (the length grows up to it). true = key taken.
    bool tableKey(const gemui::KeyEvent& e);

    // Rectangle of a control by the prototype's id (captures, tests): "arpWin", "arpOn", "arpModeSel", "arpRate",
    // "arpRatev", "arpGlideRate", "arpLand", "arpDiv", "arpOct1" to "arpOct3", "arpLen", "arpLenv", "chordSel",
    // "arpgrid".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    const ArpGrid& grid() const noexcept { return grid_; }

private:
    void build();
    void wire();
    void layout();
    bool drum() const noexcept;
    bool on() const noexcept;  // On state shown (Off on a percussion track)
    const ymengine::TrackArp& arp() const noexcept;
    // Changes the focused track's arpeggio in the state, one undo step of the track.
    void editArp(const std::function<void(ymengine::TrackArp&)>& fn);
    void setLength(int n);
    void refreshGrid();

    MainWindow& m_;
    gemui::Button on_{"Off"};
    gemui::Label hint_{"50 Hz = VBL rate", 12};
    gemui::Label labMode_{"Mode"}, labRate_{"Rate"}, labDiv_{"Division"}, labOct_{"Octaves"}, labSteps_{"Steps"};
    gemui::Button mode_;
    gemui::Button sync_{"Sync"};  // Arp Sync: Rate in Hz, or the tempo with Division (D-256)
    gemui::Slider rate_;
    gemui::ValueField ratev_;
    gemui::Label hz_{"Hz"};
    gemui::Button track_{"Track"}, land_{"Land"};
    gemui::Button div_;  // division: a menu button (D-290), without Tick with Sync (D-287)
    std::array<gemui::Button, 3> octs_;
    gemui::Slider len_;
    gemui::ValueField lenv_;
    gemui::Button chord_{"Chord..."};
    ArpGrid grid_;
    gemui::Label gridHint_{"Semitone offsets, applied to the last played note.", 12};
    int play_ = -1;  // played step (engine status)
};

}  // namespace stym::pluginui
