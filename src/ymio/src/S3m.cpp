// STYM2149 — ymio: reading S3M modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/S3m.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace stym::ymio {

namespace {

constexpr size_t kHeader = 0x60, kInstrumentBytes = 0x50, kRows = 64;
constexpr int kMaxInstruments = 255, kMaxPatterns = 256, kMaxOrders = 256;
// XM effects written by the reader (the ones the import renders).
constexpr uint8_t kFxPan = 0x8, kFxOffset = 0x9, kFxVolSlide = 0xA, kFxJump = 0xB, kFxBreak = 0xD, kFxExtra = 0xE,
                  kFxSpeed = 0xF;
constexpr uint8_t kNoteCut = 254, kNoteNone = 255;
constexpr uint8_t kDefaultPanTable = 0xFC;

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

std::string named(char letter, const char* rest) { return std::string{letter} + rest; }

// One S3M command in the XM shape: the commands the import renders become their XM effects, the others keep their
// S3M name (XmCell::foreign).
void command(XmModule& m, XmCell& x, uint8_t cmd, uint8_t info) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (cmd < 1 || cmd > 26) return;  // none, or beyond Z
    const char letter = char('A' + cmd - 1);
    const int hi = info >> 4, lo = info & 15;
    auto xm = [&x](uint8_t effect, int param) {
        x.effect = effect;
        x.param = uint8_t(param);
    };
    switch (letter) {
    case 'A':  // speed (A00: nothing); 32 and up would be an XM tempo
        if (info) xm(kFxSpeed, std::min<int>(info, 0x1F));
        return;
    case 'B':
        xm(kFxJump, info);
        return;
    case 'C':  // pattern break, decimal-coded as in the XM
        xm(kFxBreak, info);
        return;
    case 'D':  // volume slide: D0y down, Dx0 up, DxF and DFy fine (once a line, not rendered), D00 the previous one
        if (lo == 15 && hi != 0) x.foreign = m.foreignEffect("DxF");
        else if (hi == 15 && lo != 0) x.foreign = m.foreignEffect("DFy");
        else xm(kFxVolSlide, lo ? lo : info & 0xF0);
        return;
    case 'O':
        xm(kFxOffset, info);
        return;
    case 'T':  // tempo from 32 (BPM); below, the tempo slides of later trackers
        if (info >= 0x20) xm(kFxSpeed, info);
        else x.foreign = m.foreignEffect("Txx");
        return;
    case 'X':  // pan 00-80; A4 is the surround of later trackers
        if (info <= 0x80) xm(kFxPan, std::min(255, info * 2));
        else x.foreign = m.foreignEffect("Xxx");
        return;
    case 'S':
        if (hi == 0x8) xm(kFxPan, lo * 17);
        else if (hi == 0xC) xm(kFxExtra, 0xC0 | lo);
        else if (hi == 0xD) xm(kFxExtra, 0xD0 | lo);
        else if (hi == 0x2) xm(kFxExtra, 0x50 | ((lo + 8) & 15));  // signed nibble (8 = -8) → XM count from 8
        else x.foreign = m.foreignEffect(std::string{'S', kHex[hi], 'x'});
        return;
    default:
        x.foreign = m.foreignEffect(named(letter, "xx"));
        return;
    }
}

// The C2Spd of a sample (ST3 keeps 16 bits; 0 is 8363, below 1024 is 1024) as semitones and 1/128 against 8363 Hz.
void tuneOf(XmSample& s, uint32_t c2spd) {
    double hz = std::min<uint32_t>(c2spd, 0xFFFF);
    if (hz == 0) hz = kS3mC4Hz;
    hz = std::max(hz, 1024.0);
    const long total = std::lround(12 * 128 * std::log2(hz / kS3mC4Hz));
    const long rel = std::clamp<long>(std::lround(double(total) / 128.0), -96, 95);
    s.relativeNote = int8_t(rel);
    s.finetune = int8_t(std::clamp<long>(total - rel * 128, -128, 127));
}

}  // namespace

bool looksLikeS3m(std::span<const uint8_t> file) noexcept {
    return file.size() >= kHeader && file[44] == 'S' && file[45] == 'C' && file[46] == 'R' && file[47] == 'M';
}

XmParseResult parseS3m(std::span<const uint8_t> file) {
    const Reader r(file);
    if (!looksLikeS3m(file)) return fail("Not an S3M module.");
    const int orders = r.u16(0x20), samples = r.u16(0x22), patterns = r.u16(0x24);
    const bool signedData = r.u16(0x2A) == 1;  // 1: signed samples (old modules), 2: unsigned
    const size_t paraAt = kHeader + size_t(orders);
    if (orders > kMaxOrders || samples > kMaxInstruments || patterns > kMaxPatterns ||
        !r.has(paraAt, size_t(samples + patterns) * 2))
        return fail("Corrupt S3M module.");

    XmModule m;
    m.name = r.text(0, 28);
    m.tracker = "Scream Tracker 3";
    m.linear = false;
    m.c4Hz = kS3mC4Hz;
    m.samplePanning = false;
    const int speed = r.u8(0x31), tempo = r.u8(0x32);
    m.tempo = speed >= 1 && speed <= 254 ? speed : 6;
    m.bpm = tempo >= 33 ? tempo : 125;

    // Channels: the PCM channels that play (settings 0-7 left, 8-15 right), in order; AdLib channels (16-29), disabled
    // channels (bit 7) and unused ones (255) are left out. Pans: ST3's 3 and 12 out of 15 in a stereo module (master
    // volume bit 7), the centre in a mono one, the pan table where it gives one (bit 5).
    const bool stereo = (r.u8(0x33) & 0x80) != 0;
    const size_t panAt = paraAt + size_t(samples + patterns) * 2;
    const bool panTable = r.u8(0x35) == kDefaultPanTable && r.has(panAt, 32);
    std::vector<int> channelOf(kS3mMaxChannels, -1);
    int adlibChannels = 0;
    for (int c = 0; c < kS3mMaxChannels; ++c) {
        const uint8_t set = r.u8(0x40 + size_t(c));
        if (set & 0x80) continue;
        if (set >= 16) {
            adlibChannels += set <= 29;
            continue;
        }
        channelOf[size_t(c)] = m.channels++;
        int pan = !stereo ? 128 : set < 8 ? 3 * 17 : 12 * 17;
        if (panTable && (r.u8(panAt + size_t(c)) & 0x20)) pan = (r.u8(panAt + size_t(c)) & 15) * 17;
        m.channelPan.push_back(uint8_t(pan));
    }
    if (m.channels == 0) return fail("Nothing to import: no notes with a sample.");  // AdLib channels only

    // Orders: 254 is a marker (skipped), 255 the end.
    for (int i = 0; i < orders; ++i) {
        const uint8_t o = r.u8(kHeader + size_t(i));
        if (o == 255) break;
        if (o != 254) m.order.push_back(o);
    }

    // Instruments: a sample each (type 1), or nothing (type 0); AdLib instruments (2 and up) left out, reported.
    int adlibInstruments = 0;
    for (int i = 0; i < samples; ++i) {
        const size_t at = size_t(r.u16(paraAt + size_t(i) * 2)) * 16;
        XmInstrument ins;
        if (at == 0 || !r.has(at, kInstrumentBytes)) {
            if (at) return fail("Corrupt S3M module.");
            m.instruments.push_back(std::move(ins));
            continue;
        }
        const uint8_t type = r.u8(at);
        ins.name = r.text(at + 0x30, 28);
        if (ins.name.empty()) ins.name = r.text(at + 1, 12);
        if (type >= 2) {
            ++adlibInstruments;
            m.instruments.push_back(std::move(ins));
            continue;
        }
        XmSample s;
        s.name = ins.name;
        s.volume = std::min<uint8_t>(64, r.u8(at + 0x1C));
        s.panning = 128;
        tuneOf(s, r.u32(at + 0x20));
        const uint8_t pack = r.u8(at + 0x1E), flags = r.u8(at + 0x1F);
        s.sixteenBit = (flags & 4) != 0;
        const bool twoSides = (flags & 2) != 0;
        const uint32_t length = type == 1 ? r.u32(at + 0x10) : 0;
        if (pack != 0) {
            s.adpcm = true;  // DP30ADPCM or ModPlug ADPCM: not read
        } else if (length) {
            // Data at a paragraph given by 3 bytes: the high one, then a little-endian word.
            const size_t data = (size_t(r.u8(at + 0x0D)) << 20) | (size_t(r.u8(at + 0x0E)) << 4) |
                                (size_t(r.u8(at + 0x0F)) << 12);
            const size_t bytes = s.sixteenBit ? 2 : 1;
            const size_t want = size_t(length) * bytes * (twoSides ? 2 : 1);
            if (!r.has(data, want)) m.truncatedSamples = true;
            const size_t frames = std::min<size_t>(length, (r.size() - std::min(r.size(), data)) / bytes);
            auto value = [&](size_t k) {  // frame k of the left half, or of the right one (k ≥ length)
                const size_t p = data + k * bytes;
                int v = s.sixteenBit ? int16_t(r.u16(p)) : int(int8_t(r.u8(p))) * 256;
                if (!signedData) v = s.sixteenBit ? int(r.u16(p)) - 32768 : (int(r.u8(p)) - 128) * 256;
                return v;
            };
            s.data.reserve(frames);
            for (size_t k = 0; k < frames; ++k) {
                int v = value(k);
                // A stereo sample: both halves mixed, where the right one is in the file.
                if (twoSides && r.has(data + (length + k) * bytes, bytes)) v = (v + value(length + k)) / 2;
                s.data.push_back(int16_t(v));
            }
            const uint32_t begin = r.u32(at + 0x14), end = r.u32(at + 0x18);
            const uint32_t n = uint32_t(s.data.size());
            if ((flags & 1) && end > begin && begin < n) {
                s.loopType = 1;
                s.loopStart = begin;
                s.loopLength = std::min(end, n) - begin;
            }
        }
        ins.samples.push_back(std::move(s));
        m.instruments.push_back(std::move(ins));
    }

    // Patterns: 64 rows of packed cells after their length (a zero pointer: an empty pattern).
    for (int p = 0; p < patterns; ++p) {
        XmPattern pat;
        pat.rows = int(kRows);
        pat.cells.assign(kRows * size_t(m.channels), XmCell{});
        size_t at = size_t(r.u16(paraAt + size_t(samples + p) * 2)) * 16;
        if (at) {
            at += 2;
            XmCell spare;
            for (size_t row = 0; row < kRows;) {
                if (!r.has(at, 1)) return fail("Corrupt S3M module.");
                const uint8_t what = r.u8(at++);
                if (what == 0) {
                    ++row;
                    continue;
                }
                const size_t need = (what & 0x20 ? 2 : 0) + (what & 0x40 ? 1 : 0) + (what & 0x80 ? 2 : 0);
                if (!r.has(at, need)) return fail("Corrupt S3M module.");
                const int c = channelOf[size_t(what & 31)];
                XmCell& x = c >= 0 ? pat.cells[row * size_t(m.channels) + size_t(c)] : spare;
                if (what & 0x20) {
                    const uint8_t note = r.u8(at), octave = note >> 4, semitone = note & 15;
                    if (note == kNoteCut) x.note = kXmKeyOff;
                    else if (note != kNoteNone && octave <= 7 && semitone < 12) x.note = uint8_t(octave * 12 + semitone + 1);
                    x.instrument = r.u8(at + 1);
                    at += 2;
                }
                if (what & 0x40) {  // 0-64; 128-192: the pan of later trackers (XM volume column $Cx)
                    const uint8_t v = r.u8(at++);
                    x.volume = v >= 128 && v <= 192 ? uint8_t(0xC0 | std::min(15, (v - 128) * 15 / 64))
                                                    : uint8_t(0x10 + std::min<int>(v, 64));
                }
                if (what & 0x80) {
                    command(m, x, r.u8(at), r.u8(at + 1));
                    at += 2;
                }
            }
        }
        m.patterns.push_back(std::move(pat));
    }
    if (m.patterns.empty()) return fail("Corrupt S3M module.");
    for (uint8_t& o : m.order)
        if (o >= m.patterns.size()) o = 0;

    if (adlibChannels)
        m.notes.push_back("AdLib channels (no OPL chip): " + std::to_string(adlibChannels) + " left out.");
    if (adlibInstruments)
        m.notes.push_back("AdLib instruments (no OPL chip): " + std::to_string(adlibInstruments) + " left out.");
    XmParseResult out;
    out.module = std::move(m);
    return out;
}

}  // namespace stym::ymio
