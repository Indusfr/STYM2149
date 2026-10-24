// STYM2149 — plugincore: named Digidrum banks (M6, B9, D-154)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A bank keeps the eight Digidrum slots (name, playback, conversion, 8-bit source and played 4-bit values) under a
// name. The library is shared by all projects, like the Patches one (Q8 of the M6 plan): a JSON file next to the
// preferences file:
//   {"kind": "digidrumlib", "version": 1, "banks": [{"name": ..., "slots": [{"name", "rate", "root", "cents",
//    "loop", "loopStart", "loopEnd", "convert" ("linear" or "ym"), "data" (one hexadecimal digit per 4-bit value),
//    "source" (signed bytes in base64)}, ...]}, ...]}
// Tolerant reading: missing keys at their default, values brought back into their ranges, unknown characters ignored.
#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plugincore/Digidrum.h"
#include "plugincore/State.h"

namespace stym::plugin {

// A named bank: the eight slots of the editor and the 4-bit values they play.
struct DigidrumBank {
    std::string name;
    std::array<DigidrumSlotState, ymengine::kDigidrumSlots> slots{};
    std::array<std::vector<uint8_t>, ymengine::kDigidrumSlots> nibbles{};
    bool operator==(const DigidrumBank&) const = default;
};

// The eight slots of the state, named.
DigidrumBank captureBank(const std::string& name, const NonParamState& state);
// A bank into the state: the eight slots replaced.
void applyBank(const DigidrumBank& bank, NonParamState& state);

// The library file (format above). Reading fails only when the text is not JSON or not a bank library; a bank
// without a usable name is skipped.
std::optional<std::vector<DigidrumBank>> parseDigidrumBanks(std::string_view text, std::string* error = nullptr);
std::string dumpDigidrumBanks(const std::vector<DigidrumBank>& banks);

// Base64 (RFC 4648, with padding); decoding ignores characters outside the alphabet.
std::string base64Encode(std::span<const uint8_t> bytes);
std::vector<uint8_t> base64Decode(std::string_view text);

}  // namespace stym::plugin
