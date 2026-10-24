// STYM2149 — read access to the internal state of AtariAudio 1.26 (tests only)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The members of Ym2149c and YmRenderer are private. To compare the internal state step by step (S1) without
// modifying the AtariAudio copy, the "private" sections are made public in this translation unit only. Only data
// members are read there (no private function is called: under MSVC, the access level is part of the decorated name
// of functions).
#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The headers included here only include <stdint.h> themselves, already included before the macro.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include "YmRenderer.h"
#include "ym2149c.h"
#undef private
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
