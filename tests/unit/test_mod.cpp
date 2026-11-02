// STYM2149 — ymio: reading MOD modules: signatures and channels, notes from the Amiga periods, sample numbers,
// effects in the XM shape (E5x, E8x), samples and loops (words, bytes of old modules, no loop), Amiga pan and PAL rate,
// FLT8 and 15-sample Soundtracker layouts, cut or refused files.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <random>
#include <vector>

#include "ModBuild.h"
#include "ymio/Mod.h"

using namespace stym;
using test::buildMod;
using test::ModCell;
using test::ModSpec;
using ymio::parseMod;
using ymio::XmModule;

namespace {

ModCell& at(ModSpec& m, int pattern, int row, int channel) {
    return m.patterns[size_t(pattern)][size_t(row * m.channels + channel)];
}

ModSpec song() {
    ModSpec m;
    m.title = "mod song";
    m.order = {1, 0, 1};
    m.restart = 1;
    m.patterns = {test::modPattern(4), test::modPattern(4)};
    at(m, 1, 0, 0) = {428, 1, 0, 0};       // C-2: XM C-4
    at(m, 1, 1, 1) = {856, 17, 0xC, 0x20};  // C-1, sample 17 (high nibble), volume 32
    at(m, 1, 2, 2) = {113, 2, 0xE, 0x5F};   // B-3, fine tune -1
    at(m, 1, 3, 3) = {0, 0, 0xE, 0x8C};     // coarse pan 12
    at(m, 1, 4, 0) = {214, 0, 0xF, 0x03};   // C-3, speed 3
    at(m, 0, 63, 1) = {0, 0, 0xB, 0x02};
    m.samples.resize(17);
    m.samples[0] = {"lead", test::modSquare(400), -2, 48, 50, 100};
    m.samples[1] = {"bass", test::modSquare(200), 7, 70, 0, 1};
    m.samples[16] = {"pad", test::modSquare(64), 0, 64, 40, 8};  // repeat start in bytes (old module)
    return m;
}

XmModule parsed(const std::vector<uint8_t>& b) {
    ymio::XmParseResult r = parseMod(b);
    INFO(r.error);
    REQUIRE(r.module);
    return *r.module;
}

}  // namespace

TEST_CASE("MOD: header, order table, notes from periods, samples, effects in the XM shape", "[ymio][mod]") {
    const XmModule m = parsed(buildMod(song()));
    CHECK(m.name == "mod song");
    CHECK(m.tracker == "M.K.");
    CHECK(m.channels == 4);
    CHECK(m.order == std::vector<uint8_t>{1, 0, 1});
    CHECK(m.restart == 1);
    CHECK(m.tempo == 6);
    CHECK(m.bpm == 125);
    REQUIRE(m.patterns.size() == 2);
    CHECK(m.patterns[0].rows == 64);
    // Notes: period 428 (C-2) is the XM C-4 (49), an octave per halving of the period.
    CHECK(m.cell(1, 0, 0) == ymio::XmCell{49, 1, 0, 0, 0});
    CHECK(m.cell(1, 1, 1) == ymio::XmCell{37, 17, 0, 0xC, 0x20});
    // E5F: fine tune -1 as a signed nibble, 7 in the XM count (8 = 0).
    CHECK(m.cell(1, 2, 2) == ymio::XmCell{72, 2, 0, 0xE, 0x57});
    // E8C: pan 12 × 17 as an XM 8xx.
    CHECK(m.cell(1, 3, 3) == ymio::XmCell{0, 0, 0, 0x8, 204});
    CHECK(m.cell(1, 4, 0) == ymio::XmCell{61, 0, 0, 0xF, 0x03});
    CHECK(m.cell(0, 63, 1) == ymio::XmCell{0, 0, 0, 0xB, 0x02});
    // The Amiga: L R R L, the PAL rate of period 428, no sample pan.
    CHECK(m.channelPan == std::vector<uint8_t>{0, 255, 255, 0});
    CHECK_THAT(m.c4Hz, Catch::Matchers::WithinAbs(8287.14, 0.01));
    CHECK_FALSE(m.samplePanning);

    // One instrument per sample (31), each with its sample.
    REQUIRE(m.instruments.size() == 31);
    const ymio::XmSample& lead = m.instruments[0].samples.at(0);
    CHECK(m.instruments[0].name == "lead");
    CHECK(lead.data.size() == 400);
    CHECK(lead.data[0] == -64 * 256);
    CHECK(lead.data[8] == 64 * 256);
    CHECK(lead.finetune == -32);
    CHECK(lead.volume == 48);
    CHECK(lead.panning == 128);
    CHECK(lead.loopType == 1);  // repeat in words
    CHECK(lead.loopStart == 100);
    CHECK(lead.loopLength == 200);
    const ymio::XmSample& bass = m.instruments[1].samples.at(0);
    CHECK(bass.finetune == 112);
    CHECK(bass.volume == 64);   // clamped
    CHECK(bass.loopType == 0);  // one-word repeat: no loop
    const ymio::XmSample& pad = m.instruments[16].samples.at(0);
    CHECK(pad.loopType == 1);  // in words it would end past the sample: bytes
    CHECK(pad.loopStart == 40);
    CHECK(pad.loopLength == 16);
    CHECK(m.instruments[5].samples.at(0).data.empty());
    CHECK_FALSE(m.truncatedSamples);
}

TEST_CASE("MOD: channels named by the signature", "[ymio][mod]") {
    for (const auto& [sig, channels] : std::vector<std::pair<std::string, int>>{
             {"M!K!", 4}, {"FLT4", 4}, {"6CHN", 6}, {"8CHN", 8}, {"CD81", 8}, {"OKTA", 8}, {"12CH", 12},
             {"16CN", 16}, {"32CH", 32}, {"TDZ3", 3}, {"2CHN", 2}}) {
        CAPTURE(sig);
        ModSpec s;
        s.signature = sig;
        s.channels = channels;
        s.patterns = {test::modPattern(channels)};
        at(s, 0, 5, channels - 1) = {320, 1, 0, 0};
        s.samples = {{"x", test::modSquare(16)}};
        const XmModule m = parsed(buildMod(s));
        CHECK(m.channels == channels);
        CHECK(m.cell(0, 5, channels - 1).note == 54);  // period 320: D#-2 of ProTracker, F-4 of the XM
        CHECK(m.channelPan.size() == size_t(channels));
        CHECK(m.channelPan.back() == (channels % 4 == 1 || channels % 4 == 0 ? 0 : 255));
    }
}

TEST_CASE("MOD: FLT8 pairs of 4-channel patterns, 15-sample Soundtracker modules", "[ymio][mod]") {
    ModSpec s;
    s.signature = "FLT8";
    s.channels = 8;
    s.order = {2, 0, 2};
    s.patterns = {test::modPattern(8), test::modPattern(8)};
    at(s, 0, 1, 2) = {428, 1, 0, 0};
    at(s, 0, 2, 6) = {214, 1, 0, 0};
    at(s, 1, 3, 7) = {856, 1, 0, 0};
    s.samples = {{"x", test::modSquare(16)}};
    XmModule m = parsed(buildMod(s));
    CHECK(m.channels == 8);
    CHECK(m.order == std::vector<uint8_t>{1, 0, 1});
    REQUIRE(m.patterns.size() == 2);
    CHECK(m.cell(0, 1, 2).note == 49);
    CHECK(m.cell(0, 2, 6).note == 61);
    CHECK(m.cell(1, 3, 7).note == 37);

    ModSpec st;
    st.signature.clear();
    st.title = "old one";
    st.order = {0, 0};
    st.patterns = {test::modPattern(4)};
    at(st, 0, 0, 1) = {428, 3, 0xC, 0x10};
    st.samples.resize(3);
    st.samples[2] = {"snare", test::modSquare(100), 0, 40, 0, 1};
    m = parsed(buildMod(st));
    CHECK(m.tracker == "Soundtracker");
    CHECK(m.name == "old one");
    CHECK(m.instruments.size() == 15);
    CHECK(m.order == std::vector<uint8_t>{0, 0});
    CHECK(m.restart == 0);
    CHECK(m.cell(0, 0, 1) == ymio::XmCell{49, 3, 0, 0xC, 0x10});
    CHECK(m.instruments[2].samples.at(0).data.size() == 100);
}

TEST_CASE("MOD: last sample cut by the end of the file, refusals", "[ymio][mod]") {
    ModSpec s = song();
    const size_t whole = buildMod(s).size();
    s.cutAt = whole - 40;  // the pad sample (64 bytes) loses 40
    XmModule m = parsed(buildMod(s));
    CHECK(m.truncatedSamples);
    CHECK(m.instruments[16].samples.at(0).data.size() == 24);
    CHECK(m.instruments[16].samples.at(0).loopLength == 0);  // its loop starts at 40: gone
    CHECK(m.instruments[16].samples.at(0).loopType == 0);

    CHECK(parseMod(std::vector<uint8_t>(2000, 0)).error == "Not a MOD module.");  // no signature, no song length
    CHECK(parseMod(std::vector<uint8_t>(10, 'M')).error == "Not a MOD module.");
    s = song();
    s.cutAt = 1084 + 1024 + 100;  // the second pattern cut
    CHECK(parseMod(buildMod(s)).error == "Corrupt MOD module.");
    s = song();
    s.order = {};
    CHECK(parseMod(buildMod(s)).error == "Corrupt MOD module.");
    s = song();
    s.order = {0, 130};
    CHECK(parseMod(buildMod(s)).error == "Corrupt MOD module.");

    // Garbage after a signature never reads beyond the file.
    std::mt19937 rng(511);
    for (int i = 0; i < 300; ++i) {
        std::vector<uint8_t> b = buildMod(song());
        const size_t n = rng() % b.size();
        for (size_t k = 0; k < 40; ++k) b[rng() % b.size()] = uint8_t(rng());
        b.resize(std::max<size_t>(n, 1084));
        const ymio::XmParseResult r = parseMod(b);
        if (!r.module) continue;
        for (uint8_t o : r.module->order) CHECK(o < r.module->patterns.size());
    }
}
