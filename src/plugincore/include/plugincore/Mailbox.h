// STYM2149 — plugincore: passing objects between the message thread and the audio thread, without lock or allocation
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The message thread posts an object (post); the audio thread takes it (take), uses it, then hands it back (retire)
// when another one replaces it. The returned objects are destroyed by the message thread (collect): the audio thread
// never frees memory. An object posted but not yet taken is destroyed by the next post (the audio thread can no
// longer get it: the atomic exchange has handed it back to the message thread).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace stym::plugin {

// Capacity: objects retired and not yet collected. head_ and tail_ are free-running 32-bit counters (their difference
// is the fill level); Capacity must be a power of two so that counter % Capacity stays consistent when they wrap.
template <class T, uint32_t Capacity = 16>
class Mailbox {
public:
    Mailbox() = default;
    ~Mailbox() { clear(); }
    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;

    // Message thread.
    void post(std::unique_ptr<T> item) noexcept { delete pending_.exchange(item.release(), std::memory_order_acq_rel); }
    void collect() noexcept {
        uint32_t h = head_.load(std::memory_order_relaxed);
        const uint32_t t = tail_.load(std::memory_order_acquire);
        for (; h != t; ++h) {
            delete ring_[h % Capacity];
            ring_[h % Capacity] = nullptr;
        }
        head_.store(h, std::memory_order_release);
    }
    bool hasPending() const noexcept { return pending_.load(std::memory_order_acquire) != nullptr; }
    // Message thread, audio thread stopped: destroys everything.
    void clear() noexcept {
        collect();
        delete pending_.exchange(nullptr, std::memory_order_acq_rel);
    }

    // Audio thread.
    bool canRetire() const noexcept {
        return tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_acquire) < Capacity;
    }
    T* take() noexcept { return pending_.exchange(nullptr, std::memory_order_acq_rel); }
    // Hands an object back to the message thread. False if the queue is full (check canRetire() before taking).
    bool retire(T* item) noexcept {
        if (!item) return true;
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t - head_.load(std::memory_order_acquire) >= Capacity) return false;
        ring_[t % Capacity] = item;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    std::atomic<T*> pending_{nullptr};  // posted, not yet taken
    std::array<T*, Capacity> ring_{};   // retired objects: written at tail_ (audio), freed from head_ (message)
    std::atomic<uint32_t> head_{0}, tail_{0};
};

}  // namespace stym::plugin
