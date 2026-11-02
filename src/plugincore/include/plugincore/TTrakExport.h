// STYM2149 — plugincore: export of the Song as a TTrak module (.TPM)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// What the Song plays in Authentic 3ch, frame by frame (the capture of the register exports, SongCapture.h), written
// as rows and instruments that TTrak's routine (ymio::TtrakPlayer) plays back to the same registers:
// - rows: one per step of the sequencer, its frames the speed (effect $1C where it changes, on the first row and on
//   the restart position); a step on several rows where an instrument has to start within it (a digidrum, an
//   envelope that restarts) or where it lasts more than 128 frames; positions cut at each Song entry (and each repeat
//   of its pattern) and every 64 rows (a pattern break, effect $0B, ends a shorter one), identical patterns written
//   once; the song goes back to the restart entry when Loop is on, otherwise it ends on a position that silences the
//   three channels;
// - notes: the row where the sequencer attacks a note on a channel writes the TTrak note of the pitch heard (MIDI
//   note − 23) and the instrument of the frames that follow, up to the next row that starts one; where the tables of
//   one instrument cannot hold them exactly, the rest goes on as an instrument number alone on a later row;
// - instruments: the frames of a row at one table step per tick (or the slowest speed that fits): the volumes
//   brought up to 15, the row's attenuation (effect $1F) taking the difference off; the amplitude envelope (30
//   steps: volume, square off, noise, buzzer, timer), the noise table (14 periods), the pitch of the first generator
//   heard (square, then timer, then buzzer) as a frequency modulation wave from the note (256 ticks, 1/16 semitone),
//   the others a constant detune or arpeggio from it, or a fixed pitch; a square of period 1 as TTrak's YM pad; SID
//   as TTrak's square SID (the volume, then 0), sync buzzer with its shape (a plain buzzer when its envelope already
//   runs at a whole multiple of the timer's rate: the routine has two timers for three channels), digidrum with its
//   sample slot (the first 32 that play); the buzzer's shape written at the note, with hard sync where the envelope
//   restarts there.
//   A note that another instrument plays as it is takes that one; beyond 32, the least played merge into the closest.
// The module is then played by the routine and compared with the Song frame by frame: what it does not play as
// heard is reported in lines for the end-of-export alert.
#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymio/TTrak.h"

namespace stym::plugin {

struct TtrakExport {
    ymio::TtrakModule module;
    int positions = 0;
    int patterns = 0;
    int instruments = 0;
    int notes = 0;        // rows that start a note
    int samples = 0;      // sample slots
    uint32_t frameHz = 50;
    double seconds = 0;   // played once
    bool loops = false;   // the song goes back to the restart entry
    std::vector<std::string> remarks;  // what the routine does not play as heard
};
struct TtrakExportResult {
    std::optional<TtrakExport> value;
    std::string error;  // why nothing is exported
};

// The Song as TTrak plays it (see above), in Authentic 3ch, `title` its name (16 characters kept); nullopt if
// `cancel` becomes true.
std::optional<TtrakExportResult> exportTtrak(const ParamValues& values, const NonParamState& extras,
                                             const std::string& title, const std::atomic<bool>* cancel = nullptr);
// Lines of the "TTrak module exported" alert.
std::vector<std::string> ttrakExportMessage(const TtrakExport& x, const std::string& fileName);

}  // namespace stym::plugin
