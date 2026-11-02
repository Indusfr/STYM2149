// STYM2149 — ymio: maxYMiser songs played tick by tick
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The routine works on bytes and 16-bit words of the 68000: the arithmetic below keeps their widths, their wrap-around
// and their signed or unsigned comparisons, so that the registers come out the same.
#include "ymio/MaxYmPlayer.h"

#include <algorithm>
#include <cmath>

namespace stym::ymio {

namespace {

constexpr int kSeqWords = 64;      // 63 steps, then the length and repeat bytes
constexpr uint8_t kFreqReset = 2;  // period written before the real one to restart a square
constexpr int kSidSteps = 16, kSyncBuzzerSteps = 8, kFmSteps = 8;
constexpr int kPulseTune = 4;      // the pulse width timer runs this much faster (68000)
// MFP prescalers of the timer control values 1 to 7.
constexpr std::array<int, 8> kPrescale = {0, 4, 10, 16, 50, 64, 100, 200};

double noteHz(int n) noexcept { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

std::array<uint16_t, 128> table(double num) {
    std::array<uint16_t, 128> t{};
    for (int n = 0; n < 128; ++n) t[size_t(n)] = uint16_t(std::floor(num / noteHz(n) + 0.5));
    return t;
}
const std::array<uint16_t, 128>& squares() {
    static const auto t = table(2'000'000.0 / 16);
    return t;
}
const std::array<uint16_t, 128>& buzzers() {
    static const auto t = table(2'000'000.0 / 256);
    return t;
}
const std::array<uint16_t, 128>& semitones() {
    static const auto t = [] {
        std::array<uint16_t, 128> s{};
        for (int i = 0; i < 128; ++i) s[size_t(i)] = uint16_t(std::floor(32 * std::pow(2.0, i / 12.0) + 0.5));
        return s;
    }();
    return t;
}
// The tables are read at an index the routine does not check; beyond them it would read other data.
int clampIndex(int i) noexcept { return std::clamp(i, 0, 127); }

int16_t s16(int v) noexcept { return int16_t(uint16_t(v)); }
int8_t s8(int v) noexcept { return int8_t(uint8_t(v)); }

// Highest timer period that prescaler k can play for a waveform of `steps` steps (row 0 of the routine, used by the
// one-step effects, plays two steps per period); 0: not used (very slow waveforms of many steps).
int periodLimit(int steps, int k) noexcept {
    if (k == 0) return 0;
    const int l = std::max(2, steps);
    if ((k == 7 && l > 5) || (k == 6 && l > 9) || (k == 5 && l > 12)) return 0;
    return 511 * l * 10 * kPrescale[size_t(k)] / 393;
}
// Prescaler (1-7) that plays `period` with `steps` steps, 0 if none can (the effect is silenced).
int prescalerFor(int steps, uint16_t period) noexcept {
    for (int k = 1; k < 8; ++k)
        if (period <= periodLimit(steps, k)) return k;
    return 0;
}
// MFP data value: the timer runs `steps` times per period of the note.
int timerData(uint16_t period, int steps, int k) noexcept {
    const uint32_t q = uint32_t(period) * 393u / uint32_t(steps * 10 * kPrescale[size_t(k)]);
    return int((q + 1) >> 1);
}
// The effect can play: a prescaler exists and the fastest one does not run above 25.6 kHz.
bool playable(uint16_t period, int steps) noexcept {
    const int k = prescalerFor(steps, period);
    return k != 0 && !(k == 1 && timerData(period, steps, k) < 24);
}

// Portamento and slide in period units, scaled to the semitones `adj` that the arpeggio, transpose and detune add
// to the note (the table read as signed words, as the 68000 multiplies and divides them).
int16_t portaShift(int16_t acc, int adj) noexcept {
    if (s16(adj * 2) < 0) {
        const int32_t prod = int32_t(acc) * int16_t(semitones()[size_t(clampIndex(-s16(adj * 2) / 2))]);
        return int16_t(s16(prod) >> 5);
    }
    const int32_t num = int32_t(acc) * 32;
    const int16_t div = int16_t(semitones()[size_t(clampIndex(adj))]);
    const int32_t q = num / div;
    return (q >= -32768 && q <= 32767) ? int16_t(q) : s16(num);  // a quotient beyond 16 bits leaves the dividend
}

}  // namespace

uint16_t maxYmSquarePeriod(int n) noexcept { return squares()[size_t(clampIndex(n))]; }
uint16_t maxYmBuzzerPeriod(int n) noexcept { return buzzers()[size_t(clampIndex(n))]; }
uint16_t maxYmSemitoneScale(int i) noexcept { return semitones()[size_t(clampIndex(i))]; }

double maxYmTimerHz(uint16_t period, int steps) noexcept {
    if (steps < 1 || !playable(period, steps)) return 0;
    const int k = prescalerFor(steps, period);
    const int data = timerData(period, steps, k);
    return data > 0 ? 2'457'600.0 / (kPrescale[size_t(k)] * data) : 0;
}

void MaxYmPlayer::Voice::restart() noexcept {
    noiseTranspose = 0;
    seqTick = 0;
    synced = false;
    volumeColumn = 0;
    portaRate = portaAcc = 0;
    extraArp = {};
    extraArpStep = 0;
    digidrumStarted = false;
    transpose = 0;
    pos = {};
    pulseSet = false;
    vol = mix = noise = fix = 0;
    arp = vib = 0;
    pwm = 0;
    done = 0;
    slideRate = slideAcc = 0;
}

void MaxYmPlayer::Voice::cut() noexcept {
    rec = {};
    restart();
    instrument = 0;
}

MaxYmPlayer::MaxYmPlayer(const MysSong& song, const MyvVoiceset& voices) : song_(song), voices_(voices) {
    speed_ = song_.speed;
    timerMask_ = song_.header[36] & 7;
    mutes_ = song_.header[37];
    midiClock_ = (song_.header[31] & 1) != 0;
    step_ = 0;
    next_ = 1;
    patterns_ = {song_.steps[0].channel[0], song_.steps[0].channel[1], song_.steps[0].channel[2],
                 song_.steps[0].dma};
}

uint16_t MaxYmPlayer::sequenceWord(uint8_t n, int at) const noexcept {
    // The sequences lie one after another, 63 words then the length and repeat bytes; a position past the end of one
    // reads into the next ones.
    const size_t w = size_t(n) * kSeqWords + size_t(at);
    const size_t q = w / kSeqWords, i = w % kSeqWords;
    if (q >= voices_.sequences.size()) return 0;
    const MyvSequence& s = voices_.sequences[q];
    return i < size_t(kMyvSequenceLength) ? s.data[i] : uint16_t(s.length << 8 | s.repeat);
}

void MaxYmPlayer::readRow(int c, uint8_t pattern, int row, MaxYmChannelTick& o) {
    Voice& v = v_[size_t(c)];
    const bool muted = (mutes_ >> c & 1) != 0;
    if (pattern >= kMysMaxPatterns) {
        if (!muted && pattern == kMysNoteOff && row == 0) {
            v.cut();
            o.cut = true;
        }
        return;
    }
    if (pattern >= song_.patterns.size()) return;
    const MysRow& r = song_.patterns[pattern].rows[size_t(row)];
    const bool porta = r.effect1 == 'P' || r.effect2 == 'P';
    if (!muted) {
        if (r.instrument) {
            if (r.instrument != v.instrument) {
                v.instrument = r.instrument;
                v.rec = r.instrument <= kMyvInstruments ? voices_.instruments[size_t(r.instrument - 1)].raw
                                                        : std::array<uint8_t, 48>{};
            }
            const int16_t pa = v.portaAcc, sa = v.slideAcc;
            v.restart();
            if (porta) {
                v.portaAcc = pa;
                v.slideAcc = sa;
            }
        }
        if (r.note == kMysNoteOffRow) {
            v.cut();
            o.cut = true;
        } else {
            o.attack = r.note >= 2 || (r.instrument && v.note);
            if (r.note >= 2) {
                if (v.note && porta) {
                    const int d = maxYmSquarePeriod(v.note) - maxYmSquarePeriod(r.note) + v.slideAcc;
                    v.portaAcc = s16(v.portaAcc + d);
                    v.slideAcc = 0;
                }
                v.note = r.note;
            }
            if (r.volume) v.volumeColumn = r.volume & 15;
        }
    }
    effect(c, r.effect1, r.param1, muted);
    effect(c, r.effect2, r.param2, muted);
}

void MaxYmPlayer::effect(int c, uint8_t fx, uint8_t p, bool muted) {
    if (!fx || fx > 'Z') return;
    if (muted && fx != 'B' && fx != 'S' && fx != 'U' && fx != 'Y' && fx != 'Z' && fx != '7') return;
    Voice& v = v_[size_t(c)];
    // An effect on the instrument's settings: the next instrument number copies the instrument again.
    auto setting = [&](int at, uint8_t value) {
        v.rec[size_t(at)] = value;
        v.instrument = 0;
    };
    // A new sequence starts from its first step.
    auto seq = [&](int at, int i) {
        setting(at, p);
        if (i >= 0) {
            v.pos[size_t(i)] = 0;
            v.done = uint8_t(v.done & ~(1 << i));
        }
    };
    switch (fx) {
    case '1': setting(0, p); break;
    case '2': setting(1, p); v.done &= uint8_t(~2); break;
    case '3': setting(2, p); v.done &= uint8_t(~4); break;
    case '4': setting(3, p); break;
    case '5': setting(4, p); v.done &= uint8_t(~0x20); break;
    case '6': setting(5, p); break;
    case '7':
        v.pulseSet = false;
        if (p == 0x80) {
            v.pwmSlideRate = 0;
            v.pwmSlideAcc = 0;
        } else {
            v.pwmSlideRate = s8(p);
        }
        break;
    case '8': seq(39, 6); break;
    case '9': setting(17, p); v.pulseSet = true; break;
    case 'A': seq(33, 1); break;
    case 'B': row_ = 63; break;
    case 'C': setting(19, p); v.synced = false; break;
    case 'D': setting(23, p); break;
    case 'E': setting(24, p); break;
    case 'F': seq(37, 5); break;
    case 'G': if (p <= 8) setting(20, p); break;
    case 'H':
        v.slideRate = s16(-s8(p));
        v.portaRate = 0;
        break;
    case 'I': seq(38, -1); break;
    case 'L': seq(32, 0); break;
    case 'M': seq(35, 3); break;
    case 'N': seq(36, 4); break;
    case 'O': v.noiseTranspose = s8(p); break;
    case 'P':
        v.portaRate = s16(v.portaAcc < 0 ? -int(p) : int(p));
        v.slideRate = 0;
        break;
    case 'Q': setting(16, p); break;
    case 'R': if (p >= 0x18 && p <= 0xCD) setting(21, p); break;
    case 'S': if (!midiClock_ && p >= 2) speed_ = p; break;
    case 'T': v.transpose = s8(p); break;
    case 'V': seq(34, 2); break;
    case 'W': setting(18, p & 15); v.synced = false; break;
    case 'X':
        v.extraArp = {uint8_t(p >> 4), uint8_t(p & 15)};
        v.extraArpStep = 0;
        v.done &= uint8_t(~2);
        break;
    case 'Y': timerMask_ = p & 7; break;
    case 'Z': sync_ = p; break;
    default: break;  // 'U' (STe microwire) and the unused letters
    }
}

void MaxYmPlayer::sequences(Voice& v) {
    if (!v.note) return;
    // The sequences step at the first of every `speed` ticks (byte 16 of the instrument; 0 or 1: each tick).
    if (v.seqTick) {
        const uint8_t t = uint8_t(v.seqTick + 1);
        v.seqTick = t >= v.rec[16] ? 0 : t;
        return;
    }
    v.seqTick = 1 >= v.rec[16] ? 0 : 1;
    // One step of sequence i: its value, its next position, stopped where the end loops on itself. A sequence off
    // (number 0, or its flag off) gives 0 and stops; its position then takes what the routine last counted (d2_), which
    // matters when an effect restarts it without resetting it.
    auto step = [&](int i, bool on, uint8_t n, uint16_t& value) {
        const uint8_t bit = uint8_t(1 << i);
        if (v.done & bit) return false;
        if (!on || !n) {
            value = 0;
            v.done |= bit;
            v.pos[size_t(i)] = uint8_t(d2_);
            return false;
        }
        const uint16_t lr = sequenceWord(n, kMyvSequenceLength);
        const uint8_t len = uint8_t(lr >> 8), rep = uint8_t(lr);
        uint8_t at = v.pos[size_t(i)];
        value = sequenceWord(n, at);
        at = uint8_t(at + 1);
        if (!(len > at)) {
            at = rep;
            if (uint8_t(len - 1) == at) v.done |= bit;
        }
        v.pos[size_t(i)] = at;
        d2_ = at;
        return true;
    };
    const auto& r = v.rec;
    step(0, true, r[32], v.vol);
    // Arpeggio, then the extra arpeggio of 'X' (0, x, y or 0, y), which keeps the sequence running.
    if (!(v.done & 2)) {
        uint16_t a = 0;
        step(1, r[1] != 0, r[33], a);
        if (v.extraArp[0] || v.extraArp[1]) {
            v.done &= uint8_t(~2);
            if (v.extraArpStep == 0) {
                v.extraArpStep = 1;
            } else {
                uint8_t shift;
                if (v.extraArpStep == 1 && v.extraArp[0] != 0xF) {
                    shift = v.extraArp[0];
                    v.extraArpStep = 2;
                } else {
                    shift = v.extraArp[1];
                    v.extraArpStep = 0;
                }
                d2_ = shift;
                a = uint16_t(a + shift);
            }
        }
        v.arp = s16(a);
    }
    uint16_t w = uint16_t(v.vib);
    step(2, r[2] != 0, r[34], w);
    v.vib = s16(w);
    step(3, true, r[35], v.mix);
    step(4, true, r[36], v.noise);
    step(5, r[4] != 0, r[37], v.fix);
    w = v.pwm;
    step(6, true, r[39], w);
    v.pwm = uint8_t(w);
}

uint8_t MaxYmPlayer::volume(Voice& v) const noexcept {
    // Sequence volume − volume column − instrument volume, as a signed byte; nothing above 0 is silence, and an
    // inaudible buzzer leaves the mixer.
    if (v.mix && v.vol) {
        const uint8_t x = uint8_t(uint8_t(v.vol) - v.volumeColumn - v.rec[22]);
        if (s8(x) > 0) return (v.mix & 0xF0) ? 0x10 : x;
    }
    v.mix &= 0xFF0F;
    return 0;
}

uint16_t MaxYmPlayer::squarePeriod(Voice& v, int bit, bool square) {
    // bit: 4 for the square, 1 for the timer effects (flags of bytes 0-5 of the instrument).
    const auto& r = v.rec;
    if (square && !v.note) return 0;
    auto on = [&](int flag) { return (r[size_t(flag)] & bit) != 0; };
    int d3 = 0;
    bool fixed = false;
    if (on(4)) {
        if (on(5) && r[37] == 0) {
            d3 = r[23] << 8 | r[24];
            fixed = true;
        } else if (v.fix != 0xFFFF) {
            d3 = v.fix;
            fixed = true;
        }
    }
    if (!fixed) {
        d3 = 0;
        if (on(5) && r[25] == 0) d3 = s8(r[23]);
        if (on(1)) d3 = s16(d3 + v.arp);
        if (on(3)) d3 = s16(d3 + v.transpose);
        const int adj = d3;
        const int idx = clampIndex(s16(adj + v.note));
        if (r[25]) {
            // Buzzer resolution: the buzzer period × 16, the coarse detune as a factor of semitones.
            uint32_t p = uint16_t(maxYmBuzzerPeriod(idx) << 4);
            if (on(5) && r[23]) {
                p = p * maxYmSemitoneScale(s8(-s8(r[23])) + 36);
                p = uint32_t(int32_t(p) >> 8);
            }
            d3 = uint16_t(p);
        } else {
            d3 = maxYmSquarePeriod(idx);
        }
        if (on(0)) {
            // Portamento and slide, scaled by the semitones of the arpeggio, transpose and detune.
            const int16_t acc = s16(v.portaAcc + v.slideAcc);
            if (acc) {
                const int16_t add = portaShift(acc, adj);
                const int sum = s16(d3 + add);
                d3 = uint16_t(sum);
                if (d3 & 0x1000) {
                    // Out of the 12 bits: 0 below, $FFF above; the slide stops.
                    d3 = sum < 0 ? 0 : 0xFFF;
                    v.slideRate = 0;
                }
            }
        }
        if (on(5)) d3 = uint16_t(d3 - s8(r[24]));
    }
    if (on(2)) d3 = uint16_t(d3 - v.vib);
    if (square && s16(d3) > 4095) d3 = 0;
    return uint16_t(d3);
}

uint16_t MaxYmPlayer::buzzerPeriod(Voice& v) const noexcept {
    const auto& r = v.rec;
    auto on = [&](int flag) { return (r[size_t(flag)] & 2) != 0; };
    // Buzzer: the square's corrections halved and rounded down to its scale (1/16 of the square's).
    auto scale = [](int x) { return int16_t((int16_t(x) >> 3) + 1) >> 1; };
    int d3 = 0;
    bool fixed = false;
    if (on(4)) {
        if (on(5) && r[37] == 0) {
            d3 = r[23] << 8 | r[24];
            fixed = true;
        } else if (v.fix != 0xFFFF) {
            d3 = v.fix;
            fixed = true;
        }
    }
    if (!fixed) {
        d3 = 0;
        if (on(5)) d3 = s8(r[23]);
        if (on(1)) d3 = s16(d3 + v.arp);
        if (on(3)) d3 = s16(d3 + v.transpose);
        const int adj = d3;
        d3 = maxYmBuzzerPeriod(clampIndex(s16(adj + v.note)));
        if (on(0)) {
            const int16_t acc = s16(v.portaAcc + v.slideAcc);
            if (acc) {
                const int16_t add = int16_t(scale(portaShift(acc, adj)));
                const int sum = int16_t(d3) + add;
                // Signed overflow upwards: the period wraps; the routine sets 0 and stops the slide.
                const bool overflow = sum > 32767 || sum < -32768;
                d3 = uint16_t(sum);
                if (overflow && s16(d3) < 0) {
                    d3 = 0;
                    v.slideRate = 0;
                }
            }
        }
        if (on(5)) d3 = uint16_t(d3 - scale(s8(r[24])));
    }
    if (on(2)) d3 = uint16_t(d3 - scale(v.vib));
    return uint16_t(d3);
}

void MaxYmPlayer::timers(int c, MaxYmTick& t) {
    Voice& v = v_[size_t(c)];
    MaxYmChannelTick& o = t.ch[size_t(c)];
    // Channels A, B and C take the MFP timers A, B and D, which the mask allows (bits 2, 1, 0).
    if (!(timerMask_ >> (2 - c) & 1)) {
        v.mix &= 0xFFF0;
        return;
    }
    // A silent channel or one without a timer effect stops its timer; the nibbles that are no effect, and a buzzer
    // effect on another channel than the buzzer's, leave it as it is.
    uint8_t& vol = buf_[size_t(8 + c)];
    uint8_t& running = timerKind_[size_t(c)];
    if (!vol || !(v.mix & 0xF)) {
        running = 0;
        return;
    }
    const uint8_t kind = uint8_t(v.mix & 0xF);
    const MaxYmTimer k = MaxYmTimer(kind);
    o.timer = kind;
    o.timerSequence = v.rec[38];
    const bool buzzer = t.buzzerChannel == c;
    auto silence = [&] {
        vol = 0;
        running = 0;
    };
    switch (k) {
    case MaxYmTimer::SyncSquare:
    case MaxYmTimer::SquareFm: {
        const uint16_t p = squarePeriod(v, 1, false);
        o.timerPeriod = p;
        if (!playable(p, 2) || toneWord_[size_t(c)] == 0) silence();
        else {
            o.timerRuns = true;
            running = kind;
            if (k == MaxYmTimer::SquareFm) {
                // FM: two interrupts per period of the timer's note, each writing the next of the steps (the
                // timer sequence's length, 8 at most): the square's period brought up by the semitones of the step's
                // low byte (bits 0-6).
                const int steps = std::min<int>(kFmSteps, uint8_t(sequenceWord(v.rec[38], kMyvSequenceLength) >> 8));
                o.timerSteps = uint8_t(steps);
                for (int i = 0; i < steps; ++i) o.timerWave[size_t(i)] = sequenceWord(v.rec[38], i);
                o.timerPrescaler = uint8_t(prescalerFor(2, p));
                o.timerData = uint16_t(timerData(p, 2, o.timerPrescaler));
            }
        }
        break;
    }
    case MaxYmTimer::Sid: {
        const uint16_t p = squarePeriod(v, 1, false);
        o.timerPeriod = p;
        const int steps = std::min<int>(kSidSteps, uint8_t(sequenceWord(v.rec[38], kMyvSequenceLength) >> 8));
        if (steps == 0 || !playable(p, steps)) silence();
        else {
            o.timerRuns = true;
            running = kind;
            o.timerSteps = uint8_t(steps);
            for (int i = 0; i < steps; ++i) o.timerWave[size_t(i)] = sequenceWord(v.rec[38], i);
            o.timerPrescaler = uint8_t(prescalerFor(steps, p));
            o.timerData = uint16_t(timerData(p, steps, o.timerPrescaler));
        }
        break;
    }
    case MaxYmTimer::PulseWidth: {
        // Two timer periods around the note's: the pulse width (instrument, slide unless '9' set it, sequence)
        // shares the period between the high and the low part, within 10 and 5102.
        const int p0 = s16(squarePeriod(v, 1, false) - kPulseTune);
        o.timerPeriod = uint16_t(p0);
        int hi = p0, lo = p0;
        int w = v.rec[17];
        if (!v.pulseSet) w = s16(w + v.pwmSlideAcc);
        w = s16(w + v.pwm);
        if (w > 0) {
            w = std::min(w, 255);
            const int d = int((uint32_t(uint16_t(p0)) * uint32_t(w)) >> 8);
            hi = s16(hi + d);
            lo = s16(lo - d);
        }
        if (5102 - hi < 0) {
            lo = s16(lo - (5102 - hi));
            hi = 5102;
        }
        if (lo - 10 < 0) {
            hi = s16(hi + (lo - 10));
            lo = 10;
        }
        auto ok = [](int x) {
            const uint16_t p = uint16_t(x);
            const int pre = prescalerFor(2, p);
            return pre != 0 && !(pre == 1 && timerData(p, 2, pre) < 24);
        };
        if (!ok(hi) || !ok(lo)) silence();
        else {
            o.timerRuns = true;
            running = kind;
        }
        break;
    }
    case MaxYmTimer::SyncBuzzer:
    case MaxYmTimer::SyncBuzzerFm:
    case MaxYmTimer::BuzzerFm: {
        if (!buzzer) break;  // only on the channel of the buzzer; the timer keeps what it played
        uint16_t p = squarePeriod(v, 1, false);
        if (k == MaxYmTimer::BuzzerFm) p = uint16_t((p + 8) & 0x7FF0);
        o.timerPeriod = p;
        if (!playable(p, 2) || (k != MaxYmTimer::SyncBuzzer && buzzWord_ == 0)) {
            silence();
            break;
        }
        o.timerRuns = true;
        running = kind;
        o.timerPrescaler = uint8_t(prescalerFor(2, p));
        o.timerData = uint16_t(timerData(p, 2, o.timerPrescaler));
        if (k != MaxYmTimer::BuzzerFm) {
            // The sync buzzer writes the shapes of its steps (low bytes of the timer sequence) into R13.
            const int steps = std::min<int>(kSyncBuzzerSteps, uint8_t(sequenceWord(v.rec[38], kMyvSequenceLength) >> 8));
            int16_t shape = steps ? int16_t(sequenceWord(v.rec[38], 0) & 15) : int16_t(-1);
            for (int i = 1; i < steps; ++i)
                if ((sequenceWord(v.rec[38], i) & 15) != shape) shape = -1;
            timerShape_[size_t(c)] = shape;
            o.timerSteps = uint8_t(steps);
            for (int i = 0; i < steps; ++i) o.timerWave[size_t(i)] = sequenceWord(v.rec[38], i);
        }
        break;
    }
    case MaxYmTimer::Digidrum:
        o.digidrum = v.rec[20];
        o.digidrumRate = v.rec[21];
        if (!v.digidrumStarted && v.rec[20] && size_t(v.rec[20]) <= voices_.digidrums.size() &&
            !voices_.digidrums[size_t(v.rec[20] - 1)].empty()) {
            v.digidrumStarted = true;
            o.digidrumStart = true;
        }
        o.timerRuns = v.digidrumStarted;
        if (o.digidrumStart) running = kind;
        if (o.timerRuns) {
            o.timerPrescaler = 1;  // divided by 4, the data of the instrument
            o.timerData = v.rec[21] ? v.rec[21] : 256;
        }
        break;
    default: break;
    }
}

void MaxYmPlayer::slides(Voice& v) noexcept {
    if (v.pwmSlideRate) {
        v.pwmSlideAcc = int16_t(std::clamp(v.pwmSlideAcc + v.pwmSlideRate, -255, 255));
    }
    if (!v.rec[0]) return;
    v.slideAcc = s16(v.slideAcc + v.slideRate);
    if (!v.portaRate) return;
    // The portamento runs down to 0 from either side, then stops.
    const int left = v.portaAcc - v.portaRate;
    v.portaAcc = s16(left);
    if (v.portaRate < 0 ? left >= 0 : left <= 0) v.portaRate = v.portaAcc = 0;
}

void MaxYmPlayer::nextStep() {
    auto entry = [&](int b) {  // byte b of the song list (4 per step)
        const MysStep& e = song_.steps[size_t(b / 4)];
        return b % 4 < 3 ? e.channel[size_t(b % 4)] : e.dma;
    };
    int s = next_;
    if (s >= song_.length) {
        s = song_.repeat;
        ++loops_;
    }
    for (int guard = 0; guard < 1024; ++guard) {
        step_ = s;
        next_ = uint8_t(s + 1);
        const MysStep& e = song_.steps[size_t(s)];
        // A step with a looping pattern ($FD) is skipped, unless it is the first or the last (the routine tests the
        // low byte of 4 × step, so steps 64, 128 and 192 count as first too).
        const bool first = uint8_t(4 * s) == 0, last = next_ == song_.length;
        const bool loop = e.channel[0] == kMysLoop || e.channel[1] == kMysLoop || e.channel[2] == kMysLoop ||
                          e.dma == kMysLoop;
        if (first || last || !loop) {
            patterns_ = {e.channel[0], e.channel[1], e.channel[2], e.dma};
            return;
        }
        int back = s;
        if (jam_) {
            // Back to the step after the previous looping pattern, or to the start; right after one, it goes on.
            back = 0;
            for (int b = 4 * s - 1; b >= 0; --b)
                if (entry(b) == kMysLoop) {
                    back = b / 4 + 1;
                    break;
                }
        }
        if (back == s) {
            s = next_;
            if (s >= song_.length) {
                s = song_.repeat;
                ++loops_;
            }
        } else {
            s = back;
            ++loops_;
        }
    }
}

MaxYmTick MaxYmPlayer::tick() {
    MaxYmTick t;
    // Since the last call, a sync buzzer timer has been writing the shapes of its steps into R13.
    for (int c = 0; c < 3; ++c)
        if (timerKind_[size_t(c)] == uint8_t(MaxYmTimer::SyncBuzzer) ||
            timerKind_[size_t(c)] == uint8_t(MaxYmTimer::SyncBuzzerFm))
            chipShape_ = timerShape_[size_t(c)];
    t.step = step_;
    t.row = row_;
    t.rowStart = counter_ == 0;
    // A pattern break ('B') moves to the last row, but the other channels still read this one.
    if (t.rowStart)
        for (int c = 0; c < 3; ++c) readRow(c, patterns_[size_t(c)], t.row, t.ch[size_t(c)]);
    t.speed = uint8_t(speed_);

    d2_ = 0;
    for (Voice& v : v_) sequences(v);
    for (int c = 0; c < 3; ++c) buf_[size_t(8 + c)] = volume(v_[size_t(c)]);

    // Mixer: square and noise of the channels that sound.
    uint8_t mixer = 0xFF;
    for (int c = 0; c < 3; ++c) {
        if (!buf_[size_t(8 + c)]) continue;
        const uint8_t m = uint8_t(v_[size_t(c)].mix >> 8);
        if (m & 0x10) mixer = uint8_t(mixer & ~(8 << c));
        if (m & 0x01) mixer = uint8_t(mixer & ~(1 << c));
    }
    buf_[7] = mixer;
    // Noise period: the last channel using noise.
    for (int c = 2; c >= 0; --c)
        if (!(mixer >> (3 + c) & 1)) {
            const Voice& v = v_[size_t(c)];
            const uint8_t n = uint8_t(uint8_t(v.noise) + v.noiseTranspose);
            buf_[6] = (n & 0x80) ? 0 : n >= 0x20 ? 0x1F : n;
            break;
        }
    // Buzzer: the last channel with a buzzer shape in its mixer.
    t.buzzerChannel = -1;
    for (int c = 2; c >= 0; --c)
        if (v_[size_t(c)].mix & 0xF0) {
            t.buzzerChannel = c;
            buzzWord_ = buzzerPeriod(v_[size_t(c)]);
            buf_[11] = uint8_t(buzzWord_);
            buf_[12] = uint8_t(buzzWord_ >> 8);
            break;
        }
    for (int c = 0; c < 3; ++c) {
        Voice& v = v_[size_t(c)];
        if (buf_[size_t(8 + c)] && !(buf_[7] >> c & 1)) {
            const uint16_t p = squarePeriod(v, 4, true);
            buf_[size_t(2 * c)] = uint8_t(p);
            buf_[size_t(2 * c + 1)] = uint8_t(p >> 8);
            toneWord_[size_t(c)] = p;
            if (!p) buf_[7] = uint8_t(buf_[7] | 1 << c);  // a period of 0 would whistle: the square is muted
        }
        timers(c, t);
    }

    // Writes to the chip.
    static constexpr uint8_t kMask[14] = {255, 15, 255, 15, 255, 15, 31, 255, 31, 31, 31, 255, 255, 15};
    auto write = [&](int r, uint8_t value) {
        chip_[size_t(r)] = uint8_t(value & kMask[r]);
        t.written = uint16_t(t.written | 1 << r);
    };
    for (int c = 0; c < 3; ++c) {
        const Voice& v = v_[size_t(c)];
        const uint8_t kind = uint8_t(v.mix & 0xF);
        if (v.rec[19] && !v.synced) {
            write(2 * c + 1, 0);
            write(2 * c, kFreqReset);
            t.ch[size_t(c)].startSync = true;
        } else if (kind == uint8_t(MaxYmTimer::SquareFm) || kind == uint8_t(MaxYmTimer::SyncSquare)) {
            continue;
        }
        write(2 * c, buf_[size_t(2 * c)]);
        write(2 * c + 1, buf_[size_t(2 * c + 1)]);
    }
    write(6, buf_[6]);
    write(7, buf_[7]);
    for (int c = 0; c < 3; ++c) {
        const uint8_t kind = uint8_t(v_[size_t(c)].mix & 0xF);
        const bool timerVolume = kind == uint8_t(MaxYmTimer::Digidrum) || kind == uint8_t(MaxYmTimer::Sid) ||
                                 kind == uint8_t(MaxYmTimer::PulseWidth);
        if (!buf_[size_t(8 + c)] || !timerVolume) write(8 + c, buf_[size_t(8 + c)]);
    }
    if (t.buzzerChannel >= 0) {
        const Voice& v = v_[size_t(t.buzzerChannel)];
        const uint8_t kind = uint8_t(v.mix & 0xF);
        if (kind != uint8_t(MaxYmTimer::BuzzerFm) && kind != uint8_t(MaxYmTimer::SyncBuzzerFm)) {
            write(11, buf_[11]);
            write(12, buf_[12]);
        }
        if (kind != uint8_t(MaxYmTimer::SyncBuzzer) && kind != uint8_t(MaxYmTimer::SyncBuzzerFm)) {
            // Shape: that of the instrument (mixer nibble 1) or the nibble itself; written when it differs from the
            // chip's, or for a start sync not done yet.
            const uint8_t nibble = uint8_t((v.mix & 0xF0) >> 4);
            const uint8_t shape = nibble == 1 ? v.rec[18] : nibble;
            if (shape != chipShape_ || (!v.synced && v.rec[19])) {
                write(13, shape);
                chipShape_ = shape;
                t.shapeWritten = true;
            }
        }
    }
    for (Voice& v : v_) v.synced = true;
    for (Voice& v : v_) slides(v);

    for (int c = 0; c < 3; ++c) {
        MaxYmChannelTick& o = t.ch[size_t(c)];
        o.note = v_[size_t(c)].note;
        o.instrument = v_[size_t(c)].instrument;
        o.volume = buf_[size_t(8 + c)];
    }
    t.reg = chip_;
    std::copy(buf_.begin(), buf_.end(), t.buffer.begin());
    t.sync = sync_;

    // Next row, next step.
    if (++counter_ >= uint8_t(speed_)) {
        counter_ = 0;
        if (++row_ == kMysRows) {
            row_ = 0;
            nextStep();
        }
    }
    return t;
}

}  // namespace stym::ymio
