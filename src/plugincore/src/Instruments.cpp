// STYM2149 — plugincore: project instruments (named track sound, exchange with the Patches library)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/Instruments.h"

#include "plugincore/Mapping.h"

namespace stym::plugin {

namespace {

// Values of the parameters of track `t` carried by an instrument.
void putValues(ParamValues& v, int t, const Instrument& ins) {
    const ymengine::TrackPatch& p = ins.patch;
    auto set = [&](Track f, double x) {
        const int i = paramIndex(t, f);
        v[size_t(i)] = clampToSpec(paramSpec(i), float(x));
    };
    set(Track::Tone, p.toneOn ? 1 : 0);
    set(Track::Noise, p.noiseOn ? 1 : 0);
    set(Track::Voicing, p.mono ? 1 : 0);
    set(Track::NoisePeriod, p.noisePeriod);
    set(Track::VolMode, p.envelopeVolume ? 1 : 0);
    set(Track::Volume, p.volume);
    set(Track::EnvPeriod, p.envPeriod);
    set(Track::EnvShape, p.envShape);
    set(Track::Generator, int(p.generator));
    set(Track::SidRatio, p.sidRatio);
    set(Track::SidVmin, p.sidMinVolume);
    set(Track::VibDepth, p.vibratoCents);
    set(Track::VibRate, p.vibratoHz);
    set(Track::VibOn, p.vibratoOn ? 1 : 0);
    set(Track::Attack, p.attackMs);
    set(Track::Decay, p.decayMs);
    set(Track::Sustain, p.sustainPct);
    set(Track::Release, p.releaseMs);
    set(Track::AdsrMode, p.adsrChip ? 1 : 0);
    set(Track::ArpOn, ins.arp.on ? 1 : 0);
    set(Track::ArpRate, ins.arp.rateHz);
    set(Track::Level, ins.level * 100);
    set(Track::Gate, ins.gate * 100);
}

}  // namespace

std::string cleanInstrumentName(std::string_view name) {
    std::string out;
    for (char c : name)
        if (c >= 0x20 && c <= 0x7e) out += c;
    const size_t a = out.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    out = out.substr(a, out.find_last_not_of(' ') - a + 1);
    if (out.size() > kInstrumentNameMax) out.resize(kInstrumentNameMax);
    // The cut may end on a space: trimmed again.
    const size_t b = out.find_last_not_of(' ');
    out.resize(b + 1);
    return out;
}

std::string instrumentLabel(const Instrument& ins, int slot) {
    return ins.name.empty() ? "Instrument " + std::to_string(slot + 1) : ins.name;
}

Instrument captureInstrument(const std::string& name, const ParamValues& values, const NonParamState& state, int track) {
    return instrumentFromEntry(snapPatch(name, values, state, track));
}

// The instrument is put on track 0 of a default project and taken with snapPatch, so that its entry is exactly what
// the Patches library writes for a track.
PatchEntry instrumentEntry(const Instrument& ins) {
    ParamValues v = defaultValues();
    putValues(v, 0, ins);
    NonParamState s;
    s.tracks[0].patch = ins.patch;
    s.tracks[0].arp = ins.arp;
    return snapPatch(ins.name, v, s, 0);
}

// The reverse: the entry loaded on track 0 of a default project (libApply), then read back as engine settings
// (toTrack), in a mode and machine where every generator is allowed.
Instrument instrumentFromEntry(const PatchEntry& e) {
    static const NonParamState kFresh;  // new tracks (mkCh)
    const PatchChange c = patchChange(e, kFresh, 0);
    ParamValues v = defaultValues();  // 16 channels: the generator stays the entry's
    for (const auto& [i, x] : c.params) v[size_t(i)] = x;
    v[size_t(paramIndex(Global::Computer))] = 1;  // STE machine: the DMA type stays too
    const ymengine::TrackParams t = toTrack(v, 0, c.track);
    Instrument ins;
    ins.used = true;
    ins.name = cleanInstrumentName(patchName(e));
    ins.patch = t.patch;
    ins.arp = t.arp;
    ins.level = double(value(v, 0, Track::Level)) / 100;
    ins.gate = double(value(v, 0, Track::Gate)) / 100;
    return ins;
}

PatchChange instrumentChange(const Instrument& ins, const NonParamState& state, int track) {
    return patchChange(instrumentEntry(ins), state, track);
}

void fillInstruments(const NonParamState& state, ymengine::InstrumentBank& out) {
    for (int k = 0; k < kInstruments; ++k) {
        const Instrument& src = state.instruments[size_t(k)];
        ymengine::Instrument& dst = out[size_t(k)];
        dst.used = src.used;
        dst.patch = src.patch;
        dst.arp = src.arp;
    }
}

}  // namespace stym::plugin
