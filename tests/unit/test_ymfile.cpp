// STYM2149 — reading YM files and rules of the frame player
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <string>
#include <vector>

#include "ymio/YmFile.h"
#include "ymio/YmPlayer.h"

using namespace stym;
using ymio::YmFormat;

namespace {
using Bytes = std::vector<uint8_t>;

void be16(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
}
void be32(Bytes& b, uint32_t v) {
    be16(b, v >> 16);
    be16(b, v & 0xffff);
}
void str(Bytes& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

// Test value of register r at frame t.
uint8_t pattern(uint32_t t, unsigned r) { return uint8_t(t * 16 + r * 7 + 1); }

Bytes ym23(const char* id, uint32_t frames, int loop = -1) {
    Bytes b;
    b.reserve(8 + 14 * size_t(frames));  // reserve: avoids a false positive of -Wstringop-overflow (GCC 13)
    b.insert(b.end(), id, id + 4);
    for (unsigned r = 0; r < 14; ++r)
        for (uint32_t t = 0; t < frames; ++t) b.push_back(pattern(t, r));
    if (loop >= 0) {
        for (int i = 0; i < 4; ++i) b.push_back(uint8_t(uint32_t(loop) >> (8 * i)));
    }
    return b;
}

Bytes ym56(const char* id, uint32_t frames, bool interleaved, uint32_t clock = 2000000, uint32_t rate = 50, uint32_t loop = 0,
           std::vector<Bytes> drums = {}) {
    Bytes b;
    b.reserve(128 + 16 * size_t(frames) + drums.size() * 16);  // see ym23
    b.insert(b.end(), id, id + 4);
    const char* sig = "LeOnArD!";
    b.insert(b.end(), sig, sig + 8);
    be32(b, frames);
    be32(b, interleaved ? 1 : 0);
    be16(b, uint32_t(drums.size()));
    be32(b, clock);
    be16(b, rate);
    be32(b, loop);
    be16(b, 2);
    b.push_back(0xaa);
    b.push_back(0xbb);
    for (const Bytes& d : drums) {
        be32(b, uint32_t(d.size()));
        b.insert(b.end(), d.begin(), d.end());
    }
    str(b, "Titre");
    str(b, "Auteur");
    str(b, "Commentaire");
    if (interleaved) {
        for (unsigned r = 0; r < 16; ++r)
            for (uint32_t t = 0; t < frames; ++t) b.push_back(pattern(t, r));
    } else {
        for (uint32_t t = 0; t < frames; ++t)
            for (unsigned r = 0; r < 16; ++r) b.push_back(pattern(t, r));
    }
    const char* end = "End!";
    b.insert(b.end(), end, end + 4);
    return b;
}

bool framesMatch(const ymio::YmFile& f, unsigned regs) {
    for (uint32_t t = 0; t < f.frameCount; ++t)
        for (unsigned r = 0; r < regs; ++r)
            if (f.reg(t, r) != pattern(t, r)) return false;
    return true;
}
}  // namespace

TEST_CASE("YM2/YM3/YM3b: de-interleaving, loop", "[ymio]") {
    auto a = ymio::parseYm(ym23("YM3!", 37));
    REQUIRE(a.file);
    CHECK(a.file->format == YmFormat::YM3);
    CHECK(a.file->frameCount == 37);
    CHECK(a.file->playerRate == 50);
    CHECK(a.file->clock.num == 2000000);
    CHECK(framesMatch(*a.file, 14));

    auto b = ymio::parseYm(ym23("YM3b", 37, 12));
    REQUIRE(b.file);
    CHECK(b.file->format == YmFormat::YM3b);
    CHECK(b.file->frameCount == 37);
    CHECK(b.file->loopFrame == 12);
    CHECK(framesMatch(*b.file, 14));

    auto c = ymio::parseYm(ym23("YM2!", 5));
    REQUIRE(c.file);
    CHECK(c.file->format == YmFormat::YM2);

    auto d = ymio::parseYm(ym23("YM3b", 37, 99));  // loop outside the tune → 0, like AtariAudio
    REQUIRE(d.file);
    CHECK(d.file->loopFrame == 0);
}

TEST_CASE("YM5/YM6: header, digidrums, strings, interleaved or not", "[ymio]") {
    for (bool inter : {true, false}) {
        auto r = ymio::parseYm(ym56("YM6!", 300, inter, 1773400, 60, 17, {Bytes(5, 0x80), Bytes(3, 0x40)}));
        REQUIRE(r.file);
        const ymio::YmFile& f = *r.file;
        CHECK(f.format == YmFormat::YM6);
        CHECK(f.frameCount == 300);
        CHECK(f.clock.num == 1773400);
        CHECK(f.playerRate == 60);
        CHECK(f.loopFrame == 17);
        CHECK(f.title == "Titre");
        CHECK(f.author == "Auteur");
        CHECK(f.comment == "Commentaire");
        REQUIRE(f.digidrums.size() == 2);
        CHECK(f.digidrums[0].size() == 5);
        CHECK(f.digidrums[1].size() == 3);
        CHECK(framesMatch(f, 16));
    }
    auto y5 = ymio::parseYm(ym56("YM5!", 10, true));
    REQUIRE(y5.file);
    CHECK(y5.file->format == YmFormat::YM5);
}

TEST_CASE("YM: errors detected without out-of-bounds reads", "[ymio]") {
    CHECK_FALSE(ymio::parseYm(Bytes{}).file);
    CHECK_FALSE(ymio::parseYm(Bytes{'Y', 'M', '7', '!', 0, 0, 0, 0}).file);
    CHECK_FALSE(ymio::parseYm(Bytes{'Y', 'M', '3', '!'}).file);  // no frames
    Bytes full = ym56("YM6!", 100, true);
    for (size_t cut : {size_t(5), size_t(13), size_t(30), size_t(40), full.size() - 20}) {
        Bytes t(full.begin(), full.begin() + long(cut));
        INFO("truncated at " << cut);
        CHECK_FALSE(ymio::parseYm(t).file);
    }
    Bytes noSig = full;
    noSig[6] = 'X';
    CHECK_FALSE(ymio::parseYm(noSig).file);
    // Empty level 0 LHA header: decompressed (0 bytes), then refused as a YM file that is too short.
    Bytes lha(64, 0);
    lha[0] = 0x1a;
    const char* m = "-lh5-";
    std::copy(m, m + 5, lha.begin() + 2);
    const auto r = ymio::parseYm(lha);
    CHECK_FALSE(r.file);
    CHECK(r.error.find("LHA") != std::string::npos);
}

TEST_CASE("Player: AtariAudio rate and exact rate", "[ymio][player]") {
    auto r = ymio::parseYm(ym23("YM3!", 100));
    REQUIRE(r.file);
    ymio::YmPlayer aa(*r.file, ymio::YmTiming::AtariAudio, 48000);
    CHECK(aa.samplesPerFrameAtariAudio() == 958);  // 48 000 × 313 × 512 × 50 / (50 × 8 021 247)
    CHECK(aa.songLengthSamples() == 95800);
    ymio::YmPlayer exact(*r.file, ymio::YmTiming::Exact, 48000);
    CHECK(exact.songLengthSamples() == 96000);
}

namespace {
// Single hand-picked frame, non-interleaved YM5 or YM6 format, optional digidrums.
Bytes oneFrame(const char* id, const uint8_t regs[16], std::vector<Bytes> drums = {}) {
    Bytes b = ym56(id, 1, false, 2000000, 50, 0, std::move(drums));
    const size_t data = b.size() - 4 - 16;
    for (unsigned r = 0; r < 16; ++r) b[data + r] = regs[r];
    return b;
}
Bytes oneFrameYm5(const uint8_t regs[16]) { return oneFrame("YM5!", regs); }
}  // namespace

TEST_CASE("Player: AtariAudio frame rules", "[ymio][player]") {
    ymcore::RenderConfig rc;
    rc.mode = ymcore::OutputMode::RawBox;
    std::vector<float> out(64);

    SECTION("Zero EP → R11 = R12 = $FF; R13 = $FF does not restart") {
        const uint8_t regs[16] = {1, 0, 2, 0, 3, 0, 4, 0x38, 0x10, 0, 0, 0, 0, 0xff, 0, 0};
        auto f = ymio::parseYm(oneFrameYm5(regs));
        REQUIRE(f.file);
        ymcore::ChipRenderer r;
        r.prepare(rc);
        ymio::YmPlayer p(*f.file, ymio::YmTiming::AtariAudio, rc.hostRate);
        p.render(r, out.data(), 16);
        CHECK(r.chip().readRegister(11) == 0xff);
        CHECK(r.chip().readRegister(12) == 0xff);
        CHECK(r.chip().readRegister(13) == 0);  // never written
        CHECK(r.chip().readRegister(7) == 0x38);
        CHECK(p.ignoredEffectFrames() == 0);
    }
    SECTION("YM5 SID (bits 4-5 of R1): the channel volume belongs to the timer") {
        // Zero prescaler (R6 = 0): timer stopped, R8 is written neither by the frame nor by an interrupt.
        const uint8_t regs[16] = {1, 0x10, 2, 0, 3, 0, 4, 0x38, 0x0f, 0, 0, 0x10, 0, 0x0a, 0, 0};
        auto f = ymio::parseYm(oneFrameYm5(regs));
        REQUIRE(f.file);
        ymcore::ChipRenderer r;
        r.prepare(rc);
        ymio::YmPlayer p(*f.file, ymio::YmTiming::AtariAudio, rc.hostRate);
        p.render(r, out.data(), 16);
        CHECK(p.effectFrames() == 1);
        CHECK(p.ignoredEffectFrames() == 0);
        CHECK(p.timerInterrupts() == 0);
        CHECK(r.chip().readRegister(8) == 0);
        CHECK(r.chip().readRegister(13) == 0x0a);
    }
    SECTION("YM6 Sinus-SID (code $80): counted, not played, volume written by the frame") {
        const uint8_t regs[16] = {1, 0x90, 2, 0, 3, 0, 0x20, 0x38, 0x0f, 0, 0, 0x10, 0, 0x0a, 100, 0};
        auto f = ymio::parseYm(oneFrame("YM6!", regs));
        REQUIRE(f.file);
        ymcore::ChipRenderer r;
        r.prepare(rc);
        ymio::YmPlayer p(*f.file, ymio::YmTiming::AtariAudio, rc.hostRate);
        p.render(r, out.data(), 64);
        CHECK(p.ignoredEffectFrames() == 1);
        CHECK(p.timerInterrupts() == 0);
        CHECK(r.chip().readRegister(8) == 0x0f);
    }
    SECTION("YM2: R13 ≠ $FF → R12 = 0 and shape $A") {
        Bytes b = ym23("YM2!", 1);
        // Interleaved frame 0: byte 4 + r.
        b[4 + 11] = 0x33;
        b[4 + 12] = 0x44;
        b[4 + 13] = 0x01;
        auto f = ymio::parseYm(b);
        REQUIRE(f.file);
        ymcore::ChipRenderer r;
        r.prepare(rc);
        ymio::YmPlayer p(*f.file, ymio::YmTiming::AtariAudio, rc.hostRate);
        p.render(r, out.data(), 16);
        CHECK(r.chip().readRegister(11) == 0x33);
        CHECK(r.chip().readRegister(12) == 0);
        CHECK(r.chip().readRegister(13) == 0x0a);
    }
}

TEST_CASE("Player: the loop resumes at the loop frame", "[ymio][player]") {
    auto f = ymio::parseYm(ym23("YM3b", 4, 2));
    REQUIRE(f.file);
    ymcore::RenderConfig rc;
    rc.mode = ymcore::OutputMode::RawBox;
    ymcore::ChipRenderer r;
    r.prepare(rc);
    ymio::YmPlayer p(*f.file, ymio::YmTiming::AtariAudio, rc.hostRate);
    std::vector<float> out(958);
    // Frames played: 0, 1, 2, 3, 2, 3, 2…
    const uint32_t expected[] = {0, 1, 2, 3, 2, 3, 2};
    for (uint32_t t : expected) {
        p.render(r, out.data(), 958);
        CHECK(r.chip().readRegister(0) == pattern(t, 0));
    }
}

namespace {
// RawBox rendering at 250 kHz, 2 MHz clock: one sample per internal step. observe(n) is called after n steps,
// once the writes due before the next step are applied: the state then reflects everything that falls before
// master cycle 8n.
template <class F>
void stepTicks(ymio::YmPlayer& p, ymcore::ChipRenderer& r, uint32_t ticks, F&& observe) {
    float s = 0;
    for (uint32_t n = 1; n <= ticks; ++n) {
        p.render(r, &s, 1);
        r.flushDue();
        observe(n);
    }
}

ymcore::RenderConfig tickRateConfig() {
    ymcore::RenderConfig rc;
    rc.mode = ymcore::OutputMode::RawBox;
    rc.hostRate = 250000;
    return rc;
}

// Master cycle of interrupt k (k ≥ 1) of a timer started at cycle 0 with a period of P MFP cycles:
// first 2 MHz cycle at or after time k × P / 2 457 600 s, that is ceil(k × P × 625 / 768).
uint64_t irqCycle(uint64_t k, uint64_t periodMfp) { return (k * periodMfp * 625 + 767) / 768; }

// Interrupts that fell before master cycle 8n.
uint64_t irqsBefore(uint32_t n, uint64_t periodMfp) {
    uint64_t k = 0;
    while (irqCycle(k + 1, periodMfp) < 8ull * n) ++k;
    return k;
}
}  // namespace

TEST_CASE("Player, exact effects: SID timed at the MFP cycle", "[ymio][player][effects]") {
    // YM5, slot 1: SID on channel A (R1 bits 4-5), ÷4 (R6 bits 5-7 = 1), data 100 (R14):
    // 400 MFP cycles, that is 325.52 master cycles per interrupt.
    const uint8_t regs[16] = {1, 0x10, 2, 0, 3, 0, 0x20, 0x3e, 0x0f, 0, 0, 0, 0, 0xff, 100, 0};
    auto f = ymio::parseYm(oneFrameYm5(regs));
    REQUIRE(f.file);
    const ymcore::RenderConfig rc = tickRateConfig();
    ymcore::ChipRenderer r;
    r.prepare(rc);
    ymio::YmPlayer p(*f.file, ymio::YmTiming::Exact, rc.hostRate);
    uint64_t bad = 0;
    stepTicks(p, r, 4000, [&](uint32_t n) {
        // Interrupt k: volume 15 if k is odd, 0 otherwise; 0 before the first one (R8 never written by the frame).
        const uint64_t k = irqsBefore(n, 400);
        bad += r.chip().readRegister(8) != ((k & 1) ? 15 : 0);
        bad += p.timerInterrupts() != k;
    });
    CHECK(bad == 0);
    CHECK(p.timerInterrupts() == irqsBefore(4000, 400));
    CHECK(p.timerInterrupts() > 90);
}

TEST_CASE("Player, exact effects: bounded and reproducible interrupt jitter", "[ymio][player][effects]") {
    const uint8_t regs[16] = {1, 0x10, 2, 0, 3, 0, 0x20, 0x3e, 0x0f, 0, 0, 0, 0, 0xff, 100, 0};
    auto f = ymio::parseYm(oneFrameYm5(regs));
    REQUIRE(f.file);
    const ymcore::RenderConfig rc = tickRateConfig();
    // 96 68000 cycles = at most 24 master cycles of delay.
    auto transitions = [&](uint32_t jitterCpu, uint64_t seed) {
        ymcore::ChipRenderer r;
        r.prepare(rc);
        ymio::YmPlayer p(*f.file, ymio::YmPlayerOptions{ymio::YmTiming::Exact, ymio::YmFxModel::Exact, jitterCpu, seed},
                         rc.hostRate);
        std::vector<uint32_t> at;
        uint8_t last = 0;
        stepTicks(p, r, 4000, [&](uint32_t n) {
            const uint8_t v = r.chip().readRegister(8);
            if (v != last) at.push_back(n);
            last = v;
        });
        return at;
    };
    const std::vector<uint32_t> none = transitions(0, 1), j1 = transitions(96, 7), j2 = transitions(96, 7);
    REQUIRE(j1.size() == none.size());
    CHECK(j1 == j2);
    uint64_t outOfBounds = 0, moved = 0;
    for (size_t m = 0; m < j1.size(); ++m) {
        // Write between cycle c and cycle c + 24: visible at the earliest after floor(c / 8) + 1 steps.
        const uint64_t c = irqCycle(m + 1, 400);
        outOfBounds += j1[m] < c / 8 + 1 || j1[m] > (c + 24) / 8 + 1;
        moved += j1[m] != none[m];
    }
    CHECK(outOfBounds == 0);
    CHECK(moved > 0);
}

TEST_CASE("Player, exact effects: YM5 digidrum played to the end, then timer stopped", "[ymio][player][effects]") {
    // Slot 2 (YM5: always a digidrum) on channel C (R3 bits 4-5 = 3), sample R10 & 31 = 0,
    // ÷10 (R8 bits 5-7 = 2), data 50 (R15): 500 MFP cycles per interrupt.
    const uint8_t regs[16] = {1, 0, 2, 0x30, 3, 0, 0, 0x38, 0x40, 0, 0, 0, 0, 0xff, 0, 50};
    // 8-bit sample: the 4 most significant bits go through Mad Max's table.
    const Bytes drum = {0x10, 0x00, 0x30, 0x00, 0xf0, 0x40};
    const uint8_t expected[6] = {0x7, 0x0, 0xa, 0x0, 0xf, 0xb};
    auto f = ymio::parseYm(oneFrame("YM5!", regs, {drum}));
    REQUIRE(f.file);
    const ymcore::RenderConfig rc = tickRateConfig();
    ymcore::ChipRenderer r;
    r.prepare(rc);
    ymio::YmPlayer p(*f.file, ymio::YmTiming::Exact, rc.hostRate);
    uint64_t bad = 0;
    stepTicks(p, r, 2000, [&](uint32_t n) {
        const uint64_t k = irqsBefore(n, 500);
        // Interrupts 1 to 6: one sample each; the 7th stops the timer; R10 keeps the last value.
        const uint8_t want = k == 0 ? 0 : expected[std::min<uint64_t>(k, 6) - 1];
        bad += r.chip().readRegister(10) != want;
        bad += p.timerInterrupts() != std::min<uint64_t>(k, 7);
        // Channel C: tone and noise cut while the sample lasts (R7 | $24), written with the frame.
        bad += r.chip().readRegister(7) != 0x3c;
    });
    CHECK(bad == 0);
    CHECK(p.timerInterrupts() == 7);
}

TEST_CASE("Player, exact effects: YM6 Sync Buzzer restarts the envelope at each interrupt", "[ymio][player][effects]") {
    // Slot 1: Sync Buzzer ($C0) on channel A, shape = R8 & 15 = $C, ÷16 (R6 bits 5-7 = 3), data 48
    // (R14): 768 MFP cycles = 625 master cycles. EP = 4 095: the envelope takes no step between two restarts.
    const uint8_t regs[16] = {1, 0xd0, 2, 0, 3, 0, 0x60, 0x3e, 0x1c, 0, 0, 0xff, 0x0f, 0xff, 48, 0};
    auto f = ymio::parseYm(oneFrame("YM6!", regs));
    REQUIRE(f.file);
    const ymcore::RenderConfig rc = tickRateConfig();
    ymcore::ChipRenderer r;
    r.prepare(rc);
    ymio::YmPlayer p(*f.file, ymio::YmTiming::Exact, rc.hostRate);
    uint64_t bad = 0;
    stepTicks(p, r, 4000, [&](uint32_t n) {
        const uint64_t k = irqsBefore(n, 768);
        const ymcore::Ym2149::State st = r.chip().state();
        if (k == 0) {
            bad += r.chip().readRegister(13) != 0;  // R13 = $FF in the frame: never written
            return;
        }
        // Restart before step ceil(c / 8): n - ceil(c / 8) envelope steps counted since.
        const uint64_t c = irqCycle(k, 768);
        bad += r.chip().readRegister(13) != 0x0c;
        bad += st.envCounter != n - (c + 7) / 8;
        bad += st.envPos != -64;
    });
    CHECK(bad == 0);
    CHECK(p.timerInterrupts() == irqsBefore(4000, 768));
}

TEST_CASE("Player, exact effects: Mad Max YM2 digidrum", "[ymio][player][effects]") {
    Bytes b = ym23("YM2!", 1);
    b[4 + 7] = 0x38;
    b[4 + 10] = 0x80 | 21;  // sample 21 of the bank: 85 values (AtariAudio's ym2BankLens)
    b[4 + 12] = 50;         // ÷4, data R12: 200 MFP cycles per interrupt
    auto f = ymio::parseYm(b);
    REQUIRE(f.file);
    const ymcore::RenderConfig rc = tickRateConfig();
    ymcore::ChipRenderer r;
    r.prepare(rc);
    ymio::YmPlayer p(*f.file, ymio::YmTiming::Exact, rc.hostRate);
    uint64_t bad = 0;
    stepTicks(p, r, 3000, [&](uint32_t n) {
        bad += p.timerInterrupts() != std::min<uint64_t>(irqsBefore(n, 200), 86);  // 85 values, then the stop
        bad += r.chip().readRegister(7) != 0x3c;
    });
    CHECK(bad == 0);
    CHECK(p.timerInterrupts() == 86);
    CHECK(p.effectFrames() == 1);
}
