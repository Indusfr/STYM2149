// STYM2149 — plugincore: engine status for the interface (voice LEDs, note, followed envelope, oscilloscope)
// (named "telemetry" until M8: these measurements go from the audio thread to the editor and stay on the machine, §10)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Takes over the messages from the audio processor of the prototype to its page:
// - "st" (20 times per second): one LED per track that has an audible voice (active track, non-zero level, neither
//   muted nor excluded by a solo); envelope of the most recent voice of the focused track that is not fading (stage
//   and level; without ADSR: sustain at full);
// - "tp": period of the last attacked note ("Note" readout and register monitor);
// - the Web Audio analyser (2048 samples): here the last samples of the main output, (left + right) / 2, like the
//   mono mix of the analyser;
// - the sequencer ("st" and "pa" messages: step played, pattern switched; M5): playback, MIDI clock received,
//   pattern played and pending, step, Song entry, tempo; step played in the arpeggio table ("st": astep);
// - the meters and oscilloscopes of the Mixer ("st": pk, opk, scope; M5): peak of each track (Engine::takeMeters)
//   and of the left and right outputs since the previous publication, last 128 points of the oscilloscope of each
//   track; the DMA slots played (M6, D-153); the instrument chosen by Program Change for each track (M6, D-156).
// The audio thread captures after each block and publishes at 30 Hz (the editor's rate) through a triple buffer.
#pragma once
#include <array>
#include <cstdint>

#include "plugincore/Parameters.h"
#include "plugincore/TripleBuffer.h"
#include "ymengine/Engine.h"

namespace stym::plugin {

inline constexpr int kScopeSamples = 2048;
inline constexpr int kTrackScopePoints = ymengine::Engine::kScopePoints;

// One publication, about 30 per second: written by the audio thread into the triple buffer, read by the editor.
struct EngineStatus {
    uint64_t serial = 0;                 // publication number
    std::array<bool, kTracks> active{};  // voice LEDs
    uint64_t noteSerial = 0;             // changes at each new attacked note
    uint16_t notePeriod = 0;             // period of that note (TP)
    int8_t envStage = -1;                // -1: no voice followed; 1 attack, 2 decay, 3 sustain, 4 release
    float envLevel = 0;                  // ADSR level of that voice at the end of the block (1: full)
    std::array<float, kScopeSamples> scope{};  // from oldest to most recent
    ymengine::Engine::SeqStatus seq;     // sequencer
    int8_t arpStep = -1;                 // step of the arpeggio table played (Arpeggiator window: astep), -1 otherwise
    std::array<float, kTracks> peak{};   // peak of each track (1 = a single channel at the highest level)
    std::array<float, 2> outPeak{};      // peak of the left and right outputs (after the main volume)
    std::array<std::array<float, kTrackScopePoints>, kTracks> trackScope{};  // from oldest to most recent
    // STE DMA sound (M6, D-153): bank slots played by a voice or by the preview (bit k: slot k).
    uint8_t dmaSlots = 0;
    std::array<uint8_t, kTracks> program{};  // instrument chosen by Program Change (0: the track's sound, D-156)
};

// Owned by the processor: capture() on the audio thread, update() and latest() on the editor's thread.
class EngineStatusTap {
public:
    // Audio thread stopped: one publication every sampleRate / 30 samples; scope and peaks cleared.
    void prepare(uint32_t sampleRate) noexcept;

    // Audio thread, after rendering a block: `left` and `right` are the main output. Reads the engine meters (and
    // resets its peaks) at each publication.
    void capture(ymengine::Engine& engine, const ParamValues& values, const float* left, const float* right,
                 uint32_t n) noexcept;

    // Reading thread: true if a new engine status has arrived (read in latest()).
    bool update() noexcept { return buffer_.update(); }
    const EngineStatus& latest() const noexcept { return buffer_.front(); }

private:
    void publish(ymengine::Engine& engine, const ParamValues& values) noexcept;

    TripleBuffer<EngineStatus> buffer_;
    std::array<float, kScopeSamples> ring_{};  // last samples of the mono mix; ringPos_ is the oldest
    uint32_t ringPos_ = 0;
    uint32_t period_ = 1600, elapsed_ = 0;     // samples between publications, samples since the last one
    uint64_t serial_ = 0, noteSerial_ = 0, lastAge_ = 0;
    uint16_t notePeriod_ = 0;
    std::array<float, 2> outPeak_{};
    ymengine::Engine::TrackMeters meters_;
};

}  // namespace stym::plugin
