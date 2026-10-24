// STYM2149 — ymio
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Reading Arnaud Carré's YM files (formats YM2!, YM3!, YM3b, YM5!, YM6!), from the format documentation
// (ST-Sound, "YM file format") and the AtariAudio 1.26 player (YmRenderer.cpp, MIT licence).
// LHA-compressed files are decompressed by parseYm (Lzh.h, D-043).
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ymcore/ChipConfig.h"

namespace stym::ymio {

// Mark at the start of the file. YM2 comes from Mad Max's player (with its own digidrum bank), YM3b is a YM3 with a
// loop frame, YM5 and YM6 have a header, digidrums and timer effects.
enum class YmFormat : uint8_t { YM2, YM3, YM3b, YM5, YM6 };

// The four-character mark ("YM6!"...).
const char* formatName(YmFormat format) noexcept;

// A parsed tune, read-only once built (shared by the YM Player between the message and audio threads).
struct YmFile {
    static constexpr unsigned kStride = 16;  // registers per frame after deinterleaving (YM2/3: 14 used)

    YmFormat format = YmFormat::YM3;
    uint32_t frameCount = 0;
    uint32_t loopFrame = 0;   // frame played after the last one (0 if the file's value is out of range)
    uint32_t attributes = 0;  // YM5/6: bit 0 interleaved, bit 1 signed samples, bit 2 4-bit samples
    ymcore::MasterClock clock = ymcore::clocks::kDefault;  // YM2/3: implicit 2 MHz
    uint16_t playerRate = 50;                               // frames per second; YM2/3: implicit 50 Hz
    std::string title, author, comment;
    std::vector<std::vector<uint8_t>> digidrums;  // YM5/6, played by YmPlayer (D-044)
    std::vector<uint8_t> frames;                  // frameCount × kStride, frame by frame

    uint8_t reg(uint32_t frame, unsigned r) const noexcept { return frames[size_t(frame) * kStride + r]; }
};

struct YmParseResult {
    std::optional<YmFile> file;
    std::string error;  // empty if file is present
};

// Parses a YM file, LHA-compressed or not. All reads are bounded.
YmParseResult parseYm(std::span<const uint8_t> data);

}  // namespace stym::ymio
