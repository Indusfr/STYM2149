// STYM2149 — ymio: writing register dumps (M7, A5, B1, STYM2149_SPEC.md §8)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Frames of the register recorder (ymengine::RegisterFrame, one per replay tick) written as:
// - VBL source (`.s`): the prototype's vblSource, 14 bytes per frame as `dc.b`, R13 = $FF when it must not be
//   rewritten; the header gives the real rate instead of "(50 Hz)";
// - YM3: the prototype's ym3Blob, "YM3!" then the 14 registers stored register by register (Arnaud Carré's format,
//   read by ymio::parseYm);
// - YM6 (preferred format of §8): "YM6!" and "LeOnArD!" header (ST-Sound format documentation, as parseYm reads
//   it), interleaved 16-byte frames, timer effects in the free bits of R1, R3, R6, R8, R14 and R15, as YmPlayer
//   decodes them (AtariAudio rules, D-044): at most two effects per frame (SID, Digidrum, Sync Buzzer), digidrums as
//   4-bit values.
// VBL and YM3 have no timer effects: they are frozen there in their base state (prototype): SID at the high volume,
// Digidrum at zero volume, Sync Buzzer without R13 retrigger by its timer (R13 rewritten only at its attack).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ymcore/ChipConfig.h"
#include "ymengine/Engine.h"

namespace stym::ymio {

// Registers R0 to R13 of a frame of a dump without effects (VBL, YM3); R13 = $FF: do not rewrite it.
std::array<uint8_t, 14> frozenFrame(const ymengine::RegisterFrame& f);

// What the comment lines at the top of the VBL source say (frameHz: replay rate in Hz).
struct VblInfo {
    uint32_t frameHz = 50;
    std::string label;  // "pattern: 16 steps", "song: 3 entries"
    double bpm = 120;   // 0: no tempo in the header (recorder take)
};
// Assembler source of the VBL dump (text, \n line endings).
std::string vblSource(std::span<const ymengine::RegisterFrame> frames, const VblInfo& info);

// YM3 file: "YM3!" then the frozen registers R0-R13, register by register. No header: players assume 50 Hz and a
// 2 MHz clock, whatever the frames were recorded at.
std::vector<uint8_t> ym3File(std::span<const ymengine::RegisterFrame> frames);

// Header data of the YM6.
struct Ym6Info {
    uint32_t frameHz = 50;        // replay rate (Hz): one frame per replay tick
    ymcore::MasterClock clock{};  // written in whole Hz (rounded)
    uint32_t loopFrame = 0;       // frame the player loops back to (clamped to the last frame)
    std::string title, author, comment;
    // Samples of the engine's Digidrum slots (4-bit values 0-15), designated by RegisterFrame::Effect::value.
    std::array<std::span<const uint8_t>, 8> digidrums{};
};
// What the YM6 could not carry, in frames × channels.
struct Ym6Report {
    uint32_t droppedEffects = 0;  // effect without a free place (two per frame): frozen
    uint32_t frozenDrums = 0;     // digidrum the player does not play (started with no place, or looped after its end)
    uint32_t cutDrums = 0;        // digidrum stopped before its end by the engine (one timer period of deviation)
};
// Uncompressed YM6 (ymio::packLzh compresses it, D-161). `report`, when given, receives what the file could not carry.
std::vector<uint8_t> ym6File(std::span<const ymengine::RegisterFrame> frames, const Ym6Info& info,
                             Ym6Report* report = nullptr);

}  // namespace stym::ymio
