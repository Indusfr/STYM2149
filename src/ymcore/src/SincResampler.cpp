// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Kaiser filter: J. F. Kaiser, "Nonrecursive digital filter design using the I0-sinh window function"
// (1974); formulas for β and the order after Oppenheim & Schafer, Discrete-Time Signal Processing, §7.6.
#include "ymcore/SincResampler.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace stym::ymcore {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Modified Bessel function of the first kind, order 0 (power series), for the Kaiser window.
double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 200; ++k) {
        const double q = x / (2.0 * k);
        term *= q * q;
        sum += term;
        if (term < 1e-21 * sum) break;
    }
    return sum;
}

uint32_t nextPow2(uint32_t v) {
    uint32_t p = 1;
    while (p < v) p <<= 1;
    return p;
}
}  // namespace

void SincResampler::prepare(uint64_t inNum, uint64_t inDen, uint32_t outRate, const Design& design) {
    const double inRate = double(inNum) / double(inDen);
    // Reduced ratio: step = inNum / Q input samples per output.
    const uint64_t g = std::gcd(inNum, inDen * outRate);
    inNum_ = inNum / g;
    Q_ = inDen * outRate / g;
    stepInt_ = inNum_ / Q_;
    stepRem_ = inNum_ % Q_;

    // Full passband (20 kHz) from 44.1 kHz up; below that, 45 % of the output rate.
    // Both bands stay under the input's Nyquist frequency: with a chip clock of 320 kHz or less (a Custom clock, a YM
    // or SNDH tune), 20 kHz is above it, the cut-off went past 0.5 and the per-phase DC normalisation below divided by
    // sums close to zero (gains of about 10^5).
    passHz_ = design.passHz > 0 ? design.passHz : (outRate >= 44100 ? 20000.0 : 0.45 * outRate);
    passHz_ = std::min(passHz_, 0.45 * inRate);
    stopHz_ = design.stopHz > 0 ? design.stopHz : std::min(double(outRate) - passHz_, inRate / 2.0);
    stopHz_ = std::min(stopHz_, inRate / 2.0);
    if (stopHz_ <= passHz_) stopHz_ = std::min(passHz_ * 1.05, inRate / 2.0);
    // Kaiser's formulas: β from the attenuation A (dB), then the order n from A and the transition width dw (radians
    // per input sample). K = half the number of taps; fc_ = cut-off in the middle of the transition band.
    const double A = design.attenDb;
    beta_ = A > 50.0 ? 0.1102 * (A - 8.7) : (A >= 21.0 ? 0.5842 * std::pow(A - 21.0, 0.4) + 0.07886 * (A - 21.0) : 0.0);
    const double dw = 2.0 * kPi * (stopHz_ - passHz_) / inRate;
    const int n = int(std::ceil((A - 8.0) / (2.285 * dw)));
    K_ = std::clamp((n + 1) / 2, 2, 8192);
    exact_ = design.maxExactPhases > 0 && Q_ <= design.maxExactPhases;
    P_ = exact_ ? int(Q_) : std::max(1, design.phases);
    fc_ = 0.5 * (passHz_ + stopHz_) / inRate;
    i0Beta_ = besselI0(beta_);

    // Row p of the table: the 2K taps for an output centred p / P of an input sample after input K - 1 of the window.
    // The interpolated table has one row more (p = P) so that pull() can always read rows p and p + 1.
    const int T = 2 * K_;
    const int rows = exact_ ? P_ : P_ + 1;
    table_.assign(size_t(rows) * size_t(T), 0.0f);
    std::vector<double> row(size_t(T), 0.0);
    for (int p = 0; p < rows; ++p) {
        double sum = 0;
        for (int k = 0; k < T; ++k) {
            row[k] = impulse(double(p) / P_ + (K_ - 1) - k);
            sum += row[k];
        }
        // DC gain exactly one for every phase (no ripple of the DC component).
        for (int k = 0; k < T; ++k) table_[size_t(p) * T + k] = float(row[k] / sum);
    }

    // Smallest whole delay L (in outputs) such that L × step >= K: the window never goes past the known input.
    const uint64_t kq = uint64_t(K_) * Q_;
    latency_ = uint32_t((kq + inNum_ - 1) / inNum_);
    const uint64_t a = uint64_t(latency_) * inNum_;  // centre of output 0 = -a / Q
    // -a / Q split into a whole part rounded down (-ceil(a / Q)) and a non-negative remainder over Q.
    const uint64_t c = (a + Q_ - 1) / Q_;
    ipos0_ = -int64_t(c);
    rem0_ = c * Q_ - a;

    ringSize_ = nextPow2(uint32_t(T) + uint32_t(stepInt_) + 16);
    mask_ = ringSize_ - 1;
    ring_.assign(size_t(ringSize_) * 2, 0.0f);
    reset();
}

void SincResampler::reset() noexcept {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    pushed_ = 0;
    ipos_ = ipos0_;
    rem_ = rem0_;
}

void SincResampler::copyStateFrom(const SincResampler& other) noexcept {
    std::copy(other.ring_.begin(), other.ring_.end(), ring_.begin());
    pushed_ = other.pushed_;
    ipos_ = other.ipos_;
    rem_ = other.rem_;
}

void SincResampler::clearHistory() noexcept { std::fill(ring_.begin(), ring_.end(), 0.0f); }

// Sinc of cut-off fc_ times the Kaiser window, zero from K input samples away.
double SincResampler::impulse(double tau) const noexcept {
    const double r = tau / K_;
    if (std::fabs(r) >= 1.0) return 0.0;
    const double w = besselI0(beta_ * std::sqrt(1.0 - r * r)) / i0Beta_;
    const double x = 2.0 * fc_ * tau;
    const double s = std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
    return 2.0 * fc_ * s * w;
}

float SincResampler::pull() noexcept {
    // Window of inputs ipos_ - K + 1 to ipos_ + K around the centre, contiguous thanks to the mirrored ring.
    const int T = 2 * K_;
    const uint32_t start = uint32_t(uint64_t(ipos_ - int64_t(K_) + 1)) & mask_;
    const float* x = ring_.data() + start;
    if (exact_) {
        // Exact phase rem / Q: a single row of the table.
        const float* h = table_.data() + size_t(rem_) * size_t(T);
        // Eight partial sums in separate variables: the compiler keeps them in two vector registers.
        float a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
        int k = 0;
        for (; k + 8 <= T; k += 8) {
            a0 += x[k] * h[k];
            a1 += x[k + 1] * h[k + 1];
            a2 += x[k + 2] * h[k + 2];
            a3 += x[k + 3] * h[k + 3];
            a4 += x[k + 4] * h[k + 4];
            a5 += x[k + 5] * h[k + 5];
            a6 += x[k + 6] * h[k + 6];
            a7 += x[k + 7] * h[k + 7];
        }
        float s = 0;
        for (; k < T; ++k) s += x[k] * h[k];
        s += a0;
        s += a1;
        s += a2;
        s += a3;
        s += a4;
        s += a5;
        s += a6;
        s += a7;
        advance();
        return s;
    }
    // Phase rem / Q falls between rows p and p + 1 of the table: both dot products, then a linear blend by frac.
    const uint64_t pf = rem_ * uint64_t(P_);
    const uint64_t p = pf / Q_;
    const float frac = float(double(pf % Q_) / double(Q_));
    const float* h0 = table_.data() + size_t(p) * size_t(T);
    const float* h1 = h0 + T;

    // Eight partial sums: instruction-level parallelism without global reassociation.
    float a0[8] = {}, a1[8] = {};
    int k = 0;
    for (; k + 8 <= T; k += 8)
        for (int j = 0; j < 8; ++j) {
            a0[j] += x[k + j] * h0[k + j];
            a1[j] += x[k + j] * h1[k + j];
        }
    float s0 = 0, s1 = 0;
    for (; k < T; ++k) {
        s0 += x[k] * h0[k];
        s1 += x[k] * h1[k];
    }
    for (int j = 0; j < 8; ++j) {
        s0 += a0[j];
        s1 += a1[j];
    }

    advance();
    return s0 + frac * (s1 - s0);
}

}  // namespace stym::ymcore
