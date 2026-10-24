// STYM2149 — ymengine: pitches, slide curves, chords
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Formulas of the prototype (worklet: noteToTP, slideShape, slideQuant, rand; page: CHORDS), same operations in
// the same order (tests [pitch], data taken from the prototype by tools/proto_golden).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "ymcore/ChipConfig.h"
#include "ymengine/Params.h"

namespace stym::ymengine {

// Frequency of a MIDI note (A 440 Hz), shifted by bend semitones.
double noteHz(double note) noexcept;

// Closest tone period R0-R11: round(clock / 16 / f), clamped to 1-4095 (125 000 / f at 2 MHz).
uint16_t tonePeriodFor(double hz, ymcore::MasterClock clock) noexcept;

// Slide curves of the prototype (SLIDE_CURVES).
enum class SlideCurve : uint8_t {
    Linear, Exponential, Logarithmic, SCurve, Stepped, Sawtooth, Wobble,
    Chromatic, ScaleLocked, Overshoot, Bounce, RandomWalk, Custom,
};
inline constexpr int kSlideCurves = 13;

// Profile of the slide at t ∈ [0, 1] (0 = start, 1 = arrival), as a fraction of the interval in semitones.
// seed: draw specific to each slide (Random walk); table: points of the Custom curve.
double slideShape(double t, SlideCurve curve, uint32_t seed, std::span<const float> table) noexcept;

// Pitch quantisation during the slide: Chromatic (nearest semitone) and Scale-locked (nearest degree of the
// scale scaleMask, tonic scaleRoot; without a scale: semitone). The other curves pass f through.
double slideQuantize(double hz, SlideCurve curve, uint16_t scaleMask, uint8_t scaleRoot) noexcept;

// xorshift32 (13, 17, 5) of the prototype (rand of the arpeggio): returns a value in [0, 1) on 16 bits.
struct ProtoXorshift {
    uint32_t state = 0x1234;
    double next() noexcept {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return double(state & 0xffffu) / 65536.0;
    }
};

// Preview of the table of a sound (YM-Tracker Y43, D-148): tone period that the engine gives at step `step` (0 to
// 63) of a note `note` played with sound `p`: pitch of the table and PER and FIX rows according to the matrix
// (Table to the square), detune included; without slide, vibrato, wheel, pitch bend or analog randomness. Table
// off: the period of the note. The step is brought within the length of the table.
uint16_t tablePeriod(const TrackPatch& p, int note, int step, ymcore::MasterClock clock) noexcept;

// Sine-SID wave (YM-Tracker Y41, D-151): 8 volumes whose levels in the measured mixing table of ymcore (one
// channel alone, the other two at volume 0) best approximate a sine going from volume 0 to volume 15, starting
// from the middle. The 8 volumes of TAO are not published: TO CONFIRM.
std::array<uint8_t, 8> sineSidWave() noexcept;

// Tuned envelope (D-152): R11-R12 period that makes the envelope play frequency hz. A cycle of 32 steps lasts
// 256 · EP clock periods (D-010); the looped triangles ($A, $E) chain two of them per period. Clamped to
// 1-65535; 0 if hz is not positive.
int envelopeRamps(int shape) noexcept;  // cycles of 32 steps per period of the shape: 2 for $A and $E, otherwise 1
uint16_t tunedEnvPeriod(double hz, int shape, ymcore::MasterClock clock) noexcept;

// The 39 chords of the Chord… menu (CHORDS): offsets in semitones.
struct Chord {
    std::string_view name;
    std::array<int8_t, 6> offsets;
    uint8_t count;  // offsets used (2 to 6)
};
extern const std::array<Chord, 39> kChords;

}  // namespace stym::ymengine
