// STYM2149 — plugincore: Patches library (libSnap, libApply, file)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/Patches.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "plugincore/Mapping.h"
#include "plugincore/ProjectFiles.h"

namespace stym::plugin {

using json::Value;

namespace {

// Default values of a new track of the prototype (mkCh), for the keys missing from an entry.
constexpr double kDefEp = 400;  // EP of the prototype (× 32 for the chip, D-021)

double num(const Value& o, std::string_view key, double fallback) {
    const Value* v = o.find(key);
    return v ? v->number(fallback) : fallback;
}

int flag(bool b) { return b ? 1 : 0; }

template <class Array>
Value list(const Array& a) {
    Value out = Value::array();
    for (const auto& x : a) out.push(Value(double(x)));
    return out;
}

// Array of integers: completed with `fill` beyond its length (ornNorm, arpeggio table). Values are rounded and
// clamped to the element type; a non-finite one becomes `fill`.
template <class Array>
void readList(const Value& o, std::string_view key, Array& out, int fill) {
    const Value* v = o.find(key);
    const std::vector<Value> none;
    const std::vector<Value>& items = v && v->isArray() ? v->items() : none;
    using T = typename Array::value_type;
    for (size_t i = 0; i < out.size(); ++i) {
        const double x = i < items.size() ? items[i].number(fill) : fill;
        out[i] = std::isfinite(x) ? T(std::clamp<double>(std::round(x), std::numeric_limits<T>::min(),
                                                         std::numeric_limits<T>::max()))
                                  : T(fill);
    }
}

// Instrument sequences (D-148), keys specific to STYM2149: written only when not default, so that a patch without
// sequences stays the prototype's.
template <class Array>
bool allZero(const Array& a) {
    return std::all_of(a.begin(), a.end(), [](auto x) { return x == 0; });
}

void putSequences(Value& p, const ymengine::TrackPatch& t) {
    const ymengine::TrackPatch def;
    if (!allZero(t.rows.period)) p.set("seqPer", list(t.rows.period));
    if (!allZero(t.rows.noise)) p.set("seqNP", list(t.rows.noise));
    if (!allZero(t.rows.mix)) p.set("seqMix", list(t.rows.mix));
    if (!allZero(t.rows.gen)) p.set("seqGen", list(t.rows.gen));
    if (!allZero(t.rows.fix)) p.set("seqFix", list(t.rows.fix));
    if (t.fixedNote != def.fixedNote) p.set("fixNote", Value(int(t.fixedNote)));
    if (t.detuneCents != 0) p.set("detune", Value(t.detuneCents));
    if (t.analogCents != 0) p.set("analog", Value(t.analogCents));
    if (t.hardSync) p.set("hardSync", Value(1));
    if (t.link) p.set("link", Value(int(t.link)));
    if (t.pitchMatrix != def.pitchMatrix) p.set("pitchMatrix", Value(int(t.pitchMatrix)));
    if (!allZero(t.sidWave)) p.set("sidWave", list(t.sidWave));  // timer waves (B6, D-151)
    if (t.sidWaveLen != def.sidWaveLen) p.set("sidWaveLen", Value(int(t.sidWaveLen)));
    if (t.sidWavePhase != def.sidWavePhase) p.set("sidWavePhase", Value(int(t.sidWavePhase)));     // D-405
    if (t.sidWavePeriod != def.sidWavePeriod) p.set("sidWavePeriod", Value(int(t.sidWavePeriod)));  // D-406
    if (t.sidWidth != def.sidWidth) p.set("sidWidth", Value(int(t.sidWidth)));  // width of the Square (D-408)
    // The form of the SID wave (D-411), always beside the keys of the wave: without it, those keys are read as the
    // patches written before it (getSequences).
    if (t.sidForm != def.sidForm || p.find("sidWave") || p.find("sidWaveLen") || p.find("sidWidth"))
        p.set("sidForm", Value(int(t.sidForm)));
    if (!allZero(t.buzzShapes)) p.set("buzzSteps", list(t.buzzShapes));
    if (t.buzzLen) p.set("buzzLen", Value(int(t.buzzLen)));
    if (t.envTuned) p.set("envTuned", Value(1));  // buzzer (B7, D-152)
    if (t.envRatioN != def.envRatioN) p.set("envN", Value(int(t.envRatioN)));
    if (t.envRatioD != def.envRatioD) p.set("envD", Value(int(t.envRatioD)));
    if (t.envFineCents != 0) p.set("envFine", Value(t.envFineCents));
    if (t.ymPad) p.set("ymPad", Value(1));
    if (t.lfo != def.lfo) {  // LFO (D-257)
        Value l = Value::object();
        l.set("on", Value(flag(t.lfo.on)));
        l.set("shape", Value(int(t.lfo.shape)));
        l.set("to", Value(int(t.lfo.target)));
        l.set("sync", Value(flag(t.lfo.sync)));
        l.set("hz", Value(t.lfo.hz));
        l.set("div", Value(int(t.lfo.division)));
        l.set("depth", Value(t.lfo.depth));
        l.set("retrig", Value(flag(t.lfo.retrig)));  // D-279
        p.set("lfo", std::move(l));
    }
    if (t.echo != def.echo) {  // echo of the notes (D-299)
        Value x = Value::object();
        x.set("on", Value(flag(t.echo.on)));
        x.set("div", Value(int(t.echo.division)));
        x.set("repeats", Value(int(t.echo.repeats)));
        x.set("drop", Value(int(t.echo.drop)));
        x.set("to", Value(int(t.echo.target)));
        p.set("echo", std::move(x));
    }
}

// Reverse of putSequences: a missing key gives the default, so a patch of the prototype loads without sequences.
void getSequences(const Value& p, ymengine::TrackPatch& t) {
    const ymengine::TrackPatch def;
    readList(p, "seqPer", t.rows.period, 0);
    readList(p, "seqNP", t.rows.noise, 0);
    readList(p, "seqMix", t.rows.mix, 0);
    readList(p, "seqGen", t.rows.gen, 0);
    readList(p, "seqFix", t.rows.fix, 0);
    auto finite = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
    t.fixedNote = uint8_t(std::clamp(finite(std::round(num(p, "fixNote", def.fixedNote)), def.fixedNote), 0.0, 127.0));
    t.detuneCents = finite(num(p, "detune", 0), 0);
    t.analogCents = finite(num(p, "analog", 0), 0);
    t.hardSync = num(p, "hardSync", 0) != 0;
    t.link = uint8_t(std::clamp(finite(std::round(num(p, "link", 0)), 0), 0.0, 255.0));
    t.pitchMatrix = uint16_t(std::clamp(finite(std::round(num(p, "pitchMatrix", def.pitchMatrix)), def.pitchMatrix),
                                        0.0, 65535.0));
    readList(p, "sidWave", t.sidWave, 0);
    readList(p, "buzzSteps", t.buzzShapes, 0);
    auto count = [&](std::string_view key, double fallback = 0) {
        return uint8_t(std::clamp(finite(std::round(num(p, key, fallback)), fallback), 0.0, 255.0));
    };
    // The form of the SID wave (D-411). A patch without it was written before: Steps 0 or 1 (or none) was the plain
    // square, whose width counted sixteenths of its period (D-408), 8 being half and half; Steps 2 or more played the
    // bars, a Sine when sidWaveSine was lit (D-402), one cycle over the 16 bars without its period (D-406), drawn on
    // the chip's levels: the Bounce now (D-414). As in a project (State.cpp), another width becomes the Square of 16
    // Steps, the same sound.
    using ymengine::SidForm;
    const bool form = p.find("sidForm");
    const bool sine = !form && num(p, "sidWaveSine", 0) != 0;
    t.sidWaveLen = count("sidWaveLen", form ? def.sidWaveLen : 0);
    t.buzzLen = count("buzzLen");
    t.sidWavePhase = uint8_t(std::clamp(finite(std::round(num(p, "sidWavePhase", def.sidWavePhase)), def.sidWavePhase),
                                        0.0, double(ymengine::kSidWaveSteps - 1)));
    const double period = sine ? ymengine::kSidWaveSteps : def.sidWavePeriod;
    t.sidWavePeriod = uint8_t(std::clamp(finite(std::round(num(p, "sidWavePeriod", period)), period),
                                         double(ymengine::kSidWavePeriodMin), double(ymengine::kSidWaveSteps)));
    t.sidWidth = uint8_t(std::clamp(finite(std::round(num(p, "sidWidth", def.sidWidth)), def.sidWidth), 1.0,
                                    double(ymengine::kSidWidthMax)));
    if (form)
        t.sidForm = ymengine::sidFormOf(count("sidForm"));
    else {
        if (t.sidWaveLen >= 2) t.sidForm = sine ? SidForm::Bounce : SidForm::Custom;
        else if (p.find("sidWidth") && t.sidWidth != 2 * ymengine::kSidWidth) {
            t.sidForm = SidForm::Square;
            t.sidWaveLen = uint8_t(ymengine::kSidWaveSteps);
        } else t.sidForm = SidForm::Base;
        if (t.sidForm != SidForm::Square) t.sidWidth = def.sidWidth;
    }
    t.sidWaveLen = uint8_t(t.sidWaveLen < 2 ? ymengine::kSidSquareSteps
                                            : std::min<int>(t.sidWaveLen, ymengine::kSidWaveSteps));
    t.envTuned = num(p, "envTuned", 0) != 0;
    t.envRatioN = uint8_t(std::clamp(finite(std::round(num(p, "envN", 1)), 1), 1.0, 255.0));
    t.envRatioD = uint8_t(std::clamp(finite(std::round(num(p, "envD", 1)), 1), 1.0, 255.0));
    t.envFineCents = finite(num(p, "envFine", 0), 0);
    t.ymPad = num(p, "ymPad", 0) != 0;
    t.lfo = def.lfo;  // LFO (D-257); clamped with the state (sanitize)
    if (const Value* l = p.find("lfo"); l && l->isObject()) {
        auto index = [&](std::string_view key, double fallback, int n) {
            return std::clamp(finite(std::round(num(*l, key, fallback)), fallback), 0.0, double(n - 1));
        };
        t.lfo.on = num(*l, "on", 0) != 0;
        t.lfo.shape = ymengine::LfoShape(index("shape", 0, ymengine::kLfoShapes));
        t.lfo.target = ymengine::LfoTarget(index("to", 0, ymengine::kLfoTargets));
        t.lfo.sync = num(*l, "sync", 0) != 0;
        t.lfo.hz = finite(num(*l, "hz", def.lfo.hz), def.lfo.hz);
        t.lfo.division = uint8_t(index("div", def.lfo.division, ymengine::kLfoDivisions));
        t.lfo.depth = finite(num(*l, "depth", def.lfo.depth), def.lfo.depth);
        t.lfo.retrig = num(*l, "retrig", 1) != 0;
    }
    t.echo = def.echo;  // echo of the notes (D-299); clamped with the state (sanitize)
    if (const Value* x = p.find("echo"); x && x->isObject()) {
        auto whole = [&](std::string_view key, double fallback, double lo, double hi) {
            return std::clamp(finite(std::round(num(*x, key, fallback)), fallback), lo, hi);
        };
        t.echo.on = num(*x, "on", 0) != 0;
        t.echo.division = uint8_t(whole("div", def.echo.division, 0, ymengine::kDelayDivisions - 1));
        t.echo.repeats = uint8_t(whole("repeats", def.echo.repeats, 1, ymengine::kEchoRepeatsMax));
        t.echo.drop = uint8_t(whole("drop", def.echo.drop, 0, ymengine::kEchoDropMax));
        t.echo.target = int8_t(whole("to", -1, -1, ymengine::kTracks - 1));
    }
}

// Object member of an entry, or an empty object, so that every key then reads its default.
const Value& member(const Value& e, std::string_view key) {
    static const Value empty = Value::object();
    const Value* v = e.find(key);
    return v && v->isObject() ? *v : empty;
}

}  // namespace

bool inPatch(Track f) noexcept {
    return f != Track::On && !mixerOnly(f) && f != Track::Count;
}

PatchEntry snapPatch(const std::string& name, const ParamValues& v, const NonParamState& state, int track) {
    const ymengine::TrackParams& x = state.tracks[size_t(track)];
    auto iv = [&](Track f) { return Value(intValue(v, track, f)); };
    // Decimal setting: rounded to the decimals of its step (2.37 and not the f32 value 2.3699998...). k = 10 to the
    // number of decimals of the step: 100 for 0.01, 10 for 0.5.
    auto fv = [&](Track f) {
        const ParamSpec& s = paramSpec(paramIndex(track, f));
        const double k = std::pow(10.0, std::ceil(-std::log10(double(s.step))));
        return Value(std::round(double(value(v, track, f)) * k) / k);
    };
    // The keys of the prototype's patch object; the STYM2149 keys (dmaSlot, vibOn, putSequences) only when they differ
    // from the default, so that a plain patch stays the prototype's.
    Value p = Value::object();
    p.set("toneEn", iv(Track::Tone));
    p.set("noiseEn", iv(Track::Noise));
    p.set("mono", iv(Track::Voicing));
    p.set("NP", iv(Track::NoisePeriod));
    p.set("volMode", iv(Track::VolMode));
    p.set("vol4", iv(Track::Volume));
    p.set("EP", Value(double(intValue(v, track, Track::EnvPeriod)) / 32));
    p.set("shape", iv(Track::EnvShape));
    p.set("effect", iv(Track::Generator));
    p.set("sidRatio", fv(Track::SidRatio));
    p.set("sidVmin", iv(Track::SidVmin));
    p.set("slot", Value(int(x.patch.digidrumSlot)));
    if (x.patch.dmaSlot >= 0) p.set("dmaSlot", Value(int(x.patch.dmaSlot)));  // M6: DMA sample (D-153)
    p.set("vibDepth", iv(Track::VibDepth));
    p.set("vibRate", fv(Track::VibRate));
    // Vibrato Off (D-254); On: the patch of the prototype.
    if (intValue(v, track, Track::VibOn) == 0) p.set("vibOn", Value(0));
    p.set("ornOn", Value(flag(x.patch.ornamentOn)));
    p.set("ornLen", Value(int(x.patch.ornamentLength)));
    p.set("ornLoop", Value(flag(x.patch.ornamentLoop)));
    p.set("ornRate", Value(int(x.patch.ornamentRate)));
    p.set("ornTable", list(x.patch.ornamentPitch));
    p.set("ornNse", list(x.patch.ornamentNoise));
    p.set("ornVol", list(x.patch.ornamentVolume));
    p.set("atk", iv(Track::Attack));
    p.set("dcy", iv(Track::Decay));
    p.set("sus", iv(Track::Sustain));
    p.set("rel", iv(Track::Release));
    p.set("adsrChip", iv(Track::AdsrMode));
    putSequences(p, x.patch);

    Value a = Value::object();
    a.set("on", iv(Track::ArpOn));
    a.set("rate", iv(Track::ArpRate));
    a.set("track", Value(flag(x.arp.track)));
    a.set("land", Value(flag(x.arp.land)));
    a.set("mode", Value(int(x.arp.mode)));
    a.set("oct", Value(int(x.arp.octaves)));
    a.set("table", list(x.arp.table));
    a.set("tlen", Value(int(x.arp.length)));

    Value e = Value::object();
    e.set("name", Value(name));
    e.set("patch", std::move(p));
    e.set("arp", std::move(a));
    e.set("level", Value(double(value(v, track, Track::Level)) / 100));
    e.set("gate", Value(double(value(v, track, Track::Gate)) / 100));
    return e;
}

std::string patchName(const PatchEntry& e) {
    const Value* n = e.find("name");
    return n && n->isString() ? n->string() : std::string();
}

void renamePatch(PatchEntry& e, const std::string& name) { e.set("name", Value(name)); }

// libApply: the defaults below are those of a new track of the prototype (mkCh), for the keys an entry lacks. The
// non-automatable settings start from the track's current ones (fields that a patch does not carry stay).
PatchChange patchChange(const PatchEntry& e, const NonParamState& state, int track) {
    PatchChange c;
    c.track = state.tracks[size_t(track)];
    const Value& p = member(e, "patch");
    const Value& a = member(e, "arp");
    auto set = [&](Track f, double v) {
        const int i = paramIndex(track, f);
        c.params.emplace_back(i, clampToSpec(paramSpec(i), float(v)));
    };
    set(Track::Tone, num(p, "toneEn", 1));
    set(Track::Noise, num(p, "noiseEn", 0));
    set(Track::Voicing, num(p, "mono", 0));
    set(Track::NoisePeriod, num(p, "NP", 8));
    set(Track::VolMode, num(p, "volMode", 0));
    set(Track::Volume, num(p, "vol4", 13));
    set(Track::EnvPeriod, std::round(num(p, "EP", kDefEp) * 32));  // D-021: capped at 65535 by its range
    set(Track::EnvShape, num(p, "shape", 8));
    set(Track::Generator, num(p, "effect", 0));
    set(Track::SidRatio, num(p, "sidRatio", 1));
    set(Track::SidVmin, num(p, "sidVmin", 0));
    set(Track::VibDepth, num(p, "vibDepth", 0));
    set(Track::VibRate, num(p, "vibRate", 6));
    set(Track::VibOn, num(p, "vibOn", 1));  // vibrato On before D-254
    set(Track::Attack, num(p, "atk", 0));
    set(Track::Decay, num(p, "dcy", 0));
    set(Track::Sustain, num(p, "sus", 100));
    set(Track::Release, num(p, "rel", 0));
    set(Track::AdsrMode, num(p, "adsrChip", 0));
    set(Track::ArpOn, num(a, "on", 0));
    set(Track::ArpRate, num(a, "rate", 50));
    set(Track::Level, num(e, "level", 0.75) * 100);  // entry.level ?? 0.75
    set(Track::Gate, num(e, "gate", 0.5) * 100);     // entry.gate ?? 0.5

    auto toInt = [](double v, int fallback) { return std::isfinite(v) ? int(std::clamp(std::round(v), 0.0, 255.0)) : fallback; };
    ymengine::TrackPatch& t = c.track.patch;
    t.digidrumSlot = uint8_t(toInt(num(p, "slot", 0), 0));
    const double dmaSlot = num(p, "dmaSlot", -1);
    t.dmaSlot = int8_t(std::isfinite(dmaSlot) ? std::clamp(std::round(dmaSlot), -1.0, double(ymengine::kDmaSlots - 1)) : -1);
    t.ornamentOn = num(p, "ornOn", 0) != 0;
    t.ornamentLength = uint8_t(std::max(1, toInt(num(p, "ornLen", 8), 8)));
    t.ornamentLoop = num(p, "ornLoop", 1) != 0;
    t.ornamentRate = uint8_t(std::max(1, toInt(num(p, "ornRate", 1), 1)));
    readList(p, "ornTable", t.ornamentPitch, 0);
    readList(p, "ornNse", t.ornamentNoise, 0);
    readList(p, "ornVol", t.ornamentVolume, 15);
    getSequences(p, t);
    ymengine::TrackArp& r = c.track.arp;
    r.track = num(a, "track", 0) != 0;
    r.land = num(a, "land", 0) != 0;
    r.mode = ymengine::ArpMode(toInt(num(a, "mode", 0), 0));
    r.octaves = uint8_t(std::max(1, toInt(num(a, "oct", 1), 1)));
    if (a.find("table")) {
        readList(a, "table", r.table, 0);  // table of the prototype padded with zeros up to 16
    } else {
        r.table = ymengine::TrackArp{}.table;
    }
    r.length = uint8_t(std::max(1, toInt(num(a, "tlen", 3), 3)));
    sanitize(c.track);
    return c;
}

std::optional<std::vector<PatchEntry>> parsePatchLibrary(std::string_view text, std::string* error, bool* newer) {
    const std::optional<Value> v = json::parse(text, error);
    if (!v) return std::nullopt;
    if (newer) *newer = newerStymFile(*v);
    const Value* list = nullptr;
    if (v->isArray()) {
        list = &*v;
    } else if (v->isObject()) {
        const Value* kind = v->find("kind");
        const Value* patches = v->find("patches");
        if (kind && kind->isString() && kind->string() == "patchlib" && patches && patches->isArray()) list = patches;
    }
    if (!list) {
        if (error) *error = "not a patch library";
        return std::nullopt;
    }
    // Entries are kept whole, unknown keys included, so that saving the library writes them back unchanged.
    std::vector<PatchEntry> out;
    for (const Value& e : list->items()) {
        const Value* name = e.isObject() ? e.find("name") : nullptr;
        const Value* patch = e.isObject() ? e.find("patch") : nullptr;
        if (name && name->isString() && patch && patch->isObject()) out.push_back(e);
    }
    return out;
}

std::string dumpPatchLibrary(const std::vector<PatchEntry>& patches) {
    Value list = Value::array();
    for (const PatchEntry& e : patches) list.push(e);
    Value doc = Value::object();
    doc.set("kind", Value("patchlib"));
    doc.set("patches", std::move(list));
    // Keys that STYM2149 adds to the patch of the prototype (M6: sequences, timer waves, buzzer, DMA; vibrato Off;
    // LFO; echo).
    static constexpr std::array<std::string_view, 29> kStymKeys = {
        "seqPer", "seqNP", "seqMix", "seqGen", "seqFix", "fixNote", "detune", "analog", "hardSync", "link",
        "pitchMatrix", "sidWave", "sidWaveLen", "sidWaveSine", "sidWavePhase", "sidWavePeriod", "sidWidth",
        "sidForm", "buzzSteps", "buzzLen", "envTuned", "envN", "envD", "envFine", "ymPad", "dmaSlot", "vibOn", "lfo",
        "echo"};
    const bool ext = std::any_of(patches.begin(), patches.end(), [](const PatchEntry& e) {
        const Value* p = e.isObject() ? e.find("patch") : nullptr;
        return p && p->isObject() &&
               std::any_of(kStymKeys.begin(), kStymKeys.end(), [p](std::string_view k) { return p->find(k); });
    });
    if (ext) markStymFile(doc);
    return json::dump(doc, 1);
}

}  // namespace stym::plugin
