// STYM2149 — plugincore: what the Song plays in Authentic 3ch, frame by frame, for the tracker exports
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/SongCapture.h"

#include <algorithm>
#include <cmath>

#include "plugincore/Mapping.h"
#include "plugincore/Offline.h"

namespace stym::plugin {

namespace {
constexpr uint32_t kRate = 48000;  // rate of the capture render: only the times count
}  // namespace

std::optional<SongCapture> captureSong(const ParamValues& values, const NonParamState& extras,
                                       const std::atomic<bool>* cancel) {
    SongCapture out;
    OfflineRender r(OfflineRender::Source::Song, kRate, values, extras);
    const uint32_t hz = std::max<uint32_t>(1, toGlobal(values, extras.global).vblHz);
    const uint64_t maxSamples = uint64_t(kOfflineMaxSeconds * kRate);
    const size_t room = size_t(kOfflineMaxSeconds) * hz + 64;
    std::vector<ymengine::RegisterFrame> frames(room);
    std::vector<ymengine::DmaFrame> dma(steMachine(values) ? room : 0);
    if (!r.startCapture(frames, dma)) return out;
    out.hz = std::max<uint32_t>(1, r.captureHz());
    // An event at sample s plays from the first frame that starts at or after it.
    auto frameAt = [&](uint64_t s) { return int((s * out.hz + kRate - 1) / kRate); };
    while (!r.finished()) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return std::nullopt;
        if (r.position() >= maxSamples) {
            out.truncated = true;
            break;
        }
        r.advance(OfflineRender::kChunk);
        for (const ymengine::Engine::SeqLogRecord& x : r.log()) {
            if (x.op == '@') {
                const int f = frameAt(x.sample);
                if (!out.rowFrame.empty() && out.rowFrame.back() >= f) {  // a step shorter than a frame
                    out.rowFrame.pop_back();
                    out.rowEntry.pop_back();
                    out.rowStep.pop_back();
                }
                out.rowFrame.push_back(f);
                out.rowEntry.push_back(x.entry);
                out.rowStep.push_back(x.a);
            } else if (x.op == 'T' && x.voice >= 0 && x.voice < 3 && x.track >= 0) {
                out.attacks[size_t(x.voice)].push_back({frameAt(x.sample), int(std::lround(x.x)), x.track});
            }
        }
    }
    const uint64_t end = std::min<uint64_t>(r.finished() ? uint64_t(r.stopSample()) : r.position(), maxSamples);
    const uint64_t keep = (end * out.hz + kRate - 1) / kRate;
    const size_t n = size_t(std::min<uint64_t>(keep, r.capturedFrames()));
    frames.resize(n);
    out.frames = std::move(frames);
    for (size_t i = 0; i < n && i < dma.size(); ++i) out.dma = out.dma || dma[i].on;
    while (!out.rowFrame.empty() && size_t(out.rowFrame.back()) >= n) {
        out.rowFrame.pop_back();
        out.rowEntry.pop_back();
        out.rowStep.pop_back();
    }
    return out;
}

}  // namespace stym::plugin
