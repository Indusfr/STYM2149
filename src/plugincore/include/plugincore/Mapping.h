// STYM2149 — plugincore: from the plugin parameters to the engine settings
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The fields that the parameters do not carry (ornament, arpeggio mode and table, Digidrum slot, channel stolen by the
// Digidrum track, scale and curve of the slides) come from the non-automatable state (State.h): the functions
// toGlobal() and toTrack() start from these base settings and write into them the fields carried by the parameters.
#pragma once
#include <cmath>
#include <cstdint>

#include "plugincore/Parameters.h"
#include "ymcore/ChipConfig.h"
#include "ymengine/Dma.h"
#include "ymengine/Params.h"

namespace stym::plugin {

// MIDI Routing choice: Omni, every channel plays the focused track; Layer, all active tracks; PerChannel,
// channel n plays track n.
enum class MidiRouting : uint8_t { Omni, Layer, PerChannel };

// Plain value of a parameter; intValue rounds it (integers, choices, booleans).
inline float value(const ParamValues& v, Global g) noexcept { return v[size_t(paramIndex(g))]; }
inline float value(const ParamValues& v, int track, Track f) noexcept { return v[size_t(paramIndex(track, f))]; }
inline int intValue(const ParamValues& v, Global g) noexcept { return int(std::lround(value(v, g))); }
inline int intValue(const ParamValues& v, int track, Track f) noexcept {
    return int(std::lround(value(v, track, f)));
}

ymengine::VoiceMode voiceMode(const ParamValues& v) noexcept;
// Clock of the chosen chip (ChipClock): the Custom clock plays ChipClockHz in 16 ch and 2 MHz in the other modes.
ymcore::MasterClock chipClock(const ParamValues& v) noexcept;
// Frequency of the Custom clock (ChipClockHz, clamped), whatever the mode.
uint32_t customClockHz(const ParamValues& v) noexcept;
// Number of chips of the mode (0 in 16 channels).
int chipCount(ymengine::VoiceMode mode) noexcept;
MidiRouting midiRouting(const ParamValues& v) noexcept;

// STE machine (Computer parameter): STE DMA sound.
bool steMachine(const ParamValues& v) noexcept;
// DMA tracks in 3 channels with the STE machine: the eight tracks that follow those of the chips (5 to 12, or 9 to 16).
bool dmaTrack(ymengine::VoiceMode mode, bool ste, int track) noexcept;
// Tracks played: 16 in 16 channels; in 3 channels, four per chip, plus the eight DMA tracks with the STE machine.
int trackCount(ymengine::VoiceMode mode, bool ste) noexcept;
int trackCount(const ParamValues& v) noexcept;

// Fixed roles of the tracks in 3 channels (fxAllowed of the prototype): the fourth track of each chip is the Digidrum
// track and plays only samples; a DMA track plays only DMA; a hardware channel plays neither. The DMA type requires
// the STE machine (Indus's table of 16/10/2026).
bool generatorAllowed(ymengine::VoiceMode mode, bool ste, int track, ymengine::Generator g) noexcept;
// Generator played: the parameter's, or the imposed role (Digidrum for the Digidrum track, DMA for a DMA track, Chip
// Voice instead of a Digidrum on a hardware channel or of a DMA without the STE machine). The parameter is not
// modified: going back to 16 channels finds the user's setting again, as the prototype does (ch3Saved).
ymengine::Generator effectiveGenerator(const ParamValues& v, int track) noexcept;
// Gate played by a track, in % of the step: the global gate when Gate All is on, otherwise the track's.
// The GAT of a step keeps priority in the engine; a drum track has no gate.
int trackGate(const ParamValues& v, int track) noexcept;

// Sequencer clock: "MIDI ext" on the Clock selector (arpClock parameter) makes the arpeggio and the sequencer follow
// the MIDI clock, like setClkMode of the prototype; "Internal" leaves the state's (Host sync in a host, Free
// run in Standalone).
ymengine::SeqClock seqClockOf(const ParamValues& v, ymengine::SeqClock base) noexcept;
// Engine settings: a copy of `base` (the non-automatable state) with the fields carried by the parameters written
// over it. toMix gives the mixer strip of a track alone (on, level, pan, mute, solo, send).
ymengine::GlobalParams toGlobal(const ParamValues& v, const ymengine::GlobalParams& base) noexcept;
ymengine::TrackParams toTrack(const ParamValues& v, int track, const ymengine::TrackParams& base) noexcept;
ymengine::TrackMix toMix(const ParamValues& v, int track) noexcept;

// Settings that require a new preparation of the engine (allocations): clock, chain, resampling.
struct ConfigKey {
    uint8_t clock = 0, chain = 0, resampling = 0;  // indexes of the ChipClock, OutputChain and Resampling choices
    uint32_t hz = 0;  // frequency of the Custom clock (clock 0); 0 with the other clocks
    bool operator==(const ConfigKey&) const = default;
};
// Configuration wanted by the parameters (Driver compares it with that of its engine).
ConfigKey configKey(const ParamValues& v) noexcept;
// Preparation of an engine for `key`: chip clock, host rate (Hz), output mode (sinc above clk/8), board chain
// with the STE bass and treble, and the separate outputs (bit n = track n).
ymengine::EngineConfig engineConfig(const ConfigKey& key, const ParamValues& v, uint32_t hostRate,
                                    uint16_t trackOutputs) noexcept;

// Controllers of the prototype (onCC): parameter value produced by a CC, in plain units.
// Envelope period of CC 74: position ef = round(v / 127 × 4000) of the "Env freq" slider, EP of the prototype
// = round(65535^(1 - ef / 4000)) clamped to 1-65535 (calculation of the prototype, efToHz), then chip EP = EP × 32.
uint32_t envPeriodFromCc(int v) noexcept;

}  // namespace stym::plugin
