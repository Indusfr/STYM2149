// STYM2149 — register write scenarios for the tests
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "Scenarios.h"

namespace stym::test {

std::vector<Scenario> m0Scenarios() {
    auto at0 = [](std::vector<std::pair<int, int>> regs) {
        std::vector<TimedWrite> w;
        for (auto [r, v] : regs) w.push_back({0, uint8_t(r), uint8_t(v)});
        return w;
    };
    return {
        {"tone3", at0({{0, 0x1C}, {1, 0x01}, {2, 0xD5}, {3, 0}, {4, 0x7B}, {5, 0x01}, {7, 0x38}, {8, 15}, {9, 12}, {10, 10}}), false},
        {"env_saw", at0({{7, 0x3F}, {8, 0x10}, {11, 0x90}, {12, 0x01}, {13, 0x08}}), false},
        {"sq10k", at0({{0, 12}, {1, 0}, {7, 0x3E}, {8, 15}}), false},
        {"hihat", at0({{6, 1}, {7, 0x37}, {8, 15}}), true},
        {"buzzer", at0({{7, 0x3F}, {8, 0x10}, {11, 30}, {12, 0}, {13, 0x0A}}), false},
        {"full", at0({{0, 0x1C}, {1, 0x01}, {2, 0xD5}, {3, 0}, {4, 0x8E}, {5, 0}, {6, 5}, {7, 0x30}, {8, 15}, {9, 0x10},
                      {10, 13}, {11, 0xE8}, {12, 0x03}, {13, 0x0E}}),
         true},
    };
}

Scenario randomScenario(uint32_t seed, uint64_t length, unsigned writesPer1000, bool allowNoise) {
    uint32_t s = seed * 2654435761u + 12345u;
    auto rnd = [&s]() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    };
    Scenario sc{"random" + std::to_string(seed), {}, allowNoise};
    uint64_t t = 0;
    const uint64_t meanGap = writesPer1000 ? 1000 / writesPer1000 : 1000;
    while (true) {
        t += rnd() % (2 * meanGap + 1);
        if (t >= length) break;
        const uint32_t pick = rnd() % 100;
        uint8_t reg, value = uint8_t(rnd());
        if (pick < 30) {
            reg = uint8_t(rnd() % 6);  // tone periods, small values frequent
            if ((reg & 1) && rnd() % 2) value = 0;
            if (!(reg & 1) && rnd() % 8 == 0) value = uint8_t(rnd() % 3);
        } else if (pick < 38) {
            reg = 6;
        } else if (pick < 48) {
            reg = 7;
        } else if (pick < 75) {
            reg = uint8_t(8 + rnd() % 3);
        } else if (pick < 88) {
            reg = uint8_t(11 + rnd() % 2);
            if (reg == 12 && rnd() % 2) value = uint8_t(rnd() % 4);
        } else if (pick < 98) {
            reg = 13;
        } else {
            reg = uint8_t(14 + rnd() % 2);
        }
        if (reg == 7 && !allowNoise) value |= 0x38;
        sc.writes.push_back({t, reg, value});
    }
    if (!allowNoise) sc.writes.insert(sc.writes.begin(), TimedWrite{0, 7, 0x38});
    return sc;
}

}  // namespace stym::test
