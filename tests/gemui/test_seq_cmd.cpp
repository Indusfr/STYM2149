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
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "plugincore/Mapping.h"
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
    // By default (ninth DAW test, item 117): the row is shown, after the rows of the timing and before PAN
    // (thirteenth DAW test, item 166, D-375).
    REQUIRE(g.laneRect(0, SeqLane::Cmd).w > 0);
    const gemui::Rect src = g.laneRect(0, SeqLane::Cmd);
    CHECK(g.laneRect(0, SeqLane::Gat).y < src.y);
    CHECK(g.laneRect(0, SeqLane::Pnr).y > src.y);

    // Resolution > CMD row > Off: preference written, the prototype's grid.
    click(w, "seqReso");
    const std::vector<std::string> l = labels(w);
    REQUIRE(l.size() == 16);
    CHECK(l[14] == "---");
    CHECK(l[15] == "Step command rows");
    {
        const gemui::MenuItem& item = menu(w).items()[15];
        REQUIRE(item.sub.size() == 2);
        CHECK(item.subCur == 0);  // On
        item.sub[1].pick();
    }
    w.desktop().closePopups();
    CHECK(h.prefs["seqCmdRow"] == "0");
    CHECK(g.laneRect(0, SeqLane::Cmd).w == 0);
    // The rows of their own of the eleventh DAW test go with it while none of their commands is written.
    for (const SeqLane r : {SeqLane::Bpm, SeqLane::Vib, SeqLane::Dly, SeqLane::NoteCut}) {
        CAPTURE(int(r));
        CHECK(g.laneRect(0, r).w == 0);
    }

    // Resolution > CMD row > On: the row comes back before PAN.
    click(w, "seqReso");
    {
        const gemui::MenuItem& item = menu(w).items()[15];
        CHECK(item.subCur == 1);  // Off
        item.sub[0].pick();
    }
    w.desktop().closePopups();
    CHECK(h.prefs["seqCmdRow"] == "1");
    const gemui::Rect cell = g.laneRect(3, SeqLane::Cmd);
    REQUIRE(cell.w > 0);
    CHECK(cell.y == src.y);
    CHECK(g.laneRect(0, SeqLane::Pnr).y > cell.y);
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
        b.slider(SeqCmd::VolSlide).input(4);  // down 4: the direction to start from
        CHECK(b.entry(SeqCmd::VolSlide).on);
        CHECK(b.onOff(SeqCmd::VolSlide).text() == "On");
        CHECK(b.field(SeqCmd::VolSlide).text() == "4");
        CHECK(b.direction(SeqCmd::VolSlide).text() == "Down");
        b.direction(SeqCmd::VolSlide).click();  // up 4 (D-391)
        CHECK(b.direction(SeqCmd::VolSlide).text() == "Up");
        CHECK(b.entry(SeqCmd::VolSlide).value == 0x40);
        CHECK(b.field(SeqCmd::Speed).onCommit("40"));  // brought down to 31
        CHECK(b.field(SeqCmd::Speed).text() == "31");
        CHECK_FALSE(b.field(SeqCmd::VolSlide).onCommit("G1"));
        b.button(CmdBox::kOk).click();
    }
    CHECK_FALSE(w.desktop().hasModal());
    CHECK(lane0(h).cmdAt(SeqCmd::VolSlide, 3) == seqCmd(SeqCmd::VolSlide, 0x40));
    CHECK(lane0(h).cmdAt(SeqCmd::Speed, 3) == seqCmd(SeqCmd::Speed, 31));
    CHECK(g.laneText(3, SeqLane::Cmd) == "MUL");

    // The box shows the step's commands; Off takes one out.
    press(w, centre(cell));
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.entry(SeqCmd::VolSlide).on);
        CHECK(b.entry(SeqCmd::VolSlide).value == 0x40);
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
    h.editExtras([](plugin::NonParamState& st) {
        st.seq.cur().edit(0).mix[6] = 7;  // Tone+Noise+Env: all three
        // The Volume Slide of an older project (the box shows it only where it is written, D-393): down a quarter step.
        st.seq.cur().edit(0).setCmd(4, seqCmd(SeqCmd::VolSlide, 0x01));
    });
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    const gemui::Rect cell = g.laneRect(4, SeqLane::Cmd);
    press(w, centre(cell));
    {
        CmdBox& b = cmdBox(w);
        // Values to start from: the slides up one unit.
        CHECK(b.field(SeqCmd::VolSlide).text() == "1");
        CHECK(b.direction(SeqCmd::VolSlide).text() == "Down");
        CHECK(b.field(SeqCmd::EnvSlide).text() == "1");
        CHECK(b.direction(SeqCmd::EnvSlide).text() == "Up");
        CHECK(b.field(SeqCmd::NoiseSlide).text() == "1");
        CHECK(b.direction(SeqCmd::NoiseSlide).text() == "Up");
        for (const SeqCmd k : CmdBox::kOrder) {
            CHECK(b.entry(k).on == (k == SeqCmd::VolSlide));
            CHECK(b.shown(k));
        }
        // A slide: a speed from 0 (stop) to 15 and its direction, x0 up or 0y down (D-391).
        for (const SeqCmd k : {SeqCmd::VolSlide, SeqCmd::EnvSlide, SeqCmd::NoiseSlide}) {
            CHECK(b.slider(k).min() == 0);
            CHECK(b.slider(k).max() == 15);
        }
        b.slider(SeqCmd::VolSlide).input(3);
        CHECK(b.field(SeqCmd::VolSlide).text() == "3");
        CHECK(b.entry(SeqCmd::VolSlide).on);
        CHECK(b.entry(SeqCmd::VolSlide).value == 0x03);
        CHECK(b.field(SeqCmd::EnvSlide).onCommit("40"));  // brought down to 15
        CHECK(b.slider(SeqCmd::EnvSlide).value() == 15);
        CHECK(b.entry(SeqCmd::EnvSlide).value == 0xF0);
        // 16 channels: the Buzzer Slide's button opens its Curve menu (D-394), Down picked there.
        b.direction(SeqCmd::EnvSlide).click();
        REQUIRE(labels(w) == std::vector<std::string>{"Up", "Down", "---", "Curve"});
        press(w, centre(menu(w).itemRect(1)));
        CHECK_FALSE(w.desktop().hasPopups());
        CHECK(b.entry(SeqCmd::EnvSlide).value == 0x0F);
        CHECK(b.field(SeqCmd::EnvSlide).text() == "15");
        CHECK_FALSE(b.field(SeqCmd::NoiseSlide).onCommit(""));
        CHECK_FALSE(b.entry(SeqCmd::NoiseSlide).on);
        b.button(CmdBox::kOk).click();
    }
    const ymseq::Lane& l = lane0(h);
    CHECK(l.cmdAt(SeqCmd::VolSlide, 4) == seqCmd(SeqCmd::VolSlide, 0x03));
    CHECK(l.cmdAt(SeqCmd::EnvSlide, 4) == seqCmd(SeqCmd::EnvSlide, 0x0f));
    CHECK_FALSE(l.cmdAt(SeqCmd::NoiseSlide, 4));
    CHECK(g.laneText(4, SeqLane::Cmd) == "MUL");

    // An xy written with both digits (D-385, or an imported Axy): its speed is their difference, kept until touched.
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).setCmd(5, seqCmd(SeqCmd::EnvSlide, 0x41)); });
    w.tick(5);
    press(w, centre(g.laneRect(5, SeqLane::Cmd)));
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.field(SeqCmd::EnvSlide).text() == "3");
        CHECK(b.direction(SeqCmd::EnvSlide).text() == "Up");
        CHECK(b.entry(SeqCmd::EnvSlide).value == 0x41);
        b.button(CmdBox::kOk).click();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::EnvSlide, 5) == seqCmd(SeqCmd::EnvSlide, 0x41));
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).clearCmd(SeqCmd::EnvSlide, 5); });

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

    // Vibrato Off on the track: the row goes with the V01 written, which it keeps (bug of 24/10/2026, D-380).
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VibOn))] = 0;
    w.tick(10);
    CHECK(g.laneRect(2, SeqLane::Vib).w == 0);
    CHECK(lane0(h).cmdAt(SeqCmd::Vibrato, 2) == seqCmd(SeqCmd::Vibrato, 1));
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VibOn))] = 1;
    w.tick(20);
    REQUIRE(g.laneRect(2, SeqLane::Vib).w > 0);
    CHECK(g.laneText(2, SeqLane::Vib) == "On");
    CHECK_FALSE(g.laneCrossed(2, SeqLane::Vib));
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
    // Under VOL, a separator between them (4 px of black between two 2 px lines, D-458).
    const gemui::Rect vol = g.laneRect(2, SeqLane::Vol);
    CHECK(g.laneRect(2, SeqLane::Bpm).y == vol.bottom() + 2 + 4 + 2);
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
    // A right click opens the ENVELOPE settings and keeps the shape and the restart (D-392; before: the eraser).
    h.editExtras([](plugin::NonParamState& st) {
        st.seq.cur().edit(0).setCmd(1, seqCmd(SeqCmd::Shape, 10));
        st.seq.cur().edit(0).setCmd(1, seqCmd(SeqCmd::Retrig, 0));
    });
    w.tick(now += 10);
    CHECK(g.laneText(1, SeqLane::Env) == "AR");
    press(w, centre(cell), gemui::MouseButton::Right);
    CHECK(w.stepSoundWindow().visible());
    CHECK(w.stepSoundWindow().kind() == pluginui::StepSoundWindow::Kind::Envelope);
    CHECK(lane0(h).cmdAt(SeqCmd::Shape, 1) == seqCmd(SeqCmd::Shape, 10));
    CHECK(lane0(h).hasCmd(1));
    w.stepSoundWindow().close();
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
    // DLY between CND and RTG, CUT after GAT (thirteenth DAW test, item 166, D-375).
    CHECK(dly.y > g.laneRect(4, SeqLane::Cnd).y);
    CHECK(dly.y < g.laneRect(4, SeqLane::Rtg).y);
    CHECK(cut.y > g.laneRect(4, SeqLane::Gat).y);

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

TEST_CASE("LFO, MOD and ARP rows: switches like VIB, gone where the track has none on (twelfth DAW test, items 142 "
          "to 145, D-351 to D-354, D-380)",
          "[ui][seq][commands]") {
    TestHost h;
    h.state.tracks[0].patch.lfo.on = true;
    h.state.tracks[0].patch.ornamentOn = true;
    h.values[size_t(plugin::paramIndex(0, plugin::Track::ArpOn))] = 1;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    // TUN with the pitch, above GEN; after SRC (ENV hidden here), ARP, VIB, LFO, MOD, then SLD (thirteenth DAW test,
    // item 166, D-375).
    for (const SeqLane r : {SeqLane::Afl, SeqLane::Vib, SeqLane::Lfo, SeqLane::Mod, SeqLane::Tun})
        REQUIRE(g.laneRect(2, r).w > 0);
    CHECK(g.laneRect(2, SeqLane::Tun).y < g.laneRect(2, SeqLane::Fx).y);
    CHECK(g.laneRect(2, SeqLane::Afl).y > g.laneRect(2, SeqLane::Mix).y);
    CHECK(g.laneRect(2, SeqLane::Vib).y > g.laneRect(2, SeqLane::Afl).y);
    CHECK(g.laneRect(2, SeqLane::Lfo).y > g.laneRect(2, SeqLane::Vib).y);
    CHECK(g.laneRect(2, SeqLane::Mod).y > g.laneRect(2, SeqLane::Lfo).y);
    CHECK(g.laneRect(2, SeqLane::Slide).y > g.laneRect(2, SeqLane::Mod).y);

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

    // An LFO without depth: its values written are crossed (item 145).
    h.editExtras([](plugin::NonParamState& st) { st.tracks[0].patch.lfo.depth = 0; });
    w.tick(10);
    CHECK(g.laneCrossed(5, SeqLane::Lfo));
    CHECK(g.laneCrossed(2, SeqLane::Lfo));
    CHECK_FALSE(g.laneCrossed(3, SeqLane::Lfo));
    CHECK(g.tipAt(centre(g.laneRect(5, SeqLane::Lfo))) ==
          "LFO: switch the LFO of the track on or off from this step\nOn: The LFO of this track is off (Modulation "
          "window)");
    // The LFO, the modulation table and the arpeggiator of the track off: their rows go with the values written,
    // which they keep (bug of 24/10/2026, D-380).
    h.editExtras([](plugin::NonParamState& st) {
        st.tracks[0].patch.lfo.on = false;
        st.tracks[0].patch.ornamentOn = false;
    });
    h.values[size_t(plugin::paramIndex(0, plugin::Track::ArpOn))] = 0;
    w.tick(20);
    for (const auto& [row, k] : rows) {
        CAPTURE(int(row));
        CHECK(g.laneRect(5, row).w == 0);
        CHECK(g.laneRect(2, row).w == 0);
        CHECK(lane0(h).cmdAt(k, 5) == seqCmd(k, 1));
        CHECK(lane0(h).cmdAt(k, 2) == seqCmd(k, 0));
    }
    h.editExtras([](plugin::NonParamState& st) {
        for (const SeqCmd k : {SeqCmd::Arpeggiator, SeqCmd::Lfo, SeqCmd::Mod}) {
            st.seq.cur().edit(0).clearCmd(k, 2);
            st.seq.cur().edit(0).clearCmd(k, 5);
        }
        st.tracks[0].patch.lfo.depth = 4;
    });
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
    // SMP with the rows of the sound, after SRC, before SLD (thirteenth DAW test, item 166, D-375).
    CHECK(smp.y > g.laneRect(3, SeqLane::Mix).y);
    CHECK(smp.y < g.laneRect(3, SeqLane::Slide).y);

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

TEST_CASE("Control rows in the order of what they do to the note (thirteenth DAW test, item 166, D-375)",
          "[ui][seq][commands]") {
    TestHost h;
    h.state.instruments[0].used = true;  // INS
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VolMode))] = 1;  // ENV
    h.values[size_t(plugin::paramIndex(0, plugin::Track::ArpOn))] = 1;    // ARP
    h.state.tracks[0].patch.lfo.on = true;                                 // LFO
    h.state.tracks[0].patch.ornamentOn = true;                             // MOD
    h.editExtras([](plugin::NonParamState& st) {
        ymseq::Lane& l = st.seq.cur().edit(0);
        for (SeqCmd k : {SeqCmd::Lfo, SeqCmd::Mod, SeqCmd::Offset}) l.setCmd(1, seqCmd(k, 1));  // SMP
        l.slide[3] = 1;             // CRV
        st.seq.cur().bpm[1] = 120;  // BPM
    });
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    std::vector<std::pair<int, SeqLane>> shown;
    for (int k = 0; k < pluginui::kSeqLanes; ++k)
        if (const gemui::Rect r = g.laneRect(0, SeqLane(k)); r.w > 0) shown.emplace_back(r.y, SeqLane(k));
    std::sort(shown.begin(), shown.end());
    std::vector<SeqLane> order;
    for (const auto& [y, r] : shown) order.push_back(r);
    // Pitch, sound, modulations, slide, timing, the other commands, the mix, then the pattern's BPM.
    CHECK(order == std::vector<SeqLane>{SeqLane::Tun, SeqLane::Ins, SeqLane::Fx, SeqLane::Mix, SeqLane::Env,
                                        SeqLane::Smp, SeqLane::Afl, SeqLane::Vib, SeqLane::Lfo, SeqLane::Mod,
                                        SeqLane::Slide, SeqLane::Slc, SeqLane::Cnd, SeqLane::Dly, SeqLane::Rtg,
                                        SeqLane::Gat, SeqLane::NoteCut, SeqLane::Cmd, SeqLane::Pnr, SeqLane::Vol,
                                        SeqLane::Bpm});
}

TEST_CASE("Impossible cells crossed from the sound of the step, empty ones too (thirteenth DAW test, item 165, D-374)",
          "[ui][seq][commands]") {
    TestHost h;
    h.state.instruments[0].used = true;  // the INS row
    h.editExtras([](plugin::NonParamState& st) {
        ymseq::Lane& l = st.seq.cur().edit(0);
        l.pat[0] = 0;  // SID Voice
        l.fx[0] = 2;
        l.pat[2] = 0;  // Sync Buzzer (the ENV row shown)
        l.fx[2] = 3;
        l.pat[4] = 0;  // held from 4 to 6
        l.tie[4] = l.tie[5] = l.tie[6] = 1;
        l.pat[8] = 0;  // slides to the note of step 9
        l.slide[8] = 1;
        l.pat[9] = 2;
        l.fx[12] = 1;  // GEN without a note
    });
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    for (SeqLane r : {SeqLane::Mix, SeqLane::Fx, SeqLane::Env, SeqLane::Ins, SeqLane::Gat, SeqLane::Cnd})
        REQUIRE(g.laneRect(0, r).w > 0);
    // GEN on SID Voice: ENV crossed below it, empty, with its reason; a click opens nothing.
    CHECK(g.laneBlocked(0, SeqLane::Env));
    CHECK(g.laneCrossed(0, SeqLane::Env));
    CHECK_FALSE(g.laneCrossed(0, SeqLane::Mix));
    CHECK(g.tipAt(centre(g.laneRect(0, SeqLane::Env))).find("A SID Voice writes its own volumes") !=
          std::string::npos);
    press(w, centre(g.laneRect(0, SeqLane::Env)));
    CHECK_FALSE(w.desktop().topPopup());
    // GEN on Sync Buzzer: SRC crossed, ENV open.
    CHECK(g.laneBlocked(2, SeqLane::Mix));
    CHECK_FALSE(g.laneCrossed(2, SeqLane::Env));
    press(w, centre(g.laneRect(2, SeqLane::Env)));
    CHECK(w.desktop().topPopup());
    w.desktop().closePopups();
    // A held note: INS and CND crossed on the steps it holds, GAT everywhere but on its last step.
    CHECK_FALSE(g.laneCrossed(4, SeqLane::Ins));
    for (int s : {5, 6}) {
        CAPTURE(s);
        CHECK(g.laneBlocked(s, SeqLane::Ins));
        CHECK(g.laneBlocked(s, SeqLane::Cnd));
    }
    CHECK(g.laneBlocked(4, SeqLane::Gat));
    CHECK(g.laneBlocked(5, SeqLane::Gat));
    CHECK_FALSE(g.laneCrossed(6, SeqLane::Gat));
    // A slide: its first note keeps sounding, so GAT crossed there; the note it reaches keeps the instrument.
    CHECK(g.laneBlocked(8, SeqLane::Gat));
    CHECK(g.laneBlocked(9, SeqLane::Ins));
    CHECK(g.tipAt(centre(g.laneRect(9, SeqLane::Ins))).find("A slide keeps the instrument") != std::string::npos);
    // A rest step: a GEN written there crossed, the empty cells left open.
    CHECK(g.laneCrossed(12, SeqLane::Fx));
    CHECK_FALSE(g.laneBlocked(12, SeqLane::Fx));
    CHECK_FALSE(g.laneCrossed(13, SeqLane::Fx));
    CHECK_FALSE(g.laneCrossed(13, SeqLane::Ins));

    // A DMA track of Authentic 3ch: the DMA sound only, so SRC and GEN crossed everywhere.
    h.values[size_t(plugin::paramIndex(plugin::Global::Computer))] = 1;
    h.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    h.values[size_t(plugin::paramIndex(plugin::Global::FocusTrack))] = 5;  // DMA1 (1-based)
    w.tick(10);
    REQUIRE(plugin::effectiveGenerator(h.values, 4) == ymengine::Generator::Dma);
    for (int s : {0, 1, 5}) {
        CAPTURE(s);
        CHECK(g.laneBlocked(s, SeqLane::Mix));
        CHECK(g.laneBlocked(s, SeqLane::Fx));
    }
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

TEST_CASE("CMD box: the Curve of the Buzzer and Noise slides in 16 channels, Up/Down in 3 channels (D-394)",
          "[ui][seq][commands]") {
    TestHost h;
    h.editExtras([](plugin::NonParamState& st) {
        ymseq::Lane& l = st.seq.cur().edit(0);
        l.mix[6] = 7;  // Tone+Noise+Env: both slides act
        l.setCmd(4, seqCmd(SeqCmd::EnvSlide, 0x20));
        l.setCmd(4, seqCmd(SeqCmd::EnvCurve, 1));  // Exponential
        l.setCmd(4, seqCmd(SeqCmd::NoiseSlide, 0x03));
    });
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    const gemui::Point cell = centre(g.laneRect(4, SeqLane::Cmd));
    // Index of an entry of the open menu by its label.
    const auto at = [&w](const std::string& label) {
        const std::vector<std::string> l = labels(w);
        const auto it = std::find(l.begin(), l.end(), label);
        REQUIRE(it != l.end());
        return int(it - l.begin());
    };
    press(w, cell);
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.curve(SeqCmd::EnvSlide) == 1);
        CHECK(b.direction(SeqCmd::EnvSlide).text() == "Up EXP");
        CHECK(b.direction(SeqCmd::NoiseSlide).text() == "Down");
        CHECK(b.direction(SeqCmd::NoiseSlide).tip().find("Curve (16 channels)") == 0);
        // The menu over the box: Up and Down, the current one marked, then the shapes of the CRV row.
        b.direction(SeqCmd::NoiseSlide).click();
        REQUIRE(labels(w) == std::vector<std::string>{"Up", "Down", "---", "Curve"});
        CHECK(menu(w).checked() == 1);
        press(w, centre(menu(w).itemRect(at("Curve"))));
        CHECK(labels(w).size() == 13 + 4);
        CHECK(labels(w).front() == "Linear");
        CHECK(menu(w).checked() == 0);
        press(w, centre(menu(w).itemRect(at("Wobble"))));
        CHECK_FALSE(w.desktop().hasPopups());
        CHECK(w.desktop().topModal() == &b);
        CHECK(b.curve(SeqCmd::NoiseSlide) == 6);
        CHECK(b.direction(SeqCmd::NoiseSlide).text() == "Down WOB");
        CHECK(b.field(SeqCmd::NoiseSlide).text() == "3");  // the speed kept
        // Linear again: the button shows the direction alone.
        b.direction(SeqCmd::EnvSlide).click();
        press(w, centre(menu(w).itemRect(at("Curve"))));
        CHECK(menu(w).checked() == 1);
        press(w, centre(menu(w).itemRect(at("Linear"))));
        CHECK(b.direction(SeqCmd::EnvSlide).text() == "Up");
        // Esc closes the menu, then the box (Cancel) only at the second press.
        b.direction(SeqCmd::EnvSlide).click();
        CHECK(key(w, gemui::Key::Escape));
        CHECK_FALSE(w.desktop().hasPopups());
        CHECK(w.desktop().topModal() == &b);
        b.button(CmdBox::kOk).click();
    }
    {
        const ymseq::Lane& l = lane0(h);
        CHECK(l.cmdAt(SeqCmd::EnvSlide, 4) == seqCmd(SeqCmd::EnvSlide, 0x20));
        CHECK_FALSE(l.cmdAt(SeqCmd::EnvCurve, 4));  // Linear: no Kxx
        CHECK(l.cmdAt(SeqCmd::NoiseSlide, 4) == seqCmd(SeqCmd::NoiseSlide, 0x03));
        CHECK(l.cmdAt(SeqCmd::NoiseCurve, 4) == seqCmd(SeqCmd::NoiseCurve, 6));
    }
    // A slide switched off takes its curve with it.
    press(w, cell);
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.direction(SeqCmd::NoiseSlide).text() == "Down WOB");
        b.onOff(SeqCmd::NoiseSlide).click();
        b.button(CmdBox::kOk).click();
    }
    CHECK_FALSE(lane0(h).cmdAt(SeqCmd::NoiseSlide, 4));
    CHECK_FALSE(lane0(h).cmdAt(SeqCmd::NoiseCurve, 4));

    // Authentic 3ch: straight slides, an Up/Down button; the Kxx of the step left as it is.
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).setCmd(4, seqCmd(SeqCmd::EnvCurve, 3)); });
    h.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    w.tick(10);
    press(w, cell);
    {
        CmdBox& b = cmdBox(w);
        CHECK(b.direction(SeqCmd::EnvSlide).text() == "Up");
        CHECK(b.direction(SeqCmd::EnvSlide).tip() == "Direction of the slide: up or down");
        b.direction(SeqCmd::EnvSlide).click();
        CHECK_FALSE(w.desktop().hasPopups());
        CHECK(b.direction(SeqCmd::EnvSlide).text() == "Down");
        b.button(CmdBox::kOk).click();
    }
    CHECK(lane0(h).cmdAt(SeqCmd::EnvSlide, 4) == seqCmd(SeqCmd::EnvSlide, 0x02));
    CHECK(lane0(h).cmdAt(SeqCmd::EnvCurve, 4) == seqCmd(SeqCmd::EnvCurve, 3));
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

TEST_CASE("PAN box: the centre shows its 0 (D-384)", "[ui][seq][commands]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    REQUIRE(g.laneRect(2, SeqLane::Pnr).w > 0);
    press(w, centre(g.laneRect(2, SeqLane::Pnr)));  // nothing written: the channel's pan, at the centre
    {
        ValueBox b(w);
        CHECK(b.field().text() == "0");
        CHECK(b.slider().value() == 0);
        b.type("-4");
        b.set();
    }
    CHECK(lane0(h).pnr[2] == ymseq::kPanCenter - 40);
    CHECK(g.laneText(2, SeqLane::Pnr) == "L4");
    press(w, centre(g.laneRect(2, SeqLane::Pnr)));
    {
        ValueBox b(w);
        CHECK(b.field().text() == "-4");
        b.type("0");
        b.set();
    }
    CHECK(lane0(h).pnr[2] == ymseq::kPanCenter);  // a typed 0: an explicit centre
    press(w, centre(g.laneRect(2, SeqLane::Pnr)));
    CHECK(ValueBox(w).field().text() == "0");
}

TEST_CASE("CMD box: a slide shows where it can move a note, from the GEN and SRC cells too (D-386); Axy only "
          "where written (D-393)",
          "[ui][seq][commands]") {
    TestHost h;  // Chip Voice, Tone at a fixed volume
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    using Shown = std::array<bool, 3>;  // Volume, Buzzer and Noise Slides
    double okY = 0, now = 0;
    const auto shown = [&](int s) {
        press(w, centre(g.laneRect(s, SeqLane::Cmd)));
        CmdBox& b = cmdBox(w);
        CHECK(b.shown(SeqCmd::Speed));
        CHECK(b.shown(SeqCmd::Sync));
        okY = b.button(CmdBox::kOk).bounds().y;
        const Shown out = {b.shown(SeqCmd::VolSlide), b.shown(SeqCmd::EnvSlide), b.shown(SeqCmd::NoiseSlide)};
        b.button(CmdBox::kCancel).click();
        return out;
    };
    const auto editLane = [&](const std::function<void(ymseq::Lane&)>& f) {
        h.editExtras([&](plugin::NonParamState& st) { f(st.seq.cur().edit(0)); });
        w.tick(now += 10);
    };
    CHECK(shown(4) == Shown{false, false, false});
    const double oneRow = okY;
    // An SRC cell with noise and the envelope further on: those slides too, until the next command of their kind.
    editLane([](ymseq::Lane& l) { l.mix[9] = 7; });
    CHECK(shown(4) == Shown{false, true, true});
    CHECK(okY > oneRow);  // the box grows by the rows it shows
    editLane([](ymseq::Lane& l) { l.setCmd(7, seqCmd(SeqCmd::NoiseSlide, 0)); });
    CHECK(shown(4) == Shown{false, true, false});
    CHECK(shown(7) == Shown{false, true, true});  // written there, and noise further on
    CHECK(shown(12) == Shown{false, true, false});  // from 12 the track loops back to the next Nxy, at step 7
    // A GEN cell on Sync Buzzer: its envelope alone.
    editLane([](ymseq::Lane& l) {
        l.mix[9] = 0;
        l.clearCmd(SeqCmd::NoiseSlide, 7);
        l.fx[10] = 3;
    });
    CHECK(shown(4) == Shown{false, true, false});
    // The Source of the track on the envelope: no fixed volume any more but on a GEN cell on SID Voice.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::VolMode))] = 1;
    editLane([](ymseq::Lane& l) { l.fx[10] = 0; });
    CHECK(shown(4) == Shown{false, true, false});
    editLane([](ymseq::Lane& l) { l.fx[14] = 2; });
    CHECK(shown(4) == Shown{false, true, false});
    // Noise in the Source of the track; then a drum track, where nothing slides, but a slide written stays to be
    // switched off.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Noise))] = 1;
    editLane([](ymseq::Lane& l) { l.fx[14] = 0; });
    CHECK(shown(4) == Shown{false, true, true});
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    editLane([](ymseq::Lane& l) { l.setCmd(2, seqCmd(SeqCmd::VolSlide, 1)); });
    REQUIRE(g.laneRect(4, SeqLane::Cmd).w > 0);
    CHECK(shown(4) == Shown{false, false, false});
    CHECK(shown(2) == Shown{true, false, false});  // an Axy of an older project, to be switched off
}

TEST_CASE("CRV row: shown while an SLD cell is lit, or a curve is written (D-387)", "[ui][seq][commands]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    CHECK(g.laneRect(0, SeqLane::Slide).w > 0);
    CHECK(g.laneRect(0, SeqLane::Slc).w == 0);
    const double gatY = g.laneRect(0, SeqLane::Gat).y;
    // An SLD cell clicked on: the row comes under SLD, the rows below go down by one.
    press(w, centre(g.laneRect(5, SeqLane::Slide)));
    REQUIRE(lane0(h).slide[5]);
    w.tick(10);
    REQUIRE(g.laneRect(0, SeqLane::Slc).w > 0);
    CHECK(g.laneRect(0, SeqLane::Slc).y > g.laneRect(0, SeqLane::Slide).y);
    CHECK(g.laneRect(0, SeqLane::Gat).y > gatY);
    // A curve written: it keeps the row without the SLD cell (crossed, item 145), so that it can be cleared.
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).slc[9] = 2; });
    press(w, centre(g.laneRect(5, SeqLane::Slide)));
    REQUIRE_FALSE(lane0(h).slide[5]);
    w.tick(20);
    REQUIRE(g.laneRect(9, SeqLane::Slc).w > 0);
    CHECK(g.laneCrossed(9, SeqLane::Slc));
    h.editExtras([](plugin::NonParamState& st) { st.seq.cur().edit(0).slc[9] = 0; });
    w.tick(30);
    CHECK(g.laneRect(0, SeqLane::Slc).w == 0);
    CHECK(g.laneRect(0, SeqLane::Gat).y == gatY);
}

TEST_CASE("VOL box: Slide glides the step to the next step's volume, drawn as a ramp (D-393)", "[ui][seq][commands]") {
    TestHost h;
    MainWindow w(h);
    w.sequencer().show();
    const pluginui::SeqGrid& g = w.sequencer().grid();
    double now = 0;
    h.editExtras([](plugin::NonParamState& st) {
        ymseq::Lane& l = st.seq.cur().edit(0);
        l.vol[3] = 12;
        l.vol[4] = 3;
    });
    w.tick(now += 10);
    const auto slide = [&w]() -> gemui::Button* {
        gemui::Widget* p = w.desktop().topPopup();
        if (!p) return nullptr;
        for (gemui::Widget* c : p->children())
            if (auto* b = dynamic_cast<gemui::Button*>(c); b && b->text() == "Slide") return b;
        return nullptr;
    };
    const gemui::Rect vol = g.laneRect(3, SeqLane::Vol);
    CHECK(g.tipAt(centre(vol)) == "Volume 12\nRight click: a slider and the Slide to the next step");
    press(w, centre(vol), gemui::MouseButton::Right);
    gemui::Button* b = slide();
    REQUIRE(b);
    CHECK_FALSE(b->on());
    CHECK(b->enabled());
    press(w, centre(b->bounds()));
    CHECK(lane0(h).cmdAt(SeqCmd::VolGlide, 3) == seqCmd(SeqCmd::VolGlide, 1));
    CHECK(b->on());
    CHECK(lane0(h).vol[3] == 12);  // the volume stays
    w.desktop().closePopups();
    w.tick(now += 10);
    CHECK(g.tipAt(centre(vol)) == "Volume 12, slides to 3\nRight click: a slider and the Slide to the next step");
    // The CMD cell does not show it: it is the VOL row's.
    CHECK(g.laneText(3, SeqLane::Cmd) == "-");
    // Clicked again: taken out.
    press(w, centre(vol), gemui::MouseButton::Right);
    b = slide();
    REQUIRE(b);
    CHECK(b->on());
    press(w, centre(b->bounds()));
    CHECK(lane0(h).cmdAt(SeqCmd::VolGlide, 3) == 0);
    w.desktop().closePopups();
    // A drum track: greyed.
    h.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(ymengine::Generator::Chipdrum);
    w.tick(now += 10);
    press(w, centre(g.laneRect(3, SeqLane::Vol)), gemui::MouseButton::Right);
    b = slide();
    REQUIRE(b);
    CHECK_FALSE(b->enabled());
    w.desktop().closePopups();
}
