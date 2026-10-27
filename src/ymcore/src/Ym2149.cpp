// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Ported from AtariAudio 1.26 (ym2149c.cpp: Reset, WriteReg, MuteVoices), © 2026 Arnaud Carré, MIT licence.
#include "ymcore/Ym2149.h"

namespace stym::ymcore {

namespace {
// Wired bits of each register (AtariAudio regMask; YM2149 datasheet p. 4). R14/R15 = I/O ports.
constexpr uint8_t kRegMask[Ym2149::kRegisterCount] = {0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0x3f,
                                                      0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff};
// R7 (3 bits per source) → 3 × 5-bit mask. A set bit disables the source: its mask is then all ones (constant high).
constexpr uint32_t kMixerMasks[8] = {0x0000, 0x001f, 0x03e0, 0x03ff, 0x7c00, 0x7c1f, 0x7fe0, 0x7fff};
// R13 → envelope shape: $0-$3 = \___, $4-$7 = /___, then $8-$F one shape each.
constexpr uint8_t kShapeToEnv[16] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9};

// rand() of the MSVC C library, used by AtariAudio for the initial state of the edges.
uint32_t msvcRand(uint32_t& seed) noexcept {
    seed = seed * 214013u + 2531011u;
    return (seed >> 16) & 0x7fffu;
}
}  // namespace

Ym2149::Ym2149(const ChipConfig& config) noexcept : config_(config) { reset(); }

void Ym2149::reset() noexcept {
    for (int v = 0; v < 3; ++v) {
        toneCounter_[v] = 0;
        tonePeriod_[v] = 0;
    }
    // The internal edges of the chip are unpredictable at power-on (AtariAudio);
    // they are drawn from a fixed seed to stay deterministic.
    // Bits 0, 5 and 10 of one draw give the edges of voices A, B and C, each spread to its 5-bit field.
    uint32_t seed = config_.edgeSeed;
    toneEdges_ = (msvcRand(seed) & ((1u << 10) | (1u << 5) | 1u)) * 0x1fu;
    toneMask_ = noiseMask_ = 0;
    noiseOutput_ = 0;
    noiseCounter_ = noisePeriod_ = noiseHalf_ = 0;
    lfsr_ = 1;
    envCounter_ = envPeriod_ = envBase_ = 0;
    envPos_ = 0;
    enableMask_ = 0x7fff;
    levels_ = 0;
    // Through writeRegister so that the derived state (periods, masks, envelope shape) follows; R7 = $3F turns every
    // tone and noise off.
    for (unsigned r = 0; r < kRegisterCount; ++r) {
        regs_[r] = 0;
        writeRegister(r, r == 7 ? 0x3f : 0);
    }
    // AtariAudio resets the envelope position to 0 after writing R13 (hold phase of shape 0).
    envPos_ = 0;
}

void Ym2149::writeRegister(unsigned reg, uint8_t value) noexcept {
    if (reg >= kRegisterCount) {
        if (reg == kToneEdgeReset && value < 3) {
            toneEdges_ ^= 0x1fu << (value * 5);
            toneCounter_[value] = 0;
        }
        return;
    }
    regs_[reg] = value & kRegMask[reg];
    switch (reg) {
    case 0: case 1: case 2: case 3: case 4: case 5: {
        const unsigned v = reg >> 1;
        tonePeriod_[v] = (uint32_t(regs_[v * 2 + 1]) << 8) | regs_[v * 2];
        break;
    }
    case 6:
        noisePeriod_ = regs_[6];
        break;
    case 7:
        toneMask_ = kMixerMasks[value & 7];
        noiseMask_ = kMixerMasks[(value >> 3) & 7];
        break;
    case 11: case 12:
        envPeriod_ = (uint32_t(regs_[12]) << 8) | regs_[11];
        break;
    case 13:
        // Restart: first step of the shape's first phase (see tick()).
        envBase_ = uint32_t(kShapeToEnv[regs_[13]]) * 128u;
        envPos_ = -64;
        envCounter_ = 0;
        break;
    default:
        break;  // R8-R10 are read directly by tick(); R14/R15 have no effect on the sound.
    }
}

void Ym2149::setVoiceMute(uint32_t muteMask) noexcept {
    enableMask_ = 0;
    for (int v = 0; v < 3; ++v)
        if (!(muteMask & (1u << v))) enableMask_ |= 0x1fu << (v * 5);
}

Ym2149::State Ym2149::state() const noexcept {
    State s{};
    for (int v = 0; v < 3; ++v) {
        s.toneCounter[v] = toneCounter_[v];
        s.tonePeriod[v] = tonePeriod_[v];
    }
    s.toneEdges = toneEdges_;
    s.toneMask = toneMask_;
    s.noiseMask = noiseMask_;
    s.noiseOutput = noiseOutput_;
    s.noiseCounter = noiseCounter_;
    s.noisePeriod = noisePeriod_;
    s.noiseHalf = noiseHalf_;
    s.lfsr = lfsr_;
    s.envCounter = envCounter_;
    s.envPeriod = envPeriod_;
    s.envShape = envBase_ / 128u;
    s.envPos = envPos_;
    for (unsigned r = 0; r < kRegisterCount; ++r) s.regs[r] = regs_[r];
    return s;
}

}  // namespace stym::ymcore
