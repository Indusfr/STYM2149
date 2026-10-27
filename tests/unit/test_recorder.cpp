// STYM2149 — register recorder in the plugin: RECD chunk of the state, take armed by the message
// thread, started and stopped by playback (host transport, sequencer), stops (disarming, mode, engine).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "plugincore/Driver.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"

using namespace stym;
using namespace stym::plugin;
using RecState = Driver::RecorderState;

namespace {

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

RegisterTake sampleTake() {
    RegisterTake t;
    t.chip = 1;
    t.frameHz = 200;
    t.clockNum = 8'021'247;
    t.clockDen = 4;
    t.frames.resize(3);
    for (size_t i = 0; i < t.frames.size(); ++i) {
        ymengine::RegisterFrame& f = t.frames[i];
        for (size_t r = 0; r < f.reg.size(); ++r) f.reg[r] = uint8_t(i * 16 + r);
        f.r13 = i == 1;
    }
    t.frames[2].fx[1] = {true, ymengine::Generator::Digidrum, 5, {4, 256}, true};  // start of the effect
    t.frames[2].fx[2] = {true, ymengine::Generator::Sid, 13, {1, 37}};
    return t;
}

// Driver in 3 channels, track 1 active, adjustable host transport.
struct Rig {
    Driver d;
    ParamValues v = defaultValues();
    ymengine::Engine::HostTransport host;
    std::vector<float> l, r;
    explicit Rig(int mode = 1, Snapshot snap = {}) {
        v[size_t(paramIndex(Global::VoiceMode))] = float(mode);
        d.prepare({48000, 0}, v, snap);
    }
    void run(uint32_t n, std::vector<MidiEvent> midi = {}, bool offline = false) {
        l.assign(n, 0);
        r.assign(n, 0);
        ymengine::Outputs o;
        o.left = l.data();
        o.right = r.data();
        d.setHostTransport(host);
        d.process(v, midi, o, n, offline, nullptr);
    }
    // Blocks of 480 samples (100 per second).
    void blocks(int count) {
        for (int i = 0; i < count; ++i) run(480);
    }
};

MidiEvent note(uint32_t at, uint8_t n, uint8_t vel) { return MidiEvent{at, 3, {uint8_t(vel ? 0x90 : 0x80), n, vel}}; }

}  // namespace

TEST_CASE("Recorder: RECD chunk of the state, absent without a take, read back identically", "[recorder][state]") {
    PluginState def;
    CHECK_FALSE(hasChunk(encodeState(def), "RECD"));

    PluginState st;
    st.take = sampleTake();
    const std::vector<uint8_t> enc = encodeState(st);
    CHECK(hasChunk(enc, "RECD"));
    const DecodeResult back = decodeState(enc);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.take == st.take);
    CHECK(encodeState(back.state) == enc);

    // Truncated chunk: the take is ignored, the rest of the state is read back.
    std::vector<uint8_t> cut = enc;
    cut.resize(cut.size() - 10);
    for (size_t i = 0; i + 8 <= cut.size(); ++i)
        if (std::memcmp(cut.data() + i, "RECD", 4) == 0) {
            const uint32_t len = uint32_t(cut[i + 4] | cut[i + 5] << 8 | cut[i + 6] << 16 | uint32_t(cut[i + 7]) << 24) - 10;
            for (int k = 0; k < 4; ++k) cut[i + 4 + size_t(k)] = uint8_t(len >> (8 * k));
        }
    const DecodeResult partial = decodeState(cut);
    REQUIRE(partial.ok);
    CHECK(partial.state.take.empty());
}

TEST_CASE("Recorder: Zxx of the take in the RSYN chunk, absent without Zxx", "[recorder][state]") {
    PluginState st;
    st.take = sampleTake();
    CHECK_FALSE(hasChunk(encodeState(st), "RSYN"));
    REQUIRE(st.take.frames.size() == 3);
    st.take.frames[0].sync = 0;
    st.take.frames[2].sync = 0xFF;
    const std::vector<uint8_t> enc = encodeState(st);
    CHECK(hasChunk(enc, "RSYN"));
    const DecodeResult back = decodeState(enc);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.take == st.take);
    CHECK(back.state.take.frames[2].sync == 0xFF);
    CHECK(back.state.take.frames[1].sync == -1);
    CHECK(encodeState(back.state) == enc);
}

TEST_CASE("Recorder: take armed, started and stopped by the host transport", "[recorder][driver]") {
    Rig g;
    CHECK(g.d.recorderState() == RecState::Idle);
    REQUIRE(g.d.armRecorder(0));
    CHECK(g.d.recorderState() == RecState::Armed);
    CHECK_FALSE(g.d.armRecorder(0));  // already armed
    g.blocks(10);                     // the host does not play: nothing
    CHECK(g.d.recorderState() == RecState::Armed);
    CHECK(g.d.recordedFrames() == 0);

    g.host.playing = true;
    g.run(480, {note(0, 60, 100)});
    CHECK(g.d.recorderState() == RecState::Rolling);
    g.blocks(99);  // one second of playback in all
    CHECK(g.d.recordedFrames() >= 49);
    CHECK(g.d.takeRecording() == nullptr);  // running

    g.host.playing = false;
    g.blocks(1);
    CHECK(g.d.recorderState() == RecState::Idle);
    CHECK_FALSE(g.d.armRecorder(0));  // take not collected yet
    auto take = g.d.takeRecording();
    REQUIRE(take);
    CHECK(take->chip == 0);
    CHECK(take->frameHz == 50);
    CHECK(take->frames.size() == 50);  // one second at 50 Hz
    const ymcore::MasterClock clk = g.d.engine().chip(0).config().chip.clock;
    CHECK(take->clockNum == clk.num);
    CHECK(take->clockDen == clk.den);
    // The note played at the start is in the first frame: tone of channel A, volume.
    CHECK((take->frames[0].reg[0] | take->frames[0].reg[1] << 8) != 0);
    CHECK((take->frames[0].reg[8] & 15) != 0);
    CHECK(g.d.takeRecording() == nullptr);

    // One take per arming: the next playback records nothing.
    g.host.playing = true;
    g.blocks(10);
    CHECK(g.d.recorderState() == RecState::Idle);
    CHECK(g.d.takeRecording() == nullptr);
    CHECK(g.d.armRecorder(0));
}

TEST_CASE("Recorder: disarming before the start and during a take", "[recorder][driver]") {
    Rig g;
    // Not yet taken by the audio thread: handed back at once.
    REQUIRE(g.d.armRecorder(0));
    g.d.disarmRecorder();
    CHECK(g.d.recorderState() == RecState::Idle);
    CHECK(g.d.takeRecording() == nullptr);

    // Armed, taken by the audio thread: handed back empty at the next block.
    REQUIRE(g.d.armRecorder(0));
    g.blocks(1);
    g.d.disarmRecorder();
    g.blocks(1);
    CHECK(g.d.recorderState() == RecState::Idle);
    auto empty = g.d.takeRecording();
    REQUIRE(empty);
    CHECK(empty->frames.empty());

    // Running: the take stops and keeps its frames.
    REQUIRE(g.d.armRecorder(0));
    g.host.playing = true;
    g.blocks(50);
    g.d.disarmRecorder();
    g.blocks(1);
    CHECK(g.d.recorderState() == RecState::Idle);
    auto take = g.d.takeRecording();
    REQUIRE(take);
    CHECK(take->frames.size() == 25);
}

TEST_CASE("Recorder: 16 channels waits for 3-channel mode; mode or engine change, end of take",
          "[recorder][driver]") {
    Rig g(0);
    REQUIRE(g.d.armRecorder(0));
    g.host.playing = true;
    g.blocks(10);
    CHECK(g.d.recorderState() == RecState::Armed);  // no registers in 16 channels
    g.v[size_t(paramIndex(Global::VoiceMode))] = 1;
    g.blocks(10);  // mode change fade, then start
    CHECK(g.d.recorderState() == RecState::Rolling);
    g.v[size_t(paramIndex(Global::VoiceMode))] = 2;  // dual chip: the time base starts again
    g.blocks(10);
    CHECK(g.d.recorderState() == RecState::Idle);
    auto take = g.d.takeRecording();
    REQUIRE(take);
    CHECK_FALSE(take->frames.empty());

    // Dual chip, second chip; a single chip does not have it: the take stays armed.
    Rig one(1);
    REQUIRE(one.d.armRecorder(1));
    one.host.playing = true;
    one.blocks(5);
    CHECK(one.d.recorderState() == RecState::Armed);
    Rig dual(2);
    REQUIRE(dual.d.armRecorder(1));
    dual.host.playing = true;
    dual.blocks(5);
    CHECK(dual.d.recorderState() == RecState::Rolling);
    CHECK(dual.d.recorderChip() == 1);

    // New engine (chip clock changed, offline render: at once): the take stops.
    Rig e(1);
    REQUIRE(e.d.armRecorder(0));
    e.host.playing = true;
    for (int i = 0; i < 10; ++i) e.run(480, {}, true);
    REQUIRE(e.d.recorderState() == RecState::Rolling);
    e.v[size_t(paramIndex(Global::ChipClock))] = 1;
    e.run(480, {}, true);
    CHECK(e.d.engineSwaps() == 1);
    CHECK(e.d.recorderState() == RecState::Idle);
    auto swapped = e.d.takeRecording();
    REQUIRE(swapped);
    CHECK(swapped->frames.size() == 5);
}

TEST_CASE("Recorder: without host transport, sequencer playback drives the take", "[recorder][driver][seq]") {
    Snapshot s;
    s.state.global.seqClock = ymengine::SeqClock::Internal;
    Rig g(1, s);
    REQUIRE(g.d.armRecorder(0));
    MidiEvent play{0, 0, {}};
    play.screen = true;
    play.command = UiCommand::SeqPlay;
    g.run(480, {play});
    g.blocks(20);
    CHECK(g.d.recorderState() == RecState::Rolling);
    MidiEvent stop = play;
    stop.command = UiCommand::SeqStop;
    g.run(480, {stop});
    g.blocks(1);
    CHECK(g.d.recorderState() == RecState::Idle);
    auto take = g.d.takeRecording();
    REQUIRE(take);
    CHECK(take->frames.size() >= 10);
}
