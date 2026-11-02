// STYM2149 — ymio: TTrak modules played tick by tick
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The arithmetic follows the routine's 68000 operations: bytes and words wrap, and the comparisons the routine makes
// signed are made signed here.
#include "ymio/TTrakPlayer.h"

#include <algorithm>
#include <cmath>

namespace stym::ymio {

namespace {

using I = TtrakInstrument;

constexpr std::array<int, 7> kPrescalers{4, 10, 16, 50, 64, 100, 200};
constexpr std::array<uint32_t, 4> kFmFactors{0x8000, 0x4000, 0x2AAA, 0x5555};  // ×1, ×1/2, ×1/3, ×2/3 (of 65 536 / 2)
const TtrakInstrument kSilent{};  // a note before any instrument

TtrakPlayer::Tables makeTables() {
    TtrakPlayer::Tables t;
    for (int i = 0; i < kTtrakPitches; ++i) {
        t.period[size_t(i)] = uint16_t(std::lround(kTtrakPeriod0 * std::pow(2.0, -i / 192.0)));
        // Two interrupts per period of the pitch (A4 = 440 Hz on note 46) from the MFP's 2.4576 MHz.
        const double rate = 880.0 * std::pow(2.0, (i / 16.0 - 46.0) / 12.0);
        size_t k = 0;
        double data = 0;
        for (; k < kPrescalers.size(); ++k) {
            data = 2457600.0 / (kPrescalers[k] * rate);
            if (data <= 256.0 || k + 1 == kPrescalers.size()) break;
        }
        t.mfpData[size_t(i)] = uint8_t(std::lround(data) & 0xFF);
        t.mfpPrescaler[size_t(i)] = uint8_t(k + 1);
    }
    // Wow: a slow walk of the pitch around level 8, each level held a few ticks to a few seconds.
    uint32_t seed = 0x2149;
    int level = 8;
    for (int i = 0; i < 72; ++i) {
        seed = seed * 1103515245u + 12345u;
        const int move = int(seed >> 16) % 3 - 1;
        level = std::clamp(level + (level < 5 ? 1 : level > 11 ? -1 : move), 1, 15);
        t.wow.push_back(uint8_t(level));
        t.wow.push_back(uint8_t(2 + (seed >> 24) % 96));
    }
    t.wow.push_back(0xFF);
    return t;
}

// What the interrupt of a SID writes for volume + level: max(0, volume + level − 15), up to 30.
uint8_t sidLevel(int i) noexcept { return uint8_t(i >= 16 && i <= 30 ? i - 15 : 0); }

uint8_t at(const TtrakInstrument& r, int offset) noexcept {
    return offset >= 0 && offset < int(kTtrakInstrumentSize) ? r.raw[size_t(offset)] : 0;
}
uint16_t wordAt(const TtrakInstrument& r, int offset) noexcept {
    return uint16_t(at(r, offset) << 8 | at(r, offset + 1));
}
bool any4(const TtrakInstrument& r, int offset) noexcept {
    return r.raw[size_t(offset)] | r.raw[size_t(offset) + 1] | r.raw[size_t(offset) + 2] | r.raw[size_t(offset) + 3];
}
// Where a table of the record starts (its word in the record's head).
int tableAt(const TtrakInstrument& r, int word) noexcept { return int(int16_t(r.word(word))); }

}  // namespace

const TtrakPlayer::Tables& TtrakPlayer::defaultTables() {
    static const Tables t = makeTables();
    return t;
}

TtrakPlayer::TtrakPlayer(const TtrakModule& module) : m_(module), tables_(&defaultTables()) {
    counter_ = int8_t(uint8_t(speed_ + m_.groove[0]));
    groove_ = m_.groove[0];
    vibDivider_ = uint8_t((m_.rate ? m_.rate : 50) / 50 - 1);
    buf_[7] = 0xFF;
}

const TtrakInstrument& TtrakPlayer::rec(const Channel& c) const noexcept {
    return c.instrument >= 1 && c.instrument <= kTtrakInstruments ? m_.instruments[size_t(c.instrument - 1)] : kSilent;
}

TtrakTick TtrakPlayer::tick() {
    TtrakTick t;
    t.position = pos_;
    t.row = row_;
    if (readRow_) {
        readRow_ = false;
        t.rowStart = true;
        const uint8_t p = m_.order[size_t(pos_)];
        static const TtrakPattern kEmpty{};
        const TtrakPattern& pat = p < m_.patterns.size() ? m_.patterns[p] : kEmpty;
        for (int c = 0; c < 3; ++c) readRow(c, pat.rows[size_t(row_ & 63)][size_t(c)], t);
    }
    counter_ = int8_t(counter_ - 1);
    if (counter_ < 0) {
        counter_ = int8_t(speed_);
        readRow_ = true;
        row_ = (row_ + 1) & 0xFF;
        // A break wins over a jump, which waits for the next row's end then.
        int target = -1;
        if (break_) {
            break_ = false;
            target = (pos_ + 1) & 0xFF;
        } else if (jump_) {
            jump_ = false;
            target = jumpTo_;
        } else if (int8_t(row_) >= 64) {
            target = (pos_ + 1) & 0xFF;
        }
        if (target >= 0) {
            row_ = 0;
            if (int8_t(target) >= int8_t(m_.length)) target = m_.restart;
            if (target <= pos_) ++loops_;
            pos_ = target;
        }
        groove_ = m_.groove[size_t(row_ & 15)];
        counter_ = int8_t(counter_ + groove_);
    }
    mixer_ = 0xF8;
    for (int c = 0; c < 3; ++c) channelTick(c, t);
    buf_[7] = mixer_;
    decay_ = int8_t(decay_ - 1);
    if (decay_ <= 0) {
        decay_ = 3;
        const auto level = int8_t(sidechain_ - 1);
        sidechain_ = uint8_t(level < 0 ? 0 : level);
    }
    // The writes, in the routine's order.
    auto write = [&](int r, uint8_t v) {
        static constexpr std::array<uint8_t, 14> kMask{0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F,
                                                       0xFF, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F};
        chip_[size_t(r)] = v & kMask[size_t(r)];
        t.written |= uint16_t(1u << r);
    };
    for (int c = 0; c < 3; ++c) {
        const Channel& v = ch_[size_t(c)];
        if (sync_[size_t(c)]) {
            write(2 * c, 0);
            write(2 * c + 1, 0);
        }
        if (!(v.step & 0x80) || (v.type != 6 && v.type != 8)) {
            write(2 * c, buf_[size_t(2 * c)]);
            write(2 * c + 1, buf_[size_t(2 * c + 1)]);
        }
    }
    write(6, buf_[6]);
    write(7, buf_[7]);
    for (int c = 0; c < 3; ++c) {
        const Channel& v = ch_[size_t(c)];
        if (!(v.step & 0x80) || (v.type >= 5 && v.type != 7)) write(8 + c, buf_[size_t(8 + c)]);
    }
    write(11, buf_[11]);
    write(12, buf_[12]);
    sync_ = {};
    t.buffer = buf_;
    t.reg = chip_;
    t.speed = speed_;
    t.sidechain = sidechain_;
    for (int c = 0; c < 3; ++c) {
        const Channel& v = ch_[size_t(c)];
        TtrakChannelTick& o = t.ch[size_t(c)];
        o.note = v.note;
        o.instrument = uint8_t(v.instrument);
        o.timer = v.type;
        o.mfpTimer = v.mfpTimer;
        o.timerRuns = v.mfpTimer && v.timerOn && running_[size_t(c)];
        o.step = v.step;
    }
    return t;
}

void TtrakPlayer::readRow(int c, const TtrakCell& cell, TtrakTick& t) {
    Channel& v = ch_[size_t(c)];
    if (cell.note) {
        v.note = cell.note;
        v.vibDepth = 0;
        v.vibSpeed = v.vibOffset = v.vibCount = 0;
        v.slide = v.slideRate = 0;
        v.porta = false;
        t.ch[size_t(c)].noteRow = true;
    }
    if (cell.instrument) t.ch[size_t(c)].instrumentRow = true;
    const uint8_t high = cell.instrument ? trigger(c, cell.instrument, t) : 0;
    v.effect = cell.effect;
    if (cell.effect) {
        v.param = cell.param;
        effect(c, high);
    }
    v.linked = false;
}

uint32_t TtrakPlayer::sampleOffset(int slot) const noexcept {
    // The long that the routine reads at $82 + 4 × slot of the module: the slot's offset in the sample block (past
    // the table, slots 33 to 255 read the order list and the instruments, as the module holds them).
    auto offsetOf = [&](int s) {
        uint32_t o = 0;
        for (int k = 0; k < s && k < kTtrakSampleSlots; ++k)
            o += uint32_t(std::max<size_t>(1, m_.samples[size_t(k)].size()));
        return o;
    };
    auto byteAt = [&](int off) -> uint8_t {
        if (off < 0x106) return uint8_t(offsetOf((off - 0x82) / 4) >> (24 - 8 * ((off - 0x82) % 4)));
        if (off < 0x206) return m_.order[size_t(off - 0x106)];
        const int k = (off - 0x206) / int(kTtrakInstrumentSize), b = (off - 0x206) % int(kTtrakInstrumentSize);
        return k < kTtrakInstruments ? m_.instruments[size_t(k)].raw[size_t(b)] : 0;
    };
    const int at = 0x82 + 4 * slot;
    return uint32_t(byteAt(at)) << 24 | uint32_t(byteAt(at + 1)) << 16 | uint32_t(byteAt(at + 2)) << 8 | byteAt(at + 3);
}

uint8_t TtrakPlayer::trigger(int c, uint8_t instrument, TtrakTick& t) {
    if (instrument < 1 || instrument > kTtrakInstruments) return 0;  // the routine reads past its table
    Channel& v = ch_[size_t(c)];
    TtrakChannelTick& o = t.ch[size_t(c)];
    v.instrument = instrument;
    const TtrakInstrument& r = rec(v);
    o.attack = true;
    v.atten = r.at(I::kAttenuation);
    v.type = r.at(I::kTimerType);
    v.shape = r.at(I::kBuzzerShape);
    v.wow = r.at(I::kWowDepth);
    v.hardSync = r.at(I::kHardSync);
    v.link = r.at(I::kLink);
    v.detune = r.word(I::kDetune);
    v.fixed = r.word(I::kFixedPitch);
    v.fmSpeed = r.at(I::kFmSpeed);
    v.fmDepth = r.at(I::kFmDepth);
    v.fmValue = r.word(I::kFmValue);
    v.fmPos = r.word(I::kFmPosition);
    v.morphSpeed = r.at(I::kMorphSpeed);
    v.morphValue = r.word(I::kMorphValue);
    v.morphPos = r.word(I::kMorphPosition);
    v.ampCount = int8_t(r.at(I::kAmpSpeed));
    v.ampSpeed = r.at(I::kAmpSpeed);
    v.ampPos = 0;
    v.ampEnd = uint8_t(r.at(I::kAmpLast) + 1);
    v.ampLoop = r.at(I::kAmpLoop);
    const int arp = tableAt(r, I::kArpTableAt);
    v.arpCount = int8_t(at(r, arp));
    v.arpSpeed = at(r, arp);
    for (int i = 0; i < 16; ++i) v.arp[size_t(i)] = at(r, arp + i);
    v.arpPos = 1;
    const int noise = tableAt(r, I::kNoiseTableAt);
    v.noiseCount = int8_t(at(r, noise));
    v.noiseSpeed = at(r, noise);
    v.noisePos = noise + 1;
    if (v.hardSync) {
        sync_[size_t(c)] = true;
        o.startSync = true;
    }
    auto shape = [&](uint8_t s) {
        chip_[13] = s & 0x0F;
        if (t.shapes < t.shape.size()) t.shape[t.shapes++] = s;
    };
    if (v.type == 5) {
        lastShape_ = 0;
        shape(0x0F);
    } else if (v.shape && (v.hardSync || v.shape != lastShape_)) {
        lastShape_ = v.shape;
        shape(v.shape);
    }
    if (v.type) {
        takeTimer(c);
        v.mfpPitch = 0;
        running_[size_t(c)] = false;  // stopped until the routine sets the MFP
        if (v.type == 7) {
            o.sampleSlot = r.at(I::kSampleSlot);
            o.sampleStart = true;
        }
        v.timerOn = true;
    } else if (v.timerOn) {
        dropTimer(c);
    }
    const uint8_t send = r.at(I::kSidechainSend);
    if (send && int8_t(send) >= int8_t(sidechain_)) sidechain_ = send;
    return v.type == 7 ? uint8_t(sampleOffset(r.at(I::kSampleSlot)) >> 8) : 0;
}

void TtrakPlayer::effect(int c, uint8_t high) {
    Channel& v = ch_[size_t(c)];
    const uint8_t p = v.param;
    switch (v.effect) {
    case 0x01: v.slideRate = int8_t(p); break;
    case 0x02: v.slideRate = int8_t(uint8_t(-p)); break;
    case 0x03: v.slideRate = int16_t(p); break;
    case 0x04:
        v.vibSpeed = int8_t(((high & 15) << 4) | p >> 4);  // a word shifted where the routine masked a byte
        v.vibOffset = int8_t((p & 15) * 4);
        v.vibDepth = uint8_t((p & 15) * 8);
        v.vibCount = 0;
        break;
    case 0x07: v.fmSpeed = p; break;
    case 0x08: v.fmDepth = p; break;
    case 0x0A:
        v.arp[1] = 0;
        v.arp[2] = p >> 4;
        v.arp[3] = p & 15;
        break;
    case 0x0B: break_ = true; break;
    case 0x0D: v.detune = uint16_t(p >> 3); break;
    case 0x0F: v.fixed = uint16_t(p << 2); break;
    case 0x12: v.ampCount = int8_t(p), v.ampSpeed = p; break;
    case 0x13:
        jump_ = true;
        jumpTo_ = p;
        break;
    case 0x14:
        if (int8_t(p & 15) >= int8_t(sidechain_)) sidechain_ = p & 15;
        break;
    case 0x15: v.link = p; break;
    case 0x1C:
        speed_ = p;
        counter_ = int8_t(uint8_t(p + groove_));
        v.effect = 0;
        break;
    case 0x1F: v.atten = p; break;
    case 0x20: v.wow = p; break;
    default: break;
    }
}

void TtrakPlayer::takeTimer(int c) {
    Channel& v = ch_[size_t(c)];
    if (v.mfpTimer) return;
    // The free timer (A when both are), or the one not taken last.
    const bool freeA = !(timersHeld_ & 2), freeD = !(timersHeld_ & 4);
    const uint8_t take = freeA ? 1 : freeD ? 2 : (timersHeld_ & 1) ? 1 : 2;
    timersHeld_ = uint8_t((timersHeld_ & 6) | (take == 1 ? 2 : 5));
    for (int w = 0; w < 3; ++w) {
        Channel& o = ch_[size_t(w)];
        if (o.mfpTimer != take) continue;
        o.mfpTimer = 0;
        o.mfpPitch = 0;
        o.timerOn = false;
        running_[size_t(w)] = false;
    }
    v.mfpTimer = take;
    v.mfpPitch = 0;
    v.timerOn = true;
}

void TtrakPlayer::dropTimer(int c) {
    Channel& v = ch_[size_t(c)];
    if (v.mfpTimer) timersHeld_ = uint8_t(timersHeld_ & ~(1u << v.mfpTimer));
    v.mfpTimer = 0;
    v.mfpPitch = 0;
    v.timerOn = false;
    running_[size_t(c)] = false;
}

void TtrakPlayer::fmStep(Channel& v) const noexcept {
    const TtrakInstrument& r = rec(v);
    const int wave = tableAt(r, I::kFmWaveAt);
    auto pos = int16_t(v.fmPos + v.fmSpeed * 2);
    const auto end = int16_t(r.word(I::kFmEnd));
    if (pos < end || r.at(I::kFmLoopOn)) {
        if (pos >= end) pos = int16_t(r.word(I::kFmLoop));
        v.fmPos = uint16_t(pos);
        v.fmValue = wordAt(r, wave + pos);
    } else {
        pos = int16_t(end - 2);
        v.fmPos = uint16_t(pos);
        v.fmValue = uint16_t((int32_t(int16_t(wordAt(r, wave + pos))) * v.fmDepth) >> 8);
    }
}

void TtrakPlayer::morphStep(Channel& v) const noexcept {
    const TtrakInstrument& r = rec(v);
    const int env = tableAt(r, I::kMorphEnvAt);
    auto pos = int16_t(v.morphPos + v.morphSpeed);
    const auto end = int16_t(r.word(I::kMorphEnd));
    if (pos < end || r.at(I::kMorphLoopOn)) {
        if (pos >= end) pos = int16_t(r.word(I::kMorphLoop));
        v.morphPos = uint16_t(pos);
        v.morphValue = uint16_t((v.morphValue & 0xFF00) | at(r, env + pos));
    } else {
        pos = int16_t(end - 1);
        v.morphPos = uint16_t(pos);
        v.morphValue = uint16_t((uint16_t(pos) & 0xFF00) | at(r, env + pos));
    }
}

void TtrakPlayer::channelTick(int c, TtrakTick& t) {
    Channel& v = ch_[size_t(c)];
    TtrakChannelTick& o = t.ch[size_t(c)];
    if (!v.note) {
        buf_[size_t(8 + c)] = 0;
        buf_[size_t(2 * c)] = buf_[size_t(2 * c + 1)] = 0;
        return;
    }
    // Amplitude envelope; at its end, the linked instrument takes over within the same tick (once).
    uint8_t vol = 0;
    for (;;) {
        const TtrakInstrument& r = rec(v);
        v.step = at(r, I::kAmpEnv + v.ampPos);
        vol = v.step & 0x1F;
        v.ampCount = int8_t(v.ampCount - 1);
        if (v.ampCount >= 0) break;
        ++v.ampPos;
        v.ampCount = int8_t(v.ampSpeed);
        if (v.ampEnd > v.ampPos) break;
        v.ampPos = v.ampLoop;
        if (!v.link || v.linked) break;
        v.linked = true;
        trigger(c, v.link, t);
        if (v.effect) effect(c);
    }
    v.linked = false;
    const TtrakInstrument& r = rec(v);
    if (r.at(I::kSidechainDuck)) vol = uint8_t(vol - sidechain_);
    vol = uint8_t(vol - v.atten);
    if (int8_t(vol) < 0) vol = 0;
    if (v.step & 0x20) mixer_ = uint8_t(mixer_ | (1u << c));
    else mixer_ = uint8_t(mixer_ & ~(1u << c));
    // Noise table ($Fx: back to step x).
    const int noise = tableAt(r, I::kNoiseTableAt) + 1;
    uint8_t n = at(r, v.noisePos);
    if (n & 0x80) {
        v.noisePos = noise + (n & 15);
        n = at(r, v.noisePos);
    }
    v.noiseCount = int8_t(v.noiseCount - 1);
    if (v.noiseCount < 0) {
        v.noiseCount = int8_t(v.noiseSpeed);
        ++v.noisePos;
    }
    if (!(n & 0x20) && (v.step & 0x40)) {
        mixer_ = uint8_t(mixer_ & ~(1u << (3 + c)));
        buf_[6] = n;
    } else {
        mixer_ = uint8_t(mixer_ | (1u << (3 + c)));
    }
    // Pitches of the square (p2), buzzer (p4) and timer (p5).
    auto arpAt = [&](int i) { return i >= 0 && i < 16 ? v.arp[size_t(i)] : uint8_t(0); };
    uint8_t a = arpAt(v.arpPos);
    if (a & 0x80) {
        v.arpPos = 1 + (a & 15);
        a = arpAt(v.arpPos);
    }
    v.arpCount = int8_t(v.arpCount - 1);
    if (v.arpCount < 0) {
        v.arpCount = int8_t(v.arpSpeed);
        ++v.arpPos;
    }
    uint8_t n2 = v.note, n4 = v.note, n5 = v.note;
    if (r.at(I::kMatrixArp)) n2 = uint8_t(n2 + a);
    if (r.at(I::kMatrixArp + 1)) n4 = uint8_t(n4 + a);
    if (r.at(I::kMatrixArp + 2)) n5 = uint8_t(n5 + a);
    auto p2 = uint16_t((n2 & 0x7F) << 4), p4 = uint16_t((n4 & 0x7F) << 4), p5 = uint16_t((n5 & 0x7F) << 4);
    if (r.at(I::kMatrixFixed)) p2 = v.fixed;
    if (r.at(I::kMatrixFixed + 1)) p4 = v.fixed;
    if (r.at(I::kMatrixFixed + 2)) p5 = v.fixed;
    if (r.at(I::kMatrixDetune)) p2 = uint16_t(p2 + v.detune);
    if (r.at(I::kMatrixDetune + 2)) p5 = uint16_t(p5 + v.detune);
    if (r.at(I::kMatrixDetune + 1)) p4 = uint16_t(p4 + v.detune);
    if (any4(r, I::kMatrixSlide)) {
        // Slides (effects 1 and 2 add their rate while they are on the row), portamento (effect 3: from the last
        // square pitch to the note, by its rate), then the vibrato (effect 4).
        int16_t s = v.slide;
        bool porta = v.porta;
        if (!porta) {
            const auto fx = int8_t(v.effect);
            if (fx == 3) porta = v.porta = true;
            else if (fx >= 1 && fx < 3) v.slide = s = int16_t(s + v.slideRate);
        }
        if (porta) {
            s = int16_t(v.lastPitch - int16_t(p2));
            if (s >= 0) {
                if (s > v.slideRate) s = int16_t(s - v.slideRate);
                else s = 0, v.porta = false;
            } else {
                if (int16_t(-s) > v.slideRate) s = int16_t(s + v.slideRate);
                else s = 0, v.porta = false;
            }
        }
        if (r.at(I::kMatrixSlide)) p2 = uint16_t(p2 + s);
        if (r.at(I::kMatrixSlide + 1)) p4 = uint16_t(p4 + s);
        if (r.at(I::kMatrixSlide + 2)) p5 = uint16_t(p5 + s);
        v.lastPitch = int16_t(p2);
        int8_t off = v.vibOffset;
        v.vibCount = int8_t(v.vibCount - 1);
        if (v.vibCount < 0) {
            v.vibCount = int8_t(vibDivider_);
            off = int8_t(off + v.vibSpeed);
            if (off < 0) {
                v.vibSpeed = int8_t(-v.vibSpeed);
                off = 0;
            }
            if (off >= int8_t(v.vibDepth)) {
                v.vibSpeed = int8_t(-v.vibSpeed);
                off = int8_t(v.vibDepth);
            }
            v.vibOffset = off;
        }
        const int vib = off - (uint16_t(int16_t(int8_t(v.vibDepth))) >> 1);
        if (r.at(I::kMatrixSlide)) p2 = uint16_t(p2 + vib);
        if (r.at(I::kMatrixSlide + 1)) p4 = uint16_t(p4 + vib);
        if (r.at(I::kMatrixSlide + 2)) p5 = uint16_t(p5 + vib);
    }
    if (any4(r, I::kMatrixFm)) {
        const auto f = int16_t((int32_t(int16_t(v.fmValue)) * v.fmDepth) >> 8);
        if (r.at(I::kMatrixFm)) p2 = uint16_t(p2 + f);
        if (r.at(I::kMatrixFm + 1)) p4 = uint16_t(p4 + f);
        if (r.at(I::kMatrixFm + 2)) p5 = uint16_t(p5 + f);
        fmStep(v);
    }
    if (v.wow) {
        const std::vector<uint8_t>& w = tables_->wow;
        uint8_t level = v.wowValue;
        v.wowCount = int8_t(v.wowCount - 1);
        if (v.wowCount < 0) {
            if (v.wowIndex + 1u >= w.size() || (w[v.wowIndex] & 0x80)) v.wowIndex = 0;
            if (w.size() >= 2 && !(w[0] & 0x80)) {
                level = v.wowValue = w[v.wowIndex];
                v.wowCount = int8_t(w[v.wowIndex + 1u]);
                v.wowIndex = uint16_t(v.wowIndex + 2);
            } else {
                level = v.wowValue = 8;
            }
        }
        const auto k = int16_t(uint16_t(uint16_t((int8_t(level) - 8) * v.wow) << 4));
        const auto off = int16_t((int32_t(int16_t(p2)) * k) >> 16);
        if (r.at(I::kMatrixWow)) p2 = uint16_t(p2 + off);
        if (r.at(I::kMatrixWow + 1)) p4 = uint16_t(p4 + off);
        if (r.at(I::kMatrixWow + 2)) p5 = uint16_t(p5 + off);
    }
    for (uint16_t* p : {&p2, &p4, &p5}) {
        if (int16_t(*p) >= kTtrakPitches - 1) *p = uint16_t(kTtrakPitches - 1);
        if (int16_t(*p) < 0) *p = 0;
    }
    uint16_t period = tables_->period[p2];
    if (r.at(I::kYmPad)) period = 0;
    if (v.step & 0x10) {
        buf_[11] = uint8_t(((tables_->period[p4] >> 3) + 1) >> 1);
        buf_[12] = 0;
    }
    o.volume = vol;
    o.squarePitch = p2;
    o.buzzerPitch = p4;
    o.timerPitch = p5;
    if (v.type == 4) morphStep(v);
    buf_[size_t(8 + c)] = vol;
    buf_[size_t(2 * c)] = uint8_t(period);
    buf_[size_t(2 * c + 1)] = uint8_t(period >> 8);
    if (!v.type || !v.mfpTimer) return;
    if (!(v.step & 0x80)) {
        v.mfpPitch = 0;
        running_[size_t(c)] = false;
        return;
    }
    // The interrupt's wave for this tick.
    const int timerAt = tableAt(r, I::kTimerTableAt);
    auto table = [&](int i) { return at(r, timerAt + i); };
    switch (v.type) {
    case 1:
        o.timerSteps = 2;
        o.timerWave = {15, 0};
        o.timerOut = {vol, 0};
        break;
    case 2:
    case 3:
        o.timerSteps = v.type == 2 ? 2 : 16;
        for (int i = 0; i < o.timerSteps; ++i) {
            o.timerWave[size_t(i)] = table(i);
            o.timerOut[size_t(i)] = sidLevel(vol + table(i));
        }
        break;
    case 4: {
        o.timerSteps = 16;
        const int waves = tableAt(r, I::kMorphWavesAt);
        const auto m = int16_t(uint16_t(v.morphValue << 7));
        for (int i = 0; i < 16; ++i) {
            const int x = at(r, waves + i), y = at(r, waves + 16 + i);
            const auto step = int16_t(x + int16_t(uint32_t(int32_t(int16_t(y - x)) * m * 2) >> 16));
            o.timerWave[size_t(i)] = uint8_t(std::clamp<int>(step, 0, 15));
            o.timerOut[size_t(i)] = sidLevel(vol + step);
        }
        break;
    }
    case 5:
        o.timerSteps = 4;
        for (int i = 0; i < 4; ++i) {
            o.timerWave[size_t(i)] = table(i);
            o.timerOut[size_t(i)] = table(i);
        }
        break;
    case 6:
    case 8:
        o.timerSteps = 4;
        for (int i = 0; i < 4; ++i) {
            o.timerWave[size_t(i)] = table(i) & 3;
            o.timerOut[size_t(i)] = uint16_t((uint32_t(period) * kFmFactors[table(i) & 3] * 2) >> 16);
        }
        break;
    default: break;
    }
    o.sampleSlot = v.type == 7 ? r.at(I::kSampleSlot) : o.sampleSlot;
    if (p5 != v.mfpPitch) {
        v.mfpPitch = p5;
        o.mfpWritten = true;
        running_[size_t(c)] = true;
    }
    o.timerPrescaler = tables_->mfpPrescaler[v.mfpPitch];
    o.timerData = tables_->mfpData[v.mfpPitch];
}

}  // namespace stym::ymio
