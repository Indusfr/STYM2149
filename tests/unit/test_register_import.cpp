// STYM2149 — plugincore: import of YM and SNDH tunes as editable patterns: grid guess, notes, Dxx, GAT,
// envelope, shared instruments, refused tunes, and the playback of the import compared frame by frame with the tune
// (at 60 Hz too, where the steps of the Ticks timing start at their tick).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "plugincore/RegisterImport.h"

using namespace stym;
using plugin::NonParamState;
using plugin::RegisterGrid;
using plugin::RegisterImport;
using plugin::RegisterTune;

namespace {

using Regs = std::array<uint8_t, 14>;

// A silent tune of n frames.
RegisterTune silence(int n, uint32_t rate = 50) {
    RegisterTune t;
    t.title = "test";
    t.rate = rate;
    Regs r{};
    r[7] = 0x3f;
    t.frames.assign(size_t(n), r);
    t.restart.assign(size_t(n), 0);
    return t;
}

// Voice c plays tone period tp from frame `at`, one volume per frame.
void tone(RegisterTune& t, int c, int at, int tp, const std::vector<int>& vols) {
    REQUIRE(size_t(at) + vols.size() <= t.frames.size());
    for (size_t i = 0; i < vols.size(); ++i) {
        Regs& r = t.frames[size_t(at) + i];
        r[size_t(2 * c)] = uint8_t(tp & 255);
        r[size_t(2 * c + 1)] = uint8_t(tp >> 8);
        r[7] = uint8_t(r[7] & ~(1 << c));
        r[size_t(8 + c)] = uint8_t(vols[i]);
    }
}

// Voice c plays the envelope (shape, period ep) on tone period tp from frame `at` for n frames, R13 written at `at`.
void envelope(RegisterTune& t, int c, int at, int n, int tp, int shape, int ep) {
    REQUIRE(size_t(at + n) <= t.frames.size());
    for (int i = 0; i < n; ++i) {
        Regs& r = t.frames[size_t(at + i)];
        r[size_t(2 * c)] = uint8_t(tp & 255);
        r[size_t(2 * c + 1)] = uint8_t(tp >> 8);
        r[7] = uint8_t(r[7] & ~(1 << c));
        r[size_t(8 + c)] = 16;
        r[11] = uint8_t(ep & 255);
        r[12] = uint8_t(ep >> 8);
        r[13] = uint8_t(shape);
    }
    t.restart[size_t(at)] = 1;
}

const std::vector<int> kPluck = {15, 13, 11, 9, 7, 5};

struct Played {
    RegisterImport import;
    NonParamState state;
    std::vector<Regs> frames;  // frames of the playback, from the first step
};

// The import with `grid`, played back as File > Import sets it up (Authentic 3ch, tracks 1 to 3 on Chip Voice).
Played play(const RegisterTune& t, const RegisterGrid& grid) {
    Played p;
    const plugin::RegisterImportResult r = plugin::importRegisters(t, grid, p.state);
    INFO(r.error);
    REQUIRE(r.import);
    p.import = *r.import;
    plugin::ParamValues v = plugin::defaultValues();
    v[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1;
    v[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(p.import.vblRate);
    v[size_t(plugin::paramIndex(plugin::Global::A4Hz))] = p.import.a4Hz;
    for (int k = 0; k < plugin::kTracks; ++k) {
        v[size_t(plugin::paramIndex(k, plugin::Track::On))] = k < 3 ? 1.0f : 0.0f;
        if (k < 3) {
            v[size_t(plugin::paramIndex(k, plugin::Track::Generator))] = float(int(ymengine::Generator::Chip));
            v[size_t(plugin::paramIndex(k, plugin::Track::Level))] = 100;
        }
    }
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, v, p.state);
    REQUIRE(cap);
    for (const auto& f : cap->take.frames) p.frames.push_back(f.reg);
    return p;
}

struct Voice {
    bool on = false, env = false;
    int vol = 0, tp = 0;
};
Voice voiceOf(const Regs& r, int c) {
    Voice v;
    const bool toneOn = !(r[7] >> c & 1);
    v.env = (r[size_t(8 + c)] & 16) != 0;
    v.vol = r[size_t(8 + c)] & 15;
    v.tp = r[size_t(2 * c)] | (r[size_t(2 * c + 1)] & 15) << 8;
    v.on = v.env || (toneOn && v.vol > 0);
    return v;
}

// Frames [from, to) of voice c where the playback differs from the tune (on/off, volume or envelope, period ±1).
std::vector<int> differences(const RegisterTune& t, const Played& p, int offset, int c, int from, int to) {
    std::vector<int> out;
    for (int i = from; i < to; ++i) {
        const int j = i - offset;
        Regs silent{};
        silent[7] = 0x3f;
        const Voice a = voiceOf(t.frames[size_t(i)], c);
        const Voice b = voiceOf(j >= 0 && j < int(p.frames.size()) ? p.frames[size_t(j)] : silent, c);
        const bool same = a.on == b.on && (!a.on || (a.env == b.env && (a.env || a.vol == b.vol) &&
                                                     std::abs(a.tp - b.tp) <= 1));
        if (!same) out.push_back(i);
    }
    return out;
}

const ymseq::Lane& lane(const NonParamState& s, int track) { return s.seq.patterns[0].lane(track); }

}  // namespace

TEST_CASE("register import: the grid follows the note starts", "[register-import]") {
    RegisterTune t = silence(200);
    const int tps[] = {284, 253, 225, 213};
    for (int k = 0; k < 32; ++k) tone(t, 0, 2 + 6 * k, tps[k % 4], kPluck);
    const RegisterGrid g = plugin::guessRegisterGrid(t);
    CHECK(g.framesPerStep == 6);
    CHECK(g.offset == 2);
    CHECK(g.aligned == 100);
    CHECK(g.notes == 32);
    // A step given by the user keeps its length; the first step goes where the notes are.
    const RegisterGrid three = plugin::guessRegisterGrid(t, 3);
    CHECK(three.framesPerStep == 3);
    CHECK(three.offset == 2);
}

TEST_CASE("register import: a step of 8 where some notes come 2 frames late", "[register-import]") {
    // Half of the time between note starts is not a multiple of 8, but 3 notes in 4 start on the step.
    RegisterTune t = silence(512);
    for (int k = 0; k < 64; ++k) tone(t, 0, 8 * k + (k % 4 == 3 ? 2 : 0), k % 2 ? 300 : 400, kPluck);
    const RegisterGrid g = plugin::guessRegisterGrid(t);
    CHECK(g.framesPerStep == 8);
    CHECK(g.offset == 0);
    CHECK(g.aligned == 75);
}

TEST_CASE("register import: a melody plays back frame for frame", "[register-import]") {
    for (const uint32_t rate : {50u, 60u}) {
        CAPTURE(rate);
        RegisterTune t = silence(96, rate);
        const int tps[] = {284, 253, 225, 213, 190, 169, 159, 142};
        for (int k = 0; k < 16; ++k) tone(t, 1, 6 * k, tps[k % 8], kPluck);
        const RegisterGrid g = plugin::guessRegisterGrid(t);
        REQUIRE(g.framesPerStep == 6);
        const Played p = play(t, g);
        CHECK(p.import.notes == 16);
        CHECK(p.import.instruments == 1);  // one table for all of them
        CHECK(p.import.speed == 6);
        CHECK(plugin::kVblRates[size_t(p.import.vblRate)] == rate);
        CHECK(differences(t, p, g.offset, 1, 0, 96) == std::vector<int>{});
        // The other voices stay silent.
        CHECK(differences(t, p, g.offset, 0, 0, 96) == std::vector<int>{});
        CHECK(differences(t, p, g.offset, 2, 0, 96) == std::vector<int>{});
    }
}

TEST_CASE("register import: a note within a step waits with Dxx, one that ends early gets GAT", "[register-import]") {
    RegisterTune t = silence(48);
    tone(t, 0, 0, 284, kPluck);
    tone(t, 0, 8, 253, {15, 13, 11, 9});  // 2 frames into step 1 (steps of 6), 4 frames long
    tone(t, 0, 18, 225, {15, 14, 13});   // step 3, silent after 3 frames
    tone(t, 0, 24, 213, kPluck);
    const RegisterGrid g = plugin::guessRegisterGrid(t, 6);
    REQUIRE(g.offset == 0);
    const Played p = play(t, g);
    const ymseq::Lane& l = lane(p.state, 0);
    CHECK(l.get(ymseq::Row::CmdDelay, 1) != 0);
    CHECK((l.get(ymseq::Row::CmdDelay, 1) & 255) == 2);
    CHECK(l.gat[3] > 0);
    CHECK(l.gat[3] < 50);
    CHECK(differences(t, p, g.offset, 0, 0, 48) == std::vector<int>{});
}

TEST_CASE("register import: notes on the envelope restart it and keep its shape", "[register-import]") {
    RegisterTune t = silence(64);
    for (int k = 0; k < 4; ++k) envelope(t, 2, 16 * k, 12, k % 2 ? 300 : 400, 14, 0x400);
    const RegisterGrid g = plugin::guessRegisterGrid(t);
    CHECK(g.framesPerStep == 16);
    const Played p = play(t, g);
    CHECK(p.import.notes == 4);
    CHECK(differences(t, p, g.offset, 2, 0, 64) == std::vector<int>{});
    // R13 written at each note.
    for (int k = 0; k < 4; ++k) {
        CAPTURE(k);
        CHECK(p.frames[size_t(16 * k)][13] == 14);
    }
}

TEST_CASE("register import: a single decay ends the note where it falls silent", "[register-import]") {
    // Shape 0 decays once (about 6.5 frames at a period of $400), then holds 0: the note ends there, played with the
    // shape 9 that the Shape menu has for it.
    RegisterTune t = silence(64);
    for (int k = 0; k < 4; ++k) envelope(t, 2, 16 * k, 12, 400, 0, 0x400);
    const Played p = play(t, plugin::guessRegisterGrid(t, 16));
    CHECK(p.import.notes == 4);
    for (int k = 0; k < 4; ++k) {
        CAPTURE(k);
        const Regs& r = p.frames[size_t(16 * k)];
        CHECK(r[13] == 9);
        CHECK((r[10] & 16) != 0);
        CHECK((p.frames[size_t(16 * k + 11)][10] & 16) == 0);
    }
}

TEST_CASE("register import: tunes without notes are refused", "[register-import]") {
    NonParamState st;
    const RegisterTune quiet = silence(100);
    const auto none = plugin::importRegisters(quiet, plugin::guessRegisterGrid(quiet), st);
    CHECK_FALSE(none.import);
    CHECK(none.error == "The tune plays no note.");
    // Volume up with tone and noise off: samples through the volume register.
    RegisterTune digi = silence(100);
    for (int i = 0; i < 100; ++i) digi.frames[size_t(i)][8] = uint8_t(i % 16);
    const auto samples = plugin::importRegisters(digi, plugin::guessRegisterGrid(digi), st);
    CHECK_FALSE(samples.import);
    CHECK(samples.error.find("samples") != std::string::npos);
}

TEST_CASE("register import: the alert names what is left out", "[register-import]") {
    RegisterTune t = silence(96, 57);
    for (int k = 0; k < 16; ++k) tone(t, 0, 6 * k, 284, kPluck);
    t.effectFrames = 12;
    NonParamState st;
    const auto r = plugin::importRegisters(t, plugin::guessRegisterGrid(t), st);
    REQUIRE(r.import);
    auto has = [&](const std::string& s) {
        return std::any_of(r.import->remarks.begin(), r.import->remarks.end(),
                           [&](const std::string& x) { return x.find(s) != std::string::npos; });
    };
    CHECK(has("Replay rate 57 Hz played at 60 Hz"));
    CHECK(has("Timer effects"));
    const std::vector<std::string> lines = plugin::registerImportMessage(*r.import, "test.ym", false);
    CHECK_FALSE(lines.empty());
}
