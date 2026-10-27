// STYM2149 — ymio: reading XM modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/Xm.h"

#include <algorithm>
#include <cstring>

namespace stym::ymio {

namespace {

// Little-endian reads that return 0 beyond the end of the file; the parser checks each block with has() first.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> b) : b_(b) {}
    size_t size() const noexcept { return b_.size(); }
    bool has(size_t at, size_t n) const noexcept { return at <= b_.size() && n <= b_.size() - at; }
    uint8_t u8(size_t at) const noexcept { return has(at, 1) ? b_[at] : 0; }
    uint16_t u16(size_t at) const noexcept { return uint16_t(u8(at) | u8(at + 1) << 8); }
    uint32_t u32(size_t at) const noexcept { return uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16; }
    // Fixed-length text, cut at the first zero, trailing spaces removed.
    std::string text(size_t at, size_t n) const {
        std::string s;
        for (size_t i = 0; i < n && has(at + i, 1) && b_[at + i]; ++i) s += char(b_[at + i]);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }

private:
    std::span<const uint8_t> b_;
};

XmParseResult fail(std::string e) {
    XmParseResult r;
    r.error = std::move(e);
    return r;
}

}  // namespace

XmParseResult parseXm(std::span<const uint8_t> file) {
    const Reader r(file);
    // Header: id text (17 bytes), module name (20), $1A, tracker name (20), version (2), then a block that starts at
    // offset 60 with its own size (4): song length, restart, channels, patterns, instruments, flags (bit 0: linear
    // table), tempo, BPM, all 16-bit, then the order table. The patterns start at the end of this block.
    if (!r.has(0, 80) || std::memcmp(file.data(), "Extended Module: ", 17) != 0) return fail("Not an XM module.");
    XmModule m;
    m.name = r.text(17, 20);
    m.tracker = r.text(38, 20);
    m.version = r.u16(58);
    if (m.version != 0x0104) return fail("Unsupported XM version (1.04 only).");
    const uint32_t headerSize = r.u32(60);
    const int length = r.u16(64);
    m.restart = r.u16(66);
    m.channels = r.u16(68);
    const int patterns = r.u16(70);
    const int instruments = r.u16(72);
    m.linear = (r.u16(74) & 1) != 0;
    m.tempo = r.u16(76);
    m.bpm = r.u16(78);
    if (m.channels < 1 || m.channels > kXmMaxChannels || patterns > kXmMaxPatterns ||
        instruments > kXmMaxInstruments || length > 256 || headerSize < 20 || !r.has(60, headerSize))
        return fail("Corrupt XM header.");
    // Only the order entries inside the header block are read (the 20 bytes before them are the fields above).
    for (int i = 0; i < length && i < int(headerSize) - 20; ++i) m.order.push_back(r.u8(80 + size_t(i)));

    // Pattern: header length (4 bytes), packing type (1), rows (2), size of the packed data (2), then the data.
    size_t at = 60 + size_t(headerSize);
    for (int p = 0; p < patterns; ++p) {
        if (!r.has(at, 9)) return fail("Corrupt XM pattern.");
        const uint32_t len = r.u32(at);
        XmPattern pat;
        pat.rows = r.u16(at + 5);
        const uint16_t packed = r.u16(at + 7);
        if (len < 9 || pat.rows < 1 || pat.rows > kXmMaxRows || !r.has(at, size_t(len) + packed))
            return fail("Corrupt XM pattern.");
        pat.cells.assign(size_t(pat.rows) * size_t(m.channels), XmCell{});
        size_t q = at + len;
        const size_t end = q + packed;
        // Each cell starts with a byte. With bit 7 set, its bits 0-4 say which of note, instrument, volume, effect
        // and parameter follow; otherwise the byte is the note and the four other fields follow.
        if (packed) {
            for (XmCell& c : pat.cells) {
                if (q >= end) return fail("Corrupt XM pattern.");
                const uint8_t b = r.u8(q++);
                uint8_t mask = 0x1F;
                if (b & 0x80) {
                    mask = b & 0x1F;
                } else {
                    c.note = b;
                    mask = 0x1E;
                }
                uint8_t* fields[5] = {&c.note, &c.instrument, &c.volume, &c.effect, &c.param};
                for (int k = 0; k < 5; ++k) {
                    if (!(mask & (1 << k))) continue;
                    if (q >= end) return fail("Corrupt XM pattern.");
                    *fields[k] = r.u8(q++);
                }
            }
        }
        m.patterns.push_back(std::move(pat));
        at = end;
    }
    for (uint8_t& o : m.order)
        if (o >= m.patterns.size()) o = 0;  // missing pattern: Fasttracker II then plays an empty 64-row pattern

    // Instrument: header size (4 bytes), name (22), type (1), sample count (2); with samples, the size of each sample
    // header (4) and the keymap (96 bytes), then envelopes and the rest, skipped. The sample headers follow the
    // instrument header, then the data of all its samples, in the same order.
    for (int i = 0; i < instruments; ++i) {
        if (!r.has(at, 29)) return fail("Corrupt XM instrument.");
        const uint32_t size = r.u32(at);
        XmInstrument ins;
        ins.name = r.text(at + 4, 22);
        const int count = r.u16(at + 27);
        if (size < 29 || count > kXmMaxSamples || !r.has(at, size)) return fail("Corrupt XM instrument.");
        uint32_t headerLen = 40;
        if (count > 0) {
            if (size < 33 + 96) return fail("Corrupt XM instrument.");
            headerLen = r.u32(at + 29);
            for (size_t k = 0; k < 96; ++k) ins.keymap[k] = r.u8(at + 33 + k);
            if (headerLen < 40) return fail("Corrupt XM instrument.");
        }
        at += size;
        std::vector<uint32_t> bytes(size_t(count), 0);
        for (int s = 0; s < count; ++s) {
            if (!r.has(at, headerLen)) return fail("Corrupt XM instrument.");
            // Sample header: data length in bytes (4), loop start (4), loop length (4), volume, finetune, type (bits
            // 0-1: loop, bit 4: 16-bit), panning, relative note, $AD for ModPlug ADPCM, name (22). The lengths count
            // bytes, hence the halving for 16-bit samples.
            XmSample smp;
            bytes[size_t(s)] = r.u32(at);
            uint32_t loopStart = r.u32(at + 4), loopLength = r.u32(at + 8);
            smp.volume = std::min<uint8_t>(64, r.u8(at + 12));
            smp.finetune = int8_t(r.u8(at + 13));
            const uint8_t type = r.u8(at + 14);
            smp.loopType = type & 3;
            if (smp.loopType == 3) smp.loopType = 0;
            smp.sixteenBit = (type & 0x10) != 0;
            smp.panning = r.u8(at + 15);
            smp.relativeNote = int8_t(r.u8(at + 16));
            smp.adpcm = r.u8(at + 17) == 0xAD;
            smp.name = r.text(at + 18, 22);
            if (smp.sixteenBit) {
                loopStart /= 2;
                loopLength /= 2;
            }
            smp.loopStart = loopStart;
            smp.loopLength = loopLength;
            ins.samples.push_back(std::move(smp));
            at += headerLen;
        }
        for (int s = 0; s < count; ++s) {
            XmSample& smp = ins.samples[size_t(s)];
            uint32_t n = bytes[size_t(s)];
            if (smp.adpcm) {  // 16 bytes of table then 4 bits per frame
                const size_t packed = 16 + (size_t(n) + 1) / 2;
                if (r.has(at, packed)) {
                    at += packed;
                } else {
                    at = r.size();
                    m.truncatedSamples = true;
                }
                continue;
            }
            if (!r.has(at, n)) {
                n = uint32_t(r.size() - std::min(r.size(), at));
                m.truncatedSamples = true;
            }
            // Delta coding: each value is the difference to the previous one; the sum wraps around at 8 or 16 bits.
            if (smp.sixteenBit) {
                int16_t v = 0;
                smp.data.reserve(n / 2);
                for (uint32_t k = 0; k + 1 < n; k += 2) {
                    v = int16_t(v + int16_t(r.u16(at + k)));
                    smp.data.push_back(v);
                }
            } else {
                int8_t v = 0;
                smp.data.reserve(n);
                for (uint32_t k = 0; k < n; ++k) {
                    v = int8_t(v + int8_t(r.u8(at + k)));
                    smp.data.push_back(int16_t(v * 256));
                }
            }
            at += n;
            // Loop bounded to the data.
            const uint32_t frames = uint32_t(smp.data.size());
            smp.loopStart = std::min(smp.loopStart, frames);
            smp.loopLength = std::min(smp.loopLength, frames - smp.loopStart);
            if (smp.loopLength == 0) smp.loopType = 0;
        }
        m.instruments.push_back(std::move(ins));
    }
    XmParseResult out;
    out.module = std::move(m);
    return out;
}

}  // namespace stym::ymio
