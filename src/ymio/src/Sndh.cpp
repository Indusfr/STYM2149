// STYM2149 — ymio: reading SNDH files by emulating the Atari (M7, A2, D-159)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Tags and render loop taken from SndhRenderer.cpp of AtariAudio 1.26 (Arnaud Carré, MIT licence), with bounds on
// every read of the header.
#include "ymio/Sndh.h"

#include <AtariMachine.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <new>

#include "ymio/Ice.h"

namespace stym::ymio {

namespace {

// AtariAudio's kAtariSTECpuClock and ComputeHostSamplePerTick: VBL of 313 lines of 512 cycles at 8 021 247 Hz.
constexpr uint64_t kAtariCpuClock = 8021247;
uint32_t hostSamplesPerTick(uint32_t rate, int tickRate) noexcept {
    const uint64_t n = uint64_t(rate) * 313 * 512 * 50 / (uint64_t(tickRate) * kAtariCpuClock);
    return uint32_t(std::max<uint64_t>(1, n));
}

// Bounded reads at an offset of the unpacked file: nothing is read past its end (a number there reads as 0, a string
// stops at the end).
class Header {
public:
    explicit Header(std::span<const uint8_t> d) noexcept : d_(d) {}
    bool has(size_t at, const char* tag) const noexcept {
        const size_t n = std::strlen(tag);
        return at + n <= d_.size() && std::memcmp(d_.data() + at, tag, n) == 0;
    }
    uint32_t be16(size_t at) const noexcept {
        return at + 2 <= d_.size() ? uint32_t(d_[at]) << 8 | d_[at + 1] : 0;
    }
    uint32_t be32(size_t at) const noexcept { return be16(at) << 16 | be16(at + 2); }
    // Zero-terminated string (or terminated by the end of the data).
    std::string text(size_t at) const {
        std::string s;
        for (; at < d_.size() && d_[at] != 0; ++at) s += char(d_[at]);
        return s;
    }
    size_t skip(size_t at) const noexcept {  // SkipNTString
        while (at < d_.size() && d_[at] != 0) ++at;
        return at + 1;
    }
    // atoi: blanks, sign, digits (bounded to 7 digits).
    int number(size_t at, size_t maxChars = SIZE_MAX) const noexcept {
        const size_t end = std::min(d_.size(), at > SIZE_MAX - maxChars ? SIZE_MAX : at + maxChars);
        while (at < end && (d_[at] == ' ' || (d_[at] >= 9 && d_[at] <= 13))) ++at;
        bool neg = false;
        if (at < end && (d_[at] == '-' || d_[at] == '+')) neg = d_[at++] == '-';
        int v = 0;
        for (int digits = 0; at < end && d_[at] >= '0' && d_[at] <= '9' && digits < 7; ++at, ++digits)
            v = v * 10 + (d_[at] - '0');
        return neg ? -v : v;
    }

private:
    std::span<const uint8_t> d_;
};

// Unpacked SNDH: a BRA instruction ($60xx) at the start and the "SNDH" mark at byte 12.
bool rawSndh(std::span<const uint8_t> d) noexcept {
    return d.size() > 16 && d[0] == 0x60 && std::memcmp(d.data() + 12, "SNDH", 4) == 0;
}

}  // namespace

bool looksLikeSndh(std::span<const uint8_t> data) noexcept {
    if (isIce(data)) return true;  // most SNDH files are packed
    return rawSndh(data);
}

SndhParseResult parseSndh(std::span<const uint8_t> data) {
    SndhParseResult r;
    SndhFile f;
    if (isIce(data)) {
        auto out = iceDepack(data, kSndhMaxSize - kSndhLoadAddress);
        if (!out) {
            r.error = "The packed data (Ice) is damaged.";
            return r;
        }
        f.image = std::move(*out);
        f.packed = true;
    } else {
        f.image.assign(data.begin(), data.end());
    }
    const std::span<const uint8_t> d(f.image);
    if (!rawSndh(d)) {
        r.error = "Not an SNDH file.";
        return r;
    }
    if (d.size() > kSndhMaxSize - kSndhLoadAddress) {
        r.error = "The SNDH file is too large for the Atari memory.";
        return r;
    }
    const Header h(d);
    // bra.w (or bra.s) to the init: the header stops there.
    size_t headerSize = h.be16(2) + 2;
    if (d[1]) headerSize = size_t(d[1]) + 2;
    const size_t end = std::min(headerSize, d.size());
    SndhInfo& si = f.info;
    bool frms = false;
    int tags = 0;
    std::vector<uint32_t> len(kSndhMaxSubsongs, 0);
    // Tags from byte 16 to the target of the first BRA, in AtariAudio's order of tests; an unknown byte is skipped
    // one at a time, so padding and unknown tags are passed over.
    for (size_t at = 16; at + 4 <= end;) {
        if (h.has(at, "!#SN")) {  // table of the subsong names: skipped
            at += 4 + size_t(si.subsongs) * 2;
            ++tags;
        }
        if (h.has(at, "!#")) {
            si.defaultSubsong = h.number(at + 2);
            at = h.skip(at + 2);
            ++tags;
        } else if (h.has(at, "TITL")) {
            si.title = h.text(at + 4);
            at = h.skip(at + 4);
            ++tags;
        } else if (h.has(at, "COMM")) {
            si.composer = h.text(at + 4);
            at = h.skip(at + 4);
            ++tags;
        } else if (h.has(at, "RIPP")) {
            si.ripper = h.text(at + 4);
            at = h.skip(at + 4);
            ++tags;
        } else if (h.has(at, "CONV")) {
            si.converter = h.text(at + 4);
            at = h.skip(at + 4);
            ++tags;
        } else if (h.has(at, "YEAR")) {
            si.year = h.text(at + 4);
            at = h.skip(at + 4);
            ++tags;
        } else if (h.has(at, "##")) {
            si.subsongs = h.number(at + 2, 2);
            if (si.subsongs <= 0 || si.subsongs > kSndhMaxSubsongs) si.subsongs = 1;  // broken tag
            at += 4;
            ++tags;
        } else if (h.has(at, "TIME")) {
            at += 4;
            if (at & 1) ++at;  // aligned word (even address in the file)
            for (int i = 0; i < si.subsongs; ++i, at += 2) len[size_t(i)] = h.be16(at) * uint32_t(std::max(si.tickRate, 0));
            ++tags;
        } else if (h.has(at, "FRMS")) {
            at += 4;
            for (int i = 0; i < si.subsongs; ++i, at += 4) len[size_t(i)] = h.be32(at);
            frms = true;
            ++tags;
        } else if (h.has(at, "HDNS")) {
            break;
        } else if (h.has(at, "TA") || h.has(at, "TB") || h.has(at, "TC") || h.has(at, "TD") || h.has(at, "!V")) {
            si.timer = char(d[at + 1]);
            si.tickRate = h.number(at + 2);
            at = h.skip(at + 2);
            ++tags;
        } else {
            ++at;
        }
    }
    (void)frms;  // AtariAudio also looks up lengths in its timedb base without FRMS: not here (licence to check)
    if (tags < 1) {
        r.error = "The SNDH header has no tag.";
        return r;
    }
    if (si.defaultSubsong < 1 || si.defaultSubsong > si.subsongs) si.defaultSubsong = 1;
    // Zero or negative rate (broken tag): AtariAudio would divide by zero; 50 Hz as without a tag.
    if (si.tickRate <= 0) si.tickRate = 50;
    si.tickRate = std::min(si.tickRate, 1000);
    si.frames.assign(len.begin(), len.begin() + si.subsongs);
    r.file = std::move(f);
    return r;
}

// ------------------------------------------------------------------------------------------------- capture

void SndhCapture::MachineDelete::operator()(AtariMachine* m) const noexcept {
    m->~AtariMachine();
    ::operator delete(static_cast<void*>(m));
}

namespace {

// AtariAudio leaves members of its machine without an initial value (Ym2149c, SteDac): the memory is zeroed before
// construction, for an identical capture at each playback.
AtariMachine* newMachine() {
    void* mem = ::operator new(sizeof(AtariMachine));
    std::memset(mem, 0, sizeof(AtariMachine));
    return new (mem) AtariMachine();
}

}  // namespace

SndhCapture::SndhCapture(const SndhFile& file, int subsong, uint32_t rate)
    : machine_(newMachine()), rate_(std::max<uint32_t>(rate, 1000)) {
    spt_ = hostSamplesPerTick(rate_, file.info.tickRate);
    AtariMachine& m = *machine_;
    {
        // Ym2149c::Reset draws the state of the tone edges from a global generator (sRndSeed of ym2149c.cpp): only one
        // machine starts at a time. No effect on the capture (chip channels muted).
        static std::mutex startup;
        const std::lock_guard<std::mutex> lock(startup);
        m.Startup(rate_);
    }
    m.MuteVoices(7);  // chip channels muted: the machine's output is that of the STE DAC
    if (subsong < 1 || subsong > file.info.subsongs) return;
    if (!m.Upload(file.image.data(), kSndhLoadAddress, uint32_t(file.image.size()))) return;
    m.SetYmPortObserver(&SndhCapture::observe, this);
    std::vector<SndhEvent> init;
    out_ = &init;
    budget_ = kMaxEventsPerRun;
    ok_ = m.Jsr(kSndhLoadAddress, uint32_t(subsong));
    out_ = nullptr;
    pending_ = std::move(init);
}

SndhCapture::~SndhCapture() = default;

uint8_t SndhCapture::peek(uint32_t address) { return uint8_t(machine_->memRead8(address)); }

uint64_t SndhCapture::cycleOf(uint64_t s) const noexcept {
    constexpr uint64_t clk8 = kClock / 8;
    return 8 * ((s * clk8 + rate_ - 1) / rate_);
}

// Write events are timed at stamp_; the edge resets of an interrupt follow its writes.
void SndhCapture::observe(void* user, uint8_t port, uint8_t value) {
    auto& c = *static_cast<SndhCapture*>(user);
    if (port == AtariMachine::YM_OBSERVER_TIMER_IRQ_END) {
        // Ym2149c::InsideTimerIrq(false): edges reset, in voice order.
        for (uint8_t v = 0; v < 3; ++v)
            if (c.edgeReset_ & (1u << v)) c.push(uint8_t(ymcore::Ym2149::kToneEdgeReset), v);
        c.edgeReset_ = 0;
        return;
    }
    if (!(port & 2)) {
        c.selected_ = value;  // Ym2149c::WritePort: register selected as is
        return;
    }
    const uint8_t reg = c.selected_;
    if (reg >= 14) return;  // I/O ports (R14, R15): no effect on the sound
    if (reg < 6) {
        // Ym2149c::WriteReg: period 0 or 1 written in a timer interrupt → edge to reset on exit.
        c.tone_[reg] = (reg & 1) ? (value & 0x0f) : value;
        const unsigned v = reg >> 1u;
        if (c.inTimers_ && (unsigned(c.tone_[v * 2]) | unsigned(c.tone_[v * 2 + 1]) << 8) <= 1) c.edgeReset_ |= uint8_t(1u << v);
    }
    c.push(reg, value);
}

void SndhCapture::push(uint8_t reg, uint8_t value) {
    if (!out_) return;
    if (budget_ == 0) {
        ++dropped_;
        return;
    }
    --budget_;
    SndhEvent e;
    e.cycle = cycleOf(stamp_);
    e.kind = SndhEvent::Kind::Write;
    e.reg = reg;
    e.value = value;
    out_->push_back(e);
}

bool SndhCapture::run(uint64_t until, std::vector<SndhEvent>& out) {
    // Writes of the init: at the first sample.
    if (!pending_.empty()) {
        out.insert(out.end(), pending_.begin(), pending_.end());
        pending_.clear();
        pending_.shrink_to_fit();
    }
    if (!ok_) return false;
    AtariMachine& m = *machine_;
    out_ = &out;
    budget_ = kMaxEventsPerRun;
    // SndhRenderer::AudioRender, one sample at a time: the play every spt_ samples, then the machine computes the
    // sample and serves the MFP timers after it.
    for (; sample_ < until; ++sample_) {
        if (inner_ == 0) {
            stamp_ = sample_;
            SndhEvent tick;
            tick.cycle = cycleOf(sample_);
            tick.kind = SndhEvent::Kind::Tick;
            out.push_back(tick);
            if (!m.PlayerTick(kSndhLoadAddress + 8)) {  // crashed routine: SndhRenderer stops too
                ok_ = false;
                break;
            }
            ++ticks_;
            inner_ = spt_;
        }
        stamp_ = sample_ + 1;  // writes of the timers: after the computation of this sample
        const size_t mark = out.size();
        inTimers_ = true;
        const int16_t dac = m.ComputeNextSample();
        inTimers_ = false;
        if (dac != dac_) {
            // Level of this sample, placed before the writes of the timers (timed at the next one): the stream stays
            // in time order.
            SndhEvent e;
            e.cycle = cycleOf(sample_);
            e.kind = SndhEvent::Kind::Dac;
            e.dac = dac;
            out.insert(out.begin() + std::ptrdiff_t(mark), e);
            dac_ = dac;
            dacUsed_ = true;
        }
        --inner_;
    }
    out_ = nullptr;
    return ok_;
}

// -------------------------------------------------------------------------------------------------- stream

SndhStream::SndhStream(std::shared_ptr<const SndhFile> file, int subsong)
    : SndhStream(std::move(file), subsong, Options{}) {}

SndhStream::SndhStream(std::shared_ptr<const SndhFile> file, int subsong, const Options& options)
    : file_(std::move(file)), subsong_(subsong), options_(options) {
    // Ring of at least 1 024 events, rounded up to a power of two for the mask.
    uint32_t cap = 1024;
    while (cap < options_.capacity) cap <<= 1;
    ring_.resize(cap);
    mask_ = cap - 1;
    thread_ = std::thread([this] { work(); });
}

SndhStream::~SndhStream() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

bool SndhStream::waitFor(uint64_t end, int timeoutMs) const noexcept {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (ready_.load(std::memory_order_acquire) < end) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::yield();
    }
    return true;
}

// Producer side of the ring. Events are copied into the ring before ready_ is published (release), so a reader that
// sees ready_ >= end also sees every event before end. A full ring makes the worker wait for the reader; a crashed
// routine sets ready_ to UINT64_MAX, so the reader no longer waits for it.
void SndhStream::work() {
    using namespace std::chrono_literals;
    SndhCapture capture(*file_, subsong_, options_.rate);
    const uint64_t ahead = uint64_t(options_.lookahead * double(SndhCapture::kClock));
    const uint64_t chunk = std::max<uint64_t>(1, options_.rate / 100);  // 10 ms of capture at a time
    std::vector<SndhEvent> events;
    events.reserve(4096);
    size_t sent = 0;
    bool alive = true;
    while (!stop_.load(std::memory_order_acquire)) {
        // Events waiting for room in the ring.
        if (sent < events.size()) {
            const uint64_t tail = tail_.load(std::memory_order_relaxed);
            const uint64_t head = head_.load(std::memory_order_acquire);
            uint64_t t = tail;
            for (; sent < events.size() && t - head < ring_.size(); ++sent, ++t) ring_[size_t(t & mask_)] = events[sent];
            tail_.store(t, std::memory_order_release);
            if (sent < events.size()) {
                std::this_thread::sleep_for(2ms);
                continue;
            }
            ready_.store(alive ? capture.cycleOf(capture.sample()) : UINT64_MAX, std::memory_order_release);
            if (!alive) {
                failed_.store(true, std::memory_order_release);
                return;
            }
        }
        if (!alive) {
            ready_.store(UINT64_MAX, std::memory_order_release);
            failed_.store(true, std::memory_order_release);
            return;
        }
        // Far enough ahead of the reader: wait.
        if (ready_.load(std::memory_order_relaxed) >= reading_.load(std::memory_order_relaxed) + ahead) {
            std::this_thread::sleep_for(2ms);
            continue;
        }
        events.clear();
        sent = 0;
        alive = capture.run(capture.sample() + chunk, events);
        if (events.empty()) ready_.store(alive ? capture.cycleOf(capture.sample()) : UINT64_MAX, std::memory_order_release);
    }
}

// ------------------------------------------------------------------------------------------------ playback

// Per chunk: takes the stream's events up to the end of the chunk (writes into the renderer's queue, DAC changes into
// dacQueue_), renders the chip, then adds the DAC level sample by sample.
void SndhPlayer::render(ymcore::ChipRenderer& renderer, float* out, uint32_t n, bool offline) noexcept {
    const uint32_t host = renderer.config().hostRate;
    const uint32_t chunkMax = std::clamp<uint32_t>(host / 250, 1, kMaxChunk);  // 4 ms
    // Renderer cycle of a stream event (never before the start of the render).
    const auto toRenderer = [this](uint64_t c) noexcept {
        const int64_t at = int64_t(c) - offset_;
        return at > 0 ? uint64_t(at) : uint64_t(0);
    };
    while (n > 0) {
        const uint32_t k = std::min(n, chunkMax);
        const uint64_t first = renderer.samplesRendered();
        const int64_t endStream = int64_t(renderer.cycleAtSample(first + k)) + offset_;
        const uint64_t streamEnd = endStream > 0 ? uint64_t(endStream) : 0;
        if (offline) stream_->waitFor(streamEnd, 2000);
        dacCount_ = 0;
        const bool ready = stream_->consume(streamEnd, [&](const SndhEvent& e) {
            switch (e.kind) {
            case SndhEvent::Kind::Write: renderer.write(toRenderer(e.cycle), e.reg, e.value); break;
            case SndhEvent::Kind::Tick: ++frames_; break;
            case SndhEvent::Kind::Dac:
                if (dacCount_ == dacQueue_.size()) {  // chunk too dense: the oldest is applied at once
                    dac_ = dacQueue_[0].level;
                    std::copy(dacQueue_.begin() + 1, dacQueue_.end(), dacQueue_.begin());
                    --dacCount_;
                }
                dacQueue_[dacCount_++] = {toRenderer(e.cycle), e.dac};
                break;
            }
        });
        if (!ready) {
            // Late capture: the stream time stops at what it has reached.
            const uint64_t reached = std::min(stream_->readyCycle(), streamEnd);
            lost_ += streamEnd - reached;
            offset_ -= int64_t(streamEnd - reached);
        }
        renderer.render(out, k);
        constexpr float kScale = 1.0f / 32768.0f;
        size_t d = 0;
        for (uint32_t i = 0; i < k; ++i) {
            const uint64_t c = renderer.cycleAtSample(first + i);
            while (d < dacCount_ && dacQueue_[d].cycle <= c) dac_ = dacQueue_[d++].level;
            out[i] += float(dac_) * kScale;
        }
        for (; d < dacCount_; ++d) dac_ = dacQueue_[d].level;
        out += k;
        n -= k;
    }
}

void SndhPlayer::moveTo(ymcore::ChipRenderer& from, ymcore::ChipRenderer& to) noexcept {
    // Writes already read but due at the next step of the old chip: applied before the copy.
    from.flushDue();
    // The new renderer resumes, at its current cycle, where the stream is in the old one.
    offset_ += int64_t(from.cycleAtSample(from.samplesRendered())) - int64_t(to.cycleAtSample(to.samplesRendered()));
    const uint64_t at = to.cycleAtSample(to.samplesRendered());
    for (unsigned r = 0; r < 14; ++r) to.write(at, uint8_t(r), from.chip().readRegister(r));
}

}  // namespace stym::ymio
