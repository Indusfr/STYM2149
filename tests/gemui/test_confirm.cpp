// STYM2149 — gemui tests: confirm and prompt boxes from the keyboard (gemConfirm, gemPrompt; P417, P365) and the
// buttons of every confirm and prompt of the editor.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Prototype (checked in Chromium 141): #confirmWin shows Cancel (.dflt, 4 px frame, focused) then OK; Tab and
// Shift+Tab move the focus from one to the other, the focus ring rounding the corners of the focused button; Enter
// and Esc cancel whatever has the focus. #promptWin shows Cancel then OK with no default frame; the field has the
// focus, Tab goes to Cancel then OK; Enter in the field is OK, Enter on a focused button presses it; Esc cancels.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "UiHost.h"
#include "gemui/Alert.h"
#include "plugincore/DigidrumBanks.h"
#include "pluginui/AppIcon.h"

using namespace stym;
using namespace stym::ui_testing;
using gemui::Alert;
using gemui::Button;
using gemui::Desktop;
using gemui::Key;
using gemui::Rect;

namespace {

gemui::KeyEvent keyEvent(Key k, bool shift = false) {
    gemui::KeyEvent e;
    e.key = k;
    e.mods.shift = shift;
    return e;
}

struct Result {
    int button = -2;
    std::string text;
};

Alert& openConfirm(Desktop& d, Result& r) {
    Alert::Spec spec;
    spec.lines = {{"Clear track 1?"}, {"All its notes and settings will be lost."}};
    spec.confirmButtons();
    return Alert::open(d, std::move(spec), [&r](int b, const std::string& t) {
        r.button = b;
        r.text = t;
    });
}

Alert& openPrompt(Desktop& d, Result& r) {
    Alert::Spec spec;
    spec.lines = {{"Track name, 8 chars (blank = number):"}};
    spec.promptButtons();
    spec.inputText = "Bass";
    spec.inputChars = 10;
    return Alert::open(d, std::move(spec), [&r](int b, const std::string& t) {
        r.button = b;
        r.text = t;
    });
}

void clickAt(Desktop& d, gemui::Point p) {
    d.mouseMove(p, {});
    d.mouseDown(p, gemui::MouseButton::Left, {});
    d.mouseUp(p, {});
}

// Corner pixels of a button: black on its frame, white once the focus ring rounds them.
bool rounded(const Desktop& d, const Rect& r) {
    const gemui::Surface& s = d.surface();
    using gemui::colors::kBlack;
    using gemui::colors::kWhite;
    return s.at(r.x, r.y) == kWhite && s.at(r.right() - 1, r.y) == kWhite && s.at(r.x, r.bottom() - 1) == kWhite &&
           s.at(r.right() - 1, r.bottom() - 1) == kWhite && s.at(r.x + 1, r.y) == kBlack && s.at(r.x, r.y + 1) == kBlack;
}

bool square(const Desktop& d, const Rect& r) {
    const gemui::Surface& s = d.surface();
    return s.at(r.x, r.y) == gemui::colors::kBlack && s.at(r.right() - 1, r.bottom() - 1) == gemui::colors::kBlack;
}

// Box of the prototype's gemConfirm: Cancel framed, focused, then the action.
void checkConfirm(const Alert& a, const std::string& action) {
    REQUIRE(a.buttonCount() == 2);
    CHECK(a.button(0).text() == "Cancel");
    CHECK(a.button(0).style() == Button::Style::Default);
    CHECK(a.button(1).text() == action);
    CHECK(a.button(1).style() == Button::Style::Normal);
    CHECK(a.focusedButton() == 0);
    CHECK_FALSE(a.focusRing());
}

// Box of the prototype's gemPrompt: Cancel then OK, none framed, the field focused.
void checkPrompt(const Alert& a) {
    REQUIRE(a.buttonCount() == 2);
    CHECK(a.button(0).text() == "Cancel");
    CHECK(a.button(1).text() == "OK");
    CHECK(a.button(0).style() == Button::Style::Normal);
    CHECK(a.button(1).style() == Button::Style::Normal);
    CHECK(a.focusedButton() == -1);
}

void typeText(pluginui::MainWindow& w, const std::string& text) {
    for (int i = 0; i < 40; ++i) key(w, Key::Backspace);
    for (char c : text) {
        gemui::KeyEvent e;
        e.key = Key::Character;
        e.ch = char32_t(c);
        w.desktop().keyDown(e);
    }
}

}  // namespace

TEST_CASE("Confirm box: Tab and Shift+Tab move the focus between Cancel and OK (P417)", "[gemui][alert][keyboard]") {
    Desktop d(1272, 944);
    Result r;
    Alert& a = openConfirm(d, r);
    checkConfirm(a, "OK");
    CHECK(d.keyDown(keyEvent(Key::Tab)));
    CHECK(a.focusedButton() == 1);
    CHECK(a.focusRing());
    CHECK(d.keyDown(keyEvent(Key::Tab)));
    CHECK(a.focusedButton() == 0);
    CHECK(d.keyDown(keyEvent(Key::Tab, true)));
    CHECK(a.focusedButton() == 1);
    CHECK(d.keyDown(keyEvent(Key::Tab, true)));
    CHECK(a.focusedButton() == 0);
    CHECK(d.keyDown(keyEvent(Key::Tab, true)));
    CHECK(a.focusedButton() == 1);
    CHECK(r.button == -2);
    CHECK(d.hasModal());
}

TEST_CASE("Confirm box: Enter and Esc cancel, even with the focus on OK; a click on OK confirms (P417)",
          "[gemui][alert][keyboard]") {
    Desktop d(1272, 944);
    Result r;
    openConfirm(d, r);
    d.keyDown(keyEvent(Key::Enter));
    CHECK(r.button == 0);
    CHECK_FALSE(d.hasModal());

    r = {};
    openConfirm(d, r);
    d.keyDown(keyEvent(Key::Tab));
    d.keyDown(keyEvent(Key::Enter));
    CHECK(r.button == 0);
    CHECK_FALSE(d.hasModal());

    r = {};
    openConfirm(d, r);
    d.keyDown(keyEvent(Key::Escape));
    CHECK(r.button == 0);

    r = {};
    openConfirm(d, r);
    d.keyDown(keyEvent(Key::Tab));
    d.keyDown(keyEvent(Key::Escape));
    CHECK(r.button == 0);

    r = {};
    Alert& a = openConfirm(d, r);
    clickAt(d, centre(a.button(1).bounds()));
    CHECK(r.button == Alert::kConfirmOk);
    CHECK_FALSE(d.hasModal());
}

TEST_CASE("Confirm box: the focus ring follows Tab, the 4 px frame stays on Cancel (P417)", "[gemui][alert]") {
    Desktop d(1272, 944);
    Result r;
    Alert& a = openConfirm(d, r);
    const Rect cancel = a.button(0).bounds(), ok = a.button(1).bounds();
    d.paint();
    // Focus on Cancel when the box opens, but no ring before a key moves it (:focus-visible after a click).
    CHECK(square(d, cancel));
    CHECK(square(d, ok));
    d.keyDown(keyEvent(Key::Tab));
    d.paint();
    CHECK(rounded(d, ok));
    CHECK(square(d, cancel));
    d.keyDown(keyEvent(Key::Tab, true));
    d.paint();
    CHECK(rounded(d, cancel));
    CHECK(square(d, ok));
    // .dflt: 4 px frame on Cancel only.
    const gemui::Surface& s = d.surface();
    CHECK(s.at(cancel.x + 3, cancel.y + cancel.h / 2) == gemui::colors::kBlack);
    CHECK(s.at(ok.x + 3, ok.y + ok.h / 2) == gemui::colors::kWhite);
}

TEST_CASE("Prompt box: Cancel then OK; Tab goes field, Cancel, OK; Enter presses the focused button (P365)",
          "[gemui][alert][keyboard]") {
    Desktop d(1272, 944);
    Result r;
    Alert& a = openPrompt(d, r);
    checkPrompt(a);
    // Enter in the field: OK with the text.
    d.keyDown(keyEvent(Key::Enter));
    CHECK(r.button == Alert::kPromptOk);
    CHECK(r.text == "Bass");

    r = {};
    Alert& b = openPrompt(d, r);
    d.keyDown(keyEvent(Key::Tab));
    CHECK(b.focusedButton() == 0);
    d.keyDown(keyEvent(Key::Tab));
    CHECK(b.focusedButton() == 1);
    d.keyDown(keyEvent(Key::Tab));
    CHECK(b.focusedButton() == -1);
    d.keyDown(keyEvent(Key::Tab, true));
    CHECK(b.focusedButton() == 1);
    d.keyDown(keyEvent(Key::Tab, true));
    CHECK(b.focusedButton() == 0);
    d.keyDown(keyEvent(Key::Enter));  // Cancel has the focus
    CHECK(r.button == 0);

    r = {};
    openPrompt(d, r);
    d.keyDown(keyEvent(Key::Tab));
    d.keyDown(keyEvent(Key::Tab));
    d.keyDown(keyEvent(Key::Enter));  // OK has the focus
    CHECK(r.button == Alert::kPromptOk);

    r = {};
    openPrompt(d, r);
    d.keyDown(keyEvent(Key::Tab, true));
    d.keyDown(keyEvent(Key::Escape));
    CHECK(r.button == 0);
}

TEST_CASE("Message box: Enter and Esc close it, Tab only moves the focus to OK", "[gemui][alert][keyboard]") {
    Desktop d(1272, 944);
    Result r;
    Alert::Spec spec;
    spec.lines = {{"Not a valid patch library file."}};
    Alert& a = Alert::open(d, spec, [&r](int b, const std::string&) { r.button = b; });
    CHECK(a.focusedButton() == -1);
    d.keyDown(keyEvent(Key::Tab));
    CHECK(a.focusedButton() == 0);
    CHECK(d.hasModal());
    d.keyDown(keyEvent(Key::Enter));
    CHECK(r.button == 0);
    r = {};
    Alert::open(d, spec, [&r](int b, const std::string&) { r.button = b; });
    d.keyDown(keyEvent(Key::Escape));
    CHECK(r.button == 0);
}

TEST_CASE("Message box picture: centred above the text, each pixel a square, transparent pixels left out (D-235)",
          "[gemui][alert]") {
    Desktop d(1272, 944);
    constexpr gemui::Color kRed = 0xFFFF0000, kBlue = 0xFF0000FF;
    const gemui::Color px[4] = {kRed, 0x00000000, 0x00000000, kBlue};
    Alert::Spec spec;
    spec.lines = {{"STYM2149"}};
    Alert::Spec plain = spec;
    spec.picture = px;
    spec.pictureW = spec.pictureH = 2;
    spec.pictureScale = 3;
    const Rect without = Alert::open(d, plain).bounds();
    d.keyDown(keyEvent(Key::Escape));
    Alert& a = Alert::open(d, spec);
    const Rect box = a.bounds();
    // 6 px of picture and the 16 px of a paragraph below it.
    CHECK(box.h == without.h + 6 + 16);
    d.paint();
    const gemui::Surface& s = d.surface();
    Rect red{}, blue{};
    int reds = 0, blues = 0;
    for (int y = box.y; y < box.bottom(); ++y)
        for (int x = box.x; x < box.right(); ++x) {
            if (s.at(x, y) == kRed && reds++ == 0) red = {x, y, 0, 0};
            if (s.at(x, y) == kBlue && blues++ == 0) blue = {x, y, 0, 0};
        }
    CHECK(reds == 9);
    CHECK(blues == 9);
    CHECK(blue.x == red.x + 3);
    CHECK(blue.y == red.y + 3);
    CHECK(s.at(red.x + 3, red.y) == gemui::colors::kWhite);  // transparent pixel
    CHECK(std::abs((red.x + 3) - (box.x + box.w / 2)) <= 1);  // centred
    CHECK(red.y == box.y + 2 + 24);                             // top margin of the box
}

TEST_CASE("Information box: the STYM2149 icon above the text, OK framed as the default button, Enter closes it "
          "(D-235, D-236)",
          "[ui][alert]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.openMenu(0);
    pickItem(w, 0);
    const Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text().rfind("STYM2149\n", 0) == 0);
    REQUIRE(a->buttonCount() == 1);
    CHECK(a->button(0).text() == "OK");
    CHECK(a->button(0).style() == Button::Style::Default);
    w.desktop().paint();
    // The green pixels of the DIP chip (packaging/icon/STYM2149-32.png), each one doubled, above the text.
    const auto& icon = pluginui::icons::kApp;
    const auto isGreen = [](gemui::Color c) { return (c >> 24) && ((c >> 8) & 0xFF) > 0x60 && !((c >> 16) & 0xFF); };
    int expected = 0;
    for (int i = 0; i < icon.w * icon.h; ++i) expected += isGreen(icon.px[i]);
    const Rect box = a->bounds();
    const gemui::Surface& s = w.desktop().surface();
    int green = 0;
    for (int y = box.y; y < box.y + 2 + 24 + 2 * icon.h; ++y)
        for (int x = box.x; x < box.right(); ++x) green += isGreen(s.at(x, y));
    CHECK(expected > 100);
    CHECK(green == 4 * expected);
    key(w, Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
}

TEST_CASE("Clear track confirm: Cancel by default; Enter, Tab then Enter, and Esc keep the notes (P417)",
          "[ui][seq][alert]") {
    UiHost h;
    h.state.seq.cur().edit(0).pat[3] = 5;
    pluginui::MainWindow w(h);
    w.sequencer().show();
    click(w, "seqClear");
    const Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "Clear track 1?\nAll its notes and settings will be lost.");
    checkConfirm(*a, "OK");
    key(w, Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.state.seq.cur().lane(0).pat[3] == 5);

    click(w, "seqClear");
    key(w, Key::Tab);
    CHECK(topAlert(w)->focusedButton() == 1);
    key(w, Key::Enter);
    CHECK(h.state.seq.cur().lane(0).pat[3] == 5);

    click(w, "seqClear");
    key(w, Key::Tab);
    key(w, Key::Tab, true);
    key(w, Key::Escape);
    CHECK(h.state.seq.cur().lane(0).pat[3] == 5);

    click(w, "seqClear");
    alertButton(w, 1);
    CHECK(h.state.seq.cur().lane(0).pat[3] < 0);
}

TEST_CASE("Demo pattern and Quit confirms: Cancel by default, only OK goes on (P417)", "[ui][alert]") {
    UiHost h;
    h.state.seq.cur().edit(0).pat[0] = 5;  // channels 1-4 already contain data
    pluginui::MainWindow w(h);
    // File > Demo pattern: its ^D went to the DMA generator (eighth DAW test, item 100, D-301).
    auto demo = [&w] {
        w.openMenu(1);
        const std::vector<std::string> labels = menuLabels(w);
        const auto at = std::find(labels.begin(), labels.end(), "Demo pattern");
        REQUIRE(at != labels.end());
        CHECK(topMenu(w)->items()[size_t(at - labels.begin())].key.empty());
        pickItem(w, int(at - labels.begin()));
    };
    demo();
    const Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "Demo pattern\n\nChannels 1-4 already contain data.\nOverwrite them?");
    checkConfirm(*a, "OK");
    key(w, Key::Tab);
    key(w, Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.state.seq.cur().lane(1).pat[2] < 0);
    demo();
    alertButton(w, 1);
    CHECK(h.state.seq.cur().lane(1).pat[2] == 12);  // lead of the demo (doDemo)

    ctrlKey(w, U'q');
    a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "Quit STYM2149?\nUnsaved work will be lost.");
    checkConfirm(*a, "OK");
    key(w, Key::Enter);
    CHECK(h.quits == 0);
    ctrlKey(w, U'q');
    key(w, Key::Tab);
    key(w, Key::Escape);
    CHECK(h.quits == 0);
    ctrlKey(w, U'q');
    alertButton(w, 1);
    CHECK(h.quits == 1);
}

TEST_CASE("Quit in a plugin: greyed in the File menu, ^Q left to the host", "[ui][alert]") {
    struct PluginHost : UiHost {
        bool standalone() const override { return false; }
    };
    PluginHost h;
    pluginui::MainWindow w(h);
    w.openMenu(1);
    const std::vector<std::string> labels = menuLabels(w);
    const auto at = std::find(labels.begin(), labels.end(), "Quit");
    REQUIRE(at != labels.end());
    const gemui::MenuItem& quit = topMenu(w)->items()[size_t(at - labels.begin())];
    CHECK(quit.disabled);
    CHECK(quit.why == "The host closes the plugin");
    key(w, Key::Escape);
    CHECK_FALSE(ctrlKey(w, U'q'));
    CHECK_FALSE(topAlert(w));
    CHECK(h.quits == 0);
}

TEST_CASE("Quit: the close box of the standalone application's window opens the Quit box, once (D-439)",
          "[ui][alert]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.requestQuit();
    const Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "Quit STYM2149?\nUnsaved work will be lost.");
    CHECK(w.quitAsked());
    w.requestQuit();  // clicked again while the box is open: no second box
    w.desktop().keyDown(charKey(U'q', true));  // nor from ^Q, which goes to the box
    key(w, Key::Escape);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK_FALSE(w.quitAsked());
    CHECK(h.quits == 0);
    w.requestQuit();
    alertButton(w, 1);
    CHECK(h.quits == 1);

    // In a plugin the host closes the window: no box.
    struct PluginHost : UiHost {
        bool standalone() const override { return false; }
    };
    PluginHost p;
    pluginui::MainWindow pw(p);
    pw.requestQuit();
    CHECK_FALSE(topAlert(pw));
}

TEST_CASE("Recorder Clear confirm: Cancel, then Delete; Enter keeps the take (P417)", "[ui][alert]") {
    UiHost h;
    h.rec.takeFrames = 500;
    pluginui::MainWindow w(h);
    w.recorderWindow().show();
    click(w, "recClear");
    const Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "Delete the recorded take?\nIt cannot be undone.");
    checkConfirm(*a, "Delete");
    key(w, Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.rec.takeFrames == 500);
    click(w, "recClear");
    key(w, Key::Tab);
    key(w, Key::Enter);
    CHECK(h.rec.takeFrames == 500);
    click(w, "recClear");
    key(w, Key::Escape);
    CHECK(h.rec.takeFrames == 500);
    click(w, "recClear");
    alertButton(w, 1);
    CHECK(h.rec.takeFrames == 0);
}

TEST_CASE("Digidrum bank confirms follow gemConfirm: Cancel first, framed and focused (P417)",
          "[ui][digidrum][alert]") {
    UiHost h;
    h.digidrums = plugin::dumpDigidrumBanks({plugin::captureBank("Set A", h.state)});
    pluginui::MainWindow w(h);
    w.digidrums().show();
    // Banks > Load bank > Set A.
    click(w, "digBanks");
    REQUIRE(menuLabels(w) == std::vector<std::string>{"Save bank...", "Load bank", "---", "Delete bank"});
    hoverItem(w, 1);
    REQUIRE(menuLabels(w) == std::vector<std::string>{"Set A"});
    pickItem(w, 0);
    const Alert* a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "Replace the 8 slots with bank \"Set A\"?");
    checkConfirm(*a, "Load");
    const uint64_t before = h.serial;
    key(w, Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.serial == before);
    // Banks > Delete bank > Set A: Tab then Enter keeps it, Delete removes it.
    click(w, "digBanks");
    hoverItem(w, 3);
    pickItem(w, 0);
    a = topAlert(w);
    REQUIRE(a);
    CHECK(a->text() == "Delete bank \"Set A\" from the library?");
    checkConfirm(*a, "Delete");
    key(w, Key::Tab);
    key(w, Key::Enter);
    CHECK(w.digidrums().banks().size() == 1);
    click(w, "digBanks");
    hoverItem(w, 3);
    pickItem(w, 0);
    alertButton(w, 1);
    CHECK(w.digidrums().banks().empty());
}

TEST_CASE("Prompts of the editor follow gemPrompt: Cancel then OK, none framed, Enter is OK (P365)",
          "[ui][alert]") {
    UiHost h;
    pluginui::MainWindow w(h);

    // Track tab > Rename...
    click(w, "tab2", gemui::MouseButton::Right);
    REQUIRE(menuLabels(w) == std::vector<std::string>{"Copy settings", "Paste settings", "---", "Rename..."});
    pickItem(w, 3);
    const Alert* a = topAlert(w);
    REQUIRE(a);
    checkPrompt(*a);
    typeText(w, "Lead");
    key(w, Key::Enter);
    CHECK(h.state.trackNames[2] == "Lead");

    // Patches > Save current.
    w.patches().show();
    click(w, "libSave");
    a = topAlert(w);
    REQUIRE(a);
    checkPrompt(*a);
    key(w, Key::Escape);
    CHECK(h.patches.empty());
    click(w, "libSave");
    key(w, Key::Enter);
    CHECK(h.patches.find("Patch 1") != std::string::npos);

    // Digidrums: slot name, Banks > Save bank...
    w.digidrums().show();
    click(w, "digName");
    a = topAlert(w);
    REQUIRE(a);
    checkPrompt(*a);
    key(w, Key::Tab);  // Cancel
    key(w, Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
    click(w, "digBanks");
    pickItem(w, 0);
    a = topAlert(w);
    REQUIRE(a);
    checkPrompt(*a);
    typeText(w, "Kit");
    key(w, Key::Enter);
    CHECK(h.digidrums.find("Kit") != std::string::npos);

    // Step Sequencer: Resolution > Speed (Ticks timing).
    w.sequencer().show();
    click(w, "seqReso");
    pickItem(w, 11);  // Ticks: Speed ticks per line
    click(w, "seqReso");
    pickItem(w, 12);  // Speed: 6 ticks...
    a = topAlert(w);
    REQUIRE(a);
    checkPrompt(*a);
    typeText(w, "3");
    key(w, Key::Enter);
    CHECK(h.state.global.speed == 3);
}
