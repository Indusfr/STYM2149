// STYM2149 — FM Voice and TTrak's tone FM timers: an FM timer imported as an FM Voice instrument with its four
// steps, played back by the engine; the FM Voice exported as TTrak's FM timer, played back by our model of its routine;
// a wave TTrak cannot play named in the message. Synthetic modules only.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "plugincore/TTrakExport.h"
#include "plugincore/TTrakImport.h"
#include "ymio/TTrak.h"
#include "ymio/TTrakPlayer.h"

using namespace stym;
using plugin::NonParamState;
using ymengine::Generator;
using I = ymio::TtrakInstrument;

namespace {

// Speed 2 (three ticks a row), one pattern: channel A an FM instrument (type 6, factors x1, x1/2, x1/3, x2/3 of the
// period) playing C4 at row 0 and G4 at row 32; channel B a plain square on C3.
ymio::TtrakModule module() {
    ymio::TtrakModule m = ymio::TtrakModule::blank();
    I& fm = m.instruments[0];
    fm.at(I::kAmpEnv) = 0x8F;
    fm.at(I::kTimerType) = uint8_t(ymio::TtrakTimer::Fm);
    for (int k = 0; k < 4; ++k) fm.at(I::kTimerTable + k) = uint8_t(k);
    fm.setName("Fm");
    I& lead = m.instruments[1];
    lead.at(I::kAmpEnv) = 0x0C;
    lead.setName("Lead");
    ymio::TtrakPattern& p = m.patterns[0];
    p.rows[0][0] = {37, 1, 0x1C, 2};
    p.rows[32][0] = {44, 1, 0, 0};
    p.rows[0][1] = {25, 2, 0, 0};
    return m;
}

struct Project {
    plugin::ParamValues values{};
    NonParamState state;
    int fm = -1;  // the FM Voice instrument
};

// The module imported as File > Import sets it up: Authentic 3ch, A4 as TTrak tunes it, tracks 1 to 3 on Chip Voice.
Project project() {
    Project p;
    const plugin::RegisterImportResult r = plugin::importTtrak(module(), p.state);
    INFO(r.error);
    REQUIRE(r.import);
    p.values = plugin::defaultValues();
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    p.values[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(r.import->vblRate);
    p.values[size_t(plugin::paramIndex(plugin::Global::A4Hz))] = r.import->a4Hz;
    for (int k = 0; k < plugin::kTracks; ++k) {
        const bool on = k < 3;
        p.values[size_t(plugin::paramIndex(k, plugin::Track::On))] = on ? 1.0f : 0.0f;
        if (!on) continue;
        p.values[size_t(plugin::paramIndex(k, plugin::Track::Generator))] = float(int(Generator::Chip));
        p.values[size_t(plugin::paramIndex(k, plugin::Track::Level))] = 100;
    }
    for (int k = 0; k < r.import->instruments; ++k)
        if (p.state.instruments[size_t(k)].patch.generator == Generator::Fm) p.fm = k;
    return p;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

bool has(const std::vector<std::string>& lines, const std::string& start) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.rfind(start, 0) == 0; });
}

}  // namespace

TEST_CASE("TTrak FM: the four factors of the period are FM Voice's multiples x1, x2, x3 and x1.5",
          "[ttrak-import][fm]") {
    CHECK(ymengine::kFmMultiples[plugin::ttrakFmMultiple(0)] == 1.0);
    CHECK(ymengine::kFmMultiples[plugin::ttrakFmMultiple(1)] == 2.0);
    CHECK(ymengine::kFmMultiples[plugin::ttrakFmMultiple(2)] == 3.0);
    CHECK(ymengine::kFmMultiples[plugin::ttrakFmMultiple(3)] == 1.5);
    for (int f = 0; f < 4; ++f) CHECK(plugin::ttrakFmFactor(plugin::ttrakFmMultiple(uint8_t(f))) == f);
    for (const int m : {0, 5, 6, 7}) CHECK(plugin::ttrakFmFactor(uint8_t(m)) == -1);
}

TEST_CASE("TTrak FM: an FM timer becomes an FM Voice instrument with its four steps", "[ttrak-import][fm]") {
    const Project p = project();
    REQUIRE(p.fm >= 0);
    const ymengine::TrackPatch& patch = p.state.instruments[size_t(p.fm)].patch;
    REQUIRE(patch.fmWaveLen == 4);
    CHECK(patch.fmWave[0] == plugin::ttrakFmMultiple(0));
    CHECK(patch.fmWave[1] == plugin::ttrakFmMultiple(1));
    CHECK(patch.fmWave[2] == plugin::ttrakFmMultiple(2));
    CHECK(patch.fmWave[3] == plugin::ttrakFmMultiple(3));
    // Played back on channel A: the four steps at the rate of TTrak's timer for the note.
    ymio::TtrakPlayer ttrak(module());
    const ymio::TtrakTick k = ttrak.tick();
    REQUIRE(k.ch[0].timerRuns);
    const double want = ymengine::MfpSetting{k.ch[0].timerPrescaler, uint16_t(k.ch[0].timerData)}.hz();
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, p.values, p.state);
    REQUIRE(cap);
    for (const int f : {2, 32 * 3 + 2}) {
        CAPTURE(f);
        const ymengine::RegisterFrame::Effect& e = cap->take.frames[size_t(f)].fx[0];
        CHECK(e.on);
        CHECK(e.gen == Generator::Fm);
        CHECK(e.value == 4);
        CHECK(e.wave == std::array<uint8_t, 16>{1, 3, 4, 2});
        if (f == 2) CHECK(std::abs(cents(e.timer.hz(), want)) < 20);
    }
}

TEST_CASE("TTrak FM: an FM Voice exported as TTrak's FM timer, played back with its steps", "[ttrak-export][fm]") {
    const Project p = project();
    REQUIRE(p.fm >= 0);
    const std::optional<plugin::TtrakExportResult> r = plugin::exportTtrak(p.values, p.state, "Fm");
    REQUIRE(r);
    INFO(r->error);
    REQUIRE(r->value);
    const plugin::TtrakExport& x = *r->value;
    CHECK_FALSE(has(x.remarks, "FM Voice frames played as the note alone"));
    CHECK_FALSE(has(x.remarks, "Frames that TTrak plays otherwise"));
    int fm = 0;
    for (const I& ins : x.module.instruments) {
        if (ins.at(I::kTimerType) != uint8_t(ymio::TtrakTimer::Fm)) continue;
        ++fm;
        for (int k = 0; k < 4; ++k) CHECK(ins.at(I::kTimerTable + k) == k);
    }
    CHECK(fm == 1);
    // TTrak's routine plays the four steps on channel A, the note's period divided.
    const std::vector<uint8_t> file = ymio::writeTtrakModule(x.module);
    const auto back = ymio::parseTtrakModule(std::span<const uint8_t>(file.data(), file.size()));
    REQUIRE(back.value);
    ymio::TtrakPlayer player(*back.value);
    const ymio::TtrakTick t = player.tick();
    CHECK(t.ch[0].timer == uint8_t(ymio::TtrakTimer::Fm));
    CHECK(t.ch[0].timerRuns);
    for (int k = 0; k < 4; ++k) CHECK(t.ch[0].timerWave[size_t(k)] == k);
}

TEST_CASE("TTrak FM: a wave TTrak cannot play goes as the note alone, named in the message", "[ttrak-export][fm]") {
    Project p = project();
    REQUIRE(p.fm >= 0);
    const bool steps = GENERATE(false, true);
    CAPTURE(steps);
    for (plugin::Instrument& ins : p.state.instruments) {
        if (ins.patch.generator != Generator::Fm) continue;
        if (steps) ins.patch.fmWaveLen = 3;
        else ins.patch.fmWave[1] = 5;  // x4
    }
    const std::optional<plugin::TtrakExportResult> r = plugin::exportTtrak(p.values, p.state, "Fm");
    REQUIRE(r);
    REQUIRE(r->value);
    CHECK(has(r->value->remarks, "FM Voice frames played as the note alone"));
    for (const I& ins : r->value->module.instruments) CHECK(ins.at(I::kTimerType) != uint8_t(ymio::TtrakTimer::Fm));
}
