// STYM2149 — ymcore
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Tables of AtariAudio 1.26 (ym2149_tables.h, © 2026 Arnaud Carré, MIT licence): envelope shapes
// and mixing table measured by Paulo Simoes on an Atari ST, interpolated to 32×32×32 by A. Carré (D-013).
// The original file is included as is from third_party/AtariAudio/src.
#include <cstdint>
#include "ymcore/Ym2149.h"

namespace stym::ymcore::tables {
namespace atariaudio {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif
#include "ym2149_tables.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}  // namespace atariaudio

const uint8_t (&kEnvData)[10 * 128] = atariaudio::s_envData;
const uint16_t (&kMixTable)[32 * 32 * 32] = atariaudio::s_ym2149RecordedMixTable;
}  // namespace stym::ymcore::tables
