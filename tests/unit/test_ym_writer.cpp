// STYM2149 — ymio: writing register dumps: VBL, YM3 and YM6 with timer effects.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "ymcore/ChipRenderer.h"
#include "ymio/Lzh.h"
#include "ymio/YmFile.h"
#include "ymio/YmPlayer.h"
#include "ymio/YmWriter.h"

using namespace stym;
using ymengine::Generator;
using ymengine::MfpSetting;
using ymengine::RegisterFrame;

namespace {

RegisterFrame frame(uint8_t base) {
    RegisterFrame f;
    for (size_t k = 0; k < 14; ++k) f.reg[k] = uint8_t(base + k);
    f.reg[1] &= 0x0f;
    f.reg[3] &= 0x0f;
    f.reg[5] &= 0x0f;
    f.reg[6] &= 0x1f;
    f.reg[7] = 0x38;
    f.reg[8] = 12;
    f.reg[9] = 0x10;
    f.reg[10] = 7;
    f.reg[13] = 0x0a;
    return f;
}

RegisterFrame::Effect fx(Generator g, uint8_t value, MfpSetting t, bool restart) {
    RegisterFrame::Effect e;
    e.on = true;
    e.gen = g;
    e.value = value;
    e.timer = t;
    e.restart = restart;
    return e;
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < s.size()) {
        const size_t nl = s.find('\n', at);
        out.push_back(s.substr(at, nl - at));
        at = nl + 1;
    }
    return out;
}

}  // namespace

TEST_CASE("VBL and YM3: timer effects frozen, R13 restarted only when needed", "[ymwriter]") {
    std::vector<RegisterFrame> f = {frame(0x10), frame(0x20), frame(0x30), frame(0x40)};
    f[0].r13 = true;
    f[0].fx[0] = fx(Generator::Sid, 13, {1, 37}, true);       // SID: high volume
    f[1].fx[2] = fx(Generator::Digidrum, 2, {1, 79}, true);   // Digidrum: zero volume
    f[2].r13 = true;                                          // restart by the Sync Buzzer timer: frozen
    f[2].fx[1] = fx(Generator::SyncBuzzer, 8, {3, 100}, false);
    f[3].r13 = true;
    f[3].fx[1] = fx(Generator::SyncBuzzer, 8, {3, 100}, true);  // attack: R13 written
    const auto z0 = ymio::frozenFrame(f[0]), z1 = ymio::frozenFrame(f[1]), z2 = ymio::frozenFrame(f[2]),
               z3 = ymio::frozenFrame(f[3]);
    CHECK(z0[8] == 13);
    CHECK(z0[13] == 0x0a);
    CHECK(z1[10] == 0);
    CHECK(z1[13] == 0xff);
    CHECK(z2[13] == 0xff);
    CHECK(z2[9] == 0x10);
    CHECK(z3[13] == 0x0a);

    const std::vector<uint8_t> ym3 = ymio::ym3File(f);
    REQUIRE(ym3.size() == 4 + 14 * 4);
    CHECK(std::string(ym3.begin(), ym3.begin() + 4) == "YM3!");
    CHECK(ym3[4 + 8 * 4 + 0] == 13);  // R8 of frame 0, register by register
    const ymio::YmParseResult r = ymio::parseYm(ym3);
    REQUIRE(r.file);
    CHECK(r.file->format == ymio::YmFormat::YM3);
    REQUIRE(r.file->frameCount == 4);
    for (uint32_t t = 0; t < 4; ++t) {
        const auto z = ymio::frozenFrame(f[t]);
        for (unsigned k = 0; k < 14; ++k) CHECK(r.file->reg(t, k) == z[k]);
    }

    const auto l = lines(ymio::vblSource(f, {200, "pattern: 16 steps", 125}));
    REQUIRE(l.size() == 8 + 4);
    CHECK(l[0] == "; STYM2149F - VBL register dump (200 Hz)");
    CHECK(l[1] == "; pattern: 16 steps @ 125.00 BPM -> 4 frames");
    CHECK(l[2] == "; 14 bytes/frame: R0-R13. R13 = $FF -> do not write (envelope not retriggered).");
    CHECK(l[5].empty());
    CHECK(l[6] == "ym_frames:\tdc.w\t4");
    CHECK(l[7] == "ym_data:");
    CHECK(l[8] == "\tdc.b\t$10,$01,$12,$03,$14,$05,$16,$38,$0D,$10,$07,$1B,$1C,$0A");
    CHECK(l[9] == "\tdc.b\t$20,$01,$22,$03,$24,$05,$06,$38,$0C,$10,$00,$2B,$2C,$FF");
    // More than 65 535 frames: the counter switches to dc.l.
    const std::vector<RegisterFrame> many(70000);
    CHECK(lines(ymio::vblSource(many, {50, "song: 1 entries", 120}))[6] == "ym_frames:\tdc.l\t70000");
    // Recorder take: no tempo.
    CHECK(lines(ymio::vblSource(f, {50, "recorder take", 0}))[1] == "; recorder take -> 4 frames");
}

TEST_CASE("YM6: two effects per frame, digidrums, cut, read back by parseYm and played by YmPlayer", "[ymwriter]") {
    std::vector<uint8_t> drum(1000);
    for (size_t i = 0; i < drum.size(); ++i) drum[i] = uint8_t(i % 16);
    std::vector<RegisterFrame> f(6, frame(0x10));
    const MfpSetting sid{1, 37}, dd{1, 79}, buzz{3, 100};
    f[0].fx[0] = fx(Generator::Sid, 13, sid, true);
    f[0].fx[2] = fx(Generator::Digidrum, 2, dd, true);
    f[1].fx[0] = fx(Generator::Sid, 13, sid, false);
    f[1].fx[2] = fx(Generator::Digidrum, 2, dd, false);
    f[2].fx[0] = fx(Generator::Sid, 13, sid, false);
    f[2].fx[2] = fx(Generator::Digidrum, 2, dd, false);
    f[2].fx[1] = fx(Generator::SyncBuzzer, 8, buzz, true);  // no free place: frozen
    f[2].r13 = true;
    f[3].fx[0] = fx(Generator::Sid, 13, sid, false);
    f[3].fx[1] = fx(Generator::SyncBuzzer, 8, buzz, false);  // the freed slot is used for the cut
    f[3].reg[10] = 9;                                         // digidrum stopped by the engine: cut at level 9
    f[4].fx[1] = fx(Generator::SyncBuzzer, 8, buzz, false);  // slot 1 of the finished SID is free
    f[5].fx[1] = fx(Generator::SyncBuzzer, 8, buzz, false);
    f[5].fx[2] = fx(Generator::Digidrum, 2, dd, true);       // slot 2
    ymio::Ym6Info info;
    info.frameHz = 50;
    info.clock = {8'021'247, 4};
    info.title = "STYM2149 song";
    info.author = "Indus";
    info.comment = "Made with STYM2149";
    info.digidrums[2] = drum;
    ymio::Ym6Report rep;
    const std::vector<uint8_t> b = ymio::ym6File(f, info, &rep);
    CHECK(rep.droppedEffects == 2);
    CHECK(rep.cutDrums == 1);
    CHECK(rep.frozenDrums == 0);

    const ymio::YmParseResult r = ymio::parseYm(b);
    REQUIRE(r.file);
    const ymio::YmFile& y = *r.file;
    CHECK(y.format == ymio::YmFormat::YM6);
    CHECK(y.frameCount == 6);
    CHECK(y.attributes == 5);
    CHECK(y.playerRate == 50);
    CHECK(y.clock.num == 2'005'312);
    CHECK(y.title == "STYM2149 song");
    CHECK(y.author == "Indus");
    CHECK(y.comment == "Made with STYM2149");
    REQUIRE(y.digidrums.size() == 2);
    CHECK(y.digidrums[0] == drum);
    CHECK(y.digidrums[1] == std::vector<uint8_t>{9});
    CHECK(std::string(b.end() - 4, b.end()) == "End!");
    // Frame 0: SID on channel A (slot 1), digidrum 0 on channel C (slot 2).
    CHECK((y.reg(0, 1) & 0xf0) == 0x10);
    CHECK((y.reg(0, 1) & 0x0f) == 0x01);
    CHECK(y.reg(0, 6) >> 5 == 1);
    CHECK(y.reg(0, 14) == 37);
    CHECK((y.reg(0, 3) & 0xf0) == 0x70);
    CHECK(y.reg(0, 8) == (0x20 | 13));
    CHECK(y.reg(0, 15) == 79);
    CHECK(y.reg(0, 10) == 0);
    // Frame 1: the digidrum continues, without a code.
    CHECK((y.reg(1, 1) & 0xf0) == 0x10);
    CHECK((y.reg(1, 3) & 0xf0) == 0);
    // Frame 2: Sync Buzzer frozen (envelope volume, R13 of its attack).
    CHECK(y.reg(2, 9) == 0x10);
    CHECK(y.reg(2, 13) == 0x0a);
    // Frame 3: cut of the digidrum (sample 1, as fast as possible).
    CHECK((y.reg(3, 3) & 0xf0) == 0x70);
    CHECK(y.reg(3, 10) == 1);
    CHECK(y.reg(3, 15) == 1);
    CHECK(y.reg(3, 13) == 0xff);
    // Frame 4: no more SID; Sync Buzzer of channel B on slot 1.
    CHECK((y.reg(4, 1) & 0xf0) == 0xe0);
    CHECK((y.reg(4, 3) & 0xf0) == 0);
    CHECK((y.reg(4, 6) & 0xe0) == 3 << 5);
    CHECK(y.reg(4, 9) == 8);
    CHECK(y.reg(4, 14) == 100);
    // Frame 5: the Sync Buzzer keeps its slot, new digidrum on slot 2.
    CHECK((y.reg(5, 1) & 0xf0) == 0xe0);
    CHECK((y.reg(5, 3) & 0xf0) == 0x70);
    CHECK(y.reg(5, 15) == 79);
    CHECK(y.reg(5, 10) == 0);

    ymcore::RenderConfig rc;
    rc.hostRate = 48000;
    ymcore::ChipRenderer chip;
    chip.prepare(rc);
    ymio::YmPlayer p(y, ymio::YmTiming::Exact, 48000);
    std::vector<float> out(48000 * 6 / 50);
    p.render(chip, out.data(), uint32_t(out.size()));
    CHECK(p.ignoredEffectFrames() == 0);
    CHECK(p.effectFrames() == 9);  // 2 + 1 + 1 + 2 + 1 + 2
    CHECK(p.timerInterrupts() > 0);
}

TEST_CASE("YM6 of a captured pattern: a SID code at each frame where the channel plays it", "[ymwriter]") {
    using namespace stym::plugin;
    ParamValues v = defaultValues();
    NonParamState x;
    v[size_t(paramIndex(Global::VoiceMode))] = 1;
    v[size_t(paramIndex(Global::FreeTempo))] = 120;
    v[size_t(paramIndex(0, Track::Generator))] = float(int(Generator::Sid));
    x.seq.cur().steps = 16;
    x.seq.cur().edit(0).pat[0] = 0;
    x.seq.cur().edit(0).pat[8] = 7;
    const auto c = captureRegisters(OfflineRender::Source::Pattern, v, x);
    REQUIRE(c);
    const auto& frames = c->take.frames;
    REQUIRE(frames.size() == 100);
    ymio::Ym6Info info;
    info.frameHz = c->take.frameHz;
    info.clock = {c->take.clockNum, c->take.clockDen};
    ymio::Ym6Report rep;
    const std::vector<uint8_t> ym6 = ymio::ym6File(frames, info, &rep);
    const ymio::YmParseResult r = ymio::parseYm(ym6);
    REQUIRE(r.file);
    CHECK(rep.droppedEffects == 0);
    // Compressed with -lh5-: read back identically, much smaller.
    const std::vector<uint8_t> lzh = ymio::packLzh("stym2149-song.ym", ym6);
    CHECK(lzh.size() * 3 < ym6.size());
    const ymio::YmParseResult packed = ymio::parseYm(lzh);
    REQUIRE(packed.file);
    CHECK(packed.file->frames == r.file->frames);
    CHECK(packed.file->frameCount == 100);
    int sidFrames = 0;
    for (uint32_t t = 0; t < 100; ++t) {
        CAPTURE(t);
        const bool on = frames[t].fx[0].on;
        sidFrames += on;
        CHECK(((r.file->reg(t, 1) & 0xf0) == 0x10) == on);
        if (on) CHECK(r.file->reg(t, 14) == (frames[t].fx[0].timer.data & 0xff));
    }
    CHECK(sidFrames >= 4);  // two notes, each for the duration of its gate
}
