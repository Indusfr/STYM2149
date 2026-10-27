// STYM2149 — gemui tests: Step Sequencer grid behaviours: preview of a written note, first note arming the
// track, horizontal scroll above 64 steps, Copy row / Paste row, overview map, the
// cells below B0 (PLAY_LO) and the greyed rows of a scale under Snap.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Prototype: previewNote(), armChan(), the #seqScroll input handler, the contextmenu handler of the row labels
// (rowClip), refreshOverview() and the #overMap pointer handlers, PLAY_LO and the .cell.nope class.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "UiHost.h"
#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "pluginui/IconData.h"
#include "plugincore/Parameters.h"
#include "pluginui/SeqWindow.h"
#include "ymseq/Pattern.h"
#include "ymseq/Tables.h"

using namespace stym;
using namespace stym::ui_testing;
using pluginui::SeqLane;
using plugin::Global;
using plugin::Track;
using Command = UiHost::Command;
using SeqCommand = pluginui::Host::SeqCommand;

namespace {

const pluginui::SeqGrid& grid(pluginui::MainWindow& w) { return w.sequencer().grid(); }

// Note cell of the grid (column shown, visual row 0 at the top).
gemui::Point cell(pluginui::MainWindow& w, int col, int row) {
    const gemui::Rect r = grid(w).noteRect(col, row);
    REQUIRE(r.w > 0);
    return centre(r);
}

// Label of a control row, in the label column of the grid.
gemui::Point rowLabel(pluginui::MainWindow& w, SeqLane lane) {
    const gemui::Rect r = grid(w).laneRect(0, lane);
    REQUIRE(r.w > 0);
    const gemui::Rect g = grid(w).bounds();
    return {g.x + pluginui::SeqGrid::kBorder + pluginui::SeqGrid::kLabelW / 2, r.y + r.h / 2};
}

std::string scrollText(pluginui::MainWindow& w) {
    const auto* l = dynamic_cast<const gemui::Label*>(w.sequencer().widgetOf("seqScrollv"));
    REQUIRE(l);
    return l->text();
}

void setSteps(UiHost& h, pluginui::MainWindow& w, int steps) {
    h.editExtras([steps](plugin::NonParamState& s) { s.seq.cur().steps = steps; });
    w.tick(0);
}

// Pixel of the overview canvas (inside its 2 px frame).
gemui::Color mapPixel(pluginui::MainWindow& w, int x, int y) {
    const auto r = w.rectOf("overMap");
    REQUIRE(r);
    return w.desktop().surface().at(r->x + 2 + x, r->y + 2 + y);
}

constexpr gemui::Color kMapOut = gemui::rgb(0xC8, 0xC8, 0xC8), kMapOther = gemui::rgb(0xA8, 0xA8, 0xA8),
                       kMapView = gemui::rgb(0x00, 0x00, 0xA0);

std::vector<int> row(const ymseq::Lane& l, ymseq::Row r) {
    std::vector<int> out;
    for (int s = 0; s < ymseq::kSteps; ++s) out.push_back(l.get(r, s));
    return out;
}

}  // namespace

TEST_CASE("Preview of a written note: velocity 110, cut after 240 ms or by the next one", "[ui][seq][preview]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(1000);
    h.take();

    // C3 (row 12 with the view at C3, track octave 3).
    press(w, cell(w, 4, 12));
    REQUIRE(h.state.seq.cur().lane(0).pat[4] == 0);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 48, 110}});
    w.tick(1239);
    CHECK(h.take().empty());
    w.tick(1240);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 48, 0}});
    w.tick(2000);
    CHECK(h.take().empty());

    // A second note within 240 ms cuts the first one and restarts the delay.
    press(w, cell(w, 6, 10));
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 50, 110}});
    w.tick(2100);
    press(w, cell(w, 8, 8));
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 50, 0}, {kNoteOn, 52, 110}});
    w.tick(2339);
    CHECK(h.take().empty());
    w.tick(2340);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 52, 0}});
    CHECK(h.takeCommands().empty());
}

TEST_CASE("Preview of a drum hit goes through the sequencer path (seqprev), not a note", "[ui][seq][preview]") {
    UiHost h;
    h.p(0, Track::Generator) = float(int(ymengine::Generator::Chipdrum));
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    h.take();
    h.takeCommands();

    press(w, cell(w, 2, 12));  // Kick
    REQUIRE(h.state.seq.cur().lane(0).pat[2] == 0);
    press(w, cell(w, 3, 9));  // fourth instrument
    REQUIRE(h.state.seq.cur().lane(0).pat[3] == 3);
    // The note of the hit is taken from the track octave, as the engine plays the drum steps.
    CHECK(h.takeCommands() ==
          std::vector<Command>{{SeqCommand::Preview, 0, 48, 127}, {SeqCommand::Preview, 0, 51, 127}});
    w.tick(1000);
    CHECK(h.take().empty());
}

TEST_CASE("First note written on a disabled track enables it (armChan)", "[ui][seq]") {
    UiHost h;
    h.p(Global::FocusTrack) = 2;
    REQUIRE(h.p(1, Track::On) == 0.0f);
    pluginui::MainWindow w(h);
    w.sequencer().show();

    press(w, cell(w, 3, 12));
    CHECK(h.state.seq.cur().lane(1).pat[3] == 0);
    CHECK(h.p(1, Track::On) == 1.0f);
    CHECK(h.p(0, Track::On) == 1.0f);  // only the focused track

    // Disabled again: a click on the note removes it and leaves the track off.
    h.p(1, Track::On) = 0;
    w.tick(0);
    press(w, cell(w, 3, 12));
    CHECK(h.state.seq.cur().lane(1).pat[3] < 0);
    CHECK(h.p(1, Track::On) == 0.0f);
    // A new note enables it.
    press(w, cell(w, 5, 10));
    CHECK(h.state.seq.cur().lane(1).pat[5] == 2);
    CHECK(h.p(1, Track::On) == 1.0f);
}

TEST_CASE("Horizontal scroll: shown above 64 steps, a-b readout, the grid follows", "[ui][seq][scroll]") {
    UiHost h;
    h.state.seq.cur().steps = 64;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    CHECK_FALSE(w.rectOf("seqScroll"));
    CHECK_FALSE(w.rectOf("seqScrollv"));

    setSteps(h, w, 128);
    const auto sc = w.rectOf("seqScroll");
    REQUIRE(sc);
    REQUIRE(w.rectOf("seqScrollv"));
    CHECK(scrollText(w) == "1-64");
    // Right end of the track (inside its arrow): the last 64 steps.
    press(w, {sc->right() - 1 - gemui::Slider::kArrow, sc->y + sc->h / 2});
    CHECK(h.editor.viewStart == 64);
    CHECK(scrollText(w) == "65-128");
    // The slider stays put beside its readout, which never covers it.
    CHECK(*w.rectOf("seqScroll") == *sc);
    CHECK(w.rectOf("seqScrollv")->x >= sc->right());
    // Column 0 is now step 65.
    press(w, cell(w, 0, 12));
    CHECK(h.state.seq.cur().lane(0).pat[64] == 0);
    CHECK(h.state.seq.cur().lane(0).pat[0] < 0);
    // Left end: back to the start.
    press(w, {sc->x + gemui::Slider::kArrow, sc->y + sc->h / 2});
    CHECK(h.editor.viewStart == 0);
    CHECK(scrollText(w) == "1-64");
    // Right arrow: one step.
    press(w, {sc->right() - 4, sc->y + sc->h / 2});
    CHECK(h.editor.viewStart == 1);
    CHECK(scrollText(w) == "2-65");
    press(w, {sc->x + 4, sc->y + sc->h / 2});
    CHECK(h.editor.viewStart == 0);

    // 256 steps: up to 193-256; a shorter pattern pulls the view back.
    setSteps(h, w, 256);
    const auto sc2 = w.rectOf("seqScroll");
    REQUIRE(sc2);
    press(w, {sc2->right() - 1 - gemui::Slider::kArrow, sc2->y + sc2->h / 2});
    CHECK(h.editor.viewStart == 192);
    CHECK(scrollText(w) == "193-256");
    setSteps(h, w, 100);
    CHECK(h.editor.viewStart == 36);
    CHECK(scrollText(w) == "37-100");
    setSteps(h, w, 64);
    CHECK_FALSE(w.rectOf("seqScroll"));
    CHECK(h.editor.viewStart == 0);
}

TEST_CASE("Notes scrolled by a bar at the right edge of the window, with arrows, as high as the grid",
          "[ui][seq][scroll]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    const auto win = w.rectOf("seqWin"), bar = w.rectOf("seqTr"), g = w.rectOf("seqgrid");
    REQUIRE(win);
    REQUIRE(bar);
    REQUIRE(g);
    // Against the frame on the right (6 px of border and margin), right of the grid, as high as the whole grid: step
    // numbers, note rows and control rows (ninth DAW test, item 108).
    CHECK(bar->right() == win->right() - 6);
    CHECK(bar->w == gemui::ScrollPane::kBar);
    CHECK(bar->x > g->right());
    CHECK(bar->y == g->y);
    CHECK(bar->bottom() == g->bottom());
    const auto span = grid(w).noteSpan();
    REQUIRE(span);
    CHECK(bar->bottom() > g->y + span->second + 100);  // past the note rows

    const auto* tr = dynamic_cast<const gemui::ScrollBar*>(w.sequencer().widgetOf("seqTr"));
    REQUIRE(tr);
    const int base = h.editor.viewAbsBase;
    CHECK(tr->value() == base);
    // Up arrow: one semitone up; down arrow: one down; the track above the thumb: an octave up.
    press(w, centre(tr->partRect(gemui::ScrollBar::Part::Up)));
    CHECK(h.editor.viewAbsBase == base + 1);
    press(w, centre(tr->partRect(gemui::ScrollBar::Part::Down)));
    press(w, centre(tr->partRect(gemui::ScrollBar::Part::Down)));
    CHECK(h.editor.viewAbsBase == base - 1);
    const gemui::Rect thumb = tr->partRect(gemui::ScrollBar::Part::Thumb);
    press(w, {thumb.x + 10, thumb.y - 4});
    CHECK(h.editor.viewAbsBase == base + 11);
    CHECK(tr->value() == base + 11);
    // The grid's wheel moves the bar with the view.
    w.desktop().mouseWheel(cell(w, 10, 6), 0, 100, {});
    CHECK(h.editor.viewAbsBase == base + 10);
    CHECK(tr->value() == base + 10);

    // Drum track: the bar greyed out, inert.
    h.p(0, Track::Generator) = float(int(ymengine::Generator::Chipdrum));
    w.tick(0);
    CHECK_FALSE(tr->enabled());
}

TEST_CASE("Row labels: Copy row and Paste row across tracks, same row only, one undo step", "[ui][seq][rows]") {
    UiHost h;
    ymseq::Lane& a = h.state.seq.cur().edit(0);
    a.set(ymseq::Row::Vol, 2, 5);
    a.set(ymseq::Row::Vol, 200, 9);
    ymseq::Lane& b = h.state.seq.cur().edit(1);
    b.pat[1] = 4;
    b.set(ymseq::Row::Vol, 7, 3);
    b.set(ymseq::Row::Gat, 7, 6);
    const ymseq::Lane aBefore = a, bBefore = b;
    pluginui::MainWindow w(h);
    w.sequencer().show();

    // Nothing copied yet: Paste row (n/a) does nothing.
    press(w, rowLabel(w, SeqLane::Vol), gemui::MouseButton::Right);
    REQUIRE(menuLabels(w) == std::vector<std::string>{"Copy row", "Paste row (n/a)"});
    pickItem(w, 1);
    CHECK(h.state.seq.cur().lane(0) == aBefore);
    CHECK(h.editor.undo.undoSize() == 0);

    // Copy VOL of track 1; a later change of track 1 is not pasted (the copy is a snapshot).
    press(w, rowLabel(w, SeqLane::Vol), gemui::MouseButton::Right);
    pickItem(w, 0);
    const std::vector<int> copied = row(h.state.seq.cur().lane(0), ymseq::Row::Vol);
    h.editExtras([](plugin::NonParamState& s) { s.seq.cur().edit(0).set(ymseq::Row::Vol, 2, 1); });
    w.tick(0);

    // Track 2: another row says n/a and does nothing; VOL pastes the whole row, the rest of the lane stays.
    h.p(Global::FocusTrack) = 2;
    w.tick(0);
    press(w, rowLabel(w, SeqLane::Gat), gemui::MouseButton::Right);
    REQUIRE(menuLabels(w) == std::vector<std::string>{"Copy row", "Paste row (n/a)"});
    pickItem(w, 1);
    CHECK(h.state.seq.cur().lane(1) == bBefore);
    press(w, rowLabel(w, SeqLane::Vol), gemui::MouseButton::Right);
    REQUIRE(menuLabels(w) == std::vector<std::string>{"Copy row", "Paste row"});
    pickItem(w, 1);
    const ymseq::Lane& pasted = h.state.seq.cur().lane(1);
    CHECK(row(pasted, ymseq::Row::Vol) == copied);
    CHECK(pasted.get(ymseq::Row::Vol, 2) == 5);
    CHECK(pasted.get(ymseq::Row::Gat, 7) == 6);
    CHECK(pasted.pat[1] == 4);
    // One undo step.
    CHECK(h.editor.undo.undoSize() == 1);
    ctrlKey(w, U'z');
    CHECK(h.state.seq.cur().lane(1) == bBefore);
}

TEST_CASE("Overview map: notes, out-of-pattern area, view frame and play head on a 256-step scale",
          "[ui][seq][overview]") {
    UiHost h;
    h.state.seq.cur().steps = 64;
    h.state.seq.cur().edit(0).pat[8] = 5;   // F3 on the focused track
    h.state.seq.cur().edit(1).pat[16] = 7;  // G3 on another track
    pluginui::MainWindow w(h);
    w.sequencer().show();
    CHECK_FALSE(w.rectOf("overMap"));
    CHECK(w.rectOf("seqHint"));
    click(w, "seqOver");
    auto r = w.rectOf("overMap");
    REQUIRE(r);
    // Its line alone under it: no legend (twelfth DAW test, item 151).
    CHECK_FALSE(w.rectOf("seqHint"));
    // Canvas as wide as the grid (38 + 64 × 18 px), 104 px high, in a 2 px frame.
    CHECK(r->w == 1194);
    CHECK(r->h == 108);
    w.desktop().paint();
    // x = step / 256 × 1190; y = 104 − (note − 23) / 97 × 104, two pixels high.
    CHECK(mapPixel(w, 39, 71) == gemui::colors::kBlack);  // step 8, note 53
    CHECK(mapPixel(w, 76, 69) == kMapOther);              // step 16, note 55
    CHECK(mapPixel(w, 150, 30) == gemui::colors::kWhite);
    CHECK(mapPixel(w, 500, 10) == kMapOut);               // after step 64
    CHECK(mapPixel(w, 100, 63) == kMapView);              // top of the view frame (C3 to C4, steps 1-64)
    CHECK(mapPixel(w, 100, 66) == gemui::colors::kWhite);

    // Play head: a green column at the step played.
    ymengine::Engine::SeqStatus st;
    st.playing = true;
    st.step = 20;
    st.stepAbs = 20;
    h.publish(st);
    w.tick(10);
    REQUIRE(grid(w).playStep() == 20);
    w.desktop().paint();
    CHECK(mapPixel(w, 95, 30) == gemui::colors::kGreen);
    st.playing = false;
    st.step = -1;
    h.publish(st);
    w.tick(20);
    w.desktop().paint();
    CHECK(mapPixel(w, 95, 30) == gemui::colors::kWhite);

    // A note written in the grid shows on the map (refreshSeqGrid redraws the overview).
    CHECK(mapPixel(w, 10, 67) == gemui::colors::kWhite);
    press(w, cell(w, 2, 3));  // A3 (57) at step 3
    REQUIRE(h.state.seq.cur().lane(0).pat[2] == 9);
    w.tick(30);
    w.desktop().paint();
    CHECK(mapPixel(w, 10, 67) == gemui::colors::kBlack);

    // Click and drag move the view: steps across, pitch up and down (the view centred on the pointer).
    const int top = w.sequencer().bounds().y;
    setSteps(h, w, 256);  // the scroll row appears above the map (the window may move up)
    const auto r2 = w.rectOf("overMap");
    REQUIRE(r2);
    CHECK(r2->y - w.sequencer().bounds().y > r->y - top);
    r = r2;
    // The view frame follows the scroll bar.
    w.desktop().paint();
    CHECK(mapPixel(w, 1, 70) == kMapView);
    const auto sc = w.rectOf("seqScroll");
    REQUIRE(sc);
    press(w, {sc->right() - 1 - gemui::Slider::kArrow, sc->y + sc->h / 2});
    REQUIRE(h.editor.viewStart == 192);
    w.desktop().paint();
    CHECK(mapPixel(w, 893, 70) == kMapView);  // left side of the frame at step 192
    CHECK(mapPixel(w, 1, 70) == gemui::colors::kWhite);
    w.desktop().mouseMove({r->x + 597, r->y + 27}, {});
    w.desktop().mouseDown({r->x + 597, r->y + 27}, gemui::MouseButton::Left, {});
    CHECK(h.editor.viewStart == 96);       // round(0.5 × 256 − 32)
    CHECK(h.editor.viewAbsBase == 90);     // round(120 − 0.25 × 97) − 6
    CHECK(scrollText(w) == "97-160");
    const auto* tr = dynamic_cast<const gemui::Slider*>(w.sequencer().widgetOf("seqTr"));
    REQUIRE(tr);
    CHECK(tr->value() == 90);
    w.desktop().mouseDrag({r->x + 895, r->y + 81}, {});
    CHECK(h.editor.viewStart == 160);
    CHECK(h.editor.viewAbsBase == 41);
    w.desktop().mouseDrag({r->x + r->w + 40, r->y - 30}, {});  // clamped
    CHECK(h.editor.viewStart == 192);
    CHECK(h.editor.viewAbsBase == 108);
    w.desktop().mouseUp({r->x + r->w + 40, r->y - 30}, {});
    // A 64-step pattern does not scroll.
    setSteps(h, w, 64);
    press(w, {r->x + 900, r->y + 27});
    CHECK(h.editor.viewStart == 0);
}

TEST_CASE("Overview map with a drum track: 26 px band, separator, the pitch of the view kept",
          "[ui][seq][overview]") {
    UiHost h;
    h.p(2, Track::Generator) = float(int(ymengine::Generator::Chipdrum));
    h.state.seq.cur().steps = 256;
    h.state.seq.cur().edit(0).pat[8] = 5;
    h.state.seq.cur().edit(2).pat[24] = 3;
    h.editor.overview = true;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    REQUIRE(w.rectOf("overMap"));
    w.desktop().paint();
    // Melodic area 78 px high: note 53 at y = 78 − 30 / 97 × 78.
    CHECK(mapPixel(w, 39, 53) == gemui::colors::kBlack);
    CHECK(mapPixel(w, 300, 78) == gemui::colors::kBlack);  // separator
    CHECK(mapPixel(w, 114, 90) == kMapOther);              // fourth drum, step 24
    CHECK(mapPixel(w, 114, 70) == gemui::colors::kWhite);

    // Drum track focused: its hit in black; a click moves the steps only.
    h.p(Global::FocusTrack) = 3;
    w.tick(0);
    w.desktop().paint();
    CHECK(mapPixel(w, 114, 90) == gemui::colors::kBlack);
    const int base = h.editor.viewAbsBase;
    const auto r = w.rectOf("overMap");  // the grid of a drum track is shorter: the map moved up
    REQUIRE(r);
    press(w, {r->x + 597, r->y + 10});
    CHECK(h.editor.viewStart == 96);
    CHECK(h.editor.viewAbsBase == base);
}

TEST_CASE("Below B0 (PLAY_LO): cells hatched, not-allowed pointer, a click writes nothing", "[ui][seq]") {
    UiHost h;
    h.p(Global::FocusTrack) = 2;
    h.editor.viewAbsBase = 18;  // the slider stops at B0: only a view kept from elsewhere shows these rows
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    h.take();
    // Rows from the top: note 30 down to 18; row 7 is B0 (23), row 8 is A#0 (22).
    // Column 6 (not on a beat: .qtr comes after .nope in the cascade).
    const gemui::Rect nope = grid(w).noteRect(6, 8), ok = grid(w).noteRect(6, 7);
    REQUIRE(nope.w > 0);
    w.desktop().paint();
    const gemui::Surface& surf = w.desktop().surface();
    auto stripes = [&surf](const gemui::Rect& c) {
        int n = 0;
        for (int y = c.y + 1; y < c.bottom() - 1; ++y)
            for (int x = c.x + 1; x < c.right() - 1; ++x) n += surf.at(x, y) == gemui::colors::kNopeStripe;
        return n;
    };
    CHECK(stripes(nope) > 10);
    CHECK(stripes(grid(w).noteRect(6, 12)) > 10);
    CHECK(stripes(ok) == 0);
    h.editor.tool = 0;  // Move: the pointer tells
    CHECK(grid(w).pointerAt(centre(nope)) == gemui::Pointer::NotAllowed);
    CHECK(grid(w).pointerAt(centre(ok)) == gemui::Pointer::Hand);
    h.editor.tool = 1;

    // Draw: nothing written, nothing heard, the track not enabled.
    press(w, centre(nope));
    for (int s = 0; s < ymseq::kSteps; ++s) REQUIRE(h.state.seq.cur().lane(1).pat[size_t(s)] < 0);
    CHECK(h.take().empty());
    CHECK(h.p(1, Track::On) == 0.0f);
    // B0 itself is written.
    press(w, centre(ok));
    CHECK(h.state.seq.cur().lane(1).pat[6] >= 0);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 23, 110}});
    CHECK(h.p(1, Track::On) == 1.0f);
}

TEST_CASE("Snap: a press on a greyed row writes on the nearest degree, chord voices included", "[ui][seq]") {
    UiHost h;
    h.state.global.scaleMask = ymseq::scaleMask(1);  // Major (Ionian), on C
    h.editor.snap = true;
    h.editor.tool = 1;  // Draw
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    h.take();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    // Rows from the top: C4 down to C3 (row 12); row 11 is C#3, row 6 is F#3, row 9 is D#3.

    // Empty step: the root goes to C.
    press(w, cell(w, 4, 11));
    CHECK(lane().pat[4] == 0);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 48, 110}});
    // Step already holding a note: the added voice goes to F, not to the greyed F#.
    press(w, cell(w, 4, 6));
    CHECK(lane().vp(2, 4) == 5);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 48, 0}, {kNoteOn, 53, 110}});
    // Greyed rows whose degree already sounds at this step: nothing changes (the press does not take a note away).
    press(w, cell(w, 4, 6));
    press(w, cell(w, 4, 11));
    CHECK(lane().pat[4] == 0);
    CHECK(lane().vp(2, 4) == 5);
    CHECK(lane().vp(3, 4) == -1);
    CHECK(h.take().empty());
    // Chord of the input: a major triad pressed on D# stands on D and stays in the scale (D, F, A).
    h.editor.chord = 0;
    press(w, cell(w, 8, 9));
    CHECK(lane().pat[8] == 2);
    CHECK(lane().vp(2, 8) == 5);
    CHECK(lane().vp(3, 8) == 9);
    for (int s = 0; s < 16; ++s)
        for (int v = 1; v <= ymseq::kPolyMax; ++v)
            if (lane().vp(v, s) >= 0) {
                INFO("step " << s << " voice " << v);
                CHECK(ymseq::inScale(lane().vp(v, s), 1, 0));
            }
}

// ---------------------------------------------------------------- piano roll editing

namespace {

// Press at the first point, drag through the others, release at the last one.
void dragPath(pluginui::MainWindow& w, const std::vector<gemui::Point>& path,
              gemui::MouseButton b = gemui::MouseButton::Left, gemui::Modifiers m = {}) {
    REQUIRE_FALSE(path.empty());
    w.desktop().mouseMove(path.front(), m);
    w.desktop().mouseDown(path.front(), b, m);
    for (size_t i = 1; i < path.size(); ++i) w.desktop().mouseDrag(path[i], m);
    w.desktop().mouseUp(path.back(), m);
}

gemui::Modifiers shiftMods() {
    gemui::Modifiers m;
    m.shift = true;
    return m;
}

// Root written straight into the state: `pat` semitones above C of the track octave at step s, held len steps.
void putNote(ymseq::Lane& l, int s, int pat, int len = 1) {
    l.pat[size_t(s)] = int16_t(pat);
    l.oct[size_t(s)] = 0;
    for (int k = 0; k < len; ++k) l.tie[size_t(s + k)] = len > 1 ? 1 : 0;
}

// What the editor reads in a track (track octave 3, 16 steps).
ymseq::EditContext readContext(int length = 16) {
    ymseq::EditContext ctx;
    ctx.length = length;
    return ctx;
}
int noteLen(const ymseq::Lane& l, int s) {
    ymseq::Lane copy = l;
    return ymseq::LaneEditor(copy, readContext()).noteLenAt(s);
}
int noteStartAt(const ymseq::Lane& l, int s) {
    ymseq::Lane copy = l;
    return ymseq::LaneEditor(copy, readContext()).noteStart(s);
}
// Voice (1 = root) that starts at step s on this row, 0 if none.
int voiceOn(const ymseq::Lane& l, int s, int row) {
    ymseq::Lane copy = l;
    return ymseq::LaneEditor(copy, readContext()).voiceAt(s, row);
}

}  // namespace

// View at C3: row 12 is C3, row 10 D3, row 8 E3, row 7 F3, row 5 G3, row 3 A3.

TEST_CASE("Piano roll: a click removes a note even when another note follows it at once", "[ui][seq][pianoroll]") {
    UiHost h;
    h.editor.figure = 4;  // 1/8: two steps at 1/16
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    press(w, cell(w, 0, 12));  // C3, steps 1-2
    press(w, cell(w, 2, 10));  // D3, steps 3-4
    REQUIRE(lane().pat[0] == 0);
    REQUIRE(lane().pat[2] == 2);
    REQUIRE(noteLen(lane(), 0) == 2);
    REQUIRE(noteLen(lane(), 2) == 2);
    // The first note has the current length: a click removes it, and the D3 keeps its two steps.
    press(w, cell(w, 0, 12));
    CHECK(lane().pat[0] < 0);
    CHECK(noteStartAt(lane(), 1) < 0);
    CHECK(lane().pat[2] == 2);
    CHECK(noteLen(lane(), 2) == 2);
}

TEST_CASE("Note length: the values that are not a whole number of steps are greyed, Step written instead",
          "[ui][seq][pianoroll]") {
    UiHost h;
    h.state.global.resolution = 3;  // 1/32
    h.editor.figure = 10;           // 1/16.: three steps at 1/32
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    const auto* fig = dynamic_cast<const gemui::Button*>(w.sequencer().widgetOf("seqFig"));
    REQUIRE(fig);
    CHECK(fig->icon() == &pluginui::icons::kFigures[10]);
    click(w, "seqFig");
    REQUIRE(topMenu(w));
    CHECK(topMenu(w)->checked() == 10);
    CHECK(!topMenu(w)->items()[12].disabled);
    CHECK(topMenu(w)->items()[8].disabled);  // 1/64: half a step of 1/32
    w.desktop().closePopups();

    // At 1/16 the dotted sixteenth lasts a step and a half: greyed with the other values that do not fit, their
    // length unrounded; the Note button and the check mark go to Step, and a note is written one step long.
    h.editExtras([](plugin::NonParamState& s) { s.global.resolution = 2; });
    w.tick(0);
    CHECK(fig->icon() == &pluginui::icons::kFigures[5]);
    click(w, "seqFig");
    const std::vector<std::string> l = menuLabels(w);
    REQUIRE(l.size() == 18);
    CHECK(l[6] == "1/16 1 step");
    CHECK(l[7] == "1/32 0.5 step");
    CHECK(l[8] == "1/64 0.25 step");
    CHECK(l[11] == "1/8. 3 steps");
    CHECK(l[12] == "1/16. 1.5 steps");
    CHECK(l[14] == "1/4T 2.67 steps");
    CHECK(l[16] == "1/16T 0.67 step");
    CHECK(l[17] == "1/32T 0.33 step");
    const std::vector<gemui::MenuItem>& it = topMenu(w)->items();
    for (size_t k : {size_t(0), size_t(2), size_t(6), size_t(10), size_t(11)}) CHECK(!it[k].disabled);
    for (size_t k : {size_t(7), size_t(8), size_t(12), size_t(14), size_t(15), size_t(16), size_t(17)})
        CHECK(it[k].disabled);
    CHECK(it[7].why == "Shorter than one step of 1/16");
    CHECK(it[12].why == "Lasts 1.5 steps of 1/16: not a whole number");
    CHECK(topMenu(w)->checked() == 0);
    pickItem(w, 8);  // a greyed value cannot be chosen
    CHECK(h.editor.figure == 10);
    w.desktop().closePopups();
    press(w, cell(w, 0, 12));
    CHECK(lane().pat[0] == 0);
    CHECK(noteLen(lane(), 0) == 1);  // rounded, the dotted sixteenth would have been two steps

    // The choice is kept: back at 1/32, the dotted sixteenth is written again.
    CHECK(h.editor.figure == 10);
    h.editExtras([](plugin::NonParamState& s) { s.global.resolution = 3; });
    w.tick(0);
    CHECK(fig->icon() == &pluginui::icons::kFigures[10]);
    press(w, cell(w, 4, 10));
    CHECK(noteLen(lane(), 4) == 3);
}

TEST_CASE("Piano roll: with the Move tool, a click on a note leaves it whole", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 4, 5, 4);  // F3, steps 5-8: longer than the figure
    putNote(l, 10, 0);    // C3, step 11: as long as the figure
    h.editor.tool = 0;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    // Clicks on the start, inside and on a one-step note: the cursor goes there, nothing is removed or cut.
    press(w, cell(w, 4, 7));
    press(w, cell(w, 6, 7));
    CHECK(h.editor.curStep == 6);
    press(w, cell(w, 10, 12));
    CHECK(h.editor.curStep == 10);
    CHECK(lane().pat[4] == 5);
    CHECK(noteLen(lane(), 4) == 4);
    CHECK(lane().pat[10] == 0);
    CHECK(h.editor.undo.undoSize() == 0);
    // A drag still moves the note.
    dragPath(w, {cell(w, 5, 7), cell(w, 6, 7)});
    CHECK(lane().pat[5] == 5);
    CHECK(noteLen(lane(), 5) == 4);
    CHECK(lane().pat[4] < 0);
}

TEST_CASE("Piano roll: a note dragged along the bottom or top row keeps its pitch", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 2, 0);  // C3 on the bottom row of the view
    putNote(l, 10, 0);  // C4 on the top row
    l.oct[10] = 4;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    REQUIRE(h.editor.viewAbsBase == 48);
    SECTION("bottom row") {
        dragPath(w, {cell(w, 2, 12), cell(w, 3, 12), cell(w, 4, 12), cell(w, 5, 12)});
        CHECK(lane().pat[5] == 0);
        CHECK(lane().oct[5] == 0);
    }
    SECTION("top row") {
        dragPath(w, {cell(w, 10, 0), cell(w, 11, 0), cell(w, 12, 0)});
        CHECK(lane().pat[12] == 0);
        CHECK(lane().oct[12] == 4);
    }
    CHECK(h.editor.viewAbsBase == 48);
}

TEST_CASE("Piano roll: a long note dragged left by less than its length moves", "[ui][seq][pianoroll]") {
    UiHost h;
    putNote(h.state.seq.cur().edit(0), 4, 5, 4);  // F3, steps 5-8
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    // Grabbed on its second step, dragged two steps to the left.
    dragPath(w, {cell(w, 5, 7), cell(w, 4, 7), cell(w, 3, 7)});
    CHECK(lane().pat[2] == 5);
    CHECK(noteLen(lane(), 2) == 4);
    CHECK(lane().pat[4] < 0);
    CHECK(noteStartAt(lane(), 6) < 0);
}

TEST_CASE("Piano roll: dragging a note across a held note leaves that note whole", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 0, 0, 4);  // C3, steps 1-4
    putNote(l, 6, 7);     // G3, step 7
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    // The G3 goes left over the held C3, then right to step 10.
    std::vector<gemui::Point> path;
    for (int c = 6; c >= 1; --c) path.push_back(cell(w, c, 5));
    for (int c = 2; c <= 9; ++c) path.push_back(cell(w, c, 5));
    dragPath(w, path);
    CHECK(lane().pat[9] == 7);
    CHECK(lane().pat[0] == 0);
    CHECK(noteLen(lane(), 0) == 4);
}

TEST_CASE("Piano roll: a moved note keeps its accent and its slide", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 2, 5);  // F3
    l.vol[2] = 6;
    l.slide[2] = 1;
    putNote(l, 8, 4);  // the E3 that the slide goes to
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    dragPath(w, {cell(w, 2, 7), cell(w, 3, 7), cell(w, 4, 7)});
    REQUIRE(lane().pat[4] == 5);
    CHECK(lane().vol[4] == 6);
    CHECK(lane().slide[4] == 1);
}

TEST_CASE("Piano roll: right click on a selection box opens the box menu", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 2, 0);
    putNote(l, 3, 2);
    pluginui::MainWindow w(h);
    w.sequencer().show();

    SECTION("box drawn with the right button") {
        dragPath(w, {cell(w, 1, 12), cell(w, 2, 11), cell(w, 4, 8)}, gemui::MouseButton::Right);
    }
    SECTION("box drawn with Shift") {
        dragPath(w, {cell(w, 1, 12), cell(w, 2, 11), cell(w, 4, 8)}, gemui::MouseButton::Left, shiftMods());
    }
    SECTION("box drawn with the Select tool") {
        h.editor.tool = 5;
        dragPath(w, {cell(w, 1, 12), cell(w, 2, 11), cell(w, 4, 8)});
    }
    REQUIRE(grid(w).hasRect());
    REQUIRE_FALSE(topMenu(w));
    press(w, cell(w, 3, 10), gemui::MouseButton::Right);  // on the D3, inside the box
    REQUIRE(topMenu(w));
    CHECK(menuLabels(w).front() == "Copy (2 notes)");
    CHECK(grid(w).hasRect());
}

TEST_CASE("Piano roll: Delete on a selection box removes the chord voices inside it", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 2, 0);  // C major triad at step 3
    l.vp(2, 2) = 4;
    l.vdur[0][2] = 1;
    l.vp(3, 2) = 7;
    l.vdur[1][2] = 1;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    dragPath(w, {cell(w, 1, 12), cell(w, 3, 5)}, gemui::MouseButton::Left, shiftMods());  // steps 2-4, C3 to G3
    REQUIRE(grid(w).hasRect());
    key(w, gemui::Key::Delete);
    CHECK(lane().pat[2] < 0);
    CHECK(lane().vp(2, 2) < 0);
    CHECK(lane().vp(3, 2) < 0);
}

TEST_CASE("Piano roll: moving a selection box does not erase the notes it passes over", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 0, 0, 4);  // C3, steps 1-4
    putNote(l, 5, 7);     // G3, step 6, outside the box
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    dragPath(w, {cell(w, 1, 11), cell(w, 0, 12)}, gemui::MouseButton::Left, shiftMods());  // the C3 alone
    REQUIRE(grid(w).hasRect());
    key(w, gemui::Key::Right);
    key(w, gemui::Key::Right);
    CHECK(voiceOn(lane(), 5, 7) != 0);  // the G3 still sounds at step 6
}

TEST_CASE("Piano roll: Enter on a held note right after another one does not lengthen the first",
          "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 0, 0, 2);  // C3, steps 1-2
    putNote(l, 2, 2, 2);  // D3, steps 3-4
    h.editor.curStep = 2;
    h.editor.curRow = 10;  // cursor on the D3
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    key(w, gemui::Key::Enter);  // removes the D3
    CHECK(lane().pat[2] < 0);
    CHECK(noteLen(lane(), 0) == 2);
    CHECK(noteStartAt(lane(), 3) < 0);
}

TEST_CASE("Piano roll: Enter on a drum track writes only one of its eight instruments", "[ui][seq][pianoroll]") {
    UiHost h;
    h.p(0, Track::Generator) = float(int(ymengine::Generator::Chipdrum));
    h.editor.curRow = 0;  // left at the top by a melodic track: no instrument there
    pluginui::MainWindow w(h);
    w.sequencer().show();
    key(w, gemui::Key::Enter);
    for (int s = 0; s < ymseq::kSteps; ++s) {
        INFO("step " << s);
        CHECK(h.state.seq.cur().lane(0).pat[size_t(s)] < 8);
    }
}

TEST_CASE("Piano roll: a dragged chord voice is heard at its new pitch", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 4, 0);  // C3 with an E3 chord voice
    l.vp(2, 4) = 4;
    l.vdur[0][4] = 1;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    h.take();
    dragPath(w, {cell(w, 4, 8), cell(w, 4, 5)});  // E3 to G3
    REQUIRE(h.state.seq.cur().lane(0).vp(2, 4) == 7);
    const std::vector<Midi> heard = h.take();
    REQUIRE_FALSE(heard.empty());
    CHECK(heard.back() == Midi{kNoteOn, 55, 110});
}

TEST_CASE("Piano roll: Paste on a drum track does not write a melodic note there", "[ui][seq][pianoroll]") {
    UiHost h;
    h.p(1, Track::Generator) = float(int(ymengine::Generator::Chipdrum));
    putNote(h.state.seq.cur().edit(0), 0, 0, 4);  // C3, steps 1-4 on track 1
    h.editor.curStep = 0;
    h.editor.curRow = 12;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    REQUIRE(ctrlKey(w, U'c'));
    REQUIRE(h.editor.noteClip);
    h.p(Global::FocusTrack) = 2;  // the Chipdrum track
    w.tick(0);
    h.editor.curStep = 8;
    w.openMenu(2);
    const std::vector<std::string> items = menuLabels(w);
    const auto at = std::find(items.begin(), items.end(), "Paste");
    REQUIRE(at != items.end());
    pickItem(w, int(at - items.begin()));
    const ymseq::Lane& d = h.state.seq.cur().lane(1);
    for (int s = 0; s < ymseq::kSteps; ++s) {
        INFO("step " << s);
        CHECK(d.pat[size_t(s)] < 0);
        CHECK(d.tie[size_t(s)] == 0);  // HLD
    }
}

TEST_CASE("Piano roll: Redo survives a click that changes nothing", "[ui][seq][pianoroll]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    press(w, cell(w, 4, 12));
    REQUIRE(lane().pat[4] == 0);
    ctrlKey(w, U'z');
    REQUIRE(lane().pat[4] < 0);

    SECTION("Shift+click on an empty cell") {
        w.desktop().mouseMove(cell(w, 8, 3), shiftMods());
        w.desktop().mouseDown(cell(w, 8, 3), gemui::MouseButton::Left, shiftMods());
        w.desktop().mouseUp(cell(w, 8, 3), shiftMods());
    }
    SECTION("right click on an empty cell, menu closed") {
        press(w, cell(w, 8, 3), gemui::MouseButton::Right);
        key(w, gemui::Key::Escape);
    }
    ctrlKey(w, U'y');
    CHECK(lane().pat[4] == 0);
}

TEST_CASE("Piano roll: Redo survives a drag of a note edge that the lane refuses", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 0, 5, 4);  // F3, steps 1-4
    putNote(l, 4, 7, 2);  // G3, steps 5-6, right after it
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    press(w, cell(w, 10, 12));
    REQUIRE(lane().pat[10] == 0);
    ctrlKey(w, U'z');
    REQUIRE(lane().pat[10] < 0);

    // The start of the G3 pulled left: the F3 stops it, the lane does not change and Redo stays.
    const gemui::Rect g = grid(w).noteRect(4, 5);
    dragPath(w, {{g.x + 2, g.y + g.h / 2}, cell(w, 3, 5), cell(w, 2, 5)});
    REQUIRE(lane().pat[4] == 7);
    REQUIRE(noteLen(lane(), 0) == 4);
    ctrlKey(w, U'y');
    CHECK(lane().pat[10] == 0);
}

TEST_CASE("Piano roll: the note selection of one track does not edit another track", "[ui][seq][pianoroll]") {
    UiHost h;
    putNote(h.state.seq.cur().edit(0), 2, 0);  // track 1: C3 at step 3
    ymseq::Lane& b = h.state.seq.cur().edit(1);
    putNote(b, 2, 7);  // track 2: G3 at step 3 and A3 at step 7
    putNote(b, 6, 9);
    pluginui::MainWindow w(h);
    w.sequencer().show();
    // Shift+click picks the C3 of track 1.
    w.desktop().mouseMove(cell(w, 2, 12), shiftMods());
    w.desktop().mouseDown(cell(w, 2, 12), gemui::MouseButton::Left, shiftMods());
    w.desktop().mouseUp(cell(w, 2, 12), shiftMods());
    REQUIRE(grid(w).noteSel().size() == 1);
    // Track 2: nothing is selected there, +1 transposes the whole track.
    h.p(Global::FocusTrack) = 2;
    w.tick(0);
    click(w, "trUp");
    const ymseq::Lane& l2 = h.state.seq.cur().lane(1);
    CHECK(l2.pat[2] == 8);
    CHECK(l2.pat[6] == 10);
    CHECK(h.state.seq.cur().lane(0).pat[2] == 0);
}

TEST_CASE("Piano roll: a held Digidrum hit keeps its length when dragged", "[ui][seq][pianoroll]") {
    UiHost h;
    h.p(0, Track::Generator) = float(int(ymengine::Generator::Digidrum));
    putNote(h.state.seq.cur().edit(0), 2, 0, 3);  // slot 1, steps 3-5
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    dragPath(w, {cell(w, 2, 12), cell(w, 5, 12), cell(w, 8, 12)});
    REQUIRE(lane().pat[8] == 0);
    CHECK(lane().pat[2] < 0);
    CHECK(noteLen(lane(), 8) == 3);
    for (int s = 2; s < 5; ++s) {
        INFO("step " << s);
        CHECK(lane().tie[size_t(s)] == 0);
    }
}

TEST_CASE("Piano roll: a note dragged up or down in the last column stays on its step", "[ui][seq][pianoroll]") {
    UiHost h;
    h.state.seq.cur().steps = 128;
    putNote(h.state.seq.cur().edit(0), 63, 5);  // F3 at step 64, last column of the view
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    REQUIRE(h.editor.viewStart == 0);
    dragPath(w, {cell(w, 63, 7), cell(w, 63, 6), cell(w, 63, 5)});  // F3 to G3
    CHECK(lane().pat[63] == 7);
    CHECK(h.editor.viewStart == 0);
}

TEST_CASE("Piano roll: dragging the start of a note over a held note leaves that note whole", "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 0, 5, 4);  // F3, steps 1-4
    putNote(l, 6, 7, 2);  // G3, steps 7-8
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    const gemui::Rect g = grid(w).noteRect(6, 5);
    std::vector<gemui::Point> path = {{g.x + 2, g.y + g.h / 2}};  // left edge of the G3
    for (int c : {5, 4, 3, 2, 3, 4, 5}) path.push_back(cell(w, c, 5));
    dragPath(w, path);
    CHECK(noteLen(lane(), 0) == 4);
    CHECK(lane().pat[5] == 7);
    CHECK(noteLen(lane(), 5) == 3);
}

TEST_CASE("Piano roll: dragging up or down inside one VOL cell follows the pointer", "[ui][seq][pianoroll]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    const gemui::Rect r = grid(w).laneRect(3, SeqLane::Vol);
    REQUIRE(r.w > 0);
    dragPath(w, {{r.x + 8, r.bottom() - 2}, {r.x + 8, r.y + r.h / 2}, {r.x + 8, r.y + 1}});
    CHECK(h.state.seq.cur().lane(0).vol[3] == 15);
}

TEST_CASE("Piano roll: in 3 channels, lengthening a note does not turn the next note into a silent voice",
          "[ui][seq][pianoroll]") {
    UiHost h;
    h.p(Global::VoiceMode) = 1;  // Authentic 3ch: chord voices are not played
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 0, 0, 2);  // C3, steps 1-2
    putNote(l, 2, 4);     // E3, step 3
    h.editor.curStep = 0;
    h.editor.curRow = 12;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    REQUIRE(w.desktop().keyDown(charKey(U']')));
    const ymseq::Lane& out = h.state.seq.cur().lane(0);
    CHECK(out.pat[2] == 4);  // the E3 is still a root note, the one note 3 channels play
}

TEST_CASE("Piano roll: a press on a step that has no free voice writes nothing, plays nothing",
          "[ui][seq][pianoroll]") {
    UiHost h;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 2, 9, 2);  // A3, steps 3-4
    const int rows[] = {0, 2, 4, 5, 7};  // step 5: five chord voices, root removed
    for (int v = 2; v <= ymseq::kPolyMax; ++v) {
        l.vp(v, 4) = int16_t(rows[v - 2]);
        l.vdur[size_t(v - 2)][4] = 1;
    }
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    h.take();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    dragPath(w, {cell(w, 4, 3), cell(w, 5, 3), cell(w, 6, 3)});  // A3 at step 5, dragged to step 7
    CHECK(h.take().empty());
    CHECK(noteLen(lane(), 2) == 2);
    for (int s = 4; s < 8; ++s) {
        INFO("step " << s);
        CHECK(lane().tie[size_t(s)] == 0);
    }
}

TEST_CASE("Piano roll: in 3 channels, the held part of a chord voice is greyed like its start",
          "[ui][seq][pianoroll]") {
    UiHost h;
    h.p(Global::VoiceMode) = 1;
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    putNote(l, 4, 0);  // C3 with an E3 voice held over steps 5-7
    l.vp(2, 4) = 4;
    l.vdur[0][4] = 3;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.desktop().paint();
    auto whites = [&w](int col, int row) {
        const gemui::Rect r = grid(w).noteRect(col, row);
        int n = 0;
        for (int y = r.y + 2; y < r.bottom() - 2; ++y)
            for (int x = r.x + 2; x < r.right() - 2; ++x) n += w.desktop().surface().at(x, y) == gemui::colors::kWhite;
        return n;
    };
    REQUIRE(whites(4, 8) > 20);  // start of the voice: checkerboard
    CHECK(whites(5, 8) > 20);
    CHECK(whites(6, 8) > 20);
}

// ---------------------------------------------------------------- twelfth DAW test

TEST_CASE("Piano roll: the edge of a note stretches it with the Draw tool alone (item 141)",
          "[ui][seq][pianoroll]") {
    for (const int tool : {0, 1}) {
        CAPTURE(tool);
        UiHost h;
        h.editor.tool = tool;
        putNote(h.state.seq.cur().edit(0), 2, 7, 2);  // G (row 5), steps 2 and 3
        pluginui::MainWindow w(h);
        w.sequencer().show();
        w.tick(0);
        auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
        const gemui::Rect last = grid(w).noteRect(3, 5), first = grid(w).noteRect(2, 5);
        const gemui::Point end{last.x + last.w - 2, last.y + last.h / 2};
        const gemui::Point start{first.x + 1, first.y + first.h / 2};
        if (!tool) {  // Move (bug of 24/10/2026): no resize pointer, the edge moves the whole note
            CHECK(grid(w).pointerAt(end) == gemui::Pointer::Move);
            CHECK(grid(w).pointerAt(start) == gemui::Pointer::Move);
            dragPath(w, {end, cell(w, 4, 5), cell(w, 5, 5)});
            CHECK(lane().pat[2] == -1);
            CHECK(lane().pat[4] == 7);
            CHECK(lane().tie[4] == 1);
            CHECK(lane().tie[5] == 1);
            CHECK(lane().tie[6] == 0);
            continue;
        }
        // The outer part of the end steps shows the resize pointer; their middle keeps the pointer of the tool.
        CHECK(grid(w).pointerAt(end) == gemui::Pointer::ResizeH);
        CHECK(grid(w).pointerAt(start) == gemui::Pointer::ResizeH);
        CHECK(grid(w).pointerAt(centre(last)) == gemui::Pointer::Pencil);
        CHECK(grid(w).pointerAt({first.x + first.w - 2, first.y + first.h / 2}) != gemui::Pointer::ResizeH);
        // Its end pulled to step 6.
        dragPath(w, {end, cell(w, 4, 5), cell(w, 6, 5)});
        CHECK(lane().pat[2] == 7);
        for (int s = 2; s <= 6; ++s) CHECK(lane().tie[size_t(s)] == 1);
        CHECK(lane().tie[7] == 0);
        // Its start pulled back to step 0.
        dragPath(w, {start, cell(w, 1, 5), cell(w, 0, 5)});
        CHECK(lane().pat[0] == 7);
        CHECK(lane().pat[2] == -1);
        CHECK(lane().tie[6] == 1);
    }
    // Draw tool, a click on the edge without a drag acts as a click in the middle of the note.
    auto clickAt = [](bool edge) {
        UiHost h;
        h.editor.tool = 1;
        putNote(h.state.seq.cur().edit(0), 2, 7, 2);
        pluginui::MainWindow w(h);
        w.sequencer().show();
        w.tick(0);
        const gemui::Rect last = grid(w).noteRect(3, 5);
        dragPath(w, {edge ? gemui::Point{last.x + last.w - 2, last.y + last.h / 2} : centre(last)});
        return h.state.seq.cur().lane(0);
    };
    const ymseq::Lane a = clickAt(true), b = clickAt(false);
    CHECK(a.pat == b.pat);
    CHECK(a.tie == b.tie);
}

TEST_CASE("Piano roll: with the Draw tool, a note held under the button shows the hand until the release",
          "[ui][seq][pianoroll]") {
    UiHost h;
    h.editor.tool = 1;
    putNote(h.state.seq.cur().edit(0), 2, 7, 2);  // G (row 5), steps 2 and 3
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    const gemui::Point p = centre(grid(w).noteRect(2, 5));
    w.desktop().mouseMove(p, {});
    CHECK(w.desktop().pointer() == gemui::Pointer::Pencil);
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    CHECK(w.desktop().pointer() == gemui::Pointer::Hand);
    w.desktop().mouseDrag(cell(w, 5, 5), {});
    CHECK(w.desktop().pointer() == gemui::Pointer::Hand);
    w.desktop().mouseUp(cell(w, 5, 5), {});
    CHECK(h.state.seq.cur().lane(0).pat[5] == 7);  // moved
    CHECK(w.desktop().pointer() == gemui::Pointer::Pencil);
    // On an empty cell the pencil draws: no hand.
    w.desktop().mouseMove(cell(w, 9, 8), {});
    w.desktop().mouseDown(cell(w, 9, 8), gemui::MouseButton::Left, {});
    CHECK(w.desktop().pointer() == gemui::Pointer::Pencil);
    w.desktop().mouseUp(cell(w, 9, 8), {});
}

TEST_CASE("Piano roll: a drag from a selected note moves the whole selection (item 147)", "[ui][seq][pianoroll]") {
    UiHost h;
    h.editor.tool = 1;  // Draw
    putNote(h.state.seq.cur().edit(0), 2, 7);  // G (row 5)
    putNote(h.state.seq.cur().edit(0), 4, 4);  // E (row 8)
    pluginui::MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };

    // Box drawn with Shift over both notes, then a drag of the G two steps to the right: both go.
    dragPath(w, {cell(w, 1, 4), cell(w, 3, 6), cell(w, 5, 9)}, gemui::MouseButton::Left, shiftMods());
    REQUIRE(grid(w).hasRect());
    dragPath(w, {cell(w, 2, 5), cell(w, 3, 5), cell(w, 4, 5)});
    CHECK(lane().pat[2] == -1);
    CHECK(lane().pat[4] == 7);
    CHECK(lane().pat[6] == 4);
    REQUIRE(grid(w).hasRect());  // the box follows the notes
    CHECK(grid(w).rectBounds().a == 3);
    CHECK(grid(w).rectBounds().b == 7);
    // One row up: both a semitone higher.
    dragPath(w, {cell(w, 4, 5), cell(w, 4, 4)});
    CHECK(lane().pat[4] == 8);
    CHECK(lane().pat[6] == 5);
    // A click without a drag still ends the selection (and the Draw tool takes the note away, as before).
    dragPath(w, {cell(w, 4, 4)});
    CHECK_FALSE(grid(w).hasRect());

    // Picked notes (Shift+click): the same, all or nothing.
    h.editor.tool = 0;  // Move
    putNote(h.state.seq.cur().edit(0), 0, 7);
    ++h.serial;
    w.tick(0);
    dragPath(w, {cell(w, 0, 5)}, gemui::MouseButton::Left, shiftMods());
    dragPath(w, {cell(w, 6, 7)}, gemui::MouseButton::Left, shiftMods());
    REQUIRE(grid(w).noteSel().size() == 2);
    dragPath(w, {cell(w, 6, 7), cell(w, 5, 7)});  // the note on step 0 would leave the pattern: nothing moves
    CHECK(lane().pat[0] == 7);
    CHECK(lane().pat[6] == 5);
    dragPath(w, {cell(w, 6, 7), cell(w, 7, 7), cell(w, 8, 7)});
    CHECK(lane().pat[0] == -1);
    CHECK(lane().pat[2] == 7);
    CHECK(lane().pat[6] == -1);
    CHECK(lane().pat[8] == 5);
    CHECK(grid(w).noteSel() == std::vector<ymseq::SelKey>{{2, 1}, {8, 1}});  // the selection follows
}

TEST_CASE("Piano roll: a press cut short by an alert does nothing, then or at the next press", "[ui][seq][pianoroll]") {
    UiHost h;
    h.editor.figure = 4;  // 1/8: two steps at 1/16
    pluginui::MainWindow w(h);
    w.sequencer().show();
    auto lane = [&h]() -> const ymseq::Lane& { return h.state.seq.cur().lane(0); };
    press(w, cell(w, 0, 12));  // C3, steps 1-2: as long as the figure, a click on it removes it
    REQUIRE(lane().pat[0] == 0);
    const size_t undo0 = h.editor.undo.undoSize();
    // A press on the note, then an alert opens (end of a background export) before the release.
    const gemui::Point p = cell(w, 0, 12);
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    gemui::Widget& box = w.desktop().openModal(std::make_unique<gemui::Widget>());
    w.desktop().mouseUp(p, {});
    w.desktop().closeModal(box);
    CHECK(lane().pat[0] == 0);
    // Shift on an empty cell starts a box: the cut press is not finished there as a click on the note.
    dragPath(w, {cell(w, 6, 10)}, gemui::MouseButton::Left, shiftMods());
    CHECK(lane().pat[0] == 0);
    CHECK(noteLen(lane(), 0) == 2);
    CHECK(h.editor.undo.undoSize() == undo0);
}

TEST_CASE("Piano roll: a press ends when the pattern or the track changes under it", "[ui][seq][pianoroll]") {
    UiHost h;
    h.state.seq.used.set(1);
    h.state.seq.patterns[1].steps = 16;
    putNote(h.state.seq.patterns[0].edit(0), 4, 0);  // A1, track 1: C3 at step 5
    putNote(h.state.seq.patterns[1].edit(0), 4, 4);  // A2, track 1: E3 at step 5
    putNote(h.state.seq.patterns[1].edit(0), 8, 7);  // and G3 at step 9
    putNote(h.state.seq.patterns[0].edit(1), 6, 2);  // A1, track 2: D3 at step 7
    pluginui::MainWindow w(h);
    w.sequencer().show();
    double now = 1000;
    w.tick(now);
    const ymseq::Lane a2 = h.state.seq.patterns[1].lane(0), t2 = h.state.seq.patterns[0].lane(1);
    // The engine plays A2 now (end of A1 in Song playback): read by the next tick.
    auto songMovesOn = [&] {
        ymengine::Engine::SeqStatus s;
        s.playing = true;
        s.pattern = 1;
        s.stepAbs = 16;
        s.songPos = 1;
        s.bpm = 120;
        h.publish(s);
        w.tick(now += 33);
        REQUIRE(h.state.seq.current == 1);
    };
    w.desktop().mouseMove(cell(w, 4, 12), {});
    w.desktop().mouseDown(cell(w, 4, 12), gemui::MouseButton::Left, {});
    SECTION("A drag of A1's note goes on in neither pattern once A2 plays") {
        w.desktop().mouseDrag(cell(w, 6, 12), {});
        songMovesOn();
        w.desktop().mouseDrag(cell(w, 10, 12), {});
        w.desktop().mouseUp(cell(w, 10, 12), {});
        CHECK(h.state.seq.patterns[1].lane(0) == a2);
        CHECK(h.state.seq.patterns[0].lane(0).pat[6] == 0);  // where the drag had taken it in A1
    }
    SECTION("A click on A1's note released once A2 plays changes nothing in A2") {
        songMovesOn();
        w.desktop().mouseUp(cell(w, 4, 12), {});
        CHECK(h.state.seq.patterns[1].lane(0) == a2);
        CHECK(h.state.seq.patterns[0].lane(0).pat[4] == 0);
    }
    SECTION("A drag on track 1 goes on in neither track once the host focuses track 2") {
        w.desktop().mouseDrag(cell(w, 6, 12), {});
        h.p(Global::FocusTrack) = 2;  // automation, or MIDI CC 84
        w.tick(now += 33);
        w.desktop().mouseDrag(cell(w, 10, 12), {});
        w.desktop().mouseUp(cell(w, 10, 12), {});
        CHECK(h.state.seq.patterns[0].lane(1) == t2);
    }
    // The next press is a fresh one.
    press(w, cell(w, 12, 12));
    CHECK(h.state.seq.cur().lane(h.state.seq.current == 1 ? 0 : 1).pat[12] >= 0);
}
