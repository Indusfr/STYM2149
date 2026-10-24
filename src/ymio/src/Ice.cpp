// STYM2149 — ymio: bounded Ice 2.4 decompression
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// From Hans Wessels' ice_24_depack (public domain, 2007). The length and distance tables and the bit reading order
// are his; the bounds are added.
//
// Everything runs backwards: the command bits and the literal bytes are taken from the end of the packed data towards
// its header, and the output is filled from its last byte towards its first. Each step is a run of literal bytes,
// then a copy of bytes already produced (which lie after the write position in the output).
#include "ymio/Ice.h"

#include <array>

namespace stym::ymio {

namespace {

constexpr size_t kHeader = 12;  // "ICE!", packed size, original size

uint32_t be32(const uint8_t* p) noexcept {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

// Bit stream read from the end towards the start, one byte at a time, most significant bit first (ice24_getbits).
class Bits {
public:
    Bits(const uint8_t* data, size_t end) noexcept : data_(data), pos_(end) {}

    // Reads len bits into out, first bit read = most significant. False if the stream is exhausted (read into the
    // header).
    bool get(int len, uint32_t& out) noexcept {
        uint32_t v = 0;
        for (; len > 0; --len) {
            v += v;
            mask_ >>= 1;
            if (mask_ == 0) {
                if (pos_ <= kHeader) return false;
                cmd_ = data_[--pos_];
                mask_ = 0x80;
            }
            if (cmd_ & mask_) v += 1;
        }
        out = v;
        return true;
    }
    // Next literal byte, from the same backward stream as the command bytes.
    bool byte(uint8_t& out) noexcept {
        if (pos_ <= kHeader) return false;
        out = data_[--pos_];
        return true;
    }
    // First command byte: its lowest bit set to 1 marks the end of the valid bits ("fix reload").
    bool init() noexcept {
        uint32_t first = 0;
        if (!get(1, first)) return false;
        mask_ = 0x80;
        while (!(cmd_ & 1)) {
            cmd_ >>= 1;
            mask_ >>= 1;
            if (mask_ == 0) return false;  // zero byte: invalid stream
        }
        cmd_ >>= 1;
        return true;
    }

private:
    const uint8_t* data_;
    size_t pos_;                   // next byte read: data_[pos_ - 1]
    uint32_t cmd_ = 0, mask_ = 0;  // current command byte and the bit of it read next (0: load a new byte)
};

}  // namespace

bool isIce(std::span<const uint8_t> data) noexcept {
    return data.size() >= kHeader && be32(data.data()) == 0x49434521u;  // "ICE!"
}

std::optional<std::vector<uint8_t>> iceDepack(std::span<const uint8_t> data, size_t maxSize) {
    if (!isIce(data)) return std::nullopt;
    const size_t packed = be32(data.data() + 4);
    const size_t orig = be32(data.data() + 8);
    if (packed <= kHeader || packed > data.size() || orig == 0 || orig > maxSize) return std::nullopt;
    std::vector<uint8_t> out(orig);
    Bits bits(data.data(), packed);
    if (!bits.init()) return std::nullopt;
    size_t p = orig;  // next byte written: out[p - 1]
    for (;;) {
        uint32_t literal = 0;
        if (!bits.get(1, literal)) return std::nullopt;
        if (literal) {
            // Run length: fields of 1, 2, 2, 3, 8 then 15 bits; a field at its maximum value means "read the next
            // field", and the run is the last field read plus its offset (1 to 270).
            static constexpr std::array<int, 6> kLenBits{1, 2, 2, 3, 8, 15};
            static constexpr std::array<uint32_t, 6> kMaxLen{1, 3, 3, 7, 255, 32768};
            static constexpr std::array<uint32_t, 6> kOffset{1, 2, 5, 8, 15, 270};
            size_t t = 0;
            uint32_t len = 0;
            for (;; ++t) {
                if (!bits.get(kLenBits[t], len)) return std::nullopt;
                if (len != kMaxLen[t] || t + 1 == kLenBits.size()) break;
            }
            size_t n = std::min<size_t>(len + kOffset[t], p);
            for (; n > 0; --n)
                if (!bits.byte(out[--p])) return std::nullopt;
            if (p == 0) return out;
        }
        // Copy of a string already written (always after a literal).
        // Length: up to four 1 bits choose a class, whose extra bits are added to its base; copy = lenCode + 2 bytes.
        static constexpr std::array<int, 5> kLenExtra{0, 0, 1, 2, 10};
        static constexpr std::array<uint32_t, 5> kLenOffset{0, 1, 2, 4, 8};
        size_t t = 0;
        for (uint32_t b = 0;;) {
            if (!bits.get(1, b)) return std::nullopt;
            if (!b) break;
            if (++t == 4) break;
        }
        uint32_t extra = 0;
        if (!bits.get(kLenExtra[t], extra)) return std::nullopt;
        const uint32_t lenCode = kLenOffset[t] + extra;
        // Distance: for copies of three bytes or more, up to two 1 bits choose 8, 5 or 12 bits (bases 32, 0, 288),
        // a non-zero distance then growing by lenCode; for two-byte copies, 6 bits, or 9 bits + 64 after a 1 bit.
        uint32_t pos = 0;
        if (lenCode) {
            static constexpr std::array<int, 3> kPosBits{8, 5, 12};
            static constexpr std::array<uint32_t, 3> kPosOffset{32, 0, 288};
            size_t u = 0;
            for (uint32_t b = 0;;) {
                if (!bits.get(1, b)) return std::nullopt;
                if (!b) break;
                if (++u == 2) break;
            }
            if (!bits.get(kPosBits[u], pos)) return std::nullopt;
            pos += kPosOffset[u];
            if (pos != 0) pos += lenCode;
        } else {
            uint32_t far = 0;
            if (!bits.get(1, far)) return std::nullopt;
            if (far) {
                if (!bits.get(9, pos)) return std::nullopt;
                pos += 64;
            } else if (!bits.get(6, pos)) {
                return std::nullopt;
            }
        }
        // Source: out[p + pos] and below, already written.
        size_t q = p + pos + 1;
        if (q > orig) return std::nullopt;
        size_t n = std::min<size_t>(size_t(lenCode) + 2, p);
        for (; n > 0; --n) out[--p] = out[--q];
        if (p == 0) return out;
    }
}

}  // namespace stym::ymio
