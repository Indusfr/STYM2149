// STYM2149 — tests: writing an S3M module (public Scream Tracker 3 format), to build the modules read by
// ymio::parseS3m and the S3M import: header, orders, paragraph pointers, pan table, instruments, packed
// patterns, 8 or 16-bit samples (unsigned or signed, mono or stereo).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stym::test {

// One cell of a pattern: its row and channel (0-31, the file's numbering), then what it holds.
struct S3mCell {
    int row = 0, channel = 0;
    int note = -1;        // -1: no note and instrument; else the byte (octave × 16 + semitone, 254 cut, 255 none)
    int instrument = 0;   // with the note
    int volume = -1;      // -1: none
    int command = 0;      // 0: none; 1 = A … 26 = Z
    int info = 0;
};

inline S3mCell s3mNote(int row, int channel, int octave, int semitone, int instrument) {
    return {row, channel, octave * 16 + semitone, instrument};
}

struct S3mSampleSpec {
    std::string name;
    std::vector<int> data;  // signed values, 8 or 16-bit; the right half after it for a stereo sample
    int volume = 64;
    uint32_t c2spd = 8363;
    bool loop = false;
    uint32_t loopBegin = 0, loopEnd = 0;
    bool sixteenBit = false, stereo = false;
    int type = 1;  // 1: sample; 2: AdLib melody
    int pack = 0;
};

struct S3mSpec {
    std::string title = "s3m test";
    std::vector<int> channels = {0, 8, 9, 1};  // channel settings 0-31; the others are 255 (unused)
    std::vector<uint8_t> order = {0};
    int speed = 6, tempo = 125;
    bool stereo = true;
    bool signedData = false;  // 1 in the header (old modules), otherwise 2 (unsigned)
    std::vector<int> pans;    // pan table (bit 5 and the pan) when not empty
    std::vector<std::vector<S3mCell>> patterns = {{}};
    std::vector<S3mSampleSpec> samples;
    size_t cutAt = 0;  // the file cut to that size (0: whole)
};

inline std::vector<uint8_t> buildS3m(const S3mSpec& m) {
    std::vector<uint8_t> b(0x60, 0);
    auto put16 = [&b](size_t at, int v) {
        b[at] = uint8_t(v);
        b[at + 1] = uint8_t(v >> 8);
    };
    auto put32 = [&b](size_t at, uint32_t v) {
        for (size_t i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i));
    };
    auto align = [&b] {
        while (b.size() % 16) b.push_back(0);
        return b.size() / 16;
    };
    for (size_t i = 0; i < m.title.size() && i < 27; ++i) b[i] = uint8_t(m.title[i]);
    b[0x1C] = 0x1A;
    b[0x1D] = 16;
    put16(0x20, int(m.order.size()));
    put16(0x22, int(m.samples.size()));
    put16(0x24, int(m.patterns.size()));
    put16(0x28, 0x1320);
    put16(0x2A, m.signedData ? 1 : 2);
    b[0x2C] = 'S';
    b[0x2D] = 'C';
    b[0x2E] = 'R';
    b[0x2F] = 'M';
    b[0x30] = 64;
    b[0x31] = uint8_t(m.speed);
    b[0x32] = uint8_t(m.tempo);
    b[0x33] = uint8_t(m.stereo ? 0xB0 : 0x30);
    b[0x35] = m.pans.empty() ? 0 : 0xFC;
    for (size_t c = 0; c < 32; ++c) b[0x40 + c] = c < m.channels.size() ? uint8_t(m.channels[c]) : 255;
    b.insert(b.end(), m.order.begin(), m.order.end());
    const size_t para = b.size();
    b.resize(b.size() + 2 * (m.samples.size() + m.patterns.size()), 0);
    if (!m.pans.empty())
        for (size_t c = 0; c < 32; ++c) b.push_back(c < m.pans.size() ? uint8_t(m.pans[c]) : 0);
    std::vector<size_t> headers;
    for (size_t i = 0; i < m.samples.size(); ++i) {
        const S3mSampleSpec& s = m.samples[i];
        put16(para + 2 * i, int(align()));
        headers.push_back(b.size());
        b.resize(b.size() + 0x50, 0);
        const size_t h = headers.back();
        b[h] = uint8_t(s.type);
        const size_t frames = s.stereo ? s.data.size() / 2 : s.data.size();
        put32(h + 0x10, uint32_t(frames));
        put32(h + 0x14, s.loopBegin);
        put32(h + 0x18, s.loopEnd);
        b[h + 0x1C] = uint8_t(s.volume);
        b[h + 0x1E] = uint8_t(s.pack);
        b[h + 0x1F] = uint8_t((s.loop ? 1 : 0) | (s.stereo ? 2 : 0) | (s.sixteenBit ? 4 : 0));
        put32(h + 0x20, s.c2spd);
        for (size_t k = 0; k < s.name.size() && k < 27; ++k) b[h + 0x30 + k] = uint8_t(s.name[k]);
        b[h + 0x4C] = 'S';
        b[h + 0x4D] = 'C';
        b[h + 0x4E] = 'R';
        b[h + 0x4F] = 'S';
    }
    for (size_t p = 0; p < m.patterns.size(); ++p) {
        put16(para + 2 * (m.samples.size() + p), int(align()));
        const size_t start = b.size();
        b.resize(b.size() + 2, 0);
        for (int row = 0; row < 64; ++row) {
            for (const S3mCell& c : m.patterns[p]) {
                if (c.row != row) continue;
                const int what = c.channel | (c.note >= 0 ? 0x20 : 0) | (c.volume >= 0 ? 0x40 : 0) |
                                 (c.command ? 0x80 : 0);
                b.push_back(uint8_t(what));
                if (c.note >= 0) {
                    b.push_back(uint8_t(c.note));
                    b.push_back(uint8_t(c.instrument));
                }
                if (c.volume >= 0) b.push_back(uint8_t(c.volume));
                if (c.command) {
                    b.push_back(uint8_t(c.command));
                    b.push_back(uint8_t(c.info));
                }
            }
            b.push_back(0);
        }
        put16(start, int(b.size() - start));
    }
    for (size_t i = 0; i < m.samples.size(); ++i) {
        const S3mSampleSpec& s = m.samples[i];
        if (s.type != 1 || s.data.empty()) continue;
        const size_t seg = align();
        const size_t h = headers[i];
        b[h + 0x0D] = uint8_t(seg >> 16);
        b[h + 0x0E] = uint8_t(seg);
        b[h + 0x0F] = uint8_t(seg >> 8);
        for (int v : s.data) {
            if (s.sixteenBit) {
                const int u = m.signedData ? v : v + 32768;
                b.push_back(uint8_t(u));
                b.push_back(uint8_t(u >> 8));
            } else {
                b.push_back(uint8_t(m.signedData ? v : v + 128));
            }
        }
    }
    if (m.cutAt) b.resize(m.cutAt);
    return b;
}

// A square wave of `frames` 8-bit frames.
inline std::vector<int> s3mSquare(size_t frames, int level = 64) {
    std::vector<int> d(frames);
    for (size_t i = 0; i < frames; ++i) d[i] = i / 8 % 2 ? level : -level;
    return d;
}

}  // namespace stym::test
