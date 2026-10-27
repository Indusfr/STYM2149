// STYM2149 — plugincore: parameter table
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Ranges, steps and defaults of the sliders and menus of the prototype (mkCh, GLOB, CLK, SOUND, ENVELOPE, VIBRATO,
// ADSR, GENERATOR, ARPEGGIO panels, Options menu); labels of its menus (MODE_NAMES, VM_NAMES, FXNAMES, ADSR_MODES,
// CLKMODES).
#include "plugincore/Parameters.h"

#include <cmath>
#include <unordered_map>

#include "ymengine/Pitch.h"

namespace stym::plugin {
namespace {

using namespace std::string_view_literals;

constexpr std::array kVoiceModes = {"16 ch"sv, "Authentic 3ch"sv, "Dual chip 2x3ch"sv};
// Custom clock in 16 ch (the former 2 MHz entry, 2 MHz in the other modes), Atari clock (PAL, named Atari
// ST), plus the AY ones of the ZX Spectrum and the Amstrad CPC, put back at Indus's request.
constexpr std::array kClocks = {"Custom"sv, "Atari ST 2.0053 MHz"sv, "ZX Spectrum 1.7734 MHz"sv,
                                "Amstrad CPC 1 MHz"sv};
constexpr std::array kChains = {"Raw"sv, "ST"sv, "STE"sv};
constexpr std::array kResampling = {"Sinc"sv, "Box (AtariAudio)"sv, "Retro"sv};
constexpr std::array kVblNames = {"50 Hz (PAL)"sv, "60 Hz (NTSC)"sv, "70 Hz (Monochrome)"sv, "100 Hz"sv, "200 Hz"sv};
constexpr std::array kTimers = {"MFP"sv, "Free"sv};
constexpr std::array kRoutings = {"Omni"sv, "Layer"sv, "Per channel"sv};
constexpr std::array kVelocities = {"Accent"sv, "Vol 4-bit"sv, "Off"sv};
// MIDI expression: ymengine::BendTarget, ExpressTarget and SlideTarget, in their order.
constexpr std::array kBendTargets = {"Pitch"sv, "Volume"sv, "Envelope"sv};
constexpr std::array kExpressTargets = {"Off"sv, "Vibrato"sv, "Volume"sv, "Envelope"sv, "LFO"sv};
constexpr std::array kSlideTargets = {"Volume"sv, "Envelope"sv, "Vibrato"sv};
constexpr std::array kArpClocks = {"Internal"sv, "MIDI ext"sv};
constexpr std::array kArpSyncs = {"Rate"sv, "Tempo"sv};
constexpr std::array kDelaySyncs = {"Time"sv, "Tempo"sv};
constexpr std::array kDelayDivisions = {"1/2"sv, "1/4D"sv, "1/4"sv,  "1/4T"sv,  "1/8D"sv, "1/8"sv,
                                        "1/8T"sv, "1/16D"sv, "1/16"sv, "1/16T"sv, "1/32"sv};  // kDelayDivisionBeats
constexpr std::array kArpDivisions = {"1/4"sv,  "1/8"sv, "1/8T"sv,  "1/16"sv, "1/16T"sv,
                                      "1/32"sv, "Tick"sv, "1/32T"sv, "1/64"sv};  // order: kArpDivisionTicks
constexpr std::array kVolModes = {"Vol 4-bit"sv, "Envelope"sv};
// FXNAMES of the prototype, then DMA (STE DMA sound).
constexpr std::array kGenerators = {"Chip Voice"sv, "SID Voice"sv, "Digidrum"sv, "Sync Buzzer"sv, "Chipdrum"sv, "DMA"sv};
static_assert(kGenerators.size() == size_t(ymengine::kGenerators));
constexpr std::array kComputers = {"ST"sv, "STE"sv};
constexpr std::array kAdsrModes = {"Smooth"sv, "Chip"sv};
constexpr std::array kVoicings = {"Polyphonic"sv, "Monophonic"sv};

static_assert(kArpDivisions.size() == kArpDivisionTicks.size());
static_assert(kVblNames.size() == kVblRates.size());
static_assert(kDelayDivisions.size() == ymengine::kDelayDivisionBeats.size());

// Builders of the table: num for a number, flag for an on/off, choice for a menu. The id and name given here are
// those of a global, or the bare field of a track, which buildSpecs prefixes with the track number.
ParamSpec num(std::string id, std::string name, ParamKind kind, float min, float max, float step, float def,
              std::string_view unit = {}, ParamScale scale = ParamScale::Linear) {
    ParamSpec s;
    s.id = std::move(id);
    s.name = std::move(name);
    s.kind = kind;
    s.min = min;
    s.max = max;
    s.step = step;
    s.def = def;
    s.unit = unit;
    s.scale = scale;
    return s;
}

// Parameter that appeared after the first publication (version of its ParameterID).
ParamSpec since(ParamSpec s, int version) {
    s.version = version;
    return s;
}

ParamSpec flag(std::string id, std::string name, bool def) {
    return num(std::move(id), std::move(name), ParamKind::Bool, 0, 1, 1, def ? 1.0f : 0.0f);
}

template <size_t N>
ParamSpec choice(std::string id, std::string name, const std::array<std::string_view, N>& names, int def) {
    ParamSpec s = num(std::move(id), std::move(name), ParamKind::Choice, 0, float(N - 1), 1, float(def));
    s.choices = std::span<const std::string_view>(names.data(), N);
    return s;
}

ParamSpec globalSpec(Global g) {
    switch (g) {
    case Global::Master: return num("master", "Master", ParamKind::Int, 0, 100, 1, 70, "%");
    case Global::VoiceMode: return choice("voiceMode", "Voice Mode", kVoiceModes, 0);
    case Global::ChipClock: return choice("chipClock", "Chip Clock", kClocks, 0);
    case Global::OutputChain: return choice("outputChain", "Output Chain", kChains, 0);
    case Global::Resampling: return choice("resampling", "Resampling", kResampling, 0);
    case Global::SteBass: return num("steBass", "STE Bass", ParamKind::Int, -12, 12, 2, 0, "dB");
    case Global::SteTreble: return num("steTreble", "STE Treble", ParamKind::Int, -12, 12, 2, 0, "dB");
    case Global::VblRate: return choice("vblRate", "VBL Rate", kVblNames, 0);
    case Global::SoftClip: return flag("softClip", "Soft Clip", true);
    case Global::Timer: return choice("timer", "Timer", kTimers, 0);
    case Global::FocusTrack: return num("focusTrack", "Focus Track", ParamKind::Int, 1, kTracks, 1, 1);
    case Global::MidiRouting: return choice("midiRouting", "MIDI Routing", kRoutings, 0);
    case Global::Velocity: return choice("velocity", "Velocity", kVelocities, 0);
    case Global::BendRange: return num("bendRange", "Bend Range", ParamKind::Int, 0, 24, 1, 2, "st");
    case Global::WheelDepth: return num("wheelDepth", "Wheel Depth", ParamKind::Int, 0, 200, 1, 50, "ct");
    case Global::ArpClock: return choice("arpClock", "Clock", kArpClocks, 0);  // arpeggio and sequencer
    case Global::ArpDivision: return choice("arpDivision", "Arp Division", kArpDivisions, 3);
    // Sequencer: BPM (60-240, step 0.01) and Swing (50-75) sliders of the prototype.
    case Global::FreeTempo: return since(num("freeTempo", "Free Tempo", ParamKind::Float, 60, 240, 0.01f, 120, "BPM"), 2);
    case Global::Swing: return since(num("swing", "Swing", ParamKind::Int, 50, 75, 1, 50, "%"), 2);
    // Global gate: absent from the prototype (its GLOB.gate is unused), under its reserved id.
    case Global::Gate: return since(num("gate", "Gate", ParamKind::Int, 5, 95, 1, 50, "%"), 2);
    case Global::GateAll: return since(flag("gateAll", "Gate All", false), 2);
    // Machine (Indus's table of 16/10/2026): ST, the prototype's, or STE with the DMA sound.
    case Global::Computer: return since(choice("computer", "Computer", kComputers, 0), 3);
    // Custom chip clock (fourth DAW test), set in a dialogue of the Chip clock menu.
    case Global::ChipClockHz:
        return since(num("chipClockHz", "Chip Clock Hz", ParamKind::Int, float(kCustomClockMinHz),
                         float(kCustomClockMaxHz), 1, float(kCustomClockDefaultHz), "Hz"),
                     4);
    // Arpeggios at their Rate, or synced to the tempo with Arp Division (sixth DAW test).
    case Global::ArpSync: return since(choice("arpSync", "Arp Sync", kArpSyncs, 0), 4);
    // Second Master, balance, link: left and right levels of the stereo outputs, chips 1 and 2 in 2x3 ch (seventh
    // DAW test).
    case Global::Master2: return since(num("master2", "Master 2", ParamKind::Int, 0, 100, 1, 70, "%"), 5);
    case Global::MasterPan: return since(num("masterPan", "Master Pan", ParamKind::Int, -100, 100, 1, 0), 5);
    case Global::MasterLink: return since(flag("masterLink", "Master Link", true), 5);
    // Audio delay (eighth DAW test): 1/8D at the tempo, 35 % of feedback, repeats darkened from 6 kHz.
    case Global::DelayOn: return since(flag("delayOn", "Delay On", false), 6);
    case Global::DelaySync: return since(choice("delaySync", "Delay Sync", kDelaySyncs, 1), 6);
    case Global::DelayDivision: return since(choice("delayDivision", "Delay Division", kDelayDivisions, 4), 6);
    case Global::DelayTime:
        return since(num("delayTime", "Delay Time", ParamKind::Int, float(ymengine::kDelayMinMs),
                         float(ymengine::kDelayMaxMs), 1, 375, "ms", ParamScale::Log),
                     6);
    case Global::DelayFeedback:
        return since(num("delayFeedback", "Delay Feedback", ParamKind::Int, 0,
                         float(ymengine::kDelayFeedbackMax * 100), 1, 35, "%"),
                     6);
    case Global::DelayHiCut:
        return since(num("delayHiCut", "Delay Hi Cut", ParamKind::Int, float(ymengine::kDelayHiCutMin),
                         float(ymengine::kDelayHiCutMax), 1, 6000, "Hz", ParamScale::Log),
                     6);
    case Global::DelayLevel: return since(num("delayLevel", "Delay Level", ParamKind::Int, 0, 100, 1, 100, "%"), 6);
    case Global::DelayPingPong: return since(flag("delayPingPong", "Delay Ping-Pong", false), 6);
    // Frequency of A4 for every note (Indus, 25/10/2026 19:29): 350 to 480 Hz, 440 by default.
    case Global::A4Hz:
        return since(num("a4Hz", "Tuning A4", ParamKind::Float, float(ymengine::kA4MinHz), float(ymengine::kA4MaxHz),
                         0.01f, float(ymengine::kA4Hz), "Hz"),
                     7);
    // MIDI expression (Indus, 25/10/2026 19:50-20:51): the bend on the pitch, the wheel and the aftertouch on
    // the vibrato, MPE off with notes bent by ±48 semitones (the MPE default) and CC74 on the volume.
    case Global::BendTarget: return since(choice("bendTarget", "Bend Target", kBendTargets, 0), 8);
    case Global::WheelTarget: return since(choice("wheelTarget", "Wheel Target", kExpressTargets, 1), 8);
    case Global::Aftertouch: return since(choice("aftertouch", "Aftertouch", kExpressTargets, 1), 8);
    case Global::Mpe: return since(flag("mpe", "MPE", false), 8);
    case Global::MpeBendRange:
        return since(num("mpeBendRange", "MPE Bend Range", ParamKind::Int, 0, 96, 1, 48, "st"), 8);
    case Global::MpeSlide: return since(choice("mpeSlide", "MPE Slide", kSlideTargets, 0), 8);
    case Global::Count: break;
    }
    return {};
}

ParamSpec trackSpec(Track f, int t) {
    switch (f) {
    case Track::On: return flag("on", "On", t == 0);  // the prototype enables only track 1 at the start
    case Track::Level: return num("level", "Level", ParamKind::Int, 0, 100, 1, 75, "%");
    case Track::Pan: return num("pan", "Pan", ParamKind::Int, -100, 100, 1, 0);
    case Track::Mute: return flag("mute", "Mute", false);
    case Track::Solo: return flag("solo", "Solo", false);
    case Track::Tone: return flag("tone", "Tone", true);
    case Track::Noise: return flag("noise", "Noise", false);
    case Track::NoisePeriod: return num("noisePeriod", "Noise Period", ParamKind::Int, 1, 31, 1, 8);
    case Track::Volume: return num("volume", "Volume", ParamKind::Int, 0, 15, 1, 13);
    case Track::VolMode: return choice("volMode", "Vol Mode", kVolModes, 0);
    // Chip period (R11-R12). Default: EP 400 of the prototype × 32. Geometric scale, like the "Env freq"
    // slider of the prototype (EP = 65535^(1 - ef / 4000)).
    case Track::EnvPeriod:
        return num("envPeriod", "Env Period", ParamKind::Int, 1, 65535, 1, 12800, {}, ParamScale::Log);
    case Track::EnvShape: return num("envShape", "Env Shape", ParamKind::Int, 0, 15, 1, 8);
    case Track::Generator: return choice("generator", "Generator", kGenerators, 0);
    case Track::SidRatio: return num("sidRatio", "Timer Ratio", ParamKind::Float, 0.25f, 8, 0.01f, 1, "x");
    case Track::SidVmin: return num("sidVmin", "SID Min Vol", ParamKind::Int, 0, 15, 1, 0);
    case Track::VibDepth: return num("vibDepth", "Vibrato Depth", ParamKind::Int, 0, 200, 1, 0, "ct");
    case Track::VibRate: return num("vibRate", "Vibrato Rate", ParamKind::Float, 1, 20, 0.5f, 6, "Hz");
    // Attack and Decay up to 2 000 ms like Release, the 100 VBL ticks of the Chip ADSR at 50 Hz (Indus, 25/10/2026
    // 18:30; 1 280 ms for a few minutes, and 1 000 ms before, as in the prototype).
    case Track::Attack: return num("attack", "Attack", ParamKind::Int, 0, 2000, 1, 0, "ms");
    case Track::Decay: return num("decay", "Decay", ParamKind::Int, 0, 2000, 1, 0, "ms");
    case Track::Sustain: return num("sustain", "Sustain", ParamKind::Int, 0, 100, 1, 100, "%");
    case Track::Release: return num("release", "Release", ParamKind::Int, 0, 2000, 1, 0, "ms");
    case Track::AdsrMode: return choice("adsrMode", "ADSR Mode", kAdsrModes, 0);
    case Track::Voicing: return choice("voicing", "Voicing", kVoicings, 0);
    case Track::ArpOn: return flag("arpOn", "Arp On", false);
    case Track::ArpRate: return num("arpRate", "Arp Rate", ParamKind::Int, 4, 200, 1, 50, "Hz");
    case Track::Gate: return since(num("gate", "Gate", ParamKind::Int, 5, 95, 1, 50, "%"), 2);  // Gate slider
    case Track::VibOn: return since(flag("vibOn", "Vibrato On", true), 4);  // VIBRATO frame
    case Track::Send: return since(num("send", "Send", ParamKind::Int, 0, 100, 1, 0, "%"), 6);  // Mixer
    // Right side of a stereo DMA sample (Mixer).
    case Track::LevelR: return since(num("levelR", "Level R", ParamKind::Int, 0, 100, 1, 75, "%"), 9);
    case Track::MuteR: return since(flag("muteR", "Mute R", false), 9);
    case Track::SoloR: return since(flag("soloR", "Solo R", false), 9);
    case Track::Link: return since(flag("link", "Link", true), 9);
    case Track::Count: break;
    }
    return {};
}

bool isMidiSetting(Global g) {
    switch (g) {
    case Global::MidiRouting:
    case Global::Velocity:
    case Global::BendRange:
    case Global::WheelDepth:
    case Global::ArpClock:
    case Global::ArpDivision:
    case Global::BendTarget:
    case Global::WheelTarget:
    case Global::Aftertouch:
    case Global::Mpe:
    case Global::MpeBendRange:
    case Global::MpeSlide: return true;
    default: return false;
    }
}

// The whole table in paramIndex order. Each track repeats trackSpec under its own id "tNN_<field>" (t01 to t16),
// name "Tn <name>" and group "Track n".
std::array<ParamSpec, kParamCount> buildSpecs() {
    std::array<ParamSpec, kParamCount> specs;
    for (int g = 0; g < kGlobalParams; ++g) {
        ParamSpec s = globalSpec(Global(g));
        s.group = "Global";
        s.field = g;
        // MIDI settings: a matter of configuration, not of automation. Custom clock: typed in a dialogue, each
        // value builds a new engine. Tuning A4: a setting of the project, typed in the Settings window.
        s.automatable = !isMidiSetting(Global(g)) && Global(g) != Global::ChipClockHz && Global(g) != Global::A4Hz;
        specs[size_t(paramIndex(Global(g)))] = std::move(s);
    }
    for (int t = 0; t < kTracks; ++t) {
        const std::string num2 = (t + 1 < 10 ? "0" : "") + std::to_string(t + 1);
        for (int f = 0; f < kTrackParams; ++f) {
            ParamSpec s = trackSpec(Track(f), t);
            s.id = "t" + num2 + "_" + s.id;
            s.name = "T" + std::to_string(t + 1) + " " + s.name;
            s.group = "Track " + std::to_string(t + 1);
            s.track = t;
            s.field = f;
            specs[size_t(paramIndex(t, Track(f)))] = std::move(s);
        }
    }
    return specs;
}

}  // namespace

const std::array<ParamSpec, kParamCount>& paramSpecs() {
    static const std::array<ParamSpec, kParamCount> specs = buildSpecs();
    return specs;
}

int findParam(std::string_view id) {
    // Built on first use; the keys view the ids of the static table, which live as long as the program.
    static const std::unordered_map<std::string_view, int> index = [] {
        std::unordered_map<std::string_view, int> m;
        const auto& specs = paramSpecs();
        for (int i = 0; i < kParamCount; ++i) m.emplace(specs[size_t(i)].id, i);
        return m;
    }();
    const auto it = index.find(id);
    return it == index.end() ? -1 : it->second;
}

ParamValues defaultValues() {
    ParamValues v{};
    const auto& specs = paramSpecs();
    for (int i = 0; i < kParamCount; ++i) v[size_t(i)] = specs[size_t(i)].def;
    return v;
}

float clampToSpec(const ParamSpec& s, float v) noexcept {
    if (!std::isfinite(v)) return s.def;
    if (v < s.min) v = s.min;
    if (v > s.max) v = s.max;
    switch (s.kind) {
    case ParamKind::Bool: return v >= 0.5f ? 1.0f : 0.0f;
    case ParamKind::Int:
    case ParamKind::Choice:
    case ParamKind::Float:
        // The steps are counted from min (STE bass and treble: -12, -10... +12), not from 0.
        if (s.step > 0) {
            v = s.min + std::round((v - s.min) / s.step) * s.step;
            if (v > s.max) v = s.max;
        }
        return v;
    }
    return v;
}

float toNormalized(const ParamSpec& s, float v) noexcept {
    if (s.max <= s.min) return 0;
    float t = 0;
    // Log scale: the inverse of v = min × (max / min)^t (fromNormalized); min is never 0 on such a range.
    if (s.scale == ParamScale::Log)
        t = float(std::log(double(v) / double(s.min)) / std::log(double(s.max) / double(s.min)));
    else
        t = (v - s.min) / (s.max - s.min);
    if (!(t > 0)) return 0;  // NaN included
    return t > 1 ? 1 : t;
}

float fromNormalized(const ParamSpec& s, float t) noexcept {
    if (!(t > 0)) t = 0;
    if (t > 1) t = 1;
    if (s.scale == ParamScale::Log)
        return float(double(s.min) * std::pow(double(s.max) / double(s.min), double(t)));
    return s.min + t * (s.max - s.min);
}

}  // namespace stym::plugin
