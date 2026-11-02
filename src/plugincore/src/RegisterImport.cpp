// STYM2149 — plugincore: import of YM and SNDH tunes as editable patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/RegisterImport.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "plugincore/Instruments.h"
#include "ymcore/Ym2149.h"
#include "ymengine/Pitch.h"
#include "ymio/Sndh.h"
#include "ymio/YmFile.h"
#include "ymseq/Pattern.h"

namespace stym::plugin {

using ymengine::Generator;
using ymengine::SeqCmd;
using ymengine::seqCmd;

namespace {

constexpr double kChipHz = 2'000'000;            // clock of Authentic 3ch, to which the periods are brought
constexpr int kTable = ymengine::kOrnamentSteps;  // frames of a table (one per step)
constexpr int kMinTonePeriod = 8;                 // below: above 15 kHz, no pitch heard
constexpr double kMinPitchHz = 20;                // an envelope slower than this is no buzzer
constexpr int kMinFineCents = 5;                  // Fxx from this offset of the first pitch of a note
constexpr double kPerCents = 8;                   // PER of a step dropped below this offset
constexpr int kGridCollide = 2;  // % of the notes that may share a step with the one before on their voice
constexpr int kPatternFrames = 384;               // frames of a pattern by default (64 steps of 6)
constexpr double kLegatoCents = 70;  // jump of a legato note
constexpr double kLegatoHold = 50;   // spread of its first 3 frames
constexpr uint32_t kCaptureRate = 50000;  // samples per second of the SNDH capture (the register state per tick only)
constexpr int kRatioMax = ymengine::kEnvRatioMax;

int64_t roundTo(double x) noexcept { return int64_t(std::floor(x + 0.5)); }

// --------------------------------------------------------------------------------------------- Frames read
// One voice at one frame, at 2 MHz.
struct Chan {
    bool tone = false, noise = false, env = false;
    uint8_t vol = 0;  // fixed volume (bit 4 of R8-R10 apart)
    uint16_t tp = 0;  // tone period
    bool operator==(const Chan&) const = default;
};
struct Frame {
    std::array<Chan, 3> ch{};
    uint8_t np = 1;          // noise period
    uint16_t ep = 0xffff;    // envelope period
    uint8_t shape = 0;       // R13
    bool restart = false;    // the envelope restarts at this frame
    bool envAlive = true;    // the envelope still moves or holds a level other than 0
    std::array<TimerEffect, 3> fx{};  // timer effect of each voice, where the tune tells it
};

bool repeating(int shape) noexcept { return shape >= 8 && (shape & 1) == 0; }  // 8, 10, 12, 14
bool holdsHigh(int shape) noexcept { return shape == 11 || shape == 13; }
// Shape as the Shape R13 menu and Exx have it: 0 to 3 are 9, 4 to 7 are 15.
uint8_t menuShape(int shape) noexcept { return uint8_t(shape < 4 ? 9 : shape < 8 ? 15 : shape); }

double toneHz(const Chan& a) noexcept { return a.tone && a.tp >= kMinTonePeriod ? kChipHz / (16.0 * a.tp) : 0; }
double envHz(const Frame& f) noexcept {
    return repeating(f.shape) ? kChipHz / (256.0 * ymengine::envelopeRamps(f.shape) * f.ep) : 0;
}
// The envelope plays a pitch under voice c (buzzer).
bool buzzer(const Frame& f, int c) noexcept { return f.ch[size_t(c)].env && envHz(f) >= kMinPitchHz; }

bool audible(const Frame& f, int c) noexcept {
    const Chan& a = f.ch[size_t(c)];
    switch (f.fx[size_t(c)].gen) {
    case Generator::Digidrum: return false;    // played by the Digidrum track
    case Generator::SyncBuzzer: return a.env;  // the timer restarts the envelope
    case Generator::Sid: return a.vol > 0;     // the timer steps the volume, with or without the square
    default: break;
    }
    if (a.env) return f.envAlive;
    return a.vol > 0 && ((a.tone && a.tp >= kMinTonePeriod) || a.noise);
}

// Pitch heard on voice c: the tone, otherwise a buzzer; 0 without one.
double pitchHz(const Frame& f, int c) noexcept {
    const double t = toneHz(f.ch[size_t(c)]);
    return t > 0 ? t : buzzer(f, c) ? envHz(f) : 0;
}

std::vector<Frame> decode(const RegisterTune& t) {
    const double k = kChipHz / t.clockHz;
    const size_t n = t.frames.size();
    std::vector<Frame> out(n);
    // Some dumps write R13 at nearly every frame where the envelope plays (a restart at every frame, a 50 Hz buzz):
    // only its changes of shape, and the frames where a voice takes the envelope, restart it then.
    size_t envFrames = 0, writes = 0;
    for (size_t i = 0; i < n; ++i) {
        const auto& r = t.frames[i];
        if ((r[8] | r[9] | r[10]) & 16) {
            ++envFrames;
            writes += i < t.restart.size() && t.restart[i];
        }
    }
    const bool storm = envFrames >= 50 && writes * 10 >= envFrames * 8;
    int64_t last = -1;  // frame of the last restart
    for (size_t i = 0; i < n; ++i) {
        const auto& r = t.frames[i];
        Frame& f = out[i];
        for (int c = 0; c < 3; ++c) {
            Chan& a = f.ch[size_t(c)];
            const int tp = r[size_t(2 * c)] | (r[size_t(2 * c + 1)] & 15) << 8;
            a.tp = uint16_t(std::clamp<int64_t>(roundTo(tp * k), 0, 4095));
            a.tone = !(r[7] >> c & 1);
            a.noise = !(r[7] >> (3 + c) & 1);
            a.env = (r[size_t(8 + c)] & 16) != 0;
            a.vol = r[size_t(8 + c)] & 15;
        }
        f.np = uint8_t(std::clamp<int64_t>(roundTo(std::max(1, r[6] & 31) * k), 1, 31));
        // A zero period is $FFFF, as the YM Player plays it.
        const int ep = r[11] | r[12] << 8;
        f.ep = uint16_t(std::clamp<int64_t>(roundTo((ep ? ep : 0xffff) * k), 1, 65535));
        f.shape = r[13] & 15;
        f.restart = i < t.restart.size() && t.restart[i];
        if (i < t.effects.size()) f.fx = t.effects[i];
        if (storm && f.restart && i > 0) {
            const Frame& p = out[i - 1];
            bool taken = false;
            for (int c = 0; c < 3; ++c) taken = taken || (f.ch[size_t(c)].env && !p.ch[size_t(c)].env);
            f.restart = taken || f.shape != p.shape;
        }
        if (f.restart) last = int64_t(i);
        const double ramp = 256.0 * f.ep / kChipHz;  // seconds of one ramp
        f.envAlive = repeating(f.shape) || holdsHigh(f.shape) || last < 0 ||
                     double(int64_t(i) - last) / std::max<uint32_t>(1, t.rate) < ramp;
    }
    return out;
}

// --------------------------------------------------------------------------------------------- Notes
struct Note {
    int voice = 0, start = 0, end = 0;  // frames [start, end)
    int step = 0, delay = 0;            // step of the attack and frames of Dxx
    int retrig = -1;                    // frame of an envelope restart merged from a second note (Rxx)
    int note = 60, fine = 0;            // NOTE/OCT (MIDI) and Fxx offset in cents
    int sound = -1;
    bool env = false;                   // plays the envelope
    uint8_t shape = 8;                  // Exx
    int known = -1;                     // MIDI note that a known row starts; -1: from the frames
};

// Pitch of each frame of voice c in cents from A4 = 440 Hz (only their differences count), NaN without one.
std::vector<double> centsOf(const std::vector<Frame>& fr, int c) {
    std::vector<double> s(fr.size(), std::nan(""));
    for (size_t i = 0; i < fr.size(); ++i)
        if (const double hz = pitchHz(fr[i], c); hz > 0) s[i] = 1200 * std::log2(hz / 440);
    return s;
}

// A new note may start without a new attack (legato) at frame f of a note started at `start`: the pitch jumps by
// kLegatoCents or more, holds there for 3 frames (within kLegatoHold) and was not heard since `from` (the last attack,
// at most 12 frames back, where an arpeggio comes back to its own pitches); a vibrato or a slide never jumps that far
// from one frame to the next.
bool legato(const std::vector<double>& cents, int f, int start, int from) {
    const int n = int(cents.size());
    if (f <= start || f + 2 >= n) return false;
    const double a = cents[size_t(f)], b = cents[size_t(f - 1)];
    if (std::isnan(a) || std::isnan(b) || std::abs(a - b) < kLegatoCents) return false;
    for (int g = f + 1; g <= f + 2; ++g)
        if (std::isnan(cents[size_t(g)]) || std::abs(cents[size_t(g)] - a) > kLegatoHold) return false;
    for (int g = std::max(from, f - 12); g < f; ++g)
        if (!std::isnan(cents[size_t(g)]) && std::abs(cents[size_t(g)] - a) <= kLegatoHold) return false;
    return true;
}

// Steps of the grid where notes are looked for: `frames` frames from frame `offset`; 0 frames: no grid yet (the notes
// that guess it, from their attacks only).
struct Steps {
    int frames = 0, offset = 0;
    const std::vector<int>* starts = nullptr;  // or the frames where the steps of known rows start
    bool any() const noexcept { return frames > 0 || starts; }
    bool on(int f) const noexcept {
        if (starts) return std::binary_search(starts->begin(), starts->end(), f);
        return frames > 0 && ((f - offset) % frames + frames) % frames == 0;
    }
};

std::vector<Note> splitLong(const std::vector<Frame>& fr, const std::vector<Note>& notes, int c, Steps grid);

// The notes of voice c (see the file comment).
std::vector<Note> findNotes(const std::vector<Frame>& fr, int c, Steps grid) {
    std::vector<Note> notes;
    const int n = int(fr.size());
    const std::vector<double> cents = centsOf(fr, c);
    auto close = [&](int start, int end) {
        Note x;
        x.voice = c;
        x.start = start;
        x.end = end;
        notes.push_back(x);
    };
    // Frame g plays another sound than g - 1: its mixer changes, or its pitch jumps.
    auto turns = [&](int g) {
        const Chan& a = fr[size_t(g)].ch[size_t(c)];
        const Chan& b = fr[size_t(g - 1)].ch[size_t(c)];
        return a.tone != b.tone || a.noise != b.noise ||
               (!std::isnan(cents[size_t(g)]) && !std::isnan(cents[size_t(g - 1)]) &&
                std::abs(cents[size_t(g)] - cents[size_t(g - 1)]) >= kLegatoCents);
    };
    // On a step, the pitch jumps to one that the note has not played in the frames before (a step of them, 8 at
    // least: an arpeggio plays all of its pitches within them), whether it holds there or not.
    auto stepJump = [&](int g, int start) {
        if (!grid.on(g) || g < start + 2 || !turns(g) || std::isnan(cents[size_t(g)])) return false;
        for (int h = std::max(start, g - std::max(grid.frames, 8)); h < g; ++h)
            if (!std::isnan(cents[size_t(h)]) && std::abs(cents[size_t(h)] - cents[size_t(g)]) <= kLegatoHold)
                return false;
        return std::abs(cents[size_t(g)] - cents[size_t(g - 1)]) >= kLegatoCents;
    };
    bool in = false;
    int start = 0, peak = 0, hard = 0;  // hard: the last attack that was not a legato
    for (int f = 0; f <= n; ++f) {
        if (f == n || !audible(fr[size_t(f)], c)) {
            if (in) close(start, f);
            in = false;
            continue;
        }
        const Chan& x = fr[size_t(f)].ch[size_t(c)];
        int attack = in ? -1 : f;  // frame of a new attack
        if (in) {
            const Chan& p = fr[size_t(f - 1)].ch[size_t(c)];
            // The envelope restarts under the voice (not where it restarts again at the next frame: some replays
            // write R13 a frame before their note; nor at the end of a click of 3 frames at most that the note plays
            // before its envelope).
            if (x.env && fr[size_t(f)].restart && !(f + 1 < n && fr[size_t(f + 1)].restart)) {
                bool click = f - start <= 3;
                for (int g = start; g < f && click; ++g) click = !fr[size_t(g)].ch[size_t(c)].env;
                if (!click) attack = f;
            }
            // The volume jumps back up after a decay (not the rise of an attack); the frame before it starts the note
            // when it already plays another sound after a steady one (a click of noise, or of another pitch, at the
            // same volume; not an arpeggio, whose every frame turns).
            else if (!x.env && !p.env && f >= start + 2 && x.vol >= p.vol + 2 && p.vol + 3 <= peak)
                attack = f - 1 >= start + 3 && turns(f - 1) && !turns(f - 2) ? f - 1 : f;
            // Noise comes in after 3 frames without it, at the loudest volume of the note (a click of noise at the
            // attack; not the noise that a drum lets in again as it decays).
            else if (!x.env && x.vol >= peak && f >= start + 3 && x.noise && !p.noise &&
                     !fr[size_t(f - 2)].ch[size_t(c)].noise && !fr[size_t(f - 3)].ch[size_t(c)].noise)
                attack = f;
        }
        if (attack >= 0) {
            if (in) close(start, attack);
            in = true;
            start = hard = attack;
            peak = attack < f && !fr[size_t(attack)].ch[size_t(c)].env ? fr[size_t(attack)].ch[size_t(c)].vol : 0;
        } else if (grid.frames > 0 && (stepJump(f, start) || legato(cents, f, start, hard))) {
            // On a step, or a frame or two after one (the first frames of a note often jump around its pitch).
            int at = -1;
            for (int g = f; g >= f - 2 && at < 0; --g)
                if (g >= start + 2 && grid.on(g)) at = g;
            if (at >= 0) {
                close(start, at);
                start = at;
                peak = 0;
                for (int g = at; g < f; ++g)
                    if (!fr[size_t(g)].ch[size_t(c)].env) peak = std::max<int>(peak, fr[size_t(g)].ch[size_t(c)].vol);
            }
        }
        if (!x.env) peak = std::max<int>(peak, x.vol);
    }
    if (grid.frames == 0) return notes;
    return splitLong(fr, notes, c, grid);
}

// A note longer than a table that does not hold still goes on as a new note at its last step within the table (not
// under the envelope, which a new note would restart).
std::vector<Note> splitLong(const std::vector<Frame>& fr, const std::vector<Note>& notes, int c, Steps grid) {
    std::vector<Note> out;
    for (Note x : notes) {
        while (x.end - x.start > kTable) {
            int at = x.start + kTable;
            while (at > x.start + 1 && !grid.on(at)) --at;
            const Frame& f = fr[size_t(at)];
            if (f.ch[size_t(c)].env && f.fx[size_t(c)].gen != Generator::SyncBuzzer) break;
            bool holds = true;
            const Frame& h = fr[size_t(at - 1)];
            const TimerEffect& ht = h.fx[size_t(c)];
            for (int g = at; g < x.end && holds; ++g) {
                const Frame& q = fr[size_t(g)];
                const TimerEffect& qt = q.fx[size_t(c)];
                holds = q.ch[size_t(c)] == h.ch[size_t(c)] && (!q.ch[size_t(c)].noise || q.np == h.np) &&
                        qt.gen == ht.gen && qt.wave == ht.wave && std::abs(qt.hz - ht.hz) <= 0.002 * ht.hz;
            }
            if (holds) break;
            Note head = x;
            head.end = at;
            out.push_back(head);
            x.start = at;
        }
        out.push_back(x);
    }
    return out;
}

std::vector<Note> allNotes(const std::vector<Frame>& fr, Steps grid) {
    std::vector<Note> notes;
    for (int c = 0; c < 3; ++c) {
        std::vector<Note> v = findNotes(fr, c, grid);
        notes.insert(notes.end(), v.begin(), v.end());
    }
    return notes;
}

// Best phase for steps of s frames and the share of the note starts on a step there.
std::pair<int, int> phaseOf(const std::vector<Note>& notes, int s) {
    std::vector<int> count(size_t(s), 0);
    for (const Note& x : notes) ++count[size_t(x.start % s)];
    const int phase = int(std::max_element(count.begin(), count.end()) - count.begin());
    return {phase, notes.empty() ? 100 : int(int64_t(count[size_t(phase)]) * 100 / int64_t(notes.size()))};
}

int defaultPatternSteps(int s) {
    const int k = int(roundTo(double(kPatternFrames) / s / 16));
    return std::clamp(k * 16, 16, ymseq::kSteps);
}

RegisterGrid gridFor(const std::vector<Note>& notes, int s) {
    RegisterGrid g;
    g.framesPerStep = s;
    const auto [phase, aligned] = phaseOf(notes, s);
    g.aligned = aligned;
    g.offset = phase;
    for (const Note& x : notes)
        if (x.start < phase) g.offset = phase - s;  // the tune starts within the first step
    g.stepsPerPattern = defaultPatternSteps(s);
    g.notes = int(notes.size());
    return g;
}

// --------------------------------------------------------------------------------------------- Sounds
// One step of a table.
struct Step {
    int8_t semis = 0, per = 0;
    uint8_t vol = 15, mix = 1, noise = 0;
    uint8_t gen = 0;  // GEN row: 0 the generator of the sound, 1 Chip Voice, 2 SID Voice, 3 Sync Buzzer, 4 FM Voice
    bool operator==(const Step&) const = default;
};
// What an instrument plays.
struct Sound {
    Generator gen = Generator::Chip;
    uint16_t ep = 0;      // Chip Voice: envelope period of the note (0: no envelope); Sync Buzzer not tuned: its own
    bool tuned = true;    // Sync Buzzer: the envelope at the note × ratio / ratioD, shifted by envFine cents
    uint8_t ratio = 1, ratioD = 1;
    int8_t envFine = 0;
    bool pad = false;     // Sync Buzzer: YM Pad
    // Timer effects of a tracker's tune: SID (its waveform, the volume of each step at volume 15, played at
    // the note × sidRatio), sync buzzer (the R13 shapes written in turn at each restart) or FM (the multiple of
    // the note's frequency of each step, or with fmSemis its semitones above the note, played at the note ×
    // sidRatio).
    bool sid = false, buzz = false, fm = false, fmSemis = false;
    std::vector<uint8_t> wave;
    double sidRatio = 1;
    bool sidHold = false;     // the SID at the note × sidRatio, not following the table (its arpeggio, its vibrato)
    bool squareHold = false;  // the table plays the SID's pitch, the square holds the note's
    std::vector<Step> table;
    uint8_t shape = 8;
    double tp0 = 0;       // tone period of the note it comes from (PER in cents)
    int64_t weight = 0;   // frames played
    int first = INT_MAX;  // first frame played
    bool sameKind(const Sound& o) const noexcept {
        return gen == o.gen && ep == o.ep && tuned == o.tuned && ratio == o.ratio && ratioD == o.ratioD &&
               envFine == o.envFine && pad == o.pad && sid == o.sid && buzz == o.buzz && fm == o.fm && wave == o.wave &&
               fmSemis == o.fmSemis &&
               sidRatio == o.sidRatio && sidHold == o.sidHold && squareHold == o.squareHold;
    }
};

uint64_t hashOf(const Sound& s, size_t len) noexcept {
    uint64_t h = 1469598103934665603ull;
    auto mixIn = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mixIn(uint64_t(s.gen) | uint64_t(s.ep) << 8 | uint64_t(s.ratio) << 24 | uint64_t(s.pad) << 32 |
          uint64_t(s.ratioD) << 33 | uint64_t(uint8_t(s.envFine)) << 41 | uint64_t(s.tuned) << 49 |
          uint64_t(s.sid) << 50 | uint64_t(s.buzz) << 51 | uint64_t(s.sidHold) << 52 | uint64_t(s.squareHold) << 53 |
          uint64_t(s.fm) << 54 | uint64_t(s.fmSemis) << 55);
    mixIn(uint64_t(std::llround(s.sidRatio * 1000)));
    for (const uint8_t w : s.wave) mixIn(w);
    for (size_t i = 0; i < len; ++i) {
        const Step& x = s.table[i];
        mixIn(uint64_t(uint8_t(x.semis)) | uint64_t(uint8_t(x.per)) << 8 | uint64_t(x.vol) << 16 |
              uint64_t(x.mix) << 24 | uint64_t(x.noise) << 32 | uint64_t(x.gen) << 38 | uint64_t(i + 1) << 40);
    }
    return h;
}

// Pitch in cents of step x of a table played on a note of tone period tp0 (as the engine computes it).
double stepCents(const Step& x, double tp0) noexcept {
    const double p = std::floor(tp0 * std::pow(2.0, -x.semis / 12.0) + 0.5) + x.per;
    return 1200 * std::log2(tp0 / std::max(1.0, p));
}

// Cost per frame of playing the notes of `a` with the table of `b` (b's last step held beyond its end): a step of
// volume counts 1, 10 cents 1 up to 3 (a wrong pitch costs the same however far it is), another mixer 4, a step of
// noise period 1; a different generator or envelope much more.
double distance(const Sound& a, const Sound& b) {
    double d = 0;
    if (a.gen != b.gen) d += 1000;
    if (a.ratio != b.ratio || a.ratioD != b.ratioD || a.envFine != b.envFine || a.tuned != b.tuned || a.pad != b.pad)
        d += 30;
    if (a.sid != b.sid || a.buzz != b.buzz || a.fm != b.fm || a.fmSemis != b.fmSemis || a.wave != b.wave ||
        a.sidRatio != b.sidRatio ||
        a.sidHold != b.sidHold || a.squareHold != b.squareHold)
        d += 30;
    if (a.ep != b.ep) d += (a.ep && b.ep) ? 40 * std::abs(std::log2(double(a.ep) / b.ep)) : 200;
    const double tp0 = a.tp0 > 0 ? a.tp0 : 500;
    for (size_t i = 0; i < a.table.size(); ++i) {
        const Step& x = a.table[i];
        const Step& y = b.table[std::min(i, b.table.size() - 1)];
        d += std::abs(x.vol - y.vol) + (x.mix != y.mix ? 4 : 0) + (x.gen != y.gen ? 4 : 0) +
             std::min(3.0, std::abs(stepCents(x, tp0) - stepCents(y, tp0)) / 10) + std::abs(x.noise - y.noise);
    }
    return d / double(std::max<size_t>(1, a.table.size()));
}

// --------------------------------------------------------------------------------------------- Remarks
struct Counts {
    int preroll = 0, merged = 0, clamped = 0, epChanged = 0, longer = 0, retrigLost = 0, outOfRange = 0;
    int sidScaled = 0, buzzSquare = 0, buzzEp = 0;  // timer effects
};

// A timer effect that becomes a generator of the instrument: SID Voice, Sync Buzzer or FM Voice.
bool timerGen(Generator g) noexcept { return g == Generator::Sid || g == Generator::SyncBuzzer || g == Generator::Fm; }
// GEN row value of a generator: 1 Chip Voice, 2 SID Voice, 3 Sync Buzzer, 4 FM Voice.
uint8_t genCode(Generator g) noexcept {
    return uint8_t(g == Generator::Sid ? ymengine::SeqGen::Sid
                   : g == Generator::SyncBuzzer ? ymengine::SeqGen::SyncBuzzer
                   : g == Generator::Fm ? ymengine::SeqGen::Fm
                                        : ymengine::SeqGen::Chip);
}

// N / D (1 to kRatioMax) and fine tune in cents closest to the ratio r, the smallest D first; false beyond
// ymengine::kEnvFineMax cents.
bool envRatio(double r, uint8_t& n, uint8_t& d, int8_t& cents) noexcept {
    double best = HUGE_VAL;
    for (int dd = 1; dd <= kRatioMax; ++dd) {
        const int64_t nn = roundTo(r * dd);
        if (nn < 1 || nn > kRatioMax) continue;
        const double ct = 1200 * std::log2(r * dd / double(nn));
        if (std::abs(ct) < std::abs(best) - 0.5) {
            best = ct;
            n = uint8_t(nn);
            d = uint8_t(dd);
        }
    }
    if (!(std::abs(best) <= ymengine::kEnvFineMax)) return false;
    cents = int8_t(roundTo(best));
    return true;
}

// Base note, fine tune and table of a note; its sound.
Sound soundOf(const std::vector<Frame>& fr, Note& x, double a4, Counts& counts) {
    const int c = x.voice;
    const int len = std::min(x.end - x.start, kTable);
    Sound s;
    // Timer effects: the first frame of the note with a SID, a sync buzzer or an FM timer gives their
    // settings, the first frame of the note its generator.
    int fxAt = -1;
    for (int f = x.start; f < x.end && fxAt < 0; ++f)
        if (timerGen(fr[size_t(f)].fx[size_t(c)].gen)) fxAt = f;
    const bool timers = fxAt >= 0;
    int kEng = 2;  // sync buzzer: its steps in the engine, which restarts it at the note × kEng
    if (timers) {
        const Generator g0 = fr[size_t(x.start)].fx[size_t(c)].gen;
        s.gen = timerGen(g0) ? g0 : Generator::Chip;
        const Frame& e = fr[size_t(fxAt)];
        const TimerEffect& t = e.fx[size_t(c)];
        const Chan& a = e.ch[size_t(c)];
        const int steps = std::clamp<int>(t.steps, 1, int(t.wave.size()));
        if (t.gen == Generator::Sid) {
            // Its waveform (a single step: that level held, over two); the timer at the note × ratio × steps.
            s.sid = true;
            s.wave.assign(t.wave.begin(), t.wave.begin() + steps);
            if (steps == 1) s.wave.push_back(s.wave[0]);
            const double th = toneHz(a);
            double ratio = th > 0 && steps > 1 ? t.hz / steps / th : 1;
            if (std::abs(ratio - 1) < 0.01) ratio = 1;
            s.sidRatio = std::round(std::clamp(ratio, 0.25, 8.0) * 1000) / 1000;
        } else if (t.gen == Generator::Fm) {
            // Its steps, each a multiple of the note's frequency or semitones above it; the timer at
            // the note × ratio × steps.
            s.fm = true;
            s.fmSemis = t.semis;
            const int top = t.semis ? ymengine::kFmSemitoneMax : ymengine::kFmMultipleCount - 1;
            for (int i = 0; i < steps; ++i) s.wave.push_back(uint8_t(std::min<int>(t.wave[size_t(i)], top)));
            const double th = toneHz(a);
            double ratio = th > 0 ? t.hz / steps / th : 1;
            if (std::abs(ratio - 1) < 0.01) ratio = 1;
            s.sidRatio = std::round(std::clamp(ratio, 0.25, 8.0) * 1000) / 1000;
        } else {
            // Its shapes, written at each restart (the tune restarts it twice per period of the note; a single shape
            // is written twice); n steps beyond 2 restart it n times per period of a note n / 2 times lower.
            s.buzz = true;
            for (int i = 0; i < steps; ++i) s.wave.push_back(menuShape(t.wave[size_t(i)]));
            if (std::all_of(s.wave.begin(), s.wave.end(), [&](uint8_t w) { return w == s.wave[0]; }))
                s.wave.assign(2, s.wave[0]);
            kEng = int(s.wave.size());
            s.shape = s.wave[0];
            // The envelope at the restart rate × N / D, fine-tuned; otherwise its period, which no longer follows
            // the note.
            const double r = kChipHz / (256.0 * ymengine::envelopeRamps(s.shape) * e.ep * t.hz);
            if (!envRatio(r, s.ratio, s.ratioD, s.envFine)) {
                s.tuned = false;
                s.ep = e.ep;
                ++counts.buzzEp;
            }
            s.pad = a.tone && a.tp < kMinTonePeriod;
            if (kEng > 2 && a.tone && a.tp >= kMinTonePeriod) ++counts.buzzSquare;
        }
    }
    // Envelope: the first frame where the note plays it.
    int envAt = -1;
    for (int f = x.start; f < x.end && envAt < 0; ++f)
        if (fr[size_t(f)].ch[size_t(c)].env) envAt = f;
    x.env = envAt >= 0;
    if (x.env) {
        const Frame& e = fr[size_t(envAt)];
        if (!s.buzz) s.shape = menuShape(e.shape);
        x.shape = s.shape;
        // A buzzer from the start: Sync Buzzer when its frequency is the note's × n, or the note itself.
        if (!timers && envAt == x.start && buzzer(e, c)) {
            const double th = toneHz(e.ch[size_t(c)]), eh = envHz(e);
            if (th <= 0) {
                s.gen = Generator::SyncBuzzer;
                s.pad = e.ch[size_t(c)].tone;  // tone at a period below hearing: YM Pad
            } else {
                const double r = eh / th;
                const int n = int(roundTo(r));
                if (n >= 1 && n <= kRatioMax && std::abs(r / n - 1) < 0.01) {
                    s.gen = Generator::SyncBuzzer;
                    s.ratio = uint8_t(n);
                }
            }
        }
        if (s.gen == Generator::Chip && !s.buzz) s.ep = e.ep;
        for (int f = envAt + 1; f < x.end && !(s.buzz && s.tuned); ++f)
            if (fr[size_t(f)].ch[size_t(c)].env && fr[size_t(f)].ep != e.ep) {
                ++counts.epChanged;
                break;
            }
    }
    // Pitch period of a frame: the tone's, or the note's period under a Sync Buzzer that plays without tone.
    // A timer effect: the note whose timer it is.
    auto period = [&](int f) -> double {
        const Frame& q = fr[size_t(f)];
        const Chan& a = q.ch[size_t(c)];
        const TimerEffect& t = q.fx[size_t(c)];
        if (t.gen == Generator::SyncBuzzer && t.hz > 0) return kChipHz * kEng / (16 * t.hz);
        if ((t.gen == Generator::Sid || t.gen == Generator::Fm) && t.hz > 0 && toneHz(a) <= 0)
            return kChipHz * std::max<int>(1, t.steps) / (16 * t.hz * s.sidRatio);
        if (s.gen == Generator::SyncBuzzer && !s.buzz && toneHz(a) <= 0) {
            const double eh = buzzer(q, c) ? envHz(q) : 0;
            return eh > 0 ? kChipHz / 16 / (eh / s.ratio) : 0;
        }
        return a.tone && a.tp > 0 ? double(a.tp) : 0;
    };
    // NOTE and Fxx: the first audible pitch of the note; where its pitches spread beyond the ±12 semitones of the
    // table, the semitone from which the table reaches the most of its frames (the first pitch if it is one of them).
    // A known note is the base itself, the table reaching its frames from it the same way.
    bool have = x.known >= 0;
    double first = have ? double(x.known) : 0;
    if (have && s.buzz && s.gen == Generator::SyncBuzzer) first += 12 * std::log2(2.0 / kEng);
    std::vector<int> rel;  // semitones of the frames from the first pitch
    for (int f = x.start; f < x.start + len; ++f) {
        const double p = period(f);
        if (p <= 0 || (p < kMinTonePeriod && s.gen != Generator::SyncBuzzer)) continue;
        const double semi = 69 + 12 * std::log2(kChipHz / 16 / p / a4);
        if (!have) {
            first = semi;
            have = true;
        }
        rel.push_back(int(roundTo(semi - first)));
    }
    x.note = 60;
    x.fine = 0;
    if (have) {
        x.note = int(roundTo(first));
        const int cents = int(roundTo((first - x.note) * 100));
        x.fine = std::abs(cents) >= kMinFineCents ? cents : 0;
        const auto [lo, hi] = std::minmax_element(rel.begin(), rel.end());
        if (rel.empty()) {
            // No pitch heard: the known note stays the base.
        } else if (*hi - *lo > 24) {
            int best = 0, reach = -1;
            for (int b = *lo + 12; b <= *hi - 12; ++b) {
                int n = 0;
                for (const int v : rel) n += std::abs(v - b) <= 12;
                if (n > reach || (n == reach && std::abs(b) < std::abs(best))) {
                    reach = n;
                    best = b;
                }
            }
            int fromFirst = 0;
            for (const int v : rel) fromFirst += std::abs(v) <= 12;
            if (fromFirst < reach) x.note += best;
        } else if (*hi > 12 || *lo < -12) {
            x.note += *hi > 12 ? *hi - 12 : *lo + 12;  // all of them within reach
        }
        if (x.note < ymseq::kPlayLow || x.note > ymseq::kMapHigh) ++counts.outOfRange;
        x.note = std::clamp(x.note, ymseq::kPlayLow, ymseq::kMapHigh);
    }
    const double tp0 = double(ymengine::tonePeriodFor(ymengine::noteHz(x.note + x.fine / 100.0, a4),
                                                      ymcore::clocks::kDefault));
    bool clamped = false, sidLow = false;
    s.table.resize(size_t(len));
    // Semitones and PER of a step that plays period p; false where PER does not reach it.
    auto setPitch = [&](Step& st, double p) {
        const int semis = int(std::clamp<int64_t>(roundTo(12 * std::log2(tp0 / p)), -12, 12));
        const double base = std::floor(tp0 * std::pow(2.0, -semis / 12.0) + 0.5);
        int64_t delta = roundTo(p - base);
        if (std::abs(1200 * std::log2(base / p)) < kPerCents) delta = 0;
        st.semis = int8_t(semis);
        st.per = int8_t(std::clamp<int64_t>(delta, -ymengine::kPeriodDeltaMax, ymengine::kPeriodDeltaMax));
        return std::abs(delta) <= ymengine::kPeriodDeltaMax;
    };
    for (int i = 0; i < len; ++i) {
        const int f = x.start + i;
        const Frame& q = fr[size_t(f)];
        const Chan& a = q.ch[size_t(c)];
        Step& st = s.table[size_t(i)];
        st.vol = a.env ? 15 : a.vol;
        st.mix = uint8_t(1 + (a.tone ? 1 : 0) + (a.noise ? 2 : 0) + (a.env ? 4 : 0));
        st.noise = a.noise ? q.np : 0;
        if (timers) {
            const Generator g = q.fx[size_t(c)].gen;
            const uint8_t code = genCode(timerGen(g) ? g : Generator::Chip);
            st.gen = code == genCode(s.gen) ? 0 : code;
            sidLow = sidLow || (g == Generator::Sid && !a.env && a.vol > 0 && a.vol < 15);
        }
        const double p = period(f);
        if (p <= 0) continue;  // no pitch: the steps keep 0
        const bool heard = audible(q, c) && (p >= kMinTonePeriod || s.gen == Generator::SyncBuzzer);
        if (!setPitch(st, p) && heard) clamped = true;
    }
    counts.clamped += clamped;
    counts.sidScaled += sidLow;
    // The SID follows the pitch of the table (as the square does) or holds the note's (a tracker whose timer leaves
    // the arpeggio out): whichever plays the pitch of its waveform closer, each at the ratio of the first frame.
    // The FM Voice likewise, its steps at any count (maxYMiser's square FM whose timer holds a pitch while the
    // square slides).
    const auto timed = [&](const TimerEffect& t) {
        return t.hz > 0 && ((s.sid && t.gen == Generator::Sid && t.steps > 1) || (s.fm && t.gen == Generator::Fm));
    };
    if ((s.sid && s.wave.size() > 1) || (s.fm && !s.wave.empty())) {
        auto tableHz = [&](int i) {
            const Step& st = s.table[size_t(i)];
            const double p = std::floor(tp0 * std::pow(2.0, -st.semis / 12.0) + 0.5) + st.per;
            return kChipHz / 16 / std::max(1.0, p);
        };
        auto snap = [](double r) {
            r = std::clamp(r, 0.25, 8.0);
            return std::abs(r - 1) < 0.01 ? 1.0 : std::round(r * 1000) / 1000;
        };
        const double noteHz0 = kChipHz / 16 / tp0;
        // Cents off beyond kPerCents (the MFP rounds the rate of the timer anyway).
        auto off = [](double r) { return std::max(0.0, std::abs(1200 * std::log2(r)) - kPerCents); };
        double follow = s.sidRatio, hold = 0, errFollow = 0, errHold = 0;
        for (int i = 0; i < len; ++i) {
            const TimerEffect& t = fr[size_t(x.start + i)].fx[size_t(c)];
            if (!timed(t)) continue;
            const double want = t.hz / std::max<int>(1, t.steps);
            if (hold == 0) hold = snap(want / noteHz0);
            errFollow += off(tableHz(i) * follow / want);
            errHold += off(noteHz0 * hold / want);
        }
        // Or the table plays the SID's pitch and the square holds the note's (a timer with its own slide).
        double errSquare = 0;
        bool any = false;
        for (int i = 0; i < len; ++i) {
            const Frame& q = fr[size_t(x.start + i)];
            const Chan& a = q.ch[size_t(c)];
            any = any || timed(q.fx[size_t(c)]);
            if (audible(q, c) && toneHz(a) > 0) errSquare += off(a.tp / tp0);
        }
        if (hold > 0 && errHold + 1 < errFollow && errHold <= errSquare) {
            s.sidHold = true;
            s.sidRatio = hold;
        } else if (any && errSquare + 1 < std::min(errFollow, hold > 0 ? errHold : HUGE_VAL)) {
            s.squareHold = true;
            s.sidRatio = 1;
            for (int i = 0; i < len; ++i) {
                const TimerEffect& t = fr[size_t(x.start + i)].fx[size_t(c)];
                Step& st = s.table[size_t(i)];
                st.semis = 0;
                st.per = 0;
                if (timed(t)) setPitch(st, kChipHz * std::max<int>(1, t.steps) / (16 * t.hz));
            }
        }
    }
    // Frames beyond the table that do not hold its last step.
    if (x.end - x.start > kTable) {
        const Frame& h = fr[size_t(x.start + kTable - 1)];
        for (int f = x.start + kTable; f < x.end; ++f)
            if (!(fr[size_t(f)].ch[size_t(c)] == h.ch[size_t(c)])) {
                ++counts.longer;
                break;
            }
    }
    s.tp0 = tp0;
    s.weight = x.end - x.start;
    s.first = x.start;
    return s;
}

// Notes grouped into sounds: the same table, or the start of a longer one; beyond kInstruments, the least played
// merge into the closest. Returns the sounds, each note's `sound` set.
std::vector<Sound> groupSounds(std::vector<Note>& notes, std::vector<Sound>& own, Counts& counts) {
    std::vector<size_t> order(notes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return own[a].table.size() > own[b].table.size(); });
    std::vector<Sound> sounds;
    std::unordered_multimap<uint64_t, size_t> prefixes;  // hash of each start of each sound's table
    for (size_t i : order) {
        const Sound& s = own[i];
        const uint64_t h = hashOf(s, s.table.size());
        int found = -1;
        auto [a, b] = prefixes.equal_range(h);
        for (auto it = a; it != b && found < 0; ++it) {
            const Sound& t = sounds[it->second];
            if (t.sameKind(s) && std::equal(s.table.begin(), s.table.end(), t.table.begin())) found = int(it->second);
        }
        if (found < 0) {
            found = int(sounds.size());
            sounds.push_back(s);
            sounds.back().weight = 0;
            sounds.back().first = INT_MAX;
            for (size_t k = 1; k <= s.table.size(); ++k) prefixes.emplace(hashOf(s, k), size_t(found));
        }
        Sound& t = sounds[size_t(found)];
        t.weight += s.weight;
        t.first = std::min(t.first, s.first);
        notes[i].sound = found;
    }
    // Beyond kInstruments: the least played merges into the closest (its table carries on that one's when longer).
    const size_t n = sounds.size();
    std::vector<int> into(n);
    for (size_t i = 0; i < n; ++i) into[i] = int(i);
    std::vector<bool> alive(n, true);
    size_t count = n;
    if (count > size_t(kInstruments)) {
        std::vector<std::vector<double>> dist(n, std::vector<double>(n, 0));
        auto row = [&](size_t a) {
            for (size_t b = 0; b < n; ++b)
                if (b != a && alive[b]) {
                    dist[a][b] = distance(sounds[a], sounds[b]);
                    dist[b][a] = distance(sounds[b], sounds[a]);
                }
        };
        for (size_t a = 0; a < n; ++a)
            for (size_t b = 0; b < n; ++b)
                if (a != b) dist[a][b] = distance(sounds[a], sounds[b]);
        while (count > size_t(kInstruments)) {
            // The merge that costs the least: the frames of the sound times their cost with the closest one.
            size_t a = n, b = n;
            double best = HUGE_VAL;
            for (size_t i = 0; i < n; ++i) {
                if (!alive[i]) continue;
                for (size_t j = 0; j < n; ++j)
                    if (alive[j] && j != i) {
                        const double cost = double(sounds[i].weight) * dist[i][j];
                        if (cost < best) {
                            best = cost;
                            a = i;
                            b = j;
                        }
                    }
            }
            alive[a] = false;
            into[a] = int(b);
            Sound& t = sounds[b];
            Sound& s = sounds[a];
            if (s.table.size() > t.table.size())
                t.table.insert(t.table.end(), s.table.begin() + std::ptrdiff_t(t.table.size()), s.table.end());
            t.weight += s.weight;
            t.first = std::min(t.first, s.first);
            --count;
            row(b);
        }
    }
    // The sounds kept, in the order of their first note.
    std::vector<size_t> kept;
    for (size_t i = 0; i < n; ++i)
        if (alive[i]) kept.push_back(i);
    std::stable_sort(kept.begin(), kept.end(), [&](size_t a, size_t b) { return sounds[a].first < sounds[b].first; });
    std::vector<int> rank(n, -1);
    for (size_t k = 0; k < kept.size(); ++k) rank[kept[k]] = int(k);
    for (Note& x : notes) {
        int s = x.sound;
        const bool moved = into[size_t(s)] != s;
        while (into[size_t(s)] != s) s = into[size_t(s)];
        counts.merged += moved;
        x.sound = rank[size_t(s)];
    }
    std::vector<Sound> out;
    for (size_t i : kept) out.push_back(std::move(sounds[i]));
    return out;
}

Instrument instrumentOf(const Sound& s, const std::string& name) {
    Instrument ins;
    ins.used = true;
    ins.name = cleanInstrumentName(name);
    ins.level = 1.0;
    ymengine::TrackPatch& p = ins.patch;
    p.generator = s.gen;
    const Step& head = s.table.front();
    const int mode = head.mix - 1;
    p.toneOn = (mode & 1) != 0;
    p.noiseOn = (mode & 2) != 0;
    p.envelopeVolume = false;  // the envelope comes from MIX (and SRC), which can then switch it off
    p.volume = 15;
    p.noisePeriod = 8;
    for (const Step& x : s.table)
        if (x.noise) {
            p.noisePeriod = x.noise;
            break;
        }
    if (s.ep) p.envPeriod = s.ep;
    p.envShape = s.shape;
    p.ornamentOn = true;
    p.ornamentLength = uint8_t(s.table.size());
    p.ornamentLoop = false;  // Hold
    p.ornamentRate = 1;
    for (size_t i = 0; i < s.table.size(); ++i) {
        const Step& x = s.table[i];
        p.ornamentPitch[i] = x.semis;
        p.ornamentVolume[i] = x.vol;
        p.ornamentNoise[i] = 0;
        p.rows.period[i] = x.per;
        p.rows.noise[i] = x.noise;
        p.rows.mix[i] = x.mix;
        p.rows.gen[i] = x.gen;
    }
    if (s.sid) {
        p.sidForm = ymengine::SidForm::Custom;
        p.sidWaveLen = uint8_t(std::min(s.wave.size(), p.sidWave.size()));
        for (size_t i = 0; i < p.sidWaveLen; ++i) p.sidWave[i] = s.wave[i];
        p.sidRatio = s.sidRatio;
    }
    if (s.fm) {
        p.fmWaveLen = uint8_t(std::min(s.wave.size(), p.fmWave.size()));
        p.fmScale = s.fmSemis ? ymengine::FmScale::Semitones : ymengine::FmScale::Ratios;
        auto& wave = s.fmSemis ? p.fmSemi : p.fmWave;
        for (size_t i = 0; i < p.fmWaveLen; ++i) wave[i] = s.wave[i];
        p.sidRatio = s.sidRatio;
    }
    if (s.sidHold)
        p.pitchMatrix = uint16_t(p.pitchMatrix &
                                 ~ymengine::pitchBit(ymengine::PitchSource::Table, ymengine::PitchTarget::Timer));
    if (s.squareHold)
        p.pitchMatrix = uint16_t(p.pitchMatrix &
                                 ~ymengine::pitchBit(ymengine::PitchSource::Table, ymengine::PitchTarget::Square));
    if (s.buzz) {
        p.buzzLen = uint8_t(std::min(s.wave.size(), p.buzzShapes.size()));
        for (size_t i = 0; i < p.buzzLen; ++i) p.buzzShapes[i] = s.wave[i];
    }
    if (s.gen == Generator::SyncBuzzer || s.buzz) {
        p.envTuned = s.tuned;
        p.envRatioN = s.ratio;
        p.envRatioD = s.ratioD;
        p.envFineCents = s.envFine;
        p.ymPad = s.pad;
        // The envelope follows the pitch of the table, as the square and the timer do.
        p.pitchMatrix = uint16_t(p.pitchMatrix |
                                 ymengine::pitchBit(ymengine::PitchSource::Table, ymengine::PitchTarget::Buzzer));
    }
    return ins;
}

std::string kindName(const Sound& s) {
    if (s.sid) return "SID";
    if (s.fm) return "FM";
    if (s.gen == Generator::SyncBuzzer || s.buzz) return "Buzz";
    if (s.ep) return "Env";
    const int mode = s.table.front().mix - 1;
    return (mode & 3) == 2 ? "Noise" : (mode & 3) == 3 ? "Mix" : "Tone";
}

// --------------------------------------------------------------------------------------------- Steps
// What one step of a track holds.
struct Cell {
    int note = -1;  // MIDI note of an attack
    bool tie = false;
    uint8_t gat = 0, ins = 0, mix = 0;
    int delay = 0, retrig = -1, fine = 0, shape = 0;
    bool operator==(const Cell&) const = default;
};

// GAT of a note that sounds `frames` frames of a step (or of its rest after Dxx) of `of` frames: its release half a
// frame before the frame where it is silent, so that the tick of that frame finds it released.
int gatPct(int frames, int of) { return std::clamp((2 * frames - 1) * 50 / std::max(1, of), 1, 99); }

std::string count(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string seconds(double s) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f s", s);
    return b;
}

}  // namespace

// --------------------------------------------------------------------------------------------- Frames of a file
RegisterTune registerTune(const ymio::YmFile& f) {
    using ymio::YmFormat;
    RegisterTune t;
    t.title = f.title;
    t.author = f.author;
    t.rate = std::max<uint32_t>(1, f.playerRate);
    t.clockHz = f.clock.hz();
    const uint32_t max = kRegisterImportMaxSeconds * t.rate;
    const uint32_t n = std::min(f.frameCount, max);
    t.cutFrames = f.frameCount - n;
    t.frames.resize(n);
    t.restart.assign(n, 0);
    uint8_t shape = 0, r11 = 0;
    for (uint32_t i = 0; i < n; ++i) {
        std::array<uint8_t, 14>& r = t.frames[i];
        for (unsigned k = 0; k <= 12; ++k) r[k] = f.reg(i, k);
        const uint8_t r13 = f.reg(i, 13);
        bool fx = false;
        if (f.format == YmFormat::YM2) {
            // Mad Max: R13 ≠ $FF writes R11, R12 = 0 and shape $A; bit 7 of R10 plays a digidrum on channel C.
            if (r13 != 0xff) {
                r11 = r[11];
                shape = 10;
                t.restart[i] = 1;
            }
            r[11] = r11;
            r[12] = 0;
            if (r[10] & 0x80) {
                r[10] = 0;
                ++t.digidrums;
                fx = true;
            }
        } else {
            if (r13 != 0xff) {
                shape = r13 & 15;
                t.restart[i] = 1;
            }
            if (f.format == YmFormat::YM5 || f.format == YmFormat::YM6) {
                // Effect codes (YmPlayer::decodeFx): R1 and R3, bits 4-5 the channel + 1, bits 6-7 the type ($00
                // SID, $40 digidrum, $80 Sinus-SID, $C0 Sync Buzzer); the channel's volume register holds its value.
                for (int slot = 0; slot < 2; ++slot) {
                    unsigned code = f.reg(i, slot ? 3 : 1) & 0xf0;
                    if (f.format == YmFormat::YM5) {
                        code &= 0x30;
                        if (code) code |= slot ? 0x40u : 0x00u;
                    }
                    if (!(code & 0x30)) continue;
                    fx = true;
                    const int voice = int((code & 0x30) >> 4) - 1;
                    if ((code & 0xc0) == 0x40) {  // a drum number, not a volume
                        r[size_t(8 + voice)] = 0;
                        ++t.digidrums;
                    } else if ((code & 0xc0) == 0xc0) {  // a shape: the voice plays the envelope
                        r[size_t(8 + voice)] = 0x10;
                    }
                }
            }
        }
        r[13] = shape;
        r[1] &= 15;
        r[3] &= 15;
        r[5] &= 15;
        r[6] &= 31;
        r[7] &= 63;
        for (unsigned k = 8; k <= 10; ++k) r[k] &= 31;
        t.effectFrames += fx;
    }
    t.loopFrame = f.loopFrame < n ? int(f.loopFrame) : 0;
    return t;
}

namespace {

// The end of a capture that repeats an earlier part, of 20 seconds at least: the tune is cut after its first pass,
// and loops where the repeat starts.
void findLoop(RegisterTune& t) {
    const size_t n = t.frames.size();
    const size_t w = std::min<size_t>(size_t(t.rate) * 20, n / 3);
    if (w < size_t(t.rate) * 4) return;
    std::vector<uint64_t> h(n);
    for (size_t i = 0; i < n; ++i) {
        uint64_t x = 1469598103934665603ull;
        for (uint8_t b : t.frames[i]) x = (x ^ b) * 1099511628211ull;
        h[i] = (x ^ t.restart[i]) * 1099511628211ull;
    }
    // Polynomial hash of the windows of w frames.
    constexpr uint64_t kBase = 0x9E3779B97F4A7C15ull;
    std::vector<uint64_t> pre(n + 1, 0);
    for (size_t i = 0; i < n; ++i) pre[i + 1] = pre[i] * kBase + h[i];
    uint64_t pw = 1;
    for (size_t i = 0; i < w; ++i) pw *= kBase;
    auto win = [&](size_t i) { return pre[i + w] - pre[i] * pw; };
    const size_t tail = n - w;
    const uint64_t target = win(tail);
    auto same = [&](size_t a, size_t b) { return t.frames[a] == t.frames[b] && t.restart[a] == t.restart[b]; };
    for (size_t j = n - 2 * w + 1; j-- > 0;) {
        if (win(j) != target) continue;
        bool equal = true;
        for (size_t i = 0; i < w && equal; ++i) equal = same(j + i, tail + i);
        if (!equal) continue;
        const size_t p = tail - j;
        size_t a = j;
        while (a > 0 && same(a - 1, a - 1 + p)) --a;
        t.frames.resize(a + p);
        t.restart.resize(a + p);
        t.loopFrame = int(a);
        return;
    }
}

}  // namespace

std::optional<RegisterTune> registerTune(const ymio::SndhFile& f, int subsong, uint32_t frames,
                                         const std::atomic<bool>* cancel) {
    RegisterTune t;
    t.title = f.info.title;
    t.author = f.info.composer;
    t.rate = uint32_t(std::max(1, f.info.tickRate));
    t.clockHz = double(ymio::SndhCapture::kClock);
    frames = std::min(frames, kRegisterImportMaxSeconds * t.rate);
    ymio::SndhCapture cap(f, subsong, kCaptureRate);
    // Two readings of the writes: those of the play routine at each tick (its timer interrupts being effects), or
    // all of them at the end of each tick, for a routine that plays the music from interrupts of its own.
    std::array<uint8_t, 14> play{}, all{};
    play[7] = all[7] = 0x3f;
    std::vector<std::array<uint8_t, 14>> byPlay, byAll;
    std::vector<uint8_t> restartPlay, restartAll;
    uint64_t tick = 0, playWrites = 0;
    bool started = false, rPlay = false, rAll = false, fx = false;
    uint32_t effectFrames = 0;
    std::vector<ymio::SndhEvent> events;
    const uint64_t chunk = uint64_t(cap.samplesPerTick()) * 50;
    auto mask = [](unsigned reg, uint8_t v) -> uint8_t {
        static constexpr uint8_t kMask[14] = {255, 15, 255, 15, 255, 15, 31, 63, 31, 31, 31, 255, 255, 15};
        return uint8_t(v & kMask[reg]);
    };
    bool alive = cap.ok();
    while (alive && byPlay.size() < frames) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return std::nullopt;
        events.clear();
        alive = cap.run(cap.sample() + chunk, events);
        for (const ymio::SndhEvent& e : events) {
            if (e.kind == ymio::SndhEvent::Kind::Dac) {
                t.dac = true;
            } else if (e.kind == ymio::SndhEvent::Kind::Tick) {
                if (started && byPlay.size() < frames) {
                    byPlay.push_back(play);
                    byAll.push_back(all);
                    restartPlay.push_back(rPlay);
                    restartAll.push_back(rAll);
                    effectFrames += fx;
                }
                started = true;
                tick = e.cycle;
                rPlay = rAll = fx = false;
            } else if (e.reg > 13) {
                fx = true;  // square-sync of a timer interrupt
            } else {
                const uint8_t v = mask(e.reg, e.value);
                all[e.reg] = v;
                rAll = rAll || e.reg == 13;
                if (!started || e.cycle == tick) {  // the init, or the play routine of this tick
                    play[e.reg] = v;
                    rPlay = rPlay || e.reg == 13;
                    playWrites += started;
                } else {
                    fx = true;
                }
            }
        }
    }
    if (byPlay.empty()) return std::nullopt;
    const bool byTicks = playWrites >= byPlay.size();
    t.frames = byTicks ? std::move(byPlay) : std::move(byAll);
    t.restart = byTicks ? std::move(restartPlay) : std::move(restartAll);
    t.effectFrames = byTicks ? effectFrames : 0;
    findLoop(t);
    // Without a loop, the silence after the last sound is left out.
    if (t.loopFrame < 0) {
        const std::vector<Frame> fr = decode(t);
        size_t last = 0;
        for (size_t i = 0; i < fr.size(); ++i)
            for (int c = 0; c < 3; ++c)
                if (audible(fr[i], c)) last = i + 1;
        t.frames.resize(std::max<size_t>(1, last));
        t.restart.resize(t.frames.size());
    }
    return t;
}

uint32_t sndhImportFrames(const ymio::SndhFile& f, int subsong) {
    const uint32_t rate = uint32_t(std::max(1, f.info.tickRate));
    uint32_t n = subsong >= 1 && size_t(subsong) <= f.info.frames.size() ? f.info.frames[size_t(subsong - 1)] : 0;
    if (!n) n = kSndhImportDefaultSeconds * rate;
    return std::min(n, kRegisterImportMaxSeconds * rate);
}

RegisterGrid guessRegisterGrid(const RegisterTune& t, int framesPerStep) {
    const std::vector<Frame> fr = decode(t);
    const std::vector<Note> notes = allNotes(fr, {});
    if (framesPerStep > 0) return gridFor(notes, std::min(framesPerStep, kGridMaxFrames));
    // The step: the longest where no more than kGridCollide % of the notes share a step with the one before on their
    // voice (a second note in a step goes on in the table of the first one), that puts half of the note starts on a
    // step or divides half of the times between two note starts of a voice, and that does either about as well as
    // each of its divisors: the times, less 10 %, or the starts, less 30 % (a step of 12 where the notes fall every 6
    // frames would put half of them within a step; a step of 8 where some notes come 2 frames late is still the
    // step). The time between note starts of a voice, not between those of the tune: a voice that plays a frame after
    // the others, or a shift of the whole tune by a frame now and then (some replays lose one at the end of their
    // patterns), do not hide the step.
    std::vector<int> gaps;
    for (size_t i = 1; i < notes.size(); ++i)
        if (notes[i].voice == notes[i - 1].voice) gaps.push_back(notes[i].start - notes[i - 1].start);
    if (gaps.empty()) return gridFor(notes, 1);
    std::array<int, kGridMaxFrames + 1> divides{};  // % of the gaps that s divides
    for (int s = 1; s <= kGridMaxFrames; ++s) {
        size_t on = 0;
        for (const int g : gaps) on += g % s == 0;
        divides[size_t(s)] = int(on * 100 / gaps.size());
    }
    std::array<int, kGridMaxFrames + 1> aligned{};  // % of the note starts on a step
    for (int s = 1; s <= kGridMaxFrames; ++s) aligned[size_t(s)] = gridFor(notes, s).aligned;
    for (int s = kGridMaxFrames; s > 1; --s) {
        bool keeps = std::max(divides[size_t(s)], aligned[size_t(s)]) >= 50;
        for (int d = 2; d < s && keeps; ++d)
            keeps = s % d != 0 || divides[size_t(s)] >= divides[size_t(d)] - 10 ||
                    aligned[size_t(s)] >= aligned[size_t(d)] - 30;
        if (!keeps) continue;
        const RegisterGrid g = gridFor(notes, s);
        size_t collide = 0;
        for (size_t i = 1; i < notes.size(); ++i)
            collide += notes[i].voice == notes[i - 1].voice &&
                       (notes[i].start - g.offset) / s == (notes[i - 1].start - g.offset) / s;
        if (collide * 100 <= notes.size() * kGridCollide) return g;
    }
    return gridFor(notes, 1);
}

// --------------------------------------------------------------------------------------------- Import
namespace {

// Steps of the patterns: the frame where each starts, then the frame after the last; and how they are cut into
// patterns: `chunk` steps each (doubled beyond 32 patterns, up to 256), or the parts of a known song (joined
// two, four... at a time beyond 32 patterns, counting from the loop on, up to 256 steps).
struct Timeline {
    std::vector<int> start;
    int chunk = 64;
    std::vector<int> parts;
    int loopFrame = -1;
    bool speeds = false;  // steps of different lengths: Sxx at the start of each pattern and where the length changes
    int steps() const noexcept { return int(start.size()) - 1; }
    int len(int k) const noexcept { return start[size_t(k) + 1] - start[size_t(k)]; }
    int stepOf(int f) const noexcept {
        return int(std::upper_bound(start.begin(), start.end(), f) - start.begin()) - 1;
    }
    // Patterns as ranges of steps [first, end) at the n-th try.
    std::vector<std::pair<int, int>> cut(int n) const {
        std::vector<std::pair<int, int>> out;
        const int total = steps();
        if (parts.empty()) {
            const int L = int(std::min<int64_t>(ymseq::kSteps, int64_t(std::clamp(chunk, 1, ymseq::kSteps)) << n));
            for (int a = 0; a < total; a += L) out.push_back({a, std::min(total, a + L)});
            return out;
        }
        const int group = 1 << std::min(n, 16);
        const int loop = loopFrame >= 0 ? stepOf(loopFrame) : -1;
        int a = 0, joined = 0;
        for (int p = 0, at = 0; p < int(parts.size()); at += parts[size_t(p)], ++p) {
            const int end = at + parts[size_t(p)];
            if (at > a && (at == loop || joined == group || end - a > ymseq::kSteps)) {
                out.push_back({a, at});
                a = at;
                joined = 0;
            }
            ++joined;
        }
        if (a < total) out.push_back({a, total});
        return out;
    }
};

// Patterns, Song, instruments and timing from the notes of the tune placed on the steps (see the file comment).
RegisterImportResult convert(const RegisterTune& t, const std::vector<Frame>& fr, std::vector<Note> notes,
                             const Timeline& tl, double a4, NonParamState& state) {
    RegisterImportResult out;
    auto refuse = [&] {
        // A voice whose tone and noise are off while its volume is up plays samples through the volume register.
        bool samples = t.effectFrames || t.dac;
        for (size_t i = 0; i < fr.size() && !samples; ++i)
            for (const Chan& x : fr[i].ch) samples = samples || (!x.tone && !x.noise && !x.env && x.vol > 0);
        out.error = samples ? "The tune plays only through timer effects or samples, which the import leaves out."
                            : "The tune plays no note.";
        return out;
    };
    if (notes.empty()) return refuse();
    RegisterImport r;
    r.title = t.title;
    if (!t.author.empty()) r.title += r.title.empty() ? t.author : " (" + t.author + ")";
    Counts counts;
    const int total = tl.steps();

    // ---- Steps: the step of each note; a second note in a step goes on in the table of the first ----
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.voice != b.voice ? a.voice < b.voice : a.start < b.start;
    });
    std::vector<Note> placed;
    for (const Note& x : notes) {
        if (x.start < tl.start.front()) {
            ++counts.preroll;
            continue;
        }
        Note y = x;
        y.step = tl.stepOf(x.start);
        y.delay = x.start - tl.start[size_t(y.step)];
        if (!placed.empty() && placed.back().voice == y.voice && placed.back().step == y.step) {
            Note& p = placed.back();
            if (fr[size_t(y.start)].restart && fr[size_t(y.start)].ch[size_t(y.voice)].env) {
                if (p.retrig < 0) p.retrig = y.start;
                else ++counts.retrigLost;
            }
            p.end = y.end;
            continue;
        }
        placed.push_back(y);
    }
    notes = std::move(placed);
    if (notes.empty()) return refuse();

    // ---- Tuning A4: given, or the median offset of the first pitches of the notes from 440 Hz ----
    if (a4 <= 0) {
        std::vector<double> offsets;
        for (const Note& x : notes) {
            const Chan& a = fr[size_t(x.start)].ch[size_t(x.voice)];
            if (!a.tone || a.tp < 16) continue;
            double d = 1200 * std::log2(kChipHz / 16 / a.tp / 440);
            d -= 100 * std::floor(d / 100 + 0.5);
            offsets.push_back(d);
        }
        a4 = ymengine::kA4Hz;
        if (!offsets.empty()) {
            std::nth_element(offsets.begin(), offsets.begin() + std::ptrdiff_t(offsets.size() / 2), offsets.end());
            a4 = std::round(440 * std::pow(2.0, offsets[offsets.size() / 2] / 1200) * 10) / 10;
            a4 = std::clamp(a4, ymengine::kA4MinHz, ymengine::kA4MaxHz);
        }
    }
    r.a4Hz = float(a4);

    // ---- Sounds and instruments ----
    std::vector<Sound> own;
    own.reserve(notes.size());
    for (Note& x : notes) own.push_back(soundOf(fr, x, a4, counts));
    const std::vector<Sound> sounds = groupSounds(notes, own, counts);
    r.instruments = int(sounds.size());
    r.notes = int(notes.size());

    // ---- Steps of the three tracks ----
    std::array<std::vector<Cell>, 3> cells;
    for (auto& v : cells) v.assign(size_t(total), Cell{});
    std::array<bool, 3> fines{};
    for (size_t i = 0; i < notes.size(); ++i) {
        const Note& x = notes[i];
        const Note* next = i + 1 < notes.size() && notes[i + 1].voice == x.voice ? &notes[i + 1] : nullptr;
        std::vector<Cell>& v = cells[size_t(x.voice)];
        const int k0 = x.step;
        if (k0 >= total) continue;
        Cell& a = v[size_t(k0)];
        a.note = x.note;
        a.ins = uint8_t(x.sound + 1);
        const int mode = sounds[size_t(x.sound)].table.front().mix - 1;
        a.mix = uint8_t(mode >= 1 ? mode : 0);
        a.delay = x.delay;
        a.fine = x.fine;
        fines[size_t(x.voice)] = fines[size_t(x.voice)] || x.fine != 0;
        if (x.env) a.shape = x.shape;
        if (x.retrig >= 0) a.retrig = x.retrig - tl.start[size_t(k0)];
        const int last = std::min(total - 1, tl.stepOf(x.end - 1));
        const int rest = x.end - tl.start[size_t(last)];  // frames of the note in its last step
        for (int s = k0; s < last; ++s) v[size_t(s)].tie = true;
        // Held to the next step only when the next note attacks right at its start (which stops this one there);
        // otherwise GAT.
        const bool natural = rest == tl.len(last) && next && next->step == last + 1 && next->delay == 0;
        if (last == k0) {
            if (natural) a.tie = true;
            else a.gat = uint8_t(gatPct(x.end - x.start, tl.len(k0) - x.delay));
        } else {
            v[size_t(last)].tie = true;
            if (!natural) v[size_t(last)].gat = uint8_t(gatPct(rest, tl.len(last)));
        }
    }

    // ---- Digidrums: the hits of the voice that plays the most of them, on the Digidrum track (one channel in
    // Authentic 3ch); each sample in its slot at the rate it plays most, the others a TON (and OCT) away ----
    struct Hit {
        int voice = 0, start = 0, end = 0, sample = 0;
        double hz = 0;
        uint8_t vol = 15;
    };
    std::vector<Hit> hits;
    std::array<int, 3> perVoice{};
    const int nf = int(fr.size());
    for (int c = 0; c < 3; ++c)
        for (int f = 0; f < nf; ++f) {
            const TimerEffect& x = fr[size_t(f)].fx[size_t(c)];
            if (x.gen != Generator::Digidrum ||
                !(x.start || f == 0 || fr[size_t(f - 1)].fx[size_t(c)].gen != Generator::Digidrum))
                continue;
            int e = f + 1;
            while (e < nf && fr[size_t(e)].fx[size_t(c)].gen == Generator::Digidrum && !fr[size_t(e)].fx[size_t(c)].start)
                ++e;
            const Chan& a = fr[size_t(f)].ch[size_t(c)];
            hits.push_back({c, f, e, x.sample & 7, x.hz, uint8_t(a.env ? 15 : a.vol)});
            ++perVoice[size_t(c)];
        }
    struct DrumCell {
        int slot = -1, oct = 0, ton = 0;
        uint8_t vol = 15;
        bool tie = false;
        bool operator==(const DrumCell&) const = default;
    };
    std::vector<DrumCell> drums;
    std::array<double, 8> drumRate{};
    int drumVoice = -1, drumLost = 0, drumFar = 0;
    if (!hits.empty()) {
        drumVoice = int(std::max_element(perVoice.begin(), perVoice.end()) - perVoice.begin());
        std::array<std::unordered_map<long, int>, 8> rates;
        for (const Hit& h : hits)
            if (h.voice == drumVoice && h.hz > 0) ++rates[size_t(h.sample)][std::lround(h.hz)];
        for (size_t k = 0; k < rates.size(); ++k) {
            long best = 0;
            int most = 0;
            for (const auto& [hz, n] : rates[k])
                if (n > most || (n == most && hz < best)) {
                    best = hz;
                    most = n;
                }
            drumRate[k] = std::clamp(best ? double(best) : ymengine::kDigidrumRate, ymengine::kDigidrumRateMin,
                                     ymengine::kDigidrumRateMax);
        }
        drums.assign(size_t(total), DrumCell{});
        for (const Hit& h : hits) {
            if (h.voice != drumVoice) {
                ++drumLost;
                continue;
            }
            if (h.start < tl.start.front()) {
                ++counts.preroll;
                continue;
            }
            const int k0 = tl.stepOf(h.start);
            if (k0 >= total) continue;
            DrumCell& d = drums[size_t(k0)];
            d.slot = h.sample;
            const int semis = h.hz > 0 ? int(roundTo(12 * std::log2(h.hz / drumRate[size_t(h.sample)]))) : 0;
            d.oct = std::clamp(semis / 12, -2, 2);
            d.ton = std::clamp(semis - 12 * d.oct, -12, 12);
            drumFar += d.oct * 12 + d.ton != semis;
            d.vol = h.vol;
            // Held over the steps of the hit (the hit stops at the end of the last one).
            const int last = std::min(total - 1, tl.stepOf(h.end - 1));
            d.tie = last > k0;
            for (int k = k0 + 1; k <= last; ++k) drums[size_t(k)].tie = true;
        }
    }

    // ---- Patterns: the steps cut into patterns, the same ones played again; longer ones beyond 32 ----
    const int speedMax = ymengine::kSpeedMax;
    std::vector<ymseq::Pattern> patterns;
    std::vector<int> order;  // pattern of each range
    std::vector<std::pair<int, int>> ranges;
    std::array<int, 3> octaves{};
    for (int c = 0; c < 3; ++c) octaves[size_t(c)] = state.seq.tracks[size_t(c)].octave;
    for (int attempt = 0;; ++attempt) {
        ranges = tl.cut(attempt);
        patterns.clear();
        order.clear();
        for (const auto& [from, to] : ranges) {
            ymseq::Pattern pat;
            pat.steps = to - from;
            for (int c = 0; c < 3; ++c) {
                const std::vector<Cell>& v = cells[size_t(c)];
                bool any = false;
                for (int s = 0; s < pat.steps && !any; ++s) any = !(v[size_t(from + s)] == Cell{});
                if (!any) continue;
                ymseq::Lane& l = pat.edit(c);
                int fine = INT_MIN;  // Fxx in force on the track: unknown at the start of a pattern
                for (int s = 0; s < pat.steps; ++s) {
                    const Cell& x = v[size_t(from + s)];
                    const size_t k = size_t(s);
                    l.tie[k] = x.tie;
                    l.gat[k] = x.gat;
                    if (x.note < 0) continue;
                    const int octave = x.note / 12 - 1;
                    l.pat[k] = int16_t(x.note % 12);
                    l.oct[k] = int8_t(ymseq::octaveCode(octave, octaves[size_t(c)]));
                    l.ins[k] = x.ins;
                    l.mix[k] = x.mix;
                    if (x.delay > 0) l.setCmd(s, seqCmd(SeqCmd::Delay, x.delay));
                    if (x.shape) l.setCmd(s, seqCmd(SeqCmd::Shape, x.shape));
                    if (x.retrig > x.delay && x.retrig < tl.len(from + s))
                        l.setCmd(s, seqCmd(SeqCmd::Retrig, x.retrig));
                    if ((fine == INT_MIN && (x.fine != 0 || fines[size_t(c)])) || (fine != INT_MIN && x.fine != fine))
                        l.setCmd(s, seqCmd(SeqCmd::Fine, x.fine + ymengine::kFineCmdCenter));
                    fine = x.fine;
                }
            }
            // The Digidrum track.
            if (drumVoice >= 0) {
                bool any = false;
                for (int s = 0; s < pat.steps && !any; ++s) any = !(drums[size_t(from + s)] == DrumCell{});
                if (any) {
                    ymseq::Lane& l = pat.edit(3);
                    for (int s = 0; s < pat.steps; ++s) {
                        const DrumCell& d = drums[size_t(from + s)];
                        const size_t k = size_t(s);
                        l.tie[k] = d.tie;
                        if (d.slot < 0) continue;
                        l.pat[k] = int16_t(d.slot);
                        l.oct[k] = int8_t(d.oct);
                        l.ton[k] = int8_t(d.ton);
                        l.vol[k] = d.vol;
                    }
                }
            }
            // Sxx on track 1: the speed at the start of the pattern, and where it changes.
            if (tl.speeds)
                for (int s = 0; s < pat.steps; ++s)
                    if (s == 0 || tl.len(from + s) != tl.len(from + s - 1))
                        pat.edit(0).setCmd(s, seqCmd(SeqCmd::Speed, std::clamp(tl.len(from + s), 1, speedMax)));
            pat.compact();
            int same = -1;
            for (size_t q = 0; q < patterns.size() && same < 0; ++q)
                if (patterns[q].sameContent(pat)) same = int(q);
            if (same < 0) {
                same = int(patterns.size());
                patterns.push_back(std::move(pat));
            }
            order.push_back(same);
        }
        if (int(patterns.size()) <= ymseq::kPatterns || tl.cut(attempt + 1) == ranges) break;
    }
    // Beyond 32 patterns: the Song stops at the first range that would need a 33rd.
    size_t kept = order.size();
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] >= ymseq::kPatterns) {
            kept = i;
            break;
        }
    if (kept < order.size()) {
        const int lost = tl.start.back() - tl.start[size_t(ranges[kept].first)];
        r.remarks.push_back("Beyond 32 patterns: " + seconds(double(lost) / t.rate) + " left out.");
        order.resize(kept);
        patterns.resize(size_t(ymseq::kPatterns));
    }

    // ---- Song: one entry per range, repeats grouped; it restarts at the loop of the tune ----
    int loopPart = -1;
    if (tl.loopFrame > 0) {
        const int step = std::max(0, tl.stepOf(tl.loopFrame));
        loopPart = 0;
        for (size_t i = 0; i < ranges.size(); ++i)
            if (ranges[i].first <= step) loopPart = int(i);
        loopPart = std::clamp(loopPart, 0, int(order.size()) - 1);
        if (tl.start[size_t(ranges[size_t(loopPart)].first)] != tl.loopFrame)
            r.remarks.push_back("Loop moved to the start of its pattern.");
    }
    ymseq::Song song;
    int restartEntry = -1;
    for (size_t i = 0; i < order.size(); ++i) {
        const bool restart = int(i) == loopPart;
        if (!song.entries.empty() && !restart && song.entries.back().pat == order[i] &&
            song.entries.back().rep < ymseq::kSongRepeatMax) {
            ++song.entries.back().rep;
            continue;
        }
        if (restart) restartEntry = song.size();
        song.entries.push_back({order[i], 1, 0, ymseq::SongKind::Pattern, 0, false});
    }
    if (restartEntry > 0) song.entries[size_t(restartEntry)].restart = true;

    // ---- Project ----
    for (size_t k = 0; k < patterns.size(); ++k) {
        state.seq.patterns[k] = std::move(patterns[k]);
        state.seq.used.set(k);
    }
    state.seq.song = std::move(song);
    for (int c = 0; c < 3; ++c) state.seq.tracks[size_t(c)].length = 0;
    // Digidrum slots: the samples of the tune (4-bit at the YM levels), each at the root note of its row.
    if (drumVoice >= 0) {
        r.digidrums = true;
        state.seq.tracks[3].length = 0;
        state.global.digidrumChannel[0] = uint8_t(drumVoice);
        const int octave = state.seq.tracks[3].octave;
        for (size_t k = 0; k < t.samples.size() && k < size_t(ymengine::kDigidrumPads); ++k) {  // a pad each
            if (t.samples[k].empty()) continue;
            DigidrumSlotState d;
            d.name = "Digi " + std::to_string(k + 1);
            d.play.rateHz = drumRate[k];
            d.play.root = uint8_t(std::clamp((octave + 1) * 12 + int(k), 0, 127));
            d.convert = DigiConvert::YmLevels;
            d.source = t.samples[k];
            state.digidrums[k] = digidrumNibbles(d.source, d.convert);
            d.play.end = uint32_t(state.digidrums[k].size());
            state.digiSlots[k] = std::move(d);
            ++r.samples;
        }
    }
    // Every name kindName gives, FM included.
    static constexpr std::array<const char*, 7> kNames = {"Tone", "Noise", "Mix", "Env", "Buzz", "SID", "FM"};
    std::array<int, kNames.size()> kinds{};
    for (size_t k = 0; k < sounds.size(); ++k) {
        const std::string kind = kindName(sounds[k]);
        const size_t ki = size_t(std::find_if(kNames.begin(), kNames.end(), [&](const char* s) { return kind == s; }) -
                                 kNames.begin());
        state.instruments[k] = instrumentOf(sounds[k], kind + " " + std::to_string(++kinds[ki]));
    }
    const int speed = std::clamp(tl.len(0), 1, speedMax);
    state.global.timing = ymengine::SeqTiming::Ticks;
    state.global.speed = uint8_t(speed);
    state.global.grooveLength = 1;
    state.global.groove.fill(0);
    r.patterns = int(std::min<size_t>(ymseq::kPatterns, size_t(*std::max_element(order.begin(), order.end()) + 1)));
    r.entries = state.seq.song.size();
    r.speed = speed;
    int best = 0;
    for (size_t i = 1; i < kVblRates.size(); ++i)
        if (std::abs(int(kVblRates[i]) - int(t.rate)) < std::abs(int(kVblRates[size_t(best)]) - int(t.rate)))
            best = int(i);
    r.vblRate = best;

    // ---- What the import does not render ----
    auto& n = r.remarks;
    if (kVblRates[size_t(best)] != t.rate)
        n.push_back("Replay rate " + std::to_string(t.rate) + " Hz played at " +
                    std::to_string(kVblRates[size_t(best)]) + " Hz.");
    if (t.effectFrames)
        n.push_back((t.effects.empty() ? "Timer effects (SID, digidrums, sync buzzers) left out: "
                                       : "Timer effects that STYM2149 does not play (pulse width, buzzer FM, "
                                         "sync square) left out: ") +
                    count(int(t.effectFrames), "frame", "frames") + ".");
    if (drumLost)
        n.push_back(std::string("Digidrum hits of channels other than ") + char('A' + drumVoice) +
                    " left out (one Digidrum track): " + std::to_string(drumLost) + ".");
    if (drumFar) n.push_back("Digidrum hits beyond 3 octaves of their slot's rate: " + std::to_string(drumFar) + ".");
    if (counts.sidScaled)
        n.push_back("SID notes below volume 15 (their waveform scaled down, not lowered): " +
                    std::to_string(counts.sidScaled) + ".");
    if (counts.buzzSquare)
        n.push_back("Sync buzzers of more than 2 shapes whose square plays lower: " + std::to_string(counts.buzzSquare) +
                    ".");
    if (counts.buzzEp)
        n.push_back("Sync buzzers whose envelope period does not follow the note: " + std::to_string(counts.buzzEp) +
                    ".");
    if (t.digidrums) n.push_back("Digidrum hits left out: " + std::to_string(t.digidrums) + ".");
    if (t.dac) n.push_back("STE DAC sound left out.");
    if (t.cutFrames) n.push_back("Beyond 10 minutes: " + seconds(double(t.cutFrames) / t.rate) + " left out.");
    if (counts.preroll) n.push_back("Notes before the first step left out: " + std::to_string(counts.preroll) + ".");
    if (counts.merged)
        n.push_back("Notes played with the closest of the 127 instruments: " + std::to_string(counts.merged) + ".");
    if (counts.clamped)
        n.push_back("Notes whose pitch goes beyond their table (12 semitones up or down): " +
                    std::to_string(counts.clamped) + ".");
    if (counts.outOfRange) n.push_back("Notes beyond B0-C9 brought within: " + std::to_string(counts.outOfRange) + ".");
    if (counts.epChanged)
        n.push_back("Notes whose envelope period changes (one per instrument): " + std::to_string(counts.epChanged) +
                    ".");
    if (counts.longer)
        n.push_back("Notes longer than their table (64 frames): " + std::to_string(counts.longer) + ".");
    if (counts.retrigLost)
        n.push_back("Envelope restarts left out (one per step): " + std::to_string(counts.retrigLost) + ".");
    out.import = std::move(r);
    return out;
}

}  // namespace

RegisterImportResult importRegisters(const RegisterTune& t, const RegisterGrid& grid, NonParamState& state) {
    const std::vector<Frame> fr = decode(t);
    const int S = std::clamp(grid.framesPerStep, 1, kGridMaxFrames);
    const int offset = std::clamp(grid.offset, -(S - 1), S - 1);
    Timeline tl;
    const int total = std::max(1, (int(fr.size()) - offset + S - 1) / S);
    tl.start.resize(size_t(total) + 1);
    for (int k = 0; k <= total; ++k) tl.start[size_t(k)] = offset + k * S;
    tl.chunk = grid.stepsPerPattern;
    tl.loopFrame = t.loopFrame;
    return convert(t, fr, allNotes(fr, {S, offset}), tl, 0, state);
}

RegisterImportResult importRegisters(const RegisterTune& t, const RegisterLayout& layout, NonParamState& state) {
    const std::vector<Frame> fr = decode(t);
    // The steps: a row longer than the longest speed (Sxx) becomes several steps of nearly equal ticks, the first
    // one holding the row; a part that then outgrows a pattern goes on in another one.
    Timeline tl;
    tl.start.assign(1, 0);
    std::vector<int> parts = layout.partSteps;
    int sum = 0;
    for (const int p : parts) sum += std::max(0, p);
    if (sum != int(layout.stepFrames.size())) parts.assign(1, int(layout.stepFrames.size()));
    int row = 0, loopStep = -1;
    for (const int p : parts) {
        int in = 0;  // steps of the current part
        for (int k = 0; k < p; ++k, ++row) {
            const int n = std::max(1, layout.stepFrames[size_t(row)]);
            const int pieces = (n + ymengine::kSpeedMax - 1) / ymengine::kSpeedMax;
            if (in > 0 && in + pieces > ymseq::kSteps) {
                tl.parts.push_back(in);
                in = 0;
            }
            if (row == layout.loopStep) loopStep = tl.steps();
            for (int q = 0; q < pieces; ++q) tl.start.push_back(tl.start.back() + n * (q + 1) / pieces - n * q / pieces);
            in += pieces;
        }
        if (in > 0) tl.parts.push_back(in);
    }
    if (tl.steps() == 0) tl.start.push_back(int(fr.size()));
    for (int k = 1; k < tl.steps() && !tl.speeds; ++k) tl.speeds = tl.len(k) != tl.len(0);
    tl.loopFrame = loopStep >= 0 ? tl.start[size_t(loopStep)] : -1;
    // Notes: from each row that starts one to the next row of its voice, ending where the voice falls silent before.
    std::vector<RegisterLayout::Row> rows = layout.rows;
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.voice != b.voice ? a.voice < b.voice : a.frame < b.frame;
    });
    std::vector<Note> notes;
    const int n = int(fr.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        const RegisterLayout::Row& w = rows[i];
        if (w.note < 0 || w.voice < 0 || w.voice > 2 || w.frame < 0 || w.frame >= n) continue;
        int end = i + 1 < rows.size() && rows[i + 1].voice == w.voice ? std::min(n, rows[i + 1].frame) : n;
        while (end > w.frame && !audible(fr[size_t(end - 1)], w.voice)) --end;
        if (end <= w.frame) continue;
        Note x;
        x.voice = w.voice;
        x.start = w.frame;
        x.end = end;
        x.known = w.note;
        std::vector<Note> split = splitLong(fr, {x}, w.voice, Steps{0, 0, &tl.start});
        notes.insert(notes.end(), split.begin(), split.end());
    }
    return convert(t, fr, std::move(notes), tl, layout.a4Hz, state);
}

int attenuatedVolume(int vol, double level) noexcept {
    vol = std::clamp(vol, 0, 15);
    if (level >= 1) return vol;
    if (level <= 0 || vol == 0) return 0;
    const auto& t = ymcore::tables::kMixTable;  // one channel alone, at the odd index of each 4-bit volume
    const double want = std::log(double(t[2 * vol + 1]) * level);
    int best = 1;
    double err = HUGE_VAL;
    for (int k = 1; k <= vol; ++k)
        if (const double e = std::fabs(std::log(double(t[2 * k + 1])) - want); e < err) {
            err = e;
            best = k;
        }
    return best;
}

int volForLevel(int level4) noexcept {
    level4 = std::clamp(level4, 0, 15);
    if (level4 == 0) return 0;
    const auto& t = ymcore::tables::kMixTable;
    const double want = std::log(double(t[2 * level4 + 1]) / double(t[31]));
    int best = 1;
    double err = HUGE_VAL;
    for (int v = 1; v <= 15; ++v)
        if (const double e = std::fabs(std::log(v / 15.0) - want); e < err) {
            err = e;
            best = v;
        }
    return best;
}

std::vector<std::string> registerImportMessage(const RegisterImport& r, const std::string& fileName, bool sndh) {
    std::vector<std::string> lines = {sndh ? "SNDH imported" : "YM imported", "", fileName};
    if (!r.title.empty()) lines.push_back(r.title);
    lines.push_back(std::to_string(3 + r.digidrums) + " tracks, " + count(r.patterns, "pattern", "patterns") + ", " +
                    count(r.instruments, "instrument", "instruments") + ", " +
                    count(r.entries, "Song entry", "Song entries") + ".");
    if (r.digidrums) lines.push_back("Digidrum samples on track 4: " + std::to_string(r.samples) + ".");
    char a4[16];
    std::snprintf(a4, sizeof a4, "%.1f", double(r.a4Hz));
    lines.push_back("Ticks: speed " + std::to_string(r.speed) + " at " + std::to_string(kVblRates[size_t(r.vblRate)]) +
                    " Hz, Authentic 3ch, A4 " + a4 + " Hz.");
    if (!r.remarks.empty()) {
        lines.emplace_back("");
        lines.emplace_back("Not as in the tune:");
        for (const std::string& n : r.remarks) lines.push_back("• " + n);
    }
    return lines;
}

void registerImportValues(const RegisterImport& r, ParamValues& values) {
    auto set = [&values](int idx, float v) { values[size_t(idx)] = clampToSpec(paramSpec(idx), v); };
    if (values[size_t(paramIndex(Global::VoiceMode))] != 1.0f) {
        set(paramIndex(Global::VoiceMode), 1.0f);
        set(paramIndex(Global::Mpe), 0.0f);
    }
    for (int t = 0; t < kTracks; ++t) {
        const bool drums = t == 3 && r.digidrums;
        set(paramIndex(t, Track::On), t < 3 || drums ? 1.0f : 0.0f);
        if (t >= 3 && !drums) continue;
        set(paramIndex(t, Track::Generator), float(int(drums ? ymengine::Generator::Digidrum : ymengine::Generator::Chip)));
        set(paramIndex(t, Track::Level), 100.0f);
    }
    set(paramIndex(Global::VblRate), float(r.vblRate));
    set(paramIndex(Global::A4Hz), r.a4Hz);
    if (r.tempo > 0) set(paramIndex(Global::FreeTempo), r.tempo);
}

}  // namespace stym::plugin
