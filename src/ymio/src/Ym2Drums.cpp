// STYM2149 — ymio: digidrum bank of YM2 files
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Data of AtariAudio 1.26 (ym2data.h and YmRenderer.cpp, © 2026 Arnaud Carré, MIT licence): the bank of the 40
// 4-bit samples of Mad Max's YM2 player, included as is from third_party/AtariAudio/src.
#include "Ym2Drums.h"

namespace stym::ymio::detail {
namespace atariaudio {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#endif
#include "ym2data.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}  // namespace atariaudio

std::span<const uint8_t> ym2Drum(unsigned n) noexcept {
    if (n >= kYm2DrumCount) return {};
    return {atariaudio::ym2Bank + atariaudio::ym2BankOffsets[n], atariaudio::ym2BankLens[n]};
}

const uint8_t kMadMax4Bits[16] = {0x0, 0x7, 0x9, 0xa, 0xb, 0xc, 0xc, 0xd, 0xd, 0xd, 0xe, 0xe, 0xe, 0xf, 0xf, 0xf};

}  // namespace stym::ymio::detail
