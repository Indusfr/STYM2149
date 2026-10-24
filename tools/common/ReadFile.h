// STYM2149 — development tools and tests
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <vector>

namespace stym::tools {

// Reads a whole file. Empty vector if the file is missing or empty.
std::vector<uint8_t> readFile(const char* path);

}  // namespace stym::tools
