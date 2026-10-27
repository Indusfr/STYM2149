// STYM2149 — plugincore: sample of the STE DMA track (M6, B8, D-153) from a file, its tools
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Loading like the Digidrums (toDigidrum): linear resampling to the slot rate (the next sample counts as 0 after the
// end), then signed bytes round(v × 127) clamped to -128..127, 32 KB at most (16 KB per channel in stereo, bytes
// interleaved left then right). Mono: first channel of the file, like the Digidrums; stereo: the first two (a single
// channel: copied). Raw file (.raw, .spl, .sam): bytes taken as they are.
// Destructive tools (X15: maxYMiser): Sign flips the most significant bit of each byte (signed ↔ unsigned), Gain
// multiplies by a gain in dB and clips (steps in a row from the bytes before them, D-453); Normalize, Reverse and Fade
// out are those of the Digidrums (D-450).
#pragma once
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace stym::plugin {

// Sample at `targetRate`, stereo when `right` is not empty (the caller passes the same channel twice to make stereo
// from a mono file).
std::vector<int8_t> toDmaSample(std::span<const float> left, std::span<const float> right, double sampleRate,
                                double targetRate);
// Resample option of the DMA window (D-454): the bytes read at `fromHz` taken again at `toHz` by the same linear
// interpolation, so that the sample keeps its length in time and its pitch; 32 KB at most (a longer one is cut).
std::vector<int8_t> dmaResample(std::span<const int8_t> bytes, bool stereo, double fromHz, double toHz);
// Raw file, cut to 32 KB; in stereo, to an even count so that each frame has both of its bytes.
std::vector<int8_t> dmaRaw(std::span<const uint8_t> bytes, bool stereo);
// The Sign and Gain tools (see above), in place.
void dmaSign(std::vector<int8_t>& bytes) noexcept;
void dmaGain(std::vector<int8_t>& bytes, double db) noexcept;

// A tool of the DMA window on a sample (D-450): mono, the whole sample; stereo, the channels chosen (`channels` bit 0
// left, bit 1 right), each taken out of the interleaved bytes, processed and put back, so that Reverse and Fade out
// follow the frames. Normalize gives the chosen channels one gain, keeping their balance. `db`: step of Gain.
enum class DmaTool : uint8_t { Normalize, Reverse, FadeOut, Sign, Gain };
void dmaTool(std::vector<int8_t>& bytes, bool stereo, unsigned channels, DmaTool tool, double db = 0);

// Gain steps in a row (-3 dB, +3 dB of the DMA and Digidrums windows, D-453): each one computed from the bytes before
// the first, with the total of the run, so that a step down after a clipped step up gives the clipped peaks back. The
// run ends when the bytes are no longer those its last step left (another tool, an undo, another slot or channels).
class GainRun {
public:
    // Bytes the run's total, `db` included, applies to: `now`, the present bytes of `key` (slot and channels), when a
    // new run starts; otherwise the bytes before its first step.
    const std::vector<int8_t>& step(int key, const std::vector<int8_t>& now, double db);
    double total() const noexcept { return db_; }
    void gave(std::vector<int8_t> bytes) { last_ = std::move(bytes); }  // the bytes the step left

private:
    int key_ = -1;
    std::vector<int8_t> base_, last_;
    double db_ = 0;
};

}  // namespace stym::plugin
