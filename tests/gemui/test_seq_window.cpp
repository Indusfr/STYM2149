// STYM2149 — Step Sequencer window: All button of the gate, Gate slider, Clock selector;
// Settings > Quantize to VBL; moved up when it grows past the bottom of the desktop.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <tuple>
#include <vector>

#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;

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
    const plugin::EngineStatus* engineStatus() override { return nullptr; }
    pluginui::EditorSession& session() override { return editor; }
    bool standalone() const override { return true; }
    std::string midiStatus() const override { return "none"; }
    std::string version() const override { return "test"; }
    void quit() override {}
    bool hasAudioSettings() const override { return false; }
    void openAudioSettings() override {}
    std::string preference(std::string_view, std::string_view fallback) const override { return std::string(fallback); }
    void setPreference(std::string_view, std::string_view) override {}
    std::string patchLibrary() const override { return patches; }
    void setPatchLibrary(const std::string& json) override { patches = json; }

    std::string patches;

    float v(const char* id) const { return values[size_t(plugin::findParam(id))]; }
};

void press(MainWindow& w, gemui::Point p) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, gemui::MouseButton::Left, {});
    w.desktop().mouseUp(p, {});
}

void click(MainWindow& w, const char* id) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, {r->x + r->w / 2, r->y + r->h / 2});
}

// Press at the left or right edge of the Gate track: the thumb goes there, minimum or maximum.
// Ends of a slider's track, inside its arrows.
int trackEnd(const gemui::Rect& r, bool max) {
    return max ? r.right() - 1 - gemui::Slider::kArrow : r.x + gemui::Slider::kArrow;
}

void gateTo(MainWindow& w, bool max) {
    const auto r = w.rectOf("gate");
    REQUIRE(r);
    press(w, {trackEnd(*r, max), r->y + r->h / 2});
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

// Picks entry `label` of the open menu.
void pick(MainWindow& w, const std::string& label) {
    const std::vector<std::string> l = labels(w);
    const auto at = std::find(l.begin(), l.end(), label);
    REQUIRE(at != l.end());
    const gemui::Rect r = menu(w).rowsFrom(int(at - l.begin()));
    REQUIRE(r.h > 0);
    press(w, {r.x + 20, r.y + 10});
}

}  // namespace

TEST_CASE("Step Sequencer: All button of the global gate, on the row of the Gate slider", "[ui][seq]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const auto all = w.rectOf("seqGateAll");
    const auto gate = w.rectOf("gate");
    const auto tempo = w.rectOf("bpm");
    REQUIRE(all);
    REQUIRE(gate);
    REQUIRE(tempo);
    // Between the Gate label and the track, without enlarging the row (16 px): the grid does not move.
    CHECK(all->h <= 16);
    CHECK(all->x + all->w <= gate->x);
    CHECK(all->x > tempo->x + tempo->w);
    CHECK(all->y >= tempo->y);
    CHECK(all->y + all->h <= tempo->y + 16);

    // Off: the slider sets the gate of the shown track.
    gateTo(w, true);
    CHECK(h.v("t01_gate") == 95.0f);
    CHECK(h.v("gate") == 50.0f);

    // On: the slider sets the global gate, that of the tracks stays.
    click(w, "seqGateAll");
    CHECK(h.v("gateAll") == 1.0f);
    w.tick(0);
    gateTo(w, false);
    CHECK(h.v("gate") == 5.0f);
    CHECK(h.v("t01_gate") == 95.0f);

    // Off again: the track takes over again.
    click(w, "seqGateAll");
    CHECK(h.v("gateAll") == 0.0f);
    w.tick(0);
    gateTo(w, false);
    CHECK(h.v("t01_gate") == 5.0f);
    CHECK(h.v("gate") == 5.0f);
}

TEST_CASE("Step Sequencer: Clock on MIDI ext, BPM slider greyed out", "[ui][seq]") {
    TestHost h;
    h.state.global.seqClock = ymengine::SeqClock::Internal;
    MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    auto bpmTo = [&](bool max) {
        const auto r = w.rectOf("bpm");
        REQUIRE(r);
        press(w, {trackEnd(*r, max), r->y + r->h / 2});
    };
    bpmTo(true);
    CHECK(h.v("freeTempo") == 240.0f);
    // MIDI ext: the tempo comes from the MIDI clock, the slider no longer responds (the prototype's setClkMode).
    h.values[size_t(plugin::findParam("arpClock"))] = 1;
    w.tick(0);
    bpmTo(false);
    CHECK(h.v("freeTempo") == 240.0f);
    // Internal: the slider takes over again.
    h.values[size_t(plugin::findParam("arpClock"))] = 0;
    w.tick(0);
    bpmTo(false);
    CHECK(h.v("freeTempo") == 60.0f);
}

TEST_CASE("Settings > Quantize to VBL, state setting", "[ui][seq]") {
    TestHost h;
    MainWindow w(h);
    w.settingsWindow().show();
    auto choose = [&](const std::string& value) {
        click(w, "setQuantize");
        CHECK(labels(w) == std::vector<std::string>{"Off", "On"});
        pick(w, value);
        w.tick(0);
    };
    CHECK_FALSE(h.state.global.quantizeVbl);
    choose("On");
    CHECK(h.state.global.quantizeVbl);
    choose("Off");
    CHECK_FALSE(h.state.global.quantizeVbl);
}

TEST_CASE("Step Sequencer: right click on the metronome, its volume by slider", "[ui][seq][metronome]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const gemui::Rect m = *w.rectOf("seqMetro");
    const gemui::Point c{m.x + m.w / 2, m.y + m.h / 2};
    w.desktop().mouseMove(c, {});
    w.desktop().mouseDown(c, gemui::MouseButton::Right, {});
    w.desktop().mouseUp(c, {});
    REQUIRE(w.desktop().hasPopups());
    CHECK_FALSE(h.state.global.metronome);  // the right click does not switch it on
    CHECK(h.state.global.metronomeLevel == 100);
    // 96 px vertical slider under the button, or above it near the bottom of the desktop: its bottom is worth 0, its
    // middle about half.
    REQUIRE(w.desktop().topPopup());
    const gemui::Rect pb = w.desktop().topPopup()->bounds();
    CHECK(pb.x == m.x);
    CHECK((pb.y == m.bottom() + 2 || pb.bottom() == m.y - 2));
    press(w, {pb.x + 2 + 4 + 2 + 8, pb.y + 8 + 95});
    CHECK(h.state.global.metronomeLevel == 0);
    press(w, {pb.x + 2 + 4 + 2 + 8, pb.y + 8 + 48});
    CHECK(h.state.global.metronomeLevel >= 45);
    CHECK(h.state.global.metronomeLevel <= 55);
}

TEST_CASE("Step Sequencer: grown past the bottom of the desktop, the window moves up until its bottom shows",
          "[ui][seq]") {
    TestHost h;
    MainWindow w(h);
    pluginui::SeqWindow& s = w.sequencer();
    s.show();
    // Moved low by its bar: its bottom on the desktop's, never below (Indus, 26/10/2026).
    const gemui::Rect b0 = s.bounds();
    const int low = MainWindow::kHeight - b0.h;
    s.place({double(b0.x), double(MainWindow::kHeight - b0.h / 2), double(b0.w), double(b0.h)});
    s.refresh();
    REQUIRE(s.bounds().y == low);
    REQUIRE(s.bounds().h == b0.h);
    // The overview opens: taller, the window moves up, its bottom on the desktop's.
    click(w, "seqOver");
    REQUIRE(w.rectOf("overMap"));
    const gemui::Rect b1 = s.bounds();
    CHECK(b1.h > b0.h);
    CHECK(b1.bottom() == MainWindow::kHeight);
    // It closes: the window gets shorter where it stands.
    click(w, "seqOver");
    CHECK_FALSE(w.rectOf("overMap"));
    CHECK(s.bounds().y == b1.y);
    CHECK(s.bounds().h == b0.h);
    // Never above the menu bar: at the top of the work area, it grows downwards.
    s.place({double(b0.x), double(w.desktop().workTop()), double(b0.w), double(b0.h)});
    s.refresh();
    click(w, "seqOver");
    CHECK(s.bounds().y == w.desktop().workTop());
}

TEST_CASE("Step Sequencer: the Track button between Launch and Bank chooses the track edited", "[ui][seq]") {
    // Indus, 28/10/2026 10:31: a button between Launch and Bank that selects the track edited.
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const auto launch = w.rectOf("seqLaunch"), track = w.rectOf("seqTrack"), bank = w.rectOf("seqBankA");
    REQUIRE(launch);
    REQUIRE(track);
    REQUIRE(bank);
    CHECK(track->x > launch->right());
    CHECK(track->right() < bank->x);
    CHECK(track->y < launch->bottom());
    // Right after its "Track" label, 8 px away like the other label + button pairs (Indus, 29/10/2026 10:54):
    // Launch, 8 px gap and 14 px margin, the label, 8 px gap, the button.
    CHECK(std::abs(track->x - (launch->right() + 8 + 14 + int(gemui::Font::width("Track", 16)) + 8)) <= 1);
    const auto* b = dynamic_cast<const gemui::Button*>(w.widgetOf("seqTrack"));
    REQUIRE(b);
    CHECK(b->text() == "1");
    auto menu = [&w]() {
        click(w, "seqTrack");
        const auto* m = dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup());
        REQUIRE(m);
        return m->items();
    };
    std::vector<gemui::MenuItem> items = menu();
    REQUIRE(items.size() == 16);  // the tracks of 16 ch; 32 in 32 ch
    CHECK(items[0].label == "1 [Chip Voice]");
    CHECK(items[15].label == "16 [Chip Voice]");
    items[2].pick();
    w.desktop().closePopups();
    w.tick(0);
    CHECK(h.v("focusTrack") == 3.0f);
    CHECK(b->text() == "3");
    CHECK(w.trackTitle(2) == "3");
    // Authentic 3ch, Computer ST: the four voices of the chip.
    h.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    h.values[size_t(plugin::paramIndex(plugin::Global::FocusTrack))] = 1;
    w.tick(10);
    items = menu();
    w.desktop().closePopups();
    REQUIRE(items.size() == 4);
    CHECK(items[0].label.rfind("A [", 0) == 0);
    CHECK(items[3].label.rfind("Digi [", 0) == 0);
    CHECK(b->text() == "A");
}

TEST_CASE("Step Sequencer: a longer track name keeps Bank and Pattern on the transport's row", "[ui][seq]") {
    // Indus, 29/10/2026 15:43: on a DMA or Digidrum track, the banks moved right and the patterns went under
    // the transport buttons. The Track button keeps the width of the mode's longest name, shortened where the row
    // lacks room.
    TestHost h;
    MainWindow w(h);
    // Song mode with an entry: the Song arrows join the transport, the row is at its fullest. An 8-character name
    // (kTrackNameMax) on track 1 for the 16-track mode.
    h.editExtras([](plugin::NonParamState& s) {
        s.global.songMode = true;
        s.seq.song.entries = {ymseq::SongEntry{0, 1, 0}};
        s.trackNames[0] = "WWWWWWWW";
    });
    w.tick(0);
    w.sequencer().show();
    const auto* b = dynamic_cast<const gemui::Button*>(w.widgetOf("seqTrack"));
    REQUIRE(b);
    // The Song button stays in reverse video while Song mode is on (Indus, 29/10/2026 20:11).
    const auto* songBtn = dynamic_cast<const gemui::Button*>(w.widgetOf("songMode"));
    REQUIRE(songBtn);
    CHECK(songBtn->on());
    h.editExtras([](plugin::NonParamState& s) { s.global.songMode = false; });
    w.tick(0);
    CHECK_FALSE(songBtn->on());
    h.editExtras([](plugin::NonParamState& s) { s.global.songMode = true; });
    w.tick(0);
    CHECK(songBtn->on());
    double tick = 0;
    auto show = [&](float mode, float computer, int track) {
        h.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = mode;
        h.values[size_t(plugin::paramIndex(plugin::Global::Computer))] = computer;
        h.values[size_t(plugin::paramIndex(plugin::Global::FocusTrack))] = float(track);
        w.tick(tick += 10);
        const auto play = w.rectOf("trPlay"), track_ = w.rectOf("seqTrack"), bankA = w.rectOf("seqBankA"),
                   pat1 = w.rectOf("seqPat1"), pat8 = w.rectOf("seqPat8");
        REQUIRE(play);
        REQUIRE(track_);
        REQUIRE(bankA);
        REQUIRE(pat1);
        REQUIRE(pat8);
        CHECK(pat1->y < play->bottom());  // on the transport's row
        CHECK(pat8->right() <= w.sequencer().bounds().right());
        CHECK(track_->right() < bankA->x);
        // The track's instrument, its number alone with the Song arrows, between them.
        const auto ins = w.rectOf("seqInsNum");
        REQUIRE(ins);
        CHECK(ins->x > track_->right());
        CHECK(ins->right() < bankA->x);
        CHECK_FALSE(w.rectOf("seqInsOpen"));
        return std::pair{track_->w, bankA->x};
    };
    // 16 tracks: the long name is shortened to what the row leaves, the other tracks keep that width.
    const auto named = show(0, 0, 1);  // FocusTrack counts from 1
    CHECK(b->text().size() < 8);
    CHECK(std::string("WWWWWWWW").starts_with(b->text()));
    CHECK(show(0, 0, 2) == named);
    CHECK(b->text() == "2");
    // Authentic 3ch, then Computer STE, then Dual chip: "Digi", "DMA1", "Digi1" do not move Bank and Pattern.
    for (const auto& [mode, computer, wide, name] :
         {std::tuple{1.0f, 0.0f, 4, "Digi"}, {1.0f, 1.0f, 5, "DMA1"}, {2.0f, 0.0f, 4, "Digi1"}}) {
        CAPTURE(name);
        const auto a = show(mode, computer, 1);
        const auto d = show(mode, computer, wide);
        CHECK(b->text() == name);
        CHECK(d == a);
        CHECK(d.second == named.second);
    }
}
