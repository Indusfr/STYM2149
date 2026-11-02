// STYM2149 — plugincore: export of the Song as a TTrak module (.TPM)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/TTrakExport.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <map>
#include <unordered_map>
#include <utility>

#include "plugincore/Mapping.h"
#include "plugincore/SongCapture.h"
#include "plugincore/TTrakImport.h"
#include "ymengine/Mfp68901.h"
#include "ymengine/Pitch.h"
#include "ymio/TTrakPlayer.h"

namespace stym::plugin {

namespace {

using ymengine::Generator;
using ymengine::RegisterFrame;
using I = ymio::TtrakInstrument;

constexpr int kAmpMax = I::kAmpSteps;  // steps of the amplitude envelope
constexpr int kNoiseMax = 14;          // values of the noise table (its 15th entry jumps back)
constexpr int kFmMax = 256;            // words of the frequency modulation wave
constexpr int kSpeedMax = 127;         // the routine counts table steps and rows in signed bytes
constexpr int kRowFramesMax = kSpeedMax + 1;
constexpr int kPositionsMax = 127;     // the song length is compared as a signed byte
constexpr int kNoteMax = (ymio::kTtrakPitches - 1) / 16;  // 105: the highest note the pitch table reaches
constexpr int kDefaultSpeed = 20;      // the routine's speed before any effect $1C
constexpr uint8_t kTimerBit = 0x80, kNoiseBit = 0x40, kSquareOff = 0x20, kBuzzerBit = 0x10;
constexpr uint8_t kSid = 1, kSyncBuzzer = 5, kFm = 6, kDigidrum = 7;  // TTrak's timer types written here
constexpr uint8_t kSpeedFx = 0x1C, kBreakFx = 0x0B, kAttenFx = 0x1F;
constexpr double kNearCents = 10;  // a pitch played within this counts as the one heard
constexpr double kChipHz = 2'000'000;  // the clock of Authentic 3ch, the ST's
constexpr int kNone = INT_MIN;
enum : int { kSq, kBz, kTm, kGens };  // the generators, in the order of the instrument's matrix

double cents(double a, double b) { return a > 0 && b > 0 ? std::abs(1200 * std::log2(a / b)) : HUGE_VAL; }

// Shapes 0-3 and 4-7 play as 9 and 15; 0 would mean no shape.
uint8_t shapeNibble(int shape) {
    shape &= 15;
    return uint8_t(shape < 4 ? 9 : shape < 8 ? 15 : shape);
}

// ---- A channel at a frame, as the routine has to play it ----

// A Sync Buzzer whose repeating envelope already runs at a whole multiple of its timer's rate: the timer restarts the
// envelope where it is, so a plain buzzer plays it alike and leaves the routine's two timers to the SIDs and digidrums.
bool plainSyncBuzzer(const std::array<uint8_t, 14>& r, const RegisterFrame::Effect& e) {
    const int shape = e.value & 15;
    if (shape < 8 || (shape & 1) || !e.timer.running()) return false;
    const double hz = e.timer.hz();
    const double env = kChipHz / (256.0 * ymengine::envelopeRamps(shape) * std::max(1, r[11] | r[12] << 8));
    const double k = std::round(env / hz);
    return k >= 1 && cents(env, k * hz) <= kNearCents;
}

// Multiple of kFmMultiples that a step plays: its own, or for a step in semitones the multiple within
// kNearCents of it (0 x1, 7 x1.5, 12 x2, 19 x3...); -1 for none.
int fmMultiple(const RegisterFrame::Effect& e, int i) {
    const int v = e.wave[size_t(i)];
    if (!e.semis) return v;
    for (int m = 0; m < ymengine::kFmMultipleCount; ++m)
        if (std::abs(1200 * std::log2(ymengine::kFmMultiples[size_t(m)]) - 100.0 * v) <= kNearCents) return m;
    return -1;
}

// TTrak's FM table of an FM Voice: the factors 0-3 of its four steps, two bits each; -1 where TTrak cannot play
// its wave (a multiple TTrak has not, x0.5, x4, x5 or x6, semitones away from x1, x1.5, x2 and x3, or a wave whose
// steps do not repeat every four).
int fmTable(const RegisterFrame::Effect& e) {
    const int n = std::clamp<int>(e.value, 1, 16);
    auto step = [&](int i) { return fmMultiple(e, i % n); };
    for (int i = 0; i < n; ++i)
        if (step(i) != step(i + 4)) return -1;
    int table = 0;
    for (int k = 0; k < 4; ++k) {
        const int m = step(k);
        const int f = m < 0 ? -1 : ttrakFmFactor(uint8_t(m));
        if (f < 0) return -1;
        table |= f << (2 * k);
    }
    return table;
}

// An FM Voice heard at a frame that TTrak plays as the note alone.
bool fmLost(const RegisterFrame& f, int c) {
    const RegisterFrame::Effect& e = f.fx[size_t(c)];
    const uint8_t amp = f.reg[size_t(8 + c)];
    return e.on && e.gen == Generator::Fm && e.timer.running() && (amp & 31) && !(f.reg[7] >> c & 1) &&
           ((amp & 16) || fmTable(e) < 0);
}

struct Want {
    bool heard = false;
    uint8_t step = 0;  // the amplitude envelope's byte that plays it (0: silence)
    // Square and buzzer periods, MFP cycles of the timer's interrupts (0: not heard).
    std::array<uint32_t, kGens> want{};
    uint8_t np = 0;                 // noise period
    uint8_t timer = 0, value = 0;   // TTrak timer type; sync buzzer: its shape; FM: its table; digidrum: the slot
    bool restart = false;           // the envelope restarts at this frame
    bool steady = false;            // a Sync Buzzer played as a plain buzzer (plainSyncBuzzer)
    uint8_t shape = 0;
};

Want wantOf(const RegisterFrame& f, int c) {
    Want w;
    const auto& r = f.reg;
    const RegisterFrame::Effect& e = f.fx[size_t(c)];
    const bool tone = !(r[7] >> c & 1), noise = !(r[7] >> (3 + c) & 1);
    const uint8_t amp = r[size_t(8 + c)];
    uint8_t vol = 0;
    bool buzzer = false;
    const bool steady = e.on && e.gen == Generator::SyncBuzzer && plainSyncBuzzer(r, e);
    if (e.on && e.gen == Generator::Digidrum && e.timer.running()) {
        w.timer = kDigidrum;
        w.value = e.value;
    } else if (e.on && e.gen == Generator::SyncBuzzer && e.timer.running() && !steady) {
        w.timer = kSyncBuzzer;
        w.value = uint8_t(e.value & 15);
        buzzer = true;
    } else if (e.on && e.gen == Generator::Fm && e.timer.running() && !(amp & 16) && fmTable(e) >= 0) {
        // TTrak's FM: the note's period divided at each interrupt, its volume held.
        w.timer = kFm;
        w.value = uint8_t(fmTable(e));
        vol = uint8_t(amp & 15);
    } else if (e.on && e.gen == Generator::Sid && e.timer.running() && !(amp & 16)) {
        // TTrak's square SID: the volume, then 0, whatever the mixer.
        w.timer = kSid;
        vol = uint8_t(e.value & 15);
    } else if (amp & 16) {
        // A steady Sync Buzzer's timer writes R13 at every frame: its restarts are placed afterwards.
        buzzer = true;
        w.steady = steady;
        w.restart = f.r13 && !steady;
        w.shape = uint8_t((steady ? e.value : r[13]) & 15);
    } else {
        vol = uint8_t(amp & 15);
    }
    w.heard = w.timer == kDigidrum || buzzer || (vol > 0 && (tone || noise || w.timer == kSid));
    if (w.timer == kFm && !tone) w.timer = 0;  // nothing for its steps to divide
    if (!w.heard) return Want{};
    w.step = uint8_t(vol | (buzzer ? kBuzzerBit : 0) | (tone ? 0 : kSquareOff) | (noise ? kNoiseBit : 0) |
                     (w.timer ? kTimerBit : 0));
    if (tone) w.want[kSq] = uint32_t(std::max(1, r[size_t(2 * c)] | (r[size_t(2 * c + 1)] & 15) << 8));
    if (buzzer) w.want[kBz] = uint32_t(std::max(1, r[11] | r[12] << 8));
    if (w.timer) w.want[kTm] = e.timer.periodTicks();
    if (noise) w.np = uint8_t(r[6] & 31);
    return w;
}

// Both silent, or both heard alike: the same timer, volume and mixer, pitches within kNearCents, the same noise.
bool sameWant(const Want& a, const Want& b) {
    if (!a.heard || !b.heard) return a.heard == b.heard;
    if (a.timer != b.timer || a.value != b.value) return false;
    const uint8_t mask = a.timer == kDigidrum ? 0xF0 : 0xFF;  // a digidrum's volumes are its sample's
    if ((a.step & mask) != (b.step & mask)) return false;
    for (int g = 0; g < kGens; ++g)
        if ((a.want[size_t(g)] || b.want[size_t(g)]) && cents(a.want[size_t(g)], b.want[size_t(g)]) > kNearCents)
            return false;
    return !(a.step & kNoiseBit) || a.np == b.np;
}

// What the routine plays at a tick, as a frame of the register capture (the sample slots mapped to the project's).
RegisterFrame frameOf(const ymio::TtrakTick& k, const std::vector<int>& projectSlot) {
    RegisterFrame f;
    std::copy(k.buffer.begin(), k.buffer.end(), f.reg.begin());
    f.reg[13] = k.reg[13];
    f.r13 = k.shapes > 0;
    for (int c = 0; c < 3; ++c) {
        const ymio::TtrakChannelTick& ch = k.ch[size_t(c)];
        RegisterFrame::Effect& e = f.fx[size_t(c)];
        if (!ch.timer || !ch.timerRuns) continue;
        e.on = true;
        e.timer = ymengine::MfpSetting{ch.timerPrescaler, uint16_t(ch.timerData ? ch.timerData : 256)};
        if (ch.timer <= 4) {
            e.gen = Generator::Sid;
            e.value = uint8_t(ch.volume & 15);
        } else if (ch.timer == kSyncBuzzer) {
            e.gen = Generator::SyncBuzzer;
            e.value = uint8_t(ch.timerWave[0] & 15);
        } else if (ch.timer == kFm || ch.timer == uint8_t(ymio::TtrakTimer::FmSync)) {
            e.gen = Generator::Fm;
            e.value = 4;
            for (int i = 0; i < 4; ++i) e.wave[size_t(i)] = ttrakFmMultiple(ch.timerWave[size_t(i)]);
        } else if (ch.timer == kDigidrum) {
            e.gen = Generator::Digidrum;
            e.value = uint8_t(ch.sampleSlot < projectSlot.size() ? projectSlot[ch.sampleSlot] : 255);
        } else {
            e = {};
        }
    }
    return f;
}

// ---- Tables ----

// A table of the instrument: one value per `speed + 1` ticks, the end going back to `loop`.
struct Table {
    std::vector<int> data{0};
    int loop = 0, speed = 0;
    int at(int t) const {
        const int k = t / (speed + 1), n = int(data.size());
        return k < n ? data[size_t(k)] : data[size_t(loop + (k - n) % (n - loop))];
    }
};

// The shortest table of `cap` values at most that plays v (one value per tick), holding or looping its end.
std::optional<Table> fit(const std::vector<int>& v, int cap) {
    const int n = int(v.size());
    if (n == 0) return Table{};
    int bestLen = INT_MAX, bestLoop = 0;
    for (int p = 1; p <= std::min(n, cap); ++p) {
        // The end repeats every p ticks from tick s on.
        int last = -1;
        for (int i = n - 1; i >= p; --i)
            if (v[size_t(i)] != v[size_t(i - p)]) {
                last = i;
                break;
            }
        const int s = last < 0 ? 0 : last - p + 1;
        if (s + p <= cap && s + p < bestLen) {
            bestLen = s + p;
            bestLoop = s;
        }
    }
    if (bestLen == INT_MAX) return std::nullopt;
    Table t;
    t.data.assign(v.begin(), v.begin() + bestLen);
    t.loop = bestLoop;
    return t;
}

// The same at the first speed that fits (each value held speed + 1 ticks).
std::optional<Table> fitSpeed(const std::vector<int>& v, int cap) {
    if (std::optional<Table> t = fit(v, cap)) return t;
    const int n = int(v.size());
    for (int s = 1; s <= kSpeedMax && s < n; ++s) {
        const int k = s + 1;
        bool ok = true;
        for (int i = 0; i < n && ok; ++i) ok = v[size_t(i)] == v[size_t(i - i % k)];
        if (!ok) continue;
        std::vector<int> blocks;
        for (int i = 0; i < n; i += k) blocks.push_back(v[size_t(i)]);
        if (std::optional<Table> t = fit(blocks, cap)) {
            t->speed = s;
            return t;
        }
    }
    return std::nullopt;
}

// A table that plays v approximately: a value every speed + 1 ticks (speed 0 without `speeds`), the last one held.
Table squeeze(const std::vector<int>& v, int cap, bool speeds) {
    const int n = int(v.size());
    Table t;
    t.speed = speeds ? std::clamp((n + cap - 1) / cap - 1, 0, kSpeedMax) : 0;
    t.data.clear();
    for (int i = 0; i < n && int(t.data.size()) < cap; i += t.speed + 1) t.data.push_back(v[size_t(i)]);
    if (t.data.empty()) t.data.push_back(0);
    t.loop = int(t.data.size()) - 1;
    return t;
}

// Values not set (kNone) take the one before; the first ones the first one set (all unset: `none`).
void hold(std::vector<int>& v, int none = 0) {
    const auto first = std::find_if(v.begin(), v.end(), [](int x) { return x != kNone; });
    const int start = first == v.end() ? none : *first;
    int last = start;
    for (int& x : v) {
        if (x == kNone) x = last;
        last = x;
    }
}

// A word of the frequency modulation wave that the routine plays as d at depth 255: (w × 255) >> 8 == d.
uint16_t fmWord(int d) {
    const long w0 = std::lround(d * 256.0 / 255.0);
    for (long w = w0 - 2; w <= w0 + 2; ++w)
        if (((w * 255) >> 8) == d) return uint16_t(int16_t(w));
    return uint16_t(int16_t(w0));
}

// ---- Pitches ----

struct Range {
    int lo = 0, hi = -1;  // pitches (1/16 semitone) that play what is wanted; lo > hi: nothing wanted
    bool exact = false;   // otherwise the nearest pitch
    bool wanted() const noexcept { return lo <= hi; }
    int mid() const noexcept { return (lo + hi) / 2; }
};

// What each pitch of TTrak's tables plays: square period, buzzer period (the square's / 16, R12 at 0), MFP cycles of
// the timer's interrupts.
class Pitches {
public:
    Pitches() {
        const ymio::TtrakPlayer::Tables& t = ymio::TtrakPlayer::defaultTables();
        for (size_t p = 0; p < size_t(ymio::kTtrakPitches); ++p) {
            value_[kSq][p] = t.period[p];
            value_[kBz][p] = uint32_t(((t.period[p] >> 3) + 1) >> 1);
            value_[kTm][p] =
                ymengine::MfpSetting{t.mfpPrescaler[p], uint16_t(t.mfpData[p] ? t.mfpData[p] : 256)}.periodTicks();
        }
    }
    // The pitches that play `want` on generator g, or the nearest one. The timer starts only from pitch 1 on (the
    // routine sets the MFP when the pitch differs from the one set, 0 after an attack).
    const Range& range(int g, uint32_t want) {
        auto& memo = memo_[size_t(g)];
        const auto it = memo.find(want);
        if (it != memo.end()) return it->second;
        int lo = -1, hi = -1, near = 0;
        double err = HUGE_VAL;
        for (int p = g == kTm ? 1 : 0; p < ymio::kTtrakPitches; ++p) {
            const uint32_t v = value_[size_t(g)][size_t(p)];
            if (v == want) {
                if (lo < 0) lo = hi = p;
                else if (hi == p - 1) hi = p;
            }
            const double e = cents(v, want);
            if (e < err) {
                err = e;
                near = p;
            }
        }
        return memo.emplace(want, lo >= 0 ? Range{lo, hi, true} : Range{near, near, false}).first->second;
    }

    uint32_t value(int g, int p) const noexcept { return value_[size_t(g)][size_t(p)]; }

private:
    std::array<std::array<uint32_t, ymio::kTtrakPitches>, kGens> value_{};
    std::array<std::unordered_map<uint32_t, Range>, kGens> memo_;
};

struct Iv {
    int lo, hi;
};
// The value that the most intervals hold (the middle of the first run where they are most): {value, count}.
std::pair<int, int> mostHeld(const std::vector<Iv>& iv) {
    if (iv.empty()) return {0, 0};
    std::vector<std::pair<int, int>> ev;
    for (const Iv& x : iv) {
        ev.push_back({x.lo, 1});
        ev.push_back({x.hi + 1, -1});
    }
    std::sort(ev.begin(), ev.end());
    int n = 0, best = 0, at = 0;
    for (size_t i = 0; i < ev.size(); ++i) {
        n += ev[i].second;
        if (i + 1 < ev.size() && ev[i + 1].first == ev[i].first) continue;
        if (n > best) {
            best = n;
            const int end = i + 1 < ev.size() ? ev[i + 1].first - 1 : ev[i].first;
            at = ev[i].first + (end - ev[i].first) / 2;
        }
    }
    return {at, best};
}
int heldBy(const std::vector<Iv>& iv, int v) {
    return int(std::count_if(iv.begin(), iv.end(), [&](const Iv& x) { return x.lo <= v && v <= x.hi; }));
}
// Intervals of semitones k such that base + 16k falls within each interval.
std::vector<Iv> semitones(const std::vector<Iv>& iv, int base) {
    std::vector<Iv> out;
    for (const Iv& x : iv) {
        const int lo = int(std::ceil((x.lo - base) / 16.0)), hi = int(std::floor((x.hi - base) / 16.0));
        if (lo <= hi) out.push_back({lo, hi});
    }
    return out;
}

// ---- Sounds: the instrument of a row ----

struct Sound {
    I ins;  // the record, without its name
    // What it plays at each tick from its attack, to measure how far two sounds are.
    uint8_t timer = 0, value = 0, shape = 0;
    Table amp, noise, fm;
    bool fmOn = false;
    std::array<int, kGens> add{};     // pitch added to the wave (detune, arpeggio)
    std::array<bool, kGens> fixed{};  // the generator plays a pitch of its own (add)
    // Not part of the sound: how much it plays, where first, from which track, the most ticks a row plays it, the
    // rows that give it.
    int weight = 0, first = INT_MAX, track = -1, span = 0, rows = 1;
    int pitchAt(int g, int t) const {
        return fixed[size_t(g)] ? add[size_t(g)] : (fmOn ? fm.at(t) : 0) + add[size_t(g)];
    }
};

struct Built {
    Sound sound;
    int note = 0;       // the note the instrument plays from (1-105)
    bool exact = true;  // the tables hold every frame (otherwise they were squeezed)
    int lost = 0;       // digidrum frames whose sample is past the module's 32
    int beyond = 0;     // frames whose pitch the tables do not reach
    uint8_t atten = 0;  // what the row takes off the volumes (effect $1F), so that louder and softer notes share it
};

class Builder {
public:
    Builder(const std::vector<Want>& w, Pitches& pitches, const std::vector<int>& sampleOf)
        : w_(w), pitches_(pitches), sampleOf_(sampleOf) {}

    // The instrument that plays frames [f0, f1) from note `note` (−1: the pitch heard first); nullopt if its tables
    // cannot hold them, unless `force` (then squeezed).
    std::optional<Built> build(int f0, int f1, int note, bool force) const;

private:
    const std::vector<Want>& w_;
    Pitches& pitches_;
    const std::vector<int>& sampleOf_;  // TTrak sample slot of each Digidrum slot of the project (−1: none)
};

std::optional<Built> Builder::build(int f0, int f1, int note, bool force) const {
    const int len = f1 - f0;
    auto at = [&](int i) -> const Want& { return w_[size_t(f0 + i)]; };
    Built out;
    Sound& s = out.sound;
    // The timer of the first frame that has one; a frame with another one plays without it.
    for (int i = 0; i < len; ++i)
        if (at(i).timer) {
            s.timer = at(i).timer;
            s.value = at(i).value;
            break;
        }
    int sample = -1;
    if (s.timer == kDigidrum) {
        sample = s.value < sampleOf_.size() ? sampleOf_[s.value] : -1;
        if (sample < 0) s.timer = 0;
    }
    std::vector<int> steps(size_t(len), 0);
    for (int i = 0; i < len; ++i) {
        const Want& x = at(i);
        uint8_t st = x.step;
        if ((st & kTimerBit) && (x.timer != s.timer || x.value != s.value)) {
            st = uint8_t(st & ~kTimerBit);
            out.lost += x.timer == kDigidrum && !s.timer;
        }
        steps[size_t(i)] = st;
    }
    // The volumes brought up to 15, the row's attenuation taking the difference off; the buzzer's volume bits all
    // set, so that the attenuation leaves its envelope bit.
    int loud = 0;
    for (const int st : steps)
        if (!(st & kBuzzerBit)) loud = std::max(loud, st & 15);
    out.atten = uint8_t(loud > 0 ? 15 - loud : 0);
    for (int& st : steps) {
        if (st & kBuzzerBit) st |= 15;
        else if (st & 15) st += out.atten;
    }
    // Amplitude envelope and noise table.
    auto table = [&](const std::vector<int>& v, int cap) -> std::optional<Table> {
        std::optional<Table> t = fitSpeed(v, cap);
        if (t || !force) return t;
        out.exact = false;
        return squeeze(v, cap, true);
    };
    std::optional<Table> amp = table(steps, kAmpMax);
    if (!amp) return std::nullopt;
    s.amp = *amp;
    std::vector<int> nz(size_t(len), kNone);
    bool noisy = false;
    for (int i = 0; i < len; ++i)
        if (steps[size_t(i)] & kNoiseBit) {
            nz[size_t(i)] = at(i).np;
            noisy = true;
        }
    hold(nz);
    if (noisy) {
        std::optional<Table> t = table(nz, kNoiseMax);
        if (!t) return std::nullopt;
        s.noise = *t;
    }
    // Pitches: the first generator heard (square, timer, buzzer) follows the wave from the note; the others the same
    // wave with an offset (detune, arpeggio), or a pitch of their own.
    // A square of period 1 (ultrasonic, under a buzzer or a timer) is TTrak's YM pad: period 0, for the whole
    // instrument.
    bool pad = false, tones = false;
    for (int i = 0; i < len; ++i) {
        pad = pad || at(i).want[kSq] == 1;
        tones = tones || at(i).want[kSq] > 1;
    }
    if (pad && tones) {
        if (!force) return std::nullopt;
        pad = false;
    }
    std::array<std::vector<Range>, kGens> want;
    std::array<int, kGens> wanted{};
    std::vector<bool> beyond(size_t(len), false);  // a pitch out of the tables' reach
    for (int g = 0; g < kGens; ++g) want[size_t(g)].assign(size_t(len), Range{});
    for (int i = 0; i < len; ++i)
        for (int g = 0; g < kGens; ++g) {
            const uint32_t x = at(i).want[size_t(g)];
            if (!x || (g == kTm && !(steps[size_t(i)] & kTimerBit)) || (g == kSq && pad)) continue;
            const Range& r = pitches_.range(g, x);
            want[size_t(g)][size_t(i)] = r;
            ++wanted[size_t(g)];
            if (!r.exact && cents(pitches_.value(g, r.lo), x) > kNearCents) beyond[size_t(i)] = true;
        }
    out.beyond = int(std::count(beyond.begin(), beyond.end(), true));
    const int prim = wanted[kSq] ? kSq : wanted[kTm] ? kTm : wanted[kBz] ? kBz : -1;
    // The note: the sequencer's, or that of the pitch heard first. Each pitch is the one nearest the note's that plays
    // what is heard, so that notes played alike share an instrument.
    if (note <= 0) {
        note = 46;
        for (int i = 0; i < len && prim >= 0; ++i)
            if (want[size_t(prim)][size_t(i)].wanted()) {
                note = (want[size_t(prim)][size_t(i)].mid() + 8) / 16;
                break;
            }
    }
    out.note = std::clamp(note, 1, kNoteMax);
    const int home = 16 * out.note;
    std::vector<int> pitch(size_t(len), kNone);
    std::vector<int> others;
    std::array<bool, kGens> relative{}, arp{}, detune{};
    int detuneBy = 0, arpBy = 0, fixedAt = 0;
    if (prim >= 0) {
        relative[size_t(prim)] = true;
        for (int i = 0; i < len; ++i)
            if (want[size_t(prim)][size_t(i)].wanted())
                pitch[size_t(i)] = std::clamp(home, want[size_t(prim)][size_t(i)].lo, want[size_t(prim)][size_t(i)].hi);
        for (int g : {kSq, kTm, kBz})
            if (g != prim && wanted[size_t(g)]) others.push_back(g);
        // For each other generator: the offsets from the first one that play it, where both are heard, and the
        // pitches that play it.
        struct Fit {
            std::vector<Iv> rel, abs;
            int alone = 0;  // frames where it is heard without the first one (they take its pitch)
        };
        std::array<Fit, kGens> fits;
        for (int g : others) {
            Fit& f = fits[size_t(g)];
            for (int i = 0; i < len; ++i) {
                const Range& r = want[size_t(g)][size_t(i)];
                if (!r.wanted()) continue;
                f.abs.push_back({r.lo, r.hi});
                if (pitch[size_t(i)] == kNone) ++f.alone;
                else f.rel.push_back({r.lo - pitch[size_t(i)], r.hi - pitch[size_t(i)]});
            }
        }
        if (!others.empty()) {
            const int g = others[0];
            const Fit& f = fits[size_t(g)];
            const auto [rel, relCount] = mostHeld(f.rel);
            const auto [abs, absCount] = mostHeld(f.abs);
            if (absCount > relCount + f.alone) {
                s.fixed[size_t(g)] = true;
                fixedAt = abs;
            } else {
                relative[size_t(g)] = true;
                detune[size_t(g)] = rel != 0;
                detuneBy = rel;
            }
        }
        if (others.size() == 2) {
            // The third generator: what plays it most often of an arpeggio, the detune, both, or a pitch of its own.
            const int g = others[1];
            const Fit& f = fits[size_t(g)];
            const bool detuneFree = !detune[size_t(others[0])];
            const bool fixedFree = !s.fixed[size_t(others[0])];
            enum { kWave, kDetune, kArp, kArpDetune, kFixed };
            std::array<std::pair<int, int>, 5> choice{};  // {value, frames}
            choice[kWave] = {0, heldBy(f.rel, 0)};
            if (detuneFree) choice[kDetune] = mostHeld(f.rel);
            choice[kArp] = mostHeld(semitones(f.rel, 0));
            if (!detuneFree) choice[kArpDetune] = mostHeld(semitones(f.rel, detuneBy));
            if (fixedFree) choice[kFixed] = mostHeld(f.abs);
            int pick = kWave;
            for (int k : {kDetune, kArp, kArpDetune, kFixed})
                if (choice[size_t(k)].second > choice[size_t(pick)].second) pick = k;
            switch (pick) {
            case kDetune:
                relative[size_t(g)] = detune[size_t(g)] = true;
                detuneBy = choice[kDetune].first;
                break;
            case kArp:
            case kArpDetune:
                relative[size_t(g)] = arp[size_t(g)] = true;
                detune[size_t(g)] = pick == kArpDetune;
                arpBy = choice[size_t(pick)].first;
                break;
            case kFixed:
                s.fixed[size_t(g)] = true;
                fixedAt = choice[kFixed].first;
                break;
            default: relative[size_t(g)] = true; break;
            }
        }
        // Where the first generator is silent, the wave follows the others.
        for (int i = 0; i < len; ++i) {
            if (pitch[size_t(i)] != kNone) continue;
            for (int g : others) {
                const Range& r = want[size_t(g)][size_t(i)];
                if (!relative[size_t(g)] || !r.wanted()) continue;
                const int off = (detune[size_t(g)] ? detuneBy : 0) + (arp[size_t(g)] ? 16 * arpBy : 0);
                pitch[size_t(i)] = std::clamp(home + off, r.lo, r.hi) - off;
                break;
            }
        }
    }
    hold(pitch, home);
    arpBy = std::clamp(arpBy, -out.note, 127 - out.note);
    std::vector<int> d(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) d[size_t(i)] = pitch[size_t(i)] - 16 * out.note;
    s.fmOn = prim >= 0 && std::any_of(d.begin(), d.end(), [](int x) { return x != 0; });
    if (s.fmOn) {
        std::optional<Table> t = fit(d, kFmMax);
        if (!t && !force) return std::nullopt;
        if (!t) out.exact = false;
        s.fm = t ? *t : squeeze(d, kFmMax, false);
    }
    for (int g = 0; g < kGens; ++g)
        s.add[size_t(g)] = s.fixed[size_t(g)] ? fixedAt
                                              : (detune[size_t(g)] ? detuneBy : 0) + (arp[size_t(g)] ? 16 * arpBy : 0);

    // The record.
    I& r = s.ins;
    r = I::blank(0);
    r.at(I::kTimerType) = s.timer;
    r.at(I::kYmPad) = pad ? 1 : 0;
    if (s.timer == kDigidrum) r.at(I::kSampleSlot) = uint8_t(sample);
    if (s.timer == kSyncBuzzer)
        for (int k = 0; k < 4; ++k) r.at(I::kTimerTable + k) = s.value;
    if (s.timer == kFm)
        for (int k = 0; k < 4; ++k) r.at(I::kTimerTable + k) = uint8_t(s.value >> (2 * k) & 3);
    r.at(I::kAmpSpeed) = uint8_t(s.amp.speed);
    r.at(I::kAmpLast) = uint8_t(s.amp.data.size() - 1);
    r.at(I::kAmpLoop) = uint8_t(s.amp.loop);
    for (size_t k = 0; k < s.amp.data.size(); ++k) r.at(I::kAmpEnv + int(k)) = uint8_t(s.amp.data[k]);
    if (noisy) {
        r.at(I::kNoiseTable) = uint8_t(s.noise.speed);
        for (size_t k = 0; k < s.noise.data.size(); ++k) r.at(I::kNoiseTable + 1 + int(k)) = uint8_t(s.noise.data[k]);
        r.at(I::kNoiseTable + 1 + int(s.noise.data.size())) = uint8_t(0xF0 | s.noise.loop);
    }
    for (int g = 0; g < kGens; ++g) {
        if (!wanted[size_t(g)]) continue;
        if (s.fixed[size_t(g)]) {
            r.at(I::kMatrixFixed + g) = 1;
            continue;
        }
        if (s.fmOn) r.at(I::kMatrixFm + g) = 1;
        if (detune[size_t(g)]) r.at(I::kMatrixDetune + g) = 1;
        if (arp[size_t(g)]) r.at(I::kMatrixArp + g) = 1;
    }
    if (std::any_of(s.fixed.begin(), s.fixed.end(), [](bool b) { return b; }))
        r.setWord(I::kFixedPitch, uint16_t(fixedAt));
    if (std::any_of(detune.begin(), detune.end(), [](bool b) { return b; }))
        r.setWord(I::kDetune, uint16_t(int16_t(detuneBy)));
    if (std::any_of(arp.begin(), arp.end(), [](bool b) { return b; })) r.at(I::kArpTable + 1) = uint8_t(arpBy & 0x7F);
    if (s.fmOn) {
        r.at(I::kFmSpeed) = 1;
        r.at(I::kFmDepth) = 0xFF;
        r.setWord(I::kFmValue, fmWord(s.fm.data[0]));
        r.setWord(I::kFmPosition, 0);
        r.at(I::kFmLoopOn) = 1;
        r.setWord(I::kFmLoop, uint16_t(2 * s.fm.loop));
        r.setWord(I::kFmEnd, uint16_t(2 * s.fm.data.size()));
        for (size_t k = 0; k < s.fm.data.size(); ++k) r.setWord(I::kFmWave + 2 * int(k), fmWord(s.fm.data[k]));
    }
    // The envelope restarts with the note: its shape written again at the attack (hard sync).
    if (s.timer != kSyncBuzzer && (steps[0] & kBuzzerBit) && at(0).restart) {
        s.shape = shapeNibble(at(0).shape);
        r.at(I::kBuzzerShape) = s.shape;
        r.at(I::kHardSync) = 1;
    }
    s.weight = len;
    s.first = f0;
    s.span = std::min(len, kFmMax);
    return out;
}

// Cost of playing sound a with b's instrument, per tick over the ticks that a plays (256 at most): volume steps, mixer
// changes, noise period steps where a has noise, pitch of the generators that a plays (a semitone counts as 6 volume
// steps); another timer, shape or sample does not merge. With `limit`, it stops once past it.
double distance(const Sound& a, const Sound& b, double limit = HUGE_VAL) {
    if (a.timer != b.timer || a.value != b.value) return 1e9;
    double d = a.shape != b.shape ? 8 : 0;
    auto vol = [](int st) { return st & kBuzzerBit ? 16 : st & 15; };
    for (int t = 0; t < std::max(1, a.span) && d <= limit; ++t) {
        const int sa = a.amp.at(t), sb = b.amp.at(t);
        d += std::abs(vol(sa) - vol(sb));
        for (int bit : {kTimerBit, kNoiseBit, kSquareOff}) d += (sa & bit) != (sb & bit) ? 8 : 0;
        if (!(sa & (kTimerBit | kBuzzerBit | 15))) continue;
        if (sa & kNoiseBit) d += std::abs(a.noise.at(t) - b.noise.at(t));
        for (int g = 0; g < kGens; ++g) {
            const bool plays =
                g == kSq ? !(sa & kSquareOff) : g == kBz ? (sa & kBuzzerBit) != 0 : (sa & kTimerBit) != 0;
            if (!plays) continue;
            if (a.fixed[size_t(g)] != b.fixed[size_t(g)]) d += 40;
            else d += std::min(40.0, std::abs(a.pitchAt(g, t) - b.pitchAt(g, t)) * 6 / 16.0);
        }
    }
    return d;
}

// A sound that another plays as it is (over its own ticks) takes that one's instrument; beyond `room` sounds, the least
// played merges into the closest, until `room` are left. Returns the sound that plays each; `merged`: the rows
// played differently.
std::vector<int> mergeSounds(std::vector<Sound>& sounds, int room, int& merged, const std::atomic<bool>* cancel) {
    const size_t n = sounds.size();
    std::vector<int> into(n);
    for (size_t i = 0; i < n; ++i) into[i] = int(i);
    std::vector<int> rows(n);
    for (size_t i = 0; i < n; ++i) rows[i] = sounds[i].rows;
    std::vector<bool> alive(n, true), lossy(n, false);
    auto join = [&](size_t a, size_t b, double cost) {
        alive[a] = false;
        into[a] = int(b);
        lossy[a] = cost > 0;
        sounds[b].weight += sounds[a].weight;
        sounds[b].first = std::min(sounds[b].first, sounds[a].first);
        sounds[b].span = std::max(sounds[b].span, sounds[a].span);
    };
    // Shortest first: a row that another sound's longer row starts the same way takes its instrument.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return sounds[x].span < sounds[y].span; });
    for (size_t k = 0; k < n; ++k) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        const size_t a = order[k];
        for (size_t j = n; j-- > k + 1;) {
            const size_t b = order[j];
            if (alive[b] && distance(sounds[a], sounds[b], 0) == 0) {
                join(a, b, 0);
                break;
            }
        }
    }
    std::vector<size_t> live;
    for (size_t i = 0; i < n; ++i)
        if (alive[i]) live.push_back(i);
    if (live.size() > size_t(room)) {
        const size_t m = live.size();
        std::vector<std::vector<double>> dist(m, std::vector<double>(m, 0));
        auto measure = [&](size_t a) {
            for (size_t b = 0; b < m; ++b)
                if (a != b) dist[a][b] = distance(sounds[live[a]], sounds[live[b]]);
        };
        for (size_t a = 0; a < m; ++a) measure(a);
        std::vector<bool> on(m, true);
        std::vector<size_t> closest(m, m);
        auto nearest = [&](size_t i) {
            closest[i] = m;
            for (size_t j = 0; j < m; ++j)
                if (j != i && on[j] && (closest[i] == m || dist[i][j] < dist[i][closest[i]])) closest[i] = j;
        };
        for (size_t i = 0; i < m; ++i) nearest(i);
        for (size_t count = m; count > size_t(room); --count) {
            size_t a = m;
            double best = HUGE_VAL;
            for (size_t i = 0; i < m; ++i)
                if (on[i] && closest[i] < m && double(sounds[live[i]].weight) * dist[i][closest[i]] < best) {
                    best = double(sounds[live[i]].weight) * dist[i][closest[i]];
                    a = i;
                }
            if (a == m) break;
            const size_t b = closest[a];
            const int spanB = sounds[live[b]].span;
            on[a] = false;
            join(live[a], live[b], dist[a][b]);
            // b now plays longer rows: what it costs to play them is measured again.
            if (sounds[live[b]].span != spanB) measure(b);
            for (size_t i = 0; i < m; ++i)
                if (on[i] && (closest[i] == a || i == b)) nearest(i);
        }
    }
    for (size_t i = 0; i < n; ++i) {
        int s = int(i);
        bool off = false;
        while (into[size_t(s)] != s) {
            off = off || lossy[size_t(s)];
            s = into[size_t(s)];
        }
        merged += off ? rows[i] : 0;
        into[i] = s;
    }
    return into;
}

// ---- The song ----

struct Row {
    int frame = 0, entry = 0, step = 0;
    bool more = false;  // a step longer than kRowFramesMax goes on
};

constexpr int kSilence = -2;  // the instrument that silences a channel
struct Cell {
    uint8_t note = 0;
    int sound = -1;     // −1: none
    uint8_t atten = 0;  // effect $1F with the instrument
};

std::string count(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

std::optional<TtrakExportResult> convert(const SongCapture& cap, const NonParamState& x, const std::string& title,
                                         const std::atomic<bool>* cancel) {
    auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    TtrakExportResult res;
    const int frames = int(cap.frames.size());
    if (frames == 0 || cap.rowFrame.empty()) {
        res.error = "The Song plays nothing.";
        return res;
    }
    std::array<std::vector<Want>, 3> wants;
    int fmAlone = 0;  // frames of an FM Voice that TTrak plays as the note alone
    for (int c = 0; c < 3; ++c) {
        wants[size_t(c)].resize(size_t(frames));
        for (int f = 0; f < frames; ++f) {
            wants[size_t(c)][size_t(f)] = wantOf(cap.frames[size_t(f)], c);
            fmAlone += fmLost(cap.frames[size_t(f)], c);
        }
        // A Sync Buzzer played as a plain buzzer restarts where the buzzer starts or changes shape.
        for (int f = 0; f < frames; ++f) {
            Want& w = wants[size_t(c)][size_t(f)];
            if (!w.steady) continue;
            const Want* p = f > 0 ? &wants[size_t(c)][size_t(f - 1)] : nullptr;
            w.restart = !p || !(p->step & kBuzzerBit) || p->timer || p->shape != w.shape;
        }
    }
    auto plainBuzzer = [&](int c, int f) {
        const Want& w = wants[size_t(c)][size_t(f)];
        return (w.step & kBuzzerBit) && w.timer != kSyncBuzzer;
    };
    // Where an instrument has to start within a step: a digidrum that starts, an envelope that restarts.
    std::vector<bool> trigger(size_t(frames), false);
    for (int f = 0; f < frames; ++f) {
        const RegisterFrame& x0 = cap.frames[size_t(f)];
        for (int c = 0; c < 3; ++c) {
            const RegisterFrame::Effect& e = x0.fx[size_t(c)];
            if ((e.on && e.restart && e.gen == Generator::Digidrum) ||
                (wants[size_t(c)][size_t(f)].restart && plainBuzzer(c, f)))
                trigger[size_t(f)] = true;
        }
    }
    // Rows: the steps played (the first from frame 0), a step on several rows where an instrument starts within it
    // and where it is longer than 128 frames.
    std::vector<Row> rows;
    for (size_t r = 0; r < cap.rowFrame.size(); ++r) {
        const int f0 = r == 0 ? 0 : cap.rowFrame[r];
        const int f1 = r + 1 < cap.rowFrame.size() ? cap.rowFrame[r + 1] : frames;
        for (int f = f0, from = f0; f < f1; ++f)
            if (f == f0 || trigger[size_t(f)] || f - from == kRowFramesMax) {
                rows.push_back({f, cap.rowEntry[r], cap.rowStep[r], f > f0});
                from = f;
            }
    }
    const int nRows = int(rows.size());
    auto rowFrame = [&](int r) { return r < nRows ? rows[size_t(r)].frame : frames; };
    auto rowOf = [&](int f) {
        const auto it = std::upper_bound(rows.begin(), rows.end(), f, [](int v, const Row& r) { return v < r.frame; });
        return std::max(0, int(it - rows.begin()) - 1);
    };

    // The module's samples: the Digidrum slots in the order they first play, 32 at most.
    std::vector<int> sampleOf(size_t(ymengine::kDigidrumSlotsMax), -1), projectSlot;
    int samplesLost = 0;
    for (const RegisterFrame& f : cap.frames)
        for (const RegisterFrame::Effect& e : f.fx) {
            if (!e.on || e.gen != Generator::Digidrum || e.value >= sampleOf.size() || sampleOf[e.value] >= 0) continue;
            if (projectSlot.size() == size_t(ymio::kTtrakSampleSlots)) {
                sampleOf[e.value] = -2;
                ++samplesLost;
                continue;
            }
            sampleOf[e.value] = int(projectSlot.size());
            projectSlot.push_back(e.value);
        }
    for (int& s : sampleOf) s = std::max(s, -1);

    // The rows where an instrument starts on each channel: an attack of the sequencer (its note), a digidrum that
    // starts, and an envelope restart at the start of a row that no attack writes.
    struct Start {
        int note = -1;  // MIDI note; −1: the instrument alone
        int track = -1;
    };
    std::array<std::map<int, Start>, 3> starts;
    for (int c = 0; c < 3; ++c) {
        for (const SongAttack& a : cap.attacks[size_t(c)]) {
            if (a.frame >= frames) continue;
            Start& s = starts[size_t(c)][rowOf(a.frame)];
            if (s.note < 0) s.note = a.note;
            if (s.track < 0) s.track = a.track;
        }
        for (int f = 0; f < frames; ++f) {
            const RegisterFrame::Effect& e = cap.frames[size_t(f)].fx[size_t(c)];
            if (e.on && e.restart && e.gen == Generator::Digidrum) starts[size_t(c)].try_emplace(rowOf(f));
        }
    }
    for (int r = 0; r < nRows; ++r) {
        const int f = rows[size_t(r)].frame;
        if (!cap.frames[size_t(f)].r13) continue;
        bool written = false;
        for (int c = 0; c < 3 && !written; ++c) written = starts[size_t(c)].count(r) && plainBuzzer(c, f);
        for (int c = 0; c < 3 && !written; ++c)
            if (plainBuzzer(c, f)) {
                starts[size_t(c)].try_emplace(r);
                written = true;
            }
    }

    Pitches pitches;
    std::array<std::vector<Cell>, 3> cells;
    std::vector<Sound> sounds;
    std::map<std::string, int> known;
    std::vector<bool> shapeAt(size_t(frames), false);  // frames where an attack writes the envelope's shape
    int notes = 0, squeezed = 0, digiLost = 0, beyond = 0;
    bool silence = false;
    for (int c = 0; c < 3; ++c) {
        const std::vector<Want>& w = wants[size_t(c)];
        const Builder builder(w, pitches, sampleOf);
        cells[size_t(c)].assign(size_t(nRows), Cell{});
        // Frames heard before the first start play from row 0.
        int first = -1;
        for (int f = 0; f < frames && first < 0; ++f)
            if (w[size_t(f)].heard) first = f;
        if (first >= 0 && (starts[size_t(c)].empty() || rowFrame(starts[size_t(c)].begin()->first) > first))
            starts[size_t(c)].try_emplace(rowOf(first));
        bool sounding = false;
        int note = 0, track = -1;
        for (auto it = starts[size_t(c)].begin(); it != starts[size_t(c)].end(); ++it) {
            if (cancelled()) return std::nullopt;
            const int r0 = it->first;
            const auto next = std::next(it);
            const int r1 = next != starts[size_t(c)].end() ? next->first : nRows;
            const int fa = rowFrame(r0), fb = rowFrame(r1);
            if (std::none_of(w.begin() + fa, w.begin() + fb, [](const Want& v) { return v.heard; })) {
                // An attack that plays nothing.
                if (sounding) {
                    cells[size_t(c)][size_t(r0)].sound = kSilence;
                    silence = true;
                }
                sounding = false;
                continue;
            }
            sounding = true;
            if (it->second.track >= 0) track = it->second.track;
            const bool attack = it->second.note >= 0 || note == 0;
            const int want = it->second.note >= 0 ? std::clamp(it->second.note - 23, 1, kNoteMax) : note ? note : -1;
            if (it->second.note >= 0) ++notes;
            int row = r0;
            bool firstPart = true;
            while (row < r1) {
                const int f = rowFrame(row);
                auto tryTo = [&](int r) { return builder.build(f, rowFrame(r), want, false); };
                std::optional<Built> part = tryTo(r1);
                int to = r1;
                if (!part) {
                    // The furthest row that one instrument reaches exactly; a digidrum is never cut (it would start
                    // again), nor a row longer than the tables.
                    const bool digi = std::any_of(w.begin() + f, w.begin() + fb, [](const Want& v) {
                        return v.timer == kDigidrum;
                    });
                    int lo = row, hi = r1;
                    std::optional<Built> loPart;
                    if (!digi) {
                        for (int stepBy = 1; lo + 1 < hi; stepBy *= 2) {
                            const int r = std::min(hi - 1, row + stepBy);
                            if (r <= lo) break;
                            std::optional<Built> b = tryTo(r);
                            if (!b) {
                                hi = r;
                                break;
                            }
                            lo = r;
                            loPart = std::move(b);
                        }
                        while (hi - lo > 1) {
                            const int mid = lo + (hi - lo) / 2;
                            std::optional<Built> b = tryTo(mid);
                            if (b) {
                                lo = mid;
                                loPart = std::move(b);
                            } else {
                                hi = mid;
                            }
                        }
                    }
                    if (loPart) {
                        part = std::move(loPart);
                        to = lo;
                    } else {
                        to = digi ? r1 : row + 1;
                        part = builder.build(f, rowFrame(to), want, true);
                    }
                }
                Built& b = *part;
                squeezed += b.exact ? 0 : 1;
                digiLost += b.lost;
                beyond += b.beyond;
                // A sync buzzer's attack writes shape $F.
                if (b.sound.shape || b.sound.timer == kSyncBuzzer) shapeAt[size_t(f)] = true;
                const std::string key(b.sound.ins.raw.begin(), b.sound.ins.raw.end());
                auto k = known.find(key);
                if (k == known.end()) {
                    k = known.emplace(key, int(sounds.size())).first;
                    sounds.push_back(b.sound);
                    sounds.back().track = track;
                } else {
                    Sound& s = sounds[size_t(k->second)];
                    s.weight += b.sound.weight;
                    s.first = std::min(s.first, b.sound.first);
                    s.span = std::max(s.span, b.sound.span);
                    ++s.rows;
                }
                Cell& cell = cells[size_t(c)][size_t(row)];
                if (firstPart && attack) cell.note = uint8_t(b.note);
                cell.sound = k->second;
                cell.atten = b.atten;
                note = b.note;
                firstPart = false;
                row = to;
            }
        }
    }

    TtrakExport out;
    out.frameHz = cap.hz;
    out.seconds = double(frames) / cap.hz;
    out.notes = notes;
    ymio::TtrakModule& m = out.module;
    m = ymio::TtrakModule::blank();
    m.patterns.clear();
    m.setTitle(title);
    // Instruments: the sounds kept, in the order where they first play, then the silence.
    const bool loop = x.global.loop;
    const int restartEntry = x.seq.song.restartIndex();
    const bool loops =
        loop && std::any_of(rows.begin(), rows.end(), [&](const Row& r) { return r.entry == restartEntry; });
    const bool silent = silence || !loops;
    int merged = 0;
    const std::vector<int> into = mergeSounds(sounds, ymio::kTtrakInstruments - (silent ? 1 : 0), merged, cancel);
    if (cancelled()) return std::nullopt;
    std::vector<int> kept;
    for (size_t i = 0; i < sounds.size(); ++i)
        if (into[i] == int(i)) kept.push_back(int(i));
    std::stable_sort(kept.begin(), kept.end(),
                     [&](int a, int b) { return sounds[size_t(a)].first < sounds[size_t(b)].first; });
    std::vector<int> number(sounds.size(), 0);
    for (size_t k = 0; k < kept.size(); ++k) {
        number[size_t(kept[k])] = int(k) + 1;
        const Sound& s = sounds[size_t(kept[k])];
        I& ins = m.instruments[k];
        ins = s.ins;
        const std::string base = s.track >= 0 && s.track < kTracks && !x.trackNames[size_t(s.track)].empty()
                                     ? x.trackNames[size_t(s.track)]
                                     : s.track >= 0 ? "Track " + std::to_string(s.track + 1) : std::string("Sound");
        ins.setName(base + " " + (k + 1 < 10 ? "0" : "") + std::to_string(k + 1));
    }
    const int silenceNumber = int(kept.size()) + 1;
    if (silent) {
        I& ins = m.instruments[size_t(silenceNumber - 1)];
        ins = I::blank(silenceNumber);
        ins.setName("Silence");
    }
    out.instruments = int(kept.size()) + (silent ? 1 : 0);
    // Samples: the Digidrum slots between their Start and End, as volumes 0-15.
    for (size_t k = 0; k < projectSlot.size(); ++k) {
        const int slot = projectSlot[k];
        const auto& levels = x.digidrums[size_t(slot)];
        const ymengine::DigidrumSlot& play = x.digiSlots[size_t(slot)].play;
        const size_t end = play.end ? std::min<size_t>(play.end, levels.size()) : levels.size();
        std::vector<uint8_t> v;
        for (size_t i = std::min<size_t>(play.start, end); i < end; ++i) v.push_back(std::min<uint8_t>(levels[i], 15));
        m.samples[k] = ymio::ttrakSampleBytes(v);
        ++out.samples;
        if (play.loop)
            out.remarks.push_back("Digidrum " + std::to_string(slot + 1) + " played once, without its loop.");
    }

    // Positions: one at each Song entry (and each repeat of its pattern), every 64 rows.
    std::map<std::string, int> patterns;
    int positions = 0, speed = kDefaultSpeed, repeat = -1, louder = 0;
    bool cut = false;
    const int room = kPositionsMax - (loops ? 0 : 1);
    const int patternRoom = ymio::kTtrakMaxPatterns - (loops ? 0 : 1);
    for (int r0 = 0; r0 < nRows;) {
        int r1 = r0 + 1;
        while (r1 < nRows && r1 - r0 < ymio::kTtrakRows && rows[size_t(r1)].entry == rows[size_t(r0)].entry &&
               (rows[size_t(r1)].step != 0 || rows[size_t(r1)].more))
            ++r1;
        if (positions == room) {
            cut = true;
            break;
        }
        if (loops && repeat < 0 && rows[size_t(r0)].entry == restartEntry) repeat = positions;
        ymio::TtrakPattern pat;
        for (int r = r0; r < r1; ++r) {
            auto& row = pat.rows[size_t(r - r0)];
            for (int c = 0; c < 3; ++c) {
                const Cell& cell = cells[size_t(c)][size_t(r)];
                ymio::TtrakCell& dst = row[size_t(c)];
                dst.note = cell.note;
                if (cell.sound == kSilence) dst.instrument = uint8_t(silenceNumber);
                else if (cell.sound >= 0) dst.instrument = uint8_t(number[size_t(into[size_t(cell.sound)])]);
                if (dst.instrument && cell.atten) {
                    dst.effect = kAttenFx;
                    dst.param = cell.atten;
                }
            }
            // The speed where it changes and on the first row of the song and of the restart position, the pattern
            // break on the last row of a short position: on a channel without effect, or in place of an attenuation.
            auto place = [&](uint8_t effect, uint8_t param) {
                for (const bool over : {false, true})
                    for (ymio::TtrakCell& cell : row)
                        if (!cell.effect || (over && cell.effect == kAttenFx)) {
                            louder += cell.effect == kAttenFx;
                            cell.effect = effect;
                            cell.param = param;
                            return;
                        }
            };
            const int sp = rowFrame(r + 1) - rowFrame(r) - 1;
            if (sp != speed || (r == r0 && (positions == 0 || positions == repeat))) {
                place(kSpeedFx, uint8_t(sp));
                speed = sp;
            }
            if (r == r1 - 1 && r1 - r0 < ymio::kTtrakRows) place(kBreakFx, 0);
        }
        std::string key;
        for (const auto& row : pat.rows)
            for (const ymio::TtrakCell& cell : row)
                key += {char(cell.note), char(cell.instrument), char(cell.effect), char(cell.param)};
        auto it = patterns.find(key);
        if (it == patterns.end()) {
            if (int(m.patterns.size()) == patternRoom) {
                cut = true;
                break;
            }
            it = patterns.emplace(key, int(m.patterns.size())).first;
            m.patterns.push_back(pat);
        }
        m.order[size_t(positions++)] = uint8_t(it->second);
        r0 = r1;
    }
    if (loops && repeat < 0) repeat = 0;  // cut before the restart entry: back to the start
    out.loops = loops;
    if (!loops) {
        // The end: a position that silences the three channels, played again and again.
        ymio::TtrakPattern pat;
        for (int c = 0; c < 3; ++c) pat.rows[0][size_t(c)].instrument = uint8_t(silenceNumber);
        pat.rows[0][1].effect = kBreakFx;
        m.patterns.push_back(pat);
        m.order[size_t(positions)] = uint8_t(m.patterns.size() - 1);
        repeat = positions++;
    }
    if (m.patterns.empty()) m.patterns.resize(1);
    m.length = uint8_t(positions);
    m.restart = uint8_t(repeat);
    out.positions = positions;
    out.patterns = int(m.patterns.size());
    // TTrak replays at 50, 100, 150 or 200 Hz.
    static constexpr std::array<uint32_t, 4> kRates{50, 100, 150, 200};
    uint32_t rate = kRates[0];
    for (uint32_t r : kRates)
        if (std::abs(double(r) - cap.hz) < std::abs(double(rate) - cap.hz)) rate = r;
    m.rate = uint8_t(rate);

    // What the routine plays, frame by frame, against the Song.
    int otherwise = 0;
    {
        ymio::TtrakPlayer player(m);
        for (int f = 0; f < frames; ++f) {
            if (cancelled()) return std::nullopt;
            const RegisterFrame got = frameOf(player.tick(), projectSlot);
            bool same = true;
            for (int c = 0; c < 3 && same; ++c) same = sameWant(wants[size_t(c)][size_t(f)], wantOf(got, c));
            otherwise += same ? 0 : 1;
        }
    }
    int restarts = 0;
    for (int f = 0; f < frames; ++f)
        if (cap.frames[size_t(f)].r13 && !shapeAt[size_t(f)])
            for (int c = 0; c < 3; ++c)
                if (plainBuzzer(c, f) && f > 0) {
                    ++restarts;
                    break;
                }

    auto& n = out.remarks;
    if (otherwise) n.push_back("Frames that TTrak plays otherwise: " + std::to_string(otherwise) + " of " +
                               std::to_string(frames) + ".");
    if (beyond)
        n.push_back("Frames beyond TTrak's pitch tables (or a timer too slow): " + std::to_string(beyond) + ".");
    if (cut) n.push_back("The song stops where TTrak runs out of positions (127) or patterns (128).");
    if (cap.truncated) n.push_back("The song was longer than one hour and has been cut.");
    if (rate != cap.hz)
        n.push_back("Replayed at " + std::to_string(rate) + " Hz instead of " + std::to_string(cap.hz) +
                    " Hz (TTrak's rates: 50, 100, 150, 200 Hz).");
    if (merged) n.push_back("Notes played with the closest of the 32 instruments: " + std::to_string(merged) + ".");
    if (squeezed)
        n.push_back("Notes longer than an instrument's tables, played approximately: " + std::to_string(squeezed) +
                    ".");
    if (louder)
        n.push_back("Notes played louder (their row has no room for the volume effect): " + std::to_string(louder) +
                    ".");
    if (restarts) n.push_back("Envelope restarts within a row left out: " + std::to_string(restarts) + ".");
    if (fmAlone)
        n.push_back("FM Voice frames played as the note alone (TTrak's FM: four steps of x1, x1.5, x2 or x3, no "
                    "envelope): " + std::to_string(fmAlone) + ".");
    if (samplesLost || digiLost)
        n.push_back("Digidrums past the 32 samples of TTrak left out: " + std::to_string(samplesLost) + ".");
    if (cap.dma) n.push_back("STE DMA sound left out.");
    res.value = std::move(out);
    return res;
}

}  // namespace

std::optional<TtrakExportResult> exportTtrak(const ParamValues& values, const NonParamState& extras,
                                             const std::string& title, const std::atomic<bool>* cancel) {
    if (voiceMode(values) != ymengine::VoiceMode::Authentic3)
        return TtrakExportResult{std::nullopt, "TTrak plays the three channels of one chip: switch to Authentic 3ch"};
    if (extras.seq.song.entries.empty()) return TtrakExportResult{std::nullopt, "The Song is empty."};
    std::optional<SongCapture> cap = captureSong(values, extras, cancel);
    if (!cap) return std::nullopt;
    return convert(*cap, extras, title, cancel);
}

std::vector<std::string> ttrakExportMessage(const TtrakExport& x, const std::string& fileName) {
    std::vector<std::string> lines = {"TTrak module exported", "", fileName};
    lines.push_back(count(x.positions, "position", "positions") + ", " + count(x.patterns, "pattern", "patterns") +
                    ", " + count(x.instruments, "instrument", "instruments") +
                    (x.samples ? ", " + count(x.samples, "sample", "samples") : std::string()) + ".");
    const std::string end =
        x.loops ? "loops at position " + std::to_string(x.module.restart + 1) : std::string("plays once");
    lines.push_back(std::to_string(x.module.rate) + " Hz, " + end + ".");
    if (!x.remarks.empty()) {
        lines.push_back("");
        lines.push_back("Not as heard:");
        for (const std::string& r : x.remarks) lines.push_back("• " + r);
    }
    return lines;
}

}  // namespace stym::plugin
