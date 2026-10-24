// STYM2149 — pluginui: Step Sequencer window (controls, transport, banks, keyboard, undo)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours taken from the prototype functions quoted in comments. The grid and the overview are in SeqGrid.cpp, the
// boxes (Euclid, track length, gate, pan, volume) in SeqPopups.cpp.
//
// Everything here runs on the message thread. The sequencer plays on the audio thread: transport and pattern changes
// reach it as Host::seqCommand, the patterns and settings in the published state (edit, editLane), and what it plays
// comes back in the engine status (refreshEngineStatus). Tempo, gate and swing are host parameters (MainWindow::set).
#include "pluginui/SeqWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "gemui/Alert.h"
#include "gemui/Font.h"
#include "pluginui/CmdBox.h"
#include "pluginui/IconData.h"
#include "pluginui/Layout.h"
#include "plugincore/Mapping.h"
#include "pluginui/MainWindow.h"
#include "ymengine/Pitch.h"
#include "ymseq/Commands.h"
#include "ymseq/Tables.h"
#include "ymseq/Timing.h"

namespace stym::pluginui {

using gemui::Box;
using gemui::Button;
using gemui::Key;
using gemui::MenuItem;
using gemui::Rect;
using plugin::Global;
using plugin::Track;
using ymengine::Generator;
namespace colors = gemui::colors;

namespace {

// Tools and clipboard (TOOLS, CLIPS).
constexpr std::array<const char*, 6> kToolTips = {
    "Move: drag notes or a selection without resizing them",
    "Draw: click to write notes, drag to move them",
    "Erase: click or drag over notes to remove them",
    "Split: click a note to cut it in two at that step",
    "Join: click a note to merge it with the next one on the same row",
    "Select: drag a box over the grid to select notes",
};
constexpr std::array<const char*, 3> kClipTips = {
    "Copy the note under the cursor, or the selection (Ctrl+C)",
    "Cut the note under the cursor, or the selection (Ctrl+X)",
    "Paste at the cursor (Ctrl+V)",
};
constexpr std::array<const char*, 6> kFxNames = {"Chip Voice", "SID Voice", "Digidrum", "Sync Buzzer", "Chipdrum", "DMA"};
constexpr int kPolyMax = ymseq::kPolyMax;
constexpr const char* kRtgHint =
    "RTG = ratchet: number of hits inside the step (1 = normal). CND = trigger condition: 1:2 / 2:2 = every other "
    "pass, 1:4 / 4:4 = one pass in four, 25 / 50 / 75 = chance in %.";

// Repeated spaces of a label collapsed to one, like the HTML rendering of a menu entry.
std::string collapse(std::string_view s) {
    std::string out;
    for (char c : s)
        if (c != ' ' || out.empty() || out.back() != ' ') out += c;
    return out;
}

std::string fmt2(double v) { return gemui::formatDecimal("%.2f", v); }

int mod(int a, int n) noexcept { return n > 0 ? ((a % n) + n) % n : 0; }

// Value entered in a field, brought to the step and the bounds of the slider (sliderApply); nullopt if unreadable.
std::optional<double> sliderValue(const gemui::Slider& s, const std::string& txt, double step) {
    size_t used = 0;
    const double raw = gemui::readDecimal(txt, &used);
    if (used == 0 || !std::isfinite(raw)) return std::nullopt;
    const double v = std::round(raw / step) * step;
    return std::clamp(v, s.min(), s.max());
}

void toolButton(Button& b, const gemui::Bitmap& icon, const char* tip) {
    b.setIcon(&icon);
    b.setPadding(5, 8);   // .tools .btn
    b.setMinWidth(30);
    b.setTip(tip);
}

void bigButton(Button& b, const gemui::Bitmap& icon) {
    b.setIcon(&icon);
    b.setPadding(7, 14);  // .tbar.big .btn, #seqMetro
    b.setMinWidth(46);
}

}  // namespace

ymseq::Row laneRow(SeqLane l) noexcept {
    using ymseq::Row;
    switch (l) {
    case SeqLane::Slt: return Row::Slt;
    case SeqLane::Oct: return Row::Oct;
    case SeqLane::Ton: return Row::Ton;
    case SeqLane::Ins: return Row::Ins;
    case SeqLane::Cmd: return Row::CmdArp;  // stands for the kinds of the CMD row in block and row operations (D-330)
    case SeqLane::Mix: return Row::Mix;
    case SeqLane::Fx: return Row::Fx;
    case SeqLane::Afl: return Row::CmdArpeggiator;  // Jxx since the twelfth DAW test (item 144)
    case SeqLane::Tie: return Row::Tie;
    case SeqLane::Cut: return Row::Cut;
    case SeqLane::Slide: return Row::Slide;
    case SeqLane::Slc: return Row::Slc;
    case SeqLane::Gat: return Row::Gat;
    case SeqLane::Pnr: return Row::Pnr;
    case SeqLane::Rtg: return Row::Rtg;
    case SeqLane::Cnd: return Row::Cnd;
    case SeqLane::Vol: return Row::Vol;
    case SeqLane::Vib: return Row::CmdVibrato;
    case SeqLane::Bpm: return Row::CmdTempo;  // the BPM row of the pattern; the tracks no longer hold Txx (item 150)
    case SeqLane::Env: return Row::CmdShape;  // with Rxx (envPartner)
    case SeqLane::Dly: return Row::CmdDelay;
    case SeqLane::NoteCut: return Row::CmdCut;
    case SeqLane::Lfo: return Row::CmdLfo;
    case SeqLane::Mod: return Row::CmdMod;
    case SeqLane::Smp: return Row::CmdOffset;
    case SeqLane::Tun: return Row::CmdFine;
    }
    return Row::Vol;
}

// ------------------------------------------------------------------------------------------- construction

SeqWindow::SeqWindow(MainWindow& main) : m_(main) {
    build();
    wire();
    place({double(kX), double(kY), double(kW), 675});
}

void SeqWindow::build() {
    // Prototype ids of the sliders and buttons that MIDI learn can learn (D-146).
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&tr_, "seqTr"}, {&stepRec_, "seqStepRec"}, {&clear_, "seqClear"}, {&euclid_, "euclidBtn"},
            {&rotL_, "rotL"}, {&rotR_, "rotR"}, {&trDn_, "trDn"}, {&trUp_, "trUp"}, {&trOctDn_, "trOctDn"},
            {&trOctUp_, "trOctUp"}, {&follow_, "seqFollow"}, {&plen_, "plenBtn"}, {&song_, "songMode"},
            {&steps_, "seqSteps"}, {&bpm_, "bpm"}, {&gate_, "gate"}, {&swing_, "swing"}, {&scaleRoot_, "scaleRoot"},
            {&scaleSel_, "scaleSel"}, {&scaleSnap_, "scaleSnap"}, {&sig_, "seqSig"}, {&reso_, "seqReso"},
            {&fig_, "seqFig"}, {&chord_, "seqChord"}, {&poly_, "seqPolySel"}, {&ghost_, "seqGhost"},
            {&over_, "seqOver"}, {&play_, "trPlay"}, {&rec_, "trRec"}, {&loop_, "trLoop"}, {&metro_, "seqMetro"},
            {&scroll_, "seqScroll"}})
        w->setId(id);
    static const std::array<const gemui::Bitmap*, 6> toolIcons = {&icons::kToolMove,  &icons::kToolDraw,
                                                                  &icons::kToolErase, &icons::kToolSplit,
                                                                  &icons::kToolJoin,  &icons::kToolSelect};
    static const std::array<const gemui::Bitmap*, 3> clipIcons = {&icons::kClipCopy, &icons::kClipCut,
                                                                  &icons::kClipPaste};
    for (size_t i = 0; i < tools_.size(); ++i) {
        toolButton(tools_[i], *toolIcons[i], kToolTips[i]);
        add(tools_[i]);
    }
    for (size_t i = 0; i < clips_.size(); ++i) {
        toolButton(clips_[i], *clipIcons[i], kClipTips[i]);
        add(clips_[i]);
    }
    const std::pair<Button*, const char*> row1[] = {
        {&stepRec_, "Step input: play a note to write it at the cursor"},
        {&clear_, "Clear the current track"},
        {&euclid_, "Euclidean rhythm generator"},
        {&rotL_, "Rotate pattern left"},
        {&rotR_, "Rotate pattern right"},
        {&trDn_, "Transpose down 1 semitone"},
        {&trUp_, "Transpose up 1 semitone"},
        {&trOctDn_, "Transpose down 1 octave"},
        {&trOctUp_, "Transpose up 1 octave"},
        {&follow_, "Scroll the view with the playhead"},
        {&plen_, "Track length (polymeter)"},
        {&song_, "Open the Song window and chain patterns"},
    };
    for (const auto& [b, tip] : row1) {
        b->setTip(tip);
        add(*b);
    }

    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{&labSteps_, &steps_, &stepsv_, &labBpm_, &bpm_,
                                                                  &bpmv_, &labGate_, &gateAll_, &gate_, &gatev_,
                                                                  &gatePct_, &labSwing_, &swing_, &swingv_,
                                                                  &swingPct_})
        add(*w);
    steps_.setRange(1, ymseq::kSteps, 1);
    steps_.setTip("Pattern length, up to 256 steps");
    stepsv_.setMask("999");
    stepsv_.setMinChars(3);
    bpm_.setRange(60, 240, 0.01);
    bpm_.setTip("Tempo in beats per minute");
    bpmv_.setMask("999.99");
    bpmv_.setMinChars(6);
    gate_.setRange(5, 95, 1);
    gate_.setTip("Note length as a share of the step");
    // Button of the slider row: 16 px high at most (11 px text, margins 0 / 4 px), 22 px wide at least like the .mstrip
    // buttons of the Mixer.
    gateAll_.setPx(11);
    gateAll_.setPadding(0, 4);
    gateAll_.setMinWidth(22);
    gateAll_.setTip("One gate for all tracks: the Gate slider then sets it");
    gatev_.setMask("99");
    gatev_.setMinChars(2);
    swing_.setRange(50, 75, 1);
    swing_.setTip("Shift every other step: 50% straight, 66% triplet shuffle");
    swingv_.setMask("99");
    swingv_.setMinChars(2);

    for (gemui::Widget* w : std::initializer_list<gemui::Widget*>{
             &labScale_, &scaleRoot_, &scaleSel_, &scaleSnap_, &labSig_, &sig_, &labReso_, &reso_, &labNote_, &fig_,
             &labChord_, &chord_, &labPoly_, &poly_, &ghost_, &over_})
        add(*w);
    sig_.setTip("Bar length: accents and metronome follow it");
    reso_.setTip("Value of one step");
    ghost_.setTip("Show the other channels behind the grid");
    over_.setTip("Overview strip: one line per channel");

    add(grid_);
    add(tr_);
    // A scroll bar against the right edge of the window (eighth DAW test, item 93, D-294): an arrow, one semitone; the
    // track, one octave; the thumb as long as the 13 rows shown.
    tr_.setRange(ymseq::kPlayLow, 108, 1);
    tr_.setPage(SeqGrid::kRows, 12);
    tr_.setTip("Scroll the notes B0-C9: an arrow by 1 semitone, a click beside the thumb by 1 octave, "
               "the wheel by 1 semitone");
    add(scroll_);
    add(scrollv_);
    scroll_.setTip("Scroll through a pattern longer than the grid");

    add(sep_);
    for (Button* b : {&play_, &rec_, &loop_, &start_, &prev_, &next_}) add(*b);
    add(tbarSep_);
    bigButton(play_, icons::kBigPlay);
    bigButton(rec_, icons::kBigRec);
    rec_.setTip("Record while playing");
    bigButton(loop_, icons::kBigLoop);
    loop_.setTip("Loop");
    bigButton(start_, icons::kBigStart);
    start_.setTip("Play from start");
    bigButton(prev_, icons::kBigPrev);
    prev_.setTip("Previous song entry");
    bigButton(next_, icons::kBigNext);
    next_.setTip("Next song entry");
    add(metro_);
    bigButton(metro_, icons::kBigMetro);
    metro_.setTip("Metronome: click on every beat, bell on the first beat of the bar; right click: its volume");
    add(labBank_);
    add(labPattern_);
    for (int i = 0; i < 4; ++i) {
        Button& b = banks_[size_t(i)];
        const std::string n(1, char('A' + i));
        b.setText(n);
        b.setPx(15);
        b.setPadding(6, 11);
        b.setMinWidth(30);
        b.setTip("Bank " + n + ": eight patterns (" + n + "1 to " + n + "8). Shift+" + std::to_string(i + 1) +
                 " from the keyboard.");
        add(b);
    }
    for (int i = 0; i < 8; ++i) {
        Button& b = pats_[size_t(i)];
        b.setText(std::to_string(i + 1));
        b.setPx(15);
        b.setPadding(6, 11);
        b.setMinWidth(30);
        b.setTip("Pattern " + std::to_string(i + 1) + " of the current bank. Key " + std::to_string(i + 1) +
                 ".\nRight click: copy, cut, paste or transpose.");
        add(b);
    }
    add(overMap_);
    add(overHint_);
    overHint_.setColor(colors::kHint);
    add(hint_);
}

void SeqWindow::wire() {
    onClose = [this] { m_.syncWindowButtons(); };
    for (int i = 0; i < int(tools_.size()); ++i) tools_[size_t(i)].onClick = [this, i] { setTool(i); };
    for (int i = 0; i < int(clips_.size()); ++i)
        clips_[size_t(i)].onClick = [this, i] {
            editClip(i);
            refreshClips();
        };
    stepRec_.onClick = [this] {
        ses().stepRec = !ses().stepRec;
        refreshControls();
    };
    clear_.onClick = [this] { clearLaneAsk(); };
    euclid_.onClick = [this] { openEuclid(); };
    rotL_.onClick = [this] {
        pushUndo();
        rotateLane(-1);
    };
    rotR_.onClick = [this] {
        pushUndo();
        rotateLane(1);
    };
    const std::pair<Button*, int> tr[] = {{&trDn_, -1}, {&trUp_, 1}, {&trOctDn_, -12}, {&trOctUp_, 12}};
    for (const auto& [b, n] : tr)
        b->onClick = [this, semis = n] {
            pushUndo();
            transposeLane(semis);
        };
    follow_.onClick = [this] {
        ses().follow = !ses().follow;
        refreshControls();
    };
    plen_.onClick = [this] { openTrackLength(); };
    song_.onClick = [this] { m_.openSong(); };

    // Pattern length: state that cannot be automated (the held slider is not read back).
    steps_.onGestureStart = [this] { m_.dragging_ = &steps_; };
    steps_.onGestureEnd = [this] {
        m_.dragging_ = nullptr;
        refresh();
    };
    steps_.onChange = [this](double v) {
        const int n = int(v);
        edit([n](plugin::NonParamState& s) { s.seq.setSteps(n); });
    };
    stepsv_.onCommit = [this](const std::string& t) {
        const auto v = sliderValue(steps_, t, 1);
        if (!v) return false;
        const int n = int(*v);
        edit([n](plugin::NonParamState& s) { s.seq.setSteps(n); });
        return true;
    };
    m_.bindSlider(bpm_, [this](double v) { m_.set(Global::FreeTempo, float(v)); });
    bpmv_.onCommit = [this](const std::string& t) {
        const auto v = sliderValue(bpm_, t, 0.01);
        if (!v) return false;
        m_.set(Global::FreeTempo, float(*v));
        return true;
    };
    // Gate All on: the slider sets the global gate, otherwise that of the track shown (D-134).
    auto setGate = [this](double v) {
        if (m_.iv(Global::GateAll)) m_.set(Global::Gate, float(v));
        else m_.set(Track::Gate, float(v));
    };
    m_.bindSlider(gate_, setGate);
    gatev_.onCommit = [this, setGate](const std::string& t) {
        const auto v = sliderValue(gate_, t, 1);
        if (!v) return false;
        setGate(*v);
        return true;
    };
    gateAll_.onClick = [this] { m_.set(Global::GateAll, m_.iv(Global::GateAll) ? 0.0f : 1.0f); };
    m_.bindSlider(swing_, [this](double v) { m_.set(Global::Swing, float(v)); });
    swingv_.onCommit = [this](const std::string& t) {
        const auto v = sliderValue(swing_, t, 1);
        if (!v) return false;
        m_.set(Global::Swing, float(*v));
        return true;
    };

    scaleRoot_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < 12; ++i) {
            MenuItem m;
            m.label = noteName(i);
            m.pick = [this, i] { edit([i](plugin::NonParamState& s) { s.global.scaleRoot = uint8_t(i); }); };
            items.push_back(std::move(m));
        }
        popup(scaleRoot_, std::move(items), scaleRoot());
    };
    scaleSel_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < ymseq::kScaleCount; ++i) {
            MenuItem m;
            m.label = std::string(ymseq::kScales[size_t(i)].name);
            m.pick = [this, i] {
                edit([i](plugin::NonParamState& s) { s.global.scaleMask = ymseq::scaleMask(i); });
            };
            items.push_back(std::move(m));
        }
        popup(scaleSel_, std::move(items), scale());
    };
    scaleSnap_.onClick = [this] {
        if (scale() <= 0) return;
        ses().snap = !ses().snap;
        refresh();
    };
    sig_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = 0; i < ymseq::kSignatureCount; ++i) {
            MenuItem m;
            m.label = std::string(ymseq::kSignatures[size_t(i)].name);
            m.pick = [this, i] {
                edit([i](plugin::NonParamState& s) {
                    s.global.sigNum = ymseq::kSignatures[size_t(i)].numerator;
                    s.global.sigDen = ymseq::kSignatures[size_t(i)].denominator;
                });
            };
            items.push_back(std::move(m));
        }
        popup(sig_, std::move(items), signature());
    };
    reso_.onClick = [this] { resolutionMenu(); };
    fig_.onClick = [this] { popup(fig_, figItems(), ses().figure); };
    chord_.onClick = [this] {
        std::vector<MenuItem> items;
        for (int i = -1; i < int(ymengine::kChords.size()); ++i) {
            MenuItem m;
            m.label = i < 0 ? "Off" : std::string(ymengine::kChords[size_t(i)].name);
            m.pick = [this, i] {
                ses().chord = i;
                refreshControls();
            };
            items.push_back(std::move(m));
        }
        popup(chord_, std::move(items), ses().chord + 1);
    };
    poly_.onClick = [this] {
        // pickVoicing: mono, the voices stay but muted and the entry chord goes back to Off.
        std::vector<MenuItem> items;
        for (int i = 0; i < 2; ++i) {
            MenuItem m;
            m.label = i ? "Monophonic" : "Polyphonic";
            m.pick = [this, i] {
                if (i) ses().chord = -1;
                m_.set(Track::Voicing, float(i));
            };
            items.push_back(std::move(m));
        }
        popup(poly_, std::move(items), mono() ? 1 : 0);
    };
    ghost_.onClick = [this] {
        ses().ghost = !ses().ghost;
        refresh();
    };
    over_.onClick = [this] {
        ses().overview = !ses().overview;
        refresh();
    };

    tr_.onChange = [this](double v) { setViewBase(int(v)); };
    scroll_.onChange = [this](double v) { setViewStart(int(v)); };

    play_.onClick = [this] { togglePlay(); };
    rec_.onClick = [this] { toggleRecord(); };
    loop_.onClick = [this] { toggleLoop(); };
    start_.onClick = [this] { transportStart(); };
    prev_.onClick = [this] { m_.songStep(-1); };
    next_.onClick = [this] { m_.songStep(1); };
    metro_.onClick = [this] { toggleMetro(); };
    metro_.onContext = [this](gemui::Point) {
        // Volume of the metronome (seventh DAW test, item 87, D-288): the slider of the Modulation window's cells,
        // 0 to 100 %, back to 100 on a right click on it; each move applies.
        gemui::Desktop& d = m_.desktop();
        d.closePopups();
        SliderPopupSpec spec;
        spec.min = 0;
        spec.max = 100;
        spec.value = st().global.metronomeLevel;
        spec.reset = 100;
        spec.set = [this](int v) { edit([v](plugin::NonParamState& s) { s.global.metronomeLevel = uint8_t(v); }); };
        d.openPopup(makeSliderPopup(std::move(spec), metro_.bounds(), d.width(), d.height()));
    };
    for (int i = 0; i < 4; ++i)
        banks_[size_t(i)].onClick = [this, i] {
            edit([i](plugin::NonParamState& s) { s.seq.viewBank = i; });
            selectPattern(proj().bankLast[size_t(i)]);
            refreshPatterns();
        };
    for (int i = 0; i < 8; ++i) {
        pats_[size_t(i)].onClick = [this, i] { selectPattern(proj().viewBank * ymseq::kBankSize + i); };
        pats_[size_t(i)].onContext = [this, i](gemui::Point) { patternMenu(i); };
    }
}

// ------------------------------------------------------------------------------------------- reading

const plugin::NonParamState& SeqWindow::st() const noexcept { return m_.extras_; }
const ymseq::Project& SeqWindow::proj() const noexcept { return m_.extras_.seq; }
const ymseq::Lane& SeqWindow::lane() const noexcept { return proj().cur().lane(cur()); }
gemui::Desktop& SeqWindow::desk() const noexcept { return m_.desk_; }
EditorSession& SeqWindow::ses() const noexcept { return m_.host_.session(); }
int SeqWindow::cur() const noexcept { return m_.cur_; }
SeqWindow::StepTarget SeqWindow::stepTarget() const noexcept { return {cur(), proj().current}; }
bool SeqWindow::stillOn(StepTarget t) const noexcept { return cur() == t.track && proj().current == t.pattern; }
int SeqWindow::trackOct() const noexcept { return proj().tracks[size_t(cur())].octave; }
int SeqWindow::length() const noexcept { return proj().trackLength(cur()); }
int SeqWindow::steps() const noexcept { return proj().steps(); }
int SeqWindow::generator() const noexcept { return m_.generator(); }
bool SeqWindow::drum() const noexcept { return m_.drumTrack(); }
bool SeqWindow::chipdrum() const noexcept { return generator() == int(Generator::Chipdrum); }
bool SeqWindow::mode3() const noexcept { return m_.mode3(); }
bool SeqWindow::mono() const noexcept { return m_.iv(Track::Voicing) == 1; }
bool SeqWindow::chordsPlay() const noexcept { return !mono() && !mode3(); }
bool SeqWindow::arpOn() const noexcept { return m_.iv(Track::ArpOn) != 0; }
bool SeqWindow::insShown() const noexcept {
    // Without instruments, the grid stays that of the prototype; an INS cell still written stays visible so that it can
    // be cleared.
    if (drum()) return false;
    const auto& ins = st().instruments;
    if (std::any_of(ins.begin(), ins.end(), [](const plugin::Instrument& i) { return i.used; })) return true;
    const ymseq::Lane& l = lane();
    return std::any_of(l.ins.begin(), l.ins.end(), [](uint8_t v) { return v != 0; });
}
bool SeqWindow::cmdShown() const {
    // Preference "Step command rows" (Resolution menu), on by default since the ninth DAW test (item 117); off, a
    // command of the row still written stays visible so that it can be cleared.
    if (cmdPreference()) return true;
    const ymseq::Lane& l = lane();
    for (int s = 0; s < ymseq::kSteps; ++s)
        if (ymseq::cmdCellText(l, s) != "-") return true;
    return false;
}
bool SeqWindow::written(ymengine::SeqCmd k) const {
    const ymseq::Lane& l = lane();
    for (int s = 0; s < ymseq::kSteps; ++s)
        if (l.cmdAt(k, s)) return true;
    return false;
}
bool SeqWindow::ownRowShown(ymengine::SeqCmd k) const {
    // The rows of their own follow the preference of the CMD row, of which they were part until the eleventh DAW test
    // (off: the prototype's grid); a command written keeps its row so that it can be cleared.
    return cmdPreference() || written(k);
}
bool SeqWindow::envPlays() const {
    // Env entries of Source (volume on the envelope), Sync Buzzer, or a step that asks for it (an Env entry of SRC,
    // Sync Buzzer in GEN).
    if (drum()) return false;
    if (m_.iv(Track::VolMode) == 1 || generator() == int(Generator::SyncBuzzer)) return true;
    const ymseq::Lane& l = lane();
    for (int s = 0; s < ymseq::kSteps; ++s)
        if (l.mix[size_t(s)] >= ymengine::kSeqMixEnv || l.fx[size_t(s)] == 3) return true;  // GEN 3: Sync Buzzer
    return false;
}
// The rows of the switches and of the envelope: with the preference where they can act, and wherever a value of
// theirs is written (crossed if it cannot act, item 145).
bool SeqWindow::vibShown() const {
    using ymengine::SeqCmd;
    return !drum() && ((m_.iv(Track::VibOn) != 0 && cmdPreference()) || written(SeqCmd::Vibrato));
}
bool SeqWindow::lfoShown() const {
    const ymengine::TrackLfo& lfo = st().tracks[size_t(cur())].patch.lfo;
    return !drum() && ((lfo.on && cmdPreference()) || written(ymengine::SeqCmd::Lfo));
}
bool SeqWindow::modShown() const {
    return !drum() &&
           ((st().tracks[size_t(cur())].patch.ornamentOn && cmdPreference()) || written(ymengine::SeqCmd::Mod));
}
bool SeqWindow::arpShown() const {
    // The ARP row of the prototype, without the preference.
    return !drum() && (arpOn() || written(ymengine::SeqCmd::Arpeggiator));
}
bool SeqWindow::envShown() const {
    using ymengine::SeqCmd;
    return !drum() && ((envPlays() && cmdPreference()) || written(SeqCmd::Shape) || written(SeqCmd::Retrig));
}
bool SeqWindow::smpShown() const {
    return (generator() == int(Generator::Dma) && cmdPreference()) || written(ymengine::SeqCmd::Offset);
}
bool SeqWindow::tunShown() const { return (!drum() && cmdPreference()) || written(ymengine::SeqCmd::Fine); }
bool SeqWindow::bpmShown() const {
    // The BPM row of the pattern (item 150), in the standalone application (D-342).
    if (!m_.host_.standalone()) return false;
    if (cmdPreference()) return true;
    const auto& bpm = proj().cur().bpm;
    return std::any_of(bpm.begin(), bpm.end(), [](uint8_t v) { return v != 0; });
}
std::string SeqWindow::cantAct(SeqLane row, int s) const {
    // Twelfth DAW test, item 145: a value written where it cannot act. The rows that stay hidden where they cannot
    // act (drums) show it all the same once a value is written.
    using ymengine::SeqCmd;
    const ymseq::Lane& l = lane();
    const size_t S = size_t(s);
    const auto onTrack = [&](SeqCmd k) { return l.cmdAt(k, s) != 0; };
    const bool starts = l.pat[S] >= 0 || std::any_of(l.vpat.begin(), l.vpat.end(), [S](const auto& v) {
                            return v[S] >= 0;
                        });
    switch (row) {
    case SeqLane::Afl:
        if (!onTrack(SeqCmd::Arpeggiator)) return {};
        if (drum()) return "A drum track has no arpeggiator";
        return arpOn() ? "" : "The arpeggiator of this track is off (Arpeggiator window)";
    case SeqLane::Vib:
        if (!onTrack(SeqCmd::Vibrato)) return {};
        if (drum()) return "A drum track has no vibrato";
        return m_.iv(Track::VibOn) ? "" : "The vibrato of this track is off (VIBRATO frame)";
    case SeqLane::Lfo: {
        if (!onTrack(SeqCmd::Lfo)) return {};
        if (drum()) return "A drum track has no LFO";
        const ymengine::TrackLfo& lfo = st().tracks[size_t(cur())].patch.lfo;
        return lfo.on && lfo.depth > 0 ? "" : "The LFO of this track is off (Modulation window)";
    }
    case SeqLane::Mod:
        if (!onTrack(SeqCmd::Mod)) return {};
        if (drum()) return "A drum track has no modulation table";
        return st().tracks[size_t(cur())].patch.ornamentOn ? ""
                                                           : "The modulation table of this track is off (Modulation "
                                                             "window)";
    case SeqLane::Env:
        if (!onTrack(SeqCmd::Shape) && !onTrack(SeqCmd::Retrig)) return {};
        return envPlays() ? "" : "The envelope does not play on this track (Source Env, Sync Buzzer, an Env entry of "
                                 "SRC or Sync Buzzer in GEN)";
    case SeqLane::Smp:
        if (!onTrack(SeqCmd::Offset)) return {};
        if (generator() != int(Generator::Dma)) return "Not a DMA track: there is no sample to start further in";
        return starts ? "" : "No note starts on this step";
    case SeqLane::Tun:
        return onTrack(SeqCmd::Fine) && drum() ? "A drum track keeps its tuning (OCT and TON rows)" : "";
    case SeqLane::Dly:
        return onTrack(SeqCmd::Delay) && !starts && !l.rtg[S] ? "No note starts on this step" : "";
    case SeqLane::Slc: {
        if (!l.slc[S] || drum()) return {};
        ymseq::Lane copy = l;
        const ymseq::LaneEditor ed(copy, context());
        const bool act =
            (l.pat[S] >= 0 && l.slide[S]) || (l.pat[S] < 0 && (l.slide[S] || l.tie[S]) && ed.slideRunsAt(s));
        return act ? "" : "No slide runs on this step (SLD row)";
    }
    case SeqLane::Bpm: {
        if (!proj().cur().bpm[S]) return {};
        const ymengine::GlobalParams& g = st().global;
        if (g.timing == ymengine::SeqTiming::Ticks) return "Ticks timing: a line lasts its ticks per line";
        return g.seqClock == ymengine::SeqClock::Internal ? "" : "The sequencer follows an external clock";
    }
    default: return {};
    }
}
int SeqWindow::lineTicks() const noexcept {
    const ymengine::GlobalParams& g = st().global;
    if (g.timing == ymengine::SeqTiming::Ticks) return std::max(1, int(g.speed));
    const double tempo = std::max(1.0, double(m_.pv(Global::FreeTempo)));
    const double lineMs = 60000.0 / (tempo * ymseq::kResolutions[size_t(resolution())].stepsPerBeat);
    return std::max(1, int(lineMs / m_.msPerTick()));
}
bool SeqWindow::cmdPreference() const { return m_.host_.preference("seqCmdRow", "1") == "1"; }
int SeqWindow::scale() const noexcept {
    // Chosen scale: the one whose mask is that of the state (the 25 scales have distinct masks).
    const uint16_t m = st().global.scaleMask;
    if (!m) return 0;
    for (int i = 1; i < ymseq::kScaleCount; ++i)
        if (ymseq::scaleMask(i) == m) return i;
    return 0;
}
int SeqWindow::scaleRoot() const noexcept { return std::clamp<int>(st().global.scaleRoot, 0, 11); }
int SeqWindow::signature() const noexcept {
    for (int i = 0; i < ymseq::kSignatureCount; ++i)
        if (ymseq::kSignatures[size_t(i)].numerator == st().global.sigNum &&
            ymseq::kSignatures[size_t(i)].denominator == st().global.sigDen)
            return i;
    return ymseq::kDefaultSignature;
}
int SeqWindow::resolution() const noexcept {
    return std::clamp<int>(st().global.resolution, 0, ymseq::kResolutionCount - 1);
}
int SeqWindow::viewSemi() const noexcept { return drum() ? 0 : ses().viewAbsBase - (trackOct() + 1) * 12; }
int SeqWindow::beatSteps() const noexcept { return ymseq::beatSteps(signature(), resolution()); }
int SeqWindow::barSteps() const noexcept { return ymseq::barSteps(signature(), resolution()); }
int SeqWindow::figSteps() const noexcept { return ymseq::figureSteps(ses().figure, resolution()); }
bool SeqWindow::latin() const { return m_.host_.preference("noteNames", "0") == "1"; }
std::string SeqWindow::noteName(int pc) const {
    return std::string(ymseq::noteName(pc, latin() ? ymseq::NoteNames::Latin : ymseq::NoteNames::English));
}
std::string SeqWindow::absName(int absNote) const {
    return ymseq::absNoteName(absNote, latin() ? ymseq::NoteNames::Latin : ymseq::NoteNames::English);
}

std::string SeqWindow::fxName(int track) const {
    return kFxNames[size_t(std::clamp(int(plugin::effectiveGenerator(m_.values_, track)), 0, int(kFxNames.size()) - 1))];
}

ymseq::EditContext SeqWindow::context() const {
    ymseq::EditContext c;
    c.length = length();
    c.trackOctave = trackOct();
    c.drum = drum();
    c.mono = mono();
    c.threeVoice = mode3();
    c.figure = ses().figure;
    c.resolution = resolution();
    c.chord = ses().chord;
    c.scale = scale();
    c.scaleRoot = scaleRoot();
    c.snap = ses().snap;
    c.digidrumSlot = st().tracks[size_t(cur())].patch.digidrumSlot;
    return c;
}

// ------------------------------------------------------------------------------------------- writing

// The host applies fn to a copy of the state and publishes it to the audio thread; pullExtras reads it back and
// refreshes the windows.
void SeqWindow::edit(const std::function<void(plugin::NonParamState&)>& fn) {
    m_.host_.editExtras(fn);
    m_.pullExtras();
}

// ------------------------------------------------------------------------------------------- Ticks rate

void SeqWindow::resolutionMenu() {
    // Resolutions of the prototype (BPM rate), then, under a rule, the Ticks rate of STYM2149 (D-149): a line lasts
    // Speed replay ticks plus those of the groove; Speed and Groove are set in either rate.
    const ymengine::GlobalParams& g = st().global;
    const bool ticks = g.timing == ymengine::SeqTiming::Ticks;
    std::vector<MenuItem> items;
    int checked = -1;
    for (int i = 0; i < ymseq::kResolutionCount; ++i) {
        if (i == ymseq::kResolutionSep) items.push_back(MenuItem::separatorItem());
        if (!ticks && i == resolution()) checked = int(items.size());
        MenuItem m;
        m.label = collapse(ymseq::kResolutions[size_t(i)].name);
        m.pick = [this, i] {
            edit([i](plugin::NonParamState& s) {
                s.global.resolution = uint8_t(i);
                s.global.timing = ymengine::SeqTiming::Bpm;
            });
        };
        items.push_back(std::move(m));
    }
    items.push_back(MenuItem::separatorItem());
    if (ticks) checked = int(items.size());
    MenuItem t;
    t.label = "Ticks: Speed ticks per line";
    t.pick = [this] { edit([](plugin::NonParamState& s) { s.global.timing = ymengine::SeqTiming::Ticks; }); };
    items.push_back(std::move(t));
    MenuItem sp;
    sp.label = "Speed: " + std::to_string(g.speed) + " ticks...";
    sp.pick = [this] { askSpeed(); };
    items.push_back(std::move(sp));
    MenuItem gr;
    gr.label = "Groove: " + ymseq::grooveText(g.groove, g.grooveLength) + "...";
    gr.pick = [this] { askGroove(); };
    items.push_back(std::move(gr));
    // Rows of the step commands (D-150; CMD, BPM, VIB, ENV, DLY and CUT since the eleventh DAW test), preference shared
    // by all projects.
    items.push_back(MenuItem::separatorItem());
    MenuItem cmd;
    cmd.label = "Step command rows";
    cmd.subMarks = true;
    cmd.subCur = cmdPreference() ? 0 : 1;
    for (int i = 0; i < 2; ++i) {
        MenuItem e;
        e.label = i ? "Off" : "On";
        e.pick = [this, i] {
            m_.host_.setPreference("seqCmdRow", i ? "0" : "1");
            refresh();
        };
        cmd.sub.push_back(std::move(e));
    }
    items.push_back(std::move(cmd));
    popup(reso_, std::move(items), checked);
}

void SeqWindow::prompt(const std::string& message, const std::string& text, int chars,
                       std::function<void(const std::string&)> ok) {
    gemui::Alert::Spec spec;
    spec.lines = {{message}};
    spec.promptButtons();
    spec.inputText = text;
    spec.inputChars = chars;
    gemui::Alert::open(m_.desk_, std::move(spec), [ok = std::move(ok)](int button, const std::string& s) {
        if (button == gemui::Alert::kPromptOk) ok(s);
    });
}

void SeqWindow::askSpeed() {
    prompt("Speed: ticks per line (1-" + std::to_string(ymengine::kSpeedMax) + ")", std::to_string(st().global.speed),
           2, [this](const std::string& s) {
               const auto v = ymseq::parseSpeed(s);
               if (!v) return;
               edit([v](plugin::NonParamState& st) { st.global.speed = uint8_t(*v); });
           });
}

void SeqWindow::askGroove() {
    const ymengine::GlobalParams& g = st().global;
    prompt("Groove: ticks added to each line, up to 16 values (0-15)", ymseq::grooveText(g.groove, g.grooveLength),
           47, [this](const std::string& s) {
               const auto v = ymseq::parseGroove(s);
               if (!v) return;
               edit([&v](plugin::NonParamState& st) {
                   st.global.groove = v->values;
                   st.global.grooveLength = uint8_t(v->length);
               });
           });
}

void SeqWindow::askCmd(int s) {
    // Box of the commands of the step (tenth DAW test, items 121, 122 and 130: D-329, D-330, D-338). An absent command
    // shows a value to start from: restart at tick 0, the song's ticks per line, sync mark 1; volume slide down a
    // quarter step per tick, buzzer and noise slides up one unit, offset 0, fine tune 0 cents. OK writes the kinds of
    // the box only (eleventh DAW test: the VIB, BPM, ENV, DLY and CUT rows have the others, D-341, D-342, D-347 to
    // D-349; Xyz and Pxy are retired, D-340, D-344).
    using ymengine::SeqCmd;
    const ymengine::GlobalParams& g = st().global;
    const auto start = [&g](SeqCmd k) {
        switch (k) {
        case SeqCmd::Speed: return int(g.speed);
        case SeqCmd::Sync:
        case SeqCmd::VolSlide: return 1;
        case SeqCmd::EnvSlide:
        case SeqCmd::NoiseSlide: return 0x10;
        case SeqCmd::Fine: return ymengine::kFineCmdCenter;
        default: return 0;
        }
    };
    CmdBox::Spec spec;
    spec.step = s;
    for (int i = 1; i <= ymengine::kSeqCmdKinds; ++i) {
        const auto k = SeqCmd(i);
        const uint16_t c = lane().cmdAt(k, s);
        spec.entries[size_t(i - 1)] = {c != 0, c ? int(ymengine::seqCmdParam(c)) : start(k)};
    }
    spec.ok = [this, s, at = stepTarget()](const CmdBox::Entries& entries) {
        if (!stillOn(at)) return;  // the box was for that step of another track or pattern
        editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            for (const SeqCmd k : CmdBox::kOrder) {
                const CmdBox::Entry& e = entries[size_t(int(k) - 1)];
                if (e.on)
                    l.setCmd(s, ymengine::seqCmd(k, uint8_t(e.value)));
                else
                    l.clearCmd(k, s);
            }
            return true;
        });
    };
    CmdBox::open(m_.desk_, std::move(spec));
}

// Every change of the track's notes and rows goes through here. The edit works on the current pattern of a copy of the
// state (Pattern::edit copies a shared track before writing, compact() drops a track left empty) and is published
// through the host. Undo: when a grid gesture has armed a step (armUndo), the pattern is recorded before the edit and
// pushed only if the edit changed it; the arm then ends, so the rest of the gesture adds nothing. An arm that no change
// used stays until the next one; the callers that are not grid gestures push their own step first (pushUndo), and
// UndoHistory then drops the armed copy as identical.
bool SeqWindow::editLane(const std::function<bool(ymseq::LaneEditor&, ymseq::Lane&)>& fn) {
    const int t = cur();
    const ymseq::EditContext ctx = context();
    std::optional<UndoEntry> armed;
    if (undoArmed_) armed = patternEntry(proj().current);
    bool result = false;
    edit([&](plugin::NonParamState& s) {
        ymseq::Pattern& p = s.seq.cur();
        ymseq::Lane& l = p.edit(t);
        ymseq::LaneEditor ed(l, ctx);
        result = fn(ed, l);
        p.compact();
    });
    if (armed && !(*armed == patternEntry(proj().current))) {  // first change of the gesture
        ses().undo.push(std::move(*armed));
        undoArmed_ = false;
    }
    return result;
}

void SeqWindow::armChan() {
    // First note: the track must sound.
    if (m_.iv(Track::On)) return;
    m_.set(Track::On, 1);
}

void SeqWindow::previewNote(int note) {
    // Short preview of the written note (previewNote): drums through the sequencer's channel, otherwise a note of 240
    // ms at most, the previous one cut.
    if (note < 0) return;
    if (drum()) {
        m_.host_.seqCommand(Host::SeqCommand::Preview, cur(), (trackOct() + 1) * 12 + note, 127);
        return;
    }
    m_.previewNote(note);
}

void SeqWindow::previewStep(int s) {
    const ymseq::Lane& l = lane();
    if (s < 0 || s >= ymseq::kSteps || l.pat[size_t(s)] < 0) return;
    previewNote(drum() ? l.pat[size_t(s)] : (ymseq::octaveOf(l.oct[size_t(s)], trackOct()) + 1) * 12 + l.pat[size_t(s)]);
}

// curEntryLike of a pattern. A slot never created is recorded as an empty pattern of the current length: undoing a
// first edit gives back an empty pattern (applyUndo marks it as created).
UndoEntry SeqWindow::patternEntry(int idx) const {
    UndoEntry e;
    e.kind = UndoEntry::Kind::Pattern;
    e.index = idx;
    if (proj().used.test(size_t(idx)))
        e.pattern = proj().patterns[size_t(idx)];
    else
        e.pattern.steps = steps();  // emptyPattern
    return e;
}

void SeqWindow::pushUndoFor(int idx) { ses().undo.push(patternEntry(idx)); }
void SeqWindow::pushUndo() { pushUndoFor(proj().current); }

UndoEntry SeqWindow::entryLike(const UndoEntry& e) const {
    switch (e.kind) {
    case UndoEntry::Kind::Pattern: return patternEntry(e.index);
    case UndoEntry::Kind::Song: {
        UndoEntry s;
        s.kind = UndoEntry::Kind::Song;
        s.song = proj().song;
        return s;
    }
    case UndoEntry::Kind::Track:
    case UndoEntry::Kind::Mix:
    case UndoEntry::Kind::Kit:
    case UndoEntry::Kind::Dma:
    case UndoEntry::Kind::Digi:
    case UndoEntry::Kind::Import: return m_.undoEntryLike(e);
    }
    return e;
}

void SeqWindow::applyUndo(const UndoEntry& e) {
    if (e.kind == UndoEntry::Kind::Song) {
        m_.song_.applyUndo(e);
        return;
    }
    if (e.kind != UndoEntry::Kind::Pattern) {
        m_.applyUndoEntry(e);
        return;
    }
    // applyUndoEntry: the pattern comes back; another pattern becomes current. While playing, the switch goes through
    // the player and happens at the end of the loop (the prototype switched at once).
    const int idx = e.index;
    const ymseq::Pattern p = e.pattern;
    const bool other = idx != proj().current, play = playing_;
    bool changed = false;
    edit([&](plugin::NonParamState& s) {
        s.seq.patterns[size_t(idx)] = p;
        s.seq.used.set(size_t(idx));
        if (other) {
            s.seq.viewBank = idx / ymseq::kBankSize;
            changed = s.seq.select(idx, play);
        }
    });
    if (changed) m_.host_.seqCommand(Host::SeqCommand::Select, idx);
    refresh();
}

// Undo and Redo of the whole editor (one history for patterns, Song, tracks, mixer, kit, DMA, Digidrums, imports).
void SeqWindow::undo() {
    if (auto e = ses().undo.undo([this](const UndoEntry& x) { return entryLike(x); })) applyUndo(*e);
}

void SeqWindow::redo() {
    if (auto e = ses().undo.redo([this](const UndoEntry& x) { return entryLike(x); })) applyUndo(*e);
}

// ------------------------------------------------------------------------------------------- layout

void SeqWindow::show() {
    // openSeqWin: the window comes to the front; the transposition is measured once shown.
    open();
    refresh();
    m_.syncWindowButtons();
}

void SeqWindow::layout() {
    const double ox = bounds().x, oy = bounds().y;
    const double x = ox + 14, w = kW - 28;  // .mixbody content (border 2, margin 4, margin 8)
    double y = oy + 46;                      // title bar, then top margin of the first row

    auto btn = [](Button& b, double ml = 0) {
        FlexItem it{&b, b.naturalWidth(), double(b.naturalHeight())};
        it.marginLeft = ml;
        return it;
    };
    auto lab = [](gemui::Label& l, double ml = 0) {
        FlexItem it{&l, l.naturalWidth(), double(l.px())};
        it.marginLeft = ml;
        return it;
    };
    auto spacer = [](double width = 0, bool autoMargin = false) {
        FlexItem it;
        it.width = width;
        it.autoMargin = autoMargin;
        return it;
    };

    // Row 1: .tools (35 × 29, gap 3, 10 px before the clipboard), then the buttons. Width of the group: 6 tools and 3
    // clipboard buttons, the 7 gaps inside the two groups, and the 10 px span with a gap on each side.
    const double toolsW = 9 * 35 + 7 * 3 + 2 * 3 + 10;
    std::vector<FlexItem> r1 = {{nullptr, toolsW, 29}, spacer(14), btn(stepRec_), btn(clear_), spacer(0, true),
                                btn(euclid_), btn(rotL_, 14), btn(rotR_), btn(trDn_, 14), btn(trUp_),
                                btn(trOctDn_, 14), btn(trOctUp_), spacer(0, true), btn(follow_), btn(plen_),
                                btn(song_)};
    y += flexRow(x, y, w, r1) + 6;
    {
        double tx = r1[0].box.x;
        const double ty = r1[0].box.y;
        for (size_t i = 0; i < tools_.size(); ++i, tx += 38) tools_[i].setBox({tx, ty, 35, 29});
        tx += 10 + 3;  // 10 px span (tools / actions), then the gap of .tools
        for (size_t i = 0; i < clips_.size(); ++i, tx += 38) clips_[i].setBox({tx, ty, 35, 29});
    }

    // Row 2: length.
    std::vector<FlexItem> r2 = {lab(labSteps_), {&steps_, 0, 16}, {&stepsv_, stepsv_.naturalWidth(), 16}};
    r2[1].grow = 1;
    y += flexRow(x, y, w, r2) + 6;

    // Row 3: tempo, gate, swing (value and "%" without a gap).
    const double gw = gatev_.naturalWidth() + gatePct_.naturalWidth();
    const double sw = swingv_.naturalWidth() + swingPct_.naturalWidth();
    // BPM (D-244) keeps the width of the prototype's "Tempo": the row does not move.
    FlexItem bpmLab = lab(labBpm_);
    bpmLab.width = std::max(bpmLab.width, gemui::Font::width("Tempo", labBpm_.px()));
    std::vector<FlexItem> r3 = {bpmLab, {&bpm_, 0, 16}, {&bpmv_, bpmv_.naturalWidth(), 16},
                                lab(labGate_),  {&gate_, 0, 16}, {nullptr, gw, 16},
                                lab(labSwing_), {&swing_, 0, 16}, {nullptr, sw, 16}};
    r3[1].grow = r3[4].grow = r3[7].grow = 1;
    y += flexRow(x, y, w, r3) + 6;
    gatev_.setBox({r3[5].box.x, r3[5].box.y, gatev_.naturalWidth(), 16});
    gatePct_.setBox({r3[5].box.x + gatev_.naturalWidth(), r3[5].box.y, gatePct_.naturalWidth(), 16});
    swingv_.setBox({r3[8].box.x, r3[8].box.y, swingv_.naturalWidth(), 16});
    swingPct_.setBox({r3[8].box.x + swingv_.naturalWidth(), r3[8].box.y, swingPct_.naturalWidth(), 16});
    // All button of the global gate (D-134), after the Gate label: it takes its space from the Gate slider alone, the
    // Tempo and Swing sliders keep the prototype's.
    {
        const gemui::Box g = r3[4].box;
        const double bw = gateAll_.naturalWidth(), bh = gateAll_.naturalHeight();
        gateAll_.setBox({g.x, g.y + std::floor((g.h - bh) / 2), bw, bh});
        gate_.setBox({g.x + bw + 8, g.y, std::max(16.0, g.w - bw - 8), g.h});
    }

    // Row 4: scale, time signature, resolution, figure, chord, voices, ghosts, overview.
    std::vector<FlexItem> r4 = {lab(labScale_), btn(scaleRoot_), btn(scaleSel_), btn(scaleSnap_), spacer(0, true),
                                lab(labSig_),   btn(sig_),       lab(labReso_, 14), btn(reso_), lab(labNote_, 14),
                                btn(fig_),      lab(labChord_, 14), btn(chord_), lab(labPoly_, 14), btn(poly_),
                                spacer(0, true), btn(ghost_), btn(over_)};
    y += flexRow(x, y, w, r4) + 6;

    // Grid and transposition (flex div, align-items:flex-start, gap 6; grid 8 px from the top).
    const int gridH = grid_.naturalHeight();
    grid_.setBox({x, y + 8, double(SeqGrid::kWidth), double(gridH)});
    fitTrSlider();
    // The scroll bar beside the whole grid, from the step numbers down to the last control row (D-294; ninth DAW test,
    // item 108, D-309: over the note rows before); the prototype's slider stood 8 px above the note rows, beside the
    // grid.
    tr_.setBox({ox + kW - kInset - gemui::ScrollPane::kBar, y + 8, double(gemui::ScrollPane::kBar), double(gridH)});
    trProto_ = {x + SeqGrid::kWidth + 6 + 2, y + trTop_, 16, double(trHeight_)};
    y += std::max(8.0 + gridH, double(trTop_ + trHeight_));

    // Scrolling of a pattern of more than 64 steps.
    const bool scrolling = steps() > SeqGrid::kCols;
    scroll_.setVisible(scrolling);
    scrollv_.setVisible(scrolling);
    if (scrolling) {
        // Room for the widest readout ("193-256"): the slider's right arrow stays put while the view moves (D-297).
        FlexItem readout = lab(scrollv_);
        readout.width = std::max(readout.width, gemui::Font::width("000-000", scrollv_.px()));
        std::vector<FlexItem> rs = {{&scroll_, 0, 16}, readout};
        rs[0].grow = 1;
        y += 6 + flexRow(x, y + 6, w, rs);
    }

    // Rule (margin 8px -8px) and transport.
    sep_.setBox({x - 8, y + 8, w + 16, 2});
    y += 8 + 2 + 8;
    // The Song arrows (start, previous, next) only in Song mode with entries. Transport: 52 px buttons 6 px apart,
    // and before the arrows a 2 px rule with 8 px margins.
    const bool nav = st().global.songMode && !proj().song.empty();
    for (Button* b : {&start_, &prev_, &next_}) b->setVisible(nav);
    tbarSep_.setVisible(nav);
    const double tbarW = nav ? 6 * 52 + 6 * 6 + 2 + 16 : 3 * 52 + 2 * 6;
    const double banksW = 4 * banks_[0].naturalWidth() + 3 * 6, patsW = 8 * pats_[0].naturalWidth() + 7 * 6;
    std::vector<FlexItem> rt = {{nullptr, tbarW, 38}, btn(metro_, 14), lab(labBank_), {nullptr, banksW, 31},
                                lab(labPattern_, 8), {nullptr, patsW, 31}};
    rt[2].autoMargin = true;
    const double th = flexRow(x, y, w, rt);
    {
        double bx = rt[0].box.x;
        const double by = rt[0].box.y;
        for (Button* b : {&play_, &rec_, &loop_}) {
            b->setBox({bx, by, 52, 38});
            bx += 52 + 6;
        }
        if (nav) {
            tbarSep_.setBox({bx + 8, by, 2, 38});
            bx += 8 + 2 + 8 + 6;
            for (Button* b : {&start_, &prev_, &next_}) {
                b->setBox({bx, by, 52, 38});
                bx += 52 + 6;
            }
        }
        bx = rt[3].box.x;
        for (Button& b : banks_) {
            b.setBox({bx, rt[3].box.y, b.naturalWidth(), 31});
            bx += b.naturalWidth() + 6;
        }
        bx = rt[5].box.x;
        for (Button& b : pats_) {
            b.setBox({bx, rt[5].box.y, b.naturalWidth(), 31});
            bx += b.naturalWidth() + 6;
        }
    }
    y += th;

    // Overview (#overRow, margin-top 6) and its line, or the legend: the overview keeps its line alone (twelfth DAW
    // test, item 151, D-360).
    overMap_.setVisible(ses().overview);
    overHint_.setVisible(ses().overview);
    hint_.setVisible(!ses().overview);
    if (ses().overview) {
        overMap_.setBox({x, y + 6, double(SeqGrid::kWidth), double(SeqOverview::kCanvasH + 4)});
        y += 6 + SeqOverview::kCanvasH + 4;
        overHint_.setBox({x, y + 6, overHint_.naturalWidth(), 12});
        y += 6 + 12;
    } else {
        hint_.setBox({x, y + 6, w, 12});  // width first: lineCount() wraps the legend at that width
        const double hh = 12.0 * hint_.lineCount();
        hint_.setBox({x, y + 6, w, hh});
        y += 6 + hh;
    }

    // Bottom of the window: margin of .mixbody (8), margin and border of .gwin (4 + 2).
    const double h = y + 8 + 4 + 2 - oy;
    if (std::abs(h - box().h) > 0.01) {
        setBox({ox, oy, double(kW), h});
        titleBar().setBounds({int(ox) + kInset, int(oy) + kInset, kW - 2 * kInset, gemui::TitleBar::kHeight});
    }
    repaint();
}

void SeqWindow::fitTrSlider() {
    // Height = note matrix, from the top of the first row to the bottom of the last; measured only when they are shown
    // (drums: the previous measurement stays).
    if (const auto span = grid_.noteSpan()) {
        // Top margin = gap between the top of the grid and its first row, set from the top of the container: the
        // prototype does not count the 8 px margin of the grid, the slider sits 8 px above the rows.
        trTop_ = span->first;
        trHeight_ = std::max(40, span->second - span->first);
    }
}

std::vector<MenuItem> SeqWindow::figItems() {
    std::vector<MenuItem> items;
    for (int k = 0; k < ymseq::kFigureCount; ++k) {
        if (std::find(ymseq::kFigureSeps.begin(), ymseq::kFigureSeps.end(), k) != ymseq::kFigureSeps.end())
            items.push_back(MenuItem::separatorItem());
        const int n = ymseq::figureSteps(k, resolution());
        MenuItem m;
        m.label = std::string(ymseq::kFigures[size_t(k)].name) + " " + std::to_string(n) + (n > 1 ? " steps" : " step");
        m.icon = &icons::kFigures[size_t(k)];
        m.pick = [this, k] {
            ses().figure = k;
            refreshControls();
        };
        items.push_back(std::move(m));
    }
    return items;
}

void SeqWindow::popup(gemui::Widget& anchor, std::vector<MenuItem> items, int checked) {
    gemui::MenuList::open(desk(), std::move(items), checked, anchor.box(), false);
}

// Menu of a grid cell: its entries act only while the track and the pattern it was opened on are the current ones.
void SeqWindow::popupAt(const Rect& anchor, std::vector<MenuItem> items, int checked) {
    gemui::pickOnlyWhile(items, [this, at = stepTarget()] { return stillOn(at); });
    gemui::MenuList::open(desk(), std::move(items), checked, gemui::boxOf(anchor), false);
}

// ------------------------------------------------------------------------------------------- refresh

void SeqWindow::refresh() {
    // The view stays inside the pattern (loadPattern).
    EditorSession& s = ses();
    s.viewStart = std::clamp(s.viewStart, 0, std::max(0, steps() - SeqGrid::kCols));
    s.curStep = std::clamp(s.curStep, 0, ymseq::kSteps - 1);
    // Deviation (D-282): on a drum track the cursor stays on the eight instrument rows (it could stay on a hidden row
    // left by a melodic track, where Enter wrote an instrument that does not exist).
    s.curRow = std::clamp(s.curRow, drum() ? SeqGrid::kRows - 8 : 0, SeqGrid::kRows - 1);
    if (!isOpen()) return;
    setTitle("STEP SEQUENCER - " + m_.trackName(cur()) + " [" + fxName(cur()) + "]");
    refreshControls();
    refreshTransport();
    refreshPatterns();
    refreshHint();
    grid_.refresh();
    if (euclidSync_) euclidSync_();
    layout();
}

void SeqWindow::refreshControls() {
    const EditorSession& s = ses();
    const bool dr = drum();
    for (size_t i = 0; i < tools_.size(); ++i) tools_[i].setOn(int(i) == s.tool);
    refreshClips();
    stepRec_.setOn(s.stepRec);
    follow_.setOn(s.follow);
    const int plen = proj().tracks[size_t(cur())].length;
    plen_.setText(plen ? "Len " + std::to_string(plen) : "Len =");
    for (Button* b : {&trDn_, &trUp_, &trOctDn_, &trOctUp_}) b->setEnabled(!dr);

    // Rows 2 and 3 (the held slider is not read back).
    auto keep = [this](const gemui::Slider& sl) { return m_.dragging_ == &sl; };
    if (!keep(steps_)) steps_.setValue(steps());
    if (!stepsv_.editing()) stepsv_.setText(std::to_string(steps()));
    const ymengine::GlobalParams& g = st().global;
    const ymengine::SeqClock clock = m_.seqClock();
    const bool midi = clock == ymengine::SeqClock::Midi, host = clock == ymengine::SeqClock::Host;
    const double tempo = m_.pv(Global::FreeTempo);
    if (!keep(bpm_)) bpm_.setValue(tempo);
    // Ticks in Free run (D-149): the tempo no longer drives the lines; the value shows the resulting tempo (without the
    // groove); the swing is replaced by the groove.
    const bool ticksRun = g.timing == ymengine::SeqTiming::Ticks && !midi && !host;
    bpm_.setEnabled(!midi && !host && !ticksRun);
    bpmv_.setDim(midi || host || ticksRun);
    if (!bpmv_.editing()) {
        // MIDI clock: measured tempo ("ext"), like the prototype; Host sync: tempo of the host.
        if (midi && externalBpm_ > 0) bpmv_.setText("ext " + fmt2(externalBpm_));
        else if (host && externalBpm_ > 0) bpmv_.setText("host " + fmt2(externalBpm_));
        else if (ticksRun) bpmv_.setText("ticks " + fmt2(ymseq::ticksBpm(1000 / m_.msPerTick(), g.speed, resolution())));
        else bpmv_.setText(fmt2(tempo));
    }
    const bool gateAll = m_.iv(Global::GateAll) != 0;
    gateAll_.setOn(gateAll);
    const int gate = gateAll ? m_.iv(Global::Gate) : m_.iv(Track::Gate);
    if (!keep(gate_)) gate_.setValue(gate);
    if (!gatev_.editing()) gatev_.setText(std::to_string(gate));
    const int swing = m_.iv(Global::Swing);
    if (!keep(swing_)) swing_.setValue(swing);
    swing_.setEnabled(!ticksRun);
    swingv_.setDim(ticksRun);
    if (!swingv_.editing()) swingv_.setText(std::to_string(swing));  // without "%" (D-119)

    // Scale (refreshScale, refreshScaleEnable).
    const int sc = scale();
    const bool on = sc > 0;
    scaleRoot_.setText(noteName(scaleRoot()));
    scaleSel_.setText(std::string(ymseq::kScales[size_t(sc)].name));
    scaleRoot_.setEnabled(on && !dr);
    scaleSel_.setEnabled(!dr);
    scaleSnap_.setEnabled(on && !dr);
    scaleSnap_.setOn(on && s.snap && !dr);
    labScale_.setDim(dr);
    const std::string why = dr ? "Percussion rows are instruments, not pitches" : "";
    scaleRoot_.setTip(dr ? why : "Scale root");
    scaleSel_.setTip(dr ? why : "Pick a scale");
    scaleSnap_.setTip(dr ? why
                      : on ? (s.snap ? "Notes are forced into the scale"
                                     : "Out-of-scale rows are greyed; click to force notes in")
                           : "Pick a scale first");
    sig_.setText(std::string(ymseq::kSignatures[size_t(signature())].name));
    const bool ticks = g.timing == ymengine::SeqTiming::Ticks;
    if (ticks) {
        const bool swung = std::any_of(g.groove.begin(), g.groove.begin() + std::clamp<int>(g.grooveLength, 1, 16),
                                       [](uint8_t v) { return v > 0; });
        reso_.setText("Ticks " + std::to_string(g.speed) + (swung ? "+" : ""));
        reso_.setTip(host ? "Ticks: Host sync follows the host's beats, the lines use the resolution"
                          : "Ticks: a line lasts " + std::to_string(g.speed) + (midi ? " MIDI clocks" : " ticks") +
                                (swung ? " plus the groove" : ""));
    } else {
        reso_.setText(std::string(ymseq::kResolutions[size_t(resolution())].label));
        reso_.setTip("Value of one step");
    }
    fig_.setText(std::string(ymseq::kFigures[size_t(std::clamp(s.figure, 0, ymseq::kFigureCount - 1))].name));
    fig_.setEnabled(!dr);
    labNote_.setDim(dr);
    fig_.setTip(dr ? "Percussion hits are one step long" : "Length written when you place a note");

    // Chord (refreshChordBtn).
    const bool m3 = mode3(), mo = mono();
    const bool chordOff = dr || m3 || mo;
    chord_.setText(s.chord < 0 ? "Off" : std::string(ymengine::kChords[size_t(s.chord)].name));
    chord_.setEnabled(!chordOff);
    labChord_.setDim(chordOff);
    chord_.setOn(s.chord >= 0 && !chordOff);
    if (dr) chord_.setTip("Percussion rows are instruments, not pitches: stack hits directly in the grid");
    else if (m3) chord_.setTip("Authentic 3 ch: one voice per channel, so no chords");
    else if (mo) chord_.setTip("This track is monophonic: set Voicing to Polyphonic to write chords");
    else if (s.chord < 0) chord_.setTip("Off: placing a note writes that note only. Pick a chord to add voices above it.");
    else {
        const auto& c = ymengine::kChords[size_t(s.chord)];
        std::set<int> uniq;
        std::string offs;
        for (int k = 0; k < c.count; ++k) {
            uniq.insert(c.offsets[size_t(k)]);
            offs += (k ? ", " : "") + std::to_string(c.offsets[size_t(k)]);
        }
        const int voices = std::min(kPolyMax, int(uniq.size()));
        chord_.setTip(std::string(c.name) + ": " + std::to_string(voices) + " voices per note (" + offs +
                      " semitones), " + std::to_string(kPolyMax) + " at most");
    }

    // Voices (refreshPolyBtn).
    poly_.setText(mo ? "Monophonic" : "Polyphonic");
    poly_.setEnabled(!m3);
    labPoly_.setDim(m3);
    poly_.setTip(dr ? (mo ? "Monophonic: a new hit chokes the one still ringing, like a closed hi-hat"
                          : "Polyphonic: hits ring on together, up to " + std::to_string(kPolyMax) + " at once")
                 : m3 ? "Authentic 3 ch: one voice per channel, always monophonic"
                 : mo ? "Monophonic: one note at a time. Holding a new key glides to it instead of retriggering."
                      : "Polyphonic: chords in the grid and under your fingers, up to " + std::to_string(kPolyMax) +
                            " voices");
    ghost_.setOn(s.ghost);
    over_.setOn(s.overview);

    // Transposition of the view (not applicable to a drum track) and scrolling.
    tr_.setEnabled(!dr);
    if (!keep(tr_)) tr_.setValue(s.viewAbsBase);
    scroll_.setRange(0, std::max(0, steps() - SeqGrid::kCols), 1);
    if (!keep(scroll_)) scroll_.setValue(s.viewStart);
    scrollv_.setText(std::to_string(s.viewStart + 1) + "-" + std::to_string(s.viewStart + SeqGrid::kCols));
}

void SeqWindow::refreshClips() {
    // refreshClipBtns: grey out what has nothing to do.
    const CutState c = cutState();
    clips_[0].setEnabled(c.cut);
    clips_[1].setEnabled(c.cut);
    clips_[2].setEnabled(c.paste);
}

void SeqWindow::refreshTransport() {
    // refreshTbars: Play becomes Stop during playback; in Host sync, the host drives the transport.
    const ymengine::GlobalParams& g = st().global;
    const bool host = m_.seqClock() == ymengine::SeqClock::Host;
    play_.setIcon(playing_ ? &icons::kBigStop : &icons::kBigPlay);
    play_.setOn(playing_);
    play_.setEnabled(!host);
    play_.setTip(host ? "Host sync: the host transport starts and stops the sequencer" : playing_ ? "Stop" : "Play");
    rec_.setOn(ses().record);
    loop_.setOn(g.loop);
    const bool nav = g.songMode && !proj().song.empty();
    for (Button* b : {&start_, &prev_, &next_}) b->setEnabled(nav && !host);
    metro_.setOn(g.metronome);
    m_.song_.refreshTransport();
}

void SeqWindow::refreshPatterns() {
    // refreshPatBtns: bank shown, bank of the current pattern (dotted), current and pending pattern.
    const ymseq::Project& p = proj();
    for (int i = 0; i < 4; ++i) {
        banks_[size_t(i)].setOn(i == p.viewBank);
        banks_[size_t(i)].setPending(p.current / ymseq::kBankSize == i && i != p.viewBank);
    }
    for (int i = 0; i < 8; ++i) {
        const int idx = p.viewBank * ymseq::kBankSize + i;
        pats_[size_t(i)].setOn(idx == p.current);
        pats_[size_t(i)].setPending(idx == p.pending);
    }
    m_.song_.refreshPatterns();
}

void SeqWindow::refreshHint() {
    // Legend limited to the rows active in the current mode (refreshSeqGrid).
    const int g = generator();
    std::string t;
    if (g == int(Generator::Digidrum))
        t = std::string("Rows = sample slots. Drag a note to set how long the sample plays. PAN = per-step pan. VOL = "
                        "accent. ") +
            kRtgHint;
    else if (g == int(Generator::Chipdrum))
        t = std::string("Kit by note: C=kick C#=snare D=clap D#=hi-hat E=open-hat F=closed-hat F#=tom 1 G=tom 2 (mod "
                        "8). HLD = do not retrigger while it still rings. PAN = per-step pan. VOL = accent. ") +
            kRtgHint;
    else
        t = std::string("SRC = tone/noise source. GEN = generator per step (CV/SID/SB). ") +
            (arpOn() ? "ARP = arpeggio table. " : "") +
            "Note length is drawn in the grid: drag a note to stretch it. SLD = slide to next note. CRV = slide shape "
            "for that step. GAT = per-step gate. PAN = per-step pan. VOL = accent. " +
            kRtgHint;
    if (mode3() && !m_.dmaTrack(cur())) {  // a YM channel in 3-channel mode: no pan (DMA track: yes)
        const std::string pan = " PAN = per-step pan.";
        const size_t at = t.find(pan);
        if (at != std::string::npos) t.erase(at, pan.size());
    }
    hint_.setText(t);
}

void SeqWindow::refreshEngineStatus(const plugin::EngineStatus& t) {
    const auto& s = t.seq;
    const bool was = playing_;
    playing_ = s.playing;
    externalBpm_ = m_.seqClock() == ymengine::SeqClock::Host ? s.bpm : s.externalBpm;
    // The player has switched to another pattern (message "pa"): it becomes current.
    if (s.pattern >= 0 && s.pattern < ymseq::kPatterns && s.pattern != proj().current) {
        const int idx = s.pattern;
        edit([idx](plugin::NonParamState& st2) { st2.seq.applied(idx); });
    }
    if (was && !playing_) ses().record = false;  // stopping disarms Rec
    // Played step of the focused track (own length included): keys lit, window open or not. A track with its own length
    // (polymeter) loops on its own, so its step is the absolute step count modulo that length.
    const int plen = proj().tracks[size_t(cur())].length;
    const int stEff = s.step < 0 || !playing_ ? -1 : plen ? int(((s.stepAbs % plen) + plen) % plen) : s.step;
    m_.seqKbdLight(stEff);
    if (!isOpen()) return;
    if (stEff >= 0) {
        if (ses().follow && steps() > 16) {  // the view follows playback
            const int page = stEff / SeqGrid::kCols * SeqGrid::kCols;
            const int want = std::max(0, std::min(page, steps() - SeqGrid::kCols));
            if (want != ses().viewStart) setViewStart(want);
        }
        grid_.markStep(stEff);
    } else {
        grid_.markStep(-1);
    }
    if (was != playing_) refreshTransport();
    if (m_.seqClock() != ymengine::SeqClock::Internal) refreshControls();
}

// ------------------------------------------------------------------------------------------- view

void SeqWindow::setTool(int tool) {
    ses().tool = std::clamp(tool, 0, 5);
    refreshControls();
    grid_.repaint();
}

void SeqWindow::ensureVisible() {
    // The view goes along with the cursor.
    if (steps() <= SeqGrid::kCols) return;
    const int page = ses().curStep / SeqGrid::kCols * SeqGrid::kCols;
    setViewStart(std::max(0, std::min(page, steps() - SeqGrid::kCols)));
}

void SeqWindow::setViewStart(int start) {
    const int v = std::clamp(start, 0, std::max(0, steps() - SeqGrid::kCols));
    if (v == ses().viewStart) return;
    ses().viewStart = v;
    refreshControls();
    grid_.refresh();
}

void SeqWindow::setViewBase(int base) {
    const int v = std::clamp(base, ymseq::kPlayLow, 108);
    if (v == ses().viewAbsBase) return;
    ses().viewAbsBase = v;
    if (!m_.dragging_) tr_.setValue(v);
    grid_.refresh();
    if (euclidSync_) euclidSync_();
}

// ------------------------------------------------------------------------------------------- transport, patterns

void SeqWindow::togglePlay() {
    // seqPlay.onclick: Play or Stop; stopping clears the playhead and disarms Rec.
    if (m_.seqClock() == ymengine::SeqClock::Host) return;  // the host drives the transport (D-110)
    if (playing_) {
        m_.host_.seqCommand(Host::SeqCommand::Stop);
        playing_ = false;
        ses().record = false;
        grid_.markStep(-1);
    } else {
        m_.host_.seqCommand(Host::SeqCommand::Play);
        playing_ = true;
    }
    refreshTransport();
}

void SeqWindow::transportStart() {
    // transportStart: restart from the beginning (the Song starts again from its first entry).
    if (m_.seqClock() == ymengine::SeqClock::Host) return;
    if (playing_) m_.host_.seqCommand(Host::SeqCommand::Stop);
    m_.host_.seqCommand(Host::SeqCommand::Play);
    playing_ = true;
    refreshTransport();
}

void SeqWindow::toggleRecord() {
    ses().record = !ses().record;
    if (ses().record && !playing_) togglePlay();  // arming the recording starts playback
    refreshTransport();
}

void SeqWindow::toggleLoop() {
    edit([](plugin::NonParamState& s) { s.global.loop = !s.global.loop; });
}

void SeqWindow::toggleMetro() {
    edit([](plugin::NonParamState& s) { s.global.metronome = !s.global.metronome; });
}

void SeqWindow::selectPattern(int idx) {
    // selectPattern: when stopped, the pattern becomes current; while playing, it waits for the end of the loop.
    bool changed = false;
    const bool play = playing_;
    edit([&](plugin::NonParamState& s) { changed = s.seq.select(idx, play); });
    if (changed) m_.host_.seqCommand(Host::SeqCommand::Select, idx);
    grid_.clearSelections();
    refresh();
}

void SeqWindow::pickPattern(int slot) {
    if (slot < 0 || slot >= ymseq::kBankSize) return;
    selectPattern(proj().viewBank * ymseq::kBankSize + slot);
}

void SeqWindow::pickBank(int bank) {
    if (bank < 0 || bank >= ymseq::kBanks) return;
    banks_[size_t(bank)].onClick();
}

void SeqWindow::patCopy(int idx) {
    ymseq::Pattern p;
    edit([&](plugin::NonParamState& s) { p = s.seq.copyPattern(idx); });
    ses().patClip = p;
}

void SeqWindow::patCut(int idx) {
    pushUndoFor(idx);
    ymseq::Pattern p;
    edit([&](plugin::NonParamState& s) {
        p = s.seq.copyPattern(idx);
        s.seq.cutPattern(idx);
    });
    ses().patClip = p;
}

void SeqWindow::patPaste(int idx) {
    if (!ses().patClip) return;
    pushUndoFor(idx);
    const ymseq::Pattern p = *ses().patClip;
    edit([&](plugin::NonParamState& s) { s.seq.pastePattern(idx, p); });
}

void SeqWindow::patternMenu(int slot) {
    const int idx = proj().viewBank * ymseq::kBankSize + slot;
    std::vector<MenuItem> items;
    auto add = [&](std::string label, std::function<void()> fn) {
        MenuItem m;
        m.label = std::move(label);
        m.pick = std::move(fn);
        items.push_back(std::move(m));
    };
    add("Copy", [this, idx] { patCopy(idx); });
    add("Cut", [this, idx] { patCut(idx); });
    add("Paste", [this, idx] { patPaste(idx); });
    items.push_back(MenuItem::separatorItem());
    const std::pair<const char*, int> tr[] = {{"+1 semitone", 1}, {"-1 semitone", -1}, {"+1 octave", 12},
                                              {"-1 octave", -12}};
    for (const auto& [label, n] : tr)
        add(label, [this, idx, semis = n] {
            pushUndoFor(idx);
            std::bitset<ymseq::kTracks> drums;
            for (int t = 0; t < ymseq::kTracks; ++t) {
                const int g = int(plugin::effectiveGenerator(m_.values_, t));
                drums.set(size_t(t), g == int(Generator::Digidrum) || g == int(Generator::Chipdrum));
            }
            edit([&](plugin::NonParamState& s) { s.seq.transposePattern(idx, semis, drums); });
        });
    popup(pats_[size_t(slot)], std::move(items), -1);
}

// ------------------------------------------------------------------------------------------- track actions

void SeqWindow::clearLaneAsk() {
    // seqClear: track already empty, nothing to confirm.
    auto clearLane = [this] {
        pushUndo();
        editLane([](ymseq::LaneEditor& ed, ymseq::Lane&) {
            ed.clear();
            return true;
        });
        grid_.clearNoteSel();
        grid_.clearRect();
    };
    bool has = false;
    {
        ymseq::Lane copy = lane();
        has = ymseq::LaneEditor(copy, context()).hasNotes();
    }
    if (!has) {
        clearLane();
        return;
    }
    const std::string& n = st().trackNames[size_t(cur())];
    gemui::Alert::Spec spec;
    spec.lines = {{"Clear track " + std::to_string(cur() + 1) + (n.empty() ? "" : " \"" + n + "\"") + "?"},
                  {"All its notes and settings will be lost."}};
    spec.confirmButtons();  // #confirmWin: Cancel by default, Enter and Escape cancel
    gemui::Alert::open(desk(), std::move(spec), [clearLane](int button, const std::string&) {
        if (button == gemui::Alert::kConfirmOk) clearLane();
    });
}

void SeqWindow::transposeLane(int semis) {
    // transposeLane: a note selection wins; drums: fixed slots; all or nothing.
    std::vector<ymseq::SelKey> keys = grid_.noteSel();
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
        if (ed.transposeSel(keys, semis)) return true;
        if (drum()) return false;
        return ed.transpose(semis);
    });
}

void SeqWindow::rotateLane(int dir) {
    // rotateLane: a note selection wins over the track (the keys follow the notes).
    std::vector<ymseq::SelKey> keys = grid_.noteSel();
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
        if (ed.shiftSel(keys, dir)) return true;
        ed.rotate(dir);
        return true;
    });
    grid_.setNoteSel(std::move(keys));
}

int SeqWindow::noteUnderCursor() const {
    // editTargetNote: note under the edit cursor (melodic only).
    if (drum()) return -1;
    ymseq::Lane copy = lane();
    const ymseq::LaneEditor ed(copy, context());
    const int row = viewSemi() + 12 - ses().curRow, c = ses().curStep;
    if (copy.pat[size_t(c)] >= 0 && ed.relNote(c) == row) return c;
    const int s = ed.noteStart(c);
    return s >= 0 && ed.relNote(s) == row ? s : -1;
}

SeqWindow::CutState SeqWindow::cutState() const {
    CutState c;
    if (grid_.hasRect()) {
        c.cut = true;
        c.what = "Selection";
        c.paste = ses().rectClip.has_value();
        return c;
    }
    c.cut = noteUnderCursor() >= 0;
    c.what = "Note";
    c.paste = !drum() && ses().noteClip.has_value();  // a note is not pasted on a drum track (D-282), as Ctrl+V
    return c;
}

void SeqWindow::editClip(int act) {
    if (grid_.hasRect()) {  // rectangular selection
        if (act == 2) rectPaste();
        else rectCopy(act == 1);
        refreshClips();
        return;
    }
    if (act == 2) {
        pasteNote(ses().curStep, viewSemi() + 12 - ses().curRow);
        refreshClips();
        return;
    }
    const int s = noteUnderCursor();
    if (s >= 0) copyNote(s, act == 1);
    refreshClips();
}

void SeqWindow::copyNote(int s, bool cut) {
    ymseq::Lane copy = lane();
    ses().noteClip = ymseq::LaneEditor(copy, context()).copyNoteAt(s);
    if (cut) {
        pushUndo();
        editLane([s](ymseq::LaneEditor& ed, ymseq::Lane&) {
            ed.cutNoteAt(s);
            return true;
        });
    }
    refreshClips();
}

bool SeqWindow::pasteNote(int s, int row) {
    // Deviation (D-282): not on a drum track, whose rows are instruments and whose ties are HLD (Edit > Paste wrote a
    // hit there and held the steps that followed; Ctrl+V was already refused).
    if (!ses().noteClip || drum() || s < 0 || s >= length()) return false;
    pushUndo();
    armChan();
    const ymseq::NoteClip clip = *ses().noteClip;
    return editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) { return ed.pasteNoteAt(s, row, clip); });
}

void SeqWindow::rectCopy(bool cut) {
    if (!grid_.hasRect()) return;
    const ymseq::RectBounds r = grid_.rectBounds();
    ymseq::Lane copy = lane();
    std::vector<ymseq::RectNote> notes = ymseq::LaneEditor(copy, context()).rectNotes(r);
    for (auto& n : notes) n.s -= r.a;
    ses().rectClip = std::move(notes);
    if (cut) {
        pushUndo();
        rectClear();
    }
    refreshClips();
}

void SeqWindow::rectClear() {
    const ymseq::RectBounds r = grid_.rectBounds();
    editLane([r](ymseq::LaneEditor& ed, ymseq::Lane&) {
        ed.rectClear(r);
        return true;
    });
}

void SeqWindow::rectPaste() {
    if (!grid_.hasRect() || !ses().rectClip) return;
    pushUndo();
    const ymseq::RectBounds r = grid_.rectBounds();
    const std::vector<ymseq::RectNote> clip = *ses().rectClip;
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
        ed.rectPaste(r, clip);
        return true;
    });
}

void SeqWindow::rectMove(int ds, int dr) {
    if (!grid_.hasRect()) return;
    ymseq::RectBounds r = grid_.rectBounds();
    // Outside the pattern: nothing is done, not even an undo step.
    {
        ymseq::Lane copy = lane();
        ymseq::RectBounds probe = r;
        if (!ymseq::LaneEditor(copy, context()).rectMove(probe, ds, dr)) return;
    }
    pushUndo();
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) { return ed.rectMove(r, ds, dr); });
    grid_.rect_ = SeqGrid::RectSel{r.a, r.b, r.r1, r.r2};  // the box follows the notes (Snap can take them further)
    grid_.repaint();
}

void SeqWindow::blockCopy(bool cut) {
    if (!grid_.hasBlock()) return;
    std::optional<ymseq::Row> row;
    if (grid_.blockRow()) row = laneRow(*grid_.blockRow());
    ymseq::Lane copy = lane();
    ses().blockClip = ymseq::LaneEditor(copy, context()).blockCopy(grid_.blockLo(), grid_.blockHi(), row);
    if (cut) blockClear();
}

void SeqWindow::blockClear() {
    if (!grid_.hasBlock()) return;
    pushUndo();
    std::optional<ymseq::Row> row;
    if (grid_.blockRow()) row = laneRow(*grid_.blockRow());
    const int a = grid_.blockLo(), b = grid_.blockHi();
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
        ed.blockClear(a, b, row);
        return true;
    });
}

void SeqWindow::blockPaste(int at) {
    if (!ses().blockClip) return;
    pushUndo();
    const ymseq::BlockClip clip = *ses().blockClip;
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
        ed.blockPaste(at, clip);
        return true;
    });
    grid_.setBlock(at, std::min(length() - 1, at + clip.n - 1));
}

void SeqWindow::splitNote(int s, int at) {
    ymseq::Lane copy = lane();
    if (!ymseq::LaneEditor(copy, context()).splitNote(s, at)) return;  // nothing to split: no undo step
    pushUndo();
    editLane([s, at](ymseq::LaneEditor& ed, ymseq::Lane&) { return ed.splitNote(s, at); });
}

void SeqWindow::joinNote(int s) {
    ymseq::Lane copy = lane();
    if (!ymseq::LaneEditor(copy, context()).canJoin(s)) return;
    pushUndo();
    editLane([s](ymseq::LaneEditor& ed, ymseq::Lane&) { return ed.joinNote(s); });
}

void SeqWindow::setNoteLen(int s, int n) {
    editLane([s, n](ymseq::LaneEditor& ed, ymseq::Lane&) {
        ed.setNoteLen(s, n);
        return true;
    });
}

// ------------------------------------------------------------------------------------------- entry

bool SeqWindow::stepRecord(int note, int velocity) {
    // Writing at the cursor, then advance (step entry, window open).
    if (!ses().stepRec || !isOpen()) return false;
    armChan();
    pushUndo();
    const int s = ses().curStep;
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
        ed.recordAt(s, note, velocity);
        return true;
    });
    ses().curStep = (s + 1) % std::max(1, length());
    ensureVisible();
    grid_.refresh();
    return true;
}

void SeqWindow::recordNote(int note, int velocity) {
    // Step entry first; otherwise, while playing with Rec armed, the note is written at the played step.
    if (stepRecord(note, velocity)) return;
    if (!ses().record || !playing_ || grid_.playStep() < 0) return;
    armChan();
    pushUndo();
    const int s = grid_.playStep();
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
        ed.recordAt(s, note, velocity);
        return true;
    });
}

// ------------------------------------------------------------------------------------------- keyboard

bool SeqWindow::noteKey(const gemui::KeyEvent& e) {
    // Note under the cursor (window in front, no rectangular selection, melodic track).
    const int s = noteUnderCursor();
    // Letter of the key: its code when the host gives one, else the character typed, in capitals.
    const int c = e.code ? e.code : (e.ch < 128 ? std::toupper(int(e.ch)) : 0);
    if (e.mods.ctrl) {
        if ((c == 'C' || c == 'X') && s >= 0) {
            copyNote(s, c == 'X');
            return true;
        }
        if (c == 'V' && ses().noteClip) {
            pasteNote(ses().curStep, viewSemi() + 12 - ses().curRow);
            return true;
        }
        return false;
    }
    if (s < 0 || e.key != Key::Character) return false;
    ymseq::Lane copy = lane();
    const int len = ymseq::LaneEditor(copy, context()).noteLenAt(s);
    switch (e.ch) {
    case ']':  // lengthen by one step
        pushUndo();
        setNoteLen(s, len + 1);
        return true;
    case '[':  // shorten by one step
        pushUndo();
        setNoteLen(s, len - 1);
        return true;
    case '\\':  // split at the cursor
        splitNote(s, ses().curStep);
        return true;
    default: break;
    }
    if (c == 'J') {  // weld to the next note
        joinNote(s);
        return true;
    }
    return false;
}

bool SeqWindow::rectKey(const gemui::KeyEvent& e) {
    const int c = e.code ? e.code : (e.ch < 128 ? std::toupper(int(e.ch)) : 0);
    if (!e.mods.ctrl) {
        switch (e.key) {
        case Key::Left: rectMove(-1, 0); return true;
        case Key::Right: rectMove(1, 0); return true;
        case Key::Up: rectMove(0, e.mods.shift ? 12 : 1); return true;
        case Key::Down: rectMove(0, e.mods.shift ? -12 : -1); return true;
        case Key::Delete:
        case Key::Backspace:
            pushUndo();
            rectClear();
            return true;
        case Key::Escape: grid_.clearRect(); return true;
        default: return false;
        }
    }
    if (c == 'C' || c == 'X') {
        rectCopy(c == 'X');
        return true;
    }
    if (c == 'V' && ses().rectClip) {
        rectPaste();
        return true;
    }
    return false;
}

bool SeqWindow::cursorKey(const gemui::KeyEvent& e) {
    EditorSession& s = ses();
    const int n = std::max(1, length());
    const bool dr = drum();
    switch (e.key) {
    case Key::Left:
    case Key::Right:
        if (e.mods.shift) return false;  // Shift: change channel
        s.curStep = mod(s.curStep + (e.key == Key::Right ? 1 : n - 1), n);
        ensureVisible();
        grid_.refresh();
        return true;
    case Key::Up:
    case Key::Down: {
        const bool up = e.key == Key::Up;
        if (dr) {  // rows = instruments, no scrolling
            s.curRow = std::clamp(s.curRow + (up ? -1 : 1), 12 - 7, 12);
        } else {
            // The keys scroll the view only while its bottom note stays within 24 to 84, as in the prototype's keydown
            // handler (the scroll bar goes from 23 to 108).
            s.curRow += up ? -1 : 1;
            if (s.curRow < 0) {  // leaving through the top: the view moves up
                s.curRow = 0;
                if (s.viewAbsBase < 84) setViewBase(s.viewAbsBase + 1);
            } else if (s.curRow > 12) {
                s.curRow = 12;
                if (s.viewAbsBase > 24) setViewBase(s.viewAbsBase - 1);
            }
        }
        grid_.refresh();
        if (euclidSync_) euclidSync_();
        return true;
    }
    case Key::Enter: {
        pushUndo();
        const int c = s.curStep;
        if (dr) s.curRow = std::clamp(s.curRow, 12 - 7, 12);  // the eight instruments (D-282)
        const int off = dr ? 12 - s.curRow : viewSemi() + 12 - s.curRow;
        bool wrote = false;
        editLane([&](ymseq::LaneEditor& ed, ymseq::Lane& l) {
            const bool same = l.pat[size_t(c)] >= 0 && (dr ? l.pat[size_t(c)] : ed.relNote(c)) == off;
            if (same && !dr) {  // already there: remove it, with its hold (D-282: the hold went to the note before)
                ed.cutNoteAt(c);
            } else if (same) {
                l.pat[size_t(c)] = -1;
                l.oct[size_t(c)] = 0;
            } else {
                if (dr) l.pat[size_t(c)] = int16_t(off);
                else ed.writeNoteAt(c, off);
                wrote = true;
            }
            return true;
        });
        if (wrote) previewStep(c);
        return true;
    }
    case Key::Delete:
    case Key::Backspace: {
        pushUndo();
        const int c = s.curStep;
        editLane([c](ymseq::LaneEditor&, ymseq::Lane& l) {
            l.clearStep(c);
            return true;
        });
        return true;
    }
    default: return false;
    }
}

// ------------------------------------------------------------------------------------------- captures, tests

std::optional<Rect> SeqWindow::rectOf(std::string_view id) const {
    if (!isOpen() || id == "bpmVal" || id == "swingVal") return std::nullopt;
    if (id == "seqTrProto") return trProto_.snapped();
    const gemui::Widget* w = widgetOf(id);
    return w && w->showing() ? std::optional<Rect>(w->bounds()) : std::nullopt;
}

const gemui::Widget* SeqWindow::widgetOf(std::string_view id) const {
    struct Named {
        std::string_view id;
        const gemui::Widget* w;
    };
    const Named named[] = {
        {"seqWin", this},         {"seqgrid", &grid_},    {"seqTr", &tr_},          {"seqStepRec", &stepRec_},
        {"seqClear", &clear_},    {"euclidBtn", &euclid_}, {"rotL", &rotL_},        {"rotR", &rotR_},
        {"trDn", &trDn_},         {"trUp", &trUp_},       {"trOctDn", &trOctDn_},   {"trOctUp", &trOctUp_},
        {"seqFollow", &follow_},  {"plenBtn", &plen_},    {"songMode", &song_},     {"seqSteps", &steps_},
        {"bpm", &bpm_},           {"gate", &gate_},       {"seqGateAll", &gateAll_}, {"swing", &swing_},
        {"scaleRoot", &scaleRoot_}, {"scaleSel", &scaleSel_}, {"scaleSnap", &scaleSnap_}, {"seqSig", &sig_},
        {"seqReso", &reso_},      {"seqFig", &fig_},      {"seqChord", &chord_},    {"seqPolySel", &poly_},
        {"seqGhost", &ghost_},    {"seqOver", &over_},    {"trPlay", &play_},       {"trRec", &rec_},
        {"trLoop", &loop_},       {"seqMetro", &metro_},  {"overMap", &overMap_},   {"seqHint", &hint_},
        {"bpmVal", &bpmv_},       {"swingVal", &swingv_}, {"seqScrollv", &scrollv_},
    };
    for (const Named& n : named)
        if (n.id == id) return n.w;
    return nullptr;
}

}  // namespace stym::pluginui
