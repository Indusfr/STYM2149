// STYM2149 — ymio: gzip members (YM-Tracker songs)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/Gzip.h"

#include <algorithm>
#include <array>

#include "ymio/Zip.h"

namespace stym::ymio {

namespace {

constexpr int kMaxBits = 15;       // longest code of a deflate Huffman table
constexpr int kLengthCodes = 286;  // literal and length symbols (286 and 287 never occur)
constexpr int kDistCodes = 30;

// Bits of the stream, least significant first; reading past the end sets `over` and gives zeros.
class Bits {
public:
    explicit Bits(std::span<const uint8_t> in) : in_(in) {}
    int get(int n) noexcept {
        uint32_t v = buf_;
        while (cnt_ < n) {
            if (pos_ >= in_.size()) {
                over = true;
                return 0;
            }
            v |= uint32_t(in_[pos_++]) << cnt_;
            cnt_ += 8;
        }
        buf_ = v >> n;
        cnt_ -= n;
        return int(v & ((1u << n) - 1));
    }
    // Drops the bits left in the current byte (stored block).
    void align() noexcept {
        buf_ = 0;
        cnt_ = 0;
    }
    bool byte(uint8_t& b) noexcept {
        if (pos_ >= in_.size()) {
            over = true;
            return false;
        }
        b = in_[pos_++];
        return true;
    }
    size_t position() const noexcept { return pos_; }
    bool over = false;

private:
    std::span<const uint8_t> in_;
    size_t pos_ = 0;
    uint32_t buf_ = 0;
    int cnt_ = 0;
};

// Canonical Huffman table: codes per length, then the symbols in code order.
struct Huffman {
    std::array<int16_t, kMaxBits + 1> count{};
    std::vector<int16_t> symbol;
};

// The table of the code lengths; the codes left over (0 when the code is complete, < 0 when over-subscribed).
int build(Huffman& h, std::span<const uint8_t> lengths) {
    h.count.fill(0);
    for (const uint8_t l : lengths) ++h.count[l];
    h.symbol.assign(lengths.size(), 0);
    if (size_t(h.count[0]) == lengths.size()) return 0;  // no code: complete, but decoding fails
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left = left * 2 - h.count[size_t(len)];
        if (left < 0) return left;
    }
    std::array<int16_t, kMaxBits + 1> offs{};
    for (int len = 1; len < kMaxBits; ++len) offs[size_t(len + 1)] = int16_t(offs[size_t(len)] + h.count[size_t(len)]);
    for (size_t s = 0; s < lengths.size(); ++s)
        if (lengths[s]) h.symbol[size_t(offs[lengths[s]]++)] = int16_t(s);
    return left;
}

// Next symbol, or -1 for a code that the table does not hold.
int decode(Bits& in, const Huffman& h) noexcept {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= in.get(1);
        const int count = h.count[size_t(len)];
        if (code - count < first) return h.symbol[size_t(index + (code - first))];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (in.over) return -1;
    }
    return -1;
}

constexpr std::array<uint16_t, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Literals and copies of a Huffman block, up to its end code.
bool codes(Bits& in, std::vector<uint8_t>& out, size_t max, const Huffman& lens, const Huffman& dists) {
    for (;;) {
        const int sym = decode(in, lens);
        if (sym < 0 || in.over) return false;
        if (sym < 256) {
            if (out.size() >= max) return false;
            out.push_back(uint8_t(sym));
            continue;
        }
        if (sym == 256) return true;
        const int l = sym - 257;
        if (l >= int(kLengthBase.size())) return false;
        const size_t len = kLengthBase[size_t(l)] + size_t(in.get(kLengthExtra[size_t(l)]));
        const int d = decode(in, dists);
        if (d < 0 || d >= kDistCodes) return false;
        const size_t dist = kDistBase[size_t(d)] + size_t(in.get(kDistExtra[size_t(d)]));
        if (in.over || dist > out.size() || len > max - out.size()) return false;
        for (size_t k = 0; k < len; ++k) out.push_back(out[out.size() - dist]);
    }
}

bool fixedBlock(Bits& in, std::vector<uint8_t>& out, size_t max) {
    static const std::array<Huffman, 2> tables = [] {
        std::array<uint8_t, 288> l{};
        for (size_t s = 0; s < l.size(); ++s) l[s] = s < 144 ? 8 : s < 256 ? 9 : s < 280 ? 7 : 8;
        std::array<uint8_t, kDistCodes> d{};
        d.fill(5);
        std::array<Huffman, 2> t;
        build(t[0], l);
        build(t[1], d);
        return t;
    }();
    return codes(in, out, max, tables[0], tables[1]);
}

bool dynamicBlock(Bits& in, std::vector<uint8_t>& out, size_t max) {
    static constexpr std::array<uint8_t, 19> kOrder = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = in.get(5) + 257, ndist = in.get(5) + 1, ncode = in.get(4) + 4;
    if (in.over || nlen > kLengthCodes || ndist > kDistCodes) return false;
    std::array<uint8_t, 19> clen{};
    for (int k = 0; k < ncode; ++k) clen[kOrder[size_t(k)]] = uint8_t(in.get(3));
    Huffman lencode, distcode;
    if (build(lencode, clen) != 0) return false;  // the code of the lengths must be complete
    std::vector<uint8_t> lengths(size_t(nlen + ndist), 0);
    for (size_t k = 0; k < lengths.size();) {
        const int sym = decode(in, lencode);
        if (sym < 0 || in.over) return false;
        if (sym < 16) {
            lengths[k++] = uint8_t(sym);
            continue;
        }
        uint8_t len = 0;
        int rep = 0;
        if (sym == 16) {  // repeat the previous length 3 to 6 times
            if (k == 0) return false;
            len = lengths[k - 1];
            rep = 3 + in.get(2);
        } else if (sym == 17) {
            rep = 3 + in.get(3);
        } else {
            rep = 11 + in.get(7);
        }
        if (k + size_t(rep) > lengths.size()) return false;
        while (rep--) lengths[k++] = len;
    }
    if (lengths[256] == 0) return false;  // no end code
    // An incomplete code is only valid when it holds a single code of one bit.
    const std::span<const uint8_t> all(lengths);
    int err = build(lencode, all.first(size_t(nlen)));
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return false;
    err = build(distcode, all.subspan(size_t(nlen)));
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return false;
    return codes(in, out, max, lencode, distcode);
}

bool storedBlock(Bits& in, std::vector<uint8_t>& out, size_t max) {
    in.align();
    std::array<uint8_t, 4> h{};
    for (uint8_t& b : h)
        if (!in.byte(b)) return false;
    const size_t len = size_t(h[0] | h[1] << 8);
    if (size_t(h[2] | h[3] << 8) != (~len & 0xFFFF) || len > max - out.size()) return false;
    for (size_t k = 0; k < len; ++k) {
        uint8_t b = 0;
        if (!in.byte(b)) return false;
        out.push_back(b);
    }
    return true;
}

GunzipResult refuse(std::string why) {
    GunzipResult r;
    r.error = std::move(why);
    return r;
}

}  // namespace

std::optional<std::vector<uint8_t>> inflate(std::span<const uint8_t> in, size_t maxBytes, size_t* used) {
    Bits bits(in);
    std::vector<uint8_t> out;
    for (bool last = false; !last;) {
        last = bits.get(1) != 0;
        const int type = bits.get(2);
        if (bits.over) return std::nullopt;
        const bool ok = type == 0   ? storedBlock(bits, out, maxBytes)
                        : type == 1 ? fixedBlock(bits, out, maxBytes)
                        : type == 2 ? dynamicBlock(bits, out, maxBytes)
                                    : false;
        if (!ok) return std::nullopt;
    }
    if (used) *used = bits.position();
    return out;
}

GunzipResult gunzip(std::span<const uint8_t> file, size_t maxBytes) {
    // Header: 1F 8B, method 8 (deflate), flags, time (4), extra flags, operating system; then the optional fields
    // that the flags announce: extra field (2-byte length), name and comment (ending with a zero byte), header CRC.
    if (file.size() < 18 || file[0] != 0x1F || file[1] != 0x8B) return refuse("Not a gzip stream.");
    if (file[2] != 8) return refuse("Unknown gzip compression method.");
    const uint8_t flags = file[3];
    if (flags & 0xE0) return refuse("Unknown gzip header flags.");
    size_t at = 10;
    if (flags & 4) {  // FEXTRA
        if (at + 2 > file.size()) return refuse("The gzip header is cut.");
        at += 2 + size_t(file[at] | file[at + 1] << 8);
    }
    for (const uint8_t f : {uint8_t(8), uint8_t(16)}) {  // FNAME, FCOMMENT
        if (!(flags & f)) continue;
        while (at < file.size() && file[at]) ++at;
        ++at;
    }
    if (flags & 2) at += 2;  // FHCRC
    if (at >= file.size()) return refuse("The gzip header is cut.");
    size_t used = 0;
    std::optional<std::vector<uint8_t>> data = inflate(file.subspan(at), maxBytes, &used);
    if (!data) return refuse("The compressed data are damaged or too large.");
    at += used;
    if (at + 8 > file.size()) return refuse("The gzip stream is cut.");
    const auto u32 = [&](size_t p) {
        return uint32_t(file[p]) | uint32_t(file[p + 1]) << 8 | uint32_t(file[p + 2]) << 16 | uint32_t(file[p + 3]) << 24;
    };
    if (u32(at) != crc32(*data)) return refuse("The gzip checksum does not match.");
    if (u32(at + 4) != uint32_t(data->size())) return refuse("The gzip length does not match.");
    GunzipResult r;
    r.data = std::move(data);
    return r;
}

std::vector<uint8_t> gzipStore(std::span<const uint8_t> data) {
    std::vector<uint8_t> out = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 255};
    size_t at = 0;
    do {
        const size_t n = std::min<size_t>(data.size() - at, 0xFFFF);
        const bool last = at + n == data.size();
        out.push_back(last ? 1 : 0);  // BFINAL, BTYPE 00 (stored)
        out.push_back(uint8_t(n));
        out.push_back(uint8_t(n >> 8));
        out.push_back(uint8_t(~n));
        out.push_back(uint8_t(~n >> 8));
        out.insert(out.end(), data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + n));
        at += n;
    } while (at < data.size());
    const uint32_t crc = crc32(data), size = uint32_t(data.size());
    for (const uint32_t v : {crc, size})
        for (int k = 0; k < 4; ++k) out.push_back(uint8_t(v >> (8 * k)));
    return out;
}

}  // namespace stym::ymio
