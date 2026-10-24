// STYM2149 — plugin: build identifier (D-211)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Written by CMake into BuildId.cpp (from BuildId.cpp.in): the number of the CI run that compiled the plugin and the
// commit it built, so that Fuji > Information tells which build is running. A local build is "dev", without commit.
#pragma once

namespace stym::plugin {

extern const char* const kBuildNumber;  // CI run number, or "dev"
extern const char* const kBuildCommit;  // first 7 characters of the commit, or empty

}  // namespace stym::plugin
