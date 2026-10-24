// STYM2149 — virtual channel of the 16-channel mode against channel A of ymcore::Ym2149 (step by step)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "ymcore/Ym2149.h"
#include "ymengine/VirtualChannel.h"

using namespace stym;

namespace {
struct Rng {
    uint64_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return uint32_t(s >> 11);
    }
};

// Compares n steps of channel A (B and C muted) with the virtual channel, with randomly drawn writes.
// Returns the number of steps whose output index differs, and counts the output changes.
uint64_t compare(ymcore::NoiseLfsr noise, uint64_t seed, uint64_t ticks, uint64_t& changes) {
    ymcore::ChipConfig cfg;
    cfg.noise = noise;
    cfg.edgeSeed = uint32_t(seed);
    ymcore::Ym2149 chip(cfg);
    chip.setVoiceMute(0b110);
    ymengine::VirtualChannel v;
    v.reset(noise, (chip.state().toneEdges & 1u) != 0);

    Rng rng{seed * 0x9e3779b97f4a7c15ull + 1};
    uint64_t bad = 0, t = 0;
    uint8_t cur = 0xff;
    std::vector<uint8_t> got;
    while (t < ticks) {
        // One write, then a very variable waiting time (1 step to ~8 000 steps).
        const uint32_t r = rng.next();
        const uint8_t value = uint8_t(rng.next());
        switch (r % 9) {
        case 0:
            chip.writeRegister(0, value);
            chip.writeRegister(1, uint8_t(value & 1));  // short and long periods
            v.setTonePeriod(uint16_t(chip.readRegister(0) | (chip.readRegister(1) << 8)));
            break;
        case 1:
            chip.writeRegister(1, uint8_t(value & 15));
            v.setTonePeriod(uint16_t(chip.readRegister(0) | (chip.readRegister(1) << 8)));
            break;
        case 2:
            chip.writeRegister(6, value);
            v.setNoisePeriod(value);
            break;
        case 3:
            chip.writeRegister(7, value);
            v.setMixer(!(value & 1), !(value & 8));
            break;
        case 4:
        case 5:
            chip.writeRegister(8, value);
            v.setVolume(value);
            break;
        case 6:
            chip.writeRegister(11, uint8_t(value & 7));
            chip.writeRegister(12, 0);
            v.setEnvPeriod(uint16_t(value & 7));
            break;
        case 7:
            chip.writeRegister(11, value);
            chip.writeRegister(12, uint8_t(rng.next() & 3));
            v.setEnvPeriod(uint16_t(chip.readRegister(11) | (chip.readRegister(12) << 8)));
            break;
        case 8:
            chip.writeRegister(13, value);
            v.writeShape(value);
            break;
        }
        const uint64_t wait = std::min<uint64_t>(ticks - t, 1 + (rng.next() % 4 == 0 ? rng.next() % 8000 : rng.next() % 40));
        got.assign(size_t(wait), 0);
        uint64_t filled = 0;
        v.run(wait, [&](uint64_t at, uint8_t index) {
            for (; filled < at; ++filled) got[size_t(filled)] = cur;
            cur = index;
            ++changes;
        });
        for (; filled < wait; ++filled) got[size_t(filled)] = cur;
        for (uint64_t k = 0; k < wait; ++k) {
            chip.tick();
            bad += (chip.levels() & 0x1fu) != got[size_t(k)];
        }
        t += wait;
    }
    // Identical final state.
    const ymcore::Ym2149::State st = chip.state();
    bad += st.toneCounter[0] != v.toneCounter();
    bad += (st.toneEdges & 1u) != uint32_t(v.toneEdge());
    bad += st.lfsr != v.lfsr();
    bad += st.envCounter != v.envCounter();
    bad += st.envPos != v.envPos();
    return bad;
}
}  // namespace

TEST_CASE("Virtual channel: identical to channel A of the chip, step by step", "[channel]") {
    for (ymcore::NoiseLfsr noise : {ymcore::NoiseLfsr::Maximal, ymcore::NoiseLfsr::AtariAudio}) {
        uint64_t changes = 0, bad = 0;
        for (uint64_t seed = 1; seed <= 24; ++seed) bad += compare(noise, seed, 400'000, changes);
        INFO("LFSR " << int(noise) << ", " << changes << " output changes");
        CHECK(bad == 0);
        CHECK(changes > 100'000);
    }
}

TEST_CASE("Virtual channel: skips without a change of output", "[channel]") {
    // Fixed volume, tone and noise off: a single emission over a million steps.
    ymengine::VirtualChannel v;
    v.reset(ymcore::NoiseLfsr::Maximal, true);
    v.setVolume(12);
    v.setNoisePeriod(1);
    int emits = 0;
    v.run(1'000'000, [&](uint64_t, uint8_t index) {
        ++emits;
        CHECK(index == 25);
    });
    CHECK(emits == 1);
    // Square of period 100: one change every 100 steps, at steps 100, 200… 9 900 (the high edge at the start
    // extends the previous output).
    v.setTonePeriod(100);
    v.setMixer(true, false);
    emits = 0;
    v.run(10'000, [&](uint64_t at, uint8_t) {
        ++emits;
        CHECK(at % 100 == 0);
    });
    CHECK(emits == 99);
}
