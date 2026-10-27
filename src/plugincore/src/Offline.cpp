// STYM2149 — plugincore: offline render of the exports (STYM2149_SPEC.md §8)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/Offline.h"

#include <algorithm>
#include <array>

#include "plugincore/Driver.h"
#include "plugincore/Mapping.h"

namespace stym::plugin {

using ymengine::Engine;

namespace {
// Log of a chunk: a few hundred entries at most (arpeggios at 200 Hz on 16 tracks and their chords); the capacity is
// reserved once, the engine does not write beyond it.
constexpr size_t kLogCapacity = size_t(1) << 16;
}  // namespace

OfflineRender::OfflineRender(Source source, uint32_t sampleRate, const ParamValues& values,
                             const NonParamState& extras, uint16_t trackOutputs)
    : driver_(std::make_unique<Driver>()),
      snapshot_(std::make_unique<Snapshot>()),
      values_(values),
      rate_(std::max<uint32_t>(8000, sampleRate)) {
    // The project copied, with the forced settings of the file comment of Offline.h.
    Snapshot& s = *snapshot_;
    s.state = extras;
    ymengine::GlobalParams& g = s.state.global;
    g.seqClock = ymengine::SeqClock::Internal;
    g.loop = false;
    g.songMode = source == Source::Song;
    g.metronome = false;
    fillSequence(s.state.seq, s.seq);
    s.patternSerial = 1;  // the engine starts from the current pattern (state.seq.current)
    midiClock_ = toGlobal(values_, g).arpClock == ymengine::ArpClock::MidiClock;
    driver_->fixSeqClock(ymengine::SeqClock::Internal);  // pulses below for the arpeggio only
    driver_->prepare({rate_, trackOutputs}, values_, s);
    log_.reserve(kLogCapacity);
    driver_->engine().setSeqLog(&log_);
}

OfflineRender::~OfflineRender() = default;

const NonParamState& OfflineRender::extras() const noexcept { return snapshot_->state; }

uint32_t OfflineRender::latencySamples() const noexcept { return driver_->latencySamples(); }

bool OfflineRender::startCapture(std::span<ymengine::RegisterFrame> frames, std::span<ymengine::DmaFrame> dma) noexcept {
    if (started_) return false;
    driver_->engine().startCapture(0, frames, dma);
    return driver_->engine().capturing();
}

uint32_t OfflineRender::capturedFrames() const noexcept { return driver_->engine().capturedFrames(); }
uint32_t OfflineRender::captureHz() const noexcept { return driver_->engine().captureHz(); }
ymcore::MasterClock OfflineRender::chipClock() const noexcept { return driver_->engine().chip(0).config().chip.clock; }

void OfflineRender::process(const ymengine::Outputs& out, uint32_t n) {
    log_.clear();
    std::array<MidiEvent, 1 + kChunk> midi{};
    size_t count = 0;
    // The first chunk starts the sequencer, at its first sample.
    if (!started_) {
        midi[count].command = UiCommand::SeqPlay;
        ++count;
        started_ = true;
    }
    if (midiClock_) {  // MIDI clock pulses at the tempo played
        const double bpm = std::max(1.0, driver_->engine().seqStatus().bpm > 0 ? driver_->engine().seqStatus().bpm
                                                                               : double(value(values_, Global::FreeTempo)));
        // 24 pulses per quarter note: a pulse every 60 × rate / (bpm × 24) samples, carried over between chunks.
        const double period = 60.0 * rate_ / (bpm * 24);
        while (nextPulse_ < double(position_ + n) && count < midi.size()) {
            MidiEvent& e = midi[count++];
            e.offset = uint32_t(std::max(0.0, nextPulse_ - double(position_)));
            e.size = 1;
            e.data = {0xF8, 0, 0};
            nextPulse_ += period;
        }
    }
    driver_->process(values_, std::span<const MidiEvent>(midi.data(), count), out, n, true, nullptr);
    // The first stop of the log, kept within the chunk.
    if (stop_ < 0)
        for (const Engine::SeqLogRecord& r : log_)
            if (r.op == '!') {  // the sequencer has stopped
                stop_ = int64_t(std::clamp<uint64_t>(r.sample, position_, position_ + n));
                break;
            }
}

uint32_t OfflineRender::render(const ymengine::Outputs& out, uint32_t n) {
    if (finished_) return 0;
    n = std::min(n, kChunk);
    if (n == 0) return 0;
    process(out, n);
    uint32_t valid = n;
    if (stop_ >= 0) {  // clean cut at the stop sample
        finished_ = true;
        valid = uint32_t(uint64_t(stop_) - position_);
    }
    position_ += valid;
    return valid;
}

uint32_t OfflineRender::renderRaw(const ymengine::Outputs& out, uint32_t n) {
    n = std::min(n, kChunk);
    if (n == 0) return 0;
    process(out, n);
    position_ += n;
    return n;
}

uint32_t OfflineRender::advance(uint32_t n) {
    left_.resize(kChunk);
    right_.resize(kChunk);
    ymengine::Outputs out;
    out.left = left_.data();
    out.right = right_.data();
    return render(out, n);
}

}  // namespace stym::plugin
