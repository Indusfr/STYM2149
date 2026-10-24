// STYM2149 — ymio: reading XM modules (M7, B5, D-056, D-166)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Fasttracker II XM 1.04 format, from its public description ("The XM module format description for XM files
// version $0104", Mr.H / Triton, 1995): "Extended Module:" header, order table, packed patterns (flag byte with
// bit 7, or five bytes), instruments (table from notes to samples, envelopes ignored here) and 8 or 16-bit
// delta-coded samples. Bounded reading: a cut or inconsistent file is refused, except the data of the last sample,
// kept up to the end of the file (a common case) and flagged.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stym::ymio {

inline constexpr int kXmMaxChannels = 64;  // 32 for Fasttracker II; more recent trackers go up to 64
inline constexpr int kXmMaxPatterns = 256;
inline constexpr int kXmMaxInstruments = 128;
inline constexpr int kXmMaxSamples = 16;  // per instrument
inline constexpr int kXmMaxRows = 256;
inline constexpr uint8_t kXmKeyOff = 97;  // note 97: release; 1 to 96: C-0 to B-7

// One cell of a pattern (one row of one channel), with the values of the file as they are.
struct XmCell {
    uint8_t note = 0;        // 0: nothing; 1-96; 97: release
    uint8_t instrument = 0;  // 0: nothing; 1-128
    uint8_t volume = 0;      // volume column (0x10-0x50: volume 0-64; beyond, effects)
    uint8_t effect = 0;      // 0-35 (0-9, then A = 10 … Z = 35)
    uint8_t param = 0;
    bool operator==(const XmCell&) const = default;
};

// One pattern; an empty pattern of the file (no packed data) holds empty cells.
struct XmPattern {
    int rows = 64;  // 1-256
    std::vector<XmCell> cells;  // row by row, rows × channels
};

// One sample of an instrument. Loop type 3 becomes "no loop"; once the data are decoded, the loop is bounded to them
// and a loop of length 0 becomes "no loop" too.
struct XmSample {
    std::string name;
    uint32_t loopStart = 0, loopLength = 0;  // in frames
    uint8_t volume = 64;                     // 0-64
    int8_t finetune = 0;                     // -128 to 127: 1/128 of a semitone
    uint8_t loopType = 0;                    // 0: no loop, 1: forward, 2: ping-pong
    bool sixteenBit = false;
    uint8_t panning = 128;  // 0-255, left to right
    int8_t relativeNote = 0;  // semitones added to the played note
    bool adpcm = false;       // ModPlug ADPCM compression: data not read
    std::vector<int16_t> data;  // decoded frames, at 16-bit scale (8 bits × 256)
};

// One instrument: only its name, its samples and the sample of each note are kept.
struct XmInstrument {
    std::string name;
    std::array<uint8_t, 96> keymap{};  // note 1-96 → sample of the instrument (index from 0 into `samples`)
    std::vector<XmSample> samples;
};

// The module as read from the file, converted afterwards by plugincore's importXm. Order entries that name a missing
// pattern are set to 0.
struct XmModule {
    std::string name, tracker;
    uint16_t version = 0x0104;
    int channels = 0;
    int restart = 0;     // entry of the order table where the song restarts
    bool linear = true;  // linear frequency table (otherwise Amiga)
    int tempo = 6;       // ticks per row
    int bpm = 125;       // tick rate: a tick lasts 2.5 / bpm seconds (D-173)
    std::vector<uint8_t> order;  // order table (song length)
    std::vector<XmPattern> patterns;
    std::vector<XmInstrument> instruments;
    bool truncatedSamples = false;  // sample data cut by the end of the file

    const XmCell& cell(int pattern, int row, int channel) const {
        return patterns[size_t(pattern)].cells[size_t(row * channels + channel)];
    }
};

// Either a module, or no module and the reason shown to the user in the "XM not imported" alert (D-173).
struct XmParseResult {
    std::optional<XmModule> module;
    std::string error;
};

// Reads a whole XM file held in memory. Pure function, no shared state.
XmParseResult parseXm(std::span<const uint8_t> file);

}  // namespace stym::ymio
