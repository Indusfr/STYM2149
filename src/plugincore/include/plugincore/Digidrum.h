// STYM2149 — plugincore: Digidrum sample from a decoded audio file, Digidrum editor (M6, B9, D-154)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Loading as in the prototype (#drumFile): first channel only, linear resampling to 7800 Hz (the next sample counts
// as 0 after the end), 4-bit quantisation round(v × 7.5 + 7.5) clamped to 0-15, 3 s at most (DRUM_LEN_MAX). The
// browser first decoded the file at the rate of its audio context; here, we start from the rate of the file.
//
// Digidrum editor (D-154): each slot has a name, its playback (rate at the root note, fine tune, held loop:
// ymengine::DigidrumSlot), a 4-bit conversion mode and, since loading, a signed 8-bit source at the slot rate, from
// which the 4-bit values are recomputed (conversion changed, tools). Without a source (state from before M6), the
// source is derived from the 4-bit values by the inverse linear conversion.
// - Linear: round(s / 127 × 7.5 + 7.5), the quantisation of the prototype;
// - YM levels: the 4-bit level whose chip output (measured mixing table, a single channel, level 2n + 1, D-011,
//   D-013) is closest to the source, placed linearly between levels 0 and 15: the shape of the sample passes
//   through unchanged despite the logarithmic law of the volumes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ymengine/Params.h"

namespace stym::plugin {

// 3 s at the loading rate of the prototype (DRUM_LEN_MAX).
inline constexpr size_t kDigidrumMaxSamples = 3 * 7800;
inline constexpr size_t kDigidrumNameMax = 16;

// How the 4-bit values are computed from the 8-bit source (see above).
enum class DigiConvert : uint8_t { Linear, YmLevels };

// What the Digidrum editor keeps for one slot besides its 4-bit values, saved with the project.
struct DigidrumSlotState {
    std::string name;                 // 16 printable characters at most
    ymengine::DigidrumSlot play{};    // rate, root note, fine tune, loop
    DigiConvert convert = DigiConvert::Linear;
    std::vector<int8_t> source;       // signed 8-bit at the slot rate; empty: the 4-bit values only
    bool operator==(const DigidrumSlotState&) const = default;
};

// Maximum samples for a rate: 3 s (DRUM_LEN_MAX), 65535 at most.
size_t digidrumMaxSamples(double rateHz) noexcept;

// 4-bit values of the prototype (Linear) at rate `rateHz` (7800 Hz: loading as in the prototype).
std::vector<uint8_t> toDigidrum(std::span<const float> channel, double sampleRate, double rateHz = 7800);
// Signed 8-bit source at the same rate: round(v × 127) clamped to -128..127.
std::vector<int8_t> toDigidrumSource(std::span<const float> channel, double sampleRate, double rateHz = 7800);
// 4-bit value of a source byte, and those of a source.
uint8_t digidrumNibble(int8_t source, DigiConvert mode) noexcept;
std::vector<uint8_t> digidrumNibbles(std::span<const int8_t> source, DigiConvert mode);
// Source derived from 4-bit values (inverse linear conversion): round((n - 7.5) / 7.5 × 127).
std::vector<int8_t> digidrumSourceOf(std::span<const uint8_t> nibbles);
// Playback settings of a slot for the engine: `play` with the value of silence of its conversion (rest, D-455).
// Without allocation: the driver calls it on the audio thread.
ymengine::DigidrumSlot digidrumEngineSlot(const DigidrumSlotState& s) noexcept;

// Editor tools, on the source: Normalize (peak at ±127), Reverse, Fade out (linear over the whole sample).
void digiNormalize(std::vector<int8_t>& s) noexcept;
void digiReverse(std::vector<int8_t>& s) noexcept;
void digiFadeOut(std::vector<int8_t>& s) noexcept;

// Slot name: printable characters, 16 at most, edge spaces removed.
std::string cleanDigidrumName(std::string_view name);

}  // namespace stym::plugin
