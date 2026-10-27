// STYM2149 — LZH decompression: port of pylzh (ymio) against the AtariAudio 1.26 decompressor
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

#include "Metrics.h"
#include "RefData.h"
#include "ymio/Lzh.h"

#include "external/lzh.h"  // last: this header defines short macros (NC, BUFSIZE…)

using namespace stym;
using namespace stym::test;

TEST_CASE("LZH: pylzh port against the AtariAudio decompressor", "[lzh][oracle][refdata]") {
    if (refDataDir().empty()) {
        if (refDataRequired()) FAIL("reference tunes missing although STYM_REQUIRE_REFDATA is set");
        SKIP("reference tunes missing: run tools/fetch_refdata.sh");
    }
    int packed = 0, mismatches = 0;
    uint64_t bytes = 0;
    for (const RefTune& t : refTunes()) {
        const std::vector<uint8_t> data = loadRefTune(t.file);
        REQUIRE_FALSE(data.empty());
        if (!LzhDepacker::IsLzhPacked(data.data(), uint32_t(data.size()))) continue;
        ++packed;
        INFO(t.file);
        auto depacker = std::make_unique<LzhDepacker>();  // ~40 KB of state: not on the stack
        uint32_t size = 0;
        void* out = depacker->Unpack(data.data(), uint32_t(data.size()), size);
        REQUIRE(out);
        const std::vector<uint8_t> ref(static_cast<uint8_t*>(out), static_cast<uint8_t*>(out) + size);
        std::free(out);
        const ymio::LzhResult ours = ymio::unpackLzh(data);
        CHECK(ours.error.empty());
        const bool same = ours.data == ref;
        CHECK(same);
        mismatches += !same;
        bytes += ref.size();
    }
    CHECK(packed > 0);
    metric("LZH", "pylzh port against AtariAudio, " + std::to_string(packed) + " compressed tunes (" +
                      std::to_string(bytes) + " bytes)",
           std::to_string(mismatches) + " deviation(s)", "0", mismatches == 0);
}

TEST_CASE("LZH: packLzh archives read by the AtariAudio decompressor", "[lzh][oracle]") {
    uint32_t seed = 11;
    const auto noise = [&](size_t n) {
        std::vector<uint8_t> d(n);
        for (uint8_t& b : d) {
            seed = seed * 1664525u + 1013904223u;
            b = uint8_t(seed >> 24);
        }
        return d;
    };
    std::vector<std::vector<uint8_t>> samples;
    samples.push_back({'A'});
    samples.push_back(std::vector<uint8_t>(70'000, 0));
    samples.push_back(noise(100'000));
    std::vector<uint8_t> far = noise(8192);
    for (int k = 0; k < 3; ++k) far.insert(far.end(), far.begin(), far.begin() + 8192);
    samples.push_back(far);
    // Plausible register frames: values that change little, very uneven distribution (deep trees).
    std::vector<uint8_t> regs;
    for (uint32_t t = 0; t < 30'000; ++t)
        for (uint32_t r = 0; r < 16; ++r) regs.push_back(uint8_t((r * 17 + (t / (r + 3)) * (r & 3)) & (r < 8 ? 0xff : 0x1f)));
    samples.push_back(regs);
    int mismatches = 0;
    for (const std::vector<uint8_t>& d : samples) {
        INFO(d.size());
        const std::vector<uint8_t> a = ymio::packLzh("TEST.YM", d);
        REQUIRE(LzhDepacker::IsLzhPacked(a.data(), uint32_t(a.size())));
        auto depacker = std::make_unique<LzhDepacker>();
        uint32_t size = 0;
        void* out = depacker->Unpack(a.data(), uint32_t(a.size()), size);
        REQUIRE(out);
        const bool same = size == d.size() && std::equal(d.begin(), d.end(), static_cast<const uint8_t*>(out));
        std::free(out);
        CHECK(same);
        mismatches += !same;
    }
    metric("LZH", "packLzh read back by the AtariAudio decompressor, " + std::to_string(samples.size()) + " contents",
           std::to_string(mismatches) + " deviation(s)", "0", mismatches == 0);
}
