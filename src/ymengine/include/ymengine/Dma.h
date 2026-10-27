// STYM2149 — ymengine: STE DMA sound, sample bank
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The STE DMA sound reads from memory a stream of signed bytes (mono, or stereo interleaved left then right) at one
// of four fixed rates, then mixes it with the YM before the LMC1992 (bass, treble). A feature of STYM2149 of its own
// (a maxYMiser feature), active with the STE machine (GlobalParams::ste, Indus's table of
// 16/10/2026):
// - a bank of eight samples of 32 KB at most, each with its rate, mono or stereo, its root note and fine tune,
//   the part played (Start, End) and its loop;
// - a DMA sound type (Generator::Dma): each DMA track plays a sample of the bank, its voices mixed in software into
//   the stream like the STE module players. In 3-channel mode, eight DMA tracks follow those of the YM (tracks 5 to
//   12 in Authentic 3ch, 9 to 16 in Dual chip), one voice each; in 16-channel mode, any track can be DMA;
// - pitch by resampling the stream (read step = frequency of the note / frequency of the root note, nearest byte,
//   no interpolation), the stream staying at the rate of the sample.
#pragma once
#include <array>
#include <cstdint>

namespace stym::ymengine {

inline constexpr int kDmaSlots = 8;
inline constexpr int kDmaTracks = 8;             // DMA tracks in 3-channel mode (STE machine)
inline constexpr uint32_t kDmaMaxBytes = 32768;  // 32 KB per sample (16 KB per channel in stereo)
inline constexpr int kDmaRates = 4;
// STE rates (public STE documentation): **TO CONFIRM** on hardware (pitch test planned).
inline constexpr std::array<double, kDmaRates> kDmaRateHz = {6258, 12517, 25033, 50066};
inline constexpr double kDmaCentsMax = 100;  // fine tune of a slot, ± cents

// Settings of a slot of the bank (its bytes are given apart, Engine::setDmaSample).
struct DmaSlot {
    uint8_t rate = 2;      // index in kDmaRateHz (25 033 Hz by default)
    bool stereo = false;   // bytes interleaved left, right
    uint8_t root = 60;     // note played at the rate of the sample (C4)
    bool loop = false;     // the part looped while the note sounds; otherwise played once
    uint32_t start = 0, end = 0;  // part played, in frames (Start, End): ymengine::sampleSpan; end 0: to the end
    double cents = 0;  // fine tune, -100 to 100: the root note plays the sample this much higher
    bool operator==(const DmaSlot&) const = default;
};

// Settings of the eight slots (Engine::setDma).
struct DmaParams {
    std::array<DmaSlot, kDmaSlots> slots{};
    bool operator==(const DmaParams&) const = default;
};

}  // namespace stym::ymengine
