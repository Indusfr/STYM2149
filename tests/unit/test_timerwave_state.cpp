// STYM2149 — timer waves in the plugin state and the Patches library (M6, B6, D-151): end of the TSEQ chunk,
// STYM2149 keys of a patch, values brought within their ranges.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "plugincore/Instruments.h"
#include "plugincore/Json.h"
#include "plugincore/Patches.h"
#include "plugincore/State.h"

using namespace stym;
using namespace stym::plugin;
using json::Value;
using ymengine::TrackPatch;

namespace {

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

void waves(TrackPatch& p) {
    p.sidWave = {15, 0, 9, 4, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    p.sidWaveLen = 4;
    p.buzzShapes = {8, 10, 0, 14};
    p.buzzLen = 3;
}

}  // namespace

TEST_CASE("Timer waves: end of the TSEQ chunk, written for a track that only has waves", "[timerwave][state]") {
    PluginState st;
    waves(st.extras.tracks[5].patch);
    const std::vector<uint8_t> bytes = encodeState(st);
    CHECK(hasChunk(bytes, "TSEQ"));
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.tracks[5].patch == st.extras.tracks[5].patch);
    CHECK(back.state.extras.tracks[4].patch == TrackPatch{});
    CHECK(encodeState(back.state) == bytes);

    // TSEQ chunk of B2 (without the waves): the waves stay at their default.
    PluginState old;
    old.extras.tracks[1].patch.hardSync = true;
    std::vector<uint8_t> b2 = encodeState(old);
    const size_t at = [&] {
        for (size_t i = 0; i + 4 <= b2.size(); ++i)
            if (std::memcmp(b2.data() + i, "TSEQ", 4) == 0) return i;
        return size_t(0);
    }();
    REQUIRE(at > 0);
    // Fields of B6 then of B7 (buzzer, D-152), at the end of the track.
    const size_t tail = 1 + ymengine::kSidWaveSteps + 1 + ymengine::kBuzzSteps + 1 + 1 + 1 + 8 + 1;
    auto u32 = [&](size_t i) { return uint32_t(b2[i]) | uint32_t(b2[i + 1]) << 8 | uint32_t(b2[i + 2]) << 16 | uint32_t(b2[i + 3]) << 24; };
    auto put = [&](size_t i, uint32_t v) {
        for (int k = 0; k < 4; ++k) b2[i + size_t(k)] = uint8_t(v >> (8 * k));
    };
    // Chunk: u32 size; body: u8 count, u8 track, u32 length, fields. The chunk may or may not be the last one
    // written: the last bytes of the track are removed and the rest is joined back.
    const uint32_t chunk = u32(at + 4), track = u32(at + 8 + 2);
    REQUIRE(track > tail);
    const size_t end = at + 8 + 2 + 4 + track;
    b2.erase(b2.begin() + std::ptrdiff_t(end - tail), b2.begin() + std::ptrdiff_t(end));
    put(at + 4, chunk - uint32_t(tail));
    put(at + 8 + 2, track - uint32_t(tail));
    const DecodeResult r = decodeState(b2);
    REQUIRE(r.ok);
    CHECK(r.state.extras.tracks[1].patch.hardSync);
    CHECK(r.state.extras.tracks[1].patch.sidWaveLen == 0);
    CHECK(r.state.extras.tracks[1].patch.buzzLen == 0);

    // Out-of-range values: brought within theirs; a one-step wave is the square wave.
    NonParamState n;
    TrackPatch& p = n.tracks[0].patch;
    p.sidWave[3] = 20;
    p.sidWaveLen = 40;
    p.buzzShapes[2] = 0x1F;
    p.buzzLen = 9;
    sanitize(n);
    CHECK(p.sidWave[3] == 15);
    CHECK(p.sidWaveLen == ymengine::kSidWaveSteps);
    CHECK(p.buzzShapes[2] == 15);
    CHECK(p.buzzLen == ymengine::kBuzzSteps);
    p.sidWaveLen = 1;
    sanitize(n);
    CHECK(p.sidWaveLen == 0);
}

TEST_CASE("Timer waves: STYM2149 keys of a patch, absent without a wave", "[timerwave][patches]") {
    ParamValues v = defaultValues();
    NonParamState s;
    const PatchEntry plain = snapPatch("Plain", v, s, 0);
    for (const char* k : {"sidWave", "sidWaveLen", "buzzSteps", "buzzLen"}) {
        INFO(k);
        CHECK_FALSE(plain.find("patch")->find(k));
    }

    waves(s.tracks[0].patch);
    const PatchEntry e = snapPatch("Waves", v, s, 0);
    const Value& p = *e.find("patch");
    CHECK(p.find("sidWave")->items().size() == ymengine::kSidWaveSteps);
    CHECK(p.find("sidWave")->items()[2].number() == 9);
    CHECK(p.find("sidWaveLen")->number() == 4);
    CHECK(p.find("buzzSteps")->items().size() == ymengine::kBuzzSteps);
    CHECK(p.find("buzzSteps")->items()[1].number() == 10);
    CHECK(p.find("buzzLen")->number() == 3);

    // Loaded into another track: the waves come along; a patch without a wave clears them.
    const PatchChange c = patchChange(e, s, 7);
    CHECK(c.track.patch.sidWave == s.tracks[0].patch.sidWave);
    CHECK(c.track.patch.sidWaveLen == 4);
    CHECK(c.track.patch.buzzShapes == s.tracks[0].patch.buzzShapes);
    CHECK(c.track.patch.buzzLen == 3);
    const PatchChange cleared = patchChange(plain, s, 0);
    CHECK(cleared.track.patch.sidWaveLen == 0);
    CHECK(cleared.track.patch.sidWave == TrackPatch{}.sidWave);
    CHECK(cleared.track.patch.buzzLen == 0);

    // Values of an out-of-range file: brought back.
    PatchEntry bad = e;
    Value q = *bad.find("patch");
    Value wave = Value::array();
    for (int k = 0; k < 3; ++k) wave.push(Value(99.0));
    q.set("sidWave", std::move(wave));
    q.set("sidWaveLen", Value(-3.0));
    q.set("buzzLen", Value(300.0));
    bad.set("patch", std::move(q));
    const PatchChange b = patchChange(bad, s, 0);
    CHECK(b.track.patch.sidWave[0] == 15);
    CHECK(b.track.patch.sidWave[3] == 0);
    CHECK(b.track.patch.sidWaveLen == 0);
    CHECK(b.track.patch.buzzLen == ymengine::kBuzzSteps);

    // Instrument: same entry, read back identically.
    const Instrument ins = captureInstrument("Waves", v, s, 0);
    CHECK(ins.patch.sidWave == s.tracks[0].patch.sidWave);
    CHECK(ins.patch.buzzLen == 3);
    CHECK(instrumentFromEntry(instrumentEntry(ins)) == ins);
}
