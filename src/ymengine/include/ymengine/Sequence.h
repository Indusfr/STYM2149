// STYM2149 — ymengine: sequencer patterns as the engine plays them
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A pattern track (SeqLane) has the per-step rows of the prototype (LANEKEYS); ymseq::Lane derives from it for
// editing. The engine receives an immutable snapshot (SeqData): the tracks in it are shared with the editing
// model (copy on write), so publishing a snapshot only copies pointers. The audio thread only reads the
// snapshot; its owner keeps it alive as long as the engine has not replaced it.
//
// Notes are stored as in the prototype (relative note and octave code): the engine computes the absolute note
// at playback with the octave of the track (chanPayload), and the note of the drums from the track octave and the row.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "ymengine/Params.h"

namespace stym::ymengine {

inline constexpr int kSeqSteps = 256;       // NSTEP
inline constexpr int kSeqPatterns = 32;     // 4 banks × 8
inline constexpr int kSeqChordVoices = 5;   // chord voices 2 to 6
inline constexpr int kSeqResolutions = 9;   // entries of kStepsPerBeat (GlobalParams::resolution)

// Resolutions of the prototype (RESOS): steps per quarter note. 1/4, 1/8, 1/16, 1/32, then the triplets 1/4T,
// 1/8T, 1/16T; 1/64 and 1/32T added after them so that the saved indices keep their meaning.
inline constexpr std::array<double, kSeqResolutions> kStepsPerBeat = {1, 2, 4, 8, 1.5, 3, 6, 16, 12};
// Half MIDI clock pulses (48 per quarter note) per step: twice the whole pulses of the prototype's seven
// resolutions, three for 1/64, a pulse and a half, four for 1/32T, two pulses.
inline constexpr std::array<int, kSeqResolutions> kHalfPulsesPerStep = {48, 24, 12, 6, 32, 16, 8, 3, 4};

// Effect commands of the step (CMD row), one of each kind at most: letter × 256 + parameter xx.
//   Xyz arpeggio of the step (retired); Sxx ticks per row (Ticks cadence);
//   Rxx retrigger of the envelope (R13) at tick xx of the row; Zxx demo sync, silent, written by the SNDH export;
//   Txx tempo in BPM from this row on, $20 to $FF (BPM cadence, internal clock); Vxx vibrato of the track from
//   this row on, V00 off, V01 to VFF on, until the next Vxx or Play.
// The effects of the tenth DAW test (item 130), each a register write per replay tick that a real chip plays:
//   Axy volume slide, Pxy pitch slide (retired), Bxy envelope period slide (buzzer), Nxy noise period slide:
//   x up, y down, the speed being x - y per tick, from this row on until the next command of the same kind (a speed
//   of 0, A00 for instance, stops it); each new note starts from its own value. Dxx delays the notes of the row to
//   tick xx, Cxx cuts them at tick xx (only that row; a tick beyond the row is ignored). Exx sets the envelope shape
//   R13 (8 to 15) of the track from this row on; Oxx starts the DMA samples of the row xx × 256 frames in; Fxx detunes
//   the track by xx - 100 cents (0 to 200) from this row on. Play starts without any of them.
// The switches of the twelfth DAW test (items 142 to 144), as Vxx: Lxx the LFO of the track (L00 off, L01 on), Mxx its
//   modulation table, Jxx its arpeggiator, from this row on until the next one of the kind or Play. Play starts with
//   the LFO and the table of the tracks, and without the arpeggiator (the ARP row of the prototype armed it step by
//   step).
// 24/10/2026: Gxx glides the volume of the step from its VOL to the next step's, frame after frame (G01; set from the
//   VOL row); Kxx and Qxx give the Bxy and Nxy of their step a slide curve (xx: a SlideCurve, from the step to
//   the next command of the kind; 16-channel mode).
enum class SeqCmd : uint8_t {
    None, Arp, Speed, Retrig, Sync, Tempo, Vibrato,
    VolSlide, PitchSlide, Delay, Cut, EnvSlide, NoiseSlide, Shape, Offset, Fine,
    Lfo, Mod, Arpeggiator, VolGlide, EnvCurve, NoiseCurve
};
inline constexpr int kSeqCmds = 22;
inline constexpr std::array<char, kSeqCmds> kSeqCmdLetters = {'-', 'X', 'S', 'R', 'Z', 'T', 'V', 'A', 'P', 'D', 'C',
                                                              'B', 'N', 'E', 'O', 'F', 'L', 'M', 'J', 'G', 'K', 'Q'};
inline constexpr int kTempoCmdMin = 0x20;  // Txx below: no effect (speeds in the Fxx of trackers)
inline constexpr uint16_t seqCmd(SeqCmd k, int xx) noexcept { return uint16_t(int(k) << 8 | (xx & 0xff)); }
inline constexpr SeqCmd seqCmdKind(uint16_t c) noexcept { return (c >> 8) < kSeqCmds ? SeqCmd(c >> 8) : SeqCmd::None; }
inline constexpr int seqCmdParam(uint16_t c) noexcept { return c & 0xff; }
inline constexpr int kSeqCmdMax = (kSeqCmds - 1) << 8 | 0xff;
// Slides: speed x - y of a command xy, and the unit of one step of speed per replay tick.
inline constexpr int slideSpeed(uint16_t c) noexcept { return (seqCmdParam(c) >> 4) - (seqCmdParam(c) & 15); }
inline constexpr double kVolSlideUnit = 0.25;          // 4-bit volume steps (A01: 15 steps in 60 ticks)
inline constexpr double kPitchSlideUnit = 1.0 / 16;    // semitones, as the linear slides of XM
inline constexpr double kNoiseSlideUnit = 0.25;        // noise period R6 (up: shorter period, higher noise)
inline constexpr int kShapeCmdMin = 8, kShapeCmdMax = 15;  // Exx: the shapes of the Shape R13 menu
inline constexpr int kOffsetCmdFrames = 256;           // Oxx: frames of the DMA sample per unit, as XM's 9xx
inline constexpr int kFineCmdCenter = 100, kFineCmdMax = 200;  // Fxx: xx - 100 cents
// Kinds of command a step can hold together, one row each (Arp to Arpeggiator; tenth DAW test, items 122 and 130;
// twelfth, items 142 to 144).
inline constexpr int kSeqCmdKinds = kSeqCmds - 1;
// Eleventh DAW test, items 131 and 135: Xyz and Pxy are retired (the ARP and SLD rows of the Step
// Sequencer play arpeggios and glides); their rows stay empty and a value read for them is dropped.
inline constexpr bool seqCmdRetired(SeqCmd k) noexcept { return k == SeqCmd::Arp || k == SeqCmd::PitchSlide; }
// Items 132, 133 and 138 to 140: Vxx, Txx, Exx, Dxx and Cxx have rows of their own in
// the Step Sequencer (VIB, BPM, ENV, DLY, CUT); twelfth DAW test, items 142 to 146, 152 and 153: Lxx (LFO), Mxx
// (MOD), Jxx (ARP), Rxx (ENV with Exx), Oxx (SMP) and Fxx (TUN) too. The other kinds share the CMD row and its
// Commands box. Txx has left the tracks for the BPM row of the pattern (item 150, SeqPattern::bpm).
inline constexpr bool seqCmdOwnRow(SeqCmd k) noexcept {
    switch (k) {
    case SeqCmd::Vibrato:
    case SeqCmd::Tempo:
    case SeqCmd::Shape:
    case SeqCmd::Retrig:
    case SeqCmd::Delay:
    case SeqCmd::Cut:
    case SeqCmd::Offset:
    case SeqCmd::Fine:
    case SeqCmd::Lfo:
    case SeqCmd::Mod:
    case SeqCmd::Arpeggiator:
    case SeqCmd::VolGlide: return true;  // the VOL row
    default: return false;
    }
}
// Curves of the Buzzer and Noise slides: part of the Commands box, with the slide of their step, not a row of
// the box of their own.
inline constexpr bool seqCmdCurve(SeqCmd k) noexcept { return k == SeqCmd::EnvCurve || k == SeqCmd::NoiseCurve; }
inline constexpr SeqCmd seqSlideCurve(SeqCmd slide) noexcept {
    return slide == SeqCmd::EnvSlide     ? SeqCmd::EnvCurve
           : slide == SeqCmd::NoiseSlide ? SeqCmd::NoiseCurve
                                         : SeqCmd::None;
}
inline constexpr bool seqCmdInBox(SeqCmd k) noexcept {
    return k != SeqCmd::None && !seqCmdRetired(k) && !seqCmdOwnRow(k);
}
// SRC row (mix): 0 the track's, 1 Tone, 2 Noise, 3 Tone+Noise; from kSeqMixEnv the same plus the envelope as volume
// (4 Env to 7 Tone+Noise+Env).
inline constexpr int kSeqMixEnv = 4, kSeqMixMax = 7;
// GEN row (fx): 0 the generator of the note (track or instrument), then Chip Voice, SID Voice, Sync Buzzer, Chipdrum
// (values of Generator).
inline constexpr std::array<int, 5> kSeqFxGen = {-1, 0, 1, 3, 4};

// One track of a pattern: its rows, one value per step. ymseq/Pattern.h lists each row with its name on screen and
// its range (tie HLD, slide SLD, afl ARP, mix SRC, fx GEN, gat GAT, pnr PAN, rtg RTG, cnd CND, slc CRV...). slt (SLT)
// is kept as the prototype writes it, but playback ignores it.
struct SeqLane {
    std::array<int16_t, kSeqSteps> pat;      // relative note (0-12) or drum row (0-7); -1: nothing
    std::array<int8_t, kSeqSteps> oct{};     // octave code (0 = octave of the track, -1 = octave 0); OCT of the drums
    std::array<uint8_t, kSeqSteps> tie{}, slide{}, afl{}, vol, mix{}, slt{}, cut{}, fx{}, gat{}, pnr{}, rtg{}, cnd{},
        slc{};
    std::array<int8_t, kSeqSteps> ton{};     // TON of the drums, semitones
    std::array<uint8_t, kSeqSteps> ins{};    // INS: 0 = sound of the track, 1-32 = instrument
    // CMD: one row per kind of command, cmd[k - 1][s] = seqCmd(k, xx) when step s has the
    // command of kind k, 0 otherwise.
    std::array<std::array<uint16_t, kSeqSteps>, kSeqCmdKinds> cmd{};
    std::array<std::array<int16_t, kSeqSteps>, kSeqChordVoices> vpat;  // voices 2 to 6
    std::array<std::array<int8_t, kSeqSteps>, kSeqChordVoices> voct{};
    std::array<std::array<uint16_t, kSeqSteps>, kSeqChordVoices> vdur{};  // duration in steps (0: one step)

    SeqLane() noexcept {
        pat.fill(-1);
        vol.fill(15);
        for (auto& v : vpat) v.fill(-1);
    }
    bool operator==(const SeqLane&) const = default;

    // Command of kind k at step s (0: none).
    uint16_t cmdAt(SeqCmd k, int s) const noexcept {
        return k == SeqCmd::None ? 0 : cmd[size_t(int(k) - 1)][size_t(s)];
    }
    // Writes command c in the row of its kind (a command of kind None or of a retired kind writes nothing).
    void setCmd(int s, uint16_t c) noexcept {
        if (const SeqCmd k = seqCmdKind(c); k != SeqCmd::None && !seqCmdRetired(k))
            cmd[size_t(int(k) - 1)][size_t(s)] = c;
    }
    void clearCmd(SeqCmd k, int s) noexcept {
        if (k != SeqCmd::None) cmd[size_t(int(k) - 1)][size_t(s)] = 0;
    }
    // Step s holds at least one command (the retired kinds are never counted).
    bool hasCmd(int s) const noexcept {
        for (int k = 1; k <= kSeqCmdKinds; ++k)
            if (!seqCmdRetired(SeqCmd(k)) && cmd[size_t(k - 1)][size_t(s)]) return true;
        return false;
    }
};

// Pattern: its length and its tracks, shared with the editing model and never modified once published.
struct SeqPattern {
    int steps = 16;                                              // 1 to 256
    std::array<std::shared_ptr<const SeqLane>, kTracks> lanes;   // nullptr: empty track
    // BPM row (twelfth DAW test, item 150): tempo in BPM from this step of the pattern on, as a Txx that every track
    // shares; 0: none, kTempoCmdMin to 255 otherwise.
    std::array<uint8_t, kSeqSteps> bpm{};
};

// Kind of a Song entry: pattern; silence (Rest) or release of the sequencer notes followed by a silence
// (NoteOff), of arg steps; jump (Loop) that replays the arg entries before it, repeats times, without playing
// itself. The entries it replays stop at the previous jump: jumps do not nest.
enum class SeqSongKind : uint8_t { Pattern, Rest, NoteOff, Loop };

// Song entry: pattern, repeats, own tempo (0: tempo of the song).
struct SeqSongEntry {
    uint8_t pattern = 0;
    uint16_t repeats = 1;  // times the entry plays; Loop: times it jumps back
    uint16_t bpm = 0;
    SeqSongKind kind = SeqSongKind::Pattern;
    uint16_t arg = 0;      // Rest and NoteOff: length in steps; Loop: entries replayed
};

// Pattern played by a Rest or NoteOff entry: kSeqRestPattern + length in steps, no track.
inline constexpr int kSeqRestPattern = 0x1000;

// Snapshot of the sequencer given to the engine (Engine::setSequence): the patterns and the Song.
struct SeqData {
    std::array<SeqPattern, kSeqPatterns> patterns;
    std::vector<SeqSongEntry> song;
    uint16_t songRestart = 0;  // entry where the Song resumes at its end, looping
};

// Octave played for an octave code (octOf of the prototype).
inline int seqOctave(int code, int trackOctave) noexcept { return code == -1 ? 0 : (code ? code : trackOctave); }

// Empty track (tracks absent from a pattern).
const SeqLane& emptySeqLane() noexcept;

}  // namespace stym::ymengine
