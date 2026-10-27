// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Settings of one emulated YM2149: master clock (used by ChipRenderer to place writes and samples in time; the chip
// itself only counts internal steps), noise feedback and seed of the power-on state. Plain values, copied by
// Ym2149::setConfig and ChipRenderer::prepare.
#pragma once
#include <cstdint>

namespace stym::ymcore {

// Feedback of the noise generator.
enum class NoiseLfsr : uint8_t {
    Maximal,     // bit 0 ⊕ bit 3, period 131 071; sequence of MAME (ay8910.cpp) and ym2149-rs. Default.
    AtariAudio,  // bit 0 ⊕ bit 2, period 114 681; reproduces AtariAudio 1.26 bit for bit (oracle).
};

// Master clock in Hz, as a fraction num / den (some Atari clocks are not whole numbers).
struct MasterClock {
    uint64_t num = 2'000'000;
    uint32_t den = 1;
    constexpr double hz() const { return double(num) / double(den); }
};

namespace clocks {
// 2 000 000 Hz: value of the YM files and of AtariAudio.
inline constexpr MasterClock kDefault{2'000'000, 1};
// Atari STF PAL: 8 021 247 Hz / 4 = 2 005 311.75 Hz. Source: Hatari (clocks_timings) and STYM2149_SPEC.md §3.
// TO CONFIRM.
inline constexpr MasterClock kAtariStfPal{8'021'247, 4};
// AY clocks of other machines, kept at Indus's request: they only change the chip frequency.
// AY-3-8910 of the ZX Spectrum 128: 1.7734 MHz (STYM2149_SPEC.md §3, compatibility).
inline constexpr MasterClock kZxSpectrum{1'773'400, 1};
// AY-3-8912 of the Amstrad CPC: 1 MHz (STYM2149_SPEC.md §3, compatibility).
inline constexpr MasterClock kAmstradCpc{1'000'000, 1};
}  // namespace clocks

// Configuration of one chip (see the file comment).
struct ChipConfig {
    MasterClock clock = clocks::kDefault;
    NoiseLfsr noise = NoiseLfsr::Maximal;
    // Seed of the draw of the tone edges at reset (MSVC rand() generator, as in AtariAudio).
    // The same seed always gives the same initial state. 1 = first draw of AtariAudio.
    uint32_t edgeSeed = 1;
};

}  // namespace stym::ymcore
