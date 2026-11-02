// STYM2149 — tests: writing a MOD module (public ProTracker format), to build the modules read by ymio::parseMod and
// the MOD import. 31 samples and a signature, or 15 samples and none (Soundtracker); FLT8 pairs of 4-channel
// patterns.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stym::test {

struct ModCell {
    int period = 0;  // Amiga period, 0: no note
    int sample = 0;  // 1-31, 0: none
    int effect = 0, param = 0;
};

struct ModSampleSpec {
    std::string name;
    std::vector<int8_t> data;  // an even number of bytes
    int finetune = 0;          // -8 to 7
    int volume = 64;
    int repStart = 0, repLength = 1;  // as written in the header (words, or bytes for an old module)
};

struct ModSpec {
    std::string title = "mod test";
    std::string signature = "M.K.";  // empty: 15-sample Soundtracker module
    int channels = 4;                // FLT8: 8
    std::vector<uint8_t> order = {0};
    int restart = 127;
    std::vector<std::vector<ModCell>> patterns;  // 64 rows × channels each
    std::vector<ModSampleSpec> samples;           // up to 31 (15)
    size_t cutAt = 0;                             // the file cut to that size (0: whole)
};

inline std::vector<ModCell> modPattern(int channels) { return std::vector<ModCell>(size_t(64 * channels)); }

inline std::vector<uint8_t> buildMod(const ModSpec& m) {
    std::vector<uint8_t> b;
    auto text = [&b](const std::string& s, size_t n) {
        for (size_t i = 0; i < n; ++i) b.push_back(i < s.size() ? uint8_t(s[i]) : 0);
    };
    auto be16 = [&b](int v) {
        b.push_back(uint8_t(v >> 8));
        b.push_back(uint8_t(v));
    };
    const size_t slots = m.signature.empty() ? 15 : 31;
    text(m.title, 20);
    for (size_t i = 0; i < slots; ++i) {
        const ModSampleSpec s = i < m.samples.size() ? m.samples[i] : ModSampleSpec{};
        text(s.name, 22);
        be16(int(s.data.size() / 2));
        b.push_back(uint8_t(s.finetune & 15));
        b.push_back(uint8_t(s.volume));
        be16(s.repStart);
        be16(s.repLength);
    }
    b.push_back(uint8_t(m.order.size()));
    b.push_back(uint8_t(m.restart));
    for (size_t i = 0; i < 128; ++i) b.push_back(i < m.order.size() ? m.order[i] : 0);
    if (!m.signature.empty()) text(m.signature, 4);
    const bool flt8 = m.signature == "FLT8";
    auto cell = [&b](const ModCell& c) {
        b.push_back(uint8_t((c.sample & 0xF0) | (c.period >> 8 & 0x0F)));
        b.push_back(uint8_t(c.period));
        b.push_back(uint8_t((c.sample & 0x0F) << 4 | (c.effect & 0x0F)));
        b.push_back(uint8_t(c.param));
    };
    for (const std::vector<ModCell>& p : m.patterns) {
        if (flt8) {  // channels 1-4 of the 64 rows, then channels 5-8
            for (int half = 0; half < 2; ++half)
                for (int row = 0; row < 64; ++row)
                    for (int c = 0; c < 4; ++c) cell(p[size_t(row * 8 + half * 4 + c)]);
        } else {
            for (const ModCell& c : p) cell(c);
        }
    }
    for (const ModSampleSpec& s : m.samples)
        for (int8_t v : s.data) b.push_back(uint8_t(v));
    if (m.cutAt) b.resize(m.cutAt);
    return b;
}

// A square wave of `bytes` bytes (an even number).
inline std::vector<int8_t> modSquare(size_t bytes, int8_t level = 64) {
    std::vector<int8_t> d(bytes);
    for (size_t i = 0; i < bytes; ++i) d[i] = i / 8 % 2 ? level : int8_t(-level);
    return d;
}

}  // namespace stym::test
