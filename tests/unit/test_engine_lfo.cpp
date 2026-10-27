// STYM2149 — LFO of a track (sixth DAW test, item 59): shapes, rate in Hz or at the tempo, depth on each
// target (volume, noise period, envelope period, SID ratio, pitch, pan), restart at each note or free running (Retrig
// Off), Authentic 3ch, drums.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

#include "ymengine/Engine.h"
#include "ymengine/Pitch.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

constexpr uint32_t kFrame = 960;  // one 50 Hz VBL frame at 48 kHz

struct Rig {
    Engine e;
    EngineConfig cfg;
    GlobalParams g;
    TrackParams t;

    explicit Rig(VoiceMode mode = VoiceMode::Studio16) {
        cfg.render.hostRate = 48000;
        e.prepare(cfg);
        g.mode = mode;
        e.setGlobal(g);
        t.mix.enabled = true;
        t.patch.volume = 10;
        t.patch.lfo.on = true;
        t.patch.lfo.shape = LfoShape::Square;
        t.patch.lfo.hz = 12.5;  // a quarter of a cycle per frame: +1, +1, -1, -1...
    }
    void apply() {
        e.setGlobal(g);
        e.setTrack(0, t);
    }
    void render(uint32_t n) {
        static std::vector<float> l(kFrame), r(kFrame);
        while (n > 0) {
            const uint32_t k = std::min<uint32_t>(n, kFrame);
            e.render(l.data(), r.data(), k);
            n -= k;
        }
    }
    // Note played, then the middle of its first frame.
    void play(int note) {
        e.noteOn(0, note, 100, 0);
        render(kFrame / 2);
    }
    Engine::VoiceInfo v() const { return e.voice(0); }
    // A value of the voice at each of n frames, from the middle of the first one.
    template <class F>
    std::vector<double> frames(int n, F f) {
        std::vector<double> out;
        for (int k = 0; k < n; ++k) {
            out.push_back(double(f(v())));
            render(kFrame);
        }
        return out;
    }
    std::vector<double> volumes(int n) {
        return frames(n, [](const Engine::VoiceInfo& i) { return i.volume; });
    }
};

}  // namespace

TEST_CASE("LFO: the shapes on the 4-bit volume, frame by frame, depth on either side", "[engine][lfo]") {
    struct Case {
        LfoShape shape;
        std::vector<double> want;
    };
    // 12.5 Hz at 50 Hz: a quarter of a cycle per frame, from phase 0 at the attack; depth 4 around 10.
    const Case cases[] = {{LfoShape::Triangle, {10, 14, 10, 6, 10}},
                          {LfoShape::Sine, {10, 14, 10, 6, 10}},
                          {LfoShape::Square, {14, 14, 6, 6, 14}},
                          {LfoShape::SawUp, {6, 8, 10, 12, 6}},
                          {LfoShape::SawDown, {14, 12, 10, 8, 14}}};
    for (const Case& c : cases) {
        Rig r;
        r.t.patch.lfo.shape = c.shape;
        r.t.patch.lfo.hz = 12.5;
        r.apply();
        r.play(60);
        CHECK(r.volumes(5) == c.want);
    }

    // Clamped to 0-15; Off, or a depth of 0: the volume of the track.
    Rig r;
    r.t.patch.volume = 13;
    r.apply();
    r.play(60);
    CHECK(r.volumes(3) == std::vector<double>{15, 15, 9});
    r.e.allNotesOff(0);
    r.render(4 * kFrame);
    r.t.patch.lfo.depth = 0;
    r.apply();
    r.play(60);
    CHECK(r.volumes(3) == std::vector<double>{13, 13, 13});
    r.e.allNotesOff(0);
    r.render(4 * kFrame);
    r.t.patch.lfo.depth = 4;
    r.t.patch.lfo.on = false;
    r.apply();
    r.play(60);
    CHECK(r.volumes(3) == std::vector<double>{13, 13, 13});
}

TEST_CASE("LFO: noise period, envelope period, SID ratio, pitch and pan", "[engine][lfo]") {
    SECTION("Noise period: steps on either side, clamped to 1-31") {
        Rig r;
        r.t.patch.lfo.target = LfoTarget::NoisePeriod;
        r.t.patch.lfo.depth = 8;
        r.t.patch.noisePeriod = 10;
        r.apply();
        r.play(60);
        CHECK(r.frames(3, [](const auto& i) { return i.noisePeriod; }) == std::vector<double>{18, 18, 2});
        r.e.allNotesOff(0);
        r.render(4 * kFrame);
        r.t.patch.noisePeriod = 28;
        r.apply();
        r.play(60);
        CHECK(r.frames(3, [](const auto& i) { return i.noisePeriod; }) == std::vector<double>{31, 31, 20});
    }
    SECTION("Envelope period: cents, a higher envelope for a positive value") {
        Rig r;
        r.t.patch.lfo.target = LfoTarget::EnvPeriod;
        r.t.patch.lfo.depth = 1200;
        r.t.patch.envPeriod = 1000;
        r.apply();
        r.play(60);
        CHECK(r.frames(3, [](const auto& i) { return i.envPeriod; }) == std::vector<double>{500, 500, 2000});
    }
    SECTION("SID ratio: the timer an octave up, then down") {
        Rig r;
        r.t.patch.generator = Generator::Sid;
        r.t.patch.lfo.on = false;
        r.apply();
        r.play(57);
        const double hz = r.v().timerHz;
        REQUIRE(hz > 0);
        r.e.allNotesOff(0);
        r.render(4 * kFrame);
        r.t.patch.lfo.on = true;
        r.t.patch.lfo.target = LfoTarget::SidRatio;
        r.t.patch.lfo.depth = 1200;
        r.apply();
        r.play(57);
        const auto t = r.frames(3, [](const auto& i) { return i.timerHz; });
        CHECK(std::abs(t[0] / hz - 2) < 1e-9);
        CHECK(std::abs(t[1] / hz - 2) < 1e-9);
        CHECK(std::abs(t[2] / hz - 0.5) < 1e-9);
        CHECK(r.v().tonePeriod == r.v().notePeriod);  // the square keeps its pitch
    }
    SECTION("Pitch: cents, with the vibrato (its row of the pitch matrix)") {
        Rig r;
        r.t.patch.lfo.target = LfoTarget::Pitch;
        r.t.patch.lfo.depth = 1200;
        r.apply();
        r.play(57);
        const uint16_t tp = tonePeriodFor(noteHz(57), r.cfg.render.chip.clock);
        CHECK(r.v().notePeriod == tp);
        CHECK(r.frames(3, [](const auto& i) { return i.tonePeriod; }) ==
              std::vector<double>{std::floor(tp / 2.0 + 0.5), std::floor(tp / 2.0 + 0.5), double(tp * 2)});
        // Not routed to the square in the matrix: no pitch.
        r.e.allNotesOff(0);
        r.render(4 * kFrame);
        r.t.patch.pitchMatrix &= uint16_t(~pitchBit(PitchSource::Vibrato, PitchTarget::Square));
        r.apply();
        r.play(57);
        CHECK(r.frames(2, [](const auto& i) { return i.tonePeriod; }) == std::vector<double>{double(tp), double(tp)});
    }
    SECTION("Pan: percent of the panning, 16 channels") {
        Rig r;
        r.t.patch.lfo.target = LfoTarget::Pan;
        r.t.patch.lfo.depth = 50;
        r.apply();
        r.play(60);
        CHECK(r.frames(3, [](const auto& i) { return i.lfoPan; }) == std::vector<double>{0.5, 0.5, -0.5});
        CHECK(r.v().volume == 10);  // nothing else moves
    }
}

TEST_CASE("LFO: synced to the tempo, one cycle per division", "[engine][lfo]") {
    // 120 BPM: 1/4 is 2 Hz, 1/8 is 4 Hz (Saw up: a value per frame).
    auto run = [](bool sync, double hz, int division, double bpm = 120) {
        Rig r;
        r.g.tempo = bpm;
        r.t.patch.lfo.shape = LfoShape::SawUp;
        r.t.patch.lfo.depth = 15;
        r.t.patch.volume = 8;
        r.t.patch.lfo.sync = sync;
        r.t.patch.lfo.hz = hz;
        r.t.patch.lfo.division = uint8_t(division);
        r.apply();
        r.play(60);
        return r.volumes(30);
    };
    CHECK(run(true, 20, 4) == run(false, 2, 4));
    CHECK(run(true, 20, 5) == run(false, 4, 0));
    CHECK(run(true, 20, 5) != run(true, 20, 4));
    // 1/32T and 1/64 after 1/32 (ninth DAW test, item 119): at 60 BPM, 12 Hz and 16 Hz.
    CHECK(run(true, 20, 10, 60) == run(false, 12, 0, 60));
    CHECK(run(true, 20, 11, 60) == run(false, 16, 0, 60));
    CHECK(run(true, 20, 9, 60) == run(false, 8, 0, 60));
}

TEST_CASE("LFO: restarted at each note, Random held over a cycle, no LFO on the drums, Authentic 3ch",
          "[engine][lfo]") {
    SECTION("Restart") {
        Rig r;
        r.apply();
        r.play(60);
        CHECK(r.volumes(3) == std::vector<double>{14, 14, 6});
        r.e.noteOff(0, 60, 0);
        r.render(2 * kFrame);
        r.play(62);
        CHECK(r.volumes(3) == std::vector<double>{14, 14, 6});
    }
    SECTION("Random: one value per cycle, within the depth") {
        Rig r;
        r.t.patch.lfo.shape = LfoShape::Random;
        r.t.patch.lfo.hz = 12.5;  // four frames per cycle
        r.apply();
        r.play(60);
        const auto vol = r.volumes(24);
        bool moved = false;
        for (size_t k = 0; k < vol.size(); ++k) {
            CHECK(vol[k] >= 6);
            CHECK(vol[k] <= 14);
            if (k % 4) CHECK(vol[k] == vol[k - 1]);
            else if (k) moved |= vol[k] != vol[k - 1];
        }
        CHECK(moved);
    }
    SECTION("Drums: none") {
        Rig r;
        r.t.patch.generator = Generator::Chipdrum;
        r.apply();
        r.e.noteOn(0, 36, 100, 0);
        r.render(kFrame / 2);
        const double first = r.v().volume;
        r.render(kFrame);
        CHECK(r.v().volume == first);  // the LFO leaves the drums alone
    }
    SECTION("Authentic 3ch: the 4-bit volume of the channel, register only") {
        Rig r(VoiceMode::Authentic3);
        r.g.velocity = VelocityMode::Off;  // neither the velocity nor the level lower the volume of the channel
        r.t.mix.level = 1;
        r.apply();
        r.play(60);
        CHECK(r.volumes(3) == std::vector<double>{14, 14, 6});
    }
}

TEST_CASE("LFO: Retrig Off, free running: a note takes the phase of the LFO of its track", "[engine][lfo]") {
    SECTION("Rate in Hz") {
        Rig r;
        r.t.patch.lfo.retrig = false;
        r.apply();
        r.play(60);  // the free LFO starts with the time base: phase 0 at this first note
        CHECK(r.volumes(3) == std::vector<double>{14, 14, 6});
        r.e.noteOff(0, 60, 0);
        r.render(2 * kFrame);
        r.play(62);  // 5.5 frames after the start: phase 0.375, then a quarter of a cycle per frame
        CHECK(r.volumes(4) == std::vector<double>{14, 6, 6, 14});
    }
    SECTION("Synced to the tempo while the host plays in Host sync: on the host's beat grid") {
        for (const bool retrig : {false, true})
            for (const double ppq : {0.0, 0.25, 0.5, 0.75, 7.6}) {
                CAPTURE(retrig, ppq);
                Rig r;
                r.g.seqClock = SeqClock::Host;
                r.t.patch.lfo.retrig = retrig;
                r.t.patch.lfo.sync = true;
                r.t.patch.lfo.division = 4;  // 1/4: one cycle per quarter note
                r.apply();
                Engine::HostTransport h;
                h.playing = true;
                h.bpm = 120;
                h.ppq = ppq;
                r.e.setHostTransport(h);
                r.play(60);
                CHECK(r.v().volume == (retrig || std::fmod(ppq, 1.0) < 0.5 ? 14 : 6));
            }
    }
}

TEST_CASE("LFO: it goes on from one note of the keyboard arpeggio to the next", "[engine][lfo][arp]") {
    // Like the table and the vibrato: restarted at each note of a 37 Hz arpeggio, it would stay near its
    // start (+1); carried, it plays its cycle, two frames up and two down.
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3}) {
        CAPTURE(int(mode));
        Rig r(mode);
        r.g.velocity = VelocityMode::Off;
        r.t.mix.level = 1;
        r.t.arp.on = true;
        r.t.arp.mode = ArpMode::Table;  // 0, 4, 7
        r.t.arp.rateHz = 37;
        r.apply();
        r.e.noteOn(0, 60, 100, 0);
        auto arpVoice = [&r] {
            for (int i = 0; i < Engine::kVoices; ++i)
                if (const Engine::VoiceInfo v = r.e.voice(i); v.on && !v.fading && v.tag == VoiceTag::Arp) return i;
            return -1;
        };
        int vi = -1;
        for (int s = 0; s < 4 * int(kFrame) && vi < 0; ++s) {
            r.render(1);
            vi = arpVoice();
        }
        REQUIRE(vi >= 0);
        r.render(kFrame / 2);
        std::vector<int> vol, notes;
        for (int k = 0; k < 12; ++k) {
            vi = arpVoice();
            REQUIRE(vi >= 0);
            vol.push_back(r.e.voice(vi).volume);
            notes.push_back(r.e.voice(vi).note);
            r.render(kFrame);
        }
        std::sort(notes.begin(), notes.end());
        CHECK(std::unique(notes.begin(), notes.end()) - notes.begin() > 1);  // the arpeggio plays
        CHECK(std::count(vol.begin(), vol.end(), 6) == 6);
        for (size_t k = 0; k + 4 < vol.size(); ++k) CHECK(vol[k] == vol[k + 4]);
    }
}
