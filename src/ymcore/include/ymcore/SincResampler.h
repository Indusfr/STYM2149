// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <vector>

namespace stym::ymcore {

// Streaming resampler, Kaiser-windowed sinc, exact rational ratio (D-014).
// Input at inNum / inDen Hz (chip stream at clk/8), output at outRate Hz.
// Output n is the input signal at the instant (n - latency()) / outRate: whole-sample delay.
// prepare() allocates; reset(), push(), pull() do not allocate.
// Positions are exact: the centre of the next output is ipos_ + rem_ / Q_ input samples, advanced by the reduced step
// inNum / Q with integer arithmetic, so the phase never drifts however long the stream (D-039).
class SincResampler {
public:
    // Filter design (D-039): passband, stopband and attenuation give the Kaiser β and the number of taps.
    struct Design {
        double passHz = 0;   // passband kept (0 → 20 kHz from 44.1 kHz up, otherwise 0.45 × output)
        double stopHz = 0;   // start of the stopband (0 → min(output - pass, input / 2))
        double attenDb = 100;  // stopband attenuation
        int phases = 1024;   // phases of the table, linear interpolation between two phases
        // When the frequency ratio has a denominator Q at most equal to this value (2 MHz to 32, 44.1, 48,
        // 88.2, 96, 176.4 or 192 kHz: Q ≤ 441), the table holds the Q exact phases and the computation needs no
        // interpolation (D-053). 0: always interpolate.
        uint32_t maxExactPhases = 2048;
    };

    void prepare(uint64_t inNum, uint64_t inDen, uint32_t outRate, const Design& design);
    void reset() noexcept;
    // Position and history of `other` (prepared with the same ratio and design), without allocation; clearHistory()
    // then makes the input pushed so far silent. Used to start a stream in the middle of another (D-299).
    void copyStateFrom(const SincResampler& other) noexcept;
    void clearHistory() noexcept;

    // Total number of input samples that must have been pushed to compute the next output.
    uint64_t inputNeeded() const noexcept {
        const int64_t need = ipos_ + int64_t(K_) + 1;
        return need > 0 ? uint64_t(need) : 0;
    }
    // Same for the output that follows the next `pulls` calls to pull() (pulls = 0: the next output).
    uint64_t inputNeededAfter(uint64_t pulls) const noexcept {
        const uint64_t carry = (rem_ + pulls * stepRem_) / Q_;
        const int64_t need = ipos_ + int64_t(pulls * stepInt_ + carry) + int64_t(K_) + 1;
        return need > 0 ? uint64_t(need) : 0;
    }
    uint64_t inputPushed() const noexcept { return pushed_; }

    // Appends one input sample. It is stored twice (mirror) so that the window of pull() is always contiguous.
    void push(float x) noexcept {
        const uint32_t i = uint32_t(pushed_) & mask_;
        ring_[i] = x;
        ring_[i + ringSize_] = x;
        ++pushed_;
    }
    // Computes the next output. Precondition: inputPushed() >= inputNeeded().
    float pull() noexcept;

    uint32_t latency() const noexcept { return latency_; }  // in output samples
    int taps() const noexcept { return 2 * K_; }
    double passHz() const noexcept { return passHz_; }
    double stopHz() const noexcept { return stopHz_; }
    double cutoff() const noexcept { return fc_; }        // cut-off normalised to the input
    bool exactPhases() const noexcept { return exact_; }
    double kaiserBeta() const noexcept { return beta_; }

    // Continuous impulse response (for the tests), tau in input samples.
    double impulse(double tau) const noexcept;

private:
    // Moves the centre one output step further: stepInt_ whole inputs plus stepRem_ / Q_, the carry going to ipos_.
    void advance() noexcept {
        ipos_ += int64_t(stepInt_);
        rem_ += stepRem_;
        if (rem_ >= Q_) {
            rem_ -= Q_;
            ++ipos_;
        }
    }

    uint64_t inNum_ = 0, Q_ = 1;           // step = inNum / Q (input samples per output)
    uint64_t stepInt_ = 0, stepRem_ = 0;   // step = stepInt_ + stepRem_ / Q
    int64_t ipos0_ = 0;                    // position of the centre of output 0 (integer part)
    uint64_t rem0_ = 0;                    // and remainder over Q
    int64_t ipos_ = 0;                     // centre of the next output (integer part, negative before the start)
    uint64_t rem_ = 0;                     // and remainder over Q: selects the phase of the table
    uint64_t pushed_ = 0;
    int K_ = 0, P_ = 1024;                 // K: half the number of taps; P: phases of the table
    bool exact_ = false;                   // P_ = Q_: one phase per possible position, no interpolation
    uint32_t latency_ = 0;
    uint32_t ringSize_ = 0, mask_ = 0;     // power of two, larger than one window plus one step
    double passHz_ = 0, stopHz_ = 0, fc_ = 0, beta_ = 0, i0Beta_ = 1;
    std::vector<float> table_;  // (P + 1) × 2K
    std::vector<float> ring_;   // 2 × ringSize (mirror)
};

}  // namespace stym::ymcore
