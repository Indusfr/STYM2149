// STYM2149 — plugincore: export of the Song as a YM-Tracker song (.ymt)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The Song of Authentic 3ch as rows of YM-Tracker, the other way of YmtImport.h: from the steps of the sequencer, as
// it plays them, rather than from frames.
// - Song: the entries in play order (repeats and Loop jumps unrolled, 256 positions at most), each one a pattern of its
//   steps (a Rest or Note-off entry a pattern of its length), the identical ones written once; when Loop is on, the
//   restart entry is the restart position; otherwise the song ends on a silent pattern that jumps to itself.
// - Channels: tracks 1 to 3 on channels 1 to 3 (a muted track, or one that a solo leaves out, stays empty); the hits of
//   the Digidrum track on the channel that they take, as Zxx with a C-5 and the silent "Digidrums" instrument (as in
//   the demo songs); channel 4, the digidrum channel, carries the Fxx.
// - Notes: NOTE and OCT (C4, MIDI note 60, is 37) and the instrument on each attack; the volume column where the
//   4-bit volume that the chip plays (the sound's at VOL × the level of the track) is not 15; a note ends as the
//   sequencer ends it: Kxx at its gate, a note off (97) on the step that does not hold it on; SLD → 3xx on the next
//   note; a SID → Qxx (Sxx for Sine and Bounce) on each attack at the rate of the note; GEN SID or Sync Buzzer on an
//   attack → Qxx or Uxx; Axy while a volume slide runs; Cxx → ECx, Dxx → EDx, Rxx → Rxx, Exx → Xxx on a note with the
//   envelope; Sxx and the groove (Ticks timing) or the tempo (BPM timing) → Fxx. One effect per cell: Q, S and U
//   first, then 3xx, Kxx (a note off on the next row when it has no room), Xxx, ECx, EDx, Rxx and Axy.
// - Instruments: one per sound that the notes play (the track's or that of the INS row, with the Vxx and Mxx in force
//   at the attack), 16 at most with the Digidrums one (the slots of the demo songs): ADSR in ticks, tone, noise and its
//   period; the envelope and its shape, its period from the note when the buzzer is tuned (octave N/D), the fixed one
//   otherwise; Sync Buzzer; vibrato; the Modulation table as the arpeggio list (semitones), the pitch list (PER) and,
//   with a flat ADSR, the volume list.
// - Digidrums: the slots that the hits play, at the rate that each hit plays (Rate, root note, Fine, OCT, TON), from
//   Start to End, 255 at most in the bank of the "Digidrums" instrument.
// INFERRED (YmtImport.h): a tick lasts 2.5 / BPM seconds; Qxx, Sxx and Uxx run their timer at 20 × 2^(x + y / 16) Hz;
// a Zxx plays its sample at its own rate; the volume column scales the 4-bit volume linearly. The tempo of BPM timing
// becomes ticks near the replay rate and a BPM that keeps the length of the steps.
// What YM-Tracker does not play as the sequencer does is reported in lines for the end-of-export alert.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymio/YmTracker.h"

namespace stym::plugin {

struct YmtExport {
    ymio::YmtSong song;
    int positions = 0;    // of the order table
    int patterns = 0;
    int instruments = 0;  // used (the others are empty slots)
    int notes = 0;        // attacks written
    int samples = 0;      // digidrum samples
    bool loops = false;   // the song goes back to the restart entry
    std::vector<std::string> remarks;  // what YM-Tracker does not play as the sequencer does
};
struct YmtExportResult {
    std::optional<YmtExport> value;
    std::string error;  // why nothing is exported
};

// The Song as YM-Tracker rows (see above), in Authentic 3ch; `title` names the song.
YmtExportResult exportYmt(const ParamValues& values, const NonParamState& extras, const std::string& title);
// Lines of the "YM-Tracker song exported" alert.
std::vector<std::string> ymtExportMessage(const YmtExport& x, const std::string& fileName);

}  // namespace stym::plugin
