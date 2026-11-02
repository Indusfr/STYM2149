// STYM2149 — plugincore: fixed-size array kept on the heap
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The 127 project instruments weigh over 100 KB: in a std::array, every NonParamState made on a host thread's stack
// (File menu, editor, tests) would carry them. A HeapArray behaves like the std::array (indexing, iteration, size,
// equality) and copies its elements, but keeps them on the heap. A moved-from one reads as N default elements and
// allocates them again at its first write.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

namespace stym::plugin {

template <class T, size_t N>
class HeapArray {
public:
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    HeapArray() : p_(std::make_unique<std::array<T, N>>()) {}
    HeapArray(const HeapArray& o) : p_(std::make_unique<std::array<T, N>>(o.get())) {}
    HeapArray(HeapArray&& o) noexcept = default;
    HeapArray& operator=(const HeapArray& o) {
        if (this != &o) mut() = o.get();
        return *this;
    }
    HeapArray& operator=(HeapArray&& o) noexcept {
        p_.swap(o.p_);
        return *this;
    }

    static constexpr size_t size() noexcept { return N; }
    T& operator[](size_t i) { return mut()[i]; }
    const T& operator[](size_t i) const { return get()[i]; }
    T* begin() { return mut().data(); }
    T* end() { return mut().data() + N; }
    const T* begin() const { return get().data(); }
    const T* end() const { return get().data() + N; }

    bool operator==(const HeapArray& o) const { return p_ == o.p_ || get() == o.get(); }

private:
    const std::array<T, N>& get() const {
        static const std::array<T, N> kEmpty{};
        return p_ ? *p_ : kEmpty;
    }
    std::array<T, N>& mut() {
        if (!p_) p_ = std::make_unique<std::array<T, N>>();
        return *p_;
    }

    std::unique_ptr<std::array<T, N>> p_;
};

}  // namespace stym::plugin
