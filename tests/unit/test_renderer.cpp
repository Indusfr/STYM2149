// STYM2149 — timestamped writes, queue, output modes, S10 (determinism)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <vector>

#include "Metrics.h"
#include "Scenarios.h"
#include "ymcore/ChipRenderer.h"
#include "ymcore/TickStreamOutput.h"

using namespace stym::ymcore;
using namespace stym::test;

namespace {
// Renders a scenario whose instants are host samples, in blocks of a given size; the writes of a block are
// passed just before its render (as the engine will do).
std::vector<float> renderBlocks(const RenderConfig& rc, const Scenario& sc, size_t total, uint32_t block) {
    ChipRenderer r;
    r.prepare(rc);
    std::vector<float> out(total);
    size_t next = 0;
    for (size_t done = 0; done < total;) {
        const uint32_t n = uint32_t(std::min<size_t>(block, total - done));
        while (next < sc.writes.size() && sc.writes[next].at < done + n) {
            const TimedWrite& w = sc.writes[next++];
            r.write(r.cycleAtSample(w.at), w.reg, w.value);
        }
        r.render(out.data() + done, n);
        done += n;
    }
    return out;
}

bool bitIdentical(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

const char* modeName(OutputMode m) { return m == OutputMode::Raw ? "Raw" : m == OutputMode::RawBox ? "RawBox" : "Retro"; }
}  // namespace

TEST_CASE("Write at cycle c applied before internal step ceil(c / 8)", "[timestamps]") {
    RenderConfig rc;
    rc.hostRate = 250000;  // one internal step per sample in RawBox
    rc.mode = OutputMode::RawBox;
    rc.dc = DcFilter::None;
    for (uint64_t cycle : {0ull, 1ull, 7ull, 8ull, 9ull, 80ull, 81ull, 88ull}) {
        ChipRenderer r;
        r.prepare(rc);
        r.write(cycle, 8, 15);  // R7 = $3F at reset: channel A goes to level 31
        std::vector<float> out(32);
        r.render(out.data(), 32);
        size_t first = 0;
        while (first < out.size() && out[first] == out[0] && cycle > 0) ++first;
        INFO("cycle " << cycle);
        CHECK(first == (cycle + 7) / 8);
    }
}

TEST_CASE("Channel levels at the last step of each sample (meters)", "[blocks]") {
    // Retro without DC removal: the output is the mixing table at the last internal step, so at the levels
    // rendered with it. Channel A in tone at volume 15, B and C off at volume 0 (level 1).
    RenderConfig rc;
    rc.mode = OutputMode::Retro;
    rc.dc = DcFilter::None;
    ChipRenderer r;
    r.prepare(rc);
    r.write(0, 0, 100);
    r.write(0, 7, 0x3e);
    r.write(0, 8, 15);
    std::vector<float> out(480), plain(480);
    std::vector<uint16_t> levels(480);
    r.render(out.data(), 480, levels.data());
    int high = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        const uint16_t a = levels[i] & 31u;
        CHECK((a == 0 || a == 31));
        CHECK((levels[i] >> 5) == (1u | 1u << 5));
        CHECK(out[i] == float(tables::kMixTable[levels[i]]) * (1.0f / 32768.0f));
        high += a == 31;
    }
    CHECK(high > 100);
    CHECK(high < 380);
    // Without a levels array, same output.
    ChipRenderer q;
    q.prepare(rc);
    q.write(0, 0, 100);
    q.write(0, 7, 0x3e);
    q.write(0, 8, 15);
    q.render(plain.data(), 480);
    CHECK(bitIdentical(out, plain));
}

TEST_CASE("cycleAtSample: first internal step at the instant of the sample", "[timestamps]") {
    RenderConfig rc;
    rc.hostRate = 48000;
    for (OutputMode m : {OutputMode::Raw, OutputMode::RawBox}) {
        rc.mode = m;
        ChipRenderer r;
        r.prepare(rc);
        CHECK(r.cycleAtSample(0) == 0);
        CHECK(r.cycleAtSample(1) == 8 * 6);       // ceil(250 000 / 48 000) = 6
        CHECK(r.cycleAtSample(48000) == 2000000);  // one second
        CHECK(r.cycleAtSample(3) == 8 * 16);       // ceil(3 × 5.2083) = 16
    }
}

TEST_CASE("Queue: order kept, late writes brought to the previous date, overflow without loss", "[file]") {
    RenderConfig rc;
    rc.mode = OutputMode::RawBox;
    ChipRenderer r;
    r.prepare(rc);
    r.write(100, 8, 5);
    r.write(100, 8, 9);
    r.write(50, 9, 3);  // late: applied at cycle 100, after the other two
    CHECK(r.reorderedWrites() == 1);
    std::vector<float> out(16);
    r.render(out.data(), 16);
    CHECK(r.chip().readRegister(8) == 9);
    CHECK(r.chip().readRegister(9) == 3);

    for (int i = 0; i < 5000; ++i) r.write(1'000'000ull + uint64_t(i), 10, uint8_t(i & 15));
    CHECK(r.forcedWrites() == 5000 - RegisterQueue::kCapacity);
    std::vector<float> big(48000);
    r.render(big.data(), uint32_t(big.size()));
    CHECK(r.chip().readRegister(10) == (4999 & 15));
}

TEST_CASE("Identical render whatever the block size", "[blocks]") {
    const Scenario sc = randomScenario(7, 48000, 20, true);
    for (OutputMode m : {OutputMode::Raw, OutputMode::RawBox, OutputMode::Retro}) {
        RenderConfig rc;
        rc.mode = m;
        const std::vector<float> ref = renderBlocks(rc, sc, 48000, 4096);
        for (uint32_t block : {1u, 7u, 64u, 480u}) {
            INFO(modeName(m) << ", blocks of " << block);
            CHECK(bitIdentical(renderBlocks(rc, sc, 48000, block), ref));
        }
    }
}

TEST_CASE("S10 determinism: two offline renders identical bit for bit", "[S10]") {
    bool all = true;
    for (OutputMode m : {OutputMode::Raw, OutputMode::RawBox, OutputMode::Retro}) {
        for (NoiseLfsr noise : {NoiseLfsr::Maximal, NoiseLfsr::AtariAudio}) {
            RenderConfig rc;
            rc.mode = m;
            rc.chip.noise = noise;
            const Scenario sc = randomScenario(11, 5 * 48000, 30, true);
            const auto a = renderBlocks(rc, sc, 5 * 48000, 512);
            const auto b = renderBlocks(rc, sc, 5 * 48000, 512);
            // Same instance, after reset(): same result.
            ChipRenderer r;
            r.prepare(rc);
            std::vector<float> c(a.size());
            for (int pass = 0; pass < 2; ++pass) {
                r.reset();
                size_t next = 0;
                for (size_t done = 0; done < c.size();) {
                    const uint32_t n = uint32_t(std::min<size_t>(512, c.size() - done));
                    while (next < sc.writes.size() && sc.writes[next].at < done + n) {
                        const TimedWrite& w = sc.writes[next++];
                        r.write(r.cycleAtSample(w.at), w.reg, w.value);
                    }
                    r.render(c.data() + done, n);
                    done += n;
                }
            }
            const bool ok = bitIdentical(a, b) && bitIdentical(a, c);
            all = all && ok;
            INFO(modeName(m));
            CHECK(ok);
        }
    }
    metric("S10", "two 5 s renders (3 modes × 2 noises, new instance and reset())", all ? "identical" : "different",
           "identical bit for bit", all);
}

TEST_CASE("joinSilent: a stream that joins another = one fed silence until then", "[blocks]") {
    for (OutputMode m : {OutputMode::Raw, OutputMode::RawBox, OutputMode::Retro}) {
        INFO(modeName(m));
        RenderConfig rc;
        rc.mode = m;
        TickStreamOutput main, joined, silent;
        main.prepare(rc);
        joined.prepare(rc);
        silent.prepare(rc);
        auto signal = [](uint64_t t) { return float(0.5 + 0.4 * std::sin(double(t) * 0.003)); };
        std::vector<float> ticks, outM(480), outJ(480), outS(480);
        auto feed = [&](TickStreamOutput& o, bool on, float* out) {
            const uint64_t first = o.ticksConsumed(), need = o.ticksNeeded(480);
            ticks.assign(size_t(need - first), 0.0f);
            for (uint64_t t = first; t < need; ++t) ticks[size_t(t - first)] = on ? signal(t) : 0.0f;
            o.render(ticks.data(), first, out, 480);
        };
        for (int block = 0; block < 20; ++block) {
            if (block == 10) joined.joinSilent(main);
            feed(main, true, outM.data());
            feed(silent, block >= 10, outS.data());
            if (block >= 10) {
                feed(joined, true, outJ.data());
                CHECK(std::memcmp(outJ.data(), outS.data(), 480 * sizeof(float)) == 0);
            }
        }
    }
}

TEST_CASE("Modes: levels and DC component", "[modes]") {
    // Square TP = 1000 at volume 15: RawBox and Retro stay within the range of the table, Raw is centred by the
    // DC removal.
    for (OutputMode m : {OutputMode::Raw, OutputMode::RawBox, OutputMode::Retro}) {
        RenderConfig rc;
        rc.mode = m;
        ChipRenderer r;
        r.prepare(rc);
        r.write(0, 0, 0xe8);
        r.write(0, 1, 0x03);
        r.write(0, 7, 0x3e);
        r.write(0, 8, 15);
        std::vector<float> y(48000);
        r.render(y.data(), uint32_t(y.size()));
        double mean = 0, peak = 0;
        for (size_t i = 24000; i < y.size(); ++i) {
            mean += y[i];
            peak = std::max(peak, double(std::fabs(y[i])));
        }
        mean /= 24000.0;
        INFO(modeName(m));
        CHECK(std::fabs(mean) < 0.01);
        CHECK(peak > 0.2);
        CHECK(peak < 0.5);
    }
}
