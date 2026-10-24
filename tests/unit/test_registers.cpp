// STYM2149 — S8: registers, mixer, zero periods, envelope restart
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "ymcore/Ym2149.h"

using namespace stym::ymcore;

namespace {
std::vector<int> run(Ym2149& chip, int ticks) {
    std::vector<int> out(size_t(ticks), 0);
    for (auto& v : out) v = chip.tick();
    return out;
}
}  // namespace

TEST_CASE("S8 width of each register", "[S8][registers]") {
    // YM2149 datasheet p. 4: R1/R3/R5 4 bits, R6 5 bits, R7 6 useful bits, R8-R10 5 bits, R13 4 bits.
    const uint8_t expected[16] = {0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0x3f, 0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff};
    Ym2149 chip;
    for (unsigned r = 0; r < 16; ++r) {
        chip.writeRegister(r, 0xff);
        CHECK(chip.readRegister(r) == expected[r]);
    }
    chip.writeRegister(16, 0x55);  // out of range: ignored
    CHECK(chip.readRegister(16) == 0);
}

TEST_CASE("S8 power-on state", "[S8][registers]") {
    Ym2149 chip;
    const Ym2149::State s = chip.state();
    for (unsigned r = 0; r < 16; ++r) CHECK(s.regs[r] == (r == 7 ? 0x3f : 0));
    CHECK(s.lfsr == 1);
    CHECK(s.envPos == 0);
    CHECK(s.envShape == 0);
    // First draw of rand() (seed 1) = 41: channels A and B high, C low (like AtariAudio).
    CHECK(s.toneEdges == 0x3ffu);
    // Output at rest: volumes at 0 → level 1 on each channel (2v + 1, D-011).
    CHECK(chip.tick() == tables::kMixTable[1 | (1 << 5) | (1 << 10)]);
}

TEST_CASE("S8 same seed, same state; different seed, another state possible", "[S8][registers]") {
    ChipConfig c;
    c.edgeSeed = 1;
    Ym2149 a(c), b(c);
    CHECK(a.state().toneEdges == b.state().toneEdges);
    a.reset();
    CHECK(a.state().toneEdges == b.state().toneEdges);  // reset() draws the same value again (D-019)
    c.edgeSeed = 2745024u;  // state of the generator after one draw: second Reset of AtariAudio
    Ym2149 d(c);
    CHECK(d.state().toneEdges == ((18467u & 0x421u) * 0x1fu));  // second MSVC rand() = 18467
}

TEST_CASE("S8 inverted mixer: source off = constant high level", "[S8][mixer]") {
    Ym2149 chip;
    chip.writeRegister(7, 0x3f);  // everything off
    chip.writeRegister(8, 15);
    chip.writeRegister(0, 1);     // fast tone, but off
    const int expected = tables::kMixTable[31 | (1 << 5) | (1 << 10)];
    for (int v : run(chip, 1000)) REQUIRE(v == expected);
    CHECK((chip.levels() & 31) == 31);
}

TEST_CASE("S8 tone on: square between 0 and the level", "[S8][mixer]") {
    Ym2149 chip;
    chip.writeRegister(9, 0);
    chip.writeRegister(10, 0);
    chip.writeRegister(7, 0x3e);  // tone A alone
    chip.writeRegister(8, 15);
    chip.writeRegister(0, 10);
    const int hi = tables::kMixTable[31 | (1 << 5) | (1 << 10)];
    const int lo = tables::kMixTable[0 | (1 << 5) | (1 << 10)];
    const auto out = run(chip, 200);
    for (int v : out) REQUIRE((v == hi || v == lo));
    // 10 steps high, 10 steps low (the first plateau depends on the edges drawn at reset).
    int changes = 0;
    for (size_t i = 1; i < out.size(); ++i) changes += out[i] != out[i - 1];
    CHECK(changes >= 18);
    CHECK(changes <= 20);
}

TEST_CASE("S8 period 0 behaves like 1 (tone, noise, envelope)", "[S8][registers]") {
    auto render = [](uint8_t tp, uint8_t np, uint8_t ep) {
        Ym2149 chip;
        chip.writeRegister(0, tp);
        chip.writeRegister(6, np);
        chip.writeRegister(11, ep);
        chip.writeRegister(7, 0x36);  // tone A + noise A
        chip.writeRegister(8, 0x10);  // A on the envelope
        chip.writeRegister(13, 0x0e);
        return run(chip, 20000);
    };
    CHECK(render(0, 0, 0) == render(1, 1, 1));
    CHECK(render(0, 0, 0) != render(2, 2, 2));
}

TEST_CASE("S8 R13 restarts the envelope even with the same value", "[S8][envelope]") {
    Ym2149 chip;
    chip.writeRegister(11, 4);
    chip.writeRegister(13, 0x0d);
    run(chip, 1000);
    CHECK(chip.state().envPos != -64);
    chip.writeRegister(13, 0x0d);
    CHECK(chip.state().envPos == -64);
    CHECK(chip.state().envCounter == 0);
    // Writing R11/R12 does not restart it.
    run(chip, 100);
    const int pos = chip.state().envPos;
    chip.writeRegister(11, 4);
    CHECK(chip.state().envPos == pos);
}

TEST_CASE("S8 R14/R15: read back, no effect on the sound", "[S8][registers]") {
    Ym2149 a, b;
    a.writeRegister(14, 0xa5);
    a.writeRegister(15, 0x5a);
    CHECK(a.readRegister(14) == 0xa5);
    CHECK(a.readRegister(15) == 0x5a);
    CHECK(run(a, 5000) == run(b, 5000));
}

TEST_CASE("S8 mute per voice", "[S8][mixer]") {
    Ym2149 chip;
    chip.writeRegister(8, 15);
    chip.writeRegister(9, 15);
    chip.setVoiceMute(1);  // A muted
    chip.tick();
    CHECK((chip.levels() & 31) == 0);
    CHECK(((chip.levels() >> 5) & 31) == 31);
    chip.setVoiceMute(0);
    chip.tick();
    CHECK((chip.levels() & 31) == 31);
}

TEST_CASE("S8 fixed volume v → level 2v + 1 (D-011)", "[S8][registers]") {
    Ym2149 chip;
    for (uint8_t v = 0; v < 16; ++v) {
        chip.writeRegister(8, v);
        chip.tick();
        CHECK((chip.levels() & 31) == uint32_t(2 * v + 1));
    }
}

// Command of ymio::SndhCapture ("square-sync" of AtariAudio): edge inverted, counter reset.
TEST_CASE("S8 tone edge reset", "[S8][registers]") {
    Ym2149 chip;
    chip.writeRegister(2, 100);
    run(chip, 37);
    const Ym2149::State before = chip.state();
    REQUIRE(before.toneCounter[1] == 37);
    chip.writeRegister(Ym2149::kToneEdgeReset, 1);
    const Ym2149::State after = chip.state();
    CHECK(after.toneEdges == (before.toneEdges ^ (0x1fu << 5)));
    CHECK(after.toneCounter[1] == 0);
    CHECK(after.toneCounter[0] == before.toneCounter[0]);
    for (unsigned r = 0; r < 16; ++r) CHECK(after.regs[r] == before.regs[r]);
    chip.writeRegister(Ym2149::kToneEdgeReset, 3);  // unknown voice: ignored
    CHECK(chip.state().toneEdges == after.toneEdges);
}
