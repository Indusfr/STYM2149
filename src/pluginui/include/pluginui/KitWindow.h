// STYM2149 — pluginui: Drum Kit window (#kitWin of the prototype), the eight instruments of the Chipdrum generator
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Choice of the instrument (Kick to Tom 2), its settings (one slider and one value to type per setting, ranges from
// KITSPEC), Audition (preview of the instrument), Reset (factory values of the instrument), Kits (one of the 13
// factory banks) and the kit name (identical bank, otherwise Custom). The kit is shared by every track; it concerns
// only the Chipdrum: on another track, a note says so and the content is inert (.inert, 40 %). Positions and sizes:
// the prototype's CSS boxes at 100 % (window at 190, 150, as wide as its content).
//
// The kit lives in the non-automatable state (NonParamState::kit) and is recomputed live, even while playing. A
// slider drag, a typed value, Reset and Kits each make one undo step of the kit (pushKitUndo).
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "pluginui/Widgets.h"
#include "ymengine/DrumKit.h"

namespace stym::pluginui {

class MainWindow;

// Kit name (kitLabel): the first identical factory bank, otherwise "Custom".
std::string kitLabel(const ymengine::KitParams& kit);

// Owned by MainWindow, used on the message thread. The instrument shown is EditorSession::kitInst, kept while the
// plugin instance lives.
class KitWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 190, kY = 150;  // #kitWin: left 190, top 150
    static constexpr int kMaxRows = 5;        // kick and toms: four settings and the level

    explicit KitWindow(MainWindow& main);

    // Opens the window in front of the others (openKit).
    void show();
    // Reads the kit and the focused track's generator again (called by MainWindow::refresh); nothing while closed.
    void refresh();

    // Settings of the shown instrument: number (4 or 5), sliders and values (tests).
    int rows() const noexcept;
    const gemui::Slider& slider(int row) const { return rows_[size_t(row)].slider; }
    const gemui::ValueField& valueField(int row) const { return rows_[size_t(row)].value; }

    // Rectangle of an element by the prototype's id (captures, tests): "kitWin", "kitOff", "kitBody", "kitPlay",
    // "kitReset", "kitBank", "kitName", and "kitInst0" to "kitInst7" for the instrument buttons.
    std::optional<gemui::Rect> rectOf(std::string_view id) const;

private:
    struct Row {
        gemui::Label label;
        gemui::Slider slider;
        gemui::ValueField value;
        bool armed = false;  // drag started: the first movement makes the undo step
    };

    void build();
    void wire();
    void layout();
    bool chipdrum() const noexcept;  // the focused track plays the Chipdrum generator
    int inst() const noexcept;       // shown instrument, 0 (Kick) to 7 (Tom 2)
    // Changes the kit in the state; undo: one undo step before (pushKitUndo).
    void editKit(bool undo, const std::function<void(ymengine::KitParams&)>& fn);
    // Setting `row` of the shown instrument set to v, clamped to its range; no change, no undo step.
    void setField(int row, int v, bool undo);

    MainWindow& m_;
    // Shown above the body on a track that is not a Chipdrum.
    Paragraph off_{"This track is not a Chipdrum. Switch its generator to Chipdrum (Ctrl+C) to edit the kit."};
    Rule offRule_;
    Layer body_;  // everything below the note: at 40 % and inert on a track that is not a Chipdrum
    std::array<gemui::Button, ymengine::kDrumSlots> inst_;
    std::array<Row, kMaxRows> rows_;
    Rule sep_;
    gemui::Button play_{"Audition"}, reset_{"Reset"}, bank_{"Kits"};
    gemui::Label hint_{"Rebuilds live while playing.", 12};
    gemui::Label name_{"", 12};  // "Kit: " and the kit's name (kitLabel)
};

}  // namespace stym::pluginui
