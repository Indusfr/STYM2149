// STYM2149 — Settings window (Indus, 25/10/2026 19:33, D-430): opened by Options > Settings..., it holds what the
// Options menu held in submenus (Sound engine, Keyboard layout, Note names, Renderer, Pixels, Tooltips) and the Tuning
// of A4 (D-429): frames, values shown on the buttons, the window following the parameters, the A4 typed in Hz,
// preferences stored, the window kept open from one opening of the editor to the next; Cancel and OK (D-435), the box
// that Cancel opens before changes are lost (D-437), the unit of Tuning A4 left out of the pointer's reverse video
// (D-438).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "Scenes.h"
#include "UiHost.h"
#include "gemui/Menu.h"
#include "plugincore/Parameters.h"

using namespace stym;
using namespace stym::ui_testing;
using plugin::Global;

namespace {

const gemui::Button& button(pluginui::MainWindow& w, std::string_view id) {
    const gemui::Button* b = w.settingsWindow().button(id);
    REQUIRE(b);
    return *b;
}

// The GEM box that Cancel opens when changes would be lost (D-437); OK lets them go.
void discard(pluginui::MainWindow& w) {
    const gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "Discard the changes?\nThe settings changed in this window will be lost.");
    alertButton(w, size_t(gemui::Alert::kConfirmOk));
    w.tick(0);
}

// Types `text` in the Tuning A4 field after clearing it, then Enter.
void typeA4(pluginui::MainWindow& w, std::string_view text) {
    click(w, "setA4");
    for (int i = 0; i < 10; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) w.desktop().keyDown(charKey(char32_t(c)));
    key(w, gemui::Key::Enter);
    w.tick(0);
}

}  // namespace

TEST_CASE("Settings: Options > Settings... opens the window, the Options menu keeps its other entries (D-430)",
          "[ui][settings]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.openMenu(3);
    CHECK(menuLabels(w) == std::vector<std::string>{"Audio/MIDI settings", "---", "MIDI learn", "Clear MIDI map", "---",
                                                    "Oscilloscope", "Register monitor", "Note monitor", "---",
                                                    "Settings..."});
    CHECK_FALSE(w.settingsWindow().isOpen());
    pickItem(w, 9);
    REQUIRE(w.settingsWindow().isOpen());

    // Three frames side by side, SOUND ENGINE, MIDI and INTERFACE (D-432), inside the window and the desktop.
    const gemui::Rect win = *w.rectOf("settingsWin"), engine = *w.rectOf("setEngine"), midi = *w.rectOf("setMidi"),
                      ui = *w.rectOf("setInterface");
    CHECK(win.x >= 0);
    CHECK(win.y >= 0);
    CHECK(win.right() <= pluginui::MainWindow::kWidth);
    CHECK(win.bottom() <= pluginui::MainWindow::kHeight);
    for (const gemui::Rect& f : {engine, midi, ui}) {
        CHECK(f.x > win.x);
        CHECK(f.right() < win.right());
        CHECK(f.bottom() < win.bottom());
    }
    CHECK(engine.right() < midi.x);
    CHECK(midi.right() < ui.x);
    CHECK(engine.y == midi.y);
    CHECK(midi.y == ui.y);
    CHECK(engine.bottom() == midi.bottom());  // the columns end together
    CHECK(midi.bottom() == ui.bottom());

    // Every control in its frame, one below the other.
    auto inside = [](const gemui::Rect& r, const gemui::Rect& f) {
        return r.x > f.x && r.right() < f.right() && r.y > f.y && r.bottom() < f.bottom();
    };
    int lastBottom = 0;
    std::string_view lastFrame;
    for (const std::string_view id : pluginui::SettingsWindow::menuIds()) {
        CAPTURE(id);
        const gemui::Rect r = *w.rectOf(id);
        const bool inEngine = inside(r, engine), inMidi = inside(r, midi), inUi = inside(r, ui);
        CHECK(int(inEngine) + int(inMidi) + int(inUi) == 1);
        const std::string_view frame = inEngine ? "engine" : inMidi ? "midi" : "ui";
        if (frame == lastFrame) CHECK(r.y >= lastBottom);
        lastFrame = frame;
        lastBottom = r.bottom();
    }
    CHECK(inside(*w.rectOf("setA4"), engine));
    CHECK(w.rectOf("setA4")->y > w.rectOf("setClock")->bottom());
    CHECK(w.rectOf("setA4")->bottom() < w.rectOf("setChain")->y);
    CHECK(inside(*w.rectOf("setFolder"), ui));
    CHECK(w.rectOf("setFolder")->y > w.rectOf("setLanguage")->bottom());

    // The values by default, on the buttons.
    const std::pair<std::string_view, std::string_view> values[] = {
        {"setComputer", "ST"},       {"setClock", "Custom 2.0000 MHz"}, {"setChain", "Raw"},
        {"setResampling", "Sinc"},   {"setBass", "0 dB"},               {"setTreble", "0 dB"},
        {"setSoftClip", "On"},       {"setTimer", "MFP"},               {"setVbl", "50 Hz (PAL)"},
        {"setQuantize", "Off"},
        {"setRouting", "Omni"},      {"setVelocity", "Accent"},         {"setBendTarget", "Pitch"},
        {"setBend", "2 st"},         {"setWheelTarget", "Vibrato"},     {"setWheel", "50 ct"},
        {"setAftertouch", "Vibrato"}, {"setMpe", "Off"},                {"setMpeBend", "48 st"},
        {"setMpeSlide", "Volume"},   {"setPixels", "Smooth"},           {"setRenderer", "Software"},
        {"setNoteNames", "English (C D E)"}, {"setLayout", "Auto (detect)"}, {"setTips", "On"},
        {"setLanguage", "English"}};
    for (const auto& [id, text] : values) {
        CAPTURE(id);
        CHECK(button(w, id).text() == text);
    }
    CHECK(w.settingsWindow().tuningField().text() == "440.00 Hz");
    // Each button shows its menu below it, the current value checked.
    click(w, "setResampling");
    REQUIRE(topMenu(w));
    CHECK(menuLabels(w) == std::vector<std::string>{"Sinc", "Box (AtariAudio)", "Retro"});
    CHECK(topMenu(w)->checked() == 0);
    pickItem(w, 2);
    CHECK(h.p(Global::Resampling) == 2.0f);
    w.tick(0);
    CHECK(button(w, "setResampling").text() == "Retro");
    // The Wheel depth in steps of 25 cents, as the Options menu had them.
    const gemui::MenuItem wheel = setting(w, "setWheel");
    REQUIRE(wheel.sub.size() == 9);
    CHECK(wheel.sub.front().label == "0 ct");
    CHECK(wheel.sub.back().label == "200 ct");
    CHECK(wheel.subCur == 2);
}

TEST_CASE("Settings: the window follows the parameters, the STE settings greyed out with Computer ST (D-206, D-430)",
          "[ui][settings]") {
    UiHost h;
    h.p(Global::SteBass) = -6;
    pluginui::MainWindow w(h);
    w.settingsWindow().show();
    for (const char* id : {"setBass", "setTreble"}) {
        CAPTURE(id);
        CHECK_FALSE(button(w, id).enabled());
        CHECK(button(w, id).tip() == "STE only: set Computer to STE");
    }
    CHECK(button(w, "setBass").text() == "-6 dB");
    // Computer picked in the window: the STE settings available, the SYSTEM frame's button follows.
    click(w, "setComputer");
    pickItem(w, 1);
    w.tick(0);
    CHECK(h.p(Global::Computer) == 1.0f);
    CHECK(button(w, "setComputer").text() == "STE");
    CHECK(button(w, "setBass").enabled());
    CHECK(button(w, "setBass").tip() == "STE bass: -12 to +12 dB, with the STE output chain");
    // A parameter changed by the host (automation, a preset): shown at the next tick.
    h.p(Global::MidiRouting) = 2;
    h.p(Global::BendRange) = 12;
    w.tick(0);
    CHECK(button(w, "setRouting").text() == "Per channel");
    CHECK(button(w, "setBend").text() == "12 st");
}

TEST_CASE("Settings > MIDI: the targets of the expression, MPE in 16 ch only (D-432)", "[ui][settings]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.settingsWindow().show();
    // The menus of the targets, in the engine's order.
    click(w, "setBendTarget");
    CHECK(menuLabels(w) == std::vector<std::string>{"Pitch", "Volume", "Envelope"});
    pickItem(w, 2);
    CHECK(h.p(Global::BendTarget) == 2.0f);
    click(w, "setWheelTarget");
    CHECK(menuLabels(w) == std::vector<std::string>{"Off", "Vibrato", "Volume", "Envelope", "LFO"});
    pickItem(w, 4);
    CHECK(h.p(Global::WheelTarget) == 4.0f);
    click(w, "setAftertouch");
    CHECK(menuLabels(w) == std::vector<std::string>{"Off", "Vibrato", "Volume", "Envelope", "LFO"});
    pickItem(w, 2);
    CHECK(h.p(Global::Aftertouch) == 2.0f);
    w.tick(0);
    CHECK(button(w, "setBendTarget").text() == "Envelope");
    CHECK(button(w, "setWheelTarget").text() == "LFO");
    CHECK(button(w, "setAftertouch").text() == "Volume");
    // MPE off: its bend range and slide greyed out, their values kept.
    for (const char* id : {"setMpeBend", "setMpeSlide"}) {
        CAPTURE(id);
        CHECK_FALSE(button(w, id).enabled());
        CHECK(button(w, id).tip() == "MPE: set MPE to On");
    }
    click(w, "setMpe");
    pickItem(w, 1);
    w.tick(0);
    CHECK(h.p(Global::Mpe) == 1.0f);
    CHECK(button(w, "setMpeBend").enabled());
    CHECK(button(w, "setMpeSlide").enabled());
    // The MPE bend ranges: small ones, then the octaves; a value from the state outside them is listed too.
    const gemui::MenuItem bends = setting(w, "setMpeBend");
    REQUIRE(bends.sub.size() == 14);
    CHECK(bends.sub.front().label == "1 st");
    CHECK(bends.sub.back().label == "96 st");
    CHECK(bends.sub[size_t(bends.subCur)].label == "48 st");
    click(w, "setMpeBend");
    pickItem(w, 7);
    CHECK(h.p(Global::MpeBendRange) == 24.0f);
    h.p(Global::MpeBendRange) = 10;
    w.tick(0);
    CHECK(button(w, "setMpeBend").text() == "10 st");
    CHECK(setting(w, "setMpeBend").sub.size() == 15);
    click(w, "setMpeSlide");
    CHECK(menuLabels(w) == std::vector<std::string>{"Volume", "Envelope", "Vibrato"});
    pickItem(w, 2);
    CHECK(h.p(Global::MpeSlide) == 2.0f);
    // Authentic 3ch: MPE greyed out, 16 ch only.
    h.p(Global::VoiceMode) = 1;
    w.tick(0);
    for (const char* id : {"setMpe", "setMpeBend", "setMpeSlide"}) {
        CAPTURE(id);
        CHECK_FALSE(button(w, id).enabled());
        CHECK(button(w, id).tip() == "MPE: 16 ch only");
    }
    CHECK(button(w, "setMpe").text() == "On");
}

TEST_CASE("Settings > Tuning A4: typed in Hz, 350 to 480 Hz with two decimals (D-429, D-430, D-434)",
          "[ui][settings]") {
    using pluginui::SettingsWindow;
    CHECK(SettingsWindow::parseA4("432") == 432.0);
    CHECK(SettingsWindow::parseA4("445,5") == 445.5);
    CHECK(SettingsWindow::parseA4("440.144") == 440.14);
    CHECK(SettingsWindow::parseA4("500") == 480.0);
    CHECK(SettingsWindow::parseA4("392") == 392.0);
    CHECK(SettingsWindow::parseA4("300") == 350.0);
    CHECK_FALSE(SettingsWindow::parseA4("Hz"));
    CHECK_FALSE(SettingsWindow::parseA4(""));

    UiHost h;
    pluginui::MainWindow w(h);
    w.settingsWindow().show();
    CHECK(w.settingsWindow().tuningField().tip().rfind("Tuning: the frequency of A4", 0) == 0);
    typeA4(w, "432");
    CHECK(h.p(Global::A4Hz) == 432.0f);
    CHECK(w.settingsWindow().tuningField().text() == "432.00 Hz");
    typeA4(w, "441.75");
    CHECK(h.p(Global::A4Hz) == 441.75f);
    CHECK(w.settingsWindow().tuningField().text() == "441.75 Hz");
    typeA4(w, "999");
    CHECK(h.p(Global::A4Hz) == 480.0f);
    typeA4(w, "392");  // below 400 Hz (Indus, 25/10/2026 21:50, D-434)
    CHECK(h.p(Global::A4Hz) == 392.0f);
    CHECK(w.settingsWindow().tuningField().text() == "392.00 Hz");
    // Set by the host: shown at the next tick.
    h.p(Global::A4Hz) = 415.3f;
    w.tick(0);
    CHECK(w.settingsWindow().tuningField().text() == "415.30 Hz");
}

TEST_CASE("Settings > Tuning A4: Hz shown while the value is typed (D-434)", "[ui][settings]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.settingsWindow().show();
    const gemui::ValueField& f = w.settingsWindow().tuningField();
    click(w, "setA4");
    REQUIRE(f.editing());
    CHECK(f.shownText() == "440.00 Hz");
    for (int i = 0; i < 5; ++i) key(w, gemui::Key::Backspace);
    CHECK(f.shownText() == "___.__ Hz");
    for (char c : std::string("39")) w.desktop().keyDown(charKey(char32_t(c)));
    CHECK(f.shownText() == "_39.__ Hz");
    w.desktop().keyDown(charKey(U'5'));
    w.desktop().keyDown(charKey(U'.'));
    w.desktop().keyDown(charKey(U'5'));
    CHECK(f.shownText() == "395.5_ Hz");
    key(w, gemui::Key::Enter);
    w.tick(0);
    CHECK_FALSE(f.editing());
    CHECK(h.p(Global::A4Hz) == 395.5f);
    CHECK(f.shownText() == "395.50 Hz");
}

TEST_CASE("Settings: Note names and Keyboard layout stored as preferences, the windows follow (D-430)",
          "[ui][settings]") {
    UiHost h;
    pluginui::MainWindow w(h);
    gemui::MenuItem names = setting(w, "setNoteNames");
    REQUIRE(names.sub.size() == 2);
    CHECK(names.sub[1].label == "Latin (Do Re Mi)");
    CHECK(names.subCur == 0);
    names.sub[1].pick();
    CHECK(h.prefs["noteNames"] == "1");
    CHECK(button(w, "setNoteNames").text() == "Latin (Do Re Mi)");
    CHECK(setting(w, "setNoteNames").subCur == 1);

    gemui::MenuItem layout = setting(w, "setLayout");
    REQUIRE(layout.sub.size() == 4);
    CHECK(layout.sub[0].label == "Auto (detect)");
    CHECK(layout.subCur == 0);
    layout.sub[2].pick();
    CHECK(h.prefs["kbdLayout"] == "azerty");
    CHECK(w.keyLayout() == "azerty");
    CHECK(button(w, "setLayout").text() == "AZERTY");
}

TEST_CASE("Settings: the window open again at the next opening of the editor, where it was (D-430)",
          "[ui][settings]") {
    UiHost h;
    gemui::Rect was;
    {
        pluginui::MainWindow w(h);
        w.settingsWindow().show();
        w.settingsWindow().moveBy(40, 30);
        was = *w.rectOf("settingsWin");
    }
    pluginui::MainWindow w(h);
    REQUIRE(w.settingsWindow().isOpen());
    CHECK(*w.rectOf("settingsWin") == was);
    w.settingsWindow().close();
}

TEST_CASE("Settings: Cancel, the default button, and OK centred under the frames (D-435)", "[ui][settings]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.settingsWindow().show();
    const gemui::Button &cancel = button(w, "setCancel"), &ok = button(w, "setOk");
    CHECK(cancel.text() == "Cancel");
    CHECK(cancel.style() == gemui::Button::Style::Default);  // the bold border: Enter presses it
    CHECK(ok.text() == "OK");
    CHECK(ok.style() == gemui::Button::Style::Normal);
    CHECK(cancel.tip() == "Cancel: put back the settings changed since the window opened, then close it");
    CHECK(ok.tip() == "OK: keep the settings and close the window");
    const gemui::Rect win = *w.rectOf("settingsWin"), engine = *w.rectOf("setEngine"), ui = *w.rectOf("setInterface"),
                      c = *w.rectOf("setCancel"), o = *w.rectOf("setOk");
    CHECK(c.y > engine.bottom());
    CHECK(c.y == o.y);
    CHECK(c.right() + 14 == o.x);  // spaced as the buttons of a GEM box
    CHECK(std::abs((c.x + o.right()) - (engine.x + ui.right())) <= 1);  // centred under the frames
    CHECK(o.bottom() < win.bottom());
}

TEST_CASE("Settings: Cancel puts back the settings changed in the window, OK keeps them (D-435)", "[ui][settings]") {
    UiHost h;
    h.languages = gemshot::installedLanguages();
    pluginui::MainWindow w(h);
    pluginui::SettingsWindow& s = w.settingsWindow();
    // Changes of every kind: parameters (Computer twice, Bend range, Tuning A4), the Quantize of the state, the
    // preferences, the tooltips and their language.
    auto change = [&] {
        s.show();
        setting(w, "setComputer").sub[1].pick();
        setting(w, "setComputer").sub[0].pick();
        setting(w, "setComputer").sub[1].pick();
        click(w, "setBend");
        pickItem(w, 5);
        typeA4(w, "432");
        setting(w, "setVbl").sub[3].pick();
        setting(w, "setQuantize").sub[1].pick();
        setting(w, "setPixels").sub[0].pick();
        setting(w, "setNoteNames").sub[1].pick();
        setting(w, "setLayout").sub[2].pick();
        setting(w, "setTips").sub[1].pick();
        setting(w, "setLanguage").sub[3].pick();
        w.tick(0);
        REQUIRE(h.p(Global::Computer) == 1.0f);
        REQUIRE(h.p(Global::BendRange) != 2.0f);
        REQUIRE(h.p(Global::A4Hz) == 432.0f);
        REQUIRE(h.state.global.quantizeVbl);
        REQUIRE(h.prefs["noteNames"] == "1");
        REQUIRE(h.prefs["tipLanguage"] == "fr");
        REQUIRE_FALSE(w.desktop().tooltips());
    };
    change();
    click(w, "setCancel");
    discard(w);
    CHECK_FALSE(s.isOpen());
    CHECK(h.p(Global::Computer) == 0.0f);  // the value before the first change
    CHECK(h.p(Global::BendRange) == 2.0f);
    CHECK(h.p(Global::A4Hz) == 440.0f);
    CHECK(h.p(Global::VblRate) == 0.0f);
    CHECK_FALSE(h.state.global.quantizeVbl);
    CHECK(h.prefs["pixels"] == "smooth");
    CHECK(h.prefs["noteNames"] == "0");
    CHECK(h.prefs["kbdLayout"] == "auto");
    CHECK(w.keyLayout() != "azerty");
    CHECK(w.desktop().tooltips());
    CHECK(h.prefs["tooltips"] == "1");
    CHECK(h.prefs["tipLanguage"] == "en");

    // OK keeps them; a later Cancel puts back only what changed after the next opening.
    change();
    click(w, "setOk");
    w.tick(0);
    CHECK_FALSE(s.isOpen());
    CHECK(h.p(Global::Computer) == 1.0f);
    CHECK(h.p(Global::A4Hz) == 432.0f);
    CHECK(h.prefs["noteNames"] == "1");
    CHECK(h.prefs["tipLanguage"] == "fr");
    s.show();
    typeA4(w, "445");
    click(w, "setCancel");
    discard(w);
    CHECK(h.p(Global::A4Hz) == 432.0f);
    CHECK(h.p(Global::Computer) == 1.0f);
    CHECK(h.prefs["noteNames"] == "1");
}

TEST_CASE("Settings: the close box, Escape and Enter cancel; Enter types the A4 while it is edited (D-435)",
          "[ui][settings]") {
    UiHost h;
    pluginui::MainWindow w(h);
    pluginui::SettingsWindow& s = w.settingsWindow();
    s.show();
    typeA4(w, "432");  // Enter ends the typing, the window stays open
    REQUIRE(s.isOpen());
    CHECK(h.p(Global::A4Hz) == 432.0f);
    const gemui::Rect bar = s.titleBar().bounds();
    press(w, {bar.x + 12, bar.y + 12});  // the close box
    discard(w);
    CHECK_FALSE(s.isOpen());
    CHECK(h.p(Global::A4Hz) == 440.0f);

    s.show();
    typeA4(w, "450");
    key(w, gemui::Key::Escape);
    discard(w);
    CHECK_FALSE(s.isOpen());
    CHECK(h.p(Global::A4Hz) == 440.0f);

    s.show();
    typeA4(w, "460");
    click(w, "setBend");  // a menu open: Enter leaves the window alone
    key(w, gemui::Key::Enter);
    CHECK(s.isOpen());
    w.desktop().closePopups();
    key(w, gemui::Key::Enter);
    discard(w);
    CHECK_FALSE(s.isOpen());
    CHECK(h.p(Global::A4Hz) == 440.0f);

    // Another window in front: Enter is its own.
    s.show();
    typeA4(w, "470");
    w.sequencer().show();
    key(w, gemui::Key::Enter);
    CHECK(s.isOpen());
    CHECK(h.p(Global::A4Hz) == 470.0f);
}

TEST_CASE("Settings: Cancel asks before the changes are lost, only when a setting differs (D-437)", "[ui][settings]") {
    UiHost h;
    pluginui::MainWindow w(h);
    pluginui::SettingsWindow& s = w.settingsWindow();
    // Nothing changed, or a change undone in the window: closed without a box.
    s.show();
    click(w, "setCancel");
    CHECK_FALSE(topAlert(w));
    CHECK_FALSE(s.isOpen());
    s.show();
    typeA4(w, "432");
    typeA4(w, "440");
    setting(w, "setNoteNames").sub[1].pick();
    setting(w, "setNoteNames").sub[0].pick();
    CHECK_FALSE(s.changed());
    key(w, gemui::Key::Escape);
    CHECK_FALSE(topAlert(w));
    CHECK_FALSE(s.isOpen());

    // A change: the box, Cancel its default button, which keeps the window open with the change.
    s.show();
    typeA4(w, "432");
    CHECK(s.changed());
    click(w, "setCancel");
    const gemui::Alert* a = topAlert(w);
    REQUIRE(a);
    REQUIRE(a->buttonCount() == 2);
    CHECK(a->button(0).text() == "Cancel");
    CHECK(a->button(0).style() == gemui::Button::Style::Default);
    CHECK(a->button(1).text() == "OK");
    key(w, gemui::Key::Enter);  // the box's default button
    CHECK_FALSE(topAlert(w));
    CHECK(s.isOpen());
    CHECK(h.p(Global::A4Hz) == 432.0f);
    const gemui::Rect bar = s.titleBar().bounds();
    press(w, {bar.x + 12, bar.y + 12});  // the close box asks too
    REQUIRE(topAlert(w));
    key(w, gemui::Key::Escape);  // the box's Cancel
    CHECK(s.isOpen());
    CHECK(h.p(Global::A4Hz) == 432.0f);
    // OK never asks.
    click(w, "setOk");
    CHECK_FALSE(topAlert(w));
    CHECK_FALSE(s.isOpen());
    CHECK(h.p(Global::A4Hz) == 432.0f);
}
