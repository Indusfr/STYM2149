// STYM2149 — Instruments window and INS row of the Step Sequencer: opening (Edit menu, Ctrl+T, menu
// of the INS row), storing a track's sound, loading into the focused track (one undo step), menu of a slot (Rename,
// Save to Patches, Load from Patches, Clear), INS row and its menu, session.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>

#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Instruments.h"
#include "plugincore/Parameters.h"
#include "plugincore/Patches.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using pluginui::SeqLane;
using plugin::paramIndex;
using plugin::Track;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::string patches;
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
    void screenMidi(uint8_t, uint8_t, uint8_t) override {}
    void setMidiCapture(const plugin::MidiKeys&, bool) override {}
    bool learnMidi(std::array<uint8_t, 3>&) override { return false; }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
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
    void loadDigidrum(int) override {}
    std::string preference(std::string_view, std::string_view fallback) const override { return std::string(fallback); }
    void setPreference(std::string_view, std::string_view) override {}
    std::string patchLibrary() const override { return patches; }
    void setPatchLibrary(const std::string& json) override { patches = json; }

    float& p(int track, Track f) { return values[size_t(paramIndex(track, f))]; }
    std::vector<plugin::PatchEntry> library() const {
        const auto lib = plugin::parsePatchLibrary(patches);
        REQUIRE(lib);
        return *lib;
    }
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void press(MainWindow& w, gemui::Point p, gemui::MouseButton b = gemui::MouseButton::Left) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, b, {});
    w.desktop().mouseUp(p, {});
}

void click(MainWindow& w, const std::string& id, gemui::MouseButton b = gemui::MouseButton::Left) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, centre(*r), b);
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

// Prompt box open: the proposed text erased, `text` typed, Enter.
void answer(MainWindow& w, const std::string& text) {
    REQUIRE(w.desktop().hasModal());
    for (int i = 0; i < 40; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) {
        gemui::KeyEvent e;
        e.key = gemui::Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
    key(w, gemui::Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
}

// Frontmost open menu.
const gemui::MenuList& menu(MainWindow& w) {
    const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(m);
    return *m;
}

std::vector<std::string> labels(MainWindow& w) {
    std::vector<std::string> out;
    for (const gemui::MenuItem& it : menu(w).items()) out.push_back(it.separator ? "---" : it.label);
    return out;
}

// Click on entry `index` (separators included) of the frontmost open menu.
void pick(MainWindow& w, int index) {
    const gemui::Rect r = menu(w).rowsFrom(index);
    REQUIRE(r.h > 0);
    press(w, {r.x + 20, r.y + 10});
}

const ymseq::Lane& lane0(const TestHost& h) { return h.state.seq.cur().lane(0); }

}  // namespace

TEST_CASE("Instruments: Edit menu, Ctrl+T, empty slots", "[ui][instruments]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.instruments().isOpen());
    CHECK_FALSE(w.rectOf("insWin"));

    // Edit menu: first entry below the rule added after those of the prototype (Samples follows it since Sequences
    // went into the Modulation window).
    w.openMenu(2);
    const std::vector<std::string> edit = labels(w);
    const size_t at = size_t(std::find(edit.begin(), edit.end(), "Instruments") - edit.begin());
    REQUIRE(at + 1 < edit.size());
    REQUIRE(at > 0);
    CHECK(edit[at - 1] == "---");
    CHECK(edit[at + 1] == "Samples");
    CHECK(menu(w).items()[at].key == "^T");
    CHECK(w.rectOf("menuExtra"));
    pick(w, int(at));
    REQUIRE(w.instruments().isOpen());
    CHECK(w.instruments().titleBar().title() == "INSTRUMENTS");
    CHECK(gemui::FloatWindow::topmost(w) == &w.instruments());

    // 32 slots in two columns, $01-$10 then $11-$20, all empty.
    const gemui::Rect win = *w.rectOf("insWin");
    CHECK(win.x == 200);
    CHECK(win.y == 120);
    CHECK(win.w == 400);
    CHECK(win.bottom() < MainWindow::kHeight);
    const gemui::Rect a = *w.rectOf("insSlot0"), b = *w.rectOf("insSlot1"), q = *w.rectOf("insSlot16");
    CHECK(b.x == a.x);
    CHECK(b.y > a.bottom());
    CHECK(q.y == a.y);
    CHECK(q.x > a.right());
    CHECK(w.rectOf("insSlot31")->bottom() < win.bottom());
    CHECK_FALSE(w.rectOf("insSlot32"));
    CHECK(w.instruments().slotText(0) == "01 \xe2\x80\x94");
    CHECK(w.instruments().slotText(31) == "20 \xe2\x80\x94");

    // Escape closes it; Ctrl+T reopens it.
    key(w, gemui::Key::Escape);
    CHECK_FALSE(w.instruments().isOpen());
    CHECK(letter(w, U't', true));
    CHECK(w.instruments().isOpen());
}

TEST_CASE("Instruments: track sound stored, loaded into the focused track, one undo step",
          "[ui][instruments][undo]") {
    TestHost h;
    h.p(0, Track::Volume) = 9;
    h.p(0, Track::Generator) = 1;
    h.p(0, Track::Level) = 40;
    h.state.tracks[0].patch.ornamentOn = true;
    h.state.tracks[0].patch.ornamentPitch[1] = 7;
    h.state.tracks[0].arp.length = 5;
    MainWindow w(h);
    w.instruments().show();

    // Empty slot: name asked ("Instrument 1" proposed), then the sound of track 1.
    click(w, "insSlot0");
    answer(w, "  Lead  ");
    const plugin::Instrument& a = h.state.instruments[0];
    REQUIRE(a.used);
    CHECK(a.name == "Lead");
    CHECK(a.patch.volume == 9);
    CHECK(a.patch.generator == ymengine::Generator::Sid);
    CHECK(a.patch.ornamentPitch[1] == 7);
    CHECK(a.arp.length == 5);
    CHECK(a.level == 0.4);
    CHECK(w.instruments().slotText(0) == "01 Lead");
    CHECK(h.editor.undo.undoSize() == 0);  // the slots have no undo
    // Empty name or Cancel: nothing is stored.
    click(w, "insSlot1");
    answer(w, "   ");
    CHECK_FALSE(h.state.instruments[1].used);
    click(w, "insSlot1");
    key(w, gemui::Key::Escape);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK_FALSE(h.state.instruments[1].used);
    // Proposed name kept as it is.
    click(w, "insSlot2");
    key(w, gemui::Key::Enter);
    CHECK(h.state.instruments[2].name == "Instrument 3");

    // Track 2 focused: the instrument is loaded into it, one undo step.
    h.values[size_t(paramIndex(plugin::Global::FocusTrack))] = 2;
    w.tick(0);
    click(w, "insSlot0");
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.p(1, Track::Volume) == 9.0f);
    CHECK(h.p(1, Track::Generator) == 1.0f);
    CHECK(h.p(1, Track::Level) == 40.0f);
    CHECK(h.state.tracks[1].patch.ornamentPitch[1] == 7);
    CHECK(h.state.tracks[1].arp.length == 5);
    CHECK(h.editor.undo.undoSize() == 1);
    letter(w, U'z', true);
    CHECK(h.p(1, Track::Volume) == 13.0f);
    CHECK(h.p(1, Track::Level) == 75.0f);
    CHECK(h.state.tracks[1].arp.length == 3);
    CHECK(h.state.instruments[0].used);  // undo does not touch the slots
}

TEST_CASE("Instruments: menu of a slot, Rename, Save to Patches, Load from Patches, Clear", "[ui][instruments]") {
    TestHost h;
    h.p(0, Track::NoisePeriod) = 21;
    h.state.instruments[3] = plugin::captureInstrument("Bass", h.values, h.state, 0);
    MainWindow w(h);
    w.instruments().show();
    CHECK(w.instruments().slotText(3) == "04 Bass");

    // Empty slot: only Store channel and Load from Patches are possible.
    click(w, "insSlot5", gemui::MouseButton::Right);
    CHECK(labels(w) == std::vector<std::string>{"Store channel", "Load into channel", "Rename", "---",
                                                "Save to Patches", "Load from Patches", "---", "Clear"});
    const auto& empty = menu(w).items();
    CHECK_FALSE(empty[0].disabled);
    CHECK(empty[1].disabled);
    CHECK(empty[2].disabled);
    CHECK(empty[4].disabled);
    CHECK_FALSE(empty[5].disabled);
    CHECK(empty[7].disabled);
    key(w, gemui::Key::Escape);

    // Rename.
    click(w, "insSlot3", gemui::MouseButton::Right);
    pick(w, 2);
    answer(w, "Bass 2");
    CHECK(h.state.instruments[3].name == "Bass 2");
    CHECK(w.instruments().slotText(3) == "04 Bass 2");

    // Save to Patches: a patch of the shared library, under the name of the instrument.
    click(w, "insSlot3", gemui::MouseButton::Right);
    pick(w, 4);
    REQUIRE(h.library().size() == 1);
    CHECK(plugin::patchName(h.library()[0]) == "Bass 2");
    CHECK(h.library()[0].find("patch")->find("NP")->number() == 21);

    // Load from Patches into an empty slot: menu of the patches, the instrument takes on the patch.
    click(w, "insSlot5", gemui::MouseButton::Right);
    pick(w, 5);
    CHECK(labels(w) == std::vector<std::string>{"Bass 2"});
    pick(w, 0);
    CHECK(h.state.instruments[5] == h.state.instruments[3]);
    CHECK(w.instruments().slotText(5) == "06 Bass 2");

    // Clear: the slot becomes empty again.
    click(w, "insSlot3", gemui::MouseButton::Right);
    pick(w, 7);
    CHECK(h.state.instruments[3] == plugin::Instrument{});
    CHECK(w.instruments().slotText(3) == "04 \xe2\x80\x94");

    // Empty library: a greyed-out entry says so.
    h.patches.clear();
    click(w, "insSlot7", gemui::MouseButton::Right);
    pick(w, 5);
    REQUIRE(labels(w) == std::vector<std::string>{"Library is empty"});
    CHECK(menu(w).items()[0].disabled);
}

TEST_CASE("INS row: visible with an instrument, step menu, percussion", "[ui][instruments][seq]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    // Without an instrument: no INS row; GEN starts the rows of the sound, under TUN (thirteenth DAW test, item 166).
    CHECK(g.laneRect(0, SeqLane::Ins).w == 0);
    const gemui::Rect first = g.laneRect(0, SeqLane::Fx);

    h.editExtras([&](plugin::NonParamState& s) { s.instruments[0] = plugin::captureInstrument("Lead", h.values, s, 0); });
    w.tick(0);
    const gemui::Rect cell = g.laneRect(2, SeqLane::Ins);
    REQUIRE(cell.w > 0);
    CHECK(cell.y == first.y);  // before GEN and SRC
    CHECK(g.laneRect(0, SeqLane::Fx).y > cell.y);
    CHECK(g.laneRect(0, SeqLane::Mix).y > cell.y);
    CHECK(g.laneRect(0, SeqLane::Tun).y < cell.y);

    // Step menu: Channel (checked), the instrument, Instruments...
    press(w, centre(cell));
    CHECK(labels(w) == std::vector<std::string>{"Channel", "01 Lead", "---", "Instruments..."});
    CHECK(menu(w).checked() == 0);
    pick(w, 1);
    CHECK(lane0(h).ins[2] == 1);
    CHECK(lane0(h).ins[1] == 0);
    CHECK(g.laneRect(2, SeqLane::Ins) == cell);

    // Instrument cleared: the step keeps its number, the menu shows it checked, the row stays so it can be cleared.
    h.editExtras([](plugin::NonParamState& s) { s.instruments[0] = {}; });
    w.tick(0);
    REQUIRE(g.laneRect(2, SeqLane::Ins).w > 0);
    press(w, centre(cell));
    CHECK(labels(w) == std::vector<std::string>{"Channel", "01 (empty)", "---", "Instruments..."});
    CHECK(menu(w).checked() == 1);
    pick(w, 0);
    CHECK(lane0(h).ins[2] == 0);
    CHECK(g.laneRect(2, SeqLane::Ins).w == 0);

    // Last entry: the Instruments window.
    h.editExtras([&](plugin::NonParamState& s) { s.instruments[9] = plugin::captureInstrument("Pad", h.values, s, 0); });
    w.tick(0);
    press(w, centre(g.laneRect(2, SeqLane::Ins)));
    CHECK(labels(w) == std::vector<std::string>{"Channel", "0A Pad", "---", "Instruments..."});
    pick(w, 3);
    CHECK(w.instruments().isOpen());

    // Percussion track (Chipdrum): no INS row.
    h.p(0, Track::Generator) = float(ymengine::Generator::Chipdrum);
    w.tick(0);
    CHECK(g.laneRect(2, SeqLane::Ins).w == 0);
}

TEST_CASE("Instruments: window reopened with the session", "[ui][instruments]") {
    TestHost h;
    {
        MainWindow a(h);
        a.instruments().show();
    }
    {
        MainWindow b(h);
        CHECK(b.instruments().isOpen());
        key(b, gemui::Key::Escape);
        CHECK_FALSE(b.instruments().isOpen());
    }
    MainWindow c(h);
    CHECK_FALSE(c.instruments().isOpen());
}

TEST_CASE("Program Change: the track's instrument shown in the register monitor", "[ui][instruments][program]") {
    TestHost h;
    MainWindow w(h);
    w.tick(0);
    CHECK(w.registerInfo() == "CH 1 / CHIP VOICE / 16CH");
    plugin::EngineStatus t;
    t.program[0] = 0x1A;
    t.program[1] = 3;
    h.pending = t;
    w.tick(0);
    CHECK(w.registerInfo() == "CH 1 / INS 1A / 16CH");
    // Percussion track: no instrument.
    h.p(0, Track::Generator) = float(ymengine::Generator::Chipdrum);
    w.tick(0);
    CHECK(w.registerInfo() == "CH 1 / CHIPDRUM / 16CH");
    h.p(0, Track::Generator) = 0;
    t.program[0] = 0;
    h.pending = t;
    w.tick(0);
    CHECK(w.registerInfo() == "CH 1 / CHIP VOICE / 16CH");
}
