// STYM2149 — ymengine: STE DMA sample bank and preview of a slot
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Eight samples of signed bytes (mono, or stereo interleaved left then right) copied into fixed storage: the audio
// thread allocates nothing. The DMA voices (Generator::Dma) read them; the preview of the DMA window plays their part,
// once or in a loop, at the rate of the slot, at the pace of the host (byte held until the next one), outside
// the output chain like the preview of a Digidrum.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

#include "ymengine/Dma.h"
#include "ymengine/Params.h"

namespace stym::ymengine::detail {

// Held by the engine (Engine::Impl::dmaBank) and used on its thread: the samples are copied in with setSample, the
// DMA voices keep a pointer to the bytes of their slot (data), the preview has its own position.
class DmaBank {
public:
    // A byte at ±128 is worth ±kScale, the peak of a YM channel alone at its loudest (index 31 of the table of one
    // channel); ratio of the YM and DMA levels on the STE TO CONFIRM.
    static constexpr double kScale = 0.5;

    void setParams(const DmaParams& p) noexcept {
        params_ = p;
        for (auto& s : params_.slots) {
            s.rate = uint8_t(std::min<int>(s.rate, kDmaRates - 1));
            s.root = uint8_t(std::min<int>(s.root, 127));
            s.cents = std::isfinite(s.cents) ? std::clamp(s.cents, -kDmaCentsMax, kDmaCentsMax) : 0.0;
        }
    }
    const DmaParams& params() const noexcept { return params_; }
    const DmaSlot& slot(int k) const noexcept { return params_.slots[size_t(k & 7)]; }

    // Copy (truncated to kDmaMaxBytes). The preview of this slot stops; the voices playing it are stopped by the
    // engine.
    void setSample(int k, std::span<const int8_t> bytes) noexcept {
        if (k < 0 || k >= kDmaSlots) return;
        const size_t n = std::min<size_t>(bytes.size(), kDmaMaxBytes);
        std::copy_n(bytes.begin(), n, data_[size_t(k)].begin());
        len_[size_t(k)] = uint32_t(n);
        if (preview_ == k) preview_ = -1;
    }
    uint32_t sampleBytes(int k) const noexcept { return k >= 0 && k < kDmaSlots ? len_[size_t(k)] : 0; }
    // Frames (one stereo frame: two bytes).
    uint32_t frames(int k) const noexcept {
        if (k < 0 || k >= kDmaSlots) return 0;
        const uint32_t n = len_[size_t(k)];
        return params_.slots[size_t(k)].stereo ? n / 2 : n;
    }
    const int8_t* data(int k) const noexcept { return data_[size_t(k & 7)].data(); }
    // Frames played of slot k, from its Start to its End.
    SampleSpan part(int k) const noexcept { return sampleSpan(slot(k).start, slot(k).end, frames(k)); }

    // Preview: from Start to End, read again at each block; with the slot's Loop on, the part repeats until
    // the preview is stopped or Loop goes off, otherwise it plays once. Only one at a time.
    void preview(int k) noexcept {
        const SampleSpan p = part(k);
        if (p.size() == 0) return;
        preview_ = k;
        pos_ = p.start;
        looping_ = slot(k).loop;
    }
    void stopPreview() noexcept { preview_ = -1; }
    int previewSlot() const noexcept { return preview_; }
    double previewFrame() const noexcept { return pos_; }
    // Adds n host samples of the preview to l (and r, otherwise to l alone: average of the two channels), centred.
    void mixPreview(float* l, float* r, uint32_t n, double hostRate, double scale) noexcept {
        if (preview_ < 0) return;
        const DmaSlot& s = params_.slots[size_t(preview_)];
        if (s.loop) looping_ = true;
        else if (looping_) {
            preview_ = -1;
            return;
        }
        const SampleSpan p = part(preview_);
        const int8_t* d = data_[size_t(preview_)].data();
        // Frames of the sample per host sample: its rate, raised by its fine tune.
        const double inc = kDmaRateHz[s.rate] * std::pow(2.0, s.cents / 1200) / hostRate;
        // A byte at ±128 is worth ±kScale × √½ (centred, constant-power panning), in the mix unit `scale`.
        const double k = kScale / 128.0 * 0.7071067811865476 * scale;
        for (uint32_t i = 0; i < n; ++i) {
            auto f = uint32_t(pos_);
            if (f >= p.end && s.loop && p.size()) {
                pos_ = p.start + std::fmod(pos_ - p.end, double(p.size()));
                f = uint32_t(pos_);
            }
            if (f >= p.end) {
                preview_ = -1;
                return;
            }
            const int a = s.stereo ? d[2 * f] : d[f];
            const int b = s.stereo ? d[2 * f + 1] : a;
            if (r) {
                l[i] += float(a * k);
                r[i] += float(b * k);
            } else {
                l[i] += float((a + b) * 0.5 * k);
            }
            pos_ += inc;
        }
    }

private:
    DmaParams params_{};
    std::array<std::array<int8_t, kDmaMaxBytes>, kDmaSlots> data_{};
    std::array<uint32_t, kDmaSlots> len_{};  // bytes of each slot
    int preview_ = -1;  // slot being previewed, -1: none
    double pos_ = 0;    // frame of the preview (fractional)
    bool looping_ = false;  // the slot's Loop was on during the preview, which stops when Loop goes off
};

}  // namespace stym::ymengine::detail
