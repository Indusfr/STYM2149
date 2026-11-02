// STYM2149 — plugincore: Digidrum sample from a decoded audio file, Digidrum editor
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Loading as in the prototype (#drumFile): linear resampling to 7800 Hz (the next sample counts as 0 after the end),
// 4-bit quantisation round(v × 7.5 + 7.5) clamped to 0-15, the whole file (the prototype kept 3 s, DRUM_LEN_MAX; the
// memory of the Atari limits the samples). The browser first decoded the file at the rate of its audio
// context; here, we start from the rate of the file. The prototype took the first channel of a stereo file; the Stereo
// menu of the Digidrums window chooses the left one, the right one or both mixed (Indus, 29/10/2026 01:41-01:43).
//
// Digidrum editor: each slot has a name, its playback (rate at the root note, fine tune, held loop:
// ymengine::DigidrumSlot), a 4-bit conversion mode and, since loading, a signed 8-bit source at the slot rate, from
// which the 4-bit values are recomputed (conversion changed, tools). Without a source (older state), the
// source is derived from the 4-bit values by the inverse linear conversion.
// - Linear: round(s / 127 × 7.5 + 7.5), the quantisation of the prototype;
// - YM levels: the 4-bit level whose chip output (measured mixing table, a single channel, level 2n + 1)
//   is closest to the source, placed linearly between levels 0 and 15: the shape of the sample passes
//   through unchanged despite the logarithmic law of the volumes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "plugincore/SharedSamples.h"
#include "ymengine/Params.h"

namespace stym::plugin {

inline constexpr size_t kDigidrumNameMax = 16;

// How the 4-bit values are computed from the 8-bit source (see above).
enum class DigiConvert : uint8_t { Linear, YmLevels };

// What the Digidrum editor keeps for one slot besides its 4-bit values, saved with the project.
struct DigidrumSlotState {
    std::string name;                 // 16 printable characters at most
    ymengine::DigidrumSlot play{};    // rate, root note, fine tune, loop
    DigiConvert convert = DigiConvert::Linear;
    SharedSamples<int8_t> source;     // signed 8-bit at the slot rate; empty: the 4-bit values only
    bool operator==(const DigidrumSlotState&) const = default;
};

// Channel of a stereo file that a slot loads, kept in the preferences under kDigiStereoPref as its name.
enum class DigiStereo : uint8_t { Left, Right, Mix };
inline constexpr std::string_view kDigiStereoPref = "digiStereo";
std::string_view digiStereoName(DigiStereo s) noexcept;  // "left", "right", "mix"
DigiStereo digiStereoOf(std::string_view name) noexcept;  // Mix for an unknown name
// The channel loaded: `left`, `right` (left when empty: a mono file) or their mean, as long as the shorter one.
std::vector<float> digidrumChannel(std::span<const float> left, std::span<const float> right, DigiStereo s);

// 4-bit values of the prototype (Linear) at rate `rateHz` (7800 Hz: loading as in the prototype).
std::vector<uint8_t> toDigidrum(std::span<const float> channel, double sampleRate, double rateHz = 7800);
// Signed 8-bit source at the same rate: round(v × 127) clamped to -128..127.
std::vector<int8_t> toDigidrumSource(std::span<const float> channel, double sampleRate, double rateHz = 7800);
// 4-bit value of a source byte, and those of a source.
uint8_t digidrumNibble(int8_t source, DigiConvert mode) noexcept;
std::vector<uint8_t> digidrumNibbles(std::span<const int8_t> source, DigiConvert mode);
// Source derived from 4-bit values (inverse linear conversion): round((n - 7.5) / 7.5 × 127).
std::vector<int8_t> digidrumSourceOf(std::span<const uint8_t> nibbles);
// Source of 4-bit values played as they are at the YM levels (a YM-Tracker digidrum): for each value, the byte
// nearest to the output of its level that the YmLevels conversion brings back to it.
std::vector<int8_t> digidrumSourceOfLevels(std::span<const uint8_t> nibbles);
// Playback settings of a slot for the engine: `play` with the value of silence of its conversion (rest).
// Without allocation: the driver calls it on the audio thread.
ymengine::DigidrumSlot digidrumEngineSlot(const DigidrumSlotState& s) noexcept;

// Editor tools, on the source: Normalize (peak at ±127), Reverse, Fade out (linear over the whole sample).
void digiNormalize(std::vector<int8_t>& s) noexcept;
void digiReverse(std::vector<int8_t>& s) noexcept;
void digiFadeOut(std::vector<int8_t>& s) noexcept;

// Slot name: printable characters, 16 at most, edge spaces removed.
std::string cleanDigidrumName(std::string_view name);

}  // namespace stym::plugin
