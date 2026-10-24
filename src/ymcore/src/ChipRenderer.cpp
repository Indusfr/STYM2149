// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The RawBox and Retro modes and the DC removal follow Ym2149c::ComputeNextSample and Ym2149c::dcAdjust of
// AtariAudio 1.26 (© 2026 Arnaud Carré, MIT licence).
#include "ymcore/ChipRenderer.h"

#include <algorithm>
#include <cmath>

namespace stym::ymcore {

namespace {
// ceil(s × num / den) without overflow as long as den × num fits in 64 bits.
uint64_t mulDivCeil(uint64_t s, uint64_t num, uint64_t den) noexcept {
    const uint64_t q = s / den, r = s % den;
    return q * num + (r * num + den - 1) / den;
}
}  // namespace

void ChipRenderer::prepare(const RenderConfig& config) {
    config_ = config;
    chip_.setConfig(config.chip);
    const MasterClock clk = config.chip.clock;
    // AtariAudio works with an integer clock: clk/8 rounded down (2 000 000 → 250 000).
    clk8_ = uint32_t((clk.num / clk.den) / 8);
    // The sinc resampler takes the exact internal step rate, num / (den × 8) Hz, as its input rate.
    if (config.mode == OutputMode::Raw) resampler_.prepare(clk.num, uint64_t(clk.den) * 8, config.hostRate, config.resampler);
    dcBuf_.assign(size_t(1) << kDcBits, 0);
    dcBufFixed_.assign(size_t(1) << kDcBits, 0);
    // C10 filters each internal step; the other board filters run on the host-rate output.
    const BoardConfig& b = config.board;
    c10_.prepare(clk.hz() / 8, b.pullUpOhm, b.pullDownOhm, b.c10Farad);
    highPass_ = FirstOrder::highPass(b.highPassHz, config.hostRate);
    bass_ = FirstOrder::lowShelf(lmc1992Step(b.bassDb), b.bassHz, config.hostRate);
    treble_ = FirstOrder::highShelf(lmc1992Step(b.trebleDb), b.trebleHz, config.hostRate);
    reset();
}

void ChipRenderer::setBoardTone(int bassDb, int trebleDb) noexcept {
    BoardConfig& b = config_.board;
    b.bassDb = bassDb;
    b.trebleDb = trebleDb;
    bass_.retune(FirstOrder::lowShelf(lmc1992Step(bassDb), b.bassHz, config_.hostRate));
    treble_.retune(FirstOrder::highShelf(lmc1992Step(trebleDb), b.trebleHz, config_.hostRate));
}

void ChipRenderer::reset() noexcept {
    chip_.reset();
    queue_.clear();
    nextDueTick_ = UINT64_MAX;
    ticks_ = samples_ = forced_ = 0;
    inner_ = 0;
    std::fill(dcBuf_.begin(), dcBuf_.end(), int16_t(0));
    std::fill(dcBufFixed_.begin(), dcBufFixed_.end(), int64_t(0));
    dcPos_ = 0;
    dcSum_ = 0;
    dcSumFixed_ = 0;
    c10_.reset();
    highPass_.reset();
    bass_.reset();
    treble_.reset();
    if (config_.mode == OutputMode::Raw) resampler_.reset();
}

void ChipRenderer::write(uint64_t masterCycle, uint8_t reg, uint8_t value) noexcept {
    if (queue_.full()) {
        const RegisterWrite& w = queue_.front();
        chip_.writeRegister(w.reg, w.value);
        queue_.pop();
        ++forced_;
    }
    queue_.push(masterCycle, reg, value);
    nextDueTick_ = dueTick(queue_.front().cycle);
}

uint64_t ChipRenderer::cycleAtSample(uint64_t sample) const noexcept {
    const MasterClock clk = config_.chip.clock;
    // Raw: the exact instant s / host rate, rounded up to the next internal step: 8 × ceil(s × clk / (8 × host)).
    if (config_.mode == OutputMode::Raw) return 8 * mulDivCeil(sample, clk.num, uint64_t(clk.den) * 8 * config_.hostRate);
    // RawBox / Retro: sample s starts at step ceil(s × clk8 / host rate) (AtariAudio loop, valid as long as the
    // host rate does not exceed clk/8).
    return 8 * mulDivCeil(sample, clk8_, config_.hostRate);
}

// Subtracts the mean of the last 2 048 outputs (this one included) from the output; the sum is kept up to date by
// removing the value that leaves the ring and adding the one that enters it (D-035).
int16_t ChipRenderer::dcAdjust(int16_t v) noexcept {
    dcSum_ -= dcBuf_[dcPos_];
    dcSum_ += v;
    dcBuf_[dcPos_] = v;
    dcPos_ = (dcPos_ + 1) & ((1u << kDcBits) - 1);
    return int16_t(int32_t(v) - (dcSum_ >> kDcBits));
}

float ChipRenderer::dcAdjustFloat(float v) noexcept {
    // Exact sum in fixed point (16 fraction bits): no drift over long durations.
    const int64_t fixed = int64_t(std::floor(double(v) * 65536.0 + 0.5));
    dcSumFixed_ -= dcBufFixed_[dcPos_];
    dcSumFixed_ += fixed;
    dcBufFixed_[dcPos_] = fixed;
    dcPos_ = (dcPos_ + 1) & ((1u << kDcBits) - 1);
    return float(double(v) - double(dcSumFixed_) / double(int64_t(1) << (kDcBits + 16)));
}

void ChipRenderer::render(float* out, uint32_t n, uint16_t* levels) noexcept {
    constexpr float kScale = 1.0f / 32768.0f;
    const bool dc = config_.dc == DcFilter::MovingAverage;
    const bool board = config_.board.chain != BoardChain::Raw;
    const bool c10 = config_.board.chain == BoardChain::ST;
    switch (config_.mode) {
    case OutputMode::Raw:
        // Runs the chip as far as the resampler needs for its next output (the writes are applied just before the
        // step they are due at), then pulls that output. The board chains replace the DC filter (D-047).
        for (uint32_t i = 0; i < n; ++i) {
            const uint64_t need = resampler_.inputNeeded();
            while (ticks_ < need) {
                applyDue();
                const int level = chip_.tick();
                resampler_.push(c10 ? float(c10_.process(level)) : float(level));
                ++ticks_;
            }
            float y = resampler_.pull();
            if (board) y = float(boardPost(y));
            else if (dc) y = dcAdjustFloat(y);
            out[i] = y * kScale;
            if (levels) levels[i] = chip_.levels();
        }
        break;
    case OutputMode::RawBox:
    case OutputMode::Retro: {
        // AtariAudio's loop: each sample runs internal steps until the phase accumulator reaches clk8 (clk8 / host
        // steps per sample on average); RawBox averages them, Retro keeps the last one.
        const bool box = config_.mode == OutputMode::RawBox;
        const uint32_t host = config_.hostRate;
        if (board) {
            // ST and STE chains: same steps and same average, in floating point after C10.
            for (uint32_t i = 0; i < n; ++i) {
                double acc = 0, last = 0;
                int count = 0;
                do {
                    applyDue();
                    const int level = chip_.tick();
                    last = c10 ? c10_.process(level) : double(level);
                    acc += last;
                    ++count;
                    ++ticks_;
                    inner_ += host;
                } while (inner_ < clk8_);
                inner_ -= clk8_;
                out[i] = float(boardPost(box ? acc / count : last)) * kScale;
                if (levels) levels[i] = chip_.levels();
            }
            break;
        }
        // Raw chain: integer arithmetic, as AtariAudio computes it (bit-for-bit comparisons).
        for (uint32_t i = 0; i < n; ++i) {
            int acc = 0, count = 0, last = 0;
            do {
                applyDue();
                last = chip_.tick();
                acc += last;
                ++count;
                ++ticks_;
                inner_ += host;
            } while (inner_ < clk8_);
            inner_ -= clk8_;
            int16_t v = int16_t(box ? acc / count : last);
            if (dc) v = dcAdjust(v);
            out[i] = float(v) * kScale;
            if (levels) levels[i] = chip_.levels();
        }
        break;
    }
    }
    samples_ += n;
}

}  // namespace stym::ymcore
