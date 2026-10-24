// STYM2149 — plugincore: register exports (M7, A5, D-160, D-163): capture by a second instance of the engine.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "plugincore/RegisterFiles.h"
#include "ymio/Sndh.h"
#include "ymio/YmFile.h"

using namespace stym;
using namespace stym::plugin;
using ymengine::Generator;

namespace {

float& param(ParamValues& v, int track, Track f) { return v[size_t(paramIndex(track, f))]; }
float& param(ParamValues& v, Global g) { return v[size_t(paramIndex(g))]; }

void note(ymseq::Lane& l, int step, int midi) {
    const int oc = midi / 12 - 1;
    l.pat[size_t(step)] = int16_t(midi - (oc + 1) * 12);
    l.oct[size_t(step)] = int8_t(ymseq::octaveCode(oc, 3));
}

// Authentic 3ch, 16 steps at 120 BPM (2 s): C4 at step 0 and G4 at step 8 on channel A.
struct Project {
    ParamValues v = defaultValues();
    NonParamState x;
    Project() {
        param(v, Global::VoiceMode) = 1;
        param(v, Global::FreeTempo) = 120;
        x.seq.cur().steps = 16;
        note(x.seq.cur().edit(0), 0, 60);
        note(x.seq.cur().edit(0), 8, 67);
    }
};

int period(const ymengine::RegisterFrame& f, int voice) {
    return f.reg[size_t(2 * voice)] | (f.reg[size_t(2 * voice + 1)] & 15) << 8;
}

}  // namespace

TEST_CASE("Register capture: one frame per replay tick, from the start to the sequencer stop", "[regexport]") {
    Project pr;
    const auto c = captureRegisters(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(c);
    CHECK_FALSE(c->truncated);
    const RegisterTake& t = c->take;
    CHECK(t.chip == 0);
    CHECK(t.frameHz == 50);
    CHECK(t.clockNum == 2'000'000);
    CHECK(t.clockDen == 1);
    REQUIRE(t.frames.size() == 100);  // 2 s at 50 Hz
    // C4 from the first frame, G4 from frame 50 (1 s), channel A audible.
    const int c4 = period(t.frames[0], 0), g4 = period(t.frames[50], 0);
    CHECK(c4 == int(std::lround(2'000'000.0 / (16 * 261.6255653))));
    CHECK(double(c4) / g4 == Catch::Approx(std::pow(2.0, 7.0 / 12)).epsilon(0.01));
    CHECK(period(t.frames[49], 0) == c4);
    CHECK((t.frames[0].reg[8] & 15) != 0);
    CHECK((t.frames[0].reg[7] & 1) == 0);  // tone of channel A open

    // Replay tick of 200 Hz (D-149): four times as many frames.
    param(pr.v, Global::VblRate) = 4;
    const auto fast = captureRegisters(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(fast);
    CHECK(fast->take.frameHz == 200);
    CHECK(fast->take.frames.size() == 400);
    param(pr.v, Global::VblRate) = 0;

    // Song of two passes: 4 s.
    pr.x.seq.song.entries = {{0, 2, 0}};
    const auto song = captureRegisters(OfflineRender::Source::Song, pr.v, pr.x);
    REQUIRE(song);
    CHECK(song->take.frames.size() == 200);
    CHECK(period(song->take.frames[100], 0) == c4);

    // Cancel requested; nothing outside Authentic 3ch mode (D-160).
    std::atomic<bool> cancel{true};
    CHECK_FALSE(captureRegisters(OfflineRender::Source::Song, pr.v, pr.x, &cancel));
    for (const float mode : {0.0f, 2.0f}) {
        param(pr.v, Global::VoiceMode) = mode;
        const auto none = captureRegisters(OfflineRender::Source::Pattern, pr.v, pr.x);
        REQUIRE(none);
        CHECK(none->take.empty());
    }
}

TEST_CASE("Register capture: timer effects and their restart at each attack", "[regexport]") {
    Project pr;
    param(pr.v, 0, Track::Generator) = float(int(Generator::Sid));
    const auto c = captureRegisters(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(c);
    const auto& f = c->take.frames;
    REQUIRE(f.size() == 100);
    CHECK(f[0].fx[0].on);
    CHECK(f[0].fx[0].gen == Generator::Sid);
    CHECK(f[0].fx[0].restart);
    CHECK(f[0].fx[0].timer.running());
    CHECK_FALSE(f[1].fx[0].restart);
    // Second note at step 8: the timer restarts at the frame that follows its attack.
    int restarts = 0, at = -1;
    for (size_t i = 1; i < f.size(); ++i)
        if (f[i].fx[0].restart) {
            ++restarts;
            at = int(i);
        }
    CHECK(restarts == 1);
    CHECK((at == 50 || at == 51));
}

// STE machine (D-153, D-162): the DMA sound of each frame is taken with the registers, for the SNDH.
TEST_CASE("Register capture: DMA sound of the STE machine, frame by frame", "[regexport][dma]") {
    Project pr;
    param(pr.v, Global::Computer) = 1;  // STE: track 5 = DMA 1, slot 0
    pr.x.dmaSamples[0] = std::vector<int8_t>(20000, 50);
    pr.x.dma.slots[0].loop = false;
    param(pr.v, 4, Track::On) = 1;
    note(pr.x.seq.cur().edit(4), 4, 72);  // step 4 (0.5 s), one octave above the root note
    const auto c = captureRegisters(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(c);
    REQUIRE(c->take.frames.size() == 100);
    REQUIRE(c->dma.size() == 100);
    int first = -1, on = 0;
    for (size_t i = 0; i < c->dma.size(); ++i) {
        const ymengine::DmaFrame& d = c->dma[i];
        if (!d.on) continue;
        ++on;
        if (first < 0) first = int(i);
        CHECK(d.slot == 0);
        CHECK(d.voices == 1);
        CHECK(d.step == Catch::Approx(2.0).epsilon(1e-3));
        CHECK(d.restart == (int(i) == first));
    }
    CHECK((first == 25 || first == 26));
    CHECK(on >= 3);  // gate of one step (0.125 s)
    CHECK_FALSE(c->dma.back().on);

    // ST machine: no DMA sound.
    param(pr.v, Global::Computer) = 0;
    const auto st = captureRegisters(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(st);
    CHECK(st->dma.empty());
    CHECK(st->take.frames.size() == 100);
}

// Files of the exports (D-172): formats, subsongs of the SNDH, what the alert reports.
TEST_CASE("Register exports: files and alert notes", "[regexport]") {
    Project pr;
    param(pr.v, 1, Track::On) = 1;
    param(pr.v, 1, Track::Generator) = float(int(Generator::Sid));
    note(pr.x.seq.cur().edit(1), 4, 64);
    pr.x.seq.song.entries = {{0, 2, 0}};  // two passes: 4 s
    RegisterFileOptions o;
    o.name = "tune.ym";

    // VBL and YM3: SID frozen; YM3 at 2 MHz and 50 Hz, nothing else.
    for (const RegisterFormat f : {RegisterFormat::Vbl, RegisterFormat::Ym3}) {
        const auto r = exportSongRegisters(f, pr.v, pr.x, o);
        REQUIRE(r);
        CHECK(r->error.empty());
        CHECK(r->frames == 200);
        CHECK(r->frameHz == 50);
        CHECK(r->seconds() == Catch::Approx(4.0));
        REQUIRE(r->notes.size() == 1);
        CHECK(r->notes[0].rfind("Timer effects frozen: ", 0) == 0);
    }
    const auto vbl = exportSongRegisters(RegisterFormat::Vbl, pr.v, pr.x, o);
    const std::string text(vbl->bytes.begin(), vbl->bytes.end());
    CHECK(text.find("; song: 1 entry @ 120.00 BPM -> 200 frames\n") != std::string::npos);

    // Compressed YM6, read back: the SID is carried.
    const auto ym6 = exportSongRegisters(RegisterFormat::Ym6, pr.v, pr.x, o);
    REQUIRE(ym6);
    CHECK(ym6->notes.empty());
    CHECK(std::string(ym6->bytes.begin() + 2, ym6->bytes.begin() + 7) == "-lh5-");
    const ymio::YmParseResult y = ymio::parseYm(ym6->bytes);
    REQUIRE(y.file);
    CHECK(y.file->frameCount == 200);
    CHECK(y.file->title == "STYM2149 song");

    // SNDH: Song, then current pattern.
    const auto sndh = exportSongRegisters(RegisterFormat::Sndh, pr.v, pr.x, o);
    REQUIRE(sndh);
    CHECK(sndh->notes.empty());
    const ymio::SndhParseResult s = ymio::parseSndh(sndh->bytes);
    REQUIRE(s.file);
    CHECK(s.file->info.subsongs == 2);
    CHECK(s.file->info.frames == std::vector<uint32_t>{200, 100});
    CHECK(s.file->info.title == "STYM2149 song");

    // CPC clock and replay at 100 Hz: the YM3 does not carry them, the alert says so.
    param(pr.v, Global::VblRate) = 3;    // 100 Hz
    param(pr.v, Global::ChipClock) = 3;  // Amstrad CPC
    const auto cpc = exportSongRegisters(RegisterFormat::Ym3, pr.v, pr.x, o);
    REQUIRE(cpc);
    CHECK(cpc->frameHz == 100);
    REQUIRE(cpc->notes.size() == 3);
    CHECK(cpc->notes[1] == "YM3 has no clock: players assume 2 MHz, not 1 MHz.");
    CHECK(cpc->notes[2] == "YM3 has no replay rate: players assume 50 Hz, not 100 Hz.");

    // Outside Authentic 3ch: refused.
    param(pr.v, Global::VoiceMode) = 0;
    const auto none = exportSongRegisters(RegisterFormat::Sndh, pr.v, pr.x, o);
    REQUIRE(none);
    CHECK(none->bytes.empty());
    CHECK_FALSE(none->error.empty());
}

TEST_CASE("Register exports: recorder take", "[regexport]") {
    Project pr;
    RegisterTake t;
    t.frameHz = 50;
    t.frames.resize(10);
    for (auto& f : t.frames) f.reg[7] = 0x3E;
    RegisterFileOptions o;
    const RegisterFile vbl = exportTake(RegisterFormat::Vbl, t, pr.v, pr.x, o);
    const std::string text(vbl.bytes.begin(), vbl.bytes.end());
    CHECK(text.find("; recorder take -> 10 frames\n") != std::string::npos);
    CHECK(registerFileName(RegisterFormat::Sndh, true) == "stym2149-take.sndh");
    CHECK(registerFileName(RegisterFormat::Vbl, false) == "stym2149-song.s");
    // STE machine: the SNDH of the take has no DMA sound, and the alert says so.
    param(pr.v, Global::Computer) = 1;
    const RegisterFile sndh = exportTake(RegisterFormat::Sndh, t, pr.v, pr.x, o);
    REQUIRE(sndh.error.empty());
    REQUIRE(sndh.notes.size() == 1);
    CHECK(sndh.notes[0] == "The Recorder keeps no DMA sound: the SNDH has none.");
    const ymio::SndhParseResult s = ymio::parseSndh(sndh.bytes);
    REQUIRE(s.file);
    CHECK(s.file->info.title == "STYM2149 take");
    // Empty take: nothing written.
    CHECK_FALSE(exportTake(RegisterFormat::Ym3, RegisterTake{}, pr.v, pr.x, o).error.empty());
}
