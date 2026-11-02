// STYM2149 — plugincore: import of XM, MOD, S3M and OKT modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// An XM 1.04 module (ymio::parseXm) becomes patterns, a Song and instruments with the STE DMA sound:
// - Song: the order table walked as Fasttracker II plays it (Bxx jumps, Dxx pattern breaks), up to the end of the
//   table or the return to a position already played, which becomes the restart point; one entry per position
//   (repeats grouped), the patterns met become patterns 1 to n (32 at most), cut at their Dxx.
// - Channels that play notes, in order, on the tracks that play the DMA sound (xmImportTracks). More channels than
//   tracks: a channel goes on the track of channels that never sound on the same lines (from a note to its
//   release, the end of its sample without a loop, a cut or the channel's next note), the track then taking its lines
//   where it sounds; a channel that finds no such track is left out.
// - Notes: NOTE and OCT (C-4 of the XM = C4, MIDI note 60); a note holds until the next one or the release (TIE on
//   its line and the held lines; release line empty); INS = instrument of the note; VOL = volume of the channel
//   (sample, volume column, Cxx) out of 15 and PAN = its pan (sample, 8xx, volume column), on each held line;
//   Fxx → speed (Sxx, Ticks timing) or tempo (Txx below the first line, BPM timing); the arpeggio 0xy and the
//   portamentos 1xx and 2xx are left out (their commands are retired).
// - Instruments: the 8 most played that have a sample become instruments 1 to 8, with the DMA sound, and their most
//   played sample the DMA slots 1 to 8: signed 8-bit, at the STE rate closest to that of the sample's C4 that fits
//   in 32 KB, root note 60. Forward or ping-pong loop from the start: loop of the slot; loop that starts further on:
//   attack then loop unrolled up to 32 KB, without a loop (the STE only loops the whole sample).
// - Timing: Ticks at the module tick if its tempo does not change and falls on a VBL rate (125, 150, 175 or
//   250 BPM: 50, 60, 70 or 100 Hz); otherwise BPM, the tempo of each Song entry coming from the speed and the tempo
//   of the module at the start of the position, and a change within a pattern becoming a Txx at its line:
//   every entry then has its own tempo, which it starts again at.
// - What the import does not render is reported in text lines for the end-of-import alert.
// - A MOD module (ymio::parseMod) comes in the XM shape and takes the same way: its samples at the PAL rate of
//   the Amiga, its channels at the Amiga's pan (L R R L), "MOD imported". So do an S3M module (ymio::parseS3m)
//   and an Oktalyzer one (ymio::parseOkt); their effects that the XM shape leaves out are counted under their
//   own names.
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

// Maximum size of a module read: 128 instruments of 16 samples of a few MB (a MOD is far below).
inline constexpr size_t kXmMaxBytes = size_t(64) << 20;

// Module formats the import takes: the XM, and those read into its shape.
enum class ModuleFormat : uint8_t { Xm, Mod, S3m, Okt };

// The format of a module from its content (the signature of an XM, OKT or S3M; a MOD, which has none at its start, is
// one that reads), or none: File > Import > Tracker song and File > Play.
std::optional<ModuleFormat> moduleFormat(std::span<const uint8_t> file);
// "XM", "MOD", "S3M" or "OKT", in the alerts ("S3M imported", "OKT not imported").
const char* moduleFormatName(ModuleFormat f) noexcept;

// Tracks filled by the import, in channel order: those that play the DMA sound with the STE machine, the 32 tracks
// in 16 and 32 channels, the 8 DMA tracks in 3 channels (5 to 12 in Authentic 3ch, 9 to 16 in Dual chip). A module of
// `channels` channels with notes, 8 or fewer, takes tracks 5 to 12 in 16 and 32 channels too, those of the DMA in 3ch,
// so that the project can switch to Authentic 3ch and keep playing it (Indus, 29/10/2026 20:01).
std::vector<int> xmImportTracks(ymengine::VoiceMode mode, int channels = kTracks);

// What an import did: the settings the caller applies (tracks, timing, tempo) and the lines of the alert.
struct XmImport {
    std::string title;          // module name
    int channels = 0;           // tracks filled
    int shared = 0;             // channels on the track of another one, which never sounds on their lines
    int played = 0;             // channels of the module that play notes, imported or left out
    ymengine::VoiceMode mode = ymengine::VoiceMode::Studio16;  // the mode the module was imported for
    int patterns = 0;           // patterns 0 to n-1 replaced
    int entries = 0;            // Song entries
    int instruments = 0;        // instruments and DMA slots 0 to k-1 replaced
    std::array<bool, kTracks> on{};  // tracks switched on (the others are switched off)
    bool ticks = false;         // Ticks timing (module tick = VBL), otherwise BPM
    int vblRate = 0;            // Ticks: index into kVblRates
    int speed = 6;              // Ticks: ticks per line at the start
    float tempo = 120;          // BPM: free tempo (60-240)
    std::vector<std::string> notes;  // what the import does not render
    ModuleFormat format = ModuleFormat::Xm;  // "MOD imported", "S3M imported" …
};

// The import, or the alert that explains why the module was refused.
struct XmImportResult {
    std::optional<XmImport> import;
    std::string error;
};

// Replaces patterns 0 to n-1, the Song, the instruments and DMA slots 0 to k-1 and the timing (timing, speed) of
// `state`; nothing changes if the module is refused. The automatable settings (STE machine, tracks, DMA generator of
// the tracks in 16 and 32 channels, VBL rate, free tempo) are returned in the result.
XmImportResult importXm(std::span<const uint8_t> file, ymengine::VoiceMode mode, NonParamState& state);
// The same for a MOD module.
XmImportResult importMod(std::span<const uint8_t> file, ymengine::VoiceMode mode, NonParamState& state);
// The same for a module of any of the formats (S3M; OKT).
XmImportResult importModule(ModuleFormat f, std::span<const uint8_t> file, ymengine::VoiceMode mode,
                            NonParamState& state);
// Lines of the "XM imported" alert, or "MOD imported", "S3M imported", "OKT imported".
std::vector<std::string> xmImportMessage(const XmImport& r, const std::string& fileName);
// The settings of the result given to the project (File > Import, and the copy of the project that File > Play plays):
// the mode it was imported for, STE machine, tracks switched on or off, DMA generator of the tracks
// switched on in 16 and 32 channels, VBL rate (Ticks) or free tempo (BPM).
void xmImportValues(const XmImport& r, ParamValues& values);
// A module that plays more channels than the mode it was imported for has tracks: more than the 8 DMA tracks of a
// 3-channel mode, or more than the 16 tracks of 16 ch. File > Import asks whether it goes to the studio mode of
// xmStudioMode or stays in its mode (Indus, 29/10/2026 19:08; 16 ch again, 30/10/2026 02:19).
bool xmWantsStudio(const XmImport& r) noexcept;
// The studio mode that plays the whole module: 16 ch up to 16 channels, otherwise 32 ch.
ymengine::VoiceMode xmStudioMode(const XmImport& r) noexcept;

}  // namespace stym::plugin
