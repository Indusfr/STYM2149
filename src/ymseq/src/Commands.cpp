// STYM2149 — ymseq: effect commands of the CMD row (text of the cells)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymseq/Commands.h"

#include <array>

namespace stym::ymseq {

std::string_view cmdTag(ymengine::SeqCmd k) noexcept {
    static constexpr std::array<std::string_view, ymengine::kSeqCmds> kTags = {
        "",    "ARP", "TIK", "RST", "SYN", "BPM", "VIB", "VSL", "PSL", "DLY",
        "CUT", "BSL", "NSL", "SHP", "OFS", "FIN", "LFO", "MOD", "ARP", "VGL", "BCV", "NCV"};
    return kTags[size_t(k)];
}

std::string cmdCellText(const ymengine::SeqLane& l, int s) {
    std::string_view tag = "-";
    for (int k = 1; k < ymengine::kSeqCmds; ++k) {
        const auto kind = ymengine::SeqCmd(k);  // a slide's curve goes with its slide (D-394)
        if (!ymengine::seqCmdInBox(kind) || ymengine::seqCmdCurve(kind) || !l.cmdAt(kind, s)) continue;
        if (tag != "-") return "MUL";
        tag = cmdTag(ymengine::SeqCmd(k));
    }
    return std::string(tag);
}

}  // namespace stym::ymseq
