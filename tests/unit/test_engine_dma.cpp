// STYM2149 — STE DMA sound (M6, B8, D-153): DMA sound type played per track, bank of eight samples, STE machine;
// rate, pitch to the semitone, mono and stereo, loop and sample played to the end, DMA tracks in 3-channel mode
// (5 to 12, 9 to 16 in Dual chip), mixer of the track, chords, mix before the LMC1992, panic, meters, preview.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using ymengine::DmaParams;
using ymengine::Engine;
using ymengine::Generator;
using ymengine::VoiceMode;

namespace {

constexpr double kPi = 3.141592653589793;
constexpr uint32_t kRate = 48000;

struct Rig {
    Engine e;
    std::vector<float> l = std::vector<float>(1 << 15), r = std::vector<float>(1 << 15);
    std::vector<float> outL, outR;
    ymengine::GlobalParams g;

    explicit Rig(VoiceMode mode = VoiceMode::Studio16, bool ste = true,
                 ymcore::BoardChain chain = ymcore::BoardChain::STE, int bassDb = 0) {
        ymengine::EngineConfig cfg;
        cfg.render.hostRate = kRate;
        cfg.render.board.chain = chain;
        cfg.render.board.bassDb = bassDb;
        e.prepare(cfg);
        g.mode = mode;
        g.ste = ste;
        e.setGlobal(g);
    }
    // Track of DMA type (16-channel), active, at level 1, centred; slot -1: according to the rank.
    void dmaTrack(int t, int slot = 0, double pan = 0) {
        ymengine::TrackParams p = e.track(t);
        p.patch.generator = Generator::Dma;
        p.patch.dmaSlot = int8_t(slot);
        p.mix.enabled = true;
        p.mix.level = 1;
        p.mix.pan = pan;
        e.setTrack(t, p);
    }
    void run(uint32_t n, bool keep = false) {
        while (n > 0) {
            const uint32_t k = std::min<uint32_t>(n, uint32_t(l.size()));
            e.render(l.data(), r.data(), k);
            if (keep) {
                outL.insert(outL.end(), l.begin(), l.begin() + k);
                outR.insert(outR.end(), r.begin(), r.begin() + k);
            }
            n -= k;
        }
    }
    void clear() {
        outL.clear();
        outR.clear();
    }
    // DMA voices that sound.
    std::vector<Engine::DmaInfo> playing() const {
        std::vector<Engine::DmaInfo> out;
        for (int i = 0; i < Engine::kVoices; ++i)
            if (const Engine::DmaInfo d = e.dmaVoice(i); d.on) out.push_back(d);
        return out;
    }
};

// Sine of `period` frames, peak ±100.
std::vector<int8_t> sine(int frames, double period, bool stereo = false, bool rightOnly = false) {
    std::vector<int8_t> d;
    for (int i = 0; i < frames; ++i) {
        const auto v = int8_t(std::lround(100 * std::sin(2 * kPi * i / period)));
        if (stereo) {
            d.push_back(rightOnly ? 0 : v);
            d.push_back(v);
        } else {
            d.push_back(v);
        }
    }
    return d;
}

DmaParams params(int rate = 2, bool stereo = false, bool loop = true, int root = 60, int slot = 0) {
    DmaParams p;
    auto& s = p.slots[size_t(slot)];
    s.rate = uint8_t(rate);
    s.stereo = stereo;
    s.loop = loop;
    s.root = uint8_t(root);
    return p;
}

// Amplitude of the component at hz (Hann window).
double component(const std::vector<float>& x, double hz) {
    double re = 0, im = 0;
    const size_t n = x.size();
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n - 1));
        const double a = 2 * kPi * hz * double(i) / kRate;
        re += w * x[i] * std::cos(a);
        im += w * x[i] * std::sin(a);
    }
    return std::hypot(re, im);
}

// Frequency of the strongest component between lo and hi (steps of 0.25 % of the frequency).
double peakHz(const std::vector<float>& x, double lo, double hi) {
    double best = lo, top = -1;
    for (double f = lo; f <= hi; f *= 1.0025) {
        const double c = component(x, f);
        if (c > top) {
            top = c;
            best = f;
        }
    }
    return best;
}

double rms(const std::vector<float>& x) {
    double s = 0;
    for (float v : x) s += double(v) * v;
    return std::sqrt(s / double(std::max<size_t>(1, x.size())));
}

// Silence: the STE chain (LMC1992 filters) leaves a tail that tends towards zero without reaching it.
bool silent(const std::vector<float>& x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return std::fabs(v) < 1e-6f; });
}

}  // namespace

TEST_CASE("DMA: STE rates and pitch to the semitone", "[engine][dma]") {
    for (int rate = 0; rate < ymengine::kDmaRates; ++rate) {
        CAPTURE(rate);
        Rig g;
        g.dmaTrack(0);
        g.e.setDma(params(rate));
        g.e.setDmaSample(0, sine(4096, 32));  // 32 frames per period: rate / 32
        for (const int semis : {0, 7, -12}) {
            CAPTURE(semis);
            g.e.noteOn(0, 60 + semis, 127, 0);
            g.run(2400);
            g.clear();
            g.run(12000, true);
            const auto v = g.playing();
            REQUIRE(v.size() == 1);
            CHECK(v[0].slot == 0);
            CHECK(std::fabs(v[0].step - std::pow(2.0, semis / 12.0)) < 1e-9);
            const double want = ymengine::kDmaRateHz[size_t(rate)] / 32 * std::pow(2.0, semis / 12.0);
            if (want < 12000) {
                const double got = peakHz(g.outL, want * 0.9, want * 1.1);
                CHECK(std::fabs(got / want - 1) < 0.005);
            }
            g.e.noteOff(0, 60 + semis, 0);
            g.run(480);
            CHECK(g.playing().empty());  // looped: the released note stops (release of 0 ms)
        }
    }
}

TEST_CASE("DMA: Fine raises the slot by up to 100 cents, notes and preview (Indus, 26/10/2026, D-452)",
          "[engine][dma]") {
    for (const double cents : {50.0, -100.0, 500.0}) {
        CAPTURE(cents);
        const double c = std::clamp(cents, -100.0, 100.0);  // ±100 cents at most
        Rig g;
        g.dmaTrack(0);
        DmaParams p = params();
        p.slots[0].cents = cents;
        g.e.setDma(p);
        g.e.setDmaSample(0, sine(4096, 32));  // 32 frames per period
        g.e.noteOn(0, 60, 127, 0);            // the root note
        g.run(2400);
        const auto v = g.playing();
        REQUIRE(v.size() == 1);
        CHECK(std::fabs(v[0].step - std::pow(2.0, c / 1200)) < 1e-9);
        g.e.noteOff(0, 60, 0);
        g.run(480);
        // The preview plays the slot at its rate, raised the same way.
        g.e.dmaPreview(0, 0);
        g.run(2400);
        g.clear();
        g.run(12000, true);
        const double want = ymengine::kDmaRateHz[2] / 32 * std::pow(2.0, c / 1200);
        CHECK(std::fabs(peakHz(g.outL, want * 0.9, want * 1.1) / want - 1) < 0.005);
        g.e.dmaStopPreview(0);
    }
}

TEST_CASE("DMA: only with the STE machine, whatever the output chain", "[engine][dma]") {
    for (const bool ste : {false, true}) {
        CAPTURE(ste);
        Rig g(VoiceMode::Studio16, ste, ymcore::BoardChain::ST);
        g.dmaTrack(0);
        g.e.setDma(params());
        g.e.setDmaSample(0, sine(4096, 32));
        g.e.noteOn(0, 60, 127, 0);
        g.run(4800);
        // ST machine: the track plays as Chip Voice (fallback role, like a Digidrum outside its track in 3-channel
        // mode).
        CHECK(g.playing().size() == (ste ? 1u : 0u));
        CHECK(g.e.activeVoices() == 1);
    }
}

TEST_CASE("DMA: loop, sample played to the end, end of the sample", "[engine][dma]") {
    // Without loop: the release has no effect, the sample plays to the end (1 000 frames at 25 033 Hz ≈ 40 ms).
    Rig g;
    g.dmaTrack(0);
    g.e.setDma(params(2, false, false));
    g.e.setDmaSample(0, sine(1000, 50));
    g.e.noteOn(0, 60, 127, 0);
    g.run(480);
    g.e.noteOff(0, 60, 0);
    g.run(480);
    REQUIRE(g.playing().size() == 1);
    CHECK(g.playing()[0].position > 300);
    g.run(2400);
    CHECK(g.playing().empty());
    g.run(9600);  // tail of the output filters (like a released YM voice)
    g.clear();
    g.run(480, true);
    CHECK(silent(g.outL));

    // Looped: while the note is held, well beyond the length.
    g.e.setDma(params(2, false, true));
    g.e.noteOn(0, 60, 127, 0);
    g.run(9600);
    REQUIRE(g.playing().size() == 1);
    CHECK(g.playing()[0].position < 1000);
    g.clear();
    g.run(480, true);
    CHECK(rms(g.outL) > 0.01);
}

TEST_CASE("DMA: Start and End bound the part played, with or without loop (D-444)", "[engine][dma]") {
    // Frames 200 to 599 of 1 000: the voice starts at Start and stops at End (400 frames at 25 033 Hz ≈ 16 ms).
    DmaParams p = params(2, false, false);
    p.slots[0].start = 200;
    p.slots[0].end = 600;
    Rig g;
    g.dmaTrack(0);
    g.e.setDma(p);
    g.e.setDmaSample(0, sine(1000, 50));
    g.e.noteOn(0, 60, 127, 0);
    g.run(48);
    REQUIRE(g.playing().size() == 1);
    CHECK(g.playing()[0].position >= 200);
    CHECK(g.playing()[0].position < 260);
    g.run(960);
    CHECK(g.playing().empty());

    // Looped: from End back to Start while the note is held.
    p.slots[0].loop = true;
    g.e.setDma(p);
    g.e.noteOn(0, 60, 127, 0);
    for (int k = 0; k < 20; ++k) {
        g.run(480);
        CAPTURE(k);
        REQUIRE(g.playing().size() == 1);
        CHECK(g.playing()[0].position >= 200);
        CHECK(g.playing()[0].position < 600);
    }

    // End 0: to the end of the sample (frame 700 after 20 ms, stopped at 1 000).
    Rig h;
    h.dmaTrack(0);
    p.slots[0] = {2, false, 60, false, 200, 0};
    h.e.setDma(p);
    h.e.setDmaSample(0, sine(1000, 50));
    h.e.noteOn(0, 60, 127, 0);
    h.run(960);
    REQUIRE(h.playing().size() == 1);
    CHECK(h.playing()[0].position > 650);
    h.run(960);
    CHECK(h.playing().empty());
}

TEST_CASE("DMA: slot of the track, or the one of its rank", "[engine][dma]") {
    Rig g;
    DmaParams p;
    for (auto& s : p.slots) s.loop = true;
    g.e.setDma(p);
    for (int k = 0; k < ymengine::kDmaSlots; ++k) g.e.setDmaSample(k, sine(2000, 20 + k));
    g.dmaTrack(10, -1);  // 16-channel: track of index 10, slot 10 & 7 = 2
    g.e.noteOn(0, 60, 127, 10);
    g.run(480);
    REQUIRE(g.playing().size() == 1);
    CHECK(g.playing()[0].slot == 2);
    g.e.allNotesOff(0);
    g.run(4800);
    g.dmaTrack(10, 6);
    g.e.noteOn(0, 60, 127, 10);
    g.run(480);
    REQUIRE(g.playing().size() == 1);
    CHECK(g.playing()[0].slot == 6);
    CHECK(g.e.dmaPlayingSlots() == 1u << 6);
    // Empty slot: nothing plays.
    g.e.allNotesOff(0);
    g.run(4800);
    g.e.setDmaSample(6, {});
    g.e.noteOn(0, 60, 127, 10);
    g.run(480);
    CHECK(g.playing().empty());
}

TEST_CASE("DMA: a new sample stops the voices that play the old one", "[engine][dma]") {
    Rig g;
    g.dmaTrack(0);
    g.e.setDma(params());
    g.e.setDmaSample(0, sine(4096, 32));
    g.e.noteOn(0, 60, 127, 0);
    g.run(480);
    REQUIRE(g.playing().size() == 1);
    g.e.setDmaSample(0, sine(4096, 64));
    g.run(480);
    CHECK(g.playing().empty());
    g.run(9600);  // tail of the output filters
    g.clear();
    g.run(480, true);
    CHECK(silent(g.outL));  // the bus keeps nothing of the stopped voice
}

TEST_CASE("DMA: chords of a DMA track in 16-channel mode", "[engine][dma]") {
    Rig g;
    g.dmaTrack(0);
    g.e.setDma(params());
    g.e.setDmaSample(0, sine(4096, 32));
    for (const int n : {60, 64, 67}) g.e.noteOn(0, n, 127, 0);
    g.run(480);
    const auto v = g.playing();
    REQUIRE(v.size() == 3);
    std::vector<double> steps;
    for (const auto& d : v) steps.push_back(d.step);
    std::sort(steps.begin(), steps.end());
    CHECK(std::fabs(steps[1] - std::pow(2.0, 4 / 12.0)) < 1e-9);
    CHECK(std::fabs(steps[2] - std::pow(2.0, 7 / 12.0)) < 1e-9);
}

TEST_CASE("DMA: stereo, panning, level, mute and solo of the track", "[engine][dma]") {
    auto play = [](double pan, double level, bool mute, bool soloOther, bool stereo, bool rightOnly) {
        Rig g;
        g.dmaTrack(0, 0, pan);
        auto t = g.e.track(0);
        t.mix.level = level;
        t.mix.mute = mute;
        g.e.setTrack(0, t);
        if (soloOther) {
            auto o = g.e.track(1);
            o.mix.enabled = true;
            o.mix.solo = true;
            g.e.setTrack(1, o);
        }
        g.e.setDma(params(2, stereo));
        g.e.setDmaSample(0, sine(4096, 32, stereo, rightOnly));
        g.e.noteOn(0, 60, 127, 0);
        g.run(2400);
        g.run(9600, true);
        return std::pair{rms(g.outL), rms(g.outR)};
    };
    const auto [cl, cr] = play(0, 1, false, false, false, false);
    CHECK(cl > 0.01);
    CHECK(std::fabs(cl / cr - 1) < 0.01);
    const auto [ll, lr] = play(-1, 1, false, false, false, false);
    CHECK(lr < ll * 0.01);  // hard left
    const auto [hl, hr] = play(0, 0.5, false, false, false, false);
    CHECK(std::fabs(hl / cl - 0.5) < 0.02);
    (void)hr;
    const auto [ml, mr] = play(0, 1, true, false, false, false);
    CHECK(ml == 0);
    CHECK(mr == 0);
    const auto [sl, sr] = play(0, 1, false, true, false, false);
    CHECK(sl == 0);
    CHECK(sr == 0);
    // Stereo: the left channel of the sample (silent) stays on the left.
    const auto [stl, str] = play(0, 1, false, false, true, true);
    CHECK(str > 0.01);
    CHECK(stl < str * 0.01);
}

TEST_CASE("DMA: velocity as accent gain", "[engine][dma]") {
    auto level = [](int vel) {
        Rig g;
        g.dmaTrack(0);
        g.e.setDma(params());
        g.e.setDmaSample(0, sine(4096, 32));
        g.e.noteOn(0, 60, vel, 0);
        g.run(2400);
        g.run(4800, true);
        return rms(g.outL);
    };
    const double hi = level(127), lo = level(40);
    CHECK(hi > 0.01);
    CHECK(lo < hi * 0.9);
    CHECK(lo > 0);
}

TEST_CASE("DMA: smooth ADSR taken at each frame (attack, release)", "[engine][dma]") {
    Rig g;
    g.dmaTrack(0);
    ymengine::TrackParams p = g.e.track(0);
    p.patch.attackMs = 100;
    p.patch.releaseMs = 100;
    g.e.setTrack(0, p);
    g.e.setDma(params());
    g.e.setDmaSample(0, sine(4096, 32));
    g.e.noteOn(0, 60, 127, 0);
    g.run(14400, true);  // 300 ms
    auto part = [&](size_t from, size_t to) {
        return rms(std::vector<float>(g.outL.begin() + long(from), g.outL.begin() + long(to)));
    };
    const double start = part(0, 960), late = part(3840, 4800), held = part(9600, 14400);
    CHECK(start < late * 0.5);  // 0-20 ms against 80-100 ms of the attack
    CHECK(late > held * 0.7);
    g.clear();
    g.e.noteOff(0, 60, 0);
    g.run(4800, true);  // 100 ms of release
    CHECK(part(0, 960) > part(3840, 4800) * 2);
    CHECK(part(0, 960) < held);
    g.run(4800);
    CHECK(g.playing().empty());
}

TEST_CASE("DMA: same mix with the separate outputs of the tracks", "[engine][dma]") {
    // The DMA voices take a direct path without a separate output (runPcmFast) and the common path with one: the
    // left and right mix must stay the same, ADSR ramps included.
    auto render = [](bool separate) {
        Engine e;
        ymengine::EngineConfig cfg;
        cfg.render.hostRate = kRate;
        cfg.render.board.chain = ymcore::BoardChain::STE;
        cfg.trackOutputs = separate ? 3 : 0;
        e.prepare(cfg);
        ymengine::GlobalParams gp;
        gp.ste = true;
        e.setGlobal(gp);
        DmaParams d = params(3, true, true);
        d.slots[1] = {1, false, 57, false};
        e.setDma(d);
        e.setDmaSample(0, sine(3000, 40, true));
        e.setDmaSample(1, sine(5000, 25));
        for (int t = 0; t < 2; ++t) {
            ymengine::TrackParams p = e.track(t);
            p.patch.generator = Generator::Dma;
            p.patch.dmaSlot = int8_t(t);
            p.patch.attackMs = 20;
            p.patch.decayMs = 60;
            p.patch.sustainPct = 50;
            p.patch.releaseMs = 40;
            p.mix.enabled = true;
            p.mix.level = 0.8;
            p.mix.pan = t == 0 ? -0.5 : 0.3;
            e.setTrack(t, p);
        }
        std::vector<float> l(9600), r(9600), a(9600), b(9600), out;
        ymengine::Outputs o;
        o.left = l.data();
        o.right = r.data();
        if (separate) {
            o.track[0] = a.data();
            o.track[1] = b.data();
        }
        e.noteOn(0, 64, 100, 0);
        e.noteOn(100, 55, 127, 1);
        for (int k = 0; k < 3; ++k) {
            if (k == 1) {
                e.noteOff(500, 64, 0);
                e.noteOff(700, 55, 1);
            }
            e.render(o, uint32_t(l.size()));
            out.insert(out.end(), l.begin(), l.end());
            out.insert(out.end(), r.begin(), r.end());
        }
        return out;
    };
    const std::vector<float> direct = render(false), common = render(true);
    REQUIRE(direct.size() == common.size());
    double diff = 0, top = 0;
    for (size_t i = 0; i < direct.size(); ++i) {
        diff = std::max(diff, double(std::fabs(direct[i] - common[i])));
        top = std::max(top, double(std::fabs(direct[i])));
    }
    CHECK(top > 0.01);
    CHECK(diff < 1e-6);
}

TEST_CASE("DMA: mixed before the STE chain (LMC1992 bass)", "[engine][dma]") {
    auto low = [](int bassDb) {
        Rig g(VoiceMode::Studio16, true, ymcore::BoardChain::STE, bassDb);
        g.dmaTrack(0);
        g.e.setDma(params(0));
        g.e.setDmaSample(0, sine(8000, 64));  // 6 258 / 64 ≈ 98 Hz
        g.e.noteOn(0, 60, 127, 0);
        g.run(4800);
        g.run(9600, true);
        return rms(g.outL);
    };
    CHECK(low(12) > low(0) * 1.5);
}

TEST_CASE("DMA: DMA tracks in Authentic 3ch with the STE machine (5 to 12)", "[engine][dma][3ch]") {
    Rig g(VoiceMode::Authentic3, true);
    DmaParams p;
    for (auto& s : p.slots) s.loop = true;
    g.e.setDma(p);
    for (int k = 0; k < ymengine::kDmaSlots; ++k) g.e.setDmaSample(k, sine(4096, 32));
    for (int t = 4; t < 12; ++t) {
        auto tp = g.e.track(t);
        tp.mix.enabled = true;
        tp.mix.level = 1;
        tp.patch.generator = Generator::Chip;  // the role is imposed: DMA whatever the setting
        tp.mix.pan = t == 5 ? 1.0 : 0.0;
        g.e.setTrack(t, tp);
    }
    // At rest: the output stays mono (the ST).
    g.run(2400, true);
    CHECK(g.outL == g.outR);
    g.clear();
    // Track 6 (DMA 2): slot 2 (its rank), on the right.
    g.e.noteOn(0, 60, 127, 5);
    g.run(2400);
    g.run(9600, true);
    const auto v = g.playing();
    REQUIRE(v.size() == 1);
    CHECK(v[0].slot == 1);
    CHECK(rms(g.outR) > 0.01);
    CHECK(rms(g.outL) < rms(g.outR) * 0.01);
    // The eight tracks together, one voice each.
    for (int t = 4; t < 12; ++t) g.e.noteOn(0, 60, 127, t);
    g.run(480);
    CHECK(g.playing().size() == 8);
    CHECK(g.e.dmaPlayingSlots() == 0xff);
    // A second note on a DMA track replaces the first one (one voice per track).
    g.e.noteOn(0, 72, 127, 4);
    g.run(480);
    CHECK(g.playing().size() == 8);
    // Meter of the DMA track.
    Engine::TrackMeters m;
    g.e.takeMeters(m);
    g.run(2400);
    g.e.takeMeters(m);
    CHECK(m.peak[4] > 0.0f);
    CHECK(m.peak[11] > 0.0f);
    CHECK(m.peak[12] == 0.0f);
}

TEST_CASE("DMA: Dual chip and ST machine", "[engine][dma][3ch]") {
    {
        Rig g(VoiceMode::Dual3, true);
        g.e.setDma(params());
        g.e.setDmaSample(0, sine(4096, 32));
        auto tp = g.e.track(8);
        tp.mix.enabled = true;
        g.e.setTrack(8, tp);
        g.e.noteOn(0, 60, 127, 8);  // track 9: DMA 1, slot 1
        g.run(480);
        REQUIRE(g.playing().size() == 1);
        CHECK(g.playing()[0].slot == 0);
    }
    {
        Rig g(VoiceMode::Authentic3, false);
        g.e.setDma(params());
        g.e.setDmaSample(0, sine(4096, 32));
        auto tp = g.e.track(4);
        tp.mix.enabled = true;
        tp.patch.generator = Generator::Dma;
        g.e.setTrack(4, tp);
        g.e.noteOn(0, 60, 127, 4);
        g.run(480);
        CHECK(g.playing().empty());
    }
}

TEST_CASE("DMA: changing machine and the panic cut the DMA voices", "[engine][dma]") {
    Rig g;
    g.dmaTrack(0);
    g.e.setDma(params());
    g.e.setDmaSample(0, sine(4096, 32));
    g.e.noteOn(0, 60, 127, 0);
    g.run(480);
    REQUIRE(g.playing().size() == 1);
    g.e.allNotesOff(0);
    g.run(4800);
    CHECK(g.playing().empty());
    g.e.noteOn(0, 60, 127, 0);
    g.run(480);
    REQUIRE(g.playing().size() == 1);
    g.g.ste = false;
    g.e.setGlobal(g.g);
    g.run(4800);
    CHECK(g.playing().empty());
    CHECK(g.e.activeVoices() == 0);
}

TEST_CASE("DMA: pitch bend of the track", "[engine][dma]") {
    Rig g;
    g.dmaTrack(0);
    g.e.setDma(params());
    g.e.setDmaSample(0, sine(4096, 32));
    g.e.noteOn(0, 60, 127, 0);
    g.e.pitchBend(0, 2, 0);
    g.run(4800);
    REQUIRE(g.playing().size() == 1);
    CHECK(std::fabs(g.playing()[0].step - std::pow(2.0, 2 / 12.0)) < 1e-6);
}

TEST_CASE("DMA: preview of a slot from Start to End, whatever the machine", "[engine][dma]") {
    for (const bool ste : {false, true}) {
        CAPTURE(ste);
        Rig g(VoiceMode::Authentic3, ste);
        g.e.setDma(params(2, false, false));
        g.e.setDmaSample(0, sine(2500, 32));  // 0.1 s, once (Loop off)
        g.e.dmaPreview(0, 0);
        g.run(480, true);
        CHECK(g.e.dmaPreviewSlot() == 0);
        CHECK(rms(g.outL) > 0.01);
        g.run(9600);
        CHECK(g.e.dmaPreviewSlot() == -1);
        g.e.dmaPreview(0, 0);
        g.run(48);
        g.e.allNotesOff(0);
        g.run(48);
        CHECK(g.e.dmaPreviewSlot() == -1);
        g.e.dmaPreview(0, 0);
        g.run(48);
        g.e.dmaStopPreview(0);
        g.run(48);
        CHECK(g.e.dmaPreviewSlot() == -1);
        // From Start to End (D-444): 500 frames, 20 ms.
        DmaParams p = params(2, false, false);
        p.slots[0].start = 2000;
        g.e.setDma(p);
        g.e.dmaPreview(0, 0);
        g.run(480);
        CHECK(g.e.dmaPreviewSlot() == 0);
        g.run(960);
        CHECK(g.e.dmaPreviewSlot() == -1);
    }
}

TEST_CASE("DMA: with Loop on, the preview repeats the part until stopped or Loop goes off (Indus, 26/10/2026, D-449)",
          "[engine][dma]") {
    Rig g(VoiceMode::Studio16, true);
    DmaParams p = params(2, false, true);
    p.slots[0].start = 2000;  // part of 500 frames, 20 ms
    g.e.setDma(p);
    g.e.setDmaSample(0, sine(2500, 32));
    // Ten times the part later, it still sounds; stopped, it falls silent.
    g.e.dmaPreview(0, 0);
    g.run(9600);
    CHECK(g.e.dmaPreviewSlot() == 0);
    g.run(480, true);
    CHECK(rms(g.outL) > 0.01);
    g.e.dmaStopPreview(0);
    g.run(48);
    CHECK(g.e.dmaPreviewSlot() == -1);
    g.outL.clear();
    g.run(480, true);
    CHECK(rms(g.outL) < 1e-6);
    // Loop switched off while it loops: the preview stops at once.
    g.e.dmaPreview(0, 0);
    g.run(4800);
    REQUIRE(g.e.dmaPreviewSlot() == 0);
    p.slots[0].loop = false;
    g.e.setDma(p);
    g.run(48);
    CHECK(g.e.dmaPreviewSlot() == -1);
    // Started with Loop off, it plays once; Loop switched on while it plays makes it loop.
    g.e.dmaPreview(0, 0);
    g.run(48);
    p.slots[0].loop = true;
    g.e.setDma(p);
    g.run(9600);
    CHECK(g.e.dmaPreviewSlot() == 0);
    g.e.allNotesOff(0);
    g.run(48);
    CHECK(g.e.dmaPreviewSlot() == -1);
}

TEST_CASE("DMA: a stereo sample gives the Mixer the peaks and oscilloscope of each side (Indus, 26/10/2026, D-457)",
          "[engine][dma][mixer]") {
    // Left side silent, right side a sine: left = scope − side stays at zero, right = scope + side carries the sine.
    // 16 channels (track 1) and Authentic 3ch with the STE (track 5, the first DMA track).
    for (const auto& [mode, track] : {std::pair{VoiceMode::Studio16, 1}, std::pair{VoiceMode::Authentic3, 4}}) {
        CAPTURE(int(mode));
        for (const bool stereo : {true, false}) {
            CAPTURE(stereo);
            Rig g(mode);
            g.dmaTrack(track, 0);
            g.e.setDma(params(2, stereo));
            g.e.setDmaSample(0, sine(4096, 32, stereo, true));
            g.e.noteOn(0, 60, 127, track);
            Engine::TrackMeters m;
            g.run(2400);
            g.e.takeMeters(m);
            g.run(2400);  // 50 ms: the 128 points of the oscilloscope all written while the sample plays
            g.e.takeMeters(m);
            const size_t t = size_t(track);
            float right = 0;
            for (int j = 0; j < Engine::kScopePoints; ++j) {
                const float s = m.scope[t][size_t(j)], d = m.side[t][size_t(j)];
                if (stereo) {
                    CHECK(std::abs(s - d) < 1e-6f);
                } else {
                    CHECK(d == 0.0f);
                }
                right = std::max(right, s + d);
            }
            CHECK(m.peakR[t] > 0.1f);
            CHECK(right > m.peakR[t] * 0.5f);
            CHECK(right <= m.peakR[t] + 1e-6f);
            if (stereo) {
                CHECK(m.peak[t] == 0.0f);
            } else {
                CHECK(m.peak[t] == m.peakR[t]);
            }
            // No other track has a side.
            for (int o = 0; o < ymengine::kTracks; ++o)
                if (o != track)
                    CHECK(std::all_of(m.side[size_t(o)].begin(), m.side[size_t(o)].end(),
                                      [](float v) { return v == 0.0f; }));
            // The note stops (after its release): the side goes back to an exact zero.
            g.e.allNotesOff(0);
            g.run(9600);
            g.e.takeMeters(m);
            g.run(2400);
            g.e.takeMeters(m);
            CHECK(std::all_of(m.side[t].begin(), m.side[t].end(), [](float v) { return v == 0.0f; }));
            CHECK(m.peak[t] == 0.0f);
            CHECK(m.peakR[t] == 0.0f);
        }
    }
}

TEST_CASE("DMA: the right side of a stereo sample has its own fader, mute and solo (Indus, 26/10/2026, D-459)",
          "[engine][dma][mixer]") {
    // Track A plays a stereo sample (both sides the same sine), centred; track B a mono sample, hard left. 16 channels
    // (tracks 1 and 2) and Authentic 3ch with the STE (tracks 5 and 6, DMA tracks).
    for (const auto& [mode, a] : {std::pair{VoiceMode::Studio16, 0}, std::pair{VoiceMode::Authentic3, 4}}) {
        CAPTURE(int(mode));
        auto play = [mode, a](bool stereo, auto setMix, bool withB) {
            Rig g(mode);
            g.dmaTrack(a, 0);
            g.dmaTrack(a + 1, 1, -1);
            DmaParams d = params(2, stereo);
            d.slots[1] = {2, false, 60, true};
            g.e.setDma(d);
            g.e.setDmaSample(0, sine(4096, 32, stereo));
            g.e.setDmaSample(1, sine(4096, 40));
            ymengine::TrackParams p = g.e.track(a);
            setMix(p.mix);
            g.e.setTrack(a, p);
            g.e.noteOn(0, 60, 127, a);
            if (withB) g.e.noteOn(0, 60, 127, a + 1);
            g.run(2400);
            g.run(9600, true);
            Engine::TrackMeters m;
            g.e.takeMeters(m);
            return std::tuple{rms(g.outL), rms(g.outR), m.peak[size_t(a)], m.peakR[size_t(a)]};
        };
        // Linked (the default): levelR is not used, both sides at the level.
        const auto [l, r, pl, pr] = play(true, [](ymengine::TrackMix& m) { m.levelR = 0.25; }, false);
        CHECK(l > 0.01);
        CHECK(std::fabs(r / l - 1) < 0.01);
        CHECK(std::fabs(pr / pl - 1) < 0.01);
        // Unlinked: the right side at its own level, its meter too.
        const auto [ul, ur, upl, upr] = play(true,
                                             [](ymengine::TrackMix& m) {
                                                 m.link = false;
                                                 m.levelR = 0.5;
                                             },
                                             false);
        CHECK(std::fabs(ul / l - 1) < 0.01);
        CHECK(std::fabs(ur / r - 0.5) < 0.02);
        CHECK(std::fabs(upr / upl - 0.5) < 0.02);
        // Mute of either side.
        const auto [ml, mr, mpl, mpr] = play(true, [](ymengine::TrackMix& m) { m.muteR = true; }, false);
        CHECK(ml > 0.01);
        CHECK(mr == 0);
        CHECK(mpl > 0.1f);
        CHECK(mpr == 0.0f);
        const auto [nl, nr, npl, npr] = play(true, [](ymengine::TrackMix& m) { m.mute = true; }, false);
        CHECK(nl == 0);
        CHECK(nr > 0.01);
        CHECK(npl == 0.0f);
        CHECK(npr > 0.1f);
        // Solo of the right side: the left side and track B (hard left) fall silent.
        const auto [bl, br, bpl, bpr] = play(true, [](ymengine::TrackMix&) {}, true);
        CHECK(bl > l * 1.2);
        const auto [sl, sr, spl, spr] = play(true, [](ymengine::TrackMix& m) { m.soloR = true; }, true);
        CHECK(sl == 0);
        CHECK(std::fabs(sr / r - 1) < 0.01);
        (void)bpl, (void)bpr, (void)spl, (void)spr;
        // A mono sample: the right side's settings do nothing, its solo either.
        auto right = [](ymengine::TrackMix& m) {
            m.link = false;
            m.levelR = 0;
            m.muteR = true;
            m.soloR = true;
        };
        const auto [ol, orr, opl, opr] = play(false, right, true);
        const auto [rl, rr, rpl, rpr] = play(false, [](ymengine::TrackMix&) {}, true);
        CHECK(ol > 0.01);
        CHECK(std::fabs(ol / rl - 1) < 1e-6);
        CHECK(std::fabs(orr / rr - 1) < 1e-6);
        CHECK(opr == opl);
        (void)rpl, (void)rpr;
    }
}

TEST_CASE("DMA: the right side's mixer applies live, and when the sample becomes stereo (D-459)",
          "[engine][dma][mixer]") {
    Rig g;
    g.dmaTrack(0, 0);
    g.e.setDma(params(2, true));
    g.e.setDmaSample(0, sine(4096, 32, true));
    g.e.noteOn(0, 60, 127, 0);
    g.run(2400);
    ymengine::TrackMix m = g.e.track(0).mix;
    m.link = false;
    m.levelR = 0;
    g.e.setTrackMix(0, 0, m);
    g.run(4800);  // the tail of the STE chain's filters
    g.run(4800, true);
    CHECK(rms(g.outL) > 0.01);
    CHECK(rms(g.outR) < rms(g.outL) * 1e-4);
    CHECK(g.e.trackStereo(0));
    // The same settings with a mono slot: the right side follows the left one.
    g.e.setDma(params(2, false));
    CHECK_FALSE(g.e.trackStereo(0));
    g.e.setDmaSample(0, sine(4096, 32));
    g.e.noteOn(0, 60, 127, 0);
    g.clear();
    g.run(480);
    g.run(4800, true);
    CHECK(rms(g.outR) > 0.01);
    CHECK(std::fabs(rms(g.outR) / rms(g.outL) - 1) < 0.01);
}
