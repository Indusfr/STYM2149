// STYM2149 — MIDI learn (M6, A4, D-146): Options menu, banner, control armed by a click, learnt CCs and notes that
// drive sliders and buttons, shared map kept in the preferences.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using plugin::paramIndex;
using plugin::Track;

namespace {

class TestHost : public pluginui::Host {
public:
    bool audioSettings = false;  // the standalone application's audio and MIDI window
    int audioOpened = 0;
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    std::map<std::string, std::string> prefs;
    plugin::MidiKeys keys;
    bool armed = false;
    int captures = 0;
    std::deque<std::array<uint8_t, 3>> learned;
    std::vector<std::array<uint8_t, 3>> screen;

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
    void screenMidi(uint8_t s, uint8_t a, uint8_t b) override { screen.push_back({s, a, b}); }
    bool hostNote(std::array<uint8_t, 3>&) override { return false; }
    void setMidiCapture(const plugin::MidiKeys& k, bool a) override {
        keys = k;
        armed = a;
        ++captures;
    }
    bool learnMidi(std::array<uint8_t, 3>& m) override {
        if (learned.empty()) return false;
        m = learned.front();
        learned.pop_front();
        return true;
    }
    void seqCommand(SeqCommand, int, int, int) override {}
    const plugin::EngineStatus* engineStatus() override { return nullptr; }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return false; }
    std::string midiStatus() const override { return "host"; }
    std::string version() const override { return "test"; }
    void quit() override {}
    bool hasAudioSettings() const override { return audioSettings; }
    void openAudioSettings() override { ++audioOpened; }
    void loadDigidrum(int) override {}
    std::string preference(std::string_view key, std::string_view fallback) const override {
        const auto it = prefs.find(std::string(key));
        return it == prefs.end() ? std::string(fallback) : it->second;
    }
    void setPreference(std::string_view key, std::string_view value) override { prefs[std::string(key)] = value; }
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}

    float& p(int track, Track f) { return values[size_t(paramIndex(track, f))]; }
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

// MIDI message taken by the host for the interface, read at the next tick.
void send(TestHost& h, MainWindow& w, uint8_t status, uint8_t a, uint8_t b) {
    h.learned.push_back({status, a, b});
    w.tick(0);
}

// Entry of the Options menu: Audio/MIDI settings, ---, MIDI learn (1), Clear MIDI map (2), ---...
void options(MainWindow& w, int entry) {
    w.openMenu(3);
    const gemui::Rect bar = *w.rectOf("mOptions");
    const int seps = entry >= 1 ? 1 : 0;
    press(w, {bar.x + 20, bar.bottom() + 2 + 20 * entry + 8 * seps + 10});
    CHECK_FALSE(w.desktop().hasPopups());
}

}  // namespace

TEST_CASE("Options: one Audio/MIDI settings entry, greyed out in a host (second DAW test, item 16, D-217)",
          "[ui][menu]") {
    auto labels = [](MainWindow& w) {
        w.openMenu(3);
        const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
        REQUIRE(m);
        std::vector<gemui::MenuItem> items = m->items();
        w.desktop().closePopups();
        return items;
    };
    TestHost h;
    {
        MainWindow w(h);
        const std::vector<gemui::MenuItem> items = labels(w);
        REQUIRE(items.size() > 2);
        CHECK(items[0].label == "Audio/MIDI settings");
        CHECK(items[0].disabled);
        CHECK(items[0].why == "The host chooses the audio output and sends MIDI to the plugin");
        for (const gemui::MenuItem& m : items) {
            CHECK(m.label != "Audio output");
            CHECK(m.label != "MIDI input");
        }
    }
    h.audioSettings = true;  // standalone application
    MainWindow w(h);
    CHECK_FALSE(labels(w)[0].disabled);
    options(w, 0);
    CHECK(h.audioOpened == 1);
}

TEST_CASE("Options > Pixels: Smooth by default, Sharp stored as a preference (items 123 and 130, D-331, D-339)",
          "[ui][menu]") {
    TestHost h;
    MainWindow w(h);
    auto pixels = [&w]() -> gemui::MenuItem {
        w.openMenu(3);
        const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
        REQUIRE(m);
        const std::vector<gemui::MenuItem> items = m->items();
        w.desktop().closePopups();
        for (const gemui::MenuItem& it : items)
            if (it.label == "Pixels") return it;
        FAIL("no Pixels entry");
        return {};
    };
    gemui::MenuItem p = pixels();
    REQUIRE(p.sub.size() == 2);
    CHECK(p.sub[0].label == "Sharp");
    CHECK(p.sub[1].label == "Smooth");
    CHECK(p.subMarks);
    CHECK(p.subCur == 1);
    p.sub[0].pick();
    CHECK(h.prefs["pixels"] == "sharp");
    CHECK(pixels().subCur == 0);
    pixels().sub[1].pick();
    CHECK(h.prefs["pixels"] == "smooth");
    CHECK(pixels().subCur == 1);
}

TEST_CASE("Options > Oscilloscope: the main window's shown by default, hidden as a preference, what follows moves up "
          "(twelfth DAW test, item 149, D-358)",
          "[ui][menu]") {
    TestHost h;
    MainWindow w(h);
    auto scope = [&w]() -> gemui::MenuItem {
        w.openMenu(3);
        const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
        REQUIRE(m);
        const std::vector<gemui::MenuItem> items = m->items();
        w.desktop().closePopups();
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].label == "Oscilloscope") {
                CHECK(items[i - 1].label == "Pixels");
                return items[i];
            }
        FAIL("no Oscilloscope entry");
        return {};
    };
    gemui::MenuItem o = scope();
    REQUIRE(o.sub.size() == 2);
    CHECK(o.sub[0].label == "On");
    CHECK(o.sub[1].label == "Off");
    CHECK(o.subMarks);
    CHECK(o.subCur == 0);
    REQUIRE(w.rectOf("scope"));
    const gemui::Rect regs = *w.rectOf("regs"), kbd = *w.rectOf("kbd"), btn = *w.rectOf("kbdMapBtn");
    o.sub[1].pick();
    CHECK(h.prefs["mainScope"] == "0");
    CHECK(scope().subCur == 1);
    CHECK_FALSE(w.rectOf("scope"));
    CHECK(w.rectOf("regs")->y == regs.y - 100);
    CHECK(w.rectOf("kbd")->y == kbd.y - 100);
    CHECK(w.rectOf("kbdMapBtn")->y == btn.y - 100);
    // Kept for the next opening.
    {
        MainWindow w2(h);
        CHECK_FALSE(w2.rectOf("scope"));
        CHECK(w2.rectOf("regs")->y == regs.y - 100);
    }
    scope().sub[0].pick();
    CHECK(h.prefs["mainScope"] == "1");
    CHECK(w.rectOf("scope"));
    CHECK(*w.rectOf("regs") == regs);
}

TEST_CASE("MIDI learn: Options menu, banner, a click arms the control without acting", "[ui][learn]") {
    TestHost h;
    // Noise in the source, so that the noise period slider is in use (greyed out otherwise, D-253).
    h.values[size_t(plugin::paramIndex(0, Track::Noise))] = 1;
    MainWindow w(h);
    CHECK(h.captures == 1);  // map from the preferences (empty) given to the host
    CHECK(h.keys == plugin::MidiKeys{});
    CHECK_FALSE(h.armed);
    CHECK(w.learnBadge().empty());
    CHECK_FALSE(w.rectOf("learnBadge"));

    options(w, 1);
    REQUIRE(w.midiLearn());
    CHECK(w.learnBadge() == "MIDI learn: click a slider or button, then move a control...");
    CHECK(w.rectOf("learnBadge") == gemui::Rect{8, 908, 504, 28});  // box of the prototype at 100 %

    // Slider: armed, its value does not move.
    const float np = h.p(0, Track::NoisePeriod);
    click(w, "np");
    CHECK(h.p(0, Track::NoisePeriod) == np);
    CHECK(w.learnBadge() == "MIDI learn: [np] armed - move a knob or press a pad");
    CHECK(h.armed);
    // Button: armed, its action does not take place (the prototype let the click through).
    click(w, "openMixBtn");
    CHECK_FALSE(w.mixer().isOpen());
    CHECK(w.learnBadge() == "MIDI learn: [openMixBtn] armed - move a knob or press a pad");
    // Neither slider nor button (piano): the press acts as usual.
    click(w, "kbd");
    CHECK_FALSE(h.screen.empty());
    CHECK(w.learnBadge() == "MIDI learn: [openMixBtn] armed - move a knob or press a pad");

    // The banner stays in front of the windows opened afterwards (z-index 95) and takes the presses.
    w.sequencer().show();
    REQUIRE_FALSE(w.children().empty());
    CHECK(w.children().back()->bounds() == *w.rectOf("learnBadge"));
    w.sequencer().close();

    options(w, 1);  // Stop MIDI learn
    CHECK_FALSE(w.midiLearn());
    CHECK(w.learnBadge().empty());
    CHECK_FALSE(h.armed);
    click(w, "openMixBtn");
    CHECK(w.mixer().isOpen());
}

TEST_CASE("MIDI learn: learnt CCs and notes drive sliders and buttons; shared map", "[ui][learn]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, Track::Noise))] = 1;  // noise period slider in use (D-253)
    MainWindow w(h);
    w.setMidiLearn(true);

    // Slider learnt on CC 21: map written in the prototype's format, keys given back to the host, disarmed.
    click(w, "np");
    send(h, w, 0xB0, 21, 0);
    REQUIRE(w.midiMap().size() == 1);
    CHECK(h.prefs["midiMap"] == R"({"CC21":{"id":"np","kind":"range"}})");
    CHECK(w.learnBadge() == "MIDI learn: CC21 -> [np] - click another control or exit via Options");
    CHECK(h.keys.cc.test(21));
    CHECK_FALSE(h.armed);
    CHECK(h.p(0, Track::NoisePeriod) == 8.0f);  // the message that learns sets nothing

    // Button learnt on a note; the release that follows does nothing.
    click(w, "openMixBtn");
    send(h, w, 0x90, 36, 100);
    send(h, w, 0x80, 36, 0);
    CHECK_FALSE(w.mixer().isOpen());
    CHECK(h.keys.notes.test(36));

    // Volume of a mixer strip, learnt in the open window.
    w.mixer().show();
    click(w, "mixFad2");
    send(h, w, 0xB0, 7, 0);
    w.mixer().close();
    w.setMidiLearn(false);
    CHECK(h.prefs["midiMap"] == R"({"CC21":{"id":"np","kind":"range"},"Note36":{"id":"openMixBtn","kind":"btn"},)"
                                R"("CC7":{"id":"mixFad2","kind":"range"}})");

    // Slider: min + v / 127 × (max − min), on the step; one gesture per message.
    send(h, w, 0xB0, 21, 127);
    CHECK(h.p(0, Track::NoisePeriod) == 31.0f);
    send(h, w, 0xB0, 21, 0);
    CHECK(h.p(0, Track::NoisePeriod) == 1.0f);
    send(h, w, 0xB0, 21, 64);
    CHECK(h.p(0, Track::NoisePeriod) == 16.0f);
    // Window closed: the slider still exists and drives.
    send(h, w, 0xB0, 7, 0);
    CHECK(h.p(2, Track::Level) == 0.0f);
    send(h, w, 0xB0, 7, 127);
    CHECK(h.p(2, Track::Level) == 100.0f);

    // Button: clicked on the rising edge (≥ 64 after < 64), a played note is worth 127, its release 0.
    send(h, w, 0x90, 36, 100);
    CHECK(w.mixer().isOpen());
    w.mixer().close();
    send(h, w, 0x90, 36, 100);  // no release between the two: no edge
    CHECK_FALSE(w.mixer().isOpen());
    send(h, w, 0x80, 36, 0);
    send(h, w, 0x90, 36, 1);
    CHECK(w.mixer().isOpen());

    // Key taken for a control disarmed since: the host again receives only the learnt keys.
    h.keys.cc.set(50);
    send(h, w, 0xB0, 50, 3);
    CHECK_FALSE(h.keys.cc.test(50));
    CHECK(h.keys == plugin::midiKeys(w.midiMap()));

    // Shared map: another window reads it again from the preferences and gives it back to the host.
    h.keys = {};
    {
        MainWindow b(h);
        CHECK(b.midiMap() == w.midiMap());
        CHECK(h.keys.cc.test(21));
        CHECK(h.keys.cc.test(7));
        CHECK(h.keys.notes.test(36));
    }

    // Clear MIDI map: empty map written, the banner says so in learn mode.
    w.setMidiLearn(true);
    options(w, 2);
    CHECK(w.midiMap().empty());
    CHECK(h.prefs["midiMap"] == "{}");
    CHECK(w.learnBadge() == "MIDI learn: map cleared");
    CHECK(h.keys == plugin::MidiKeys{});
    w.setMidiLearn(false);
    send(h, w, 0xB0, 21, 127);
    CHECK(h.p(0, Track::NoisePeriod) == 16.0f);
}
