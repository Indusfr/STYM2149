// STYM2149 — ymengine: timers of the MFP 68901
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// MfpTimerAtariAudio follows Mk68901::Timer of AtariAudio 1.26 (© 2026 Arnaud Carré, MIT licence).
#include "ymengine/Mfp68901.h"

#include <cmath>
#include <initializer_list>

namespace stym::ymengine {

namespace {
// ceil(t × num / den) without overflow as long as den × num fits in 64 bits.
uint64_t mulDivCeil(uint64_t t, uint64_t num, uint64_t den) noexcept {
    const uint64_t q = t / den, r = t % den;
    return q * num + (r * num + den - 1) / den;
}
}  // namespace

// For each prescaler, the two data values around the ideal one, then the two bounds; the deviation is measured in
// octaves (|log2|), so the closest pitch wins, and the strict comparison keeps the smallest prescaler on a tie.
MfpSetting mfpQuantize(double hz) noexcept {
    if (!(hz > 0)) return {};
    MfpSetting best{};
    double bestErr = 1e300;
    for (uint8_t p = 1; p < 8; ++p) {
        const double ideal = double(kMfpClockHz) / (double(kMfpPrescalers[p]) * hz);
        const double base = std::floor(ideal);
        for (double d = base; d <= base + 1; d += 1) {
            if (d < 1 || d > 256) continue;
            const MfpSetting s{p, uint16_t(d)};
            const double err = std::fabs(std::log2(s.hz() / hz));
            if (err < bestErr) {
                bestErr = err;
                best = s;
            }
        }
        // Out of reach of this prescaler: the closest bound stays a candidate.
        for (double d : {1.0, 256.0}) {
            const MfpSetting s{p, uint16_t(d)};
            const double err = std::fabs(std::log2(s.hz() / hz));
            if (err < bestErr) {
                bestErr = err;
                best = s;
            }
        }
    }
    return best;
}

// Master cycles per MFP cycle: (num / den) / 2 457 600, applied with exact integers.
uint64_t MfpClockMap::toMaster(uint64_t mfpTick) const noexcept {
    return mulDivCeil(mfpTick, ym_.num, uint64_t(kMfpClockHz) * ym_.den);
}

uint64_t MfpClockMap::toMfp(uint64_t masterCycle) const noexcept {
    return mulDivCeil(masterCycle, uint64_t(kMfpClockHz) * ym_.den, ym_.num);
}

void MfpTimer::write(uint64_t t, MfpSetting s) noexcept {
    if (!s.running()) {
        setting_ = {};
        next_ = UINT64_MAX;
        return;
    }
    if (!running()) {
        // Timer stopped: writing the data register also loads the main counter.
        setting_ = s;
        next_ = t + s.periodTicks();
        return;
    }
    if (next_ <= t) next_ = t + 1;
    if (s.prescaler != setting_.prescaler) {
        // The main counter keeps its value; its countdowns now follow the new prescaler. remaining: counts left
        // before the interrupt, the one in progress included.
        const uint64_t pOld = kMfpPrescalers[setting_.prescaler];
        const uint64_t remaining = (next_ - t + pOld - 1) / pOld;
        next_ = t + remaining * kMfpPrescalers[s.prescaler];
    }
    setting_ = s;  // new reload value, taken at the next interrupt
}

void MfpTimerAtariAudio::reset(uint32_t hostRate) noexcept {
    host_ = hostRate;
    control_ = counter_ = reload_ = 0;
    inner_ = 0;
}

void MfpTimerAtariAudio::write(uint8_t prescaler, uint8_t data) noexcept {
    control_ = prescaler & 0x0f;  // SetCR
    reload_ = data;               // SetDR
    if (control_ == 0) {          // Restart
        inner_ = 0;
        counter_ = reload_;
    }
}

bool MfpTimerAtariAudio::tick() noexcept {
    if ((control_ & 8) || !(control_ & 7)) return false;  // "event" mode or stopped
    // s_Prescale of AtariAudio: 2 457 600 / prescaler, whole values for the 7 prescalers.
    inner_ += kMfpClockHz / kMfpPrescalers[control_ & 7];
    bool fired = false;
    while (inner_ >= host_) {
        --counter_;  // 8 bits: 0 counts down 256 periods
        if (counter_ == 0) {
            counter_ = reload_;
            fired = true;
        }
        inner_ -= host_;
    }
    return fired;
}

}  // namespace stym::ymengine
