// M0 trial: "tick" core at clk/8 derived from AtariAudio 1.26 (ym2149c.cpp),
// (c) Arnaud Carré (Leonard/Oxygene), MIT licence. Mixing table measured by
// Paulo Simoes, interpolated 32x32x32 by A. Carré (ym2149_tables.h, MIT).
// Exposes tick() so that an alias-free resampler can be plugged in.
#pragma once
#include <cstdint>
#include <cstring>

namespace m0 {
namespace aa_tables {
#include "ym2149_tables.h"
}

class YmTick {
public:
    static uint32_t& seed() { static uint32_t s = 1; return s; }
    static uint16_t rnd() { seed() = seed() * 214013u + 2531011u; return uint16_t((seed() >> 16) & 0x7fff); }

    void reset(uint32_t hostRate, uint32_t ymClock) {
        std::memset(this, 0, sizeof *this);
        toneEdges_ = (rnd() & ((1u << 10) | (1u << 5) | 1u)) * 0x1f;
        hostRate_ = hostRate; clk8_ = ymClock / 8; rack_ = 1; enable_ = 0x7fff;
        for (int r = 0; r < 14; ++r) write(r, r == 7 ? 0x3f : 0);
        envPos_ = 0;
    }
    void write(int reg, uint8_t v) {
        static const uint8_t mask[14] = {0xff,0x0f,0xff,0x0f,0xff,0x0f,0x1f,0x3f,0x1f,0x1f,0x1f,0xff,0xff,0x0f};
        static const uint32_t masks[8] = {0x0000,0x001f,0x03e0,0x03ff,0x7c00,0x7c1f,0x7fe0,0x7fff};
        static const uint8_t shapeToEnv[16] = {0,0,0,0,1,1,1,1,2,3,4,5,6,7,8,9};
        if (unsigned(reg) >= 14) return;
        regs_[reg] = v & mask[reg];
        switch (reg) {
        case 0: case 1: case 2: case 3: case 4: case 5: { int c = reg >> 1; period_[c] = (regs_[c*2+1] << 8) | regs_[c*2]; break; }
        case 6: noisePeriod_ = regs_[6]; break;
        case 7: toneMask_ = masks[v & 7]; noiseMask_ = masks[(v >> 3) & 7]; break;
        case 11: case 12: envPeriod_ = (regs_[12] << 8) | regs_[11]; break;
        case 13: env_ = shapeToEnv[regs_[13]] * 128; envPos_ = -64; envCounter_ = 0; break;
        default: break;
        }
    }
    // One step at 250 kHz (clk/8). Returns the measured mixed output (0..32767).
    inline int tick() {
        const uint32_t vmask = (toneEdges_ | toneMask_) & (curNoise_ | noiseMask_);
        for (int v = 0; v < 3; ++v)
            if (++counter_[v] >= period_[v]) { toneEdges_ ^= 0x1fu << (v * 5); counter_[v] = 0; }
        if (++envCounter_ >= envPeriod_) { ++envPos_; if (envPos_ > 0) envPos_ &= 63; envCounter_ = 0; }
        noiseHalf_ ^= 1;
        if (noiseHalf_ && ++noiseCounter_ >= noisePeriod_) {
            curNoise_ = ((rack_ ^ (rack_ >> 2)) & 1) ? ~0u : 0u;
            rack_ = (rack_ >> 1) | ((curNoise_ & 1) << 16);
            noiseCounter_ = 0;
        }
        const uint32_t e = aa_tables::s_envData[env_ + envPos_ + 64];
        uint32_t lv = ((regs_[8] & 0x10) ? e : ((regs_[8] << 1) | 1u))
                    | (((regs_[9] & 0x10) ? e : ((regs_[9] << 1) | 1u)) << 5)
                    | (((regs_[10] & 0x10) ? e : ((regs_[10] << 1) | 1u)) << 10);
        lv &= vmask & enable_;
        return aa_tables::s_ym2149RecordedMixTable[lv];
    }
    // Original AtariAudio decimation (box average + DC adjustment): used to check the port.
    int16_t nextSampleBox() {
        int acc = 0, n = 0;
        do { acc += tick(); ++n; inner_ += hostRate_; } while (inner_ < clk8_);
        inner_ -= clk8_;
        const int16_t v = int16_t(acc / n);
        dcSum_ -= dcBuf_[dcPos_]; dcSum_ += v; dcBuf_[dcPos_] = v; dcPos_ = (dcPos_ + 1) & 2047;
        return int16_t(int32_t(v) - (dcSum_ >> 11));
    }

private:
    uint8_t regs_[14];
    uint32_t period_[3], counter_[3], toneEdges_, toneMask_, noiseMask_, curNoise_;
    uint32_t envCounter_, envPeriod_, env_; int envPos_;
    uint32_t noisePeriod_, noiseCounter_, noiseHalf_, rack_, enable_;
    uint32_t hostRate_, clk8_, inner_;
    int16_t dcBuf_[2048]; int32_t dcPos_, dcSum_;
};
}  // namespace m0
