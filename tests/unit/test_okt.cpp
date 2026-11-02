// STYM2149 — ymio: reading Oktalyzer modules: channels from CMOD (split voices) and their Amiga pans, speed,
// orders, notes 1-36 and samples, full volume on split channels, effects in the XM shape or kept by their Oktalyzer
// name, samples and their data chunks, loops, cut or refused files.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>
#include <vector>

#include "OktBuild.h"
#include "ymio/Okt.h"

using namespace stym;
using test::buildOkt;
using test::OktCell;
using test::OktSpec;
using ymio::parseOkt;
using ymio::XmCell;
using ymio::XmModule;

namespace {

XmModule parsed(const std::vector<uint8_t>& b) {
    ymio::XmParseResult r = parseOkt(b);
    INFO(r.error);
    REQUIRE(r.module);
    return *r.module;
}

OktCell& at(OktSpec& m, int pattern, int row, int channel) {
    return m.patterns[size_t(pattern)].cells[size_t(row * m.channels() + channel)];
}

// Amiga channels 2 and 3 split: six channels, L R R R R L.
OktSpec song() {
    OktSpec m;
    m.split = {0, 1, 1, 0};
    m.speed = 3;
    m.order = {1, 0, 1};
    m.patterns = {test::oktPattern(16, 6), test::oktPattern(32, 6)};
    at(m, 1, 0, 0) = {13, 0};          // C-2: the XM C-4, sample 1
    at(m, 1, 1, 1) = {1, 1};           // C-1, sample 2, on a split channel: full volume
    at(m, 1, 2, 5) = {36, 0, 31, 32};  // B-3, volume 32
    at(m, 1, 3, 0) = {0, 0, 25, 2};    // position jump
    at(m, 1, 4, 0) = {0, 0, 28, 4};    // speed 4
    at(m, 1, 5, 0) = {0, 0, 27, 0};    // release
    at(m, 1, 6, 0) = {0, 0, 31, 0x43}; // slide down 3
    at(m, 1, 7, 0) = {0, 0, 31, 0x52}; // slide up 2
    at(m, 1, 8, 0) = {0, 0, 31, 0x61}; // fine slide down: kept by name
    at(m, 1, 9, 0) = {0, 0, 31, 0x71}; // fine slide up
    at(m, 1, 10, 0) = {0, 0, 10, 0x37};  // A: arpeggio
    at(m, 1, 11, 0) = {0, 0, 1, 0x05};   // 1: portamento
    at(m, 1, 12, 0) = {0, 0, 28, 0x00};  // S00: nothing
    m.samples.resize(4);
    m.samples[0] = {"lead", test::oktSquare(400), 48, 50, 100};
    m.samples[1] = {"bass", test::oktSquare(200), 20, 0, 1};
    m.samples[3] = {"pad", test::oktSquare(64), 64, 0, 0};  // sample 3 empty: no SBOD for it
    return m;
}

}  // namespace

TEST_CASE("OKT: split channels and Amiga pans, speed, orders, notes, effects in the XM shape", "[ymio][okt]") {
    const XmModule m = parsed(buildOkt(song()));
    CHECK(m.tracker == "Oktalyzer");
    CHECK(m.name.empty());
    CHECK(m.channels == 6);
    CHECK(m.channelPan == std::vector<uint8_t>{0, 255, 255, 255, 255, 0});
    CHECK_FALSE(m.samplePanning);
    CHECK(m.tempo == 3);
    CHECK(m.bpm == 125);
    CHECK_THAT(m.c4Hz, Catch::Matchers::WithinAbs(8287.14, 0.01));
    CHECK(m.order == std::vector<uint8_t>{1, 0, 1});
    REQUIRE(m.patterns.size() == 2);
    CHECK(m.patterns[0].rows == 16);
    CHECK(m.patterns[1].rows == 32);
    CHECK(m.cell(1, 0, 0) == XmCell{49, 1});
    CHECK(m.cell(1, 1, 1) == XmCell{37, 2, 0x50});      // split: volume 64
    CHECK(m.cell(1, 2, 5) == XmCell{72, 1, 0, 0xC, 32});
    CHECK(m.cell(1, 3, 0) == XmCell{0, 0, 0, 0xB, 2});
    CHECK(m.cell(1, 4, 0) == XmCell{0, 0, 0, 0xF, 4});
    CHECK(m.cell(1, 5, 0) == XmCell{ymio::kXmKeyOff});
    CHECK(m.cell(1, 6, 0) == XmCell{0, 0, 0, 0xA, 0x03});
    CHECK(m.cell(1, 7, 0) == XmCell{0, 0, 0, 0xA, 0x20});
    CHECK(m.cell(1, 12, 0) == XmCell{});
    const std::vector<std::pair<int, std::string>> named = {{8, "V6x"}, {9, "V7x"}, {10, "Axx"}, {11, "1xx"}};
    for (const auto& [row, name] : named) {
        CAPTURE(row);
        const XmCell& x = m.cell(1, row, 0);
        CHECK(x.effect == 0);
        REQUIRE(x.foreign >= 1);
        CHECK(m.foreignEffects[size_t(x.foreign - 1)] == name);
    }

    // Samples: 36 headers, the data chunks in order for those that have a length.
    REQUIRE(m.instruments.size() == 36);
    const ymio::XmSample& lead = m.instruments[0].samples.at(0);
    CHECK(m.instruments[0].name == "lead");
    CHECK(lead.data.size() == 400);
    CHECK(lead.data[0] == -64 * 256);
    CHECK(lead.volume == 48);
    CHECK(lead.loopType == 1);  // in words
    CHECK(lead.loopStart == 100);
    CHECK(lead.loopLength == 200);
    CHECK(m.instruments[1].samples.at(0).loopType == 0);  // a one-word repeat: no loop
    CHECK(m.instruments[1].samples.at(0).volume == 20);
    CHECK(m.instruments[2].samples.at(0).data.empty());
    CHECK(m.instruments[3].samples.at(0).data.size() == 64);
    CHECK(m.instruments[3].name == "pad");
    CHECK_FALSE(m.truncatedSamples);
}

TEST_CASE("OKT: last sample cut by the end of the file, refusals, garbage", "[ymio][okt]") {
    OktSpec s = song();
    s.cutAt = buildOkt(s).size() - 40;
    XmModule m = parsed(buildOkt(s));
    CHECK(m.truncatedSamples);
    CHECK(m.instruments[3].samples.at(0).data.size() == 24);

    CHECK(parseOkt(std::vector<uint8_t>(64, 0)).error == "Not an OKT module.");
    CHECK(parseOkt(std::vector<uint8_t>{'O', 'K', 'T', 'A'}).error == "Not an OKT module.");
    const std::vector<uint8_t> head = {'O', 'K', 'T', 'A', 'S', 'O', 'N', 'G'};
    CHECK(parseOkt(head).error == "Corrupt OKT module.");
    s = song();
    s.patterns.clear();
    s.order.clear();
    CHECK(parseOkt(buildOkt(s)).error == "Corrupt OKT module.");
    // A pattern chunk smaller than its rows.
    std::vector<uint8_t> b = buildOkt(song());
    s = song();
    s.patterns[1].cells.resize(10);
    CHECK(parseOkt(buildOkt(s)).error == "Corrupt OKT module.");

    // Garbage never reads beyond the file.
    std::mt19937 rng(554);
    for (int i = 0; i < 300; ++i) {
        b = buildOkt(song());
        for (size_t k = 0; k < 40; ++k) b[8 + rng() % (b.size() - 8)] = uint8_t(rng());
        b.resize(std::max<size_t>(rng() % b.size(), 8));
        const ymio::XmParseResult r = parseOkt(b);
        if (!r.module) continue;
        for (uint8_t o : r.module->order) CHECK(o < r.module->patterns.size());
        for (const ymio::XmPattern& p : r.module->patterns)
            CHECK(p.cells.size() == size_t(p.rows * r.module->channels));
    }
}
