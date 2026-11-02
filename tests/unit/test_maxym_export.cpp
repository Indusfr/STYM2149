// STYM2149 — plugincore: export of the Song as a maxYMiser song and voiceset: rows where the notes start,
// instruments that maxYMiser's routine plays back to the registers that the Song plays.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/MaxYmExport.h"
#include "plugincore/MaxYmImport.h"
#include "plugincore/RegisterExport.h"
#include "ymio/MaxYm.h"
#include "ymio/MaxYmPlayer.h"

using namespace stym;
using plugin::NonParamState;
using ymengine::Generator;
using ymio::MyvInstrument;

namespace {

// Instrument 1: square with a volume decay and an arpeggio; 2: square and SID; 3: buzzer (envelope 8) on the note,
// imported as a Sync Buzzer; 4: digidrum 1.
ymio::MyvVoiceset voices() {
    ymio::MyvVoiceset v;
    v.sequences.resize(9);
    auto seq = [&](int n, const std::vector<uint16_t>& words) {
        ymio::MyvSequence& s = v.sequences[size_t(n)];
        s.length = uint8_t(words.size());
        s.repeat = uint8_t(words.size() - 1);
        for (size_t i = 0; i < words.size(); ++i) s.data[i] = words[i];
    };
    seq(1, {15, 14, 13, 12, 11, 10, 9, 8});
    seq(2, {0x0100});              // mixer: square
    seq(3, {0, 0, 12, 12, 7, 7});  // arpeggio
    seq(4, {0x0105});              // square and SID
    seq(5, {0, 15, 0, 15});        // SID waveform
    seq(6, {0x0010});              // mixer: buzzer, the instrument's shape
    seq(7, {0x000D});              // digidrum
    seq(8, {15});
    auto ins = [&](int i, const char* name, int vol, int mix) {
        MyvInstrument& x = v.instruments[size_t(i)];
        x.name = name;
        x.at(MyvInstrument::kSequenceSpeed) = 1;
        x.at(MyvInstrument::kSequences + 0) = uint8_t(vol);
        x.at(MyvInstrument::kSequences + 3) = uint8_t(mix);
        return std::ref(x);
    };
    MyvInstrument& lead = ins(0, "lead", 1, 2);
    lead.at(MyvInstrument::kArpeggio) = 4;
    lead.at(MyvInstrument::kSequences + 1) = 3;
    MyvInstrument& sid = ins(1, "sid", 8, 4);
    sid.at(MyvInstrument::kSequences + 6) = 5;
    MyvInstrument& bass = ins(2, "bass", 8, 6);
    bass.at(MyvInstrument::kBuzzerWave) = 8;
    bass.at(MyvInstrument::kBuzzerSync) = 1;
    MyvInstrument& drum = ins(3, "drum", 8, 7);
    drum.at(MyvInstrument::kDigidrum) = 1;
    drum.at(MyvInstrument::kDigidrumRate) = 80;
    for (int i = 0; i < 1536; ++i)
        v.digidrums[0].push_back(uint8_t(int8_t(std::lround(100 * std::sin(i * 0.3) * (1 - i / 1536.0)))));
    return v;
}

// Speed 3, two patterns: channel A a lead, then a SID; channel B a buzzer bass; channel C digidrums.
ymio::MysSong song() {
    ymio::MysSong s;
    s.speed = 3;
    s.length = 2;
    s.repeat = 0;
    s.header[36] = 7;
    s.patterns.resize(4);
    for (int r = 0; r < 64; r += 16) s.patterns[0].rows[size_t(r)] = {uint8_t(60 + r / 8), 1};
    for (int r = 0; r < 64; r += 16) s.patterns[1].rows[size_t(r)] = {uint8_t(62 + r / 16), 2};
    for (int r = 0; r < 64; r += 32) s.patterns[2].rows[size_t(r)] = {uint8_t(36 + r / 16), 3};
    s.patterns[2].rows[48] = {ymio::kMysNoteOffRow};
    for (int r = 0; r < 64; r += 8) s.patterns[3].rows[size_t(r)] = {60, 4};
    s.steps[0].channel = {0, 2, 3};
    s.steps[1].channel = {1, 2, 3};
    return s;
}

// The tune imported as the File menu does it (Authentic 3ch, track 4 on Digidrum), with Loop as given.
struct Project {
    plugin::ParamValues values{};
    NonParamState state;
};
Project project(bool loop) {
    Project p;
    const plugin::RegisterImportResult r = plugin::importMaxYm(song(), voices(), p.state);
    INFO(r.error);
    REQUIRE(r.import);
    p.values = plugin::defaultValues();
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    p.values[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(r.import->vblRate);
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
// with one on the same effect (and rate within 20 cents).
struct Played {
    int total = 0, same = 0, timers = 0, timersSame = 0;
};
Played playBack(const plugin::MaxYmExport& e, const std::vector<ymengine::RegisterFrame>& want) {
    // The files read back as written.
    const auto mys = ymio::parseMys(ymio::writeMys(e.song));
    const auto myv = ymio::parseMyv(ymio::writeMyv(e.voices));
    REQUIRE(mys.value);
    REQUIRE(myv.value);
    ymio::MaxYmPlayer player(*mys.value, *myv.value);
    Played n;
    for (size_t f = 0; f < want.size(); ++f) {
        const ymio::MaxYmTick t = player.tick();
        std::array<uint8_t, 14> got{};
        std::copy(t.buffer.begin(), t.buffer.end(), got.begin());
        got[13] = t.reg[13];
        for (int c = 0; c < 3; ++c) {
            const ymengine::RegisterFrame::Effect& fx = want[f].fx[size_t(c)];
            ++n.total;
            const Heard a = heard(want[f].reg, c), b = heard(got, c);
            if (fx.on) {
                // Timer effects: the routine's effect and rate (the SID and digidrum drive the volume, the sync buzzer
                // the shape).
                ++n.timers;
                const ymio::MaxYmChannelTick& ch = t.ch[size_t(c)];
                const double hz = ch.timerRuns ? ymengine::MfpSetting{ch.timerPrescaler, ch.timerData}.hz() : 0;
                const bool kind = (fx.gen == Generator::Sid && ch.timer == 5) ||
                                  (fx.gen == Generator::SyncBuzzer && ch.timer == 0xB) ||
                                  (fx.gen == Generator::Digidrum && ch.timer == 0xD) ||
                                  (fx.gen == Generator::Fm && ch.timer == 0xF);
                n.timersSame += kind && std::abs(1200 * std::log2(hz / fx.timer.hz())) < 20;
                n.same += kind;
                continue;
            }
            if (a == b) ++n.same;
            else UNSCOPED_INFO("frame " << f << " channel " << c << ": vol " << a.vol << "/" << b.vol << " tone "
                                        << a.tone << "/" << b.tone << " noise " << a.noise << "/" << b.noise
                                        << " buzzer " << a.buzzer << "/" << b.buzzer);
        }
    }
    return n;
}

void note(ymseq::Lane& l, int step, int midi) {
    const int oc = midi / 12 - 1;
    l.pat[size_t(step)] = int16_t(midi - (oc + 1) * 12);
    l.oct[size_t(step)] = int8_t(ymseq::octaveCode(oc, 3));
}

}  // namespace

TEST_CASE("maxYMiser export: the routine plays the song back as the Song plays it", "[maxym-export]") {
    const Project p = project(true);
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, p.values, p.state);
    REQUIRE(cap);
    const std::optional<plugin::MaxYmExportResult> x = plugin::exportMaxYm(p.values, p.state);
    REQUIRE(x);
    INFO(x->error);
    REQUIRE(x->value);
    const plugin::MaxYmExport& e = *x->value;
    for (const std::string& r : e.remarks) UNSCOPED_INFO(r);
    CHECK(e.song.speed == 3);
    CHECK(e.loops);
    CHECK(e.song.repeat == 0);
    CHECK(e.samples == 1);
    CHECK(e.instruments <= 32);
    const Played n = playBack(e, cap->take.frames);
    CHECK(n.total == int(cap->take.frames.size()) * 3);
    CHECK(n.same == n.total);
    CHECK(n.timers > 0);
    CHECK(n.timersSame == n.timers);
}

TEST_CASE("maxYMiser export: a Song made in STYM2149, rows of 6 and 7 frames", "[maxym-export]") {
    // 120 BPM: a step lasts 6.25 frames at 50 Hz. Channel A a vibrato lead with an ADSR, B a SID, C a Chipdrum beat.
    plugin::ParamValues v = plugin::defaultValues();
    NonParamState x;
    auto param = [&](int t, plugin::Track f) -> float& { return v[size_t(plugin::paramIndex(t, f))]; };
    v[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    v[size_t(plugin::paramIndex(plugin::Global::FreeTempo))] = 120;
    for (int t = 0; t < 3; ++t) param(t, plugin::Track::On) = 1;
    param(0, plugin::Track::VibDepth) = 30;
    param(0, plugin::Track::Attack) = 40;
    param(0, plugin::Track::Decay) = 200;
    param(0, plugin::Track::Sustain) = 60;
    param(0, plugin::Track::Release) = 100;
    param(1, plugin::Track::Generator) = float(int(Generator::Sid));
    param(2, plugin::Track::Generator) = float(int(Generator::Chipdrum));
    ymseq::Pattern& pat = x.seq.cur();
    pat.steps = 16;
    for (int k = 0; k < 16; k += 4) note(pat.edit(0), k, 60 + k / 2);
    for (int k = 2; k < 16; k += 8) note(pat.edit(1), k, 55 + k);
    for (int k = 0; k < 16; k += 2) {
        pat.edit(2).pat[size_t(k)] = int16_t(k % 4 == 0 ? 0 : 2);
        pat.edit(2).oct[size_t(k)] = int8_t(ymseq::octaveCode(3, 3));
    }
    x.seq.song.entries = {{0, 2, 0}};
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, v, x);
    REQUIRE(cap);
    const auto e = plugin::exportMaxYm(v, x);
    REQUIRE(e);
    INFO(e->error);
    REQUIRE(e->value);
    for (const std::string& r : e->value->remarks) UNSCOPED_INFO(r);
    // Rows of 6 and 7 frames: the speed changes where they alternate.
    int speeds = 0;
    for (const ymio::MysPattern& q : e->value->song.patterns)
        for (const ymio::MysRow& r : q.rows) speeds += r.effect1 == 'S';
    CHECK(speeds > 0);
    CHECK(e->value->instruments >= 3);
    const Played n = playBack(*e->value, cap->take.frames);
    // Something plays on each channel.
    for (int c = 0; c < 3; ++c)
        CHECK(std::any_of(cap->take.frames.begin(), cap->take.frames.end(),
                          [&](const ymengine::RegisterFrame& f) { return heard(f.reg, c).vol > 0 || f.fx[size_t(c)].on; }));
    CHECK(n.same == n.total);
    CHECK(n.timers > 0);
    CHECK(n.timersSame == n.timers);
}

TEST_CASE("maxYMiser export: notes on the rows where they start, a note off where the channel falls silent",
          "[maxym-export]") {
    const Project p = project(false);
    const std::optional<plugin::MaxYmExportResult> x = plugin::exportMaxYm(p.values, p.state);
    REQUIRE(x);
    REQUIRE(x->value);
    const plugin::MaxYmExport& e = *x->value;
    CHECK_FALSE(e.loops);
    // Two song steps of 64 rows, then the end that cuts the channels.
    REQUIRE(e.song.length == 3);
    CHECK(e.song.repeat == 2);
    CHECK(e.song.steps[2].channel == std::array<uint8_t, 3>{ymio::kMysNoteOff, ymio::kMysNoteOff, ymio::kMysNoteOff});
    const ymio::MysPattern& a = e.song.patterns[e.song.steps[0].channel[0]];
    for (int r = 0; r < 64; ++r) {
        CAPTURE(r);
        CHECK((a.rows[size_t(r)].note >= 2) == (r % 16 == 0));
        if (r % 16 == 0) CHECK(a.rows[size_t(r)].note == 60 + r / 8);
    }
    // Channel B: the buzzer bass from row 0 and 32, silent from row 48.
    const ymio::MysPattern& b = e.song.patterns[e.song.steps[0].channel[1]];
    CHECK(b.rows[0].note == 36);
    CHECK(b.rows[32].note == 38);
    CHECK(b.rows[48].note == ymio::kMysNoteOffRow);
    // Channel C: a digidrum every 8 rows, with its sample.
    const ymio::MysPattern& c = e.song.patterns[e.song.steps[0].channel[2]];
    for (int r = 0; r < 64; r += 8) CHECK(c.rows[size_t(r)].instrument != 0);
    CHECK(e.voices.digidrums[0].size() == 1536);
    // The same pattern of channels B and C in both steps.
    CHECK(e.song.steps[1].channel[1] == e.song.steps[0].channel[1]);
    CHECK(e.song.steps[1].channel[2] == e.song.steps[0].channel[2]);
}

TEST_CASE("maxYMiser export: refused outside Authentic 3ch and without a Song", "[maxym-export]") {
    Project p = project(false);
    p.values[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 0;
    auto x = plugin::exportMaxYm(p.values, p.state);
    REQUIRE(x);
    CHECK_FALSE(x->value);
    CHECK(x->error.find("Authentic 3ch") != std::string::npos);
    p = project(false);
    p.state.seq.song.entries.clear();
    x = plugin::exportMaxYm(p.values, p.state);
    REQUIRE(x);
    CHECK_FALSE(x->value);
}
