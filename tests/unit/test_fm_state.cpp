// STYM2149 — FM Voice in the plugin: its generator entry, its wave at the end of the TSEQ chunk, the keys of a
// patch and an instrument, the steps of a recorder take (RFMW chunk); its scale and its wave in semitones (RFMS
// chunk for a take).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <memory>
#include <vector>

#include "plugincore/Instruments.h"
#include "plugincore/Json.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "plugincore/Patches.h"
#include "plugincore/State.h"

using namespace stym;
using namespace stym::plugin;
using json::Value;
using ymengine::Generator;
using ymengine::TrackPatch;

namespace {

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

void fmWave(TrackPatch& p) {
    p.generator = Generator::Fm;
    p.fmWave = {0, 7, 2, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    p.fmWaveLen = 3;
}

// Steps of a wave given 4 bits per step, the first step in the lowest.
std::array<uint8_t, 16> nibbles(uint64_t packed) {
    std::array<uint8_t, 16> a{};
    for (size_t i = 0; i < a.size(); ++i) a[i] = uint8_t(packed >> (4 * i) & 15);
    return a;
}

// Effect of a take: FM Voice of `steps` steps, 4 bits per step.
ymengine::RegisterFrame::Effect fmEffect(uint8_t steps, uint64_t wave) {
    ymengine::RegisterFrame::Effect e;
    e.on = true;
    e.gen = Generator::Fm;
    e.value = steps;
    e.timer = {1, 50};
    e.wave = nibbles(wave);
    return e;
}

}  // namespace

TEST_CASE("FM Voice: seventh entry of the generator, allowed on the tracks without a role in every mode",
          "[fm][plugin]") {
    const int g = paramIndex(0, Track::Generator);
    REQUIRE(paramSpec(g).choices.size() == size_t(ymengine::kGenerators));
    CHECK(paramSpec(g).choices.back() == "FM Voice");
    using ymengine::VoiceMode;
    for (VoiceMode m : {VoiceMode::Authentic3, VoiceMode::Dual3, VoiceMode::Studio16, VoiceMode::Studio32}) {
        CAPTURE(int(m));
        CHECK(generatorAllowed(m, false, 0, Generator::Fm));
        CHECK(generatorAllowed(m, true, 2, Generator::Fm));
    }
    // The Digidrum track of a chip and the DMA tracks keep their role.
    CHECK_FALSE(generatorAllowed(VoiceMode::Authentic3, false, 3, Generator::Fm));
    CHECK_FALSE(generatorAllowed(VoiceMode::Authentic3, true, 4, Generator::Fm));
    ParamValues v = defaultValues();
    v[size_t(g)] = float(int(Generator::Fm));
    CHECK(toTrack(v, 0, {}).patch.generator == Generator::Fm);
}

TEST_CASE("FM Voice: its wave at the end of the TSEQ chunk, read back, kept within its ranges", "[fm][state]") {
    auto st = std::make_unique<PluginState>();
    fmWave(st->extras.tracks[6].patch);
    const std::vector<uint8_t> bytes = encodeState(*st);
    CHECK(hasChunk(bytes, "TSEQ"));
    auto back = std::make_unique<DecodeResult>(decodeState(bytes));
    REQUIRE(back->ok);
    CHECK(back->unknownChunks == 0);
    CHECK(back->state.extras.tracks[6].patch.fmWave == st->extras.tracks[6].patch.fmWave);
    CHECK(back->state.extras.tracks[6].patch.fmWaveLen == 3);
    CHECK(back->state.extras.tracks[5].patch.fmWave == TrackPatch{}.fmWave);
    CHECK(encodeState(back->state) == bytes);

    // A track at its defaults writes no sequences: the note and its octave are those of a new track.
    const TrackPatch def;
    CHECK(def.fmWaveLen == 2);
    CHECK(def.fmWave[0] == ymengine::kFmNote);
    CHECK(def.fmWave[1] == ymengine::kFmOctave);

    // Out-of-range values: brought within theirs.
    NonParamState n;
    TrackPatch& p = n.tracks[0].patch;
    p.fmWave[2] = 30;
    p.fmWaveLen = 0;
    sanitize(n);
    CHECK(p.fmWave[2] == ymengine::kFmMultipleCount - 1);
    CHECK(p.fmWaveLen == 1);
    p.fmWaveLen = 40;
    sanitize(n);
    CHECK(p.fmWaveLen == ymengine::kSidWaveSteps);
}

namespace {

// The state with the last `tail` bytes of track 1's TSEQ body taken off, as a version that did not write them.
std::vector<uint8_t> withoutTail(const PluginState& st, uint32_t tail) {
    std::vector<uint8_t> b = encodeState(st);
    size_t at = 0;
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, "TSEQ", 4) == 0) at = i;
    REQUIRE(at > 0);
    auto u32 = [&](size_t i) {
        return uint32_t(b[i]) | uint32_t(b[i + 1]) << 8 | uint32_t(b[i + 2]) << 16 | uint32_t(b[i + 3]) << 24;
    };
    auto put = [&](size_t i, uint32_t v) {
        for (int k = 0; k < 4; ++k) b[i + size_t(k)] = uint8_t(v >> (8 * k));
    };
    // Chunk: u32 size; body: u8 count, u8 track, u32 length, fields; the FM wave, then its scale and semitones last.
    const uint32_t chunk = u32(at + 4), track = u32(at + 8 + 2);
    const size_t end = at + 8 + 2 + 4 + track;
    b.erase(b.begin() + std::ptrdiff_t(end - tail), b.begin() + std::ptrdiff_t(end));
    put(at + 4, chunk - tail);
    put(at + 8 + 2, track - tail);
    return b;
}

}  // namespace

TEST_CASE("FM Voice: a TSEQ chunk written before it keeps the note and its octave", "[fm][state]") {
    auto st = std::make_unique<PluginState>();
    TrackPatch& p = st->extras.tracks[1].patch;
    p.hardSync = true;
    fmWave(p);
    p.fmScale = ymengine::FmScale::Semitones;
    const uint32_t tail = 2 * (1 + ymengine::kSidWaveSteps);  // the FM wave, then its scale and semitones
    auto r = std::make_unique<DecodeResult>(decodeState(withoutTail(*st, tail)));
    REQUIRE(r->ok);
    CHECK(r->state.extras.tracks[1].patch.hardSync);
    CHECK(r->state.extras.tracks[1].patch.fmWave == TrackPatch{}.fmWave);
    CHECK(r->state.extras.tracks[1].patch.fmWaveLen == 2);
    CHECK(r->state.extras.tracks[1].patch.fmScale == ymengine::FmScale::Ratios);
}

TEST_CASE("FM Voice: its scale and semitones after its wave in TSEQ; written before them, Ratios",
          "[fm][state]") {
    auto st = std::make_unique<PluginState>();
    TrackPatch& p = st->extras.tracks[2].patch;
    fmWave(p);
    p.fmScale = ymengine::FmScale::Semitones;
    p.fmSemi = {0, 12, 19, 127, 7, 0, 12, 0, 12, 0, 12, 0, 12, 0, 12, 0};
    const std::vector<uint8_t> bytes = encodeState(*st);
    auto back = std::make_unique<DecodeResult>(decodeState(bytes));
    REQUIRE(back->ok);
    CHECK(back->state.extras.tracks[2].patch.fmScale == ymengine::FmScale::Semitones);
    CHECK(back->state.extras.tracks[2].patch.fmSemi == p.fmSemi);
    CHECK(back->state.extras.tracks[2].patch.fmWave == p.fmWave);
    CHECK(encodeState(back->state) == bytes);
    // A track whose only change is its scale writes its sequences.
    auto only = std::make_unique<PluginState>();
    only->extras.tracks[0].patch.fmScale = ymengine::FmScale::Semitones;
    auto r = std::make_unique<DecodeResult>(decodeState(encodeState(*only)));
    REQUIRE(r->ok);
    CHECK(r->state.extras.tracks[0].patch.fmScale == ymengine::FmScale::Semitones);
    CHECK(r->state.extras.tracks[0].patch.fmSemi == TrackPatch{}.fmSemi);
    // Written by an older version: the wave in ratios kept, the scale Ratios, the semitones of a new track.
    auto old = std::make_unique<DecodeResult>(decodeState(withoutTail(*st, 1 + ymengine::kSidWaveSteps)));
    REQUIRE(old->ok);
    CHECK(old->state.extras.tracks[2].patch.fmWave == p.fmWave);
    CHECK(old->state.extras.tracks[2].patch.fmScale == ymengine::FmScale::Ratios);
    CHECK(old->state.extras.tracks[2].patch.fmSemi == TrackPatch{}.fmSemi);
    // Out of range: a scale beyond the two reads as Ratios, semitones beyond 127 as 127.
    NonParamState n;
    n.tracks[0].patch.fmScale = ymengine::FmScale(9);
    n.tracks[0].patch.fmSemi[3] = 200;
    sanitize(n);
    CHECK(n.tracks[0].patch.fmScale == ymengine::FmScale::Ratios);
    CHECK(n.tracks[0].patch.fmSemi[3] == ymengine::kFmSemitoneMax);
}

TEST_CASE("FM Voice: keys of a patch and of an instrument, absent at the defaults", "[fm][patches]") {
    ParamValues v = defaultValues();
    auto s = std::make_unique<NonParamState>();
    const PatchEntry plain = snapPatch("Plain", v, *s, 0);
    CHECK_FALSE(plain.find("patch")->find("fmWave"));
    CHECK_FALSE(plain.find("patch")->find("fmWaveLen"));

    fmWave(s->tracks[0].patch);
    v[size_t(paramIndex(0, Track::Generator))] = float(int(Generator::Fm));
    const PatchEntry e = snapPatch("FM", v, *s, 0);
    const Value& p = *e.find("patch");
    REQUIRE(p.find("fmWave"));
    CHECK(p.find("fmWave")->items().size() == ymengine::kSidWaveSteps);
    CHECK(p.find("fmWave")->items()[1].number() == 7);
    CHECK(p.find("fmWaveLen")->number() == 3);
    CHECK(p.find("effect")->number() == int(Generator::Fm));

    // Loaded into another track: the wave comes along; a patch without it gives the note and its octave back.
    const PatchChange c = patchChange(e, *s, 9);
    CHECK(c.track.patch.fmWave == s->tracks[0].patch.fmWave);
    CHECK(c.track.patch.fmWaveLen == 3);
    const PatchChange cleared = patchChange(plain, *s, 0);
    CHECK(cleared.track.patch.fmWave == TrackPatch{}.fmWave);
    CHECK(cleared.track.patch.fmWaveLen == 2);

    // Values of an out-of-range file: brought back.
    PatchEntry bad = e;
    Value q = *bad.find("patch");
    Value wave = Value::array();
    for (int k = 0; k < 3; ++k) wave.push(Value(99.0));
    q.set("fmWave", std::move(wave));
    q.set("fmWaveLen", Value(-3.0));
    bad.set("patch", std::move(q));
    const PatchChange b = patchChange(bad, *s, 0);
    CHECK(b.track.patch.fmWave[0] == ymengine::kFmMultipleCount - 1);
    CHECK(b.track.patch.fmWave[3] == 0);
    CHECK(b.track.patch.fmWaveLen == 1);

    // Its scale and semitones: keys of their own, absent at their defaults, read back.
    CHECK_FALSE(p.find("fmScale"));
    CHECK_FALSE(p.find("fmSemi"));
    s->tracks[0].patch.fmScale = ymengine::FmScale::Semitones;
    s->tracks[0].patch.fmSemi[1] = 24;
    const PatchEntry semis = snapPatch("FM semitones", v, *s, 0);
    REQUIRE(semis.find("patch")->find("fmScale"));
    CHECK(semis.find("patch")->find("fmScale")->number() == 1);
    CHECK(semis.find("patch")->find("fmSemi")->items()[1].number() == 24);
    const PatchChange cs = patchChange(semis, *s, 5);
    CHECK(cs.track.patch.fmScale == ymengine::FmScale::Semitones);
    CHECK(cs.track.patch.fmSemi == s->tracks[0].patch.fmSemi);
    CHECK(patchChange(e, *s, 5).track.patch.fmScale == ymengine::FmScale::Ratios);

    // Instrument: same entry, read back identically, generator included.
    const Instrument ins = captureInstrument("FM", v, *s, 0);
    CHECK(ins.patch.generator == Generator::Fm);
    CHECK(ins.patch.fmWave == s->tracks[0].patch.fmWave);
    CHECK(ins.patch.fmWaveLen == 3);
    CHECK(instrumentFromEntry(instrumentEntry(ins)) == ins);
}

TEST_CASE("FM Voice: steps of a recorder take in the RFMW chunk, absent without FM", "[fm][recorder][state]") {
    auto st = std::make_unique<PluginState>();
    st->take.frameHz = 50;
    st->take.clockNum = 2'000'000;
    st->take.clockDen = 1;
    st->take.frames.resize(4);
    st->take.frames[1].fx[0] = {true, Generator::Sid, 13, {1, 37}};
    CHECK_FALSE(hasChunk(encodeState(*st), "RFMW"));
    st->take.frames[2].fx[1] = fmEffect(3, 0x407);
    st->take.frames[3].fx[2] = fmEffect(1, 0x5);
    const std::vector<uint8_t> enc = encodeState(*st);
    CHECK(hasChunk(enc, "RFMW"));
    auto back = std::make_unique<DecodeResult>(decodeState(enc));
    REQUIRE(back->ok);
    CHECK(back->unknownChunks == 0);
    CHECK(back->state.take == st->take);
    CHECK(back->state.take.frames[2].fx[1].gen == Generator::Fm);
    CHECK(back->state.take.frames[2].fx[1].wave == nibbles(0x407));
    CHECK(encodeState(back->state) == enc);
}

TEST_CASE("FM Voice: a take's steps beyond its count or the table read back within them", "[fm][recorder][state]") {
    auto st = std::make_unique<PluginState>();
    st->take.frameHz = 50;
    st->take.clockNum = 2'000'000;
    st->take.clockDen = 1;
    st->take.frames.resize(2);
    st->take.frames[0].fx[0] = fmEffect(2, 0xF9A);  // steps 10 and 9: beyond the table; a third step beyond the count
    auto back = std::make_unique<DecodeResult>(decodeState(encodeState(*st)));
    REQUIRE(back->ok);
    const uint64_t last = uint64_t(ymengine::kFmMultipleCount - 1);
    CHECK(back->state.take.frames[0].fx[0].wave == nibbles(last | last << 4));
}

TEST_CASE("FM Voice: steps in semitones of a recorder take in the RFMS chunk", "[fm][recorder][state]") {
    auto st = std::make_unique<PluginState>();
    st->take.frameHz = 50;
    st->take.clockNum = 2'000'000;
    st->take.clockDen = 1;
    st->take.frames.resize(3);
    st->take.frames[0].fx[0] = fmEffect(3, 0x31);  // in ratios: RFMW
    CHECK_FALSE(hasChunk(encodeState(*st), "RFMS"));
    ymengine::RegisterFrame::Effect& e = st->take.frames[1].fx[2];
    e = fmEffect(4, 0);
    e.semis = true;
    e.wave = {0, 12, 127, 31};
    const std::vector<uint8_t> enc = encodeState(*st);
    CHECK(hasChunk(enc, "RFMS"));
    CHECK(hasChunk(enc, "RFMW"));
    auto back = std::make_unique<DecodeResult>(decodeState(enc));
    REQUIRE(back->ok);
    CHECK(back->unknownChunks == 0);
    CHECK(back->state.take == st->take);
    CHECK(back->state.take.frames[1].fx[2].semis);
    CHECK_FALSE(back->state.take.frames[0].fx[0].semis);
    CHECK(encodeState(back->state) == enc);
    // Steps beyond the effect's count read as 0.
    e.wave[6] = 40;
    auto cut = std::make_unique<DecodeResult>(decodeState(encodeState(*st)));
    REQUIRE(cut->ok);
    CHECK(cut->state.take.frames[1].fx[2].wave == std::array<uint8_t, 16>{0, 12, 127, 31});
}
