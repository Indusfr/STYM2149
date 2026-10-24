// STYM2149 — S6: tone, noise and envelope frequencies
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <vector>

#include "Dsp.h"
#include "Metrics.h"
#include "ymcore/ChipRenderer.h"

using namespace stym::ymcore;
using namespace stym::test;

namespace {
// Number of internal steps between two rising edges of a discrete signal.
double measuredPeriodTicks(Ym2149& chip, uint32_t (*probe)(const Ym2149&), int ticks) {
    chip.tick();  // the starting state is not a rising edge
    uint32_t prev = probe(chip);
    int64_t first = -1, last = -1, rises = 0;
    for (int t = 0; t < ticks; ++t) {
        chip.tick();
        const uint32_t v = probe(chip);
        if (v > prev) {
            if (first < 0) first = t;
            else ++rises;
            last = t;
        }
        prev = v;
    }
    return rises ? double(last - first) / double(rises) : 0.0;
}

uint32_t toneA(const Ym2149& c) { return c.state().toneEdges & 0x1f; }
// 1 when the envelope level leaves 0: once per cycle in sawtooth $8 as in triangle $A.
uint32_t envLeavesZero(const Ym2149& c) { return (c.levels() & 31) != 0 ? 1u : 0u; }

// Frequency of the main peak of the HQ output, parabolic interpolation on the magnitude in dB.
double peakFrequency(const std::vector<float>& x, double fs, double fMin) {
    size_t n = 1;
    while (n * 2 <= x.size()) n *= 2;
    const std::vector<double> w = kaiserWindow(n, 12);
    std::vector<std::complex<double>> buf(n);
    double mean = 0;
    for (size_t i = 0; i < n; ++i) mean += x[i];
    mean /= double(n);
    for (size_t i = 0; i < n; ++i) buf[i] = (x[i] - mean) * w[i];
    fft(buf);
    size_t best = 1;
    for (size_t k = 1; k < n / 2; ++k)
        if (double(k) * fs / double(n) >= fMin && std::abs(buf[k]) > std::abs(buf[best])) best = k;
    const double a = 20 * std::log10(std::abs(buf[best - 1])), b = 20 * std::log10(std::abs(buf[best])),
                 c = 20 * std::log10(std::abs(buf[best + 1]));
    const double delta = 0.5 * (a - c) / (a - 2 * b + c);
    return (double(best) + delta) * fs / double(n);
}

std::vector<float> renderHq(MasterClock clk, std::vector<std::pair<int, int>> regs, double seconds) {
    RenderConfig rc;
    rc.chip.clock = clk;
    rc.hostRate = 48000;
    rc.dc = DcFilter::None;
    ChipRenderer r;
    r.prepare(rc);
    for (auto [g, v] : regs) r.write(0, uint8_t(g), uint8_t(v));
    std::vector<float> out(size_t(seconds * rc.hostRate));
    r.render(out.data(), uint32_t(out.size()));
    return {out.begin() + rc.hostRate / 2, out.end()};
}
}  // namespace

TEST_CASE("S6 tone: period of 2·TP internal steps, f = clk / (16·TP)", "[S6][frequency]") {
    for (uint32_t tp : {1u, 2u, 12u, 284u, 1000u, 4095u}) {
        Ym2149 chip;
        chip.writeRegister(0, uint8_t(tp & 0xff));
        chip.writeRegister(1, uint8_t(tp >> 8));
        const double period = measuredPeriodTicks(chip, toneA, int(2 * tp * 200));
        INFO("TP = " << tp);
        CHECK(period == double(2 * tp));
    }
}

TEST_CASE("S6 envelope: clk / (256·EP) as a sawtooth, clk / (512·EP) as a triangle", "[S6][frequency]") {
    for (uint32_t ep : {1u, 10u, 400u}) {
        for (int shape : {0x08, 0x0a}) {
            Ym2149 chip;
            chip.writeRegister(7, 0x3f);
            chip.writeRegister(8, 0x10);
            chip.writeRegister(11, uint8_t(ep & 0xff));
            chip.writeRegister(12, uint8_t(ep >> 8));
            chip.writeRegister(13, uint8_t(shape));
            const int steps = shape == 0x08 ? 32 : 64;
            const double period = measuredPeriodTicks(chip, envLeavesZero, int(ep * steps * 30));
            INFO("EP = " << ep << ", R13 = " << shape);
            CHECK(period == double(steps * ep));
        }
    }
}

TEST_CASE("S6 frequencies measured on the HQ output at 48 kHz (±0.1 %)", "[S6][frequency]") {
    struct Case {
        const char* what;
        MasterClock clk;
        std::vector<std::pair<int, int>> regs;
        double expected;
        double fMin;
    };
    const double clk = 2'000'000.0;
    const double pal = clocks::kAtariStfPal.hz();
    const std::vector<Case> cases = {
        {"tone TP = 284", clocks::kDefault, {{0, 284 & 0xff}, {1, 284 >> 8}, {7, 0x3e}, {8, 15}}, clk / (16 * 284), 100},
        {"tone TP = 284, STF PAL clock", clocks::kAtariStfPal, {{0, 284 & 0xff}, {1, 284 >> 8}, {7, 0x3e}, {8, 15}}, pal / (16 * 284), 100},
        {"tone TP = 12", clocks::kDefault, {{0, 12}, {7, 0x3e}, {8, 15}}, clk / (16 * 12), 1000},
        {"envelope $8 EP = 400", clocks::kDefault, {{7, 0x3f}, {8, 0x10}, {11, 400 & 0xff}, {12, 400 >> 8}, {13, 0x08}}, clk / (256 * 400), 5},
        {"envelope $A EP = 100", clocks::kDefault, {{7, 0x3f}, {8, 0x10}, {11, 100}, {12, 0}, {13, 0x0a}}, clk / (512 * 100), 5},
        {"buzzer $A EP = 30", clocks::kDefault, {{7, 0x3f}, {8, 0x10}, {11, 30}, {12, 0}, {13, 0x0a}}, clk / (512 * 30), 20},
    };
    for (const Case& c : cases) {
        const std::vector<float> y = renderHq(c.clk, c.regs, 23.0);
        const double f = peakFrequency(y, 48000, c.fMin);
        const double err = std::fabs(f / c.expected - 1) * 100;
        metric("S6", std::string(c.what) + ": " + fmt(f, 7) + " Hz (expected " + fmt(c.expected, 7) + ")", fmt(err, 3) + " %",
               "≤ 0.1 %", err <= 0.1);
        INFO(c.what << ": " << f << " Hz, expected " << c.expected);
        CHECK(err <= 0.1);
    }
}
