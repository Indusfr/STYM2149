// STYM2149 — plugincore: engine driver for the plugin (audio thread), without JUCE
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Receives at each block the parameter values, the MIDI events and the outputs, and drives the engine:
// - parameters: compared with the last applied ones, applied at the start of the block (track sound through
//   setTrack, mixer through setTrackMix, STE bass and treble through setBoardTone);
// - MIDI: Omni / Layer / per-channel routing, controllers of the prototype (CC 70-84, 7), mod wheel, pitch bend,
//   MIDI clock (D-061; Start, Stop, Continue and Song Position Pointer for the sequencer). A CC that changes a
//   parameter cuts the render at its sample and notifies the host. The DMA tracks (M6, D-153) follow the same
//   routing as the other tracks (their dedicated MIDI channel was dropped, D-153);
// - sequencer: commands from the interface (Play, Stop, pattern choice, preview of a step, kit audition), host
//   transport (Host sync) given at each render segment, patterns and Song published with the non-automatable
//   settings;
// - voice mode change: panic fade, then the new mode (the voices do not survive the change);
// - clock, chain or resampling change: the engine must be prepared again (allocations). The audio thread requests
//   it, the message thread prepares it (service()), the audio thread fades then switches over. Outside real time
//   (offline render), the preparation is done at once, at the same sample on each render (D-064);
// - non-automatable settings: frozen copies published by the message thread (publish());
// - register recorder (M6, D-157): take armed by the message thread, started and stopped by the audio thread at the
//   start of a block according to playback, handed back to the message thread;
// - YM Player (M7, A2, D-169): playback prepared by the message thread (YmPlayback), played by the audio thread
//   instead of the engine output, which keeps running without being heard.
// Audio thread: process() does not allocate, lock or free anything (Mailbox), except outside real time.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "plugincore/Mailbox.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "plugincore/Recorder.h"
#include "plugincore/State.h"
#include "plugincore/YmTune.h"
#include "ymengine/Engine.h"

namespace stym::plugin {

// Commands from the interface to the sequencer, played in their place among the events (data: see each command).
enum class UiCommand : uint8_t {
    None,
    SeqPlay,     // Play (no effect in Host sync)
    SeqStop,     // Stop (no effect in Host sync)
    SeqSelect,   // data[0]: pattern (when stopped at once, when playing at the end of the loop)
    SeqPreview,  // preview of a grid note: data[0] track, data[1] note, data[2] accent (0-127)
    SeqSongJump, // jump to a Song entry: data[0] | data[1] << 8
    DrumPreview, // audition of an instrument of the Chipdrum kit (Audition): data[0] slot (0-7)
    DmaPreview,  // preview of a slot of the DMA bank (M6, D-153): data[0] slot (0-7), data[1] 1: stop (D-449)
    DigiPreview, // preview of a Digidrum slot (editor, M6, D-154): data[0] slot (0-7), data[1] 1: stop (D-449)
};

// One event of a block, given to process() in time order: a host MIDI message, a note or CC from the editor
// (`screen`), or an interface command (`command`). The processor builds them on the audio thread.
struct MidiEvent {
    uint32_t offset = 0;  // sample in the block
    uint8_t size = 0;     // 1 to 3 bytes (longer messages, SysEx included, are ignored)
    std::array<uint8_t, 3> data{};
    // Event from the interface (on-screen keyboard and pads): the notes go to the track played from the keyboard
    // (focused track, or all active tracks in Layer) whatever the MIDI routing; CC 120 cuts manual play (manOff of
    // the prototype, generator change), CC 123 is the panic (Esc).
    bool screen = false;
    UiCommand command = UiCommand::None;  // interface command (data: its arguments), otherwise a MIDI message
};

// Non-automatable settings seen by the audio thread: frozen copy and versions (a number changes when the matching
// part changes, so that only what is needed is redone).
struct Snapshot {
    NonParamState state;
    uint64_t kitVersion = 0;
    std::array<uint64_t, ymengine::kDigidrumSlots> sampleVersion{};
    std::array<uint64_t, ymengine::kDmaSlots> dmaVersion{};  // samples of the DMA bank (D-153)
    // Patterns and Song played (fillSequence of state.seq); the engine reads them while this copy is the current one.
    ymengine::SeqData seq;
    // Changes when the engine must take state.seq.current as its pattern (state reloaded, preparation).
    uint64_t patternSerial = 0;
    // Instruments played (M6): filled by the driver from state.instruments when it receives the copy.
    ymengine::InstrumentBank instruments{};
};

// Parameter changes coming from MIDI (CCs of the prototype), to pass on to the host. Called on the audio thread.
class ParamListener {
public:
    virtual ~ParamListener() = default;
    virtual void parameterChangedByMidi(int index, float plainValue) noexcept = 0;
};

// One per plugin instance, owned by the processor; OfflineRender owns a second one for the exports. Each function
// below says on which thread it runs.
class Driver {
public:
    // Settings of prepare(): host rate (Hz) and separate outputs.
    struct Setup {
        uint32_t sampleRate = 48000;
        uint16_t trackOutputs = 0;  // separate outputs prepared (bit n = track n)
    };

    Driver();
    ~Driver();
    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;

    // Before prepare: sequencer clock forced, whatever the Clock selector (offline render: Free run, with no MIDI
    // clock to follow, D-188).
    void fixSeqClock(ymengine::SeqClock clock) noexcept { fixedSeqClock_ = clock; }
    // Message thread, audio thread stopped: prepares the engine (allocations) and applies all the settings.
    void prepare(const Setup& setup, const ParamValues& values, const Snapshot& snapshot);
    // Message thread: prepare() has run. The engine slot itself belongs to the audio thread, which replaces it on
    // an engine swap, so this flag is read instead.
    bool prepared() const noexcept { return prepared_.load(std::memory_order_acquire); }
    // Message thread: new copy of the non-automatable settings, taken at the start of a coming block.
    void publish(const Snapshot& snapshot);
    // Audio thread, before process(): host transport at the start of the block (Host sync).
    void setHostTransport(const ymengine::Engine::HostTransport& t) noexcept { host_ = t; }
    // Message thread, regularly: prepares the engine requested by the audio thread and destroys the returned objects.
    void service(const ParamValues& values);
    // Any thread: resampling delay of the current engine, to report to the host.
    uint32_t latencySamples() const noexcept { return latency_.load(std::memory_order_relaxed); }

    // Audio thread. `values` is updated by the CCs that change a parameter (listener notified). `offline`: render
    // outside real time (AudioProcessor::isNonRealtime).
    void process(ParamValues& values, std::span<const MidiEvent> midi, const ymengine::Outputs& out, uint32_t n,
                 bool offline, ParamListener* listener) noexcept;

    // Register recorder (M6, B11, D-157). Message thread: arms a take on chip `chip` (0, or 1 in dual chip; buffer of
    // kRecorderMaxFrames frames allocated here); false if a take is already armed, running or not yet collected. The
    // audio thread starts it at the start of the first block where the host plays (or the sequencer, without host
    // transport), in 3 channels or in dual chip with this chip, and stops it at the start of the block where playback
    // stopped, when the buffer is full, when the mode or the engine changes, or on disarmRecorder(): one take per
    // arming.
    bool armRecorder(int chip);
    // Message thread: a take not yet seen by the audio thread is dropped at once; otherwise the audio thread stops it
    // at its next block and hands it back (takeRecording).
    void disarmRecorder();
    // Message thread: finished take, nullptr otherwise (disarmed before starting: without frames).
    std::unique_ptr<RegisterTake> takeRecording();
    // Armed: waiting for playback to start; Rolling: frames being captured.
    enum class RecorderState : uint8_t { Idle, Armed, Rolling };
    // Any thread: recorder state, chip and frames of the armed or running take.
    RecorderState recorderState() const noexcept { return RecorderState(recState_.load(std::memory_order_acquire)); }
    int recorderChip() const noexcept { return recChip_.load(std::memory_order_relaxed); }
    uint32_t recordedFrames() const noexcept { return recFrames_.load(std::memory_order_relaxed); }

    // YM Player (M7, A2, D-169). Message thread: tune loaded (stopped, at the default subsong), play and stop
    // (playback resumes where it stopped), back to the start with playback (Rewind), subsong of an SNDH (back to its
    // start), close. Like the prototype (ymplay), each start of playback releases manual play and the sequencer
    // notes; during playback, the main output is the tune's (separate outputs silent). A change of chain, resampling
    // or rate prepares a new chip that resumes playback where it is.
    void ymLoad(std::shared_ptr<const YmTune> tune);
    void ymPlay(bool on);
    void ymRewind();
    void ymSubsong(int subsong);
    void ymClose();
    // Message thread: tune loaded (nullptr: none) and subsong.
    std::shared_ptr<const YmTune> ymTune() const noexcept { return ymTune_; }
    int ymSubsong() const noexcept { return ymSubsong_; }
    // Any thread: playback running (also false after a crashed SNDH routine stopped), position, registers.
    bool ymPlaying() const noexcept { return ymOn_.load(std::memory_order_acquire); }
    // Snapshot of the player for the YM Player window, read from atomics.
    struct YmStatus {
        uint32_t position = 0;  // frame in the tune (0 = first)
        uint64_t frames = 0;    // frames played since the start
        std::array<uint8_t, 14> regs{};  // R0 to R13 of the player's chip at the end of the last block
        bool failed = false;    // SNDH routine crashed
    };
    YmStatus ymStatus() const noexcept;

    // Observation (tests).
    const ymengine::Engine& engine() const noexcept { return slot_->engine; }
    ymengine::Engine& engine() noexcept { return slot_->engine; }
    ConfigKey engineKey() const noexcept { return slot_->key; }
    bool fading() const noexcept { return action_ != Action::None; }
    // Length of the fade before a mode change or an engine swap, in samples (D-064).
    uint32_t fadeSamples() const noexcept;
    uint64_t engineSwaps() const noexcept { return swaps_; }
    uint64_t droppedMidi() const noexcept { return droppedMidi_; }

private:
    // An engine, the configuration it was prepared for, and the versions of the kit, samples and pattern selection
    // already given to it: compared with those of the Snapshot so that only what changed is applied again.
    struct EngineSlot {
        ymengine::Engine engine;
        ConfigKey key;
        uint64_t kitVersion = 0;
        std::array<uint64_t, ymengine::kDigidrumSlots> sampleVersion{};
        std::array<uint64_t, ymengine::kDmaSlots> dmaVersion{};
        uint64_t patternSerial = 0;
    };
    // Transition in progress: a fade of fadeLeft_ samples, after which the new mode or machine is applied
    // (ModeChange) or the prepared engine incoming_ takes the place of the current one (EngineSwap).
    enum class Action : uint8_t { None, ModeChange, EngineSwap };

    std::unique_ptr<EngineSlot> build(const ConfigKey& key, const ParamValues& values, const Snapshot& snap) const;
    static std::unique_ptr<Snapshot> copyOf(const Snapshot& snapshot);
    void takeSnapshot() noexcept;
    void applySnapshotData() noexcept;
    void requestConfig(const ParamValues& values, bool offline) noexcept;
    void sync(const ParamValues& values) noexcept;
    void applyAll(const ParamValues& values) noexcept;
    void startAction(Action a) noexcept;
    void finishAction(const ParamValues& values) noexcept;
    void renderTo(const ymengine::Outputs& out, uint32_t to) noexcept;
    void advance(const ParamValues& values, const ymengine::Outputs& out, uint32_t to) noexcept;
    void finishDueActions(const ParamValues& values, const ymengine::Outputs& out, uint32_t at) noexcept;
    void midi(ParamValues& values, const MidiEvent& e, uint32_t at, const ymengine::Outputs& out,
              ParamListener* listener) noexcept;
    bool controlChange(ParamValues& values, int cc, int v, int channel, uint32_t at, const ymengine::Outputs& out,
                       ParamListener* listener) noexcept;
    void setParam(ParamValues& values, int index, float v, ParamListener* listener) noexcept;
    void queue(uint32_t at, uint8_t type, int track, int a, int b, double value) noexcept;
    void replayControllers() noexcept;
    ymengine::GlobalParams globalFor(const ParamValues& values) const noexcept;
    void carrySequencer(const ymengine::Engine::SeqStatus& seq) noexcept;
    void runRecorder(const ParamValues& values) noexcept;
    void finishTake() noexcept;
    void postYm(bool continues);
    void takeYm() noexcept;
    void renderYm(const ParamValues& values, const ymengine::Outputs& out, uint32_t n) noexcept;

    Setup setup_;
    // Audio thread.
    std::unique_ptr<EngineSlot> slot_;
    EngineSlot* incoming_ = nullptr;     // prepared engine, waiting to be switched in
    Snapshot* snapshot_ = nullptr;       // current copy (handed back to the message thread when replaced)
    ParamValues applied_{};              // values last applied to the engine; sync() applies the differences
    bool appliedValid_ = false;          // false: sync() applies everything again (new engine, new mode)
    bool extrasDirty_ = false;           // new snapshot: global and track settings applied again
    ymengine::VoiceMode appliedMode_ = ymengine::VoiceMode::Studio16;
    bool appliedSte_ = false;
    std::array<ymengine::Generator, kTracks> appliedGen_{};  // generator of each track; a change releases manual play
    Action action_ = Action::None;
    uint32_t fadeLeft_ = 0;              // samples left in the fade of action_
    uint32_t pos_ = 0;                   // start of the current render segment in the block
    ConfigKey wantKey_{};                // configuration wanted by the parameters
    uint32_t requestSerial_ = 0;         // + 1 each time wantKey_ changes (high half of request_)
    std::array<double, kTracks> bend_{}, wheel_{}, press_{};  // to replay on a new engine
    std::array<uint8_t, kTracks> program_{};        // Program Change of each track (D-156), replayed likewise
    uint64_t swaps_ = 0;
    // Render outside real time in this block, and the value the global settings were last applied with (the
    // metronome depends on it, globalFor).
    bool offline_ = false, appliedOffline_ = false;
    std::optional<ymengine::SeqClock> fixedSeqClock_;
    ymengine::Engine::HostTransport host_{};
    // MIDI events waiting for the render that contains them (absolute positions in the block): a CC that changes a
    // parameter cuts the render at its position, the events at that position go into the next segment.
    struct Queued {
        uint32_t at;
        uint8_t type;
        int8_t track;
        uint8_t a, b;
        double value;
    };
    static constexpr uint32_t kQueue = 2048;
    std::array<Queued, kQueue> queue_{};
    uint32_t qHead_ = 0, qTail_ = 0;  // next event to play, end of the queue; both reset at each block
    uint64_t droppedMidi_ = 0;        // events lost because a block queued more than kQueue

    // Register recorder: buffer of a take (frames of the maximum size, header of the take).
    struct RecBuffer {
        std::vector<ymengine::RegisterFrame> frames;
        RegisterTake take;  // header, without frames
        uint32_t count = 0;  // frames captured
    };
    RecBuffer* rec_ = nullptr;  // audio thread: take armed or running
    bool rolling_ = false;      // rec_ is capturing

    // Between the threads.
    std::atomic<RecBuffer*> recIn_{nullptr};   // armed take (message → audio)
    std::atomic<RecBuffer*> recOut_{nullptr};  // finished take (audio → message)
    std::atomic<bool> recStop_{false};         // stop asked by disarmRecorder, seen at the next block
    std::atomic<uint8_t> recState_{0};         // RecorderState
    std::atomic<int> recChip_{0};
    std::atomic<uint32_t> recFrames_{0};
    Mailbox<Snapshot> snapshots_;
    // YM Player: current playback (null playback: no tune), replaced by the message thread.
    struct YmSlot {
        std::unique_ptr<YmPlayback> playback;
        bool continues = false;  // resumes the previous playback (output settings changed)
        uint32_t serial = 0;     // number of the playback (loading, back to the start, subsong)
    };
    YmSlot* ymSlot_ = nullptr;  // audio thread
    uint32_t ymPlaySeen_ = 0;   // last ymPlaySerial_ handled by the audio thread
    int ymBass_ = INT32_MIN, ymTreble_ = INT32_MIN;  // STE tone given to the player's chip (INT32_MIN: not yet)
    Mailbox<YmSlot> ymBox_;
    std::atomic<bool> ymOn_{false};
    std::atomic<uint32_t> ymPlaySerial_{0};  // + 1 at each start of playback
    std::atomic<uint32_t> ymPosition_{0};
    // Frames played, then registers R0-R7 and R8-R13 packed one byte each, so that ymStatus() reads them lock-free.
    std::atomic<uint64_t> ymFrames_{0}, ymRegsLo_{0}, ymRegsHi_{0};
    std::atomic<bool> ymFailed_{false};
    // Message thread.
    std::shared_ptr<const YmTune> ymTune_;
    int ymSubsong_ = 1;
    std::shared_ptr<ymio::SndhStream> ymStream_;
    ConfigKey ymKey_{};    // configuration and host rate the current playback was built with (compared by service())
    uint32_t ymRate_ = 0;
    uint32_t ymSerial_ = 0;  // number of the last playback posted
    ymcore::RenderConfig ymRender_{};  // output settings of the project (engineConfig), reread by service()
    ConfigKey ymRenderKey_{};
    Mailbox<EngineSlot> built_;          // prepared engines (message → audio), handed back when replaced
    std::atomic<uint64_t> request_{0};   // serial << 32 | active << 29 | key
    std::atomic<uint32_t> latency_{0};
    std::atomic<bool> prepared_{false};
    // Message thread.
    std::unique_ptr<Snapshot> latest_;  // last published settings, from which service() builds new engines
    uint64_t builtFor_ = 0;             // request_ word already answered by service()
};

}  // namespace stym::plugin
