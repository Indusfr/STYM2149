// STYM2149 — plugincore: import of YM-Tracker songs (.ymt) as editable patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The song (ymio::parseYmt) becomes patterns, a Song, instruments and Digidrum slots in Authentic 3ch, as a YM, an
// SNDH or a maxYMiser song does (RegisterImport.h), but from its rows rather than from frames:
// - Song: the order table walked as an XM is (XmImport.h): Bxx jumps, Dxx pattern breaks, the end of the table or the
//   return to a position already played becomes the restart point; the patterns met become patterns 1 to n (32 at
//   most), cut at their Dxx.
// - Channels: the first three tone channels of the first chip on tracks 1 to 3; the digidrum channel and the Zxx
//   hits on track 4 (Digidrum). The other chip and its channels are left out.
// - Notes: NOTE and OCT (37, the C-5 of YM-Tracker, is C4, MIDI note 60); a note holds until the next one, the note
//   off (97) or its Kxx (GAT at that tick); VOL = the VOL that plays the volume of the channel (volume column, Cxx,
//   EAx, EBx) on each held line, less the slides that the engine plays since the attack (VOL 1 plays the 4-bit volume
//   8, the quietest that VOL reaches on a sound of volume 15); Axy (5xy, 6xy) → Axy for its line;
//   Fxx → Sxx (Ticks timing) or the BPM row (BPM timing); 3xx → SLD on the note before; ECx → Cxx, EDx → Dxx,
//   Rxx → Rxx.
// - Instruments: one per instrument of the song and set of effects that its notes play (arpeggio 0xy, vibrato 4xy,
//   SID Qxx, Sinus-SID Sxx, Sync-Buzzer Uxx, envelope Xxx, Pxx, Yxx and Mxx), 32 at most: Chip ADSR in ticks of the
//   song, tone, noise and noise period; the arpeggio macro, the pitch macro (semitones and PER at the note the
//   instrument plays most) and, without ADSR, the volume list in its Modulation table, looped when the three repeat
//   within 64 ticks; the vibrato of the instrument or of the 4xy; the hardware envelope as a Sync Buzzer tuned to the
//   note when its period follows the note and its shape loops ($8, $A, $C, $E), at its fixed period otherwise. An
//   effect that starts after the attack switches on there (Vxx, Mxx, GEN, SRC, Exx).
// - Digidrums: the samples that the song plays, 8 at most, in Digidrum slots 1 to 8 at their own rate (the values of
//   the file kept), each hit held for the length of its sample.
// INFERRED from the demo songs and the application's texts (rule §9, the file layout in ymio/YmTracker.h): the effects
// of the effect column last until the end of their note; the volume of a channel (0-64) scales its 4-bit volume
// linearly; a tick lasts 2.5 / BPM seconds; Qxx, Sxx and Uxx run their timer at 20 × 2^(x + y / 16) Hz; the automatic
// envelope period is that of a sawtooth at the note (round(2 MHz / (256 × frequency))), times the octave of the
// HardEnv tab or the Pxx divisor; a note of the digidrum channel plays sample (note - 1) mod 12 of the bank of its
// instrument.
// What the import does not render is reported in lines for the end-of-import alert.
#pragma once
#include <string>
#include <vector>

#include "plugincore/RegisterImport.h"

namespace stym::ymio {
struct YmtSong;
}  // namespace stym::ymio

namespace stym::plugin {

// Replaces patterns 0 to n-1, the Song, instruments 0 to k-1, the timing (timing, speed, no groove), the length of
// tracks 1 to 4 and the Digidrum slots that the song fills in `state`; nothing changes if the song is refused (no
// note on the channels imported). The free tempo of BPM timing is in the result (RegisterImport::tempo).
RegisterImportResult importYmt(const ymio::YmtSong& song, NonParamState& state);
// Lines of the "YM-Tracker song imported" alert.
std::vector<std::string> ymtImportMessage(const RegisterImport& r, const std::string& fileName);

}  // namespace stym::plugin
