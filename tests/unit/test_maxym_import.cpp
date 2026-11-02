// STYM2149 — plugincore: import of maxYMiser songs as editable patterns: each row a step, the notes where the
// rows start them, a SID instrument as a SID Voice with its waveform, digidrums on the Digidrum track with their
// samples, all played back by the engine.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/MaxYmImport.h"
#include "plugincore/RegisterExport.h"
#include "ymio/MaxYm.h"

using namespace stym;
using plugin::NonParamState;
using ymengine::Generator;
using ymio::MyvInstrument;

namespace {

constexpr int kDrumRate = 80;  // MFP data of the digidrum: 614400 / 80 = 7680 Hz
constexpr int kDrumLength = 1536;

// Instrument 1: square and SID (4 steps, the volume taken off every other one); instrument 2: digidrum 1.
ymio::MyvVoiceset voices() {
    ymio::MyvVoiceset v;
    v.sequences.resize(5);
    auto seq = [&](int n, const std::vector<uint16_t>& words) {
        ymio::MyvSequence& s = v.sequences[size_t(n)];
        s.length = uint8_t(words.size());
        s.repeat = uint8_t(words.size() - 1);  // stops on its last word
        for (size_t i = 0; i < words.size(); ++i) s.data[i] = words[i];
    };
    seq(1, {15});                // volume
    seq(2, {0x0105});            // mixer: square, SID
    seq(3, {0, 15, 0, 15});      // SID waveform
    seq(4, {0x000D});            // mixer: digidrum
    MyvInstrument& sid = v.instruments[0];
    sid.name = "sid";
    sid.at(MyvInstrument::kSequenceSpeed) = 1;
    sid.at(MyvInstrument::kSequences + 0) = 1;
    sid.at(MyvInstrument::kSequences + 3) = 2;
    sid.at(MyvInstrument::kSequences + 6) = 3;
    MyvInstrument& drum = v.instruments[1];
    drum.name = "drum";
    drum.at(MyvInstrument::kSequenceSpeed) = 1;
    drum.at(MyvInstrument::kSequences + 0) = 1;
    drum.at(MyvInstrument::kSequences + 3) = 4;
    drum.at(MyvInstrument::kDigidrum) = 1;
    drum.at(MyvInstrument::kDigidrumRate) = kDrumRate;
    for (int i = 0; i < kDrumLength; ++i)
        v.digidrums[0].push_back(uint8_t(int8_t(std::lround(100 * std::sin(i * 0.3) * (1 - i / double(kDrumLength))))));
    return v;
}

// Speed 3, one pattern of 64 rows: channel A plays C4, E4, G4 every 16 rows and stops at row 48, channel C a
// digidrum every 8 rows.
ymio::MysSong song() {
    ymio::MysSong s;
    s.speed = 3;
    s.length = 1;
    s.repeat = 0;
    s.header[36] = 7;  // the timers of the three channels
    s.patterns.resize(2);
    s.patterns[0].rows[0] = {60, 1};
    s.patterns[0].rows[16] = {64, 1};
    s.patterns[0].rows[32] = {67, 1};
    s.patterns[0].rows[48] = {ymio::kMysNoteOffRow};
    for (int r = 0; r < 64; r += 8) s.patterns[1].rows[size_t(r)] = {60, 2};
    s.steps[0].channel = {0, ymio::kMysEmpty, 1};
    return s;
}

struct Played {
    plugin::RegisterImport import;
    NonParamState state;
    std::vector<ymengine::RegisterFrame> frames;
};

// The import played back as File > Import sets it up: Authentic 3ch, tracks 1 to 3 on Chip Voice, track 4 on
// Digidrum.
Played play() {
    Played p;
    const plugin::RegisterImportResult r = plugin::importMaxYm(song(), voices(), p.state);
    INFO(r.error);
    REQUIRE(r.import);
    p.import = *r.import;
    plugin::ParamValues v = plugin::defaultValues();
    v[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    v[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(p.import.vblRate);
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

}  // namespace

TEST_CASE("maxYMiser import: each row a step, the notes where the rows start them", "[maxym-import]") {
    const plugin::MaxYmTune m = plugin::maxYmTune(song(), voices());
    CHECK(m.tune.frames.size() == 64 * 3);
    CHECK(m.layout.stepFrames == std::vector<int>(64, 3));
    CHECK(m.layout.partSteps == std::vector<int>{64});
    CHECK(m.layout.loopStep == 0);
    const Played p = play();
    CHECK(p.import.speed == 3);
    CHECK(p.import.patterns == 1);
    CHECK(p.import.notes == 3);
    const ymseq::Lane& a = p.state.seq.patterns[0].lane(0);
    for (int s = 0; s < 64; ++s) {
        CAPTURE(s);
        CHECK((a.pat[size_t(s)] >= 0) == (s == 0 || s == 16 || s == 32));
    }
    // C4, E4 and G4 on channel A, silent from row 48.
    auto heard = [&](int f) {
        const auto& r = p.frames[size_t(f)].reg;
        return (r[8] & 15) > 0 && !(r[7] & 1);
    };
    const double hz[] = {261.63, 329.63, 392.0};
    for (int k = 0; k < 3; ++k) {
        CAPTURE(k);
        const auto& r = p.frames[size_t(48 * k + 1)].reg;
        const int tp = r[0] | (r[1] & 15) << 8;
        CHECK(std::abs(1200 * std::log2(2'000'000.0 / 16 / tp / hz[k])) < 10);
    }
    CHECK(heard(47 * 3));
    CHECK_FALSE(heard(48 * 3 + 1));
}

TEST_CASE("maxYMiser import: a SID instrument becomes a SID Voice with its waveform", "[maxym-import]") {
    const Played p = play();
    int sid = -1;
    for (int k = 0; k < p.import.instruments; ++k)
        if (p.state.instruments[size_t(k)].patch.generator == Generator::Sid) sid = k;
    REQUIRE(sid >= 0);
    const ymengine::TrackPatch& patch = p.state.instruments[size_t(sid)].patch;
    CHECK(patch.sidForm == ymengine::SidForm::Custom);
    REQUIRE(patch.sidWaveLen == 4);
    CHECK(patch.sidWave[0] == 15);
    CHECK(patch.sidWave[1] == 0);
    CHECK(patch.sidWave[2] == 15);
    CHECK(patch.sidWave[3] == 0);
    CHECK(patch.sidRatio == 1);
    CHECK(patch.toneOn);
    // Played back: the SID of channel A at its note's frequency (4 interrupts per period, the effect counting 2).
    const double hz[] = {261.63, 329.63, 392.0};
    for (int k = 0; k < 3; ++k) {
        CAPTURE(k);
        const ymengine::RegisterFrame::Effect& e = p.frames[size_t(48 * k + 2)].fx[0];
        CHECK(e.on);
        CHECK(e.gen == Generator::Sid);
        CHECK(std::abs(1200 * std::log2(e.timer.hz() / 2 / hz[k])) < 17);
    }
}

TEST_CASE("maxYMiser import: digidrums play on the Digidrum track with their samples", "[maxym-import]") {
    const Played p = play();
    REQUIRE(p.import.digidrums);
    CHECK(p.import.samples == 1);
    CHECK(p.state.global.digidrumChannel[0] == 2);
    const plugin::DigidrumSlotState& slot = p.state.digiSlots[0];
    CHECK(slot.play.rateHz == Catch::Approx(7680).margin(1));
    CHECK(slot.source.size() == size_t(kDrumLength));
    CHECK(slot.play.end == p.state.digidrums[0].size());
    // A hit every 8 steps, in slot 1 at its root.
    const ymseq::Lane& d = p.state.seq.patterns[0].lane(3);
    for (int s = 0; s < 64; ++s) {
        CAPTURE(s);
        CHECK((d.pat[size_t(s)] == 0) == (s % 8 == 0));
        if (s % 8 == 0) CHECK(d.oct[size_t(s)] == 0);
        if (s % 8 == 0) CHECK(d.ton[size_t(s)] == 0);
    }
    // Played back on channel C at the rate of the sample, for its 10 frames (1536 samples at 7680 Hz), then silent.
    for (int f = 0; f < 64 * 3; ++f) {
        CAPTURE(f);
        const ymengine::RegisterFrame::Effect& e = p.frames[size_t(f)].fx[2];
        const int into = f % 24;
        if (into < 9) {
            CHECK(e.on);
            CHECK(e.gen == Generator::Digidrum);
            CHECK(e.timer.hz() == Catch::Approx(7680).epsilon(0.03));
        } else if (into > 11) {
            CHECK_FALSE(e.on);
        }
    }
}
