// STYM2149 — plugincore: import of YM-Tracker songs (.ymt) as editable patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/YmtImport.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "plugincore/Digidrum.h"
#include "plugincore/Instruments.h"
#include "ymengine/Pitch.h"
#include "ymio/YmTracker.h"

namespace stym::plugin {

using ymengine::Generator;
using ymengine::SeqCmd;
using ymengine::seqCmd;
using ymio::YmtCell;
using ymio::YmtInstrument;
using ymio::YmtSong;

namespace {

constexpr int kMidiOffset = 23;     // MIDI note of note n of YM-Tracker: n + 23 (37, its C-5, is C4)
constexpr int kDrumNotes = 12;      // a note of the digidrum channel plays sample (note - 1) mod 12 of its bank
constexpr int kBpmMin = 32, kBpmMax = 255;  // Fxx from $20: a tempo
constexpr int kTableSteps = ymengine::kOrnamentSteps;
constexpr int kPerMax = ymengine::kPeriodDeltaMax;
constexpr ymcore::MasterClock kClock = ymcore::clocks::kDefault;  // 2 MHz, the clock of Authentic 3ch

std::string count(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

bool hasNote(const YmtCell& x) noexcept { return x.note >= 1 && x.note <= 96; }
// VOL that plays the volume `v64` of 64 (INFERRED: the 4-bit volume × v64 / 64) on a sound of volume 15; `quiet`
// counts the volumes below the one that VOL 1 plays.
int volOf(int v64, int& quiet) noexcept {
    const int level = int(std::lround(std::clamp(v64, 0, 64) * 15 / 64.0));
    quiet += level > 0 && level < attenuatedVolume(15, 1 / 15.0);
    return volForLevel(level);
}

// Rate of the timer of Qxx, Sxx and Uxx (INFERRED from the effect sheet of YM-Tracker): 20 × 2^(x + y / 16) Hz.
double timerHz(int param) noexcept { return 20 * std::pow(2.0, (param >> 4) + (param & 15) / 16.0); }

// Name of an effect left out: "1xx", "E9x" (Exy, per subcommand).
std::string effectName(char effect, uint8_t param) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (effect == 'E') return std::string{'E', kHex[param >> 4], 'x'};
    return std::string{effect, 'x', 'x'};
}

// Lines played of a pattern: up to the first Bxx or Dxx, included.
struct Flow {
    int rows = 64;
    int jump = -1;     // Bxx: next position in the order table
    int breakRow = 0;  // Dxx: start line of the next position (decimal, as in an XM)
};

Flow flowOf(const YmtSong& m, int p) {
    Flow f;
    f.rows = m.patterns[size_t(p)].rows;
    for (int r = 0; r < f.rows; ++r) {
        bool stop = false;
        for (int c = 0; c < m.channels(); ++c) {
            const YmtCell& x = m.cell(p, r, c);
            if (x.effect == 'B') {
                f.jump = x.param;
                stop = true;
            } else if (x.effect == 'D') {
                f.breakRow = (x.param >> 4) * 10 + (x.param & 15);
                stop = true;
            }
        }
        if (stop) {
            f.rows = r + 1;
            break;
        }
    }
    return f;
}

// State of a channel line after line.
struct Chan {
    int inst = 0;       // instrument of the notes (0: the "--" one)
    int vol = 64;       // 0-64
    int slid = 0;       // of which the volume slides since the attack (the engine adds them to VOL)
    int volMem = 0;     // Axy of A00
    bool holding = false;
};

// Volume of a channel at tick 0 of a line: the instrument column, the volume column, Cxx, EAx and EBx; a note starts
// its slides again.
void lineVolume(Chan& cs, const YmtCell& x) noexcept {
    if (x.instrument) cs.vol = 64;
    if (hasNote(x)) cs.slid = 0;
    if (x.volume >= 0x10 && x.volume <= 0x50) cs.vol = x.volume - 0x10;
    if (x.effect == 'C') cs.vol = std::min<int>(x.param, 64);
    if (x.effect == 'E' && (x.param >> 4) == 0xA) cs.vol = std::min(64, cs.vol + (x.param & 15));
    if (x.effect == 'E' && (x.param >> 4) == 0xB) cs.vol = std::max(0, cs.vol - (x.param & 15));
}
// Its volume slide (Axy, 5xy, 6xy) over the ticks 1 to speed - 1 of the line; returns the Axy played (0: none).
int slideVolume(Chan& cs, const YmtCell& x, int speed) noexcept {
    if (x.effect != 'A' && x.effect != '5' && x.effect != '6') return 0;
    const int a = x.param ? x.param : cs.volMem;
    cs.volMem = a;
    const int d = ((a >> 4) ? (a >> 4) : -(a & 15)) * (speed - 1);
    cs.vol = std::clamp(cs.vol + d, 0, 64);
    cs.slid += d;
    return a;
}
// VOL of a line: the volume less the slides that the engine plays on the note.
int volRow(const Chan& cs) noexcept { return std::clamp(cs.vol - cs.slid, 0, 64); }

// The effects of the effect column that a note plays (INFERRED: they last from their line to the end of the note): the
// first of each kind, and the line of the note where it starts (0: the attack).
struct NoteFx {
    int arp = 0;              // 0xy (xy ≠ 0)
    int vib = 0;              // 4xy (xy ≠ 0)
    char timer = 0;           // 'Q' (SID), 'S' (Sinus-SID), 'U' (Sync-Buzzer)
    int timerParam = 0;
    int shape = -1;           // Xxx: R13 shape, envelope on
    int divisor = 0;          // Pxx (xx ≠ 0)
    int periodHi = -1, periodLo = -1;  // Yxx, Mxx
    auto operator<=>(const NoteFx&) const = default;
};
struct FxStart {
    int arp = -1, vib = -1, timer = -1, shape = -1, period = -1;
};

// A note of a channel in a pattern: its attack line, its last line (held to the end of it, or released at tick
// `releaseTick` of it by a Kxx), its MIDI note and its sound.
struct NoteRec {
    int row = 0, last = 0;
    int releaseTick = -1;
    int midi = 60;
    int inst = 0;
    NoteFx fx;
    FxStart at;
    bool slide = false;  // a 3xx on the next note: SLD
    int variant = -1;
};

struct VariantKey {
    int inst = 0;
    NoteFx fx;
    auto operator<=>(const VariantKey&) const = default;
};

// Per tick: semitones of the arpeggio, period offset of the pitch macro, volume of the list (without ADSR).
struct Tick {
    int semis = 0, delta = 0, vol = 15;
    bool operator==(const Tick&) const = default;
};

Tick tickOf(const YmtInstrument& in, const NoteFx& fx, int t) {
    Tick k;
    if (fx.arp) {  // 0xy: note, x, y, one tick each (XM)
        const int a[3] = {0, fx.arp >> 4, fx.arp & 15};
        k.semis = a[t % 3];
    } else if (!in.arp.empty()) {
        const int speed = std::max<int>(1, in.arpSpeed);
        k.semis = in.arp[size_t(t / speed) % in.arp.size()];
    }
    if (!in.pitch.empty()) k.delta = in.pitch[size_t(t) % in.pitch.size()];
    if (!in.useAdsr && !in.volume.empty()) {
        const int n = int(in.volume.size());
        int i = t;
        if (i >= n) i = in.volumeLoop >= 0 && in.volumeLoop < n ? in.volumeLoop + (t - in.volumeLoop) % (n - in.volumeLoop)
                                                                 : n - 1;
        k.vol = std::min<int>(in.volume[size_t(i)], 15);
    }
    return k;
}

// Period that the engine plays for a table step of `semis` semitones on a note of period tp0 (tablePeriod).
int tableTp(int tp0, int semis) noexcept { return int(std::floor(tp0 * std::pow(2.0, -semis / 12.0) + 0.5)); }

// What the import counts for its alert.
struct Counts {
    int tableCut = 0, pitchClamped = 0, sidClamped = 0, buzzRate = 0, moved = 0, carried = 0, full = 0;
};

// The R13 shape of an Xxx or of the HardEnv tab among those of the Shape menu (8 to 15): 0-3 hold like $9, 4-7 like $F.
int shapeOf(int s) noexcept { return s < 4 ? 9 : s < 8 ? 15 : s & 15; }
bool loopingShape(int s) noexcept { return s == 8 || s == 10 || s == 12 || s == 14; }

// The sound of an instrument of the song with the effects of its notes, tuned on `refMidi`, the note it plays most.
Instrument instrumentOf(const YmtInstrument& in, const NoteFx& fx, const FxStart& at, int refMidi, double tickHz,
                        Counts& counts) {
    Instrument ins;
    ins.used = true;
    ins.level = 1.0;
    ymengine::TrackPatch& p = ins.patch;
    p.toneOn = in.tone;
    p.noiseOn = in.noise;
    p.noisePeriod = uint8_t(std::clamp<int>(in.noisePeriod, 1, 31));
    p.volume = 15;
    p.envelopeVolume = false;
    p.generator = Generator::Chip;
    // ADSR in ticks of the song (Chip ADSR: the 4-bit volume at the replay tick).
    const double tickMs = 1000 / tickHz;
    p.adsrChip = true;
    if (in.useAdsr) {
        p.attackMs = in.attack * tickMs;
        p.decayMs = in.decay * tickMs;
        p.sustainPct = std::min<int>(in.sustain, 15) * 100.0 / 15;
        p.releaseMs = in.release * tickMs;
    }
    // Hardware envelope: that of the HardEnv tab, or that of an Xxx (from its line: SRC or GEN there, Exx for its
    // shape on a note that has the envelope already).
    const bool late = fx.shape >= 0 && at.shape > 0;
    const bool envAttack = in.hardEnv || (fx.shape >= 0 && !late);
    const bool env = in.hardEnv || fx.shape >= 0;
    const int shape = shapeOf(fx.shape >= 0 ? fx.shape : in.shape);  // that of the note, Xxx included
    const bool fixed = !in.autoPeriod || fx.periodHi >= 0 || fx.periodLo >= 0;
    const double refHz = ymengine::noteHz(refMidi);
    p.envShape = uint8_t(late && in.hardEnv ? shapeOf(in.shape) : shape);
    p.envelopeVolume = envAttack;
    bool buzz = in.syncBuzzer || (fx.timer == 'U' && at.timer <= 0);
    if (env) {
        if (fixed) {
            int ep = in.envPeriod;
            if (fx.periodHi >= 0) ep = (ep & 0xFF) | fx.periodHi << 8;
            if (fx.periodLo >= 0) ep = (ep & 0xFF00) | fx.periodLo;
            p.envPeriod = uint16_t(std::max(1, ep));
        } else {
            // The period of a sawtooth at the note (INFERRED: the period that the HardEnv tab shows), the octave or the
            // Pxx making the envelope that many times faster; a looped triangle then plays half that frequency.
            const int n = std::clamp(fx.divisor ? fx.divisor : std::max<int>(1, in.octave), 1, ymengine::kEnvRatioMax);
            if (loopingShape(shape)) {
                // A looped shape at the note: a Sync Buzzer tuned to it (from the attack, or from the Xxx: GEN).
                buzz = buzz || envAttack;
                p.envTuned = true;
                p.envRatioN = uint8_t(n);
                p.envRatioD = uint8_t(shape == 10 || shape == 14 ? 2 : 1);
            } else {
                p.envPeriod = uint16_t(std::clamp(std::lround(kClock.hz() / (256.0 * refHz) / n), 1L, 65535L));
            }
        }
    }
    // Timer effects: Qxx and Sxx a SID at the rate of the timer, Uxx and the Sync-Buzzer of the HardEnv tab a Sync
    // Buzzer (its envelope tuned to the note when its period follows it; the timer follows the note).
    if (fx.timer == 'Q' || fx.timer == 'S') {
        const double ratio = timerHz(fx.timerParam) / refHz;
        p.sidRatio = std::clamp(ratio, 0.25, 8.0);
        counts.sidClamped += p.sidRatio != ratio;
        if (fx.timer == 'S') {
            p.sidForm = ymengine::SidForm::Sine;
            p.sidWave = ymengine::sineSidWave(3, 15, p.sidWavePhase, p.sidWavePeriod);
            p.sidWaveLen = p.sidWavePeriod;
        }
        if (at.timer == 0) p.generator = Generator::Sid;
    } else if (buzz) {
        p.generator = Generator::SyncBuzzer;
    }
    counts.buzzRate += fx.timer == 'U';
    if (p.envTuned)
        p.pitchMatrix = uint16_t(p.pitchMatrix |
                                 ymengine::pitchBit(ymengine::PitchSource::Table, ymengine::PitchTarget::Buzzer));
    // Vibrato: 4xy or that of the Tone tab, x/64 of a cycle per tick, y/8 of a semitone deep (as the XM 4xy).
    const int vs = fx.vib ? fx.vib >> 4 : in.vibSpeed, vd = fx.vib ? fx.vib & 15 : in.vibDepth;
    if (vs && vd) {
        p.vibratoHz = std::clamp(tickHz * vs / 64, 1.0, 20.0);
        p.vibratoCents = std::clamp(vd * 12.5, 0.0, 200.0);
        p.vibratoOn = true;
    }
    // Modulation table: the arpeggio, the pitch macro and the volume list per tick (one VBL frame per step), looped
    // when they repeat within 64 ticks, otherwise held at its last step.
    std::vector<Tick> ticks;
    for (int t = 0; t < 2 * kTableSteps; ++t) ticks.push_back(tickOf(in, fx, t));
    int len = 0;
    bool loop = false;
    for (int L = 1; L <= kTableSteps && !len; ++L) {
        bool same = true;
        for (int t = L; t < int(ticks.size()) && same; ++t) same = ticks[size_t(t)] == ticks[size_t(t % L)];
        if (same) len = L, loop = true;
    }
    if (!len) {  // held: up to the tick from which it no longer changes
        int last = int(ticks.size()) - 1;
        while (last > 0 && ticks[size_t(last - 1)] == ticks.back()) --last;
        len = std::min(last + 1, kTableSteps);
        counts.tableCut += last + 1 > kTableSteps;
    }
    const bool flat = loop && len == 1 && ticks.front() == Tick{};
    if (!flat) {
        const int tp0 = ymengine::tonePeriodFor(refHz, kClock);
        p.ornamentOn = true;
        p.ornamentLength = uint8_t(len);
        p.ornamentLoop = loop;
        p.ornamentRate = 1;
        bool clamped = false;
        for (int i = 0; i < len; ++i) {
            const Tick& k = ticks[size_t(i)];
            // The arpeggio in semitones, the pitch macro in PER (the same period offset on every note); beyond their
            // ranges, the semitones nearest to the period wanted at the reference note.
            const int want = std::clamp(tableTp(tp0, k.semis) + k.delta, 1, 4095);
            int orn = std::clamp(k.semis, -12, 12);
            int per = want - tableTp(tp0, orn);
            if (std::abs(per) > kPerMax) {
                orn = std::clamp(int(std::lround(12 * std::log2(double(tp0) / want))), -12, 12);
                per = want - tableTp(tp0, orn);
            }
            clamped = clamped || std::abs(per) > kPerMax;
            p.ornamentPitch[size_t(i)] = int8_t(orn);
            p.rows.period[size_t(i)] = int8_t(std::clamp(per, -kPerMax, kPerMax));
            p.ornamentVolume[size_t(i)] = uint8_t(k.vol);
        }
        counts.pitchClamped += clamped;
    }
    return ins;
}

// A digidrum of the song: the sample of a bank (its values as they are).
struct Drum {
    const ymio::YmtDrum* d = nullptr;
    bool operator==(const Drum& o) const { return *d == *o.d; }
};

}  // namespace

RegisterImportResult importYmt(const YmtSong& m, NonParamState& state) {
    RegisterImportResult out;
    if (m.patterns.empty() || m.order.empty() || m.instruments.empty()) {
        out.error = "Nothing to import: the song has no pattern.";
        return out;
    }
    RegisterImport r;
    r.title = m.title;
    Counts counts;
    std::map<std::string, int> effects;  // effects left out
    int volumeColumn = 0, quiet = 0, breaks = 0, tempoClamped = 0, tempoCmdClamped = 0, tempoCmds = 0, speedLost = 0;

    // ---- Song: the order table walked as an XM is ----
    std::vector<Flow> flows;
    for (int p = 0; p < int(m.patterns.size()); ++p) flows.push_back(flowOf(m, p));
    struct Visit {
        int pattern = 0;
        int speed = 6, bpm = 125;  // at the start of the position, effects of its first line included
    };
    std::vector<Visit> visits;
    int restartVisit = 0;
    {
        std::vector<int> seen(m.order.size(), -1);
        int pos = 0;
        for (;;) {
            if (pos >= int(m.order.size())) {
                if (m.restart < int(seen.size()) && seen[size_t(m.restart)] >= 0) restartVisit = seen[size_t(m.restart)];
                break;
            }
            if (seen[size_t(pos)] >= 0) {
                restartVisit = seen[size_t(pos)];
                break;
            }
            seen[size_t(pos)] = int(visits.size());
            visits.push_back({m.order[size_t(pos)]});
            const Flow& f = flows[size_t(m.order[size_t(pos)])];
            breaks += f.breakRow != 0;
            pos = f.jump >= 0 ? f.jump : pos + 1;
        }
    }
    std::vector<int> stymOf(m.patterns.size(), -1), ymtOf;
    int droppedVisits = 0;
    for (const Visit& v : visits) {
        if (stymOf[size_t(v.pattern)] < 0 && int(ymtOf.size()) < ymseq::kPatterns) {
            stymOf[size_t(v.pattern)] = int(ymtOf.size());
            ymtOf.push_back(v.pattern);
        }
        droppedVisits += stymOf[size_t(v.pattern)] < 0;
    }

    // ---- Channels: the tone channels of the first chip on tracks 1 to 3, the digidrum channel on track 4 ----
    const int channels = m.channels();
    const int drumChannel = m.drumChannel ? channels - 1 : -1;
    std::vector<int> trackOf(size_t(channels), -1);
    for (int c = 0; c < std::min({3, m.toneChannels, channels}); ++c)
        if (c != drumChannel) trackOf[size_t(c)] = c;
    int droppedChannels = 0;
    for (int c = 0; c < channels; ++c) {
        if (trackOf[size_t(c)] >= 0 || c == drumChannel) continue;
        bool noted = false;
        for (int p : ymtOf)
            for (int row = 0; row < flows[size_t(p)].rows && !noted; ++row) noted = hasNote(m.cell(p, row, c));
        droppedChannels += noted;
    }
    const int instCount = int(m.instruments.size());
    auto instOf = [&](int i) -> const YmtInstrument& { return m.instruments[size_t(std::clamp(i, 0, instCount - 1))]; };

    // ---- Walk: speed and tempo, channel state at the start of the patterns ----
    std::vector<std::vector<Chan>> firstState(ymtOf.size());
    std::vector<std::pair<int, int>> startOf(ymtOf.size(), {0, 0});
    std::vector<int> bpms;
    {
        std::vector<Chan> ch(static_cast<size_t>(channels));
        int speed = std::clamp(m.speed, 1, ymengine::kSpeedMax), bpm = std::clamp(m.bpm, kBpmMin, kBpmMax);
        bpms.push_back(bpm);
        for (Visit& v : visits) {
            const int k = stymOf[size_t(v.pattern)];
            if (k >= 0 && firstState[size_t(k)].empty()) firstState[size_t(k)] = ch;
            for (int row = 0; row < flows[size_t(v.pattern)].rows; ++row) {
                // Fxx first: the speed of the line is that of its ticks.
                for (int c = 0; c < channels; ++c) {
                    const YmtCell& x = m.cell(v.pattern, row, c);
                    if (x.effect != 'F' || x.param == 0) continue;
                    if (x.param < 0x20) speed = x.param;
                    else bpm = x.param;
                    if (std::find(bpms.begin(), bpms.end(), bpm) == bpms.end()) bpms.push_back(bpm);
                }
                if (row == 0) {
                    v.speed = speed;
                    v.bpm = bpm;
                }
                for (int c = 0; c < channels; ++c) {
                    const YmtCell& x = m.cell(v.pattern, row, c);
                    Chan& cs = ch[size_t(c)];
                    if (x.instrument) cs.inst = x.instrument;
                    if (hasNote(x)) cs.holding = !(x.effect == 'K' && x.param < speed);
                    else if (x.note == ymio::kYmtNoteOff || (x.effect == 'K' && x.param < speed)) cs.holding = false;
                    lineVolume(cs, x);
                    slideVolume(cs, x, speed);
                }
            }
            if (k >= 0 && !startOf[size_t(k)].first) startOf[size_t(k)] = {v.speed, v.bpm};
        }
    }
    const int bpm0 = visits.front().bpm, speed0 = std::clamp(visits.front().speed, 1, ymengine::kSpeedMax);
    const double tickHz = bpm0 * 0.4;  // a tick lasts 2.5 / BPM seconds
    // Ticks timing: a single tempo, on a VBL rate (125, 150, 175, 250 or 500 BPM); otherwise BPM, as for an XM.
    const bool ticks = bpms.size() == 1 && bpms[0] * 2 % 5 == 0 &&
                       std::find(kVblRates.begin(), kVblRates.end(), uint32_t(bpms[0] * 2 / 5)) != kVblRates.end();
    const double stepsPerBeat =
        ymengine::kStepsPerBeat[std::min<size_t>(state.global.resolution, ymengine::kSeqResolutions - 1)];
    auto tempoOf = [stepsPerBeat](int speed, int bpm) { return std::lround(24.0 * bpm / (speed * stepsPerBeat)); };
    // Speed and tempo in force at each line of the patterns (their first visit).
    std::vector<std::vector<std::pair<int, int>>> rowTime(ymtOf.size());
    for (size_t k = 0; k < ymtOf.size(); ++k) {
        auto [speed, bpm] = startOf[k];
        for (int row = 0; row < flows[size_t(ymtOf[k])].rows; ++row) {
            for (int c = 0; c < channels && row > 0; ++c) {
                const YmtCell& x = m.cell(ymtOf[k], row, c);
                if (x.effect == 'F' && x.param) (x.param < 0x20 ? speed : bpm) = x.param;
            }
            rowTime[k].push_back({speed, bpm});
        }
    }
    // A silent sound: no tone, no noise, no envelope (that of the digidrum hits of the demo songs).
    auto silent = [&](int inst, const YmtCell& x) {
        const YmtInstrument& in = instOf(inst);
        return !in.tone && !in.noise && !in.hardEnv && x.effect != 'X';
    };
    // ---- Notes of the tone channels and the effects that they play ----
    std::vector<std::vector<std::vector<NoteRec>>> notes(ymtOf.size(),
                                                        std::vector<std::vector<NoteRec>>(size_t(channels)));
    int slideLost = 0;
    for (size_t k = 0; k < ymtOf.size(); ++k) {
        const int p = ymtOf[k];
        for (int c = 0; c < channels; ++c) {
            if (trackOf[size_t(c)] < 0) continue;
            Chan cs = firstState[k][size_t(c)];
            std::vector<NoteRec>& v = notes[k][size_t(c)];
            int cur = -1;  // index in v of the note that sounds
            bool carried = cs.holding;
            auto close = [&](int last) {
                if (cur >= 0) v[size_t(cur)].last = last;
                cur = -1;
                carried = false;
            };
            for (int row = 0; row < flows[size_t(p)].rows; ++row) {
                const YmtCell& x = m.cell(p, row, c);
                const int speed = rowTime[k][size_t(row)].first;
                if (x.instrument) cs.inst = x.instrument;
                if (hasNote(x)) {
                    if (x.effect == '3') {  // tone portamento: the note before slides to this one (SLD)
                        if (cur >= 0) v[size_t(cur)].slide = true;
                        else ++slideLost;
                    }
                    close(row - 1);
                    if (x.effect == 'Z' && silent(cs.inst, x)) continue;  // a digidrum hit alone (track 4)
                    NoteRec n;
                    n.row = n.last = row;
                    n.midi = x.note + kMidiOffset;
                    n.inst = std::clamp(cs.inst, 0, instCount - 1);
                    v.push_back(n);
                    cur = int(v.size()) - 1;
                    if (x.effect == 'K' && x.param < speed) {  // released within its own line
                        v[size_t(cur)].releaseTick = std::max<int>(1, x.param);
                        close(row);
                        continue;
                    }
                } else if (x.note == ymio::kYmtNoteOff || (x.effect == 'K' && x.param == 0)) {
                    close(row - 1);
                    continue;
                } else if (x.effect == 'K' && x.param < speed && (cur >= 0 || carried)) {
                    if (cur >= 0) v[size_t(cur)].releaseTick = x.param;
                    close(row);
                    continue;
                }
                if (cur >= 0) v[size_t(cur)].last = row;
                // The effects of the note (the first of each kind), from their line on.
                if (!x.effect || std::string_view("04QSUXPYM").find(x.effect) == std::string_view::npos ||
                    (x.effect == '0' && x.param == 0) || (x.effect == '4' && x.param == 0) ||
                    (x.effect == 'P' && x.param == 0))
                    continue;
                if (cur < 0) {
                    ++counts.carried;
                    continue;
                }
                NoteRec& n = v[size_t(cur)];
                const int at = row - n.row;
                switch (x.effect) {
                case '0':
                    if (!n.fx.arp) n.fx.arp = x.param, n.at.arp = at;
                    break;
                case '4':
                    if (!n.fx.vib) n.fx.vib = x.param, n.at.vib = at;
                    break;
                case 'X':
                    if (n.fx.shape < 0) n.fx.shape = x.param & 15, n.at.shape = at;
                    break;
                case 'P':
                    if (!n.fx.divisor) n.fx.divisor = x.param, n.at.period = std::max(n.at.period, at);
                    break;
                case 'Y':
                    if (n.fx.periodHi < 0) n.fx.periodHi = x.param, n.at.period = std::max(n.at.period, at);
                    break;
                case 'M':
                    if (n.fx.periodLo < 0) n.fx.periodLo = x.param, n.at.period = std::max(n.at.period, at);
                    break;
                default:  // Q, S, U
                    if (!n.fx.timer) n.fx.timer = x.effect, n.fx.timerParam = x.param, n.at.timer = at;
                    break;
                }
            }
            close(flows[size_t(p)].rows - 1);
        }
    }

    // ---- Instruments: one per instrument of the song and set of effects of its notes, 32 at most ----
    struct Variant {
        VariantKey key;
        bool timerLate = false, shapeLate = false;
        std::map<int, int> midis;  // notes played
        auto id() const { return std::tuple(key, timerLate, shapeLate); }
    };
    std::vector<Variant> variants;
    std::map<std::tuple<VariantKey, bool, bool>, int> variantOf;
    int noteCount = 0;
    for (auto& perPattern : notes)
        for (auto& v : perPattern)
            for (NoteRec& n : v) {
                ++noteCount;
                Variant w;
                w.key = {n.inst, n.fx};
                w.timerLate = n.fx.timer && n.at.timer > 0;
                w.shapeLate = n.fx.shape >= 0 && n.at.shape > 0;
                auto it = variantOf.find(w.id());
                if (it == variantOf.end()) {
                    if (int(variants.size()) < kInstruments) {
                        it = variantOf.emplace(w.id(), int(variants.size())).first;
                        variants.push_back(w);
                    } else {  // full: the same instrument without effects, or with others
                        ++counts.full;
                        int best = -1;
                        for (size_t i = 0; i < variants.size() && best < 0; ++i)
                            if (variants[i].key.inst == n.inst && variants[i].key.fx == NoteFx{}) best = int(i);
                        for (size_t i = 0; i < variants.size() && best < 0; ++i)
                            if (variants[i].key.inst == n.inst) best = int(i);
                        n.variant = std::max(best, 0);
                        n.fx = variants[size_t(n.variant)].key.fx;
                        n.at = {};
                        ++variants[size_t(n.variant)].midis[n.midi];
                        continue;
                    }
                }
                n.variant = it->second;
                ++variants[size_t(n.variant)].midis[n.midi];
            }
    std::vector<Instrument> instruments;
    std::vector<bool> baseFlat;  // the table of the instrument without its 0xy holds nothing
    for (const Variant& w : variants) {
        int ref = 60, most = 0;
        for (const auto& [midi, times] : w.midis)
            if (times > most) ref = midi, most = times;
        FxStart at;
        at.timer = w.key.fx.timer ? (w.timerLate ? 1 : 0) : -1;
        at.shape = w.key.fx.shape >= 0 ? (w.shapeLate ? 1 : 0) : -1;
        const YmtInstrument& in = instOf(w.key.inst);
        Instrument ins = instrumentOf(in, w.key.fx, at, ref, tickHz, counts);
        // Name: that of the instrument, then its effects ("Lead 047 Q24").
        std::string tags;
        auto tag = [&tags](char letter, int param) {
            static constexpr char kHex[] = "0123456789ABCDEF";
            tags += std::string(tags.empty() ? "" : " ") + letter + kHex[(param >> 4) & 15] + kHex[param & 15];
        };
        const NoteFx& f = w.key.fx;
        if (f.arp) tag('0', f.arp);
        if (f.vib) tag('4', f.vib);
        if (f.timer) tag(f.timer, f.timerParam);
        if (f.shape >= 0) tag('X', f.shape);
        if (f.divisor) tag('P', f.divisor);
        if (f.periodHi >= 0) tag('Y', f.periodHi);
        if (f.periodLo >= 0) tag('M', f.periodLo);
        std::string base = cleanInstrumentName(in.name);
        if (base.empty()) base = "Instrument " + std::to_string(w.key.inst);
        if (!tags.empty()) {
            const size_t room = kInstrumentNameMax > tags.size() + 1 ? kInstrumentNameMax - tags.size() - 1 : 0;
            base = room ? cleanInstrumentName(base.substr(0, room)) + " " + tags : tags;
        }
        ins.name = cleanInstrumentName(base);
        instruments.push_back(std::move(ins));
        NoteFx plain = w.key.fx;
        plain.arp = 0;
        bool flat = true;
        for (int t = 0; t < 2 * kTableSteps && flat; ++t) flat = tickOf(in, plain, t) == Tick{};
        baseFlat.push_back(flat);
    }

    // ---- Digidrums: the hits of the digidrum channel and of the Zxx, their samples in slots 1 to 8 ----
    struct Hit {
        int row = 0, channel = 0, slot = -1, vol = 15, rows = 1;
    };
    std::vector<std::vector<Hit>> hits(ymtOf.size());
    std::vector<Drum> drums;  // the samples played, in the order of their first hit
    std::vector<int> zHits(size_t(channels), 0);
    int drumMissing = 0, drumNoSlot = 0, drumLost = 0, drumMoved = 0;
    for (size_t k = 0; k < ymtOf.size(); ++k) {
        const int p = ymtOf[k];
        std::vector<Chan> chs = firstState[k];
        for (int row = 0; row < flows[size_t(p)].rows; ++row) {
            for (int c = 0; c < channels; ++c) {
                if (c != drumChannel && trackOf[size_t(c)] < 0) continue;
                Chan& cs = chs[size_t(c)];
                const YmtCell& x = m.cell(p, row, c);
                if (x.instrument) cs.inst = x.instrument;
                lineVolume(cs, x);
                int index = -1;
                if (c == drumChannel && hasNote(x)) index = (x.note - 1) % kDrumNotes;
                else if (c != drumChannel && x.effect == 'Z' && x.param) index = x.param - 1;
                slideVolume(cs, x, rowTime[k][size_t(row)].first);
                if (index < 0) continue;
                const YmtInstrument& in = instOf(cs.inst);
                if (index >= int(in.drums.size()) || in.drums[size_t(index)].levels.empty() ||
                    !in.drums[size_t(index)].rate) {
                    ++drumMissing;
                    continue;
                }
                const Drum d{&in.drums[size_t(index)]};
                auto it = std::find(drums.begin(), drums.end(), d);
                if (it == drums.end() && drums.size() >= size_t(ymengine::kDigidrumPads)) {  // a pad each
                    ++drumNoSlot;
                    continue;
                }
                if (it == drums.end()) it = drums.insert(drums.end(), d);
                Hit h;
                h.row = row;
                h.channel = c;
                h.slot = int(it - drums.begin());
                h.vol = volOf(cs.vol, quiet);
                // Held for the length of its sample.
                const auto [speed, bpm] = rowTime[k][size_t(row)];
                const double seconds = double(d.d->levels.size()) / d.d->rate, rowSeconds = speed * 2.5 / bpm;
                h.rows = std::max(1, int(std::ceil(seconds / rowSeconds - 1e-9)));
                hits[k].push_back(h);
                if (c != drumChannel) ++zHits[size_t(c)];
            }
        }
    }
    // The Digidrum track takes the channel of most Zxx hits (channel C without any).
    int digiChannel = 2;
    if (*std::max_element(zHits.begin(), zHits.end()) > 0)
        digiChannel = int(std::max_element(zHits.begin(), zHits.end()) - zHits.begin());
    digiChannel = std::clamp(digiChannel, 0, 2);
    for (const auto& hs : hits)
        for (const Hit& h : hs) drumMoved += h.channel != drumChannel && h.channel != digiChannel;

    // ---- Patterns ----
    using ymengine::kSeqMixEnv;
    std::vector<ymseq::Pattern> patterns(ymtOf.size());
    for (size_t k = 0; k < ymtOf.size(); ++k) {
        const int p = ymtOf[k];
        const int rows = flows[size_t(p)].rows;
        ymseq::Pattern& pat = patterns[k];
        pat.steps = rows;
        for (int c = 0; c < channels; ++c) {
            const int t = trackOf[size_t(c)];
            if (t < 0) continue;
            ymseq::Lane& l = pat.edit(t);
            const int octave = state.seq.tracks[size_t(t)].octave;
            const std::vector<NoteRec>& v = notes[k][size_t(c)];
            Chan cs = firstState[k][size_t(c)];
            // Line of each note; -2: the note of the previous position goes on.
            std::vector<int> held(size_t(rows), -1);
            int carriedRelease = -1;
            if (cs.holding) {
                for (int row = 0; row < rows; ++row) {
                    const YmtCell& x = m.cell(p, row, c);
                    if (hasNote(x) || x.note == ymio::kYmtNoteOff || (x.effect == 'K' && x.param == 0)) break;
                    held[size_t(row)] = -2;
                    if (x.effect == 'K' && x.param < rowTime[k][size_t(row)].first) {
                        carriedRelease = x.param;
                        break;
                    }
                }
            }
            for (size_t i = 0; i < v.size(); ++i)
                for (int row = v[i].row; row <= v[i].last; ++row) held[size_t(row)] = int(i);
            bool vibOff = false, modOff = false, volRuns = false;
            for (int row = 0; row < rows; ++row) {
                const YmtCell& x = m.cell(p, row, c);
                const auto s = size_t(row);
                const int speed = rowTime[k][s].first;
                lineVolume(cs, x);
                const int h = held[s];
                if (h >= 0) {
                    const NoteRec& n = v[size_t(h)];
                    const ymengine::TrackPatch& P = instruments[size_t(n.variant)].patch;
                    const int at = row - n.row;
                    if (at == 0) {  // attack: NOTE, OCT, INS, the switches of the track
                        const int midi = std::clamp(n.midi, 12, 131);
                        const int oc = std::clamp(midi / 12 - 1, 0, 9);
                        l.pat[s] = int16_t(std::clamp(midi - (oc + 1) * 12, 0, 12));
                        l.oct[s] = int8_t(ymseq::octaveCode(oc, octave));
                        l.ins[s] = uint8_t(n.variant + 1);
                        l.slide[s] = n.slide;
                        if (P.vibratoOn && P.vibratoCents > 0 && vibOff != (n.at.vib > 0)) {
                            vibOff = n.at.vib > 0;
                            l.setCmd(row, seqCmd(SeqCmd::Vibrato, vibOff ? 0 : 1));
                        }
                        const bool lateArp = n.fx.arp && n.at.arp > 0 && baseFlat[size_t(n.variant)];
                        counts.moved += n.fx.arp && n.at.arp > 0 && !lateArp;
                        counts.moved += n.at.period > 0;
                        if (P.ornamentOn && modOff != lateArp) {
                            modOff = lateArp;
                            l.setCmd(row, seqCmd(SeqCmd::Mod, modOff ? 0 : 1));
                        }
                    } else {  // the effects that start after the attack
                        if (n.fx.vib && at == n.at.vib && vibOff) {
                            vibOff = false;
                            l.setCmd(row, seqCmd(SeqCmd::Vibrato, 1));
                        }
                        if (n.fx.arp && at == n.at.arp && modOff) {
                            modOff = false;
                            l.setCmd(row, seqCmd(SeqCmd::Mod, 1));
                        }
                        if (n.fx.timer && at == n.at.timer) {
                            const Generator g = n.fx.timer == 'U' ? Generator::SyncBuzzer : Generator::Sid;
                            if (g != P.generator) l.fx[s] = uint8_t(g == Generator::Sid ? 2 : 3);
                        }
                        if (n.fx.shape >= 0 && at == n.at.shape) {
                            if (P.envTuned && P.generator != Generator::SyncBuzzer) l.fx[s] = 3;
                            else l.setCmd(row, seqCmd(SeqCmd::Shape, shapeOf(n.fx.shape)));
                        }
                    }
                    // An Xxx after the attack on a Chip Voice without the envelope: the envelope on from its line (SRC),
                    // held lines included.
                    if (n.fx.shape >= 0 && n.at.shape > 0 && at >= n.at.shape && !P.envelopeVolume && !P.envTuned &&
                        P.generator == Generator::Chip)
                        l.mix[s] = uint8_t(kSeqMixEnv + (P.toneOn ? 1 : 0) + (P.noiseOn ? 2 : 0));
                    l.tie[s] = 1;
                    l.vol[s] = uint8_t(volOf(volRow(cs), quiet));
                    if (row == n.last && n.releaseTick > 0)
                        l.gat[s] = uint8_t(std::clamp(int(std::lround(100.0 * n.releaseTick / speed)), 1, 99));
                } else if (h == -2) {  // the note of the previous position goes on
                    l.tie[s] = 1;
                    l.vol[s] = uint8_t(volOf(volRow(cs), quiet));
                    if (carriedRelease > 0 && (row + 1 == rows || held[s + 1] != -2))
                        l.gat[s] = uint8_t(std::clamp(int(std::lround(100.0 * carriedRelease / speed)), 1, 99));
                }
                // Effects of the line.
                const int sub = x.param >> 4, low = x.param & 15;
                const int a = slideVolume(cs, x, speed);
                bool volHere = false;
                switch (x.effect) {
                case 'A':
                case '5':
                case '6':  // the volume slide (the portamento of 5xy and the vibrato of 6xy go on as the note's)
                    if (a) l.setCmd(row, seqCmd(SeqCmd::VolSlide, a >> 4 ? a & 0xF0 : a & 15));
                    volHere = a != 0;
                    break;
                case 'E':
                    if (sub == 0xC) l.setCmd(row, seqCmd(SeqCmd::Cut, low));
                    else if (sub == 0xD) {
                        if (low) l.setCmd(row, seqCmd(SeqCmd::Delay, low));
                    } else if (sub != 0xA && sub != 0xB)
                        ++effects[effectName(x.effect, x.param)];
                    break;
                case 'R': l.setCmd(row, seqCmd(SeqCmd::Retrig, x.param)); break;
                case 0:
                case '0': case '3': case '4': case 'B': case 'C': case 'D': case 'F': case 'K':
                case 'M': case 'P': case 'Q': case 'S': case 'U': case 'X': case 'Y': case 'Z': break;
                default: ++effects[effectName(x.effect, x.param)]; break;
                }
                // A volume slide lasts its line: 00 on the next one.
                if (volRuns && !volHere) l.setCmd(row, seqCmd(SeqCmd::VolSlide, 0));
                volRuns = volHere;
                volumeColumn += x.volume >= 0x60;
            }
        }
        // The Digidrum track: each hit held for the length of its sample (the next hit cuts it).
        if (!hits[k].empty()) {
            std::vector<Hit>& hs = hits[k];
            std::stable_sort(hs.begin(), hs.end(), [](const Hit& a, const Hit& b) { return a.row < b.row; });
            ymseq::Lane& l = pat.edit(3);
            int last = -1;
            for (size_t i = 0; i < hs.size(); ++i) {
                const Hit& h = hs[i];
                if (h.row == last) {
                    ++drumLost;
                    continue;
                }
                last = h.row;
                int end = std::min(rows, h.row + h.rows);
                for (size_t j = i + 1; j < hs.size(); ++j)
                    if (hs[j].row > h.row) {
                        end = std::min(end, hs[j].row);
                        break;
                    }
                const auto s = size_t(h.row);
                l.pat[s] = int16_t(h.slot);
                l.vol[s] = uint8_t(h.vol);
                l.tie[s] = end - h.row > 1;
                for (int row = h.row + 1; row < end; ++row) l.tie[size_t(row)] = 1;
            }
        }
        // A command on a track that has none of its kind on that row (a step holds one of each kind).
        auto place = [&](int row, int from, uint16_t cmd) {
            const SeqCmd kind = ymengine::seqCmdKind(cmd);
            if (from >= 0 && !pat.lane(from).cmdAt(kind, row)) {
                pat.edit(from).setCmd(row, cmd);
                return true;
            }
            for (int t = 0; t < 3; ++t) {
                if (pat.lane(t).cmdAt(kind, row)) continue;
                pat.edit(t).setCmd(row, cmd);
                return true;
            }
            return false;
        };
        if (!ticks) {
            // BPM timing: a tempo change below the first line goes to the BPM row of the pattern.
            int speed = startOf[k].first, bpm = startOf[k].second;
            long tempo = tempoOf(speed, bpm);
            for (int row = 1; row < rows; ++row) {
                for (int c = 0; c < channels; ++c) {
                    const YmtCell& x = m.cell(p, row, c);
                    if (x.effect == 'F' && x.param) (x.param < 0x20 ? speed : bpm) = x.param;
                }
                const long t = tempoOf(speed, bpm);
                if (t == tempo) continue;
                tempo = t;
                const long w = std::clamp(t, long(ymengine::kTempoCmdMin), 255L);
                tempoCmdClamped += w != t;
                pat.bpm[size_t(row)] = uint8_t(w);
                ++tempoCmds;
            }
        } else {
            // Speed (Fxx below $20) in Ticks timing: Sxx.
            for (int row = 0; row < rows; ++row) {
                int speed = 0, from = -1;
                for (int c = 0; c < channels; ++c) {
                    const YmtCell& x = m.cell(p, row, c);
                    if (x.effect == 'F' && x.param && x.param < 0x20) {
                        speed = x.param;
                        from = c < 3 ? trackOf[size_t(c)] : -1;
                    }
                }
                if (speed) speedLost += !place(row, from, seqCmd(SeqCmd::Speed, speed));
            }
        }
    }
    if (!noteCount && std::all_of(hits.begin(), hits.end(), [](const std::vector<Hit>& h) { return h.empty(); })) {
        out.error = "Nothing to import: no notes on the first three channels.";
        return out;
    }

    // ---- Song: one entry per position played, repeats grouped ----
    r.tempo = ticks ? 0.0f : float(std::clamp(tempoOf(speed0, bpm0), 60L, 240L));
    ymseq::Song song;
    int restartEntry = -1;
    for (size_t i = 0; i < visits.size(); ++i) {
        const int k = stymOf[size_t(visits[i].pattern)];
        if (k < 0) continue;
        int bpm = 0;
        if (!ticks) {
            const long t = tempoOf(visits[i].speed, visits[i].bpm);
            if (t != long(r.tempo) || tempoCmds) {
                bpm = int(std::clamp(t, long(ymseq::kSongBpmMin), long(ymseq::kSongBpmMax)));
                tempoClamped += bpm != t;
            }
        }
        const bool restart = int(i) == restartVisit;
        if (!song.entries.empty() && !restart && song.entries.back().pat == k && song.entries.back().bpm == bpm &&
            song.entries.back().rep < ymseq::kSongRepeatMax) {
            ++song.entries.back().rep;
            continue;
        }
        if (restart) restartEntry = song.size();
        song.entries.push_back({k, 1, bpm, ymseq::SongKind::Pattern, 0, false});
    }
    if (restartEntry > 0) song.entries[size_t(restartEntry)].restart = true;

    // ---- Project ----
    for (size_t k = 0; k < patterns.size(); ++k) {
        patterns[k].compact();
        state.seq.patterns[k] = std::move(patterns[k]);
        state.seq.used.set(k);
    }
    state.seq.song = std::move(song);
    for (int t = 0; t < 4; ++t) state.seq.tracks[size_t(t)].length = 0;
    for (size_t i = 0; i < instruments.size(); ++i) state.instruments[i] = std::move(instruments[i]);
    if (!drums.empty()) {
        r.digidrums = true;
        state.global.digidrumChannel[0] = uint8_t(digiChannel);
        const int octave = state.seq.tracks[3].octave;
        for (size_t k = 0; k < drums.size(); ++k) {
            const ymio::YmtDrum& d = *drums[k].d;
            // At its own rate: beyond the range of a slot, the nearest rate whole semitones away, the root note moved
            // by as many (the hits play the rate of the file).
            double rate = d.rate;
            int shift = 0;
            if (rate > ymengine::kDigidrumRateMax) shift = -int(std::ceil(12 * std::log2(rate / ymengine::kDigidrumRateMax) - 1e-9));
            else if (rate < ymengine::kDigidrumRateMin)
                shift = int(std::ceil(12 * std::log2(ymengine::kDigidrumRateMin / rate) - 1e-9));
            rate *= std::pow(2.0, shift / 12.0);
            DigidrumSlotState slot;
            slot.name = cleanDigidrumName(d.name);
            if (slot.name.empty()) slot.name = "Digi " + std::to_string(k + 1);
            slot.play.rateHz = std::clamp(rate, ymengine::kDigidrumRateMin, ymengine::kDigidrumRateMax);
            slot.play.root = uint8_t(std::clamp((octave + 1) * 12 + int(k) + shift, 0, 127));
            slot.convert = DigiConvert::YmLevels;
            std::vector<uint8_t> levels = d.levels;
            for (uint8_t& v : levels) v = std::min<uint8_t>(v, 15);
            slot.source = digidrumSourceOfLevels(levels);
            slot.play.end = uint32_t(levels.size());
            state.digidrums[k] = std::move(levels);
            state.digiSlots[k] = std::move(slot);
        }
        r.samples = int(drums.size());
        state.seq.tracks[3].length = 0;
    }
    state.global.timing = ticks ? ymengine::SeqTiming::Ticks : ymengine::SeqTiming::Bpm;
    state.global.speed = uint8_t(speed0);
    state.global.grooveLength = 1;
    state.global.groove.fill(0);
    r.patterns = int(ymtOf.size());
    r.entries = state.seq.song.size();
    r.instruments = int(variants.size());
    r.notes = noteCount;
    r.speed = speed0;
    int best = 0;
    for (size_t i = 1; i < kVblRates.size(); ++i)
        if (std::abs(kVblRates[i] - tickHz) < std::abs(kVblRates[size_t(best)] - tickHz)) best = int(i);
    r.vblRate = best;
    r.a4Hz = float(ymengine::kA4Hz);

    // ---- What the import does not render ----
    auto& n = r.remarks;
    if (m.chips > 1 || droppedChannels)
        n.push_back("Channels beyond the first three (one chip, Authentic 3ch): " + std::to_string(droppedChannels) + ".");
    if (droppedVisits)
        n.push_back("Patterns beyond 32: " + count(droppedVisits, "Song position", "Song positions") + " left out.");
    if (breaks) n.push_back("Pattern breaks to a later row (played from row 0): " + std::to_string(breaks) + ".");
    if (counts.full)
        n.push_back("Notes beyond 127 instruments, played without their effects: " + std::to_string(counts.full) + ".");
    if (counts.moved)
        n.push_back("Arpeggios and envelope periods that start after their note's attack, played from it: " +
                    std::to_string(counts.moved) + ".");
    if (counts.carried)
        n.push_back("Note effects on rows without a note of this pattern: " + std::to_string(counts.carried) + ".");
    if (slideLost) n.push_back("3xx without a note before it in the pattern: " + std::to_string(slideLost) + ".");
    if (counts.pitchClamped)
        n.push_back("Pitch macros beyond an octave and 127 period steps, clamped: " +
                    count(counts.pitchClamped, "instrument", "instruments") + ".");
    if (counts.tableCut)
        n.push_back("Macros that do not repeat within 64 ticks, held at the 64th: " +
                    count(counts.tableCut, "instrument", "instruments") + ".");
    if (counts.sidClamped)
        n.push_back("SID rates beyond 1/4 to 8 times the note, clamped: " +
                    count(counts.sidClamped, "instrument", "instruments") + ".");
    if (counts.buzzRate)
        n.push_back("Sync-Buzzer (Uxx) rates following the note: " +
                    count(counts.buzzRate, "instrument", "instruments") + ".");
    if (drumMoved)
        n.push_back(std::string("Digidrum hits of other channels, played on channel ") + char('A' + digiChannel) + ": " +
                    std::to_string(drumMoved) + ".");
    if (drumLost) n.push_back("Digidrum hits on the same row as another: " + std::to_string(drumLost) + ".");
    if (drumNoSlot) n.push_back("Digidrum hits beyond 8 samples: " + std::to_string(drumNoSlot) + ".");
    if (drumMissing) n.push_back("Digidrum hits without a sample in the bank: " + std::to_string(drumMissing) + ".");
    if (tempoClamped) n.push_back("Tempos clamped to 20-300 BPM: " + std::to_string(tempoClamped) + ".");
    if (tempoCmdClamped) n.push_back("BPM row tempos clamped to 32-255 BPM: " + std::to_string(tempoCmdClamped) + ".");
    if (speedLost) n.push_back("Speed changes without a free CMD cell: " + std::to_string(speedLost) + ".");
    if (volumeColumn) n.push_back("Volume column effects left out: " + std::to_string(volumeColumn) + ".");
    if (quiet)
        n.push_back("Volumes below the one of VOL 1 (" + std::to_string(attenuatedVolume(15, 1 / 15.0)) +
                    " of 15), played at it: " + count(quiet, "row", "rows") + ".");
    if (m.unknownEffects) n.push_back("Unknown effects left out: " + std::to_string(m.unknownEffects) + ".");
    if (!effects.empty()) {  // four per line; a continuation line starts with a space
        std::string line = "Effects left out: ";
        int inLine = 0;
        for (const auto& [name, times] : effects) {
            if (inLine == 4) {
                n.push_back(line + ",");
                line = " ";
                inLine = 0;
            }
            line += (inLine ? ", " : "") + name + " (" + std::to_string(times) + ")";
            ++inLine;
        }
        n.push_back(line + ".");
    }
    out.import = std::move(r);
    return out;
}

std::vector<std::string> ymtImportMessage(const RegisterImport& r, const std::string& fileName) {
    std::vector<std::string> lines = {"YM-Tracker song imported", "", fileName};
    if (!r.title.empty()) lines.push_back(r.title);
    lines.push_back(std::to_string(3 + r.digidrums) + " tracks, " + count(r.patterns, "pattern", "patterns") + ", " +
                    count(r.instruments, "instrument", "instruments") + ", " +
                    count(r.entries, "Song entry", "Song entries") + ".");
    if (r.digidrums) lines.push_back("Digidrum samples on track 4: " + std::to_string(r.samples) + ".");
    if (r.tempo > 0)
        lines.push_back("Tempo " + std::to_string(int(r.tempo)) + " BPM, replay at " +
                        std::to_string(kVblRates[size_t(r.vblRate)]) + " Hz, Authentic 3ch.");
    else
        lines.push_back("Ticks: speed " + std::to_string(r.speed) + " at " +
                        std::to_string(kVblRates[size_t(r.vblRate)]) + " Hz, Authentic 3ch.");
    if (!r.remarks.empty()) {
        lines.emplace_back("");
        lines.emplace_back("Not as in the song:");
        for (const std::string& n : r.remarks) lines.push_back(n.front() == ' ' ? "  " + n.substr(1) : "• " + n);
    }
    return lines;
}

}  // namespace stym::plugin
