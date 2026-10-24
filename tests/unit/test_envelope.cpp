// STYM2149 — S8: 16 values of R13 against an independent model derived from the CONT/ATT/ALT/HOLD bits
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "ymcore/Ym2149.h"

using namespace stym::ymcore;

namespace {
// YM2149 datasheet p. 7: 32 steps per cycle. ATT = first cycle rising. CONT = 0: after the first cycle,
// level 0 held. CONT = 1 and HOLD = 1: the end of the cycle is kept (inverted if ALT). CONT = 1 and HOLD = 0:
// repeated, changing direction at each cycle if ALT.
std::vector<int> datasheetEnvelope(int shape, int steps) {
    const bool cont = shape & 8, att = shape & 4, alt = shape & 2, hold = shape & 1;
    std::vector<int> out;
    bool up = att;
    for (int cycle = 0; int(out.size()) < steps; ++cycle) {
        for (int i = 0; i < 32 && int(out.size()) < steps; ++i) {
            if (cycle == 0) {
                out.push_back(up ? i : 31 - i);
            } else if (!cont) {
                out.push_back(0);
            } else if (hold) {
                const int end = att ? 31 : 0;
                out.push_back(alt ? 31 - end : end);
            } else {
                out.push_back(up ? i : 31 - i);
            }
        }
        if (cont && !hold && alt) up = !up;
    }
    return out;
}
}  // namespace

TEST_CASE("S8 envelope shapes: 16 values of R13 × 4 cycles of 32 steps", "[S8][envelope]") {
    constexpr int kEp = 4;
    for (int shape = 0; shape < 16; ++shape) {
        Ym2149 chip;
        chip.writeRegister(7, 0x3f);  // tone and noise off: channel A follows the envelope
        chip.writeRegister(8, 0x10);
        chip.writeRegister(11, kEp);
        chip.writeRegister(13, uint8_t(shape));
        // After a restart, the first step lasts EP - 1 internal steps, the following ones EP (behaviour
        // of AtariAudio): step m is read at internal step EP × m + 1.
        std::vector<int> got;
        for (int t = 0; got.size() < 128; ++t) {
            chip.tick();
            if (t % kEp == 1) got.push_back(int(chip.levels() & 31));
        }
        INFO("R13 = " << shape);
        CHECK(got == datasheetEnvelope(shape, 128));
    }
}

TEST_CASE("S8 R13: shapes $0-$3 and $4-$7 identical to $9 and $F", "[S8][envelope]") {
    auto levels = [](int shape) {
        Ym2149 chip;
        chip.writeRegister(7, 0x3f);
        chip.writeRegister(8, 0x10);
        chip.writeRegister(11, 1);
        chip.writeRegister(13, uint8_t(shape));
        std::vector<int> v;
        for (int t = 0; t < 200; ++t) {
            chip.tick();
            v.push_back(int(chip.levels() & 31));
        }
        return v;
    };
    for (int s = 0; s < 4; ++s) CHECK(levels(s) == levels(9));
    for (int s = 4; s < 8; ++s) CHECK(levels(s) == levels(15));
}
