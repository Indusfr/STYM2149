// STYM2149 — development tools: a tune imported as patterns (File > Import), played back by the engine and compared
// frame by frame with the tune (ymimport, mymimport).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// For each voice, over the frames of the tune: the share of frames where both are silent or both sound, and among
// those that sound in both, the share with the same volume (or both on the envelope), the same mixer, the same tone
// period (and within 10 cents) and the same noise period; "exact" counts the frames where all of them agree.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "plugincore/RegisterImport.h"
#include "ymengine/Engine.h"

namespace stym::tools {

using Frame = std::array<uint8_t, 14>;

struct ScoreVoice {
    bool audible = false, env = false, tone = false, noise = false;
    int vol = 0, tp = 0, np = 0;
};

// The three voices of a frame, periods multiplied by k (2 MHz / clock of the frame).
std::array<ScoreVoice, 3> scoreVoices(const Frame& r, double k);
// Both silent, or both sounding alike.
bool sameVoice(const ScoreVoice& x, const ScoreVoice& y);
// "tn 15  478 np  1": mixer, volume (e: envelope; - after it: silent), tone and noise periods.
std::string showVoice(const ScoreVoice& v);

struct Score {
    long frames = 0, agree = 0, both = 0, vol = 0, mix = 0, period = 0, cents = 0, tonal = 0, noise = 0, noisy = 0,
         exact = 0;
    void add(const Score& s);
};

// Frame i of the tune (periods × k) against frame i − offset of the playback (silent outside it), from frame
// max(0, offset) on; the voices that `skip` marks at a frame (timer effects, scored apart) are left out.
std::array<Score, 3> scoreFrames(const std::vector<Frame>& tune, double k, const std::vector<Frame>& played,
                                 int offset, const std::vector<std::array<bool, 3>>* skip = nullptr);
Score total(const std::array<Score, 3>& s);
double percent(long a, long b);

// The settings that File > Import applies (Authentic 3ch, tracks 1 to 3 on Chip Voice at Level 100, track 4 on
// Digidrum when the import has digidrums, the others off, the VBL rate and Tuning A4 of the import); the Song played
// once by the engine with them: its registers, one frame per replay tick, and in `fx` the timer effect of each
// channel; nullopt if the capture fails.
using Effects = std::array<ymengine::RegisterFrame::Effect, 3>;
plugin::ParamValues importValues(const plugin::RegisterImport& m);
std::optional<std::vector<Frame>> playImport(const plugin::RegisterImport& m, const plugin::NonParamState& state,
                                             std::vector<Effects>* fx = nullptr);
// The steps `from` to `to` of track t along the Song: NOTE, INS, SRC, TIE, GAT, commands.
void printCells(const plugin::NonParamState& state, int t, int from, int to);

}  // namespace stym::tools
