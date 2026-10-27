// STYM2149 — plugincore: Patches library, named track sounds
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// A patch is the sound of a track as the prototype stores it (libSnap): its `patch` object (source, volume,
// envelope, generator, vibrato, Modulation table, ADSR...), its `arp` object, its level and its gate, under a name.
// The library is shared by all projects: a JSON file next to the preferences file, in the export format of the
// prototype ({"kind": "patchlib", "patches": [...]}, which its Import button reads back).
//
// Units and keys of the prototype in the file: EP = chip period / 32 (a decimal number keeps the exact
// period), level and gate from 0 to 1, flags as 0 / 1. The keys that STYM2149 does not know stay in the entry and
// go back out unchanged.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "plugincore/Json.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"

namespace stym::plugin {

// Track parameter carried by a patch (all except On, Pan, Mute, Solo and Send, which belong to the mix).
bool inPatch(Track f) noexcept;

// Library entry: JSON object with at least "name" (text) and "patch" (object).
using PatchEntry = json::Value;

// libSnap: the sound of track `track`, named.
PatchEntry snapPatch(const std::string& name, const ParamValues& values, const NonParamState& state, int track);

std::string patchName(const PatchEntry& e);
void renamePatch(PatchEntry& e, const std::string& name);

// libApply: what loading an entry changes on the track: values of the patch parameters (index, plain value, within
// their ranges) and non-automatable settings of the track (ornament, Digidrum slot, arpeggio), brought back into
// their ranges. A missing key takes the default value of a new track (mkCh).
struct PatchChange {
    std::vector<std::pair<int, float>> params;
    ymengine::TrackParams track;
};
PatchChange patchChange(const PatchEntry& e, const NonParamState& state, int track);

// Library file. Reading also accepts a bare array (the localStorage key of the prototype) and ignores entries
// without a name or without a patch; nullopt if the text is not a library. `newer`: "stym" key of a more recent
// version. Writing adds the "stym" key when a patch carries a STYM2149 key.
std::optional<std::vector<PatchEntry>> parsePatchLibrary(std::string_view text, std::string* error = nullptr,
                                                         bool* newer = nullptr);
std::string dumpPatchLibrary(const std::vector<PatchEntry>& patches);

}  // namespace stym::plugin
