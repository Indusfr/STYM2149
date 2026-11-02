// STYM2149 — ymio: maxYMiser songs played tick by tick
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// What maxYMiser's replay routine does at each of its calls, for the three YM channels, written from how gwEm's
// routine behaves (github.com/gwEm303/maxYMiser_replay, read as a reference and run in the emulated Atari to compare;
// none of its code or data is reused here) and from the manual of maxYMiser 1.67. The note, square, buzzer and
// semitone tables are the standard formulas (2 MHz clock, A4 = 440 Hz on note 69).
//
// Each tick:
// - at the first tick of a row, the rows of channels A, B and C are read: an instrument number other than the
//   current one copies the instrument (an effect that changes the instrument forgets which one it is, so the next
//   number copies it again); any instrument number restarts the sequences, keeping the portamento under 'P'; a note
//   (2 and up) becomes the current note ('P' glides from the old one), note 1 cuts the channel; the volume column
//   attenuates ($0 loudest); then the two effects;
// - the eight sequences of each channel step every `speed` ticks of the instrument: volume, arpeggio (semitones),
//   vibrato (period units), mixer (square, noise, buzzer shape, timer effect), noise period, fixed frequency, pulse
//   width (and the timer waveform, read whole by the timer effects); a sequence whose end loops on itself stops there;
// - volume = volume sequence − volume column − instrument volume, nothing below 1; a channel with a buzzer plays the
//   envelope; the mixer of the chip, the noise period (of the last channel using noise), the buzzer period and shape
//   (of the last channel using the buzzer), the square periods (note + arpeggio + transpose + detune + portamento +
//   vibrato, or a fixed frequency);
// - the timer effects (SID, sync buzzer, digidrum, pulse width, square FM, buzzer FM, sync square) take their timer
//   when the timer mask allows it; one that the MFP cannot play silences its channel;
// - the registers are written, except those that a timer effect drives (the volume of a SID, digidrum or pulse
//   width channel, the square period of an FM or sync square channel, the buzzer of a buzzer FM channel); the buzzer
//   shape is written when it changes or when the instrument asks for a start sync;
// - the song moves on: `speed` ticks per row, 64 rows per step (a pattern break ends the step after its row), steps
//   whose pattern is $FD skipped (looping patterns of the jam mode), the song restarting at its repeat step. A song
//   saved in jam mode plays as the routine plays it when asked (setJamMode): a looping pattern sends the song back to
//   the step after the previous one, or to the start.
// Not played: the STe DMA column, the pattern-play mode of the editor, MIDI.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "ymio/MaxYm.h"

namespace stym::ymio {

// Timer effect of a channel: the low nibble of its mixer value.
enum class MaxYmTimer : uint8_t {
    None = 0,
    SyncSquare = 0x1,
    Sid = 0x5,
    PulseWidth = 0x9,
    SyncBuzzer = 0xB,
    SyncBuzzerFm = 0xC,
    Digidrum = 0xD,
    BuzzerFm = 0xE,
    SquareFm = 0xF,
};

struct MaxYmChannelTick {
    uint8_t note = 0;        // current note (0: none)
    bool attack = false;     // a row starts a note: a note, or an instrument number that restarts the current one
    bool cut = false;        // a row cuts the channel (note off, or the song's $FE)
    uint8_t instrument = 0;  // instrument whose settings the channel holds (0: none or changed by an effect)
    uint8_t volume = 0;      // volume computed by the routine (bit 4: envelope), before any timer effect
    uint8_t timer = 0;       // MaxYmTimer of the mixer (0 when the timer mask takes it away)
    bool timerRuns = false;  // the timer effect plays (not silenced by its frequency)
    uint16_t timerPeriod = 0;   // its frequency, in tone period units at 2 MHz
    uint8_t timerSequence = 0;  // its waveform: SID volumes, sync buzzer shapes, FM steps
    // SID, sync buzzer and square FM that play: the steps of the waveform (SID 1-16, sync buzzer and FM 1-8) and their
    // words (SID: the volume taken off the channel's at that step; sync buzzer: the R13 shape in the low byte; FM: the
    // semitones above the note in bits 0-6 of the low byte), one step per interrupt. SID, sync buzzer, square
    // FM and digidrum that play: the MFP setting written (prescaler 1-7 of 4, 10, 16, 50, 64, 100, 200; data 1-256): a
    // SID of n steps runs n interrupts per period of its note, a sync buzzer and an FM two, a digidrum one per sample.
    uint8_t timerSteps = 0;
    std::array<uint16_t, 16> timerWave{};
    uint8_t timerPrescaler = 0;
    uint16_t timerData = 0;
    uint8_t digidrum = 0;       // Digidrum: sample 1-8 and its MFP rate
    uint8_t digidrumRate = 0;
    bool digidrumStart = false;  // the digidrum starts at this tick
    bool startSync = false;      // the square was restarted (period 2 written first)
};

struct MaxYmTick {
    int step = 0, row = 0;  // song step and row of this tick
    bool rowStart = false;  // the rows were read at this tick
    uint8_t speed = 6;      // ticks per row after the rows of this tick
    std::array<uint8_t, 14> reg{};     // R0-R13 as the chip holds them after the writes of the routine
    std::array<uint8_t, 13> buffer{};  // R0-R12 as the routine computed them (written or not)
    uint16_t written = 0;              // registers written (bit r)
    bool shapeWritten = false;         // R13 written: the envelope restarts
    int buzzerChannel = -1;            // channel whose buzzer the routine sets up (0-2), -1: none
    std::array<MaxYmChannelTick, 3> ch{};
    uint8_t sync = 0;  // demo sync byte ('Z')
};

class MaxYmPlayer {
public:
    // The song and voiceset are copied; the playback starts at step 0 as the routine's init does.
    MaxYmPlayer(const MysSong& song, const MyvVoiceset& voices);

    // One call of the routine.
    MaxYmTick tick();

    // Jam mode (the editor's live mode, saved with the song): off by default, the song plays as the editor plays
    // it outside that mode.
    void setJamMode(bool on) noexcept { jam_ = on; }
    bool savedInJamMode() const noexcept { return song_.header[39] != 0; }

    int step() const noexcept { return step_; }
    int row() const noexcept { return row_; }
    // Steps played, in order, from the start (each step once, until the song goes back to its repeat step).
    int loops() const noexcept { return loops_; }

private:
    struct Voice {
        std::array<uint8_t, 48> rec{};  // the instrument as the effects left it (bytes 16-63 of its record)
        int8_t noiseTranspose = 0;
        uint8_t seqTick = 0;  // ticks into the current sequence step
        bool synced = false;  // start sync done
        uint8_t volumeColumn = 0;
        int16_t portaRate = 0, portaAcc = 0;
        std::array<uint8_t, 2> extraArp{};
        uint8_t extraArpStep = 0;
        bool digidrumStarted = false;
        int8_t transpose = 0;
        std::array<uint8_t, 7> pos{};  // positions of the volume, arpeggio, vibrato, mixer, noise, fixed, PWM sequences
        bool pulseSet = false;         // '9' set the pulse width: its slide no longer counts
        uint16_t vol = 0, mix = 0, noise = 0, fix = 0;
        int16_t arp = 0, vib = 0;
        uint8_t pwm = 0;
        uint8_t done = 0;  // sequences that stopped (bit 0 volume ... bit 6 pulse width)
        int16_t slideRate = 0, slideAcc = 0;
        int8_t pwmSlideRate = 0;  // kept by the row reads
        int16_t pwmSlideAcc = 0;
        uint8_t instrument = 0;
        uint8_t note = 0;
        // Fresh state of a row with an instrument (rec and what follows it is cleared up to the slide).
        void restart() noexcept;
        void cut() noexcept;  // note off: everything up to the slide, the instrument forgotten
    };

    uint16_t sequenceWord(uint8_t n, int at) const noexcept;
    void readRow(int c, uint8_t pattern, int row, MaxYmChannelTick& o);
    void effect(int c, uint8_t fx, uint8_t param, bool muted);
    void sequences(Voice& v);
    uint8_t volume(Voice& v) const noexcept;
    uint16_t squarePeriod(Voice& v, int bit, bool square);
    uint16_t buzzerPeriod(Voice& v) const noexcept;
    void timers(int c, MaxYmTick& t);
    void slides(Voice& v) noexcept;
    void nextStep();

    MysSong song_;
    MyvVoiceset voices_;
    std::array<Voice, 3> v_{};
    std::array<uint8_t, 4> patterns_{};
    int step_ = 0, next_ = 1, row_ = 0, counter_ = 0, speed_ = 6, loops_ = 0;
    uint8_t timerMask_ = 7, mutes_ = 0;
    bool midiClock_ = false, jam_ = false;
    std::array<uint8_t, 13> buf_{};  // registers as computed (R0-R12)
    std::array<uint16_t, 3> toneWord_{};
    uint16_t buzzWord_ = 0;
    std::array<uint8_t, 14> chip_{};
    std::array<uint8_t, 3> timerKind_{};   // effect that the timer of each channel plays between two calls (0: stopped)
    std::array<int16_t, 3> timerShape_{};  // shape that a sync buzzer timer leaves in R13 (-1: it changes along its steps)
    int chipShape_ = 0;                    // R13 as the chip holds it, the timers' writes included (-1: unknown)
    uint32_t d2_ = 0;  // what the sequences leave in a stopped sequence's position (see sequences())
    uint8_t sync_ = 0;
};

// The note tables of the routine: square period of note n (0-127) at 2 MHz, buzzer period, and the semitone factors
// (32 × 2^(i/12)) used to scale portamentos.
uint16_t maxYmSquarePeriod(int n) noexcept;
uint16_t maxYmBuzzerPeriod(int n) noexcept;
uint16_t maxYmSemitoneScale(int i) noexcept;
// Rate (Hz) of the interrupts that the routine sets for a timer effect of `steps` steps at `period` (tone period units
// at 2 MHz): `steps` interrupts per period, rounded to the MFP's prescalers and data; 0 when it cannot play it (the
// effect is then silenced). A sync buzzer counts 2 steps.
double maxYmTimerHz(uint16_t period, int steps) noexcept;

}  // namespace stym::ymio
