// STYM2149 — plugincore: import of XM, MOD, S3M and OKT modules
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/XmImport.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "plugincore/Digidrum.h"
#include "plugincore/DmaSample.h"
#include "plugincore/Instruments.h"
#include "plugincore/Mapping.h"
#include "ymengine/Dma.h"
#include "ymio/Mod.h"
#include "ymio/Okt.h"
#include "ymio/S3m.h"
#include "ymio/Xm.h"

namespace stym::plugin {

using ymengine::kDmaRateHz;
using ymengine::kDmaRates;
using ymengine::kDmaSlotsMax;
using ymengine::VoiceMode;
using ymio::XmCell;
using ymio::XmModule;
using ymio::XmSample;

namespace {

// XM effects (effect column) rendered or read by the import; subcommands of Exy.
constexpr uint8_t kFxArp = 0x0, kFxPan = 0x8, kFxOffset = 0x9, kFxVolSlide = 0xA, kFxJump = 0xB, kFxVolume = 0xC,
                  kFxBreak = 0xD, kFxExtra = 0xE, kFxSpeed = 0xF;
constexpr uint8_t kExFine = 0x5, kExCut = 0xC, kExDelay = 0xD;
constexpr int kXmBpmMin = 32, kXmBpmMax = 255;  // module tempo range (Fxx from $20)

std::string count(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// Name of an effect left out: "1xx", "Axx", "E9x" (Exy, per subcommand).
std::string effectName(uint8_t effect, uint8_t param) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    const char c = effect < 10 ? char('0' + effect) : char('A' + effect - 10);
    if (effect == kFxExtra) return std::string{'E', kHex[param >> 4], 'x'};
    return std::string{c, 'x', 'x'};
}

bool hasNote(const XmCell& x) noexcept { return x.note >= 1 && x.note <= 96; }

// Lines played of a pattern: up to the first jump (Bxx) or pattern break (Dxx), included.
struct Flow {
    int rows = 64;
    int jump = -1;     // Bxx: next position in the order table
    int breakRow = 0;  // Dxx: start line of the next position (in decimal in the XM)
};

Flow flowOf(const XmModule& m, int p) {
    Flow f;
    f.rows = m.patterns[size_t(p)].rows;
    for (int r = 0; r < f.rows; ++r) {
        bool stop = false;
        for (int c = 0; c < m.channels; ++c) {
            const XmCell& x = m.cell(p, r, c);
            if (x.effect == kFxJump) {
                f.jump = x.param;
                stop = true;
            } else if (x.effect == kFxBreak) {
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

// Sample of an instrument (1 to n) for a note (1 to 96): its note table; without a note, the first one.
int sampleIndex(const XmModule& m, int inst, int note) noexcept {
    if (inst < 1 || inst > int(m.instruments.size())) return -1;
    const ymio::XmInstrument& ins = m.instruments[size_t(inst - 1)];
    const int k = note >= 1 && note <= 96 ? ins.keymap[size_t(note - 1)] : 0;
    return k < int(ins.samples.size()) ? k : -1;
}

bool usable(const XmSample& s) noexcept { return !s.adpcm && !s.data.empty(); }

// State of a channel line after line (tick 0 of Fasttracker II: instrument, note, volume column, effect).
struct Chan {
    int inst = 0, note = 0;
    int vol = 64, pan = 128;
};

// Channels before the first line: full volume, the module's pan (a MOD: the Amiga's L R R L).
std::vector<Chan> startChannels(const XmModule& m) {
    std::vector<Chan> ch(size_t(m.channels));
    for (size_t c = 0; c < ch.size() && c < m.channelPan.size(); ++c) ch[c].pan = m.channelPan[c];
    return ch;
}

void applyRow(const XmModule& m, Chan& ch, const XmCell& x) noexcept {
    if (x.instrument) ch.inst = x.instrument;
    if (hasNote(x)) ch.note = x.note;
    if (x.instrument) {  // the instrument resets the volume of its sample, and in an XM its pan
        const int k = sampleIndex(m, ch.inst, ch.note);
        if (k >= 0) {
            const XmSample& s = m.instruments[size_t(ch.inst - 1)].samples[size_t(k)];
            ch.vol = s.volume;
            if (m.samplePanning) ch.pan = s.panning;
        }
    }
    // Volume column: $10-$50 sets the volume (0-64), $Cx the pan (x × 16).
    if (x.volume >= 0x10 && x.volume <= 0x50) ch.vol = x.volume - 0x10;
    else if ((x.volume & 0xF0) == 0xC0) ch.pan = (x.volume & 15) * 16;
    if (x.effect == kFxVolume) ch.vol = std::min<int>(x.param, 64);
    else if (x.effect == kFxPan) ch.pan = x.param;
}

// Sample of a DMA slot: signed 8-bit at the STE rate closest to that of its C4.
struct DmaBuild {
    std::vector<int8_t> bytes;
    uint8_t rate = 0;
    bool loop = false;
    bool unrolled = false;  // loop that does not start at the beginning: attack and loop passes unrolled, no loop
    double scale = 1;       // frames of the DMA sample per frame of the XM sample (9xx: Oxx)
};

// Length of an unrolled loop: 32 KB, the former length of a DMA sample (Claude's choice).
inline constexpr size_t kUnrollBytes = 32 * 1024;

DmaBuild buildDma(const XmSample& s, double c4Hz) {
    DmaBuild b;
    // Rate of the sample's C4: that of the module's C-4, relative note in semitones, fine tune in 1/128 of a
    // semitone.
    const double src = c4Hz * std::pow(2.0, (s.relativeNote + s.finetune / 128.0) / 12.0);
    std::vector<float> x(s.data.size());
    for (size_t i = 0; i < x.size(); ++i) x[i] = float(s.data[i]) / 32768.0f;
    const size_t ls = s.loopStart, le = size_t(s.loopStart) + s.loopLength;
    const bool looped = s.loopType != 0 && s.loopLength > 0;
    std::vector<float> cycle;  // one loop pass (forward then backward for ping-pong)
    if (looped) {
        cycle.assign(x.begin() + std::ptrdiff_t(ls), x.begin() + std::ptrdiff_t(le));
        // Ping-pong: the way back leaves out both ends, which the forward pass already plays.
        if (s.loopType == 2)
            for (size_t k = le - 1; k-- > ls + 1;) cycle.push_back(x[k]);
    }
    std::vector<float> seq;
    size_t core = 0;  // frames that must fit: the sample, or the attack and one loop pass
    if (!looped) {
        seq = std::move(x);
        core = seq.size();
    } else if (ls == 0) {
        seq = cycle;
        core = seq.size();
        b.loop = true;
    } else {
        seq.assign(x.begin(), x.begin() + std::ptrdiff_t(ls));
        core = ls + cycle.size();
        b.unrolled = true;
    }
    // The STE rate closest to the C4 rate (the whole sample kept: the memory of the chosen STE limits the
    // samples in the 3-channel modes, SteMemory.h).
    int best = 0;
    for (int k = 1; k < kDmaRates; ++k)
        if (std::abs(kDmaRateHz[size_t(k)] - src) < std::abs(kDmaRateHz[size_t(best)] - src)) best = k;
    b.rate = uint8_t(best);
    const double rate = kDmaRateHz[b.rate];
    b.scale = rate / src;
    if (b.unrolled) {  // attack and loop passes up to kUnrollBytes, at least one whole pass
        const double need = std::max(double(kUnrollBytes) * src / rate + 2, double(core));
        while (double(seq.size()) < need) seq.insert(seq.end(), cycle.begin(), cycle.end());
    }
    b.bytes = toDmaSample(seq, {}, src, rate);
    return b;
}

}  // namespace

std::vector<int> xmImportTracks(VoiceMode mode, int channels) {
    // 8 channels or fewer in 16 or 32 ch: the tracks of the DMA in 3ch (Indus, 29/10/2026 20:01).
    if (studioMode(mode) && channels <= ymengine::kDmaTracks) mode = VoiceMode::Authentic3;
    const int n = studioMode(mode) ? studioTracks(mode) : ymengine::kDmaTracks;
    std::vector<int> out;
    const int first = 4 * chipCount(mode);
    for (int t = 0; t < n; ++t) out.push_back(first + t);
    return out;
}

namespace {

// The conversion of a module read (an XM, or a MOD, S3M or OKT module in the XM shape).
XmImportResult convert(const XmModule& m, VoiceMode mode, NonParamState& state) {
    XmImportResult out;
    if (m.patterns.empty() || m.order.empty()) {
        out.error = "Nothing to import: no notes with a sample.";
        return out;
    }
    XmImport r;
    r.title = m.name;
    std::map<std::string, int> effects;  // effects left out
    int volumeColumn = 0, tempoClamped = 0, breaks = 0, speedLost = 0;
    int tempoCmds = 0, tempoCmdClamped = 0;
    int notesNoSample = 0, notesNoSlot = 0;

    // ---- Song: order table walked as Fasttracker II plays it ----
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
            if (pos >= int(m.order.size())) {  // end of the table: restart at the restart position of the module
                if (m.restart < int(seen.size()) && seen[size_t(m.restart)] >= 0) restartVisit = seen[size_t(m.restart)];
                break;
            }
            if (seen[size_t(pos)] >= 0) {  // going back: restart point
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
    // Patterns met → patterns 1 to 32.
    std::vector<int> stymOf(m.patterns.size(), -1), xmOf;
    int droppedVisits = 0;
    for (const Visit& v : visits) {
        if (stymOf[size_t(v.pattern)] < 0 && int(xmOf.size()) < ymseq::kPatterns) {
            stymOf[size_t(v.pattern)] = int(xmOf.size());
            xmOf.push_back(v.pattern);
        }
        droppedVisits += stymOf[size_t(v.pattern)] < 0;
    }
    // Channels that play notes → tracks.
    std::vector<bool> noted(size_t(m.channels), false);
    for (int p : xmOf)
        for (int row = 0; row < flows[size_t(p)].rows; ++row)
            for (int c = 0; c < m.channels; ++c) noted[size_t(c)] = noted[size_t(c)] || hasNote(m.cell(p, row, c));
    const int notedCount = int(std::count(noted.begin(), noted.end(), true));
    const std::vector<int> tracks = xmImportTracks(mode, notedCount);
    // More channels than tracks (Indus, 29/10/2026 19:48): the rows where each channel sounds, from a note to
    // its release, the end of its sample (without a loop), a cut (ECx) or its next note; a row of a pattern sounds when
    // it does in one of its visits.
    std::vector<size_t> base(xmOf.size() + 1, 0);  // first row of each pattern, the patterns end to end
    for (size_t k = 0; k < xmOf.size(); ++k) base[k + 1] = base[k] + size_t(flows[size_t(xmOf[k])].rows);
    std::vector<std::vector<bool>> sounds(size_t(m.channels));
    if (notedCount > int(tracks.size())) {
        for (std::vector<bool>& v : sounds) v.assign(base.back(), false);
        constexpr double kForever = 1e30;
        std::vector<double> left(size_t(m.channels), 0);  // seconds the channel still sounds
        std::vector<int> inst(size_t(m.channels), 0);
        int speed = std::clamp(m.tempo, 1, ymengine::kSpeedMax), bpm = std::clamp(m.bpm, kXmBpmMin, kXmBpmMax);
        for (const Visit& v : visits) {
            const int k = stymOf[size_t(v.pattern)];
            for (int row = 0; row < flows[size_t(v.pattern)].rows; ++row) {
                for (int c = 0; c < m.channels; ++c) {  // the line lasts at the speed and tempo of its Fxx
                    const XmCell& x = m.cell(v.pattern, row, c);
                    if (x.effect == kFxSpeed && x.param) (x.param < 0x20 ? speed : bpm) = x.param;
                }
                const double line = speed * 2.5 / bpm;
                for (int c = 0; c < m.channels; ++c) {
                    const XmCell& x = m.cell(v.pattern, row, c);
                    double& l = left[size_t(c)];
                    if (x.instrument) inst[size_t(c)] = x.instrument;
                    if (hasNote(x)) {
                        l = 0;
                        const int s = sampleIndex(m, inst[size_t(c)], x.note);
                        if (s >= 0) {
                            const XmSample& sm = m.instruments[size_t(inst[size_t(c)] - 1)].samples[size_t(s)];
                            const double hz =
                                m.c4Hz * std::pow(2.0, (x.note - 49 + sm.relativeNote + sm.finetune / 128.0) / 12.0);
                            const bool looped = sm.loopType != 0 && sm.loopLength > 0;
                            if (usable(sm)) l = looped ? kForever : double(sm.data.size()) / hz;
                        }
                    } else if (x.note == ymio::kXmKeyOff) {
                        l = 0;
                    }
                    if (k >= 0 && l > 0) sounds[size_t(c)][base[size_t(k)] + size_t(row)] = true;
                    if (x.effect == kFxExtra && x.param >> 4 == kExCut) l = 0;
                    if (l < kForever) l = std::max(0.0, l - line);
                }
            }
        }
    }
    // Channels in order, one per track while there are enough tracks for those to come; otherwise on the first track
    // whose channels never sound on its rows, or the next free track; a channel that finds neither is left out.
    std::vector<int> trackOf(size_t(m.channels), -1);
    std::array<int, kTracks> onTrack{};  // channels of each track
    std::vector<std::vector<bool>> busy;  // rows where the channels of tracks[i] sound
    int used = 0, droppedChannels = 0, sharing = 0, toCome = notedCount;
    for (int c = 0; c < m.channels; ++c) {
        if (!noted[size_t(c)]) continue;
        int i = -1;
        if (toCome-- > int(tracks.size()) - used)
            for (int j = 0; j < used && i < 0; ++j) {
                bool clash = false;
                for (size_t row = 0; row < busy[size_t(j)].size() && !clash; ++row)
                    clash = busy[size_t(j)][row] && sounds[size_t(c)][row];
                if (!clash) i = j;
            }
        if (i >= 0) {
            ++sharing;
            for (size_t row = 0; row < busy[size_t(i)].size(); ++row)
                if (sounds[size_t(c)][row]) busy[size_t(i)][row] = true;
        } else if (used < int(tracks.size())) {
            i = used++;
            busy.push_back(sounds[size_t(c)]);
        } else {
            ++droppedChannels;
            continue;
        }
        trackOf[size_t(c)] = tracks[size_t(i)];
        ++onTrack[size_t(trackOf[size_t(c)])];
    }

    // ---- Walk: notes played per instrument and sample, tempo, channel state at the start of the patterns ----
    std::vector<std::vector<int>> plays(m.instruments.size());
    for (size_t i = 0; i < m.instruments.size(); ++i) plays[i].assign(m.instruments[i].samples.size(), 0);
    std::vector<std::vector<Chan>> firstState(xmOf.size());
    std::vector<int> bpms;
    {
        std::vector<Chan> ch = startChannels(m);
        int speed = std::clamp(m.tempo, 1, ymengine::kSpeedMax), bpm = std::clamp(m.bpm, kXmBpmMin, kXmBpmMax);
        bpms.push_back(bpm);
        for (Visit& v : visits) {
            const int k = stymOf[size_t(v.pattern)];
            if (k >= 0 && firstState[size_t(k)].empty()) firstState[size_t(k)] = ch;
            for (int row = 0; row < flows[size_t(v.pattern)].rows; ++row) {
                for (int c = 0; c < m.channels; ++c) {
                    const XmCell& x = m.cell(v.pattern, row, c);
                    applyRow(m, ch[size_t(c)], x);
                    if (hasNote(x) && k >= 0 && trackOf[size_t(c)] >= 0) {
                        const int s = sampleIndex(m, ch[size_t(c)].inst, x.note);
                        if (s >= 0) ++plays[size_t(ch[size_t(c)].inst - 1)][size_t(s)];
                    }
                    // Fxx: below $20 a speed (ticks per line), otherwise a tempo (BPM); F00 is left out.
                    if (x.effect != kFxSpeed || x.param == 0) continue;
                    if (x.param < 0x20) speed = x.param;
                    else bpm = x.param;
                    if (std::find(bpms.begin(), bpms.end(), bpm) == bpms.end()) bpms.push_back(bpm);
                }
                if (row == 0) {
                    v.speed = speed;
                    v.bpm = bpm;
                }
            }
        }
    }

    // Speed and tempo of the first visit of each pattern, its first line included (Txx).
    std::vector<std::pair<int, int>> startOf(xmOf.size(), {0, 0});
    for (const Visit& v : visits) {
        const int k = stymOf[size_t(v.pattern)];
        if (k >= 0 && !startOf[size_t(k)].first) startOf[size_t(k)] = {v.speed, v.bpm};
    }

    // ---- Instruments: the most played that have a sample ----
    struct Pick {
        int inst = 0, sample = 0, plays = 0;
    };
    std::vector<Pick> picks;
    std::vector<bool> sampled(m.instruments.size() + 1, false);  // instrument that has a readable sample
    for (size_t i = 0; i < m.instruments.size(); ++i) {
        const ymio::XmInstrument& ins = m.instruments[i];
        Pick p{int(i) + 1, -1, 0};
        for (size_t s = 0; s < ins.samples.size(); ++s) {
            p.plays += plays[i][s];
            if (usable(ins.samples[s]) && (p.sample < 0 || plays[i][s] > plays[i][size_t(p.sample)])) p.sample = int(s);
        }
        sampled[i + 1] = p.sample >= 0;
        if (p.plays > 0 && p.sample >= 0) picks.push_back(p);
    }
    std::stable_sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.plays > b.plays; });
    // The most played keep an instrument and its DMA slot (127 at most, the instruments of a project; 8,
    // then 32 in earlier versions), then go back to the order of the module.
    constexpr size_t kPicks = std::min<size_t>(ymengine::kInstruments, kDmaSlotsMax);
    const int noSlot = int(std::max(picks.size(), kPicks) - kPicks);
    if (picks.size() > kPicks) picks.resize(kPicks);
    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.inst < b.inst; });
    if (picks.empty()) {
        out.error = "Nothing to import: no notes with a sample.";
        return out;
    }
    std::vector<int> slotOf(m.instruments.size() + 1, -1);
    for (size_t k = 0; k < picks.size(); ++k) slotOf[size_t(picks[k].inst)] = int(k);

    // ---- Samples (before the patterns: the sample offsets follow their rate) ----
    int unrolled = 0;
    std::vector<DmaBuild> builds;
    for (const Pick& p : picks) {
        const XmSample& s = m.instruments[size_t(p.inst - 1)].samples[size_t(p.sample)];
        builds.push_back(buildDma(s, m.c4Hz));
        unrolled += builds.back().unrolled;
    }

    // ---- Patterns ----
    // Ticks timing: a single module tempo, on a VBL rate (tick of 2.5 / tempo seconds, i.e. tempo × 2 / 5 Hz).
    r.ticks = bpms.size() == 1 && bpms[0] * 2 % 5 == 0 &&
              std::find(kVblRates.begin(), kVblRates.end(), uint32_t(bpms[0] * 2 / 5)) != kVblRates.end();
    const double stepsPerBeat =
        ymengine::kStepsPerBeat[std::min<size_t>(state.global.resolution, ymengine::kSeqResolutions - 1)];
    // One line per step: 60 / (line length × steps per beat) = 24 × bpm / (speed × steps per beat).
    auto tempoOf = [stepsPerBeat](int speed, int bpm) {  // a line lasts speed × 2.5 / module tempo seconds
        return std::lround(24.0 * bpm / (speed * stepsPerBeat));
    };
    int otherSample = 0, offsetClamped = 0;
    std::vector<bool> panned(size_t(m.channels), false);
    std::vector<ymseq::Pattern> patterns(xmOf.size());
    const auto spare = std::make_unique<ymseq::Lane>();
    for (size_t k = 0; k < xmOf.size(); ++k) {
        const int p = xmOf[k];
        const int rows = flows[size_t(p)].rows;
        ymseq::Pattern& pat = patterns[k];
        pat.steps = rows;
        std::vector<Chan> ch = firstState[k];
        for (int c = 0; c < m.channels; ++c) {
            const int t = trackOf[size_t(c)];
            if (t < 0) continue;
            ymseq::Lane& lane = pat.edit(t);
            const bool alone = onTrack[size_t(t)] == 1;
            const int octave = state.seq.tracks[size_t(t)].octave;
            bool holding = true;  // first lines: the note of the previous position continues
            // Step effects: memories of the parameters (xx = 00: the previous one), slide that runs (an XM slide
            // lasts its line, the slides of STYM2149 until their 00) and fine tune of the last note. The arpeggio 0xy
            // and the portamentos 1xx and 2xx are left out: their commands are retired.
            int volMem = 0, offsetMem = 0;
            bool volRuns = false, tuned = false;
            for (int row = 0; row < rows; ++row) {
                const XmCell& x = m.cell(p, row, c);
                Chan& cs = ch[size_t(c)];
                applyRow(m, cs, x);
                const auto s = size_t(row);
                // A shared track takes the channel's lines where it sounds; the others go to a spare lane.
                ymseq::Lane& l = alone || sounds[size_t(c)][base[k] + s] ? lane : *spare;
                if (hasNote(x)) {
                    const bool known = cs.inst >= 1 && cs.inst <= int(m.instruments.size());
                    const int slot = known ? slotOf[size_t(cs.inst)] : -1;
                    holding = slot >= 0;
                    if (!holding) ++(known && sampled[size_t(cs.inst)] ? notesNoSlot : notesNoSample);
                    if (holding) {
                        const int midi = x.note + 11;  // C-4 (49): C4, MIDI note 60
                        const int oc = std::clamp(midi / 12 - 1, 0, 9);
                        l.pat[s] = int16_t(std::clamp(midi - (oc + 1) * 12, 0, 12));
                        l.oct[s] = int8_t(ymseq::octaveCode(oc, octave));
                        l.ins[s] = uint8_t(slot + 1);
                        l.vol[s] = uint8_t(std::clamp(int(std::lround(cs.vol * 15 / 64.0)), 0, 15));  // 0-64 → 0-15
                        l.tie[s] = 1;
                        otherSample += sampleIndex(m, cs.inst, x.note) != picks[size_t(slot)].sample;
                    }
                } else if (x.note == ymio::kXmKeyOff) {
                    holding = false;
                } else if (holding) {  // the volume of the channel at each line of the note (VOL of a held step)
                    l.tie[s] = 1;
                    l.vol[s] = uint8_t(std::clamp(int(std::lround(cs.vol * 15 / 64.0)), 0, 15));
                }
                // XM pan 0-255 (centre 128) → -10 to 10, written in PNR as v·10 + kPanCenter.
                if (holding) {
                    const int v = std::clamp(int(std::lround((cs.pan - 128) / 12.8)), -10, 10);
                    l.pnr[s] = uint8_t(v * 10 + ymseq::kPanCenter);
                    panned[size_t(c)] = panned[size_t(c)] || v != 0;
                }
                using ymengine::SeqCmd;
                using ymengine::seqCmd;
                const int sub = x.param >> 4, low = x.param & 15;
                const int slot = cs.inst >= 1 && cs.inst <= int(m.instruments.size()) ? slotOf[size_t(cs.inst)] : -1;
                bool volHere = false;
                // An effect of an S3M or OKT module kept out of the XM shape, by its own name.
                if (x.foreign) ++effects[m.foreignEffects[size_t(x.foreign - 1)]];
                if (x.effect == kFxArp && x.param == 0) {  // 000: no effect
                } else if (x.effect == kFxSpeed) {
                    if (x.param == 0) ++effects["F00"];
                } else if (x.effect == kFxVolSlide) {  // Axy: up x (first) or down y, units of 1/64 ≈ 1/60
                    const int v = x.param ? x.param : volMem;
                    volMem = v;
                    if (v) l.setCmd(row, seqCmd(SeqCmd::VolSlide, v >> 4 ? v & 0xF0 : v & 15));
                    volHere = v != 0;
                } else if (x.effect == kFxOffset) {  // 9xx: xx × 256 frames of the XM sample, at the DMA rate
                    const int v = x.param ? x.param : offsetMem;
                    offsetMem = v;
                    if (v && hasNote(x) && holding && slot >= 0) {
                        const long o = std::lround(v * builds[size_t(slot)].scale);
                        offsetClamped += o > 255;
                        l.setCmd(row, seqCmd(SeqCmd::Offset, int(std::min(o, 255L))));
                    }
                } else if (x.effect == kFxExtra && sub == kExDelay) {  // EDx: Dxx (ED0: no delay)
                    if (low) l.setCmd(row, seqCmd(SeqCmd::Delay, low));
                } else if (x.effect == kFxExtra && sub == kExCut) {  // ECx: Cxx (EC0: at once, as C00)
                    l.setCmd(row, seqCmd(SeqCmd::Cut, low));
                } else if (x.effect == kFxExtra && sub == kExFine) {  // E5x: the fine tune of the note's sample
                    if (hasNote(x) && holding && slot >= 0) {
                        const ymio::XmInstrument& in = m.instruments[size_t(cs.inst - 1)];
                        const XmSample& sm = in.samples[size_t(picks[size_t(slot)].sample)];
                        const long cents = std::lround(((low - 8) * 16 - sm.finetune) * 100 / 128.0);
                        l.setCmd(row, seqCmd(SeqCmd::Fine, int(std::clamp(cents, -100L, 100L)) +
                                                               ymengine::kFineCmdCenter));
                        tuned = true;
                    }
                } else if (x.effect != kFxPan && x.effect != kFxVolume && x.effect != kFxJump && x.effect != kFxBreak) {
                    ++effects[effectName(x.effect, x.param)];
                }
                // An XM slide stops at the end of its line: 00 on the next line; an E5x lasts its note.
                if (volRuns && !volHere) l.setCmd(row, seqCmd(SeqCmd::VolSlide, 0));
                volRuns = volHere;
                if (tuned && hasNote(x) && !(x.effect == kFxExtra && sub == kExFine)) {
                    l.setCmd(row, seqCmd(SeqCmd::Fine, ymengine::kFineCmdCenter));
                    tuned = false;
                }
                // Volume column effects ($60 and up) other than the pan ($Cx) are not rendered.
                volumeColumn += x.volume >= 0x60 && (x.volume < 0xC0 || x.volume >= 0xD0);
            }
        }
        // A command on the channel's track, otherwise on an imported track that has no command of its kind on that
        // row (a step holds one of each kind).
        auto place = [&](int row, int from, uint16_t cmd) {
            const ymengine::SeqCmd kind = ymengine::seqCmdKind(cmd);
            if (from >= 0 && !pat.lane(from).cmdAt(kind, row)) {
                pat.edit(from).setCmd(row, cmd);
                return true;
            }
            for (int t : tracks) {
                const bool imported = std::find(trackOf.begin(), trackOf.end(), t) != trackOf.end();
                if (!imported || pat.lane(t).cmdAt(kind, row)) continue;
                pat.edit(t).setCmd(row, cmd);
                return true;
            }
            return false;
        };
        if (!r.ticks) {
            // BPM timing: a tempo change below the first line (Fxx, speed or tempo) goes to the BPM row of the pattern
            // at its line (twelfth DAW test, item 150), from the speed and tempo of the first visit of the
            // pattern (its first line is the tempo of the entry).
            int speed = startOf[k].first, bpm = startOf[k].second;
            long tempo = tempoOf(speed, bpm);
            for (int row = 1; row < rows; ++row) {
                for (int c = 0; c < m.channels; ++c) {
                    const XmCell& x = m.cell(p, row, c);
                    if (x.effect != kFxSpeed || !x.param) continue;
                    (x.param < 0x20 ? speed : bpm) = x.param;
                }
                const long t = tempoOf(speed, bpm);
                if (t == tempo) continue;
                tempo = t;
                const long v = std::clamp(t, long(ymengine::kTempoCmdMin), 255L);
                tempoCmdClamped += v != t;
                pat.bpm[size_t(row)] = uint8_t(v);
                ++tempoCmds;
            }
            continue;
        }
        // Speed (Fxx < $20) in Ticks timing: Sxx.
        for (int row = 0; row < rows; ++row) {
            int speed = 0, from = -1;
            for (int c = 0; c < m.channels; ++c) {
                const XmCell& x = m.cell(p, row, c);
                if (x.effect == kFxSpeed && x.param && x.param < 0x20) {
                    speed = x.param;
                    from = trackOf[size_t(c)];
                }
            }
            if (speed) speedLost += !place(row, from, ymengine::seqCmd(ymengine::SeqCmd::Speed, speed));
        }
    }
    // Pan: only on the tracks of channels that move away from the centre (otherwise the track's).
    std::array<bool, kTracks> panTrack{};
    for (int c = 0; c < m.channels; ++c)
        if (trackOf[size_t(c)] >= 0 && panned[size_t(c)]) panTrack[size_t(trackOf[size_t(c)])] = true;
    for (int t : tracks) {
        if (!onTrack[size_t(t)] || panTrack[size_t(t)]) continue;
        for (ymseq::Pattern& pat : patterns)
            if (pat.lanes[size_t(t)]) pat.edit(t).pnr.fill(0);
    }

    // ---- Song: one entry per position played, repeats grouped ----
    // With Txx, every entry has its own tempo: the tempo of a Txx holds until an entry that has one.
    r.tempo = float(std::clamp(tempoOf(visits[0].speed, visits[0].bpm), 60L, 240L));
    ymseq::Song song;
    int restartEntry = -1;
    for (size_t i = 0; i < visits.size(); ++i) {
        const int k = stymOf[size_t(visits[i].pattern)];
        if (k < 0) continue;
        int bpm = 0;
        if (!r.ticks) {
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

    int adpcm = 0;
    for (const ymio::XmInstrument& ins : m.instruments)
        for (const XmSample& s : ins.samples) adpcm += s.adpcm;

    // ---- Project ----
    for (size_t k = 0; k < patterns.size(); ++k) {
        patterns[k].compact();
        state.seq.patterns[k] = std::move(patterns[k]);
        state.seq.used.set(k);
    }
    state.seq.song = std::move(song);
    for (size_t k = 0; k < picks.size(); ++k) {
        const ymio::XmInstrument& ins = m.instruments[size_t(picks[k].inst - 1)];
        Instrument& dst = state.instruments[k];
        dst = Instrument{};
        dst.used = true;
        dst.name = cleanInstrumentName(ins.name);
        if (dst.name.empty()) dst.name = cleanInstrumentName(ins.samples[size_t(picks[k].sample)].name);
        dst.patch.generator = ymengine::Generator::Dma;
        dst.patch.dmaSlot = int16_t(k);
        state.dma.slots[k] = {builds[k].rate, false, 60, builds[k].loop};  // mono, root note 60
        state.dmaSamples[k] = std::move(builds[k].bytes);
        // Name of the slot: that of the sample, otherwise that of the instrument.
        state.dmaNames[k] = cleanDigidrumName(ins.samples[size_t(picks[k].sample)].name);
        if (state.dmaNames[k].empty()) state.dmaNames[k] = cleanDigidrumName(ins.name);
    }
    state.global.timing = r.ticks ? ymengine::SeqTiming::Ticks : ymengine::SeqTiming::Bpm;
    if (r.ticks) {
        state.global.speed = uint8_t(std::clamp(m.tempo, 1, ymengine::kSpeedMax));
        r.speed = state.global.speed;
        r.vblRate = int(std::find(kVblRates.begin(), kVblRates.end(), uint32_t(bpms[0] * 2 / 5)) - kVblRates.begin());
    }
    r.channels = used;
    r.shared = sharing;
    r.played = notedCount;
    r.mode = mode;
    r.patterns = int(xmOf.size());
    r.entries = state.seq.song.size();
    r.instruments = int(picks.size());
    for (int c = 0; c < m.channels; ++c)
        if (trackOf[size_t(c)] >= 0) r.on[size_t(trackOf[size_t(c)])] = true;

    // ---- What the import does not render ----
    auto& n = r.notes;
    n = m.notes;  // what the reader of the module's format left out (AdLib of an S3M)
    if (sharing)
        n.push_back("Channels on the track of one that never plays at the same time: " + std::to_string(sharing) +
                    ".");
    if (droppedChannels)
        n.push_back("Channels beyond the " + std::to_string(tracks.size()) + " DMA tracks: " +
                    std::to_string(droppedChannels) + " left out.");
    if (droppedVisits)
        n.push_back("Patterns beyond 32: " + count(droppedVisits, "Song position", "Song positions") + " left out.");
    if (noSlot)
        n.push_back("Instruments beyond the 127 of a project: " + std::to_string(noSlot) + ", " +
                    count(notesNoSlot, "note", "notes") + " left out.");
    if (notesNoSample) n.push_back("Notes without a sample: " + std::to_string(notesNoSample) + ".");
    if (adpcm) n.push_back("ADPCM samples (not read): " + std::to_string(adpcm) + ".");
    if (otherSample)
        n.push_back("Notes played with their instrument's main sample: " + std::to_string(otherSample) + ".");
    if (m.truncatedSamples) n.push_back("Sample data cut by the end of the file.");
    if (unrolled)
        n.push_back("Loops unrolled to 32 KB (the STE loops whole samples): " + std::to_string(unrolled) + ".");
    if (tempoClamped) n.push_back("Tempos clamped to 20-300 BPM: " + std::to_string(tempoClamped) + ".");
    if (tempoCmdClamped) n.push_back("BPM row tempos clamped to 32-255 BPM: " + std::to_string(tempoCmdClamped) + ".");
    if (speedLost) n.push_back("Speed changes without a free CMD cell: " + std::to_string(speedLost) + ".");
    if (offsetClamped) n.push_back("Sample offsets above OFF brought down: " + std::to_string(offsetClamped) + ".");
    if (breaks) n.push_back("Pattern breaks to a later row (played from row 0): " + std::to_string(breaks) + ".");
    if (volumeColumn) n.push_back("Volume column effects left out: " + std::to_string(volumeColumn) + ".");
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

}  // namespace

std::optional<ModuleFormat> moduleFormat(std::span<const uint8_t> file) {
    static constexpr char kXm[] = "Extended Module: ";
    if (file.size() >= sizeof kXm - 1 && std::equal(kXm, kXm + sizeof kXm - 1, file.begin())) return ModuleFormat::Xm;
    if (ymio::looksLikeOkt(file)) return ModuleFormat::Okt;
    if (ymio::looksLikeS3m(file)) return ModuleFormat::S3m;
    // A MOD has no signature at its start (and none at all for a Soundtracker one): a MOD is one that reads.
    if (ymio::parseMod(file).module) return ModuleFormat::Mod;
    return std::nullopt;
}

const char* moduleFormatName(ModuleFormat f) noexcept {
    switch (f) {
    case ModuleFormat::Mod: return "MOD";
    case ModuleFormat::S3m: return "S3M";
    case ModuleFormat::Okt: return "OKT";
    case ModuleFormat::Xm: break;
    }
    return "XM";
}

XmImportResult importModule(ModuleFormat f, std::span<const uint8_t> file, VoiceMode mode, NonParamState& state) {
    ymio::XmParseResult parsed = f == ModuleFormat::Mod   ? ymio::parseMod(file)
                                 : f == ModuleFormat::S3m ? ymio::parseS3m(file)
                                 : f == ModuleFormat::Okt ? ymio::parseOkt(file)
                                                          : ymio::parseXm(file);
    if (!parsed.module) {
        XmImportResult out;
        out.error = parsed.error;
        return out;
    }
    XmImportResult out = convert(*parsed.module, mode, state);
    if (out.import) out.import->format = f;
    return out;
}

XmImportResult importXm(std::span<const uint8_t> file, VoiceMode mode, NonParamState& state) {
    return importModule(ModuleFormat::Xm, file, mode, state);
}

XmImportResult importMod(std::span<const uint8_t> file, VoiceMode mode, NonParamState& state) {
    return importModule(ModuleFormat::Mod, file, mode, state);
}

std::vector<std::string> xmImportMessage(const XmImport& r, const std::string& fileName) {
    std::vector<std::string> lines = {std::string(moduleFormatName(r.format)) + " imported", "", fileName};
    if (!r.title.empty()) lines.push_back(r.title);
    lines.push_back(count(r.channels, "track", "tracks") + ", " + count(r.patterns, "pattern", "patterns") + ", " +
                    count(r.instruments, "DMA instrument", "DMA instruments") + ", " +
                    count(r.entries, "Song entry", "Song entries") + ".");
    if (r.ticks)
        lines.push_back("Ticks: speed " + std::to_string(r.speed) + " at " +
                        std::to_string(kVblRates[size_t(r.vblRate)]) + " Hz, STE machine.");
    else
        lines.push_back("Tempo " + std::to_string(int(r.tempo)) + " BPM, STE machine.");
    if (!r.notes.empty()) {
        lines.emplace_back("");
        lines.emplace_back("Not as in the module:");
        // A continuation line (leading space) is indented under its bullet.
        for (const std::string& n : r.notes) lines.push_back(n.front() == ' ' ? "  " + n.substr(1) : "• " + n);
    }
    return lines;
}

void xmImportValues(const XmImport& r, ParamValues& values) {
    auto set = [&values](int idx, float v) { values[size_t(idx)] = clampToSpec(paramSpec(idx), v); };
    const bool studio = ymengine::studioMode(r.mode);
    set(paramIndex(Global::VoiceMode), float(int(r.mode)));
    set(paramIndex(Global::Computer), 1.0f);
    for (int t = 0; t < kTracks; ++t) {
        set(paramIndex(t, Track::On), r.on[size_t(t)] ? 1.0f : 0.0f);
        if (r.on[size_t(t)] && studio) set(paramIndex(t, Track::Generator), float(int(ymengine::Generator::Dma)));
    }
    if (r.ticks) set(paramIndex(Global::VblRate), float(r.vblRate));
    else set(paramIndex(Global::FreeTempo), r.tempo);
}

bool xmWantsStudio(const XmImport& r) noexcept {
    if (r.mode == ymengine::VoiceMode::Studio32) return false;
    return r.played > (r.mode == ymengine::VoiceMode::Studio16 ? 16 : ymengine::kDmaTracks);
}

ymengine::VoiceMode xmStudioMode(const XmImport& r) noexcept {
    return r.played > 16 ? ymengine::VoiceMode::Studio32 : ymengine::VoiceMode::Studio16;
}

}  // namespace stym::plugin
