// STYM2149 — pluginui: Instruments window, the project's 127 instruments
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 window (the prototype has no instruments), in the style of the Patches window: 127 slots numbered 1 to
// 127 (32 in earlier versions; in decimal, $01 to $7F before), 32 to a page in two columns, the arrows under
// them turning the pages. Click: an instrument loads into the focused track, an empty slot receives the sound of the
// focused track (name asked); right click: Store channel, Load into channel, Rename, Clear, Save to Patches, Load from
// Patches. Melodic steps play an instrument through the INS row of the Step Sequencer.
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

    // Slots to a page and pages: 1-32, 33-64, 65-96, 97-127 (in decimal).
    static constexpr int kPageSlots = 32;
    static constexpr int kPages = (plugin::kInstruments + kPageSlots - 1) / kPageSlots;
    // Page shown (0 to kPages - 1); show() opens on that of the focused track's instrument.
    int page() const noexcept { return page_; }
    void setPage(int page);

    // Rectangle of an element (captures, tests): "insWin", "insHint", "insSlot0" to "insSlot31" (the buttons of the
    // page), "insPrev", "insPage", "insNext".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Text of button i of the page (tests).
    std::string slotText(int i) const { return slots_[size_t(i)].text(); }

    // Actions on slot k (0 to 126), also those of the menu.
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
    std::array<gemui::Button, kPageSlots> slots_;  // button i: slot page_ * kPageSlots + i
    gemui::Button prev_{"<"}, next_{">"};
    gemui::Label pageLab_;  // "01-20": the slots of the page
    int page_ = 0;
};

}  // namespace stym::pluginui
