// STYM2149 — ymio: reading MOD modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// ProTracker MOD format, from its public description (the ProTracker 2.x documentation, "MOD format" texts): title
// (20 bytes), 31 sample headers, song length, restart, order table (128), a four-letter signature that gives the
// channels ("M.K.", "FLT4", "6CHN", "8CHN", "16CH"…), 64-row patterns of 4-byte cells (Amiga period, sample,
// effect), then the signed 8-bit samples. The older 15-sample Soundtracker modules (no signature) are read too, when
// their header holds together. The module comes out in the shape of an XM module (ymio/Xm.h), so that the XM import
// converts it: notes from the periods (period 428, ProTracker's C-2, is the XM C-4), one instrument per
// sample, the effects with their XM numbers (the same, E5x turned to the XM fine tune), the Amiga pan of the channels
// (L R R L) and the PAL rate of period 428 as the rate of C-4. Bounded reading: a cut or inconsistent file is refused,
// except the data of the last sample, kept up to the end of the file and flagged.
#pragma once
#include <cstdint>
#include <span>

#include "ymio/Xm.h"

namespace stym::ymio {

inline constexpr int kModMaxChannels = 32;
inline constexpr double kAmigaPalClock = 3546895;  // Paula's clock on a PAL Amiga: rate = clock / period
inline constexpr int kModC2Period = 428;            // ProTracker's C-2, the XM C-4

// Reads a whole MOD file held in memory into the XM shape (XmParseResult::error: "Not a MOD module." or
// "Corrupt MOD module."). Pure function, no shared state.
XmParseResult parseMod(std::span<const uint8_t> file);

}  // namespace stym::ymio
