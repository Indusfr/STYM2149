// STYM2149 — plugincore: export of the Song as a maxYMiser song (.MYS) and voiceset (.MYV)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/MaxYmExport.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <map>
#include <utility>

#include "plugincore/Mapping.h"
#include "plugincore/SongCapture.h"
#include "ymio/MaxYmPlayer.h"

namespace stym::plugin {

namespace {

using ymengine::Generator;
using ymengine::RegisterFrame;

constexpr int kSeqMax = ymio::kMyvSequenceLength;
constexpr uint8_t kSquare = 4, kBuzzer = 2, kTimer = 1;  // generator bits of the instrument's flags
constexpr double kDigidrumClock = 614'400;               // MFP clock divided by 4: a digidrum plays 614400 / data Hz
constexpr double kTwoStepClock = 2'457'600.0 * 40 / 393;  // a timer of two steps runs at kTwoStepClock / period Hz
constexpr double kApproxCents = 10;  // a generator played further off counts as approximate
constexpr double kShareCents = 5;    // the timer shares the square's pitch when its rate comes within this
constexpr uint8_t kNoteOff = ymio::kMysNoteOffRow;
constexpr uint8_t kSpeedFx = 'S', kBreakFx = 'B';
constexpr int kFmSteps = 8;  // the steps of maxYMiser's square FM

double cents(double a, double b) { return a > 0 && b > 0 ? std::abs(1200 * std::log2(a / b)) : HUGE_VAL; }

// ---- A channel at a frame, as the routine has to play it ----

struct Want {
    bool heard = false;
    uint8_t level = 0;      // volume 1-15 that the volume column scales (square, noise, SID)
    bool full = false;      // the volume only switches the channel on (envelope, digidrum)
    uint16_t mix = 0;       // mixer word: square $100, noise $1000, buzzer shape × $10, timer effect
    bool noiseOn = false;
    uint8_t noise = 0;
    int square = 0, buzzer = 0;  // periods played (0: none)
    double timerHz = 0;          // SID, sync buzzer or FM: rate of its interrupts
    uint8_t effect = 0;          // timer effect (mixer nibble): 5 SID, $B sync buzzer, $D digidrum, $F square FM
    uint8_t shape = 0;           // sync buzzer: its shape
    std::array<uint8_t, kFmSteps> fm{};  // square FM: the semitones above the note of its steps
    uint8_t fmSteps = 0;
    bool fmOff = false;          // FM Voice whose steps play approximately
    uint8_t digidrum = 0, digiRate = 0;  // digidrum: slot of the project + 1 (1-127) and MFP data (0: 256)
    bool restart = false;        // the envelope restarts
};

// FM Voice as maxYMiser's square FM: the semitones of its steps, those of the Semitones scale as they are, a
// multiple at its nearest semitones (x0.5, below the note, plays the note); beyond 8 steps, the period of the wave
// when it repeats within 8, otherwise its first 8. Returns false when a step plays approximately.
bool fmStepsOf(const RegisterFrame::Effect& e, Want& w) {
    const int n = std::clamp<int>(e.value, 1, int(e.wave.size()));
    std::array<int, 16> t{};
    bool exact = true;
    for (int i = 0; i < n; ++i) {
        if (e.semis) {
            t[size_t(i)] = std::min<int>(e.wave[size_t(i)], ymengine::kFmSemitoneMax);
            continue;
        }
        const int m = e.wave[size_t(i)];
        t[size_t(i)] = std::max(0, ymengine::fmSemitonesOf(m));
        exact = exact && ymengine::fmSemitonesOf(m) >= 0 && std::abs(ymengine::fmSemitoneCents(m)) <= kApproxCents;
    }
    int len = n;
    for (int p = 1; p <= kFmSteps && len > kFmSteps; ++p) {
        bool repeats = n % p == 0;
        for (int i = p; i < n && repeats; ++i) repeats = t[size_t(i)] == t[size_t(i - p)];
        if (repeats) len = p;
    }
    if (len > kFmSteps) {
        len = kFmSteps;
        exact = false;
    }
    w.fmSteps = uint8_t(len);
    for (int i = 0; i < len; ++i) w.fm[size_t(i)] = uint8_t(t[size_t(i)]);
    return exact;
}

// Shapes 0-3 and 4-7 play as 9 and 15; nibble 1 would mean the instrument's shape.
uint8_t shapeNibble(int shape) {
    shape &= 15;
    return uint8_t(shape < 4 ? 9 : shape < 8 ? 15 : shape);
}

Want wantOf(const RegisterFrame& f, int c) {
    Want w;
    const auto& r = f.reg;
    const RegisterFrame::Effect& e = f.fx[size_t(c)];
    const bool tone = !(r[7] >> c & 1), noise = !(r[7] >> (3 + c) & 1);
    const uint8_t amp = r[size_t(8 + c)];
    const int tp = std::max(1, r[size_t(2 * c)] | (r[size_t(2 * c + 1)] & 15) << 8);
    const int ep = std::max(1, r[11] | r[12] << 8);
    const auto mix = uint16_t((tone ? 0x100 : 0) | (noise ? 0x1000 : 0));
    if (e.on && e.gen == Generator::Digidrum) {
        const double hz = e.timer.hz();
        w.heard = hz > 0;
        w.full = true;
        w.effect = 0xD;
        w.mix = 0xD;
        w.digidrum = uint8_t(std::min<int>(e.value, ymengine::kDigidrumSlotsMax - 1) + 1);
        w.digiRate = uint8_t(std::clamp<long>(std::lround(kDigidrumClock / std::max(hz, 1.0)), 1, 256) & 0xFF);
        return w;
    }
    if (e.on && e.gen == Generator::SyncBuzzer) {
        w.heard = true;
        w.full = true;
        w.effect = 0xB;
        w.shape = uint8_t(e.value & 15);
        w.mix = uint16_t(mix | shapeNibble(w.shape) << 4 | 0xB);
        w.timerHz = e.timer.hz();
        w.buzzer = ep;
    } else if (e.on && e.gen == Generator::Sid && !(amp & 16)) {
        // Any mixer: the SID steps the volume (the level itself when the square and noise are off).
        w.level = uint8_t(e.value & 15);
        w.heard = w.level > 0;
        w.effect = 5;
        w.mix = uint16_t(mix | 5);
        w.timerHz = e.timer.hz();
    } else if (amp & 16) {
        w.heard = true;
        w.full = true;
        w.mix = uint16_t(mix | shapeNibble(r[13]) << 4);
        w.buzzer = ep;
        w.restart = f.r13;
    } else {
        w.level = uint8_t(amp & 15);
        w.heard = w.level > 0 && (tone || noise);
        w.mix = mix;
    }
    if (e.on && e.gen == Generator::Fm && w.heard) {
        // Square FM: the timer steps the square's period, two interrupts per period of its note.
        w.effect = 0xF;
        w.mix = uint16_t(w.mix | 0xF);
        w.timerHz = e.timer.hz();
        w.fmOff = !fmStepsOf(e, w);
    }
    w.square = w.heard && tone ? tp : 0;
    w.noiseOn = w.heard && noise;
    w.noise = uint8_t(r[6] & 31);
    return w;
}

// ---- Sequences ----

struct Seq {
    std::vector<uint16_t> data;  // empty: no sequence (number 0)
    uint8_t repeat = 0;          // step the end goes back to (the last one: the sequence stops there)
    bool operator==(const Seq&) const = default;
    // Value at tick t, as the routine steps it.
    int at(int t) const {
        if (data.empty()) return 0;
        const int len = int(data.size());
        if (t < len) return data[size_t(t)];
        return data[size_t(repeat + (t - len) % (len - repeat))];
    }
};

// The shortest sequence (63 steps at most) that plays `v`, one value per tick, holding or looping its end.
std::optional<Seq> fit(const std::vector<uint16_t>& v) {
    const int n = int(v.size());
    if (n == 0) return Seq{{0}, 0};
    int bestLen = INT_MAX, bestRep = 0;
    for (int p = 1; p <= std::min(n, kSeqMax); ++p) {
        // The end repeats every p ticks from tick s on.
        int last = -1;
        for (int i = n - 1; i >= p; --i)
            if (v[size_t(i)] != v[size_t(i - p)]) {
                last = i;
                break;
            }
        const int s = last < 0 ? 0 : last - p + 1;
        if (s + p <= kSeqMax && s + p < bestLen) {
            bestLen = s + p;
            bestRep = s;
        }
    }
    if (bestLen == INT_MAX) return std::nullopt;
    return Seq{std::vector<uint16_t>(v.begin(), v.begin() + bestLen), uint8_t(bestRep)};
}

// The timer sequence of a square FM: its steps' semitones.
Seq fmSeq(const Want& w) {
    return Seq{std::vector<uint16_t>(w.fm.begin(), w.fm.begin() + std::max<int>(1, w.fmSteps)), 0};
}

// ---- Sounds: the instrument of a row ----

enum : size_t { kVol, kArp, kVib, kMix, kNoise, kFix, kWave, kSeqs };  // kWave: timer waveform (read whole)

struct Sound {
    std::array<Seq, kSeqs> seq{};
    uint8_t arp = 0, vib = 0, fix = 0;  // generators that follow them
    bool sync = false;                  // start sync: the envelope restarts with the row
    uint8_t digidrum = 0, digiRate = 0;
    // Not part of the sound: how much it plays, where first, from which track, the most ticks a row plays it.
    int weight = 0, first = INT_MAX, track = -1, span = 0;

    std::string key() const {
        std::string k{char(arp), char(vib), char(fix), char(sync), char(digidrum), char(digiRate)};
        for (const Seq& s : seq) {
            k += char(s.data.size());
            k += char(s.repeat);
            for (uint16_t d : s.data) {
                k += char(d >> 8);
                k += char(d & 0xFF);
            }
        }
        return k;
    }
};

struct Part {
    Sound sound;
    int column = 0;  // volume column: what the row takes off the volume
    int approx = 0, restarts = 0, unplayable = 0, mixed = 0;
};

// Timer period (tone units at 2 MHz) whose two-step interrupts come closest to `hz`, within the MFP's reach (the
// nearest end of it beyond); 0: no rate.
int timerPeriod(double hz) {
    if (hz <= 0) return 0;
    const long p0 = std::clamp(std::lround(kTwoStepClock / hz), 1L, 0xFFFEL);
    int best = 0;
    double err = HUGE_VAL;
    for (long p = std::max(1L, p0 - 8); p <= std::min(0xFFFEL, p0 + 8); ++p) {
        const double h = ymio::maxYmTimerHz(uint16_t(p), 2);
        if (h > 0 && cents(h, hz) < err) {
            err = cents(h, hz);
            best = int(p);
        }
    }
    // Beyond the reach: the nearest period that plays.
    for (long p = p0, d = p0 > 1000 ? -1 : 1; !best && p >= 1 && p <= 0xFFFE; p += d)
        if (ymio::maxYmTimerHz(uint16_t(p), 2) > 0) best = int(p);
    return best;
}

// Arpeggio (semitones from note n) and vibrato (period units taken off) that bring a table to `period`.
std::pair<int, int> squareRel(int n, int period) {
    const int a = int(std::lround(12 * std::log2(double(ymio::maxYmSquarePeriod(n)) / period)));
    const int i = std::clamp(n + a, 0, 127);
    return {i - n, std::clamp(int(ymio::maxYmSquarePeriod(i)) - period, -32768, 32767)};
}
std::pair<int, int> buzzerRel(int n, int period) {
    const int a = int(std::lround(12 * std::log2(double(std::max<uint16_t>(1, ymio::maxYmBuzzerPeriod(n))) / period)));
    const int i = std::clamp(n + a, 0, 127);
    // The routine takes the vibrato off the buzzer at 1/16 (rounded): 16 × d takes d off.
    return {i - n, std::clamp(16 * (int(ymio::maxYmBuzzerPeriod(i)) - period), -32768, 32752)};
}
// Period that a generator plays from note n with the arpeggio and vibrato (as the routine computes it).
int squareOf(int n, int a, int v) { return int(ymio::maxYmSquarePeriod(std::clamp(n + a, 0, 127))) - v; }
int buzzerOf(int n, int a, int v) {
    const int scaled = ((int16_t(v) >> 3) + 1) >> 1;
    return int(ymio::maxYmBuzzerPeriod(std::clamp(n + a, 0, 127))) - scaled;
}

// The instrument that plays frames [f0, f1) of a channel from note n; nullopt if a sequence would need more than 63
// steps.
std::optional<Part> soundOf(const std::vector<Want>& w, int f0, int f1, int n) {
    const int len = f1 - f0;
    Part p;
    Sound& s = p.sound;
    auto at = [&](int i) -> const Want& { return w[size_t(f0 + i)]; };
    // Volume: the loudest scaled level brought to 15, the column taking the difference off.
    int loud = 0;
    bool sq = false, tm = false, bz = false;
    for (int i = 0; i < len; ++i) {
        const Want& x = at(i);
        if (!x.heard) continue;
        if (!x.full) loud = std::max<int>(loud, x.level);
        sq = sq || x.square > 0;
        tm = tm || x.timerHz > 0;
        bz = bz || x.buzzer > 0;
    }
    const int k = loud > 0 ? 15 - loud : 0;
    p.column = k;
    // Timer periods: those whose interrupts come closest to the rate played.
    std::vector<int> tper(static_cast<size_t>(len), 0);
    for (int i = 0; i < len; ++i)
        if (at(i).heard && at(i).timerHz > 0) {
            tper[size_t(i)] = timerPeriod(at(i).timerHz);
            p.unplayable += cents(ymio::maxYmTimerHz(uint16_t(tper[size_t(i)]), 2), at(i).timerHz) > kApproxCents;
        }
    // Arpeggio and vibrato follow the square, otherwise the timer, otherwise the buzzer; the timer shares them when
    // the square's period plays its rate.
    uint8_t rel = sq ? kSquare : tm ? kTimer : bz ? kBuzzer : 0;
    if (rel == kSquare && tm) {
        bool same = true;
        for (int i = 0; i < len && same; ++i) {
            const Want& x = at(i);
            if (x.heard && x.square && x.timerHz > 0) same = cents(ymio::maxYmTimerHz(uint16_t(x.square), 2), x.timerHz) <= kShareCents;
        }
        if (same) rel |= kTimer;
    }
    // Fixed frequency: the first generator left; a third one plays approximately.
    uint8_t fixed = 0, other = 0;
    for (uint8_t b : {kSquare, kTimer, kBuzzer}) {
        const bool need = b == kSquare ? sq : b == kTimer ? tm : bz;
        if (!need || (rel & b)) continue;
        if (!fixed) fixed = b;
        else other = b;
    }
    auto target = [&](uint8_t b, int i) {
        const Want& x = at(i);
        if (!x.heard) return 0;
        return b == kSquare ? x.square : b == kTimer ? tper[size_t(i)] : x.buzzer;
    };
    // The values of each tick; a tick without a value holds the one before (the first ones take the first value).
    auto hold = [](std::vector<int>& v, const std::vector<bool>& set) {
        int first = -1;
        for (size_t i = 0; i < v.size() && first < 0; ++i)
            if (set[i]) first = int(i);
        if (first < 0) return;
        for (size_t i = 0; i < v.size(); ++i)
            if (!set[i]) v[i] = int(i) < first ? v[size_t(first)] : v[i - 1];
    };
    std::vector<int> arp(static_cast<size_t>(len), 0), vib(static_cast<size_t>(len), 0), fix(static_cast<size_t>(len), 0);
    std::vector<bool> relSet(static_cast<size_t>(len), false), fixSet(static_cast<size_t>(len), false);
    for (int i = 0; i < len; ++i) {
        for (uint8_t b : {kSquare, kTimer, kBuzzer}) {
            if (!(rel & b) || !target(b, i)) continue;
            const auto [a, v] = b == kBuzzer ? buzzerRel(n, target(b, i)) : squareRel(n, target(b, i));
            arp[size_t(i)] = a;
            vib[size_t(i)] = v;
            relSet[size_t(i)] = true;
            break;
        }
        if (fixed && target(fixed, i)) {
            fix[size_t(i)] = target(fixed, i);
            fixSet[size_t(i)] = true;
        }
    }
    hold(arp, relSet);
    hold(vib, relSet);
    hold(fix, fixSet);
    // The third generator: with the arpeggio and vibrato, or on the note alone, whichever plays closer.
    if (other) {
        auto played = [&](int i, bool withRel) {
            const int a = withRel ? arp[size_t(i)] : 0, v = withRel ? vib[size_t(i)] : 0;
            return other == kBuzzer ? buzzerOf(n, a, v) : squareOf(n, a, v);
        };
        auto off = [&](int i, bool withRel) {
            const int want = target(other, i);
            if (!want) return 0.0;
            const int got = played(i, withRel);
            if (other == kTimer) return cents(ymio::maxYmTimerHz(uint16_t(std::max(1, got)), 2), at(i).timerHz);
            return cents(double(want), double(std::max(1, got)));
        };
        double errRel = 0, errNote = 0;
        for (int i = 0; i < len; ++i) {
            errRel += std::min(off(i, true), 2400.0);
            errNote += std::min(off(i, false), 2400.0);
        }
        const bool withRel = errRel <= errNote;
        if (withRel) rel |= other;
        for (int i = 0; i < len; ++i) p.approx += off(i, withRel) > kApproxCents;
    }
    // Tables.
    std::vector<uint16_t> vol(static_cast<size_t>(len)), mix(static_cast<size_t>(len)), noise(static_cast<size_t>(len)), a16(static_cast<size_t>(len)), v16(static_cast<size_t>(len)),
        f16(static_cast<size_t>(len));
    std::vector<int> nz(static_cast<size_t>(len), 0);
    std::vector<bool> nzSet(static_cast<size_t>(len), false);
    uint8_t effect = 0;
    for (int i = 0; i < len; ++i) {
        const Want& x = at(i);
        if (x.heard) {
            // A silent tick has no mixer, so that the routine reads the mixer again when the sound comes back.
            vol[size_t(i)] = uint16_t(x.full ? 15 : x.level + k);
            mix[size_t(i)] = x.mix;
        }
        if (x.noiseOn) {
            nz[size_t(i)] = x.noise;
            nzSet[size_t(i)] = true;
        }
        if (x.heard && x.effect) {
            if (!effect) {
                effect = x.effect;
                if (x.effect == 0xB) s.seq[kWave] = Seq{{uint16_t(x.shape)}, 0};
                if (x.effect == 5) s.seq[kWave] = Seq{{0, 15}, 0};  // the SID played as its square
                if (x.effect == 0xF) s.seq[kWave] = fmSeq(x);
                if (x.effect == 0xD) {
                    s.digidrum = x.digidrum;
                    s.digiRate = x.digiRate;
                }
            } else if (x.effect != effect || (effect == 0xB && x.shape != s.seq[kWave].data[0]) ||
                       (effect == 0xF && !(fmSeq(x) == s.seq[kWave])) ||
                       (effect == 0xD && (x.digidrum != s.digidrum || x.digiRate != s.digiRate))) {
                ++p.mixed;
            }
        }
        if (i > 0 && x.restart && x.heard) ++p.restarts;
    }
    s.sync = at(0).heard && at(0).restart;
    hold(nz, nzSet);
    bool anyArp = false, anyVib = false;
    for (int i = 0; i < len; ++i) {
        noise[size_t(i)] = uint16_t(nz[size_t(i)]);
        a16[size_t(i)] = uint16_t(int16_t(arp[size_t(i)]));
        v16[size_t(i)] = uint16_t(int16_t(vib[size_t(i)]));
        f16[size_t(i)] = uint16_t(fix[size_t(i)]);
        anyArp = anyArp || arp[size_t(i)] != 0;
        anyVib = anyVib || vib[size_t(i)] != 0;
    }
    auto put = [&](size_t q, const std::vector<uint16_t>& v) {
        const std::optional<Seq> f = fit(v);
        if (!f) return false;
        s.seq[q] = *f;
        return true;
    };
    if (!put(kVol, vol) || !put(kMix, mix)) return std::nullopt;
    if (std::any_of(noise.begin(), noise.end(), [](uint16_t x) { return x != 0; }) && !put(kNoise, noise))
        return std::nullopt;
    if (anyArp) {
        if (!put(kArp, a16)) return std::nullopt;
        s.arp = rel;
    }
    if (anyVib) {
        if (!put(kVib, v16)) return std::nullopt;
        s.vib = rel;
    }
    if (fixed) {
        if (!put(kFix, f16)) return std::nullopt;
        s.fix = fixed;
    }
    s.weight = len;
    s.first = f0;
    s.span = len;
    return p;
}

// Pitch of a generator at tick t: a fixed period, or semitones and period units from the note.
struct Pitch {
    bool fixed = false;
    int arp = 0, vib = 0, fix = 0;
};
Pitch pitchAt(const Sound& s, uint8_t gen, int t) {
    if (s.fix & gen) return {true, 0, 0, s.seq[kFix].at(t)};
    return {false, (s.arp & gen) ? int16_t(s.seq[kArp].at(t)) : 0, (s.vib & gen) ? int16_t(s.seq[kVib].at(t)) : 0, 0};
}

// Cost of playing sound a with b's instrument, per tick over the ticks that a plays (64 at most): volume steps, mixer
// changes, noise period steps where a has noise, pitch of the generators that a plays (a semitone counts as 6 volume
// steps); another digidrum or timer waveform does not merge. 0: b plays a as it is (a sound cut shorter by the next
// row is one that plays longer).
double distance(const Sound& a, const Sound& b) {
    if (a.digidrum != b.digidrum || a.digiRate != b.digiRate || !(a.seq[kWave] == b.seq[kWave])) return 1e9;
    const int span = std::clamp(a.span, 1, 64);
    double d = a.sync != b.sync ? 8 : 0;
    for (int t = 0; t < span; ++t) {
        d += std::abs(a.seq[kVol].at(t) - b.seq[kVol].at(t));
        const int ma = a.seq[kMix].at(t), mb = b.seq[kMix].at(t);
        d += 8 * ((ma & 0x100) != (mb & 0x100)) + 8 * ((ma & 0x1000) != (mb & 0x1000)) + 8 * ((ma & 0xFF) != (mb & 0xFF));
        if (!a.seq[kVol].at(t)) continue;
        if (ma & 0x1000) d += std::abs(a.seq[kNoise].at(t) - b.seq[kNoise].at(t));
        const int fx = ma & 0xF;
        for (const uint8_t g : {kSquare, kTimer, kBuzzer}) {
            const bool timer = fx == 5 || fx == 0xB || fx == 0xF;
            if (!(g == kSquare ? (ma & 0x100) != 0 : g == kTimer ? timer : (ma & 0xF0) != 0)) continue;
            const Pitch x = pitchAt(a, g, t), y = pitchAt(b, g, t);
            if (x.fixed != y.fixed) d += 40;
            else if (x.fixed) d += std::min(40.0, std::abs(x.fix - y.fix) / 2.0);
            else d += 6 * std::abs(x.arp - y.arp) + std::abs(x.vib - y.vib) / 2.0;
        }
    }
    return d;
}

// A sound that another plays as it is takes that one's instrument; beyond 32 sounds, the least played merges into
// the closest, until 32 are left. Returns the sound that plays each; `merged`: the frames played differently.
std::vector<int> mergeSounds(std::vector<Sound>& sounds, int& merged) {
    const size_t n = sounds.size();
    std::vector<int> into(n);
    for (size_t i = 0; i < n; ++i) into[i] = int(i);
    if (n < 2) return into;
    std::vector<std::vector<double>> dist(n, std::vector<double>(n, 0));
    for (size_t a = 0; a < n; ++a)
        for (size_t b = 0; b < n; ++b)
            if (a != b) dist[a][b] = distance(sounds[a], sounds[b]);
    std::vector<int> weight(n);
    for (size_t i = 0; i < n; ++i) weight[i] = sounds[i].weight;
    std::vector<bool> alive(n, true), lossy(n, false);
    auto join = [&](size_t a, size_t b) {
        alive[a] = false;
        into[a] = int(b);
        lossy[a] = dist[a][b] > 0;
        sounds[b].weight += sounds[a].weight;
        sounds[b].first = std::min(sounds[b].first, sounds[a].first);
        if (sounds[a].span > sounds[b].span) {
            // b now plays longer rows: what it costs to play them is measured again.
            sounds[b].span = sounds[a].span;
            for (size_t j = 0; j < n; ++j)
                if (j != b) dist[b][j] = distance(sounds[b], sounds[j]);
        }
    };
    // Shortest first: a row that another sound's longer row starts the same way takes its instrument.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return sounds[x].span < sounds[y].span; });
    for (size_t a : order)
        for (size_t b = 0; b < n; ++b)
            if (b != a && alive[b] && dist[a][b] == 0) {
                join(a, b);
                break;
            }
    size_t count = size_t(std::count(alive.begin(), alive.end(), true));
    std::vector<size_t> closest(n, n);
    auto nearest = [&](size_t i) {
        closest[i] = n;
        for (size_t j = 0; j < n; ++j)
            if (j != i && alive[j] && (closest[i] == n || dist[i][j] < dist[i][closest[i]])) closest[i] = j;
    };
    if (count > size_t(ymio::kMyvInstruments))
        for (size_t i = 0; i < n; ++i)
            if (alive[i]) nearest(i);
    for (; count > size_t(ymio::kMyvInstruments); --count) {
        size_t a = n;
        double best = HUGE_VAL;
        for (size_t i = 0; i < n; ++i)
            if (alive[i] && closest[i] < n && double(sounds[i].weight) * dist[i][closest[i]] < best) {
                best = double(sounds[i].weight) * dist[i][closest[i]];
                a = i;
            }
        if (a == n) break;
        const size_t b = closest[a];
        const int spanB = sounds[b].span;
        join(a, b);
        for (size_t i = 0; i < n; ++i)
            if (alive[i] && (closest[i] == a || (i == b && sounds[b].span != spanB))) nearest(i);
    }
    for (size_t i = 0; i < n; ++i) {
        int s = int(i);
        bool off = false;
        while (into[size_t(s)] != s) {
            off = off || lossy[size_t(s)];
            s = into[size_t(s)];
        }
        merged += off ? weight[i] : 0;
        into[i] = s;
    }
    return into;
}

// The digidrum sample of a slot as maxYMiser stores it (signed bytes), between its Start and End.
std::vector<uint8_t> sampleOf(const NonParamState& x, int slot) {
    const DigidrumSlotState& d = x.digiSlots[size_t(slot)];
    std::vector<int8_t> src = d.source.vec();
    if (src.empty())
        for (uint8_t v : x.digidrums[size_t(slot)])
            src.push_back(int8_t(std::lround((std::min<int>(v, 15) - 7.5) / 7.5 * 127)));
    const size_t end = d.play.end ? std::min<size_t>(d.play.end, src.size()) : src.size();
    const size_t start = std::min<size_t>(d.play.start, end);
    std::vector<uint8_t> out;
    for (size_t i = start; i < end && out.size() < 0xFFFF; ++i) out.push_back(uint8_t(src[i]));
    return out;
}

struct Cell {
    uint8_t note = 0;  // 0 nothing, 1 off, 2+ note
    int sound = -1;
    uint8_t column = 0;
};

std::string count(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

MaxYmExportResult convert(const SongCapture& cap, const NonParamState& x) {
    MaxYmExportResult res;
    const int frames = int(cap.frames.size());
    const int rows = int(cap.rowFrame.size());
    if (frames == 0 || rows == 0) {
        res.error = "The Song plays nothing.";
        return res;
    }
    auto rowEnd = [&](int r) { return r + 1 < rows ? cap.rowFrame[size_t(r + 1)] : frames; };
    auto rowOf = [&](int f) {
        const auto it = std::upper_bound(cap.rowFrame.begin(), cap.rowFrame.end(), f);
        return std::max(0, int(it - cap.rowFrame.begin()) - 1);
    };
    std::array<std::vector<Cell>, 3> cells;
    std::vector<Sound> sounds;
    std::map<std::string, int> known;
    int notes = 0, approx = 0, restarts = 0, unplayable = 0, mixed = 0, longer = 0, fmOff = 0;
    for (int c = 0; c < 3; ++c) {
        std::vector<Want> w(static_cast<size_t>(frames));
        for (int f = 0; f < frames; ++f) {
            w[size_t(f)] = wantOf(cap.frames[size_t(f)], c);
            fmOff += w[size_t(f)].fmOff;
        }
        // The rows that start a note: an attack of the sequencer or a digidrum that starts.
        std::vector<SongAttack> events = cap.attacks[size_t(c)];
        for (int f = 0; f < frames; ++f) {
            const RegisterFrame::Effect& e = cap.frames[size_t(f)].fx[size_t(c)];
            if (e.on && e.restart && e.gen == Generator::Digidrum) events.push_back({f, -1, -1});
        }
        std::stable_sort(events.begin(), events.end(),
                         [](const SongAttack& a, const SongAttack& b) { return a.frame < b.frame; });
        struct Start {
            int row, note, track;
        };
        std::vector<Start> starts;
        for (const SongAttack& e : events) {
            if (e.frame >= frames) continue;
            const int r = rowOf(e.frame);
            if (!starts.empty() && starts.back().row == r) {
                if (starts.back().note < 0) starts.back().note = e.note;
                if (starts.back().track < 0) starts.back().track = e.track;
                continue;
            }
            starts.push_back({r, e.note, e.track});
        }
        cells[size_t(c)].assign(size_t(rows), Cell{});
        bool sounding = false;
        int note = 60, track = -1;
        for (size_t i = 0; i < starts.size(); ++i) {
            const int r0 = starts[i].row;
            const int r1 = i + 1 < starts.size() ? starts[i + 1].row : rows;
            const int fa = cap.rowFrame[size_t(r0)];
            int fb = r1 < rows ? cap.rowFrame[size_t(r1)] : frames;
            int last = -1;
            for (int f = fb - 1; f >= fa && last < 0; --f)
                if (w[size_t(f)].heard) last = f;
            if (last < 0) {  // an attack that plays nothing
                if (sounding) cells[size_t(c)][size_t(r0)].note = kNoteOff;
                sounding = false;
                continue;
            }
            // A note off on the row after the last frame heard, when the channel stays silent up to the next note.
            const int off = rowOf(last) + 1;
            if (off < r1) fb = cap.rowFrame[size_t(off)];
            if (starts[i].note >= 0) note = std::clamp(starts[i].note, 2, 127);
            if (starts[i].track >= 0) track = starts[i].track;
            ++notes;
            int row = r0, f = fa;
            bool first = true;
            while (f < fb) {
                std::optional<Part> part = soundOf(w, f, fb, note);
                int next = -1;
                if (!part) {
                    // Cut at the last row the sequences reach; a row longer than that holds its 63rd tick.
                    for (int r = row + 1; r < r1 && cap.rowFrame[size_t(r)] < fb && cap.rowFrame[size_t(r)] - f <= kSeqMax;
                         ++r)
                        next = r;
                    if (next < 0) {
                        part = soundOf(w, f, std::min(fb, f + kSeqMax), note);
                        ++longer;
                    } else {
                        part = soundOf(w, f, cap.rowFrame[size_t(next)], note);
                    }
                }
                if (!part) break;  // cannot happen: 63 ticks always fit
                approx += part->approx;
                restarts += part->restarts;
                unplayable += part->unplayable;
                mixed += part->mixed;
                const std::string key = part->sound.key();
                auto it = known.find(key);
                if (it == known.end()) {
                    it = known.emplace(key, int(sounds.size())).first;
                    sounds.push_back(part->sound);
                    sounds.back().track = track;
                } else {
                    sounds[size_t(it->second)].weight += part->sound.weight;
                    sounds[size_t(it->second)].first = std::min(sounds[size_t(it->second)].first, part->sound.first);
                    sounds[size_t(it->second)].span = std::max(sounds[size_t(it->second)].span, part->sound.span);
                }
                Cell& cell = cells[size_t(c)][size_t(row)];
                if (first) cell.note = uint8_t(note);
                cell.sound = it->second;
                cell.column = uint8_t(part->column);
                if (next < 0) break;
                row = next;
                f = cap.rowFrame[size_t(next)];
                first = false;
            }
            sounding = off >= r1;
            if (!sounding) cells[size_t(c)][size_t(off)].note = kNoteOff;
        }
    }

    MaxYmExport out;
    out.frameHz = cap.hz;
    out.seconds = double(frames) / cap.hz;
    out.notes = notes;
    // Instruments: the sounds kept, in the order where they first play.
    int merged = 0;
    const std::vector<int> into = mergeSounds(sounds, merged);
    std::vector<int> kept;
    for (size_t i = 0; i < sounds.size(); ++i)
        if (into[i] == int(i)) kept.push_back(int(i));
    std::stable_sort(kept.begin(), kept.end(),
                     [&](int a, int b) { return sounds[size_t(a)].first < sounds[size_t(b)].first; });
    std::vector<int> number(sounds.size(), 0);
    for (size_t k = 0; k < kept.size(); ++k) number[size_t(kept[k])] = int(k) + 1;
    ymio::MyvVoiceset& v = out.voices;
    v.sequences.resize(1);
    std::map<std::string, int> seqNumbers;
    auto seqNumber = [&](const Seq& s) {
        if (s.data.empty()) return 0;
        std::string key{char(s.repeat)};
        for (uint16_t d : s.data) key += {char(d >> 8), char(d & 0xFF)};
        auto it = seqNumbers.find(key);
        if (it != seqNumbers.end()) return it->second;
        ymio::MyvSequence q;
        for (size_t i = 0; i < s.data.size(); ++i) q.data[i] = s.data[i];
        q.length = uint8_t(s.data.size());
        q.repeat = s.repeat;
        v.sequences.push_back(q);
        return seqNumbers.emplace(key, int(v.sequences.size()) - 1).first->second;
    };
    // The Digidrums of the sounds kept, numbered 1 to kMyvDigidrums in the order they come (a project has up to
    // 127); a sound playing one of the others keeps no digidrum.
    std::array<uint8_t, 256> myvOf{};  // slot of the project + 1 → digidrum of the file (0: none)
    std::vector<int> digiSlots;        // slot of the project of each digidrum of the file
    int digiLost = 0;
    for (size_t k = 0; k < kept.size(); ++k) {
        const Sound& s = sounds[size_t(kept[k])];
        ymio::MyvInstrument& ins = v.instruments[k];
        const std::string base = s.track >= 0 && s.track < kTracks && !x.trackNames[size_t(s.track)].empty()
                                     ? x.trackNames[size_t(s.track)]
                                     : s.track >= 0 ? "Track " + std::to_string(s.track + 1) : std::string("Sound");
        ins.name = base + " " + (k + 1 < 10 ? "0" : "") + std::to_string(k + 1);
        ins.at(ymio::MyvInstrument::kArpeggio) = s.arp;
        ins.at(ymio::MyvInstrument::kVibrato) = s.vib;
        ins.at(ymio::MyvInstrument::kFixedFrequency) = s.fix;
        ins.at(ymio::MyvInstrument::kSequenceSpeed) = 1;
        ins.at(ymio::MyvInstrument::kBuzzerWave) = 8;
        ins.at(ymio::MyvInstrument::kBuzzerSync) = s.sync ? 1 : 0;
        if (s.digidrum && !myvOf[s.digidrum] && digiSlots.size() < size_t(ymio::kMyvDigidrums)) {
            digiSlots.push_back(s.digidrum - 1);
            myvOf[s.digidrum] = uint8_t(digiSlots.size());
        }
        digiLost += s.digidrum && !myvOf[s.digidrum];
        ins.at(ymio::MyvInstrument::kDigidrum) = myvOf[s.digidrum];
        ins.at(ymio::MyvInstrument::kDigidrumRate) = s.digiRate;
        // Sequences in the instrument's order: volume, arpeggio, vibrato, mixer, noise, fixed, timer waveform.
        for (size_t q = 0; q < kSeqs; ++q) ins.at(ymio::MyvInstrument::kSequences + int(q)) = uint8_t(seqNumber(s.seq[q]));
    }
    for (size_t d = 0; d < digiSlots.size(); ++d) {
        const int slot = digiSlots[d];
        v.digidrums[d] = sampleOf(x, slot);
        ++out.samples;
        if (x.digiSlots[size_t(slot)].play.loop)
            out.remarks.push_back("Digidrum " + std::to_string(slot + 1) + " played once, without its loop.");
    }
    if (digiLost)
        out.remarks.push_back("Instruments whose Digidrum is past the " + std::to_string(ymio::kMyvDigidrums) +
                              " of maxYMiser, without it: " + std::to_string(digiLost) + ".");
    out.instruments = int(kept.size());

    // Song steps: one at each Song entry (and each repeat of its pattern), every 64 rows.
    ymio::MysSong& song = out.song;
    auto speedOf = [&](int r) { return rowEnd(r) - cap.rowFrame[size_t(r)]; };
    song.speed = std::clamp(speedOf(0), 1, 255);
    song.header[10] = song.header[11] = 0xFF;
    song.header[14] = uint8_t(std::min<uint32_t>(cap.hz, 255));
    song.header[19] = song.header[23] = 7;
    song.header[29] = 1;
    for (int i = 32; i < 36; ++i) song.header[size_t(i)] = 1;
    song.header[36] = 7;  // the timers of the three channels
    song.header[50] = 1;
    song.header[51] = 4;
    const bool loop = x.global.loop;
    const int restartEntry = x.seq.song.restartIndex();
    std::map<std::string, int> patterns;  // a pattern's rows as bytes
    int steps = 0, speed = song.speed, repeat = -1, fast = 0, slow = 0;
    bool cut = false;
    const int room = ymio::kMysSteps - 1;  // and the end step when the song does not loop
    for (int r0 = 0; r0 < rows;) {
        int r1 = r0 + 1;
        while (r1 < rows && r1 - r0 < ymio::kMysRows && cap.rowEntry[size_t(r1)] == cap.rowEntry[size_t(r0)] &&
               cap.rowStep[size_t(r1)] != 0)
            ++r1;
        if (steps == room) {
            cut = true;
            break;
        }
        if (loop && repeat < 0 && cap.rowEntry[size_t(r0)] == restartEntry) repeat = steps;
        std::array<ymio::MysPattern, 3> pat{};
        for (int r = r0; r < r1; ++r) {
            for (int c = 0; c < 3; ++c) {
                const Cell& cell = cells[size_t(c)][size_t(r)];
                ymio::MysRow& row = pat[size_t(c)].rows[size_t(r - r0)];
                row.note = cell.note;
                if (cell.sound >= 0) {
                    row.instrument = uint8_t(number[size_t(into[size_t(cell.sound)])]);
                    row.volume = cell.column;
                }
            }
            // Speed and pattern break on channel A.
            ymio::MysRow& a = pat[0].rows[size_t(r - r0)];
            int sp = speedOf(r);
            if (sp > 255) {
                sp = 255;
                ++slow;
            }
            if (sp != speed || (r == r0 && steps == repeat && r0 > 0)) {
                if (sp < 2) {
                    sp = 2;
                    ++fast;
                }
                if (sp != speed || r == r0) {
                    a.effect1 = kSpeedFx;
                    a.param1 = uint8_t(sp);
                }
                speed = sp;
            }
            if (r == r1 - 1 && r1 - r0 < ymio::kMysRows) a.effect2 = kBreakFx;
        }
        ymio::MysStep& e = song.steps[size_t(steps)];
        bool full = false;
        for (int c = 0; c < 3; ++c) {
            const ymio::MysPattern& p = pat[size_t(c)];
            if (std::all_of(p.rows.begin(), p.rows.end(), [](const ymio::MysRow& row) { return row.empty(); })) {
                e.channel[size_t(c)] = ymio::kMysEmpty;
                continue;
            }
            std::string key;
            for (const ymio::MysRow& row : p.rows)
                for (const uint8_t b : {row.note, row.instrument, row.volume, row.effect1, row.param1, row.effect2,
                                        row.param2})
                    key.push_back(char(b));
            auto it = patterns.find(key);
            if (it == patterns.end()) {
                if (int(song.patterns.size()) == ymio::kMysMaxPatterns) {
                    full = true;
                    break;
                }
                it = patterns.emplace(key, int(song.patterns.size())).first;
                song.patterns.push_back(p);
            }
            e.channel[size_t(c)] = uint8_t(it->second);
        }
        if (full) {
            e = {};
            cut = true;
            break;
        }
        e.dma = 0;
        ++steps;
        r0 = r1;
    }
    out.loops = loop && repeat >= 0;
    if (!out.loops) {
        // The end: a step that cuts the three channels, played again and again.
        ymio::MysStep& e = song.steps[size_t(steps)];
        e.channel = {ymio::kMysNoteOff, ymio::kMysNoteOff, ymio::kMysNoteOff};
        repeat = steps++;
    }
    song.length = steps;
    song.repeat = repeat;
    out.steps = steps;
    out.patterns = int(song.patterns.size());

    auto& n = out.remarks;
    if (cut) n.push_back("The song stops where maxYMiser runs out of song steps (256) or patterns (240).");
    if (cap.truncated) n.push_back("The song was longer than one hour and has been cut.");
    if (merged) n.push_back("Frames played with the closest of the 32 instruments: " + std::to_string(merged) + ".");
    if (approx) n.push_back("Frames whose buzzer or timer pitch is approximate (a third generator): " + std::to_string(approx) + ".");
    if (restarts) n.push_back("Envelope restarts within a row left out: " + std::to_string(restarts) + ".");
    if (unplayable) n.push_back("Timer effect frames beyond the MFP's reach, played at its nearest rate: " + std::to_string(unplayable) + ".");
    if (mixed) n.push_back("Frames whose timer effect differs from their row's: " + std::to_string(mixed) + ".");
    if (longer) n.push_back("Rows longer than 63 frames, their notes held from the 63rd: " + std::to_string(longer) + ".");
    if (fast) n.push_back("Rows of one frame played as two: " + std::to_string(fast) + ".");
    if (slow) n.push_back("Rows longer than 255 frames cut to 255: " + std::to_string(slow) + ".");
    if (fmOff)
        n.push_back("FM Voice frames whose steps play approximately (x0.5, x5 or more than 8 steps): " +
                    std::to_string(fmOff) + ".");
    if (cap.dma) n.push_back("STE DMA sound left out.");
    res.value = std::move(out);
    return res;
}

}  // namespace

std::optional<MaxYmExportResult> exportMaxYm(const ParamValues& values, const NonParamState& extras,
                                             const std::atomic<bool>* cancel) {
    if (voiceMode(values) != ymengine::VoiceMode::Authentic3)
        return MaxYmExportResult{std::nullopt, "maxYMiser plays the three channels of one chip: switch to Authentic 3ch"};
    if (extras.seq.song.entries.empty()) return MaxYmExportResult{std::nullopt, "The Song is empty."};
    std::optional<SongCapture> cap = captureSong(values, extras, cancel);
    if (!cap) return std::nullopt;
    return convert(*cap, extras);
}

std::vector<std::string> maxYmExportMessage(const MaxYmExport& x, const std::string& songFile,
                                            const std::string& voicesFile) {
    std::vector<std::string> lines = {"maxYMiser song exported", "", songFile + " + " + voicesFile};
    lines.push_back(count(x.steps, "song step", "song steps") + ", " + count(x.patterns, "pattern", "patterns") + ", " +
                    count(x.instruments, "instrument", "instruments") +
                    (x.samples ? ", " + count(x.samples, "digidrum", "digidrums") : std::string()) + ".");
    lines.push_back("Speed " + std::to_string(x.song.speed) + " at " + std::to_string(x.frameHz) + " Hz, " +
                    (x.loops ? "loops at step " + std::to_string(x.song.repeat + 1) : std::string("plays once")) + ".");
    if (!x.remarks.empty()) {
        lines.push_back("");
        lines.push_back("Not as heard:");
        for (const std::string& r : x.remarks) lines.push_back("• " + r);
    }
    return lines;
}

}  // namespace stym::plugin
