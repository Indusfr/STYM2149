// STYM2149 — gemui: how the GEM elements are drawn
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "gemui/Look.h"

namespace stym::gemui {
namespace {
bool gPrototype = false;
}

bool prototypeLook() noexcept { return gPrototype; }
void usePrototypeLook(bool on) noexcept { gPrototype = on; }

}  // namespace stym::gemui
