// STYM2149 — ymio: uncompressed ZIP archive of the stems (M7, A4)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/Zip.h"

#include <array>

namespace stym::ymio {

namespace {

constexpr std::array<uint32_t, 256> kCrcTable = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[n] = c;
    }
    return t;
}();

constexpr uint16_t kDosDate = (0 << 9) | (1 << 5) | 1;  // 1 January 1980
constexpr uint16_t kUtf8 = 1 << 11;

void le16(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
void le32(std::vector<uint8_t>& o, uint32_t v) {
    le16(o, v & 0xFFFF);
    le16(o, v >> 16);
}

// Product of a 32 × 32 matrix over GF(2) by a vector, and square of a matrix (shift of the CRC by zeros).
uint32_t gf2Times(const std::array<uint32_t, 32>& m, uint32_t v) noexcept {
    uint32_t sum = 0;
    for (int i = 0; v; ++i, v >>= 1)
        if (v & 1) sum ^= m[size_t(i)];
    return sum;
}
std::array<uint32_t, 32> gf2Square(const std::array<uint32_t, 32>& m) noexcept {
    std::array<uint32_t, 32> s{};
    for (size_t n = 0; n < 32; ++n) s[n] = gf2Times(m, m[n]);
    return s;
}

// Local file header (30 bytes + name): its size never changes, so end() can rewrite it in place.
std::vector<uint8_t> localHeader(const std::string& name, uint32_t crc, uint32_t size) {
    std::vector<uint8_t> h;
    le32(h, 0x04034B50);  // "PK\3\4"
    le16(h, 20);     // version needed
    le16(h, kUtf8);  // flags
    le16(h, 0);      // stored
    le16(h, 0);      // time
    le16(h, kDosDate);
    le32(h, crc);
    le32(h, size);  // compressed size = size (stored)
    le32(h, size);
    le16(h, uint32_t(name.size()));
    le16(h, 0);     // extra field
    h.insert(h.end(), name.begin(), name.end());
    return h;
}

}  // namespace

uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc) noexcept {
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (const uint8_t b : bytes) c = kCrcTable[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// Same method as zlib's crc32_combine: CRC(A + B) = CRC(A shifted by len(B) zero bytes) ⊕ CRC(B), the shift being
// applied with powers of the one-zero-bit operator over GF(2), one per bit of lengthB.
uint32_t crc32Combine(uint32_t crcA, uint32_t crcB, uint64_t lengthB) noexcept {
    if (lengthB == 0) return crcA;
    // Operator for one zero bit, then 2, 4, 8... bits: crcA shifted by lengthB zero bytes.
    std::array<uint32_t, 32> odd{}, even{};
    odd[0] = 0xEDB88320u;
    for (size_t n = 1; n < 32; ++n) odd[n] = 1u << (n - 1);
    even = gf2Square(odd);  // 2 bits
    odd = gf2Square(even);  // 4 bits
    do {
        even = gf2Square(odd);  // 8 bits, then 32, 128...
        if (lengthB & 1) crcA = gf2Times(even, crcA);
        lengthB >>= 1;
        if (!lengthB) break;
        odd = gf2Square(even);
        if (lengthB & 1) crcA = gf2Times(odd, crcA);
        lengthB >>= 1;
    } while (lengthB);
    return crcA ^ crcB;
}

bool ZipWriter::begin(const std::string& name, size_t prefixBytes) {
    if (open_ || entries_.size() >= 0xFFFF) return false;
    Entry e;
    e.name = name;
    e.offset = sink_.position();
    entries_.push_back(e);
    prefix_ = prefixBytes;
    bodyCrc_ = 0;
    bodySize_ = 0;
    open_ = true;
    const std::vector<uint8_t> zeros(prefixBytes, 0);
    return sink_.write(localHeader(name, 0, 0)) && sink_.write(zeros);
}

bool ZipWriter::write(std::span<const uint8_t> body) {
    if (!open_) return false;
    bodyCrc_ = crc32(body, bodyCrc_);
    bodySize_ += body.size();
    return sink_.write(body);
}

bool ZipWriter::end(std::span<const uint8_t> prefix) {
    if (!open_ || prefix.size() != prefix_) return false;
    open_ = false;
    Entry& e = entries_.back();
    e.size = prefix_ + bodySize_;
    e.crc = crc32Combine(crc32(prefix), bodyCrc_, bodySize_);
    const uint64_t endAt = sink_.position();
    if (endAt > kMaxBytes) return false;
    const std::vector<uint8_t> h = localHeader(e.name, e.crc, uint32_t(e.size));
    return sink_.seek(e.offset) && sink_.write(h) && sink_.write(prefix) && sink_.seek(endAt);
}

bool ZipWriter::finish() {
    if (open_) return false;
    const uint64_t cdAt = sink_.position();
    std::vector<uint8_t> cd;
    // Central directory: one record per entry (same fields as its local header, plus the header's offset), then the
    // end-of-directory record.
    for (const Entry& e : entries_) {
        le32(cd, 0x02014B50);
        le16(cd, 20);  // version made by
        le16(cd, 20);  // version needed
        le16(cd, kUtf8);
        le16(cd, 0);
        le16(cd, 0);
        le16(cd, kDosDate);
        le32(cd, e.crc);
        le32(cd, uint32_t(e.size));
        le32(cd, uint32_t(e.size));
        le16(cd, uint32_t(e.name.size()));
        le16(cd, 0);  // extra field
        le16(cd, 0);  // comment
        le16(cd, 0);  // disk
        le16(cd, 0);  // internal attributes
        le32(cd, 0);  // external attributes
        le32(cd, uint32_t(e.offset));
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    if (cdAt + cd.size() + 22 > kMaxBytes) return false;
    const auto cdSize = uint32_t(cd.size());
    le32(cd, 0x06054B50);
    le16(cd, 0);  // disk
    le16(cd, 0);  // disk of the directory
    le16(cd, uint32_t(entries_.size()));
    le16(cd, uint32_t(entries_.size()));
    le32(cd, cdSize);
    le32(cd, uint32_t(cdAt));
    le16(cd, 0);  // comment
    return sink_.write(cd);
}

}  // namespace stym::ymio
