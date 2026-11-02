// STYM2149 — ymio: reading and writing TTrak modules (.TPM), instruments (.TPI) and 4-bit samples (.4BI)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// TTrak (2020) is an Atari ST tracker whose formats were never published. Their layout was found by studying how
// TTrak 1.04 and its SNDH replay routine read and write them, for interoperability; none of TTrak's code, tables,
// modules, instruments or samples is part of STYM2149. Big-endian (68000) words and longs.
//
// TPM, a module saved by TTrak:
// - $0000 "GODMOD30"; $0008 title (16 bytes) and $0018 author (16), padded with '-';
// - $0028 song length (positions played), $0029 restart position, $002A replay rate (Hz: 50, 100, 150, 200),
//   $002B-$002F five bytes kept as they are, $0030 groove (16 bytes: ticks added to the speed on rows 0-15, 16-31...);
// - $0040 33 words: where instrument 1-32 starts, counted from $0040 (word 0 repeats instrument 1);
// - $0082 33 longs: where sample slot 0-31 starts in the sample block, the last one being the block's length;
// - $0106 order list (256 pattern numbers);
// - $0206 32 instrument records of $416 bytes (see TtrakInstrument);
// - $84C6 the patterns, $400 bytes each: 64 rows of 16 bytes, three channels of (note, instrument, effect,
//   parameter) then four unused bytes. TTrak saves patterns 0 to the highest number of positions 0 to the song
//   length (that position included), compared as signed bytes;
// - then the sample block: 4-bit volumes, one per byte, each sample ending with its first byte that has bit 7 set
//   (whose low nibble is still played); an empty slot holds the single byte $80.
// TPI, one instrument: "GODINS30", its $416-byte record, then its sample (the single byte $80 when it has none).
// 4BI, one sample: the bytes of a sample slot as stored (TTrak sets bit 7 of the last byte when it loads one; files
// made elsewhere may end with $88 padding, which then stays after the end byte).
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stym::ymio {

inline constexpr int kTtrakRows = 64;
inline constexpr int kTtrakChannels = 3;
inline constexpr int kTtrakInstruments = 32;
inline constexpr int kTtrakSampleSlots = 32;
inline constexpr int kTtrakPositions = 256;
inline constexpr int kTtrakMaxPatterns = 128;  // pattern numbers past $7F are not counted when TTrak saves
inline constexpr size_t kTtrakInstrumentSize = 0x416;
inline constexpr size_t kTtrakPatternSize = 0x400;

// One row of one channel, as stored.
struct TtrakCell {
    uint8_t note = 0;        // 0: none; note n plays MIDI note n + 23 (A4 = 46)
    uint8_t instrument = 0;  // 0: none, 1-32
    uint8_t effect = 0;      // 0: none
    uint8_t param = 0;
    bool empty() const noexcept { return *this == TtrakCell{}; }
    bool operator==(const TtrakCell&) const = default;
};

struct TtrakPattern {
    std::array<std::array<TtrakCell, kTtrakChannels>, kTtrakRows> rows{};
    std::array<std::array<uint8_t, 4>, kTtrakRows> spare{};  // the four unused bytes of each row, kept
    bool operator==(const TtrakPattern&) const = default;
};

// An instrument record, kept whole; the fields below name the bytes that the replay routine reads.
struct TtrakInstrument {
    std::array<uint8_t, kTtrakInstrumentSize> raw{};

    // Word offsets, inside the record, of its tables (always the same in files saved by TTrak).
    static constexpr int kArpTableAt = 0x00, kNoiseTableAt = 0x02, kTimerTableAt = 0x04, kFmWaveAt = 0x06,
                         kMorphEnvAt = 0x08, kMorphWavesAt = 0x0A;
    static constexpr int kArpTable = 0x6A, kNoiseTable = 0x7A, kTimerTable = 0x8A, kFmWave = 0x9A,
                         kMorphEnv = 0x29A, kMorphWaves = 0x39A;
    // Settings.
    static constexpr int kAttenuation = 0x0C;  // taken off every volume
    static constexpr int kTimerType = 0x0D;    // 0 none, 1-3 SID, 4 morph, 5 sync buzzer, 6 FM, 7 digidrum, 8 FM sync
    static constexpr int kBuzzerShape = 0x0E;  // R13 written at the note (0: not written)
    static constexpr int kWowDepth = 0x0F;
    static constexpr int kHardSync = 0x10;     // R13 written again at each note, even unchanged
    static constexpr int kLink = 0x11;         // instrument that follows this one (0: none)
    static constexpr int kDetune = 0x12;       // word
    static constexpr int kFixedPitch = 0x14;   // word
    static constexpr int kFmSpeed = 0x16, kFmDepth = 0x17, kFmValue = 0x18 /* word */, kFmPosition = 0x1A;
    static constexpr int kMorphSpeed = 0x1C, kMorphValue = 0x1E /* word */, kMorphPosition = 0x20;
    static constexpr int kYmPad = 0x22;
    static constexpr int kFmLoopOn = 0x23, kFmLoop = 0x24 /* word */, kFmEnd = 0x26 /* word */;
    static constexpr int kSidechainSend = 0x28, kSidechainDuck = 0x29;
    static constexpr int kMorphLoopOn = 0x2B, kMorphLoop = 0x2C /* word */, kMorphEnd = 0x2E /* word */;
    // Matrix: one byte per generator (square, buzzer, timer) for each pitch source; non-zero: the source acts on it.
    static constexpr int kMatrixArp = 0x30, kMatrixFixed = 0x34, kMatrixDetune = 0x38, kMatrixSlide = 0x3C,
                         kMatrixFm = 0x40, kMatrixWow = 0x44;
    static constexpr int kSampleSlot = 0x33;  // sample slot of a digidrum (0-31)
    static constexpr int kAmpSpeed = 0x48, kAmpLast = 0x49, kAmpLoop = 0x4B;
    // Amplitude envelope, up to 30 steps: bit 7 timer on, bit 6 noise on, bit 5 square off, bit 4 buzzer, 0-3 volume.
    static constexpr int kAmpEnv = 0x4C, kAmpSteps = 30;
    static constexpr int kEditor = 0x3BA;  // editor data (the drawn envelope's points), kept
    static constexpr int kName = 0x406, kNameSize = 16;

    uint8_t& at(int offset) { return raw[size_t(offset)]; }
    uint8_t at(int offset) const { return raw[size_t(offset)]; }
    uint16_t word(int offset) const { return uint16_t(raw[size_t(offset)] << 8 | raw[size_t(offset) + 1]); }
    void setWord(int offset, uint16_t v) {
        raw[size_t(offset)] = uint8_t(v >> 8);
        raw[size_t(offset) + 1] = uint8_t(v);
    }
    std::string name() const;           // without the '-' or space padding
    void setName(std::string_view n);   // 16 characters at most, padded with '-'

    // An instrument as a new TTrak module holds it: no sound settings, sample slot `slot`.
    static TtrakInstrument blank(int slot);
    bool operator==(const TtrakInstrument&) const = default;
};

struct TtrakModule {
    std::array<uint8_t, 16> title{}, author{};  // as stored (padded with '-')
    uint8_t length = 1, restart = 0, rate = 50;
    std::array<uint8_t, 5> reserved{};
    std::array<uint8_t, 16> groove{};
    std::array<uint8_t, kTtrakPositions> order{};
    std::array<TtrakInstrument, kTtrakInstruments> instruments{};
    std::vector<TtrakPattern> patterns;
    // Sample slots, bytes as stored (end byte included); empty: no sample (saved as the single byte $80).
    std::array<std::vector<uint8_t>, kTtrakSampleSlots> samples{};

    std::string titleText() const;
    std::string authorText() const;
    void setTitle(std::string_view t);
    void setAuthor(std::string_view a);
    // A new TTrak module: no title, 50 Hz, one empty pattern, blank instruments.
    static TtrakModule blank();
};

template <class T>
struct TtrakResult {
    std::optional<T> value;
    std::string error;
};

// Read whole files held in memory. Pure functions; a cut or inconsistent file is refused.
TtrakResult<TtrakModule> parseTtrakModule(std::span<const uint8_t> file);
struct TtrakInstrumentFile {
    TtrakInstrument instrument;
    std::vector<uint8_t> sample;  // as stored; empty: none
};
TtrakResult<TtrakInstrumentFile> parseTtrakInstrument(std::span<const uint8_t> file);

// Write the files as TTrak saves them: offset tables recomputed, patterns 0 to the highest one the song plays.
std::vector<uint8_t> writeTtrakModule(const TtrakModule& module);
std::vector<uint8_t> writeTtrakInstrument(const TtrakInstrumentFile& instrument);

// Patterns that TTrak saves for this order list and song length (see above).
int ttrakPatternCount(const std::array<uint8_t, kTtrakPositions>& order, int length) noexcept;

// The volumes (0-15) that a stored sample plays, its end byte included; empty for an empty slot (the single $80).
std::vector<uint8_t> ttrakSampleLevels(std::span<const uint8_t> stored);
// The stored bytes of a sample of volumes 0-15 (the last one gets bit 7); empty levels: empty.
std::vector<uint8_t> ttrakSampleBytes(std::span<const uint8_t> levels);
// A .4BI file as a stored sample (bit 7 set on its last byte, as TTrak loads it); empty file: empty.
std::vector<uint8_t> parseTtrak4bi(std::span<const uint8_t> file);

// "GODMOD30" at the start: a TTrak module.
bool isTtrakModule(std::span<const uint8_t> file) noexcept;
bool isTtrakInstrument(std::span<const uint8_t> file) noexcept;

}  // namespace stym::ymio
