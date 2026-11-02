// STYM2149 — plugincore: named DMA banks
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A bank keeps the DMA samples of a project (settings, signed bytes and names) under a name, as the Digidrum banks keep
// the Digidrum slots: eight at least, up to the last one used (127 at most). The library is shared by
// all projects: a JSON file next to the preferences file:
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

// A named bank: the settings of the slots, their bytes and their names (as many bytes as names; the settings past
// them at their default).
struct DmaBank {
    std::string name;
    ymengine::DmaParams dma{};
    std::vector<SharedSamples<int8_t>> samples;
    std::vector<std::string> names;
    bool operator==(const DmaBank&) const = default;
};

// The DMA slots of the state, named: eight at least, up to the last one used.
DmaBank captureDmaBank(const std::string& name, const NonParamState& state);
// A bank into the state: every slot replaced, those past the bank's emptied.
void applyDmaBank(const DmaBank& bank, NonParamState& state);

// The library file (format above). Reading fails only when the text is not JSON or not a DMA bank library; a bank
// without a usable name is skipped.
std::optional<std::vector<DmaBank>> parseDmaBanks(std::string_view text, std::string* error = nullptr);
std::string dumpDmaBanks(const std::vector<DmaBank>& banks);

}  // namespace stym::plugin
