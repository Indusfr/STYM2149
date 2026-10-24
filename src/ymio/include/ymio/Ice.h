// STYM2149 — ymio: Ice 2.4 decompression (packed SNDH files, "ICE!" mark)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Bounded rewrite of Hans Wessels' depacker ("placed in public domain 2007", ice_24.cpp of AtariAudio 1.26, kept
// for the test oracle): same bit reading, from the end of the data towards the start, but any read outside the data
// or copy outside the output gives a refusal instead of an out-of-bounds memory access.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace stym::ymio {

// Ice header: "ICE!", packed size (header included) and original size, big-endian.
bool isIce(std::span<const uint8_t> data) noexcept;

// Original data; empty if the header, the sizes or the stream are invalid, or if the original size exceeds maxSize.
std::optional<std::vector<uint8_t>> iceDepack(std::span<const uint8_t> data, size_t maxSize);

}  // namespace stym::ymio
