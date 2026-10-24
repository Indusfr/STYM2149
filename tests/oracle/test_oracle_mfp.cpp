// STYM2149 — replica of the AtariAudio timer (ymengine::MfpTimerAtariAudio) against Mk68901 of AtariAudio 1.26.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include "Mk68901.h"
#include "ymengine/Mfp68901.h"

using namespace stym;

TEST_CASE("MFP timer: bit-exact replica of AtariAudio", "[mfp][oracle]") {
    for (uint32_t host : {44100u, 48000u, 96000u, 250000u}) {
        Mk68901 aa;
        aa.Reset(host);
        // Timers A and B enabled and unmasked, like YmRenderer::Load.
        aa.Write8(0x07, (1 << 5) | (1 << 0));
        aa.Write8(0x13, (1 << 5) | (1 << 0));
        ymengine::MfpTimerAtariAudio ours[2];
        ours[0].reset(host);
        ours[1].reset(host);
        uint32_t rng = 12345;
        auto next = [&] {
            rng = rng * 1103515245u + 12345u;
            return (rng >> 16) & 0x7fff;
        };
        uint64_t fires = 0, mismatches = 0;
        for (int i = 0; i < 400000; ++i) {
            if (next() % 500 == 0) {
                const int slot = int(next() & 1);
                const uint8_t prediv = uint8_t(next() % 8), count = uint8_t(next() & 0xff);
                // YmRenderer::SetTimer: control register then data register.
                aa.Write8(slot ? 0x1b : 0x19, prediv);
                aa.Write8(slot ? 0x21 : 0x1f, count);
                ours[slot].write(prediv, count);
            }
            for (int t = 0; t < 2; ++t) {
                const bool a = aa.Tick(t), o = ours[t].tick();
                fires += a;
                mismatches += a != o;
            }
        }
        INFO(host << " Hz");
        CHECK(fires > 1000);
        CHECK(mismatches == 0);
    }
}
