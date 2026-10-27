// STYM2149 — plugincore: from the plugin parameters to the engine settings
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/Mapping.h"

#include <algorithm>
#include <cmath>

namespace stym::plugin {

using namespace ymengine;

VoiceMode voiceMode(const ParamValues& v) noexcept {
    switch (intValue(v, Global::VoiceMode)) {
    case 1: return VoiceMode::Authentic3;
    case 2: return VoiceMode::Dual3;
    default: return VoiceMode::Studio16;
    }
}

int chipCount(VoiceMode mode) noexcept {
    return mode == VoiceMode::Authentic3 ? 1 : mode == VoiceMode::Dual3 ? 2 : 0;
}

MidiRouting midiRouting(const ParamValues& v) noexcept {
    switch (intValue(v, Global::MidiRouting)) {
    case 1: return MidiRouting::Layer;
    case 2: return MidiRouting::PerChannel;
    default: return MidiRouting::Omni;
    }
}

bool steMachine(const ParamValues& v) noexcept { return intValue(v, Global::Computer) == 1; }

bool dmaTrack(VoiceMode mode, bool ste, int track) noexcept {
    // Each chip has four tracks (its three channels and its Digidrum track); the DMA tracks come after them.
    const int first = 4 * chipCount(mode);
    return ste && first > 0 && track >= first && track < first + kDmaTracks;
}

int trackCount(VoiceMode mode, bool ste) noexcept {
    const int chips = chipCount(mode);
    return chips == 0 ? kTracks : 4 * chips + (ste ? kDmaTracks : 0);
}

int trackCount(const ParamValues& v) noexcept { return trackCount(voiceMode(v), steMachine(v)); }

namespace {
// Imposed role of a track in 3 channels, or -1.
int fixedRole(VoiceMode mode, bool ste, int track) noexcept {
    const int chips = chipCount(mode);
    if (chips == 0) return -1;
    // The fourth track of each chip (tracks 4 and 8, 1-based) is its Digidrum track.
    if ((track & 3) == 3 && track < 4 * chips) return int(Generator::Digidrum);
    if (dmaTrack(mode, ste, track)) return int(Generator::Dma);
    return -1;
}
}  // namespace

bool generatorAllowed(VoiceMode mode, bool ste, int track, Generator g) noexcept {
    const int role = fixedRole(mode, ste, track);
    if (role >= 0) return int(g) == role;
    // A track without a role: in 3 channels, a hardware channel, which plays neither DMA nor Digidrum; in 16
    // channels, any type, DMA with the STE machine only.
    if (g == Generator::Dma) return ste && chipCount(mode) == 0;
    return chipCount(mode) == 0 || g != Generator::Digidrum;
}

Generator effectiveGenerator(const ParamValues& v, int track) noexcept {
    const Generator g = Generator(std::clamp(intValue(v, track, Track::Generator), 0, kGenerators - 1));
    const VoiceMode mode = voiceMode(v);
    const bool ste = steMachine(v);
    if (generatorAllowed(mode, ste, track, g)) return g;
    const int role = fixedRole(mode, ste, track);
    return role >= 0 ? Generator(role) : Generator::Chip;
}

int trackGate(const ParamValues& v, int track) noexcept {
    return value(v, Global::GateAll) >= 0.5f ? intValue(v, Global::Gate) : intValue(v, track, Track::Gate);
}

SeqClock seqClockOf(const ParamValues& v, SeqClock base) noexcept {
    return intValue(v, Global::ArpClock) == 1 ? SeqClock::Midi : base;
}

GlobalParams toGlobal(const ParamValues& v, const GlobalParams& base) noexcept {
    GlobalParams g = base;
    g.mode = voiceMode(v);
    g.master = double(value(v, Global::Master)) / 100.0;
    g.master2 = value(v, Global::MasterLink) >= 0.5f ? g.master : double(value(v, Global::Master2)) / 100.0;
    g.masterPan = std::clamp(double(value(v, Global::MasterPan)) / 100.0, -1.0, 1.0);
    g.softClip = value(v, Global::SoftClip) >= 0.5f;
    g.vblHz = kVblRates[size_t(std::clamp(intValue(v, Global::VblRate), 0, int(kVblRates.size()) - 1))];
    // Focus Track counts from 1 like the interface; the engine counts tracks from 0.
    g.focusTrack = std::clamp(intValue(v, Global::FocusTrack), 1, kTracks) - 1;
    g.layer = midiRouting(v) == MidiRouting::Layer;
    g.timer = intValue(v, Global::Timer) == 1 ? TimerMode::Free : TimerMode::Mfp;
    switch (intValue(v, Global::Velocity)) {
    case 1: g.velocity = VelocityMode::Volume; break;
    case 2: g.velocity = VelocityMode::Off; break;
    default: g.velocity = VelocityMode::Accent; break;
    }
    // MIDI ext: the MIDI clock; Internal: the Rate of the arpeggio, or the tempo with Arp Sync.
    g.arpClock = intValue(v, Global::ArpClock) == 1 ? ArpClock::MidiClock
                 : intValue(v, Global::ArpSync) == 1 ? ArpClock::Tempo
                                                     : ArpClock::Hz;
    g.seqClock = seqClockOf(v, base.seqClock);
    g.arpDivision = kArpDivisionTicks[size_t(std::clamp(intValue(v, Global::ArpDivision), 0,
                                                        int(kArpDivisionTicks.size()) - 1))];
    g.tempo = double(value(v, Global::FreeTempo));
    g.swing = double(value(v, Global::Swing));
    g.ste = steMachine(v);
    // Audio delay.
    ymengine::DelayParams& d = g.delay;
    d.on = value(v, Global::DelayOn) >= 0.5f;
    d.sync = intValue(v, Global::DelaySync) == 1;
    d.division = uint8_t(std::clamp(intValue(v, Global::DelayDivision), 0, ymengine::kDelayDivisions - 1));
    d.timeMs = double(value(v, Global::DelayTime));
    d.feedback = double(value(v, Global::DelayFeedback)) / 100.0;
    d.hiCutHz = double(value(v, Global::DelayHiCut));
    d.level = double(value(v, Global::DelayLevel)) / 100.0;
    d.pingPong = value(v, Global::DelayPingPong) >= 0.5f;
    g.a4Hz = double(value(v, Global::A4Hz));
    // MIDI expression.
    g.bendTarget = BendTarget(std::clamp(intValue(v, Global::BendTarget), 0, 2));
    g.bendRange = double(value(v, Global::BendRange));
    g.wheelTarget = ExpressTarget(std::clamp(intValue(v, Global::WheelTarget), 0, 4));
    g.aftertouch = ExpressTarget(std::clamp(intValue(v, Global::Aftertouch), 0, 4));
    g.mpeSlide = SlideTarget(std::clamp(intValue(v, Global::MpeSlide), 0, 2));
    g.expressCents = double(value(v, Global::WheelDepth));
    return g;
}

TrackMix toMix(const ParamValues& v, int t) noexcept {
    TrackMix m;
    m.enabled = value(v, t, Track::On) >= 0.5f;
    m.level = double(value(v, t, Track::Level)) / 100.0;
    m.pan = double(value(v, t, Track::Pan)) / 100.0;
    m.mute = value(v, t, Track::Mute) >= 0.5f;
    m.solo = value(v, t, Track::Solo) >= 0.5f;
    m.send = double(value(v, t, Track::Send)) / 100.0;
    m.link = value(v, t, Track::Link) >= 0.5f;  // the right side of a stereo DMA sample
    m.levelR = double(value(v, t, Track::LevelR)) / 100.0;
    m.muteR = value(v, t, Track::MuteR) >= 0.5f;
    m.soloR = value(v, t, Track::SoloR) >= 0.5f;
    return m;
}

TrackParams toTrack(const ParamValues& v, int t, const TrackParams& base) noexcept {
    TrackParams p = base;
    TrackPatch& s = p.patch;
    s.toneOn = value(v, t, Track::Tone) >= 0.5f;
    s.noiseOn = value(v, t, Track::Noise) >= 0.5f;
    s.mono = intValue(v, t, Track::Voicing) == 1;
    s.noisePeriod = uint8_t(std::clamp(intValue(v, t, Track::NoisePeriod), 1, 31));
    s.envelopeVolume = intValue(v, t, Track::VolMode) == 1;
    s.volume = uint8_t(std::clamp(intValue(v, t, Track::Volume), 0, 15));
    s.envPeriod = uint16_t(std::clamp(intValue(v, t, Track::EnvPeriod), 1, 65535));
    s.envShape = uint8_t(std::clamp(intValue(v, t, Track::EnvShape), 0, 15));
    s.generator = effectiveGenerator(v, t);
    s.sidRatio = double(value(v, t, Track::SidRatio));
    s.sidMinVolume = uint8_t(std::clamp(intValue(v, t, Track::SidVmin), 0, 15));
    s.vibratoCents = double(value(v, t, Track::VibDepth));
    s.vibratoHz = double(value(v, t, Track::VibRate));
    s.vibratoOn = value(v, t, Track::VibOn) >= 0.5f;
    s.attackMs = double(value(v, t, Track::Attack));
    s.decayMs = double(value(v, t, Track::Decay));
    s.sustainPct = double(value(v, t, Track::Sustain));
    s.releaseMs = double(value(v, t, Track::Release));
    s.adsrChip = intValue(v, t, Track::AdsrMode) == 1;
    p.arp.on = value(v, t, Track::ArpOn) >= 0.5f;
    p.arp.rateHz = double(value(v, t, Track::ArpRate));
    p.seq.gate = double(trackGate(v, t)) / 100.0;
    p.mix = toMix(v, t);
    return p;
}

namespace {
// Clocks in the order of the ChipClock choice (kClocks of Parameters.cpp). Entry 0: the Custom clock, whose frequency
// comes from the key.
constexpr ymcore::MasterClock kClocks[] = {ymcore::clocks::kDefault, ymcore::clocks::kAtariStfPal,
                                           ymcore::clocks::kZxSpectrum, ymcore::clocks::kAmstradCpc};

ymcore::MasterClock clockOf(const ConfigKey& k) noexcept {
    return k.clock == 0 && k.hz ? ymcore::MasterClock{k.hz, 1} : kClocks[k.clock];
}
}  // namespace

uint32_t customClockHz(const ParamValues& v) noexcept {
    return uint32_t(std::clamp(intValue(v, Global::ChipClockHz), kCustomClockMinHz, kCustomClockMaxHz));
}

ymcore::MasterClock chipClock(const ParamValues& v) noexcept { return clockOf(configKey(v)); }

ConfigKey configKey(const ParamValues& v) noexcept {
    ConfigKey k;
    k.clock = uint8_t(std::clamp(intValue(v, Global::ChipClock), 0, 3));
    // Custom clock: 16 channels only (Indus, fourth DAW test); the 2 MHz of the entry it replaces in the other modes,
    // the clock of the register exports.
    if (k.clock == 0)
        k.hz = voiceMode(v) == ymengine::VoiceMode::Studio16 ? customClockHz(v) : uint32_t(kCustomClockDefaultHz);
    k.chain = uint8_t(std::clamp(intValue(v, Global::OutputChain), 0, 2));
    k.resampling = uint8_t(std::clamp(intValue(v, Global::Resampling), 0, 2));
    return k;
}

EngineConfig engineConfig(const ConfigKey& key, const ParamValues& v, uint32_t hostRate, uint16_t trackOutputs) noexcept {
    EngineConfig c;
    static constexpr ymcore::BoardChain kChains[] = {ymcore::BoardChain::Raw, ymcore::BoardChain::ST,
                                                     ymcore::BoardChain::STE};
    static constexpr ymcore::OutputMode kModes[] = {ymcore::OutputMode::Raw, ymcore::OutputMode::RawBox,
                                                    ymcore::OutputMode::Retro};
    c.render.chip.clock = clockOf(key);
    c.render.hostRate = hostRate;
    c.render.mode = kModes[key.resampling];
    // AtariAudio averaging and Retro: at least one internal step (clk/8) per sample. Beyond that (Amstrad CPC at
    // 176.4 or 192 kHz, Atari clocks at 352.8 or 384 kHz), sinc.
    if (c.render.mode != ymcore::OutputMode::Raw && double(hostRate) > c.render.chip.clock.hz() / 8.0)
        c.render.mode = ymcore::OutputMode::Raw;
    c.render.board.chain = kChains[key.chain];
    c.render.board.bassDb = intValue(v, Global::SteBass);
    c.render.board.trebleDb = intValue(v, Global::SteTreble);
    c.trackOutputs = trackOutputs;
    return c;
}

uint32_t envPeriodFromCc(int v) noexcept {
    // Same operations as the prototype (EF_LO, efToHz, "ef" slider) for identical rounding. The prototype's envelope
    // frequency is 2 MHz / (256 × 32 × EP): efLo is that of EP 65535, and the 4000 steps of the slider raise
    // it geometrically up to 65535 × efLo (EP 1). The frequency is then turned back into the prototype's EP, which
    // × 32 gives the chip period.
    constexpr double kSteps = 4000, kSpan = 65535;
    const double efLo = 2000000.0 / (256.0 * 32.0 * 65535.0);
    const double ef = std::floor(double(std::clamp(v, 0, 127)) / 127.0 * kSteps + 0.5);
    const double hz = efLo * std::pow(kSpan, ef / kSteps);
    const double ep = std::clamp(std::floor(2000000.0 / (256.0 * 32.0 * hz) + 0.5), 1.0, 65535.0);
    return std::min<uint32_t>(65535, uint32_t(ep) * 32);
}

}  // namespace stym::plugin
