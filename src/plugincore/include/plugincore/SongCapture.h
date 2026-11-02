// STYM2149 — plugincore: what the Song plays in Authentic 3ch, frame by frame, for the tracker exports
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The capture of the register exports, with the steps and the note attacks of the sequencer, so that a
// tracker export (maxYMiser, TTrak) writes one row per step and a note where the sequencer starts one.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymengine/Engine.h"

namespace stym::plugin {

struct SongAttack {
    int frame = 0;
    int note = -1;  // MIDI note; -1: a digidrum that starts without an attack of the sequencer
    int track = -1;
};

struct SongCapture {
    std::vector<ymengine::RegisterFrame> frames;
    std::vector<int> rowFrame;  // first frame of each step played
    std::vector<int> rowEntry;  // its Song entry
    std::vector<int> rowStep;   // its step in the pattern
    std::array<std::vector<SongAttack>, 3> attacks;  // the attacks of the sequencer on each channel
    uint32_t hz = 50;
    bool dma = false, truncated = false;  // the STE DMA plays; the Song was cut at kOfflineMaxSeconds
};

// The Song played once through in the settings of `values` (an hour at most); nullopt if `cancel` becomes true.
std::optional<SongCapture> captureSong(const ParamValues& values, const NonParamState& extras,
                                       const std::atomic<bool>* cancel);

}  // namespace stym::plugin
