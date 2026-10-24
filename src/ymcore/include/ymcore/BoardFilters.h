// STYM2149 — ymcore: analogue filters of the board (ST and STE output chains, §3.2)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Component values taken from Hatari (sound.c and dmaSnd.c, analyses by David Savinkoff): **TO CONFIRM**
// on hardware (D-006: no recording of a real ST). Only the values are reused, not the code (GPL).
#pragma once
#include <cstdint>

namespace stym::ymcore {

// Analogue chain after the chip (D-047). The monitor has no source with figures: not offered (D-048).
enum class BoardChain : uint8_t {
    Raw,  // chip output, no board filter. Default.
    ST,   // STF: capacitor C10 (asymmetric low-pass) then output coupling (high-pass)
    STE,  // STE: no C10; output coupling then LMC1992 bass and treble settings
};

// Component values of the chain, turned into filters by ChipRenderer::prepare and TickStreamOutput::prepare.
struct BoardConfig {
    BoardChain chain = BoardChain::Raw;
    // ST: C10 = 0.1 µF charged through the YM output (206.865 Ω, rising) and discharged through the network
    // R8 // R9 // R10 // R12 × hFE (759.1 Ω, falling): cut-offs 7 693.7 Hz and 2 096.6 Hz (Hatari, sound.c).
    double c10Farad = 0.1e-6;
    double pullUpOhm = 206.865;
    double pullDownOhm = 759.1;
    // ST and STE: first-order high-pass of the output coupling, 0 = none (Hatari, Subsonic_IIR_HPF).
    double highPassHz = 13.7;
    // STE: first-order shelves of the LMC1992, -12 to +12 dB in 2 dB steps, 0 = flat (Hatari, dmaSnd.c:
    // 8.2 nF capacitors, turnover points 118.276 Hz and 8 438.756 Hz).
    int bassDb = 0;
    int trebleDb = 0;
    double bassHz = 118.276;
    double trebleHz = 8438.756;
};

// LMC1992 gain brought to the chip's grid: clamped to ±12 dB, then 2 dB steps towards zero (3 → 2, −3 → −2).
int lmc1992Step(int db) noexcept;

// First-order RC low-pass whose resistance depends on the direction: rising when the input exceeds the output
// (the YM charges C10), falling otherwise. The input is constant during an internal step: the update
// y ← x + (y − x) · e^(−T/RC) is exact, and the direction does not change during the step.
class AsymmetricRc {
public:
    // tickHz: rate of the process() calls (the internal step, clk / 8). A zero resistance or capacitance gives no
    // filtering.
    void prepare(double tickHz, double rUpOhm, double rDownOhm, double farad) noexcept;
    void reset() noexcept { y_ = 0; }
    double process(double x) noexcept {
        y_ = x + (y_ - x) * (x >= y_ ? aUp_ : aDown_);
        return y_;
    }
    double coefficientUp() const noexcept { return aUp_; }
    double coefficientDown() const noexcept { return aDown_; }

private:
    double aUp_ = 0, aDown_ = 0, y_ = 0;  // e^(−T/RC) for each direction; y_: last output
};

// First-order filter y = b0·x + b1·x₁ − a1·y₁, obtained from an analogue prototype by bilinear transform,
// characteristic frequency preserved (pre-warping). Frequencies fc and fs in Hz; the default object is the identity.
class FirstOrder {
public:
    // High-pass s / (s + ωc). fc ≤ 0: identity.
    static FirstOrder highPass(double fc, double fs) noexcept;
    // First-order shelves of Zölzer (DAFX, Wiley, 2002), as in Hatari: boost
    // H = 1 + (G − 1)·ωc/(s + ωc) (bass) or 1 + (G − 1)·s/(s + ωc) (treble); cut by the inverse of the
    // boost of gain 1/G. fc is brought below 0.45 × fs.
    static FirstOrder lowShelf(double gainDb, double fc, double fs) noexcept;
    static FirstOrder highShelf(double gainDb, double fc, double fs) noexcept;

    void reset() noexcept { x1_ = y1_ = 0; }
    // New coefficients, state kept (setting changed during playback).
    void retune(const FirstOrder& o) noexcept {
        b0_ = o.b0_;
        b1_ = o.b1_;
        a1_ = o.a1_;
    }
    double process(double x) noexcept {
        const double y = b0_ * x + b1_ * x1_ - a1_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }
    bool identity() const noexcept { return b0_ == 1 && b1_ == 0 && a1_ == 0; }
    double b0() const noexcept { return b0_; }
    double b1() const noexcept { return b1_; }
    double a1() const noexcept { return a1_; }

private:
    double b0_ = 1, b1_ = 0, a1_ = 0;
    double x1_ = 0, y1_ = 0;
};

}  // namespace stym::ymcore
