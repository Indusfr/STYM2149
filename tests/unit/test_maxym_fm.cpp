// STYM2149 — maxYMiser's square FM and the FM Voice in semitones: the steps that the routine writes, the
// import as an FM Voice in the Semitones scale, the export of an FM Voice back to square FM.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/MaxYmExport.h"
#include "plugincore/MaxYmImport.h"
#include "plugincore/RegisterExport.h"
#include "ymio/MaxYm.h"
#include "ymio/MaxYmPlayer.h"

using namespace stym;
using plugin::NonParamState;
using ymengine::FmScale;
using ymengine::Generator;
using ymio::MyvInstrument;

namespace {

constexpr int kMixSeq = 3, kWaveSeq = 6;  // sequences of an instrument: mixer, timer waveform

double cents(double a, double b) { return std::abs(1200 * std::log2(a / b)); }

// Instrument 1: square FM (mixer $10F) whose timer sequence holds `steps`, the semitones of each step.
ymio::MyvVoiceset fmVoices(const std::vector<uint16_t>& steps) {
    ymio::MyvVoiceset v;
    v.sequences.resize(4);
    auto seq = [&](int n, const std::vector<uint16_t>& words) {
        ymio::MyvSequence& s = v.sequences[size_t(n)];
        s.length = uint8_t(words.size());
        s.repeat = uint8_t(words.size() - 1);
        for (size_t i = 0; i < words.size(); ++i) s.data[i] = words[i];
    };
    seq(1, {15});
    seq(2, {0x010F});
    seq(3, steps);
    MyvInstrument& fm = v.instruments[0];
    fm.name = "fm";
    fm.at(MyvInstrument::kSequenceSpeed) = 1;
    fm.at(MyvInstrument::kSequences + 0) = 1;
    fm.at(MyvInstrument::kSequences + kMixSeq) = 2;
    fm.at(MyvInstrument::kSequences + kWaveSeq) = 3;
    return v;
}

// Speed 3, one pattern: channel A plays C4 at row 0 and G4 at row 32.
ymio::MysSong fmSong() {
    ymio::MysSong s;
    s.speed = 3;
    s.length = 1;
    s.repeat = 0;
    s.header[36] = 7;  // the timers of the three channels
    s.patterns.resize(1);
    s.patterns[0].rows[0] = {60, 1};
    s.patterns[0].rows[32] = {67, 1};
    s.steps[0].channel = {0, ymio::kMysEmpty, ymio::kMysEmpty};
    return s;
}

// The import as File > Import sets it up (Authentic 3ch, tracks 1 to 3 on).
struct Imported {
    plugin::ParamValues values{};
    NonParamState state;
    plugin::RegisterImport import;
};
Imported imported(const std::vector<uint16_t>& steps) {
    Imported p;
    const plugin::RegisterImportResult r = plugin::importMaxYm(fmSong(), fmVoices(steps), p.state);
    INFO(r.error);
    REQUIRE(r.import);
    p.import = *r.import;
    p.values = plugin::defaultValues();
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    p.values[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(p.import.vblRate);
    for (int k = 0; k < plugin::kTracks; ++k) {
        p.values[size_t(plugin::paramIndex(k, plugin::Track::On))] = k < 3 ? 1.0f : 0.0f;
        if (k < 3) p.values[size_t(plugin::paramIndex(k, plugin::Track::Level))] = 100;
    }
    return p;
}

// The instrument of an export whose mixer holds square FM, and its timer sequence.
const ymio::MyvSequence* fmSequence(const ymio::MyvVoiceset& v) {
    for (const MyvInstrument& ins : v.instruments) {
        const int mix = ins.at(MyvInstrument::kSequences + kMixSeq);
        if (!mix || size_t(mix) >= v.sequences.size()) continue;
        if ((v.sequences[size_t(mix)].data[0] & 0xF) != 0xF) continue;
        const int wave = ins.at(MyvInstrument::kSequences + kWaveSeq);
        return wave && size_t(wave) < v.sequences.size() ? &v.sequences[size_t(wave)] : nullptr;
    }
    return nullptr;
}

std::vector<int> words(const ymio::MyvSequence& s) {
    std::vector<int> w;
    for (int i = 0; i < s.length; ++i) w.push_back(s.data[size_t(i)] & 0x7F);
    return w;
}

void note(ymseq::Lane& l, int step, int midi) {
    const int oc = midi / 12 - 1;
    l.pat[size_t(step)] = int16_t(midi - (oc + 1) * 12);
    l.oct[size_t(step)] = int8_t(ymseq::octaveCode(oc, 3));
}

// A Song of one note C4 on an FM Voice of track 1 with this patch, at 120 BPM in Authentic 3ch.
struct Made {
    plugin::ParamValues values = plugin::defaultValues();
    NonParamState state;
};
Made fmProject(const ymengine::TrackPatch& patch) {
    Made m;
    m.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    m.values[size_t(plugin::paramIndex(plugin::Global::FreeTempo))] = 120;
    m.values[size_t(plugin::paramIndex(0, plugin::Track::On))] = 1;
    m.values[size_t(plugin::paramIndex(0, plugin::Track::Generator))] = float(int(Generator::Fm));
    m.state.tracks[0].patch = patch;
    m.state.tracks[0].patch.generator = Generator::Fm;
    ymseq::Pattern& pat = m.state.seq.cur();
    pat.steps = 16;
    note(pat.edit(0), 0, 60);
    m.state.seq.song.entries = {{0, 1, 0}};
    return m;
}

bool remark(const plugin::MaxYmExport& e, const std::string& head) {
    return std::any_of(e.remarks.begin(), e.remarks.end(),
                       [&](const std::string& r) { return r.rfind(head, 0) == 0; });
}

}  // namespace

TEST_CASE("Square FM: a step's period is the note's × 65536 × 2^(-t/12), rounded, over 65536", "[maxym][fm]") {
    CHECK(ymengine::fmSemitoneFactor(12) == 32768);
    CHECK(ymengine::fmSemitoneFactor(24) == 16384);
    CHECK(ymengine::fmSemitoneFactor(7) == 43740);
    CHECK(ymengine::fmSemitoneFactor(127) == 43);
    CHECK(ymengine::fmSemitonePeriod(478, 0) == 478);   // the note's own period
    CHECK(ymengine::fmSemitonePeriod(478, 12) == 239);  // its octave
    CHECK(ymengine::fmSemitonePeriod(477, 12) == 238);  // rounded down
    CHECK(ymengine::fmSemitonePeriod(1000, 7) == 667);
    CHECK(ymengine::fmSemitonePeriod(1000, 200) == ymengine::fmSemitonePeriod(1000, 127));
    // The multiples of the Ratios scale at their nearest semitones.
    CHECK(ymengine::fmSemitonesOf(0) == -12);  // x0.5
    CHECK(ymengine::fmSemitonesOf(2) == 7);    // x1.5
    CHECK(ymengine::fmSemitonesOf(4) == 19);   // x3
    CHECK(ymengine::fmSemitonesOf(6) == 28);   // x5
    CHECK(ymengine::fmSemitonesOf(7) == 31);   // x6
    CHECK(std::abs(ymengine::fmSemitoneCents(6) + 13.7) < 0.1);
}

TEST_CASE("maxYMiser player: square FM runs two interrupts per period of its note, each step its semitones",
          "[maxym][fm]") {
    ymio::MaxYmPlayer p(fmSong(), fmVoices({0, 12, 24}));
    ymio::MaxYmTick t;
    for (int i = 0; i < 3; ++i) t = p.tick();
    const ymio::MaxYmChannelTick& ch = t.ch[0];
    CHECK(ch.timer == uint8_t(ymio::MaxYmTimer::SquareFm));
    REQUIRE(ch.timerRuns);
    REQUIRE(ch.timerSteps == 3);
    CHECK((ch.timerWave[0] & 0x7F) == 0);
    CHECK((ch.timerWave[1] & 0x7F) == 12);
    CHECK((ch.timerWave[2] & 0x7F) == 24);
    const double hz = ymengine::MfpSetting{ch.timerPrescaler, ch.timerData}.hz();
    CHECK(cents(hz, 2 * 2'000'000.0 / 16 / ch.timerPeriod) < 20);
    // Eight steps at most.
    ymio::MaxYmPlayer q(fmSong(), fmVoices({0, 12, 0, 12, 0, 12, 0, 12, 24, 36}));
    for (int i = 0; i < 3; ++i) t = q.tick();
    CHECK(t.ch[0].timerSteps == 8);
}

TEST_CASE("maxYMiser import: a square FM instrument becomes an FM Voice in semitones", "[maxym-import][fm]") {
    Imported p = imported({0, 12, 19, 7});
    const ymengine::TrackPatch* fm = nullptr;
    for (int k = 0; k < p.import.instruments; ++k)
        if (p.state.instruments[size_t(k)].patch.generator == Generator::Fm) fm = &p.state.instruments[size_t(k)].patch;
    REQUIRE(fm);
    CHECK(fm->fmScale == FmScale::Semitones);
    REQUIRE(fm->fmWaveLen == 4);
    CHECK(fm->fmSemi[0] == 0);
    CHECK(fm->fmSemi[1] == 12);
    CHECK(fm->fmSemi[2] == 19);
    CHECK(fm->fmSemi[3] == 7);
    CHECK(fm->fmWave == ymengine::TrackPatch{}.fmWave);  // the Ratios wave left as it is
    CHECK(std::abs(fm->sidRatio - 0.5) < 0.01);         // two of its four steps per period of the note
    for (const std::string& r : p.import.remarks) CHECK(r.find("FM") == std::string::npos);

    // Played back: the engine's steps and rate are the routine's.
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, p.values, p.state);
    REQUIRE(cap);
    ymio::MaxYmPlayer routine(fmSong(), fmVoices({0, 12, 19, 7}));
    int checked = 0;
    for (size_t f = 0; f < cap->take.frames.size(); ++f) {
        const ymio::MaxYmTick t = routine.tick();
        const ymengine::RegisterFrame::Effect& e = cap->take.frames[f].fx[0];
        if (f < 2 || !e.on || !t.ch[0].timerRuns) continue;
        CAPTURE(f);
        CHECK(e.gen == Generator::Fm);
        CHECK(e.semis);
        CHECK(e.value == 4);
        CHECK(e.wave[1] == 12);
        CHECK(e.wave[2] == 19);
        CHECK(cents(e.timer.hz(), ymengine::MfpSetting{t.ch[0].timerPrescaler, t.ch[0].timerData}.hz()) < 20);
        ++checked;
    }
    CHECK(checked > 30);
}

TEST_CASE("maxYMiser export: an FM Voice in semitones goes back to square FM with its steps",
          "[maxym-export][fm]") {
    const Imported p = imported({0, 12, 19, 7});
    const auto x = plugin::exportMaxYm(p.values, p.state);
    REQUIRE(x);
    INFO(x->error);
    REQUIRE(x->value);
    for (const std::string& r : x->value->remarks) UNSCOPED_INFO(r);
    const ymio::MyvSequence* s = fmSequence(x->value->voices);
    REQUIRE(s);
    CHECK(words(*s) == std::vector<int>{0, 12, 19, 7});
    CHECK_FALSE(remark(*x->value, "FM Voice"));
    // The export played by the routine: square FM at the rate of the song it came from.
    ymio::MaxYmPlayer original(fmSong(), fmVoices({0, 12, 19, 7}));
    ymio::MaxYmPlayer back(x->value->song, x->value->voices);
    int same = 0, both = 0;
    for (int f = 0; f < 150; ++f) {
        const ymio::MaxYmChannelTick a = original.tick().ch[0], b = back.tick().ch[0];
        if (!a.timerRuns) continue;
        ++both;
        same += b.timer == 0xF && b.timerRuns && b.timerSteps == 4 &&
                cents(ymengine::MfpSetting{a.timerPrescaler, a.timerData}.hz(),
                      ymengine::MfpSetting{b.timerPrescaler, b.timerData}.hz()) < 20;
    }
    CHECK(both > 100);
    CHECK(same >= both - 2);
}

TEST_CASE("maxYMiser export: an FM Voice in ratios plays its multiples at their nearest semitones",
          "[maxym-export][fm]") {
    // The default wave: the note, then its octave.
    {
        const Made m = fmProject(ymengine::TrackPatch{});
        const auto x = plugin::exportMaxYm(m.values, m.state);
        REQUIRE(x);
        INFO(x->error);
        REQUIRE(x->value);
        const ymio::MyvSequence* s = fmSequence(x->value->voices);
        REQUIRE(s);
        CHECK(words(*s) == std::vector<int>{0, 12});
        CHECK_FALSE(remark(*x->value, "FM Voice"));
    }
    // x1, x5 and x0.5: x5 a few cents off, x0.5 below the note, played as the note; named in the remarks.
    {
        ymengine::TrackPatch patch;
        patch.fmWave[0] = ymengine::kFmNote;
        patch.fmWave[1] = 6;
        patch.fmWave[2] = 0;
        patch.fmWaveLen = 3;
        const Made m = fmProject(patch);
        const auto x = plugin::exportMaxYm(m.values, m.state);
        REQUIRE(x);
        REQUIRE(x->value);
        const ymio::MyvSequence* s = fmSequence(x->value->voices);
        REQUIRE(s);
        CHECK(words(*s) == std::vector<int>{0, 28, 0});
        CHECK(remark(*x->value, "FM Voice frames whose steps play approximately"));
    }
    // Sixteen steps that repeat every two: the two of them; ten that do not: their first eight, named.
    {
        ymengine::TrackPatch patch;
        patch.fmScale = FmScale::Semitones;
        patch.fmWaveLen = 16;
        const Made m = fmProject(patch);
        const auto x = plugin::exportMaxYm(m.values, m.state);
        REQUIRE(x);
        REQUIRE(x->value);
        const ymio::MyvSequence* s = fmSequence(x->value->voices);
        REQUIRE(s);
        CHECK(words(*s) == std::vector<int>{0, 12});
        CHECK_FALSE(remark(*x->value, "FM Voice"));
    }
    {
        ymengine::TrackPatch patch;
        patch.fmScale = FmScale::Semitones;
        for (int i = 0; i < 10; ++i) patch.fmSemi[size_t(i)] = uint8_t(i);
        patch.fmWaveLen = 10;
        const Made m = fmProject(patch);
        const auto x = plugin::exportMaxYm(m.values, m.state);
        REQUIRE(x);
        REQUIRE(x->value);
        const ymio::MyvSequence* s = fmSequence(x->value->voices);
        REQUIRE(s);
        CHECK(words(*s) == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7});
        CHECK(remark(*x->value, "FM Voice frames whose steps play approximately"));
    }
}
