// STYM2149 — plugin core without JUCE (M3): parameter table, conversion to the engine settings, state format, MIDI
// routing and controllers of the prototype, transitions (voice mode, new preparation of the engine).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "plugincore/Digidrum.h"
#include "plugincore/Driver.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "plugincore/Patches.h"
#include "plugincore/State.h"
#include "plugincore/EngineStatus.h"

using namespace stym;
using namespace stym::plugin;

namespace {

int idx(const char* id) {
    const int i = findParam(id);
    REQUIRE(i >= 0);
    return i;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Reference state of the file tests/data/state_v1.bin (frozen v1 format).
PluginState referenceState() {
    PluginState s;
    s.values[size_t(idx("master"))] = 55;
    s.values[size_t(idx("voiceMode"))] = 1;
    s.values[size_t(idx("t01_envPeriod"))] = 4321;
    s.values[size_t(idx("t05_sidRatio"))] = 2.5f;
    s.values[size_t(idx("t16_pan"))] = -40;
    s.values[size_t(idx("t03_on"))] = 1;
    ymengine::TrackParams& t = s.extras.tracks[2];
    t.patch.ornamentOn = true;
    t.patch.ornamentPitch[0] = 0;
    t.patch.ornamentPitch[1] = 12;
    t.patch.ornamentPitch[2] = -12;
    t.patch.ornamentPitch[3] = 7;
    t.patch.ornamentNoise[1] = 1;
    t.patch.ornamentVolume[2] = 9;
    t.patch.digidrumSlot = 5;
    t.arp.mode = ymengine::ArpMode::Table;
    t.arp.table[3] = 21;
    t.arp.length = 4;
    t.arp.land = true;
    s.extras.global.digidrumChannel = {1, 0};
    s.extras.global.scaleMask = 0x0ab5;
    s.extras.global.scaleRoot = 9;
    s.extras.global.slideTable[5] = 0.25f;
    s.extras.kit = ymengine::kKitBanks[7].kit;
    for (int i = 0; i < 33; ++i) s.extras.digidrums[0].push_back(uint8_t((i * 7) % 16));
    s.extras.digidrums[7] = {15, 0, 15, 0, 7};
    return s;
}

// Reference state of the file tests/data/state_v1_m5.bin: the M3 one, plus the parameters and the sequencer of M5.
PluginState referenceStateM5() {
    PluginState s = referenceState();
    s.values[size_t(idx("freeTempo"))] = 133.5f;
    s.values[size_t(idx("swing"))] = 62;
    s.values[size_t(idx("t02_gate"))] = 30;
    s.values[size_t(idx("gate"))] = 70;  // global gate (D-134)
    s.values[size_t(idx("gateAll"))] = 1;
    auto& g = s.extras.global;
    g.seqClock = ymengine::SeqClock::Host;
    g.resolution = 5;
    g.loop = false;
    g.songMode = true;
    g.metronome = true;
    g.quantizeVbl = true;
    g.sigNum = 7;
    g.sigDen = 8;
    ymseq::Project& p = s.extras.seq;
    p.current = 9;
    p.used.set(9);
    p.used.set(31);
    p.viewBank = 1;
    p.bankLast = {3, 9, 16, 31};
    p.tracks[1].octave = 5;
    p.tracks[15].length = 12;
    p.patterns[9].steps = 24;
    ymseq::Lane& a = p.patterns[9].edit(0);
    a.pat[0] = 0;
    a.oct[0] = -1;
    a.tie[0] = 1;
    a.pat[1] = 12;
    a.slide[1] = 1;
    a.vol[3] = 7;
    a.gat[3] = 80;
    a.pnr[3] = 151;
    a.ton[4] = -12;
    a.cnd[200] = 7;
    a.slc[255] = 12;
    a.vpat[0][0] = 4;
    a.voct[0][0] = 6;
    a.vdur[4][0] = 256;
    ymseq::Lane& d = p.patterns[31].edit(15);
    d.pat[7] = 3;
    d.oct[7] = -3;
    d.rtg[7] = 3;
    d.fx[7] = 4;
    d.mix[7] = 3;
    d.slt[7] = 8;
    p.patterns[31].steps = 256;
    p.song.entries = {{9, 2, 0}, {31, 1, 150}, {0, 300, 20}};
    sanitize(s.extras);
    return s;
}

bool sameSequencer(const NonParamState& a, const NonParamState& b) {
    const auto &g = a.global, &h = b.global;
    if (g.seqClock != h.seqClock || g.resolution != h.resolution || g.loop != h.loop || g.songMode != h.songMode ||
        g.metronome != h.metronome || g.metronomeLevel != h.metronomeLevel || g.quantizeVbl != h.quantizeVbl ||
        g.sigNum != h.sigNum || g.sigDen != h.sigDen)
        return false;
    const ymseq::Project &p = a.seq, &q = b.seq;
    if (p.current != q.current || p.used != q.used || p.viewBank != q.viewBank || p.bankLast != q.bankLast ||
        !(p.song == q.song))
        return false;
    for (int t = 0; t < kTracks; ++t)
        if (p.tracks[size_t(t)].octave != q.tracks[size_t(t)].octave ||
            p.tracks[size_t(t)].length != q.tracks[size_t(t)].length)
            return false;
    for (int i = 0; i < ymseq::kPatterns; ++i)
        if (!p.patterns[size_t(i)].sameContent(q.patterns[size_t(i)])) return false;
    return true;
}

bool sameExtras(const NonParamState& a, const NonParamState& b) {
    for (int t = 0; t < kTracks; ++t) {
        const auto& x = a.tracks[size_t(t)];
        const auto& y = b.tracks[size_t(t)];
        if (x.patch.digidrumSlot != y.patch.digidrumSlot || x.patch.ornamentOn != y.patch.ornamentOn ||
            x.patch.ornamentLength != y.patch.ornamentLength || x.patch.ornamentLoop != y.patch.ornamentLoop ||
            x.patch.ornamentRate != y.patch.ornamentRate || x.patch.ornamentPitch != y.patch.ornamentPitch ||
            x.patch.ornamentNoise != y.patch.ornamentNoise || x.patch.ornamentVolume != y.patch.ornamentVolume ||
            x.arp.track != y.arp.track || x.arp.land != y.arp.land || x.arp.mode != y.arp.mode ||
            x.arp.octaves != y.arp.octaves || x.arp.table != y.arp.table || x.arp.length != y.arp.length)
            return false;
    }
    const auto& g = a.global;
    const auto& h = b.global;
    if (g.digidrumChannel != h.digidrumChannel || g.scaleMask != h.scaleMask || g.scaleRoot != h.scaleRoot ||
        g.slideTable != h.slideTable)
        return false;
    for (int k = 0; k < ymengine::kDrumSlots; ++k) {
        const auto& d = a.kit.inst[size_t(k)];
        const auto& e = b.kit.inst[size_t(k)];
        if (d.a != e.a || d.b != e.b || d.dec != e.dec || d.noise != e.noise || d.vol != e.vol) return false;
    }
    return a.digidrums == b.digidrums && a.trackNames == b.trackNames;
}

// Writing of little-endian values into a hand-made state.
void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}
void putChunk(std::vector<uint8_t>& v, const char* tag, const std::vector<uint8_t>& body) {
    v.insert(v.end(), tag, tag + 4);
    put32(v, uint32_t(body.size()));
    v.insert(v.end(), body.begin(), body.end());
}
std::vector<uint8_t> header(uint16_t version) {
    return {'S', 'T', 'Y', 'M', uint8_t(version), uint8_t(version >> 8), 0, 0};
}
std::vector<uint8_t> parm(const std::vector<std::pair<std::string, float>>& entries) {
    std::vector<uint8_t> b;
    put32(b, uint32_t(entries.size()));
    for (const auto& [id, v] : entries) {
        b.push_back(uint8_t(id.size()));
        b.push_back(0);
        b.insert(b.end(), id.begin(), id.end());
        put32(b, std::bit_cast<uint32_t>(v));
    }
    return b;
}

// M5 file as it is rewritten now: PARM chunk completed with the Computer parameter (ST or STE machine, D-153), the
// frequency of the Custom clock (D-237), Arp Sync (D-256), the Master of the stereo outputs (D-284), the delay
// (D-299), the Tuning A4 (D-429) and the MIDI expression (D-432), at their defaults, inserted after "gateAll" (index
// order), parameter count and chunk length updated.
std::vector<uint8_t> withNewParams(std::vector<uint8_t> f) {
    auto get32 = [&](size_t i) {
        return uint32_t(f[i]) | uint32_t(f[i + 1]) << 8 | uint32_t(f[i + 2]) << 16 | uint32_t(f[i + 3]) << 24;
    };
    auto set32 = [&](size_t i, uint32_t x) {
        for (int k = 0; k < 4; ++k) f[i + size_t(k)] = uint8_t(x >> (8 * k));
    };
    const size_t at = 8;  // first chunk: PARM
    REQUIRE(std::memcmp(f.data() + at, "PARM", 4) == 0);
    const std::string anchor = "gateAll";
    const auto it = std::search(f.begin(), f.end(), anchor.begin(), anchor.end());
    REQUIRE(it != f.end());
    const size_t after = size_t(it - f.begin()) + anchor.size() + 4;  // id then f32
    const std::vector<std::pair<std::string, float>> added = {
        {"computer", 0},      {"chipClockHz", 2000000}, {"arpSync", 0},         {"master2", 70},
        {"masterPan", 0},     {"masterLink", 1},        {"delayOn", 0},         {"delaySync", 1},
        {"delayDivision", 4}, {"delayTime", 375},       {"delayFeedback", 35},  {"delayHiCut", 6000},
        {"delayLevel", 100},  {"delayPingPong", 0},       {"a4Hz", 440},          {"bendTarget", 0},
        {"wheelTarget", 1},   {"aftertouch", 1},        {"mpe", 0},             {"mpeBendRange", 48},
        {"mpeSlide", 0}};
    std::vector<uint8_t> add = parm(added);
    add.erase(add.begin(), add.begin() + 4);  // without the count
    f.insert(f.begin() + std::ptrdiff_t(after), add.begin(), add.end());
    set32(at + 4, get32(at + 4) + uint32_t(add.size()));
    set32(at + 8, get32(at + 8) + uint32_t(added.size()));
    // Sixth DAW test (D-254): Vibrato On of each track, after its Gate; then its Send (D-299) and the right side of a
    // stereo DMA sample (D-459).
    for (int t = 1; t <= kTracks; ++t) {
        const std::string id = std::string(t < 10 ? "t0" : "t") + std::to_string(t) + "_gate";
        std::vector<uint8_t> key = {uint8_t(id.size()), 0};
        key.insert(key.end(), id.begin(), id.end());
        const auto g = std::search(f.begin(), f.end(), key.begin(), key.end());
        REQUIRE(g != f.end());
        const std::string tr = id.substr(0, 4);
        const std::vector<std::pair<std::string, float>> fields = {{tr + "vibOn", 1}, {tr + "send", 0},
                                                                   {tr + "levelR", 75}, {tr + "muteR", 0},
                                                                   {tr + "soloR", 0},  {tr + "link", 1}};
        std::vector<uint8_t> on = parm(fields);
        on.erase(on.begin(), on.begin() + 4);
        f.insert(g + std::ptrdiff_t(key.size() + 4), on.begin(), on.end());
        set32(at + 4, get32(at + 4) + uint32_t(on.size()));
        set32(at + 8, get32(at + 8) + uint32_t(fields.size()));
    }
    // Seventh DAW test (D-288): the level of the metronome (100 %) at the end of the SEQS chunk.
    const std::string seqs = "SEQS";
    const auto sq = std::search(f.begin(), f.end(), seqs.begin(), seqs.end());
    REQUIRE(sq != f.end());
    const size_t len = size_t(sq - f.begin()) + 4;
    f.insert(f.begin() + std::ptrdiff_t(len + 4 + get32(len)), uint8_t(100));
    set32(len, get32(len) + 1);
    return f;
}

// Driver ready to play: 48 kHz, track 1 active.
struct Rig {
    Driver d;
    ParamValues v = defaultValues();
    Snapshot snap;
    std::vector<float> l, r;
    explicit Rig(uint16_t trackOutputs = 0) { d.prepare({48000, trackOutputs}, v, snap); }
    // Renders n samples with the given events (positions in this block).
    void run(uint32_t n, std::vector<MidiEvent> midi = {}, bool offline = false, ParamListener* listener = nullptr) {
        l.assign(n, 0);
        r.assign(n, 0);
        ymengine::Outputs o;
        o.left = l.data();
        o.right = r.data();
        d.process(v, midi, o, n, offline, listener);
    }
    double energy() const {
        double e = 0;
        for (float x : l) e += double(x) * double(x);
        return e;
    }
};

MidiEvent msg(uint32_t at, uint8_t a, uint8_t b, uint8_t c) { return MidiEvent{at, 3, {a, b, c}}; }

struct Recorder : ParamListener {
    std::vector<std::pair<int, float>> changes;
    void parameterChangedByMidi(int index, float v) noexcept override { changes.emplace_back(index, v); }
};

}  // namespace

TEST_CASE("Parameters: complete table, stable and unique ids", "[plugin]") {
    const auto& specs = paramSpecs();
    REQUIRE(kParamCount == 42 + 16 * 32);
    std::set<std::string> ids;
    for (int i = 0; i < kParamCount; ++i) {
        const ParamSpec& s = specs[size_t(i)];
        INFO(s.id);
        CHECK(ids.insert(s.id).second);
        CHECK(findParam(s.id) == i);
        CHECK(s.min < s.max);
        CHECK(s.def >= s.min);
        CHECK(s.def <= s.max);
        CHECK(clampToSpec(s, s.def) == s.def);
        // Version of appearance: 2 for the sequencer parameters (M5), 3 for the ST or STE machine (M6, D-153), 4 for
        // the frequency of the Custom clock (D-237), the Vibrato On of the tracks (D-254) and Arp Sync (D-256), 5 for
        // the Master of the stereo outputs (D-284), 6 for the delay and the Send of the tracks (D-299), 7 for the
        // Tuning A4 (D-429), 8 for the MIDI expression (D-432), 9 for the right side of a stereo DMA sample (D-459).
        const bool delay = s.id.starts_with("delay") || s.id.ends_with("_send");
        const bool master = s.id == "master2" || s.id == "masterPan" || s.id == "masterLink";
        const bool m5 = s.id == "freeTempo" || s.id == "swing" || s.id == "gate" || s.id == "gateAll" ||
                        s.id.ends_with("_gate");
        const bool m6 = s.id == "computer";
        const bool daw = s.id == "chipClockHz" || s.id.ends_with("_vibOn") || s.id == "arpSync";
        const bool expr =
            s.id == "bendTarget" || s.id == "wheelTarget" || s.id == "aftertouch" || s.id.starts_with("mpe");
        const bool right = s.id.ends_with("_levelR") || s.id.ends_with("_muteR") || s.id.ends_with("_soloR") ||
                           s.id.ends_with("_link");
        const int version =
            right ? 9 : expr ? 8 : s.id == "a4Hz" ? 7 : delay ? 6 : master ? 5 : daw ? 4 : m6 ? 3 : m5 ? 2 : 1;
        CHECK(s.version == version);
        if (s.kind == ParamKind::Choice) CHECK(s.choices.size() == size_t(s.max) + 1);
    }
    CHECK(findParam("inconnu") == -1);
    CHECK(specs[size_t(idx("t01_level"))].group == "Track 1");
    CHECK(specs[size_t(idx("t16_arpRate"))].name == "T16 Arp Rate");
    CHECK(specs[size_t(idx("master"))].group == "Global");
    // Defaults of the prototype (mkCh, GLOB).
    const ParamValues v = defaultValues();
    CHECK(v[size_t(idx("master"))] == 70.0f);
    CHECK(v[size_t(idx("t01_on"))] == 1.0f);
    CHECK(v[size_t(idx("t02_on"))] == 0.0f);
    CHECK(v[size_t(idx("t07_envPeriod"))] == 12800.0f);
    CHECK(v[size_t(idx("t07_noisePeriod"))] == 8.0f);
    CHECK(v[size_t(idx("t07_volume"))] == 13.0f);
    CHECK(v[size_t(idx("t07_level"))] == 75.0f);
    CHECK(v[size_t(idx("arpDivision"))] == 3.0f);
    CHECK(v[size_t(idx("freeTempo"))] == 120.0f);  // GLOB.bpm, swing, CH[].gate
    CHECK(v[size_t(idx("swing"))] == 50.0f);
    CHECK(v[size_t(idx("t09_gate"))] == 50.0f);
    CHECK(specs[size_t(idx("t09_gate"))].group == "Track 9");
    // Global gate (D-134), under the id reserved in M3: 50 %, off.
    CHECK(v[size_t(idx("gate"))] == 50.0f);
    CHECK(v[size_t(idx("gateAll"))] == 0.0f);
    CHECK(specs[size_t(idx("gate"))].group == "Global");
    CHECK(specs[size_t(idx("gate"))].automatable);
    CHECK(specs[size_t(idx("gateAll"))].kind == ParamKind::Bool);
    CHECK(specs[size_t(idx("freeTempo"))].automatable);
    CHECK_FALSE(specs[size_t(idx("midiRouting"))].automatable);
    CHECK(specs[size_t(idx("chipClock"))].automatable);
}

TEST_CASE("Parameters: bounds, step and normalised position", "[plugin]") {
    const ParamSpec& ep = paramSpec(idx("t01_envPeriod"));
    CHECK(ep.scale == ParamScale::Log);
    CHECK(clampToSpec(ep, 0) == 1.0f);
    CHECK(clampToSpec(ep, 1e9f) == 65535.0f);
    CHECK(clampToSpec(ep, 1234.4f) == 1234.0f);
    CHECK(clampToSpec(ep, std::nanf("")) == 12800.0f);
    CHECK(fromNormalized(ep, 0) == 1.0f);
    CHECK(std::abs(fromNormalized(ep, 1) - 65535) < 0.05f);
    CHECK(std::abs(fromNormalized(ep, toNormalized(ep, 400)) - 400) < 0.01f);
    const ParamSpec& ratio = paramSpec(idx("t01_sidRatio"));
    CHECK(clampToSpec(ratio, 1.234f) == 0.25f + std::round((1.234f - 0.25f) / 0.01f) * 0.01f);
    const ParamSpec& bass = paramSpec(idx("steBass"));
    CHECK(clampToSpec(bass, 3) == 4.0f);  // steps of 2 dB
    CHECK(clampToSpec(bass, -13) == -12.0f);
    const ParamSpec& on = paramSpec(idx("t04_on"));
    CHECK(clampToSpec(on, 0.6f) == 1.0f);
    CHECK(toNormalized(paramSpec(idx("t01_pan")), 0.0f) == 0.5f);
}

TEST_CASE("Mapping: parameter defaults = defaults of the engine and of the prototype", "[plugin]") {
    const ParamValues v = defaultValues();
    const ymengine::TrackParams base{};
    for (int t = 0; t < kTracks; ++t) {
        const ymengine::TrackParams p = toTrack(v, t, base);
        INFO(t);
        CHECK(p.patch.toneOn == base.patch.toneOn);
        CHECK(p.patch.noiseOn == base.patch.noiseOn);
        CHECK(p.patch.noisePeriod == base.patch.noisePeriod);
        CHECK(p.patch.volume == base.patch.volume);
        CHECK(p.patch.envPeriod == base.patch.envPeriod);
        CHECK(p.patch.envShape == base.patch.envShape);
        CHECK(p.patch.generator == base.patch.generator);
        CHECK(p.patch.sidRatio == base.patch.sidRatio);
        CHECK(p.patch.vibratoHz == base.patch.vibratoHz);
        CHECK(p.patch.vibratoOn == base.patch.vibratoOn);
        CHECK(p.patch.sustainPct == base.patch.sustainPct);
        CHECK(p.arp.rateHz == base.arp.rateHz);
        CHECK(p.mix.level == base.mix.level);
        CHECK(p.mix.enabled == (t == 0));
        CHECK(p.mix.link == base.mix.link);
        CHECK(p.mix.levelR == base.mix.levelR);
        CHECK(p.mix.muteR == base.mix.muteR);
        CHECK(p.mix.soloR == base.mix.soloR);
    }
    const ymengine::GlobalParams g = toGlobal(v, {});
    const ymengine::GlobalParams d{};
    CHECK(g.mode == d.mode);
    CHECK(g.master == d.master);
    CHECK(g.vblHz == d.vblHz);
    CHECK(g.focusTrack == 0);
    CHECK(g.arpDivision == d.arpDivision);
    CHECK(g.timer == d.timer);
    CHECK(g.velocity == d.velocity);
    CHECK(g.tempo == d.tempo);
    CHECK(g.swing == d.swing);
    CHECK(toTrack(v, 3, base).seq.gate == base.seq.gate);
}

TEST_CASE("Mapping: the right side of a stereo DMA sample, a setting of the Mixer (Indus, 26/10/2026, D-459)",
          "[plugin]") {
    ParamValues v = defaultValues();
    v[size_t(paramIndex(2, Track::Level))] = 80;
    v[size_t(paramIndex(2, Track::LevelR))] = 40;
    v[size_t(paramIndex(2, Track::MuteR))] = 1;
    v[size_t(paramIndex(2, Track::SoloR))] = 1;
    ymengine::TrackMix m = toMix(v, 2);
    CHECK(m.link);  // Link on by default: the engine plays the right side at the Level
    CHECK(m.level == 0.8);
    CHECK(m.levelR == 0.4);
    CHECK(m.muteR);
    CHECK(m.soloR);
    CHECK_FALSE(m.mute);
    CHECK_FALSE(m.solo);
    v[size_t(paramIndex(2, Track::Link))] = 0;
    CHECK_FALSE(toMix(v, 2).link);
    CHECK(toTrack(v, 2, {}).mix.levelR == 0.4);
    // Neither in patches nor copied with a track: settings of the Mixer, like the pan.
    for (const Track f : {Track::LevelR, Track::MuteR, Track::SoloR, Track::Link}) {
        CHECK(mixerOnly(f));
        CHECK_FALSE(inPatch(f));
    }
    CHECK_FALSE(mixerOnly(Track::Level));
    CHECK(inPatch(Track::Level));
}

TEST_CASE("Mapping: global gate takes priority when Gate All is on (D-134)", "[plugin][seq]") {
    ParamValues v = defaultValues();
    v[size_t(idx("t03_gate"))] = 20;
    v[size_t(idx("t16_gate"))] = 95;
    v[size_t(idx("gate"))] = 80;
    const ymengine::TrackParams base{};
    // Off: each track keeps its own, the global gate does not play.
    CHECK(trackGate(v, 2) == 20);
    CHECK(trackGate(v, 15) == 95);
    CHECK(trackGate(v, 0) == 50);
    CHECK(toTrack(v, 2, base).seq.gate == 0.2);
    // On: all tracks play the global gate; their settings stay intact.
    v[size_t(idx("gateAll"))] = 1;
    for (int t = 0; t < kTracks; ++t) {
        INFO(t);
        CHECK(trackGate(v, t) == 80);
        CHECK(toTrack(v, t, base).seq.gate == 0.8);
    }
    CHECK(v[size_t(idx("t03_gate"))] == 20.0f);
    v[size_t(idx("gateAll"))] = 0;
    CHECK(toTrack(v, 2, base).seq.gate == 0.2);
}

TEST_CASE("Mapping: Clock selector, clock of the arpeggio and of the sequencer (D-188)", "[plugin][seq]") {
    using ymengine::SeqClock;
    const ParamSpec& spec = paramSpecs()[size_t(idx("arpClock"))];
    CHECK(spec.name == "Clock");  // id unchanged: projects and automations kept
    ParamValues v = defaultValues();
    ymengine::GlobalParams base;
    // Internal: the state's clock stays (Host sync in a host, Free run in Standalone, D-110).
    for (const SeqClock c : {SeqClock::Host, SeqClock::Internal, SeqClock::Midi}) {
        base.seqClock = c;
        CHECK(seqClockOf(v, c) == c);
        CHECK(toGlobal(v, base).seqClock == c);
        CHECK(toGlobal(v, base).arpClock == ymengine::ArpClock::Hz);
    }
    // MIDI ext: the arpeggio and the sequencer follow the MIDI clock, like setClkMode of the prototype.
    v[size_t(idx("arpClock"))] = 1;
    for (const SeqClock c : {SeqClock::Host, SeqClock::Internal, SeqClock::Midi}) {
        base.seqClock = c;
        CHECK(seqClockOf(v, c) == SeqClock::Midi);
        CHECK(toGlobal(v, base).seqClock == SeqClock::Midi);
        CHECK(toGlobal(v, base).arpClock == ymengine::ArpClock::MidiClock);
    }
    // Arp Sync (D-256): the tempo with the internal clock; MIDI ext keeps the MIDI clock.
    v[size_t(idx("arpSync"))] = 1;
    CHECK(toGlobal(v, base).arpClock == ymengine::ArpClock::MidiClock);
    v[size_t(idx("arpClock"))] = 0;
    for (const SeqClock c : {SeqClock::Host, SeqClock::Internal}) {
        base.seqClock = c;
        CHECK(toGlobal(v, base).seqClock == c);
        CHECK(toGlobal(v, base).arpClock == ymengine::ArpClock::Tempo);
    }
    CHECK(paramSpecs()[size_t(idx("arpSync"))].name == "Arp Sync");
    CHECK(paramSpecs()[size_t(idx("arpSync"))].automatable);
    CHECK(defaultValues()[size_t(idx("arpSync"))] == 0.0f);  // Rate
}

TEST_CASE("Mapping: roles imposed in 3 channels (fxAllowed), setting kept", "[plugin]") {
    using ymengine::Generator;
    ParamValues v = defaultValues();
    v[size_t(idx("t01_generator"))] = 2;  // Digidrum on a hardware channel
    v[size_t(idx("t04_generator"))] = 1;  // SID on the Digidrum track of chip 1
    v[size_t(idx("t08_generator"))] = 0;
    v[size_t(idx("t12_generator"))] = 2;
    CHECK(effectiveGenerator(v, 0) == Generator::Digidrum);  // 16 channels: everything is allowed
    CHECK(effectiveGenerator(v, 3) == Generator::Sid);
    v[size_t(idx("voiceMode"))] = 1;  // Authentic 3ch
    CHECK(effectiveGenerator(v, 0) == Generator::Chip);
    CHECK(effectiveGenerator(v, 3) == Generator::Digidrum);
    CHECK(effectiveGenerator(v, 7) == Generator::Chip);   // outside the chip: not a Digidrum track
    CHECK(effectiveGenerator(v, 11) == Generator::Chip);  // fxAllowed: no sample outside the Digidrum track
    v[size_t(idx("voiceMode"))] = 2;  // Dual chip
    CHECK(effectiveGenerator(v, 7) == Generator::Digidrum);
    CHECK(effectiveGenerator(v, 11) == Generator::Chip);
    CHECK(v[size_t(idx("t01_generator"))] == 2.0f);              // the parameter keeps the user's choice
}

TEST_CASE("Mapping: STE machine, DMA tracks and DMA type (D-153)", "[plugin]") {
    using ymengine::Generator;
    using ymengine::VoiceMode;
    ParamValues v = defaultValues();
    CHECK(intValue(v, Global::Computer) == 0);  // ST by default
    CHECK(!steMachine(v));
    CHECK(!toGlobal(v, {}).ste);
    v[size_t(idx("t02_generator"))] = 5;  // DMA
    v[size_t(idx("t06_generator"))] = 1;  // SID
    CHECK(effectiveGenerator(v, 1) == Generator::Chip);  // 16 channels, ST: no DMA
    CHECK(trackCount(v) == 16);
    v[size_t(idx("computer"))] = 1;
    CHECK(toGlobal(v, {}).ste);
    CHECK(effectiveGenerator(v, 1) == Generator::Dma);  // 16 channels, STE: any track
    CHECK(trackCount(v) == 16);
    // Authentic 3ch, STE: tracks 5 to 12 as DMA, whatever their parameter; DMA elsewhere falls back to Chip Voice.
    v[size_t(idx("voiceMode"))] = 1;
    CHECK(trackCount(v) == 12);
    CHECK(effectiveGenerator(v, 1) == Generator::Chip);
    CHECK(effectiveGenerator(v, 3) == Generator::Digidrum);
    CHECK(effectiveGenerator(v, 5) == Generator::Dma);
    CHECK(effectiveGenerator(v, 11) == Generator::Dma);
    CHECK(effectiveGenerator(v, 12) == Generator::Chip);  // outside the played tracks
    CHECK(dmaTrack(VoiceMode::Authentic3, true, 4));
    CHECK(!dmaTrack(VoiceMode::Authentic3, true, 12));
    CHECK(!dmaTrack(VoiceMode::Authentic3, false, 4));
    CHECK(!dmaTrack(VoiceMode::Studio16, true, 4));
    CHECK(!generatorAllowed(VoiceMode::Authentic3, true, 4, Generator::Chip));
    CHECK(!generatorAllowed(VoiceMode::Authentic3, true, 0, Generator::Dma));
    // Dual chip, STE: tracks 9 to 16.
    v[size_t(idx("voiceMode"))] = 2;
    CHECK(trackCount(v) == 16);
    CHECK(effectiveGenerator(v, 5) == Generator::Sid);
    CHECK(effectiveGenerator(v, 8) == Generator::Dma);
    CHECK(effectiveGenerator(v, 15) == Generator::Dma);
    CHECK(dmaTrack(VoiceMode::Dual3, true, 15));
    // ST machine in 3 channels: only four tracks per chip.
    v[size_t(idx("computer"))] = 0;
    CHECK(trackCount(v) == 8);
    CHECK(effectiveGenerator(v, 8) == Generator::Chip);
    CHECK(v[size_t(idx("t02_generator"))] == 5.0f);  // the parameter keeps the user's choice
}

TEST_CASE("Mapping: CC 74 like the Env freq slider of the prototype", "[plugin]") {
    CHECK(envPeriodFromCc(0) == 65535);  // EP 65535 × 32, capped
    CHECK(envPeriodFromCc(127) == 32);   // EP 1
    for (int cc = 0; cc <= 127; ++cc) {
        const double ef = std::floor(cc / 127.0 * 4000 + 0.5);
        const double ep = std::clamp(std::floor(std::pow(65535.0, 1 - ef / 4000) + 0.5), 1.0, 65535.0);
        INFO(cc);
        CHECK(std::abs(double(envPeriodFromCc(cc)) - std::min(65535.0, ep * 32)) <= 32);
    }
}

TEST_CASE("Mapping: engine configuration, sinc fallback beyond clk/8", "[plugin]") {
    ParamValues v = defaultValues();
    v[size_t(idx("chipClock"))] = 3;    // Amstrad CPC, 1 MHz: 125 kHz internal
    v[size_t(idx("resampling"))] = 1;   // AtariAudio averaging
    v[size_t(idx("outputChain"))] = 2;  // STE
    v[size_t(idx("steTreble"))] = -6;
    ymengine::EngineConfig c = engineConfig(configKey(v), v, 96000, 0x8001);
    CHECK(c.render.mode == ymcore::OutputMode::RawBox);
    CHECK(c.render.chip.clock.hz() == 1e6);
    CHECK(c.render.board.chain == ymcore::BoardChain::STE);
    CHECK(c.render.board.trebleDb == -6);
    CHECK(c.trackOutputs == 0x8001);
    c = engineConfig(configKey(v), v, 192000, 0);
    CHECK(c.render.mode == ymcore::OutputMode::Raw);
    v[size_t(idx("chipClock"))] = 2;  // ZX Spectrum, 1.7734 MHz: clk/8 ≈ 221.7 kHz
    c = engineConfig(configKey(v), v, 192000, 0);
    CHECK(c.render.chip.clock.hz() == 1773400.0);
    CHECK(c.render.mode == ymcore::OutputMode::RawBox);
    v[size_t(idx("chipClock"))] = 1;  // Atari ST (PAL): clk/8 ≈ 250.7 kHz
    c = engineConfig(configKey(v), v, 192000, 0);
    CHECK(c.render.chip.clock.hz() == 8021247.0 / 4.0);
    CHECK(c.render.mode == ymcore::OutputMode::RawBox);
    c = engineConfig(configKey(v), v, 384000, 0);
    CHECK(c.render.mode == ymcore::OutputMode::Raw);
    v[size_t(idx("chipClock"))] = 7;  // value outside the list read from a state: clamped to the last clock
    CHECK(configKey(v).clock == 3);
}

TEST_CASE("Mapping: Custom clock, its frequency in 16 ch, 2 MHz in the other modes (D-237)", "[plugin]") {
    ParamValues v = defaultValues();
    const ParamSpec& s = paramSpecs()[size_t(idx("chipClockHz"))];
    CHECK(paramSpecs()[size_t(idx("chipClock"))].choices[0] == "Custom");
    CHECK(s.min == 100000.0f);
    CHECK(s.max == 8000000.0f);
    CHECK_FALSE(s.automatable);
    // Default: the 2 MHz of the entry the Custom clock replaces, in 16 ch.
    CHECK(v[size_t(idx("chipClock"))] == 0.0f);
    CHECK(v[size_t(idx("chipClockHz"))] == 2000000.0f);
    CHECK(configKey(v).hz == 2000000);
    CHECK(chipClock(v).hz() == 2e6);
    v[size_t(idx("chipClockHz"))] = 2457600;
    CHECK(configKey(v).hz == 2457600);
    CHECK(engineConfig(configKey(v), v, 48000, 0).render.chip.clock.hz() == 2457600.0);
    CHECK(chipClock(v).hz() == 2457600.0);
    for (const int mode : {1, 2}) {  // Authentic 3ch, Dual chip 2x3ch: 2 MHz
        CAPTURE(mode);
        v[size_t(idx("voiceMode"))] = float(mode);
        CHECK(configKey(v).hz == 2000000);
        CHECK(chipClock(v).hz() == 2e6);
        CHECK(customClockHz(v) == 2457600);
    }
    v[size_t(idx("voiceMode"))] = 0;
    v[size_t(idx("chipClockHz"))] = 50;  // value outside the range read from a state: clamped
    CHECK(configKey(v).hz == 100000);
    // Above clk/8 (12.5 kHz at 100 kHz): sinc (D-058).
    v[size_t(idx("resampling"))] = 1;
    CHECK(engineConfig(configKey(v), v, 48000, 0).render.mode == ymcore::OutputMode::Raw);
    // Another clock: the frequency of the Custom clock is not part of the key (no new engine when it changes).
    v[size_t(idx("chipClock"))] = 1;
    CHECK(configKey(v).hz == 0);
    CHECK(chipClock(v).hz() == 8021247.0 / 4.0);
}

TEST_CASE("Mapping: Tuning A4, 350 to 480 Hz, 440 by default (D-429, D-434)", "[plugin]") {
    ParamValues v = defaultValues();
    const ParamSpec& s = paramSpecs()[size_t(idx("a4Hz"))];
    CHECK(s.min == 350.0f);
    CHECK(s.max == 480.0f);
    CHECK(s.def == 440.0f);
    CHECK(s.unit == "Hz");
    CHECK_FALSE(s.automatable);
    CHECK(toGlobal(v, {}).a4Hz == 440.0);
    v[size_t(idx("a4Hz"))] = 432;
    CHECK(toGlobal(v, {}).a4Hz == 432.0);
    // Hundredths of a hertz; a value outside the range read from a state is clamped.
    CHECK(clampToSpec(s, 442.123f) == 442.12f);
    CHECK(clampToSpec(s, 392.0f) == 392.0f);  // below 400 Hz (D-434)
    CHECK(clampToSpec(s, 300.0f) == 350.0f);
    CHECK(clampToSpec(s, 1000.0f) == 480.0f);
}

TEST_CASE("Mapping: targets of the MIDI expression and their depths (D-432)", "[plugin]") {
    ParamValues v = defaultValues();
    for (const char* id : {"bendTarget", "wheelTarget", "aftertouch", "mpe", "mpeBendRange", "mpeSlide"}) {
        INFO(id);
        CHECK_FALSE(paramSpecs()[size_t(idx(id))].automatable);  // MIDI settings (D-057)
    }
    ymengine::GlobalParams g = toGlobal(v, {});
    CHECK(g.bendTarget == ymengine::BendTarget::Pitch);
    CHECK(g.bendRange == 2.0);
    CHECK(g.wheelTarget == ymengine::ExpressTarget::Vibrato);
    CHECK(g.aftertouch == ymengine::ExpressTarget::Vibrato);
    CHECK(g.mpeSlide == ymengine::SlideTarget::Volume);
    CHECK(g.expressCents == 50.0);
    CHECK(paramSpecs()[size_t(idx("mpeBendRange"))].def == 48.0f);
    v[size_t(idx("bendTarget"))] = 2;
    v[size_t(idx("bendRange"))] = 12;
    v[size_t(idx("wheelTarget"))] = 4;
    v[size_t(idx("aftertouch"))] = 2;
    v[size_t(idx("mpeSlide"))] = 1;
    v[size_t(idx("wheelDepth"))] = 120;
    g = toGlobal(v, {});
    CHECK(g.bendTarget == ymengine::BendTarget::Envelope);
    CHECK(g.bendRange == 12.0);
    CHECK(g.wheelTarget == ymengine::ExpressTarget::Lfo);
    CHECK(g.aftertouch == ymengine::ExpressTarget::Volume);
    CHECK(g.mpeSlide == ymengine::SlideTarget::Envelope);
    CHECK(g.expressCents == 120.0);
}

TEST_CASE("State: round trip, reference v1 file", "[plugin][state]") {
    const PluginState ref = referenceState();
    const std::vector<uint8_t> bytes = encodeState(ref);
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.version == 1);
    CHECK(back.unknownParams == 0);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.values == ref.values);
    CHECK(sameExtras(back.state.extras, ref.extras));

    // M3 file: reads back identically (M5 parameters and chunks missing: defaults). It is no longer rewritten byte
    // for byte: the M5 state has five more parameters and three more chunks (D-065: new chunks in v1).
    const std::vector<uint8_t> golden = readFile(std::string(STYM_TEST_DATA) + "/state_v1.bin");
    REQUIRE(!golden.empty());
    const DecodeResult g = decodeState(golden);
    REQUIRE(g.ok);
    CHECK_FALSE(g.hasSequencer);
    CHECK(g.state.values == ref.values);
    CHECK(sameExtras(g.state.extras, ref.extras));
    CHECK(sameSequencer(g.state.extras, NonParamState{}));

    // Frozen v1 format of M5: the reference file reads back identically and is rewritten byte for byte.
    const PluginState m5 = referenceStateM5();
    if (const char* out = std::getenv("STYM_STATE_GOLDEN_OUT")) {  // deliberate regeneration of the file
        const auto bytes5 = encodeState(m5);
        std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(bytes5.data()), std::streamsize(bytes5.size()));
    }
    const std::vector<uint8_t> golden5 = readFile(std::string(STYM_TEST_DATA) + "/state_v1_m5.bin");
    REQUIRE(!golden5.empty());
    const DecodeResult g5 = decodeState(golden5);
    REQUIRE(g5.ok);
    CHECK(g5.hasSequencer);
    CHECK(g5.unknownChunks == 0);
    CHECK(g5.state.values == m5.values);
    CHECK(sameExtras(g5.state.extras, m5.extras));
    CHECK(sameSequencer(g5.state.extras, m5.extras));
    // M6 (D-153) and the fourth DAW test (D-237): two more parameters (Computer, Custom clock), written at their
    // defaults just after "gateAll"; the sixth (D-254): Vibrato On after the Gate of each track; the rest of the file
    // is rewritten byte for byte.
    CHECK(encodeState(g5.state) == withNewParams(golden5));
}

TEST_CASE("State: level of the metronome kept (D-288)", "[plugin][state][metronome]") {
    PluginState s = referenceStateM5();
    for (const uint8_t level : {uint8_t(0), uint8_t(35), uint8_t(100)}) {
        s.extras.global.metronomeLevel = level;
        const DecodeResult back = decodeState(encodeState(s));
        REQUIRE(back.ok);
        CHECK(back.state.extras.global.metronomeLevel == level);
        CHECK(sameSequencer(back.state.extras, s.extras));
    }
}

TEST_CASE("State: sequencer, cells clamped, unknown rows ignored", "[plugin][state]") {
    const PluginState m5 = referenceStateM5();
    const DecodeResult back = decodeState(encodeState(m5));
    REQUIRE(back.ok);
    const ymseq::Project& p = back.state.extras.seq;
    CHECK(p.patterns[9].lane(0).vdur[4][0] == 256);
    CHECK(p.patterns[9].lane(0).oct[0] == -1);
    CHECK(p.patterns[31].lane(15).oct[7] == -3);
    CHECK(p.song.entries[2].rep == 300);
    CHECK(back.state.extras.tracks[1].seq.octave == 5);  // copied for the engine
    CHECK(back.state.extras.tracks[15].seq.length == 12);
    // Patterns and Song as the engine plays them: shared tracks, without copying.
    ymengine::SeqData data;
    fillSequence(p, data);
    CHECK(data.patterns[9].steps == 24);
    CHECK(data.patterns[9].lanes[0].get() == p.patterns[9].lanes[0].get());
    CHECK(data.song.size() == 3);
    CHECK(data.song[1].bpm == 150);
    CHECK(data.song[2].repeats == 300);

    // Hand-made chunk: out-of-bounds values, unknown row, pattern outside the bank, out-of-bounds settings.
    std::vector<uint8_t> s = header(1);
    putChunk(s, "SEQS", {9, 40, 0, 4, 3, 77, 9, 0, 0, 0, 0, 0, 0, 0, 0, 1, 200, 0xff, 0xff});
    std::vector<uint8_t> lane = {
        3,                     // three rows
        0, 1, 0, 5, 50, 0,     // Pat: step 5 = 50 → 12
        99, 1, 0, 2, 1, 0,     // unknown id: ignored
        4, 1, 0, 6, 0xff, 0x7f,  // Vol: step 6 = 32767 → 15 (empty: nothing is written)
    };
    std::vector<uint8_t> pat = {16, 0, 1, 2};  // 16 steps, one track: track 3
    pat.insert(pat.end(), lane.begin(), lane.end());
    std::vector<uint8_t> patn = {2, 4};
    put32(patn, uint32_t(pat.size()));
    patn.insert(patn.end(), pat.begin(), pat.end());
    patn.push_back(40);  // pattern 40: does not exist
    put32(patn, 0);
    putChunk(s, "PATN", patn);
    putChunk(s, "SONG", {1, 0, 50, 0, 0, 0, 0});
    const DecodeResult r = decodeState(s);
    REQUIRE(r.ok);
    const auto& x = r.state.extras;
    CHECK(x.global.seqClock == ymengine::SeqClock::Host);
    CHECK(x.global.resolution == 8);  // the last resolution, 1/32T (D-433)
    CHECK(x.global.sigNum == 4);  // 4/3 is not a time signature of the prototype: 4/4
    CHECK(x.global.sigDen == 4);
    CHECK(x.global.metronomeLevel == 100);  // no level (before D-288): full level
    CHECK(x.seq.current == 31);
    CHECK(x.seq.viewBank == 3);
    CHECK(x.seq.used.test(4));
    CHECK(x.seq.used.test(31));  // the current pattern still exists
    CHECK(x.seq.patterns[4].lane(2).pat[5] == 12);
    CHECK(x.seq.patterns[4].lane(2).vol[6] == 15);
    CHECK(x.seq.tracks[0].octave == 9);
    CHECK(x.seq.tracks[0].length == 64);
    REQUIRE(x.seq.song.entries.size() == 1);
    CHECK(x.seq.song.entries[0].pat == 31);
    CHECK(x.seq.song.entries[0].rep == 1);
}

TEST_CASE("State: INS row of M6 (id 31), clamped to the 32 instruments", "[plugin][state]") {
    PluginState st;
    ymseq::Lane& l = st.extras.seq.patterns[0].edit(1);
    l.pat[0] = 2;
    l.ins[0] = 32;
    l.ins[5] = 3;
    l.vpat[4][7] = 9;  // last row of the chord voices (id 30): distinct from INS
    const DecodeResult back = decodeState(encodeState(st));
    REQUIRE(back.ok);
    const ymseq::Lane& b = back.state.extras.seq.patterns[0].lane(1);
    CHECK(b.ins[0] == 32);
    CHECK(b.ins[5] == 3);
    CHECK(b.vpat[4][7] == 9);
    CHECK(b == l);

    std::vector<uint8_t> s = header(1);
    std::vector<uint8_t> lane = {
        1,                     // one row
        31, 2, 0, 4, 99, 0, 6, 0xff, 0xff,  // INS: step 4 = 99 → 32, step 6 = -1 → 0 (empty)
    };
    std::vector<uint8_t> pat = {16, 0, 1, 0};
    pat.insert(pat.end(), lane.begin(), lane.end());
    std::vector<uint8_t> patn = {1, 0};
    put32(patn, uint32_t(pat.size()));
    patn.insert(patn.end(), pat.begin(), pat.end());
    putChunk(s, "PATN", patn);
    const DecodeResult r = decodeState(s);
    REQUIRE(r.ok);
    CHECK(r.state.extras.seq.patterns[0].lane(0).ins[4] == 32);
    CHECK(r.state.extras.seq.patterns[0].lane(0).ins[6] == 0);
}

TEST_CASE("State: CMD rows (ids 32 to 52), unknown command cleared", "[plugin][state][commands]") {
    using ymengine::SeqCmd;
    using ymengine::seqCmd;
    PluginState st;
    ymseq::Lane& l = st.extras.seq.patterns[0].edit(1);
    l.ins[0] = 32;  // INS (id 31): distinct from CMD
    l.setCmd(0, seqCmd(SeqCmd::Offset, 0x47));
    l.setCmd(3, seqCmd(SeqCmd::Speed, 0x03));
    l.setCmd(9, seqCmd(SeqCmd::Retrig, 0x00));
    l.setCmd(255, seqCmd(SeqCmd::Sync, 0xff));
    l.setCmd(0, seqCmd(SeqCmd::Vibrato, 0));  // several commands on a step (tenth DAW test, item 122, D-330)
    // D-338, items 142 to 144, D-393, D-394: one step each
    for (int k = int(SeqCmd::VolSlide); k <= int(SeqCmd::NoiseCurve); ++k)
        if (k != int(SeqCmd::PitchSlide)) l.setCmd(20 + k, seqCmd(SeqCmd(k), uint8_t(k * 9)));
    l.setCmd(1, seqCmd(SeqCmd::Fine, 100));
    const ymseq::Lane kept = l;
    // A Txx of a track (BPM row of the eleventh DAW test) goes to the BPM row of the pattern (item 150).
    l.setCmd(0, seqCmd(SeqCmd::Tempo, 0x8c));
    st.extras.seq.patterns[0].bpm[9] = 0x60;
    const DecodeResult back = decodeState(encodeState(st));
    REQUIRE(back.ok);
    CHECK(back.state.extras.seq.patterns[0].lane(1) == kept);
    CHECK(back.state.extras.seq.patterns[0].bpm[0] == 0x8c);
    CHECK(back.state.extras.seq.patterns[0].bpm[9] == 0x60);
    CHECK(back.state.extras.seq.patterns[0].bpm[1] == 0);

    std::vector<uint8_t> s = header(1);
    std::vector<uint8_t> lane = {
        3,                                // three rows
        32, 4, 0, 4, 0x47, 0x01,          // CMD (Xyz): step 4 = X47, retired (D-340): dropped
        6, 0x00, 0x16,                    // step 6 = letter 22 (unknown): empty
        7, 0xff, 0xff,                    // step 7 = -1: empty
        4, 0x8c, 0x05,                    // step 4 = T8C: the single CMD row of a state before D-330, to its kind
        37, 1, 0, 4, 0x00, 0x06,          // CMD (Vxx): step 4 = V00
        46, 1, 0, 4, 0x64, 0x0f,          // CMD (Fxx, D-338): step 4 = F64
    };
    std::vector<uint8_t> pat = {16, 0, 1, 0};
    pat.insert(pat.end(), lane.begin(), lane.end());
    std::vector<uint8_t> patn = {1, 0};
    put32(patn, uint32_t(pat.size()));
    patn.insert(patn.end(), pat.begin(), pat.end());
    putChunk(s, "PATN", patn);
    const DecodeResult r = decodeState(s);
    REQUIRE(r.ok);
    const ymseq::Lane& d = r.state.extras.seq.patterns[0].lane(0);
    CHECK(d.cmdAt(SeqCmd::Arp, 4) == 0);
    CHECK(d.cmdAt(SeqCmd::Tempo, 4) == 0);  // to the BPM row of the pattern (item 150)
    CHECK(r.state.extras.seq.patterns[0].bpm[4] == 0x8c);
    CHECK(d.cmdAt(SeqCmd::Vibrato, 4) == seqCmd(SeqCmd::Vibrato, 0));
    CHECK(d.cmdAt(SeqCmd::Fine, 4) == seqCmd(SeqCmd::Fine, 0x64));
    CHECK_FALSE(d.hasCmd(6));
    CHECK_FALSE(d.hasCmd(7));
}

TEST_CASE("State: ARP flags of an older state become Jxx, BPM row of the pattern after its tracks (items 144, 150)",
          "[plugin][state][commands]") {
    using ymengine::SeqCmd;
    using ymengine::seqCmd;
    std::vector<uint8_t> s = header(1);
    const std::vector<uint8_t> lane = {
        2,                     // two rows
        0, 1, 0, 0, 0x00, 0x00,  // PAT: step 0 = note 0
        3, 1, 0, 2, 0x01, 0x00,  // ARP flag at step 2
    };
    std::vector<uint8_t> pat = {16, 0, 1, 0};
    pat.insert(pat.end(), lane.begin(), lane.end());
    pat.insert(pat.end(), {1, 0, 5, 0x78});  // BPM row: 120 at step 5
    std::vector<uint8_t> patn = {1, 0};
    put32(patn, uint32_t(pat.size()));
    patn.insert(patn.end(), pat.begin(), pat.end());
    putChunk(s, "PATN", patn);
    const DecodeResult r = decodeState(s);
    REQUIRE(r.ok);
    const ymseq::Pattern& p = r.state.extras.seq.patterns[0];
    const ymseq::Lane& d = p.lane(0);
    CHECK(d.afl[2] == 0);
    CHECK(d.cmdAt(SeqCmd::Arpeggiator, 0) == seqCmd(SeqCmd::Arpeggiator, 0));
    CHECK(d.cmdAt(SeqCmd::Arpeggiator, 2) == seqCmd(SeqCmd::Arpeggiator, 1));
    CHECK(d.cmdAt(SeqCmd::Arpeggiator, 3) == seqCmd(SeqCmd::Arpeggiator, 0));
    CHECK(p.bpm[5] == 0x78);
    // Written back: the Jxx rows (ids 47 to 49) and the BPM row; a pattern without tempo ends after its tracks.
    const DecodeResult back = decodeState(encodeState(r.state));
    REQUIRE(back.ok);
    CHECK(back.state.extras.seq.patterns[0].sameContent(p));
}

TEST_CASE("State: track names (NAME chunk, written only if a track is named)", "[plugin][state]") {
    PluginState s;
    const auto plain = encodeState(s);
    s.extras.trackNames[2] = "Bass";
    s.extras.trackNames[15] = "  Lead line 2  ";  // edge spaces removed, 8 characters at most
    const auto named = encodeState(s);
    CHECK(named.size() > plain.size());
    const DecodeResult back = decodeState(named);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.trackNames[2] == "Bass");
    CHECK(back.state.extras.trackNames[15] == "Lead lin");
    CHECK(back.state.extras.trackNames[0].empty());
    CHECK(cleanTrackName("\tA\xc3\xa9" "B") == "AB");  // printable ASCII only
    CHECK(cleanTrackName("Hi hats open") == "Hi hats");  // cut after 8 characters, then the space at the end
}

TEST_CASE("State: unknown, missing and out-of-bounds chunks and parameters; versions", "[plugin][state]") {
    std::vector<uint8_t> s = header(1);
    putChunk(s, "ZZZZ", {1, 2, 3});
    putChunk(s, "PARM", parm({{"master", 250.0f}, {"t02_on", 1.0f}, {"futur", 3.0f}, {"t01_envShape", 7.6f},
                              {"t01_pan", std::nanf("")}}));
    putChunk(s, "TRKX", {1, 2, 0, 0, 0, 9, 1});  // one track, 2-byte chunk: slot 9, ornament active
    DecodeResult r = decodeState(s);
    REQUIRE(r.ok);
    CHECK(r.unknownChunks == 1);
    CHECK(r.unknownParams == 1);
    CHECK(r.state.values[size_t(idx("master"))] == 100.0f);    // clamped
    CHECK(r.state.values[size_t(idx("t02_on"))] == 1.0f);
    CHECK(r.state.values[size_t(idx("t01_envShape"))] == 8.0f);  // rounded to the step
    CHECK(r.state.values[size_t(idx("t01_pan"))] == 0.0f);       // non-finite: default
    CHECK(r.state.values[size_t(idx("t03_level"))] == 75.0f);    // missing: default
    CHECK(r.state.extras.tracks[0].patch.digidrumSlot == 7);  // clamped
    CHECK(r.state.extras.tracks[0].patch.ornamentOn);
    CHECK(r.state.extras.tracks[0].patch.ornamentLength == 8);  // missing field: default

    CHECK_FALSE(decodeState(header(2)).ok);   // more recent version: refused
    CHECK_FALSE(decodeState(header(0)).ok);
    CHECK_FALSE(decodeState(std::vector<uint8_t>{'S', 'T', 'Y'}).ok);
    std::vector<uint8_t> cut = encodeState(referenceState());
    cut.resize(cut.size() - 3);
    CHECK_FALSE(decodeState(cut).ok);         // truncated chunk
    CHECK(decodeState(header(1)).ok);         // empty: everything at default
}

TEST_CASE("Driver: MIDI note → sound, then silence; velocity 0 = note off", "[plugin][driver]") {
    Rig g;
    g.run(512);
    CHECK(g.energy() < 1e-9);
    g.run(4800, {msg(10, 0x90, 60, 100)});
    CHECK(g.energy() > 1e-3);
    CHECK(g.d.engine().activeVoices() > 0);
    g.run(4800, {msg(0, 0x90, 60, 0)});
    g.run(9600);
    CHECK(g.d.engine().activeVoices() == 0);
}

TEST_CASE("Driver: a host that prepares at 0 Hz gets a playable engine", "[plugin][driver]") {
    // Some hosts prepare during a probe with a rate of 0: no division by zero, the floor of the offline renders.
    Driver d;
    ParamValues v = defaultValues();
    d.prepare({0, 0}, v, Snapshot{});
    REQUIRE(d.prepared());
    std::vector<float> l(512), r(512);
    ymengine::Outputs o;
    o.left = l.data();
    o.right = r.data();
    const std::vector<MidiEvent> midi = {msg(0, 0x90, 60, 100)};
    d.process(v, midi, o, 512, false, nullptr);
    for (float x : l) CHECK(std::isfinite(x));
}

TEST_CASE("Driver: delay parameters and Send of the track reach the engine, 16 ch only (D-299)",
          "[plugin][driver][delay]") {
    for (const int mode : {0, 1, 2}) {
        CAPTURE(mode);
        // A note of 50 ms, then the echo one second later (Time, 1000 ms) with the delay on and track 1 sent.
        auto take = [mode](bool delay) {
            Rig g;
            g.v[size_t(paramIndex(Global::VoiceMode))] = float(mode);
            g.v[size_t(paramIndex(Global::DelayOn))] = delay ? 1.0f : 0.0f;
            g.v[size_t(paramIndex(Global::DelaySync))] = 0;
            g.v[size_t(paramIndex(Global::DelayTime))] = 1000;
            g.v[size_t(paramIndex(0, Track::Send))] = 100;
            g.v[size_t(paramIndex(0, Track::Release))] = 0;
            g.run(4800);  // a change of mode goes through a fade
            g.run(2400, {msg(0, 0x90, 60, 100)});
            g.run(2400, {msg(0, 0x90, 60, 0)});
            g.run(38400);
            const double before = g.energy();
            g.run(9600);
            return std::pair{before, g.energy()};
        };
        const auto [dryBefore, dryEcho] = take(false);
        const auto [wetBefore, wetEcho] = take(true);
        CHECK(dryBefore < 1e-6);
        CHECK(dryEcho < 1e-9);
        CHECK(wetBefore < 1e-6);
        if (mode == 0) CHECK(wetEcho > 1e-3);
        else CHECK(wetEcho < 1e-9);  // Authentic 3ch, Dual chip
    }
}

// Voice that plays the note in the engine of a driver (-1: none), the n-th of them by attack order.
static int voiceOf(const Driver& d, int note, int nth = 0) {
    std::vector<std::pair<uint64_t, int>> found;
    for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
        const auto info = d.engine().voice(i);
        if (info.on && info.note == note) found.emplace_back(info.age, i);
    }
    std::sort(found.begin(), found.end());
    return nth < int(found.size()) ? found[size_t(nth)].second : -1;
}

TEST_CASE("Driver: channel and polyphonic pressure drive the Aftertouch target (D-432)",
          "[plugin][driver][expression]") {
    Rig g;
    g.v[size_t(idx("aftertouch"))] = 2;  // Volume
    g.v[size_t(paramIndex(0, Track::Volume))] = 8;
    g.run(512);
    g.run(2400, {msg(0, 0x90, 60, 100), msg(0, 0x90, 64, 100)});
    const int a = voiceOf(g.d, 60), b = voiceOf(g.d, 64);
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    CHECK(g.d.engine().voice(a).volume == 8);
    g.run(2400, {msg(0, 0xa0, 64, 127)});  // polyphonic: note 64 only
    g.run(2400);
    CHECK(g.d.engine().voice(a).volume == 8);
    CHECK(g.d.engine().voice(b).volume == 15);
    g.run(2400, {MidiEvent{0, 2, {0xd0, 127, 0}}});  // channel pressure: both
    g.run(2400);
    CHECK(g.d.engine().voice(a).volume == 15);
    g.run(2400, {msg(0, 0xb0, 121, 0)});  // Reset All Controllers: the channel pressure back to rest
    g.run(2400);
    CHECK(g.d.engine().voice(a).volume == 8);
}

TEST_CASE("Driver: MPE in 16 ch, one note per member channel, routing of the keyboard (D-432)",
          "[plugin][driver][expression][mpe]") {
    Rig g;
    g.v[size_t(idx("mpe"))] = 1;
    g.v[size_t(idx("mpeBendRange"))] = 12;
    g.v[size_t(idx("midiRouting"))] = 2;  // Per channel: ignored in MPE
    g.v[size_t(paramIndex(0, Track::Volume))] = 8;
    g.run(512);
    g.run(2400, {msg(0, 0x91, 69, 100), msg(0, 0x92, 69, 100)});  // channels 2 and 3
    const int a = voiceOf(g.d, 69, 0), b = voiceOf(g.d, 69, 1);
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    CHECK(g.d.engine().voice(a).track == 0);  // the focused track
    CHECK(g.d.engine().voice(b).track == 0);
    const int period = g.d.engine().voice(b).tonePeriod;
    g.run(2400, {msg(0, 0xe1, 0, 0x60)});  // channel 2: +6 semitones of 12
    g.run(2400);
    CHECK(g.d.engine().voice(a).tonePeriod < period * 3 / 4);
    CHECK(g.d.engine().voice(b).tonePeriod == period);
    g.run(2400, {msg(0, 0xb2, 74, 127)});  // CC74 of channel 3: Volume
    g.run(2400);
    CHECK(g.d.engine().voice(a).volume == 8);
    CHECK(g.d.engine().voice(b).volume == 15);
    g.run(2400, {msg(0, 0x82, 69, 0)});  // note-off of channel 3: its note only
    g.run(9600);
    CHECK(g.d.engine().voice(a).on);
    CHECK_FALSE(g.d.engine().voice(b).on);

    // MPE off: a bend of channel 2 bends every note (Omni).
    Rig o;
    o.run(512);
    o.run(2400, {msg(0, 0x91, 69, 100), msg(0, 0x92, 72, 100)});
    const int c = voiceOf(o.d, 72);
    REQUIRE(c >= 0);
    const int before = o.d.engine().voice(c).tonePeriod;
    o.run(2400, {msg(0, 0xe1, 0, 0x7f)});
    CHECK(o.d.engine().voice(c).tonePeriod < before);
}

TEST_CASE("Driver: per-channel routing, the channel's track plays even when inactive", "[plugin][driver]") {
    Rig g;
    g.v[size_t(idx("midiRouting"))] = 2;
    g.run(256);
    g.run(256, {msg(0, 0x92, 64, 100)});  // channel 3 → track 3
    bool found = false;
    for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
        const auto info = g.d.engine().voice(i);
        if (info.on && info.note == 64) found = info.track == 2;
    }
    CHECK(found);
    // Omni: the focused track, whatever the channel.
    Rig o;
    o.run(256, {msg(0, 0x95, 62, 100)});
    found = false;
    for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
        const auto info = o.d.engine().voice(i);
        if (info.on && info.note == 62) found = info.track == 0;
    }
    CHECK(found);
}

TEST_CASE("Driver: on-screen notes on the keyboard track, CC 120 and 123 from the screen", "[plugin][driver]") {
    Rig g;
    g.v[size_t(idx("midiRouting"))] = 2;  // per channel: does not affect the screen
    g.v[size_t(idx("focusTrack"))] = 4;
    g.v[size_t(idx("t04_on"))] = 1;
    MidiEvent on = msg(0, 0x90, 64, 100);
    on.screen = true;
    g.run(256, {on});
    int track = -1;
    for (int i = 0; i < ymengine::Engine::kVoices; ++i)
        if (g.d.engine().voice(i).on) track = g.d.engine().voice(i).track;
    CHECK(track == 3);
    // CC 120 from the screen: manual play released (fade); from a MIDI keyboard, it would stay "all sound off".
    MidiEvent off = msg(0, 0xb0, 120, 0);
    off.screen = true;
    g.run(9600, {off});
    CHECK(g.d.engine().activeVoices() == 0);
    g.run(256, {on});
    CHECK(g.d.engine().activeVoices() > 0);
    MidiEvent panic = msg(0, 0xb0, 123, 0);
    panic.screen = true;
    g.run(256, {panic});
    g.run(256);
    CHECK(g.d.engine().activeVoices() == 0);
}

TEST_CASE("Engine status: LEDs of audible tracks, attacked note, followed envelope, oscilloscope",
          "[plugin][engine-status]") {
    Rig g;
    EngineStatusTap tap;
    tap.prepare(48000);
    auto run = [&](uint32_t n, std::vector<MidiEvent> midi = {}) {
        g.run(n, std::move(midi));
        tap.capture(g.d.engine(), g.v, g.l.data(), g.r.data(), n);
    };
    g.v[size_t(idx("t01_attack"))] = 100;
    run(1600, {msg(0, 0x90, 69, 100)});  // 1600 samples: one publication (48000 / 30)
    REQUIRE(tap.update());
    EngineStatus t = tap.latest();
    CHECK(t.active[0]);
    CHECK(!t.active[1]);
    CHECK(t.noteSerial == 1);
    CHECK(t.notePeriod == 284);
    CHECK(t.envCount == 1);
    CHECK(t.env[0].stage == 1);
    CHECK(std::fabs(t.env[0].level - 1600.0f / 4800.0f) < 0.01f);
    CHECK(t.env[1].stage == -1);
    float peak = 0;
    for (float x : t.scope) peak = std::max(peak, std::fabs(x));
    CHECK(peak > 0.01f);
    // Mixer meters: peak of the played track only, output peaks, oscilloscope of the track.
    CHECK(t.peak[0] > 0.01f);
    CHECK(t.peak[0] <= 0.75f);  // default track level
    for (int k = 1; k < kTracks; ++k) CHECK(t.peak[size_t(k)] == 0.0f);
    CHECK(t.outPeak[0] > 0.01f);
    CHECK(t.outPeak[0] == t.outPeak[1]);  // centred
    CHECK(*std::max_element(t.trackScope[0].begin(), t.trackScope[0].end()) > 0.01f);
    CHECK(*std::max_element(t.trackScope[1].begin(), t.trackScope[1].end()) == 0.0f);
    CHECK(t.arpStep == -1);  // arpeggio stopped
    CHECK(!tap.update());  // nothing new
    // Track muted: no more LED; the note stays the last attacked one.
    g.v[size_t(idx("t01_mute"))] = 1;
    run(1600);
    REQUIRE(tap.update());
    CHECK(!tap.latest().active[0]);
    CHECK(tap.latest().noteSerial == 1);
    CHECK(tap.latest().peak[0] == 0.0f);  // peaks since the previous publication
    // Focused track without a voice: no envelope followed.
    g.v[size_t(idx("focusTrack"))] = 2;
    run(1600);
    REQUIRE(tap.update());
    CHECK(tap.latest().envCount == 0);
    CHECK(tap.latest().env[0].stage == -1);
    // Arpeggio of the focused track in Table mode, one held note: step played from the table.
    g.v[size_t(idx("t02_arpOn"))] = 1;
    Snapshot s;
    s.state.tracks[1].arp.mode = ymengine::ArpMode::Table;
    g.d.publish(s);
    run(1600, {msg(0, 0x90, 60, 100)});
    REQUIRE(tap.update());
    CHECK(tap.latest().arpStep >= 0);
    CHECK(tap.latest().arpStep < 3);
}

TEST_CASE("Engine status: a voice sounding until the release ends, the last period kept (Indus, 26/10/2026 04:00, "
          "D-440)",
          "[plugin][engine-status]") {
    Rig g;
    EngineStatusTap tap;
    tap.prepare(48000);
    auto run = [&](uint32_t n, std::vector<MidiEvent> midi = {}) {
        g.run(n, std::move(midi));
        tap.capture(g.d.engine(), g.v, g.l.data(), g.r.data(), n);
    };
    CHECK(!tap.latest().noteSounding);
    g.v[size_t(idx("t01_release"))] = 50;
    run(1600, {msg(0, 0x90, 69, 100)});
    REQUIRE(tap.update());
    CHECK(tap.latest().noteSounding);
    // Released: the release still sounds for 50 ms.
    run(1600, {msg(0, 0x80, 69, 0)});
    REQUIRE(tap.update());
    CHECK(tap.latest().noteSounding);
    // The release ended: no voice; the period and the serial of the last note stay (register monitor).
    for (int k = 0; k < 4; ++k) run(1600);
    REQUIRE(tap.update());
    CHECK(!tap.latest().noteSounding);
    CHECK(tap.latest().notePeriod == 284);
    CHECK(tap.latest().noteSerial == 1);
}

TEST_CASE("Engine status: one envelope point per note sounding on the focused track, the newest first (Indus, "
          "24/10/2026 11:45, D-403)",
          "[plugin][engine-status]") {
    Rig g;
    EngineStatusTap tap;
    tap.prepare(48000);
    auto run = [&](uint32_t n, std::vector<MidiEvent> midi = {}) {
        g.run(n, std::move(midi));
        tap.capture(g.d.engine(), g.v, g.l.data(), g.r.data(), n);
    };
    g.v[size_t(idx("t01_attack"))] = 10;
    g.v[size_t(idx("t01_release"))] = 2000;  // a long release: the notes released still sound
    // A note, released after one publication, then a second note while the first one still sounds.
    run(1600, {msg(0, 0x90, 60, 100)});
    run(1600, {msg(0, 0x80, 60, 0), msg(0, 0x90, 64, 100)});
    REQUIRE(tap.update());
    EngineStatus t = tap.latest();
    CHECK(t.envCount == 2);
    CHECK(t.env[0].stage != 4);  // the newest: the note held
    CHECK(t.env[1].stage == 4);  // the first note, in its release
    CHECK(t.env[1].level > 0.5f);
    CHECK(t.env[2].stage == -1);
    // A third note: three points.
    run(1600, {msg(0, 0x90, 67, 100)});
    REQUIRE(tap.update());
    t = tap.latest();
    CHECK(t.envCount == 3);
    CHECK(t.env[2].stage == 4);
    // The releases over: the held notes only.
    run(48000 * 3);
    REQUIRE(tap.update());
    t = tap.latest();
    CHECK(t.envCount == 2);
    CHECK(t.env[0].stage != 4);
    CHECK(t.env[1].stage != 4);
}

TEST_CASE("Engine status: a monophonic track shows one envelope point at a time, the newest note's (Indus, "
          "24/10/2026 14:51)",
          "[plugin][engine-status]") {
    // Voicing Monophonic in 16 channels; any track of the 3-channel modes (one voice per channel, Voicing greyed),
    // whose keyboard notes still end their release on the other channels of the chip.
    for (const int mode : {0, 1, 2}) {
        CAPTURE(mode);
        Rig g;
        g.v[size_t(idx("voiceMode"))] = float(mode);
        if (mode == 0) g.v[size_t(idx("t01_voicing"))] = 1;  // Monophonic
        g.v[size_t(idx("t01_attack"))] = 10;
        g.v[size_t(idx("t01_release"))] = 2000;  // a long release: a released note would still sound
        EngineStatusTap tap;
        tap.prepare(48000);
        auto run = [&](uint32_t n, std::vector<MidiEvent> midi = {}) {
            g.run(n, std::move(midi));
            tap.capture(g.d.engine(), g.v, g.l.data(), g.r.data(), n);
        };
        auto points = [&] {
            REQUIRE(tap.update());
            return int(tap.latest().envCount);
        };
        run(1600);
        // Notes held over each other, then played apart, then the same note again: one point each time.
        run(1600, {msg(0, 0x90, 60, 100)});
        CHECK(points() == 1);
        run(1600, {msg(0, 0x90, 64, 100)});
        CHECK(points() == 1);
        run(1600, {msg(0, 0x80, 60, 0), msg(0, 0x80, 64, 0)});
        CHECK(points() == 1);  // the last note, in its release
        CHECK(tap.latest().env[0].stage == 4);
        run(1600, {msg(0, 0x90, 67, 100)});
        CHECK(points() == 1);
        CHECK(tap.latest().env[0].stage != 4);
        run(1600, {msg(0, 0x80, 67, 0)});
        CHECK(points() == 1);
        run(1600, {msg(0, 0x90, 67, 100)});
        CHECK(points() == 1);
        CHECK(tap.latest().env[0].stage != 4);
        run(1600, {msg(0, 0x80, 67, 0), msg(0, 0x90, 69, 100), msg(0, 0x90, 71, 100)});
        CHECK(points() == 1);
    }
}

TEST_CASE("Engine status: the followed envelope ignores the echo repeats of the track", "[plugin][engine-status]") {
    // Ninth DAW test, item 113: a held note with a long attack and the echo on; each repeat starts a voice of the
    // same track, and the cursor of the curve went back to zero at each one.
    Rig g;
    EngineStatusTap tap;
    tap.prepare(48000);
    g.v[size_t(idx("t01_attack"))] = 1000;
    Snapshot s;
    s.state.tracks[0].patch.echo.on = true;
    g.d.publish(s);
    g.run(256);
    bool echoed = false;
    float last = -1;
    g.run(1600, {msg(0, 0x90, 60, 100)});
    tap.capture(g.d.engine(), g.v, g.l.data(), g.r.data(), 1600);
    for (int k = 0; k < 27; ++k) {  // 27 publications of 1600 samples: inside the attack of 48000 samples
        REQUIRE(tap.update());
        const EngineStatus t = tap.latest();
        CHECK(t.envCount == 1);  // the note alone, not its repeats
        CHECK(t.env[0].stage == 1);
        CHECK(t.env[0].level >= last);
        last = t.env[0].level;
        for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
            const auto v = g.d.engine().voice(i);
            echoed = echoed || (v.on && v.tag == ymengine::VoiceTag::Echo && v.track == 0);
        }
        g.run(1600);
        tap.capture(g.d.engine(), g.v, g.l.data(), g.r.data(), 1600);
    }
    CHECK(echoed);
    CHECK(last > 0.8f);
}

TEST_CASE("Driver: controllers of the prototype → parameters, host notified", "[plugin][driver]") {
    Rig g;
    Recorder rec;
    g.run(256, {msg(5, 0xb0, 74, 127), msg(6, 0xb0, 75, 80), msg(7, 0xb0, 7, 127), msg(8, 0xb0, 84, 17)}, false, &rec);
    CHECK(g.v[size_t(idx("t01_envPeriod"))] == 32.0f);
    CHECK(g.v[size_t(idx("t01_envShape"))] == 10.0f);
    CHECK(g.v[size_t(idx("master"))] == 100.0f);
    CHECK(g.v[size_t(idx("focusTrack"))] == 3.0f);
    REQUIRE(rec.changes.size() == 4);
    CHECK(rec.changes[0].first == idx("t01_envPeriod"));
    // The focused track follows CC 84: the next CC targets track 3.
    g.run(256, {msg(0, 0xb0, 72, 127)}, false, &rec);
    CHECK(g.v[size_t(idx("t03_noisePeriod"))] == 31.0f);
    CHECK(g.d.engine().track(2).patch.noisePeriod == 31);
    // Sync Buzzer: fast envelope imposed (setEffect), and only if the period is slow.
    g.v[size_t(idx("t03_envPeriod"))] = 12800;
    g.run(256, {msg(0, 0xb0, 76, 3 * 26)}, false, &rec);
    CHECK(g.v[size_t(idx("t03_generator"))] == 3.0f);
    CHECK(g.v[size_t(idx("t03_envPeriod"))] == 32.0f);
    // 3 channels: a hardware channel does not take the Digidrum generator.
    g.v[size_t(idx("voiceMode"))] = 1;
    g.run(4096);
    rec.changes.clear();
    g.run(256, {msg(0, 0xb0, 76, 2 * 26)}, false, &rec);
    CHECK(g.v[size_t(idx("t03_generator"))] == 3.0f);
    CHECK(rec.changes.empty());
}

TEST_CASE("Driver: pitch bend according to the set range", "[plugin][driver]") {
    Rig g;
    g.v[size_t(idx("bendRange"))] = 12;
    auto period = [&] {
        for (int i = 0; i < ymengine::Engine::kVoices; ++i) {
            const auto info = g.d.engine().voice(i);
            if (info.on && info.note == 57) return info.tonePeriod;
        }
        return uint16_t(0);
    };
    g.run(2400, {msg(0, 0x90, 57, 100)});
    const uint16_t base = period();
    g.run(2400, {msg(0, 0xe0, 0, 0)});  // -8192: one octave lower
    const uint16_t low = period();
    REQUIRE(base > 0);
    CHECK(std::abs(double(low) / double(base) - 2.0) < 0.01);
}

TEST_CASE("Driver: voice mode change after a fade, notes ignored meanwhile", "[plugin][driver]") {
    Rig g;
    g.run(2400, {msg(0, 0x90, 60, 100)});
    g.v[size_t(idx("voiceMode"))] = 1;
    const uint32_t fade = g.d.fadeSamples();
    g.run(16, {msg(4, 0x90, 64, 100)});
    CHECK(g.d.fading());
    CHECK(g.d.engine().global().mode == ymengine::VoiceMode::Studio16);
    g.run(fade);
    CHECK_FALSE(g.d.fading());
    CHECK(g.d.engine().global().mode == ymengine::VoiceMode::Authentic3);
    for (int i = 0; i < ymengine::Engine::kVoices; ++i) CHECK(g.d.engine().voice(i).note != 64);
}

TEST_CASE("Driver: Digidrum definition kept from 16 to 3 channels and back (D-063)", "[plugin][driver]") {
    using ymengine::Generator;
    Rig g;
    Snapshot s;
    s.state.tracks[0].patch.digidrumSlot = 5;  // hardware channel in 3 channels
    s.state.tracks[3].patch.digidrumSlot = 2;  // Digidrum track of chip 1
    s.state.global.digidrumChannel = {1, 0};
    s.state.digidrums[5] = {1, 2, 3, 4};
    s.sampleVersion[5] = 1;
    g.d.publish(s);
    g.v[size_t(idx("t01_generator"))] = 2;  // Digidrum
    g.v[size_t(idx("t04_generator"))] = 1;  // SID
    g.run(256);
    const auto& e = g.d.engine();
    auto definitionsKept = [&] {
        CHECK(e.track(0).patch.digidrumSlot == 5);
        CHECK(e.track(3).patch.digidrumSlot == 2);
        CHECK(e.global().digidrumChannel == std::array<uint8_t, 2>{1, 0});
        CHECK(g.v[size_t(idx("t01_generator"))] == 2.0f);
        CHECK(g.v[size_t(idx("t04_generator"))] == 1.0f);
    };
    CHECK(e.track(0).patch.generator == Generator::Digidrum);
    definitionsKept();
    for (int round = 0; round < 2; ++round) {
        INFO("round trip " << round);
        g.v[size_t(idx("voiceMode"))] = 1;  // 16 → 3 channels: roles imposed at render time
        g.run(g.d.fadeSamples() + 256);
        REQUIRE(e.global().mode == ymengine::VoiceMode::Authentic3);
        CHECK(e.track(0).patch.generator == Generator::Chip);
        CHECK(e.track(3).patch.generator == Generator::Digidrum);
        definitionsKept();
        g.v[size_t(idx("voiceMode"))] = 0;  // 3 → 16 channels: the user's setting comes back
        g.run(g.d.fadeSamples() + 256);
        REQUIRE(e.global().mode == ymengine::VoiceMode::Studio16);
        CHECK(e.track(0).patch.generator == Generator::Digidrum);
        CHECK(e.track(3).patch.generator == Generator::Sid);
        definitionsKept();
    }
}

TEST_CASE("Driver: Program Change (Omni, Layer, per channel), replayed on a new engine", "[plugin][driver][program]") {
    Rig g;
    auto pc = [](uint8_t ch, uint8_t p) { return MidiEvent{0, 2, {uint8_t(0xC0 | ch), p, 0}}; };
    auto programs = [&] {
        std::array<int, kTracks> out{};
        for (int t = 0; t < kTracks; ++t) out[size_t(t)] = g.d.engine().program(t);
        return out;
    };
    // Omni: the focused track (3), whatever the channel.
    g.v[size_t(idx("focusTrack"))] = 3;
    g.run(256, {pc(9, 4)});
    std::array<int, kTracks> want{};
    want[2] = 4;
    CHECK(programs() == want);
    g.run(256, {pc(0, 33)});  // above 32: ignored
    CHECK(programs() == want);
    // Per channel: the channel's track.
    g.v[size_t(idx("midiRouting"))] = 2;
    g.run(256, {pc(5, 7)});
    want[5] = 7;
    CHECK(programs() == want);
    // Layer: all tracks, played together.
    g.v[size_t(idx("midiRouting"))] = 1;
    g.run(256, {pc(0, 2)});
    want.fill(2);
    CHECK(programs() == want);
    g.run(256, {pc(0, 0)});
    want.fill(0);
    CHECK(programs() == want);

    // New engine (clock): the choices are replayed.
    g.v[size_t(idx("midiRouting"))] = 2;
    g.run(256, {pc(1, 12)});
    g.v[size_t(idx("chipClock"))] = 1;
    g.run(512);
    g.d.service(g.v);
    g.run(512);
    g.run(g.d.fadeSamples() + 512);
    REQUIRE(g.d.engineSwaps() == 1);
    CHECK(g.d.engine().program(1) == 12);
    CHECK(g.d.engine().program(0) == 0);
}

TEST_CASE("Driver: new clock prepared outside the audio thread, then switch", "[plugin][driver]") {
    Rig g;
    g.run(512, {msg(0, 0x90, 60, 100)});
    g.v[size_t(idx("chipClock"))] = 1;
    g.run(512);
    CHECK(g.d.engineSwaps() == 0);  // the message thread has not prepared the engine yet
    g.d.service(g.v);               // message thread
    g.run(512);                     // taken, fade in progress
    g.run(g.d.fadeSamples() + 512);
    CHECK(g.d.engineSwaps() == 1);
    CHECK(g.d.engineKey().clock == 1);
    g.d.service(g.v);               // destroys the old engine
    g.run(4800, {msg(0, 0x90, 60, 100)});
    CHECK(g.energy() > 1e-3);
}

TEST_CASE("Driver: Custom clock, a new engine at each frequency, 2 MHz outside 16 ch (D-237)", "[plugin][driver]") {
    Rig g;
    auto settle = [&g] {
        g.run(512);
        g.d.service(g.v);
        g.run(512);
        g.run(g.d.fadeSamples() + 512);
        g.d.service(g.v);
    };
    g.run(512);
    CHECK(g.d.engineKey().hz == 2000000);
    g.v[size_t(idx("chipClockHz"))] = 3000000;
    settle();
    CHECK(g.d.engineSwaps() == 1);
    CHECK(g.d.engineKey().hz == 3000000);
    CHECK(g.d.engine().chip(0).config().chip.clock.hz() == 3e6);
    g.run(4800, {msg(0, 0x90, 60, 100)});
    CHECK(g.energy() > 1e-3);
    // Authentic 3ch: the Custom entry plays 2 MHz.
    g.v[size_t(idx("voiceMode"))] = 1;
    settle();
    settle();
    CHECK(g.d.engineKey().hz == 2000000);
    CHECK(g.d.engine().chip(0).config().chip.clock.hz() == 2e6);
    // Its frequency changes nothing there, nor with another clock.
    const uint64_t swaps = g.d.engineSwaps();
    g.v[size_t(idx("chipClockHz"))] = 4000000;
    settle();
    CHECK(g.d.engineSwaps() == swaps);
    g.v[size_t(idx("voiceMode"))] = 0;
    g.v[size_t(idx("chipClock"))] = 1;
    settle();
    settle();
    CHECK(g.d.engineKey().clock == 1);
    const uint64_t atari = g.d.engineSwaps();
    g.v[size_t(idx("chipClockHz"))] = 5000000;
    settle();
    CHECK(g.d.engineSwaps() == atari);
}

TEST_CASE("Driver: outside real time, new preparation at the same sample", "[plugin][driver]") {
    auto render = [](bool) {
        Rig g;
        std::vector<float> all;
        for (int b = 0; b < 20; ++b) {
            if (b == 5) g.v[size_t(idx("resampling"))] = 2;
            if (b == 9) g.v[size_t(idx("outputChain"))] = 1;
            g.run(480, b % 4 == 0 ? std::vector<MidiEvent>{msg(17, 0x90, uint8_t(60 + b), 90)} : std::vector<MidiEvent>{},
                  true);
            all.insert(all.end(), g.l.begin(), g.l.end());
        }
        CHECK(g.d.engineSwaps() == 2);
        return all;
    };
    CHECK(render(true) == render(true));
}

TEST_CASE("Driver: render independent of the block size (MIDI and CC included)", "[plugin][driver]") {
    // Events at absolute positions; two different splits into blocks.
    struct Ev {
        uint32_t at;
        MidiEvent m;
    };
    const std::vector<Ev> evs = {{100, msg(0, 0x90, 60, 100)}, {700, msg(0, 0xb0, 74, 90)},
                                 {700, msg(0, 0x90, 67, 80)},  {1500, msg(0, 0xe0, 0, 80)},
                                 {2100, msg(0, 0x80, 60, 0)},  {2600, msg(0, 0xb0, 1, 127)},
                                 {3333, msg(0, 0xb0, 70, 0)},  {4000, msg(0, 0x90, 72, 127)}};
    auto render = [&](uint32_t block) {
        Rig g;
        std::vector<float> all;
        for (uint32_t at = 0; at < 9600; at += block) {
            std::vector<MidiEvent> m;
            for (const Ev& e : evs)
                if (e.at >= at && e.at < at + block) {
                    MidiEvent x = e.m;
                    x.offset = e.at - at;
                    m.push_back(x);
                }
            g.run(block, m);
            all.insert(all.end(), g.l.begin(), g.l.end());
        }
        return all;
    };
    const std::vector<float> a = render(64), b = render(480);
    REQUIRE(a.size() == b.size());
    CHECK(a == b);
}

TEST_CASE("Driver: non-automatable settings published during playback", "[plugin][driver]") {
    Rig g;
    g.run(256);
    Snapshot s;
    s.state.kit = ymengine::kKitBanks[3].kit;
    s.kitVersion = 1;
    s.state.digidrums[2] = {1, 2, 3};
    s.sampleVersion[2] = 1;
    s.state.tracks[0].patch.ornamentOn = true;
    g.d.publish(s);
    g.run(256);
    CHECK(g.d.engine().kit().inst[0].b == ymengine::kKitBanks[3].kit.inst[0].b);
    CHECK(g.d.engine().track(0).patch.ornamentOn);
    g.d.service(g.v);
}

TEST_CASE("Driver: separate track outputs", "[plugin][driver]") {
    Rig g(0x0003);
    g.v[size_t(idx("t02_on"))] = 1;
    g.v[size_t(idx("midiRouting"))] = 2;
    std::vector<float> l(4800), r(4800), t1(4800), t2(4800);
    ymengine::Outputs o;
    o.left = l.data();
    o.right = r.data();
    o.track[0] = t1.data();
    o.track[1] = t2.data();
    std::vector<MidiEvent> m = {msg(0, 0x91, 60, 100)};
    g.d.process(g.v, m, o, 4800, false, nullptr);
    double e1 = 0, e2 = 0;
    for (size_t i = 0; i < t1.size(); ++i) {
        e1 += double(t1[i]) * t1[i];
        e2 += double(t2[i]) * t2[i];
    }
    CHECK(e1 < 1e-9);
    CHECK(e2 > 1e-3);
}

namespace {

// Settings published with a sequencer: pattern 1 (16 steps, one note every four steps on track 1) and pattern 3
// (8 steps), current pattern `current`.
Snapshot seqSnapshot(ymengine::SeqClock clock, int current = 0) {
    Snapshot s;
    s.state.global.seqClock = clock;
    ymseq::Project& p = s.state.seq;
    ymseq::Lane& l = p.patterns[0].edit(0);
    for (int k = 0; k < 16; k += 4) l.pat[size_t(k)] = 0;
    p.patterns[2].steps = 8;
    p.patterns[2].edit(0).pat[0] = 7;
    p.used.set(2);
    p.current = current;
    sanitize(s.state);
    fillSequence(p, s.seq);
    s.patternSerial = 1;
    return s;
}

MidiEvent command(uint32_t at, UiCommand c, uint8_t a = 0, uint8_t b = 0, uint8_t d = 0) {
    MidiEvent e{at, 0, {a, b, d}};
    e.screen = true;
    e.command = c;
    return e;
}

struct SeqRig : Rig {
    explicit SeqRig(ymengine::SeqClock clock, int current = 0) { d.prepare({48000, 0}, v, seqSnapshot(clock, current)); }
    ymengine::Engine::SeqStatus status() const { return d.engine().seqStatus(); }
};

}  // namespace

TEST_CASE("Driver: sequencer, interface commands, CC 82 and pattern of the state", "[plugin][driver][seq]") {
    SeqRig g(ymengine::SeqClock::Internal, 2);
    g.run(256);
    CHECK(g.status().pattern == 2);  // current pattern of the state
    CHECK_FALSE(g.status().playing);
    g.run(4800, {command(0, UiCommand::SeqSelect, 0), command(10, UiCommand::SeqPlay)});
    CHECK(g.status().playing);
    CHECK(g.status().pattern == 0);
    CHECK(g.energy() > 1e-3);
    g.run(256, {command(0, UiCommand::SeqSelect, 2)});
    CHECK(g.status().queued == 2);  // when playing: at the end of the loop
    g.run(256, {command(0, UiCommand::SeqStop)});
    CHECK_FALSE(g.status().playing);
    CHECK(g.status().pattern == 2);
    g.run(256, {msg(0, 0xb0, 82, 127)});  // CC 82 of the prototype: Play
    CHECK(g.status().playing);
    g.run(256, {msg(0, 0xb0, 82, 0)});
    CHECK_FALSE(g.status().playing);
    g.run(48000);
    CHECK(g.d.engine().activeVoices() == 0);
    // Preview of a grid step.
    g.run(256, {command(0, UiCommand::SeqPreview, 0, 60, 127)});
    CHECK(g.d.engine().activeVoices() > 0);
}

TEST_CASE("Driver: jump to a Song entry, number on two bytes", "[plugin][driver][seq]") {
    Snapshot s = seqSnapshot(ymengine::SeqClock::Internal, 0);
    s.state.global.songMode = true;
    s.state.seq.song.entries.assign(299, ymseq::SongEntry{0, 1, 0});
    s.state.seq.song.entries.push_back({2, 1, 0});
    fillSequence(s.state.seq, s.seq);
    Rig g;
    g.d.prepare({48000, 0}, g.v, std::move(s));
    g.run(256, {command(0, UiCommand::SeqSongJump, 299 & 0xff, 299 >> 8)});
    CHECK(g.d.engine().seqStatus().songPos == 299);
    CHECK(g.d.engine().seqStatus().pattern == 2);
}

TEST_CASE("Driver: Drum Kit Audition, audition of an instrument outside the voices (UiCommand::DrumPreview)",
          "[plugin][driver][kit]") {
    Rig g;
    g.run(4800);
    CHECK(g.energy() == 0);
    g.run(4800, {command(0, UiCommand::DrumPreview, uint8_t(ymengine::DrumSlot::Kick))});
    CHECK(g.energy() > 0);
    CHECK(g.d.engine().activeVoices() == 0);
    // Bass drum: 2340 samples at 7800 Hz, 0.3 s; the audition stops by itself.
    g.run(14400);
    g.run(4800);
    CHECK(g.energy() == 0);
    // Slot outside the table: clamped to the last one (Tom 2).
    g.run(4800, {command(0, UiCommand::DrumPreview, 200)});
    CHECK(g.energy() > 0);
}

TEST_CASE("Driver: Gate All on, off or global gate changed: all tracks take it up (D-134)",
          "[plugin][driver][seq]") {
    Rig g;
    g.v[size_t(idx("t03_gate"))] = 20;
    g.v[size_t(idx("gate"))] = 80;
    g.run(256);
    auto gates = [&g] {
        std::array<double, kTracks> a{};
        for (int t = 0; t < kTracks; ++t) a[size_t(t)] = g.d.engine().track(t).seq.gate;
        return a;
    };
    CHECK(gates()[2] == 0.2);
    CHECK(gates()[0] == 0.5);
    g.v[size_t(idx("gateAll"))] = 1;
    g.run(256);
    for (double x : gates()) CHECK(x == 0.8);
    g.v[size_t(idx("gate"))] = 35;
    g.run(256);
    for (double x : gates()) CHECK(x == 0.35);
    g.v[size_t(idx("t03_gate"))] = 60;  // track gate changed under the global gate: no effect
    g.run(256);
    CHECK(gates()[2] == 0.35);
    g.v[size_t(idx("gateAll"))] = 0;
    g.run(256);
    CHECK(gates()[2] == 0.6);
    CHECK(gates()[0] == 0.5);
    // Off: the global gate no longer affects the tracks.
    g.v[size_t(idx("gate"))] = 90;
    g.run(256);
    CHECK(gates()[0] == 0.5);
}

TEST_CASE("Driver: CC 83 sets the free tempo like the BPM slider of the prototype", "[plugin][driver][seq]") {
    SeqRig g(ymengine::SeqClock::Internal);
    Recorder rec;
    g.run(256, {msg(0, 0xb0, 83, 127)}, false, &rec);
    CHECK(g.v[size_t(idx("freeTempo"))] == 240.0f);
    CHECK(g.d.engine().global().tempo == 240.0);
    g.run(256, {msg(0, 0xb0, 83, 64)}, false, &rec);
    CHECK(g.v[size_t(idx("freeTempo"))] == 151.0f);  // 60 + round(64 / 127 × 180)
    REQUIRE(rec.changes.size() == 2);
    CHECK(rec.changes[1].first == idx("freeTempo"));
}

TEST_CASE("Driver: MIDI Start, Stop, Continue and Song Position Pointer", "[plugin][driver][seq]") {
    SeqRig g(ymengine::SeqClock::Midi);
    g.run(256, {command(0, UiCommand::SeqPlay), msg(10, 0xfa, 0, 0)});
    auto clocks = [&](int n) {
        for (int k = 0; k < n; ++k) g.run(1000, {msg(0, 0xf8, 0, 0)});
    };
    clocks(12);  // two sixteenth-note steps (6 pulses)
    CHECK(g.status().stepAbs == 1);
    CHECK(g.status().clockRunning);
    MidiEvent stop{0, 1, {0xfc, 0, 0}};
    g.run(256, {stop});
    CHECK_FALSE(g.status().clockRunning);
    clocks(12);
    CHECK(g.status().stepAbs == 1);
    MidiEvent spp{0, 3, {0xf2, 20, 0}};  // 20 sixteenth notes
    MidiEvent cont{1, 1, {0xfb, 0, 0}};
    g.run(256, {spp, cont});
    CHECK(g.status().clockRunning);
    clocks(1);
    CHECK(g.status().stepAbs == 20);
    CHECK(g.status().step == 4);
}

TEST_CASE("Driver: Clock selector on MIDI ext, the sequencer follows the MIDI clock (D-188)", "[plugin][driver][seq]") {
    SeqRig g(ymengine::SeqClock::Internal);
    g.run(4800, {command(0, UiCommand::SeqPlay)});
    REQUIRE(g.status().playing);
    // Clock switches to MIDI ext during playback: new clock, stop (D-110).
    g.v[size_t(idx("arpClock"))] = 1;
    g.run(256);
    CHECK_FALSE(g.status().playing);
    // Play arms the sequencer; without Start or pulses, it waits.
    g.run(256, {command(0, UiCommand::SeqPlay)});
    g.run(48000);
    CHECK(g.status().stepAbs <= 0);
    CHECK_FALSE(g.status().clockRunning);
    g.run(256, {msg(0, 0xfa, 0, 0)});
    for (int k = 0; k < 12; ++k) g.run(1000, {msg(0, 0xf8, 0, 0)});
    CHECK(g.status().clockRunning);
    CHECK(g.status().stepAbs == 1);  // 12 pulses: two sixteenth notes
    // Back to Internal: stop, then playback at the free tempo without pulses.
    g.v[size_t(idx("arpClock"))] = 0;
    g.run(256);
    CHECK_FALSE(g.status().playing);
    g.run(256, {command(0, UiCommand::SeqPlay)});
    g.run(48000);
    CHECK(g.status().playing);
    CHECK(g.status().stepAbs > 4);
}

TEST_CASE("Driver: Host sync, host position at each block segment", "[plugin][driver][seq]") {
    // Same events (CCs that cut the render) at absolute positions, two splits into blocks.
    auto render = [](uint32_t block) {
        SeqRig g(ymengine::SeqClock::Host);
        ymengine::Engine::HostTransport t;
        t.playing = true;
        t.bpm = 120;
        std::vector<float> all;
        const std::vector<uint32_t> ccAt = {700, 6001, 11999, 18000, 30017};
        for (uint32_t at = 0; at < 48000; at += block) {
            std::vector<MidiEvent> m;
            for (uint32_t c : ccAt)
                if (c >= at && c < at + block) m.push_back(msg(c - at, 0xb0, 7, uint8_t(90 + (c & 7))));
            g.d.setHostTransport(t);
            g.run(block, m);
            all.insert(all.end(), g.l.begin(), g.l.end());
            t.ppq += block * 120.0 / 60 / 48000;
        }
        CHECK(g.status().playing);
        CHECK(g.status().stepAbs == 7);  // two quarter notes: steps 0 to 7
        return all;
    };
    const std::vector<float> a = render(64), b = render(480);
    REQUIRE(a.size() == b.size());
    CHECK(a == b);
    double e = 0;
    for (float x : a) e += double(x) * x;
    CHECK(e > 1e-3);
}

TEST_CASE("Driver: metronome muted outside real time (bounce, export)", "[plugin][driver][seq]") {
    auto energy = [](bool offline) {
        SeqRig g(ymengine::SeqClock::Internal);
        Snapshot s = seqSnapshot(ymengine::SeqClock::Internal);
        s.state.seq.patterns[0] = ymseq::Pattern{};  // empty pattern: only the metronome sounds
        fillSequence(s.state.seq, s.seq);
        s.state.global.metronome = true;
        g.d.prepare({48000, 0}, g.v, s);
        g.run(4800, {command(0, UiCommand::SeqPlay)}, offline);
        return g.energy();
    };
    CHECK(energy(false) > 1e-3);
    CHECK(energy(true) < 1e-9);
}

TEST_CASE("Driver: patterns published during playback; new engine, the sequencer continues", "[plugin][driver][seq]") {
    SeqRig g(ymengine::SeqClock::Internal);
    g.run(256, {command(0, UiCommand::SeqPlay)});
    g.run(6000 * 6);
    CHECK(g.status().step == 6);
    Snapshot s = seqSnapshot(ymengine::SeqClock::Internal);
    s.state.seq.patterns[0].steps = 4;  // shortened pattern: the current step is brought back into the length
    fillSequence(s.state.seq, s.seq);
    s.patternSerial = 1;                // same serial: no new pattern choice
    g.d.publish(s);
    g.run(256);
    CHECK(g.status().playing);
    CHECK(g.status().step < 4);
    g.d.service(g.v);
    // New chip clock: new engine prepared outside the audio thread, playback continues.
    g.run(256, {command(0, UiCommand::SeqSelect, 2)});
    g.run(6000 * 4);
    CHECK(g.status().pattern == 2);
    g.v[size_t(idx("chipClock"))] = 1;
    g.run(256);
    g.d.service(g.v);
    g.run(256);
    g.run(g.d.fadeSamples() + 512);
    REQUIRE(g.d.engineSwaps() == 1);
    CHECK(g.status().playing);
    CHECK(g.status().pattern == 2);
}

TEST_CASE("Digidrum: audio file brought to 7800 Hz on 4 bits (loading of the prototype)", "[plugin][digidrum]") {
    // Linear resampling, the sample after the end counts as 0; quantisation round(v × 7.5 + 7.5).
    const std::vector<float> two = {-1.0f, 1.0f};
    CHECK(toDigidrum(two, 5200.0) == std::vector<uint8_t>{0, 10, 13});
    // Half up (Math.round): silence gives 8; bounds 0 and 15.
    CHECK(toDigidrum(std::vector<float>(4, 0.0f), 7800.0) == std::vector<uint8_t>(4, 8));
    CHECK(toDigidrum(std::vector<float>(4, 2.0f), 7800.0) == std::vector<uint8_t>(4, 15));
    CHECK(toDigidrum(std::vector<float>(4, -2.0f), 7800.0) == std::vector<uint8_t>(4, 0));
    // 3 s at most.
    CHECK(toDigidrum(std::vector<float>(48000 * 10, 0.5f), 48000.0).size() == kDigidrumMaxSamples);
    CHECK(toDigidrum(std::vector<float>(48000, 0.5f), 48000.0).size() == 7800);
    CHECK(toDigidrum({}, 48000.0).empty());
    CHECK(toDigidrum(two, 0.0).empty());
}
