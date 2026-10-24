// STYM2149 — zeroed allocation for the AtariAudio oracle (tests only)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// AtariAudio leaves members uninitialised (Ym2149c: m_noiseCounter, m_currentNoiseMask, m_edgeNeedReset;
// YmRenderer: partial constructor). ymcore zeroes everything (D-019); to compare bit for bit, the oracle therefore
// allocates all memory zeroed.
#include <cstdlib>
#include <new>

void* operator new(std::size_t n) {
    if (void* p = std::calloc(1, n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    if (void* p = std::calloc(1, n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
