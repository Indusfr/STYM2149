// STYM2149 — S11: no allocation during reset(), write() and render()
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The global new/delete operators (and malloc with glibc) are replaced to count the allocations during the real-time
// sections. Built with -fsanitize=realtime (STYM_RTSAN option, clang ≥ 20), the same test also fails on any lock or
// blocking system call in these sections.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "Metrics.h"
#include "Scenarios.h"
#include "plugincore/Driver.h"
#include "plugincore/YmTune.h"
#include "ymcore/ChipRenderer.h"
#include "ymengine/Engine.h"
#include "ymio/YmPlayer.h"

#if STYM_RTSAN
extern "C" void __rtsan_realtime_enter();
extern "C" void __rtsan_realtime_exit();
#define STYM_RT_ENTER() __rtsan_realtime_enter()
#define STYM_RT_EXIT() __rtsan_realtime_exit()
#else
#define STYM_RT_ENTER() (void)0
#define STYM_RT_EXIT() (void)0
#endif

namespace {
std::atomic<bool> g_watch{false};
std::atomic<long> g_allocs{0};
void count() {
    if (g_watch.load(std::memory_order_relaxed)) g_allocs.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace

#if !STYM_RTSAN
void* operator new(std::size_t n) {
    count();
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    count();
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    count();
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    count();
    return std::malloc(n ? n : 1);
}
// These operators replace those of the library and pair with the new operators above (malloc): GCC, which sees
// free() applied to the result of an operator new once the call is inlined, wrongly reports a mismatch.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if defined(__GLIBC__)
extern "C" {
void* __libc_malloc(size_t);
void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t);
void* malloc(size_t n) {
    count();
    return __libc_malloc(n);
}
void* calloc(size_t a, size_t b) {
    count();
    return __libc_calloc(a, b);
}
void* realloc(void* p, size_t n) {
    count();
    return __libc_realloc(p, n);
}
}
#endif
#endif

using namespace stym;

int main() {
    using test::TimedWrite;
    long total = 0;
    bool ok = true;
    const test::Scenario sc = test::randomScenario(3, 10 * 48000, 50, true);
    for (ymcore::OutputMode mode : {ymcore::OutputMode::Raw, ymcore::OutputMode::RawBox, ymcore::OutputMode::Retro}) {
        for (uint32_t rate : {44100u, 48000u, 96000u}) {
            ymcore::RenderConfig rc;
            rc.mode = mode;
            rc.hostRate = rate;
            // One board chain per rate: each chain goes through each mode.
            rc.board.chain = rate == 48000 ? ymcore::BoardChain::ST : rate == 96000 ? ymcore::BoardChain::STE : ymcore::BoardChain::Raw;
            rc.board.bassDb = 6;
            ymcore::ChipRenderer r;
            r.prepare(rc);  // outside the real-time section: may allocate
            std::vector<float> buf(4096);
            g_allocs = 0;
            g_watch = true;
            STYM_RT_ENTER();
            for (int pass = 0; pass < 2; ++pass) {
                r.reset();
                size_t next = 0;
                for (uint64_t done = 0; done < 10ull * 48000; done += 256) {
                    while (next < sc.writes.size() && sc.writes[next].at < done + 256) {
                        const TimedWrite& w = sc.writes[next++];
                        r.write(r.cycleAtSample(w.at), w.reg, w.value);
                    }
                    r.render(buf.data(), 256);
                }
                // Queue overflow: path of the forced writes.
                for (int i = 0; i < 5000; ++i) r.write(r.cycleAtSample(r.samplesRendered() + 1000), 8, uint8_t(i & 15));
                r.render(buf.data(), 4096);
            }
            STYM_RT_EXIT();
            g_watch = false;
            total += g_allocs;
            if (g_allocs != 0) {
                ok = false;
                std::printf("allocations in mode %d at %u Hz: %ld\n", int(mode), rate, g_allocs.load());
            }
        }
    }
    // YM player: schedule() + render() on a synthetic file.
    {
        ymio::YmFile f;
        f.format = ymio::YmFormat::YM3;
        f.frameCount = 500;
        f.frames.assign(size_t(f.frameCount) * ymio::YmFile::kStride, 0);
        for (uint32_t t = 0; t < f.frameCount; ++t)
            for (unsigned r = 0; r < 14; ++r) f.frames[size_t(t) * 16 + r] = uint8_t(t * 7 + r);
        ymcore::RenderConfig rc;
        ymcore::ChipRenderer r;
        r.prepare(rc);
        ymio::YmPlayer p(f, ymio::YmTiming::AtariAudio, rc.hostRate);
        std::vector<float> buf(512);
        g_allocs = 0;
        g_watch = true;
        STYM_RT_ENTER();
        for (int i = 0; i < 1000; ++i) p.render(r, buf.data(), 512);
        STYM_RT_EXIT();
        g_watch = false;
        total += g_allocs;
        if (g_allocs != 0) ok = false;
    }
    // YM player with timer effects (SID, Digidrum, Sync Buzzer), two models, jitter included.
    for (ymio::YmFxModel fx : {ymio::YmFxModel::Exact, ymio::YmFxModel::AtariAudio}) {
        ymio::YmFile f;
        f.format = ymio::YmFormat::YM6;
        f.frameCount = 300;
        f.attributes = 4;  // samples already in 4-bit volumes
        f.digidrums.assign(1, std::vector<uint8_t>(2000));
        for (size_t i = 0; i < f.digidrums[0].size(); ++i) f.digidrums[0][i] = uint8_t(i & 15);
        f.frames.assign(size_t(f.frameCount) * ymio::YmFile::kStride, 0);
        for (uint32_t t = 0; t < f.frameCount; ++t) {
            uint8_t* reg = &f.frames[size_t(t) * ymio::YmFile::kStride];
            for (unsigned r = 0; r < 14; ++r) reg[r] = uint8_t(t * 7 + r);
            reg[1] = (t % 3 == 2) ? 0xd0 : 0x10;  // slot 1: SID or Sync Buzzer on channel A
            reg[6] = uint8_t(0x20 | (reg[6] & 0x1f));
            reg[14] = uint8_t(20 + t % 50);
            reg[3] = 0x70;  // slot 2: digidrum on channel C
            reg[8] = uint8_t(0x20 | (reg[8] & 0x1f));
            reg[10] = 0;
            reg[15] = 30;
        }
        ymcore::RenderConfig rc;
        ymcore::ChipRenderer r;
        r.prepare(rc);
        ymio::YmPlayer p(f, ymio::YmPlayerOptions{ymio::YmTiming::Exact, fx, 96, 1}, rc.hostRate);
        std::vector<float> buf(512);
        g_allocs = 0;
        g_watch = true;
        STYM_RT_ENTER();
        for (int i = 0; i < 600; ++i) p.render(r, buf.data(), 512);
        STYM_RT_EXIT();
        g_watch = false;
        total += g_allocs;
        if (g_allocs != 0 || p.timerInterrupts() == 0) ok = false;
    }
    // Voice engine (M2): three voice modes, MFP and continuous timer effects, arpeggio, live mixer, settings changed
    // from the audio thread, event queue overflow. M3: separate outputs of the tracks and chips, notes, bend and mod
    // wheel per track, velocity effect, bass and treble of the STE chain.
    {
        long engineAllocs = 0;
        std::vector<uint8_t> nibbles(3000);
        for (size_t i = 0; i < nibbles.size(); ++i) nibbles[i] = uint8_t((i * 5) & 15);
        for (ymcore::OutputMode mode : {ymcore::OutputMode::Raw, ymcore::OutputMode::RawBox, ymcore::OutputMode::Retro}) {
            for (uint32_t rate : {44100u, 48000u, 96000u}) {
                ymengine::EngineConfig cfg;
                cfg.render.mode = mode;
                cfg.render.hostRate = rate;
                cfg.render.board.chain = rate == 48000 ? ymcore::BoardChain::ST : rate == 96000 ? ymcore::BoardChain::STE : ymcore::BoardChain::Raw;
                cfg.trackOutputs = rate == 48000 ? uint16_t(0xffff) : rate == 96000 ? uint16_t(0x00f3) : uint16_t(0);
                ymengine::Engine e;
                e.prepare(cfg);  // outside the real-time section: may allocate
                std::vector<float> l(1024), r(1024);
                std::vector<std::vector<float>> sep(ymengine::kTracks + 2, std::vector<float>(1024));
                ymengine::Outputs out;
                out.left = l.data();
                out.right = r.data();
                for (int t = 0; t < ymengine::kTracks; ++t)
                    if (t % 4 != 2) out.track[size_t(t)] = sep[size_t(t)].data();  // a few outputs not wanted
                out.chip[0] = sep[ymengine::kTracks].data();
                out.chip[1] = sep[ymengine::kTracks + 1].data();
                ymengine::TrackParams tracks[ymengine::kTracks];
                for (int t = 0; t < ymengine::kTracks; ++t) {
                    ymengine::TrackParams& p = tracks[t];
                    p.mix.enabled = true;
                    p.mix.pan = (t % 3 - 1) * 0.5;
                    p.patch.generator = ymengine::Generator(t % 5);
                    p.patch.noiseOn = t % 2 == 0;
                    p.patch.envelopeVolume = t % 7 == 3;
                    p.patch.mono = t % 6 == 5;
                    p.patch.vibratoCents = 30;
                    p.patch.ornamentOn = t % 3 == 0;
                    p.patch.attackMs = 3;
                    p.patch.decayMs = 40;
                    p.patch.sustainPct = 60;
                    p.patch.releaseMs = 80;
                    p.patch.adsrChip = t % 4 == 1;
                    p.arp.on = t == 0;
                    p.arp.mode = ymengine::ArpMode::UpDown;
                    p.arp.octaves = 2;
                }
                uint32_t rng = 12345;
                auto next = [&]() {
                    rng ^= rng << 13;
                    rng ^= rng >> 17;
                    rng ^= rng << 5;
                    return rng;
                };
                g_allocs = 0;
                g_watch = true;
                STYM_RT_ENTER();
                e.reset();
                for (int s = 0; s < ymengine::kDigidrumSlots; ++s) e.setUserSample(s, nibbles);
                e.setKit(ymengine::kKitBanks[3].kit);
                for (int t = 0; t < ymengine::kTracks; ++t) e.setTrack(t, tracks[t]);
                int pass = 0;
                for (ymengine::VoiceMode vm : {ymengine::VoiceMode::Studio16, ymengine::VoiceMode::Authentic3, ymengine::VoiceMode::Dual3}) {
                    for (ymengine::TimerMode tm : {ymengine::TimerMode::Mfp, ymengine::TimerMode::Free}) {
                        ymengine::GlobalParams g;
                        g.mode = vm;
                        g.timer = tm;
                        g.layer = (pass & 1) != 0;
                        g.vblHz = pass % 3 == 2 ? 60 : 50;
                        g.irqJitterCpuCycles = tm == ymengine::TimerMode::Mfp ? 16 : 0;
                        g.arpClock = pass % 4 == 3 ? ymengine::ArpClock::MidiClock : ymengine::ArpClock::Hz;
                        g.velocity = ymengine::VelocityMode(pass % 3);
                        g.bendTarget = ymengine::BendTarget(pass % 3);  // MIDI expression (D-432)
                        g.wheelTarget = ymengine::ExpressTarget(pass % 5);
                        g.aftertouch = ymengine::ExpressTarget((pass + 2) % 5);
                        g.mpeSlide = ymengine::SlideTarget(pass % 3);
                        e.setGlobal(g);
                        ++pass;
                        for (int b = 0; b < 200; ++b) {
                            const uint32_t n = 64 + next() % 961;
                            for (int k = 0; k < 4; ++k) {
                                const uint32_t off = next() % n;
                                const int note = 36 + int(next() % 48);
                                const int track = (k & 1) ? int(next() % 17) - 1 : -1;  // -1: keyboard playing
                                const int ch = int(next() % 4);  // MPE member channel (0: none)
                                if (next() % 3) e.noteOn(off, note, 1 + int(next() % 127), track, ch);
                                else e.noteOff(off, note, track, ch);
                            }
                            if (b % 7 == 0) e.pitchBend(next() % n, double(int(next() % 9) - 4) * 0.5, int(next() % 17) - 1);
                            if (b % 9 == 0) e.modWheel(next() % n, double(next() % 128) / 127, int(next() % 17) - 1);
                            if (b % 19 == 0)
                                e.channelPressure(next() % n, double(next() % 128) / 127, int(next() % 17) - 1);
                            if (b % 23 == 0) e.mpeBend(next() % n, int(next() % 16), double(int(next() % 97) - 48));
                            if (b % 29 == 0) e.mpeSlide(next() % n, int(next() % 16), double(next() % 128) / 127);
                            if (b % 17 == 0) e.setBoardTone(int(next() % 25) - 12, int(next() % 25) - 12);
                            if (b % 11 == 0) e.setTrackMix(next() % n, int(next() % 16), tracks[next() % 16].mix);
                            if (b % 13 == 0) e.midiClock(next() % n);
                            if (b % 37 == 0) e.releaseManual(next() % n);
                            if (b % 53 == 0) e.allNotesOff(next() % n);
                            if (b & 1) e.render(out, n);
                            else e.render(l.data(), r.data(), n);
                        }
                        // Event queue overflow.
                        for (uint32_t k = 0; k < ymengine::Engine::kMaxEvents + 100; ++k) e.noteOn(k % 256, 40 + int(k % 40), 100);
                        e.render(l.data(), r.data(), 256);
                        e.allNotesOff(0);
                        e.render(l.data(), r.data(), 1024);
                    }
                }
                STYM_RT_EXIT();
                g_watch = false;
                engineAllocs += g_allocs;
                if (g_allocs != 0) {
                    ok = false;
                    std::printf("engine: allocations in mode %d at %u Hz: %ld\n", int(mode), rate, g_allocs.load());
                }
            }
        }
        total += engineAllocs;
    }
    {
        // Plugin driver (M3): MIDI, prototype controllers, automation, mode changes, new engine preparations
        // (prepared outside the section, like the message thread), published settings.
        using namespace stym::plugin;
        struct Sink : ParamListener {
            long calls = 0;
            void parameterChangedByMidi(int, float) noexcept override { ++calls; }
        } sink;
        uint32_t rng = 777;
        auto next = [&]() {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return rng;
        };
        long driverAllocs = 0;
        // YM Player (M7): YM3 dump loaded, played, rewound, stopped then closed by the message thread, while the
        // automation changes the chain and the resampling (playback resumed on a new chip).
        std::vector<uint8_t> ym3 = {'Y', 'M', '3', '!'};
        ym3.resize(4 + 14 * 400);
        for (size_t f = 0; f < 400; ++f) {
            ym3[4 + 1 * 400 + f] = 1;
            ym3[4 + 7 * 400 + f] = 0x3e;
            ym3[4 + 8 * 400 + f] = uint8_t(f & 15);
        }
        const std::shared_ptr<const YmTune> ymTune = loadYmTune(ym3, "rt.ym").tune;
        if (!ymTune) {
            ok = false;
            std::printf("driver: YM3 dump refused\n");
        }
        for (uint32_t rate : {44100u, 96000u}) {
            Driver d;
            ParamValues v = defaultValues();
            for (int t = 0; t < 16; ++t) v[size_t(paramIndex(t, Track::On))] = 1;
            Snapshot snap;
            for (auto& smp : snap.state.digidrums) smp.assign(2000, 7);
            d.prepare({rate, uint16_t(rate == 44100 ? 0xffff : 0)}, v, snap);
            std::vector<float> l(1024), r(1024);
            std::vector<std::vector<float>> sep(16, std::vector<float>(1024));
            ymengine::Outputs out;
            out.left = l.data();
            out.right = r.data();
            if (rate == 44100)
                for (int t = 0; t < 16; ++t) out.track[size_t(t)] = sep[size_t(t)].data();
            std::vector<MidiEvent> midi;
            midi.reserve(64);
            const int ccs[] = {1, 7, 70, 71, 72, 73, 74, 75, 76, 77, 78, 80, 81, 84, 120, 121, 123};
            for (int b = 0; b < 1500; ++b) {
                const uint32_t n = 32 + next() % 993;
                midi.clear();
                const int count = int(next() % 12);
                uint32_t at = 0;
                for (int k = 0; k < count; ++k) {
                    at = std::min(n - 1, at + next() % (n / 4 + 1));
                    const uint8_t ch = uint8_t(next() % 16);
                    switch (next() % 6) {
                    case 0:
                    case 1: midi.push_back({at, 3, {uint8_t(0x90 | ch), uint8_t(30 + next() % 60), uint8_t(next() % 128)}}); break;
                    case 2: midi.push_back({at, 3, {uint8_t(0x80 | ch), uint8_t(30 + next() % 60), 0}}); break;
                    case 3: midi.push_back({at, 3, {uint8_t(0xb0 | ch), uint8_t(ccs[next() % 17]), uint8_t(next() % 128)}}); break;
                    case 4: midi.push_back({at, 3, {uint8_t(0xe0 | ch), uint8_t(next() % 128), uint8_t(next() % 128)}}); break;
                    default: midi.push_back({at, 1, {0xf8, 0, 0}}); break;
                    }
                }
                // Host automation (between two blocks).
                if (b % 5 == 0) {
                    const int i = int(next() % uint32_t(kParamCount));
                    const ParamSpec& sp = paramSpec(i);
                    v[size_t(i)] = clampToSpec(sp, fromNormalized(sp, float(next() % 1001) / 1000.0f));
                }
                if (b % 97 == 0) v[size_t(paramIndex(Global::VoiceMode))] = float(next() % 3);
                if (b % 211 == 0) v[size_t(paramIndex(Global::ChipClock))] = float(next() % 4);
                if (b % 307 == 0) v[size_t(paramIndex(Global::Resampling))] = float(next() % 3);
                g_allocs = 0;
                g_watch = true;
                STYM_RT_ENTER();
                d.process(v, midi, out, n, false, &sink);
                STYM_RT_EXIT();
                g_watch = false;
                driverAllocs += g_allocs;
                if (g_allocs != 0 && driverAllocs == g_allocs)
                    std::printf("driver: allocations at block %d at %u Hz: %ld\n", b, rate, g_allocs.load());
                // Message thread: prepares the requested engines, publishes settings now and then.
                if (b % 3 == 0) d.service(v);
                if (b == 200 && ymTune) {
                    d.ymLoad(ymTune);
                    d.ymPlay(true);
                }
                if (b == 700) d.ymRewind();
                if (b == 1100) d.ymPlay(false);
                if (b == 1150) d.ymPlay(true);
                if (b == 1399 && d.ymStatus().frames == 0) {
                    ok = false;
                    std::printf("driver: YM Player silent at %u Hz\n", rate);
                }
                if (b == 1400) d.ymClose();
                if (b % 101 == 0) {
                    snap.kitVersion++;
                    snap.state.kit = ymengine::kKitBanks[next() % 13].kit;
                    snap.sampleVersion[next() % 8]++;
                    d.publish(snap);
                }
            }
            if (d.engineSwaps() == 0) {
                ok = false;
                std::printf("driver: no engine swap at %u Hz\n", rate);
            }
        }
        if (driverAllocs != 0) ok = false;
        total += driverAllocs;
    }
#if STYM_RTSAN
    test::metric("S11", "RealtimeSanitizer: locks, system calls, allocations (3 modes × 3 rates, Raw/ST/STE chains, + YM player, effects included, + voice engine and plugin driver, YM Player included)",
                 "no violation", "0", ok);
#else
    test::metric("S11", "allocations during reset/write/render (3 modes × 3 rates, Raw/ST/STE chains, + YM player, effects included, + M2 voice engine and M3 additions, + plugin driver and YM Player)", std::to_string(total), "0", ok);
#endif
    return ok ? 0 : 1;
}
