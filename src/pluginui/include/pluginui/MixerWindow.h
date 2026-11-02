// STYM2149 — pluginui: Mixer window (#mixWin of the prototype), level, pan, mute and solo of each track
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// One strip per track (.mstrip: label, oscilloscope, vertical fader and VU meter, pan, M, S) and the VU meters of
// the outputs (Out L, Out R; a single "Out" output in 3-channel mode, the ST is mono). In 3-channel mode, one strip
// per chip channel and one for the Digidrum (A, B, C, Digi; eight with two chips): faders inactive (the level comes
// from the 4-bit volume), no pan. Positions and sizes: the prototype's CSS boxes at 100 % (window at 110, 110, as
// wide as its strips or as the row of outputs). Placed or grown near the desktop's right or bottom edge, it moves
// back inside. The strips lie in a pane as wide as 16 strips of 32 channels at most: the 32 tracks
// scroll under a horizontal scroll bar, Indus's choice against the rule of no scroll bars, the Master and the
// outputs staying in place; the focused track's strip is scrolled into view when the window opens and when the focus
// changes.
//
// VU meters (drawMeters): 48 dB scale, peak hold that falls by 50 % of the height per second (2.5 % per message at
// 20 Hz in the prototype; here counted by the publications of the engine status), red #B00000 from 0.999.
// Oscilloscopes (drawScopes): 26 points taken from the last 128 of the track, y = 9 - v × 15.3.
//
// DMA strip: with the STE output chain only, after the track strips, in both modes (fader and pan
// active: parameters DMA Level, DMA Pan, DMA Mute, DMA Solo); its label opens the DMA window. In 3-channel mode, the
// DMA track can be stereo: Out L and Out R are then shown.
//
// A DMA track playing a stereo sample, then every DMA track (Indus, 30/10/2026 03:29-03:52):
// two oscilloscopes, two faders each with its meter, two values, two M and two S, the left side's then the right
// side's, then Mono and Link on rows of their own. Link: one Volume, M and S for both sides. The meters and
// oscilloscopes of the two sides are taken after the pan, as the outputs receive them. Mono: one fader, meter,
// oscilloscope, M and S, Link greyed; a stereo sample plays its two channels mixed. A Mono on the Master too, where
// the outputs are stereo: the outputs mixed, one Master, its Link greyed. The other generators are mono, in every
// mode: one fader, meter and oscilloscope, the pan and the send (16 and 32 channels), one M and one S, without Mono
// or Link (Indus, 30/10/2026 03:52 for the YM channels of the 3-channel modes, 31/10/2026 16:56 for the tracks of 16
// and 32 channels).
//
// Values (eighth DAW test, item 99): each fader's and pan's value under it, in the 6×6 font; a right click on
// any of the window's sliders opens a menu: Value... (typed) and Reset (the parameter's default). A click on a value
// types it in place, Enter confirms, Escape cancels (24/10/2026), as the Value... box does.
//
// Send (eighth DAW test, item 98): in 16 and 32 channels, under the pan, the level that each track sends to the
// delay (Edit > Effects), after its fader, with its value ("Send 35").
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "gemui/Scroll.h"
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

// input.pfader: a double click puts it back in the centre. Without the arrows of the other horizontal sliders.
class PanSlider : public gemui::Slider {
public:
    PanSlider() { setArrows(false); }

    std::function<void()> onDoubleClick;
    void mouseDoubleClick(const gemui::MouseEvent&) override {
        if (enabled() && onDoubleClick) onDoubleClick();
    }
};

// The Mixer window, a member of MainWindow (message thread). Its 32 strips always exist; layout() shows the first
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
    // with two sides, "mixScopeR3" and "mixVuR3" for the right side's, "mixFadR3", "mixFadRVal3",
    // "mixMuteR3", "mixSoloR3" and "mixLink3", "mixMono3";
    // "mixSep", "mixMasterLab", "mixMaster" for the Master fader; with two Masters, "mixMaster2", "mixLink",
    // the labels "mixMaster1Lab" / "mixMaster2Lab" and, with stereo outputs, "mixMasterPan" and
    // "mixMasterMono"; "mixMasterArea":
    // all of them. Values: "mixFadVal3", "mixPanVal3", "mixMasterVal", "mixMaster2Val", "mixMasterPanVal".
    // Send of the delay: "mixSend3", "mixSendVal3". "mixStrips": the pane of the strips.
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Text of a value (tests): the ids of rectOf ending in "Val".
    std::string valueText(std::string_view id) const;
    // Its field, typed in place (tests).
    const gemui::ValueField* field(std::string_view id) const { return valueField(id); }
    // VU meter of a strip, of the left (-1) or right (-2) output (tests); `right`: the right side's of a stereo
    // sample.
    const VuMeter& meter(int strip, bool right = false) const noexcept;
    // A Master slider (tests): "mixMaster", "mixMaster2", "mixMasterPan".
    const gemui::Slider* slider(std::string_view id) const noexcept;
    // The pane of the strips: scrolled by its bar, the wheel with Shift, or reveal().
    gemui::ScrollPane& stripPane() noexcept { return pane_; }
    // Scrolls the strips so that the strip of `track` shows whole.
    void reveal(int track);

    // amplitude -> height in % (vuPct).
    static double vuPct(double amplitude) noexcept;

private:
    // One track's strip (.mstrip). The fader is the track's Level (0-100), or its 4-bit Volume (0-15) for a chip
    // channel in 3-channel mode.
    // Two sides: two oscilloscopes and two meters, the left side's (scope, vu) and the right side's
    // (scopeR, vuR); the right side's fader, value, M and S, and Link; Mono above Link.
    struct Strip {
        gemui::Button label;
        MiniScope scope, scopeR;
        gemui::Slider fader, faderR;
        VuMeter vu, vuR;
        PanSlider pan;
        gemui::Slider send;
        gemui::ValueField faderVal, faderRVal, panVal, sendVal;  // typed in place
        gemui::Button mute{"M"}, solo{"S"}, muteR{"M"}, soloR{"S"}, mono{"Mono"}, link{"Link"};
        double hold = 0, holdR = 0;  // peak hold of the meters, in % of their height
        bool stereo = false;  // two sides shown
        bool sided = false;   // a DMA track: Mono and Link shown
        // fArm, pArm: one undo step per drag
        bool faderArmed = false, faderRArmed = false, panArmed = false, sendArmed = false;
    };

    void build();
    void wire();
    void layout();
    int strips() const noexcept;  // strips shown (NVOICES)
    void pushUndo();              // pushMixUndo
    void pushTrackUndo(int track);  // fader of a chip channel: its Volume, a setting of the track
    void toggle(int track, plugin::Track field);
    bool stereoOut() const noexcept;           // Out L and Out R
    bool perChip() const noexcept;             // 2x3 channels: a Master per chip
    bool twoMasters() const noexcept;          // two Master faders: stereo outputs (left, right), or one per chip
    bool panned(int track) const noexcept;     // pan and fader: 32-channel mode, or DMA track in 3-channel mode
    bool sided(int track) const noexcept;      // a DMA track: two sides unless Mono, Mono and Link
    bool stereo(int track) const noexcept;     // two sides: a DMA track without Mono
    bool masterMono() const noexcept;          // the Master's Mono, where the outputs are stereo
    void setMaster(bool right, double v);       // a Master fader, both with Link
    void setRight(int track, double v);         // the right side's fader of a stereo sample, both with Link
    // Right click on a slider: Value... and Reset, applied like a move of the slider (its undo step); `param`
    // gives the default.
    void sliderMenu(gemui::Slider& s, gemui::Point at, std::string name, std::function<int()> param,
                    std::function<void(double)> apply);
    void typeValue(gemui::Slider& s, const std::string& name, std::function<void(double)> apply);
    // A value under its slider typed in place: through `apply`, like Value....
    static void typeUnder(gemui::ValueField& f, gemui::Slider& s, std::function<void(double)> apply);
    const gemui::ValueField* valueField(std::string_view id) const;

    MainWindow& m_;
    gemui::ScrollPane pane_;  // the strips
    std::array<Strip, plugin::kTracks> strips_;
    // Left edge and width of each strip shown in the pane's content, set by layout() (reveal).
    std::array<double, plugin::kTracks> stripX_{}, stripW_{};
    int revealed_ = -1;  // focused track last scrolled into view
    Rule masterSep_;  // Master fader set apart from the tracks (sixth DAW test, item 78)
    gemui::Label masterLab_{"Master"};
    gemui::Slider master_;
    // Two Masters (seventh DAW test, item 83): with stereo outputs, left (master_) and right faders and the
    // balance; in 2x3 channels, chip 1 (master_) and chip 2 faders, without balance; Link.
    gemui::Label master1Lab_{"L", 11}, master2Lab_{"R", 11};
    gemui::Slider master2_;
    PanSlider masterPan_;
    gemui::ValueField masterVal_, master2Val_, masterPanVal_;
    gemui::Button link_{"Link"};
    gemui::Button masterMono_{"Mono"};
    gemui::Label outLab_{"Out L"}, outRLab_{"Out R"};
    VuMeter outL_{true}, outR_{true};
    std::array<double, 2> outHold_{};  // peak holds of Out L and Out R, in %
    uint64_t serial_ = 0;  // last engine status read
    bool mode3_ = false;
    int shown_ = -1;       // strips placed by the last layout
};

}  // namespace stym::pluginui
