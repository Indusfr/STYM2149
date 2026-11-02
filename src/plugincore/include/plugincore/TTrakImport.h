// STYM2149 — plugincore: import of TTrak modules (.TPM) as editable patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// As for a maxYMiser song (MaxYmImport.h): the module is played once through by our model of TTrak's replay routine
// (ymio/TTrakPlayer.h), and the register import (RegisterImport.h) turns its ticks into patterns, with what the module
// tells: each row a step (Sxx where the speed or the groove changes it), each position of the song a part, each row
// that gives a note or an instrument a note on its channel's track (MIDI note = TTrak note + 23, the table following
// the ticks from it), the Song restarting at the module's restart position. Tracks 1 to 3 in Authentic 3ch. The timer
// effects that the engine plays become its own:
// - SID (2 or 16 levels, square, morph): a SID Voice instrument with the levels of the timer's wave at volume 15;
// - sync buzzer: a Sync Buzzer instrument with its four shapes;
// - tone FM (types 6 and 8): an FM Voice instrument with its four steps, multiples x1, x2, x3 or x1.5 of the
//   note's frequency;
// - digidrums: the channel that plays most of them on the Digidrum track (track 4); the first eight sample slots that
//   the module plays in slots 1 to 8, at the YM levels (TTrak's samples are volumes 0-15).
#pragma once
#include <string>
#include <vector>

#include "plugincore/RegisterImport.h"

namespace stym::ymio {
struct TtrakModule;
}  // namespace stym::ymio

namespace stym::plugin {

// The ticks of a TTrak module played once through (10 minutes at most) and the rows that place them.
struct TtrakTune {
    RegisterTune tune;
    RegisterLayout layout;
    std::vector<int> sampleSlots;  // TTrak sample slot of each Digidrum slot of the import, in order
};
TtrakTune ttrakTune(const ymio::TtrakModule& module);

// FM Voice multiple (index in ymengine::kFmMultiples) of a step of TTrak's FM timers, whose factor 0-3 divides the
// note's period by 1, 2, 3 or 3/2; the factor of a multiple, -1 for those TTrak has not (x0.5, x4, x5, x6).
uint8_t ttrakFmMultiple(uint8_t factor) noexcept;
int ttrakFmFactor(uint8_t multiple) noexcept;

// Replaces patterns, Song, instruments, timing and the length of tracks 1 to 3 as importRegisters does.
RegisterImportResult importTtrak(const ymio::TtrakModule& module, NonParamState& state);
// Lines of the "TTrak module imported" alert.
std::vector<std::string> ttrakImportMessage(const RegisterImport& r, const std::string& fileName);

}  // namespace stym::plugin
