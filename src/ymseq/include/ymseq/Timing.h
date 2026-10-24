// STYM2149 — ymseq: Ticks timing of the sequencer (speed, groove, equivalent tempo)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Ticks timing (M6, Q3, D-149): a line lasts Speed replay ticks, plus the groove's ticks for that line (values taken
// in a loop). Keyboard input of the speed and the groove, and the tempo that follows from them.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ymengine/Params.h"

namespace stym::ymseq {

// Ticks added to successive lines (0 to kGrooveMax each), used in a loop over the groove's length.
using GrooveValues = std::array<uint8_t, ymengine::kGrooveSteps>;

// Groove as text: its values separated by a space ("0 1"); `length` is brought into 1-kGrooveSteps.
std::string grooveText(const GrooveValues& groove, int length);

// Typed speed: an integer, brought into 1-kSpeedMax; nothing if the text is not an integer.
std::optional<int> parseSpeed(std::string_view text);

// Typed groove: 1 to 16 integers separated by spaces or commas, brought into 0-kGrooveMax; empty text: a null groove
// of one value; nothing if an item is not an integer or if there are more than 16.
struct Groove {
    GrooveValues values{};
    int length = 1;
};
std::optional<Groove> parseGroove(std::string_view text);

// Tempo at which one step of the resolution (kResolutions) lasts `speed` ticks of `tickHz` (without the groove):
// 60 × tickHz / (speed × steps per beat); 125 BPM for 6 ticks at 50 Hz in sixteenth notes.
double ticksBpm(double tickHz, int speed, int resolution) noexcept;

}  // namespace stym::ymseq
