// STYM2149 — ymio: reading S3M modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Scream Tracker 3 S3M format, from its public description (the Scream Tracker 3.0x TECH.DOC, "S3M format" texts):
// a 96-byte header ("SCRM" at offset 44; song length, instruments, patterns, sample format, speed, tempo, the
// settings of 32 channels), the order table, paragraph pointers (×16 bytes) to the instruments and to the patterns, an
// optional table of channel pans, then the instruments (80 bytes each, "SCRS"), their 8 or 16-bit samples (signed or
// unsigned as the header says, a stereo sample as its left then its right half) and the packed 64-row patterns (a
// flag byte per cell: channel, then note and instrument, volume, command). The module comes out in the shape of an XM
// module (ymio/Xm.h), so that the XM import converts it: notes from octave and semitone (the S3M C-4 is the
// XM C-4, played at the sample's C2Spd), one instrument per sample, the sample's C2Spd as relative note and fine tune
// against 8363 Hz, the channels' pans (ST3's left 3 and right 12 out of 15, or the pan table; all centred in a mono
// module), the commands the import renders turned into their XM effects (A speed, B jump, C break, D volume slide,
// O offset, T tempo, X and S8x pan, S2x fine tune, SCx cut, SDx delay) and the others kept by name
// (XmModule::foreignEffects). PCM channels only: AdLib channels and instruments are left out and reported. Bounded
// reading: a cut or inconsistent file is refused, except the data of the last samples, kept up to the end of the file
// and flagged.
#pragma once
#include <cstdint>
#include <span>

#include "ymio/Xm.h"

namespace stym::ymio {

inline constexpr int kS3mMaxChannels = 32;
inline constexpr double kS3mC4Hz = 8363;  // rate of a C-4 at a C2Spd of 8363

// "SCRM" at offset 44: the signature of an S3M module (File > Import > Tracker song and File > Play tell it by it).
bool looksLikeS3m(std::span<const uint8_t> file) noexcept;

// Reads a whole S3M file held in memory into the XM shape (XmParseResult::error: "Not an S3M module." or
// "Corrupt S3M module."). Pure function, no shared state.
XmParseResult parseS3m(std::span<const uint8_t> file);

}  // namespace stym::ymio
