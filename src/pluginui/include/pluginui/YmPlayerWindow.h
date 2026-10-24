// STYM2149 — pluginui: YM PLAYER window (#ymWin of the prototype, M7, A2, D-169)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Opened by File > Import > YM dump: name of the file and of the tune, Play (Stop during playback), Rewind, position
// "n / total (s)" read again every 8 frames like the prototype, then the note. STYM2149 adds the choice of the
// subsong of an SNDH (row absent for a YM dump). Closing stops playback and forgets the tune.
#pragma once
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "pluginui/Host.h"
#include "pluginui/Widgets.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. The tune plays on the audio thread (plugincore Driver); the window
// only reads its state from the host (Host::ymPlayer) and sends Play, Stop, Rewind, subsong and close.
class YmPlayerWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 200, kY = 120, kW = 400;  // #ymWin: left 200, top 120, width 400

    explicit YmPlayerWindow(MainWindow& main);

    // After a load: window opened in the foreground, position at 1.
    void show();
    // Message thread, at each tick: player state read again, window rebuilt if it has changed.
    void poll();

    // Text of the position (#ymPos): "(n + 1) / frames (duration s)". `position` and `frames` in frames of the tune,
    // `rate` in frames per second; frames 0 (an SNDH of unknown length): "?".
    static std::string positionText(uint32_t position, uint32_t frames, int rate);

    // Rectangle of an element (captures, tests): "ymWin", "ymPlay", "ymRew", "ymPos", "ymName", "ymSubPrev",
    // "ymSubNext".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    std::string nameText() const { return name_.text(); }
    std::string posText() const { return pos_.text(); }
    std::string subsongText() const { return subv_.text(); }
    bool playing() const noexcept { return last_.playing; }

private:
    void build();
    void wire();
    void refresh();
    void layout();

    MainWindow& m_;
    // State last read (position rounded down to 8 frames, registers left out): poll compares the new one with it. The
    // buttons update it at once, before the host's next state.
    Host::YmPlayer last_{};
    Paragraph name_, hint_;
    gemui::Label pos_{"-"};  // .readout: body font (16 px)
    gemui::Button play_{"Play"}, rew_{"Rewind"};
    gemui::Label labSub_{"Subsong"}, subv_{""};
    gemui::Button subPrev_{"-"}, subNext_{"+"};
};

}  // namespace stym::pluginui
