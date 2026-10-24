// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// One real chip from register writes to host samples: writes timestamped in master cycles wait in a RegisterQueue,
// the Ym2149 runs at its internal step (clk/8), and the output chain brings its levels to the host rate (sinc
// resampler, or AtariAudio's averaging), removes the DC component or applies the board filters. Used by the engine in
// the 3-channel and dual-chip modes, by the YM Player and by the tools.
#pragma once
#include <cstdint>
#include <vector>

#include "ymcore/BoardFilters.h"
#include "ymcore/ChipConfig.h"
#include "ymcore/RegisterQueue.h"
#include "ymcore/SincResampler.h"
#include "ymcore/Ym2149.h"

namespace stym::ymcore {

// From the chip (clk/8) to the host rate (D-014). The board filters are in BoardConfig.
enum class OutputMode : uint8_t {
    Raw,     // chip output, resampled without aliasing (Kaiser sinc). Default.
    RawBox,  // original decimation of AtariAudio: average of the internal steps of each sample (bit for bit)
    Retro,   // last internal step of each sample: deliberate aliasing, "raw ST" colour
};

// Removal of the DC component (the chip output is unipolar), Raw chain only: the ST and STE chains have the
// high-pass of their output coupling (D-047).
enum class DcFilter : uint8_t {
    None,
    MovingAverage,  // moving average over 2 048 output samples, like AtariAudio dcAdjust (D-035)
};

// Everything prepare() needs. Changing a field means a new prepare() (allocation), except the STE bass and treble
// (setBoardTone).
struct RenderConfig {
    ChipConfig chip;
    uint32_t hostRate = 48000;  // Hz
    OutputMode mode = OutputMode::Raw;
    DcFilter dc = DcFilter::MovingAverage;
    BoardConfig board{};
    SincResampler::Design resampler{};
};

// One chip, its queue of timestamped writes and its output chain.
// prepare() allocates (outside the audio thread). reset(), write() and render() do not allocate, do not lock
// and make no system call (threshold S11).
//
// Timestamps: a write dated at master cycle c is applied just before the first internal step k such that
// 8k >= c (step k covers cycles [8k, 8k + 8)).
class ChipRenderer {
public:
    void prepare(const RenderConfig& config);
    void reset() noexcept;
    // Bass and treble of the STE chain (BoardConfig::bassDb, trebleDb), without allocation: the filters keep their
    // state.
    void setBoardTone(int bassDb, int trebleDb) noexcept;

    // Adds a timestamped write. If the queue is full, the oldest write is applied at once to make room
    // (counter forcedWrites()).
    void write(uint64_t masterCycle, uint8_t reg, uint8_t value) noexcept;

    // Produces n mono samples (1.0 = level 32 768 of the mixing table). `levels` (optional): 5-bit levels of the
    // three channels at the last internal step of each sample (Ym2149::levels), for the meters.
    void render(float* out, uint32_t n, uint16_t* levels = nullptr) noexcept;

    // Applies at once the writes due before the next internal step. No effect on the sound (they would be
    // applied just before that step); used to observe the chip state between two calls (S1).
    void flushDue() noexcept { applyDue(); }

    // Master cycle of the first internal step at the instant of sample s or after it.
    uint64_t cycleAtSample(uint64_t sample) const noexcept;

    // Delay of the sinc resampler (Raw mode), in host samples: output n carries the chip's sound at sample
    // n - latencySamples(). The other modes have none.
    uint32_t latencySamples() const noexcept { return config_.mode == OutputMode::Raw ? resampler_.latency() : 0; }
    uint64_t samplesRendered() const noexcept { return samples_; }
    uint64_t ticksRendered() const noexcept { return ticks_; }
    uint64_t forcedWrites() const noexcept { return forced_; }
    uint64_t reorderedWrites() const noexcept { return queue_.reordered(); }

    const RenderConfig& config() const noexcept { return config_; }
    Ym2149& chip() noexcept { return chip_; }
    const Ym2149& chip() const noexcept { return chip_; }
    const SincResampler& resampler() const noexcept { return resampler_; }

private:
    // Applies every queued write due at or before the next internal step (ticks_).
    void applyDue() noexcept {
        while (ticks_ >= nextDueTick_) {
            const RegisterWrite& w = queue_.front();
            chip_.writeRegister(w.reg, w.value);
            queue_.pop();
            nextDueTick_ = queue_.empty() ? UINT64_MAX : dueTick(queue_.front().cycle);
        }
    }
    // First internal step k with 8k >= cycle (D-037).
    static uint64_t dueTick(uint64_t cycle) noexcept { return (cycle + 7) >> 3; }
    // DC removal: integer version of the RawBox and Retro modes (AtariAudio's), floating-point version of Raw.
    int16_t dcAdjust(int16_t v) noexcept;
    float dcAdjustFloat(float v) noexcept;
    // Board filters at the host rate: coupling high-pass, then LMC1992 shelves (STE).
    double boardPost(double v) noexcept {
        v = highPass_.process(v);
        if (config_.board.chain == BoardChain::STE) v = treble_.process(bass_.process(v));
        return v;
    }

    RenderConfig config_{};
    Ym2149 chip_;
    RegisterQueue queue_;
    SincResampler resampler_;
    uint64_t nextDueTick_ = UINT64_MAX;  // due step of the oldest queued write; UINT64_MAX when the queue is empty
    uint64_t ticks_ = 0;                 // internal steps run since reset()
    uint64_t samples_ = 0;
    uint64_t forced_ = 0;
    // AtariAudio decimation: integer internal clock clk/8 and phase accumulator (+ host rate per internal step; a
    // sample is complete each time it reaches clk8_).
    uint32_t clk8_ = 250000;
    uint32_t inner_ = 0;
    // Moving average of the DC component (2 048 values): ring of the last outputs and their running sum, integer for
    // dcAdjust, fixed point for dcAdjustFloat.
    static constexpr uint32_t kDcBits = 11;
    std::vector<int16_t> dcBuf_;
    std::vector<int64_t> dcBufFixed_;
    uint32_t dcPos_ = 0;
    int32_t dcSum_ = 0;
    int64_t dcSumFixed_ = 0;
    // ST and STE chains (D-047): C10 at the internal step, the rest at the host rate.
    AsymmetricRc c10_;
    FirstOrder highPass_, bass_, treble_;
};

}  // namespace stym::ymcore
