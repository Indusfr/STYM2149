// STYM2149 — plugincore: sample bytes shared between the copies of the state
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The Digidrum and DMA samples of a project go up to the memory of the chosen STE (4 MB) in the 3-channel modes, and
// without limit in 32-channel mode. The state is copied at each setting edited (editExtras), and again for the audio
// thread (Driver.h, Snapshot): the copies share the bytes instead of copying them. A sample never changes once made:
// an edit makes a new one (vec(), changed, assigned back), so that the bytes the engine reads (Engine::shareUserSample,
// shareDmaSample) stay as they are while a copy of the state that holds them lives.
#pragma once
#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

namespace stym::plugin {

template <class T>
class SharedSamples {
public:
    using value_type = T;
    using const_iterator = const T*;

    SharedSamples() = default;
    SharedSamples(std::vector<T> v)  // NOLINT: implicit, assigned from a vector
        : p_(v.empty() ? nullptr : std::make_shared<const std::vector<T>>(std::move(v))) {}
    SharedSamples(std::span<const T> s) : SharedSamples(std::vector<T>(s.begin(), s.end())) {}  // NOLINT
    SharedSamples(std::initializer_list<T> l) : SharedSamples(std::vector<T>(l)) {}

    size_t size() const noexcept { return p_ ? p_->size() : 0; }
    bool empty() const noexcept { return size() == 0; }
    const T* data() const noexcept { return p_ ? p_->data() : nullptr; }
    const T* begin() const noexcept { return data(); }
    const T* end() const noexcept { return data() + size(); }
    const T& operator[](size_t i) const noexcept { return (*p_)[i]; }
    const T& front() const noexcept { return p_->front(); }
    const T& back() const noexcept { return p_->back(); }
    // Also converts to std::span<const T> by itself, as a contiguous range.
    std::span<const T> span() const noexcept { return {data(), size()}; }
    // A copy of the bytes, to edit them and assign them back as a new sample.
    std::vector<T> vec() const { return p_ ? *p_ : std::vector<T>{}; }
    void clear() noexcept { p_.reset(); }

    // Same bytes (the same buffer, or equal contents).
    bool operator==(const SharedSamples& o) const noexcept {
        return p_ == o.p_ || std::ranges::equal(span(), o.span());
    }
    bool operator==(const std::vector<T>& v) const noexcept { return std::ranges::equal(span(), v); }

private:
    std::shared_ptr<const std::vector<T>> p_;
};

}  // namespace stym::plugin
