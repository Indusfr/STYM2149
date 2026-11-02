// STYM2149 — memory of the Atari for the samples in the interface (Indus, 29/10/2026): pages of eight slots in
// the Digidrums and DMA windows, memory line and RAM menu, slots past the memory crossed out, the Digidrum of each pad
// (right click), and the GEM box (Cancel by default) before a change that would silence samples.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "UiHost.h"
#include "plugincore/SteMemory.h"

using namespace stym;
using namespace stym::ui_testing;
using plugin::Global;
using plugin::Track;

namespace {

constexpr size_t kKb = 1024;

void setMode(UiHost& h, ymengine::VoiceMode m) { h.p(Global::VoiceMode) = float(int(m)); }

// The pad button of the GENERATOR frame whose tip starts with "Pad n:" (n from 1).
const gemui::Button* padButton(const gemui::Widget& root, int n) {
    const std::string head = "Pad " + std::to_string(n) + ":";
    if (const auto* b = dynamic_cast<const gemui::Button*>(&root); b && b->tip().rfind(head, 0) == 0) return b;
    for (const gemui::Widget* c : root.children())
        if (const gemui::Button* b = padButton(*c, n)) return b;
    return nullptr;
}

// Types `text` in the open prompt box and presses Enter.
void answer(pluginui::MainWindow& w, const std::string& text) {
    REQUIRE(topAlert(w));
    for (int i = 0; i < 8; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) w.desktop().keyDown(charKey(char32_t(c)));
    key(w, gemui::Key::Enter);
    CHECK_FALSE(topAlert(w));
}

// The GEM box of the memory: Cancel first and by default, then Proceed.
void checkMemoryBox(pluginui::MainWindow& w) {
    const gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text().find("the memory of the Atari") != std::string::npos);
    REQUIRE(a->buttonCount() == 2);
    CHECK(a->button(0).text() == "Cancel");
    CHECK(a->button(1).text() == "Proceed");
    CHECK(a->focusedButton() == 0);
}

}  // namespace

TEST_CASE("STE memory: the Digidrums window turns pages of eight slots up to the 127th",
          "[ui][stemem]") {
    UiHost h;
    pluginui::MainWindow w(h);
    pluginui::DigidrumWindow& d = w.digidrums();
    d.show();
    w.tick(0);
    CHECK(d.slotButton(0).text() == "1");
    CHECK(d.slotButton(7).text() == "8");
    REQUIRE(w.rectOf("digPrev"));
    click(w, "digPrev");  // greyed on the first page
    CHECK(d.slot() == 0);

    click(w, "digNext");
    w.tick(10);
    CHECK(d.slot() == 8);
    CHECK(d.slotButton(0).text() == "9");
    CHECK(d.slotButton(0).on());
    click(w, "digSlot3");
    w.tick(20);
    CHECK(d.slot() == 11);
    click(w, "digPrev");
    w.tick(30);
    CHECK(d.slot() == 3);

    // The last page holds slots 121 to 127: seven buttons, the arrow to the right greyed.
    d.selectSlot(126);
    w.tick(40);
    CHECK(d.slotButton(0).text() == "121");
    CHECK(d.slotButton(6).text() == "127");
    CHECK(d.slotButton(6).on());
    CHECK_FALSE(w.rectOf("digSlot7"));
    click(w, "digNext");
    CHECK(d.slot() == 126);
}

TEST_CASE("STE memory: the DMA window turns pages of eight samples too", "[ui][stemem][dma]") {
    UiHost h;
    h.p(Global::Computer) = 1;  // STE
    pluginui::MainWindow w(h);
    pluginui::DmaWindow& d = w.dmaWindow();
    d.show();
    w.tick(0);
    CHECK(d.slotButton(0).text() == "1");
    click(w, "dmaNext");
    w.tick(10);
    CHECK(d.slot() == 8);
    CHECK(d.slotButton(2).text() == "11");
    click(w, "dmaSlot2");
    w.tick(20);
    CHECK(d.slot() == 10);
    click(w, "dmaPrev");
    w.tick(30);
    CHECK(d.slot() == 2);
}

TEST_CASE("STE memory: memory line, RAM in Settings, slots past the memory crossed out, and the box before silencing "
          "some",
          "[ui][stemem]") {
    UiHost h;
    setMode(h, ymengine::VoiceMode::Authentic3);
    h.state.digidrums[0] = std::vector<uint8_t>(300 * kKb, 8);
    pluginui::MainWindow w(h);
    pluginui::DigidrumWindow& d = w.digidrums();
    d.show();
    w.tick(0);
    CHECK(d.memoryLine() == "300 KB of 3968 KB");
    // The RAM left the window for Settings > SOUND ENGINE (Indus, 30/10/2026 02:49).
    CHECK_FALSE(w.rectOf("digRam"));
    pluginui::SettingsWindow& set = w.settingsWindow();
    set.show();
    w.tick(1);
    REQUIRE(set.button("setRam"));
    CHECK(set.button("setRam")->text() == "4 MB");
    CHECK(set.button("setRam")->enabled());

    // 512 KB: 384 KB for the samples, the 300 KB fit: no box.
    click(w, "setRam");
    CHECK(menuLabels(w) == std::vector<std::string>{"512 KB", "1 MB", "2 MB", "4 MB"});
    CHECK(topMenu(w)->checked() == 3);
    pickItem(w, 0);
    CHECK_FALSE(topAlert(w));
    CHECK(h.state.steRam == 0);
    w.tick(10);
    CHECK(d.memoryLine() == "300 KB of 384 KB");
    CHECK(set.button("setRam")->text() == "512 KB");
    CHECK(h.editor.undo.undoSize() == 0);  // not an undo step, like loading a sample

    // A sample of 100 KB in slot 6: past the memory, crossed out.
    h.editExtras([](plugin::NonParamState& s) { s.digidrums[5] = std::vector<uint8_t>(100 * kKb, 8); });
    w.tick(20);
    CHECK(d.memoryLine() == "400 KB of 384 KB");
    CHECK(d.slotButton(5).crossed());
    CHECK_FALSE(d.slotButton(0).crossed());
    CHECK(d.slotButton(5).tip().find("silent") != std::string::npos);

    // 1 MB silences none: at once.
    click(w, "setRam");
    pickItem(w, 1);
    CHECK_FALSE(topAlert(w));
    CHECK(h.state.steRam == 1);
    w.tick(30);
    CHECK_FALSE(d.slotButton(5).crossed());

    // Back to 512 KB: the box first; Enter cancels.
    click(w, "setRam");
    pickItem(w, 0);
    checkMemoryBox(w);
    key(w, gemui::Key::Enter);
    CHECK_FALSE(topAlert(w));
    CHECK(h.state.steRam == 1);
    // Proceed makes the change.
    click(w, "setRam");
    pickItem(w, 0);
    checkMemoryBox(w);
    alertButton(w, 1);
    CHECK(h.state.steRam == 0);
    w.tick(40);
    CHECK(d.slotButton(5).crossed());

    // 2x3ch, two Ataris: twice the memory, everything heard.
    setMode(h, ymengine::VoiceMode::Dual3);
    w.tick(45);
    CHECK(d.memoryLine() == "400 KB of 768 KB");
    CHECK_FALSE(d.slotButton(5).crossed());

    // 16- and 32-channel modes: no limit, nothing crossed, no memory line, the RAM greyed in Settings.
    setMode(h, ymengine::VoiceMode::Studio16);
    w.tick(50);
    CHECK(d.memoryLine().empty());
    CHECK_FALSE(w.rectOf("digMemory"));
    CHECK_FALSE(w.rectOf("digGauge"));
    CHECK_FALSE(d.slotButton(5).crossed());
    CHECK_FALSE(set.button("setRam")->enabled());
    CHECK(set.button("setRam")->tip() == "RAM: 3ch and 2x3ch only (no limit in 16 and 32 ch)");
    CHECK(set.button("setRam")->text() == "512 KB");  // the value kept

    // Cancel of the Settings window puts the RAM back as it was at the opening.
    setMode(h, ymengine::VoiceMode::Authentic3);
    w.tick(60);
    set.cancel();
    if (topAlert(w)) alertButton(w, 1);  // Discard the changes
    CHECK(h.state.steRam == 3);
}

TEST_CASE("STE memory: a 3-channel mode or the STE that would silence samples asks first",
          "[ui][stemem]") {
    UiHost h;
    h.state.steRam = 0;
    h.state.digidrums[0] = std::vector<uint8_t>(500 * kKb, 8);
    pluginui::MainWindow w(h);
    w.tick(0);
    CHECK(int(h.p(Global::VoiceMode)) == 0);

    click(w, "modeSel");
    pickItem(w, 0);  // Authentic 3ch
    checkMemoryBox(w);
    alertButton(w, 0);  // Cancel
    CHECK(int(h.p(Global::VoiceMode)) == 0);
    click(w, "modeSel");
    pickItem(w, 0);
    alertButton(w, 1);  // Proceed
    CHECK(int(h.p(Global::VoiceMode)) == 1);

    // In Authentic 3ch with the ST, a large DMA sample counts only on the STE.
    h.editExtras([](plugin::NonParamState& s) {
        s.digidrums[0].clear();
        s.dmaSamples[0] = std::vector<int8_t>(500 * kKb, 1);
    });
    w.tick(10);
    click(w, "compSel");
    pickItem(w, 1);  // STE
    checkMemoryBox(w);
    key(w, gemui::Key::Escape);
    CHECK(int(h.p(Global::Computer)) == 0);
    click(w, "compSel");
    pickItem(w, 1);
    alertButton(w, 1);
    CHECK(int(h.p(Global::Computer)) == 1);
    // Back to the ST frees the memory: no box.
    click(w, "compSel");
    pickItem(w, 0);
    CHECK_FALSE(topAlert(w));
    CHECK(int(h.p(Global::Computer)) == 0);
}

TEST_CASE("STE memory: a sample loaded past the memory asks first", "[ui][stemem]") {
    UiHost h;
    setMode(h, ymengine::VoiceMode::Authentic3);
    h.state.steRam = 0;
    pluginui::MainWindow w(h);
    w.tick(0);
    const auto load = [](plugin::NonParamState& s) { s.digidrums[3] = std::vector<uint8_t>(500 * kKb, 8); };
    w.applySampleLoad(load);
    checkMemoryBox(w);
    alertButton(w, 0);
    CHECK(h.state.digidrums[3].empty());
    w.applySampleLoad(load);
    alertButton(w, 1);
    CHECK(h.state.digidrums[3].size() == 500 * kKb);
    // A small one before it that fits: loaded at once, the large one still silent.
    w.applySampleLoad([](plugin::NonParamState& s) { s.digidrums[2] = std::vector<uint8_t>(kKb, 8); });
    CHECK_FALSE(topAlert(w));
    CHECK(h.state.digidrums[2].size() == kKb);
    // A small one after it would be silent too: asked.
    w.applySampleLoad([](plugin::NonParamState& s) { s.digidrums[9] = std::vector<uint8_t>(kKb, 8); });
    checkMemoryBox(w);
    alertButton(w, 0);
    CHECK(h.state.digidrums[9].empty());
}

TEST_CASE("STE memory: each pad plays the Digidrum chosen by its right-click menu, one undo step",
          "[ui][stemem][digidrum]") {
    UiHost h;
    h.p(0, Track::Generator) = float(ymengine::Generator::Digidrum);
    h.state.digidrums[0] = std::vector<uint8_t>(40, 8);
    h.state.digiSlots[0].name = "kick";
    h.state.digidrums[40] = std::vector<uint8_t>(40, 8);
    h.state.digiSlots[40].name = "clap";
    pluginui::MainWindow w(h);
    w.tick(0);
    const gemui::Button* pad2 = padButton(*w.desktop().root(), 2);
    REQUIRE(pad2);
    CHECK(pad2->text() == "2");
    // Its Digidrum is empty: the pad greyed, a click does not choose it, the right click stays (Indus, 30/10/2026
    // 02:07).
    CHECK(pad2->greyed());
    CHECK_FALSE(padButton(*w.desktop().root(), 1)->greyed());
    CHECK(pad2->tip() ==
          "Pad 2: Digidrum 2, empty. Right click: the Digidrum this pad plays. In the sequencer, row 2 plays it.");
    click(w, "genSlot1");
    CHECK(h.state.tracks[0].patch.digidrumSlot == 0);

    // The Digidrums loaded and the pad's own (empty), checked; then Number...
    click(w, "genSlot1", gemui::MouseButton::Right);
    CHECK(menuLabels(w) ==
          std::vector<std::string>{"Digidrum 1 \"kick\"", "Digidrum 2", "Digidrum 41 \"clap\"", "---", "Number..."});
    CHECK(topMenu(w)->checked() == 1);
    pickItem(w, 2);
    CHECK(h.state.tracks[0].patch.digiPads[1] == 40);
    w.tick(10);
    CHECK(pad2->text() == "41");
    CHECK(pad2->tip().rfind("Pad 2: Digidrum 41 \"clap\". Click: the keyboard plays it.", 0) == 0);
    CHECK_FALSE(pad2->greyed());
    click(w, "genSlot1");
    CHECK(h.state.tracks[0].patch.digidrumSlot == 1);
    CHECK(ctrlKey(w, U'z'));
    CHECK(h.state.tracks[0].patch.digiPads[1] == 1);

    // Number...: any of the 127, out of range clamped.
    click(w, "genSlot1", gemui::MouseButton::Right);
    pickItem(w, 4);
    answer(w, "100");
    CHECK(h.state.tracks[0].patch.digiPads[1] == 99);
    click(w, "genSlot1", gemui::MouseButton::Right);
    pickItem(w, 4);
    answer(w, "999");
    CHECK(h.state.tracks[0].patch.digiPads[1] == 126);
    // Not a number: nothing changes.
    click(w, "genSlot1", gemui::MouseButton::Right);
    pickItem(w, 4);
    answer(w, "x");
    CHECK(h.state.tracks[0].patch.digiPads[1] == 126);

    // Another generator: no menu.
    h.p(0, Track::Generator) = float(ymengine::Generator::Chip);
    w.tick(20);
    if (w.rectOf("genSlot1")) {
        click(w, "genSlot1", gemui::MouseButton::Right);
        CHECK_FALSE(topMenu(w));
    }
}

TEST_CASE("Digidrum pads: the arrows give the pads the eight Digidrums before or after, 127 in all",
          "[ui][stemem][digidrum]") {
    UiHost h;
    h.p(0, Track::Generator) = float(ymengine::Generator::Digidrum);
    pluginui::MainWindow w(h);
    w.tick(0);
    REQUIRE(w.rectOf("genDigiPrev"));
    REQUIRE(w.rectOf("genDigiNext"));
    // Under the label, left of Digidrum samples, on its line.
    CHECK(w.rectOf("genDigiNext")->right() < w.rectOf("digiEdit")->x);
    CHECK(w.rectOf("genDigiPrev")->y == w.rectOf("digiEdit")->y);
    CHECK(w.rectOf("genDigiPrev")->right() <= w.rectOf("genSlot0")->x);
    const auto first = ymengine::TrackPatch{}.digiPads;
    const gemui::Button* pad1 = padButton(*w.desktop().root(), 1);
    const gemui::Button* pad8 = padButton(*w.desktop().root(), 8);
    REQUIRE(pad1);
    REQUIRE(pad8);

    // First page: the arrow to the left does nothing.
    const size_t undo = h.editor.undo.undoSize();
    click(w, "genDigiPrev");
    CHECK(h.state.tracks[0].patch.digiPads == first);
    CHECK(h.editor.undo.undoSize() == undo);

    // The page after: Digidrums 9 to 16, one undo step.
    click(w, "genDigiNext");
    w.tick(10);
    for (int k = 0; k < 8; ++k) CHECK(h.state.tracks[0].patch.digiPads[size_t(k)] == 8 + k);
    CHECK(pad1->text() == "9");
    CHECK(pad8->text() == "16");
    CHECK(h.editor.undo.undoSize() == undo + 1);
    CHECK(ctrlKey(w, U'z'));
    CHECK(h.state.tracks[0].patch.digiPads == first);

    // The last page ends on the 127th: Digidrums 120 to 127.
    for (int p = 0; p < 20; ++p) click(w, "genDigiNext");
    w.tick(20);
    for (int k = 0; k < 8; ++k) CHECK(h.state.tracks[0].patch.digiPads[size_t(k)] == 119 + k);
    CHECK(pad1->text() == "120");
    CHECK(pad8->text() == "127");
    click(w, "genDigiPrev");
    for (int k = 0; k < 8; ++k) CHECK(h.state.tracks[0].patch.digiPads[size_t(k)] == 112 + k);

    // Pads chosen one by one: the arrows start from the page of pad 1.
    h.state.tracks[0].patch.digiPads = {16, 40, 18, 19, 20, 21, 22, 23};
    ++h.serial;
    w.tick(30);
    click(w, "genDigiNext");
    for (int k = 0; k < 8; ++k) CHECK(h.state.tracks[0].patch.digiPads[size_t(k)] == 24 + k);
}
