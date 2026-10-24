// STYM2149 — ymio: decompression of LZH / LHA archives
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// C++ port of pylzh, © 2026 Hirofumi Ukawa, Apache-2.0 licence (third_party/licenses/pylzh-LICENSE.txt),
// commit 37727d97747e13a23b05c8fbbd55c5685ee6be3f: headers.py, crc.py, decompress/lzhuf.py, huffman.py, bitio.py.
// Modifications relative to pylzh (Apache-2.0, §4 b):
//  - rewritten in C++, reading from an in-memory buffer instead of a stream; first file of the archive only
//    (the directory entries that precede it are skipped);
//  - compressed stream read up to the end of the buffer rather than up to the announced compressed size: this is how
//    AtariAudio tolerates YM file headers whose size is wrong; the CRC checks the result;
//  - safeguards against corrupt data: code length ≤ 32, offset ≤ 24 bits, size ≤ 64 MiB, truncated headers
//    refused;
//  - AtariAudio's tolerance of level 0 headers whose size byte is wrong: if decoding fails, new attempt with the data
//    placed just after the header CRC;
//  - method -lh1- (dynamic Huffman) and level 3 headers not ported: absent from known YM files
//    (ym2149-rs corpus: 1 962 archives, all -lh5- level 0).
#include "ymio/Lzh.h"

#include <algorithm>
#include <array>

namespace stym::ymio {

namespace {

constexpr uint64_t kMaxOriginalSize = uint64_t(64) << 20;  // a YM file is at most a few MiB

const std::array<uint16_t, 256>& crcTable() {
    static const std::array<uint16_t, 256> table = [] {
        std::array<uint16_t, 256> t{};
        for (unsigned i = 0; i < 256; ++i) {
            uint16_t d = uint16_t(i);
            for (int k = 0; k < 8; ++k) d = (d & 1u) ? uint16_t((d >> 1) ^ 0xA001u) : uint16_t(d >> 1);
            t[i] = d;
        }
        return t;
    }();
    return table;
}

uint16_t le16(const uint8_t* p) noexcept { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) noexcept {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t le64(const uint8_t* p) noexcept { return uint64_t(le32(p)) | (uint64_t(le32(p + 4)) << 32); }

// Bit reader, most significant first (bitio.py), bounded at the end of the buffer.
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) noexcept : p_(data), size_(size) {}

    // n ≤ 24 bits. -1 at the end of the data.
    int32_t readBits(int n) noexcept {
        if (n == 0) return 0;
        while (bits_ < n) {
            if (pos_ >= size_) return -1;
            buf_ = (buf_ << 8) | p_[pos_++];
            bits_ += 8;
        }
        bits_ -= n;
        const uint32_t res = (buf_ >> bits_) & ((1u << n) - 1u);
        buf_ &= (1u << bits_) - 1u;
        return int32_t(res);
    }
    int32_t readBit() noexcept { return readBits(1); }

private:
    const uint8_t* p_;
    size_t size_;
    size_t pos_ = 0;
    uint32_t buf_ = 0;  // the bits_ bits loaded but not consumed yet, in its low bits
    int bits_ = 0;
};

// Huffman tree as an array (huffman.py): a node holds the index of its two children, or a symbol | kLeaf.
// A node's children are consecutive: bit 0 leads to the stored index, bit 1 to the next one. build() assigns the
// codes breadth first: for each length, the free nodes of the previous depth become internal nodes with two free
// children, then the symbols of that length take the free nodes in symbol order (canonical code).
constexpr uint16_t kLeaf = 0x8000;

class HuffmanTree {
public:
    void reset(size_t size) {
        tree_.assign(size, kLeaf);
        allocated_ = 1;
        next_ = 0;
    }
    // Table of a single symbol: the root is a leaf, decoding reads no bit.
    void setSingle(uint32_t code) { tree_[0] = uint16_t(code | kLeaf); }

    // lengths[symbol] = code length in bits, 0 = absent; size = nodes available.
    void build(size_t size, const std::vector<uint8_t>& lengths) {
        reset(size);
        for (unsigned len = 1;; ++len) {
            expandQueue();
            if (!addCodesWithLength(lengths, len)) break;
        }
    }

    int32_t decode(BitReader& reader) const noexcept {
        uint32_t code = tree_[0];
        while (!(code & kLeaf)) {
            const int32_t bit = reader.readBit();
            if (bit < 0) return -1;
            const size_t index = code + uint32_t(bit);
            if (index >= tree_.size()) return -1;  // impossible by construction: safeguard
            code = tree_[index];
        }
        return int32_t(code & 0x7fffu);
    }

private:
    // Free nodes are next_ to allocated_ - 1. Each becomes an internal node pointing at two new free nodes appended
    // at allocated_; nothing happens if the array has no room left for them.
    void expandQueue() {
        const size_t newNodes = (allocated_ - next_) * 2;
        if (allocated_ + newNodes > tree_.size()) return;
        const size_t end = allocated_;
        while (next_ < end) {
            tree_[next_] = uint16_t(allocated_);
            allocated_ += 2;
            ++next_;
        }
    }
    // Next free node; 0 (the root) when none is left: an over-full length table overwrites the root instead of
    // writing past the allocated nodes.
    size_t readNextEntry() noexcept { return next_ >= allocated_ ? 0 : next_++; }
    bool addCodesWithLength(const std::vector<uint8_t>& lengths, unsigned len) {
        bool remaining = false;
        for (size_t i = 0; i < lengths.size(); ++i) {
            if (lengths[i] == len)
                tree_[readNextEntry()] = uint16_t(i | kLeaf);
            else if (lengths[i] > len)
                remaining = true;
        }
        return remaining;
    }

    std::vector<uint16_t> tree_;
    size_t allocated_ = 1, next_ = 0;
};

// -lh4- to -lh7- decoder: LZSS and static Huffman by blocks (lzhuf.py, LHNewDecompressor).
// Each block starts with its number of codes (16 bits) and three tables: the "temp" table (lengths of the codes used
// to send the next table), the code table (510 symbols: bytes 0-255, then copies of 3 to 256 bytes) and the offset
// table. A table whose count is 0 is followed by its only symbol. A copy reads its offset after its code and repeats
// bytes from the history ring.
class LhNewDecoder {
public:
    LhNewDecoder(const uint8_t* data, size_t size, int historyBits, int offsetBits)
        : reader_(data, size), offsetBits_(offsetBits), maxOffsetCodes_((1u << offsetBits) - 1u),
          ring_(size_t(1) << historyBits, uint8_t(' ')) {}

    bool decompress(uint64_t expected, std::vector<uint8_t>& out, std::string& error) {
        out.clear();
        out.reserve(size_t(expected));
        const size_t mask = ring_.size() - 1;
        size_t ringPos = 0;
        while (out.size() < expected) {
            while (blockRemaining_ == 0) {
                if (!startNewBlock()) {
                    error = "premature end of the LZH stream after " + std::to_string(out.size()) + " bytes of " +
                            std::to_string(expected);
                    return false;
                }
            }
            --blockRemaining_;
            const int32_t code = codeTree_.decode(reader_);
            if (code < 0) {
                error = "unreadable symbol after " + std::to_string(out.size()) + " bytes";
                return false;
            }
            if (code < 256) {
                const uint8_t b = uint8_t(code);
                out.push_back(b);
                ring_[ringPos] = b;
                ringPos = (ringPos + 1) & mask;
            } else {
                const int32_t count = code - 256 + kCopyThreshold;
                const int64_t offset = readOffsetCode();
                if (offset < 0) {
                    error = "unreadable offset after " + std::to_string(out.size()) + " bytes";
                    return false;
                }
                // (ringPos - offset - 1) modulo the ring size, a power of 2 (same result as pylzh).
                const size_t start = size_t(uint64_t(ringPos) - uint64_t(offset) - 1u) & mask;
                for (int32_t i = 0; i < count && out.size() < expected; ++i) {
                    const uint8_t b = ring_[(start + size_t(i)) & mask];
                    out.push_back(b);
                    ring_[ringPos] = b;
                    ringPos = (ringPos + 1) & mask;
                }
            }
        }
        return true;
    }

private:
    static constexpr int32_t kCopyThreshold = 3;
    static constexpr int kTempCodeBits = 5;
    static constexpr uint32_t kMaxTempCodes = (1u << kTempCodeBits) - 1u;  // 31
    static constexpr uint32_t kNumCodes = 510;
    static constexpr int32_t kMaxCodeLength = 32;  // safeguard: LHA uses no more than 16 bits

    // Code length in the temp and offset tables: 3 bits; 7 is followed by one more unit per 1 bit, up to a 0 bit.
    int32_t readLengthValue() noexcept {
        int32_t length = reader_.readBits(3);
        if (length < 0) return -1;
        if (length == 7) {
            for (;;) {
                const int32_t bit = reader_.readBit();
                if (bit < 0) return -1;
                if (bit == 0) break;
                if (++length > kMaxCodeLength) return -1;
            }
        }
        return length;
    }

    bool readTempTable() {
        int32_t n = reader_.readBits(kTempCodeBits);
        if (n < 0) return false;
        if (n == 0) {
            const int32_t code = reader_.readBits(5);
            if (code < 0) return false;
            tempTree_.reset(kMaxTempCodes * 2);
            tempTree_.setSingle(uint32_t(code));
            return true;
        }
        n = std::min<int32_t>(n, int32_t(kMaxTempCodes));
        std::vector<uint8_t> lengths(size_t(n), 0);
        for (int32_t i = 0; i < n; ++i) {
            const int32_t length = readLengthValue();
            if (length < 0) return false;
            lengths[size_t(i)] = uint8_t(length);
            // After the third length, 2 bits give how many of the following lengths are 0.
            if (i == 2) {
                const int32_t skip = reader_.readBits(2);
                if (skip < 0) return false;
                for (int32_t k = 0; k < skip; ++k)
                    if (++i < n) lengths[size_t(i)] = 0;
            }
        }
        tempTree_.build(kMaxTempCodes * 2, lengths);
        return true;
    }

    // Run of zero lengths in the code table, from temp symbols 0 to 2: one zero, 3 to 18 zeros (4 bits) or 20 to 531
    // zeros (9 bits).
    int32_t readSkipCount(int32_t skipRange) noexcept {
        if (skipRange == 0) return 1;
        if (skipRange == 1) {
            const int32_t res = reader_.readBits(4);
            return res < 0 ? -1 : res + 3;
        }
        const int32_t res = reader_.readBits(9);
        return res < 0 ? -1 : res + 20;
    }

    // Lengths of the 510 codes, sent as temp symbols: 0 to 2 are runs of zeros, k ≥ 3 is the length k − 2.
    bool readCodeTable() {
        int32_t n = reader_.readBits(9);
        if (n < 0) return false;
        if (n == 0) {
            const int32_t code = reader_.readBits(9);
            if (code < 0) return false;
            codeTree_.reset(kNumCodes * 2);
            codeTree_.setSingle(uint32_t(code));
            return true;
        }
        n = std::min<int32_t>(n, int32_t(kNumCodes));
        std::vector<uint8_t> lengths(size_t(n), 0);
        for (int32_t i = 0; i < n;) {
            const int32_t code = tempTree_.decode(reader_);
            if (code < 0) return false;
            if (code <= 2) {
                const int32_t skip = readSkipCount(code);
                if (skip < 0) return false;
                for (int32_t k = 0; k < skip && i < n; ++k) lengths[size_t(i++)] = 0;
            } else {
                lengths[size_t(i++)] = uint8_t(code - 2);
            }
        }
        codeTree_.build(kNumCodes * 2, lengths);
        return true;
    }

    bool readOffsetTable() {
        int32_t n = reader_.readBits(offsetBits_);
        if (n < 0) return false;
        if (n == 0) {
            const int32_t code = reader_.readBits(offsetBits_);
            if (code < 0) return false;
            offsetTree_.reset(maxOffsetCodes_ * 2);
            offsetTree_.setSingle(uint32_t(code));
            return true;
        }
        n = std::min<int32_t>(n, int32_t(maxOffsetCodes_));
        std::vector<uint8_t> lengths(size_t(n), 0);
        for (int32_t i = 0; i < n; ++i) {
            const int32_t length = readLengthValue();
            if (length < 0) return false;
            lengths[size_t(i)] = uint8_t(length);
        }
        offsetTree_.build(maxOffsetCodes_ * 2, lengths);
        return true;
    }

    bool startNewBlock() {
        const int32_t length = reader_.readBits(16);
        if (length < 0) return false;
        blockRemaining_ = uint32_t(length);
        return readTempTable() && readCodeTable() && readOffsetTable();
    }

    // Offset symbol b: 0 and 1 are the offset itself; from 2, b − 1 more bits follow and the offset is
    // 2^(b − 1) + those bits. The copy starts offset + 1 bytes back.
    int64_t readOffsetCode() noexcept {
        const int32_t bits = offsetTree_.decode(reader_);
        if (bits < 0) return -1;
        if (bits <= 1) return bits;
        if (bits - 1 > 24) return -1;  // safeguard: -lh7- uses no more than 16 bits
        const int32_t res = reader_.readBits(bits - 1);
        return res < 0 ? -1 : int64_t(res) + (int64_t(1) << (bits - 1));
    }

    BitReader reader_;
    int offsetBits_;             // width of the offset table's count
    uint32_t maxOffsetCodes_;
    std::vector<uint8_t> ring_;  // history (dictionary), filled with spaces at the start
    uint32_t blockRemaining_ = 0;
    HuffmanTree tempTree_, codeTree_, offsetTree_;
};

// What the decoder needs from the header of one archive member.
struct Header {
    int level = 0;
    std::string method;
    uint64_t packedSize = 0;
    uint64_t originalSize = 0;
    uint16_t crc = 0;       // CRC-16 of the original data
    size_t dataOffset = 0;  // start of the compressed data in the archive
    std::string name;
};

// Extended headers of levels 1 and 2 (headers.py, _decode_extended_headers). pos points at the first header; on
// exit, just after the last "next size" field. Returns the sum of the sizes read, or -1.
// Each one: type byte, body, then the 16-bit size of the next one (0: last). Type $01 holds the file name, $42 the
// 64-bit compressed and original sizes; the others are skipped.
int64_t readExtendedHeaders(std::span<const uint8_t> d, size_t& pos, uint16_t size, Header& h) {
    int64_t total = 0;
    while (size != 0) {
        if (size < 3 || pos + size > d.size()) return -1;  // type + next size at least
        total += size;
        const uint8_t type = d[pos];
        const uint8_t* body = d.data() + pos + 1;
        const size_t bodySize = size_t(size) - 3;
        if (type == 0x01) {
            const uint8_t* end = std::find(body, body + bodySize, uint8_t(0));
            h.name.assign(reinterpret_cast<const char*>(body), size_t(end - body));
        } else if (type == 0x42 && bodySize >= 16) {
            h.packedSize = le64(body);
            h.originalSize = le64(body + 8);
        }
        pos += size_t(size) - 2;
        size = le16(d.data() + pos);
        pos += 2;
    }
    return total;
}

// Common fields: method at byte 2, compressed size at 7, original size at 11 (little-endian), level at 20.
// Levels 0 and 1: byte 0 = header size minus 2, name length at 21, name, then the CRC; level 1 adds a system byte and
// extended headers. Level 2: bytes 0-1 = whole header size, CRC at 21, size of the first extended header at 24.
bool readHeader(std::span<const uint8_t> d, Header& h, std::string& error) {
    if (d.size() < 22) {
        error = "truncated LHA header";
        return false;
    }
    h.level = d[20];
    h.method.assign(reinterpret_cast<const char*>(d.data() + 2), 5);
    h.packedSize = le32(d.data() + 7);
    h.originalSize = le32(d.data() + 11);
    if (h.level == 0 || h.level == 1) {
        const size_t headerSize = d[0];
        const size_t nameLen = d[21];
        const size_t readSoFar = 22 + nameLen + 2;
        if (readSoFar > d.size()) {
            error = "truncated LHA header";
            return false;
        }
        h.name.assign(reinterpret_cast<const char*>(d.data() + 22), nameLen);
        h.crc = le16(d.data() + 22 + nameLen);
        if (h.level == 0) {
            h.dataOffset = std::max(headerSize + 2, readSoFar);
        } else {
            // End of the base header (system type), then chain of extended headers whose size is counted in the
            // compressed size.
            size_t pos = std::max(headerSize, readSoFar);
            if (pos + 2 > d.size()) {
                error = "truncated LHA header";
                return false;
            }
            const uint16_t next = le16(d.data() + pos);
            pos += 2;
            const int64_t ext = readExtendedHeaders(d, pos, next, h);
            if (ext < 0) {
                error = "invalid or truncated LHA extended header";
                return false;
            }
            h.packedSize -= std::min<uint64_t>(h.packedSize, uint64_t(ext));
            h.dataOffset = pos;
        }
    } else if (h.level == 2) {
        if (d.size() < 26) {
            error = "truncated LHA header";
            return false;
        }
        h.crc = le16(d.data() + 21);
        size_t pos = 26;
        if (readExtendedHeaders(d, pos, le16(d.data() + 24), h) < 0) {
            error = "invalid or truncated LHA extended header";
            return false;
        }
        h.dataOffset = le16(d.data());
    } else {
        error = "LHA header level " + std::to_string(h.level) + " not supported";
        return false;
    }
    if (h.dataOffset > d.size()) {
        error = "truncated LHA archive";
        return false;
    }
    return true;
}

// Decompresses a file whose header has been read, and checks its CRC. False with result.error filled on failure.
bool decodeMember(std::span<const uint8_t> archive, const Header& h, LzhResult& result) {
    result.data.clear();
    result.error.clear();
    const uint8_t* data = archive.data() + h.dataOffset;
    const size_t available = archive.size() - h.dataOffset;
    // History of 2^historyBits bytes, offset table count on offsetBits bits; 0: stored data (-lh0-), no decoder.
    int historyBits = 0, offsetBits = 0;
    if (h.method == "-lh0-") {
        if (h.originalSize > available) {
            result.error = "truncated LHA archive";
            return false;
        }
        result.data.assign(data, data + h.originalSize);
    } else if (h.method == "-lh4-" || h.method == "-lh5-") {
        historyBits = 14;  // like pylzh and Lhasa: the -lh5- ring is enough for -lh4-
        offsetBits = 4;
    } else if (h.method == "-lh6-") {
        historyBits = 16;
        offsetBits = 5;
    } else if (h.method == "-lh7-") {
        historyBits = 17;
        offsetBits = 5;
    } else {
        result.error = "LHA method " + h.method + " not supported";
        return false;
    }
    if (historyBits != 0) {
        LhNewDecoder decoder(data, available, historyBits, offsetBits);
        if (!decoder.decompress(h.originalSize, result.data, result.error)) {
            result.data.clear();
            return false;
        }
    }
    if (lzhCrc16(result.data) != h.crc) {
        result.error = "wrong CRC after LHA decompression";
        result.data.clear();
        return false;
    }
    return true;
}

}  // namespace

uint16_t lzhCrc16(std::span<const uint8_t> data, uint16_t init) noexcept {
    const auto& table = crcTable();
    uint16_t crc = init;
    for (uint8_t b : data) crc = uint16_t((crc >> 8) ^ table[(crc ^ b) & 0xffu]);
    return crc;
}

bool isLzh(std::span<const uint8_t> data) noexcept {
    return data.size() >= 22 && data[2] == '-' && data[3] == 'l' && data[4] == 'h' && data[6] == '-' && data[20] <= 2;
}

LzhResult unpackLzh(std::span<const uint8_t> whole) {
    LzhResult result;
    Header h;
    // First file of the archive: directory entries (-lhd-) are skipped, as pylzh does when listing them.
    std::span<const uint8_t> archive = whole;
    for (;;) {
        if (archive.empty() || archive[0] == 0) {
            result.error = "LHA archive without a file";
            return result;
        }
        if (!isLzh(archive)) {
            result.error = "not an LHA archive";
            return result;
        }
        h = Header{};
        if (!readHeader(archive, h, result.error)) return result;
        if (h.method != "-lhd-") break;
        if (h.packedSize > archive.size() - h.dataOffset) {
            result.error = "truncated LHA archive";
            return result;
        }
        archive = archive.subspan(h.dataOffset + size_t(h.packedSize));
    }
    result.name = h.name;
    result.method = h.method;
    if (h.originalSize > kMaxOriginalSize) {
        result.error = "announced decompressed size too large (" + std::to_string(h.originalSize) + " bytes)";
        return result;
    }
    if (decodeMember(archive, h, result)) return result;
    // AtariAudio's tolerance: some YM files have a wrong level 0 header size byte (ym2149-rs corpus: 20 archives out
    // of 1 962, size 120). AtariAudio always places the data just after the CRC.
    const size_t afterCrc = 22 + h.name.size() + 2;
    if (h.level == 0 && h.dataOffset != afterCrc && afterCrc <= archive.size()) {
        LzhResult retry;
        retry.name = result.name;
        retry.method = result.method;
        h.dataOffset = afterCrc;
        if (decodeMember(archive, h, retry)) return retry;
    }
    return result;
}

}  // namespace stym::ymio
