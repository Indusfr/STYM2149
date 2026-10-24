// STYM2149 — ymio: reading SNDH files by emulating the Atari (M7, A2, D-159)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// An SNDH is a 68000 routine: its init (load address, d0 = subsong) then its play (address + 8) at each replay tick.
// The AtariAudio 1.26 machine (MIT: Musashi 68000, MFP, STE DAC, blitter) runs them; its writes to the chip are
// captured with their time, then replayed by ymcore in the YM Player:
// - header: tags of AtariAudio's SndhRenderer::Load, read here with bounds; Ice-packed data decompressed by
//   iceDepack (Ice.h);
// - capture (SndhCapture): the machine advances one sample at a time at `rate` (by default clk/8 = 250 kHz, one
//   internal step of the chip per sample), like SndhRenderer::AudioRender (play every ComputeHostSamplePerTick
//   samples, MFP timers at each sample). A write from the play lands at the sample where it is called; a write from a
//   timer interrupt, at the next one (AtariAudio computes the sample before serving the timers). On interrupt exit,
//   the AtariAudio chip resets the tone edge of the voices whose period was written there as 0 or 1 ("square-sync"):
//   captured as a Ym2149::kToneEdgeReset command, after the writes of the interrupt. The STE DAC is read at each
//   sample, with the chip channels muted in the machine: its level changes are captured too;
// - stream (SndhStream): the capture runs in a worker thread, half a second ahead of playback, into a ring read
//   lock-free by the audio thread. Each Musashi instance has its own state (AtariAudio version, "thread safe"):
//   several streams can run at once.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "ymcore/ChipRenderer.h"

class AtariMachine;

namespace stym::ymio {

inline constexpr int kSndhMaxSubsongs = 128;          // AtariAudio's kSubsongCountMax
inline constexpr uint32_t kSndhLoadAddress = 0x10002;  // AtariAudio's SNDH_UPLOAD_ADDR
inline constexpr size_t kSndhMaxSize = 4u << 20;       // memory of the machine (4 MB), load address included

// What the header tags say (TITL, COMM, RIPP, CONV, YEAR, ##, !#, the timer tags, TIME and FRMS).
struct SndhInfo {
    std::string title, composer, ripper, converter, year;
    int subsongs = 1;
    int defaultSubsong = 1;  // 1-based, like the subsong numbers passed to the init
    int tickRate = 50;      // TA, TB, TC, TD or !V: play calls per second
    char timer = 'C';       // timer of the tag (V: VBL); C by default
    std::vector<uint32_t> frames;  // length of each subsong in ticks (FRMS, or TIME × tickRate); 0: unknown
};

struct SndhFile {
    SndhInfo info;
    bool packed = false;          // Ice-packed
    std::vector<uint8_t> image;   // unpacked file, loaded as is at kSndhLoadAddress
};

struct SndhParseResult {
    std::optional<SndhFile> file;
    std::string error;
};

// True if the data looks like an SNDH (Ice-packed or not); parseSndh tells whether it is readable.
bool looksLikeSndh(std::span<const uint8_t> data) noexcept;
SndhParseResult parseSndh(std::span<const uint8_t> data);

// Captured event, timed in cycles of the chip clock (2 MHz, that of AtariAudio).
struct SndhEvent {
    // Write: chip register or tone-edge command; Dac: new STE DAC level; Tick: a call of the play (counts frames).
    enum class Kind : uint8_t { Write, Dac, Tick };
    uint64_t cycle = 0;
    Kind kind = Kind::Write;
    uint8_t reg = 0, value = 0;  // Write: register (0-13) and value, or Ym2149::kToneEdgeReset and voice (0-2)
    int16_t dac = 0;             // Dac: DAC output level (AtariAudio: 32 767 = chip full scale)
};

// One subsong running in its own AtariAudio machine (see the file comment). Allocates; used by the worker thread of
// SndhStream, or directly (tests, offline tools). Not thread-safe.
class SndhCapture {
public:
    static constexpr uint32_t kDefaultRate = 250000;
    static constexpr uint64_t kClock = 2000000;
    // Writes kept at most per run() call (a routine that would loop on the chip); beyond that, counted.
    static constexpr size_t kMaxEventsPerRun = size_t(1) << 20;

    // Starts the machine, loads the SNDH and calls its init (up to 10 s of 68000 time, like AtariAudio).
    SndhCapture(const SndhFile& file, int subsong, uint32_t rate = kDefaultRate);
    ~SndhCapture();
    SndhCapture(const SndhCapture&) = delete;
    SndhCapture& operator=(const SndhCapture&) = delete;

    // False if the init or a play failed (crashed routine, or one that never returns): the capture stops there.
    bool ok() const noexcept { return ok_; }
    // Advances up to sample `until` (excluded), adding the events to `out`, in the order of their times.
    // False if the routine crashes.
    bool run(uint64_t until, std::vector<SndhEvent>& out);

    uint32_t rate() const noexcept { return rate_; }
    uint64_t sample() const noexcept { return sample_; }
    uint64_t ticks() const noexcept { return ticks_; }
    uint32_t samplesPerTick() const noexcept { return spt_; }
    uint64_t droppedEvents() const noexcept { return dropped_; }
    // Byte of the emulated Atari's memory (routine variables, tests).
    uint8_t peek(uint32_t address);
    bool dacUsed() const noexcept { return dacUsed_; }
    // First internal step of the chip for sample s: 8 × ceil(s × 250 000 / rate).
    uint64_t cycleOf(uint64_t s) const noexcept;

private:
    // Called by the machine for each write to the chip ports (port 0: select, port 2: data) and after each exit from
    // a timer interrupt (AtariMachine::YM_OBSERVER_TIMER_IRQ_END).
    static void observe(void* user, uint8_t port, uint8_t value);
    // Adds a write event timed at sample stamp_ (counted in dropped_ beyond the budget of the run).
    void push(uint8_t reg, uint8_t value);

    struct MachineDelete {
        void operator()(AtariMachine* m) const noexcept;
    };
    std::unique_ptr<AtariMachine, MachineDelete> machine_;
    std::vector<SndhEvent>* out_ = nullptr;  // destination of push(); null outside the init and run()
    std::vector<SndhEvent> pending_;  // writes of the init, returned by the first run()
    uint32_t rate_;
    uint32_t spt_ = 1;    // samples between two calls of the play
    uint32_t inner_ = 0;  // samples before the next play
    // sample_: next sample to compute; stamp_: sample at which the current writes are timed.
    uint64_t sample_ = 0, ticks_ = 0, stamp_ = 0, dropped_ = 0;
    size_t budget_ = 0;        // writes still accepted in this run()
    uint8_t selected_ = 0;     // register selected by the last write to the select port
    uint8_t tone_[6] = {};     // R0-R5 of the machine's chip (masked), for the "square-sync"
    uint8_t edgeReset_ = 0;    // voices to reset on exit from the current interrupt (bit v)
    bool inTimers_ = false;    // writes from timer interrupts (during ComputeNextSample)
    int16_t dac_ = 0;
    bool dacUsed_ = false;
    bool ok_ = false;
};

// Capture kept ahead in a worker thread, read without lock or allocation by a single reader (audio thread).
class SndhStream {
public:
    struct Options {
        uint32_t rate = SndhCapture::kDefaultRate;
        uint32_t capacity = 1u << 17;  // events of the ring (power of two)
        double lookahead = 0.5;        // lead of the capture over playback, in seconds
    };

    // Message thread: starts the worker thread (the machine is created and the init is called there).
    SndhStream(std::shared_ptr<const SndhFile> file, int subsong);
    SndhStream(std::shared_ptr<const SndhFile> file, int subsong, const Options& options);
    // Stops and waits for the worker thread.
    ~SndhStream();
    SndhStream(const SndhStream&) = delete;
    SndhStream& operator=(const SndhStream&) = delete;

    // Reader: passes to f, in order, the events timed before cycle `end`. True if the capture has reached `end` (all
    // these events are passed), false if it is late (the missing ones will come later, with their time in the past);
    // a crashed routine no longer delays anything.
    template <class F>
    bool consume(uint64_t end, F&& f) noexcept {
        const uint64_t ready = ready_.load(std::memory_order_acquire);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        uint64_t h = head_.load(std::memory_order_relaxed);
        for (; h < tail; ++h) {
            const SndhEvent& e = ring_[size_t(h & mask_)];
            if (e.cycle >= end) break;
            f(e);
        }
        head_.store(h, std::memory_order_release);
        reading_.store(end, std::memory_order_relaxed);
        return ready >= end;
    }
    // Offline only: waits until the capture reaches `end` (at most `timeoutMs`).
    bool waitFor(uint64_t end, int timeoutMs) const noexcept;

    bool failed() const noexcept { return failed_.load(std::memory_order_acquire); }
    // Reader: crashed routine and all its events passed to the reader.
    bool exhausted() const noexcept {
        return failed() && head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_acquire);
    }
    uint64_t readyCycle() const noexcept { return ready_.load(std::memory_order_acquire); }

private:
    // Worker thread: creates the capture, then keeps it `lookahead` ahead of reading_, in chunks of 10 ms, until
    // stop_ or a crash of the routine.
    void work();

    std::shared_ptr<const SndhFile> file_;
    int subsong_;
    Options options_;
    std::vector<SndhEvent> ring_;
    uint64_t mask_;
    // Free-running counters (index = counter & mask_): head_ is advanced by the reader only, tail_ by the worker
    // only; the ring holds tail_ - head_ events.
    std::atomic<uint64_t> head_{0}, tail_{0};
    std::atomic<uint64_t> ready_{0};    // cycle up to which all events are in the ring
    std::atomic<uint64_t> reading_{0};  // last cycle requested by the reader
    std::atomic<bool> failed_{false}, stop_{false};
    std::thread thread_;
};

// Playing a stream into a chip (audio thread, no allocation): writes replayed at their time, STE DAC level added at
// each sample (taken at the first internal step of the sample, as AtariAudio reads it at the output rate), in render
// units (32 768 = 1). A late capture does not lose music: the stream time stops until it arrives (the chip keeps its
// state), then resumes where it was.
class SndhPlayer {
public:
    explicit SndhPlayer(std::shared_ptr<SndhStream> stream) noexcept : stream_(std::move(stream)) {}

    // `offline`: offline render, which waits for the capture instead of stopping the stream time.
    void render(ymcore::ChipRenderer& renderer, float* out, uint32_t n, bool offline) noexcept;
    // Resumes on another renderer (output settings changed): the stream continues where it is, the registers of the
    // old chip (pending writes included) are written into the new one at its current cycle.
    void moveTo(ymcore::ChipRenderer& from, ymcore::ChipRenderer& to) noexcept;

    uint64_t frames() const noexcept { return frames_; }      // replay ticks played
    uint64_t lostCycles() const noexcept { return lost_; }    // stream time stopped (late capture)
    const SndhStream& stream() const noexcept { return *stream_; }
    std::shared_ptr<SndhStream> sharedStream() const noexcept { return stream_; }

    // Render chunk: 4 ms at most, so that the DAC levels of a chunk fit in dacQueue_.
    static constexpr uint32_t kMaxChunk = 256;

private:
    struct DacChange {
        uint64_t cycle;
        int16_t level;
    };
    std::shared_ptr<SndhStream> stream_;
    int64_t offset_ = 0;  // stream cycle = render cycle + offset_
    uint64_t frames_ = 0, lost_ = 0;
    int16_t dac_ = 0;                         // DAC level of the current sample
    std::array<DacChange, 2048> dacQueue_{};  // DAC changes of the current chunk, in time order
    size_t dacCount_ = 0;
};

}  // namespace stym::ymio
