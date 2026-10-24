// STYM2149 — ymengine: sequencer patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymengine/Sequence.h"

namespace stym::ymengine {

const SeqLane& emptySeqLane() noexcept {
    static const SeqLane lane;
    return lane;
}

}  // namespace stym::ymengine
