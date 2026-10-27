// STYM2149 — pluginui: Step Sequencer window (#seqWin of the prototype) and its grid (#seqgrid)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The window edits the current channel's track in the current pattern (ymseq::Project of the non-automatable state):
// tools and clipboard, length, tempo, gate, swing, scale, time signature, resolution, input note value and chord,
// 64-column grid, view transposition (a scroll bar at the window's right edge, D-294), transport, banks and
// patterns. Behaviours taken from the prototype's functions
// named in the comments (buildSeqGrid, refreshSeqGrid, markStep, grid gestures, row menus, transport, banks).
// Positions and sizes: the prototype's CSS boxes at 100 % (window at 36, 96).
//
// The editing state specific to the screen (tool, view, cursor, clipboard, undo) is kept by the host
// (EditorSession); the selections live only as long as the plugin window.
#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gemui/Controls.h"
#include "gemui/Menu.h"
#include "gemui/Scroll.h"
#include "plugincore/State.h"
#include "plugincore/EngineStatus.h"
#include "pluginui/Session.h"
#include "pluginui/Widgets.h"
#include "ymseq/Edit.h"

namespace stym::pluginui {

class MainWindow;
class SeqWindow;

// Control rows of the grid, in DOM order (buildSeqGrid); INS (M6, D-147) and CMD (M6, D-150) before SRC. The rows of
// the eleventh and twelfth DAW tests come after them in the enumeration (stable values: blocks, clipboards) and take
// their place on screen through kSeqLaneOrder: VIB (Vxx, item 132, D-341), BPM (item 133, D-342; the row of the
// pattern, item 150, D-359), ENV (Exx and Rxx, items 138 and 146, D-347, D-355), DLY (Dxx, item 139, D-348), the Note
// Cut row CUT (Cxx, item 140, D-349; the Cut flag of the prototype stays hidden), LFO (Lxx, item 142, D-351), MOD
// (Mxx, item 143, D-352), SMP (Oxx, item 152, D-361) and TUN (Fxx, item 153, D-362). ARP holds Jxx (item 144, D-353).
enum class SeqLane : uint8_t {
    Slt, Oct, Ton, Ins, Cmd, Mix, Fx, Afl, Tie, Cut, Slide, Slc, Gat, Pnr, Rtg, Cnd, Vol, Vib, Bpm, Env, Dly, NoteCut,
    Lfo, Mod, Smp, Tun
};
inline constexpr int kSeqLanes = 26;
// Order on screen, by what the rows do to the note (thirteenth DAW test, item 166, D-375): its pitch (OCT, TON, TUN);
// its sound (INS, GEN, then SRC, ENV and SMP, which GEN decides); its modulations (ARP, VIB, LFO, MOD); its slide
// (SLD, CRV); its timing in the step (CND, DLY, RTG, GAT, HLD, CUT); the other commands (CMD); its place in the mix
// (PAN, VOL); then BPM, shared by the tracks, under VOL behind a separator (item 150).
inline constexpr std::array<SeqLane, kSeqLanes> kSeqLaneOrder = {
    SeqLane::Slt, SeqLane::Oct, SeqLane::Ton, SeqLane::Tun,   SeqLane::Ins,  SeqLane::Fx,  SeqLane::Mix,
    SeqLane::Env, SeqLane::Smp, SeqLane::Afl, SeqLane::Vib,   SeqLane::Lfo,  SeqLane::Mod, SeqLane::Slide,
    SeqLane::Slc, SeqLane::Cnd, SeqLane::Dly, SeqLane::Rtg,   SeqLane::Gat,  SeqLane::Tie, SeqLane::Cut,
    SeqLane::NoteCut, SeqLane::Cmd, SeqLane::Pnr, SeqLane::Vol, SeqLane::Bpm};
// Model row carried by a control row.
ymseq::Row laneRow(SeqLane l) noexcept;
// TUN: cents typed and shown, two digits and a sign in the cell (the engine takes ±100).
inline constexpr int kFineShown = 99;

// Track and pattern a step popup, a cell menu or a grid gesture was opened on (SeqWindow::stepTarget): it writes only
// while they are still the current ones (Song playback, focus automation, a state read again or an undo of another
// pattern can change them under it).
struct SeqStepTarget {
    int track = -1, pattern = -1;
};

// .seqgrid: 38 px label column then 64 columns of 16 px, 1 px gap on a black background, 2 px line; header of the
// step numbers (13 px, 3 px bottom rule), 13 note rows of 16 px (8 for a percussion track), 4 px separator, control
// rows of 13 px and VOL of 44 px. The gaps between the columns and between the rows are 2 px (Indus, 26/10/2026,
// D-458).
class SeqGrid : public gemui::Widget {
public:
    static constexpr int kCols = 64;  // NCOLS
    static constexpr int kRows = 13;  // note rows
    static constexpr int kBorder = 2, kGap = 2, kLabelW = 38, kCellW = 16;
    static constexpr int kHeaderH = 13, kNoteH = 16, kSepH = 4, kLaneH = 13, kVolH = 44;
    static constexpr int kWidth = 2 * kBorder + kLabelW + kCols * (kCellW + kGap);  // 1194

    explicit SeqGrid(SeqWindow& win) : w_(win) {}

    // Height according to the rows shown.
    int naturalHeight() const;
    // Top of the first and bottom of the last note row (fitTrSlider), from the top of the grid; empty when the first
    // row is hidden (percussion).
    std::optional<std::pair<int, int>> noteSpan() const;

    // Playhead (markStep, -1: none).
    void markStep(int step);
    int playStep() const noexcept { return playStep_; }
    // Reads the state again (ghosts included).
    void refresh();

    // Selections.
    bool hasRect() const noexcept { return rect_.has_value(); }
    bool hasBlock() const noexcept { return selA_ >= 0; }
    void clearRect();
    void clearBlock();
    void clearNoteSel();
    void clearSelections();
    ymseq::RectBounds rectBounds() const;
    const std::vector<ymseq::SelKey>& noteSel() const noexcept { return noteSel_; }
    void setNoteSel(std::vector<ymseq::SelKey> keys);
    void setBlock(int a, int b);
    int blockLo() const noexcept { return std::min(selA_, selB_); }
    int blockHi() const noexcept { return std::max(selA_, selB_); }
    std::optional<SeqLane> blockRow() const noexcept { return selRow_; }
    // Alt held: scissors on the notes (setCutCursor).
    void setAlt(bool alt);

    // Rectangle of a note cell (visual row 0-12) or of a control cell, in desktop coordinates; empty if the row is
    // hidden.
    gemui::Rect noteRect(int col, int row) const;
    gemui::Rect laneRect(int col, SeqLane lane) const;
    // Text of a control cell (shown column).
    const std::string& laneText(int col, SeqLane lane) const { return lanes_[size_t(lane)][size_t(col)].text; }
    // Control cell greyed and crossed: its value cannot act there (item 145).
    bool laneCrossed(int col, SeqLane lane) const { return lanes_[size_t(lane)][size_t(col)].nope; }
    bool laneBlocked(int col, SeqLane lane) const { return lanes_[size_t(lane)][size_t(col)].blocked; }

    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    void mouseUp(const gemui::MouseEvent& e) override;
    void mouseCancel() override;
    bool mouseWheel(const gemui::WheelEvent& e) override;
    gemui::Pointer pointerAt(gemui::Point p) const noexcept override;
    std::string tipAt(gemui::Point p) const override;

private:
    friend class SeqWindow;

    // What the mouse hits.
    struct Hit {
        enum class Kind : uint8_t { None, Corner, Step, NoteLabel, LaneLabel, Note, Lane } kind = Kind::None;
        int col = -1;    // shown column (0-63)
        int row = -1;    // visual note row (0 at the top)
        SeqLane lane = SeqLane::Vol;
        gemui::Rect r;   // cell hit
        bool cell() const noexcept { return kind == Kind::Note || kind == Kind::Lane; }
        bool operator==(const Hit& o) const noexcept {
            return kind == o.kind && col == o.col && row == o.row && lane == o.lane;
        }
    };
    Hit hitAt(gemui::Point p) const;

    // Rows shown, from top to bottom.
    struct RowBox {
        enum class Kind : uint8_t { Header, Note, Sep, Lane } kind;
        int index;     // visual note row (0-12) or SeqLane
        int y, h;      // from the top of the grid (line included)
    };
    std::vector<RowBox> rows() const;
    int colX(int col) const noexcept { return bounds().x + kBorder + kLabelW + kGap + col * (kCellW + kGap); }
    int rowOff(int r) const noexcept { return viewSemi_ + 12 - r; }  // relative row (off) of a visual row

    // Classes of a cell (refreshSeqGrid), read again at each refresh. Note cell: on (.onc) a note starts here; held a
    // held note covers it; nend its last step; rz resize pointer; mute chord voice not played (checkerboard); psel note
    // picked with Shift (black outline); offsc row out of the scale; sel in the block or the box; cur edit cursor; qtr
    // beat; bar first step of a bar; dead beyond the track's length; nope below B0 (hatched); ghost, gstart, gend: a
    // note of another track at this pitch, its first and last step.
    struct NoteCell {
        bool on = false, held = false, nend = false, rz = false, mute = false, psel = false, offsc = false,
             sel = false, cur = false, qtr = false, bar = false, dead = false, nope = false;
        bool ghost = false, gstart = false, gend = false;
        int octave = 0;  // digit shown on a note (octOf)
    };
    // Control cell: its short text, on (toggle row set), dim (CRV value with no slide running at that step).
    struct LaneCell {
        std::string text;
        bool on = false, dim = false, sel = false, qtr = false, dead = false;
        bool nope = false;  // a value that cannot act there: greyed and crossed (twelfth DAW test, item 145, D-354)
        // Empty, and nothing written there could act (thirteenth DAW test, item 165, D-374): crossed, a click opens
        // nothing.
        bool blocked = false;
    };
    struct StepNum {
        std::string text;
        bool qtr = false, bar = false;
    };
    void buildGhosts(const ymseq::LaneEditor& ed);
    void paintNoteCell(gemui::Painter& p, const gemui::Rect& r, const NoteCell& c, bool play) const;
    void paintLaneCell(gemui::Painter& p, const gemui::Rect& r, SeqLane lane, int col, bool play) const;
    void paintHeader(gemui::Painter& p, const RowBox& b) const;
    void paintLabel(gemui::Painter& p, const gemui::Rect& r, const std::string& text, bool dis) const;

    // Gestures (pointerdown, pointerover, pointerup, contextmenu of #seqgrid).
    void press(const Hit& h, const gemui::MouseEvent& e);
    void pressLane(const Hit& h, int c, const gemui::MouseEvent& e);
    void pressNote(const Hit& h, int c, const gemui::MouseEvent& e);
    void over(const Hit& h, gemui::Point pos);
    void release();
    void contextMenu(const Hit& h);
    void noteMenu(const Hit& h, int c, int s, int off);
    void rectMenu(const Hit& h);
    void blockMenu(const Hit& h, int c);
    void laneLabelMenu(const Hit& h);
    void laneMenu(const Hit& h, int c);
    void insMenu(const Hit& h, int c);  // INS row: the track's sound or an instrument of the project
    void envMenu(const Hit& h, int c);  // ENV row: the shape and the restart of the envelope
    void resetFlag(SeqLane lane, int c);
    int volAt(const gemui::Rect& r, int y) const noexcept;
    // Edge of a note or chord voice under the pointer, Move and Draw tools (twelfth DAW test, item 141): the outer
    // 40 % of its first step (its start, when it lasts more than one step) or of its last step (its end).
    struct Edge {
        int from, len, voice;  // voice: 0 = root, 2..6 = chord voice
        bool left;
    };
    std::optional<Edge> edgeAt(const Hit& h, gemui::Point p) const;
    // Note of the selection box or of the picked notes at step c, row off (Move and Draw tools): a drag from it moves
    // the whole selection (item 147).
    bool selectedAt(int c, int off) const;

    SeqWindow& w_;
    int playStep_ = -1;

    // Cache of refresh().
    bool drum_ = false;
    int viewSemi_ = 0;
    std::array<bool, kRows> rowShown_{};
    std::array<bool, kSeqLanes> laneShown_{};
    std::array<std::array<NoteCell, kRows>, kCols> cells_{};
    std::array<std::array<LaneCell, kCols>, kSeqLanes> lanes_{};
    std::array<int, kCols> vol_{};
    std::array<int, kCols> volTo_{};  // Gxx (D-393): VOL of the next step that the step glides to, -1: no glide
    std::array<StepNum, kCols> stepNums_{};
    std::array<std::string, kRows> noteLabels_{};
    std::array<bool, kRows> labelDis_{};
    // Ghosts: notes of the other melodic tracks at the same pitch (buildGhostIndex), per shown cell.
    struct Ghost {
        bool on = false, start = false, end = false;
    };
    std::array<std::array<Ghost, kRows>, kCols> ghosts_{};

    // Selections (selA, selB, selRow, rectSel, noteSel) and gestures in progress (moving, sizing, paint, rightSel); the
    // gesture state machine is described at the top of SeqGrid.cpp. Steps are absolute, rows relative (rowOff).
    int selA_ = -1, selB_ = -1;      // block of steps: first step pressed and step reached (-1: no block)
    std::optional<SeqLane> selRow_;  // block limited to one control row; none: every row
    int selDrag_ = 0;                // drag in progress: 0 none, 1 block of steps, 2 box
    struct RectSel {
        int a, b, r1, r2;  // box: steps and rows of its first corner (a, r1) and of its moving corner (b, r2)
    };
    std::optional<RectSel> rect_;
    std::vector<ymseq::SelKey> noteSel_;  // notes picked one by one with Shift (start step and voice)
    // Box started by the right button or the Select tool: its first cell; moved once the pointer has left it.
    struct RightSel {
        int from, row;
        bool moved = false;
    };
    std::optional<RightSel> rightSel_;
    bool erasedByRight_ = false;  // the right button erased or drew a box: no context menu on its release
    // Edge of a note or chord voice being dragged: start step, row, length, voice (0: root), left: its start rather
    // than its end.
    struct Sizing {
        int from, row, len, voice = 0;
        bool left = false;
        bool moved = false;  // the pointer left the step pressed
    };
    std::optional<Sizing> sizing_;
    struct GroupMove {
        int col, row;  // step and row pressed
        int ds = 0, dr = 0;  // move already made
        bool moved = false;  // the pointer left the step pressed
    };
    std::optional<GroupMove> group_;
    std::optional<ymseq::LaneEditor::Moving> moving_;  // note or chord voice grabbed (moved: the drag has begun)
    // Painting continued by the drag: erase, VOL from the pointer's height, notes (from, row: the last note written,
    // that a drag to its right lengthens), or a toggle row set to val.
    struct Paint {
        bool erase = false, vol = false, note = false;
        std::optional<SeqLane> flag;
        int val = 0, from = 0, row = 0;
        static Paint erasing() {
            Paint p;
            p.erase = true;
            return p;
        }
        static Paint volume() {
            Paint p;
            p.vol = true;
            return p;
        }
        static Paint flagged(SeqLane lane, int val) {
            Paint p;
            p.flag = lane;
            p.val = val;
            return p;
        }
        static Paint notes(int from, int row) {
            Paint p;
            p.note = true;
            p.from = from;
            p.row = row;
            return p;
        }
    };
    std::optional<Paint> paint_;
    bool drawing_ = false;  // pencil stroke in progress (pencil cursor everywhere)
    bool alt_ = false;      // Alt held: scissors
    Hit last_;              // cell hovered during the gesture (pointerover comes only on entering)
    bool pressed_ = false;
    SeqStepTarget pressAt_;  // track and pattern of the press: the gesture ends if either changes under it
    gemui::MouseButton button_ = gemui::MouseButton::Left;
    int selTrack_ = -1;     // track of noteSel_ and rect_ (D-282)
};

// canvas#overMap: the whole pattern (256 steps, B0 to C9), one line per note, current channel in black, the others in
// grey, percussion band, blue frame of the view, playhead; a click or a drag moves the view.
class SeqOverview : public gemui::Widget {
public:
    static constexpr int kCanvasW = SeqGrid::kWidth - 4, kCanvasH = 104;  // 38 + 64 × 18, inside a 2 px line
    explicit SeqOverview(SeqWindow& win) : w_(win) {}
    void paint(gemui::Painter& p) override;
    bool acceptsMouse() const noexcept override { return true; }
    void mouseDown(const gemui::MouseEvent& e) override;
    void mouseDrag(const gemui::MouseEvent& e) override;
    gemui::Pointer pointer() const noexcept override { return gemui::Pointer::Crosshair; }

private:
    void jump(gemui::Point p);
    SeqWindow& w_;
    Canvas canvas_{kCanvasW, kCanvasH};
};

// The Step Sequencer window, a member of MainWindow that lives as long as the editor (message thread only). It also
// holds the undo entry points of the whole editor (undo, redo, applyUndo).
class SeqWindow : public gemui::FloatWindow {
public:
    // #seqWin: top 96, 112 + 64 × 17 px wide; 64 px wider with the 2 px gaps of the grid (D-458), so 4 px from the
    // left of the desktop instead of 36.
    static constexpr int kX = 4, kY = 96, kW = SeqGrid::kWidth + 70;

    explicit SeqWindow(MainWindow& main);

    // Opens the window (openSeqWin).
    void show();
    // State read again (parameters, non-automatable state).
    void refresh();
    // Sequencer engine status: playing, played step, pattern switched by the player.
    void refreshEngineStatus(const plugin::EngineStatus& t);
    bool playing() const noexcept { return playing_; }

    // Keys of the prototype specific to the sequencer (order of its keydown handler): true = taken.
    bool noteKey(const gemui::KeyEvent& e);    // note under the cursor: ^C ^X ^V ] [ \ J
    bool rectKey(const gemui::KeyEvent& e);    // rectangular selection: arrows, Delete, Escape, ^C ^X ^V
    bool hasRect() const noexcept { return grid_.hasRect(); }
    bool cursorKey(const gemui::KeyEvent& e);  // ← → ↑ ↓, Enter, Delete when the window is open
    void togglePlay();                         // Play / Stop (Space, CC 82 on screen)
    void transportStart();                     // Play from start (transportStart)
    void toggleRecord();                       // Rec: arming the recording starts playback
    void toggleLoop();
    void toggleMetro();                        // M
    void pickPattern(int slot);                // 1-8: pattern of the shown bank
    void pickBank(int bank);                   // Shift+1-4
    void undo();
    void redo();
    // Edit menu: cut, copy, paste (editClip) and what they target (editCutState).
    struct CutState {
        bool cut = false, paste = false;
        std::string what;  // "Selection" or "Note"
    };
    CutState cutState() const;
    void editClip(int act);  // 0 copy, 1 cut, 2 paste
    // Input from the keyboard or MIDI (stepRecord, recordNote): true if the note was written.
    bool stepRecord(int note, int velocity);
    void recordNote(int note, int velocity);
    // Alt held or released (scissors cursor).
    void setAlt(bool alt) { grid_.setAlt(alt); }

    // Rectangle of a control by the prototype's id (captures, tests); "seqTrProto": where the prototype's slider of
    // the view stood, beside the grid (D-294).
    std::optional<gemui::Rect> rectOf(std::string_view id) const;
    // Control by the same id (tests), plus "bpmVal" and "swingVal" (values of the tempo and the swing) and
    // "seqScrollv" (steps shown by the scrolling).
    const gemui::Widget* widgetOf(std::string_view id) const;
    const SeqGrid& grid() const noexcept { return grid_; }

private:
    friend class MainWindow;
    friend class SeqGrid;
    friend class SeqOverview;
    friend class SongWindow;
    friend class StepSoundWindow;

    // Reading.
    const plugin::NonParamState& st() const noexcept;
    const ymseq::Project& proj() const noexcept;
    const ymseq::Lane& lane() const noexcept;
    gemui::Desktop& desk() const noexcept;
    EditorSession& ses() const noexcept;
    int cur() const noexcept;
    using StepTarget = SeqStepTarget;
    StepTarget stepTarget() const noexcept;
    bool stillOn(StepTarget t) const noexcept;
    int trackOct() const noexcept;
    int length() const noexcept;  // CH[cur].plen || GLOB.steps
    int steps() const noexcept;   // GLOB.steps
    int generator() const noexcept;
    bool drum() const noexcept;
    bool chipdrum() const noexcept;
    bool mode3() const noexcept;
    bool mono() const noexcept;
    // Chord voices (PAT2 to PAT6) played: polyphonic track in 16-channel mode, like polyTrigger of the engine.
    // Elsewhere the grid greys them out and the piano, the keyboard drawing and the pads do not light them.
    bool chordsPlay() const noexcept;
    bool arpOn() const noexcept;
    bool insShown() const noexcept;  // INS row: melodic track, and an instrument exists or the track names one
    bool cmdShown() const;           // CMD row: "Step command rows" preference, or a command of its box written
    bool cmdPreference() const;      // "Step command rows" preference (on by default)
    // Rows of the eleventh and twelfth DAW tests: shown with the preference when they can act, and wherever a value of
    // theirs is written, so that it can be cleared (crossed when it cannot act, item 145). VIB, LFO and MOD (items 132,
    // 142, 143) on melodic tracks; ENV (items 138, 146) on melodic tracks; DLY, CUT and TUN (items 139, 140, 153); SMP
    // on DMA tracks (item 152); BPM in the standalone application (items 133, 150), for the BPM row of the pattern.
    // ARP follows the arpeggiator of the track, as in the prototype (item 144).
    bool vibShown() const;
    bool bpmShown() const;
    bool envShown() const;
    bool arpShown() const;
    bool lfoShown() const;
    bool modShown() const;
    bool smpShown() const;
    bool tunShown() const;
    bool crvShown() const;  // an SLD cell lit, or a curve written (D-387)
    bool ownRowShown(ymengine::SeqCmd k) const;  // the preference, or a command of kind k written in the track
    bool written(ymengine::SeqCmd k) const;      // a command of kind k written in the track
    bool envPlays() const;  // Env source, Sync Buzzer, an Env entry of SRC or a Sync Buzzer step of GEN
    // The sound of step s as the engine plays it (thirteenth DAW test, item 165, D-374): the generator of its note (of
    // a note written there on a rest step), and how the note of the track goes through the step.
    struct StepSound {
        int gen = 0;               // ymengine::Generator
        bool attack = false;       // a note starts there (root or chord voice)
        bool held = false;         // a note started earlier sounds on (TIE, or the steps of a slide)
        bool slid = false;         // held while it slides to the next note (SLD): only VOL acts
        bool slideTarget = false;  // a root that a slide reaches: no new attack
        bool continues = false;    // the note goes on after the step (TIE, or SLD towards the next note)
        int ins = 0;               // instrument of the note (INS, 1 to 32), 0 for the sound of the track
    };
    StepSound stepSound(int s) const;
    // A slide of the CMD box (Axy, Bxy, Nxy) written at step s can move a note of the track (D-386).
    bool slideActs(ymengine::SeqCmd kind, int s) const;
    // What the sound of step s forbids to a row, written or not (item 165: the cell is crossed even empty).
    std::string stepBlocks(SeqLane lane, int s) const;
    // Why the value of a row at step s cannot act there (item 145: the cell is greyed and crossed); empty if it can.
    // An empty cell gets the reasons of stepBlocks only.
    std::string cantAct(SeqLane lane, int s) const;
    // Replay ticks of a line at the current tempo (Ticks timing: ticks per line; BPM timing: the line at the tempo of
    // the sequencer), at least 1.
    int lineTicks() const noexcept;
    int scale() const noexcept;
    int scaleRoot() const noexcept;
    int signature() const noexcept;
    int resolution() const noexcept;
    int viewSemi() const noexcept;  // curViewSemi
    int beatSteps() const noexcept;
    int barSteps() const noexcept;
    int figSteps() const noexcept;
    // Note length written: the one chosen, or Step while it is not a whole number of steps of the resolution (D-425).
    int inputFigure() const noexcept;
    bool latin() const;
    std::string noteName(int pitchClass) const;
    std::string absName(int absNote) const;
    ymseq::EditContext context() const;
    int absCol(int col) const noexcept { return ses().viewStart + col; }  // ABS

    // Writing.
    // Change of the non-automatable state through the host (Host::editExtras), read back at once (pullExtras), which
    // refreshes the windows. No undo step.
    void edit(const std::function<void(plugin::NonParamState&)>& fn);
    // Ticks rate (M6, D-149): Resolution menu, input boxes of the speed and the groove.
    void resolutionMenu();
    // Resolution i chosen (BPM rate); with notes in the patterns, an alert first asks whether they fit the new grid
    // (D-448).
    void setResolution(int i);
    void askSpeed();
    void askGroove();
    // CMD row (M6, D-150): box of the commands of step s (CmdBox, tenth DAW test: D-330).
    void askCmd(int s);
    void prompt(const std::string& message, const std::string& text, int chars,
                std::function<void(const std::string&)> ok);
    // Editing of the current track of the current pattern (the empty track is removed afterwards); returns what fn
    // returns.
    bool editLane(const std::function<bool(ymseq::LaneEditor&, ymseq::Lane&)>& fn);
    void armChan();                     // first note: the track must sound
    void previewNote(int note);         // absolute note (melodic) or row (percussion)
    void previewStep(int s);            // note of step s (root)
    void pushUndo();                    // step of the current pattern (pushUndo of the prototype)
    // Undo step of a grid gesture, taken by editLane at its first change only (D-282: a click that changes
    // nothing no longer empties Redo).
    void armUndo() noexcept { undoArmed_ = true; }
    void pushUndoFor(int idx);  // step of pattern idx (pattern menus, which may target another pattern)
    // Undo step holding pattern idx as it is now (an empty pattern of the current length if it was never created).
    UndoEntry patternEntry(int idx) const;
    // Current state of the object that step e describes (curEntryLike): UndoHistory skips the steps equal to it and
    // keeps it for Redo. Pattern and Song here, the other kinds in MainWindow.
    UndoEntry entryLike(const UndoEntry& e) const;
    void applyUndo(const UndoEntry& e);  // puts back a step of any kind (applyUndoEntry)

    // Building, layout, refresh.
    void build();
    void wire();
    void layout();
    void refreshControls();
    void refreshHint();
    void refreshTransport();
    void refreshPatterns();
    void refreshClips();
    void setTool(int tool);        // 0 Move, 1 Draw, 2 Erase, 3 Split, 4 Join, 5 Select
    void ensureVisible();          // shows the page of 64 steps that holds the edit cursor
    void setViewStart(int start);  // first step shown, kept inside the pattern
    void setViewBase(int base);    // absolute note of the bottom row, 23 (B0) to 108
    void fitTrSlider();
    // Popup menu under a control or a cell; checked: entry marked (-1: none).
    void popup(gemui::Widget& anchor, std::vector<gemui::MenuItem> items, int checked);
    void popupAt(const gemui::Rect& anchor, std::vector<gemui::MenuItem> items, int checked);
    std::vector<gemui::MenuItem> figItems();  // input note values (FIGS), with their length in steps

    // Actions.
    void selectPattern(int idx);
    void patternMenu(int slot);
    void patCopy(int idx);   // patCopy, patCut, patPaste (menus of the pattern buttons)
    void patCut(int idx);
    void patPaste(int idx);
    void clearLaneAsk();
    void transposeLane(int semis);
    void rotateLane(int dir);
    void copyNote(int s, bool cut);
    bool pasteNote(int s, int row);
    void rectCopy(bool cut);
    void rectPaste();
    void rectClear();
    void rectMove(int ds, int dr);
    void blockCopy(bool cut);
    void blockClear();
    void blockPaste(int at);
    void splitNote(int s, int at);
    void joinNote(int s);
    void setNoteLen(int s, int n);
    int noteUnderCursor() const;  // editTargetNote
    void openEuclid();
    void openTrackLength();
    void openVol(const gemui::Rect& cell, int s);   // openVolMenu
    // Right click on a GEN, SRC or ENV cell that holds a value: the window of the settings it brings into play
    // (24/10/2026, D-392); false for an empty cell (the right button erases, as before).
    bool openStepSound(SeqLane row, int s);
    void openGate(const gemui::Rect& cell, int s);  // gatPopup
    void openPan(const gemui::Rect& cell, int s);   // panPopup
    // Value of a command with a row of its own (DLY, CUT, SMP, TUN, the restart tick of ENV): slider, field, None and
    // Set. The cell is taken by value: the Restart submenu of ENV calls this from a menu pick that closePopups()
    // destroys, with the cell it holds.
    void openCmdValue(gemui::Rect cell, int s, ymengine::SeqCmd k);
    // BPM row of the pattern (item 150): the same popup, for every track.
    void openBpm(const gemui::Rect& cell, int s);
    void setBpm(int s, int bpm);  // 0: none; one undo step per gesture
    std::string fxName(int track) const;            // FXNAMES[effect of the track]

    MainWindow& m_;
    bool undoArmed_ = false;
    bool playing_ = false;
    double externalBpm_ = 0;
    std::function<void()> euclidSync_;  // Euclid open: the note follows the cursor

    // Row 1: tools, clipboard, actions.
    std::array<gemui::Button, 6> tools_;
    std::array<gemui::Button, 3> clips_;
    gemui::Button stepRec_{"Step"}, clear_{"Clear"}, euclid_{"Euclid"}, rotL_{"<"}, rotR_{">"}, trDn_{"-1"},
        trUp_{"+1"}, trOctDn_{"Oct-"}, trOctUp_{"Oct+"}, follow_{"Follow"}, plen_{"Len ="}, song_{"Song"};
    // Rows 2 and 3. BPM: "Tempo" in the prototype (fifth DAW test, item 46, D-244).
    gemui::Label labSteps_{"Steps"}, labBpm_{"BPM"}, labGate_{"Gate"}, labSwing_{"Swing"}, gatePct_{"%"},
        swingPct_{"%"};
    gemui::Slider steps_, bpm_, gate_, swing_;
    gemui::ValueField stepsv_, bpmv_, gatev_, swingv_;
    // Global gate (D-134, not in the prototype): when lit, the Gate slider sets the gate of every track.
    gemui::Button gateAll_{"All"};
    // Row 4.
    gemui::Label labScale_{"Scale"}, labSig_{"Signature"}, labReso_{"Resolution"}, labNote_{"Note"},
        labChord_{"Chord"}, labPoly_{"Voicing"};
    gemui::Button scaleRoot_, scaleSel_, scaleSnap_{"Snap"}, sig_, reso_, fig_, chord_, poly_, ghost_{"Ghost"},
        over_{"Overview"};
    // Grid and view transposition.
    SeqGrid grid_{*this};
    gemui::ScrollBar tr_;  // at the right edge of the window (D-294)
    gemui::Box trProto_;   // the prototype's place of it, beside the grid (captures)
    int trHeight_ = 180;  // .trwrap as long as it has not been measured (fitTrSlider)
    int trTop_ = 8;
    // Scrolling of a pattern of more than 64 steps.
    gemui::Slider scroll_;
    gemui::Label scrollv_;
    // Transport, banks, patterns.
    Rule sep_, tbarSep_;
    gemui::Button play_, rec_, loop_, start_, prev_, next_, metro_;
    gemui::Label labBank_{"Bank"}, labPattern_{"Pattern"};
    std::array<gemui::Button, 4> banks_;
    std::array<gemui::Button, 8> pats_;
    // Overview and legend.
    SeqOverview overMap_{*this};
    gemui::Label overHint_{"Whole pattern: every step, every octave. Click or drag to move the view.", 12};
    Paragraph hint_;
};

}  // namespace stym::pluginui
