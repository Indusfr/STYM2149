// STYM2149 — CMD row of the Step Sequencer (M6, B4, D-150): "CMD row" preference of the Resolution menu, box of the
// commands of a step (one row each with its value and On/Off, Cancel by default: tenth DAW test, items 121 and 122; the
// effects of item 130, D-338), right-click eraser, row kept as long as a command is written; the rows of their own of
// the eleventh DAW test (VIB, BPM, ENV, DLY, CUT: items 132, 133 and 138 to 140) and of the twelfth (LFO, MOD, ARP,
// the restart of ENV, the BPM row of the pattern, SMP, TUN, crossed cells: items 142 to 146, 150, 152, 153); Env
// entries of the SRC row (D-277).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "plugincore/Parameters.h"
#include "pluginui/CmdBox.h"
#include "pluginui/MainWindow.h"
#include "ymseq/Commands.h"

using namespace stym;
using pluginui::CmdBox;
using pluginui::MainWindow;
using pluginui::SeqLane;
using ymengine::SeqCmd;
using ymengine::seqCmd;

namespace {

class TestHost : public pluginui::Host {
public:
    plugin::ParamValues values = plugin::defaultValues();
    plugin::NonParamState state;
    uint64_t serial = 1;
    pluginui::EditorSession editor;
    bool alone = true;  // standalone application, otherwise a plugin

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
    bool standalone() const override { return alone; }
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

CmdBox& cmdBox(MainWindow& w) {
    auto* b = dynamic_cast<CmdBox*>(w.desktop().topModal());
    REQUIRE(b);
    return *b;
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

bool ctrl(MainWindow& w, char32_t ch) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = ch;
    e.code = int(ch - U'a' + U'A');
    e.mods = {false, true, false};
    return w.desktop().keyDown(e);
}

const ymseq::Lane& lane0(TestHost& h) { return h.state.seq.cur().lane(0); }

// Box of a row of its own (BPM, DLY, CUT): title, slider, field, None and Set.
class ValueBox {
public:
    explicit ValueBox(MainWindow& w) {
        gemui::Widget* p = w.desktop().topPopup();
        REQUIRE(p);
        for (gemui::Widget* c : p->children()) {
            if (auto* s = dynamic_cast<gemui::Slider*>(c)) slider_ = s;
            if (auto* f = dynamic_cast<gemui::TextField*>(c)) field_ = f;
            if (auto* b = dynamic_cast<gemui::Button*>(c)) (b->text() == "Set" ? set_ : none_) = b;
        }
        REQUIRE(slider_);
        REQUIRE(field_);
        REQUIRE(set_);
        REQUIRE(none_);
        tip_ = p->children().front()->tip();  // the title's
    }
    gemui::Slider& slider() { return *slider_; }
    gemui::TextField& field() { return *field_; }
    const std::string& titleTip() const { return tip_; }
    void type(const std::string& t) {
        field_->setText(t);
        if (field_->onChange) field_->onChange();
    }
    void set() { set_->click(); }
    void none() {
        CHECK(none_->text() == "None");
        none_->click();
    }

private:
    std::string tip_;
    gemui::Slider* slider_ = nullptr;
    gemui::TextField* field_ = nullptr;
    gemui::Button *set_ = nullptr, *none_ = nullptr;
};

}  // namespace

TEST_CASE("CMD row: preference of the Resolution menu, input, eraser", "[ui][seq][commands]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    // By default (ninth DAW test, item 117): the row is shown, before SRC.
    REQUIRE(g.laneRect(0, SeqLane::Cmd).w > 0);
    const gemui::Rect src = g.laneRect(0, SeqLane::Cmd);
    CHECK(g.laneRect(0, SeqLane::Mix).y > src.y);

    // Resolution > CMD row > Off: preference written, the prototype's grid.
    click(w, "seqReso");
    const std::vector<std::string> l = labels(w);
    REQUIRE(l.size() == 14);
    CHECK(l[12] == "---");
    CHECK(l[13] == "Step command rows");
    {
        const gemui::MenuItem& item = menu(w).items()[13];
        REQUIRE(item.sub.size() == 2);
        CHECK(item.subCur == 0);  // On
        item.sub[1].pick();
    }
    w.desktop().closePopups();
    CHECK(h.prefs["seqCmdRow"] == "0");
    CHECK(g.laneRect(0, SeqLane::Cmd).w == 0);
    CHECK(g.laneRect(0, SeqLane::Mix).y == src.y);
    // The rows of their own of the eleventh DAW test go with it while none of their commands is written.
    for (const SeqLane r : {SeqLane::Bpm, SeqLane::Vib, SeqLane::Dly, SeqLane::NoteCut}) {
        CAPTURE(int(r));
        CHECK(g.laneRect(0, r).w == 0);
    }

    // Resolution > CMD row > On: the row comes back before SRC.
    click(w, "seqReso");
    {
        const gemui::MenuItem& item = menu(w).items()[13];
        CHECK(item.subCur == 1);  // Off
        item.sub[0].pick();
    }
    w.desktop().closePopups();
    CHECK(h.prefs["seqCmdRow"] == "1");
    const gemui::Rect cell = g.laneRect(3, SeqLane::Cmd);
    REQUIRE(cell.w > 0);
    CHECK(cell.y == src.y);
    CHECK(g.laneRect(0, SeqLane::Mix).y > cell.y);
    CHECK(g.laneText(3, SeqLane::Cmd) == "-");

    // The box of the step's commands (tenth DAW test, items 121 and 122, D-329, D-330): a row for each, in this order,
    // Cancel the default button.
    press(w, centre(cell));
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.button(CmdBox::kCancel).text() == "Cancel");
        CHECK(b.button(CmdBox::kCancel).style() == gemui::Button::Style::Default);
        CHECK(b.button(CmdBox::kOk).style() == gemui::Button::Style::Normal);
        std::vector<std::string> names;
        for (const SeqCmd k : CmdBox::kOrder) {
            names.push_back(b.label(k).text());
            CHECK_FALSE(b.entry(k).on);
            CHECK(b.onOff(k).text() == "Off");
            CHECK(ymengine::seqCmdInBox(k));
        }
        // Arpeggio and Pitch Slide retired (items 131 and 135, D-340, D-344); Vibrato, BPM, Envelope Shape, Note Delay
        // and Note Cut in rows of their own (items 132, 133 and 138 to 140); Envelope Restart, Sample Offset and Fine
        // Tune too (twelfth DAW test, items 146, 152 and 153).
        CHECK(names ==
              std::vector<std::string>{"Ticks per Line", "Sync Mark", "Volume Slide", "Buzzer Slide", "Noise Slide"});
        // Values to start from: the song's ticks per line, sync mark 1.
        CHECK(b.field(SeqCmd::Speed).text() == "6");
        CHECK(b.field(SeqCmd::Sync).text() == "1");
    }
    CHECK(key(w, gemui::Key::Enter));  // Cancel
    CHECK_FALSE(w.desktop().hasModal());
    CHECK_FALSE(lane0(h).hasCmd(3));

    // A value chosen turns its row on; OK writes the step, MUL for several commands.
    press(w, centre(cell));
    {
        CmdBox& b = cmdBox(w);
        b.slider(SeqCmd::VolSlide, 0).input(4);  // up 4, down 1
        CHECK(b.entry(SeqCmd::VolSlide).on);
        CHECK(b.onOff(SeqCmd::VolSlide).text() == "On");
        CHECK(b.field(SeqCmd::VolSlide).text() == "41");
        CHECK(b.field(SeqCmd::Speed).onCommit("40"));  // brought down to 31
        CHECK(b.field(SeqCmd::Speed).text() == "31");
        CHECK_FALSE(b.field(SeqCmd::VolSlide).onCommit("G1"));
        b.button(CmdBox::kOk).click();
    }
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(lane0(h).cmdAt(SeqCmd::VolSlide, 3) == seqCmd(SeqCmd::VolSlide, 0x41));
    CHECK(lane0(h).cmdAt(SeqCmd::Speed, 3) == seqCmd(SeqCmd::Speed, 31));
    CHECK(g.laneText(3, SeqLane::Cmd) == "MUL");

    // The box shows the step's commands; Off takes one out.
    press(w, centre(cell));
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.entry(SeqCmd::VolSlide).on);
        CHECK(b.entry(SeqCmd::VolSlide).value == 0x41);
        CHECK(b.entry(SeqCmd::Speed).value == 31);
        b.onOff(SeqCmd::VolSlide).click();
        CHECK_FALSE(b.entry(SeqCmd::VolSlide).on);
        CHECK(b.onOff(SeqCmd::VolSlide).text() == "Off");
        b.button(CmdBox::kOk).click();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::VolSlide, 3) == 0);
    CHECK(g.laneText(3, SeqLane::Cmd) == "TIK");

    // A sync mark, ticks per line taken out.
    press(w, centre(cell));
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.field(SeqCmd::Sync).onCommit("5"));
        CHECK(b.entry(SeqCmd::Sync).on);
        b.onOff(SeqCmd::Speed).click();
        b.button(CmdBox::kOk).click();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::Sync, 3) == seqCmd(SeqCmd::Sync, 5));
    CHECK(lane0(h).cmdAt(SeqCmd::Speed, 3) == 0);
    CHECK(g.laneText(3, SeqLane::Cmd) == "SYN");

    // Escape cancels; every row off clears the step.
    press(w, centre(cell));
    cmdBox(w).onOff(SeqCmd::Sync).click();
    CHECK(key(w, gemui::Key::Escape));
    CHECK(lane0(h).cmdAt(SeqCmd::Sync, 3) == seqCmd(SeqCmd::Sync, 5));
    press(w, centre(cell));
    {
        CmdBox& b = cmdBox(w);
        b.onOff(SeqCmd::Sync).click();
        b.button(CmdBox::kOk).click();
    }
    CHECK_FALSE(lane0(h).hasCmd(3));
    CHECK(g.laneText(3, SeqLane::Cmd) == "-");

    // Preference off: the row stays as long as a command is written, so that it can be cleared (right click).
    press(w, centre(g.laneRect(5, SeqLane::Cmd)));
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.field(SeqCmd::Sync).onCommit("2"));
        b.button(CmdBox::kOk).click();
    }
    h.prefs["seqCmdRow"] = "0";
    w.sequencer().refresh();
    REQUIRE(g.laneRect(5, SeqLane::Cmd).w > 0);
    CHECK(g.laneText(5, SeqLane::Cmd) == "SYN");
    press(w, centre(g.laneRect(5, SeqLane::Cmd)), gemui::MouseButton::Right);
    CHECK_FALSE(lane0(h).hasCmd(5));
    CHECK(g.laneRect(5, SeqLane::Cmd).w == 0);

    // Undo: the cleared command comes back.
    CHECK(ctrl(w, U'z'));
    CHECK(lane0(h).cmdAt(SeqCmd::Sync, 5) == seqCmd(SeqCmd::Sync, 0x02));
}

TEST_CASE("CMD row: the commands of the rows of their own stay out of it (eleventh DAW test, items 132, 133, 138 to "
          "140)",
          "[ui][seq][commands]") {
    TestHost h;
    h.editExtras([](plugin::NonParamState& st) {
        ymseq::Lane& l = st.seq.cur().edit(0);
        l.setCmd(3, seqCmd(SeqCmd::Delay, 2));
        l.setCmd(3, seqCmd(SeqCmd::Vibrato, 0));
        st.seq.cur().bpm[3] = 0x8c;
    });
    h.prefs["seqCmdRow"] = "0";
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    // Preference off: no command of the box, no CMD row; the rows of the commands written stay.
    CHECK(g.laneRect(3, SeqLane::Cmd).w == 0);
    CHECK(g.laneRect(3, SeqLane::Dly).w > 0);
    CHECK(g.laneRect(3, SeqLane::Bpm).w > 0);
    CHECK(g.laneRect(3, SeqLane::Vib).w > 0);
    CHECK(g.laneRect(3, SeqLane::NoteCut).w == 0);
    h.prefs["seqCmdRow"] = "1";
    w.sequencer().refresh();
    CHECK(g.laneText(3, SeqLane::Cmd) == "-");
    // OK in the box keeps them; the eraser of the CMD row takes the kinds of the box only.
    press(w, centre(g.laneRect(3, SeqLane::Cmd)));
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.field(SeqCmd::Sync).onCommit("16"));
        b.button(CmdBox::kOk).click();
    }
    CHECK(g.laneText(3, SeqLane::Cmd) == "SYN");
    CHECK(lane0(h).cmdAt(SeqCmd::Delay, 3) == seqCmd(SeqCmd::Delay, 2));
    CHECK(h.state.seq.cur().bpm[3] == 0x8c);
    press(w, centre(g.laneRect(3, SeqLane::Cmd)), gemui::MouseButton::Right);
    CHECK(lane0(h).cmdAt(SeqCmd::Sync, 3) == 0);
    CHECK(lane0(h).cmdAt(SeqCmd::Delay, 3) == seqCmd(SeqCmd::Delay, 2));
    CHECK(lane0(h).cmdAt(SeqCmd::Vibrato, 3) == seqCmd(SeqCmd::Vibrato, 0));
    CHECK(g.laneText(3, SeqLane::Dly) == "2");
    CHECK(g.laneText(3, SeqLane::Bpm) == "140");
    CHECK(g.laneText(3, SeqLane::Vib) == "Off");
}

TEST_CASE("CMD box: slides (tenth DAW test, item 130, D-338)", "[ui][seq][commands]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    const gemui::Rect cell = g.laneRect(4, SeqLane::Cmd);
    press(w, centre(cell));
    {
        CmdBox& b = cmdBox(w);
        // Values to start from: volume down a quarter step, the other slides up one unit.
        CHECK(b.field(SeqCmd::VolSlide).text() == "01");
        CHECK(b.field(SeqCmd::EnvSlide).text() == "10");
        CHECK(b.field(SeqCmd::NoiseSlide).text() == "10");
        for (const SeqCmd k : CmdBox::kOrder) CHECK_FALSE(b.entry(k).on);
        // A slide: up x and down y on two half sliders, typed in hexadecimal.
        b.slider(SeqCmd::VolSlide, 0).input(3);
        CHECK(b.field(SeqCmd::VolSlide).text() == "31");
        CHECK(b.entry(SeqCmd::VolSlide).on);
        CHECK(b.field(SeqCmd::EnvSlide).onCommit("0f"));
        CHECK(b.slider(SeqCmd::EnvSlide, 1).value() == 15);
        CHECK_FALSE(b.field(SeqCmd::NoiseSlide).onCommit("123"));
        b.button(CmdBox::kOk).click();
    }
    const ymseq::Lane& l = lane0(h);
    CHECK(l.cmdAt(SeqCmd::VolSlide, 4) == seqCmd(SeqCmd::VolSlide, 0x31));
    CHECK(l.cmdAt(SeqCmd::EnvSlide, 4) == seqCmd(SeqCmd::EnvSlide, 0x0f));
    CHECK_FALSE(l.cmdAt(SeqCmd::NoiseSlide, 4));
    CHECK(g.laneText(4, SeqLane::Cmd) == "MUL");

    // One command alone: its short name.
    for (const SeqCmd k : CmdBox::kOrder) {
        h.editExtras([k](plugin::NonParamState& st) { st.seq.cur().edit(0).setCmd(6, seqCmd(k, 9)); });
        w.tick(int(k) * 10.0);
        CAPTURE(int(k));
        CHECK(g.laneText(6, SeqLane::Cmd) == std::string(ymseq::cmdTag(k)));
        h.editExtras([k](plugin::NonParamState& st) { st.seq.cur().edit(0).clearCmd(k, 6); });
    }
}

TEST_CASE("VIB row: shown with the vibrato of the track On, a step starts or stops it (item 132, D-341)",
          "[ui][seq][commands]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    // Vibrato On by default (D-279): the row is shown, between GEN and SLD.
    REQUIRE(h.values[size_t(plugin::paramIndex(0, plugin::Track::VibOn))] != 0.0F);
    const gemui::Rect cell = g.laneRect(2, SeqLane::Vib);
    REQUIRE(cell.w > 0);
    CHECK(cell.y > g.laneRect(2, SeqLane::Fx).y);
    CHECK(cell.y < g.laneRect(2, SeqLane::Slide).y);
    CHECK(g.laneText(2, SeqLane::Vib) == "-");
    press(w, centre(cell));
    CHECK(labels(w) == std::vector<std::string>{"No change", "On", "Off"});
    CHECK(menu(w).checked() == 0);
    menu(w).items()[2].pick();  // Off
    w.desktop().closePopups();
    CHECK(lane0(h).cmdAt(SeqCmd::Vibrato, 2) == seqCmd(SeqCmd::Vibrato, 0));
    CHECK(g.laneText(2, SeqLane::Vib) == "Off");
    CHECK(g.laneText(2, SeqLane::Cmd) == "-");
    press(w, centre(cell));
    CHECK(menu(w).checked() == 2);
    menu(w).items()[1].pick();  // On
    w.desktop().closePopups();
    CHECK(lane0(h).cmdAt(SeqCmd::Vibrato, 2) == seqCmd(SeqCmd::Vibrato, 1));
    CHECK(g.laneText(2, SeqLane::Vib) == "On");
    press(w, centre(cell));
    menu(w).items()[0].pick();  // No change
    w.desktop().closePopups();
    CHECK_FALSE(lane0(h).hasCmd(2));
    CHECK(g.laneText(2, SeqLane::Vib) == "-");
    CHECK(ctrl(w, U'z'));
    CHECK(lane0(h).cmdAt(SeqCmd::Vibrato, 2) == seqCmd(SeqCmd::Vibrato, 1));

    // Vibrato Off on the track: the row stays for the V01 written, crossed (twelfth DAW test, item 145); none written,
    // no row.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VibOn))] = 0;
    w.tick(10);
    REQUIRE(g.laneRect(2, SeqLane::Vib).w > 0);
    CHECK(g.laneCrossed(2, SeqLane::Vib));
    CHECK_FALSE(g.laneCrossed(3, SeqLane::Vib));
    CHECK(g.laneText(2, SeqLane::Vib) == "On");
    CHECK(g.tipAt(centre(g.laneRect(2, SeqLane::Vib))) ==
          "VIB: switch the vibrato of the track on or off from this step\nOn: The vibrato of this track is off "
          "(VIBRATO frame)");
    press(w, centre(g.laneRect(2, SeqLane::Vib)), gemui::MouseButton::Right);
    CHECK_FALSE(lane0(h).hasCmd(2));
    CHECK(g.laneRect(2, SeqLane::Vib).w == 0);
}

TEST_CASE("BPM row: the pattern's, under VOL behind a separator, in the standalone application only (items 133, "
          "150, D-342, D-359)",
          "[ui][seq][commands]") {
    TestHost h;
    h.alone = false;
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().bpm[2] = 0x8c; });
    {
        // A plugin: the DAW sets the tempo, no row (a BPM written in the standalone application is kept).
        MainWindow w(h);
        w.sequencer().show();
        CHECK(w.sequencer().grid().laneRect(2, SeqLane::Bpm).w == 0);
        CHECK(w.sequencer().grid().laneText(2, SeqLane::Cmd) == "-");
    }
    h.alone = true;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    REQUIRE(g.laneRect(2, SeqLane::Bpm).w > 0);
    // Under VOL, a separator between them (4 px of black and the line).
    const gemui::Rect vol = g.laneRect(2, SeqLane::Vol);
    CHECK(g.laneRect(2, SeqLane::Bpm).y == vol.bottom() + 1 + 4 + 1);
    CHECK(g.laneText(2, SeqLane::Bpm) == "140");
    CHECK(g.tipAt({g.bounds().x + 10, g.laneRect(2, SeqLane::Bpm).y + 5}) ==
          "BPM: one row for every track of the pattern");

    // A step without a tempo: the box starts from the tempo of the sequencer; a tempo under 32 is brought up.
    press(w, centre(g.laneRect(5, SeqLane::Bpm)));
    {
        ValueBox b(w);
        CHECK(b.titleTip() ==
              "Tempo of every track from this step on, until the next BPM (standalone application, BPM timing)");
        CHECK(b.slider().min() == 32);
        CHECK(b.slider().max() == 255);
        CHECK(b.field().text() == std::to_string(int(h.values[size_t(plugin::paramIndex(plugin::Global::FreeTempo))])));
        b.type("20");
        CHECK(b.slider().value() == 32);
        b.set();
    }
    CHECK(h.state.seq.cur().bpm[5] == 32);
    CHECK_FALSE(lane0(h).hasCmd(5));
    CHECK(g.laneText(5, SeqLane::Bpm) == "32");
    // The same row on every track.
    h.values[size_t(plugin::paramIndex(plugin::Global::FocusTrack))] = 2;
    w.tick(10);
    CHECK(g.laneText(5, SeqLane::Bpm) == "32");
    CHECK(g.laneText(2, SeqLane::Bpm) == "140");
    // Undo, then None takes it out; Shift does not make a block of it; the eraser.
    CHECK(ctrl(w, U'z'));
    CHECK(h.state.seq.cur().bpm[5] == 0);
    press(w, centre(g.laneRect(2, SeqLane::Bpm)));
    {
        ValueBox b(w);
        CHECK(b.field().text() == "140");
        b.none();
    }
    CHECK(h.state.seq.cur().bpm[2] == 0);
    CHECK(g.laneText(2, SeqLane::Bpm) == "-");
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().bpm[7] = 90; });
    w.tick(10);
    press(w, centre(g.laneRect(7, SeqLane::Bpm)), gemui::MouseButton::Right);
    CHECK(h.state.seq.cur().bpm[7] == 0);
    CHECK(ctrl(w, U'z'));
    CHECK(h.state.seq.cur().bpm[7] == 90);

    // Ticks timing: the tempo of the lines comes from their ticks, the BPM written is crossed (item 145).
    h.state.global.timing = ymengine::SeqTiming::Ticks;
    h.editExtras([](plugin::NonParamState&) {});
    w.tick(20);
    CHECK(g.laneCrossed(7, SeqLane::Bpm));
    CHECK_FALSE(g.laneCrossed(6, SeqLane::Bpm));
}

TEST_CASE("ENV row: shown when the envelope plays, the shapes 8 to F (item 138, D-347)", "[ui][seq][commands]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chip);
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VolMode))] = 0;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    CHECK(g.laneRect(1, SeqLane::Env).w == 0);
    double now = 0;
    const auto shown = [&] {
        w.tick(now += 10);
        return g.laneRect(1, SeqLane::Env).w > 0;
    };
    // An Env entry of SRC, a Sync Buzzer step of GEN.
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).mix[6] = uint8_t(ymengine::kSeqMixEnv + 1); });
    CHECK(shown());
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).mix[6] = 1; });
    CHECK_FALSE(shown());
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).fx[6] = 3; });  // Sync Buzzer
    CHECK(shown());
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).fx[6] = 0; });
    CHECK_FALSE(shown());
    // The volume of the track on the envelope, the Sync Buzzer generator.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VolMode))] = 1;
    CHECK(shown());
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VolMode))] = 0;
    CHECK_FALSE(shown());
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::SyncBuzzer);
    REQUIRE(shown());
    // Under GEN.
    const gemui::Rect cell = g.laneRect(1, SeqLane::Env);
    CHECK(cell.y > g.laneRect(1, SeqLane::Fx).y);
    CHECK(cell.y < g.laneRect(1, SeqLane::Vib).y);

    // The menu: No change, then the shapes with their pictures, then Restart (twelfth DAW test, item 146, D-355).
    press(w, centre(cell));
    const std::vector<std::string> want = {"No change", "---", "8", "9", "A", "B", "C", "D", "E", "F", "---",
                                           "Restart"};
    CHECK(labels(w) == want);
    CHECK(menu(w).checked() == 0);
    CHECK(menu(w).items()[0].icon == nullptr);
    for (size_t i = 2; i < 10; ++i) CHECK(menu(w).items()[i].icon != nullptr);
    menu(w).items()[6].pick();  // C
    w.desktop().closePopups();
    CHECK(lane0(h).cmdAt(SeqCmd::Shape, 1) == seqCmd(SeqCmd::Shape, 12));
    CHECK(g.laneText(1, SeqLane::Env) == "C");
    // Restart: None, At once (R00), At tick... (a box of the ticks of the line).
    press(w, centre(cell));
    CHECK(menu(w).checked() == 5);  // among the entries, separators left out
    {
        const gemui::MenuItem& r = menu(w).items()[11];
        REQUIRE(r.sub.size() == 3);
        CHECK(r.subCur == 0);
        CHECK(r.sub[0].label == "None");
        CHECK(r.sub[1].label == "At once");
        CHECK(r.sub[2].label == "At tick...");
        r.sub[1].pick();
    }
    w.desktop().closePopups();
    CHECK(lane0(h).cmdAt(SeqCmd::Retrig, 1) == seqCmd(SeqCmd::Retrig, 0));
    CHECK(lane0(h).cmdAt(SeqCmd::Shape, 1) == seqCmd(SeqCmd::Shape, 12));
    CHECK(g.laneText(1, SeqLane::Env) == "CR");
    press(w, centre(cell));
    CHECK(menu(w).items()[11].subCur == 1);
    menu(w).items()[11].sub[2].pick();
    {
        ValueBox b(w);
        CHECK(b.titleTip() == "The envelope restarts at this tick of the line (0: at once)");
        CHECK(b.slider().min() == 0);
        b.type("3");
        b.set();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::Retrig, 1) == seqCmd(SeqCmd::Retrig, 3));
    press(w, centre(cell));
    CHECK(menu(w).items()[11].subCur == 2);
    CHECK(menu(w).items()[11].sub[2].label == "At tick 3...");
    // No change keeps the restart; the restart alone shows R.
    menu(w).items()[0].pick();
    w.desktop().closePopups();
    CHECK(lane0(h).cmdAt(SeqCmd::Shape, 1) == 0);
    CHECK(g.laneText(1, SeqLane::Env) == "R");
    press(w, centre(cell));
    menu(w).items()[11].sub[0].pick();
    w.desktop().closePopups();
    CHECK_FALSE(lane0(h).hasCmd(1));
    // The eraser takes the shape and the restart.
    h.editExtras([](plugin::NonParamState& st) {
        st.seq.cur().edit(0).setCmd(1, seqCmd(SeqCmd::Shape, 10));
        st.seq.cur().edit(0).setCmd(1, seqCmd(SeqCmd::Retrig, 0));
    });
    w.tick(now += 10);
    CHECK(g.laneText(1, SeqLane::Env) == "AR");
    press(w, centre(cell), gemui::MouseButton::Right);
    CHECK_FALSE(lane0(h).hasCmd(1));
    // A shape written where the envelope does not play: the row stays, crossed.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chip);
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).setCmd(1, seqCmd(SeqCmd::Retrig, 0)); });
    CHECK(shown());
    CHECK(g.laneCrossed(1, SeqLane::Env));
}

TEST_CASE("DLY and CUT rows: a tick of the line, None takes it out (items 139 and 140, D-348, D-349)",
          "[ui][seq][commands]") {
    TestHost h;
    h.state.global.timing = ymengine::SeqTiming::Ticks;
    h.state.global.speed = 6;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    const gemui::Rect dly = g.laneRect(4, SeqLane::Dly), cut = g.laneRect(4, SeqLane::NoteCut);
    REQUIRE(dly.w > 0);
    REQUIRE(cut.w > 0);
    // Under GAT, before RTG.
    CHECK(dly.y > g.laneRect(4, SeqLane::Gat).y);
    CHECK(cut.y > dly.y);
    CHECK(cut.y < g.laneRect(4, SeqLane::Rtg).y);

    // Note delay: from tick 1 to the last tick of the line, starting half way.
    press(w, centre(dly));
    {
        ValueBox b(w);
        CHECK(b.titleTip() == "The notes of this step start at this tick of the line");
        CHECK(b.slider().min() == 1);
        CHECK(b.slider().max() == 5);
        CHECK(b.field().text() == "3");
        b.type("2");
        b.set();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::Delay, 4) == seqCmd(SeqCmd::Delay, 2));
    CHECK(g.laneText(4, SeqLane::Dly) == "2");
    CHECK(g.laneText(4, SeqLane::Cmd) == "-");

    // Note cut: from tick 0; a tick typed beyond the line is kept (up to 255).
    press(w, centre(cut));
    {
        ValueBox b(w);
        CHECK(b.titleTip() == "The notes of this step stop at this tick of the line (0: at once)");
        CHECK(b.slider().min() == 0);
        b.type("0");
        b.set();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::Cut, 4) == seqCmd(SeqCmd::Cut, 0));
    CHECK(g.laneText(4, SeqLane::NoteCut) == "0");
    press(w, centre(cut));
    {
        ValueBox b(w);
        CHECK(b.field().text() == "0");
        b.type("300");
        b.set();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::Cut, 4) == seqCmd(SeqCmd::Cut, 255));
    press(w, centre(dly));
    ValueBox(w).none();
    CHECK(lane0(h).cmdAt(SeqCmd::Delay, 4) == 0);
    CHECK(lane0(h).cmdAt(SeqCmd::Cut, 4) == seqCmd(SeqCmd::Cut, 255));
    CHECK(g.laneText(4, SeqLane::Dly) == "-");

    // A drum track has them too.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    w.tick(10);
    CHECK(g.laneRect(4, SeqLane::Dly).w > 0);
    CHECK(g.laneRect(4, SeqLane::NoteCut).w > 0);
    CHECK(g.laneRect(4, SeqLane::Vib).w == 0);
}

TEST_CASE("LFO, MOD and ARP rows: switches like VIB, crossed where the track has none on (twelfth DAW test, items 142 "
          "to 145, D-351 to D-354)",
          "[ui][seq][commands]") {
    TestHost h;
    h.state.tracks[0].patch.lfo.on = true;
    h.state.tracks[0].patch.ornamentOn = true;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::ArpOn))] = 1;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    // ENV (hidden here), ARP, VIB, LFO, MOD, TUN, then SLD.
    for (const SeqLane r : {SeqLane::Afl, SeqLane::Vib, SeqLane::Lfo, SeqLane::Mod, SeqLane::Tun})
        REQUIRE(g.laneRect(2, r).w > 0);
    CHECK(g.laneRect(2, SeqLane::Afl).y > g.laneRect(2, SeqLane::Fx).y);
    CHECK(g.laneRect(2, SeqLane::Vib).y > g.laneRect(2, SeqLane::Afl).y);
    CHECK(g.laneRect(2, SeqLane::Lfo).y > g.laneRect(2, SeqLane::Vib).y);
    CHECK(g.laneRect(2, SeqLane::Mod).y > g.laneRect(2, SeqLane::Lfo).y);
    CHECK(g.laneRect(2, SeqLane::Tun).y > g.laneRect(2, SeqLane::Mod).y);
    CHECK(g.laneRect(2, SeqLane::Slide).y > g.laneRect(2, SeqLane::Tun).y);

    const std::pair<SeqLane, SeqCmd> rows[] = {
        {SeqLane::Afl, SeqCmd::Arpeggiator}, {SeqLane::Lfo, SeqCmd::Lfo}, {SeqLane::Mod, SeqCmd::Mod}};
    for (const auto& [row, k] : rows) {
        CAPTURE(int(row));
        CHECK(g.laneText(2, row) == "-");
        press(w, centre(g.laneRect(2, row)));
        CHECK(labels(w) == std::vector<std::string>{"No change", "On", "Off"});
        menu(w).items()[2].pick();  // Off
        w.desktop().closePopups();
        CHECK(lane0(h).cmdAt(k, 2) == seqCmd(k, 0));
        CHECK(g.laneText(2, row) == "Off");
        press(w, centre(g.laneRect(5, row)));
        CHECK(menu(w).checked() == 0);
        menu(w).items()[1].pick();  // On
        w.desktop().closePopups();
        CHECK(lane0(h).cmdAt(k, 5) == seqCmd(k, 1));
        CHECK(g.laneText(5, row) == "On");
        CHECK_FALSE(g.laneCrossed(5, row));
    }
    CHECK(g.laneText(2, SeqLane::Cmd) == "-");

    // The LFO, the modulation table and the arpeggiator of the track off: the values written are crossed.
    h.editExtras([](plugin::NonParamState& st) {
        st.tracks[0].patch.lfo.on = false;
        st.tracks[0].patch.ornamentOn = false;
    });
    h.values[size_t(plugin::paramIndex(0, plugin::Track::ArpOn))] = 0;
    w.tick(10);
    for (const auto& [row, k] : rows) {
        CAPTURE(int(row));
        REQUIRE(g.laneRect(5, row).w > 0);
        CHECK(g.laneCrossed(5, row));
        CHECK(g.laneCrossed(2, row));
        CHECK_FALSE(g.laneCrossed(3, row));
    }
    CHECK(g.tipAt(centre(g.laneRect(5, SeqLane::Afl))) ==
          "ARP: switch the arpeggiator of the track on or off from this step\nOn: The arpeggiator of this track is off "
          "(Arpeggiator window)");
    // Cleared, the rows go.
    for (const auto& [row, k] : rows) {
        press(w, centre(g.laneRect(5, row)), gemui::MouseButton::Right);
        press(w, centre(g.laneRect(2, row)), gemui::MouseButton::Right);
        CHECK(g.laneRect(2, row).w == 0);
    }
    CHECK_FALSE(lane0(h).hasCmd(2));

    // On again, the preference off: ARP stays (the row of the prototype), LFO and MOD go.
    h.editExtras([](plugin::NonParamState& st) {
        st.tracks[0].patch.lfo.on = true;
        st.tracks[0].patch.ornamentOn = true;
    });
    h.values[size_t(plugin::paramIndex(0, plugin::Track::ArpOn))] = 1;
    h.prefs["seqCmdRow"] = "0";
    w.tick(20);
    CHECK(g.laneRect(2, SeqLane::Afl).w > 0);
    CHECK(g.laneRect(2, SeqLane::Lfo).w == 0);
    CHECK(g.laneRect(2, SeqLane::Mod).w == 0);
    CHECK(g.laneRect(2, SeqLane::Tun).w == 0);
}

TEST_CASE("SMP and TUN rows: the sample offset of DMA tracks, the fine tune in cents; DLY crossed without a note "
          "(twelfth DAW test, items 145, 152, 153, D-354, D-361, D-362)",
          "[ui][seq][commands]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(plugin::Global::Computer))] = 1;  // STE
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Dma);
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    const gemui::Rect smp = g.laneRect(3, SeqLane::Smp), tun = g.laneRect(3, SeqLane::Tun);
    REQUIRE(smp.w > 0);
    REQUIRE(tun.w > 0);
    // SMP under CUT, before PAN.
    CHECK(smp.y > g.laneRect(3, SeqLane::NoteCut).y);
    CHECK(smp.y < g.laneRect(3, SeqLane::Pnr).y);

    // Sample offset: 0 to 255 steps of 256 frames.
    press(w, centre(smp));
    {
        ValueBox b(w);
        CHECK(b.titleTip() == "The DMA sample of this step starts this far in, in steps of 256 frames");
        CHECK(b.slider().min() == 0);
        CHECK(b.slider().max() == 255);
        CHECK(b.field().text() == "0");
        b.type("16");
        b.set();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::Offset, 3) == seqCmd(SeqCmd::Offset, 16));
    CHECK(g.laneText(3, SeqLane::Smp) == "16");
    CHECK(g.laneText(3, SeqLane::Cmd) == "-");
    // No note starts there: crossed; a note, no longer.
    CHECK(g.laneCrossed(3, SeqLane::Smp));
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).pat[3] = 0; });
    w.tick(10);
    CHECK_FALSE(g.laneCrossed(3, SeqLane::Smp));

    // Fine tune: signed cents, two digits.
    press(w, centre(tun));
    {
        ValueBox b(w);
        CHECK(b.titleTip() == "Tuning of the track in cents from this step on, until the next TUN");
        CHECK(b.slider().min() == -99);
        CHECK(b.slider().max() == 99);
        CHECK(b.field().text() == "0");
        b.type("-25");
        b.set();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::Fine, 3) == seqCmd(SeqCmd::Fine, 75));
    CHECK(g.laneText(3, SeqLane::Tun) == "-25");
    press(w, centre(tun));
    {
        ValueBox b(w);
        CHECK(b.field().text() == "-25");
        b.type("300");
        b.set();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::Fine, 3) == seqCmd(SeqCmd::Fine, 199));
    CHECK(g.laneText(3, SeqLane::Tun) == "+99");
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).setCmd(4, seqCmd(SeqCmd::Fine, 200)); });
    w.tick(20);
    CHECK(g.laneText(4, SeqLane::Tun) == "+99");  // a whole semitone of an older state, shown in two digits

    // A chip track: the offset written stays, crossed; a drum track keeps its tuning.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chip);
    w.tick(30);
    REQUIRE(g.laneRect(3, SeqLane::Smp).w > 0);
    CHECK(g.laneCrossed(3, SeqLane::Smp));
    CHECK_FALSE(g.laneCrossed(3, SeqLane::Tun));
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    w.tick(40);
    REQUIRE(g.laneRect(3, SeqLane::Tun).w > 0);
    CHECK(g.laneCrossed(3, SeqLane::Tun));
    press(w, centre(g.laneRect(3, SeqLane::Tun)), gemui::MouseButton::Right);
    press(w, centre(g.laneRect(4, SeqLane::Tun)), gemui::MouseButton::Right);
    CHECK(g.laneRect(3, SeqLane::Tun).w == 0);

    // Note delay without a note: crossed.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chip);
    h.editExtras([](plugin::NonParamState& st) {
        st.seq.cur().edit(0).setCmd(6, seqCmd(SeqCmd::Delay, 2));
        st.seq.cur().edit(0).setCmd(3, seqCmd(SeqCmd::Delay, 2));
    });
    w.tick(50);
    CHECK(g.laneCrossed(6, SeqLane::Dly));
    CHECK_FALSE(g.laneCrossed(3, SeqLane::Dly));
}

TEST_CASE("SRC row: the Env entries after a separator, shown as E, TE, NE and TNE (sixth DAW test, item 79, D-277)",
          "[ui][seq]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    const gemui::Rect cell = g.laneRect(2, SeqLane::Mix);
    REQUIRE(cell.w > 0);
    press(w, centre(cell));
    const std::vector<std::string> want = {"Channel", "Tone", "Noise",    "Tone+Noise",    "---",
                                           "Env",     "Tone+Env", "Noise+Env", "Tone+Noise+Env"};
    CHECK(labels(w) == want);
    menu(w).items()[6].pick();  // Tone+Env
    w.desktop().closePopups();
    CHECK(lane0(h).mix[2] == 5);
    CHECK(g.laneText(2, SeqLane::Mix) == "TE");
    const char* const texts[] = {"-", "T", "N", "TN", "E", "TE", "NE", "TNE"};
    for (int v = 0; v <= ymengine::kSeqMixMax; ++v) {
        h.editExtras([v](plugin::NonParamState& st) { st.seq.cur().edit(0).mix[2] = uint8_t(v); });
        w.tick(v * 10.0);
        CHECK(g.laneText(2, SeqLane::Mix) == texts[v]);
    }
    CHECK(ctrl(w, U'z'));  // the menu's pick is one undo step
}

TEST_CASE("Step value boxes: a track or pattern changed by the host under the box is not written",
          "[ui][seq][commands]") {
    TestHost h;
    h.state.global.timing = ymengine::SeqTiming::Ticks;
    h.state.global.speed = 6;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    // DLY box opened on track 1, then the host focuses track 2 (automation): Set writes into neither track.
    press(w, centre(g.laneRect(4, SeqLane::Dly)));
    {
        ValueBox b(w);
        h.values[size_t(plugin::paramIndex(plugin::Global::FocusTrack))] = 2;
        w.tick(10);
        b.type("2");
        b.set();
    }
    CHECK_FALSE(lane0(h).hasCmd(4));
    CHECK_FALSE(h.state.seq.cur().lane(1).hasCmd(4));
    w.desktop().closePopups();
    // BPM box opened on pattern 1, then another pattern becomes current (host state read again): nothing written.
    press(w, centre(g.laneRect(5, SeqLane::Bpm)));
    {
        ValueBox b(w);
        h.editExtras([](plugin::NonParamState& s) {
            s.seq.used.set(1);
            s.seq.current = 1;
        });
        w.tick(20);
        b.type("100");
        b.set();
    }
    CHECK(h.state.seq.patterns[0].bpm[5] == 0);
    CHECK(h.state.seq.patterns[1].bpm[5] == 0);
}

TEST_CASE("CMD box and cell menus: a track or pattern changed under them is not written", "[ui][seq][commands]") {
    TestHost h;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VolMode))] = 1;  // volume on the envelope: ENV row shown
    auto& p = h.state.seq;
    p.used.set(1);
    p.patterns[1].steps = 16;
    p.patterns[0].edit(0).setCmd(4, seqCmd(SeqCmd::Speed, 3));
    p.patterns[1].edit(0).setCmd(4, seqCmd(SeqCmd::Sync, 2));
    p.patterns[1].edit(0).rtg[4] = 2;
    p.patterns[0].edit(1).setCmd(4, seqCmd(SeqCmd::NoiseSlide, 0x12));
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    const ymseq::Lane a2 = p.patterns[1].lane(0), t2 = p.patterns[0].lane(1);
    double now = 0;
    // Song playback moves on to A2 (or the state is read again); the host focuses track 2 (automation, MIDI CC 84).
    const auto otherPattern = [&] {
        h.editExtras([](plugin::NonParamState& s) { s.seq.current = 1; });
        w.tick(now += 10);
    };
    const auto otherTrack = [&] {
        h.values[size_t(plugin::paramIndex(plugin::Global::FocusTrack))] = 2;
        w.tick(now += 10);
    };
    SECTION("CMD box: OK once A2 is current") {
        press(w, centre(g.laneRect(4, SeqLane::Cmd)));
        CmdBox& box = cmdBox(w);
        otherPattern();
        REQUIRE(w.desktop().topModal() == &box);
        press(w, centre(box.button(CmdBox::kOk).bounds()));
    }
    SECTION("CMD box: OK once track 2 is focused") {
        press(w, centre(g.laneRect(4, SeqLane::Cmd)));
        CmdBox& box = cmdBox(w);
        otherTrack();
        REQUIRE(w.desktop().topModal() == &box);
        press(w, centre(box.button(CmdBox::kOk).bounds()));
    }
    SECTION("RTG menu: an entry picked once A2 is current") {
        press(w, centre(g.laneRect(4, SeqLane::Rtg)));
        otherPattern();
        const std::vector<std::string> l = labels(w);
        const auto it = std::find(l.begin(), l.end(), "4");
        REQUIRE(it != l.end());
        menu(w).items()[size_t(it - l.begin())].pick();
    }
    SECTION("ENV menu: an entry of the Restart submenu picked once track 2 is focused") {
        press(w, centre(g.laneRect(4, SeqLane::Env)));
        otherTrack();
        REQUIRE(labels(w).back() == "Restart");
        menu(w).items().back().sub[1].pick();  // At once
    }
    w.desktop().closePopups();
    CHECK(h.state.seq.patterns[1].lane(0) == a2);
    CHECK(h.state.seq.patterns[0].lane(1) == t2);
}
