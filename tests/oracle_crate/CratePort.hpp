// STYM2149 — C++ port of the ym2149-rs 0.9.1 core (secondary oracle S3, tests only)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Line-by-line port of crates/ym2149-core (generators.rs, mixer.rs, chip.rs, dc_filter.rs, tables.rs), © slippyex,
// MIT licence; original algorithm AtariAudio, © Arnaud Carré, MIT licence. Taken from the initial trial.
// It reproduces the crate, deviations included: it is not ymcore.
#pragma once
#include <cstdint>
#include <cstring>

#include "ymcore/Ym2149.h"

namespace stym::test {

namespace crate_tables {
// Envelope shapes: identical in the crate and in AtariAudio.
inline const uint8_t (&ENV_DATA)[10 * 128] = stym::ymcore::tables::kEnvData;
static constexpr uint8_t SHAPE_TO_ENV[16] = {0,0,0,0,1,1,1,1,2,3,4,5,6,7,8,9};
static constexpr uint32_t MASKS[8] = {0x0000,0x001f,0x03e0,0x03ff,0x7c00,0x7c1f,0x7fe0,0x7fff};
static constexpr uint8_t REG_MASK[14] = {0xff,0x0f,0xff,0x0f,0xff,0x0f,0x1f,0x3f,0x1f,0x1f,0x1f,0xff,0xff,0x0f};
static constexpr uint32_t LOG_LEVELS[32] = {
    152/3, 181/3, 215/3, 255/3, 304/3, 362/3, 430/3, 511/3,
    608/3, 724/3, 861/3, 1023/3, 1217/3, 1448/3, 1722/3, 2047/3,
    2435/3, 2896/3, 3444/3, 4095/3, 4870/3, 5792/3, 6888/3, 8191/3,
    9741/3, 11584/3, 13776/3, 16383/3, 19483/3, 23169/3, 27553/3, 32767/3};
}  // namespace crate_tables

class CrateYm2149 {
public:
    CrateYm2149(uint32_t masterClock, uint32_t sampleRate) : internalClock_(masterClock / 8), sampleRate_(sampleRate) { reset(); }

    void reset() {
        uint32_t seed = 1;
        seed = seed * 214013u + 2531011u;
        const uint32_t rnd = (seed >> 16) & 0x7fff;
        const uint32_t edges = (rnd & ((1u << 10) | (1u << 5) | 1u)) * 0x1f;
        for (int i = 0; i < 3; ++i) { toneCounter_[i] = 0; tonePeriod_[i] = 0; toneEdge_[i] = edges & (0x1fu << (i * 5)); }
        noiseCounter_ = 0; noisePeriod_ = 0; lfsr_ = 1; noiseMask_ = 0; noiseHalf_ = false;
        envCounter_ = 0; envPeriod_ = 0; envPos_ = 0; envOffset_ = 0;
        toneMaskCfg_ = 0; noiseMaskCfg_ = 0;
        std::memset(dcBuf_, 0, sizeof dcBuf_); dcPos_ = 0; dcSum_ = 0;
        std::memset(regs_, 0, sizeof regs_);
        write(7, 0x3f);
        cycleAcc_ = 0;
    }

    void write(int reg, uint8_t value) {
        using namespace crate_tables;
        if (reg < 0 || reg >= 14) return;
        value &= REG_MASK[reg];
        regs_[reg] = value;
        switch (reg) {
        case 0: case 1: case 2: case 3: case 4: case 5: {
            const int ch = reg / 2;
            tonePeriod_[ch] = (uint32_t(regs_[ch * 2 + 1]) << 8) | regs_[ch * 2];
            break;
        }
        case 6: noisePeriod_ = value; break;
        case 7: toneMaskCfg_ = MASKS[value & 7]; noiseMaskCfg_ = MASKS[(value >> 3) & 7]; break;
        case 11: case 12: envPeriod_ = (uint32_t(regs_[12]) << 8) | regs_[11]; break;
        case 13: envOffset_ = SHAPE_TO_ENV[value & 0x0f] * 128u; envPos_ = -64; envCounter_ = 0; break;
        default: break;
        }
    }

    int16_t nextSample() {
        using namespace crate_tables;
        uint16_t acc = 0;
        for (;;) {
            acc |= uint16_t(tick());
            cycleAcc_ += sampleRate_;
            if (cycleAcc_ >= internalClock_) break;
        }
        cycleAcc_ -= internalClock_;
        const uint32_t env = ENV_DATA[envOffset_ + uint32_t(envPos_ + 64)];
        uint32_t levels = 0;
        for (int i = 0; i < 3; ++i) {
            const uint8_t v = regs_[8 + i];
            const uint32_t l = (v & 0x10) ? env : (uint32_t(v) << 1);
            levels |= l << (i * 5);
        }
        const uint32_t gated = levels & acc;
        uint32_t total = 0;
        for (int i = 0; i < 3; ++i) {
            uint32_t o = LOG_LEVELS[(gated >> (i * 5)) & 0x1f];
            if (tonePeriod_[i] <= 1) o >>= 1;
            total += o;
        }
        return dc(uint16_t(total));
    }

private:
    uint32_t tick() {
        using namespace crate_tables;
        uint32_t edges = 0;
        for (int i = 0; i < 3; ++i) {
            if (++toneCounter_[i] >= tonePeriod_[i]) { toneEdge_[i] ^= 0x1fu << (i * 5); toneCounter_[i] = 0; }
            edges |= toneEdge_[i];
        }
        noiseHalf_ = !noiseHalf_;
        if (noiseHalf_) {
            ++noiseCounter_;
            const uint32_t eff = noisePeriod_ ? noisePeriod_ : 1;
            if (noiseCounter_ >= eff) {
                const uint32_t lsb = lfsr_ & 1;
                lfsr_ >>= 1;
                if (lsb) lfsr_ ^= 0x12000;
                noiseMask_ = lsb ? ~0u : 0u;
                noiseCounter_ = 0;
            }
        }
        if (++envCounter_ >= envPeriod_) {
            ++envPos_;
            if (envPos_ > 0) envPos_ &= 63;
            envCounter_ = 0;
        }
        return (edges | toneMaskCfg_) & (noiseMask_ | noiseMaskCfg_);
    }

    int16_t dc(uint16_t s) {
        dcSum_ -= dcBuf_[dcPos_];
        dcSum_ += s;
        dcBuf_[dcPos_] = s;
        dcPos_ = (dcPos_ + 1) & 2047;
        return int16_t(int32_t(s) - int32_t(dcSum_ >> 11));
    }

    uint32_t internalClock_, sampleRate_, cycleAcc_ = 0;
    uint8_t regs_[14];
    uint32_t toneCounter_[3], tonePeriod_[3], toneEdge_[3];
    uint32_t noiseCounter_, noisePeriod_, lfsr_, noiseMask_; bool noiseHalf_;
    uint32_t envCounter_, envPeriod_, envOffset_; int32_t envPos_;
    uint32_t toneMaskCfg_, noiseMaskCfg_;
    uint16_t dcBuf_[2048]; uint32_t dcPos_, dcSum_;
};

}  // namespace stym::test
