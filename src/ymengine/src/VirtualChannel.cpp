// STYM2149 — ymengine: virtual YM channel of the 16-channel mode
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Same rules as ymcore::Ym2149::tick (port of AtariAudio 1.26), rewritten to skip the steps without a change of
// output.
#include "ymengine/VirtualChannel.h"

namespace stym::ymengine {

namespace {
// R13 shape → envelope in ymcore::tables::kEnvData (128 entries each): shapes 0 to 3 play the same envelope, as do
// shapes 4 to 7.
constexpr uint8_t kShapeToEnv[16] = {0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9};
}  // namespace

void VirtualChannel::reset(ymcore::NoiseLfsr noise, bool edgeHigh) noexcept {
    noise_ = noise;
    tonePeriod_ = toneCounter_ = 0;
    edge_ = edgeHigh;
    toneMasked_ = noiseMasked_ = true;  // R7 = $3F at reset
    noisePeriod_ = noiseCounter_ = noiseHalf_ = 0;
    noiseOut_ = false;
    lfsr_ = 1;
    pendingShifts_ = 0;
    envPeriod_ = envCounter_ = envBase_ = 0;
    envPos_ = 0;
    volume_ = 0;
    last_ = 0xff;
}

// While the noise is off, its shifts are only counted (pendingShifts_); they are applied when it is heard again.
void VirtualChannel::setMixer(bool toneOn, bool noiseOn) noexcept {
    toneMasked_ = !toneOn;
    noiseMasked_ = !noiseOn;
    if (noiseOn) flushNoise();
}

// R13 write: the envelope restarts at the start of its first ramp.
void VirtualChannel::writeShape(uint8_t shape) noexcept {
    envBase_ = uint32_t(kShapeToEnv[shape & 15]) * 128u;
    envPos_ = -64;
    envCounter_ = 0;
}

void VirtualChannel::flushNoise() noexcept {
    // The register starts from state 1 and stays on its cycle (131 071 states, or 114 681 for that of AtariAudio):
    // shifting by a number of steps that is a multiple of the period changes nothing. At least one shift is kept so
    // that the noise output is that of the last shift.
    const uint64_t period = noise_ == ymcore::NoiseLfsr::Maximal ? 131071u : 114681u;
    if (pendingShifts_ > period) pendingShifts_ = (pendingShifts_ - 1) % period + 1;
    for (; pendingShifts_ > 0; --pendingShifts_) shiftNoise();
}

}  // namespace stym::ymengine
