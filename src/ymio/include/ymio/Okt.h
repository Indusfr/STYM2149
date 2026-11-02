// STYM2149 — ymio: reading Oktalyzer modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Oktalyzer OKT format (Amiga), from its public descriptions: "OKTASONG", then big-endian IFF chunks: CMOD (for each
// of the 4 Amiga channels, 1 when it is split into two mixed voices: 4 to 8 channels), SAMP (36 sample headers of 32
// bytes: name, length, repeat start and length in words, volume, type), SPEE (speed), SLEN (patterns), PLEN (song
// length), PATT (order table), one PBOD per pattern (its rows, then 4 bytes per cell: note 1-36, sample, effect,
// parameter) and one SBOD per sample that has data (signed 8-bit). The module comes out in the shape of an XM module
// (ymio/Xm.h), so that the XM import converts it: notes 1-36 are ProTracker's C-1 to B-3 (note 13, C-2, the
// XM C-4, at the PAL rate of period 428 as for a MOD), one instrument per sample, the Amiga pan of the channels (L R R
// L, a split channel's two voices on its side), 50 Hz ticks (125 BPM), the effects the import renders turned into XM
// effects (P jump, S speed, V volume and slides, R release) and the others kept by name (XmModule::foreignEffects). A
// note on a split channel plays at full volume, as Oktalyzer plays it. Bounded reading: a cut or inconsistent file is
// refused, except the data of the last samples, kept up to the end of the file and flagged.
#pragma once
#include <cstdint>
#include <span>

#include "ymio/Xm.h"

namespace stym::ymio {

inline constexpr int kOktMaxChannels = 8;

// "OKTASONG" at the start: the signature of an Oktalyzer module.
bool looksLikeOkt(std::span<const uint8_t> file) noexcept;

// Reads a whole OKT file held in memory into the XM shape (XmParseResult::error: "Not an OKT module." or
// "Corrupt OKT module."). Pure function, no shared state.
XmParseResult parseOkt(std::span<const uint8_t> file);

}  // namespace stym::ymio
