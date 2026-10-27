// STYM2149 — instrument sequences in the plugin state and the Patches library: TSEQ chunk written
// only when used, STYM2149-specific keys in a patch, values brought back into their ranges.
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
using ymengine::PitchSource;
using ymengine::PitchTarget;
using ymengine::TrackPatch;

namespace {

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

void sequences(TrackPatch& p) {
    p.rows.period[0] = -40;
    p.rows.period[63] = 127;
    p.rows.noise[1] = 17;
    p.rows.mix[2] = 8;
    p.rows.gen[3] = uint8_t(ymengine::SeqGen::Sid);
    p.rows.fix[4] = 1;
    p.fixedNote = 36;
    p.detuneCents = -12.5;
    p.analogCents = 8;
    p.hardSync = true;
    p.link = 5;
    p.pitchMatrix = pitchBit(PitchSource::Table, PitchTarget::Buzzer) | pitchBit(PitchSource::Slide, PitchTarget::Square);
}

}  // namespace

TEST_CASE("Sequences: TSEQ chunk of the state, written only for the tracks that have some", "[sequences][state]") {
    PluginState st;
    CHECK_FALSE(hasChunk(encodeState(st), "TSEQ"));
    sequences(st.extras.tracks[3].patch);
    st.extras.tracks[12].patch.hardSync = true;
    const std::vector<uint8_t> bytes = encodeState(st);
    CHECK(hasChunk(bytes, "TSEQ"));
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.tracks[3].patch == st.extras.tracks[3].patch);
    CHECK(back.state.extras.tracks[12].patch.hardSync);
    CHECK(back.state.extras.tracks[0].patch == TrackPatch{});
    CHECK(encodeState(back.state) == bytes);

    // Out-of-range values: brought back into their own.
    NonParamState n;
    TrackPatch& p = n.tracks[0].patch;
    p.rows.period[0] = -128;
    p.rows.noise[0] = 40;
    p.rows.mix[0] = 9;
    p.rows.gen[0] = 7;
    p.rows.fix[0] = 3;
    p.fixedNote = 200;
    p.detuneCents = 500;
    p.analogCents = -3;
    p.link = 33;
    p.pitchMatrix = 0xFFFF;
    sanitize(n);
    CHECK(p.rows.period[0] == -127);
    CHECK(p.rows.noise[0] == 31);
    CHECK(p.rows.mix[0] == 8);
    CHECK(p.rows.gen[0] == 3);
    CHECK(p.rows.fix[0] == 1);
    CHECK(p.fixedNote == 127);
    CHECK(p.detuneCents == 100);
    CHECK(p.analogCents == 0);
    CHECK(p.link == 32);
    CHECK(p.pitchMatrix == ymengine::kPitchMatrixAll);
    p.detuneCents = std::numeric_limits<double>::quiet_NaN();
    sanitize(n);
    CHECK(p.detuneCents == 0);

    // Truncated chunk: only the fields present.
    std::vector<uint8_t> s = {'S', 'T', 'Y', 'M', 1, 0, 0, 0, 'T', 'S', 'E', 'Q', 0, 0, 0, 0, 1, 2};
    const uint32_t body = 64;
    for (int i = 0; i < 4; ++i) s.push_back(uint8_t(body >> (8 * i)));
    for (int i = 0; i < 64; ++i) s.push_back(uint8_t(i == 5 ? 0xF6 : 0));  // PER only, -10 at step 6
    const uint32_t len = uint32_t(s.size() - 16);
    for (int i = 0; i < 4; ++i) s[12 + size_t(i)] = uint8_t(len >> (8 * i));
    const DecodeResult r = decodeState(s);
    REQUIRE(r.ok);
    CHECK(r.state.extras.tracks[2].patch.rows.period[5] == -10);
    CHECK(r.state.extras.tracks[2].patch.pitchMatrix == ymengine::kPitchMatrixDefault);
}

TEST_CASE("Sequences: STYM2149 keys of a patch, absent from a patch without sequences", "[sequences][patches]") {
    ParamValues v = defaultValues();
    NonParamState s;
    const PatchEntry plain = snapPatch("Plain", v, s, 0);
    for (const char* k : {"seqPer", "seqNP", "seqMix", "seqGen", "seqFix", "fixNote", "detune", "analog", "hardSync",
                          "link", "pitchMatrix"}) {
        INFO(k);
        CHECK_FALSE(plain.find("patch")->find(k));
    }

    sequences(s.tracks[0].patch);
    const PatchEntry e = snapPatch("Seq", v, s, 0);
    const Value& p = *e.find("patch");
    CHECK(p.find("seqPer")->items().size() == 64);
    CHECK(p.find("seqPer")->items()[0].number() == -40);
    CHECK(p.find("fixNote")->number() == 36);
    CHECK(p.find("detune")->number() == -12.5);
    CHECK(p.find("link")->number() == 5);

    // Loaded into another track: the sequences come along; a patch without sequences clears them.
    const PatchChange c = patchChange(e, s, 7);
    CHECK(c.track.patch.rows == s.tracks[0].patch.rows);
    CHECK(c.track.patch.fixedNote == 36);
    CHECK(c.track.patch.detuneCents == -12.5);
    CHECK(c.track.patch.analogCents == 8);
    CHECK(c.track.patch.hardSync);
    CHECK(c.track.patch.link == 5);
    CHECK(c.track.patch.pitchMatrix == s.tracks[0].patch.pitchMatrix);
    const PatchChange cleared = patchChange(plain, s, 0);
    CHECK(cleared.track.patch.rows.empty());
    CHECK(cleared.track.patch.fixedNote == 60);
    CHECK(cleared.track.patch.pitchMatrix == ymengine::kPitchMatrixDefault);

    // Instrument: same entry, read back identically.
    const Instrument ins = captureInstrument("Seq", v, s, 0);
    CHECK(ins.patch.rows == s.tracks[0].patch.rows);
    CHECK(instrumentFromEntry(instrumentEntry(ins)) == ins);
}
