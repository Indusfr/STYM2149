// STYM2149 — ymio: stereo WAV files of the exports
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The prototype's wavBlobDv (16-bit stereo), extended to 24 bits and 32-bit float of §8. Classic RIFF header:
// "fmt" chunk of 16 bytes (PCM, format 1) or of 18 bytes with a "fact" chunk (IEEE float, format 3), then
// "data". The prototype's PCM conversion: clipping at ±1, × (2^(n−1) − 1), truncation towards zero; float is written
// as is. Deviations: the RIFF size is right (the prototype wrote half the data size there); the file stops at 4 GiB,
// the limit of the format (WavWriter::full).
#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "ymio/ByteSink.h"

namespace stym::ymio {

enum class WavFormat : uint8_t { Pcm16, Pcm24, Float32 };

int wavBytesPerFrame(WavFormat f) noexcept;  // stereo frame: 4, 6 or 8 bytes
// Complete header for `frames` stereo frames at `rate` Hz (44 bytes in PCM, 58 in float).
std::vector<uint8_t> wavHeader(WavFormat f, uint32_t rate, uint64_t frames);
// Converted frames, appended to `out` (left then right). Samples are floats with full scale at ±1; the shorter span
// sets the frame count.
void appendWavFrames(std::vector<uint8_t>& out, WavFormat f, std::span<const float> left, std::span<const float> right);
// Frames that the RIFF header can count.
uint64_t wavMaxFrames(WavFormat f) noexcept;

// File written as the render goes: provisional header, frames, then header completed (seek back) by finish.
// The sink must outlive the writer; finish() does not commit it.
class WavWriter {
public:
    // Writes the provisional header at the sink's current position. (The stems use wavHeader and appendWavFrames
    // inside ZIP entries instead.)
    WavWriter(ByteSink& sink, WavFormat format, uint32_t rate);
    // False if the file is full (the extra frames are left out) or if the write fails.
    bool write(std::span<const float> left, std::span<const float> right);
    bool finish();
    uint64_t frames() const noexcept { return frames_; }
    bool full() const noexcept { return frames_ >= wavMaxFrames(format_); }

private:
    ByteSink& sink_;
    WavFormat format_;
    uint32_t rate_;
    uint64_t start_, frames_ = 0;
    std::vector<uint8_t> buf_;
};

}  // namespace stym::ymio
