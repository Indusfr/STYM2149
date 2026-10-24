// STYM2149 — common tools of the AtariAudio oracle (tests only)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "OracleCommon.h"

namespace stym::test {

uint32_t takeAtariAudioSeed() {
    static uint32_t seed = 1;
    const uint32_t current = seed;
    seed = seed * 214013u + 2531011u;
    return current;
}

std::string compareChipState(const ymcore::Ym2149& ours, const Ym2149c& aa, const uint8_t* envBase, bool compareNoise) {
    const ymcore::Ym2149::State s = ours.state();
    auto diff = [](const char* what, long long a, long long b) {
        return std::string(what) + ": ymcore " + std::to_string(a) + ", AtariAudio " + std::to_string(b);
    };
    for (int v = 0; v < 3; ++v) {
        if (s.toneCounter[v] != aa.m_toneCounter[v]) return diff("tone counter", s.toneCounter[v], aa.m_toneCounter[v]);
        if (s.tonePeriod[v] != aa.m_tonePeriod[v]) return diff("tone period", s.tonePeriod[v], aa.m_tonePeriod[v]);
    }
    if (s.toneEdges != aa.m_toneEdges) return diff("edges", s.toneEdges, aa.m_toneEdges);
    if (s.toneMask != aa.m_toneMask) return diff("tone mask", s.toneMask, aa.m_toneMask);
    if (s.noiseMask != aa.m_noiseMask) return diff("noise mask", s.noiseMask, aa.m_noiseMask);
    if (s.envCounter != aa.m_envCounter) return diff("envelope counter", s.envCounter, aa.m_envCounter);
    if (s.envPeriod != aa.m_envPeriod) return diff("envelope period", s.envPeriod, aa.m_envPeriod);
    if (s.envPos != aa.m_envPos) return diff("envelope position", s.envPos, aa.m_envPos);
    const long long shape = (aa.m_pCurrentEnv - envBase) / 128;
    if (s.envShape != shape) return diff("envelope shape", s.envShape, shape);
    if (s.noiseCounter != aa.m_noiseCounter) return diff("noise counter", s.noiseCounter, aa.m_noiseCounter);
    if (s.noisePeriod != aa.m_noisePeriod) return diff("noise period", s.noisePeriod, aa.m_noisePeriod);
    if (s.noiseHalf != aa.m_noiseHalf) return diff("noise half-rate", s.noiseHalf, aa.m_noiseHalf);
    if (compareNoise) {
        if (s.lfsr != aa.m_noiseRndRack) return diff("LFSR", s.lfsr, aa.m_noiseRndRack);
        if (s.noiseOutput != aa.m_currentNoiseMask) return diff("noise output", s.noiseOutput, aa.m_currentNoiseMask);
    }
    for (int r = 0; r < 14; ++r)
        if (s.regs[r] != aa.m_regs[r]) return diff(("R" + std::to_string(r)).c_str(), s.regs[r], aa.m_regs[r]);
    if (compareNoise && ours.levels() != aa.m_currentVisualLevels) return diff("levels", ours.levels(), aa.m_currentVisualLevels);
    return {};
}

}  // namespace stym::test
