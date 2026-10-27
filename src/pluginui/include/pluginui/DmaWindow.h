// STYM2149 — pluginui: DMA window (M6, B8, D-153), sample bank of the STE's DMA sound: eight samples (rate, mono or
// stereo, part played, root note, loop, name), loading, preview and tools
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 window (the prototype has none; a maxYMiser feature, Q6 of the M6 plan). The DMA sound plays with the STE
// machine (Computer): elsewhere, a button switches to it. The tracks of the DMA type play these samples (DMA tracks in
// 3-channel mode, DMA type in 16-channel mode); the track chooses its own in the GENERATOR frame. Here: the eight
// slots (the chosen one is edited), its name (the file loaded, or typed, D-445), its drawing (a click loads a file),
// Load, Play, Clear, the part played (Start, End, D-444), rate (resampled with Resample on, D-454), Mono or Stereo,
// Loop, root note, fine tune (D-452) and the tools: those of the Digidrums (Normalize, Reverse, Fade out) with Sign
// and Gain (±3 dB), on the left, right or both channels of a stereo sample (D-450). Banks (D-462): save the eight
// samples under a name, load them again, delete them, in a library shared by every project (Host::dmaLibrary), as the
// Digidrums window does. Loading a file or a bank cannot be undone (like a Digidrum); the other settings are each one
// undo step (one per gesture for a slider).
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "plugincore/DmaBanks.h"
#include "plugincore/DmaSample.h"
#include "plugincore/EngineStatus.h"
#include "pluginui/Widgets.h"
#include "ymengine/Dma.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. It keeps no copy of the bank: it reads MainWindow's
// non-automatable state (dma for the settings of the eight slots, dmaSamples for their bytes, dmaNames for their
// names) and writes it through Host::editExtras, pushing an UndoEntry of kind Dma first.
class DmaWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 160, kY = 180;  // top-left corner before the user moves the window (logical pixels)

    explicit DmaWindow(MainWindow& main);

    // Opens the window in front of the others.
    void show();
    void showSlot(int slot);  // opened on a slot (button of the GENERATOR frame)
    // Reads the chosen slot again from MainWindow's state and lays the window out (called by MainWindow::refresh
    // after each change); nothing while the window is closed.
    void refresh();
    int slot() const noexcept { return sel_; }  // edited slot (interface state)
    // Slot previewed by the engine (EngineStatus::dmaPreview): Play lit while the chosen slot loops (D-449).
    void refreshEngineStatus(const plugin::EngineStatus& t);

    // Rectangle of an element (captures, tests): "dmaWin", "dmaUseSte", "dmaSlot0" to "dmaSlot7", "dmaName", "dmaWave",
    // "dmaLoad", "dmaPlay", "dmaClear", "dmaStart", "dmaEnd", "dmaStartV", "dmaEndV" (typed values), "dmaRate0" to
    // "dmaRate3", "dmaResample", "dmaMono", "dmaStereo", "dmaLoop", "dmaRoot", "dmaRootV" (its typed value),
    // "dmaFine", "dmaFineV" (D-452), "dmaNormalize", "dmaReverse", "dmaFade", "dmaSign", "dmaGainDown", "dmaGainUp",
    // "dmaChanL", "dmaChanR", "dmaBanks" (D-462).
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Slider (tests): "dmaRoot", "dmaFine", "dmaStart", "dmaEnd".
    const gemui::Slider* slider(std::string_view id) const noexcept;
    std::string status() const { return status_.text(); }
    bool playLit() const noexcept { return play_.on(); }  // a looping preview of the chosen slot plays (D-449)
    std::string rootText() const { return rootv_.text(); }
    std::string fineText() const { return finev_.text(); }  // "+12 ct" (D-452)
    std::string startText() const { return startv_.text(); }  // Start and End in frames (D-444)
    std::string endText() const { return endv_.text(); }
    // Short name of rate k (index of ymengine::kDmaRateHz, clamped to 0-3): "6.3k" to "50k".
    static std::string rateName(int k);

    // Named banks (D-462), read again from the library at each call. Saving under an existing name replaces that bank;
    // loading replaces the eight samples (no undo step); an unreadable library is never overwritten.
    const std::vector<plugin::DmaBank>& banks();
    void saveBank(const std::string& name);
    void loadBank(const std::string& name);
    void deleteBank(const std::string& name);

private:
    void build();
    void wire();
    void layout();
    bool ste() const noexcept;
    const ymengine::DmaParams& params() const noexcept;
    const std::vector<int8_t>& bytes() const noexcept;  // sample of the chosen slot
    uint32_t frames() const noexcept;                   // its frames (one stereo frame: two bytes)
    // Setting of the chosen slot: one undo step (without the bytes) when it changes something and `push` is set (the
    // first change of a slider gesture). The Root slider does not come through here: its own handler makes one step
    // per gesture.
    void edit(const std::function<void(ymengine::DmaSlot&)>& fn, bool push = true);
    // Start, End (D-444) or Fine (D-452) slider: one undo step per gesture, `set` writing the value into the slot.
    void bindSlider(gemui::Slider& s, bool& armed, std::function<void(ymengine::DmaSlot&, double)> set);
    // Bytes of the chosen slot (tools, Clear, switch mono ↔ stereo): one step that keeps them.
    void editBytes(const std::function<void(ymengine::DmaSlot&, std::vector<int8_t>&)>& fn);
    void pushUndo(int slot);  // Dma step: settings, and bytes of `slot` (-1: without bytes)
    void setStereo(bool stereo);
    // Rate k of the chosen slot: with Resample on, the bytes resampled to it, Start and End following (D-454).
    void setRate(int k);
    void rename();  // name of the chosen slot, typed in an alert (D-445)
    // Tool on the chosen slot's bytes, on the channels chosen with L and R (D-450); Reverse mirrors Start and End when
    // every frame turns round (mono, or both channels); Gain steps in a row start from the bytes before them (D-453).
    void tool(plugin::DmaTool t, double db = 0);
    void loadLibrary();
    void storeLibrary();
    void banksMenu();
    // Alert for a destructive action: Cancel first, as the default (gemConfirm); `ok` runs on the other button.
    void confirm(const std::string& line, const std::string& button, std::function<void()> ok);

    MainWindow& m_;
    int sel_ = 0;              // edited slot, 0-7
    int previewing_ = -1;      // slot previewed by the engine, -1: none (D-449)
    bool looping() const noexcept;  // the chosen slot's preview plays with Loop on: Play stops it (D-449)
    void stopPreview();
    bool rootArmed_ = false;   // set by the press on the Root slider, cleared by the change that pushes the undo step
    bool startArmed_ = false, endArmed_ = false, fineArmed_ = false;  // the same for Start, End and Fine
    gemui::Label status_{"", 14};      // machine and the tracks that can play the bank
    gemui::Button useSte_{"Use STE"};  // shown on the ST machine only
    gemui::Label hint_{"Eight 8-bit signed samples, 32 KB each. A track of the DMA type plays one of them, tuned from its "
                       "root note.",
                       12},
        hint2_{"3 ch: the eight DMA tracks follow the YM tracks. 16 ch: any track can use the DMA type.", 12};
    Rule sep_;

    // Laid out like the Digidrums window (D-446): the slot column on the left, the sample column on the right.
    gemui::Label labSlot_{"Slot", 14};
    std::array<gemui::Button, ymengine::kDmaSlots> slots_;
    gemui::Label labName_{"Name", 14};
    gemui::Button name_;
    DmaWave wave_;
    gemui::Button load_{"Load..."}, play_{"Play"}, clear_{"Clear"};
    gemui::Label labStart_{"Start", 14}, labEnd_{"End", 14};
    gemui::ValueField startv_, endv_;  // typed too, in frames
    gemui::Slider start_, end_;
    gemui::Label labRate_{"Rate", 14};
    std::array<gemui::Button, ymengine::kDmaRates> rates_;
    // A new rate resamples the sample (D-454): preference "dmaResample", off by default.
    gemui::Button resample_{"Resample"};
    gemui::Label labFormat_{"Format", 14};
    gemui::Button mono_{"Mono"}, stereo_{"Stereo"}, loop_{"Loop"};
    gemui::Label labRoot_{"Root", 14};
    gemui::ValueField rootv_;  // typed too (sixth DAW test, item 66, D-264)
    gemui::Slider root_;
    gemui::Label labFine_{"Fine", 14};  // fine tune of the slot in cents (D-452)
    gemui::ValueField finev_;
    gemui::Slider fine_;
    gemui::Label labSample_{"Sample", 14};  // over the drawing, Load, Play and Clear on its line
    gemui::Label labTools_{"Tools", 14};
    gemui::Button normalize_{"Normalize"}, reverse_{"Reverse"}, fade_{"Fade out"}, sign_{"Sign"},
        gainDown_{"-3 dB"}, gainUp_{"+3 dB"};
    // Channels the tools work on in stereo (D-450): bit 0 left, bit 1 right, at least one lit. Interface state, not
    // saved; L and R are greyed for a mono or empty sample.
    gemui::Button chanL_{"L"}, chanR_{"R"};
    unsigned toolCh_ = 3;
    gemui::Button banksBtn_{"Banks"};  // under Fine, as in the Digidrums window (D-462)
    std::vector<plugin::DmaBank> banks_;
    bool unreadable_ = false;  // the library file exists but could not be read
    plugin::GainRun gainRun_;  // -3 dB and +3 dB in a row, from the bytes before them (D-453)
};

}  // namespace stym::pluginui
