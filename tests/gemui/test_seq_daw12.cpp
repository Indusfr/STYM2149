// STYM2149 — Step Sequencer fixes of the twelfth DAW test: the Euclidean generator keeps its box open under its note
// menu (item 154), asks before replacing written notes (item 155) and picks named rhythms (item 157); the Track length
// box shows the length the track plays (item 156).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gemui/Alert.h"
#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"
#include "ymseq/Tables.h"

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
    void loadDigidrum(int) override {}
    std::map<std::string, std::string, std::less<>> prefs;
    std::string preference(std::string_view k, std::string_view fallback) const override {
        const auto it = prefs.find(k);
        return it == prefs.end() ? std::string(fallback) : it->second;
    }
    void setPreference(std::string_view k, std::string_view v) override { prefs[std::string(k)] = std::string(v); }
    std::string patchLibrary() const override { return {}; }
    void setPatchLibrary(const std::string&) override {}

    const ymseq::Lane& lane0() const { return state.seq.cur().lane(0); }
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

void clickWidget(MainWindow& w, const gemui::Widget& x) { press(w, centre(x.bounds())); }

// Widgets of a popup box of a given type, in the order of its children.
template <class T>
std::vector<T*> widgetsOf(gemui::Widget& box) {
    std::vector<T*> out;
    for (gemui::Widget* c : box.children())
        if (auto* t = dynamic_cast<T*>(c)) out.push_back(t);
    return out;
}

gemui::Button& buttonOf(gemui::Widget& box, const std::string& text) {
    for (gemui::Button* b : widgetsOf<gemui::Button>(box))
        if (b->text() == text) return *b;
    FAIL("no button " << text);
    throw;
}

const gemui::MenuList* topMenu(MainWindow& w) { return dynamic_cast<const gemui::MenuList*>(w.desktop().topPopup()); }

void pick(MainWindow& w, const std::string& label) {
    const gemui::MenuList* m = topMenu(w);
    REQUIRE(m);
    const auto& items = m->items();
    const auto at = std::find_if(items.begin(), items.end(), [&](const gemui::MenuItem& i) { return i.label == label; });
    REQUIRE(at != items.end());
    const gemui::Rect r = m->rowsFrom(int(at - items.begin()));
    REQUIRE(r.h > 0);
    press(w, {r.x + 20, r.y + 10});
}

// Opens the Euclidean generator of track 1 and returns its box (the popup on top).
gemui::Widget& openEuclid(MainWindow& w) {
    w.sequencer().show();
    w.tick(0);
    click(w, "euclidBtn");
    gemui::Widget* box = w.desktop().topPopup();
    REQUIRE(box);
    REQUIRE(!widgetsOf<gemui::Slider>(*box).empty());
    return *box;
}

// The pitch button: the one that is neither Merge, Rhythms nor Apply.
gemui::Button& pitchButton(gemui::Widget& box) {
    for (gemui::Button* b : widgetsOf<gemui::Button>(box))
        if (b->text() != "Merge" && b->text() != "Rhythms" && b->text() != "Apply") return *b;
    FAIL("no pitch button");
    throw;
}

}  // namespace

TEST_CASE("Euclidean generator: the note menu opens over the box, which stays open (item 154)", "[ui][seq][euclid]") {
    TestHost h;
    MainWindow w(h);
    gemui::Widget& box = openEuclid(w);
    gemui::Button& pitch = pitchButton(box);
    const std::string before = pitch.text();
    clickWidget(w, pitch);
    const gemui::MenuList* m = topMenu(w);
    REQUIRE(m);
    REQUIRE(m->items().size() > 2);
    CHECK(m->items()[size_t(m->checked())].label == before);
    // A click outside the menu but inside the box closes the menu alone.
    press(w, {box.bounds().x + 6, box.bounds().y + 6});
    CHECK(w.desktop().topPopup() == &box);
    // The pick is taken and the box is still there.
    clickWidget(w, pitch);
    const std::string other = topMenu(w)->items().front().label;
    REQUIRE(other != before);
    pick(w, other);
    REQUIRE(w.desktop().topPopup() == &box);
    CHECK(pitch.text() == other);
    // Apply writes that pitch (an empty track: no question).
    clickWidget(w, buttonOf(box, "Apply"));
    CHECK(w.desktop().topModal() == nullptr);
    CHECK(h.lane0().pat[0] >= 0);
}

TEST_CASE("Euclidean generator: Apply over written notes asks first (item 155)", "[ui][seq][euclid]") {
    TestHost h;
    h.state.seq.cur().edit(0).pat[1] = 4;  // a note off the hits of E(4,16)
    MainWindow w(h);
    gemui::Widget& box = openEuclid(w);
    clickWidget(w, buttonOf(box, "Apply"));
    auto* a = dynamic_cast<gemui::Alert*>(w.desktop().topModal());
    REQUIRE(a);
    CHECK(a->text().find("The notes already written will be lost.") != std::string::npos);
    CHECK(w.desktop().topPopup() == &box);  // the box stays under the alert
    // Cancel: nothing is written, the box is still open.
    clickWidget(w, a->button(0));
    CHECK(w.desktop().topModal() == nullptr);
    CHECK(w.desktop().topPopup() == &box);
    CHECK(h.lane0().pat[1] == 4);
    CHECK(h.lane0().pat[0] == -1);
    // Apply confirmed: the figure replaces the note.
    clickWidget(w, buttonOf(box, "Apply"));
    a = dynamic_cast<gemui::Alert*>(w.desktop().topModal());
    REQUIRE(a);
    clickWidget(w, a->button(size_t(gemui::Alert::kConfirmOk)));
    CHECK(w.desktop().topModal() == nullptr);
    CHECK(h.lane0().pat[1] == -1);
    CHECK(h.lane0().pat[0] >= 0);
    // Merge on a track that keeps its notes: written at once.
    clickWidget(w, buttonOf(box, "Merge"));
    clickWidget(w, buttonOf(box, "Apply"));
    CHECK(w.desktop().topModal() == nullptr);
}

TEST_CASE("Euclidean generator: Rhythms menu sets steps and pulses (item 157)", "[ui][seq][euclid]") {
    TestHost h;
    MainWindow w(h);
    gemui::Widget& box = openEuclid(w);
    clickWidget(w, buttonOf(box, "Rhythms"));
    const gemui::MenuList* m = topMenu(w);
    REQUIRE(m);
    CHECK(m->items().size() == ymseq::kEuclidRhythms.size());
    // Sorted by steps: the shortest first. A pattern of 16 steps greys the rhythms of 24 steps.
    CHECK(m->items().front().label == "E(2,3) Rumba");
    for (const gemui::MenuItem& it : m->items())
        CHECK(it.disabled == (it.label.find(",24)") != std::string::npos));
    pick(w, "E(5,16) Bossa-Nova");
    REQUIRE(w.desktop().topPopup() == &box);
    const std::vector<gemui::Slider*> s = widgetsOf<gemui::Slider>(box);
    REQUIRE(s.size() == 4);
    CHECK(s[0]->value() == 16);
    CHECK(s[1]->value() == 5);
    // The menu checks the rhythm shown.
    clickWidget(w, buttonOf(box, "Rhythms"));
    m = topMenu(w);
    REQUIRE(m);
    CHECK(m->items()[size_t(m->checked())].label == "E(5,16) Bossa-Nova");
}

TEST_CASE("Track length: the box shows the length the track plays (item 156)", "[ui][seq]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    w.tick(0);
    click(w, "plenBtn");
    gemui::Widget* box = w.desktop().topPopup();
    REQUIRE(box);
    const std::vector<gemui::TextField*> f = widgetsOf<gemui::TextField>(*box);
    REQUIRE(f.size() == 1);
    CHECK(f[0]->text() == std::to_string(h.state.seq.cur().steps));
    const std::vector<gemui::Slider*> s = widgetsOf<gemui::Slider>(*box);
    REQUIRE(s.size() == 1);
    CHECK(s[0]->value() == h.state.seq.cur().steps);
    // Its own length shows as before.
    clickWidget(w, buttonOf(*box, "Set"));
    w.tick(0);
    h.state.seq.tracks[0].length = 12;
    ++h.serial;
    w.tick(0);
    click(w, "plenBtn");
    box = w.desktop().topPopup();
    REQUIRE(box);
    CHECK(widgetsOf<gemui::TextField>(*box)[0]->text() == "12");
}

TEST_CASE("Euclidean generator: another track focused closes the box or leaves Apply without effect",
          "[ui][seq][euclid]") {
    using plugin::paramIndex;
    TestHost h;
    // Track 2 plays drums: its rows are instruments 0-7, not notes.
    h.values[size_t(paramIndex(1, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    MainWindow w(h);
    openEuclid(w);
    // Shift+Right: the track changes and the box, built for track 1's notes, closes with it.
    gemui::KeyEvent e;
    e.key = gemui::Key::Right;
    e.mods.shift = true;
    w.desktop().keyDown(e);
    CHECK(h.values[size_t(paramIndex(plugin::Global::FocusTrack))] == 2.0f);
    CHECK_FALSE(w.desktop().hasPopups());
    // Focus moved by the host (automation) with the box open: Apply writes nothing into the drum track.
    h.values[size_t(paramIndex(plugin::Global::FocusTrack))] = 1.0f;
    w.tick(0);
    gemui::Widget& box = openEuclid(w);
    h.values[size_t(paramIndex(plugin::Global::FocusTrack))] = 2.0f;
    w.tick(0);
    clickWidget(w, buttonOf(box, "Apply"));
    const ymseq::Lane& drums = h.state.seq.cur().lane(1);
    CHECK(std::all_of(drums.pat.begin(), drums.pat.end(), [](auto v) { return v < 0; }));
}

TEST_CASE("Step Sequencer: a digit picks the pattern and closes the popup opened on the old one", "[ui][seq][euclid]") {
    TestHost h;
    MainWindow w(h);
    openEuclid(w);
    REQUIRE(h.state.seq.current == 0);
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = U'5';
    e.code = '5';
    w.desktop().keyDown(e);
    CHECK(h.state.seq.current == 4);
    CHECK_FALSE(w.desktop().hasPopups());
}

TEST_CASE("Euclidean generator: a track focused under the confirmation is not written", "[ui][seq][euclid]") {
    using plugin::paramIndex;
    TestHost h;
    h.values[size_t(paramIndex(1, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    h.state.seq.cur().edit(0).pat[1] = 4;  // a note off the hits: Apply asks first
    MainWindow w(h);
    gemui::Widget& box = openEuclid(w);
    clickWidget(w, buttonOf(box, "Apply"));
    REQUIRE(dynamic_cast<gemui::Alert*>(w.desktop().topModal()));
    // The host focuses the drum track (automation) while the question is open; then OK.
    h.values[size_t(paramIndex(plugin::Global::FocusTrack))] = 2.0f;
    w.tick(0);
    auto* a = dynamic_cast<gemui::Alert*>(w.desktop().topModal());
    REQUIRE(a);
    clickWidget(w, a->button(size_t(gemui::Alert::kConfirmOk)));
    const ymseq::Lane& drums = h.state.seq.cur().lane(1);
    CHECK(std::all_of(drums.pat.begin(), drums.pat.end(), [](auto v) { return v < 0; }));
    CHECK(h.lane0().pat[1] == 4);
}
