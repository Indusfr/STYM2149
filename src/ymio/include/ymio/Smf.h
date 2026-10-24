// STYM2149 — ymio: standard MIDI files (SMF), reading formats 0 and 1 and writing format 1 (M7, A3, D-170)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Reading: the prototype's parseMidiFile, with bounds. Only notes count: a note on (velocity > 0) opens the note of
// its channel and pitch (a note already open is replaced), a note off or a note on of velocity 0 closes it; notes
// still open at the end of the track are lost. The first tempo of the file (meta 0x51) gives the BPM, rounded and
// clamped to 20-300 (120 without tempo). Running status is followed; controllers, pressure, program changes, pitch
// bend, meta and SysEx are skipped. Notes are stored in the order in which they close, track after track. Refused:
// missing header, SMPTE or zero division.
// Bounds: a track stops at the end of its chunk or of the file; an event cut by this end stops the track; beyond
// kSmfMaxNotes notes, the following ones are ignored (`notesDropped`).
//
// Writing: the prototype's midiBlob. Format 1 header, then each track with its events in the given order, their
// deltas as variable-length quantities, and the end of track (meta 0x2F).
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace stym::ymio {

inline constexpr size_t kSmfMaxNotes = size_t(1) << 20;

struct SmfNote {
    int channel = 0;   // 0-15 (9: GM percussion)
    int note = 0;      // 0-127
    int velocity = 0;  // 1-127 (that of the note on)
    int64_t start = 0, end = 0;  // in ticks from the start of the track
};

// The notes of a file, all tracks together, with its timing.
struct SmfSong {
    int division = 96;   // ticks per quarter note
    int bpm = 120;       // first tempo of the file, clamped to 20-300
    bool tempo = false;  // a tempo was read
    std::vector<SmfNote> notes;
    bool notesDropped = false;
};

// Empty for a refused file (see the file comment).
std::optional<SmfSong> readSmf(std::span<const uint8_t> bytes);

// Written event: absolute tick and bytes (status and data, or meta FF type length data).
struct SmfEvent {
    uint32_t tick = 0;
    std::vector<uint8_t> bytes;
};

// Variable-length quantity (midiVar), clamped to 0x0FFFFFFF (four bytes).
void appendVarLen(std::vector<uint8_t>& out, uint32_t v);

// Format 1 file: one track per event list, sorted by tick (a negative delta is written as 0).
std::vector<uint8_t> writeSmf(uint16_t division, std::span<const std::vector<SmfEvent>> tracks);

// Common events. Tempo in microseconds per quarter note; time signature denominator as a note value (4: quarter),
// stored as a power of two.
SmfEvent smfTempo(uint32_t tick, uint32_t microsPerQuarter);
SmfEvent smfTimeSignature(uint32_t tick, int numerator, int denominator);
SmfEvent smfTrackName(uint32_t tick, const std::string& name);
SmfEvent smfNoteOn(uint32_t tick, int channel, int note, int velocity);
SmfEvent smfNoteOff(uint32_t tick, int channel, int note);

}  // namespace stym::ymio
