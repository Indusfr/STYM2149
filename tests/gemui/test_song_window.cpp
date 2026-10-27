// STYM2149 — Song window: Song mode linked to the window, Add, menu of the patterns, keyboard control (songKey),
// drag and drop of the blocks, undo, jump from one entry to another (songStep).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using SongEntries = std::vector<ymseq::SongEntry>;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    struct Command {
        SeqCommand c;
        int a;
    };
    std::vector<Command> commands;

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
    void seqCommand(SeqCommand c, int a, int, int) override { commands.push_back({c, a}); }
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
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void click(MainWindow& w, const char* id, gemui::Modifiers m = {}, gemui::MouseButton b = gemui::MouseButton::Left) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    const gemui::Point p = centre(*r);
    w.desktop().mouseMove(p, m);
    w.desktop().mouseDown(p, b, m);
    w.desktop().mouseUp(p, m);
}

bool key(MainWindow& w, gemui::Key k, char32_t ch = 0, gemui::Modifiers m = {}) {
    gemui::KeyEvent e;
    e.key = k;
    e.ch = ch;
    if (ch >= U'a' && ch <= U'z') e.code = int(ch - U'a' + U'A');
    e.mods = m;
    return w.desktop().keyDown(e);
}
bool chr(MainWindow& w, char32_t ch, gemui::Modifiers m = {}) { return key(w, gemui::Key::Character, ch, m); }

SongEntries entries(const TestHost& h) { return h.state.seq.song.entries; }

// Initial Song, window open.
void withSong(TestHost& h, MainWindow& w, SongEntries e) {
    h.editExtras([&e](plugin::NonParamState& s) { s.seq.song.entries = std::move(e); });
    w.tick(0);
    w.songWindow().show();
}

}  // namespace

TEST_CASE("Song: opening the window switches Song mode on, closing it switches it off", "[ui][song]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(h.state.global.songMode);
    w.sequencer().show();
    click(w, "songMode");  // Song button of the Step Sequencer window
    CHECK(h.state.global.songMode);
    CHECK(w.songWindow().isOpen());
    // Escape closes the top window (CLOSERS): the mode goes off.
    CHECK(key(w, gemui::Key::Escape));
    CHECK_FALSE(w.songWindow().isOpen());
    CHECK_FALSE(h.state.global.songMode);
    // State read again by the host in Song mode: the window opens.
    h.editExtras([](plugin::NonParamState& s) { s.global.songMode = true; });
    w.tick(0);
    CHECK(w.songWindow().isOpen());
}

TEST_CASE("Song: Add, Shift+Add and Add to song in a pattern's menu", "[ui][song]") {
    TestHost h;
    MainWindow w(h);
    w.songWindow().show();
    click(w, "songAdd");
    CHECK(entries(h) == SongEntries{{0, 1, 0}});
    click(w, "songAdd");  // same pattern: one more repeat
    CHECK(entries(h) == SongEntries{{0, 2, 0}});
    click(w, "songAdd", {true, false, false});  // Shift: a separate block
    CHECK(entries(h) == SongEntries{{0, 2, 0}, {0, 1, 0}});
    // The Song arrows appear with the first block.
    CHECK(w.rectOf("songTrNext"));
    // Undo: one step per Add.
    CHECK(chr(w, U'z', {false, true, false}));
    CHECK(entries(h) == SongEntries{{0, 2, 0}});
    CHECK(chr(w, U'y', {false, true, false}));
    CHECK(entries(h) == SongEntries{{0, 2, 0}, {0, 1, 0}});
}

TEST_CASE("Song: keyboard control when the window is in front", "[ui][song]") {
    TestHost h;
    MainWindow w(h);
    withSong(h, w, {{0, 1, 0}, {1, 2, 0}, {2, 1, 0}});
    auto& song = w.songWindow();
    CHECK(song.cursor() == 0);
    CHECK(key(w, gemui::Key::Right));
    CHECK(song.cursor() == 1);
    CHECK(song.selection() == ymseq::SongSel{1});
    CHECK(key(w, gemui::Key::Left));
    CHECK(key(w, gemui::Key::Left));  // full cycle
    CHECK(song.cursor() == 2);
    CHECK(key(w, gemui::Key::Home));
    CHECK(song.cursor() == 0);
    CHECK(key(w, gemui::Key::End));
    CHECK(song.cursor() == 2);

    // D: duplicates; = and -: repeats; Delete: removes.
    CHECK(chr(w, U'd'));
    CHECK(entries(h) == SongEntries{{0, 1, 0}, {1, 2, 0}, {2, 1, 0}, {2, 1, 0}});
    CHECK(song.cursor() == 3);
    CHECK(chr(w, U'='));
    CHECK(chr(w, U'='));
    CHECK(chr(w, U'-'));
    CHECK(entries(h)[3].rep == 2);
    CHECK(key(w, gemui::Key::Delete));
    CHECK(entries(h) == SongEntries{{0, 1, 0}, {1, 2, 0}, {2, 1, 0}});
    CHECK(song.cursor() == 2);

    // A and Insert: the current pattern after the cursor.
    CHECK(key(w, gemui::Key::Home));
    CHECK(chr(w, U'a'));
    CHECK(entries(h) == SongEntries{{0, 1, 0}, {0, 1, 0}, {1, 2, 0}, {2, 1, 0}});
    CHECK(song.cursor() == 1);
    CHECK(key(w, gemui::Key::Insert));
    CHECK(entries(h).size() == 5);
    CHECK(song.cursor() == 2);

    // X cuts the cursor's block, V pastes it after the cursor.
    CHECK(key(w, gemui::Key::End));
    CHECK(chr(w, U'x'));
    CHECK(entries(h) == SongEntries{{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {1, 2, 0}});
    CHECK(key(w, gemui::Key::Home));
    CHECK(chr(w, U'v'));
    CHECK(entries(h) == SongEntries{{0, 1, 0}, {2, 1, 0}, {0, 1, 0}, {0, 1, 0}, {1, 2, 0}});
    CHECK(song.cursor() == 1);

    // With Ctrl, the letters go back to the main window's shortcuts (^D: the DMA generator).
    const auto before = entries(h);
    CHECK(chr(w, U'd', {false, true, false}));
    CHECK(entries(h) == before);
    // Step Sequencer in front: the Song no longer takes the keys.
    w.sequencer().show();
    CHECK_FALSE(chr(w, U'd'));
    CHECK(entries(h) == before);
}

TEST_CASE("Song: Enter and the transport arrows jump from one block to another", "[ui][song]") {
    TestHost h;
    MainWindow w(h);
    withSong(h, w, {{0, 1, 0}, {9, 1, 0}, {3, 2, 0}});
    CHECK(key(w, gemui::Key::Right));
    CHECK(key(w, gemui::Key::Enter));
    REQUIRE_FALSE(h.commands.empty());
    CHECK(h.commands.back().c == pluginui::Host::SeqCommand::SongJump);
    CHECK(h.commands.back().a == 1);
    CHECK(h.state.seq.current == 9);  // the block's pattern becomes current
    CHECK(h.state.seq.pending == -1);
    click(w, "songTrNext");
    CHECK(h.commands.back().a == 2);
    CHECK(h.state.seq.current == 3);
    click(w, "songTrNext");  // looping
    CHECK(h.commands.back().a == 0);
    click(w, "songTrPrev");
    CHECK(h.commands.back().a == 2);
}

TEST_CASE("Song: click, Shift+click, block menu and drag and drop", "[ui][song]") {
    TestHost h;
    MainWindow w(h);
    withSong(h, w, {{0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
    auto& song = w.songWindow();
    click(w, "songChip1");
    CHECK(song.selection() == ymseq::SongSel{1});
    click(w, "songChip2", {true, false, false});
    CHECK(song.selection() == ymseq::SongSel{1, 2});
    CHECK(song.cursor() == 2);

    // Block menu (its frame on the block's bottom rule, 20 px entries inside a 2 px frame): Repeat x4 on the selection.
    click(w, "songChip2", {}, gemui::MouseButton::Right);
    REQUIRE(w.desktop().hasPopups());
    const gemui::Rect chip = *w.rectOf("songChip2");
    const gemui::Point x4{chip.x + 20, chip.bottom() - 2 + 2 + 2 * 20 + 10};
    w.desktop().mouseMove(x4, {});
    w.desktop().mouseDown(x4, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(x4, {});
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK(entries(h)[1].rep == 4);
    CHECK(entries(h)[2].rep == 4);
    CHECK(entries(h)[0].rep == 1);

    // Dragging the selection (blocks 1 and 2) onto block 3: it moves after it.
    const gemui::Point from = centre(*w.rectOf("songChip1"));
    const gemui::Point to = centre(*w.rectOf("songChip3"));
    w.desktop().mouseMove(from, {});
    w.desktop().mouseDown(from, gemui::MouseButton::Left, {});
    w.desktop().mouseDrag({from.x + 3, from.y}, {});  // below the 5 px threshold
    w.desktop().mouseDrag(to, {});
    w.desktop().mouseUp(to, {});
    CHECK(entries(h) == SongEntries{{0, 1, 0}, {3, 1, 0}, {1, 4, 0}, {2, 4, 0}});
    CHECK(song.selection() == ymseq::SongSel{2, 3});
    // The click that ends a drag does not change the selection.
    CHECK(song.cursor() == 2);
}

namespace {

// Menu of block i: entry `label` (REQUIRE that it exists), menu left open.
const gemui::MenuItem& chipItem(MainWindow& w, int i, const std::string& label) {
    click(w, ("songChip" + std::to_string(i)).c_str(), {}, gemui::MouseButton::Right);
    const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(list);
    for (const gemui::MenuItem& m : list->items())
        if (m.label == label) return m;
    FAIL("missing entry: " << label);
    return list->items().front();
}

// Picks entry `label` of the menu of block i.
void pickChip(MainWindow& w, int i, const std::string& label) {
    const gemui::MenuItem& m = chipItem(w, i, label);
    REQUIRE_FALSE(m.disabled);
    const auto pick = m.pick;
    w.desktop().closePopups();
    pick();
    w.tick(0);
}

// Typing into the open prompt, then Enter.
void answer(MainWindow& w, const std::string& text) {
    REQUIRE(w.desktop().hasModal());
    for (int i = 0; i < 40; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) chr(w, char32_t(c));
    key(w, gemui::Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
}

}  // namespace

TEST_CASE("Extended Song: Rest, Note-off and Loop entries, length, jumps, restart point", "[ui][song]") {
    using ymseq::SongKind;
    TestHost h;
    MainWindow w(h);
    withSong(h, w, {{0, 1, 0}, {1, 1, 0}});
    auto& song = w.songWindow();

    // The settings specific to the special entries are greyed out on a pattern.
    CHECK(chipItem(w, 0, "Length...").disabled);
    w.desktop().closePopups();
    CHECK(chipItem(w, 0, "Loop back...").disabled);
    w.desktop().closePopups();

    pickChip(w, 0, "Insert rest");
    REQUIRE(entries(h).size() == 3);
    CHECK(entries(h)[1] == ymseq::SongEntry::rest(SongKind::Rest));
    CHECK(song.cursor() == 1);
    pickChip(w, 1, "Insert note-off");
    CHECK(entries(h)[2] == ymseq::SongEntry::rest(SongKind::NoteOff));
    // Loop after block 3: it replays the 4 blocks before it (no jump before it).
    pickChip(w, 3, "Insert loop");
    REQUIRE(entries(h).size() == 5);
    CHECK(entries(h)[4] == ymseq::SongEntry::loop(4));
    CHECK(w.rectOf("songChip4"));

    // Length of the Rest and Note-off entries (both selected), entries replayed by the jump.
    click(w, "songChip1");
    click(w, "songChip2", {true, false, false});
    pickChip(w, 2, "Length...");
    answer(w, "8");
    CHECK(entries(h)[1].arg == 8);
    CHECK(entries(h)[2].arg == 8);
    pickChip(w, 4, "Loop back...");
    answer(w, "2");
    CHECK(entries(h)[4].arg == 2);
    // Repeat on a jump: the number of jumps; Tempo... and Restart here greyed out.
    pickChip(w, 4, "Repeat x4");
    CHECK(entries(h)[4].rep == 4);
    CHECK(chipItem(w, 4, "Tempo...").disabled);
    w.desktop().closePopups();
    CHECK(chipItem(w, 4, "Restart here").disabled);
    w.desktop().closePopups();

    // Restart point, then back to the start; one undo per gesture.
    pickChip(w, 3, "Restart here");
    CHECK(entries(h)[3].restart);
    pickChip(w, 3, "Restart from start");
    CHECK_FALSE(entries(h)[3].restart);
    CHECK(chr(w, U'z', {false, true, false}));
    CHECK(entries(h)[3].restart);

    // Jump to a special entry: the current pattern does not change; the arrows skip the jumps.
    h.editExtras([](plugin::NonParamState& s) { s.seq.applied(1); });
    click(w, "songChip1");
    CHECK(key(w, gemui::Key::Enter));
    CHECK(h.commands.back().a == 1);
    CHECK(h.state.seq.current == 1);
    click(w, "songChip3");
    CHECK(key(w, gemui::Key::Enter));
    click(w, "songTrNext");  // block 4: a jump, skipped
    CHECK(h.commands.back().a == 0);
    CHECK(h.state.seq.current == 0);
    click(w, "songTrPrev");
    CHECK(h.commands.back().a == 3);
}

TEST_CASE("Song: a key that edits the Song closes the block menu; entries of a menu older than the Song do nothing",
          "[ui][song]") {
    TestHost h;
    MainWindow w(h);
    withSong(h, w, {{0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
    // Delete with the menu of block 3 open: the menu closes with the block it was opened on.
    chipItem(w, 3, "Tempo...");
    REQUIRE(w.desktop().hasPopups());
    key(w, gemui::Key::Delete);
    CHECK_FALSE(w.desktop().hasPopups());
    REQUIRE(entries(h).size() == 3);
    // The host shortens the Song (state read again) under a menu opened on block 2: Tempo... and Insert loop used to
    // read past the end of the entries (crash, or a prompt with a stray value).
    const auto tempo = chipItem(w, 2, "Tempo...").pick;
    const auto loop = chipItem(w, 2, "Insert loop").pick;
    h.editExtras([](plugin::NonParamState& s) { s.seq.song.entries.resize(1); });
    w.tick(0);
    w.desktop().closePopups();
    tempo();
    CHECK_FALSE(w.desktop().hasModal());
    loop();
    CHECK(entries(h).size() == 1);
}

TEST_CASE("Song: a drag cut short by an alert drops nothing and draws the blocks back as they were", "[ui][song]") {
    TestHost h;
    MainWindow w(h);
    withSong(h, w, {{0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {3, 1, 0}});
    const SongEntries before = entries(h);
    const gemui::Rect win = *w.rectOf("songWin");
    const gemui::Point from = centre(*w.rectOf("songChip1"));
    const gemui::Point to = centre(*w.rectOf("songChip3"));
    auto pixels = [&w, &win] {
        w.desktop().paint();
        std::vector<gemui::Color> out;
        for (int y = win.y; y < win.bottom(); ++y)
            for (int x = win.x; x < win.right(); ++x) out.push_back(w.desktop().surface().at(x, y));
        return out;
    };
    w.desktop().mouseMove(from, {});
    const std::vector<gemui::Color> still = pixels();
    // Block 2 dragged onto block 4, then an alert opens (end of a background export) before the drop.
    w.desktop().mouseDown(from, gemui::MouseButton::Left, {});
    w.desktop().mouseDrag(to, {});
    CHECK(pixels() != still);  // the dragged block at 45 %, the target marked
    gemui::Widget& box = w.desktop().openModal(std::make_unique<gemui::Widget>());
    w.desktop().mouseUp(to, {});
    w.desktop().closeModal(box);
    w.desktop().mouseMove(from, {});
    CHECK(entries(h) == before);
    CHECK(pixels() == still);  // the block used to stay at 45 % until the next press
}

TEST_CASE("Song: a block menu left open across Ctrl+Z, Ctrl+Y or a new host state acts on nothing", "[ui][song]") {
    TestHost h;
    MainWindow w(h);
    withSong(h, w, {{0, 1, 0}, {1, 1, 0}, {2, 1, 0}});
    const gemui::Modifiers ctrl{false, true, false};
    // Clicks entry `label` of the open block menu.
    auto pickOpen = [&w](const std::string& label) {
        const auto* list = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
        REQUIRE(list);
        for (size_t k = 0; k < list->items().size(); ++k)
            if (list->items()[k].label == label) {
                const gemui::Point p = centre(list->itemRect(int(k)));
                w.desktop().mouseMove(p, {});
                w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
                w.desktop().mouseUp(p, {});
                return;
            }
        FAIL("missing entry: " << label);
    };
    pickChip(w, 0, "Remove");
    REQUIRE(entries(h) == SongEntries{{1, 1, 0}, {2, 1, 0}});
    SECTION("Ctrl+Z under the menu of the A3 block: Remove used to take A2") {
        chipItem(w, 1, "Remove");
        CHECK(chr(w, U'z', ctrl));
        REQUIRE(entries(h).size() == 3);
        REQUIRE(w.desktop().hasPopups());
        pickOpen("Remove");
        CHECK(entries(h) == SongEntries{{0, 1, 0}, {1, 1, 0}, {2, 1, 0}});
    }
    SECTION("Ctrl+Y under the menu of the A2 block: Duplicate used to copy A3") {
        CHECK(chr(w, U'z', ctrl));
        REQUIRE(entries(h).size() == 3);
        chipItem(w, 1, "Duplicate");
        CHECK(chr(w, U'y', ctrl));
        REQUIRE(w.desktop().hasPopups());
        pickOpen("Duplicate");
        CHECK(entries(h) == SongEntries{{1, 1, 0}, {2, 1, 0}});
    }
    SECTION("A Song set by the host under the Tempo prompt of a block") {
        pickChip(w, 1, "Tempo...");
        REQUIRE(w.desktop().hasModal());
        h.editExtras([](plugin::NonParamState& s) { s.seq.song.entries = {{3, 1, 0}, {4, 1, 0}}; });
        w.tick(0);
        answer(w, "150");
        CHECK(entries(h) == SongEntries{{3, 1, 0}, {4, 1, 0}});
    }
}
