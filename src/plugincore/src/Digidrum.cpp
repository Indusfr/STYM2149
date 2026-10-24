// STYM2149 — plugincore: Digidrum sample from a decoded audio file, Digidrum editor
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/Digidrum.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ymcore/Ym2149.h"

namespace stym::plugin {

namespace {

// Linear resampling of the prototype at rate `rateHz`; `put(i, v)` receives each value. The output has
// floor(size × ratio) values at most, and a value that is not finite counts as 0.
template <class Put>
size_t resample(std::span<const float> channel, double sampleRate, double rateHz, size_t maxN, Put put) {
    if (channel.empty() || !(sampleRate > 0) || !(rateHz > 0)) return 0;
    const double ratio = rateHz / sampleRate;
    const size_t n = std::min(maxN, size_t(std::floor(double(channel.size()) * ratio)));
    auto at = [&](size_t i) {
        const float v = i < channel.size() ? channel[i] : 0.0f;
        return std::isfinite(v) ? double(v) : 0.0;
    };
    for (size_t i = 0; i < n; ++i) {
        const double x = double(i) / ratio;
        const auto i0 = size_t(x);
        const double fr = x - double(i0);
        put(i, at(i0) * (1 - fr) + at(i0 + 1) * fr);
    }
    return n;
}

// Math.round (half up), as in the prototype.
int8_t toByte(double v) noexcept { return int8_t(std::clamp(std::floor(v * 127 + 0.5), -128.0, 127.0)); }

// Output of a single channel at volume n (level 2n + 1 of the measured mixing table), from 0 (n = 0) to 1 (n = 15).
const std::array<double, 16>& ymLevels() noexcept {
    static const std::array<double, 16> table = [] {
        std::array<double, 16> t{};
        const double lo = ymcore::tables::kMixTable[1], hi = ymcore::tables::kMixTable[31];
        for (int n = 0; n < 16; ++n) t[size_t(n)] = (ymcore::tables::kMixTable[2 * n + 1] - lo) / (hi - lo);
        return t;
    }();
    return table;
}

}  // namespace

size_t digidrumMaxSamples(double rateHz) noexcept {
    const double r = std::isfinite(rateHz) ? std::clamp(rateHz, ymengine::kDigidrumRateMin, ymengine::kDigidrumRateMax)
                                           : ymengine::kDigidrumRate;
    return std::min<size_t>(65535, size_t(std::floor(3 * r)));
}

std::vector<uint8_t> toDigidrum(std::span<const float> channel, double sampleRate, double rateHz) {
    std::vector<uint8_t> out(digidrumMaxSamples(rateHz));
    const size_t n = resample(channel, sampleRate, rateHz, out.size(), [&](size_t i, double v) {
        out[i] = uint8_t(std::clamp(std::floor(v * 7.5 + 7.5 + 0.5), 0.0, 15.0));  // Math.round: half up
    });
    out.resize(n);
    return out;
}

std::vector<int8_t> toDigidrumSource(std::span<const float> channel, double sampleRate, double rateHz) {
    std::vector<int8_t> out(digidrumMaxSamples(rateHz));
    const size_t n = resample(channel, sampleRate, rateHz, out.size(), [&](size_t i, double v) { out[i] = toByte(v); });
    out.resize(n);
    return out;
}

std::vector<uint8_t> digidrumNibbles(std::span<const int8_t> source, DigiConvert mode) {
    std::vector<uint8_t> out(source.size());
    const auto& lv = ymLevels();
    for (size_t i = 0; i < source.size(); ++i) {
        // -128 counts as -127, so that v stays within -1..1.
        const double v = std::max(-1.0, source[i] / 127.0);
        if (mode == DigiConvert::Linear) {
            out[i] = uint8_t(std::clamp(std::floor(v * 7.5 + 7.5 + 0.5), 0.0, 15.0));
            continue;
        }
        // The source mapped to 0..1, like the levels; on a tie, the lower level wins.
        const double target = (v + 1) / 2;
        int best = 0;
        for (int n = 1; n < 16; ++n)
            if (std::abs(lv[size_t(n)] - target) < std::abs(lv[size_t(best)] - target)) best = n;
        out[i] = uint8_t(best);
    }
    return out;
}

std::vector<int8_t> digidrumSourceOf(std::span<const uint8_t> nibbles) {
    std::vector<int8_t> out(nibbles.size());
    for (size_t i = 0; i < nibbles.size(); ++i) out[i] = toByte((std::min<int>(nibbles[i], 15) - 7.5) / 7.5);
    return out;
}

void digiNormalize(std::vector<int8_t>& s) noexcept {
    int peak = 0;
    for (int8_t b : s) peak = std::max(peak, std::abs(int(b)));
    if (peak == 0) return;
    const double g = 127.0 / peak;
    for (int8_t& b : s) b = int8_t(std::clamp(std::floor(b * g + 0.5), -128.0, 127.0));
}

void digiReverse(std::vector<int8_t>& s) noexcept { std::reverse(s.begin(), s.end()); }

// The gain goes from 1 on the first sample down to 1/n on the last one.
void digiFadeOut(std::vector<int8_t>& s) noexcept {
    const size_t n = s.size();
    for (size_t i = 0; i < n; ++i) s[i] = int8_t(std::floor(s[i] * double(n - i) / double(n) + 0.5));
}

std::string cleanDigidrumName(std::string_view name) {
    std::string out;
    for (char c : name)
        if (c >= 0x20 && c <= 0x7e) out += c;
    const size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    // Edge spaces removed, cut to 16 characters, then the spaces the cut may leave at the end removed.
    out = out.substr(a, std::min(out.find_last_not_of(' ') - a + 1, kDigidrumNameMax));
    return out.substr(0, out.find_last_not_of(' ') + 1);
}

}  // namespace stym::plugin
