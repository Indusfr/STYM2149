// STYM2149 — plugincore: JSON files of the File menu (preset, pattern, Song, kit)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/ProjectFiles.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "plugincore/DigidrumBanks.h"
#include "plugincore/Json.h"
#include "plugincore/Mapping.h"
#include "plugincore/Patches.h"
#include "ymseq/Tables.h"

namespace stym::plugin {

using json::Value;

namespace {

constexpr std::string_view kApp = "STYM2149";

// Rows of a track: LANEKEYS of the prototype (rows 0 to 15 of the state, then the chord voices in the order of
// POLYKEYS: pat2-pat6, oct2-oct6, dur2-dur6), then INS and the CMD rows of STYM2149: "cmd" holds the Xyz (and, in a
// file written before D-330, every kind of command, each going to the row of its kind), then one key per kind
// ("cmd" followed by its letter; D-330, D-338; "cmdL", "cmdM" and "cmdJ" since the twelfth DAW test, "cmdG", "cmdK"
// and "cmdQ" since D-393 and D-394). Xyz and Pxy are retired (D-340, D-344): "cmd" and "cmdP" are no longer written,
// an Xyz or a Pxy read is dropped. The "afl" flags and the Txx of a file written before the twelfth DAW test become Jxx
// and the "bpm" row of the pattern (items 144, 150).
struct LaneKey {
    std::string_view key;
    int row;
};
// Row of a chord voice (2 to 6) field (0: pat, 1: oct, 2: dur), after the 16 rows of the prototype.
constexpr int chordRow(int voice, int field) { return ymseq::kProtoRows + 3 * (voice - 2) + field; }
constexpr std::array<LaneKey, 31> kLaneKeys = {{
    {"pat", 0},  {"tie", 1},  {"slide", 2}, {"afl", 3},  {"vol", 4},  {"mix", 5},  {"slt", 6},  {"cut", 7},
    {"fx", 8},   {"oct", 9},  {"gat", 10},  {"pnr", 11}, {"rtg", 12}, {"cnd", 13}, {"slc", 14}, {"ton", 15},
    {"pat2", chordRow(2, 0)}, {"pat3", chordRow(3, 0)}, {"pat4", chordRow(4, 0)}, {"pat5", chordRow(5, 0)},
    {"pat6", chordRow(6, 0)}, {"oct2", chordRow(2, 1)}, {"oct3", chordRow(3, 1)}, {"oct4", chordRow(4, 1)},
    {"oct5", chordRow(5, 1)}, {"oct6", chordRow(6, 1)}, {"dur2", chordRow(2, 2)}, {"dur3", chordRow(3, 2)},
    {"dur4", chordRow(4, 2)}, {"dur5", chordRow(5, 2)}, {"dur6", chordRow(6, 2)},
}};
constexpr std::array<LaneKey, 22> kStymLaneKeys = {{
    {"ins", 31},  {"cmd", 32},  {"cmdS", 33}, {"cmdR", 34}, {"cmdZ", 35}, {"cmdT", 36}, {"cmdV", 37},
    {"cmdA", 38}, {"cmdP", 39}, {"cmdD", 40}, {"cmdC", 41}, {"cmdB", 42}, {"cmdN", 43}, {"cmdE", 44},
    {"cmdO", 45}, {"cmdF", 46}, {"cmdL", 47}, {"cmdM", 48}, {"cmdJ", 49}, {"cmdG", 50}, {"cmdK", 51},
    {"cmdQ", 52},
}};
static_assert(int(ymseq::Row::Ton) == 15 && int(ymseq::Row::Ins) == 16 && int(ymseq::Row::CmdArp) == 17 &&
              int(ymseq::Row::CmdNoiseCurve) == 37 && kLaneRows == 53);

// Kit: keys of the prototype (KIT) carried by the fields a, b, dec and noise of DrumParams, then vol.
struct KitKeys {
    std::string_view name;
    std::array<std::string_view, 4> keys;  // a, b, dec, noise ("": the family does not have this setting)
};
constexpr std::array<KitKeys, ymengine::kDrumSlots> kKitKeys = {{
    {"kick", {"pitch", "sweep", "dec", "noise"}},
    {"snare", {"body", "tone", "dec", ""}},
    {"clap", {"space", "color", "tail", ""}},
    {"hat", {"metal", "tune", "dec", ""}},
    {"ohat", {"metal", "tune", "dec", ""}},
    {"chat", {"metal", "tune", "dec", ""}},
    {"tom", {"pitch", "sweep", "dec", "noise"}},
    {"tom2", {"pitch", "sweep", "dec", "noise"}},
}};

double* kitField(ymengine::DrumParams& d, int k) {
    switch (k) {
    case 0: return &d.a;
    case 1: return &d.b;
    case 2: return &d.dec;
    default: return &d.noise;
    }
}

// Operators of the prototype: Math.round (half towards +∞), x | 0 (truncation), x ?? d (null or missing).
double jsRound(double v) { return std::floor(v + 0.5); }
int jsInt(double v) { return std::isfinite(v) ? int(std::clamp(std::trunc(v), -2147483648.0, 2147483647.0)) : 0; }
const Value* present(const Value& o, std::string_view key) {
    const Value* v = o.isObject() ? o.find(key) : nullptr;
    return v && !v->isNull() ? v : nullptr;
}
double num(const Value& o, std::string_view key, double fallback) {
    const Value* v = present(o, key);
    return v ? v->number(fallback) : fallback;
}
// JavaScript truthiness: a non-empty string, any array or object, a number other than 0.
bool truthy(const Value& o, std::string_view key) {
    const Value* v = present(o, key);
    if (!v) return false;
    if (v->isString()) return !v->string().empty();
    return v->truthy(v->isArray() || v->isObject());
}
bool isKind(const Value& d, std::string_view kind) {
    const Value* k = d.isObject() ? d.find("kind") : nullptr;
    return k && k->isString() && k->string() == kind;
}

// Pattern that carries STYM2149 rows (INS, CMD, BPM).
bool stymRows(const ymseq::Pattern& p) {
    const auto set = [](auto x) { return x != 0; };
    if (std::any_of(p.bpm.begin(), p.bpm.end(), set)) return true;
    for (const ymseq::LanePtr& lane : p.lanes) {
        if (!lane) continue;
        const ymseq::Lane l = ymseq::withArpFlags(*lane);
        if (std::any_of(l.ins.begin(), l.ins.end(), set)) return true;
        for (const auto& row : l.cmd)
            if (std::any_of(row.begin(), row.end(), set)) return true;
    }
    return false;
}

// Decimal setting rounded to the decimals of its step (120.5 and not the f32 value 120.4999...).
Value stepped(const ParamValues& v, int index) {
    const ParamSpec& s = paramSpec(index);
    const double x = double(v[size_t(index)]);
    if (s.step <= 0 || s.step >= 1) return Value(x);
    // 10 to the power of the decimals of the step: 0.5 → 10, 0.01 → 100.
    const double k = std::pow(10.0, std::ceil(-std::log10(double(s.step))));
    return Value(std::round(x * k) / k);
}

void setParam(ParamValues& v, int index, double x) {
    if (std::isfinite(x)) v[size_t(index)] = clampToSpec(paramSpec(index), float(x));
}

// ---- Rows and motifs ----

Value laneRow(const ymseq::Lane& l, int row) {
    std::vector<double> cells(static_cast<size_t>(ymseq::kSteps));
    for (int s = 0; s < ymseq::kSteps; ++s) cells[size_t(s)] = laneCell(l, row, s);
    return Value::numberArray(std::move(cells));
}

bool rowEmpty(const ymseq::Lane& l, int row) {
    const int e = laneEmpty(row);
    for (int s = 0; s < ymseq::kSteps; ++s)
        if (laneCell(l, row, s) != e) return false;
    return true;
}

// The rows of the prototype are always written; INS and CMD only when they hold something.
Value laneObject(const ymseq::Lane& lane) {
    const ymseq::Lane l = ymseq::withArpFlags(lane);  // the "afl" of the prototype when they say the same (item 144)
    Value o = Value::object();
    for (const LaneKey& k : kLaneKeys) o.set(std::string(k.key), laneRow(l, k.row));
    for (const LaneKey& k : kStymLaneKeys)
        if (!rowEmpty(l, k.row)) o.set(std::string(k.key), laneRow(l, k.row));
    return o;
}

// padLane: the steps beyond the array stay empty; beyond 256, ignored. Value that is not a number: empty.
void readRow(const Value& a, ymseq::Lane& l, int row) {
    const int e = laneEmpty(row);
    auto put = [&](int s, double v) { setLaneCell(l, row, s, std::isfinite(v) ? int(std::lround(v)) : e); };
    if (const std::vector<double>* nums = a.numbers()) {
        for (int s = 0; s < ymseq::kSteps; ++s) put(s, size_t(s) < nums->size() ? (*nums)[size_t(s)] : e);
    } else {
        const std::vector<Value>& items = a.items();
        for (int s = 0; s < ymseq::kSteps; ++s) {
            const Value* v = size_t(s) < items.size() ? &items[size_t(s)] : nullptr;
            put(s, v && v->isNumber() ? v->number() : e);
        }
    }
}

// Rows of a track: those of the file; the others stay those of `l` (lane of the prototype: Object.assign).
void readLane(const Value& chan, ymseq::Lane& l) {
    if (!chan.isObject()) return;
    for (const LaneKey& k : kLaneKeys)
        if (const Value* a = chan.find(k.key); a && a->isArray()) readRow(*a, l, k.row);
    for (const LaneKey& k : kStymLaneKeys)
        if (const Value* a = chan.find(k.key); a && a->isArray()) readRow(*a, l, k.row);
}

Value patternObject(const ymseq::Pattern& p) {
    Value chans = Value::array();
    for (int t = 0; t < ymseq::kTracks; ++t) chans.push(laneObject(p.lane(t)));
    Value o = Value::object();
    o.set("steps", Value(p.steps));
    o.set("chans", std::move(chans));
    if (std::any_of(p.bpm.begin(), p.bpm.end(), [](uint8_t b) { return b != 0; })) {
        Value bpm = Value::array();
        for (uint8_t b : p.bpm) bpm.push(Value(int(b)));
        o.set("bpm", std::move(bpm));
    }
    return o;
}

// Motif (snapPattern); nullopt if it is not one (neither an object nor an array of tracks).
std::optional<ymseq::Pattern> readPattern(const Value& p, int fallbackSteps) {
    const Value* chans = p.isObject() ? p.find("chans") : nullptr;
    if (!chans || !chans->isArray()) return std::nullopt;
    ymseq::Pattern out;
    out.steps = std::clamp(jsInt(num(p, "steps", fallbackSteps)), 1, ymseq::kSteps);
    const std::vector<Value>& items = chans->items();
    for (int t = 0; t < ymseq::kTracks && size_t(t) < items.size(); ++t) {
        ymseq::Lane l;
        readLane(items[size_t(t)], l);
        ymseq::migrateArpFlags(l);
        if (!l.empty()) out.edit(t) = l;
    }
    // BPM row (twelfth DAW test, item 150): 0 or 32 to 255 at each step.
    if (const Value* bpm = present(p, "bpm"); bpm && bpm->isArray())
        for (size_t s = 0; s < bpm->items().size() && s < size_t(ymseq::kSteps); ++s) {
            const Value& v = bpm->items()[s];
            const int b = v.isNumber() ? jsInt(v.number()) : 0;
            out.bpm[s] = uint8_t(b >= ymengine::kTempoCmdMin && b <= 255 ? b : 0);
        }
    out.migrateTempo();
    return out;
}

// ---- Song ----

constexpr std::array<std::string_view, 4> kSongKinds = {"pattern", "rest", "noteoff", "loop"};

Value songArray(const ymseq::Song& song) {
    Value a = Value::array();
    for (const ymseq::SongEntry& e : song.entries) {
        Value o = Value::object();
        o.set("pat", Value(e.pat));
        o.set("rep", Value(e.rep));
        if (e.bpm > 0) o.set("bpm", Value(e.bpm));
        if (e.special()) {
            o.set("kind", Value(std::string(kSongKinds[size_t(e.kind)])));
            o.set("arg", Value(e.arg));
        }
        if (e.restart) o.set("restart", Value(1));
        a.push(std::move(o));
    }
    return a;
}

// Entries: pat | 0, max(1, rep | 0), bpm if given (clamped to 20-300), then the STYM2149 keys.
ymseq::Song readSong(const Value& a) {
    ymseq::Song song;
    if (!a.isArray()) return song;
    for (const Value& x : a.items()) {
        ymseq::SongEntry e;
        e.pat = jsInt(num(x, "pat", 0));
        e.rep = std::max(1, jsInt(num(x, "rep", 0)));
        if (truthy(x, "bpm")) e.bpm = std::clamp(jsInt(num(x, "bpm", 0)), ymseq::kSongBpmMin, ymseq::kSongBpmMax);
        if (const Value* k = present(x, "kind"); k && k->isString())
            for (size_t i = 0; i < kSongKinds.size(); ++i)
                if (k->string() == kSongKinds[i]) e.kind = ymseq::SongKind(i);
        e.arg = jsInt(num(x, "arg", 0));
        e.restart = truthy(x, "restart");
        song.entries.push_back(e);
    }
    return song;
}

// ---- Kit ----

Value kitObject(const ymengine::KitParams& kit) {
    Value o = Value::object();
    for (int k = 0; k < ymengine::kDrumSlots; ++k) {
        ymengine::DrumParams d = kit.inst[size_t(k)];
        Value inst = Value::object();
        for (int f = 0; f < 4; ++f)
            if (!kKitKeys[size_t(k)].keys[size_t(f)].empty())
                inst.set(std::string(kKitKeys[size_t(k)].keys[size_t(f)]), Value(*kitField(d, f)));
        inst.set("vol", Value(d.vol));
        o.set(std::string(kKitKeys[size_t(k)].name), std::move(inst));
    }
    return o;
}

// Old format (hat only, dec in % for all three hi-hats): hat 45 × s, ohat 9 × s, chat 90 × s (loadPresetObj).
Value migrateKit(Value kit) {
    const Value* hat = kit.find("hat");
    if (!hat || !hat->isObject() || present(kit, "ohat")) return kit;
    const double s = num(*hat, "dec", 100) / 100;
    const Value base = *hat;
    for (const auto& [name, dec] : {std::pair<const char*, double>{"hat", 45}, {"ohat", 9}, {"chat", 90}}) {
        Value v = base;
        v.set("dec", Value(jsRound(dec * s)));
        kit.set(name, std::move(v));
    }
    return kit;
}

// for (const k in KIT) if (d.kit[k]) Object.assign(KIT[k], d.kit[k]); bounds of the state.
ymengine::KitParams readKit(const Value& kitIn, const ymengine::KitParams& current) {
    const Value kit = migrateKit(kitIn);
    ymengine::KitParams out = current;
    for (int k = 0; k < ymengine::kDrumSlots; ++k) {
        const Value* inst = present(kit, kKitKeys[size_t(k)].name);
        if (!inst || !inst->isObject()) continue;
        ymengine::DrumParams& d = out.inst[size_t(k)];
        for (int f = 0; f < 4; ++f) {
            const std::string_view key = kKitKeys[size_t(k)].keys[size_t(f)];
            if (!key.empty()) *kitField(d, f) = num(*inst, key, *kitField(d, f));
        }
        d.vol = num(*inst, "vol", d.vol);
    }
    NonParamState s;  // sanitize clamps the kit (non-finite value: that of the factory kit)
    s.kit = out;
    sanitize(s);
    return s.kit;
}

// ---- Preset ----

int signatureIndex(const ymengine::GlobalParams& g) {
    for (size_t i = 0; i < ymseq::kSignatures.size(); ++i)
        if (ymseq::kSignatures[i].numerator == g.sigNum && ymseq::kSignatures[i].denominator == g.sigDen) return int(i);
    return ymseq::kDefaultSignature;
}

Value globObject(const ParamValues& v, const NonParamState& x) {
    const ymengine::GlobalParams& g = x.global;
    // Voice mode 0: 16 channels, 1: Authentic 3ch, 2: Dual chip; the prototype says it with mode3ch and chips.
    const int mode = intValue(v, Global::VoiceMode);
    Value o = Value::object();
    o.set("master", Value(double(intValue(v, Global::Master)) / 100));
    o.set("mode3ch", Value(mode != 0 ? 1 : 0));
    o.set("chips", Value(mode == 2 ? 2 : 1));
    o.set("bpm", stepped(v, paramIndex(Global::FreeTempo)));
    o.set("gate", Value(double(intValue(v, Global::Gate)) / 100));
    o.set("seqOn", Value(0));  // playback belongs to the host and to the Play button: never restarted by a preset
    o.set("steps", Value(x.seq.steps()));
    o.set("digiCh", Value(int(g.digidrumChannel[0])));
    o.set("digiCh2", Value(int(g.digidrumChannel[1])));
    o.set("vbl", Value(int(kVblRates[size_t(std::clamp(intValue(v, Global::VblRate), 0, int(kVblRates.size()) - 1))])));
    o.set("sig", Value(signatureIndex(g)));
    o.set("reso", Value(int(g.resolution)));
    // Keys that the prototype only adds when the Swing slider and the Metro button are first set (GLOB.swing || 50).
    if (intValue(v, Global::Swing) != 50) o.set("swing", Value(intValue(v, Global::Swing)));
    if (g.metronome) o.set("metro", Value(1));
    return o;
}

// A track of the preset: its patch and arpeggio as the Patches library writes them, then the mixer and sequencer
// settings and the track's lane of the current pattern.
Value chanObject(const ParamValues& v, const NonParamState& x, int t) {
    const PatchEntry snap = snapPatch(std::string(), v, x, t);
    Value o = Value::object();
    o.set("patch", *snap.find("patch"));
    o.set("enabled", Value(intValue(v, t, Track::On)));
    o.set("level", Value(double(intValue(v, t, Track::Level)) / 100));
    o.set("name", Value(x.trackNames[size_t(t)]));
    o.set("seqOct", Value(x.seq.tracks[size_t(t)].octave));
    o.set("gate", Value(double(intValue(v, t, Track::Gate)) / 100));
    o.set("mute", Value(intValue(v, t, Track::Mute)));
    o.set("solo", Value(intValue(v, t, Track::Solo)));
    o.set("pan", Value(double(intValue(v, t, Track::Pan)) / 100));
    o.set("plen", Value(x.seq.tracks[size_t(t)].length));
    o.set("arp", *snap.find("arp"));
    o.set("lane", laneObject(x.seq.cur().lane(t)));
    return o;
}

void readGlob(const Value& gl, ParamValues& v, NonParamState& x, int& steps) {
    ymengine::GlobalParams& g = x.global;
    if (const Value* m = present(gl, "master")) setParam(v, paramIndex(Global::Master), jsRound(m->number(0.7) * 100));
    const Value* m3 = present(gl, "mode3ch");
    const Value* ch = present(gl, "chips");
    if (m3 || ch) {
        const int mode = intValue(v, Global::VoiceMode);
        const bool three = m3 ? truthy(gl, "mode3ch") : mode != 0;
        const double chips = ch ? ch->number(1) : (mode == 2 ? 2 : 1);
        setParam(v, paramIndex(Global::VoiceMode), three ? (chips > 1 ? 2 : 1) : 0);
    }
    if (const Value* b = present(gl, "bpm")) setParam(v, paramIndex(Global::FreeTempo), b->number(120));
    if (const Value* gt = present(gl, "gate")) setParam(v, paramIndex(Global::Gate), jsRound(gt->number(0.5) * 100));
    if (const Value* s = present(gl, "steps")) steps = jsInt(s->number(steps));
    if (const Value* d = present(gl, "digiCh")) g.digidrumChannel[0] = uint8_t(std::clamp(jsInt(d->number(2)), 0, 2));
    if (const Value* d = present(gl, "digiCh2")) g.digidrumChannel[1] = uint8_t(std::clamp(jsInt(d->number(2)), 0, 2));
    if (const Value* hz = present(gl, "vbl")) {
        const auto it = std::find(kVblRates.begin(), kVblRates.end(), uint32_t(std::max(0, jsInt(hz->number(0)))));
        if (it != kVblRates.end()) setParam(v, paramIndex(Global::VblRate), double(it - kVblRates.begin()));
    }
    // GLOB.sig and GLOB.reso clamped after reading, like the prototype.
    const int sig = std::clamp(jsInt(num(gl, "sig", signatureIndex(g))), 0, ymseq::kSignatureCount - 1);
    g.sigNum = ymseq::kSignatures[size_t(sig)].numerator;
    g.sigDen = ymseq::kSignatures[size_t(sig)].denominator;
    g.resolution = uint8_t(std::clamp(jsInt(num(gl, "reso", g.resolution)), 0, ymseq::kResolutionCount - 1));
    // A swing of 0 reads as 50 (GLOB.swing || 50).
    if (const Value* sw = present(gl, "swing")) setParam(v, paramIndex(Global::Swing), sw->number(0) != 0 ? sw->number(0) : 50);
    if (present(gl, "metro")) g.metronome = truthy(gl, "metro");
}

// A track of the preset, on top of the current track: patch and arp merged (Object.assign), the rest replaced.
void readChan(const Value& s, int t, ParamValues& v, NonParamState& x, ymseq::Lane& lane) {
    if (!s.isObject()) return;
    const PatchEntry cur = snapPatch(std::string(), v, x, t);
    Value patch = *cur.find("patch");
    if (const Value* p = present(s, "patch"); p && p->isObject())
        for (const auto& [k, val] : p->members()) patch.set(k, val);
    Value arp = *cur.find("arp");
    if (const Value* a = present(s, "arp"); a && a->isObject()) {
        for (const auto& [k, val] : a->members()) arp.set(k, val);
        if (!present(*a, "table")) arp.set("table", Value::numberArray({0}));  // (s.arp.table || [0])
    }
    // The merged patch is applied as a Patches library entry, with the same conversions and clamps.
    Value e = Value::object();
    e.set("name", Value(""));
    e.set("patch", std::move(patch));
    e.set("arp", std::move(arp));
    e.set("level", Value(num(s, "level", 1)));  // s.level ?? 1
    e.set("gate", Value(num(s, "gate", 0.5)));
    const PatchChange c = patchChange(e, x, t);
    for (const auto& [i, val] : c.params) v[size_t(i)] = val;
    x.tracks[size_t(t)] = c.track;
    setParam(v, paramIndex(t, Track::On), truthy(s, "enabled") ? 1 : 0);
    setParam(v, paramIndex(t, Track::Mute), jsInt(num(s, "mute", 0)) != 0 ? 1 : 0);
    setParam(v, paramIndex(t, Track::Solo), jsInt(num(s, "solo", 0)) != 0 ? 1 : 0);
    setParam(v, paramIndex(t, Track::Pan), jsRound(num(s, "pan", 0) * 100));
    x.seq.tracks[size_t(t)].octave = jsInt(num(s, "seqOct", 3));
    x.seq.tracks[size_t(t)].length = jsInt(num(s, "plen", 0));
    const Value* name = present(s, "name");
    x.trackNames[size_t(t)] = name && name->isString() ? name->string() : std::string();
    if (const Value* l = present(s, "lane"); l && l->isObject()) {
        // Missing rows: those of the current track; INS and CMD rows, unknown to the prototype: empty.
        for (const LaneKey& k : kStymLaneKeys)
            for (int st = 0; st < ymseq::kSteps; ++st) setLaneCell(lane, k.row, st, laneEmpty(k.row));
        readLane(*l, lane);
    }
}

// A preset read field by field over the current project (loadPresetObj): the file's "chans" lanes fill the current
// pattern, which replaces the one of "pats" with the same number.
void readPrototypePreset(const Value& d, ParamValues& v, NonParamState& x) {
    ymseq::Project& p = x.seq;
    int steps = p.steps();
    std::array<ymseq::Lane, ymseq::kTracks> lanes;  // CH[].lane: tracks of the current pattern
    for (int t = 0; t < ymseq::kTracks; ++t) lanes[size_t(t)] = p.cur().lane(t);

    if (const Value* gl = present(d, "glob"); gl && gl->isObject()) readGlob(*gl, v, x, steps);
    if (const Value* kit = present(d, "kit"); kit && kit->isObject()) x.kit = readKit(*kit, x.kit);
    if (const Value* chans = present(d, "chans"); chans && chans->isArray())
        for (size_t t = 0; t < chans->items().size() && t < size_t(ymseq::kTracks); ++t)
            readChan(chans->items()[t], int(t), v, x, lanes[t]);
    if (const Value* pats = present(d, "pats"); pats && pats->isArray()) {
        for (size_t i = 0; i < pats->items().size() && i < size_t(ymseq::kPatterns); ++i) {
            const std::optional<ymseq::Pattern> pat = readPattern(pats->items()[i], steps);
            p.patterns[i] = pat ? *pat : ymseq::Pattern{};
            p.used.set(i, pat.has_value());
        }
    }
    p.current = std::clamp(jsInt(num(d, "curPat", 0)), 0, ymseq::kPatterns - 1);
    p.pending = -1;
    p.viewBank = p.current / ymseq::kBankSize;
    // The current pattern is that of the tracks (CH[].lane) and of GLOB.steps, with the BPM row of its "pats" entry;
    // its tracks are migrated like those of readPattern (ARP flags into Jxx, Txx into the BPM row).
    ymseq::Pattern& cur = p.cur();
    const auto bpm = cur.bpm;
    cur = ymseq::Pattern{};
    cur.steps = steps;
    cur.bpm = bpm;
    for (int t = 0; t < ymseq::kTracks; ++t) {
        ymseq::Lane l = lanes[size_t(t)];
        ymseq::migrateArpFlags(l);
        if (!l.empty()) cur.edit(t) = l;
    }
    cur.migrateTempo();
    p.used.set(size_t(p.current));
    const Value* song = present(d, "song");
    p.song = readSong(song ? *song : Value::array());
    x.global.songMode = truthy(d, "songOn");
    sanitize(x);
}

}  // namespace

std::string_view defaultFileName(FileKind k) noexcept {
    switch (k) {
    case FileKind::Preset: return "stym2149-preset.json";
    case FileKind::Pattern: return "stym2149-pattern.json";
    case FileKind::Song: return "stym2149-song.json";
    case FileKind::Kit: return "stym2149-kit.json";
    case FileKind::Patches: return "stym2149-patches.json";
    }
    return "stym2149.json";
}

std::string ensureExtension(std::string_view name, std::initializer_list<std::string_view> extensions) {
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.remove_prefix(1);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.remove_suffix(1);
    if (name.empty()) return {};
    std::string n(name);
    for (std::string_view e : extensions) {
        if (n.size() < e.size()) continue;
        std::string tail = n.substr(n.size() - e.size());
        for (char& c : tail) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (tail == e) return n;
    }
    return extensions.size() ? n + std::string(*extensions.begin()) : n;
}

std::string ensureJson(std::string_view name) { return ensureExtension(name, {".json"}); }

std::string dumpPreset(const ParamValues& values, const NonParamState& extras) {
    Value d = Value::object();
    d.set("app", Value(std::string(kApp)));
    d.set("v", Value(1));
    d.set("glob", globObject(values, extras));
    d.set("curPat", Value(extras.seq.current));
    Value chans = Value::array();
    for (int t = 0; t < kTracks; ++t) chans.push(chanObject(values, extras, t));
    d.set("chans", std::move(chans));
    Value pats = Value::array();
    for (int i = 0; i < ymseq::kPatterns; ++i)
        pats.push(extras.seq.used[size_t(i)] ? patternObject(extras.seq.patterns[size_t(i)]) : Value(nullptr));
    d.set("pats", std::move(pats));
    d.set("song", songArray(extras.seq.song));
    d.set("songOn", Value(extras.global.songMode ? 1 : 0));
    d.set("kit", kitObject(extras.kit));
    // The complete state, which wins when the file is read back; no recorder take (PluginState::take stays empty).
    PluginState st;
    st.values = values;
    st.extras = extras;
    Value stym = Value::object();
    stym.set("v", Value(kStymFileVersion));
    stym.set("state", Value(base64Encode(encodeState(st))));
    d.set("stym", std::move(stym));
    return json::dump(d);
}

PresetRead parsePreset(std::string_view text, const ParamValues& values, const NonParamState& extras) {
    PresetRead r;
    const std::optional<Value> d = json::parse(text);
    if (!d) return r;  // "Preset read error."
    const Value* app = d->isObject() ? d->find("app") : nullptr;
    if (!app || !app->isString() || app->string() != kApp) {
        r.status = PresetRead::Status::NotPreset;  // "Not a STYM2149 preset."
        return r;
    }
    r.status = PresetRead::Status::Ok;
    if (const Value* stym = present(*d, "stym"); stym && stym->isObject()) {
        const Value* state = present(*stym, "state");
        if (num(*stym, "v", 0) > kStymFileVersion) {
            r.newer = true;
        } else if (state && state->isString()) {
            const std::vector<uint8_t> bytes = base64Decode(state->string());
            DecodeResult dec = decodeState(bytes);
            if (dec.ok) {
                r.values = dec.state.values;
                r.extras = std::move(dec.state.extras);
                r.exact = true;
                return r;
            }
            r.newer = dec.version > kStateVersion;
        }
    }
    // No usable "stym" state: the fields of the prototype, on top of the current project.
    r.values = values;
    r.extras = extras;
    readPrototypePreset(*d, r.values, r.extras);
    return r;
}

void markStymFile(Value& doc) {
    Value stym = Value::object();
    stym.set("v", Value(kStymFileVersion));
    doc.set("stym", std::move(stym));
}

bool newerStymFile(const Value& doc) noexcept {
    const Value* stym = present(doc, "stym");
    return stym && stym->isObject() && num(*stym, "v", 0) > kStymFileVersion;
}

std::string dumpPatternFile(const ymseq::Pattern& p) {
    Value d = Value::object();
    d.set("app", Value(std::string(kApp)));
    d.set("kind", Value("pattern"));
    d.set("v", Value(1));
    d.set("pattern", patternObject(p));
    if (stymRows(p)) markStymFile(d);
    return json::dump(d);
}

std::optional<ymseq::Pattern> parsePatternFile(std::string_view text, bool* newer) {
    const std::optional<Value> d = json::parse(text);
    if (!d || !isKind(*d, "pattern")) return std::nullopt;  // "Pattern read error."
    const Value* p = present(*d, "pattern");
    if (!p) return std::nullopt;
    if (newer) *newer = newerStymFile(*d);
    return readPattern(*p, ymseq::kDefaultSteps);
}

std::string dumpSongFile(const ymseq::Project& p, bool songOn) {
    Value d = Value::object();
    d.set("app", Value(std::string(kApp)));
    d.set("kind", Value("song"));
    d.set("v", Value(1));
    d.set("song", songArray(p.song));
    d.set("songOn", Value(songOn ? 1 : 0));
    Value pats = Value::array();
    for (int i = 0; i < ymseq::kPatterns; ++i)
        pats.push(p.used[size_t(i)] ? patternObject(p.patterns[size_t(i)]) : Value(nullptr));
    d.set("pats", std::move(pats));
    bool ext = std::any_of(p.song.entries.begin(), p.song.entries.end(),
                           [](const ymseq::SongEntry& e) { return e.special() || e.restart; });
    for (int i = 0; i < ymseq::kPatterns && !ext; ++i) ext = p.used[size_t(i)] && stymRows(p.patterns[size_t(i)]);
    if (ext) markStymFile(d);
    return json::dump(d);
}

std::optional<SongFile> parseSongFile(std::string_view text) {
    const std::optional<Value> d = json::parse(text);
    if (!d || !isKind(*d, "song")) return std::nullopt;  // "Song read error."
    const Value* song = present(*d, "song");
    if (!song || !song->isArray()) return std::nullopt;
    SongFile f;
    f.newer = newerStymFile(*d);
    f.song = readSong(*song);
    f.songOn = truthy(*d, "songOn");
    if (const Value* pats = present(*d, "pats"); pats && pats->isArray()) {
        f.hasPatterns = true;
        for (size_t i = 0; i < pats->items().size() && i < size_t(ymseq::kPatterns); ++i)
            f.patterns[i] = readPattern(pats->items()[i], ymseq::kDefaultSteps);
    }
    return f;
}

void applySongFile(const SongFile& f, ymseq::Project& p, bool& songOn) {
    p.song = f.song;
    songOn = f.songOn;
    if (!f.hasPatterns) return;
    const int steps = p.steps();
    for (int i = 0; i < ymseq::kPatterns; ++i) {
        const std::optional<ymseq::Pattern>& pat = f.patterns[size_t(i)];
        p.patterns[size_t(i)] = pat ? *pat : ymseq::Pattern{};
        p.used.set(size_t(i), pat.has_value());
    }
    // loadPattern(PATS[curPat] || emptyPattern()): a missing pattern becomes empty, of the current length.
    if (!f.patterns[size_t(p.current)]) p.cur().steps = steps;
    p.used.set(size_t(p.current));
    // A pending switch to a pattern that no longer exists is dropped.
    if (p.pending >= 0 && !p.used[size_t(p.pending)]) p.pending = -1;
}

std::string dumpKitFile(const ymengine::KitParams& kit) {
    Value d = Value::object();
    d.set("kind", Value("drumkit"));
    d.set("kit", kitObject(kit));
    return json::dump(d, 1);
}

std::optional<ymengine::KitParams> parseKitFile(std::string_view text, const ymengine::KitParams& current,
                                               bool* newer) {
    const std::optional<Value> d = json::parse(text);
    if (!d || !isKind(*d, "drumkit")) return std::nullopt;  // "Not a valid drum kit file."
    const Value* kit = present(*d, "kit");
    if (!kit || !kit->isObject()) return std::nullopt;
    if (newer) *newer = newerStymFile(*d);
    return readKit(*kit, current);
}

}  // namespace stym::plugin
