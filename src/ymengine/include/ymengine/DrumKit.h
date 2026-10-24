// STYM2149 — ymengine: drum kit of the Chipdrum generator
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Synthesis taken from the prototype (worklet, buildDrums and buildChipDrums; KIT_BANKS banks): eight adjustable
// instruments, rendered in two ways.
// - 16-channel mode: one 4-bit sample at 7 800 Hz per instrument, played like a digidrum.
// - 3-channel mode: a table of VBL frames (tone period, noise, mixer, volume) written to the registers.
// Both are identical to the prototype (tests [kit], data taken from the prototype by tools/proto_golden).
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "ymcore/ChipConfig.h"

namespace stym::ymengine {

// Settings of an instrument. Fields by family: kick and toms (pitch, sweep, dec, noise, vol), snare (body,
// tone, dec, vol), clap (space, color, tail, vol), hats (metal, tune, dec, vol). Units and ranges of the prototype.
struct DrumParams {
    double a = 0, b = 0, dec = 0, noise = 0, vol = 100;
    bool operator==(const DrumParams&) const = default;
};

// Instruments of the kit, in the order of KitParams::inst (the slot of a hit is its row in the grid).
enum class DrumSlot : uint8_t { Kick, Snare, Clap, Hat, OpenHat, ClosedHat, Tom1, Tom2 };
inline constexpr int kDrumSlots = 8;

// Chipdrum kit: the settings of its eight instruments (Engine::setKit).
struct KitParams {
    // Order of the slots: kick, snare, clap, hat, ohat, chat, tom, tom2 (KIT_SLOTS of the prototype).
    std::array<DrumParams, kDrumSlots> inst{};
    bool operator==(const KitParams&) const = default;
};

// Factory kit: its name and its settings.
struct KitBank {
    std::string_view name;
    KitParams kit;
};
// The 13 factory banks of the prototype; the first one ("Classic") is the default kit.
extern const std::array<KitBank, 13> kKitBanks;

// One VBL frame of a register drum.
struct ChipDrumFrame {
    uint16_t tonePeriod = 0;  // 0: no tone in this frame
    uint8_t noisePeriod = 0;  // 0: unchanged
    bool tone = false, noise = false;
    uint8_t volume = 0;       // 4-bit volume, 0 to 15
};

// Kit rendered for playback, held by the engine: rebuilt (build) when the kit, the VBL rate or the clock changes, read
// by the voices at their attack.
class DrumKit {
public:
    static constexpr uint32_t kSampleRate = 7800;
    static constexpr int kMaxFrames = 48;  // longest register drum, in VBL frames

    // Computes the samples and the frames. Without allocation; a few hundred microseconds: to be done outside
    // the audio thread when the kit changes during playback.
    void build(const KitParams& kit, uint32_t vblHz, ymcore::MasterClock clock) noexcept;

    // 4-bit sample (16-channel mode) and VBL frames (3-channel mode) of a slot; empty outside 0-7.
    std::span<const uint8_t> sample(int slot) const noexcept;
    std::span<const ChipDrumFrame> frames(int slot) const noexcept;

private:
    // Fixed durations of the prototype: 0.30 / 0.18 / 0.16 / 0.09 / 0.30 / 0.045 / 0.22 / 0.20 s at 7 800 Hz.
    static constexpr std::array<uint32_t, kDrumSlots> kLengths = {2340, 1404, 1248, 702, 2340, 351, 1716, 1560};
    // Start of each sample in samples_ (running sum of kLengths), and their total.
    static constexpr std::array<uint32_t, kDrumSlots> kOffsets = {0, 2340, 3744, 4992, 5694, 8034, 8385, 10101};
    static constexpr uint32_t kTotal = 11661;

    std::array<uint8_t, kTotal> samples_{};
    std::array<std::array<ChipDrumFrame, kMaxFrames>, kDrumSlots> frames_{};
    std::array<uint8_t, kDrumSlots> frameCount_{};
};

}  // namespace stym::ymengine
