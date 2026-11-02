// STYM2149 — tests: writing an Oktalyzer module (OKT, Amiga), to build the modules read by ymio::parseOkt and the OKT
// import: CMOD, SAMP, SPEE, SLEN, PLEN, PATT, one PBOD per pattern, one SBOD per sample with data.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stym::test {

struct OktCell {
    int note = 0;    // 1-36, 0: none
    int sample = 0;  // 0-35, with the note
    int effect = 0, param = 0;
};

struct OktSampleSpec {
    std::string name;
    std::vector<int8_t> data;
    int volume = 64;
    int repStart = 0, repLength = 0;  // words
    int type = 1;
};

struct OktPatternSpec {
    int rows = 64;
    std::vector<OktCell> cells;  // rows × channels
};

struct OktSpec {
    std::vector<int> split = {0, 0, 0, 0};  // CMOD: 1 splits the Amiga channel into two voices
    std::vector<OktSampleSpec> samples;     // SAMP holds 36 headers
    int speed = 6;
    std::vector<uint8_t> order = {0};
    std::vector<OktPatternSpec> patterns;
    size_t cutAt = 0;  // the file cut to that size (0: whole)

    int channels() const {
        int n = 0;
        for (int s : split) n += s ? 2 : 1;
        return n;
    }
};

inline OktPatternSpec oktPattern(int rows, int channels) {
    return {rows, std::vector<OktCell>(size_t(rows * channels))};
}

inline std::vector<uint8_t> buildOkt(const OktSpec& m) {
    std::vector<uint8_t> b = {'O', 'K', 'T', 'A', 'S', 'O', 'N', 'G'};
    auto be16 = [](std::vector<uint8_t>& v, int x) {
        v.push_back(uint8_t(x >> 8));
        v.push_back(uint8_t(x));
    };
    auto be32 = [](std::vector<uint8_t>& v, uint32_t x) {
        for (int i = 3; i >= 0; --i) v.push_back(uint8_t(x >> (8 * i)));
    };
    auto chunk = [&](const char* id, const std::vector<uint8_t>& body) {
        b.insert(b.end(), id, id + 4);
        be32(b, uint32_t(body.size()));
        b.insert(b.end(), body.begin(), body.end());
    };
    std::vector<uint8_t> c;
    for (int s : m.split) be16(c, s);
    chunk("CMOD", c);
    c.clear();
    for (size_t i = 0; i < 36; ++i) {
        const OktSampleSpec s = i < m.samples.size() ? m.samples[i] : OktSampleSpec{};
        for (size_t k = 0; k < 20; ++k) c.push_back(k < s.name.size() ? uint8_t(s.name[k]) : 0);
        be32(c, uint32_t(s.data.size()));
        be16(c, s.repStart);
        be16(c, s.repLength);
        be16(c, s.volume);
        be16(c, s.type);
    }
    chunk("SAMP", c);
    c.clear();
    be16(c, m.speed);
    chunk("SPEE", c);
    c.clear();
    be16(c, int(m.patterns.size()));
    chunk("SLEN", c);
    c.clear();
    be16(c, int(m.order.size()));
    chunk("PLEN", c);
    c.assign(128, 0);
    for (size_t i = 0; i < m.order.size() && i < 128; ++i) c[i] = m.order[i];
    chunk("PATT", c);
    for (const OktPatternSpec& p : m.patterns) {
        c.clear();
        be16(c, p.rows);
        for (const OktCell& x : p.cells) {
            c.push_back(uint8_t(x.note));
            c.push_back(uint8_t(x.sample));
            c.push_back(uint8_t(x.effect));
            c.push_back(uint8_t(x.param));
        }
        chunk("PBOD", c);
    }
    for (const OktSampleSpec& s : m.samples) {
        if (s.data.empty()) continue;
        c.assign(s.data.begin(), s.data.end());
        chunk("SBOD", c);
    }
    if (m.cutAt) b.resize(m.cutAt);
    return b;
}

// A square wave of `bytes` bytes.
inline std::vector<int8_t> oktSquare(size_t bytes, int8_t level = 64) {
    std::vector<int8_t> d(bytes);
    for (size_t i = 0; i < bytes; ++i) d[i] = i / 8 % 2 ? level : int8_t(-level);
    return d;
}

}  // namespace stym::test
