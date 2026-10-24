// STYM2149 — LFO of a track (sixth DAW test, item 59, D-257) in the plugin state (TSEQ chunk), the Patches library
// ("lfo" key, STYM2149 marker) and the instruments; values brought back into their ranges.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "plugincore/Instruments.h"
#include "plugincore/Json.h"
#include "plugincore/Patches.h"
#include "plugincore/State.h"

using namespace stym;
using namespace stym::plugin;
using json::Value;
using ymengine::LfoShape;
using ymengine::LfoTarget;
using ymengine::TrackLfo;
using ymengine::TrackPatch;

namespace {

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

TrackLfo lfo() {
    TrackLfo l;
    l.on = true;
    l.shape = LfoShape::SawDown;
    l.target = LfoTarget::Pitch;
    l.sync = true;
    l.hz = 7.5;
    l.division = 7;
    l.depth = 350;
    l.retrig = false;  // D-279
    return l;
}

}  // namespace

TEST_CASE("LFO: kept by the state, only for the tracks that change it", "[lfo][state]") {
    PluginState st;
    st.extras.tracks[5].patch.lfo = lfo();
    const std::vector<uint8_t> bytes = encodeState(st);
    CHECK(hasChunk(bytes, "TSEQ"));
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.tracks[5].patch.lfo == lfo());
    CHECK(back.state.extras.tracks[0].patch.lfo == TrackLfo{});
    CHECK(encodeState(back.state) == bytes);

    // Out of range: brought back (the depth to the maximum of its target).
    NonParamState n;
    TrackLfo& l = n.tracks[0].patch.lfo;
    l.shape = LfoShape(9);
    l.target = LfoTarget(1);
    l.hz = 99;
    l.division = 40;
    l.depth = 500;
    sanitize(n);
    CHECK(l.shape == LfoShape::Random);
    CHECK(l.hz == ymengine::kLfoMaxHz);
    CHECK(l.division == ymengine::kLfoDivisions - 1);
    CHECK(l.depth == 31);
    l.hz = std::numeric_limits<double>::quiet_NaN();
    l.depth = -1;
    sanitize(n);
    CHECK(l.hz == TrackLfo{}.hz);
    CHECK(l.depth == 0);
}

TEST_CASE("LFO: \"lfo\" key of a patch and of an instrument, absent while the LFO is the default one",
          "[lfo][patches]") {
    ParamValues v = defaultValues();
    NonParamState s;
    const PatchEntry plain = snapPatch("Plain", v, s, 0);
    CHECK_FALSE(plain.find("patch")->find("lfo"));
    CHECK(dumpPatchLibrary({plain}).find("\"stym\"") == std::string::npos);

    s.tracks[0].patch.lfo = lfo();
    const PatchEntry e = snapPatch("Lfo", v, s, 0);
    const Value* l = e.find("patch")->find("lfo");
    REQUIRE(l);
    CHECK(l->find("on")->number() == 1);
    CHECK(l->find("shape")->number() == double(LfoShape::SawDown));
    CHECK(l->find("to")->number() == double(LfoTarget::Pitch));
    CHECK(l->find("sync")->number() == 1);
    CHECK(l->find("hz")->number() == 7.5);
    CHECK(l->find("div")->number() == 7);
    CHECK(l->find("depth")->number() == 350);
    CHECK(l->find("retrig")->number() == 0);
    CHECK(dumpPatchLibrary({e}).find("\"stym\"") != std::string::npos);

    // Loaded into another track; a patch without the key gives the default LFO back.
    CHECK(patchChange(e, NonParamState{}, 9).track.patch.lfo == lfo());
    CHECK(patchChange(plain, s, 0).track.patch.lfo == TrackLfo{});

    // Instrument: the same entry.
    const Instrument ins = captureInstrument("Lfo", v, s, 0);
    CHECK(ins.patch.lfo == lfo());
    CHECK(instrumentFromEntry(instrumentEntry(ins)) == ins);

    // Values out of range or of the wrong type in a file.
    PatchEntry bad = e;
    Value p = *bad.find("patch");
    Value b = Value::object();
    b.set("on", Value(1));
    b.set("shape", Value(12));
    b.set("to", Value(-3));
    b.set("hz", Value("fast"));
    b.set("depth", Value(99));
    p.set("lfo", std::move(b));
    bad.set("patch", std::move(p));
    const TrackLfo got = patchChange(bad, s, 0).track.patch.lfo;
    CHECK(got.on);
    CHECK(got.shape == LfoShape::Random);
    CHECK(got.target == LfoTarget::Volume);
    CHECK(got.hz == TrackLfo{}.hz);
    CHECK(got.depth == 15);
    CHECK(got.retrig);  // no "retrig" key: restarted at each note
}
