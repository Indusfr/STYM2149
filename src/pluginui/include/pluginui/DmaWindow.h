// STYM2149 — pluginui: DMA window (M6, B8, D-153), sample bank of the STE's DMA sound: eight samples (rate, mono or
// stereo, root note, loop), loading, preview and tools
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 window (the prototype has none; a maxYMiser feature, Q6 of the M6 plan). The DMA sound plays with the
// STE machine (Computer): elsewhere, a button switches to it. The tracks of the DMA type play these samples (DMA
// tracks in 3-channel mode, DMA type in 16-channel mode); the track chooses its own in the GENERATOR frame. Here: the
// eight slots (the chosen one is edited), its drawing (a click loads a file), Load, Play, Clear, rate, Mono or Stereo,
// Loop, root note, Sign and Gain tools (±3 dB). Loading a file cannot be undone (like a Digidrum); the other settings
// are each one undo step (one per gesture for a slider).
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "pluginui/Widgets.h"
#include "ymengine/Dma.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. It keeps no copy of the bank: it reads MainWindow's
// non-automatable state (dma for the settings of the eight slots, dmaSamples for their bytes) and writes it through
// Host::editExtras, pushing an UndoEntry of kind Dma first.
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

    // Rectangle of an element (captures, tests): "dmaWin", "dmaUseSte", "dmaSlot0" to "dmaSlot7", "dmaWave",
    // "dmaLoad", "dmaPlay", "dmaClear", "dmaRate0" to "dmaRate3", "dmaMono", "dmaStereo", "dmaLoop", "dmaRoot",
    // "dmaRootV" (its typed value), "dmaSign", "dmaGainDown", "dmaGainUp".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Slider (tests): "dmaRoot".
    const gemui::Slider* slider(std::string_view id) const noexcept;
    std::string status() const { return status_.text(); }
    std::string rootText() const { return rootv_.text(); }
    // Short name of rate k (index of ymengine::kDmaRateHz, clamped to 0-3): "6.3k" to "50k".
    static std::string rateName(int k);

private:
    void build();
    void wire();
    void layout();
    bool ste() const noexcept;
    const ymengine::DmaParams& params() const noexcept;
    const std::vector<int8_t>& bytes() const noexcept;  // sample of the chosen slot
    // Setting of the chosen slot: one undo step (without the bytes) when it changes something. The Root slider does not
    // come through here: its own handler makes one step per gesture.
    void edit(const std::function<void(ymengine::DmaSlot&)>& fn);
    // Bytes of the chosen slot (tools, Clear, switch mono ↔ stereo): one step that keeps them.
    void editBytes(const std::function<void(ymengine::DmaSlot&, std::vector<int8_t>&)>& fn);
    void pushUndo(int slot);  // Dma step: settings, and bytes of `slot` (-1: without bytes)
    void setStereo(bool stereo);

    MainWindow& m_;
    int sel_ = 0;              // edited slot, 0-7
    bool rootArmed_ = false;   // set by the press on the Root slider, cleared by the change that pushes the undo step
    gemui::Label status_{"", 14};      // machine and the tracks that can play the bank
    gemui::Button useSte_{"Use STE"};  // shown on the ST machine only
    gemui::Label hint_{"Eight 8-bit signed samples, 32 KB each. A track of the DMA type plays one of them, tuned from its "
                       "root note.",
                       12},
        hint2_{"3 ch: the eight DMA tracks follow the YM tracks. 16 ch: any track can use the DMA type.", 12};
    Rule sep_;

    gemui::Label labSample_{"Sample", 14};
    std::array<gemui::Button, ymengine::kDmaSlots> slots_;
    DmaWave wave_;
    gemui::Button load_{"Load..."}, play_{"Play"}, clear_{"Clear"};
    gemui::Label labRate_{"Rate", 14};
    std::array<gemui::Button, ymengine::kDmaRates> rates_;
    gemui::Label labFormat_{"Format", 14};
    gemui::Button mono_{"Mono"}, stereo_{"Stereo"}, loop_{"Loop"};
    gemui::Label labRoot_{"Root", 14};
    gemui::ValueField rootv_;  // typed too (sixth DAW test, item 66, D-264)
    gemui::Slider root_;
    gemui::Label labTools_{"Tools", 14};
    gemui::Button sign_{"Sign"}, gainDown_{"-3 dB"}, gainUp_{"+3 dB"};
};

}  // namespace stym::pluginui
