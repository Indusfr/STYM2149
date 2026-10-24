// STYM2149 — plugincore: MIDI import and export of the File menu (M7, A3, D-170)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Import (midiImport of the prototype): an SMF 0 or 1 file (ymio::readSmf) becomes patterns and a Song.
// - One step is a sixteenth note: division / 4 ticks, positions rounded (Math.round).
// - Patterns of the length of the current pattern, as many as needed, 32 at most (the rest is lost and reported).
// - Channels in the order in which their notes close, the melodic ones first, channel 10 (GM percussion) next, on
//   the tracks of the mode (midiImportTracks). A channel 10 track becomes Chipdrum, its notes the rows of the kit
//   (GM_SLOT of the prototype); a melodic track becomes Chip Voice, its held notes fill the following steps with
//   ties (TIE). VOL = velocity / 127 × 15, clamped to 1-15. A note with the same start as another one of its track
//   replaces it (no chord voices, like the prototype).
// - Patterns 1 to n are replaced, the Song becomes one entry per pattern; the other patterns stay. The filled
//   tracks are switched on, the others off.
// - Deviations (D-170): the octave is written as octCode (the prototype wrote 0 for octave 0, which then played at
//   the octave of the track); a held note also carries TIE on its first step, like a note stretched in the grid
//   (the prototype wrote it only on the following steps, and the note did not hold); in dual chip, the channels go
//   to the channels of both chips (tracks 1-3 and 5-7; the prototype took tracks 1 to 6, including the Digidrum
//   track of the first chip); a tempo outside the range of the free tempo (60-240) is carried by each Song entry
//   (the prototype played a tempo that its slider did not show).
//
// Export (exportMidi, exportSongMidi of the prototype): SMF 1, 96 ticks per quarter note, like the prototype; the
// content is what the sequencer plays (D-163), rendered offline (OfflineRender) and read from its log:
// - each attack of a sequencer voice is a note (root, chord voices, RTG repeated hits, notes of the track
//   arpeggios), which ends at the release of its voice (gate, CUT, Note-off, stop), at the next attack of the same
//   voice, or when the voice changes note (arrival of a slide, held arpeggio);
// - percussion (Digidrum and Chipdrum tracks) on channel 10, GM_DRUM notes of the prototype, half a step long;
// - velocity = VOL / 15 × 127 (× 0.92 for the repeated hits, like the sound);
// - track 0: tempo of each step where it changes (Song blocks; in Ticks mode, 60 × VBL / (speed × steps per
//   quarter note)) and time signature of the project; the steps follow the time played (swing, groove);
// - one MIDI track per track played, in track order, channels like the prototype (1 to 9 then 11 to 16), named
//   after the track name, otherwise "STYM2149 chN" (the prototype wrote "PRISM chN", D-060).
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "plugincore/Offline.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymengine/Engine.h"
#include "ymio/Smf.h"
#include "ymseq/Project.h"

namespace stym::plugin {

// Maximum size of a MIDI file read.
inline constexpr size_t kMidiMaxBytes = size_t(8) << 20;
// GM mapping of the prototype (GM_DRUM): kit row → GM note (Kick, Snare, Clap, Hat, Open hat, Closed hat, Tom 1,
// Tom 2), and GM note → row (GM_SLOT).
inline constexpr std::array<int, 8> kGmDrum = {36, 38, 39, 42, 46, 44, 45, 41};
int gmSlot(int note) noexcept;

// Tracks filled by the import, in channel order: the 16 tracks in 16 channels; the channels of the chip in 3
// channels (tracks 1 to 3); those of both chips in dual chip (1-3, then 5-7).
std::vector<int> midiImportTracks(ymengine::VoiceMode mode);

// What an import did, for the caller (tempo, track switches and generators, alert).
struct MidiImport {
    int tracks = 0;    // tracks filled: the first ones of midiImportTracks
    int patterns = 0;  // patterns 0 to n-1 replaced
    int steps = 16;    // their length (that of the current pattern)
    int bpm = 120;     // tempo of the file (20-300)
    bool truncated = false;  // the file ran past the last pattern: the rest is lost
    std::array<bool, kTracks> on{};    // tracks switched on (the others are switched off)
    std::array<bool, kTracks> drum{};  // channel 10 tracks: Chipdrum; the other tracks switched on: Chip Voice
};
// Replaces patterns 0 to n-1 and the Song of `project`; nothing changes if the file is refused (nullopt:
// "Unsupported MIDI file" of the prototype, also for a file without notes).
std::optional<MidiImport> importMidi(std::span<const uint8_t> file, ymengine::VoiceMode mode, ymseq::Project& project);
// Free tempo set by the import: the file's, brought back to 60-240.
float midiImportTempo(const MidiImport& r) noexcept;
// Lines of the "MIDI imported" alert (gemInfo of the prototype).
std::vector<std::string> midiImportMessage(const MidiImport& r, const std::string& fileName);

// Ticks per quarter note of the exported files.
inline constexpr uint16_t kMidiPpq = 96;

// Notes played by the sequencer of an offline render, read from its log, then an SMF 1 file.
class MidiExportCapture {
public:
    // Settings played (OfflineRender::values() and extras()) and rate of the render.
    MidiExportCapture(const ParamValues& values, const NonParamState& extras, uint32_t sampleRate);
    // Sequencer log of one rendered block.
    void feed(std::span<const ymengine::Engine::SeqLogRecord> log);
    // End of the render (sample): notes still open are closed there.
    void finish(uint64_t endSample);
    // Track 0 (time signature, tempo map), then one track per track that has notes.
    std::vector<uint8_t> file() const;
    int tracks() const noexcept;  // note tracks (without the tempo track)

private:
    // The note sounding on an engine voice.
    struct Open {
        bool on = false;
        int track = 0, key = 0, velocity = 0;
        uint32_t tick = 0;
        int64_t drumOff = -1;  // percussion: end tick (half step)
    };
    double tickAt(uint64_t sample) const noexcept;
    void start(int voice, int track, int key, int velocity, uint64_t sample);
    void close(int voice, uint64_t sample);
    void closeAll(uint64_t sample);
    int keyFor(int track, double note) const noexcept;

    uint32_t rate_;
    bool studio_ = true;
    std::array<bool, kTracks> drum_{};    // percussion track (Digidrum, Chipdrum): channel 10
    std::array<int, kTracks> octave_{};   // sequencer octave of the track, to find the kit row of a note
    std::array<double, kTracks> gain_{};  // track level included in the logged attack levels
    std::array<std::string, kTracks> name_{};
    int sigNum_ = 4, sigDen_ = 4;
    // Tempo map: current segment (start in samples and in ticks, tempo written).
    uint32_t us_ = 0;  // microseconds per quarter note of the segment (0: none written yet)
    double bpm_ = 120, segTick_ = 0;
    uint64_t segSample_ = 0;
    double stepSamples_ = 0;  // length of the current step, for the half-step percussion notes
    std::vector<ymio::SmfEvent> tempo_;
    std::array<std::vector<ymio::SmfEvent>, kTracks> notes_{};  // channel 0 here, set when the file is written
    std::array<Open, ymengine::Engine::kVoices> open_{};
    std::array<int, ymengine::Engine::kVoices> slideTo_{};  // armed slide target note of each voice (-1: none)
};

// Result of exportMidi: the file, its note tracks and the length rendered.
struct MidiExport {
    std::vector<uint8_t> bytes;
    int tracks = 0;
    double seconds = 0;
    bool truncated = false;  // longer than kOfflineMaxSeconds: cut
};
// Offline render of the current pattern or of the Song, capture and file. nullopt if `cancel` becomes true.
std::optional<MidiExport> exportMidi(OfflineRender::Source source, const ParamValues& values,
                                     const NonParamState& extras, const std::atomic<bool>* cancel = nullptr);

}  // namespace stym::plugin
