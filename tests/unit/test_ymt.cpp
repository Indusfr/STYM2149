// STYM2149 — ymio: gzip members and YM-Tracker songs (.ymt): stored, fixed and dynamic deflate blocks,
// checksums, the song layout read from the bytes, packed cells, digidrum banks, round trips, refused files.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "ymio/Gzip.h"
#include "ymio/YmTracker.h"

using namespace stym;
using ymio::YmtCell;
using ymio::YmtInstrument;
using ymio::YmtSong;

namespace {

std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) out.push_back(uint8_t(std::stoi(std::string(s + i, 2), nullptr, 16)));
    return out;
}

std::vector<uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

// Text of the dynamic-block vector: the squares modulo 97 of 0 to 299, spaced.
std::string squares() {
    std::string s;
    for (int i = 0; i < 300; ++i) s += (i ? " " : "") + std::to_string(i * i % 97);
    return s;
}

YmtSong song() {
    YmtSong s;
    s.title = "Test · song";
    s.bpm = 140;
    s.speed = 4;
    s.restart = 1;
    s.channelNames = {"Ch 1", "Ch 2", "Ch 3", "Ch 4"};
    s.order = {0, 1, 0};
    s.patterns.resize(2);
    s.patterns[0].rows = 3;
    s.patterns[0].cells.assign(12, YmtCell{});
    s.patterns[0].cells[0] = {37, 1, 0x50, '0', 0x47};
    s.patterns[0].cells[2] = {ymio::kYmtNoteOff, 0, 0, 0, 0};
    s.patterns[0].cells[5] = {0, 0, 0, 'X', 0};  // effect without its parameter
    s.patterns[0].cells[11] = {13, 2, 0, 'Z', 3};
    s.patterns[1].rows = 1;
    s.patterns[1].cells = {{0, 0, 0, 'Q', 0x24}, {0, 0, 0x30, 0, 0}, {0, 0, 0, 'F', 0x7D}, {96, 16, 0x10, 'Y', 4}};
    s.instruments.resize(3);
    s.instruments[0].name = "--";
    s.instruments[1].name = "Kick";
    s.instruments[1].sustain = 0;
    s.instruments[1].pitch = {0, 150, 400, 900, 1800, -3000};
    s.instruments[1].arp = {0, 4, 7, -12};
    s.instruments[1].unknown19 = {1, 2, 3, 4, 5};
    s.instruments[2].name = "Buzz";
    s.instruments[2].hardEnv = true;
    s.instruments[2].syncBuzzer = true;
    s.instruments[2].envPeriod = 0x1234;
    s.instruments[2].drums = {{"Odd", 8000, {1, 2, 3}}, {"Even", 30300, {15, 0, 7, 8}}, {"", 11025, {}}};
    return s;
}

}  // namespace

TEST_CASE("gzip: stored, fixed and dynamic deflate blocks are read", "[ymio][ymt]") {
    // Fixed Huffman block: "YM2149 ST STE " three times, as zlib compresses it.
    const auto fixed = ymio::inflate(hex("8bf4353234b154080e01225785483c3c00"), 1000);
    REQUIRE(fixed);
    CHECK(*fixed == bytes("YM2149 ST STE YM2149 ST STE YM2149 ST STE "));
    // Dynamic Huffman block.
    const std::vector<uint8_t> dyn = hex(
        "ed90db8dc5200c055b9912621b0cf4dfd80e94b0df578a5012cefb23181ca2c94935e3d0831d1439188b95241574724414eb2392"
        "591cd17215f07d303f363dc9c5decc41265b35a52611eca6656c2a09919c624fd66179d67bdea73fbdea0b122a419ae47842ca29"
        "aab406da68a6e5bee646b841e285ea1bd0988635b2c1fb95c85bc75256ab5bd2aa16ae57de098e5b04df6f93df26ffdee40f");
    size_t used = 0;
    const auto text = ymio::inflate(dyn, 10000, &used);
    REQUIRE(text);
    CHECK(*text == bytes(squares()));
    CHECK(used == dyn.size());
    // Bounded: the same stream refused below its length; cut streams refused.
    CHECK_FALSE(ymio::inflate(dyn, 849));
    for (size_t n = 0; n < dyn.size(); n += 7) CHECK_FALSE(ymio::inflate(std::span(dyn).first(n), 10000));
}

TEST_CASE("gzip: stored members round trip and their checks refuse damage", "[ymio][ymt]") {
    std::mt19937 rng(5);
    for (const size_t n : {size_t(0), size_t(1), size_t(65535), size_t(65536), size_t(200000)}) {
        std::vector<uint8_t> data(n);
        for (uint8_t& b : data) b = uint8_t(rng());
        const std::vector<uint8_t> gz = ymio::gzipStore(data);
        CHECK(gz[0] == 0x1F);
        CHECK(gz[1] == 0x8B);
        const ymio::GunzipResult r = ymio::gunzip(gz, 1 << 20);
        REQUIRE(r.data);
        CHECK(*r.data == data);
    }
    std::vector<uint8_t> gz = ymio::gzipStore(bytes("STYM2149"));
    gz[gz.size() - 8] ^= 1;  // checksum
    CHECK(ymio::gunzip(gz, 1000).error == "The gzip checksum does not match.");
    gz = ymio::gzipStore(bytes("STYM2149"));
    gz[gz.size() - 4] ^= 1;  // length
    CHECK(ymio::gunzip(gz, 1000).error == "The gzip length does not match.");
    CHECK_FALSE(ymio::gunzip(std::span(gz).first(gz.size() - 1), 1000).data);
    CHECK(ymio::gunzip(bytes("PK\3\4 not a gzip stream"), 1000).error == "Not a gzip stream.");
    // A name and a comment in the header are skipped.
    std::vector<uint8_t> named = ymio::gzipStore(bytes("YM"));
    named[3] = 8 | 16;
    const std::vector<uint8_t> fields = bytes(std::string("song.ymt\0note\0", 14));
    named.insert(named.begin() + 10, fields.begin(), fields.end());
    const ymio::GunzipResult r = ymio::gunzip(named, 1000);
    REQUIRE(r.data);
    CHECK(*r.data == bytes("YM"));
}

TEST_CASE("YM-Tracker: effect letters follow the codes read from the demo songs", "[ymio][ymt]") {
    const std::string letters = "0123456789ABCDEFGHKLMPRTXYQSUZ";
    for (size_t k = 0; k < letters.size(); ++k) {
        CHECK(ymio::ymtEffectLetter(uint8_t(k + 1)) == letters[k]);
        CHECK(ymio::ymtEffectCode(letters[k]) == k + 1);
    }
    CHECK(ymio::ymtEffectLetter(0) == 0);
    CHECK(ymio::ymtEffectLetter(31) == 0);
    CHECK(ymio::ymtEffectCode('W') == 0);
    CHECK(ymio::ymtEffectCode(0) == 0);
}

TEST_CASE("YM-Tracker: a song written and read back is the same", "[ymio][ymt]") {
    const YmtSong s = song();
    const std::vector<uint8_t> file = ymio::writeYmt(s);
    REQUIRE(file.size() > 18);
    CHECK(std::memcmp(file.data(), "YMT1\x01\x01\x08\x00", 8) == 0);
    CHECK(file[8] == 0x1F);
    const ymio::YmtParseResult r = ymio::parseYmt(file);
    REQUIRE(r.song);
    CHECK(*r.song == s);
    CHECK(r.song->cell(0, 0, 0).effect == '0');
    CHECK(r.song->cell(0, 2, 3).note == 13);
    CHECK(r.song->instruments[2].drums[0].levels == std::vector<uint8_t>{1, 2, 3});
}

TEST_CASE("YM-Tracker: the layout as the demo songs store it", "[ymio][ymt]") {
    // A body written by hand: title "A", 125 BPM, speed 6, 1 chip, restart 0, 2 channels, 3 tone channels, one
    // order entry, one pattern of one row, one instrument, digidrum channel.
    std::vector<uint8_t> body = {1, 0, 'A', 125, 0, 6, 1, 0, 0, 2, 3, 1, 0, 1, 0, 1, 0, 1,
                                 1, 'X', 1, 'Y',  // channel names
                                 0,               // order
                                 1, 0, 9, 0, 0, 0,  // pattern: 1 row, 9 bytes
                                 0x9F, 37, 1, 0x50, 30, 2,  // packed cell: note, instrument, volume, Z, 02
                                 49, 2, 0x40};              // the second cell has no bit 7: five bytes...
    body.insert(body.end(), {27, 0x24});                    // ... Q24
    body[25] = 11;                                          // the pattern holds 11 bytes
    const std::vector<uint8_t> instrument = {
        3, 0, 'K', 'i', 't',
        1, 0, 2, 0, 1, 0, 10, 0, 0, 1, 1, 0, 1, 16, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,  // the 24 bytes: noise, no tone
        0xFF, 0xFF, 2, 0, 15, 0,  // volume list: no loop, 15 then 0
        0, 0,                     // word
        1, 0, 0,                  // arpeggio list
        2, 0, 0x96, 0, 0x18, 0xFC,  // pitch list: 150, -1000
        0, 0,                       // word
        1, 0, 4, 'K', 'i', 'c', 'k', 0x78, 0x69, 3, 0, 0, 0, 0x21, 0x0F};  // one drum of 3 values at 27 000 Hz
    body.insert(body.end(), instrument.begin(), instrument.end());
    std::vector<uint8_t> file = {'Y', 'M', 'T', '1', 1, 1, 8, 0};
    const std::vector<uint8_t> gz = ymio::gzipStore(body);
    file.insert(file.end(), gz.begin(), gz.end());
    const ymio::YmtParseResult r = ymio::parseYmt(file);
    INFO(r.error);
    REQUIRE(r.song);
    const YmtSong& s = *r.song;
    CHECK(s.title == "A");
    CHECK(s.bpm == 125);
    CHECK(s.speed == 6);
    CHECK(s.channels() == 2);
    CHECK(s.channelNames[1] == "Y");
    CHECK(s.drumChannel);
    CHECK(s.cell(0, 0, 0) == YmtCell{37, 1, 0x50, 'Z', 2});
    CHECK(s.cell(0, 0, 1) == YmtCell{49, 2, 0x40, 'Q', 0x24});
    REQUIRE(s.instruments.size() == 1);
    const YmtInstrument& in = s.instruments[0];
    CHECK(in.name == "Kit");
    CHECK(in.decay == 2);
    CHECK(in.release == 1);
    CHECK(in.shape == 10);
    CHECK(in.noise);
    CHECK_FALSE(in.tone);
    CHECK(in.noisePeriod == 16);
    CHECK(in.volumeLoop == -1);
    CHECK(in.volume == std::vector<uint8_t>{15, 0});
    CHECK(in.pitch == std::vector<int16_t>{150, -1000});
    REQUIRE(in.drums.size() == 1);
    CHECK(in.drums[0].rate == 27000);
    CHECK(in.drums[0].levels == std::vector<uint8_t>{1, 2, 15});  // low nibble first
    // Written back, the body is the same bytes.
    const std::vector<uint8_t> written = ymio::writeYmt(s);
    const ymio::GunzipResult again = ymio::gunzip(std::span(written).subspan(8), 1 << 20);
    REQUIRE(again.data);
    // The five-byte cell (bytes 35 to 39) comes back packed, in 6 bytes.
    std::vector<uint8_t> expected = body;
    const std::vector<uint8_t> repacked = {0x9F, 49, 2, 0x40, 27, 0x24};
    expected.erase(expected.begin() + 35, expected.begin() + 40);
    expected.insert(expected.begin() + 35, repacked.begin(), repacked.end());
    expected[25] = 12;
    CHECK(*again.data == expected);
}

TEST_CASE("YM-Tracker: damaged and cut songs are refused without harm", "[ymio][ymt]") {
    CHECK(ymio::parseYmt(bytes("YMT0\1\1\x08")).error == "Not a YM-Tracker song.");
    CHECK(ymio::parseYmt(bytes("Extended Module: ")).error == "Not a YM-Tracker song.");
    const std::vector<uint8_t> file = ymio::writeYmt(song());
    for (size_t n = 0; n < file.size(); n += 3) CHECK_FALSE(ymio::parseYmt(std::span(file).first(n)).song);
    // A body cut after its patterns.
    const ymio::GunzipResult body = ymio::gunzip(std::span(file).subspan(8), 1 << 20);
    REQUIRE(body.data);
    for (const size_t keep : {size_t(4), size_t(30), body.data->size() - 1}) {
        std::vector<uint8_t> cut = {'Y', 'M', 'T', '1', 1, 1, 8, 0};
        const std::vector<uint8_t> gz = ymio::gzipStore(std::span(*body.data).first(keep));
        cut.insert(cut.end(), gz.begin(), gz.end());
        const ymio::YmtParseResult r = ymio::parseYmt(cut);
        CHECK_FALSE(r.song);
        CHECK_FALSE(r.error.empty());
    }
    // Random bytes in the body: refused or read, never out of bounds.
    std::mt19937 rng(9);
    for (int t = 0; t < 200; ++t) {
        std::vector<uint8_t> b = *body.data;
        for (int k = 0; k < 4; ++k) b[rng() % b.size()] = uint8_t(rng());
        std::vector<uint8_t> f = {'Y', 'M', 'T', '1', 1, 1, 8, 0};
        const std::vector<uint8_t> gz = ymio::gzipStore(b);
        f.insert(f.end(), gz.begin(), gz.end());
        const ymio::YmtParseResult r = ymio::parseYmt(f);
        CHECK((r.song.has_value() != !r.error.empty()));
    }
}
