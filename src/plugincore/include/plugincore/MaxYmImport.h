// STYM2149 — plugincore: import of maxYMiser songs (.MYS + .MYV) as editable patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The song is played once through by our model of maxYMiser's replay routine (ymio/MaxYmPlayer.h), and the register
// import of YM and SNDH tunes (RegisterImport.h) turns its frames into patterns, with what the song itself tells
// instead of what the frames suggest: each row a step (Sxx where the speed changes), each pattern of the song a part
// (the same ones played again; joined into longer patterns beyond 32), each row that starts a note on a channel a
// note on its track (its MIDI note, the table following the frames from it), the Song restarting at the song's repeat
// step, the tuning at 440 Hz. Tracks 1 to 3 in Authentic 3ch, as for a YM or an SNDH. The timer effects that the
// engine plays become its own:
// - SID: a SID Voice instrument with a Custom waveform of the steps of the timer (volume 15 less what each step takes
//   off), at the ratio of its rate to the note; the SID follows the table (arpeggio, vibrato) as the square does, or
//   holds the note's pitch where the routine's timer leaves the arpeggio out, or the table plays the SID's pitch where
//   the square holds still, whichever plays both closer;
// - sync buzzer: a Sync Buzzer instrument with the shapes of its steps, the envelope tuned to the note (N/D and fine
//   tuning that the timer's rate gives, or its period fixed when no ratio does);
// - digidrums: the channel that plays most of them on the Digidrum track (track 4), each hit on the step where it
//   starts, held until the sample ends, its rate an offset in semitones from the slot's; the samples of the voiceset
//   in slots 1 to 8, at the YM levels, each at the rate that plays it most;
// - square FM: an FM Voice instrument in the Semitones scale, the semitones of its steps (8 at most) as they
//   are, at the ratio of its rate (two interrupts per period of the timer's note) to the note and its steps.
// Pulse width, buzzer FM, sync buzzer FM and sync square are left out (named in the alert).
#pragma once
#include <string>
#include <vector>

#include "plugincore/RegisterImport.h"

namespace stym::ymio {
struct MysSong;
struct MyvVoiceset;
}  // namespace stym::ymio

namespace stym::plugin {

// The frames of a maxYMiser song played once through (10 minutes at most) and the rows that place them.
struct MaxYmTune {
    RegisterTune tune;
    RegisterLayout layout;
};
MaxYmTune maxYmTune(const ymio::MysSong& song, const ymio::MyvVoiceset& voices);

// Replaces patterns, Song, instruments, timing and the length of tracks 1 to 3 as importRegisters does.
RegisterImportResult importMaxYm(const ymio::MysSong& song, const ymio::MyvVoiceset& voices, NonParamState& state);
// Lines of the "maxYMiser song imported" alert.
std::vector<std::string> maxYmImportMessage(const RegisterImport& r, const std::string& fileName);

}  // namespace stym::plugin
