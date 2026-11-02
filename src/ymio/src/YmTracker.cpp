// STYM2149 — ymio: reading and writing YM-Tracker songs (.ymt)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/YmTracker.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "ymio/Gzip.h"

namespace stym::ymio {

namespace {

// Effect letters by code, from 1 (see the header).
constexpr std::string_view kEffects = "0123456789ABCDEFGHKLMPRTXYQSUZ";
constexpr size_t kMaxList = 4096;       // entries of a volume, arpeggio or pitch list
constexpr uint32_t kMaxDrum = 1u << 20;  // values of a digidrum

// Little-endian reads; a read past the end sets `cut` and gives zeros.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> b) : b_(b) {}
    bool cut = false;
    size_t at = 0;
    bool has(size_t n) const noexcept { return at <= b_.size() && n <= b_.size() - at; }
    uint8_t u8() noexcept {
        if (!has(1)) {
            cut = true;
            return 0;
        }
        return b_[at++];
    }
    uint16_t u16() noexcept {
        const uint8_t lo = u8();
        return uint16_t(lo | u8() << 8);
    }
    uint32_t u32() noexcept {
        const uint16_t lo = u16();
        return uint32_t(lo) | uint32_t(u16()) << 16;
    }
    std::span<const uint8_t> bytes(size_t n) noexcept {
        if (!has(n)) {
            cut = true;
            at = b_.size();
            return {};
        }
        const std::span<const uint8_t> s = b_.subspan(at, n);
        at += n;
        return s;
    }
    std::string text(size_t n) {
        const std::span<const uint8_t> s = bytes(n);
        return {s.begin(), s.end()};
    }

private:
    std::span<const uint8_t> b_;
};

class Writer {
public:
    std::vector<uint8_t> out;
    void u8(int v) { out.push_back(uint8_t(v)); }
    void u16(int v) {
        u8(v & 0xFF);
        u8((v >> 8) & 0xFF);
    }
    void u32(uint32_t v) {
        u16(int(v & 0xFFFF));
        u16(int(v >> 16));
    }
    void text16(const std::string& s) {
        const size_t n = std::min<size_t>(s.size(), 0xFFFF);
        u16(int(n));
        out.insert(out.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
    }
    void text8(const std::string& s) {
        const size_t n = std::min<size_t>(s.size(), 0xFF);
        u8(int(n));
        out.insert(out.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
    }
};

YmtParseResult refuse(std::string why) {
    YmtParseResult r;
    r.error = std::move(why);
    return r;
}

// The cells of a pattern, unpacked; false if they overrun or fall short of its bytes.
bool unpack(std::span<const uint8_t> data, int rows, int channels, YmtPattern& p, int& unknown) {
    Reader r(data);
    p.rows = rows;
    p.cells.assign(size_t(rows * channels), YmtCell{});
    for (YmtCell& c : p.cells) {
        const uint8_t f = r.u8();
        uint8_t code = 0;
        if (f & 0x80) {
            if (f & 1) c.note = r.u8();
            if (f & 2) c.instrument = r.u8();
            if (f & 4) c.volume = r.u8();
            if (f & 8) code = r.u8();
            if (f & 16) c.param = r.u8();
        } else {
            c.note = f;
            c.instrument = r.u8();
            c.volume = r.u8();
            code = r.u8();
            c.param = r.u8();
        }
        c.effect = ymtEffectLetter(code);
        unknown += code && !c.effect;
    }
    return !r.cut && r.at == data.size();
}

void pack(const YmtPattern& p, Writer& w) {
    for (const YmtCell& c : p.cells) {
        const uint8_t code = ymtEffectCode(c.effect);
        const int f = 0x80 | (c.note ? 1 : 0) | (c.instrument ? 2 : 0) | (c.volume ? 4 : 0) | (code ? 8 : 0) |
                      (c.param ? 16 : 0);
        w.u8(f);
        if (c.note) w.u8(c.note);
        if (c.instrument) w.u8(c.instrument);
        if (c.volume) w.u8(c.volume);
        if (code) w.u8(code);
        if (c.param) w.u8(c.param);
    }
}

bool readInstrument(Reader& r, YmtInstrument& in) {
    in.name = r.text(r.u16());
    in.useAdsr = r.u8() != 0;
    in.attack = r.u8();
    in.decay = r.u8();
    in.sustain = r.u8();
    in.release = r.u8();
    in.hardEnv = r.u8() != 0;
    in.shape = r.u8();
    in.envPeriod = r.u16();
    in.autoPeriod = r.u8() != 0;
    in.octave = r.u8();
    in.syncBuzzer = r.u8() != 0;
    in.noise = r.u8() != 0;
    in.noisePeriod = r.u8();
    in.unknown14 = r.u8();
    in.tone = r.u8() != 0;
    in.arpSpeed = r.u8();
    in.vibSpeed = r.u8();
    in.vibDepth = r.u8();
    for (uint8_t& b : in.unknown19) b = r.u8();
    in.volumeLoop = int16_t(r.u16());
    size_t n = r.u16();
    if (n > kMaxList) return false;
    const std::span<const uint8_t> vol = r.bytes(n);
    in.volume.assign(vol.begin(), vol.end());
    in.word1 = r.u16();
    n = r.u16();
    if (n > kMaxList) return false;
    in.arp.clear();
    for (size_t k = 0; k < n; ++k) in.arp.push_back(int8_t(r.u8()));
    n = r.u16();
    if (n > kMaxList) return false;
    in.pitch.clear();
    for (size_t k = 0; k < n; ++k) in.pitch.push_back(int16_t(r.u16()));
    in.word2 = r.u16();
    n = r.u16();
    if (n > 255) return false;
    in.drums.clear();
    for (size_t k = 0; k < n && !r.cut; ++k) {
        YmtDrum& d = in.drums.emplace_back();
        d.name = r.text(r.u8());
        d.rate = r.u16();
        const uint32_t count = r.u32();
        if (count > kMaxDrum) return false;
        const std::span<const uint8_t> packed = r.bytes((size_t(count) + 1) / 2);
        if (r.cut) return false;
        d.levels.resize(count);
        for (uint32_t i = 0; i < count; ++i) d.levels[i] = uint8_t(i & 1 ? packed[i / 2] >> 4 : packed[i / 2] & 15);
    }
    return !r.cut;
}

void writeInstrument(Writer& w, const YmtInstrument& in) {
    w.text16(in.name);
    for (const int v : {int(in.useAdsr), int(in.attack), int(in.decay), int(in.sustain), int(in.release),
                        int(in.hardEnv), int(in.shape)})
        w.u8(v);
    w.u16(in.envPeriod);
    for (const int v : {int(in.autoPeriod), int(in.octave), int(in.syncBuzzer), int(in.noise), int(in.noisePeriod),
                        int(in.unknown14), int(in.tone), int(in.arpSpeed), int(in.vibSpeed), int(in.vibDepth)})
        w.u8(v);
    for (const uint8_t b : in.unknown19) w.u8(b);
    w.u16(uint16_t(in.volumeLoop));
    const size_t nv = std::min(in.volume.size(), kMaxList);
    w.u16(int(nv));
    for (size_t k = 0; k < nv; ++k) w.u8(in.volume[k]);
    w.u16(in.word1);
    const size_t na = std::min(in.arp.size(), kMaxList);
    w.u16(int(na));
    for (size_t k = 0; k < na; ++k) w.u8(uint8_t(in.arp[k]));
    const size_t np = std::min(in.pitch.size(), kMaxList);
    w.u16(int(np));
    for (size_t k = 0; k < np; ++k) w.u16(uint16_t(in.pitch[k]));
    w.u16(in.word2);
    const size_t nd = std::min<size_t>(in.drums.size(), 255);
    w.u16(int(nd));
    for (size_t k = 0; k < nd; ++k) {
        const YmtDrum& d = in.drums[k];
        w.text8(d.name);
        w.u16(d.rate);
        const uint32_t count = uint32_t(std::min<size_t>(d.levels.size(), kMaxDrum));
        w.u32(count);
        for (uint32_t i = 0; i < count; i += 2)
            w.u8((d.levels[i] & 15) | (i + 1 < count ? (d.levels[i + 1] & 15) << 4 : 0));
    }
}

}  // namespace

char ymtEffectLetter(uint8_t code) noexcept {
    return code >= 1 && code <= kEffects.size() ? kEffects[code - 1] : 0;
}

uint8_t ymtEffectCode(char letter) noexcept {
    if (!letter) return 0;
    const size_t k = kEffects.find(letter);
    return k == std::string_view::npos ? 0 : uint8_t(k + 1);
}

YmtParseResult parseYmt(std::span<const uint8_t> file) {
    if (file.size() < 8 || std::memcmp(file.data(), "YMT1", 4) != 0) return refuse("Not a YM-Tracker song.");
    const size_t offset = size_t(file[6] | file[7] << 8);
    if (offset < 8 || offset >= file.size()) return refuse("Not a YM-Tracker song.");
    GunzipResult unpacked = gunzip(file.subspan(offset), kYmtMaxBytes);
    if (!unpacked.data) return refuse(unpacked.error);
    Reader r(*unpacked.data);
    YmtSong s;
    s.title = r.text(r.u16());
    s.bpm = r.u16();
    s.speed = r.u8();
    s.chips = r.u8();
    s.restart = r.u16();
    const int channels = r.u8();
    s.toneChannels = r.u8();
    const size_t orderLength = r.u16();
    const int patterns = r.u16();
    const int instruments = r.u16();
    s.drumChannel = r.u8() != 0;
    if (r.cut) return refuse("The song is cut.");
    if (channels < 1 || channels > kYmtMaxChannels) return refuse("Unsupported number of channels.");
    if (patterns > kYmtMaxPatterns || instruments > kYmtMaxInstruments) return refuse("The song is damaged.");
    for (int c = 0; c < channels; ++c) s.channelNames.push_back(r.text(r.u8()));
    const std::span<const uint8_t> order = r.bytes(orderLength);
    s.order.assign(order.begin(), order.end());
    for (int p = 0; p < patterns && !r.cut; ++p) {
        const int rows = r.u16();
        const uint32_t size = r.u32();
        const std::span<const uint8_t> data = r.bytes(size);
        if (r.cut) break;
        if (rows < 1 || rows > kYmtMaxRows) return refuse("Unsupported pattern length.");
        if (!unpack(data, rows, channels, s.patterns.emplace_back(), s.unknownEffects))
            return refuse("A pattern of the song is damaged.");
    }
    for (int i = 0; i < instruments && !r.cut; ++i)
        if (!readInstrument(r, s.instruments.emplace_back())) return refuse("An instrument of the song is damaged.");
    if (r.cut) return refuse("The song is cut.");
    for (uint8_t& o : s.order)
        if (o >= s.patterns.size()) o = 0;
    YmtParseResult out;
    out.song = std::move(s);
    return out;
}

std::vector<uint8_t> writeYmt(const YmtSong& s) {
    Writer w;
    w.text16(s.title);
    w.u16(s.bpm);
    w.u8(s.speed);
    w.u8(s.chips);
    w.u16(s.restart);
    w.u8(s.channels());
    w.u8(s.toneChannels);
    w.u16(int(s.order.size()));
    w.u16(int(s.patterns.size()));
    w.u16(int(s.instruments.size()));
    w.u8(s.drumChannel ? 1 : 0);
    for (const std::string& n : s.channelNames) w.text8(n);
    for (const uint8_t o : s.order) w.u8(o);
    for (const YmtPattern& p : s.patterns) {
        Writer cells;
        pack(p, cells);
        w.u16(p.rows);
        w.u32(uint32_t(cells.out.size()));
        w.out.insert(w.out.end(), cells.out.begin(), cells.out.end());
    }
    for (const YmtInstrument& in : s.instruments) writeInstrument(w, in);
    std::vector<uint8_t> file = {'Y', 'M', 'T', '1', 1, 1, 8, 0};
    const std::vector<uint8_t> gz = gzipStore(w.out);
    file.insert(file.end(), gz.begin(), gz.end());
    return file;
}

}  // namespace stym::ymio
