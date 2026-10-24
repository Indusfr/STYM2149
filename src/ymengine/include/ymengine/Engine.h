// STYM2149 — ymengine: voice engine (16-channel, 3-channel and dual-chip modes)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Takes over the engine of the prototype (worklet YMProc: allocation and triggering of voices, generators, ADSR,
// vibrato, modulation table, slides, arpeggio played on the keyboard) on top of the ymcore emulation:
// - 16-channel mode: each voice is a virtual YM channel computed by events (VirtualChannel), mixed in floating
//   point on two buses at the internal step, each resampled once (D-018, D-049);
// - 3-channel and dual-chip modes: the voices write the registers of real chips (ChipRenderer), noise and
//   envelope shared according to the rules of D-050; mono output like the ST.
// Timer effects go through the MFP 68901 (quantised) or are continuous (TimerMode::Free).
//
// Audio thread: render() and the event functions do not allocate, do not lock and make no system call.
// prepare() allocates. The engine is not shared between threads: the plugin (M3) passes it settings and
// events from its audio thread.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "ymcore/ChipRenderer.h"
#include "ymengine/Dma.h"
#include "ymengine/DrumKit.h"
#include "ymengine/Mfp68901.h"
#include "ymengine/Params.h"
#include "ymengine/Sequence.h"

namespace stym::ymengine {

enum class VoiceTag : uint8_t { Manual, Arp, Sequencer, Chord, Echo };  // Echo: repeat of a note (D-299)

// Outputs of a render. The separate outputs (plugin buses, STYM2149_SPEC.md §7) are mono and go through the same
// output stage as the main mix (main level, tanh or linear):
// - track: the track alone, after its mixer (level, mute, solo) and without panning. In 16-channel mode, its sum
//   is the one that enters the mix; in 3-channel mode, it is the chip rendered with only the channels of the track
//   (channels of the other tracks off), as the mixing of the chip is not linear. It must have been requested at
//   preparation (EngineConfig::trackOutputs);
// - chip: each chip of the 3-channel modes; silent in 16-channel mode (the channels are virtual there).
// A null pointer: output not wanted for this render.
// Frame of the register recorder (M6, B11, D-157): the state of a chip of the 3-channel modes at the start of a
// replay tick, as a YM file player would replay it at each tick (YM6 and SNDH exports of M7):
// - reg: R0 to R13 as written to the chip, writes dated at the start of the frame included; the muted channels
//   (mute, solo) have a zero volume in it, as heard;
// - r13: R13 written during the previous frame (envelope retrigger), or first frame of a take where a channel
//   is at envelope volume; otherwise a player does not write it again;
// - fx: timer effect of each channel (SID, Sync Buzzer, Digidrum, sampled Chipdrum), whose writes fall between
//   two frames: generator, value (SID: high volume; Sync Buzzer: R13 shape; Digidrum and Chipdrum: slot) and
//   timer setting (the MFP one; in Free mode, the closest MFP setting);
//   restart: the timer of the channel fired during the previous frame (attack: a Digidrum restarts at the start
//   of its sample), or it is the first frame of the take;
// - sync: parameter of the last Zxx command of the sequencer since the previous frame (demo sync, written by the
//   SNDH export, D-150), -1 without Zxx.
struct RegisterFrame {
    struct Effect {
        bool on = false;
        Generator gen = Generator::Chip;
        uint8_t value = 0;
        MfpSetting timer{};
        bool restart = false;
        bool operator==(const Effect&) const = default;
    };
    std::array<uint8_t, 14> reg{};
    bool r13 = false;
    std::array<Effect, 3> fx{};
    int16_t sync = -1;
    bool operator==(const RegisterFrame&) const = default;
};

// STE DMA sound at a frame of the recorder (M7, D-162). Only one DMA channel on the hardware: the DMA voice started
// most recently takes it at its attack and keeps it until its end (older voices do not come back to it).
struct DmaFrame {
    bool on = false;
    bool restart = false;  // started since the previous frame, or first frame of the take
    uint8_t slot = 0;      // slot of the DMA bank
    uint8_t voices = 0;    // DMA voices that sound (more than one: layered)
    float step = 1;        // pitch ratio (1: rate of the sample)
    float gain = 1;        // volume of the note, from 0 to 1
    uint32_t start = 0;    // first frame of the slot read at the attack (Oxx of the CMD row, D-338)
    bool operator==(const DmaFrame&) const = default;
};

// Buffers of one render of n samples: left and right are required (the main mix); the separate outputs and the
// metronome are optional, as described above RegisterFrame.
struct Outputs {
    float* left = nullptr;
    float* right = nullptr;
    std::array<float*, kTracks> track{};
    std::array<float*, 2> chip{};
    // Metronome alone, mono, added to what is there; null: added to left and right. The plugin adds it to its output
    // after taking the oscilloscope and the output meters (third DAW test, item 27, D-226).
    float* click = nullptr;
};

// The voice engine. Its owner (the plugin's Driver, an offline render, a test) prepares it outside the audio thread,
// then makes every other call from the one thread that renders; the state lives in Impl (src/EngineImpl.h).
class Engine {
public:
    // Voices of the prototype: 16 track voices, 80 sequencer chord voices (16 × 5, M5), 16 free voices.
    // Indices: voice t (0 to 15) is the voice of track t, 16 + 5t + k the chord voice of rank k of track t, then the
    // free voices. In the 3-channel modes, voice 3 × chip + channel plays a channel of a chip, and voices 6 to 13 the
    // DMA tracks of the STE machine.
    static constexpr int kLaneVoices = 16;
    static constexpr int kChordVoices = 80;
    static constexpr int kPoolVoices = 16;
    static constexpr int kVoices = kLaneVoices + kChordVoices + kPoolVoices;
    static constexpr uint32_t kMaxEvents = 1024;  // events per call to render()
    static constexpr uint32_t kChunk = 256;       // largest render slice, in samples
    static constexpr uint32_t kMaxUserSample = 1u << 16;  // 4-bit samples per Digidrum slot (8.4 s)

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Allocates everything the render needs. Settings and kit are kept.
    void prepare(const EngineConfig& config);
    // Immediate silence: voices cut, chips and buses reset, pending events dropped.
    void reset() noexcept;

    // Change of voice mode: all the voices are cut. VBL rate: taken at the next frame.
    void setGlobal(const GlobalParams& g) noexcept;
    const GlobalParams& global() const noexcept;
    // Sound, arpeggio and mixer of a track. The sound is frozen in the voices at the attack (like the prototype); the
    // mixer (level, panning, mute, solo) applies live in 16-channel mode.
    void setTrack(int track, const TrackParams& t) noexcept;
    const TrackParams& track(int track) const noexcept;
    // Mixer of a track at sample `offset` of the next render().
    void setTrackMix(uint32_t offset, int track, const TrackMix& mix) noexcept;

    // Chipdrum kit (samples and frames recomputed, without allocation).
    void setKit(const KitParams& kit) noexcept;
    const KitParams& kit() const noexcept;
    // Digidrum sample of a slot: 4-bit values (0-15) at 7 800 Hz, copied (truncated to kMaxUserSample). An empty
    // slot plays nothing.
    void setUserSample(int slot, std::span<const uint8_t> nibbles) noexcept;
    // Playback of a Digidrum slot (rate, root note, fine tune, loop; M6, B9, D-154), read at the attack (the loop)
    // and at each pitch computation (the rate). Values brought within their ranges.
    void setDigidrumSlot(int slot, const DigidrumSlot& s) noexcept;
    const DigidrumSlot& digidrumSlot(int slot) const noexcept;

    // STE DMA bank (M6, B8, D-153), played by the DMA voices (Generator::Dma) with the STE machine
    // (GlobalParams::ste): settings of the slots (rate, mono or stereo, root note, loop), read at the attack, and
    // signed bytes of a slot (copied, truncated to kDmaMaxBytes; the voices playing it stop). Without
    // allocation.
    void setDma(const DmaParams& p) noexcept;
    const DmaParams& dma() const noexcept;
    void setDmaSample(int slot, std::span<const int8_t> bytes) noexcept;

    // Events, at sample `offset` (0 ≤ offset < n) of the next render(n). Beyond kMaxEvents per block, events are
    // ignored (counter droppedEvents()).
    //
    // Notes: `track` < 0 follows the keyboard playing of the prototype (focused track, or all the active tracks in
    // layers); `track` ≥ 0 plays this track alone, active or not (routing by MIDI channel), the keyboard arpeggio
    // applying only to the focused track. A track beyond the current mode (3-channel: 4 tracks per chip) is
    // ignored. noteOff(track ≥ 0) only releases the notes of this track. Velocity 1 to 127 (above: 127); 0 or less is
    // a noteOff.
    void noteOn(uint32_t offset, int note, int velocity, int track = -1) noexcept;
    void noteOff(uint32_t offset, int note, int track = -1) noexcept;
    // Pitch bend and wheel: all the tracks (`track` < 0) or only one.
    void pitchBend(uint32_t offset, double semitones, int track = -1) noexcept;
    // Modulation wheel: vibrato depth added to that of the track, in cents, at the speed of the track and at the
    // VBL cadence (like the vibrato of the prototype). Drums have none.
    void modWheel(uint32_t offset, double cents, int track = -1) noexcept;
    void allNotesOff(uint32_t offset) noexcept;   // panic: 64-sample fade (allOff)
    void releaseManual(uint32_t offset) noexcept; // change of timbre: cuts the manual playing (manOff)
    // Start of the YM Player (ymplay of the prototype): manual playing, chords and sequencer notes released
    // (killTag 'man' and 'seq', without hard fade).
    void releaseForPlayer(uint32_t offset) noexcept;
    void midiClock(uint32_t offset) noexcept;     // MIDI clock pulse (24 per quarter note)
    void midiStart(uint32_t offset) noexcept;
    void midiStop(uint32_t offset) noexcept;
    void midiContinue(uint32_t offset) noexcept;
    void midiSongPosition(uint32_t offset, int sixteenths) noexcept;  // SPP, in sixteenth notes
    // Program Change (M6, B10, D-156): the track (`track` ≥ 0) or all the tracks (`track` < 0) from now on play
    // instrument `program` (1 to 32) instead of their sound, or their sound again (0), like the INS row: notes
    // played live (keyboard arpeggio included) and sequencer steps whose INS row is 00. The notes that are
    // sounding keep their sound; a drum track has no instrument. Above 32: ignored.
    void programChange(uint32_t offset, int program, int track = -1) noexcept;

    // Sequencer (M5). The patterns and the Song come from an immutable snapshot, read by the audio thread: its
    // owner keeps it alive until another one has replaced it (next setSequence). nullptr: nothing.
    void setSequence(const SeqData* data) noexcept;
    // Instruments of the project (M6, D-147), read like the patterns: its owner keeps the table alive until the
    // next call. A melodic step whose INS row is n (1 to 32) plays instrument n instead of the sound of its track
    // (sound, volume and arpeggio; level, panning, gate and role of the track stay those of the track);
    // an empty slot, or nullptr: the sound of the track.
    void setInstruments(const InstrumentBank* bank) noexcept;
    // Transport of the host for the next block (Host sync), to give before render().
    struct HostTransport {
        bool playing = false;
        double ppq = 0;           // position in quarter notes at the first sample of the block
        double bpm = 120;
        int sigNum = 4, sigDen = 4;
        bool hasBar = false;
        double barPpq = 0;        // start of the current bar, in quarter notes
    };
    void setHostTransport(const HostTransport& t) noexcept;
    // Playback (Play of the prototype): start at the current pattern, or at the first block of the Song. Stop: the
    // sequencer notes are released.
    void seqPlay(uint32_t offset) noexcept;
    void seqStop(uint32_t offset) noexcept;
    // Pattern played: when stopped, at once; during playback, at the end of the loop (queue of the prototype).
    void seqSelectPattern(uint32_t offset, int pattern) noexcept;
    // Jump to a Song entry (songStep, Enter in the Song window): its pattern becomes the pattern played at once,
    // the current step kept; during playback, the entry restarts at its first repeat and the next one is queued
    // again (songArm). When stopped, the next Play starts again from the first entry, as in the prototype. No
    // effect in Host sync playback (the host places the Song) or without a Song.
    void seqSongJump(uint32_t offset, int entry) noexcept;
    // Preview of a note written in the grid of a drum track (seqprev): same path as the sequencer.
    void seqPreview(uint32_t offset, int track, int note, double accent) noexcept;
    // Preview of an instrument of the Chipdrum kit (Audition button of the Drum Kit window, drumprev): its
    // 16-channel sample, played as is into the main output, whatever the track.
    void drumPreview(uint32_t offset, int slot) noexcept;
    // Preview of a Digidrum slot (editor, D-154): once, at its rate and fine tune, without loop, through the level
    // table of the chip (one channel alone), added to the mix like the preview of the kit.
    void digidrumPreview(uint32_t offset, int slot) noexcept;
    // Preview of a DMA slot (DMA window): at its rate, once, centred, added to the mix like that of a Digidrum,
    // whatever the machine; only one at a time. The panic (allNotesOff) stops it too.
    void dmaPreview(uint32_t offset, int slot) noexcept;
    void dmaStopPreview(uint32_t offset) noexcept;

    // State of the sequencer for the interface (read between two renders). queued: -1 when nothing is queued;
    // pattern and queued are also -1 for a Rest or NoteOff entry of the Song (D-158).
    struct SeqStatus {
        bool playing = false;
        bool clockRunning = false;   // MIDI clock: SYNC (Start/Continue received) or STOP
        int pattern = 0, queued = -1;
        int step = -1;               // global step shown (-1 when stopped)
        int64_t stepAbs = -1;        // steps since the start (tracks with their own length)
        int songPos = 0;
        double bpm = 0;              // tempo played (Song block or free tempo)
        double externalBpm = 0;      // MIDI clock: measured tempo
    };
    SeqStatus seqStatus() const noexcept;
    // Step of the table played by the keyboard arpeggio (astep of the prototype: arpeggio of the focused track in
    // Table mode, of the held notes), -1 otherwise.
    int arpStep() const noexcept;

    // Meters and oscilloscopes of the tracks (Mixer window of the prototype: chPk, scopeBuf). Peak: highest level
    // of a voice of the track since the previous read (1 = one channel alone at index 31 of the level table;
    // level, mute, solo, accent and drum gain included, without panning or main volume);
    // oscilloscope: sum of the voices of the track, kScopeRate points per second (one point every 8 samples at
    // 48 kHz in the prototype), the last kScopePoints. In 3-channel mode: level of the chip channels used by the
    // track (last note played on the channel).
    static constexpr int kScopePoints = 128;
    static constexpr double kScopeRate = 6000;
    struct TrackMeters {
        std::array<float, kTracks> peak{};
        std::array<std::array<float, kScopePoints>, kTracks> scope{};
        uint32_t scopePos = 0;  // next point written: the oldest
    };
    // Copies the meters and resets the peaks (audio thread, between two renders).
    void takeMeters(TrackMeters& out) noexcept;

    // Log of the sequencer decisions (parity tests with the prototype): filled without allocation as long as the
    // capacity reserved by the caller allows.
    // The exports (M7, D-170) also read from it the track of an attack and the nominal tempo of each step, outside
    // the comparison with the prototype.
    struct SeqLogRecord {
        uint64_t sample;   // sample since the reset
        char op;           // operation (format: tests/unit/test_proto_seq.cpp)
        int16_t voice;
        int32_t a, b;
        double x, y, z;
        int16_t track = -1;  // 'T' (attack): track that plays the note; -1 otherwise
        double bpm = 0;      // '@' (step): nominal tempo, without swing or groove (Ticks: 60 × VBL / (speed × steps
                             // per quarter note)); 0 otherwise
    };
    void setSeqLog(std::vector<SeqLogRecord>* log) noexcept;

    // Stereo render of n samples (in 3-channel mode, left = right).
    void render(float* left, float* right, uint32_t n) noexcept;
    // Render with separate outputs.
    void render(const Outputs& out, uint32_t n) noexcept;

    // Bass and treble settings of the STE chain (D-047), without reallocation: the filters keep their state.
    void setBoardTone(int bassDb, int trebleDb) noexcept;

    // Delay of the resampling, in samples (to report to the host).
    uint32_t latencySamples() const noexcept;

    // Observation (tests, interface).
    struct VoiceInfo {
        bool on = false;
        VoiceTag tag = VoiceTag::Manual;
        int track = -1;
        int note = -1;
        uint64_t age = 0;             // attack rank: the most recently attacked voice has the largest
        uint16_t notePeriod = 0;      // period of the note at the attack ("tp" message of the prototype)
        uint16_t tonePeriod = 0;      // effective period (vibrato and ornament included)
        uint8_t volume = 0;           // current 4-bit volume
        bool noise = false;           // audible noise (setting of the track or ornament step)
        double timerHz = 0;           // frequency requested from the effect timer (0: no timer)
        double timerActualHz = 0;     // frequency actually played (quantised by the MFP or continuous)
        Generator gen = Generator::Chip;  // current generator (GEN row of the sequences included)
        bool tone = false;            // tone in the mixer
        bool envelope = false;        // volume driven by the envelope (bit 4)
        uint8_t noisePeriod = 0;      // current noise period (LFO included)
        uint16_t envPeriod = 0;       // current envelope period (pitch matrix and LFO included)
        double lfoPan = 0;            // panning added by the LFO, -1 to 1 (D-257)
        uint8_t adsrStage = 0;        // 0: frozen, 1 attack, 2 decay, 3 sustain, 4 release
        double adsrLevel = 1;         // level at the start of the stage
        double adsrNow = 1;           // level at the end of the last render (envelope tracking on screen)
        bool fading = false;
    };
    VoiceInfo voice(int index) const noexcept;
    // DMA voice (D-153): its slot, its frame and its read step (frames of the sample per frame of the stream), at
    // the rate of the slot. dmaPreviewSlot: slot being previewed (-1: none).
    struct DmaInfo {
        bool on = false;
        int slot = 0;
        double position = 0;
        double step = 1;
    };
    DmaInfo dmaVoice(int index) const noexcept;
    int dmaPreviewSlot() const noexcept;
    uint8_t dmaPlayingSlots() const noexcept;  // bit k: slot k played by a voice or the preview
    int program(int track) const noexcept;      // instrument chosen by Program Change (0: the sound of the track)
    int activeVoices() const noexcept;
    uint64_t droppedEvents() const noexcept;
    uint64_t samplesRendered() const noexcept;
    // Chips of the 3-channel mode (0 or 1).
    const ymcore::ChipRenderer& chip(int index) const noexcept;

    // Register recorder (M6, B11, D-157). Between two renders: start of a take on chip `chip` (0, or 1 in dual
    // chip) at the current sample, one frame (RegisterFrame) per replay tick (GlobalParams::vblHz at the start,
    // kept for the whole take), written to `frames`, which the caller keeps alive until stopCapture().
    // No effect in 16-channel mode, on an absent chip or without room (capturing() stays false). The take stops by
    // itself when `frames` is full and when the time base restarts (reset, change of mode or machine). A take in
    // progress is replaced. `dma`, if it is not empty, receives the DMA sound of each frame (DmaFrame, STE
    // machine) and bounds the take like `frames`.
    void startCapture(int chip, std::span<RegisterFrame> frames, std::span<DmaFrame> dma = {}) noexcept;
    // End of the take: number of frames written.
    uint32_t stopCapture() noexcept;
    bool capturing() const noexcept;
    uint32_t capturedFrames() const noexcept;
    uint32_t captureHz() const noexcept;  // replay tick of the take

    // Log of the register writes of the 3-channel mode (tests): filled without allocation as long as the capacity
    // reserved by the caller allows.
    struct WriteRecord {
        uint8_t chip;
        uint64_t cycle;
        uint8_t reg;
        uint8_t value;
    };
    void setWriteLog(std::vector<WriteRecord>* log) noexcept;

    // Internal state and render (src/EngineImpl.h; sequencer in src/Sequencer.cpp).
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace stym::ymengine
