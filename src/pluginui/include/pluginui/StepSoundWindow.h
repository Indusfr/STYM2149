// STYM2149 — pluginui: settings of the sound a step chooses, opened by a right click on a GEN, SRC or ENV cell of the
// Step Sequencer (24/10/2026)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 window. The cell only chooses a generator, a source or a shape: the settings it brings into play are those
// of the focused track, so the window shows the main window's GENERATOR, SOURCE or ENVELOPE settings that the cell
// uses, and drives the main window's own controls (same callbacks, same undo steps, same values on both sides).
// GEN: SID Voice (Timer ratio, SID min vol, SID wave, the menu of its forms and its Steps, Period with Sine or Bounce,
// Width with Square) or Sync Buzzer (Tuning, N, D, Fine, YM Pad, Presets, its steps); Chip Voice has none, a line says
// so. An empty GEN cell: the generator the step plays.
// SRC: Noise period and Volume, greyed when the cell's source does not use them, then Level. ENV: Env freq and EP.
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "pluginui/Widgets.h"

namespace stym::pluginui {

class MainWindow;

class StepSoundWindow : public gemui::FloatWindow {
public:
    enum class Kind { Generator, Source, Envelope };
    static constexpr int kX = 330, kY = 140;  // top-left corner before the user moves the window (logical pixels)

    explicit StepSoundWindow(MainWindow& main);
    ~StepSoundWindow() override;

    // Opens the window in front for step `step` (from 0) of the focused track: GEN (value: the cell's generator, a
    // ymengine::Generator, or -1 for an empty cell), SRC (the cell's source, 0 to 7 as the SRC menu numbers them) or
    // ENV (value unused).
    void openFor(Kind kind, int step, int value);
    // Follows the main window's controls (called by MainWindow::refresh).
    void refresh();

    Kind kind() const noexcept { return kind_; }
    int generator() const noexcept { return gen_; }  // GEN: the generator shown
    // Controls (tests, captures), by id: "ratio", "vmin", "waveSteps", "wavePeriod", "waveWidth", "buzN", "buzD",
    // "buzFine", "buzzLen", "np", "vol", "level", "ef" (sliders, their fields with a "v" after the id; "ep" the EP
    // field); buttons "waveForm" (the menu of the SID wave's forms), "buzTuned", "buzPad", "buzPresets", "buzzStep0"
    // to "buzzStep7";
    // "stepWin" the window, "stepWave" the SID wave, "stepNone" the line of Chip Voice.
    gemui::Slider* slider(std::string_view id) noexcept;
    gemui::ValueField* field(std::string_view id) noexcept;
    gemui::Button* button(std::string_view id) noexcept;
    const gemui::Label* label(std::string_view id) const noexcept;  // the label of a slider row
    std::optional<gemui::Rect> rectOf(std::string_view id) const;  // none for a control hidden

private:
    // A slider row of the main window, shown again: label (left or above), slider, a text before the field ("x"), the
    // field and its unit.
    struct SliderRow {
        std::string id;
        gemui::Label label, pre, unit;
        gemui::Slider slider;
        gemui::ValueField field;
        gemui::Slider* src = nullptr;
        gemui::ValueField* srcField = nullptr;
        gemui::Label* srcLabel = nullptr;
        bool above = false;       // the label on a line of its own
        bool held = false;        // a gesture of this slider is in progress
        double labelW = 0;        // labels of a column lined up
        bool ownDim = false;      // greyed by the cell, whatever the main window shows
        bool ownDimmed = false;
        const SliderRow* alignTo = nullptr;  // its slider ends where that row's does (SID Voice)
        std::function<bool()> showWith;  // shown only while it says so
    };
    struct ButtonItem {
        std::string id;
        gemui::Button button;
        gemui::Button* src = nullptr;
    };
    struct FieldSpec {
        int minChars = 2, pad = 0;
        std::string mask;
    };

    void clear();
    SliderRow& addSlider(std::string id, const char* label, gemui::Slider& src, gemui::ValueField& srcField,
                         gemui::Label* srcLabel, FieldSpec spec, bool above);
    ButtonItem& addButton(std::string id, gemui::Button& src, std::function<void(gemui::Button&)> click = {});
    gemui::Label& addHeading(const char* text);
    void build();
    void layout();
    std::string titleText() const;

    MainWindow& m_;
    Kind kind_ = Kind::Generator;
    int step_ = 0, value_ = 0, gen_ = 0, track_ = 0;
    double rowW_ = 0;  // width of the rows: that of a main window frame, more for a long title (layout)
    std::vector<std::unique_ptr<SliderRow>> rows_;
    std::vector<std::unique_ptr<ButtonItem>> buttons_;
    std::vector<std::unique_ptr<gemui::Label>> headings_;
    SidWaveGrid wave_;
    gemui::Label none_{"Chip Voice: no settings of its own"};
    gemui::Label hint_{"", 12};
    std::vector<std::function<double(double x, double y)>> lines_;  // layout, top to bottom
};

}  // namespace stym::pluginui
