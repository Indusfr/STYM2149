// STYM2149 — pluginui: Digidrums editor (M6, B9, D-154): eight slots, name, waveform, loading, preview, rate, root
// note, fine tune, part played (Start, End) and its sustain loop, 4-bit conversion, tools and named banks
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 window (the prototype has only the slot buttons and the drawing in the track's frame; a maxYMiser feature,
// Q6 of the M6 plan). Slot column: the eight slots (the chosen one is edited), name (16 characters), playback rate at
// the root note (2000 to 20000 Hz, presets 7800, 8000 and 11025 Hz), root note, fine tune (±100 cents). Sample column:
// drawing of the 4-bit values (a click loads a file), Load, Play, Clear, the part played (Start, End, D-443) and its
// sustain loop (Loop), 4-bit conversion (Linear or YM levels) and tools on the 8-bit source (Normalize, Reverse, Fade
// out, Sign, -3 dB, +3 dB; the same as the DMA window's, D-450). Banks: save the eight slots under a name, load one
// again, delete one (a library shared by every project, Host::digidrumLibrary). Loading a file or a bank cannot be
// undone (like loading a Digidrum in the prototype); the other settings are each one undo step (one per gesture for a
// slider).
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "plugincore/Digidrum.h"
#include "plugincore/DigidrumBanks.h"
#include "plugincore/DmaSample.h"
#include "plugincore/EngineStatus.h"
#include "pluginui/Widgets.h"
#include "ymengine/Params.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. It keeps no copy of the slots: it reads them from MainWindow's
// non-automatable state (digiSlots for the settings and source, digidrums for the 4-bit values played) and writes
// them through Host::editExtras, pushing an UndoEntry of kind Digi first.
class DigidrumWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 200, kY = 150;  // top-left corner before the user moves the window (logical pixels)

    explicit DigidrumWindow(MainWindow& main);

    // Opens the window in front of the others.
    void show();
    // Reads the chosen slot again from MainWindow's state and lays the window out (called by MainWindow::refresh
    // after each change); nothing while the window is closed.
    void refresh();

    // Edited slot (0-7, kept by the editor session).
    int slot() const noexcept;
    void selectSlot(int k);
    // Slot previewed by the engine (EngineStatus::digiPreview): Play lit while the chosen slot loops (D-449).
    void refreshEngineStatus(const plugin::EngineStatus& t);

    // Banks: the library read again, a bank saved (same name: replaced), loaded again or deleted. Load and Delete
    // name the bank: the file can change between the menu and the confirmation (another instance).
    const std::vector<plugin::DigidrumBank>& banks();
    void saveBank(const std::string& name);
    void loadBank(const std::string& name);
    void deleteBank(const std::string& name);

    // Rectangle of an element (captures, tests): "digWin", "digSlot0" to "digSlot7", "digName", "digBanks",
    // "digWave", "digLoad", "digPlay", "digClear", "digRate", "digPreset0" to "digPreset2", "digRoot", "digFine",
    // "digLoop", "digStart", "digEnd", "digLinear", "digYm", "digNormalize", "digReverse", "digFade", "digSign",
    // "digGainDown", "digGainUp"; the typed values "digRateV", "digRootV", "digFineV", "digStartV", "digEndV".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Slider (tests): "digRate", "digRoot", "digFine", "digStart", "digEnd".
    const gemui::Slider* slider(std::string_view id) const noexcept;
    std::string valueText(std::string_view id) const;  // text to the right of a slider (same names)
    bool playLit() const noexcept { return play_.on(); }  // a looping preview of the chosen slot plays (D-449)

private:
    void build();
    void wire();
    void layout();
    const plugin::DigidrumSlotState& state() const noexcept;
    const std::vector<uint8_t>& nibbles() const noexcept;  // 4-bit values of the chosen slot
    // Setting of the slot (rate, note, tuning, loop, name): one step without the bytes, except during the gesture of
    // a slider already pushed (`armed` false).
    void edit(const std::function<void(plugin::DigidrumSlotState&)>& fn, bool push = true);
    // Source and values (conversion, tools, Clear): one step that keeps them; the values are computed again from the
    // source (derived from the values for a slot from before M6), the loop brought back within the new length.
    void editBytes(const std::function<void(plugin::DigidrumSlotState&, std::vector<int8_t>&)>& fn);
    void pushUndo(bool bytes);
    // Slider bound to a setting: one step at the first movement of each gesture.
    void bindSlider(gemui::Slider& s, bool& armed,
                    std::function<void(plugin::DigidrumSlotState&, double)> set);
    void banksMenu();
    void rename();
    void loadLibrary();
    void storeLibrary();
    void confirm(const std::string& line, const std::string& button, std::function<void()> ok);

    MainWindow& m_;
    std::vector<plugin::DigidrumBank> banks_;  // the library as last read (loadLibrary)
    bool unreadable_ = false;                  // library file present but unreadable: never rewritten

    gemui::Label hint_{"4-bit samples played by the timer of a Digidrum track, at the rate below from the root note.",
                       12},
        hint2_{"Start to End is played; Loop repeats it while the note is held. The 8-bit source is kept.", 12};
    Rule sep_;

    // Slot column.
    gemui::Label labSlot_{"Slot", 14};
    std::array<gemui::Button, ymengine::kDigidrumSlots> slots_;
    gemui::Label labName_{"Name", 14};
    gemui::Button name_;
    // The values right of the sliders are typed too (sixth DAW test, item 66, D-264).
    gemui::Label labRate_{"Rate", 14};
    gemui::ValueField ratev_;
    gemui::Slider rate_;
    std::array<gemui::Button, 3> presets_;
    gemui::Label labRoot_{"Root", 14};
    gemui::ValueField rootv_;
    gemui::Slider root_;
    gemui::Label labFine_{"Fine", 14};
    gemui::ValueField finev_;
    gemui::Slider fine_;
    gemui::Button banksBtn_{"Banks"};
    // Set by the press on a slider, cleared by its first change, which pushes the gesture's undo step.
    bool rateArmed_ = false, rootArmed_ = false, fineArmed_ = false, startArmed_ = false, endArmed_ = false;
    int previewing_ = -1;  // slot previewed by the engine, -1: none (D-449)
    bool looping() const noexcept;  // the chosen slot's preview plays with Loop on: Play stops it (D-449)
    void stopPreview();

    // Sample column.
    gemui::Label labSample_{"Sample", 14};
    DigiWave wave_;
    gemui::Button load_{"Load..."}, play_{"Play"}, clear_{"Clear"};
    gemui::Button loop_{"Loop"};
    gemui::Label labStart_{"Start", 14}, labEnd_{"End", 14};
    gemui::ValueField startv_, endv_;
    gemui::Slider start_, end_;
    gemui::Label labConvert_{"4-bit", 14};
    gemui::Button linear_{"Linear"}, ym_{"YM levels"};
    gemui::Label labTools_{"Tools", 14};
    gemui::Button normalize_{"Normalize"}, reverse_{"Reverse"}, fade_{"Fade out"}, sign_{"Sign"},
        gainDown_{"-3 dB"}, gainUp_{"+3 dB"};
    plugin::GainRun gainRun_;  // -3 dB and +3 dB in a row, from the source before them (D-453)
    void gain(double db);
};

}  // namespace stym::pluginui
