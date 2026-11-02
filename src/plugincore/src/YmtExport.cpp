// STYM2149 — plugincore: export of the Song as a YM-Tracker song (.ymt)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/YmtExport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

#include "plugincore/Instruments.h"
#include "plugincore/Mapping.h"
#include "plugincore/RegisterImport.h"
#include "plugincore/SteMemory.h"
#include "ymengine/Pitch.h"

namespace stym::plugin {

namespace {

using ymengine::Generator;
using ymengine::SeqCmd;
using ymengine::TrackPatch;
using ymio::YmtCell;
using ymio::YmtInstrument;

constexpr int kMidiOffset = 23;          // YM-Tracker's note n is MIDI note n + 23 (37, its C-5, is C4)
constexpr int kChannels = 4;             // three tone channels and the digidrum channel
constexpr int kFxChannel = 3;            // the digidrum channel, which carries the Fxx
constexpr int kMaxPositions = 256;       // those of an XM order table
constexpr int kMaxInstruments = 16;      // the instrument slots of the demo songs (INFERRED)
constexpr int kMaxSamples = 255;         // Z01 to ZFF
constexpr int kBpmMin = 32, kBpmMax = 255;  // Fxx from $20: a tempo
constexpr int kYmtSpeedMax = 31;            // Fxx below $20: ticks per row
constexpr int kHoldTicks = 256;          // a table that holds its last step: its lists that long, then they loop
constexpr int kEndRows = 16;             // the silent pattern that ends a song without Loop
constexpr uint8_t kNoteOff = ymio::kYmtNoteOff;
constexpr uint8_t kDrumNote = ymio::kYmtC5;  // the note of the digidrum hits of the demo songs
constexpr ymcore::MasterClock kClock = ymcore::clocks::kDefault;  // 2 MHz, the clock of Authentic 3ch

std::string count(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

uint8_t volColumn(int v64) noexcept { return uint8_t(0x10 + std::clamp(v64, 0, 64)); }
int vol64(int vol4) noexcept { return std::clamp(int(std::lround(vol4 * 64 / 15.0)), 0, 64); }
// Parameter of Qxx, Sxx and Uxx for a timer at `hz` (INFERRED: 20 × 2^(x + y / 16) Hz, so xy is 16 x + y).
uint8_t timerParam(double hz) noexcept {
    return uint8_t(std::clamp(std::lround(16 * std::log2(std::max(hz, 1.0) / 20)), 0L, 255L));
}
int periodOf(int midi) noexcept { return ymengine::tonePeriodFor(ymengine::noteHz(midi), kClock); }

// A sound that the notes play: a project instrument (1 to 127) or the sound of track 1 to 3 (-1 to -3), its generator,
// with its vibrato or its table switched off by Vxx and Mxx; source 0: the Digidrums instrument.
struct SoundKey {
    int source = 0;
    Generator gen = Generator::Chip;
    bool vibOff = false, modOff = false;
    auto operator<=>(const SoundKey&) const = default;
};
constexpr SoundKey kDrumKey{0, Generator::Digidrum, false, false};

// A sample of the Digidrums bank: a slot at the rate that its hits play.
struct Sample {
    int slot = 0;
    uint16_t rate = 0;
    bool operator==(const Sample&) const = default;
};

// One position of the order table as written: its rows, and the sound (key + 1, 0: none) of each attack.
struct Work {
    int rows = 0;
    std::vector<YmtCell> cells;
    std::vector<int> sound;
};

// A tone channel from row to row (Impl::laneStep).
struct Tone {
    bool sounding = false;  // a note sounds at the start of the next row
    bool tied = false;      // its steps carried TIE or SLD: a step without a note goes on with it
    bool slide = false;     // SLD: the next note slides from it
    int midi = 60;
    int col = 64;           // volume column in force
    int vol4 = 15;          // volume of its sound
    bool env = false;       // its sound plays the envelope
    int volSpeed = 0;       // volume slide of the track (Axy), kept from row to row
    bool vibOff = false, modOff = false, arp = false;  // Vxx, Mxx, Jxx in force
    const ymseq::Lane* switchLane = nullptr;
};

// Effects that want the cell of a row, by priority.
enum Fx { kTimerFx, kPortaFx, kReleaseFx, kShapeFx, kCutFx, kDelayFx, kRetrigFx, kSlideFx, kFxKinds };

class Writer {
public:
    Writer(const ParamValues& v, const NonParamState& x) : values_(v), x_(x) {
        const ymengine::GlobalParams g = toGlobal(v, x.global);
        ticks_ = g.timing == ymengine::SeqTiming::Ticks && g.seqClock != ymengine::SeqClock::Host;
        vbl_ = std::max<uint32_t>(1, g.vblHz);
        div_ = vbl_ > 100 ? 2 : 1;  // a tick above 100 Hz would need a BPM above 255
        tickHz_ = double(vbl_) / div_;
        spb_ = ymengine::kStepsPerBeat[std::min<size_t>(g.resolution, ymengine::kSeqResolutions - 1)];
        tempo_ = std::clamp(g.tempo, 20.0, 300.0);
        speed_ = std::clamp<int>(g.speed, 1, ymengine::kSpeedMax);
        grooveLen_ = std::clamp<int>(g.grooveLength, 1, ymengine::kGrooveSteps);
        groove_ = g.groove;
        swing_ = !ticks_ && std::abs(g.swing - 50) > 1e-9;
        loop_ = g.loop;
        a4_ = g.a4Hz;
        dc_ = std::clamp<int>(g.digidrumChannel[0], 0, 2);
        bool solo = false;
        for (int t = 0; t < 4; ++t) solo = solo || toMix(v, t).solo;
        for (int t = 0; t < 4; ++t) {
            const ymengine::TrackMix m = toMix(v, t);
            audible_[size_t(t)] = m.enabled && !m.mute && (!solo || m.solo);
            level_[size_t(t)] = std::clamp(m.level, 0.0, 1.0);
            gen_[size_t(t)] = effectiveGenerator(v, t);
            octave_[size_t(t)] = x.seq.tracks[size_t(t)].octave;
            length_[size_t(t)] = std::clamp(x.seq.tracks[size_t(t)].length, 0, ymseq::kSteps);
            gate_[size_t(t)] = std::clamp(trackGate(v, t), 1, 100) / 100.0;
            const ymengine::TrackParams tp = toTrack(v, t, x.tracks[size_t(t)]);
            patch_[size_t(t)] = tp.patch;
            arp_[size_t(t)] = tp.arp;
        }
    }

    // ---- Song: the entries in play order (Impl::songNext), 256 positions at most ----
    bool walk() {
        const auto& E = x_.seq.song.entries;
        const int n = int(E.size());
        if (n == 0) return false;
        auto target = [&](int k) {
            int floor = 0;
            for (int j = k - 1; j >= 0; --j)
                if (E[size_t(j)].kind == ymseq::SongKind::Loop) {
                    floor = j + 1;
                    break;
                }
            const int t = std::max(floor, k - std::max(1, E[size_t(k)].arg));
            return t < k ? t : -1;
        };
        const int restart = std::clamp(x_.seq.song.restartIndex(), 0, n - 1);
        int pos = 0, loopAt = -1, loopCount = 0;
        while (pos < n) {
            const ymseq::SongEntry& e = E[size_t(pos)];
            if (e.kind == ymseq::SongKind::Loop) {
                const int t = target(pos);
                if (t >= 0 && loopAt != pos) loopAt = pos, loopCount = 0;
                if (t >= 0 && loopCount < std::max(1, e.rep)) {
                    ++loopCount;
                    pos = t;
                } else {
                    if (loopAt == pos) loopAt = -1, loopCount = 0;
                    ++pos;
                }
                continue;
            }
            if (pos >= restart && restartVisit_ < 0) restartVisit_ = int(visits_.size());
            for (int r = 0; r < std::max(1, e.rep); ++r) {
                if (int(visits_.size()) >= kMaxPositions) {
                    cut_ = true;
                    return true;
                }
                visits_.push_back({pos, r});
            }
            ++pos;
        }
        if (restartVisit_ < 0) restartVisit_ = 0;
        return !visits_.empty();
    }

    // ---- Rows: the steps as the sequencer plays them ----
    std::vector<Work> play(bool tempoAtStart) {
        reset();
        std::vector<Work> works;
        std::array<Tone, 3> tone{};
        long drumUntil = -1, drumCutAt = -1;  // rows (from the start) that the hit takes, the row where it is cut
        long row = 0, stepAbs = 0;
        int speed = speed_, cmdBpm = 0;
        int ymtSpeed = -1, ymtBpm = -1;
        for (size_t vi = 0; vi < visits_.size(); ++vi) {
            const ymseq::SongEntry& e = x_.seq.song.entries[size_t(visits_[vi].entry)];
            const bool pattern = e.kind == ymseq::SongKind::Pattern;
            const ymseq::Pattern* pat = pattern ? &x_.seq.patterns[size_t(std::clamp(e.pat, 0, ymseq::kPatterns - 1))]
                                                : nullptr;
            const int rows = pattern ? std::clamp(pat->steps, 1, ymseq::kSteps) : std::clamp(e.arg, 1, ymseq::kSteps);
            Work w;
            w.rows = rows;
            w.cells.assign(size_t(rows * kChannels), YmtCell{});
            w.sound.assign(w.cells.size(), 0);
            if (e.bpm > 0) cmdBpm = 0;  // an entry with its own tempo starts at it, each repeat too
            for (int r = 0; r < rows; ++r, ++row, ++stepAbs) {
                YmtCell* cells = &w.cells[size_t(r * kChannels)];
                int* sound = &w.sound[size_t(r * kChannels)];
                auto laneOf = [&](int t) -> const ymseq::Lane& {
                    return pattern ? pat->lane(t) : *ymseq::emptyLane();
                };
                auto stepOf = [&](int t) {
                    const int L = length_[size_t(t)];
                    return L > 0 && pattern ? int(stepAbs % L) : r;
                };
                auto lenOf = [&](int t) { return length_[size_t(t)] > 0 ? length_[size_t(t)] : rows; };
                // Note-off entry: every note released at its start.
                if (!pattern && e.kind == ymseq::SongKind::NoteOff && r == 0) {
                    for (int t = 0; t < 3; ++t) {
                        if (tone[size_t(t)].sounding) cells[t].note = kNoteOff;
                        tone[size_t(t)].sounding = tone[size_t(t)].tied = tone[size_t(t)].slide = false;
                    }
                    if (row < drumUntil) cells[dc_].note = kNoteOff;
                    drumUntil = drumCutAt = -1;
                }
                // Speed (Sxx, groove) or tempo of the row, and its length.
                int rowTicks = 0;
                double rowSec = 0;
                if (ticks_) {
                    for (int t = 0; t < 4; ++t)
                        if (const int p = ymengine::seqCmdParam(laneOf(t).cmdAt(SeqCmd::Speed, stepOf(t))); p > 0)
                            speed = std::min(p, ymengine::kSpeedMax);
                    const int frames = speed + std::min<int>(groove_[size_t(r % grooveLen_)], ymengine::kGrooveMax);
                    const long want = std::lround(double(frames) / div_);
                    rowTicks = int(std::clamp(want, 1L, long(kYmtSpeedMax)));
                    speedClamped_ += rowTicks != want;
                    rowSec = double(frames) / vbl_;
                    if (rowTicks != ymtSpeed || (r == 0 && tempoAtStart)) {
                        if (ymtSpeed >= 0 || tempoAtStart) {
                            cells[kFxChannel].effect = 'F';
                            cells[kFxChannel].param = uint8_t(rowTicks);
                        }
                        if (ymtSpeed >= 0) ++tempoChanges_;
                        else firstSpeed_ = rowTicks;
                        ymtSpeed = rowTicks;
                    }
                } else {
                    if (pattern && pat->bpm[size_t(r)] >= ymengine::kTempoCmdMin) cmdBpm = pat->bpm[size_t(r)];
                    const double T = cmdBpm > 0 ? cmdBpm : e.bpm > 0 ? e.bpm : tempo_;
                    if (ymtBpm < 0) {
                        // Ticks near the replay rate, and the BPM that keeps the length of the steps at that speed.
                        firstSpeed_ = int(std::clamp(std::lround(tickHz_ * 60 / (T * spb_)), 1L, long(kYmtSpeedMax)));
                    }
                    rowTicks = firstSpeed_;
                    rowSec = 60 / (T * spb_);
                    const long want = std::lround(T * spb_ * firstSpeed_ / 24);
                    const int bpm = int(std::clamp(want, long(kBpmMin), long(kBpmMax)));
                    if (bpm != ymtBpm || (r == 0 && tempoAtStart)) {
                        if (ymtBpm >= 0 || tempoAtStart) {
                            cells[kFxChannel].effect = 'F';
                            cells[kFxChannel].param = uint8_t(bpm);
                        }
                        if (ymtBpm >= 0) ++tempoChanges_;
                        else firstBpm_ = bpm;
                        tempoClamped_ += bpm != want;
                        ymtBpm = bpm;
                    }
                }

                // ---- The Digidrum track: its hits on the channel that it takes, from their row on ----
                const ymseq::Lane& dl = laneOf(3);
                const int ds = stepOf(3);
                const int dn = audible_[3] ? dl.pat[size_t(ds)] : -1;
                if (dl.cut[size_t(ds)] && dn < 0 && row < drumUntil) {  // CUT alone: the hit stops
                    if (!cells[dc_].note) cells[dc_].note = kNoteOff;
                    drumUntil = drumCutAt = -1;
                }
                if (dn >= 0) {
                    if (dl.cnd[size_t(ds)]) lose("CND");
                    hit(dl, ds, lenOf(3), dn, rowSec, row, cells[dc_], sound[dc_], tone[size_t(dc_)], drumUntil,
                        drumCutAt);
                } else if (row == drumCutAt && !cells[dc_].note) {
                    cells[dc_].note = kNoteOff;  // a hit held for fewer steps than its sample lasts
                }
                if (row == drumCutAt) drumCutAt = -1;

                // ---- Tone channels: tracks 1 to 3 ----
                for (int t = 0; t < 3; ++t) {
                    Tone& T = tone[size_t(t)];
                    const ymseq::Lane& l = laneOf(t);
                    const int s = stepOf(t);
                    const auto ss = size_t(s);
                    // Vxx, Mxx and Jxx: from their step to the next one of their kind, chased in a new pattern.
                    const bool chase = &l != T.switchLane;
                    T.switchLane = &l;
                    if (chase) T.vibOff = T.modOff = T.arp = false;
                    for (const SeqCmd k : {SeqCmd::Vibrato, SeqCmd::Mod, SeqCmd::Arpeggiator}) {
                        uint16_t c = l.cmdAt(k, s);
                        for (int b = s - 1; chase && !c && b >= 0; --b) c = l.cmdAt(k, b);
                        if (!c) continue;
                        const bool on = ymengine::seqCmdParam(c) != 0;
                        (k == SeqCmd::Vibrato ? T.vibOff : k == SeqCmd::Mod ? T.modOff : T.arp) =
                            k == SeqCmd::Arpeggiator ? on : !on;
                    }
                    if (const uint16_t a = l.cmdAt(SeqCmd::VolSlide, s)) T.volSpeed = ymengine::slideSpeed(a);
                    if (!audible_[size_t(t)]) continue;
                    YmtCell& c = cells[t];
                    int n = l.pat[ss] >= 0 ? ymseq::absoluteNote(l, 1, s, octave_[size_t(t)]) : -1;
                    if (t == dc_ && row < drumUntil) {  // the Digidrum hit takes the channel: the track gives up its step
                        if (n >= 0) ++underDrum_;
                        T.sounding = T.tied = T.slide = false;
                        continue;
                    }
                    if (n >= 0 && l.cnd[ss]) lose("CND");
                    const double lvl = level_[size_t(t)] * l.vol[ss] / 15.0;
                    std::array<int, kFxKinds> fx;
                    fx.fill(-1);
                    std::array<char, kFxKinds> letter{};
                    auto want = [&](Fx f, char ch, int param) {
                        fx[size_t(f)] = std::clamp(param, 0, 255);
                        letter[size_t(f)] = ch;
                    };
                    auto release = [&](double share) {
                        const long k = std::lround(share * rowTicks);
                        if (k < rowTicks) want(kReleaseFx, 'K', int(std::max(1L, k)));
                        T.tied = T.slide = false;  // a full step: released when the next one starts
                    };
                    bool active = false;
                    if (n < 0) {
                        if (T.sounding && T.tied && (l.tie[ss] || (T.slide && l.slide[ss]))) {
                            active = true;  // TIE or SLD: the note goes on, VOL sets its level
                            const int v = vol64(attenuatedVolume(T.vol4, lvl));
                            if (v != T.col) {
                                c.volume = volColumn(v);
                                T.col = v;
                            }
                            const int nx = (s + 1) % lenOf(t);
                            if (!T.slide && l.gat[ss] && !(l.tie[size_t(nx)] && l.pat[size_t(nx)] < 0))
                                release(l.gat[ss] / 100.0);  // GAT on the last step of the note
                            if (l.fx[ss]) lose("GEN on held steps");
                            if (l.mix[ss]) lose("SRC");
                        } else {
                            if (T.sounding) c.note = kNoteOff;
                            T.sounding = T.tied = T.slide = false;
                        }
                    } else {
                        active = true;
                        int midi = n;
                        while (midi < 1 + kMidiOffset) midi += 12, ++moved_;
                        while (midi > 96 + kMidiOffset) midi -= 12, ++moved_;
                        if (T.slide && T.sounding) {  // SLD: the note before slides to this one
                            c.note = uint8_t(midi - kMidiOffset);
                            const int dp = std::abs(periodOf(T.midi) - periodOf(midi));
                            want(kPortaFx, '3', std::max(1, (dp + std::max(1, rowTicks - 1) - 1) / std::max(1, rowTicks - 1)));
                            const int v = vol64(attenuatedVolume(T.vol4, lvl));
                            if (v != T.col) {
                                c.volume = volColumn(v);
                                T.col = v;
                            }
                        } else {
                            const int ins = l.ins[ss];
                            const Instrument* I = ins >= 1 && ins <= ymengine::kInstruments &&
                                                          x_.instruments[size_t(ins - 1)].used
                                                      ? &x_.instruments[size_t(ins - 1)]
                                                      : nullptr;
                            const TrackPatch& p = I ? I->patch : patch_[size_t(t)];
                            // In 3 channels a Digidrum instrument leaves the track its generator (Impl::laneGen).
                            const Generator g = I && I->patch.generator != Generator::Digidrum ? I->patch.generator
                                                                                                : gen_[size_t(t)];
                            const int fxv = std::min<int>(l.fx[ss], ymengine::kSeqFxMax);
                            const int ov = fxv ? ymengine::kSeqFxGen[size_t(fxv)] : -1;
                            const Generator played = ov >= 0 ? Generator(ov) : g;
                            if (played != Generator::Chip && played != Generator::Sid &&
                                played != Generator::SyncBuzzer && played != Generator::Fm) {
                                lose("Chipdrum and DMA notes");
                                if (T.sounding) c.note = kNoteOff;
                                T.sounding = T.tied = T.slide = false;
                                continue;
                            }
                            const SoundKey key{I ? ins : -(t + 1), g,
                                               T.vibOff && p.vibratoOn && p.vibratoCents > 0,
                                               T.modOff && p.ornamentOn};
                            sound[t] = keyOf(key) + 1;
                            c.note = uint8_t(midi - kMidiOffset);
                            T.vol4 = std::clamp<int>(p.volume, 0, 15);
                            T.col = vol64(attenuatedVolume(T.vol4, lvl));
                            if (T.col != 64) c.volume = volColumn(T.col);
                            T.env = p.envelopeVolume || g == Generator::SyncBuzzer;
                            // Timer effects (INFERRED: they last until the end of the note).
                            if (played == Generator::Sid) {
                                const bool sine = p.sidForm == ymengine::SidForm::Sine ||
                                                  p.sidForm == ymengine::SidForm::Bounce;
                                want(kTimerFx, sine ? 'S' : 'Q', timerParam(ymengine::noteHz(midi) * p.sidRatio));
                            } else if (played == Generator::SyncBuzzer && g != Generator::SyncBuzzer) {
                                want(kTimerFx, 'U', timerParam(ymengine::noteHz(midi)));
                            } else if (played == Generator::Chip && g == Generator::SyncBuzzer) {
                                lose("GEN Chip Voice on a Sync Buzzer");
                            } else if (played == Generator::Fm) {
                                lose("FM Voice steps (the note plays alone)");
                            }
                            if (l.mix[ss]) lose("SRC");
                            const ymengine::TrackArp& arp = I ? I->arp : arp_[size_t(t)];
                            if (T.arp && arp.on) lose("ARP (Jxx)");
                            ++notes_;
                        }
                        T.midi = midi;
                        T.sounding = true;
                        T.slide = l.slide[ss] != 0;
                        T.tied = l.slide[ss] || l.tie[ss];
                        if (l.rtg[ss]) lose("RTG");
                        for (int v = 2; v <= ymseq::kPolyMax; ++v)
                            if (l.vp(v, s) >= 0) lose("chord notes");
                        if (!T.tied) release(l.gat[ss] ? l.gat[ss] / 100.0 : gate_[size_t(t)]);
                    }
                    // Commands of the row.
                    const auto param = [&](SeqCmd k) { return ymengine::seqCmdParam(l.cmdAt(k, s)); };
                    if (l.cmdAt(SeqCmd::Cut, s) && param(SeqCmd::Cut) < rowTicks)
                        want(kCutFx, 'E', 0xC0 | std::min(param(SeqCmd::Cut), 15));
                    if (param(SeqCmd::Delay) > 0 && param(SeqCmd::Delay) < rowTicks)
                        want(kDelayFx, 'E', 0xD0 | std::min(param(SeqCmd::Delay), 15));
                    if (l.cmdAt(SeqCmd::Retrig, s) && param(SeqCmd::Retrig) < rowTicks)
                        want(kRetrigFx, 'R', param(SeqCmd::Retrig));
                    if (l.cmdAt(SeqCmd::Shape, s)) {
                        if (active && T.env) want(kShapeFx, 'X', param(SeqCmd::Shape) & 15);
                        else lose("Exx without the envelope");
                    }
                    if (T.volSpeed && active)
                        want(kSlideFx, 'A', T.volSpeed > 0 ? std::min(T.volSpeed, 15) << 4 : std::min(-T.volSpeed, 15));
                    for (const SeqCmd k : {SeqCmd::PitchSlide, SeqCmd::EnvSlide, SeqCmd::NoiseSlide, SeqCmd::Offset,
                                           SeqCmd::Fine, SeqCmd::Lfo, SeqCmd::VolGlide, SeqCmd::EnvCurve,
                                           SeqCmd::NoiseCurve, SeqCmd::Sync})
                        if (l.cmdAt(k, s)) lose(std::string{ymengine::kSeqCmdLetters[size_t(k)], 'x', 'x'});
                    // One effect per cell: the first by priority; a release without room goes to the next row.
                    int chosen = -1;
                    for (int f = 0; f < kFxKinds; ++f) {
                        if (fx[size_t(f)] < 0) continue;
                        if (chosen < 0) {
                            chosen = f;
                            c.effect = letter[size_t(f)];
                            c.param = uint8_t(fx[size_t(f)]);
                        } else if (f == kReleaseFx) {
                            ++releaseMoved_;
                        } else {
                            ++noRoom_;
                        }
                    }
                    if (chosen == kReleaseFx || chosen == kCutFx) T.sounding = false;
                }
            }
            works.push_back(std::move(w));
        }
        return works;
    }

    // ---- The project's instruments as YM-Tracker's ----
    YmtInstrument instrumentOf(const SoundKey& k) {
        YmtInstrument in;
        if (k.source == 0) {  // the hits: a silent sound, the bank of the samples
            in.name = "Digidrums";
            in.tone = false;
            in.attack = in.decay = in.release = 0;
            in.sustain = 15;
            for (size_t i = 0; i < samples_.size(); ++i) {
                const Sample& sm = samples_[i];
                const std::span<const uint8_t> v = x_.digidrums[size_t(sm.slot)];
                const ymengine::SampleSpan part =
                    ymengine::sampleSpan(x_.digiSlots[size_t(sm.slot)].play.start,
                                         x_.digiSlots[size_t(sm.slot)].play.end, uint32_t(v.size()));
                ymio::YmtDrum d;
                d.name = x_.digiSlots[size_t(sm.slot)].name;
                if (d.name.empty()) d.name = "Digi " + std::to_string(sm.slot + 1);
                int same = 0;
                for (size_t j = 0; j < i; ++j) same += samples_[j].slot == sm.slot;
                if (same) d.name += " " + std::to_string(same + 1);
                d.rate = sm.rate;
                d.levels.assign(v.begin() + part.start, v.begin() + part.end);
                for (uint8_t& l : d.levels) l = std::min<uint8_t>(l, 15);
                in.drums.push_back(std::move(d));
            }
            return in;
        }
        const int t = k.source < 0 ? -k.source - 1 : 0;
        const Instrument* I = k.source > 0 ? &x_.instruments[size_t(k.source - 1)] : nullptr;
        const TrackPatch& p = I ? I->patch : patch_[size_t(t)];
        std::string name = I ? cleanInstrumentName(I->name) : cleanInstrumentName(x_.trackNames[size_t(t)]);
        if (name.empty()) name = I ? "Instrument " + std::to_string(k.source) : "Track " + std::to_string(t + 1);
        std::string tags = std::string(k.vibOff ? " V00" : "") + (k.modOff ? " M00" : "");
        if (!tags.empty()) name = name.substr(0, kInstrumentNameMax - std::min(tags.size(), kInstrumentNameMax)) + tags;
        in.name = cleanInstrumentName(name);
        in.tone = p.toneOn;
        in.noise = p.noiseOn;
        in.noisePeriod = uint8_t(std::clamp<int>(p.noisePeriod, 1, 31));
        // ADSR in ticks of the song.
        const double tickMs = 1000 / tickHz_;
        bool clamped = false;
        auto ticks = [&](double ms) {
            const long v = std::lround(ms / tickMs);
            clamped = clamped || v > 15;
            return uint8_t(std::clamp(v, 0L, 15L));
        };
        in.useAdsr = true;
        in.attack = ticks(p.attackMs);
        in.decay = ticks(p.decayMs);
        in.sustain = uint8_t(std::clamp(std::lround(p.sustainPct * 15 / 100), 0L, 15L));
        in.release = ticks(p.releaseMs);
        adsrClamped_ += clamped;
        // The envelope (HardEnv tab): its shape, its period from the note when the buzzer is tuned (INFERRED: a
        // looped triangle then plays half the frequency of the octave), the fixed period otherwise.
        const bool buzz = k.gen == Generator::SyncBuzzer;
        in.hardEnv = p.envelopeVolume || buzz;
        in.syncBuzzer = buzz;
        in.shape = uint8_t(p.envShape & 15);
        if (!in.hardEnv) {
            in.autoPeriod = true;  // as an instrument of the demo songs: no envelope, the period from the note
        } else if (p.envTuned) {
            in.autoPeriod = true;
            const double n = double(p.envRatioN) * (in.shape == 10 || in.shape == 14 ? 2 : 1) /
                             std::max<int>(1, p.envRatioD);
            in.octave = uint8_t(std::clamp(std::lround(n), 1L, long(ymengine::kEnvRatioMax)));
            envRatio_ += std::abs(n - in.octave) > 1e-9;
            if (p.envFineCents != 0) lose("buzzer fine tune");
        } else {
            in.autoPeriod = false;
            in.envPeriod = p.envPeriod;
        }
        // Vibrato (Tone tab): x/64 of a cycle per tick, y/8 of a semitone deep.
        if (p.vibratoOn && !k.vibOff && p.vibratoCents > 0) {
            in.vibSpeed = uint8_t(std::clamp(std::lround(p.vibratoHz * 64 / tickHz_), 1L, 15L));
            in.vibDepth = uint8_t(std::clamp(std::lround(p.vibratoCents / 12.5), 1L, 15L));
        }
        // The Modulation table: arpeggio list (semitones per step), pitch list (PER per tick) and, with a flat ADSR,
        // volume list; one tick per VBL frame; a table that holds its last step: its lists held that long.
        if (p.ornamentOn && !k.modOff) {
            const int len = std::clamp<int>(p.ornamentLength, 1, ymengine::kOrnamentSteps);
            const int rate = std::clamp<int>(p.ornamentRate, 1, 4);
            const int hold = p.ornamentLoop ? 0 : std::max(0, kHoldTicks / rate - len);
            std::vector<int8_t> arp;
            std::vector<int16_t> pitch;
            std::vector<uint8_t> vol;
            for (int i = 0; i < len + hold; ++i) {
                const auto k8 = size_t(std::min(i, len - 1));
                arp.push_back(p.ornamentPitch[k8]);
                for (int r = 0; r < rate; ++r) {
                    pitch.push_back(p.rows.period[k8]);
                    if (i < len) vol.push_back(std::min<uint8_t>(p.ornamentVolume[k8], 15));
                }
            }
            if (std::any_of(arp.begin(), arp.end(), [](int8_t v) { return v != 0; })) {
                in.arp = std::move(arp);
                in.arpSpeed = uint8_t(rate);
            }
            if (std::any_of(pitch.begin(), pitch.end(), [](int16_t v) { return v != 0; })) in.pitch = std::move(pitch);
            if (std::any_of(vol.begin(), vol.end(), [](uint8_t v) { return v != 15; })) {
                const bool flat = in.attack == 0 && in.release == 0 && (in.decay == 0 || in.sustain == 15);
                if (flat) {
                    in.useAdsr = false;
                    in.volume = std::move(vol);
                    in.volumeLoop = int16_t(p.ornamentLoop ? 0 : -1);
                } else {
                    lose("volume tables under an ADSR");
                }
            }
            for (int i = 0; i < len; ++i) {
                const auto k8 = size_t(i);
                if (p.ornamentNoise[k8]) lose("noise steps of tables");
                if (p.rows.noise[k8] || p.rows.mix[k8] || p.rows.gen[k8] || p.rows.fix[k8])
                    lose("NP, MIX, GEN and FIX table rows");
            }
        }
        if (p.lfo.on) lose("LFO");
        if (p.echo.on) lose("Echo");
        if (p.detuneCents != 0 || p.analogCents != 0) lose("Detune and Analog");
        if (p.ymPad) lose("YM Pad");
        if (p.hardSync) lose("Hard sync");
        if (p.link) lose("Link");
        return in;
    }

    // ---- The whole song ----
    YmtExport assemble() {
        // Two passes: the second writes the tempo at the start of each position when the song changes it, so that a
        // position played after another tempo (the restart, the first one included) finds its own.
        std::vector<Work> works = play(false);
        if (tempoChanges_) works = play(true);
        YmtExport out;
        ymio::YmtSong& s = out.song;
        s.bpm = ticks_ ? int(std::lround(tickHz_ * 2.5)) : firstBpm_;
        s.speed = firstSpeed_;
        s.chips = 1;
        s.toneChannels = 3;
        s.drumChannel = true;
        for (int c = 0; c < kChannels; ++c) {
            std::string name = c < 3 ? cleanTrackName(x_.trackNames[size_t(c)]) : std::string();
            s.channelNames.push_back(name.empty() ? "Ch " + std::to_string(c + 1) : name);
        }
        // Instruments: the most played ones (16 slots, the Digidrums one included), the others played as the closest.
        std::vector<int> order(keys_.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            if ((keys_[size_t(a)] == kDrumKey) != (keys_[size_t(b)] == kDrumKey)) return keys_[size_t(a)] == kDrumKey;
            return uses_[size_t(a)] > uses_[size_t(b)];
        });
        std::vector<bool> kept(keys_.size(), false);
        for (size_t i = 0; i < order.size() && i < size_t(kMaxInstruments); ++i) kept[size_t(order[i])] = true;
        std::vector<int> slot(keys_.size(), 0);
        int next = 1;
        for (size_t i = 0; i < keys_.size(); ++i)
            if (kept[i]) slot[i] = next++;
        int merged = 0;
        for (size_t i = 0; i < keys_.size(); ++i) {
            if (kept[i]) continue;
            int best = -1;
            for (int j : order)
                if (kept[size_t(j)] && keys_[size_t(j)].source == keys_[i].source) {
                    best = j;
                    break;
                }
            if (best < 0)
                for (int j : order)
                    if (kept[size_t(j)] && !(keys_[size_t(j)] == kDrumKey)) {
                        best = j;
                        break;
                    }
            slot[i] = best >= 0 ? slot[size_t(best)] : 1;
            merged += uses_[i];
        }
        YmtInstrument none;
        none.name = "--";
        s.instruments.push_back(none);
        for (size_t i = 0; i < keys_.size(); ++i)
            if (kept[i]) s.instruments.push_back(instrumentOf(keys_[i]));
        out.instruments = int(s.instruments.size()) - 1;
        while (int(s.instruments.size()) <= kMaxInstruments) {  // the empty slots, as in the demo songs
            YmtInstrument blank;
            blank.name = "Instrument " + std::to_string(s.instruments.size());
            s.instruments.push_back(blank);
        }
        // Patterns: one per distinct position.
        for (Work& w : works) {
            ymio::YmtPattern p;
            p.rows = w.rows;
            p.cells = std::move(w.cells);
            for (size_t i = 0; i < p.cells.size(); ++i)
                if (w.sound[i]) p.cells[i].instrument = uint8_t(slot[size_t(w.sound[i] - 1)]);
            auto it = std::find(s.patterns.begin(), s.patterns.end(), p);
            if (it == s.patterns.end()) it = s.patterns.insert(s.patterns.end(), std::move(p));
            s.order.push_back(uint8_t(it - s.patterns.begin()));
        }
        out.loops = loop_;
        if (loop_) {
            s.restart = restartVisit_;
        } else {  // the song ends: its notes released, a pattern that jumps to itself
            ymio::YmtPattern end;
            end.rows = kEndRows;
            end.cells.assign(size_t(kEndRows * kChannels), YmtCell{});
            for (int c = 0; c < 3; ++c) end.cells[size_t(c)].note = kNoteOff;
            YmtCell& jump = end.cells[size_t((kEndRows - 1) * kChannels + kFxChannel)];
            jump.effect = 'B';
            jump.param = uint8_t(s.order.size());
            s.restart = int(s.order.size());
            s.patterns.push_back(std::move(end));
            s.order.push_back(uint8_t(s.patterns.size() - 1));
        }
        out.positions = int(s.order.size());
        out.patterns = int(s.patterns.size());
        out.notes = notes_;
        out.samples = int(samples_.size());

        // ---- What YM-Tracker does not play as the sequencer does ----
        auto& n = out.remarks;
        if (cut_) n.push_back("The Song stops after 256 positions.");
        if (merged) n.push_back("Notes played with the closest of the 16 instruments: " + std::to_string(merged) + ".");
        if (underDrum_) n.push_back("Notes under the Digidrum hits, left out as the sequencer does: " +
                                    std::to_string(underDrum_) + ".");
        if (moved_) n.push_back("Notes beyond C-1 to B-8 moved by octaves: " + std::to_string(moved_) + ".");
        if (releaseMoved_)
            n.push_back("Note releases moved to the end of their row (the cell holds another effect): " +
                        std::to_string(releaseMoved_) + ".");
        if (noRoom_) n.push_back("Effects without room in their cell: " + std::to_string(noRoom_) + ".");
        if (adsrClamped_)
            n.push_back("ADSR stages beyond 15 ticks, clamped: " + count(adsrClamped_, "instrument", "instruments") +
                        ".");
        if (envRatio_)
            n.push_back("Buzzer ratios played at the nearest octave: " + count(envRatio_, "instrument", "instruments") +
                        ".");
        if (div_ > 1) n.push_back("Replay at " + std::to_string(vbl_) + " Hz written at " +
                                  std::to_string(vbl_ / div_) + " Hz (ticks of twice the length).");
        if (speedClamped_) n.push_back("Rows longer than 31 ticks, shortened: " + std::to_string(speedClamped_) + ".");
        if (tempoClamped_) n.push_back("Tempos beyond 32-255 BPM once in ticks, clamped: " +
                                       std::to_string(tempoClamped_) + ".");
        if (swing_) n.push_back("Swing left out.");
        if (sampleCut_) n.push_back("Digidrum hits beyond 255 samples: " + std::to_string(sampleCut_) + ".");
        if (rateClamped_) n.push_back("Digidrum rates beyond 65535 Hz, clamped: " + std::to_string(rateClamped_) + ".");
        if (loopSlots_) n.push_back("Looped Digidrum slots played once: " + std::to_string(loopSlots_) + ".");
        if (std::abs(a4_ - ymengine::kA4Hz) > 1e-9) n.push_back("Tuning A4 left out (YM-Tracker plays A4 at 440 Hz).");
        if (steMachine(values_)) n.push_back("STE DMA tracks left out.");
        // One line per feature, so that the alert translates its name (a line of the interface catalogue).
        for (const auto& [name, times] : lost_) n.push_back("Left out: " + name + " (" + std::to_string(times) + ").");
        return out;
    }

private:
    struct Visit {
        int entry = 0, repeat = 0;
    };

    void reset() {
        keys_.clear();
        keyIndex_.clear();
        uses_.clear();
        samples_.clear();
        lost_.clear();
        notes_ = underDrum_ = moved_ = releaseMoved_ = noRoom_ = 0;
        speedClamped_ = tempoClamped_ = tempoChanges_ = sampleCut_ = rateClamped_ = loopSlots_ = 0;
        firstSpeed_ = 6;
        firstBpm_ = 125;
    }
    void lose(const std::string& what) { ++lost_[what]; }
    int keyOf(const SoundKey& k) {
        auto it = keyIndex_.find(k);
        if (it == keyIndex_.end()) {
            it = keyIndex_.emplace(k, int(keys_.size())).first;
            keys_.push_back(k);
            uses_.push_back(0);
        }
        ++uses_[size_t(it->second)];
        return it->second;
    }

    // A hit of the Digidrum track (Impl::laneStep, Impl::applyPitch): its slot and rate, held for its steps or its
    // sample, whichever is shorter; the melodic track of the channel gives up the rows that it takes.
    void hit(const ymseq::Lane& l, int s, int len, int dn, double rowSec, long row, YmtCell& c, int& sound, Tone& T,
             long& until, long& cutAt) {
        const auto ss = size_t(s);
        const int note = (octave_[3] + 1) * 12 + dn;
        // The row gives the pad, the pad its Digidrum: that of the step's instrument, otherwise of the track.
        const int pad = (note % 12 + 12) % 12 % ymengine::kDigidrumPads;
        const int ins = l.ins[ss];
        const Instrument* I = ins >= 1 && ins <= ymengine::kInstruments && x_.instruments[size_t(ins - 1)].used
                                  ? &x_.instruments[size_t(ins - 1)]
                                  : nullptr;
        const int slot =
            std::min<int>((I ? I->patch : patch_[3]).digiPads[size_t(pad)], ymengine::kDigidrumSlotsMax - 1);
        const ymengine::DigidrumSlot& d = x_.digiSlots[size_t(slot)].play;
        const ymengine::SampleSpan part =
            ymengine::sampleSpan(d.start, d.end, uint32_t(x_.digidrums[size_t(slot)].size()));
        if (part.size() == 0) {
            lose("Digidrum hits on empty slots");
            return;
        }
        if (mem_.digiMuted[size_t(slot)]) {  // silent: past the memory of the Atari
            lose("Digidrum hits on samples past the memory of the Atari");
            return;
        }
        const double tr = std::pow(2.0, (l.oct[ss] * 12 + l.ton[ss]) / 12.0);
        const double rate = d.rateHz * std::pow(2.0, (note - d.root + d.cents / 100) / 12.0) * (a4_ / ymengine::kA4Hz) * tr;
        const long r16 = std::clamp(std::lround(rate), 1L, 65535L);
        rateClamped_ += r16 != std::lround(rate);
        const Sample sm{slot, uint16_t(r16)};
        auto it = std::find(samples_.begin(), samples_.end(), sm);
        if (it == samples_.end()) {
            if (int(samples_.size()) >= kMaxSamples) {
                ++sampleCut_;
                return;
            }
            it = samples_.insert(samples_.end(), sm);
        }
        c = YmtCell{};
        c.note = kDrumNote;
        sound = keyOf(kDrumKey) + 1;
        const int v = vol64(attenuatedVolume(15, level_[3] * l.vol[ss] / 15.0));
        if (v != 64) c.volume = volColumn(v);
        c.effect = 'Z';
        c.param = uint8_t(it - samples_.begin() + 1);
        T.sounding = T.tied = T.slide = false;
        // Held for the steps that TIE adds, up to the end of its sample (a looped slot: its steps).
        int hold = 1;
        if (l.tie[ss])
            while (hold < len && l.tie[size_t((s + hold) % len)] && l.pat[size_t((s + hold) % len)] < 0) ++hold;
        long need = std::numeric_limits<long>::max();
        if (d.loop) ++loopSlots_;
        else need = std::max(1L, long(std::ceil(double(part.size()) / double(r16) / std::max(rowSec, 1e-9) - 1e-9)));
        until = row + std::min<long>(hold, need);
        cutAt = hold < need ? row + hold : -1;
    }

    const ParamValues& values_;
    const NonParamState& x_;
    const SteMemory mem_ = steMemory(x_, voiceMode(values_), steMachine(values_));  // samples heard
    bool ticks_ = true, swing_ = false, loop_ = true, cut_ = false;
    uint32_t vbl_ = 50;
    int div_ = 1;
    double tickHz_ = 50, spb_ = 4, tempo_ = 120, a4_ = ymengine::kA4Hz;
    int speed_ = 6, grooveLen_ = 1, dc_ = 2;
    std::array<uint8_t, ymengine::kGrooveSteps> groove_{};
    std::array<bool, 4> audible_{};
    std::array<double, 4> level_{}, gate_{};
    std::array<Generator, 4> gen_{};
    std::array<int, 4> octave_{}, length_{};
    std::array<TrackPatch, 4> patch_{};
    std::array<ymengine::TrackArp, 4> arp_{};
    std::vector<Visit> visits_;
    int restartVisit_ = -1;
    // Found by play().
    std::vector<SoundKey> keys_;
    std::map<SoundKey, int> keyIndex_;
    std::vector<int> uses_;
    std::vector<Sample> samples_;
    std::map<std::string, int> lost_;
    int notes_ = 0, underDrum_ = 0, moved_ = 0, releaseMoved_ = 0, noRoom_ = 0;
    int speedClamped_ = 0, tempoClamped_ = 0, tempoChanges_ = 0, sampleCut_ = 0, rateClamped_ = 0, loopSlots_ = 0;
    int adsrClamped_ = 0, envRatio_ = 0;
    int firstSpeed_ = 6, firstBpm_ = 125;
};

}  // namespace

YmtExportResult exportYmt(const ParamValues& values, const NonParamState& extras, const std::string& title) {
    YmtExportResult res;
    if (voiceMode(values) != ymengine::VoiceMode::Authentic3) {
        res.error = "YM-Tracker plays the three channels of one chip: switch to Authentic 3ch";
        return res;
    }
    Writer w(values, extras);
    if (!w.walk()) {
        res.error = "The Song is empty.";
        return res;
    }
    YmtExport out = w.assemble();
    out.song.title = title;
    res.value = std::move(out);
    return res;
}

std::vector<std::string> ymtExportMessage(const YmtExport& x, const std::string& fileName) {
    std::vector<std::string> lines = {"YM-Tracker song exported", "", fileName};
    lines.push_back(count(x.positions, "position", "positions") + ", " + count(x.patterns, "pattern", "patterns") +
                    ", " + count(x.instruments, "instrument", "instruments") +
                    (x.samples ? ", " + count(x.samples, "digidrum", "digidrums") : std::string()) + ".");
    lines.push_back("Speed " + std::to_string(x.song.speed) + " at " + std::to_string(x.song.bpm) + " BPM, " +
                    (x.loops ? "loops at position " + std::to_string(x.song.restart + 1) : std::string("plays once")) +
                    ".");
    if (!x.remarks.empty()) {
        lines.emplace_back("");
        lines.emplace_back("Not as heard:");
        for (const std::string& r : x.remarks) lines.push_back("• " + r);
    }
    return lines;
}

}  // namespace stym::plugin
