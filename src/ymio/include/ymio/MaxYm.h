// STYM2149 — ymio: reading and writing maxYMiser songs (.MYS) and voicesets (.MYV)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// gwEm never published these formats. Where the data sit comes from the public reverse engineering of minYMize
// (Jean-Baptiste Queru, 2025, AGPL-3.0-or-later, codeberg.org/jbqueru/minymize, files mys.c and myv.c); what the
// values mean when maxYMiser plays them is not described there and is not decided here: the values are kept as they
// are in the file. Big-endian (68000) words.
//
// MYS: "MYM0TRAK" (8 bytes), editor header (64 bytes, byte 13 = song speed), song (256 steps of 4 bytes: patterns of
// channels A, B, C and STe DMA; $FF = empty, $FE = note off), song length (1), repeat position (1), then the
// patterns one after another, 64 rows each, each written row taking 8 bytes: note (0 none, 1 off), instrument
// (1-32, 0 none), volume (0 none, 1-16), effect 1 and its parameter, effect 2 and its parameter, and the number of
// empty rows that follow it in the pattern.
//
// MYV: 8 digidrum start addresses (32-bit offsets in the file), "MYM1INST", 32 instruments of 64 bytes (name, flags,
// settings and the numbers of their 8 sequences), 8 digidrum entries of 4 bytes (length in bytes, 2 unused), the
// sequences (128 bytes each: 63 words, length, repeat), then "MYM1DIGI" and the digidrum bytes.
//
// SNDH saved by maxYMiser (or by maXYmizer, its older name): the replay routine, then for each song its voiceset and
// its song in the layouts above, without their file lengths: the voiceset's digidrum addresses count from its start,
// its sequences run up to the next block, and so do the song's patterns (zero bytes may follow them). The voiceset of
// a song is the last one before it; older versions sign it "MYM0INST", with sequences of 64 bytes (31 words, length,
// repeat).
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stym::ymio {

inline constexpr int kMysRows = 64;
inline constexpr int kMysMaxPatterns = 240;  // song entries from $F0 up are special, not patterns
inline constexpr int kMysSteps = 256;
inline constexpr uint8_t kMysEmpty = 0xFF;    // song entry: nothing on this channel (and the undefined $F0-$FC)
inline constexpr uint8_t kMysNoteOff = 0xFE;  // song entry: the channel is cut
inline constexpr uint8_t kMysLoop = 0xFD;     // song entry: looping pattern of the jam mode, skipped in playback
inline constexpr uint8_t kMysNoteOffRow = 1;  // note value of a "note off" row
inline constexpr int kMyvInstruments = 32;
inline constexpr int kMyvMaxSequences = 256;
inline constexpr int kMyvSequenceLength = 63;
inline constexpr int kMyvDigidrums = 8;

// One row of one channel, with the values of the file as they are.
struct MysRow {
    uint8_t note = 0;        // 0: nothing, 1: off, otherwise the note
    uint8_t instrument = 0;  // 0: nothing, 1-32
    uint8_t volume = 0;      // 0: nothing, 1-16
    uint8_t effect1 = 0, param1 = 0;  // effect: 0 or an ASCII letter or digit ('1'-'9', 'A'-'I', 'L'-'Z')
    uint8_t effect2 = 0, param2 = 0;
    bool empty() const noexcept { return *this == MysRow{}; }
    bool operator==(const MysRow&) const = default;
};

struct MysPattern {
    std::array<MysRow, kMysRows> rows{};
};

struct MysStep {
    std::array<uint8_t, 3> channel{kMysEmpty, kMysEmpty, kMysEmpty};  // pattern of channels A, B, C
    uint8_t dma = 0;  // STe DMA column, kept as it is
    bool operator==(const MysStep&) const = default;
};

struct MysSong {
    std::array<uint8_t, 64> header{};  // editor header; byte 13 is the song speed
    int speed = 6;                     // VBLs per row (header byte 13)
    int length = 1;                    // steps played
    int repeat = 0;                    // step where the song restarts
    std::array<MysStep, kMysSteps> steps{};
    std::vector<MysPattern> patterns;
};

struct MyvInstrument {
    std::string name;  // 15 characters at most
    // Bytes 16-63 of the instrument record, kept whole; the named fields below are read from and written to it.
    std::array<uint8_t, 48> raw{};
    uint8_t& at(int offset) { return raw[size_t(offset - 16)]; }
    uint8_t at(int offset) const { return raw[size_t(offset - 16)]; }
    // Offsets in the 64-byte record (minYMize, myv.c).
    static constexpr int kPortamento = 16, kArpeggio = 17, kVibrato = 18, kTranspose = 19, kFixedFrequency = 20,
                         kFixedDetune = 21;  // flags, one bit per generator (4 square, 2 buzzer, 1 timer)
    static constexpr int kSequenceSpeed = 32, kPulseWidth = 33, kBuzzerWave = 34, kBuzzerSync = 35,
                         kDigidrum = 36, kDigidrumRate = 37, kVolume = 38, kDetuneCoarse = 39, kDetuneFine = 40,
                         kBuzzerResolution = 41;
    // Sequence numbers: volume, arpeggio, vibrato, mixer, noise, fixed frequency, timer waveform, pulse width.
    static constexpr int kSequences = 48;
    bool operator==(const MyvInstrument&) const = default;
};

struct MyvSequence {
    std::array<uint16_t, kMyvSequenceLength> data{};
    uint8_t length = 1;  // 1-63
    uint8_t repeat = 0;  // entry where the sequence loops
    bool operator==(const MyvSequence&) const = default;
};

struct MyvVoiceset {
    std::array<MyvInstrument, kMyvInstruments> instruments{};
    std::vector<MyvSequence> sequences;  // sequence 0 is not shown in maxYMiser
    std::array<std::vector<uint8_t>, kMyvDigidrums> digidrums{};  // bytes as stored; empty: no sample
};

// Either the data, or nothing and the reason shown to the user.
template <class T>
struct MaxYmResult {
    std::optional<T> value;
    std::string error;
};

// Read a whole file held in memory. Pure functions. A cut or inconsistent file is refused.
MaxYmResult<MysSong> parseMys(std::span<const uint8_t> file);
MaxYmResult<MyvVoiceset> parseMyv(std::span<const uint8_t> file);

// Write the files in the layout read above: patterns packed with the count of empty rows, song speed in header byte
// 13, digidrum addresses recomputed.
std::vector<uint8_t> writeMys(const MysSong& song);
std::vector<uint8_t> writeMyv(const MyvVoiceset& voices);

// The songs of an SNDH that maxYMiser saved (see above), in the order of the file, each with its voiceset; empty if
// the image holds none that reads whole. `image`: the unpacked SNDH. Pure function.
struct MaxYmSong {
    MysSong song;
    MyvVoiceset voices;
};
std::vector<MaxYmSong> maxYmSongsIn(std::span<const uint8_t> image);

}  // namespace stym::ymio
