// STYM2149 — ymio: uncompressed ZIP archive of the stems
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The prototype's zipStore: stored entries (method 0), local headers, central directory and end of directory,
// without ZIP64. Entries are written as the render goes: the local header is completed at the end of the entry (seek
// back), without a data descriptor. An entry begins with a fixed-size prefix written at the end (the header of a
// WAV, whose sizes are only known at the end); the CRC of the whole is recombined from those of the prefix and the
// body (crc32Combine). Deviations: valid entry date (1 January 1980; the prototype wrote zeros), names flagged UTF-8
// (bit 11).
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ymio/ByteSink.h"

namespace stym::ymio {

// ZIP CRC-32 (polynomial 0xEDB88320), continued from `crc`.
uint32_t crc32(std::span<const uint8_t> bytes, uint32_t crc = 0) noexcept;
// CRC of A followed by B, from those of A and B and the length of B.
uint32_t crc32Combine(uint32_t crcA, uint32_t crcB, uint64_t lengthB) noexcept;

// Usage: begin(), write() the body in pieces, end() with the prefix; repeat per entry, then finish(). One entry open at
// a time; the sink must outlive the writer and is committed by the caller. At most 65 535 entries.
class ZipWriter {
public:
    // Archive size that the format without ZIP64 can describe.
    static constexpr uint64_t kMaxBytes = 0xFFFFFFFFu;

    explicit ZipWriter(ByteSink& sink) : sink_(sink) {}
    // New entry: provisional local header, then `prefixBytes` reserved bytes. Name in UTF-8.
    bool begin(const std::string& name, size_t prefixBytes = 0);
    // Appends to the body of the open entry (its CRC is updated as it goes).
    bool write(std::span<const uint8_t> body);
    // End of the entry: prefix written in its place, CRC and sizes completed.
    bool end(std::span<const uint8_t> prefix = {});
    // Central directory and end of directory.
    bool finish();
    // Bytes written since the start of the archive.
    uint64_t bytes() const noexcept { return sink_.position(); }

private:
    // Kept for the central directory; offset = position of the local header.
    struct Entry {
        std::string name;
        uint32_t crc = 0;
        uint64_t size = 0, offset = 0;
    };
    ByteSink& sink_;
    std::vector<Entry> entries_;
    size_t prefix_ = 0;     // bytes reserved for the prefix of the open entry
    uint32_t bodyCrc_ = 0;  // CRC of the body written so far (prefix excluded)
    uint64_t bodySize_ = 0;
    bool open_ = false;
};

}  // namespace stym::ymio
