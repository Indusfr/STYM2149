// STYM2149 — ymseq: editing of a pattern track (notes, ties, chord voices, selections)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Functions of the prototype's page that modify the track of the current channel (writeNoteAt, writeFigure,
// stretchNote, setNoteLen, splitNote, joinNote, chord voices, rectSel, rotateLane, transposeLane…), same rules and
// same limits. They do not take an undo step: the caller does it (ymseq/Undo.h), at the same moment as the prototype.
// The "rows" (row, off) are relative to the track's octave: 0 = C of seqOct.
#pragma once
#include <algorithm>
#include <optional>
#include <vector>

#include "ymseq/Pattern.h"

namespace stym::ymseq {

// What the prototype's functions read outside the track.
struct EditContext {
    int length = kDefaultSteps;  // CH[cur].plen || GLOB.steps
    int trackOctave = 3;         // CH[cur].seqOct
    bool drum = false;           // Digidrum or Chipdrum generator: rows = instruments
    bool mono = false;           // Voicing Monophonic
    bool threeVoice = false;     // 3-channel modes: no chord
    int figure = 0;              // input figure (FIGS)
    int resolution = 2;          // GLOB.reso
    int chord = -1;              // input chord (CHORDS), -1 = Off
    int scale = 0, scaleRoot = 0;
    bool snap = false;
    int digidrumSlot = 0;        // Digidrum slot of the track ("Chan" of the Euclidean generator, D-103)
};

// Chord voice that covers a step.
struct VoiceHit {
    int start = 0;
    int voice = 0;  // 2..6
};

// Copied note (noteClip).
struct NoteClip {
    int row = 0, len = 1;
    int vol = 15, mix = 0, fx = 0, slide = 0, slc = 0;
    int ins = 0;  // INS (M6): follows the note like GEN (D-147)
};

// Note of a rectangular selection (rectNotes): relative step possible (clipboard). A chord voice keeps only its pitch
// and its duration: VOL, SLD and CRV belong to the step's root.
struct RectNote {
    int s = 0, row = 0, len = 1;
    int vol = 15, slide = 0, slc = 0;
    int v = 1;  // 1 = root, 2..6 = chord voice
};
struct RectBounds {
    int a = 0, b = 0, r1 = 0, r2 = 0;  // steps a..b, rows r1..r2 (bounds included)
};

// Note picked one by one (noteSel): step and voice (1 = root, 2..6 = chord voices).
struct SelKey {
    int s = 0, v = 1;
    bool operator==(const SelKey&) const = default;
};
// Note of a note-by-note selection (selNotes): step, voice, row.
struct SelNote {
    int s = 0, v = 1, row = 0;
};

// Copied block of steps (blockClip): all the rows (chord voices included) or a single one.
struct BlockClip {
    int n = 0;
    std::optional<Row> row;  // single row, or all
    Lane data;               // steps 0..n-1
};

// Start of the note held at step c of a lane (0-255): c for a root, the step of the root whose ties reach c, -1
// otherwise. LaneEditor::noteStart within the length of the track.
int noteStartOf(const Lane& lane, int c) noexcept;

// The editing rules of one track, with the context of the current channel. Short-lived, on the message thread: the
// Step Sequencer builds one for each change (SeqWindow::editLane, on the lane that Pattern::edit returns inside
// Host::editExtras) or to read a track. It keeps a reference to the lane and a copy of the context. Steps (s, c, a, at)
// are indices in the track (0-255); rows are relative to the track's octave, or percussion rows 0-7.
class LaneEditor {
public:
    LaneEditor(Lane& lane, const EditContext& ctx) noexcept : l_(lane), c_(ctx) {}

    const EditContext& context() const noexcept { return c_; }
    int length() const noexcept { return std::clamp(c_.length, 1, kSteps); }

    // ---- notes and rows
    int relNote(int s) const noexcept;                   // row of the root of step s
    int snapRow(int row) const noexcept;                 // row brought to the nearest degree by Snap (D-280)
    void writeNoteAt(int s, int row) noexcept;           // writes the root (Snap included)
    void writeVoiceAt(int s, int row, int v) noexcept;   // writes voice v (without Snap)
    int noteStart(int c) const noexcept;                 // start of the note held at c, -1 otherwise
    int figLenAt(int c) const noexcept;                  // steps held from c up to the next note; 1 if c is not tied
    void clearFigure(int c) noexcept;                    // removes the hold from c (ties up to the next note)
    void stretchNote(int c, int to) noexcept;            // holds the note at c up to step `to`, not over a note
    void writeFigure(int c) noexcept;                    // gives the note at c the length of the input figure
    bool slideRunsAt(int a) const noexcept;              // an earlier note's slide (SLD) still runs at step a
    int noteLenAt(int s) const noexcept;                 // length of the note at s in steps, its hold included
    // Length of the note at s brought to n steps (within the track). A note met on the way stops it, except in
    // polyphonic mode, where that note becomes a chord voice and the note goes on.
    void setNoteLen(int s, int n) noexcept;
    int setNoteStart(int s, int ns) noexcept;            // new start, -1 if nothing changes
    bool splitNote(int s, int at) noexcept;              // two notes, the second from step `at`
    int nextNoteAfter(int s) const noexcept;             // first note after the end of the note at s, -1 if none
    bool canJoin(int s) const noexcept { return s >= 0 && nextNoteAfter(s) >= 0; }
    bool joinNote(int s) noexcept;                       // the note at s held to the end of the next note
    void eraseAt(int c) noexcept;                        // step emptied (except chord voices and free rows)
    bool stepBusyAt(int c, int row) const noexcept;      // a root (held or not) or a chord voice on `row` at c
    bool eraseNoteAt(int c, int row) noexcept;           // removes that note, whole; false if there is none

    // ---- chord voices
    int voiceDur(int s, int v) const noexcept;           // duration of chord voice v in steps (at least 1)
    std::optional<int> voiceRow(int s, int v) const noexcept;  // row of voice v (1-6); nothing if it is empty
    int voiceAt(int s, int row) const noexcept;          // voice (1-6) that holds this row, 0 otherwise
    int voiceCount(int s) const noexcept;                // voices written at s, the root included
    // Chord voice on `row` that starts at s or before and still sounds at s.
    std::optional<VoiceHit> voiceStartCovering(int s, int row) const noexcept;
    bool stepBusy(int s) const noexcept;                 // a note starts at s or covers it (hold, chord voice)
    int freeVoice(int s) const noexcept;                 // first empty chord voice (2-6) of step s, 0 if none
    int addVoiceAt(int s, int row) noexcept;             // Snap included (D-280)
    void delVoiceAt(int s, int v) noexcept;
    // Voices of the input chord (EditContext::chord) stacked on the root at `row`; none on a percussion, monophonic
    // or 3-channel track. Returns the number of voices added.
    int writeChord(int s, int row) noexcept;
    bool demoteMain(int s) noexcept;                     // the root at s becomes a chord voice of the same length
    bool promoteVoice(int c, const VoiceHit& hit) noexcept;  // mono: the greyed voice becomes the root
    // Handles of a chord voice: right edge (duration, true if it changes) and left edge (new start, the end does not
    // move; nothing if the row is already taken at the new start).
    bool setVoiceLen(int s, int v, int n) noexcept;
    std::optional<VoiceHit> setVoiceStart(int s, int v, int row, int ns) noexcept;
    // Note to preview for the root of step s (previewNote): absolute note, or percussion row; -1 if none.
    int previewNote(int s) const noexcept;

    // ---- note clipboard: the root at s with its length and the rows that follow it; a paste writes it at step s on
    // `row` (Snap included) and is refused outside the track; cut removes the note and leaves its rows.
    NoteClip copyNoteAt(int s) const noexcept;
    void cutNoteAt(int s) noexcept;
    bool pasteNoteAt(int s, int row, const NoteClip& clip) noexcept;

    // ---- moving a note or a voice (moveNoteTo, moveVoiceTo)
    // State of a drag, kept by the grid from press to release; moveNoteTo updates it at each step of the drag.
    struct Moving {
        int from = 0, row = 0, len = 1;  // where the note is now (start step, row) and its length in steps
        bool moved = false, drum = false;  // moved at least once; percussion hit
        int voice = 0;  // 0 = root, 2..6 = chord voice
        int grab = 0;   // step under the pointer at the press, counted from the note's start (used by the grid)
        // Values of the rows that follow a moved root (kNoteRows) that its start covers, given back when it leaves.
        std::array<int, 8> under{15, 0, 0, 0, 0, 0, 0, 0};
    };
    void moveNoteTo(Moving& m, int c2, int row2) noexcept;

    // ---- rectangular selection
    std::vector<RectNote> rectNotes(const RectBounds& r) const;  // roots and chord voices that start in the box
    void rectClear(const RectBounds& r) noexcept;                 // removes them
    bool rectMove(RectBounds& r, int ds, int dr);         // false: outside the pattern, nothing is done
    // Writes the clip with its steps counted from r.a; a note that would not fit in the track is left out.
    void rectPaste(const RectBounds& r, const std::vector<RectNote>& clip) noexcept;

    // ---- note-by-note selection: true if the selection took the gesture (even if refused at a limit)
    std::vector<SelNote> resolve(const std::vector<SelKey>& keys) const;  // notes still present, by step
    bool transposeSel(const std::vector<SelKey>& keys, int semis);
    bool shiftSel(std::vector<SelKey>& keys, int dir);                     // the keys follow the notes
    // Drag of the picked notes by ds steps and dr semitones, all or nothing like rectMove (twelfth DAW test, item
    // 147); false (nothing done) if refused. The keys follow the notes.
    bool moveSel(std::vector<SelKey>& keys, int ds, int dr);

    // ---- whole track
    void rotate(int dir) noexcept;                       // rotateLane without a selection (shiftSel first)
    bool transpose(int semis) noexcept;                  // transposeLane without a selection: all or nothing
    void clear() noexcept;                               // clearLane
    bool hasNotes() const noexcept;                      // laneHasNotes
    // Block of steps a..b (clamped to the 256 steps), all the rows or a single one; paste from step `at`, cut at the
    // track's length.
    BlockClip blockCopy(int a, int b, std::optional<Row> row) const noexcept;
    void blockClear(int a, int b, std::optional<Row> row) noexcept;
    void blockPaste(int at, const BlockClip& clip) noexcept;

    // ---- recording (stepRecord, recordNote)
    // MIDI note (0-127, Snap applied; percussion: pitch class folded onto the rows 0-7) written at step s, the
    // velocity (0-127) giving VOL 1-15.
    void recordAt(int s, int note, int velocity) noexcept;

private:
    int transposedRow(int row, int semis) const noexcept;  // row + semis under Snap (snapTransposed)
    void clearNotes(const std::vector<RectNote>& notes) noexcept;
    // Notes taken away then written back ds steps and dr semitones further (rectMove, moveSel), in `dst`; all or
    // nothing: false, the track unchanged, outside the pattern or the range, or on a note in the way.
    bool moveNotes(const std::vector<RectNote>& src, int ds, int dr, std::vector<RectNote>& dst);

    Lane& l_;
    EditContext c_;
};

// Step s of a grid whose steps are multiplied by num / den (D-448), rounded half up: a resolution change that keeps
// the time of the notes (1/16 to 1/8: num 1, den 2).
inline int fitStep(int s, int num, int den) noexcept { return (2 * s * num + den) / (2 * den); }

// The track at a resolution num / den times finer (D-448), the notes keeping their time: the root at step s moves to
// fitStep(s) with the rows of its step and ends at fitStep of its end (one step at least, before the next note); a
// chord voice the same. When two notes land on the same step, the first one stays and the other is dropped with its
// step; the rows of the steps without a root then move the same way, into the cells still empty. What lands beyond
// the 256 steps is dropped.
Lane fitLane(const Lane& src, int num, int den);

// Rows that follow a root moved in the grid, as with Copy and Paste (VOL, SRC, GEN, INS, SLD, CRV); a drum hit also
// takes its OCT and TON. Order of Moving::under.
inline constexpr std::array<Row, 8> kNoteRows = {Row::Vol,   Row::Mix, Row::Fx,  Row::Ins,
                                                 Row::Slide, Row::Slc, Row::Oct, Row::Ton};
inline constexpr int kDrumNoteRows = 8, kMelodicNoteRows = 6;

// Rows copied by the track rotation (rotateLane): not PAN, RTG, CND, CRV, TON nor the chord voices. INS (M6) rotates
// with GEN (D-147), the CMD rows with their line (D-150, D-330, D-338; Gxx with VOL, Kxx and Qxx with their slides,
// D-393, D-394), except the retired Xyz and Pxy (D-340, D-344); the BPM row belongs to the pattern (twelfth DAW test,
// item 150).
inline constexpr std::array<Row, 30> kRotatedRows = {
    Row::Pat, Row::Tie, Row::Slide, Row::Afl, Row::Vol, Row::Mix, Row::Fx, Row::Slt, Row::Cut, Row::Oct, Row::Gat,
    Row::Ins, Row::CmdSpeed, Row::CmdRetrig, Row::CmdSync, Row::CmdVibrato, Row::CmdVolSlide,
    Row::CmdDelay, Row::CmdCut, Row::CmdEnvSlide, Row::CmdNoiseSlide, Row::CmdShape, Row::CmdOffset, Row::CmdFine,
    Row::CmdLfo, Row::CmdMod, Row::CmdArpeggiator, Row::CmdVolGlide, Row::CmdEnvCurve, Row::CmdNoiseCurve};

}  // namespace stym::ymseq
