// STYM2149 — ymio: exported SNDH files: 68000 routine played by the AtariAudio machine (Musashi).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "../../src/ymio/src/Asm68k.h"
#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "plugincore/RegisterFiles.h"
#include "ymio/Sndh.h"
#include "ymio/SndhWriter.h"

using namespace stym;
using ymengine::DmaFrame;
using ymengine::Generator;
using ymengine::MfpSetting;
using ymengine::RegisterFrame;

namespace {

std::vector<uint16_t> words(const std::vector<uint8_t>& b) {
    std::vector<uint16_t> w;
    for (size_t i = 0; i + 1 < b.size(); i += 2) w.push_back(uint16_t(b[i] << 8 | b[i + 1]));
    return w;
}

ymio::SndhFile load(const std::vector<uint8_t>& data) {
    const ymio::SndhParseResult p = ymio::parseSndh(data);
    INFO(p.error);
    REQUIRE(p.file);
    return *p.file;
}

// A played frame: registers after the writes of the play (timed at its call), writes of the interrupts that follow
// up to the next play.
struct Played {
    std::array<int, 14> reg{};
    std::vector<std::pair<uint8_t, uint8_t>> irq;  // register, value
    std::vector<int16_t> dac;
    int r13Writes = 0;
};

std::vector<Played> play(const ymio::SndhFile& f, int subsong, uint32_t frames) {
    ymio::SndhCapture cap(f, subsong);
    REQUIRE(cap.ok());
    std::vector<ymio::SndhEvent> ev;
    REQUIRE(cap.run(uint64_t(cap.samplesPerTick()) * frames, ev));
    std::vector<Played> out;
    std::array<int, 14> reg;
    reg.fill(-1);
    uint64_t tick = 0;
    for (const ymio::SndhEvent& e : ev) {
        if (e.kind == ymio::SndhEvent::Kind::Tick) {
            out.emplace_back();
            out.back().reg = reg;
            tick = e.cycle;
            continue;
        }
        if (out.empty()) continue;  // init
        Played& p = out.back();
        if (e.kind == ymio::SndhEvent::Kind::Dac) {
            p.dac.push_back(e.dac);
            continue;
        }
        if (e.reg >= 14) continue;
        reg[e.reg] = e.value;
        if (e.cycle == tick) {
            p.reg[e.reg] = e.value;
            p.r13Writes += e.reg == 13;
        } else {
            p.irq.emplace_back(e.reg, e.value);
        }
    }
    return out;
}

RegisterFrame frame(uint32_t k) {
    RegisterFrame f;
    f.reg = {uint8_t(k * 3), uint8_t(k % 16), uint8_t(200 - k), 1, 0x40, 2, uint8_t(k % 32), 0x38, uint8_t(k % 16),
             9, 0x10, 0x34, 0x12, 0x0A};
    f.r13 = k % 10 == 0;
    return f;
}

}  // namespace

TEST_CASE("68000 assembler: encodings of the SNDH routine", "[sndhwriter]") {
    using namespace ymio::m68k;
    Asm a;
    const int l = a.label();
    a.lea(pcRel(l), 2);                                   // 45FA
    a.move(Size::L, an(3), dn(2));                         // 240B
    a.moveq(1, 1);                                         // 7201
    a.cmp(Size::L, dn(1), 0);                              // B081
    a.bind(l);
    a.adda(dn(0), 3);                                      // D7C0
    a.move(Size::L, postInc(11), dn(1));                   // 221B
    a.add(Size::L, dn(2), 1);                              // D282
    a.move(Size::L, dn(1), ind(10));                       // 2481
    a.move(Size::L, dn(1), disp(10, 8));                   // 2541 0008
    a.lea(absW(0x8800), 1);                                // 43F8 8800
    a.move(Size::B, imm(7), ind(9));                       // 12BC 0007
    a.move(Size::B, imm(0x3F), disp(9, 2));                // 137C 003F 0002
    a.move(Size::B, postInc(8), disp(9, 2));               // 1358 0002
    a.subq(Size::L, 1, disp(10, 4));                       // 53AA 0004
    a.move(Size::L, disp(10, 12), disp(10, 4));            // 256A 000C 0004
    a.move(Size::L, postInc(SP), an(2));                   // 245F
    a.move(Size::L, an(0), preDec(SP));                    // 2F08
    a.movepW(1, 1, 0);                                     // 0389 0000
    a.movemPush(0x0301);                                   // 48E7 80C0 (d0, a0, a1)
    a.movemPop(0x0301);                                    // 4CDF 0301
    a.move(Size::B, imm(0xDF), absW(0xFA0F));              // 11FC 00DF FA0F
    a.move(Size::L, an(3), absW(0x0134));                  // 21CB 0134
    a.bchg(0, disp(8, 9));                                 // 0868 0000 0009
    a.or_(Size::B, 1, absW(0xFA1D));                       // 8338 FA1D
    a.andi(Size::B, 0xF0, absW(0xFA1D));                   // 0238 00F0 FA1D
    a.btst(14, dn(4));                                     // 0804 000E
    a.moveFromSr(5);                                       // 40C5
    a.moveToSr(5);                                         // 46C5
    a.oriSr(0x0700);                                       // 007C 0700
    a.cmpa(disp(9, 4), 0);                                 // B1E9 0004
    a.lsl(Size::W, 8, 3);                                  // E14B
    a.lsr(Size::W, 1, 3);                                  // E24B
    a.lsl(Size::L, 4, 2);                                  // E98A
    a.addi(Size::W, 0x100, dn(0));                         // 0640 0100
    a.cmpi(Size::W, 0x700, dn(0));                         // 0C40 0700
    a.swap(3);                                             // 4843
    a.movepW(3, 12, 5);                                    // 078C 0005
    a.adda(dn(2), 3);                                      // D7C2
    a.move(Size::L, idx(11, 2), dn(3));                    // 2633 2000
    a.add(Size::L, idx(11, 2, 4), 4);                      // D8B3 2004
    a.move(Size::L, idx(11, 0, 0, true), dn(1));           // 2233 0800
    a.move(Size::B, pcRel(l, 3), dn(0));                   // 103A xxxx
    a.rte();                                               // 4E73
    a.rts();                                               // 4E75
    std::string error;
    REQUIRE(a.finish(error));
    const std::vector<uint16_t> want = {
        0x45FA, 0x0000, 0x240B, 0x7201, 0xB081, 0xD7C0, 0x221B, 0xD282, 0x2481, 0x2541, 0x0008, 0x43F8, 0x8800,
        0x12BC, 0x0007, 0x137C, 0x003F, 0x0002, 0x1358, 0x0002, 0x53AA, 0x0004, 0x256A, 0x000C, 0x0004, 0x245F,
        0x2F08, 0x0389, 0x0000, 0x48E7, 0x80C0, 0x4CDF, 0x0301, 0x11FC, 0x00DF, 0xFA0F, 0x21CB, 0x0134, 0x0868,
        0x0000, 0x0009, 0x8338, 0xFA1D, 0x0238, 0x00F0, 0xFA1D, 0x0804, 0x000E, 0x40C5, 0x46C5, 0x007C, 0x0700,
        0xB1E9, 0x0004, 0xE14B, 0xE24B, 0xE98A, 0x0640, 0x0100, 0x0C40, 0x0700, 0x4843, 0x078C, 0x0005, 0xD7C2,
        0x2633, 0x2000, 0xD8B3, 0x2004, 0x2233, 0x0800, 0x103A, 0x0000, 0x4E73, 0x4E75};
    std::vector<uint16_t> got = words(a.out);
    // PC-relative displacements: lea l(pc) from the extension word (2) to l (10); move.b l+3(pc).
    CHECK(got[1] == 8);
    const auto ext = got.size() - 3;
    CHECK(int16_t(got[ext]) == int16_t(10 + 3 - int(2 * ext)));
    got[1] = 0;
    got[ext] = 0;
    CHECK(got == want);

    // Branches: short (zero displacement forbidden), long, DBF; relative jump table.
    Asm b;
    const int top = b.label(), next = b.label(), table = b.label();
    b.bind(top);
    b.bccS(EQ, next);  // 6704
    b.dbf(2, top);           // 51CA FFFC
    b.bind(next);
    b.bcc(NE, top);    // 6600 FFF8
    b.move(Size::W, pcIdx(table, 0), dn(0));  // 303B 0006
    b.jmp(pcIdx(table, 0));                    // 4EFB 0002
    b.bind(table);
    b.dcwDiff(top, table);                     // FFEE
    REQUIRE(b.finish(error));
    CHECK(words(b.out) == std::vector<uint16_t>{0x6704, 0x51CA, 0xFFFC, 0x6600, 0xFFF8, 0x303B, 0x0006, 0x4EFB, 0x0002, 0xFFEE});
    Asm c;
    const int self = c.label();
    c.bind(self);
    c.braS(c.label());  // label never placed
    CHECK_FALSE(c.finish(error));
}

TEST_CASE("Exported SNDH: header and registers of each frame, subsongs, back to the start", "[sndhwriter]") {
    std::vector<RegisterFrame> song, pattern;
    for (uint32_t k = 0; k < 120; ++k) song.push_back(frame(k));
    for (uint32_t k = 0; k < 30; ++k) pattern.push_back(frame(k + 40));
    const ymio::SndhSubsong subs[2] = {{song, {}}, {pattern, {}}};
    ymio::SndhWriteInfo info;
    info.title = "Démo";
    info.composer = "Pascal \"Indus\" Nowak";
    const ymio::SndhWriteResult w = ymio::sndhFile(subs, info);
    INFO(w.error);
    REQUIRE(w.error.empty());
    const std::string bytes(w.data.begin(), w.data.end());
    CHECK(bytes.find("PRSM") == std::string::npos);
    CHECK(bytes.find("PRISM") == std::string::npos);
    CHECK(bytes.find("FLAG") == std::string::npos);
    CHECK(bytes.find("\x01\x02\x03\x04") == std::string::npos);

    const ymio::SndhFile f = load(w.data);
    CHECK(f.info.title == "D?mo");
    CHECK(f.info.composer == "Pascal \"Indus\" Nowak");
    CHECK(f.info.converter == "STYM2149");
    CHECK(f.info.subsongs == 2);
    CHECK(f.info.tickRate == 50);
    CHECK(f.info.timer == 'C');
    CHECK(f.info.frames == std::vector<uint32_t>{120, 30});

    const std::vector<Played> p = play(f, 1, 245);
    REQUIRE(p.size() == 245);
    for (uint32_t k = 0; k < 245; ++k) {
        CAPTURE(k);
        const RegisterFrame want = frame(k % 120);
        for (int r = 0; r < 13; ++r) {
            CAPTURE(r);
            CHECK(p[k].reg[size_t(r)] == (r == 7 ? want.reg[7] & 0x3F : want.reg[size_t(r)]));
        }
        CHECK(p[k].r13Writes == (want.r13 ? 1 : 0));
        if (want.r13) CHECK(p[k].reg[13] == 0x0A);
        CHECK(p[k].irq.empty());
        CHECK(p[k].dac.empty());
    }
    const std::vector<Played> q = play(f, 2, 31);
    for (uint32_t k = 0; k < 31; ++k) CHECK(q[k].reg[0] == frame(40 + k % 30).reg[0]);

    // Refused inputs.
    const ymio::SndhSubsong empty[1] = {{{}, {}}};
    CHECK_FALSE(ymio::sndhFile(empty, info).error.empty());
    CHECK_FALSE(ymio::sndhFile({}, info).error.empty());
    // 99 subsongs at most: the ## tag has two digits.
    const std::vector<RegisterFrame> one = {frame(0)};
    std::vector<ymio::SndhSubsong> many(size_t(ymio::kSndhWriteMaxSubsongs), ymio::SndhSubsong{one, {}});
    const ymio::SndhWriteResult m = ymio::sndhFile(many, info);
    REQUIRE(m.error.empty());
    CHECK(std::string(m.data.begin(), m.data.end()).find("##99") != std::string::npos);
    CHECK(load(m.data).info.subsongs == 99);
    many.push_back(ymio::SndhSubsong{one, {}});
    CHECK_FALSE(ymio::sndhFile(many, info).error.empty());
}

namespace {

// Channel A: SID; B: Sync Buzzer; C: Digidrum of slot 2; all start at frame 0 and stop at 50.
struct EffectsSong {
    std::vector<RegisterFrame> frames;
    std::vector<uint8_t> drum;
    MfpSetting sid{4, 49}, buzz{5, 40}, digi{1, 77};
    EffectsSong() {
        for (int k = 0; k < 100; ++k) drum.push_back(uint8_t((k * 7 + 3) % 16));
        for (uint32_t k = 0; k < 70; ++k) {
            RegisterFrame f;
            f.reg = {0x40, 0, 0x80, 0, 0, 0, 0, 0x3E, 12, 0x10, 5, 0x20, 0, 0x0A};
            if (k < 50) {
                f.fx[0] = {true, Generator::Sid, uint8_t(k < 25 ? 12 : 9), k < 30 ? sid : MfpSetting{4, 98}, k == 0};
                f.fx[1] = {true, Generator::SyncBuzzer, 10, buzz, k == 0};
                f.fx[2] = {true, Generator::Digidrum, 2, digi, k == 0};
            }
            f.r13 = k == 0;
            frames.push_back(f);
        }
    }
};

int countWrites(const Played& p, uint8_t reg) {
    return int(std::count_if(p.irq.begin(), p.irq.end(), [&](const auto& w) { return w.first == reg; }));
}

}  // namespace

TEST_CASE("Exported SNDH: timer effects played by MFP timers A, B and D", "[sndhwriter]") {
    EffectsSong s;
    const ymio::SndhSubsong sub[1] = {{s.frames, {}}};
    ymio::SndhWriteInfo info;
    info.digidrums[2] = s.drum;
    const ymio::SndhWriteResult w = ymio::sndhFile(sub, info);
    REQUIRE(w.error.empty());
    CHECK(w.report.droppedEffects == 0);
    const std::vector<Played> p = play(load(w.data), 1, 70);
    REQUIRE(p.size() == 70);

    // Frame 0: attack volumes written by the play (SID at the high level, digidrum at its first value).
    CHECK(p[0].reg[8] == 12);
    CHECK(p[0].reg[10] == s.drum[0]);
    CHECK(p[0].reg[13] == 10);

    // SID (channel A): 0 and 12 alternately, at 2 457 600 / (50 × 49) ≈ 1 003 Hz; 9 from frame 25; twice as slow
    // from frame 30 (new reload value).
    std::vector<uint8_t> sidVals;
    int sidCount = 0;
    for (int k = 0; k < 25; ++k)
        for (const auto& [reg, val] : p[size_t(k)].irq)
            if (reg == 8) {
                sidVals.push_back(val);
                ++sidCount;
            }
    REQUIRE(sidVals.size() > 10);
    for (size_t i = 0; i < sidVals.size(); ++i) CHECK(sidVals[i] == (i % 2 == 0 ? 0 : 12));
    const double sidHz = 2457600.0 / (50 * 49);
    CHECK(std::abs(sidCount - sidHz * 25 / 50) <= 2);
    for (const auto& [reg, val] : p[26].irq)
        if (reg == 8) CHECK((val == 0 || val == 9));
    CHECK(std::abs(countWrites(p[40], 8) - sidHz / 2 / 50) <= 1.5);

    // Sync Buzzer (channel B): R13 = 10 rewritten at 2 457 600 / (64 × 40) = 960 Hz.
    int buzz = 0;
    for (int k = 0; k < 50; ++k)
        for (const auto& [reg, val] : p[size_t(k)].irq)
            if (reg == 13) {
                CHECK(val == 10);
                ++buzz;
            }
    CHECK(std::abs(buzz - 960.0) <= 2);

    // Digidrum (channel C): values 1 to 99 of the sample, then volume 0, once.
    std::vector<uint8_t> drum;
    for (int k = 0; k < 70; ++k)
        for (const auto& [reg, val] : p[size_t(k)].irq)
            if (reg == 10) drum.push_back(val);
    std::vector<uint8_t> want(s.drum.begin() + 1, s.drum.end());
    want.push_back(0);
    CHECK(drum == want);

    // Stop at frame 50: no more interrupts; volumes restored to those of the frame.
    for (int k = 50; k < 70; ++k) CHECK(p[size_t(k)].irq.empty());
    CHECK(p[50].reg[8] == 12);
    CHECK(p[50].reg[10] == 5);
}

TEST_CASE("Exported SNDH: the SID interrupt gives D0 back whole to the program it interrupts", "[sndhwriter]") {
    // AtariAudio runs each handler on its own, so only the code shows it: the SID handler clears D0 with moveq
    // (all 32 bits) and must save and restore it as a long word, not only its low word.
    EffectsSong s;
    const ymio::SndhSubsong sub[1] = {{s.frames, {}}};
    ymio::SndhWriteInfo info;
    info.digidrums[2] = s.drum;
    const ymio::SndhWriteResult w = ymio::sndhFile(sub, info);
    REQUIRE(w.error.empty());
    const std::vector<uint8_t>& d = w.data;
    auto word = [&](size_t at) { return uint16_t(d[at] << 8 | d[at + 1]); };
    int handlers = 0;
    // bchg #0,(d16,a0); beq.s; moveq #0,d0: the phase toggle of a SID handler, 8 bytes after its first save.
    for (size_t at = 8; at + 10 <= d.size(); at += 2) {
        if (word(at) != 0x0868 || word(at + 2) != 0 || (word(at + 6) >> 8) != 0x67 || word(at + 8) != 0x7000) continue;
        ++handlers;
        CHECK(word(at - 8) == 0x2F00);  // move.l d0,-(sp)
        size_t rte = at;
        while (rte + 2 <= d.size() && word(rte) != 0x4E73) rte += 2;
        REQUIRE(rte + 2 <= d.size());
        CHECK(word(rte - 2) == 0x201F);  // move.l (sp)+,d0
    }
    CHECK(handlers >= 1);
}

TEST_CASE("Exported SNDH: STE DMA sound, copies at the pitch and volume of the attack", "[sndhwriter]") {
    std::vector<int8_t> pcm;
    for (int i = 0; i < 2000; ++i) pcm.push_back(int8_t((i * 7) % 200 - 100));
    std::vector<RegisterFrame> frames(40);
    for (RegisterFrame& f : frames) f.reg[7] = 0x3F;
    std::vector<DmaFrame> dma(40);
    for (int k = 0; k < 20; ++k) dma[size_t(k)] = {true, k == 0 || k == 10, 0, 1, k < 10 ? 1.0f : 2.0f, k < 10 ? 1.0f : 0.5f};
    dma[5].voices = 2;
    const ymio::SndhSubsong sub[1] = {{frames, dma}};
    ymio::SndhWriteInfo info;
    info.ste = true;
    info.dma[0] = {pcm, 2, false, true};
    const ymio::SndhWriteResult w = ymio::sndhFile(sub, info);
    REQUIRE(w.error.empty());
    CHECK(w.report.dmaCopies == 2);
    CHECK(w.report.dmaOverlap == 1);
    const std::vector<Played> p = play(load(w.data), 1, 25);

    // AtariAudio DAC levels: byte × 64 (STE master volume at maximum), one per DMA sample.
    std::vector<int16_t> first;
    for (int k = 0; k < 10; ++k) first.insert(first.end(), p[size_t(k)].dac.begin(), p[size_t(k)].dac.end());
    REQUIRE(first.size() > 100);
    for (size_t i = 0; i < 100; ++i) CHECK(first[i] == pcm[i] * 64);
    // An octave higher, half volume: one byte in two, divided by two.
    std::vector<int16_t> second;
    for (int k = 10; k < 20; ++k) second.insert(second.end(), p[size_t(k)].dac.begin(), p[size_t(k)].dac.end());
    REQUIRE(second.size() > 100);
    for (size_t i = 0; i < 100; ++i) CHECK(second[i] == int16_t(std::lround(pcm[2 * i] * 8 / 16.0) * 64));
    // Stop at frame 20: silence.
    CHECK(p[20].dac == std::vector<int16_t>{0});
    for (int k = 21; k < 25; ++k) CHECK(p[size_t(k)].dac.empty());

    // ST machine: no DMA in the routine, notes counted.
    info.ste = false;
    const ymio::SndhWriteResult st = ymio::sndhFile(sub, info);
    REQUIRE(st.error.empty());
    CHECK(st.report.dmaDropped == 2);
    CHECK(st.report.dmaCopies == 0);
    for (const Played& q : play(load(st.data), 1, 25)) CHECK(q.dac.empty());
}

TEST_CASE("Exported SNDH: STE DMA sound started at a sample offset (Oxx)", "[sndhwriter]") {
    std::vector<int8_t> pcm;
    for (int i = 0; i < 2000; ++i) pcm.push_back(int8_t((i * 7) % 200 - 100));
    std::vector<RegisterFrame> frames(30);
    for (RegisterFrame& f : frames) f.reg[7] = 0x3F;
    std::vector<DmaFrame> dma(30);
    for (int k = 0; k < 30; ++k) {
        dma[size_t(k)] = {true, k == 0 || k == 10 || k == 20, 0, 1, 1.0f, 1.0f};
        dma[size_t(k)].start = k < 10 ? 0 : k < 20 ? 512 : 2000;  // the third note starts past the end
    }
    const ymio::SndhSubsong sub[1] = {{frames, dma}};
    ymio::SndhWriteInfo info;
    info.ste = true;
    info.dma[0] = {pcm, 2, false, true};
    const ymio::SndhWriteResult w = ymio::sndhFile(sub, info);
    REQUIRE(w.error.empty());
    CHECK(w.report.dmaCopies == 2);
    const std::vector<Played> p = play(load(w.data), 1, 30);
    std::vector<int16_t> second;
    for (int k = 10; k < 20; ++k) second.insert(second.end(), p[size_t(k)].dac.begin(), p[size_t(k)].dac.end());
    REQUIRE(second.size() > 100);
    for (size_t i = 0; i < 100; ++i) CHECK(second[i] == pcm[512 + i] * 64);
    // Past the end: the DMA stops, as the engine plays nothing.
    CHECK(p[20].dac == std::vector<int16_t>{0});
    for (int k = 21; k < 30; ++k) CHECK(p[size_t(k)].dac.empty());
}

TEST_CASE("Exported SNDH: periods recomputed for the Atari's 2 MHz clock", "[sndhwriter]") {
    RegisterFrame f;
    f.reg = {0x10, 0x01, 0xFF, 0x0F, 0x01, 0, 20, 0x38, 15, 15, 15, 0x00, 0x90, 0x08};
    const std::vector<RegisterFrame> frames(3, f);
    const ymio::SndhSubsong sub[1] = {{frames, {}}};
    ymio::SndhWriteInfo info;
    info.clock = {1'000'000, 1};  // Amstrad CPC
    const ymio::SndhWriteResult w = ymio::sndhFile(sub, info);
    REQUIRE(w.error.empty());
    const Played p = play(load(w.data), 1, 1)[0];
    CHECK((p.reg[0] | p.reg[1] << 8) == 0x220);  // 0x110 × 2
    CHECK((p.reg[2] | p.reg[3] << 8) == 4095);   // 0xFFF × 2: brought back into range
    CHECK((p.reg[4] | p.reg[5] << 8) == 2);
    CHECK(p.reg[6] == 31);                       // 20 × 2: brought back to 31
    CHECK((p.reg[11] | p.reg[12] << 8) == 0xFFFF);  // 0x9000 × 2: brought back to 65 535
    CHECK(w.report.clampedPeriods == 3 * 3);
}

// End to end: pattern of an Authentic 3ch project on the STE machine captured (captureRegisters), written as SNDH,
// replayed by the AtariAudio machine: registers of each frame, SID on channel B, DMA sound at its attack frame.
TEST_CASE("SNDH of a captured pattern: registers, SID and DMA replayed by AtariAudio", "[sndhwriter][regexport]") {
    using namespace stym::plugin;
    ParamValues v = defaultValues();
    NonParamState x;
    v[size_t(paramIndex(Global::VoiceMode))] = 1;
    v[size_t(paramIndex(Global::FreeTempo))] = 120;
    v[size_t(paramIndex(Global::Computer))] = 1;  // STE
    v[size_t(paramIndex(1, Track::Generator))] = float(int(Generator::Sid));
    for (const int t : {1, 4}) v[size_t(paramIndex(t, Track::On))] = 1;
    x.seq.cur().steps = 16;
    x.seq.cur().edit(0).pat[0] = 0;  // C, then G at step 8 on channel A
    x.seq.cur().edit(0).pat[8] = 7;
    x.seq.cur().edit(1).pat[4] = 4;  // E on channel B (SID)
    x.seq.cur().edit(4).pat[12] = 0;  // DMA 1 at step 12 (1.5 s)
    std::vector<int8_t> pcm(3000);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = int8_t(int(i % 64) * 2 - 64);
    x.dmaSamples[0] = pcm;
    x.dma.slots[0].loop = false;
    const auto c = captureRegisters(OfflineRender::Source::Pattern, v, x);
    REQUIRE(c);
    const std::vector<RegisterFrame>& frames = c->take.frames;
    REQUIRE(frames.size() == 100);
    REQUIRE(c->dma.size() == 100);

    const ymio::SndhSubsong sub[1] = {{frames, c->dma}};
    ymio::SndhWriteInfo info;
    info.frameHz = c->take.frameHz;
    info.clock = {c->take.clockNum, c->take.clockDen};
    info.ste = true;
    info.dma[0] = {x.dmaSamples[0], x.dma.slots[0].rate, x.dma.slots[0].stereo, x.dma.slots[0].loop};
    const ymio::SndhWriteResult w = ymio::sndhFile(sub, info);
    INFO(w.error);
    REQUIRE(w.error.empty());
    CHECK(w.report.droppedEffects == 0);
    CHECK(w.report.dmaDropped == 0);
    CHECK(w.report.dmaCopies == 1);
    const std::vector<Played> p = play(load(w.data), 1, 100);
    REQUIRE(p.size() >= 100);

    int sid = 0, dmaFirst = -1;
    for (size_t t = 0; t < 100; ++t) {
        CAPTURE(t);
        const RegisterFrame& f = frames[t];
        for (int r = 0; r < 13; ++r) {
            if (r == 9 && f.fx[1].on) continue;  // channel B volume driven by the SID timer
            const int want = r == 7 ? (f.reg[7] & 0x3F) : f.reg[size_t(r)];
            CHECK(p[t].reg[size_t(r)] == want);
        }
        if (f.fx[1].on) {
            ++sid;
            CHECK(countWrites(p[t], 9) > 0);
        }
        if (c->dma[t].on && dmaFirst < 0) dmaFirst = int(t);
        const bool sound = std::any_of(p[t].dac.begin(), p[t].dac.end(), [](int16_t d) { return d != 0; });
        if (dmaFirst < 0) CHECK_FALSE(sound);
        else if (int(t) == dmaFirst) CHECK(sound);
    }
    CHECK(sid >= 3);
    CHECK((dmaFirst == 75 || dmaFirst == 76));
}

// The part of a DMA sample from Start to End: the capture starts it at Start, the file stops it at End.
TEST_CASE("SNDH of a project: the DMA sample plays from Start to End", "[sndhwriter][regexport]") {
    using namespace stym::plugin;
    ParamValues v = defaultValues();
    NonParamState x;
    v[size_t(paramIndex(Global::VoiceMode))] = 1;
    v[size_t(paramIndex(Global::FreeTempo))] = 120;
    v[size_t(paramIndex(Global::Computer))] = 1;  // STE
    v[size_t(paramIndex(4, Track::On))] = 1;
    x.seq.cur().steps = 16;
    x.seq.cur().edit(4).pat[0] = 0;  // DMA 1 at step 0
    std::vector<int8_t> pcm(3000);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = int8_t(int(i % 64) * 2 - 63);
    x.dmaSamples[0] = pcm;
    x.dma.slots[0].loop = false;
    x.dma.slots[0].root = 48;  // the note of the step: the bytes at their rate, each one a new DAC level
    x.dma.slots[0].start = 1000;
    x.dma.slots[0].end = 1400;
    const auto c = captureRegisters(OfflineRender::Source::Pattern, v, x);
    REQUIRE(c);
    REQUIRE_FALSE(c->dma.empty());
    REQUIRE(c->dma[0].on);
    CHECK(c->dma[0].start == 1000);
    REQUIRE(c->dma[0].step == 1.0f);

    const auto f = exportSongRegisters(RegisterFormat::Sndh, v, x, RegisterFileOptions{});
    REQUIRE(f);
    INFO(f->error);
    REQUIRE(f->error.empty());
    const std::vector<Played> p = play(load(f->bytes), 2, 10);  // subsong 2: the current pattern
    std::vector<int16_t> dac;
    for (const Played& q : p) dac.insert(dac.end(), q.dac.begin(), q.dac.end());
    REQUIRE(dac.size() >= 400);
    for (size_t i = 0; i < 400; ++i) {
        CAPTURE(i);
        CHECK(dac[i] == pcm[1000 + i] * 64);
    }
    for (size_t i = 400; i < dac.size(); ++i) CHECK(dac[i] == 0);  // stopped at End
}

TEST_CASE("SNDH of a captured pattern: Zxx commands in the sync byte, address in A0",
          "[sndhwriter][regexport]") {
    using namespace stym::plugin;
    using ymengine::SeqCmd;
    ParamValues v = defaultValues();
    NonParamState x;
    v[size_t(paramIndex(Global::VoiceMode))] = 1;
    v[size_t(paramIndex(Global::FreeTempo))] = 120;
    v[size_t(paramIndex(1, Track::On))] = 1;
    x.seq.cur().steps = 16;
    ymseq::Lane& a = x.seq.cur().edit(0);
    ymseq::Lane& b = x.seq.cur().edit(1);
    a.pat[0] = 0;
    a.setCmd(0, ymengine::seqCmd(SeqCmd::Sync, 0x01));
    a.setCmd(4, ymengine::seqCmd(SeqCmd::Sync, 0x42));  // 0.5 s: frame 25
    a.setCmd(8, ymengine::seqCmd(SeqCmd::Sync, 0x07));  // 1 s: frame 50, two tracks, the last one wins
    b.setCmd(8, ymengine::seqCmd(SeqCmd::Sync, 0x09));
    a.setCmd(12, ymengine::seqCmd(SeqCmd::Offset, 0x37));  // another command: no sync
    const auto c = captureRegisters(OfflineRender::Source::Pattern, v, x);
    REQUIRE(c);
    const std::vector<RegisterFrame>& frames = c->take.frames;
    REQUIRE(frames.size() == 100);
    std::vector<std::pair<size_t, int>> syncs;
    for (size_t t = 0; t < frames.size(); ++t)
        if (frames[t].sync >= 0) syncs.emplace_back(t, frames[t].sync);
    REQUIRE(syncs.size() == 3);
    CHECK(syncs[0] == std::pair<size_t, int>{0, 0x01});
    CHECK((syncs[1].first == 25 || syncs[1].first == 26));
    CHECK(syncs[1].second == 0x42);
    CHECK((syncs[2].first == 50 || syncs[2].first == 51));
    CHECK(syncs[2].second == 0x09);

    const ymio::SndhSubsong sub[1] = {{frames, {}}};
    ymio::SndhWriteInfo info;
    info.frameHz = c->take.frameHz;
    info.clock = {c->take.clockNum, c->take.clockDen};
    const ymio::SndhWriteResult w = ymio::sndhFile(sub, info);
    INFO(w.error);
    REQUIRE(w.error.empty());
    REQUIRE(w.syncOffset > 0);
    REQUIRE(w.syncOffset < w.data.size());

    // The init ends with LEA sync(PC),A0 then RTS: A0 returns the address of the byte.
    const std::vector<uint16_t> code = words(w.data);
    REQUIRE(code[0] == 0x6000);
    size_t at = (2 + size_t(int16_t(code[1]))) / 2;
    while (at + 2 < code.size() && !(code[at] == 0x41FA && code[at + 2] == 0x4E75)) ++at;
    REQUIRE(at + 2 < code.size());
    CHECK(int64_t(at) * 2 + 2 + int16_t(code[at + 1]) == int64_t(w.syncOffset));

    // Replayed by AtariAudio: 0 after the init, then the value of the last Zxx played, frame after frame.
    ymio::SndhCapture cap(load(w.data), 1);
    REQUIRE(cap.ok());
    CHECK(cap.peek(ymio::kSndhLoadAddress + w.syncOffset) == 0);
    std::vector<ymio::SndhEvent> ev;
    int want = 0;
    for (uint64_t k = 0; k < 100; ++k) {
        REQUIRE(cap.run(uint64_t(cap.samplesPerTick()) * (k + 1), ev));
        const uint64_t played = cap.ticks();
        REQUIRE(played >= 1);
        CAPTURE(k, played);
        want = 0;
        for (const auto& [t, value] : syncs)
            if (t < played) want = value;
        CHECK(cap.peek(ymio::kSndhLoadAddress + w.syncOffset) == want);
    }
    CHECK(want == 0x09);
}
