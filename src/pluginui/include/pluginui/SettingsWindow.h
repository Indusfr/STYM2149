// STYM2149 — pluginui: Settings window, the settings of the sound engine, of MIDI and of the interface (Indus,
// 25/10/2026 19:33, D-430)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A STYM2149 addition (the prototype has none), opened by Options > Settings. It gathers what the Options menu held in
// submenus: Sound engine (Computer, Chip clock, Output chain, Resampling, STE bass and treble, Soft clip, Timer,
// Quantize to VBL), with the Tuning of A4 (D-429) after the clock, its MIDI entries (MIDI routing, Velocity, Bend
// range, Wheel depth), with the MIDI expression (D-432: the targets of the bend, the wheel and the aftertouch, MPE,
// its bend range and the target of its CC74), and Pixels, Renderer, Note names, Keyboard layout and Tooltips (On /
// Off, Language and the Languages folder). Three frames side by side: SOUND ENGINE, MIDI and INTERFACE. Each setting
// is a label and a menu button that shows its value; the menu lists the values with the current one checked, and
// greys out those that do not apply, as the submenus did (D-206, D-207, D-237). Tuning A4 is a value typed in Hz.
// Under the frames, Cancel, the default button with its bold border, and OK (Indus, 25/10/2026 21:50, D-435): a
// setting applies as soon as it is picked, so that it is heard; OK keeps the changes and closes the window, Cancel
// puts back the settings changed in the window since it opened, then closes it, as the close box and Escape do.
// When a setting then differs from its value at the opening, Cancel first asks in a GEM box whether the changes may
// be lost (Indus, 26/10/2026 03:51, D-437). The VBL frequency left the Options menu for SOUND ENGINE, before Quantize
// to VBL (Indus, 26/10/2026 04:03, D-441).
#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Menu.h"

namespace stym::pluginui {

class MainWindow;

// Owned by MainWindow, used on the message thread. The host parameters are set through MainWindow::set, Quantize to
// VBL through the non-automatable state, the others are preferences of the editor (Host::setPreference).
class SettingsWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 160, kY = 70;  // top-left corner before the user moves the window (logical pixels)

    explicit SettingsWindow(MainWindow& main);

    // Opens the window in front of the others; opened, it starts noting the settings it changes (D-435).
    void show();
    // OK: the changes kept, the window closed. Cancel (the close box and Escape too): the settings changed in the
    // window since it opened put back as they were, the window closed; a GEM box asks first when one of them differs
    // from its value at the opening (D-437).
    void ok();
    void cancel();
    void requestClose() override { cancel(); }
    // A setting changed in the window differs from its value at the opening.
    bool changed() const;
    // Enter with the window in front of the others, Tuning A4 not being typed: Cancel, the default button. True when
    // the key was used.
    bool dialogKey(const gemui::KeyEvent& e);
    // Shows the values again (called by MainWindow::refresh, and after a preference is picked).
    void refresh();

    // Ids of the settings with a menu button, in the window's order: "setComputer", "setClock", "setChain",
    // "setResampling", "setBass", "setTreble", "setSoftClip", "setTimer", "setQuantize", "setRouting", "setVelocity",
    // "setBendTarget", "setBend", "setWheelTarget", "setWheel", "setAftertouch", "setMpe", "setMpeBend", "setMpeSlide",
    // "setPixels", "setRenderer", "setNoteNames", "setLayout", "setTips", "setLanguage".
    static std::vector<std::string_view> menuIds();
    // Label of a setting by its id ("Chip clock"); "setA4" is the Tuning A4 field.
    static std::string_view labelOf(std::string_view id) noexcept;
    // Rectangle of a control (captures, tests): "settingsWin", the ids above, "setA4", "setFolder", the frames
    // "setEngine", "setMidi", "setInterface", and the buttons "setCancel" and "setOk".
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Menu button of a setting (tests): its click opens the menu of the values, under it; also "setFolder",
    // "setCancel" and "setOk".
    const gemui::Button* button(std::string_view id) const noexcept;
    const gemui::ValueField& tuningField() const noexcept { return a4v_; }
    // The entries of a setting's menu and the checked one (-1: none), as its button opens them.
    std::vector<gemui::MenuItem> items(std::string_view id, int& checked);

    // Tuning A4 typed in Hz: a number, a comma read as a point, rounded to 0.01 Hz and brought back to 350 to 480 Hz;
    // nullopt when nothing usable is typed.
    static std::optional<double> parseA4(const std::string& text);

private:
    enum Row : int {
        kComputer, kClock, kA4, kChain, kResampling, kBass, kTreble, kSoftClip, kTimer, kVbl, kQuantize,  // ENGINE
        kRouting, kVelocity, kBendTarget, kBend, kWheelTarget, kWheel, kAftertouch, kMpe, kMpeBend,  // MIDI
        kMpeSlide,
        kPixels, kRenderer, kNoteNames, kLayout, kTips, kLanguage,                                   // INTERFACE
        kRows
    };

    void build();
    void wire();
    void layout();
    static int rowOf(std::string_view id) noexcept;
    std::vector<gemui::MenuItem> rowItems(int row, int& checked);
    std::string valueText(int row);
    // The widest text the button of a row can show, so that the buttons keep their width (the language names are
    // read again at each refresh).
    double buttonWidth(int row);
    void preferenceSet();  // after a preference is picked: the main window and this one shown again
    // The entries of a row's menu, each pick noting first the value the row had (D-435).
    std::vector<gemui::MenuItem> notedItems(int row, int& checked);
    // Value of a row before the window first changed it since it opened (D-435): its host parameters (index and
    // plain value), or the text of its preference (the tooltip language included), or Quantize to VBL / Tooltips.
    struct Saved {
        std::vector<std::pair<int, float>> params;
        std::string text;
        bool flag = false;
    };
    void remember(int row);
    void putBack();  // the rows noted, as they were
    bool differs(int row) const;  // a row noted no longer has its value at the opening

    MainWindow& m_;
    gemui::GroupBox engineFs_{"SOUND ENGINE"}, midiFs_{"MIDI"}, uiFs_{"INTERFACE"};
    std::array<gemui::Label, kRows> labs_;
    std::array<gemui::Button, kRows> btns_;  // kA4 has the field a4v_ instead; its button stays out of the window
    gemui::ValueField a4v_;
    gemui::Button folder_{"Languages folder..."};
    gemui::Button cancel_{"Cancel", gemui::Button::Style::Default}, ok_{"OK"};
    std::array<std::optional<Saved>, kRows> saved_;
    bool keep_ = false;  // set by OK: the close that follows keeps the changes
};

}  // namespace stym::pluginui
