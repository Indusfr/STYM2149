// STYM2149 — ymcore: analogue filters of the board
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymcore/BoardFilters.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace stym::ymcore {

namespace {
// Normalised analogue prototype (ωc = 1) H(s) = (β1·s + β0) / (s + α0), bilinear transform with pre-warping:
// s = (1/t)·(1 − z⁻¹)/(1 + z⁻¹), t = tan(π·fc/fs).
struct Coefficients {
    double b0, b1, a1;
};
Coefficients bilinear(double beta1, double beta0, double alpha0, double t) noexcept {
    const double d = 1 + alpha0 * t;
    return {(beta1 + beta0 * t) / d, (beta0 * t - beta1) / d, (alpha0 * t - 1) / d};
}
// Pre-warped t = tan(π·fc/fs), fc capped at 0.45 × fs.
double warp(double fc, double fs) noexcept { return std::tan(std::numbers::pi * std::min(fc, 0.45 * fs) / fs); }
}  // namespace

// Integer division truncates towards zero: odd values go to the even step nearer to 0.
int lmc1992Step(int db) noexcept { return std::clamp(db, -12, 12) / 2 * 2; }

void AsymmetricRc::prepare(double tickHz, double rUpOhm, double rDownOhm, double farad) noexcept {
    auto coefficient = [&](double ohm) { return ohm > 0 && farad > 0 ? std::exp(-1.0 / (tickHz * ohm * farad)) : 0.0; };
    aUp_ = coefficient(rUpOhm);
    aDown_ = coefficient(rDownOhm);
    y_ = 0;
}

FirstOrder FirstOrder::highPass(double fc, double fs) noexcept {
    FirstOrder f;
    if (!(fc > 0)) return f;
    // s / (s + 1).
    const Coefficients c = bilinear(1, 0, 1, warp(fc, fs));
    f.b0_ = c.b0;
    f.b1_ = c.b1;
    f.a1_ = c.a1;
    return f;
}

FirstOrder FirstOrder::lowShelf(double gainDb, double fc, double fs) noexcept {
    FirstOrder f;
    if (gainDb == 0 || !(fc > 0)) return f;
    const double g = std::pow(10.0, gainDb / 20), t = warp(fc, fs);
    // Boost (s + G)/(s + 1); cut (s + 1)/(s + 1/G).
    const Coefficients c = g >= 1 ? bilinear(1, g, 1, t) : bilinear(1, 1, 1 / g, t);
    f.b0_ = c.b0;
    f.b1_ = c.b1;
    f.a1_ = c.a1;
    return f;
}

FirstOrder FirstOrder::highShelf(double gainDb, double fc, double fs) noexcept {
    FirstOrder f;
    if (gainDb == 0 || !(fc > 0)) return f;
    const double g = std::pow(10.0, gainDb / 20), t = warp(fc, fs);
    // Boost (G·s + 1)/(s + 1); cut G·(s + 1)/(s + G).
    const Coefficients c = g >= 1 ? bilinear(g, 1, 1, t) : bilinear(g, g, g, t);
    f.b0_ = c.b0;
    f.b1_ = c.b1;
    f.a1_ = c.a1;
    return f;
}

}  // namespace stym::ymcore
