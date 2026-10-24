// STYM2149 — common tools of the AtariAudio oracle (tests only)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <string>

#include "AtariAudioAccess.h"
#include "ymcore/Ym2149.h"

namespace stym::test {

// AtariAudio draws the initial state of the edges with a global rand() consumed once per Reset. This mirror follows
// the same sequence: call it once just before each AtariAudio Reset; the returned value is the seed of the compared
// chip.
uint32_t takeAtariAudioSeed();

// Internal step value produced by the last ComputeNextSample of a Ym2149c set to clk/8 (one step per sample): it is
// the last value entered into the dcAdjust buffer.
inline int atariAudioLastTick(const Ym2149c& aa) {
    return aa.m_dcAdjustBuffer[uint32_t(aa.m_dcAdjustPos - 1) & ((1u << Ym2149c::kDcAdjustHistoryBit) - 1)];
}

// Compares the internal state of ymcore and AtariAudio. envBase = m_pCurrentEnv just after Reset (shape 0).
// Returns a description of the first deviation, or an empty string.
std::string compareChipState(const ymcore::Ym2149& ours, const Ym2149c& aa, const uint8_t* envBase, bool compareNoise);

}  // namespace stym::test
