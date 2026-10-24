// STYM2149 — LZH decompression (port of pylzh, D-043)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <string>
#include <vector>

#include "ReadFile.h"
#include "ymio/Lzh.h"
#include "ymio/YmFile.h"

using namespace stym;

namespace {
std::vector<uint8_t> archive(const char* name) {
    const std::string path = std::string(STYM_TEST_DATA) + "/lzh/" + name;
    std::vector<uint8_t> data = tools::readFile(path.c_str());
    REQUIRE_FALSE(data.empty());
    return data;
}
std::string text(const std::vector<uint8_t>& d, size_t n) { return {d.begin(), d.begin() + long(std::min(n, d.size()))}; }

void le16(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}
void le32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    le16(b, at, v & 0xffff);
    le16(b, at + 2, v >> 16);
}
// Level 0 -lh0- archive containing `content` under the name `name`.
std::vector<uint8_t> storeArchive(const std::vector<uint8_t>& content, const std::string& name) {
    std::vector<uint8_t> b(24 + name.size(), 0);
    b[0] = uint8_t(22 + name.size());
    const char* method = "-lh0-";
    std::copy(method, method + 5, b.begin() + 2);
    le32(b, 7, uint32_t(content.size()));
    le32(b, 11, uint32_t(content.size()));
    b[21] = uint8_t(name.size());
    std::copy(name.begin(), name.end(), b.begin() + 22);
    le16(b, 22 + name.size(), ymio::lzhCrc16(content));
    b[1] = 0;  // header checksum: not checked (like pylzh)
    b.insert(b.end(), content.begin(), content.end());
    b.push_back(0);  // end of archive
    return b;
}
}  // namespace

TEST_CASE("LZH: CRC-16", "[lzh]") {
    const std::string s = "123456789";
    CHECK(ymio::lzhCrc16({reinterpret_cast<const uint8_t*>(s.data()), s.size()}) == 0xBB3D);
}

TEST_CASE("LZH: methods and header levels (pylzh archives)", "[lzh]") {
    const auto lh0 = ymio::unpackLzh(archive("h0_lh0.lzh"));
    REQUIRE(lh0.error.empty());
    CHECK(lh0.name == "gpl-2.gz");
    CHECK(lh0.method == "-lh0-");
    CHECK(lh0.data.size() == 6829);
    CHECK(ymio::lzhCrc16(lh0.data) == 0xB6D5);

    for (const char* name : {"h0_lh5.lzh", "h1_lh5.lzh", "h2_lh5.lzh", "h2_lh6.lzh", "h2_lh7.lzh"}) {
        INFO(name);
        const std::vector<uint8_t> a = archive(name);
        CHECK(ymio::isLzh(a));
        const auto r = ymio::unpackLzh(a);
        REQUIRE(r.error.empty());
        CHECK(r.name == "gpl-2");
        CHECK(r.data.size() == 18092);
        CHECK(ymio::lzhCrc16(r.data) == 0xA33A);
        CHECK(text(r.data, 46) == "                    GNU GENERAL PUBLIC LICENSE");
    }

    const auto sub = ymio::unpackLzh(archive("h2_subdir.lzh"));  // two directory entries, then the file
    REQUIRE(sub.error.empty());
    CHECK(sub.name == "hello.txt");
    CHECK(text(sub.data, 100) == "hello world\n");

    const auto lh1 = ymio::unpackLzh(archive("lh1-64k.lzh"));
    CHECK(lh1.data.empty());
    CHECK(lh1.error.find("-lh1-") != std::string::npos);
}

TEST_CASE("LZH: truncated or corrupt data refused without out-of-bounds reads", "[lzh]") {
    const std::vector<uint8_t> full = archive("h2_lh5.lzh");
    int refused = 0, tried = 0;
    for (size_t cut = 0; cut < full.size() - 1; cut += 7) {
        const std::vector<uint8_t> t(full.begin(), full.begin() + long(cut));
        const auto r = ymio::unpackLzh(t);
        ++tried;
        refused += !r.error.empty() && r.data.empty();
    }
    CHECK(refused == tried);
    for (size_t at = 0; at < full.size(); at += 3) {
        std::vector<uint8_t> c = full;
        c[at] ^= 0x5a;
        const auto r = ymio::unpackLzh(c);
        // Either an error, or a result whose CRC matches (byte outside the useful data, for example).
        if (r.error.empty()) CHECK(ymio::lzhCrc16(r.data) == 0xA33A);
        else CHECK(r.data.empty());
    }
}

TEST_CASE("LZH: parseYm decompresses an archived YM file", "[lzh][ymio]") {
    std::vector<uint8_t> ym = {'Y', 'M', '3', '!'};
    for (unsigned r = 0; r < 14; ++r)
        for (unsigned t = 0; t < 3; ++t) ym.push_back(uint8_t(r * 3 + t));
    const std::vector<uint8_t> packed = storeArchive(ym, "TEST.YM");
    CHECK(ymio::isLzh(packed));
    const auto parsed = ymio::parseYm(packed);
    REQUIRE(parsed.file);
    CHECK(parsed.file->frameCount == 3);
    CHECK(parsed.file->reg(2, 13) == 13 * 3 + 2);

    std::vector<uint8_t> badCrc = packed;
    badCrc[24 + 7 + 5] ^= 1;  // a byte of the YM file
    const auto bad = ymio::parseYm(badCrc);
    CHECK_FALSE(bad.file);
    CHECK(bad.error.find("CRC") != std::string::npos);
}

namespace {
// Test contents for packLzh: pseudo-random (incompressible, several blocks), repetition at the maximum -lh5- distance
// (8 192), long run of one byte (copies of 256 at distance 1), text.
std::vector<uint8_t> noise(size_t n, uint32_t seed) {
    std::vector<uint8_t> d(n);
    for (uint8_t& b : d) {
        seed = seed * 1664525u + 1013904223u;
        b = uint8_t(seed >> 24);
    }
    return d;
}
std::vector<std::vector<uint8_t>> packSamples() {
    std::vector<std::vector<uint8_t>> s;
    s.push_back({});
    s.push_back({'A'});
    s.push_back({'A', 'B'});
    s.push_back(std::vector<uint8_t>(5000, 'a'));
    s.push_back(noise(100'000, 1));
    std::vector<uint8_t> far = noise(8192, 2);
    for (int k = 0; k < 2; ++k) far.insert(far.end(), far.begin(), far.begin() + 8192);
    far.push_back(7);
    s.push_back(far);
    std::string text;
    for (int k = 0; text.size() < 60'000; ++k) text += "Programming: Pascal Indus Nowak, frame " + std::to_string(k * 37 % 1000) + "\n";
    s.push_back({text.begin(), text.end()});
    return s;
}
uint32_t readLe32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}
}  // namespace

TEST_CASE("LZH: packLzh, -lh5- archive read back identically", "[lzh]") {
    for (const std::vector<uint8_t>& d : packSamples()) {
        INFO(d.size());
        const std::vector<uint8_t> a = ymio::packLzh("STYM2149.YM", d);
        CHECK(ymio::isLzh(a));
        const auto r = ymio::unpackLzh(a);
        REQUIRE(r.error.empty());
        CHECK(r.method == "-lh5-");
        CHECK(r.name == "STYM2149.YM");
        CHECK(r.data == d);
    }
    const auto samples = packSamples();
    CHECK(ymio::packLzh("a", samples[3]).size() < 60);                     // 5 000 "a"
    CHECK(ymio::packLzh("a", samples[4]).size() < 100'000 * 102 / 100);    // noise: barely larger
    CHECK(ymio::packLzh("a", samples[5]).size() < 8192 + 2000);           // two copies of 8 KiB at distance 8 192
    CHECK(ymio::packLzh("a", samples[6]).size() * 4 < samples[6].size());  // repetitive text
}

TEST_CASE("LZH: packLzh, level 0 header", "[lzh]") {
    const std::vector<uint8_t> d = noise(3000, 3);
    const uint32_t when = ymio::lzhDosTime(2026, 10, 16, 15, 42, 31);
    CHECK(when == ((46u << 9 | 10u << 5 | 16u) << 16 | (15u << 11 | 42u << 5 | 15u)));
    const std::vector<uint8_t> a = ymio::packLzh("SONG.YM", d, when);
    REQUIRE(a.size() > 31);
    CHECK(a[0] == 22 + 7);
    uint8_t sum = 0;
    for (size_t i = 2; i < size_t(2 + a[0]); ++i) sum = uint8_t(sum + a[i]);
    CHECK(a[1] == sum);
    CHECK(text(a, 7).substr(2) == "-lh5-");
    CHECK(readLe32(a, 7) == a.size() - 31 - 1);  // compressed data between the header and the end of archive
    CHECK(readLe32(a, 11) == 3000);
    CHECK(readLe32(a, 15) == when);
    CHECK(a[19] == 0x20);
    CHECK(a[20] == 0);
    CHECK(a[21] == 7);
    CHECK(std::string(a.begin() + 22, a.begin() + 29) == "SONG.YM");
    CHECK((a[29] | a[30] << 8) == ymio::lzhCrc16(d));
    CHECK(a.back() == 0);
    // Name too long for a level 0 header: cut.
    const auto longName = ymio::unpackLzh(ymio::packLzh(std::string(300, 'n'), d));
    REQUIRE(longName.error.empty());
    CHECK(longName.name.size() == ymio::kLzhMaxName);
    CHECK(longName.data == d);
}

TEST_CASE("LZH: Huffman lengths limited to 16 bits, complete code", "[lzh]") {
    CHECK(ymio::lzhHuffmanLengths(std::vector<uint32_t>{0, 9, 0}, 16) == std::vector<uint8_t>{0, 0, 0});
    CHECK(ymio::lzhHuffmanLengths(std::vector<uint32_t>{1, 1}, 16) == std::vector<uint8_t>{1, 1});
    CHECK(ymio::lzhHuffmanLengths(std::vector<uint32_t>{5, 0, 1, 1}, 16) == std::vector<uint8_t>{1, 0, 2, 2});
    // Fibonacci frequencies: 29 bits deep without a limit.
    std::vector<uint32_t> fib = {1, 1};
    while (fib.size() < 30) fib.push_back(fib[fib.size() - 1] + fib[fib.size() - 2]);
    const std::vector<uint8_t> free = ymio::lzhHuffmanLengths(fib, 40);
    CHECK(*std::ranges::max_element(free) == 29);
    const std::vector<uint8_t> len = ymio::lzhHuffmanLengths(fib, 16);
    uint64_t kraft = 0;
    for (size_t s = 0; s < len.size(); ++s) {
        CAPTURE(s);
        REQUIRE(len[s] >= 1);
        REQUIRE(len[s] <= 16);
        kraft += uint64_t(1) << (16 - len[s]);
        if (s > 0) CHECK(len[s] <= len[s - 1]);  // more frequent, not longer
    }
    CHECK(kraft == uint64_t(1) << 16);
}
