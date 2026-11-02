// STYM2149 — STE DMA sound in the interface: Computer row below Voices (ST or STE) and Options
// (no longer the modes menu); DMA type of the generator and its slot; tabs DMA1 to DMA8 in 3 ch mode; DMA window
// (bank: slots, name, loading and listening, rate, mono or stereo, loop, root note, Sign and Gain tools, undo steps);
// Mixer of the DMA tracks (panning, Out L and Out R in 3 ch mode); session; named banks.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "gemui/Alert.h"
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
    std::vector<int> loads, previews, stops;

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
        if (c == SeqCommand::DmaPreview) (b ? stops : previews).push_back(a);
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
    // The file dialog answers `chosen`; the load records the slot and the file.
    std::string chosen = "/samples/pad.wav";
    std::vector<FileDialog> dialogs;
    std::vector<std::string> files;
    void chooseFileToOpen(const FileDialog& d, std::function<void(const std::string&)> done) override {
        dialogs.push_back(d);
        if (!chosen.empty()) done(chosen);
    }
    void loadDmaSample(int slot, const std::string& path) override {
        loads.push_back(slot);
        files.push_back(path);
    }
    std::map<std::string, std::string, std::less<>> prefs;
    std::string preference(std::string_view key, std::string_view fallback) const override {
        const auto it = prefs.find(key);
        return it == prefs.end() ? std::string(fallback) : it->second;
    }
    void setPreference(std::string_view key, std::string_view value) override { prefs[std::string(key)] = value; }
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}
    std::string library;  // the DMA bank file
    std::string dmaLibrary() const override { return library; }
    void setDmaLibrary(const std::string& json) override { library = json; }

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

// Computer menu button (thirteenth DAW test, item 164): 0 = ST, 1 = STE.
void setComputer(MainWindow& w, int machine) {
    click(w, "compSel");
    pick(w, machine);
}

// Types `text` in the name field `id`: a click gives it the keys, the old name erased, Enter keeps it.
void typeName(MainWindow& w, const std::string& id, const std::string& text) {
    click(w, id);
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

// Captions of the DMA drawings and tips of every widget under `root`.
void collect(const gemui::Widget& root, std::vector<std::string>& caps, std::vector<std::string>& tips) {
    if (const auto* d = dynamic_cast<const pluginui::DmaWave*>(&root)) caps.push_back(d->caption());
    if (!root.tip().empty()) tips.push_back(root.tip());
    for (const gemui::Widget* c : root.children()) collect(*c, caps, tips);
}

std::vector<std::string> captions(MainWindow& w) {
    std::vector<std::string> caps, tips;
    collect(*w.desktop().root(), caps, tips);
    return caps;
}

std::vector<std::string> tips(MainWindow& w) {
    std::vector<std::string> caps, tips;
    collect(*w.desktop().root(), caps, tips);
    return tips;
}

std::vector<int8_t> ramp(size_t n) {
    std::vector<int8_t> b(n);
    for (size_t i = 0; i < n; ++i) b[i] = int8_t(int(i % 200) - 100);
    return b;
}

// The button shown at the place of `id` in the main window (the samples of the GENERATOR frame have no id of their
// own: rectOf names them).
const gemui::Button* shownButton(const gemui::Widget& root, const gemui::Rect& r) {
    if (const auto* b = dynamic_cast<const gemui::Button*>(&root); b && b->showing() && b->bounds() == r) return b;
    for (const gemui::Widget* c : root.children())
        if (const gemui::Button* b = shownButton(*c, r)) return b;
    return nullptr;
}
const gemui::Button& genButton(MainWindow& w, const std::string& id) {
    const std::optional<gemui::Rect> r = w.rectOf(id);
    REQUIRE(r);
    const gemui::Button* b = shownButton(*w.desktop().root(), *r);
    REQUIRE(b);
    return *b;
}

}  // namespace

TEST_CASE("DMA: Computer row below Voices, ST by default, and Options; not in the modes menu", "[ui][dma]") {
    TestHost h;
    MainWindow w(h);
    CHECK(h.global(Global::Computer) == 0);
    const gemui::Rect voices = *w.rectOf("modeSel"), comp = *w.rectOf("compSel");
    CHECK(comp.y > voices.bottom());
    // The two buttons start together (thirteenth DAW test, item 161).
    CHECK(comp.x == voices.x);
    // Band of the row, in the place of the Clock row, which went to Settings: Master under it.
    const gemui::Rect row = *w.rectOf("computerRow");
    CHECK(row.y == comp.y);
    CHECK(row.bottom() <= w.rectOf("master")->y);
    CHECK_FALSE(w.rectOf("clkSel"));

    // A menu button (item 164): ST or STE, the current one marked.
    click(w, "compSel");
    CHECK(labels(w) == std::vector<std::string>{"ST", "STE"});
    CHECK(menu(w).checked() == 0);
    // STE: host setting (no undo step), notes cut as on a mode change.
    pick(w, 1);
    CHECK(h.global(Global::Computer) == 1);
    CHECK(h.editor.undo.undoSize() == 0);
    w.tick(0);
    click(w, "compSel");
    CHECK(menu(w).checked() == 1);
    pick(w, 0);
    CHECK(h.global(Global::Computer) == 0);

    // Modes menu: the four modes alone, in the order 3ch, 2x3ch, 16 ch, 32 ch (Indus, 30/10/2026 02:21), the
    // machine left to the Computer row (24/10/2026).
    click(w, "modeSel");
    CHECK(labels(w) == std::vector<std::string>{"Authentic 3ch", "Dual chip 2x3ch", "16 ch", "32 ch"});
    CHECK(menu(w).checked() == 2);
    pick(w, 2);
    click(w, "compSel");
    pick(w, 1);
    CHECK(h.global(Global::Computer) == 1);

    // Options > Settings..., then Computer in its window.
    w.openMenu(3);
    const std::vector<std::string> opts = labels(w);
    const size_t at = size_t(std::find(opts.begin(), opts.end(), "Settings...") - opts.begin());
    REQUIRE(at < opts.size());
    pick(w, int(at));
    REQUIRE(w.settingsWindow().isOpen());
    CHECK(w.settingsWindow().button("setComputer")->text() == "STE");
    click(w, "setComputer");
    CHECK(labels(w) == std::vector<std::string>{"ST", "STE"});
    pick(w, 0);
    CHECK(h.global(Global::Computer) == 0);
}

TEST_CASE("DMA: DMA type of the generator with the STE, slot of the track", "[ui][dma]") {
    TestHost h;
    for (const size_t k : {4, 18}) h.state.dmaSamples[k] = ramp(100);
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

    // Slots: the one of the rank by default (track 1: 1), then the one chosen; no undo step (like a Digidrum). An empty
    // sample is greyed, the track's one too, and a click does not choose it (Indus, 30/10/2026 02:07).
    REQUIRE(w.rectOf("genDma0"));
    CHECK_FALSE(w.rectOf("drumwave"));
    CHECK(genButton(w, "genDma0").greyed());
    CHECK(genButton(w, "genDma0").on());
    CHECK(genButton(w, "genDma0").tip() == "DMA sample 1: empty. Load it in the DMA window (DMA samples).");
    CHECK_FALSE(genButton(w, "genDma4").greyed());
    const size_t undo = h.editor.undo.undoSize();  // choosing the DMA type made one
    click(w, "genDma3");
    CHECK(h.state.tracks[0].patch.dmaSlot == -1);  // still the rank's
    click(w, "genDma4");
    CHECK(h.state.tracks[0].patch.dmaSlot == 4);
    CHECK(h.editor.undo.undoSize() == undo);
    w.tick(0);
    CHECK_FALSE(genButton(w, "genDma0").on());
    CHECK(genButton(w, "genDma4").on());
    // "DMA samples", under samples 5 to 8 and as wide as them, the arrows of the pages of eight on its left,
    // under the label (eight again, Indus, 30/10/2026 02:06): the DMA window, on the track's slot.
    REQUIRE(w.rectOf("genDma7"));
    CHECK_FALSE(w.rectOf("genDma8"));
    const gemui::Rect s5 = *w.rectOf("genDma4"), s8 = *w.rectOf("genDma7"), edit = *w.rectOf("dmaEdit");
    CHECK(s5.x == w.rectOf("genDma0")->x);
    CHECK(s5.y > w.rectOf("genDma0")->bottom());
    const gemui::Rect prev = *w.rectOf("genDmaPrev"), next = *w.rectOf("genDmaNext");
    CHECK(edit.x == s5.x);
    CHECK(edit.right() == s8.right());
    CHECK(prev.right() < next.x);
    CHECK(next.right() < edit.x);
    CHECK(prev.y == edit.y);
    CHECK(next.y == edit.y);
    CHECK(edit.y > s5.bottom());
    CHECK(edit.y - s5.bottom() <= 6);
    // The drawing of the sample, between the button and the text; it loads nothing.
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
    w.dmaWindow().close();

    // Pages of eight samples: the arrows turn them, a click on a button gives the track its sample.
    CHECK_FALSE(genButton(w, "genDmaPrev").enabled());
    click(w, "genDmaNext");
    click(w, "genDmaNext");
    CHECK(genButton(w, "genDma0").text() == "17");
    CHECK(genButton(w, "genDma7").text() == "24");
    CHECK(h.state.tracks[0].patch.dmaSlot == 4);  // turning a page changes nothing
    click(w, "genDma2");
    CHECK(h.state.tracks[0].patch.dmaSlot == 18);
    for (int k = 0; k < 13; ++k) click(w, "genDmaNext");
    CHECK_FALSE(genButton(w, "genDmaNext").enabled());
    CHECK(genButton(w, "genDma0").text() == "121");
    CHECK(genButton(w, "genDma6").text() == "127");
    CHECK_FALSE(w.rectOf("genDma7"));  // no 128th sample
    click(w, "genDmaPrev");
    CHECK(genButton(w, "genDma0").text() == "113");
    // The track's sample changed elsewhere: the page follows it.
    h.state.tracks[0].patch.dmaSlot = 100;
    ++h.serial;
    w.tick(0);
    CHECK(genButton(w, "genDma4").text() == "101");
    CHECK(genButton(w, "genDma4").on());

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
    setComputer(w, 1);
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
    setComputer(w, 0);
    CHECK(h.global(Global::FocusTrack) == 1);
    w.tick(0);
    CHECK_FALSE(w.rectOf("mixFad4"));
    CHECK_FALSE(w.rectOf("vuhR"));

    // A change of voice mode keeps a focused track that the new mode still has, a DMA track of the STE included.
    setComputer(w, 1);
    click(w, "modeSel");
    pick(w, 2);  // 16 ch
    click(w, "tab9");
    CHECK(h.global(Global::FocusTrack) == 10);
    click(w, "modeSel");
    pick(w, 0);  // Authentic 3ch: DMA6
    CHECK(h.global(Global::FocusTrack) == 10);
    click(w, "modeSel");
    pick(w, 1);  // Dual chip 2x3ch: DMA2
    CHECK(h.global(Global::FocusTrack) == 10);
    click(w, "tab13");
    click(w, "modeSel");
    pick(w, 0);  // Authentic 3ch has twelve tracks: back to the first one
    CHECK(h.global(Global::FocusTrack) == 1);
}

TEST_CASE("DMA: Edit > Samples menu, greyed out on an ST (fourth DAW test, item 36); STE machine offered",
          "[ui][dma]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.dmaWindow().isOpen());
    auto openSamples = [&w] {
        w.openMenu(2);
        const std::vector<std::string> edit = labels(w);
        const size_t at = size_t(std::find(edit.begin(), edit.end(), "Samples") - edit.begin());
        REQUIRE(at < edit.size());
        CHECK(edit[at - 1] == "Instruments");  // Timer wave, Buzzer and Sequences left the menu
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
                           "dmaMono", "dmaStereo", "dmaLoop", "dmaRoot", "dmaNormalize", "dmaReverse", "dmaFade",
                           "dmaSign", "dmaGainDown", "dmaGainUp", "dmaChanL", "dmaChanR"}) {
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

TEST_CASE("DMA: Start and End of the part played, with or without Loop", "[ui][dma][undo]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot1");
    CHECK_FALSE(w.dmaWindow().slider("dmaStart")->enabled());  // empty slot
    h.editExtras([](plugin::NonParamState& s) { s.dmaSamples[1] = ramp(1000); });
    w.tick(0);
    CHECK(w.dmaWindow().slider("dmaStart")->enabled());
    CHECK(w.dmaWindow().slider("dmaEnd")->enabled());
    CHECK(w.dmaWindow().startText() == "0");
    CHECK(w.dmaWindow().endText() == "1000");  // End 0: the end of the sample, shown as its frames

    // Start at the end: brought back to End - 1 (End written); End at the start: brought back to Start + 1.
    dragSlider(w, "dmaStart", true);
    CHECK(h.dma().slots[1].start == 999);
    CHECK(h.dma().slots[1].end == 1000);
    CHECK_FALSE(h.dma().slots[1].loop);
    dragSlider(w, "dmaEnd", false);
    CHECK(h.dma().slots[1].end == 1000);
    dragSlider(w, "dmaStart", false);
    CHECK(h.dma().slots[1].start == 0);
    dragSlider(w, "dmaEnd", false);
    CHECK(h.dma().slots[1].end == 1);
    CHECK(w.dmaWindow().endText() == "1");
    CHECK(h.editor.undo.undoSize() == 3);  // End already at the lowest allowed: no step

    // Mono → stereo keeps the frames, and the part; Clear empties it, undone with the bytes.
    click(w, "dmaStereo");
    CHECK(h.dma().slots[1].end == 1);
    click(w, "dmaClear");
    CHECK(h.dma().slots[1].start == 0);
    CHECK(h.dma().slots[1].end == 0);
    CHECK_FALSE(w.dmaWindow().slider("dmaEnd")->enabled());
    CHECK(undoKey(w));
    CHECK(h.dma().slots[1].end == 1);
    CHECK(h.state.dmaSamples[1].size() == 2000);
    CHECK(undoKey(w));  // stereo
    CHECK(undoKey(w));  // End
    CHECK(h.dma().slots[1].start == 0);
    CHECK(h.dma().slots[1].end == 1000);
    CHECK(undoKey(w));  // Start
    CHECK(h.dma().slots[1].start == 999);
}

TEST_CASE("DMA: name of the sample in the window, the drawings and the tips; renamed, undone, cleared",
          "[ui][dma][undo]") {
    TestHost h;
    h.values[size_t(paramIndex(Global::Computer))] = 1;
    h.values[size_t(paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Dma);
    h.state.tracks[0].patch.dmaSlot = 1;
    h.state.dmaSamples[1] = ramp(1000);
    h.state.dmaNames[1] = "Pad loop";
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot1");
    w.tick(0);
    CHECK(w.rectOf("dmaName"));
    // The name first in both drawings, in the tips of the slots and in the text of the generator frame.
    const std::vector<std::string> caps = captions(w);
    CHECK(std::count_if(caps.begin(), caps.end(), [](const std::string& c) {
              return c.starts_with("Pad loop, 1000 bytes, ");
          }) == 2);
    const std::vector<std::string> t = tips(w);
    CHECK(std::find(t.begin(), t.end(), "Slot 2 \"Pad loop\": 1000 bytes") != t.end());
    const std::string trackTip =
        "DMA sample 2 \"Pad loop\": 1000 bytes. This track plays it, tuned from its root note.";
    CHECK(std::find(t.begin(), t.end(), trackTip) != t.end());
    CHECK(std::find(t.begin(), t.end(), "Slot 1: empty") != t.end());

    // Renamed in its field, cleaned like a Digidrum name: one step; the same name again: none.
    typeName(w, "dmaName", "  Strings ");
    CHECK(h.state.dmaNames[1] == "Strings");
    CHECK(w.dmaWindow().nameField().text() == "Strings");
    CHECK(h.editor.undo.undoSize() == 1);
    typeName(w, "dmaName", "Strings");
    CHECK(h.editor.undo.undoSize() == 1);
    // Clear empties the name with the sample; both come back with the undo.
    click(w, "dmaClear");
    CHECK(h.state.dmaNames[1].empty());
    CHECK(h.state.dmaSamples[1].empty());
    CHECK(undoKey(w));
    CHECK(h.state.dmaNames[1] == "Strings");
    CHECK(h.state.dmaSamples[1].size() == 1000);
    CHECK(undoKey(w));
    CHECK(h.state.dmaNames[1] == "Pad loop");
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

    // Mono → stereo of a long sample: the whole of it on both sides (once 16 KB per channel at most).
    h.editExtras([](plugin::NonParamState& s) {
        s.dma.slots[2].stereo = false;
        s.dmaSamples[2] = ramp(100000);
    });
    w.tick(0);
    click(w, "dmaStereo");
    CHECK(h.state.dmaSamples[2].size() == 200000);

    // Clear, then undone.
    click(w, "dmaClear");
    CHECK(h.state.dmaSamples[2].empty());
    CHECK(undoKey(w));
    CHECK(h.state.dmaSamples[2].size() == 200000);
}

TEST_CASE("DMA: Normalize, Reverse and Fade out of the Digidrums; L and R choose the channels of a stereo sample "
          "(Indus, 26/10/2026)",
          "[ui][dma][undo]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot3");
    // Empty slot: the new tools are greyed out too.
    for (const char* id : {"dmaNormalize", "dmaReverse", "dmaFade", "dmaChanL"}) click(w, id);
    CHECK(h.editor.undo.undoSize() == 0);

    // Mono: Reverse turns the whole sample round and mirrors Start and End; L and R are greyed out.
    h.editExtras([](plugin::NonParamState& s) {
        s.dmaSamples[3] = {10, 20, 30, 40, 50};
        s.dma.slots[3].start = 1;
        s.dma.slots[3].end = 3;
    });
    w.tick(0);
    click(w, "dmaChanL");  // greyed: both channels stay chosen
    click(w, "dmaReverse");
    CHECK(h.state.dmaSamples[3] == std::vector<int8_t>{50, 40, 30, 20, 10});
    CHECK(h.dma().slots[3].start == 2);
    CHECK(h.dma().slots[3].end == 4);
    click(w, "dmaNormalize");
    CHECK(h.state.dmaSamples[3].front() == 127);
    click(w, "dmaFade");
    CHECK(h.state.dmaSamples[3].back() < 25);
    CHECK(h.editor.undo.undoSize() == 3);
    CHECK(undoKey(w));
    CHECK(undoKey(w));
    CHECK(undoKey(w));
    CHECK(h.state.dmaSamples[3] == std::vector<int8_t>{10, 20, 30, 40, 50});
    CHECK(h.dma().slots[3].start == 1);
    CHECK(h.dma().slots[3].end == 3);

    // Stereo, frames (L, R): both channels by default; Reverse keeps left and right in place.
    h.editExtras([](plugin::NonParamState& s) {
        s.dma.slots[3] = {};
        s.dma.slots[3].stereo = true;
        s.dma.slots[3].start = 1;
        s.dmaSamples[3] = {1, -1, 2, -2, 3, -3};
    });
    w.tick(0);
    click(w, "dmaReverse");
    CHECK(h.state.dmaSamples[3] == std::vector<int8_t>{3, -3, 2, -2, 1, -1});
    CHECK(h.dma().slots[3].start == 0);
    CHECK(h.dma().slots[3].end == 2);
    CHECK(undoKey(w));

    // L off: the tools work on the right channel only, and Start and End stay; R cannot go off as well.
    click(w, "dmaChanL");
    click(w, "dmaChanR");
    click(w, "dmaReverse");
    CHECK(h.state.dmaSamples[3] == std::vector<int8_t>{1, -3, 2, -2, 3, -1});
    CHECK(h.dma().slots[3].start == 1);
    CHECK(h.dma().slots[3].end == 0);
    click(w, "dmaSign");
    CHECK(h.state.dmaSamples[3][0] == 1);
    CHECK(h.state.dmaSamples[3][1] == int8_t(-3 ^ 0x80));
    // L on again with R: both channels.
    click(w, "dmaChanL");
    click(w, "dmaSign");
    CHECK(h.state.dmaSamples[3][0] == int8_t(1 ^ 0x80));
    CHECK(h.state.dmaSamples[3][1] == -3);
}

TEST_CASE("DMA: -3 dB after a clipped +3 dB gives the peaks back; each step undone alone (Indus, 26/10/2026)",
          "[ui][dma][undo]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot0");
    const std::vector<int8_t> orig = {100, -100, 50, 1};
    h.editExtras([&](plugin::NonParamState& s) { s.dmaSamples[0] = orig; });
    w.tick(0);
    for (int i = 0; i < 3; ++i) click(w, "dmaGainUp");
    CHECK(h.state.dmaSamples[0][0] == 127);  // clipped
    for (int i = 0; i < 3; ++i) click(w, "dmaGainDown");
    CHECK(h.state.dmaSamples[0] == orig);
    CHECK(h.editor.undo.undoSize() == 6);  // one step each
    // Another tool ends the run: the next step starts from its bytes.
    click(w, "dmaGainUp");
    click(w, "dmaReverse");
    click(w, "dmaGainDown");
    std::vector<int8_t> want = orig;
    plugin::dmaGain(want, 3);
    std::reverse(want.begin(), want.end());
    plugin::dmaGain(want, -3);
    CHECK(h.state.dmaSamples[0] == want);
}

TEST_CASE("DMA: with Resample on, a new rate resamples the sample; Start and End follow (Indus, 26/10/2026)",
          "[ui][dma][undo]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot1");
    h.editExtras([](plugin::NonParamState& s) {
        s.dmaSamples[1] = ramp(1000);
        s.dma.slots[1].rate = 2;  // 25 kHz
        s.dma.slots[1].start = 100;
        s.dma.slots[1].end = 400;
    });
    w.tick(0);
    // Off (default): the rate changes alone, the bytes are kept.
    REQUIRE(w.dmaWindow().rectOf("dmaResample"));
    click(w, "dmaRate3");
    CHECK(h.dma().slots[1].rate == 3);
    CHECK(h.state.dmaSamples[1] == ramp(1000));
    // On: kept in the preferences; back to 25 kHz, the sample is resampled to half its frames.
    click(w, "dmaResample");
    CHECK(h.prefs["dmaResample"] == "1");
    const size_t undo = h.editor.undo.undoSize();
    click(w, "dmaRate2");
    CHECK(h.dma().slots[1].rate == 2);
    CHECK(h.state.dmaSamples[1] == plugin::dmaResample(ramp(1000), false, 50066, 25033));
    CHECK(h.state.dmaSamples[1].size() == 500);
    CHECK(h.dma().slots[1].start == 50);
    CHECK(h.dma().slots[1].end == 200);
    CHECK(h.editor.undo.undoSize() == undo + 1);  // one step, bytes included
    CHECK(undoKey(w));
    CHECK(h.state.dmaSamples[1] == ramp(1000));
    CHECK(h.dma().slots[1].rate == 3);
    CHECK(h.dma().slots[1].start == 100);
    // Off again: the next rate keeps the bytes.
    click(w, "dmaResample");
    CHECK(h.prefs["dmaResample"] == "0");
    click(w, "dmaRate0");
    CHECK(h.state.dmaSamples[1] == ramp(1000));
}

TEST_CASE("DMA: Fine tunes the slot from -100 to +100 cents, one undo step per gesture, typed too (Indus, 26/10/2026)",
          "[ui][dma][undo]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot4");
    REQUIRE(w.dmaWindow().rectOf("dmaFine"));
    CHECK(w.dmaWindow().fineText() == "0 ct");
    dragSlider(w, "dmaFine", true);
    CHECK(h.dma().slots[4].cents == 100);
    CHECK(w.dmaWindow().fineText() == "+100 ct");
    CHECK(h.editor.undo.undoSize() == 1);  // one step for the gesture
    click(w, "dmaFineV");
    for (int i = 0; i < 8; ++i) key(w, gemui::Key::Backspace);
    for (char c : std::string("-37")) {
        gemui::KeyEvent e;
        e.key = gemui::Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
    key(w, gemui::Key::Enter);
    CHECK(h.dma().slots[4].cents == -37);
    CHECK(w.dmaWindow().fineText() == "-37 ct");
    CHECK(h.editor.undo.undoSize() == 2);
    CHECK(h.dma().slots[0].cents == 0);  // the other slots keep theirs
    CHECK(undoKey(w));
    CHECK(h.dma().slots[4].cents == 100);
    CHECK(undoKey(w));
    CHECK(h.dma().slots[4].cents == 0);
}

TEST_CASE("DMA: with Loop on, Play loops the sample; Play again, Loop or closing stops it (Indus, 26/10/2026)",
          "[ui][dma]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot2");
    h.editExtras([](plugin::NonParamState& s) { s.dmaSamples[2] = ramp(1000); });
    w.tick(0);
    plugin::EngineStatus t;
    // Loop off: each click plays the part once, Play never lit.
    click(w, "dmaPlay");
    click(w, "dmaPlay");
    CHECK(h.previews == std::vector<int>{2, 2});
    CHECK(h.stops.empty());
    CHECK_FALSE(w.dmaWindow().playLit());
    h.pending = t;  // the part played to its end
    w.tick(0);
    // Loop on: Play lit while the engine loops the slot, a second click stops it.
    click(w, "dmaLoop");
    REQUIRE(h.dma().slots[2].loop);
    click(w, "dmaPlay");
    CHECK(h.previews.size() == 3);
    t.dmaPreview = 2;
    h.pending = t;
    w.tick(0);
    CHECK(w.dmaWindow().playLit());
    click(w, "dmaPlay");
    CHECK(h.stops == std::vector<int>{2});
    CHECK_FALSE(w.dmaWindow().playLit());
    t.dmaPreview = -1;
    h.pending = t;
    w.tick(0);
    // Loop switched off: Play goes out (the engine stops the loop itself).
    click(w, "dmaPlay");
    t.dmaPreview = 2;
    h.pending = t;
    w.tick(0);
    REQUIRE(w.dmaWindow().playLit());
    click(w, "dmaLoop");
    CHECK_FALSE(h.dma().slots[2].loop);
    CHECK_FALSE(w.dmaWindow().playLit());
    // Another slot chosen: Play plays that one.
    click(w, "dmaLoop");
    h.pending = t;
    w.tick(0);
    REQUIRE(w.dmaWindow().playLit());
    click(w, "dmaSlot3");
    CHECK_FALSE(w.dmaWindow().playLit());
    // Closing the window stops the preview.
    w.dmaWindow().close();
    CHECK(h.stops == std::vector<int>{2, 2});
}

TEST_CASE("DMA: named banks in a shared library, as in the Digidrums window (Indus, 26/10/2026)",
          "[ui][dma]") {
    TestHost h;
    MainWindow w(h);
    w.dmaWindow().show();
    w.tick(0);
    // Under Fine, in the slot column.
    const auto banksBtn = w.rectOf("dmaBanks"), fine = w.rectOf("dmaFine");
    REQUIRE(banksBtn);
    REQUIRE(fine);
    CHECK(banksBtn->y > fine->y);
    // Empty library: Load and Delete unavailable.
    click(w, "dmaBanks");
    CHECK(labels(w) == std::vector<std::string>{"Save bank...", "Load bank", "---", "Delete bank"});
    CHECK(menu(w).items()[1].disabled);
    CHECK(menu(w).items()[3].disabled);
    pick(w, 0);
    h.editExtras([](plugin::NonParamState& s) {
        s.dmaSamples[0] = ramp(400);
        s.dmaNames[0] = "Pad";
        s.dma.slots[0].stereo = true;
        s.dma.slots[0].rate = 1;
    });
    w.tick(0);
    answer(w, "Set A");
    REQUIRE(w.dmaWindow().banks().size() == 1);
    CHECK(w.dmaWindow().banks()[0].name == "Set A");
    CHECK(w.dmaWindow().banks()[0].samples[0].size() == 400);

    // Same name: replaced; another name: added.
    h.editExtras([](plugin::NonParamState& s) { s.dmaSamples[1] = ramp(10); });
    w.tick(0);
    w.dmaWindow().saveBank("Set A");
    w.dmaWindow().saveBank("Set B");
    REQUIRE(w.dmaWindow().banks().size() == 2);
    CHECK(w.dmaWindow().banks()[0].samples[1].size() == 10);

    // Reloading a bank: confirmation, then the eight samples replaced (no undo step), a looping preview stopped.
    h.editExtras([](plugin::NonParamState& s) {
        s.dmaSamples = {};
        s.dmaNames = {};
        s.dma = {};
    });
    w.tick(0);
    plugin::EngineStatus t;
    t.dmaPreview = 0;
    h.pending = t;
    w.tick(0);
    const size_t undoBefore = h.editor.undo.undoSize();
    click(w, "dmaBanks");
    pick(w, 1);  // Load bank submenu
    CHECK(labels(w) == std::vector<std::string>{"Set A", "Set B"});
    pick(w, 0);
    REQUIRE(w.desktop().hasModal());
    key(w, gemui::Key::Escape);  // Cancel
    CHECK(h.state.dmaSamples[0].empty());
    w.dmaWindow().loadBank("Set A");
    CHECK(h.state.dmaSamples[0] == ramp(400));
    CHECK(h.state.dmaNames[0] == "Pad");
    CHECK(h.dma().slots[0].stereo);
    CHECK(h.dma().slots[0].rate == 1);
    CHECK(h.state.dmaSamples[1] == ramp(10));
    CHECK(h.editor.undo.undoSize() == undoBefore);
    CHECK(h.stops == std::vector<int>{0});

    // Delete.
    w.dmaWindow().deleteBank("Set A");
    REQUIRE(w.dmaWindow().banks().size() == 1);
    CHECK(w.dmaWindow().banks()[0].name == "Set B");

    // Unreadable file: nothing is overwritten, an alert says so; Save is greyed.
    h.library = "{ not json";
    w.dmaWindow().saveBank("Set C");
    CHECK(h.library == "{ not json");
    CHECK(w.desktop().hasModal());
    key(w, gemui::Key::Enter);
    click(w, "dmaBanks");
    CHECK(menu(w).items()[0].disabled);
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

TEST_CASE("DMA: Load asks before replacing a sample; a file dropped on the drawing loads the same way (Indus, "
          "30/10/2026 02:42-02:50)",
          "[ui][dma]") {
    TestHost h;
    h.values[size_t(paramIndex(Global::Computer))] = 1;
    MainWindow w(h);
    w.dmaWindow().show();
    click(w, "dmaSlot3");
    click(w, "dmaLoad");
    REQUIRE(h.dialogs.size() == 1);
    CHECK(h.dialogs[0].title == "Load a DMA sample into slot 4");
    CHECK(h.dialogs[0].patterns == "*.wav;*.aif;*.aiff;*.raw;*.spl;*.sam");
    CHECK(h.loads == std::vector<int>{3});
    CHECK_FALSE(w.desktop().hasModal());

    // Full: the box (Cancel by default) without a name when the sample has none.
    h.editExtras([](plugin::NonParamState& s) { s.dmaSamples[3] = ramp(500); });
    w.tick(0);
    const gemui::Point at = centre(*w.rectOf("dmaWave"));
    CHECK(w.dropFiles({"/x/bass.raw"}, at));
    const auto* a = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
    REQUIRE(a);
    CHECK(a->text().find("Slot 4 already holds a sample.") != std::string::npos);
    CHECK(a->text().find("Replace it with \"bass\"?") != std::string::npos);
    CHECK(a->focusedButton() == 0);
    key(w, gemui::Key::Enter);  // Cancel
    CHECK(h.loads.size() == 1);
    CHECK(w.dropFiles({"/x/bass.raw"}, at));
    a = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
    REQUIRE(a);
    press(w, centre(a->button(size_t(gemui::Alert::kConfirmOk)).bounds()));
    CHECK(h.loads == std::vector<int>{3, 3});
    CHECK(h.files.back() == "/x/bass.raw");
}

TEST_CASE("DMA: the memory line only in the 3-channel modes, the RAM in Settings (Indus, 30/10/2026 02:47-02:49)",
          "[ui][dma]") {
    TestHost h;
    h.values[size_t(paramIndex(Global::Computer))] = 1;
    h.values[size_t(paramIndex(Global::VoiceMode))] = float(int(ymengine::VoiceMode::Authentic3));
    h.state.dmaSamples[0] = ramp(2048);
    MainWindow w(h);
    w.dmaWindow().show();
    w.tick(0);
    CHECK(w.dmaWindow().memoryLine() == "2 KB of 3968 KB");
    CHECK(w.rectOf("dmaGauge"));
    CHECK_FALSE(w.rectOf("dmaRam"));
    const double h3 = w.rectOf("dmaWin")->h;
    h.values[size_t(paramIndex(Global::VoiceMode))] = float(int(ymengine::VoiceMode::Dual3));
    w.tick(10);
    CHECK(w.dmaWindow().memoryLine() == "2 KB of 7936 KB");  // two Ataris
    h.values[size_t(paramIndex(Global::VoiceMode))] = float(int(ymengine::VoiceMode::Studio32));
    w.tick(20);
    CHECK(w.dmaWindow().memoryLine().empty());
    CHECK_FALSE(w.rectOf("dmaMemory"));
    CHECK_FALSE(w.rectOf("dmaGauge"));
    CHECK(w.rectOf("dmaWin")->h < h3);  // the row taken out
}
