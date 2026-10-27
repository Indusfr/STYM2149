// STYM2149 — plugincore: sample of the STE DMA track
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/DmaSample.h"

#include <algorithm>
#include <cmath>

#include "plugincore/Digidrum.h"
#include "ymengine/Dma.h"

namespace stym::plugin {

namespace {

// Math.round (half up), as for the Digidrums.
int8_t quantize(double v) noexcept { return int8_t(std::clamp(std::floor(v * 127 + 0.5), -128.0, 127.0)); }

}  // namespace

std::vector<int8_t> toDmaSample(std::span<const float> left, std::span<const float> right, double sampleRate,
                                double targetRate) {
    std::vector<int8_t> out;
    if (left.empty() || !(sampleRate > 0) || !(targetRate > 0)) return out;
    const bool stereo = !right.empty();
    const double ratio = targetRate / sampleRate;
    // The 32 KB limit counts bytes: a stereo frame takes two.
    const size_t maxFrames = ymengine::kDmaMaxBytes / (stereo ? 2 : 1);
    const size_t n = std::min(maxFrames, size_t(std::floor(double(left.size()) * ratio)));
    out.resize(n * (stereo ? 2 : 1));
    auto at = [](std::span<const float> c, size_t i) {
        const float v = i < c.size() ? c[i] : 0.0f;
        return std::isfinite(v) ? double(v) : 0.0;
    };
    // The same linear interpolation as the Digidrums; both channels at the same positions.
    for (size_t i = 0; i < n; ++i) {
        const double x = double(i) / ratio;
        const auto i0 = size_t(x);
        const double fr = x - double(i0);
        const double l = at(left, i0) * (1 - fr) + at(left, i0 + 1) * fr;
        if (!stereo) {
            out[i] = quantize(l);
            continue;
        }
        const double r = at(right, i0) * (1 - fr) + at(right, i0 + 1) * fr;
        out[2 * i] = quantize(l);
        out[2 * i + 1] = quantize(r);
    }
    return out;
}

std::vector<int8_t> dmaResample(std::span<const int8_t> bytes, bool stereo, double fromHz, double toHz) {
    // Bytes back to -1..1 (b / 127: quantize gives each byte back at the same rate).
    const size_t frames = bytes.size() / (stereo ? 2 : 1);
    std::vector<float> left(frames), right(stereo ? frames : 0);
    for (size_t f = 0; f < frames; ++f) {
        left[f] = float(stereo ? bytes[2 * f] : bytes[f]) / 127.0f;
        if (stereo) right[f] = float(bytes[2 * f + 1]) / 127.0f;
    }
    return toDmaSample(left, right, fromHz, toHz);
}

std::vector<int8_t> dmaRaw(std::span<const uint8_t> bytes, bool stereo) {
    size_t n = std::min<size_t>(bytes.size(), ymengine::kDmaMaxBytes);
    if (stereo) n &= ~size_t(1);
    std::vector<int8_t> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = int8_t(bytes[i]);
    return out;
}

void dmaSign(std::vector<int8_t>& bytes) noexcept {
    for (int8_t& b : bytes) b = int8_t(uint8_t(b) ^ 0x80u);
}

void dmaGain(std::vector<int8_t>& bytes, double db) noexcept {
    const double g = std::pow(10.0, db / 20);
    for (int8_t& b : bytes) b = int8_t(std::clamp(std::floor(b * g + 0.5), -128.0, 127.0));
}

void dmaTool(std::vector<int8_t>& bytes, bool stereo, unsigned channels, DmaTool tool, double db) {
    auto apply = [tool, db](std::vector<int8_t>& s) {
        switch (tool) {
        case DmaTool::Normalize: digiNormalize(s); break;
        case DmaTool::Reverse: digiReverse(s); break;
        case DmaTool::FadeOut: digiFadeOut(s); break;
        case DmaTool::Sign: dmaSign(s); break;
        case DmaTool::Gain: dmaGain(s, db); break;
        }
    };
    channels &= 3u;
    if (!stereo || (channels == 3 && tool == DmaTool::Normalize)) {
        apply(bytes);
        return;
    }
    const size_t frames = bytes.size() / 2;
    for (size_t c = 0; c < 2; ++c) {
        if (!(channels >> c & 1u)) continue;
        std::vector<int8_t> one(frames);
        for (size_t f = 0; f < frames; ++f) one[f] = bytes[2 * f + c];
        apply(one);
        for (size_t f = 0; f < frames; ++f) bytes[2 * f + c] = one[f];
    }
}

const std::vector<int8_t>& GainRun::step(int key, const std::vector<int8_t>& now, double db) {
    if (key != key_ || now != last_ || now.empty()) {
        key_ = key;
        base_ = now;
        db_ = 0;
    }
    db_ += db;
    return base_;
}

}  // namespace stym::plugin
