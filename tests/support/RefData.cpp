// STYM2149 — reference tunes
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "RefData.h"

#include <filesystem>

#include "ReadFile.h"
#include "Metrics.h"

namespace stym::test {

std::string refDataDir() {
    std::string dir = env("STYM_REFDATA");
#ifdef STYM_REFDATA_DEFAULT
    if (dir.empty()) dir = STYM_REFDATA_DEFAULT;
#endif
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(std::filesystem::path(dir) / "ym", ec)) return {};
    return dir;
}

bool refDataRequired() {
    const std::string v = env("STYM_REQUIRE_REFDATA");
    return !v.empty() && v != "0";
}

std::vector<uint8_t> loadRefTune(const std::string& file) {
    const std::string dir = refDataDir();
    if (dir.empty()) return {};
    const std::filesystem::path p = std::filesystem::path(dir) / "ym" / file;
    return tools::readFile(p.string().c_str());
}

std::vector<uint8_t> loadRefSndh(const std::string& file) {
    const std::string dir = refDataDir();
    if (dir.empty()) return {};
    const std::filesystem::path p = std::filesystem::path(dir) / "sndh" / file;
    return tools::readFile(p.string().c_str());
}

}  // namespace stym::test
