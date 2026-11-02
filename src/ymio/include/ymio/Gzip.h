// STYM2149 — ymio: gzip members (YM-Tracker songs)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Reading: a gzip member (RFC 1952: header with its optional fields, deflate data, CRC-32 and length checked) and
// raw deflate streams (RFC 1951: stored, fixed and dynamic Huffman blocks), decoded as the RFC describes them; the
// output is bounded so that a small file cannot unpack without end. Writing: a gzip member of stored blocks (no
// compression), which every gzip reader accepts.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stym::ymio {

// The data of a deflate stream, or nullopt if it is cut, invalid or longer than maxBytes once decoded; `used` takes the
// bytes of `in` that the stream took.
std::optional<std::vector<uint8_t>> inflate(std::span<const uint8_t> in, size_t maxBytes, size_t* used = nullptr);

// The data of the first gzip member of `file`, or the reason it cannot be read.
struct GunzipResult {
    std::optional<std::vector<uint8_t>> data;
    std::string error;
};
GunzipResult gunzip(std::span<const uint8_t> file, size_t maxBytes);

// A gzip member holding `data` in stored blocks: no name, time 0, operating system "unknown" (255).
std::vector<uint8_t> gzipStore(std::span<const uint8_t> data);

}  // namespace stym::ymio
