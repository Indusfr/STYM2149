// STYM2149 — plugincore: engine driver for the plugin (audio thread), without JUCE
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/Driver.h"

#include <algorithm>
#include <cmath>

#include "plugincore/Instruments.h"

namespace stym::plugin {

using namespace ymengine;

namespace {

// Kinds of the queued events (Driver::Queued::type): one per engine call made by renderTo.
enum QType : uint8_t {
    qNoteOn, qNoteOff, qBend, qWheel, qAllOff, qManOff, qClock, qStart, qContinue, qStop, qSongPos,
    qSeqPlay, qSeqStop, qSeqSelect, qSeqPreview, qSeqSongJump, qDrumPreview,
    qDmaPreview,   // preview of a DMA slot; value 1: stop it
    qDigiPreview,  // preview of a Digidrum slot; value 1: stop it
    qProgram,      // Program Change
    qYmPlay,       // start of the YM Player (ymplay): manual play and sequencer released
    qPressure, qNotePressure, qMpeBend, qMpePressure, qMpeSlide  // MIDI expression
};

// Key on 29 bits: clock, chain and resampling on 2 bits each, then the frequency of the Custom clock on 23 bits;
// bit 29 of the request word says whether a new engine is wanted.
static_assert(kCustomClockMaxHz < (1 << 23));
constexpr int kActiveBit = 29;
uint32_t packKey(const ConfigKey& k) noexcept {
    return uint32_t(k.clock & 3) | uint32_t(k.chain & 3) << 2 | uint32_t(k.resampling & 3) << 4 |
           (k.hz & 0x7fffff) << 6;
}
ConfigKey unpackKey(uint64_t r) noexcept {
    ConfigKey k;
    k.clock = uint8_t(r & 3);
    k.chain = uint8_t((r >> 2) & 3);
    k.resampling = uint8_t((r >> 4) & 3);
    k.hz = uint32_t((r >> 6) & 0x7fffff);
    return k;
}

// Rounding of the prototype (Math.round).
int jsRound(double x) noexcept { return int(std::floor(x + 0.5)); }

// Track fields of the mixer strip: applied by setTrackMix, without rebuilding the track's sound (setTrack).
bool isMixField(int f) noexcept {
    switch (Track(f)) {
    case Track::On:
    case Track::Level:
    case Track::Pan:
    case Track::Mute:
    case Track::Solo:
    case Track::Send:
    case Track::LevelR:
    case Track::MuteR:
    case Track::SoloR:
    case Track::Link: return true;
    default: return false;
    }
}

// Global settings applied by setGlobal() (the others: engine configuration, STE bass and treble).
bool isEngineGlobal(int g) noexcept {
    switch (Global(g)) {
    case Global::ChipClock:
    case Global::ChipClockHz:
    case Global::OutputChain:
    case Global::Resampling:
    case Global::SteBass:
    case Global::SteTreble:
    case Global::Mpe:           // MPE: read by the driver
    case Global::MpeBendRange:
    case Global::Gate:  // global gate: carried by the tracks (trackGate)
    case Global::GateAll: return false;
    default: return true;
    }
}

// MPE: in 16 ch only, where every note has its own channel of the chip.
bool mpeOn(const ParamValues& v) noexcept {
    return value(v, Global::Mpe) >= 0.5f && voiceMode(v) == VoiceMode::Studio16;
}

// The same outputs from sample `at` of the block on, for a render segment that starts there.
Outputs offsetOutputs(const Outputs& o, uint32_t at) noexcept {
    Outputs r;
    r.left = o.left ? o.left + at : nullptr;
    r.right = o.right ? o.right + at : nullptr;
    for (size_t i = 0; i < r.track.size(); ++i) r.track[i] = o.track[i] ? o.track[i] + at : nullptr;
    for (size_t i = 0; i < r.chip.size(); ++i) r.chip[i] = o.chip[i] ? o.chip[i] + at : nullptr;
    r.click = o.click ? o.click + at : nullptr;
    return r;
}

}  // namespace

Driver::Driver() = default;

// The audio thread no longer runs: whatever either side still holds (mailboxes, prepared engine, snapshot, takes,
// YM Player slot) is freed here.
Driver::~Driver() {
    built_.clear();
    snapshots_.clear();
    delete incoming_;
    delete snapshot_;
    delete rec_;
    delete recIn_.exchange(nullptr);
    delete recOut_.exchange(nullptr);
    ymBox_.clear();
    delete ymSlot_;
}

uint32_t Driver::fadeSamples() const noexcept {
    // Panic fade of the engine: 64 steps at 48 kHz, then the resampling delay, so that the end of the fade
    // is output before the switch.
    // ceil(64 × rate / 48000) in integers.
    const uint32_t fade = uint32_t((uint64_t(64) * setup_.sampleRate + 47999) / 48000);
    return fade + (slot_ ? slot_->engine.latencySamples() : 0) + 1;
}

// A new engine with every setting of `values` and `snap`, then prepared for `key` (allocations): message thread, or
// the audio thread outside real time. Patterns, instruments and the current pattern are given to it once it is in
// use (applySnapshotData, carrySequencer), from the snapshot of that moment.
std::unique_ptr<Driver::EngineSlot> Driver::build(const ConfigKey& key, const ParamValues& values,
                                                  const Snapshot& snap) const {
    auto s = std::make_unique<EngineSlot>();
    s->key = key;
    Engine& e = s->engine;
    ymengine::GlobalParams g = toGlobal(values, snap.state.global);
    if (fixedSeqClock_) g.seqClock = *fixedSeqClock_;
    e.setGlobal(g);
    for (int t = 0; t < kTracks; ++t) e.setTrack(t, toTrack(values, t, snap.state.tracks[size_t(t)]));
    e.setKit(snap.state.kit);
    for (int i = 0; i < kDigidrumSlots; ++i) {
        e.setUserSample(i, snap.state.digidrums[size_t(i)]);
        e.setDigidrumSlot(i, digidrumEngineSlot(snap.state.digiSlots[size_t(i)]));
    }
    e.setDma(snap.state.dma);
    for (int i = 0; i < kDmaSlots; ++i) e.setDmaSample(i, snap.state.dmaSamples[size_t(i)]);
    s->kitVersion = snap.kitVersion;
    s->sampleVersion = snap.sampleVersion;
    s->dmaVersion = snap.dmaVersion;
    e.prepare(engineConfig(key, values, setup_.sampleRate, setup_.trackOutputs));
    return s;
}

// Global settings of the engine; outside real time (bounce, export), no metronome.
GlobalParams Driver::globalFor(const ParamValues& values) const noexcept {
    GlobalParams g = toGlobal(values, snapshot_->state.global);
    if (fixedSeqClock_) g.seqClock = *fixedSeqClock_;
    if (offline_) g.metronome = false;
    return g;
}

// Copy kept by the driver: engine instruments drawn from the state (message thread).
std::unique_ptr<Snapshot> Driver::copyOf(const Snapshot& snapshot) {
    auto s = std::make_unique<Snapshot>(snapshot);
    fillInstruments(s->state, s->instruments);
    return s;
}

void Driver::prepare(const Setup& setup, const ParamValues& values, const Snapshot& snapshot) {
    // Audio thread stopped: a running take stops with the engine that played it.
    if (rec_ && slot_) finishTake();
    built_.clear();
    snapshots_.clear();
    delete incoming_;
    incoming_ = nullptr;
    delete snapshot_;
    setup_ = setup;
    // A host that prepares at 0 Hz (probe, offline initialisation) would divide by zero in the resamplers and size
    // the buses from an infinite step: same floor as the offline renders.
    setup_.sampleRate = std::max<uint32_t>(8000, setup.sampleRate);
    snapshot_ = copyOf(snapshot).release();
    latest_ = copyOf(snapshot);
    wantKey_ = configKey(values);
    slot_ = build(wantKey_, values, *snapshot_);
    applySnapshotData();
    // No engine requested, no transition, no queued event; the controllers start again from rest.
    request_.store(0);
    requestSerial_ = 0;
    builtFor_ = 0;
    action_ = Action::None;
    fadeLeft_ = 0;
    bend_.fill(0);
    wheel_.fill(0);
    press_.fill(0);
    program_.fill(0);
    qHead_ = qTail_ = 0;
    appliedValid_ = false;
    applyAll(values);
    latency_.store(slot_->engine.latencySamples());
    prepared_.store(true, std::memory_order_release);
    // YM Player: new chip with the new settings, which resumes the current playback (audio thread stopped).
    ymRenderKey_ = wantKey_;
    ymRender_ = engineConfig(wantKey_, values, setup_.sampleRate, 0).render;
    // A playback posted while the audio was stopped, taken as takeYm() does: a new chip resumes the current playback,
    // a new tune starts its counters again.
    if (YmSlot* pending = ymBox_.take()) {
        if (pending->continues && pending->playback && ymSlot_ && ymSlot_->playback)
            pending->playback->continueFrom(*ymSlot_->playback);
        if (!pending->continues) {
            ymPosition_.store(0, std::memory_order_relaxed);
            ymFrames_.store(0, std::memory_order_relaxed);
        }
        delete ymSlot_;
        ymSlot_ = pending;
    }
    ymBox_.clear();
    if (ymSlot_ && ymSlot_->playback && ymTune_) {
        auto p = std::make_unique<YmPlayback>(ymTune_, ymSubsong_, ymRender_, ymStream_);
        p->continueFrom(*ymSlot_->playback);
        ymSlot_->playback = std::move(p);
        ymSlot_->continues = false;
        ymKey_ = wantKey_;
        ymRate_ = setup_.sampleRate;
    }
    ymBass_ = ymTreble_ = INT32_MIN;
}

// Two copies: latest_ stays here for the engines that service() builds; the other goes to the audio thread, which
// owns it until it retires it.
void Driver::publish(const Snapshot& snapshot) {
    latest_ = copyOf(snapshot);
    snapshots_.collect();
    snapshots_.post(copyOf(snapshot));
}

void Driver::service(const ParamValues& values) {
    snapshots_.collect();
    built_.collect();
    ymBox_.collect();
    if (prepared()) {
        // YM Player: chain, resampling or rate changed → new chip that resumes playback.
        const ConfigKey key = configKey(values);
        ymRenderKey_ = key;
        ymRender_ = engineConfig(key, values, setup_.sampleRate, 0).render;
        if (ymTune_ && (key.chain != ymKey_.chain || key.resampling != ymKey_.resampling || setup_.sampleRate != ymRate_))
            postYm(true);
    }
    if (!latest_) return;
    // Engine request of the audio thread: built once per request word (serial and key), then posted. The audio
    // thread checks the key of what it takes, so an engine built for an outdated request is simply handed back.
    const uint64_t r = request_.load(std::memory_order_acquire);
    if (((r >> kActiveBit) & 1) == 0 || r == builtFor_) return;
    built_.post(build(unpackKey(r), values, *latest_));
    builtFor_ = r;
}

bool Driver::armRecorder(int chip) {
    if (recState_.load(std::memory_order_acquire) != uint8_t(RecorderState::Idle) ||
        recIn_.load(std::memory_order_acquire) || recOut_.load(std::memory_order_acquire))
        return false;
    auto b = std::make_unique<RecBuffer>();
    b->frames.resize(kRecorderMaxFrames);
    b->take.chip = uint8_t(std::clamp(chip, 0, 1));
    recChip_.store(b->take.chip, std::memory_order_relaxed);
    recFrames_.store(0, std::memory_order_relaxed);
    recStop_.store(false, std::memory_order_release);
    recState_.store(uint8_t(RecorderState::Armed), std::memory_order_release);  // before the audio thread takes it
    recIn_.store(b.release(), std::memory_order_release);
    return true;
}

void Driver::disarmRecorder() {
    // Not yet taken by the audio thread (no block rendered since): handed back at once, without frames.
    if (RecBuffer* b = recIn_.exchange(nullptr, std::memory_order_acq_rel)) {
        delete b;
        recState_.store(uint8_t(RecorderState::Idle), std::memory_order_release);
        return;
    }
    if (recState_.load(std::memory_order_acquire) != uint8_t(RecorderState::Idle))
        recStop_.store(true, std::memory_order_release);
}

std::unique_ptr<RegisterTake> Driver::takeRecording() {
    std::unique_ptr<RecBuffer> b(recOut_.exchange(nullptr, std::memory_order_acq_rel));
    if (!b) return nullptr;
    auto t = std::make_unique<RegisterTake>(std::move(b->take));
    t->frames.assign(b->frames.begin(), b->frames.begin() + std::ptrdiff_t(b->count));
    return t;
}

// ---- Audio thread ----

// Start of a block: armed take received, started when playback starts, stopped when it stops.
void Driver::runRecorder(const ParamValues& values) noexcept {
    if (!rec_) {
        rec_ = recIn_.exchange(nullptr, std::memory_order_acq_rel);
        if (!rec_) return;
        rolling_ = false;
    }
    Engine& e = slot_->engine;
    const bool stop = recStop_.exchange(false, std::memory_order_acq_rel);
    const bool playing = host_.playing || e.seqStatus().playing;
    if (rolling_) {
        if (stop || !playing || !e.capturing()) finishTake();
        else recFrames_.store(e.capturedFrames(), std::memory_order_relaxed);
        return;
    }
    if (stop) {
        finishTake();
        return;
    }
    // Armed: the take starts at the first block that plays in a 3-channel mode, outside a transition.
    if (!playing || action_ != Action::None || voiceMode(values) == VoiceMode::Studio16) return;
    e.startCapture(rec_->take.chip, rec_->frames);
    if (!e.capturing()) return;  // chip absent (a single chip): the take stays armed
    rolling_ = true;
    rec_->take.frameHz = e.captureHz();
    const ymcore::MasterClock clk = e.chip(0).config().chip.clock;
    rec_->take.clockNum = clk.num;
    rec_->take.clockDen = clk.den;
    recState_.store(uint8_t(RecorderState::Rolling), std::memory_order_release);
}

// End of the take (audio thread, or message thread with the audio thread stopped): handed back to the message
// thread. recOut_ is free: armRecorder does not start as long as a take has not been collected.
void Driver::finishTake() noexcept {
    if (!rec_) return;
    rec_->count = rolling_ ? slot_->engine.stopCapture() : 0;
    rolling_ = false;
    recFrames_.store(rec_->count, std::memory_order_relaxed);
    recOut_.store(rec_, std::memory_order_release);
    rec_ = nullptr;
    recState_.store(uint8_t(RecorderState::Idle), std::memory_order_release);
}

void Driver::takeSnapshot() noexcept {
    if (!snapshots_.canRetire()) return;  // the message thread has not destroyed the previous ones yet: later
    Snapshot* s = snapshots_.take();
    if (!s) return;
    Snapshot* old = snapshot_;
    snapshot_ = s;
    extrasDirty_ = true;
    applySnapshotData();   // the engine now reads the patterns of the new copy
    snapshots_.retire(old);
}

void Driver::applySnapshotData() noexcept {
    // Kit and samples: only what changed. setKit recomputes the kit on this thread (a few hundred microseconds,
    // a known limit).
    EngineSlot& s = *slot_;
    if (s.kitVersion != snapshot_->kitVersion) {
        s.engine.setKit(snapshot_->state.kit);
        s.kitVersion = snapshot_->kitVersion;
    }
    for (int i = 0; i < kDigidrumSlots; ++i)
        if (s.sampleVersion[size_t(i)] != snapshot_->sampleVersion[size_t(i)]) {
            s.engine.setUserSample(i, snapshot_->state.digidrums[size_t(i)]);
            s.sampleVersion[size_t(i)] = snapshot_->sampleVersion[size_t(i)];
        }
    for (int i = 0; i < kDigidrumSlots; ++i)
        s.engine.setDigidrumSlot(i, digidrumEngineSlot(snapshot_->state.digiSlots[size_t(i)]));
    s.engine.setDma(snapshot_->state.dma);
    for (int i = 0; i < kDmaSlots; ++i)
        if (s.dmaVersion[size_t(i)] != snapshot_->dmaVersion[size_t(i)]) {
            s.engine.setDmaSample(i, snapshot_->state.dmaSamples[size_t(i)]);
            s.dmaVersion[size_t(i)] = snapshot_->dmaVersion[size_t(i)];
        }
    s.engine.setSequence(&snapshot_->seq);
    s.engine.setInstruments(&snapshot_->instruments);
    if (s.patternSerial != snapshot_->patternSerial) {
        s.engine.seqSelectPattern(0, snapshot_->state.seq.current);
        s.patternSerial = snapshot_->patternSerial;
    }
}

// Each block: publishes in request_ the configuration the parameters want, for service(); keeps a prepared engine
// that matches it in incoming_ (sync then starts the swap) and hands back one that no longer matches. retire() fails
// only when the ring of returned objects is full: the object is then kept and handed back at a later block.
void Driver::requestConfig(const ParamValues& values, bool offline) noexcept {
    const ConfigKey want = configKey(values);
    if (!(want == wantKey_)) {
        wantKey_ = want;
        ++requestSerial_;
    }
    bool active = !(want == slot_->key);
    if (active && offline && action_ == Action::None) {
        // Outside real time: new preparation at once, at the same sample on each render (allocations allowed: the
        // thread has no deadline).
        std::unique_ptr<EngineSlot> fresh = build(want, values, *snapshot_);
        delete incoming_;
        incoming_ = nullptr;
        if (rolling_) finishTake();  // the take stops with its engine
        const Engine::SeqStatus seq = slot_->engine.seqStatus();
        slot_ = std::move(fresh);
        ++swaps_;
        appliedValid_ = false;
        latency_.store(slot_->engine.latencySamples(), std::memory_order_relaxed);
        replayControllers();
        carrySequencer(seq);
        active = false;
    }
    request_.store(uint64_t(requestSerial_) << 32 | uint64_t(active ? 1 : 0) << kActiveBit | packKey(want),
                   std::memory_order_release);
    if (incoming_ && !(incoming_->key == want) && action_ == Action::None && built_.retire(incoming_))
        incoming_ = nullptr;  // request outdated
    if (active && !incoming_ && built_.canRetire()) {
        if (EngineSlot* b = built_.take()) {
            if (b->key == want)
                incoming_ = b;
            else
                built_.retire(b);
        }
    }
}

// Every parameter and track setting applied to the current engine, whatever changed.
void Driver::applyAll(const ParamValues& values) noexcept {
    Engine& e = slot_->engine;
    const Snapshot& s = *snapshot_;
    e.setGlobal(globalFor(values));
    appliedOffline_ = offline_;
    e.setBoardTone(intValue(values, Global::SteBass), intValue(values, Global::SteTreble));
    for (int t = 0; t < kTracks; ++t) {
        const TrackParams p = toTrack(values, t, s.state.tracks[size_t(t)]);
        e.setTrack(t, p);
        appliedGen_[size_t(t)] = p.patch.generator;
    }
    appliedMode_ = voiceMode(values);
    appliedSte_ = steMachine(values);
    applied_ = values;
    appliedValid_ = true;
    extrasDirty_ = false;
}

// Start of each block, after each CC that changes a parameter, and at the end of a fade: applies what changed since
// applied_. A new mode or machine and a prepared engine first go through a fade (startAction); otherwise only the
// changed globals, the STE tone and the changed tracks are applied (mix only, or the whole track sound).
void Driver::sync(const ParamValues& values) noexcept {
    requestConfig(values, offline_);
    if (action_ != Action::None) return;  // applied at the end of the fade
    if (!appliedValid_) {
        applyAll(values);
        return;
    }
    if (voiceMode(values) != appliedMode_ || steMachine(values) != appliedSte_) {  // mode or machine
        startAction(Action::ModeChange);
        return;
    }
    if (incoming_) {
        startAction(Action::EngineSwap);
        return;
    }
    Engine& e = slot_->engine;
    const Snapshot& s = *snapshot_;
    auto changed = [&](int i) { return values[size_t(i)] != applied_[size_t(i)]; };

    bool global = extrasDirty_ || offline_ != appliedOffline_;
    for (int g = 0; g < kGlobalParams && !global; ++g)
        if (isEngineGlobal(g) && changed(g)) global = true;
    if (changed(paramIndex(Global::MidiRouting))) e.allNotesOff(0);  // the held notes change destination
    if (global) {
        e.setGlobal(globalFor(values));
        appliedOffline_ = offline_;
    }
    if (changed(paramIndex(Global::SteBass)) || changed(paramIndex(Global::SteTreble)))
        e.setBoardTone(intValue(values, Global::SteBass), intValue(values, Global::SteTreble));

    // Global gate switched on, off or changed while it plays: all tracks take it up.
    const bool gate = changed(paramIndex(Global::GateAll)) ||
                      (value(values, Global::GateAll) >= 0.5f && changed(paramIndex(Global::Gate)));
    bool release = false;
    for (int t = 0; t < kTracks; ++t) {
        bool patch = extrasDirty_ || gate, mix = false;
        for (int f = 0; f < kTrackParams; ++f)
            if (changed(paramIndex(t, Track(f)))) (isMixField(f) ? mix : patch) = true;
        if (patch) {
            const TrackParams p = toTrack(values, t, s.state.tracks[size_t(t)]);
            // New generator: the old timbre must not survive (manPanic of setEffect).
            if (p.patch.generator != appliedGen_[size_t(t)]) release = true;
            appliedGen_[size_t(t)] = p.patch.generator;
            e.setTrack(t, p);
        } else if (mix) {
            e.setTrackMix(0, t, toMix(values, t));
        }
    }
    if (release) e.releaseManual(0);
    applied_ = values;
    extrasDirty_ = false;
}

// The notes are released now; the action completes fadeSamples() later (finishAction), once the fade is heard.
void Driver::startAction(Action a) noexcept {
    slot_->engine.allNotesOff(0);
    fadeLeft_ = fadeSamples();
    action_ = a;
}

// New engine: settings published, and the sequencer continues on the same pattern, playback restarted at the start
// of the pattern in Free run (in Host sync, the host transport puts it back by itself; with MIDI clock, at the next
// Start or Continue).
void Driver::carrySequencer(const Engine::SeqStatus& seq) noexcept {
    slot_->patternSerial = snapshot_->patternSerial;
    applySnapshotData();
    if (seq.pattern >= 0) slot_->engine.seqSelectPattern(0, seq.pattern);  // -1: Rest entry of the Song
    if (seq.playing) slot_->engine.seqPlay(0);
}

// A new engine starts from rest: the pitch bend, mod wheel, channel pressure and Program Change received so far are
// sent to it again.
void Driver::replayControllers() noexcept {
    Engine& e = slot_->engine;
    for (int t = 0; t < kTracks; ++t) {
        if (bend_[size_t(t)] != 0) e.pitchBend(0, bend_[size_t(t)], t);
        if (wheel_[size_t(t)] != 0) e.modWheel(0, wheel_[size_t(t)], t);
        if (press_[size_t(t)] != 0) e.channelPressure(0, press_[size_t(t)], t);
        if (program_[size_t(t)] != 0) e.programChange(0, program_[size_t(t)], t);
    }
}

void Driver::finishAction(const ParamValues& values) noexcept {
    if (action_ == Action::EngineSwap) {
        if (!incoming_ || !(incoming_->key == wantKey_)) {
            // The request changed during the fade: we keep the current engine (silent), the rest resumes.
            if (incoming_ && built_.retire(incoming_)) incoming_ = nullptr;
        } else {
            if (!built_.canRetire()) {
                fadeLeft_ = 64;  // queue full: new attempt a little later (output already silent)
                return;
            }
            const Engine::SeqStatus seq = slot_->engine.seqStatus();
            if (rolling_) finishTake();  // the take stops with its engine
            built_.retire(slot_.release());
            slot_.reset(incoming_);
            incoming_ = nullptr;
            ++swaps_;
            latency_.store(slot_->engine.latencySamples(), std::memory_order_relaxed);
            replayControllers();
            carrySequencer(seq);
        }
    }
    action_ = Action::None;
    fadeLeft_ = 0;
    appliedValid_ = false;  // everything is applied again (new mode or new engine)
    sync(values);
}

// Event at absolute sample `at` of the block. `track` -1: a note goes where the engine sends the keyboard (focused
// track, or every active track in Layer), a bend, wheel or Program Change to every track. Beyond kQueue events in
// one block, dropped and counted.
void Driver::queue(uint32_t at, uint8_t type, int track, int a, int b, double value) noexcept {
    if (qTail_ >= kQueue) {
        ++droppedMidi_;
        return;
    }
    queue_[qTail_++] = Queued{at, type, int8_t(track), uint8_t(a), uint8_t(b), value};
}

// Renders the segment [pos_, to) of the block: the queued events before `to` go to the engine first, at their offset
// in the segment (the engine counts offsets from the start of its render).
void Driver::renderTo(const Outputs& out, uint32_t to) noexcept {
    if (to <= pos_) return;
    Engine& e = slot_->engine;
    while (qHead_ < qTail_ && queue_[qHead_].at < to) {
        const Queued& q = queue_[qHead_++];
        const uint32_t rel = q.at - pos_;
        switch (q.type) {
        case qNoteOn: e.noteOn(rel, q.a, q.b, q.track, int(q.value)); break;  // value: MPE channel
        case qNoteOff: e.noteOff(rel, q.a, q.track, int(q.value)); break;
        case qBend: e.pitchBend(rel, q.value, q.track); break;
        case qWheel: e.modWheel(rel, q.value, q.track); break;
        case qPressure: e.channelPressure(rel, q.value, q.track); break;
        case qNotePressure: e.notePressure(rel, q.a, q.value, q.track); break;
        case qMpeBend: e.mpeBend(rel, q.a, q.value); break;
        case qMpePressure: e.mpePressure(rel, q.a, q.value); break;
        case qMpeSlide: e.mpeSlide(rel, q.a, q.value); break;
        case qAllOff: e.allNotesOff(rel); break;
        case qManOff: e.releaseManual(rel); break;
        case qClock: e.midiClock(rel); break;
        case qStart: e.midiStart(rel); break;
        case qContinue: e.midiContinue(rel); break;
        case qStop: e.midiStop(rel); break;
        case qSongPos: e.midiSongPosition(rel, int(q.value)); break;
        case qSeqPlay: e.seqPlay(rel); break;
        case qSeqStop: e.seqStop(rel); break;
        case qSeqSelect: e.seqSelectPattern(rel, q.a); break;
        case qSeqPreview: e.seqPreview(rel, q.track, q.a, q.value); break;
        case qSeqSongJump: e.seqSongJump(rel, int(q.value)); break;
        case qDrumPreview: e.drumPreview(rel, q.a); break;
        case qDigiPreview: q.value > 0 ? e.digidrumStopPreview(rel) : e.digidrumPreview(rel, q.a); break;
        case qDmaPreview: q.value > 0 ? e.dmaStopPreview(rel) : e.dmaPreview(rel, q.a); break;
        case qProgram: e.programChange(rel, q.a, q.track); break;
        case qYmPlay: e.releaseForPlayer(rel); break;
        default: break;
        }
    }
    const uint32_t n = to - pos_;
    // Host sync: host position at the start of this segment of the block (pos_ samples later: pos_ / rate seconds,
    // × bpm / 60 quarter notes).
    Engine::HostTransport t = host_;
    if (t.playing) t.ppq += double(pos_) * std::max(1.0, t.bpm) / 60.0 / double(setup_.sampleRate);
    e.setHostTransport(t);
    e.render(offsetOutputs(out, pos_), n);
    if (action_ != Action::None) fadeLeft_ -= std::min(fadeLeft_, n);
    pos_ = to;
}

void Driver::finishDueActions(const ParamValues& values, const Outputs& out, uint32_t at) noexcept {
    // Finishes the fades that end at `at` at the latest (before queueing an event placed at `at`).
    while (action_ != Action::None && pos_ + fadeLeft_ <= at) {
        renderTo(out, pos_ + fadeLeft_);
        finishAction(values);
    }
}

void Driver::advance(const ParamValues& values, const Outputs& out, uint32_t to) noexcept {
    finishDueActions(values, out, to);
    renderTo(out, to);
}

// A parameter changed by a MIDI CC: clamped to its range, stored in `values` and reported to the host if it changed.
void Driver::setParam(ParamValues& values, int index, float v, ParamListener* listener) noexcept {
    v = clampToSpec(paramSpec(index), v);
    if (values[size_t(index)] == v) return;
    values[size_t(index)] = v;
    if (listener) listener->parameterChangedByMidi(index, v);
}

// A host CC: controllers that act on the engine are queued; those of the prototype change a parameter at
// sample `at` (render cut there). True when a parameter changed.
bool Driver::controlChange(ParamValues& values, int cc, int v, int channel, uint32_t at, const Outputs& out,
                           ParamListener* listener) noexcept {
    const bool perChannel = midiRouting(values) == MidiRouting::PerChannel && !mpeOn(values);
    const int track = perChannel ? channel : -1;
    switch (cc) {
    case 1: {  // mod wheel: vibrato or the Wheel target
        const double amount = double(v) / 127.0;
        queue(at, qWheel, track, 0, 0, amount);
        for (int t = 0; t < kTracks; ++t)
            if (track < 0 || t == track) wheel_[size_t(t)] = amount;
        return false;
    }
    case 120:
    case 123: queue(at, qAllOff, -1, 0, 0, 0); return false;
    case 82: queue(at, v >= 64 ? qSeqPlay : qSeqStop, -1, 0, 0, 0); return false;  // sequencer Play / Stop
    case 121:
        queue(at, qBend, track, 0, 0, 0);
        queue(at, qWheel, track, 0, 0, 0);
        queue(at, qPressure, track, 0, 0, 0);
        for (int t = 0; t < kTracks; ++t)
            if (track < 0 || t == track) bend_[size_t(t)] = wheel_[size_t(t)] = press_[size_t(t)] = 0;
        return false;
    default: break;
    }

    // Controllers of the prototype (onCC): parameters of the focused track, or of the channel's track. The same
    // arithmetic as onCC: most values 0-127 span the slider's range (v / 127 × span, Math.round), switches are on
    // from 64, shape and focused track take v / 8.
    const int t = perChannel ? channel : std::clamp(intValue(values, Global::FocusTrack), 1, kTracks) - 1;
    int index = -1;
    float nv = 0;
    switch (cc) {
    case 70: index = paramIndex(t, Track::Tone), nv = v >= 64 ? 1.0f : 0.0f; break;
    case 71: index = paramIndex(t, Track::Noise), nv = v >= 64 ? 1.0f : 0.0f; break;
    case 72: index = paramIndex(t, Track::NoisePeriod), nv = float(1 + jsRound(double(v) / 127.0 * 30.0)); break;
    case 73: index = paramIndex(t, Track::VolMode), nv = v >= 64 ? 1.0f : 0.0f; break;
    case 74: index = paramIndex(t, Track::EnvPeriod), nv = float(envPeriodFromCc(v)); break;
    case 75: index = paramIndex(t, Track::EnvShape), nv = float(std::min(15, v >> 3)); break;
    case 76: {
        const Generator g = Generator(std::min(4, v / 26));  // CC 76 of the prototype: the five original types
        if (!generatorAllowed(voiceMode(values), steMachine(values), t, g)) return false;  // fixed role
        index = paramIndex(t, Track::Generator);
        nv = float(int(g));
        break;
    }
    case 77: index = paramIndex(t, Track::SidRatio), nv = float(25 + jsRound(double(v) / 127.0 * 775.0)) / 100.0f; break;
    case 78: index = paramIndex(t, Track::SidVmin), nv = float(jsRound(double(v) / 127.0 * 15.0)); break;
    case 80: index = paramIndex(t, Track::ArpOn), nv = v >= 64 ? 1.0f : 0.0f; break;
    case 81: index = paramIndex(t, Track::ArpRate), nv = float(4 + jsRound(double(v) / 127.0 * 196.0)); break;
    case 83: index = paramIndex(Global::FreeTempo), nv = float(60 + jsRound(double(v) / 127.0 * 180.0)); break;
    case 84: index = paramIndex(Global::FocusTrack), nv = float(std::min(kTracks - 1, v >> 3) + 1); break;
    case 7: index = paramIndex(Global::Master), nv = float(jsRound(double(v) / 127.0 * 100.0)); break;
    default: return false;  // the others are not assigned
    }

    // The change takes effect at this sample: render up to there, then the setting.
    advance(values, out, at);
    setParam(values, index, nv, listener);
    if (cc == 76 && Generator(int(nv)) == Generator::SyncBuzzer &&
        value(values, t, Track::EnvPeriod) > float(100 * 32))
        // setEffect of the prototype: the Sync Buzzer wants a fast envelope (EP > 100 → 1, i.e. 32 on the chip).
        setParam(values, paramIndex(t, Track::EnvPeriod), 32.0f, listener);
    sync(values);
    return true;
}

// Sorts one event of the block, placed at sample `at`: interface commands, then real-time and system messages, then
// channel messages, routed to the channel's track (per-channel routing) or left to the engine (track -1).
void Driver::midi(ParamValues& values, const MidiEvent& e, uint32_t at, const Outputs& out,
                  ParamListener* listener) noexcept {
    switch (e.command) {  // interface commands
    case UiCommand::None: break;
    case UiCommand::SeqPlay: queue(at, qSeqPlay, -1, 0, 0, 0); return;
    case UiCommand::SeqStop: queue(at, qSeqStop, -1, 0, 0, 0); return;
    case UiCommand::SeqSelect: queue(at, qSeqSelect, -1, e.data[0], 0, 0); return;
    case UiCommand::SeqPreview:
        queue(at, qSeqPreview, std::min<int>(e.data[0], kTracks - 1), e.data[1] & 0x7f, 0, double(e.data[2] & 0x7f) / 127.0);
        return;
    case UiCommand::SeqSongJump: queue(at, qSeqSongJump, -1, 0, 0, double(e.data[0] | e.data[1] << 8)); return;
    case UiCommand::DrumPreview: queue(at, qDrumPreview, -1, std::min<int>(e.data[0], kDrumSlots - 1), 0, 0); return;
    case UiCommand::DmaPreview:
        queue(at, qDmaPreview, -1, std::min<int>(e.data[0], kDmaSlots - 1), 0, e.data[1] ? 1 : 0);
        return;
    case UiCommand::DigiPreview:
        queue(at, qDigiPreview, -1, std::min<int>(e.data[0], kDigidrumSlots - 1), 0, e.data[1] ? 1 : 0);
        return;
    }
    if (e.size == 0) return;
    const uint8_t st = e.data[0];
    if (st >= 0xf8) {  // real time: clock, Start, Continue, Stop
        if (st == 0xf8) queue(at, qClock, -1, 0, 0, 0);
        else if (st == 0xfa) queue(at, qStart, -1, 0, 0, 0);
        else if (st == 0xfb) queue(at, qContinue, -1, 0, 0, 0);
        else if (st == 0xfc) queue(at, qStop, -1, 0, 0, 0);
        return;
    }
    if (st == 0xf2) {  // Song Position Pointer: sixteenth notes since the start
        if (e.size >= 3) queue(at, qSongPos, -1, 0, 0, double((e.data[2] & 0x7f) << 7 | (e.data[1] & 0x7f)));
        return;
    }
    if (st < 0x80 || st >= 0xf0) return;
    const int ch = st & 0x0f;
    const int d1 = e.size > 1 ? e.data[1] & 0x7f : 0;
    const int d2 = e.size > 2 ? e.data[2] & 0x7f : 0;
    // MPE: channels 2 to 16 (members) play a note each, with its own bend, pressure and CC74; channel 1 (the
    // master) and the other messages act on all the notes. The notes go where the keyboard plays (focused track or
    // Layer), per channel routing aside.
    const bool mpe = !e.screen && mpeOn(values);
    const int member = mpe ? ch : 0;
    const int track = !e.screen && !mpe && midiRouting(values) == MidiRouting::PerChannel ? ch : -1;
    if (e.screen && (st & 0xf0) == 0xb0) {
        if (d1 == 120) queue(at, qManOff, -1, 0, 0, 0);
        else if (d1 == 123) queue(at, qAllOff, -1, 0, 0, 0);
        return;
    }
    switch (st & 0xf0) {
    case 0x90:
        if (e.size < 3) return;
        if (d2 == 0) {
            queue(at, qNoteOff, track, d1, 0, member);
        } else if (action_ == Action::None) {  // during a transition fade, the notes are ignored
            queue(at, qNoteOn, track, d1, d2, member);
        }
        return;
    case 0x80:
        if (e.size < 3) return;
        queue(at, qNoteOff, track, d1, 0, member);
        return;
    case 0xe0: {
        // Pitch bend: 14 bits (LSB first) centred on 8192, ± Bend Range semitones; kept for a new engine. That of an
        // MPE member, ± MPE Bend Range, bends its notes.
        if (e.size < 3) return;
        const double bent = double((d2 << 7 | d1) - 8192) / 8192.0;
        if (member) {
            queue(at, qMpeBend, -1, member, 0, bent * double(value(values, Global::MpeBendRange)));
            return;
        }
        const double semis = bent * double(value(values, Global::BendRange));
        queue(at, qBend, track, 0, 0, semis);
        for (int t = 0; t < kTracks; ++t)
            if (track < 0 || t == track) bend_[size_t(t)] = semis;
        return;
    }
    case 0xb0:
        if (e.size < 3) return;
        if (member && d1 == 74) {  // slide of the notes of a member
            queue(at, qMpeSlide, -1, member, 0, double(d2) / 127.0);
            return;
        }
        if (member && d1 == 121) {  // Reset All Controllers of a member: its bend, pressure and slide
            queue(at, qMpeBend, -1, member, 0, 0);
            queue(at, qMpePressure, -1, member, 0, 0);
            queue(at, qMpeSlide, -1, member, 0, 0.5);
            return;
        }
        controlChange(values, d1, d2, ch, at, out, listener);
        return;
    case 0xd0: {
        // Channel pressure: that of the tracks, kept for a new engine; that of an MPE member, of its notes.
        if (e.size < 2) return;
        const double amount = double(d1) / 127.0;
        if (member) {
            queue(at, qMpePressure, -1, member, 0, amount);
            return;
        }
        queue(at, qPressure, track, 0, 0, amount);
        for (int t = 0; t < kTracks; ++t)
            if (track < 0 || t == track) press_[size_t(t)] = amount;
        return;
    }
    case 0xa0:  // polyphonic pressure: of one note (an MPE member has its channel pressure)
        if (e.size < 3 || member) return;
        queue(at, qNotePressure, track, d1, 0, double(d2) / 127.0);
        return;
    case 0xc0: {
        // Program Change n: instrument n (1 to 32) on the channel's track (per-channel routing), the focused
        // track (Omni) or all tracks (Layer, which plays them together); 0 gives the track its own sound back; above
        // 32, ignored. The on-screen keyboard does not send any.
        if (e.size < 2 || d1 > kInstruments) return;
        const int to = track >= 0                                      ? track
                       : midiRouting(values) == MidiRouting::Layer ? -1
                                                                   : std::clamp(intValue(values, Global::FocusTrack), 1, kTracks) - 1;
        queue(at, qProgram, to, d1, 0, 0);
        for (int t = 0; t < kTracks; ++t)
            if (to < 0 || t == to) program_[size_t(t)] = uint8_t(d1);
        return;
    }
    default: return;
    }
}

// ------------------------------------------------------------------------------------------------- YM Player

// Message thread: posts a playback of the current tune (an empty slot without a tune). `continues`: the same playback
// on a new chip (output settings changed), which takeYm() resumes where the old one is; otherwise a new playback
// from the start, with a new serial.
void Driver::postYm(bool continues) {
    auto slot = std::make_unique<YmSlot>();
    slot->serial = continues ? ymSerial_ : ++ymSerial_;
    slot->continues = continues;
    if (ymTune_) {
        if (!continues) ymStream_ = nullptr;  // SNDH: new capture, from the init of the subsong
        slot->playback = std::make_unique<YmPlayback>(ymTune_, ymSubsong_, ymRender_, ymStream_);
        ymStream_ = slot->playback->stream();
        ymKey_ = ymRenderKey_;
        ymRate_ = setup_.sampleRate;
    } else {
        ymStream_ = nullptr;
    }
    ymBox_.collect();
    ymBox_.post(std::move(slot));
}

void Driver::ymLoad(std::shared_ptr<const YmTune> tune) {
    ymOn_.store(false, std::memory_order_release);
    ymFailed_.store(false, std::memory_order_relaxed);
    ymTune_ = std::move(tune);
    ymSubsong_ = ymTune_ ? ymTune_->defaultSubsong() : 1;
    postYm(false);
}

void Driver::ymPlay(bool on) {
    if (!ymTune_) return;
    if (on) ymPlaySerial_.fetch_add(1, std::memory_order_acq_rel);
    ymOn_.store(on, std::memory_order_release);
}

void Driver::ymRewind() {
    if (!ymTune_) return;
    ymFailed_.store(false, std::memory_order_relaxed);
    postYm(false);
    ymPlay(true);
}

void Driver::ymSubsong(int subsong) {
    if (!ymTune_) return;
    ymSubsong_ = std::clamp(subsong, 1, ymTune_->subsongs());
    ymFailed_.store(false, std::memory_order_relaxed);
    postYm(false);
}

void Driver::ymClose() {
    ymOn_.store(false, std::memory_order_release);
    ymTune_ = nullptr;
    postYm(false);
}

Driver::YmStatus Driver::ymStatus() const noexcept {
    YmStatus s;
    s.position = ymPosition_.load(std::memory_order_relaxed);
    s.frames = ymFrames_.load(std::memory_order_relaxed);
    const uint64_t lo = ymRegsLo_.load(std::memory_order_relaxed), hi = ymRegsHi_.load(std::memory_order_relaxed);
    // Byte r of lo is register r (R0-R7), byte r - 8 of hi register r (R8-R13), as packed by renderYm.
    for (unsigned r = 0; r < 14; ++r) s.regs[r] = uint8_t((r < 8 ? lo >> (8 * r) : hi >> (8 * (r - 8))) & 0xff);
    s.failed = ymFailed_.load(std::memory_order_relaxed);
    return s;
}

// Audio thread, start of a block: the playback posted by the message thread replaces the current one, which is
// handed back to be destroyed there.
void Driver::takeYm() noexcept {
    if (!ymBox_.canRetire()) return;
    YmSlot* s = ymBox_.take();
    if (!s) return;
    if (s->continues && s->playback && ymSlot_ && ymSlot_->playback) s->playback->continueFrom(*ymSlot_->playback);
    if (!s->continues) {
        ymPosition_.store(0, std::memory_order_relaxed);
        ymFrames_.store(0, std::memory_order_relaxed);
    }
    ymBox_.retire(ymSlot_);
    ymSlot_ = s;
    ymBass_ = ymTreble_ = INT32_MIN;
}

// Output of the tune instead of the engine's: mono, output law of the engine in 3 channels (gain3 × volume × 1.2,
// tanh saturation according to the setting), like the YM Player output of the prototype (tanh(Σ × master × 1.2)).
void Driver::renderYm(const ParamValues& values, const Outputs& out, uint32_t n) noexcept {
    YmPlayback* p = ymSlot_ ? ymSlot_->playback.get() : nullptr;
    if (!p || !ymOn_.load(std::memory_order_acquire) || n == 0 || !out.left) return;
    const int bass = intValue(values, Global::SteBass), treble = intValue(values, Global::SteTreble);
    if (bass != ymBass_ || treble != ymTreble_) {
        p->setBoardTone(bass, treble);
        ymBass_ = bass;
        ymTreble_ = treble;
    }
    p->render(out.left, n, offline_);
    const GlobalParams g = globalFor(values);
    // The player outputs mixing-table units / 32768: × 32768 / kMixTable[31] makes one channel at its highest level 1.
    const double gain = 32768.0 / double(ymcore::tables::kMixTable[31]) * g.master * 1.2;
    if (g.softClip)
        for (uint32_t i = 0; i < n; ++i) out.left[i] = float(std::tanh(double(out.left[i]) * gain));
    else
        for (uint32_t i = 0; i < n; ++i) out.left[i] = float(double(out.left[i]) * gain);
    if (out.right && out.right != out.left) std::copy(out.left, out.left + n, out.right);
    for (float* t : out.track)
        if (t) std::fill(t, t + n, 0.0f);
    for (float* c : out.chip)
        if (c) std::fill(c, c + n, 0.0f);
    if (out.click) std::fill(out.click, out.click + n, 0.0f);
    uint64_t lo = 0, hi = 0;
    for (unsigned r = 0; r < 14; ++r) (r < 8 ? lo : hi) |= uint64_t(p->reg(r)) << (8 * (r < 8 ? r : r - 8));
    ymRegsLo_.store(lo, std::memory_order_relaxed);
    ymRegsHi_.store(hi, std::memory_order_relaxed);
    ymPosition_.store(p->position(), std::memory_order_relaxed);
    ymFrames_.store(p->framesPlayed(), std::memory_order_relaxed);
    // SNDH routine crashed, everything it wrote played: playback stops (AtariAudio's SndhRenderer too).
    if (p->finished()) {
        ymFailed_.store(true, std::memory_order_relaxed);
        ymOn_.store(false, std::memory_order_release);
    }
}

void Driver::process(ParamValues& values, std::span<const MidiEvent> midiIn, const Outputs& out, uint32_t n,
                     bool offline, ParamListener* listener) noexcept {
    if (!slot_) {  // not prepared yet: silence
        auto clear = [n](float* p) {
            if (p) std::fill(p, p + n, 0.0f);
        };
        clear(out.left);
        clear(out.right);
        for (float* p : out.track) clear(p);
        for (float* p : out.chip) clear(p);
        clear(out.click);
        return;
    }
    // The block is rendered in segments: each event is queued at its sample, and the render is cut where a CC changes
    // a parameter (controlChange) or a fade ends (finishDueActions), so that each change applies at its sample.
    offline_ = offline;
    pos_ = 0;
    qHead_ = qTail_ = 0;
    takeSnapshot();
    sync(values);
    runRecorder(values);
    takeYm();
    // Start of the YM Player (ymplay): killTag('man') and killTag('seq') of the prototype, at the start of the block.
    if (const uint32_t serial = ymPlaySerial_.load(std::memory_order_acquire); serial != ymPlaySeen_) {
        ymPlaySeen_ = serial;
        if (ymOn_.load(std::memory_order_acquire)) queue(0, qYmPlay, -1, 0, 0, 0);
    }
    for (const MidiEvent& e : midiIn) {
        if (n == 0) break;
        // Never before what is already rendered (a CC may have moved pos_), never past the block.
        const uint32_t at = std::clamp(e.offset, pos_, n - 1);
        finishDueActions(values, out, at);
        midi(values, e, at, out, listener);
    }
    advance(values, out, n);
    // Fade finished exactly at the end of the block: switch now rather than at the next block.
    if (action_ != Action::None && fadeLeft_ == 0) finishAction(values);
    renderYm(values, out, n);
}

}  // namespace stym::plugin
