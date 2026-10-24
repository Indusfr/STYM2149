// STYM2149 — pluginui: Recorder window (M6, B11, D-157), register recorder: chip, Rec, kept take
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 window (the prototype has none; Q7 of the M6 plan). Rec arms a take on the chosen chip (2: dual chip
// only): it starts with the host's playback (in Standalone, the sequencer's) and stops with it, one frame of R0 to
// R13 per replay tick; a click while it is armed or rolling stops it. The last take is kept with the project; Clear
// deletes it after confirmation; the Export row writes it as YM6, SNDH or VBL dump (M7, B3, D-172). In 16-channel
// mode, no registers: the window says so.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "pluginui/Host.h"
#include "pluginui/Widgets.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. The recorder itself runs on the audio thread (plugincore Driver);
// the window only asks the host for its state (Host::recorder), arms, stops and clears it.
class RecorderWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 300, kY = 220;  // top-left corner before the user moves the window (logical pixels)

    explicit RecorderWindow(MainWindow& main);

    // Opens the window in front of the others.
    void show();
    // Shows the state last read (last_) and the mode (called by MainWindow::refresh); nothing while closed.
    void refresh();
    // Message thread, at each tick: recorder state read again, window rebuilt if it has changed.
    void poll();
    int chip() const noexcept { return chip_; }  // chip chosen for the next take, 0 or 1

    // Rectangle of an element (captures, tests): "recWin", "recChip0", "recChip1", "recRec", "recClear", "recYm6",
    // "recSndh", "recVbl".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    std::string status() const { return status_.text(); }
    std::string takeText() const { return takev_.text(); }
    // Duration of a take: minutes, seconds and hundredths ("1:02.34").
    static std::string duration(uint32_t frames, uint32_t hz);

private:
    void build();
    void wire();
    void layout();
    void confirmClear();

    MainWindow& m_;
    int chip_ = 0;
    Host::Recorder last_{};  // recorder state last read: poll compares the new one with it
    gemui::Label status_{"", 14};
    gemui::Label hint_{"One frame of R0-R13 per replay tick while the host plays (standalone: the sequencer).", 12},
        hint2_{"The last take is saved with the project.", 12};
    Rule sep_;
    gemui::Label labChip_{"Chip", 14}, labTake_{"Take", 14}, takev_{"", 14}, labExport_{"Export", 14};
    std::array<gemui::Button, 2> chips_;
    gemui::Button rec_{"Rec"}, clear_{"Clear"};
    std::array<gemui::Button, 3> exports_;  // YM6, SNDH, VBL
};

}  // namespace stym::pluginui
