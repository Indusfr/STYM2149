// STYM2149 — plugincore: memory of the Atari for the samples
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/SteMemory.h"

#include <algorithm>

#include "plugincore/State.h"

namespace stym::plugin {

uint32_t steSampleBudget(uint8_t ram) noexcept {
    const uint32_t bytes = kSteRamKb[std::min<size_t>(ram, kSteRamKb.size() - 1)] * 1024u;
    return bytes - kSteReserve;
}

uint64_t digidrumBytes(const NonParamState& s, int k) noexcept {
    return k >= 0 && k < ymengine::kDigidrumSlotsMax ? s.digidrums[size_t(k)].size() : 0;
}

uint64_t dmaBytes(const NonParamState& s, int k) noexcept {
    return k >= 0 && k < ymengine::kDmaSlotsMax ? s.dmaSamples[size_t(k)].size() : 0;
}

SteMemory steMemory(const NonParamState& s, ymengine::VoiceMode mode, bool ste) noexcept {
    SteMemory m;
    m.limited = !ymengine::studioMode(mode);
    // 2x3ch plays two Ataris: each brings its memory (Indus, 30/10/2026 02:49).
    m.budget = steSampleBudget(s.steRam) * (mode == ymengine::VoiceMode::Dual3 ? 2u : 1u);
    bool full = false;  // a sample did not fit: those after it are silent too
    auto count = [&](uint64_t bytes, auto& muted, int k) {
        if (!bytes) return;
        m.total += bytes;
        if (!full && (!m.limited || m.used + bytes <= m.budget)) m.used += bytes;
        else {
            full = true;
            muted.set(size_t(k));
        }
    };
    for (int k = 0; k < ymengine::kDigidrumSlotsMax; ++k) count(digidrumBytes(s, k), m.digiMuted, k);
    if (ste)
        for (int k = 0; k < ymengine::kDmaSlotsMax; ++k) count(dmaBytes(s, k), m.dmaMuted, k);
    return m;
}

}  // namespace stym::plugin
