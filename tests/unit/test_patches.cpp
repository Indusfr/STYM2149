// STYM2149 — JSON of the prototype files and Patches library (M6, A3, D-141)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#include "plugincore/Json.h"
#include "plugincore/Patches.h"

using namespace stym;
using namespace stym::plugin;
using json::Value;

namespace {

std::string readFile(const std::string& name) {
    std::ifstream f(std::string(STYM_TEST_DATA) + "/patches/" + name, std::ios::binary);
    REQUIRE(f);
    std::string text{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    // The prototype writes LFs; a git checkout on Windows may give CRLFs.
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

float& param(ParamValues& v, int track, Track f) { return v[size_t(paramIndex(track, f))]; }

}  // namespace

TEST_CASE("JSON: numbers written like JSON.stringify", "[json]") {
    auto num = [](double v) { return json::dump(Value(v)); };
    CHECK(num(0) == "0");
    CHECK(num(-0.0) == "0");
    CHECK(num(400) == "400");
    CHECK(num(-12) == "-12");
    CHECK(num(0.75) == "0.75");
    CHECK(num(2.37) == "2.37");
    CHECK(num(12.53125) == "12.53125");
    CHECK(num(0.1 + 0.2) == "0.30000000000000004");
    CHECK(num(1e21) == "1e+21");
    CHECK(num(123456789012345680000.0) == "123456789012345680000");
    CHECK(num(0.000001) == "0.000001");
    CHECK(num(1.5e-7) == "1.5e-7");
    CHECK(num(std::numeric_limits<double>::quiet_NaN()) == "null");
    CHECK(num(std::numeric_limits<double>::infinity()) == "null");
}

TEST_CASE("JSON: reading, writing, key order, strings and errors", "[json]") {
    const std::string text = R"({"b": [1, 2.5, -3e2, true, false, null], "a": {"x": "é\u00e9\ud83c\udfb5\n\"\\/"}, "b2": {}})";
    std::string error;
    const auto v = json::parse(text, &error);
    REQUIRE(v);
    CHECK(error.empty());
    REQUIRE(v->members().size() == 3);
    CHECK(v->members()[0].first == "b");  // order of the text
    const Value& b = *v->find("b");
    REQUIRE(b.items().size() == 6);
    CHECK(b.items()[2].number() == -300);
    CHECK(b.items()[3].truthy());
    CHECK(b.items()[5].isNull());
    CHECK(v->find("a")->find("x")->string() == "éé\xF0\x9F\x8E\xB5\n\"\\/");
    // Outside the macro: MSVC rejects (C2017) a raw string containing \" once the macro is stringised.
    const std::string dumped = R"({"b":[1,2.5,-300,true,false,null],"a":{"x":"éé🎵\n\"\\/"},"b2":{}})";
    CHECK(json::dump(*v) == dumped);
    CHECK(json::dump(*json::parse("[]"), 1) == "[]");
    CHECK(json::dump(*json::parse(R"({"k":[1,{"z":0}]})"), 1) == "{\n \"k\": [\n  1,\n  {\n   \"z\": 0\n  }\n ]\n}");
    // Repeated key: the last one wins; byte order mark ignored.
    CHECK(json::parse(R"({"k":1,"k":2})")->find("k")->number() == 2);
    CHECK(json::parse("\xEF\xBB\xBF [1] ")->items().size() == 1);

    for (const char* bad : {"", "[1,]", "{\"a\" 1}", "[1] x", "\"abc", "01", "1.", "-", "{'a':1}", "[\"\x01\"]",
                            "nul", "\"\\x\""}) {
        CAPTURE(bad);
        std::string why;
        CHECK_FALSE(json::parse(bad, &why));
        CHECK_FALSE(why.empty());
    }
    std::string deep(300, '[');
    deep += std::string(300, ']');
    CHECK_FALSE(json::parse(deep));
    CHECK(json::parse(std::string(200, '[') + std::string(200, ']')));
}

TEST_CASE("Patches: the sound of a new track is written like the export of the prototype", "[patches]") {
    const ParamValues v = defaultValues();
    const NonParamState s;
    const std::vector<PatchEntry> lib = {snapPatch("Patch 1", v, s, 0)};
    CHECK(dumpPatchLibrary(lib) == readFile("proto_lib_default.json"));
}

TEST_CASE("Patches: a tweaked sound is written like the prototype and read back identically", "[patches]") {
    // The settings of track 1 of the prototype in proto_lib_custom.json (tools/proto_capture is not involved: file
    // written by the prototype, CH[0] modified then libSnap).
    ParamValues v = defaultValues();
    NonParamState s;
    param(v, 0, Track::EnvPeriod) = 1234 * 32;
    param(v, 0, Track::Generator) = 1;
    param(v, 0, Track::SidRatio) = 2.37f;
    param(v, 0, Track::VibRate) = 7.5f;
    param(v, 0, Track::Attack) = 120;
    param(v, 0, Track::Level) = 42;
    param(v, 0, Track::Gate) = 30;
    ymengine::TrackParams& t = s.tracks[0];
    t.patch.ornamentOn = true;
    t.patch.ornamentPitch[2] = -5;
    t.patch.ornamentVolume[3] = 4;
    t.arp.mode = ymengine::ArpMode::Table;
    t.arp.table[3] = 12;
    t.arp.length = 4;
    const std::vector<PatchEntry> lib = {snapPatch("Lead \"\xC3\xA9\"", v, s, 0)};
    CHECK(dumpPatchLibrary(lib) == readFile("proto_lib_custom.json"));

    // Read back on track 3 of a new project: same parameters and same track settings.
    const auto read = parsePatchLibrary(readFile("proto_lib_custom.json"));
    REQUIRE(read);
    REQUIRE(read->size() == 1);
    CHECK(patchName((*read)[0]) == "Lead \"\xC3\xA9\"");
    const PatchChange c = patchChange((*read)[0], NonParamState{}, 2);
    ParamValues w = defaultValues();
    for (const auto& [i, x] : c.params) w[size_t(i)] = x;
    for (int f = 0; f < kTrackParams; ++f) {
        if (!inPatch(Track(f))) continue;
        CAPTURE(paramSpec(paramIndex(0, Track(f))).id);
        CHECK(w[size_t(paramIndex(2, Track(f)))] == v[size_t(paramIndex(0, Track(f)))]);
    }
    CHECK(c.params.size() == 23);
    CHECK(c.track.patch.ornamentOn);
    CHECK(c.track.patch.ornamentPitch == t.patch.ornamentPitch);
    CHECK(c.track.patch.ornamentVolume == t.patch.ornamentVolume);
    CHECK(c.track.arp.mode == ymengine::ArpMode::Table);
    CHECK(c.track.arp.table == t.arp.table);
    CHECK(c.track.arp.length == 4);
}

TEST_CASE("Patches: missing keys, out-of-range values, short arrays", "[patches]") {
    // Missing keys: new track (mkCh); EP beyond 2047 capped at 65535 (D-021); the rest within its range.
    const auto e = json::parse(R"({"name":"x","patch":{"EP":3000,"vol4":99,"NP":0,"effect":7,"ornLen":200,
        "ornTable":[30,-30,"a",3],"ornNse":[2],"ornVol":[-4,40],"sidRatio":1e9,"vibRate":7.3,"slot":12},
        "arp":{"mode":9,"oct":0,"table":[5,99,-1],"tlen":40},"level":2,"gate":0})");
    REQUIRE(e);
    NonParamState s;
    s.tracks[0].patch.ornamentPitch[10] = 7;  // replaced: the table comes from the patch
    const PatchChange c = patchChange(*e, s, 0);
    ParamValues w = defaultValues();
    for (const auto& [i, x] : c.params) w[size_t(i)] = x;
    CHECK(param(w, 0, Track::EnvPeriod) == 65535.0f);
    CHECK(param(w, 0, Track::Volume) == 15.0f);
    CHECK(param(w, 0, Track::NoisePeriod) == 1.0f);
    CHECK(param(w, 0, Track::Generator) == 5.0f);  // last type (DMA since M6)
    CHECK(param(w, 0, Track::SidRatio) == 8.0f);
    CHECK(param(w, 0, Track::VibRate) == 7.5f);
    CHECK(param(w, 0, Track::Level) == 100.0f);
    CHECK(param(w, 0, Track::Gate) == 5.0f);
    CHECK(param(w, 0, Track::Tone) == 1.0f);      // missing: mkCh
    CHECK(param(w, 0, Track::Sustain) == 100.0f);  // missing: mkCh
    CHECK(param(w, 0, Track::ArpRate) == 50.0f);   // missing: mkCh
    const ymengine::TrackPatch& p = c.track.patch;
    CHECK(p.ornamentLength == 64);
    CHECK(p.ornamentPitch[0] == 12);
    CHECK(p.ornamentPitch[1] == -12);
    CHECK(p.ornamentPitch[2] == 0);
    CHECK(p.ornamentPitch[3] == 3);
    CHECK(p.ornamentPitch[10] == 0);
    CHECK(p.ornamentNoise[0] == 1);
    CHECK(p.ornamentVolume[0] == 0);
    CHECK(p.ornamentVolume[1] == 15);
    CHECK(p.ornamentVolume[63] == 15);
    CHECK(p.digidrumSlot == 7);
    CHECK(c.track.arp.mode == ymengine::ArpMode::Table);
    CHECK(c.track.arp.octaves == 1);
    CHECK(c.track.arp.table[0] == 5);
    CHECK(c.track.arp.table[1] == 24);
    CHECK(c.track.arp.table[2] == 0);
    CHECK(c.track.arp.table[3] == 0);
    CHECK(c.track.arp.length == 16);

    // Neither patch nor arp: the new track.
    const PatchChange d = patchChange(*json::parse(R"({"name":"y"})"), NonParamState{}, 1);
    ParamValues u = defaultValues();
    for (const auto& [i, x] : d.params) u[size_t(i)] = x;
    CHECK(u == defaultValues());
    CHECK(d.track.arp.table == ymengine::TrackArp{}.table);
}

TEST_CASE("Patches: library file, bare array, invalid entries, renaming", "[patches]") {
    const auto lib = parsePatchLibrary(R"({"kind":"patchlib","patches":[{"name":"A","patch":{}},{"name":3,"patch":{}},
        {"name":"B"},{"name":"C","patch":{"vol4":5},"extra":[1]},7]})");
    REQUIRE(lib);
    REQUIRE(lib->size() == 2);
    CHECK(patchName((*lib)[0]) == "A");
    CHECK(patchName((*lib)[1]) == "C");
    CHECK((*lib)[1].find("extra"));  // unknown key kept
    const auto bare = parsePatchLibrary(R"([{"name":"Z","patch":{}}])");
    REQUIRE(bare);
    CHECK(bare->size() == 1);
    std::string why;
    CHECK_FALSE(parsePatchLibrary(R"({"kind":"drumkit","kit":{}})", &why));
    CHECK(why == "not a patch library");
    CHECK_FALSE(parsePatchLibrary("{", &why));

    std::vector<PatchEntry> v = *lib;
    renamePatch(v[1], "Lead");
    const auto again = parsePatchLibrary(dumpPatchLibrary(v));
    REQUIRE(again);
    CHECK(patchName((*again)[1]) == "Lead");
    CHECK(*again == v);
    CHECK(dumpPatchLibrary({}) == "{\n \"kind\": \"patchlib\",\n \"patches\": []\n}");
}

TEST_CASE("Patches: stym key when a patch carries STYM2149 keys (B6)", "[patches]") {
    // Patch of the prototype: file unchanged; STYM2149 key (sequence, DMA...): version of the additions at the end.
    const auto plain = parsePatchLibrary(R"({"kind":"patchlib","patches":[{"name":"A","patch":{"vol4":5}}]})");
    REQUIRE(plain);
    CHECK(dumpPatchLibrary(*plain).find("stym") == std::string::npos);
    const auto ext = parsePatchLibrary(R"({"kind":"patchlib","patches":[{"name":"A","patch":{"seqPer":[1,2]}}]})");
    REQUIRE(ext);
    const std::string text = dumpPatchLibrary(*ext);
    const std::string tail = ",\n \"stym\": {\n  \"v\": 1\n }\n}";
    REQUIRE(text.size() > tail.size());
    CHECK(text.substr(text.size() - tail.size()) == tail);
    bool newer = true;
    REQUIRE(parsePatchLibrary(text, nullptr, &newer));
    CHECK_FALSE(newer);
    REQUIRE(parsePatchLibrary(R"({"kind":"patchlib","patches":[],"stym":{"v":3}})", nullptr, &newer));
    CHECK(newer);
    REQUIRE(parsePatchLibrary(R"([{"name":"Z","patch":{}}])", nullptr, &newer));
    CHECK_FALSE(newer);
}

TEST_CASE("Patches: vibrato Off written as a STYM2149 key, On when absent (sixth DAW test, D-254)", "[patches]") {
    ParamValues v = defaultValues();
    param(v, 0, Track::VibOn) = 0;
    const std::vector<PatchEntry> lib = {snapPatch("No vib", v, NonParamState{}, 0)};
    const std::string text = dumpPatchLibrary(lib);
    CHECK(text.find("\"vibOn\": 0") != std::string::npos);
    CHECK(text.find("\"stym\"") != std::string::npos);
    const auto read = parsePatchLibrary(text);
    REQUIRE(read);
    ParamValues w = defaultValues();
    for (const auto& [i, x] : patchChange((*read)[0], NonParamState{}, 1).params) w[size_t(i)] = x;
    CHECK(param(w, 1, Track::VibOn) == 0.0f);
    // A patch of the prototype has no vibOn: vibrato On.
    param(w, 1, Track::VibOn) = 0;
    for (const auto& [i, x] : patchChange(snapPatch("P", defaultValues(), NonParamState{}, 0), {}, 1).params)
        w[size_t(i)] = x;
    CHECK(param(w, 1, Track::VibOn) == 1.0f);
}
