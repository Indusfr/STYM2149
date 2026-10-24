// STYM2149 — pluginui: Slide Curve window (#curveWin of the prototype), "Custom" curve of the glissandos
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Free curve of 32 points (slideTable, global non-automatable state) used by the "Custom" entry of the sequencer's
// CRV row: drawing with the mouse (#curvegrid), Linear (straight line), Smooth (moving average), Invert (upside down),
// warning when the curve does not reach the target note. Positions and sizes: the prototype's CSS boxes at 100 %
// (window at 180, 160, as wide as the row of buttons). Neither undo nor Escape, as in the prototype (D-130).
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string_view>

#include "gemui/Controls.h"
#include "pluginui/Widgets.h"
#include "ymengine/Params.h"

namespace stym::pluginui {

class MainWindow;

// #curvegrid: 2 px black line, white background, 32 columns of 14 px by 120 px, right rule #D0D0D0 (#909090 every
// 8 columns), 13 px black fill from the bottom up to the point's height.
class CurveGrid : public gemui::Widget {
public:
    static constexpr int kPoints = ymengine::kSlideTablePoints, kColW = 14, kColH = 120, kBorder = 2;
    static constexpr int kWidth = 2 * kBorder + kPoints * kColW;  // 452
    static constexpr int kHeight = 2 * kBorder + kColH;           // 124
    static constexpr gemui::Color kLine = gemui::rgb(0xD0, 0xD0, 0xD0), kLine8 = gemui::rgb(0x90, 0x90, 0x90);

    void setTable(const std::array<float, kPoints>& t);
    const std::array<float, kPoints>& table() const noexcept { return table_; }
    // Column (point) on the desktop, rule included.
    gemui::Rect columnRect(int i) const noexcept;

    // Press (left button) then drag on a column: point i at the pointer's height (0 at the bottom, 1 at the top).
    std::function<void(int i, float level)> onDraw;

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override { mouseUp({}); }  // the stroke stops where it is
    gemui::Pointer pointerAt(gemui::Point p) const noexcept override;

private:
    int columnAt(gemui::Point p) const noexcept;  // -1 outside the columns (line)
    void draw(gemui::Point p);

    std::array<float, kPoints> table_{};  // copy of the curve shown: point i from 0 (start note) to 1 (target note)
    bool painting_ = false;               // left button held after a press on a column
};

// The Slide Curve window, a member of MainWindow (message thread), opened by the Custom entry of the CRV menu.
class CurveWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 180, kY = 160;  // #curveWin: left 180, top 160

    explicit CurveWindow(MainWindow& main);

    // Opens the window in front of the others (openCurveWin).
    void show();
    void refresh();

    // Rectangle of a control by the prototype's id (captures, tests): "curveWin", "curveReset", "curveSmooth",
    // "curveInvert", "curvegrid", "curveWarn".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    const CurveGrid& grid() const noexcept { return grid_; }
    const gemui::Label& warning() const noexcept { return warn_; }

private:
    void build();
    void wire();
    void layout();
    const std::array<float, CurveGrid::kPoints>& table() const noexcept;
    // Replaces the curve in the state (sendPatch), without an undo step.
    void edit(const std::function<void(std::array<float, CurveGrid::kPoints>&)>& fn);

    MainWindow& m_;
    gemui::Button reset_{"Linear"}, smooth_{"Smooth"}, invert_{"Invert"};
    gemui::Label hint_{"drag to draw — left = start of the slide, right = arrival", 12};
    Rule sep_;
    CurveGrid grid_;
    gemui::Label warn_{"", 12};
    gemui::Label foot_{"Used by the Custom entry of the CRV row.", 12};
};

}  // namespace stym::pluginui
