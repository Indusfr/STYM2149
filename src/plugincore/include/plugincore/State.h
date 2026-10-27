// STYM2149 — plugincore: non-automatable state and binary format of the plugin state
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The state saved by the host (STYM2149_SPEC.md §7) = parameter values + non-automatable state: track settings that
// the parameters do not carry, Chipdrum kit, Digidrum samples, sequencer (patterns, Song, settings), project
// instruments, register recorder take.
//
// Format v1, little-endian:
//   "STYM", u16 version, u16 flags (0), then chunks: fourcc, u32 length, content.
//   PARM: u32 count, then per parameter u16 length, UTF-8 id, f32 plain value.
//   TRKX: u8 track count, then per track u32 length and its fields (see State.cpp; i8 DMA sample at the end of
//         the track, -1 depending on the position).
//   GLBX: global fields outside the parameters.  KIT : 8 × 5 f64.  DIGI: u8 count, then per slot u32 sample count
//   and the 4-bit values, two per byte (low nibble first).
//   NAME (only if a track is named): u8 count, then per track u8 length and the name in ASCII.
//   SEQS: sequencer settings: u8 clock (0 Free run, 1 MIDI, 2 Host sync), u8 resolution (RESOS), u8 flags
//         (1 loop, 2 Song, 4 metronome, 8 Quantize to VBL), u8 numerator and u8 denominator of the time signature,
//         u8 current pattern, u8 displayed bank, 4 × u8 last pattern of each bank, u32 patterns created (bit n =
//         pattern n), then u8 track count and per track u8 octave (seqOct) and u16 own length; then u8 level of the
//         metronome, 0 to 100 % (100 when absent).
//   PATN: u8 count, then per created pattern u8 number, u32 length and its content: u16 steps, u8 count of
//         non-empty tracks, then per track u8 number, u8 row count and per row u8 id (order of ymseq::Row up to TON,
//         then 16 + 3 × chord voice + 0 note, 1 octave, 2 duration, then 31 INS and 32 CMD), u16 cell count
//         and per cell u8 step and i16 value (only the cells that differ from the empty value are written; CMD:
//         letter × 256 + parameter).
//   SONG: u16 entry count, then per entry u8 pattern, u16 repeats, u16 tempo (0: the tune's).
//   SNGX (only if an entry is special or marks the restart): u16 entry count (that of SONG, otherwise
//         the chunk is ignored), then per entry u8 kind (0 Pattern, 1 Rest, 2 Note-off, 3 Loop), u16 argument
//         (length in steps, or entries replayed by the jump), u8 flags (1: restart point).
//   INST (only if an instrument exists): u8 count, then per instrument u8 slot (0 to 31), u32 length and the
//         instrument in compact JSON, in the format of a Patches library entry (name included).
//   TSEQ (only if a track has instrument sequences): u8 count, then per track u8 number, u32 length,
//         the PER (64 × i8), NP, MIX, GEN and FIX rows (64 × u8 each), u8 fixed note, f64 detune and f64 analog
//         randomness in cents, u8 hard sync, u8 chained instrument, u16 pitch matrix; then the timer waves:
//         u8 step of the SID wave, 16 × u8 volumes, u8 step of the Sync Buzzer, 8 × u8 R13 shapes; then the
//         buzzer: u8 tuned envelope, u8 N, u8 D, f64 fine tune in cents, u8 YM Pad; then the LFO:
//         u8 on, u8 shape, u8 target, u8 sync, f64 rate (Hz), u8 division, f64 depth, u8 retrigger; then
//         the echo of the notes: u8 on, u8 division, u8 repeats, u8 drop, i8 target track (-1: its own);
//         then u8 Sine button of the SID wave lit, u8 phase of its sine (0 to 15),
//         u8 period of its sine (2 to 16 bars), u8 width of the square wave (1 to 15 sixteenths).
//         Written for a track whose sequences, waves, buzzer, LFO or echo differ from the default.
//   TIMG (only when not default): rate of the sequencer lines: u8 timing (0 BPM, 1 Ticks), u8 speed
//         (ticks per line), u8 groove length, 16 × u8 ticks added to the lines.
//   DMAS (only when not default): STE DMA bank: u8 slot count, then per slot u8 rate (0-3), u8 stereo,
//         u8 root note, u8 loop, u32 byte count and the signed bytes (interleaved in stereo).
//   DIGS (only when not default): Digidrum editor: u8 slot count, then per slot u8 name length and the
//         name, f64 rate (Hz), u8 root note, f64 fine tune (cents), u8 loop, u32 start and u32 end of the loop, u8
//         conversion (0 Linear, 1 YM levels), u32 byte count and the signed 8-bit source. The played 4-bit values
//         stay in DIGI.
//   RECD (only if a take exists): register recorder take (Recorder.h): u8 chip, u32 replay tick (Hz),
//         u64 and u32 chip clock (numerator, denominator), u32 frame count, then per frame 14 × u8 registers R0 to
//         R13, u8 flags (1: R13 written; 2, 4, 8: start of the effect of channels A, B, C), and per channel u8
//         generator of the timer effect (255: none), u8 value, u8 prescaler and u16 data of the MFP timer.
//   RSYN (only if a frame of the take carries a Zxx): u32 count, then per entry u32 frame and u8 value of the
//         Zxx; kept apart so that RECD keeps its frame size for older versions.
// Reading: unknown chunks ignored, unknown parameters ignored, missing parameters at their default, values clamped,
// fields missing at the end of a track at their default; a more recent version refused (the state is not touched).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "plugincore/Digidrum.h"
#include "plugincore/Parameters.h"
#include "plugincore/Recorder.h"
#include "ymengine/Dma.h"
#include "ymengine/DrumKit.h"
#include "ymengine/Params.h"
#include "ymseq/Project.h"

namespace stym::plugin {

inline constexpr uint16_t kStateVersion = 1;

// Non-automatable settings. In `tracks` and `global`, the fields carried by parameters are ignored (Mapping.h):
// only the ornament, the arpeggio (Track, Land, mode, octaves, table, length) and the Digidrum slot of the tracks,
// the channel stolen by the Digidrum track, the scale and curve of the slides, and the sequencer settings (clock,
// resolution, loop, Song, time signature, metronome, Quantize to VBL) count. The sequencer settings of the tracks
// (octave, own length) come from `seq.tracks` (sanitize copies them into `tracks[].seq`); the gate of the tracks is
// the tNN_gate parameter (Project::tracks[].gate is not saved).
// Project instrument: the sound of a track stored under a name, what a library patch carries
// (plugincore/Instruments.h): sound and arpeggio (generator chosen by the user, without the roles of the 3-channel
// mode), level and gate of the track, kept to load it back into a track or store it in the library.
struct Instrument {
    bool used = false;
    std::string name;  // 16 printable ASCII characters at most
    ymengine::TrackPatch patch;
    ymengine::TrackArp arp;
    double level = 0.75, gate = 0.5;
    bool operator==(const Instrument&) const = default;
};

// The non-automatable settings described above Instrument. The processor owns the reference copy and edits it on the
// message thread by copy (editExtras); the audio thread only sees frozen copies (Driver.h, Snapshot).
struct NonParamState {
    std::array<ymengine::TrackParams, kTracks> tracks{};
    ymengine::GlobalParams global{};
    ymengine::KitParams kit = ymengine::kKitBanks[0].kit;
    std::array<std::vector<uint8_t>, ymengine::kDigidrumSlots> digidrums{};  // 4-bit values (0-15) played
    // Digidrum editor: name, playback (rate, root note, fine tune, loop), conversion, source.
    std::array<DigidrumSlotState, ymengine::kDigidrumSlots> digiSlots{};
    // Track names (renameTrack of the prototype): 8 printable ASCII characters at most; empty = number.
    std::array<std::string, kTracks> trackNames{};
    // Sequencer: patterns, Song, current pattern and banks, octave and own length of the tracks.
    ymseq::Project seq;
    // Project instruments, $01 to $20: the INS row of a step designates the slot + 1.
    std::array<Instrument, ymengine::kInstruments> instruments{};
    // STE DMA track: settings, signed bytes (kDmaMaxBytes at most) and names of the eight slots (the file
    // loaded, or typed; cleanDigidrumName).
    ymengine::DmaParams dma{};
    std::array<std::vector<int8_t>, ymengine::kDmaSlots> dmaSamples{};
    std::array<std::string, ymengine::kDmaSlots> dmaNames{};
};

// Rows of a pattern track as the state numbers them (PATN chunk): 0 to 15 those of the prototype (order of
// ymseq::Row and of LANEKEYS), 16 to 30 the note, octave and duration of each chord voice (voices 2 to 6), 31 INS,
// 32 to 37 the CMD rows Xyz, Sxx, Rxx, Zxx, Txx, Vxx, 38 to 46 those of the step effects (Axy, Pxy, Dxx, Cxx, Bxy, Nxy,
// Exx, Oxx, Fxx), 47 to 49 the switches of the twelfth DAW test (Lxx, Mxx, Jxx) and 50 to 52 the volume glide and
// the slide curves (Gxx, Kxx, Qxx); a command written in one of them goes to the row of its kind (Xyz
// and Pxy, retired, are dropped). Value of a cell, empty value of a row, write clamped as when
// reading a state (an unknown command leaves the cell empty; row or step out of range: nothing is written).
inline constexpr int kLaneRows = 53;
int laneCell(const ymseq::Lane& l, int row, int step) noexcept;
int laneEmpty(int row) noexcept;
void setLaneCell(ymseq::Lane& l, int row, int step, int value) noexcept;

// Patterns and Song as the engine plays them (tracks shared with the model, without copying the rows).
void fillSequence(const ymseq::Project& p, ymengine::SeqData& out);

inline constexpr size_t kTrackNameMax = 8;
// Track name brought back to the rule: printable ASCII characters, edge spaces removed, 8 characters at most.
std::string cleanTrackName(std::string_view name);

// Brings the non-automatable fields back into their ranges (after reading a state), those of a single track.
void sanitize(NonParamState& s);
void sanitize(ymengine::TrackParams& t);

// Everything the host saves for one instance (encodeState / decodeState).
struct PluginState {
    ParamValues values = defaultValues();
    NonParamState extras;
    // Last take of the register recorder, outside NonParamState: the interface copies the latter at each
    // setting change.
    RegisterTake take;
};

// The binary blob of the format above. Pure functions, usable on any thread.
std::vector<uint8_t> encodeState(const PluginState& state);

// Outcome of decodeState: the state read, or the reason of the refusal; counters of what was skipped.
struct DecodeResult {
    bool ok = false;
    std::string error;          // if !ok
    uint16_t version = 0;
    int unknownParams = 0;      // unknown ids ignored
    int unknownChunks = 0;      // unknown chunks ignored
    bool hasSequencer = false;  // SEQS chunk read (otherwise: default sequencer settings, see the processor)
    PluginState state;          // valid if ok
};
DecodeResult decodeState(std::span<const uint8_t> data);

}  // namespace stym::plugin
