// STYM2149 — plugincore: export of the Song as a maxYMiser song (.MYS) and voiceset (.MYV)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// What the Song plays in Authentic 3ch, frame by frame (the capture of the register exports), written as rows
// and instruments that maxYMiser's routine (ymio::MaxYmPlayer) plays back to the same registers:
// - rows: one per step of the sequencer, the speed its frames (Sxx where it changes); song steps cut at each Song entry
//   and every 64 rows (Bxx ends a shorter one), the identical patterns of a channel written once, an empty one left
//   out ($FF); the song goes back to the restart entry when Loop is on, otherwise it ends on a step that cuts the
//   three channels;
// - notes: the row where the sequencer attacks a note on a channel (or a digidrum starts) writes the note and the
//   instrument of the frames that follow, up to the next row that does, or to a note off where the channel falls
//   silent until then; a note longer than the sequences of an instrument hold goes on as an instrument number alone on
//   a later row; a note that starts within a row plays in the instrument of the row (its envelope restart is lost);
// - instruments: the frames of a row's note at one sequence step per tick: volume (the loudest brought to 15, the
//   volume column taking the difference off), arpeggio and vibrato holding the period of the square from the note (of
//   the timer or the buzzer when there is no square), fixed frequency the period of the next generator, mixer (square,
//   noise, buzzer shape, timer effect) and noise period; SID as a square waveform of 2 steps, sync buzzer with its
//   shape, digidrum with its sample and its MFP rate; a start sync where the envelope restarts at the row. Notes whose
//   tables are the same share an instrument; beyond 32, the least played merge into the closest.
// What the routine does not play as heard is reported in lines for the end-of-export alert.
#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymio/MaxYm.h"

namespace stym::plugin {

struct MaxYmExport {
    ymio::MysSong song;
    ymio::MyvVoiceset voices;
    int steps = 0;        // song steps
    int patterns = 0;
    int instruments = 0;
    int notes = 0;        // rows that start a note
    int samples = 0;      // digidrum samples
    uint32_t frameHz = 50;
    double seconds = 0;   // played once
    bool loops = false;   // the song goes back to the restart entry
    std::vector<std::string> remarks;  // what the routine does not play as heard
};
struct MaxYmExportResult {
    std::optional<MaxYmExport> value;
    std::string error;  // why nothing is exported
};

// The Song as maxYMiser plays it (see above), in Authentic 3ch; nullopt if `cancel` becomes true.
std::optional<MaxYmExportResult> exportMaxYm(const ParamValues& values, const NonParamState& extras,
                                             const std::atomic<bool>* cancel = nullptr);
// Lines of the "maxYMiser exported" alert, the files written named.
std::vector<std::string> maxYmExportMessage(const MaxYmExport& x, const std::string& songFile,
                                            const std::string& voicesFile);

}  // namespace stym::plugin
