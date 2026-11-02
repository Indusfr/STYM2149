// STYM2149 — pluginui: Digidrums editor: slots by pages of eight, memory of the Atari, name, waveform,
// loading (also by a file dropped on it), preview, rate (resampled or not), root note, fine tune, part played (Start,
// End) and its sustain loop, 4-bit conversion, tools, named banks and the channel that a stereo file loads
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 window (the prototype has only the slot buttons and the drawing in the track's frame; a maxYMiser feature).
// Across the top: the slots, eight to a page turned by the arrows (127 of them, as long as
// the memory allows; the chosen one is edited, one past the memory crossed out), then, in the 3-channel modes
// only, the memory the samples take out of that of the Atari chosen in Settings with its gauge. Slot
// column: name (16 characters, typed in its field), playback rate at the root note (2000 to 20000 Hz, presets
// 7800, 8000 and 11025 Hz) and Resample under it, root note, fine tune (±100 cents). Sample column: drawing
// of the 4-bit values (a click loads a file, a file dropped on it too), Load, Play, Clear, the part played
// (Start, End) and its
// sustain loop (Loop), 4-bit conversion (Linear or YM levels) and tools on the 8-bit source (Normalize, Reverse, Fade
// out, Sign, -3 dB, +3 dB; the same as the DMA window's). Banks: save the slots under a name, load one
// again, delete one (a library shared by every project, Host::digidrumLibrary). Stereo: what Load takes from a stereo
// file, its left or right channel or both mixed (a menu; an option of the window kept in the preferences, Indus,
// 29/10/2026 01:41-01:43). Loading a file into a slot that holds a sample asks first. Loading a file
// or a bank cannot be undone (like loading a Digidrum in the prototype); the other settings are each one undo step
// (one per gesture for a slider, the resampling of a rate included).
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

    // Edited slot (0 to 126, kept by the editor session).
    int slot() const noexcept;
    void selectSlot(int k);
    // Slot previewed by the engine (EngineStatus::digiPreview): Play lit while the chosen slot loops.
    void refreshEngineStatus(const plugin::EngineStatus& t);

    // Banks: the library read again, a bank saved (same name: replaced), loaded again or deleted. Load and Delete
    // name the bank: the file can change between the menu and the confirmation (another instance).
    const std::vector<plugin::DigidrumBank>& banks();
    void saveBank(const std::string& name);
    void loadBank(const std::string& name);
    void deleteBank(const std::string& name);

    // Rectangle of an element (captures, tests): "digWin", "digSlot0" to "digSlot7" (the page shown), "digPrev",
    // "digNext", "digMemory" (its text) and "digGauge" (3-channel modes), "digName" (the name field), "digBanks",
    // "digWave", "digLoad", "digPlay", "digClear", "digRate", "digPreset0" to "digPreset2", "digResample", "digRoot",
    // "digFine",
    // "digLoop", "digStart", "digEnd", "digLinear", "digYm", "digNormalize", "digReverse", "digFade", "digSign",
    // "digGainDown", "digGainUp", "digStereo"; the typed values "digRateV", "digRootV", "digFineV", "digStartV",
    // "digEndV".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Slider (tests): "digRate", "digRoot", "digFine", "digStart", "digEnd".
    const gemui::Slider* slider(std::string_view id) const noexcept;
    std::string valueText(std::string_view id) const;  // text to the right of a slider (same names)
    bool playLit() const noexcept { return play_.on(); }  // a looping preview of the chosen slot plays
    // Button k of the page of slots shown and the memory line, empty when hidden (tests).
    const gemui::Button& slotButton(int k) const noexcept { return slots_[size_t(k)]; }
    std::string memoryLine() const { return memText_.visible() ? memText_.text() : std::string(); }
    // The name field and the drawing a file is dropped on.
    const gemui::TextField& nameField() const noexcept { return name_; }
    bool isWave(const gemui::Widget& w) const noexcept { return isOpen() && &w == &wave_; }

private:
    void build();
    void wire();
    void layout();
    const plugin::DigidrumSlotState& state() const noexcept;
    const plugin::SharedSamples<uint8_t>& nibbles() const noexcept;  // 4-bit values of the chosen slot
    // Setting of the slot (rate, note, tuning, loop, name): one step without the bytes, except during the gesture of
    // a slider already pushed (`armed` false).
    void edit(const std::function<void(plugin::DigidrumSlotState&)>& fn, bool push = true);
    // Source and values (conversion, tools, Clear): one step that keeps them; the values are computed again from the
    // source (derived from the values for an older slot), the loop brought back within the new length.
    void editBytes(const std::function<void(plugin::DigidrumSlotState&, std::vector<int8_t>&)>& fn);
    void pushUndo(bool bytes);
    // Slider bound to a setting: one step at the first movement of each gesture.
    void bindSlider(gemui::Slider& s, bool& armed,
                    std::function<void(plugin::DigidrumSlotState&, double)> set);
    void banksMenu();
    // The name typed: into the slot it was typed for (nameSlot_), one undo step when it changes the name.
    void commitName();
    // Rate of the chosen slot in Hz: with Resample on, the source resampled to it from the slot as rateFrom_
    // holds it (the slot when the gesture or the change began), Start and End following; one undo step when `push`.
    void setRate(double hz, bool push);
    bool resampling() const;  // the "digiResample" preference
    plugin::DigiStereo stereoChoice() const;  // the Stereo preference
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

    // Slots, eight to a page: the arrows move the choice by eight.
    gemui::Label labSlot_{"Slot", 14};
    gemui::Button prev_{"<"}, next_{">"};
    std::array<gemui::Button, 8> slots_;
    // Memory of the Atari: the bytes the samples take and the gauge, in the 3-channel modes.
    gemui::Label labMemory_{"Memory", 14}, memText_{"", 14};
    MemoryGauge gauge_;

    // Slot column.
    gemui::Label labName_{"Name", 14};
    NameField name_;   // typed in place
    int nameSlot_ = 0;  // slot whose name the field shows
    // The values right of the sliders are typed too (sixth DAW test, item 66).
    gemui::Label labRate_{"Rate", 14};
    gemui::ValueField ratev_;
    gemui::Slider rate_;
    std::array<gemui::Button, 3> presets_;
    // A new rate resamples the source: preference "digiResample", off by default, as in the DMA window.
    gemui::Button resample_{"Resample"};
    std::optional<plugin::DigidrumSlotState> rateFrom_;  // the slot before the rate gesture, its source filled
    gemui::Label labRoot_{"Root", 14};
    gemui::ValueField rootv_;
    gemui::Slider root_;
    gemui::Label labFine_{"Fine", 14};
    gemui::ValueField finev_;
    gemui::Slider fine_;
    gemui::Label labStereo_{"Stereo", 14};
    gemui::Button stereo_;  // menu: Left, Right, Mix
    gemui::Button banksBtn_{"Banks"};
    // Set by the press on a slider, cleared by its first change, which pushes the gesture's undo step.
    bool rateArmed_ = false, rootArmed_ = false, fineArmed_ = false, startArmed_ = false, endArmed_ = false;
    int previewing_ = -1;  // slot previewed by the engine, -1: none
    bool looping() const noexcept;  // the chosen slot's preview plays with Loop on: Play stops it
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
    plugin::GainRun gainRun_;  // -3 dB and +3 dB in a row, from the source before them
    void gain(double db);
};

}  // namespace stym::pluginui
