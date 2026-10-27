// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <array>
#include <cstdint>

namespace stym::ymcore {

// Register write timestamped in master clock cycles since the last reset.
struct RegisterWrite {
    uint64_t cycle;
    uint8_t reg;    // 0-15, or a command such as Ym2149::kToneEdgeReset
    uint8_t value;
};

// Fixed-capacity queue, without allocation, for a single producer and a single consumer on the same thread
// (the audio thread). Writes stay in order: a write dated before the previous one is brought to the date of the
// previous one. Ring buffer: head_ is the oldest write, count_ the number queued.
class RegisterQueue {
public:
    static constexpr uint32_t kCapacity = 4096;

    bool full() const noexcept { return count_ == kCapacity; }
    bool empty() const noexcept { return count_ == 0; }
    uint32_t size() const noexcept { return count_; }
    uint64_t lastCycle() const noexcept { return last_; }

    // Returns false if the queue is full (nothing is added).
    bool push(uint64_t cycle, uint8_t reg, uint8_t value) noexcept {
        if (count_ == kCapacity) return false;
        if (cycle < last_) {
            cycle = last_;
            ++reordered_;
        }
        buf_[(head_ + count_) & (kCapacity - 1)] = {cycle, reg, value};
        ++count_;
        last_ = cycle;
        return true;
    }
    const RegisterWrite& front() const noexcept { return buf_[head_]; }
    void pop() noexcept {
        head_ = (head_ + 1) & (kCapacity - 1);
        --count_;
    }
    void clear() noexcept {
        head_ = count_ = 0;
        last_ = 0;
        reordered_ = 0;
    }
    // Number of writes that arrived out of order since the last clear().
    uint64_t reordered() const noexcept { return reordered_; }

private:
    static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of 2");
    std::array<RegisterWrite, kCapacity> buf_{};
    uint32_t head_ = 0;
    uint32_t count_ = 0;
    uint64_t last_ = 0;
    uint64_t reordered_ = 0;
};

}  // namespace stym::ymcore
