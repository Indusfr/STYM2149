// STYM2149 — plugincore: project instruments (M6, B1, D-147)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// An instrument is the sound of a track stored in the project under a name, up to 32 ($01 to $20). The melodic steps
// whose INS row designates it play it (sound and arpeggio; level, pan and gate stay those of the track).
// The patch of the prototype (plugincore/Patches.h) is its exchange format: an instrument carries what a patch
// carries, loading it into a track is a patch load (libApply), and the Patches library stores instruments.
#pragma once
#include <string>
#include <string_view>

#include "plugincore/Patches.h"
#include "plugincore/State.h"

namespace stym::plugin {

using ymengine::kInstruments;
inline constexpr size_t kInstrumentNameMax = 16;

// Name brought back to the rule: printable ASCII characters, edge spaces removed, 16 characters at most.
std::string cleanInstrumentName(std::string_view name);
// Displayed name: its own, or "Instrument n" (n from 1 to 32) when it has none.
std::string instrumentLabel(const Instrument& ins, int slot);

// The sound of track `track` (libSnap), named.
Instrument captureInstrument(const std::string& name, const ParamValues& values, const NonParamState& state, int track);
// Entry of the Patches library, and instrument of an entry (missing keys: values of a new track).
PatchEntry instrumentEntry(const Instrument& ins);
Instrument instrumentFromEntry(const PatchEntry& e);
// What loading an instrument into track `track` changes (libApply).
PatchChange instrumentChange(const Instrument& ins, const NonParamState& state, int track);

// Instruments as the engine plays them.
void fillInstruments(const NonParamState& state, ymengine::InstrumentBank& out);

}  // namespace stym::plugin
