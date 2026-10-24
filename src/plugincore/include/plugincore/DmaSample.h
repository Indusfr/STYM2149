// STYM2149 — plugincore: sample of the STE DMA track (M6, B8, D-153) from a file, Sign and Gain tools
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Loading like the Digidrums (toDigidrum): linear resampling to the slot rate (the next sample counts as 0 after the
// end), then signed bytes round(v × 127) clamped to -128..127, 32 KB at most (16 KB per channel in stereo, bytes
// interleaved left then right). Mono: first channel of the file, like the Digidrums; stereo: the first two (a single
// channel: copied). Raw file (.raw, .spl, .sam): bytes taken as they are.
// Destructive tools (X15: maxYMiser): Sign flips the most significant bit of each byte (signed ↔ unsigned), Gain
// multiplies by a gain in dB and clips.
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace stym::plugin {

// Sample at `targetRate`, stereo when `right` is not empty (the caller passes the same channel twice to make stereo
// from a mono file).
std::vector<int8_t> toDmaSample(std::span<const float> left, std::span<const float> right, double sampleRate,
                                double targetRate);
// Raw file, cut to 32 KB; in stereo, to an even count so that each frame has both of its bytes.
std::vector<int8_t> dmaRaw(std::span<const uint8_t> bytes, bool stereo);
// The Sign and Gain tools (see above), in place.
void dmaSign(std::vector<int8_t>& bytes) noexcept;
void dmaGain(std::vector<int8_t>& bytes, double db) noexcept;

}  // namespace stym::plugin
