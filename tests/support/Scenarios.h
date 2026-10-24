// STYM2149 — register write scenarios for the tests
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stym::test {

// Write at time "at": an internal step (S1) or a host sample (S2) depending on the test.
struct TimedWrite {
    uint64_t at;
    uint8_t reg;
    uint8_t value;
};

struct Scenario {
    std::string name;
    std::vector<TimedWrite> writes;  // sorted by "at"
    bool usesNoise;
};

// The six scenarios of the M0 trial (writes at time 0).
std::vector<Scenario> m0Scenarios();

// Deterministic pseudo-random script: writes to all registers over [0, length), about writesPerUnit writes per
// 1 000 units. Without noise if allowNoise is false (R7 bits 3-5 forced to 1).
Scenario randomScenario(uint32_t seed, uint64_t length, unsigned writesPer1000, bool allowNoise);

}  // namespace stym::test
