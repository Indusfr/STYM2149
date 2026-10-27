// STYM2149 — ymio: writing SNDH files (STYM2149_SPEC.md §8)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// SNDH of one or more register takes (one per subsong: 1 = Song, 2 = current pattern), with its
// position-independent 68000 routine, written by a small assembler (Asm68k.h):
// - header with the tags that AtariAudio (SndhRenderer::Load) and parseSndh read: TITL, COMM, CONV, ##, TC (the play
//   is called by timer C at the replay rate), TIME, FRMS, HDNS; no FLAG tag (its form is only known from ym2149-rs,
//   to confirm in the SNDH documentation) and no PRSM mark;
// - frames: changed registers (14-bit mask, like the prototype), then commands for the timer effects and the DMA.
//   Each channel has its MFP timer: A, B and D (C stays with the system). SID: the volume toggles between the
//   note level and 0; Sync Buzzer: R13 rewritten (the envelope restarts); Digidrum: the 4-bit values of the slot
//   written one by one, then volume 0 at the end. Start at each retrigger of the take (the timer is stopped, loaded
//   then restarted), otherwise new reload or prescaler value like the engine (MfpTimer);
//   - chip writes are atomic (MOVEP); R7 keeps its bits 6 and 7 (the chip's I/O ports, wired on the Atari ST), read
//     then rewritten with interrupts masked;
// - STE machine: a single DMA channel, the most recent DMA note at each frame, its sample resampled at
//   its attack pitch and attack volume (shared copies), looping if the slot loops; ST machine: no access to the DMA
//   registers;
// - chip clock other than the Atari's 2 MHz: tone, noise and envelope periods recomputed;
// - sequencer Zxx commands (RegisterFrame::sync): the value xx is written into a sync byte of the routine at
//   the frame that carries it; the init sets it to 0 and returns its address in A0, for the demo that calls it.
// At the end of a subsong, the routine stops the effects and starts again from the first frame (like the prototype).
// The init saves the vectors and MFP registers it touches; the exit restores them and cuts the sound.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ymcore/ChipConfig.h"
#include "ymengine/Engine.h"

namespace stym::ymio {

// One take: a register frame per replay tick (and the DMA state of each frame on an STE).
struct SndhSubsong {
    std::span<const ymengine::RegisterFrame> frames;
    std::span<const ymengine::DmaFrame> dma;  // frame by frame, or empty (no DMA sound)
};

struct SndhDmaSample {
    std::span<const int8_t> data;  // signed bytes up to End of the slot, interleaved left, right if stereo
    uint8_t rate = 2;              // index into ymengine::kDmaRateHz
    bool stereo = false;
    bool loop = false;
};

struct SndhWriteInfo {
    uint32_t frameHz = 50;        // replay rate: frequency of timer C (TC tag)
    ymcore::MasterClock clock{};  // clock of the capture; periods are converted to the Atari's 2 MHz
    bool ste = false;  // STE machine: DMA sound
    std::string title, composer;
    std::array<std::span<const uint8_t>, 8> digidrums{};  // 4-bit values of the Digidrum slots
    std::array<SndhDmaSample, 8> dma{};                   // slots of the DMA bank
};

// What the SNDH does not render like the engine, in frames (channels included).
struct SndhReport {
    uint32_t clampedPeriods = 0;  // periods brought back into their range (clock other than 2 MHz)
    uint32_t droppedEffects = 0;  // timer effect not played (Digidrum without sample, generator not ported)
    uint32_t heldDrums = 0;       // digidrum still sounding after the end of its sample (held loop)
    uint32_t dmaOverlap = 0;      // several DMA voices at once: only the most recent plays
    uint32_t dmaChanges = 0;      // pitch or volume of a DMA note changed after the attack
    uint32_t dmaDropped = 0;      // DMA note without sample, or DMA on an ST machine
    uint32_t dmaTruncated = 0;    // resampled copies cut at kSndhMaxDmaCopy bytes
    uint32_t dmaCopies = 0;       // resampled copies in the file
};

inline constexpr size_t kSndhMaxDmaCopy = size_t(1) << 19;  // 512 KiB per copy

struct SndhWriteResult {
    std::vector<uint8_t> data;  // empty on error
    std::string error;
    SndhReport report;
    uint32_t syncOffset = 0;  // position in the file of the sync byte (Zxx), whose address the init returns in A0
};

// The file must fit in the memory of the AtariAudio machine (kSndhMaxSize); each subsong has at least one frame,
// kSndhWriteMaxSubsongs subsongs at most. Pure function: runs on any thread (the export worker).
inline constexpr int kSndhWriteMaxSubsongs = 99;  // the ## tag gives the number of subsongs in two decimal digits
SndhWriteResult sndhFile(std::span<const SndhSubsong> subsongs, const SndhWriteInfo& info);

}  // namespace stym::ymio
