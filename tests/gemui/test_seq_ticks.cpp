// STYM2149 — Ticks timing in the Step Sequencer window (M6, B5, D-149): entries of the Resolution menu, typed speed
// and groove, button and tempo shown; 100 and 200 Hz replay tick in Settings > VBL frequency (D-441).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/IconData.h"
#include "pluginui/MainWindow.h"
#include "pluginui/SettingsWindow.h"

using namespace stym;
using pluginui::MainWindow;
using plugin::paramIndex;
using ymengine::SeqTiming;

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
    void loadDigidrum(int) override {}
    std::string preference(std::string_view, std::string_view fallback) const override { return std::string(fallback); }
    void setPreference(std::string_view, std::string_view) override {}
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}

    const ymengine::TrackPatch& patch(int track = 0) const { return state.tracks[size_t(track)].patch; }
};

gemui::Point centre(const gemui::Rect& r) { return {r.x + r.w / 2, r.y + r.h / 2}; }

void press(MainWindow& w, gemui::Point p, gemui::MouseButton b = gemui::MouseButton::Left) {
    w.desktop().mouseMove(p, {});
    w.desktop().mouseDown(p, b, {});
    w.desktop().mouseUp(p, {});
}

void click(MainWindow& w, const std::string& id, gemui::MouseButton b = gemui::MouseButton::Left) {
    const auto r = w.rectOf(id);
    REQUIRE(r);
    press(w, centre(*r), b);
}

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

// Prompt box open: the proposed text erased, `text` typed, Enter.
void answer(MainWindow& w, const std::string& text) {
    REQUIRE(w.desktop().hasModal());
    for (int i = 0; i < 60; ++i) key(w, gemui::Key::Backspace);
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

std::string text(MainWindow& w, const char* id) {
    const gemui::Widget* x = w.sequencer().widgetOf(id);
    REQUIRE(x);
    if (const auto* b = dynamic_cast<const gemui::Button*>(x)) return b->text();
    if (const auto* v = dynamic_cast<const gemui::ValueField*>(x)) return v->text();
    FAIL("no text");
    return {};
}

bool enabled(MainWindow& w, const char* id) {
    const gemui::Widget* x = w.sequencer().widgetOf(id);
    REQUIRE(x);
    return x->enabled();
}

}  // namespace

TEST_CASE("Ticks: Resolution menu, Ticks timing, typed speed and groove", "[ui][seq][ticks]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    CHECK(text(w, "seqReso") == "1/16");
    CHECK(enabled(w, "bpm"));

    // Resolutions of the prototype (1/16 checked), 1/64 after 1/32 (D-422), 1/32T after 1/16T (D-433), then below a
    // rule the entries of the Ticks timing.
    click(w, "seqReso");
    std::vector<std::string> l = labels(w);
    REQUIRE(l.size() == 16);  // then the CMD row preference (D-150)
    CHECK(l[3] == "1/32 thirty-second");
    CHECK(l[4] == "1/64 sixty-fourth");
    CHECK(l[5] == "---");
    CHECK(l[6] == "1/4T triplet");
    CHECK(l[8] == "1/16T triplet");
    CHECK(l[9] == "1/32T triplet");
    CHECK(l[10] == "---");
    CHECK(l[11] == "Ticks: Speed ticks per line");
    CHECK(l[12] == "Speed: 6 ticks...");
    CHECK(l[13] == "Groove: 0...");
    CHECK(menu(w).checked() == 2);
    pick(w, 11);
    CHECK(h.state.global.timing == SeqTiming::Ticks);
    CHECK(h.state.global.resolution == 2);  // kept for the grid and for going back to BPM
    CHECK(text(w, "seqReso") == "Ticks 6");
    // Free run: the tempo no longer leads, the value shows that of the lines (6 ticks at 50 Hz in 1/16: 125 BPM).
    CHECK_FALSE(enabled(w, "bpm"));
    CHECK_FALSE(enabled(w, "swing"));
    CHECK(text(w, "bpmVal") == "ticks 125.00");

    // Speed: brought into 1-31; a text that is not a number changes nothing. The check mark is on the Ticks entry,
    // the tenth plain one (it fell on Groove before D-422, which counts the plain entries only).
    click(w, "seqReso");
    CHECK(menu(w).checked() == 9);
    pick(w, 12);
    answer(w, "3");
    CHECK(h.state.global.speed == 3);
    click(w, "seqReso");
    pick(w, 12);
    answer(w, "99");
    CHECK(h.state.global.speed == 31);
    click(w, "seqReso");
    pick(w, 12);
    answer(w, "x");
    CHECK(h.state.global.speed == 31);
    click(w, "seqReso");
    pick(w, 12);
    answer(w, "6");

    // Groove: up to 16 values from 0 to 15; the button marks it with a "+".
    click(w, "seqReso");
    pick(w, 13);
    answer(w, "1 0, 2 20");
    CHECK(h.state.global.grooveLength == 4);
    CHECK(h.state.global.groove[0] == 1);
    CHECK(h.state.global.groove[2] == 2);
    CHECK(h.state.global.groove[3] == 15);
    CHECK(text(w, "seqReso") == "Ticks 6+");
    click(w, "seqReso");
    CHECK(labels(w)[13] == "Groove: 1 0 2 15...");
    pick(w, 13);
    answer(w, "");
    CHECK(h.state.global.grooveLength == 1);
    CHECK(h.state.global.groove[0] == 0);
    CHECK(text(w, "seqReso") == "Ticks 6");

    // Replay tick at 200 Hz: 6 ticks make lines of 30 ms (500 BPM in 1/16).
    h.values[size_t(paramIndex(plugin::Global::VblRate))] = 4;
    w.tick(0);
    CHECK(text(w, "bpmVal") == "ticks 500.00");

    // A resolution sets the BPM timing back.
    click(w, "seqReso");
    pick(w, 3);
    CHECK(h.state.global.timing == SeqTiming::Bpm);
    CHECK(h.state.global.resolution == 3);
    CHECK(text(w, "seqReso") == "1/32");
    CHECK(enabled(w, "bpm"));
    CHECK(enabled(w, "swing"));

    // 1/64 (D-422): saved as the eighth resolution, shown after 1/32; the check mark follows the order of the menu,
    // that of a triplet the plain entries (1/4T is the sixth one, after the rule).
    click(w, "seqReso");
    pick(w, 4);
    CHECK(h.state.global.resolution == 7);
    CHECK(text(w, "seqReso") == "1/64");
    click(w, "seqReso");
    CHECK(menu(w).checked() == 4);
    w.desktop().closePopups();
    // The Note button draws the sixty-fourth note for Step; the Note menu has 1/64 after 1/32, one step long.
    const auto* fig = dynamic_cast<const gemui::Button*>(w.sequencer().widgetOf("seqFig"));
    REQUIRE(fig);
    CHECK(fig->icon() == &pluginui::icons::kFigures[7]);
    click(w, "seqFig");
    l = labels(w);
    REQUIRE(l.size() == 18);  // 15 figures, 3 rules
    CHECK(l[7] == "1/32 2 steps");
    CHECK(l[8] == "1/64 1 step");
    CHECK(l[9] == "---");
    CHECK(l[10] == "1/4. 24 steps");
    CHECK(menu(w).items()[8].icon == &pluginui::icons::kFigures[7]);
    pick(w, 8);
    CHECK(fig->icon() == &pluginui::icons::kFigures[7]);
    click(w, "seqFig");
    CHECK(menu(w).checked() == 7);
    pick(w, 0);
    click(w, "seqReso");
    pick(w, 6);
    CHECK(h.state.global.resolution == 4);
    CHECK(text(w, "seqReso") == "1/4T");
    click(w, "seqReso");
    CHECK(menu(w).checked() == 5);

    // 1/32T (D-433): saved as the ninth resolution, shown after 1/16T, twelve steps per quarter note. The Note button
    // draws the thirty-second-note triplet for Step, the last value of the Note menu, one step long.
    pick(w, 9);
    CHECK(h.state.global.resolution == 8);
    CHECK(text(w, "seqReso") == "1/32T");
    click(w, "seqReso");
    CHECK(menu(w).checked() == 8);
    w.desktop().closePopups();
    CHECK(fig->icon() == &pluginui::icons::kFigures[14]);
    click(w, "seqFig");
    l = labels(w);
    REQUIRE(l.size() == 18);
    CHECK(l[4] == "1/4 12 steps");
    CHECK(l[6] == "1/16 3 steps");
    CHECK(l[7] == "1/32 1.5 steps");
    CHECK(l[14] == "1/4T 8 steps");
    CHECK(l[16] == "1/16T 2 steps");
    CHECK(l[17] == "1/32T 1 step");
    CHECK(menu(w).items()[7].disabled);
    CHECK_FALSE(menu(w).items()[17].disabled);
    CHECK(menu(w).items()[17].icon == &pluginui::icons::kFigures[14]);
    pick(w, 16);
    CHECK(fig->icon() == &pluginui::icons::kFigures[13]);
    w.desktop().closePopups();
}

TEST_CASE("Ticks: Settings > VBL frequency, 50 to 200 Hz, out of the Options menu (D-441)", "[ui][ticks]") {
    // Indus, 26/10/2026 04:03: the VBL frequency belongs in the Settings window.
    TestHost h;
    MainWindow w(h);
    w.openMenu(3);
    const auto& items = menu(w).items();
    const auto vblEntry = [](const gemui::MenuItem& m) { return m.label == "VBL frequency"; };
    CHECK(std::none_of(items.begin(), items.end(), vblEntry));
    w.desktop().closePopups();
    pluginui::SettingsWindow& s = w.settingsWindow();
    s.show();
    CHECK(pluginui::SettingsWindow::labelOf("setVbl") == "VBL frequency");
    int checked = -1;
    std::vector<gemui::MenuItem> vbl = s.items("setVbl", checked);
    std::vector<std::string> labels;
    for (const auto& m : vbl) labels.push_back(m.label);
    CHECK(labels == std::vector<std::string>{"50 Hz (PAL)", "60 Hz (NTSC)", "70 Hz (Monochrome)", "100 Hz", "200 Hz"});
    CHECK(checked == 0);
    REQUIRE(s.button("setVbl"));
    CHECK(s.button("setVbl")->text() == "50 Hz (PAL)");
    // Between Timer and Quantize to VBL, in SOUND ENGINE.
    const gemui::Rect engine = *s.rectOf("setEngine"), r = *s.rectOf("setVbl");
    CHECK(r.x >= engine.x);
    CHECK(r.right() <= engine.right());
    CHECK(r.y > s.rectOf("setTimer")->bottom());
    CHECK(r.bottom() < s.rectOf("setQuantize")->y);
    vbl[4].pick();
    CHECK(h.values[size_t(paramIndex(plugin::Global::VblRate))] == 4.0f);
    s.refresh();
    CHECK(s.button("setVbl")->text() == "200 Hz");
}

namespace {

// Button of the open alert that reads `label`.
void alertButton(MainWindow& w, const std::string& label) {
    gemui::Widget* m = w.desktop().topModal();
    REQUIRE(m);
    for (gemui::Widget* c : m->children())
        if (const auto* b = dynamic_cast<const gemui::Button*>(c); b && b->text() == label) {
            press(w, centre(c->bounds()));
            return;
        }
    FAIL("no button " << label);
}

bool undoKey(MainWindow& w) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = U'z';
    e.code = 'Z';
    e.mods.ctrl = true;
    return w.desktop().keyDown(e);
}

}  // namespace

TEST_CASE("Resolution: with notes in the patterns, an alert asks whether they fit the new grid (Indus, 26/10/2026, "
          "D-448)",
          "[ui][seq]") {
    TestHost h;
    // 1/16: a note at step 4 held 4 steps (beat 2, one beat long), a tempo at step 8, a Rest of 16 steps in the Song.
    ymseq::Lane& l = h.state.seq.cur().edit(0);
    l.pat[4] = 0;
    for (int s = 4; s < 8; ++s) l.tie[size_t(s)] = 1;
    h.state.seq.cur().bpm[8] = 140;
    h.state.seq.song.entries.push_back(ymseq::SongEntry::rest(ymseq::SongKind::Rest, 16));
    MainWindow w(h);
    w.sequencer().show();
    const ymseq::Project& p = h.state.seq;

    // Cancel, the default (Enter): nothing changes.
    click(w, "seqReso");
    pick(w, 1);  // 1/8
    REQUIRE(w.desktop().hasModal());
    key(w, gemui::Key::Enter);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.state.global.resolution == 2);

    // Fit: the note keeps its time, on the steps of 1/8; one undo step gives everything back.
    click(w, "seqReso");
    pick(w, 1);
    alertButton(w, "Fit");
    CHECK(h.state.global.resolution == 1);
    CHECK(p.cur().steps == 8);
    CHECK(p.cur().lane(0).pat[2] == 0);
    CHECK(p.cur().lane(0).tie[3] == 1);
    CHECK(p.cur().lane(0).tie[4] == 0);
    CHECK(p.cur().bpm[4] == 140);
    CHECK(p.song.entries[0].arg == 8);
    CHECK(undoKey(w));
    CHECK(h.state.global.resolution == 2);
    CHECK(p.cur().steps == 16);
    CHECK(p.cur().lane(0).pat[4] == 0);
    CHECK(p.cur().bpm[8] == 140);
    CHECK(p.song.entries[0].arg == 16);

    // Keep: the note stays on its step, as before.
    click(w, "seqReso");
    pick(w, 1);
    alertButton(w, "Keep");
    CHECK(h.state.global.resolution == 1);
    CHECK(p.cur().steps == 16);
    CHECK(p.cur().lane(0).pat[4] == 0);

    // The same resolution, or no note: no alert.
    click(w, "seqReso");
    pick(w, 1);
    CHECK_FALSE(w.desktop().hasModal());
    h.editExtras([](plugin::NonParamState& s) { s.seq.cur().lanes[0] = nullptr; });
    w.tick(0);
    click(w, "seqReso");
    pick(w, 2);
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(h.state.global.resolution == 2);
}
