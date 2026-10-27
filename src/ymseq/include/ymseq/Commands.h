// STYM2149 — ymseq: effect commands of the CMD row (text of the cells)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// CMD row of the Step Sequencer (Q2): commands attached to the line, each a letter and a two-digit
// hexadecimal parameter as in maxYMiser: Sxx (ticks per line), Rxx (envelope restart at tick xx), Zxx (demo sync), then
// the step effects: Axy, Bxy and Nxy (volume, envelope period and noise period slides), Oxx (DMA sample offset)
// and Fxx (fine tune). Txx (tempo), Vxx (vibrato), Exx (envelope shape), Dxx and Cxx (note delay and
// cut) have rows of their own since the eleventh DAW test (BPM, VIB, ENV, DLY, CUT); Xyz
// and Pxy are retired. The engine plays them (ymengine::SeqCmd). A step holds one command of each kind
// at most; the CMD cell shows the short name of its command, or MUL for several (tenth DAW test, item 122).
#pragma once
#include <string>
#include <string_view>

#include "ymengine/Sequence.h"

namespace stym::ymseq {

// Short name of a kind of command: ARP (Xyz, retired), TIK (Sxx), RST (Rxx; ENV before the ENV row), SYN
// (Zxx), BPM (Txx), VIB (Vxx), VSL (Axy), PSL (Pxy, retired), DLY (Dxx), CUT (Cxx), BSL (Bxy), NSL (Nxy), SHP (Exx),
// OFS (Oxx), FIN (Fxx); empty for None.
std::string_view cmdTag(ymengine::SeqCmd k) noexcept;
// Text of the CMD cell of step s, from the kinds of the Commands box: "-" without one, the short name of its only
// command, MUL for several.
std::string cmdCellText(const ymengine::SeqLane& l, int s);

}  // namespace stym::ymseq
