// STYM2149 — ymengine: virtual YM channel of the 16-channel mode
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// One channel of the YM2149 with its own noise and its own envelope, computed by events: instead of evaluating
// every internal step (clk/8), it jumps straight to the next step where its output can change. The result is
// identical, step by step, to channel A of ymcore::Ym2149 when channels B and C are muted (test [channel]).
#pragma once
#include <algorithm>
#include <cstdint>

#include "ymcore/ChipConfig.h"
#include "ymcore/Ym2149.h"

namespace stym::ymengine {

// One per voice of the engine (Engine::Impl::vch), driven on the audio thread: the voice writes its registers, then
// run() emits the level changes that the engine puts on its buses. No allocation.
class VirtualChannel {
public:
    // Reset (state of ymcore::Ym2149::reset for one channel). edgeHigh: initial edge of the tone.
    void reset(ymcore::NoiseLfsr noise, bool edgeHigh) noexcept;

    // Equivalents of the register writes, applied before the next step.
    void setTonePeriod(uint16_t period) noexcept { tonePeriod_ = period & 0x0fffu; }        // R0/R1
    void setNoisePeriod(uint8_t period) noexcept { noisePeriod_ = period & 0x1fu; }         // R6
    void setMixer(bool toneOn, bool noiseOn) noexcept;                                      // R7 (bits 0 and 3)
    void setVolume(uint8_t value) noexcept { volume_ = value & 0x1fu; }                     // R8 (bit 4: envelope)
    void setEnvPeriod(uint16_t period) noexcept { envPeriod_ = period; }                     // R11/R12
    void writeShape(uint8_t shape) noexcept;                                                // R13: restart
    // Puts the tone back at the start of a high half-period (attack of a voice of the prototype: tonePhase = 0,
    // toneBit = 1). No equivalent on the chip.
    void restartTone() noexcept {
        toneCounter_ = 0;
        edge_ = true;
    }

    // Advances by `ticks` internal steps. emit(offset, index) is called at each step (0 ≤ offset < ticks) whose
    // output index (5-bit level after the mixer, 0..31) differs from that of the previous step.
    template <class Emit>
    void run(uint64_t ticks, Emit&& emit) noexcept;

    // Index produced by the last step (0xff before the first one).
    uint8_t lastIndex() const noexcept { return last_; }
    // Forces the emission of the next index (the channel resumes after a silence decided by the engine).
    void invalidate() noexcept { last_ = 0xff; }

    // State, in the format of ymcore::Ym2149::State for channel A (tests).
    bool toneEdge() const noexcept { return edge_; }
    uint32_t toneCounter() const noexcept { return toneCounter_; }
    uint32_t lfsr() noexcept {
        flushNoise();
        return lfsr_;
    }
    uint32_t envCounter() const noexcept { return envCounter_; }
    int32_t envPos() const noexcept { return envPos_; }

private:
    bool toneAudible() const noexcept { return !toneMasked_; }
    bool noiseAudible() const noexcept { return !noiseMasked_; }
    bool envAudible() const noexcept { return (volume_ & 0x10u) != 0; }
    // Hot steps of the render (called at each output change): defined further down, in the header, so that run
    // inlines them.
    uint8_t output() const noexcept;
    uint8_t tickOnce() noexcept;
    void skip(uint64_t c) noexcept;
    // run and skip for a fixed combination of audible tone, noise and envelope (it does not change during
    // run): an audible counter has at most one event in a skip, the last step of the skip.
    template <bool kTone, bool kNoise, bool kEnv, class Emit>
    void runAs(uint64_t ticks, Emit& emit) noexcept;
    template <bool kTone, bool kNoise, bool kEnv>
    void skipAs(uint64_t c) noexcept;
    void shiftNoise() noexcept;
    void flushNoise() noexcept;
    // Envelope position after `steps` steps: −64..0 once, then 0..63 looped (rule of Ym2149::tick).
    static int32_t envAdvance(int32_t pos, uint64_t steps) noexcept {
        if (steps == 0) return pos;
        const int64_t q = int64_t(pos) + int64_t(steps);
        return q <= 0 ? int32_t(q) : int32_t(q & 63);
    }
    // Steps before the next toggle / envelope step / noise shift (1 = at the next step).
    uint64_t toneDue() const noexcept { return toneCounter_ + 1 >= tonePeriod_ ? 1 : tonePeriod_ - toneCounter_; }
    uint64_t envDue() const noexcept { return envCounter_ + 1 >= envPeriod_ ? 1 : envPeriod_ - envCounter_; }
    uint64_t noiseDue() const noexcept;

    ymcore::NoiseLfsr noise_ = ymcore::NoiseLfsr::Maximal;
    uint32_t tonePeriod_ = 0, toneCounter_ = 0;
    bool edge_ = true;  // tone output (square), high or low
    bool toneMasked_ = true, noiseMasked_ = true;
    // noiseHalf_: parity of the step; the noise counter only advances on every second step.
    uint32_t noisePeriod_ = 0, noiseCounter_ = 0, noiseHalf_ = 0;
    bool noiseOut_ = false;
    uint32_t lfsr_ = 1;
    uint64_t pendingShifts_ = 0;  // noise shifts pending while it is off
    // envBase_: first of the 128 entries of the shape in ymcore::tables::kEnvData, read at envPos_ + 64; envPos_
    // runs from −64 to −1 once (first ramp), then from 0 to 63 in a loop (envAdvance).
    uint32_t envPeriod_ = 0, envCounter_ = 0, envBase_ = 0;
    int32_t envPos_ = 0;
    uint8_t volume_ = 0;  // R8 value: 4-bit volume, bit 4 = envelope
    uint8_t last_ = 0xff;
};

// Mixer rule of the chip: the channel sounds when (tone high or tone off) and (noise high or noise off); its level is
// then that of the envelope or 2 × volume + 1, otherwise 0.
inline uint8_t VirtualChannel::output() const noexcept {
    if (!((edge_ || toneMasked_) && (noiseOut_ || noiseMasked_))) return 0;
    return (volume_ & 0x10u) ? ymcore::tables::kEnvData[envBase_ + uint32_t(envPos_ + 64)] : uint8_t((volume_ << 1) | 1u);
}

// One shift of the 17-bit noise register (ymcore::NoiseLfsr: taps 0 and 3, or 0 and 2 for AtariAudio's sequence).
inline void VirtualChannel::shiftNoise() noexcept {
    if (noise_ == ymcore::NoiseLfsr::Maximal) {
        lfsr_ = (lfsr_ >> 1) | (((lfsr_ ^ (lfsr_ >> 3)) & 1u) << 16);
        noiseOut_ = (lfsr_ & 1u) != 0;
    } else {
        const uint32_t out = (lfsr_ ^ (lfsr_ >> 2)) & 1u;
        noiseOut_ = out != 0;
        lfsr_ = (lfsr_ >> 1) | (out << 16);
    }
}

inline uint64_t VirtualChannel::noiseDue() const noexcept {
    // The counter advances at the step where noiseHalf goes to 1: steps 1, 3, 5… if noiseHalf is 0, otherwise 2, 4, 6…
    const uint64_t s = noiseCounter_ + 1 >= noisePeriod_ ? 1 : noisePeriod_ - noiseCounter_;
    return noiseHalf_ ? 2 * s : 2 * s - 1;
}

// One internal step, in the order of Ym2149::tick: the tone and noise gates are read before the counters advance, the
// envelope level after its step.
inline uint8_t VirtualChannel::tickOnce() noexcept {
    const bool t = edge_ || toneMasked_, n = noiseOut_ || noiseMasked_;
    if (++toneCounter_ >= tonePeriod_) {
        edge_ = !edge_;
        toneCounter_ = 0;
    }
    if (++envCounter_ >= envPeriod_) {
        envPos_ = envAdvance(envPos_, 1);
        envCounter_ = 0;
    }
    noiseHalf_ ^= 1u;
    if (noiseHalf_ && ++noiseCounter_ >= noisePeriod_) {
        if (noiseMasked_) ++pendingShifts_;
        else shiftNoise();
        noiseCounter_ = 0;
    }
    if (!(t && n)) return 0;
    return (volume_ & 0x10u) ? ymcore::tables::kEnvData[envBase_ + uint32_t(envPos_ + 64)] : uint8_t((volume_ << 1) | 1u);
}

inline void VirtualChannel::skip(uint64_t c) noexcept {
    // Number of events of a counter of period p (at least 1) whose first falls at step `first`, over c steps;
    // `rest` receives the value of the counter after the last one. No division in the usual case (a single event).
    auto events = [](uint64_t span, uint64_t first, uint64_t p, uint32_t& rest) noexcept -> uint64_t {
        const uint64_t over = span - first;
        if (over < p) {
            rest = uint32_t(over);
            return 1;
        }
        rest = uint32_t(over % p);
        return 1 + over / p;
    };
    // Tone: toggles every max(period, 1) steps once the first one is reached.
    {
        const uint64_t first = toneDue();
        if (c < first) {
            toneCounter_ += uint32_t(c);
        } else {
            const uint64_t toggles = events(c, first, tonePeriod_ ? tonePeriod_ : 1, toneCounter_);
            if (toggles & 1) edge_ = !edge_;
        }
    }
    // Envelope: same counter rule, position advanced by as many steps.
    {
        const uint64_t first = envDue();
        if (c < first) {
            envCounter_ += uint32_t(c);
        } else {
            const uint64_t steps = events(c, first, envPeriod_ ? envPeriod_ : 1, envCounter_);
            envPos_ = envAdvance(envPos_, steps);
        }
    }
    // Noise: one chance to advance every two steps.
    {
        const uint64_t chances = noiseHalf_ ? c / 2 : (c + 1) / 2;
        noiseHalf_ ^= uint32_t(c & 1);
        const uint64_t first = noiseCounter_ + 1 >= noisePeriod_ ? 1 : noisePeriod_ - noiseCounter_;
        if (chances < first) {
            noiseCounter_ += uint32_t(chances);
        } else {
            const uint64_t shifts = events(chances, first, noisePeriod_ ? noisePeriod_ : 1, noiseCounter_);
            if (noiseMasked_) {
                pendingShifts_ += shifts;
            } else {
                for (uint64_t i = 0; i < shifts; ++i) shiftNoise();
            }
        }
    }
}

template <bool kTone, bool kNoise, bool kEnv>
inline void VirtualChannel::skipAs(uint64_t c) noexcept {
    // An inaudible counter keeps the general rule of skip, which can count several events.
    // Audible tone: c ≤ toneDue(), one toggle at most (at the last step).
    if constexpr (kTone) {
        if (c < toneDue()) {
            toneCounter_ += uint32_t(c);
        } else {
            edge_ = !edge_;
            toneCounter_ = 0;
        }
    } else {
        const uint64_t first = toneDue();
        if (c < first) {
            toneCounter_ += uint32_t(c);
        } else {
            const uint64_t p = tonePeriod_ ? tonePeriod_ : 1, over = c - first;
            if (over < p) {
                toneCounter_ = uint32_t(over);
                edge_ = !edge_;
            } else {
                toneCounter_ = uint32_t(over % p);
                if ((1 + over / p) & 1) edge_ = !edge_;
            }
        }
    }
    // Audible envelope: c ≤ envDue() − 1, no envelope step in the skip.
    if constexpr (kEnv) {
        envCounter_ += uint32_t(c);
    } else {
        const uint64_t first = envDue();
        if (c < first) {
            envCounter_ += uint32_t(c);
        } else {
            const uint64_t p = envPeriod_ ? envPeriod_ : 1, over = c - first;
            uint64_t steps = 1;
            if (over < p) {
                envCounter_ = uint32_t(over);
            } else {
                envCounter_ = uint32_t(over % p);
                steps = 1 + over / p;
            }
            envPos_ = envAdvance(envPos_, steps);
        }
    }
    // Audible noise: c ≤ noiseDue(), so at most one shift (chances ≤ first).
    const uint64_t chances = noiseHalf_ ? c / 2 : (c + 1) / 2;
    noiseHalf_ ^= uint32_t(c & 1);
    const uint64_t first = noiseCounter_ + 1 >= noisePeriod_ ? 1 : noisePeriod_ - noiseCounter_;
    if (chances < first) {
        noiseCounter_ += uint32_t(chances);
    } else if constexpr (kNoise) {
        shiftNoise();
        noiseCounter_ = 0;
    } else {
        const uint64_t p = noisePeriod_ ? noisePeriod_ : 1, over = chances - first;
        uint64_t shifts = 1;
        if (over < p) {
            noiseCounter_ = uint32_t(over);
        } else {
            noiseCounter_ = uint32_t(over % p);
            shifts = 1 + over / p;
        }
        if (noiseMasked_) {
            pendingShifts_ += shifts;
        } else {
            for (uint64_t i = 0; i < shifts; ++i) shiftNoise();
        }
    }
}

// Hot loop: everything it calls (exact step, skip, emission) is inlined in it with GCC and Clang.
#if defined(__GNUC__) || defined(__clang__)
#define STYM_VCH_FLATTEN __attribute__((flatten))
#else
#define STYM_VCH_FLATTEN
#endif

template <bool kTone, bool kNoise, bool kEnv, class Emit>
STYM_VCH_FLATTEN void VirtualChannel::runAs(uint64_t ticks, Emit& emit) noexcept {
    uint64_t at = 0;
    while (at < ticks) {
        // First step where the output can change: the step that follows an audible tone toggle or noise shift, or
        // the very step of an audible envelope step.
        uint64_t c = ticks - at;
        if constexpr (kTone) c = std::min(c, toneDue());
        if constexpr (kNoise) c = std::min(c, noiseDue());
        if constexpr (kEnv) {
            c = std::min(c, envDue() - 1);
            if (c == 0) {
                // Audible envelope step right away: one exact step.
                const uint8_t o = tickOnce();
                if (o != last_) {
                    last_ = o;
                    emit(at, o);
                }
                ++at;
                continue;
            }
        }
        // Constant output over [at, at + c): that of the current state.
        uint8_t o = 0;
        if ((!kTone || edge_) && (!kNoise || noiseOut_))
            o = kEnv ? ymcore::tables::kEnvData[envBase_ + uint32_t(envPos_ + 64)] : uint8_t((volume_ << 1) | 1u);
        if (o != last_) {
            last_ = o;
            emit(at, o);
        }
        skipAs<kTone, kNoise, kEnv>(c);
        at += c;
    }
}

// Picks the loop compiled for the counters that are audible now (bit 2 tone, bit 1 noise, bit 0 envelope).
template <class Emit>
void VirtualChannel::run(uint64_t ticks, Emit&& emit) noexcept {
    switch ((toneAudible() ? 4 : 0) | (noiseAudible() ? 2 : 0) | (envAudible() ? 1 : 0)) {
    case 0: runAs<false, false, false>(ticks, emit); break;
    case 1: runAs<false, false, true>(ticks, emit); break;
    case 2: runAs<false, true, false>(ticks, emit); break;
    case 3: runAs<false, true, true>(ticks, emit); break;
    case 4: runAs<true, false, false>(ticks, emit); break;
    case 5: runAs<true, false, true>(ticks, emit); break;
    case 6: runAs<true, true, false>(ticks, emit); break;
    default: runAs<true, true, true>(ticks, emit); break;
    }
}

#undef STYM_VCH_FLATTEN

}  // namespace stym::ymengine
