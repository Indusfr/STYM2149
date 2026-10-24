// STYM2149 — plugincore: files of the register exports (M7, A5, B1 to B3, STYM2149_SPEC.md §8, D-160 to D-162)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// exportSongVbl, exportSongYm3 and exportSongSndh of the prototype, plus the compressed YM6 (D-161) and the recorder
// take (B3): captured frames (captureRegisters, D-160) or kept take (RegisterTake, D-157), written by the ymio
// writers, with the Digidrum and DMA samples of the project. SNDH of the Song: subsong 1 = Song, 2 = current pattern
// (sndhBlob of the prototype), DMA sound with the STE machine (D-162); that of a take has no DMA sound (the recorder
// does not take it). What the file does not render like the engine is reported in text lines for the end-of-export
// alert.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/Recorder.h"
#include "plugincore/State.h"
#include "ymio/Lzh.h"

namespace stym::plugin {

// The four register formats (D-172): VBL dump as assembler source (.s), YM3 dump, YM6 in an LHA archive (D-161),
// SNDH with its 68000 replay routine (D-162).
enum class RegisterFormat : uint8_t { Vbl, Ym3, Ym6, Sndh };

// What the caller decides about the file beyond its content.
struct RegisterFileOptions {
    std::string name;  // file name (YM6: name kept in the LHA archive)
    uint32_t dosTime = ymio::lzhDosTime(1980, 1, 1, 0, 0, 0);  // date of the LHA archive (YM6)
};

// A written file, and what the end-of-export alert says about it.
struct RegisterFile {
    std::vector<uint8_t> bytes;  // empty: nothing written (see error)
    uint32_t frames = 0;         // frames of the tune (subsong 1 of the SNDH)
    uint32_t frameHz = 50;
    bool truncated = false;          // Song cut at one hour (kOfflineMaxSeconds)
    std::vector<std::string> notes;  // what the file does not render like the engine
    std::string error;               // file refused by its writer (SNDH too large…)

    double seconds() const noexcept { return frameHz ? double(frames) / double(frameHz) : 0.0; }
};

// Name offered: stym2149-song.s, .ym (YM3 and YM6), .sndh; stym2149-take.* for the take (D-060).
std::string registerFileName(RegisterFormat format, bool take);

// Song export (Authentic 3ch only, D-160): empty outside this mode, nullopt if `cancel` becomes true.
std::optional<RegisterFile> exportSongRegisters(RegisterFormat format, const ParamValues& values,
                                                const NonParamState& extras, const RegisterFileOptions& options,
                                                const std::atomic<bool>* cancel = nullptr);
// Recorder take (B3), with the Digidrum samples of the current project.
RegisterFile exportTake(RegisterFormat format, const RegisterTake& take, const ParamValues& values,
                        const NonParamState& extras, const RegisterFileOptions& options);

}  // namespace stym::plugin
