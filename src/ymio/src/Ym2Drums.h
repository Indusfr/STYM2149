// STYM2149 — ymio: digidrum bank of YM2 files (Mad Max's player)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <span>

namespace stym::ymio::detail {

inline constexpr unsigned kYm2DrumCount = 40;

// Sample n (0 to 39) of the YM2 bank: 4-bit volume values, played as is in R10.
std::span<const uint8_t> ym2Drum(unsigned n) noexcept;

// Conversion table from 8 bits → 4-bit volume of the Wings of Death player (YmRenderer::ConvertTo4Bits of
// AtariAudio 1.26), indexed by the 4 most significant bits of the sample.
extern const uint8_t kMadMax4Bits[16];

}  // namespace stym::ymio::detail
