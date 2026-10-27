// STYM2149 — ymseq: sequencer tables (scales, figures, resolutions, time signatures, names)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Values and labels of the prototype (SCALES, FIGS, RESOS, SIGS, DRUMNAMES, SLIDE_CURVES, EUCLID_NAMES), in the same
// order: the indices are those of the menus and of the state. The 39 chords are those of ymengine (kChords, CHORDS).
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace stym::ymseq {

// Scales (SCALES): "Off" then 25 scales, degrees in semitones from the tonic.
struct Scale {
    std::string_view name;
    std::array<int8_t, 12> degrees;  // the first `count` entries are used, in ascending order
    uint8_t count;  // 0: Off
};
inline constexpr int kScaleCount = 26;
extern const std::array<Scale, kScaleCount> kScales;

// 12-bit mask of the scale's degrees (the prototype's scaleMask); 0 for Off.
uint16_t scaleMask(int scale) noexcept;
// Absolute note in the scale (inScale): always true without a scale.
bool inScale(int absNote, int scale, int root) noexcept;
// Nearest degree, searching downwards first (snapToScale); unchanged without a scale or without Snap.
int snapToScale(int absNote, int scale, int root, bool snap) noexcept;
// Note absNote transposed by semis under Snap: the nearest degree (snapToScale) or, when that is the note itself, the
// next degree the way of the transposition, so that +1 or -1 never leaves a note where it was.
int snapTransposed(int absNote, int semis, int scale, int root, bool snap) noexcept;

// Note figures (FIGS): duration in beats (quarter note = 1), 0 = one step whatever the resolution. 1/64 (D-422), which
// the prototype does not have, comes after 1/32: the figures after it are one index further than in the prototype.
// 1/32T (D-433), the note of the 1/32T resolution, comes last, after the other triplets.
struct Figure {
    std::string_view name;
    double beats;
};
inline constexpr int kFigureCount = 15;
extern const std::array<Figure, kFigureCount> kFigures;
inline constexpr std::array<int, 3> kFigureSeps = {1, 8, 11};  // after Step, before dotted, before triplets

// Grid resolutions (RESOS): steps per quarter note. 1/64 (D-422) and 1/32T (D-433) are added at the end so that the
// saved indices keep their meaning; the menus show 1/64 after 1/32 and 1/32T after 1/16T (kResolutionOrder).
struct Resolution {
    std::string_view name;   // menu label ("1/16  sixteenth")
    std::string_view label;  // button label ("1/16")
    double stepsPerBeat;     // steps per quarter note: 4 for 1/16, 1.5 for 1/4T
};
inline constexpr int kResolutionCount = 9;
extern const std::array<Resolution, kResolutionCount> kResolutions;
inline constexpr std::array<int, kResolutionCount> kResolutionOrder = {0, 1, 2, 3, 7, 4, 5, 6, 8};  // menu order
inline constexpr int kResolutionSep = 5;  // separator before the triplets (position in kResolutionOrder)
inline constexpr int kDefaultResolution = 2;  // 1/16

// Time signatures (SIGS).
struct Signature {
    std::string_view name;
    uint8_t numerator, denominator;
};
inline constexpr int kSignatureCount = 12;
extern const std::array<Signature, kSignatureCount> kSignatures;
inline constexpr int kDefaultSignature = 2;  // 4/4

// Duration of a figure in steps for a resolution (figSteps): round(beats × steps per quarter note), at least 1.
int figureSteps(int figure, int resolution) noexcept;
// The same, not rounded: 0.5 for a thirty-second note in 1/16, 1 for Step.
double figureStepsExact(int figure, int resolution) noexcept;
// Whether a figure lasts a whole number of steps of a resolution, one at least (Step always does). The Note length
// menu greys the others (Indus, 25/10/2026 17:21, D-425).
bool figureFits(int figure, int resolution) noexcept;
// Steps in a beat and in a bar (beatSteps, barSteps). A beat is the signature's denominator note (an eighth in 6/8),
// rounded to whole steps, at least 1.
int beatSteps(int signature, int resolution) noexcept;
int barSteps(int signature, int resolution) noexcept;

// Note names (NOTES_EN, NOTES_LAT): 13 entries, the last one repeats the first (C). English: C, C#…; Latin: Do,
// Do#… (the Note names preference). pitchClass is 0 to 12, clamped.
enum class NoteNames : uint8_t { English, Latin };
std::string_view noteName(int pitchClass, NoteNames names) noexcept;
// Absolute note with its octave (absNoteName): 60 → "C4".
std::string absNoteName(int absNote, NoteNames names);
// Typed note (sixth DAW test, item 66): a name in either naming with its octave ("C#4", "Sol3", "Bb-1", case
// ignored) or a MIDI note number ("61"); nothing outside 0 to 127.
std::optional<int> parseAbsNote(std::string_view text);

// Percussion (DRUMNAMES, DRUMFULL): names of the rows 0-7 of a percussion track, short and full.
extern const std::array<std::string_view, 8> kDrumNames;
extern const std::array<std::string_view, 8> kDrumFullNames;

// Slide curves (SLIDE_CURVES, SLIDE_SHORT) and menu separators (SLIDE_SEPS, a separator before each of these
// indices). The index is the value of the CRV row.
extern const std::array<std::string_view, 13> kSlideCurveNames;
extern const std::array<std::string_view, 13> kSlideCurveShort;
inline constexpr std::array<int, 4> kSlideCurveSeps = {4, 7, 9, 12};

// Known traditional Euclidean rhythms (EUCLID_NAMES, in the prototype's order): name of E(pulses, steps), empty
// otherwise. The table also fills the Rhythms menu of the Euclidean generator (twelfth DAW test, item 157).
struct EuclidRhythm {
    int pulses, steps;
    std::string_view name;
};
extern const std::array<EuclidRhythm, 23> kEuclidRhythms;
std::string_view euclidName(int pulses, int steps) noexcept;

}  // namespace stym::ymseq
