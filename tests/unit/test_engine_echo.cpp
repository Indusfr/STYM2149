// STYM2149 — echo of the notes (eighth DAW test, item 98, D-299): repeats at each division of the tempo, quieter each
// time, in every mode; in 3 channels a repeat takes the channel of its target track only when it plays no note of its
// own, none for a Sync Buzzer (D-316); the register recorder keeps the repeats; 16 channels: voices of the pool; panic.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using ymengine::Engine;
using ymengine::VoiceMode;
using ymengine::VoiceTag;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kEighth = 12000;  // 1/8 at 120 BPM, in samples

ymengine::TrackParams echoTrack(bool on, int repeats = 3, int drop = 3, int target = -1, int division = 5) {
    ymengine::TrackParams t;
    t.mix.enabled = true;
    t.patch.releaseMs = 0;
    t.patch.echo.on = on;
    t.patch.echo.repeats = uint8_t(repeats);
    t.patch.echo.drop = uint8_t(drop);
    t.patch.echo.target = int8_t(target);
    t.patch.echo.division = uint8_t(division);
    return t;
}

struct Rig {
    Engine e;
    std::vector<float> l, r;
    // Events to send at a sample (since the start): fn(engine, offset in its block).
    std::vector<std::pair<uint32_t, std::function<void(Engine&, uint32_t)>>> pending;

    explicit Rig(VoiceMode mode) {
        ymengine::EngineConfig cfg;
        cfg.render.hostRate = kRate;
        e.prepare(cfg);
        ymengine::GlobalParams g = e.global();
        g.mode = mode;
        g.softClip = false;
        g.tempo = 120;
        g.seqClock = ymengine::SeqClock::Internal;
        e.setGlobal(g);
    }
    void noteOn(uint32_t at, int note, int track) {
        pending.emplace_back(at, [=](Engine& x, uint32_t o) { x.noteOn(o, note, 127, track); });
    }
    void noteOff(uint32_t at, int note, int track) {
        pending.emplace_back(at, [=](Engine& x, uint32_t o) { x.noteOff(o, note, track); });
    }
    // Renders up to sample `to` (since the start), in blocks of 512 at most, the events in the block where they fall.
    void runTo(uint32_t to) {
        while (l.size() < to) {
            const uint32_t len = std::min<uint32_t>(512, to - uint32_t(l.size()));
            const auto at = uint32_t(l.size());
            for (const auto& [when, fn] : pending)
                if (when >= at && when < at + len) fn(e, when - at);
            l.resize(at + len);
            r.resize(at + len);
            e.render(l.data() + at, r.data() + at, len);
        }
    }
    // Repeats sounding (attacked, not released), on `track` (-1: any).
    int repeats(int track = -1) const {
        int n = 0;
        for (int i = 0; i < Engine::kVoices; ++i) {
            const auto v = e.voice(i);
            if (v.on && v.tag == VoiceTag::Echo && !v.fading && v.adsrStage != 4 && (track < 0 || v.track == track))
                ++n;
        }
        return n;
    }
};

double rms(const std::vector<float>& x, size_t from, size_t to) {
    // One-pole high-pass at 100 Hz first: the slow movements of the output chain stay out of the comparison.
    const double a = std::exp(-2 * 3.141592653589793 * 100 / kRate);
    double px = 0, py = 0, s = 0;
    for (size_t i = 0; i < to; ++i) {
        py = a * (py + x[i] - px);
        px = x[i];
        if (i >= from) s += py * py;
    }
    return std::sqrt(s / double(to - from));
}

}  // namespace

TEST_CASE("Echo: repeats at each division of the tempo, quieter each time, in every mode", "[engine][echo]") {
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3, VoiceMode::Dual3}) {
        CAPTURE(int(mode));
        Rig g(mode);
        g.e.setTrack(0, echoTrack(true));
        g.noteOn(0, 57, 0);
        g.noteOff(2400, 57, 0);
        g.runTo(kEighth - 10);
        CHECK(g.repeats() == 0);
        for (int k = 1; k <= 3; ++k) {
            CAPTURE(k);
            g.runTo(k * kEighth + 10);
            REQUIRE(g.repeats(0) == 1);
            for (int i = 0; i < Engine::kVoices; ++i)
                if (g.e.voice(i).on && g.e.voice(i).tag == VoiceTag::Echo) CHECK(g.e.voice(i).note == 57);
            g.runTo(k * kEighth + 2400 + 10);  // released as far from its attack as the note
            CHECK(g.repeats() == 0);
        }
        g.runTo(4 * kEighth + 10);
        CHECK(g.repeats() == 0);  // three repeats
        // Each repeat lower than the one before (three steps of the 4-bit volume).
        const double note = rms(g.l, 200, 2200);
        double prev = note;
        for (int k = 1; k <= 3; ++k) {
            const double rep = rms(g.l, k * kEighth + 200, k * kEighth + 2200);
            CAPTURE(k, note, rep);
            CHECK(rep < prev * 0.9);
            CHECK(rep > note * 0.02);
            prev = rep;
        }
    }
}

TEST_CASE("Echo: without drop the repeats keep the level, a repeat 15 steps down is not played", "[engine][echo]") {
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3}) {
        CAPTURE(int(mode));
        {
            Rig g(mode);
            g.e.setTrack(0, echoTrack(true, 2, 0));
            g.noteOn(0, 57, 0);
            g.noteOff(2400, 57, 0);
            g.runTo(3 * kEighth);
            const double note = rms(g.l, 200, 2200), rep = rms(g.l, 2 * kEighth + 200, 2 * kEighth + 2200);
            CAPTURE(note, rep);
            CHECK(std::fabs(rep / note - 1) < 0.05);
        }
        Rig g(mode);
        g.e.setTrack(0, echoTrack(true, 8, 5));  // 5, 10, then 15 steps down: two repeats
        g.noteOn(0, 57, 0);
        g.noteOff(2400, 57, 0);
        g.runTo(2 * kEighth + 10);
        CHECK(g.repeats() == 1);
        g.runTo(3 * kEighth + 10);
        CHECK(g.repeats() == 0);
    }
}

TEST_CASE("Echo: in 3 channels a repeat only takes a channel without a note of its own, a target track takes them",
          "[engine][echo][3ch]") {
    SECTION("same track: the held note keeps its channel, the repeats after its release take it") {
        Rig g(VoiceMode::Authentic3);
        g.e.setTrack(0, echoTrack(true));
        g.noteOn(0, 57, 0);
        g.noteOff(20000, 57, 0);
        g.runTo(kEighth + 10);
        CHECK(g.e.voice(0).tag == VoiceTag::Manual);  // repeat 1 skipped: the note still sounds
        CHECK(g.repeats() == 0);
        g.runTo(2 * kEighth + 10);
        CHECK(g.e.voice(0).on);
        CHECK(g.e.voice(0).tag == VoiceTag::Echo);
        CHECK(g.e.voice(0).track == 0);
        // Repeat 3 takes the channel from repeat 2, and is released as far from its attack as the note.
        g.runTo(20000 + 3 * kEighth - 10);
        CHECK(g.repeats() == 1);
        g.runTo(20000 + 3 * kEighth + 10);
        CHECK(g.repeats() == 0);
    }
    SECTION("target track B: the repeats play on its channel while the note holds, a new note takes it back") {
        Rig g(VoiceMode::Authentic3);
        g.e.setTrack(0, echoTrack(true, 3, 3, 1));
        g.e.setTrack(1, echoTrack(false));
        g.e.setTrack(2, echoTrack(false));
        g.noteOn(0, 57, 0);
        g.runTo(kEighth + 10);
        CHECK(g.e.voice(0).tag == VoiceTag::Manual);
        CHECK(g.e.voice(1).on);
        CHECK(g.e.voice(1).tag == VoiceTag::Echo);
        CHECK(g.e.voice(1).track == 1);
        CHECK(g.e.voice(1).note == 57);
        // C takes a note; the next one has no free channel: it takes B from the repeat rather than A from the note.
        g.noteOn(kEighth + 100, 64, 2);
        g.noteOn(kEighth + 200, 60, 1);
        g.noteOff(kEighth + 1000, 57, 0);
        g.runTo(kEighth + 600);
        CHECK(g.e.voice(0).tag == VoiceTag::Manual);
        CHECK(g.e.voice(0).note == 57);
        CHECK(g.e.voice(1).tag == VoiceTag::Manual);
        CHECK(g.e.voice(1).note == 60);
        // The release of repeat 1 leaves the note of B alone.
        g.runTo(kEighth + 1000 + kEighth + 10);
        CHECK(g.e.voice(1).on);
        CHECK(g.e.voice(1).tag == VoiceTag::Manual);
        CHECK_FALSE(g.e.voice(1).fading);
    }
}

TEST_CASE("Echo: the register recorder keeps the repeats", "[engine][echo][3ch][recorder]") {
    Rig g(VoiceMode::Authentic3);
    g.e.setTrack(0, echoTrack(true, 3, 3, 1));
    g.e.setTrack(1, echoTrack(false));
    std::vector<ymengine::RegisterFrame> frames(256);
    g.e.startCapture(0, frames);
    g.noteOn(0, 57, 0);
    g.noteOff(2400, 57, 0);
    g.runTo(3 * kEighth);
    REQUIRE(g.e.capturedFrames() >= 35);
    // 50 Hz: 960 samples per frame. Note on channel A in frame 1, repeat 1 on channel B in frames 13 and 14 (sample
    // 12000), silence of B in between.
    const int noteVol = frames[1].reg[8] & 15;
    CHECK(noteVol > 0);
    for (int f = 4; f <= 12; ++f) CHECK((frames[size_t(f)].reg[9] & 15) == 0);
    const int repVol = frames[13].reg[9] & 15;
    CAPTURE(noteVol, repVol);
    CHECK(repVol > 0);
    CHECK(repVol < noteVol);
    CHECK(frames[13].reg[2] == frames[1].reg[0]);  // same pitch on B as the note on A
    CHECK(frames[13].reg[3] == frames[1].reg[1]);
    CHECK(g.e.stopCapture() > 0);
}

TEST_CASE("Echo: 16 channels: repeats in the voices of the pool, the oldest gives way; the panic clears them",
          "[engine][echo]") {
    Rig g(VoiceMode::Studio16);
    g.e.setTrack(0, echoTrack(true, 8, 1, -1, 10));  // 1/32: 3000 samples
    for (int n = 0; n < 20; ++n) g.noteOn(0, 48 + n, 0);
    g.runTo(3000 + 10);
    // 20 notes: 16 track voices and 4 of the pool; the 12 voices of the pool left take the repeats.
    int echoes = 0;
    for (int i = 0; i < Engine::kVoices; ++i)
        if (g.e.voice(i).on && g.e.voice(i).tag == VoiceTag::Echo) {
            ++echoes;
            CHECK(i >= Engine::kLaneVoices + Engine::kChordVoices);
        }
    CHECK(echoes == 12);
    g.e.allNotesOff(0);
    g.runTo(3000 + 3 * 3000 + 10);
    for (int i = 0; i < Engine::kVoices; ++i) CHECK_FALSE((g.e.voice(i).on && g.e.voice(i).tag == VoiceTag::Echo));
}

TEST_CASE("Echo: off, the settings of the echo change nothing", "[engine][echo]") {
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3, VoiceMode::Dual3}) {
        CAPTURE(int(mode));
        auto play = [mode](const ymengine::TrackParams& t) {
            Rig g(mode);
            g.e.setTrack(0, t);
            g.noteOn(0, 57, 0);
            g.noteOff(2400, 57, 0);
            g.runTo(2 * kEighth);
            return g.l;
        };
        CHECK(play(echoTrack(false)) == play(echoTrack(false, 8, 0, 2, 0)));
    }
}

TEST_CASE("Echo: none for a Sync Buzzer in 3 channels, a Sync Buzzer in 16 channels has it (ninth DAW test, item 115)",
          "[engine][echo][3ch]") {
    for (const VoiceMode mode : {VoiceMode::Authentic3, VoiceMode::Dual3, VoiceMode::Studio16}) {
        CAPTURE(int(mode));
        Rig g(mode);
        ymengine::TrackParams t = echoTrack(true);
        t.patch.generator = ymengine::Generator::SyncBuzzer;
        g.e.setTrack(0, t);
        g.noteOn(0, 57, 0);
        g.noteOff(2400, 57, 0);
        g.runTo(kEighth + 10);
        CHECK(g.repeats() == (mode == VoiceMode::Studio16 ? 1 : 0));
        // The note itself plays.
        CHECK(rms(g.l, 200, 2200) > 0.001);
    }
    // Another sound of the same track, after it: its echo in 3 channels.
    Rig g(VoiceMode::Authentic3);
    g.e.setTrack(0, echoTrack(true));
    g.noteOn(0, 57, 0);
    g.noteOff(2400, 57, 0);
    g.runTo(kEighth + 10);
    CHECK(g.repeats() == 1);
}
