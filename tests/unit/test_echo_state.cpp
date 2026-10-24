// STYM2149 — echo of the notes of a track (eighth DAW test, item 98, D-299) in the plugin state (TSEQ chunk), the
// Patches library ("echo" key, STYM2149 marker) and the instruments; values brought back into their ranges.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "plugincore/Instruments.h"
#include "plugincore/Json.h"
#include "plugincore/Patches.h"
#include "plugincore/State.h"

using namespace stym;
using namespace stym::plugin;
using json::Value;
using ymengine::TrackEcho;

namespace {

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

TrackEcho echo() {
    TrackEcho x;
    x.on = true;
    x.division = 8;
    x.repeats = 6;
    x.drop = 2;
    x.target = 3;
    return x;
}

}  // namespace

TEST_CASE("Echo: kept by the state, only for the tracks that change it", "[echo][state]") {
    PluginState st;
    st.extras.tracks[7].patch.echo = echo();
    const std::vector<uint8_t> bytes = encodeState(st);
    CHECK(hasChunk(bytes, "TSEQ"));
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.tracks[7].patch.echo == echo());
    CHECK(back.state.extras.tracks[0].patch.echo == TrackEcho{});
    CHECK(encodeState(back.state) == bytes);

    // Out of range: brought back.
    NonParamState n;
    TrackEcho& x = n.tracks[0].patch.echo;
    x.division = 40;
    x.repeats = 0;
    x.drop = 99;
    x.target = 30;
    sanitize(n);
    CHECK(x.division == ymengine::kDelayDivisions - 1);
    CHECK(x.repeats == 1);
    CHECK(x.drop == ymengine::kEchoDropMax);
    CHECK(x.target == ymengine::kTracks - 1);
    x.repeats = 50;
    x.target = -9;
    sanitize(n);
    CHECK(x.repeats == ymengine::kEchoRepeatsMax);
    CHECK(x.target == -1);
}

TEST_CASE("Echo: \"echo\" key of a patch and of an instrument, absent while the echo is the default one",
          "[echo][patches]") {
    ParamValues v = defaultValues();
    NonParamState s;
    const PatchEntry plain = snapPatch("Plain", v, s, 0);
    CHECK_FALSE(plain.find("patch")->find("echo"));
    CHECK(dumpPatchLibrary({plain}).find("\"stym\"") == std::string::npos);

    s.tracks[0].patch.echo = echo();
    const PatchEntry e = snapPatch("Echo", v, s, 0);
    const Value* x = e.find("patch")->find("echo");
    REQUIRE(x);
    CHECK(x->find("on")->number() == 1);
    CHECK(x->find("div")->number() == 8);
    CHECK(x->find("repeats")->number() == 6);
    CHECK(x->find("drop")->number() == 2);
    CHECK(x->find("to")->number() == 3);
    CHECK(dumpPatchLibrary({e}).find("\"stym\"") != std::string::npos);

    // Loaded into another track; a patch without the key gives the default echo back.
    CHECK(patchChange(e, NonParamState{}, 9).track.patch.echo == echo());
    CHECK(patchChange(plain, s, 0).track.patch.echo == TrackEcho{});

    // Instrument: the same entry.
    const Instrument ins = captureInstrument("Echo", v, s, 0);
    CHECK(ins.patch.echo == echo());
    CHECK(instrumentFromEntry(instrumentEntry(ins)) == ins);

    // Values out of range or of the wrong type in a file.
    PatchEntry bad = e;
    Value p = *bad.find("patch");
    Value b = Value::object();
    b.set("on", Value(1));
    b.set("div", Value(99));
    b.set("repeats", Value("many"));
    b.set("drop", Value(-4));
    b.set("to", Value(70));
    p.set("echo", std::move(b));
    bad.set("patch", std::move(p));
    const TrackEcho got = patchChange(bad, s, 0).track.patch.echo;
    CHECK(got.on);
    CHECK(got.division == ymengine::kDelayDivisions - 1);
    CHECK(got.repeats == TrackEcho{}.repeats);
    CHECK(got.drop == 0);
    CHECK(got.target == ymengine::kTracks - 1);
}
