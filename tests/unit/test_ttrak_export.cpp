// STYM2149 — plugincore: export of the Song as a TTrak module: rows where the notes start, instruments that our
// model of TTrak's routine plays back to the registers that the Song plays, the end of the song, refused projects.
// Synthetic modules only.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "plugincore/TTrakExport.h"
#include "plugincore/TTrakImport.h"
#include "ymengine/Pitch.h"
#include "ymio/TTrak.h"
#include "ymio/TTrakPlayer.h"

using namespace stym;
using plugin::NonParamState;
using ymengine::Generator;
using I = ymio::TtrakInstrument;

namespace {

// Instruments: 1 a square lead decaying from 15 with an arpeggio 0, 12; 2 a noise drum; 3 a buzzer bass (shape 8);
// 4 a square SID; 5 a digidrum (slot 1). Speed 3 (four ticks a row), two positions: channel A the lead, then the SID;
// channel B the bass; channel C the drum and the digidrum.
ymio::TtrakModule module() {
    ymio::TtrakModule m = ymio::TtrakModule::blank();
    auto ins = [&](int k, const std::vector<uint8_t>& steps, int timer = 0) -> I& {
        I& r = m.instruments[size_t(k - 1)];
        for (size_t i = 0; i < steps.size(); ++i) r.at(I::kAmpEnv + int(i)) = steps[i];
        r.at(I::kAmpLast) = uint8_t(steps.size() - 1);
        r.at(I::kAmpLoop) = uint8_t(steps.size() - 1);
        r.at(I::kTimerType) = uint8_t(timer);
        r.at(I::kSampleSlot) = 0;
        return r;
    };
    I& lead = ins(1, {0x0F, 0x0E, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08});
    const uint8_t arp[] = {0, 0, 12, 0x81};
    for (int i = 0; i < 4; ++i) lead.at(I::kArpTable + i) = arp[i];
    lead.at(I::kMatrixArp) = 1;
    I& drum = ins(2, {0x6F, 0x6C, 0x69, 0x66, 0x63, 0x60});
    const uint8_t noise[] = {0, 3, 8, 15, 0xF3};
    for (int i = 0; i < 5; ++i) drum.at(I::kNoiseTable + i) = noise[i];
    I& bass = ins(3, {0x30});
    bass.at(I::kBuzzerShape) = 8;
    ins(4, {0x8F}, 1);
    ins(5, {0x8F}, 7);
    std::vector<uint8_t> levels;
    for (int i = 0; i < 300; ++i) levels.push_back(uint8_t(8 + std::lround(7 * std::sin(i * 0.3) * (1 - i / 300.0))));
    m.samples[0] = ymio::ttrakSampleBytes(levels);
    m.length = 2;
    m.order[0] = 0;
    m.order[1] = 1;
    m.patterns.resize(2);
    for (int p = 0; p < 2; ++p) {
        ymio::TtrakPattern& pat = m.patterns[size_t(p)];
        for (int r = 0; r < 64; r += 8) {
            pat.rows[size_t(r)][0] = {uint8_t(37 + (r / 8) % 5), uint8_t(p == 0 ? 1 : 4), 0, 0};
            if (r % 16 == 0) pat.rows[size_t(r)][1] = {uint8_t(25 + (r / 16) % 3), 3, 0, 0};
            pat.rows[size_t(r)][2] = {85, uint8_t(r % 16 ? 5 : 2), 0, 0};
        }
    }
    m.patterns[0].rows[0][1].effect = 0x1C;
    m.patterns[0].rows[0][1].param = 3;
    return m;
}

struct Project {
    plugin::ParamValues values{};
    NonParamState state;
};

// The module imported as File > Import does it: Authentic 3ch, A4 as TTrak tunes it, tracks 1 to 3 on Chip Voice,
// track 4 on Digidrum.
Project project(bool loop) {
    Project p;
    const plugin::RegisterImportResult r = plugin::importTtrak(module(), p.state);
    INFO(r.error);
    REQUIRE(r.import);
    p.values = plugin::defaultValues();
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    p.values[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(r.import->vblRate);
    p.values[size_t(plugin::paramIndex(plugin::Global::A4Hz))] = r.import->a4Hz;
    for (int k = 0; k < plugin::kTracks; ++k) {
        const bool on = k < 3 || (k == 3 && r.import->digidrums);
        p.values[size_t(plugin::paramIndex(k, plugin::Track::On))] = on ? 1.0f : 0.0f;
        if (!on) continue;
        p.values[size_t(plugin::paramIndex(k, plugin::Track::Generator))] =
            float(int(k == 3 ? Generator::Digidrum : Generator::Chip));
        p.values[size_t(plugin::paramIndex(k, plugin::Track::Level))] = 100;
    }
    p.state.global.loop = loop;
    return p;
}

plugin::TtrakExport exported(const Project& p) {
    const std::optional<plugin::TtrakExportResult> r = plugin::exportTtrak(p.values, p.state, "Back");
    REQUIRE(r);
    INFO(r->error);
    REQUIRE(r->value);
    return *r->value;
}

// What a channel plays at a frame: volume (or the envelope), square and noise periods when they sound, buzzer.
struct Heard {
    int vol = 0;  // 0-15, 16: envelope
    int tone = 0, noise = -1, buzzer = 0, shape = -1;
    bool operator==(const Heard&) const = default;
};
Heard heard(const std::array<uint8_t, 14>& r, int c) {
    Heard h;
    const bool tone = !(r[7] >> c & 1), noise = !(r[7] >> (3 + c) & 1);
    h.vol = r[size_t(8 + c)] & 16 ? 16 : r[size_t(8 + c)] & 15;
    if (!h.vol || (!tone && !noise && h.vol < 16)) return {};
    if (tone) h.tone = r[size_t(2 * c)] | (r[size_t(2 * c + 1)] & 15) << 8;
    if (noise) h.noise = r[6] & 31;
    if (h.vol == 16) {
        h.buzzer = r[11] | r[12] << 8;
        h.shape = r[13] & 15;
    }
    return h;
}

// The export played by the routine against the Song, frame by frame: the channels without a timer effect alike, those
// with one on the same effect within 20 cents; a Sync Buzzer may play as a plain buzzer at its rate (or a multiple).
struct Compared {
    int total = 0, same = 0, timers = 0;
};
Compared playBack(const plugin::TtrakExport& x, const std::vector<ymengine::RegisterFrame>& want) {
    const std::vector<uint8_t> file = ymio::writeTtrakModule(x.module);
    const auto back = ymio::parseTtrakModule(std::span<const uint8_t>(file.data(), file.size()));
    REQUIRE(back.value);
    ymio::TtrakPlayer player(*back.value);
    Compared n;
    for (size_t f = 0; f < want.size(); ++f) {
        const ymio::TtrakTick t = player.tick();
        std::array<uint8_t, 14> got{};
        std::copy(t.buffer.begin(), t.buffer.end(), got.begin());
        got[13] = t.reg[13];
        for (int c = 0; c < 3; ++c) {
            ++n.total;
            const ymengine::RegisterFrame::Effect& fx = want[f].fx[size_t(c)];
            if (fx.on) {
                ++n.timers;
                const ymio::TtrakChannelTick& ch = t.ch[size_t(c)];
                const int shape = got[13] & 15;
                if (fx.gen == Generator::SyncBuzzer && !ch.timer && (got[size_t(8 + c)] & 16) && shape >= 8 &&
                    !(shape & 1)) {
                    const int ep = std::max(1, got[11] | got[12] << 8);
                    const double env = 2e6 / (256.0 * ymengine::envelopeRamps(shape) * ep);
                    const double k = std::round(env / fx.timer.hz());
                    n.same += k >= 1 && std::abs(1200 * std::log2(env / (k * fx.timer.hz()))) < 20;
                    continue;
                }
                const bool kind = (fx.gen == Generator::Sid && ch.timer >= 1 && ch.timer <= 3) ||
                                  (fx.gen == Generator::Digidrum && ch.timer == 7);
                const ymengine::MfpSetting mfp{ch.timerPrescaler, uint16_t(ch.timerData ? ch.timerData : 256)};
                const bool rate = fx.gen == Generator::Digidrum ||
                                  std::abs(1200 * std::log2(mfp.hz() / fx.timer.hz())) < 20;
                n.same += kind && ch.timerRuns && rate;
                continue;
            }
            const Heard a = heard(want[f].reg, c), b = heard(got, c);
            if (a == b) ++n.same;
            else UNSCOPED_INFO("frame " << f << " channel " << c << ": vol " << a.vol << "/" << b.vol << " tone "
                                        << a.tone << "/" << b.tone << " noise " << a.noise << "/" << b.noise
                                        << " buzzer " << a.buzzer << "/" << b.buzzer);
        }
    }
    return n;
}

bool has(const std::vector<std::string>& lines, const std::string& start) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.rfind(start, 0) == 0; });
}

}  // namespace

TEST_CASE("TTrak export: the routine plays the module back as the Song plays it", "[ttrak-export]") {
    const Project p = project(true);
    const plugin::TtrakExport x = exported(p);
    CHECK(x.module.titleText() == "Back");
    CHECK(x.module.rate == 50);
    CHECK(x.loops);
    CHECK(x.module.restart == 0);
    CHECK(x.instruments <= 32);
    CHECK(x.samples == 1);
    CHECK_FALSE(has(x.remarks, "Frames that TTrak plays otherwise"));
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, p.values, p.state);
    REQUIRE(cap);
    const std::vector<ymengine::RegisterFrame>& want = cap->take.frames;
    REQUIRE(want.size() >= 2 * 64 * 4);
    const Compared n = playBack(x, want);
    CHECK(n.timers > 0);
    CHECK(n.same == n.total);
}

TEST_CASE("TTrak export: notes on the rows where they start, the speed on the first row", "[ttrak-export]") {
    const plugin::TtrakExport x = exported(project(true));
    const ymio::TtrakModule& m = x.module;
    REQUIRE(m.length >= 2);
    const ymio::TtrakPattern& first = m.patterns[m.order[0]];
    // Four frames a row: $1C 3 on row 0.
    bool speed = false;
    for (const ymio::TtrakCell& c : first.rows[0]) speed = speed || (c.effect == 0x1C && c.param == 3);
    CHECK(speed);
    for (int r = 0; r < 64; ++r) {
        CAPTURE(r);
        const ymio::TtrakCell& a = first.rows[size_t(r)][0];
        if (r % 8 == 0) {
            CHECK(a.note == 37 + (r / 8) % 5);
            CHECK(a.instrument >= 1);
        } else {
            CHECK(a.note == 0);
        }
        CHECK((first.rows[size_t(r)][1].note != 0) == (r % 16 == 0));
    }
    // The digidrum's sample, as volumes 0-15 with its end byte.
    CHECK(ymio::ttrakSampleLevels(m.samples[0]).size() == 300);
    // The lines of the alert.
    const std::vector<std::string> lines = plugin::ttrakExportMessage(x, "BACK.TPM");
    REQUIRE(lines.size() >= 5);
    CHECK(lines[0] == "TTrak module exported");
    CHECK(lines[2] == "BACK.TPM");
    CHECK(lines[4] == "50 Hz, loops at position 1.");
}

TEST_CASE("TTrak export: without Loop, the song ends on a position that silences the three channels",
          "[ttrak-export]") {
    const plugin::TtrakExport x = exported(project(false));
    const ymio::TtrakModule& m = x.module;
    CHECK_FALSE(x.loops);
    REQUIRE(m.length >= 1);
    CHECK(m.restart == m.length - 1);
    const ymio::TtrakPattern& end = m.patterns[m.order[size_t(m.length - 1)]];
    for (int c = 0; c < 3; ++c) {
        const int k = end.rows[0][size_t(c)].instrument;
        REQUIRE(k >= 1);
        CHECK(m.instruments[size_t(k - 1)].name() == "Silence");
    }
    // Played on past its end: silent.
    ymio::TtrakPlayer player(m);
    for (int f = 0; f < 2 * 64 * 4 + 20; ++f) player.tick();
    const ymio::TtrakTick t = player.tick();
    for (int c = 0; c < 3; ++c) CHECK((t.buffer[size_t(8 + c)] & 31) == 0);
    CHECK(plugin::ttrakExportMessage(x, "A.TPM")[4] == "50 Hz, plays once.");
}

TEST_CASE("TTrak export: refused outside Authentic 3ch and without a Song", "[ttrak-export]") {
    Project p = project(true);
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 0;
    std::optional<plugin::TtrakExportResult> r = plugin::exportTtrak(p.values, p.state, "x");
    REQUIRE(r);
    CHECK_FALSE(r->value);
    CHECK(r->error == "TTrak plays the three channels of one chip: switch to Authentic 3ch");
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    p.state.seq.song.entries.clear();
    r = plugin::exportTtrak(p.values, p.state, "x");
    REQUIRE(r);
    CHECK_FALSE(r->value);
    CHECK(r->error == "The Song is empty.");
    // Cancelled: nothing.
    const std::atomic<bool> cancel{true};
    CHECK_FALSE(plugin::exportTtrak(project(true).values, project(true).state, "x", &cancel));
}
