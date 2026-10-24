// STYM2149 — Slide Curve window (M5): opened by the Custom entry of the CRV row, drawing with the mouse, Linear,
// Smooth, Invert, warning for a curve that misses the target note, neither Escape nor undo (D-130), place kept.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using Catch::Approx;
using pluginui::CurveGrid;
using pluginui::MainWindow;
using pluginui::SeqLane;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;

    float param(int i) const override { return values[size_t(i)]; }
    void beginGesture(int) override {}
    void setParam(int i, float v) override { values[size_t(i)] = plugin::clampToSpec(plugin::paramSpec(i), v); }
    void endGesture(int) override {}
    uint64_t extrasVersion() const override { return serial; }
    plugin::NonParamState extras() const override { return state; }
    void editExtras(const std::function<void(plugin::NonParamState&)>& edit) override {
        edit(state);
        ++serial;
    }
    void screenMidi(uint8_t, uint8_t, uint8_t) override {}
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void seqCommand(SeqCommand, int, int, int) override {}
    const plugin::EngineStatus* engineStatus() override { return nullptr; }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return true; }
    std::string midiStatus() const override { return "none"; }
    std::string version() const override { return "test"; }
    void quit() override {}
    bool hasAudioSettings() const override { return false; }
    void openAudioSettings() override {}
    void loadDigidrum(int) override {}
    std::string preference(std::string_view, std::string_view fallback) const override { return std::string(fallback); }
    void setPreference(std::string_view, std::string_view) override {}
    std::string patchLibrary() const override { return patches; }
    void setPatchLibrary(const std::string& json) override { patches = json; }

    std::string patches;

    const std::array<float, CurveGrid::kPoints>& table() const { return state.global.slideTable; }
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void press(MainWindow& w, gemui::Point p) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(p, {});
}

void click(MainWindow& w, const std::string& id) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, centre(*r));
}

// Entry `index` of the curve menu opened below cell `cell`: 13 entries of 20 px and 4 separators of 8 px (before
// entries 4, 7, 9 and 12) inside a 2 px border, 6 px below the cell, or above if it does not fit.
void pickCurve(MainWindow& w, const gemui::Rect& cell, int index) {
    REQUIRE(w.desktop().hasPopups());
    constexpr std::array<int, 4> seps = {4, 7, 9, 12};
    const int h = 4 + 13 * 20 + 4 * 8;
    int top = cell.bottom() + 6;
    if (top + h > MainWindow::kHeight - 4) top = std::max(4, cell.y - h - 2);
    int y = top + 2;
    for (int k = 0; k < index; ++k) y += 20;
    for (int s : seps)
        if (s <= index) y += 8;
    press(w, {cell.x + 20, y + 10});
    CHECK_FALSE(w.desktop().hasPopups());
}

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

// Point of a column of the curve at height `level` (1 at the top, 0 at the bottom).
gemui::Point at(MainWindow& w, int column, double level) {
    const gemui::Rect c = w.slideCurve().grid().columnRect(column);
    return {c.x + 6, c.y + int(std::lround((1 - level) * CurveGrid::kColH))};
}

void setTable(TestHost& h, MainWindow& w, const std::function<float(int)>& f) {
    h.editExtras([&](plugin::NonParamState& s) {
        for (int i = 0; i < CurveGrid::kPoints; ++i) s.global.slideTable[size_t(i)] = f(i);
    });
    w.tick(0);
}

}  // namespace

TEST_CASE("Slide Curve: the Custom entry of the CRV row opens the window; Escape does not close it", "[ui][curve]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.slideCurve().isOpen());
    CHECK_FALSE(w.rectOf("curveWin"));

    w.sequencer().show();
    const gemui::Rect cell = w.sequencer().grid().laneRect(0, SeqLane::Slc);
    REQUIRE(cell.w > 0);
    // Another curve: the window stays closed.
    press(w, centre(cell));
    pickCurve(w, cell, 1);  // Exponential
    CHECK_FALSE(w.slideCurve().isOpen());
    // Custom: the window opens in front, at 180, 160, as wide as the row of the buttons and of the note.
    press(w, centre(cell));
    pickCurve(w, cell, 12);
    REQUIRE(w.slideCurve().isOpen());
    CHECK(gemui::FloatWindow::topmost(w) == &w.slideCurve());
    CHECK(w.rectOf("curveWin") == gemui::Rect{180, 160, 593, 238});
    CHECK(w.rectOf("curveReset") == gemui::Rect{194, 200, 66, 24});
    CHECK(w.rectOf("curveSmooth") == gemui::Rect{268, 200, 66, 24});
    CHECK(w.rectOf("curveInvert") == gemui::Rect{342, 200, 66, 24});
    CHECK(w.rectOf("curvegrid") == gemui::Rect{194, 242, 452, 124});
    CHECK_FALSE(w.rectOf("curveWarn"));  // straight line by default: nothing to report

    // Escape (absent from the CLOSERS) closes the window below, not the curve.
    key(w, gemui::Key::Escape);
    CHECK(w.slideCurve().isOpen());
    CHECK_FALSE(w.sequencer().isOpen());
    // The close box closes it.
    press(w, {198, 178});
    CHECK_FALSE(w.slideCurve().isOpen());
}

TEST_CASE("Slide Curve: drawing with the mouse, without an undo step", "[ui][curve]") {
    TestHost h;
    MainWindow w(h);
    w.slideCurve().show();
    const auto lin = h.table();
    const CurveGrid& g = w.slideCurve().grid();
    CHECK(g.columnRect(0) == gemui::Rect{196, 244, 14, 120});
    CHECK(g.columnRect(31) == gemui::Rect{630, 244, 14, 120});

    // Press then drag: each column hovered takes the height of the pointer.
    w.desktop().mouseMove(at(w, 3, 0.5), {});
    w.desktop().mouseDown(at(w, 3, 0.5), gemui::MouseButton::Left, {});
    CHECK(h.table()[3] == Approx(0.5));
    w.desktop().mouseDrag(at(w, 3, 0.25), {});  // in the same column, downwards too (D-130)
    CHECK(h.table()[3] == Approx(0.25));
    w.desktop().mouseDrag(at(w, 5, 0.75), {});
    CHECK(h.table()[5] == Approx(0.75));
    w.desktop().mouseDrag({at(w, 6, 0).x, 244 + 120 + 1}, {});  // bottom border: nothing
    CHECK(h.table()[6] == lin[6]);
    w.desktop().mouseDrag({700, 300}, {});  // outside the grid: nothing
    w.desktop().mouseDrag(at(w, 7, 1), {});
    CHECK(h.table()[7] == Approx(1));
    w.desktop().mouseUp(at(w, 7, 1), {});
    w.desktop().mouseDrag(at(w, 8, 0.25), {});  // button released: no more drawing
    CHECK(h.table()[8] == lin[8]);

    // A press on the grid's border does not draw, nor does the drag that follows; nor does the right button.
    w.desktop().mouseMove({300, 243}, {});
    w.desktop().mouseDown({300, 243}, gemui::MouseButton::Left, {});
    w.desktop().mouseDrag(at(w, 9, 0.25), {});
    w.desktop().mouseUp(at(w, 9, 0.25), {});
    CHECK(h.table()[9] == lin[9]);
    w.desktop().mouseMove(at(w, 10, 0.25), {});
    w.desktop().mouseDown(at(w, 10, 0.25), gemui::MouseButton::Right, {});
    w.desktop().mouseUp(at(w, 10, 0.25), {});
    CHECK(h.table()[10] == lin[10]);

    CHECK(g.pointerAt(at(w, 4, 0.5)) == gemui::Pointer::Crosshair);
    CHECK(g.pointerAt({300, 243}) == gemui::Pointer::Arrow);
    // Neither the prototype nor gemui make it an undo step (D-130).
    CHECK(h.editor.undo.undoSize() == 0);
}

TEST_CASE("Slide Curve: Linear, Smooth, Invert and warning", "[ui][curve]") {
    TestHost h;
    MainWindow w(h);
    w.slideCurve().show();
    const auto lin = h.table();

    // Smooth: (1, 2, 1) average of the inner points, ends unchanged.
    setTable(h, w, [](int i) { return i < 16 ? 0.0f : 1.0f; });
    click(w, "curveSmooth");
    CHECK(h.table()[0] == 0.0f);
    CHECK(h.table()[15] == Approx(0.25));
    CHECK(h.table()[16] == Approx(0.75));
    CHECK(h.table()[31] == 1.0f);

    // Invert: the whole curve upside down, straight parts included (sixth DAW test, item 65, D-263).
    setTable(h, w, [](int i) { return i < 8 ? float(std::pow(i / 7.0, 2)) * 0.25f : float(i / 31.0); });
    const auto drawn = h.table();
    click(w, "curveInvert");
    for (size_t i = 0; i < drawn.size(); ++i) CHECK(h.table()[i] == Approx(1 - drawn[i]));
    CHECK(h.table()[31] == Approx(0));
    CHECK(w.slideCurve().warning().visible());  // the slide no longer ends on the target note
    click(w, "curveInvert");
    for (size_t i = 0; i < drawn.size(); ++i) CHECK(h.table()[i] == Approx(drawn[i]));

    // Warning: the curve stops more than 2 % away from the target note; the window grows by the line.
    setTable(h, w, [](int i) { return float(0.8 * i / 31.0); });
    CHECK(w.slideCurve().warning().visible());
    CHECK(w.slideCurve().warning().text() ==
          "Ends at 80% of the interval: the slide stops short of the target note.");
    CHECK(w.rectOf("curveWarn") == gemui::Rect{194, 372, 420, 12});
    CHECK(w.rectOf("curveWin") == gemui::Rect{180, 160, 593, 256});
    setTable(h, w, [](int i) { return float(0.985 * i / 31.0); });
    CHECK_FALSE(w.slideCurve().warning().visible());
    setTable(h, w, [](int i) { return float(0.97 * i / 31.0); });
    CHECK(w.slideCurve().warning().text().rfind("Ends at 97%", 0) == 0);
    // The grid follows the state changed elsewhere (preset, undo).
    CHECK(w.slideCurve().grid().table() == h.table());
    CHECK(h.table()[31] == Approx(0.97));

    // Linear: the initial straight line.
    click(w, "curveReset");
    CHECK(h.table() == lin);
    CHECK_FALSE(w.slideCurve().warning().visible());
    CHECK(w.rectOf("curveWin") == gemui::Rect{180, 160, 593, 238});
}

TEST_CASE("Slide Curve: the reopened window gets its place back", "[ui][curve]") {
    TestHost h;
    {
        MainWindow w(h);
        w.slideCurve().show();
        const gemui::Point bar{300, 172};
        w.desktop().mouseMove(bar, {});
        w.desktop().mouseDown(bar, gemui::MouseButton::Left, {});
        w.desktop().mouseDrag({340, 212}, {});
        w.desktop().mouseUp({340, 212}, {});
    }
    MainWindow w(h);
    CHECK(w.slideCurve().isOpen());
    CHECK(w.rectOf("curveWin") == gemui::Rect{220, 200, 593, 238});
}
