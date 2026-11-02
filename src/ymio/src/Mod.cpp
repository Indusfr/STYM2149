// STYM2149 — ymio: reading MOD modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/Mod.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace stym::ymio {

namespace {

constexpr size_t kTitleBytes = 20, kSampleHeader = 30, kRows = 64, kCellBytes = 4;
constexpr int kOrders = 128;
constexpr uint8_t kFxPan = 0x8, kFxExtra = 0xE, kExFine = 0x5, kExPan = 0x8;

// Big-endian reads that return 0 beyond the end of the file; the parser checks each block with has() first.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> b) : b_(b) {}
    size_t size() const noexcept { return b_.size(); }
    bool has(size_t at, size_t n) const noexcept { return at <= b_.size() && n <= b_.size() - at; }
    uint8_t u8(size_t at) const noexcept { return has(at, 1) ? b_[at] : 0; }
    uint16_t u16(size_t at) const noexcept { return uint16_t(u8(at) << 8 | u8(at + 1)); }
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

// Channels named by the signature at offset 1080; 0 when it is not one. `flt8`: StarTrekker's 8-channel layout.
int signatureChannels(const Reader& r, bool& flt8) {
    std::string s;
    for (size_t i = 0; i < 4; ++i) s += char(r.u8(1080 + i));
    flt8 = s == "FLT8";
    for (const char* four : {"M.K.", "M!K!", "M&K!", "N.T.", "FLT4", "NSMS"})
        if (s == four) return 4;
    for (const char* eight : {"FLT8", "CD81", "OKTA", "OCTA"})
        if (s == eight) return 8;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (s.compare(0, 3, "TDZ") == 0 && s[3] >= '1' && s[3] <= '3') return s[3] - '0';
    if (s.compare(1, 3, "CHN") == 0 && s[0] >= '1' && s[0] <= '9') return s[0] - '0';
    if ((s.compare(2, 2, "CH") == 0 || s.compare(2, 2, "CN") == 0) && digit(s[0]) && digit(s[1])) {
        const int n = (s[0] - '0') * 10 + (s[1] - '0');
        return n >= 1 && n <= kModMaxChannels ? n : 0;
    }
    return 0;
}

// XM note of an Amiga period: the nearest semitone, period 428 (ProTracker's C-2) being the XM C-4 (49); 0 without a
// period.
uint8_t noteOf(int period) noexcept {
    if (period <= 0) return 0;
    const int n = 49 + int(std::lround(12 * std::log2(double(kModC2Period) / period)));
    return uint8_t(std::clamp(n, 1, 96));
}

// A 15-sample Soundtracker module has no signature: its header must hold together (volumes up to 64, no fine tune,
// names without control characters, a song length and orders in range) and the file must hold its patterns.
bool soundtrackerHeader(const Reader& r) {
    constexpr size_t kSongAt = kTitleBytes + 15 * kSampleHeader;  // 470
    if (!r.has(0, kSongAt + 2 + kOrders)) return false;
    uint32_t sampleBytes = 0;
    for (size_t i = 0; i < 15; ++i) {
        const size_t at = kTitleBytes + i * kSampleHeader;
        for (size_t k = 0; k < 22; ++k) {
            const uint8_t c = r.u8(at + k);
            if (c && c < 32) return false;
        }
        if (r.u8(at + 24) != 0 || r.u8(at + 25) > 64 || r.u16(at + 22) > 32768) return false;
        sampleBytes += uint32_t(r.u16(at + 22)) * 2;
    }
    const int length = r.u8(kSongAt);
    if (length < 1 || length > kOrders || sampleBytes == 0) return false;
    int patterns = 0;
    for (int i = 0; i < kOrders; ++i) {
        const int o = r.u8(kSongAt + 2 + size_t(i));
        if (o >= kOrders) return false;
        patterns = std::max(patterns, o + 1);
    }
    return r.has(kSongAt + 2 + kOrders, size_t(patterns) * kRows * 4 * kCellBytes);
}

}  // namespace

XmParseResult parseMod(std::span<const uint8_t> file) {
    const Reader r(file);
    // 31 samples and a signature at 1080, or 15 samples and none (patterns at 600).
    bool flt8 = false;
    int channels = r.has(0, 1084) ? signatureChannels(r, flt8) : 0;
    int samples = 31;
    if (!channels) {
        if (!soundtrackerHeader(r)) return fail("Not a MOD module.");
        channels = 4;
        samples = 15;
    }
    const size_t songAt = kTitleBytes + size_t(samples) * kSampleHeader;
    const size_t patternsAt = songAt + 2 + kOrders + (samples == 31 ? 4 : 0);
    XmModule m;
    m.name = r.text(0, kTitleBytes);
    m.tracker = samples == 31 ? r.text(1080, 4) : "Soundtracker";
    m.channels = channels;
    m.c4Hz = kAmigaPalClock / kModC2Period;
    m.samplePanning = false;
    for (int c = 0; c < channels; ++c) m.channelPan.push_back(c % 4 == 0 || c % 4 == 3 ? 0 : 255);  // L R R L

    // Song length, restart (NoiseTracker; ProTracker writes 127 there), then 128 orders. The patterns stored are those
    // up to the highest order of the whole table, or, if the file is shorter, up to the highest of the song.
    const int length = r.u8(songAt);
    const int restart = samples == 31 ? r.u8(songAt + 1) : 0;
    if (length < 1 || length > kOrders) return fail("Corrupt MOD module.");
    int all = 0, played = 0;
    for (int i = 0; i < kOrders; ++i) {
        const int o = r.u8(songAt + 2 + size_t(i));
        if (o >= kOrders) {
            if (i < length) return fail("Corrupt MOD module.");
            continue;
        }
        all = std::max(all, o + 1);
        if (i < length) played = std::max(played, o + 1);
    }
    // FLT8: each pattern is a pair of 4-channel patterns (channels 1-4, then 5-8), named by its even number.
    const size_t stored = flt8 ? 4 : size_t(channels);
    const size_t patternBytes = kRows * stored * kCellBytes;
    auto patternsFor = [flt8](int n) { return flt8 ? (n + 1) / 2 * 2 : n; };
    int count = patternsFor(all);
    if (!r.has(patternsAt, size_t(count) * patternBytes)) count = patternsFor(played);
    if (!r.has(patternsAt, size_t(count) * patternBytes)) return fail("Corrupt MOD module.");
    for (int i = 0; i < length; ++i) {
        const int o = r.u8(songAt + 2 + size_t(i));
        m.order.push_back(uint8_t(flt8 ? o / 2 : o));
    }
    m.restart = restart < length ? restart : 0;

    // Cell: sample high bits and period (12 bits), sample low bits and effect, parameter.
    const int patterns = flt8 ? count / 2 : count;
    for (int p = 0; p < patterns; ++p) {
        XmPattern pat;
        pat.rows = int(kRows);
        pat.cells.assign(kRows * size_t(channels), XmCell{});
        for (size_t row = 0; row < kRows; ++row) {
            for (int c = 0; c < channels; ++c) {
                const size_t block = flt8 ? size_t(p) * 2 + size_t(c / 4) : size_t(p);
                const size_t at = patternsAt + block * patternBytes +
                                  (row * stored + (flt8 ? size_t(c % 4) : size_t(c))) * kCellBytes;
                const uint8_t b0 = r.u8(at), b1 = r.u8(at + 1), b2 = r.u8(at + 2), b3 = r.u8(at + 3);
                XmCell& x = pat.cells[row * size_t(channels) + size_t(c)];
                x.note = noteOf((b0 & 0x0F) << 8 | b1);
                x.instrument = uint8_t((b0 & 0xF0) | b2 >> 4);
                x.effect = b2 & 0x0F;
                x.param = b3;
                // E5x: the MOD fine tune is a signed nibble (8 = -8), the XM one counts from 8 (8 = 0). E8x: the coarse
                // pan of some MOD players, as the XM 8xx.
                if (x.effect == kFxExtra && x.param >> 4 == kExFine) x.param = uint8_t(0x50 | ((x.param + 8) & 15));
                else if (x.effect == kFxExtra && x.param >> 4 == kExPan) {
                    x.effect = kFxPan;
                    x.param = uint8_t((x.param & 15) * 17);
                }
            }
        }
        m.patterns.push_back(std::move(pat));
    }
    for (uint8_t& o : m.order)
        if (o >= m.patterns.size()) o = 0;

    // Samples, each an instrument: name (22), length in words, fine tune (low nibble, signed), volume, repeat start
    // and length in words (a repeat of one word: no loop). The data follow the patterns, signed 8-bit, in order.
    size_t at = patternsAt + size_t(count) * patternBytes;
    for (int i = 0; i < samples; ++i) {
        const size_t h = kTitleBytes + size_t(i) * kSampleHeader;
        XmInstrument ins;
        ins.name = r.text(h, 22);
        XmSample s;
        s.name = ins.name;
        uint32_t bytes = uint32_t(r.u16(h + 22)) * 2;
        const int nibble = r.u8(h + 24) & 15;
        s.finetune = int8_t((nibble < 8 ? nibble : nibble - 16) * 16);
        s.volume = std::min<uint8_t>(64, r.u8(h + 25));
        s.panning = 128;
        const uint32_t repStart = r.u16(h + 26), repLength = uint32_t(r.u16(h + 28)) * 2;
        if (repLength > 2) {
            s.loopType = 1;
            s.loopLength = repLength;
            // The repeat start counts words, except in older modules where it counts bytes: when only the byte
            // reading keeps the loop inside the sample.
            s.loopStart = repStart * 2 + repLength > bytes && repStart + repLength <= bytes ? repStart : repStart * 2;
        }
        if (!r.has(at, bytes)) {
            bytes = uint32_t(r.size() - std::min(r.size(), at));
            m.truncatedSamples = true;
        }
        s.data.reserve(bytes);
        for (uint32_t k = 0; k < bytes; ++k) s.data.push_back(int16_t(int8_t(r.u8(at + k)) * 256));
        at += bytes;
        const uint32_t frames = uint32_t(s.data.size());
        s.loopStart = std::min(s.loopStart, frames);
        s.loopLength = std::min(s.loopLength, frames - s.loopStart);
        if (s.loopLength == 0) s.loopType = 0;
        ins.samples.push_back(std::move(s));
        m.instruments.push_back(std::move(ins));
    }
    XmParseResult out;
    out.module = std::move(m);
    return out;
}

}  // namespace stym::ymio
