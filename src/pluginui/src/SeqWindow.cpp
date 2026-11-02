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
#include <limits>
#include <numeric>
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
constexpr std::array<const char*, 7> kFxNames = {"Chip Voice", "SID Voice", "Digidrum", "Sync Buzzer",
                                                "Chipdrum",   "DMA",       "FM Voice"};
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
    case SeqLane::Cmd: return Row::CmdArp;  // stands for the kinds of the CMD row in block and row operations
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
    case SeqLane::Env: return Row::CmdShape;  // with Rxx (rowPartner)
    case SeqLane::Dly: return Row::CmdDelay;
    case SeqLane::NoteCut: return Row::CmdCut;
    case SeqLane::Lfo: return Row::CmdLfo;
    case SeqLane::Mod: return Row::CmdMod;
    case SeqLane::Smp: return Row::CmdOffset;
    case SeqLane::Tun: return Row::CmdFine;
    case SeqLane::Tik: return Row::CmdSpeed;
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
    // Prototype ids of the sliders and buttons that MIDI learn can learn.
    for (const auto& [w, id] : std::initializer_list<std::pair<gemui::Widget*, const char*>>{
            {&tr_, "seqTr"}, {&stepRec_, "seqStepRec"}, {&clear_, "seqClear"}, {&euclid_, "euclidBtn"},
            {&rotL_, "rotL"}, {&rotR_, "rotR"}, {&trDn_, "trDn"}, {&trUp_, "trUp"}, {&trOctDn_, "trOctDn"},
            {&trOctUp_, "trOctUp"}, {&follow_, "seqFollow"}, {&plen_, "plenBtn"}, {&song_, "songMode"},
            {&steps_, "seqSteps"}, {&bpm_, "bpm"}, {&gate_, "gate"}, {&swing_, "swing"}, {&scaleRoot_, "scaleRoot"},
            {&scaleSel_, "scaleSel"}, {&scaleSnap_, "scaleSnap"}, {&sig_, "seqSig"}, {&reso_, "seqReso"},
            {&fig_, "seqFig"}, {&chord_, "seqChord"}, {&poly_, "seqPolySel"}, {&ghost_, "seqGhost"},
            {&over_, "seqOver"}, {&play_, "trPlay"}, {&rec_, "trRec"}, {&loop_, "trLoop"}, {&metro_, "seqMetro"},
            {&launch_, "seqLaunch"}, {&trackSel_, "seqTrack"}, {&scroll_, "seqScroll"}})
        w->setId(id);
    // Banks and Patterns: learnt keys replace the default ones of Launch (C2 to B2).
    for (int i = 0; i < ymseq::kBanks; ++i) banks_[size_t(i)].setId(std::string("seqBank") + char('A' + i));
    for (int i = 0; i < ymseq::kBankSize; ++i) pats_[size_t(i)].setId("seqPat" + std::to_string(i + 1));
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
    // A scroll bar against the right edge of the window (eighth DAW test, item 93): an arrow, one semitone; the
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
    add(launch_);
    launch_.setTip("Launch: the MIDI keys and pads C2 to G2 pick the patterns 1 to 8, G#2 to B2 the banks A to D, "
                   "instead of playing\nMIDI learn can switch it from a button of the controller");
    add(labTrack_);
    add(trackSel_);
    trackSel_.setRaw(true);  // a name the user gives, or the chip's
    trackSel_.setMinWidth(40);
    trackSel_.setTip("Track edited: the one the grid shows and the main window's tabs choose");
    // The track's instrument between Track and Bank (Indus, 01/11/2026 20:21 and 20:43), as at the bottom of
    // the GENERATOR frame: the Instruments window, and the number, whose menu loads an instrument into the
    // track or opens the window. The name is in the menu: the row has no room for its 16 characters.
    insOpen_.setId("seqInsOpen");
    insNum_.setId("seqInsNum");
    add(insOpen_);
    add(insNum_);
    insOpen_.setTip("Open the Instruments window (Ctrl+T)");
    insNum_.setTip("Instrument of this track: the one last loaded into it or stored from it. Pick another to load it "
                   "into the track; --: the track plays its own sound");
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
    // Gate All on: the slider sets the global gate, otherwise that of the track shown.
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
    insOpen_.onClick = [this] { m_.ins_.show(); };
    insNum_.onClick = [this] { m_.instrumentMenu(insNum_); };
    m_.bindSlider(swing_, [this](double v) { m_.set(Global::Swing, float(v)); });
    swingv_.onCommit = [this](const std::string& t) {
        const auto v = sliderValue(swing_, t, 1);
        if (!v) return false;
        m_.set(Global::Swing, float(*v));
        return true;
    };

    scaleRoot_.setPopup(true);  // a GEM pop-up button, with its shadow
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
    scaleSel_.setPopup(true);  // a GEM pop-up button, with its shadow
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
    sig_.setPopup(true);  // a GEM pop-up button, with its shadow
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
    reso_.setPopup(true);  // a GEM pop-up button, with its shadow
    reso_.onClick = [this] { resolutionMenu(); };
    fig_.setPopup(true);  // a GEM pop-up button, with its shadow
    fig_.onClick = [this] { popup(fig_, figItems(), inputFigure()); };
    fig_.setPadding(2, 6);  // the 16 px figure in the height of a 14 px text
    chord_.setPopup(true);  // a GEM pop-up button, with its shadow
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
    poly_.setPopup(true);  // a GEM pop-up button, with its shadow
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
    // The tracks of the mode, each with its generator; the one picked is edited, as a click on its tab does.
    trackSel_.setPopup(true);  // a GEM pop-up button, with its shadow
    trackSel_.onClick = [this] { popup(trackSel_, m_.trackItems(), cur()); };
    launch_.onClick = [this] {
        ses().launch = !ses().launch;
        m_.pushCapture();  // the keys go to the editor rather than to the engine
        refreshTransport();
    };
    metro_.onContext = [this](gemui::Point) {
        // Volume of the metronome (seventh DAW test, item 87): the slider of the Modulation window's cells,
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
// The rows of the switches: while the vibrato, LFO, modulation table or arpeggiator of the track is on, with the
// preference or a value of theirs written; off, the row goes and keeps its values. The row of the envelope:
// with the preference where it can act, and wherever a value of its own is written (crossed if it cannot act, item
// 145).
bool SeqWindow::vibShown() const {
    using ymengine::SeqCmd;
    return !drum() && m_.iv(Track::VibOn) != 0 && (cmdPreference() || written(SeqCmd::Vibrato));
}
bool SeqWindow::lfoShown() const {
    return !drum() && st().tracks[size_t(cur())].patch.lfo.on && (cmdPreference() || written(ymengine::SeqCmd::Lfo));
}
bool SeqWindow::modShown() const {
    return !drum() && st().tracks[size_t(cur())].patch.ornamentOn &&
           (cmdPreference() || written(ymengine::SeqCmd::Mod));
}
bool SeqWindow::arpShown() const {
    // The ARP row of the prototype, without the preference.
    return !drum() && arpOn();
}
bool SeqWindow::envShown() const {
    using ymengine::SeqCmd;
    return !drum() && ((envPlays() && cmdPreference()) || written(SeqCmd::Shape) || written(SeqCmd::Retrig));
}
bool SeqWindow::smpShown() const {
    return (generator() == int(Generator::Dma) && cmdPreference()) || written(ymengine::SeqCmd::Offset);
}
bool SeqWindow::tunShown() const { return (!drum() && cmdPreference()) || written(ymengine::SeqCmd::Fine); }
bool SeqWindow::crvShown() const {
    // The curve of a slide, where a note slides (bugs of 24/10); a curve still written keeps the row so that it
    // can be cleared (crossed, item 145).
    if (drum()) return false;
    const ymseq::Lane& l = lane();
    return std::any_of(l.slide.begin(), l.slide.end(), [](auto v) { return v != 0; }) ||
           std::any_of(l.slc.begin(), l.slc.end(), [](auto v) { return v != 0; });
}
bool SeqWindow::bpmShown() const {
    // The BPM row of the pattern (item 150), in the standalone application.
    if (!m_.host_.standalone()) return false;
    if (cmdPreference()) return true;
    const auto& bpm = proj().cur().bpm;
    return std::any_of(bpm.begin(), bpm.end(), [](uint8_t v) { return v != 0; });
}
SeqWindow::StepSound SeqWindow::stepSound(int s) const {
    // The track played from its first step as laneStep does it (Sequencer.cpp), without CND draws or commands. A root
    // attacks with the generator of its GEN row, otherwise that of its instrument (INS; in 3 ch a Digidrum instrument
    // leaves the track's), otherwise the track's; a DMA track of the 3-channel mode always plays DMA, and DMA falls
    // back to the sound of the track without the STE (laneGen, setupVoice). A root reached by a slide does not
    // attack. A step without a root holds the note while its TIE (or SLD, when the note slides) goes on; there, a
    // GEN step changes a Chip Voice, SID Voice or Sync Buzzer note into another of the three (liveFx).
    const ymseq::Lane& l = lane();
    const int own = generator();
    const auto& bank = st().instruments;
    const auto insAt = [&](size_t k) {  // the instrument a note attacking there plays
        return l.ins[k] > 0 && size_t(l.ins[k]) <= bank.size() && bank[size_t(l.ins[k] - 1)].used ? int(l.ins[k]) : 0;
    };
    const auto attackGen = [&](size_t k) {
        int g = l.fx[k] ? ymengine::kSeqFxGen[size_t(std::min<int>(l.fx[k], ymengine::kSeqFxMax))] : -1;
        if (g < 0 && insAt(k)) {
            const Generator x = bank[size_t(l.ins[k] - 1)].patch.generator;
            if (!(mode3() && x == Generator::Digidrum)) g = int(x);
        }
        if (g < 0) g = own;
        if (m_.dmaTrack(cur())) return int(Generator::Dma);
        if (g == int(Generator::Dma) && (mode3() || !m_.ste()))
            return own == int(Generator::Dma) ? int(Generator::Chip) : own;
        return g;
    };
    const auto isDrum = [](int g) { return g == int(Generator::Digidrum) || g == int(Generator::Chipdrum); };
    const auto liveFx = [&](int g, size_t k) {
        const int eff = l.fx[k] ? ymengine::kSeqFxGen[size_t(std::min<int>(l.fx[k], ymengine::kSeqFxMax))] : -1;
        return eff < 0 || eff == int(Generator::Chipdrum) || isDrum(g) || g == int(Generator::Dma) ? g : eff;
    };
    const auto chordAt = [&](size_t k) {
        return std::any_of(l.vpat.begin(), l.vpat.end(), [k](const auto& v) { return v[k] >= 0; });
    };
    StepSound out;
    const int last = std::clamp(s, 0, ymseq::kSteps - 1);
    if (drum()) {  // hits: no instrument, no GEN, no hold
        out.gen = own;
        out.attack = l.pat[size_t(last)] >= 0 || chordAt(size_t(last));
        return out;
    }
    bool sounding = false, slidePend = false, tiedHold = false;
    int gen = own, ins = 0;
    for (int k = 0; k <= last; ++k) {
        const size_t K = size_t(k);
        out = StepSound{};
        if (l.pat[K] < 0) {
            if (sounding && ((slidePend && (l.slide[K] || l.tie[K])) || (tiedHold && l.tie[K]))) {
                gen = liveFx(gen, K);
                out.held = true;
                out.slid = slidePend;
            } else {
                sounding = slidePend = tiedHold = false;
                gen = attackGen(K);  // the sound a note written there would have
                ins = insAt(K);
            }
        } else {
            if (sounding && slidePend) {
                gen = liveFx(gen, K);
                out.slideTarget = true;
            } else {
                gen = attackGen(K);
                ins = insAt(K);
            }
            sounding = true;
            slidePend = l.slide[K] != 0;
            tiedHold = l.slide[K] || l.tie[K];
        }
        out.gen = gen;
        out.ins = ins;
        out.attack = (l.pat[K] >= 0 && !out.slideTarget) || chordAt(K);
    }
    // A root that starts or that a slide reaches goes on with its TIE or SLD (no gate); a held note goes on while
    // the next step is tied to it (heldStep), the track looping.
    const size_t L = size_t(last), next = size_t((last + 1) % std::max(1, length()));
    if (l.pat[L] >= 0) out.continues = l.slide[L] || l.tie[L];
    else if (out.held) out.continues = l.tie[next] && l.pat[next] < 0;
    return out;
}

bool SeqWindow::slideActs(ymengine::SeqCmd kind, int s) const {
    // Thirteenth DAW test, bugs of 24/10: a slide runs from its step until the next command of its kind, the
    // track looping (laneLineCmds); it can act if it moves a note of that span. Each step has the generator of its
    // note (GEN, INS, track: stepSound) and its source: its SRC cell, otherwise that of the instrument or of the track
    // (applyMix, applyMixDef); the Modulation table of the sound can change both on its steps (NOI, MIX and GEN rows,
    // rowsAt). A rest step counts as the note that would be written there. The drums do not slide (slideFrame); Axy
    // moves the 4-bit volume (also SID's) and the DMA gain, Bxy the envelope that is heard, Nxy the noise.
    using ymengine::SeqCmd;
    if (drum()) return false;
    const ymseq::Lane& l = lane();
    const auto& bank = st().instruments;
    struct Table {  // what the steps of a Modulation table can switch on
        bool noise = false, env = false, fixed = false, sid = false, buzzer = false;
    };
    const auto table = [](const ymengine::TrackPatch& p) {
        Table t;
        if (!p.ornamentOn) return t;
        for (int i = 0; i < std::min<int>(p.ornamentLength, ymengine::kOrnamentSteps); ++i) {
            const size_t I = size_t(i);
            t.noise = t.noise || p.ornamentNoise[I];
            if (const int m = p.rows.mix[I]) {
                const int mode = std::min(int(m), ymengine::kChannelModes) - 1;
                t.noise = t.noise || (mode & 2);
                (mode & 4 ? t.env : t.fixed) = true;
            }
            t.sid = t.sid || p.rows.gen[I] == uint8_t(ymengine::SeqGen::Sid);
            t.buzzer = t.buzzer || p.rows.gen[I] == uint8_t(ymengine::SeqGen::SyncBuzzer);
        }
        return t;
    };
    const int n = std::max(1, length());
    for (int j = 0; j < n; ++j) {
        const int k = (std::clamp(s, 0, n - 1) + j) % n;
        if (j && l.cmdAt(kind, k)) break;  // the next command of its kind takes over
        const StepSound snd = stepSound(k);
        const auto g = Generator(snd.gen);
        const ymengine::TrackPatch& p = snd.ins ? bank[size_t(snd.ins - 1)].patch : st().tracks[size_t(cur())].patch;
        bool noise = snd.ins ? p.noiseOn : m_.iv(Track::Noise) != 0;
        bool env = snd.ins ? p.envelopeVolume : m_.iv(Track::VolMode) == 1;
        if (const int m = l.mix[size_t(k)]) {
            noise = ((m >= ymengine::kSeqMixEnv ? m - ymengine::kSeqMixEnv : m) & 2) != 0;
            env = env || m >= ymengine::kSeqMixEnv;
        }
        const bool chip = g == Generator::Chip, sid = g == Generator::Sid, sb = g == Generator::SyncBuzzer;
        const Table t = chip || sid || sb ? table(p) : Table{};
        bool acts = false;
        switch (kind) {
        case SeqCmd::VolSlide: acts = g == Generator::Dma || sid || (chip && !env) || t.sid || t.fixed; break;
        case SeqCmd::EnvSlide: acts = sb || (chip && env) || t.buzzer || ((chip || sb) && t.env); break;
        case SeqCmd::NoiseSlide: acts = ((chip || sid) && noise) || t.noise; break;
        default: return true;
        }
        if (acts) return true;
    }
    return false;
}

std::string SeqWindow::stepBlocks(SeqLane row, int s) const {
    // Thirteenth DAW test, item 165: what the sound of the step forbids, so that an empty cell is crossed too
    // (GEN on SID Voice: ENV crossed below it). The rows of the drums are hidden there.
    if (drum()) return {};
    const StepSound k = stepSound(s);
    const bool dma = k.gen == int(Generator::Dma), sid = k.gen == int(Generator::Sid),
               sb = k.gen == int(Generator::SyncBuzzer),
               perc = k.gen == int(Generator::Chipdrum) || k.gen == int(Generator::Digidrum);
    static constexpr const char* kDma = "A DMA note plays its sample: no square, no noise, no envelope";
    static constexpr const char* kPerc = "A drum hit plays its own sound: no source, no envelope";
    const bool through = k.held || k.slideTarget;  // no new attack on this step
    switch (row) {
    case SeqLane::Mix:  // applyMix leaves them alone
        if (sb) return "Sync Buzzer plays the hardware envelope alone: no square, no noise, no fixed volume";
        if (dma) return kDma;
        return perc ? kPerc : "";
    case SeqLane::Fx:
        if (m_.dmaTrack(cur())) return "In 3 ch mode, the DMA tracks play the DMA sound";
        return through && (dma || perc) ? "A held DMA or drum note keeps its sound: GEN acts on the notes that start"
                                        : "";
    case SeqLane::Env:  // a SID Voice writes its own volumes; DMA and the drums have no envelope
        if (sid) return "A SID Voice writes its own volumes: the envelope is not used";
        if (dma) return kDma;
        return perc ? kPerc : "";
    case SeqLane::Smp:  // the offset of the DMA attacks of the row
        if (!dma) return "Not a DMA note: there is no sample to start further in";
        return through ? "No note starts on this step" : "";
    case SeqLane::Ins:  // ties, slides and the note a slide reaches keep the instrument of the sounding note
        if (k.held) return "A held note keeps the instrument it started with";
        return k.slideTarget ? "A slide keeps the instrument of the note it starts from" : "";
    case SeqLane::Cnd: return k.held ? "No note starts on this step" : "";  // the roots that start only
    case SeqLane::Gat:  // offIn: a tied or sliding note goes on; heldStep: only on the last step of the hold
        if (k.slid) return "During a slide, only VOL acts";
        return k.continues ? "The note goes on after this step: only the GAT of its last step acts" : "";
    case SeqLane::Rtg: return k.slid ? "During a slide, only VOL acts" : "";
    default: return {};
    }
}

std::string SeqWindow::cantAct(SeqLane row, int s) const {
    // Twelfth DAW test, item 145: a value written where it cannot act. The rows that stay hidden where they cannot
    // act (drums) show it all the same once a value is written, but those of the switches. Thirteenth, item 165: first
    // what the sound of the step forbids, written or not.
    using ymengine::SeqCmd;
    if (std::string why = stepBlocks(row, s); !why.empty()) return why;
    const ymseq::Lane& l = lane();
    const size_t S = size_t(s);
    const auto onTrack = [&](SeqCmd k) { return l.cmdAt(k, s) != 0; };
    const bool starts = l.pat[S] >= 0 || std::any_of(l.vpat.begin(), l.vpat.end(), [S](const auto& v) {
                            return v[S] >= 0;
                        });
    const auto noNote = [&](bool written) {  // a value on a step where no note starts or sounds
        if (!written || starts) return "";
        const StepSound k = stepSound(s);
        return k.held ? "" : "No note on this step";
    };
    switch (row) {
    case SeqLane::Mix: return noNote(l.mix[S] != 0);
    case SeqLane::Fx: return noNote(l.fx[S] != 0);
    case SeqLane::Gat: return noNote(l.gat[S] != 0);
    case SeqLane::Rtg: return noNote(l.rtg[S] != 0);
    case SeqLane::Ins: return l.ins[S] && !starts ? "No note starts on this step" : "";
    case SeqLane::Cnd: return l.cnd[S] && l.pat[S] < 0 && !starts ? "No note starts on this step" : "";
    case SeqLane::Oct:  // drums: the tuning of the hit
        return drum() && l.oct[S] && !starts ? "No note starts on this step" : "";
    case SeqLane::Ton: return drum() && l.ton[S] && !starts ? "No note starts on this step" : "";
    // ARP, VIB and MOD show only where their switch acts (vibShown to arpShown); the LFO can be on without depth.
    case SeqLane::Lfo:
        return onTrack(SeqCmd::Lfo) && st().tracks[size_t(cur())].patch.lfo.depth <= 0
                   ? "The LFO of this track is off (Modulation window)"
                   : "";
    case SeqLane::Env:
        if (!onTrack(SeqCmd::Shape) && !onTrack(SeqCmd::Retrig)) return {};
        return envPlays() ? "" : "The envelope does not play on this track (Source Env, Sync Buzzer, an Env entry of "
                                 "SRC or Sync Buzzer in GEN)";
    case SeqLane::Smp: return onTrack(SeqCmd::Offset) && !starts ? "No note starts on this step" : "";
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
    case SeqLane::Tik:  // the ticks of a line act in Ticks timing only
        return onTrack(SeqCmd::Speed) && st().global.timing != ymengine::SeqTiming::Ticks
                   ? "BPM timing: a line lasts by the tempo"
                   : "";
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
int SeqWindow::figSteps() const noexcept { return ymseq::figureSteps(inputFigure(), resolution()); }
int SeqWindow::inputFigure() const noexcept {
    const int f = std::clamp(ses().figure, 0, ymseq::kFigureCount - 1);
    return ymseq::figureFits(f, resolution()) ? f : 0;
}
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
    c.figure = inputFigure();
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
    // Resolutions of the prototype (BPM rate), 1/64 after 1/32, 1/32T after 1/16T, then, under a rule,
    // the Ticks rate of STYM2149: a line lasts Speed replay ticks plus those of the groove; Speed and Groove
    // are set in either rate. The check mark counts the plain entries only, the rules left out (it fell one
    // entry too low after a rule).
    const ymengine::GlobalParams& g = st().global;
    const bool ticks = g.timing == ymengine::SeqTiming::Ticks;
    std::vector<MenuItem> items;
    int checked = -1;
    for (int k = 0; k < ymseq::kResolutionCount; ++k) {
        const int i = ymseq::kResolutionOrder[size_t(k)];
        if (k == ymseq::kResolutionSep) items.push_back(MenuItem::separatorItem());
        if (!ticks && i == resolution()) checked = k;
        MenuItem m;
        m.label = collapse(ymseq::kResolutions[size_t(i)].name);
        m.pick = [this, i] { setResolution(i); };
        items.push_back(std::move(m));
    }
    items.push_back(MenuItem::separatorItem());
    if (ticks) checked = ymseq::kResolutionCount;
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
    // Rows of the step commands (CMD, BPM, VIB, ENV, DLY and CUT since the eleventh DAW test), preference shared
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

// Another grid with notes in the patterns (Indus, 26/10/2026): Fit moves and resizes them so that they keep
// their time (every pattern, the tracks' own lengths and the rests of the Song; one undo step); Keep leaves them on
// their steps, which then last the new resolution's time, as before; Cancel, the default, changes nothing.
void SeqWindow::setResolution(int i) {
    auto apply = [this, i](bool fit) {
        // Steps per quarter note doubled: whole numbers for every resolution (1.5 for 1/4T).
        int num = int(std::lround(2 * ymseq::kResolutions[size_t(i)].stepsPerBeat));
        int den = int(std::lround(2 * ymseq::kResolutions[size_t(resolution())].stepsPerBeat));
        const int g = std::gcd(num, den);
        num /= g;
        den /= g;
        if (fit) {
            UndoEntry e;
            e.kind = UndoEntry::Kind::Grid;
            ses().undo.push(m_.undoEntryLike(e));
        }
        edit([i, fit, num, den](plugin::NonParamState& s) {
            if (fit) s.seq.fitResolution(num, den);
            s.global.resolution = uint8_t(i);
            s.global.timing = ymengine::SeqTiming::Bpm;
        });
    };
    if (i == resolution() || !proj().hasNotes()) {
        apply(false);
        return;
    }
    const std::string to(ymseq::kResolutions[size_t(i)].label);
    gemui::Alert::Spec spec;
    spec.lines = {{"The patterns hold notes. Fit them to " + to + "?"},
                  {"Fit: each note keeps its time, on the steps of " + to + ".", 12},
                  {"Keep: each note keeps its step, which lasts " + to + ".", 12}};
    spec.buttons = {"Cancel", "Keep", "Fit"};
    spec.defaultButton = spec.escapeButton = spec.focusButton = 0;  // the safest choice
    gemui::Alert::open(desk(), std::move(spec), [apply](int button, const std::string&) {
        if (button > 0) apply(button == 2);
    });
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
    // Box of the commands of the step (tenth DAW test, items 121, 122 and 130). An absent command
    // shows a value to start from: sync mark 1; volume slide down a quarter step per tick, buzzer and noise slides up
    // one unit. OK writes the kinds of the box only (eleventh DAW test: the VIB, BPM, ENV, DLY and CUT rows have the
    // others, and TIK the ticks per line; Xyz and Pxy are retired).
    using ymengine::SeqCmd;
    const auto start = [](SeqCmd k) {
        switch (k) {
        case SeqCmd::Sync:
        case SeqCmd::VolSlide: return 1;
        case SeqCmd::EnvSlide:
        case SeqCmd::NoiseSlide: return 0x10;
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
    // The slides that cannot act from this step are left out, but those written there, to be switched off. The
    // Volume Slide is only that of an older project: the Slide of the VOL row replaces it.
    for (size_t i = 0; i < CmdBox::kOrder.size(); ++i) {
        const SeqCmd k = CmdBox::kOrder[i];
        if (k == SeqCmd::VolSlide) spec.shown[i] = lane().cmdAt(k, s) != 0;
        else if (k == SeqCmd::EnvSlide || k == SeqCmd::NoiseSlide)
            spec.shown[i] = lane().cmdAt(k, s) != 0 || slideActs(k, s);
    }
    // 16 and 32 channels: the Buzzer and Noise slides take a curve, Kxx and Qxx; the 3-channel modes leave them
    // as they are (straight slides of the replay).
    spec.curves = !mode3();
    spec.ok = [this, s, at = stepTarget(), curves = spec.curves](const CmdBox::Entries& entries) {
        if (!stillOn(at)) return;  // the box was for that step of another track or pattern
        editLane([&](ymseq::LaneEditor&, ymseq::Lane& l) {
            const auto write = [&](SeqCmd k) {
                const CmdBox::Entry& e = entries[size_t(int(k) - 1)];
                if (e.on)
                    l.setCmd(s, ymengine::seqCmd(k, uint8_t(e.value)));
                else
                    l.clearCmd(k, s);
            };
            for (const SeqCmd k : CmdBox::kOrder) write(k);
            if (curves)
                for (const SeqCmd k : {SeqCmd::EnvCurve, SeqCmd::NoiseCurve}) write(k);
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
    case UndoEntry::Kind::Import:
    case UndoEntry::Kind::Grid: return m_.undoEntryLike(e);
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
    // BPM keeps the width of the prototype's "Tempo": the row does not move.
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
    // All button of the global gate, after the Gate label: it takes its space from the Gate slider alone, the
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
    // The scroll bar beside the whole grid, from the step numbers down to the last control row (ninth DAW test,
    // item 108, over the note rows before); the prototype's slider stood 8 px above the note rows, beside the
    // grid.
    tr_.setBox({ox + kW - kInset - gemui::ScrollPane::kBar, y + 8, double(gemui::ScrollPane::kBar), double(gridH)});
    trProto_ = {x + SeqGrid::kWidth + 6 + 2, y + trTop_, 16, double(trHeight_)};
    y += std::max(8.0 + gridH, double(trTop_ + trHeight_));

    // Scrolling of a pattern of more than 64 steps.
    const bool scrolling = steps() > SeqGrid::kCols;
    scroll_.setVisible(scrolling);
    scrollv_.setVisible(scrolling);
    if (scrolling) {
        // Room for the widest readout ("193-256"): the slider's right arrow stays put while the view moves.
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
    // and before the arrows a 2 px rule with 8 px margins. The arrows are 34 px wide, so that the track's
    // instrument keeps its number on the row and the longest track name ("Digi1") its characters.
    const bool nav = st().global.songMode && !proj().song.empty();
    for (Button* b : {&start_, &prev_, &next_}) b->setVisible(nav);
    tbarSep_.setVisible(nav);
    constexpr double kArrowW = 34;
    const double tbarW = nav ? 3 * 52 + 3 * kArrowW + 6 * 6 + 2 + 16 : 3 * 52 + 2 * 6;
    const double banksW = 4 * banks_[0].naturalWidth() + 3 * 6, patsW = 8 * pats_[0].naturalWidth() + 7 * 6;
    // The track button right after its label, the 8 px gap of the other label + button pairs (Indus, 29/10/2026
    // 10:54, an extra 8 px margin before). Then the track's instrument (Indus, 01/11/2026 20:43): the
    // Instrument button and the number, as wide as "127"; with the Song arrows, the number alone, its menu opening
    // the Instruments window too.
    insOpen_.setVisible(!nav);
    insNum_.setMinWidth(std::max(40.0, insNum_.widthFor("127")));
    std::vector<FlexItem> rt = {{nullptr, tbarW, 38}, btn(metro_, 14), btn(launch_, 14), lab(labTrack_, 14),
                                btn(trackSel_)};
    if (!nav) rt.push_back(btn(insOpen_, 14));
    rt.push_back(btn(insNum_, nav ? 14 : 0));
    const size_t bankAt = rt.size();  // Bank's label, its buttons, Pattern's label, its buttons
    rt.insert(rt.end(), {lab(labBank_), {nullptr, banksW, 31}, lab(labPattern_, 8), {nullptr, patsW, 31}});
    rt[bankAt].autoMargin = true;
    // The track button keeps the width of the mode's longest track name, so that Bank and Pattern stay put whatever
    // the track ("Digi", "DMA1" are wider than "A"). Where the row lacks room (the Song arrows), the margins before
    // Metronome, Launch, Track and the instrument shrink to 8 px, then the name is shortened: Bank and Pattern never
    // wrap under the transport (Indus, 29/10/2026 15:43).
    double trackW = 0;
    for (int i = 0; i < ymseq::kTracks; ++i) trackW = std::max(trackW, trackSel_.widthFor(m_.trackName(i)));
    rt[4].width = trackW;
    const auto rowWidth = [&rt] {
        double sum = 8.0 * double(rt.size() - 1);
        for (const FlexItem& it : rt) sum += it.width + (it.autoMargin ? 0 : it.marginLeft);
        return sum;
    };
    if (rowWidth() > w)
        for (size_t i : {size_t(1), size_t(2), size_t(3), size_t(5)}) rt[i].marginLeft = 8;
    rt[4].width = std::max(trackSel_.widthFor(""), trackW - std::max(0.0, rowWidth() - w));
    {
        std::string name = m_.trackName(cur());
        while (!name.empty() && trackSel_.widthFor(name) > rt[4].width) {
            while ((static_cast<unsigned char>(name.back()) & 0xC0) == 0x80 && name.size() > 1) name.pop_back();
            name.pop_back();  // the last character, its continuation bytes removed just before
        }
        trackSel_.setText(name);
    }
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
                b->setBox({bx, by, kArrowW, 38});
                bx += kArrowW + 6;
            }
        }
        bx = rt[bankAt + 1].box.x;
        for (Button& b : banks_) {
            b.setBox({bx, rt[bankAt + 1].box.y, b.naturalWidth(), 31});
            bx += b.naturalWidth() + 6;
        }
        bx = rt[bankAt + 3].box.x;
        for (Button& b : pats_) {
            b.setBox({bx, rt[bankAt + 3].box.y, b.naturalWidth(), 31});
            bx += b.naturalWidth() + 6;
        }
    }
    y += th;

    // Overview (#overRow, margin-top 6) and its line, or the legend: the overview keeps its line alone (twelfth DAW
    // test, item 151).
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
    // Grown past the bottom of the desktop (every control row, the 2 px gaps of the grid): the window moves up
    // until its bottom shows, its top never above the menu bar.
    const gemui::Desktop& d = m_.desktop();
    const double top = std::max(double(d.workTop()), std::min(oy, double(d.height()) - h));
    if (h > box().h + 0.01 && top < oy) {
        place({ox, top, double(kW), h});
        return layout();
    }
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
    const int r = resolution();
    const std::string step(ymseq::kResolutions[size_t(r)].label);
    for (int k = 0; k < ymseq::kFigureCount; ++k) {
        if (std::find(ymseq::kFigureSeps.begin(), ymseq::kFigureSeps.end(), k) != ymseq::kFigureSeps.end())
            items.push_back(MenuItem::separatorItem());
        const std::string name(ymseq::kFigures[size_t(k)].name);
        const int n = ymseq::figureSteps(k, r);
        MenuItem m;
        m.label = name + " " + std::to_string(n) + (n > 1 ? " steps" : " step");
        m.icon = &icons::kFigures[size_t(k)];
        // A value that is not a whole number of steps is greyed, its length shown unrounded (Indus, 25/10/2026 17:21):
        // a thirty-second note in 1/16 lasts half a step, a quarter-note triplet two steps and two thirds.
        if (!ymseq::figureFits(k, r)) {
            const double x = ymseq::figureStepsExact(k, r);
            std::string v = fmt2(x);
            while (v.back() == '0') v.pop_back();
            if (v.back() == '.') v.pop_back();
            m.label = name + " " + v + (x > 1 ? " steps" : " step");
            m.disabled = true;
            m.why = x < 1 ? "Shorter than one step of " + step
                          : "Lasts " + v + " steps of " + step + ": not a whole number";
        }
        m.pick = [this, k] {
            ses().figure = k;
            refreshControls();
        };
        items.push_back(std::move(m));
    }
    return items;
}

void SeqWindow::popup(gemui::Widget& anchor, std::vector<MenuItem> items, int checked) {
    const auto* button = dynamic_cast<const gemui::Button*>(&anchor);  // a GEM pop-up button
    gemui::MenuList::open(desk(), std::move(items), checked, anchor.box(), false, false, button && button->popup());
}

// Menu of a grid cell: its entries act only while the track and the pattern it was opened on are the current ones.
void SeqWindow::popupAt(const Rect& anchor, std::vector<MenuItem> items, int checked, bool columns) {
    gemui::pickOnlyWhile(items, [this, at = stepTarget()] { return stillOn(at); });
    gemui::MenuList::open(desk(), std::move(items), checked, gemui::boxOf(anchor), false, columns);
}

// ------------------------------------------------------------------------------------------- refresh

void SeqWindow::refresh() {
    // The view stays inside the pattern (loadPattern).
    EditorSession& s = ses();
    s.viewStart = std::clamp(s.viewStart, 0, std::max(0, steps() - SeqGrid::kCols));
    s.curStep = std::clamp(s.curStep, 0, ymseq::kSteps - 1);
    // Deviation: on a drum track the cursor stays on the eight instrument rows (it could stay on a hidden row
    // left by a melodic track, where Enter wrote an instrument that does not exist).
    s.curRow = std::clamp(s.curRow, drum() ? SeqGrid::kRows - 8 : 0, SeqGrid::kRows - 1);
    if (!isOpen()) return;
    setTitle("STEP SEQUENCER - " + m_.trackName(cur()) + " [" + fxName(cur()) + "]");
    trackSel_.setText(m_.trackName(cur()));
    m_.showInstrumentNumber(insNum_);
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
    // Ticks in Free run: the tempo no longer drives the lines; the value shows the resulting tempo (without the
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
    if (!swingv_.editing()) swingv_.setText(std::to_string(swing));  // without "%"

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
    // The note drawn rather than its name (24/10/2026): Step, the note of the resolution.
    static constexpr std::array<int, ymseq::kResolutionCount> kStepFigure = {3, 4, 5, 6, 11, 12, 13, 7, 14};
    const int fig = inputFigure();
    fig_.setText({});
    fig_.setIcon(&icons::kFigures[size_t(fig ? fig : kStepFigure[size_t(resolution())])]);
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
    launch_.setOn(ses().launch);
    song_.setOn(g.songMode);  // in reverse video while Song mode is on (Indus, 29/10/2026 20:11)
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
        t = std::string("SRC = tone/noise source. GEN = generator per step (CV/SID/SB/FM). ") +
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
    recordClock(s);
    recordHold();
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
    if (m_.seqClock() == ymengine::SeqClock::Host) return;  // the host drives the transport
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
    using Clip = EditorSession::GridClip;
    CutState c;
    if (grid_.hasRect()) {
        c.cut = true;
        c.what = "Selection";
        c.paste = ses().rectClip.has_value();
        return c;
    }
    if (grid_.hasBlock()) {
        c.cut = true;
        c.what = "Block";
        c.paste = ses().blockClip.has_value();
        return c;
    }
    c.cut = noteUnderCursor() >= 0;
    c.what = "Note";
    // At the cursor, the clipboard filled last; a note or a box is not pasted on a drum track.
    switch (ses().lastGridClip) {
    case Clip::Rect: c.paste = !drum() && ses().rectClip.has_value(); break;
    case Clip::Block: c.paste = ses().blockClip.has_value(); break;
    default: c.paste = !drum() && ses().noteClip.has_value(); break;
    }
    return c;
}

void SeqWindow::editClip(int act) {
    using Clip = EditorSession::GridClip;
    if (grid_.hasRect()) {  // rectangular selection
        if (act == 2) {
            rectPaste();
        } else {
            rectCopy(act == 1);
            ses().lastGridClip = Clip::Rect;
        }
    } else if (grid_.hasBlock()) {  // block of steps, as the menu of the block
        if (act == 2) {
            if (ses().blockClip) blockPaste(grid_.blockLo());
        } else {
            blockCopy(act == 1);
            ses().lastGridClip = Clip::Block;
        }
    } else if (act == 2) {
        // Without a selection: the clipboard filled last, at the cursor.
        const int at = ses().curStep;
        if (ses().lastGridClip == Clip::Rect && ses().rectClip && !drum()) {
            pushUndo();
            const std::vector<ymseq::RectNote> clip = *ses().rectClip;
            editLane([&](ymseq::LaneEditor& ed, ymseq::Lane&) {
                ed.rectPaste({at, at, 0, 0}, clip);
                return true;
            });
        } else if (ses().lastGridClip == Clip::Block && ses().blockClip) {
            blockPaste(at);
        } else {
            pasteNote(at, viewSemi() + 12 - ses().curRow);
        }
    } else if (const int s = noteUnderCursor(); s >= 0) {
        copyNote(s, act == 1);
        ses().lastGridClip = Clip::Note;
    }
    refreshClips();
}

void SeqWindow::selectAll() {
    const int n = std::max(1, length());
    grid_.clearSelections();
    grid_.selTrack_ = cur();  // the selection belongs to this track
    if (drum()) {
        grid_.setBlock(0, n - 1);
        return;
    }
    // The box spans the steps of the track and the rows of its lowest and highest notes, chord voices included, and
    // goes on to the end of a note held beyond the last step: the box takes only the notes it covers whole.
    ymseq::Lane copy = lane();
    const ymseq::LaneEditor ed(copy, context());
    int lo = std::numeric_limits<int>::max(), hi = std::numeric_limits<int>::min();
    for (int s = 0; s < n; ++s) {
        if (copy.pat[size_t(s)] >= 0) {
            lo = std::min(lo, ed.relNote(s));
            hi = std::max(hi, ed.relNote(s));
        }
        for (int v = 2; v <= ymseq::kPolyMax; ++v)
            if (const auto r = ed.voiceRow(s, v)) {
                lo = std::min(lo, *r);
                hi = std::max(hi, *r);
            }
    }
    if (lo > hi) {
        grid_.refresh();
        return;
    }
    int end = n - 1;
    for (const ymseq::RectNote& note : ed.rectNotes({0, ymseq::kSteps - 1, lo, hi}))
        if (note.s < n) end = std::max(end, note.s + note.len - 1);
    grid_.rect_ = SeqGrid::RectSel{0, end, lo, hi};
    grid_.refresh();
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
    // Deviation: not on a drum track, whose rows are instruments and whose ties are HLD (Edit > Paste wrote a
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
    if (m_.iv(Global::Velocity) == 2) velocity = 127;  // Velocity Off: VOL stays 15
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
    if (m_.iv(Global::Velocity) == 2) velocity = 127;  // Velocity Off: VOL stays 15
    // Step entry first; otherwise, while playing with Rec armed, the note is written at the played step.
    if (stepRecord(note, velocity)) return;
    if (!ses().record || !playing_ || grid_.playStep() < 0) return;
    armChan();
    pushUndo();
    const int s = grid_.playStep(), t = cur(), p = proj().current;
    const bool hit = drum(), poly = chordsPlay(), monoChord = mono() && !mode3();
    // A key pressed while others recorded on the track are held, or with those written at this step, joins them as a
    // chord voice where the track plays chords. On a Monophonic track, the keys played together are all written
    // too, their chord voices greyed until the track is Polyphonic again, but a key pressed later ends the held note
    // there. In the 3-voice modes it is the only note: the held one ends here.
    bool chord = false;
    for (RecNote& r : recNotes_) {
        if (r.track != t || r.pattern != p || !(r.abs == recAbs_ || (r.held && !r.hit))) continue;
        if (poly || (monoChord && r.abs == recAbs_)) {
            chord = true;
            continue;
        }
        if (r.held && !r.hit) recordLength(r, int(recAbs_ - r.abs));
        r.held = false;
    }
    // The notes written at this step before this pass are replaced, its chord voices included, greyed or not; the
    // keys of this pass at the step join the first one. A drum track keeps its other hits.
    const bool fresh = !hit && std::none_of(recNotes_.begin(), recNotes_.end(), [&](const RecNote& r) {
        return r.track == t && r.pattern == p && r.step == s && r.abs == recAbs_;
    });
    int voice = 1, demoted = 0;
    editLane([&](ymseq::LaneEditor& ed, ymseq::Lane& l) {
        if (fresh) {
            for (int v = 2; v <= ymseq::kPolyMax; ++v) ed.delVoiceAt(s, v);
            if (chord && l.pat[size_t(s)] >= 0)  // the key joins a held note: the root of the step goes too
                for (int k = 0, n = ed.noteLenAt(s); k < n; ++k) {
                    l.pat[size_t(s + k)] = -1;
                    l.oct[size_t(s + k)] = 0;
                    l.tie[size_t(s + k)] = 0;
                }
        }
        if (chord) {
            voice = ed.recordVoiceAt(s, note);
            // Monophonic: the lowest key of the chord is the root, the note heard and drawn in black.
            if (voice > 1 && !poly && !hit && l.pat[size_t(s)] >= 0) {
                const int root = ed.relNote(s);
                const std::optional<int> row = ed.voiceRow(s, voice);
                if (row && *row < root && ed.promoteVoice(s, {s, voice})) {
                    demoted = ed.voiceAt(s, root);
                    voice = 1;
                }
            }
            return voice != 0;
        }
        ed.recordAt(s, note, velocity);
        if (!hit) ed.setNoteLen(s, 1);  // its hold follows the key, not that of the note it replaces
        return true;
    });
    if (!voice) return;  // the six voices of the step are taken
    if (demoted)  // the former root, now a chord voice, still follows its key
        for (RecNote& r : recNotes_)
            if (r.track == t && r.pattern == p && r.step == s && r.voice == 1) r.voice = demoted;
    RecNote r;
    r.note = note;
    r.track = t;
    r.pattern = p;
    r.step = s;
    r.voice = voice;
    r.abs = recAbs_;
    r.phase = recPhase();
    r.hit = hit;
    recNotes_.push_back(r);
}

void SeqWindow::recordRelease(int note) {
    // Length: the time held, rounded to the nearest step, one at least.
    for (RecNote& r : recNotes_) {
        if (!r.held || r.note != note) continue;
        r.held = false;
        if (!r.hit) recordLength(r, int(std::lround(double(recAbs_ - r.abs) + recPhase() - r.phase)));
    }
}

double SeqWindow::recPhase() const noexcept {
    if (recStepMs_ <= 0) return 0;
    return std::clamp((m_.nowMs_ - recStepAt_) / recStepMs_, 0.0, 0.99);
}

void SeqWindow::recordClock(const ymengine::Engine::SeqStatus& s) {
    // Length of a step measured between the steps seen (several at once when they are shorter than a frame).
    const int64_t abs = playing_ ? s.stepAbs : -1;
    if (abs == recAbs_) return;
    const int64_t k = abs - recAbs_;
    if (recAbs_ >= 0 && abs >= 0 && k >= 1 && k <= 16) {
        const double d = (m_.nowMs_ - recStepAt_) / double(k);
        recStepMs_ = recStepMs_ > 0 ? recStepMs_ + (d - recStepMs_) * 0.25 : d;
    }
    recAbs_ = abs;
    recStepAt_ = m_.nowMs_;
}

void SeqWindow::recordHold() {
    const bool on = playing_ && ses().record && recAbs_ >= 0;
    for (RecNote& r : recNotes_) {
        if (!r.held || r.hit) continue;
        // The note stops following its key when recording stops, or the track or the pattern changes.
        if (!on || r.track != cur() || r.pattern != proj().current || recAbs_ < r.abs) {
            r.held = false;
            continue;
        }
        recordLength(r, int(recAbs_ - r.abs) + 1);  // up to the played step
    }
    std::erase_if(recNotes_, [&](const RecNote& r) { return !on || (!r.held && r.abs != recAbs_); });
}

void SeqWindow::recordLength(RecNote& r, int n) {
    if (r.track != cur() || r.pattern != proj().current) return;
    n = std::clamp(n, 1, std::max(1, length() - r.step));  // up to the end of the track
    if (n == r.len) return;
    r.len = n;
    const RecNote at = r;
    editLane([&at, n](ymseq::LaneEditor& ed, ymseq::Lane& l) {
        if (l.vp(at.voice, at.step) < 0) return false;  // taken away meanwhile
        if (at.voice == 1)
            ed.setNoteLen(at.step, n);
        else
            ed.setVoiceLen(at.step, at.voice, n);
        return true;
    });
}

// ------------------------------------------------------------------------------------------- keyboard

bool SeqWindow::clipKey(const gemui::KeyEvent& e) {
    // Window in front (Indus, 28/10/2026 02:09): ^A selects every note, ^C ^X ^V act on the box, the block of
    // steps or the note under the cursor, like Edit > Cut, Copy, Paste. Shift + ^ is left to the generators.
    if (!e.mods.ctrl || e.mods.shift || e.mods.alt) return false;
    // Letter of the key: its code when the host gives one, else the character typed, in capitals.
    const int c = e.code ? e.code : (e.ch < 128 ? std::toupper(int(e.ch)) : 0);
    switch (c) {
    case 'A': selectAll(); return true;
    case 'C': editClip(0); return true;
    case 'X': editClip(1); return true;
    case 'V': editClip(2); return true;
    default: return false;
    }
}

bool SeqWindow::noteKey(const gemui::KeyEvent& e) {
    // Note under the cursor (window in front, no rectangular selection, melodic track).
    const int s = noteUnderCursor();
    if (e.mods.ctrl) return false;  // ^C ^X ^V: clipKey
    // Letter of the key: its code when the host gives one, else the character typed, in capitals.
    const int c = e.code ? e.code : (e.ch < 128 ? std::toupper(int(e.ch)) : 0);
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
    if (e.mods.ctrl) return false;  // ^C ^X ^V: clipKey
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
        if (dr) s.curRow = std::clamp(s.curRow, 12 - 7, 12);  // the eight instruments
        const int off = dr ? 12 - s.curRow : viewSemi() + 12 - s.curRow;
        bool wrote = false;
        editLane([&](ymseq::LaneEditor& ed, ymseq::Lane& l) {
            const bool same = l.pat[size_t(c)] >= 0 && (dr ? l.pat[size_t(c)] : ed.relNote(c)) == off;
            if (same && !dr) {  // already there: remove it, with its hold (the hold went to the note before)
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
        {"trLoop", &loop_},       {"seqMetro", &metro_},  {"seqLaunch", &launch_}, {"overMap", &overMap_},
        {"seqTrack", &trackSel_}, {"seqInsOpen", &insOpen_}, {"seqInsNum", &insNum_},
        {"seqHint", &hint_},
        {"bpmVal", &bpmv_},       {"swingVal", &swingv_}, {"seqScrollv", &scrollv_},
    };
    for (const Named& n : named)
        if (n.id == id) return n.w;
    return nullptr;
}

}  // namespace stym::pluginui
