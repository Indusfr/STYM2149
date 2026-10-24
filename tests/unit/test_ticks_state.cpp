// STYM2149 — Ticks timing in the plugin state (TIMG chunk, M6, B5, D-149) and entry of the speed and the groove.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymseq/Timing.h"

using namespace stym;
using namespace stym::plugin;
using ymengine::SeqTiming;

namespace {
bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}
}  // namespace

TEST_CASE("Ticks: TIMG chunk written only when not default, read back, values clamped", "[ticks][state]") {
    PluginState st;
    CHECK_FALSE(hasChunk(encodeState(st), "TIMG"));
    st.extras.global.timing = SeqTiming::Ticks;
    st.extras.global.speed = 3;
    st.extras.global.grooveLength = 3;
    st.extras.global.groove = {2, 0, 1};
    const std::vector<uint8_t> bytes = encodeState(st);
    CHECK(hasChunk(bytes, "TIMG"));
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.global.timing == SeqTiming::Ticks);
    CHECK(back.state.extras.global.speed == 3);
    CHECK(back.state.extras.global.grooveLength == 3);
    CHECK(back.state.extras.global.groove == st.extras.global.groove);
    CHECK(encodeState(back.state) == bytes);
    // Groove alone not default: the chunk is written, the timing stays BPM.
    PluginState g;
    g.extras.global.groove[5] = 1;
    CHECK(hasChunk(encodeState(g), "TIMG"));

    NonParamState n;
    n.global.timing = SeqTiming(7);
    n.global.speed = 0;
    n.global.grooveLength = 40;
    n.global.groove[1] = 99;
    sanitize(n);
    CHECK(n.global.timing == SeqTiming::Ticks);
    CHECK(n.global.speed == 1);
    CHECK(n.global.grooveLength == ymengine::kGrooveSteps);
    CHECK(n.global.groove[1] == ymengine::kGrooveMax);
    n.global.speed = 200;
    sanitize(n);
    CHECK(n.global.speed == ymengine::kSpeedMax);

    // Truncated chunk: the timing only.
    const std::vector<uint8_t> s = {'S', 'T', 'Y', 'M', 1, 0, 0, 0, 'T', 'I', 'M', 'G', 1, 0, 0, 0, 1};
    const DecodeResult r = decodeState(s);
    REQUIRE(r.ok);
    CHECK(r.state.extras.global.timing == SeqTiming::Ticks);
    CHECK(r.state.extras.global.speed == 6);
}

TEST_CASE("Ticks: replay tick from 50 to 200 Hz (VBL Rate parameter)", "[ticks][state]") {
    CHECK(kVblRates == std::array<uint32_t, 5>{50, 60, 70, 100, 200});
    const ParamSpec& s = paramSpec(paramIndex(Global::VblRate));
    CHECK(s.choices.size() == 5);
    CHECK(s.choices[3] == "100 Hz");
    CHECK(s.choices[4] == "200 Hz");
    ParamValues v = defaultValues();
    v[size_t(paramIndex(Global::VblRate))] = 4;
    CHECK(toGlobal(v, {}).vblHz == 200);
}

TEST_CASE("Ticks: speed and groove typed in, equivalent tempo", "[ticks][ymseq]") {
    CHECK(ymseq::parseSpeed(" 12 ") == 12);
    CHECK(ymseq::parseSpeed("0") == 1);
    CHECK(ymseq::parseSpeed("64") == ymengine::kSpeedMax);
    CHECK_FALSE(ymseq::parseSpeed(""));
    CHECK_FALSE(ymseq::parseSpeed("6x"));

    const auto g = ymseq::parseGroove("1, 0;3  15 16");
    REQUIRE(g);
    CHECK(g->length == 5);
    CHECK(g->values[2] == 3);
    CHECK(g->values[4] == ymengine::kGrooveMax);
    CHECK(ymseq::grooveText(g->values, g->length) == "1 0 3 15 15");
    const auto empty = ymseq::parseGroove("  ");
    REQUIRE(empty);
    CHECK(empty->length == 1);
    CHECK(empty->values[0] == 0);
    CHECK_FALSE(ymseq::parseGroove("1 a"));
    CHECK_FALSE(ymseq::parseGroove("0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"));  // 17 values

    CHECK(std::fabs(ymseq::ticksBpm(50, 6, 2) - 125) < 1e-9);  // 1/16
    CHECK(std::fabs(ymseq::ticksBpm(60, 6, 1) - 300) < 1e-9);  // 1/8: 2 steps per beat
}
