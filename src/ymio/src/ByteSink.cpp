// STYM2149 — ymio: destination of a file written in pieces (WAV and stem exports)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/ByteSink.h"

#include <algorithm>

namespace stym::ymio {

bool MemorySink::write(std::span<const uint8_t> b) {
    if (!ok_) return false;
    const size_t at = size_t(pos_);
    if (at + b.size() > bytes.size()) bytes.resize(at + b.size());
    std::copy(b.begin(), b.end(), bytes.begin() + ptrdiff_t(at));
    pos_ += b.size();
    return true;
}

// A seek past the end is a failure, and like every failure it is final.
bool MemorySink::seek(uint64_t position) {
    if (!ok_ || position > bytes.size()) return ok_ = false;
    pos_ = position;
    return true;
}

}  // namespace stym::ymio
