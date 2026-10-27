// STYM2149 — Drum Kit window: opening (button, Ctrl+K, Edit menu), content inert outside Chipdrum, choice of
// the instrument, settings (slider, typed input), Audition, Reset, Kits, kit name, undo.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using ymengine::DrumSlot;
using ymengine::kKitBanks;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::vector<std::pair<SeqCommand, int>> commands;

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
    void seqCommand(SeqCommand c, int a, int, int) override { commands.emplace_back(c, a); }
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

    const ymengine::DrumParams& drum(DrumSlot s) const { return state.kit.inst[size_t(s)]; }
};

const ymengine::KitParams& bank(std::string_view name) {
    for (const auto& b : kKitBanks)
        if (b.name == name) return b.kit;
    FAIL("unknown bank");
    return kKitBanks[0].kit;
}

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

void drag(MainWindow& w, std::initializer_list<gemui::Point> path) {
    const gemui::Point first = *path.begin();
    w.desktop().mouseMove(first, {});
    w.desktop().mouseDown(first, gemui::MouseButton::Left, {});
    gemui::Point last = first;
    for (const gemui::Point& p : path) {
        w.desktop().mouseDrag(p, {});
        last = p;
    }
    w.desktop().mouseUp(last, {});
}

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

bool letter(MainWindow& w, char32_t ch, bool ctrl = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = ch;
    e.code = int(ch - U'a' + U'A');
    e.mods.ctrl = ctrl;
    return w.desktop().keyDown(e);
}

void type(MainWindow& w, const gemui::Rect& field, const std::string& text) {
    press(w, centre(field));
    for (int i = 0; i < 4; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) {
        gemui::KeyEvent e;
        e.key = gemui::Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
    key(w, gemui::Key::Enter);
}

// Drag of a setting's thumb to the right end of the track.
void dragToMax(MainWindow& w, int row) {
    const gemui::Slider& s = w.drumKit().slider(row);
    const gemui::Rect t = s.thumbRect(), r = s.bounds();
    drag(w, {centre(t), {r.x + r.w / 2, t.y + 8}, {r.right() + 20, t.y + 8}});
}

}  // namespace

TEST_CASE("Drum Kit: button, Ctrl+K and Edit menu open the window; boxes of the prototype", "[ui][kit]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.drumKit().isOpen());
    w.pickGenerator(int(ymengine::Generator::Chipdrum));
    w.tick(0);
    click(w, "openKitBtn");
    REQUIRE(w.drumKit().isOpen());
    CHECK_FALSE(w.desktop().hasModal());  // no more "not available yet" alert
    CHECK(w.drumKit().titleBar().title() == "DRUM KIT");
    // Chipdrum: Kick, five settings (CSS boxes of the prototype at 100 %).
    CHECK(w.rectOf("kitWin") == gemui::Rect{190, 150, 570, 232});
    CHECK_FALSE(w.rectOf("kitOff"));
    CHECK(w.rectOf("kitInst0") == gemui::Rect{204, 190, 52, 24});
    CHECK(w.rectOf("kitInst7") == gemui::Rect{687, 190, 59, 24});
    CHECK(w.drumKit().rows() == 5);
    CHECK(w.drumKit().slider(0).bounds() == gemui::Rect{264, 222, 450, 16});
    CHECK(w.drumKit().valueField(4).bounds() == gemui::Rect{722, 310, 24, 16});
    CHECK(w.rectOf("kitPlay") == gemui::Rect{204, 344, 80, 24});
    CHECK(w.rectOf("kitBank") == gemui::Rect{359, 344, 52, 24});
    CHECK(w.rectOf("kitName")->right() == 746);
    CHECK(w.rectOf("kitName")->y == 353);

    key(w, gemui::Key::Escape);
    CHECK_FALSE(w.drumKit().isOpen());
    CHECK(letter(w, U'k', true));
    CHECK(w.drumKit().isOpen());
    key(w, gemui::Key::Escape);
    w.openMenu(2);  // Edit > Drum kit (eighth entry since Effects, below the bar)
    const gemui::Rect edit = *w.rectOf("mEdit");
    press(w, {edit.x + 20, edit.bottom() + 2 + 7 * 20 + 10});
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK(w.drumKit().isOpen());
}

TEST_CASE("Drum Kit: Edit > Drum kit and Ctrl+K only on a Chipdrum track (second DAW test, item 18)",
          "[ui][kit][menu]") {
    TestHost h;
    MainWindow w(h);
    auto kitEntry = [&w] {
        w.openMenu(2);
        const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
        REQUIRE(m);
        const auto it = std::find_if(m->items().begin(), m->items().end(),
                                     [](const gemui::MenuItem& i) { return i.label == "Drum kit"; });
        REQUIRE(it != m->items().end());
        const gemui::MenuItem kit = *it;
        w.desktop().closePopups();
        return kit;
    };
    using ymengine::Generator;
    for (const Generator g : {Generator::Chip, Generator::Sid, Generator::Digidrum, Generator::SyncBuzzer}) {
        INFO("generator " << int(g));
        w.pickGenerator(int(g));
        w.tick(0);
        const gemui::MenuItem kit = kitEntry();
        CHECK(kit.disabled);
        CHECK(kit.why == "Used by Chipdrum tracks only");
        CHECK(letter(w, U'k', true));  // taken, without opening the window
        CHECK_FALSE(w.drumKit().isOpen());
    }
    w.pickGenerator(int(Generator::Chipdrum));
    w.tick(0);
    CHECK_FALSE(kitEntry().disabled);
    CHECK(letter(w, U'k', true));
    CHECK(w.drumKit().isOpen());
}

TEST_CASE("Drum Kit: outside Chipdrum, a note and the content inert", "[ui][kit]") {
    TestHost h;
    MainWindow w(h);
    w.drumKit().show();
    // #kitOff (margin 6, padding 8, 2 px rule) above the content, at 40 %.
    CHECK(w.rectOf("kitWin") == gemui::Rect{190, 150, 570, 268});
    CHECK(w.rectOf("kitOff")->y == 196);
    CHECK(w.rectOf("kitBody")->y == 218);
    const ymengine::KitParams before = h.state.kit;
    dragToMax(w, 0);
    click(w, "kitReset");
    click(w, "kitPlay");
    click(w, "kitInst1");
    CHECK(h.state.kit == before);
    CHECK(h.commands.empty());
    CHECK(h.editor.kitInst == 0);
    CHECK(h.editor.undo.undoSize() == 0);
    // The track switches to Chipdrum: the note goes away, the content responds.
    w.pickGenerator(int(ymengine::Generator::Chipdrum));
    w.tick(0);
    CHECK_FALSE(w.rectOf("kitOff"));
    CHECK(w.rectOf("kitWin")->h == 232);
    click(w, "kitInst1");
    CHECK(h.editor.kitInst == 1);
}

TEST_CASE("Drum Kit: instrument, settings by slider and typed input, kit name, one step per gesture",
          "[ui][kit][undo]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    MainWindow w(h);
    w.drumKit().show();
    CHECK(w.drumKit().valueField(0).text() == "45");  // Kick, Pitch of the Classic kit
    CHECK(w.rectOf("kitName")->w == 72);              // "Kit: Classic"

    // Clap: four settings, ranges of KITSPEC.
    click(w, "kitInst2");
    REQUIRE(w.drumKit().rows() == 4);
    CHECK(w.drumKit().slider(0).min() == 6);
    CHECK(w.drumKit().slider(0).max() == 20);
    CHECK(w.drumKit().slider(2).max() == 60);  // Tail
    CHECK(w.drumKit().slider(0).tip() == "Gap between the bursts that make up the clap (6 to 20)");
    CHECK(w.drumKit().valueField(2).text() == std::to_string(int(bank("Classic").inst[2].dec)));

    // Drag: the tail goes to the end of its range, one step for the whole drag; the kit becomes Custom.
    dragToMax(w, 2);
    CHECK(h.drum(DrumSlot::Clap).dec == 60);
    CHECK(w.drumKit().valueField(2).text() == "60");
    CHECK(h.editor.undo.undoSize() == 1);
    CHECK(w.rectOf("kitName")->w == 66);  // "Kit: Custom"

    // Typed input (comma or point): rounded, brought into the range, one step.
    type(w, w.drumKit().valueField(0).bounds(), "6,6");
    CHECK(h.drum(DrumSlot::Clap).a == 7);
    type(w, w.drumKit().valueField(1).bounds(), "999");
    CHECK(h.drum(DrumSlot::Clap).b == 80);
    CHECK(h.editor.undo.undoSize() == 3);

    // Undo, then back to the Classic kit.
    letter(w, U'z', true);
    CHECK(h.drum(DrumSlot::Clap).b == bank("Classic").inst[2].b);
    CHECK(h.drum(DrumSlot::Clap).a == 7);
    letter(w, U'z', true);
    letter(w, U'z', true);
    CHECK(h.state.kit == bank("Classic"));
    CHECK(w.rectOf("kitName")->w == 72);
    letter(w, U'y', true);
    CHECK(h.drum(DrumSlot::Clap).dec == 60);
}

TEST_CASE("Drum Kit: Audition, Reset of the instrument, banks of the Kits menu", "[ui][kit][undo]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    MainWindow w(h);
    w.drumKit().show();
    click(w, "kitInst3");  // Hat
    click(w, "kitPlay");
    REQUIRE(h.commands.size() == 1);
    CHECK(h.commands[0].first == pluginui::Host::SeqCommand::DrumPreview);
    CHECK(h.commands[0].second == int(DrumSlot::Hat));
    CHECK(h.editor.undo.undoSize() == 0);

    // Kits: 13 banks sorted ignoring case; TR-808-ish (eleventh).
    click(w, "kitBank");
    REQUIRE(w.desktop().hasPopups());
    const gemui::Rect b = *w.rectOf("kitBank");
    press(w, {b.x + 20, b.bottom() - 2 + 2 + 20 * 10 + 10});
    CHECK_FALSE(w.desktop().hasPopups());
    CHECK(h.state.kit == bank("TR-808-ish"));
    CHECK(h.editor.undo.undoSize() == 1);

    // Reset: the factory values (Classic) of the shown instrument only.
    click(w, "kitReset");
    CHECK(h.drum(DrumSlot::Hat) == bank("Classic").inst[size_t(DrumSlot::Hat)]);
    CHECK(h.drum(DrumSlot::Kick) == bank("TR-808-ish").inst[size_t(DrumSlot::Kick)]);
    CHECK(h.editor.undo.undoSize() == 2);
    letter(w, U'z', true);
    CHECK(h.state.kit == bank("TR-808-ish"));
    letter(w, U'z', true);
    CHECK(h.state.kit == bank("Classic"));
}

TEST_CASE("Drum Kit: kit name, the first identical bank, otherwise Custom", "[ui][kit]") {
    ymengine::KitParams k = bank("Techno");
    CHECK(pluginui::kitLabel(k) == "Techno");
    k.inst[0].vol = 99;
    CHECK(pluginui::kitLabel(k) == "Custom");
    CHECK(pluginui::kitLabel(kKitBanks[0].kit) == "Classic");
}

TEST_CASE("Drum Kit: the reopened window gets its place and its instrument back", "[ui][kit]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    {
        MainWindow w(h);
        w.drumKit().show();
        click(w, "kitInst6");
        const gemui::Point bar{300, 162};
        w.desktop().mouseMove(bar, {});
        w.desktop().mouseDown(bar, gemui::MouseButton::Left, {});
        w.desktop().mouseDrag({320, 192}, {});
        w.desktop().mouseUp({320, 192}, {});
    }
    MainWindow w(h);
    CHECK(w.drumKit().isOpen());
    CHECK(w.rectOf("kitWin")->x == 210);
    CHECK(w.rectOf("kitWin")->y == 180);
    CHECK(h.editor.kitInst == 6);
}
