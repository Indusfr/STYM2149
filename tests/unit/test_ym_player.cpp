// STYM2149 — YM Player in the plugin (M7, A2, D-169): files read and refused, playback in place of the engine, stop
// and resume, Rewind, subsong, close, loop, resume on a new chip, SNDH and crashed routine.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "SndhBuild.h"
#include "plugincore/Driver.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "plugincore/YmTune.h"

using namespace stym;
using namespace stym::plugin;
using namespace stym::test;

namespace {

// YM3 (YM3b with `loop`): 14 interleaved registers; channel A at period 256, R8 = frame & 15, R13 without restart.
std::vector<uint8_t> ym3(uint32_t frames, int loop = -1) {
    std::vector<uint8_t> d = {'Y', 'M', '3', loop >= 0 ? uint8_t('b') : uint8_t('!')};
    std::vector<uint8_t> regs(size_t(14) * frames, 0);
    for (uint32_t f = 0; f < frames; ++f) {
        auto at = [&](unsigned r) -> uint8_t& { return regs[size_t(r) * frames + f]; };
        at(1) = 1;
        at(7) = 0x3e;
        at(8) = uint8_t(f & 15);
        at(13) = 0xff;
    }
    d.insert(d.end(), regs.begin(), regs.end());
    if (loop >= 0)
        for (int s = 0; s < 4; ++s) d.push_back(uint8_t(uint32_t(loop) >> (8 * s)));  // little-endian (YM3b)
    return d;
}

// Non-interleaved YM6, without digidrums: clock and rate to choose.
std::vector<uint8_t> ym6(uint32_t clock, uint16_t rate, uint32_t frames) {
    std::vector<uint8_t> d = {'Y', 'M', '6', '!', 'L', 'e', 'O', 'n', 'A', 'r', 'D', '!'};
    auto be = [&](uint32_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; --i) d.push_back(uint8_t(v >> (8 * i)));
    };
    be(frames, 4);
    be(0, 4);  // attributes: not interleaved
    be(0, 2);  // digidrums
    be(clock, 4);
    be(rate, 2);
    be(0, 4);  // loop frame
    be(0, 2);  // additional data
    for (const char* s : {"Tune", "Someone", ""}) {
        d.insert(d.end(), s, s + std::char_traits<char>::length(s));
        d.push_back(0);
    }
    for (uint32_t f = 0; f < frames; ++f)
        for (unsigned r = 0; r < 16; ++r) d.push_back(r == 7 ? 0x3e : r == 8 ? 12 : r == 13 ? 0xff : 0);
    return d;
}

std::shared_ptr<const YmTune> load(const std::vector<uint8_t>& d, const char* name) {
    YmTuneLoad r = loadYmTune(d, name);
    INFO(r.error);
    REQUIRE(r.tune);
    return r.tune;
}

// Driver in 3 voices, one block at a time.
struct Rig {
    Driver d;
    ParamValues v = defaultValues();
    std::vector<float> l, r;
    Rig() {
        v[size_t(paramIndex(Global::VoiceMode))] = 1.0f;
        d.prepare({48000, 0}, v, Snapshot{});
    }
    void run(uint32_t n, std::vector<MidiEvent> midi = {}, bool offline = false) {
        l.assign(n, 0);
        r.assign(n, 0);
        ymengine::Outputs o;
        o.left = l.data();
        o.right = r.data();
        d.process(v, midi, o, n, offline, nullptr);
    }
    void blocks(int count, bool offline = false) {
        for (int i = 0; i < count; ++i) run(480, {}, offline);
    }
    bool silent() const {
        for (float x : l)
            if (x != 0.0f) return false;
        return true;
    }
    bool manualHeld() const {
        for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
            const ymengine::Engine::VoiceInfo vi = d.engine().voice(i);
            if (vi.on && vi.tag == ymengine::VoiceTag::Manual && !vi.fading && vi.adsrStage != 4) return true;
        }
        return false;
    }
};

MidiEvent noteOn(uint8_t n) { return MidiEvent{0, 3, {0x90, n, 100}}; }

// Expected output of the tune: its chip at the project's settings, engine output law in 3 voices.
std::vector<float> expected(const std::shared_ptr<const YmTune>& tune, const ParamValues& v, uint32_t n) {
    YmPlayback p(tune, 1, engineConfig(configKey(v), v, 48000, 0).render);
    std::vector<float> out(n);
    p.render(out.data(), n, true);
    const double g = 32768.0 / double(ymcore::tables::kMixTable[31]) * double(value(v, Global::Master)) / 100.0 * 1.2;
    for (float& x : out) x = float(std::tanh(double(x) * g));
    return out;
}

}  // namespace

TEST_CASE("YM Player: files read and refused", "[ymplayer]") {
    const auto t = load(ym3(100), "song.ym");
    CHECK(t->displayName() == "song.ym  -  YM3!");
    CHECK(t->subsongs() == 1);
    CHECK(t->frameRate() == 50);
    CHECK(t->frames(1) == 100);
    CHECK(t->clock().hz() == 2000000.0);

    const auto y6 = load(ym6(1773400, 50, 10), "zx.ym");
    CHECK(y6->displayName() == "zx.ym  -  YM6! - Tune (Someone)");
    CHECK(y6->clock().hz() == 1773400.0);

    SndhSource s;
    s.subsongs = 3;
    s.title = "Square";
    const auto sn = load(buildSndh(s), "a.sndh");
    CHECK(sn->displayName() == "a.sndh  -  SNDH - Square");
    CHECK(sn->subsongs() == 3);
    CHECK(sn->frames(1) == 0);  // neither TIME nor FRMS

    CHECK(loadYmTune(std::vector<uint8_t>(64, 'x'), "x").error == "Unsupported file");
    CHECK(loadYmTune(std::vector<uint8_t>{'Y', 'M', '3', '!'}, "x").error == "The YM file is damaged.");
    CHECK(loadYmTune(ym6(20000000, 50, 4), "x").error == "Unsupported chip clock: 20000000 Hz.");
    CHECK(loadYmTune(ym6(50000, 50, 4), "x").error == "Unsupported chip clock: 50000 Hz.");
    std::vector<uint8_t> bad = buildSndh(SndhSource{});
    bad.resize(14);
    CHECK_FALSE(loadYmTune(bad, "x").error.empty());
}

TEST_CASE("YM Player: the tune replaces the engine output", "[ymplayer]") {
    Rig g;
    g.run(480, {noteOn(60)});
    g.blocks(4);
    CHECK_FALSE(g.silent());
    CHECK(g.manualHeld());

    const auto tune = load(ym3(200), "song.ym");
    g.d.ymLoad(tune);
    CHECK(g.d.ymTune() == tune);
    CHECK_FALSE(g.d.ymPlaying());
    g.blocks(2);  // loaded, stopped: the engine is still heard
    CHECK(g.manualHeld());

    g.d.ymPlay(true);
    const uint32_t n = 4800;
    std::vector<float> got;
    for (int b = 0; b < 10; ++b) {
        g.run(n / 10);
        got.insert(got.end(), g.l.begin(), g.l.end());
        CHECK(g.l == g.r);
    }
    CHECK(got == expected(tune, g.v, n));
    // Start of playback: manual playing released (killTag('man')).
    CHECK_FALSE(g.manualHeld());
    const Driver::YmStatus st = g.d.ymStatus();
    CHECK(st.frames == 5);  // 0.1 s at 50 Hz: frames 0 to 4 (the 5th starts at sample 4 800, not played yet)
    CHECK(st.position == 4);
    CHECK(st.regs[1] == 1);
    CHECK(st.regs[7] == 0x3e);
    CHECK(st.regs[8] == 4);

    // Stop: the engine comes back; playback resumes where it stopped.
    g.d.ymPlay(false);
    g.blocks(3);
    CHECK(g.d.ymStatus().frames == 5);
    g.d.ymPlay(true);
    g.blocks(10);
    CHECK(g.d.ymStatus().frames == 10);
}

TEST_CASE("YM Player: separate outputs silent during playback", "[ymplayer]") {
    Driver d;
    ParamValues v = defaultValues();
    v[size_t(paramIndex(Global::VoiceMode))] = 1.0f;
    d.prepare({48000, 0b11}, v, Snapshot{});
    d.ymLoad(load(ym3(50), "s.ym"));
    d.ymPlay(true);
    std::vector<float> l(480), r(480), c0(480, 1.0f), t0(480, 1.0f);
    ymengine::Outputs o;
    o.left = l.data();
    o.right = r.data();
    o.chip[0] = c0.data();
    o.track[0] = t0.data();
    d.process(v, {}, o, 480, false, nullptr);
    for (size_t i = 0; i < 480; ++i) {
        REQUIRE(c0[i] == 0.0f);
        REQUIRE(t0[i] == 0.0f);
    }
}

TEST_CASE("YM Player: Rewind, loop, close", "[ymplayer]") {
    Rig g;
    // YM3b of 20 frames that loops on frame 15.
    g.d.ymLoad(load(ym3(20, 15), "loop.ym"));
    g.d.ymPlay(true);
    g.blocks(50);  // 0.5 s: 25 frames, the last (24) replays frame 19
    CHECK(g.d.ymStatus().frames == 25);
    CHECK(g.d.ymStatus().position == 15 + (24 - 20) % 5);
    g.d.ymRewind();
    g.blocks(2);
    CHECK(g.d.ymPlaying());
    CHECK(g.d.ymStatus().frames == 1);
    CHECK(g.d.ymStatus().position == 0);
    g.d.ymClose();
    CHECK_FALSE(g.d.ymTune());
    CHECK_FALSE(g.d.ymPlaying());
    g.blocks(2);
    g.d.service(g.v);  // returned playbacks destroyed by the message thread
    CHECK(g.silent());
}

// Output chain changed during playback: new chip, playback continues without going back to the start.
TEST_CASE("YM Player: resume on a new chip", "[ymplayer]") {
    Rig g;
    g.d.ymLoad(load(ym3(500), "long.ym"));
    g.d.ymPlay(true);
    g.blocks(20);
    const uint64_t before = g.d.ymStatus().frames;
    CHECK(before == 10);
    g.v[size_t(paramIndex(Global::OutputChain))] = 2.0f;  // STE
    g.d.service(g.v);
    g.blocks(20);
    CHECK(g.d.ymStatus().frames == 20);
    CHECK(g.d.ymStatus().regs[8] == 19 % 16);
    // Rate changed (prepare): playback continues too.
    g.d.prepare({44100, 0}, g.v, Snapshot{});
    for (int i = 0; i < 10; ++i) g.run(441);
    CHECK(g.d.ymStatus().frames == 25);
    CHECK(g.d.ymPlaying());
}

TEST_CASE("YM Player: a new chip posted while the audio is stopped resumes at the next preparation", "[ymplayer]") {
    Rig g;
    g.d.ymLoad(load(ym3(500), "long.ym"));
    g.d.ymPlay(true);
    g.blocks(20);
    REQUIRE(g.d.ymStatus().frames == 10);
    // Output chain changed with no block in between (host suspended), then the host prepares again: the tune goes
    // on where it stopped instead of starting again from frame 0.
    g.v[size_t(paramIndex(Global::OutputChain))] = 2.0f;  // STE
    g.d.service(g.v);
    g.d.prepare({44100, 0}, g.v, Snapshot{});
    for (int i = 0; i < 10; ++i) g.run(441);
    CHECK(g.d.ymStatus().frames == 15);
    CHECK(g.d.ymStatus().regs[8] == 14 % 16);
}

TEST_CASE("YM Player: SNDH, subsong and crashed routine", "[ymplayer][sndh]") {
    Rig g;
    // play: R8 = tick counter; init: counter = subsong × 16 (d0).
    SndhSource s;
    s.subsongs = 2;
    s.init = {0x11c0, 0x1000, 0xe908, 0x11c0, 0x1000};  // move.b d0,$1000.w ; lsl.b #4,d0 ; move.b d0,$1000.w
    s.play = {0x5238, 0x1000};
    moveByte(s.play, 8, 0x8800);
    s.play.insert(s.play.end(), {0x11f8, 0x1000, 0x8802});
    g.d.ymLoad(load(buildSndh(s), "s.sndh"));
    g.d.ymPlay(true);
    g.blocks(30, true);  // 0.3 s, offline: the capture is waited for
    const Driver::YmStatus a = g.d.ymStatus();
    CHECK(a.frames == 16);
    CHECK(a.regs[8] == ((16 + 16) & 0x1f));
    g.d.ymSubsong(2);
    CHECK(g.d.ymSubsong() == 2);
    g.blocks(2, true);
    CHECK(g.d.ymStatus().regs[8] == ((32 + 1) & 0x1f));
    CHECK(g.d.ymPlaying());

    SndhSource crash = s;
    crash.play.push_back(0x4afc);  // illegal after the write
    g.d.ymLoad(load(buildSndh(crash), "crash.sndh"));
    g.d.ymPlay(true);
    for (int i = 0; i < 100 && g.d.ymPlaying(); ++i) g.blocks(1, true);
    CHECK_FALSE(g.d.ymPlaying());
    CHECK(g.d.ymStatus().failed);
}
