// STYM2149 — ymcore: output chain of an arbitrary stream at the internal step (clk/8)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Same chain as the floating-point path of ChipRenderer (Raw, RawBox, Retro modes; DC component; board
// filters), but for a signal supplied by the caller at each internal step instead of a chip: used for the left
// and right buses of the 16-channel mode (ymengine), where the virtual channels are mixed in floating point.
#pragma once
#include <cstdint>
#include <vector>

#include "ymcore/BoardFilters.h"
#include "ymcore/ChipRenderer.h"
#include "ymcore/SincResampler.h"

namespace stym::ymcore {

// One bus of the engine (left, right, a separate track output or a delay send): the engine fills an array with one
// value per internal step and gets host samples back in the same unit (no scaling here, unlike ChipRenderer).
class TickStreamOutput {
public:
    // Allocates (outside the audio thread). Uses config.chip.clock, hostRate, mode, dc, board and resampler.
    void prepare(const RenderConfig& config);
    void reset() noexcept;
    // Starts at the position of `other` (prepared with the same configuration) as if it had been fed silence until
    // then, without allocation: the stream of a bus that begins to sound in the middle of a render (send of the delay).
    void joinSilent(const TickStreamOutput& other) noexcept;
    // Bass and treble of the STE chain, without allocation (like ChipRenderer::setBoardTone).
    void setBoardTone(int bassDb, int trebleDb) noexcept;

    // Total number of internal steps that must have been supplied to produce the next `samples` samples.
    uint64_t ticksNeeded(uint32_t samples) const noexcept;
    uint64_t ticksConsumed() const noexcept { return ticks_; }

    // Produces n samples. ticks[i] is the value of step firstTick + i; firstTick == ticksConsumed() is required
    // and the array must cover at least ticksNeeded(n) - firstTick steps. Does not allocate.
    void render(const float* ticks, uint64_t firstTick, float* out, uint32_t n) noexcept;

    uint32_t latencySamples() const noexcept { return config_.mode == OutputMode::Raw ? resampler_.latency() : 0; }
    const RenderConfig& config() const noexcept { return config_; }

private:
    float dcAdjust(float v) noexcept;
    double boardPost(double v) noexcept {
        v = highPass_.process(v);
        if (config_.board.chain == BoardChain::STE) v = treble_.process(bass_.process(v));
        return v;
    }

    RenderConfig config_{};
    SincResampler resampler_;
    uint64_t ticks_ = 0;      // internal steps consumed since reset()
    uint32_t clk8_ = 250000;  // integer internal clock of the RawBox and Retro modes (like ChipRenderer)
    uint32_t inner_ = 0;      // phase accumulator of those modes
    // Moving average over 2 048 samples, in fixed point with 32 fraction bits (exact sum).
    static constexpr uint32_t kDcBits = 11;
    std::vector<int64_t> dcBuf_;
    uint32_t dcPos_ = 0;
    int64_t dcSum_ = 0;
    AsymmetricRc c10_;
    FirstOrder highPass_, bass_, treble_;
};

}  // namespace stym::ymcore
