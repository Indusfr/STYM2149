// STYM2149 — ymio: reading and writing maxYMiser songs (.MYS) and voicesets (.MYV)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/MaxYm.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace stym::ymio {

namespace {

constexpr char kMysMagic[] = "MYM0TRAK";
constexpr char kMyvMagic[] = "MYM1INST";
constexpr char kDigiMagic[] = "MYM1DIGI";
constexpr size_t kMysSong = 8 + 64;               // song steps
constexpr size_t kMysLength = kMysSong + 1024;    // song length, then repeat
constexpr size_t kMysPatterns = kMysLength + 2;   // first pattern row
constexpr size_t kMyvInst = 32 + 8;               // first instrument
constexpr size_t kMyvDigiInfo = kMyvInst + 2048;  // digidrum lengths
constexpr size_t kMyvSeq = kMyvDigiInfo + 32;     // first sequence
constexpr size_t kSeqBytes = 128;

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
MaxYmResult<T> fail(std::string e) {
    MaxYmResult<T> r;
    r.error = std::move(e);
    return r;
}

}  // namespace

MaxYmResult<MysSong> parseMys(std::span<const uint8_t> file) {
    if (file.size() < kMysPatterns || std::memcmp(file.data(), kMysMagic, 8) != 0)
        return fail<MysSong>("Not a maxYMiser song (.MYS).");
    MysSong s;
    std::copy_n(file.begin() + 8, 64, s.header.begin());
    s.speed = s.header[13];
    s.length = file[kMysLength];
    s.repeat = file[kMysLength + 1];
    for (size_t i = 0; i < kMysSteps; ++i) {
        const size_t at = kMysSong + 4 * i;
        s.steps[i].channel = {file[at], file[at + 1], file[at + 2]};
        s.steps[i].dma = file[at + 3];
    }
    // Rows of 8 bytes; the last byte counts the empty rows that follow, which must not run past the pattern's end.
    size_t at = kMysPatterns;
    int row = 0;
    while (at + 8 <= file.size()) {
        if (row == 0) {
            if (s.patterns.size() == kMysMaxPatterns) return fail<MysSong>("Too many patterns in the song.");
            s.patterns.emplace_back();
        }
        MysRow& r = s.patterns.back().rows[size_t(row)];
        r = {file[at], file[at + 1], file[at + 2], file[at + 3], file[at + 4], file[at + 5], file[at + 6]};
        row += 1 + file[at + 7];
        if (row > kMysRows) return fail<MysSong>("Corrupt pattern in the song.");
        if (row == kMysRows) row = 0;
        at += 8;
    }
    if (row != 0 || at != file.size()) return fail<MysSong>("The song file is cut.");
    if (s.speed == 0 || s.length == 0 || s.repeat >= s.length) return fail<MysSong>("Corrupt song header.");
    for (int i = 0; i < s.length; ++i)
        for (uint8_t p : s.steps[size_t(i)].channel)
            if (p < kMysMaxPatterns && p >= s.patterns.size())
                return fail<MysSong>("The song names a missing pattern.");
    for (const MysPattern& p : s.patterns)
        for (const MysRow& r : p.rows)
            if (r.instrument > kMyvInstruments || r.volume > 16) return fail<MysSong>("Corrupt pattern in the song.");
    return {s, {}};
}

MaxYmResult<MyvVoiceset> parseMyv(std::span<const uint8_t> file) {
    if (file.size() < kMyvSeq || std::memcmp(file.data() + 32, kMyvMagic, 8) != 0)
        return fail<MyvVoiceset>("Not a maxYMiser voiceset (.MYV).");
    MyvVoiceset v;
    for (size_t i = 0; i < kMyvInstruments; ++i) {
        const size_t at = kMyvInst + 64 * i;
        MyvInstrument& ins = v.instruments[i];
        for (size_t k = 0; k < 16 && file[at + k]; ++k) ins.name += char(file[at + k]);
        std::copy_n(file.begin() + long(at + 16), 48, ins.raw.begin());
    }
    // The sequences run up to the digidrum block, whose signature starts on a 128-byte boundary after them.
    size_t at = kMyvSeq;
    while (at + 8 <= file.size() && std::memcmp(file.data() + at, kDigiMagic, 8) != 0) {
        if (at + kSeqBytes > file.size()) return fail<MyvVoiceset>("The voiceset file is cut.");
        if (v.sequences.size() == kMyvMaxSequences) return fail<MyvVoiceset>("Too many sequences in the voiceset.");
        MyvSequence q;
        for (size_t k = 0; k < kMyvSequenceLength; ++k) q.data[k] = be16(file, at + 2 * k);
        q.length = file[at + 126];
        q.repeat = file[at + 127];
        v.sequences.push_back(q);
        at += kSeqBytes;
    }
    if (at + 8 > file.size()) return fail<MyvVoiceset>("The voiceset file is cut.");
    const size_t digi = at;
    for (size_t d = 0; d < kMyvDigidrums; ++d) {
        const uint32_t start = be32(file, 4 * d);
        const uint16_t len = be16(file, kMyvDigiInfo + 4 * d);
        if (len == 0) continue;
        if (start < digi + 8 || start > file.size() || len > file.size() - start)
            return fail<MyvVoiceset>("Corrupt digidrum in the voiceset.");
        v.digidrums[d].assign(file.begin() + long(start), file.begin() + long(start + len));
    }
    for (const MyvInstrument& ins : v.instruments)
        for (int k = 0; k < 8; ++k)
            if (ins.at(MyvInstrument::kSequences + k) >= std::max<size_t>(1, v.sequences.size()))
                return fail<MyvVoiceset>("An instrument names a missing sequence.");
    return {v, {}};
}

std::vector<uint8_t> writeMys(const MysSong& song) {
    std::vector<uint8_t> out(kMysPatterns, 0);
    std::memcpy(out.data(), kMysMagic, 8);
    std::copy(song.header.begin(), song.header.end(), out.begin() + 8);
    out[8 + 13] = uint8_t(std::clamp(song.speed, 1, 255));
    for (size_t i = 0; i < kMysSteps; ++i) {
        const size_t at = kMysSong + 4 * i;
        for (size_t c = 0; c < 3; ++c) out[at + c] = song.steps[i].channel[c];
        out[at + 3] = song.steps[i].dma;
    }
    out[kMysLength] = uint8_t(std::clamp(song.length, 1, kMysSteps - 1));
    out[kMysLength + 1] = uint8_t(std::clamp(song.repeat, 0, int(out[kMysLength]) - 1));
    // Each pattern starts with its first row, empty or not; every written row counts the empty rows after it.
    const size_t count = std::min<size_t>(song.patterns.size(), kMysMaxPatterns);
    for (size_t p = 0; p < count; ++p) {
        const auto& rows = song.patterns[p].rows;
        for (int r = 0; r < kMysRows;) {
            int next = r + 1;
            while (next < kMysRows && rows[size_t(next)].empty()) ++next;
            const MysRow& w = rows[size_t(r)];
            for (uint8_t b : {w.note, w.instrument, w.volume, w.effect1, w.param1, w.effect2, w.param2})
                out.push_back(b);
            out.push_back(uint8_t(next - r - 1));
            r = next;
        }
    }
    return out;
}

std::vector<uint8_t> writeMyv(const MyvVoiceset& voices) {
    const size_t sequences = std::clamp<size_t>(voices.sequences.size(), 1, kMyvMaxSequences);
    const size_t digi = kMyvSeq + kSeqBytes * sequences;
    std::vector<uint8_t> out(digi + 8, 0);
    std::memcpy(out.data() + 32, kMyvMagic, 8);
    for (size_t i = 0; i < kMyvInstruments; ++i) {
        const size_t at = kMyvInst + 64 * i;
        const MyvInstrument& ins = voices.instruments[i];
        std::memcpy(out.data() + at, ins.name.data(), std::min<size_t>(15, ins.name.size()));
        std::copy(ins.raw.begin(), ins.raw.end(), out.begin() + long(at + 16));
    }
    for (size_t k = 0; k < sequences; ++k) {
        const size_t at = kMyvSeq + kSeqBytes * k;
        const MyvSequence q = k < voices.sequences.size() ? voices.sequences[k] : MyvSequence{};
        for (size_t e = 0; e < kMyvSequenceLength; ++e) put16(out, at + 2 * e, q.data[e]);
        out[at + 126] = q.length;
        out[at + 127] = q.repeat;
    }
    std::memcpy(out.data() + digi, kDigiMagic, 8);
    // At least one byte per digidrum, 8 bytes in all for an empty voiceset.
    for (size_t d = 0; d < kMyvDigidrums; ++d) {
        const std::vector<uint8_t>& smp = voices.digidrums[d];
        const size_t len = std::min<size_t>(smp.size(), 0xFFFF);
        put32(out, 4 * d, uint32_t(out.size()));
        put16(out, kMyvDigiInfo + 4 * d, uint16_t(len));
        if (len == 0) out.push_back(0);
        else out.insert(out.end(), smp.begin(), smp.begin() + long(len));
    }
    return out;
}

namespace {

// Blocks of an SNDH saved by maxYMiser, at their signature.
struct Block {
    size_t at;
    char kind;  // 'I' voiceset, 'O' voiceset of the older layout, 'T' song, 'D' digidrums
};

std::vector<Block> blocksIn(std::span<const uint8_t> b) {
    static constexpr std::pair<const char*, char> kinds[] = {
        {kMyvMagic, 'I'}, {"MYM0INST", 'O'}, {kMysMagic, 'T'}, {kDigiMagic, 'D'}};
    std::vector<Block> out;
    for (size_t at = 0; at + 8 <= b.size(); ++at) {
        if (b[at] != 'M' || b[at + 1] != 'Y' || b[at + 2] != 'M') continue;
        for (const auto& [magic, kind] : kinds)
            if (std::memcmp(b.data() + at, magic, 8) == 0) out.push_back({at, kind});
    }
    return out;
}

// The voiceset whose signature is at `inst`, as a .MYV file; empty if it does not fit in the image.
std::vector<uint8_t> voicesetAt(std::span<const uint8_t> b, size_t inst, bool old, size_t end) {
    if (inst < 32 || inst - 32 + kMyvSeq > end) return {};
    const size_t v0 = inst - 32;
    std::vector<uint8_t> out(b.begin() + long(v0), b.begin() + long(v0 + kMyvSeq));
    std::memcpy(out.data() + 32, kMyvMagic, 8);
    // Older sequences: 31 words, length and repeat in 64 bytes, put in the 128 of a newer one.
    const size_t bytes = old ? 64 : kSeqBytes;
    const size_t count = std::min<size_t>((end - (v0 + kMyvSeq)) / bytes, kMyvMaxSequences);
    for (size_t k = 0; k < count; ++k) {
        const size_t at = v0 + kMyvSeq + bytes * k;
        const size_t words = old ? 62 : kSeqBytes - 2;
        out.insert(out.end(), b.begin() + long(at), b.begin() + long(at + words));
        out.resize(out.size() + (kSeqBytes - 2 - words), 0);
        out.insert(out.end(), b.begin() + long(at + bytes - 2), b.begin() + long(at + bytes));
    }
    out.insert(out.end(), kDigiMagic, kDigiMagic + 8);
    for (size_t d = 0; d < kMyvDigidrums; ++d) {
        const size_t start = v0 + be32(b, v0 + 4 * d);
        const size_t len = be16(b, v0 + kMyvDigiInfo + 4 * d);
        put32(out, 4 * d, uint32_t(out.size()));
        if (len == 0) out.push_back(0);
        else if (start > b.size() || len > b.size() - start) return {};
        else out.insert(out.end(), b.begin() + long(start), b.begin() + long(start + len));
    }
    return out;
}

// The song whose signature is at `trak`, as a .MYS file: its patterns up to `end`, or up to the zero bytes after the
// last one it plays.
std::vector<uint8_t> songAt(std::span<const uint8_t> b, size_t trak, size_t end) {
    if (trak + kMysPatterns > end) return {};
    const int length = b[trak + kMysLength];
    int used = 0;  // patterns the song names
    for (int i = 0; i < length; ++i)
        for (size_t c = 0; c < 3; ++c)
            if (const uint8_t p = b[trak + kMysSong + 4 * size_t(i) + c]; p < kMysMaxPatterns) used = std::max(used, p + 1);
    size_t at = trak + kMysPatterns;
    for (int patterns = 0; patterns < kMysMaxPatterns; ++patterns) {
        if (patterns >= used && std::all_of(b.begin() + long(at), b.begin() + long(end), [](uint8_t x) { return !x; }))
            break;
        size_t next = at;
        int row = 0;
        while (row < kMysRows && next + 8 <= end) {
            row += 1 + b[next + 7];
            next += 8;
        }
        if (row != kMysRows) break;  // cut, or what follows is not a pattern
        at = next;
    }
    return {b.begin() + long(trak), b.begin() + long(at)};
}

}  // namespace

std::vector<MaxYmSong> maxYmSongsIn(std::span<const uint8_t> image) {
    const std::vector<Block> blocks = blocksIn(image);
    std::vector<MaxYmSong> out;
    std::optional<MyvVoiceset> voices;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const size_t end = i + 1 < blocks.size() ? blocks[i + 1].at : image.size();
        const Block& k = blocks[i];
        if (k.kind == 'I' || k.kind == 'O') {
            voices = parseMyv(voicesetAt(image, k.at, k.kind == 'O', end)).value;
        } else if (k.kind == 'T' && voices) {
            if (std::optional<MysSong> song = parseMys(songAt(image, k.at, end)).value)
                out.push_back({std::move(*song), *voices});
        }
    }
    return out;
}

}  // namespace stym::ymio
