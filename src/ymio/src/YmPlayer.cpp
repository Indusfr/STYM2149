// STYM2149 — ymio
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Frame and effect rules taken from AtariAudio 1.26 (YmRenderer.cpp, © 2026 Arnaud Carré, MIT licence).
#include "ymio/YmPlayer.h"

#include <algorithm>

#include "Ym2Drums.h"

namespace stym::ymio {

namespace {
// AtariAudioRenderer::ComputeHostSamplePerTick: 313 lines × 512 cycles per VBL, CPU clock 8 021 247 Hz.
uint32_t atariAudioSamplesPerFrame(uint32_t hostRate, uint32_t playerRate) noexcept {
    const uint64_t num = uint64_t(hostRate) * 313 * 512 * 50;
    return uint32_t(num / (uint64_t(playerRate) * 8021247u));
}

constexpr uint32_t kYm4BitSamples = 1u << 2;  // YM5/YM6 attribute: digidrums already as 4-bit volumes
}  // namespace

YmPlayer::YmPlayer(const YmFile& file, YmTiming timing, uint32_t hostRate)
    : YmPlayer(file, YmPlayerOptions{timing, YmFxModel::Exact, 0, 1}, hostRate) {}

YmPlayer::YmPlayer(const YmFile& file, const YmPlayerOptions& options, uint32_t hostRate)
    : file_(file), options_(options), hostRate_(hostRate), spt_(atariAudioSamplesPerFrame(hostRate, file.playerRate)) {
    // YM5/YM6 digidrums: 4-bit volumes as is, otherwise the 4 most significant bits go through Mad Max's table
    // (YmRenderer::ConvertTo4Bits).
    drums_.reserve(file.digidrums.size());
    for (const auto& d : file.digidrums) {
        std::vector<uint8_t> v(d);
        if (!(file.attributes & kYm4BitSamples))
            for (auto& s : v) s = detail::kMadMax4Bits[(s >> 4) & 15];
        drums_.push_back(std::move(v));
    }
    for (int t = 0; t < 2; ++t) aaTimer_[t].reset(hostRate);
    jitter_.configure(options.fx == YmFxModel::Exact ? ymengine::cpuCyclesToYmMaster(options.irqJitterCpuCycles) : 0,
                      options.jitterSeed);
}

uint64_t YmPlayer::songLengthSamples() const noexcept {
    if (options_.timing == YmTiming::AtariAudio) return uint64_t(file_.frameCount) * spt_;
    return uint64_t(file_.frameCount) * hostRate_ / file_.playerRate;
}

uint32_t YmPlayer::songFrame(uint64_t frame) const noexcept {
    if (frame < file_.frameCount) return uint32_t(frame);
    const uint64_t loopLen = file_.frameCount - file_.loopFrame;
    return file_.loopFrame + uint32_t((frame - file_.frameCount) % loopLen);
}

uint64_t YmPlayer::frameCycle(const ymcore::ChipRenderer& renderer, uint64_t frame) const noexcept {
    frame -= origin_;
    if (options_.timing == YmTiming::AtariAudio) return renderer.cycleAtSample(frame * spt_);
    // frame × num / (den × rate), from the quotient and remainder of frame / (den × rate): the product frame × num
    // is never formed.
    const ymcore::MasterClock clk = renderer.config().chip.clock;
    const uint64_t den = uint64_t(clk.den) * file_.playerRate;
    return (frame / den) * clk.num + ((frame % den) * clk.num) / den;
}

void YmPlayer::schedule(ymcore::ChipRenderer& renderer, uint64_t endSample) noexcept {
    if (options_.fx == YmFxModel::AtariAudio) {
        // AtariAudio's ComputeNextSample: possible frame at the start of the sample, then the internal steps, then
        // one step of each timer; the write of an effect applies at the start of the next sample.
        for (; aaSample_ < endSample; ++aaSample_) {
            const uint64_t sampleEnd = renderer.cycleAtSample(aaSample_ + 1);
            for (;;) {
                const uint64_t fc = frameCycle(renderer, nextFrame_);
                if (fc >= sampleEnd) break;
                pushFrame(renderer, fc, songFrame(nextFrame_));
                ++nextFrame_;
            }
            for (int t = 0; t < 2; ++t)
                if (aaTimer_[t].tick()) serveTimer(renderer, sampleEnd, t);
        }
        return;
    }
    // Exact model: merges the three event sources (next frame, next interrupt of each timer, timers counted in MFP
    // cycles and converted to master cycles) in time order up to the end of the request.
    const uint64_t endCycle = renderer.cycleAtSample(endSample);
    const ymengine::MfpClockMap map(renderer.config().chip.clock);
    for (;;) {
        const uint64_t fc = frameCycle(renderer, nextFrame_);
        uint64_t irq[2];
        for (int t = 0; t < 2; ++t) {
            const uint64_t at = timer_[t].nextFire();
            irq[t] = at == UINT64_MAX ? UINT64_MAX : map.toMaster(at);
        }
        const uint64_t next = std::min({fc, irq[0], irq[1]});
        if (next >= endCycle) break;
        // On a tie, the timer interrupt (level 6) goes before the VBL (level 4), and timer A before B.
        if (irq[0] == next) {
            timer_[0].acknowledge();
            serveTimer(renderer, next, 0);
        } else if (irq[1] == next) {
            timer_[1].acknowledge();
            serveTimer(renderer, next, 1);
        } else {
            pushFrame(renderer, fc, songFrame(nextFrame_));
            ++nextFrame_;
        }
    }
}

void YmPlayer::render(ymcore::ChipRenderer& renderer, float* out, uint32_t n) noexcept {
    while (n > 0) {
        const uint32_t k = std::min(n, kChunk);
        schedule(renderer, renderer.samplesRendered() + k);
        renderer.render(out, k);
        out += k;
        n -= k;
    }
}

void YmPlayer::setTimer(ymcore::ChipRenderer& renderer, uint64_t cycle, int slot, uint8_t prescaler, uint8_t count) noexcept {
    if (options_.fx == YmFxModel::AtariAudio) {
        aaTimer_[slot].write(prescaler, count);
        return;
    }
    const ymengine::MfpClockMap map(renderer.config().chip.clock);
    // Data register 0 means 256 on the MC68901.
    timer_[slot].write(map.toMfp(cycle), ymengine::MfpSetting{prescaler, uint16_t(count ? count : 256)});
}

// What the effect's interrupt routine writes (YmRenderer::ComputeNextSample), delayed by the optional jitter.
void YmPlayer::serveTimer(ymcore::ChipRenderer& renderer, uint64_t cycle, int slot) noexcept {
    ++irqs_;
    Fx& fx = fx_[slot];
    const uint64_t at = cycle + jitter_.next();
    switch (fx.type) {
    case FxType::Sid:
        ++fx.phase;
        renderer.write(at, uint8_t(8 + fx.voice), (fx.phase & 1) ? fx.sidVolume : 0);
        break;
    case FxType::SyncBuzzer:
        renderer.write(at, 13, fx.buzzerShape);
        break;
    case FxType::Digidrum:
        if (fx.phase < fx.length) {
            renderer.write(at, uint8_t(8 + fx.voice), fx.sample[fx.phase]);
            ++fx.phase;
        } else {
            // End of the sample: the routine stops its timer.
            setTimer(renderer, cycle, slot, 0, 0);
            fx.type = FxType::None;
        }
        break;
    case FxType::None:
        break;
    }
}

// YmRenderer::YmFxDecode. Bits 4-5 of the code register: channel + 1 (0: no effect); bits 6-7: effect type ($00 SID,
// $40 Digidrum, $80 Sinus-SID, $C0 Sync Buzzer); bits 5-7 of the prescaler register: MFP prescaler; count register:
// timer data. The channel volume register carries the effect's value (SID level, drum number, buzzer shape).
uint32_t YmPlayer::decodeFx(ymcore::ChipRenderer& renderer, uint64_t cycle, uint32_t t, int slot, unsigned codeReg,
                            unsigned predivReg, unsigned countReg) noexcept {
    const YmFile& f = file_;
    Fx& fx = fx_[slot];
    const bool aa = options_.fx == YmFxModel::AtariAudio;
    unsigned code = f.reg(t, codeReg) & 0xf0;
    const uint8_t prediv = uint8_t((f.reg(t, predivReg) >> 5) & 7);
    const uint8_t count = f.reg(t, countReg);
    if (f.format == YmFormat::YM5) {
        // YM5: bits 4-5 only; slot 1 carries a SID, slot 2 a digidrum.
        code &= 0x30;
        if (code) code |= slot ? 0x40u : 0x00u;
    }
    uint32_t skip = 0;
    if (code & 0x30) {
        ++fxFrames_;
        const uint8_t voice = uint8_t(((code & 0x30) >> 4) - 1);
        if (aa) fx.voice = voice;  // AtariAudio changes the channel before knowing the effect
        switch (code & 0xc0) {
        case 0x00:  // SID: the timer alternates the channel volume between its value and 0
            fx.type = FxType::Sid;
            fx.voice = voice;
            fx.sidVolume = f.reg(t, 8 + voice) & 15;
            skip = 1u << (voice + 8);
            setTimer(renderer, cycle, slot, prediv, count);
            break;
        case 0x40: {  // Digidrum: the timer writes one 4-bit sample per interrupt into the channel volume
            const unsigned drum = f.reg(t, 8 + voice) & 31;
            if (drum < drums_.size()) {
                fx.type = FxType::Digidrum;
                fx.voice = voice;
                fx.sample = drums_[drum].data();
                fx.length = uint32_t(drums_[drum].size());
                fx.phase = 0;
                skip = 1u << (voice + 8);
                setTimer(renderer, cycle, slot, prediv, count);
            }
            break;
        }
        case 0xc0:  // Sync Buzzer: the timer rewrites R13; the shape is in the channel volume
            fx.type = FxType::SyncBuzzer;
            fx.voice = voice;
            fx.buzzerShape = f.reg(t, 8 + voice) & 15;
            skip = 1u << (voice + 8);
            setTimer(renderer, cycle, slot, prediv, count);
            break;
        default:  // 0x80: Sinus-SID, not played
            ++ignoredFx_;
            if (!aa && fx.type != FxType::Digidrum) {
                setTimer(renderer, cycle, slot, 0, 0);
                fx.type = FxType::None;
            }
            break;
        }
    } else if (fx.type != FxType::Digidrum) {
        // No effect: the timer stops, except a digidrum in progress, which goes to the end of its sample.
        setTimer(renderer, cycle, slot, 0, 0);
        fx.type = FxType::None;
    }
    if (fx.type == FxType::Digidrum) skip |= 1u << (fx.voice + 8);
    return skip;
}

void YmPlayer::pushFrame(ymcore::ChipRenderer& r, uint64_t cycle, uint32_t t) noexcept {
    const YmFile& f = file_;
    if (f.format == YmFormat::YM2) {
        // Ym2DriverTick: R13 ≠ $FF → R11 of the frame, R12 = 0, shape $A; Mad Max digidrum; then R0-R10.
        if (f.reg(t, 13) != 0xff) {
            r.write(cycle, 11, f.reg(t, 11));
            r.write(cycle, 12, 0);
            r.write(cycle, 13, 10);
        }
        Fx& fx = fx_[0];
        const uint8_t r10 = f.reg(t, 10);
        if (r10 & 0x80) {
            const unsigned drum = r10 & 0x7f;
            const uint8_t r12 = f.reg(t, 12);
            if (r12 && drum < detail::kYm2DrumCount) {
                const auto s = detail::ym2Drum(drum);
                fx.type = FxType::Digidrum;
                fx.phase = 0;
                fx.sample = s.data();
                fx.length = uint32_t(s.size());
                fx.voice = 2;
                ++fxFrames_;
                setTimer(r, cycle, 0, 1, r12);  // ÷4, data = R12
            }
        }
        uint32_t skip = 0;
        if (fx.type == FxType::Digidrum) {
            r.write(cycle, 7, f.reg(t, 7) | 0x24);  // channel C: tone and noise off
            skip = (1u << 7) | (1u << (fx.voice + 8));
        }
        for (unsigned reg = 0; reg <= 10; ++reg)
            if (!(skip & (1u << reg))) r.write(cycle, uint8_t(reg), f.reg(t, reg));
        return;
    }

    uint32_t skip = 0;
    // Effect 1: code in R1, prescaler in R6, count in R14; effect 2: R3, R8, R15.
    if (f.format == YmFormat::YM5 || f.format == YmFormat::YM6) {
        skip |= decodeFx(r, cycle, t, 0, 1, 6, 14);
        skip |= decodeFx(r, cycle, t, 1, 3, 8, 15);
    }
    // Ym356DriverTick: a digidrum in progress turns off the tone and noise of its channel; R7 first.
    uint8_t r7 = f.reg(t, 7);
    for (const Fx& fx : fx_)
        if (fx.type == FxType::Digidrum) r7 = uint8_t(r7 | (0x09u << fx.voice));
    r.write(cycle, 7, r7);
    skip |= 1u << 7;
    // Zero envelope period → $FFFF: some files enable the envelope one frame before setting its period (rule of the
    // AtariAudio player).
    if (f.reg(t, 11) == 0 && f.reg(t, 12) == 0) {
        r.write(cycle, 11, 0xff);
        r.write(cycle, 12, 0xff);
        skip |= (1u << 11) | (1u << 12);
    }
    for (unsigned reg = 0; reg <= 12; ++reg)
        if (!(skip & (1u << reg))) r.write(cycle, uint8_t(reg), f.reg(t, reg));
    // R13 = $FF: no envelope restart for this frame.
    if (f.reg(t, 13) != 0xff) r.write(cycle, 13, f.reg(t, 13));
}

}  // namespace stym::ymio
