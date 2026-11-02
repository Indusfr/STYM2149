// STYM2149 — ymio: reading and writing YM-Tracker songs (.ymt)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// YM-Tracker (ym-tracker.lovable.app) does not describe its song files. The layout below was read from the nine demo
// songs that Indus saved from the application (29/10/2026): only the bytes of those files, never the application's
// code (rule §9). Every field the demos vary was placed by them; the meaning of the fields they leave at one value is
// INFERRED from the instrument tabs as the application shows them (Y20 to Y24, Y34) and
// those values are kept as they are.
//
// File: "YMT1", the bytes 1, 1, then the 16-bit offset of the data (8), then a gzip member. Its data, little-endian:
//   title (16-bit length, UTF-8), tempo in BPM (16), speed in ticks per row (8), chips (8), restart position (16),
//   channels (8), tone channels per chip (8), order length (16), patterns (16), instruments (16, the first one "--"
//   standing for no instrument), digidrum channel (8: 1 when the last channel is the "Drum" one), then the channel
//   names (8-bit length each), the order table (8 bits per position) and the patterns: rows (16), byte count (32),
//   then the cells row by row, packed as in an XM module (flag byte with bit 7 and the fields present: note,
//   instrument, volume column, effect, parameter; a cell without bit 7 is the five bytes).
// Cells: note 1 to 96 (37 is the C-5 of YM-Tracker, 261.6 Hz: MIDI note 60), 97 the note off; volume column as in
//   XM ($10 to $50: 0 to 64); effect 1 to 16 the XM effects 0 to F, then the letters G, H, K, L, M, P, R, T, X, Y
//   (17 to 26), then Q, S, U, Z (27 to 30, the "YM6 FX hooks" added after them). The demos place 0 to F, K, M, P, R,
//   X, Y, Q, S, U and Z; G, H, L and T, which no demo plays, are INFERRED from the alphabetical order.
// Instrument: name (16-bit length), 24 bytes (below), the volume list (16-bit loop, -1 for none; 16-bit count; one
//   byte per tick), a 16-bit word (0), the arpeggio list (16-bit count, semitones), the pitch list (16-bit count,
//   16-bit tone period offsets), a 16-bit word (0), then the digidrum bank (16-bit count; per sample: 8-bit name
//   length and name, 16-bit rate, 32-bit sample count, the 4-bit values two per byte, low nibble first).
// The 24 bytes: Use ADSR, Attack, Decay, Sustain, Release (ticks, 0-15), hardware envelope, R13 shape, envelope
//   period (16), automatic period, octave, Sync-Buzzer, noise, noise period, (unknown), tone, arpeggio speed (ticks per
//   step), vibrato speed and depth, then 5 bytes that the demos leave at 0.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stym::ymio {

inline constexpr uint8_t kYmtNoteOff = 97;
inline constexpr int kYmtC5 = 37;              // note of YM-Tracker's C-5 (MIDI note 60)
inline constexpr int kYmtMaxChannels = 32;
inline constexpr int kYmtMaxPatterns = 256;
inline constexpr int kYmtMaxRows = 256;
inline constexpr int kYmtMaxInstruments = 256;
inline constexpr size_t kYmtMaxBytes = size_t(64) << 20;  // data once unpacked

// Effect letter ('0'-'9', 'A'-'Z'; 0 for none) of a code of the file, and the code of a letter (0: none or not one
// of the file's effects).
char ymtEffectLetter(uint8_t code) noexcept;
uint8_t ymtEffectCode(char letter) noexcept;

// One cell (one row of one channel), the values of the file as they are; the effect as its letter.
struct YmtCell {
    uint8_t note = 0;        // 0: nothing; 1-96; 97: note off
    uint8_t instrument = 0;  // 0: nothing
    uint8_t volume = 0;      // volume column (0: nothing)
    char effect = 0;         // 0: nothing
    uint8_t param = 0;
    bool operator==(const YmtCell&) const = default;
};

struct YmtPattern {
    int rows = 64;
    std::vector<YmtCell> cells;  // row by row, rows × channels
    bool operator==(const YmtPattern&) const = default;
};

// A digidrum of an instrument's bank: 4-bit values played on the volume register.
struct YmtDrum {
    std::string name;
    uint16_t rate = 0;  // INFERRED: rate in Hz (27 100 to 30 300 in the demos)
    std::vector<uint8_t> levels;
    bool operator==(const YmtDrum&) const = default;
};

struct YmtInstrument {
    std::string name;
    // ADSR tab: the volume follows the ADSR when on, otherwise the volume list.
    bool useAdsr = true;
    uint8_t attack = 0, decay = 0, sustain = 15, release = 8;  // ticks; sustain 0-15
    // HardEnv tab.
    bool hardEnv = false;
    uint8_t shape = 0x0A;
    uint16_t envPeriod = 0;
    bool autoPeriod = true;  // period from the note
    uint8_t octave = 1;      // INFERRED: 1 for "1× (note pitch)"
    bool syncBuzzer = false;  // INFERRED
    // Tone tab.
    bool noise = false;
    uint8_t noisePeriod = 16;
    uint8_t unknown14 = 0;
    bool tone = true;
    uint8_t arpSpeed = 1;  // ticks per step of the arpeggio list
    uint8_t vibSpeed = 0, vibDepth = 0;
    std::array<uint8_t, 5> unknown19{};
    // Volume list: INFERRED, the volume per tick when Use ADSR is off (15 down to 0 in the demos).
    int16_t volumeLoop = -1;
    std::vector<uint8_t> volume = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    uint16_t word1 = 0;          // after the volume list
    std::vector<int8_t> arp = {0};      // Macros tab: semitones per step, looping
    std::vector<int16_t> pitch = {0};   // Macros tab: tone period offset per tick, looping
    uint16_t word2 = 0;          // after the pitch list
    std::vector<YmtDrum> drums;  // DigiDrum bank (Zxx)
    bool operator==(const YmtInstrument&) const = default;
};

struct YmtSong {
    std::string title;
    int bpm = 125;    // a tick lasts 2.5 / bpm seconds
    int speed = 6;    // ticks per row
    int chips = 1;
    int toneChannels = 3;  // per chip
    bool drumChannel = true;  // the last channel is the digidrum one
    int restart = 0;
    std::vector<std::string> channelNames;  // one per channel
    std::vector<uint8_t> order;
    std::vector<YmtPattern> patterns;
    std::vector<YmtInstrument> instruments;  // [0]: "--", no instrument
    int unknownEffects = 0;  // read: cells whose effect code is none of the known ones (left empty)

    int channels() const noexcept { return int(channelNames.size()); }
    const YmtCell& cell(int pattern, int row, int channel) const {
        return patterns[size_t(pattern)].cells[size_t(row * channels() + channel)];
    }
    bool operator==(const YmtSong&) const = default;
};

// The song, or why it is refused (shown in the "YM-Tracker song not imported" alert).
struct YmtParseResult {
    std::optional<YmtSong> song;
    std::string error;
};
YmtParseResult parseYmt(std::span<const uint8_t> file);

// The file of a song (its gzip member in stored blocks).
std::vector<uint8_t> writeYmt(const YmtSong& song);

}  // namespace stym::ymio
