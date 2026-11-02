// STYM2149 — plugincore: binary files of the File menu (preset, pattern, Song, kit) and the naming rules of the
// Patches library
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// File > Load, Save and Save as write and read the format of the plugin state (State.h), described in
// docs/FILE_FORMAT.md. The JSON files of the prototype (preset, pattern, Song, kit) are no longer written nor read:
// STYM2149 has not been released, so no user keeps one (Indus, 29/10/2026 14:20). The Patches library stays in
// JSON (Patches.h): its file is shared by every project, and the Patches window imports and exports it; its "stym" key
// = {v: 1} says which version of the STYM2149 additions it carries, a more recent one being reported when
// reading, which takes what it knows.
#pragma once
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plugincore/Json.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymengine/DrumKit.h"
#include "ymseq/Project.h"

namespace stym::plugin {

// The files of the File menu, and the Patches library (Patches.h).
enum class FileKind : uint8_t { Preset, Pattern, Song, Kit, Patches };
inline constexpr int kFileKinds = 5;

// Version of the STYM2149 additions in the JSON of the Patches library ("stym" key).
inline constexpr int kStymFileVersion = 1;
// "stym" key = {v: kStymFileVersion} appended at the end of the document.
void markStymFile(json::Value& doc);
// Document whose "stym" key comes from a more recent version of STYM2149.
bool newerStymFile(const json::Value& doc) noexcept;

// Name offered by the Export of the Patches library (the prototype's, without PRISM).
inline constexpr std::string_view kPatchLibraryFileName = "stym2149-patches.json";
// ensureJson: edge spaces removed, ".json" appended if missing (case ignored); empty if the name is empty.
std::string ensureJson(std::string_view name);
// Same rule for the exports: the first extension (in lower case) appended if the name ends with none of them.
std::string ensureExtension(std::string_view name, std::initializer_list<std::string_view> extensions);

// A Song file read (NativeRead), applied by applySongFile.
struct SongFile {
    ymseq::Song song;
    bool songOn = false;
    bool hasPatterns = false;  // it replaces the 32 patterns
    std::array<std::optional<ymseq::Pattern>, ymseq::kPatterns> patterns;  // nullopt: no pattern
};
// Reader of songFile of the prototype: Song and Song on replaced; patterns replaced if the file has some, the current
// pattern staying the one with the same number (empty, of the current length, if the file lacks it).
void applySongFile(const SongFile& f, ymseq::Project& p, bool& songOn);

// Binary files of File > Load, Save and Save as: the format of the plugin state (State.h), described field by
// field in docs/FILE_FORMAT.md, the flags of the header giving the kind of file. Project (.stym): the whole state
// without the recorder take; pattern (.stympat): the current pattern, as pattern 0; Song (.stymsng): Song flag,
// patterns created, Song; kit (.stymkit): the Chipdrum kit.
std::string_view nativeExtension(FileKind k) noexcept;
// Name offered by the first Save: "stym2149.stym", "stym2149.stympat"...
std::string nativeFileName(FileKind k);
std::vector<uint8_t> encodeNativeFile(FileKind k, const ParamValues& values, const NonParamState& extras);
// Content that starts with the header of the format ("STYM").
bool isNativeFile(std::string_view bytes) noexcept;
// A binary file read: its kind and what it carries for that kind.
struct NativeRead {
    bool ok = false;
    bool newer = false;       // refused: written by a more recent version of STYM2149
    FileKind kind = FileKind::Preset;
    ParamValues values{};     // project
    NonParamState extras;     // project
    ymseq::Pattern pattern;   // pattern
    SongFile song;            // Song, applied by applySongFile
    ymengine::KitParams kit;  // kit
};
NativeRead readNativeFile(std::string_view bytes);

}  // namespace stym::plugin
