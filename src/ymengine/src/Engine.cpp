// STYM2149 — ymengine: voice engine
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// API of the engine; the state and the render are in EngineImpl.h, the playback of the tracks in Sequencer.cpp.
#include <limits>

#include "EngineImpl.h"

namespace stym::ymengine {

// ======================================================================================================
// The Digidrum slots are allocated here at their largest size, so that setUserSample never allocates.
Engine::Engine() : impl_(std::make_unique<Impl>()) {
    for (auto& u : impl_->user) u.assign(kMaxUserSample, 0);
    impl_->tracks[0].mix.enabled = true;  // the prototype activates track 1
    impl_->panStep.fill(std::numeric_limits<double>::quiet_NaN());
    impl_->refreshLive();
}

Engine::~Engine() = default;

void Engine::prepare(const EngineConfig& config) {
    Impl& m = *impl_;
    m.config = config;
    const ymcore::RenderConfig& rc = config.render;
    // Level of one channel alone (measured table, channels B and C at 0), normalised to index 31.
    const double top = ymcore::tables::kMixTable[31];
    for (int i = 0; i < 32; ++i) m.tab1[size_t(i)] = ymcore::tables::kMixTable[i] / top;
    // A chip renders kMixTable / 32768 (ChipRenderer::render): gain3 brings it to the unit of tab1.
    m.gain3 = 32768.0 / top;
    // 16-channel mode: buses and outputs.
    m.outL.prepare(rc);
    m.outR.prepare(rc);
    // Internal steps of one render slice (kChunk samples), with a margin for the steps that the output chain asks
    // ahead (TickStreamOutput::ticksNeeded); the bus buffers hold twice as much, unread steps kept at their head.
    const double step = rc.chip.clock.hz() / 8 / rc.hostRate;
    const size_t cap = size_t(std::ceil(step * kChunk)) + 4 * size_t(std::ceil(step)) + 64;
    m.dL.assign(cap, 0);
    m.dR.assign(cap, 0);
    m.sL.assign(cap, 0);
    m.sR.assign(cap, 0);
    m.bufL.assign(2 * cap, 0);
    m.bufR.assign(2 * cap, 0);
    // Delay: send buses with the same chain, their output, the lines.
    m.outSL.prepare(rc);
    m.outSR.prepare(rc);
    m.dSL.assign(cap, 0);
    m.dSR.assign(cap, 0);
    m.sSL.assign(cap, 0);
    m.sSR.assign(cap, 0);
    m.bufSL.assign(2 * cap, 0);
    m.bufSR.assign(2 * cap, 0);
    m.sendL.assign(kChunk, 0);
    m.sendR.assign(kChunk, 0);
    m.delay.prepare(rc.hostRate);
    // Separate outputs: mono bus (16-channel) and chip copies (3-channel) of the requested tracks only.
    m.trackMask = config.trackOutputs;
    for (int t = 0; t < kTracks; ++t) {
        const bool on = m.trackOut(t);
        m.dT[size_t(t)].assign(on ? cap : 0, 0);
        m.sT[size_t(t)].assign(on ? cap : 0, 0);
        m.bufT[size_t(t)].assign(on ? 2 * cap : 0, 0);
        if (on) m.outT[size_t(t)].prepare(rc);
        if (on && t < Impl::kShadows) m.shadow[size_t(t)].prepare(rc);
    }
    m.scratch.assign(m.trackMask ? kChunk : 0, 0);
    // DMA voices in 3-channel mode: output of the buses before they are added to the chips.
    m.pcmHostL.assign(kChunk, 0);
    m.pcmHostR.assign(kChunk, 0);
    // 3-channel modes.
    for (auto& c : m.chips) c.prepare(rc);
    m.chipBuf.assign(kChunk, 0);
    m.chipLevels.assign(kChunk, 0);
    // Mixer meters: buckets of the steps of a render and of those computed ahead (16-channel), points of a render
    // (3-channel).
    for (auto& d : m.meterD) d.assign(size_t(std::ceil(2.0 * double(cap) / m.meterTickStep())) + 3, 0);
    for (auto& d : m.meterDD) d.assign(m.meterD[0].size(), 0);
    m.meterTickInv = 1.0 / m.meterTickStep();
    m.bucketStart.assign(m.meterD[0].size() + 1, 0);
    m.meterAt.assign(size_t(std::ceil(kChunk / m.meterSampleStep())) + 2, 0);
    m.mfpMap = MfpClockMap(rc.chip.clock);
    m.kit.build(m.kitParams, std::max<uint32_t>(1, m.global.vblHz), rc.chip.clock);
    m.prepared = true;
    reset();
}

void Engine::reset() noexcept {
    Impl& m = *impl_;
    m.killAll();
    for (int i = 0; i < kVoices; ++i) m.vch[size_t(i)].reset(m.config.render.chip.noise, true);
    m.samples = 0;
    m.qFrom = 0;
    m.restartTimeline();
    m.eventCount = 0;
    m.heldCount = 0;
    m.arp = ArpState{};
    m.arpRng = ProtoXorshift{};
    // Draws of the plugin from the seed of the configuration (a zero state would stay zero); the jitter has its own
    // sequence, from the same seed.
    m.rng.s = m.config.seed ? m.config.seed * 0x9E3779B97F4A7C15ull : 0x9E3779B97F4A7C15ull;
    m.jitter.configure(cpuCyclesToYmMaster(m.global.irqJitterCpuCycles), m.config.seed + 0x51u);
    m.stamp = 0;
    m.bend.fill(0);
    m.wheel.fill(0);
    m.press.fill(0);
    m.mpe.fill({});
    m.program.fill(0);
    m.arpWasEnabled = m.arpEnabled();
    m.resetSeq();
    m.refreshLive();
}

void Engine::setGlobal(const GlobalParams& g) noexcept {
    Impl& m = *impl_;
    // The machine changes the tracks played (DMA tracks in 3-channel mode): like a change of mode.
    const bool modeChanged = g.mode != m.global.mode || g.ste != m.global.ste;
    const bool vblChanged = g.vblHz != m.global.vblHz;
    const bool jitterChanged = g.irqJitterCpuCycles != m.global.irqJitterCpuCycles;
    // Sequencer clock changed during playback: stop at the next block (the new clock starts again).
    if (g.seqClock != m.global.seqClock) m.clockChanged = true;
    if (g.speed != m.global.speed) m.seq.speed = std::clamp<int>(g.speed, 1, kSpeedMax);  // Ticks: replaces Sxx
    if (g.tempo != m.global.tempo) m.seq.cmdBpm = 0;  // BPM: replaces Txx
    const BendTarget bentWas = m.global.bendTarget;
    m.global = g;
    m.global.vblHz = std::clamp<uint32_t>(g.vblHz, 1, 1000);
    if (modeChanged && m.prepared) {
        // The voices do not survive a change of mode (the voice → channel mapping changes).
        m.killAll();
        m.restartTimeline();
    }
    if (vblChanged) {
        if (m.prepared) m.kit.build(m.kitParams, m.global.vblHz, m.config.render.chip.clock);
        for (Voice& v : m.voices)
            if (v.on && v.vblOn) {  // new cadence from the next frame on
                v.vblStart = v.nextVbl;
                v.vblIndex = 0;
                v.vblHz = m.global.vblHz;
            }
    }
    if (jitterChanged) m.jitter.configure(cpuCyclesToYmMaster(g.irqJitterCpuCycles), m.config.seed + 0x51u);
    if (m.prepared) {
        // MIDI expression: the sounding notes follow a new target of the bend (their pitch let go of or taken
        // up again) and start their frames for a new target of the wheel or the pressure.
        const uint64_t c = m.eventCycle();
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = m.voices[size_t(i)];
            if (!v.on) continue;
            if (bentWas != g.bendTarget && v.track >= 0) {
                const double b = m.bend[size_t(v.track)];
                const double d = (g.bendTarget == BendTarget::Pitch ? b : 0) - (bentWas == BendTarget::Pitch ? b : 0);
                if (d != 0) {
                    m.bendVoice(v, d, c);
                    m.touch(i, c);
                }
            }
            if (m.expressive(v)) m.startFrames(v, c);
        }
    }
    m.refreshLive();
}

const GlobalParams& Engine::global() const noexcept { return impl_->global; }

void Engine::setTrack(int track, const TrackParams& t) noexcept {
    if (track < 0 || track >= kTracks) return;
    Impl& m = *impl_;
    const bool off = m.tracks[size_t(track)].mix.enabled && !t.mix.enabled;
    m.tracks[size_t(track)] = t;
    m.refreshLive();
    if (off && m.prepared) m.laneOff(track, m.eventCycle());  // track switched off: its sequencer notes stop
}

const TrackParams& Engine::track(int track) const noexcept {
    return impl_->tracks[size_t(std::clamp(track, 0, kTracks - 1))];
}

void Engine::setTrackMix(uint32_t offset, int track, const TrackMix& mix) noexcept {
    if (track < 0 || track >= kTracks) return;
    if (Event* e = impl_->push(offset, EventType::Mix, track, 0, 0)) e->mix = mix;
}

void Engine::setKit(const KitParams& kit) noexcept {
    Impl& m = *impl_;
    m.kitParams = kit;
    m.kit.build(kit, std::max<uint32_t>(1, m.global.vblHz), m.config.render.chip.clock);
}

const KitParams& Engine::kit() const noexcept { return impl_->kitParams; }

void Engine::setUserSample(int slot, std::span<const uint8_t> nibbles) noexcept {
    if (slot < 0 || slot >= kDigidrumSlots) return;
    Impl& m = *impl_;
    const size_t n = std::min<size_t>(nibbles.size(), kMaxUserSample);
    for (size_t i = 0; i < n; ++i) m.user[size_t(slot)][i] = uint8_t(std::min<int>(nibbles[i], 15));
    m.userLen[size_t(slot)] = uint32_t(n);
}

void Engine::setDigidrumSlot(int slot, const DigidrumSlot& s) noexcept {
    if (slot < 0 || slot >= kDigidrumSlots) return;
    DigidrumSlot d = s;
    d.rateHz = std::isfinite(s.rateHz) ? std::clamp(s.rateHz, kDigidrumRateMin, kDigidrumRateMax) : kDigidrumRate;
    d.root = uint8_t(std::min<int>(s.root, 127));
    d.cents = std::isfinite(s.cents) ? std::clamp(s.cents, -kDigidrumCentsMax, kDigidrumCentsMax) : 0.0;
    d.end = std::min<uint32_t>(s.end, kMaxUserSample);
    d.start = std::min(s.start, d.end > 0 ? d.end : kMaxUserSample);  // End 0: to the end of the sample
    d.rest = uint8_t(std::min<int>(s.rest, 15));
    impl_->digiSlots[size_t(slot)] = d;
}

const DigidrumSlot& Engine::digidrumSlot(int slot) const noexcept {
    return impl_->digiSlots[size_t(std::clamp(slot, 0, kDigidrumSlots - 1))];
}

void Engine::setDma(const DmaParams& p) noexcept {
    Impl& m = *impl_;
    bool moved = false;  // a slot goes from mono to stereo or back: the right side's mixer comes or goes
    for (int k = 0; k < kDmaSlots; ++k) moved |= m.dmaBank.slot(k).stereo != p.slots[size_t(k)].stereo;
    m.dmaBank.setParams(p);
    if (!moved) return;
    m.refreshLive();
    if (m.prepared) m.remix(m.eventCycle());
}
const DmaParams& Engine::dma() const noexcept { return impl_->dmaBank.params(); }
void Engine::setDmaSample(int slot, std::span<const int8_t> bytes) noexcept {
    Impl& m = *impl_;
    if (slot < 0 || slot >= kDmaSlots) return;
    m.dmaBank.setSample(slot, bytes);
    // The voices that play this slot stop (their bytes have changed): their contribution leaves the buses.
    if (!m.prepared) return;
    const uint64_t c = m.eventCycle();
    for (int i = 0; i < kVoices; ++i) {
        Voice& v = m.voices[size_t(i)];
        if (!v.on || v.gen != Generator::Dma || v.slot != slot) continue;
        v.on = false;
        v.pcmL = v.pcmR = 0;
        m.touch(i, c);
    }
}

void Engine::noteOn(uint32_t offset, int note, int velocity, int track, int channel) noexcept {
    if (note < 0 || note > 127 || track >= kTracks) return;
    if (velocity <= 0) {
        noteOff(offset, note, track, channel);
        return;
    }
    const double ch = std::clamp(channel, 0, Impl::kMpeChannels - 1);  // MPE member channel in value
    if (Event* e = impl_->push(offset, EventType::NoteOn, note, std::min(velocity, 127), ch))
        e->track = int16_t(std::max(track, -1));
}
void Engine::noteOff(uint32_t offset, int note, int track, int channel) noexcept {
    if (note < 0 || note > 127 || track >= kTracks) return;
    const double ch = std::clamp(channel, 0, Impl::kMpeChannels - 1);
    if (Event* e = impl_->push(offset, EventType::NoteOff, note, 0, ch)) e->track = int16_t(std::max(track, -1));
}
void Engine::pitchBend(uint32_t offset, double semitones, int track) noexcept {
    if (track >= kTracks) return;
    if (Event* e = impl_->push(offset, EventType::Bend, 0, 0, semitones)) e->track = int16_t(std::max(track, -1));
}
void Engine::modWheel(uint32_t offset, double amount, int track) noexcept {
    if (track >= kTracks) return;
    if (Event* e = impl_->push(offset, EventType::Wheel, 0, 0, std::clamp(amount, 0.0, 1.0)))
        e->track = int16_t(std::max(track, -1));
}
void Engine::channelPressure(uint32_t offset, double amount, int track) noexcept {
    if (track >= kTracks) return;
    if (Event* e = impl_->push(offset, EventType::Pressure, 0, 0, std::clamp(amount, 0.0, 1.0)))
        e->track = int16_t(std::max(track, -1));
}
void Engine::notePressure(uint32_t offset, int note, double amount, int track) noexcept {
    if (note < 0 || note > 127 || track >= kTracks) return;
    if (Event* e = impl_->push(offset, EventType::NotePressure, note, 0, std::clamp(amount, 0.0, 1.0)))
        e->track = int16_t(std::max(track, -1));
}
void Engine::mpeBend(uint32_t offset, int channel, double semitones) noexcept {
    if (channel < 1 || channel >= Impl::kMpeChannels) return;
    impl_->push(offset, EventType::MpeBend, channel, 0, std::clamp(semitones, -96.0, 96.0));
}
void Engine::mpePressure(uint32_t offset, int channel, double amount) noexcept {
    if (channel < 1 || channel >= Impl::kMpeChannels) return;
    impl_->push(offset, EventType::MpePressure, channel, 0, std::clamp(amount, 0.0, 1.0));
}
void Engine::mpeSlide(uint32_t offset, int channel, double amount) noexcept {
    if (channel < 1 || channel >= Impl::kMpeChannels) return;
    impl_->push(offset, EventType::MpeSlide, channel, 0, std::clamp(amount, 0.0, 1.0));
}
void Engine::programChange(uint32_t offset, int program, int track) noexcept {
    if (program < 0 || program > kInstruments || track >= kTracks) return;
    if (Event* e = impl_->push(offset, EventType::Program, program, 0, 0)) e->track = int16_t(std::max(track, -1));
}
void Engine::allNotesOff(uint32_t offset) noexcept { impl_->push(offset, EventType::AllOff, 0, 0, 0); }
void Engine::releaseManual(uint32_t offset) noexcept { impl_->push(offset, EventType::ManualOff, 0, 0, 0); }
void Engine::releaseForPlayer(uint32_t offset) noexcept { impl_->push(offset, EventType::PlayerStart, 0, 0, 0); }
void Engine::midiClock(uint32_t offset) noexcept { impl_->push(offset, EventType::MidiClock, 0, 0, 0); }
void Engine::midiStart(uint32_t offset) noexcept { impl_->push(offset, EventType::MidiStart, 0, 0, 0); }
void Engine::midiStop(uint32_t offset) noexcept { impl_->push(offset, EventType::MidiStop, 0, 0, 0); }
void Engine::midiContinue(uint32_t offset) noexcept { impl_->push(offset, EventType::MidiContinue, 0, 0, 0); }
void Engine::midiSongPosition(uint32_t offset, int sixteenths) noexcept {
    impl_->push(offset, EventType::MidiSongPosition, std::clamp(sixteenths, 0, 16383), 0, 0);
}

void Engine::setInstruments(const InstrumentBank* bank) noexcept { impl_->instruments = bank; }

void Engine::setSequence(const SeqData* data) noexcept {
    Impl& m = *impl_;
    m.seqData = data;
    m.songFirst = -1;
    if (data)
        for (size_t i = 0; i < data->song.size(); ++i)
            if (data->song[i].kind != SeqSongKind::Loop) {
                m.songFirst = int(i);
                break;
            }
    // Host sync: the position comes from the host; patterns or Song changed, the sequencer is put back on the step
    // played (beyond the end of a playback without loop: stopped until the host comes back).
    if (m.global.seqClock == SeqClock::Host && m.seq.on) {
        if (!m.locateTo(m.seq.stepAbs)) {
            const int64_t at = m.seq.stepAbs;
            m.seqHalt(m.eventCycle());
            m.seq.parkedAt = std::max<int64_t>(0, at);
        }
        return;
    }
    // Length of the pattern played changed: the current step is kept (seq message of the prototype).
    const int len = m.patternSteps(m.seq.pattern);
    if (len == m.seq.len) return;
    if (m.seq.on && m.seq.stepAbs >= 0) {
        const int64_t st = m.seq.stepAbs % m.seq.len, cyc = m.seq.stepAbs / m.seq.len;
        m.seq.stepAbs = cyc * len + std::min<int64_t>(st, len - 1);
        m.seq.step = int(m.seq.stepAbs % len);
    } else if (!m.seq.on) {
        m.seq.step = len - 1;
    }
    m.seq.len = len;
}
void Engine::seqPlay(uint32_t offset) noexcept { impl_->push(offset, EventType::SeqPlay, 0, 0, 0); }
void Engine::seqStop(uint32_t offset) noexcept { impl_->push(offset, EventType::SeqStop, 0, 0, 0); }
void Engine::seqSelectPattern(uint32_t offset, int pattern) noexcept {
    impl_->push(offset, EventType::SeqQueue, std::clamp(pattern, 0, kSeqPatterns - 1), 0, 0);
}
void Engine::seqSongJump(uint32_t offset, int entry) noexcept {
    impl_->push(offset, EventType::SeqSongJump, std::max(entry, 0), 0, 0);
}
void Engine::seqPreview(uint32_t offset, int track, int note, double accent) noexcept {
    if (track < 0 || track >= kTracks || note < 0 || note > 127) return;
    if (Event* e = impl_->push(offset, EventType::SeqPreview, note, 0, std::clamp(accent, 0.0, 1.0))) e->track = int16_t(track);
}

void Engine::drumPreview(uint32_t offset, int slot) noexcept {
    if (slot < 0 || slot >= kDrumSlots) return;
    impl_->push(offset, EventType::DrumPreview, slot, 0, 0);
}

void Engine::digidrumPreview(uint32_t offset, int slot) noexcept {
    if (slot < 0 || slot >= kDigidrumSlots) return;
    impl_->push(offset, EventType::DigiPreview, slot, 0, 0);
}
void Engine::digidrumStopPreview(uint32_t offset) noexcept { impl_->push(offset, EventType::DigiStop, 0, 0, 0); }

void Engine::dmaPreview(uint32_t offset, int slot) noexcept {
    if (slot >= 0 && slot < kDmaSlots) impl_->push(offset, EventType::DmaPreview, slot, 0, 0);
}
void Engine::dmaStopPreview(uint32_t offset) noexcept { impl_->push(offset, EventType::DmaStop, 0, 0, 0); }

int Engine::arpStep() const noexcept {
    const Impl& m = *impl_;
    return m.arpEnabled() && m.arpSettings().mode == ArpMode::Table && m.heldCount > 0 ? m.arp.cur : -1;
}

Engine::SeqStatus Engine::seqStatus() const noexcept {
    const Impl& m = *impl_;
    SeqStatus st;
    st.playing = m.seq.on;
    st.clockRunning = m.seq.run;
    // Rest or NoteOff entry of the Song: no pattern.
    st.pattern = m.seq.pattern < kSeqPatterns ? m.seq.pattern : -1;
    st.queued = m.seq.queued < kSeqPatterns ? m.seq.queued : -1;
    st.step = m.seq.on ? m.seq.step : -1;
    st.stepAbs = m.seq.stepAbs;
    st.songPos = m.seq.songPos;
    st.bpm = m.seqBpm();
    st.externalBpm = m.seq.spt > 0 ? 60.0 * m.hostRate() / (m.seq.spt * 24) : 0;  // spt: samples per MIDI clock pulse
    return st;
}

void Engine::setSeqLog(std::vector<SeqLogRecord>* log) noexcept { impl_->seqLog = log; }

void Engine::takeMeters(TrackMeters& out) noexcept {
    Impl& m = *impl_;
    out.peak = m.meterPeak;
    out.peakR = m.meterPeakR;
    out.scope = m.meterScope;
    out.side = m.meterScopeD;
    out.scopePos = m.meterPos;
    m.meterPeak.fill(0);
    m.meterPeakR.fill(0);
}

void Engine::setHostTransport(const HostTransport& t) noexcept { impl_->host = t; }

void Engine::render(float* left, float* right, uint32_t n) noexcept {
    Outputs out;
    out.left = left;
    out.right = right;
    render(out, n);
}

void Engine::render(const Outputs& out, uint32_t n) noexcept {
    Impl& m = *impl_;
    if (!m.prepared) {
        std::fill(out.left, out.left + n, 0.0f);
        std::fill(out.right, out.right + n, 0.0f);
        for (float* t : out.track)
            if (t) std::fill(t, t + n, 0.0f);
        for (float* c : out.chip)
            if (c) std::fill(c, c + n, 0.0f);
        m.eventCount = 0;
        return;
    }
    // Events sorted by position, order of arrival kept (insertion sort: they arrive almost sorted).
    for (uint32_t i = 1; i < m.eventCount; ++i) {
        const Event e = m.events[i];
        uint32_t j = i;
        while (j > 0 && (m.events[j - 1].offset > e.offset ||
                         (m.events[j - 1].offset == e.offset && m.events[j - 1].order > e.order))) {
            m.events[j] = m.events[j - 1];
            --j;
        }
        m.events[j] = e;
    }
    // The arpeggio has just been switched off (setting of the focused track): its notes are released.
    const bool arpOn = m.arpEnabled();
    uint32_t ev = 0;
    m.rOut = &out;
    m.rSeg = m.rI = 0;
    m.rStart = m.samples;
    if (m.arpWasEnabled && !arpOn) {
        m.killTag(VoiceTag::Arp, false, m.eventCycle());
        m.arp.cur = -1;
    }
    m.arpWasEnabled = arpOn;
    m.updateSendLive();  // delay switched on or off, change of mode
    if (m.clockChanged) {  // sequencer clock changed: playback stops, the new clock starts again
        m.clockChanged = false;
        m.seqHalt(m.at());
        m.seq.parkedAt = -1;
        m.hostPulse = Impl::kNoPulse;
        m.halfPulseIn = 0;
    }
    const TrackArp& arpSet = m.arpSettings();
    const double arpInc = std::clamp(m.arpRate(arpSet.rateHz), 0.0, 1000.0) / m.hostRate();
    // Quantize to VBL: events, arpeggio and sequencer taken into account at the first sample of each frame
    // only (g·vbl mod fs < vbl), with the time elapsed since the previous boundary.
    const bool quant = m.global.quantizeVbl;
    const uint64_t vbl = std::max<uint32_t>(1, m.global.vblHz), sr = std::max<uint32_t>(1, m.hostRate());
    // One pass per sample: events due, keyboard arpeggio, sequencer, echoes; the voices are rendered in slices of
    // kChunk samples at most (flush), or earlier when the sequencer needs them up to the current sample (at()).
    for (uint32_t i = 0; i < n; ++i) {
        m.rI = i;
        const uint64_t g = m.rStart + i;
        if (quant && (g * vbl) % sr >= vbl) {
            if (i + 1 - m.rSeg >= kChunk) {
                m.flush(out, m.rSeg, i + 1);
                m.rSeg = i + 1;
            }
            continue;
        }
        // Samples since the last sample taken into account (1 without Quantize to VBL), one second at most.
        const uint32_t dt = uint32_t(std::clamp<uint64_t>(g + 1 - std::min(m.qFrom, g), 1, sr));
        m.qFrom = g + 1;
        if (ev < m.eventCount && m.events[ev].offset <= i) {
            m.lanesCatchUp();
            const uint64_t c = m.at();
            while (ev < m.eventCount && m.events[ev].offset <= i) m.applyEvent(m.events[ev++], c);
        }
        // Arpeggio at internal cadence (Rate, or the tempo outside the host's playback): the phase advances at
        // each sample while a note is held or while the last note of the arpeggio is being released (arpTick of the
        // prototype).
        if (m.arpEnabled() && !m.arpOnPulses() && (m.heldCount > 0 || m.arpReleasing())) {
            const double next = m.arp.phase + arpInc * dt;
            if (next >= 1) {
                const uint64_t c = m.at();
                m.arp.phase = next;
                while (m.arp.phase >= 1) {
                    m.arp.phase -= 1;
                    m.arpTick(c);
                }
            } else {
                m.arp.phase = next;
            }
        }
        // Sequencer: clock (internal or host), gates, cuts, retriggers, arpeggios of the tracks.
        m.seqSample(dt);
        if (g >= m.echoDue) m.echoRun();  // repeats of the notes
        if (i + 1 - m.rSeg >= kChunk) {
            m.flush(out, m.rSeg, i + 1);
            m.rSeg = i + 1;
        }
    }
    m.lanesCatchUp();  // tracks up to date between two renders (settings, snapshots)
    m.flush(out, m.rSeg, n);
    m.renderClicks(out, n);
    m.rOut = nullptr;
    m.rI = 0;
    // Quantize to VBL: the events of the block that arrive after its last frame boundary wait for the next one
    // (start of the next block). Beyond the block: ignored.
    uint32_t kept = 0;
    if (quant)
        for (; ev < m.eventCount; ++ev)
            if (m.events[ev].offset < n) {
                m.events[kept] = m.events[ev];
                m.events[kept++].offset = 0;
            }
    m.eventCount = kept;
}

void Engine::setBoardTone(int bassDb, int trebleDb) noexcept {
    Impl& m = *impl_;
    m.config.render.board.bassDb = bassDb;
    m.config.render.board.trebleDb = trebleDb;
    if (!m.prepared) return;
    m.outL.setBoardTone(bassDb, trebleDb);
    m.outR.setBoardTone(bassDb, trebleDb);
    for (int t = 0; t < kTracks; ++t)
        if (m.trackOut(t)) m.outT[size_t(t)].setBoardTone(bassDb, trebleDb);
    for (auto& c : m.chips) c.setBoardTone(bassDb, trebleDb);
    for (int t = 0; t < Impl::kShadows; ++t)
        if (m.trackOut(t)) m.shadow[size_t(t)].setBoardTone(bassDb, trebleDb);
    m.outSL.setBoardTone(bassDb, trebleDb);
    m.outSR.setBoardTone(bassDb, trebleDb);
}

uint32_t Engine::latencySamples() const noexcept {
    const Impl& m = *impl_;
    return m.studio() ? m.outL.latencySamples() : m.chips[0].latencySamples();
}

const std::array<uint8_t, 128>& Engine::manualCuts() const noexcept { return impl_->manualCut; }

Engine::VoiceInfo Engine::voice(int index) const noexcept {
    const Impl& m = *impl_;
    VoiceInfo info;
    if (index < 0 || index >= kVoices) return info;
    const Voice& v = m.voices[size_t(index)];
    info.on = v.on;
    info.tag = v.tag;
    info.track = v.track;
    info.note = v.note;
    info.age = v.age;
    info.notePeriod = v.tp;
    info.tonePeriod = v.tpEff ? v.tpEff : v.tp;
    info.volume = v.vol4;
    info.accent = v.accent;
    info.noise = v.noiseOn || v.ornNoiseOn;
    info.gen = v.gen;
    info.tone = v.toneOn;
    info.envelope = v.envVolume;
    info.noisePeriod = Impl::noiseOut(v);
    info.envPeriod = Impl::envOut(v);
    info.lfoPan = v.lfoPan;
    info.timerHz = v.timerOn ? v.timerHz : 0;
    info.timerActualHz = m.timerActualHz(v);
    info.adsrStage = v.adsrOn ? v.stage : 0;
    info.adsrLevel = v.adsrOn ? v.aRef : 1;
    info.adsrNow = v.adsrOn ? m.adsrAt(v, std::max(m.eventCycle(), v.aT)) : 1;
    info.fading = v.fading();
    return info;
}

Engine::DmaInfo Engine::dmaVoice(int index) const noexcept {
    DmaInfo info;
    if (index < 0 || index >= kVoices) return info;
    const Voice& v = impl_->voices[size_t(index)];
    if (v.gen != Generator::Dma) return info;
    info.on = v.on;
    info.slot = v.slot;
    info.position = v.pcmPos;
    info.step = v.pcmStep;
    return info;
}
int Engine::dmaPreviewSlot() const noexcept { return impl_->dmaBank.previewSlot(); }
int Engine::digidrumPreviewSlot() const noexcept { return impl_->digiPreviewSlot; }
int Engine::program(int track) const noexcept {
    return track >= 0 && track < kTracks ? impl_->program[size_t(track)] : 0;
}
bool Engine::trackStereo(int track) const noexcept {
    return track >= 0 && track < kTracks && impl_->trackStereo(track);
}
uint8_t Engine::dmaPlayingSlots() const noexcept {
    const Impl& m = *impl_;
    uint8_t mask = m.dmaBank.previewSlot() >= 0 ? uint8_t(1u << m.dmaBank.previewSlot()) : 0;
    for (const Voice& v : m.voices)
        if (v.on && v.gen == Generator::Dma) mask = uint8_t(mask | (1u << (v.slot & 7)));
    return mask;
}

int Engine::activeVoices() const noexcept {
    int n = 0;
    for (const Voice& v : impl_->voices) n += v.on;
    return n;
}

uint64_t Engine::droppedEvents() const noexcept { return impl_->dropped; }
uint64_t Engine::samplesRendered() const noexcept { return impl_->samples; }
const ymcore::ChipRenderer& Engine::chip(int index) const noexcept { return impl_->chips[size_t(std::clamp(index, 0, 1))]; }
void Engine::setWriteLog(std::vector<WriteRecord>* log) noexcept { impl_->log = log; }

// The first frame is due at the current cycle; the frames are taken as the chip renders (Impl::captureUntil).
void Engine::startCapture(int chip, std::span<RegisterFrame> frames, std::span<DmaFrame> dma) noexcept {
    Impl& m = *impl_;
    m.cap.chip = -1;
    m.cap.count = 0;
    if (m.studio() || chip < 0 || chip >= m.chipCount() || frames.empty()) return;
    m.cap.frames = frames.data();
    m.cap.dma = dma.empty() ? nullptr : dma.data();
    m.cap.dmaSerial = 0;
    m.cap.sync = -1;
    const size_t room = dma.empty() ? frames.size() : std::min(frames.size(), dma.size());
    m.cap.capacity = uint32_t(std::min<size_t>(room, UINT32_MAX));
    m.cap.hz = std::max<uint32_t>(1, m.global.vblHz);
    m.cap.start = m.cap.next = m.eventCycle();
    m.r13Written[size_t(chip)] = false;
    m.cap.chip = chip;
}

uint32_t Engine::stopCapture() noexcept {
    impl_->cap.chip = -1;
    return impl_->cap.count;
}

bool Engine::capturing() const noexcept { return impl_->cap.chip >= 0; }
uint32_t Engine::capturedFrames() const noexcept { return impl_->cap.count; }
uint32_t Engine::captureHz() const noexcept { return impl_->cap.hz; }

}  // namespace stym::ymengine
