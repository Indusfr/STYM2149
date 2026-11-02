// STYM2149 — ymio: maxYMiser songs (.MYS) and voicesets (.MYV): layout, packed rows, digidrums, round trips,
// refused files; the songs of an SNDH saved by maxYMiser, in its layouts.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <random>
#include <utility>
#include <vector>

#include "ymio/MaxYm.h"

using namespace stym;
using ymio::MysRow;
using ymio::MysSong;
using ymio::MyvInstrument;
using ymio::MyvVoiceset;

namespace {

MysSong song() {
    MysSong s;
    s.speed = 5;
    s.length = 3;
    s.repeat = 1;
    s.header[0] = 7;  // editor state, kept as it is
    s.patterns.resize(2);
    s.patterns[0].rows[0] = {70, 1, 0, 0, 0, 0, 0};             // note, instrument 1
    s.patterns[0].rows[4] = {ymio::kMysNoteOffRow, 0, 0, 0, 0, 0, 0};
    s.patterns[0].rows[63] = {0, 0, 16, 'A', 0x12, 'V', 0x03};  // volume and two effects on the last row
    s.steps[0].channel = {0, 1, ymio::kMysEmpty};
    s.steps[1].channel = {1, 0, ymio::kMysNoteOff};
    s.steps[2].channel = {0, 0, 0};
    s.steps[2].dma = 4;
    return s;
}

MyvVoiceset voiceset() {
    MyvVoiceset v;
    v.sequences.resize(3);
    v.sequences[1].length = 4;
    v.sequences[1].repeat = 2;
    v.sequences[1].data = {15, 14, 0x8001, 12};
    v.sequences[2].length = 1;
    v.sequences[2].data[0] = 0xFFFF;
    MyvInstrument& i = v.instruments[0];
    i.name = "lead";
    i.at(MyvInstrument::kSequenceSpeed) = 2;
    i.at(MyvInstrument::kVolume) = 3;
    i.at(MyvInstrument::kArpeggio) = 4 | 1;
    i.at(MyvInstrument::kSequences) = 1;
    i.at(MyvInstrument::kSequences + 7) = 2;
    v.instruments[31].name = "a name too long!!";
    v.digidrums[2] = {1, 2, 3, 250};
    v.digidrums[5] = {9};
    return v;
}

using Bytes = std::vector<uint8_t>;

// An SNDH header and a stand-in for the replay routine, before the blocks of maxYMiser.
Bytes routine() {
    Bytes b = {0x60, 0x00, 0x00, 0x0a, 'S', 'N', 'D', 'H', 'H', 'D', 'N', 'S'};
    for (int i = 0; i < 300; ++i) b.push_back(uint8_t(i * 13));
    return b;
}

// The layout of an SNDH of one song: the voiceset without its digidrums, the song, then the digidrum block, the
// digidrum addresses counted from the voiceset's start.
Bytes oneSongImage(const MyvVoiceset& v, const MysSong& s, size_t zeros) {
    const Bytes myv = ymio::writeMyv(v), mys = ymio::writeMys(s);
    const size_t digi = myv.size() - [&] {
        size_t n = 0;
        for (const Bytes& d : v.digidrums) n += d.empty() ? 1 : d.size();
        return n + 8;
    }();
    Bytes b = routine();
    const size_t v0 = b.size();
    b.insert(b.end(), myv.begin(), myv.begin() + long(digi));
    b.insert(b.end(), mys.begin(), mys.end());
    b.resize(b.size() + zeros, 0);
    const size_t moved = b.size() - v0 - digi;  // what the digidrum block moved by
    b.insert(b.end(), myv.begin() + long(digi), myv.end());
    for (size_t d = 0; d < 8; ++d) {
        const size_t at = v0 + 4 * d;
        const uint32_t start = uint32_t(b[at] << 24 | b[at + 1] << 16 | b[at + 2] << 8 | b[at + 3]) + uint32_t(moved);
        for (int k = 0; k < 4; ++k) b[at + size_t(k)] = uint8_t(start >> (24 - 8 * k));
    }
    return b;
}

// The voiceset of the older maXYmizer: "MYM0INST" and sequences of 64 bytes (31 words, length, repeat).
Bytes oldVoicesetBlocks(const MyvVoiceset& v, const MysSong& s) {
    Bytes b = oneSongImage(v, s, 0);
    const size_t v0 = routine().size();
    std::memcpy(b.data() + v0 + 32, "MYM0INST", 8);
    const size_t first = v0 + 2120;
    Bytes out(b.begin(), b.begin() + long(first));
    for (size_t k = 0; k < v.sequences.size(); ++k) {
        const size_t at = first + 128 * k;
        out.insert(out.end(), b.begin() + long(at), b.begin() + long(at + 62));
        out.insert(out.end(), b.begin() + long(at + 126), b.begin() + long(at + 128));
    }
    const size_t shrink = 64 * v.sequences.size();
    out.insert(out.end(), b.begin() + long(first + 128 * v.sequences.size()), b.end());
    for (size_t d = 0; d < 8; ++d) {
        const size_t at = v0 + 4 * d;
        const uint32_t start = uint32_t(out[at] << 24 | out[at + 1] << 16 | out[at + 2] << 8 | out[at + 3]) -
                               uint32_t(shrink);
        for (int k = 0; k < 4; ++k) out[at + size_t(k)] = uint8_t(start >> (24 - 8 * k));
    }
    return out;
}

}  // namespace

TEST_CASE("maxYMiser song: layout of the header, the song and the packed rows", "[maxym]") {
    const std::vector<uint8_t> f = ymio::writeMys(song());
    REQUIRE(std::memcmp(f.data(), "MYM0TRAK", 8) == 0);
    CHECK(f[8] == 7);
    CHECK(f[8 + 13] == 5);  // song speed
    CHECK(f[72] == 0);      // step 0: patterns of A, B, C, then DMA
    CHECK(f[73] == 1);
    CHECK(f[74] == 0xFF);
    CHECK(f[72 + 4 + 2] == 0xFE);
    CHECK(f[72 + 8 + 3] == 4);
    CHECK(f[1096] == 3);  // length
    CHECK(f[1097] == 1);  // repeat
    // Pattern 0: row 0 then 3 empty rows, row 4 then 58 empty rows, row 63. Pattern 1: row 0 then 63 empty rows.
    REQUIRE(f.size() == 1098 + 8 * 4);
    const std::vector<uint8_t> rows(f.begin() + 1098, f.end());
    CHECK(rows == std::vector<uint8_t>{70, 1, 0, 0, 0, 0, 0, 3, 1, 0, 0, 0, 0, 0, 0, 58,
                                       0, 0, 16, 'A', 0x12, 'V', 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 63});
}

TEST_CASE("maxYMiser song: read back what is written", "[maxym]") {
    const MysSong s = song();
    const auto r = ymio::parseMys(ymio::writeMys(s));
    REQUIRE(r.value);
    CHECK(r.error.empty());
    CHECK(r.value->speed == 5);
    CHECK(r.value->length == 3);
    CHECK(r.value->repeat == 1);
    CHECK(r.value->header == [] {
        auto h = song().header;
        h[13] = 5;
        return h;
    }());
    CHECK(r.value->steps == s.steps);
    REQUIRE(r.value->patterns.size() == 2);
    for (size_t p = 0; p < 2; ++p) CHECK(r.value->patterns[p].rows == s.patterns[p].rows);
    CHECK(ymio::writeMys(*r.value) == ymio::writeMys(s));
}

TEST_CASE("maxYMiser song: cut or inconsistent files are refused", "[maxym]") {
    const std::vector<uint8_t> good = ymio::writeMys(song());
    auto refused = [](std::vector<uint8_t> f) { return !ymio::parseMys(f).value && !ymio::parseMys(f).error.empty(); };
    CHECK(refused({}));
    auto f = good;
    f[3] = 'X';
    CHECK(refused(f));                                                       // signature
    CHECK(refused(std::vector<uint8_t>(good.begin(), good.end() - 8)));      // last pattern cut
    CHECK(refused(std::vector<uint8_t>(good.begin(), good.end() - 3)));      // row cut
    f = good;
    f[1098 + 7] = 64;  // more empty rows than the pattern holds
    CHECK(refused(f));
    f = good;
    f[73] = 2;  // step 0 names pattern 2, missing
    CHECK(refused(f));
    f = good;
    f[73 + 4 * 10] = 9;  // after the end of the song: not checked
    CHECK(ymio::parseMys(f).value);
    f = good;
    f[8 + 13] = 0;  // speed 0
    CHECK(refused(f));
    f = good;
    f[1097] = 3;  // repeat past the end
    CHECK(refused(f));
    f = good;
    f[1098 + 1] = 33;  // instrument 33
    CHECK(refused(f));
    f = good;
    f[1098 + 2] = 17;  // volume 17
    CHECK(refused(f));
}

TEST_CASE("maxYMiser voiceset: layout of instruments, sequences and digidrums", "[maxym]") {
    const std::vector<uint8_t> f = ymio::writeMyv(voiceset());
    REQUIRE(std::memcmp(f.data() + 32, "MYM1INST", 8) == 0);
    CHECK(std::memcmp(f.data() + 40, "lead\0", 5) == 0);
    CHECK(f[40 + 32] == 2);   // sequence speed
    CHECK(f[40 + 38] == 3);   // volume
    CHECK(f[40 + 17] == 5);   // arpeggio flag on square and timer
    CHECK(f[40 + 48] == 1);   // volume sequence
    CHECK(f[40 + 55] == 2);   // pulse width sequence
    CHECK(std::memcmp(f.data() + 40 + 64 * 31, "a name too long", 15) == 0);
    CHECK(f[40 + 64 * 31 + 15] == 0);
    // Sequence 1 at 2120 + 128: big-endian words, then length and repeat.
    const size_t s1 = 2120 + 128;
    CHECK(f[s1 + 4] == 0x80);
    CHECK(f[s1 + 5] == 0x01);
    CHECK(f[s1 + 126] == 4);
    CHECK(f[s1 + 127] == 2);
    // Digidrums: signature after the three sequences, one byte for each empty slot, lengths and offsets.
    const size_t digi = 2120 + 3 * 128;
    REQUIRE(std::memcmp(f.data() + digi, "MYM1DIGI", 8) == 0);
    CHECK(f.size() == digi + 8 + 6 + 4 + 1);
    const uint32_t start2 = uint32_t(f[8] << 24 | f[9] << 16 | f[10] << 8 | f[11]);
    CHECK(start2 == digi + 8 + 2);
    CHECK(f[start2 + 3] == 250);
    CHECK(f[2088 + 2 * 4 + 1] == 4);  // length of digidrum 2
    CHECK(f[2088 + 0 * 4 + 1] == 0);
}

TEST_CASE("maxYMiser voiceset: read back what is written", "[maxym]") {
    const MyvVoiceset v = voiceset();
    const auto r = ymio::parseMyv(ymio::writeMyv(v));
    REQUIRE(r.value);
    CHECK(r.value->sequences == v.sequences);
    CHECK(r.value->digidrums == v.digidrums);
    CHECK(r.value->instruments[0] == v.instruments[0]);
    CHECK(r.value->instruments[31].name == "a name too long");
    CHECK(ymio::writeMyv(*r.value) == ymio::writeMyv(v));
}

TEST_CASE("maxYMiser voiceset: cut or inconsistent files are refused", "[maxym]") {
    const std::vector<uint8_t> good = ymio::writeMyv(voiceset());
    auto refused = [](std::vector<uint8_t> f) { return !ymio::parseMyv(f).value && !ymio::parseMyv(f).error.empty(); };
    CHECK(refused({}));
    auto f = good;
    f[33] = 'X';
    CHECK(refused(f));  // signature
    const size_t digi = 2120 + 3 * 128;
    CHECK(refused(std::vector<uint8_t>(good.begin(), good.begin() + long(digi) + 4)));  // no digidrum block
    CHECK(refused(std::vector<uint8_t>(good.begin(), good.begin() + long(digi) - 3)));  // sequence cut
    f = good;
    f[2088 + 2 * 4 + 1] = 200;  // digidrum longer than the file
    CHECK(refused(f));
    f = good;
    f[40 + 50] = 3;  // vibrato sequence 3, missing
    CHECK(refused(f));
}

TEST_CASE("maxYMiser files: random bytes never crash the readers", "[maxym]") {
    std::mt19937 rng(510);
    const std::vector<uint8_t> mys = ymio::writeMys(song()), myv = ymio::writeMyv(voiceset());
    for (int n = 0; n < 2000; ++n) {
        std::vector<uint8_t> a = mys, b = myv;
        for (int k = 0; k < 6; ++k) {
            a[rng() % a.size()] = uint8_t(rng());
            b[rng() % b.size()] = uint8_t(rng());
        }
        a.resize(rng() % (a.size() + 16));
        b.resize(rng() % (b.size() + 16));
        (void)ymio::parseMys(a);
        (void)ymio::parseMyv(b);
    }
    SUCCEED();
}

TEST_CASE("maxYMiser in an SNDH: the song and its voiceset read from the layout of one song", "[maxym]") {
    const MysSong s = song();
    const MyvVoiceset v = voiceset();
    for (size_t zeros : {size_t(0), size_t(6), size_t(700)}) {
        CAPTURE(zeros);
        const auto songs = ymio::maxYmSongsIn(oneSongImage(v, s, zeros));
        REQUIRE(songs.size() == 1);
        CHECK(ymio::writeMys(songs[0].song) == ymio::writeMys(s));
        CHECK(ymio::writeMyv(songs[0].voices) == ymio::writeMyv(v));
        CHECK(songs[0].voices.digidrums == v.digidrums);
    }
}

TEST_CASE("maxYMiser in an SNDH: several songs in the order of the file, each with the voiceset before it",
          "[maxym]") {
    // The layout of an SNDH of several songs: each voiceset whole, digidrums included, then its song.
    MysSong s2 = song();
    s2.speed = 9;
    s2.patterns[1].rows[10] = {50, 2, 0, 0, 0, 0, 0};
    MyvVoiceset v2 = voiceset();
    v2.instruments[1].name = "bass";
    v2.digidrums[2].clear();
    Bytes b = routine();
    for (const auto& [v, s] : {std::pair{voiceset(), song()}, std::pair{v2, s2}}) {
        const Bytes myv = ymio::writeMyv(v), mys = ymio::writeMys(s);
        b.insert(b.end(), myv.begin(), myv.end());  // digidrum addresses from the voiceset's start, as in the file
        b.insert(b.end(), mys.begin(), mys.end());
    }
    b.resize(b.size() + 4, 0);
    const auto songs = ymio::maxYmSongsIn(b);
    REQUIRE(songs.size() == 2);
    CHECK(ymio::writeMys(songs[0].song) == ymio::writeMys(song()));
    CHECK(ymio::writeMyv(songs[0].voices) == ymio::writeMyv(voiceset()));
    CHECK(ymio::writeMys(songs[1].song) == ymio::writeMys(s2));
    CHECK(ymio::writeMyv(songs[1].voices) == ymio::writeMyv(v2));
    // A second song with no voiceset of its own plays with the first one's.
    Bytes shared = oneSongImage(voiceset(), song(), 0);
    const Bytes mys2 = ymio::writeMys(s2);
    shared.insert(shared.end(), mys2.begin(), mys2.end());
    const auto two = ymio::maxYmSongsIn(shared);
    REQUIRE(two.size() == 2);
    CHECK(two[1].song.speed == 9);
    CHECK(two[1].voices.instruments[0].name == "lead");
}

TEST_CASE("maxYMiser in an SNDH: the older voiceset of maXYmizer, with sequences of 31 steps", "[maxym]") {
    const MysSong s = song();
    const MyvVoiceset v = voiceset();
    const auto songs = ymio::maxYmSongsIn(oldVoicesetBlocks(v, s));
    REQUIRE(songs.size() == 1);
    CHECK(songs[0].voices.sequences == v.sequences);
    CHECK(songs[0].voices.digidrums == v.digidrums);
    CHECK(songs[0].voices.instruments[0] == v.instruments[0]);
    CHECK(ymio::writeMys(songs[0].song) == ymio::writeMys(s));
}

TEST_CASE("maxYMiser in an SNDH: none without a voiceset, a whole song or a digidrum in the file", "[maxym]") {
    CHECK(ymio::maxYmSongsIn(routine()).empty());
    CHECK(ymio::maxYmSongsIn({}).empty());
    const Bytes good = oneSongImage(voiceset(), song(), 0);
    // A song without a voiceset before it.
    const Bytes mys = ymio::writeMys(song());
    Bytes b = routine();
    b.insert(b.end(), mys.begin(), mys.end());
    CHECK(ymio::maxYmSongsIn(b).empty());
    // The song's second pattern cut by the end of the file.
    const size_t trak = routine().size() + 2120 + 3 * 128;
    REQUIRE(std::memcmp(good.data() + trak, "MYM0TRAK", 8) == 0);
    CHECK(ymio::maxYmSongsIn(Bytes(good.begin(), good.begin() + long(trak + 1098 + 12))).empty());
    // A digidrum beyond the end of the file.
    Bytes far = good;
    far[routine().size() + 8] = 0x7f;
    CHECK(ymio::maxYmSongsIn(far).empty());
}

TEST_CASE("maxYMiser in an SNDH: random bytes never crash the search", "[maxym]") {
    std::mt19937 rng(5101);
    const Bytes good = oneSongImage(voiceset(), song(), 3);
    for (int n = 0; n < 2000; ++n) {
        Bytes b = good;
        for (int k = 0; k < 8; ++k) b[rng() % b.size()] = uint8_t(rng());
        b.resize(rng() % (b.size() + 16));
        (void)ymio::maxYmSongsIn(b);
    }
    SUCCEED();
}
