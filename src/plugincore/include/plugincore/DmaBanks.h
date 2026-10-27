// STYM2149 — plugincore: named DMA banks
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A bank keeps the eight DMA samples (settings, signed bytes and names) under a name, as the Digidrum banks keep the
// eight Digidrum slots. The library is shared by all projects: a JSON file next to the preferences file:
//   {"kind": "dmalib", "version": 1, "banks": [{"name": ..., "slots": [{"name", "rate" (Hz, one of kDmaRateHz),
//    "stereo", "root", "cents", "loop", "start", "end", "data" (the signed bytes in base64)}, ...]}, ...]}
// Tolerant reading: missing keys at their default, a rate brought to the nearest of the four, values brought back
// into their ranges.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plugincore/State.h"
#include "ymengine/Dma.h"

namespace stym::plugin {

// A named bank: the settings of the eight slots, their bytes and their names.
struct DmaBank {
    std::string name;
    ymengine::DmaParams dma{};
    std::array<std::vector<int8_t>, ymengine::kDmaSlots> samples{};
    std::array<std::string, ymengine::kDmaSlots> names{};
    bool operator==(const DmaBank&) const = default;
};

// The eight DMA slots of the state, named.
DmaBank captureDmaBank(const std::string& name, const NonParamState& state);
// A bank into the state: the eight slots replaced.
void applyDmaBank(const DmaBank& bank, NonParamState& state);

// The library file (format above). Reading fails only when the text is not JSON or not a DMA bank library; a bank
// without a usable name is skipped.
std::optional<std::vector<DmaBank>> parseDmaBanks(std::string_view text, std::string* error = nullptr);
std::string dumpDmaBanks(const std::vector<DmaBank>& banks);

}  // namespace stym::plugin
