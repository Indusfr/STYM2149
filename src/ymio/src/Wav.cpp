// STYM2149 — ymio: stereo WAV files of the exports (M7, A4, D-164)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/Wav.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace stym::ymio {

namespace {

void le16(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
void le32(std::vector<uint8_t>& o, uint32_t v) {
    le16(o, v & 0xFFFF);
    le16(o, v >> 16);
}
void tag(std::vector<uint8_t>& o, const char (&t)[5]) { o.insert(o.end(), t, t + 4); }

// RIFF/WAVE (12) + "fmt " chunk (8 + 16) + "data" chunk header (8); float: "fmt " of 18 and a "fact" chunk (12).
size_t headerBytes(WavFormat f) noexcept { return f == WavFormat::Float32 ? 58 : 44; }

// The prototype's (x | 0): truncation towards zero (a NaN too, which the prototype wrote as 0).
int32_t pcm(float x, double scale) noexcept {
    if (std::isnan(x)) return 0;
    return int32_t(std::clamp(double(x), -1.0, 1.0) * scale);
}

}  // namespace

int wavBytesPerFrame(WavFormat f) noexcept {
    switch (f) {
    case WavFormat::Pcm16: return 4;
    case WavFormat::Pcm24: return 6;
    case WavFormat::Float32: return 8;
    }
    return 4;
}

uint64_t wavMaxFrames(WavFormat f) noexcept {
    // RIFF size (file − 8) on 32 bits.
    return (uint64_t(0xFFFFFFFFu) - (headerBytes(f) - 8)) / uint64_t(wavBytesPerFrame(f));
}

std::vector<uint8_t> wavHeader(WavFormat f, uint32_t rate, uint64_t frames) {
    frames = std::min(frames, wavMaxFrames(f));
    const uint32_t bpf = uint32_t(wavBytesPerFrame(f));
    const auto data = uint32_t(frames * bpf);
    const bool fl = f == WavFormat::Float32;
    std::vector<uint8_t> o;
    o.reserve(headerBytes(f));
    tag(o, "RIFF");
    le32(o, uint32_t(headerBytes(f) - 8) + data);
    tag(o, "WAVE");
    tag(o, "fmt ");
    le32(o, fl ? 18 : 16);
    le16(o, fl ? 3 : 1);  // WAVE_FORMAT_IEEE_FLOAT or PCM
    le16(o, 2);           // channels
    le32(o, rate);
    le32(o, rate * bpf);  // bytes per second
    le16(o, bpf);         // block align: bytes per stereo frame
    le16(o, bpf * 4);  // bits per sample: 16, 24 or 32
    if (fl) {
        le16(o, 0);  // cbSize
        tag(o, "fact");
        le32(o, 4);
        le32(o, uint32_t(frames));  // frames per channel
    }
    tag(o, "data");
    le32(o, data);
    return o;
}

void appendWavFrames(std::vector<uint8_t>& out, WavFormat f, std::span<const float> left,
                     std::span<const float> right) {
    const size_t n = std::min(left.size(), right.size());
    out.reserve(out.size() + n * size_t(wavBytesPerFrame(f)));
    for (size_t i = 0; i < n; ++i) {
        for (const float x : {left[i], right[i]}) {
            switch (f) {
            case WavFormat::Pcm16: le16(out, uint32_t(pcm(x, 32767.0)) & 0xFFFF); break;
            case WavFormat::Pcm24: {
                const auto v = uint32_t(pcm(x, 8388607.0));
                out.push_back(uint8_t(v));
                out.push_back(uint8_t(v >> 8));
                out.push_back(uint8_t(v >> 16));
                break;
            }
            case WavFormat::Float32: {
                uint32_t v;
                std::memcpy(&v, &x, 4);
                le32(out, v);
                break;
            }
            }
        }
    }
}

WavWriter::WavWriter(ByteSink& sink, WavFormat format, uint32_t rate)
    : sink_(sink), format_(format), rate_(rate), start_(sink.position()) {
    const std::vector<uint8_t> h = wavHeader(format_, rate_, 0);
    sink_.write(h);
}

// Writes what still fits under wavMaxFrames; false when frames had to be left out or the sink failed.
bool WavWriter::write(std::span<const float> left, std::span<const float> right) {
    size_t n = std::min(left.size(), right.size());
    const uint64_t room = wavMaxFrames(format_) - std::min(frames_, wavMaxFrames(format_));
    const bool cut = n > room;
    n = size_t(std::min<uint64_t>(n, room));
    buf_.clear();
    appendWavFrames(buf_, format_, left.first(n), right.first(n));
    frames_ += n;
    return sink_.write(buf_) && !cut;
}

// Rewrites the header with the real sizes, then goes back to the end of the file.
bool WavWriter::finish() {
    const uint64_t end = sink_.position();
    const std::vector<uint8_t> h = wavHeader(format_, rate_, frames_);
    return sink_.seek(start_) && sink_.write(h) && sink_.seek(end);
}

}  // namespace stym::ymio
