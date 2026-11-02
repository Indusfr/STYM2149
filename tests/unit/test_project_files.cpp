// STYM2149 — files of the File menu: binary preset, pattern, Song and kit, and the naming and version
// rules of the Patches library (JSON). The JSON files of the prototype are gone.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "plugincore/Json.h"
#include "plugincore/ProjectFiles.h"
#include "ymseq/Tables.h"

using namespace stym;
using namespace stym::plugin;
using json::Value;

namespace {

float& param(ParamValues& v, int track, Track f) { return v[size_t(paramIndex(track, f))]; }
float& param(ParamValues& v, Global g) { return v[size_t(paramIndex(g))]; }

// A tweaked project in the STYM2149 model.
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

// The same with settings that only STYM2149 has.
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

}  // namespace

TEST_CASE("Files: names offered without PRISM, ensureJson, ensureExtension", "[files]") {
    CHECK(kPatchLibraryFileName == "stym2149-patches.json");
    CHECK(ensureJson("  song ") == "song.json");
    CHECK(ensureJson("Kit.JSON") == "Kit.JSON");
    CHECK(ensureJson("a.json.txt") == "a.json.txt.json");
    CHECK(ensureJson("   ").empty());
    CHECK(ensureExtension("tune", {".stym"}) == "tune.stym");
    CHECK(ensureExtension("TUNE.STYM", {".stym"}) == "TUNE.STYM");
    CHECK(ensureExtension("a.ym6", {".ym", ".ym6"}) == "a.ym6");
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

TEST_CASE("Versioned library: stym key of the STYM2149 additions, more recent version reported", "[files]") {
    Value doc = Value::object();
    doc.set("kind", Value("patchlib"));
    CHECK_FALSE(newerStymFile(doc));
    markStymFile(doc);
    CHECK(json::dump(doc) == R"({"kind":"patchlib","stym":{"v":1}})");
    CHECK_FALSE(newerStymFile(doc));
    CHECK(newerStymFile(*json::parse(R"({"kind":"patchlib","stym":{"v":2,"more":[1]}})")));
    CHECK_FALSE(newerStymFile(*json::parse(R"({"stym":{"v":null}})")));
    CHECK_FALSE(newerStymFile(*json::parse(R"({"stym":2})")));
    CHECK_FALSE(newerStymFile(*json::parse("[1,2]")));
}

TEST_CASE("Binary files: kind in the header, what each kind carries, refusals", "[files]") {
    Project p = stymProject();
    p.x.seq.current = 5;  // the pattern with STYM2149 rows
    p.x.seq.used.set(5);
    p.x.global.songMode = true;
    p.x.kit.inst[2].a = 77;
    sanitize(p.x);

    CHECK(nativeExtension(FileKind::Preset) == ".stym");
    CHECK(nativeFileName(FileKind::Pattern) == "stym2149.stympat");
    CHECK(nativeFileName(FileKind::Song) == "stym2149.stymsng");
    CHECK(nativeFileName(FileKind::Kit) == "stym2149.stymkit");

    // Project: the state the host saves, without the recorder take; flags 0.
    const std::vector<uint8_t> proj = encodeNativeFile(FileKind::Preset, p.v, p.x);
    PluginState st;
    st.values = p.v;
    st.extras = p.x;
    st.take.frames.resize(3);
    CHECK(proj == encodeState(st, StateKind::State, false));
    CHECK(proj[6] == 0);
    const std::string_view projBytes(reinterpret_cast<const char*>(proj.data()), proj.size());
    CHECK(isNativeFile(projBytes));
    NativeRead r = readNativeFile(projBytes);
    REQUIRE(r.ok);
    CHECK(r.kind == FileKind::Preset);
    CHECK(r.values == p.v);
    PluginState back;
    back.values = r.values;
    back.extras = r.extras;
    CHECK(encodeState(back) == encodeState(PluginState{p.v, p.x, {}}));

    // Pattern: the current pattern alone, read back whole.
    const std::vector<uint8_t> pat = encodeNativeFile(FileKind::Pattern, p.v, p.x);
    CHECK(pat[6] == 1);
    r = readNativeFile({reinterpret_cast<const char*>(pat.data()), pat.size()});
    REQUIRE(r.ok);
    CHECK(r.kind == FileKind::Pattern);
    CHECK(r.pattern.sameContent(p.x.seq.patterns[5]));
    CHECK(r.pattern.bpm == p.x.seq.patterns[5].bpm);
    CHECK(pat.size() < proj.size());

    // Song: entries (with their kinds), Song on, patterns created.
    const std::vector<uint8_t> song = encodeNativeFile(FileKind::Song, p.v, p.x);
    CHECK(song[6] == 2);
    r = readNativeFile({reinterpret_cast<const char*>(song.data()), song.size()});
    REQUIRE(r.ok);
    CHECK(r.kind == FileKind::Song);
    CHECK(r.song.song == p.x.seq.song);
    CHECK(r.song.songOn);
    CHECK(r.song.hasPatterns);
    for (int i = 0; i < ymseq::kPatterns; ++i) {
        CAPTURE(i);
        CHECK(r.song.patterns[size_t(i)].has_value() == bool(p.x.seq.used[size_t(i)]));
        if (r.song.patterns[size_t(i)]) CHECK(r.song.patterns[size_t(i)]->sameContent(p.x.seq.patterns[size_t(i)]));
    }

    // Kit.
    const std::vector<uint8_t> kit = encodeNativeFile(FileKind::Kit, p.v, p.x);
    CHECK(kit[6] == 3);
    r = readNativeFile({reinterpret_cast<const char*>(kit.data()), kit.size()});
    REQUIRE(r.ok);
    CHECK(r.kind == FileKind::Kit);
    CHECK(r.kit == p.x.kit);

    // Refusals: not the format, unknown kind, more recent version; the host ignores a file that is not a state.
    CHECK_FALSE(isNativeFile("{\"app\""));
    CHECK_FALSE(readNativeFile("{\"app\":\"STYM2149\"}").ok);
    std::vector<uint8_t> odd = kit;
    odd[6] = 4;
    CHECK_FALSE(decodeState(odd).ok);
    odd[6] = 3;
    odd[4] = uint8_t(kStateVersion + 1);
    r = readNativeFile({reinterpret_cast<const char*>(odd.data()), odd.size()});
    CHECK_FALSE(r.ok);
    CHECK(r.newer);
    const DecodeResult d = decodeState(kit);
    REQUIRE(d.ok);
    CHECK(d.kind == StateKind::Kit);
}

TEST_CASE("Song file applied: Song replaced, patterns replaced, missing current pattern emptied", "[files]") {
    const Project p = stymProject();
    const std::vector<uint8_t> bytes = encodeNativeFile(FileKind::Song, p.v, p.x);
    const NativeRead r = readNativeFile({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    REQUIRE(r.ok);

    ymseq::Project target;
    target.current = 3;
    target.used.set(3);
    target.patterns[3].steps = 48;
    target.patterns[20].steps = 12;
    target.used.set(20);
    target.pending = 20;
    bool on = false;
    applySongFile(r.song, target, on);
    CHECK(on);
    CHECK(target.song == p.x.seq.song);
    for (int i = 0; i < ymseq::kPatterns; ++i) {
        CAPTURE(i);
        if (i == 3) continue;
        CHECK(target.used[size_t(i)] == p.x.seq.used[size_t(i)]);
        if (target.used[size_t(i)]) CHECK(target.patterns[size_t(i)].sameContent(p.x.seq.patterns[size_t(i)]));
    }
    // Current pattern missing from the file: empty, of the current length (emptyPattern); a pending switch to a
    // pattern that is gone is dropped.
    CHECK(target.used[3]);
    CHECK(target.patterns[3].steps == 48);
    CHECK(target.patterns[3].lanes[0] == nullptr);
    CHECK(target.pending == -1);

    // Without patterns: they stay.
    SongFile bare;
    bare.song.entries = {{1, 3, 90}};
    ymseq::Project keep = p.x.seq;
    applySongFile(bare, keep, on);
    CHECK_FALSE(on);
    REQUIRE(keep.song.size() == 1);
    CHECK(keep.song.entries[0].bpm == 90);
    CHECK(keep.patterns[9].sameContent(p.x.seq.patterns[9]));
}
