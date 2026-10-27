// STYM2149 — pluginui: Effects window, the echo and the delay (eighth DAW test, item 98; named Effects by the
// ninth, items 110 to 112)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 addition (the prototype has none), opened by Edit > Effects (^F) or the Effects button.
// Echo, in every mode: the notes of the focused track's sound played again (ymengine::TrackEcho, in the
// non-automatable state; each setting is one undo step of the track): On / Off, Division (the time between two
// repeats, a division of the tempo) and To (the track that plays them: the same one or another), Repeats (1 to 8)
// and Drop (steps of the 4-bit volume lost at each repeat). Off, only "Echo" and its button show, like the LFO.
// A Sync Buzzer in the 3-channel modes has none: its level is the hardware envelope that the channels share,
// which a repeat would restart at full level; the Echo is greyed out there and the engine plays no repeat.
// Delay, in 16 channels only: the audio that each track sends to it (Send row of the Mixer) comes back as echoes,
// from host parameters: On / Off and Ping-pong, Sync (a division of the tempo) or Time in ms, Feedback, Hi cut and
// Level. In the 3-channel modes, its On / Off is greyed out and nothing else of it shows; Off, only On / Off shows
// (Ping-pong hidden too, ninth DAW test, item 114).
// Below a rule, as wide as the window, like the LFO in the Modulation window.
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "pluginui/Widgets.h"
#include "ymengine/Params.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. The Echo half edits the focused track's patch in MainWindow's
// non-automatable state (Host::editExtras, one undo step of the track through MainWindow::pushCur); the Delay half sets
// global host parameters through MainWindow::set, inside host gestures.
class EchoWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 150, kY = 120;  // top-left corner before the user moves the window (logical pixels)

    explicit EchoWindow(MainWindow& main);

    // Opens the window in front of the others.
    void show();
    // Follows the focused track and the parameters (called by MainWindow::refresh); the title is kept up to date even
    // while the window is closed.
    void refresh();

    // Rectangle of a control (captures, tests): "echoWin", "echoOn", "echoDiv", "echoTo", "echoRep", "echoRepv",
    // "echoDrop", "echoDropv", "delaySep", "delayOn", "delayPing", "delaySync", "delayDiv", "delayTime", "delayTimev",
    // "delayFb", "delayFbv", "delayCut", "delayCutv", "delayLevel", "delayLevelv".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Slider and button of a setting (tests), by the same ids.
    const gemui::Slider* slider(std::string_view id) const noexcept;
    const gemui::Button* button(std::string_view id) const noexcept;

    // Positions of the logarithmic sliders (Time in ms, Hi cut in Hz) and back. Position p (0 to kLogPositions) gives
    // lo × (hi / lo)^(p / kLogPositions), rounded to a whole ms or Hz; lo and hi are the bounds of ymengine::kDelay*.
    static constexpr int kLogPositions = 200;
    static int timeOfPosition(int pos) noexcept;
    static int positionOfTime(double ms) noexcept;
    static int cutOfPosition(int pos) noexcept;
    static int positionOfCut(double hz) noexcept;

private:
    void build();
    void wire();
    void layout();
    bool studio() const noexcept;  // 16 channels: the delay applies
    bool buzzer3() const noexcept;  // Sync Buzzer in 3 channels: no echo
    const ymengine::TrackEcho& echo() const noexcept;
    // Changes the focused track's echo in the state, one undo step of the track.
    void editEcho(const std::function<void(ymengine::TrackEcho&)>& fn);
    std::string targetText(int target) const;  // name of the To track; "Same track" for -1 or a track out of range

    MainWindow& m_;
    gemui::Label labEcho_{"Echo"}, labDiv_{"Division"}, labTo_{"To"}, labRep_{"Repeats"}, labDrop_{"Drop"};
    gemui::Button on_{"Off"}, div_, to_;
    gemui::Slider rep_, drop_;
    gemui::ValueField repv_, dropv_;
    Rule sep_;
    gemui::Label labDelay_{"Delay"}, labDelDiv_{"Division"}, labTime_{"Time"}, labFb_{"Feedback"},
        labCut_{"Hi cut"}, labLevel_{"Level"};
    gemui::Button delayOn_{"Off"}, ping_{"Ping-pong"}, sync_{"Sync"}, delDiv_;
    gemui::Slider time_, fb_, cut_, level_;
    gemui::ValueField timev_, fbv_, cutv_, levelv_;
};

}  // namespace stym::pluginui
