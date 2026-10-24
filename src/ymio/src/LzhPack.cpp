// STYM2149 — ymio: LHA -lh5- compression (compressed YM6 files, M7, A5, D-161)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Encoder written for STYM2149 from the format read by the ymio decoder (Lzh.cpp, port of pylzh) and by AtariAudio's
// (LZH code of Haruhiko Okumura and Kerwin F. Medina, third_party/AtariAudio/src/external/lzh.cpp); no code is taken
// from them:
//  - LZSS over the -lh5- window (DICBIT 13: distance of 1 to 8 192 bytes), copies of 3 to 256 bytes, search by hash
//    chains and lazy matching by one byte (longer copy at the next byte: literal first);
//  - static Huffman blocks of at most kBlockCodes codes: table of the lengths of the code table (19 codes, skip of 0
//    to 3 zeros after the third length), table of the 510 codes (bytes then copies), table of the 14 offset codes;
//    a single-symbol table is written "0 then that symbol" and its code takes no bits;
//  - canonical codes (increasing lengths, then increasing symbols) limited to 16 bits and complete: table-based
//    decoders (AtariAudio's make_table) refuse longer codes and incomplete tables;
//  - level 0 header (checksum, CRC-16 of the content) followed by the end-of-archive mark (zero byte), which
//    AtariAudio's decompressor counts in the file size.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <numeric>
#include <queue>
#include <utility>

#include "ymio/Lzh.h"

namespace stym::ymio {

namespace {

constexpr int kDicBits = 13;
constexpr uint32_t kWindow = 1u << kDicBits;  // 8 192
constexpr uint32_t kMinMatch = 3, kMaxMatch = 256;
constexpr size_t kNumCodes = 256 + kMaxMatch - kMinMatch + 1;  // 510
constexpr size_t kNumTemp = 16 + 3;                             // lengths 1 to 16 (+ 2) and three skip codes
constexpr size_t kNumOffsets = kDicBits + 1;                    // 14
constexpr int kTempBits = 5, kCodeBits = 9, kOffsetBits = 4;  // widths of the counts of the three tables
constexpr int kMaxCodeLength = 16;
constexpr size_t kBlockCodes = 16384;
constexpr uint32_t kFarMatch = 4096;  // 3-byte copy further away: costs more than three literals
constexpr int kHashBits = 15;         // hash of the next three bytes
constexpr int kMaxChain = 256;        // candidates examined at most per search

// Bits accumulate in acc_ (bits_ of them pending) and leave a byte at a time.
class BitWriter {
public:
    // n ≤ 24 bits, most significant first (as Lzh.cpp and AtariAudio read them).
    void put(int n, uint32_t v) {
        if (n == 0) return;
        acc_ = (acc_ << n) | (v & ((1u << n) - 1u));
        bits_ += n;
        while (bits_ >= 8) {
            bits_ -= 8;
            out_.push_back(uint8_t(acc_ >> bits_));
        }
        acc_ &= (uint64_t(1) << bits_) - 1u;
    }
    std::vector<uint8_t> finish() {
        if (bits_ > 0) put(8 - bits_, 0);
        return std::move(out_);
    }

private:
    std::vector<uint8_t> out_;
    uint64_t acc_ = 0;
    int bits_ = 0;
};

// Canonical codes: for each length, increasing symbols (HuffmanTree::build of Lzh.cpp, make_table).
// start[l] = (start[l − 1] + count[l − 1]) × 2: the first code of length l follows those of length l − 1.
std::vector<uint16_t> canonicalCodes(const std::vector<uint8_t>& len) {
    std::array<uint32_t, kMaxCodeLength + 2> count{}, start{};
    for (const uint8_t l : len)
        if (l != 0) ++count[l];
    for (int i = 1; i <= kMaxCodeLength; ++i) start[size_t(i + 1)] = (start[size_t(i)] + count[size_t(i)]) << 1;
    std::vector<uint16_t> code(len.size(), 0);
    for (size_t s = 0; s < len.size(); ++s)
        if (len[s] != 0) code[s] = uint16_t(start[len[s]]++);
    return code;
}

// Huffman table of one alphabet: length and code of each symbol (length 0: absent).
struct Table {
    std::vector<uint8_t> len;
    std::vector<uint16_t> code;
    int single = -1;  // a single symbol (or none: 0): written "0, symbol", with no bits per code
};

Table makeTable(std::span<const uint32_t> freq) {
    Table t;
    t.len = lzhHuffmanLengths(freq, kMaxCodeLength);
    t.code = canonicalCodes(t.len);
    if (std::all_of(t.len.begin(), t.len.end(), [](uint8_t l) { return l == 0; })) {
        const auto used = std::find_if(freq.begin(), freq.end(), [](uint32_t f) { return f != 0; });
        t.single = used == freq.end() ? 0 : int(used - freq.begin());
    }
    return t;
}

void putSymbol(BitWriter& w, const Table& t, size_t s) { w.put(t.len[s], t.code[s]); }

// Lengths of a "temp" or offset table: 3 bits, or 7 followed by as many 1s as bits beyond 7 then a 0; after the
// length of index special - 1, number of skipped zeros on 2 bits (up to index 6).
void writeLengths(BitWriter& w, const Table& t, int nbits, size_t special) {
    if (t.single >= 0) {
        w.put(nbits, 0);
        w.put(nbits, uint32_t(t.single));
        return;
    }
    size_t n = t.len.size();
    while (n > 0 && t.len[n - 1] == 0) --n;
    w.put(nbits, uint32_t(n));
    for (size_t i = 0; i < n;) {
        const int k = t.len[i++];
        if (k <= 6)
            w.put(3, uint32_t(k));
        else
            w.put(k - 3, (1u << (k - 3)) - 2u);
        if (i == special) {
            size_t j = i;
            while (j < n && j < 6 && t.len[j] == 0) ++j;
            w.put(2, uint32_t(j - i));
            i = j;
        }
    }
}

// Lengths of the code table as "temp" symbols: length k → k + 2; one or two zeros → 0 per zero; 3 to 18 zeros →
// 1 and 4 bits; 19 zeros → 0 then 1 and 4 bits; 20 zeros and more → 2 and 9 bits.
struct TempItem {
    uint8_t sym = 0;
    uint8_t extraBits = 0;
    uint16_t extra = 0;
};

size_t codeLengthItems(const std::vector<uint8_t>& cLen, std::vector<TempItem>& items) {
    size_t n = cLen.size();
    while (n > 0 && cLen[n - 1] == 0) --n;
    for (size_t i = 0; i < n;) {
        const uint8_t k = cLen[i++];
        if (k != 0) {
            items.push_back({uint8_t(k + 2), 0, 0});
            continue;
        }
        uint32_t count = 1;
        while (i < n && cLen[i] == 0) {
            ++i;
            ++count;
        }
        if (count <= 2) {
            for (uint32_t c = 0; c < count; ++c) items.push_back({0, 0, 0});
        } else if (count <= 18) {
            items.push_back({1, 4, uint16_t(count - 3)});
        } else if (count == 19) {
            items.push_back({0, 0, 0});
            items.push_back({1, 4, 15});
        } else {
            items.push_back({2, kCodeBits, uint16_t(count - 20)});
        }
    }
    return n;
}

// Code < 256: byte; otherwise copy of code - 253 bytes at distance offset + 1.
struct Token {
    uint16_t code = 0;
    uint16_t offset = 0;
};

// Offset symbol b = number of significant bits: offsets 0 and 1 are their own symbol; from b = 2, b − 1 extra bits
// follow it, offset = 2^(b − 1) + extra.
uint32_t offsetCode(uint32_t offset) { return uint32_t(std::bit_width(offset)); }

// One block: number of tokens, code table (sent through a temp table, or as a single symbol), offset table, then the
// tokens with their offsets. The tables are built from the frequencies of this block only.
void writeBlock(BitWriter& w, std::span<const Token> tokens) {
    std::vector<uint32_t> cFreq(kNumCodes, 0), pFreq(kNumOffsets, 0);
    for (const Token& t : tokens) {
        ++cFreq[t.code];
        if (t.code >= 256) ++pFreq[offsetCode(t.offset)];
    }
    w.put(16, uint32_t(tokens.size()));
    const Table c = makeTable(cFreq);
    if (c.single >= 0) {
        w.put(kTempBits, 0);
        w.put(kTempBits, 0);
        w.put(kCodeBits, 0);
        w.put(kCodeBits, uint32_t(c.single));
    } else {
        std::vector<TempItem> items;
        const size_t n = codeLengthItems(c.len, items);
        std::vector<uint32_t> tFreq(kNumTemp, 0);
        for (const TempItem& it : items) ++tFreq[it.sym];
        const Table t = makeTable(tFreq);
        writeLengths(w, t, kTempBits, 3);
        w.put(kCodeBits, uint32_t(n));
        for (const TempItem& it : items) {
            putSymbol(w, t, it.sym);
            w.put(it.extraBits, it.extra);
        }
    }
    const Table p = makeTable(pFreq);
    writeLengths(w, p, kOffsetBits, 0);
    for (const Token& t : tokens) {
        putSymbol(w, c, t.code);
        if (t.code < 256) continue;
        const uint32_t b = offsetCode(t.offset);
        putSymbol(w, p, b);
        if (b > 1) w.put(int(b) - 1, t.offset - (1u << (b - 1)));
    }
}

// LZSS match finder by hash chains: head_[h] is the most recent position whose next three bytes hash to h, and
// prev_[i mod window] the previous position with the same hash as i (-1: none).
class Matcher {
public:
    explicit Matcher(std::span<const uint8_t> d) : d_(d), head_(size_t(1) << kHashBits, -1), prev_(kWindow, -1) {}

    struct Match {
        uint32_t len = 0, dist = 0;
    };

    // Previous positions indexed before any search at `i`.
    void insert(size_t i) {
        if (i + kMinMatch > d_.size()) return;
        const size_t h = hash(i);
        prev_[i & (kWindow - 1)] = head_[h];
        head_[h] = int64_t(i);
    }

    // Longest earlier copy of the bytes at i within the window (at most kMaxChain candidates), or none (len 0).
    Match find(size_t i) const {
        Match best;
        if (i + kMinMatch > d_.size()) return best;
        const uint32_t limit = uint32_t(std::min<size_t>(kMaxMatch, d_.size() - i));
        int chain = kMaxChain;
        // prev_[cand] stays valid as long as cand + kWindow is not indexed, i.e. cand ≥ i - kWindow.
        for (int64_t cand = head_[hash(i)]; cand >= 0 && i - size_t(cand) <= kWindow && chain-- > 0;
             cand = prev_[size_t(cand) & (kWindow - 1)]) {
            const size_t c = size_t(cand);
            // Quick rejection: a candidate that differs at position best.len cannot be longer than the best.
            if (d_[c + best.len] != d_[i + best.len]) continue;
            uint32_t len = 0;
            while (len < limit && d_[c + len] == d_[i + len]) ++len;
            if (len > best.len) {
                best = {len, uint32_t(i - c)};
                if (len == limit) break;
            }
        }
        if (best.len < kMinMatch || (best.len == kMinMatch && best.dist > kFarMatch)) return {};
        return best;
    }

private:
    size_t hash(size_t i) const {
        return ((size_t(d_[i]) << 10) ^ (size_t(d_[i + 1]) << 5) ^ d_[i + 2]) & ((size_t(1) << kHashBits) - 1);
    }

    std::span<const uint8_t> d_;
    std::vector<int64_t> head_, prev_;
};

// Compressed stream of `data`: tokens gathered into blocks of kBlockCodes, the last block shorter.
std::vector<uint8_t> compress(std::span<const uint8_t> data) {
    BitWriter w;
    std::vector<Token> block;
    block.reserve(kBlockCodes);
    const auto emit = [&](Token t) {
        block.push_back(t);
        if (block.size() == kBlockCodes) {
            writeBlock(w, block);
            block.clear();
        }
    };
    Matcher m(data);
    size_t i = 0;
    Matcher::Match cur = m.find(0);
    while (i < data.size()) {
        m.insert(i);
        if (cur.len == 0) {
            emit({data[i], 0});
            cur = m.find(++i);
            continue;
        }
        // Lazy matching: if the next byte starts a longer copy, this byte goes out as a literal.
        const Matcher::Match next = cur.len < kMaxMatch ? m.find(i + 1) : Matcher::Match{};
        if (next.len > cur.len) {
            emit({data[i], 0});
            ++i;
            cur = next;
            continue;
        }
        emit({uint16_t(256 + cur.len - kMinMatch), uint16_t(cur.dist - 1)});
        // The bytes covered by the copy enter the hash chains too, for later searches.
        for (uint32_t k = 1; k < cur.len; ++k) m.insert(i + k);
        i += cur.len;
        cur = m.find(i);
    }
    if (!block.empty()) writeBlock(w, block);
    return w.finish();
}

void putLe(std::vector<uint8_t>& out, uint32_t v, int bytes) {
    for (int k = 0; k < bytes; ++k) out.push_back(uint8_t(v >> (8 * k)));
}

}  // namespace

std::vector<uint8_t> lzhHuffmanLengths(std::span<const uint32_t> freq, int maxBits) {
    const size_t n = freq.size();
    std::vector<uint8_t> len(n, 0);
    std::vector<uint32_t> used;
    for (size_t s = 0; s < n; ++s)
        if (freq[s] != 0) used.push_back(uint32_t(s));
    if (used.size() < 2) return len;

    // Huffman tree: leaves 0 to n - 1, internal nodes after (a parent always has a larger index).
    // At equal weights, the smallest index comes out first: deterministic result.
    using Item = std::pair<uint64_t, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;
    for (const uint32_t s : used) heap.push({freq[s], s});
    std::vector<int64_t> parent(n, -1);
    while (heap.size() > 1) {
        const Item a = heap.top();
        heap.pop();
        const Item b = heap.top();
        heap.pop();
        const uint32_t node = uint32_t(parent.size());
        parent.push_back(-1);
        parent[a.second] = node;
        parent[b.second] = node;
        heap.push({a.first + b.first, node});
    }
    std::vector<int> depth(parent.size(), 0);
    for (size_t i = parent.size(); i-- > 0;)
        if (parent[i] >= 0) depth[i] = depth[size_t(parent[i])] + 1;

    const int deepest = std::accumulate(used.begin(), used.end(), 0, [&](int m, uint32_t s) {
        return std::max(m, depth[s]);
    });
    if (deepest <= maxBits) {
        for (const uint32_t s : used) len[s] = uint8_t(depth[s]);
        return len;
    }
    // Too deep: the leaves beyond maxBits move up to maxBits, then the Kraft sum, now too large, goes back down one
    // unit at a time (one leaf of maxBits less, a higher leaf split one level lower); the lengths then go to the
    // symbols from the deepest to the shallowest.
    std::vector<uint32_t> count(size_t(maxBits) + 1, 0);
    for (const uint32_t s : used) ++count[size_t(std::min(depth[s], maxBits))];
    uint64_t kraft = 0;
    for (int l = 1; l <= maxBits; ++l) kraft += uint64_t(count[size_t(l)]) << (maxBits - l);
    for (; kraft > (uint64_t(1) << maxBits); --kraft) {
        --count[size_t(maxBits)];
        for (int l = maxBits - 1; l > 0; --l)
            if (count[size_t(l)] != 0) {
                --count[size_t(l)];
                count[size_t(l) + 1] += 2;
                break;
            }
    }
    std::stable_sort(used.begin(), used.end(), [&](uint32_t a, uint32_t b) {
        return depth[a] != depth[b] ? depth[a] > depth[b] : freq[a] < freq[b];
    });
    size_t next = 0;
    for (int l = maxBits; l > 0; --l)
        for (uint32_t k = 0; k < count[size_t(l)]; ++k) len[used[next++]] = uint8_t(l);
    return len;
}

// MS-DOS layout: date = (year − 1980) << 9 | month << 5 | day in the high word, time = hour << 11 | minute << 5 |
// second / 2 in the low word.
uint32_t lzhDosTime(int year, int month, int day, int hour, int minute, int second) noexcept {
    const uint32_t date = uint32_t(std::clamp(year, 1980, 2107) - 1980) << 9 |
                          uint32_t(std::clamp(month, 1, 12)) << 5 | uint32_t(std::clamp(day, 1, 31));
    const uint32_t time = uint32_t(std::clamp(hour, 0, 23)) << 11 | uint32_t(std::clamp(minute, 0, 59)) << 5 |
                          uint32_t(std::clamp(second, 0, 59) / 2);
    return date << 16 | time;
}

std::vector<uint8_t> packLzh(std::string_view name, std::span<const uint8_t> data, uint32_t dosTime) {
    if (data.size() > UINT32_MAX) return {};
    const std::string_view stored = name.substr(0, kLzhMaxName);
    const std::vector<uint8_t> packed = compress(data);
    if (packed.size() > UINT32_MAX) return {};
    std::vector<uint8_t> out;
    out.reserve(24 + stored.size() + packed.size() + 1);
    out.push_back(uint8_t(22 + stored.size()));  // header bytes after the first two
    out.push_back(0);                            // checksum, below
    for (const char ch : std::string_view("-lh5-")) out.push_back(uint8_t(ch));
    putLe(out, uint32_t(packed.size()), 4);
    putLe(out, uint32_t(data.size()), 4);
    putLe(out, dosTime, 4);
    out.push_back(0x20);  // MS-DOS "archive" attribute
    out.push_back(0);     // header level
    out.push_back(uint8_t(stored.size()));
    out.insert(out.end(), stored.begin(), stored.end());
    putLe(out, lzhCrc16(data), 2);
    // Header checksum: sum modulo 256 of the header bytes after the first two.
    uint8_t sum = 0;
    for (size_t i = 2; i < out.size(); ++i) sum = uint8_t(sum + out[i]);
    out[1] = sum;
    out.insert(out.end(), packed.begin(), packed.end());
    out.push_back(0);  // end of archive
    return out;
}

}  // namespace stym::ymio
