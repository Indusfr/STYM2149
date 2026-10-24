// STYM2149 — S1 (internal state step by step) and S2 (RawBox mode) against AtariAudio 1.26, synthetic scenarios
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <vector>

#include "Metrics.h"
#include "OracleCommon.h"
#include "Scenarios.h"
#include "ymcore/ChipRenderer.h"

using namespace stym::ymcore;
using namespace stym::test;

namespace {
struct TickResult {
    uint64_t ticks = 0;
    uint64_t mismatches = 0;
    std::string first;
};

// ymcore chip and Ym2149c set to clk/8 (one step per sample), same writes at the same steps.
TickResult compareTicks(const Scenario& sc, NoiseLfsr noise, uint64_t ticks) {
    ChipConfig cfg;
    cfg.noise = noise;
    auto aa = std::make_unique<Ym2149c>();
    cfg.edgeSeed = takeAtariAudioSeed();
    aa->Reset(250000, 2000000);
    const uint8_t* envBase = aa->m_pCurrentEnv;
    Ym2149 chip(cfg);
    const bool aaNoise = noise == NoiseLfsr::AtariAudio;
    TickResult res;
    size_t next = 0;
    for (uint64_t t = 0; t < ticks; ++t) {
        while (next < sc.writes.size() && sc.writes[next].at <= t) {
            const TimedWrite& w = sc.writes[next++];
            aa->WritePort(0, w.reg);
            aa->WritePort(2, w.value);
            chip.writeRegister(w.reg, w.value);
        }
        aa->ComputeNextSample();
        const int ref = atariAudioLastTick(*aa);
        const int got = chip.tick();
        std::string d = compareChipState(chip, *aa, envBase, aaNoise);
        if (d.empty() && (aaNoise || !sc.usesNoise) && got != ref)
            d = "output: ymcore " + std::to_string(got) + ", AtariAudio " + std::to_string(ref);
        ++res.ticks;
        if (!d.empty()) {
            if (!res.mismatches) res.first = "step " + std::to_string(t) + ": " + d;
            ++res.mismatches;
        }
    }
    return res;
}

std::vector<Scenario> tickScenarios(bool noise) {
    std::vector<Scenario> v;
    for (const Scenario& s : m0Scenarios())
        if (noise || !s.usesNoise) v.push_back(s);
    for (uint32_t seed = 1; seed <= 4; ++seed) v.push_back(randomScenario(seed, 2 * 250000, 2, noise));
    return v;
}
}  // namespace

TEST_CASE("S1 internal state step by step against AtariAudio (AtariAudio noise)", "[S1][oracle]") {
    uint64_t total = 0, bad = 0;
    for (const Scenario& sc : tickScenarios(true)) {
        const TickResult r = compareTicks(sc, NoiseLfsr::AtariAudio, 2 * 250000);
        total += r.ticks;
        bad += r.mismatches;
        INFO(sc.name << ": " << r.first);
        CHECK(r.mismatches == 0);
    }
    metric("S1", "internal state and output, synthetic scenarios, AtariAudio noise (" + std::to_string(total) + " steps)",
           std::to_string(bad) + " deviation(s)", "0", bad == 0);
}

TEST_CASE("S1 internal state step by step against AtariAudio corrected for D-012 (maximal noise)", "[S1][oracle]") {
    uint64_t total = 0, bad = 0;
    for (const Scenario& sc : tickScenarios(true)) {
        // All the state except the LFSR; the output only when no channel listens to the noise.
        const TickResult r = compareTicks(sc, NoiseLfsr::Maximal, 2 * 250000);
        total += r.ticks;
        bad += r.mismatches;
        INFO(sc.name << ": " << r.first);
        CHECK(r.mismatches == 0);
    }
    metric("S1", "internal state except LFSR (and output without noise), maximal noise (" + std::to_string(total) + " steps)",
           std::to_string(bad) + " deviation(s)", "0", bad == 0);
}

TEST_CASE("S2 RawBox mode against Ym2149c::ComputeNextSample", "[S2][oracle]") {
    for (NoiseLfsr noise : {NoiseLfsr::AtariAudio, NoiseLfsr::Maximal}) {
        uint64_t total = 0, bad = 0;
        for (uint32_t rate : {44100u, 48000u, 96000u}) {
            std::vector<Scenario> scs;
            for (const Scenario& s : m0Scenarios())
                if (noise == NoiseLfsr::AtariAudio || !s.usesNoise) scs.push_back(s);
            for (uint32_t seed = 10; seed < 14; ++seed)
                scs.push_back(randomScenario(seed, uint64_t(rate) * 3, 20, noise == NoiseLfsr::AtariAudio));
            for (const Scenario& sc : scs) {
                RenderConfig rc;
                rc.hostRate = rate;
                rc.mode = OutputMode::RawBox;
                rc.chip.noise = noise;
                rc.chip.edgeSeed = takeAtariAudioSeed();
                auto aa = std::make_unique<Ym2149c>();
                aa->Reset(rate, 2000000);
                ChipRenderer r;
                r.prepare(rc);
                const size_t n = size_t(rate) * 3;
                std::vector<int16_t> ref(n);
                std::vector<float> got(n);
                size_t next = 0;
                for (size_t i = 0; i < n; ++i) {
                    while (next < sc.writes.size() && sc.writes[next].at <= i) {
                        const TimedWrite& w = sc.writes[next++];
                        aa->WritePort(0, w.reg);
                        aa->WritePort(2, w.value);
                    }
                    ref[i] = aa->ComputeNextSample();
                }
                // ymcore: writes of a block passed before its render (the queue has a fixed capacity).
                next = 0;
                for (size_t done = 0; done < n;) {
                    const uint32_t k = uint32_t(std::min<size_t>(256, n - done));
                    while (next < sc.writes.size() && sc.writes[next].at < done + k) {
                        const TimedWrite& w = sc.writes[next++];
                        r.write(r.cycleAtSample(w.at), w.reg, w.value);
                    }
                    r.render(got.data() + done, k);
                    done += k;
                }
                uint64_t d = 0;
                for (size_t i = 0; i < n; ++i) d += int(got[i] * 32768.0f) != ref[i];
                total += n;
                bad += d;
                INFO(sc.name << " at " << rate << " Hz");
                CHECK(d == 0);
            }
        }
        metric("S2", std::string("RawBox against ComputeNextSample, 44.1/48/96 kHz, noise ") +
                         (noise == NoiseLfsr::AtariAudio ? "AtariAudio" : "maximal (scenarios without noise)") + " (" +
                         std::to_string(total) + " samples)",
               std::to_string(bad) + " deviation(s)", "0", bad == 0);
    }
}
