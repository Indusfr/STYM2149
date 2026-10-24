// STYM2149 — ymseq: sequencer patterns (tracks, rows per step, chord voices)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The prototype's schema (LANEKEYS, POLYKEYS, snapPattern): a pattern has a length (1 to 256 steps) and one track
// (lane) per channel, each with its rows of 256 steps:
//   pat   note relative (0-12) to the track's octave, or percussion row (0-7); -1 = nothing
//   oct   octave of the note: 0 = the track's octave (seqOct), -1 = octave 0, 1-9 = octave; percussion: OCT -3..+3
//   tie   tie (HLD); slide (SLD); afl (ARP); vol 0-15 (default 15); mix (SRC) 0-3; slt (SLT) 0-8;
//   cut; fx (GEN) 0-4; gat (GAT) 0-100 %; pnr (PAN) 0 or v·10+101 (v from -10 to +10); rtg (RTG) 0-3;
//   cnd (CND) 0-7; slc (CRV) 0-12; ton (TON) -12..+12
//   ins (INS, M6) 0 = the track's sound, 1-32 = project instrument (D-147)
//   cmd (CMD, M6) effect commands of the line, one row per kind (Xyz, Sxx, Rxx, Zxx, Txx, Vxx, D-330; Axy, Pxy, Dxx,
//       Cxx, Bxy, Nxy, Exx, Oxx, Fxx, D-338: a step can hold one of each), letter × 256 + xx (ymengine::seqCmd,
//       D-150); 0 = none. The letter counts as its index in ymengine::kSeqCmdLetters (1 = X to 15 = F), not as
//       its character code. Xyz and Pxy are retired (D-340, D-344: their rows stay empty); Vxx, Txx, Exx, Dxx and
//       Cxx are edited in rows of their own (VIB, BPM, ENV, DLY, CUT; D-341, D-342, D-347 to D-349)
//   pat2-pat6, oct2-oct6, dur2-dur6: chord voices 2 to 6 (duration in steps, 0 = one step)
// Empty tracks are not stored: a shared track (shared_ptr<const Lane>) is only copied when it is modified, which
// makes undo snapshots and publishing to the audio thread cheap. The rows are those of ymengine::SeqLane (what the
// engine plays); Lane adds the editing functions to it.
#pragma once
#include <array>
#include <cstdint>
#include <memory>

#include "ymengine/Sequence.h"

namespace stym::ymseq {

inline constexpr int kTracks = 16;
inline constexpr int kSteps = 256;       // NSTEP
inline constexpr int kPolyMax = 6;       // POLYMAX: voices per step (root + 5)
inline constexpr int kExtraVoices = kPolyMax - 1;
inline constexpr int kPatterns = 32;     // 4 banks × 8
inline constexpr int kDefaultSteps = 16;
inline constexpr int kPlayLow = 23;      // PLAY_LO: B0, 12-bit period saturated below
inline constexpr int kMapHigh = 120;     // MAP_HI: C9
inline constexpr int kPanCenter = 101;   // pnr = v·10 + 101
static_assert(kSteps == ymengine::kSeqSteps && kExtraVoices == ymengine::kSeqChordVoices &&
              kPatterns == ymengine::kSeqPatterns && kTracks == ymengine::kTracks);

// Rows per step (stable identifiers: order of LANEKEYS, then the rows of M6; used by the state and the
// clipboards).
// The eighteen CMD rows follow the kinds of ymengine::SeqCmd, Arp to Arpeggiator (D-330, D-338; twelfth DAW test,
// items 142 to 144). Afl, the ARP flags of the prototype, is read from older states and files only, and turned into Jxx
// (migrateArpFlags).
enum class Row : uint8_t {
    Pat, Tie, Slide, Afl, Vol, Mix, Slt, Cut, Fx, Oct, Gat, Pnr, Rtg, Cnd, Slc, Ton, Ins,
    CmdArp, CmdSpeed, CmdRetrig, CmdSync, CmdTempo, CmdVibrato,
    CmdVolSlide, CmdPitchSlide, CmdDelay, CmdCut, CmdEnvSlide, CmdNoiseSlide, CmdShape, CmdOffset, CmdFine,
    CmdLfo, CmdMod, CmdArpeggiator
};
inline constexpr int kProtoRows = 16;  // rows of the prototype (LANEKEYS)
inline constexpr int kRowCount = 35;
// CMD row of a kind of command, kind of a CMD row (None: another row).
inline constexpr Row cmdRow(ymengine::SeqCmd k) noexcept { return Row(int(Row::CmdArp) + int(k) - 1); }
inline constexpr bool isCmdRow(Row r) noexcept { return r >= Row::CmdArp && r <= Row::CmdArpeggiator; }
inline constexpr ymengine::SeqCmd cmdKind(Row r) noexcept {
    return isCmdRow(r) ? ymengine::SeqCmd(int(r) - int(Row::CmdArp) + 1) : ymengine::SeqCmd::None;
}
static_assert(int(Row::CmdArpeggiator) - int(Row::CmdArp) + 1 == ymengine::kSeqCmdKinds &&
              int(Row::CmdArpeggiator) + 1 == kRowCount);
// The CMD row on screen: the kinds of the Commands box together, Row::CmdArp (retired) standing for them in the block
// and row operations; the rows of Vxx, Txx, Exx, Dxx and Cxx are rows of their own (VIB, BPM, ENV, DLY, CUT; D-341,
// D-342, D-347 to D-349).
inline constexpr bool isCmdBoxRow(Row r) noexcept { return isCmdRow(r) && !ymengine::seqCmdOwnRow(cmdKind(r)); }
// The ENV row shows Exx and Rxx (twelfth DAW test, item 146): its block and row operations carry the two.
inline constexpr Row envPartner(Row r) noexcept { return r == Row::CmdShape ? Row::CmdRetrig : r; }
inline constexpr int kInstrumentMax = ymengine::kInstruments;  // INS: 1 to 32 ($01 to $20)

// One track of a pattern. Once stored in a Pattern it is shared and treated as immutable (LanePtr): it is changed only
// through Pattern::edit, which copies it first when anything else (undo step, copy published to the audio thread)
// still holds it. LaneEditor (Edit.h) holds the editing rules of the grid.
struct Lane : ymengine::SeqLane {
    bool operator==(const Lane&) const = default;

    // Value of a row (pat, oct and ton signed; cmd from 0 to ymengine::kSeqCmdMax; the others from 0 to 255) and its
    // empty value. A command written in a CMD row goes to the row of its kind (a command read from a state or a file
    // made before D-330, when one row held every kind); 0 or a command of kind None empties the row.
    int get(Row r, int s) const noexcept;
    void set(Row r, int s, int v) noexcept;
    static int emptyValue(Row r) noexcept { return r == Row::Pat ? -1 : r == Row::Vol ? 15 : 0; }
    // Empties step s of all the rows (chord voices included).
    void clearStep(int s) noexcept;
    // Commands of the CMD row at step s (the kinds of the Commands box): taken from step ss of src, or emptied.
    void copyBoxCmds(int s, const Lane& src, int ss) noexcept;
    void clearBoxCmds(int s) noexcept;
    bool empty() const noexcept;
    // Step holding a note (root or chord voice) among the first n.
    bool hasNotes(int n = kSteps) const noexcept;

    // Voice v (1 = root, 2..6 = chord voices).
    int16_t& vp(int v, int s) noexcept { return v == 1 ? pat[size_t(s)] : vpat[size_t(v - 2)][size_t(s)]; }
    int16_t vp(int v, int s) const noexcept { return v == 1 ? pat[size_t(s)] : vpat[size_t(v - 2)][size_t(s)]; }
    int8_t& vo(int v, int s) noexcept { return v == 1 ? oct[size_t(s)] : voct[size_t(v - 2)][size_t(s)]; }
    int8_t vo(int v, int s) const noexcept { return v == 1 ? oct[size_t(s)] : voct[size_t(v - 2)][size_t(s)]; }
};

using LanePtr = std::shared_ptr<const Lane>;

// Shared empty track (never modified).
const LanePtr& emptyLane();

// A pattern: its length and its 16 tracks. Copying one copies 16 pointers, not the steps (undo steps, clipboard,
// publishing). Edited on the message thread only.
struct Pattern {
    int steps = kDefaultSteps;          // 1 to 256
    std::array<LanePtr, kTracks> lanes;  // nullptr = empty track
    // BPM row, shared by the tracks (twelfth DAW test, item 150; ymengine::SeqPattern::bpm): 0 or 32 to 255.
    std::array<uint8_t, kSteps> bpm{};

    const Lane& lane(int track) const noexcept;
    // Track to modify: copied if it is shared (or created if it is empty).
    Lane& edit(int track);
    // Sets the tracks that have become empty back to nullptr.
    void compact() noexcept;
    // Same length and same steps (a null track equals an empty one); shared tracks are not compared step by step.
    bool sameContent(const Pattern& o) const noexcept;
    // Moves the Txx that the tracks hold (CMD and BPM rows before the twelfth DAW test) to the BPM row of the pattern,
    // at their step; the highest track wins, as in the engine. False if there was none.
    bool migrateTempo();
};

// Turns the ARP flags of a track (Afl, armed step by step before the twelfth DAW test, item 144) into Jxx switches: J01
// where a run of flags starts, J00 at the step after it and at step 0 when it is not flagged, so that each loop starts
// as before. The flags are emptied. False if there was none.
bool migrateArpFlags(ymengine::SeqLane& l);
// The other way, for the files of the prototype: the track with its Jxx written as ARP flags when the flags give them
// back exactly (migrateArpFlags), so that the prototype plays them; otherwise the track as it is.
Lane withArpFlags(const Lane& l);

using PatternPtr = std::shared_ptr<const Pattern>;

// Stored octave → played octave (octOf): -1 = octave 0, 0 = the track's octave.
inline int octaveOf(int stored, int trackOctave) noexcept { return stored == -1 ? 0 : (stored ? stored : trackOctave); }
// Played octave → stored value (octCode).
inline int octaveCode(int octave, int trackOctave) noexcept {
    if (octave == trackOctave) return 0;
    return octave == 0 ? -1 : octave;
}
// Absolute note of voice v at step s (chanPayload, polyAbs), MIDI numbering ((octave + 1) × 12 + note, 60 = C4);
// -1 if none.
int absoluteNote(const Lane& l, int v, int s, int trackOctave) noexcept;

// Labels of the banks and of the patterns (A1 … D8).
char bankLetter(int pattern) noexcept;

}  // namespace stym::ymseq
