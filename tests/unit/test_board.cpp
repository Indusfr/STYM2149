// STYM2149 — board filters (ST and STE output chains)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>
#include <vector>

#include "ymcore/BoardFilters.h"
#include "ymcore/ChipRenderer.h"

using namespace stym;
using namespace stym::ymcore;

namespace {
using cd = std::complex<double>;
constexpr double kPi = std::numbers::pi;

// Response of the digital filter at frequency f.
cd digital(const FirstOrder& h, double f, double fs) {
    const cd zi = std::polar(1.0, -2 * kPi * f / fs);
    return (h.b0() + h.b1() * zi) / (1.0 + h.a1() * zi);
}
// Analogue frequency (normalised to ωc = 1) that corresponds to f through the pre-warped bilinear transform.
double warped(double f, double fc, double fs) { return std::tan(kPi * f / fs) / std::tan(kPi * fc / fs); }
double db(cd h) { return 20 * std::log10(std::abs(h)); }
}  // namespace

TEST_CASE("Board: output coupling high-pass", "[board]") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        const FirstOrder h = FirstOrder::highPass(13.7, fs);
        CHECK(std::fabs(db(digital(h, 13.7, fs)) + 10 * std::log10(2.0)) < 1e-9);  // −3.01 dB at fc
        CHECK(std::abs(digital(h, 0, fs)) < 1e-12);
        for (double f : {1.0, 5.0, 50.0, 1000.0, 15000.0}) {
            const double w = warped(f, 13.7, fs);
            const cd analog = cd(0, w) / cd(1, w);
            CHECK(std::abs(digital(h, f, fs) - analog) < 1e-9);
        }
    }
    CHECK(FirstOrder::highPass(0, 48000).identity());
}

TEST_CASE("Board: LMC1992 shelves (Zölzer, first order)", "[board]") {
    const double fs = 48000;
    for (int g = -12; g <= 12; g += 2) {
        INFO(g << " dB");
        const double G = std::pow(10.0, g / 20.0);
        const FirstOrder bass = FirstOrder::lowShelf(g, 118.276, fs);
        const FirstOrder treble = FirstOrder::highShelf(g, 8438.756, fs);
        if (g == 0) {
            CHECK(bass.identity());
            CHECK(treble.identity());
            continue;
        }
        // Full gain at DC (bass) or at half of fs (treble), unity on the other side.
        CHECK(std::fabs(db(digital(bass, 0, fs)) - g) < 1e-9);
        CHECK(std::fabs(db(digital(bass, fs / 2, fs))) < 1e-9);
        CHECK(std::fabs(db(digital(treble, 0, fs))) < 1e-9);
        CHECK(std::fabs(db(digital(treble, fs / 2, fs)) - g) < 1e-9);
        // Analogue prototypes: boost (s + G)/(s + 1) and (G·s + 1)/(s + 1), cut by the inverse.
        for (double f : {20.0, 60.0, 118.276, 500.0, 2000.0, 8438.756, 15000.0}) {
            const cd sb(0, warped(f, 118.276, fs)), st(0, warped(f, 8438.756, fs));
            const cd hb = G >= 1 ? (sb + G) / (sb + 1.0) : (sb + 1.0) / (sb + 1.0 / G);
            const cd ht = G >= 1 ? (G * st + 1.0) / (st + 1.0) : G * (st + 1.0) / (st + G);
            CHECK(std::abs(digital(bass, f, fs) - hb) < 1e-9);
            CHECK(std::abs(digital(treble, f, fs) - ht) < 1e-9);
        }
    }
    CHECK(lmc1992Step(13) == 12);
    CHECK(lmc1992Step(-40) == -12);
    CHECK(lmc1992Step(3) == 2);
    CHECK(lmc1992Step(-3) == -2);
    CHECK(lmc1992Step(4) == 4);
}

TEST_CASE("Board: C10 of the STF, rise and fall time constants", "[board]") {
    const BoardConfig b;
    AsymmetricRc rc;
    rc.prepare(250000, b.pullUpOhm, b.pullDownOhm, b.c10Farad);
    // Cut-offs quoted by Hatari: 7 693.7 Hz (rise) and 2 096.6 Hz (fall).
    const double tauUp = -4e-6 / std::log(rc.coefficientUp()), tauDown = -4e-6 / std::log(rc.coefficientDown());
    CHECK(std::fabs(1 / (2 * kPi * tauUp) - 7693.7) < 0.1);
    CHECK(std::fabs(1 / (2 * kPi * tauDown) - 2096.6) < 0.1);
    // Rising then falling step: exact exponentials, each with its own constant.
    double worst = 0;
    for (int n = 1; n <= 50; ++n) worst = std::max(worst, std::fabs(rc.process(1.0) - (1 - std::pow(rc.coefficientUp(), n))));
    const double top = 1 - std::pow(rc.coefficientUp(), 50);
    for (int n = 1; n <= 50; ++n) worst = std::max(worst, std::fabs(rc.process(0.0) - top * std::pow(rc.coefficientDown(), n)));
    CHECK(worst < 1e-12);
}

namespace {
// A volume step on channel A (tone off: constant level), then back to 0.
std::vector<float> renderSteps(const RenderConfig& rc, uint32_t n) {
    ChipRenderer r;
    r.prepare(rc);
    r.write(0, 7, 0x3f);
    r.write(8 * 100, 8, 15);
    r.write(8 * 300, 8, 3);
    r.write(8 * 600, 8, 12);
    std::vector<float> out(n);
    r.render(out.data(), n);
    return out;
}
}  // namespace

TEST_CASE("Board: ST chain, C10 applied at each internal step", "[board]") {
    // 250 kHz in RawBox: one sample per internal step. High-pass off to isolate C10.
    RenderConfig raw;
    raw.mode = OutputMode::RawBox;
    raw.hostRate = 250000;
    raw.dc = DcFilter::None;
    RenderConfig st = raw;
    st.board.chain = BoardChain::ST;
    st.board.highPassHz = 0;
    const std::vector<float> x = renderSteps(raw, 1000), y = renderSteps(st, 1000);
    AsymmetricRc rc;
    rc.prepare(250000, st.board.pullUpOhm, st.board.pullDownOhm, st.board.c10Farad);
    uint32_t bad = 0;
    for (size_t i = 0; i < x.size(); ++i) bad += y[i] != float(rc.process(double(x[i]) * 32768.0)) * (1.0f / 32768.0f);
    CHECK(bad == 0);
    // The filter acts: the rise follows the step with a delay.
    CHECK(y[101] < x[101]);
}

TEST_CASE("Board: STE chain, high-pass then shelves at the host rate", "[board]") {
    // HQ at 48 kHz, and RawBox at 250 kHz (one step per sample: the integer average of the Raw chain is exact).
    for (auto [mode, rate] : {std::pair{OutputMode::Raw, 48000u}, std::pair{OutputMode::RawBox, 250000u}}) {
        RenderConfig raw;
        raw.mode = mode;
        raw.hostRate = rate;
        raw.dc = DcFilter::None;
        RenderConfig ste = raw;
        ste.board.chain = BoardChain::STE;
        ste.board.bassDb = 12;
        ste.board.trebleDb = -6;
        const std::vector<float> x = renderSteps(raw, 4000), y = renderSteps(ste, 4000);
        FirstOrder hp = FirstOrder::highPass(13.7, rate), bass = FirstOrder::lowShelf(12, 118.276, rate),
                   treble = FirstOrder::highShelf(-6, 8438.756, rate);
        uint32_t bad = 0;
        for (size_t i = 0; i < x.size(); ++i) {
            const double v = treble.process(bass.process(hp.process(double(x[i]) * 32768.0)));
            bad += y[i] != float(v) * (1.0f / 32768.0f);
        }
        INFO(rate << " Hz");
        CHECK(bad == 0);
    }
}
