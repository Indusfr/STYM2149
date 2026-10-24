// STYM2149 — STE DMA sound in the interface (M6, B8, D-153, D-155): Computer row below Voices (ST or STE), modes menu
// and Options; DMA type of the generator and its slot; tabs DMA1 to DMA8 in 3 ch mode; DMA window (bank: slots,
// loading and listening, rate, mono or stereo, loop, root note, Sign and Gain tools, undo steps); Mixer of the DMA
// tracks (panning, Out L and Out R in 3 ch mode); session.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::DmaWindow;
using pluginui::MainWindow;
using plugin::Global;
using plugin::paramIndex;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::optional<plugin::EngineStatus> pending;
    plugin::EngineStatus last;
    std::vector<int> loads, previews;

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
    void seqCommand(SeqCommand c, int a, int, int) override {
        if (c == SeqCommand::DmaPreview) previews.push_back(a);
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
    void loadDigidrum(int) override {}
    void loadDmaSample(int slot) override { loads.push_back(slot); }
    std::string preference(std::string_view, std::string_view fallback) const override { return std::string(fallback); }
    void setPreference(std::string_view, std::string_view) override {}
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}

    int global(Global g) const { return int(values[size_t(paramIndex(g))]); }
    const ymengine::DmaParams& dma() const { return state.dma; }
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

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

bool undoKey(MainWindow& w) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = U'z';
    e.code = 'Z';
    e.mods.ctrl = true;
    return w.desktop().keyDown(e);
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

bool hasLabel(MainWindow& w, const std::string& label) {
    const std::vector<std::string> l = labels(w);
    return std::find(l.begin(), l.end(), label) != l.end();
}

// Drag of the thumb of a DMA window slider to the right (or left) end of its track.
void dragSlider(MainWindow& w, const char* id, bool toMax) {
    const gemui::Slider* s = w.dmaWindow().slider(id);
    REQUIRE(s);
    const gemui::Rect t = s->thumbRect(), r = s->bounds();
    drag(w, {centre(t), {r.x + r.w / 2, t.y + 8}, {toMax ? r.right() + 20 : r.x - 20, t.y + 8}});
}

std::vector<int8_t> ramp(size_t n) {
    std::vector<int8_t> b(n);
    for (size_t i = 0; i < n; ++i) b[i] = int8_t(int(i % 200) - 100);
    return b;
}

}  // namespace

TEST_CASE("DMA: Computer row below Voices, ST by default, modes menu and Options", "[ui][dma]") {
    TestHost h;
    MainWindow w(h);
    CHECK(h.global(Global::Computer) == 0);
    const gemui::Rect voices = *w.rectOf("modeSel"), st = *w.rectOf("steSt"), ste = *w.rectOf("steSte");
    const gemui::Rect clock = *w.rectOf("clkSel");
    CHECK(st.y > voices.bottom());
    CHECK(clock.y > st.bottom());
    CHECK(ste.x > st.right());
    // Band of the row and rows moved down (comparison with the screenshots, D-155).
    const gemui::Rect row = *w.rectOf("computerRow"), below = *w.rectOf("systemBelow");
    CHECK(row.y == st.y);
    CHECK(row.bottom() == clock.y);
    CHECK(below.y == clock.y);

    // STE: host setting (no undo step), notes cut as on a mode change.
    click(w, "steSte");
    CHECK(h.global(Global::Computer) == 1);
    CHECK(h.editor.undo.undoSize() == 0);
    click(w, "steSt");
    CHECK(h.global(Global::Computer) == 0);

    // Modes menu: Computer submenu.
    click(w, "modeSel");
    const std::vector<std::string> modes = labels(w);
    REQUIRE(modes.size() == 5);
    CHECK(modes[3] == "---");
    CHECK(modes[4] == "Computer");
    pick(w, 4);
    CHECK(labels(w) == std::vector<std::string>{"ST", "STE"});
    pick(w, 1);
    CHECK(h.global(Global::Computer) == 1);

    // Options > Sound engine > Computer.
    w.openMenu(3);
    const std::vector<std::string> opts = labels(w);
    const size_t at = size_t(std::find(opts.begin(), opts.end(), "Sound engine") - opts.begin());
    REQUIRE(at < opts.size());
    pick(w, int(at));
    CHECK(labels(w).front() == "Computer");
    pick(w, 0);
    CHECK(labels(w) == std::vector<std::string>{"ST", "STE"});
    pick(w, 0);
    CHECK(h.global(Global::Computer) == 0);
}

TEST_CASE("DMA: DMA type of the generator with the STE, slot of the track", "[ui][dma]") {
    TestHost h;
    MainWindow w(h);
    // ST: no DMA type in the menu (as in the prototype).
    click(w, "fxSel");
    CHECK_FALSE(hasLabel(w, "DMA"));
    key(w, gemui::Key::Escape);
    h.values[size_t(paramIndex(Global::Computer))] = 1;
    w.tick(0);
    click(w, "fxSel");
    REQUIRE(hasLabel(w, "DMA"));
    const std::vector<std::string> l = labels(w);
    pick(w, int(std::find(l.begin(), l.end(), "DMA") - l.begin()));
    CHECK(h.values[size_t(paramIndex(0, plugin::Track::Generator))] == 5.0f);

    // Slots: the one of the rank by default (track 1: 1), then the one chosen; no undo step (like a Digidrum).
    REQUIRE(w.rectOf("genDma0"));
    CHECK_FALSE(w.rectOf("drumwave"));
    const size_t undo = h.editor.undo.undoSize();  // choosing the DMA type made one
    click(w, "genDma4");
    CHECK(h.state.tracks[0].patch.dmaSlot == 4);
    CHECK(h.editor.undo.undoSize() == undo);
    w.tick(0);
    // "DMA samples", under samples 5 to 8 and as wide as them (D-213): the DMA window, on the track's slot.
    const gemui::Rect s5 = *w.rectOf("genDma4"), s8 = *w.rectOf("genDma7"), edit = *w.rectOf("dmaEdit");
    CHECK(edit.x == s5.x);
    CHECK(edit.right() == s8.right());
    CHECK(edit.y > s5.bottom());
    CHECK(edit.y - s5.bottom() <= 6);
    // The drawing of the sample, between the button and the text (D-213); it loads nothing.
    const gemui::Rect wave = *w.rectOf("genDmaWave");
    CHECK(wave.y > edit.bottom());
    CHECK(w.rectOf("dmaHint")->y > wave.bottom());
    CHECK(w.rectOf("dmaHint")->bottom() < w.rectOf("genFrame")->bottom());
    const size_t loads = h.loads.size();
    click(w, "genDmaWave");
    CHECK(h.loads.size() == loads);
    CHECK_FALSE(w.dmaWindow().isOpen());
    click(w, "dmaEdit");
    REQUIRE(w.dmaWindow().isOpen());
    CHECK(w.dmaWindow().slot() == 4);

    // ST machine: the track plays as Chip Voice again, its DMA setting kept.
    h.values[size_t(paramIndex(Global::Computer))] = 0;
    w.tick(0);
    CHECK_FALSE(w.rectOf("genDma0"));
    CHECK(h.values[size_t(paramIndex(0, plugin::Track::Generator))] == 5.0f);
}

TEST_CASE("DMA: DMA tracks in 3 ch mode with the STE (tabs, enforced role, Mixer)", "[ui][dma][mixer]") {
    TestHost h;
    h.values[size_t(paramIndex(Global::VoiceMode))] = 1;
    MainWindow w(h);
    CHECK(w.trackTitle(4) == "5");
    click(w, "steSte");
    CHECK(w.trackTitle(3) == "Digi");
    CHECK(w.trackTitle(4) == "DMA1");
    CHECK(w.trackTitle(11) == "DMA8");
    CHECK(w.trackTitle(12) == "13");

    // DMA track 2: type enforced, the others greyed out.
    click(w, "tab5");
    CHECK(h.global(Global::FocusTrack) == 6);
    REQUIRE(w.rectOf("genDma1"));
    click(w, "fxSel");
    const auto& items = menu(w).items();
    for (const gemui::MenuItem& it : items) {
        if (it.separator) continue;
        INFO(it.label);
        CHECK(it.disabled == (it.label != "DMA"));
    }
    key(w, gemui::Key::Escape);

    // Mixer: four YM strips without panning, eight DMA strips with it, stereo output.
    w.mixer().show();
    CHECK(w.rectOf("mixFad11"));
    CHECK_FALSE(w.rectOf("mixFad12"));
    CHECK_FALSE(w.rectOf("mixPan0"));
    CHECK(w.rectOf("mixPan4"));
    CHECK(w.rectOf("vuhR"));
    CHECK(w.rectOf("mixMute0")->y == w.rectOf("mixMute4")->y);  // M and S aligned

    // Back to the ST: the DMA track left hands over to the first track, mono Mixer with four strips.
    click(w, "steSt");
    CHECK(h.global(Global::FocusTrack) == 1);
    w.tick(0);
    CHECK_FALSE(w.rectOf("mixFad4"));
    CHECK_FALSE(w.rectOf("vuhR"));

    // A change of voice mode keeps a focused track that the new mode still has, a DMA track of the STE included.
    click(w, "steSte");
    click(w, "modeSel");
    pick(w, 0);  // 16 ch
    click(w, "tab9");
    CHECK(h.global(Global::FocusTrack) == 10);
    click(w, "modeSel");
    pick(w, 1);  // Authentic 3ch: DMA6
    CHECK(h.global(Global::FocusTrack) == 10);
    click(w, "modeSel");
    pick(w, 2);  // Dual chip 2x3ch: DMA2
    CHECK(h.global(Global::FocusTrack) == 10);
    click(w, "tab13");
    click(w, "modeSel");
    pick(w, 1);  // Authentic 3ch has twelve tracks: back to the first one
    CHECK(h.global(Global::FocusTrack) == 1);
}

TEST_CASE("DMA: Edit > Samples menu, greyed out on an ST (fourth DAW test, item 36, D-234); STE machine offered",
          "[ui][dma]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.dmaWindow().isOpen());
    auto openSamples = [&w] {
        w.openMenu(2);
        const std::vector<std::string> edit = labels(w);
        const size_t at = size_t(std::find(edit.begin(), edit.end(), "Samples") - edit.begin());
        REQUIRE(at < edit.size());
        CHECK(edit[at - 1] == "Instruments");  // Timer wave, Buzzer (D-238) and Sequences (D-241) left the menu
        pick(w, int(at));                     // the submenu opens
        CHECK(labels(w) == std::vector<std::string>{"Digidrums", "DMA (STE)"});
    };
    // Computer ST: the entry is greyed out with its reason.
    openSamples();
    CHECK(menu(w).items()[1].disabled);
    CHECK(menu(w).items()[1].why == "STE only: set Computer to STE");
    pick(w, 1);
    CHECK_FALSE(w.dmaWindow().isOpen());
    w.desktop().closePopups();
    // Computer STE: the entry opens the window.
    h.values[size_t(paramIndex(Global::Computer))] = 1;
    w.tick(0);
    openSamples();
    CHECK_FALSE(menu(w).items()[1].disabled);
    pick(w, 1);
    REQUIRE(w.dmaWindow().isOpen());
    h.values[size_t(paramIndex(Global::Computer))] = 0;
    w.tick(0);
    CHECK(w.dmaWindow().titleBar().title() == "DMA - STE");
    const gemui::Rect win = *w.rectOf("dmaWin");
    CHECK(win.x == DmaWindow::kX);
    CHECK(win.y == DmaWindow::kY);
    CHECK(win.right() <= MainWindow::kWidth);
    CHECK(win.bottom() <= MainWindow::kHeight);
    for (const char* id : {"dmaSlot0", "dmaSlot7", "dmaWave", "dmaLoad", "dmaPlay", "dmaClear", "dmaRate0", "dmaRate3",
                           "dmaMono", "dmaStereo", "dmaLoop", "dmaRoot", "dmaSign", "dmaGainDown", "dmaGainUp"}) {
        INFO(id);
        const auto r = w.rectOf(id);
        REQUIRE(r);
        CHECK(r->bottom() < win.bottom());
        CHECK(r->right() < win.right());
    }
    CHECK_FALSE(w.rectOf("dmaSlot8"));
    CHECK_FALSE(w.rectOf("dmaRate4"));

    // ST machine by default: a button switches to the STE (no undo step: host setting).
    CHECK(w.dmaWindow().status() == "Computer: ST. The DMA sound needs the STE.");
    click(w, "dmaUseSte");
    CHECK(h.global(Global::Computer) == 1);
    CHECK(w.dmaWindow().status() == "Computer: STE, any track can use the DMA type.");
    CHECK_FALSE(w.rectOf("dmaUseSte"));
    CHECK(h.editor.undo.undoSize() == 0);
    h.values[size_t(paramIndex(Global::VoiceMode))] = 2;
    w.tick(0);
    CHECK(w.dmaWindow().status() == "Computer: STE, DMA tracks 9 to 16.");
}

TEST_CASE("DMA: slot, rate, format, loop, root note, one undo step each", "[ui][dma][undo]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();

    click(w, "dmaSlot3");  // interface choice: no step
    CHECK(w.dmaWindow().slot() == 3);
    CHECK(h.editor.undo.undoSize() == 0);
    click(w, "dmaRate0");
    CHECK(h.dma().slots[3].rate == 0);
    CHECK(h.dma().slots[2].rate == 2);  // the other slots keep their rate
    click(w, "dmaRate0");               // already chosen: no step
    click(w, "dmaLoop");
    CHECK(h.dma().slots[3].loop);
    click(w, "dmaStereo");
    CHECK(h.dma().slots[3].stereo);
    CHECK(h.editor.undo.undoSize() == 3);

    // Root note: one step for the whole drag.
    CHECK(w.dmaWindow().rootText() == "C4");
    dragSlider(w, "dmaRoot", true);
    CHECK(h.dma().slots[3].root == 127);
    CHECK(w.dmaWindow().rootText() == "G9");
    CHECK(h.editor.undo.undoSize() == 4);

    // Undo, from the last to the first.
    CHECK(undoKey(w));
    CHECK(h.dma().slots[3].root == 60);
    for (int i = 0; i < 3; ++i) CHECK(undoKey(w));
    CHECK(h.dma() == ymengine::DmaParams{});
}

TEST_CASE("DMA: loading, listening, tools and mono-stereo switch; bytes in the undo step", "[ui][dma][undo]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot2");
    // Empty slot: listening and tools greyed out; a click on the drawing or on Load... loads a file.
    for (const char* id : {"dmaPlay", "dmaClear", "dmaSign", "dmaGainUp"}) {
        INFO(id);
        click(w, id);
    }
    CHECK(h.previews.empty());
    click(w, "dmaWave");
    click(w, "dmaLoad");
    CHECK(h.loads == std::vector<int>{2, 2});

    // The host has loaded the sample (no step: like a Digidrum).
    const size_t undoBefore = h.editor.undo.undoSize();
    h.editExtras([](plugin::NonParamState& s) { s.dmaSamples[2] = ramp(1000); });
    w.tick(0);
    CHECK(h.editor.undo.undoSize() == undoBefore);
    click(w, "dmaPlay");
    CHECK(h.previews == std::vector<int>{2});

    // Sign: most significant bit flipped; -3 dB; undone one by one.
    click(w, "dmaSign");
    CHECK(h.state.dmaSamples[2][0] == int8_t(uint8_t(int8_t(-100)) ^ 0x80));
    click(w, "dmaGainDown");
    CHECK(h.editor.undo.undoSize() == undoBefore + 2);
    CHECK(undoKey(w));
    CHECK(undoKey(w));
    CHECK(h.state.dmaSamples[2] == ramp(1000));

    // Mono → stereo: each byte doubled; stereo → mono: average of the two channels.
    click(w, "dmaStereo");
    REQUIRE(h.state.dmaSamples[2].size() == 2000);
    CHECK(h.state.dmaSamples[2][6] == h.state.dmaSamples[2][7]);
    CHECK(h.dma().slots[2].stereo);
    h.editExtras([](plugin::NonParamState& s) { s.dmaSamples[2] = {10, 20, -5, -8}; });
    w.tick(0);
    click(w, "dmaMono");
    CHECK(h.state.dmaSamples[2] == std::vector<int8_t>{15, -7});
    CHECK_FALSE(h.dma().slots[2].stereo);
    CHECK(undoKey(w));
    CHECK(h.state.dmaSamples[2] == std::vector<int8_t>{10, 20, -5, -8});
    CHECK(h.dma().slots[2].stereo);

    // Mono → stereo of a 32 KB sample: 16 KB per channel.
    h.editExtras([](plugin::NonParamState& s) {
        s.dma.slots[2].stereo = false;
        s.dmaSamples[2] = ramp(ymengine::kDmaMaxBytes);
    });
    w.tick(0);
    click(w, "dmaStereo");
    CHECK(h.state.dmaSamples[2].size() == ymengine::kDmaMaxBytes);

    // Clear, then undone.
    click(w, "dmaClear");
    CHECK(h.state.dmaSamples[2].empty());
    CHECK(undoKey(w));
    CHECK(h.state.dmaSamples[2].size() == ymengine::kDmaMaxBytes);
}

TEST_CASE("DMA: window reopened with the session", "[ui][dma]") {
    TestHost h;
    {
        MainWindow a(h);
        a.dmaWindow().show();
    }
    {
        MainWindow b(h);
        CHECK(b.dmaWindow().isOpen());
        key(b, gemui::Key::Escape);
        CHECK_FALSE(b.dmaWindow().isOpen());
    }
    MainWindow c(h);
    CHECK_FALSE(c.dmaWindow().isOpen());
}
