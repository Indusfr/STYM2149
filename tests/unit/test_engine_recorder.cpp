// STYM2149 — register recorder (M6, B11, D-157): one frame per replay tick, registers of the chip at the start of
// the frame, R13 retriggered, muted channels, timer effects, end of a take.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using ymengine::DmaFrame;
using ymengine::Engine;
using ymengine::Generator;
using ymengine::RegisterFrame;

namespace {

using Log = std::vector<Engine::WriteRecord>;

struct Rig {
    Engine e;
    Log log;
    std::vector<float> l = std::vector<float>(4096), r = std::vector<float>(4096);
    std::vector<RegisterFrame> frames = std::vector<RegisterFrame>(4096);

    explicit Rig(ymengine::VoiceMode mode, uint32_t vbl = 50, ymengine::TimerMode timer = ymengine::TimerMode::Mfp) {
        ymengine::EngineConfig cfg;
        cfg.render.hostRate = 48000;  // default clock: 2 MHz, 125 000 / 3 cycles per sample
        e.prepare(cfg);
        ymengine::GlobalParams g;
        g.mode = mode;
        g.vblHz = vbl;
        g.timer = timer;
        e.setGlobal(g);
        log.reserve(1 << 20);
        e.setWriteLog(&log);
    }
    void run(uint32_t n) {
        while (n > 0) {
            const uint32_t k = std::min<uint32_t>(n, uint32_t(l.size()));
            e.render(l.data(), r.data(), k);
            n -= k;
        }
    }
};

ymengine::TrackParams track(Generator g = Generator::Chip, bool env = false) {
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.mix.level = 1;
    t.patch.generator = g;
    t.patch.envelopeVolume = env;
    return t;
}

// Expected frames: the log of the chip writes replayed up to the cycle of each frame (writes of that cycle
// included), R13 retriggered by a write dated in the previous frame.
struct Expected {
    std::array<uint8_t, 14> reg{};
    bool r13 = false;
};
std::vector<Expected> replay(const Log& log, uint8_t chip, uint64_t start, uint64_t period, uint32_t count) {
    std::vector<Expected> out(count);
    std::array<uint8_t, 14> reg{};
    size_t i = 0;
    for (uint32_t n = 0; n < count; ++n) {
        const uint64_t at = start + n * period;
        bool r13 = false;
        for (; i < log.size() && log[i].cycle <= at; ++i) {
            if (log[i].chip != chip) continue;
            reg[log[i].reg] = log[i].value;
            if (log[i].reg == 13 && (n == 0 ? log[i].cycle >= start : log[i].cycle > at - period)) r13 = true;
        }
        out[n].reg = reg;
        out[n].r13 = r13;
    }
    return out;
}

}  // namespace

TEST_CASE("Recorder: one frame per replay tick, registers of the chip at the start of each frame",
          "[engine][3ch][recorder]") {
    for (const uint32_t vbl : {50u, 200u}) {
        CAPTURE(vbl);
        Rig g(ymengine::VoiceMode::Authentic3, vbl);
        g.e.setTrack(0, track());
        g.e.setTrack(1, track(Generator::Chip, true));
        g.e.noteOn(0, 60, 100, 0);
        g.run(4800);  // 200 000 cycles: start on a known cycle
        REQUIRE_FALSE(g.e.capturing());
        g.e.startCapture(0, g.frames);
        REQUIRE(g.e.capturing());
        CHECK(g.e.captureHz() == vbl);
        // Writes dated at the start included in the first frame: note at sample 0 of the next render.
        g.e.noteOn(0, 64, 100, 1);
        g.e.noteOff(1000, 60, 0);
        g.e.noteOn(9000, 67, 100, 0);
        g.run(48000);
        // One second: vbl frames (the frame dated at the end of the render waits for what follows).
        const uint32_t n = g.e.capturedFrames();
        CHECK(n == vbl);
        const uint64_t period = 2'000'000 / vbl;
        const auto want = replay(g.log, 0, 200'000, period, n);
        bool sawR13 = false, sawTone = false;
        for (uint32_t i = 0; i < n; ++i) {
            CAPTURE(i);
            REQUIRE(g.frames[i].reg == want[i].reg);
            sawR13 = sawR13 || g.frames[i].r13;
            sawTone = sawTone || g.frames[i].reg[2] != 0;
            // First frame: the envelope channel is in it, R13 must be written by a player.
            REQUIRE(g.frames[i].r13 == (want[i].r13 || i == 0));
        }
        CHECK(sawR13);
        CHECK(sawTone);
        CHECK((g.frames[0].reg[9] & 0x10) != 0);  // channel B at envelope volume from the first frame
        CHECK(g.e.stopCapture() == n);
        CHECK_FALSE(g.e.capturing());
        g.run(4800);
        CHECK(g.e.capturedFrames() == n);  // nothing more after the stop
    }
}

TEST_CASE("Recorder: muted channels at zero volume, timer effects described", "[engine][3ch][recorder]") {
    for (const auto timer : {ymengine::TimerMode::Mfp, ymengine::TimerMode::Free}) {
        CAPTURE(int(timer));
        Rig g(ymengine::VoiceMode::Authentic3, 50, timer);
        g.e.setTrack(0, track(Generator::Sid));
        g.e.setTrack(1, track());
        g.e.setTrack(2, track(Generator::SyncBuzzer));
        g.e.startCapture(0, g.frames);
        g.e.noteOn(0, 57, 127, 0);
        g.e.noteOn(0, 60, 127, 1);
        g.e.noteOn(0, 48, 127, 2);
        g.run(9600);
        REQUIRE(g.e.capturedFrames() == 10);
        const RegisterFrame& f = g.frames[5];
        const ymengine::MfpSetting sid = ymengine::mfpQuantize(g.e.voice(0).timerActualHz);
        CHECK(f.fx[0].on);
        CHECK(f.fx[0].gen == Generator::Sid);
        CHECK(f.fx[0].value == 13);  // high volume: that of the note
        CHECK(f.fx[0].timer == sid);
        CHECK_FALSE(f.fx[1].on);     // tone channel: no effect
        CHECK(f.fx[2].on);
        CHECK(f.fx[2].gen == Generator::SyncBuzzer);
        CHECK(f.fx[2].value == 8);   // R13 shape of the track
        CHECK(f.fx[2].timer.running());
        CHECK(f.reg[9] != 0);
        // Start of the effects: at the first frame, then no more while the timer keeps running.
        CHECK(g.frames[0].fx[0].restart);
        CHECK(g.frames[0].fx[2].restart);
        CHECK_FALSE(g.frames[0].fx[1].restart);
        CHECK_FALSE(f.fx[0].restart);
        CHECK_FALSE(f.fx[2].restart);

        // Mute of track 2 (channel B): zero volume in the following frames, as heard.
        ymengine::TrackMix mute = track().mix;
        mute.mute = true;
        g.e.setTrackMix(0, 1, mute);
        g.run(9600);
        REQUIRE(g.e.capturedFrames() == 20);
        for (uint32_t i = 11; i < 20; ++i) {
            CAPTURE(i);
            REQUIRE(g.frames[i].reg[9] == 0);
            REQUIRE(g.frames[i].fx[0].on);
        }
        // New note on channel A during frame 20: its timer restarts, marked in frame 21 only.
        g.e.noteOn(100, 59, 127, 0);
        g.run(2880);
        REQUIRE(g.e.capturedFrames() == 23);
        CHECK_FALSE(g.frames[20].fx[0].restart);
        CHECK(g.frames[21].fx[0].restart);
        CHECK_FALSE(g.frames[22].fx[0].restart);
        CHECK_FALSE(g.frames[21].fx[2].restart);
    }
}

TEST_CASE("Recorder: chosen chip, end of a take (room, mode), nothing in 16-channel mode", "[engine][recorder]") {
    // 16-channel: no registers.
    Rig s(ymengine::VoiceMode::Studio16);
    s.e.startCapture(0, s.frames);
    CHECK_FALSE(s.e.capturing());

    // One chip: the second one does not exist.
    Rig one(ymengine::VoiceMode::Authentic3);
    one.e.startCapture(1, one.frames);
    CHECK_FALSE(one.e.capturing());

    // Dual chip: track 5 plays on the second one, which the take follows alone.
    Rig d(ymengine::VoiceMode::Dual3);
    d.e.setTrack(0, track());
    d.e.setTrack(4, track());
    d.e.startCapture(1, std::span(d.frames).first(8));
    d.e.noteOn(0, 72, 100, 4);
    d.run(4800);
    CHECK(d.e.capturedFrames() == 5);
    CHECK((d.frames[0].reg[0] | d.frames[0].reg[1] << 8) != 0);
    CHECK((d.frames[0].reg[8] & 15) != 0);
    // Buffer full: the take stops by itself.
    d.run(48000);
    CHECK(d.e.capturedFrames() == 8);
    CHECK_FALSE(d.e.capturing());

    // Change of mode: the time base restarts, the take stops, its frames remain.
    Rig m(ymengine::VoiceMode::Authentic3);
    m.e.startCapture(0, m.frames);
    m.run(4800);
    REQUIRE(m.e.capturing());
    ymengine::GlobalParams g = m.e.global();
    g.mode = ymengine::VoiceMode::Studio16;
    m.e.setGlobal(g);
    CHECK_FALSE(m.e.capturing());
    CHECK(m.e.capturedFrames() == 5);
}

// STE DMA sound (D-162): a single channel, taken at its attack by the most recent DMA note and kept until its end;
// the older note that still sounds does not come back to it. Muted track: its voice no longer counts.
TEST_CASE("Recorder: STE DMA sound, channel taken by the most recent DMA note", "[engine][3ch][recorder][dma]") {
    Rig g(ymengine::VoiceMode::Authentic3);
    ymengine::GlobalParams gp = g.e.global();
    gp.ste = true;
    g.e.setGlobal(gp);
    ymengine::DmaParams p;
    p.slots[0].loop = false;  // 4 800 bytes at 25 033 Hz: 0.19 s, 9.6 frames
    p.slots[1].loop = true;
    g.e.setDma(p);
    const std::vector<int8_t> pcm(4800, 40);
    g.e.setDmaSample(0, pcm);
    g.e.setDmaSample(1, pcm);
    for (int t = 4; t < 6; ++t) g.e.setTrack(t, track());  // DMA tracks 1 and 2: slots 0 and 1
    std::vector<DmaFrame> dma(40);
    g.e.startCapture(0, g.frames, dma);
    REQUIRE(g.e.capturing());
    // Half frames: each event falls in the middle of a frame, which the next one shows.
    const auto run = [&](int halves) {
        for (int i = 0; i < halves; ++i) g.run(480);
    };
    run(5);                         // frames 0 to 2
    g.e.noteOn(0, 72, 127, 5);      // frame 3: slot 1, one octave higher
    run(6);                         // frames 3 to 5
    g.e.noteOn(0, 60, 127, 4);      // frame 6: slot 0 at its pitch
    run(29);                        // up to frame 19
    auto t = g.e.track(5);
    t.mix.mute = true;
    g.e.setTrack(5, t);
    run(20);
    REQUIRE(g.e.capturedFrames() >= 26);
    for (int k = 0; k < 3; ++k) {
        CAPTURE(k);
        CHECK(dma[size_t(k)] == DmaFrame{});
    }
    CHECK(dma[3].on);
    CHECK(dma[3].restart);
    CHECK(dma[3].slot == 1);
    CHECK(dma[3].voices == 1);
    CHECK(std::abs(dma[3].step - 2.0f) < 1e-3f);
    CHECK(dma[3].gain > 0.0f);
    CHECK(dma[3].gain <= 1.0f);
    for (int k = 4; k < 6; ++k) {
        CAPTURE(k);
        CHECK(dma[size_t(k)].on);
        CHECK_FALSE(dma[size_t(k)].restart);
        CHECK(dma[size_t(k)].slot == 1);
    }
    CHECK(dma[6].on);
    CHECK(dma[6].restart);
    CHECK(dma[6].slot == 0);
    CHECK(dma[6].voices == 2);
    CHECK(std::abs(dma[6].step - 1.0f) < 1e-3f);
    for (int k = 7; k < 15; ++k) {
        CAPTURE(k);
        CHECK(dma[size_t(k)].on);
        CHECK_FALSE(dma[size_t(k)].restart);
        CHECK(dma[size_t(k)].slot == 0);
        CHECK(dma[size_t(k)].voices == 2);
    }
    // End of slot 0 (frame 15 within one slice): channel free, the note of slot 1 still sounds.
    for (int k = 17; k < 20; ++k) {
        CAPTURE(k);
        CHECK_FALSE(dma[size_t(k)].on);
        CHECK(dma[size_t(k)].voices == 1);
    }
    // Track 6 muted.
    for (int k = 21; k < 26; ++k) {
        CAPTURE(k);
        CHECK(dma[size_t(k)] == DmaFrame{});
    }

    // ST machine: no DMA sound.
    Rig st(ymengine::VoiceMode::Authentic3);
    st.e.setDma(p);
    st.e.setDmaSample(0, pcm);
    st.e.setTrack(4, track(Generator::Dma));
    std::vector<DmaFrame> none(8);
    st.e.startCapture(0, st.frames, none);
    st.e.noteOn(0, 60, 127, 4);
    st.run(4800);
    for (const DmaFrame& d : none) CHECK(d == DmaFrame{});
}
