// STYM2149 — plugin processor (M3), with JUCE: host parameters, buses, MIDI → audio, state, CC → host,
// configuration changes, independence from the block size, no allocation in processBlock; sequencer (M5): state,
// default clock, host transport, interface commands.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>
#include <vector>

#include "PluginProcessor.h"

using namespace stym;
using namespace stym::plugin;

// ThreadSanitizer brings its own operator new and delete (D-184): allocations are counted through its malloc hook,
// which sees every allocation (operator new included). Elsewhere, operator new is replaced.
#if defined(__SANITIZE_THREAD__)
#define STYM_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define STYM_TSAN 1
#endif
#endif

#ifdef STYM_TSAN
#include <sanitizer/allocator_interface.h>
#endif

namespace {

// Allocations counted on the thread that calls processBlock only: another (JUCE) thread can allocate meanwhile
// without the audio processing having anything to do with it (seen under TSan, D-184).
thread_local bool g_watch = false;
std::atomic<long> g_allocs{0};

}  // namespace

#ifdef STYM_TSAN
namespace {
void countAlloc(const volatile void*, size_t) {
    if (g_watch) g_allocs.fetch_add(1, std::memory_order_relaxed);
}
void ignoreFree(const volatile void*) {}
[[maybe_unused]] const int g_hooks = __sanitizer_install_malloc_and_free_hooks(countAlloc, ignoreFree);
}  // namespace
#else
void* operator new(std::size_t n) {
    if (g_watch) g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    if (g_watch) g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

namespace {

int idx(const char* id) {
    const int i = findParam(id);
    REQUIRE(i >= 0);
    return i;
}

void setPlain(StymProcessor& p, const char* id, float v) {
    auto& param = p.parameter(idx(id));
    param.setValueNotifyingHost(param.convertTo0to1(v));
}

struct Block {
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
};

// Renders `total` samples in blocks of `block`; the notes are placed at absolute positions.
std::vector<float> render(StymProcessor& p, int total, int block, const std::vector<std::pair<int, juce::MidiMessage>>& evs) {
    std::vector<float> out;
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    for (int at = 0; at < total; at += block) {
        midi.clear();
        for (const auto& [t, m] : evs)
            if (t >= at && t < at + block) midi.addEvent(m, t - at);
        buffer.setSize(p.getTotalNumOutputChannels(), block, false, false, true);
        p.processBlock(buffer, midi);
        out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + block);
    }
    return out;
}

double energy(const float* x, int n) {
    double e = 0;
    for (int i = 0; i < n; ++i) e += double(x[i]) * double(x[i]);
    return e;
}

// Test host playhead: playing at 120 BPM, position advanced by the test.
struct TestPlayHead final : juce::AudioPlayHead {
    bool playing = true;
    double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setIsPlaying(playing);
        info.setPpqPosition(ppq);
        info.setBpm(bpm);
        info.setTimeSignature(TimeSignature{3, 4});
        info.setPpqPositionOfLastBarStart(std::floor(ppq / 3) * 3);
        return info;
    }
};

// Pattern 1: one note every four steps on track 1.
void writePattern(StymProcessor& p) {
    p.editExtras([](NonParamState& s) {
        ymseq::Lane& l = s.seq.patterns[0].edit(0);
        for (int k = 0; k < 16; k += 4) l.pat[size_t(k)] = 0;
    });
}

struct CountingListener : juce::AudioProcessorListener {
    std::vector<int> changed;
    int gestures = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float) override { changed.push_back(index); }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++gestures; }
};

}  // namespace

TEST_CASE("Processor: host parameters built from the table", "[processor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    const auto& params = p.getParameters();
    REQUIRE(params.size() == kParamCount);
    for (int i = 0; i < kParamCount; ++i) {
        const auto* r = dynamic_cast<juce::RangedAudioParameter*>(params[i]);
        REQUIRE(r != nullptr);
        CHECK(r->getParameterID() == juce::String(paramSpec(i).id));
        CHECK(r->getName(64) == juce::String::fromUTF8(paramSpec(i).name.c_str()));
        CHECK(r->isAutomatable() == paramSpec(i).automatable);
    }
    CHECK(p.currentValues() == defaultValues());
    CHECK(p.getParameterTree().getSubgroups(false).size() == 1 + kTracks);
    // Envelope period: geometric scale, integer.
    setPlain(p, "t01_envPeriod", 400.4f);
    CHECK(p.currentValues()[size_t(idx("t01_envPeriod"))] == 400.0f);
    CHECK(p.parameter(idx("t01_envPeriod")).getCurrentValueAsText() == "400");
}

TEST_CASE("Processor: output buses", "[processor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    CHECK(p.getBusCount(false) == StymProcessor::kBusCount);
    CHECK(p.getBusCount(true) == 0);
    CHECK(p.getTotalNumOutputChannels() == 2);  // separate outputs disabled by default
    auto layout = p.getBusesLayout();
    layout.outputBuses.getReference(3) = juce::AudioChannelSet::mono();
    layout.outputBuses.getReference(StymProcessor::kChipBusFirst) = juce::AudioChannelSet::mono();
    CHECK(p.checkBusesLayoutSupported(layout));
    layout.outputBuses.getReference(3) = juce::AudioChannelSet::stereo();
    CHECK_FALSE(p.checkBusesLayoutSupported(layout));
    layout.outputBuses.getReference(3) = juce::AudioChannelSet::mono();
    layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
    CHECK_FALSE(p.checkBusesLayoutSupported(layout));
}

TEST_CASE("Processor: MIDI note → audio, separate output of the channel's track", "[processor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    REQUIRE(p.enableAllBuses());
    p.setRateAndBufferSizeDetails(48000, 512);
    setPlain(p, "midiRouting", 2);  // per channel
    p.prepareToPlay(48000, 512);
    CHECK(p.getLatencySamples() == int(p.driver().latencySamples()));
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    REQUIRE(buffer.getNumChannels() == 2 + kTracks + 2);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(3, 60, uint8_t(100)), 10);  // channel 3 → track 3
    double main = 0, t3 = 0, t1 = 0;
    for (int b = 0; b < 10; ++b) {
        p.processBlock(buffer, midi);
        midi.clear();
        main += energy(buffer.getReadPointer(0), 512);
        t1 += energy(buffer.getReadPointer(2), 512);
        t3 += energy(buffer.getReadPointer(2 + 2), 512);
    }
    CHECK(main > 1e-3);
    CHECK(t3 > 1e-3);
    CHECK(t1 < 1e-12);
}

TEST_CASE("Processor: host notes passed on to the editor, not those of the screen", "[processor][editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    p.setRateAndBufferSizeDetails(48000, 512);
    p.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(2, 60, uint8_t(100)), 10);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 10), 11);  // other CC: not passed on
    midi.addEvent(juce::MidiMessage::noteOff(2, 60), 20);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 123, 0), 30);
    p.pushScreenMidi(0x90, 48, 127);  // on-screen playing: the editor already knows it
    p.processBlock(buffer, midi);
    std::array<uint8_t, 3> m{};
    std::vector<std::array<uint8_t, 3>> got;
    while (p.popHostNote(m)) got.push_back(m);
    CHECK(got == std::vector<std::array<uint8_t, 3>>{{0x91, 60, 100}, {0x81, 60, 0}, {0xB0, 123, 0}});
    // Full queue (editor closed): the following ones are lost; the editor empties it when it opens.
    for (int b = 0; b < 100; ++b) {
        midi.clear();
        for (int k = 0; k < 4; ++k) midi.addEvent(juce::MidiMessage::noteOn(1, 40 + k, uint8_t(90)), k);
        p.processBlock(buffer, midi);
    }
    p.dropHostNotes();
    CHECK_FALSE(p.popHostNote(m));
}

TEST_CASE("Processor: a block without samples keeps the on-screen notes for the next block", "[processor][editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    p.setRateAndBufferSizeDetails(48000, 512);
    p.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> empty(p.getTotalNumOutputChannels(), 0), buffer(p.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    p.pushScreenMidi(0x90, 60, 100);
    p.processBlock(empty, midi);  // parameter flush of a VST3 host
    p.processBlock(buffer, midi);
    CHECK(p.driver().engine().activeVoices() > 0);
    CHECK(energy(buffer.getReadPointer(0), 512) > 1e-6);
}

TEST_CASE("Processor: blocks processed before prepareToPlay are silent", "[processor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    p.setRateAndBufferSizeDetails(48000, 512);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, uint8_t(100)), 0);
    double e = 0;
    // More than 1600 samples: the engine status of the editor is published, with no engine to read yet.
    for (int b = 0; b < 8; ++b) {
        p.processBlock(buffer, midi);
        e += energy(buffer.getReadPointer(0), 512);
    }
    CHECK(e == 0);
}

namespace {
// A host that reads the state as soon as it is told of the new latency, from the same call stack.
struct StateOnLatency : juce::AudioProcessorListener {
    int reads = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor* p, const ChangeDetails& d) override {
        if (!d.latencyChanged) return;
        juce::MemoryBlock block;
        p->getStateInformation(block);
        ++reads;
    }
};
}  // namespace

TEST_CASE("Processor: the host can call back into the plugin when told of the latency", "[processor][state]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    StateOnLatency host;
    p.addListener(&host);
    p.setRateAndBufferSizeDetails(48000, 512);
    p.prepareToPlay(48000, 512);  // used to lock controlLock_ a second time on the same thread (hang)
    p.removeListener(&host);
    CHECK(host.reads == 1);
    CHECK(p.getLatencySamples() == int(p.driver().latencySamples()));
}

TEST_CASE("Processor: state saved and read back, reference v1 file", "[processor][state]") {
    juce::ScopedJuceInitialiser_GUI juce;
    std::ifstream f(std::string(STYM_TEST_DATA) + "/state_v1.bin", std::ios::binary);
    const std::vector<uint8_t> golden{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    REQUIRE(!golden.empty());
    const DecodeResult ref = decodeState(golden);
    REQUIRE(ref.ok);

    StymProcessor p;
    p.setStateInformation(golden.data(), int(golden.size()));
    const ParamValues v = p.currentValues();
    for (int i = 0; i < kParamCount; ++i) {
        INFO(paramSpec(i).id);
        CHECK(std::abs(v[size_t(i)] - ref.state.values[size_t(i)]) <= 1e-4f * std::max(1.0f, std::abs(v[size_t(i)])));
    }
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    const DecodeResult back = decodeState({static_cast<const uint8_t*>(saved.getData()), saved.getSize()});
    REQUIRE(back.ok);
    CHECK(back.state.values == v);
    CHECK(back.state.extras.digidrums == ref.state.extras.digidrums);
    CHECK(back.state.extras.tracks[2].arp.table == ref.state.extras.tracks[2].arp.table);

    // A second processor reads the state of the first one back identically.
    StymProcessor q;
    q.setStateInformation(saved.getData(), int(saved.getSize()));
    CHECK(q.currentValues() == v);
    // Unreadable state: ignored.
    const char junk[] = "not a state";
    q.setStateInformation(junk, int(sizeof junk));
    CHECK(q.currentValues() == v);
}

TEST_CASE("Processor: state read back, value rescan requested from CLAP only", "[processor][state]") {
    juce::ScopedJuceInitialiser_GUI juce;
    struct Listener final : juce::AudioProcessorListener {
        int programChanges = 0;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& d) override {
            if (d.programChanged) ++programChanges;
        }
    };
    StymProcessor src;
    setPlain(src, "t01_level", 40.0f);
    juce::MemoryBlock saved;
    src.getStateInformation(saved);

    // clap-juce-extensions translates programChanged into CLAP_PARAM_RESCAN_VALUES (D-066); the other formats do not
    // receive it (the AU would see a preset in it).
    for (const bool clap : {false, true}) {
        INFO("CLAP: " << clap);
        StymProcessor p;
        p.is_clap = clap;
        Listener l;
        p.addListener(&l);
        p.setStateInformation(saved.getData(), int(saved.getSize()));
        CHECK(l.programChanges == (clap ? 1 : 0));
        CHECK(p.currentValues() == src.currentValues());
        // Unreadable state: nothing is read, nothing is announced.
        const char junk[] = "not a state";
        p.setStateInformation(junk, int(sizeof junk));
        CHECK(l.programChanges == (clap ? 1 : 0));
        p.removeListener(&l);
    }
}

TEST_CASE("Processor: a prototype CC changes the parameter and notifies the host", "[processor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    CountingListener listener;
    p.addListener(&listener);
    p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 127), 3);
    p.processBlock(buffer, midi);
    CHECK(p.currentValues()[size_t(idx("t01_envPeriod"))] == 32.0f);
    CHECK(listener.changed.empty());  // the host is notified by the message thread
    p.serviceMessageThread();
    REQUIRE(listener.changed.size() == 1);
    CHECK(listener.changed[0] == idx("t01_envPeriod"));
    CHECK(listener.gestures == 1);
    p.removeListener(&listener);
}

TEST_CASE("Processor: MIDI learn, learned CCs and notes caught for the editor (D-146)", "[processor][editor][learn]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 256);
    const float ep = p.currentValues()[size_t(idx("t01_envPeriod"))];
    // Map from the preferences: CC 74 learned (it no longer changes the envelope period), note 36 learned (does not
    // play).
    p.setMidiMap(R"({"CC74":{"id":"np","kind":"range"},"Note36":{"id":"kbLatch","kind":"btn"}})");
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 127), 3);
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(100)), 4);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 5);
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, uint8_t(100)), 6);
    p.processBlock(buffer, midi);
    CHECK(p.currentValues()[size_t(idx("t01_envPeriod"))] == ep);
    std::array<uint8_t, 3> m{};
    std::vector<std::array<uint8_t, 3>> learned, notes;
    while (p.popLearnMidi(m)) learned.push_back(m);
    while (p.popHostNote(m)) notes.push_back(m);
    CHECK(learned == std::vector<std::array<uint8_t, 3>>{{0xB0, 74, 127}, {0x90, 36, 100}, {0x80, 36, 0}});
    CHECK(notes == std::vector<std::array<uint8_t, 3>>{{0x90, 60, 100}});

    // Armed control: the first CC is taken for it (once); CC 123 keeps its meaning.
    p.setMidiCapture(MidiKeys{}, true);
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 123, 0), 1);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 72, 0), 2);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 72, 127), 3);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 127), 4);
    p.processBlock(buffer, midi);
    learned.clear();
    while (p.popLearnMidi(m)) learned.push_back(m);
    CHECK(learned == std::vector<std::array<uint8_t, 3>>{{0xB0, 72, 0}, {0xB0, 72, 127}});
    CHECK(p.currentValues()[size_t(idx("t01_envPeriod"))] == 32.0f);  // CC 74 is no longer learned: D-061

    // Messages caught with the editor closed: emptied when it opens.
    p.setMidiMap(R"({"CC1":{"id":"vol","kind":"range"}})");
    midi.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 5), 0);
    p.processBlock(buffer, midi);
    p.dropLearnMidi();
    CHECK_FALSE(p.popLearnMidi(m));
}

// A key learned while its note sounds (learn armed, or the shared map read again when a window opens): the note off
// goes to the editor and still releases the note.
TEST_CASE("Processor: MIDI learn, a note learned while it sounds is released by its note off", "[processor][learn]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    p.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, uint8_t(100)), 0);
    p.processBlock(buffer, midi);
    REQUIRE(p.driver().engine().activeVoices() > 0);
    MidiKeys keys;
    keys.notes.set(60);
    p.setMidiCapture(keys, false);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    p.processBlock(buffer, midi);
    std::array<uint8_t, 3> m{};
    REQUIRE(p.popLearnMidi(m));
    CHECK(m == std::array<uint8_t, 3>{0x80, 60, 0});
    for (int i = 0; i < 3 * 48000 / 256; ++i) {
        midi.clear();
        p.processBlock(buffer, midi);
    }
    CHECK(p.driver().engine().activeVoices() == 0);
}

TEST_CASE("Processor: clock changed during playback, real time and offline", "[processor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    const std::vector<std::pair<int, juce::MidiMessage>> none;
    render(p, 1024, 512, {{0, juce::MidiMessage::noteOn(1, 60, uint8_t(100))}});
    setPlain(p, "chipClock", 2);
    setPlain(p, "resampling", 1);
    render(p, 1024, 512, none);
    CHECK(p.driver().engineKey().clock == 0);  // preparation not done yet by the message thread
    p.serviceMessageThread();
    render(p, 2048, 512, none);
    CHECK(p.driver().engineKey().clock == 2);
    p.serviceMessageThread();
    CHECK(p.getLatencySamples() == 0);  // AtariAudio averaging: no delay

    p.setNonRealtime(true);
    setPlain(p, "chipClock", 3);
    render(p, 512, 512, none);
    CHECK(p.driver().engineKey().clock == 3);
}

TEST_CASE("Processor: render independent of the block size", "[processor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    const std::vector<std::pair<int, juce::MidiMessage>> evs = {
        {37, juce::MidiMessage::noteOn(1, 57, uint8_t(110))},
        {900, juce::MidiMessage::controllerEvent(1, 75, 100)},
        {900, juce::MidiMessage::noteOn(1, 64, uint8_t(90))},
        {2000, juce::MidiMessage::pitchWheel(1, 12000)},
        {3001, juce::MidiMessage::noteOff(1, 57)},
        {4100, juce::MidiMessage::controllerEvent(1, 1, 90)}};
    auto run = [&](int block) {
        StymProcessor p;
        p.prepareToPlay(48000, block);
        return render(p, 6144, block, evs);
    };
    CHECK(run(32) == run(512));
}

TEST_CASE("Processor: sequencer, default clock and state", "[processor][state][seq]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    CHECK(p.extras().global.seqClock == ymengine::SeqClock::Host);  // in a host (D-114)
    writePattern(p);
    p.editExtras([](NonParamState& s) {
        s.seq.current = 5;
        s.seq.used.set(5);
        s.seq.song.entries = {{0, 2, 0}, {5, 1, 140}};
        s.global.metronome = true;
    });
    setPlain(p, "swing", 60);
    setPlain(p, "t01_gate", 25);
    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    StymProcessor q;
    q.setStateInformation(saved.getData(), int(saved.getSize()));
    const NonParamState x = q.extras();
    CHECK(x.seq.current == 5);
    CHECK(x.seq.patterns[0].lane(0).pat[4] == 0);
    CHECK(x.seq.song.entries.size() == 2);
    CHECK(x.global.metronome);
    CHECK(q.currentValues()[size_t(idx("t01_gate"))] == 25.0f);
    CHECK(q.currentValues()[size_t(idx("swing"))] == 60.0f);
    // The engine takes the current pattern of the state read back.
    q.prepareToPlay(48000, 512);
    render(q, 512, 512, {});
    CHECK(q.driver().engine().seqStatus().pattern == 5);
    // M3 state (without a sequencer): default clock of the format.
    std::ifstream f(std::string(STYM_TEST_DATA) + "/state_v1.bin", std::ios::binary);
    const std::vector<uint8_t> m3{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    q.setStateInformation(m3.data(), int(m3.size()));
    CHECK(q.extras().global.seqClock == ymengine::SeqClock::Host);
}

TEST_CASE("Processor: Host sync on the playhead, commands in Free run", "[processor][seq]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    writePattern(p);
    TestPlayHead head;
    p.setPlayHead(&head);
    p.prepareToPlay(48000, 480);
    juce::AudioBuffer<float> buffer(2, 480);
    juce::MidiBuffer midi;
    double e = 0;
    for (int b = 0; b < 100; ++b) {  // one second: two quarter notes
        p.processBlock(buffer, midi);
        e += energy(buffer.getReadPointer(0), 480);
        head.ppq += 480 * 120.0 / 60 / 48000;
    }
    CHECK(e > 1e-3);
    auto st = p.driver().engine().seqStatus();
    CHECK(st.playing);
    CHECK(st.stepAbs == 7);
    CHECK(st.bpm == 120);
    head.playing = false;
    p.processBlock(buffer, midi);
    CHECK_FALSE(p.driver().engine().seqStatus().playing);
    // Free run: Play and Stop from the interface.
    p.editExtras([](NonParamState& s) { s.global.seqClock = ymengine::SeqClock::Internal; });
    p.pushSeqCommand(UiCommand::SeqPlay);
    p.processBlock(buffer, midi);
    CHECK(p.driver().engine().seqStatus().playing);
    p.pushSeqCommand(UiCommand::SeqStop);
    p.processBlock(buffer, midi);
    CHECK_FALSE(p.driver().engine().seqStatus().playing);
    p.setPlayHead(nullptr);
}

TEST_CASE("Processor: the metronome sounds, but not on the oscilloscope or the output meters (third DAW test, item "
          "27, D-226)",
          "[processor][seq]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    p.editExtras([](NonParamState& s) {
        s.global.seqClock = ymengine::SeqClock::Internal;
        s.global.metronome = true;  // empty patterns: only the metronome sounds
    });
    p.prepareToPlay(48000, 480);
    juce::AudioBuffer<float> buffer(2, 480);
    juce::MidiBuffer midi;
    p.pushSeqCommand(UiCommand::SeqPlay);
    double e = 0;
    float scopePeak = 0, outPeak = 0;
    for (int b = 0; b < 100; ++b) {  // one second: two beats at 120 BPM
        p.processBlock(buffer, midi);
        e += energy(buffer.getReadPointer(0), 480);
        if (p.updateEngineStatus()) {
            for (float v : p.engineStatus().scope) scopePeak = std::max(scopePeak, std::abs(v));
            outPeak = std::max({outPeak, p.engineStatus().outPeak[0], p.engineStatus().outPeak[1]});
        }
    }
    CHECK(p.driver().engine().seqStatus().playing);
    CHECK(e > 1e-3);
    CHECK(scopePeak == 0.0f);
    CHECK(outPeak == 0.0f);
}

TEST_CASE("Processor: no allocation in processBlock", "[processor][realtime]") {
    juce::ScopedJuceInitialiser_GUI juce;
    StymProcessor p;
    REQUIRE(p.enableAllBuses());
    writePattern(p);
    TestPlayHead head;
    p.setPlayHead(&head);
    p.prepareToPlay(44100, 1024);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 1024);
    juce::MidiBuffer midi;
    midi.ensureSize(4096);
    uint32_t rng = 99;
    auto next = [&] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    long allocs = 0;
    for (int b = 0; b < 400; ++b) {
        const int n = 16 + int(next() % 1009);
        midi.clear();
        for (int k = 0; k < 6; ++k) {
            const int at = int(next() % uint32_t(n));
            const int ch = 1 + int(next() % 16);
            switch (next() % 4) {
            case 0: midi.addEvent(juce::MidiMessage::noteOn(ch, 36 + int(next() % 48), uint8_t(1 + next() % 127)), at); break;
            case 1: midi.addEvent(juce::MidiMessage::noteOff(ch, 36 + int(next() % 48)), at); break;
            case 2: midi.addEvent(juce::MidiMessage::controllerEvent(ch, 70 + int(next() % 15), int(next() % 128)), at); break;
            default: midi.addEvent(juce::MidiMessage::pitchWheel(ch, int(next() % 16384)), at); break;
            }
        }
        if (b % 50 == 0) setPlain(p, "voiceMode", float(next() % 3));
        if (b % 7 == 0) setPlain(p, "t01_level", float(next() % 101));
        // On-screen playing (editor, M4): read by processBlock, like the engine status it publishes.
        if (b % 5 == 0) p.pushScreenMidi(0x90, uint8_t(48 + next() % 24), 100);
        if (b % 5 == 2) p.pushScreenMidi(0xB0, 120, 0);
        // Sequencer (M5): Host sync, host jump, then Free run with the interface commands.
        if (b == 150) head.ppq = 0.3;
        if (b == 200) p.editExtras([](NonParamState& s) { s.global.seqClock = ymengine::SeqClock::Internal; });
        if (b % 40 == 1) p.pushSeqCommand(UiCommand::SeqPlay);
        if (b % 40 == 21) p.pushSeqCommand(UiCommand::SeqSelect, uint8_t(next() % 4));
        if (b % 40 == 31) p.pushSeqCommand(UiCommand::SeqPreview, 0, uint8_t(48 + next() % 24), 100);
        if (b % 80 == 39) p.pushSeqCommand(UiCommand::SeqStop);
        // M6: extended Song (Rest, Note-off, jump) in Free run; recorder takes armed and collected (the mode change
        // every 50 blocks stops them).
        if (b == 210)
            p.editExtras([](NonParamState& s) {
                s.global.songMode = true;
                s.seq.song.entries = {{0, 1, 0}, ymseq::SongEntry::rest(ymseq::SongKind::Rest, 2),
                                      ymseq::SongEntry::rest(ymseq::SongKind::NoteOff, 1), ymseq::SongEntry::loop(2, 1)};
            });
        if (b % 25 == 3) p.armRecorder(int(next() % 2));
        buffer.setSize(buffer.getNumChannels(), n, false, false, true);
        g_allocs = 0;
        g_watch = true;
        p.processBlock(buffer, midi);
        g_watch = false;
        allocs += g_allocs;
        head.ppq += n * 120.0 / 60 / 44100;
        if (b % 3 == 0) p.serviceMessageThread();
    }
    CHECK(allocs == 0);
    CHECK(p.takeInfo().frames > 0);  // the recorder did run
    CHECK(p.updateEngineStatus());
    p.setPlayHead(nullptr);
}
