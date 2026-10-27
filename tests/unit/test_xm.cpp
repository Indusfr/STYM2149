// STYM2149 — ymio: reading XM 1.04 modules: header, order table, packed or full cells, 8 and
// 16-bit delta samples, bounded loops, cut or refused files.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <random>
#include <vector>

#include "XmBuild.h"
#include "ymio/Xm.h"

using namespace stym;
using ymio::XmCell;
using ymio::XmModule;
using ymio::XmSample;

namespace {

XmSample sample8(std::vector<int> steps, uint8_t loopType, uint32_t loopStart, uint32_t loopLength) {
    XmSample s;
    s.name = "kick";
    s.volume = 48;
    s.finetune = -16;
    s.panning = 96;
    s.relativeNote = 12;
    s.loopType = loopType;
    s.loopStart = loopStart;
    s.loopLength = loopLength;
    for (int v : steps) s.data.push_back(int16_t(v * 256));
    return s;
}

XmModule module() {
    XmModule m;
    m.name = "test song";
    m.tracker = "FastTracker v2.00";
    m.channels = 2;
    m.restart = 1;
    m.linear = true;
    m.tempo = 5;
    m.bpm = 140;
    m.order = {1, 0, 1};
    ymio::XmPattern a;
    a.rows = 4;
    a.cells.assign(8, XmCell{});
    a.cells[0] = {49, 1, 0x50, 0, 0};       // C-4, instrument 1, volume 64
    a.cells[3] = {0, 0, 0, 0x0F, 0x03};     // F03 alone
    a.cells[4] = {61, 2, 0x30, 0x00, 0x37};  // full cell: five bytes
    a.cells[6] = {ymio::kXmKeyOff, 0, 0, 0, 0};
    ymio::XmPattern b;  // empty pattern: no data
    b.rows = 3;
    b.cells.assign(6, XmCell{});
    m.patterns = {a, b};

    ymio::XmInstrument i1;
    i1.name = "drums";
    for (size_t k = 0; k < 96; ++k) i1.keymap[k] = k < 48 ? 0 : 1;
    i1.samples.push_back(sample8({0, 10, -20, 127, -128, 5, 6, 7}, 1, 2, 4));
    XmSample s16;
    s16.name = "pad";
    s16.sixteenBit = true;
    s16.loopType = 2;
    s16.loopStart = 1;
    s16.loopLength = 3;
    s16.data = {0, 1000, -32768, 32767, -1, 12345};
    i1.samples.push_back(s16);
    ymio::XmInstrument i2;  // instrument without a sample
    i2.name = "empty";
    m.instruments = {i1, i2};
    return m;
}

void checkSample(const XmSample& got, const XmSample& want) {
    CHECK(got.name == want.name);
    CHECK(got.volume == want.volume);
    CHECK(got.finetune == want.finetune);
    CHECK(got.panning == want.panning);
    CHECK(got.relativeNote == want.relativeNote);
    CHECK(got.loopType == want.loopType);
    CHECK(got.loopStart == want.loopStart);
    CHECK(got.loopLength == want.loopLength);
    CHECK(got.sixteenBit == want.sixteenBit);
    CHECK_FALSE(got.adpcm);
    CHECK(got.data == want.data);
}

}  // namespace

TEST_CASE("XM: reading back a module (header, order, cells, 8 and 16-bit samples)", "[ymio][xm]") {
    const XmModule want = module();
    const std::vector<uint8_t> file = test::buildXm(want);
    const ymio::XmParseResult r = ymio::parseXm(file);
    REQUIRE(r.error.empty());
    REQUIRE(r.module);
    const XmModule& m = *r.module;
    CHECK(m.name == "test song");
    CHECK(m.tracker == "FastTracker v2.00");
    CHECK(m.channels == 2);
    CHECK(m.restart == 1);
    CHECK(m.linear);
    CHECK(m.tempo == 5);
    CHECK(m.bpm == 140);
    CHECK(m.order == std::vector<uint8_t>{1, 0, 1});
    REQUIRE(m.patterns.size() == 2);
    CHECK(m.patterns[0].rows == 4);
    CHECK(m.patterns[0].cells == want.patterns[0].cells);
    CHECK(m.cell(0, 2, 0) == XmCell{61, 2, 0x30, 0x00, 0x37});
    CHECK(m.cell(0, 1, 1) == XmCell{0, 0, 0, 0x0F, 0x03});
    CHECK(m.patterns[1].rows == 3);
    CHECK(m.patterns[1].cells == std::vector<XmCell>(6));
    REQUIRE(m.instruments.size() == 2);
    CHECK(m.instruments[0].name == "drums");
    CHECK(m.instruments[0].keymap == want.instruments[0].keymap);
    REQUIRE(m.instruments[0].samples.size() == 2);
    checkSample(m.instruments[0].samples[0], want.instruments[0].samples[0]);
    checkSample(m.instruments[0].samples[1], want.instruments[0].samples[1]);
    CHECK(m.instruments[1].name == "empty");
    CHECK(m.instruments[1].samples.empty());
    CHECK_FALSE(m.truncatedSamples);
}

TEST_CASE("XM: Amiga table, order pointing to a missing pattern, loop outside the data", "[ymio][xm]") {
    XmModule m = module();
    m.linear = false;
    m.order = {0, 7, 1};
    m.instruments[0].samples[0] = sample8({1, 2, 3, 4}, 1, 9, 4);  // loop start after the end
    m.instruments[0].samples[1].loopStart = 4;
    m.instruments[0].samples[1].loopLength = 10;  // length beyond the end
    const ymio::XmParseResult r = ymio::parseXm(test::buildXm(m));
    REQUIRE(r.module);
    CHECK_FALSE(r.module->linear);
    CHECK(r.module->order == std::vector<uint8_t>{0, 0, 1});
    const XmSample& a = r.module->instruments[0].samples[0];
    CHECK(a.loopStart == 4);
    CHECK(a.loopLength == 0);
    CHECK(a.loopType == 0);
    const XmSample& b = r.module->instruments[0].samples[1];
    CHECK(b.loopStart == 4);
    CHECK(b.loopLength == 2);
    CHECK(b.loopType == 2);
}

TEST_CASE("XM: last sample cut by the end of the file, kept and flagged", "[ymio][xm]") {
    XmModule m = module();
    m.instruments.pop_back();  // the 16-bit sample becomes the last one in the file
    std::vector<uint8_t> file = test::buildXm(m);
    file.resize(file.size() - 5);  // 16 bits: 6 frames written, 12 bytes; 7 remain, i.e. 3 whole frames
    const ymio::XmParseResult r = ymio::parseXm(file);
    REQUIRE(r.module);
    CHECK(r.module->truncatedSamples);
    const XmSample& s = r.module->instruments[0].samples[1];
    CHECK(s.data == std::vector<int16_t>{0, 1000, -32768});
    CHECK(s.loopStart == 1);
    CHECK(s.loopLength == 2);
}

TEST_CASE("XM: refused files (signature, version, header, pattern, instrument)", "[ymio][xm]") {
    const std::vector<uint8_t> good = test::buildXm(module());
    CHECK(ymio::parseXm({}).error == "Not an XM module.");
    std::vector<uint8_t> f = good;
    f[0] = 'e';
    CHECK(ymio::parseXm(f).error == "Not an XM module.");

    XmModule old = module();
    old.version = 0x0103;
    const ymio::XmParseResult v = ymio::parseXm(test::buildXm(old));
    CHECK_FALSE(v.module);
    CHECK(v.error == "Unsupported XM version (1.04 only).");

    f = good;
    f[68] = 0;  // no channels
    CHECK(ymio::parseXm(f).error == "Corrupt XM header.");
    f = good;
    f[68] = 65;
    CHECK(ymio::parseXm(f).error == "Corrupt XM header.");

    // First pattern: header at 336 (60 + 276), packed data from 345; file cut inside it.
    f.assign(good.begin(), good.begin() + 350);
    CHECK(ymio::parseXm(f).error == "Corrupt XM pattern.");
    f = good;
    f[336 + 5] = 0;  // zero rows
    f[336 + 6] = 0;
    CHECK(ymio::parseXm(f).error == "Corrupt XM pattern.");
    f = good;
    f[336 + 7] = 2;  // packed data too short for 4 × 2 cells
    f[336 + 8] = 0;
    CHECK(ymio::parseXm(f).error == "Corrupt XM pattern.");

    // Instruments: after the two patterns; the first announces 17 samples.
    size_t at = 336 + 9 + size_t(good[336 + 7] | good[336 + 8] << 8);
    at += 9;  // empty pattern
    REQUIRE((good[at] | good[at + 1] << 8) == 263);
    f = good;
    f[at + 27] = 17;
    CHECK(ymio::parseXm(f).error == "Corrupt XM instrument.");
    f.assign(good.begin(), good.begin() + long(at + 200));
    CHECK(ymio::parseXm(f).error == "Corrupt XM instrument.");
}

TEST_CASE("XM: cut or damaged files read without going past their bounds", "[ymio][xm][fuzz]") {
    // Cuts at every length and random bytes: never a read outside the buffer (sanitizers in CI); a module read stays
    // consistent (cells rows × channels, order pointing to present patterns, loops within the data).
    const std::vector<uint8_t> good = test::buildXm(module());
    const auto coherent = [](const XmModule& m) {
        for (uint8_t o : m.order) CHECK(o < m.patterns.size());
        for (const ymio::XmPattern& p : m.patterns) CHECK(p.cells.size() == size_t(p.rows * m.channels));
        for (const ymio::XmInstrument& i : m.instruments)
            for (const XmSample& s : i.samples) CHECK(s.loopStart + s.loopLength <= s.data.size());
    };
    for (size_t cut = 0; cut <= good.size(); ++cut) {
        const ymio::XmParseResult r = ymio::parseXm(std::span(good.data(), cut));
        CHECK(r.module.has_value() != !r.error.empty());
        if (r.module) coherent(*r.module);
    }
    std::mt19937 rng(2149);
    for (int round = 0; round < 3000; ++round) {
        std::vector<uint8_t> b = good;
        const int flips = 1 + int(rng() % 8);
        for (int k = 0; k < flips; ++k) b[60 + rng() % (b.size() - 60)] = uint8_t(rng());
        if (rng() % 4 == 0) b.resize(60 + rng() % (b.size() - 60));
        const ymio::XmParseResult r = ymio::parseXm(b);
        if (r.module) coherent(*r.module);
    }
}
