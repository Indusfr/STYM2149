// STYM2149 — gemui tests: Step Sequencer buttons and keys: ghost notes, overview row, Follow,
// scissors pointer with Alt, digit keys, arrows, Enter and Delete.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Prototype: the #seqGhost, #seqOver and #seqFollow handlers, buildGhostIndex() and the .cell.ghost rules,
// setCutCursor() and the .seqgrid.cut rules, the keydown handler (Digit, ArrowLeft/Right, ArrowUp/Down, Enter,
// Delete/Backspace).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "UiHost.h"
#include "plugincore/Parameters.h"
#include "pluginui/SeqWindow.h"
#include "ymseq/Pattern.h"

using namespace stym;
using namespace stym::ui_testing;
using gemui::Key;
using gemui::Pointer;
using plugin::Global;
using plugin::Track;
using ymengine::Generator;
using Command = UiHost::Command;
using SeqCommand = pluginui::Host::SeqCommand;

namespace {

const pluginui::SeqGrid& grid(pluginui::MainWindow& w) { return w.sequencer().grid(); }

constexpr gemui::Color kGhost = gemui::rgb(0x8C, 0x8C, 0x8C);

// Centre pixel of a note cell, once painted.
gemui::Color cellPixel(pluginui::MainWindow& w, int col, int row) {
    const gemui::Rect r = grid(w).noteRect(col, row);
    REQUIRE(r.w > 0);
    w.desktop().paint();
    return w.desktop().surface().at(r.x + r.w / 2, r.y + r.h / 2);
}

int ghostPixels(pluginui::MainWindow& w) {
    w.desktop().paint();
    const gemui::Rect g = grid(w).bounds();
    int n = 0;
    for (int y = g.y; y < g.bottom(); ++y)
        for (int x = g.x; x < g.right(); ++x) n += w.desktop().surface().at(x, y) == kGhost;
    return n;
}

Pointer pointer(pluginui::MainWindow& w, int col, int row) {
    return grid(w).pointerAt(centre(grid(w).noteRect(col, row)));
}

// Digit of the top row, with the unshifted key as the JUCE editor gives it (Shift+2 types '@' on a US keyboard).
bool digit(pluginui::MainWindow& w, char d, bool shift = false, char32_t typed = 0) {
    gemui::KeyEvent e;
    e.key = Key::Character;
    e.ch = typed ? typed : char32_t(d);
    e.code = d;
    e.mods.shift = shift;
    return w.desktop().keyDown(e);
}

bool numpad(pluginui::MainWindow& w, int n) {
    gemui::KeyEvent e;
    e.key = Key::Character;
    e.ch = char32_t('0' + n);
    e.code = gemui::KeyEvent::kNumpad0 + n;
    return w.desktop().keyDown(e);
}

void publishStep(UiHost& h, pluginui::MainWindow& w, int step, double now) {
    ymengine::Engine::SeqStatus st;
    st.playing = true;
    st.step = step;
    st.stepAbs = step;
    h.publish(st);
    w.tick(now);
}

}  // namespace

TEST_CASE("Ghost notes: the other melodic tracks behind the grid, at the pitch of the focused track",
          "[ui][seq][ghost]") {
    UiHost h;
    ymseq::Lane& b = h.state.seq.cur().edit(1);
    b.pat[4] = 5;  // F3 on track 2 over steps 5 and 6 (TIE on every step of the note)
    b.tie[4] = 1;
    b.tie[5] = 1;
    h.p(2, Track::Generator) = float(int(Generator::Chipdrum));
    h.state.seq.cur().edit(2).pat[8] = 5;  // a drum hit has no pitch here
    h.editor.ghost = false;  // the prototype's start (ghostOn = 0)
    pluginui::MainWindow w(h);
    w.sequencer().show();
    CHECK(ghostPixels(w) == 0);

    click(w, "seqGhost");
    CHECK(h.editor.ghost);
    const auto* btn = dynamic_cast<const gemui::Button*>(w.sequencer().widgetOf("seqGhost"));
    REQUIRE(btn);
    CHECK(btn->on());
    // Row 7 is F3 with the view at C3: a grey band from 2 px under the top to 2 px above the bottom, lined in white.
    CHECK(cellPixel(w, 4, 7) == kGhost);
    CHECK(cellPixel(w, 5, 7) == kGhost);
    CHECK(cellPixel(w, 6, 7) != kGhost);
    CHECK(cellPixel(w, 8, 7) != kGhost);
    const gemui::Rect r = grid(w).noteRect(5, 7);
    const gemui::Surface& surf = w.desktop().surface();
    const int cx = r.x + r.w / 2;
    CHECK(surf.at(cx, r.y + 2) == gemui::colors::kWhite);
    CHECK(surf.at(cx, r.y + 3) == kGhost);
    CHECK(surf.at(cx, r.bottom() - 3) == gemui::colors::kWhite);
    CHECK(surf.at(cx, r.bottom() - 4) == kGhost);
    CHECK(surf.at(r.right() - 2, r.y + r.h / 2) == gemui::colors::kWhite);  // end of the note
    // A note of the focused track hides the ghost under it.
    h.editExtras([](plugin::NonParamState& s) { s.seq.cur().edit(0).pat[4] = 5; });
    w.tick(0);
    CHECK(cellPixel(w, 4, 7) != kGhost);
    CHECK(cellPixel(w, 5, 7) == kGhost);

    // A drum track shows no ghost.
    h.p(Global::FocusTrack) = 3;
    w.tick(0);
    CHECK(ghostPixels(w) == 0);
    h.p(Global::FocusTrack) = 1;
    w.tick(0);
    CHECK(ghostPixels(w) > 0);

    // Off again.
    click(w, "seqGhost");
    CHECK_FALSE(h.editor.ghost);
    CHECK(ghostPixels(w) == 0);

    // 3ch: only the tracks of the chip.
    UiHost h3;
    h3.p(Global::VoiceMode) = 1;
    h3.state.seq.cur().edit(1).pat[4] = 5;
    h3.state.seq.cur().edit(5).pat[10] = 5;
    h3.editor.ghost = true;
    pluginui::MainWindow w3(h3);
    w3.sequencer().show();
    CHECK(cellPixel(w3, 4, 7) == kGhost);
    CHECK(cellPixel(w3, 10, 7) != kGhost);
}

// DAW test, item 10: Ghost is on when the plugin starts, where the prototype started with it off; the
// choice then lasts as long as the plugin (editor session), the editor closed and opened again included.
TEST_CASE("Ghost notes: on by default, with or without the overview; switched off, they stay off",
          "[ui][seq][ghost]") {
    UiHost h;
    CHECK(h.editor.ghost);
    CHECK(pluginui::EditorSession{}.ghost);
    h.state.seq.cur().edit(1).pat[4] = 5;  // F3 on track 2: row 7 with the view at C3
    {
        pluginui::MainWindow w(h);
        w.sequencer().show();
        const auto* btn = dynamic_cast<const gemui::Button*>(w.sequencer().widgetOf("seqGhost"));
        REQUIRE(btn);
        CHECK(btn->on());
        CHECK(cellPixel(w, 4, 7) == kGhost);
        click(w, "seqOver");
        REQUIRE(h.editor.overview);
        CHECK(cellPixel(w, 4, 7) == kGhost);
        click(w, "seqGhost");
        CHECK_FALSE(h.editor.ghost);
        CHECK_FALSE(btn->on());
        CHECK(ghostPixels(w) == 0);
    }
    pluginui::MainWindow w(h);  // editor opened again: same session
    w.sequencer().show();
    const auto* btn = dynamic_cast<const gemui::Button*>(w.sequencer().widgetOf("seqGhost"));
    REQUIRE(btn);
    CHECK_FALSE(btn->on());
    CHECK(ghostPixels(w) == 0);
}

TEST_CASE("Overview button: shows and hides the overview row", "[ui][seq][overview]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    const auto win = w.rectOf("seqWin");
    REQUIRE(win);
    CHECK_FALSE(w.rectOf("overMap"));
    click(w, "seqOver");
    CHECK(h.editor.overview);
    const auto map = w.rectOf("overMap");
    REQUIRE(map);
    CHECK(w.rectOf("seqWin")->h > win->h);
    click(w, "seqOver");
    CHECK_FALSE(h.editor.overview);
    CHECK_FALSE(w.rectOf("overMap"));
    // As high as before; moved up if the overview took it past the bottom of the desktop, it stays there.
    CHECK(w.rectOf("seqWin")->h == win->h);
    CHECK(w.rectOf("seqWin")->y <= win->y);
}

TEST_CASE("Follow: the view pages with the play head when on, stays when off", "[ui][seq][follow]") {
    UiHost h;
    h.state.seq.cur().steps = 128;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    const auto* btn = dynamic_cast<const gemui::Button*>(w.sequencer().widgetOf("seqFollow"));
    REQUIRE(btn);
    CHECK(h.editor.follow);  // followOn = 1
    CHECK(btn->on());

    publishStep(h, w, 70, 10);
    CHECK(h.editor.viewStart == 64);
    publishStep(h, w, 5, 20);
    CHECK(h.editor.viewStart == 0);

    click(w, "seqFollow");
    CHECK_FALSE(h.editor.follow);
    CHECK_FALSE(btn->on());
    publishStep(h, w, 70, 30);
    CHECK(h.editor.viewStart == 0);
    CHECK(grid(w).playStep() == 70);

    click(w, "seqFollow");
    CHECK(h.editor.follow);
    publishStep(h, w, 71, 40);
    CHECK(h.editor.viewStart == 64);

    // A track of its own length: the page of the step in the track.
    h.editExtras([](plugin::NonParamState& s) { s.seq.tracks[0].length = 48; });
    w.tick(45);
    publishStep(h, w, 100, 50);  // 100 mod 48 = 4
    CHECK(h.editor.viewStart == 0);
}

TEST_CASE("Alt held over the grid: scissors on the notes that can be split, not on a drum track",
          "[ui][seq][tools]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    l.pat[4] = 5;  // F3 over steps 5 to 8 (TIE on every step of the note)
    for (int s = 4; s < 8; ++s) l.tie[size_t(s)] = 1;
    l.pat[10] = 5;  // F3 on one step
    h.p(1, Track::Generator) = float(int(Generator::Chipdrum));
    h.state.seq.cur().edit(1).pat[4] = 3;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    h.editor.tool = 2;  // Erase
    gemui::Modifiers alt;
    alt.alt = true;

    CHECK(pointer(w, 5, 7) == Pointer::Hand);
    CHECK(pointer(w, 4, 7) == Pointer::ResizeH);
    w.setModifiers(alt);
    CHECK(pointer(w, 5, 7) == Pointer::Scissors);
    CHECK(pointer(w, 6, 7) == Pointer::Scissors);
    // The edges of a note (resize handles) take the pointer of the grid: Alt wins over resizing, and a one-step
    // note cannot be split.
    CHECK(pointer(w, 4, 7) == Pointer::Crosshair);
    CHECK(pointer(w, 7, 7) == Pointer::Crosshair);
    CHECK(pointer(w, 10, 7) == Pointer::Crosshair);
    CHECK(pointer(w, 8, 7) == Pointer::Hand);
    // Draw and Move keep their pointers (their rules come first in the cascade).
    h.editor.tool = 1;
    CHECK(pointer(w, 5, 7) == Pointer::Pencil);
    h.editor.tool = 0;
    CHECK(pointer(w, 5, 7) == Pointer::Move);
    // Alt released.
    h.editor.tool = 2;
    w.setModifiers({});
    CHECK(pointer(w, 5, 7) == Pointer::Hand);

    // A drum track: no scissors.
    h.p(Global::FocusTrack) = 2;
    w.tick(0);
    w.setModifiers(alt);
    CHECK(pointer(w, 4, 9) == Pointer::Hand);  // fourth instrument
}

TEST_CASE("Digit keys: 1-8 pick a pattern of the bank shown, Shift+1-4 a bank, never the numeric keypad",
          "[ui][seq][keys]") {
    UiHost h;
    pluginui::MainWindow w(h);
    h.takeCommands();

    CHECK(digit(w, '3'));
    CHECK(h.state.seq.current == 2);
    CHECK(h.takeCommands() == std::vector<Command>{{SeqCommand::Select, 2, 0, 0}});
    // Shift+2: bank B, on its last pattern (B1 at first).
    CHECK(digit(w, '2', true, U'@'));
    CHECK(h.state.seq.viewBank == 1);
    CHECK(h.state.seq.current == 8);
    CHECK(digit(w, '5'));
    CHECK(h.state.seq.current == 12);
    // Each bank returns to its last pattern.
    CHECK(digit(w, '1', true, U'!'));
    CHECK(h.state.seq.viewBank == 0);
    CHECK(h.state.seq.current == 2);
    CHECK(digit(w, '2', true, U'@'));
    CHECK(h.state.seq.current == 12);
    CHECK(digit(w, '4', true, U'$'));
    CHECK(h.state.seq.viewBank == 3);
    CHECK(h.state.seq.current == 24);
    CHECK(digit(w, '8'));
    CHECK(h.state.seq.current == 31);
    h.takeCommands();

    // 9, 0 and Shift+5 to Shift+9: used, nothing chosen.
    CHECK(digit(w, '9'));
    CHECK(digit(w, '0'));
    CHECK(digit(w, '5', true, U'%'));
    CHECK(h.state.seq.current == 31);
    CHECK(h.state.seq.viewBank == 3);
    CHECK(h.takeCommands().empty());

    // Numeric keypad: never a pattern; the octave of the keys when the Computer Keyboard window is open.
    CHECK_FALSE(numpad(w, 3));
    CHECK(h.state.seq.current == 31);
    w.keyboardWindow().show();
    CHECK(numpad(w, 3));
    CHECK(w.baseOctave() == 3);
    CHECK(h.state.seq.current == 31);
    CHECK(h.takeCommands().empty());
}

TEST_CASE("Left and Right: the grid cursor when the grid is open, else the channel, modulo the tracks",
          "[ui][seq][keys]") {
    UiHost h;
    h.state.seq.cur().steps = 32;
    pluginui::MainWindow w(h);

    // Grid closed: the channel, around the 16 tracks.
    CHECK(key(w, Key::Right));
    CHECK(h.p(Global::FocusTrack) == 2.0f);
    key(w, Key::Left);
    key(w, Key::Left);
    CHECK(h.p(Global::FocusTrack) == 16.0f);
    key(w, Key::Right);
    CHECK(h.p(Global::FocusTrack) == 1.0f);
    // 3ch: around the four tracks of the chip.
    h.p(Global::VoiceMode) = 1;
    w.tick(0);
    key(w, Key::Left);
    CHECK(h.p(Global::FocusTrack) == 4.0f);
    key(w, Key::Right);
    CHECK(h.p(Global::FocusTrack) == 1.0f);
    h.p(Global::VoiceMode) = 0;
    w.tick(0);

    // Grid open: the step cursor, around the pattern length; the channel stays.
    w.sequencer().show();
    REQUIRE(h.editor.curStep == 0);
    CHECK(key(w, Key::Right));
    CHECK(h.editor.curStep == 1);
    key(w, Key::Left);
    key(w, Key::Left);
    CHECK(h.editor.curStep == 31);
    CHECK(h.p(Global::FocusTrack) == 1.0f);
    // Shift: the channel.
    CHECK(key(w, Key::Right, true));
    CHECK(h.p(Global::FocusTrack) == 2.0f);
    CHECK(h.editor.curStep == 31);
    key(w, Key::Left, true);
    CHECK(h.p(Global::FocusTrack) == 1.0f);

    // A track of its own length: around it.
    h.editExtras([](plugin::NonParamState& s) { s.seq.tracks[0].length = 12; });
    w.tick(0);
    h.editor.curStep = 11;
    key(w, Key::Right);
    CHECK(h.editor.curStep == 0);
    key(w, Key::Left);
    CHECK(h.editor.curStep == 11);

    // Past 64 steps the view follows the cursor.
    h.editExtras([](plugin::NonParamState& s) {
        s.seq.tracks[0].length = 0;
        s.seq.cur().steps = 128;
    });
    w.tick(0);
    h.editor.curStep = 63;
    key(w, Key::Right);
    CHECK(h.editor.curStep == 64);
    CHECK(h.editor.viewStart == 64);
    key(w, Key::Left);
    CHECK(h.editor.viewStart == 0);
}

TEST_CASE("Up and Down in the grid: the cursor row, the view scrolls between 24 and 84, eight drum rows",
          "[ui][seq][keys]") {
    UiHost h;
    h.editor.viewAbsBase = 82;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    REQUIRE(h.editor.curRow == 6);
    // Grid closed: the keys do nothing to the cursor.
    w.sequencer().close();
    key(w, Key::Up);
    CHECK(h.editor.curRow == 6);
    w.sequencer().show();

    CHECK(key(w, Key::Up));
    CHECK(h.editor.curRow == 5);
    for (int i = 0; i < 5; ++i) key(w, Key::Up);
    CHECK(h.editor.curRow == 0);
    CHECK(h.editor.viewAbsBase == 82);
    // Past the top row the view goes up, up to 84.
    key(w, Key::Up);
    CHECK(h.editor.curRow == 0);
    CHECK(h.editor.viewAbsBase == 83);
    key(w, Key::Up);
    key(w, Key::Up);
    CHECK(h.editor.viewAbsBase == 84);
    const auto* tr = dynamic_cast<const gemui::Slider*>(w.sequencer().widgetOf("seqTr"));
    REQUIRE(tr);
    CHECK(tr->value() == 84);
    // Down to the bottom row, then the view goes down, down to 24.
    for (int i = 0; i < 12; ++i) key(w, Key::Down);
    CHECK(h.editor.curRow == 12);
    CHECK(h.editor.viewAbsBase == 84);
    key(w, Key::Down);
    CHECK(h.editor.curRow == 12);
    CHECK(h.editor.viewAbsBase == 83);
    for (int i = 0; i < 70; ++i) key(w, Key::Down);
    CHECK(h.editor.viewAbsBase == 24);
    CHECK(tr->value() == 24);

    // Drum track: the eight instruments (rows 5 to 12), no scrolling.
    h.p(0, Track::Generator) = float(int(Generator::Chipdrum));
    w.tick(0);
    for (int i = 0; i < 10; ++i) key(w, Key::Up);
    CHECK(h.editor.curRow == 5);
    CHECK(h.editor.viewAbsBase == 24);
    for (int i = 0; i < 10; ++i) key(w, Key::Down);
    CHECK(h.editor.curRow == 12);
    CHECK(h.editor.viewAbsBase == 24);
}

TEST_CASE("Enter toggles the note at the cursor with a preview; Shift+Space stays Play / Stop",
          "[ui][seq][keys]") {
    UiHost h;
    h.p(Global::FocusTrack) = 2;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(1000);
    h.take();
    h.takeCommands();
    const auto lane = [&h] { return h.state.seq.cur().lane(1); };

    // Row 6 with the view at C3: F#3.
    CHECK(key(w, Key::Enter));
    CHECK(lane().pat[0] == 6);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 54, 110}});
    CHECK(h.p(1, Track::On) == 0.0f);  // the keyboard entry does not enable the track
    // Another pitch at the cursor replaces the note.
    key(w, Key::Up);
    key(w, Key::Enter);
    CHECK(lane().pat[0] == 7);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 54, 0}, {kNoteOn, 55, 110}});
    // The same pitch removes it, without a preview.
    key(w, Key::Enter);
    CHECK(lane().pat[0] < 0);
    CHECK(h.take().empty());
    CHECK(h.editor.undo.undoSize() == 3);
    CHECK(h.takeCommands().empty());

    // Shift+Space: Play / Stop, the grid is not touched.
    CHECK(w.desktop().keyDown([] {
        gemui::KeyEvent e = charKey(U' ');
        e.mods.shift = true;
        return e;
    }()));
    CHECK(h.takeCommands() == std::vector<Command>{{SeqCommand::Play, 0, 0, 0}});
    CHECK(lane().pat[0] < 0);
    w.desktop().keyDown(charKey(U' '));
    CHECK(h.takeCommands() == std::vector<Command>{{SeqCommand::Stop, 0, 0, 0}});

    // Drum track: the instrument of the row, heard through the sequencer.
    h.p(1, Track::Generator) = float(int(Generator::Chipdrum));
    w.tick(2000);
    h.take();
    h.editor.curRow = 9;
    key(w, Key::Enter);
    CHECK(lane().pat[0] == 3);
    CHECK(h.takeCommands() == std::vector<Command>{{SeqCommand::Preview, 1, 51, 127}});
    key(w, Key::Enter);
    CHECK(lane().pat[0] < 0);
    CHECK(h.takeCommands().empty());
}

TEST_CASE("Delete and Backspace clear the step at the cursor, every row and chord voice, one undo step",
          "[ui][seq][keys]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    l.pat[3] = 5;
    l.oct[3] = 4;
    l.tie[3] = 1;
    l.set(ymseq::Row::Vol, 3, 7);
    l.set(ymseq::Row::Gat, 3, 9);
    l.set(ymseq::Row::Fx, 3, 2);
    l.set(ymseq::Row::Ton, 3, -3);
    l.vp(2, 3) = 9;
    l.vo(2, 3) = 4;
    l.pat[4] = 2;
    l.set(ymseq::Row::Vol, 4, 3);
    const ymseq::Lane before = l;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    h.editor.curStep = 3;

    CHECK(key(w, Key::Delete));
    const ymseq::Lane& r = h.state.seq.cur().lane(0);
    for (int k = 0; k < ymseq::kRowCount; ++k) {
        INFO("row " << k);
        CHECK(r.get(ymseq::Row(k), 3) == ymseq::Lane::emptyValue(ymseq::Row(k)));
    }
    for (int v = 2; v <= ymseq::kPolyMax; ++v) CHECK(h.state.seq.cur().lane(0).vp(v, 3) == -1);
    CHECK(h.state.seq.cur().lane(0).pat[4] == 2);
    CHECK(h.state.seq.cur().lane(0).get(ymseq::Row::Vol, 4) == 3);
    CHECK(h.editor.undo.undoSize() == 1);
    ctrlKey(w, U'z');
    CHECK(h.state.seq.cur().lane(0) == before);

    // Backspace does the same.
    h.editor.curStep = 4;
    CHECK(key(w, Key::Backspace));
    CHECK(h.state.seq.cur().lane(0).pat[4] < 0);
    CHECK(h.state.seq.cur().lane(0).get(ymseq::Row::Vol, 4) == 15);
    CHECK(h.state.seq.cur().lane(0).pat[3] == 5);
}
