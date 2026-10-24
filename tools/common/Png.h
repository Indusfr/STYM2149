// STYM2149 — tools: reading and writing PNG images (interface captures, visual regression test)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Just what the captures need: 8-bit-per-channel PNG, RGB or RGBA, non-interlaced, for reading (complete inflate of
// RFC 1951); RGB for writing (deflate with fixed Huffman codes, greedy match search).
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace stym::tools {

struct Image {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;  // 0xAARRGGBB, row by row

    uint32_t at(int x, int y) const noexcept { return pixels[size_t(y) * size_t(width) + size_t(x)]; }
};

// Decodes a PNG; on failure, returns an empty image and puts the reason in *error.
Image decodePng(std::span<const uint8_t> data, std::string* error = nullptr);
// Encodes to RGB PNG (the alpha is ignored).
std::vector<uint8_t> encodePng(const Image& image);

bool readPng(const std::string& path, Image& out, std::string* error = nullptr);
bool writePng(const std::string& path, const Image& image);

}  // namespace stym::tools
