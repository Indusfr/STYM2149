// STYM2149 — plugincore: import of YM and SNDH tunes as editable patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// One converter for both formats: the register frames of the tune (those of the YM file, or those that the emulated
// Atari writes while it plays the SNDH) become the notes of the three voices on tracks 1 to 3, in Authentic 3ch:
// - frames (RegisterTune): R0-R13 at each replay tick, periods brought to the 2 MHz clock of the mode; an SNDH keeps
//   the writes of its play routine (the writes of its timer interrupts, SID voices or digidrums, are counted as
//   effects), and the end of its capture that repeats an earlier part becomes the loop;
// - notes: a voice starts a note where it becomes audible, where the envelope restarts under it (R13 written; not
//   after a click of 3 frames at most that leads into the envelope), where its volume jumps back up after a decay,
//   where noise comes in at the loudest volume of the note after 3 frames without it, or on a step where it moves to a
//   pitch it has not just played (an arpeggio or a vibrato does not); a note ends where the voice falls silent or
//   where the next one starts; a note longer than a table (64 frames) that does not hold still goes on as a new note
//   at its last step within the table (never under the envelope, which a new note would restart);
// - grid (RegisterGrid): frames per step (the Speed of the Ticks timing) and the frame of the first step, guessed from
//   the attacks alone as the longest step that keeps two notes of a voice out of one step and that the notes follow
//   (see guessRegisterGrid), the user correcting it before the import; a note that starts within a step waits for
//   its frame with Dxx, a second note in the same step goes on in the table of the first one (its envelope restart
//   becoming Rxx);
// - each note: NOTE and OCT (its first pitch, at the Tuning A4 that the tune is closest to, its offset in Fxx when
//   it is 5 cents or more), INS, SRC of its first frame, ENV (Exx) when it plays the envelope, TIE on its steps and
//   GAT where it ends within a step (its release half a frame before the frame where it falls silent);
// - instruments: the frames of a note as a table of 64 steps at one frame per step, Hold: the pitch in semitones from
//   the note (±12, the note moved to the base that keeps most frames within that) with the rest in PER, the volume,
//   the mixer in MIX, the noise period in NP; Chip Voice with the envelope period of the note when it plays the
//   envelope, Sync Buzzer when the envelope is a buzzer whose frequency is a multiple of the note's (tuned envelope
//   N/1). Notes whose tables are the same, or the start of one another,
//   share an instrument; beyond 32, the pairs that cost the least merge first (the frames of the merged notes,
//   weighted by how often they play: volume steps, mixer changes, noise period steps, and pitch up to 30 cents).
// - patterns of 16 to 256 steps, the same ones played again by the Song, which restarts at the loop of the tune.
// What the import does not render is reported in lines for the end-of-import alert.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/State.h"

namespace stym::ymio {
struct YmFile;
struct SndhFile;
}  // namespace stym::ymio

namespace stym::plugin {

// Frames of a tune, at most (10 minutes at 50 Hz) and by default for an SNDH without a length (3 minutes).
inline constexpr uint32_t kRegisterImportMaxSeconds = 600;
inline constexpr uint32_t kSndhImportDefaultSeconds = 180;

// Timer effect of a voice at a frame, where the tune tells it (a tracker's): SID (the volume stepped through a
// waveform), sync buzzer (the envelope restarted, its shape stepped) or digidrum (a sample played through the volume).
// `hz` is the rate of its interrupts: a SID of n steps plays its waveform hz / n times per second, a sync buzzer
// restarts the envelope hz times per second, a digidrum plays hz samples per second.
struct TimerEffect {
    ymengine::Generator gen = ymengine::Generator::Chip;  // Sid, SyncBuzzer, Fm or Digidrum; Chip: none
    uint8_t steps = 0;                // SID 1-16, sync buzzer 1-8, FM 1-16
    // SID: the volume (0-15) of each step at volume 15; sync buzzer: its R13 shape; FM: the index of its multiple of
    // the note's frequency in ymengine::kFmMultiples, or with semis its semitones above the note.
    std::array<uint8_t, 16> wave{};
    bool semis = false;
    double hz = 0;
    uint8_t sample = 0;               // digidrum: 0-7
    bool start = false;               // digidrum: the sample starts at this frame
    bool operator==(const TimerEffect&) const = default;
};

// The register frames of a tune: one per replay tick, R0-R13 as the chip holds them after the writes of the tick.
struct RegisterTune {
    std::string title, author;
    uint32_t rate = 50;            // frames per second
    double clockHz = 2'000'000;    // chip clock of the file (the periods are brought to 2 MHz)
    std::vector<std::array<uint8_t, 14>> frames;
    std::vector<uint8_t> restart;  // 1 where R13 was written in the frame (the envelope restarts)
    int loopFrame = -1;            // frame played after the last one; -1: the tune does not loop
    // What the frames leave out.
    uint32_t effectFrames = 0;     // frames with a timer effect (YM5/YM6, or SNDH timer interrupts writing the chip)
    uint32_t digidrums = 0;        // digidrum hits of a YM2, YM5 or YM6
    bool dac = false;              // SNDH: the STE DAC played
    uint32_t cutFrames = 0;        // frames beyond kRegisterImportMaxSeconds
    // A tracker's tune: the timer effect of each voice at each frame (empty: not known; effectFrames then
    // counts only the effects that are not one of them), and the samples of its digidrums (8-bit, signed).
    std::vector<std::array<TimerEffect, 3>> effects;
    std::array<std::vector<int8_t>, 8> samples{};
};

// The frames of a YM file.
RegisterTune registerTune(const ymio::YmFile& f);
// The frames of an SNDH subsong (1-based), `frames` replay ticks played by the emulated Atari (the end that repeats an
// earlier part becomes the loop); nullopt if `cancel` becomes true or if the routine crashes before its first tick.
std::optional<RegisterTune> registerTune(const ymio::SndhFile& f, int subsong, uint32_t frames,
                                         const std::atomic<bool>* cancel = nullptr);
// Ticks to capture for an SNDH subsong: its length tag, otherwise kSndhImportDefaultSeconds; at most
// kRegisterImportMaxSeconds.
uint32_t sndhImportFrames(const ymio::SndhFile& f, int subsong);

// Grid of the patterns: steps of `framesPerStep` frames (the Speed of the Ticks timing, 1 to kGridMaxFrames), the first
// one at frame `offset` (negative: the tune starts within the first step), `stepsPerPattern` steps per pattern.
inline constexpr int kGridMaxFrames = 24;
struct RegisterGrid {
    int framesPerStep = 6;
    int offset = 0;
    int stepsPerPattern = 64;
    int aligned = 100;  // % of the note starts that fall on a step (guess)
    int notes = 0;      // notes found
};
// The grid that the note starts suggest (see the file comment), with the pattern length closest to 384 frames; with
// `framesPerStep`, the frame of the first step that suits steps of that length.
RegisterGrid guessRegisterGrid(const RegisterTune& t, int framesPerStep = 0);

// What an import did: the settings that the caller applies (Authentic 3ch, tracks 1 to 3 on with Chip Voice at Level
// 100 %, the others off; VBL rate, Tuning A4) and the lines of the alert.
struct RegisterImport {
    std::string title;
    int patterns = 0;     // patterns 0 to n-1 replaced
    int entries = 0;      // Song entries
    int instruments = 0;  // instruments 0 to k-1 replaced
    int notes = 0;
    int vblRate = 0;      // index into kVblRates
    int speed = 6;        // Ticks: frames per step
    float tempo = 0;      // BPM timing (a YM-Tracker song): the free tempo, 60-240; 0: Ticks timing
    float a4Hz = 440;
    // The tune's digidrums on track 4: the caller sets it on with the Digidrum generator at Level 100; its
    // channel (NonParamState::global.digidrumChannel[0]) and the slots that hold the tune's samples are set.
    bool digidrums = false;
    int samples = 0;      // Digidrum slots 0 to 7 replaced where the tune has a sample
    std::vector<std::string> remarks;  // what the import does not render
};
struct RegisterImportResult {
    std::optional<RegisterImport> import;
    std::string error;
};

// Replaces patterns 0 to n-1, the Song, instruments 0 to k-1, the timing (Ticks, speed, no groove) and the length of
// tracks 1 to 3 in `state`; nothing changes if the tune is refused (no audible frame).
RegisterImportResult importRegisters(const RegisterTune& t, const RegisterGrid& grid, NonParamState& state);

// The rows of a tune whose song is known (a tracker's), in place of the grid and the notes that the frames
// suggest: each step a row played (its frames: the speed of the tune there; Sxx where it changes, and at the start of
// each pattern when it does), the parts that make patterns (the patterns of the tune; consecutive ones are joined
// into longer patterns beyond 32, from the loop on), the notes where the rows start them (a note lasts until the next
// row that starts or cuts one on its voice, or until the voice falls silent before).
struct RegisterLayout {
    std::vector<int> stepFrames;  // frames of each step, in the order played (1 to 255)
    std::vector<int> partSteps;   // steps of each part, in order; their sum is the number of steps
    int loopStep = -1;            // step played after the last one; -1: the tune does not loop
    struct Row {
        int voice = 0, frame = 0;
        int note = -1;  // MIDI note that starts; -1: the voice is cut
    };
    std::vector<Row> rows;
    double a4Hz = 440;  // tuning of the notes
};
RegisterImportResult importRegisters(const RegisterTune& t, const RegisterLayout& layout, NonParamState& state);
// Lines of the "YM imported" / "SNDH imported" alert.
std::vector<std::string> registerImportMessage(const RegisterImport& r, const std::string& fileName, bool sndh);
// The settings an import gives the project (File > Import, and the copy of the project that File > Play plays):
// Authentic 3ch with MPE off, tracks 1 to 3 on Chip Voice at full level, track 4 on Digidrum at full level
// for the tune's digidrums, the others off, VBL rate and Tuning A4 of the tune, the free tempo of a song in BPM timing.
void registerImportValues(const RegisterImport& r, ParamValues& values);

// VOL in Authentic 3ch (Impl::attenuated): the 4-bit volume that a sound of volume `vol` plays at VOL × `level` (the
// level nearest in decibels, never silent above 0); and the VOL (0-15) that plays the 4-bit volume `level4` on a sound
// of volume 15 (the nearest in decibels; 1 for the levels quieter than VOL 1 plays).
int attenuatedVolume(int vol, double level) noexcept;
int volForLevel(int level4) noexcept;

}  // namespace stym::plugin
