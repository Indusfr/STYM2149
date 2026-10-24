// STYM2149 — plugincore: non-automatable state and binary format of the plugin state
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/State.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <string_view>

#include "plugincore/Instruments.h"
#include "ymengine/Engine.h"
#include "ymseq/Tables.h"

namespace stym::plugin {
namespace {

using namespace ymengine;

// Chunk tag: the four characters as a little-endian u32, so that they appear in this order in the file.
constexpr uint32_t fourcc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}
constexpr uint32_t kParm = fourcc("PARM"), kTrkx = fourcc("TRKX"), kGlbx = fourcc("GLBX"), kKit = fourcc("KIT "),
                   kDigi = fourcc("DIGI"), kName = fourcc("NAME"), kSeqs = fourcc("SEQS"), kPatn = fourcc("PATN"),
                   kSong = fourcc("SONG"), kInst = fourcc("INST"), kTseq = fourcc("TSEQ"),
                   kTimg = fourcc("TIMG"), kDmas = fourcc("DMAS"), kDigs = fourcc("DIGS"),
                   kRecd = fourcc("RECD"), kSngx = fourcc("SNGX"), kRsyn = fourcc("RSYN");

// Ranges of the kit settings (KITSPEC of the prototype), in the order of the fields a, b, dec, noise, vol of
// DrumParams. The families without a noise setting (snare, clap, hats) keep it at 0-100, with no effect on the sound.
struct Range {
    double lo, hi;
};
constexpr std::array<std::array<Range, 5>, kDrumSlots> kKitRanges = {{
    {{{25, 90}, {20, 300}, {4, 20}, {0, 100}, {0, 100}}},    // kick: pitch, sweep, dec, noise, vol
    {{{80, 400}, {0, 70}, {6, 30}, {0, 100}, {0, 100}}},     // snare: body, tone, dec
    {{{6, 20}, {15, 80}, {10, 60}, {0, 100}, {0, 100}}},     // clap: space, color, tail
    {{{0, 100}, {60, 160}, {15, 120}, {0, 100}, {0, 100}}},  // hat: metal, tune, dec
    {{{0, 100}, {60, 160}, {3, 30}, {0, 100}, {0, 100}}},    // ohat
    {{{0, 100}, {60, 160}, {30, 250}, {0, 100}, {0, 100}}},  // chat
    {{{60, 200}, {20, 150}, {4, 20}, {0, 100}, {0, 100}}},   // tom: pitch, sweep, dec, noise
    {{{80, 260}, {20, 150}, {4, 20}, {0, 100}, {0, 100}}},   // tom2
}};

// Arpeggio table: 0 to 15 in the grid of the prototype, up to 21 through its chords (CHORDS); clamped to two octaves.
constexpr int kArpOffsetMax = 24;

double clampFinite(double v, double lo, double hi, double def) {
    if (!std::isfinite(v)) return def;
    return std::clamp(v, lo, hi);
}

// Little-endian writer of the state. end() also completes a nested u32 length (one track, one pattern) when given
// the position just after that length.
class Writer {
public:
    void u8(uint8_t v) { out_.push_back(v); }
    void i8(int8_t v) { u8(uint8_t(v)); }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) u8(uint8_t(v >> (8 * i)));
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) u8(uint8_t(v >> (8 * i)));
    }
    void f32(float v) { u32(std::bit_cast<uint32_t>(v)); }
    void f64(double v) { u64(std::bit_cast<uint64_t>(v)); }
    void bytes(std::string_view s) { out_.insert(out_.end(), s.begin(), s.end()); }
    size_t size() const { return out_.size(); }
    // Chunk: fourcc, length reserved then filled in by end().
    size_t begin(uint32_t tag) {
        u32(tag);
        u32(0);
        return out_.size();
    }
    void end(size_t start) {
        const uint32_t len = uint32_t(out_.size() - start);
        for (int i = 0; i < 4; ++i) out_[start - 4 + size_t(i)] = uint8_t(len >> (8 * i));
    }
    std::vector<uint8_t> take() { return std::move(out_); }

private:
    std::vector<uint8_t> out_;
};

// Bounded reader: any read past the end fails (ok() becomes false) and returns 0.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> d) : d_(d) {}
    bool ok() const { return ok_; }
    size_t left() const { return d_.size() - pos_; }
    bool has(size_t n) const { return ok_ && left() >= n; }
    uint8_t u8() {
        if (!has(1)) return fail();
        return d_[pos_++];
    }
    int8_t i8() { return int8_t(u8()); }
    int16_t i16() { return int16_t(u16()); }
    uint16_t u16() {
        if (!has(2)) return fail();
        const uint16_t v = uint16_t(d_[pos_] | d_[pos_ + 1] << 8);
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        if (!has(4)) return fail();
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(d_[pos_ + size_t(i)]) << (8 * i);
        pos_ += 4;
        return v;
    }
    uint64_t u64() {
        if (!has(8)) return fail();
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(d_[pos_ + size_t(i)]) << (8 * i);
        pos_ += 8;
        return v;
    }
    float f32() { return std::bit_cast<float>(u32()); }
    double f64() { return std::bit_cast<double>(u64()); }
    std::span<const uint8_t> take(size_t n) {
        if (!has(n)) {
            fail();
            return {};
        }
        const auto s = d_.subspan(pos_, n);
        pos_ += n;
        return s;
    }

private:
    uint8_t fail() {
        ok_ = false;
        pos_ = d_.size();
        return 0;
    }
    std::span<const uint8_t> d_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// TRKX body of one track. The order of the fields is the format: a new field only goes at the end.
void writeTrack(Writer& w, const TrackParams& t) {
    const TrackPatch& p = t.patch;
    w.u8(p.digidrumSlot);
    w.u8(p.ornamentOn ? 1 : 0);
    w.u8(p.ornamentLength);
    w.u8(p.ornamentLoop ? 1 : 0);
    w.u8(p.ornamentRate);
    for (int8_t v : p.ornamentPitch) w.i8(v);
    for (uint8_t v : p.ornamentNoise) w.u8(v);
    for (uint8_t v : p.ornamentVolume) w.u8(v);
    w.u8(t.arp.track ? 1 : 0);
    w.u8(t.arp.land ? 1 : 0);
    w.u8(uint8_t(t.arp.mode));
    w.u8(t.arp.octaves);
    for (int8_t v : t.arp.table) w.i8(v);
    w.u8(t.arp.length);
    // M6: DMA sample of the track, written only when not default (-1: depending on its position, D-153); a field
    // added after it will have to write it always.
    if (p.dmaSlot != -1) w.i8(p.dmaSlot);
}

// Reads the fields present; those missing at the end of the chunk keep their value (default).
void readTrack(Reader r, TrackParams& t) {
    TrackPatch& p = t.patch;
    auto rd = [&](auto& field, auto conv) {
        if (r.has(1)) field = conv(r.u8());
    };
    rd(p.digidrumSlot, [](uint8_t v) { return v; });
    rd(p.ornamentOn, [](uint8_t v) { return v != 0; });
    rd(p.ornamentLength, [](uint8_t v) { return v; });
    rd(p.ornamentLoop, [](uint8_t v) { return v != 0; });
    rd(p.ornamentRate, [](uint8_t v) { return v; });
    if (r.has(kOrnamentSteps))
        for (auto& v : p.ornamentPitch) v = r.i8();
    if (r.has(kOrnamentSteps))
        for (auto& v : p.ornamentNoise) v = r.u8();
    if (r.has(kOrnamentSteps))
        for (auto& v : p.ornamentVolume) v = r.u8();
    rd(t.arp.track, [](uint8_t v) { return v != 0; });
    rd(t.arp.land, [](uint8_t v) { return v != 0; });
    rd(t.arp.mode, [](uint8_t v) { return ArpMode(std::min<int>(v, int(ArpMode::Table))); });
    rd(t.arp.octaves, [](uint8_t v) { return v; });
    if (r.has(kArpSteps))
        for (auto& v : t.arp.table) v = r.i8();
    rd(t.arp.length, [](uint8_t v) { return v; });
    if (r.has(1)) p.dmaSlot = r.i8();
}

// Instrument sequences and pitch settings of a track (TSEQ chunk, D-148).
bool defaultSequences(const TrackPatch& p) {
    const TrackPatch d;
    return p.rows == d.rows && p.fixedNote == d.fixedNote && p.detuneCents == d.detuneCents &&
           p.analogCents == d.analogCents && p.hardSync == d.hardSync && p.link == d.link && p.pitchMatrix == d.pitchMatrix &&
           p.sidWave == d.sidWave && p.sidWaveLen == d.sidWaveLen && p.buzzShapes == d.buzzShapes && p.buzzLen == d.buzzLen &&
           p.envTuned == d.envTuned && p.envRatioN == d.envRatioN && p.envRatioD == d.envRatioD &&
           p.envFineCents == d.envFineCents && p.ymPad == d.ymPad && p.lfo == d.lfo && p.echo == d.echo;
}

// TSEQ body of one track, in the order of State.h; a new field only goes at the end.
void writeSequences(Writer& w, const TrackPatch& p) {
    for (int8_t v : p.rows.period) w.i8(v);
    for (const auto* row : {&p.rows.noise, &p.rows.mix, &p.rows.gen, &p.rows.fix})
        for (uint8_t v : *row) w.u8(v);
    w.u8(p.fixedNote);
    w.f64(p.detuneCents);
    w.f64(p.analogCents);
    w.u8(p.hardSync ? 1 : 0);
    w.u8(p.link);
    w.u16(p.pitchMatrix);
    w.u8(p.sidWaveLen);  // timer waves (B6, D-151)
    for (uint8_t v : p.sidWave) w.u8(v);
    w.u8(p.buzzLen);
    for (uint8_t v : p.buzzShapes) w.u8(v);
    w.u8(p.envTuned ? 1 : 0);  // buzzer (B7, D-152)
    w.u8(p.envRatioN);
    w.u8(p.envRatioD);
    w.f64(p.envFineCents);
    w.u8(p.ymPad ? 1 : 0);
    w.u8(p.lfo.on ? 1 : 0);  // LFO (D-257)
    w.u8(uint8_t(p.lfo.shape));
    w.u8(uint8_t(p.lfo.target));
    w.u8(p.lfo.sync ? 1 : 0);
    w.f64(p.lfo.hz);
    w.u8(p.lfo.division);
    w.f64(p.lfo.depth);
    w.u8(p.lfo.retrig ? 1 : 0);  // D-279
    w.u8(p.echo.on ? 1 : 0);     // echo of the notes (D-299)
    w.u8(p.echo.division);
    w.u8(p.echo.repeats);
    w.u8(p.echo.drop);
    w.i8(p.echo.target);
}

// Reads the fields present, like readTrack.
void readSequences(Reader r, TrackPatch& p) {
    if (r.has(kOrnamentSteps))
        for (auto& v : p.rows.period) v = r.i8();
    for (auto* row : {&p.rows.noise, &p.rows.mix, &p.rows.gen, &p.rows.fix})
        if (r.has(kOrnamentSteps))
            for (auto& v : *row) v = r.u8();
    if (r.has(1)) p.fixedNote = r.u8();
    if (r.has(8)) p.detuneCents = r.f64();
    if (r.has(8)) p.analogCents = r.f64();
    if (r.has(1)) p.hardSync = r.u8() != 0;
    if (r.has(1)) p.link = r.u8();
    if (r.has(2)) p.pitchMatrix = r.u16();
    if (r.has(1 + kSidWaveSteps)) {
        p.sidWaveLen = r.u8();
        for (auto& v : p.sidWave) v = r.u8();
    }
    if (r.has(1 + kBuzzSteps)) {
        p.buzzLen = r.u8();
        for (auto& v : p.buzzShapes) v = r.u8();
    }
    if (r.has(3)) {
        p.envTuned = r.u8() != 0;
        p.envRatioN = r.u8();
        p.envRatioD = r.u8();
    }
    if (r.has(8)) p.envFineCents = r.f64();
    if (r.has(1)) p.ymPad = r.u8() != 0;
    if (r.has(4 + 8 + 1 + 8)) {  // LFO: on, shape, target, sync, f64 rate, division, f64 depth
        p.lfo.on = r.u8() != 0;
        p.lfo.shape = LfoShape(std::min<int>(r.u8(), kLfoShapes - 1));
        p.lfo.target = LfoTarget(std::min<int>(r.u8(), kLfoTargets - 1));
        p.lfo.sync = r.u8() != 0;
        p.lfo.hz = r.f64();
        p.lfo.division = r.u8();
        p.lfo.depth = r.f64();
    }
    if (r.has(1)) p.lfo.retrig = r.u8() != 0;
    if (r.has(5)) {  // echo of the notes (D-299)
        p.echo.on = r.u8() != 0;
        p.echo.division = r.u8();
        p.echo.repeats = r.u8();
        p.echo.drop = r.u8();
        p.echo.target = r.i8();
    }
}

// GLBX: channel taken by the Digidrum track of each chip (0 = A to 2 = C), scale of the scale-locked slides (u16
// 12-bit mask, u8 tonic 0-11), then the points of the Custom slide curve as f32.
void writeGlobal(Writer& w, const GlobalParams& g) {
    w.u8(g.digidrumChannel[0]);
    w.u8(g.digidrumChannel[1]);
    w.u16(g.scaleMask);
    w.u8(g.scaleRoot);
    for (float v : g.slideTable) w.f32(v);
}

void readGlobal(Reader r, GlobalParams& g) {
    if (r.has(2)) {
        g.digidrumChannel[0] = r.u8();
        g.digidrumChannel[1] = r.u8();
    }
    if (r.has(2)) g.scaleMask = r.u16();
    if (r.has(1)) g.scaleRoot = r.u8();
    if (r.has(4 * kSlideTablePoints))
        for (auto& v : g.slideTable) v = r.f32();
}

// ---- Sequencer (M5) ----

// Rows of a pattern track in the state: the 16 rows of the prototype (ymseq::Row up to TON), then for each chord
// voice its note, its octave and its duration (16 to 30), then the rows of M6 in the order of ymseq::Row (31: INS,
// 32 to 37: the CMD rows Xyz, Sxx, Rxx, Zxx, Txx, Vxx, D-330; 38 to 46: Axy, Pxy, Dxx, Cxx, Bxy, Nxy, Exx, Oxx, Fxx,
// D-338; 47 to 49: Lxx, Mxx, Jxx, twelfth DAW test; before D-330, row 32 held every kind of command: a command read
// there goes to the row of its kind). Xyz and Pxy are retired (D-340, D-344): their rows are no longer written and a
// value read for them is dropped. An M5 state does not have the last ones; M5 ignores their ids. The ARP flags (row
// 3) and the Txx of a state written before the twelfth DAW test become Jxx and the BPM row of the pattern.
constexpr int kChordRow0 = ymseq::kProtoRows;
constexpr int kChordRowEnd = kChordRow0 + 3 * ymseq::kExtraVoices;
constexpr int kStateRows = kLaneRows;
static_assert(kLaneRows == kChordRowEnd + ymseq::kRowCount - ymseq::kProtoRows);
constexpr ymseq::Row laterRow(int id) noexcept { return ymseq::Row(ymseq::kProtoRows + id - kChordRowEnd); }

// Range and empty value of a state row (PATN id), used by setLaneCell to clamp a cell (or empty it, `drop`).
struct RowRange {
    int lo, hi, empty;
    bool drop = false;  // value out of range: empty cell rather than clamped (unknown command, D-150)
};
RowRange rowRange(int id) noexcept {
    using ymseq::Row;
    if (id >= kChordRowEnd) {
        switch (laterRow(id)) {
        case Row::Ins: return {0, ymseq::kInstrumentMax, 0};
        default:
            if (ymseq::isCmdRow(laterRow(id))) return {0, ymengine::kSeqCmdMax, 0, true};  // letter × 256 + xx (D-150)
            return {0, 0, 0};
        }
    }
    if (id >= kChordRow0) {
        switch ((id - kChordRow0) % 3) {
        case 0: return {-1, 12, -1};            // note (relative, or percussion row)
        case 1: return {-3, 9, 0};              // octave code (OCT of the percussion: -3 to +3)
        default: return {0, ymseq::kSteps, 0};  // duration in steps (0: one step)
        }
    }
    switch (Row(id)) {
    case Row::Pat: return {-1, 12, -1};
    case Row::Vol: return {0, 15, 15};
    case Row::Mix: return {0, ymengine::kSeqMixMax, 0};  // Env entries (D-277)
    case Row::Slt: return {0, 8, 0};
    case Row::Fx: return {0, 4, 0};
    case Row::Oct: return {-3, 9, 0};
    case Row::Gat: return {0, 100, 0};
    case Row::Pnr: return {0, 201, 0};  // 0, or v·10 + 101 for v from -10 to +10
    case Row::Rtg: return {0, 3, 0};
    case Row::Cnd: return {0, 7, 0};
    case Row::Slc: return {0, 12, 0};
    case Row::Ton: return {-12, 12, 0};
    default: return {0, 1, 0};  // HLD, SLD, ARP, CUT
    }
}

// Cell of state row `id` at step s: rows of ymseq::Row through Lane::get and set; the chord rows 16 to 30 go
// directly to the arrays of voices 2 to 6 (three rows per voice: note, octave, duration).
int cellOf(const ymseq::Lane& l, int id, int s) noexcept {
    if (id < kChordRow0) return l.get(ymseq::Row(id), s);
    if (id >= kChordRowEnd) return l.get(laterRow(id), s);
    const size_t v = size_t((id - kChordRow0) / 3), i = size_t(s);
    switch ((id - kChordRow0) % 3) {
    case 0: return l.vpat[v][i];
    case 1: return l.voct[v][i];
    default: return l.vdur[v][i];
    }
}

void putCell(ymseq::Lane& l, int id, int s, int value) noexcept {
    if (id < kChordRow0) return l.set(ymseq::Row(id), s, value);
    if (id >= kChordRowEnd) return l.set(laterRow(id), s, value);
    const size_t v = size_t((id - kChordRow0) / 3), i = size_t(s);
    switch ((id - kChordRow0) % 3) {
    case 0: l.vpat[v][i] = int16_t(value); break;
    case 1: l.voct[v][i] = int8_t(value); break;
    default: l.vdur[v][i] = uint16_t(value); break;
    }
}

// A pattern track: the count of non-empty rows comes first, hence a first pass that counts them.
void writeLane(Writer& w, const ymseq::Lane& l) {
    uint8_t rows = 0;
    for (int id = 0; id < kStateRows; ++id) {
        const int empty = rowRange(id).empty;
        for (int s = 0; s < ymseq::kSteps; ++s)
            if (cellOf(l, id, s) != empty) {
                ++rows;
                break;
            }
    }
    w.u8(rows);
    for (int id = 0; id < kStateRows; ++id) {
        const int empty = rowRange(id).empty;
        uint16_t cells = 0;
        for (int s = 0; s < ymseq::kSteps; ++s) cells = uint16_t(cells + (cellOf(l, id, s) != empty));
        if (cells == 0) continue;
        w.u8(uint8_t(id));
        w.u16(cells);
        for (int s = 0; s < ymseq::kSteps; ++s) {
            const int v = cellOf(l, id, s);
            if (v == empty) continue;
            w.u8(uint8_t(s));
            w.i16(int16_t(v));
        }
    }
}

// Known rows read and clamped; unknown ids ignored.
void readLane(Reader& r, ymseq::Lane& l) {
    const int rows = r.u8();
    for (int k = 0; k < rows && r.ok(); ++k) {
        const int id = r.u8();
        const int cells = r.u16();
        for (int c = 0; c < cells && r.ok(); ++c) {
            const int s = r.u8();
            const int v = r.i16();
            if (!r.ok() || id >= kStateRows) continue;
            setLaneCell(l, id, s, v);
        }
    }
}

void writeSeqSettings(Writer& w, const GlobalParams& g, const ymseq::Project& p) {
    w.u8(uint8_t(g.seqClock));
    w.u8(g.resolution);
    w.u8(uint8_t((g.loop ? 1 : 0) | (g.songMode ? 2 : 0) | (g.metronome ? 4 : 0) | (g.quantizeVbl ? 8 : 0)));
    w.u8(g.sigNum);
    w.u8(g.sigDen);
    w.u8(uint8_t(p.current));
    w.u8(uint8_t(p.viewBank));
    for (int b : p.bankLast) w.u8(uint8_t(b));
    w.u32(uint32_t(p.used.to_ulong()));
    w.u8(uint8_t(ymseq::kTracks));
    for (const ymseq::TrackSeq& t : p.tracks) {
        w.u8(uint8_t(t.octave));
        w.u16(uint16_t(t.length));
    }
    w.u8(g.metronomeLevel);  // D-288
}

void readSeqSettings(Reader r, GlobalParams& g, ymseq::Project& p) {
    if (r.has(5)) {
        g.seqClock = SeqClock(std::min<int>(r.u8(), int(SeqClock::Host)));
        g.resolution = r.u8();
        const uint8_t f = r.u8();
        g.loop = (f & 1) != 0;
        g.songMode = (f & 2) != 0;
        g.metronome = (f & 4) != 0;
        g.quantizeVbl = (f & 8) != 0;
        g.sigNum = r.u8();
        g.sigDen = r.u8();
    }
    if (r.has(2)) {
        p.current = r.u8();
        p.viewBank = r.u8();
    }
    if (r.has(4))
        for (int& b : p.bankLast) b = r.u8();
    if (r.has(4)) p.used = std::bitset<ymseq::kPatterns>(r.u32());
    const int count = r.has(1) ? r.u8() : 0;
    for (int t = 0; t < count && r.has(3); ++t) {
        const int octave = r.u8();
        const int length = r.u16();
        if (t >= ymseq::kTracks) continue;
        p.tracks[size_t(t)].octave = octave;
        p.tracks[size_t(t)].length = length;
    }
    g.metronomeLevel = r.has(1) ? uint8_t(std::min<int>(r.u8(), 100)) : 100;  // D-288; full level before
}

}  // namespace

static_assert(int(ymseq::SongKind::Rest) == int(SeqSongKind::Rest) && int(ymseq::SongKind::NoteOff) ==
                  int(SeqSongKind::NoteOff) && int(ymseq::SongKind::Loop) == int(SeqSongKind::Loop));

// The lanes are shared pointers: the engine's patterns point to the rows of the model without copying them. The
// Song entries are clamped to what the engine accepts.
void fillSequence(const ymseq::Project& p, SeqData& out) {
    for (int i = 0; i < ymseq::kPatterns; ++i) {
        const ymseq::Pattern& src = p.patterns[size_t(i)];
        SeqPattern& dst = out.patterns[size_t(i)];
        dst.steps = src.steps;
        for (int t = 0; t < ymseq::kTracks; ++t) dst.lanes[size_t(t)] = src.lanes[size_t(t)];
        dst.bpm = src.bpm;
    }
    out.song.clear();
    out.song.reserve(p.song.entries.size());
    for (const ymseq::SongEntry& e : p.song.entries)
        out.song.push_back({uint8_t(std::clamp(e.pat, 0, ymseq::kPatterns - 1)),
                            uint16_t(std::clamp(e.rep, 1, ymseq::kSongRepeatMax)),
                            uint16_t(std::clamp(e.bpm, 0, ymseq::kSongBpmMax)), SeqSongKind(e.kind),
                            uint16_t(std::clamp(e.arg, 0, 65535))});
    out.songRestart = uint16_t(std::clamp(p.song.restartIndex(), 0, 65535));
}

std::string cleanTrackName(std::string_view name) {
    std::string out;
    for (char c : name)
        if (c >= 0x20 && c <= 0x7e) out += c;
    const size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    out = out.substr(a, out.find_last_not_of(' ') - a + 1);
    if (out.size() > kTrackNameMax) {
        out.resize(kTrackNameMax);
        out.erase(out.find_last_not_of(' ') + 1);  // a cut that ends on a space does not keep it
    }
    return out;
}

void sanitize(TrackParams& t) {
    TrackPatch& p = t.patch;
    p.digidrumSlot = uint8_t(std::min<int>(p.digidrumSlot, kDigidrumSlots - 1));
    p.dmaSlot = int8_t(std::clamp<int>(p.dmaSlot, -1, kDmaSlots - 1));
    p.ornamentLength = uint8_t(std::clamp<int>(p.ornamentLength, 1, kOrnamentSteps));
    p.ornamentRate = uint8_t(std::clamp<int>(p.ornamentRate, 1, 4));
    for (auto& v : p.ornamentPitch) v = int8_t(std::clamp<int>(v, -12, 12));
    for (auto& v : p.ornamentNoise) v = v ? 1 : 0;
    for (auto& v : p.ornamentVolume) v = uint8_t(std::min<int>(v, 15));
    for (auto& v : p.rows.period) v = int8_t(std::clamp<int>(v, -kPeriodDeltaMax, kPeriodDeltaMax));
    for (auto& v : p.rows.noise) v = uint8_t(std::min<int>(v, 31));
    for (auto& v : p.rows.mix) v = uint8_t(std::min<int>(v, kChannelModes));
    for (auto& v : p.rows.gen) v = uint8_t(std::min<int>(v, kSeqGens - 1));
    for (auto& v : p.rows.fix) v = v ? 1 : 0;
    p.fixedNote = uint8_t(std::min<int>(p.fixedNote, 127));
    p.detuneCents = clampFinite(p.detuneCents, -kDetuneMaxCents, kDetuneMaxCents, 0.0);
    p.analogCents = clampFinite(p.analogCents, 0.0, kAnalogMaxCents, 0.0);
    p.link = uint8_t(std::min<int>(p.link, kInstruments));
    p.pitchMatrix &= kPitchMatrixAll;
    for (auto& v : p.sidWave) v = uint8_t(std::min<int>(v, 15));
    p.sidWaveLen = p.sidWaveLen < 2 ? 0 : uint8_t(std::min<int>(p.sidWaveLen, kSidWaveSteps));
    for (auto& v : p.buzzShapes) v = uint8_t(std::min<int>(v, 15));
    p.buzzLen = uint8_t(std::min<int>(p.buzzLen, kBuzzSteps));
    p.envRatioN = uint8_t(std::clamp<int>(p.envRatioN, 1, kEnvRatioMax));
    p.envRatioD = uint8_t(std::clamp<int>(p.envRatioD, 1, kEnvRatioMax));
    p.envFineCents = clampFinite(p.envFineCents, -kEnvFineMax, kEnvFineMax, 0.0);
    p.lfo.shape = LfoShape(std::min<int>(int(p.lfo.shape), kLfoShapes - 1));
    p.lfo.target = LfoTarget(std::min<int>(int(p.lfo.target), kLfoTargets - 1));
    p.lfo.hz = clampFinite(p.lfo.hz, kLfoMinHz, kLfoMaxHz, TrackLfo{}.hz);
    p.lfo.division = uint8_t(std::min<int>(p.lfo.division, kLfoDivisions - 1));
    p.lfo.depth = clampFinite(p.lfo.depth, 0.0, double(kLfoDepthMax[size_t(p.lfo.target)]), 0.0);
    p.echo.division = uint8_t(std::min<int>(p.echo.division, kDelayDivisions - 1));
    p.echo.repeats = uint8_t(std::clamp<int>(p.echo.repeats, 1, kEchoRepeatsMax));
    p.echo.drop = uint8_t(std::min<int>(p.echo.drop, kEchoDropMax));
    p.echo.target = int8_t(std::clamp<int>(p.echo.target, -1, kTracks - 1));
    t.arp.mode = ArpMode(std::min<int>(int(t.arp.mode), int(ArpMode::Table)));
    t.arp.octaves = uint8_t(std::clamp<int>(t.arp.octaves, 1, 3));
    for (auto& v : t.arp.table) v = int8_t(std::clamp<int>(v, 0, kArpOffsetMax));
    t.arp.length = uint8_t(std::clamp<int>(t.arp.length, 1, kArpSteps));
}

// Whatever comes from a state or a file goes through here: every field brought into the range the engine and the
// editor expect, non-finite numbers replaced by their default.
void sanitize(NonParamState& s) {
    for (TrackParams& t : s.tracks) sanitize(t);
    GlobalParams& g = s.global;
    for (auto& c : g.digidrumChannel) c = uint8_t(std::min<int>(c, 2));
    g.scaleMask &= 0x0fff;
    g.scaleRoot = uint8_t(std::min<int>(g.scaleRoot, 11));
    const GlobalParams gdef{};
    for (size_t i = 0; i < g.slideTable.size(); ++i)
        g.slideTable[i] = float(clampFinite(double(g.slideTable[i]), 0.0, 1.0, double(gdef.slideTable[i])));
    const KitParams kdef = kKitBanks[0].kit;
    for (int k = 0; k < kDrumSlots; ++k) {
        DrumParams& d = s.kit.inst[size_t(k)];
        const DrumParams& dd = kdef.inst[size_t(k)];
        const auto& rg = kKitRanges[size_t(k)];
        d.a = clampFinite(d.a, rg[0].lo, rg[0].hi, dd.a);
        d.b = clampFinite(d.b, rg[1].lo, rg[1].hi, dd.b);
        d.dec = clampFinite(d.dec, rg[2].lo, rg[2].hi, dd.dec);
        d.noise = clampFinite(d.noise, rg[3].lo, rg[3].hi, dd.noise);
        d.vol = clampFinite(d.vol, rg[4].lo, rg[4].hi, dd.vol);
    }
    for (auto& smp : s.digidrums) {
        if (smp.size() > Engine::kMaxUserSample) smp.resize(Engine::kMaxUserSample);
        for (auto& v : smp) v = uint8_t(std::min<int>(v, 15));
    }
    for (auto& n : s.trackNames) n = cleanTrackName(n);
    // DMA bank (D-153).
    for (DmaSlot& sl : s.dma.slots) {
        sl.rate = uint8_t(std::min<int>(sl.rate, kDmaRates - 1));
        sl.root = uint8_t(std::min<int>(sl.root, 127));
    }
    for (auto& smp : s.dmaSamples)
        if (smp.size() > kDmaMaxBytes) smp.resize(kDmaMaxBytes);
    // Digidrum editor (D-154).
    for (DigidrumSlotState& ds : s.digiSlots) {
        ds.name = cleanDigidrumName(ds.name);
        DigidrumSlot& p = ds.play;
        p.rateHz = std::isfinite(p.rateHz) ? std::clamp(p.rateHz, kDigidrumRateMin, kDigidrumRateMax) : kDigidrumRate;
        p.root = uint8_t(std::min<int>(p.root, 127));
        p.cents = std::isfinite(p.cents) ? std::clamp(p.cents, -kDigidrumCentsMax, kDigidrumCentsMax) : 0.0;
        p.loopEnd = std::min<uint32_t>(p.loopEnd, Engine::kMaxUserSample);
        p.loopStart = std::min(p.loopStart, p.loopEnd);
        ds.convert = DigiConvert(std::min<int>(int(ds.convert), int(DigiConvert::YmLevels)));
        if (ds.source.size() > Engine::kMaxUserSample) ds.source.resize(Engine::kMaxUserSample);
    }
    for (Instrument& ins : s.instruments) {
        if (!ins.used) {
            ins = Instrument{};
            continue;
        }
        ins.name = cleanInstrumentName(ins.name);
        TrackParams t;
        t.patch = ins.patch;
        t.arp = ins.arp;
        sanitize(t);
        ins.patch = t.patch;
        ins.arp = t.arp;
    }

    // Sequencer: global settings, current pattern, banks, Song, tracks.
    g.seqClock = SeqClock(std::min<int>(int(g.seqClock), int(SeqClock::Host)));
    g.resolution = uint8_t(std::min<int>(g.resolution, kSeqResolutions - 1));
    g.timing = SeqTiming(std::min<int>(int(g.timing), int(SeqTiming::Ticks)));
    g.speed = uint8_t(std::clamp<int>(g.speed, 1, kSpeedMax));
    g.grooveLength = uint8_t(std::clamp<int>(g.grooveLength, 1, kGrooveSteps));
    for (uint8_t& v : g.groove) v = uint8_t(std::min<int>(v, kGrooveMax));
    const bool sigKnown = std::any_of(ymseq::kSignatures.begin(), ymseq::kSignatures.end(), [&](const auto& sig) {
        return sig.numerator == g.sigNum && sig.denominator == g.sigDen;
    });
    if (!sigKnown) {
        g.sigNum = ymseq::kSignatures[ymseq::kDefaultSignature].numerator;
        g.sigDen = ymseq::kSignatures[ymseq::kDefaultSignature].denominator;
    }
    ymseq::Project& p = s.seq;
    p.current = std::clamp(p.current, 0, ymseq::kPatterns - 1);
    p.pending = p.pending >= 0 && p.pending < ymseq::kPatterns && p.pending != p.current ? p.pending : -1;
    p.used.set(size_t(p.current));
    p.viewBank = std::clamp(p.viewBank, 0, ymseq::kBanks - 1);
    for (int b = 0; b < ymseq::kBanks; ++b)
        p.bankLast[size_t(b)] = std::clamp(p.bankLast[size_t(b)], b * ymseq::kBankSize, (b + 1) * ymseq::kBankSize - 1);
    for (ymseq::Pattern& pat : p.patterns) pat.steps = std::clamp(pat.steps, 1, ymseq::kSteps);
    bool restart = false;
    for (ymseq::SongEntry& e : p.song.entries) {
        e.pat = std::clamp(e.pat, 0, ymseq::kPatterns - 1);
        e.rep = std::clamp(e.rep, 1, ymseq::kSongRepeatMax);
        e.bpm = e.bpm <= 0 ? 0 : std::clamp(e.bpm, ymseq::kSongBpmMin, ymseq::kSongBpmMax);
        // Extended Song (D-158): length of the Rest and Note-off entries, entries replayed by a jump; one restart.
        switch (e.kind) {
            case ymseq::SongKind::Pattern: e.arg = 0; break;
            case ymseq::SongKind::Rest:
            case ymseq::SongKind::NoteOff: e.arg = std::clamp(e.arg, 1, ymseq::kSteps); break;
            case ymseq::SongKind::Loop: e.arg = std::clamp(e.arg, 1, ymseq::kSongLoopMax); break;
        }
        if (e.special()) e.pat = 0;
        if (e.kind == ymseq::SongKind::Loop) e.bpm = 0;
        e.restart = e.restart && !restart;
        restart = restart || e.restart;
    }
    for (int t = 0; t < kTracks; ++t) {
        ymseq::TrackSeq& ts = p.tracks[size_t(t)];
        ts.octave = std::clamp(ts.octave, 0, 9);
        ts.length = std::clamp(ts.length, 0, 64);
        s.tracks[size_t(t)].seq.octave = ts.octave;
        s.tracks[size_t(t)].seq.length = ts.length;
    }
}

// Chunks in the order of State.h. The optional ones are written only when they differ from the default, so that a
// project that does not use a feature gives the same bytes as the version before it.
std::vector<uint8_t> encodeState(const PluginState& state) {
    Writer w;
    w.bytes("STYM");
    w.u16(kStateVersion);
    w.u16(0);

    size_t c = w.begin(kParm);
    w.u32(uint32_t(kParamCount));
    const auto& specs = paramSpecs();
    for (int i = 0; i < kParamCount; ++i) {
        const std::string& id = specs[size_t(i)].id;
        w.u16(uint16_t(id.size()));
        w.bytes(id);
        w.f32(state.values[size_t(i)]);
    }
    w.end(c);

    c = w.begin(kTrkx);
    w.u8(uint8_t(kTracks));
    for (const TrackParams& t : state.extras.tracks) {
        const size_t tc = w.size() + 4;
        w.u32(0);
        writeTrack(w, t);
        w.end(tc);
    }
    w.end(c);

    c = w.begin(kGlbx);
    writeGlobal(w, state.extras.global);
    w.end(c);

    c = w.begin(kKit);
    for (const DrumParams& d : state.extras.kit.inst) {
        w.f64(d.a);
        w.f64(d.b);
        w.f64(d.dec);
        w.f64(d.noise);
        w.f64(d.vol);
    }
    w.end(c);

    c = w.begin(kDigi);
    w.u8(uint8_t(kDigidrumSlots));
    for (const auto& smp : state.extras.digidrums) {
        const size_t n = std::min<size_t>(smp.size(), Engine::kMaxUserSample);
        w.u32(uint32_t(n));
        // Two 4-bit values per byte, the first in the low nibble; an odd count leaves the last high nibble at 0.
        for (size_t i = 0; i < n; i += 2) {
            const uint8_t lo = uint8_t(std::min<int>(smp[i], 15));
            const uint8_t hi = i + 1 < n ? uint8_t(std::min<int>(smp[i + 1], 15)) : 0;
            w.u8(uint8_t(lo | hi << 4));
        }
    }
    w.end(c);

    // Track names: chunk written only if a track is named (a state without names stays the M3 one).
    const auto& names = state.extras.trackNames;
    if (std::any_of(names.begin(), names.end(), [](const std::string& n) { return !n.empty(); })) {
        c = w.begin(kName);
        w.u8(uint8_t(kTracks));
        for (const std::string& n : names) {
            const std::string clean = cleanTrackName(n);
            w.u8(uint8_t(clean.size()));
            w.bytes(clean);
        }
        w.end(c);
    }

    // Sequencer (M5).
    const ymseq::Project& p = state.extras.seq;
    c = w.begin(kSeqs);
    writeSeqSettings(w, state.extras.global, p);
    w.end(c);

    c = w.begin(kPatn);
    w.u8(uint8_t(p.used.count()));
    for (int i = 0; i < ymseq::kPatterns; ++i) {
        if (!p.used.test(size_t(i))) continue;
        const ymseq::Pattern& pat = p.patterns[size_t(i)];
        w.u8(uint8_t(i));
        const size_t pc = w.size() + 4;
        w.u32(0);
        w.u16(uint16_t(pat.steps));
        uint8_t lanes = 0;
        for (int t = 0; t < ymseq::kTracks; ++t) lanes = uint8_t(lanes + (pat.lanes[size_t(t)] && !pat.lane(t).empty()));
        w.u8(lanes);
        for (int t = 0; t < ymseq::kTracks; ++t) {
            if (!pat.lanes[size_t(t)] || pat.lane(t).empty()) continue;
            w.u8(uint8_t(t));
            writeLane(w, pat.lane(t));
        }
        // BPM row of the pattern (twelfth DAW test, item 150), after the tracks and only when it holds a tempo: earlier
        // versions stop before it.
        const auto bpmCells = uint16_t(std::count_if(pat.bpm.begin(), pat.bpm.end(), [](uint8_t b) { return b; }));
        if (bpmCells) w.u16(bpmCells);
        for (int st = 0; st < ymseq::kSteps; ++st)
            if (pat.bpm[size_t(st)]) {
                w.u8(uint8_t(st));
                w.u8(pat.bpm[size_t(st)]);
            }
        w.end(pc);
    }
    w.end(c);

    c = w.begin(kSong);
    w.u16(uint16_t(std::min<size_t>(p.song.entries.size(), 65535)));
    for (size_t i = 0; i < p.song.entries.size() && i < 65535; ++i) {
        const ymseq::SongEntry& e = p.song.entries[i];
        w.u8(uint8_t(std::clamp(e.pat, 0, ymseq::kPatterns - 1)));
        w.u16(uint16_t(std::clamp(e.rep, 1, ymseq::kSongRepeatMax)));
        w.u16(uint16_t(std::clamp(e.bpm, 0, 65535)));
    }
    w.end(c);
    // Extended Song (D-158): chunk written only if an entry is special or marks the restart.
    const auto& song = p.song.entries;
    if (std::any_of(song.begin(), song.end(), [](const ymseq::SongEntry& e) { return e.special() || e.restart; })) {
        c = w.begin(kSngx);
        const size_t n = std::min<size_t>(song.size(), 65535);
        w.u16(uint16_t(n));
        for (size_t i = 0; i < n; ++i) {
            w.u8(uint8_t(song[i].kind));
            w.u16(uint16_t(std::clamp(song[i].arg, 0, 65535)));
            w.u8(song[i].restart ? 1 : 0);
        }
        w.end(c);
    }

    // Instruments (M6): chunk written only if an instrument exists (a state without instruments stays the M5 one).
    const auto& inst = state.extras.instruments;
    const int used = int(std::count_if(inst.begin(), inst.end(), [](const Instrument& i) { return i.used; }));
    if (used > 0) {
        c = w.begin(kInst);
        w.u8(uint8_t(used));
        for (int k = 0; k < kInstruments; ++k) {
            if (!inst[size_t(k)].used) continue;
            const std::string text = json::dump(instrumentEntry(inst[size_t(k)]));
            w.u8(uint8_t(k));
            w.u32(uint32_t(text.size()));
            w.bytes(text);
        }
        w.end(c);
    }
    // Instrument sequences of the tracks (D-148): chunk written only if a track has some.
    const auto& tracks = state.extras.tracks;
    const int seqTracks = int(std::count_if(tracks.begin(), tracks.end(),
                                            [](const TrackParams& t) { return !defaultSequences(t.patch); }));
    if (seqTracks > 0) {
        c = w.begin(kTseq);
        w.u8(uint8_t(seqTracks));
        for (int t = 0; t < kTracks; ++t) {
            if (defaultSequences(tracks[size_t(t)].patch)) continue;
            w.u8(uint8_t(t));
            const size_t tc = w.size() + 4;
            w.u32(0);
            writeSequences(w, tracks[size_t(t)].patch);
            w.end(tc);
        }
        w.end(c);
    }
    // Ticks timing of the sequencer (D-149): chunk written only when the settings are not the defaults.
    const GlobalParams& g = state.extras.global;
    const GlobalParams gdef{};
    if (g.timing != gdef.timing || g.speed != gdef.speed || g.grooveLength != gdef.grooveLength || g.groove != gdef.groove) {
        c = w.begin(kTimg);
        w.u8(uint8_t(g.timing));
        w.u8(g.speed);
        w.u8(g.grooveLength);
        for (uint8_t v : g.groove) w.u8(v);
        w.end(c);
    }
    // STE DMA bank (D-153): chunk written only when not default.
    const NonParamState& x = state.extras;
    if (!(x.dma == DmaParams{}) ||
        std::any_of(x.dmaSamples.begin(), x.dmaSamples.end(), [](const auto& v) { return !v.empty(); })) {
        c = w.begin(kDmas);
        w.u8(uint8_t(kDmaSlots));
        for (int k = 0; k < kDmaSlots; ++k) {
            const DmaSlot& sl = x.dma.slots[size_t(k)];
            w.u8(sl.rate);
            w.u8(sl.stereo ? 1 : 0);
            w.u8(sl.root);
            w.u8(sl.loop ? 1 : 0);
            const auto& smp = x.dmaSamples[size_t(k)];
            const size_t n = std::min<size_t>(smp.size(), kDmaMaxBytes);
            w.u32(uint32_t(n));
            w.bytes(std::string_view(reinterpret_cast<const char*>(smp.data()), n));
        }
        w.end(c);
    }
    // Digidrum editor (D-154): chunk written only when not default.
    if (std::any_of(x.digiSlots.begin(), x.digiSlots.end(),
                    [](const DigidrumSlotState& slot) { return !(slot == DigidrumSlotState{}); })) {
        c = w.begin(kDigs);
        w.u8(uint8_t(kDigidrumSlots));
        for (const DigidrumSlotState& ds : x.digiSlots) {
            const std::string name = cleanDigidrumName(ds.name);
            w.u8(uint8_t(name.size()));
            w.bytes(name);
            w.f64(ds.play.rateHz);
            w.u8(ds.play.root);
            w.f64(ds.play.cents);
            w.u8(ds.play.loop ? 1 : 0);
            w.u32(ds.play.loopStart);
            w.u32(ds.play.loopEnd);
            w.u8(uint8_t(ds.convert));
            const size_t n = std::min<size_t>(ds.source.size(), Engine::kMaxUserSample);
            w.u32(uint32_t(n));
            w.bytes(std::string_view(reinterpret_cast<const char*>(ds.source.data()), n));
        }
        w.end(c);
    }
    // Register recorder take (D-157): chunk written only if there is one.
    if (const RegisterTake& t = state.take; !t.empty()) {
        c = w.begin(kRecd);
        w.u8(t.chip);
        w.u32(t.frameHz);
        w.u64(t.clockNum);
        w.u32(t.clockDen);
        const size_t n = std::min<size_t>(t.frames.size(), kRecorderMaxFrames);
        w.u32(uint32_t(n));
        for (size_t i = 0; i < n; ++i) {
            const RegisterFrame& f = t.frames[i];
            for (uint8_t v : f.reg) w.u8(v);
            // Bit 0: R13 written; bits 1 to 3: start of the effect of each channel (M7).
            uint8_t flags = f.r13 ? 1 : 0;
            for (size_t k = 0; k < f.fx.size(); ++k) flags = uint8_t(flags | (f.fx[k].restart ? 2u << k : 0u));
            w.u8(flags);
            for (const RegisterFrame::Effect& e : f.fx) {
                w.u8(e.on ? uint8_t(e.gen) : 0xff);
                w.u8(e.value);
                w.u8(e.timer.prescaler);
                w.u16(e.timer.data);
            }
        }
        w.end(c);
        // Zxx of the take (D-150), separately: RECD keeps its frame size, earlier versions ignore it.
        std::vector<std::pair<uint32_t, uint8_t>> syncs;
        for (size_t i = 0; i < n; ++i)
            if (t.frames[i].sync >= 0) syncs.emplace_back(uint32_t(i), uint8_t(t.frames[i].sync));
        if (!syncs.empty()) {
            c = w.begin(kRsyn);
            w.u32(uint32_t(syncs.size()));
            for (const auto& [frame, value] : syncs) {
                w.u32(frame);
                w.u8(value);
            }
            w.end(c);
        }
    }
    return w.take();
}

DecodeResult decodeState(std::span<const uint8_t> data) {
    DecodeResult res;
    Reader r(data);
    const auto magic = r.take(4);
    if (!r.ok() || std::memcmp(magic.data(), "STYM", 4) != 0) {
        res.error = "not a STYM2149 state";
        return res;
    }
    res.version = r.u16();
    r.u16();  // flags: none in v1
    if (!r.ok()) {
        res.error = "truncated header";
        return res;
    }
    if (res.version == 0 || res.version > kStateVersion) {
        res.error = "state version " + std::to_string(res.version) + " not supported (version " +
                    std::to_string(kStateVersion) + " at most)";
        return res;
    }

    // Chunks in any order. Each body has its own bounded Reader, so a chunk shorter or longer than expected never
    // shifts the next one; only a chunk cut by the end of the blob, or a truncated PARM, refuses the state.
    PluginState& st = res.state;
    std::vector<ymseq::SongEntry> songx;  // SNGX: kinds of the entries, set on those of SONG at the end
    std::vector<std::pair<uint32_t, uint8_t>> takeSync;  // RSYN: Zxx of the take, set on its frames at the end
    while (r.left() > 0) {
        const uint32_t tag = r.u32();
        const uint32_t len = r.u32();
        const auto body = r.take(len);
        if (!r.ok()) {
            res.error = "truncated chunk";
            return res;
        }
        Reader b(body);
        if (tag == kParm) {
            const uint32_t count = b.u32();
            for (uint32_t i = 0; i < count && b.ok(); ++i) {
                const uint16_t n = b.u16();
                const auto id = b.take(n);
                const float v = b.f32();
                if (!b.ok()) break;
                const int index = findParam(std::string_view(reinterpret_cast<const char*>(id.data()), id.size()));
                if (index < 0) {
                    ++res.unknownParams;
                    continue;
                }
                st.values[size_t(index)] = clampToSpec(paramSpec(index), v);
            }
            if (!b.ok()) {
                res.error = "truncated PARM chunk";
                return res;
            }
        } else if (tag == kTrkx) {
            const int count = b.u8();
            for (int t = 0; t < count && b.ok(); ++t) {
                const uint32_t n = b.u32();
                const auto tb = b.take(n);
                if (!b.ok()) break;
                if (t < kTracks) readTrack(Reader(tb), st.extras.tracks[size_t(t)]);
            }
        } else if (tag == kGlbx) {
            readGlobal(b, st.extras.global);
        } else if (tag == kKit) {
            for (DrumParams& d : st.extras.kit.inst) {
                if (!b.has(40)) break;
                d.a = b.f64();
                d.b = b.f64();
                d.dec = b.f64();
                d.noise = b.f64();
                d.vol = b.f64();
            }
        } else if (tag == kDigi) {
            const int count = b.u8();
            for (int s = 0; s < count && b.ok(); ++s) {
                const uint32_t n = b.u32();
                const auto packed = b.take((size_t(n) + 1) / 2);
                if (!b.ok()) break;
                if (s >= kDigidrumSlots) continue;
                auto& smp = st.extras.digidrums[size_t(s)];
                const size_t keep = std::min<size_t>(n, Engine::kMaxUserSample);
                smp.resize(keep);
                for (size_t i = 0; i < keep; ++i) smp[i] = uint8_t((packed[i / 2] >> ((i & 1) * 4)) & 15);
            }
        } else if (tag == kSeqs) {
            readSeqSettings(b, st.extras.global, st.extras.seq);
            res.hasSequencer = true;
        } else if (tag == kPatn) {
            // The created patterns are those this chunk holds (they replace the mask of SEQS).
            ymseq::Project& p = st.extras.seq;
            p.used.reset();
            const int count = b.u8();
            for (int k = 0; k < count && b.ok(); ++k) {
                const int idx = b.u8();
                const uint32_t n = b.u32();
                const auto patBody = b.take(n);
                if (!b.ok()) break;
                if (idx >= ymseq::kPatterns) continue;
                Reader pr(patBody);
                ymseq::Pattern pat;
                pat.steps = pr.has(2) ? pr.u16() : ymseq::kDefaultSteps;
                const int lanes = pr.has(1) ? pr.u8() : 0;
                for (int l = 0; l < lanes && pr.has(2); ++l) {
                    const int t = pr.u8();
                    ymseq::Lane lane;
                    readLane(pr, lane);
                    ymseq::migrateArpFlags(lane);
                    if (t < ymseq::kTracks && !lane.empty()) pat.lanes[size_t(t)] = std::make_shared<const ymseq::Lane>(lane);
                }
                const int bpmCells = pr.has(2) ? pr.u16() : 0;
                for (int cell = 0; cell < bpmCells && pr.has(2); ++cell) {
                    const int step = pr.u8();
                    const int bpm = pr.u8();
                    if (bpm >= ymengine::kTempoCmdMin) pat.bpm[size_t(step)] = uint8_t(bpm);
                }
                pat.migrateTempo();
                p.patterns[size_t(idx)] = std::move(pat);
                p.used.set(size_t(idx));
            }
        } else if (tag == kSong) {
            auto& entries = st.extras.seq.song.entries;
            entries.clear();
            const int count = b.u16();
            for (int k = 0; k < count && b.has(5); ++k) {
                ymseq::SongEntry e;
                e.pat = b.u8();
                e.rep = b.u16();
                e.bpm = b.u16();
                entries.push_back(e);
            }
        } else if (tag == kInst) {
            const int count = b.u8();
            for (int k = 0; k < count && b.ok(); ++k) {
                const int slot = b.u8();
                const uint32_t n = b.u32();
                const auto text = b.take(n);
                if (!b.ok()) break;
                if (slot >= kInstruments) continue;
                const auto v = json::parse(std::string_view(reinterpret_cast<const char*>(text.data()), text.size()));
                const json::Value* patch = v && v->isObject() ? v->find("patch") : nullptr;
                if (!patch || !patch->isObject()) continue;  // not an instrument: slot left empty
                st.extras.instruments[size_t(slot)] = instrumentFromEntry(*v);
            }
        } else if (tag == kTimg) {
            GlobalParams& g = st.extras.global;
            if (b.has(1)) g.timing = SeqTiming(std::min<int>(b.u8(), int(SeqTiming::Ticks)));
            if (b.has(1)) g.speed = b.u8();
            if (b.has(1)) g.grooveLength = b.u8();
            for (uint8_t& v : g.groove)
                if (b.has(1)) v = b.u8();
        } else if (tag == kDmas) {
            DmaParams& d = st.extras.dma;
            const int count = b.has(1) ? b.u8() : 0;
            for (int k = 0; k < count && b.has(8); ++k) {
                DmaSlot sl;
                sl.rate = b.u8();
                sl.stereo = b.u8() != 0;
                sl.root = b.u8();
                sl.loop = b.u8() != 0;
                const uint32_t n = b.u32();
                const auto bytes = b.take(n);
                if (!b.ok()) break;
                if (k >= kDmaSlots) continue;
                d.slots[size_t(k)] = sl;
                auto& smp = st.extras.dmaSamples[size_t(k)];
                smp.resize(std::min<size_t>(n, kDmaMaxBytes));
                if (!smp.empty()) std::memcpy(smp.data(), bytes.data(), smp.size());
            }
        } else if (tag == kTseq) {
            const int count = b.u8();
            for (int k = 0; k < count && b.ok(); ++k) {
                const int t = b.u8();
                const uint32_t n = b.u32();
                const auto sb = b.take(n);
                if (!b.ok()) break;
                if (t < kTracks) readSequences(Reader(sb), st.extras.tracks[size_t(t)].patch);
            }
        } else if (tag == kDigs) {
            const int count = b.has(1) ? b.u8() : 0;
            for (int k = 0; k < count && b.has(1); ++k) {
                DigidrumSlotState g;
                const auto name = b.take(b.u8());
                if (!b.has(8 + 1 + 8 + 1 + 4 + 4 + 1 + 4)) break;
                g.name.assign(reinterpret_cast<const char*>(name.data()), name.size());
                g.play.rateHz = b.f64();
                g.play.root = b.u8();
                g.play.cents = b.f64();
                g.play.loop = b.u8() != 0;
                g.play.loopStart = b.u32();
                g.play.loopEnd = b.u32();
                g.convert = DigiConvert(std::min<int>(b.u8(), int(DigiConvert::YmLevels)));
                const uint32_t n = b.u32();
                const auto src = b.take(n);
                if (!b.ok()) break;
                if (k >= kDigidrumSlots) continue;
                g.source.resize(std::min<size_t>(n, Engine::kMaxUserSample));
                if (!g.source.empty()) std::memcpy(g.source.data(), src.data(), g.source.size());
                st.extras.digiSlots[size_t(k)] = std::move(g);
            }
        } else if (tag == kSngx) {
            const int count = b.u16();
            songx.clear();
            if (!b.has(size_t(count) * 4)) continue;  // truncated chunk: ignored
            for (int k = 0; k < count; ++k) {
                ymseq::SongEntry e;
                e.kind = ymseq::SongKind(std::min<int>(b.u8(), int(ymseq::SongKind::Loop)));
                e.arg = b.u16();
                e.restart = (b.u8() & 1) != 0;
                songx.push_back(e);
            }
        } else if (tag == kRsyn) {
            const uint32_t count = b.u32();
            takeSync.clear();
            if (!b.ok() || count > kRecorderMaxFrames || !b.has(size_t(count) * 5)) continue;  // truncated: ignored
            for (uint32_t k = 0; k < count; ++k) {
                const uint32_t frame = b.u32();
                takeSync.emplace_back(frame, b.u8());
            }
        } else if (tag == kRecd) {
            RegisterTake t;
            t.chip = uint8_t(std::min<int>(b.u8(), 1));
            t.frameHz = std::clamp<uint32_t>(b.u32(), 1, 1000);
            t.clockNum = b.u64();
            t.clockDen = b.u32();
            const uint32_t n = b.u32();
            // 14 registers, the flags, then per channel generator, value, prescaler and u16 data.
            constexpr size_t kFrameBytes = 14 + 1 + 3 * 5;
            if (!b.ok() || t.clockNum == 0 || t.clockDen == 0 || n > kRecorderMaxFrames || !b.has(size_t(n) * kFrameBytes))
                continue;  // unreadable take: ignored
            t.frames.resize(n);
            for (RegisterFrame& f : t.frames) {
                for (uint8_t& v : f.reg) v = b.u8();
                const uint8_t flags = b.u8();
                f.r13 = (flags & 1) != 0;
                for (size_t k = 0; k < f.fx.size(); ++k) {
                    RegisterFrame::Effect& e = f.fx[k];
                    const uint8_t gen = b.u8();
                    e.on = gen <= uint8_t(Generator::Dma);
                    e.restart = e.on && (flags & (2u << k)) != 0;
                    e.gen = e.on ? Generator(gen) : Generator::Chip;
                    e.value = b.u8();
                    // Ranges of ymengine::MfpSetting: prescaler index 0 to 7, data 1 to 256 (0: stopped).
                    e.timer.prescaler = uint8_t(std::min<int>(b.u8(), 7));
                    e.timer.data = std::min<uint16_t>(b.u16(), 256);
                }
            }
            st.take = std::move(t);
        } else if (tag == kName) {
            const int count = b.u8();
            for (int t = 0; t < count && b.ok(); ++t) {
                const auto n = b.take(b.u8());
                if (!b.ok()) break;
                if (t < kTracks)
                    st.extras.trackNames[size_t(t)].assign(reinterpret_cast<const char*>(n.data()), n.size());
            }
        } else {
            ++res.unknownChunks;
        }
    }
    // SNGX of another Song (different entry count): ignored, the entries stay patterns.
    if (auto& entries = st.extras.seq.song.entries; !songx.empty() && songx.size() == entries.size())
        for (size_t i = 0; i < entries.size(); ++i) {
            entries[i].kind = songx[i].kind;
            entries[i].arg = songx[i].arg;
            entries[i].restart = songx[i].restart;
        }
    for (const auto& [frame, value] : takeSync)  // frames outside the take: ignored
        if (frame < st.take.frames.size()) st.take.frames[frame].sync = value;
    sanitize(st.extras);
    res.ok = true;
    return res;
}

// ---- Rows of a pattern track (PATN chunk, JSON files) ----

int laneCell(const ymseq::Lane& l, int row, int step) noexcept { return cellOf(l, row, step); }

int laneEmpty(int row) noexcept { return rowRange(row).empty; }

void setLaneCell(ymseq::Lane& l, int row, int step, int value) noexcept {
    if (row < 0 || row >= kLaneRows || step < 0 || step >= ymseq::kSteps) return;
    const RowRange rg = rowRange(row);
    putCell(l, row, step, rg.drop && (value < rg.lo || value > rg.hi) ? rg.empty : std::clamp(value, rg.lo, rg.hi));
}

}  // namespace stym::plugin
