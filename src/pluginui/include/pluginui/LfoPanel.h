// STYM2149 — pluginui: LFO of the focused track (sixth DAW test, item 59, D-257), a part of the Modulation window
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 addition (the prototype has none), below the settings of the sequences, under a rule: On / Off, shape
// and target (menus), Sync (rate in Hz, or one cycle per division of the tempo), rate and depth (sliders, the values
// typed too). The rate slider is logarithmic in Hz (0.1 to 20); with Sync, a menu button takes its place and picks a
// division (4/1 to 1/64, ninth DAW test, item 119, D-320). The depth is in the unit of the target, and keeps its share
// of the range when the target changes. Each setting is one undo step of the track; nothing applies on a percussion
// track; a target that moves nothing on the track (Pan in 3 channels, Volume of a Sync Buzzer...) is greyed in the
// menu, and if chosen before, greys the other settings (item 120, D-321). Off, only "LFO" and its button show (D-298).
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

// Not a window: a group of widgets that the Modulation window holds, adds to itself (addTo) and places (layout), on
// the message thread. The LFO lives in the focused track's patch (TrackPatch::lfo) in MainWindow's non-automatable
// state; every edit goes through MainWindow::editPatch.
class LfoPanel {
public:
    explicit LfoPanel(MainWindow& main);
    LfoPanel(const LfoPanel&) = delete;
    LfoPanel& operator=(const LfoPanel&) = delete;

    // Adds the widgets to the window that holds them (once).
    void addTo(gemui::Widget& window);
    // Values of the focused track (the window's refresh).
    void refresh();
    // Places a rule from edge to edge of the content (x - pad to x + width + pad), then the LFO, from `y` down;
    // returns its bottom.
    double layout(double x, double y, double width, double pad);

    // Rectangle of an element (captures, tests): "lfoOn", "lfoShape", "lfoTo", "lfoSync", "lfoRetrig", "lfoRate",
    // "lfoDepth", the division "lfoDiv"; the typed values "lfoRateV", "lfoDepthV".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Slider of a setting (tests): "lfoRate", "lfoDepth".
    const gemui::Slider* slider(std::string_view id) const noexcept;
    // Button of a setting (tests): "lfoOn", "lfoSync", "lfoRetrig", "lfoTo", "lfoDiv".
    const gemui::Button* button(std::string_view id) const noexcept;

    // Slider position of a rate in Hz and back (logarithmic, 0 to kRatePositions), division labels, depth shown.
    // Rates: two decimals below 1 Hz, one above. Divisions: index of ymengine::kLfoDivisionTicks, "4/1" to "1/64".
    // Depth: a bare number for volume and noise period steps, "c" for cents, "%" for pan.
    static constexpr int kRatePositions = 200;
    static double rateOfPosition(int pos) noexcept;
    static int positionOfRate(double hz) noexcept;
    static std::string rateText(double hz);
    static std::string divisionText(int division);
    static std::string depthText(ymengine::LfoTarget target, double depth);
    static std::string depthUnit(ymengine::LfoTarget target);  // "", "c" or "%" (out of the reverse video, D-464)
    // Typed division ("1/8T", any case) or -1.
    static int divisionOf(std::string_view text);
    // Why a target moves nothing on the focused track (its generator, its source, the mode), empty if it applies
    // (D-321).
    std::string whyOff(ymengine::LfoTarget target) const;

    // Drawing of a shape, 36 × 16 like the envelope shapes of Shape R13: two cycles (seventh DAW test, item 90,
    // D-291); a pointed triangle, a round sine (ninth DAW test, item 118, D-319).
    static const gemui::Bitmap& shapeIcon(ymengine::LfoShape shape) noexcept;

private:
    void build();
    void wire();
    const ymengine::TrackLfo& lfo() const noexcept;  // the focused track's LFO, as MainWindow last read it
    // Changes it (MainWindow::editPatch: nothing on a percussion track, one undo step of the track).
    void edit(const std::function<void(ymengine::TrackLfo&)>& fn);
    void shapeMenu();
    void targetMenu();
    void divisionMenu();

    MainWindow& m_;
    Rule sep_;
    gemui::Label labLfo_{"LFO", 14}, labShape_{"Shape", 14}, labTo_{"To", 14}, labRate_{"Rate", 14},
        labDepth_{"Depth", 14};
    gemui::Button on_{"Off"}, sync_{"Sync"}, retrig_{"Retrig"}, shape_, to_{"Volume"};  // shape: a drawing (D-291)
    gemui::Button div_{"1/4"};  // with Sync, in place of the rate slider (D-320)
    gemui::Slider rate_, depth_;
    gemui::ValueField ratev_, depthv_;
};

}  // namespace stym::pluginui
