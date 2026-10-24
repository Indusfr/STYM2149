// STYM2149 — plugincore: offline render of the exports (M7, STYM2149_SPEC.md §8, D-160, D-163, D-170)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A second instance of the driver (Driver), never the audio thread's, plays the project copied at the time of the
// export: the current pattern once, or the Song from its first entry to its end (D-163: tempo of each block, swing,
// groove, Ticks mode, Loop jumps unrolled, stop at the last entry). Settings forced for this: internal clock (Free
// run, at the free tempo of the project), loop off, Song mode according to the source; the driver mutes the
// metronome outside real time (D-112). With the arpeggio on the MIDI clock, pulses are produced at the tempo played
// (24 per quarter note), for lack of an external clock.
// The render stops dead at the sample where the sequencer stops (D-164); the audio exports add the resampling delay
// to it (renderRaw), so that the file starts and ends with the music. The sequencer log (Engine::SeqLogRecord) of
// each rendered chunk is kept for the exports that read it (MIDI).
// To be used from a single thread, the export job's (allocations at construction, and in the first advance()).
#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymengine/Engine.h"

namespace stym::plugin {

class Driver;
struct Snapshot;

// Maximum length rendered by an export: one hour (a longer Song is cut, and the export reports it).
inline constexpr double kOfflineMaxSeconds = 3600;

// One export render: owns its driver and the snapshot it plays (see above).
class OfflineRender {
public:
    enum class Source : uint8_t { Pattern, Song };  // the current pattern once, or the whole Song
    static constexpr uint32_t kChunk = 1024;  // maximum samples rendered per call

    // `trackOutputs`: separate track outputs to prepare (bit n = track n), as in Driver::Setup; a rate below 8000 Hz
    // is raised to 8000 Hz.
    OfflineRender(Source source, uint32_t sampleRate, const ParamValues& values, const NonParamState& extras,
                  uint16_t trackOutputs = 0);
    ~OfflineRender();
    OfflineRender(const OfflineRender&) = delete;
    OfflineRender& operator=(const OfflineRender&) = delete;

    // Renders at most min(n, kChunk) samples into `out`; returns the number of valid samples (0: finished). The
    // chunk where the sequencer stops is cut at that sample.
    uint32_t render(const ymengine::Outputs& out, uint32_t n);
    // Like render(), without keeping the sound (exports that only read the log).
    uint32_t advance(uint32_t n);
    // Raw render, to compensate the resampling delay (audio exports): min(n, kChunk) samples, without a cut; the
    // sequencer stop is only noted (stopSample). A compensated output is then the interval
    // [latencySamples(), stopSample() + latencySamples()) of the raw render. Do not mix with render() in one render.
    uint32_t renderRaw(const ymengine::Outputs& out, uint32_t n);
    // Sample (since the start) where the sequencer stopped, -1 while it plays.
    int64_t stopSample() const noexcept { return stop_; }
    bool finished() const noexcept { return finished_; }
    uint64_t position() const noexcept { return position_; }  // samples rendered since the start
    uint32_t sampleRate() const noexcept { return rate_; }
    uint32_t latencySamples() const noexcept;
    // Capture of the registers of the chip in 3-channel mode (register exports, D-160): one frame per replay tick
    // from the start (Engine::startCapture), written into `frames` which the caller keeps alive, with the DMA sound
    // of the STE machine in `dma` if it is not empty. To be called before the first render; false in 16 channels.
    bool startCapture(std::span<ymengine::RegisterFrame> frames, std::span<ymengine::DmaFrame> dma = {}) noexcept;
    uint32_t capturedFrames() const noexcept;
    uint32_t captureHz() const noexcept;
    ymcore::MasterClock chipClock() const noexcept;
    // Sequencer log of the last chunk (samples counted since the start).
    std::span<const ymengine::Engine::SeqLogRecord> log() const noexcept { return log_; }
    // Settings played (after the forced settings).
    const ParamValues& values() const noexcept { return values_; }
    const NonParamState& extras() const noexcept;

private:
    void process(const ymengine::Outputs& out, uint32_t n);

    std::unique_ptr<Driver> driver_;
    std::unique_ptr<Snapshot> snapshot_;
    ParamValues values_{};
    uint32_t rate_ = 48000;
    uint64_t position_ = 0;
    // started_: Play sent; midiClock_: the arpeggio follows the MIDI clock, so pulses are produced here.
    bool started_ = false, finished_ = false, midiClock_ = false;
    int64_t stop_ = -1;
    double nextPulse_ = 0;  // next MIDI clock pulse (sample)
    std::vector<ymengine::Engine::SeqLogRecord> log_;
    std::vector<float> left_, right_;  // advance(): sound rendered and dropped
};

}  // namespace stym::plugin
