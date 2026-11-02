// STYM2149 — plugincore: import of TTrak modules as editable patterns: each row a step at the module's speed,
// the notes where the rows start them, a SID instrument as a SID Voice, digidrums on the Digidrum track with their
// 4-bit samples, all played back by the engine. Synthetic modules only.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "plugincore/TTrakImport.h"
#include "ymio/TTrak.h"

using namespace stym;
using plugin::NonParamState;
using ymengine::Generator;
using I = ymio::TtrakInstrument;

namespace {

// Speed 2 (three ticks a row), one pattern: channel A a square playing C4, E4, G4 every 16 rows, channel B a square
// SID on C3 at rows 0 and 32, channel C a digidrum (slot 1, 120 volumes) on C8 every 8 rows.
ymio::TtrakModule module() {
    ymio::TtrakModule m = ymio::TtrakModule::blank();
    m.setTitle("Import");
    auto ins = [&](int k, uint8_t step, int timer) -> I& {
        I& r = m.instruments[size_t(k - 1)];
        r.at(I::kAmpEnv) = step;
        r.at(I::kTimerType) = uint8_t(timer);
        r.at(I::kSampleSlot) = 0;
        return r;
    };
    ins(1, 0x0F, 0).setName("Lead");
    ins(2, 0x8F, 1).setName("Sid");
    ins(3, 0x8F, 7).setName("Drum");
    std::vector<uint8_t> levels;
    for (int i = 0; i < 120; ++i) levels.push_back(uint8_t(8 + std::lround(7 * std::sin(i * 0.4) * (1 - i / 120.0))));
    m.samples[0] = ymio::ttrakSampleBytes(levels);
    ymio::TtrakPattern& p = m.patterns[0];
    p.rows[0][0] = {37, 1, 0x1C, 2};
    p.rows[16][0] = {41, 1, 0, 0};
    p.rows[32][0] = {44, 1, 0, 0};
    p.rows[0][1] = {25, 2, 0, 0};
    p.rows[32][1] = {25, 2, 0, 0};
    for (int r = 0; r < 64; r += 8) p.rows[size_t(r)][2] = {85, 3, 0, 0};
    return m;
}

struct Played {
    plugin::RegisterImport import;
    NonParamState state;
    std::vector<ymengine::RegisterFrame> frames;
};

// The import played back as File > Import sets it up: Authentic 3ch, A4 as TTrak tunes it, tracks 1 to 3 on Chip Voice,
// track 4 on Digidrum.
Played play() {
    Played p;
    const plugin::RegisterImportResult r = plugin::importTtrak(module(), p.state);
    INFO(r.error);
    REQUIRE(r.import);
    p.import = *r.import;
    plugin::ParamValues v = plugin::defaultValues();
    v[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    v[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(p.import.vblRate);
    v[size_t(plugin::paramIndex(plugin::Global::A4Hz))] = p.import.a4Hz;
    for (int k = 0; k < plugin::kTracks; ++k) {
        const bool on = k < 3 || (k == 3 && p.import.digidrums);
        v[size_t(plugin::paramIndex(k, plugin::Track::On))] = on ? 1.0f : 0.0f;
        if (!on) continue;
        v[size_t(plugin::paramIndex(k, plugin::Track::Generator))] =
            float(int(k == 3 ? Generator::Digidrum : Generator::Chip));
        v[size_t(plugin::paramIndex(k, plugin::Track::Level))] = 100;
    }
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, v, p.state);
    REQUIRE(cap);
    p.frames = cap->take.frames;
    return p;
}

double cents(double a, double b) { return 1200 * std::log2(a / b); }

}  // namespace

TEST_CASE("TTrak import: each row a step at the module's speed, the notes where the rows start them",
          "[ttrak-import]") {
    const plugin::TtrakTune t = plugin::ttrakTune(module());
    CHECK(t.tune.title == "Import");
    CHECK(t.tune.frames.size() == 64 * 3);
    // The $1C 2 of row 0 is read at its first tick: every row lasts three ticks.
    CHECK(t.layout.stepFrames == std::vector<int>(64, 3));
    CHECK(t.layout.partSteps == std::vector<int>{64});
    CHECK(t.layout.loopStep == 0);
    const Played p = play();
    CHECK(p.import.speed == 3);
    CHECK(p.import.patterns == 1);
    const ymseq::Lane& a = p.state.seq.patterns[0].lane(0);
    for (int s = 0; s < 64; ++s) {
        CAPTURE(s);
        CHECK((a.pat[size_t(s)] >= 0) == (s == 0 || s == 16 || s == 32));
    }
    CHECK(ymseq::absoluteNote(a, 1, 0, p.state.seq.tracks[0].octave) == 60);
    CHECK(ymseq::absoluteNote(a, 1, 16, p.state.seq.tracks[0].octave) == 64);
    CHECK(ymseq::absoluteNote(a, 1, 32, p.state.seq.tracks[0].octave) == 67);
    // Played back: C4, E4 and G4 on channel A, within a few cents of TTrak's periods (A4 at 438.8 Hz).
    const double hz[] = {261.63, 329.63, 392.0};
    for (int k = 0; k < 3; ++k) {
        CAPTURE(k);
        const auto& r = p.frames[size_t(48 * k + 1)].reg;
        const int tp = r[0] | (r[1] & 15) << 8;
        CHECK(std::abs(cents(2'000'000.0 / 16 / tp, hz[k] * 438.84 / 440)) < 10);
        CHECK((r[8] & 15) == 15);
    }
}

TEST_CASE("TTrak import: a square SID instrument becomes a SID Voice at the rate of its note", "[ttrak-import]") {
    const Played p = play();
    int sid = -1;
    for (int k = 0; k < p.import.instruments; ++k)
        if (p.state.instruments[size_t(k)].patch.generator == Generator::Sid) sid = k;
    REQUIRE(sid >= 0);
    const ymengine::TrackPatch& patch = p.state.instruments[size_t(sid)].patch;
    CHECK(patch.sidForm == ymengine::SidForm::Custom);
    REQUIRE(patch.sidWaveLen == 2);
    CHECK(patch.sidWave[0] == 15);
    CHECK(patch.sidWave[1] == 0);
    // Played back on channel B: a SID at C3, two interrupts per period.
    for (const int f : {2, 32 * 3 + 2}) {
        CAPTURE(f);
        const ymengine::RegisterFrame::Effect& e = p.frames[size_t(f)].fx[1];
        CHECK(e.on);
        CHECK(e.gen == Generator::Sid);
        CHECK(std::abs(cents(e.timer.hz() / 2, 130.81 * 438.84 / 440)) < 20);
    }
}

TEST_CASE("TTrak import: digidrums play on the Digidrum track with their 4-bit samples", "[ttrak-import]") {
    const Played p = play();
    REQUIRE(p.import.digidrums);
    CHECK(p.import.samples == 1);
    const plugin::DigidrumSlotState& slot = p.state.digiSlots[0];
    CHECK(slot.source.size() == 120);
    const ymseq::Lane& d = p.state.seq.patterns[0].lane(3);
    for (int s = 0; s < 64; ++s) {
        CAPTURE(s);
        CHECK((d.pat[size_t(s)] >= 0) == (s % 8 == 0));
    }
    // Played back on channel C from each hit.
    for (int s = 0; s < 64; s += 8) {
        CAPTURE(s);
        const ymengine::RegisterFrame::Effect& e = p.frames[size_t(3 * s)].fx[2];
        CHECK(e.on);
        CHECK(e.gen == Generator::Digidrum);
    }
}
