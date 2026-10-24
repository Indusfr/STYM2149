// STYM2149 — plugincore: WAV and stem exports of the File menu (M7, A4, STYM2149_SPEC.md §8, D-163, D-164, D-171)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// exportWav, exportSongWav, exportStems and exportSongStems of the prototype: the current pattern (one pass) or the
// Song (from its first entry to its end, D-163) rendered offline by a second instance of the engine (OfflineRender),
// in the format chosen in the WAV dialog (D-164: 16-bit, 24-bit or 32-bit float, 44.1 to 192 kHz). Clean cut when
// the sequencer stops, without a release tail, like the prototype; the resampling delay is compensated, so that the
// file starts and ends with the music.
// Stems: one isolated track per WAV, like isolate() of the prototype (only that track audible, even when muted, with
// its level and pan; here through solo), in a stored ZIP. Tracks: those that play at least one note during the
// render (the prototype took the tracks of the current pattern that have notes, even for the Song). Names
// "stym2149-track-<track>.wav", with the channel names of the prototype (A, B, C, Digi; 01 to 16 in 16-channel mode).
// The files are written as the render goes (ymio::ByteSink); they stop at one hour of music (kOfflineMaxSeconds) or
// at the limit of their format (4 GiB for a WAV or for the ZIP), and the export reports it.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "plugincore/Offline.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymio/ByteSink.h"
#include "ymio/Wav.h"

namespace stym::plugin {

// Rates offered by the WAV dialog (D-164), in addition to the host's.
inline constexpr std::array<uint32_t, 6> kWavRates = {44100, 48000, 88200, 96000, 176400, 192000};

// The choice of the WAV dialog (D-164).
struct WavSettings {
    ymio::WavFormat format = ymio::WavFormat::Pcm16;  // wavBlobDv of the prototype: 16-bit
    uint32_t rate = 48000;                             // Hz; the exports raise a rate below 8000 Hz to 8000 Hz
};

// Result of an audio export, for the end-of-export alert.
struct AudioExport {
    int files = 0;            // WAVs written (stems: 0 if no track plays)
    std::vector<int> tracks;  // stems: tracks written, in order
    double seconds = 0;       // length of each WAV
    bool truncated = false;   // cut at one hour or at the format limit
    bool failed = false;      // write refused by the destination
};

// Name of a track in the stems (stemName of the prototype): channels A, B, C, Digi with 3 channels (A1 … Digi2 with
// two chips), DMA1 to DMA8 for the DMA tracks, otherwise the number on two digits.
std::string stemName(const ParamValues& values, int track);

// Stereo WAV of the current pattern or of the Song. nullopt if `cancel` becomes true.
std::optional<AudioExport> exportWav(OfflineRender::Source source, const ParamValues& values,
                                     const NonParamState& extras, const WavSettings& settings, ymio::ByteSink& sink,
                                     const std::atomic<bool>* cancel = nullptr);
// ZIP of the stems. nullopt if `cancel` becomes true.
std::optional<AudioExport> exportStems(OfflineRender::Source source, const ParamValues& values,
                                       const NonParamState& extras, const WavSettings& settings, ymio::ByteSink& sink,
                                       const std::atomic<bool>* cancel = nullptr);

}  // namespace stym::plugin
