// STYM2149 — plugin: JUCE audio processor (MIDI instrument)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "PluginEditor.h"

namespace stym::plugin {
namespace {

juce::String str(std::string_view s) { return juce::String::fromUTF8(s.data(), int(s.size())); }

// JUCE parameter of an entry of the JUCE-free table (Parameters.h). Bool, Choice and an Int of step 1 on a linear
// scale take JUCE's own classes; anything else becomes an AudioParameterFloat whose range calls the table's own
// mapping (fromNormalized, toNormalized, clampToSpec), so that host and core agree on every normalised value.
std::unique_ptr<juce::RangedAudioParameter> makeParameter(const ParamSpec& s) {
    const juce::ParameterID id{str(s.id), s.version};
    const juce::String name = str(s.name);
    const juce::String label = str(s.unit);
    switch (s.kind) {
    case ParamKind::Bool:
        return std::make_unique<juce::AudioParameterBool>(
            id, name, s.def >= 0.5f, juce::AudioParameterBoolAttributes().withAutomatable(s.automatable));
    case ParamKind::Choice: {
        juce::StringArray names;
        for (std::string_view c : s.choices) names.add(str(c));
        return std::make_unique<juce::AudioParameterChoice>(
            id, name, names, int(s.def), juce::AudioParameterChoiceAttributes().withAutomatable(s.automatable));
    }
    case ParamKind::Int:
        if (s.step == 1 && s.scale == ParamScale::Linear)
            return std::make_unique<juce::AudioParameterInt>(
                id, name, int(s.min), int(s.max), int(s.def),
                juce::AudioParameterIntAttributes().withLabel(label).withAutomatable(s.automatable));
        break;
    case ParamKind::Float: break;
    }
    // Floats, integers with a step of 2 and envelope period (geometric scale).
    const ParamSpec spec = s;
    juce::NormalisableRange<float> range(
        s.min, s.max, [spec](float, float, float t) { return fromNormalized(spec, t); },
        [spec](float, float, float v) { return toNormalized(spec, v); },
        [spec](float, float, float v) { return clampToSpec(spec, v); });
    // Text shown by the host: no decimal for an integer, one for a step of 0.5 or more, two otherwise.
    const int decimals = s.kind == ParamKind::Int ? 0 : s.step >= 0.5f ? 1 : 2;
    auto attributes = juce::AudioParameterFloatAttributes()
                          .withLabel(label)
                          .withAutomatable(s.automatable)
                          .withStringFromValueFunction([decimals](float v, int) {
                              return decimals == 0 ? juce::String(juce::roundToInt(v)) : juce::String(v, decimals);
                          });
    return std::make_unique<juce::AudioParameterFloat>(id, name, range, s.def, attributes);
}

}  // namespace

// Message thread timer. JUCE does not wait for a call in progress when it is stopped from another thread: detach()
// stops the timer then waits for the end of a call in progress; after that, no call reaches the processor any more.
// The object itself is destroyed on the message thread, after any call already started (see ~StymProcessor).
class ServiceTimer final : public juce::Timer {
public:
    explicit ServiceTimer(StymProcessor& owner) : owner_(&owner) {}
    ~ServiceTimer() override { stopTimer(); }

    void timerCallback() override {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (owner_ != nullptr) owner_->serviceMessageThread();
    }
    void detach() {
        stopTimer();
        const std::lock_guard<std::mutex> lock(mutex_);
        owner_ = nullptr;
    }

private:
    std::mutex mutex_;
    StymProcessor* owner_;
};

juce::AudioProcessor::BusesProperties StymProcessor::makeBuses() {
    auto b = BusesProperties().withOutput("Main", juce::AudioChannelSet::stereo(), true);
    for (int t = 0; t < kTracks; ++t)
        b = b.withOutput("Track " + juce::String(t + 1), juce::AudioChannelSet::mono(), false);
    for (int c = 0; c < 2; ++c) b = b.withOutput("Chip " + juce::String(c + 1), juce::AudioChannelSet::mono(), false);
    return b;
}

StymProcessor::StymProcessor() : AudioProcessor(makeBuses()) {
    // Parameters in the table's order, grouped as "global" and "t01" to "t16"; params_ keeps a pointer to each one
    // (JUCE owns them), indexed like the table.
    auto global = std::make_unique<juce::AudioProcessorParameterGroup>("global", "Global", "|");
    std::array<std::unique_ptr<juce::AudioProcessorParameterGroup>, kTracks> tracks;
    for (int t = 0; t < kTracks; ++t)
        tracks[size_t(t)] = std::make_unique<juce::AudioProcessorParameterGroup>(
            juce::String("t") + juce::String(t + 1).paddedLeft('0', 2), "Track " + juce::String(t + 1), "|");
    for (int i = 0; i < kParamCount; ++i) {
        const ParamSpec& s = paramSpec(i);
        auto p = makeParameter(s);
        params_[size_t(i)] = p.get();
        (s.track < 0 ? *global : *tracks[size_t(s.track)]).addChild(std::move(p));
    }
    addParameterGroup(std::move(global));
    for (auto& g : tracks) addParameterGroup(std::move(g));
    values_ = defaultValues();
    extras_.global.seqClock = defaultSeqClock();
    midi_.reserve(4096);
    serviceTimer_ = std::make_shared<ServiceTimer>(*this);
    serviceTimer_->startTimerHz(30);
}

StymProcessor::~StymProcessor() {
    serviceTimer_->detach();
    // Outside the message thread, a call already taken from the JUCE queue can still target the timer object: it is
    // destroyed by a message, after that call (without a message manager, at once).
    if (!juce::MessageManager::existsAndIsCurrentThread())
        juce::MessageManager::callAsync([timer = std::move(serviceTimer_)] {});
}

const juce::String StymProcessor::getName() const { return JucePlugin_Name; }

// Longest release of the ADSR (2 s, the prototype's Release slider).
double StymProcessor::getTailLengthSeconds() const { return 2.0; }

// Only the buses of makeBuses: no input, a stereo main output, every other output mono or disabled.
bool StymProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (!layouts.inputBuses.isEmpty()) return false;
    if (layouts.outputBuses.size() != kBusCount) return false;
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i) {
        const auto& set = layouts.outputBuses.getReference(i);
        if (!set.isDisabled() && set != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

// Bit t set when the host has enabled the output of track t.
uint16_t StymProcessor::enabledTrackOutputs() const {
    uint16_t mask = 0;
    for (int t = 0; t < kTracks; ++t)
        if (const auto* bus = getBus(false, kTrackBusFirst + t); bus && bus->isEnabled()) mask = uint16_t(mask | 1u << t);
    return mask;
}

ParamValues StymProcessor::currentValues() const {
    ParamValues v{};
    for (int i = 0; i < kParamCount; ++i) {
        const auto* p = params_[size_t(i)];
        v[size_t(i)] = clampToSpec(paramSpec(i), p->convertFrom0to1(p->getValue()));
    }
    return v;
}

ymengine::SeqClock StymProcessor::defaultSeqClock() const noexcept {
    return wrapperType == wrapperType_Standalone ? ymengine::SeqClock::Internal : ymengine::SeqClock::Host;
}

Snapshot StymProcessor::makeSnapshot() const {
    Snapshot s;
    s.state = extras_;
    s.kitVersion = kitVersion_;
    s.sampleVersion = sampleVersion_;
    s.dmaVersion = dmaVersion_;
    // Standalone: no host transport, a state set to Host sync plays in Free run.
    if (wrapperType == wrapperType_Standalone && s.state.global.seqClock == ymengine::SeqClock::Host)
        s.state.global.seqClock = ymengine::SeqClock::Internal;
    fillSequence(s.state.seq, s.seq);
    s.patternSerial = patternSerial_;
    return s;
}

// Host thread, audio stopped: everything that allocates is done here (metronome buffer, engine, engine status).
// The enabled track outputs are read now: one enabled later stays silent until the next preparation.
void StymProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    {
        const std::lock_guard<std::mutex> lock(controlLock_);
        click_.assign(size_t(std::max(maxBlock, 0)), 0.0f);
        preparedOutputs_ = enabledTrackOutputs();
        values_ = currentValues();
        driver_.prepare({uint32_t(std::lround(sampleRate)), preparedOutputs_}, values_, makeSnapshot());
        engineStatus_.prepare(uint32_t(std::lround(sampleRate)));
    }
    // Outside the lock, as in serviceMessageThread: a VST3 host told of the new latency can call back into the plugin
    // (state, reactivation) from the same call stack, which would lock controlLock_ a second time.
    setLatencySamples(int(driver_.latencySamples()));
}

void StymProcessor::releaseResources() {}

void StymProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    // A block without samples (parameter flush of some VST3 hosts): nothing read from the on-screen queue, whose notes
    // and commands wait for the next block instead of being lost.
    if (n <= 0) {
        midi.clear();
        return;
    }
    // Host parameters (atomics) → plain values in table units, clamped; the driver applies what changed.
    for (int i = 0; i < kParamCount; ++i) {
        const auto* p = params_[size_t(i)];
        values_[size_t(i)] = clampToSpec(paramSpec(i), p->convertFrom0to1(p->getValue()));
    }

    midi_.clear();
    // On-screen playing first, at the start of the block.
    screenFifo_.read(screenFifo_.getNumReady()).forEach([&](int slot) {
        if (midi_.size() == midi_.capacity()) return;
        const auto& b = screenBuffer_[size_t(slot)];
        // Byte 0 is the command (None: a MIDI message of 3 bytes); a command has size 0, its arguments in data.
        MidiEvent e;
        e.command = UiCommand(b[0]);
        e.size = e.command == UiCommand::None ? 3 : 0;
        e.data = {b[1], b[2], b[3]};
        e.screen = true;
        midi_.push_back(e);
    });
    for (const auto m : midi) {
        if (m.numBytes <= 0 || m.numBytes > 3) continue;  // SysEx: ignored
        // MIDI learn: a learned CC or note, or one caught for the armed control, goes to the editor. A note off
        // goes on to the engine too (not to the editor's host notes): a note that was sounding when its key was
        // learned is released.
        const int st = m.data[0] & 0xF0;
        const bool claimed = capture_.claim(m.data, m.numBytes);
        if (claimed) {
            learnFifo_.write(1).forEach(
                [&](int slot) { std::copy(m.data, m.data + 3, learnBuffer_[size_t(slot)].begin()); });
            if (m.numBytes != 3 || !(st == 0x80 || (st == 0x90 && m.data[2] == 0))) continue;
        }
        if (midi_.size() == midi_.capacity()) break;      // no allocation on this thread
        MidiEvent e;
        e.offset = uint32_t(std::clamp(m.samplePosition, 0, std::max(0, n - 1)));
        e.size = uint8_t(m.numBytes);
        std::copy(m.data, m.data + m.numBytes, e.data.begin());
        midi_.push_back(e);
        if (claimed) continue;
        // Note on and off, CC 120 and 123 (all cut): also seen by the editor.
        if (e.size == 3 && (st == 0x80 || st == 0x90 || (st == 0xB0 && (e.data[1] == 120 || e.data[1] == 123))))
            hostNoteFifo_.write(1).forEach([&](int slot) { hostNoteBuffer_[size_t(slot)] = e.data; });
    }
    midi.clear();

    // Host transport (Host sync): playing, position in quarter notes, tempo, time signature, start of the bar.
    // Without a position in quarter notes, it is derived from the time in samples at the current tempo.
    ymengine::Engine::HostTransport host;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition()) {
            host.playing = pos->getIsPlaying();
            if (const auto bpm = pos->getBpm(); bpm && *bpm > 0) host.bpm = *bpm;
            if (const auto ppq = pos->getPpqPosition())
                host.ppq = *ppq;
            else if (const auto smp = pos->getTimeInSamples(); smp && getSampleRate() > 0)
                host.ppq = double(*smp) / getSampleRate() * host.bpm / 60.0;
            if (const auto sig = pos->getTimeSignature(); sig && sig->numerator > 0 && sig->denominator > 0) {
                host.sigNum = sig->numerator;
                host.sigDen = sig->denominator;
            }
            if (const auto bar = pos->getPpqPositionOfLastBarStart()) {
                host.hasBar = true;
                host.barPpq = *bar;
            }
        }
    driver_.setHostTransport(host);

    // Output pointers: the stereo main bus, then the mono track and chip buses; a null pointer (bus disabled, or a
    // track output not prepared) is not rendered.
    ymengine::Outputs out;
    auto main = getBusBuffer(buffer, false, 0);
    if (main.getNumChannels() < 2) return;
    out.left = main.getWritePointer(0);
    out.right = main.getWritePointer(1);
    for (int t = 0; t < kTracks; ++t) {
        if ((preparedOutputs_ & (1u << t)) == 0) continue;  // output not prepared: left at zero
        auto bus = getBusBuffer(buffer, false, kTrackBusFirst + t);
        if (bus.getNumChannels() > 0) out.track[size_t(t)] = bus.getWritePointer(0);
    }
    for (int c = 0; c < 2; ++c) {
        auto bus = getBusBuffer(buffer, false, kChipBusFirst + c);
        if (bus.getNumChannels() > 0) out.chip[size_t(c)] = bus.getWritePointer(0);
    }
    // The metronome apart, added after the oscilloscope and the output meters have taken the output. A block
    // longer than announced keeps it in the output, and on the oscilloscope.
    if (size_t(n) <= click_.size()) {
        std::fill(click_.begin(), click_.begin() + n, 0.0f);
        out.click = click_.data();
    }
    driver_.process(values_, midi_, out, uint32_t(n), isNonRealtime(), this);
    // A host that calls process before prepareToPlay (FL Studio's Patcher): silence, and no engine to look at yet.
    if (driver_.prepared()) engineStatus_.capture(driver_.engine(), values_, out.left, out.right, uint32_t(n));
    if (out.click)
        for (int i = 0; i < n; ++i) {
            out.left[i] += click_[size_t(i)];
            out.right[i] += click_[size_t(i)];
        }
}

void StymProcessor::pushScreenMidi(uint8_t status, uint8_t data1, uint8_t data2) noexcept {
    screenFifo_.write(1).forEach([&](int slot) {
        screenBuffer_[size_t(slot)] = {uint8_t(UiCommand::None), status, data1, data2};
    });
}

void StymProcessor::pushSeqCommand(UiCommand command, uint8_t a, uint8_t b, uint8_t c) noexcept {
    screenFifo_.write(1).forEach([&](int slot) { screenBuffer_[size_t(slot)] = {uint8_t(command), a, b, c}; });
}

bool StymProcessor::popHostNote(std::array<uint8_t, 3>& message) noexcept {
    bool got = false;
    hostNoteFifo_.read(1).forEach([&](int slot) {
        message = hostNoteBuffer_[size_t(slot)];
        got = true;
    });
    return got;
}

void StymProcessor::dropHostNotes() noexcept { hostNoteFifo_.read(hostNoteFifo_.getNumReady()).forEach([](int) {}); }

void StymProcessor::setMidiMap(std::string_view json) { capture_.set(midiKeys(parseMidiMap(json)), false); }

bool StymProcessor::popLearnMidi(std::array<uint8_t, 3>& message) noexcept {
    bool got = false;
    learnFifo_.read(1).forEach([&](int slot) {
        message = learnBuffer_[size_t(slot)];
        got = true;
    });
    return got;
}

void StymProcessor::dropLearnMidi() noexcept { learnFifo_.read(learnFifo_.getNumReady()).forEach([](int) {}); }

NonParamState StymProcessor::extras() const {
    const std::lock_guard<std::mutex> lock(controlLock_);
    return extras_;
}

// Copy, edit, sanitize, then publish a new Snapshot to the audio thread: the audio thread never sees extras_ itself.
void StymProcessor::editExtras(const std::function<void(NonParamState&)>& edit) {
    {
        const std::lock_guard<std::mutex> lock(controlLock_);
        NonParamState next = extras_;
        edit(next);
        sanitize(next);
        // The kit and the samples are rebuilt by the audio thread only if they change (setKit is costly).
        bool kitChanged = false;
        for (size_t i = 0; i < next.kit.inst.size(); ++i) {
            const auto &a = next.kit.inst[i], &b = extras_.kit.inst[i];
            kitChanged = kitChanged || a.a != b.a || a.b != b.b || a.dec != b.dec || a.noise != b.noise || a.vol != b.vol;
        }
        if (kitChanged) ++kitVersion_;
        for (size_t i = 0; i < next.digidrums.size(); ++i)
            if (next.digidrums[i] != extras_.digidrums[i]) ++sampleVersion_[i];
        for (size_t i = 0; i < next.dmaSamples.size(); ++i)
            if (next.dmaSamples[i] != extras_.dmaSamples[i]) ++dmaVersion_[i];
        extras_ = std::move(next);
        driver_.publish(makeSnapshot());
    }
    extrasVersion_.fetch_add(1, std::memory_order_acq_rel);
    // The host marks the project as modified (renaming, kit, Digidrum: none of this is a parameter).
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

bool StymProcessor::armRecorder(int chip) {
    collectTake();  // a take finished but not collected yet would block the arming
    return driver_.armRecorder(chip);
}

void StymProcessor::collectTake() {
    std::unique_ptr<RegisterTake> t = driver_.takeRecording();
    if (!t || t->empty()) return;
    {
        const std::lock_guard<std::mutex> lock(controlLock_);
        take_ = std::move(*t);
    }
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

StymProcessor::TakeInfo StymProcessor::takeInfo() const {
    const std::lock_guard<std::mutex> lock(controlLock_);
    return {uint32_t(take_.frames.size()), take_.frameHz, take_.chip};
}

RegisterTake StymProcessor::recordedTake() const {
    const std::lock_guard<std::mutex> lock(controlLock_);
    return take_;
}

void StymProcessor::clearTake() {
    {
        const std::lock_guard<std::mutex> lock(controlLock_);
        if (take_.empty()) return;
        take_ = RegisterTake{};
    }
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

// Audio thread (Driver::process, for a prototype CC): the JUCE parameter takes the value at once, so that the next
// block reads it back; the host is told later, from the message thread (serviceMessageThread). A full queue drops
// the notification, not the change.
void StymProcessor::parameterChangedByMidi(int index, float plainValue) noexcept {
    auto* p = params_[size_t(index)];
    p->setValue(p->convertTo0to1(plainValue));
    notifyFifo_.write(1).forEach([&](int slot) { notifyBuffer_[size_t(slot)] = index; });
}

void StymProcessor::serviceMessageThread() {
    // Parameters changed by a CC: the host receives them as a user gesture.
    notifyFifo_.read(notifyFifo_.getNumReady()).forEach([&](int slot) {
        auto* p = params_[size_t(notifyBuffer_[size_t(slot)])];
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getValue());
        p->endChangeGesture();
    });
    // Under controlLock_: service() shares the driver's message-side state (last published snapshot, mailboxes) with
    // publish(), which a host thread can also reach (setStateInformation).
    {
        const std::lock_guard<std::mutex> lock(controlLock_);
        driver_.service(currentValues());
    }
    collectTake();
    const int latency = int(driver_.latencySamples());
    if (latency != getLatencySamples()) setLatencySamples(latency);
}

std::string StymProcessor::loadYm(std::span<const uint8_t> bytes, std::string fileName) {
    YmTuneLoad r = loadYmTune(bytes, std::move(fileName));
    if (!r.tune) return r.error;
    const std::lock_guard<std::mutex> lock(controlLock_);
    driver_.ymLoad(std::move(r.tune));
    return {};
}

void StymProcessor::ymPlay(bool on) {
    const std::lock_guard<std::mutex> lock(controlLock_);
    driver_.ymPlay(on);
}

void StymProcessor::ymRewind() {
    const std::lock_guard<std::mutex> lock(controlLock_);
    driver_.ymRewind();
}

void StymProcessor::ymSubsong(int subsong) {
    const std::lock_guard<std::mutex> lock(controlLock_);
    driver_.ymSubsong(subsong);
}

void StymProcessor::ymClose() {
    const std::lock_guard<std::mutex> lock(controlLock_);
    driver_.ymClose();
}

juce::AudioProcessorEditor* StymProcessor::createEditor() { return new StymEditor(*this); }

// Host state: the binary blob of plugincore/State.h (parameters by ID, non-automatable state, recorder take).
void StymProcessor::getStateInformation(juce::MemoryBlock& dest) {
    PluginState state;
    state.values = currentValues();
    {
        const std::lock_guard<std::mutex> lock(controlLock_);
        state.extras = extras_;
        state.take = take_;
    }
    const std::vector<uint8_t> bytes = encodeState(state);
    dest.replaceAll(bytes.data(), bytes.size());
}

void StymProcessor::applyExtras(const NonParamState& extras, const RegisterTake* take, bool hasSequencer) {
    {
        const std::lock_guard<std::mutex> lock(controlLock_);
        extras_ = extras;
        sanitize(extras_);
        if (take) take_ = *take;
        // State from before the sequencer: default sequencer settings for this format.
        if (!hasSequencer) extras_.global.seqClock = defaultSeqClock();
        // Every version moves: the audio thread rebuilds the kit, the samples and the pattern from the new state.
        ++kitVersion_;
        for (auto& v : sampleVersion_) ++v;
        for (auto& v : dmaVersion_) ++v;
        ++patternSerial_;  // the engine takes the state's current pattern again
        driver_.publish(makeSnapshot());
    }
    extrasVersion_.fetch_add(1, std::memory_order_acq_rel);
}

void StymProcessor::loadProject(const ParamValues& values, const NonParamState& extras) {
    for (int i = 0; i < kParamCount; ++i) {
        auto* p = params_[size_t(i)];
        const float want = p->convertTo0to1(clampToSpec(paramSpec(i), values[size_t(i)]));
        if (p->getValue() == want) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost(want);
        p->endChangeGesture();
    }
    applyExtras(extras, nullptr, true);
    // The host marks the project as modified (the non-automatable state changed).
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void StymProcessor::setStateInformation(const void* data, int size) {
    if (data == nullptr || size <= 0) return;
    const DecodeResult r = decodeState({static_cast<const uint8_t*>(data), size_t(size)});
    if (!r.ok) return;  // unreadable or newer state: the current state is kept
    for (int i = 0; i < kParamCount; ++i) {
        auto* p = params_[size_t(i)];
        p->setValueNotifyingHost(p->convertTo0to1(r.state.values[size_t(i)]));
    }
    applyExtras(r.state.extras, &r.state.take, r.hasSequencer);
    // CLAP: values changed by a state read back are announced by a rescan (CLAP_PARAM_RESCAN_VALUES), which
    // clap-juce-extensions sends for programChanged. The other formats follow setValueNotifyingHost; a programChanged
    // would wrongly announce a preset there (AU). Outside the lock: the host can call back into the plugin.
    if (is_clap) updateHostDisplay(ChangeDetails().withProgramChanged(true));
}

}  // namespace stym::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    auto* p = new stym::plugin::StymProcessor();
    // MIDI learn map from the user's preferences: applied from creation, with the editor closed.
    // The tests create the processor without it.
    p->setMidiMap(stym::plugin::StymEditor::userPreferences()->getValue("midiMap").toStdString());
    return p;
}
