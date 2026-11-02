// STYM2149 — ymio: reading S3M modules: header, PCM channels and their pans (ST3's, mono, pan table), orders
// (markers, end), notes and cuts, volume column, commands in the XM shape or kept by name, samples (unsigned or signed,
// 8 or 16-bit, stereo mixed, C2Spd as relative note and fine tune, loops, ADPCM), AdLib left out, cut or refused files.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>
#include <vector>

#include "S3mBuild.h"
#include "ymio/S3m.h"

using namespace stym;
using test::buildS3m;
using test::S3mCell;
using test::s3mNote;
using test::S3mSpec;
using ymio::parseS3m;
using ymio::XmCell;
using ymio::XmModule;

namespace {

XmModule parsed(const std::vector<uint8_t>& b) {
    ymio::XmParseResult r = parseS3m(b);
    INFO(r.error);
    REQUIRE(r.module);
    return *r.module;
}

S3mSpec song() {
    S3mSpec m;
    m.title = "scream";
    m.channels = {0, 8, 16, 0x81, 9};  // L1, R1, AdLib A1, a disabled L2, R2: three PCM channels
    m.order = {1, 254, 0, 255, 1};
    m.speed = 4;
    m.tempo = 140;
    m.patterns = {{s3mNote(0, 0, 3, 0, 1)},
                  {s3mNote(0, 0, 4, 0, 1),
                   s3mNote(1, 1, 5, 2, 1),
                   {2, 4, 254, 0},
                   s3mNote(3, 2, 4, 0, 2),  // the AdLib channel: left out
                   s3mNote(3, 3, 4, 0, 1),  // the disabled channel: left out
                   {4, 0, -1, 0, 32},
                   {5, 1, -1, 0, 160},
                   {6, 4, -1, 0, -1, 8, 0x44}}};
    m.samples.resize(2);
    m.samples[0] = {"lead", test::s3mSquare(400), 48, 16726, true, 100, 300};
    m.samples[1].name = "organ";
    m.samples[1].type = 2;  // AdLib
    return m;
}

}  // namespace

TEST_CASE("S3M: header, PCM channels and ST3 pans, orders, notes, volume column, AdLib left out", "[ymio][s3m]") {
    const XmModule m = parsed(buildS3m(song()));
    CHECK(m.name == "scream");
    CHECK(m.tracker == "Scream Tracker 3");
    CHECK(m.channels == 3);
    CHECK(m.channelPan == std::vector<uint8_t>{51, 204, 204});  // 3 and 12 out of 15
    CHECK_FALSE(m.samplePanning);
    CHECK(m.order == std::vector<uint8_t>{1, 0});  // 254 skipped, 255 the end
    CHECK(m.tempo == 4);
    CHECK(m.bpm == 140);
    CHECK(m.c4Hz == 8363);
    REQUIRE(m.patterns.size() == 2);
    CHECK(m.patterns[1].rows == 64);
    CHECK(m.cell(0, 0, 0).note == 37);           // C-3
    CHECK(m.cell(1, 0, 0) == XmCell{49, 1});     // C-4: the XM C-4
    CHECK(m.cell(1, 1, 1) == XmCell{63, 1});     // D-5
    CHECK(m.cell(1, 2, 2).note == ymio::kXmKeyOff);  // ^^: a cut
    for (int c = 0; c < 3; ++c) CHECK(m.cell(1, 3, c) == XmCell{});
    CHECK(m.cell(1, 4, 0).volume == 0x10 + 32);
    CHECK(m.cell(1, 5, 1).volume == 0xC7);  // 160: pan 32 of 64, the XM $Cx
    CHECK(m.cell(1, 6, 2).effect == 0);
    REQUIRE(m.cell(1, 6, 2).foreign == 1);
    CHECK(m.foreignEffects == std::vector<std::string>{"Hxx"});

    REQUIRE(m.instruments.size() == 2);
    const ymio::XmSample& lead = m.instruments[0].samples.at(0);
    CHECK(m.instruments[0].name == "lead");
    CHECK(lead.data.size() == 400);
    CHECK(lead.data[0] == -64 * 256);
    CHECK(lead.data[8] == 64 * 256);
    CHECK(lead.volume == 48);
    CHECK(lead.relativeNote == 12);  // 16726 Hz: an octave above 8363
    CHECK(lead.finetune == 0);
    CHECK(lead.loopType == 1);
    CHECK(lead.loopStart == 100);
    CHECK(lead.loopLength == 200);
    CHECK(m.instruments[1].name == "organ");
    CHECK(m.instruments[1].samples.empty());
    CHECK(m.notes == std::vector<std::string>{"AdLib channels (no OPL chip): 1 left out.",
                                               "AdLib instruments (no OPL chip): 1 left out."});
    CHECK_FALSE(m.truncatedSamples);
}

TEST_CASE("S3M: commands in the XM shape, the others kept by their S3M name", "[ymio][s3m]") {
    struct Case {
        char letter;
        int info;
        XmCell want;
        const char* foreign;
    };
    const std::vector<Case> cases = {
        {'A', 0x06, {0, 0, 0, 0xF, 0x06}, nullptr},  {'A', 0x00, {}, nullptr},
        {'A', 0x40, {0, 0, 0, 0xF, 0x1F}, nullptr},  {'B', 0x02, {0, 0, 0, 0xB, 0x02}, nullptr},
        {'C', 0x12, {0, 0, 0, 0xD, 0x12}, nullptr},  {'D', 0x05, {0, 0, 0, 0xA, 0x05}, nullptr},
        {'D', 0x50, {0, 0, 0, 0xA, 0x50}, nullptr},  {'D', 0x00, {0, 0, 0, 0xA, 0x00}, nullptr},
        {'D', 0x0F, {0, 0, 0, 0xA, 0x0F}, nullptr},  {'D', 0x35, {0, 0, 0, 0xA, 0x05}, nullptr},
        {'D', 0x3F, {}, "DxF"},                      {'D', 0xF3, {}, "DFy"},
        {'O', 0x20, {0, 0, 0, 0x9, 0x20}, nullptr},  {'T', 0x80, {0, 0, 0, 0xF, 0x80}, nullptr},
        {'T', 0x05, {}, "Txx"},                      {'X', 0x40, {0, 0, 0, 0x8, 0x80}, nullptr},
        {'X', 0xA4, {}, "Xxx"},                      {'S', 0x8F, {0, 0, 0, 0x8, 0xFF}, nullptr},
        {'S', 0xC3, {0, 0, 0, 0xE, 0xC3}, nullptr},  {'S', 0xD2, {0, 0, 0, 0xE, 0xD2}, nullptr},
        {'S', 0x2F, {0, 0, 0, 0xE, 0x57}, nullptr},  {'S', 0x13, {}, "S1x"},
        {'S', 0xB2, {}, "SBx"},                      {'G', 0x10, {}, "Gxx"},
        {'J', 0x37, {}, "Jxx"},                      {'Q', 0x03, {}, "Qxx"}};
    S3mSpec s;
    s.channels = {0};
    s.patterns = {{}};
    for (size_t i = 0; i < cases.size(); ++i)
        s.patterns[0].push_back({int(i), 0, -1, 0, -1, cases[i].letter - 'A' + 1, cases[i].info});
    const XmModule m = parsed(buildS3m(s));
    for (size_t i = 0; i < cases.size(); ++i) {
        CAPTURE(cases[i].letter, cases[i].info);
        XmCell x = m.cell(0, int(i), 0);
        if (cases[i].foreign) {
            REQUIRE(x.foreign >= 1);
            CHECK(m.foreignEffects[size_t(x.foreign - 1)] == cases[i].foreign);
            x.foreign = 0;
        }
        CHECK(x == cases[i].want);
    }
}

TEST_CASE("S3M: sample formats, C2Spd, loops, mono module, pan table", "[ymio][s3m]") {
    S3mSpec s;
    s.channels = {0, 8};
    s.signedData = true;
    s.stereo = false;
    s.patterns = {{s3mNote(0, 0, 4, 0, 1)}};
    s.samples.resize(4);
    s.samples[0] = {"signed 16", {1000, -2000, 30000}, 64, 8484};
    s.samples[0].sixteenBit = true;
    s.samples[1] = {"stereo", {64, 64, 32, 32}, 64, 0};  // left 64, right 32
    s.samples[1].stereo = true;
    s.samples[2] = {"loop", test::s3mSquare(100), 64, 500, true, 90, 400};  // the end past the sample
    s.samples[3] = {"packed", test::s3mSquare(16), 64, 70000};
    s.samples[3].pack = 1;
    XmModule m = parsed(buildS3m(s));
    CHECK(m.channelPan == std::vector<uint8_t>{128, 128});  // mono
    const ymio::XmSample& a = m.instruments[0].samples.at(0);
    CHECK(a.sixteenBit);
    CHECK(a.data == std::vector<int16_t>{1000, -2000, 30000});
    CHECK(a.relativeNote == 0);  // 8484 Hz: 32/128 of a semitone above 8363
    CHECK(a.finetune == 32);
    const ymio::XmSample& b = m.instruments[1].samples.at(0);
    CHECK(b.data == std::vector<int16_t>{48 * 256, 48 * 256});  // both halves mixed
    CHECK(b.relativeNote == 0);  // C2Spd 0: 8363
    CHECK(b.finetune == 0);
    const ymio::XmSample& c = m.instruments[2].samples.at(0);
    CHECK(c.loopType == 1);
    CHECK(c.loopStart == 90);
    CHECK(c.loopLength == 10);
    CHECK(c.relativeNote == -36);  // below 1024 Hz: 1024
    CHECK(c.finetune == -46);
    CHECK(m.instruments[3].samples.at(0).adpcm);
    CHECK(m.instruments[3].samples.at(0).data.empty());
    CHECK(m.instruments[3].samples.at(0).relativeNote == 36);  // above 65535 Hz: 65535
    CHECK(m.instruments[3].samples.at(0).finetune == -46);

    // Unsigned data (the default of ST3), 16-bit too; the pan table of the stereo module.
    s.signedData = false;
    s.stereo = true;
    s.pans = {0x20 | 0, 0x20 | 15};
    m = parsed(buildS3m(s));
    CHECK(m.instruments[0].samples.at(0).data == std::vector<int16_t>{1000, -2000, 30000});
    CHECK(m.instruments[1].samples.at(0).data == std::vector<int16_t>{48 * 256, 48 * 256});
    CHECK(m.channelPan == std::vector<uint8_t>{0, 255});
    s.pans = {0x20 | 7, 0x0C};  // the second without bit 5: ST3's own pan
    m = parsed(buildS3m(s));
    CHECK(m.channelPan == std::vector<uint8_t>{119, 204});
}

TEST_CASE("S3M: samples cut by the end of the file, refusals, garbage", "[ymio][s3m]") {
    S3mSpec s = song();
    const size_t whole = buildS3m(s).size();
    s.cutAt = whole - 150;
    XmModule m = parsed(buildS3m(s));
    CHECK(m.truncatedSamples);
    CHECK(m.instruments[0].samples.at(0).data.size() == 250);
    CHECK(m.instruments[0].samples.at(0).loopLength == 150);

    CHECK(parseS3m(std::vector<uint8_t>(200, 0)).error == "Not an S3M module.");
    CHECK(parseS3m(std::vector<uint8_t>(10, 'S')).error == "Not an S3M module.");
    std::vector<uint8_t> b = buildS3m(song());
    b.resize(0x62);  // the parapointers cut
    CHECK(parseS3m(b).error == "Corrupt S3M module.");
    S3mSpec adlib;
    adlib.channels = {16, 17};
    CHECK(parseS3m(buildS3m(adlib)).error == "Nothing to import: no notes with a sample.");
    // A pattern cut before its 64 rows.
    S3mSpec cut;
    cut.channels = {0};
    cut.patterns = {{s3mNote(0, 0, 4, 0, 1)}};
    cut.cutAt = buildS3m(cut).size() - 10;
    CHECK(parseS3m(buildS3m(cut)).error == "Corrupt S3M module.");

    // Garbage after the signature never reads beyond the file.
    std::mt19937 rng(553);
    for (int i = 0; i < 300; ++i) {
        b = buildS3m(song());
        for (size_t k = 0; k < 40; ++k) b[rng() % b.size()] = uint8_t(rng());
        b[44] = 'S';
        b[45] = 'C';
        b[46] = 'R';
        b[47] = 'M';
        b.resize(std::max<size_t>(rng() % b.size(), 0x60));
        const ymio::XmParseResult r = parseS3m(b);
        if (!r.module) continue;
        for (uint8_t o : r.module->order) CHECK(o < r.module->patterns.size());
        for (const ymio::XmPattern& p : r.module->patterns)
            CHECK(p.cells.size() == size_t(p.rows * r.module->channels));
    }
}
