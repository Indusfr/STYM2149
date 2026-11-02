// STYM2149 — plugincore: memory of the Atari for the samples
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// What a real Atari can hold (Indus, 29/10/2026 15:53-16:06): the samples of a project, as many and as long as wanted,
// share the memory of the machine chosen for it (512 KB, 1, 2 or 4 MB; 4 MB by default), Digidrums (ST and STE) and
// DMA samples (STE) alike. The limit holds in the 3-channel modes (Authentic 3ch and 2x3ch), those a real Atari plays;
// 2x3ch, two Ataris, has twice the memory chosen (Indus, 30/10/2026 02:49), chosen in Settings > SOUND ENGINE
// for both modes; the 16- and 32-channel modes have none. Over the limit, the samples that do not fit, starting from the last one, are silent
// (and crossed out in the windows) until the memory is enough again, and the exports leave them out.
//
// Count (Claude's choices): one byte per Digidrum value (the 4-bit values as the replay routines read them),
// the bytes of a DMA sample (two per stereo frame), each sample whole whatever its Start and End; the DMA samples only
// on the STE, the ST not playing them; kSteReserve kept for the system, the screen and the replay routine. The order is
// the Digidrums, then the DMA samples, each by its number: the samples kept are the longest run from the first that
// fits.
#pragma once
#include <array>
#include <bitset>
#include <cstdint>

#include "ymengine/Dma.h"
#include "ymengine/Params.h"

namespace stym::plugin {

struct NonParamState;

// Memory of the machine, in KB: the index is NonParamState::steRam.
inline constexpr std::array<uint32_t, 4> kSteRamKb = {512, 1024, 2048, 4096};
inline constexpr uint8_t kSteRamDefault = 3;  // 4 MB
inline constexpr uint32_t kSteReserve = 128 * 1024;  // bytes for the system, the screen and the replay routine
// The longest sample a file loads into a slot (64 MB once converted, Claude's choice): no Atari holds one, and a long
// file loaded in 32-channel mode (no limit) cannot exhaust the memory of the computer.
inline constexpr uint64_t kSampleLoadMax = uint64_t(64) << 20;

// Bytes the samples can take with RAM index `ram`.
uint32_t steSampleBudget(uint8_t ram) noexcept;

struct SteMemory {
    bool limited = false;   // 3-channel mode: the limit applies
    uint32_t budget = 0;    // bytes for the samples (RAM − reserve, twice that in 2x3ch)
    uint64_t used = 0;      // bytes of the samples counted that fit (heard)
    uint64_t total = 0;     // bytes of every sample counted, those that do not fit included
    std::bitset<ymengine::kDigidrumSlotsMax> digiMuted;  // Digidrum k does not fit: silent
    std::bitset<ymengine::kDmaSlotsMax> dmaMuted;        // DMA sample k does not fit: silent
    bool over() const noexcept { return limited && total > budget; }
};

// The memory of `s` in voice mode `mode` on machine STE (`ste`) or ST. Without allocation: the driver calls it on the
// audio thread.
SteMemory steMemory(const NonParamState& s, ymengine::VoiceMode mode, bool ste) noexcept;
// Bytes that sample k takes (one per Digidrum value; those of a DMA sample).
uint64_t digidrumBytes(const NonParamState& s, int k) noexcept;
uint64_t dmaBytes(const NonParamState& s, int k) noexcept;

}  // namespace stym::plugin
