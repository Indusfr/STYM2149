// STYM2149 — plugincore: register exports of the File menu (M7, A5, STYM2149_SPEC.md §8, D-160, D-163)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/RegisterExport.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "plugincore/Mapping.h"

namespace stym::plugin {

namespace {
// Rate of the capture render: only the times count (the frames are taken in chip cycles).
constexpr uint32_t kCaptureRate = 48000;

bool cancelled(const std::atomic<bool>* cancel) noexcept { return cancel && cancel->load(std::memory_order_relaxed); }
}  // namespace

std::optional<RegisterCapture> captureRegisters(OfflineRender::Source source, const ParamValues& values,
                                                const NonParamState& extras, const std::atomic<bool>* cancel) {
    RegisterCapture out;
    if (voiceMode(values) != ymengine::VoiceMode::Authentic3) return out;
    OfflineRender r(source, kCaptureRate, values, extras);
    const uint32_t hz = std::max<uint32_t>(1, toGlobal(values, extras.global).vblHz);  // replay tick (D-149)
    // One hour of frames, plus those that the last rendered chunk takes beyond the stop.
    const uint64_t maxSamples = uint64_t(kOfflineMaxSeconds * kCaptureRate);
    const size_t room = size_t(kOfflineMaxSeconds) * hz + 64;
    // Sized in full before the render: the engine writes into these spans, it never grows them.
    std::vector<ymengine::RegisterFrame> frames(room);
    std::vector<ymengine::DmaFrame> dma(steMachine(values) ? room : 0);
    if (!r.startCapture(frames, dma)) return out;
    while (!r.finished()) {
        if (cancelled(cancel)) return std::nullopt;
        if (r.position() >= maxSamples) {
            out.truncated = true;
            break;
        }
        r.advance(OfflineRender::kChunk);
    }
    // Frames whose start precedes the stop (or the one-hour cut): k / hz < end / rate.
    const uint64_t end = std::min<uint64_t>(r.finished() ? uint64_t(r.stopSample()) : r.position(), maxSamples);
    const uint64_t keep = (end * hz + kCaptureRate - 1) / kCaptureRate;
    const uint32_t n = uint32_t(std::min<uint64_t>(keep, r.capturedFrames()));
    frames.resize(n);
    if (!dma.empty()) dma.resize(n);
    out.dma = std::move(dma);
    out.take.chip = 0;
    out.take.frameHz = r.captureHz();
    out.take.clockNum = r.chipClock().num;
    out.take.clockDen = r.chipClock().den;
    out.take.frames = std::move(frames);
    return out;
}

}  // namespace stym::plugin
