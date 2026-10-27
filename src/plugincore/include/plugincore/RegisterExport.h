// STYM2149 — plugincore: register exports of the File menu (STYM2149_SPEC.md §8)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// collectYmFrames and collectYmFramesSong of the prototype, replaced by a capture: a second instance of the
// engine (OfflineRender) plays the current pattern (one pass) or the Song in Authentic 3ch, and the register
// recorder (Engine::startCapture) takes one frame per replay tick, from its start to the sequencer stop: what
// is heard, timer effects and R13 retriggers included (the prototype rebuilt an approximate state of the voices at
// each frame). The take has the shape of the recorder's (RegisterTake), which the same file writers export.
// With the STE machine, the DMA sound of each frame (ymengine::DmaFrame) is taken too, for the SNDH.
#pragma once
#include <atomic>
#include <optional>
#include <vector>

#include "plugincore/Offline.h"
#include "plugincore/Parameters.h"
#include "plugincore/Recorder.h"
#include "plugincore/State.h"

namespace stym::plugin {

// Result of captureRegisters, for the register file writers (RegisterFiles.h).
struct RegisterCapture {
    RegisterTake take;                    // empty outside Authentic 3ch mode
    std::vector<ymengine::DmaFrame> dma;  // one per frame with the STE machine, otherwise empty
    bool truncated = false;               // Song cut at one hour (kOfflineMaxSeconds)
};

// Frames of the current pattern or of the Song. nullopt if `cancel` becomes true.
std::optional<RegisterCapture> captureRegisters(OfflineRender::Source source, const ParamValues& values,
                                                const NonParamState& extras, const std::atomic<bool>* cancel = nullptr);

}  // namespace stym::plugin
