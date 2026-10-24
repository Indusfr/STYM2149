// STYM2149 — ymengine: timers of the Atari ST MFP 68901 (STYM2149_SPEC.md §3.3)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Timer effects (SID, Sync Buzzer, Digidrum) are register writes made by an MFP interrupt routine. The "exact"
// model dates each interrupt to the MFP cycle, then converts it to a cycle of the YM master clock: the write
// lands on the intended internal step, not on the host sample (AtariAudio rounds to the host sample,
// docs/m0/AUDIT_DSP.md). MfpTimerAtariAudio reproduces AtariAudio 1.26 (Mk68901.cpp, © 2026 Arnaud Carré, MIT
// licence) for the oracle comparisons.
#pragma once
#include <cstdint>

#include "ymcore/ChipConfig.h"

namespace stym::ymengine {

// Timer clock: 2.4576 MHz crystal of the Atari ST (STYM2149_SPEC.md §3.3; AtariAudio kAtariMfpClock;
// Hatari mfp.c, MFP_TIMER_FREQ). Identical on STF, STE and Mega ST.
inline constexpr uint32_t kMfpClockHz = 2'457'600;
// Prescalers of the "delay" mode, bits 0-2 of TACR, TBCR and TCDCR (Motorola MC68901 datasheet, "Timer
// Control Registers"). Index 0 stops the timer.
inline constexpr uint16_t kMfpPrescalers[8] = {0, 4, 10, 16, 50, 64, 100, 200};

// Setting of a timer: prescaler and data register. The register is 1 to 256 (256 is written as 0).
struct MfpSetting {
    uint8_t prescaler = 0;  // index 1 to 7 in kMfpPrescalers; 0 = stopped
    uint16_t data = 0;      // 1 to 256

    constexpr bool running() const noexcept { return prescaler != 0 && data != 0; }
    // Period in MFP cycles (0 if stopped).
    constexpr uint32_t periodTicks() const noexcept { return running() ? uint32_t(kMfpPrescalers[prescaler & 7]) * data : 0; }
    double hz() const noexcept { return running() ? double(kMfpClockHz) / double(periodTicks()) : 0.0; }
    constexpr bool operator==(const MfpSetting&) const noexcept = default;
};

// Setting whose pitch is closest to hz (deviation in cents); on a tie, the smallest prescaler.
// A frequency out of reach (above 614 400 Hz or below 48 Hz) gives the closest setting.
// hz ≤ 0 gives a stopped timer.
MfpSetting mfpQuantize(double hz) noexcept;

// Exact conversion between MFP cycles and cycles of the YM master clock (fraction num / den Hz).
class MfpClockMap {
public:
    explicit MfpClockMap(ymcore::MasterClock ym = ymcore::clocks::kDefault) noexcept : ym_(ym) {}
    // First master cycle at the instant of MFP cycle t or after it.
    uint64_t toMaster(uint64_t mfpTick) const noexcept;
    // First MFP cycle at the instant of master cycle c or after it.
    uint64_t toMfp(uint64_t masterCycle) const noexcept;
    ymcore::MasterClock ymClock() const noexcept { return ym_; }

private:
    ymcore::MasterClock ym_;
};

// One MFP timer in "delay" mode, exact model. The main counter counts down once per prescaler period; when it
// reaches 0, it reloads from the data register and the interrupt fires.
// Writing the data register while the timer runs only changes the reload value (MC68901 datasheet); starting a
// stopped timer loads the counter, the first interrupt comes after prescaler × data cycles. The phase of the
// prescaler at start is not documented: TO CONFIRM (it shifts the first interrupt by less than one prescaler
// period, 81 µs at most).
class MfpTimer {
public:
    void reset() noexcept { *this = MfpTimer{}; }

    // Writes the setting at instant t (MFP cycles). The interrupts before t must have been handled
    // (nextFire() > t); otherwise the next one is brought to t + 1.
    void write(uint64_t t, MfpSetting s) noexcept;

    bool running() const noexcept { return setting_.running(); }
    MfpSetting setting() const noexcept { return setting_; }
    // MFP cycle of the next interrupt; UINT64_MAX when stopped.
    uint64_t nextFire() const noexcept { return running() ? next_ : UINT64_MAX; }
    // The interrupt nextFire() has been served: move to the next one (one reload period later).
    void acknowledge() noexcept { next_ += setting_.periodTicks(); }

private:
    MfpSetting setting_{};
    uint64_t next_ = UINT64_MAX;
};

// Replica of the AtariAudio 1.26 timer (Mk68901::Timer, delay mode): advanced once per host sample, it fires
// only one interrupt per sample even if several periods have elapsed in it. Used to reproduce the AtariAudio
// YM player bit for bit (oracle); never used by default.
class MfpTimerAtariAudio {
public:
    // State of YmRenderer::Load: timer enabled and unmasked, stopped, registers at 0.
    void reset(uint32_t hostRate) noexcept;
    // YmRenderer::SetTimer: control register then data register (prescaler = index 0-7, raw data).
    void write(uint8_t prescaler, uint8_t data) noexcept;
    // One host sample; true if the interrupt fires.
    bool tick() noexcept;

private:
    uint32_t host_ = 48000;
    uint8_t control_ = 0;  // control register: prescaler index in bits 0-2, bit 3 = event mode
    uint8_t counter_ = 0;
    uint8_t reload_ = 0;
    uint32_t inner_ = 0;   // + MFP clock / prescaler per host sample; each host_ of it is one count of counter_
};

// Interrupt jitter (latency before the 68000 takes it): pseudo-random, uniform delay added to each write made by a
// timer routine. Deterministic: same seed, same sequence (STYM2149_SPEC.md §7).
// The real amplitude depends on the instruction being executed at the time of the interrupt: TO CONFIRM.
class IrqJitter {
public:
    // maxMasterCycles = 0: no jitter.
    void configure(uint32_t maxMasterCycles, uint64_t seed) noexcept {
        max_ = maxMasterCycles;
        state_ = seed ? seed : 0x9E3779B97F4A7C15ull;
    }
    uint32_t maxCycles() const noexcept { return max_; }
    // Delay in master cycles, between 0 and maxMasterCycles inclusive.
    uint32_t next() noexcept {
        if (max_ == 0) return 0;
        // xorshift64* (Vigna, 2016).
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        const uint64_t r = state_ * 0x2545F4914F6CDD1Dull;
        return uint32_t((r >> 32) % (uint64_t(max_) + 1));
    }

private:
    uint32_t max_ = 0;
    uint64_t state_ = 0x9E3779B97F4A7C15ull;
};

// The ST's 68000 runs at 4 times the YM clock (8 MHz and 2 MHz; Hatari sound.c, "CPU freq / 4").
inline constexpr uint32_t cpuCyclesToYmMaster(uint32_t cpuCycles) noexcept { return (cpuCycles + 3) / 4; }

}  // namespace stym::ymengine
