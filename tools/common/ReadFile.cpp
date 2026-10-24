// STYM2149 — development tools and tests
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ReadFile.h"

#include <cstdio>

namespace stym::tools {

std::vector<uint8_t> readFile(const char* path) {
    std::vector<uint8_t> data;
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return data;
    uint8_t buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

}  // namespace stym::tools
