// STYM2149 — ymengine: engine settings (tracks, arpeggio, global settings)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Fields, ranges and defaults of the prototype (mkCh, GLOB and the worklet), in the units of the plugin:
// the envelope period is that of the chip (EP of the prototype × 32), durations are in milliseconds.
#pragma once
#include <array>
#include <cstdint>

#include "ymcore/ChipRenderer.h"

namespace stym::ymengine {

inline constexpr int kTracks = 16;
inline constexpr int kOrnamentSteps = 64;     // steps of the Modulation table of a sound (ornament)
inline constexpr int kArpSteps = 16;          // steps of the arpeggio table (Table mode)
inline constexpr int kSlideTablePoints = 32;  // CURVE_N of the prototype
inline constexpr int kDigidrumSlots = 8;      // slots of user Digidrum samples
// Digidrum: rate of a slot at its root note (7 800 Hz: drumRate of the prototype), range of
// STYM2149 (Y34 shows 8 000 and 11 025 Hz).
inline constexpr double kDigidrumRate = 7800;
inline constexpr double kDigidrumRateMin = 2000, kDigidrumRateMax = 20000;
inline constexpr double kDigidrumCentsMax = 100;  // fine tune of a slot, ± cents
inline constexpr int kInstruments = 32;       // instruments of the project, $01 to $20

// Part of a sample of n values (or frames) that a slot plays, from start to end (end excluded): its Start and End,
// End 0 or past a shorter sample meaning its end, a Start not before that end meaning its beginning.
struct SampleSpan {
    uint32_t start = 0, end = 0;
    uint32_t size() const noexcept { return end - start; }
};
inline SampleSpan sampleSpan(uint32_t start, uint32_t end, uint32_t n) noexcept {
    const uint32_t e = end > 0 && end <= n ? end : n;
    return {start < e ? start : 0, e};
}
// The same part once the n values are reversed (Reverse tool): Start and End mirrored; the whole sample stays
// the whole sample (Start 0, End 0).
inline void mirrorSpan(uint32_t& start, uint32_t& end, uint32_t n) noexcept {
    const SampleSpan p = sampleSpan(start, end, n);
    if (p.start == 0 && p.end == n) return;
    start = n - p.end;
    end = n - p.start;
}

// Playback of a Digidrum slot: rate at the root note, fine tune, the part played (Start to
// End, sampleSpan) and its sustain loop (Start to End replayed while the note is held; on release, the part plays on
// to End). The defaults play like the prototype: 7 800 Hz for C4, the whole sample, without loop.
struct DigidrumSlot {
    double rateHz = kDigidrumRate;
    uint8_t root = 60;
    double cents = 0;  // -100 to 100
    bool loop = false;
    uint32_t start = 0, end = 0;  // values; end 0: to the end of the sample
    // Value of silence, a source byte 0 once converted (8 for Linear): the editor's preview centres on its level, so
    // that it starts and ends without a click. Set from the conversion when the slot goes to the engine.
    uint8_t rest = 8;
    bool operator==(const DigidrumSlot&) const = default;
};

// Voice modes (STYM2149_SPEC.md §3.5).
enum class VoiceMode : uint8_t {
    Studio16,    // 16 tracks with independent virtual channels, stereo (16-channel mode of the prototype)
    Authentic3,  // one chip: three channels and the Digidrum track, noise and envelope shared, mono
    Dual3,       // two chips of three channels
};

// Generator of a track (`effect` codes of the prototype; Dma: 8-bit sample of the STE DMA bank, specific to
// STYM2149).
enum class Generator : uint8_t { Chip = 0, Sid = 1, Digidrum = 2, SyncBuzzer = 3, Chipdrum = 4, Dma = 5 };
inline constexpr int kGenerators = 6;

// Frequency of the timer effects: quantised by the MFP 68901 (default, §3.3) or continuous ("Free").
enum class TimerMode : uint8_t { Mfp, Free };

enum class ArpMode : uint8_t { Up = 0, Down = 1, UpDown = 2, Order = 3, Random = 4, Table = 5 };

// Clock of the arpeggios (keyboard and sequencer tracks): cadence in Hz, division of the MIDI clock (24 pulses per
// quarter note), or division of the tempo (Arp Sync, sixth DAW test): the quarter notes of the host while it
// plays in Host sync, otherwise a cadence of tempo × 24 / 60 / division Hz (that of the host, or of the sequencer).
enum class ArpClock : uint8_t { Hz, MidiClock, Tempo };

// Clock of the sequencer: internal (Free run: free tempo, swing), external MIDI clock (24 pulses per quarter note,
// Start/Stop/Continue/SPP, like the prototype), or host transport (Host sync: play, stop, position, tempo and
// time signature of the host; the arpeggios set to the clock then follow its quarter notes).
enum class SeqClock : uint8_t { Internal, Midi, Host };

// Cadence of the sequencer rows (Q3). BPM: that of the prototype, a step is a fraction of a beat of the
// tempo (resolution), swing included. Ticks: tracker style, a row lasts Speed ticks plus those of the groove; the
// tick is the VBL frame (vblHz) in Free run, a pulse with the MIDI clock; in Host sync, rows follow the host (BPM).
enum class SeqTiming : uint8_t { Bpm, Ticks };
inline constexpr int kSpeedMax = 31;     // ticks per row (Sxx: 1 to $1F)
inline constexpr int kGrooveSteps = 16;  // groove: ticks added to successive rows, looped
inline constexpr int kGrooveMax = 15;

// Effect of the velocity (STYM2149_SPEC.md §7). Accent: that of the prototype, audio gain in 16-channel mode and
// 4-bit volume in 3-channel mode (the only one possible on the chip). Volume: 4-bit volume in all modes. Off: ignored.
enum class VelocityMode : uint8_t { Accent, Volume, Off };

// What the modulation wheel and the pressure of a note (aftertouch) drive, from nothing at rest to the full effect
// at the top (other targets than the vibrato: Indus, 25/10/2026 20:47): nothing, the depth of its vibrato
// (GlobalParams::expressCents at the top), its 4-bit volume (raised towards 15), its envelope frequency (up to an
// octave higher: Sync Buzzer and the Env sources) or the depth of the LFO of its track (none at rest). Where the wheel
// and the pressure drive the same target, the higher of the two plays.
enum class ExpressTarget : uint8_t { Off, Vibrato, Volume, Envelope, Lfo };
// What the pitch bend of a track drives (Indus 25/10/2026 20:51): the pitch of its notes, their 4-bit volume
// (the centre leaves it, raised towards 15 up, lowered towards 0 down) or their envelope frequency (by the semitones of
// the bend, the note unchanged: Sync Buzzer and the Env sources). The bend of an MPE note stays on its pitch.
enum class BendTarget : uint8_t { Pitch, Volume, Envelope };
// What the MPE slide (CC74) of a note drives (Indus 25/10/2026 20:42): its 4-bit volume (64 leaves it, raised
// towards 15 above, lowered towards 0 below), its envelope frequency (up to an octave either way: Sync Buzzer and the
// Env sources) or the depth of its vibrato (that of the wheel at the top, none from the middle down).
enum class SlideTarget : uint8_t { Volume, Envelope, Vibrato };

// Instrument sequences: rows of the Modulation table specific to STYM2149, played with the rows
// of the prototype (same run, same length, same loop, same cadence). A value of 0 changes nothing.
struct SeqRows {
    std::array<int8_t, kOrnamentSteps> period{};  // PER: tone period offset, -127 to +127 (+: lower)
    std::array<uint8_t, kOrnamentSteps> noise{};  // NP: noise period 1 to 31
    std::array<uint8_t, kOrnamentSteps> mix{};    // MIX: channel mode 0 to 7 (Y40) + 1, tone, noise, envelope bits
    std::array<uint8_t, kOrnamentSteps> gen{};    // GEN: SeqGen
    std::array<uint8_t, kOrnamentSteps> fix{};    // FIX: 1 = the step plays the fixed note of the track
    bool operator==(const SeqRows&) const = default;
    bool empty() const noexcept { return *this == SeqRows{}; }
};
inline constexpr int kPeriodDeltaMax = 127;
inline constexpr int kChannelModes = 8;  // 0 Off to 7 Tone+Noise+Env

// Generator of a step of the GEN row of the sequences: that of the note, or a melodic generator.
enum class SeqGen : uint8_t { Note = 0, Chip = 1, Sid = 2, SyncBuzzer = 3 };
inline constexpr int kSeqGens = 4;

// Pitch matrix (X13, T9): which pitch modulations drive the square, the timer (SID, Sync Buzzer) and the
// envelope period (buzzer). Sources: pitch rows of the table (ARP, PER, FIX), slide, vibrato (track and
// wheel). By default, that of the prototype: square and timer follow everything, the envelope nothing.
enum class PitchSource : uint8_t { Table = 0, Slide = 1, Vibrato = 2 };
enum class PitchTarget : uint8_t { Square = 0, Timer = 1, Buzzer = 2 };
// Bit of the matrix (TrackPatch::pitchMatrix) by which source s drives target t: three bits per source.
constexpr uint16_t pitchBit(PitchSource s, PitchTarget t) noexcept { return uint16_t(1u << (int(s) * 3 + int(t))); }
inline constexpr uint16_t kPitchMatrixAll = 0x1FF;  // the nine bits
inline constexpr uint16_t kPitchMatrixDefault = 0xDB;  // square and timer for the three sources
static_assert(kPitchMatrixDefault == (pitchBit(PitchSource::Table, PitchTarget::Square) |
                                      pitchBit(PitchSource::Table, PitchTarget::Timer) |
                                      pitchBit(PitchSource::Slide, PitchTarget::Square) |
                                      pitchBit(PitchSource::Slide, PitchTarget::Timer) |
                                      pitchBit(PitchSource::Vibrato, PitchTarget::Square) |
                                      pitchBit(PitchSource::Vibrato, PitchTarget::Timer)));
// Ranges of TrackPatch::detuneCents (±100) and analogCents (0 to 50).
inline constexpr int kDetuneMaxCents = 100, kAnalogMaxCents = 50;
// Timer waves: steps of the programmable SID wave and of the Sync Buzzer.
inline constexpr int kSidWaveSteps = 16, kBuzzSteps = 8;
// Phase of the Sine-SID by default, in bars (24/10/2026), and its period, in bars (8 by default, two
// cycles over the 16 bars; 2 to 16): the sine of sineSidWave().
inline constexpr int kSidWavePhase = 8;
inline constexpr int kSidWavePeriod = 8, kSidWavePeriodMin = 2;
// Forms of the SID wave, from its menu (24/10/2026): Base, the prototype's square (two interrupts per period,
// toggled at each); Square, a square of Steps interrupts per period, the note's volume over the first sidWidth of them
// (1 to Steps - 1; 4 by default, half and half); Sine, a Sine-SID drawn in the bars; Custom, the bars
// drawn; Bounce, the Sine-SID on the scale of the chip's levels (Indus, 18:41; after Custom so that the values
// already saved keep their form, before it in the menu). Steps: 8 by default (Indus, 17:47).
enum class SidForm : uint8_t { Base, Square, Sine, Custom, Bounce };
inline constexpr int kSidForms = 5, kSidSquareSteps = 8;
// The forms drawn as a Sine-SID, which a bar moved shifts and Period sets.
constexpr bool sineForm(SidForm f) noexcept { return f == SidForm::Sine || f == SidForm::Bounce; }
// The form saved as `v`; a value of a later version plays its bars, as Custom.
constexpr SidForm sidFormOf(int v) noexcept { return v >= 0 && v < kSidForms ? SidForm(v) : SidForm::Custom; }
inline constexpr int kSidWidth = 4, kSidWidthMax = kSidWaveSteps - 1;
// Buzzer: N / D ratio of the tuned envelope, fine tune in cents.
inline constexpr int kEnvRatioMax = 16, kEnvFineMax = 100;

// LFO of a track (sixth DAW test, item 59): computed at the replay tick like the tables, it only writes
// registers, so Authentic 3ch and the register exports keep it. It restarts at each note, like the vibrato, and goes
// on over the notes of an arpeggio; with Retrig Off, it runs free: a note takes the phase of its track. It moves its target by depth on either side, in the unit of the target: 4-bit
// volume steps, noise period steps, cents (envelope period, SID ratio, pitch; the pitch goes through the vibrato's row
// of the pitch matrix), percent of the pan (16 channels).
enum class LfoShape : uint8_t { Triangle, Sine, Square, SawUp, SawDown, Random };
enum class LfoTarget : uint8_t { Volume, NoisePeriod, EnvPeriod, SidRatio, Pitch, Pan };
inline constexpr int kLfoShapes = 6, kLfoTargets = 6;
inline constexpr double kLfoMinHz = 0.1, kLfoMaxHz = 20;
// Synced to the tempo: one cycle in pulses of kLfoPpq per quarter note, from 4/1 to 1/32, then 1/32T and 1/64 (ninth
// DAW test, item 119, appended, the indices kept in the saved states and patches).
inline constexpr int kLfoPpq = 48;
inline constexpr std::array<uint16_t, 12> kLfoDivisionTicks = {768, 384, 192, 96, 48, 24, 16, 12, 8, 6, 4, 3};
inline constexpr int kLfoDivisions = int(kLfoDivisionTicks.size());
// Largest depth of each target, in its unit, in the order of LfoTarget.
inline constexpr std::array<int, kLfoTargets> kLfoDepthMax = {15, 31, 2400, 2400, 2400, 100};

// Audio delay (eighth DAW test, item 98): a stereo echo fed by a send of each track, after its fader and its
// pan, in 16 channels only (Indus, 22/10/2026: the 3-channel modes have the echo of the notes, which the chips play).
// Its time is a division of the tempo (that of the host in Host sync, otherwise the sequencer's) or timeMs; each
// repeat goes through the low-pass hiCutHz again.
// Divisions in quarter notes, in the order of the menu: 1/2, 1/4D, 1/4, 1/4T, 1/8D, 1/8, 1/8T, 1/16D, 1/16, 1/16T,
// 1/32.
inline constexpr std::array<double, 11> kDelayDivisionBeats = {2,     1.5, 1,     2.0 / 3, 0.75,   0.5,
                                                               1.0 / 3, 0.375, 0.25, 1.0 / 6, 0.125};
inline constexpr int kDelayDivisions = int(kDelayDivisionBeats.size());
inline constexpr double kDelayMinMs = 10, kDelayMaxMs = 2000;
inline constexpr double kDelayFeedbackMax = 0.9;
inline constexpr double kDelayHiCutMin = 500, kDelayHiCutMax = 20000;

// Settings of the audio delay (GlobalParams::delay).
struct DelayParams {
    bool on = false;
    bool sync = true;          // a division of the tempo, otherwise timeMs
    uint8_t division = 4;      // index of kDelayDivisionBeats (1/8D)
    double timeMs = 375;       // kDelayMinMs to kDelayMaxMs
    double feedback = 0.35;    // 0 to kDelayFeedbackMax
    double hiCutHz = 6000;     // kDelayHiCutMin to kDelayHiCutMax
    double level = 1;          // level of the echoes, 0 to 1
    bool pingPong = false;     // echoes alternately left and right (stereo outputs)
    bool operator==(const DelayParams&) const = default;
};

// Echo of the notes (every mode): each note of the sound is played again `repeats` times, one division of the
// tempo apart (kDelayDivisionBeats, at the tempo of the attack), each time `drop` steps of the 4-bit volume lower,
// on the track `target` (-1: the same one) with the sound of the note. These are notes of the chip: Authentic 3ch
// plays them and the register exports keep them. In 3 channels a repeat takes the channel of the target track only
// while it plays no note of its own (a new note there always wins); in 16 channels it takes a free voice.
inline constexpr int kEchoRepeatsMax = 8, kEchoDropMax = 15;
struct TrackEcho {
    bool on = false;
    uint8_t division = 5;  // index of kDelayDivisionBeats (1/8)
    uint8_t repeats = 3;   // 1 to kEchoRepeatsMax
    uint8_t drop = 3;      // 4-bit volume steps per repeat, 0 to kEchoDropMax
    int8_t target = -1;    // track of the repeats (0 to 15), -1: the track of the note
    bool operator==(const TrackEcho&) const = default;
};

// LFO settings of a sound, described above LfoShape.
struct TrackLfo {
    bool on = false;
    LfoShape shape = LfoShape::Triangle;
    LfoTarget target = LfoTarget::Volume;
    bool sync = false;          // one cycle per division of the tempo, otherwise hz
    double hz = 4;              // kLfoMinHz to kLfoMaxHz
    uint8_t division = 4;       // index of kLfoDivisionTicks (1/4)
    double depth = 4;           // 0 to kLfoDepthMax of the target
    bool retrig = true;         // restarted at each note; Off: free running
    bool operator==(const TrackLfo&) const = default;
};

// Sound settings of a track, frozen in the voice at the attack (setupVoice of the prototype).
struct TrackPatch {
    bool toneOn = true, noiseOn = false;   // Source: Tone / Noise / Tone+Noise
    bool mono = false;                     // Voicing: Mono = legato with a 45 ms slide (16-channel)
    uint8_t noisePeriod = 8;               // R6, 1 to 31
    bool envelopeVolume = false;           // volume driven by the envelope (bit 4 of R8-R10)
    uint8_t volume = 13;                   // 0 to 15
    uint16_t envPeriod = 12800;            // R11-R12 (400 × 32)
    uint8_t envShape = 8;                  // R13
    Generator generator = Generator::Chip;
    double sidRatio = 1.0;                 // pitch of the SID wave = note × ratio (0.25 to 8; 1 plays the note)
    uint8_t sidMinVolume = 0;              // low volume of the SID wave
    uint8_t digidrumSlot = 0;              // slot played on the keyboard (0 to 7)
    double vibratoCents = 0;               // 0 to 200
    double vibratoHz = 6;                  // 1 to 20
    bool vibratoOn = true;                 // vibrato On/Off of the track (sixth DAW test)
    bool ornamentOn = false;
    uint8_t ornamentLength = 8;            // 1 to 64
    bool ornamentLoop = true;              // Loop, otherwise Hold on the last step
    uint8_t ornamentRate = 1;              // VBL frames per step, 1 to 4
    std::array<int8_t, kOrnamentSteps> ornamentPitch{};    // semitones, -12 to +12
    std::array<uint8_t, kOrnamentSteps> ornamentNoise{};   // 1 = noise on this step
    std::array<uint8_t, kOrnamentSteps> ornamentVolume = filled(15);  // 0 to 15
    double attackMs = 0, decayMs = 0, sustainPct = 100, releaseMs = 0;
    bool adsrChip = false;                 // "Chip" ADSR (4-bit volume at the VBL cadence), otherwise "Smooth"
    // Instrument sequences and pitch settings.
    SeqRows rows;
    uint8_t fixedNote = 60;                // MIDI note of the FIX steps
    double detuneCents = 0;                // detune, -100 to +100
    double analogCents = 0;                // "analog" randomness: offset drawn at each note, 0 to ±50
    bool hardSync = false;                 // square (16-channel) and timer restarted on tied notes too
    uint8_t link = 0;                      // instrument chained at the end of a table in Hold (1 to 32), 0: none
    uint16_t pitchMatrix = kPitchMatrixDefault;
    // Timer waves. SID, by its form: Base, the square wave of the prototype (volume of the
    // note, low volume); Square, over sidWaveLen steps (its Steps, 2 to 16) the volume of the note on the first
    // sidWidth of them (1 to Steps - 1), the low volume on the others; Sine, Bounce and Custom, the volumes 0 to
    // 15 of sidWave played one per interrupt over sidWaveLen steps, scaled by the volume of the note. Each form keeps
    // the settings of the others. Sync Buzzer: R13 shapes written in turn at each restart (buzzLen from 1 to 8); a step
    // at 0, or buzzLen at 0: the shape of the track.
    std::array<uint8_t, kSidWaveSteps> sidWave{};
    uint8_t sidWaveLen = kSidSquareSteps;
    SidForm sidForm = SidForm::Base;
    // Sine and Bounce (24/10/2026): the bars stay a Sine-SID of sidWavePeriod bars (2 to 16) at
    // sidWavePhase (0 to 15), which a bar moved changes (sineSidWavePhase). Read by the editor only: the engine
    // plays sidWave.
    uint8_t sidWavePhase = kSidWavePhase;
    uint8_t sidWavePeriod = kSidWavePeriod;
    uint8_t sidWidth = kSidWidth;  // width of the Square, in steps
    std::array<uint8_t, kBuzzSteps> buzzShapes{};
    uint8_t buzzLen = 0;
    // Buzzer. Tuned envelope: the envelope period follows the note, the envelope plays the note ×
    // envRatioN / envRatioD (1 to 16), shifted by envFineCents (±100); envPeriod is no longer read. YM Pad: under
    // the envelope, the tone goes to period 0 (about -6 dB, a TTrak trick).
    bool envTuned = false;
    uint8_t envRatioN = 1, envRatioD = 1;
    double envFineCents = 0;
    bool ymPad = false;
    // DMA sample played, 0 to 7; -1: the one of the rank of the track (DMA track n: sample n in 3-channel
    // mode, track n: sample ((n - 1) modulo 8) + 1 in 16-channel mode).
    int8_t dmaSlot = -1;
    TrackLfo lfo;
    TrackEcho echo;
    bool operator==(const TrackPatch&) const = default;

private:
    static constexpr std::array<uint8_t, kOrnamentSteps> filled(uint8_t v) {
        std::array<uint8_t, kOrnamentSteps> a{};
        for (auto& x : a) x = v;
        return a;
    }
};

// Arpeggio of a track. The arpeggio played on the keyboard takes that of the focused track (sendArp of the prototype).
struct TrackArp {
    bool on = false;
    double rateHz = 50;            // 4 to 200
    bool track = false, land = false;  // arpeggio of the sequencer tracks
    ArpMode mode = ArpMode::Up;
    uint8_t octaves = 1;           // 1 to 3 (no effect in Table mode)
    std::array<int8_t, kArpSteps> table = {0, 4, 7};
    uint8_t length = 3;            // 1 to 16
    bool operator==(const TrackArp&) const = default;
};

// Mixer of a track: applied live to the voices that sound.
struct TrackMix {
    bool enabled = false;          // track active (tab); the prototype activates track 1 at start
    double level = 0.75;           // 0 to 1
    double pan = 0;                // -1 to 1 (16-channel only: the 3-channel mode is mono like the ST)
    bool mute = false, solo = false;
    double send = 0;               // send to the delay, 0 to 1, after the fader and the pan
    // Right side of a stereo DMA sample: its own fader (levelR, unless link: level), mute and solo. The left
    // side and any other sound take level, mute and solo.
    bool link = true;
    double levelR = 0.75;
    bool muteR = false, soloR = false;
};

// Sequencer settings of a track.
struct TrackSeq {
    double gate = 0.5;     // duration of a one-step note, as a fraction of the step (GAT row: value of the step)
    int length = 0;        // own length of the track (polymetry), 0 = that of the pattern
    int octave = 3;        // octave of the track (seqOct): octave code 0 of the notes, note of the drums
};

// All the settings of a track (Engine::setTrack).
struct TrackParams {
    TrackPatch patch;
    TrackArp arp;
    TrackMix mix;
    TrackSeq seq;
};

// Instrument of the project: the complete sound of a track (sound and arpeggio), played by the steps of
// a melodic track whose INS row names it. The generator is the user's: in 3-channel mode, a generator that the
// track cannot play (Digidrum on a channel of the chip) leaves the track its own.
struct Instrument {
    bool used = false;
    TrackPatch patch;
    TrackArp arp;
};
using InstrumentBank = std::array<Instrument, kInstruments>;

// Settings of the whole engine (Engine::setGlobal): voice mode and machine, output levels, replay tick, keyboard
// playing, arpeggio clock, sequencer and slides.
struct GlobalParams {
    VoiceMode mode = VoiceMode::Studio16;
    // Machine: STE adds the DMA sound (eight DMA tracks after those of the YM in 3-channel mode, DMA
    // type possible on any track in 16-channel mode); ST, that of the prototype, has none.
    bool ste = false;
    double master = 0.7;
    // Second Master (seventh DAW test), that of Master while they are linked: the right output where it is
    // stereo (16 channels, 3 channels with the STE), with the balance, -1 (left only) to +1 (right only); chip 2 in
    // 2x3 channels (no balance there). The mono output of 3 channels with the ST and the YM Player take Master.
    double master2 = 0.7, masterPan = 0;
    bool softClip = true;                      // tanh saturation of the output, like the prototype
    DelayParams delay;
    uint32_t vblHz = 50;                       // replay tick: 50, 60, 70, 100 or 200 Hz
    // Frequency of A4, from which every note is tuned (kA4MinHz to kA4MaxHz of Pitch.h).
    double a4Hz = 440.0;
    std::array<uint8_t, 2> digidrumChannel = {2, 2};  // channel taken by the Digidrum track of each chip (0 = A)
    int focusTrack = 0;                        // track played on the keyboard
    bool layer = false;                        // play all the active tracks in layers
    TimerMode timer = TimerMode::Mfp;
    VelocityMode velocity = VelocityMode::Accent;
    // MIDI expression: what the bend, the wheel, the pressure and the MPE slide drive; the semitones of a bend
    // at its end (Bend range: the whole way of the Volume target); the vibrato of the others at their top, in cents
    // (Wheel depth).
    BendTarget bendTarget = BendTarget::Pitch;
    double bendRange = 2;
    ExpressTarget wheelTarget = ExpressTarget::Vibrato;
    ExpressTarget aftertouch = ExpressTarget::Vibrato;
    SlideTarget mpeSlide = SlideTarget::Volume;
    double expressCents = 50;
    uint32_t irqJitterCpuCycles = 0;           // interrupt jitter in 68000 cycles (0 = none); amplitude TO CONFIRM
    ArpClock arpClock = ArpClock::Hz;
    // Half pulses (48 per quarter note) per arpeggio step: 48, 24, 16, 12, 8, 6, 4, 3 or 2 (1/4 to 1/64, Tick),
    // MIDI clock or Tempo; the half pulse makes 1/64 possible.
    uint8_t arpDivision = 12;
    // Sequencer.
    SeqClock seqClock = SeqClock::Internal;
    double tempo = 120;                        // BPM of the song (Free run)
    double swing = 50;                         // 50 to 75 % (ignored with the MIDI clock)
    uint8_t resolution = 2;                    // kStepsPerBeat (1/16 by default)
    SeqTiming timing = SeqTiming::Bpm;
    uint8_t speed = 6;                          // Ticks: ticks per row (1 to kSpeedMax)
    uint8_t grooveLength = 1;                   // Ticks: length of the groove (1 to kGrooveSteps)
    std::array<uint8_t, kGrooveSteps> groove{}; // Ticks: ticks added to row n (n modulo the length)
    bool loop = true;                          // loop of the pattern, or of the Song
    bool songMode = false;                     // playback of the Song
    uint8_t sigNum = 4, sigDen = 4;            // time signature (metronome beats); in Host sync, that of the host
    bool metronome = false;                    // beep on each beat, higher on the first beat of the bar
    uint8_t metronomeLevel = 100;              // its level, 0 to 100 %
    bool quantizeVbl = false;                  // Quantize to VBL: notes and sequencer steps on frame boundaries
    uint16_t scaleMask = 0;                    // scale of the "Scale-locked" slides (12 bits, 0 = none)
    uint8_t scaleRoot = 0;                     // tonic, 0 = C; bit n of scaleMask: n semitones above it
    std::array<float, kSlideTablePoints> slideTable = linearTable();  // "Custom" curve

private:
    static constexpr std::array<float, kSlideTablePoints> linearTable() {
        std::array<float, kSlideTablePoints> a{};
        for (int i = 0; i < kSlideTablePoints; ++i) a[size_t(i)] = float(double(i) / (kSlideTablePoints - 1));
        return a;
    }
};

// Configuration fixed at preparation (allocations): clock, host rate, output chain.
struct EngineConfig {
    ymcore::RenderConfig render;   // clock, LFSR, host rate, output mode, board filters
    uint64_t seed = 1;             // seed of the random draws (Random arpeggio, Random walk, jitter)
    uint16_t trackOutputs = 0;     // separate outputs prepared: bit n = track n (Outputs::track)
};

}  // namespace stym::ymengine
