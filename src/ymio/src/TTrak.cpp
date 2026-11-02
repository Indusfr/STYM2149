// STYM2149 — ymio: reading and writing TTrak modules (.TPM), instruments (.TPI) and 4-bit samples (.4BI)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/TTrak.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace stym::ymio {

namespace {

constexpr char kModuleMagic[] = "GODMOD30";
constexpr char kInstrumentMagic[] = "GODINS30";
constexpr size_t kTitleAt = 0x08, kAuthorAt = 0x18, kLengthAt = 0x28, kRestartAt = 0x29, kRateAt = 0x2A,
                 kReservedAt = 0x2B, kGrooveAt = 0x30, kInstrumentTableAt = 0x40, kSampleTableAt = 0x82,
                 kOrderAt = 0x106, kInstrumentsAt = 0x206, kPatternsAt = 0x84C6;
constexpr size_t kRowBytes = 16;

uint16_t be16(std::span<const uint8_t> b, size_t at) { return uint16_t(b[at] << 8 | b[at + 1]); }
uint32_t be32(std::span<const uint8_t> b, size_t at) { return uint32_t(be16(b, at)) << 16 | be16(b, at + 2); }
void put16(std::vector<uint8_t>& out, size_t at, uint16_t v) {
    out[at] = uint8_t(v >> 8);
    out[at + 1] = uint8_t(v);
}
void put32(std::vector<uint8_t>& out, size_t at, uint32_t v) {
    put16(out, at, uint16_t(v >> 16));
    put16(out, at + 2, uint16_t(v));
}

template <class T>
TtrakResult<T> fail(std::string e) {
    TtrakResult<T> r;
    r.error = std::move(e);
    return r;
}

std::string padded(std::span<const uint8_t> text) {
    std::string s(text.begin(), text.end());
    const size_t nul = s.find('\0');
    if (nul != std::string::npos) s.resize(nul);
    while (!s.empty() && (s.back() == '-' || s.back() == ' ')) s.pop_back();
    return s;
}

void setPadded(std::span<uint8_t> out, std::string_view text) {
    std::fill(out.begin(), out.end(), uint8_t('-'));
    const size_t n = std::min(out.size(), text.size());
    for (size_t i = 0; i < n; ++i) {
        const auto c = uint8_t(text[i]);
        out[i] = c >= 0x20 && c < 0x7F ? c : uint8_t('?');
    }
}

TtrakPattern readPattern(std::span<const uint8_t> b) {
    TtrakPattern p;
    for (size_t r = 0; r < size_t(kTtrakRows); ++r) {
        const size_t at = r * kRowBytes;
        for (size_t c = 0; c < size_t(kTtrakChannels); ++c)
            p.rows[r][c] = {b[at + 4 * c], b[at + 4 * c + 1], b[at + 4 * c + 2], b[at + 4 * c + 3]};
        std::copy_n(b.begin() + long(at + 12), 4, p.spare[r].begin());
    }
    return p;
}

void writePattern(std::vector<uint8_t>& out, const TtrakPattern& p) {
    for (size_t r = 0; r < size_t(kTtrakRows); ++r) {
        for (const TtrakCell& c : p.rows[r]) out.insert(out.end(), {c.note, c.instrument, c.effect, c.param});
        out.insert(out.end(), p.spare[r].begin(), p.spare[r].end());
    }
}

}  // namespace

std::string TtrakInstrument::name() const { return padded(std::span(raw).subspan(size_t(kName), size_t(kNameSize))); }

void TtrakInstrument::setName(std::string_view n) {
    setPadded(std::span(raw).subspan(size_t(kName), size_t(kNameSize)), n);
}

TtrakInstrument TtrakInstrument::blank(int slot) {
    TtrakInstrument t;
    // Where the tables sit inside the record.
    t.setWord(kArpTableAt, kArpTable);
    t.setWord(kNoiseTableAt, kNoiseTable);
    t.setWord(kTimerTableAt, kTimerTable);
    t.setWord(kFmWaveAt, kFmWave);
    t.setWord(kMorphEnvAt, kMorphEnv);
    t.setWord(kMorphWavesAt, kMorphWaves);
    // Frequency modulation and morph run through their whole tables, one step per tick.
    t.at(kFmSpeed) = 1;
    t.at(kFmDepth) = 0xFF;
    t.setWord(kFmEnd, 0x200);
    t.at(kMorphSpeed) = 1;
    t.setWord(kMorphEnd, 0x100);
    t.at(kSampleSlot) = uint8_t(std::clamp(slot, 0, 255));
    // Arpeggio: one step of 0, looped. Noise: one step with the noise off, looped. ($Fx: back to step x.)
    for (int i = 2; i < 16; ++i) t.at(kArpTable + i) = 0xF0;
    t.at(kNoiseTable + 1) = 0x20;
    for (int i = 2; i < 16; ++i) t.at(kNoiseTable + i) = 0xF0;
    // Morph waves: a sine and a ramp, so that a morph is heard at once.
    for (int i = 0; i < 16; ++i) {
        t.at(kMorphWaves + i) = uint8_t(std::lround(7.5 + 7.5 * std::sin(6.283185307179586 * i / 16.0)));
        t.at(kMorphWaves + 16 + i) = uint8_t(i);
    }
    // The editor's two node lists (frequency modulation wave, morph envelope): a flat line through four points.
    static constexpr uint8_t kNodes[] = {0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x55, 0x00, 0x5F, 0x00,
                                         0xAA, 0x00, 0x5F, 0x00, 0xFF, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x30, 0x00, 0x55,
                                         0x00, 0x30, 0x00, 0xAA, 0x00, 0x30, 0x00, 0xFF, 0x00, 0x30};
    t.setWord(kEditor, 0x0002);
    std::copy(std::begin(kNodes), std::end(kNodes), t.raw.begin() + kEditor + 2);
    t.setWord(kEditor + 38, 0x0000);
    std::copy(std::begin(kNodes), std::end(kNodes), t.raw.begin() + kEditor + 40);
    t.setName("");
    return t;
}

std::string TtrakModule::titleText() const { return padded(title); }
std::string TtrakModule::authorText() const { return padded(author); }
void TtrakModule::setTitle(std::string_view t) { setPadded(title, t); }
void TtrakModule::setAuthor(std::string_view a) { setPadded(author, a); }

TtrakModule TtrakModule::blank() {
    TtrakModule m;
    m.setTitle("");
    m.setAuthor("");
    for (int k = 0; k < kTtrakInstruments; ++k) m.instruments[size_t(k)] = TtrakInstrument::blank(k + 1);
    m.patterns.resize(1);
    return m;
}

int ttrakPatternCount(const std::array<uint8_t, kTtrakPositions>& order, int length) noexcept {
    // As TTrak counts them: positions 0 to the song length included, pattern numbers compared as signed bytes.
    const int last = std::clamp(length, 0, kTtrakPositions - 1);
    int high = 0;
    for (int i = 0; i <= last; ++i) high = std::max(high, int(int8_t(order[size_t(i)])));
    return high + 1;
}

TtrakResult<TtrakModule> parseTtrakModule(std::span<const uint8_t> file) {
    if (!isTtrakModule(file)) return fail<TtrakModule>("Not a TTrak module (.TPM).");
    if (file.size() < kPatternsAt) return fail<TtrakModule>("The TTrak module is cut.");
    TtrakModule m;
    std::copy_n(file.begin() + kTitleAt, 16, m.title.begin());
    std::copy_n(file.begin() + kAuthorAt, 16, m.author.begin());
    m.length = file[kLengthAt];
    m.restart = file[kRestartAt];
    m.rate = file[kRateAt];
    std::copy_n(file.begin() + kReservedAt, m.reserved.size(), m.reserved.begin());
    std::copy_n(file.begin() + kGrooveAt, 16, m.groove.begin());
    std::copy_n(file.begin() + kOrderAt, kTtrakPositions, m.order.begin());
    for (size_t k = 0; k < size_t(kTtrakInstruments); ++k)
        std::copy_n(file.begin() + long(kInstrumentsAt + k * kTtrakInstrumentSize), kTtrakInstrumentSize,
                    m.instruments[k].raw.begin());
    // The instrument table must point at the records where TTrak saves them.
    for (size_t k = 0; k <= size_t(kTtrakInstruments); ++k) {
        const size_t expect = kInstrumentsAt - kInstrumentTableAt + (k == 0 ? 0 : k - 1) * kTtrakInstrumentSize;
        if (be16(file, kInstrumentTableAt + 2 * k) != expect) return fail<TtrakModule>("Corrupt TTrak module header.");
    }
    const auto count = size_t(ttrakPatternCount(m.order, m.length));
    const size_t samplesAt = kPatternsAt + count * kTtrakPatternSize;
    if (file.size() < samplesAt) return fail<TtrakModule>("The TTrak module is cut.");
    m.patterns.reserve(count);
    for (size_t p = 0; p < count; ++p)
        m.patterns.push_back(readPattern(file.subspan(kPatternsAt + p * kTtrakPatternSize, kTtrakPatternSize)));
    std::array<uint32_t, kTtrakSampleSlots + 1> at{};
    for (size_t s = 0; s <= size_t(kTtrakSampleSlots); ++s) at[s] = be32(file, kSampleTableAt + 4 * s);
    if (at[0] != 0 || !std::is_sorted(at.begin(), at.end())) return fail<TtrakModule>("Corrupt TTrak sample table.");
    if (file.size() - samplesAt < at[kTtrakSampleSlots]) return fail<TtrakModule>("The TTrak module is cut.");
    for (size_t s = 0; s < size_t(kTtrakSampleSlots); ++s) {
        m.samples[s].assign(file.begin() + long(samplesAt + at[s]), file.begin() + long(samplesAt + at[s + 1]));
        if (m.samples[s].size() == 1 && m.samples[s][0] == 0x80) m.samples[s].clear();
    }
    return {std::move(m), {}};
}

std::vector<uint8_t> writeTtrakModule(const TtrakModule& m) {
    std::vector<uint8_t> out(kPatternsAt, 0);
    std::memcpy(out.data(), kModuleMagic, 8);
    std::copy(m.title.begin(), m.title.end(), out.begin() + kTitleAt);
    std::copy(m.author.begin(), m.author.end(), out.begin() + kAuthorAt);
    out[kLengthAt] = m.length;
    out[kRestartAt] = m.restart;
    out[kRateAt] = m.rate;
    std::copy(m.reserved.begin(), m.reserved.end(), out.begin() + kReservedAt);
    std::copy(m.groove.begin(), m.groove.end(), out.begin() + kGrooveAt);
    for (size_t k = 0; k <= size_t(kTtrakInstruments); ++k)
        put16(out, kInstrumentTableAt + 2 * k,
              uint16_t(kInstrumentsAt - kInstrumentTableAt + (k == 0 ? 0 : k - 1) * kTtrakInstrumentSize));
    std::copy(m.order.begin(), m.order.end(), out.begin() + kOrderAt);
    for (size_t k = 0; k < size_t(kTtrakInstruments); ++k)
        std::copy(m.instruments[k].raw.begin(), m.instruments[k].raw.end(),
                  out.begin() + long(kInstrumentsAt + k * kTtrakInstrumentSize));
    const auto count = size_t(ttrakPatternCount(m.order, m.length));
    out.reserve(out.size() + count * kTtrakPatternSize);
    for (size_t p = 0; p < count; ++p) writePattern(out, p < m.patterns.size() ? m.patterns[p] : TtrakPattern{});
    uint32_t at = 0;
    for (size_t s = 0; s < size_t(kTtrakSampleSlots); ++s) {
        put32(out, kSampleTableAt + 4 * s, at);
        if (m.samples[s].empty()) out.push_back(0x80);
        else out.insert(out.end(), m.samples[s].begin(), m.samples[s].end());
        at += uint32_t(std::max<size_t>(1, m.samples[s].size()));
    }
    put32(out, kSampleTableAt + 4 * size_t(kTtrakSampleSlots), at);
    return out;
}

TtrakResult<TtrakInstrumentFile> parseTtrakInstrument(std::span<const uint8_t> file) {
    if (!isTtrakInstrument(file)) return fail<TtrakInstrumentFile>("Not a TTrak instrument (.TPI).");
    if (file.size() < 8 + kTtrakInstrumentSize) return fail<TtrakInstrumentFile>("The TTrak instrument is cut.");
    TtrakInstrumentFile f;
    std::copy_n(file.begin() + 8, kTtrakInstrumentSize, f.instrument.raw.begin());
    f.sample.assign(file.begin() + long(8 + kTtrakInstrumentSize), file.end());
    if (f.sample.size() == 1 && f.sample[0] == 0x80) f.sample.clear();
    return {std::move(f), {}};
}

std::vector<uint8_t> writeTtrakInstrument(const TtrakInstrumentFile& f) {
    const size_t sample = std::max<size_t>(1, f.sample.size());
    std::vector<uint8_t> out(8 + kTtrakInstrumentSize + sample, 0x80);
    std::memcpy(out.data(), kInstrumentMagic, 8);
    std::memcpy(out.data() + 8, f.instrument.raw.data(), kTtrakInstrumentSize);
    if (!f.sample.empty()) std::memcpy(out.data() + 8 + kTtrakInstrumentSize, f.sample.data(), f.sample.size());
    return out;
}

std::vector<uint8_t> ttrakSampleLevels(std::span<const uint8_t> stored) {
    std::vector<uint8_t> levels;
    if (stored.empty() || (stored.size() == 1 && stored[0] == 0x80)) return levels;
    for (uint8_t b : stored) {
        levels.push_back(b & 0x0F);
        if (b & 0x80) break;
    }
    return levels;
}

std::vector<uint8_t> ttrakSampleBytes(std::span<const uint8_t> levels) {
    std::vector<uint8_t> out;
    out.reserve(levels.size());
    for (uint8_t v : levels) out.push_back(v & 0x0F);
    if (!out.empty()) out.back() |= 0x80;
    return out;
}

std::vector<uint8_t> parseTtrak4bi(std::span<const uint8_t> file) {
    std::vector<uint8_t> out(file.begin(), file.end());
    if (!out.empty()) out.back() |= 0x80;
    return out;
}

bool isTtrakModule(std::span<const uint8_t> file) noexcept {
    return file.size() >= 8 && std::memcmp(file.data(), kModuleMagic, 8) == 0;
}

bool isTtrakInstrument(std::span<const uint8_t> file) noexcept {
    return file.size() >= 8 && std::memcmp(file.data(), kInstrumentMagic, 8) == 0;
}

}  // namespace stym::ymio
