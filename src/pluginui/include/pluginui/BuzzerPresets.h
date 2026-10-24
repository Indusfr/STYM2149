// STYM2149 — pluginui: presets of the Sync Buzzer settings (Presets menu of the GENERATOR frame, D-227, D-230)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Presets specific to STYM2149 (Y36: those of YM-Tracker are not carried over). A preset keeps the Sync Buzzer and its
// envelope alone (fourth DAW test, item 32, D-230): it sets the shape (Shape R13), tunes the envelope to the note ×
// N / D, sets YM Pad and puts the fine tune back to 0. Ratios that are not whole numbers give the "sync" timbre (the
// timer cuts the sawtooth at the note); a whole ratio would only transpose it.
#pragma once
#include <array>

namespace stym::pluginui {

// One entry of the menu, applied by MainWindow::applyBuzzerPreset to the focused track.
struct BuzzerPreset {
    const char* name;  // menu label
    // Env Shape parameter (R13, 0-15); the envelope tuned to the note × n / d (TrackPatch::envRatioN, envRatioD).
    int shape, n, d;
    bool pad;          // YM Pad (TrackPatch::ymPad)
};

inline constexpr std::array<BuzzerPreset, 8> kBuzzerPresets = {{
    {"Saw", 8, 1, 1, false},
    {"Triangle", 10, 1, 1, false},
    {"Third", 8, 5, 4, false},
    {"Fifth", 8, 3, 2, false},
    {"Seventh", 8, 7, 4, false},
    {"Tenth", 8, 5, 2, false},
    {"Tri fifth", 10, 3, 2, false},
    {"Soft saw", 8, 1, 1, true},
}};

}  // namespace stym::pluginui
