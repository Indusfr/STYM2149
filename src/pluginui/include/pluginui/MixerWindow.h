// STYM2149 — pluginui: Mixer window (#mixWin of the prototype), level, pan, mute and solo of each track
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// One strip per track (.mstrip: label, oscilloscope, vertical fader and VU meter, pan, M, S) and the VU meters of
// the outputs (Out L, Out R; a single "Out" output in 3-channel mode, the ST is mono). In 3-channel mode, one strip
// per chip channel and one for the Digidrum (A, B, C, Digi; eight with two chips): faders inactive (the level comes
// from the 4-bit volume), no pan. Positions and sizes: the prototype's CSS boxes at 100 % (window at 110, 110, as
// wide as its strips or as the row of outputs). The 1272 × 944 px desktop never makes the window overflow (92 % of
// the width, 70 % of the height of the prototype): no scroll bars.
//
// VU meters (drawMeters): 48 dB scale, peak hold that falls by 50 % of the height per second (2.5 % per message at
// 20 Hz in the prototype; here counted by the publications of the engine status), red #B00000 from 0.999.
// Oscilloscopes (drawScopes): 26 points taken from the last 128 of the track, y = 9 - v × 15.3.
//
// DMA strip (M6, D-153): with the STE output chain only, after the track strips, in both modes (fader and pan
// active: parameters DMA Level, DMA Pan, DMA Mute, DMA Solo); its label opens the DMA window. In 3-channel mode, the
// DMA track can be stereo: Out L and Out R are then shown.
//
// Values (eighth DAW test, item 99, D-300): each fader's and pan's value under it, in the 6×6 font; a right click on
// any of the window's sliders opens a menu: Value... (typed) and Reset (the parameter's default).
//
// Send (eighth DAW test, item 98, D-299): in 16 channels, under the pan, the level that each track sends to the delay
// (Edit > Effects), after its fader, with its value ("Send 35").
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "plugincore/Parameters.h"
#include "plugincore/EngineStatus.h"
#include "pluginui/Session.h"
#include "pluginui/Widgets.h"

namespace stym::pluginui {

class MainWindow;

// canvas.mscope: 26 × 18 inside a 2 px line; a click opens the track's oscilloscope (openScope).
class MiniScope : public gemui::Widget {
public:
    static constexpr int kW = 26, kH = 18;

    MiniScope();
    // Points of the track's oscilloscope, from the oldest to the newest.
    void setPoints(std::span<const float> points);

    std::function<void()> onClick;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override { pressed_ = false; }  // no click
    gemui::Pointer pointer() const noexcept override { return gemui::Pointer::Hand; }

private:
    Canvas canvas_{kW, kH};
    bool pressed_ = false;
};

// .vu (vertical, 7 × 96) and .vuh (horizontal, 11 px high): 2 px line, black fill from the bottom (the left), red
// #B00000 when clipping, 2 px peak line.
class VuMeter : public gemui::Widget {
public:
    static constexpr gemui::Color kClip = gemui::rgb(0xB0, 0x00, 0x00);

    explicit VuMeter(bool horizontal = false) : horizontal_(horizontal) {}
    // Height (width) of the fill and of the peak hold, in % of the inside; peak hold hidden at 1 % and below.
    void setLevel(double fill, double hold, bool clip);
    double fill() const noexcept { return fill_; }
    double hold() const noexcept { return hold_; }
    bool clip() const noexcept { return clip_; }

    void paint(gemui::Painter& p) override;

private:
    bool horizontal_;
    double fill_ = 0, hold_ = 0;
    bool clip_ = false;
};

// input.pfader: a double click puts it back in the centre. Without the arrows of the other horizontal sliders
// (D-297).
class PanSlider : public gemui::Slider {
public:
    PanSlider() { setArrows(false); }

    std::function<void()> onDoubleClick;
    void mouseDoubleClick(const gemui::MouseEvent&) override {
        if (enabled() && onDoubleClick) onDoubleClick();
    }
};

// The Mixer window, a member of MainWindow (message thread). Its 16 strips always exist; layout() shows the first
// strips() of them.
class MixerWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 110, kY = 110;  // #mixWin: left 110, top 110
    static constexpr double kVuDb = 48;       // VU_DB: range of the meters in dB, from -48 dB to 0 dB

    explicit MixerWindow(MainWindow& main);

    // Opens the window in front of the others (openMixer): strips rebuilt, peak holds at zero.
    void show();
    void refresh();
    void refreshEngineStatus(const plugin::EngineStatus& t);

    // Rectangle of a control by the prototype's id (captures, tests): "mixWin", "mixFad3", "mixPan3", "vuhL",
    // "vuhR"; "mixLab3", "mixScope3", "mixVu3", "mixMute3", "mixSolo3" for the other elements of the fourth strip;
    // "mixSep", "mixMasterLab", "mixMaster" for the Master fader (D-276); with two Masters, "mixMaster2", "mixLink",
    // the labels "mixMaster1Lab" / "mixMaster2Lab" and, with stereo outputs, "mixMasterPan" (D-284); "mixMasterArea":
    // all of them. Values (D-300): "mixFadVal3", "mixPanVal3", "mixMasterVal", "mixMaster2Val", "mixMasterPanVal".
    // Send of the delay (D-299): "mixSend3", "mixSendVal3".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Text of a value (tests): the ids of rectOf ending in "Val".
    std::string valueText(std::string_view id) const;
    // VU meter of a strip, of the left (-1) or right (-2) output (tests).
    const VuMeter& meter(int strip) const noexcept;
    // A Master slider (tests): "mixMaster", "mixMaster2", "mixMasterPan".
    const gemui::Slider* slider(std::string_view id) const noexcept;

    // amplitude -> height in % (vuPct).
    static double vuPct(double amplitude) noexcept;

private:
    // One track's strip (.mstrip). The fader is the track's Level (0-100), or its 4-bit Volume (0-15) for a chip
    // channel in 3-channel mode (D-260).
    struct Strip {
        gemui::Button label;
        MiniScope scope;
        gemui::Slider fader;
        VuMeter vu;
        PanSlider pan;
        gemui::Slider send;              // D-299
        gemui::Label faderVal, panVal, sendVal;  // D-300
        gemui::Button mute{"M"}, solo{"S"};
        double hold = 0;  // peak hold of the meter, in % of its height
        bool faderArmed = false, panArmed = false, sendArmed = false;  // fArm, pArm: one undo step per drag
    };

    void build();
    void wire();
    void layout();
    int strips() const noexcept;  // strips shown (NVOICES)
    void pushUndo();              // pushMixUndo
    void pushTrackUndo(int track);  // fader of a chip channel: its Volume, a setting of the track (D-260)
    void toggle(int track, plugin::Track field);
    bool stereoOut() const noexcept;           // Out L and Out R
    bool perChip() const noexcept;             // 2x3 channels: a Master per chip (D-284)
    bool twoMasters() const noexcept;          // two Master faders: stereo outputs (left, right), or one per chip
    bool panned(int track) const noexcept;     // pan and fader: 16-channel mode, or DMA track in 3-channel mode
    void setMaster(bool right, double v);       // a Master fader, both with Link (D-284)
    // Right click on a slider (D-300): Value... and Reset, applied like a move of the slider (its undo step); `param`
    // gives the default.
    void sliderMenu(gemui::Slider& s, gemui::Point at, std::string name, std::function<int()> param,
                    std::function<void(double)> apply);
    void typeValue(gemui::Slider& s, const std::string& name, std::function<void(double)> apply);
    const gemui::Label* valueLabel(std::string_view id) const;

    MainWindow& m_;
    std::array<Strip, plugin::kTracks> strips_;
    Rule masterSep_;  // Master fader set apart from the tracks (sixth DAW test, item 78, D-276)
    gemui::Label masterLab_{"Master"};
    gemui::Slider master_;
    // Two Masters (seventh DAW test, item 83, D-284): with stereo outputs, left (master_) and right faders and the
    // balance; in 2x3 channels, chip 1 (master_) and chip 2 faders, without balance; Link.
    gemui::Label master1Lab_{"L", 11}, master2Lab_{"R", 11};
    gemui::Slider master2_;
    PanSlider masterPan_;
    gemui::Label masterVal_, master2Val_, masterPanVal_;  // D-300
    gemui::Button link_{"Link"};
    gemui::Label outLab_{"Out L"}, outRLab_{"Out R"};
    VuMeter outL_{true}, outR_{true};
    std::array<double, 2> outHold_{};  // peak holds of Out L and Out R, in %
    uint64_t serial_ = 0;  // last engine status read
    bool mode3_ = false;
    int shown_ = -1;       // strips placed by the last layout
};

}  // namespace stym::pluginui
