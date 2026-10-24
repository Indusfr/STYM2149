// STYM2149 — pluginui: Instruments window (M6, B1, D-147), the project's 32 instruments
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 window (the prototype has no instruments), in the style of the Patches window: 32 slots numbered $01 to
// $20 in two columns. Click: an instrument loads into the focused track, an empty slot receives the sound of the
// focused track (name asked); right click: Store channel, Load into channel, Rename, Clear, Save to Patches, Load
// from Patches. Melodic steps play an instrument through the INS row of the Step Sequencer.
// Opening: Edit > Instruments, Ctrl+T, or the last entry of the INS row's menu.
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gemui/Controls.h"
#include "plugincore/Instruments.h"
#include "pluginui/Widgets.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. The instruments live in the project, in MainWindow's
// non-automatable state (instruments); Store, Rename and Clear change them through Host::editExtras without an undo
// step, while Load into channel is one undo step of the focused track.
class InstrumentsWindow : public gemui::FloatWindow {
public:
    // Top-left corner before the user moves the window, and its fixed width (logical pixels).
    static constexpr int kX = 200, kY = 120, kWidth = 400;

    explicit InstrumentsWindow(MainWindow& main);

    // Opens the window in front of the others.
    void show();
    // Slot labels from the state (called by MainWindow::refresh); nothing while the window is closed.
    void refresh();

    // Rectangle of an element (captures, tests): "insWin", "insHint", "insSlot0" to "insSlot31".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Text of a slot (tests).
    std::string slotText(int k) const { return slots_[size_t(k)].text(); }

    // Actions on slot k (0 to 31), also those of the menu.
    void storeChannel(int k);   // name asked, then the sound of the focused track
    void loadIntoChannel(int k);
    void rename(int k);
    void clear(int k);
    void saveToPatches(int k);
    void loadFromPatches(int k);  // menu of the library's patches

private:
    void build();
    void wire();
    void layout();
    void slotMenu(int k);                       // right click on slot k
    void put(int k, plugin::Instrument ins);    // writes slot k into the state (no undo step)
    const plugin::Instrument& at(int k) const;  // slot k as MainWindow last read it
    // Name prompt (Cancel, OK) of kInstrumentNameMax characters, `text` proposed; `ok` gets the text on OK.
    void prompt(const std::string& message, const std::string& text, std::function<void(const std::string&)> ok);

    MainWindow& m_;
    Paragraph hint_{"Click: load into the focused channel, or store it in an empty slot. Right click for actions."};
    Rule sep_;
    std::array<gemui::Button, plugin::kInstruments> slots_;
};

}  // namespace stym::pluginui
