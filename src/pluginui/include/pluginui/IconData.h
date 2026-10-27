// STYM2149 — pluginui: icons and cursors of the prototype as bitmaps (Fuji logo, R13 envelope shapes, sequencer)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Rasterised from the prototype's SVGs by tools/proto_capture/icons.mjs (src/IconData.cpp, generated file); the
// Atari logo with its outline by tools/icon/make_fuji_data.py (src/FujiData.cpp, generated file, D-404).
#pragma once
#include <array>

#include "gemui/Painter.h"

namespace stym::pluginui::icons {

extern const gemui::Bitmap kFuji;                  // 17 × 14 with its outline, menu bar (D-404)
extern const std::array<gemui::Bitmap, 16> kShapes;  // 36 × 16, envelope shapes 0 to F (SHAPE_SVG)

// Sequencer, 15 × 15: tools (TOOLS) and clipboard (CLIPS).
extern const gemui::Bitmap kToolMove, kToolDraw, kToolErase, kToolSplit, kToolJoin, kToolSelect;
extern const gemui::Bitmap kClipCopy, kClipCut, kClipPaste;
// Transport (ICONS): 20 × 20 in the Step Sequencer window (metronome included), 11 × 11 in the Song window.
extern const gemui::Bitmap kBigPlay, kBigStop, kBigRec, kBigLoop, kBigMetro, kBigStart, kBigPrev, kBigNext;
extern const gemui::Bitmap kSmallPlay, kSmallStop, kSmallRec, kSmallLoop, kSmallStart, kSmallPrev, kSmallNext;
// Note values of the Note menu (figIcon, FIGS), 36 × 16: Step, 1/1 … 1/64 (D-422), dotted, triplets to 1/32T (D-433).
extern const std::array<gemui::Bitmap, 15> kFigures;

// Drawn cursors of the prototype (CSS cursor:url): black pixels (ink), opaque pixels, white or black (mask), hot
// spot.
struct Cursor {
    gemui::Bitmap ink, mask;
    int hotX = 0, hotY = 0;
};
extern const Cursor kCursorPencil;    // 18 × 18, Draw tool
extern const Cursor kCursorScissors;  // 22 × 22, Alt on a note
extern const Cursor kCursorBee;       // 24 × 24, GEM bee of long jobs (body.busy)

}  // namespace stym::pluginui::icons
