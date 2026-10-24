// STYM2149 — ymengine: pitches, slide curves, chords
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymengine/Pitch.h"

#include <algorithm>
#include <cmath>

#include "ymcore/Ym2149.h"

namespace stym::ymengine {

namespace {
constexpr double kPi = 3.141592653589793;
double jsRound(double x) noexcept { return std::floor(x + 0.5); }  // JavaScript Math.round (half-way towards +∞)
}  // namespace

double noteHz(double note) noexcept { return 440.0 * std::pow(2.0, (note - 69) / 12); }

std::array<uint8_t, 8> sineSidWave() noexcept {
    // Output level of volume v on channel A alone (5-bit level 2v + 1, D-011; B and C at volume 0).
    auto amp = [](int v) { return double(ymcore::tables::kMixTable[(2 * v + 1) | (1 << 5) | (1 << 10)]); };
    const double lo = amp(0), hi = amp(15);
    std::array<uint8_t, 8> out{};
    for (int k = 0; k < 8; ++k) {
        const double want = lo + (hi - lo) * (1 + std::sin(2 * kPi * k / 8)) / 2;
        int best = 0;
        for (int v = 1; v < 16; ++v)
            if (std::fabs(amp(v) - want) < std::fabs(amp(best) - want)) best = v;
        out[size_t(k)] = uint8_t(best);
    }
    return out;
}

int envelopeRamps(int shape) noexcept {
    const int s = shape & 15;
    return s == 10 || s == 14 ? 2 : 1;
}

uint16_t tunedEnvPeriod(double hz, int shape, ymcore::MasterClock clock) noexcept {
    if (!(hz > 0)) return 0;
    return uint16_t(std::clamp(jsRound(clock.hz() / (256.0 * envelopeRamps(shape) * hz)), 1.0, 65535.0));
}

uint16_t tonePeriodFor(double hz, ymcore::MasterClock clock) noexcept {
    if (!(hz > 0)) return 4095;
    return uint16_t(std::clamp(jsRound(clock.hz() / 16 / hz), 1.0, 4095.0));
}

// Constants and formulas of SLIDE_CURVES of the prototype, unchanged. Some curves leave [0, 1] on purpose: Overshoot
// passes the target before settling, Bounce swings around it with a damped cosine.
double slideShape(double t, SlideCurve curve, uint32_t seed, std::span<const float> table) noexcept {
    switch (curve) {
    case SlideCurve::Exponential:
        return 0.7 * std::pow(t, 2.2) + 0.3 * t;
    case SlideCurve::Logarithmic: {  // leaps, slows down, settles before the end
        const double u = t / 0.78;
        if (u >= 1) return 1;
        return 0.7 * (1 - std::pow(1 - u, 2.2)) + 0.3 * u;
    }
    case SlideCurve::SCurve: {
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        const double w = std::pow(t, 0.80);
        const double a = std::pow(w, 3.5), b = std::pow(1 - w, 2.2);
        return a / (a + b);
    }
    case SlideCurve::Stepped:  // six plateaus, the last one on the target
        return std::min(1.0, std::floor(t * 6 + 1e-9) / 5);
    case SlideCurve::Sawtooth: {  // four rises, each followed by a plateau
        const double n = 4, u = t * n, i = std::floor(u), fr = u - i;
        return std::min(1.0, (i + std::min(1.0, fr * 1.6)) / n);
    }
    case SlideCurve::Wobble:
        return std::max(0.0, std::min(1.0, t + 0.1 * std::sin(t * kPi * 6)));
    case SlideCurve::Overshoot: {
        if (t >= 1) return 1;
        const double c = 1.9, u = t - 1;
        return 1 + (c + 1) * u * u * u + c * u * u;
    }
    case SlideCurve::Bounce: {
        if (t >= 1) return 1;
        const double d = std::exp(-4.5 * t);
        return 1 - d * std::cos(t * kPi * 3.2);
    }
    case SlideCurve::RandomWalk: {
        // One draw per 24th of the slide, its deviation shrinking to zero at the arrival (× (1 − t²)).
        // JavaScript 32-bit operations: (seed ^ (i × 2654435761)) | 0, then xorshift.
        const double i = std::floor(t * 24);
        const uint32_t mul = uint32_t(uint64_t(int64_t(i) * 2654435761LL) & 0xffffffffu);
        uint32_t x = (seed ? seed : 1u) ^ mul;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        const double r = double(x) / 4294967296.0 - 0.5;
        return std::max(0.0, std::min(1.0, t + r * 0.85 * (1 - t * t)));
    }
    case SlideCurve::Custom: {
        if (table.empty()) return t;
        const double x = std::max(0.0, std::min(1.0, t)) * double(table.size() - 1);
        const double i = std::floor(x), fr = x - i;
        const size_t k = size_t(i);
        const double a = table[k], b = table[std::min(table.size() - 1, k + 1)];
        return a + (b - a) * fr;
    }
    case SlideCurve::Linear:
    case SlideCurve::Chromatic:
    case SlideCurve::ScaleLocked:
        break;
    }
    return t;
}

double slideQuantize(double hz, SlideCurve curve, uint16_t scaleMask, uint8_t scaleRoot) noexcept {
    if (curve != SlideCurve::Chromatic && curve != SlideCurve::ScaleLocked) return hz;
    if (!(hz > 0)) return hz;
    int n = int(jsRound(69 + 12 * std::log2(hz / 440)));  // nearest MIDI note
    // Scale-locked: the nearest degree of the scale, below first at the same distance (6 semitones at most).
    if (curve == SlideCurve::ScaleLocked && scaleMask) {
        auto in = [&](int k) { return (scaleMask >> ((((k - int(scaleRoot)) % 12) + 12) % 12)) & 1; };
        for (int d = 0; d <= 6; ++d) {
            if (in(n - d)) {
                n -= d;
                break;
            }
            if (in(n + d)) {
                n += d;
                break;
            }
        }
    }
    return 440.0 * std::pow(2.0, (n - 69) / 12.0);
}

const std::array<Chord, 39> kChords = {{
    {"Major", {0, 4, 7}, 3},
    {"Minor", {0, 3, 7}, 3},
    {"Sus2", {0, 2, 7}, 3},
    {"Sus4", {0, 5, 7}, 3},
    {"Diminished", {0, 3, 6}, 3},
    {"Augmented", {0, 4, 8}, 3},
    {"Major 7", {0, 4, 7, 11}, 4},
    {"Minor 7", {0, 3, 7, 10}, 4},
    {"Dominant 7", {0, 4, 7, 10}, 4},
    {"Minor maj 7", {0, 3, 7, 11}, 4},
    {"Half-dim 7", {0, 3, 6, 10}, 4},
    {"Diminished 7", {0, 3, 6, 9}, 4},
    {"Aug 7", {0, 4, 8, 10}, 4},
    {"7 sus4", {0, 5, 7, 10}, 4},
    {"6th", {0, 4, 7, 9}, 4},
    {"Minor 6", {0, 3, 7, 9}, 4},
    {"Add9", {0, 4, 7, 14}, 4},
    {"Minor add9", {0, 3, 7, 14}, 4},
    {"6/9", {0, 4, 7, 9, 14}, 5},
    {"Major 9", {0, 4, 7, 11, 14}, 5},
    {"Minor 9", {0, 3, 7, 10, 14}, 5},
    {"Dominant 9", {0, 4, 7, 10, 14}, 5},
    {"7b9", {0, 4, 7, 10, 13}, 5},
    {"7#9", {0, 4, 7, 10, 15}, 5},
    {"Minor 11", {0, 3, 7, 10, 14, 17}, 6},
    {"Dominant 13", {0, 4, 7, 10, 14, 21}, 6},
    {"Major 1st inv", {0, 3, 8}, 3},
    {"Major 2nd inv", {0, 5, 9}, 3},
    {"Minor 1st inv", {0, 4, 9}, 3},
    {"Minor 2nd inv", {0, 5, 8}, 3},
    {"Major spread", {0, 7, 16}, 3},
    {"Minor spread", {0, 7, 15}, 3},
    {"Power", {0, 7, 12}, 3},
    {"Power + oct", {0, 7, 12, 19}, 4},
    {"Fifth", {0, 7}, 2},
    {"Fourth", {0, 5}, 2},
    {"Tritone", {0, 6}, 2},
    {"Octave", {0, 12}, 2},
    {"Double octave", {0, 12, 24}, 3},
}};

uint16_t tablePeriod(const TrackPatch& p, int note, int step, ymcore::MasterClock clock) noexcept {
    // Same operations as the engine (vblEvent, matrixPitch): period of the note (or of the fixed note), scaled by
    // the semitones of the table, rounded, then the PER offset.
    const double detune = std::clamp(p.detuneCents, -double(kDetuneMaxCents), double(kDetuneMaxCents)) / 100;
    const double tpNote = tonePeriodFor(noteHz(double(note) + detune), clock);
    const bool drum = p.generator == Generator::Digidrum || p.generator == Generator::Chipdrum;
    if (!p.ornamentOn || p.ornamentLength == 0 || drum) return uint16_t(tpNote);
    const size_t s = size_t(std::clamp(step, 0, std::min<int>(p.ornamentLength, kOrnamentSteps) - 1));
    if (!(p.pitchMatrix & pitchBit(PitchSource::Table, PitchTarget::Square))) return uint16_t(tpNote);
    const bool fix = p.rows.fix[s] != 0;
    const double tp0 = fix ? tonePeriodFor(noteHz(double(std::min<int>(p.fixedNote, 127)) + detune), clock) : tpNote;
    const double tp = std::floor(tp0 * std::pow(2.0, -double(p.ornamentPitch[s]) / 12) + 0.5) + p.rows.period[s];
    return uint16_t(std::clamp(tp, 1.0, 4095.0));
}

}  // namespace stym::ymengine
