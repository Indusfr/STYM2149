// STYM2149 — ymengine: internal state of the voice engine (shared by Engine.cpp and Sequencer.cpp)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Transcription of the worklet of the prototype (YMProc: setupVoice, applyPitch, trigger, allocVoice, relVoice,
// killTag, arpTick, slideTo, process) on an exact time base: everything that touches a voice is dated in cycles of
// the master clock (VBL frames, slide steps, timer interrupts, ADSR stages, fade steps), instead of the host
// sample. The deliberate deviations from the prototype are listed in DECISIONS.md (D-049 to D-052).
//
// Everything here runs on the thread that renders (the audio thread in the plugin), without allocation or lock;
// only the constructor and Engine::prepare allocate. Three time units meet: the host sample (events, sequencer
// countdowns), the master cycle (c: the internal events of the voices, register writes) and the internal step
// (t = c / 8, the rate at which a YM channel changes its output).
// - Events: the Engine API queues them (push); render() applies each one at its sample, after rendering the voices
//   up to there (at(), eventCycle()).
// - Voices: a fixed array (Engine::kVoices); each voice keeps its sound frozen at the attack and the cycle of its next
//   internal event (slide step, VBL frame, timer interrupt, end of ADSR stage, fade step: Voice::nextEvent).
// - 16-channel mode: each voice runs its VirtualChannel; every change of its output level is written onto the left
//   and right buses as a step (dL, dR) at its internal step, with a change of slope (sL, sR) during a smooth ADSR
//   ramp. integrateBus sums the steps into one value per internal step (bufL, bufR), which the output chain
//   (TickStreamOutput) resamples to the host rate: all the voices go through a single resampler per side (D-049).
// - 3-channel modes: the voices of a chip write its registers (applyChip, only what changes), dated at their cycle,
//   with the shared noise and envelope rules of D-050; ChipRenderer emulates and resamples the chip.
// The sequencer (Sequencer.cpp) and the echo of the notes drive the voices through the same functions as the
// keyboard (setupVoice, relVoice, touch).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#include "ymcore/TickStreamOutput.h"
#include "DmaBank.h"
#include "StereoDelay.h"
#include "ymcore/Ym2149.h"
#include "ymengine/Engine.h"
#include "ymengine/Mfp68901.h"
#include "ymengine/Pitch.h"
#include "ymengine/Sequence.h"
#include "ymengine/VirtualChannel.h"

namespace stym::ymengine {

namespace detail {
inline constexpr uint64_t kNever = UINT64_MAX;  // cycle of an internal event that is not scheduled
inline constexpr double kDrumRate = 7800;      // base rate of the sampled drums (drumRate)
inline constexpr uint64_t kFadeRate = 48000;   // one fade step = one sample of the prototype, counted at 48 kHz (D-051)
inline constexpr double kLegatoSeconds = 0.045;  // slide of a Mono track from the held note (TrackPatch::mono)

inline double jsRound(double x) noexcept { return std::floor(x + 0.5); }  // JavaScript Math.round

// ceil(t × num / den) and floor(t × num / den) without overflow as long as den × num fits in 64 bits.
inline uint64_t mulDivCeil(uint64_t t, uint64_t num, uint64_t den) noexcept {
    const uint64_t q = t / den, r = t % den;
    return q * num + (r * num + den - 1) / den;
}
inline uint64_t mulDivFloor(uint64_t t, uint64_t num, uint64_t den) noexcept {
    const uint64_t q = t / den, r = t % den;
    return q * num + (r * num) / den;
}
inline uint64_t ceilCycles(double x) noexcept { return x <= 0 ? 0 : uint64_t(std::ceil(x)); }

// Drum generators: a hit plays to its end, without ADSR (D-205) or modulation.
inline bool isDrum(Generator g) noexcept { return g == Generator::Digidrum || g == Generator::Chipdrum; }

// xorshift64* for the draws specific to the plugin (seed of the Random walk slides outside the sequencer, analog
// pitch, Random LFO); the interrupt jitter has its own generator (IrqJitter).
struct Rng64 {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint64_t next() noexcept {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
};

// Kinds of the events queued by the Engine API (one per public event function).
enum class EventType : uint8_t {
    NoteOn, NoteOff, Bend, Wheel, AllOff, ManualOff, PlayerStart, MidiClock, MidiStart, Mix,
    MidiStop, MidiContinue, MidiSongPosition, SeqPlay, SeqStop, SeqQueue, SeqPreview, SeqSongJump, DrumPreview,
    DigiPreview, DigiStop,  // preview of a Digidrum slot (M6, B9, D-154)
    DmaPreview, DmaStop,  // preview of a DMA slot (M6, B8, D-153)
    Program,              // Program Change (M6, B10, D-156)
    Pressure, NotePressure, MpeBend, MpePressure, MpeSlide,  // MIDI expression (D-432)
};
// Event of the next render (Impl::push). a, b and value carry its arguments (note and velocity, pattern, slot, Program
// Change, MPE channel; semitones of a bend, cents of the wheel, accent of a preview, pressure, MPE channel of a note);
// mix: settings of a Mix event.
struct Event {
    uint32_t offset = 0;  // sample of the block
    uint32_t order = 0;   // rank of arrival: keeps the order of the events at the same sample
    EventType type = EventType::NoteOn;
    int16_t a = 0, b = 0;
    int16_t track = -1;  // target track (notes, bend, wheel); -1: keyboard playing or all the tracks
    double value = 0;
    TrackMix mix{};
};

// Wanted state of a chip channel (registers of one channel).
struct Wanted {
    uint16_t tone = 0;   // tone period, 0 to 4095
    uint8_t noise = 0;   // noise period (R6)
    bool toneOn = false, noiseOn = false;
    uint8_t volume = 0;  // 5 bits, bit 4 = envelope
    uint16_t envPeriod = 0;
};

// One voice (Impl::voices, Engine::kVoices of them): the sound frozen at its attack (setupVoice), its pitch, the
// cycles of its internal events, and in 16-channel mode its contribution to the buses. A voice is reused by the next
// note; `on` false: silent and free.
struct Voice {
    bool on = false;
    VoiceTag tag = VoiceTag::Manual;
    int track = -1;
    int note = -1;     // MIDI note (whole part; the fraction is in `fine`)
    uint64_t age = 0;  // rank of the attack (Impl::stamp)
    int chip = 0, channel = 0;  // 3-channel: chip and channel

    // Sound frozen at the attack. volBase: 4-bit volume of the note; vol4: that of the current frame (table, Chip
    // ADSR, LFO); shape: R13; slot: Digidrum or Chipdrum slot, or DMA slot.
    Generator gen = Generator::Chip;
    bool toneOn = true, noiseOn = false, envVolume = false;
    uint8_t noisePeriod = 8, volBase = 13, vol4 = 13, shape = 8, sidMin = 0, slot = 0;
    uint16_t envPeriod = 12800;
    double sidRatio = 1, vibCents = 0, vibHz = 6;
    // Timer waves (D-151): programmable SID wave (0 steps: square) and Sync Buzzer shapes, position in each; R13 shape
    // of the track (step at 0, other generators). Square of the menu (D-411): sqSteps interrupts per period (0: the
    // prototype's square, toggled at each one), high over the first sqHigh.
    std::array<uint8_t, kSidWaveSteps> sidWave{};
    std::array<uint8_t, kBuzzSteps> buzz{};
    uint8_t sidWaveLen = 0, wavePos = 0, buzzLen = 0, buzzPos = 0, trackShape = 8;
    uint8_t sqSteps = 0, sqHigh = 1;
    // Buzzer (D-152): tuned envelope (ratio to the frequency of the note, fine tune included; 0: period of the
    // track), YM Pad.
    double envTune = 0;
    bool ymPad = false;
    bool ornOn = false, ornLoop = true;
    uint8_t ornLen = 8, ornRate = 1;
    std::array<int8_t, kOrnamentSteps> ornPitch{};
    std::array<uint8_t, kOrnamentSteps> ornNoise{};
    std::array<uint8_t, kOrnamentSteps> ornVol{};
    double accent = 1;     // 16-channel: velocity, audio gain
    double digiLevel = 1;  // 3-channel: accent of the digidrums
    std::array<uint8_t, 16> digiMap{};  // 3-channel, digiLevel < 1: sample value → attenuated value (D-200)
    double drumTr = 1;     // pitch factor of a drum hit from the OCT and TON rows (D-098)

    // ADSR: affine value per stage, aRef at cycle aT, slope per cycle. adsrChip: it scales the 4-bit volume at each VBL
    // frame (Chip ADSR, always in 3-channel mode except for DMA); otherwise it is a smooth gain on the buses.
    bool adsrOn = false, adsrChip = false;
    uint8_t stage = 0;
    double aRef = 1, aSlope = 0;
    uint64_t aT = 0, stageEnd = kNever;
    double atk = 0, dcy = 0, rel = 0, sus = 1;  // durations in cycles

    // Pitch. fine: fraction of a semitone (track arpeggio on a slid base). fCur, fTarget, f0: current pitch, target
    // and start of the slide, in Hz. tp: tone period of the note and its slide; tpEff: the period of the frame (table,
    // vibrato), 0 for tp. timerHz: frequency asked of the effect timer.
    double fine = 0;
    double fCur = 440, fTarget = 440, f0 = 440;
    uint16_t tp = 284, tpEff = 0;
    double timerHz = 0;

    // Slide: glide steps left out of glideN, at glideHz steps per second; step k is dated glideStart + ⌈k × clock /
    // glideHz⌉ (glideIndex: steps done).
    uint32_t glide = 0, glideN = 0, glideHz = 0, glideIndex = 0;
    SlideCurve curve = SlideCurve::Linear;
    uint8_t slcOv = 0;  // curve of the CRV row for the next slide of the sequencer
    uint32_t seed = 1;
    uint64_t glideStart = 0, nextGlide = kNever;
    // Fraction of a slide step begun when a slide is interrupted: the next slide of the voice resumes it
    // (vblP of the prototype, which only restarts from zero when a note is triggered).
    double glidePhase = 0;

    // VBL frames (modulation, or Chipdrum frames in 3-channel mode): frame k at vblStart + ⌈k × clock / vblHz⌉, no
    // drift (D-052). vibPhase: phase of the vibrato (0 to 1); ornPos, ornFrm: step of the table and frame within it.
    bool vblOn = false;
    uint32_t vblHz = 50;
    uint64_t vblStart = 0, vblIndex = 0, nextVbl = kNever;
    double vibPhase = 0;
    int ornPos = 0, ornFrm = 0;
    // Table and vibrato before the last frame, chained instrument that sounds (0: none): what the next note of an
    // arpeggio takes over (ModCarry).
    int ornPrevPos = 0, ornPrevFrm = 0;
    double vibPrev = 0;
    // LFO (D-257): settings, phase (0 to 1) and value of the Random shape, also before the last frame (ModCarry);
    // what it adds at this frame: noise period steps, factors of the envelope period and of the SID ratio, pan.
    TrackLfo lfo;
    double lfoPhase = 0, lfoRand = 0, lfoPrev = 0, lfoRandPrev = 0;
    int lfoNoise = 0;
    double lfoEnv = 1, lfoRatio = 1, lfoPan = 0;
    uint8_t linked = 0;  // chained instrument that sounds (1 to 32), 0: none
    // Effects of the CMD row (D-338): what the slides added since the attack (4-bit volume steps, semitones of the
    // envelope frequency, noise period steps upwards) with the factor of the envelope period, and the fine tune of the
    // track in semitones (Fxx).
    double slVol = 0, slEnv = 0, slNoise = 0, slEnvK = 1, cmdFine = 0;
    // MIDI expression (D-432): MPE member channel of the note (0: none), its own bend in semitones, its pressure (0 to
    // 1, polyphonic aftertouch or MPE), its MPE slide (CC74, 0 to 1, 0.5 neutral) and the factor of the envelope
    // period that the wheel, the pressure and the slide give (Envelope targets).
    uint8_t mpeCh = 0;
    double mpeBend = 0, press = 0, slide = 0.5, exprEnvK = 1;
    bool ornNoiseOn = false;
    // Instrument sequences (D-148): rows, pitch settings, sound of the note that the GEN, MIX and NP rows
    // replace for the duration of a step.
    SeqRows rows;
    bool rowsOn = false;
    uint8_t fixedNote = 60, link = 0;
    uint16_t matrix = kPitchMatrixDefault;
    bool hardSync = false;
    double detune = 0, analog = 0;  // semitones: detune of the track, analog draw of the note
    Generator baseGen = Generator::Chip;
    bool baseTone = true, baseNoise = false, baseEnv = false;
    uint8_t baseNoisePeriod = 8;
    uint16_t baseEnvPeriod = 12800;
    bool genOv = false, mixOv = false, npOv = false;  // current step replaced by GEN, MIX, NP
    bool chainNext = false;  // last step of a table in Hold played: the chained instrument takes the next frame
    uint16_t envEff = 0;  // envelope period driven by the pitch (matrix, Buzzer target), 0: envPeriod
    bool wasMod = false;  // pitch modulated at the previous frame (the wheel can come back to zero)
    const ChipDrumFrame* frames = nullptr;  // register drum (Chipdrum in 3-channel mode): its frames in the kit
    int frameCount = 0, framePos = 0;

    // Effect timer (mode frozen at the attack, D-052). Mfp: the quantised setting and the timer model; Free: next
    // interrupt and period in fractional cycles. nextIrq: cycle of the next interrupt. sidHigh: the SID square is on
    // its high volume; dpos: position in the sample of a drum (sample; sampleLen: its length, or the End of a
    // Digidrum, D-443).
    bool timerOn = false;
    bool fxStart = false;  // timer started since the last frame of the recorder (RegisterFrame::Effect::restart)
    TimerMode timerMode = TimerMode::Mfp;
    MfpTimer mfp;
    MfpSetting mfpSet{};
    double freeNext = 0, freePeriod = 0;
    uint64_t nextIrq = kNever;
    bool sidHigh = true;
    uint32_t dpos = 0;
    const uint8_t* sample = nullptr;
    uint32_t sampleLen = 0;
    // Sustain loop of a Digidrum (D-154): from loopStart to loopEnd (its Start and End, D-443) while the note is held.
    bool looping = false;
    uint32_t loopStart = 0, loopEnd = 0;
    // DMA voice (D-153): 8-bit sample of the bank, read step (frequency of the note / that of the root note),
    // next frame and duration of a frame in internal steps, left and right values of the frame played
    // (±DmaBank::kScale), gain of the volume tables and of the Chip ADSR (VBL frames).
    const int8_t* pcm = nullptr;
    uint32_t pcmFrames = 0;
    bool pcmStereo = false, pcmLoop = false;
    double pcmPos = 0, pcmStep = 1, pcmRootHz = 261.6255653005986, pcmNext = 0, pcmPeriod = 1;
    double pcmL = 0, pcmR = 0, pcmGain = 1;
    uint32_t pcmStart = 0;   // first frame read at the attack (Start, D-444, then Oxx, D-338); pcmFrames: its End
    uint64_t dmaSerial = 0;  // rank of the start of the DMA voice (DmaFrame of the recorder: the most recent)

    // Fade: fadeLen steps of 1 / 48 000 s (D-051), the amplitude fadeAmt losing 1/64 per step.
    int fadeLen = 0, fadeStep = 0;
    double fadeAmt = 1;
    uint64_t fadeStart = 0, nextFade = kNever;

    // To pass to the channel: envelope restart (R13), tone reset (16-channel); envRetrig: rewrite of R13 alone
    // (Rxx command, D-150).
    bool restart = false, toneRestart = false, envRetrig = false;

    // Buses of the 16-channel mode: contribution cL + sL × (t − busT) at step t. cM and sM: mono contribution to the
    // separate output of track busTrack.
    double cL = 0, cR = 0, sL = 0, sR = 0, cM = 0, sM = 0;
    // Mixer meter: contribution seen by the oscilloscope of track meterTrack; cMeterD: half the difference of the
    // right and left sides, a stereo DMA sample's alone (D-457).
    double cMeter = 0, cMeterD = 0;
    int8_t meterTrack = -1;
    uint64_t busT = 0;
    int busTrack = -1;
    uint8_t idx = 0;      // 5-bit output index of the channel (level in tab1)
    double gainBase = 0;  // accent × drum gain × fade × mixer (without ADSR or panning)
    double gainR = 0;     // gainBase of the right side: that of a stereo DMA sample has its own fader (D-459)
    double kL = 0, kR = 0;  // gainBase and gainR × panning of each bus
    double gS = 0;  // send of the delay (D-299): the contribution on the send buses is that of the buses × gS
    // Echo of the notes (D-299): entry of the note in the echoes (0: none), rank of the repeat that the voice plays.
    uint32_t echoId = 0;
    uint8_t echoRep = 0;

    bool fading() const noexcept { return fadeLen > 0; }
    bool smoothAdsr() const noexcept { return adsrOn && !adsrChip; }
    // Cycle of the next internal event of the voice (kNever: none).
    uint64_t nextEvent() const noexcept {
        return std::min({nextGlide, nextVbl, nextIrq, stageEnd, nextFade});
    }
};

// Note whose echo is on (D-299): what its repeats play, the samples of its attack and of its release (kNever while it
// is held), the time between two repeats, the next repeat to attack and the next to release.
// src, dst: track of the note and of its repeats; ins: its instrument (0: the sound of the track); gen, vol, note,
// level, drumTr: the other arguments of its attack (setupVoice); voice: the voice that plays it. Times in samples.
struct EchoNote {
    uint32_t id = 0;  // 0: free
    int src = 0, dst = 0, ins = 0, gen = -1, vol = 0, voice = 0;
    double note = 0, level = 1, drumTr = 1;
    uint64_t start = 0, end = kNever;
    double period = 1;
    int repeats = 0, drop = 0, nextOn = 1, nextOff = 1;
};

// Registers last written to a chip of the 3-channel modes (applyChip writes only what changes) and the owners of the
// shared registers (D-050). r7: mixer, bit ch = tone off, bit 3 + ch = noise off (0x3f: all off, as at reset).
struct ChipCache {
    uint16_t tone[3] = {};
    uint8_t volume[3] = {};
    uint8_t r7 = 0x3f;
    uint8_t r6 = 0;
    bool noiseOn[3] = {};
    int noiseOwner = -1;  // channel whose noise period is in R6
    int envOwner = -1;    // channel that drives R11-R13
    uint16_t envPeriod = 0;
    uint8_t shape = 0xff;  // last R13 written (0xff: none yet)
    uint32_t mute = 0;     // channels muted by mute or solo (bit per channel)
};

// Keyboard arpeggio: idx position in the list of notes (dir: direction in UpDown), cur: step of the table shown
// (arpStep), lastSrc: held note of the last step (replayed while the last note is released), phase: fraction of a
// step at the Hz cadence, clockTicks: half pulses counted (D-289), fresh: a key pressed (or a MIDI note) since the
// last step, which starts a note of its own (D-416).
struct ArpState {
    int idx = -1, dir = 1, cur = -1;
    int lastSrc = -1;
    bool fresh = false;
    int track = -1;  // track of the held notes: -1 for keyboard playing, otherwise the targeted focused track
    double phase = 0;
    int clockTicks = 0;
};

// Curve of a Buzzer or Noise slide (Kxx, Qxx; 16-channel mode, D-394): from cycle start, over len cycles (the steps to
// the next command of its kind), the offset of the notes goes along the shape of the CRV row by `total` in all (the
// distance the speed covers there in a straight line); 0: Linear, the speed per frame.
struct SlideCurveRun {
    int curve = 0;
    uint64_t start = 0, len = 1;
    double total = 0;
    uint32_t seed = 1;  // Random walk
};

// Sequencer track during playback (channel fields of the worklet: offIn, cutIn, rt, arp*, polyLeft…). The
// countdowns are in host samples, as in the prototype.
struct LaneState {
    // Countdowns of the gate (offIn) and of CUT (cutIn); tiedHold: the note is held by TIE or SLD; slidePend: the
    // note carries SLD, the next note of the track is reached by a slide.
    int64_t offIn = 0, cutIn = 0;
    bool tiedHold = false, slidePend = false;
    // RTG retriggers (D-101: the sound of the step, level of the step × 0.92).
    int rtLeft = 0;
    int64_t rtPeriod = 0, rtClk = 0;
    double rtNote = 0, rtLevel = 1, rtDrumTr = 1;
    int rtGen = -1;
    uint8_t rtMix = 0;
    // Instruments (INS row, D-147): that of the sounding note (ties, slides), of the RTG retriggers, of the arpeggio.
    int ins = 0, rtIns = 0, arpIns = 0;
    // Note of the last attack, that the held steps strike again (RTG) or arpeggiate (ARP), D-343, D-345.
    double heldNote = 0;
    // Commands of the row (CMD row, D-150): countdown of Rxx; vibrato held by V00 until the next Vxx (D-254, VIB row
    // since D-341); LFO and modulation table held by L00 and M00, arpeggiator run by J01 (LFO, MOD and ARP rows,
    // twelfth DAW test, items 142 to 144).
    bool vibOff = false, lfoOff = false, modOff = false, arp = false;
    // Pattern track the switches were last read in: another one (a new pattern, Play) takes the state of its last
    // switch before the step (laneSwitches). Compared, never read.
    const SeqLane* switchLane = nullptr;
    int64_t envIn = 0;
    // Effects of D-338: speeds of the slides (Axy, Bxy, Nxy: x - y per tick), fine tune in semitones (Fxx) and
    // envelope shape (Exx, 0: that of the sound), each until the next command of its kind; DMA offset of the row in
    // frames (Oxx); countdowns of the delayed step (Dxx, with the rest of the row after it) and of the cut (Cxx).
    int volSpeed = 0, envSpeed = 0, noiseSpeed = 0;
    SlideCurveRun envCurve, noiseCurve;  // Kxx, Qxx (D-394)
    // Gxx (D-393): the level of the step glides from vgFrom to vgTo, that of the next step, over vgLen cycles from
    // cycle vgStart (the step).
    bool vg = false;
    double vgFrom = 1, vgTo = 1;
    uint64_t vgStart = 0, vgLen = 1;
    double fine = 0;
    uint8_t shape = 0;
    uint32_t offset = 0;
    int64_t delayIn = 0, cutCmdIn = 0;
    double delayRest = 0;
    // Chord voices (D-102): steps left and gate of the voices of a step.
    std::array<int, kSeqChordVoices> polyLeft{};
    std::array<int64_t, kSeqChordVoices> polyOff{};
    int64_t polyDone = -1;
    // Track arpeggio: arpNote, root of the step (-1: none); arpIdx and arpDir, position in the list; arpClkT, half
    // pulses counted; arpFx, arpLvl, arpMix: generator, level and SRC of the step; arpHold: suspended on the root (ARP
    // 0 on a tied step); arpLastN: last root played (arpLastSet), kept for the release.
    int arpNote = -1, arpIdx = -1, arpDir = 1, arpClkT = 0, arpFx = -1;
    double arpPhase = 0, arpLvl = 1;
    uint8_t arpMix = 0;
    bool arpHold = false;
    bool arpLastSet = false;
    double arpLastN = 0;
    // Slide of the root under the arpeggio (ARP + SLD): interval in semitones, steps in all and left, curve, current
    // offset, steps per second and phase of the next step.
    int arpGl = 0, arpGlN = 0, arpGlLeft = 0, arpGlCv = 0;
    double arpGlNow = 0, arpGlHz = 0, arpGlP = 0;
    uint32_t rand = 1;  // draws of the Random arpeggio, reseeded at each step (D-104)
};

// Transport and position of the sequencer.
struct SeqState {
    bool on = false;
    // Length of the pattern played, its current step, and steps since the start of the playback (-1 before the first).
    int len = 16, step = 15;
    int64_t stepAbs = -1;
    // Internal clock: phase advanced per sample, in steps (BPM) or in ticks (Ticks); dur: length of the current step.
    double phase = 1, dur = 1;
    int pattern = 0, queued = -1;  // pattern played (≥ kSeqRestPattern: a Rest or NoteOff entry), queued (-1: none)
    // Song: entry played, repeats of it done, entry queued (-1: none).
    int songPos = 0, songRep = 0, songQueued = -1;
    // Extended Song (D-158): the queued entry is the one after the end (restart); current jump and jumps done.
    bool songWraps = false;
    int loopAt = -1, loopCount = 0;
    // MIDI clock: external transport (Start/Stop/Continue), pulses counted, smoothed duration of a pulse.
    bool run = false;
    int clockCount = 10;  // half pulses counted in the current step (D-422)
    // Ticks (D-149): current speed, that of the song at start, changed by Sxx.
    int speed = 6;
    // BPM cadence: tempo set by the last Txx (D-243), 0: none (that of the Song entry or of the song).
    int cmdBpm = 0;
    double spt = 1000;       // MIDI clock: smoothed samples per pulse
    uint64_t lastClock = 0;  // sample of the last pulse
    // Host sync: end of the playback without loop reached going forward (absolute step); the sequencer is silent as
    // long as the host plays beyond it, and starts again if the host goes back. -1: none.
    int64_t parkedAt = -1;
};

// Metronome (metroBeep of the prototype): sine of 1 760 Hz (first beat) or 880 Hz, gain 0.4 or 0.24 decaying
// exponentially towards 0.0001 in 45 ms, stopped at 50 ms; added to the main output, outside the chip and outside
// the master volume (the prototype played it straight to the audio output).
struct Click {
    bool on = false;
    double phase = 0, inc = 0, gain = 0, mul = 1;  // sine phase and step; gain, multiplied by mul at each sample
    uint32_t t = 0, ramp = 0, len = 0;             // samples played, of decay, in all
};
}  // namespace detail
using namespace detail;

// State of an engine, owned by Engine (impl_) and used only from its rendering thread. The functions follow the
// order of a note's life: ADSR, pitch, timer, fade, attack (setupVoice), internal events, channel state, 16-channel
// buses, 3-channel chips, recorder, mixer, allocation and playing, echo, keyboard arpeggio, events, render.
struct Engine::Impl {
    EngineConfig config{};
    bool prepared = false;
    GlobalParams global{};
    std::array<TrackParams, kTracks> tracks{};
    KitParams kitParams = kKitBanks[0].kit;
    // Preview of an instrument of the kit (dprev of the worklet): slot (-1: nothing) and position at 7 800 Hz.
    int previewSlot = -1;
    double previewPos = 0;
    DrumKit kit;
    // Digidrum samples: 4-bit values, kMaxUserSample allocated per slot by the constructor, userLen of them used.
    std::array<std::vector<uint8_t>, kDigidrumSlots> user;
    std::array<uint32_t, kDigidrumSlots> userLen{};
    std::array<DigidrumSlot, kDigidrumSlots> digiSlots{};  // rate, root note, fine tune, loop (D-154)
    // Preview of a Digidrum slot (editor): slot (-1: nothing), position and step per output sample; looping: the
    // slot's Loop was on during the preview, which stops when Loop goes off (D-449).
    int digiPreviewSlot = -1;
    double digiPreviewPos = 0, digiPreviewInc = 0;
    bool digiPreviewLooping = false;

    std::array<Voice, kVoices> voices{};
    std::array<VirtualChannel, kVoices> vch{};  // channel of each voice (16-channel mode)
    std::array<uint8_t, 128> manualCut{};       // live notes cut by the sequencer, per MIDI note (D-431)
    uint64_t stamp = 0;                         // attacks so far (Voice::age)
    std::array<double, kTracks> bend{};   // pitch bend of each track, in semitones
    std::array<uint8_t, kTracks> program{};  // instrument chosen by Program Change (0: the sound of the track, D-156)
    std::array<double, kTracks> wheel{};  // wheel of each track, 0 to 1 (D-432)
    std::array<double, kTracks> press{};  // channel pressure of each track, 0 to 1 (D-432)
    // MPE member channels 1 to 15 (D-432): the last bend, pressure and slide of each, which its next notes start with.
    static constexpr int kMpeChannels = 16;
    struct MpeChannel {
        double bend = 0, press = 0, slide = 0.5;
    };
    std::array<MpeChannel, kMpeChannels> mpe{};
    int mpeNoteCh = 0;  // MPE channel of the note being attacked (setupVoice)

    // Live mixer. panStep: panning of the PAN row of the current step (NaN: that of the mixer); panNow: the panning
    // that panL and panR give, from which the LFO moves a voice (D-257).
    std::array<double, kTracks> live{}, panL{}, panR{}, panStep{}, panNow{};
    std::array<double, kTracks> liveR{};  // the right side of a stereo DMA sample (D-459), otherwise live
    std::array<double, kTracks> sendNow{};  // send of each track to the delay (D-299)

    // Arpeggio played on the keyboard: notes held (velocity per note, list in the order of pressing). heldSrc: track
    // whose channel holds the note (per-channel routing), -1 for the keyboard of the focused track (Omni, Layer).
    ArpState arp;
    std::array<int, 128> heldVel{};
    std::array<uint8_t, 128> heldList{};
    std::array<int8_t, 128> heldSrc{};
    int heldCount = 0;
    bool arpWasEnabled = false;
    ProtoXorshift arpRng;  // Random keyboard arpeggio, the prototype's generator
    Rng64 rng;             // the plugin's own draws
    IrqJitter jitter;
    MfpClockMap mfpMap;

    // Events of the next block.
    std::array<Event, kMaxEvents> events{};
    uint32_t eventCount = 0, eventOrder = 0;
    uint64_t dropped = 0;

    // Time.
    uint64_t samples = 0;  // samples rendered since the reset
    uint64_t base = 0;     // sample where the time base of the current mode starts (outputs reset)
    uint64_t genPos = 0;   // 16-channel: next internal step to compute

    // 16-channel mode: buses and output.
    std::array<double, 128> tab1{};  // level of one channel alone per 5-bit index, 1 = index 31 (D-049)
    // Steps (dL, dR) and changes of slope (sL, sR) written by the voices, indexed by internal step from genPos; the
    // integrated buses (bufL, bufR) from step bufBase, not yet consumed by the output chain; vL, vR: value of the
    // last integrated step; sigL, sigR: slope in progress.
    std::vector<double> dL, dR, sL, sR;
    std::vector<float> bufL, bufR;
    uint64_t bufBase = 0;
    double vL = 0, vR = 0, sigL = 0, sigR = 0;
    bool slopeWritten = false;  // a slope has been written in sL / sR since the last render
    ymcore::TickStreamOutput outL, outR;
    // Separate outputs of the tracks (16-channel): one mono bus per prepared track.
    uint16_t trackMask = 0;
    std::array<std::vector<double>, kTracks> dT, sT;
    std::array<std::vector<float>, kTracks> bufT;
    std::array<double, kTracks> vT{}, sigT{};
    std::array<bool, kTracks> wroteT{};
    std::array<ymcore::TickStreamOutput, kTracks> outT;
    std::vector<float> scratch;  // output prepared but not wanted in this render
    // Audio delay (D-299), live in 16 channels while it is on: send buses beside the left and right ones, with the
    // same steps × the send of the track of each voice and the same output chain, joined in silence when the delay
    // goes live; their output at the host rate (sendL, sendR), the echoes added to the mix before the Master.
    bool sendLive = false;
    std::vector<double> dSL, dSR, sSL, sSR;
    std::vector<float> bufSL, bufSR;
    double vSL = 0, vSR = 0, sigSL = 0, sigSR = 0;
    ymcore::TickStreamOutput outSL, outSR;
    std::vector<float> sendL, sendR;
    detail::StereoDelay delay;

    // 3-channel modes.
    std::array<ymcore::ChipRenderer, 2> chips;
    std::array<ChipCache, 2> cache{};
    std::vector<float> chipBuf;
    // Separate outputs of the tracks (3-channel): one copy of the chip per track, which receives the same writes
    // except the volume of the channels of the other tracks, held at 0 (the track plays alone on the chip).
    static constexpr int kShadows = 8;  // tracks 1 to 8: four per chip
    std::array<ymcore::ChipRenderer, kShadows> shadow;
    std::array<uint32_t, kShadows> shadowMute{};
    std::array<std::array<uint8_t, 3>, kShadows> shadowVol{};  // volumes written to each copy
    double gain3 = 1;  // chip output (kMixTable / 32768) → unit of tab1
    std::vector<WriteRecord>* log = nullptr;
    // Register recorder (M6, B11, D-157): registers written to each chip (mirror of chipWrite), R13 written since
    // the last frame, and the take in progress (chip -1: none).
    std::array<std::array<uint8_t, 14>, 2> regs{};
    std::array<bool, 2> r13Written{};
    struct Capture {
        RegisterFrame* frames = nullptr;
        DmaFrame* dma = nullptr;  // STE DMA sound, frame by frame (or none)
        uint32_t capacity = 0, count = 0, hz = 50;
        int chip = -1;
        uint64_t start = 0, next = 0;  // cycle of the start, cycle of the next frame
        uint64_t dmaSerial = 0;        // start of the DMA voice that took the channel last (0: none)
        int16_t sync = -1;             // Zxx of the sequencer since the last frame (-1: none)
    } cap;
    uint64_t dmaSerials = 0;  // starts of the DMA voices
    std::vector<uint16_t> chipLevels;  // 5-bit levels of the channels of the rendered chip, per sample (meters)

    // STE DMA sound (M6, B8, D-153): sample bank, DMA voices (Generator::Dma) with the STE machine. 16-channel:
    // voices like the others on the buses. 3-channel: the voices of the DMA tracks (kDma3Voice and following) go
    // through the buses of the 16-channel mode (same chain as the chips: the sum afterwards equals the sum before,
    // the filters being linear), rendered by pcmHostL and pcmHostR and added to the chips.
    DmaBank dmaBank;
    std::vector<float> pcmHostL, pcmHostR;

    // Mixer meters (takeMeters): peak of a voice per track, oscilloscope of the sum of the voices of the track
    // (kScopeRate points per second, meterIndex: next point since the start of the time base). A stereo DMA sample
    // (D-457): meterPeak holds the peaks of the left side, meterPeakR those of the right one (the same for the other
    // voices), meterScopeD half the difference of the right and left sides, around meterScope (their mean).
    std::array<float, kTracks> meterPeak{}, meterPeakR{};
    std::array<std::array<float, kScopePoints>, kTracks> meterScope{}, meterScopeD{};
    uint32_t meterPos = 0;  // place of the next point in meterScope
    uint64_t meterIndex = 0;
    // 16-channel: steps of the voices per bucket (bucket j: seen from point meterIndex + j), level at the last point.
    // meterDD, meterLevelD: the same for the half differences of stereo samples, on the tracks where meterSide is set.
    std::array<std::vector<double>, kTracks> meterD, meterDD;
    std::array<double, kTracks> meterLevel{}, meterLevelD{};
    std::array<bool, kTracks> meterSide{};
    double meterTickInv = 1;  // 1 / meterTickStep()
    // 16-channel: first step of each bucket for the current render (meterBucket(t) = j ⇔ bucketStart[j] ≤ t <
    // bucketStart[j + 1]), last element without end. The loops of the voices so advance their bucket by comparison.
    std::vector<uint64_t> bucketStart;
    // 3-channel: samples of the points of the current render, levels of the channels of the rendered chip.
    std::vector<uint32_t> meterAt;
    uint32_t chipLevelCount = 0;

    // Sequencer (M5).
    const SeqData* seqData = nullptr;
    int songFirst = -1;  // first playable entry of the Song (not a jump), -1: none (D-158)
    const InstrumentBank* instruments = nullptr;  // M6 (D-147)
    SeqState seq;
    std::array<LaneState, kTracks> lanes{};
    // Countdowns of the tracks driven by deadline (D-133): laneSample only runs at the sample where the first
    // countdown reaches zero (laneDue), with the samples elapsed since its last run (laneLag); at each sample as
    // soon as a track arpeggiates. Any other read or write of the tracks first catches them up (lanesCatchUp): the
    // result is that of a run at each sample.
    int64_t laneLag = 0, laneDue = 0;
    std::vector<SeqLogRecord>* seqLog = nullptr;
    // Host sync: transport of the block, last half pulse (48 per quarter note, D-289) derived from its position.
    static constexpr int64_t kNoPulse = std::numeric_limits<int64_t>::min();
    HostTransport host{};
    int64_t hostPulse = kNoPulse;
    // MIDI clock: samples until the half pulse between two clocks (0: none), at half the smoothed clock period: for
    // the arpeggios of odd division (D-289) and the sequencer at 1/64 (D-422).
    double halfPulseIn = 0;
    bool clockChanged = false;  // sequencer clock changed: stop at the next block
    // Quantize to VBL: first sample (since the reset) that the sequencer has not yet taken into account.
    uint64_t qFrom = 0;
    // Metronome: current beep and starts in the block (sample, first beat).
    Click click;
    std::array<std::pair<uint32_t, bool>, 8> clickAt{};
    int clickCount = 0;
    // Render in progress: the sequencer computes the voices up to the current sample before acting (at()).
    const Outputs* rOut = nullptr;
    uint32_t rSeg = 0, rI = 0;
    uint64_t rStart = 0;  // sample (since the reset) of the start of the block
    // Echo of the notes (D-299): notes whose repeats are not over, next sample where one of them acts.
    static constexpr int kEchoNotes = 128;
    static constexpr uint64_t kEchoPoll = 64;  // samples between two looks at the notes still held
    std::array<EchoNote, kEchoNotes> echoes{};
    uint32_t echoSerial = 0;
    uint64_t echoDue = kNever;

    // ------------------------------------------------------------------------------------------------------
    bool studio() const noexcept { return global.mode == VoiceMode::Studio16; }
    // Frequency of A4 from which the notes are tuned (Tuning A4, D-429), kept within its range.
    double a4() const noexcept { return std::isnan(global.a4Hz) ? kA4Hz : std::clamp(global.a4Hz, kA4MinHz, kA4MaxHz); }
    int chipCount() const noexcept { return global.mode == VoiceMode::Dual3 ? 2 : 1; }
    ymcore::MasterClock clock() const noexcept { return config.render.chip.clock; }
    uint32_t hostRate() const noexcept { return config.render.hostRate; }
    // Tracks played: 16 in 16-channel mode; in 3-channel mode, four per chip, then the eight DMA tracks with the STE
    // machine.
    int trackCount() const noexcept { return studio() ? kTracks : dmaFirst() + (global.ste ? kDmaTracks : 0); }
    // DMA tracks in 3-channel mode (STE machine): those that follow the YM tracks, one voice each (kDma3Voice + rank).
    int dmaFirst() const noexcept { return 4 * chipCount(); }
    bool dmaTrack(int track) const noexcept {
        return !studio() && global.ste && track >= dmaFirst() && track < dmaFirst() + kDmaTracks;
    }
    static constexpr int kDma3Voice = 6;
    // Sample played by default (dmaSlot -1): the rank of the DMA track in 3-channel mode, that of the track modulo 8
    // in 16-channel mode.
    int dmaAutoSlot(int track) const noexcept { return (studio() ? track : track - dmaFirst()) & 7; }
    double dmaTickHz() const noexcept { return clock().hz() / 8.0; }
    // Steps of the buses at cycle c, never before those already computed (in 3-channel mode, the buses of the DMA
    // voices run ahead of the chip by the time of their output).
    uint64_t busTick(uint64_t c) const noexcept { return std::max(c / 8, genPos); }
    int chipOfTrack(int track) const noexcept { return chipCount() < 2 ? 0 : std::min(chipCount() - 1, track >> 2); }
    bool trackOut(int track) const noexcept { return track >= 0 && ((trackMask >> track) & 1u) != 0; }
    static bool hasBus(int track) noexcept { return track >= 0 && track < kTracks; }
    // Bend of a voice in semitones: the wheel of its track (when it drives the pitch, D-432), plus the fine tune of the
    // CMD row (D-338). An MPE note adds the bend of its channel (D-432).
    double pitchBendOf(int track) const noexcept {
        return track >= 0 && global.bendTarget == BendTarget::Pitch ? bend[size_t(track)] : 0.0;
    }
    double bendOf(const Voice& v) const noexcept { return pitchBendOf(v.track) + v.cmdFine + v.mpeBend; }
    // MIDI expression of a voice (D-432): the wheel of its track; its pressure, the higher of its channel's and its
    // own; what the two give to target t (the higher of those that drive it); whether the wheel, the pressure or the
    // MPE slide moves it, so that it needs its VBL frames. Drums have none.
    double wheelOf(const Voice& v) const noexcept { return v.track >= 0 ? wheel[size_t(v.track)] : 0.0; }
    double pressureOf(const Voice& v) const noexcept {
        return std::max(v.track >= 0 ? press[size_t(v.track)] : 0.0, v.press);
    }
    double expressOf(const Voice& v, ExpressTarget t) const noexcept {
        if (isDrum(v.gen)) return 0;
        return std::max(global.wheelTarget == t ? wheelOf(v) : 0.0, global.aftertouch == t ? pressureOf(v) : 0.0);
    }
    bool expressive(const Voice& v) const noexcept {
        if (isDrum(v.gen)) return false;
        return (global.wheelTarget != ExpressTarget::Off && wheelOf(v) > 0) ||
               (global.aftertouch != ExpressTarget::Off && pressureOf(v) > 0) || v.slide != 0.5 ||
               (global.bendTarget != BendTarget::Pitch && v.track >= 0 && bend[size_t(v.track)] != 0);
    }
    // Bend of the track of a voice when it drives target t, in semitones.
    double bendFor(const Voice& v, BendTarget t) const noexcept {
        return global.bendTarget == t && v.track >= 0 && !isDrum(v.gen) ? bend[size_t(v.track)] : 0.0;
    }
    // MPE slide of a voice, -1 to 1 (0 in the middle), for what GlobalParams::mpeSlide says it drives.
    double slideOf(const Voice& v, SlideTarget t) const noexcept {
        return global.mpeSlide == t && !isDrum(v.gen) ? std::clamp(v.slide * 2 - 1, -1.0, 1.0) : 0.0;
    }
    // Envelope targets, in octaves: up to one higher (half the period) at the top of the wheel or the pressure; the
    // slide one either way; the bend by its semitones.
    double exprEnvFactor(const Voice& v) const noexcept {
        const double s = expressOf(v, ExpressTarget::Envelope) + slideOf(v, SlideTarget::Envelope) +
                         bendFor(v, BendTarget::Envelope) / 12;
        return s != 0 ? std::pow(2.0, -s) : 1.0;
    }
    // LFO target: its depth from none at rest to the depth of the track at the top.
    double exprLfoAmount(const Voice& v) const noexcept {
        const bool driven = global.wheelTarget == ExpressTarget::Lfo || global.aftertouch == ExpressTarget::Lfo;
        return driven ? expressOf(v, ExpressTarget::Lfo) : 1.0;
    }

    // First internal step of host sample s of the time base, by the rule of ChipRenderer::cycleAtSample (exact ratio
    // in Raw; ceil(s × clk8 / host rate), clk8 the whole steps per second, in RawBox and Retro).
    uint64_t tickAtSample(uint64_t s) const noexcept {
        const ymcore::MasterClock clk = clock();
        if (config.render.mode == ymcore::OutputMode::Raw) return mulDivCeil(s, clk.num, uint64_t(clk.den) * 8 * hostRate());
        return mulDivCeil(s, (clk.num / clk.den) / 8, hostRate());
    }
    // Cycles from the start of a cadence of hz events per second to its event k: ⌈k × clock / hz⌉, without drift.
    uint64_t periodCycles(uint64_t k, uint64_t hz) const noexcept {
        return mulDivCeil(k, clock().num, uint64_t(clock().den) * hz);
    }

    // ---------------------------------------------------------------------------------------------- ADSR
    // Level (0 to 1) at cycle c: a straight line in the attack, decay and release, which stops at the level that
    // ends the stage; constant when frozen (0) or in the sustain (3). Stages: 1 attack, 2 decay, 4 release.
    double adsrAt(const Voice& v, uint64_t c) const noexcept {
        if (v.stage == 0 || v.stage == 3) return v.aRef;
        double a = v.aRef + v.aSlope * double(c - v.aT);
        if (v.stage == 1) a = std::min(a, 1.0);
        else if (v.stage == 2) a = std::max(a, v.sus);
        else a = std::max(a, 0.0);
        return a;
    }
    // Entry into a stage with value a at cycle c (chains the zero-duration stages).
    void enterStage(Voice& v, uint8_t s, double a, uint64_t c) noexcept {
        for (;;) {
            v.stage = s;
            v.aRef = a;
            v.aT = c;
            v.aSlope = 0;
            v.stageEnd = kNever;
            if (s == 1) {
                if (a >= 1 || v.atk <= 0) {
                    s = 2;
                    a = 1;
                    continue;
                }
                v.aSlope = 1 / v.atk;
                v.stageEnd = c + ceilCycles((1 - a) * v.atk);
                return;
            }
            if (s == 2) {
                if (v.dcy <= 0 || a <= v.sus) {
                    s = 3;
                    a = v.sus;
                    continue;
                }
                v.aSlope = -(1 - v.sus) / v.dcy;
                v.stageEnd = c + ceilCycles((a - v.sus) * v.dcy / (1 - v.sus));
                return;
            }
            if (s == 4) {
                v.aSlope = a > 0 ? -a / v.rel : 0;
                v.stageEnd = a > 0 ? c + ceilCycles(v.rel) : c;
                return;
            }
            return;  // 0 (frozen) or 3 (sustain)
        }
    }

    // --------------------------------------------------------------------------------------------- Pitch
    // Pitch of the note in Hz (bend, detune and analog draw included), without slide: sets the tone period and the
    // rate of the generator (timer frequency, read step of a DMA sample). Returns the frequency.
    double applyPitch(Voice& v) noexcept {
        const double f = noteHz(double(v.note) + v.fine + bendOf(v) + v.detune + v.analog, a4());
        v.tp = tonePeriodFor(f, clock());
        switch (v.gen) {
        case Generator::Sid: v.timerHz = f * sidRate(v); break;
        case Generator::SyncBuzzer: v.timerHz = f * buzzRate(v); break;
        case Generator::Digidrum: {  // pitch of the note, transposed by OCT and TON of the step (D-098)
            // Rate, root note and fine tune of the slot (D-154; defaults: 7 800 Hz for C4).
            // The Tuning A4 moves it like the other notes (D-429): the slot's rate plays its root note at A4 = 440 Hz.
            const DigidrumSlot& d = digiSlots[v.slot & 7];
            v.timerHz = d.rateHz * std::pow(2.0, (double(v.note) + v.fine - d.root + d.cents / 100) / 12.0) *
                        (a4() / kA4Hz);
            if (v.drumTr != 1) v.timerHz *= v.drumTr;
            break;
        }
        case Generator::Chipdrum: v.timerHz = kDrumRate * v.drumTr; break;
        case Generator::Dma: v.pcmStep = f / v.pcmRootHz; break;  // resampled stream (D-153)
        case Generator::Chip: break;
        }
        return f;
    }

    // -------------------------------------------------------------------------------------------- Timer
    uint64_t mfpFloor(uint64_t cycle) const noexcept {
        // MFP cycle in progress at master cycle `cycle` (the setting writes are dated there).
        return mulDivFloor(cycle, uint64_t(kMfpClockHz) * clock().den, clock().num);
    }
    // Effect timer of the generators played by interrupts (SID, Sync Buzzer, Digidrum, sampled Chipdrum), started at
    // cycle c. Mfp: the quantised setting is written at the MFP cycle in progress, each interrupt is converted to the
    // master cycle that follows it, plus the jitter. Free: period clock / timerHz in fractional cycles, at least 8
    // (one internal step, D-052).
    void startTimer(Voice& v, uint64_t c) noexcept {
        v.timerOn = v.gen != Generator::Chip && v.gen != Generator::Dma && !v.frames;
        v.nextIrq = kNever;
        v.timerMode = global.timer;
        if (!v.timerOn) return;
        v.fxStart = true;
        if (v.timerMode == TimerMode::Mfp) {
            v.mfp.reset();
            v.mfpSet = mfpQuantize(v.timerHz);
            v.mfp.write(mfpFloor(c), v.mfpSet);
            if (v.mfp.running()) v.nextIrq = mfpMap.toMaster(v.mfp.nextFire()) + jitter.next();
        } else {
            v.freePeriod = v.timerHz > 0 ? std::max(8.0, clock().hz() / v.timerHz) : 0;
            v.freeNext = double(c) + v.freePeriod;
            if (v.freePeriod > 0) v.nextIrq = ceilCycles(v.freeNext);
        }
    }
    // New timerHz during the note (slide, vibrato, bend). Mfp: written only if the quantised setting changes, the
    // timer keeping its count (MfpTimer::write). Free: the fraction of the period in progress is kept.
    void retuneTimer(Voice& v, uint64_t c) noexcept {
        if (!v.timerOn) return;
        if (v.timerMode == TimerMode::Mfp) {
            const MfpSetting s = mfpQuantize(v.timerHz);
            if (s == v.mfpSet) return;
            const uint64_t before = v.mfp.nextFire();
            v.mfpSet = s;
            v.mfp.write(mfpFloor(c), s);
            if (!v.mfp.running()) v.nextIrq = kNever;
            else if (v.mfp.nextFire() != before || v.nextIrq == kNever) v.nextIrq = std::max(c, mfpMap.toMaster(v.mfp.nextFire()) + jitter.next());
        } else {
            const double period = v.timerHz > 0 ? std::max(8.0, clock().hz() / v.timerHz) : 0;
            if (period <= 0) {
                v.freePeriod = 0;
                v.nextIrq = kNever;
                return;
            }
            if (v.freePeriod > 0) v.freeNext = double(c) + (v.freeNext - double(c)) / v.freePeriod * period;
            else v.freeNext = double(c) + period;
            v.freePeriod = period;
            v.nextIrq = std::max(c, ceilCycles(v.freeNext));
        }
    }
    // Rate of the running timer: that of the MFP setting, or clock / period in Free mode; 0 when it is off.
    double timerActualHz(const Voice& v) const noexcept {
        if (!v.timerOn) return 0;
        if (v.timerMode == TimerMode::Mfp) return v.mfpSet.hz();
        return v.freePeriod > 0 ? clock().hz() / v.freePeriod : 0;
    }

    // -------------------------------------------------------------------------------------------- Fade
    // Fade of len steps (1 to 64) of 1 / 48 000 s; the voice stops at the last one.
    void startFade(Voice& v, int len, uint64_t c) noexcept {
        v.fadeLen = std::clamp(len, 1, 64);
        v.fadeStep = 0;
        v.fadeAmt = 1;
        v.fadeStart = c;
        v.nextFade = c + periodCycles(1, kFadeRate);
    }
    // Release of the note: the release stage of its ADSR, otherwise a fade of fadeLen steps.
    void relVoice(Voice& v, int fadeLen, uint64_t c) noexcept {
        echoRelease(v);
        if (v.gen == Generator::Dma && !v.pcmLoop) return;  // a DMA sample without loop plays to the end (D-153)
        if (v.adsrOn && v.rel > 0) {
            if (v.stage != 4) enterStage(v, 4, adsrAt(v, c), c);
        } else if (!v.fading()) {
            startFade(v, fadeLen, c);
        }
    }
    // Hard cut (hard killTag, allOff, manOff): the ADSR freezes, fade of len steps.
    void hardFade(Voice& v, int len, uint64_t c) noexcept {
        echoRelease(v);
        enterStage(v, 0, adsrAt(v, c), c);
        startFade(v, len, c);
    }

    // -------------------------------------------------------------------------------------------- Setup
    // 3-channel (D-200): the 4-bit volume whose level is the nearest, in decibels, to that of `vol` × `level`. A
    // track level or a velocity thus lowers the chip as much as the audio gain lowers a voice in 16-channel mode,
    // where the prototype scaled the 4-bit index (a logarithmic DAC: 10 dB quieter at the default settings).
    int attenuated(int vol, double level) const noexcept {
        vol = std::clamp(vol, 0, 15);
        if (level >= 1) return vol;
        if (level <= 0 || vol == 0) return 0;
        const double want = std::log(tab1[size_t(2 * vol + 1)] * level);
        int best = 0;  // volume 0 is silent: never the nearest to a level above 0
        double err = HUGE_VAL;
        for (int k = 1; k <= vol; ++k)
            if (const double e = std::fabs(std::log(tab1[size_t(2 * k + 1)]) - want); e < err) {
                err = e;
                best = k;
            }
        return best;
    }
    // A note played live that still sounds (not released, not fading) on voice v, which a sequencer, chord or echo
    // note is about to take (Indus, 25/10/2026 19:41, D-431): a key held, or held by Latch, before Play sits on the
    // voice of a track that the sequencer reserves once it plays. In 16 channels the note moves to a free voice of the
    // pool and goes on sounding, as a note played during playback would; otherwise (3-channel modes, pool full) it
    // is cut, and manualCut counts it so that the interface lets its key go.
    void displaceManual(Voice& v, uint64_t c) noexcept {
        if (!v.on || v.tag != VoiceTag::Manual || v.fading() || v.stage == 4) return;
        const int from = int(&v - voices.data());
        if (studio())
            for (int i = kLaneVoices + kChordVoices; i < kVoices; ++i) {
                if (voices[size_t(i)].on) continue;
                voices[size_t(i)] = v;
                vch[size_t(i)] = vch[size_t(from)];
                v.echoId = 0;  // its echo follows the note to its new voice
                touch(i, c);
                return;
            }
        if (v.note >= 0 && v.note < 128) ++manualCut[size_t(v.note)];
    }
    // gen ≥ 0: generator imposed by the FX row of the step; drumTr: transposition of the drums of the step (D-098);
    // note: fraction of a semitone possible (track arpeggio on a slid base); patch: sound of an instrument
    // (INS row, D-147) instead of that of the track.
    void setupVoice(Voice& v, double note, int vol, VoiceTag tag, int track, double level, uint64_t c, int gen = -1,
                    double drumTr = 1, const TrackPatch* patch = nullptr) noexcept {
        const TrackPatch& src = patch ? *patch : tracks[size_t(std::clamp(track, 0, kTracks - 1))].patch;
        const bool m3 = !studio();
        if (tag != VoiceTag::Manual) displaceManual(v, c);
        echoRelease(v);  // the note that the voice played ends here
        const int vol0 = vol;
        if (m3 && level < 1) vol = attenuated(vol, level);
        v.on = true;
        v.note = int(std::floor(note));
        v.fine = note - std::floor(note);
        v.slcOv = 0;
        v.tag = tag;
        v.track = track;
        v.age = stamp++;
        // CMD row of the track (D-338): the slides start again from the note's own values; the fine tune, the
        // envelope shape and the DMA offset of the track's commands apply from the attack.
        const LaneState* lane = seqVoice(tag, track) ? &lanes[size_t(track)] : nullptr;
        v.slVol = v.slEnv = v.slNoise = 0;
        v.slEnvK = 1;
        v.cmdFine = lane ? lane->fine : 0;
        // An MPE note starts with the bend, pressure and slide of its channel (D-432).
        v.mpeCh = tag == VoiceTag::Manual ? uint8_t(mpeNoteCh) : 0;
        v.mpeBend = v.mpeCh ? mpe[v.mpeCh].bend : 0;
        v.press = v.mpeCh ? mpe[v.mpeCh].press : 0;
        v.slide = v.mpeCh ? mpe[v.mpeCh].slide : 0.5;
        v.exprEnvK = 1;  // set by the frames of an expressive voice
        v.fadeLen = v.fadeStep = 0;
        v.fadeAmt = 1;
        v.nextFade = kNever;
        v.accent = m3 ? 1 : level;
        v.digiLevel = m3 ? level : 1;
        if (v.digiLevel < 1)
            for (int k = 0; k < 16; ++k) v.digiMap[size_t(k)] = uint8_t(attenuated(k, v.digiLevel));
        v.toneOn = src.toneOn;
        v.noiseOn = src.noiseOn;
        v.noisePeriod = uint8_t(std::clamp<int>(src.noisePeriod, 0, 31));
        v.envVolume = src.envelopeVolume;
        v.volBase = v.vol4 = uint8_t(std::clamp(vol, 0, 15));
        v.envPeriod = src.envPeriod;
        v.shape = uint8_t(src.envShape & 15);
        v.gen = gen >= 0 ? Generator(gen) : src.generator;
        // DMA sound (D-153): STE machine only; in 3-channel mode, the DMA tracks only play it, the others never.
        if (m3 && dmaTrack(track)) v.gen = Generator::Dma;
        else if (v.gen == Generator::Dma && (m3 || !global.ste)) {
            const Generator own = tracks[size_t(std::clamp(track, 0, kTracks - 1))].patch.generator;
            v.gen = own == Generator::Dma ? Generator::Chip : own;
        }
        if (v.gen == Generator::Dma) {
            v.accent = level;  // audio gain in both modes (no 4-bit volume)
            v.digiLevel = 1;
            v.toneOn = v.noiseOn = v.envVolume = false;
        }
        // Generator settings, vibrato, table (ornament), LFO and instrument sequences of the sound.
        v.sidMin = uint8_t(std::min<int>(src.sidMinVolume, 15));
        v.sidRatio = src.sidRatio > 0 ? src.sidRatio : 1;
        v.vibCents = src.vibratoOn ? src.vibratoCents : 0;
        v.vibHz = src.vibratoHz > 0 ? src.vibratoHz : 6;
        v.ornOn = src.ornamentOn;
        v.ornLen = std::min<uint8_t>(src.ornamentLength, kOrnamentSteps);
        v.ornLoop = src.ornamentLoop;
        v.ornRate = src.ornamentRate ? src.ornamentRate : 1;
        v.ornPitch = src.ornamentPitch;
        v.ornNoise = src.ornamentNoise;
        v.ornVol = src.ornamentVolume;
        v.ornNoiseOn = false;
        v.ornPos = v.ornFrm = 0;
        v.vibPhase = 0;
        v.ornPrevPos = v.ornPrevFrm = 0;
        v.vibPrev = 0;
        startLfo(v, src, c);
        v.linked = 0;
        v.tpEff = 0;
        v.sidHigh = true;
        v.wavePos = 0;
        v.dpos = 0;
        loadSequences(v, src);
        if (lane && lane->shape) v.trackShape = v.shape = lane->shape;  // Exx (D-338)
        primeBuzz(v);
        // Analog: a uniform draw within ±analogCents (53 random bits), in semitones.
        v.analog = src.analogCents > 0 ? (double(rng.next() >> 11) * 0x1.0p-53 * 2 - 1) * std::min(src.analogCents, double(kAnalogMaxCents)) / 100 : 0;
        // The sound of the note, which the GEN, MIX and NP rows replace step by step (rowsAt).
        v.baseGen = v.gen;
        v.baseTone = v.toneOn;
        v.baseNoise = v.noiseOn;
        v.baseEnv = v.envVolume;
        v.baseNoisePeriod = v.noisePeriod;
        v.baseEnvPeriod = v.envPeriod;
        v.genOv = v.mixOv = v.npOv = v.chainNext = false;
        v.envEff = 0;
        if (v.gen == Generator::SyncBuzzer) {  // pure buzzer: envelope alone
            v.envVolume = true;
            v.toneOn = v.noiseOn = false;
            if (v.envPeriod > 3200 && v.envTune <= 0) v.envPeriod = 32;  // "EP > 100 → 1" of the prototype, × 32
        }
        // Slot: the grid row for the sequencer hits, chord voices included (D-098).
        const bool seqHit = tag == VoiceTag::Sequencer || tag == VoiceTag::Chord;
        const int row = (v.note % 12 + 12) % 12 % 8;  // pitch class of the note, folded onto the eight slots
        v.slot = uint8_t(v.gen == Generator::Digidrum && !seqHit ? (src.digidrumSlot & 7) : row);
        v.drumTr = drumTr;
        // Chipdrum: VBL frames written to the registers in 3-channel mode, a 4-bit sample in 16-channel mode; Digidrum:
        // the user sample of the slot.
        v.frames = nullptr;
        v.frameCount = v.framePos = 0;
        if (v.gen == Generator::Chipdrum && m3) {
            const auto fr = kit.frames(v.slot);
            v.frames = fr.data();
            v.frameCount = int(fr.size());
            v.envVolume = false;
        }
        v.sample = nullptr;
        v.sampleLen = 0;
        if (v.gen == Generator::Chipdrum && !m3) {
            const auto s = kit.sample(v.slot);
            v.sample = s.data();
            v.sampleLen = uint32_t(s.size());
        } else if (v.gen == Generator::Digidrum) {
            v.sample = user[v.slot].data();
            v.sampleLen = userLen[v.slot];
        }
        // Digidrum: the part from Start to End (D-443), its sustain loop (D-154) replaying it while the note is held.
        v.looping = false;
        if (v.gen == Generator::Digidrum) {
            const DigidrumSlot& ds = digiSlots[v.slot & 7];
            const SampleSpan part = sampleSpan(ds.start, ds.end, v.sampleLen);
            v.sampleLen = part.end;
            v.dpos = part.start;
            v.looping = ds.loop && part.size() > 0;
            v.loopStart = part.start;
            v.loopEnd = part.end;
        }
        // DMA voice (D-153): sample of the track or of the instrument (-1: the one of the rank of the track), first
        // frame at the step of the attack: its part from Start to End (D-444), the Oxx offset counted from Start.
        v.pcm = nullptr;
        v.pcmFrames = 0;
        if (v.gen == Generator::Dma) {
            const int k = src.dmaSlot >= 0 ? (src.dmaSlot & 7) : dmaAutoSlot(track);
            const DmaSlot& d = dmaBank.slot(k);
            v.slot = uint8_t(k);
            v.pcm = dmaBank.data(k);
            const SampleSpan part = dmaBank.part(k);
            v.pcmFrames = part.end;
            v.pcmStereo = d.stereo;
            v.pcmLoop = d.loop;
            // At A4 = 440 Hz: the Tuning A4 moves the sample too (D-429); Fine plays it higher at its root (D-452).
            v.pcmRootHz = noteHz(double(d.root)) * std::pow(2.0, -d.cents / 1200);
            v.pcmPeriod = dmaTickHz() / kDmaRateHz[d.rate];  // internal steps per frame of the stream
            v.pcmNext = double(c) / 8.0;
            v.pcmStart = part.start + (lane ? lane->offset : 0);  // Start (D-444), then Oxx (D-338)
            v.pcmPos = double(v.pcmStart);
            v.pcmL = v.pcmR = 0;
            v.pcmGain = 1;
            v.dmaSerial = ++dmaSerials;
        }
        // ADSR (0/0/100/0: transparent). A drum hit plays to its end with its own shape (kit instrument or sample):
        // the ADSR of the track does not apply to it, in any mode (D-205; the interface greys it out).
        const double hz = clock().hz();  // durations converted from ms to master cycles
        v.atk = std::max(0.0, src.attackMs) / 1000 * hz;
        v.dcy = std::max(0.0, src.decayMs) / 1000 * hz;
        v.sus = std::clamp(src.sustainPct, 0.0, 100.0) / 100;
        v.rel = std::max(0.0, src.releaseMs) / 1000 * hz;
        v.adsrOn = !isDrum(v.gen) && (v.atk > 0 || v.dcy > 0 || v.sus < 1 || v.rel > 0);
        v.adsrChip = (m3 && v.gen != Generator::Dma) || src.adsrChip;  // DMA has no 4-bit volume
        if (v.adsrOn) enterStage(v, 1, v.atk > 0 ? 0 : 1, c);
        else {
            v.stage = 1;
            v.aRef = 1;
            v.aSlope = 0;
            v.aT = c;
            v.stageEnd = kNever;
        }
        // Envelope restarted, tone at the start of a high half-period.
        v.restart = true;
        v.toneRestart = true;
        const double f = applyPitch(v);
        v.fCur = v.fTarget = v.f0 = f;
        tuneEnv(v, f);
        v.glide = v.glideN = 0;
        v.nextGlide = kNever;
        v.glidePhase = 0;
        // VBL frames: at the attack, then every clock / VBL cycles.
        // The envelope driven by the pitch (matrix) also follows a slide: VBL frames.
        const bool modulated = ornActive(v) || (v.adsrOn && v.adsrChip) || v.vibCents > 0 ||
                               buzzerFollows(v) || lfoActive(v) || laneSliding(v) || expressive(v);
        v.wasMod = false;
        v.vblOn = v.frames != nullptr || (modulated && !isDrum(v.gen));
        v.vblHz = std::max<uint32_t>(1, global.vblHz);
        v.vblStart = c;
        v.vblIndex = 0;
        v.nextVbl = v.vblOn ? c : kNever;
        startTimer(v, c);
        // Digidrum without a sample: silence (the prototype cuts the voice).
        if (v.gen == Generator::Digidrum && v.sampleLen == 0) v.on = false;
        if (v.gen == Generator::Dma && v.pcmStart >= v.pcmFrames) v.on = false;  // empty slot, or offset past End
        // A note whose sound has its echo on schedules its repeats (the repeats themselves have none).
        v.echoId = 0;
        v.echoRep = 0;
        if (v.on && tag != VoiceTag::Echo && src.echo.on)
            echoAttack(v, src, patch, track, note, vol0, level, gen, drumTr, c);
    }

    // Exact duration of a slide step, in cycles.
    double glidePeriod(const Voice& v) const noexcept {
        return double(clock().num) / (double(clock().den) * std::max<uint32_t>(1, v.glideHz));
    }
    // Interruption of a slide in progress: the fraction of a step begun is kept for the next one.
    void stopGlide(Voice& v, uint64_t c) noexcept {
        if (v.glide > 0 && v.nextGlide != kNever) {
            const uint64_t last = v.glideStart + periodCycles(v.glideIndex, v.glideHz);
            v.glidePhase = c > last ? std::min(1.0, double(c - last) / glidePeriod(v)) : 0.0;
        }
        v.glide = 0;
        v.nextGlide = kNever;
    }

    // New pitch without a new attack (arpeggio during a release, Land, Hold): the slide stops; hard sync restarts the
    // square and the timer.
    void retune(Voice& v, double note, uint64_t c) noexcept {
        v.note = int(std::floor(note));
        v.fine = note - std::floor(note);
        const double f = applyPitch(v);
        v.fCur = v.fTarget = f;
        tuneEnv(v, f);
        stopGlide(v, c);
        v.tpEff = 0;
        if (v.hardSync) syncRestart(v, c);
        else retuneTimer(v, c);
    }

    // Pitch bend changed by `semis` semitones on a sounding voice. The start, the course and the target of a slide in
    // progress are bent alike, so the voice keeps its place in the slide (the prototype jumped to the bent target note
    // until the next slide step); the target pitch that the sequence frames read is bent too.
    void bendVoice(Voice& v, double semis, uint64_t c) noexcept {
        const double k = std::pow(2.0, semis / 12);
        v.f0 *= k;
        v.fCur *= k;
        v.fTarget *= k;
        if (v.glide > 0) {
            v.tp = tonePeriodFor(slideHz(v, PitchTarget::Square), clock());
            if (v.gen == Generator::Sid) v.timerHz = slideHz(v, PitchTarget::Timer) * sidRate(v);
            else if (v.gen == Generator::SyncBuzzer) v.timerHz = slideHz(v, PitchTarget::Timer) * buzzRate(v);
            else if (v.gen == Generator::Dma) v.pcmStep = slideHz(v, PitchTarget::Timer) / v.pcmRootHz;
            tuneEnv(v, v.fTarget);
        } else {
            tuneEnv(v, applyPitch(v));
        }
        retuneTimer(v, c);
    }

    void slideTo(Voice& v, int note, double stepSamples, uint64_t c) noexcept {
        slideTo(v, note, stepSamples, c, uint32_t(rng.next() >> 32));
    }
    // Slide to note in stepSamples host samples, curve of the CRV row (slcOv), draw seed (Random walk).
    void slideTo(Voice& v, int note, double stepSamples, uint64_t c, uint32_t seed) noexcept {
        stopGlide(v, c);
        const double f = noteHz(double(note) + bendOf(v) + v.detune + v.analog, a4());
        v.note = note;
        v.fine = 0;
        // Slide steps at 16 × VBL in 16-channel mode, at the VBL rate in 3-channel mode (D-052); upd: their number.
        const uint32_t vbl = std::max<uint32_t>(1, global.vblHz);
        v.glideHz = studio() ? vbl * 16 : vbl;
        const double upd = std::max(1.0, jsRound(stepSamples * v.glideHz / hostRate()));
        v.f0 = v.fCur;
        v.fTarget = f;
        tuneEnv(v, f);
        v.glideN = v.glide = uint32_t(upd);
        v.curve = SlideCurve(std::min<int>(v.slcOv, kSlideCurves - 1));
        v.seed = seed;
        // The first step comes earlier if the previous slide of the voice had begun its own.
        const uint64_t carried = uint64_t(std::llround(v.glidePhase * glidePeriod(v)));
        v.glidePhase = 0;
        v.glideStart = c - std::min(c, carried);
        v.glideIndex = 0;
        v.nextGlide = std::max(c, v.glideStart + periodCycles(1, v.glideHz));
        // The slide starts from the current pitch (the prototype keeps the period of the target note until the
        // first slide step: D-051).
        v.tp = tonePeriodFor(slideHz(v, PitchTarget::Square), clock());
    }

    // ----------------------------------------------------------------------------------- Internal events
    // One slide step: position t on the curve, pitch interpolated in ratio from f0 to fTarget (linearly if one of them
    // is not positive), quantised for Chromatic and Scale-locked; the last step lands on the target, except with the
    // Custom curve, whose last point may lie elsewhere.
    void glideStep(Voice& v, uint64_t c) noexcept {
        --v.glide;
        ++v.glideIndex;
        const double t = v.glideN ? 1.0 - double(v.glide) / double(v.glideN) : 1.0;
        const double sh = slideShape(t, v.curve, v.seed, global.slideTable);
        v.fCur = (v.f0 > 0 && v.fTarget > 0) ? v.f0 * std::pow(v.fTarget / v.f0, sh) : v.f0 + (v.fTarget - v.f0) * sh;
        v.fCur = slideQuantize(v.fCur, v.curve, global.scaleMask, global.scaleRoot, a4());
        if (v.glide == 0 && v.curve != SlideCurve::Custom) v.fCur = v.fTarget;
        v.tp = tonePeriodFor(slideHz(v, PitchTarget::Square), clock());
        if (v.gen == Generator::Sid) v.timerHz = slideHz(v, PitchTarget::Timer) * sidRate(v);
        else if (v.gen == Generator::SyncBuzzer) v.timerHz = slideHz(v, PitchTarget::Timer) * buzzRate(v);
        else if (v.gen == Generator::Dma) v.pcmStep = slideHz(v, PitchTarget::Timer) / v.pcmRootHz;
        retuneTimer(v, c);
        v.nextGlide = v.glide > 0 ? v.glideStart + periodCycles(v.glideIndex + 1, v.glideHz) : kNever;
        if (v.glide == 0) v.glidePhase = 0;
    }

    // One VBL frame of the voice (replay tick). A register drum plays its next frame. Otherwise, the modulation of the
    // frame: chained instrument, step of the table (pitch, noise, volume, sequence rows), Chip ADSR, LFO and vibrato,
    // then the period, timer and envelope period that result (matrixPitch when the pitch is modulated and the
    // matrix is not the default one or a PER or FIX step plays).
    // A voice without modulation receives its VBL frames from now on (a vibrato starts at phase 0): the wheel, and the
    // MIDI expression (D-432). Drums have none.
    void startFrames(Voice& v, uint64_t c) const noexcept {
        if (v.vblOn || v.frames || isDrum(v.gen) || v.track < 0) return;
        v.vblOn = true;
        v.vblHz = std::max<uint32_t>(1, global.vblHz);
        v.vblStart = c;
        v.vblIndex = 0;
        v.nextVbl = c;
    }
    // A frame moved by the MIDI expression (D-432). Volume: the wheel and the pressure raise the 4-bit volume towards
    // 15; the MPE slide and the bend (its whole Bend range) raise it towards 15 above the middle and lower it towards 0
    // below; the gain of a DMA voice alike, without 4-bit steps. Envelope: the factor of the envelope period.
    void express(Voice& v) const noexcept {
        if (isDrum(v.gen)) return;
        v.exprEnvK = exprEnvFactor(v);
        const double p = expressOf(v, ExpressTarget::Volume);
        const double b = global.bendRange > 0 ? bendFor(v, BendTarget::Volume) / global.bendRange : 0.0;
        const double s = std::clamp(slideOf(v, SlideTarget::Volume) + b, -1.0, 1.0);
        if (p <= 0 && s == 0) return;
        double vol = v.vol4, gain = v.pcmGain;
        vol += (15 - vol) * p;
        gain += (1 - gain) * p;
        if (s > 0) {
            vol += (15 - vol) * s;
            gain += (1 - gain) * s;
        } else {
            vol *= 1 + s;
            gain *= 1 + s;
        }
        v.vol4 = uint8_t(std::clamp(jsRound(vol), 0.0, 15.0));
        v.pcmGain = std::clamp(gain, 0.0, 1.0);
    }

    void vblEvent(Voice& v, uint64_t c) noexcept {
        ++v.vblIndex;
        v.nextVbl = v.vblStart + periodCycles(v.vblIndex, v.vblHz);
        if (v.frames) {  // register drum (3-channel)
            if (v.framePos >= v.frameCount) {
                v.on = false;
                return;
            }
            const ChipDrumFrame& fr = v.frames[v.framePos++];
            v.toneOn = fr.tone;
            v.noiseOn = fr.noise;
            v.noisePeriod = fr.noisePeriod ? fr.noisePeriod : 1;
            // Level and velocity of the hit lower the frame's 4-bit volume like any 3-channel note (D-200, D-286).
            v.vol4 = v.volBase = v.digiLevel < 1 ? v.digiMap[size_t(fr.volume & 15)] : fr.volume;
            if (fr.tonePeriod)
                v.tp = v.drumTr != 1 ? uint16_t(std::clamp(jsRound(fr.tonePeriod / v.drumTr), 1.0, 4095.0)) : fr.tonePeriod;
            return;
        }
        if (v.chainNext) {
            v.chainNext = false;
            relink(v, c);
        }
        if (v.vblIndex > 1) slideFrame(v, c);  // not at the attack's frame, as the slides of trackers (tick 0)
        // State before this frame, which an arpeggio note attacked at the same cycle takes over (grabMod).
        v.ornPrevPos = v.ornPos;
        v.ornPrevFrm = v.ornFrm;
        v.vibPrev = v.vibPhase;
        v.lfoPrev = v.lfoPhase;
        v.lfoRandPrev = v.lfoRand;
        const bool wantOrn = ornActive(v), wantVol = wantOrn;
        const bool chipEnv = v.adsrOn && v.adsrChip;
        double semis = 0;
        int delta = 0;  // PER row
        bool fix = false;
        const int op = std::min(v.ornPos, int(v.ornLen) - 1);
        if (wantOrn) {
            semis = v.ornPitch[size_t(op)];
            v.ornNoiseOn = v.ornNoise[size_t(op)] != 0;
            if (v.rowsOn || v.genOv || v.mixOv || v.npOv) {
                rowsAt(v, op, c);
                delta = v.rows.period[size_t(op)];
                fix = v.rows.fix[size_t(op)] != 0;
            }
        }
        double vs = 1;  // volume factor of the frame: table volume / 15, times the Chip ADSR
        if (wantVol) vs = double(std::min<int>(v.ornVol[size_t(op)], 15)) / 15;
        if (wantOrn || wantVol) {  // next step of the table every ornRate frames
            if (++v.ornFrm >= v.ornRate) {
                v.ornFrm = 0;
                if (++v.ornPos >= v.ornLen) {
                    v.chainNext = !v.ornLoop && v.link != 0;  // end of a table in Hold: chained instrument
                    v.ornPos = v.ornLoop ? 0 : v.ornLen - 1;
                }
            }
        }
        if (chipEnv) vs *= adsrAt(v, c);
        v.vol4 = (wantVol || chipEnv) ? uint8_t(std::clamp(jsRound(v.volBase * vs), 0.0, 15.0)) : v.volBase;
        v.pcmGain = (wantVol || chipEnv) ? std::clamp(vs, 0.0, 1.0) : 1.0;  // DMA voice: gain without 4-bit steps
        const double lfoSemis = lfoFrame(v);  // after the volume of the frame, which it moves (D-257)
        if (v.slVol != 0) {                   // volume slide of the CMD row (D-338)
            v.vol4 = uint8_t(std::clamp(jsRound(v.vol4 + v.slVol), 0.0, 15.0));
            v.pcmGain = std::clamp(v.pcmGain + v.slVol / 15, 0.0, 1.0);
        }
        express(v);  // wheel, pressure and MPE slide (D-432)
        const bool lfoPitch = lfoActive(v) && (v.lfo.target == LfoTarget::Pitch || v.lfo.target == LfoTarget::SidRatio);
        // Vibrato of the track (unless a V00 of its lane holds it, D-254), plus that of the wheel, the pressure or the
        // MPE slide (from the middle up), up to the Wheel depth: the deepest plays (D-432; no effect on the drums).
        const double own = seq.on && lanes[size_t(v.track)].vibOff ? 0.0 : v.vibCents;
        const double expr = std::max(expressOf(v, ExpressTarget::Vibrato), slideOf(v, SlideTarget::Vibrato));
        const double cents = own + std::max(0.0, expr) * global.expressCents;
        double vib = lfoSemis;  // the LFO's pitch goes with the vibrato
        if (cents > 0) {
            v.vibPhase = std::fmod(v.vibPhase + v.vibHz / v.vblHz, 1.0);
            vib += std::sin(v.vibPhase * 6.283185307179586) * cents / 100;
        }
        const bool mod = wantOrn || cents > 0 || lfoPitch;
        if (mod && (v.matrix != kPitchMatrixDefault || delta != 0 || fix)) {
            matrixPitch(v, semis, vib, delta, fix, c);
            v.wasMod = true;
        } else {  // no modulation, or the default matrix: the square and the timer move together
            semis += vib;
            v.tpEff = mod ? uint16_t(std::clamp(jsRound(v.tp * std::pow(2.0, -semis / 12)), 1.0, 4095.0)) : 0;
            if (mod || v.wasMod) {  // the timer follows: buzzer and SID vibrate (and come back when the wheel drops)
                const double fm = slideHz(v, PitchTarget::Timer) * std::pow(2.0, semis / 12);
                if (v.gen == Generator::SyncBuzzer) v.timerHz = fm * buzzRate(v);
                else if (v.gen == Generator::Sid) v.timerHz = fm * sidRate(v);
                else if (v.gen == Generator::Dma) v.pcmStep = fm / v.pcmRootHz;
                retuneTimer(v, c);
            }
            v.wasMod = mod;
            v.envEff = buzzerFollows(v) ? buzzerPeriod(v, 0, 0, false) : 0;
        }
    }

    // --------------------------------------------------------------------------- CMD row slides (D-338)
    // A voice that the sequencer plays (its track's commands apply to it).
    static bool seqVoice(VoiceTag tag, int track) noexcept {
        return (tag == VoiceTag::Sequencer || tag == VoiceTag::Chord) && track >= 0 && track < kTracks;
    }
    // Its track has a slide in progress (Axy, Bxy or Nxy with a speed, a Gxx glide): the voice needs its VBL frames.
    bool laneSliding(const Voice& v) const noexcept {
        if (!seqVoice(v.tag, v.track) || isDrum(v.gen)) return false;
        const LaneState& L = lanes[size_t(v.track)];
        return L.volSpeed || L.envSpeed || L.noiseSpeed || L.vg;
    }
    // One frame of the slides of the track: the offsets of the note move by the speeds (x - y per tick). Bounded:
    // 4-bit volume ±15 steps, envelope ±8 octaves, noise period ±31. A Gxx glide sets the level that the step has
    // reached at cycle c, on a straight line to that of the next step (D-393).
    void slideFrame(Voice& v, uint64_t c) noexcept {
        if (!seqVoice(v.tag, v.track) || isDrum(v.gen)) return;
        constexpr double kMaxSemis = 96;
        const LaneState& L = lanes[size_t(v.track)];
        if (L.vg) {
            const double t = std::min(1.0, double(c - std::min(c, L.vgStart)) / double(L.vgLen));
            setLevel(v, v.track, L.ins, L.vgFrom + (L.vgTo - L.vgFrom) * t);
        }
        if (L.volSpeed) v.slVol = std::clamp(v.slVol + L.volSpeed * kVolSlideUnit, -15.0, 15.0);
        if (L.noiseSpeed) {
            const double d = L.noiseCurve.curve ? curveStep(L.noiseCurve, v, c) : L.noiseSpeed * kNoiseSlideUnit;
            v.slNoise = std::clamp(v.slNoise + d, -31.0, 31.0);
        }
        if (L.envSpeed) {
            const double d = L.envCurve.curve ? curveStep(L.envCurve, v, c) : L.envSpeed * kPitchSlideUnit;
            v.slEnv = std::clamp(v.slEnv + d, -kMaxSemis, kMaxSemis);
            v.slEnvK = std::pow(2.0, -v.slEnv / 12);  // higher frequency, shorter period
        }
    }

    // What a curved slide moves in the frame of the voice at cycle c: the share of its distance that the shape adds over
    // that frame (D-394). As the straight slide adds its speed from the frame at the start of the row, the frame before
    // the next command lands on the end of the curve.
    double curveStep(const SlideCurveRun& r, const Voice& v, uint64_t c) const noexcept {
        const auto at = [&](uint64_t x) {
            const double t = std::clamp(double(int64_t(x) - int64_t(r.start)) / double(r.len), 0.0, 1.0);
            return slideShape(t, SlideCurve(r.curve), r.seed, global.slideTable);
        };
        return r.total * (at(c + periodCycles(1, std::max<uint32_t>(1, v.vblHz))) - at(c));
    }

    // ------------------------------------------------------------------------------------------ LFO (D-257)
    // Active on a voice: on, with a depth; the drums have none.
    // An L00 of its lane holds it while the sequencer plays (item 142).
    bool lfoActive(const Voice& v) const noexcept {
        return v.lfo.on && v.lfo.depth > 0 && !isDrum(v.gen) && !(seq.on && lanes[size_t(v.track)].lfoOff);
    }
    // Modulation table of a voice: on, with steps, unless an M00 of its lane holds it (item 143).
    bool ornActive(const Voice& v) const noexcept {
        return v.ornOn && v.ornLen > 0 && !(seq.on && lanes[size_t(v.track)].modOff);
    }
    // Uniform draw in [−1, 1) from the top 53 bits of the generator.
    double lfoDraw() noexcept { return double(rng.next() >> 11) * 0x1.0p-53 * 2 - 1; }
    // Cadence in Hz: its rate, or one cycle per division of the tempo.
    double lfoHz(const TrackLfo& l) const noexcept {
        return l.sync ? seqBpm() * kLfoPpq / 60 / kLfoDivisionTicks[std::min<size_t>(l.division, kLfoDivisions - 1)]
                      : std::clamp(l.hz, kLfoMinHz, kLfoMaxHz);
    }
    // At the attack: the settings of the sound, phase 0, the first value of the Random shape (drawn only for it, so
    // that the other random draws stay the same). Retrig Off (D-279): the phase and the value of the free LFO of the
    // track.
    void startLfo(Voice& v, const TrackPatch& p, uint64_t c) noexcept {
        v.lfo = p.lfo;
        v.lfoNoise = 0;
        v.lfoEnv = v.lfoRatio = 1;
        v.lfoPan = 0;
        if (v.lfo.on && !v.lfo.retrig && hasBus(v.track) && !isDrum(v.gen)) {
            const LfoRun& r = lfoRunAt(v.track, v.lfo, c);
            v.lfoPhase = v.lfoPrev = r.phase;
            v.lfoRand = v.lfoRandPrev = r.rand;
            return;
        }
        v.lfoPhase = v.lfoPrev = 0;
        v.lfoRand = v.lfoRandPrev = v.lfo.on && v.lfo.shape == LfoShape::Random ? lfoDraw() : 0;
    }
    // Free LFO of a track (Retrig Off, D-279): brought up to cycle c at its cadence since its last note; synced to
    // the tempo while the host plays in Host sync, on the host's beat grid. A new Random value at each of its cycles.
    // It starts again with the time base and with the sequencer (seqStart), so that a playback sounds the same.
    struct LfoRun {
        double phase = 0, rand = 0;
        uint64_t at = 0;    // cycle of the last update
        int64_t turn = 0;   // number of the LFO cycle
        bool drawn = false; // rand drawn for the Random shape
    };
    std::array<LfoRun, kTracks> lfoRuns{};
    const LfoRun& lfoRunAt(int track, const TrackLfo& l, uint64_t c) noexcept {
        LfoRun& r = lfoRuns[size_t(track)];
        int64_t turn = r.turn;
        if (l.sync && global.seqClock == SeqClock::Host && host.playing && rOut) {
            const double q =
                hostPpqAt(c) * kLfoPpq / kLfoDivisionTicks[std::min<size_t>(l.division, kLfoDivisions - 1)];
            turn = int64_t(std::floor(q));
            r.phase = q - std::floor(q);
        } else if (c > r.at) {
            const double x = r.phase + double(c - r.at) * double(clock().den) / double(clock().num) * lfoHz(l);
            turn += int64_t(std::floor(x));
            r.phase = x - std::floor(x);
        }
        r.at = c;
        if (l.shape == LfoShape::Random && (!r.drawn || turn != r.turn)) {
            r.rand = lfoDraw();
            r.drawn = true;
        }
        r.turn = turn;
        return r;
    }
    void restartLfoRuns(uint64_t c) noexcept {
        for (LfoRun& r : lfoRuns) r = LfoRun{0, 0, c, 0, false};
    }
    // Position of the host in quarter notes at cycle c of the block being rendered.
    double hostPpqAt(uint64_t c) const noexcept {
        const double secs = double(c) * double(clock().den) / double(clock().num) -
                            double(rStart - base) / double(hostRate());
        return host.ppq + secs * std::max(1.0, host.bpm) / 60;
    }
    // Frame of the LFO: its value (-1 to 1) at the phase of the frame, times the depth, put on its target; then the
    // phase of the next frame, at its rate or at the tempo. Returns the semitones that it adds to the vibrato (Pitch).
    double lfoFrame(Voice& v) noexcept {
        v.lfoNoise = 0;
        v.lfoEnv = v.lfoRatio = 1;
        v.lfoPan = 0;
        if (!lfoActive(v)) return 0;
        const double p = v.lfoPhase;
        double x = v.lfoRand;
        switch (v.lfo.shape) {
        case LfoShape::Triangle: x = p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4; break;
        case LfoShape::Sine: x = std::sin(p * 6.283185307179586); break;
        case LfoShape::Square: x = p < 0.5 ? 1 : -1; break;
        case LfoShape::SawUp: x = 2 * p - 1; break;
        case LfoShape::SawDown: x = 1 - 2 * p; break;
        case LfoShape::Random: break;
        }
        v.lfoPhase += lfoHz(v.lfo) / v.vblHz;
        if (v.lfoPhase >= 1) {
            v.lfoPhase -= std::floor(v.lfoPhase);
            if (v.lfo.shape == LfoShape::Random) v.lfoRand = lfoDraw();
        }
        // Unit of the offset per target: volume and noise steps, cents (envelope period, SID ratio, pitch), % of pan.
        const size_t t = std::min<size_t>(size_t(v.lfo.target), kLfoTargets - 1);
        const double d = x * std::min(v.lfo.depth, double(kLfoDepthMax[t])) * exprLfoAmount(v);  // D-432
        switch (LfoTarget(t)) {
        case LfoTarget::Volume:
            v.vol4 = uint8_t(std::clamp(jsRound(v.vol4 + d), 0.0, 15.0));
            v.pcmGain = std::clamp(v.pcmGain + d / 15, 0.0, 1.0);
            break;
        case LfoTarget::NoisePeriod: v.lfoNoise = int(jsRound(d)); break;
        case LfoTarget::EnvPeriod: v.lfoEnv = std::pow(2.0, -d / 1200); break;
        case LfoTarget::SidRatio: v.lfoRatio = std::pow(2.0, d / 1200); break;
        case LfoTarget::Pitch: return d / 100;
        case LfoTarget::Pan: v.lfoPan = d / 100; break;
        }
        return 0;
    }

    // --------------------------------------------------------------------- Instrument sequences (D-148)
    // Rows and pitch settings of a sound (at the attack, or chained instrument).
    static void loadSequences(Voice& v, const TrackPatch& p) noexcept {
        v.rows = p.rows;
        v.rowsOn = !p.rows.empty();
        v.fixedNote = uint8_t(std::min<int>(p.fixedNote, 127));
        v.link = uint8_t(std::min<int>(p.link, kInstruments));
        v.matrix = uint16_t(p.pitchMatrix & kPitchMatrixAll);
        v.hardSync = p.hardSync;
        v.detune = std::clamp(p.detuneCents, -double(kDetuneMaxCents), double(kDetuneMaxCents)) / 100;
        v.sidWave = p.sidWave;
        // By the form of the SID wave (D-411): the bars of Sine, Bounce and Custom, the Square's steps, Base neither.
        const uint8_t n = uint8_t(std::clamp<int>(p.sidWaveLen, 2, kSidWaveSteps));
        const bool bars = sineForm(p.sidForm) || p.sidForm == SidForm::Custom;
        v.sidWaveLen = bars ? n : 0;
        v.sqSteps = p.sidForm == SidForm::Square ? n : 0;
        v.sqHigh = uint8_t(std::clamp<int>(p.sidWidth, 1, n - 1));
        v.buzz = p.buzzShapes;
        v.buzzLen = uint8_t(std::min<int>(p.buzzLen, kBuzzSteps));
        v.trackShape = uint8_t(p.envShape & 15);
        v.envTune = p.envTuned ? double(std::clamp<int>(p.envRatioN, 1, kEnvRatioMax)) /
                                     std::clamp<int>(p.envRatioD, 1, kEnvRatioMax) *
                                     std::pow(2.0, std::clamp(p.envFineCents, -double(kEnvFineMax), double(kEnvFineMax)) / 1200)
                               : 0;
        v.ymPad = p.ymPad;
    }
    // Tuned envelope (D-152): the period follows the note of frequency hz (attack, new note, slide, bend). A Sync
    // Buzzer setting (D-231): another generator, a GEN row or a chained instrument included, keeps the period of the
    // track, which baseEnvPeriod holds.
    void tuneEnv(Voice& v, double hz) const noexcept {
        if (v.envTune <= 0 || v.gen != Generator::SyncBuzzer) return;
        const uint16_t ep = tunedEnvPeriod(hz * v.envTune * buzzRate(v), v.trackShape, clock());
        if (ep) v.envPeriod = ep;
    }
    // Envelope period played: the one driven by the pitch (matrix), otherwise that of the voice; moved by the LFO, by
    // the envelope slide of the CMD row (D-338) and by the MPE slide (D-432).
    static uint16_t envOut(const Voice& v) noexcept {
        const uint16_t ep = v.envEff ? v.envEff : v.envPeriod;
        const double k = v.lfoEnv * v.slEnvK * v.exprEnvK;
        return k == 1 ? ep : uint16_t(std::clamp(jsRound(ep * k), 1.0, 65535.0));
    }
    // Noise period played: that of the voice, moved by the LFO and by the noise slide of the CMD row (up: shorter).
    static uint8_t noiseOut(const Voice& v) noexcept {
        const int d = v.lfoNoise - int(jsRound(v.slNoise));
        return d ? uint8_t(std::clamp(v.noisePeriod + d, 1, 31)) : v.noisePeriod;
    }
    // SID timer: interrupts in one period of the note, so that ratio 1 plays the note (Indus, 25/10/2026 17:39,
    // D-426; the prototype played an octave lower): two for the square wave (high, then low), n for a programmable wave
    // of n steps (D-151) and for the Square of the menu (D-408, D-411). The LFO moves the ratio (D-257).
    static double sidSteps(const Voice& v) noexcept {
        return v.sidWaveLen ? double(v.sidWaveLen) : v.sqSteps ? double(v.sqSteps) : 2.0;
    }
    static double sidRate(const Voice& v) noexcept { return v.sidRatio * v.lfoRatio * sidSteps(v); }
    // Stepped Sync Buzzer: the n steps fit in one period of the note, so the timer goes n times faster (D-388).
    static double buzzRate(const Voice& v) noexcept { return v.buzzLen ? double(v.buzzLen) : 1.0; }
    // Stepped Sync Buzzer (D-151): the restart of the attack writes the first step, each interrupt the next one.
    static void nextBuzz(Voice& v) noexcept {
        const uint8_t s = v.buzz[v.buzzPos];
        v.shape = s ? uint8_t(s & 15) : v.trackShape;
        if (++v.buzzPos >= v.buzzLen) v.buzzPos = 0;
    }
    // Attack: a stepped Sync Buzzer starts again from its first step.
    static void primeBuzz(Voice& v) noexcept {
        v.buzzPos = 0;
        if (v.gen == Generator::SyncBuzzer && v.buzzLen) nextBuzz(v);
    }
    bool follows(const Voice& v, PitchSource s, PitchTarget t) const noexcept { return (v.matrix & pitchBit(s, t)) != 0; }
    // Base pitch of a target: the slide in progress, or the target note if the target does not follow the slide.
    double slideHz(const Voice& v, PitchTarget t) const noexcept { return follows(v, PitchSource::Slide, t) ? v.fCur : v.fTarget; }
    // The envelope period follows the pitch: the table, the slide or the vibrato drives the Buzzer target.
    static bool buzzerFollows(const Voice& v) noexcept {
        return (v.matrix & (pitchBit(PitchSource::Table, PitchTarget::Buzzer) | pitchBit(PitchSource::Slide, PitchTarget::Buzzer) |
                            pitchBit(PitchSource::Vibrato, PitchTarget::Buzzer))) != 0;
    }
    double fixedHz(const Voice& v) const noexcept {
        return noteHz(double(v.fixedNote) + v.detune + v.analog, a4());
    }
    // Envelope period driven by the pitch: that of the track at the pitch of the note, scaled.
    uint16_t buzzerPeriod(const Voice& v, double semis, double vib, bool fix) const noexcept {
        const double s = (follows(v, PitchSource::Table, PitchTarget::Buzzer) ? semis : 0) +
                         (follows(v, PitchSource::Vibrato, PitchTarget::Buzzer) ? vib : 0);
        const double from = fix && follows(v, PitchSource::Table, PitchTarget::Buzzer) ? fixedHz(v) : slideHz(v, PitchTarget::Buzzer);
        const double f = from * std::pow(2.0, s / 12);
        if (f <= 0 || v.fTarget <= 0) return 0;
        return uint16_t(std::clamp(jsRound(v.envPeriod * v.fTarget / f), 1.0, 65535.0));
    }
    // Pitch of a frame according to the matrix: square (period), timer (SID, Sync Buzzer), envelope.
    void matrixPitch(Voice& v, double semis, double vib, int delta, bool fix, uint64_t c) noexcept {
        const bool tabSq = follows(v, PitchSource::Table, PitchTarget::Square);
        const double sSq = (tabSq ? semis : 0) + (follows(v, PitchSource::Vibrato, PitchTarget::Square) ? vib : 0);
        const double tp0 = fix && tabSq ? double(tonePeriodFor(fixedHz(v), clock())) : double(v.tp);
        double tp = jsRound(tp0 * std::pow(2.0, -sSq / 12));
        if (tabSq) tp += delta;
        v.tpEff = uint16_t(std::clamp(tp, 1.0, 4095.0));
        if (v.gen == Generator::SyncBuzzer || v.gen == Generator::Sid || v.gen == Generator::Dma) {
            const bool tabTm = follows(v, PitchSource::Table, PitchTarget::Timer);
            const double sTm = (tabTm ? semis : 0) + (follows(v, PitchSource::Vibrato, PitchTarget::Timer) ? vib : 0);
            double fm = (fix && tabTm ? fixedHz(v) : slideHz(v, PitchTarget::Timer)) * std::pow(2.0, sTm / 12);
            if (tabTm && delta != 0) {  // period offset turned into a frequency ratio
                const double t0 = tonePeriodFor(fm, clock());
                fm *= t0 / std::clamp(t0 + delta, 1.0, 4095.0);
            }
            if (v.gen == Generator::Dma) {
                v.pcmStep = fm / v.pcmRootHz;
            } else {
                v.timerHz = fm * (v.gen == Generator::Sid ? sidRate(v) : buzzRate(v));
                retuneTimer(v, c);
            }
        }
        v.envEff = buzzerFollows(v) ? buzzerPeriod(v, semis, vib, fix) : 0;
    }
    // Steps of the GEN, MIX and NP rows: the sound of the note comes back when the row is at 0.
    void rowsAt(Voice& v, int op, uint64_t c) noexcept {
        const uint8_t g = v.rows.gen[size_t(op)];
        if ((g || v.genOv) && v.gen != Generator::Dma) {  // a DMA voice keeps its sample
            static constexpr Generator kGen[kSeqGens] = {Generator::Chip, Generator::Chip, Generator::Sid, Generator::SyncBuzzer};
            const Generator want = g ? kGen[std::min<int>(g, kSeqGens - 1)] : v.baseGen;
            if (want != v.gen) switchGen(v, want, c);
            v.genOv = g != 0;
        }
        const uint8_t m = v.rows.mix[size_t(op)];
        if (m) {
            const int mode = std::min<int>(m, kChannelModes) - 1;
            v.toneOn = (mode & 1) != 0;
            v.noiseOn = (mode & 2) != 0;
            v.envVolume = (mode & 4) != 0;
            v.mixOv = true;
        } else if (v.mixOv) {
            restoreMix(v);
            v.mixOv = false;
        }
        const uint8_t n = v.rows.noise[size_t(op)];
        if (n) {
            v.noisePeriod = uint8_t(std::min<int>(n, 31));
            v.npOv = true;
        } else if (v.npOv) {
            v.noisePeriod = v.baseNoisePeriod;
            v.npOv = false;
        }
    }
    // Mixer of the sound of the note (the Sync Buzzer keeps the envelope alone).
    static void restoreMix(Voice& v) noexcept {
        if (v.gen == Generator::SyncBuzzer) {
            v.envVolume = true;
            v.toneOn = v.noiseOn = false;
        } else {
            v.toneOn = v.baseTone;
            v.noiseOn = v.baseNoise;
            v.envVolume = v.baseEnv;
        }
    }
    // Melodic generator changed during a note (GEN row, chained instrument), like liveFx.
    void switchGen(Voice& v, Generator g, uint64_t c) noexcept {
        v.gen = g;
        v.envPeriod = v.baseEnvPeriod;
        restoreMix(v);
        if (g == Generator::SyncBuzzer && v.envPeriod > 3200 && v.envTune <= 0) v.envPeriod = 32;
        tuneEnv(v, v.fTarget);
        v.sidHigh = true;
        v.wavePos = 0;
        v.shape = v.trackShape;
        primeBuzz(v);
        const double f = slideHz(v, PitchTarget::Timer);
        v.timerHz = g == Generator::Sid ? f * sidRate(v) : (g == Generator::SyncBuzzer ? f * buzzRate(v) : 0);
        startTimer(v, c);
    }
    // Hard sync: square (16-channel) and timer restarted.
    void syncRestart(Voice& v, uint64_t c) noexcept {
        v.toneRestart = true;
        v.sidHigh = true;
        v.wavePos = 0;
        startTimer(v, c);
    }
    // End of a table in Hold: the sound of the chained instrument takes over (table, rows, mixer, melodic
    // generator, noise, envelope, SID, vibrato, LFO, pitch settings); volume, level, ADSR and panning stay those of
    // the note.
    void relink(Voice& v, uint64_t c) noexcept {
        const Instrument* x = instrument(v.link);
        if (!x || isDrum(v.gen)) return;
        v.linked = v.link;
        const TrackPatch& p = x->patch;
        const bool keepGen = isDrum(p.generator) || p.generator == Generator::Dma || v.gen == Generator::Dma;
        v.ornOn = p.ornamentOn;
        v.ornLen = std::min<uint8_t>(p.ornamentLength, kOrnamentSteps);
        v.ornLoop = p.ornamentLoop;
        v.ornRate = p.ornamentRate ? p.ornamentRate : 1;
        v.ornPitch = p.ornamentPitch;
        v.ornNoise = p.ornamentNoise;
        v.ornVol = p.ornamentVolume;
        v.ornPos = v.ornFrm = 0;
        v.ornNoiseOn = false;
        loadSequences(v, p);
        v.vibCents = p.vibratoOn ? p.vibratoCents : 0;
        v.vibHz = p.vibratoHz > 0 ? p.vibratoHz : 6;
        v.lfo = p.lfo;  // its phase goes on
        v.sidRatio = p.sidRatio > 0 ? p.sidRatio : 1;
        v.sidMin = uint8_t(std::min<int>(p.sidMinVolume, 15));
        v.shape = uint8_t(p.envShape & 15);
        v.baseTone = p.toneOn;
        v.baseNoise = p.noiseOn;
        v.baseEnv = p.envelopeVolume;
        v.baseNoisePeriod = v.noisePeriod = uint8_t(std::clamp<int>(p.noisePeriod, 0, 31));
        v.baseEnvPeriod = p.envPeriod;
        v.genOv = v.mixOv = v.npOv = false;
        // A drum or DMA generator leaves the voice its own, a DMA voice keeps its own.
        v.baseGen = keepGen ? v.gen : p.generator;
        if (v.baseGen != v.gen) switchGen(v, v.baseGen, c);
        else {
            v.envPeriod = v.baseEnvPeriod;
            if (v.gen == Generator::SyncBuzzer && v.envPeriod > 3200 && v.envTune <= 0) v.envPeriod = 32;
            restoreMix(v);
            v.wavePos = 0;
            primeBuzz(v);
        }
        v.fTarget = noteHz(double(v.note) + v.fine + bendOf(v) + v.detune + v.analog, a4());
        tuneEnv(v, v.fTarget);
        if (v.glide == 0) {
            v.fCur = v.fTarget;
            v.tp = tonePeriodFor(v.fCur, clock());
        }
        v.tpEff = 0;
    }

    // Interrupt of the effect timer: the SID square toggles (or its wave steps), the Sync Buzzer restarts its envelope
    // (next shape of its steps), a drum moves to its next sample value; then the next interrupt is dated.
    void timerEvent(Voice& v, uint64_t c) noexcept {
        switch (v.gen) {
        case Generator::Sid:
            if (v.sidWaveLen) {
                if (++v.wavePos >= v.sidWaveLen) v.wavePos = 0;
            } else if (!v.sqSteps) v.sidHigh = !v.sidHigh;
            else {  // the Square of the menu (D-408, D-411): high over the first sqHigh of its sqSteps interrupts
                if (++v.wavePos >= v.sqSteps) v.wavePos = 0;
                v.sidHigh = v.wavePos < v.sqHigh;
            }
            break;
        case Generator::SyncBuzzer:
            if (v.buzzLen) nextBuzz(v);
            v.restart = true;
            break;
        case Generator::Digidrum:
        case Generator::Chipdrum:
            if (v.looping && v.dpos + 1 >= v.loopEnd) {  // sustain loop (D-154)
                v.dpos = v.loopStart;
                break;
            }
            if (++v.dpos >= v.sampleLen) {
                v.on = false;
                return;
            }
            break;
        case Generator::Dma:
        case Generator::Chip: break;
        }
        if (v.timerMode == TimerMode::Mfp) {
            v.mfp.acknowledge();
            v.nextIrq = std::max(c + 1, mfpMap.toMaster(v.mfp.nextFire()) + jitter.next());
        } else {
            v.freeNext += v.freePeriod;
            v.nextIrq = std::max(c + 1, ceilCycles(v.freeNext));
        }
    }

    // End of an ADSR stage: attack → decay → sustain; the end of the release silences the voice.
    void stageEvent(Voice& v, uint64_t c) noexcept {
        if (v.stage == 1) enterStage(v, 2, 1, c);
        else if (v.stage == 2) enterStage(v, 3, v.sus, c);
        else if (v.stage == 4) {
            v.aRef = 0;
            v.on = false;
        } else v.stageEnd = kNever;
    }

    // One step of the fade: the amplitude falls by 1/64 per step whatever the length, and the voice stops at step
    // fadeLen.
    void fadeEvent(Voice& v, uint64_t c) noexcept {
        (void)c;
        if (++v.fadeStep >= v.fadeLen) {
            v.on = false;
            return;
        }
        v.fadeAmt = 1.0 - double(v.fadeStep) / 64.0;
        v.nextFade = v.fadeStart + periodCycles(uint64_t(v.fadeStep) + 1, kFadeRate);
    }

    // All the internal events of the voice dated at cycle c, in the order of the prototype (slide, VBL frame,
    // timer, ADSR, fade).
    void processDue(Voice& v, uint64_t c) noexcept {
        if (v.nextGlide == c) glideStep(v, c);
        if (v.nextVbl == c) vblEvent(v, c);
        if (v.on && v.nextIrq == c) timerEvent(v, c);
        if (v.on && v.stageEnd == c) stageEvent(v, c);
        if (v.on && v.nextFade == c) fadeEvent(v, c);
    }

    // ------------------------------------------------------------------------------- State of the channel
    // Registers of the voice's channel at this moment, in both modes. A sampled drum plays its current 4-bit value as
    // the volume, tone and noise off; a SID voice the volume of its square or wave step; otherwise the volume of the
    // frame, or the envelope (0x10).
    Wanted wanted(const Voice& v) const noexcept {
        Wanted w;
        if (!v.on) return w;
        const bool m3 = !studio();
        w.tone = v.tpEff ? v.tpEff : v.tp;
        w.noise = noiseOut(v);
        w.envPeriod = envOut(v);
        const bool sampled = isDrum(v.gen) && !v.frames;
        if (sampled) {
            int sv = v.dpos < v.sampleLen ? v.sample[v.dpos] : 0;
            if (v.digiLevel < 1) sv = v.digiMap[size_t(std::clamp(sv, 0, 15))];
            w.volume = uint8_t(std::clamp(sv, 0, 15));
        } else {
            w.toneOn = v.toneOn;
            w.noiseOn = v.noiseOn || v.ornNoiseOn;
            if (v.gen == Generator::Sid)  // wave: its step scaled by the volume of the note, rounded
                w.volume = v.sidWaveLen ? uint8_t((std::min<int>(v.sidWave[v.wavePos], 15) * v.vol4 + 7) / 15)
                                        : v.sidHigh ? v.vol4 : v.sidMin;
            else w.volume = v.envVolume ? 0x10 : v.vol4;
            // YM Pad (D-152): under the envelope, the tone at period 0 chops the channel at the pace of the inner loop;
            // a Sync Buzzer setting (D-231).
            if (v.ymPad && v.gen == Generator::SyncBuzzer && (w.volume & 0x10)) {
                w.toneOn = true;
                w.tone = 0;
            }
        }
        // 3-channel: the fade goes through the 4-bit volume (the envelope volume stays until the end).
        if (m3 && v.fading() && !(w.volume & 0x10)) w.volume = uint8_t(jsRound(w.volume * v.fadeAmt));
        return w;
    }
    // The voice plays the envelope (candidate to drive the shared R11-R13 in 3-channel mode).
    bool usesEnvelope(const Voice& v) const noexcept {
        if (!v.on || (isDrum(v.gen) && !v.frames)) return false;
        return v.gen == Generator::SyncBuzzer || (v.envVolume && v.gen != Generator::Sid);
    }

    // --------------------------------------------------------------------------------- 16-channel buses
    double drumGain(const Voice& v) const noexcept {
        if (v.frames) return 1;
        // A Digidrum is a chip channel playing its 4-bit values, as in 3 channels (no gain). The Chipdrum sample is
        // brought to the level of the register drums of the 3-channel mode (kick, snare, clap, toms). D-286.
        return v.gen == Generator::Chipdrum ? 1.8 : 1.0;
    }
    // Gains of the voice on the buses: gainBase, then × the constant-power panning of its track (cos and sin of
    // (pan + 1) × π/4, D-049); a voice without a track is centred (√½).
    void refreshGain(Voice& v) noexcept {
        const double lv = v.track >= 0 ? live[size_t(v.track)] : 1.0;
        const double gain = v.accent * drumGain(v) * v.fadeAmt;
        v.gainBase = gain * lv;
        // The right side of a stereo DMA sample: its own fader, mute and solo (D-459).
        const bool split = v.track >= 0 && v.gen == Generator::Dma && v.pcmStereo;
        v.gainR = split ? gain * liveR[size_t(v.track)] : v.gainBase;
        if (v.lfoPan != 0 && v.track >= 0) {  // panning moved by the LFO (D-257), same law as refreshLive
            const double ang = (std::clamp(panNow[size_t(v.track)] + v.lfoPan, -1.0, 1.0) + 1) * 0.7853981633974483;
            v.kL = v.gainBase * std::cos(ang);
            v.kR = v.gainR * std::sin(ang);
            return;
        }
        v.kL = v.gainBase * (v.track >= 0 ? panL[size_t(v.track)] : 0.7071067811865476);
        v.kR = v.gainR * (v.track >= 0 ? panR[size_t(v.track)] : 0.7071067811865476);
    }
    // Left and right levels of the output of the voice: the table of one channel alone, or the frame of a DMA voice.
    double levelL(const Voice& v) const noexcept { return v.gen == Generator::Dma ? v.pcmL * v.pcmGain : tab1[v.idx]; }
    double levelR(const Voice& v) const noexcept { return v.gen == Generator::Dma ? v.pcmR * v.pcmGain : tab1[v.idx]; }
    // Smooth ADSR ramp in progress (attack, decay, release).
    static bool adsrRamp(const Voice& v) noexcept {
        return v.on && v.smoothAdsr() && (v.stage == 1 || v.stage == 2 || v.stage == 4);
    }
    // Send of the track of the voice to the delay (D-299).
    double sendOf(const Voice& v) const noexcept { return hasBus(v.track) ? sendNow[size_t(v.track)] : 0.0; }
    // Send buses: the voice goes from its contribution × gS to the new one (cl, cr, slopes sl, sr) × its send. Called
    // before cL, cR, sL, sR and busT are updated.
    void sendUpdate(Voice& v, size_t i, double dt, double cl, double cr, double sl, double sr) noexcept {
        const double g = sendOf(v), o = v.gS;
        dSL[i] += cl * g - (v.cL + v.sL * dt) * o;
        dSR[i] += cr * g - (v.cR + v.sR * dt) * o;
        sSL[i] += sl * g - v.sL * o;
        sSR[i] += sr * g - v.sR * o;
        v.gS = g;
    }
    // The delay goes live: the contribution of the voice from now on, × its send.
    void sendJoin(Voice& v) noexcept {
        v.gS = sendOf(v);
        if (v.gS == 0 || (v.cL == 0 && v.cR == 0 && v.sL == 0 && v.sR == 0)) return;
        const uint64_t t = std::max(genPos, v.busT);
        const size_t i = size_t(t - genPos);
        const double dt = double(t - v.busT);
        dSL[i] += (v.cL + v.sL * dt) * v.gS;
        dSR[i] += (v.cR + v.sR * dt) * v.gS;
        sSL[i] += v.sL * v.gS;
        sSR[i] += v.sR * v.gS;
        if (v.sL != 0 || v.sR != 0) slopeWritten = true;
    }
    // New contribution of the voice from step t on: the difference with what it gave at t (cL + sL × (t − busT)) is
    // added as a step at t, the change of its slope likewise; integrateBus turns these differences back into the sum
    // of the voices. The meters, the separate output of the track and the send buses follow.
    void busUpdate(Voice& v, uint64_t t) noexcept {
        const size_t i = size_t(t - genPos);
        // DMA voice: the smooth ADSR is taken at each frame, in steps without slope, as the software mixing of an
        // STE player would do (D-153); runPcm therefore updates the voice at each frame of a ramp.
        const bool sloped = v.gen != Generator::Dma && adsrRamp(v);
        if (!sloped && v.sL == 0 && v.sR == 0 && v.sM == 0) {
            // Constant level between two changes (usual case): one step on each bus.
            double cl = 0, cr = 0, lv = 0, ld = 0;
            if (v.on) {
                const double a = v.smoothAdsr() ? adsrAt(v, t * 8) : 1.0;  // aRef outside the ramps
                const double ll = levelL(v) * a, lr = levelR(v) * a;
                if (v.gainR == v.gainBase) {
                    lv = (ll + lr) * 0.5 * v.gainBase;
                    ld = (lr - ll) * 0.5 * v.gainBase;
                } else {  // each side of a stereo sample at its own level (D-459)
                    lv = (ll * v.gainBase + lr * v.gainR) * 0.5;
                    ld = (lr * v.gainR - ll * v.gainBase) * 0.5;
                }
                cl = ll * v.kL;
                cr = lr * v.kR;
            }
            meterUpdate(v, t, lv, ld);
            if (trackMask) monoUpdate(v, i, t, lv, 0);
            if (sendLive) {
                const double g = sendOf(v);
                dSL[i] += cl * g - v.cL * v.gS;
                dSR[i] += cr * g - v.cR * v.gS;
                v.gS = g;
            }
            dL[i] += cl - v.cL;
            dR[i] += cr - v.cR;
            v.cL = cl;
            v.cR = cr;
            v.busT = t;
            return;
        }
        // Smooth ADSR ramp, or a slope to cancel: level and slope per internal step (da: ADSR slope × 8 cycles).
        slopeWritten = true;
        double cl = 0, cr = 0, sl = 0, sr = 0, cm = 0, sm = 0, dm = 0;
        if (v.on) {
            const double ll = levelL(v), lr = levelR(v), lv = (ll + lr) * 0.5;
            double a = 1, da = 0;
            if (v.smoothAdsr()) {
                a = adsrAt(v, t * 8);
                if (sloped) da = v.aSlope * 8;
            }
            cl = ll * a * v.kL;
            cr = lr * a * v.kR;
            sl = ll * da * v.kL;
            sr = lr * da * v.kR;
            if (v.gainR == v.gainBase) {
                cm = lv * a * v.gainBase;
                sm = lv * da * v.gainBase;
                dm = (lr - ll) * 0.5 * a * v.gainBase;
            } else {  // each side of a stereo sample at its own level (D-459)
                const double mid = (ll * v.gainBase + lr * v.gainR) * 0.5;
                cm = mid * a;
                sm = mid * da;
                dm = (lr * v.gainR - ll * v.gainBase) * 0.5 * a;
            }
        }
        meterUpdate(v, t, cm, dm);
        if (trackMask) monoUpdate(v, i, t, cm, sm);
        const double dt = double(t - v.busT);
        if (sendLive) sendUpdate(v, i, dt, cl, cr, sl, sr);
        dL[i] += cl - (v.cL + v.sL * dt);
        dR[i] += cr - (v.cR + v.sR * dt);
        sL[i] += sl - v.sL;
        sR[i] += sr - v.sR;
        v.cL = cl;
        v.cR = cr;
        v.sL = sl;
        v.sR = sr;
        v.busT = t;
    }
    // Separate output of the track (16-channel): the same contribution, without panning, on the bus of the track.
    // Called by busUpdate before busT is updated. A voice that changes track leaves the old bus.
    void monoUpdate(Voice& v, size_t i, uint64_t t, double cm, double sm) noexcept {
        const double dt = double(t - v.busT);
        if (v.busTrack != v.track) {
            if (trackOut(v.busTrack)) {
                const size_t o = size_t(v.busTrack);
                dT[o][i] -= v.cM + v.sM * dt;
                sT[o][i] -= v.sM;
                wroteT[o] = true;
            }
            v.cM = v.sM = 0;
            v.busTrack = v.track;
        }
        if (!trackOut(v.track)) return;
        const size_t o = size_t(v.track);
        dT[o][i] += cm - (v.cM + v.sM * dt);
        sT[o][i] += sm - v.sM;
        if (sm != 0) slopeWritten = true;
        v.cM = cm;
        v.sM = sm;
        wroteT[o] = true;
    }
    // Sampled drum: tone and noise off, fixed volume between two interrupts. The output of a YM channel set up
    // this way is the index (volume × 2) | 1, without going through the virtual channel.
    static bool sampledVoice(const Voice& v) noexcept { return isDrum(v.gen) && !v.frames; }

    // Pushes the state of the voice into its virtual channel and onto the bus, before step t.
    void applyStudio(int index, uint64_t t) noexcept {
        Voice& v = voices[size_t(index)];
        VirtualChannel& ch = vch[size_t(index)];
        if (v.on && v.gen == Generator::Dma) {
            ch.invalidate();  // DMA voice: the current frame (runPcm), without a virtual channel
        } else if (v.on && sampledVoice(v)) {
            v.idx = uint8_t((wanted(v).volume << 1) | 1);
            ch.invalidate();  // the channel will take its index again at the next voice that uses it
        } else if (v.on) {
            const Wanted w = wanted(v);
            ch.setTonePeriod(w.tone);
            ch.setNoisePeriod(w.noise);
            ch.setMixer(w.toneOn, w.noiseOn);
            ch.setVolume(w.volume);
            ch.setEnvPeriod(w.envPeriod);
            if (v.restart || v.envRetrig) ch.writeShape(v.shape);
            if (v.toneRestart) ch.restartTone();
        } else {
            ch.invalidate();
            v.idx = 0;
        }
        v.restart = v.toneRestart = v.envRetrig = false;
        refreshGain(v);
        busUpdate(v, t);
    }
    // Renders a voice over internal steps [from, to): between two of its internal events its output runs (virtual
    // channel, DMA frames, or nothing for a sampled drum, constant in between); an event at cycle ev applies at step
    // ⌈ev / 8⌉ (processDue, then applyStudio).
    void renderStudioVoice(int index, uint64_t from, uint64_t to) noexcept {
        Voice& v = voices[size_t(index)];
        VirtualChannel& ch = vch[size_t(index)];
        uint64_t t = from;
        auto run = [&](uint64_t a, uint64_t b) {
            if (v.gen == Generator::Dma) {
                runPcm(v, a, b);
                return;
            }
            if (sampledVoice(v)) return;  // constant output between two events
            if (chipFast(v)) {
                runChipFast(v, ch, a, b);
                return;
            }
            ch.run(b - a, [&](uint64_t off, uint8_t idx) {
                v.idx = idx;
                busUpdate(v, a + off);
            });
        };
        while (v.on) {
            const uint64_t ev = v.nextEvent();
            const uint64_t evTick = ev == kNever ? kNever : (ev + 7) >> 3;
            if (evTick >= to) {
                run(t, to);
                return;
            }
            if (evTick > t) {
                run(t, evTick);
                t = evTick;
                if (!v.on) return;  // end of a DMA sample
            }
            processDue(v, ev);
            applyStudio(index, t);
        }
    }
    // Usual case of a chip voice (oscilloscope on its track, no separate output): busUpdate in local variables,
    // constant gains over [a, b) (they only change at the events of the voice). Outside a smooth ADSR ramp
    // (no slope left), one step per change; during a ramp, the step and the slope of busUpdate. The two levels
    // of a channel are equal, (l + l) / 2 is l: the result is that of busUpdate to the bit.
    bool chipFast(const Voice& v) const noexcept {
        if (trackMask || !hasBus(v.track) || v.meterTrack != v.track || !sendSteady(v)) return false;
        return adsrRamp(v) || (v.sL == 0 && v.sR == 0 && v.sM == 0);
    }
    // The send of the voice to the delay does not change (D-299): the fast paths add its steps × gS.
    bool sendSteady(const Voice& v) const noexcept { return !sendLive || v.gS == sendOf(v); }
    // The fast path announced above chipFast.
    void runChipFast(Voice& v, VirtualChannel& ch, uint64_t a, uint64_t b) noexcept {
        const size_t o = size_t(v.track);
        double* const dl = dL.data();
        double* const dr = dR.data();
        double* const md = meterD[o].data();
        const double* const tab = tab1.data();
        const uint64_t at0 = a - genPos;
        const double kL = v.kL, kR = v.kR, gb = v.gainBase;
        // Send buses (D-299), with the steps of the voice × g.
        const double g = sendLive ? v.gS : 0.0;
        double* const dsl = dSL.data();
        double* const dsr = dSR.data();
        double cL = v.cL, cR = v.cR, cMeter = v.cMeter;
        float peak = 0;
        uint64_t busT = v.busT;
        uint8_t last = v.idx;
        const uint64_t* const bs = bucketStart.data();
        size_t bucket = 0;
        auto meter = [&](uint64_t t, double cm) {
            peak = std::max(peak, float(cm));
            while (t >= bs[bucket + 1]) ++bucket;  // meterBucket(t)
            if (cm != cMeter) {
                md[bucket] += cm - cMeter;
                cMeter = cm;
            }
        };
        if (!adsrRamp(v)) {
            const double env = v.smoothAdsr() ? v.aRef : 1.0;  // adsrAt outside the ramps
            ch.run(b - a, [&](uint64_t off, uint8_t idx) {
                last = idx;
                const uint64_t t = a + off;
                const double l = tab[idx] * env;
                meter(t, l * gb);
                const double cl = l * kL, cr = l * kR;
                const size_t i = size_t(at0 + off);
                dl[i] += cl - cL;
                dr[i] += cr - cR;
                if (g != 0) {
                    dsl[i] += cl * g - cL * g;
                    dsr[i] += cr * g - cR * g;
                }
                cL = cl;
                cR = cr;
                busT = t;
            });
        } else {
            double* const sl = sL.data();
            double* const sr = sR.data();
            double* const ssl = sSL.data();
            double* const ssr = sSR.data();
            const uint8_t stage = v.stage;
            const double aRef = v.aRef, aSlope = v.aSlope, sus = v.sus, da = v.aSlope * 8;
            const uint64_t aT = v.aT;
            double sLv = v.sL, sRv = v.sR;
            bool wrote = false;
            ch.run(b - a, [&](uint64_t off, uint8_t idx) {
                last = idx;
                wrote = true;
                const uint64_t t = a + off;
                const double l = tab[idx];
                // adsrAt(v, 8t) of a ramp
                double env = aRef + aSlope * double(t * 8 - aT);
                if (stage == 1) env = std::min(env, 1.0);
                else if (stage == 2) env = std::max(env, sus);
                else env = std::max(env, 0.0);
                const double cl = l * env * kL, cr = l * env * kR;
                const double nl = l * da * kL, nr = l * da * kR;
                meter(t, l * env * gb);
                const double dt = double(t - busT);
                const size_t i = size_t(at0 + off);
                dl[i] += cl - (cL + sLv * dt);
                dr[i] += cr - (cR + sRv * dt);
                sl[i] += nl - sLv;
                sr[i] += nr - sRv;
                if (g != 0) {
                    dsl[i] += cl * g - (cL + sLv * dt) * g;
                    dsr[i] += cr * g - (cR + sRv * dt) * g;
                    ssl[i] += nl * g - sLv * g;
                    ssr[i] += nr * g - sRv * g;
                }
                cL = cl;
                cR = cr;
                sLv = nl;
                sRv = nr;
                busT = t;
            });
            if (wrote) slopeWritten = true;
            v.sL = sLv;
            v.sR = sRv;
        }
        v.idx = last;
        v.cL = cL;
        v.cR = cR;
        v.cMeter = cMeter;
        v.busT = busT;
        meterPeak[o] = std::max(meterPeak[o], peak);
        meterPeakR[o] = std::max(meterPeakR[o], peak);
    }
    // DMA voice (D-153): frames of the sample in steps [a, b), one step on the buses at each frame (at the step
    // that follows the start of the frame: a frame that starts in the last step waits for the next render). The
    // smooth ADSR is taken at the frame there, without slope (busUpdate). Without std::ceil (slow without SSE4.1):
    // ceil(x) < b is equivalent to x ≤ b − 1, and ceilTick computes it by truncation.
    void runPcm(Voice& v, uint64_t a, uint64_t b) noexcept {
        const double last = double(b - 1);
        const bool fast = !trackMask && hasBus(v.track) && v.meterTrack == v.track && sendSteady(v);
        while (v.on && v.pcmNext <= last) {
            if (fast && v.sL == 0 && v.sR == 0 && v.sM == 0) {
                runPcmFast(v, a, last);
                return;
            }
            const uint64_t t = std::max(a, ceilTick(v.pcmNext));
            v.pcmNext += v.pcmPeriod;
            pcmFrame(v);
            busUpdate(v, t);
        }
    }
    // Usual case of runPcm (voice without slope, oscilloscope on its track, no separate output): same computations
    // as pcmFrame and busUpdate, the state of the voice in local variables. The ADSR stage does not change in
    // [a, b): its ends are events of the voice (renderStudioVoice).
    void runPcmFast(Voice& v, uint64_t a, double last) noexcept {
        const size_t o = size_t(v.track);
        double* const dl = dL.data();
        double* const dr = dR.data();
        double* const md = meterD[o].data();
        double* const mdd = meterDD[o].data();
        const uint64_t g0 = genPos;
        const uint64_t* const bs = bucketStart.data();
        size_t bucket = 0;
        const int8_t* const pcm = v.pcm;
        const double frames = double(v.pcmFrames), period = v.pcmPeriod, step = v.pcmStep;
        const uint32_t lastFrame = v.pcmFrames == 0 ? 0 : v.pcmFrames - 1;
        const bool loop = v.pcmLoop && v.pcmFrames > 0;
        const double from = double(v.pcmStart), span = frames - from;  // the loop resumes at the start read (Oxx)
        constexpr double kScale = DmaBank::kScale / 128.0;
        const double gain = v.pcmGain, kL = v.kL, kR = v.kR, gb = v.gainBase, gr = v.gainR;
        const double g = sendLive ? v.gS : 0.0;  // send of the delay (D-299)
        double* const dsl = dSL.data();
        double* const dsr = dSR.data();
        // Smooth ADSR (adsrAt): aRef + aSlope × (8t − aT), clamped according to the stage; 1 without smooth ADSR.
        constexpr double kInf = std::numeric_limits<double>::infinity();
        const bool smooth = v.smoothAdsr();
        const bool ramp = smooth && (v.stage == 1 || v.stage == 2 || v.stage == 4);
        const double aRef = smooth ? v.aRef : 1.0, aSlope = ramp ? v.aSlope : 0.0;
        const uint64_t aT = v.aT;
        const double lo = !ramp ? -kInf : v.stage == 2 ? v.sus : v.stage == 4 ? 0.0 : -kInf;
        const double hi = ramp && v.stage == 1 ? 1.0 : kInf;
        double next = v.pcmNext, pos = v.pcmPos, cL = v.cL, cR = v.cR, cMeter = v.cMeter, l = v.pcmL, r = v.pcmR;
        double cMeterD = v.cMeterD;
        float peakL = 0, peakR = 0;
        uint64_t t = v.busT;
        bool ended = false;
        // A mono sample after a stereo one: the half difference of the stereo one leaves the track (D-457).
        if (!v.pcmStereo && cMeterD != 0) {
            mdd[meterBucket(a)] -= cMeterD;
            cMeterD = 0;
            meterSide[o] = true;
        }
        if (v.pcmStereo) meterSide[o] = true;
        // One loop per case (ADSR ramp, stereo sample). In mono, r = l: (l + l) / 2 is l, the level of the
        // oscilloscope is the same to the bit.
        auto play = [&](auto rampTag, auto stereoTag) {
            constexpr bool kRamp = decltype(rampTag)::value, kStereo = decltype(stereoTag)::value;
            while (next <= last) {
                t = std::max(a, ceilTick(next));
                next += period;
                if (pos >= frames) {
                    if (!loop) {
                        ended = true;
                        break;
                    }
                    pos = from + std::fmod(pos - from, span);
                }
                const uint32_t k = std::min(uint32_t(pos), lastFrame);
                if constexpr (kStereo) {
                    l = pcm[2 * k] * kScale;
                    r = pcm[2 * k + 1] * kScale;
                } else {
                    l = r = pcm[k] * kScale;
                }
                pos += step;
                double env = aRef;
                if constexpr (kRamp) env = std::min(std::max(aRef + aSlope * double(int64_t(t * 8 - aT)), lo), hi);
                const double ll = l * gain * env;
                const double lr = kStereo ? r * gain * env : ll;
                const double cm = kStereo ? (ll * gb + lr * gr) * 0.5 : ll * gb;  // gr: the right side's (D-459)
                while (t >= bs[bucket + 1]) ++bucket;  // meterBucket(t)
                md[bucket] += cm - cMeter;
                cMeter = cm;
                if constexpr (kStereo) {  // each side its peak, their half difference around the mean (D-457)
                    peakL = std::max(peakL, float(ll * gb));
                    peakR = std::max(peakR, float(lr * gr));
                    const double dm = (lr * gr - ll * gb) * 0.5;
                    mdd[bucket] += dm - cMeterD;
                    cMeterD = dm;
                } else {
                    peakL = std::max(peakL, float(cm));
                }
                const double cl = ll * kL, cr = lr * kR;
                const size_t i = size_t(t - g0);
                dl[i] += cl - cL;
                dr[i] += cr - cR;
                if (g != 0) {
                    dsl[i] += cl * g - cL * g;
                    dsr[i] += cr * g - cR * g;
                }
                cL = cl;
                cR = cr;
            }
        };
        using Yes = std::true_type;
        using No = std::false_type;
        if (ramp && v.pcmStereo) play(Yes{}, Yes{});
        else if (ramp) play(Yes{}, No{});
        else if (v.pcmStereo) play(No{}, Yes{});
        else play(No{}, No{});
        v.pcmNext = next;
        v.pcmPos = pos;
        v.pcmL = l;
        v.pcmR = r;
        v.cL = cL;
        v.cR = cR;
        v.cMeter = cMeter;
        v.cMeterD = cMeterD;
        v.busT = t;
        meterPeak[o] = std::max(meterPeak[o], peakL);
        meterPeakR[o] = std::max(meterPeakR[o], v.pcmStereo ? peakR : peakL);
        if (ended) {  // end of the sample without loop (pcmFrame)
            v.on = false;
            v.pcmL = v.pcmR = 0;
            busUpdate(v, t);
        }
    }
    // Ceiling of a position that is positive or zero (signed conversions: one instruction each on x86-64).
    static uint64_t ceilTick(double x) noexcept {
        const int64_t i = int64_t(x);
        return uint64_t(i + (double(i) < x ? 1 : 0));
    }
    // Next frame: byte nearest (truncation) to the position, which advances by the read step; at the end, the
    // loop resumes at the start (while the voice sounds), otherwise the voice stops. The start is the first frame
    // read at the attack (Start, D-444, then Oxx, D-338), as the STE reloads its frame start address; the end is End.
    static void pcmFrame(Voice& v) noexcept {
        if (v.pcmPos >= double(v.pcmFrames)) {
            if (!v.pcmLoop || v.pcmFrames <= v.pcmStart) {
                v.on = false;
                v.pcmL = v.pcmR = 0;
                return;
            }
            const double from = double(v.pcmStart);
            v.pcmPos = from + std::fmod(v.pcmPos - from, double(v.pcmFrames) - from);
        }
        const uint32_t k = std::min(uint32_t(v.pcmPos), v.pcmFrames - 1);
        const int a = v.pcmStereo ? v.pcm[2 * k] : v.pcm[k];
        const int b = v.pcmStereo ? v.pcm[2 * k + 1] : a;
        v.pcmL = a * (DmaBank::kScale / 128.0);
        v.pcmR = b * (DmaBank::kScale / 128.0);
        v.pcmPos += v.pcmStep;
    }

    // ----------------------------------------------------------------------------------- 3-channel chips
    // Register write to chip k at a master cycle, mirrored into regs (recorder), into the copies of the separate
    // outputs (the volume of another track's channel held at 0) and into the write log.
    void chipWrite(int k, uint64_t cycle, uint8_t reg, uint8_t value) noexcept {
        chips[size_t(k)].write(cycle, reg, value);
        if (reg < 14) {
            regs[size_t(k)][reg] = value;
            if (reg == 13) r13Written[size_t(k)] = true;
        }
        for (int j = 0; j < 4; ++j) {  // copies for the separate outputs of the tracks of this chip
            const int tr = 4 * k + j;
            if (!trackOut(tr)) continue;
            if (reg >= 8 && reg <= 10) {
                const int ch = reg - 8;
                const uint8_t own = chipVoice(k, ch).track == tr ? value : 0;
                if (own == shadowVol[size_t(tr)][size_t(ch)]) continue;
                shadowVol[size_t(tr)][size_t(ch)] = own;
                shadow[size_t(tr)].write(cycle, reg, own);
            } else {
                shadow[size_t(tr)].write(cycle, reg, value);
            }
        }
        if (log && log->size() < log->capacity()) log->push_back({uint8_t(k), cycle, reg, value});
    }
    Voice& chipVoice(int k, int ch) noexcept { return voices[size_t(k * 3 + ch)]; }

    // Channel that drives R11-R13: a Sync Buzzer first, otherwise the most recent voice at envelope volume.
    int envOwnerOf(int k) const noexcept {
        int best = -1;
        uint64_t bestAge = 0;
        bool bestBuzz = false;
        for (int ch = 0; ch < 3; ++ch) {
            const Voice& v = voices[size_t(k * 3 + ch)];
            if (!usesEnvelope(v) || v.fading()) continue;
            const bool buzz = v.gen == Generator::SyncBuzzer;
            if (best < 0 || (buzz && !bestBuzz) || (buzz == bestBuzz && v.age > bestAge)) {
                best = ch;
                bestAge = v.age;
                bestBuzz = buzz;
            }
        }
        return best;
    }

    // Brings the registers of channel ch of chip k to what its voice wants at cycle c (D-050): period, noise period
    // R6, mixer R7, volume, then the shared envelope R11-R13. Only the values that change are written.
    void applyChip(int k, int ch, uint64_t c) noexcept {
        Voice& v = chipVoice(k, ch);
        ChipCache& cc = cache[size_t(k)];
        const Wanted w = wanted(v);
        if (w.tone != cc.tone[ch] && v.on) {
            chipWrite(k, c, uint8_t(2 * ch), uint8_t(w.tone & 0xff));
            chipWrite(k, c, uint8_t(2 * ch + 1), uint8_t(w.tone >> 8));
            cc.tone[ch] = w.tone;
        }
        // Noise: R6 follows the voice that switched its noise on last; when it switches it off, it goes back to the
        // most recent of the channels whose noise stays on.
        const bool noiseStarted = w.noiseOn && (!cc.noiseOn[ch] || v.restart);
        if (noiseStarted) cc.noiseOwner = ch;
        cc.noiseOn[ch] = w.noiseOn;
        if (!w.noiseOn && cc.noiseOwner == ch) {
            cc.noiseOwner = -1;
            for (int o = 0; o < 3; ++o)
                if (o != ch && cc.noiseOn[o] && (cc.noiseOwner < 0 || chipVoice(k, o).age > chipVoice(k, cc.noiseOwner).age))
                    cc.noiseOwner = o;
        }
        if (cc.noiseOwner >= 0) {
            const uint8_t np = cc.noiseOwner == ch ? w.noise : wanted(chipVoice(k, cc.noiseOwner)).noise;
            if (np != cc.r6) {
                chipWrite(k, c, 6, np);
                cc.r6 = np;
            }
        }
        uint8_t r7 = cc.r7;  // bit ch: tone off, bit 3 + ch: noise off
        r7 = uint8_t((r7 & ~(1u << ch)) | (w.toneOn ? 0u : 1u << ch));
        r7 = uint8_t((r7 & ~(8u << ch)) | (w.noiseOn ? 0u : 8u << ch));
        if (r7 != cc.r7) {
            chipWrite(k, c, 7, r7);
            cc.r7 = r7;
        }
        if (w.volume != cc.volume[ch]) {
            chipWrite(k, c, uint8_t(8 + ch), w.volume);
            cc.volume[ch] = w.volume;
        }
        // Shared envelope.
        const int owner = envOwnerOf(k);
        if (owner >= 0) {
            const Voice& o = chipVoice(k, owner);
            const uint16_t ep = envOut(o);  // matrix (Buzzer target) and tuned envelope included (D-152)
            if (ep != cc.envPeriod) {
                chipWrite(k, c, 11, uint8_t(ep & 0xff));
                chipWrite(k, c, 12, uint8_t(ep >> 8));
                cc.envPeriod = ep;
            }
            // R13 restarts the envelope: written when the owner restarts its note (or Rxx), or when the owner changes
            // to a voice of another shape.
            const bool ownerChanged = owner != cc.envOwner;
            cc.envOwner = owner;
            if ((owner == ch && (v.restart || v.envRetrig)) || (ownerChanged && o.shape != cc.shape)) {
                chipWrite(k, c, 13, o.shape);
                cc.shape = o.shape;
            }
        } else {
            cc.envOwner = -1;
        }
        v.restart = v.toneRestart = v.envRetrig = false;
    }

    // Runs the internal events of the three voices of chip k that fall before `end`, earliest first, and writes the
    // registers each one changes at its own cycle. Recorder frames due before an event are taken first.
    void advanceChip(int k, uint64_t end) noexcept {
        for (;;) {
            int best = -1;
            uint64_t when = end;
            for (int ch = 0; ch < 3; ++ch) {
                const Voice& v = chipVoice(k, ch);
                if (!v.on) continue;
                const uint64_t e = v.nextEvent();
                if (e < when) {
                    when = e;
                    best = ch;
                }
            }
            if (best < 0) return;
            if (k == cap.chip) captureUntil(when);  // frames before these events, on the state before them
            Voice& v = chipVoice(k, best);
            const bool wasOn = v.on;
            processDue(v, when);
            applyChip(k, best, when);
            if (wasOn && !v.on) applyEnvAfterEnd(k, when);
        }
    }
    // A voice has fallen silent: R11-R13 go to the remaining envelope voice.
    void applyEnvAfterEnd(int k, uint64_t c) noexcept {
        ChipCache& cc = cache[size_t(k)];
        const int owner = envOwnerOf(k);
        if (owner < 0 || owner == cc.envOwner) {
            if (owner < 0) cc.envOwner = -1;
            return;
        }
        applyChip(k, owner, c);
    }

    // --------------------------------------------------------------------------- Register recorder (D-157)
    // Frames whose cycle comes before c. A write dated at the cycle of a frame is included in it: frames only go out
    // once time has moved past them (a later voice event, end of a render slice).
    void captureUntil(uint64_t c) noexcept {
        while (cap.chip >= 0 && cap.next < c) captureFrame();
    }
    // Records the next frame: the registers of the recorded chip, a muted channel at volume 0, the timer effect of
    // each channel, whether R13 was written since the previous frame (on the first frame: whether a channel uses the
    // envelope), the pending Sync command and the DMA state. Frame n is due at start + periodCycles(n, hz).
    void captureFrame() noexcept {
        const size_t k = size_t(cap.chip);
        RegisterFrame& f = cap.frames[cap.count];
        f.reg = regs[k];
        const uint32_t mute = cache[k].mute;
        bool env = false;
        for (int ch = 0; ch < 3; ++ch) {
            const bool muted = ((mute >> ch) & 1u) != 0;
            if (muted) f.reg[size_t(8 + ch)] = 0;
            env = env || (f.reg[size_t(8 + ch)] & 0x10) != 0;
            Voice& v = chipVoice(cap.chip, ch);
            f.fx[size_t(ch)] = muted ? RegisterFrame::Effect{} : captureEffect(v);
            f.fx[size_t(ch)].restart = f.fx[size_t(ch)].on && (v.fxStart || cap.count == 0);
            v.fxStart = false;
        }
        f.r13 = r13Written[k] || (cap.count == 0 && env);
        r13Written[k] = false;
        f.sync = cap.sync;
        cap.sync = -1;
        if (cap.dma) cap.dma[cap.count] = captureDma();
        ++cap.count;
        cap.next = cap.start + periodCycles(cap.count, cap.hz);
        if (cap.count >= cap.capacity) cap.chip = -1;  // full: the take stops
    }
    // Only one DMA channel (D-162): the DMA voice started most recently takes it at its attack and keeps it until
    // its end, the older voices still playing do not come back to it. Voices of muted tracks (or outside solo):
    // silenced.
    DmaFrame captureDma() noexcept {
        DmaFrame d;
        if (!global.ste || studio()) return d;
        const bool anySolo = soloed();
        const Voice* newest = nullptr;
        const Voice* owner = nullptr;
        for (int i = kDma3Voice; i < kDma3Voice + kDmaTracks; ++i) {
            const Voice& v = voices[size_t(i)];
            if (!v.on || v.gen != Generator::Dma) continue;
            // A stereo sample is heard while one of its sides is (D-459).
            if (v.track >= 0 && !heard(v.track, false, anySolo) && !(v.pcmStereo && heard(v.track, true, anySolo)))
                continue;
            ++d.voices;
            if (!newest || v.dmaSerial > newest->dmaSerial) newest = &v;
            if (v.dmaSerial == cap.dmaSerial) owner = &v;
        }
        if (newest && newest->dmaSerial > cap.dmaSerial) {
            owner = newest;
            d.restart = true;
            cap.dmaSerial = newest->dmaSerial;
        }
        if (!owner) return d;  // cap.dmaSerial stays: only a voice started later takes the channel back
        d.on = true;
        d.restart = d.restart || cap.count == 0;
        d.slot = owner->slot;
        d.start = owner->pcmStart;
        d.step = float(owner->pcmStep);
        d.gain = float(owner->pcmGain);
        return d;
    }
    RegisterFrame::Effect captureEffect(const Voice& v) const noexcept {
        RegisterFrame::Effect e;
        if (!v.on || !v.timerOn) return e;
        e.on = true;
        e.gen = v.gen;
        e.value = v.gen == Generator::Sid ? v.vol4 : v.gen == Generator::SyncBuzzer ? uint8_t(v.shape & 15) : v.slot;
        // The exports play the SID as its square wave, two interrupts per period: a wave of steps or the Square of the
        // menu gives the timer of the square at the same pitch, not its own, which is sidSteps() / 2 times faster
        // (D-408, D-411, D-426).
        const double steps = v.gen == Generator::Sid ? sidSteps(v) / 2 : 1.0;
        e.timer = v.timerMode == TimerMode::Mfp && steps == 1 ? v.mfpSet : mfpQuantize(timerActualHz(v) / steps);
        return e;
    }

    // Mute and solo in 3-channel mode, at cycle c: the channel mask of each chip (setVoiceMute) and, for each track
    // with its own output, the copy chip that plays it.
    void updateMutes(uint64_t c) noexcept {
        const bool anySolo = soloed();
        for (int k = 0; k < 2; ++k) {
            uint32_t mask = 0;
            for (int ch = 0; ch < 3; ++ch) {
                const Voice& v = chipVoice(k, ch);
                if (v.track < 0) continue;
                const TrackMix& m = tracks[size_t(v.track)].mix;
                if (m.mute || (anySolo && !m.solo)) mask |= 1u << ch;
            }
            if (mask != cache[size_t(k)].mute) {
                chips[size_t(k)].chip().setVoiceMute(mask);
                cache[size_t(k)].mute = mask;
            }
            // Copy of a track: a channel that moves to another track is set to volume 0 in it; the muted track (or
            // outside solo) is cut entirely.
            for (int j = 0; j < 4; ++j) {
                const int tr = 4 * k + j;
                if (!trackOut(tr)) continue;
                for (int ch = 0; ch < 3; ++ch) {
                    const uint8_t want = chipVoice(k, ch).track == tr ? cache[size_t(k)].volume[ch] : 0;
                    if (want != shadowVol[size_t(tr)][size_t(ch)]) {
                        shadowVol[size_t(tr)][size_t(ch)] = want;
                        shadow[size_t(tr)].write(c, uint8_t(8 + ch), want);
                    }
                }
                const TrackMix& m = tracks[size_t(tr)].mix;
                const uint32_t own = (m.mute || (anySolo && !m.solo)) ? 7u : 0u;
                if (own != shadowMute[size_t(tr)]) {
                    shadow[size_t(tr)].chip().setVoiceMute(own);
                    shadowMute[size_t(tr)] = own;
                }
            }
        }
    }

    // --------------------------------------------------------------------------------------- Live mixer
    // A solo is on (that of either side of a stereo DMA sample, D-459): the other tracks fall silent.
    bool soloed() const noexcept {
        bool any = false;
        for (int i = 0; i < kTracks; ++i) {
            const TrackMix& m = tracks[size_t(i)].mix;
            any |= m.solo || (m.soloR && trackStereo(i));
        }
        return any;
    }
    // DMA track whose sample is stereo: the right side has its own fader, mute and solo (D-459).
    bool trackStereo(int track) const noexcept {
        const TrackPatch& p = tracks[size_t(track)].patch;
        return p.generator == Generator::Dma &&
               dmaBank.slot(p.dmaSlot >= 0 ? p.dmaSlot & 7 : dmaAutoSlot(track)).stereo;
    }
    // A side of the track is heard: not muted, and in the solo if there is one. `right`: the right side of a stereo
    // DMA sample, the left side elsewhere.
    bool heard(int track, bool right, bool anySolo) const noexcept {
        const TrackMix& m = tracks[size_t(track)].mix;
        if (right && trackStereo(track)) return !m.muteR && (!anySolo || m.soloR);
        return !m.mute && (!anySolo || m.solo);
    }
    // Level, panning and send of each track: level 0 for a muted track or one outside the solo; the PAN row of the
    // step takes precedence over the mixer; constant-power gains cos and sin of the angle (pan + 1) × π/4.
    void refreshLive() noexcept {
        const bool anySolo = soloed();
        for (int i = 0; i < kTracks; ++i) {
            const TrackMix& m = tracks[size_t(i)].mix;
            live[size_t(i)] = heard(i, false, anySolo) ? std::clamp(m.level, 0.0, 1.0) : 0.0;
            liveR[size_t(i)] = !trackStereo(i)        ? live[size_t(i)]
                               : heard(i, true, anySolo) ? std::clamp(m.link ? m.level : m.levelR, 0.0, 1.0)
                                                         : 0.0;
            const double pan = std::isnan(panStep[size_t(i)]) ? m.pan : panStep[size_t(i)];
            panNow[size_t(i)] = std::clamp(pan, -1.0, 1.0);
            const double ang = (panNow[size_t(i)] + 1) * 0.7853981633974483;
            panL[size_t(i)] = std::cos(ang);
            panR[size_t(i)] = std::sin(ang);
            sendNow[size_t(i)] = std::clamp(m.send, 0.0, 1.0);
        }
    }
    // The mixer changed at cycle c: the voices that sound take their new level and panning (3 channels: the mutes of
    // the chip channels, the DMA voices).
    void remix(uint64_t c) noexcept {
        if (studio()) {
            for (int i = 0; i < kVoices; ++i)
                if (voices[size_t(i)].on) touch(i, c);
        } else {
            updateMutes(c);
            for (int i = kDma3Voice; i < kDma3Voice + kDmaTracks; ++i)
                if (voices[size_t(i)].on) touch(i, c);
        }
    }

    // ----------------------------------------------------------------------------- Allocation, playing
    // Applies the modified voice outside its render (event at cycle c).
    void touch(int index, uint64_t c) noexcept {
        if (studio() || voices[size_t(index)].gen == Generator::Dma) applyStudio(index, busTick(c));
        else {
            const Voice& v = voices[size_t(index)];
            const bool wasEnv = cache[size_t(v.chip)].envOwner == v.channel;
            applyChip(v.chip, v.channel, c);
            if (!v.on && wasEnv) applyEnvAfterEnd(v.chip, c);
        }
    }

    // Instrument of a note played live (Program Change, D-156): 0 for a drum track.
    int liveIns(int track) const noexcept {
        return isDrum(tracks[size_t(track)].patch.generator) ? 0 : program[size_t(track)];
    }
    // Instrument of a step (INS row, D-147): nullptr for the sound of the track (0, empty slot, no table).
    const Instrument* instrument(int ins) const noexcept {
        if (ins <= 0 || ins > kInstruments || !instruments) return nullptr;
        const Instrument& x = (*instruments)[size_t(ins - 1)];
        return x.used ? &x : nullptr;
    }
    const TrackPatch& lanePatch(int track, int ins) const noexcept {
        const Instrument* x = instrument(ins);
        return x ? x->patch : tracks[size_t(track)].patch;
    }
    const TrackArp& laneArp(int track, int ins) const noexcept {
        const Instrument* x = instrument(ins);
        return x ? x->arp : tracks[size_t(track)].arp;
    }
    // Generator of a sequencer note: that of the GEN row (gen ≥ 0), otherwise that of the instrument, otherwise
    // that of the track (-1). In 3-channel mode, a Digidrum instrument leaves the track its generator: only the
    // Digidrum track plays samples, and it is a drum track, without an INS row.
    int laneGen(int track, int ins, int gen) const noexcept {
        if (gen >= 0) return gen;
        const Instrument* x = instrument(ins);
        if (!x) return -1;
        if (!studio() && x->patch.generator == Generator::Digidrum) return int(tracks[size_t(track)].patch.generator);
        return int(x->patch.generator);
    }

    // Voice of a sequencer track (laneVoice): the voice of the track in 16-channel mode; in 3-channel mode, each chip
    // carries A, B, C then the Digidrum track, which takes the designated channel, like any track set to Digidrum.
    int laneVoice(int track) const noexcept {
        if (studio()) return track;
        if (dmaTrack(track)) return kDma3Voice + (track - dmaFirst());  // DMA track (D-153)
        const int k = chipOfTrack(track), rank = track & 3;
        const bool digi = rank == 3 || tracks[size_t(track)].patch.generator == Generator::Digidrum;
        return k * 3 + (digi ? std::min(2, int(global.digidrumChannel[size_t(k)])) : std::min(2, rank));
    }
    // Voice reserved for the sequencer during playback: that of an active track (allocVoice of the prototype; in
    // dual chip, each channel is related to the tracks of its chip).
    bool reservedVoice(int index) const noexcept {
        if (!seq.on) return false;
        if (studio()) return index < kTracks && tracks[size_t(index)].mix.enabled;
        const int k = index / 3;
        for (int t = 4 * k; t < 4 * k + 4 && t < trackCount(); ++t)
            if (tracks[size_t(t)].mix.enabled && laneVoice(t) == index) return true;
        return false;
    }

    // Keyboard playing: free unreserved channel, then (16-channel) free voices of the manual playing, otherwise the
    // oldest of the unreserved voices, otherwise the oldest of all (voice stealing if everything is reserved).
    int allocVoice(int track) noexcept {
        int first = 0, count = kLaneVoices;
        if (!studio()) {
            first = chipOfTrack(track) * 3;
            count = 3;
        }
        int old = -1, oldAny = -1, echo = -1;
        auto repeat = [&](int i) {  // a repeat of the echo (D-299) gives way before a note
            const Voice& v = voices[size_t(i)];
            if (v.tag == VoiceTag::Echo && (echo < 0 || v.age < voices[size_t(echo)].age)) echo = i;
        };
        for (int i = first; i < first + count; ++i) {
            const Voice& v = voices[size_t(i)];
            if (oldAny < 0 || v.age < voices[size_t(oldAny)].age) oldAny = i;
            if (reservedVoice(i)) continue;
            if (!v.on) return i;
            repeat(i);
            if (old < 0 || v.age < voices[size_t(old)].age) old = i;
        }
        if (studio())
            for (int i = kLaneVoices + kChordVoices; i < kVoices; ++i) {
                if (!voices[size_t(i)].on) return i;
                repeat(i);
            }
        return echo >= 0 ? echo : old >= 0 ? old : oldAny;
    }

    // Note played live (keyboard, MIDI or keyboard arpeggio) at cycle c, one voice on each track that plays it: the
    // focused track, or every active track when Layer is on or the focused track is off.
    // `only` ≥ 0: this track alone (routing by channel), active or not.
    void trigger(int note, int velocity, VoiceTag tag, uint64_t c, int only = -1) noexcept {
        const double velLvl = global.velocity == VelocityMode::Off ? 1.0 : std::clamp(double(velocity) / 127, 0.05, 1.0);
        const int max = trackCount();
        if (only >= max) return;
        std::array<int, kTracks> layers{};
        int nl = 0;
        const int focus = global.focusTrack;
        if (only >= 0) layers[size_t(nl++)] = only;
        else if (!global.layer && focus >= 0 && focus < max && tracks[size_t(focus)].mix.enabled) layers[size_t(nl++)] = focus;
        else
            for (int i = 0; i < max; ++i)
                if (tracks[size_t(i)].mix.enabled) layers[size_t(nl++)] = i;
        if (nl == 0) layers[size_t(nl++)] = std::clamp(focus, 0, max - 1);  // no active track: focused track

        // 3-channel, or monophonic track: the previous notes of the track are released (legato in 16-channel mode).
        std::array<int, kTracks> glideFrom{};
        glideFrom.fill(-1);
        for (int l = 0; l < nl; ++l) {
            const int ci = layers[size_t(l)];
            const bool mono = lanePatch(ci, liveIns(ci)).mono;
            if (studio() && !mono) continue;
            int prev = -1;
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (!v.on || v.track != ci || v.tag != tag || v.note == note) continue;
                if (prev < 0 || v.age > voices[size_t(prev)].age) prev = i;
                relVoice(v, 64, c);
                touch(i, c);
            }
            if (prev >= 0 && mono && studio()) glideFrom[size_t(l)] = prev;
        }
        for (int l = 0; l < nl; ++l) {
            const int ci = layers[size_t(l)];
            const TrackParams& tp = tracks[size_t(ci)];
            // Instrument chosen by Program Change (D-156): sound, volume and generator, like a step whose INS row
            // names it (laneTrigger); role, level and panning stay those of the track.
            const int ins = liveIns(ci);
            const Instrument* x = instrument(ins);
            const TrackPatch& src = x ? x->patch : tp.patch;
            const int gen = laneGen(ci, ins, -1);
            const Generator g = gen >= 0 ? Generator(gen) : tp.patch.generator;
            int index;
            const bool dma3 = dmaTrack(ci);
            if (dma3) {
                index = laneVoice(ci);  // DMA track: its voice
            } else if (!studio() && g == Generator::Digidrum) {
                const int k = chipOfTrack(ci);
                index = k * 3 + std::min(2, int(global.digidrumChannel[size_t(k)]));
            } else {
                index = allocVoice(ci);
            }
            Voice& v = voices[size_t(index)];
            if (!studio() && !dma3) {
                v.chip = index / 3;
                v.channel = index % 3;
            }
            // Velocity: audio gain in 16-channel mode (accent), 4-bit volume in 3-channel mode with the level of the
            // track (prototype), or 4-bit volume everywhere; the 4-bit volume is the nearest level in decibels (D-200). A DMA voice has no 4-bit volume: audio gain, the level
            // of the track coming from the mixer (D-153).
            const bool dma = dma3 || (studio() && global.ste && g == Generator::Dma);
            int vol = src.volume;
            double level = studio() || dma ? velLvl : std::clamp(tp.mix.level, 0.0, 1.0) * velLvl;
            if (studio() && global.velocity == VelocityMode::Volume && !dma) {
                vol = attenuated(vol, velLvl);
                level = 1;
            }
            setupVoice(v, note, vol, tag, ci, level, c, gen, 1, x ? &x->patch : nullptr);
            const int from = glideFrom[size_t(l)];
            if (from >= 0 && from != index && voices[size_t(from)].on) {
                Voice& f = voices[size_t(from)];
                f.on = false;  // legato: the held note stops, the new one starts from its pitch
                touch(from, c);
                if (f.fCur > 0) v.fCur = f.fCur;
                slideTo(v, note, std::max(1.0, std::floor(hostRate() * kLegatoSeconds)), c);
            }
            touch(index, c);
        }
        if (!studio()) updateMutes(c);
    }

    // Stops every voice of one tag: its release (relVoice, a fade of 32 steps without an ADSR release), or with `hard`
    // a fade of 8 steps that also cuts the voices already in their release stage.
    void killTag(VoiceTag tag, bool hard, uint64_t c) noexcept {
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = voices[size_t(i)];
            if (!v.on || v.tag != tag || v.fading() || (!hard && v.stage == 4)) continue;
            if (hard) hardFade(v, 8, c);
            else relVoice(v, 32, c);
            touch(i, c);
        }
    }

    // ------------------------------------------------------------------------------------- Echo of the notes (D-299)
    // A note whose sound has its echo on is played again `repeats` times, one division of the tempo apart: repeat k
    // starts k periods after the note and is released k periods after it, with the sound, the generator and the
    // drum transposition of the note, at the level of `k × drop` steps of the 4-bit volume lower, on the target track.
    // 3 channels: the repeat takes the channel of the note (that of the target track if there is one) only if it plays
    // no note of its own (off, an earlier repeat, or a note being released); otherwise it is skipped, and a new note
    // always takes a channel back from a repeat. 16 channels: a free voice of the pool, otherwise the oldest repeat.
    uint64_t hostNow() const noexcept { return rOut ? rStart + rI : samples; }
    // Host sample of repeat k: k periods after `from` (the attack or the release of the note), rounded up.
    static uint64_t echoAt(uint64_t from, const EchoNote& e, int k) noexcept {
        return from + uint64_t(std::ceil(double(k) * e.period));
    }
    void echoAttack(Voice& v, const TrackPatch& src, const TrackPatch* patch, int track, double note, int vol,
                    double level, int gen, double drumTr, uint64_t c) noexcept {
        const TrackEcho& x = src.echo;
        // A Sync Buzzer in 3 channels has no echo (ninth DAW test, item 115, D-316): its level is the hardware
        // envelope that the channels share, which a repeat would restart at full level, whatever its Drop.
        if (!studio() && (gen >= 0 ? Generator(gen) : src.generator) == Generator::SyncBuzzer) return;
        const int drop = std::clamp<int>(x.drop, 0, kEchoDropMax);
        // A repeat 15 steps down or more would be silent: the repeats stop before.
        const int repeats = std::min(std::clamp<int>(x.repeats, 1, kEchoRepeatsMax), drop > 0 ? 14 / drop : 99);
        if (repeats <= 0) return;
        EchoNote* e = nullptr;
        for (EchoNote& f : echoes)
            if (!f.id) {
                e = &f;
                break;
            }
        if (!e) {  // all taken: the oldest note gives up its repeats (those playing are released)
            e = &*std::min_element(echoes.begin(), echoes.end(),
                                   [](const EchoNote& a, const EchoNote& b) { return a.start < b.start; });
            echoCut(*e, c);
        }
        *e = EchoNote{};
        if (++echoSerial == 0) ++echoSerial;
        e->id = echoSerial;
        e->src = track;
        e->dst = x.target >= 0 && x.target < trackCount() ? int(x.target) : track;
        if (patch && instruments)  // instrument of the note: its sound is looked up again at each repeat
            for (int k = 0; k < kInstruments; ++k)
                if (&(*instruments)[size_t(k)].patch == patch) e->ins = k + 1;
        e->gen = gen;
        e->vol = vol;
        e->voice = int(&v - voices.data());
        e->note = note;
        e->level = level;
        e->drumTr = drumTr;
        e->start = hostNow();
        const double beats = kDelayDivisionBeats[std::min<size_t>(x.division, kDelayDivisionBeats.size() - 1)];
        e->period = std::max(1.0, 60 / seqBpm() * beats * hostRate());
        e->repeats = repeats;
        e->drop = drop;
        v.echoId = e->id;
        echoDue = std::min(echoDue, e->start + kEchoPoll);
    }
    // The note of the voice is released, cut or replaced: its repeats will be released as far from theirs.
    void echoRelease(Voice& v) noexcept {
        if (!v.echoId || v.tag == VoiceTag::Echo) return;
        for (EchoNote& e : echoes)
            if (e.id == v.echoId && e.end == kNever) e.end = std::max(e.start, hostNow());
        v.echoId = 0;
    }
    // Attack of repeat k at cycle c, on the voice chosen by the rules above. Its gain is the level of 4-bit volume
    // 15 − k × drop relative to volume 15 (`down`), so the repeat sounds k × drop volume steps lower.
    void echoPlay(const EchoNote& e, int k, uint64_t c) noexcept {
        const Instrument* x = instrument(e.ins);
        const TrackPatch& src = x ? x->patch : tracks[size_t(e.src)].patch;
        const Generator g = e.gen >= 0 ? Generator(e.gen) : src.generator;
        int vi = -1;
        if (studio()) {
            int old = -1;
            for (int i = kLaneVoices + kChordVoices; i < kVoices && vi < 0; ++i) {
                const Voice& v = voices[size_t(i)];
                if (!v.on) vi = i;
                else if (v.tag == VoiceTag::Echo && (old < 0 || v.age < voices[size_t(old)].age)) old = i;
            }
            if (vi < 0) vi = old;
            if (vi < 0) return;
        } else {
            // The channel of the note (keyboard: the one it was given), or that of the target track.
            const int chip = chipOfTrack(e.dst);
            if (e.dst == e.src && e.voice < kDma3Voice + kDmaTracks) vi = e.voice;
            else if (!dmaTrack(e.dst) && g == Generator::Digidrum)
                vi = chip * 3 + std::min(2, int(global.digidrumChannel[size_t(chip)]));
            else vi = laneVoice(e.dst);
            const Voice& v = voices[size_t(vi)];
            if (v.on && v.tag != VoiceTag::Echo && !v.fading() && v.stage != 4) return;  // a note of its own
        }
        Voice& v = voices[size_t(vi)];
        if (!studio() && !dmaTrack(e.dst)) {
            v.chip = vi / 3;
            v.channel = vi % 3;
        }
        const double down = tab1[size_t(2 * (15 - k * e.drop) + 1)] / tab1[31];
        setupVoice(v, e.note, e.vol, VoiceTag::Echo, e.dst, e.level * down, c, e.gen, e.drumTr,
                   x ? &x->patch : nullptr);
        v.echoId = e.id;
        v.echoRep = uint8_t(k);
        touch(vi, c);
        if (!studio()) updateMutes(c);
    }
    // Release of repeat k, on whichever voice still plays it.
    void echoStop(const EchoNote& e, int k, uint64_t c) noexcept {
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = voices[size_t(i)];
            if (!v.on || v.tag != VoiceTag::Echo || v.echoId != e.id || v.echoRep != k) continue;
            relVoice(v, 32, c);
            touch(i, c);
        }
    }
    // The repeats of the note stop: those playing are released.
    void echoCut(EchoNote& e, uint64_t c) noexcept {
        for (int k = e.nextOff; k < e.nextOn; ++k) echoStop(e, k, c);
        e.id = 0;
    }
    void echoClear() noexcept {
        echoes.fill(EchoNote{});
        echoDue = kNever;
    }
    // At the sample echoDue of the render: attacks and releases due, end of the notes that stopped by themselves (end
    // of a drum, of a fade) or that another note replaced.
    void echoRun() noexcept {
        const uint64_t now = hostNow();
        bool caught = false;
        uint64_t c = 0;
        auto cycle = [&] {
            if (!caught) {
                lanesCatchUp();
                c = at();
                caught = true;
            }
            return c;
        };
        uint64_t due = kNever;
        for (EchoNote& e : echoes) {
            if (!e.id) continue;
            if (e.end == kNever) {
                const Voice& s = voices[size_t(e.voice)];
                if (!s.on || s.echoId != e.id || s.tag == VoiceTag::Echo) e.end = std::max(e.start, now);
            }
            while (e.nextOn <= e.repeats && echoAt(e.start, e, e.nextOn) <= now) echoPlay(e, e.nextOn++, cycle());
            while (e.end != kNever && e.nextOff < e.nextOn && echoAt(e.end, e, e.nextOff) <= now)
                echoStop(e, e.nextOff++, cycle());
            if (e.nextOff > e.repeats) {
                e.id = 0;
                continue;
            }
            if (e.nextOn <= e.repeats) due = std::min(due, echoAt(e.start, e, e.nextOn));
            if (e.end == kNever) due = std::min(due, now + kEchoPoll);
            else if (e.nextOff < e.nextOn) due = std::min(due, echoAt(e.end, e, e.nextOff));
        }
        echoDue = due;
    }

    // Modulation carried from one note of an arpeggio to the next (D-203), like the envelope (grabEnv / putEnv): the
    // table, the vibrato, the LFO (D-257), the cadence of the frames and the chained instrument go on instead of
    // restarting at each note (at 50 Hz, the rate of the frames, the table stayed on its first step). The first frame
    // of the new note, at once, plays the step that sounded with the new pitch.
    struct ModCarry {
        bool on = false, chain = false;
        int track = -1, pos = 0, frm = 0;
        double vib = 0, lfo = 0, lfoRand = 0;
        uint64_t vblStart = 0, vblIndex = 0;
        uint32_t vblHz = 50;
        uint8_t linked = 0;
    };
    // Modulation of the outgoing note at cycle c; none (on = false) for a note released or fading, without VBL
    // frames, or a drum.
    static ModCarry grabMod(const Voice& v, uint64_t c) noexcept {
        ModCarry m;
        if (!v.on || v.fading() || v.stage == 4 || !v.vblOn || v.frames || isDrum(v.gen)) return m;
        const bool due = v.nextVbl <= c;  // frame of cycle c not played yet: the new note plays it
        if (!due && v.vblIndex == 0) return m;  // VBL rate just changed: the table restarts
        m.on = true;
        m.track = v.track;
        m.vblStart = v.vblStart;
        m.vblHz = v.vblHz;
        m.linked = v.linked;
        m.chain = due && v.chainNext;
        m.pos = due ? v.ornPos : v.ornPrevPos;  // otherwise the new note plays the last frame again
        m.frm = due ? v.ornFrm : v.ornPrevFrm;
        m.vib = due ? v.vibPhase : v.vibPrev;
        m.lfo = due ? v.lfoPhase : v.lfoPrev;
        m.lfoRand = due ? v.lfoRand : v.lfoRandPrev;
        m.vblIndex = due ? v.vblIndex : v.vblIndex - 1;
        return m;
    }
    // Hands it to the new note if it is on the same track; its next frame is due at once (cycle c).
    bool putMod(Voice& v, const ModCarry& m, uint64_t c) noexcept {
        if (!m.on || !v.on || v.track != m.track || !v.vblOn || v.frames || isDrum(v.gen)) return false;
        if (m.linked) {
            v.link = m.linked;
            relink(v, c);
        }
        v.ornPos = m.pos;
        v.ornFrm = m.frm;
        v.vibPhase = m.vib;
        v.lfoPhase = m.lfo;
        v.lfoRand = m.lfoRand;
        v.chainNext = m.chain;
        v.vblHz = m.vblHz;
        v.vblStart = m.vblStart;
        v.vblIndex = m.vblIndex;
        v.nextVbl = c;
        return true;
    }

    // --------------------------------------------------------------------------------- Keyboard arpeggio
    // Arpeggio of the focused track, or of the instrument chosen by Program Change (D-156).
    bool arpEnabled() const noexcept {
        const int f = std::clamp(global.focusTrack, 0, kTracks - 1);
        return arpSettings().on && !isDrum(tracks[size_t(f)].patch.generator);
    }
    const TrackArp& arpSettings() const noexcept {
        const int f = std::clamp(global.focusTrack, 0, kTracks - 1);
        return laneArp(f, liveIns(f));
    }
    bool arpReleasing() const noexcept {
        for (const Voice& v : voices)
            if (v.on && v.tag == VoiceTag::Arp && v.stage == 4 && !v.fading()) return true;
        return false;
    }
    // Polyphonic arpeggio (24/10/2026, D-416): the arpeggio is one note per key pressed (or MIDI note), not one per
    // step (Indus, 20:33): its envelope runs on from step to step (the prototype's grabEnv / putEnv), and each key
    // pressed starts a note of its own from the attack. In 16 channels, on a track whose Voicing is Polyphonic, the
    // note before then goes on to its release while the new one plays; keys up, the last note walks on through the
    // arpeggio during its release (19:32), the notes before it ending theirs at their pitch. Otherwise (Monophonic,
    // 3-channel modes) the note before is cut.
    bool arpPoly() const noexcept {
        const int f = std::clamp(global.focusTrack, 0, kTracks - 1);
        return studio() && !lanePatch(f, liveIns(f)).mono;
    }
    // Arpeggios played on the half pulses (48 per quarter note, arpPulse; D-289): those of the MIDI clock, or of the
    // host's position when synced to the tempo while it plays in Host sync (D-256). Otherwise a phase at the cadence
    // of arpRate.
    bool arpOnPulses() const noexcept {
        return global.arpClock == ArpClock::MidiClock ||
               (global.arpClock == ArpClock::Tempo && global.seqClock == SeqClock::Host && host.playing);
    }
    // Cadence in Hz of an arpeggio: its Rate, or synced to the tempo, one step per division (D-256).
    double arpRate(double rateHz) const noexcept {
        if (global.arpClock != ArpClock::Tempo) return rateHz;
        return seqBpm() * 48 / 60 / std::max<int>(1, global.arpDivision);
    }
    // One step of the keyboard arpeggio at cycle c. Table: the last held note plus the offset of the next step.
    // Other modes: the held notes (sorted by pitch, except Order) over 1 to 3 octaves, walked up, down, up and down
    // without repeating the ends, or at random. Keys up and the last note in its release: the step only retunes it
    // (in a polyphonic arpeggio, arpPoly, the newest note only).
    void arpTick(uint64_t c) noexcept {
        const TrackArp& a = arpSettings();
        const bool poly = arpPoly();
        const bool rel = heldCount == 0 && arpReleasing();
        const bool fresh = std::exchange(arp.fresh, false);
        std::array<int, 128> held{};
        int nh = 0;
        if (heldCount > 0)
            for (int i = 0; i < heldCount; ++i) held[size_t(nh++)] = heldList[size_t(i)];
        else if (rel && arp.lastSrc >= 0)
            held[size_t(nh++)] = arp.lastSrc;
        if (nh == 0) return;
        int n = 0, srcNote = 0;
        if (a.mode == ArpMode::Table) {
            const int len = std::clamp<int>(a.length, 1, kArpSteps);
            arp.idx = (arp.idx + 1) % len;
            arp.cur = arp.idx;
            srcNote = held[size_t(nh - 1)];
            n = srcNote + a.table[size_t(arp.idx)];
        } else {
            std::array<int, 128> sorted{};
            for (int i = 0; i < nh; ++i) sorted[size_t(i)] = held[size_t(i)];
            if (a.mode != ArpMode::Order)  // insertion sort (stable, without allocation, order of pressing on a tie)
                for (int i = 1; i < nh; ++i) {
                    const int x = sorted[size_t(i)];
                    int j = i;
                    for (; j > 0 && sorted[size_t(j - 1)] > x; --j) sorted[size_t(j)] = sorted[size_t(j - 1)];
                    sorted[size_t(j)] = x;
                }
            const int oct = std::clamp<int>(a.octaves, 1, 3);
            const int len = nh * oct;
            int k = 0;
            if (a.mode == ArpMode::Random) {
                k = int(std::floor(arpRng.next() * len));
            } else if (a.mode == ArpMode::Down) {
                arp.idx = (arp.idx + 1) % len;
                k = len - 1 - arp.idx;
            } else if (a.mode == ArpMode::UpDown) {
                if (len < 2) arp.idx = 0;
                else {
                    arp.idx += arp.dir;
                    if (arp.idx >= len) {
                        arp.idx = len - 2;
                        arp.dir = -1;
                    }
                    if (arp.idx < 0) {
                        arp.idx = 1;
                        arp.dir = 1;
                    }
                }
                k = std::min(arp.idx, len - 1);
            } else {
                arp.idx = (arp.idx + 1) % len;
                k = arp.idx;
            }
            n = sorted[size_t(k % nh)] + 12 * (k / nh);
            srcNote = sorted[size_t(k % nh)];
        }
        arp.lastSrc = srcNote;
        if (rel) {  // release: only the pitch changes
            uint64_t last = 0;  // polyphonic: the newest note, the others keep their pitch
            if (poly)
                for (const Voice& v : voices)
                    if (v.on && v.tag == VoiceTag::Arp && v.stage == 4 && !v.fading()) last = std::max(last, v.age);
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (v.on && v.tag == VoiceTag::Arp && v.stage == 4 && !v.fading() && v.age >= last) {
                    retune(v, n, c);
                    touch(i, c);
                }
            }
            return;
        }
        // The envelope continues from one step of the arpeggio to the next (grabEnv / putEnv), the modulation too
        // (D-203), until a key is pressed: a new note, from the attack (D-416). The step before is cut (a fade of 8
        // steps); in a polyphonic arpeggio, the notes before in their release end it, and the note of the keys
        // pressed before goes on to its release when a key is pressed.
        bool haveEnv = false;
        uint8_t envStage = 0;
        double envVal = 0;
        ModCarry mod;
        if (!fresh)
            for (const Voice& v : voices)
                if (v.on && v.tag == VoiceTag::Arp && !v.fading() && v.stage != 4) {
                    if (v.adsrOn) {
                        haveEnv = true;
                        envStage = v.stage;
                        envVal = adsrAt(v, c);
                    }
                    mod = grabMod(v, c);
                    break;
                }
        if (poly && fresh) killTag(VoiceTag::Arp, false, c);
        else if (poly)
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (!v.on || v.tag != VoiceTag::Arp || v.fading() || v.stage == 4) continue;
                hardFade(v, 8, c);
                touch(i, c);
            }
        else killTag(VoiceTag::Arp, true, c);
        const int vel = heldVel[size_t(std::clamp(srcNote, 0, 127))];
        const uint64_t born = stamp;  // the voices of this step: those attacked from now on
        trigger(n, vel > 0 ? vel : 100, VoiceTag::Arp, c, arp.track);
        if (haveEnv || mod.on)
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (!v.on || v.tag != VoiceTag::Arp || v.fading() || v.age < born) continue;
                bool put = false;
                if (haveEnv && v.adsrOn && envStage != 4) {
                    enterStage(v, envStage, envVal, c);
                    put = true;
                }
                put |= putMod(v, mod, c);
                if (put) touch(i, c);
            }
    }

    // -------------------------------------------------------------------------------------------- Events
    // One queued event (notes, controllers, transport, previews, mixer), applied at cycle c of the sample it arrived
    // on; the render loop has already flushed the audio before that sample.
    void applyEvent(const Event& e, uint64_t c) noexcept {
        switch (e.type) {
        case EventType::NoteOn: {
            const int note = e.a, vel = e.b, only = e.track;
            mpeNoteCh = int(e.value);  // MPE member channel (D-432), for the voices this note attacks now
            if (only >= 0 && only != std::clamp(global.focusTrack, 0, kTracks - 1)) {
                trigger(note, vel, VoiceTag::Manual, c, only);  // routing by channel: track not focused, no arpeggio
                mpeNoteCh = 0;
                break;
            }
            bool already = false;
            for (int i = 0; i < heldCount; ++i) already |= heldList[size_t(i)] == note;
            heldVel[size_t(note)] = vel;
            heldSrc[size_t(note)] = int8_t(only);
            arp.track = only;
            if (arpEnabled()) {
                mpeNoteCh = 0;        // the arpeggio's notes have no MPE channel
                if (already) return;  // already held: the arpeggio does not change
                arp.idx = -1;         // the arpeggio restarts from the beginning when a note is added
                arp.fresh = true;     // a note of its own, from the attack (D-416)
            } else {
                trigger(note, vel, VoiceTag::Manual, c, only);  // MIDI: a note already held is played again
                mpeNoteCh = 0;
            }
            if (!already && heldCount < 128) heldList[size_t(heldCount++)] = uint8_t(note);
            break;
        }
        case EventType::NoteOff: {
            const int note = e.a, only = e.track, ch = int(e.value);  // ch: MPE member channel, its note only (D-432)
            // The key leaves the arpeggio when the channel that holds it releases it, even if another track has been
            // focused since (otherwise it would stay held for good).
            if (only < 0 || only == heldSrc[size_t(note)]) {
                int w = 0;
                for (int i = 0; i < heldCount; ++i)
                    if (heldList[size_t(i)] != note) heldList[size_t(w++)] = heldList[size_t(i)];
                heldCount = w;
                if (arpEnabled() && heldCount == 0) killTag(VoiceTag::Arp, false, c);
            }
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (ch && v.mpeCh != ch) continue;
                // Digidrum in sustain loop: the loop stops, the sample plays to the end (D-154).
                if (v.on && v.looping && v.note == note && v.tag == VoiceTag::Manual && (only < 0 || v.track == only))
                    v.looping = false;
                if (!v.on || v.note != note || v.tag != VoiceTag::Manual || v.fading() || isDrum(v.gen)) continue;
                if (only >= 0 && v.track != only) continue;
                if (v.stage != 4) relVoice(v, 64, c);
                touch(i, c);
            }
            break;
        }
        case EventType::Program:
            for (int t = 0; t < kTracks; ++t)
                if (e.track < 0 || e.track == t) program[size_t(t)] = uint8_t(e.a);
            break;
        case EventType::Bend: {
            const std::array<double, kTracks> was = bend;
            for (int t = 0; t < kTracks; ++t)
                if (e.track < 0 || e.track == t) bend[size_t(t)] = e.value;
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (!v.on || (e.track >= 0 && v.track != e.track)) continue;
                if (global.bendTarget != BendTarget::Pitch) {  // volume or envelope: at the next frame (D-432)
                    if (expressive(v)) startFrames(v, c);
                    continue;
                }
                bendVoice(v, v.track >= 0 ? bend[size_t(v.track)] - was[size_t(v.track)] : 0.0, c);
                touch(i, c);
            }
            break;
        }
        case EventType::Wheel:
            for (int t = 0; t < kTracks; ++t)
                if (e.track < 0 || e.track == t) wheel[size_t(t)] = std::max(0.0, e.value);
            for (Voice& v : voices)
                if (v.on && (e.track < 0 || v.track == e.track) && expressive(v)) startFrames(v, c);
            break;
        // MIDI expression (D-432), at the next VBL frame of the voices it moves.
        case EventType::Pressure:
            for (int t = 0; t < kTracks; ++t)
                if (e.track < 0 || e.track == t) press[size_t(t)] = e.value;
            for (Voice& v : voices)
                if (v.on && (e.track < 0 || v.track == e.track) && expressive(v)) startFrames(v, c);
            break;
        case EventType::NotePressure:
            for (Voice& v : voices) {
                if (!v.on || v.note != e.a || v.tag != VoiceTag::Manual || v.mpeCh) continue;
                if (e.track >= 0 && v.track != e.track) continue;
                v.press = e.value;
                if (expressive(v)) startFrames(v, c);
            }
            break;
        case EventType::MpeBend:
        case EventType::MpePressure:
        case EventType::MpeSlide: {
            MpeChannel& ch = mpe[size_t(std::clamp<int>(e.a, 1, kMpeChannels - 1))];
            if (e.type == EventType::MpeBend) ch.bend = e.value;
            else if (e.type == EventType::MpePressure) ch.press = e.value;
            else ch.slide = e.value;
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (!v.on || v.mpeCh != e.a) continue;
                if (e.type == EventType::MpeBend) {
                    const double d = ch.bend - v.mpeBend;
                    v.mpeBend = ch.bend;
                    bendVoice(v, d, c);
                    touch(i, c);
                } else {
                    v.press = ch.press;
                    v.slide = ch.slide;
                    if (expressive(v)) startFrames(v, c);
                }
            }
            break;
        }
        case EventType::AllOff:
            heldCount = 0;  // panic: no held note any more (the arpeggio stops)
            dmaBank.stopPreview();
            digiPreviewSlot = -1;
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (!v.on || v.fading()) continue;
                hardFade(v, 64, c);
                touch(i, c);
            }
            echoClear();
            break;
        case EventType::PlayerStart:  // ymplay: killTag('man'), killTag('seq')
            killTag(VoiceTag::Manual, false, c);
            killTag(VoiceTag::Chord, false, c);
            killTag(VoiceTag::Sequencer, false, c);
            killTag(VoiceTag::Echo, false, c);
            echoClear();
            break;
        case EventType::ManualOff:
            for (int i = 0; i < kVoices; ++i) {
                Voice& v = voices[size_t(i)];
                if (!v.on || v.tag != VoiceTag::Manual || v.fading()) continue;
                hardFade(v, 16, c);
                touch(i, c);
            }
            break;
        case EventType::MidiClock:  // in Host sync, the clock comes from the host transport
            if (global.seqClock == SeqClock::Host) break;
            // An odd division (1/64) also counts the half pulse between two clocks (D-289), as the sequencer at
            // 1/64 (D-422); the others count two half pulses per clock, so their arpeggios fall on the clocks as in
            // the prototype.
            if (halfPulseIn > 0) halfPulse(c);  // half pulse not played yet (faster clock): never lost
            arpPulse(c, kNoPulse, true, arpHalves() ? 1 : 2);
            seqMidiClock(c);  // sequencer step
            halfPulseIn = arpHalves() || seqHalves() ? seq.spt / 2 : 0;
            break;
        case EventType::MidiStart:
            if (global.seqClock == SeqClock::Host) break;
            halfPulseIn = 0;
            arp.clockTicks = std::max<int>(1, global.arpDivision) - 1;
            arp.idx = -1;
            seqMidiStart();
            break;
        case EventType::MidiStop:
            halfPulseIn = 0;
            seqMidiStop(c);
            break;
        case EventType::MidiContinue:
            if (global.seqClock == SeqClock::Midi) seq.run = true;
            break;
        case EventType::MidiSongPosition: seqSongPosition(e.a); break;
        case EventType::SeqPlay:  // Host sync: the host transport decides
            if (global.seqClock != SeqClock::Host) seqStart(c);
            break;
        case EventType::SeqStop:
            if (global.seqClock != SeqClock::Host) seqHalt(c);
            break;
        case EventType::SeqQueue: selectPattern(e.a); break;
        case EventType::SeqSongJump: songJump(e.a); break;
        case EventType::SeqPreview:
            if (e.track >= 0 && e.track < trackCount()) laneTrigger(e.track, e.a, seqLevel(e.track, e.value * 15), -1, 1, 0, c);
            break;
        case EventType::DigiPreview: {
            const int k = std::clamp<int>(e.a, 0, kDigidrumSlots - 1);
            const DigidrumSlot& d = digiSlots[size_t(k)];
            const SampleSpan part = sampleSpan(d.start, d.end, userLen[size_t(k)]);  // Start to End (D-443)
            digiPreviewSlot = part.size() ? k : -1;
            digiPreviewPos = part.start;
            digiPreviewLooping = d.loop;
            digiPreviewInc = d.rateHz * std::pow(2.0, d.cents / 1200) / double(hostRate());
            break;
        }
        case EventType::DrumPreview:
            previewSlot = std::clamp<int>(e.a, 0, kDrumSlots - 1);
            previewPos = 0;
            break;
        case EventType::DigiStop: digiPreviewSlot = -1; break;
        case EventType::DmaStop: dmaBank.stopPreview(); break;
        case EventType::DmaPreview: dmaBank.preview(e.a); break;
        case EventType::Mix: {
            const int t = std::clamp<int>(e.a, 0, kTracks - 1);
            const bool off = tracks[size_t(t)].mix.enabled && !e.mix.enabled;
            tracks[size_t(t)].mix = e.mix;
            refreshLive();
            if (off) laneOff(t, c);  // track switched off: its sequencer notes stop
            remix(c);
            break;
        }
        }
    }

    // ------------------------------------------------------------------------------------------ Render
    // Master cycle at which the events received at the current sample apply (everything before is already
    // computed).
    uint64_t eventCycle() const noexcept {
        if (studio()) return 8 * std::max(tickAtSample(samples - base), genPos);
        return chips[0].cycleAtSample(samples - base);
    }
    // Cycle of the current sample of the render (rI), without running the render up to there.
    uint64_t pendingCycle() const noexcept {
        const uint64_t at = samples - base + (rOut && rI > rSeg ? rI - rSeg : 0);
        if (studio()) return 8 * std::max(tickAtSample(at), genPos);
        return chips[0].cycleAtSample(at);
    }

    // Levels of the stereo outputs (seventh DAW test, D-284): Master and Master 2, then the balance, which lowers the
    // opposite side only (the centre keeps both at their level).
    double masterLeft() const noexcept { return global.master * (global.masterPan > 0 ? 1 - global.masterPan : 1); }
    double masterRight() const noexcept { return global.master2 * (global.masterPan < 0 ? 1 + global.masterPan : 1); }

    // Output law of a buffer, in place: tanh(x × g) with the soft clip on (D-049), otherwise x × g.
    void output(float* x, uint32_t n, double g) const noexcept {
        if (global.softClip)
            for (uint32_t i = 0; i < n; ++i) x[i] = float(std::tanh(double(x[i]) * g));
        else
            for (uint32_t i = 0; i < n; ++i) x[i] = float(double(x[i]) * g);
    }

    // Preview of the kit (dprev of the worklet): the 4-bit sample of the instrument (16-channel render, 7 800 Hz),
    // read without interpolation, added to the mix before the output law, (s - 7.5) / 7.5 × 0.55, without panning,
    // in both modes. scale: unit of the mix it enters (1 in 16-channel mode; in 3-channel mode, √2 / gain3, because
    // the prototype adds the preview to mix, which its law multiplies by √2, whereas the chip, centred, only
    // receives cos π/4 × √2 = 1 of it).
    void mixDrumPreview(float* left, float* right, uint32_t n, double scale) noexcept {
        if (previewSlot < 0) return;
        const auto s = kit.sample(previewSlot);
        const double inc = double(DrumKit::kSampleRate) / double(hostRate());
        for (uint32_t i = 0; i < n; ++i) {
            const size_t k = size_t(previewPos);
            if (k >= s.size()) {
                previewSlot = -1;
                return;
            }
            const float v = float((double(s[k]) - 7.5) / 7.5 * 0.55 * scale);
            left[i] += v;
            if (right) right[i] += v;
            previewPos += inc;
        }
    }

    // Preview of a Digidrum slot (editor, D-154): like the preview of the kit, but through the level table of the
    // chip (one channel alone, tab1 at level 2n + 1), scaled as levels 0 to 15 over ±0.55, to hear the 4-bit
    // conversion as the chip renders it. It centres on the level of silence (the slot's rest value), not halfway
    // between levels 0 and 15: a sample that starts or ends silent starts or ends at 0, without a click (D-455). From
    // Start to End (D-443), read again at each block. With the slot's Loop on, the part repeats until the preview is
    // stopped or Loop goes off (D-449); otherwise it plays once.
    void mixDigiPreview(float* left, float* right, uint32_t n, double scale) noexcept {
        if (digiPreviewSlot < 0) return;
        const size_t slot = size_t(digiPreviewSlot);
        const DigidrumSlot& d = digiSlots[slot];
        if (d.loop) digiPreviewLooping = true;
        else if (digiPreviewLooping) {
            digiPreviewSlot = -1;
            return;
        }
        const uint8_t* s = user[slot].data();
        const SampleSpan part = sampleSpan(d.start, d.end, userLen[slot]);
        const double half = (tab1[31] - tab1[1]) / 2, mid = tab1[size_t(2 * std::min<int>(d.rest, 15) + 1)];
        for (uint32_t i = 0; i < n; ++i) {
            auto k = uint32_t(digiPreviewPos);
            if (k >= part.end && d.loop && part.size()) {
                digiPreviewPos = part.start + std::fmod(digiPreviewPos - part.end, double(part.size()));
                k = uint32_t(digiPreviewPos);
            }
            if (k >= part.end) {
                digiPreviewSlot = -1;
                return;
            }
            const float v = float((tab1[size_t(2 * std::min<int>(s[k], 15) + 1)] - mid) / half * 0.55 * scale);
            left[i] += v;
            if (right) right[i] += v;
            digiPreviewPos += digiPreviewInc;
        }
    }

    // 16-channel render of the n samples at offset a of the block: the voices write their steps up to the internal
    // step that the resampler needs, the buses are integrated and resampled, then come the delay, the previews and
    // the output law. The chip outputs stay silent in this mode.
    void flushStudio(const Outputs& out, uint32_t a, uint32_t n) noexcept {
        float* left = out.left + a;
        float* right = out.right + a;
        const uint64_t endTick = tickAtSample(samples - base + n);
        const uint64_t target = std::max({endTick, outL.ticksNeeded(n), genPos});
        const uint64_t count = target - genPos;
        if (count > 0) {
            buildBuckets();
            for (int i = 0; i < kVoices; ++i)
                if (voices[size_t(i)].on) renderStudioVoice(i, genPos, target);
            const size_t w = size_t(genPos - bufBase);
            bool any = false;
            uint16_t onTracks = 0;
            for (const Voice& v : voices) {
                any |= v.on;
                if (v.on && v.track >= 0) onTracks = uint16_t(onTracks | (1u << v.track));
            }
            if (trackMask) integrateTracks(w, count, onTracks);
            meterStudio(count, onTracks);
            if (sendLive) integrateSend(w, count, any);
            integrateBus(w, count, any);
            genPos = target;
        }
        outL.render(bufL.data(), bufBase, left, n);
        outR.render(bufR.data(), bufBase, right, n);
        if (sendLive) {  // echoes of the sends (D-299), before the Master like the rest of the mix
            outSL.render(bufSL.data(), bufBase, sendL.data(), n);
            outSR.render(bufSR.data(), bufBase, sendR.data(), n);
            setDelay();
            delay.process(sendL.data(), sendR.data(), left, right, n);
        }
        mixDrumPreview(left, right, n, 1.0);
        mixDigiPreview(left, right, n, 1.0);
        dmaBank.mixPreview(left, right, n, double(hostRate()), 1.0);
        // Output of the prototype: tanh(mix × volume × 1.2 × √2), panning included (D-049); a level per side
        // (D-284).
        const double g = 1.2 * 1.4142135623730951;
        output(left, n, masterLeft() * g);
        output(right, n, masterRight() * g);
        // Separate outputs: same law, centred (cos π/4 × √2 = 1).
        for (int tr = 0; tr < kTracks; ++tr) renderTrackOut(out, a, n, tr);
        for (float* c : out.chip)
            if (c) std::fill(c + a, c + a + n, 0.0f);
        dropConsumed();
    }

    // Send buses of the delay (D-299): the same integration. Before integrateBus, which clears slopeWritten.
    void integrateSend(size_t w, uint64_t count, bool any) noexcept {
        if (slopeWritten || sigSL != 0 || sigSR != 0) {
            for (size_t i = 0; i < count; ++i) {
                vSL += sigSL + dSL[i];
                sigSL += sSL[i];
                vSR += sigSR + dSR[i];
                sigSR += sSR[i];
                bufSL[w + i] = float(vSL);
                bufSR[w + i] = float(vSR);
                dSL[i] = dSR[i] = sSL[i] = sSR[i] = 0;
            }
        } else {
            for (size_t i = 0; i < count; ++i) {
                vSL += dSL[i];
                vSR += dSR[i];
                bufSL[w + i] = float(vSL);
                bufSR[w + i] = float(vSR);
                dSL[i] = dSR[i] = 0;
            }
        }
        if (!any) vSL = vSR = sigSL = sigSR = 0;
    }
    // Left and right buses: integration of the steps and slopes of steps [genPos, genPos + count), written at w.
    void integrateBus(size_t w, uint64_t count, bool any) noexcept {
        if (slopeWritten || sigL != 0 || sigR != 0) {
            for (size_t i = 0; i < count; ++i) {
                vL += sigL + dL[i];
                sigL += sL[i];
                vR += sigR + dR[i];
                sigR += sR[i];
                bufL[w + i] = float(vL);
                bufR[w + i] = float(vR);
                dL[i] = dR[i] = sL[i] = sR[i] = 0;
            }
        } else {  // steps alone (no smooth ADSR in progress): sL and sR are zero
            for (size_t i = 0; i < count; ++i) {
                vL += dL[i];
                vR += dR[i];
                bufL[w + i] = float(vL);
                bufR[w + i] = float(vR);
                dL[i] = dR[i] = 0;
            }
        }
        slopeWritten = false;
        if (!any) vL = vR = sigL = sigR = 0;  // everything is silent: the bus starts again from an exact zero
    }
    // Separate output of a track rendered from its bus (16-channel, and DMA tracks in 3-channel): same law, centred.
    void renderTrackOut(const Outputs& out, uint32_t a, uint32_t n, int tr) noexcept {
        float* o = out.track[size_t(tr)] ? out.track[size_t(tr)] + a : nullptr;
        if (!trackOut(tr)) {
            if (o) std::fill(o, o + n, 0.0f);
            return;
        }
        outT[size_t(tr)].render(bufT[size_t(tr)].data(), bufBase, o ? o : scratch.data(), n);
        if (o) output(o, n, global.master * 1.2);
    }
    // The unconsumed steps of the buses move to the head of the buffers.
    void dropConsumed() noexcept {
        const uint64_t consumed = outL.ticksConsumed();
        const size_t keep = size_t(genPos - consumed), drop = size_t(consumed - bufBase);
        if (drop > 0) {
            std::copy(bufL.begin() + std::ptrdiff_t(drop), bufL.begin() + std::ptrdiff_t(drop + keep), bufL.begin());
            std::copy(bufR.begin() + std::ptrdiff_t(drop), bufR.begin() + std::ptrdiff_t(drop + keep), bufR.begin());
            if (sendLive) {
                std::copy(bufSL.begin() + std::ptrdiff_t(drop), bufSL.begin() + std::ptrdiff_t(drop + keep),
                          bufSL.begin());
                std::copy(bufSR.begin() + std::ptrdiff_t(drop), bufSR.begin() + std::ptrdiff_t(drop + keep),
                          bufSR.begin());
            }
            for (int tr = 0; tr < kTracks; ++tr)
                if (trackOut(tr)) {
                    auto& b = bufT[size_t(tr)];
                    std::copy(b.begin() + std::ptrdiff_t(drop), b.begin() + std::ptrdiff_t(drop + keep), b.begin());
                }
            bufBase = consumed;
        }
    }

    // Buses of the separate outputs (16-channel): integration of the steps and slopes, like the left and right buses.
    void integrateTracks(size_t w, uint64_t count, uint16_t onTracks) noexcept {
        for (int tr = 0; tr < kTracks; ++tr) {
            if (!trackOut(tr)) continue;
            const size_t t = size_t(tr);
            float* b = bufT[t].data() + w;
            double v = vT[t], sig = sigT[t];
            if (!wroteT[t] && v == 0 && sig == 0) {
                std::fill(b, b + count, 0.0f);
                continue;
            }
            double* d = dT[t].data();
            double* sl = sT[t].data();
            for (size_t i = 0; i < count; ++i) {
                v += sig + d[i];
                sig += sl[i];
                b[i] = float(v);
                d[i] = sl[i] = 0;
            }
            wroteT[t] = false;
            if (!((onTracks >> tr) & 1u)) v = sig = 0;  // no voice left on the track: exact zero
            vT[t] = v;
            sigT[t] = sig;
        }
    }

    // Points of the Mixer oscilloscope: kScopeRate per second (8 samples at 48 kHz, decimation of the
    // prototype), point n at position n × step, counted in internal steps (16-channel) or in samples (3-channel)
    // since the start of the time base, and taken at the first whole step (or sample) that follows it.
    double meterTickStep() const noexcept { return clock().hz() / 8.0 / kScopeRate; }
    double meterSampleStep() const noexcept { return double(hostRate()) / kScopeRate; }
    // Number of points taken before position `end` (excluded) that have not been taken yet.
    size_t meterPointsBefore(uint64_t end, double step) const noexcept {
        const double last = std::floor((double(end) - 1) / step);  // last point n such that ceil(n × step) < end
        return last < double(meterIndex) ? 0 : size_t(last - double(meterIndex)) + 1;
    }
    // 16-channel: bucket of the first point that sees a step at step t (ceil(n × step) ≥ t).
    // Without std::floor (slow without SSE4.1): for t ≥ 1, the integer part of a positive number; t = 0 falls in
    // bucket 0.
    size_t meterBucket(uint64_t t) const noexcept {
        if (t == 0) return 0;
        const uint64_t n = uint64_t(double(t - 1) * meterTickInv) + 1;
        return n <= meterIndex ? 0 : std::min(size_t(n - meterIndex), meterD[0].size() - 1);
    }
    // Starts of the buckets (bucketStart) for the current meterIndex, from meterBucket itself: an estimate, then
    // the adjustment to the smallest step that gives the bucket.
    void buildBuckets() noexcept {
        const size_t last = meterD[0].size() - 1;
        const double step = meterTickStep();
        bucketStart[0] = 0;
        for (size_t j = 1; j <= last; ++j) {
            const uint64_t prev = bucketStart[j - 1];
            uint64_t t = std::max(prev + 1, uint64_t(double(meterIndex + j - 1) * step) + 1);
            while (t > prev + 1 && meterBucket(t - 1) >= j) --t;
            while (meterBucket(t) < j) ++t;
            bucketStart[j] = t;
        }
        bucketStart[last + 1] = std::numeric_limits<uint64_t>::max();
    }
    // 16-channel: peak of the voice and step of its contribution (without panning) in the oscilloscope bucket of
    // its track; the smooth ADSR slopes are only seen at the updates of the voice. A voice that changes track
    // leaves the old one. dm: half the difference of the right and left sides, 0 but for a stereo sample (D-457);
    // the left side's peak in meterPeak, the right one's in meterPeakR.
    void meterUpdate(Voice& v, uint64_t t, double cm, double dm = 0) noexcept {
        if (v.meterTrack != v.track) {
            if (hasBus(v.meterTrack)) {
                meterD[size_t(v.meterTrack)][meterBucket(t)] -= v.cMeter;
                if (v.cMeterD != 0) meterDD[size_t(v.meterTrack)][meterBucket(t)] -= v.cMeterD;
            }
            v.cMeter = v.cMeterD = 0;
            v.meterTrack = int8_t(v.track);
        }
        if (!hasBus(v.track)) return;
        const size_t o = size_t(v.track);
        meterPeak[o] = std::max(meterPeak[o], float(cm - dm));
        meterPeakR[o] = std::max(meterPeakR[o], float(cm + dm));
        if (cm != v.cMeter) {
            meterD[o][meterBucket(t)] += cm - v.cMeter;
            v.cMeter = cm;
        }
        if (dm != v.cMeterD) {
            meterDD[o][meterBucket(t)] += dm - v.cMeterD;
            v.cMeterD = dm;
            meterSide[o] = true;
        }
    }
    // 16-channel, after computing steps [genPos, genPos + count): oscilloscope points of these steps, following
    // buckets moved to the head.
    void meterStudio(uint64_t count, uint16_t onTracks) noexcept {
        const size_t points = std::min(meterPointsBefore(genPos + count, meterTickStep()), meterD[0].size() - 1);
        for (int tr = 0; tr < kTracks; ++tr) meterTrackPoints(tr, points, ((onTracks >> tr) & 1u) != 0);
        meterIndex += points;
        meterPos = uint32_t((meterPos + points) % kScopePoints);
    }
    // Points [meterIndex, meterIndex + points) of the oscilloscope of a track driven by the buses: its buckets
    // integrated, the following ones moved to the head.
    void meterTrackPoints(int tr, size_t points, bool on) noexcept {
        const size_t t = size_t(tr);
        auto& d = meterD[t];
        auto& scope = meterScope[t];
        double level = meterLevel[t];
        for (size_t j = 0; j < points; ++j) {
            level += d[j];
            scope[(meterPos + j) % kScopePoints] = float(level);
        }
        std::copy(d.begin() + std::ptrdiff_t(points), d.end(), d.begin());
        std::fill(d.end() - std::ptrdiff_t(points), d.end(), 0.0);
        // No voice left on the track and no step to come: exact zero (no rounding residue).
        if (!on && std::all_of(d.begin(), d.end(), [](double x) { return x == 0; })) level = 0;
        meterLevel[t] = level;
        // Half differences of stereo samples (D-457): integrated the same way on a track where one played, until
        // they are back to zero; zero elsewhere.
        auto& side = meterScopeD[t];
        if (!meterSide[t]) {
            for (size_t j = 0; j < points; ++j) side[(meterPos + j) % kScopePoints] = 0;
            return;
        }
        auto& dd = meterDD[t];
        double ld = meterLevelD[t];
        for (size_t j = 0; j < points; ++j) {
            ld += dd[j];
            side[(meterPos + j) % kScopePoints] = float(ld);
        }
        std::copy(dd.begin() + std::ptrdiff_t(points), dd.end(), dd.begin());
        std::fill(dd.end() - std::ptrdiff_t(points), dd.end(), 0.0);
        if (std::all_of(dd.begin(), dd.end(), [](double x) { return x == 0; })) {
            if (!on) ld = 0;
            meterSide[t] = ld != 0;
        }
        meterLevelD[t] = ld;
    }
    // 3-channel, STE machine (D-153): voices of the DMA tracks on the buses up to the end of the n samples, rendered
    // into pcmHostL and pcmHostR (unit of the buses of the 16-channel mode), with the points of their oscilloscopes
    // (those of the chip) and their separate outputs.
    void flushPcm3(const Outputs& out, uint32_t a, uint32_t n, size_t points) noexcept {
        const uint64_t endTick = tickAtSample(samples - base + n);
        const uint64_t target = std::max({endTick, outL.ticksNeeded(n), genPos});
        const uint64_t count = target - genPos;
        uint16_t onTracks = 0;
        if (count > 0) {
            bool any = false;
            buildBuckets();
            for (int i = kDma3Voice; i < kDma3Voice + kDmaTracks; ++i) {
                if (voices[size_t(i)].on) renderStudioVoice(i, genPos, target);
                const Voice& v = voices[size_t(i)];
                any |= v.on;
                if (v.on && v.track >= 0) onTracks = uint16_t(onTracks | (1u << v.track));
            }
            const size_t w = size_t(genPos - bufBase);
            if (trackMask) integrateTracks(w, count, onTracks);
            integrateBus(w, count, any);
            genPos = target;
        }
        for (int tr = dmaFirst(); tr < dmaFirst() + kDmaTracks; ++tr)
            meterTrackPoints(tr, points, ((onTracks >> tr) & 1u) != 0);
        outL.render(bufL.data(), bufBase, pcmHostL.data(), n);
        outR.render(bufR.data(), bufBase, pcmHostR.data(), n);
        for (int tr = dmaFirst(); tr < dmaFirst() + kDmaTracks; ++tr) renderTrackOut(out, a, n, tr);
        dropConsumed();
    }

    // Meters in 3-channel mode: level of each channel of the rendered chip (table of one channel alone), carried by
    // the track of the last note of the channel; oscilloscope: sum of the channels of the track.
    void meterChip(int k, size_t points) noexcept {
        for (int ch = 0; ch < 3; ++ch) {
            const int tr = chipVoice(k, ch).track;
            if (!hasBus(tr)) continue;
            const unsigned shift = unsigned(5 * ch);
            unsigned top = 0;
            for (uint32_t i = 0; i < chipLevelCount; ++i) top = std::max(top, (chipLevels[i] >> shift) & 31u);
            meterPeak[size_t(tr)] = std::max(meterPeak[size_t(tr)], float(tab1[top]));
            meterPeakR[size_t(tr)] = meterPeak[size_t(tr)];
            auto& scope = meterScope[size_t(tr)];
            for (size_t j = 0; j < points; ++j)
                scope[(meterPos + j) % kScopePoints] += float(tab1[(chipLevels[meterAt[j]] >> shift) & 31u]);
        }
    }

    // ------------------------------------------------------------------------------------- Delay (D-299)
    // Live in 16 channels while it is on; the 3-channel modes have the echo of the notes instead.
    bool delayWanted() const noexcept { return global.delay.on && studio(); }
    // At the start of a render: the send buses join the left and right ones in silence when the delay goes live.
    void updateSendLive() noexcept {
        const bool want = delayWanted();
        if (want == sendLive) return;
        sendLive = want;
        delay.reset();
        if (!want) return;
        std::fill(dSL.begin(), dSL.end(), 0.0);
        std::fill(dSR.begin(), dSR.end(), 0.0);
        std::fill(sSL.begin(), sSL.end(), 0.0);
        std::fill(sSR.begin(), sSR.end(), 0.0);
        std::fill(bufSL.begin(), bufSL.end(), 0.0f);
        std::fill(bufSR.begin(), bufSR.end(), 0.0f);
        vSL = vSR = sigSL = sigSR = 0;
        outSL.joinSilent(outL);
        outSR.joinSilent(outR);
        for (Voice& v : voices) sendJoin(v);
    }
    // Time of the echoes in ms: a division of the tempo (that of the host in Host sync), or the time set.
    double delayMs() const noexcept {
        const DelayParams& d = global.delay;
        if (!d.sync) return d.timeMs;
        return 60000.0 / seqBpm() * kDelayDivisionBeats[std::min<size_t>(d.division, kDelayDivisions - 1)];
    }
    void setDelay() noexcept {
        const DelayParams& d = global.delay;
        delay.set(delayMs(), d.feedback, d.hiCutHz, d.level, d.pingPong);
    }
    // 3-channel render of the n samples at offset a of the block: the voice events of each chip up to the end of the
    // slice, then the chips, the copies of the separate track outputs, the DMA tracks of the STE and the previews.
    void flushChips(const Outputs& out, uint32_t a, uint32_t n) noexcept {
        float* left = out.left + a;
        float* right = out.right + a;
        const uint64_t end = chips[0].cycleAtSample(samples - base + n);
        const int nc = chipCount();
        for (int k = 0; k < nc; ++k) advanceChip(k, end);
        captureUntil(end);
        // Mono output of the prototype in 3-channel mode: voices centred (cos π/4 × √2 = 1). Levels (D-284): in 3
        // channels, the whole mix at Master (with the STE, Master on the left and Master 2 on the right); in 2x3
        // channels, each chip at its own Master (Master, Master 2), the rest (DMA sound, previews) at Master and the
        // output at full scale. A chip's or a track's own output takes its chip's Master.
        const bool perChip = nc == 2;
        const std::array<double, 2> chipLevel{global.master, perChip ? global.master2 : global.master};
        const double restLevel = perChip ? global.master : 1.0;
        const double g = gain3 * global.master * 1.2;
        std::fill(left, left + n, 0.0f);
        // Meters: oscilloscope points in these samples, reset before the sum of the channels.
        const uint64_t from = samples - base;
        const double step = meterSampleStep();
        const size_t points = std::min(meterPointsBefore(from + n, step), meterAt.size());
        for (size_t j = 0; j < points; ++j)
            meterAt[j] = uint32_t(std::ceil(double(meterIndex + j) * step) - double(from));
        chipLevelCount = n;
        for (auto* scopes : {&meterScope, &meterScopeD})
            for (auto& scope : *scopes)
                for (size_t j = 0; j < points; ++j) scope[(meterPos + j) % kScopePoints] = 0;
        for (int k = 0; k < 2; ++k) {
            float* o = out.chip[size_t(k)] ? out.chip[size_t(k)] + a : nullptr;
            if (k >= nc) {
                if (o) std::fill(o, o + n, 0.0f);
                continue;
            }
            chips[size_t(k)].render(chipBuf.data(), n, chipLevels.data());
            meterChip(k, points);
            if (perChip) {
                const double w = chipLevel[size_t(k)];
                for (uint32_t i = 0; i < n; ++i) left[i] += float(chipBuf[i] * w);
            } else {
                for (uint32_t i = 0; i < n; ++i) left[i] += chipBuf[i];
            }
            if (o) {
                std::copy(chipBuf.begin(), chipBuf.begin() + std::ptrdiff_t(n), o);
                output(o, n, gain3 * chipLevel[size_t(k)] * 1.2);
            }
        }
        for (int tr = 0; tr < kTracks; ++tr) {
            float* o = out.track[size_t(tr)] ? out.track[size_t(tr)] + a : nullptr;
            if (tr >= 4 * nc || !trackOut(tr)) {
                if (o) std::fill(o, o + n, 0.0f);
                continue;
            }
            shadow[size_t(tr)].render(o ? o : scratch.data(), n);
            if (o) output(o, n, gain3 * chipLevel[size_t(tr / 4)] * 1.2);
        }
        // STE machine (D-153): the buses of the DMA tracks are added to the chip, the output becomes stereo.
        const bool pcmOut = global.ste;
        if (pcmOut) flushPcm3(out, a, n, points);
        meterIndex += points;
        meterPos = uint32_t((meterPos + points) % kScopePoints);
        // Unit of the buses (16-channel) in that of the chips, at the level of the rest.
        const double chipUnit = std::sqrt(2.0) / gain3 * restLevel;
        mixDrumPreview(left, nullptr, n, chipUnit);
        mixDigiPreview(left, nullptr, n, chipUnit);
        if (pcmOut) {
            std::copy(left, left + n, right);
            for (uint32_t i = 0; i < n; ++i) {
                left[i] += float(pcmHostL[i] * chipUnit);
                right[i] += float(pcmHostR[i] * chipUnit);
            }
            dmaBank.mixPreview(left, right, n, double(hostRate()), chipUnit);
            // Stereo: a level per side in 3 channels (D-284); in 2x3 channels, the levels are in the mix.
            output(left, n, gain3 * (perChip ? 1.0 : masterLeft()) * 1.2);
            output(right, n, gain3 * (perChip ? 1.0 : masterRight()) * 1.2);
        } else {
            dmaBank.mixPreview(left, nullptr, n, double(hostRate()), chipUnit);
            output(left, n, perChip ? gain3 * 1.2 : g);
            std::copy(left, left + n, right);
        }
    }

    // Renders samples [a, b) of the block in the current mode and adds them to the sample count.
    void flush(const Outputs& out, uint32_t a, uint32_t b) noexcept {
        if (b <= a) return;
        if (studio()) flushStudio(out, a, b - a);
        else flushChips(out, a, b - a);
        samples += b - a;
    }

    // Silence at once, without a fade (reset, change of mode): the voices, the buses and their slopes, the meters,
    // the previews and the echoes. The chips and the time base are restarted separately (restartTimeline).
    void killAll() noexcept {
        for (int i = 0; i < kVoices; ++i) {
            Voice& v = voices[size_t(i)];
            v = Voice{};
            vch[size_t(i)].invalidate();
        }
        vL = vR = sigL = sigR = 0;
        digiPreviewSlot = -1;
        slopeWritten = false;
        std::fill(dL.begin(), dL.end(), 0.0);
        std::fill(dR.begin(), dR.end(), 0.0);
        std::fill(sL.begin(), sL.end(), 0.0);
        std::fill(sR.begin(), sR.end(), 0.0);
        vSL = vSR = sigSL = sigSR = 0;
        std::fill(dSL.begin(), dSL.end(), 0.0);
        std::fill(dSR.begin(), dSR.end(), 0.0);
        std::fill(sSL.begin(), sSL.end(), 0.0);
        std::fill(sSR.begin(), sSR.end(), 0.0);
        for (size_t t = 0; t < size_t(kTracks); ++t) {
            std::fill(dT[t].begin(), dT[t].end(), 0.0);
            std::fill(sT[t].begin(), sT[t].end(), 0.0);
            vT[t] = sigT[t] = 0;
            wroteT[t] = false;
        }
        meterPeak.fill(0);
        meterPeakR.fill(0);
        for (auto& scope : meterScope) scope.fill(0);
        for (auto& scope : meterScopeD) scope.fill(0);
        dmaBank.stopPreview();
        for (auto& d : meterD) std::fill(d.begin(), d.end(), 0.0);
        for (auto& d : meterDD) std::fill(d.begin(), d.end(), 0.0);
        meterLevel.fill(0);
        meterLevelD.fill(0);
        meterSide.fill(false);
        echoClear();
    }

    // New time base: chips, buses and outputs start again from zero (reset, change of mode or machine).
    void restartTimeline() noexcept {
        for (int k = 0; k < 2; ++k) {
            chips[size_t(k)].reset();
            cache[size_t(k)] = ChipCache{};
            regs[size_t(k)].fill(0);
            r13Written[size_t(k)] = false;
        }
        cap.chip = -1;  // a take stops with the time base
        outL.reset();
        outR.reset();
        for (int t = 0; t < kTracks; ++t)
            if (trackOut(t)) outT[size_t(t)].reset();
        for (int t = 0; t < kShadows; ++t)
            if (trackOut(t)) shadow[size_t(t)].reset();
        shadowMute.fill(0);
        for (auto& sv : shadowVol) sv.fill(0);
        // The delay starts again at the next render (updateSendLive), without its echoes.
        sendLive = false;
        delay.reset();
        genPos = bufBase = 0;
        base = samples;
        restartLfoRuns(0);  // the cycles start again (D-279)
        meterIndex = 0;
        for (auto& d : meterD) std::fill(d.begin(), d.end(), 0.0);
        for (auto& d : meterDD) std::fill(d.begin(), d.end(), 0.0);
        meterLevel.fill(0);
        meterLevelD.fill(0);
        meterSide.fill(false);
    }

    // -------------------------------------------------------------------- Sequencer (Sequencer.cpp, M5)
    uint64_t at() noexcept;  // cycle of the current sample, voices computed up to there
    void seqLogPush(char op, int voice, int a = 0, int b = 0, double x = 0, double y = 0, double z = 0, int track = -1,
                    double bpm = 0) noexcept;
    const SeqLane& seqLane(int track) const noexcept;
    int patternSteps(int pattern) const noexcept;
    int trackLen(int track) const noexcept;
    double seqNote(const SeqLane& l, int track, int voice, int s) const noexcept;
    double seqBpm() const noexcept;
    double stepsPerBeat() const noexcept;
    int halfPulsesPerStep() const noexcept;
    // Ticks (D-149): cadence in ticks, duration of row `step` in ticks, and in half MIDI pulses (D-422).
    bool ticksTiming() const noexcept;
    int lineTicks(int step) const noexcept;
    int lineHalves(int step) const noexcept;
    double swingFactor(int step) const noexcept;
    double swingAmount() const noexcept;
    double stepPpq(int64_t k) const noexcept;
    int64_t stepAt(double ppq) const noexcept;
    uint32_t seqHash(int64_t stepAbs, int a, uint32_t salt) const noexcept;
    bool cndPass(int cv, int64_t cycle, int track) const noexcept;
    double seqLevel(int track, double vol) const noexcept;
    void resetSeq() noexcept;
    void seqStart(uint64_t c) noexcept;
    void seqHalt(uint64_t c) noexcept;
    void clearLanes(bool full) noexcept;
    void laneOff(int i, uint64_t c) noexcept;
    void selectPattern(int pattern) noexcept;
    void applyQueue() noexcept;
    void songArm() noexcept;
    void songQueue(int pos) noexcept;
    bool songOnPattern(uint64_t c) noexcept;
    // Extended Song (D-158).
    bool songActive() const noexcept;
    int songEntryPattern(int entry) const noexcept;
    int songEntrySteps(int entry) const noexcept;
    int songLoopTarget(int entry) const noexcept;
    int songNext(int from, bool& wrapped) noexcept;
    void songQueueNext(int from) noexcept;
    void songLoopReset() noexcept;
    int restFallback() const noexcept;
    void songNoteOff(uint64_t c) noexcept;
    void songOnWrap() noexcept;
    void songJump(int entry) noexcept;
    void seqTick(uint64_t c) noexcept;
    void seqSample(uint32_t dt) noexcept;
    void hostClock(uint32_t dt) noexcept;
    void hostLocate(int64_t k, double ppq, double pps) noexcept;
    bool locateTo(int64_t last) noexcept;
    void metroTick() noexcept;
    void renderClicks(const Outputs& out, uint32_t n) noexcept;
    void laneStep(int i, double stepSamp, uint64_t c) noexcept;
    void heldStep(int i, const SeqLane& l, size_t s, double level, double stepSamp, bool arpOn, uint64_t c) noexcept;
    void relevel(int vi, int track, int ins, double level, uint64_t c) noexcept;
    void setLevel(Voice& v, int track, int ins, double level) noexcept;
    // Commands of the row (CMD row, D-150): Sxx before the duration of the row, Rxx after its notes.
    uint16_t lineCmd(int i, SeqCmd k) const noexcept;
    void laneSwitches(int i) noexcept;
    void seqSpeedCmd() noexcept;
    void seqTempoCmd() noexcept;
    void laneCmd(int i, double stepSamp, uint64_t c) noexcept;
    void laneLineCmds(int i, double stepSamp, uint64_t c) noexcept;
    void armSlideCurve(int i, SeqCmd kind, double perFrame, double stepSamp, uint64_t c) noexcept;
    void cutLine(int i, uint64_t c) noexcept;
    void wakeVbl(Voice& v, uint64_t c) const noexcept;
    template <class F> void forLaneVoices(int i, F&& f) noexcept;
    void retrigEnv(int i, uint64_t c) noexcept;
    void laneSample(int i, uint32_t dt) noexcept;
    void laneCount(int i, uint32_t dt) noexcept;
    void lanesCatchUp() noexcept;
    int64_t lanesDue() const noexcept;
    void laneTrigger(int i, double note, double level, int gen, double drumTr, int ins, uint64_t c) noexcept;
    void killLane(int vi, bool hard, uint64_t c) noexcept;
    bool laneTail(int i, uint64_t c) noexcept;
    void applyMix(int vi, int m, uint64_t c) noexcept;
    void applyMixDef(int vi, int track, int ins, int m, uint64_t c) noexcept;
    void liveFx(int vi, int track, int ins, int fxv, uint64_t c) noexcept;
    bool polyAny(const SeqLane& l, int st) const noexcept;
    void polyAge(int i, uint64_t c) noexcept;
    void polyTrigger(int i, const SeqLane& l, int st, double level, int mix, int64_t gateHold, double drumTr, int ins,
                     uint64_t c) noexcept;
    void hardPoly(int i, uint64_t c) noexcept;
    int slideTarget(int i, const SeqLane& l, int st, int& k) const noexcept;
    void armSlide(int vi, int i, const SeqLane& l, int st, double stepSamp, uint64_t c) noexcept;
    void armArpSlide(int i, const SeqLane& l, int st, double stepSamp) noexcept;
    void reshapeSlide(int vi, int cv) noexcept;
    void holdGlide(int vi, bool snap, uint64_t c) noexcept;
    void laneArpTick(int i, uint64_t c) noexcept;
    void setStepPan(int i, double pan, uint64_t c) noexcept;
    void arpPulse(uint64_t c, int64_t pulse = kNoPulse, bool lanesToo = true, int weight = 1) noexcept;
    // Half pulses that the clock delivers: all of them for an odd division (1/64), whole clocks otherwise (D-289).
    bool arpHalves() const noexcept { return global.arpDivision % 2 != 0; }
    // Likewise for the sequencer on the MIDI clock: all of them at the 1/64 resolution, a step lasting a pulse and a
    // half (D-422); clockWeight: half pulses a clock stands for.
    bool seqHalves() const noexcept;
    int clockWeight() const noexcept { return seqHalves() ? 1 : 2; }
    void halfPulse(uint64_t c) noexcept;
    void seqMidiClock(uint64_t c) noexcept;
    void seqMidiPulse(uint64_t c, int weight) noexcept;
    void seqMidiStart() noexcept;
    void seqMidiStop(uint64_t c) noexcept;
    void seqSongPosition(int sixteenths) noexcept;

    // Event to apply at the next render(); nullptr (and counter) beyond kMaxEvents.
    Event* push(uint32_t offset, EventType type, int a, int b, double value) noexcept {
        if (eventCount >= kMaxEvents) {
            ++dropped;
            return nullptr;
        }
        Event& e = events[eventCount++];
        e = Event{};
        e.offset = offset;
        e.order = eventOrder++;
        e.type = type;
        e.a = int16_t(a);
        e.b = int16_t(b);
        e.value = value;
        return &e;
    }
};

}  // namespace stym::ymengine
