// STYM2149 — gemui tests: GEM menus laid out in columns (chordCols of the prototype, P400) and their keyboard.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Expected boxes: the prototype's Chord... menu (#chordSel, chordCols = 1) measured in Chromium 141 (the browser of
// the visual references, tests/data/visual/README.md) at 100 % zoom in a 1272 px wide page, for several page
// heights, with the anchor at #chordSel's place (94, 258, 80 × 24, also STYM2149's).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "gemui/Desktop.h"
#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Pitch.h"

using namespace stym;
using gemui::Box;
using gemui::Desktop;
using gemui::MenuItem;
using gemui::MenuList;
using gemui::Rect;

namespace {

const Box kChordAnchor{94, 258, 80, 24};

std::vector<MenuItem> chordItems(int* picked = nullptr) {
    std::vector<MenuItem> items;
    for (int i = 0; i < int(ymengine::kChords.size()); ++i) {
        MenuItem m;
        m.label = std::string(ymengine::kChords[size_t(i)].name);
        if (picked) m.pick = [picked, i] { *picked = i; };
        items.push_back(std::move(m));
    }
    return items;
}

int chordIndex(const char* name) {
    for (int i = 0; i < int(ymengine::kChords.size()); ++i)
        if (ymengine::kChords[size_t(i)].name == std::string_view(name)) return i;
    return -1;
}

// Column of the prototype: left edge, width, first entry, number of entries.
struct Column {
    int x, w;
    const char* first;
    int count;
};

void checkColumns(const MenuList& m, const std::vector<Column>& cols) {
    int item = 0;
    for (const Column& c : cols) {
        INFO("column starting with " << c.first);
        REQUIRE(chordIndex(c.first) == item);
        for (int k = 0; k < c.count; ++k, ++item) {
            const Rect r = m.itemRect(item);
            CHECK(r == Rect{c.x, m.bounds().y + 2 + 20 * k, c.w, 20});
        }
    }
    CHECK(item == int(ymengine::kChords.size()));
}

void click(Desktop& d, gemui::Point p) {
    d.mouseMove(p, {});
    d.mouseDown(p, gemui::MouseButton::Left, {});
    d.mouseUp(p, {});
}

gemui::Point centre(const Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

// Root of the desktop that takes every key (the page behind the menu).
struct Page : gemui::Widget {
    std::vector<gemui::Key> keys;
    bool keyDown(const gemui::KeyEvent& e) override {
        keys.push_back(e.key);
        return true;
    }
};

gemui::KeyEvent key(gemui::Key k, bool shift = false) {
    gemui::KeyEvent e;
    e.key = k;
    e.mods.shift = shift;
    return e;
}

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
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}
};

}  // namespace

TEST_CASE("Menu columns: the 39 chords fill as many columns as the desktop height needs (P400)", "[gemui][menu]") {
    SECTION("944 px (the editor's desktop): one column, moved above the anchor to the top margin") {
        Desktop d(1272, 944);
        const MenuList& m = MenuList::open(d, chordItems(), -1, kChordAnchor, false, true);
        CHECK(m.bounds() == Rect{94, 4, 123, 784});
        checkColumns(m, {{96, 119, "Major", 39}});
    }
    SECTION("903 px: one entry too many for the first column") {
        Desktop d(1272, 903);
        const MenuList& m = MenuList::open(d, chordItems(), -1, kChordAnchor, false, true);
        CHECK(m.bounds() == Rect{94, 4, 242, 764});
        checkColumns(m, {{96, 119, "Major", 38}, {215, 119, "Double octave", 1}});
    }
    SECTION("600 px: two columns, each as wide as its widest entry") {
        Desktop d(1272, 600);
        const MenuList& m = MenuList::open(d, chordItems(), -1, kChordAnchor, false, true);
        CHECK(m.bounds() == Rect{94, 4, 235, 464});
        checkColumns(m, {{96, 112, "Major", 23}, {208, 119, "7#9", 16}});
    }
    SECTION("500 px: the last column is short, the menu as tall as the tallest one") {
        Desktop d(1272, 500);
        const MenuList& m = MenuList::open(d, chordItems(), -1, kChordAnchor, false, true);
        CHECK(m.bounds() == Rect{94, 4, 354, 364});
        checkColumns(m, {{96, 112, "Major", 18}, {208, 119, "6/9", 18}, {327, 119, "Tritone", 3}});
    }
    SECTION("400 px: three columns of 13") {
        Desktop d(1272, 400);
        const MenuList& m = MenuList::open(d, chordItems(), -1, kChordAnchor, false, true);
        CHECK(m.bounds() == Rect{94, 4, 340, 264});
        checkColumns(m, {{96, 112, "Major", 13}, {208, 105, "7 sus4", 13}, {313, 119, "Major 1st inv", 13}});
    }
    SECTION("200 px: the height never goes under 120 px; eight columns above the anchor") {
        Desktop d(1272, 200);
        const MenuList& m = MenuList::open(d, chordItems(), -1, kChordAnchor, false, true);
        CHECK(m.bounds() == Rect{94, 156, 865, 104});
        checkColumns(m, {{96, 98, "Major", 5},
                         {194, 105, "Augmented", 5},
                         {299, 112, "Half-dim 7", 5},
                         {411, 98, "Minor 6", 5},
                         {509, 98, "Minor 9", 5},
                         {607, 119, "Dominant 13", 5},
                         {726, 112, "Major spread", 5},
                         {838, 119, "Fourth", 4}});
    }
    SECTION("Without the option, the same menu keeps a single column") {
        Desktop d(1272, 400);
        const MenuList& m = MenuList::open(d, chordItems(), -1, kChordAnchor, false);
        CHECK(m.bounds().w == 123);
        CHECK(m.bounds().h == 784);
    }
}

TEST_CASE("Menu columns: the menu stays inside the desktop, every entry inside the menu (P400)", "[gemui][menu]") {
    Desktop d(1272, 400);
    // Anchor near the right edge: the menu is pulled left, 4 px from the edge.
    const MenuList& m = MenuList::open(d, chordItems(), -1, Box{1200, 300, 60, 24}, false, true);
    CHECK(m.bounds() == Rect{1272 - 340 - 4, 300 - 264 + 2, 340, 264});
    CHECK(m.bounds().x >= 4);
    CHECK(m.bounds().y >= 4);
    CHECK(m.bounds().right() <= d.width() - 4);
    CHECK(m.bounds().bottom() <= d.height() - 4);
    for (int i = 0; i < int(ymengine::kChords.size()); ++i) {
        const Rect r = m.itemRect(i);
        INFO("entry " << i);
        CHECK(r.x >= m.bounds().x + 2);
        CHECK(r.right() <= m.bounds().right() - 2);
        CHECK(r.y >= m.bounds().y + 2);
        CHECK(r.bottom() <= m.bounds().bottom() - 2);
    }
    // Anchor near the bottom: the menu goes above it, never over the top margin.
    Desktop low(1272, 400);
    const MenuList& m2 = MenuList::open(low, chordItems(), -1, Box{94, 380, 80, 16}, false, true);
    CHECK(m2.bounds() == Rect{94, 380 - 264 + 2, 340, 264});
    const MenuList& m3 = MenuList::open(low, chordItems(), -1, Box{94, 150, 80, 24}, false, true);
    CHECK(m3.bounds().y == 4);
}

TEST_CASE("Menu columns: an entry of any column is hovered in inverse video and picked by a click (P400)",
          "[gemui][menu]") {
    Desktop d(1272, 400);
    int picked = -1;
    const MenuList& m = MenuList::open(d, chordItems(&picked), -1, kChordAnchor, false, true);
    // Third column, second entry: Major 2nd inv.
    const int i = chordIndex("Major 2nd inv");
    const Rect r = m.itemRect(i);
    CHECK(r.x == 313);
    d.mouseMove(centre(r), {});
    d.paint();
    CHECK(d.surface().at(r.x + 2, r.y + 1) == gemui::colors::kBlack);
    const Rect other = m.itemRect(chordIndex("Major"));
    CHECK(d.surface().at(other.x + 2, other.y + 1) == gemui::colors::kWhite);
    click(d, centre(r));
    CHECK(picked == i);
    CHECK_FALSE(d.hasPopups());
}

TEST_CASE("Menu columns: as in the prototype, the keyboard only closes the menu (Esc), other keys reach the page",
          "[gemui][menu][keyboard]") {
    // openGemMenu has no keyboard navigation: arrows, Enter and Tab go to the page's keydown handler while the menu
    // stays open; Esc closes the menu first (closeGemMenu) and goes no further.
    Desktop d(1272, 400);
    Page page;
    page.setBounds(d.bounds());
    d.setRoot(&page);
    int picked = -1;
    MenuList::open(d, chordItems(&picked), -1, kChordAnchor, false, true);
    for (gemui::Key k : {gemui::Key::Down, gemui::Key::Right, gemui::Key::Up, gemui::Key::Left, gemui::Key::Enter,
                         gemui::Key::Tab}) {
        CHECK(d.keyDown(key(k)));
        CHECK(d.hasPopups());
    }
    CHECK(page.keys == std::vector<gemui::Key>{gemui::Key::Down, gemui::Key::Right, gemui::Key::Up, gemui::Key::Left,
                                                gemui::Key::Enter, gemui::Key::Tab});
    CHECK(picked == -1);
    CHECK(d.keyDown(key(gemui::Key::Escape)));
    CHECK_FALSE(d.hasPopups());
    CHECK(page.keys.size() == 6);
    // Menu closed: Esc goes to the page.
    d.keyDown(key(gemui::Key::Escape));
    CHECK(page.keys.size() == 7);
    d.setRoot(nullptr);
}

TEST_CASE("Arpeggiator: Chord... opens the 39 chords with chordCols, the sequencer's Chord menu does not (P400)",
          "[ui][arp][menu]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::ArpOn))] = 1;
    h.state.tracks[0].arp.mode = ymengine::ArpMode::Table;
    pluginui::MainWindow w(h);
    w.arpeggiator().show();
    const auto anchor = w.rectOf("chordSel");
    REQUIRE(anchor);
    REQUIRE(*anchor == Rect{94, 258, 80, 24});
    click(w.desktop(), centre(*anchor));
    const auto* m = dynamic_cast<const MenuList*>(w.desktop().topPopup());
    REQUIRE(m);
    // The editor's desktop (1272 × 944): one column, as in the prototype at the same size.
    CHECK(m->bounds() == Rect{94, 4, 123, 784});
    checkColumns(*m, {{96, 119, "Major", 39}});
    // A pick writes the chord into the table: Dominant 7 (0, 4, 7, 10).
    click(w.desktop(), centre(m->itemRect(chordIndex("Dominant 7"))));
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK(h.state.tracks[0].arp.length == 4);
    CHECK(h.state.tracks[0].arp.table[3] == 10);

    // #seqChord: no chordCols (a single column, Off then the 39 chords, the current one checked).
    w.sequencer().show();
    const auto seqChord = w.rectOf("seqChord");
    REQUIRE(seqChord);
    click(w.desktop(), centre(*seqChord));
    const auto* s = dynamic_cast<const MenuList*>(w.desktop().topPopup());
    REQUIRE(s);
    REQUIRE(s->items().size() == ymengine::kChords.size() + 1);
    CHECK(s->itemRect(1).x == s->itemRect(39).x);
    CHECK(s->itemRect(39).y > s->itemRect(1).y);
}
