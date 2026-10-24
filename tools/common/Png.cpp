// STYM2149 — tools: reading and writing PNG images
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Formats: PNG (ISO/IEC 15948), zlib (RFC 1950), deflate (RFC 1951). Canonical Huffman decoding in the manner of
// puff (zlib/contrib).
#include "Png.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

#include "ReadFile.h"

namespace stym::tools {
namespace {

// --------------------------------------------------------------------------------------------- inflate

constexpr std::array<uint16_t, 29> kLenBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLenExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistBase = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                                33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                                1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct Huffman {
    std::array<uint16_t, 16> count{};
    std::array<uint16_t, 320> symbol{};
};

class Inflater {
public:
    explicit Inflater(std::span<const uint8_t> in) : in_(in) {}

    bool run(std::vector<uint8_t>& out) {
        bool last = false;
        while (!last) {
            last = bits(1) != 0;
            const uint32_t type = bits(2);
            bool ok = false;
            if (type == 0) ok = stored(out);
            else if (type == 1) ok = fixed(out);
            else if (type == 2) ok = dynamic(out);
            if (!ok || failed_) return false;
        }
        return true;
    }

private:
    uint32_t bits(int n) {
        uint32_t v = bitBuf_;
        while (bitCount_ < n) {
            if (pos_ >= in_.size()) {
                failed_ = true;
                return 0;
            }
            v |= uint32_t(in_[pos_++]) << bitCount_;
            bitCount_ += 8;
        }
        bitBuf_ = v >> n;
        bitCount_ -= n;
        return v & ((1u << n) - 1);
    }

    bool stored(std::vector<uint8_t>& out) {
        bitBuf_ = 0;
        bitCount_ = 0;
        if (pos_ + 4 > in_.size()) return false;
        const uint32_t len = in_[pos_] | uint32_t(in_[pos_ + 1]) << 8;
        const uint32_t nlen = in_[pos_ + 2] | uint32_t(in_[pos_ + 3]) << 8;
        pos_ += 4;
        if (len != (~nlen & 0xFFFF) || pos_ + len > in_.size()) return false;
        out.insert(out.end(), in_.begin() + long(pos_), in_.begin() + long(pos_ + len));
        pos_ += len;
        return true;
    }

    static bool build(Huffman& h, const uint8_t* lengths, int n) {
        h.count.fill(0);
        for (int i = 0; i < n; ++i) ++h.count[lengths[i]];
        if (h.count[0] == n) return true;  // no code (block without distances)
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= h.count[size_t(len)];
            if (left < 0) return false;  // too many codes
        }
        std::array<uint16_t, 16> offs{};
        for (int len = 1; len < 15; ++len) offs[size_t(len + 1)] = uint16_t(offs[size_t(len)] + h.count[size_t(len)]);
        for (int i = 0; i < n; ++i)
            if (lengths[i]) h.symbol[offs[lengths[i]]++] = uint16_t(i);
        return true;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= int(bits(1));
            const int count = h.count[size_t(len)];
            if (code - count < first) return h.symbol[size_t(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        failed_ = true;
        return -1;
    }

    bool codes(std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist) {
        for (;;) {
            const int sym = decode(lit);
            if (failed_ || sym < 0) return false;
            if (sym < 256) {
                out.push_back(uint8_t(sym));
            } else if (sym == 256) {
                return true;
            } else {
                const int li = sym - 257;
                if (li >= 29) return false;
                const size_t len = kLenBase[size_t(li)] + bits(kLenExtra[size_t(li)]);
                const int di = decode(dist);
                if (di < 0 || di >= 30) return false;
                const size_t d = kDistBase[size_t(di)] + bits(kDistExtra[size_t(di)]);
                if (d > out.size()) return false;
                const size_t from = out.size() - d;
                for (size_t k = 0; k < len; ++k) out.push_back(out[from + k]);
            }
        }
    }

    bool fixed(std::vector<uint8_t>& out) {
        std::array<uint8_t, 320> l{};
        for (int i = 0; i < 144; ++i) l[size_t(i)] = 8;
        for (int i = 144; i < 256; ++i) l[size_t(i)] = 9;
        for (int i = 256; i < 280; ++i) l[size_t(i)] = 7;
        for (int i = 280; i < 288; ++i) l[size_t(i)] = 8;
        Huffman lit, dist;
        build(lit, l.data(), 288);
        std::array<uint8_t, 30> d{};
        d.fill(5);
        build(dist, d.data(), 30);
        return codes(out, lit, dist);
    }

    bool dynamic(std::vector<uint8_t>& out) {
        static constexpr std::array<uint8_t, 19> kOrder = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                                           11, 4, 12, 3, 13, 2, 14, 1, 15};
        const int nlen = int(bits(5)) + 257, ndist = int(bits(5)) + 1, ncode = int(bits(4)) + 4;
        if (nlen > 286 || ndist > 30) return false;
        std::array<uint8_t, 320> lengths{};
        for (int i = 0; i < ncode; ++i) lengths[kOrder[size_t(i)]] = uint8_t(bits(3));
        Huffman lencode;
        if (!build(lencode, lengths.data(), 19)) return false;
        lengths.fill(0);
        int index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (failed_ || sym < 0) return false;
            if (sym < 16) {
                lengths[size_t(index++)] = uint8_t(sym);
                continue;
            }
            uint8_t len = 0;
            int rep;
            if (sym == 16) {
                if (index == 0) return false;
                len = lengths[size_t(index - 1)];
                rep = 3 + int(bits(2));
            } else if (sym == 17) {
                rep = 3 + int(bits(3));
            } else {
                rep = 11 + int(bits(7));
            }
            if (index + rep > nlen + ndist) return false;
            while (rep--) lengths[size_t(index++)] = len;
        }
        if (lengths[256] == 0) return false;  // no end of block
        Huffman lit, dist;
        if (!build(lit, lengths.data(), nlen) || !build(dist, lengths.data() + nlen, ndist)) return false;
        return codes(out, lit, dist);
    }

    std::span<const uint8_t> in_;
    size_t pos_ = 0;
    uint32_t bitBuf_ = 0;
    int bitCount_ = 0;
    bool failed_ = false;
};

// --------------------------------------------------------------------------------------------- deflate

class BitWriter {
public:
    void put(uint32_t v, int n) {  // least significant bits first
        buf_ |= uint64_t(v) << count_;
        count_ += n;
        while (count_ >= 8) {
            out.push_back(uint8_t(buf_));
            buf_ >>= 8;
            count_ -= 8;
        }
    }
    void putReversed(uint32_t code, int n) {  // Huffman code: most significant bit first
        uint32_t r = 0;
        for (int i = 0; i < n; ++i) r |= ((code >> i) & 1) << (n - 1 - i);
        put(r, n);
    }
    void flush() {
        if (count_) out.push_back(uint8_t(buf_));
        buf_ = 0;
        count_ = 0;
    }
    std::vector<uint8_t> out;

private:
    uint64_t buf_ = 0;
    int count_ = 0;
};

void literal(BitWriter& w, int v) {
    if (v < 144) w.putReversed(uint32_t(0x30 + v), 8);
    else if (v < 256) w.putReversed(uint32_t(0x190 + v - 144), 9);
    else if (v < 280) w.putReversed(uint32_t(v - 256), 7);
    else w.putReversed(uint32_t(0xC0 + v - 280), 8);
}

void match(BitWriter& w, size_t len, size_t dist) {
    int li = 28;
    while (kLenBase[size_t(li)] > len) --li;
    literal(w, 257 + li);
    w.put(uint32_t(len - kLenBase[size_t(li)]), kLenExtra[size_t(li)]);
    int di = 29;
    while (kDistBase[size_t(di)] > dist) --di;
    w.putReversed(uint32_t(di), 5);
    w.put(uint32_t(dist - kDistBase[size_t(di)]), kDistExtra[size_t(di)]);
}

// One block with fixed codes; matches are searched at the previous position with the same hash and at the distances
// `extra` (previous pixel, previous row).
std::vector<uint8_t> deflate(const std::vector<uint8_t>& in, std::initializer_list<size_t> extra) {
    BitWriter w;
    w.put(1, 1);  // last block
    w.put(1, 2);  // fixed codes
    std::vector<int32_t> head(1 << 16, -1);
    auto hash = [&](size_t i) { return (uint32_t(in[i]) << 8 ^ uint32_t(in[i + 1]) << 4 ^ in[i + 2]) & 0xFFFF; };
    size_t i = 0;
    const size_t n = in.size();
    while (i < n) {
        size_t bestLen = 0, bestDist = 0;
        if (i + 3 <= n) {
            auto tryDist = [&](size_t d) {
                if (d == 0 || d > i || d > 32768) return;
                size_t len = 0;
                const size_t max = std::min<size_t>(258, n - i);
                while (len < max && in[i + len] == in[i - d + len]) ++len;
                if (len > bestLen) {
                    bestLen = len;
                    bestDist = d;
                }
            };
            const uint32_t h = hash(i);
            if (head[h] >= 0) tryDist(i - size_t(head[h]));
            for (size_t d : extra) tryDist(d);
            head[h] = int32_t(i);
        }
        if (bestLen >= 3) {
            match(w, bestLen, bestDist);
            for (size_t k = 1; k < bestLen && i + k + 3 <= n; ++k) head[hash(i + k)] = int32_t(i + k);
            i += bestLen;
        } else {
            literal(w, in[i]);
            ++i;
        }
    }
    literal(w, 256);
    w.flush();
    return std::move(w.out);
}

// --------------------------------------------------------------------------------------------- PNG

uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

void putBe32(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    putBe32(out, uint32_t(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    putBe32(out, crc32(out.data() + start, out.size() - start));
}

constexpr std::array<uint8_t, 8> kSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

Image fail(std::string* error, const char* why) {
    if (error) *error = why;
    return {};
}

int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

}  // namespace

Image decodePng(std::span<const uint8_t> d, std::string* error) {
    if (d.size() < 8 || !std::equal(kSignature.begin(), kSignature.end(), d.begin()))
        return fail(error, "not a PNG image");
    size_t pos = 8;
    int w = 0, h = 0, colorType = -1;
    std::vector<uint8_t> z;
    while (pos + 12 <= d.size()) {
        const uint32_t len = be32(&d[pos]);
        const std::string type(reinterpret_cast<const char*>(&d[pos + 4]), 4);
        if (pos + 12 + len > d.size()) return fail(error, "truncated PNG chunk");
        const uint8_t* body = &d[pos + 8];
        if (type == "IHDR") {
            if (len < 13) return fail(error, "IHDR too short");
            w = int(be32(body));
            h = int(be32(body + 4));
            if (body[8] != 8) return fail(error, "bit depth other than 8");
            colorType = body[9];
            if (colorType != 2 && colorType != 6) return fail(error, "neither RGB nor RGBA");
            if (body[12] != 0) return fail(error, "interlaced image");
        } else if (type == "IDAT") {
            z.insert(z.end(), body, body + len);
        } else if (type == "IEND") {
            break;
        }
        pos += 12 + len;
    }
    if (w <= 0 || h <= 0 || z.size() < 6) return fail(error, "empty image");
    std::vector<uint8_t> raw;
    Inflater inflater(std::span<const uint8_t>(z).subspan(2));  // zlib header
    if (!inflater.run(raw)) return fail(error, "invalid compressed data");
    const int bpp = colorType == 6 ? 4 : 3;
    const size_t stride = size_t(w) * size_t(bpp);
    if (raw.size() < size_t(h) * (stride + 1)) return fail(error, "data too short");
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(size_t(w) * size_t(h));
    std::vector<uint8_t> prev(stride, 0), row(stride);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = &raw[size_t(y) * (stride + 1)];
        const uint8_t filter = src[0];
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= size_t(bpp) ? row[i - size_t(bpp)] : 0, b = prev[i],
                      c = i >= size_t(bpp) ? prev[i - size_t(bpp)] : 0;
            int v = src[i + 1];
            switch (filter) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: return fail(error, "unknown PNG filter");
            }
            row[i] = uint8_t(v);
        }
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = &row[size_t(x) * size_t(bpp)];
            const uint32_t alpha = bpp == 4 ? p[3] : 0xFF;
            img.pixels[size_t(y) * size_t(w) + size_t(x)] = alpha << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
        }
        std::swap(prev, row);
    }
    return img;
}

std::vector<uint8_t> encodePng(const Image& img) {
    const size_t stride = size_t(img.width) * 3;
    std::vector<uint8_t> raw;
    raw.reserve(size_t(img.height) * (stride + 1));
    for (int y = 0; y < img.height; ++y) {
        raw.push_back(0);  // no filter
        for (int x = 0; x < img.width; ++x) {
            const uint32_t c = img.at(x, y);
            raw.push_back(uint8_t(c >> 16));
            raw.push_back(uint8_t(c >> 8));
            raw.push_back(uint8_t(c));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    const std::vector<uint8_t> body = deflate(raw, {3, stride + 1});
    z.insert(z.end(), body.begin(), body.end());
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) {
        s1 = (s1 + b) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    putBe32(z, s2 << 16 | s1);

    std::vector<uint8_t> out(kSignature.begin(), kSignature.end());
    std::vector<uint8_t> ihdr;
    putBe32(ihdr, uint32_t(img.width));
    putBe32(ihdr, uint32_t(img.height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

bool readPng(const std::string& path, Image& out, std::string* error) {
    const std::vector<uint8_t> data = readFile(path.c_str());
    if (data.empty()) {
        if (error) *error = "missing or empty file: " + path;
        return false;
    }
    out = decodePng(data, error);
    return out.width > 0;
}

bool writePng(const std::string& path, const Image& image) {
    const std::vector<uint8_t> data = encodePng(image);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace stym::tools
