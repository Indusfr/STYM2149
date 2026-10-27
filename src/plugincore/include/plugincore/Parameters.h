// STYM2149 — plugincore: automatable parameters of the plugin (STYM2149_SPEC.md §7)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Parameter table, independent of JUCE: stable ids (never renamed after publication, D-057), names, ranges,
// defaults of the prototype (mkCh, GLOB and sliders). Values travel "in plain units" (units of the table: %, ms, Hz,
// choice index), in an array indexed by paramIndex().
//
// Sequencer parameters (M5): free tempo, swing, gate of each track and global gate; the notes, patterns and tables
// are not parameters (§7).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "ymengine/Params.h"

namespace stym::plugin {

using ymengine::kTracks;

// Kind of value, which decides how the processor builds the host parameter (makeParameter) and how clampToSpec
// rounds it: Float on its step, Int on whole steps, Bool 0 or 1, Choice an index into ParamSpec::choices.
enum class ParamKind : uint8_t { Float, Int, Bool, Choice };
// Mapping of the normalised position t ∈ [0, 1]: linear, or geometric v = min × (max / min)^t.
enum class ParamScale : uint8_t { Linear, Log };

// Global settings.
enum class Global : uint8_t {
    Master,       // main level, 0-100 % (GLOB.master × 100)
    VoiceMode,    // 16 ch / Authentic 3ch / Dual chip 2x3ch
    ChipClock,    // chip clock: Custom (ChipClockHz, 16 ch only) or a preset of ChipConfig.h (D-237)
    OutputChain,  // output chain Raw / ST / STE (D-047)
    Resampling,   // conversion to the host rate: sinc, AtariAudio averaging, Retro (D-014)
    SteBass,      // bass of the LMC1992, -12 to +12 dB in steps of 2
    SteTreble,    // treble of the LMC1992
    VblRate,      // replay tick: 50 / 60 / 70 / 100 / 200 Hz
    SoftClip,     // tanh saturation of the output (D-049)
    Timer,        // effect timers: MFP 68901 or continuous
    FocusTrack,   // track played from the keyboard (1-16)
    MidiRouting,  // Omni (focused track) / Layer / per channel
    Velocity,     // accent / 4-bit volume / ignored
    BendRange,    // pitch bend range, semitones
    WheelDepth,   // vibrato of the mod wheel at full, cents
    ArpClock,     // arpeggio clock: internal / MIDI clock
    ArpDivision,  // division of the MIDI clock
    FreeTempo,    // sequencer tempo in Free run, 60-240 BPM (M5)
    Swing,        // sequencer swing, 50-75 % (M5)
    Gate,         // global gate, 5-95 % of the step (Indus's request, D-134)
    GateAll,      // global gate played by all tracks instead of their own (D-134)
    Computer,     // ST / STE machine: STE DMA sound (M6, D-153)
    ChipClockHz,  // frequency of the Custom chip clock, played in 16 ch only (fourth DAW test, D-237)
    ArpSync,      // arpeggio cadence: Rate in Hz, or a division of the tempo (sixth DAW test, D-256)
    Master2,      // with MasterLink off, 0-100 %: the right output, or chip 2 in 2x3 ch (seventh DAW test, D-284)
    MasterPan,    // balance of the outputs, -100 (left only) to +100 (right only) (D-284)
    MasterLink,   // Master sets both; off: Master the left output (chip 1), Master2 the right one (chip 2) (D-284)
    // Audio delay, fed by the Send of each track; 16 ch only (eighth DAW test, D-299).
    DelayOn,
    DelaySync,      // time in ms, or a division of the tempo
    DelayDivision,  // kDelayDivisionBeats
    DelayTime,      // 10-2000 ms
    DelayFeedback,  // 0-90 %
    DelayHiCut,     // low-pass of each repeat, 500-20000 Hz
    DelayLevel,     // level of the echoes, 0-100 %
    DelayPingPong,  // echoes alternately left and right (stereo outputs)
    A4Hz,           // frequency of A4, from which every note is tuned (Tuning A4, D-429)
    // MIDI expression (D-432): what the bend, the wheel and the aftertouch drive; MPE in 16 ch (lower zone: channel 1
    // the master, 2 to 16 a note each), with the bend range of its notes and what their CC74 drives.
    BendTarget,     // Pitch / Volume / Envelope
    WheelTarget,    // Off / Vibrato / Volume / Envelope / LFO
    Aftertouch,     // the targets of the wheel, for the channel and polyphonic pressure
    Mpe,
    MpeBendRange,   // semitones
    MpeSlide,       // Volume / Envelope / Vibrato
    Count
};

// Settings of each track. On, Level, Pan, Mute, Solo and Send form the mixer strip (applied without rebuilding the
// track's sound, Driver.cpp); the others are the track's sound. A new field goes just before Count, here as in
// Global (docs/ARCHITECTURE.md §8).
enum class Track : uint8_t {
    On, Level, Pan, Mute, Solo,
    Tone, Noise, NoisePeriod, Volume, VolMode, EnvPeriod, EnvShape,
    Generator, SidRatio, SidVmin,
    VibDepth, VibRate, Attack, Decay, Sustain, Release, AdsrMode, Voicing,
    ArpOn, ArpRate,
    Gate,  // length of a one-step note, 5-95 % of the step (Gate slider of the prototype, CH[].gate) (M5)
    VibOn,  // vibrato On/Off (sixth DAW test, D-254)
    Send,   // send to the delay, 0-100 %, after the fader and the pan (eighth DAW test, D-299)
    // Right side of a stereo DMA sample (D-459): its fader (without Link), mute and solo; Link: Level for both sides.
    LevelR, MuteR, SoloR, Link,
    Count
};

// Settings of the Mixer alone (pan, mute, solo, send, the right side of a stereo DMA sample): neither in patches nor in
// the undo step of a channel, not copied with a track. The Level is the track's (patches, Copy).
constexpr bool mixerOnly(Track f) noexcept {
    switch (f) {
    case Track::Pan:
    case Track::Mute:
    case Track::Solo:
    case Track::Send:
    case Track::LevelR:
    case Track::MuteR:
    case Track::SoloR:
    case Track::Link: return true;
    default: return false;
    }
}

// Custom chip clock (D-237): the YM Player's range (D-169); the default is the 2 MHz entry it replaces.
inline constexpr int kCustomClockMinHz = 100'000, kCustomClockMaxHz = 8'000'000, kCustomClockDefaultHz = 2'000'000;

inline constexpr int kGlobalParams = int(Global::Count);
inline constexpr int kTrackParams = int(Track::Count);
inline constexpr int kParamCount = kGlobalParams + kTracks * kTrackParams;

// Position of a parameter in ParamValues and in the host's list: the globals first, then the kTrackParams fields of
// track 0 (0-based track), of track 1, and so on.
constexpr int paramIndex(Global g) noexcept { return int(g); }
constexpr int paramIndex(int track, Track t) noexcept { return kGlobalParams + track * kTrackParams + int(t); }

// Description of one parameter (an entry of paramSpecs()), immutable once built; read from any thread.
struct ParamSpec {
    std::string id;     // stable id
    std::string name;   // name shown by the host (English, like the interface of the prototype)
    std::string group;  // "Global" or "Track n"
    ParamKind kind = ParamKind::Float;
    float min = 0, max = 1, step = 0, def = 0;  // range, step (0: continuous) and default, in plain units
    ParamScale scale = ParamScale::Linear;
    std::string_view unit;                       // unit shown by the host ("%", "ms", "Hz"...), may be empty
    std::span<const std::string_view> choices;   // labels of a Choice, in index order (static arrays)
    int version = 1;    // version in which the id appeared (JUCE ParameterID, needed by the AU)
    bool automatable = true;
    int track = -1;     // -1: global
    int field = 0;      // Global or Track
};

// Complete table (built once, stateless afterwards).
const std::array<ParamSpec, kParamCount>& paramSpecs();
inline const ParamSpec& paramSpec(int index) { return paramSpecs()[size_t(index)]; }
// Index of an id, -1 if it is unknown.
int findParam(std::string_view id);

// Plain values of all parameters.
using ParamValues = std::array<float, kParamCount>;
// The default of every parameter (ParamSpec::def).
ParamValues defaultValues();
// Value clamped to the range and brought to the step (integers, choices, booleans, step of floats).
float clampToSpec(const ParamSpec& s, float v) noexcept;
// Normalised position ↔ plain value (without step).
float toNormalized(const ParamSpec& s, float v) noexcept;
float fromNormalized(const ParamSpec& s, float t) noexcept;

// Divisions of the arpeggio in half pulses of the MIDI clock (48 per quarter note) per step, in the order of the
// ArpDivision choice: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32, Tick, then 1/32T and 1/64 added after the existing
// choices so that saved projects keep theirs (D-289).
inline constexpr std::array<uint8_t, 9> kArpDivisionTicks = {48, 24, 16, 12, 8, 6, 2, 4, 3};
// The same choices in musical order, as the menus show them (D-290).
inline constexpr std::array<int, 9> kArpDivisionOrder = {0, 1, 2, 3, 4, 5, 7, 8, 6};
inline constexpr int kArpDivisionTick = 6;  // one MIDI clock pulse: not offered with Sync (D-287)
// Replay tick: VBL frames of the ST (prototype), then 100 and 200 Hz (M6, D-149; rates of TTrak and maxYMiser).
inline constexpr std::array<uint32_t, 5> kVblRates = {50, 60, 70, 100, 200};

}  // namespace stym::plugin
