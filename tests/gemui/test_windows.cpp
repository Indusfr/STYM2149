// STYM2149 — GEM interface (M5): floating windows (moving, stacking, closing), scrollbars, vertical slider.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Desktop.h"
#include "gemui/Scroll.h"

using namespace stym;
using namespace stym::gemui;

namespace {
void click(Desktop& d, Point p) {
    d.mouseMove(p, {});
    d.mouseDown(p, MouseButton::Left, {});
    d.mouseUp(p, {});
}
}  // namespace

TEST_CASE("Floating window: dragged by its bar, bounded at the top and left, closed by its box", "[gemui]") {
    Desktop d(1272, 944);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    FloatWindow w;
    root.add(w);
    w.setTitle("MIXER");
    w.place({110, 110, 300, 200});
    CHECK_FALSE(w.isOpen());
    int closed = 0;
    w.onClose = [&] { ++closed; };
    w.open();
    REQUIRE(w.isOpen());
    // Bar: inside the frame (6 px), 26 px high; content below.
    CHECK(w.titleBar().bounds() == Rect{116, 116, 288, 26});
    CHECK(w.contentOrigin() == Point{116, 142});

    // Pointers of the prototype: move on the bar, hand on the close box.
    d.mouseMove({200, 125}, {});
    CHECK(d.pointer() == Pointer::Move);
    d.mouseMove({125, 125}, {});
    CHECK(d.pointer() == Pointer::Hand);

    d.mouseMove({200, 125}, {});
    d.mouseDown({200, 125}, MouseButton::Left, {});
    d.mouseDrag({230, 160}, {});
    CHECK(w.bounds() == Rect{140, 145, 300, 200});
    CHECK(w.titleBar().bounds() == Rect{146, 151, 288, 26});
    // Left and top edges never negative (mkWinDrag: Math.max(0, ...)); free to the right and at the bottom.
    d.mouseDrag({10, 5}, {});
    CHECK(w.bounds().x == 0);
    CHECK(w.bounds().y == 0);
    d.mouseDrag({1200, 900}, {});
    CHECK(w.bounds() == Rect{1110, 885, 300, 200});  // grabbed at (90, 15) from the corner
    d.mouseUp({1200, 900}, {});

    click(d, {1110 + 6 + 10, 885 + 6 + 10});  // close box
    CHECK_FALSE(w.isOpen());
    CHECK(closed == 1);
    w.open();  // reopened where it was left
    CHECK(w.bounds() == Rect{1110, 885, 300, 200});
    d.setRoot(nullptr);
}

TEST_CASE("Stacked windows: a press brings the window to the front, what it covers receives nothing", "[gemui]") {
    Desktop d(1272, 944);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    WindowFrame main;
    root.add(main);
    main.setBounds({146, 18, 980, 900});
    Button b("Play");
    main.add(b);
    b.setBounds({300, 300, 60, 24});
    int clicks = 0;
    b.onClick = [&] { ++clicks; };
    FloatWindow mixer, song;
    root.add(mixer);
    root.add(song);
    mixer.place({250, 250, 300, 200});
    song.place({600, 600, 200, 100});
    mixer.open();
    song.open();
    REQUIRE(root.children().back() == &song);

    // The button of the main window is below the mixer: the mixer takes the press and comes to the front.
    click(d, {320, 310});
    CHECK(clicks == 0);
    CHECK(root.children().back() == &mixer);
    // A press in the main window brings it to the front (focusWin('mainWin')): the button is then hit.
    click(d, {700, 100});
    CHECK(root.children().back() == &main);
    click(d, {320, 310});
    CHECK(clicks == 1);

    // Escape: the frontmost open window that accepts it.
    song.open();
    CHECK(FloatWindow::topmost(root, true) == &song);
    song.closesOnEscape = false;
    CHECK(FloatWindow::topmost(root) == &song);
    CHECK(FloatWindow::topmost(root, true) == &mixer);
    mixer.close();
    CHECK(FloatWindow::topmost(root, true) == nullptr);
    d.setRoot(nullptr);
}

namespace {
// The prototype's help window at 1272 × 944 (Chromium 141): .mixbody at (126, 102), 548 × 660.8, scrollbar at the
// edge of the frame; text 1746 px high once the scrollbar is in place (522 px wide).
struct HelpPane {
    Desktop d{1272, 944};
    WindowFrame root;  // white background
    ScrollPane pane;
    Button child{"Deep"};
    int clicks = 0;
    HelpPane(int contentW, int contentH) {
        child.onClick = [this] { ++clicks; };
        root.setBounds(d.bounds());
        d.setRoot(&root);
        root.add(pane);
        pane.setBox({126, 102, 548, 660.797});
        pane.setContentSize(contentW, contentH);
        pane.add(child);
        const Point o = pane.contentOrigin();
        child.setBounds({o.x + 500, o.y + 600, 60, 24});  // overflows below the scrollbar
        d.tick(0);
    }
    ~HelpPane() { d.setRoot(nullptr); }
    Color at(int x, int y) {
        d.invalidateAll();
        d.paint();
        return d.surface().at(x, y);
    }
};
}  // namespace

TEST_CASE("Vertical scrollbar: parts, proportional thumb, drawing of the prototype", "[gemui]") {
    HelpPane h(522, 1746);
    ScrollPane& s = h.pane;
    REQUIRE(s.hasVertical());
    CHECK_FALSE(s.hasHorizontal());
    CHECK(s.viewport() == Rect{126, 102, 522, 661});
    CHECK(s.maxScroll() == Point{0, 1085});
    using P = ScrollPane::Part;
    CHECK(s.partRect(P::Up) == Rect{648, 102, 26, 26});
    CHECK(s.partRect(P::Down) == Rect{648, 737, 26, 26});
    CHECK(s.partRect(P::TrackV) == Rect{648, 128, 26, 609});
    // Length rounded (661 / 1746 × 609 = 230.6), position truncated (400 × 378 / 1085 = 139.4): the rules that give,
    // in Chromium and its 557 px track, the 211 px thumb 127 px from the top of the track.
    s.scrollTo({0, 400});
    CHECK(s.partRect(P::ThumbV) == Rect{648, 267, 26, 231});
    CHECK(h.child.bounds().y == 102 + 600 - 400);

    // Top arrow, read in Chromium: 2 px rules at the top, on the left and at the bottom, none on the right (the
    // frame), 8 × 10 glyph at (658, 110).
    for (int x = 648; x < 674; ++x) {
        CHECK(h.at(x, 102) == colors::kBlack);
        CHECK(h.at(x, 127) == colors::kBlack);
    }
    CHECK(h.at(648, 115) == colors::kBlack);
    CHECK(h.at(650, 115) == colors::kWhite);
    CHECK(h.at(673, 115) == colors::kWhite);
    CHECK(h.at(661, 110) == colors::kBlack);  // ...##...
    CHECK(h.at(660, 110) == colors::kWhite);
    CHECK(h.at(658, 113) == colors::kBlack);  // ##....##
    CHECK(h.at(665, 113) == colors::kBlack);
    // Track: dots (1,1) and (3,3) of a 4 × 4 pattern from its corner (648, 128), left rule.
    CHECK(h.at(653, 129) == colors::kBlack);
    CHECK(h.at(652, 129) == colors::kWhite);
    CHECK(h.at(651, 131) == colors::kBlack);
    CHECK(h.at(649, 130) == colors::kBlack);
    // Thumb: rules at the top, on the left and at the bottom, white inside.
    CHECK(h.at(660, 267) == colors::kBlack);
    CHECK(h.at(660, 268) == colors::kBlack);
    CHECK(h.at(660, 269) == colors::kWhite);
    CHECK(h.at(660, 497) == colors::kBlack);
    CHECK(h.at(653, 300) == colors::kWhite);
    // Bottom arrow: glyph at (658, 745), first row ..####..
    for (int x = 660; x < 664; ++x) CHECK(h.at(x, 745) == colors::kBlack);
    CHECK(h.at(659, 745) == colors::kWhite);
    // The content is clipped: the button that overflows below the scrollbar (626 to 686, top at 302) is neither
    // drawn nor hit there, nor beyond the area.
    CHECK(h.at(630, 302) == colors::kBlack);
    CHECK(h.at(660, 302) == colors::kWhite);
    CHECK(h.at(680, 302) == colors::kWhite);
    h.d.mouseMove({660, 310}, {});
    h.d.mouseDown({660, 310}, MouseButton::Left, {});
    h.d.mouseUp({660, 310}, {});
    CHECK(s.scroll().y == 400);  // press on the thumb: nothing scrolls
    CHECK(h.clicks == 0);
    h.d.mouseMove({640, 310}, {});
    h.d.mouseDown({640, 310}, MouseButton::Left, {});
    h.d.mouseUp({640, 310}, {});
    CHECK(h.clicks == 1);  // visible part of the button
}

TEST_CASE("Horizontal and vertical scrollbars: halftoned corner, left and right arrows", "[gemui]") {
    HelpPane h(916, 1410);
    ScrollPane& s = h.pane;
    REQUIRE(s.hasVertical());
    REQUIRE(s.hasHorizontal());
    CHECK(s.viewport() == Rect{126, 102, 522, 635});
    using P = ScrollPane::Part;
    CHECK(s.partRect(P::Left) == Rect{126, 737, 26, 26});
    CHECK(s.partRect(P::Right) == Rect{622, 737, 26, 26});
    CHECK(s.partRect(P::Corner) == Rect{648, 737, 26, 26});
    CHECK(s.partRect(P::Down) == Rect{648, 711, 26, 26});
    // Pixels read in Chromium: left arrow at (135, 746), right arrow at (631, 746), top rule of the track, halftoned
    // corner from (648, 737).
    CHECK(h.at(137, 746) == colors::kBlack);
    CHECK(h.at(138, 746) == colors::kBlack);
    CHECK(h.at(136, 746) == colors::kWhite);
    CHECK(h.at(635, 746) == colors::kBlack);
    CHECK(h.at(636, 746) == colors::kBlack);
    CHECK(h.at(634, 746) == colors::kWhite);
    CHECK(h.at(646, 750) == colors::kBlack);  // right rule of the right arrow
    CHECK(h.at(127, 762) == colors::kBlack);  // left rule of the left arrow
    CHECK(h.at(140, 762) == colors::kWhite);  // no rule at the bottom (the frame)
    CHECK(h.at(400, 737) == colors::kBlack);
    CHECK(h.at(400, 738) == colors::kBlack);
    CHECK(h.at(499, 740) == colors::kBlack);  // track after the thumb: pattern from (152, 737)
    CHECK(h.at(501, 742) == colors::kBlack);
    CHECK(h.at(501, 740) == colors::kWhite);
    CHECK(h.at(651, 740) == colors::kBlack);  // corner
    CHECK(h.at(653, 742) == colors::kBlack);
    CHECK(h.at(652, 740) == colors::kWhite);
    CHECK(h.at(649, 750) == colors::kBlack);
}

TEST_CASE("Scrollbar: repeated arrows, track by pages, dragged thumb, wheel", "[gemui]") {
    HelpPane h(522, 1746);
    ScrollPane& s = h.pane;
    Desktop& d = h.d;
    // Bottom arrow: 40 px, then again after 250 ms and every 50 ms as long as it stays pressed.
    d.mouseMove({660, 750}, {});
    d.mouseDown({660, 750}, MouseButton::Left, {});
    CHECK(s.scroll().y == 40);
    d.tick(249);
    CHECK(s.scroll().y == 40);
    d.tick(250);
    CHECK(s.scroll().y == 80);
    d.tick(300);
    CHECK(s.scroll().y == 120);
    d.mouseUp({660, 750}, {});
    d.tick(1000);
    CHECK(s.scroll().y == 120);

    // Track below the thumb: 87.5 % of the visible part (578 px), repeated up to the pointer.
    s.scrollTo({0, 0});
    d.mouseDown({660, 700}, MouseButton::Left, {});
    CHECK(s.scroll().y == 578);
    d.tick(1250);
    CHECK(s.scroll().y == 1085);  // limited; the thumb then covers the pointer
    d.mouseUp({660, 700}, {});

    // Thumb dragged by 10 px: 10 × 1085 / 378 px of content.
    s.scrollTo({0, 0});
    const Rect th = s.partRect(ScrollPane::Part::ThumbV);
    d.mouseDown({th.x + 10, th.y + 10}, MouseButton::Left, {});
    d.mouseDrag({th.x + 10, th.y + 20}, {});
    CHECK(s.scroll().y == 29);
    d.mouseUp({th.x + 10, th.y + 20}, {});

    // Wheel over the content; Shift: horizontal (no horizontal scrollbar here, nothing moves).
    d.mouseWheel({300, 300}, 0, 100, {});
    CHECK(s.scroll().y == 129);
    Modifiers shift;
    shift.shift = true;
    d.mouseWheel({300, 300}, 0, 100, shift);
    CHECK(s.scroll() == Point{0, 129});
}

TEST_CASE("Scroll bar on its own: drawn like the pane's, arrows by one, track by a jump, thumb, wheel (D-294)",
          "[gemui]") {
    // The Step Sequencer's notes: 23 to 108, 13 rows shown, an octave per press on the track; 26 × 220 against the
    // right edge of the window.
    Desktop d(1272, 944);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    ScrollBar s;
    root.add(s);
    CHECK(s.vertical());
    s.setRange(23, 108, 1);
    s.setPage(13, 12);
    s.setValue(48);
    s.setBounds({1168, 267, 26, 220});
    std::vector<double> got;
    int starts = 0, ends = 0;
    s.onChange = [&](double v) { got.push_back(v); };
    s.onGestureStart = [&] { ++starts; };
    s.onGestureEnd = [&] { ++ends; };

    // Arrows of 26 px; thumb 13 / 98 of the 168 px track (22 px), (108 - 48) / 85 of the 146 px left above it.
    using P = ScrollBar::Part;
    CHECK(s.partRect(P::Up) == Rect{1168, 267, 26, 26});
    CHECK(s.partRect(P::Down) == Rect{1168, 461, 26, 26});
    CHECK(s.partRect(P::Track) == Rect{1168, 293, 26, 168});
    CHECK(s.partRect(P::Thumb) == Rect{1168, 293 + 103, 26, 22});

    // Drawing: the up arrow's glyph from (1178, 275), first row ...##...; rules at the top and on the left, none on
    // the right (the frame of the window); the track's left rule and dots; the thumb's top rule.
    d.paint();
    const auto at = [&](int x, int y) { return d.surface().at(x, y); };
    CHECK(at(1181, 275) == colors::kBlack);
    CHECK(at(1182, 275) == colors::kBlack);
    CHECK(at(1180, 275) == colors::kWhite);
    CHECK(at(1175, 267) == colors::kBlack);
    CHECK(at(1168, 280) == colors::kBlack);
    CHECK(at(1193, 280) == colors::kWhite);
    CHECK(at(1169, 330) == colors::kBlack);
    CHECK(at(1180, 396) == colors::kBlack);
    CHECK(at(1180, 400) == colors::kWhite);
    // Bottom arrow: glyph from (1178, 469), first row ..####..
    for (int x = 1180; x < 1184; ++x) CHECK(at(x, 469) == colors::kBlack);
    CHECK(at(1179, 469) == colors::kWhite);

    // Up arrow: one semitone up, again after 250 ms and every 50 ms while held; one gesture.
    const Point up{1180, 280};
    d.mouseMove(up, {});
    d.mouseDown(up, MouseButton::Left, {});
    CHECK(s.value() == 49);
    d.tick(249);
    CHECK(s.value() == 49);
    d.tick(250);
    CHECK(s.value() == 50);
    d.tick(300);
    CHECK(s.value() == 51);
    d.mouseUp(up, {});
    d.tick(1000);
    CHECK(s.value() == 51);
    CHECK(got == std::vector<double>{49, 50, 51});
    CHECK(starts == 1);
    CHECK(ends == 1);

    // Down arrow: one down.
    const auto press = [&d](Point p) {
        d.mouseMove(p, {});
        d.mouseDown(p, MouseButton::Left, {});
        d.mouseUp(p, {});
    };
    press({1180, 470});
    CHECK(s.value() == 50);

    // Track below the thumb: an octave down, repeated until the thumb reaches the pointer; above it: an octave up.
    s.setValue(100);
    d.mouseMove({1180, 450}, {});
    d.mouseDown({1180, 450}, MouseButton::Left, {});
    CHECK(s.value() == 88);
    for (int t = 1250; t <= 1600; t += 50) d.tick(t);
    CHECK(s.value() == 28);
    CHECK(s.partRect(P::Thumb).contains({1180, 450}));
    d.mouseUp({1180, 450}, {});
    press({1180, 300});
    CHECK(s.value() == 40);

    // Thumb dragged to the top: 108; to the bottom: 23.
    s.setValue(48);
    const Rect th = s.partRect(P::Thumb);
    d.mouseMove({th.x + 10, th.y + 10}, {});
    d.mouseDown({th.x + 10, th.y + 10}, MouseButton::Left, {});
    d.mouseDrag({th.x + 10, th.y - 200}, {});
    CHECK(s.value() == 108);
    d.mouseDrag({th.x + 10, th.y + 300}, {});
    CHECK(s.value() == 23);
    d.mouseUp({th.x + 10, th.y + 300}, {});

    // Wheel: one semitone per notch, down when the wheel turns towards the user.
    s.setValue(60);
    d.mouseWheel({1180, 380}, 0, 100, {});
    CHECK(s.value() == 59);
    d.mouseWheel({1180, 380}, 0, -100, {});
    CHECK(s.value() == 60);
}

TEST_CASE("Horizontal slider: an arrow at each end, one step per press, repeated while held (D-297)", "[gemui]") {
    // The vibrato rate's range: 1 to 20 by 0.5; 200 × 16.
    Desktop d(1272, 944);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    Slider s;
    root.add(s);
    s.setRange(1, 20, 0.5);
    s.setValue(5);
    s.setBounds({100, 100, 200, 16});
    std::vector<double> got;
    int starts = 0, ends = 0;
    s.onChange = [&](double v) { got.push_back(v); };
    s.onGestureStart = [&] { ++starts; };
    s.onGestureEnd = [&] { ++ends; };

    // 16 px cells; the thumb runs between their inner rules, which its frame covers at the ends.
    CHECK(s.arrows());
    CHECK(s.arrowRect(false) == Rect{100, 100, 16, 16});
    CHECK(s.arrowRect(true) == Rect{284, 100, 16, 16});
    s.setValue(1);
    CHECK(s.thumbRect() == Rect{114, 100, 16, 16});
    s.setValue(20);
    CHECK(s.thumbRect() == Rect{270, 100, 16, 16});
    s.setValue(5);

    // Drawing: the cells' rules; the arrows point outwards, unlike the inward ones of the prototype's horizontal
    // scroll bars: left glyph from (104, 103), first row ....##..; right glyph from (288, 103), first row ..##....
    d.paint();
    const auto at = [&](int x, int y) { return d.surface().at(x, y); };
    CHECK(at(114, 108) == colors::kBlack);
    CHECK(at(115, 108) == colors::kBlack);
    CHECK(at(285, 108) == colors::kBlack);
    CHECK(at(108, 103) == colors::kBlack);
    CHECK(at(109, 103) == colors::kBlack);
    CHECK(at(107, 103) == colors::kWhite);
    CHECK(at(110, 103) == colors::kWhite);
    CHECK(at(290, 103) == colors::kBlack);
    CHECK(at(291, 103) == colors::kBlack);
    CHECK(at(289, 103) == colors::kWhite);
    // Fifth row of the left glyph, its tip: ##.....#
    for (int x : {104, 105, 111}) CHECK(at(x, 107) == colors::kBlack);
    for (int x = 106; x < 111; ++x) CHECK(at(x, 107) == colors::kWhite);

    // Left arrow: one step down, again after 250 ms and every 50 ms while held; nothing while the pointer is off the
    // arrow; one gesture.
    const Point left{106, 108};
    d.mouseMove(left, {});
    d.mouseDown(left, MouseButton::Left, {});
    CHECK(s.value() == 4.5);
    d.tick(249);
    CHECK(s.value() == 4.5);
    d.tick(250);
    CHECK(s.value() == 4);
    d.tick(300);
    CHECK(s.value() == 3.5);
    d.mouseDrag({150, 108}, {});
    d.tick(350);
    CHECK(s.value() == 3.5);
    d.mouseDrag(left, {});
    d.tick(400);
    CHECK(s.value() == 3);
    d.mouseUp(left, {});
    d.tick(1000);
    CHECK(s.value() == 3);
    CHECK(got == std::vector<double>{4.5, 4, 3.5, 3});
    CHECK(starts == 1);
    CHECK(ends == 1);

    // Right arrow: one step up; at the maximum, nothing.
    const auto press = [&d](Point p) {
        d.mouseMove(p, {});
        d.mouseDown(p, MouseButton::Left, {});
        d.mouseUp(p, {});
    };
    press({292, 108});
    CHECK(s.value() == 3.5);
    s.setValue(20);
    got.clear();
    press({292, 108});
    CHECK(s.value() == 20);
    CHECK(got.empty());

    // The track still takes the thumb's centre where it is pressed: halfway, 10.5.
    press({200, 108});
    CHECK(s.value() == 10.5);

    // Without arrows (the Mixer's pans): the track over the whole slider.
    s.setArrows(false);
    CHECK_FALSE(s.arrows());
    CHECK(s.arrowRect(true).empty());
    s.setValue(20);
    CHECK(s.thumbRect() == Rect{284, 100, 16, 16});
    press({101, 108});
    CHECK(s.value() == 1);

    // Vertical: no arrows.
    s.setArrows(true);
    s.setVertical(true);
    CHECK_FALSE(s.arrows());
}

TEST_CASE("Slider: a right click goes to onContext, neither the value nor a gesture (D-300)", "[gemui]") {
    Desktop d(1272, 944);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    Slider s;
    root.add(s);
    s.setVertical(true);
    s.setRange(0, 100, 1);
    s.setValue(75);
    s.setBounds({100, 100, 16, 96});
    int changes = 0, gestures = 0;
    std::vector<Point> at;
    s.onChange = [&](double) { ++changes; };
    s.onGestureStart = [&] { ++gestures; };
    s.onContext = [&](Point p) { at.push_back(p); };
    d.mouseMove({108, 190}, {});
    d.mouseDown({108, 190}, MouseButton::Right, {});
    d.mouseUp({108, 190}, {});
    CHECK(at == std::vector<Point>{{108, 190}});
    CHECK(s.value() == 75);
    CHECK(changes == 0);
    CHECK(gestures == 0);
}

TEST_CASE("Vertical slider: minimum at the bottom, checkerboard rotated with the track", "[gemui]") {
    // The prototype's transposition slider of the Step Sequencer window (seqTr, a scroll bar since D-294): 16 × 220,
    // notes 23 to 108, C3 (48) at the start.
    Desktop d(1272, 944);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    Slider s;
    root.add(s);
    s.setVertical(true);
    s.setRange(23, 108, 1);
    s.setValue(48);
    s.setBounds({1188, 267, 16, 220});
    CHECK(s.thumbRect() == Rect{1188, 267 + 144, 16, 16});  // (220 - 16) × (1 - 25 / 85)

    // 220 px track rotated by -90°: dots at (1, 218) and (3, 220) modulo 4, outside the frame and the thumb.
    d.paint();
    const auto at = [&](int x, int y) { return d.surface().at(1188 + x, 267 + y); };
    CHECK(at(5, 6) == colors::kBlack);
    CHECK(at(5, 4) == colors::kWhite);
    CHECK(at(3, 8) == colors::kBlack);
    CHECK(at(3, 6) == colors::kWhite);
    CHECK(at(7, 200) == colors::kBlack);
    CHECK(at(9, 202) == colors::kBlack);

    // Press at the top: maximum; thumb dragged to the bottom: minimum.
    std::vector<double> values;
    s.onChange = [&](double v) { values.push_back(v); };
    d.mouseMove({1196, 270}, {});
    d.mouseDown({1196, 270}, MouseButton::Left, {});
    CHECK(s.value() == 108);
    CHECK(s.thumbRect().y == 267);
    d.mouseDrag({1196, 900}, {});
    CHECK(s.value() == 23);
    CHECK(s.thumbRect().y == 267 + 204);
    d.mouseUp({1196, 900}, {});
    CHECK(values == std::vector<double>{108, 23});
}

namespace {
class Counter : public Widget {
public:
    int doubles = 0;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDoubleClick(const MouseEvent&) override { ++doubles; }
};
}  // namespace

TEST_CASE("Double click: to the widget of both clicks, if it is still under the pointer", "[gemui]") {
    Desktop d(200, 100);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    Counter a, b;
    root.add(a);
    root.add(b);
    a.setBounds({10, 10, 40, 20});
    b.setBounds({60, 10, 40, 20});
    click(d, {20, 20});
    d.mouseDoubleClick({20, 20}, {});
    CHECK(a.doubles == 1);
    // Pointer moved to another widget: no double click (the browser's dblclick: same element).
    click(d, {20, 20});
    d.mouseDoubleClick({70, 20}, {});
    CHECK(a.doubles == 1);
    CHECK(b.doubles == 0);
    // Released elsewhere than on the pressed widget, then a double click on it: it goes to the widget of the press.
    d.mouseMove({70, 20}, {});
    d.mouseDown({70, 20}, MouseButton::Left, {});
    d.mouseUp({20, 20}, {});
    d.mouseDoubleClick({20, 20}, {});
    CHECK(a.doubles == 1);
    CHECK(b.doubles == 0);
    // Widget removed between the clicks: forgotten.
    click(d, {70, 20});
    root.remove(b);
    d.mouseDoubleClick({70, 20}, {});
    CHECK(b.doubles == 0);
    d.setRoot(nullptr);
}
