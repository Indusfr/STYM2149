// STYM2149 — Computer Keyboard window: opening, playing on the computer keyboard according to the layout, repeat
// ignored, release of the played note, octave (buttons, Z and X, numeric keypad, drawing), Keep and Follow, drawing
// clicked with the mouse, closing that cuts the playing, place kept.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gemui/Alert.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymseq/Edit.h"
#include "ymseq/Pattern.h"

using namespace stym;
using pluginui::KbdAction;
using pluginui::MainWindow;

namespace {

// Centre of entry `item` of the open menu, where the menu placed it (GEM look).
gemui::Point entryAt(MainWindow& w, int item) {
    const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(m);
    const gemui::Rect e = m->itemRect(item);
    REQUIRE(e.w > 0);
    return {e.x + e.w / 2, e.y + e.h / 2};
}

using Midi = std::array<uint8_t, 3>;

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::vector<Midi> midi;  // messages from the screen
    std::map<std::string, std::string, std::less<>> prefs;
    std::deque<Midi> fromHost;  // notes received from the host
    std::deque<plugin::MonitorMessage> monitor;  // every message received, for the MIDI monitor
    std::optional<plugin::EngineStatus> pending;
    plugin::EngineStatus last;

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
    void screenMidi(uint8_t s, uint8_t d1, uint8_t d2) override { midi.push_back({s, d1, d2}); }
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>& m) override {
        if (fromHost.empty()) return false;
        m = fromHost.front();
        fromHost.pop_front();
        return true;
    }
    bool monitorMidi(plugin::MonitorMessage& m) override {
        if (monitor.empty()) return false;
        m = monitor.front();
        monitor.pop_front();
        return true;
    }
    void seqCommand(SeqCommand, int, int, int) override {}
    const plugin::EngineStatus* engineStatus() override {
        if (!pending) return nullptr;
        last = *pending;
        pending.reset();
        return &last;
    }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return true; }
    std::string midiStatus() const override { return "none"; }
    std::string version() const override { return "test"; }
    void quit() override {}
    bool hasAudioSettings() const override { return false; }
    void openAudioSettings() override {}
    std::string preference(std::string_view key, std::string_view fallback) const override {
        const auto it = prefs.find(key);
        return it == prefs.end() ? std::string(fallback) : it->second;
    }
    void setPreference(std::string_view key, std::string_view value) override { prefs[std::string(key)] = value; }
    std::string patchLibrary() const override { return patches; }
    void setPatchLibrary(const std::string& json) override { patches = json; }
    std::string keyboardLayout() const override { return system; }

    std::string patches;
    std::string system = "qwerty";  // layout of the system's keyboard

    // Notes switched on and off since the last call.
    std::vector<Midi> take() { return std::exchange(midi, {}); }
};

constexpr uint8_t kOn = 0x90, kOff = 0x80;

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

// Key of a character, as the JUCE editor gives it: character produced and the character without Shift in upper case.
gemui::KeyEvent charKey(char32_t c, bool repeat = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = c;
    e.code = int(c >= U'a' && c <= U'z' ? c - 0x20 : c >= 0xE0 && c <= 0xFE ? c - 0x20 : c);
    e.repeat = repeat;
    return e;
}

bool down(MainWindow& w, char32_t c, bool repeat = false) { return w.desktop().keyDown(charKey(c, repeat)); }
void up(MainWindow& w, char32_t c) { w.desktop().keyUp(charKey(c)); }

bool numpad(MainWindow& w, int n) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = char32_t('0' + n);
    e.code = gemui::KeyEvent::kNumpad0 + n;
    return w.desktop().keyDown(e);
}

// Entry of a menu opened below a button (its frame on the button's bottom rule; 20 px entries inside a 2 px
// border).
void pick(MainWindow& w, const std::string& anchor, int index) {
    REQUIRE(w.rectOf(anchor));
    REQUIRE(w.desktop().hasPopups());
    press(w, entryAt(w, index));
    CHECK_FALSE(w.desktop().hasPopups());
}

}  // namespace

TEST_CASE("Computer Keyboard: keys according to the layout (kbdAction)", "[ui][kbd]") {
    using Kind = KbdAction::Kind;
    auto act = [](char32_t c, const char* layout) { return pluginui::kbdAction(charKey(c), layout); };
    // QWERTY (Auto too): A W S E D F T G Y H U J K O L P ; ' from C to the F of the next octave.
    const std::u32string qwerty = U"awsedftgyhujkolp;'";
    for (size_t i = 0; i < qwerty.size(); ++i) {
        for (const char* layout : {"auto", "qwerty"}) {
            const KbdAction a = act(qwerty[i], layout);
            CHECK(a.kind == Kind::Note);
            CHECK(a.value == int(i));
        }
    }
    CHECK(act(U'z', "auto").kind == Kind::OctDown);
    CHECK(act(U'x', "auto").kind == Kind::OctUp);
    CHECK(act(U'm', "auto").kind == Kind::Metro);
    CHECK(act(U'q', "auto").kind == Kind::None);
    // AZERTY: Q Z S E D F T G Y H U J K O L P M ù; W and X for the octave, the comma for the metronome.
    const std::u32string azerty = U"qzsedftgyhujkolpmù";
    for (size_t i = 0; i < azerty.size(); ++i) {
        const KbdAction a = act(azerty[i], "azerty");
        CHECK(a.kind == Kind::Note);
        CHECK(a.value == int(i));
    }
    CHECK(act(U'w', "azerty").kind == Kind::OctDown);
    CHECK(act(U'x', "azerty").kind == Kind::OctUp);
    CHECK(act(U',', "azerty").kind == Kind::Metro);
    CHECK(act(U'a', "azerty").kind == Kind::None);
    // QWERTZ: Z in the place of Y (G#), Y for the octave down, Ö and Ä for the last two notes.
    CHECK(act(U'z', "qwertz").kind == Kind::Note);
    CHECK(act(U'z', "qwertz").value == 8);
    CHECK(act(U'y', "qwertz").kind == Kind::OctDown);
    CHECK(act(U'ö', "qwertz").value == 16);
    CHECK(act(U'ä', "qwertz").value == 17);
    // Shift: the character without Shift counts (Shift+A also plays the C).
    gemui::KeyEvent shifted = charKey(U'A');
    shifted.mods.shift = true;
    CHECK(pluginui::kbdAction(shifted, "auto").kind == Kind::Note);
    // Numeric keypad: a digit of its own.
    gemui::KeyEvent np;
    np.key = gemui::Key::Character;
    np.ch = U'7';
    np.code = gemui::KeyEvent::kNumpad0 + 7;
    CHECK(pluginui::kbdAction(np, "auto").kind == Kind::Numpad);
    CHECK(pluginui::kbdAction(np, "auto").value == 7);
}

TEST_CASE("Computer Keyboard: opening, playing only with the window open, closing that cuts everything", "[ui][kbd]") {
    TestHost h;
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    CHECK_FALSE(k.isOpen());
    CHECK_FALSE(w.rectOf("kbdWin"));

    // Window closed: the note keys do not play and go to the host.
    CHECK_FALSE(down(w, U'a'));
    CHECK(h.take().empty());

    click(w, "kbdMapBtn");
    REQUIRE(k.isOpen());
    CHECK(gemui::FloatWindow::topmost(w) == &k);
    // Boxes of the prototype (#kbdWin at 120, 120; 1,005.5 × 378).
    CHECK(w.rectOf("kbdWin") == gemui::Rect{120, 120, 1006, 378});
    CHECK(w.rectOf("kbdOctDn") == gemui::Rect{188, 168, 59, 24});
    CHECK(w.rectOf("kbdOctUp") == gemui::Rect{253, 168, 59, 24});
    CHECK(w.rectOf("kbdHeldSel") == gemui::Rect{466, 168, 52, 24});
    CHECK(w.rectOf("Escape") == gemui::Rect{134, 202, 39, 39});
    CHECK(w.rectOf("KeyA") == gemui::Rect{209, 331, 39, 39});
    CHECK(w.rectOf("Space") == gemui::Rect{295, 417, 265, 39});
    CHECK(w.rectOf("tkl") == gemui::Rect{134, 202, 794, 254});
    CHECK(w.rectOf("tklnum") == gemui::Rect{944, 245, 168, 211});
    CHECK(w.rectOf("Numpad0") == gemui::Rect{944, 417, 82, 39});
    CHECK(k.range().text() == "C2 to F3");

    // A plays C2 (36) at 127, once; the auto-repeat is ignored; the release switches it off.
    CHECK(down(w, U'a'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 36, 127}});
    CHECK(k.drawing().down("KeyA"));
    CHECK(down(w, U'a', true));
    CHECK(h.take().empty());
    up(w, U'a');
    CHECK(h.take() == std::vector<Midi>{{kOff, 36, 0}});
    CHECK_FALSE(k.drawing().down("KeyA"));

    // Escape (absent from the CLOSERS) does not close the window: panic.
    CHECK(down(w, U'e'));
    w.desktop().keyDown([] {
        gemui::KeyEvent e;
        e.key = gemui::Key::Escape;
        return e;
    }());
    CHECK(k.isOpen());
    h.take();

    // The close box cuts the playing on the screen and switches the drawing off.
    CHECK(down(w, U'w'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 37, 127}});
    press(w, {130, 136});
    CHECK_FALSE(k.isOpen());
    CHECK(h.take() == std::vector<Midi>{{0xB0, 120, 0}});
    CHECK_FALSE(k.drawing().down("KeyW"));
    // Released after closing: nothing left to switch off.
    up(w, U'w');
    CHECK(h.take().empty());
}

TEST_CASE("Computer Keyboard: octave by the buttons, Z and X, the keypad and the drawing", "[ui][kbd]") {
    TestHost h;
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    // Z and X work with the window closed (0 to 9), the numeric keypad only with the window open.
    CHECK(w.baseOctave() == 2);
    CHECK(down(w, U'x'));
    CHECK(w.baseOctave() == 3);
    CHECK(down(w, U'x', true));  // repeated: nothing
    CHECK(w.baseOctave() == 3);
    CHECK_FALSE(numpad(w, 7));
    CHECK(w.baseOctave() == 3);
    for (int i = 0; i < 8; ++i) down(w, U'x');
    CHECK(w.baseOctave() == 9);
    // Oct + of the main window brings it back to 6 (the piano stops at 6, as in the prototype), Oct - goes down by one.
    click(w, "octUp");
    CHECK(w.baseOctave() == 6);
    click(w, "octUp");
    CHECK(w.baseOctave() == 6);
    click(w, "octDown");
    CHECK(w.baseOctave() == 5);
    for (int i = 0; i < 4; ++i) down(w, U'x');
    click(w, "kbdMapBtn");
    CHECK(k.range().text() == "C9 to F10 (partly out of the chip range)");

    // Numeric keypad: direct octave; buttons of the window; octave keys and digits of the drawing.
    CHECK(numpad(w, 4));
    CHECK(w.baseOctave() == 4);
    click(w, "kbdOctDn");
    CHECK(w.baseOctave() == 3);
    click(w, "kbdOctUp");
    click(w, "kbdOctUp");
    CHECK(w.baseOctave() == 5);
    click(w, "KeyZ");
    CHECK(w.baseOctave() == 4);
    click(w, "Numpad8");
    CHECK(w.baseOctave() == 8);
    CHECK(k.drawing().key("Numpad8")->cur);
    CHECK_FALSE(k.drawing().key("Numpad4")->cur);
    // At octave 8, C# + to F + are out of the chip's range (beyond C9): hatched, without effect.
    CHECK(k.drawing().key("KeyK")->nope == false);
    CHECK(k.drawing().key("KeyO")->nope);
    CHECK(k.range().text() == "C8 to F9 (partly out of the chip range)");
    CHECK(down(w, U'o'));
    CHECK(h.take().empty());
    // 9: Oct + disabled; 0: Oct - disabled.
    click(w, "Numpad9");
    click(w, "kbdOctUp");
    CHECK(w.baseOctave() == 9);
    click(w, "Numpad0");
    click(w, "kbdOctDn");
    CHECK(w.baseOctave() == 0);
    // Latin names (Note names preference).
    h.prefs["noteNames"] = "1";
    numpad(w, 2);
    CHECK(k.range().text() == "Do2 to Fa3");
}

TEST_CASE("Computer Keyboard: notes held in Keep and in Follow", "[ui][kbd]") {
    TestHost h;
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    k.show();
    // Keep: the held note keeps its pitch on an octave change; the release switches off the one that was played.
    CHECK(down(w, U'a'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 36, 127}});
    CHECK(down(w, U'x'));
    CHECK(h.take().empty());
    CHECK(k.drawing().down("KeyA"));  // key still marked
    up(w, U'a');
    CHECK(h.take() == std::vector<Midi>{{kOff, 36, 0}});

    // Follow, chosen in the menu: the held note moves to the new octave.
    click(w, "kbdHeldSel");
    pick(w, "kbdHeldSel", 1);
    CHECK(h.editor.kbdFollow);
    CHECK(down(w, U'a'));  // octave 3: 48
    CHECK(h.take() == std::vector<Midi>{{kOn, 48, 127}});
    CHECK(down(w, U'z'));
    CHECK(h.take() == std::vector<Midi>{{kOff, 48, 0}, {kOn, 36, 127}});
    up(w, U'a');
    CHECK(h.take() == std::vector<Midi>{{kOff, 36, 0}});
    // An octave key of the drawing does not transpose what is held.
    CHECK(down(w, U'a'));
    h.take();
    click(w, "KeyX");
    CHECK(w.baseOctave() == 3);
    CHECK(h.take().empty());
    up(w, U'a');
    CHECK(h.take() == std::vector<Midi>{{kOff, 36, 0}});
}

TEST_CASE("Computer Keyboard: the drawing plays with the mouse; AZERTY layout", "[ui][kbd]") {
    TestHost h;
    h.prefs["kbdLayout"] = "azerty";
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    k.show();
    CHECK(k.drawing().key("KeyM")->label == ",");
    CHECK(k.drawing().key("Semicolon")->label == "M");
    CHECK(k.drawing().key("Quote")->label == "ù");

    // Press on a key: 118, held until the release or until the pointer leaves.
    const gemui::Point a = centre(*w.rectOf("KeyA"));
    w.desktop().mouseMove(a, {});
    w.desktop().mouseDown(a, gemui::MouseButton::Left, {});
    CHECK(h.take() == std::vector<Midi>{{kOn, 36, 118}});
    CHECK(k.drawing().down("KeyA"));
    w.desktop().mouseDrag({a.x + 2, a.y}, {});
    CHECK(h.take().empty());
    w.desktop().mouseDrag({a.x + 40, a.y}, {});
    CHECK(h.take() == std::vector<Midi>{{kOff, 36, 0}});
    CHECK_FALSE(k.drawing().down("KeyA"));
    w.desktop().mouseUp({a.x + 40, a.y}, {});
    CHECK(h.take().empty());
    // Right button, key without effect: nothing.
    w.desktop().mouseDown(a, gemui::MouseButton::Right, {});
    w.desktop().mouseUp(a, {});
    click(w, "KeyC");
    CHECK(h.take().empty());

    // On the keyboard: Q plays the C, A plays nothing, W goes down one octave, the comma toggles the metronome.
    CHECK(down(w, U'q'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 36, 127}});
    CHECK(k.drawing().down("KeyA"));
    up(w, U'q');
    h.take();
    CHECK_FALSE(down(w, U'a'));
    CHECK(down(w, U'w'));
    CHECK(w.baseOctave() == 1);
    CHECK(down(w, U','));
}

TEST_CASE("Computer Keyboard: Auto takes the system's layout (second DAW test, item 14)", "[ui][kbd]") {
    TestHost h;
    h.system = "azerty";
    MainWindow w(h);
    CHECK(w.keyLayout() == "azerty");  // no preference: Auto
    pluginui::KbdWindow& k = w.keyboardWindow();
    k.show();
    CHECK(k.drawing().key("KeyQ")->label == "A");
    CHECK(k.drawing().key("KeyM")->label == ",");
    CHECK(down(w, U'q'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 36, 127}});
    up(w, U'q');
    h.take();
    CHECK(down(w, U'w'));
    CHECK(w.baseOctave() == 1);

    // The system switches to QWERTZ: the drawing follows at the next tick.
    h.system = "qwertz";
    w.tick(1000);
    CHECK(k.drawing().key("KeyY")->label == "Z");
    // A layout chosen in the window wins over the system's; an unknown system layout reads as QWERTY.
    h.prefs["kbdLayout"] = "qwerty";
    CHECK(w.keyLayout() == "qwerty");
    h.prefs["kbdLayout"] = "auto";
    h.system = "dvorak";
    CHECK(w.keyLayout() == "qwerty");
}

TEST_CASE("Computer Keyboard: the reopened window gets its place back", "[ui][kbd]") {
    TestHost h;
    {
        MainWindow w(h);
        w.keyboardWindow().show();
        const gemui::Point bar{400, 132};
        w.desktop().mouseMove(bar, {});
        w.desktop().mouseDown(bar, gemui::MouseButton::Left, {});
        w.desktop().mouseDrag({380, 162}, {});
        w.desktop().mouseUp({380, 162}, {});
        CHECK(w.rectOf("KeyA") == gemui::Rect{189, 361, 39, 39});  // the drawing follows the window
    }
    MainWindow w(h);
    CHECK(w.keyboardWindow().isOpen());
    CHECK(w.rectOf("kbdWin") == gemui::Rect{100, 150, 1006, 378});
    CHECK(w.rectOf("KeyA") == gemui::Rect{189, 361, 39, 39});
    // The click follows the moved drawing.
    click(w, "KeyS");
    CHECK(h.take() == std::vector<Midi>{{kOn, 38, 118}, {kOff, 38, 0}});
}

TEST_CASE("Host notes: keys lit, step recording, nothing sent back to the engine", "[ui][kbd]") {
    TestHost h;
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    k.show();
    // C2 and D2 received from the host: keys of the drawing lit, no note sent back to the engine (already played).
    h.fromHost = {{0x90, 36, 100}, {0x91, 38, 90}};
    w.tick(0);
    CHECK(k.drawing().down("KeyA"));
    CHECK(k.drawing().down("KeyS"));
    CHECK(h.take().empty());
    // Note off, or note on with zero velocity: switched off.
    h.fromHost = {{0x80, 36, 0}, {0x91, 38, 0}};
    w.tick(0);
    CHECK_FALSE(k.drawing().down("KeyA"));
    CHECK_FALSE(k.drawing().down("KeyS"));
    // Held on the screen then received from the host: nothing more; releasing the key switches the drawing off (keyup).
    CHECK(down(w, U'a'));
    h.fromHost = {{0x90, 36, 100}};
    w.tick(0);
    CHECK(k.drawing().down("KeyA"));
    up(w, U'a');
    CHECK_FALSE(k.drawing().down("KeyA"));
    CHECK(h.take() == std::vector<Midi>{{kOn, 36, 127}, {kOff, 36, 0}});
    h.fromHost = {{0x80, 36, 0}};
    w.tick(0);
    CHECK_FALSE(k.drawing().down("KeyA"));
    // CC 123 from the host: everything goes off.
    h.fromHost = {{0x90, 40, 100}, {0xB0, 123, 0}};
    w.tick(0);
    CHECK_FALSE(k.drawing().down("KeyD"));
    h.take();

    // Routing by channel: only the notes on the focused track's channel (track 1, channel 1) count.
    h.values[size_t(plugin::paramIndex(plugin::Global::MidiRouting))] = float(plugin::MidiRouting::PerChannel);
    w.tick(0);
    h.fromHost = {{0x92, 41, 100}};
    w.tick(0);
    CHECK_FALSE(k.drawing().down("KeyF"));
    h.fromHost = {{0x82, 41, 0}, {0x90, 41, 100}, {0x80, 41, 0}};
    w.tick(0);
    CHECK_FALSE(k.drawing().down("KeyF"));

    // Step input, sequencer window open: the host's note is written at the cursor, which moves on.
    h.editor.stepRec = true;
    w.sequencer().show();
    h.fromHost = {{0x90, 43, 100}, {0x80, 43, 0}};
    w.tick(0);
    CHECK(h.state.seq.cur().lane(0).get(ymseq::Row::Pat, 0) == 7);  // G
    CHECK(h.editor.curStep == 1);
    CHECK(h.take().empty());
}

TEST_CASE("Host notes: the piano follows the octave played on the MIDI keyboard", "[ui][kbd]") {
    TestHost h;
    MainWindow w(h);
    w.tick(0);
    REQUIRE(w.baseOctave() == 2);  // C2 to B5 shown
    // Inside the four octaves shown: nothing moves.
    h.fromHost = {{0x90, 36, 100}, {0x80, 36, 0}, {0x90, 83, 100}, {0x80, 83, 0}};
    w.tick(0);
    CHECK(w.baseOctave() == 2);
    // Above: the octave of the note becomes the highest shown; below, the lowest.
    h.fromHost = {{0x90, 84, 100}};  // C6
    w.tick(0);
    CHECK(w.baseOctave() == 3);
    CHECK(w.piano().held(84));
    h.fromHost = {{0x80, 84, 0}, {0x90, 24, 100}};  // C1
    w.tick(0);
    CHECK(w.baseOctave() == 1);
    CHECK(w.piano().held(24));
    h.fromHost = {{0x80, 24, 0}, {0x90, 120, 100}, {0x80, 120, 0}};  // C9: the piano shows C6 to B9 at most
    w.tick(0);
    CHECK(w.baseOctave() == 6);
    // A key out of the chip's range (C0) is silent: it neither moves the piano nor lights.
    h.fromHost = {{0x90, 12, 100}};
    w.tick(0);
    CHECK(w.baseOctave() == 6);
    CHECK_FALSE(w.piano().held(12));
    h.fromHost = {{0x80, 12, 0}, {0x90, 23, 100}, {0x80, 23, 0}};  // B0, the chip's lowest note
    w.tick(0);
    CHECK(w.baseOctave() == 0);
    // A percussion track keeps its pads: the octave does not move.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    w.tick(0);
    h.fromHost = {{0x90, 84, 100}, {0x80, 84, 0}};
    w.tick(0);
    CHECK(w.baseOctave() == 0);
}

TEST_CASE("Host notes: the drum pads of a controller light the pads of their sound", "[ui][kbd]") {
    TestHost h;
    auto gen = [&](ymengine::Generator g) {
        h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(g);
    };
    gen(ymengine::Generator::Chipdrum);
    MainWindow w(h);
    w.tick(0);
    // Channel 10, note 38: Chipdrum pad 3, lit until its release; no piano key, nothing sent back.
    h.fromHost = {{0x99, 38, 100}};
    w.tick(0);
    CHECK(w.pads().held(2));
    CHECK_FALSE(w.piano().held(38));
    h.fromHost = {{0x89, 38, 0}};
    w.tick(0);
    CHECK_FALSE(w.pads().held(2));
    // A Digidrum pad on a Chipdrum track: no pad of this track lights; on a Digidrum track, its sample's.
    h.fromHost = {{0x99, 45, 100}};
    w.tick(0);
    for (int k = 0; k < 8; ++k) CHECK_FALSE(w.pads().held(k));
    h.fromHost = {{0x89, 45, 0}};
    w.tick(0);
    gen(ymengine::Generator::Digidrum);
    w.tick(0);
    h.fromHost = {{0x99, 45, 100}, {0x99, 36, 100}};
    w.tick(0);
    CHECK(w.pads().held(1));
    CHECK_FALSE(w.pads().held(0));
    // CC 123 from the host: all off.
    h.fromHost = {{0xB9, 123, 0}};
    w.tick(0);
    CHECK_FALSE(w.pads().held(1));
    CHECK(h.take().empty());
    // Off: channel 10 is a keyboard channel again.
    h.values[size_t(plugin::paramIndex(plugin::Global::DrumPads))] = 0;
    gen(ymengine::Generator::Chip);
    w.tick(0);
    h.fromHost = {{0x99, 60, 100}};
    w.tick(0);
    CHECK(w.piano().held(60));
}

TEST_CASE("Host notes: keys 36 to 51 on a drum track light its pads, whatever their channel", "[ui][kbd]") {
    TestHost h;
    auto gen = [&](ymengine::Generator g) {
        h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(g);
    };
    gen(ymengine::Generator::Chipdrum);
    MainWindow w(h);
    w.tick(0);
    // Channel 1, C2 and F2: the Kick and Closed Hat pads, no piano key, nothing sent back.
    h.fromHost = {{0x90, 36, 100}, {0x90, 41, 100}};
    w.tick(0);
    CHECK(w.pads().held(0));
    CHECK(w.pads().held(5));
    CHECK_FALSE(w.piano().held(36));
    h.fromHost = {{0x80, 36, 0}, {0x90, 41, 0}};
    w.tick(0);
    CHECK_FALSE(w.pads().held(0));
    CHECK_FALSE(w.pads().held(5));
    // G#2, a Digidrum sample: no pad of the Chipdrum track; C4, out of the pads: the key, as before.
    h.fromHost = {{0x90, 44, 100}, {0x90, 60, 100}};
    w.tick(0);
    for (int k = 0; k < 8; ++k) CHECK_FALSE(w.pads().held(k));
    CHECK(w.piano().held(60));
    h.fromHost = {{0x80, 44, 0}, {0x80, 60, 0}};
    w.tick(0);
    // A key held when the track becomes melodic: its release puts the pad out; then C2 is a key.
    h.fromHost = {{0x90, 38, 100}};
    w.tick(0);
    CHECK(w.pads().held(2));
    gen(ymengine::Generator::Chip);
    w.tick(0);
    h.fromHost = {{0x80, 38, 0}};
    w.tick(0);
    CHECK_FALSE(w.pads().held(2));
    CHECK_FALSE(w.piano().held(38));
    h.fromHost = {{0x90, 36, 100}};
    w.tick(0);
    CHECK(w.piano().held(36));
    CHECK(h.take().empty());
}

// Recording while playing: the sequencer plays steps of 125 ms, Rec armed, window open. at(ms, step, notes) hands
// the engine status of that step (none when step < 0) and the host's notes to one tick at that time.
struct Recording {
    TestHost h;
    MainWindow w{h};
    Recording() {
        w.tick(0);
        h.editor.record = true;
        w.sequencer().show();
        at(0, 0);
    }
    void at(double ms, int64_t step, std::vector<Midi> notes = {}) {
        if (step >= 0) {
            plugin::EngineStatus t;
            t.seq.playing = true;
            t.seq.step = int(step % 16);
            t.seq.stepAbs = step;
            t.seq.bpm = 120;
            h.pending = t;
        }
        h.fromHost.assign(notes.begin(), notes.end());
        w.tick(ms);
    }
    const ymseq::Lane& lane() const { return h.state.seq.cur().lane(0); }
    // Length of the note at step s, its hold included.
    int len(int s) const {
        ymseq::Lane copy = lane();
        return ymseq::LaneEditor(copy, ymseq::EditContext{}).noteLenAt(s);
    }
};

TEST_CASE("Recording: a note keeps the time its MIDI key was held", "[ui][kbd][rec]") {
    Recording r;
    r.at(125, 1);
    // C4 pressed a fifth into step 1: one step at once, then as long as the key is held.
    r.at(150, -1, {{0x90, 60, 100}});
    CHECK(r.lane().pat[1] == 0);
    CHECK(r.len(1) == 1);
    r.at(250, 2);
    CHECK(r.len(1) == 2);
    r.at(375, 3);
    CHECK(r.len(1) == 3);
    // Released a fifth into step 3: held two steps, so two steps long.
    r.at(400, -1, {{0x80, 60, 0}});
    CHECK(r.len(1) == 2);
    r.at(500, 4);
    r.at(625, 5);
    CHECK(r.len(1) == 2);  // the key is up: nothing follows it any more
    // Pressed at the start of step 5, released past the middle of step 6: rounded up to two steps.
    r.at(626, -1, {{0x90, 62, 100}});
    r.at(750, 6);
    r.at(830, -1, {{0x80, 62, 0}});
    CHECK(r.lane().pat[5] == 2);
    CHECK(r.len(5) == 2);
    // A short tap: one step, even over a note recorded before that held longer.
    r.at(875, 7);
    r.at(880, -1, {{0x90, 64, 100}, {0x80, 64, 0}});
    CHECK(r.len(7) == 1);
    r.at(1000, 8);
    r.at(1125, 9);
    // A key still held at the end of the track stops there; stopping keeps what was written.
    r.at(1130, -1, {{0x90, 65, 100}});
    for (int k = 10; k < 20; ++k) r.at(125.0 * k, k);
    CHECK(r.len(9) == 7);
    plugin::EngineStatus stop;
    r.h.pending = stop;
    r.w.tick(2500);
    r.at(2510, -1, {{0x80, 65, 0}});
    CHECK(r.len(9) == 7);
}

TEST_CASE("Recording: keys played together make a chord", "[ui][kbd][rec]") {
    Recording r;
    r.at(125, 1);
    // C, E and G at once: the root and two chord voices at step 1, as long as the keys.
    r.at(130, -1, {{0x90, 60, 100}, {0x90, 64, 100}, {0x90, 67, 100}});
    CHECK(r.lane().pat[1] == 0);
    CHECK(r.lane().vp(2, 1) == 4);
    CHECK(r.lane().vp(3, 1) == 7);
    r.at(250, 2);
    r.at(375, 3);
    r.at(380, -1, {{0x80, 60, 0}, {0x80, 64, 0}, {0x80, 67, 0}});
    CHECK(r.len(1) == 2);
    CHECK(r.lane().vdur[0][1] == 2);
    CHECK(r.lane().vdur[1][1] == 2);
    // A key pressed while another is held joins it as a voice at its own step; the held note goes on.
    r.at(500, 4, {{0x90, 60, 100}});
    r.at(625, 5, {{0x90, 64, 100}});
    r.at(750, 6, {{0x80, 60, 0}, {0x80, 64, 0}});
    CHECK(r.lane().pat[4] == 0);
    CHECK(r.len(4) == 2);
    CHECK(r.lane().pat[5] < 0);
    CHECK(r.lane().vp(2, 5) == 4);
    CHECK(r.lane().vdur[0][5] == 1);
    // A key of the chord a frame late, the first already released: the same step.
    r.at(875, 7, {{0x90, 62, 100}, {0x80, 62, 0}});
    r.at(880, -1, {{0x90, 65, 100}, {0x80, 65, 0}});
    CHECK(r.lane().pat[7] == 2);
    CHECK(r.lane().vp(2, 7) == 5);

    // Monophonic: the keys played together are all written, the chord voices greyed; a key pressed later ends
    // the held notes where it starts.
    r.h.values[size_t(plugin::paramIndex(0, plugin::Track::Voicing))] = 1;
    r.at(1000, 8, {{0x90, 60, 100}, {0x90, 64, 100}});
    CHECK(r.lane().pat[8] == 0);
    CHECK(r.lane().vp(2, 8) == 4);
    r.at(1125, 9);
    r.at(1250, 10, {{0x90, 67, 100}});
    r.at(1375, 11, {{0x80, 60, 0}, {0x80, 64, 0}, {0x80, 67, 0}});
    CHECK(r.len(8) == 2);
    CHECK(r.lane().vdur[0][8] == 2);
    CHECK(r.lane().pat[10] == 7);
    CHECK(r.lane().vp(2, 10) < 0);
    CHECK(r.len(10) == 1);
}

TEST_CASE("Recording: on a Monophonic track, a chord keeps its lowest key as the root", "[ui][kbd][rec]") {
    Recording r;
    r.h.values[size_t(plugin::paramIndex(0, plugin::Track::Voicing))] = 1;
    r.at(125, 1);
    // G, then E, then C of the same chord: C becomes the root, G and E chord voices, each with the length of its key.
    r.at(130, -1, {{0x90, 67, 100}, {0x90, 64, 100}});
    r.at(140, -1, {{0x90, 60, 100}});
    CHECK(r.lane().pat[1] == 0);
    // Chord voice of step 1 that holds this pitch class, 0 if none.
    auto voiceOf = [&](int pc) {
        for (int v = 2; v <= ymseq::kPolyMax; ++v)
            if (r.lane().vp(v, 1) == pc) return v;
        return 0;
    };
    REQUIRE(voiceOf(4) > 1);
    REQUIRE(voiceOf(7) > 1);
    CHECK(voiceOf(0) == 0);
    r.at(250, 2, {{0x80, 67, 0}});
    r.at(375, 3, {{0x80, 60, 0}, {0x80, 64, 0}});
    CHECK(r.len(1) == 2);
    CHECK(r.lane().vdur[size_t(voiceOf(7) - 2)][1] == 1);
    CHECK(r.lane().vdur[size_t(voiceOf(4) - 2)][1] == 2);
    // Polyphonic again: the same notes, all of them heard.
    r.h.values[size_t(plugin::paramIndex(0, plugin::Track::Voicing))] = 0;
    r.at(500, 4);
    CHECK(r.lane().pat[1] == 0);
    CHECK(r.lane().vp(2, 1) >= 0);
}

TEST_CASE("Recording: the keys played at a step replace its notes, greyed chord voices included",
          "[ui][kbd][rec]") {
    Recording r;
    auto voices = [&](int s) {
        int n = 0;
        for (int v = 2; v <= ymseq::kPolyMax; ++v) n += r.lane().vp(v, s) >= 0;
        return n;
    };
    // A chord C E G at step 1, then the track Monophonic: E and G greyed.
    r.at(125, 1, {{0x90, 60, 100}, {0x90, 64, 100}, {0x90, 67, 100}});
    r.at(130, -1, {{0x80, 60, 0}, {0x80, 64, 0}, {0x80, 67, 0}});
    REQUIRE(voices(1) == 2);
    r.h.values[size_t(plugin::paramIndex(0, plugin::Track::Voicing))] = 1;
    // Next pass: D alone at step 1 replaces the whole chord.
    r.at(2125, 17, {{0x90, 62, 100}});
    r.at(2130, -1, {{0x80, 62, 0}});
    CHECK(r.lane().pat[1] == 2);
    CHECK(voices(1) == 0);
    // Next pass, Polyphonic: F and A replace D.
    r.h.values[size_t(plugin::paramIndex(0, plugin::Track::Voicing))] = 0;
    r.at(4125, 33, {{0x90, 65, 100}, {0x90, 69, 100}});
    r.at(4130, -1, {{0x80, 65, 0}, {0x80, 69, 0}});
    CHECK(r.lane().pat[1] == 5);
    CHECK(voices(1) == 1);
    CHECK(r.lane().vp(2, 1) == 9);
    // G at step 2; next pass, E pressed at step 2 while C is held from step 1: the voice E replaces G.
    r.at(6250, 50, {{0x90, 67, 100}});
    r.at(6255, -1, {{0x80, 67, 0}});
    REQUIRE(r.lane().pat[2] == 7);
    r.at(8125, 65, {{0x90, 60, 100}});
    r.at(8250, 66, {{0x90, 64, 100}});
    r.at(8375, 67, {{0x80, 60, 0}, {0x80, 64, 0}});
    CHECK(r.lane().pat[1] == 0);
    CHECK(voices(1) == 0);
    CHECK(r.lane().pat[2] < 0);
    CHECK(voices(2) == 1);
    CHECK(r.lane().vp(2, 2) == 4);
}

TEST_CASE("Recording: drum pads hit together make one step of both sounds", "[ui][kbd][rec]") {
    Recording r;
    r.h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    r.at(125, 1, {{0x99, 36, 100}, {0x99, 38, 100}});
    CHECK(r.lane().pat[1] == 0);
    CHECK(r.lane().vp(2, 1) == 2);
    // Held over the next steps: still a hit of one step; a pad of the next step is a new hit.
    r.at(250, 2, {{0x99, 37, 100}});
    r.at(375, 3, {{0x89, 36, 0}, {0x89, 38, 0}, {0x89, 37, 0}});
    CHECK(r.lane().tie[1] == 0);
    CHECK(r.lane().pat[2] == 1);
    CHECK(r.lane().vp(2, 2) < 0);
}

TEST_CASE("Recording: keys 36 to 51 on a drum track write the rows of their sounds", "[ui][kbd][rec]") {
    Recording r;
    r.h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    r.at(125, 1, {{0x90, 36, 100}, {0x80, 36, 0}});
    r.at(250, 2, {{0x90, 43, 100}, {0x80, 43, 0}});
    CHECK(r.lane().pat[1] == 0);  // Kick
    CHECK(r.lane().pat[2] == 7);  // Tom 2
    CHECK(r.lane().oct[1] == 0);
    CHECK(r.lane().oct[2] == 0);
    // The Digidrum track writes the samples of the top row.
    r.h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Digidrum);
    r.at(375, 3, {{0x90, 46, 100}, {0x80, 46, 0}});
    CHECK(r.lane().pat[3] == 2);
}

TEST_CASE("Recording: Velocity Off writes VOL 15", "[ui][kbd][rec]") {
    Recording r;
    r.at(125, 1, {{0x90, 60, 40}, {0x80, 60, 0}});
    CHECK(r.lane().vol[1] == 5);  // Accent: the velocity gives the accent
    r.h.values[size_t(plugin::paramIndex(plugin::Global::Velocity))] = 2;
    r.at(250, 2, {{0x90, 62, 40}, {0x80, 62, 0}});
    CHECK(r.lane().vol[2] == 15);
    // Step input too.
    r.h.editor.stepRec = true;
    r.h.editor.curStep = 4;
    r.at(260, -1, {{0x90, 64, 40}, {0x80, 64, 0}});
    CHECK(r.lane().pat[4] == 4);
    CHECK(r.lane().vol[4] == 15);
}

TEST_CASE("Recording: the VOL written follows the Velocity curve, as the engine plays the note",
          "[ui][kbd][rec]") {
    Recording r;
    r.at(125, 1, {{0x90, 60, 40}, {0x80, 60, 0}});
    CHECK(r.lane().vol[1] == 5);  // Linear
    r.h.values[size_t(plugin::paramIndex(plugin::Global::VelocityCurve))] = 1;  // Soft: louder
    r.at(250, 2, {{0x90, 62, 40}, {0x80, 62, 0}});
    CHECK(r.lane().vol[2] > 5);
    r.h.values[size_t(plugin::paramIndex(plugin::Global::VelocityCurve))] = 2;  // Hard: softer
    r.at(375, 3, {{0x90, 64, 40}, {0x80, 64, 0}});
    CHECK(r.lane().vol[3] < 5);
    CHECK(r.lane().vol[3] >= 1);
}

TEST_CASE("Sequencer: the notes of the playing step light the keyboard drawing (tklNotes)", "[ui][kbd]") {
    TestHost h;
    h.editExtras([](plugin::NonParamState& s) {
        ymseq::Lane& l = s.seq.cur().edit(0);
        l.pat[0] = 0;     // C of the track's octave (3): 48
        l.vpat[0][0] = 4; // chord voice: E, 52, for two steps
        l.vdur[0][0] = 2;
        l.tie[1] = 1;     // tie: the keys stay
        l.pat[2] = 7;     // G, 55
    });
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    w.setBaseOctave(3);
    k.show();
    auto play = [&](int step) {
        plugin::EngineStatus t;
        t.seq.playing = step >= 0;
        t.seq.step = step;
        t.seq.stepAbs = step;
        h.pending = t;
        w.tick(0);
    };
    play(0);
    CHECK(k.drawing().down("KeyA"));
    CHECK(k.drawing().down("KeyD"));
    play(1);
    CHECK(k.drawing().down("KeyA"));
    CHECK(k.drawing().down("KeyD"));
    play(2);
    CHECK_FALSE(k.drawing().down("KeyA"));
    CHECK_FALSE(k.drawing().down("KeyD"));
    CHECK(k.drawing().down("KeyG"));
    // Played on the keyboard during playback, the key stays marked by the sequencer after its release.
    CHECK(down(w, U'g'));
    up(w, U'g');
    CHECK(k.drawing().down("KeyG"));
    play(-1);  // stop: everything goes off
    CHECK_FALSE(k.drawing().down("KeyG"));
    CHECK(h.take() == std::vector<Midi>{{kOn, 55, 127}, {kOff, 55, 0}});
}

TEST_CASE("Sequencer: overlapping notes all light the keyboard drawing, each for its own steps", "[ui][kbd]") {
    // Indus's case (21/10/2026): A3 held from step 4 to 8, G3 from step 6 to 10 and F3 from step 8 to 12, the two
    // last as chord voice 2 (the first free voice of the grid at their step). The prototype lit A3 alone.
    TestHost h;
    h.editExtras([](plugin::NonParamState& s) {
        ymseq::Lane& l = s.seq.cur().edit(0);
        l.pat[4] = 9;
        for (int st = 4; st <= 8; ++st) l.tie[size_t(st)] = 1;
        l.vpat[0][6] = 7;
        l.vdur[0][6] = 5;
        l.vpat[0][8] = 5;
        l.vdur[0][8] = 5;
    });
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    w.setBaseOctave(3);
    k.show();
    auto lit = [&](int step) {  // F3, G3, A3 on the drawing (F, G, H keys)
        plugin::EngineStatus t;
        t.seq.playing = true;
        t.seq.step = step;
        t.seq.stepAbs = step;
        h.pending = t;
        w.tick(0);
        std::string out;
        for (const char* key : {"KeyF", "KeyG", "KeyH"}) out += k.drawing().down(key) ? key[3] : '-';
        return out;
    };
    const std::vector<std::string> want = {"---", "---", "---", "---", "--H", "--H", "-GH", "-GH",
                                           "FGH", "FG-", "FG-", "F--", "F--", "---", "---", "---"};
    for (int st = 0; st < 16; ++st) {
        CAPTURE(st);
        CHECK(lit(st) == want[size_t(st)]);
    }
}

TEST_CASE("Sequencer: a chord voice greyed out in the grid does not light the keyboard drawing (3 ch, monophonic)",
          "[ui][kbd]") {
    // C (48) on the root and E (52) as a chord voice of step 1: the engine plays the chord voices only on a
    // polyphonic track in 32-channel mode (polyTrigger), and only those light up.
    auto lit = [](int voiceMode, int voicing) {
        TestHost h;
        h.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = float(voiceMode);
        h.values[size_t(plugin::paramIndex(0, plugin::Track::Voicing))] = float(voicing);
        h.editExtras([](plugin::NonParamState& s) {
            ymseq::Lane& l = s.seq.cur().edit(0);
            l.pat[0] = 0;
            l.vpat[0][0] = 4;
        });
        MainWindow w(h);
        pluginui::KbdWindow& k = w.keyboardWindow();
        w.setBaseOctave(3);
        k.show();
        plugin::EngineStatus t;
        t.seq.playing = true;
        t.seq.step = 0;
        t.seq.stepAbs = 0;
        h.pending = t;
        w.tick(0);
        return std::pair{k.drawing().down("KeyA"), k.drawing().down("KeyD")};
    };
    CHECK(lit(0, 0) == std::pair{true, true});   // 32 ch, polyphonic: the chord
    CHECK(lit(0, 1) == std::pair{true, false});  // monophonic: the root alone
    CHECK(lit(1, 0) == std::pair{true, false});  // Authentic 3ch
    CHECK(lit(2, 0) == std::pair{true, false});  // Dual chip 2x3ch
}

TEST_CASE("Computer Keyboard: pads on a percussion track (fourth DAW test, item 31)", "[ui][kbd]") {
    TestHost h;
    auto gen = [&](ymengine::Generator g) {
        h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(g);
    };
    gen(ymengine::Generator::Chipdrum);
    h.editExtras([](plugin::NonParamState& s) { s.seq.cur().edit(0).pat[2] = 3; });  // step 3: Hat (pad 4)
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    k.show();
    const pluginui::TklDrawing& d = k.drawing();

    // Chipdrum: pads 1 to 8 from A to K, engraved with their sound; the other note keys, Z, X and the keypad digits
    // have no role; the octave settings are greyed out.
    CHECK(k.range().text() == "Pads 1 to 8: Kick to Tom 2");
    const std::array<const char*, 8> codes = {"KeyA", "KeyS", "KeyD", "KeyF", "KeyG", "KeyH", "KeyJ", "KeyK"};
    const std::array<const char*, 8> subs = {"Kick", "Snr", "Clap", "Hat", "OHat", "CHat", "Tom1", "Tom2"};
    for (int i = 0; i < 8; ++i) {
        CAPTURE(i);
        const pluginui::TklDrawing::Key* key = d.key(codes[size_t(i)]);
        REQUIRE(key);
        CHECK(key->kind == pluginui::TklDrawing::Kind::Note);
        CHECK(key->pad == i);
        CHECK(key->sub == subs[size_t(i)]);
    }
    CHECK(d.key("KeyA")->tip == "Kick: play it. Shape it in the Drum Kit window (Ctrl+K).");
    for (const char* dead : {"KeyW", "KeyE", "KeyT", "KeyY", "KeyU", "KeyO", "KeyL", "KeyP", "Semicolon", "Quote",
                             "KeyZ", "KeyX", "Numpad3"}) {
        CAPTURE(dead);
        CHECK(d.key(dead)->kind == pluginui::TklDrawing::Kind::Dead);
    }
    click(w, "kbdOctUp");
    click(w, "kbdOctDn");
    click(w, "Numpad5");
    CHECK(w.baseOctave() == 2);

    // A pad key plays the note of the pad, like the pads of the main window, and lights that pad; W plays nothing.
    CHECK(down(w, U'a'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 48, 118}});
    CHECK(d.down("KeyA"));
    CHECK(w.pads().held(0));
    CHECK(down(w, U'w'));
    CHECK(h.take().empty());
    CHECK(down(w, U'k'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 55, 118}});
    up(w, U'a');
    up(w, U'k');
    CHECK(h.take() == std::vector<Midi>{{kOff, 48, 0}, {kOff, 55, 0}});
    CHECK_FALSE(d.down("KeyA"));
    CHECK_FALSE(w.pads().held(0));
    // Changing the octave in Follow does not move a held pad.
    h.editor.kbdFollow = true;
    CHECK(down(w, U'd'));
    h.take();
    w.setBaseOctave(4);
    CHECK(h.take().empty());
    up(w, U'd');
    CHECK(h.take() == std::vector<Midi>{{kOff, 50, 0}});
    // The pad played by the sequencer lights its key.
    plugin::EngineStatus t;
    t.seq.playing = true;
    t.seq.step = 2;
    t.seq.stepAbs = 2;
    h.pending = t;
    w.tick(0);
    CHECK(d.down("KeyF"));
    t.seq.playing = false;
    t.seq.step = -1;
    t.seq.stepAbs = -1;
    h.pending = t;
    w.tick(0);
    CHECK_FALSE(d.down("KeyF"));

    // Digidrum: the slots; a pad key selects its slot and plays it.
    gen(ymengine::Generator::Digidrum);
    h.editExtras([](plugin::NonParamState& s) { s.digidrums[1] = {7, 8, 9}; });
    w.tick(0);
    CHECK(k.range().text() == "Pads 1 to 8: Digidrum slots 1 to 8");
    CHECK(d.key("KeyD")->sub == "Slot 3");
    CHECK(d.key("KeyD")->tip == "Pad 3: Digidrum 3: play it and select it for editing. No sample loaded yet.");
    CHECK(d.key("KeyS")->tip == "Pad 2: Digidrum 2: play it and select it for editing.");
    CHECK(down(w, U'd'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 50, 118}});
    CHECK(h.state.tracks[0].patch.digidrumSlot == 2);
    up(w, U'd');
    h.take();

    // Back to a melodic track: the notes and the octave settings come back.
    gen(ymengine::Generator::Chip);
    w.tick(0);
    CHECK(d.key("KeyA")->sub == "C");
    CHECK(d.key("KeyW")->kind == pluginui::TklDrawing::Kind::Sharp);
    CHECK(k.range().text() == "C4 to F5");
    CHECK(down(w, U'a'));
    CHECK(h.take() == std::vector<Midi>{{kOn, 60, 127}});
    up(w, U'a');
}

TEST_CASE("Computer Keyboard: a key held with the mouse is released when an alert opens", "[ui][kbd]") {
    TestHost h;
    MainWindow w(h);
    pluginui::KbdWindow& k = w.keyboardWindow();
    k.show();
    const gemui::Point a = centre(*w.rectOf("KeyA"));
    w.desktop().mouseMove(a, {});
    w.desktop().mouseDown(a, gemui::MouseButton::Left, {});
    const std::vector<Midi> on = h.take();
    REQUIRE(on.size() == 1);
    REQUIRE(on[0][0] == kOn);
    REQUIRE(k.drawing().down("KeyA"));
    // An alert opens under the held key (end of a background export): the note stops, as on a release.
    gemui::Widget& box = w.desktop().openModal(std::make_unique<gemui::Widget>());
    CHECK(h.take() == std::vector<Midi>{{kOff, on[0][1], 0}});
    CHECK_FALSE(k.drawing().down("KeyA"));
    w.desktop().mouseUp(a, {});
    w.desktop().closeModal(box);
    CHECK(h.take().empty());
    // The key plays again at the next press (it used to stay held, and that press was ignored).
    w.desktop().mouseDown(a, gemui::MouseButton::Left, {});
    CHECK(h.take() == on);
    w.desktop().mouseUp(a, {});
    CHECK(h.take() == std::vector<Midi>{{kOff, on[0][1], 0}});
}

TEST_CASE("MIDI monitor: the messages received listed under Note, the most recent at the bottom",
          "[ui][kbd][menu]") {
    // Indus, 28/10/2026 00:13: a MIDI monitor under Note in the main window, with its On/Off entry in Options.
    TestHost h;
    MainWindow w(h);
    w.tick(0);
    CHECK(w.midiLog().empty());
    const auto note = w.rectOf("noteRow"), row = w.rectOf("midiRow"), log = w.rectOf("midiLog");
    REQUIRE(note);
    REQUIRE(row);
    REQUIRE(log);
    CHECK(row->y > note->bottom());
    CHECK(log->bottom() <= w.rectOf("sysFrame")->bottom() - 12);
    CHECK(log->h >= 6 * pluginui::MidiLog::kLine + 8);  // six lines at least
    // 28/10/2026 08:19: lines of the 8×8 font, a scroll bar at the right edge of the box.
    pluginui::MidiLog& mon = w.midiMonitor();
    CHECK(mon.rows() == int((log->h - 8) / pluginui::MidiLog::kLine));
    CHECK(mon.bar().bounds().right() == log->right());
    CHECK(mon.bar().bounds().h == log->h);

    // A SysEx (28/10/2026 08:25): its length, then its first data bytes.
    const uint8_t identity[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    h.monitor = {{0x90, 60, 100}, {0x90, 60, 0}, {0x80, 61, 64}, {0x99, 36, 127}, {0xA0, 62, 30},
                 {0xB0, 21, 74},  {0xC0, 5, 0},  {0xD0, 90, 0},  {0xE0, 0, 0x40}, {0xEF, 0x7F, 0x7F},
                 {0xE0, 0, 0},    {0xFA, 0, 0},  {0xFC, 0, 0},   plugin::monitorMessage(identity, 6),
                 {0xF2, 0x10, 0x01}};
    w.tick(10);
    CHECK(h.monitor.empty());
    CHECK(w.midiLog() == std::vector<std::string>{"Ch1  Note on C4 100", "Ch1  Note off C4 0", "Ch1  Note off C#4 64",
                                                  "Ch10 Note on C2 127", "Ch1  Poly AT D4 30", "Ch1  CC 21 74",
                                                  "Ch1  Program 5", "Ch1  Pressure 90", "Ch1  Pitch bend +0",
                                                  "Ch16 Pitch bend +8191", "Ch1  Pitch bend -8192", "Start", "Stop",
                                                  "SysEx 6 bytes", "  7E 7F 06 01", "Song position 144"});
    // The same message again and again: one line counts it. A longer SysEx: six bytes, then "..".
    const uint8_t exquis[] = {0xF0, 0x00, 0x21, 0x7E, 0x7F, 0x06, 0x01, 0x10, 0x20, 0xF7};
    const plugin::MonitorMessage sx = plugin::monitorMessage(exquis, 10);
    CHECK(plugin::sysExLength(sx) == 10);
    h.prefs["noteNames"] = "1";  // note names in the chosen naming (Settings > Note names)
    h.monitor = {sx, sx, sx, {0x90, 69, 1}, {0xD0, 90, 0}, {0xD0, 90, 0}, sx};
    w.tick(20);
    std::vector<std::string> lines = w.midiLog();
    REQUIRE(lines.size() == 16 + 8);
    CHECK(std::vector<std::string>(lines.end() - 8, lines.end()) ==
          std::vector<std::string>{"SysEx 10 bytes", "  00 21 7E 7F 06 01..", "  repeated x3", "Ch1  Note on La4 1",
                                   "Ch1  Pressure 90", "  repeated x2", "SysEx 10 bytes", "  00 21 7E 7F 06 01.."});

    // 500 lines kept by default (Settings > MIDI monitor), the oldest leave first.
    CHECK(mon.kept() == 500);
    for (int i = 0; i < 600; ++i) h.monitor.push_back({0xB0, 1, uint8_t(i & 0x7F)});
    for (int t = 30; !h.monitor.empty(); ++t) w.tick(t);  // 256 messages a tick at most
    lines = w.midiLog();
    REQUIRE(lines.size() == 500);
    CHECK(lines.front() == "Ch1  CC 1 " + std::to_string(100 & 0x7F));
    CHECK(lines.back() == "Ch1  CC 1 " + std::to_string(599 & 0x7F));
    // Scrolled back with the wheel, three lines a notch: the lines shown stay put while others arrive; the bar follows.
    CHECK(mon.back() == 0);
    CHECK(mon.bar().value() == 0);
    w.desktop().mouseWheel(centre(*log), 0, -1, {});
    CHECK(mon.back() == 3);
    CHECK(mon.bar().value() == 3);
    const std::string top = w.midiLog()[size_t(500 - mon.rows() - 3)];
    h.monitor = {{0xB0, 2, 0}};  // the buffer is full: the oldest line leaves, those shown do not move
    w.tick(40);
    CHECK(mon.back() == 4);
    CHECK(w.midiLog()[size_t(500 - mon.rows() - 4)] == top);
    mon.setKept(1000);
    h.monitor = {{0xB0, 3, 0}, {0xB0, 4, 0}};
    w.tick(50);
    CHECK(mon.back() == 6);
    CHECK(mon.bar().value() == 6);
    w.desktop().mouseWheel(centre(*log), 0, 1, {});
    w.desktop().mouseWheel(centre(*log), 0, 1, {});
    CHECK(mon.back() == 0);  // back at the bottom, it follows the new lines again
    mon.scrollBack(100000);
    CHECK(mon.back() == int(w.midiLog().size()) - mon.rows());
    mon.scrollBack(0);
    // Fewer lines kept: the oldest leave at once.
    mon.setKept(100);
    CHECK(w.midiLog().size() == 100);
    CHECK(w.midiLog().back() == "Ch1  CC 4 0");

    // Clear (Indus, 28/10/2026 10:20): on the MIDI label's line, against the right edge of the box; a GEM box
    // asks first, Cancel by default.
    const auto clear = w.rectOf("midiClear");
    REQUIRE(clear);
    CHECK(clear->right() == log->right());
    CHECK(clear->bottom() <= w.rectOf("midiLog")->y);
    CHECK(clear->y >= row->y);
    const gemui::Widget* clearBtn = w.widgetOf("midiClear");
    REQUIRE(clearBtn);
    CHECK(clearBtn->enabled());
    mon.scrollBack(5);
    click(w, "midiClear");
    const auto* box = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
    REQUIRE(box);
    CHECK(box->text() == "Clear the MIDI monitor?\nThe lines it keeps will be lost.");
    REQUIRE(box->buttonCount() == 2);
    press(w, centre(box->button(0).bounds()));  // Cancel
    CHECK(w.midiLog().size() == 100);
    click(w, "midiClear");
    box = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
    REQUIRE(box);
    press(w, centre(box->button(size_t(gemui::Alert::kConfirmOk)).bounds()));
    CHECK(w.midiLog().empty());
    CHECK(mon.back() == 0);
    CHECK_FALSE(clearBtn->enabled());  // greyed out while empty
    h.monitor = {sx, sx};
    w.tick(60);
    CHECK(w.midiLog() == std::vector<std::string>{"SysEx 10 bytes", "  00 21 7E 7F 06 01..", "  repeated x2"});
    CHECK(clearBtn->enabled());

    // Options > MIDI monitor, after Note monitor: On by default; Off hides it (a preference), nothing else moves, the
    // messages are still kept.
    auto entry = [&w]() -> gemui::MenuItem {
        w.openMenu(3);
        const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
        REQUIRE(m);
        const std::vector<gemui::MenuItem> items = m->items();
        w.desktop().closePopups();
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].label == "MIDI monitor") {
                CHECK(items[i - 1].label == "Note monitor");
                return items[i];
            }
        FAIL("no MIDI monitor entry");
        return {};
    };
    gemui::MenuItem m = entry();
    CHECK(m.sub.empty());  // a check mark while shown, no submenu
    CHECK(m.checkable);
    CHECK(m.marked);
    const gemui::Rect regs = *w.rectOf("regs"), bottom = *w.rectOf("mainBottom");
    m.pick();
    CHECK(h.prefs["mainMidi"] == "0");
    CHECK_FALSE(entry().marked);
    CHECK_FALSE(w.rectOf("midiRow"));
    CHECK(*w.rectOf("noteRow") == *note);
    CHECK(*w.rectOf("regs") == regs);
    CHECK(*w.rectOf("mainBottom") == bottom);
    h.monitor = {{0xFB, 0, 0}};
    w.tick(30);
    CHECK(w.midiLog().back() == "Continue");
    {
        MainWindow w2(h);  // kept for the next opening
        CHECK_FALSE(w2.rectOf("midiRow"));
    }
    entry().pick();
    CHECK(h.prefs["mainMidi"] == "1");
    CHECK(*w.rectOf("midiRow") == *row);

    // Note monitor Off: the MIDI monitor takes the Note line's place, its box one line taller at most.
    h.prefs["mainNote"] = "0";
    MainWindow w3(h);
    const auto up = w3.rectOf("midiRow"), upLog = w3.rectOf("midiLog");
    REQUIRE(up);
    REQUIRE(upLog);
    CHECK(up->y == note->y);
    CHECK(upLog->bottom() <= w3.rectOf("sysFrame")->bottom() - 12);
    CHECK(upLog->h > log->h);
}

TEST_CASE("Record button: CC 117 on channel 16 switches Rec on a press, the release does nothing",
          "[ui][kbd][rec]") {
    TestHost h;
    MainWindow w{h};
    w.tick(0);
    auto send = [&](std::vector<Midi> m, double ms) {
        h.fromHost.assign(m.begin(), m.end());
        w.tick(ms);
    };
    // Press then release: Rec on (and playback with it, like the Rec button).
    send({{0xBF, 117, 127}, {0xBF, 117, 0}}, 10);
    CHECK(h.editor.record);
    CHECK(w.sequencer().playing());
    // Press again: Rec off; the playback goes on.
    send({{0xBF, 117, 127}}, 20);
    CHECK_FALSE(h.editor.record);
    send({{0xBF, 117, 0}}, 30);
    CHECK_FALSE(h.editor.record);
    // The notes held stay lit (it is not an all-cut).
    send({{0x90, 60, 100}, {0xBF, 117, 127}}, 40);
    CHECK(h.editor.record);
    CHECK(w.piano().held(60));
    // Another channel or another controller is not the Record button (the processor does not pass them on).
    CHECK(plugin::isRecordCc(0xBF, 117));
    CHECK_FALSE(plugin::isRecordCc(0xB0, 117));
    CHECK_FALSE(plugin::isRecordCc(0xBF, 116));
    CHECK_FALSE(plugin::isRecordCc(0x9F, 117));
}
