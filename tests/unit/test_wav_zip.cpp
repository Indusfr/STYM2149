// STYM2149 — ymio: WAV files and ZIP archive of the exports (M7, A4)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "ymio/ByteSink.h"
#include "ymio/Wav.h"
#include "ymio/Zip.h"

using namespace stym::ymio;

namespace {

uint32_t rd16(const std::vector<uint8_t>& b, size_t at) { return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8; }
uint32_t rd32(const std::vector<uint8_t>& b, size_t at) { return rd16(b, at) | rd16(b, at + 2) << 16; }
std::string str(const std::vector<uint8_t>& b, size_t at, size_t n) {
    return {reinterpret_cast<const char*>(b.data() + at), n};
}
std::vector<uint8_t> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

}  // namespace

TEST_CASE("ZIP CRC-32 and recombination of two CRCs", "[wav]") {
    CHECK(crc32(bytesOf("123456789")) == 0xCBF43926u);
    CHECK(crc32({}) == 0);
    std::mt19937 rng(7);
    for (const size_t la : {size_t(0), size_t(1), size_t(13), size_t(1000)})
        for (const size_t lb : {size_t(0), size_t(1), size_t(2), size_t(57), size_t(4096), size_t(100003)}) {
            std::vector<uint8_t> a(la), b(lb);
            for (uint8_t& x : a) x = uint8_t(rng());
            for (uint8_t& x : b) x = uint8_t(rng());
            std::vector<uint8_t> ab = a;
            ab.insert(ab.end(), b.begin(), b.end());
            CAPTURE(la, lb);
            CHECK(crc32Combine(crc32(a), crc32(b), lb) == crc32(ab));
            CHECK(crc32(b, crc32(a)) == crc32(ab));  // continued CRC
        }
}

TEST_CASE("16-bit WAV: header and the prototype's conversion (wavBlobDv)", "[wav]") {
    const float l[] = {1.0f, -1.0f, 0.5f, -0.5f, 2.0f, std::numeric_limits<float>::quiet_NaN()};
    const float r[] = {0.0f, 0.25f, -2.0f, 1e-6f, -0.999f, 0.1f};
    std::vector<uint8_t> f = wavHeader(WavFormat::Pcm16, 48000, 6);
    REQUIRE(f.size() == 44);
    appendWavFrames(f, WavFormat::Pcm16, l, r);
    REQUIRE(f.size() == 44 + 24);
    CHECK(str(f, 0, 4) == "RIFF");
    CHECK(rd32(f, 4) == 36 + 24);  // the prototype wrote 36 + 12
    CHECK(str(f, 8, 8) == "WAVEfmt ");
    CHECK(rd32(f, 16) == 16);
    CHECK(rd16(f, 20) == 1);
    CHECK(rd16(f, 22) == 2);
    CHECK(rd32(f, 24) == 48000);
    CHECK(rd32(f, 28) == 192000);
    CHECK(rd16(f, 32) == 4);
    CHECK(rd16(f, 34) == 16);
    CHECK(str(f, 36, 4) == "data");
    CHECK(rd32(f, 40) == 24);
    const int16_t expect[] = {32767, 0, -32767, 8191, 16383, -32767, -16383, 0, 32767, -32734, 0, 3276};
    for (size_t i = 0; i < 12; ++i) {
        CAPTURE(i);
        CHECK(int16_t(rd16(f, 44 + 2 * i)) == expect[i]);
    }
}

TEST_CASE("24-bit and 32-bit float WAV", "[wav]") {
    const float l[] = {0.5f, -1.0f}, r[] = {-0.25f, 0.75f};
    std::vector<uint8_t> f = wavHeader(WavFormat::Pcm24, 44100, 2);
    appendWavFrames(f, WavFormat::Pcm24, l, r);
    REQUIRE(f.size() == 44 + 12);
    CHECK(rd32(f, 4) == 36 + 12);
    CHECK(rd32(f, 28) == 44100 * 6);
    CHECK(rd16(f, 32) == 6);
    CHECK(rd16(f, 34) == 24);
    auto s24 = [&f](size_t i) {
        const uint32_t v = uint32_t(f[44 + 3 * i]) | uint32_t(f[45 + 3 * i]) << 8 | uint32_t(f[46 + 3 * i]) << 16;
        return int32_t(v << 8) >> 8;
    };
    CHECK(s24(0) == 4194303);
    CHECK(s24(1) == -2097151);
    CHECK(s24(2) == -8388607);
    CHECK(s24(3) == 6291455);

    std::vector<uint8_t> g = wavHeader(WavFormat::Float32, 96000, 2);
    REQUIRE(g.size() == 58);
    appendWavFrames(g, WavFormat::Float32, l, r);
    REQUIRE(g.size() == 58 + 16);
    CHECK(rd32(g, 4) == 50 + 16);
    CHECK(rd32(g, 16) == 18);
    CHECK(rd16(g, 20) == 3);
    CHECK(rd16(g, 32) == 8);
    CHECK(rd16(g, 34) == 32);
    CHECK(rd16(g, 36) == 0);
    CHECK(str(g, 38, 4) == "fact");
    CHECK(rd32(g, 42) == 4);
    CHECK(rd32(g, 46) == 2);
    CHECK(str(g, 50, 4) == "data");
    CHECK(rd32(g, 54) == 16);
    float x = 0;
    std::memcpy(&x, g.data() + 58 + 12, 4);
    CHECK(x == 0.75f);

    // Limit of the format: the RIFF size fits in 32 bits.
    CHECK(wavMaxFrames(WavFormat::Pcm16) == (0xFFFFFFFFull - 36) / 4);
    CHECK(wavMaxFrames(WavFormat::Pcm24) == (0xFFFFFFFFull - 36) / 6);
    CHECK(wavMaxFrames(WavFormat::Float32) == (0xFFFFFFFFull - 50) / 8);
}

TEST_CASE("WavWriter: streaming write, header completed at the end", "[wav]") {
    std::vector<float> l(1000), r(1000);
    for (size_t i = 0; i < l.size(); ++i) {
        l[i] = float(std::sin(double(i) * 0.01));
        r[i] = float(std::cos(double(i) * 0.02)) * 0.5f;
    }
    for (const WavFormat fmt : {WavFormat::Pcm16, WavFormat::Pcm24, WavFormat::Float32}) {
        MemorySink sink;
        WavWriter w(sink, fmt, 22050);
        for (size_t at = 0; at < l.size(); at += 333) {
            const size_t n = std::min<size_t>(333, l.size() - at);
            REQUIRE(w.write(std::span(l).subspan(at, n), std::span(r).subspan(at, n)));
        }
        REQUIRE(w.finish());
        CHECK(w.frames() == 1000);
        CHECK_FALSE(w.full());
        std::vector<uint8_t> expect = wavHeader(fmt, 22050, 1000);
        appendWavFrames(expect, fmt, l, r);
        CHECK(sink.bytes == expect);
        CHECK(sink.position() == expect.size());
    }
}

TEST_CASE("MemorySink: bounded seek back, final failure", "[wav]") {
    MemorySink s;
    const uint8_t abc[] = {1, 2, 3};
    REQUIRE(s.write(abc));
    REQUIRE(s.seek(1));
    const uint8_t x[] = {9};
    REQUIRE(s.write(x));
    CHECK(s.bytes == std::vector<uint8_t>{1, 9, 3});
    CHECK(s.position() == 2);
    CHECK_FALSE(s.seek(4));
    CHECK_FALSE(s.ok());
    CHECK_FALSE(s.write(abc));
    CHECK_FALSE(s.commit());
}

TEST_CASE("ZipWriter: stored entries, prefix written at the end, central directory", "[wav]") {
    MemorySink sink;
    ZipWriter z(sink);
    const std::vector<uint8_t> body1 = bytesOf("hello stems"), prefix = bytesOf("HEAD"), body2(70000, 0x5A);
    REQUIRE(z.begin("stym2149-track-01.wav"));
    REQUIRE(z.write(body1));
    REQUIRE(z.end());
    REQUIRE(z.begin("stym2149-track-A.wav", prefix.size()));
    REQUIRE(z.write(std::span(body2).first(1000)));
    REQUIRE(z.write(std::span(body2).subspan(1000)));
    CHECK_FALSE(z.end(bytesOf("XY")));  // prefix of the wrong size
    REQUIRE(z.end(prefix));
    REQUIRE(z.finish());
    const std::vector<uint8_t>& b = sink.bytes;

    // Read like a ZIP reader: end of directory, central directory, then each local header.
    REQUIRE(b.size() > 22);
    const size_t eocd = b.size() - 22;
    REQUIRE(rd32(b, eocd) == 0x06054B50u);
    CHECK(rd16(b, eocd + 8) == 2);
    CHECK(rd16(b, eocd + 10) == 2);
    const size_t cd = rd32(b, eocd + 16);
    CHECK(rd32(b, eocd + 12) == eocd - cd);
    std::vector<uint8_t> whole2 = prefix;
    whole2.insert(whole2.end(), body2.begin(), body2.end());
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> want = {
        {"stym2149-track-01.wav", body1}, {"stym2149-track-A.wav", whole2}};
    size_t at = cd;
    for (const auto& [name, data] : want) {
        CAPTURE(name);
        REQUIRE(rd32(b, at) == 0x02014B50u);
        CHECK(rd16(b, at + 8) == 1u << 11);
        CHECK(rd16(b, at + 10) == 0);  // stored
        CHECK(rd16(b, at + 14) == 0x21);
        const uint32_t crc = rd32(b, at + 16), size = rd32(b, at + 20);
        CHECK(crc == crc32(data));
        CHECK(size == data.size());
        CHECK(rd32(b, at + 24) == data.size());
        const size_t nameLen = rd16(b, at + 28);
        CHECK(str(b, at + 46, nameLen) == name);
        const size_t local = rd32(b, at + 42);
        REQUIRE(rd32(b, local) == 0x04034B50u);
        CHECK(rd32(b, local + 14) == crc);
        CHECK(rd32(b, local + 18) == size);
        CHECK(rd32(b, local + 22) == size);
        CHECK(str(b, local + 30, rd16(b, local + 26)) == name);
        const size_t dataAt = local + 30 + rd16(b, local + 26) + rd16(b, local + 28);
        CHECK(std::vector<uint8_t>(b.begin() + ptrdiff_t(dataAt), b.begin() + ptrdiff_t(dataAt + size)) == data);
        at += 46 + nameLen;
    }
    CHECK(at == eocd);
    // Entry not closed: no directory.
    MemorySink s2;
    ZipWriter z2(s2);
    REQUIRE(z2.begin("a"));
    CHECK_FALSE(z2.begin("b"));
    CHECK_FALSE(z2.finish());
}
