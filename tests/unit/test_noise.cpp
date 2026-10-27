// STYM2149 — S7: noise generator
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "Metrics.h"
#include "ymcore/Ym2149.h"

using namespace stym::ymcore;
using stym::test::metric;

namespace {
// Advances the chip to the next LFSR shift (NP = 1: one shift every 2 internal steps).
uint32_t nextShift(Ym2149& chip) {
    chip.tick();
    chip.tick();
    return chip.state().noiseOutput & 1u;
}

uint64_t lfsrPeriod(NoiseLfsr mode) {
    ChipConfig c;
    c.noise = mode;
    Ym2149 chip(c);
    chip.writeRegister(6, 1);
    const uint32_t start = chip.state().lfsr;
    for (uint64_t n = 1; n <= 200000; ++n) {
        chip.tick();
        chip.tick();
        if (chip.state().lfsr == start) return n;
    }
    return 0;
}
}  // namespace

TEST_CASE("S7 maximal LFSR: period 131 071 and MAME sequence", "[S7][noise]") {
    const uint64_t period = lfsrPeriod(NoiseLfsr::Maximal);
    metric("S7", "LFSR period (Maximal mode, default)", std::to_string(period), "131071", period == 131071);
    CHECK(period == 131071);

    // Independent reference: MAME src/devices/sound/ay8910.cpp (ay8910_device::sound_stream_update).
    uint32_t rng = 1;
    ChipConfig c;  // Maximal by default
    Ym2149 chip(c);
    chip.writeRegister(6, 1);
    uint64_t mismatches = 0;
    for (int i = 0; i < 2 * 131071; ++i) {
        rng ^= (((rng & 1) ^ ((rng >> 3) & 1)) << 17);
        rng >>= 1;
        const uint32_t expected = rng & 1;
        if (nextShift(chip) != expected) ++mismatches;
    }
    metric("S7", "noise sequence against MAME (262 142 shifts)", std::to_string(mismatches) + " deviation(s)", "0", mismatches == 0);
    CHECK(mismatches == 0);
}

TEST_CASE("S7 AtariAudio LFSR: period 114 681 (not maximal, as measured)", "[S7][noise]") {
    CHECK(lfsrPeriod(NoiseLfsr::AtariAudio) == 114681);
}

TEST_CASE("S7 noise: one shift every 2 × NP internal steps", "[S7][S6][noise]") {
    for (uint8_t np : {uint8_t(1), uint8_t(5), uint8_t(31)}) {
        Ym2149 chip;
        chip.writeRegister(6, np);
        uint32_t lfsr = chip.state().lfsr;
        int shifts = 0;
        const int ticks = 2 * np * 1000;
        for (int t = 0; t < ticks; ++t) {
            chip.tick();
            if (chip.state().lfsr != lfsr) {
                ++shifts;
                lfsr = chip.state().lfsr;
            }
        }
        INFO("NP = " << int(np));
        CHECK(shifts == 1000);
    }
}

TEST_CASE("S7 noise: R6 on 5 bits", "[S7][noise]") {
    Ym2149 a, b;
    a.writeRegister(6, 0x25);  // bit 5 ignored
    b.writeRegister(6, 0x05);
    CHECK(a.state().noisePeriod == b.state().noisePeriod);
}
