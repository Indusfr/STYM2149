// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Model of the YM2149F chip at clk/8 (250 kHz for 2 MHz), ported from AtariAudio 1.26 (ym2149c.cpp),
// © 2026 Arnaud Carré (Leonard/Oxygene), MIT licence (third_party/AtariAudio/LICENSE).
// Deliberate deviations from AtariAudio: maximal noise LFSR, state reset to zero,
// registers R14/R15 kept for read-back.
#pragma once
#include <cstdint>
#include "ymcore/ChipConfig.h"

namespace stym::ymcore {

namespace tables {
// Envelope shapes: 10 shapes × 128 5-bit levels (phases 0-1 played once, 2-3 looped).
extern const uint8_t (&kEnvData)[10 * 128];
// Measured 32×32×32 mixing table (index = A | B << 5 | C << 10), values 0..32767.
extern const uint16_t (&kMixTable)[32 * 32 * 32];
}  // namespace tables

// One chip: the 16 registers and the tone, noise and envelope generators, advanced one internal step (8 master
// cycles) at a time by tick(), which returns the mixed DAC level. It knows nothing of time or sample rates: its owner
// (normally a ChipRenderer) decides when to write and when to step. No allocation, no lock: usable on the audio
// thread. The 16-channel mode does not use it: ymengine::VirtualChannel reproduces one channel of it.
class Ym2149 {
public:
    static constexpr unsigned kRegisterCount = 16;
    // Command outside the registers, passed through writeRegister (and so through the dated queue of ChipRenderer):
    // value = voice (0-2). Inverts the tone edge of the voice and resets its counter, like AtariAudio 1.26 on the
    // way out of a timer interrupt when the voice period was written there as 0 or 1 ("square-sync",
    // Ym2149c::InsideTimerIrq). Used for SNDH playback (ymio::SndhCapture); nothing else sends it.
    static constexpr unsigned kToneEdgeReset = 0x80;

    // Snapshot of the internal state (S1 tests, display).
    struct State {
        uint32_t toneCounter[3];
        uint32_t tonePeriod[3];
        uint32_t toneEdges;      // 3 × 5 bits: 0x1f when the square output of the voice is high
        uint32_t toneMask;       // voices whose tone is off (R7 inverted: off = constant high level)
        uint32_t noiseMask;      // same for the noise
        uint32_t noiseOutput;    // 0 or ~0
        uint32_t noiseCounter;
        uint32_t noisePeriod;    // R6 (5 bits)
        uint32_t noiseHalf;      // toggles at each step: the noise counter only advances on every other step
        uint32_t lfsr;           // 17-bit shift register of the noise
        uint32_t envCounter;
        uint32_t envPeriod;      // R12:R11
        uint32_t envShape;       // 0..9 (index in kEnvData)
        int32_t envPos;          // -64..63: -64..-1 = phases 0-1 (once after an R13 write), 0..63 = phases 2-3 (loop)
        uint8_t regs[kRegisterCount];
    };

    explicit Ym2149(const ChipConfig& config = {}) noexcept;

    // The new configuration applies at the next reset() (the noise feedback, read by tick(), from the next step).
    void setConfig(const ChipConfig& config) noexcept { config_ = config; }
    const ChipConfig& config() const noexcept { return config_; }

    // Power-on state: all zero, R7 = $3F, tone edges drawn from the seed.
    void reset() noexcept;

    // Writes a register (0-15). Unwired bits are masked as on the chip.
    // R13 always restarts the envelope, even with the same value. kToneEdgeReset: see above; any other
    // number is ignored.
    void writeRegister(unsigned reg, uint8_t value) noexcept;
    uint8_t readRegister(unsigned reg) const noexcept { return reg < kRegisterCount ? regs_[reg] : 0; }

    // One internal step (clk/8). Returns the mixed output, 0..32767.
    inline int tick() noexcept;

    // 5-bit levels A | B << 5 | C << 10 of the last step, after the mixer and mutes.
    uint16_t levels() const noexcept { return levels_; }

    // Artificial mute per voice: bit v = voice v muted (like AtariAudio MuteVoices).
    void setVoiceMute(uint32_t muteMask) noexcept;

    State state() const noexcept;

private:
    // Same meaning as the fields of State; levels are kept as 5-bit fields per voice (A bits 0-4, B 5-9, C 10-14) so
    // that tick() mixes the three voices with a few masks.
    ChipConfig config_;
    uint32_t toneCounter_[3] = {};
    uint32_t tonePeriod_[3] = {};
    uint32_t toneEdges_ = 0;
    uint32_t toneMask_ = 0;
    uint32_t noiseMask_ = 0;
    uint32_t noiseOutput_ = 0;
    uint32_t noiseCounter_ = 0;
    uint32_t noisePeriod_ = 0;
    uint32_t noiseHalf_ = 0;
    uint32_t lfsr_ = 1;
    uint32_t envCounter_ = 0;
    uint32_t envPeriod_ = 0;
    uint32_t envBase_ = 0;  // envShape × 128
    int32_t envPos_ = 0;
    uint32_t enableMask_ = 0x7fff;  // voices not muted by setVoiceMute
    uint16_t levels_ = 0;
    uint8_t regs_[kRegisterCount] = {};
};

inline int Ym2149::tick() noexcept {
    // Mask of the three channels, computed on the state before the step (like AtariAudio). A channel passes its
    // level when (tone high or tone off in R7) and (noise high or noise off in R7): the chip's mixer.
    const uint32_t vmask = (toneEdges_ | toneMask_) & (noiseOutput_ | noiseMask_);

    for (int v = 0; v < 3; ++v) {
        if (++toneCounter_[v] >= tonePeriod_[v]) {  // period 0 behaves like 1
            toneEdges_ ^= 0x1fu << (v * 5);
            toneCounter_[v] = 0;
        }
    }

    // One envelope step every EP internal steps: clk / (8·EP), i.e. clk / (256·EP) per cycle of 32 steps.
    // envPos_ climbs from -64 (R13 write) to 0 once, then wraps within 0..63: the last two phases of the shape loop.
    if (++envCounter_ >= envPeriod_) {
        ++envPos_;
        if (envPos_ > 0) envPos_ &= 63;
        envCounter_ = 0;
    }

    // The noise advances at half speed: clk / (16·NP).
    noiseHalf_ ^= 1u;
    if (noiseHalf_ && ++noiseCounter_ >= noisePeriod_) {
        if (config_.noise == NoiseLfsr::Maximal) {
            // MAME ay8910.cpp: input = bit 0 ⊕ bit 3, output = bit 0.
            lfsr_ = (lfsr_ >> 1) | (((lfsr_ ^ (lfsr_ >> 3)) & 1u) << 16);
            noiseOutput_ = (lfsr_ & 1u) ? ~0u : 0u;
        } else {
            // AtariAudio 1.26: output = bit 0 ⊕ bit 2, fed back into bit 16.
            noiseOutput_ = ((lfsr_ ^ (lfsr_ >> 2)) & 1u) ? ~0u : 0u;
            lfsr_ = (lfsr_ >> 1) | ((noiseOutput_ & 1u) << 16);
        }
        noiseCounter_ = 0;
    }

    const uint32_t env = tables::kEnvData[envBase_ + uint32_t(envPos_ + 64)];
    // Fixed volume v → level 2v + 1; bit 4 = envelope.
    uint32_t lv = ((regs_[8] & 0x10) ? env : ((uint32_t(regs_[8]) << 1) | 1u))
                | (((regs_[9] & 0x10) ? env : ((uint32_t(regs_[9]) << 1) | 1u)) << 5)
                | (((regs_[10] & 0x10) ? env : ((uint32_t(regs_[10]) << 1) | 1u)) << 10);
    lv &= vmask & enableMask_;
    // The three 5-bit levels form the 15-bit index of the measured mixing table (the channels do not add linearly).
    levels_ = uint16_t(lv);
    return tables::kMixTable[lv];
}

}  // namespace stym::ymcore
