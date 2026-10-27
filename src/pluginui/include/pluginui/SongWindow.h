// STYM2149 — pluginui: Song window (#songWin of the prototype), chaining of the patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Transport (#songTbar), Add button, banks and patterns, list of blocks (#songList: 12 columns at the width of their
// longest block, 2 px gap), selection with the mouse (Shift: several blocks), drag and drop, block menu, keyboard
// control (songKey) when the window is in front. Opening the window turns Song mode on, closing it turns it off
// (openSongWin, closeSongWin). Positions and sizes: the prototype's CSS boxes at 100 % (window at 170, 170, as wide
// as its longest content).
//
// The Song lives in the non-automatable state (ymseq::Project::song); the selection, the keyboard cursor and the
// drag in progress live only as long as the plugin window, the clipboard in the session.
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "plugincore/State.h"
#include "plugincore/EngineStatus.h"
#include "pluginui/Session.h"
#include "pluginui/Widgets.h"
#include "ymseq/Song.h"

namespace stym::pluginui {

class MainWindow;
class SongWindow;

// Button that remembers Shift on click (ev.shiftKey of the prototype: Add, blocks).
class ShiftButton : public gemui::Button {
public:
    using gemui::Button::Button;
    bool shift() const noexcept { return shift_; }
    void mouseUp(const gemui::MouseEvent& e) override {
        shift_ = e.mods.shift;
        gemui::Button::mouseUp(e);
    }

protected:
    bool shift_ = false;
};

// Block of the list: button of the prototype, at 45 % while it is dragged (.dragsrc).
class SongChip : public ShiftButton {
public:
    SongChip(SongWindow& w, int index) : w_(w), index_(index) {}
    int index() const noexcept { return index_; }
    double opacity() const noexcept override;
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override;

private:
    SongWindow& w_;
    int index_;
};

// The Song window, a member of MainWindow (message thread). It owns one SongChip per block, created on demand and
// only hidden when the Song gets shorter.
class SongWindow : public gemui::FloatWindow {
public:
    static constexpr int kX = 170, kY = 170, kMinW = 340;  // #songWin: left 170, top 170, min-width 340
    static constexpr int kColumns = 12;                    // #songList: repeat(12, max-content)

    explicit SongWindow(MainWindow& main);

    // Opens the window and turns Song mode on (openSongWin); closing it turns it off (closeSongWin).
    void show();
    // Window opened or closed according to the state's Song mode (state read back by the host), without touching
    // the state.
    void syncMode();
    void refresh();
    void refreshEngineStatus(const plugin::EngineStatus& t);
    // Keyboard control (songKey) when the window is in front: true = key taken.
    bool songKey(const gemui::KeyEvent& e);
    void applyUndo(const UndoEntry& e);  // Song step (applyUndoEntry)

    // Rectangle of a control by the prototype's id (captures, tests): "songAdd", "songHint", "songTrPlay"... for the
    // window's transport, "songChip3" for the fourth block.
    std::optional<gemui::Rect> rectOf(std::string_view id) const;

    // Screen state (captures, tests).
    const ymseq::SongSel& selection() const noexcept { return sel_; }
    int cursor() const noexcept { return cur_; }
    void setSelection(ymseq::SongSel sel, int cursor);

    void paintOver(gemui::Painter& p) override;  // outlines of the selection, the cursor and the drag target

private:
    friend class SongChip;
    friend class MainWindow;
    friend class SeqWindow;

    const ymseq::Song& song() const noexcept;
    EditorSession& ses() const noexcept;
    void edit(const std::function<void(ymseq::Song&)>& fn);  // change of the Song, one undo step before
    void pushUndo();                                          // pushSongUndo

    void build();
    void wire();
    void layout();
    void rebuildChips();
    void refreshTransport();
    void refreshPatterns();

    // Blocks.
    void clickChip(int i, bool shift);
    void chipMenu(int i);
    void tempoPrompt(const ymseq::SongSel& s);
    // Extended Song: special entry after block i, length of Rest and Note-off, entries of a jump.
    void insertSpecial(int i, const ymseq::SongEntry& e);
    void lengthPrompt(const ymseq::SongSel& s);
    void loopPrompt(const ymseq::SongSel& s);
    void jump(int i);  // Enter: jump to this block
    void selectOnly(int i);
    // Drag and drop (pointerdown, pointermove, pointerover, pointerup).
    void dragStart(int i, gemui::Point at);
    void dragMove(gemui::Point at);
    bool dragEnd();  // true if the gesture moved (the click that follows does not select)
    void dragCancel();  // the press ended without a release: no drop
    int chipAt(gemui::Point p) const;

    MainWindow& m_;
    // Transport (#songTbar, .tbar: 11 px buttons), Add, notes.
    gemui::Button play_, rec_, loop_, start_, prev_, next_;
    Rule tbarSep_;
    ShiftButton add_{"Add"};
    Paragraph hint_, keysHint_;
    // Banks and patterns (#songBanks, #songSlots).
    gemui::Label labBank_{"Bank"}, labPattern_{"Pattern"};
    std::array<gemui::Button, 4> banks_;
    std::array<gemui::Button, 8> pats_;
    Rule sep_;
    // List of blocks.
    std::vector<std::unique_ptr<SongChip>> chips_;
    std::vector<std::string> labels_;  // labels shown (rebuilt when they change)

    // Screen state: selection (songSel), keyboard cursor (songCur), played block (songPos of the player, taken at
    // once after a jump) and playback in progress.
    ymseq::SongSel sel_;
    int cur_ = 0;
    int songPos_ = 0;
    bool playing_ = false;
    // Drag (sdFrom, sdDrop, sdMoved, sdX, sdY): block grabbed and block passed over (-1: none), whether the pointer
    // has gone beyond 5 px, and where it was pressed.
    int dragFrom_ = -1, dragDrop_ = -1;
    bool dragMoved_ = false;
    gemui::Point dragAt_{};
};

}  // namespace stym::pluginui
