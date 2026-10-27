// STYM2149 — audio delay (eighth DAW test, item 98): no change while it is off or in the 3-channel modes, echoes
// of the send of each track after its fader at the time set (ms or division of the tempo), feedback, ping-pong,
// switched on during playback.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using ymengine::Engine;
using ymengine::VoiceMode;

namespace {

constexpr uint32_t kRate = 48000;

struct Out {
    std::vector<float> l, r;
};

ymengine::TrackParams track(double send, double level = 1) {
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.mix.level = level;
    t.mix.send = send;
    t.patch.releaseMs = 0;
    return t;
}

ymengine::DelayParams delayMs(double ms, double feedback = 0) {
    ymengine::DelayParams d;
    d.on = true;
    d.sync = false;
    d.timeMs = ms;
    d.feedback = feedback;
    d.hiCutHz = ymengine::kDelayHiCutMax;
    d.level = 1;
    return d;
}

// Engine in `mode`, linear output, delay d; fire(e, start, len) places the events of each block of 512 samples.
Out render(VoiceMode mode, const ymengine::DelayParams& d, uint32_t n,
           const std::function<void(Engine&)>& setup,
           const std::function<void(Engine&, uint32_t, uint32_t)>& fire, bool ste = false) {
    Engine e;
    ymengine::EngineConfig cfg;
    cfg.render.hostRate = kRate;
    e.prepare(cfg);
    ymengine::GlobalParams g = e.global();
    g.mode = mode;
    g.ste = ste;
    g.softClip = false;
    g.delay = d;
    e.setGlobal(g);
    setup(e);
    Out o{std::vector<float>(n), std::vector<float>(n)};
    for (uint32_t at = 0; at < n; at += 512) {
        const uint32_t len = std::min<uint32_t>(512, n - at);
        if (fire) fire(e, at, len);
        e.render(o.l.data() + at, o.r.data() + at, len);
    }
    return o;
}

// A note on `tr` from sample 0 to `off`.
std::function<void(Engine&, uint32_t, uint32_t)> burst(int tr, uint32_t off, int note = 57) {
    return [=](Engine& e, uint32_t start, uint32_t len) {
        if (start == 0) e.noteOn(0, note, 127, tr);
        if (off >= start && off < start + len) e.noteOff(off - start, note, tr);
    };
}

std::vector<double> minus(const std::vector<float>& a, const std::vector<float>& b) {
    std::vector<double> d(a.size());
    for (size_t i = 0; i < a.size(); ++i) d[i] = double(a[i]) - double(b[i]);
    return d;
}

double peakOf(const std::vector<double>& x, size_t from, size_t to) {
    double p = 0;
    for (size_t i = from; i < to && i < x.size(); ++i) p = std::max(p, std::fabs(x[i]));
    return p;
}

double rms(const std::vector<double>& x, size_t from, size_t to) {
    double m = 0, s = 0;
    for (size_t i = from; i < to; ++i) m += x[i];
    m /= double(to - from);
    for (size_t i = from; i < to; ++i) s += (x[i] - m) * (x[i] - m);
    return std::sqrt(s / double(to - from));
}

// Correlation of x[a .. a + len) and y[b .. b + len), means removed.
double correlation(const std::vector<double>& x, size_t a, const std::vector<double>& y, size_t b, size_t len) {
    double mx = 0, my = 0;
    for (size_t i = 0; i < len; ++i) {
        mx += x[a + i];
        my += y[b + i];
    }
    mx /= double(len);
    my /= double(len);
    double sxy = 0, sxx = 0, syy = 0;
    for (size_t i = 0; i < len; ++i) {
        const double p = x[a + i] - mx, q = y[b + i] - my;
        sxy += p * q;
        sxx += p * p;
        syy += q * q;
    }
    return sxy / std::sqrt(sxx * syy);
}

std::vector<double> wide(const std::vector<float>& x) { return std::vector<double>(x.begin(), x.end()); }

// One-pole high-pass at 100 Hz: the comparisons leave out the slow movements of the DC component (output chain of the
// chip, high-pass at 10 Hz of the delay), which the echoes do not copy exactly.
std::vector<double> hp(const std::vector<double>& x) {
    const double a = std::exp(-2 * 3.141592653589793 * 100 / kRate);
    std::vector<double> y(x.size());
    double px = 0, py = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        py = a * (py + x[i] - px);
        px = x[i];
        y[i] = py;
    }
    return y;
}

}  // namespace

TEST_CASE("Delay: off, without send or in the 3-channel modes, the output does not change", "[engine][delay]") {
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Dual3, VoiceMode::Authentic3}) {
        CAPTURE(int(mode));
        auto setup = [](double send) {
            return [send](Engine& e) {
                e.setTrack(0, track(send));
                e.setTrack(1, track(send * 0.5));
            };
        };
        auto fire = [](Engine& e, uint32_t start, uint32_t len) {
            burst(0, 6000)(e, start, len);
            burst(1, 9000, 64)(e, start, len);
        };
        const Out dry = render(mode, ymengine::DelayParams{}, 24000, setup(1), fire);
        // Delay off, sends open.
        REQUIRE(render(mode, ymengine::DelayParams{}, 24000, setup(0), fire).l == dry.l);
        // Delay on, sends closed.
        const Out closed = render(mode, delayMs(50, 0.5), 24000, setup(0), fire);
        REQUIRE(closed.l == dry.l);
        REQUIRE(closed.r == dry.r);
        // Authentic 3ch and Dual chip: no delay at all (16 channels only).
        if (mode != VoiceMode::Studio16) {
            const Out on = render(mode, delayMs(50, 0.5), 24000, setup(1), fire, true);
            REQUIRE(on.l == render(mode, ymengine::DelayParams{}, 24000, setup(1), fire, true).l);
        }
    }
}

TEST_CASE("Delay: 16 channels, the echo of a track comes at the time set, after the fader", "[engine][delay]") {
    const uint32_t delay = 4800;  // 100 ms
    auto setup = [](double send, double level) { return [=](Engine& e) { e.setTrack(0, track(send, level)); }; };
    const Out dry = render(VoiceMode::Studio16, ymengine::DelayParams{}, 24000, setup(1, 1), burst(0, 2400));
    const Out wet = render(VoiceMode::Studio16, delayMs(100), 24000, setup(1, 1), burst(0, 2400));
    const std::vector<double> raw = minus(wet.l, dry.l);
    // Nothing before the time of the delay, then the note again (low-pass at 20 kHz aside).
    REQUIRE(peakOf(raw, 0, delay) == 0.0);
    const std::vector<double> echo = hp(raw), d = hp(wide(dry.l));
    REQUIRE(rms(d, 200, 2200) > 0.02);
    CHECK(correlation(echo, delay + 200, d, 200, 2000) > 0.95);
    const double ratio = rms(echo, delay + 200, delay + 2200) / rms(d, 200, 2200);
    CHECK(ratio > 0.9);
    CHECK(ratio < 1.05);
    // No feedback: a single echo.
    CHECK(rms(echo, 2 * delay + 200, 2 * delay + 2200) < 0.01 * rms(echo, delay + 200, delay + 2200));
    // Centred track: same echo on both sides.
    REQUIRE(minus(wet.r, dry.r) == raw);
    // After the fader: half the level, half the echo; mute or send 0: none.
    const Out half = render(VoiceMode::Studio16, delayMs(100), 24000, setup(1, 0.5), burst(0, 2400));
    const Out halfDry = render(VoiceMode::Studio16, ymengine::DelayParams{}, 24000, setup(1, 0.5), burst(0, 2400));
    const double h =
        rms(hp(minus(half.l, halfDry.l)), delay + 200, delay + 2200) / rms(echo, delay + 200, delay + 2200);
    CHECK(std::fabs(h - 0.5) < 0.01);
    auto muted = [](Engine& e) {
        auto t = track(1);
        t.mix.mute = true;
        e.setTrack(0, t);
    };
    const Out m = render(VoiceMode::Studio16, delayMs(100), 24000, muted, burst(0, 2400));
    const Out mDry = render(VoiceMode::Studio16, ymengine::DelayParams{}, 24000, muted, burst(0, 2400));
    CHECK(peakOf(minus(m.l, mDry.l), 0, 24000) < 1e-6);
}

TEST_CASE("Delay: feedback, ping-pong and tempo division", "[engine][delay]") {
    const uint32_t delay = 4800;
    auto setup = [](Engine& e) { e.setTrack(0, track(1)); };
    const Out dry = render(VoiceMode::Studio16, ymengine::DelayParams{}, 4 * delay, setup, burst(0, 2400));
    SECTION("each repeat comes back at the feedback") {
        const Out wet = render(VoiceMode::Studio16, delayMs(100, 0.5), 4 * delay, setup, burst(0, 2400));
        const std::vector<double> echo = hp(minus(wet.l, dry.l));
        const double first = rms(echo, delay + 200, delay + 2200);
        const double second = rms(echo, 2 * delay + 200, 2 * delay + 2200);
        const double third = rms(echo, 3 * delay + 200, 3 * delay + 2200);
        CHECK(std::fabs(second / first - 0.5) < 0.05);
        CHECK(std::fabs(third / second - 0.5) < 0.05);
    }
    SECTION("ping-pong: the first echo on the left, the second on the right") {
        auto pp = delayMs(100, 0.5);
        pp.pingPong = true;
        const Out wet = render(VoiceMode::Studio16, pp, 4 * delay, setup, burst(0, 2400));
        const std::vector<double> rawR = minus(wet.r, dry.r);
        REQUIRE(peakOf(rawR, 0, 2 * delay) == 0.0);
        const std::vector<double> el = hp(minus(wet.l, dry.l)), er = hp(rawR);
        const double first = rms(el, delay + 200, delay + 2200);
        CHECK(first > 0.01);
        CHECK(std::fabs(rms(er, 2 * delay + 200, 2 * delay + 2200) / first - 0.5) < 0.05);
        CHECK(rms(el, 2 * delay + 200, 2 * delay + 2200) < 0.01 * first);
        CHECK(std::fabs(rms(el, 3 * delay + 200, 3 * delay + 2200) / first - 0.25) < 0.04);
        CHECK(rms(er, 3 * delay + 200, 3 * delay + 2200) < 0.01 * first);
    }
    SECTION("1/8 at 120 BPM: 250 ms") {
        auto synced = delayMs(100);
        synced.sync = true;
        synced.division = 5;  // 1/8
        const Out wet = render(VoiceMode::Studio16, synced, 24000, setup, burst(0, 2400));
        const Out dry24 = render(VoiceMode::Studio16, ymengine::DelayParams{}, 24000, setup, burst(0, 2400));
        const std::vector<double> echo = minus(wet.l, dry24.l);
        REQUIRE(peakOf(echo, 0, 12000) == 0.0);
        CHECK(rms(hp(echo), 12200, 14200) > 0.02);
    }
}

TEST_CASE("Delay: switched on and sends opened during playback", "[engine][delay]") {
    const uint32_t delay = 4800, on = 6144;  // block boundary
    {
        const VoiceMode mode = VoiceMode::Studio16;
        auto setup = [](Engine& e) { e.setTrack(0, track(1)); };
        // A held note; the delay goes on at `on`.
        auto fire = [&](bool switchOn) {
            return [=](Engine& e, uint32_t start, uint32_t) {
                if (start == 0) e.noteOn(0, 57, 127, 0);
                if (switchOn && start == on) {
                    ymengine::GlobalParams g = e.global();
                    g.delay = delayMs(100);
                    e.setGlobal(g);
                }
            };
        };
        const Out dry = render(mode, ymengine::DelayParams{}, 20000, setup, fire(false));
        const Out wet = render(mode, ymengine::DelayParams{}, 20000, setup, fire(true));
        const std::vector<double> echo = minus(wet.l, dry.l);
        REQUIRE(peakOf(echo, 0, on + delay) == 0.0);
        CHECK(rms(hp(echo), on + delay + 1200, on + delay + 6000) > 0.01);
        // Send opened later on a track: echo only from then on.
        auto later = [&](Engine& e, uint32_t start, uint32_t len) {
            if (start == 0) e.noteOn(0, 57, 127, 0);
            if (on >= start && on < start + len) e.setTrackMix(on - start, 0, track(1).mix);
        };
        auto closed = [](Engine& e) { e.setTrack(0, track(0)); };
        const Out late = render(mode, delayMs(100), 20000, closed, later);
        const Out lateDry = render(mode, ymengine::DelayParams{}, 20000, closed, later);
        const std::vector<double> le = minus(late.l, lateDry.l);
        REQUIRE(peakOf(le, 0, on + delay) == 0.0);
        CHECK(rms(hp(le), on + delay + 1200, on + delay + 6000) > 0.01);
    }
}
