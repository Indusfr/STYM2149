// STYM2149 — ymseq: Euclidean generator (Bjorklund's algorithm, rotation, accents)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Port of the prototype's "Euclidean generator" box: pattern of S steps (2-32) and K hits, shifted by R steps,
// accents spread over the hits by a second Euclidean figure. The pattern repeats over the whole track.
#pragma once
#include <vector>

#include "ymseq/Edit.h"

namespace stym::ymseq {

inline constexpr int kEuclidMinSteps = 2, kEuclidMaxSteps = 32;

// Maximally even distribution of K hits over N steps (the prototype's bjorklund, same concatenation order).
// Returns N cells, 1 = hit, the first cell always a hit when K > 0.
std::vector<int> bjorklund(int pulses, int steps);

// Figure shown by the generator and written by euclidApply, with the values as euclidFigure clamped them: S = steps,
// K = pulses, R = rotate.
struct EuclidFigure {
    int steps = 16, pulses = 4, rotate = 0;
    std::vector<int> hits;     // S cells, 1 = hit
    std::vector<int> accents;  // empty without accents, otherwise S cells
};
// Current pattern (figure()): S limited to 2-32, K to S, R modulo S, accents limited to K.
EuclidFigure euclidFigure(int steps, int pulses, int rotate, int accents);

// What the generator writes: a pitch (melodic track), an instrument (Chipdrum) or a slot (Digidrum, 0 = the track's
// own, EditContext::digidrumSlot; 1-8 = slot). D-103: the slot becomes the row played (the prototype wrote row 0 and
// the slot in SLT, which playback ignores); SLT is still written as in the prototype.
enum class EuclidTarget { Melodic, Chipdrum, Digidrum };

// Apply: without Merge, the track (current length) is first emptied of its notes, ties and chord voices (and of SLT
// for Digidrum); then each hit writes the note, VOL = 15 on an accent and 10 otherwise.
// `value`: the row of the pitch (melodic, relative to the track's octave, Snap applied), the instrument row (Chipdrum)
// or the slot (Digidrum, see EuclidTarget). `editor` works on `lane`, with the length of the track.
void euclidApply(LaneEditor& editor, Lane& lane, const EuclidFigure& f, EuclidTarget target, int value, bool merge);
// True if Apply would take away or change a note already written in the track (its pitch, octave, hold or a chord
// voice): Apply then asks first (twelfth DAW test, item 155).
bool euclidReplacesNotes(const Lane& lane, const EditContext& ctx, const EuclidFigure& f, EuclidTarget target,
                         int value, bool merge);

}  // namespace stym::ymseq
