// STYM2149 — ymseq: pattern bank, Song and sequencer settings of the tracks
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Sequencer state of the prototype outside the patch: PATS (32 patterns, 4 banks × 8), curPat, pendPat, viewBank,
// bankLast, SONG, and per track seqOct, gate and plen. The length of the current pattern stands for GLOB.steps.
// The current pattern is edited in place (the prototype edits it in CH[].lane and copies it back into PATS[curPat]).
#pragma once
#include <array>
#include <bitset>
#include <optional>

#include "ymseq/Pattern.h"
#include "ymseq/Song.h"

namespace stym::ymseq {

inline constexpr int kBanks = 4;
inline constexpr int kBankSize = 8;

// Sequencer settings of a track (CH[i].seqOct, gate, plen).
struct TrackSeq {
    int octave = 3;      // seqOct: octave of the grid rows (0-9)
    double gate = 0.5;   // default note duration, as a fraction of a step
    int length = 0;      // plen: 0 = pattern length, 1-64 = own length (polyrhythm)
    bool operator==(const TrackSeq&) const = default;
};

// The sequencer part of the project. It lives in the non-automatable state (plugin::NonParamState::seq), so it is
// edited on the message thread by copy (Host::editExtras) and reaches the audio thread as ymengine::SeqData
// (plugin::fillSequence), which shares the tracks (LanePtr) instead of copying their steps.
class Project {
public:
    std::array<Pattern, kPatterns> patterns;
    std::bitset<kPatterns> used;  // pattern created (PATS[i] not null); the current one always is
    int current = 0;              // curPat
    int pending = -1;             // pendPat: switch expected at the end of the loop
    int viewBank = 0;             // bank (0-3) whose 8 pattern buttons the Step Sequencer shows
    std::array<int, kBanks> bankLast{0, 8, 16, 24};  // last pattern chosen in each bank
    Song song;
    std::array<TrackSeq, kTracks> tracks;

    Project() { used.set(0); }

    Pattern& cur() noexcept { return patterns[size_t(current)]; }
    const Pattern& cur() const noexcept { return patterns[size_t(current)]; }
    int steps() const noexcept { return cur().steps; }  // GLOB.steps
    void setSteps(int n) noexcept;                      // of the current pattern, brought into 1-256
    // Length of a track as played and edited (plen || GLOB.steps).
    int trackLength(int t) const noexcept;

    // Creates the pattern if it does not exist yet (emptyPattern: length of the current pattern).
    void ensure(int idx);
    // Choice of a pattern (selectPattern): during playback, it is made pending and the player switches at the end of
    // the loop; when stopped, it becomes current. False if nothing changes.
    bool select(int idx, bool playing);
    // The player has switched to this pattern (message "pa"): it becomes current.
    void applied(int idx);

    // Menu of the pattern buttons (patCopy, patCut, patPaste).
    Pattern copyPattern(int idx);
    void cutPattern(int idx);
    void pastePattern(int idx, const Pattern& p);
    // Transposition of a pattern (transposePattern): melodic tracks only, notes clipped to octaves 0-9. Deviations:
    // octave 0 is read and rewritten correctly and the chord voices follow the root.
    void transposePattern(int idx, int semis, const std::bitset<kTracks>& drumTracks);

    // The sequencer at a resolution num / den times finer, the notes keeping their time: every created
    // pattern (fitLane on each track, its BPM row and its length, 1-256), the tracks' own lengths (1-64) and the Rest
    // and Note-off entries of the Song (1-256 steps).
    void fitResolution(int num, int den);
    // A created pattern holds a note (root or chord voice) on one of its tracks.
    bool hasNotes() const noexcept;

    // Empties track t of the current pattern if it is stored empty (after an edit).
    void compact() noexcept { cur().compact(); }
};

}  // namespace stym::ymseq
