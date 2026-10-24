// STYM2149 — ymcore: output chain of a stream at the internal step
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymcore/TickStreamOutput.h"

#include <algorithm>
#include <cmath>

namespace stym::ymcore {

void TickStreamOutput::prepare(const RenderConfig& config) {
    config_ = config;
    const MasterClock clk = config.chip.clock;
    clk8_ = uint32_t((clk.num / clk.den) / 8);
    if (config.mode == OutputMode::Raw) resampler_.prepare(clk.num, uint64_t(clk.den) * 8, config.hostRate, config.resampler);
    dcBuf_.assign(size_t(1) << kDcBits, 0);
    const BoardConfig& b = config.board;
    c10_.prepare(clk.hz() / 8, b.pullUpOhm, b.pullDownOhm, b.c10Farad);
    highPass_ = FirstOrder::highPass(b.highPassHz, config.hostRate);
    bass_ = FirstOrder::lowShelf(lmc1992Step(b.bassDb), b.bassHz, config.hostRate);
    treble_ = FirstOrder::highShelf(lmc1992Step(b.trebleDb), b.trebleHz, config.hostRate);
    reset();
}

void TickStreamOutput::setBoardTone(int bassDb, int trebleDb) noexcept {
    BoardConfig& b = config_.board;
    b.bassDb = bassDb;
    b.trebleDb = trebleDb;
    bass_.retune(FirstOrder::lowShelf(lmc1992Step(bassDb), b.bassHz, config_.hostRate));
    treble_.retune(FirstOrder::highShelf(lmc1992Step(trebleDb), b.trebleHz, config_.hostRate));
}

void TickStreamOutput::reset() noexcept {
    ticks_ = 0;
    inner_ = 0;
    std::fill(dcBuf_.begin(), dcBuf_.end(), int64_t(0));
    dcPos_ = 0;
    dcSum_ = 0;
    c10_.reset();
    highPass_.reset();
    bass_.reset();
    treble_.reset();
    if (config_.mode == OutputMode::Raw) resampler_.reset();
}

// Same step count, accumulator and resampler phase as `other`, but an empty history and fresh filters: from now on
// both buses produce sample n from the same internal steps.
void TickStreamOutput::joinSilent(const TickStreamOutput& other) noexcept {
    reset();
    ticks_ = other.ticks_;
    inner_ = other.inner_;
    if (config_.mode == OutputMode::Raw) {
        resampler_.copyStateFrom(other.resampler_);
        resampler_.clearHistory();
    }
}

uint64_t TickStreamOutput::ticksNeeded(uint32_t samples) const noexcept {
    if (samples == 0) return ticks_;
    // Raw: the resampler knows how far its window reaches for the last of these samples.
    if (config_.mode == OutputMode::Raw) return std::max(ticks_, resampler_.inputNeededAfter(samples - 1));
    // AtariAudio loop: each sample consumes steps until the accumulator exceeds clk8.
    uint64_t t = ticks_;
    uint32_t inner = inner_;
    const uint32_t host = config_.hostRate;
    for (uint32_t i = 0; i < samples; ++i) {
        do {
            ++t;
            inner += host;
        } while (inner < clk8_);
        inner -= clk8_;
    }
    return t;
}

// Same moving average as ChipRenderer::dcAdjustFloat, with 32 fraction bits instead of 16.
float TickStreamOutput::dcAdjust(float v) noexcept {
    const int64_t fixed = int64_t(std::floor(double(v) * 4294967296.0 + 0.5));
    dcSum_ -= dcBuf_[dcPos_];
    dcSum_ += fixed;
    dcBuf_[dcPos_] = fixed;
    dcPos_ = (dcPos_ + 1) & ((1u << kDcBits) - 1);
    return float(double(v) - double(dcSum_) / double(int64_t(1) << (kDcBits + 32)));
}

void TickStreamOutput::render(const float* ticks, uint64_t firstTick, float* out, uint32_t n) noexcept {
    const bool dc = config_.dc == DcFilter::MovingAverage;
    const bool board = config_.board.chain != BoardChain::Raw;
    const bool c10 = config_.board.chain == BoardChain::ST;
    // Value of internal step t, through C10 on the ST chain (at the internal step, like ChipRenderer).
    auto at = [&](uint64_t t) {
        const float x = ticks[t - firstTick];
        return c10 ? c10_.process(x) : double(x);
    };
    switch (config_.mode) {
    case OutputMode::Raw:
        for (uint32_t i = 0; i < n; ++i) {
            const uint64_t need = resampler_.inputNeeded();
            for (; ticks_ < need; ++ticks_) resampler_.push(float(at(ticks_)));
            float y = resampler_.pull();
            if (board) y = float(boardPost(y));
            else if (dc) y = dcAdjust(y);
            out[i] = y;
        }
        break;
    case OutputMode::RawBox:
    case OutputMode::Retro: {
        // AtariAudio's loop, as in ChipRenderer::render: average (RawBox) or last (Retro) of the steps of each sample.
        const bool box = config_.mode == OutputMode::RawBox;
        const uint32_t host = config_.hostRate;
        for (uint32_t i = 0; i < n; ++i) {
            double acc = 0, last = 0;
            int count = 0;
            do {
                last = at(ticks_);
                acc += last;
                ++count;
                ++ticks_;
                inner_ += host;
            } while (inner_ < clk8_);
            inner_ -= clk8_;
            float y = float(box ? acc / count : last);
            if (board) y = float(boardPost(y));
            else if (dc) y = dcAdjust(y);
            out[i] = y;
        }
        break;
    }
    }
}

}  // namespace stym::ymcore
