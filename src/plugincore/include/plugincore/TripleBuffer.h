// STYM2149 — plugincore: triple buffer between a writing thread and a reading thread, without lock or allocation
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The writer fills back() then publishes; the reader takes the last publication with update() and reads it in
// front(). The three objects rotate: the writer never touches the object that the reader reads, and a publication
// not yet read is replaced by the next one (only the most recent counts).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace stym::plugin {

// One writing thread and one reading thread; T is held three times inside (EngineStatus: audio thread → editor).
template <class T>
class TripleBuffer {
public:
    // Writing thread.
    T& back() noexcept { return items_[back_]; }
    void publish() noexcept { back_ = uint8_t(middle_.exchange(uint8_t(back_ | kFresh), std::memory_order_acq_rel) & kIndex); }

    // Reading thread: true if a publication has arrived since the last read (front() then gives it).
    bool update() noexcept {
        if ((middle_.load(std::memory_order_relaxed) & kFresh) == 0) return false;
        front_ = uint8_t(middle_.exchange(front_, std::memory_order_acq_rel) & kIndex);
        return true;
    }
    const T& front() const noexcept { return items_[front_]; }

private:
    // middle_ holds the index of the third object (bits 0-1, kIndex) and kFresh (bit 2): set by publish(), which
    // swaps it with back_; cleared by update(), which swaps it with front_. back_ belongs to the writer, front_ to the
    // reader.
    static constexpr uint8_t kIndex = 3, kFresh = 4;
    std::array<T, 3> items_{};
    uint8_t back_ = 0, front_ = 1;
    std::atomic<uint8_t> middle_{2};
};

}  // namespace stym::plugin
