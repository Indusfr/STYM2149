// STYM2149 — gemui tests: main window controls: track tabs, All on / All off, ADSR values typed in,
// envelope frequency and period typed in and the frequency beside EP, keyboard destination
// menu, drum pads, mouse glissando, the Layer button and the ADSR frame inert on Sync
// Buzzer and on the drums, and the Source menu with the envelope entries.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Prototype: the #chtabs click handlers and setAllEnabled(), makeEditable() with the handlers of atkv, dcyv, susv,
// relv, efv and epv, chanMenu() and the contextmenu handler of #kbd and #pads, buildPads(), the pointerdown and
// pointerover handlers of #kbd, the click handler of #kbLayer, refreshAdsrEnable().
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "UiHost.h"
#include "gemui/Desktop.h"
#include "gemui/Font.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "pluginui/Widgets.h"
#include "ymseq/Pattern.h"

using namespace stym;
using namespace stym::ui_testing;
using plugin::Global;
using plugin::Track;
using ymengine::Generator;

namespace {

void mouseDown(pluginui::MainWindow& w, gemui::Point p) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
}

void mouseUp(pluginui::MainWindow& w, gemui::Point p) { w.desktop().mouseUp(p, {}); }

// Point on white key `index` of the main keyboard (28 white keys from C of the keyboard octave), below the black
// keys.
gemui::Point whiteKey(pluginui::MainWindow& w, int index) {
    const auto r = w.rectOf("kbd");
    REQUIRE(r);
    return {r->x + int((index + 0.5) * r->w / 28.0), r->bottom() - 6};
}

const gemui::ValueField& field(pluginui::MainWindow& w, std::string_view id) {
    const auto* f = dynamic_cast<const gemui::ValueField*>(w.widgetOf(id));
    REQUIRE(f);
    return *f;
}

// Clicks a value field, empties it and types `text` (the field stays open).
void type(pluginui::MainWindow& w, const std::string& id, std::string_view text) {
    click(w, id);
    REQUIRE(field(w, id).editing());
    for (int i = 0; i < 8; ++i) key(w, gemui::Key::Backspace);
    for (char c : text) w.desktop().keyDown(charKey(char32_t(c)));
}

// Types `text` in a value field and commits it with Enter.
void enter(pluginui::MainWindow& w, const std::string& id, std::string_view text) {
    type(w, id, text);
    key(w, gemui::Key::Enter);
    REQUIRE_FALSE(field(w, id).editing());
}

std::string epText(int ep) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%d ($%04X)", ep, unsigned(ep));
    return buf;
}

std::string hzText(double hz) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%.1f", hz);
    return buf;
}

// Darkest channel of the painted desktop in r (0: black ink; 153: black under the 0.4 veil of a greyed-out frame).
int darkest(pluginui::MainWindow& w, const gemui::Rect& r) {
    w.tick(0);
    w.desktop().paint();
    const gemui::Surface& s = w.desktop().surface();
    int dark = 255;
    for (int y = r.y; y < r.bottom(); ++y)
        for (int x = r.x; x < r.right(); ++x) {
            const uint32_t c = s.pixels()[size_t(y) * size_t(s.width()) + size_t(x)];
            dark = std::min({dark, int(c >> 16 & 0xFF), int(c >> 8 & 0xFF), int(c & 0xFF)});
        }
    return dark;
}

}  // namespace

TEST_CASE("Track tabs: a click focuses the track, a click on the focused tab turns it on or off",
          "[ui][tracks]") {
    UiHost h;
    pluginui::MainWindow w(h);
    REQUIRE(h.p(Global::FocusTrack) == 1.0f);
    REQUIRE(h.p(2, Track::On) == 0.0f);

    // First click: the track is edited, its state does not change.
    click(w, "tab2");
    CHECK(h.p(Global::FocusTrack) == 3.0f);
    CHECK(h.p(2, Track::On) == 0.0f);
    // Clicks on the focused tab: on, then off.
    click(w, "tab2");
    CHECK(h.p(2, Track::On) == 1.0f);
    click(w, "tab2");
    CHECK(h.p(2, Track::On) == 0.0f);
    CHECK(h.p(Global::FocusTrack) == 3.0f);

    // Back to track 1: focused first, it stays on; a second click turns it off.
    click(w, "tab0");
    CHECK(h.p(Global::FocusTrack) == 1.0f);
    CHECK(h.p(0, Track::On) == 1.0f);
    click(w, "tab0");
    CHECK(h.p(0, Track::On) == 0.0f);
    for (int t = 1; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == 0.0f);
}

TEST_CASE("All on / All off: every track of the mode, 4 in 3ch, 8 with two chips", "[ui][tracks]") {
    {
        UiHost h;
        pluginui::MainWindow w(h);
        click(w, "allOn");
        for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == 1.0f);
        click(w, "allOff");
        for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == 0.0f);
    }
    // 3ch: the four tracks of the chip; two chips: eight (the prototype stopped at four); 3ch on the STE: the
    // eight DMA tracks too. The tracks past the mode keep their state.
    struct Case {
        int voiceMode, computer, tracks;
    };
    for (const Case c : {Case{1, 0, 4}, Case{2, 0, 8}, Case{1, 1, 12}}) {
        INFO("voice mode " << c.voiceMode << ", computer " << c.computer);
        UiHost h;
        h.p(Global::VoiceMode) = float(c.voiceMode);
        h.p(Global::Computer) = float(c.computer);
        h.p(14, Track::On) = 1;
        pluginui::MainWindow w(h);
        click(w, "allOn");
        for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == (t < c.tracks || t == 14 ? 1.0f : 0.0f));
        click(w, "allOff");
        for (int t = 0; t < plugin::kTracks; ++t) CHECK(h.p(t, Track::On) == (t == 14 ? 1.0f : 0.0f));
    }
}

TEST_CASE("ADSR values typed in: rounded and clamped in the unit of the mode", "[ui][adsr]") {
    UiHost h;
    pluginui::MainWindow w(h);

    // Smooth: Attack, Decay and Release 0 to 2000 ms, Sustain 0 to 100 %.
    enter(w, "atkv", "2500");
    CHECK(h.p(0, Track::Attack) == 2000.0f);
    CHECK(field(w, "atkv").text() == "2000");
    enter(w, "dcyv", "250");
    CHECK(h.p(0, Track::Decay) == 250.0f);
    CHECK(field(w, "dcyv").text() == "250");
    enter(w, "susv", "250");
    CHECK(h.p(0, Track::Sustain) == 100.0f);
    enter(w, "susv", "42");
    CHECK(h.p(0, Track::Sustain) == 42.0f);
    CHECK(field(w, "susv").text() == "42");
    enter(w, "relv", "2500");
    CHECK(h.p(0, Track::Release) == 2000.0f);
    const auto* rel = dynamic_cast<const gemui::Slider*>(w.widgetOf("rel"));
    REQUIRE(rel);
    CHECK(rel->value() == 2000);
    // The mask takes as many digits as the maximum has.
    enter(w, "dcyv", "98765");
    CHECK(h.p(0, Track::Decay) == 2000.0f);
    // Escape: nothing changes.
    type(w, "atkv", "300");
    key(w, gemui::Key::Escape);
    CHECK_FALSE(field(w, "atkv").editing());
    CHECK(h.p(0, Track::Attack) == 2000.0f);
    CHECK(field(w, "atkv").text() == "2000");

    // Chip: VBL ticks (0 to 100) and level (0 to 15), stored in ms and percent (20 ms per tick at 50 Hz).
    h.p(0, Track::AdsrMode) = 1;
    w.tick(0);
    enter(w, "dcyv", "10");
    CHECK(h.p(0, Track::Decay) == 200.0f);
    CHECK(field(w, "dcyv").text() == "10");
    enter(w, "susv", "20");
    CHECK(h.p(0, Track::Sustain) == 100.0f);
    CHECK(field(w, "susv").text() == "15");
    enter(w, "susv", "6");
    CHECK(h.p(0, Track::Sustain) == 40.0f);
    CHECK(field(w, "susv").text() == "6");
    enter(w, "relv", "64");
    CHECK(h.p(0, Track::Release) == 1280.0f);
    CHECK(field(w, "relv").text() == "64");
    // A, D and R go to the end of their sliders, 100 ticks at 50 Hz: the 2000 ms of Smooth (Indus, 18:30; A
    // and D stopped at 50 ticks, then 64; R at 64 ticks like the prototype's).
    const auto* atk = dynamic_cast<const gemui::Slider*>(w.widgetOf("atk"));
    const auto* dcy = dynamic_cast<const gemui::Slider*>(w.widgetOf("dcy"));
    REQUIRE(atk);
    REQUIRE(dcy);
    for (const auto& [id, param, slider] : {std::tuple{"atkv", Track::Attack, atk},
                                            std::tuple{"dcyv", Track::Decay, dcy},
                                            std::tuple{"relv", Track::Release, rel}}) {
        CAPTURE(id);
        enter(w, id, "999");
        CHECK(h.p(0, param) == 2000.0f);
        CHECK(field(w, id).text() == "100");
        CHECK(slider->max() == 100);
        CHECK(slider->value() == slider->max());
    }
    // Back in Smooth, the same end: 2000 ms.
    h.p(0, Track::AdsrMode) = 0;
    w.tick(0);
    CHECK(atk->max() == 2000);
    CHECK(atk->value() == 2000);
    CHECK(field(w, "atkv").text() == "2000");
}

TEST_CASE("Envelope: EP typed in decimal or $hex, frequency typed in Hz, clamped to 1..65535",
          "[ui][env]") {
    UiHost h;
    pluginui::MainWindow w(h);
    const double clk = plugin::chipClock(h.values).hz();
    REQUIRE(field(w, "epv").text() == epText(12800));

    // EP: decimal or $hex, clamped; the frequency follows.
    enter(w, "epv", "$1A2B");
    CHECK(h.p(0, Track::EnvPeriod) == 6699.0f);
    CHECK(field(w, "epv").text() == "6699 ($1A2B)");
    CHECK(field(w, "efv").text() == hzText(clk / (256.0 * 6699)));
    enter(w, "epv", "70000");
    CHECK(h.p(0, Track::EnvPeriod) == 65535.0f);
    CHECK(field(w, "epv").text() == "65535 ($FFFF)");
    enter(w, "epv", "0");
    CHECK(h.p(0, Track::EnvPeriod) == 1.0f);
    enter(w, "epv", "-5");
    CHECK(h.p(0, Track::EnvPeriod) == 1.0f);
    enter(w, "epv", "300");
    CHECK(h.p(0, Track::EnvPeriod) == 300.0f);
    // Not a number: refused, the field shows the period again.
    for (std::string_view bad : {"abc", "$", "$zz"}) {
        INFO(bad);
        enter(w, "epv", bad);
        CHECK(h.p(0, Track::EnvPeriod) == 300.0f);
        CHECK(field(w, "epv").text() == epText(300));
    }

    // Frequency: EP = round(clock / (256 × f)), clamped; zero refused.
    enter(w, "efv", "100");
    CHECK(h.p(0, Track::EnvPeriod) == float(std::round(clk / (256.0 * 100))));
    enter(w, "efv", "12.5");
    const float ep = float(std::round(clk / (256.0 * 12.5)));
    CHECK(h.p(0, Track::EnvPeriod) == ep);
    CHECK(field(w, "epv").text() == epText(int(ep)));
    enter(w, "efv", "0");
    CHECK(h.p(0, Track::EnvPeriod) == ep);
    CHECK(field(w, "efv").text() == hzText(clk / (256.0 * ep)));
    enter(w, "efv", "0.1");
    CHECK(h.p(0, Track::EnvPeriod) == 65535.0f);
}

TEST_CASE("Envelope: the frequency value on the row of EP =, the slider over the whole row", "[ui][env]") {
    UiHost h;
    pluginui::MainWindow w(h);
    auto check = [&w] {
        const auto ef = w.rectOf("ef"), efv = w.rectOf("efv"), epv = w.rectOf("epv");
        REQUIRE(ef);
        REQUIRE(efv);
        REQUIRE(epv);
        CHECK(efv->y == epv->y);
        CHECK(efv->x > epv->right());
        // The value, then "Hz" (16 px), end where the slider ends; the slider runs past the start of the value.
        CHECK(std::abs(efv->right() + 16 - ef->right()) <= 1);
        CHECK(ef->right() > efv->x + 32);
    };
    INFO("12800");
    check();
    // The longest period with the shortest value, then the reverse: both fit side by side.
    enter(w, "epv", "65535");
    INFO("65535: " << field(w, "efv").text());
    check();
    enter(w, "epv", "1");
    REQUIRE(field(w, "efv").text() == hzText(plugin::chipClock(h.values).hz() / 256.0));
    INFO("1: " << field(w, "efv").text());
    check();
}

TEST_CASE("Keyboard destination: the tracks in a menu under the key or pad right-clicked", "[ui][kbd][tracks]") {
    UiHost h;
    h.p(2, Track::On) = 1;
    h.p(2, Track::Generator) = float(int(Generator::Sid));
    h.state.trackNames[3] = "Bass";
    pluginui::MainWindow w(h);
    h.take();

    // Third white key: the menu takes the left side of the key; too tall for the room below the keyboard, it opens
    // above it (openGemMenu). Positions measured in the prototype at the same zoom.
    press(w, whiteKey(w, 2), gemui::MouseButton::Right);
    const gemui::MenuList* m = topMenu(w);
    REQUIRE(m);
    CHECK(m->box().x == 267.4375);  // 200 + 2 × 33.71875
    CHECK(m->box().y == 392);       // 716 − 326 + 2: its frame on the key's top rule
    CHECK(m->box().w == 207);
    CHECK(m->box().h == 326);
    CHECK(h.take().empty());
    const std::vector<std::string> labels = menuLabels(w);
    REQUIRE(labels.size() == 16);
    CHECK(labels[0] == "Channel 1 [Chip Voice]");
    CHECK(labels[1] == "Channel 2 [Chip Voice]");
    CHECK(labels[2] == "Channel 3 [SID Voice]");
    CHECK(labels[3] == "Bass [Chip Voice]");
    CHECK(m->checked() == 0);
    CHECK_FALSE(m->items()[0].disabled);
    CHECK(m->items()[1].disabled);  // the tracks that are off are greyed
    CHECK_FALSE(m->items()[2].disabled);
    // A disabled track cannot be picked; an enabled one becomes the track played (and edited).
    pickItem(w, 1);
    CHECK(h.p(Global::FocusTrack) == 1.0f);
    if (topMenu(w)) key(w, gemui::Key::Escape);
    press(w, whiteKey(w, 2), gemui::MouseButton::Right);
    pickItem(w, 2);
    CHECK(h.p(Global::FocusTrack) == 3.0f);
    CHECK_FALSE(topMenu(w));

    // Second black key (D#2): its own left side.
    const auto kb = w.rectOf("kbd");
    REQUIRE(kb);
    press(w, {kb->x + 66, kb->y + 5}, gemui::MouseButton::Right);
    REQUIRE(topMenu(w));
    CHECK(topMenu(w)->box().x == 256.625);
    CHECK(topMenu(w)->box().y == 392);
    CHECK(topMenu(w)->checked() == 2);
    key(w, gemui::Key::Escape);
    CHECK(h.take().empty());

    // 3ch: the voices of the chip.
    UiHost h3;
    h3.p(Global::VoiceMode) = 1;
    pluginui::MainWindow w3(h3);
    press(w3, whiteKey(w3, 2), gemui::MouseButton::Right);
    CHECK(menuLabels(w3) ==
          std::vector<std::string>{"Voice A [Chip Voice]", "Voice B [Chip Voice]", "Voice C [Chip Voice]",
                                   "Digi [Digidrum]"});
    key(w3, gemui::Key::Escape);
    // On the STE, the DMA tracks follow, named as their tabs.
    h3.p(Global::Computer) = 1;
    w3.tick(0);
    press(w3, whiteKey(w3, 2), gemui::MouseButton::Right);
    const std::vector<std::string> ste = menuLabels(w3);
    REQUIRE(ste.size() == 12);
    CHECK(ste[3] == "Digi [Digidrum]");
    CHECK(ste[4] == "DMA1 [DMA]");
    CHECK(ste[11] == "DMA8 [DMA]");
    key(w3, gemui::Key::Escape);

    // Pads: under the pad, or under the row of pads between two of them.
    UiHost hp;
    hp.p(0, Track::Generator) = float(int(Generator::Chipdrum));
    pluginui::MainWindow wp(hp);
    press(wp, centre(wp.pads().padRect(2)), gemui::MouseButton::Right);
    REQUIRE(topMenu(wp));
    CHECK(topMenu(wp)->box().x == 437);
    CHECK(topMenu(wp)->box().y == 390);  // 714 − 326 + 2
    CHECK(menuLabels(wp)[0] == "Channel 1 [Chipdrum]");
    key(wp, gemui::Key::Escape);
    const gemui::Rect pad0 = wp.pads().padRect(0);
    press(wp, {pad0.right() + 1, pad0.y + 10}, gemui::MouseButton::Right);
    REQUIRE(topMenu(wp));
    CHECK(topMenu(wp)->box().x == 200);
    CHECK(topMenu(wp)->box().y == 390);
    CHECK(hp.take().empty());
}

TEST_CASE("Drum pads: Chipdrum names, note 48 + pad at velocity 118, released on leaving; Digidrum slots",
          "[ui][pads]") {
    UiHost h;
    h.p(0, Track::Generator) = float(int(Generator::Chipdrum));
    pluginui::MainWindow w(h);
    REQUIRE(w.rectOf("pads"));
    CHECK_FALSE(w.rectOf("kbd"));
    const pluginui::Pads& pads = w.pads();
    const std::vector<std::string> names = {"Kick", "Snare", "Clap", "Hat", "Open Hat", "Closed Hat", "Tom 1", "Tom 2"};
    for (int i = 0; i < 8; ++i) {
        CHECK(pads.label(i) == names[size_t(i)]);
        CHECK_FALSE(pads.empty(i));
    }
    h.take();

    // Held while pressed.
    const gemui::Point snare = centre(pads.padRect(1));
    mouseDown(w, snare);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 49, 118}});
    CHECK(pads.held(1));
    mouseUp(w, snare);
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 49, 0}});
    CHECK_FALSE(pads.held(1));
    // Leaving the pad with the button held releases it (pointerleave); the next pad is not played.
    mouseDown(w, centre(pads.padRect(2)));
    w.desktop().mouseDrag(centre(pads.padRect(3)), {});
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 50, 118}, {kNoteOff, 50, 0}});
    CHECK_FALSE(pads.held(2));
    CHECK_FALSE(pads.held(3));
    mouseUp(w, centre(pads.padRect(3)));
    CHECK(h.take().empty());
    CHECK(h.state.tracks[0].patch.digidrumSlot == 0);

    // Digidrum: "Slot n", the slots without a sample dimmed; the pad selects its slot and plays it.
    UiHost hd;
    hd.p(0, Track::Generator) = float(int(Generator::Digidrum));
    hd.state.digidrums[2] = {1, 2, 3};
    pluginui::MainWindow wd(hd);
    const pluginui::Pads& dp = wd.pads();
    for (int i = 0; i < 8; ++i) {
        CHECK(dp.label(i) == "Slot " + std::to_string(i + 1));
        CHECK(dp.empty(i) == (i != 2));
    }
    hd.take();
    press(wd, centre(dp.padRect(2)));
    CHECK(hd.state.tracks[0].patch.digidrumSlot == 2);
    CHECK(hd.take() == std::vector<Midi>{{kNoteOn, 50, 118}, {kNoteOff, 50, 0}});
    // An empty slot is selected too.
    press(wd, centre(dp.padRect(5)));
    CHECK(hd.state.tracks[0].patch.digidrumSlot == 5);
    CHECK(hd.take() == std::vector<Midi>{{kNoteOn, 53, 118}, {kNoteOff, 53, 0}});
}

TEST_CASE("Drum pads during playback: only the hits the engine plays light up (3 ch, monophonic)", "[ui][pads]") {
    // Chipdrum track, step 1: Kick on the root and Snare as a chord voice. In 3 channels, or on a monophonic track,
    // the grid greys the Snare out and the engine does not play it (polyTrigger): its pad stays dark.
    auto lit = [](int voiceMode, int voicing) {
        UiHost h;
        h.p(0, Track::Generator) = float(int(Generator::Chipdrum));
        h.p(Global::VoiceMode) = float(voiceMode);
        h.p(0, Track::Voicing) = float(voicing);
        h.editExtras([](plugin::NonParamState& s) {
            ymseq::Lane& l = s.seq.cur().edit(0);
            l.pat[0] = 0;
            l.vpat[0][0] = 1;
        });
        pluginui::MainWindow w(h);
        REQUIRE(w.rectOf("pads"));
        ymengine::Engine::SeqStatus st;
        st.playing = true;
        st.step = 0;
        st.stepAbs = 0;
        h.publish(st);
        w.tick(0);
        return std::pair{w.pads().held(0), w.pads().held(1)};
    };
    CHECK(lit(0, 0) == std::pair{true, true});   // 16 ch, polyphonic: both hits
    CHECK(lit(0, 1) == std::pair{true, false});  // monophonic
    CHECK(lit(1, 0) == std::pair{true, false});  // Authentic 3ch
    CHECK(lit(2, 0) == std::pair{true, false});  // Dual chip 2x3ch
}

TEST_CASE("Mouse glissando: with the button held the note follows the pointer, released at the end",
          "[ui][kbd]") {
    UiHost h;
    pluginui::MainWindow w(h);
    const gemui::Point c2 = whiteKey(w, 0), d2 = whiteKey(w, 1), e2 = whiteKey(w, 2);
    const auto kb = w.rectOf("kbd");
    REQUIRE(kb);
    h.take();

    mouseDown(w, c2);
    w.desktop().mouseDrag({c2.x + 3, c2.y}, {});  // same key: nothing
    w.desktop().mouseDrag(d2, {});
    w.desktop().mouseDrag(e2, {});
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}, {kNoteOff, 36, 0}, {kNoteOn, 38, 127},
                                        {kNoteOff, 38, 0},  {kNoteOn, 40, 127}});
    // Onto a black key (D#2), then off the keyboard: the last note holds until the button is released.
    w.desktop().mouseDrag({kb->x + 66, kb->y + 5}, {});
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 40, 0}, {kNoteOn, 39, 127}});
    w.desktop().mouseDrag({kb->x + 66, kb->y - 20}, {});
    CHECK(h.take().empty());
    mouseUp(w, {kb->x + 66, kb->y - 20});
    CHECK(h.take() == std::vector<Midi>{{kNoteOff, 39, 0}});
}

TEST_CASE("Layer: the button switches MIDI routing between Omni and Layer and silences the notes",
          "[ui][kbd][layer]") {
    UiHost h;
    pluginui::MainWindow w(h);
    const auto* layer = dynamic_cast<const gemui::Button*>(w.widgetOf("kbLayer"));
    REQUIRE(layer);
    REQUIRE(h.p(Global::MidiRouting) == float(plugin::MidiRouting::Omni));
    CHECK_FALSE(layer->on());

    // A latched note, then Layer: the routing changes and every note stops (kbmode then panic in the prototype).
    click(w, "kbLatch");
    const gemui::Point c2 = whiteKey(w, 0);
    mouseDown(w, c2);
    mouseUp(w, c2);
    h.take();
    click(w, "kbLayer");
    w.tick(0);
    CHECK(h.p(Global::MidiRouting) == float(plugin::MidiRouting::Layer));
    CHECK(layer->on());
    CHECK(h.take() == std::vector<Midi>{{0xB0, 120, 0}});
    // The latched note was dropped with the others: the same key plays it again instead of releasing it.
    mouseDown(w, c2);
    mouseUp(w, c2);
    CHECK(h.take() == std::vector<Midi>{{kNoteOn, 36, 127}});

    // Second click: back to Omni.
    click(w, "kbLayer");
    w.tick(0);
    CHECK(h.p(Global::MidiRouting) == float(plugin::MidiRouting::Omni));
    CHECK_FALSE(layer->on());
    CHECK(h.take() == std::vector<Midi>{{0xB0, 120, 0}});

    // Per-channel routing set by the host: the button shows off and a click turns Layer on.
    h.p(Global::MidiRouting) = float(plugin::MidiRouting::PerChannel);
    w.tick(0);
    CHECK_FALSE(layer->on());
    click(w, "kbLayer");
    w.tick(0);
    CHECK(h.p(Global::MidiRouting) == float(plugin::MidiRouting::Layer));
    CHECK(layer->on());
}

TEST_CASE("ADSR on Sync Buzzer in Chip mode: the frame is inert and the mouse goes through it", "[ui][adsr]") {
    UiHost h;
    h.p(0, Track::Generator) = float(Generator::SyncBuzzer);
    h.p(0, Track::AdsrMode) = 1;
    pluginui::MainWindow w(h);
    w.tick(0);
    const float attack = h.p(0, Track::Attack);
    const auto* mode = w.widgetOf("adsrMode");
    REQUIRE(mode);
    CHECK(mode->tip().find("Sync Buzzer takes its level from the hardware envelope") == 0);

    // Sliders, value fields and the mode button take no click (pointer-events: none in the prototype).
    click(w, "atk");
    CHECK(h.p(0, Track::Attack) == attack);
    click(w, "atkv");
    CHECK_FALSE(field(w, "atkv").editing());
    click(w, "adsrMode");
    CHECK(h.p(0, Track::AdsrMode) == 1.0f);

    // Smooth mode: the ADSR shapes the buzzer's level again, the frame takes clicks.
    h.p(0, Track::AdsrMode) = 0;
    w.tick(0);
    CHECK(mode->tip().find("Envelope shape") == 0);
    click(w, "atk");
    CHECK(h.p(0, Track::Attack) != attack);
    click(w, "atkv");
    CHECK(field(w, "atkv").editing());
    key(w, gemui::Key::Escape);

    // Chip mode on another generator: not inert either.
    h.p(0, Track::Generator) = float(Generator::Chip);
    h.p(0, Track::AdsrMode) = 1;
    w.tick(0);
    click(w, "atkv");
    CHECK(field(w, "atkv").editing());
}

TEST_CASE("ADSR on a drum track: the frame is inert in every voice mode", "[ui][adsr]") {
    // In 3 channels the Digidrum is the fourth track (its fixed role); the other cases use the first track.
    struct Case {
        int mode;
        Generator gen;
        int track;
    };
    for (const Case c : {Case{0, Generator::Chipdrum, 0}, Case{0, Generator::Digidrum, 0},
                         Case{1, Generator::Chipdrum, 0}, Case{1, Generator::Digidrum, 3},
                         Case{2, Generator::Chipdrum, 4}}) {
        CAPTURE(c.mode, int(c.gen));
        UiHost h;
        h.p(Global::VoiceMode) = float(c.mode);
        h.p(Global::FocusTrack) = float(c.track + 1);
        h.p(c.track, Track::Generator) = float(c.gen);
        pluginui::MainWindow w(h);
        w.tick(0);
        const auto* mode = w.widgetOf("adsrMode");
        REQUIRE(mode);
        CHECK(mode->tip().find("Percussion tracks do not use the ADSR") == 0);
        // Sliders and Mode button greyed out; the frame takes no click (value fields included).
        CHECK_FALSE(mode->enabled());
        REQUIRE(w.widgetOf("atk"));
        CHECK_FALSE(w.widgetOf("atk")->enabled());
        const float attack = h.p(c.track, Track::Attack);
        click(w, "atk");
        CHECK(h.p(c.track, Track::Attack) == attack);
        click(w, "atkv");
        CHECK_FALSE(field(w, "atkv").editing());

        // A Chip Voice track: the frame takes clicks again.
        h.p(0, Track::Generator) = float(Generator::Chip);
        h.p(Global::FocusTrack) = 1;
        w.tick(0);
        CHECK(mode->tip().find("Percussion") == std::string::npos);
        CHECK(w.widgetOf("atk")->enabled());
        click(w, "atkv");
        CHECK(field(w, "atkv").editing());
        key(w, gemui::Key::Escape);
    }
}

TEST_CASE("ADSR not applicable: the ADSR and ADSR ENVELOPE frames greyed out with their titles (third DAW test, "
          "item 21)",
          "[ui][adsr]") {
    UiHost h;
    h.p(0, Track::Generator) = float(Generator::Chipdrum);
    pluginui::MainWindow w(h);
    w.tick(0);
    const auto adsr = w.rectOf("adsrFrame"), curve = w.rectOf("curveFrame");
    REQUIRE(adsr);
    REQUIRE(curve);
    // Drum track: no black left in either frame, frame lines and titles included.
    CHECK(darkest(w, *adsr) >= 150);
    CHECK(darkest(w, *curve) >= 150);
    CHECK(w.rectOf("adsrInert") == adsr);
    CHECK(w.rectOf("curveInert") == curve);

    // Chip Voice: black ink again.
    h.p(0, Track::Generator) = float(Generator::Chip);
    CHECK(darkest(w, *adsr) == 0);
    CHECK(darkest(w, *curve) == 0);
    CHECK_FALSE(w.rectOf("adsrInert"));
    CHECK_FALSE(w.rectOf("curveInert"));

    // Sync Buzzer in Chip mode: both frames (the prototype greyed out only the ADSR frame); the hint stays in its
    // frame.
    h.p(0, Track::Generator) = float(Generator::SyncBuzzer);
    h.p(0, Track::AdsrMode) = 1;
    CHECK(darkest(w, *adsr) >= 150);
    CHECK(darkest(w, *curve) >= 150);
    const auto hint = w.rectOf("adsrHint");
    REQUIRE(hint);
    CHECK(hint->right() <= adsr->right());
    CHECK_FALSE(w.rectOf("adsrInert"));  // as the prototype
    CHECK(w.rectOf("curveInert") == curve);
}

TEST_CASE("Shape R13 menu: shapes 8 to F, a value below 8 marks the shape it plays as (third DAW test, item 22)",
          "[ui][envelope]") {
    UiHost h;
    h.p(0, Track::VolMode) = 1;  // envelope
    pluginui::MainWindow w(h);
    w.tick(0);
    click(w, "shapeSel");
    CHECK(menuLabels(w) == std::vector<std::string>{"8", "9", "A", "B", "C", "D", "E", "F"});
    pickItem(w, 2);
    CHECK(h.p(0, Track::EnvShape) == 10.0f);
    // 0 to 3 play as 9, 4 to 7 as F (YM import, MIDI CC): the value stays, the menu marks the shape heard.
    using Case = std::pair<int, int>;  // value, row marked
    for (const auto& [value, mark] : std::initializer_list<Case>{{0, 1}, {3, 1}, {4, 7}, {7, 7}, {13, 5}}) {
        CAPTURE(value);
        h.p(0, Track::EnvShape) = float(value);
        w.tick(0);
        click(w, "shapeSel");
        REQUIRE(topMenu(w));
        CHECK(topMenu(w)->checked() == mark);
        w.desktop().closePopups();
        CHECK(h.p(0, Track::EnvShape) == float(value));
    }
}

TEST_CASE("Source menu: the envelope entries set Mode too, one undo step per entry; Mode follows (fifth DAW test, "
          "item 48)",
          "[ui][source]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.tick(0);
    auto text = [&w](std::string_view id) {
        const auto* b = dynamic_cast<const gemui::Button*>(w.widgetOf(id));
        REQUIRE(b);
        return b->text();
    };
    click(w, "srcSel");
    const std::vector<std::string> names = {"Tone",     "Noise",     "Tone+Noise",    "Env",
                                            "Tone+Env", "Noise+Env", "Tone+Noise+Env"};
    CHECK(menuLabels(w) == names);
    REQUIRE(topMenu(w));
    CHECK(topMenu(w)->checked() == 0);
    w.desktop().closePopups();
    struct Want {
        float tone, noise, env;
    };
    const Want want[] = {{1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1}};
    for (int i = 6; i >= 0; --i) {
        CAPTURE(i);
        const size_t steps = h.editor.undo.undoSize();
        click(w, "srcSel");
        pickItem(w, i);
        w.tick(0);
        CHECK(h.p(0, Track::Tone) == want[i].tone);
        CHECK(h.p(0, Track::Noise) == want[i].noise);
        CHECK(h.p(0, Track::VolMode) == want[i].env);
        CHECK(h.editor.undo.undoSize() == steps + 1);
        CHECK(text("srcSel") == names[size_t(i)]);
        CHECK(text("vmSel") == (want[i].env != 0.0f ? "Envelope" : "Vol 4-bit"));
        click(w, "srcSel");
        REQUIRE(topMenu(w));
        CHECK(topMenu(w)->checked() == i);
        w.desktop().closePopups();
    }
    // One step undoes the three settings: Tone+Noise+Env back to Tone.
    click(w, "srcSel");
    pickItem(w, 6);
    w.tick(0);
    CHECK(ctrlKey(w, U'z'));
    w.tick(0);
    CHECK(h.p(0, Track::Tone) == 1.0f);
    CHECK(h.p(0, Track::Noise) == 0.0f);
    CHECK(h.p(0, Track::VolMode) == 0.0f);
    CHECK(text("srcSel") == "Tone");

    // Mode follows Source: Envelope on Noise gives Noise+Env; Vol 4-bit on Env alone turns the square on.
    click(w, "srcSel");
    pickItem(w, 1);
    click(w, "vmSel");
    pickItem(w, 1);
    w.tick(0);
    CHECK(text("srcSel") == "Noise+Env");
    click(w, "srcSel");
    pickItem(w, 3);
    click(w, "vmSel");
    pickItem(w, 0);
    w.tick(0);
    CHECK(h.p(0, Track::Tone) == 1.0f);
    CHECK(h.p(0, Track::VolMode) == 0.0f);
    CHECK(text("srcSel") == "Tone");

    // A state that the menu does not give (no square, no noise, fixed volume, from an automation): "Off".
    h.p(0, Track::Tone) = 0;
    h.p(0, Track::Noise) = 0;
    w.tick(0);
    CHECK(text("srcSel") == "Off");
    click(w, "srcSel");
    REQUIRE(topMenu(w));
    CHECK(topMenu(w)->checked() == gemui::kNoMarks);
    w.desktop().closePopups();
}

TEST_CASE("SOURCE: what the sound does not use is greyed out with its reason (sixth DAW test, items 55, 63 and 64)",
          "[ui][source]") {
    UiHost h;
    pluginui::MainWindow w(h);
    w.tick(0);
    auto on = [&w](std::string_view id) {
        const gemui::Widget* x = w.widgetOf(id);
        REQUIRE(x);
        return x->enabled();
    };
    auto tip = [&w](std::string_view id) { return w.widgetOf(id)->tip(); };
    // Chip Voice, Tone: no noise, so no noise period.
    CHECK(on("srcSel"));
    CHECK_FALSE(on("np"));
    CHECK(tip("np") == "Noise period: no noise in this channel's source");
    CHECK(on("vol"));
    CHECK(on("vmSel"));
    for (int src : {1, 2, 5, 6}) {  // Noise, Tone+Noise, Noise+Env, Tone+Noise+Env
        CAPTURE(src);
        click(w, "srcSel");
        pickItem(w, src);
        w.tick(0);
        CHECK(on("np"));
        CHECK(tip("np").find("Noise period: low is bright") == 0);
    }
    for (int src : {0, 3, 4}) {  // Tone, Env, Tone+Env
        click(w, "srcSel");
        pickItem(w, src);
        w.tick(0);
        CHECK_FALSE(on("np"));
    }

    // Sync Buzzer: the envelope alone, so no source, noise period, fixed volume or mode; Voicing and Level stay.
    h.p(0, Track::Noise) = 1;
    h.p(0, Track::Generator) = float(Generator::SyncBuzzer);
    w.tick(0);
    for (const char* id : {"srcSel", "np", "vol", "vmSel"}) {
        CAPTURE(id);
        CHECK_FALSE(on(id));
        CHECK(tip(id).find("Sync Buzzer plays the hardware envelope alone") == 0);
    }
    CHECK(on("polySel"));

    // SID Voice: square, noise and volume used, the envelope not (Mode, the envelope entries of Source, Shape R13).
    h.p(0, Track::Generator) = float(Generator::Sid);
    h.p(0, Track::VolMode) = 1;
    w.tick(0);
    CHECK(on("srcSel"));
    CHECK(on("np"));
    CHECK(on("vol"));
    CHECK_FALSE(on("vmSel"));
    CHECK(tip("vmSel") == "A SID Voice writes its own volumes: the envelope is not used");
    CHECK_FALSE(on("shapeSel"));
    click(w, "srcSel");
    REQUIRE(topMenu(w));
    const auto& items = topMenu(w)->items();
    REQUIRE(items.size() == 7);
    for (size_t i = 0; i < items.size(); ++i) CHECK(items[i].disabled == (i >= 3));
    w.desktop().closePopups();
}

TEST_CASE("SOURCE and ENVELOPE of a DMA or percussion track greyed out (thirteenth DAW test, items 169 and 170)",
          "[ui][source][env]") {
    UiHost h;
    h.p(Global::Computer) = 1;  // the STE, for the DMA type in 16 ch
    pluginui::MainWindow w(h);
    h.p(0, Track::Noise) = 1;
    h.p(0, Track::VolMode) = 1;
    w.tick(0);
    auto on = [&w](std::string_view id) {
        const gemui::Widget* x = w.widgetOf(id);
        REQUIRE(x);
        return x->enabled();
    };
    auto tip = [&w](std::string_view id) { return w.widgetOf(id)->tip(); };
    // Chip Voice with noise and the envelope: everything acts.
    for (const char* id : {"srcSel", "np", "vol", "vmSel", "ef", "shapeSel"}) CHECK(on(id));
    CHECK_FALSE(w.rectOf("envInert"));
    const struct {
        Generator gen;
        const char* why;
    } cases[] = {{Generator::Dma, "A DMA track plays its sample"},
                 {Generator::Digidrum, "Percussion tracks play their own sounds"},
                 {Generator::Chipdrum, "Percussion tracks play their own sounds"}};
    for (const auto& c : cases) {
        CAPTURE(int(c.gen));
        h.p(0, Track::Generator) = float(c.gen);
        w.tick(0);
        // The square, the noise, the fixed volume and Mode are not used; Voicing and Level still act.
        for (const char* id : {"srcSel", "np", "vol", "vmSel"}) {
            CAPTURE(id);
            CHECK_FALSE(on(id));
            CHECK(tip(id).find(c.why) == 0);
        }
        CHECK(on("polySel"));
        CHECK(on("chLevel"));
        // ENVELOPE dimmed with its title, the mouse going through it.
        const auto env = w.rectOf("envInert");
        REQUIRE(env);
        CHECK(*env == *w.rectOf("envFrame"));
        CHECK_FALSE(on("ef"));
        CHECK_FALSE(on("shapeSel"));
        click(w, "shapeSel");
        CHECK_FALSE(topMenu(w));
    }
    h.p(0, Track::Generator) = float(Generator::Chip);
    w.tick(0);
    for (const char* id : {"srcSel", "np", "vol", "vmSel", "ef", "shapeSel"}) CHECK(on(id));
    CHECK_FALSE(w.rectOf("envInert"));
}

TEST_CASE("Main window: moved by its title bar, kept on the desktop and in the session (third DAW test, item 25)",
          "[ui][windows]") {
    using pluginui::MainWindow;
    UiHost h;
    gemui::Point home;
    {
        MainWindow w(h);
        w.tick(0);
        home = w.mainPlace();
        const auto title = w.rectOf("mainTitle"), fx = w.rectOf("fxSel");
        REQUIRE(title);
        REQUIRE(fx);
        const gemui::Point grab{title->x + 300, title->y + 8};
        w.desktop().mouseMove(grab, {});
        w.desktop().mouseDown(grab, gemui::MouseButton::Left, {});
        w.desktop().mouseDrag({grab.x + 40, grab.y + 30}, {});
        w.desktop().mouseUp({grab.x + 40, grab.y + 30}, {});
        CHECK(w.mainPlace() == gemui::Point{home.x + 40, home.y + 30});
        CHECK(w.rectOf("fxSel") == fx->translated(40, 30));
        // Laid out again (another generator, a wider button): the controls stay where the window is, and answer there.
        h.p(0, Track::Generator) = float(Generator::Sid);
        w.tick(0);
        const auto moved = w.rectOf("fxSel");
        REQUIRE(moved);
        CHECK(moved->x == fx->x + 40);
        CHECK(moved->y == fx->y + 30);
        click(w, "fxSel");
        CHECK(topMenu(w));
        w.desktop().closePopups();
        // Kept on the desktop: its right and bottom edges on those of the desktop (Indus, 26/10/2026).
        w.moveMainTo(5000, 5000);
        const gemui::Rect f = w.mainFrame().bounds();
        CHECK(f.right() == MainWindow::kWidth);
        CHECK(f.bottom() == MainWindow::kHeight);
        w.moveMainTo(-50, -50);  // below the menu bar of the desktop (ninth DAW test, item 101)
        CHECK(w.mainPlace() == gemui::Point{0, MainWindow::kMenuH});
        w.moveMainTo(home.x + 10, home.y + 20);
    }
    // A new editor (the plugin keeps the session): the main window reopens at its place.
    MainWindow w(h);
    CHECK(w.mainPlace() == gemui::Point{home.x + 10, home.y + 20});
    CHECK(w.rectOf("fxSel"));
}

TEST_CASE("Ctrl+D: the DMA generator on the STE, the demo pattern left to the File menu", "[ui][kbd]") {
    // ST: no DMA; the key does nothing else (no demo pattern, no box).
    UiHost h;
    h.state.seq.cur().edit(0).pat[0] = 5;
    pluginui::MainWindow w(h);
    CHECK(ctrlKey(w, U'd'));
    CHECK(h.p(0, Track::Generator) != float(int(Generator::Dma)));
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.state.seq.cur().lane(1).pat[2] < 0);

    UiHost ste;
    ste.values[size_t(plugin::paramIndex(Global::Computer))] = 1;
    pluginui::MainWindow ws(ste);
    CHECK(ctrlKey(ws, U'd'));
    CHECK(ste.p(0, Track::Generator) == float(int(Generator::Dma)));
}

TEST_CASE("Typed and shown decimals do not depend on the C locale of the host", "[ui][env][locale]") {
    // A host may set LC_NUMERIC to a locale with a decimal comma: strtod then stopped at the '.' of a masked field
    // ("12.5" read as 12) and printf showed "12,5", so even a value committed unchanged moved.
    const std::string before = std::setlocale(LC_NUMERIC, nullptr);
    struct Restore {
        std::string name;
        ~Restore() { std::setlocale(LC_NUMERIC, name.c_str()); }
    } restore{before};
    bool comma = false;
    for (const char* name : {"fr_FR.UTF-8", "fr_FR.utf8", "de_DE.UTF-8", "de_DE.utf8", "fr_FR", "French_France.1252"})
        if (std::setlocale(LC_NUMERIC, name) && std::localeconv()->decimal_point[0] == ',') {
            comma = true;
            break;
        }
    if (!comma) SKIP("no locale with a decimal comma installed");

    CHECK(gemui::typedNumber("1,5") == 1.5);
    CHECK(gemui::typedNumber("1.5") == 1.5);
    CHECK(gemui::readDecimal("0.25") == 0.25);
    CHECK(gemui::formatDecimal("%.2f Hz", 0.25) == "0.25 Hz");
    UiHost h;
    pluginui::MainWindow w(h);
    const float ep = float(std::round(plugin::chipClock(h.values).hz() / (256.0 * 12.5)));
    enter(w, "efv", "12.5");  // masked field: its text is rebuilt with a '.'
    CHECK(h.p(0, Track::EnvPeriod) == ep);
    w.tick(0);
    CHECK(field(w, "efv").text() == "12.5");
    // The value shown, committed unchanged.
    click(w, "efv");
    key(w, gemui::Key::Enter);
    CHECK(h.p(0, Track::EnvPeriod) == ep);
}

TEST_CASE("ADSR curve: one point per note sounding (Indus, 24/10/2026 11:45)", "[ui][adsr]") {
    using pluginui::AdsrCurve;
    AdsrCurve c;
    c.setBox({0, 0, 300, 74});
    c.setShape({100, 200, 0.5, 1000});
    // Columns of the canvas where the green bar of a point stands.
    auto bars = [&] {
        gemui::Surface s(300, 74, gemui::colors::kWhite);
        gemui::Painter p(s);
        c.paint(p);
        std::vector<int> xs;
        bool prev = false;
        for (int x = 0; x < 300; ++x) {
            const bool green = s.at(x, 10) == gemui::colors::kGreen;
            if (green && !prev) xs.push_back(x);
            prev = green;
        }
        return xs;
    };
    CHECK(bars().empty());
    // Three notes: one in its attack, one held on the sustain, one released; a point without a stage is left out.
    const std::vector<AdsrCurve::LivePoint> three = {{1, 0.5}, {3, 0.5}, {4, 0.25}, {-1, 0}};
    c.setLive(three);
    CHECK(c.live().size() == 3);
    CHECK(bars().size() == 3);
    // One note left: one point.
    const std::vector<AdsrCurve::LivePoint> one = {{3, 0.5}};
    c.setLive(one);
    CHECK(bars().size() == 1);
    c.setLive({});
    CHECK(bars().empty());
}

TEST_CASE("ADSR curve: a note released before its sustain goes on down the decay line (Indus, 24/10/2026 09:11)",
          "[ui][adsr]") {
    using pluginui::AdsrCurve;
    constexpr double hold = AdsrCurve::kHoldMs;
    // A = 153 ms, D = 1000 ms, S = 0 %, R = 1000 ms: released at the start of the decay, the level falls from near 1.
    AdsrCurve::Shape s{153, 1000, 0, 1000};
    const double decayAt09 = 153 + 0.1 * 1000;
    CHECK(std::abs(AdsrCurve::liveTime(s, 2, 0.9) - decayAt09) < 1e-9);
    CHECK(std::abs(AdsrCurve::liveTime(s, 4, 0.9) - decayAt09) < 1e-9);  // on the decay line, not at the R segment
    CHECK(AdsrCurve::liveTime(s, 4, 0.9) < 153 + 1000);
    CHECK(std::abs(AdsrCurve::liveTime(s, 4, 0) - (153 + 1000)) < 1e-9);  // the end of the decay line
    // S = 50 %: above the sustain level the decay line, at and under it the release line.
    s.sustain = 0.5;
    CHECK(std::abs(AdsrCurve::liveTime(s, 4, 0.8) - (153 + 0.2 / 0.5 * 1000)) < 1e-9);
    CHECK(std::abs(AdsrCurve::liveTime(s, 4, 0.5) - (153 + 1000 + hold)) < 1e-9);
    CHECK(std::abs(AdsrCurve::liveTime(s, 4, 0.25) - (153 + 1000 + hold + 500)) < 1e-9);
    // A release from the sustain, and one from the attack under it, stay on the release line.
    s.sustain = 1;
    CHECK(std::abs(AdsrCurve::liveTime(s, 4, 0.6) - (153 + 1000 + hold + 400)) < 1e-9);
    CHECK(AdsrCurve::liveTime(s, 0, 1) < 0);
}

TEST_CASE("ADSR curve: with S = 0 %, a released note ends at the end of the decay line (Indus, 24/10/2026 14:58)",
          "[ui][adsr]") {
    using pluginui::AdsrCurve;
    // Indus's settings: A = 1000 ms, D = 1000 ms, S = 0 %, R = 2000 ms. A note released in its attack or its decay
    // falls from its level to 0 in R; its point goes down the decay line and never reaches the release segment, not
    // even in its last milliseconds, where it used to show at the end of the release.
    const AdsrCurve::Shape s{1000, 1000, 0, 2000};
    for (const double from : {1.0, 0.5, 0.1}) {
        double last = -1;
        for (int k = 0; k <= 1000; ++k) {
            const double lv = from * (1000 - k) / 1000.0, t = AdsrCurve::liveTime(s, 4, lv);
            CHECK(t >= last);  // the point only goes forward
            CHECK(t <= 1000 + 1000);  // never past the decay
            last = t;
        }
        CHECK(std::abs(last - 2000) < 1e-9);
    }
    // In the sustain at level 0, released or not: the decay's end or the middle of the plateau, not the release's end.
    CHECK(std::abs(AdsrCurve::liveTime(s, 3, 0) - (2000 + AdsrCurve::kHoldMs / 2)) < 1e-9);
    CHECK(AdsrCurve::liveTime(s, 4, 0) < 2000 + AdsrCurve::kHoldMs);
}

TEST_CASE("Sample drawings: every column drawn and joined to the next one where the wave jumps (Indus, 26/10/2026)",
          "[ui][digidrum][dma]") {
    // A square wave: 37 samples high, 37 low. Each column of the inside of the box, below the line of text, must
    // hold black pixels touching those of the column before (the canvas of 360 columns stretched into the box left
    // gaps and dropped columns).
    std::vector<uint8_t> nibbles(3000);
    std::vector<int8_t> bytes(3000);
    for (size_t i = 0; i < nibbles.size(); ++i) {
        const bool high = (i / 37) % 2 == 0;
        nibbles[i] = high ? 15 : 0;
        bytes[i] = int8_t(high ? 100 : -100);
    }
    auto joined = [](gemui::Widget& wave, gemui::Box box, int designH) {
        wave.setBox(box);
        const gemui::Rect r = wave.bounds(), in = r.inset(2);
        gemui::Surface s(r.right() + 2, r.bottom() + 2, gemui::colors::kWhite);
        gemui::Painter p(s);
        wave.paint(p);
        // Rows below the baseline of the line of text (design row 10).
        const int top = in.y + int(std::lround(10.0 * in.h / designH)) + 1;
        int prevLo = -1, prevHi = -1, bad = 0;
        for (int x = in.x; x < in.right(); ++x) {
            int lo = -1, hi = -1;
            for (int y = top; y < in.bottom(); ++y)
                if (s.at(x, y) == gemui::colors::kBlack) {
                    if (lo < 0) lo = y;
                    hi = y;
                }
            if (lo < 0 || (prevLo >= 0 && (lo > prevHi + 1 || hi < prevLo - 1))) ++bad;
            prevLo = lo;
            prevHi = hi;
        }
        return bad;
    };
    pluginui::DrumWave drum;  // GENERATOR frame, Digidrum
    drum.setSample(0, nibbles);
    CHECK(joined(drum, {10, 10, 204.5, 48}, pluginui::DrumWave::kH) == 0);
    pluginui::DmaWave frame(false);  // GENERATOR frame, DMA
    frame.setSample(0, bytes, 25033, false);
    CHECK(joined(frame, {10, 10, 204.5, 48}, pluginui::DmaWave::kH) == 0);
    pluginui::DmaWave dma;  // DMA window
    dma.setSample(0, bytes, 25033, false);
    CHECK(joined(dma, {10, 10, 360, 72}, pluginui::DmaWave::kH) == 0);
    pluginui::DigiWave digi;  // Digidrums window
    digi.setSample(0, nibbles, 7800, false, 0, 3000);
    CHECK(joined(digi, {10, 10, 360, 72}, pluginui::DigiWave::kH) == 0);
}

TEST_CASE("The units of the typed values stay in normal video under the pointer (Indus, 26/10/2026)",
          "[ui][units]") {
    UiHost h;
    h.p(Global::DelayOn) = 1;
    h.p(Global::DelaySync) = 0;  // the time in ms
    h.state.tracks[0].patch.lfo.on = true;
    h.state.tracks[0].patch.lfo.target = ymengine::LfoTarget::Pitch;
    h.state.tracks[0].patch.envTuned = true;
    pluginui::MainWindow w(h);
    gemui::Desktop& d = w.desktop();
    // Pointer on the left of the field: the number reversed (black on the field's top row, above the glyphs), its
    // unit at the right edge left white.
    auto check = [&](const char* id, std::optional<gemui::Rect> r, const char* unit, int px = 14) {
        CAPTURE(id);
        REQUIRE(r);
        d.mouseMove({r->x + 3, r->y + r->h / 2}, {});
        d.paint();
        const int uw = int(std::ceil(gemui::Font::width(unit, px)));
        CHECK(d.surface().at(r->right() - 2, r->y) == gemui::colors::kWhite);
        CHECK(d.surface().at(r->right() - uw - 2, r->y) == gemui::colors::kBlack);
        d.mouseMove({1, 300}, {});
    };
    w.modulation().show();
    check("seqxDetuneV", w.rectOf("seqxDetuneV"), "c");
    check("seqxAnalogV", w.rectOf("seqxAnalogV"), "c");
    check("lfoRateV", w.rectOf("lfoRateV"), " Hz");
    check("lfoDepthV", w.rectOf("lfoDepthV"), "c");
    w.modulation().close();
    w.echoWindow().show();
    check("delayTimev", w.echoWindow().rectOf("delayTimev"), " ms", 16);
    check("delayFbv", w.echoWindow().rectOf("delayFbv"), " %", 16);
    check("delayCutv", w.echoWindow().rectOf("delayCutv"), " Hz", 16);
    check("delayLevelv", w.echoWindow().rectOf("delayLevelv"), " %", 16);
    w.echoWindow().close();
    w.pickGenerator(int(Generator::SyncBuzzer));
    check("genFinev", w.rectOf("genFinev"), "c");
}
