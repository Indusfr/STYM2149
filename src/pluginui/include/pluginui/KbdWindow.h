// STYM2149 — pluginui: Computer Keyboard window (#kbdWin of the prototype), playing from the computer keyboard
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Octave of the keys (Oct -, Oct +, from 0 to 9), notes held during an octave change (Keep, Follow), played range,
// drawing of a TKL keyboard and its numeric keypad (#tkl, #tklnum): keys that play a note (white, black), that
// change the octave (grey), shortcuts (light grey), keys without effect; held keys in halftone.
// The computer keyboard plays only while the window is open (kbdWinOn); closing it cuts on-screen playing.
//
// Keys: the prototype reads the physical position (KeyboardEvent.code); a plugin receives only the character.
// The layout chosen in Settings > Keyboard layout says which character each position carries; Auto takes the
// layout of the system keyboard (Host::keyboardLayout, D-215; QWERTY before, D-131). Positions and sizes: the
// prototype's CSS boxes at 100 % (window at 120, 120).
// Percussion track (Chipdrum, Digidrum, D-229): the eight keys of the middle row from A to K play pads 1 to 8 like the
// pads of the main window, engraved with their sound; the other note keys, the octave keys and the octave settings
// have no role.
#pragma once
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "pluginui/Widgets.h"

namespace stym::pluginui {

class MainWindow;

// Effect of a computer keyboard key (physical codes of the prototype: CODEMAP, KeyZ, KeyX, KeyM, keypad).
struct KbdAction {
    enum class Kind { None, Note, OctDown, OctUp, Metro, Numpad } kind = Kind::None;
    int value = 0;  // Note: CODEMAP offset (0 = C, 17 = F of the next octave); Numpad: digit
};
// Layout: "auto", "qwerty", "azerty" or "qwertz" (kbdLayout preference).
KbdAction kbdAction(const gemui::KeyEvent& e, std::string_view layout);

// #tkl and #tklnum: one key per cell, drawn and clicked here.
class TklDrawing : public gemui::Widget {
public:
    enum class Kind { Dead, Note, Sharp, Oct, Tr, OctNum };
    struct Key {
        std::string code;   // KeyboardEvent.code of the prototype
        gemui::Box box;     // CSS box (fractional), from the top left corner of the drawing
        Kind kind = Kind::Dead;
        std::string label, sub, tip;
        bool nope = false;  // note out of the chip's range (B0 to C9)
        bool cur = false;   // keypad digit: current octave
        int offset = -1;    // CODEMAP offset (key that plays)
        int pad = -1;       // percussion track: pad played (0 to 7)
        int octave = -1;    // keypad digit: chosen octave
    };

    void setKeys(std::vector<Key> keys);
    const std::vector<Key>& keys() const noexcept { return keys_; }
    const Key* key(std::string_view code) const noexcept;
    // Box of a key on the desktop (the drawing follows the moved window).
    gemui::Box keyBox(const Key& k) const noexcept;
    // Key held (manual hold: press, piano, MIDI) and played by the sequencer: halftone.
    void setDown(std::string_view code, bool down);
    void setSeqDown(const std::vector<std::string>& codes);
    void clearDown();
    bool down(std::string_view code) const;

    std::function<void(const Key&)> onPress;    // press (left button) on a playing, octave or keypad key
    std::function<void(const Key&)> onRelease;  // release of a playing key (or pointer moved out of the key)

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override { mouseUp({}); }  // the held note is released
    gemui::Pointer pointerAt(gemui::Point p) const noexcept override;
    std::string tipAt(gemui::Point p) const override;

private:
    const Key* keyAt(gemui::Point p) const noexcept;
    void paintKey(gemui::Painter& p, const Key& k) const;

    std::vector<Key> keys_;
    std::map<std::string, bool, std::less<>> down_, seqDown_;  // keys lit by hand (setDown) and by the sequencer
    std::string pressed_;  // playing key held with the mouse
};

// Legend square (.klg): 11 px, 2 px black line.
class LegendSwatch : public gemui::Widget {
public:
    void setFill(gemui::Color c) { fill_ = c; }
    void paint(gemui::Painter& p) override;

private:
    gemui::Color fill_ = gemui::colors::kWhite;
};

// The Computer Keyboard window, a member of MainWindow (message thread). MainWindow hands it the keys of the computer
// keyboard (notePressed, keyReleased) and the notes played elsewhere (light, lightSeq, lightSeqPads).
class KbdWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 120, kY = 120;  // #kbdWin: left 120, top 120

    explicit KbdWindow(MainWindow& main);

    // Opens the window in front of the others (openKbdWin).
    void show();
    // Octave, layout, held notes: reads everything again (buildTkl, refreshKbdOct).
    void refresh();
    // Open window under Auto: drawing rebuilt when the system's layout changes (D-215).
    void followLayout();

    // Keyboard playing (window open): CODEMAP key pressed (`offset`, already held: ignored) and released.
    void notePressed(int offset);
    void keyReleased(const gemui::KeyEvent& e);
    // Notes held on the keyboard transposed to the current octave (Follow only: retuneHeld).
    void retune();
    // Keys lit by a played note (piano, pads, MIDI: tklNote) and by the sequencer (tklNotes).
    void light(int note, bool on);
    void lightSeq(const std::vector<int>& notes);
    // Percussion track: pads played by the sequencer (D-229).
    void lightSeqPads(const std::vector<int>& pads);

    // Rectangle of a control by the prototype's id (captures, tests): "kbdWin", "kbdOctDn", "kbdOctUp", "kbdOctV",
    // "kbdHeldSel", "kbdRange", "tkl", "tklnum", or the code of a key ("KeyA", "Numpad3"...).
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    const TklDrawing& drawing() const noexcept { return tkl_; }
    const gemui::Label& range() const noexcept { return range_; }

private:
    void build();
    void wire();
    void layout();
    void buildKeys();
    std::string layoutName() const;
    // Key of the drawing that plays `note` at the current octave (tklCodeOf); on a percussion track, the key of its
    // pad.
    std::string codeOf(int note) const;
    // 0: melodic track, 1: Chipdrum, 2: Digidrum (pads, D-229).
    int drumKind() const noexcept;
    // Percussion track: what the pad keys show (kind, empty Digidrum slots), to rebuild the drawing when it changes.
    int padSignature() const;

    MainWindow& m_;
    Rule top_;  // 2 px border-top of .mixbody
    gemui::Label labOct_{"Octave"};
    gemui::Button octDn_{"Oct -"}, octUp_{"Oct +"};
    gemui::Label octV_{"Oct 2"};
    gemui::Label labHeld_{"Held notes"};
    gemui::Button heldSel_{"Keep"};
    gemui::Label range_{"", 12};
    TklDrawing tkl_;
    // Legend: square (white, black, grey, light grey) and 11 px text.
    std::array<LegendSwatch, 4> swatch_;
    std::array<gemui::Label, 4> legend_;
    // CODEMAP key held on the keyboard → played note (kbdHeld) and pad (-1: a note of the melodic keyboard).
    struct Held {
        int note = 0, pad = -1;
    };
    std::map<int, Held> held_;
    // Octave, pads (padSignature) and layout the drawing was built for: refresh() rebuilds it when one changes.
    int builtOct_ = -1, builtPads_ = -1;
    std::string builtLayout_;
};

}  // namespace stym::pluginui
