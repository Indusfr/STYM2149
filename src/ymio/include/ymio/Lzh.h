// STYM2149 — ymio: LZH / LHA archives (compressed YM files): -lh5- decompression and compression
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// C++ port of pylzh (© 2026 Hirofumi Ukawa, Apache-2.0 licence, commit 37727d9): level 0, 1 and 2 headers
// (headers.py), methods -lh0-, -lh4- to -lh7- (decompress/lzhuf.py, huffman.py, bitio.py) and CRC-16 (crc.py).
// Original licence: third_party/licenses/pylzh-LICENSE.txt. Modifications: see Lzh.cpp.
// The compression (packLzh, LzhPack.cpp) is written for STYM2149 from this format.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stym::ymio {

// CRC-16 of LHA archives (polynomial $A001, reflected).
uint16_t lzhCrc16(std::span<const uint8_t> data, uint16_t init = 0) noexcept;

// Recognised LHA header: method "-lh?-" at byte 2 and header level 0, 1 or 2 at byte 20.
bool isLzh(std::span<const uint8_t> data) noexcept;

// Result of unpackLzh; data is meaningful only when error is empty.
struct LzhResult {
    std::vector<uint8_t> data;  // content of the first file of the archive
    std::string name;           // its name, as stored (no character set conversion)
    std::string method;         // "-lh5-", etc.
    std::string error;          // empty on success
};

// Decompresses the first file of the archive and checks its CRC. All reads are bounded; a truncated or corrupt
// archive, or one with an unsupported method (-lh1-, header level 3), gives an error, never an out-of-bounds read.
LzhResult unpackLzh(std::span<const uint8_t> archive);

// MS-DOS date and time of a level 0 header (local time, to within 2 seconds; out-of-range values clamped).
uint32_t lzhDosTime(int year, int month, int day, int hour, int minute, int second) noexcept;

// Stored name: at most 233 bytes (the level 0 header fits in 255 bytes), cut beyond.
inline constexpr size_t kLzhMaxName = 233;

// Single-file LHA archive, compressed with -lh5- (the method of YM files, read by ST-Sound and AtariAudio): level 0
// header, "archive" attribute, CRC-16, then end-of-archive mark. Empty if `data` exceeds 4 GiB.
std::vector<uint8_t> packLzh(std::string_view name, std::span<const uint8_t> data,
                             uint32_t dosTime = lzhDosTime(1980, 1, 1, 0, 0, 0));

// Huffman code lengths (0: absent symbol) limited to maxBits, complete code; none if fewer than two symbols are
// present. Exposed for the packLzh tests.
std::vector<uint8_t> lzhHuffmanLengths(std::span<const uint32_t> freq, int maxBits);

}  // namespace stym::ymio
