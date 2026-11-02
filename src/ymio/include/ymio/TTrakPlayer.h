// STYM2149 — ymio: TTrak modules played tick by tick
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// What TTrak's replay routine does at each of its calls, for the three YM channels, written from how the routine of
// TTrak 1.04 behaves (studied and run in the emulated Atari to compare, for interoperability; none of its code or data
// is reused here). The period and MFP tables are computed by formulas that give the routine's values; the "wow" walk is
// our own (setTables() takes another one, to compare with the routine).
//
// Each tick:
// - the rows of channels A, B and C are read at the first tick of a row: a note becomes the channel's note (and clears
//   its vibrato, slide and portamento); an instrument number copies the instrument's settings and restarts its tables
//   (the buzzer shape is written to R13 when it is set and differs from the last one written, or always with hard
//   sync; a sync buzzer instrument writes shape $F; an instrument with a timer takes one of the MFP's timers A and D,
//   the one taken longest ago when both are held); then the effect;
// - for each channel with a note: the amplitude envelope (one step per `speed + 1` ticks, looping, or handing over to
//   the linked instrument at its end) gives the volume (bits 0-4), the timer (bit 7), the noise (bit 6) and the square
//   (bit 5 off) and the buzzer (bit 4); the volume loses the attenuation, and the sidechain level when the instrument
//   ducks; the noise table gives R6; the pitch (1/16 semitones) of the square, the buzzer and the timer adds what the
//   instrument's matrix sends to each of them: arpeggio, fixed pitch, detune, slides and vibrato, frequency
//   modulation, wow; the square and buzzer periods come from the period table (the buzzer period is the square's / 16);
// - the timer of a channel whose step has bit 7 set plays its wave (SID of 2 or 16 volumes, morph, sync buzzer shapes,
//   tone FM) at the rate of the MFP table for the timer pitch; a digidrum plays its sample slot from the note on;
// - the registers are written, except the tone of an FM timer and the volume of a SID or digidrum timer that plays;
// - the song moves on: `speed + groove[row & 15] + 1` ticks per row, 64 rows per pattern, a pattern break or a
//   position jump ending the row; the song restarts at its restart position after its length.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "ymio/TTrak.h"

namespace stym::ymio {

inline constexpr int kTtrakPitches = 0x691;  // pitches 0 to $690 (1/16 semitone; 16 × note)
// Square period of pitch 0 at the 2 MHz clock: pitch i plays period round(kTtrakPeriod0 × 2^(−i/192)), so that A4
// (note 46) is about 438.8 Hz.
inline constexpr double kTtrakPeriod0 = 4060.29;

// Timer types of TTrak instruments (byte $0D of the record).
enum class TtrakTimer : uint8_t {
    None = 0,
    Sid = 1,        // square SID: the volume, then 0
    Sid2 = 2,       // two volume levels
    Sid16 = 3,      // sixteen volume levels
    Morph = 4,      // sixteen volume levels morphing between two waves
    SyncBuzzer = 5, // four R13 shapes
    Fm = 6,         // four tone periods (multiples of the square's)
    Digidrum = 7,   // the sample slot
    FmSync = 8,     // four tone periods, other interrupt
};

struct TtrakChannelTick {
    uint8_t note = 0;          // current note (0: none, the channel is silent); MIDI note + 23
    bool noteRow = false;      // a row gave a note at this tick
    bool instrumentRow = false;  // a row gave an instrument at this tick (which restarts it)
    bool attack = false;       // an instrument was triggered at this tick (by a row or by a link)
    uint8_t instrument = 0;    // instrument last triggered (1-32; 0: none)
    uint8_t volume = 0;        // volume computed (bit 4: buzzer), before any timer
    uint8_t step = 0;          // amplitude envelope byte of this tick
    uint16_t squarePitch = 0, buzzerPitch = 0, timerPitch = 0;  // 1/16 semitones, 0-$690
    uint8_t timer = 0;         // TtrakTimer of the instrument
    uint8_t mfpTimer = 0;      // MFP timer held: 1 = A, 2 = D, 0 = none
    bool timerRuns = false;    // the timer plays at this tick (step bit 7 and a timer held)
    bool mfpWritten = false;   // the MFP was set at this tick (timer pitch changed or timer restarted)
    // MFP setting for timerPitch (prescaler 1-7: ÷4, 10, 16, 50, 64, 100, 200; data 0 counts 256).
    uint8_t timerPrescaler = 0, timerData = 0;
    // The wave of the interrupt, one step per interrupt: SID and morph the volume of each step at volume 15 (the step
    // plays max(0, volume + level − 15)), sync buzzer the R13 shapes, FM the multiplier (0 ×1, 1 ×½, 2 ×⅓, 3 ×⅔).
    uint8_t timerSteps = 0;
    std::array<uint8_t, 16> timerWave{};
    std::array<uint16_t, 16> timerOut{};  // what each step writes: SID volume, R13 shape, tone period
    uint8_t sampleSlot = 0;    // digidrum: sample slot (0-31)
    bool sampleStart = false;  // digidrum: the sample starts from its first byte at this tick
    bool startSync = false;    // hard sync: tone period 0 written before the tone
};

struct TtrakTick {
    int position = 0, row = 0;  // song position and row of this tick
    bool rowStart = false;      // the rows were read at this tick
    uint8_t speed = 20;
    std::array<uint8_t, 14> reg{};     // R0-R13 as the chip holds them after the routine's writes
    std::array<uint8_t, 13> buffer{};  // R0-R12 as the routine computed them (written or not)
    uint16_t written = 0;              // registers written by the routine (bit r)
    uint8_t shapes = 0;                // R13 writes at this tick (one per triggered instrument at most)
    std::array<uint8_t, 3> shape{};    // their values, in order
    uint8_t sidechain = 0;             // sidechain level after the tick
    std::array<TtrakChannelTick, 3> ch{};
};

class TtrakPlayer {
public:
    // Tables of the routine. Ours: period(i) = round(kTtrakPeriod0 × 2^(−i/192)); MFP: the smallest prescaler whose
    // data (2 457 600 / prescaler / rate, rounded; 256 written 0) fits 256, for 2 interrupts per period at A4 = 440 Hz
    // on note 46; wow: pairs (level 1-15 around 8, ticks − 1), ended by a byte with bit 7 set.
    struct Tables {
        std::array<uint16_t, kTtrakPitches> period{};
        std::array<uint8_t, kTtrakPitches> mfpData{}, mfpPrescaler{};
        std::vector<uint8_t> wow;
    };
    static const Tables& defaultTables();

    // The module is copied; the playback starts at position 0 as the routine's init does.
    explicit TtrakPlayer(const TtrakModule& module);
    // Other tables (the tool that compares with the routine); they must outlive the player.
    void setTables(const Tables& t) noexcept { tables_ = &t; }

    TtrakTick tick();

    int position() const noexcept { return pos_; }
    int row() const noexcept { return row_; }
    // Times the song went back to its restart position (or jumped back) since the start.
    int loops() const noexcept { return loops_; }

    // The routine's channel variables, for the comparison tool.
    struct Channel {
        uint8_t note = 0, effect = 0, param = 0;
        int instrument = 0;  // 1-32
        uint8_t atten = 0, type = 0, shape = 0, wow = 0, hardSync = 0, link = 0;
        uint16_t detune = 0, fixed = 0;
        uint8_t fmSpeed = 0, fmDepth = 0;
        uint16_t fmValue = 0, fmPos = 0;
        uint8_t morphSpeed = 0;
        uint16_t morphValue = 0, morphPos = 0;
        int8_t ampCount = 0;
        uint8_t ampSpeed = 0;
        int ampPos = 0, ampEnd = 0, ampLoop = 0;  // bytes from the envelope's start
        int8_t arpCount = 0;
        uint8_t arpSpeed = 0;
        std::array<uint8_t, 16> arp{};  // the instrument's arpeggio table as the effects left it (0: speed)
        int arpPos = 1;
        int8_t noiseCount = 0;
        uint8_t noiseSpeed = 0;
        int noisePos = 0;  // byte of the record
        uint8_t vibDepth = 0;
        int8_t vibSpeed = 0, vibOffset = 0, vibCount = 0;
        bool porta = false;
        int16_t slide = 0, slideRate = 0, lastPitch = 0;
        uint16_t mfpPitch = 0;
        uint8_t step = 0;
        bool timerOn = false;
        uint16_t wowIndex = 0;
        uint8_t wowValue = 0;
        int8_t wowCount = 0;
        uint8_t mfpTimer = 0;
        bool linked = false;
        bool sampleStart = false;
    };
    const Channel& channel(int c) const noexcept { return ch_[size_t(c)]; }
    uint8_t lastShape() const noexcept { return lastShape_; }
    int8_t counter() const noexcept { return counter_; }

private:
    const TtrakInstrument& rec(const Channel& c) const noexcept;
    void readRow(int c, const TtrakCell& cell, TtrakTick& t);
    // Returns what the routine leaves in bits 8-15 of the register that its effects then read (the vibrato's speed
    // takes bits 8-11 of it): those of where a digidrum's sample starts in the sample block, 0 otherwise.
    uint8_t trigger(int c, uint8_t instrument, TtrakTick& t);
    void effect(int c, uint8_t high = 0);
    uint32_t sampleOffset(int slot) const noexcept;
    void takeTimer(int c);
    void dropTimer(int c);
    // The channel's part of the tick: volume, tone period, noise, buzzer, mixer bits, timer.
    void channelTick(int c, TtrakTick& t);
    void fmStep(Channel& v) const noexcept;
    void morphStep(Channel& v) const noexcept;

    TtrakModule m_;
    const Tables* tables_;
    std::array<Channel, 3> ch_{};
    // Song state.
    int8_t counter_ = 0;
    uint8_t speed_ = 20, groove_ = 0;
    int row_ = 0, pos_ = 0, loops_ = 0;
    bool readRow_ = true, jump_ = false, break_ = false;
    uint8_t jumpTo_ = 0;
    uint8_t sidechain_ = 0;
    int8_t decay_ = 3;
    uint8_t vibDivider_ = 0;
    uint8_t lastShape_ = 0;
    uint8_t timersHeld_ = 0;  // bit 1: A held, bit 2: D held, bit 0: D taken last
    uint8_t mixer_ = 0xF8;
    std::array<uint8_t, 13> buf_{};
    std::array<bool, 3> sync_{};
    std::array<bool, 3> running_{};  // the channel's MFP timer counts (set by the routine, not stopped since)
    std::array<uint8_t, 14> chip_{};
};

}  // namespace stym::ymio
