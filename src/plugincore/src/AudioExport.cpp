// STYM2149 — plugincore: WAV and stem exports of the File menu (STYM2149_SPEC.md §8)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/AudioExport.h"

#include <algorithm>
#include <bitset>
#include <span>

#include "plugincore/Mapping.h"
#include "ymio/Zip.h"

namespace stym::plugin {

using ymengine::Engine;
using ymengine::VoiceMode;
using ymio::WavFormat;

namespace {

constexpr std::array<const char*, 4> kVoiceNames1 = {"A", "B", "C", "Digi"};
constexpr std::array<const char*, 8> kVoiceNames2 = {"A1", "B1", "C1", "Digi1", "A2", "B2", "C2", "Digi2"};

bool cancelled(const std::atomic<bool>* cancel) noexcept { return cancel && cancel->load(std::memory_order_relaxed); }

uint64_t maxMusicFrames(uint32_t rate) noexcept { return uint64_t(kOfflineMaxSeconds * rate); }

// Render compensated for the resampling delay: the frames [latency, stop + latency) of the raw render, given to
// `write` chunk by chunk, `maxFrames` at most. False if cancelled.
template <class Write>
bool renderCompensated(OfflineRender& r, uint64_t maxFrames, const std::atomic<bool>* cancel, Write&& write,
                       AudioExport& result, uint64_t& frames) {
    std::vector<float> left(OfflineRender::kChunk), right(OfflineRender::kChunk);
    ymengine::Outputs out;
    out.left = left.data();
    out.right = right.data();
    const uint64_t lat = r.latencySamples();
    uint64_t raw = 0;
    frames = 0;
    for (;;) {
        if (cancelled(cancel)) return false;
        const uint32_t n = r.renderRaw(out, OfflineRender::kChunk);
        const bool stopped = r.stopSample() >= 0;
        const uint64_t end = stopped ? uint64_t(r.stopSample()) + lat : UINT64_MAX;
        // This chunk covers [raw, raw + n) of the raw render: only its part inside [latency, end) is written.
        const uint64_t from = std::max(raw, lat), to = std::min(raw + n, end);
        if (to > from) {
            uint64_t take = to - from;
            if (frames + take > maxFrames) {
                take = maxFrames - frames;
                result.truncated = true;
            }
            // A refused write ends the render; the export then reports the failure.
            if (take > 0 && !write(std::span<const float>(left).subspan(size_t(from - raw), size_t(take)),
                                   std::span<const float>(right).subspan(size_t(from - raw), size_t(take)))) {
                result.failed = true;
                return true;
            }
            frames += take;
            if (result.truncated) return true;
        }
        raw += n;
        if (raw >= end) return true;
    }
}

// Tracks that play at least one note (attacks of the sequencer log), and length of the render.
std::optional<std::vector<int>> playedTracks(OfflineRender::Source source, const ParamValues& values,
                                             const NonParamState& extras, uint32_t rate,
                                             const std::atomic<bool>* cancel) {
    OfflineRender r(source, rate, values, extras);
    std::bitset<kTracks> played;
    const uint64_t max = maxMusicFrames(rate);
    while (!r.finished() && r.position() < max) {
        if (cancelled(cancel)) return std::nullopt;
        if (r.advance(OfflineRender::kChunk) == 0 && !r.finished()) break;
        for (const Engine::SeqLogRecord& e : r.log())
            if (e.op == 'T' && e.track >= 0 && e.track < kTracks) played.set(size_t(e.track));
    }
    std::vector<int> tracks;
    for (int t = 0; t < kTracks; ++t)
        if (played[size_t(t)]) tracks.push_back(t);
    return tracks;
}

// isolate() of the prototype: only that track audible, even when muted (its level and pan stay).
ParamValues isolated(const ParamValues& values, int track) {
    ParamValues v = values;
    for (int t = 0; t < kTracks; ++t)
        for (const Track f : {Track::Solo, Track::SoloR}) v[size_t(paramIndex(t, f))] = t == track ? 1.0f : 0.0f;
    v[size_t(paramIndex(track, Track::Mute))] = 0.0f;
    v[size_t(paramIndex(track, Track::MuteR))] = 0.0f;  // both sides of a stereo DMA sample
    return v;
}

}  // namespace

std::string stemName(const ParamValues& values, int track) {
    const VoiceMode mode = voiceMode(values);
    if (mode != VoiceMode::Studio16) {
        const int chips = mode == VoiceMode::Dual3 ? 2 : 1;
        if (dmaTrack(mode, steMachine(values), track)) return "DMA" + std::to_string(track - 4 * chips + 1);
        if (chips == 2 && track < 8) return kVoiceNames2[size_t(track)];
        if (chips == 1 && track < 4) return kVoiceNames1[size_t(track)];
    }
    std::string n = std::to_string(track + 1);
    return n.size() < 2 ? "0" + n : n;
}

std::optional<AudioExport> exportWav(OfflineRender::Source source, const ParamValues& values,
                                     const NonParamState& extras, const WavSettings& settings, ymio::ByteSink& sink,
                                     const std::atomic<bool>* cancel) {
    const uint32_t rate = std::max<uint32_t>(8000, settings.rate);
    OfflineRender r(source, rate, values, extras);
    ymio::WavWriter wav(sink, settings.format, rate);
    AudioExport out;
    uint64_t frames = 0;
    const uint64_t max = std::min(maxMusicFrames(rate), ymio::wavMaxFrames(settings.format));
    const bool done = renderCompensated(
        r, max, cancel, [&wav](std::span<const float> l, std::span<const float> rr) { return wav.write(l, rr); }, out,
        frames);
    if (!done) return std::nullopt;
    if (!wav.finish()) out.failed = true;
    out.failed = out.failed || !sink.ok();
    out.files = out.failed ? 0 : 1;
    out.seconds = double(frames) / rate;
    return out;
}

std::optional<AudioExport> exportStems(OfflineRender::Source source, const ParamValues& values,
                                       const NonParamState& extras, const WavSettings& settings, ymio::ByteSink& sink,
                                       const std::atomic<bool>* cancel) {
    const uint32_t rate = std::max<uint32_t>(8000, settings.rate);
    const std::optional<std::vector<int>> tracks = playedTracks(source, values, extras, rate, cancel);
    if (!tracks) return std::nullopt;
    AudioExport out;
    if (tracks->empty()) return out;
    // Room for each WAV in a ZIP of 4 GiB at most: local and central headers (30 and 46 bytes, each followed by the
    // name), WAV header, end of directory (22 bytes).
    const size_t header = ymio::wavHeader(settings.format, rate, 0).size();
    uint64_t overhead = 22;
    std::vector<std::string> names;
    for (const int t : *tracks) {
        names.push_back("stym2149-track-" + stemName(values, t) + ".wav");
        overhead += 30 + 46 + 2 * names.back().size() + header;
    }
    const uint64_t bpf = uint64_t(ymio::wavBytesPerFrame(settings.format));
    const uint64_t zipFrames = (ymio::ZipWriter::kMaxBytes - overhead) / (bpf * tracks->size());
    const uint64_t max = std::min({maxMusicFrames(rate), ymio::wavMaxFrames(settings.format), zipFrames});
    ymio::ZipWriter zip(sink);
    std::vector<uint8_t> buf;
    uint64_t frames = 0;
    for (size_t k = 0; k < tracks->size(); ++k) {
        OfflineRender r(source, rate, isolated(values, (*tracks)[k]), extras);
        AudioExport part;
        // The WAV header has its room reserved at the start of the entry; it is written by zip.end(), once the
        // number of frames is known.
        if (!zip.begin(names[k], header)) {
            out.failed = true;
            break;
        }
        const bool done = renderCompensated(
            r, max, cancel,
            [&](std::span<const float> l, std::span<const float> rr) {
                buf.clear();
                ymio::appendWavFrames(buf, settings.format, l, rr);
                return zip.write(buf);
            },
            part, frames);
        if (!done) return std::nullopt;
        out.truncated = out.truncated || part.truncated;
        if (part.failed || !zip.end(ymio::wavHeader(settings.format, rate, frames))) {
            out.failed = true;
            break;
        }
        out.tracks.push_back((*tracks)[k]);
    }
    if (!out.failed && !zip.finish()) out.failed = true;
    out.failed = out.failed || !sink.ok();
    out.files = out.failed ? 0 : int(out.tracks.size());
    out.seconds = double(frames) / rate;
    return out;
}

}  // namespace stym::plugin
