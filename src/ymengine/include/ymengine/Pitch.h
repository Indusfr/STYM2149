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

// Frequency of A4 (MIDI note 69), the reference of every note: 440 Hz by default, set from 350 to 480 Hz (Settings >
// Tuning A4; Indus, 25/10/2026 19:29; below 400 Hz, 21:50).
inline constexpr double kA4Hz = 440.0, kA4MinHz = 350.0, kA4MaxHz = 480.0;

// Frequency of a MIDI note in equal temperament from A4 at `a4` Hz, shifted by bend semitones.
double noteHz(double note, double a4 = kA4Hz) noexcept;

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
// scale scaleMask, tonic scaleRoot; without a scale: semitone), semitones tuned from A4 at `a4` Hz. The other curves
// pass f through.
double slideQuantize(double hz, SlideCurve curve, uint16_t scaleMask, uint8_t scaleRoot, double a4 = kA4Hz) noexcept;

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

// Preview of the table of a sound (YM-Tracker Y43): tone period that the engine gives at step `step` (0 to
// 63) of a note `note` played with sound `p`: pitch of the table and PER and FIX rows according to the matrix
// (Table to the square), detune included; without slide, vibrato, wheel, pitch bend or analog randomness. Table
// off: the period of the note. The step is brought within the length of the table. Notes tuned from A4 at `a4` Hz.
uint16_t tablePeriod(const TrackPatch& p, int note, int step, ymcore::MasterClock clock, double a4 = kA4Hz) noexcept;

// Sine-SID wave (YM-Tracker Y41): one volume per bar of the SID wave (16), a sine sampled half a step after the
// middle of each bar, so that no step lies on a trough, drawn on the scale of the volumes 0 to 15 as the bars
// show them (24/10/2026; before, the volumes whose levels in the measured mixing table best approximated a sine,
// which the bars drew as a bounce). Its highest samples are at volume `high`, its lowest at volume `low`, the
// volumes between them following the sine. One cycle spans `period`
// bars (2 to 16; 24/10/2026, 8 by default, two cycles over the 16 bars; 16 is the former single cycle). Each
// `phase` above 8 moves the bars one place to the right (8, the default, puts the cycle's peak on the 2nd and
// 3rd bars at period 8, on the 4th and 5th at period 16). The Steps of the wave keep their number (before:
// 8 volumes, Steps set to 8). The 8 volumes of TAO are not published: TO CONFIRM.
// With `bounce`, the Bounce form of the SID wave (Indus, 24/10/2026 18:41), the former Sine-SID: the
// volumes whose levels in the measured mixing table of ymcore (one channel alone, the other two at volume 0) best
// approximate the sine, from the level of `low` to that of `high`; a sine in the sound, which the bars, drawing the
// volumes, show as a bounce (flat tops, a sharp trough).
std::array<uint8_t, kSidWaveSteps> sineSidWave(int low = 3, int high = 15, int phase = kSidWavePhase,
                                               int period = kSidWavePeriod, bool bounce = false) noexcept;

// The Sine-SID (or the Bounce) between the lowest and highest volumes of `wave` (one volume apart at least), at `phase`
// and `period`: what a bar moved or the Period slider draws while Sine (or Bounce) is chosen.
std::array<uint8_t, kSidWaveSteps> sineSidWaveAt(const std::array<uint8_t, kSidWaveSteps>& wave, int phase, int period,
                                                 bool bounce = false) noexcept;

// The phase (0 to 15) after bar `step` was moved to `volume` (24/10/2026; formerly it moved the highest or
// lowest volume): the one whose sine of `period` bars, between the volumes of `wave`, brings the bar nearest `volume`;
// on a tie, the nearest `phase`, the bars moved to the left rather than to the right between two as near.
int sineSidWavePhase(const std::array<uint8_t, kSidWaveSteps>& wave, int phase, int period, int step, int volume,
                     bool bounce = false) noexcept;

// Tuned envelope: R11-R12 period that makes the envelope play frequency hz. A cycle of 32 steps lasts
// 256 · EP clock periods; the looped triangles ($A, $E) chain two of them per period. Clamped to
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
