// STYM2149 — plugincore: JSON files of the File menu (preset, pattern, Song, kit) (M7, A1, D-165, D-168)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Schemas of the prototype (savePreset, savePattern, saveSong, saveKit; loadPresetObj and the readers of
// patternFile, songFile, kitFile), version 1, which the prototype reads back:
//   preset  {app: "STYM2149", v: 1, glob: {master, mode3ch, chips, bpm, gate, seqOn, steps, digiCh, digiCh2, vbl, sig,
//            reso, swing, metro}, curPat, chans: [16 × {patch, enabled, level, name, seqOct, gate, mute, solo, pan,
//            plen, arp, lane}], pats: [32 × motif or null], song: [{pat, rep, bpm?}], songOn, kit}
//   pattern {app, kind: "pattern", v: 1, pattern: {steps, chans: [16 × rows]}}
//   song    {app, kind: "song", v: 1, song, songOn, pats}
//   kit     {kind: "drumkit", kit: {kick: {pitch, sweep, dec, noise, vol}, snare, clap, hat, ohat, chat, tom, tom2}}
// A motif: {steps, chans: [16 × {pat, tie, ... ton, pat2-pat6, oct2-oct6, dur2-dur6}]}, each row of 256 steps.
// The preset, the pattern and the Song are written on one line (JSON.stringify(d)), the kit indented by one space.
//
// Additions of STYM2149, under keys that the prototype ignores (D-165, D-168):
// - preset: "stym" = {v: 1, state: complete plugin state in base64}, the state that the host saves (State.h)
//   without the recorder take; it carries everything that the fields of the prototype do not say (instruments,
//   sequences, INS and CMD rows, extended Song, machine, DMA, Digidrums and their samples, track and sequencer
//   settings). When reading, it wins over the fields of the prototype; a file without it (the prototype's) or from a
//   more recent version is read field by field, on top of the current project as loadPresetObj does;
// - motifs: "ins" and the CMD rows ("cmd" for Xyz, "cmdS", "cmdR", "cmdZ", "cmdT", "cmdV": D-330; "cmdA", "cmdP",
//   "cmdD", "cmdC", "cmdB", "cmdN", "cmdE", "cmdO", "cmdF": D-338) of the tracks that have them; a file written
//   before D-330 kept every command under "cmd", each goes to the row of its kind; Xyz and Pxy, retired (D-340,
//   D-344), are dropped;
// - Song: "kind" ("rest", "noteoff", "loop"), "arg" and "restart" of the entries that have them (D-158).
// Version of the additions (B6, D-174): the "stym" key of the preset carries "v"; a pattern, a Song or a Patches
// library that carries additions ends with "stym" = {v: 1} (without additions, the file stays the prototype's). A
// more recent version is reported when reading, which takes what it knows.
// Reading clamps everything (sanitize) and completes the short rows (padLane: old 64-step format).
#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "plugincore/Json.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymengine/DrumKit.h"
#include "ymseq/Project.h"

namespace stym::plugin {

// The JSON files of the File menu, and the Patches library (Patches.h), which shares their naming rules.
enum class FileKind : uint8_t { Preset, Pattern, Song, Kit, Patches };
inline constexpr int kFileKinds = 5;

// Version of the STYM2149 additions in the JSON files ("stym" key, B6, D-174).
inline constexpr int kStymFileVersion = 1;
// "stym" key = {v: kStymFileVersion} appended at the end of the document.
void markStymFile(json::Value& doc);
// Document whose "stym" key comes from a more recent version of STYM2149.
bool newerStymFile(const json::Value& doc) noexcept;

// Name offered when saving: fileNames of the prototype and name of its Patches export, without PRISM (D-060).
std::string_view defaultFileName(FileKind k) noexcept;
// ensureJson: edge spaces removed, ".json" appended if missing (case ignored); empty if the name is empty.
std::string ensureJson(std::string_view name);
// Same rule for the exports (M7): the first extension (in lower case) appended if the name ends with none of them.
std::string ensureExtension(std::string_view name, std::initializer_list<std::string_view> extensions);

// Preset: the whole project.
std::string dumpPreset(const ParamValues& values, const NonParamState& extras);
// A preset read: the whole project, ready to replace the current one.
struct PresetRead {
    enum class Status : uint8_t { Ok, NotJson, NotPreset };  // NotPreset: JSON without app "STYM2149"
    Status status = Status::NotJson;
    ParamValues values{};
    NonParamState extras;
    bool exact = false;  // STYM2149 state read back ("stym" key)
    bool newer = false;  // "stym" key from a more recent version: fields of the prototype only
};
// `values` and `extras`: the current project, on which a preset without a complete state is read.
PresetRead parsePreset(std::string_view text, const ParamValues& values, const NonParamState& extras);

// Pattern: the current pattern.
std::string dumpPatternFile(const ymseq::Pattern& p);
// `newer`: "stym" key from a more recent version (the file is read for what is known of it).
std::optional<ymseq::Pattern> parsePatternFile(std::string_view text, bool* newer = nullptr);

// Song: entries, Song on, patterns created.
std::string dumpSongFile(const ymseq::Project& p, bool songOn);
// A Song file read, applied afterwards by applySongFile.
struct SongFile {
    ymseq::Song song;
    bool songOn = false;
    bool hasPatterns = false;  // "pats" key: it replaces the 32 patterns
    bool newer = false;        // "stym" key from a more recent version
    std::array<std::optional<ymseq::Pattern>, ymseq::kPatterns> patterns;  // nullopt: no pattern (null)
};
std::optional<SongFile> parseSongFile(std::string_view text);
// Reader of songFile: Song and Song on replaced; patterns replaced if the file has some, the current pattern staying
// the one with the same number (empty, of the current length, if the file lacks it).
void applySongFile(const SongFile& f, ymseq::Project& p, bool& songOn);

// Chipdrum kit: the settings of the file replace those of `current`, instrument by instrument and key by key.
std::string dumpKitFile(const ymengine::KitParams& kit);
std::optional<ymengine::KitParams> parseKitFile(std::string_view text, const ymengine::KitParams& current,
                                               bool* newer = nullptr);

}  // namespace stym::plugin
