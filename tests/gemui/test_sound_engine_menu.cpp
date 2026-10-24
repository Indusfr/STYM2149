// STYM2149 — Options > Sound engine: entries greyed out according to the machine (STE hardware with Computer ST,
// D-206; ZX Spectrum and Amstrad CPC clocks and Free timer in Authentic 3ch, D-207; Custom clock outside 16 ch,
// D-237), with their reason, values already chosen kept and listed by the CHIP monitor; the Custom clock's prompt box,
// in MHz (D-239); no Arp division (D-357).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "UiHost.h"
#include "gemui/Alert.h"
#include "gemui/Menu.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "ymengine/Params.h"

using namespace stym;
using namespace stym::ui_testing;
using plugin::Global;
using plugin::Track;
using ymengine::Generator;

namespace {

const gemui::MenuList& topList(pluginui::MainWindow& w) {
    const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
    REQUIRE(m);
    return *m;
}

int indexOf(const std::vector<gemui::MenuItem>& items, const std::string& label) {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].label == label) return int(i);
    FAIL("no entry " << label);
    return -1;
}

// Entries of Options > Sound engine, as the menu builds them.
std::vector<gemui::MenuItem> soundEngine(pluginui::MainWindow& w) {
    w.openMenu(3);
    const auto& items = topList(w).items();
    std::vector<gemui::MenuItem> sub = items[size_t(indexOf(items, "Sound engine"))].sub;
    w.desktop().closePopups();
    return sub;
}

const gemui::MenuItem& entry(const std::vector<gemui::MenuItem>& items, const std::string& label) {
    return items[size_t(indexOf(items, label))];
}

// Text of the CHIP alert (the list of chipIssues), closed again.
std::string chipAlert(pluginui::MainWindow& w) {
    w.showChipAlert();
    const auto* a = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
    REQUIRE(a);
    const std::string text = a->text();
    gemui::KeyEvent e;
    e.key = gemui::Key::Enter;
    w.desktop().keyDown(e);
    REQUIRE_FALSE(w.desktop().topModal());
    return text;
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

constexpr const char* kClockWhy = "Authentic 3ch: an Atari ST or STE clocks its YM at 2 MHz";
constexpr const char* kTimerWhy = "Authentic 3ch: an Atari ST or STE times its effects with the MFP";
constexpr const char* kCustomWhy = "Custom clock: 16 ch only (2 MHz in the other modes)";

// Types `text` in the open prompt box after clearing its field, then presses Enter (or Escape).
void typePrompt(pluginui::MainWindow& w, const std::string& text, bool ok = true) {
    const auto* a = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
    REQUIRE(a);
    for (int i = 0; i < 12; ++i) key(w, gemui::Key::Backspace);
    for (const char c : text) w.desktop().keyDown(charKey(char32_t(c)));
    key(w, ok ? gemui::Key::Enter : gemui::Key::Escape);
    REQUIRE_FALSE(w.desktop().topModal());
}

}  // namespace

TEST_CASE("Sound engine: STE output chain, bass and treble greyed out with Computer ST (D-206)", "[ui][menu]") {
    UiHost h;
    h.p(Global::OutputChain) = 2;  // STE, chosen before
    h.p(Global::SteBass) = 4;
    pluginui::MainWindow w(h);
    w.tick(0);

    // Computer ST (default).
    {
        const auto items = soundEngine(w);
        for (const char* label : {"STE bass", "STE treble"}) {
            CAPTURE(label);
            CHECK(entry(items, label).disabled);
            CHECK(entry(items, label).why == "STE only: set Computer to STE");
        }
        const gemui::MenuItem& chain = entry(items, "Output chain");
        CHECK_FALSE(chain.disabled);
        REQUIRE(chain.sub.size() == 3);
        CHECK_FALSE(chain.sub[0].disabled);  // Raw
        CHECK_FALSE(chain.sub[1].disabled);  // ST
        CHECK(chain.sub[2].disabled);        // STE
        CHECK(chain.sub[2].why == "STE only: set Computer to STE");
        CHECK(chain.subCur == 2);  // the value chosen before stays, ticked
        for (const char* label : {"Computer", "Chip clock", "Resampling", "Soft clip", "Timer"})
            CHECK_FALSE(entry(items, label).disabled);
    }
    // Nothing is changed behind the user's back.
    CHECK(h.p(Global::OutputChain) == 2.0f);
    CHECK(h.p(Global::SteBass) == 4.0f);

    // Edit > Samples > DMA (STE) greyed out too (fourth DAW test, item 36, D-234; D-206 had kept it available).
    w.openMenu(2);
    const auto& edit = topList(w).items();
    const gemui::MenuItem& samples = edit[size_t(indexOf(edit, "Samples"))];
    const gemui::MenuItem& dma = samples.sub[size_t(indexOf(samples.sub, "DMA (STE)"))];
    CHECK(dma.disabled);
    CHECK(dma.why == "STE only: set Computer to STE");
    w.desktop().closePopups();

    // Hovering the greyed-out STE bass does not open its submenu; a click neither.
    w.openMenu(3);
    {
        const gemui::MenuList& options = topList(w);
        const gemui::Rect r = options.itemRect(indexOf(options.items(), "Sound engine"));
        w.desktop().mouseMove(centre(r), {});
    }
    const gemui::MenuList& engine = topList(w);
    REQUIRE(engine.items().front().label == "Computer");
    const gemui::Rect bass = engine.itemRect(indexOf(engine.items(), "STE bass"));
    press(w, centre(bass));
    CHECK(w.desktop().topPopup() == &engine);
    CHECK(engine.tipAt(centre(bass)) == "STE only: set Computer to STE");
    const gemui::Rect clock = engine.itemRect(indexOf(engine.items(), "Chip clock"));
    w.desktop().mouseMove(centre(clock), {});
    CHECK(w.desktop().topPopup() != &engine);  // an available submenu opens
    w.desktop().closePopups();

    // Computer STE: everything available.
    h.p(Global::Computer) = 1;
    w.tick(0);
    const auto ste = soundEngine(w);
    CHECK_FALSE(entry(ste, "STE bass").disabled);
    CHECK_FALSE(entry(ste, "STE treble").disabled);
    CHECK_FALSE(entry(ste, "Output chain").sub[2].disabled);
}

TEST_CASE("Sound engine: ZX Spectrum and Amstrad CPC clocks and Free timer greyed out in Authentic 3ch (D-207)",
          "[ui][menu]") {
    for (const int mode : {0, 1, 2}) {  // 16 ch, Authentic 3ch, Dual chip 2x3ch
        CAPTURE(mode);
        UiHost h;
        h.p(Global::VoiceMode) = float(mode);
        pluginui::MainWindow w(h);
        w.tick(0);
        const bool authentic = mode == 1;
        const auto items = soundEngine(w);
        const gemui::MenuItem& clock = entry(items, "Chip clock");
        const gemui::MenuItem& timer = entry(items, "Timer");
        CHECK_FALSE(clock.disabled);
        CHECK_FALSE(timer.disabled);
        REQUIRE(clock.sub.size() == 4);
        CHECK(clock.sub[0].disabled == (mode != 0));  // Custom, 16 ch only (D-237)
        CHECK(clock.sub[0].why == (mode != 0 ? kCustomWhy : ""));
        CHECK(clock.sub[1].label == "Atari ST 2.0053 MHz");  // without "PAL" (D-258)
        CHECK_FALSE(clock.sub[1].disabled);
        for (const size_t i : {size_t(2), size_t(3)}) {  // ZX Spectrum, Amstrad CPC
            CAPTURE(clock.sub[i].label);
            CHECK(clock.sub[i].disabled == authentic);
            CHECK(clock.sub[i].why == (authentic ? kClockWhy : ""));
        }
        REQUIRE(timer.sub.size() == 2);
        CHECK(timer.sub[0].label == "MFP");
        CHECK_FALSE(timer.sub[0].disabled);
        CHECK(timer.sub[1].label == "Free");
        CHECK(timer.sub[1].disabled == authentic);
        CHECK(timer.sub[1].why == (authentic ? kTimerWhy : ""));
        // Kept available: they still play on an ST or STE (output stage, settings that a replayer can follow).
        for (const char* label : {"Computer", "Output chain", "Resampling", "Soft clip", "Quantize to VBL", "Velocity"}) {
            CAPTURE(label);
            CHECK_FALSE(entry(items, label).disabled);
        }
    }
}

TEST_CASE("Authentic 3ch: a clock or a timer chosen before is kept, and the CHIP monitor lists it (D-207)",
          "[ui][menu]") {
    UiHost h;
    h.p(Global::VoiceMode) = 1;
    h.p(Global::ChipClock) = 2;  // ZX Spectrum, chosen in 16 ch or by the host
    h.p(Global::Timer) = 1;      // Free
    h.p(0, Track::Generator) = float(int(Generator::Sid));
    h.state.seq.cur().edit(0).pat[0] = 0;  // a note: the track is live
    pluginui::MainWindow w(h);
    w.tick(0);

    const auto items = soundEngine(w);
    CHECK(entry(items, "Chip clock").subCur == 2);  // ticked, although greyed out
    CHECK(entry(items, "Timer").subCur == 1);
    CHECK(h.p(Global::ChipClock) == 2.0f);  // nothing is changed behind the user's back
    CHECK(h.p(Global::Timer) == 1.0f);

    const std::string zx = chipAlert(w);
    CHECK(has(zx, "Chip clock ZX Spectrum 1.7734 MHz is not an Atari clock"));
    CHECK(has(zx, "Timer Free: exact timer rates"));

    // Picking an available entry replaces the value; its line goes away. Atari ST is an Atari clock: not listed.
    entry(items, "Chip clock").sub[1].pick();  // Atari ST
    entry(items, "Timer").sub[0].pick();       // MFP
    w.tick(0);
    CHECK(h.p(Global::ChipClock) == 1.0f);
    CHECK(h.p(Global::Timer) == 0.0f);
    const std::string atari = chipAlert(w);
    CHECK_FALSE(has(atari, "Chip clock"));
    CHECK_FALSE(has(atari, "Timer Free"));

    // The Custom entry plays 2 MHz in Authentic 3ch, whatever its frequency (D-237): not listed.
    h.p(Global::ChipClock) = 0;
    h.p(Global::ChipClockHz) = 3000000;
    w.tick(0);
    CHECK_FALSE(has(chipAlert(w), "Chip clock"));
    // The Amstrad CPC clock is listed in every mode, greyed out or not.
    h.p(Global::ChipClock) = 3;
    h.p(Global::VoiceMode) = 0;
    w.tick(0);
    CHECK(has(chipAlert(w), "Chip clock Amstrad CPC 1 MHz is not an Atari clock"));
    // A Free timer without a timer effect (a Chip Voice) changes nothing in the exports: not listed.
    h.p(Global::Timer) = 1;
    h.p(0, Track::Generator) = float(int(Generator::Chip));
    w.tick(0);
    CHECK_FALSE(has(chipAlert(w), "Timer Free"));
}

TEST_CASE("CHIP monitor: the ADSR of a Sync Buzzer is listed where it plays, not in Authentic 3ch (D-207)",
          "[ui][chip]") {
    for (const int mode : {0, 1}) {
        CAPTURE(mode);
        UiHost h;
        h.p(Global::VoiceMode) = float(mode);
        h.p(0, Track::Generator) = float(int(Generator::SyncBuzzer));
        h.p(0, Track::Attack) = 40;
        h.p(0, Track::AdsrMode) = 0;  // Smooth: Authentic 3ch plays the Chip ADSR, inert on a Sync Buzzer
        h.state.seq.cur().edit(0).pat[0] = 0;
        pluginui::MainWindow w(h);
        w.tick(0);
        CHECK(has(chipAlert(w), "ADSR on a Sync Buzzer") == (mode == 0));
    }
}

TEST_CASE("Sound engine: Custom clock typed in MHz in a prompt box, in 16 ch only (DAW tests, items 39 and 41, "
          "D-237, D-239)",
          "[ui][menu]") {
    UiHost h;  // 16 ch
    h.p(0, Track::Generator) = float(int(Generator::Sid));
    h.state.seq.cur().edit(0).pat[0] = 0;
    pluginui::MainWindow w(h);
    w.tick(0);
    auto custom = [&w] { return entry(soundEngine(w), "Chip clock").sub[0]; };
    CHECK(custom().label == "Custom 2.0000 MHz...");
    CHECK_FALSE(custom().disabled);
    CHECK(entry(soundEngine(w), "Chip clock").subCur == 0);
    CHECK_FALSE(has(chipAlert(w), "Chip clock"));  // 2 MHz by default: the clock of the 2 MHz entry it replaces

    custom().pick();
    const auto* a = dynamic_cast<const gemui::Alert*>(w.desktop().topModal());
    REQUIRE(a);
    CHECK(a->text() == "Custom chip clock in MHz\n(0.1000 to 8.0000):");
    REQUIRE(a->buttonCount() == 2);
    CHECK(a->button(1).text() == "OK");
    typePrompt(w, "2.4576");
    w.tick(0);
    CHECK(h.p(Global::ChipClockHz) == 2457600.0f);
    CHECK(h.p(Global::ChipClock) == 0.0f);
    CHECK(custom().label == "Custom 2.4576 MHz...");
    CHECK(plugin::chipClock(h.values).hz() == 2457600.0);
    CHECK(has(chipAlert(w), "Chip clock Custom 2.4576 MHz is not an Atari clock"));

    // A comma for the point, whole MHz, a fifth decimal rounded; a value in Hz shown to the nearest 100 Hz.
    custom().pick();
    typePrompt(w, "1,7734");
    CHECK(h.p(Global::ChipClockHz) == 1773400.0f);
    custom().pick();
    typePrompt(w, "4");
    CHECK(h.p(Global::ChipClockHz) == 4000000.0f);
    custom().pick();
    typePrompt(w, "1.00005");
    CHECK(h.p(Global::ChipClockHz) == 1000100.0f);
    h.p(Global::ChipClockHz) = 1773447;
    w.tick(0);
    CHECK(custom().label == "Custom 1.7734 MHz...");
    h.p(Global::ChipClockHz) = 1999960;
    w.tick(0);
    CHECK(custom().label == "Custom 2.0000 MHz...");

    // Out of range: clamped; Escape or an empty field: nothing changes.
    custom().pick();
    typePrompt(w, "0.05");
    CHECK(h.p(Global::ChipClockHz) == 100000.0f);
    custom().pick();
    typePrompt(w, "9.9999");
    CHECK(h.p(Global::ChipClockHz) == 8000000.0f);
    custom().pick();
    typePrompt(w, "3", false);
    CHECK(h.p(Global::ChipClockHz) == 8000000.0f);
    custom().pick();
    typePrompt(w, "");
    CHECK(h.p(Global::ChipClockHz) == 8000000.0f);

    // From another clock: OK picks the Custom clock.
    h.p(Global::ChipClock) = 2;
    w.tick(0);
    custom().pick();
    typePrompt(w, "1.0000");
    CHECK(h.p(Global::ChipClock) == 0.0f);
    CHECK(h.p(Global::ChipClockHz) == 1000000.0f);

    // Authentic 3ch and Dual chip: greyed out, ticked, 2 MHz played and shown.
    for (const int mode : {1, 2}) {
        CAPTURE(mode);
        h.p(Global::VoiceMode) = float(mode);
        w.tick(0);
        CHECK(custom().disabled);
        CHECK(custom().label == "Custom 2.0000 MHz...");
        CHECK(entry(soundEngine(w), "Chip clock").subCur == 0);
        CHECK(plugin::chipClock(h.values).hz() == 2e6);
        CHECK_FALSE(has(chipAlert(w), "Chip clock"));
    }
}

TEST_CASE("Sound engine: no Arp division, set in the Arpeggiator window (twelfth DAW test, item 148, D-357)",
          "[ui][menu]") {
    UiHost h;
    pluginui::MainWindow w(h);
    const auto items = soundEngine(w);
    CHECK(std::none_of(items.begin(), items.end(), [](const gemui::MenuItem& m) { return m.label == "Arp division"; }));
    CHECK(items.back().label == "Wheel depth");
}
