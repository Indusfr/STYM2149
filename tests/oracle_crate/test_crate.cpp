// STYM2149 — S3: C++ port of ym2149-rs ("Raw-crate" mode, tests only) against the crate itself
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "CratePort.hpp"
#include "Metrics.h"
#include "Scenarios.h"

extern "C" {
struct RustYm;
RustYm* ymffi_new(uint32_t clk, uint32_t sr);
void ymffi_free(RustYm*);
void ymffi_write(RustYm*, uint8_t, uint8_t);
void ymffi_render_i16(RustYm*, int16_t*, size_t);
}

using namespace stym::test;

TEST_CASE("S3 C++ port of ym2149-rs against the crate", "[S3][oracle]") {
    uint64_t total = 0, bad = 0;
    for (uint32_t rate : {44100u, 48000u}) {
        std::vector<Scenario> scs = m0Scenarios();
        for (uint32_t seed = 20; seed < 24; ++seed) scs.push_back(randomScenario(seed, uint64_t(rate) * 3, 20, true));
        for (const Scenario& sc : scs) {
            RustYm* rust = ymffi_new(2000000, rate);
            CrateYm2149 port(2000000, rate);
            const size_t n = size_t(rate) * 3;
            std::vector<int16_t> a(n), b(n);
            size_t next = 0;
            for (size_t i = 0; i < n; ++i) {
                while (next < sc.writes.size() && sc.writes[next].at <= i) {
                    const TimedWrite& w = sc.writes[next++];
                    if (w.reg < 14) {  // the crate refuses R14/R15
                        ymffi_write(rust, w.reg, w.value);
                        port.write(w.reg, w.value);
                    }
                }
                ymffi_render_i16(rust, &a[i], 1);
                b[i] = port.nextSample();
            }
            ymffi_free(rust);
            uint64_t d = 0;
            for (size_t i = 0; i < n; ++i) d += a[i] != b[i];
            total += n;
            bad += d;
            INFO(sc.name << " at " << rate << " Hz");
            CHECK(d == 0);
        }
    }
    metric("S3", "C++ \"Raw-crate\" port against ym2149-rs 0.9.1, 44.1/48 kHz (" + std::to_string(total) + " samples)",
           std::to_string(bad) + " deviation(s)", "0", bad == 0);
}
