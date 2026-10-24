// STYM2149 — ymio
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// YM frame player: translates each frame into timestamped register writes for a ChipRenderer, and plays the timer
// effects of YM5/YM6 files (SID, Sync Buzzer, Digidrum) and the YM2 digidrums through writes timed at the MFP cycle
// (ymengine::MfpTimer). Frame and effect rules taken from the AtariAudio 1.26 player (YmRenderer::Ym2DriverTick,
// Ym356DriverTick, YmFxDecode and ComputeNextSample).
#pragma once
#include <cstdint>
#include <vector>

#include "ymcore/ChipRenderer.h"
#include "ymengine/Mfp68901.h"
#include "ymio/YmFile.h"

namespace stym::ymio {

// When the frames are written (D-038).
enum class YmTiming : uint8_t {
    // Like AtariAudio: one frame every floor(host × 313 × 512 × 50 / (rate × 8 021 247)) samples (real PAL VBL of
    // 50.05 Hz rounded to the sample), applied at the first step of the sample.
    AtariAudio,
    // Frame f at master cycle f × clock / rate (exactly 50 Hz for a 50 Hz file).
    Exact,
};

// Model of the timer effects (D-044).
enum class YmFxModel : uint8_t {
    // Interrupts timed to the MFP cycle; a stopped timer starts with its counter loaded. Default (D-044).
    Exact,
    // Replica of the AtariAudio player: timers advanced once per host sample, at most one interrupt per sample, write
    // at the start of the next sample, first countdown of 256 periods at start, and Sinus-SID code handled like
    // AtariAudio in Release. For oracle comparisons.
    AtariAudio,
};

struct YmPlayerOptions {
    YmTiming timing = YmTiming::Exact;
    YmFxModel fx = YmFxModel::Exact;
    // Interrupt jitter in 68000 cycles (0 = none); exact model only. Real amplitude: TO CONFIRM.
    uint32_t irqJitterCpuCycles = 0;
    uint64_t jitterSeed = 1;
};

// Plays one YmFile into a ChipRenderer whose cycle 0 is the start of playback. Keeps a reference to the file, which
// must outlive it. Built on the message thread; schedule() and render() run on the audio thread without allocation.
class YmPlayer {
public:
    // YM5/YM6 digidrums are converted to 4-bit volumes here (allocation): build outside the audio thread.
    YmPlayer(const YmFile& file, YmTiming timing, uint32_t hostRate);
    YmPlayer(const YmFile& file, const YmPlayerOptions& options, uint32_t hostRate);

    // Pushes into the renderer the writes of all frames and all timer interrupts that fall before sample endSample.
    void schedule(ymcore::ChipRenderer& renderer, uint64_t endSample) noexcept;

    // schedule() then render(), in chunks of at most kChunk samples (the renderer's write queue stays far from its
    // capacity even with two fast timers).
    void render(ymcore::ChipRenderer& renderer, float* out, uint32_t n) noexcept;
    static constexpr uint32_t kChunk = 256;

    // Playback from frame `frame` (at cycle 0 of the renderer), before the first schedule(): resume on another
    // renderer (YM Player, output settings changed). No allocation.
    void startAt(uint64_t frame) noexcept { nextFrame_ = origin_ = frame; }

    // Length of one pass of the song (without the loop), in host samples.
    uint64_t songLengthSamples() const noexcept;
    // Next frame to write, counted from the start of the song (the loop not folded: see songFrame).
    uint64_t framesScheduled() const noexcept { return nextFrame_; }
    // File frame played at `frame`: beyond the end, the loop (loop frame of the file).
    uint32_t songFrame(uint64_t frame) const noexcept;
    uint32_t samplesPerFrameAtariAudio() const noexcept { return spt_; }
    // Frames carrying an effect that the player does not play (YM6 Sinus-SID, which neither AtariAudio nor ST-Sound
    // plays: D-045).
    uint64_t ignoredEffectFrames() const noexcept { return ignoredFx_; }
    // Frames that start or sustain an effect, and timer interrupts served.
    uint64_t effectFrames() const noexcept { return fxFrames_; }
    uint64_t timerInterrupts() const noexcept { return irqs_; }

private:
    enum class FxType : uint8_t { None, Sid, SyncBuzzer, Digidrum };
    // Effect played by one of the two timers (slot 0: timer A, slot 1: timer B, as in AtariAudio's player).
    struct Fx {
        FxType type = FxType::None;
        uint8_t voice = 0;                // channel 0-2
        uint32_t phase = 0;               // SID: interrupts served (odd: volume on); Digidrum: next sample
        const uint8_t* sample = nullptr;  // Digidrum: 4-bit volumes, owned by drums_ or the YM2 bank
        uint32_t length = 0;
        uint8_t sidVolume = 0;
        uint8_t buzzerShape = 0;          // value written to R13 at each interrupt
    };

    // Master cycle at which song frame `frame` is written (frame origin_ at cycle 0).
    uint64_t frameCycle(const ymcore::ChipRenderer& renderer, uint64_t frame) const noexcept;
    // Writes file frame f at `cycle`, after decoding its effects.
    void pushFrame(ymcore::ChipRenderer& renderer, uint64_t cycle, uint32_t f) noexcept;
    // Effect of one slot in frame f (code, prescaler and count in the given registers): starts, keeps or stops the
    // timer; returns the registers (bit r) that the frame must not write because the effect owns them.
    uint32_t decodeFx(ymcore::ChipRenderer& renderer, uint64_t cycle, uint32_t f, int slot, unsigned codeReg,
                      unsigned predivReg, unsigned countReg) noexcept;
    void setTimer(ymcore::ChipRenderer& renderer, uint64_t cycle, int slot, uint8_t prescaler, uint8_t count) noexcept;
    void serveTimer(ymcore::ChipRenderer& renderer, uint64_t cycle, int slot) noexcept;

    const YmFile& file_;
    YmPlayerOptions options_;
    uint32_t hostRate_;
    uint32_t spt_;  // samples per frame (AtariAudio)
    uint64_t nextFrame_ = 0;
    uint64_t origin_ = 0;  // frame played at cycle 0 of the renderer (startAt)
    uint64_t ignoredFx_ = 0;
    uint64_t fxFrames_ = 0;
    uint64_t irqs_ = 0;
    std::vector<std::vector<uint8_t>> drums_;  // YM5/YM6 digidrums as 4-bit volumes
    Fx fx_[2];
    ymengine::MfpTimer timer_[2];
    ymengine::MfpTimerAtariAudio aaTimer_[2];
    ymengine::IrqJitter jitter_;
    uint64_t aaSample_ = 0;  // next host sample at which to advance the timers (AtariAudio model)
};

}  // namespace stym::ymio
