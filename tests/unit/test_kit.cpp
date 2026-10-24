// STYM2149 — Chipdrum kit against the prototype (4-bit samples and register frames of the 13 banks)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Data: tests/data/proto/kit.txt, produced by tools/proto_golden/kit.mjs from YM2149_Prototype.html.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ymengine/DrumKit.h"
#include "ymengine/Engine.h"

using namespace stym;
using namespace stym::ymengine;

namespace {
struct Golden {
    std::string bank;
    std::vector<std::vector<uint8_t>> samples;              // [slot]
    std::vector<std::vector<std::vector<int>>> frames[3];  // [vbl 50/60/70][slot][frame·5 + field]
};

std::vector<Golden> loadGolden() {
    std::ifstream in(std::string(STYM_TEST_DATA) + "/proto/kit.txt");
    std::vector<Golden> out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string kind;
        ls >> kind;
        if (kind == "bank") {
            out.emplace_back();
            out.back().bank = line.substr(5);
            out.back().samples.resize(kDrumSlots);
            for (auto& f : out.back().frames) f.resize(kDrumSlots);
        } else if (kind == "sample") {
            int slot = 0;
            size_t len = 0;
            std::string hex;
            ls >> slot >> len >> hex;
            auto& s = out.back().samples[size_t(slot)];
            for (char c : hex) s.push_back(uint8_t(std::stoi(std::string(1, c), nullptr, 16)));
            REQUIRE(s.size() == len);
        } else if (kind == "chip") {
            int vbl = 0, slot = 0;
            size_t n = 0;
            ls >> vbl >> slot >> n;
            auto& f = out.back().frames[(vbl - 50) / 10][size_t(slot)];
            std::string tok;
            while (ls >> tok) {
                std::vector<int> v;
                std::istringstream ts(tok);
                std::string field;
                while (std::getline(ts, field, ',')) v.push_back(std::stoi(field));
                REQUIRE(v.size() == 5);
                f.push_back(v);
            }
            REQUIRE(f.size() == n);
        }
    }
    return out;
}
}  // namespace

TEST_CASE("Chipdrum kit: identical to the prototype, 13 banks", "[kit]") {
    const std::vector<Golden> golden = loadGolden();
    REQUIRE(golden.size() == kKitBanks.size());
    auto kit = std::make_unique<DrumKit>();
    for (size_t b = 0; b < golden.size(); ++b) {
        const Golden& g = golden[b];
        INFO("bank " << g.bank);
        // TR-606 and TR-808 of the prototype renamed TR-606-ish and TR-808-ish, like TR-909-ish (D-245).
        const std::string name = g.bank == "TR-606" || g.bank == "TR-808" ? g.bank + "-ish" : g.bank;
        CHECK(name == kKitBanks[b].name);
        for (int v = 0; v < 3; ++v) {
            const uint32_t vbl = uint32_t(50 + 10 * v);
            INFO("VBL " << vbl);
            kit->build(kKitBanks[b].kit, vbl, ymcore::clocks::kDefault);
            for (int s = 0; s < kDrumSlots; ++s) {
                INFO("slot " << s);
                // The samples do not depend on the VBL rate.
                const auto smp = kit->sample(s);
                const auto& ref = g.samples[size_t(s)];
                REQUIRE(smp.size() == ref.size());
                size_t bad = 0;
                for (size_t i = 0; i < ref.size(); ++i) bad += smp[i] != ref[i];
                CHECK(bad == 0);

                const auto fr = kit->frames(s);
                const auto& rf = g.frames[v][size_t(s)];
                REQUIRE(fr.size() == rf.size());
                for (size_t i = 0; i < rf.size(); ++i) {
                    INFO("frame " << i);
                    CHECK(int(fr[i].tonePeriod) == rf[i][0]);
                    CHECK(int(fr[i].noisePeriod) == rf[i][1]);
                    CHECK(int(fr[i].tone) == rf[i][2]);
                    CHECK(int(fr[i].noise) == rf[i][3]);
                    CHECK(int(fr[i].volume) == rf[i][4]);
                }
            }
        }
    }
}

TEST_CASE("Chipdrum kit: bounds", "[kit]") {
    auto kit = std::make_unique<DrumKit>();
    kit->build(kKitBanks[0].kit, 50, ymcore::clocks::kDefault);
    CHECK(kit->sample(-1).empty());
    CHECK(kit->sample(kDrumSlots).empty());
    CHECK(kit->frames(kDrumSlots).empty());
    // Zero or very slow decay: the number of frames stays within [2, 48].
    KitParams p = kKitBanks[0].kit;
    p.inst[0].dec = 0.01;
    p.inst[1].dec = 1000;
    kit->build(p, 70, ymcore::clocks::kDefault);
    CHECK(kit->frames(0).size() == size_t(DrumKit::kMaxFrames));
    CHECK(kit->frames(1).size() == 2);
}

TEST_CASE("Chipdrum kit: audition of an instrument (Audition, drumprev)", "[kit]") {
    // Muted engine, linear output: the difference with the same engine without audition is the 16-channel sample of
    // the instrument, read without interpolation at 7800 Hz, (s - 7.5) / 7.5 × 0.55, multiplied by the output law.
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3}) {
        CAPTURE(int(mode));
        auto rig = [mode] {
            auto e = std::make_unique<Engine>();
            EngineConfig c;
            c.render.hostRate = 48000;
            e->prepare(c);
            GlobalParams g;
            g.mode = mode;
            g.softClip = false;
            e->setGlobal(g);
            return e;
        };
        auto with = rig(), without = rig();
        const int slot = int(DrumSlot::Snare);
        with->drumPreview(100, slot);
        DrumKit kit;
        kit.build(kKitBanks[0].kit, 50, ymcore::clocks::kDefault);
        const auto s = kit.sample(slot);
        const uint32_t n = 48000 / 4;  // 0.25 s: beyond the 0.18 s of the snare
        std::vector<float> l1(n), r1(n), l2(n), r2(n);
        with->render(l1.data(), r1.data(), n);
        without->render(l2.data(), r2.data(), n);
        const double g = GlobalParams{}.master * 1.2 * 1.4142135623730951;
        int heard = 0;
        double pos = 0;  // cumulative position, like the worklet (pos += 7800 / SR)
        for (uint32_t i = 0; i < n; ++i) {
            double v = 0;
            if (i >= 100 && size_t(pos) < s.size()) {
                v = (double(s[size_t(pos)]) - 7.5) / 7.5 * 0.55;
                pos += 7800.0 / 48000.0;
            }
            CHECK(std::abs(double(l1[i] - l2[i]) - v * g) < 1e-5);
            CHECK(std::abs(double(r1[i] - r2[i]) - v * g) < 1e-5);
            heard += v != 0;
        }
        CHECK(heard > 1000);
    }
}
