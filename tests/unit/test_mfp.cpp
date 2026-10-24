// STYM2149 — timers of the MFP 68901 (ymengine): quantisation, clock conversion, reload.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "ymengine/Mfp68901.h"

using namespace stym;
using namespace stym::ymengine;

namespace {
// Exhaustive search of the 7 × 256 settings: reference for mfpQuantize.
MfpSetting bruteForce(double hz) {
    MfpSetting best{};
    double bestErr = 1e300;
    for (uint8_t p = 1; p < 8; ++p)
        for (uint16_t d = 1; d <= 256; ++d) {
            const MfpSetting s{p, d};
            const double err = std::fabs(std::log2(s.hz() / hz));
            if (err < bestErr) {
                bestErr = err;
                best = s;
            }
        }
    return best;
}
}  // namespace

TEST_CASE("MFP: quantisation of a frequency", "[mfp]") {
    // 2 457 600 / 400: six exact settings, the smallest prescaler wins.
    const MfpSetting s = mfpQuantize(6144.0);
    CHECK(s.prescaler == 1);
    CHECK(s.data == 100);
    CHECK(s.hz() == 6144.0);
    CHECK_FALSE(mfpQuantize(0.0).running());
    CHECK_FALSE(mfpQuantize(-5.0).running());
    // Bounds: 614 400 Hz (÷4, 1) and 48 Hz (÷200, 256).
    CHECK(mfpQuantize(1e6) == MfpSetting{1, 1});
    CHECK(mfpQuantize(10.0) == MfpSetting{7, 256});

    // Identical to the exhaustive search, over 30 Hz to 100 kHz (same deviation in cents).
    double worstCents = 0;
    for (double hz = 30.0; hz < 100000.0; hz *= 1.0137) {
        const MfpSetting q = mfpQuantize(hz), b = bruteForce(hz);
        const double eq = std::fabs(std::log2(q.hz() / hz)), eb = std::fabs(std::log2(b.hz() / hz));
        INFO(hz << " Hz");
        CHECK(eq <= eb + 1e-15);
        if (hz >= 50 && hz <= 8000) worstCents = std::max(worstCents, 1200 * eq);
    }
    // Musical range of the SID and Sync Buzzer voices: the granularity of the MFP leaves a deviation of about ten
    // cents at most (around 50 Hz, ÷200 and ~245; around 8 kHz, ÷4 and ~77).
    INFO("largest deviation " << worstCents << " cents");
    CHECK(worstCents < 12.0);
}

TEST_CASE("MFP: conversion between MFP cycles and master cycles", "[mfp]") {
    const MfpClockMap m;  // 2 MHz
    CHECK(m.toMaster(0) == 0);
    CHECK(m.toMaster(kMfpClockHz) == 2'000'000);
    CHECK(m.toMfp(2'000'000) == kMfpClockHz);
    // 768 MFP cycles = 625 master cycles exactly; in between, rounded up to the next cycle.
    CHECK(m.toMaster(768) == 625);
    CHECK(m.toMaster(1) == 1);
    CHECK(m.toMfp(1) == 2);
    for (uint64_t t = 0; t < 5000; ++t) {
        const uint64_t c = m.toMaster(t);
        // c is the first master cycle at instant t or after: c × 2 457 600 ≥ t × 2 000 000 > (c - 1) × 2 457 600.
        CHECK(c * 2457600 >= t * 2000000);
        if (c) CHECK((c - 1) * 2457600 < t * 2000000);
    }
    // Non-integer clock (STF PAL, 8 021 247 / 4 Hz): one day of MFP cycles without overflow or drift.
    const MfpClockMap pal(ymcore::clocks::kAtariStfPal);
    const uint64_t day = uint64_t(kMfpClockHz) * 86400;
    const uint64_t c = pal.toMaster(day);
    CHECK(c == (day * 8021247 + 9830400 - 1) / 9830400);
}

TEST_CASE("MFP: start, reload and stop of a timer", "[mfp]") {
    MfpTimer t;
    CHECK(t.nextFire() == UINT64_MAX);
    t.write(1000, {2, 50});  // ÷10, 50: period 500
    CHECK(t.nextFire() == 1500);
    t.acknowledge();
    CHECK(t.nextFire() == 2000);
    // Data register rewritten while running: only the reload changes.
    t.write(1800, {2, 20});
    CHECK(t.nextFire() == 2000);
    t.acknowledge();
    CHECK(t.nextFire() == 2200);
    // Change of prescaler: the counter keeps its value (20 countdowns left at t = 2200 - 200 = 2000).
    t.write(2000, {3, 20});  // ÷16
    CHECK(t.nextFire() == 2000 + 20 * 16);
    // Same setting rewritten: nothing moves.
    t.write(2100, {3, 20});
    CHECK(t.nextFire() == 2320);
    t.write(2200, {});
    CHECK_FALSE(t.running());
    CHECK(t.nextFire() == UINT64_MAX);
    // Restart: the counter is reloaded, first interrupt one period later.
    t.write(3000, {1, 256});
    CHECK(t.nextFire() == 3000 + 1024);
}

TEST_CASE("MFP: AtariAudio replica, at most one interrupt per sample", "[mfp]") {
    MfpTimerAtariAudio a;
    a.reset(48000);
    a.write(1, 1);  // 614 400 Hz: more than 12 periods per sample, a single interrupt
    // Start from stopped (SetTimer writes the control then the data): the counter was at 0, the
    // first interrupt comes after 256 periods, i.e. at the 20th sample (12.8 periods per sample).
    int first = 0;
    for (int i = 1; i <= 40 && !first; ++i)
        if (a.tick()) first = i;
    CHECK(first == 20);
    int fired = 0;
    for (int i = 0; i < 100; ++i) fired += a.tick();
    CHECK(fired == 100);
}

TEST_CASE("MFP: interrupt jitter bounded and deterministic", "[mfp]") {
    IrqJitter j1, j2;
    j1.configure(24, 7);
    j2.configure(24, 7);
    uint32_t mx = 0, mn = 99;
    for (int i = 0; i < 10000; ++i) {
        const uint32_t a = j1.next();
        CHECK(a == j2.next());
        mx = std::max(mx, a);
        mn = std::min(mn, a);
    }
    CHECK(mn == 0);
    CHECK(mx == 24);
    IrqJitter off;
    off.configure(0, 1);
    CHECK(off.next() == 0);
    CHECK(cpuCyclesToYmMaster(96) == 24);
}
