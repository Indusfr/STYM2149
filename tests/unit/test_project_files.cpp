// STYM2149 — JSON files of the File menu: preset, pattern, Song and kit written like the prototype (files of
// tools/proto_golden/files.mjs), read back field by field or through the complete STYM2149 state.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#include "plugincore/DigidrumBanks.h"
#include "plugincore/Json.h"
#include "plugincore/ProjectFiles.h"
#include "ymseq/Tables.h"

using namespace stym;
using namespace stym::plugin;
using json::Value;

namespace {

std::string readFile(const std::string& name) {
    std::ifstream f(std::string(STYM_TEST_DATA) + "/files/" + name, std::ios::binary);
    REQUIRE(f);
    std::string text{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());  // git checkout on Windows
    return text;
}

float& param(ParamValues& v, int track, Track f) { return v[size_t(paramIndex(track, f))]; }
float& param(ParamValues& v, Global g) { return v[size_t(paramIndex(g))]; }

// The STYM2149 preset = the prototype's, then the "stym" key (last).
void checkPresetPrefix(const std::string& ours, const std::string& proto) {
    REQUIRE(proto.back() == '}');
    const std::string head = proto.substr(0, proto.size() - 1) + ",\"stym\":{\"v\":1,\"state\":\"";
    CHECK(ours.substr(0, head.size()) == head);
    CHECK(ours.substr(ours.size() - 3) == "\"}}");
}

// The tweaked project of tools/proto_golden/files.mjs, in the STYM2149 model.
struct Project {
    ParamValues v = defaultValues();
    NonParamState x;
};

Project customProject() {
    Project p;
    ParamValues& v = p.v;
    NonParamState& x = p.x;
    param(v, Global::Master) = 42;
    param(v, Global::VoiceMode) = 2;
    param(v, Global::FreeTempo) = 133.33f;
    param(v, Global::Swing) = 62;
    param(v, Global::VblRate) = 1;  // 60 Hz
    x.global.metronome = true;
    x.global.sigNum = ymseq::kSignatures[3].numerator;
    x.global.sigDen = ymseq::kSignatures[3].denominator;
    x.global.resolution = 4;
    x.global.digidrumChannel = {1, 0};
    x.global.songMode = true;

    param(v, 2, Track::On) = 1;
    x.trackNames[2] = "Bass";
    param(v, 2, Track::Mute) = 1;
    param(v, 2, Track::Pan) = -30;
    x.seq.tracks[2].length = 12;
    x.seq.tracks[2].octave = 4;
    param(v, 2, Track::Level) = 60;
    param(v, 2, Track::Gate) = 35;
    param(v, 2, Track::Volume) = 9;
    param(v, 2, Track::EnvPeriod) = 1234 * 32;
    param(v, 2, Track::EnvShape) = 10;
    param(v, 2, Track::Generator) = 1;
    param(v, 2, Track::SidRatio) = 2.5f;
    param(v, 2, Track::Noise) = 1;
    param(v, 2, Track::NoisePeriod) = 20;
    param(v, 2, Track::VibDepth) = 30;
    param(v, 2, Track::VibRate) = 7.5f;
    ymengine::TrackParams& t2 = x.tracks[2];
    t2.patch.ornamentOn = true;
    t2.patch.ornamentLength = 4;
    t2.patch.ornamentPitch[1] = 7;
    param(v, 2, Track::ArpOn) = 1;
    param(v, 2, Track::ArpRate) = 25;
    t2.arp.mode = ymengine::ArpMode(2);
    t2.arp.octaves = 2;
    t2.arp.length = 4;
    t2.arp.table[3] = 5;
    param(v, 5, Track::Solo) = 1;

    ymseq::Project& s = x.seq;
    s.current = 5;
    s.used.reset();
    for (int i : {0, 5, 9}) s.used.set(size_t(i));
    ymseq::Pattern& p5 = s.patterns[5];
    p5.steps = 32;
    ymseq::Lane& l2 = p5.edit(2);
    l2.pat[0] = 0;
    l2.pat[4] = 7;
    l2.oct[4] = 5;
    l2.vol[4] = 12;
    l2.tie[1] = 1;
    l2.slide[4] = 1;
    l2.vpat[0][0] = 4;
    l2.vdur[0][0] = 2;
    l2.gat[8] = 60;
    l2.pnr[8] = 81;
    l2.cnd[12] = 3;
    l2.slc[4] = 6;
    l2.mix[2] = 2;
    l2.slt[2] = 4;
    l2.cut[6] = 1;
    l2.fx[6] = 3;
    l2.rtg[10] = 2;
    l2.afl[3] = 1;  // ARP of the prototype: J01 at step 3, J00 at steps 0 and 4 (twelfth DAW test, item 144)
    ymseq::migrateArpFlags(l2);
    ymseq::Lane& l3 = p5.edit(3);
    l3.pat[0] = 3;
    l3.ton[0] = -5;
    l3.oct[0] = -2;
    s.patterns[0].steps = 16;
    s.patterns[0].edit(0).pat[0] = 0;
    s.patterns[9].steps = 64;
    s.patterns[9].edit(1).pat[63] = 11;
    s.patterns[9].edit(1).vol[63] = 3;
    s.song.entries = {{0, 2, 0}, {5, 1, 140}, {9, 4, 0}};

    ymengine::KitParams& k = x.kit;
    k.inst[0].a = 50;    // kick.pitch
    k.inst[1].b = 40;    // snare.tone
    k.inst[2].dec = 30;  // clap.tail
    k.inst[5].dec = 120; // chat.dec
    k.inst[7].noise = 25;
    return p;
}

// STYM2149 project of which nothing goes through the fields of the prototype.
Project stymProject() {
    Project p = customProject();
    param(p.v, Global::Computer) = 1;
    param(p.v, Global::ChipClock) = 2;
    param(p.v, Global::Timer) = 1;
    p.x.instruments[3].used = true;
    p.x.instruments[3].name = "Lead";
    p.x.instruments[3].patch.ornamentOn = true;
    ymseq::Lane& l = p.x.seq.patterns[5].edit(2);
    l.ins[0] = 4;
    l.setCmd(4, ymengine::seqCmd(ymengine::SeqCmd::Speed, 3));
    l.setCmd(4, ymengine::seqCmd(ymengine::SeqCmd::Vibrato, 1));  // several commands on a step
    l.setCmd(6, ymengine::seqCmd(ymengine::SeqCmd::Vibrato, 0));
    // Step effects and the twelfth DAW test, items 142 and 143 (Pxy dropped; Jxx would be written as ARP flags).
    for (int k = int(ymengine::SeqCmd::VolSlide); k <= int(ymengine::SeqCmd::Mod); ++k)
        l.setCmd(7, ymengine::seqCmd(ymengine::SeqCmd(k), k));
    l.setCmd(9, ymengine::seqCmd(ymengine::SeqCmd::Arpeggiator, 0));  // a J00 that ARP flags do not give back
    p.x.seq.patterns[5].bpm[4] = 0x8c;  // BPM row of the pattern (item 150)
    p.x.seq.song.entries.push_back(ymseq::SongEntry::rest(ymseq::SongKind::NoteOff, 8));
    p.x.seq.song.entries.push_back(ymseq::SongEntry::loop(2, 3));
    p.x.seq.song.entries[1].restart = true;
    p.x.global.timing = ymengine::SeqTiming::Ticks;
    p.x.global.speed = 3;
    p.x.global.metronomeLevel = 40;
    p.x.dmaSamples[1] = {1, -2, 3, -4};
    p.x.digidrums[2] = {1, 2, 3, 15};
    p.x.digiSlots[2].name = "Snare";
    sanitize(p.x);
    return p;
}

std::string withoutStym(const std::string& text) {
    std::optional<Value> v = json::parse(text);
    REQUIRE(v);
    Value out = Value::object();
    for (const auto& [k, x] : v->members())
        if (k != "stym") out.set(k, x);
    return json::dump(out);
}

}  // namespace

TEST_CASE("Files: names offered without PRISM, ensureJson", "[files]") {
    for (int k = 0; k < kFileKinds; ++k) {
        const std::string_view n = defaultFileName(FileKind(k));
        CHECK(n.substr(0, 9) == "stym2149-");
        CHECK(n.find("prism") == std::string_view::npos);
    }
    CHECK(defaultFileName(FileKind::Kit) == "stym2149-kit.json");
    CHECK(ensureJson("  song ") == "song.json");
    CHECK(ensureJson("Kit.JSON") == "Kit.JSON");
    CHECK(ensureJson("a.json.txt") == "a.json.txt.json");
    CHECK(ensureJson("   ").empty());
}

TEST_CASE("JSON: compact arrays of numbers", "[files][json]") {
    const auto v = json::parse("[1, -2, 3.5, 0]");
    REQUIRE(v);
    REQUIRE(v->numbers());
    CHECK(v->size() == 4);
    CHECK(v->items()[2].number() == 3.5);
    CHECK(*v == Value::numberArray({1, -2, 3.5, 0}));
    Value mixed = Value::array();
    for (double d : {1.0, -2.0, 3.5, 0.0}) mixed.push(Value(d));
    CHECK(mixed == *v);
    CHECK(json::dump(*v) == "[1,-2,3.5,0]");
    CHECK(json::dump(*v, 1) == "[\n 1,\n -2,\n 3.5,\n 0\n]");
    Value grown = *v;
    grown.push(Value("x"));
    CHECK_FALSE(grown.numbers());
    CHECK(json::dump(grown) == "[1,-2,3.5,0,\"x\"]");
    const auto m = json::parse("[1, null, 2]");
    REQUIRE(m);
    CHECK_FALSE(m->numbers());
    CHECK(m->items()[1].isNull());
    CHECK(json::parse("[]")->size() == 0);
    CHECK(json::dump(*json::parse("[-0, 12345678901234567, 1e2, -7]")) == "[0,12345678901234568,100,-7]");
}

TEST_CASE("Preset: new page written like the prototype", "[files]") {
    checkPresetPrefix(dumpPreset(defaultValues(), NonParamState{}), readFile("preset_default.json"));
}

TEST_CASE("Preset, pattern, Song, kit: tweaked project written like the prototype", "[files]") {
    const Project p = customProject();
    checkPresetPrefix(dumpPreset(p.v, p.x), readFile("preset_custom.json"));
    CHECK(dumpPatternFile(p.x.seq.cur()) == readFile("pattern_custom.json"));
    CHECK(dumpSongFile(p.x.seq, true) == readFile("song_custom.json"));
    CHECK(dumpKitFile(p.x.kit) == readFile("kit_custom.json"));
}

TEST_CASE("Prototype preset: read field by field, it rewrites the same file", "[files]") {
    SECTION("new page, on a new project") {
        const std::string proto = readFile("preset_default.json");
        const PresetRead r = parsePreset(proto, defaultValues(), NonParamState{});
        REQUIRE(r.status == PresetRead::Status::Ok);
        CHECK_FALSE(r.exact);
        CHECK(withoutStym(dumpPreset(r.values, r.extras)) == proto);
    }
    SECTION("tweaked project, on an already filled project") {
        const std::string proto = readFile("preset_custom.json");
        Project cur = stymProject();
        param(cur.v, 7, Track::Volume) = 3;
        cur.x.seq.current = 12;
        cur.x.seq.used.set(12);
        cur.x.seq.patterns[12].edit(4).setCmd(0, ymengine::seqCmd(ymengine::SeqCmd::Speed, 1));
        const PresetRead r = parsePreset(proto, cur.v, cur.x);
        REQUIRE(r.status == PresetRead::Status::Ok);
        CHECK_FALSE(r.exact);
        CHECK(withoutStym(dumpPreset(r.values, r.extras)) == proto);
        // What the prototype does not say stays the project's (loadPresetObj merges); INS and CMD of the tracks read
        // are cleared.
        CHECK(r.extras.instruments[3].used);
        CHECK(r.values[size_t(paramIndex(Global::Computer))] == 1.0f);
        CHECK(r.extras.seq.current == 5);
        CHECK(r.extras.seq.viewBank == 0);
        CHECK(r.extras.seq.cur().lane(2).ins[0] == 0);
        CHECK(r.extras.seq.cur().lane(2).cmdAt(ymengine::SeqCmd::Speed, 4) == 0);
        CHECK(r.extras.seq.cur().lane(2).cmdAt(ymengine::SeqCmd::Vibrato, 4) == 0);
        CHECK_FALSE(r.extras.seq.cur().lane(2).hasCmd(6));
        CHECK(r.extras.seq.song.entries.size() == 3);
    }
}

// The current pattern of a prototype preset comes from the tracks ("chans"), not from "pats": its ARP flags become Jxx
// and its BPM row is kept, as for the other patterns (otherwise the step arpeggio only played after the DAW project was
// saved and reopened).
TEST_CASE("Prototype preset: the current pattern is migrated like the others", "[files]") {
    const PresetRead r = parsePreset(readFile("preset_custom.json"), defaultValues(), NonParamState{});
    REQUIRE(r.status == PresetRead::Status::Ok);
    REQUIRE(r.extras.seq.current == 5);
    CHECK(r.extras.seq.cur().sameContent(customProject().x.seq.cur()));
    const ymseq::Lane& l = r.extras.seq.cur().lane(2);
    CHECK(l.afl[3] == 0);
    CHECK(l.cmdAt(ymengine::SeqCmd::Arpeggiator, 3) == ymengine::seqCmd(ymengine::SeqCmd::Arpeggiator, 1));
    // Newer version (fields of the prototype only): the BPM row of the current pattern is kept.
    Project p = customProject();
    p.x.seq.cur().bpm[4] = 0x8c;
    Value d = *json::parse(dumpPreset(p.v, p.x));
    Value stym = *d.find("stym");
    stym.set("v", Value(kStymFileVersion + 1));
    d.set("stym", stym);
    const PresetRead n = parsePreset(json::dump(d), defaultValues(), NonParamState{});
    REQUIRE(n.newer);
    CHECK(n.extras.seq.cur().bpm[4] == 0x8c);
    CHECK(n.extras.seq.cur().lane(2).afl[3] == 0);
}

TEST_CASE("STYM2149 preset: the complete state read back identically", "[files]") {
    const Project p = stymProject();
    const std::string text = dumpPreset(p.v, p.x);
    checkPresetPrefix(text, withoutStym(text));  // "stym" last
    const PresetRead r = parsePreset(text, defaultValues(), NonParamState{});
    REQUIRE(r.status == PresetRead::Status::Ok);
    CHECK(r.exact);
    PluginState a, b;
    a.values = p.v;
    a.extras = p.x;
    b.values = r.values;
    b.extras = r.extras;
    CHECK(encodeState(a) == encodeState(b));
    CHECK(r.extras.dmaSamples[1] == std::vector<int8_t>{1, -2, 3, -4});
    CHECK(r.extras.seq.song.entries[3].kind == ymseq::SongKind::NoteOff);
    CHECK(r.extras.global.metronomeLevel == 40);

    // The fields of the prototype carry the STYM2149 additions under their keys.
    const std::optional<Value> d = json::parse(text);
    REQUIRE(d);
    const Value& lane = *d->find("chans")->items()[2].find("lane");
    REQUIRE(lane.find("ins"));
    CHECK(lane.find("ins")->items()[0].number() == 4);
    CHECK(lane.find("cmdS")->items()[4].number() == ymengine::seqCmd(ymengine::SeqCmd::Speed, 3));
    CHECK_FALSE(lane.find("cmdT"));  // the BPM row of the pattern (item 150)
    CHECK(lane.find("cmdV")->items()[6].number() == ymengine::seqCmd(ymengine::SeqCmd::Vibrato, 0));
    const char* const keys[] = {"cmdA", "cmdP", "cmdD", "cmdC", "cmdB", "cmdN",
                                "cmdE", "cmdO", "cmdF", "cmdL", "cmdM"};
    CHECK_FALSE(lane.find("cmd"));  // Xyz and Pxy retired: not written
    CHECK_FALSE(lane.find("cmdP"));
    // Jxx that ARP flags cannot give back: written as they are, the prototype's "afl" left empty (item 144).
    REQUIRE(lane.find("cmdJ"));
    CHECK(lane.find("cmdJ")->items()[9].number() == ymengine::seqCmd(ymengine::SeqCmd::Arpeggiator, 0));
    CHECK(lane.find("afl")->items()[3].number() == 0);
    for (int k = int(ymengine::SeqCmd::VolSlide); k <= int(ymengine::SeqCmd::Mod); ++k) {
        if (ymengine::seqCmdRetired(ymengine::SeqCmd(k))) continue;
        const Value* row = lane.find(keys[k - int(ymengine::SeqCmd::VolSlide)]);
        REQUIRE(row);
        CHECK(row->items()[7].number() == ymengine::seqCmd(ymengine::SeqCmd(k), k));
    }
    CHECK_FALSE(d->find("chans")->items()[3].find("lane")->find("ins"));
    const Value& song = *d->find("song");
    CHECK(song.items()[1].find("restart")->number() == 1);
    CHECK(song.items()[3].find("kind")->string() == "noteoff");
    CHECK(song.items()[3].find("arg")->number() == 8);
    CHECK(song.items()[4].find("kind")->string() == "loop");
    CHECK_FALSE(song.items()[0].find("kind"));
}

TEST_CASE("Preset: refusal, unreadable or more recent state", "[files]") {
    const ParamValues v = defaultValues();
    const NonParamState x;
    CHECK(parsePreset("{", v, x).status == PresetRead::Status::NotJson);
    CHECK(parsePreset("[1]", v, x).status == PresetRead::Status::NotPreset);
    CHECK(parsePreset(R"({"app":"PRISM","v":1})", v, x).status == PresetRead::Status::NotPreset);
    // Fields of the prototype only: the rest of the project does not change.
    const PresetRead bare = parsePreset(R"({"app":"STYM2149"})", v, x);
    CHECK(bare.status == PresetRead::Status::Ok);
    CHECK(bare.values == v);

    const Project p = customProject();
    // More recent version of the "stym" key, then of the state: reading of the fields of the prototype.
    Value d = *json::parse(dumpPreset(p.v, p.x));
    Value stym = *d.find("stym");
    stym.set("v", Value(2));
    d.set("stym", stym);
    PresetRead r = parsePreset(json::dump(d), v, x);
    CHECK(r.newer);
    CHECK_FALSE(r.exact);
    CHECK(r.values[size_t(paramIndex(Global::Master))] == 42.0f);
    std::vector<uint8_t> state = encodeState(PluginState{});
    state[4] = 9;  // state version
    stym.set("v", Value(1));
    stym.set("state", Value(base64Encode(state)));
    d.set("stym", stym);
    r = parsePreset(json::dump(d), v, x);
    CHECK(r.newer);
    CHECK(r.extras.seq.current == 5);
    stym.set("state", Value("not base64 state"));
    d.set("stym", stym);
    r = parsePreset(json::dump(d), v, x);
    CHECK_FALSE(r.newer);
    CHECK_FALSE(r.exact);
    CHECK(r.extras.seq.current == 5);
}

TEST_CASE("Prototype preset: partial keys and out-of-range values", "[files]") {
    // GLOB merged (Object.assign); old time signature and resolution clamped; track without a patch; old 64-step
    // pattern completed; block tempo clamped; old kit converted.
    const std::string text = R"({"app":"STYM2149","v":1,"glob":{"bpm":500,"mode3ch":1,"sig":40,"reso":-3,
        "vbl":61,"swing":0,"steps":300},"curPat":40,"chans":[{"enabled":0,"level":0.2,"pan":-2,"name":"",
        "lane":{"pat":[0,1,2],"vol":[4]}},{"patch":{"vol4":3},"seqOct":12,"plen":99,"mute":7}],
        "pats":[null,{"steps":8,"chans":[{"pat":[5]}]}],"song":[{"pat":3,"rep":0,"bpm":5},{"pat":2.9,"rep":2.5}],
        "songOn":"yes","kit":{"hat":{"metal":50,"dec":50}}})";
    ParamValues v0 = defaultValues();
    param(v0, Global::Swing) = 70;
    NonParamState x0;
    x0.seq.cur().edit(0).tie[7] = 1;
    const PresetRead r = parsePreset(text, v0, x0);
    REQUIRE(r.status == PresetRead::Status::Ok);
    const ParamValues& v = r.values;
    CHECK(v[size_t(paramIndex(Global::FreeTempo))] == 240.0f);
    CHECK(v[size_t(paramIndex(Global::VoiceMode))] == 1.0f);
    CHECK(v[size_t(paramIndex(Global::VblRate))] == 0.0f);   // 61 Hz: unknown, unchanged
    CHECK(v[size_t(paramIndex(Global::Swing))] == 50.0f);    // GLOB.swing || 50
    const NonParamState& x = r.extras;
    CHECK(x.global.sigNum == ymseq::kSignatures[11].numerator);
    CHECK(x.global.resolution == 0);
    CHECK(x.seq.current == 31);
    CHECK(x.seq.viewBank == 3);
    CHECK(x.seq.cur().steps == 256);
    // Track 1: rows read are completed, missing rows kept (Object.assign of the lane).
    const ymseq::Lane& l0 = x.seq.cur().lane(0);
    CHECK(l0.pat[2] == 2);
    CHECK(l0.pat[3] == -1);
    CHECK(l0.vol[0] == 4);
    CHECK(l0.vol[1] == 15);
    CHECK(l0.tie[7] == 1);
    CHECK(v[size_t(paramIndex(0, Track::On))] == 0.0f);
    CHECK(v[size_t(paramIndex(0, Track::Level))] == 20.0f);
    CHECK(v[size_t(paramIndex(0, Track::Pan))] == -100.0f);
    CHECK(v[size_t(paramIndex(1, Track::Volume))] == 3.0f);
    CHECK(v[size_t(paramIndex(1, Track::Level))] == 100.0f);  // s.level ?? 1
    CHECK(v[size_t(paramIndex(1, Track::Mute))] == 1.0f);
    CHECK(x.seq.tracks[1].octave == 9);
    CHECK(x.seq.tracks[1].length == 64);
    CHECK_FALSE(x.seq.used[0]);
    REQUIRE(x.seq.used[1]);
    CHECK(x.seq.patterns[1].steps == 8);
    CHECK(x.seq.patterns[1].lane(0).pat[0] == 5);
    CHECK(x.seq.patterns[1].lane(0).pat[1] == -1);
    REQUIRE(x.seq.song.entries.size() == 2);
    CHECK(x.seq.song.entries[0].rep == 1);
    CHECK(x.seq.song.entries[0].bpm == 20);
    CHECK(x.seq.song.entries[1].pat == 2);
    CHECK(x.seq.song.entries[1].rep == 2);
    CHECK(x.global.songMode);
    CHECK(x.kit.inst[3].a == 50);
    CHECK(x.kit.inst[3].dec == 23);  // 45 × 0.5 (Math.round)
    CHECK(x.kit.inst[4].dec == 5);   // 9 × 0.5
    CHECK(x.kit.inst[5].dec == 45);
    CHECK(x.kit.inst[5].a == 50);
}

TEST_CASE("Pattern: file read back, STYM2149 rows, refusal", "[files]") {
    const Project p = stymProject();
    const std::string text = dumpPatternFile(p.x.seq.cur());
    const std::optional<ymseq::Pattern> back = parsePatternFile(text);
    REQUIRE(back);
    CHECK(back->sameContent(p.x.seq.cur()));
    CHECK(back->steps == 32);
    CHECK(back->bpm[4] == 0x8c);
    const auto proto = parsePatternFile(readFile("pattern_custom.json"));
    REQUIRE(proto);
    CHECK(proto->sameContent(customProject().x.seq.cur()));
    // ARP of the prototype (twelfth DAW test, item 144): its flags are read as Jxx and written back as flags.
    CHECK(proto->lane(2).cmdAt(ymengine::SeqCmd::Arpeggiator, 3) == ymengine::seqCmd(ymengine::SeqCmd::Arpeggiator, 1));
    CHECK(proto->lane(2).cmdAt(ymengine::SeqCmd::Arpeggiator, 4) == ymengine::seqCmd(ymengine::SeqCmd::Arpeggiator, 0));
    const std::optional<Value> pd = json::parse(dumpPatternFile(*proto));
    REQUIRE(pd);
    const Value& chan2 = pd->find("pattern")->find("chans")->items()[2];
    CHECK(chan2.find("afl")->items()[3].number() == 1);
    CHECK(chan2.find("afl")->items()[4].number() == 0);
    CHECK_FALSE(chan2.find("cmdJ"));
    CHECK_FALSE(pd->find("pattern")->find("bpm"));

    CHECK_FALSE(parsePatternFile("{"));
    CHECK_FALSE(parsePatternFile(R"({"kind":"song","pattern":{"steps":4,"chans":[]}})"));
    CHECK_FALSE(parsePatternFile(R"({"kind":"pattern"})"));
    CHECK_FALSE(parsePatternFile(R"({"kind":"pattern","pattern":{"steps":4}})"));
    // Old format: 64 steps completed; out-of-range values clamped; unknown command: empty cell.
    const auto old = parsePatternFile(R"({"kind":"pattern","pattern":{"steps":64,"chans":[{"pat":[11,40,-9],
        "vol":[99],"oct":[20],"cmd":[70000,256,1420],"ins":[50]}]}})");
    REQUIRE(old);
    const ymseq::Lane& l = old->lane(0);
    CHECK(l.pat[0] == 11);
    CHECK(l.pat[1] == 12);
    CHECK(l.pat[2] == -1);
    CHECK(l.vol[0] == 15);
    CHECK(l.oct[0] == 9);
    CHECK_FALSE(l.hasCmd(0));
    CHECK_FALSE(l.hasCmd(1));  // X00: Xyz retired, dropped
    // T8C in "cmd" (older file): the row of its kind, then the BPM row of the pattern (item 150).
    CHECK(l.cmdAt(ymengine::SeqCmd::Tempo, 2) == 0);
    CHECK(old->bpm[2] == 0x8c);
    CHECK(l.ins[0] == 32);
    CHECK(old->lanes[1] == nullptr);
}

TEST_CASE("Song: file read back, STYM2149 entries, patterns replaced", "[files]") {
    const Project p = stymProject();
    const std::optional<SongFile> f = parseSongFile(dumpSongFile(p.x.seq, true));
    REQUIRE(f);
    CHECK(f->songOn);
    CHECK(f->song == p.x.seq.song);
    CHECK(f->hasPatterns);

    ymseq::Project target;
    target.current = 3;
    target.used.set(3);
    target.patterns[3].steps = 48;
    target.patterns[20].steps = 12;
    target.used.set(20);
    bool on = false;
    applySongFile(*f, target, on);
    CHECK(on);
    CHECK(target.song == p.x.seq.song);
    for (int i = 0; i < ymseq::kPatterns; ++i) {
        CAPTURE(i);
        if (i == 3) continue;
        CHECK(target.used[size_t(i)] == p.x.seq.used[size_t(i)]);
        if (target.used[size_t(i)]) CHECK(target.patterns[size_t(i)].sameContent(p.x.seq.patterns[size_t(i)]));
    }
    // Current pattern missing from the file: empty, of the current length (emptyPattern).
    CHECK(target.used[3]);
    CHECK(target.patterns[3].steps == 48);
    CHECK(target.patterns[3].lanes[0] == nullptr);

    // Without "pats": the patterns stay; Song of the prototype read back.
    const auto bare = parseSongFile(R"({"kind":"song","song":[{"pat":1,"rep":3,"bpm":90}],"songOn":0})");
    REQUIRE(bare);
    ymseq::Project keep = p.x.seq;
    applySongFile(*bare, keep, on);
    CHECK_FALSE(on);
    REQUIRE(keep.song.size() == 1);
    CHECK(keep.song.entries[0].bpm == 90);
    CHECK(keep.patterns[9].sameContent(p.x.seq.patterns[9]));
    const auto proto = parseSongFile(readFile("song_custom.json"));
    REQUIRE(proto);
    CHECK(proto->song == customProject().x.seq.song);

    CHECK_FALSE(parseSongFile(R"({"kind":"song"})"));
    CHECK_FALSE(parseSongFile(R"({"kind":"pattern","song":[]})"));
    CHECK_FALSE(parseSongFile("nope"));
}

TEST_CASE("Kit: file read back, merge per instrument, old format, refusal", "[files]") {
    const ymengine::KitParams custom = customProject().x.kit;
    const auto back = parseKitFile(readFile("kit_custom.json"), ymengine::KitParams{});
    REQUIRE(back);
    CHECK(*back == custom);
    const ymengine::KitParams def = ymengine::kKitBanks[0].kit;
    const auto part = parseKitFile(R"({"kind":"drumkit","kit":{"snare":{"tone":12},"kick":{"pitch":1e9},
        "tom":{"pitch":"x"},"bogus":{}}})", def);
    REQUIRE(part);
    CHECK(part->inst[1].b == 12);
    CHECK(part->inst[1].a == def.inst[1].a);
    CHECK(part->inst[0].a == 90);  // clamped
    CHECK(part->inst[6] == def.inst[6]);
    const auto old = parseKitFile(R"({"kind":"drumkit","kit":{"hat":{"metal":10,"tune":70,"dec":200}}})", def);
    REQUIRE(old);
    CHECK(old->inst[3].dec == 90);
    CHECK(old->inst[4].dec == 18);
    CHECK(old->inst[5].dec == 180);
    CHECK(old->inst[4].a == 10);
    CHECK_FALSE(parseKitFile(R"({"kind":"patchlib","kit":{}})", def));
    CHECK_FALSE(parseKitFile(R"({"kind":"drumkit"})", def));
    CHECK_FALSE(parseKitFile("", def));
}

TEST_CASE("Versioned files: stym key of the STYM2149 additions, more recent version reported", "[files]") {
    const std::string mark = ",\"stym\":{\"v\":1}}";
    auto endsWith = [](const std::string& s, const std::string& end) {
        return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
    };
    // Pattern: the key follows the INS and CMD rows; without them, the file stays the prototype's.
    const Project p = stymProject();
    const std::string pat = dumpPatternFile(p.x.seq.cur());
    CHECK(endsWith(pat, mark));  // without them: file of the prototype ("tweaked project written like the prototype")
    bool newer = true;
    REQUIRE(parsePatternFile(pat, &newer));
    CHECK_FALSE(newer);
    std::string later = pat.substr(0, pat.size() - mark.size()) + ",\"stym\":{\"v\":2,\"more\":[1]}}";
    REQUIRE(parsePatternFile(later, &newer));
    CHECK(newer);
    CHECK(parsePatternFile(later)->sameContent(p.x.seq.cur()));  // what is known is read

    // Song: extended entries or STYM2149 rows in its patterns.
    const std::string song = dumpSongFile(p.x.seq, true);
    CHECK(endsWith(song, mark));
    CHECK_FALSE(parseSongFile(song)->newer);
    later = song.substr(0, song.size() - mark.size()) + ",\"stym\":{\"v\":9}}";
    const auto f = parseSongFile(later);
    REQUIRE(f);
    CHECK(f->newer);
    CHECK(f->song == p.x.seq.song);

    // Kit: nothing added (no key); a more recent key is reported.
    CHECK(dumpKitFile(p.x.kit).find("stym") == std::string::npos);
    REQUIRE(parseKitFile(R"({"kind":"drumkit","kit":{},"stym":{"v":2}})", p.x.kit, &newer));
    CHECK(newer);
    REQUIRE(parseKitFile(R"({"kind":"drumkit","kit":{},"stym":{"v":1}})", p.x.kit, &newer));
    CHECK_FALSE(newer);
    // Preset: the "stym" key already carries its version.
    CHECK(dumpPreset(p.v, p.x).find(",\"stym\":{\"v\":1,\"state\":") != std::string::npos);
}
