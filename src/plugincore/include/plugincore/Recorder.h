// STYM2149 — plugincore: register recorder take
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Frames of one chip of the 3-channel modes, one per replay tick (ymengine::RegisterFrame), taken while the host
// plays (Driver::armRecorder). The last take is kept in the plugin state (RECD block, State.h) for the YM6 and SNDH
// export; a new take replaces it.
#pragma once
#include <cstdint>
#include <vector>

#include "ymengine/Engine.h"

namespace stym::plugin {

// Maximum frames of a take: 43 min 41 s at 50 Hz, 10 min 55 s at 200 Hz (about 5 MB of buffer, allocated when
// arming).
inline constexpr uint32_t kRecorderMaxFrames = 1u << 17;

// A take with what its files need besides the frames: chip, replay rate and clock. Built by the driver at the end of
// a take (Driver::takeRecording) or by the register export (RegisterCapture); message thread and export job.
struct RegisterTake {
    uint8_t chip = 0;                    // chip recorded: 0, or 1 in dual chip
    uint32_t frameHz = 50;               // replay tick of the take (GlobalParams::vblHz at the start)
    uint64_t clockNum = 2'000'000;       // chip clock (ymcore::MasterClock: num / den Hz)
    uint32_t clockDen = 1;
    std::vector<ymengine::RegisterFrame> frames;

    bool empty() const noexcept { return frames.empty(); }
    double seconds() const noexcept { return frameHz ? double(frames.size()) / double(frameHz) : 0.0; }
    bool operator==(const RegisterTake&) const = default;
};

}  // namespace stym::plugin
