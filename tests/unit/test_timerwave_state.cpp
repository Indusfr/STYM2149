// STYM2149 — timer waves in the plugin state and the Patches library: end of the TSEQ chunk,
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
    p.sidForm = ymengine::SidForm::Custom;
    p.sidWaveLen = 4;
    p.buzzShapes = {8, 10, 0, 14};
    p.buzzLen = 3;
}

// The Sine form at phase 11 and period 5, the Square's width 3.
void litSine(TrackPatch& t) {
    t.sidWave = {14, 14, 12, 12, 12, 12, 14, 14, 13, 11, 8, 3, 3, 8, 11, 13};
    t.sidWaveLen = 16;
    t.sidForm = ymengine::SidForm::Sine;
    t.sidWavePhase = 11;
    t.sidWavePeriod = 5;
    t.sidWidth = 3;
}

// The state `bytes` decoded as if its TSEQ chunk had been written n bytes shorter, before the last fields came.
DecodeResult olderChunk(std::vector<uint8_t> bytes, uint32_t n) {
    size_t at = 0;
    for (size_t i = 0; i + 4 <= bytes.size(); ++i)
        if (std::memcmp(bytes.data() + i, "TSEQ", 4) == 0) at = i;
    REQUIRE(at > 0);
    auto u32 = [&](size_t i) {
        return uint32_t(bytes[i]) | uint32_t(bytes[i + 1]) << 8 | uint32_t(bytes[i + 2]) << 16 |
               uint32_t(bytes[i + 3]) << 24;
    };
    auto put = [&](size_t i, uint32_t v) {
        for (int k = 0; k < 4; ++k) bytes[i + size_t(k)] = uint8_t(v >> (8 * k));
    };
    const uint32_t chunk = u32(at + 4), track = u32(at + 8 + 2);
    const size_t end = at + 8 + 2 + 4 + track;
    bytes.erase(bytes.begin() + std::ptrdiff_t(end - n), bytes.begin() + std::ptrdiff_t(end));
    put(at + 4, chunk - n);
    put(at + 8 + 2, track - n);
    return decodeState(bytes);
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
    // Timer wave and buzzer fields, then after them (LFO, echo, SID wave),
    // at the end of the track.
    const size_t tail = 1 + ymengine::kSidWaveSteps + 1 + ymengine::kBuzzSteps + 1 + 1 + 1 + 8 + 1 + 21 + 1 + 5 + 5;
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
    CHECK(r.state.extras.tracks[1].patch.sidForm == ymengine::SidForm::Base);
    CHECK(r.state.extras.tracks[1].patch.sidWaveLen == ymengine::kSidSquareSteps);
    CHECK(r.state.extras.tracks[1].patch.buzzLen == 0);

    // Out-of-range values: brought within theirs; Steps below 2, those by default.
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
    p.sidForm = ymengine::SidForm(9);
    sanitize(n);
    CHECK(p.sidWaveLen == ymengine::kSidSquareSteps);
    CHECK(p.sidForm == ymengine::SidForm::Custom);
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
    CHECK(c.track.patch.sidForm == ymengine::SidForm::Custom);
    const PatchChange cleared = patchChange(plain, s, 0);
    CHECK(cleared.track.patch.sidForm == ymengine::SidForm::Base);
    CHECK(cleared.track.patch.sidWaveLen == ymengine::kSidSquareSteps);
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
    CHECK(b.track.patch.sidWaveLen == ymengine::kSidSquareSteps);
    CHECK(b.track.patch.buzzLen == ymengine::kBuzzSteps);

    // Instrument: same entry, read back identically.
    const Instrument ins = captureInstrument("Waves", v, s, 0);
    CHECK(ins.patch.sidWave == s.tracks[0].patch.sidWave);
    CHECK(ins.patch.buzzLen == 3);
    CHECK(instrumentFromEntry(instrumentEntry(ins)) == ins);
}

// The cases of the SID wave's forms are split in four: their states, about 60 KB each, would overflow the 1 MB stack
// of MSVC in one test case.
TEST_CASE("Timer waves: the form of the SID wave, the phase and period of its Sine, the Square's width, kept by the "
          "state; Base, 8, 8 and 4 by default",
          "[timerwave][state]") {
    PluginState st;
    TrackPatch& t = st.extras.tracks[2].patch;
    litSine(t);
    const std::vector<uint8_t> bytes = encodeState(st);
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.state.extras.tracks[2].patch.sidForm == ymengine::SidForm::Sine);
    CHECK(back.state.extras.tracks[2].patch.sidWavePhase == 11);
    CHECK(back.state.extras.tracks[2].patch.sidWavePeriod == 5);
    CHECK(back.state.extras.tracks[2].patch.sidWidth == 3);
    CHECK(back.state.extras.tracks[2].patch == t);
    CHECK(encodeState(back.state) == bytes);
    // The form alone writes the chunk; Square keeps its Steps and width, Base the Steps of the other forms.
    PluginState lone;
    lone.extras.tracks[0].patch.sidForm = ymengine::SidForm::Square;
    const DecodeResult one = decodeState(encodeState(lone));
    REQUIRE(one.ok);
    CHECK(one.state.extras.tracks[0].patch.sidForm == ymengine::SidForm::Square);
    CHECK(one.state.extras.tracks[0].patch.sidWaveLen == 8);
    CHECK(one.state.extras.tracks[0].patch.sidWavePhase == 8);
    CHECK(one.state.extras.tracks[0].patch.sidWavePeriod == 8);
    CHECK(one.state.extras.tracks[0].patch.sidWidth == 4);
    lone.extras.tracks[0].patch.sidWaveLen = 12;
    lone.extras.tracks[0].patch.sidWidth = 5;
    lone.extras.tracks[1].patch.sidWaveLen = 12;
    const DecodeResult twelve = decodeState(encodeState(lone));
    REQUIRE(twelve.ok);
    CHECK(twelve.state.extras.tracks[0].patch == lone.extras.tracks[0].patch);
    CHECK(twelve.state.extras.tracks[1].patch.sidForm == ymengine::SidForm::Base);
    CHECK(twelve.state.extras.tracks[1].patch.sidWaveLen == 12);
    // The phase alone too.
    PluginState shifted;
    shifted.extras.tracks[1].patch.sidWavePhase = 0;
    const DecodeResult zero = decodeState(encodeState(shifted));
    REQUIRE(zero.ok);
    CHECK(zero.state.extras.tracks[1].patch.sidWavePhase == 0);
}

TEST_CASE("Timer waves: the period and the square's width alone write the chunk, kept within their ranges",
          "[timerwave][state]") {
    // The period alone, kept within 2 to 16.
    PluginState cycled;
    cycled.extras.tracks[3].patch.sidWavePeriod = 16;
    const DecodeResult full = decodeState(encodeState(cycled));
    REQUIRE(full.ok);
    CHECK(full.state.extras.tracks[3].patch.sidWavePeriod == 16);
    cycled.extras.tracks[3].patch.sidWavePeriod = 200;
    const DecodeResult wide = decodeState(encodeState(cycled));
    REQUIRE(wide.ok);
    CHECK(wide.state.extras.tracks[3].patch.sidWavePeriod == 16);
    // The width of the square alone, kept within 1 to 15.
    PluginState narrow;
    narrow.extras.tracks[4].patch.sidForm = ymengine::SidForm::Square;
    narrow.extras.tracks[4].patch.sidWidth = 15;
    const DecodeResult thin = decodeState(encodeState(narrow));
    REQUIRE(thin.ok);
    CHECK(thin.state.extras.tracks[4].patch.sidWidth == 15);
    narrow.extras.tracks[4].patch.sidWidth = 0;
    const DecodeResult zeroWidth = decodeState(encodeState(narrow));
    REQUIRE(zeroWidth.ok);
    CHECK(zeroWidth.state.extras.tracks[4].patch.sidWidth == 1);
}

TEST_CASE("Timer waves: an older TSEQ chunk reads its form from its Steps and Sine, a lit Sine's period 16 and the "
          "phase 8",
          "[timerwave][state]") {
    TrackPatch t;
    litSine(t);
    std::vector<uint8_t> bytes;
    {
        PluginState st;
        st.extras.tracks[2].patch = t;
        bytes = encodeState(st);
    }
    // A chunk from before the form menu ends one byte earlier: the lit Sine of its 16 Steps, drawn on the chip's
    // levels then, is the Bounce form, the width (of the plain square then) back to its default; from before the
    // width, two: the same; before the period, three: its lit Sine was one cycle over the 16 bars, period 16;
    // before the phase, four: the phase 8 too; before the drawn bars, five: the bars drawn, Custom, the period by
    // default (8), the wave kept.
    for (const uint32_t n : {1u, 2u}) {
        CAPTURE(n);
        const DecodeResult before411 = olderChunk(bytes, n);
        REQUIRE(before411.ok);
        CHECK(before411.state.extras.tracks[2].patch.sidForm == ymengine::SidForm::Bounce);
        CHECK(before411.state.extras.tracks[2].patch.sidWidth == 4);
        CHECK(before411.state.extras.tracks[2].patch.sidWavePeriod == 5);
        CHECK(before411.state.extras.tracks[2].patch.sidWaveLen == 16);
    }
    {
        const DecodeResult before406 = olderChunk(bytes, 3);
        REQUIRE(before406.ok);
        CHECK(before406.state.extras.tracks[2].patch.sidForm == ymengine::SidForm::Bounce);
        CHECK(before406.state.extras.tracks[2].patch.sidWavePhase == 11);
        CHECK(before406.state.extras.tracks[2].patch.sidWavePeriod == 16);
    }
    {
        const DecodeResult before405 = olderChunk(bytes, 4);
        REQUIRE(before405.ok);
        CHECK(before405.state.extras.tracks[2].patch.sidForm == ymengine::SidForm::Bounce);
        CHECK(before405.state.extras.tracks[2].patch.sidWavePhase == 8);
        CHECK(before405.state.extras.tracks[2].patch.sidWavePeriod == 16);
    }
    {
        const DecodeResult old = olderChunk(bytes, 5);
        REQUIRE(old.ok);
        CHECK(old.state.extras.tracks[2].patch.sidForm == ymengine::SidForm::Custom);
        CHECK(old.state.extras.tracks[2].patch.sidWavePhase == 8);
        CHECK(old.state.extras.tracks[2].patch.sidWavePeriod == 8);
        CHECK(old.state.extras.tracks[2].patch.sidWave == t.sidWave);
        CHECK(old.state.extras.tracks[2].patch.sidWaveLen == 16);
    }
    // With the Sine out, a chunk without the period keeps the period by default for the next Sine.
    {
        PluginState out;
        out.extras.tracks[2].patch.sidWaveLen = 0;  // the plain square, as written then
        out.extras.tracks[2].patch.sidWavePhase = 3;
        const DecodeResult outBefore406 = olderChunk(encodeState(out), 3);
        REQUIRE(outBefore406.ok);
        CHECK(outBefore406.state.extras.tracks[2].patch.sidForm == ymengine::SidForm::Base);
        CHECK(outBefore406.state.extras.tracks[2].patch.sidWaveLen == ymengine::kSidSquareSteps);
        CHECK(outBefore406.state.extras.tracks[2].patch.sidWavePhase == 3);
        CHECK(outBefore406.state.extras.tracks[2].patch.sidWavePeriod == 8);
    }
}

TEST_CASE("Timer waves: the plain square of an older TSEQ chunk, half and half Base, of another width the Square of "
          "16 Steps",
          "[timerwave][state]") {
    // Before the form menu the plain square wrote Steps 0 and its width in sixteenths of its period, 8 by default.
    // olderChunk shortens the first track of the chunk.
    PluginState st;
    TrackPatch& sq = st.extras.tracks[2].patch;
    sq.sidWaveLen = 0;
    sq.sidWidth = 3;
    const DecodeResult older = olderChunk(encodeState(st), 1);
    REQUIRE(older.ok);
    const TrackPatch& square = older.state.extras.tracks[2].patch;
    CHECK(square.sidForm == ymengine::SidForm::Square);
    CHECK(square.sidWaveLen == 16);
    CHECK(square.sidWidth == 3);
    sq.sidWidth = 8;
    const DecodeResult halfOlder = olderChunk(encodeState(st), 1);
    REQUIRE(halfOlder.ok);
    const TrackPatch& half = halfOlder.state.extras.tracks[2].patch;
    CHECK(half.sidForm == ymengine::SidForm::Base);
    CHECK(half.sidWaveLen == ymengine::kSidSquareSteps);
    CHECK(half.sidWidth == ymengine::kSidWidth);
}

TEST_CASE("Timer waves: the form of the SID wave, the phase and period of its Sine and the Square's width, kept by "
          "the patches; older patches read as the state",
          "[timerwave][patches]") {
    TrackPatch t;
    litSine(t);
    // Patches: keys of their own, absent at their defaults.
    ParamValues v = defaultValues();
    NonParamState s;
    const PatchEntry plain = snapPatch("Plain", v, s, 0);
    for (const char* k : {"sidForm", "sidWaveSine", "sidWaveLen", "sidWavePhase", "sidWavePeriod", "sidWidth"}) {
        INFO(k);
        CHECK_FALSE(plain.find("patch")->find(k));
    }
    s.tracks[0].patch = t;
    const PatchEntry e = snapPatch("Sine", v, s, 0);
    CHECK(e.find("patch")->find("sidForm")->number() == 2);
    CHECK_FALSE(e.find("patch")->find("sidWaveSine"));
    CHECK(e.find("patch")->find("sidWaveLen")->number() == 16);
    CHECK(e.find("patch")->find("sidWavePhase")->number() == 11);
    CHECK(e.find("patch")->find("sidWavePeriod")->number() == 5);
    CHECK(e.find("patch")->find("sidWidth")->number() == 3);
    const TrackPatch sine = patchChange(e, s, 4).track.patch;
    CHECK(sine.sidForm == ymengine::SidForm::Sine);
    CHECK(sine.sidWaveLen == 16);
    CHECK(sine.sidWavePhase == 11);
    CHECK(sine.sidWavePeriod == 5);
    CHECK(sine.sidWidth == 3);
    const TrackPatch base = patchChange(plain, s, 0).track.patch;
    CHECK(base.sidForm == ymengine::SidForm::Base);
    CHECK(base.sidWaveLen == 8);
    CHECK(base.sidWavePhase == 8);
    CHECK(base.sidWavePeriod == 8);
    CHECK(base.sidWidth == 4);
    // The form of a patch at Base beside other Steps: written, so that the Steps do not read as Custom.
    s.tracks[0].patch = TrackPatch{};
    s.tracks[0].patch.sidWaveLen = 12;
    const PatchEntry twelve = snapPatch("Twelve", v, s, 0);
    CHECK(twelve.find("patch")->find("sidForm")->number() == 0);
    CHECK(patchChange(twelve, s, 4).track.patch.sidForm == ymengine::SidForm::Base);
    CHECK(patchChange(twelve, s, 4).track.patch.sidWaveLen == 12);

    // Older patches, without the form: the lit Sine, the Bounce, its period 16 when absent (before the period); bars
    // drawn, Custom; the plain square of another width than 8 sixteenths, the Square of 16 Steps; of 8, Base.
    auto older = [&](std::initializer_list<std::pair<const char*, double>> keys) {
        PatchEntry entry = plain;
        Value p = *plain.find("patch");
        for (const auto& [key, value] : keys) p.set(key, Value(value));
        entry.set("patch", std::move(p));
        return patchChange(entry, s, 4).track.patch;
    };
    const TrackPatch litOld = older({{"sidWaveLen", 16}, {"sidWaveSine", 1}});
    CHECK(litOld.sidForm == ymengine::SidForm::Bounce);
    CHECK(litOld.sidWavePeriod == 16);
    CHECK(older({{"sidWaveLen", 16}, {"sidWaveSine", 1}, {"sidWavePeriod", 8}}).sidWavePeriod == 8);
    const TrackPatch drawn = older({{"sidWaveLen", 4}, {"sidWidth", 3}});
    CHECK(drawn.sidForm == ymengine::SidForm::Custom);
    CHECK(drawn.sidWaveLen == 4);
    CHECK(drawn.sidWidth == 4);
    const TrackPatch narrow = older({{"sidWidth", 3}});
    CHECK(narrow.sidForm == ymengine::SidForm::Square);
    CHECK(narrow.sidWaveLen == 16);
    CHECK(narrow.sidWidth == 3);
    const TrackPatch half = older({{"sidWidth", 8}});
    CHECK(half.sidForm == ymengine::SidForm::Base);
    CHECK(half.sidWaveLen == 8);
    CHECK(half.sidWidth == 4);
}

TEST_CASE("Timer waves: the Bounce form kept by the state and the patches, read as a Sine by an older version; a form "
          "of a later version plays its bars, as Custom",
          "[timerwave][state][patches]") {
    PluginState st;
    TrackPatch& t = st.extras.tracks[5].patch;
    litSine(t);
    t.sidForm = ymengine::SidForm::Bounce;
    const std::vector<uint8_t> bytes = encodeState(st);
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.state.extras.tracks[5].patch == t);
    // Without the form byte, as an older version reads it: Sine lit, then the Bounce again (its bars, drawn on the
    // levels).
    const DecodeResult older = olderChunk(bytes, 1);
    REQUIRE(older.ok);
    CHECK(older.state.extras.tracks[5].patch.sidForm == ymengine::SidForm::Bounce);
    CHECK(older.state.extras.tracks[5].patch.sidWave == t.sidWave);
    // A form this version does not know.
    const TrackPatch bounce = t;
    t.sidForm = ymengine::SidForm(9);
    const DecodeResult later = decodeState(encodeState(st));
    REQUIRE(later.ok);
    CHECK(later.state.extras.tracks[5].patch.sidForm == ymengine::SidForm::Custom);
    CHECK(ymengine::sidFormOf(4) == ymengine::SidForm::Bounce);
    CHECK(ymengine::sidFormOf(-1) == ymengine::SidForm::Custom);

    // Patches: its number, 4, after Custom.
    ParamValues v = defaultValues();
    NonParamState s;
    s.tracks[0].patch = bounce;
    const PatchEntry e = snapPatch("Bounce", v, s, 0);
    CHECK(e.find("patch")->find("sidForm")->number() == 4);
    CHECK(patchChange(e, s, 4).track.patch.sidForm == ymengine::SidForm::Bounce);
}
