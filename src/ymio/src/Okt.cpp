// STYM2149 — ymio: reading Oktalyzer modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/Okt.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "ymio/Mod.h"

namespace stym::ymio {

namespace {

constexpr size_t kSampleHeader = 32, kCellBytes = 4;
constexpr int kMaxSamples = 36, kMaxOrders = 128, kMaxPatterns = 256, kMaxRows = 256;
// XM effects written by the reader (the ones the import renders).
constexpr uint8_t kFxVolSlide = 0xA, kFxJump = 0xB, kFxVolume = 0xC, kFxSpeed = 0xF;
// Oktalyzer effects (shown in base 36: 25 is P, 28 S, 31 V).
constexpr uint8_t kOkJump = 25, kOkRelease = 27, kOkSpeed = 28, kOkVolume = 31;

// Big-endian reads that return 0 beyond the end of the file; the parser checks each block with has() first.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> b) : b_(b) {}
    size_t size() const noexcept { return b_.size(); }
    bool has(size_t at, size_t n) const noexcept { return at <= b_.size() && n <= b_.size() - at; }
    uint8_t u8(size_t at) const noexcept { return has(at, 1) ? b_[at] : 0; }
    uint16_t u16(size_t at) const noexcept { return uint16_t(u8(at) << 8 | u8(at + 1)); }
    uint32_t u32(size_t at) const noexcept { return uint32_t(u16(at)) << 16 | u16(at + 2); }
    bool is(size_t at, const char* four) const noexcept {
        return has(at, 4) && std::memcmp(b_.data() + at, four, 4) == 0;
    }
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

// A chunk: where its data start and how many bytes the file holds of them.
struct Chunk {
    size_t at = 0, size = 0;
};

// One Oktalyzer effect in the XM shape: the effects the import renders become their XM effects, the others keep their
// Oktalyzer name (XmCell::foreign).
void effect(XmModule& m, XmCell& x, uint8_t fx, uint8_t param) {
    static constexpr char kDigits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    auto xm = [&x](uint8_t e, int p) {
        x.effect = e;
        x.param = uint8_t(p);
    };
    switch (fx) {
    case 0:
        return;
    case kOkJump:
        xm(kFxJump, param);
        return;
    case kOkSpeed:  // ticks per line (S00 and 32 and up: nothing)
        if (param && param < 0x20) xm(kFxSpeed, param);
        return;
    case kOkRelease:  // the sample's release: the note ends
        x.note = kXmKeyOff;
        x.instrument = 0;
        return;
    case kOkVolume:  // 00-40 volume; 4x slide down, 5x slide up, 6x and 7x fine slides (once a line, not rendered)
        if (param <= 0x40) xm(kFxVolume, param);
        else if (param < 0x50) xm(kFxVolSlide, param & 15);
        else if (param < 0x60) {
            if (param & 15) xm(kFxVolSlide, (param & 15) << 4);
        } else if (param < 0x70) x.foreign = m.foreignEffect("V6x");
        else if (param < 0x80) x.foreign = m.foreignEffect("V7x");
        return;
    default:
        if (fx < 36) x.foreign = m.foreignEffect(std::string{kDigits[fx], 'x', 'x'});
        return;
    }
}

}  // namespace

bool looksLikeOkt(std::span<const uint8_t> file) noexcept {
    return file.size() >= 8 && std::memcmp(file.data(), "OKTASONG", 8) == 0;
}

XmParseResult parseOkt(std::span<const uint8_t> file) {
    const Reader r(file);
    if (!looksLikeOkt(file)) return fail("Not an OKT module.");
    // The chunks, in the order of the file; a chunk cut by the end of the file keeps what the file holds (only the
    // sample data may be cut).
    Chunk cmod, samp, spee, plen, patt;
    std::vector<Chunk> pbod, sbod;
    bool cut = false;
    for (size_t at = 8; r.has(at, 8);) {
        Chunk c{at + 8, r.u32(at + 4)};
        if (!r.has(c.at, c.size)) {
            c.size = r.size() - c.at;
            cut = true;
        }
        if (r.is(at, "CMOD")) cmod = c;
        else if (r.is(at, "SAMP")) samp = c;
        else if (r.is(at, "SPEE")) spee = c;
        else if (r.is(at, "PLEN")) plen = c;
        else if (r.is(at, "PATT")) patt = c;
        else if (r.is(at, "PBOD")) {
            if (cut) return fail("Corrupt OKT module.");
            if (pbod.size() < size_t(kMaxPatterns)) pbod.push_back(c);
        } else if (r.is(at, "SBOD") && c.size > 0) {
            sbod.push_back(c);
        }
        at = c.at + c.size;
    }
    if (cmod.size < 8 || samp.size < kSampleHeader || patt.size == 0 || plen.size < 2 || pbod.empty())
        return fail("Corrupt OKT module.");

    XmModule m;
    m.tracker = "Oktalyzer";
    m.c4Hz = kAmigaPalClock / kModC2Period;
    m.samplePanning = false;
    m.tempo = spee.size >= 2 ? std::clamp<int>(r.u16(spee.at), 1, 255) : 6;
    m.bpm = 125;  // a tick per frame of the PAL Amiga: 50 Hz

    // Channels: each Amiga channel, or its two mixed voices when it is split; the Amiga's L R R L.
    std::vector<bool> split;
    for (size_t c = 0; c < 4; ++c) {
        const bool two = r.u16(cmod.at + c * 2) != 0;
        for (int v = 0; v < (two ? 2 : 1); ++v) {
            m.channelPan.push_back(c == 1 || c == 2 ? 255 : 0);
            split.push_back(two);
        }
    }
    m.channels = int(split.size());

    // Order table: the song length's first entries.
    const size_t length = std::min<size_t>({r.u16(plen.at), patt.size, size_t(kMaxOrders)});
    for (size_t i = 0; i < length; ++i) m.order.push_back(r.u8(patt.at + i));
    if (m.order.empty()) return fail("Corrupt OKT module.");

    // Patterns: rows, then 4 bytes per cell, channel after channel.
    for (const Chunk& c : pbod) {
        XmPattern pat;
        pat.rows = c.size >= 2 ? std::clamp<int>(r.u16(c.at), 1, kMaxRows) : 64;
        pat.cells.assign(size_t(pat.rows) * size_t(m.channels), XmCell{});
        if (c.size >= 2) {
            if (c.size < 2 + pat.cells.size() * kCellBytes) return fail("Corrupt OKT module.");
            for (size_t k = 0; k < pat.cells.size(); ++k) {
                const size_t at = c.at + 2 + k * kCellBytes;
                XmCell& x = pat.cells[k];
                const uint8_t note = r.u8(at);
                if (note >= 1 && note <= 36) {
                    x.note = uint8_t(note + 36);  // note 13, C-2: the XM C-4
                    x.instrument = uint8_t(r.u8(at + 1) + 1);
                    // A split channel plays its notes at full volume, the sample's volume aside.
                    if (split[k % size_t(m.channels)]) x.volume = 0x10 + 64;
                }
                effect(m, x, r.u8(at + 2), r.u8(at + 3));
            }
        }
        m.patterns.push_back(std::move(pat));
    }
    for (uint8_t& o : m.order)
        if (o >= m.patterns.size()) o = 0;

    // Samples: 32-byte headers; the data of each sample that has a length, in order, one SBOD each.
    const size_t samples = std::min<size_t>(samp.size / kSampleHeader, kMaxSamples);
    size_t data = 0;
    for (size_t i = 0; i < samples; ++i) {
        const size_t h = samp.at + i * kSampleHeader;
        XmInstrument ins;
        ins.name = r.text(h, 20);
        XmSample s;
        s.name = ins.name;
        const uint32_t bytes = r.u32(h + 20) & ~1u;
        s.volume = uint8_t(std::min<int>(64, r.u16(h + 28)));
        s.panning = 128;
        if (bytes) {
            if (data < sbod.size()) {
                const Chunk& c = sbod[data++];
                const size_t n = std::min<size_t>(bytes, c.size);
                m.truncatedSamples = m.truncatedSamples || (n < bytes && cut);
                s.data.reserve(n);
                for (size_t k = 0; k < n; ++k) s.data.push_back(int16_t(int8_t(r.u8(c.at + k)) * 256));
            } else {
                m.truncatedSamples = true;
            }
            const uint32_t start = uint32_t(r.u16(h + 24)) * 2, len = uint32_t(r.u16(h + 26)) * 2;
            const uint32_t frames = uint32_t(s.data.size());
            if (len > 2 && start < frames) {
                s.loopType = 1;
                s.loopStart = start;
                s.loopLength = std::min(len, frames - start);
            }
        }
        ins.samples.push_back(std::move(s));
        m.instruments.push_back(std::move(ins));
    }
    XmParseResult out;
    out.module = std::move(m);
    return out;
}

}  // namespace stym::ymio
