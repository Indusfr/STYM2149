// STYM2149 — plugin: JUCE audio processor (MIDI instrument)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// JUCE wrapper of the JUCE-free core (plugincore): host parameters built from the table, output buses, MIDI, state.
// Threads:
// - audio: processBlock reads the parameters (atomics) and the host transport (Host sync), converts the MIDI and
//   calls Driver::process;
// - message (timer): notifies the host of parameters changed by a CC, prepares the requested engines, announces the
//   latency;
// - any other thread (state, preparation): under controlLock_, which the audio thread never takes.
// The editor (message thread) reads and changes the non-automatable state under controlLock_, sends the screen's
// notes through a lock-free queue and reads the engine status that the audio thread publishes (triple buffer).
// The host can create and destroy the plugin outside the JUCE message thread (LV2 on Linux: the host's thread, a
// message thread of the plugin's own): the timer is a separate object, detached on destruction (ServiceTimer).
// clap_properties (clap-juce-extensions) says whether the instance is a CLAP: after a state is read back, only the
// CLAP must ask the host to read the parameter values again (D-066).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <clap-juce-extensions/clap-juce-extensions.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

#include "plugincore/Driver.h"
#include "plugincore/MidiLearn.h"
#include "plugincore/EngineStatus.h"
#include "pluginui/Session.h"

namespace stym::plugin {

class ServiceTimer;

class StymProcessor final : public juce::AudioProcessor,
                            public clap_juce_extensions::clap_properties,
                            private ParamListener {
public:
    // Output buses: main stereo, then one mono output per track and per chip, disabled by default (D-062). Bus
    // indices: 0 main, 1-16 tracks, 17-18 chips.
    static constexpr int kTrackBusFirst = 1;
    static constexpr int kChipBusFirst = kTrackBusFirst + kTracks;
    static constexpr int kBusCount = kChipBusFirst + 2;

    StymProcessor();
    ~StymProcessor() override;

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    // Plain values of all the parameters (any thread).
    ParamValues currentValues() const;
    // Work of the message thread (called by the timer; public for the tests).
    void serviceMessageThread();
    juce::RangedAudioParameter& parameter(int index) const { return *params_[size_t(index)]; }
    const Driver& driver() const { return driver_; }

    // Editor, message thread.
    // Non-automatable state: version number (changes on every modification, state read back included), copy,
    // modification (brought back into the ranges, then published to the audio thread).
    uint64_t extrasVersion() const noexcept { return extrasVersion_.load(std::memory_order_acquire); }
    NonParamState extras() const;
    void editExtras(const std::function<void(NonParamState&)>& edit);
    // On-screen playing (MidiEvent::screen), played at the start of the next block. A single writer (the editor).
    void pushScreenMidi(uint8_t status, uint8_t data1, uint8_t data2) noexcept;
    // Sequencer command (Play, Stop, pattern, preview of a step), through the same queue as on-screen playing.
    void pushSeqCommand(UiCommand command, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0) noexcept;
    // Note on and off received from the host (not those of the screen), CC 120 and 123 included, for the editor:
    // false when none is left. A single reader. Full queue (editor closed): the following ones are lost; the editor
    // empties it when it opens.
    bool popHostNote(std::array<uint8_t, 3>& message) noexcept;
    void dropHostNotes() noexcept;
    // MIDI learn (D-146): map from the preferences (JSON text of midiMap), read when the plugin is created so that
    // the learned CCs and notes are caught even with the editor closed; keys and armed control given by the editor;
    // caught messages, for the editor (a single reader, full queue: the following ones are lost, the editor empties it
    // when it opens).
    void setMidiMap(std::string_view json);
    void setMidiCapture(const MidiKeys& keys, bool armed) noexcept { capture_.set(keys, armed); }
    void disarmMidiLearn() noexcept { capture_.disarm(); }
    bool popLearnMidi(std::array<uint8_t, 3>& message) noexcept;
    void dropLearnMidi() noexcept;
    // Register recorder (D-157), editor: arming on a chip, stop (Driver), take kept with the project (header and
    // number of frames), clearing. A take finished by the audio thread is collected by the message thread's timer; a
    // take without frames does not replace the previous one.
    bool armRecorder(int chip);
    void disarmRecorder() { driver_.disarmRecorder(); }
    // Summary of the kept take for the Recorder window: number of frames, replay tick rate (Hz) and chip (0 or 1).
    struct TakeInfo {
        uint32_t frames = 0, frameHz = 50;
        int chip = 0;
    };
    TakeInfo takeInfo() const;
    RegisterTake recordedTake() const;  // copy of the kept take (export, B3)
    void clearTake();
    // YM Player (M7, A2, D-169), message thread: file read then loaded, stopped (alert of the refusal, empty if it is
    // loaded), playback, back to the start, subsong, closing.
    std::string loadYm(std::span<const uint8_t> bytes, std::string fileName);
    void ymPlay(bool on);
    void ymRewind();
    void ymSubsong(int subsong);
    void ymClose();
    // Whole project replaced from the editor (File > Load > Preset, M7): parameters changed by gestures,
    // non-automatable state taken like a state read back; the recorder's take is kept.
    void loadProject(const ParamValues& values, const NonParamState& extras);
    // Default sequencer clock: Host sync in a host, Free run in Standalone (D-114).
    ymengine::SeqClock defaultSeqClock() const noexcept;
    // Engine status: true if a new one arrived since the previous call (read in engineStatus()). One reader.
    bool updateEngineStatus() noexcept { return engineStatus_.update(); }
    const EngineStatus& engineStatus() const noexcept { return engineStatus_.latest(); }
    // Editor state (tools, view, undo, open windows): survives the closing of the plugin window, is not saved with
    // the project (pluginui/Session.h).
    pluginui::EditorSession& editorSession() noexcept { return session_; }

private:
    static BusesProperties makeBuses();
    void parameterChangedByMidi(int index, float plainValue) noexcept override;
    Snapshot makeSnapshot() const;  // under controlLock_
    // Non-automatable state read back (host state, preset): everything is rebuilt, the engine takes the current
    // pattern again; take: the recorder's take read back (nullptr: kept).
    void applyExtras(const NonParamState& extras, const RegisterTake* take, bool hasSequencer);
    void collectTake();             // message thread
    uint16_t enabledTrackOutputs() const;

    std::array<juce::RangedAudioParameter*, kParamCount> params_{};
    Driver driver_;
    std::shared_ptr<ServiceTimer> serviceTimer_;

    // Audio thread.
    ParamValues values_{};      // plain values of the block, read from the host parameters at its start
    std::vector<MidiEvent> midi_;  // events of the block: 4096 reserved by the constructor, never grown
    uint16_t preparedOutputs_ = 0;  // track outputs enabled at prepareToPlay (bit t = track t); the others stay silent
    std::vector<float> click_;  // metronome of the block (D-226), as long as the longest block announced

    // On-screen playing, from the editor to the audio thread.
    juce::AbstractFifo screenFifo_{256};
    std::array<std::array<uint8_t, 4>, 256> screenBuffer_{};  // command (UiCommand), then 3 bytes
    EngineStatusTap engineStatus_;
    // Host notes, from the audio thread to the editor.
    juce::AbstractFifo hostNoteFifo_{256};
    std::array<std::array<uint8_t, 3>, 256> hostNoteBuffer_{};
    // MIDI learn: messages caught for the editor (neither engine nor host notes).
    MidiCapture capture_;
    juce::AbstractFifo learnFifo_{256};
    std::array<std::array<uint8_t, 3>, 256> learnBuffer_{};

    // Parameters changed by a CC, to pass on to the host through the message thread.
    juce::AbstractFifo notifyFifo_{1024};
    std::array<int, 1024> notifyBuffer_{};  // parameter indices

    // Non-automatable settings (state) and their versions, under controlLock_. The versions travel in each Snapshot:
    // the audio thread redoes the costly parts (Chipdrum kit, Digidrum and DMA samples, current pattern) only when
    // theirs changes. extrasVersion_ tells the editor, without the lock, that the state changed.
    mutable std::mutex controlLock_;
    NonParamState extras_;
    uint64_t kitVersion_ = 1;
    uint64_t patternSerial_ = 1;  // the engine takes the state's current pattern (preparation, state read back)
    std::array<uint64_t, ymengine::kDigidrumSlots> sampleVersion_{};
    std::array<uint64_t, ymengine::kDmaSlots> dmaVersion_{};
    std::atomic<uint64_t> extrasVersion_{1};
    RegisterTake take_;  // last take of the recorder (D-157), under controlLock_

    pluginui::EditorSession session_;  // message thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StymProcessor)
};

}  // namespace stym::plugin
