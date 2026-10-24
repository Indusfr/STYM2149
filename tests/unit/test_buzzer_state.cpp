// STYM2149 — buzzer in the plugin state and the Patches library (M6, B7, D-152): end of the TSEQ chunk, STYM2149
// keys of a patch, values brought within their ranges.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>
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

size_t chunkAt(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return i;
    return 0;
}

void buzzer(TrackPatch& p) {
    p.envTuned = true;
    p.envRatioN = 3;
    p.envRatioD = 2;
    p.envFineCents = -7.5;
    p.ymPad = true;
}

}  // namespace

TEST_CASE("Buzzer: end of the TSEQ chunk, written for a track that only has a buzzer", "[buzzer][state]") {
    PluginState st;
    buzzer(st.extras.tracks[9].patch);
    const std::vector<uint8_t> bytes = encodeState(st);
    REQUIRE(chunkAt(bytes, "TSEQ") > 0);
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.tracks[9].patch == st.extras.tracks[9].patch);
    CHECK(encodeState(back.state) == bytes);

    // TSEQ chunk of B6 (without the buzzer): the buzzer stays at its default.
    PluginState old;
    old.extras.tracks[1].patch.sidWaveLen = 4;
    std::vector<uint8_t> b6 = encodeState(old);
    const size_t at = chunkAt(b6, "TSEQ");
    REQUIRE(at > 0);
    const size_t tail = 1 + 1 + 1 + 8 + 1;  // fields of B7, at the end of the track
    auto u32 = [&](size_t i) { return uint32_t(b6[i]) | uint32_t(b6[i + 1]) << 8 | uint32_t(b6[i + 2]) << 16 | uint32_t(b6[i + 3]) << 24; };
    auto put = [&](size_t i, uint32_t v) {
        for (int k = 0; k < 4; ++k) b6[i + size_t(k)] = uint8_t(v >> (8 * k));
    };
    const uint32_t chunk = u32(at + 4), track = u32(at + 8 + 2);
    REQUIRE(track > tail);
    const size_t end = at + 8 + 2 + 4 + track;
    b6.erase(b6.begin() + std::ptrdiff_t(end - tail), b6.begin() + std::ptrdiff_t(end));
    put(at + 4, chunk - uint32_t(tail));
    put(at + 8 + 2, track - uint32_t(tail));
    const DecodeResult r = decodeState(b6);
    REQUIRE(r.ok);
    CHECK(r.state.extras.tracks[1].patch.sidWaveLen == 4);
    CHECK_FALSE(r.state.extras.tracks[1].patch.envTuned);
    CHECK(r.state.extras.tracks[1].patch.envRatioN == 1);
    CHECK(r.state.extras.tracks[1].patch.envRatioD == 1);

    // Out-of-range values.
    NonParamState n;
    TrackPatch& p = n.tracks[0].patch;
    p.envRatioN = 0;
    p.envRatioD = 40;
    p.envFineCents = 300;
    sanitize(n);
    CHECK(p.envRatioN == 1);
    CHECK(p.envRatioD == ymengine::kEnvRatioMax);
    CHECK(p.envFineCents == ymengine::kEnvFineMax);
    p.envFineCents = std::numeric_limits<double>::infinity();
    sanitize(n);
    CHECK(p.envFineCents == 0);
}

TEST_CASE("Buzzer: STYM2149 keys of a patch, absent without a buzzer", "[buzzer][patches]") {
    ParamValues v = defaultValues();
    NonParamState s;
    const PatchEntry plain = snapPatch("Plain", v, s, 0);
    for (const char* k : {"envTuned", "envN", "envD", "envFine", "ymPad"}) {
        INFO(k);
        CHECK_FALSE(plain.find("patch")->find(k));
    }

    buzzer(s.tracks[0].patch);
    const PatchEntry e = snapPatch("Buzz", v, s, 0);
    const Value& p = *e.find("patch");
    CHECK(p.find("envTuned")->number() == 1);
    CHECK(p.find("envN")->number() == 3);
    CHECK(p.find("envD")->number() == 2);
    CHECK(p.find("envFine")->number() == -7.5);
    CHECK(p.find("ymPad")->number() == 1);

    const PatchChange c = patchChange(e, s, 3);
    CHECK(c.track.patch.envTuned);
    CHECK(c.track.patch.envRatioN == 3);
    CHECK(c.track.patch.envRatioD == 2);
    CHECK(c.track.patch.envFineCents == -7.5);
    CHECK(c.track.patch.ymPad);
    const PatchChange cleared = patchChange(plain, s, 0);
    CHECK_FALSE(cleared.track.patch.envTuned);
    CHECK(cleared.track.patch.envRatioN == 1);
    CHECK_FALSE(cleared.track.patch.ymPad);

    // Out-of-range file: brought back.
    PatchEntry bad = e;
    Value q = *bad.find("patch");
    q.set("envN", Value(99.0));
    q.set("envD", Value(0.0));
    q.set("envFine", Value(-1000.0));
    bad.set("patch", std::move(q));
    const PatchChange b = patchChange(bad, s, 0);
    CHECK(b.track.patch.envRatioN == ymengine::kEnvRatioMax);
    CHECK(b.track.patch.envRatioD == 1);
    CHECK(b.track.patch.envFineCents == -ymengine::kEnvFineMax);

    const Instrument ins = captureInstrument("Buzz", v, s, 0);
    CHECK(ins.patch.envTuned);
    CHECK(instrumentFromEntry(instrumentEntry(ins)) == ins);
}
