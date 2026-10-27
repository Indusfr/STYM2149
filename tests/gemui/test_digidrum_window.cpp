// STYM2149 — Digidrum editor: opening (Edit > Samples), slots, loading and listening, name, rate and
// presets, root note, fine tuning, sustain loop, 4-bit conversion, tools, undo steps (settings alone or bytes),
// named banks (save, reload, delete, unreadable file), session; Digidrum frame of the main window (rate of the slot)
// and tooltip of the SID ratio.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Alert.h"
#include "gemui/Menu.h"
#include "plugincore/DigidrumBanks.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "pluginui/Widgets.h"
#include "ymengine/Params.h"

using namespace stym;
using pluginui::DigidrumWindow;
using pluginui::MainWindow;
using plugin::DigiConvert;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::vector<int> loads, previews, stops;
    std::optional<plugin::EngineStatus> pending;  // next status read by tick
    plugin::EngineStatus last;
    std::string library;

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
    void seqCommand(SeqCommand c, int a, int b, int) override {
        if (c == SeqCommand::DigiPreview) (b ? stops : previews).push_back(a);
    }
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
    void loadDigidrum(int slot) override { loads.push_back(slot); }
    void loadDmaSample(int) override {}
    std::string preference(std::string_view, std::string_view fallback) const override { return std::string(fallback); }
    void setPreference(std::string_view, std::string_view) override {}
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}
    std::string digidrumLibrary() const override { return library; }
    void setDigidrumLibrary(const std::string& json) override { library = json; }

    const plugin::DigidrumSlotState& slot(int k) const { return state.digiSlots[size_t(k)]; }
    // Loading of a file as the plugin stores it (PluginEditor::loadDigidrum): 8-bit source, Linear values, loop over
    // the whole sample, file name.
    void loaded(int k, std::vector<int8_t> src, const std::string& name) {
        editExtras([&](plugin::NonParamState& s) {
            plugin::DigidrumSlotState& d = s.digiSlots[size_t(k)];
            s.digidrums[size_t(k)] = plugin::digidrumNibbles(src, d.convert);
            d.name = name;
            d.play.start = 0;
            d.play.end = uint32_t(src.size());
            d.source = std::move(src);
        });
    }
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

// Drag of the thumb of an editor slider to the right (or left) end of its track.
void dragSlider(MainWindow& w, const char* id, bool toMax) {
    const gemui::Slider* s = w.digidrums().slider(id);
    REQUIRE(s);
    const gemui::Rect t = s->thumbRect(), r = s->bounds();
    drag(w, {centre(t), {r.x + r.w / 2, t.y + 8}, {toMax ? r.right() + 20 : r.x - 20, t.y + 8}});
}

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

bool undoKey(MainWindow& w, bool redo = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = redo ? U'y' : U'z';
    e.code = redo ? 'Y' : 'Z';
    e.mods.ctrl = true;
    return w.desktop().keyDown(e);
}

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

void pick(MainWindow& w, int index) {
    const gemui::Rect r = menu(w).rowsFrom(index);
    REQUIRE(r.h > 0);
    press(w, {r.x + 20, r.y + 10});
}

std::vector<int8_t> wave(size_t n) {
    std::vector<int8_t> b(n);
    for (size_t i = 0; i < n; ++i) b[i] = int8_t(int(i * 7 % 200) - 100);
    return b;
}

void open(TestHost& h, MainWindow& w) {
    (void)h;
    w.digidrums().show();
    REQUIRE(w.digidrums().isOpen());
}

}  // namespace

TEST_CASE("Digidrums: Edit > Samples menu, window and its elements", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.digidrums().isOpen());
    w.openMenu(2);
    const std::vector<std::string> edit = labels(w);
    const size_t at = size_t(std::find(edit.begin(), edit.end(), "Samples") - edit.begin());
    REQUIRE(at < edit.size());
    pick(w, int(at));
    REQUIRE(labels(w).front() == "Digidrums");
    pick(w, 0);
    REQUIRE(w.digidrums().isOpen());
    CHECK(w.digidrums().titleBar().title() == "DIGIDRUMS");
    const gemui::Rect win = *w.rectOf("digWin");
    CHECK(win.x == DigidrumWindow::kX);
    CHECK(win.y == DigidrumWindow::kY);
    CHECK(win.right() <= MainWindow::kWidth);
    CHECK(win.bottom() <= MainWindow::kHeight);
    for (const char* id : {"digSlot0", "digSlot7", "digName", "digBanks", "digWave", "digLoad", "digPlay", "digClear",
                           "digRate", "digPreset0", "digPreset2", "digRoot", "digFine", "digLoop", "digStart", "digEnd",
                           "digLinear", "digYm", "digNormalize", "digReverse", "digFade", "digSign", "digGainDown",
                           "digGainUp"}) {
        INFO(id);
        const auto r = w.rectOf(id);
        REQUIRE(r);
        CHECK(r->x > win.x);
        CHECK(r->y > win.y + 20);
        CHECK(r->right() < win.right());
        CHECK(r->bottom() < win.bottom());
    }
    // Defaults: slot 1, 7,800 Hz (preset lit), C-4, +0 ct, empty slot (tools off).
    CHECK(w.digidrums().slot() == 0);
    CHECK(w.digidrums().valueText("digRate") == "7800 Hz");
    CHECK(w.digidrums().valueText("digRoot") == "C4");
    CHECK(w.digidrums().valueText("digFine") == "0 ct");
    CHECK(!w.digidrums().slider("digStart")->enabled());

    click(w, "digSlot3");
    CHECK(h.editor.digiSlot == 3);
}

TEST_CASE("Digidrums: window and slot reopened with the session", "[ui][digidrum]") {
    TestHost h;
    {
        MainWindow a(h);
        a.digidrums().show();
        click(a, "digSlot5");
    }
    {
        MainWindow b(h);
        CHECK(b.digidrums().isOpen());
        CHECK(b.digidrums().slot() == 5);
        key(b, gemui::Key::Escape);
        CHECK_FALSE(b.digidrums().isOpen());
    }
    MainWindow c(h);
    CHECK_FALSE(c.digidrums().isOpen());
}

TEST_CASE("Digidrums: loading, listening, Clear and its undo step", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    click(w, "digSlot2");
    click(w, "digWave");
    click(w, "digLoad");
    CHECK(h.loads == std::vector<int>{2, 2});
    CHECK(h.editor.undo.undoSize() == 0);  // loading cannot be undone
    h.loaded(2, wave(300), "snare.wav");
    w.tick(0);
    click(w, "digPlay");
    CHECK(h.previews == std::vector<int>{2});
    CHECK(w.digidrums().slider("digStart")->enabled());  // Start and End without Loop
    CHECK_FALSE(w.digidrums().playLit());

    click(w, "digClear");
    CHECK_FALSE(w.digidrums().slider("digStart")->enabled());
    CHECK(h.state.digidrums[2].empty());
    CHECK(h.slot(2).name.empty());
    CHECK(h.slot(2).source.empty());
    CHECK(h.editor.undo.undoSize() == 1);
    undoKey(w);
    CHECK(h.state.digidrums[2].size() == 300);
    CHECK(h.slot(2).name == "snare.wav");
    CHECK(h.slot(2).source == wave(300));
    undoKey(w, true);
    CHECK(h.state.digidrums[2].empty());
}


TEST_CASE("Digidrums: with Loop on, Play loops the slot; Play again, Loop or closing stops it (Indus, 26/10/2026)", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    click(w, "digSlot2");
    h.loaded(2, wave(300), "snare.wav");
    w.tick(0);
    click(w, "digLoop");
    REQUIRE(h.slot(2).play.loop);
    // Play lit while the engine loops the slot; a second click stops it.
    click(w, "digPlay");
    CHECK(h.previews == std::vector<int>{2});
    plugin::EngineStatus t;
    t.digiPreview = 2;
    h.pending = t;
    w.tick(0);
    CHECK(w.digidrums().playLit());
    click(w, "digPlay");
    CHECK(h.stops == std::vector<int>{2});
    CHECK_FALSE(w.digidrums().playLit());
    t.digiPreview = -1;
    h.pending = t;
    w.tick(0);
    // Loop switched off: Play goes out (the engine stops the loop itself).
    click(w, "digPlay");
    t.digiPreview = 2;
    h.pending = t;
    w.tick(0);
    REQUIRE(w.digidrums().playLit());
    click(w, "digLoop");
    CHECK_FALSE(w.digidrums().playLit());
    CHECK(h.stops == std::vector<int>{2});
    // Closing the window stops the preview.
    click(w, "digLoop");
    CHECK(w.digidrums().playLit());
    w.digidrums().close();
    CHECK(h.stops == std::vector<int>{2, 2});
}
TEST_CASE("Digidrums: name, rate, presets, root note, fine tuning; one step per gesture", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    click(w, "digName");
    answer(w, " Kick 1 ");
    CHECK(h.slot(0).name == "Kick 1");
    CHECK(h.editor.undo.undoSize() == 1);

    click(w, "digPreset2");
    CHECK(h.slot(0).play.rateHz == 11025);
    CHECK(w.digidrums().valueText("digRate") == "11025 Hz");
    click(w, "digPreset2");  // already chosen: no step
    CHECK(h.editor.undo.undoSize() == 2);

    dragSlider(w, "digRate", true);
    CHECK(h.slot(0).play.rateHz == ymengine::kDigidrumRateMax);
    dragSlider(w, "digRoot", false);
    CHECK(h.slot(0).play.root == 0);
    dragSlider(w, "digFine", true);
    CHECK(h.slot(0).play.cents == ymengine::kDigidrumCentsMax);
    CHECK(w.digidrums().valueText("digFine") == "+100 ct");
    CHECK(h.editor.undo.undoSize() == 5);

    undoKey(w);
    CHECK(h.slot(0).play.cents == 0);
    undoKey(w);
    CHECK(h.slot(0).play.root == 60);
    undoKey(w);
    CHECK(h.slot(0).play.rateHz == 11025);
    undoKey(w);
    CHECK(h.slot(0).play.rateHz == ymengine::kDigidrumRate);
    undoKey(w);
    CHECK(h.slot(0).name.empty());
}

TEST_CASE("Digidrums: Start and End with or without Loop, Start before End", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    // Older slot: values, no loop, End at 0 (the end of the sample, shown as its length).
    h.editExtras([](plugin::NonParamState& s) { s.digidrums[0] = std::vector<uint8_t>(500, 8); });
    w.tick(0);
    CHECK(w.digidrums().slider("digStart")->enabled());
    CHECK(w.digidrums().slider("digEnd")->enabled());
    CHECK(w.digidrums().valueText("digStart") == "0");
    CHECK(w.digidrums().valueText("digEnd") == "500");

    // Start at the end: brought back to End - 1 (End written); End at the start: brought back to Start + 1.
    dragSlider(w, "digStart", true);
    CHECK(h.slot(0).play.start == 499);
    CHECK(h.slot(0).play.end == 500);
    CHECK_FALSE(h.slot(0).play.loop);
    dragSlider(w, "digEnd", false);
    CHECK(h.slot(0).play.end == 500);
    dragSlider(w, "digStart", false);
    CHECK(h.slot(0).play.start == 0);
    dragSlider(w, "digEnd", false);
    CHECK(h.slot(0).play.end == 1);
    CHECK(w.digidrums().valueText("digEnd") == "1");
    CHECK(h.editor.undo.undoSize() == 3);  // End already at the lowest allowed: no step
    CHECK(h.slot(0).source.empty());       // settings alone: no source created

    // Loop repeats that part: Start and End stay.
    click(w, "digLoop");
    CHECK(h.slot(0).play.loop);
    CHECK(h.slot(0).play.start == 0);
    CHECK(h.slot(0).play.end == 1);
    CHECK(h.editor.undo.undoSize() == 4);
}

TEST_CASE("Digidrums: 4-bit conversion and tools on the source, steps with the bytes", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    h.loaded(0, {-128, -64, 0, 64, 127}, "hit");
    w.tick(0);
    CHECK(h.state.digidrums[0] == std::vector<uint8_t>{0, 4, 8, 11, 15});

    click(w, "digYm");
    CHECK(h.slot(0).convert == DigiConvert::YmLevels);
    CHECK(h.state.digidrums[0] == std::vector<uint8_t>{0, 11, 13, 14, 15});
    click(w, "digYm");  // already chosen
    CHECK(h.editor.undo.undoSize() == 1);

    click(w, "digReverse");
    CHECK(h.slot(0).source == std::vector<int8_t>{127, 64, 0, -64, -128});
    click(w, "digFade");
    CHECK(h.slot(0).source.back() > -128);
    CHECK(h.slot(0).source.front() == 127);
    click(w, "digGainDown");
    CHECK(h.slot(0).source.front() == 90);  // 127 × 10^(-3/20), rounded
    click(w, "digNormalize");
    CHECK(h.slot(0).source.front() == 127);
    click(w, "digGainUp");
    CHECK(h.editor.undo.undoSize() == 6);

    // A setting alone: undoing it keeps the source and the values of the moment.
    const std::vector<int8_t> src = h.slot(0).source;
    const std::vector<uint8_t> nib = h.state.digidrums[0];
    click(w, "digPreset1");
    CHECK(h.slot(0).play.rateHz == 8000);
    undoKey(w);
    CHECK(h.slot(0).play.rateHz == ymengine::kDigidrumRate);
    CHECK(h.slot(0).source == src);
    CHECK(h.state.digidrums[0] == nib);
    // The tools undone one by one: the source comes back with its values.
    for (int i = 0; i < 4; ++i) undoKey(w);
    CHECK(h.slot(0).source == std::vector<int8_t>{127, 64, 0, -64, -128});
    undoKey(w);
    CHECK(h.slot(0).source == std::vector<int8_t>{-128, -64, 0, 64, 127});
    CHECK(h.state.digidrums[0] == std::vector<uint8_t>{0, 11, 13, 14, 15});
    undoKey(w);
    CHECK(h.slot(0).convert == DigiConvert::Linear);
    CHECK(h.state.digidrums[0] == std::vector<uint8_t>{0, 4, 8, 11, 15});
}

TEST_CASE("Digidrums: Sign of the DMA window; Reverse mirrors Start and End (Indus, 26/10/2026)",
          "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    click(w, "digSign");  // empty slot: greyed out
    CHECK(h.editor.undo.undoSize() == 0);
    h.loaded(0, {-128, -64, 0, 64, 127}, "hit");
    h.editExtras([](plugin::NonParamState& s) {
        s.digiSlots[0].play.start = 1;
        s.digiSlots[0].play.end = 3;
    });
    w.tick(0);
    click(w, "digSign");
    CHECK(h.slot(0).source == std::vector<int8_t>{0, 64, -128, -64, -1});
    click(w, "digReverse");
    CHECK(h.slot(0).source == std::vector<int8_t>{-1, -64, -128, 64, 0});
    CHECK(h.slot(0).play.start == 2);
    CHECK(h.slot(0).play.end == 4);
    CHECK(h.editor.undo.undoSize() == 2);
    undoKey(w);
    CHECK(h.slot(0).play.start == 1);
    CHECK(h.slot(0).play.end == 3);
    undoKey(w);
    CHECK(h.slot(0).source == std::vector<int8_t>{-128, -64, 0, 64, 127});
}

TEST_CASE("Digidrums: -3 dB after a clipped +3 dB gives the source back", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    const std::vector<int8_t> orig = {-128, -64, 0, 64, 127};
    h.loaded(0, orig, "hit");
    w.tick(0);
    click(w, "digGainUp");
    CHECK(h.slot(0).source.back() == 127);
    click(w, "digGainDown");
    CHECK(h.slot(0).source == orig);
    CHECK(h.state.digidrums[0] == plugin::digidrumNibbles(orig, h.slot(0).convert));
}

TEST_CASE("Digidrums: slot without a source, the source derived from the values", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    h.editExtras([](plugin::NonParamState& s) { s.digidrums[1] = {0, 15, 8}; });
    w.tick(0);
    click(w, "digSlot1");
    click(w, "digReverse");
    CHECK(h.state.digidrums[1] == std::vector<uint8_t>{8, 15, 0});
    CHECK(h.slot(1).source == plugin::digidrumSourceOf(std::vector<uint8_t>{8, 15, 0}));
    undoKey(w);
    CHECK(h.state.digidrums[1] == std::vector<uint8_t>{0, 15, 8});
    CHECK(h.slot(1).source.empty());
}

TEST_CASE("Digidrums: named banks in the shared library", "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    // Empty library: Load and Delete unavailable.
    click(w, "digBanks");
    std::vector<std::string> items = labels(w);
    CHECK(items == std::vector<std::string>{"Save bank...", "Load bank", "---", "Delete bank"});
    CHECK(menu(w).items()[1].disabled);
    CHECK(menu(w).items()[3].disabled);
    pick(w, 0);
    h.loaded(0, wave(40), "a");
    w.tick(0);
    answer(w, "Set A");
    REQUIRE(w.digidrums().banks().size() == 1);
    CHECK(w.digidrums().banks()[0].name == "Set A");
    CHECK(w.digidrums().banks()[0].nibbles[0].size() == 40);

    // Same name: replaced; another name: added.
    h.loaded(1, wave(10), "b");
    w.tick(0);
    w.digidrums().saveBank("Set A");
    w.digidrums().saveBank("Set B");
    REQUIRE(w.digidrums().banks().size() == 2);
    CHECK(w.digidrums().banks()[0].nibbles[1].size() == 10);

    // Reloading a bank: confirmation, then the eight slots replaced (no step).
    h.editExtras([](plugin::NonParamState& s) {
        s.digiSlots = {};
        s.digidrums = {};
    });
    const size_t undoBefore = h.editor.undo.undoSize();
    click(w, "digBanks");
    pick(w, 1);  // Load bank submenu
    CHECK(labels(w) == std::vector<std::string>{"Set A", "Set B"});
    pick(w, 0);
    REQUIRE(w.desktop().hasModal());
    key(w, gemui::Key::Escape);  // Cancel
    CHECK(h.state.digidrums[0].empty());
    w.digidrums().loadBank("Set A");
    CHECK(h.state.digidrums[0].size() == 40);
    CHECK(h.slot(0).name == "a");
    CHECK(h.slot(1).source == wave(10));
    CHECK(h.editor.undo.undoSize() == undoBefore);

    // Delete.
    w.digidrums().deleteBank("Set A");
    REQUIRE(w.digidrums().banks().size() == 1);
    CHECK(w.digidrums().banks()[0].name == "Set B");

    // Unreadable file: nothing is overwritten, an alert says so.
    h.library = "{ not json";
    w.digidrums().saveBank("Set C");
    CHECK(h.library == "{ not json");
    CHECK(w.desktop().hasModal());
    key(w, gemui::Key::Enter);
    click(w, "digBanks");
    CHECK(menu(w).items()[0].disabled);
}

namespace {

template <class W>
const W* findWidget(const gemui::Widget& root) {
    if (const auto* w = dynamic_cast<const W*>(&root)) return w;
    for (const gemui::Widget* c : root.children())
        if (const W* w = findWidget<W>(*c)) return w;
    return nullptr;
}

void collectTips(std::vector<std::string>& tips, const gemui::Widget& root) {
    if (!root.tip().empty()) tips.push_back(root.tip());
    for (const gemui::Widget* c : root.children()) collectTips(tips, *c);
}

}  // namespace

TEST_CASE("Digidrums: Delete and Load act on the bank named in the confirmation, even after another instance's edit",
          "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    h.loaded(0, wave(40), "a");
    w.tick(0);
    for (const char* name : {"Set A", "Set B", "Set C"}) w.digidrums().saveBank(name);
    const std::string all = h.library;
    w.digidrums().deleteBank("Set A");
    const std::string withoutA = h.library;  // what another instance deleting "Set A" leaves
    h.library = all;
    auto confirmOk = [&w] {
        const auto* a = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
        REQUIRE(a);
        press(w, centre(a->button(size_t(gemui::Alert::kConfirmOk)).bounds()));
    };
    // Delete bank > "Set B", then the library changes before the confirmation: "Set B" goes, not "Set C".
    click(w, "digBanks");
    pick(w, 3);
    REQUIRE(labels(w) == std::vector<std::string>{"Set A", "Set B", "Set C"});
    pick(w, 1);
    REQUIRE(w.desktop().hasModal());
    h.library = withoutA;
    confirmOk();
    REQUIRE(w.digidrums().banks().size() == 1);
    CHECK(w.digidrums().banks()[0].name == "Set C");
    // A bank deleted elsewhere before the confirmation: nothing loaded.
    h.editExtras([](plugin::NonParamState& s) { s.digidrums = {}; });
    click(w, "digBanks");
    pick(w, 1);
    REQUIRE(labels(w) == std::vector<std::string>{"Set C"});
    pick(w, 0);
    h.library = "[]";
    confirmOk();
    CHECK(h.state.digidrums[0].empty());
}

TEST_CASE("Main window: the Digidrum frame prints the slot's name and own rate, not always 7800 Hz",
          "[ui][digidrum]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Digidrum);
    h.loaded(0, wave(40), "kick");
    MainWindow w(h);
    w.tick(0);
    const auto* frame = findWidget<pluginui::DrumWave>(w);
    REQUIRE(frame != nullptr);
    CHECK(frame->caption() == "slot 1: kick, 40 smp @ 7800 Hz / 4-bit");

    h.editExtras([](plugin::NonParamState& s) { s.digiSlots[0].play.rateHz = 15600; });
    w.tick(100);
    CHECK(frame->caption() == "slot 1: kick, 40 smp @ 15600 Hz / 4-bit");

    h.editExtras([](plugin::NonParamState& s) { s.digidrums[0].clear(); });
    w.tick(200);
    CHECK(frame->caption() == "slot 1: empty");
}

TEST_CASE("Main window: Digidrum samples under slots 5 to 8 opens the editor on the slot; the drawing loads nothing ",
          "[ui][digidrum]") {
    for (const bool m3 : {false, true}) {
        CAPTURE(m3);
        TestHost h;
        const int track = m3 ? 3 : 0;  // Authentic 3ch: the Digi track
        if (m3) h.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
        h.values[size_t(plugin::paramIndex(plugin::Global::FocusTrack))] = float(track + 1);  // from 1
        h.values[size_t(plugin::paramIndex(track, plugin::Track::Generator))] = float(ymengine::Generator::Digidrum);
        h.loaded(2, wave(40), "snare");
        MainWindow w(h);
        w.tick(0);
        const gemui::Rect s5 = *w.rectOf("genSlot4"), s8 = *w.rectOf("genSlot7"), edit = *w.rectOf("digiEdit");
        CHECK(edit.x == s5.x);
        CHECK(edit.right() == s8.right());
        CHECK(edit.y > s5.bottom());
        CHECK(edit.y - s5.bottom() <= 6);
        // Everything fits in the GENERATOR frame, the ST channel row of 3 ch mode included.
        CHECK(w.rectOf("digiHint")->bottom() < w.rectOf("genFrame")->bottom());

        click(w, "genSlot2");
        w.tick(10);
        click(w, "digiEdit");
        REQUIRE(w.digidrums().isOpen());
        CHECK(w.digidrums().slot() == 2);
        w.digidrums().close();
        click(w, "drumwave");
        CHECK(h.loads.empty());
    }
}

TEST_CASE("Main window: the SID timer ratio explains the ratio, not a Digidrum rate", "[ui][digidrum]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Sid);
    MainWindow w(h);
    w.tick(0);
    std::vector<std::string> tips;
    collectTips(tips, w);
    CHECK(std::count(tips.begin(), tips.end(),
                     "SID timer rate as a multiple of the note's frequency: 1 plays the note, 2 one octave higher") ==
          1);
    CHECK(std::none_of(tips.begin(), tips.end(),
                       [](const std::string& t) { return t.find("Digidrum playback rate") != std::string::npos; }));
}

TEST_CASE("Digidrums: rate, root note, fine tuning and loop typed (sixth DAW test, item 66)",
          "[ui][digidrum]") {
    TestHost h;
    MainWindow w(h);
    open(h, w);
    auto type = [&](const std::string& id, std::string_view text) {
        click(w, id);
        for (int i = 0; i < 8; ++i) key(w, gemui::Key::Backspace);
        for (char c : text) {
            gemui::KeyEvent e;
            e.key = gemui::Key::Character;
            e.ch = char32_t(c);
            w.desktop().keyDown(e);
        }
        key(w, gemui::Key::Enter);
    };
    type("digRateV", "9600");
    CHECK(h.slot(0).play.rateHz == 9600);
    CHECK(w.digidrums().valueText("digRate") == "9600 Hz");
    type("digRootV", "C#5");
    CHECK(h.slot(0).play.root == 73);
    type("digRootV", "Sol3");
    CHECK(h.slot(0).play.root == 55);
    type("digRootV", "40");
    CHECK(h.slot(0).play.root == 40);
    type("digRootV", "H2");  // not a note: nothing changes
    CHECK(h.slot(0).play.root == 40);
    type("digFineV", "-12");
    CHECK(h.slot(0).play.cents == -12);
    CHECK(w.digidrums().valueText("digFine") == "-12 ct");
    CHECK(h.editor.undo.undoSize() == 5);

    h.editExtras([](plugin::NonParamState& s) { s.digidrums[0] = std::vector<uint8_t>(500, 8); });
    w.tick(0);
    click(w, "digLoop");
    type("digStartV", "120");
    CHECK(h.slot(0).play.start == 120);
    type("digEndV", "50");  // End stays after Start
    CHECK(h.slot(0).play.end == 121);
}
