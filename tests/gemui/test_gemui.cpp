// STYM2149 — GEM interface: text placement, fallback font metrics, small fonts, tooltips, alerts, menus, PNG.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Png.h"
#include "gemui/Alert.h"
#include "gemui/Controls.h"
#include "gemui/Desktop.h"
#include "gemui/Font.h"
#include "gemui/Menu.h"
#include "gemui/Painter.h"

using namespace stym;
using namespace stym::gemui;

TEST_CASE("Glyph origin at the nearest quarter of a pixel (Skia)", "[gemui]") {
    // Column floor(x + 1/8), quarter floor(4 × frac(x + 1/8)).
    CHECK(glyphPos(10.0).x == 10);
    CHECK(glyphPos(10.0).quarter == 0);
    CHECK(glyphPos(10.125).quarter == 1);
    CHECK(glyphPos(10.375).quarter == 2);
    CHECK(glyphPos(10.5).quarter == 2);
    CHECK(glyphPos(10.625).quarter == 3);
    CHECK(glyphPos(10.875).x == 11);
    CHECK(glyphPos(10.875).quarter == 0);
    // Register cell of the prototype: 14 px at x = 64.57 → column 64, half pixel.
    CHECK(glyphPos(64.57).x == 64);
    CHECK(glyphPos(64.57).quarter == 2);
}

TEST_CASE("Text takes the glyph set of its quarter", "[gemui]") {
    const int index = Font::glyph(U'1');
    for (int q = 0; q < 4; ++q) {
        Surface s(40, 30, colors::kWhite);
        Painter p(s);
        p.textAt("1", 10 + q / 4.0, 20, 14, colors::kBlack);
        int shift = 0;
        const uint16_t* rows = Font::rows(14, index, q, &shift);
        const fontdata::Size& f = Font::size(14);
        for (int r = 0; r < f.rows; ++r)
            for (int b = 0; b < 16; ++b) {
                const bool want = (rows[r] & (0x8000 >> b)) != 0;
                CHECK((s.at(10 + shift + b, 20 + f.top + r) == colors::kBlack) == want);
            }
    }
    // At 14 px, the half pixel changes the shape of the glyph (not a mere shift).
    int s0 = 0, s2 = 0;
    const uint16_t* q0 = Font::rows(14, index, 0, &s0);
    const uint16_t* q2 = Font::rows(14, index, 2, &s2);
    bool shifted = true;
    for (int r = 0; r < Font::size(14).rows; ++r) shifted = shifted && (q0[r] >> 1) == q2[r];
    CHECK_FALSE(shifted);
    // At 16 px, the outlines are on the grid: 1/2 and 3/4 shift by one column.
    CHECK(Font::size(16).shift[2] == 1);
    CHECK(Font::size(16).shift[3] == 1);
}

TEST_CASE("8×16 glyphs keep their place in the EmuTOS cell: i, l and I centred", "[gemui]") {
    // At 16 px (outlines on the pixel grid), each glyph is the bitmap of bios/fnt_st_8x16.c, row r of the cell at
    // r - 13 from the baseline, in the same columns: the prototype's TrueType started the ink of each one at the
    // origin, i and l two columns to the left of their place, I one.
    const struct {
        char32_t c;
        uint8_t rows[16];
    } emutos[] = {
        {U'i', {0x00, 0x00, 0x18, 0x18, 0x00, 0x38, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x3c, 0x00, 0x00}},
        {U'I', {0x00, 0x00, 0x7e, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x7e, 0x00, 0x00}},
        {U'l', {0x00, 0x00, 0x38, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x3c, 0x00, 0x00}},
        {U'M', {0x00, 0x00, 0xc6, 0xc6, 0xee, 0xee, 0xfe, 0xd6, 0xd6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00}},
        {U'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00}},
    };
    REQUIRE(Font::size(16).top == -13);
    REQUIRE(Font::size(16).rows == 16);
    for (const auto& g : emutos) {
        CAPTURE(uint32_t(g.c));
        int shift = -1;
        const uint16_t* rows = Font::rows(16, Font::glyph(g.c), 0, &shift);
        CHECK(shift == 0);
        for (int r = 0; r < 16; ++r) CHECK(rows[r] == uint16_t(g.rows[r] << 8));
    }
    // At 14 px (7 px advance), the stem of i in the middle column of the cell, at the first quarter.
    int shift = -1;
    const uint16_t* i14 = Font::rows(14, Font::glyph(U'i'), 0, &shift);
    CHECK(shift == 0);
    CHECK(i14[Font::size(14).rows - 6] == 0x1000);  // stem: column 3 of 0 to 6
}

TEST_CASE("Fallback font metrics (arrows, check mark)", "[gemui]") {
    // GEM font alone: ascent and descent rounded.
    CHECK(Font::lineBox("Chip Voice", 14).ascent == 11);
    CHECK(Font::lineBox("Chip Voice", 14).height() == 14);
    CHECK(Font::lineBox("A", 12).height() == 12);
    // Check mark: 16 px line, baseline 2 px lower (the prototype's 22 px checked menu entry).
    CHECK(Font::lineBox("✓ Chip Voice", 14).ascent == 13);
    CHECK(Font::lineBox("✓ Chip Voice", 14).height() == 16);
    // Arrows at 14 px: same 16 px line.
    CHECK(Font::lineBox("←→ Channel", 14).ascent == 13);
    CHECK(Font::lineBox("←→ Channel", 14).height() == 16);
    // The non-breaking space stays in the GEM font.
    CHECK(Font::lineBox("\xC2\xA0\xC2\xA0" "0", 14).height() == 14);
    // Advance: 8 px at 14 px for the check mark ("✓ Chip Voice": 85 px in the prototype).
    CHECK(Font::advance(U'✓', 14) == 8);
    CHECK(Font::advance(U'←', 14) == 8);
    CHECK(Font::advance(U'←', 16) == 10);
    CHECK(Font::advance(U'A', 14) == 7);
    CHECK(Font::width("✓ Chip Voice", 14) == 85);
}

TEST_CASE("Note, bullet and em dash: glyphs of the Atari ST encoding", "[gemui]") {
    // Characters of the prototype's fallback font, drawn with the GEM glyph: note 0x0B, middle dot 0xFA; no em dash
    // in the Atari ST encoding: hyphen.
    CHECK(Font::glyph(U'\u266A') == 103);
    CHECK(Font::glyph(U'\u2022') == 104);
    CHECK(Font::glyph(U'\u2014') == Font::glyph(U'-'));
    // AZERTY and QWERTZ key legends of the keyboard drawing: codes 0x97, 0xFD, 0x99, 0x8E, 0x9A, 0x9E, 0xBA.
    const char32_t latin[] = {U'\u00F9', U'\u00B2', U'\u00D6', U'\u00C4', U'\u00DC', U'\u00DF', U'\u00B4'};
    for (int i = 0; i < 7; ++i) CHECK(Font::glyph(latin[i]) == 105 + i);
    CHECK(Font::glyph(U'\u2026') == Font::glyph(U'?'));
    // Fallback font metrics, like the arrows.
    CHECK_FALSE(Font::native(U'\u266A'));
    CHECK(Font::lineBox("A2x1 \u266A140", 14).height() == 16);
    CHECK(Font::advance(U'\u2014', 14) == 8);
    // Rows of EmuTOS's bios/fnt_st_8x8.c and bios/fnt_st_6x6.c.
    CHECK(std::vector<uint8_t>(fontdata::k8x8[103], fontdata::k8x8[103] + 8) ==
          std::vector<uint8_t>{0x18, 0x1c, 0x16, 0x10, 0x10, 0x70, 0xf0, 0x60});
    CHECK(std::vector<uint8_t>(fontdata::k8x8[104], fontdata::k8x8[104] + 8) ==
          std::vector<uint8_t>{0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00});
    CHECK(std::vector<uint8_t>(fontdata::k6x6[104], fontdata::k6x6[104] + 6) ==
          std::vector<uint8_t>{0x00, 0x00, 0x30, 0x30, 0x00, 0x00});
    // 8×16 at 16 px (outlines on the pixel grid): the note of bios/fnt_st_8x16.c, rows 2 to 10 of the cell (from 11
    // to 3 px above the baseline), in its columns.
    int shift = 0;
    const uint16_t* note = Font::rows(16, 103, 0, &shift);
    REQUIRE(Font::size(16).top == -13);
    const uint16_t want[] = {0x0800, 0x0e00, 0x0f00, 0x0900, 0x0800, 0x0800, 0x7800, 0xf800, 0x7000};
    CHECK(shift == 0);
    for (int i = 0; i < 9; ++i) CHECK(note[2 + i] == want[i]);
}

TEST_CASE("Small texts: GEM 8×8 and 6×6 fonts", "[gemui]") {
    CHECK(Font::cell(16) == 0);
    CHECK(Font::cell(13) == 0);
    CHECK(Font::cell(12) == 8);
    CHECK(Font::cell(9) == 8);
    CHECK(Font::cell(16, Face::kSmall) == 6);
    CHECK(Font::cell(10, Face::kSmall) == 6);
    // Fixed advance in 8×8, arrows included (GEM glyphs, no fallback font); the line keeps the height of its size.
    // In 6×6, the characters outside the GEM font advance as in the fallback font.
    CHECK(Font::width("←→=Channel", 12) == 80);
    CHECK(Font::width("R11 EPlo", 10, 0, Face::kSmall) == 48);
    CHECK(Font::width("(per channel)", 12, 1, Face::kSmall) == 13 * 7);
    CHECK(Font::width("draw — left", 12, 0, Face::kSmall) == 10 * 6 + 7);
    CHECK(Font::width("←→", 16, 0, Face::kSmall) == 2 * 10);
    CHECK(Font::lineBox("←→=Channel", 12).ascent == 10);
    CHECK(Font::lineBox("←→=Channel", 12).height() == 12);
    // "A" of EmuTOS's bios/fnt_st_8x8.c and bios/fnt_st_6x6.c, placed on the whole column (the quarter of a pixel is
    // ignored): last row on the baseline (descenders), the one before just above.
    struct Case {
        Face face;
        std::vector<uint8_t> rows;
    };
    const Case cases[] = {{Face::kSystem, {0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00}},
                          {Face::kSmall, {0x70, 0x88, 0xf8, 0x88, 0x88, 0x00}}};
    for (const Case& c : cases) {
        Surface s(30, 30, colors::kWhite);
        Painter p(s);
        p.textAt("A", 10.5, 20, 12, colors::kBlack, 0, c.face);
        const int top = 20 + 1 - int(c.rows.size());
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 30; ++x) {
                const int r = y - top, b = x - 10;
                const bool want = r >= 0 && r < int(c.rows.size()) && b >= 0 && b < 8 && (c.rows[size_t(r)] & (0x80 >> b));
                CHECK((s.at(x, y) == colors::kBlack) == want);
            }
    }
}

TEST_CASE("Tooltip: 8×8 font, lines wrapped at half of the desktop", "[gemui]") {
    Desktop d(1016, 944);
    Label root;
    root.setBox({0, 0, 1016, 944});
    d.setRoot(&root);
    std::string tip = "word0xx";  // 25 words of 7 characters: 7 per line of at most 492 px (55 characters)
    for (int i = 1; i < 25; ++i) tip += " word" + std::to_string(i % 10) + "xx";
    root.setTip(tip);
    d.mouseMove({100, 100}, {});
    d.tick(0);
    d.tick(Desktop::kTipDelayMs + 1);
    d.paint();
    // White bubble with a 2 px black border on the black desktop, at (112, 120): 4 lines of 14 px, 55 characters of
    // 8 px.
    int minX = 1016, maxX = -1, minY = 944, maxY = -1;
    for (int y = 0; y < 944; ++y)
        for (int x = 0; x < 1016; ++x)
            if (d.surface().at(x, y) == colors::kWhite) {
                minX = std::min(minX, x), maxX = std::max(maxX, x);
                minY = std::min(minY, y), maxY = std::max(maxY, y);
            }
    CHECK(minX == 114);
    CHECK(minY == 122);
    CHECK(maxX - minX + 1 == 55 * 8 + 2 * 6);
    CHECK(maxY - minY + 1 == 4 * 14 + 2 * 4);
    // First glyph: "w" of the 8×8 font, baseline at 127 + 10.
    const int w = Font::glyph(U'w');
    for (int r = 0; r < fontdata::k8x8Rows; ++r)
        for (int b = 0; b < 8; ++b)
            CHECK((d.surface().at(120 + b, 137 - 7 + r) == colors::kBlack) == ((fontdata::k8x8[w][r] & (0x80 >> b)) != 0));
}

TEST_CASE("Alert: shrink-to-fit width, limited to half of the desktop", "[gemui]") {
    Desktop d(1016, 944);
    SECTION("Wrapped line: 440 px content, 508 px box (the prototype's CHIP alert)") {
        Alert::Spec spec;
        spec.lines = {{"Chip conformity"}, {""}, {"Everything in this pattern can be written to YM registers."},
                      {"VBL, YM3 and SNDH exports will match what you hear."}};
        const Alert& a = Alert::open(d, spec);
        // Prototype: [254, 247.73, 508 × 221.97]; 5 lines of 25.6 px, collapsed 18 px margins before the button.
        CHECK(a.bounds() == Rect{254, 248, 508, 222});
    }
    SECTION("1272 px desktop: the 520 px max-width limits the box, border and margins included") {
        Desktop wide(1272, 944);
        Alert::Spec spec;
        spec.lines = {{"Chip conformity"}, {""}, {"Everything in this pattern can be written to YM registers."},
                      {"VBL, YM3 and SNDH exports will match what you hear."}};
        const Alert& a = Alert::open(wide, spec);
        // Prototype at 1272 px (#msgWin, max-width:520px, box-sizing:border-box): [376, 247.73, 520 × 221.97].
        CHECK(a.bounds() == Rect{376, 248, 520, 222});
    }
    SECTION("Short text: the box shrinks to the text") {
        Alert::Spec spec;
        spec.lines = {{"Short"}};
        const Alert& a = Alert::open(d, spec);
        CHECK(a.bounds().w == 40 + 2 * 32 + 4);
    }
    SECTION("Prompt box: inline buttons, one space between them") {
        Alert::Spec spec;
        spec.lines = {{"Track name, 8 chars (blank = number):"}};
        spec.buttons = {"Cancel", "OK"};
        spec.input = true;
        spec.inputChars = 10;
        const Alert& a = Alert::open(d, spec);
        // Prototype (#promptWin): [326, 279.125, 364 × 159.19].
        CHECK(a.bounds() == Rect{326, 279, 364, 159});
    }
}

TEST_CASE("Alert: choice rows below the text, one option picked per row", "[gemui]") {
    Desktop d(1016, 944);
    Alert::Spec spec;
    spec.lines = {{"Export the pattern as WAV"}};
    spec.choices = {{"Format", {"16-bit", "24-bit", "32-bit float"}, 0}, {"Rate (kHz)", {"44.1", "48", "Host"}, 9}};
    spec.buttons = {"Cancel", "Export"};
    spec.defaultButton = 1;
    int button = -1;
    std::vector<int> picks;
    Alert& a = Alert::open(d, spec, [&](int b, const std::vector<int>& p) {
        button = b;
        picks = p;
    });
    CHECK(a.picks() == std::vector<int>{0, 2});  // option out of range: the last one
    CHECK(a.option(1, 2)->on());
    CHECK_FALSE(a.option(2, 0));
    // Options of a row side by side, one space apart; rows aligned, 10 px between them, between the text and the
    // buttons of the box.
    const Rect f0 = a.option(0, 0)->bounds(), f1 = a.option(0, 1)->bounds(), r0 = a.option(1, 0)->bounds();
    CHECK(f1.x - (f0.x + f0.w) == 8);
    CHECK(f1.y == f0.y);
    CHECK(r0.x == f0.x);
    CHECK(r0.y - (f0.y + f0.h) == 10);
    CHECK(f0.y >= a.bounds().y + 24 + 25);
    CHECK(f0.x > a.bounds().x + 32 + Font::width("Rate (kHz)", 16));
    CHECK(a.bounds().y + a.bounds().h > r0.y + r0.h + 18 + 20);
    // A click picks the option; Enter returns the default button and the picked options.
    const Point at{f1.x + f1.w / 2, f1.y + f1.h / 2};
    d.mouseMove(at, {});
    d.mouseDown(at, MouseButton::Left, {});
    d.mouseUp(at, {});
    CHECK(a.picks() == std::vector<int>{1, 2});
    CHECK_FALSE(a.option(0, 0)->on());
    KeyEvent e;
    e.key = Key::Enter;
    d.keyDown(e);
    CHECK(button == 1);
    CHECK(picks == std::vector<int>{1, 2});
    CHECK_FALSE(d.topModal());
}

TEST_CASE("Outline: non-zero winding, a polygon inside another a hole only if it turns the other way",
          "[gemui]") {
    // A 10 × 10 square, a 4 × 4 square inside it turning the same way, then the other way.
    static const float same[] = {0, 0, 10, 0, 10, 10, 0, 10, 3, 3, 7, 3, 7, 7, 3, 7};
    static const float other[] = {0, 0, 10, 0, 10, 10, 0, 10, 3, 3, 3, 7, 7, 7, 7, 3};
    static const uint16_t ends[] = {4, 8};
    const Outline a{same, ends}, b{other, ends};
    CHECK(a.inside(1, 1));
    CHECK(a.inside(5, 5));
    CHECK(b.inside(1, 1));
    CHECK_FALSE(b.inside(5, 5));
    CHECK_FALSE(a.inside(11, 5));
    CHECK_FALSE(a.inside(-1, 5));
    // An edge includes its end of smaller y only: a point on the top edge is inside, on the bottom edge outside.
    CHECK(a.inside(1, 0));
    CHECK_FALSE(a.inside(1, 10));
    // A triangle: its slanted edge cuts the row at x = y.
    static const float tri[] = {0, 0, 8, 8, 0, 8};
    static const uint16_t one[] = {3};
    const Outline t{tri, one};
    CHECK(t.inside(2.9, 3.5));
    CHECK_FALSE(t.inside(3.6, 3.5));
}

TEST_CASE("Colour blend: rounded per channel, an opacity outside [0, 1] at the nearest bound", "[gemui]") {
    const Color a = rgb(0xFF, 0x80, 0x00), b = rgb(0x00, 0x00, 0xFF);
    CHECK(mix(a, b, 0.5) == rgb(0x80, 0x40, 0x80));
    CHECK(mix(a, b, 1.5) == a);
    CHECK(mix(a, b, -0.5) == b);
    CHECK(mix(a, b, std::numeric_limits<double>::quiet_NaN()) == b);
}

TEST_CASE("Menu: a checked entry is taller", "[gemui]") {
    Desktop d(1016, 944);
    std::vector<MenuItem> items(2);
    items[0].label = "Chip Voice";
    items[1].label = "SID Voice";
    // Anchor at a half pixel: edges rounded separately (prototype: menu [565, 182, 163 × …], 6 px lower; its frame
    // now lies on the button's rule).
    const MenuList& m = MenuList::open(d, items, 0, Box{565, 152, 94, 24}, false);
    CHECK(m.bounds().y == 174);
    CHECK(m.bounds().h == 4 + 22 + 20);
    const MenuList& m2 = MenuList::open(d, items, 0, Box{366.5, 152, 94, 24}, false);
    CHECK(m2.bounds().x == 367);
}

TEST_CASE("Menu: an unavailable entry does not open its submenu, on hover or on a click", "[gemui]") {
    Desktop d(1016, 944);
    std::vector<MenuItem> items(2);
    items[0].label = "STE bass";
    items[0].disabled = true;
    items[0].why = "STE only";
    items[0].sub.resize(2);
    items[0].sub[0].label = "-2 dB";
    items[0].sub[1].label = "0 dB";
    items[1].label = "Timer";
    items[1].sub.resize(1);
    items[1].sub[0].label = "MFP";
    MenuList& m = MenuList::open(d, items, kNoMarks, Box{100, 100, 94, 24}, false);
    auto middle = [](Rect r) { return Point{r.x + r.w / 2, r.y + r.h / 2}; };
    const Point off = middle(m.itemRect(0)), on = middle(m.itemRect(1));
    d.mouseMove(on, {});
    REQUIRE(d.topPopup() != &m);  // available: the submenu opens on hover
    d.mouseMove(off, {});
    CHECK(d.topPopup() == &m);  // hovering the unavailable entry closes it and opens nothing
    d.mouseDown(off, MouseButton::Left, {});
    d.mouseUp(off, {});
    CHECK(d.topPopup() == &m);
    CHECK(m.tipAt(off) == "STE only");
}

TEST_CASE("Menu: a desktop destroyed with a submenu open frees the submenu first", "[gemui]") {
    {
        Desktop d(1016, 944);
        std::vector<MenuItem> items(1);
        items[0].label = "Timer";
        items[0].sub.resize(1);
        items[0].sub[0].label = "MFP";
        MenuList& m = MenuList::open(d, items, kNoMarks, Box{100, 100, 94, 24}, false);
        const Rect r = m.itemRect(0);
        d.mouseMove({r.x + r.w / 2, r.y + r.h / 2}, {});
        REQUIRE(d.topPopup() != &m);
    }  // ASan: the submenu used to read its parent, already freed
    SUCCEED();
}

namespace {

// A root as wide as the desktop, with a value field at (100, 100).
struct FieldRig {
    Desktop d{400, 300};
    Widget root;
    ValueField f;
    int commits = 0;
    std::string last;
    FieldRig() {
        root.setBounds(d.bounds());
        d.setRoot(&root);
        root.add(f);
        f.setBounds({100, 100, 40, 16});
        f.onCommit = [this](const std::string& t) {
            ++commits;
            last = t;
            return true;
        };
    }
    ~FieldRig() { d.setRoot(nullptr); }
    void click(Point p) {
        d.mouseMove(p, {});
        d.mouseDown(p, MouseButton::Left, {});
        d.mouseUp(p, {});
    }
    void type(char32_t c) {
        KeyEvent e;
        e.key = Key::Character;
        e.ch = c;
        d.keyDown(e);
    }
};

}  // namespace

TEST_CASE("Value field: a shorter mask while typing ends the input instead of writing outside the mask", "[gemui]") {
    FieldRig g;
    g.f.setMask("999");
    g.f.setText("100");
    g.f.beginEdit();
    REQUIRE(g.f.editing());
    // The focused track changes scale (Smooth "999" to Chip "99") from outside the editor.
    g.f.setMask("99");
    CHECK_FALSE(g.f.editing());
    CHECK(g.d.keyFocus() == nullptr);
    CHECK(g.commits == 0);
    g.d.paint();  // ASan: maskDisplay used to read before its positions and write past its text
    // The same mask again (every refresh): the input goes on.
    g.f.beginEdit();
    g.f.setMask("99");
    CHECK(g.f.editing());
}

TEST_CASE("Value field: the unit after the mask's digits stays in normal video under the pointer", "[gemui]") {
    // A white window under a field of nine 8 px cells, "440.00 Hz".
    struct Paper : Widget {
        void paint(Painter& p) override { p.fill(bounds(), colors::kWhite); }
    };
    Desktop d{400, 300};
    Paper root;
    ValueField f;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    root.add(f);
    f.setBounds({100, 100, 72, 16});
    f.setMask("999.99 Hz");
    f.setText("440.00 Hz");
    // The top row of the field, above the glyphs: black where the field is in reverse video.
    auto reversed = [&d](int x) { return d.surface().at(x, 100) == colors::kBlack; };
    d.mouseMove({110, 108}, {});
    d.paint();
    // "440.00" reversed; the space and "Hz" stay black on white.
    for (int x = 100; x < 148; ++x) CHECK(reversed(x));
    for (int x = 148; x < 172; ++x) CHECK_FALSE(reversed(x));
    // Without a unit, the whole field reversed, as before.
    f.setMask("999");
    f.setText("440");
    d.paint();
    for (int x = 100; x < 172; ++x) CHECK(reversed(x));
    // Without a mask, the unit set for the field (Digidrums' rate and fine tune).
    f.setMask("");
    f.setUnit(" ct");
    f.setText("-12 ct");
    d.paint();
    for (int x = 100; x < 148; ++x) CHECK(reversed(x));  // dashes' cells and "-12"
    for (int x = 148; x < 172; ++x) CHECK_FALSE(reversed(x));
    f.setText("Off");  // a text without that unit: reversed whole
    d.paint();
    for (int x = 100; x < 172; ++x) CHECK(reversed(x));
    d.setRoot(nullptr);
}

TEST_CASE("Value field: hidden while typed into, its input is cancelled and a click opens it again", "[gemui]") {
    FieldRig g;
    g.f.setText("12");
    g.click({110, 108});
    REQUIRE(g.f.editing());
    REQUIRE(g.d.keyFocus() == &g.f);
    g.f.setVisible(false);  // the generator changed: its fields are hidden
    CHECK_FALSE(g.f.editing());
    CHECK(g.d.keyFocus() == nullptr);
    CHECK(g.commits == 0);
    g.f.setVisible(true);
    g.click({110, 108});
    CHECK(g.f.editing());
    CHECK(g.d.keyFocus() == &g.f);
}

TEST_CASE("Popup: a value field being typed into confirms when a press outside closes the popup", "[gemui]") {
    FieldRig g;
    struct Box : Widget {
        ValueField v;
        Box() { add(v); }
    };
    auto box = std::make_unique<Box>();
    box->setBounds({200, 50, 100, 60});
    box->v.setBounds({210, 60, 40, 16});
    box->v.setText("5");
    int commits = 0;
    std::string got;
    box->v.onCommit = [&](const std::string& t) {
        ++commits;
        got = t;
        return true;
    };
    Box& b = static_cast<Box&>(g.d.openPopup(std::move(box)));
    g.click({220, 68});
    REQUIRE(b.v.editing());
    g.type(U'1');
    g.type(U'2');
    g.click({20, 250});  // outside: the popup closes, the typed value counts
    CHECK_FALSE(g.d.hasPopups());
    CHECK(commits == 1);
    CHECK(got == "12");
}

TEST_CASE("Slider: a press ended by a modal box or by hiding ends the gesture and the repeat", "[gemui]") {
    Desktop d(1272, 944);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    Slider s;
    root.add(s);
    s.setRange(1, 20, 0.5);
    s.setValue(5);
    s.setBounds({100, 100, 200, 16});
    int starts = 0, ends = 0;
    s.onGestureStart = [&] { ++starts; };
    s.onGestureEnd = [&] { ++ends; };
    const Point left{106, 108};
    // An alert opens (end of a background export) while the left arrow is held.
    d.mouseMove(left, {});
    d.mouseDown(left, MouseButton::Left, {});
    REQUIRE(s.value() == 4.5);
    d.openModal(std::make_unique<Widget>());
    CHECK(ends == 1);
    d.tick(250);
    d.tick(300);
    d.tick(2000);
    CHECK(s.value() == 4.5);  // used to step down to 1 under the alert
    d.mouseUp(left, {});
    CHECK(ends == 1);
    d.closeModal(*d.topModal());
    // The slider's window closes (Escape) during a press on its track.
    d.mouseMove({200, 108}, {});
    d.mouseDown({200, 108}, MouseButton::Left, {});
    REQUIRE(starts == 2);
    s.setVisible(false);
    CHECK(ends == 2);
    d.mouseUp({200, 108}, {});
    CHECK(ends == 2);
    d.setRoot(nullptr);
}

TEST_CASE("Desktop: a menu opened over a modal box lies over it and takes the mouse and Esc first", "[gemui]") {
    Desktop d(400, 300);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    // A black box with a button, as a modal box.
    struct Black : Widget {
        Button button{"Up"};
        Black() { add(button); }
        void paint(Painter& p) override { p.fill(bounds(), colors::kBlack); }
        bool blocksMouse() const noexcept override { return true; }
    };
    auto box = std::make_unique<Black>();
    box->setBounds({50, 50, 300, 200});
    Button* button = &box->button;
    button->setBounds({60, 60, 60, 24});
    int clicks = 0, picked = -1;
    button->onClick = [&] { ++clicks; };
    Black& modal = static_cast<Black&>(d.openModal(std::move(box)));
    const auto openMenu = [&] {
        std::vector<MenuItem> items(2);
        items[0].label = "Up";
        items[1].label = "Down";
        for (int i = 0; i < 2; ++i) items[size_t(i)].pick = [&picked, i] { picked = i; };
        return &MenuList::open(d, items, 0, button->box(), false);
    };
    const auto press = [&](Point p) {
        d.mouseMove(p, {});
        d.mouseDown(p, MouseButton::Left, {});
        d.mouseUp(p, {});
    };

    // Drawn over the box, not under its veil: the white of the menu, without the 25 % shade.
    const MenuList* m = openMenu();
    d.paint();
    const Rect down = m->itemRect(1);
    CHECK(d.surface().at(down.right() - 3, down.y + 2) == colors::kWhite);
    // Its entry takes the press; the box and its button stay.
    press({down.x + 10, down.y + 10});
    CHECK(picked == 1);
    CHECK_FALSE(d.hasPopups());
    CHECK(d.topModal() == &modal);
    CHECK(clicks == 0);
    // Esc closes the menu alone; a second Esc goes to the box.
    openMenu();
    KeyEvent esc;
    esc.key = Key::Escape;
    CHECK(d.keyDown(esc));
    CHECK_FALSE(d.hasPopups());
    CHECK(d.hasModal());
    // A press on the box outside the menu closes it and reaches the box.
    openMenu();
    press({70, 70});
    CHECK_FALSE(d.hasPopups());
    CHECK(clicks == 1);
    // Closing the box closes the menu opened over it.
    openMenu();
    d.closeModal(modal);
    CHECK_FALSE(d.hasPopups());
    CHECK_FALSE(d.hasModal());
    d.paint();
    d.setRoot(nullptr);
}

TEST_CASE("Desktop: a menu kept under a modal box's veil takes neither the mouse nor Esc", "[gemui]") {
    Desktop d(400, 300);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    std::vector<MenuItem> items(1);
    int picked = 0;
    items[0].label = "Entry";
    items[0].pick = [&] { ++picked; };
    const MenuList& m = MenuList::open(d, items, kNoMarks, Box{20, 20, 60, 24}, false);
    const Rect entry = m.itemRect(0);
    auto box = std::make_unique<Widget>();
    box->setBounds({200, 200, 100, 50});
    Widget& modal = d.openModal(std::move(box), true);
    const Point p{entry.x + 10, entry.y + 10};
    d.mouseMove(p, {});
    d.mouseDown(p, MouseButton::Left, {});
    d.mouseUp(p, {});
    CHECK(picked == 0);
    KeyEvent esc;
    esc.key = Key::Escape;
    d.keyDown(esc);
    CHECK(d.hasPopups());  // it comes back once the box is closed
    d.closeModal(modal);
    CHECK(d.hasPopups());
    d.mouseMove(p, {});
    d.mouseDown(p, MouseButton::Left, {});
    d.mouseUp(p, {});
    CHECK(picked == 1);
    d.setRoot(nullptr);
}

TEST_CASE("PNG: written then read back identically", "[gemui]") {
    tools::Image img;
    img.width = 37;
    img.height = 23;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x)
            img.pixels.push_back(0xFF000000u | uint32_t(x * 7) << 16 | uint32_t(y * 11) << 8 | uint32_t((x ^ y) & 3) * 60);
    const std::vector<uint8_t> png = tools::encodePng(img);
    std::string error;
    const tools::Image back = tools::decodePng(png, &error);
    INFO(error);
    REQUIRE(back.width == img.width);
    REQUIRE(back.height == img.height);
    CHECK(back.pixels == img.pixels);
    CHECK(tools::decodePng(std::vector<uint8_t>(png.begin(), png.begin() + 40), &error).width == 0);
}

TEST_CASE("Text field: hidden while it has the focus, it no longer takes the keys", "[gemui]") {
    Desktop d(400, 300);
    Widget root;
    root.setBounds(d.bounds());
    d.setRoot(&root);
    TextField f;
    root.add(f);
    f.setBounds({100, 100, 80, 16});
    d.mouseMove({110, 108}, {});
    d.mouseDown({110, 108}, MouseButton::Left, {});
    d.mouseUp({110, 108}, {});
    REQUIRE(d.keyFocus() == &f);
    f.setVisible(false);  // its box changes page
    CHECK(d.keyFocus() == nullptr);
    f.setVisible(true);
    KeyEvent e;
    e.key = Key::Character;
    e.ch = U'x';
    CHECK_FALSE(f.keyDown(e));  // no text cursor: the field used to keep it and take the keys
    CHECK(f.text().empty());
    d.setRoot(nullptr);
}
