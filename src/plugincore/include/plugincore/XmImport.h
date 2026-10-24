// STYM2149 — plugincore: import of XM modules (M7, B5, D-056, D-166, D-173)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// An XM 1.04 module (ymio::parseXm) becomes patterns, a Song and instruments with the STE DMA sound (D-166):
// - Song: the order table walked as Fasttracker II plays it (Bxx jumps, Dxx pattern breaks), up to the end of the
//   table or the return to a position already played, which becomes the restart point; one entry per position
//   (repeats grouped), the patterns met become patterns 1 to n (32 at most), cut at their Dxx.
// - Channels that play notes, in order, on the tracks that play the DMA sound (xmImportTracks).
// - Notes: NOTE and OCT (C-4 of the XM = C4, MIDI note 60); a note holds until the next one or the release (TIE on
//   its line and the held lines; release line empty); INS = instrument of the note; VOL = volume of the channel
//   (sample, volume column, Cxx) out of 15 and PAN = its pan (sample, 8xx, volume column), on each held line (D-345);
//   Fxx → speed (Sxx, Ticks timing) or tempo (Txx below the first line, BPM timing, D-243); the arpeggio 0xy and the
//   portamentos 1xx and 2xx are left out (their commands are retired, D-340, D-344).
// - Instruments: the 8 most played that have a sample become instruments 1 to 8, with the DMA sound, and their most
//   played sample the DMA slots 1 to 8: signed 8-bit, at the STE rate closest to that of the sample's C4 that fits
//   in 32 KB, root note 60. Forward or ping-pong loop from the start: loop of the slot; loop that starts further on:
//   attack then loop unrolled up to 32 KB, without a loop (the STE only loops the whole sample).
// - Timing: Ticks at the module tick if its tempo does not change and falls on a VBL rate (125, 150, 175 or
//   250 BPM: 50, 60, 70 or 100 Hz); otherwise BPM, the tempo of each Song entry coming from the speed and the tempo
//   of the module at the start of the position, and a change within a pattern becoming a Txx at its line (D-243):
//   every entry then has its own tempo, which it starts again at.
// - What the import does not render is reported in text lines for the end-of-import alert.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/State.h"

namespace stym::plugin {

// Maximum size of a module read: 128 instruments of 16 samples of a few MB.
inline constexpr size_t kXmMaxBytes = size_t(64) << 20;
// Rate of the C4 of an XM sample without relative note or fine tune (format specification).
inline constexpr double kXmC4Hz = 8363;

// Tracks filled by the import, in channel order: those that play the DMA sound with the STE machine, the 16 tracks
// in 16 channels, the 8 DMA tracks in 3 channels (5 to 12 in Authentic 3ch, 9 to 16 in Dual chip).
std::vector<int> xmImportTracks(ymengine::VoiceMode mode);

// What an import did: the settings the caller applies (tracks, timing, tempo) and the lines of the alert.
struct XmImport {
    std::string title;          // module name
    int channels = 0;           // channels imported (tracks filled)
    int patterns = 0;           // patterns 0 to n-1 replaced
    int entries = 0;            // Song entries
    int instruments = 0;        // instruments and DMA slots 0 to k-1 replaced
    std::array<bool, kTracks> on{};  // tracks switched on (the others are switched off)
    bool ticks = false;         // Ticks timing (module tick = VBL), otherwise BPM
    int vblRate = 0;            // Ticks: index into kVblRates
    int speed = 6;              // Ticks: ticks per line at the start
    float tempo = 120;          // BPM: free tempo (60-240)
    std::vector<std::string> notes;  // what the import does not render
};

// The import, or the alert that explains why the module was refused.
struct XmImportResult {
    std::optional<XmImport> import;
    std::string error;
};

// Replaces patterns 0 to n-1, the Song, the instruments and DMA slots 0 to k-1 and the timing (timing, speed) of
// `state`; nothing changes if the module is refused. The automatable settings (STE machine, tracks, DMA generator of
// the tracks in 16 channels, VBL rate, free tempo) are returned in the result.
XmImportResult importXm(std::span<const uint8_t> file, ymengine::VoiceMode mode, NonParamState& state);
// Lines of the "XM imported" alert.
std::vector<std::string> xmImportMessage(const XmImport& r, const std::string& fileName);

}  // namespace stym::plugin
