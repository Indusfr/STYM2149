// STYM2149 — S4 (HQ chain against a double-precision reference) and S5 (aliasing)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "Dsp.h"
#include "Metrics.h"
#include "Scenarios.h"
#include "ymcore/ChipRenderer.h"

using namespace stym::ymcore;
using namespace stym::test;

namespace {
struct Stream {
    std::vector<double> ticks;  // chip stream at clk/8
    std::vector<float> hq;      // HQ output (level / 32 768)
};

// Renders the "full" scenario (tone, noise, envelope): internal stream and HQ output of the same chip.
Stream renderFull(MasterClock clk, uint32_t rate, double seconds) {
    RenderConfig rc;
    rc.chip.clock = clk;
    rc.hostRate = rate;
    rc.dc = DcFilter::None;
    ChipRenderer r;
    r.prepare(rc);
    Ym2149 twin(rc.chip);
    const std::vector<Scenario> scenarios = m0Scenarios();
    for (const TimedWrite& w : scenarios[5].writes) {
        r.write(0, w.reg, w.value);
        twin.writeRegister(w.reg, w.value);
    }
    Stream s;
    s.hq.resize(size_t(seconds * rate));
    r.render(s.hq.data(), uint32_t(s.hq.size()));
    s.ticks.resize(size_t(r.ticksRendered()));
    for (auto& t : s.ticks) t = twin.tick();
    return s;
}
}  // namespace

TEST_CASE("S4 HQ chain against a double-precision reference", "[S4][resampling]") {
    struct Case {
        MasterClock clk;
        uint32_t rate;
        const char* label;
    };
    for (const Case& c : {Case{clocks::kDefault, 44100, "44.1 kHz"}, Case{clocks::kDefault, 48000, "48 kHz"},
                          Case{clocks::kDefault, 96000, "96 kHz"}, Case{clocks::kAtariStfPal, 48000, "48 kHz, STF PAL clock"}}) {
        const Stream s = renderFull(c.clk, c.rate, 2.0);
        // Same template, direct evaluation in double: positions recomputed in floating point, without table or
        // interpolation.
        SincResampler design;
        design.prepare(c.clk.num, uint64_t(c.clk.den) * 8, c.rate, {});
        const int K = design.taps() / 2;
        const double step = c.clk.hz() / 8.0 / double(c.rate);
        const uint32_t L = design.latency();
        std::vector<double> ref(s.hq.size(), 0.0), got(s.hq.size(), 0.0);
        double maxErr = 0;
        for (size_t n = 0; n < s.hq.size(); ++n) {
            const double center = (double(n) - double(L)) * step;
            const int64_t i = int64_t(std::floor(center));
            double acc = 0, norm = 0;
            for (int64_t j = i - K + 1; j <= i + K; ++j) {
                const double h = design.impulse(center - double(j));
                norm += h;
                if (j >= 0 && j < int64_t(s.ticks.size())) acc += s.ticks[size_t(j)] * h;
            }
            ref[n] = acc / norm / 32768.0;
            got[n] = s.hq[n];
            if (n > L) maxErr = std::max(maxErr, std::fabs(got[n] - ref[n]));
        }
        const double errDb = 20 * std::log10(maxErr + 1e-300);
        const double corr = correlation(got.data() + L, ref.data() + L, got.size() - L);
        const double lsd = logSpectralDistanceDb(welchPsd(got, 8192), welchPsd(ref, 8192), c.rate, 20, 20000);
        metric("S4", std::string("max error, ") + c.label, fmt(errDb, 4) + " dBFS", "≤ -90 dBFS", errDb <= -90);
        metric("S4", std::string("correlation, ") + c.label, fmt(corr, 9), "≥ 0.9999", corr >= 0.9999);
        metric("S4", std::string("log-spectral distance 20 Hz-20 kHz, ") + c.label, fmt(lsd, 3) + " dB", "≤ 0.1 dB", lsd <= 0.1);
        INFO(c.label);
        CHECK(errDb <= -90);
        CHECK(corr >= 0.9999);
        CHECK(lsd <= 0.1);
    }
}

TEST_CASE("S4 flat passband from 20 Hz to 20 kHz", "[S4][resampling]") {
    for (uint32_t rate : {44100u, 48000u, 96000u, 192000u}) {
        SincResampler r;
        r.prepare(2'000'000, 8, rate, {});
        const int K = r.taps() / 2;
        double worst = 0;
        for (double frac : {0.0, 0.25, 0.5, 0.75}) {
            std::vector<double> h(size_t(2 * K));
            double sum = 0;
            for (int k = 0; k < 2 * K; ++k) sum += h[size_t(k)] = r.impulse(frac + (K - 1) - k);
            for (double f = 20; f <= 20000; f *= 1.05) {
                std::complex<double> H = 0;
                for (int k = 0; k < 2 * K; ++k)
                    H += h[size_t(k)] / sum * std::polar(1.0, -2 * kPi * f / 250000.0 * (frac + (K - 1) - k));
                worst = std::max(worst, std::fabs(20 * std::log10(std::abs(H))));
            }
        }
        metric("S4", "passband deviation, " + std::to_string(rate) + " Hz", fmt(worst, 3) + " dB", "≤ 0.1 dB", worst <= 0.1);
        CHECK(worst <= 0.1);
    }
}

TEST_CASE("S5 aliasing of a TP = 12 square (10.4 kHz) below 20 kHz", "[S5][resampling]") {
    for (uint32_t rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u}) {
        RenderConfig rc;
        rc.hostRate = rate;
        rc.dc = DcFilter::None;
        ChipRenderer r;
        r.prepare(rc);
        r.write(0, 0, 12);
        r.write(0, 7, 0x3e);
        r.write(0, 8, 15);
        std::vector<float> y(size_t(rate) * 4);
        r.render(y.data(), uint32_t(y.size()));
        size_t n = 1;
        while (n * 2 <= y.size() - rate / 2) n *= 2;
        const std::vector<double> w = kaiserWindow(n, 20);
        std::vector<std::complex<double>> X(n);
        double mean = 0;
        for (size_t i = 0; i < n; ++i) mean += y[rate / 2 + i];
        mean /= double(n);
        for (size_t i = 0; i < n; ++i) X[i] = (y[rate / 2 + i] - mean) * w[i];
        fft(X);
        const double f0 = 250000.0 / 24.0, binHz = double(rate) / double(n);
        double peak = 0, worst = 0;
        for (size_t k = 1; k < n / 2; ++k) peak = std::max(peak, std::abs(X[k]));
        for (size_t k = 1; k < n / 2; ++k) {
            const double f = double(k) * binHz;
            if (f < 20 || f > 20000) continue;
            const double h = std::round(f / f0);
            if (h >= 1 && std::fabs(f - h * f0) < 12 * binHz) continue;  // true harmonic
            worst = std::max(worst, std::abs(X[k]));
        }
        const double db = 20 * std::log10(worst / peak + 1e-300);
        metric("S5", "max aliasing below 20 kHz, " + std::to_string(rate) + " Hz (" + std::to_string(r.resampler().taps()) + " taps)",
               fmt(db, 4) + " dB", "≤ -90 dB", db <= -90);
        INFO(rate << " Hz");
        CHECK(db <= -90);
    }
}

TEST_CASE("Resampler: whole-sample delay reported", "[S4][resampling]") {
    // An impulse at internal step 0 comes out at its maximum at output n = latency() (exact centre of the filter).
    for (uint32_t rate : {44100u, 48000u, 96000u, 192000u}) {
        SincResampler r;
        r.prepare(2'000'000, 8, rate, {});
        std::vector<float> out;
        for (int n = 0; n < 400; ++n) {
            while (r.inputPushed() < r.inputNeeded()) r.push(r.inputPushed() == 0 ? 1.0f : 0.0f);
            out.push_back(r.pull());
        }
        size_t best = 0;
        for (size_t n = 1; n < out.size(); ++n)
            if (out[n] > out[best]) best = n;
        INFO(rate << " Hz, latency " << r.latency());
        CHECK(best == r.latency());
    }
}

TEST_CASE("Resampler: low chip clocks keep the gain of a square wave near one", "[S4][resampling]") {
    // A clock of 320 kHz or less puts 20 kHz above the input's Nyquist frequency: the cut-off went past 0.5 and the
    // output of a 0/1 square wave reached about 2 × 10^5 (Custom clock from 100 kHz, YM and SNDH tunes).
    for (uint64_t clock : {100'000ull, 150'000ull, 200'000ull, 320'000ull, 400'000ull, 2'000'000ull})
        for (uint32_t rate : {44100u, 48000u, 96000u}) {
            SincResampler r;
            r.prepare(clock, 8, rate, {});
            float peak = 0;
            for (int n = 0; n < 4800; ++n) {
                while (r.inputPushed() < r.inputNeeded()) r.push((r.inputPushed() / 14) % 2 ? 1.0f : 0.0f);
                peak = std::max(peak, std::abs(r.pull()));
            }
            INFO(clock << " Hz clock, " << rate << " Hz output");
            CHECK(peak < 1.5f);
        }
}
