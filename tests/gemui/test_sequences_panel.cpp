// STYM2149 — sequences (M6, B2, D-148) below the Modulation table (fifth DAW test, D-240, D-241): no Edit entry nor
// Ctrl+L, rows shown with the table on, settings off on percussion; PER, NP, MIX, GEN and FIX rows drawn with the
// mouse, wheel and right click, one undo step per gesture; Clear; fixed note, detune, hard sync, pitch matrix, chained
// instrument, vibrato table; session.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "plugincore/Instruments.h"
#include "plugincore/Parameters.h"
#include "pluginui/MainWindow.h"

using namespace stym;
using pluginui::MainWindow;
using pluginui::SequencesPanel;
using Row = pluginui::SeqRowsGrid::Row;
using plugin::paramIndex;
using ymengine::PitchSource;
using ymengine::PitchTarget;

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

void drag(MainWindow& w, std::initializer_list<gemui::Point> path) {
    const gemui::Point first = *path.begin();
    w.desktop().mouseMove(first, {});
    w.desktop().mouseDown(first, gemui::MouseButton::Left, {});
    gemui::Point last = first;
    for (const gemui::Point& p : path) {
        w.desktop().mouseDrag(p, {});
        last = p;
    }
    w.desktop().mouseUp(last, {});
}

void wheel(MainWindow& w, gemui::Point p, double dy, bool shift = false) {
    gemui::Modifiers m;
    m.shift = shift;
    w.desktop().mouseMove(p, {});
    w.desktop().mouseWheel(p, 0, dy, m);
}

bool key(MainWindow& w, gemui::Key k) {
    gemui::KeyEvent e;
    e.key = k;
    return w.desktop().keyDown(e);
}

bool letter(MainWindow& w, char32_t ch, bool ctrl = false) {
    gemui::KeyEvent e;
    e.key = gemui::Key::Character;
    e.ch = ch;
    e.code = int(ch - U'a' + U'A');
    e.mods.ctrl = ctrl;
    return w.desktop().keyDown(e);
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

// Drag of a slider's thumb to the right end of its track.
void dragToMax(MainWindow& w, const char* id) {
    const gemui::Slider* s = w.modulation().sequences().slider(id);
    REQUIRE(s);
    const gemui::Rect t = s->thumbRect(), r = s->bounds();
    drag(w, {centre(t), {r.x + r.w / 2, t.y + 8}, {r.right() + 20, t.y + 8}});
}

gemui::Rect cell(MainWindow& w, Row row, int step) { return w.modulation().sequences().grid().cellRect(row, step); }

}  // namespace

TEST_CASE("Sequences: below the Modulation table, without an Edit entry nor Ctrl+L (D-240, D-241)",
          "[ui][sequences]") {
    TestHost h;
    MainWindow w(h);
    CHECK_FALSE(w.rectOf("seqxGrid"));

    // Neither an Edit menu entry nor Ctrl+L: the sequences are in the Modulation window.
    w.openMenu(2);
    const std::vector<std::string> edit = labels(w);
    CHECK(std::find(edit.begin(), edit.end(), "Sequences") == edit.end());
    key(w, gemui::Key::Escape);
    letter(w, U'l', true);
    CHECK_FALSE(w.modulation().isOpen());
    CHECK_FALSE(w.rectOf("seqxGrid"));

    // Table off: the settings show below the header of the table, the rows do not.
    w.modulation().show();
    const gemui::Rect off = *w.rectOf("ornWin");
    CHECK_FALSE(w.rectOf("orngrid"));
    CHECK_FALSE(w.rectOf("seqxGrid"));
    const char* const settings[] = {"seqxFix",    "seqxDetune", "seqxAnalog",  "seqxSync",    "seqxLink",
                                    "seqxMx0",    "seqxMx8",    "seqxVibMin",  "seqxVibRamp", "seqxVibWrite"};
    for (const char* id : settings) {
        INFO(id);
        const auto r = w.rectOf(id);
        REQUIRE(r);
        CHECK(r->y >= w.rectOf("ornClear")->bottom());
        CHECK(r->bottom() < off.bottom());
        CHECK(r->right() < off.right());
    }
    CHECK_FALSE(w.rectOf("seqxMx9"));

    // On: the rows show inside the table, between its pitch rows and NSE / VOL (D-270), at the left of its grid; the
    // settings below the table; the window stays inside the desktop.
    click(w, "ornOn");
    CHECK(h.patch().ornamentOn);
    const gemui::Rect win = *w.rectOf("ornWin");
    CHECK(win.x == pluginui::ModulationWindow::kX);
    CHECK(win.right() <= MainWindow::kWidth);
    CHECK(win.bottom() <= MainWindow::kHeight);
    const gemui::Rect og = *w.rectOf("orngrid");
    const gemui::Rect g = *w.rectOf("seqxGrid");
    CHECK(g.x == og.x);
    CHECK(g.y == og.y + pluginui::OrnGrid::kSeqY);
    CHECK(g.bottom() < og.y + pluginui::OrnGrid::kNseY);
    CHECK(w.modulation().grid().cellRect(0, pluginui::OrnGrid::kRows - 1).bottom() < g.y);
    CHECK(g.w == pluginui::SeqRowsGrid::kWidth);
    CHECK(g.h == pluginui::SeqRowsGrid::kHeight);
    for (const char* id : settings) {
        INFO(id);
        const auto r = w.rectOf(id);
        REQUIRE(r);
        CHECK(r->y >= og.bottom());
        CHECK(r->bottom() < win.bottom());
        CHECK(r->right() < win.right());
    }
    CHECK(w.modulation().sequences().grid().state().live);
    CHECK(w.modulation().sequences().grid().state().length == h.patch().ornamentLength);

    // Percussion track: the table and the settings are off.
    w.pickGenerator(int(ymengine::Generator::Chipdrum));
    w.tick(0);
    CHECK(w.modulation().isOpen());
    CHECK_FALSE(w.modulation().sequences().grid().state().live);
    const plugin::NonParamState before = h.state;
    click(w, "seqxSync");
    click(w, "seqxMx0");
    CHECK(h.state.tracks[0].patch == before.tracks[0].patch);
}

TEST_CASE("Sequences: drawn rows, wheel, right click, menus; one undo step per gesture",
          "[ui][sequences][undo]") {
    TestHost h;
    h.state.tracks[0].patch.ornamentOn = true;
    MainWindow w(h);
    w.modulation().show();
    const ymengine::SeqRows& rows = h.patch().rows;

    // PER: top of the cell +127, bottom -127, middle 0; a stroke over four steps is a single undo step.
    const gemui::Rect p0 = cell(w, Row::Period, 0);
    drag(w, {{p0.x + 5, p0.y},
             {cell(w, Row::Period, 1).x + 5, p0.y + 10},
             {cell(w, Row::Period, 2).x + 5, p0.y + p0.h / 2},
             {cell(w, Row::Period, 3).x + 5, p0.bottom() - 1}});
    CHECK(rows.period[0] == 127);
    CHECK(rows.period[1] == 64);
    CHECK(rows.period[2] == 0);
    CHECK(rows.period[3] == -127);
    CHECK(w.modulation().sequences().grid().state().rows == rows);
    CHECK(h.editor.undo.undoSize() == 1);
    letter(w, U'z', true);
    CHECK(rows.empty());

    // NP: top of the cell 31; wheel, one undo step per notch; Shift: ten on PER.
    press(w, {cell(w, Row::Noise, 0).x + 5, cell(w, Row::Noise, 0).y});
    CHECK(rows.noise[0] == 31);
    const size_t undo = h.editor.undo.undoSize();
    wheel(w, centre(cell(w, Row::Noise, 2)), -1);
    wheel(w, centre(cell(w, Row::Noise, 2)), -1);
    CHECK(rows.noise[2] == 2);
    wheel(w, centre(cell(w, Row::Period, 5)), -1, true);
    CHECK(rows.period[5] == 10);
    wheel(w, centre(cell(w, Row::Period, 5)), 1);
    CHECK(rows.period[5] == 9);
    CHECK(h.editor.undo.undoSize() == undo + 4);
    letter(w, U'z', true);
    CHECK(rows.period[5] == 10);

    // FIX: a click toggles the step, the stroke paints the value of the press; right click: back to 0.
    drag(w, {centre(cell(w, Row::Fix, 0)), centre(cell(w, Row::Fix, 1)), centre(cell(w, Row::Fix, 2))});
    CHECK(rows.fix[0] == 1);
    CHECK(rows.fix[1] == 1);
    CHECK(rows.fix[2] == 1);
    press(w, centre(cell(w, Row::Fix, 1)));
    CHECK(rows.fix[1] == 0);
    press(w, centre(cell(w, Row::Fix, 0)), gemui::MouseButton::Right);
    CHECK(rows.fix[0] == 0);

    // MIX: the track's mix table and the eight channel modes (Y40).
    press(w, centre(cell(w, Row::Mix, 4)));
    CHECK(labels(w) == std::vector<std::string>{"Channel (track mixer)", "0 Off", "1 Tone", "2 Noise", "3 Tone+Noise",
                                                "4 Env", "5 Tone+Env", "6 Noise+Env", "7 Tone+Noise+Env"});
    const size_t beforeMix = h.editor.undo.undoSize();
    pick(w, 4);
    CHECK(rows.mix[4] == 4);  // mode 3 + 1
    CHECK(h.editor.undo.undoSize() == beforeMix + 1);
    // GEN: the generator of the note, then Chip, SID and Sync Buzzer.
    press(w, centre(cell(w, Row::Gen, 6)));
    CHECK(labels(w) == std::vector<std::string>{"Note's generator", "Chip Voice", "SID Voice", "Sync Buzzer"});
    pick(w, 3);
    CHECK(rows.gen[6] == uint8_t(ymengine::SeqGen::SyncBuzzer));

    // OUT: read only; tooltip of the pitch played by a C4 (period, note, deviation, frequency).
    const plugin::NonParamState held = h.state;
    press(w, centre(cell(w, Row::Out, 0)));
    press(w, centre(cell(w, Row::Out, 0)), gemui::MouseButton::Right);
    wheel(w, centre(cell(w, Row::Out, 0)), -1);
    CHECK(h.state.tracks[0].patch == held.tracks[0].patch);
    CHECK(w.modulation().sequences().grid().tipAt(centre(cell(w, Row::Out, 0))) ==
          "OUT step 1, C4 played: period 478 ($1DE), C4 -1c, 261.5 Hz");
    CHECK(w.modulation().sequences().grid().state().periods[0] == 478);
    // PER +10 at step 6: period 488, lower.
    CHECK(w.modulation().sequences().grid().state().periods[5] == 488);
    CHECK(w.modulation().sequences().grid().tipAt(centre(cell(w, Row::Out, 5))) ==
          "OUT step 6, C4 played: period 488 ($1E8), C4 -37c, 256.1 Hz");
    CHECK(w.modulation().sequences().grid().tipAt(centre(cell(w, Row::Out, 20))).ends_with("(after the table end)"));

    // Clear of the Modulation window empties the five rows with the table (one undo step).
    h.editExtras([](plugin::NonParamState& s) { s.tracks[0].patch.ornamentPitch[1] = 5; });
    w.tick(0);
    const size_t beforeClear = h.editor.undo.undoSize();
    click(w, "ornClear");
    CHECK(rows.empty());
    CHECK(h.patch().ornamentPitch[1] == 0);
    CHECK(h.editor.undo.undoSize() == beforeClear + 1);
    letter(w, U'z', true);
    CHECK(rows.gen[6] == uint8_t(ymengine::SeqGen::SyncBuzzer));
    CHECK(h.patch().ornamentPitch[1] == 5);
}


TEST_CASE("Sequences: right click on PER and NP, a slider set back by a right click, its value typed (D-272 to "
          "D-274)",
          "[ui][sequences][undo]") {
    TestHost h;
    h.state.tracks[0].patch.ornamentOn = true;
    h.state.tracks[0].patch.rows.noise[0] = 31;
    h.state.tracks[0].patch.rows.period[3] = 50;
    MainWindow w(h);
    w.modulation().show();
    const ymengine::SeqRows& rows = h.patch().rows;

    // NP: 96 px vertical slider below the cell, from 0 to 31; the value below it, two positions.
    const gemui::Rect n0 = cell(w, Row::Noise, 0);
    press(w, centre(n0), gemui::MouseButton::Right);
    REQUIRE(w.desktop().hasPopups());
    CHECK(rows.noise[0] == 31);
    const int sx = n0.x + 2 + 4 + 2 + 8, top = n0.bottom() + 2 + 8;
    press(w, {sx, top + 95});
    CHECK(rows.noise[0] == 0);
    press(w, {sx, top});
    CHECK(rows.noise[0] == 31);
    // Right click on the slider: 0, the sound of the note.
    press(w, {sx, top + 48}, gemui::MouseButton::Right);
    CHECK(rows.noise[0] == 0);
    CHECK(w.desktop().hasPopups());
    // Typed value, clamped to the row's range.
    const gemui::Point field{n0.x + 6 + 4, top + 96 + 2 + 8};
    press(w, field);
    key(w, gemui::Key::Backspace);
    letter(w, U'1');
    letter(w, U'2');
    key(w, gemui::Key::Enter);
    CHECK(rows.noise[0] == 12);
    press(w, field);
    key(w, gemui::Key::Backspace);
    key(w, gemui::Key::Backspace);
    letter(w, U'9');
    letter(w, U'9');
    key(w, gemui::Key::Enter);
    CHECK(rows.noise[0] == 31);
    key(w, gemui::Key::Escape);
    CHECK_FALSE(w.desktop().hasPopups());

    // PER: from -127 to 127, signed value typed without a mask; right click on the slider: 0.
    const gemui::Rect p3 = cell(w, Row::Period, 3);
    press(w, centre(p3), gemui::MouseButton::Right);
    REQUIRE(w.desktop().hasPopups());
    CHECK(rows.period[3] == 50);
    press(w, {p3.x + 6 + 4, p3.bottom() + 2 + 8 + 96 + 2 + 8});
    for (char32_t c : std::u32string(U"-40")) letter(w, c);
    key(w, gemui::Key::Enter);
    CHECK(rows.period[3] == -40);
    gemui::ValueField probe;
    probe.setMinChars(4);
    const int cw = std::max(20, int(probe.naturalWidth()));
    const int px = p3.x + 2 + 4 + (cw - 20) / 2 + 2 + 8;
    press(w, {px, p3.bottom() + 2 + 8 + 48}, gemui::MouseButton::Right);
    CHECK(rows.period[3] == 0);
    key(w, gemui::Key::Escape);
    // One undo step per slider opened, taken on the right click: the NP row came back to 31, nothing to undo there.
    CHECK(h.editor.undo.undoSize() == 1);
    letter(w, U'z', true);
    CHECK(rows.period[3] == 50);
    CHECK(rows.noise[0] == 31);
    CHECK(h.editor.undo.undoSize() == 0);

    // OUT: no slider.
    press(w, centre(cell(w, Row::Out, 0)), gemui::MouseButton::Right);
    CHECK_FALSE(w.desktop().hasPopups());
}
TEST_CASE("Sequences: fixed note, detune, hard sync, matrix, chained instrument, vibrato table",
          "[ui][sequences]") {
    TestHost h;
    h.state.tracks[0].patch.ornamentOn = true;
    h.state.tracks[0].patch.ornamentLength = 4;
    h.state.instruments[3] = plugin::captureInstrument("Bass", h.values, h.state, 0);
    MainWindow w(h);
    w.modulation().show();

    dragToMax(w, "seqxFix");
    CHECK(h.patch().fixedNote == 127);
    dragToMax(w, "seqxDetune");
    CHECK(h.patch().detuneCents == ymengine::kDetuneMaxCents);
    dragToMax(w, "seqxAnalog");
    CHECK(h.patch().analogCents == ymengine::kAnalogMaxCents);
    CHECK(h.editor.undo.undoSize() == 3);

    click(w, "seqxSync");
    CHECK(h.patch().hardSync);

    // Matrix: source button × 3 + target; by default square and timer for the three sources.
    CHECK(h.patch().pitchMatrix == ymengine::kPitchMatrixDefault);
    click(w, "seqxMx2");
    CHECK(h.patch().pitchMatrix == (ymengine::kPitchMatrixDefault | pitchBit(PitchSource::Table, PitchTarget::Buzzer)));
    click(w, "seqxMx3");
    CHECK_FALSE(h.patch().pitchMatrix & pitchBit(PitchSource::Slide, PitchTarget::Square));

    // Chaining: None and the stored instruments.
    click(w, "seqxLink");
    CHECK(labels(w) == std::vector<std::string>{"None", "04 Bass"});
    pick(w, 1);
    CHECK(h.patch().link == 4);
    click(w, "seqxLink");
    CHECK(menu(w).checked() == 1);
    pick(w, 0);
    CHECK(h.patch().link == 0);

    // Vibrato table: sine of 2 periods from -8 to +8 over the 4 steps of the table.
    click(w, "seqxVibWrite");
    CHECK(h.patch().rows.period[0] == 0);
    CHECK(h.patch().rows.period[1] == 0);  // sin(pi)
    CHECK(h.patch().rows.period[2] == 0);
    CHECK(h.patch().rows.period[4] == 0);  // beyond the length
    const auto one = SequencesPanel::vibratoTable(-8, 8, 1, 0, 4);
    CHECK(std::vector<int>(one.begin(), one.begin() + 5) == std::vector<int>{0, 8, 0, -8, 0});
    const auto ramp = SequencesPanel::vibratoTable(-8, 8, 1, 2, 4);
    CHECK(std::vector<int>(ramp.begin(), ramp.begin() + 4) == std::vector<int>{0, 4, 0, -8});
    const auto off = SequencesPanel::vibratoTable(10, 30, 1, 0, 4);
    CHECK(std::vector<int>(off.begin(), off.begin() + 4) == std::vector<int>{20, 30, 20, 10});
    dragToMax(w, "seqxVibCycles");
    dragToMax(w, "seqxVibMax");
    click(w, "seqxVibWrite");
    CHECK(h.patch().rows.period == SequencesPanel::vibratoTable(-8, ymengine::kPeriodDeltaMax, 15, 0, 4));
}

TEST_CASE("Modulation on a DMA track: what moves no sample greyed, with the reason (Indus, 26/10/2026, D-463)",
          "[ui][sequences][dma]") {
    TestHost h;
    h.values[size_t(paramIndex(plugin::Global::Computer))] = 1;  // the STE, for the DMA type in 16 ch
    const size_t gen = size_t(paramIndex(0, plugin::Track::Generator));
    h.values[gen] = float(int(ymengine::Generator::Dma));
    h.state.tracks[0].patch.ornamentOn = true;
    h.state.tracks[0].patch.ornamentLength = 4;
    MainWindow w(h);
    w.modulation().show();
    const pluginui::OrnGrid& table = w.modulation().grid();
    const pluginui::SeqRowsGrid& rows = w.modulation().sequences().grid();
    const SequencesPanel& seqx = w.modulation().sequences();

    // NSE: faded, no click, no N key, the reason in its tooltip.
    const std::string nse = "NSE: a DMA track plays a sample, without noise";
    CHECK(table.state().noiseOff == nse);
    CHECK(table.tipAt(centre(table.noiseRect(1))) == nse);
    press(w, centre(table.noiseRect(1)));
    CHECK(h.patch().ornamentNoise[1] == 0);
    CHECK(letter(w, U'n'));
    CHECK(h.patch().ornamentNoise[size_t(h.editor.ornCur)] == 0);
    CHECK(h.editor.undo.undoSize() == 0);

    // NP, MIX, GEN: faded, no click nor wheel nor menu, the reason in their tooltips; PER, FIX and OUT act.
    const std::array<std::string, pluginui::SeqRowsGrid::kRowCount> why = {
        "", "NP: a DMA track plays a sample, without noise",
        "MIX: a DMA track plays a sample, without the chip's mixer", "GEN: a DMA track keeps its sample", "", ""};
    CHECK(rows.state().off == why);
    for (Row r : {Row::Noise, Row::Mix, Row::Gen}) {
        CAPTURE(int(r));
        CHECK(rows.tipAt(centre(cell(w, r, 0))) == why[size_t(r)]);
        press(w, centre(cell(w, r, 0)));
        CHECK_FALSE(w.desktop().topPopup());
        press(w, centre(cell(w, r, 0)), gemui::MouseButton::Right);
        CHECK_FALSE(w.desktop().topPopup());
        wheel(w, centre(cell(w, r, 0)), -1);
    }
    CHECK(h.patch().rows == ymengine::SeqRows{});
    CHECK(h.editor.undo.undoSize() == 0);
    press(w, {centre(cell(w, Row::Period, 0)).x, cell(w, Row::Period, 0).y + 1});
    CHECK(h.patch().rows.period[0] > 0);
    press(w, centre(cell(w, Row::Fix, 1)));
    CHECK(h.patch().rows.fix[1] == 1);
    press(w, centre(table.volumeRect(0)));
    CHECK(h.patch().ornamentVolume[0] < 15);

    // Hard sync and the Sq and Bz columns of the matrix greyed with the reason; Tm, Link and the vibrato table act.
    const ymengine::TrackPatch before = h.patch();
    click(w, "seqxSync");
    for (int k : {0, 2, 3, 5, 6, 8}) click(w, "seqxMx" + std::to_string(k));
    CHECK(h.patch() == before);
    CHECK_FALSE(seqx.button("seqxSync")->enabled());
    CHECK(seqx.button("seqxSync")->tip() ==
          "Hard sync: a DMA track plays a sample, with no square or timer to restart");
    CHECK(seqx.button("seqxMx3")->tip() == "A DMA track has no square: the Tm column moves its sample");
    CHECK(seqx.button("seqxMx8")->tip() == "A DMA track has no envelope: the Tm column moves its sample");
    CHECK(seqx.button("seqxMx4")->tip() == "Slides moves the timer (SID, Sync Buzzer) and the DMA sample");
    click(w, "seqxMx4");
    CHECK_FALSE(h.patch().pitchMatrix & pitchBit(PitchSource::Slide, PitchTarget::Timer));
    for (const char* id : {"seqxLink", "seqxVibWrite"}) CHECK(seqx.button(id)->enabled());
    for (const char* id : {"seqxFix", "seqxDetune", "seqxAnalog"}) CHECK(seqx.slider(id)->enabled());

    // Chip Voice again: everything acts.
    h.values[gen] = float(int(ymengine::Generator::Chip));
    w.tick(0);
    CHECK(table.state().noiseOff.empty());
    CHECK(rows.state().off == std::array<std::string, pluginui::SeqRowsGrid::kRowCount>{});
    CHECK(seqx.button("seqxSync")->enabled());
    CHECK(seqx.button("seqxMx0")->enabled());
    CHECK(seqx.button("seqxMx3")->tip() == "Slides moves the square (tone period)");
    press(w, centre(table.noiseRect(1)));
    CHECK(h.patch().ornamentNoise[1] == 1);
    press(w, centre(cell(w, Row::Gen, 0)));
    CHECK(w.desktop().topPopup());
}

TEST_CASE("Sequences: shown again with the Modulation window of the session", "[ui][sequences]") {
    TestHost h;
    h.state.tracks[0].patch.ornamentOn = true;
    {
        MainWindow a(h);
        a.modulation().show();
    }
    {
        MainWindow b(h);
        CHECK(b.modulation().isOpen());
        CHECK(b.rectOf("seqxGrid"));
        key(b, gemui::Key::Escape);
        CHECK_FALSE(b.modulation().isOpen());
    }
    MainWindow c(h);
    CHECK_FALSE(c.rectOf("seqxGrid"));
}

TEST_CASE("Sequences: Fix, Detune, Analog and the vibrato table settings typed (sixth DAW test, item 77, D-275)",
          "[ui][sequences]") {
    TestHost h;
    h.state.tracks[0].patch.ornamentOn = true;
    h.state.tracks[0].patch.ornamentLength = 4;
    MainWindow w(h);
    w.modulation().show();
    auto type = [&](const std::string& id, std::string_view text) {
        click(w, id);
        for (int i = 0; i < 8; ++i) key(w, gemui::Key::Backspace);
        for (char c : text) letter(w, char32_t(c));
        key(w, gemui::Key::Enter);
    };
    type("seqxFixV", "A4");
    CHECK(h.patch().fixedNote == 69);
    type("seqxFixV", "Re2");
    CHECK(h.patch().fixedNote == 38);
    type("seqxDetuneV", "-30");
    CHECK(h.patch().detuneCents == -30);
    type("seqxAnalogV", "999");  // brought back into the range of the slider
    CHECK(h.patch().analogCents == ymengine::kAnalogMaxCents);
    CHECK(h.editor.undo.undoSize() == 4);

    // Vibrato table: settings of the window, written by Write PER.
    type("seqxVibMinV", "-4");
    type("seqxVibMaxV", "4");
    type("seqxVibCyclesV", "1");
    type("seqxVibRampV", "0");
    click(w, "seqxVibWrite");
    CHECK(h.patch().rows.period == SequencesPanel::vibratoTable(-4, 4, 1, 0, 4));
}

TEST_CASE("Track menus: an entry picked once the host focuses another track writes into neither track",
          "[ui][sequences]") {
    TestHost h;
    h.state.tracks[0].patch.ornamentOn = true;
    h.state.tracks[1].patch.ornamentOn = true;
    MainWindow w(h);
    const plugin::NonParamState before = h.state;
    const plugin::ParamValues values = h.values;
    // Picks entry k of the open menu once the host has focused track 2 (Focus Track automated, MIDI CC 84).
    auto pickOnTrack2 = [&](size_t k) {
        const gemui::MenuList* open = &menu(w);
        REQUIRE(k < open->items().size());
        h.values[size_t(paramIndex(plugin::Global::FocusTrack))] = 2;
        w.tick(10);
        REQUIRE(w.desktop().topPopup() == open);  // the menu stays open
        menu(w).items()[k].pick();
        w.desktop().closePopups();
    };
    SECTION("GEN cell of the sequences") {
        w.modulation().show();
        press(w, centre(cell(w, Row::Gen, 6)));
        pickOnTrack2(3);
    }
    SECTION("Generator of the main window") {
        click(w, "fxSel");
        pickOnTrack2(1);
    }
    SECTION("Chord of the Arpeggiator") {
        h.values[size_t(paramIndex(0, plugin::Track::ArpOn))] = 1;
        h.editExtras([](plugin::NonParamState& s) { s.tracks[0].arp.mode = ymengine::ArpMode::Table; });
        w.tick(5);
        w.arpeggiator().show();
        click(w, "chordSel");
        pickOnTrack2(3);
    }
    for (int t = 0; t < 2; ++t) {
        CHECK(h.state.tracks[size_t(t)].patch == before.tracks[size_t(t)].patch);
        CHECK(h.state.tracks[size_t(t)].arp.table == before.tracks[size_t(t)].arp.table);
        const auto g = size_t(paramIndex(t, plugin::Track::Generator));
        CHECK(h.values[g] == values[g]);
    }
}
