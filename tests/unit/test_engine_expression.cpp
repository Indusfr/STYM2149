// STYM2149 — voice engine: MIDI expression (D-432). Targets of the bend, the wheel and the aftertouch (channel and
// polyphonic pressure), MPE notes with their own bend, pressure and slide (CC74).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <tuple>
#include <utility>

#include "ymengine/Engine.h"
#include "ymengine/Pitch.h"

using namespace stym;
using ymengine::BendTarget;
using ymengine::Engine;
using ymengine::ExpressTarget;
using ymengine::SlideTarget;

namespace {

// 16-channel engine at 48 kHz; track 0 plays a tone at volume 8, envelope period 12800, no vibrato of its own.
struct Rig {
    Engine e;
    float l[480], r[480];
    explicit Rig(const std::function<void(ymengine::GlobalParams&)>& set = {},
                 const std::function<void(ymengine::TrackParams&)>& patch = {}) {
        ymengine::EngineConfig c;
        c.render.hostRate = 48000;
        e.prepare(c);
        ymengine::GlobalParams g = e.global();
        g.mode = ymengine::VoiceMode::Studio16;
        if (set) set(g);
        e.setGlobal(g);
        ymengine::TrackParams t;
        t.mix.enabled = true;
        t.mix.level = 1;
        t.patch.volume = 8;
        t.patch.vibratoHz = 6;
        if (patch) patch(t);
        e.setTrack(0, t);
    }
    // Renders 10 ms blocks; the voices take their VBL frames (20 ms at 50 Hz).
    void run(int blocks = 4) {
        for (int k = 0; k < blocks; ++k) e.render(l, r, 480);
    }
    // Lowest and highest tone period of voice i over one second.
    std::pair<int, int> swing(int i = 0) {
        int lo = 4096, hi = 0;
        for (int k = 0; k < 100; ++k) {
            e.render(l, r, 480);
            lo = std::min<int>(lo, e.voice(i).tonePeriod);
            hi = std::max<int>(hi, e.voice(i).tonePeriod);
        }
        return {lo, hi};
    }
    // First voice that plays the note (-1: none).
    int voiceOf(int note) const {
        for (int i = 0; i < Engine::kVoices; ++i)
            if (e.voice(i).on && e.voice(i).note == note) return i;
        return -1;
    }
};

const int kA4Period = int(ymengine::tonePeriodFor(ymengine::noteHz(69), ymcore::clocks::kDefault));

}  // namespace

TEST_CASE("Aftertouch: vibrato up to the Wheel depth, exact return without pressure", "[engine][expression]") {
    Rig k([](ymengine::GlobalParams& g) { g.expressCents = 100; });
    k.e.noteOn(0, 69, 127);
    k.run(1);
    REQUIRE(k.e.voice(0).tonePeriod == kA4Period);
    k.e.channelPressure(0, 1.0);
    const auto [lo, hi] = k.swing();
    CHECK(lo <= 269);  // ±100 cents around 284
    CHECK(hi >= 300);
    k.e.channelPressure(0, 0);
    k.run(2);
    CHECK(k.e.voice(0).tonePeriod == kA4Period);
}

TEST_CASE("Aftertouch: Volume raises the 4-bit volume towards 15, Off changes nothing", "[engine][expression]") {
    Rig k([](ymengine::GlobalParams& g) { g.aftertouch = ExpressTarget::Volume; });
    k.e.noteOn(0, 69, 127);
    k.run();
    REQUIRE(k.e.voice(0).volume == 8);
    k.e.channelPressure(0, 0.5);
    k.run();
    CHECK(k.e.voice(0).volume == 12);  // 8 + 7 × 0.5, rounded
    k.e.channelPressure(0, 1);
    k.run();
    CHECK(k.e.voice(0).volume == 15);
    k.e.channelPressure(0, 0);
    k.run();
    CHECK(k.e.voice(0).volume == 8);

    Rig off([](ymengine::GlobalParams& g) { g.aftertouch = ExpressTarget::Off; });
    off.e.noteOn(0, 69, 127);
    off.e.channelPressure(0, 1);
    const auto [lo, hi] = off.swing();
    CHECK(lo == kA4Period);
    CHECK(hi == kA4Period);
    CHECK(off.e.voice(0).volume == 8);
}

TEST_CASE("Polyphonic aftertouch: only the pressed note moves", "[engine][expression]") {
    Rig k([](ymengine::GlobalParams& g) { g.aftertouch = ExpressTarget::Volume; });
    k.e.noteOn(0, 60, 127);
    k.e.noteOn(0, 64, 127);
    k.run();
    const int a = k.voiceOf(60), b = k.voiceOf(64);
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    k.e.notePressure(0, 64, 1);
    k.run();
    CHECK(k.e.voice(a).volume == 8);
    CHECK(k.e.voice(b).volume == 15);
    // The channel pressure adds to it: a note takes the higher of the two.
    k.e.channelPressure(0, 0.5);
    k.run();
    CHECK(k.e.voice(a).volume == 12);
    CHECK(k.e.voice(b).volume == 15);
}

TEST_CASE("Wheel targets: Off, Volume, Envelope and LFO depth", "[engine][expression]") {
    SECTION("Off: the wheel changes nothing") {
        Rig k([](ymengine::GlobalParams& g) { g.wheelTarget = ExpressTarget::Off; });
        k.e.noteOn(0, 69, 127);
        k.e.modWheel(0, 1);
        const auto [lo, hi] = k.swing();
        CHECK(lo == kA4Period);
        CHECK(hi == kA4Period);
    }
    SECTION("Volume") {
        Rig k([](ymengine::GlobalParams& g) { g.wheelTarget = ExpressTarget::Volume; });
        k.e.noteOn(0, 69, 127);
        k.e.modWheel(0, 1);
        k.run();
        CHECK(k.e.voice(0).volume == 15);
        CHECK(k.e.voice(0).tonePeriod == kA4Period);
    }
    SECTION("Envelope: up to an octave higher, half the period") {
        Rig k([](ymengine::GlobalParams& g) { g.wheelTarget = ExpressTarget::Envelope; },
              [](ymengine::TrackParams& t) { t.patch.envelopeVolume = true; });
        k.e.noteOn(0, 69, 127);
        k.run();
        REQUIRE(k.e.voice(0).envPeriod == 12800);
        k.e.modWheel(0, 1);
        k.run();
        CHECK(k.e.voice(0).envPeriod == 6400);
        k.e.modWheel(0, 0);
        k.run();
        CHECK(k.e.voice(0).envPeriod == 12800);
    }
    SECTION("LFO: none at rest, the depth of the track at the top") {
        Rig k([](ymengine::GlobalParams& g) { g.wheelTarget = ExpressTarget::Lfo; },
              [](ymengine::TrackParams& t) {
                  t.patch.lfo.on = true;
                  t.patch.lfo.target = ymengine::LfoTarget::Pitch;
                  t.patch.lfo.depth = 100;  // cents
              });
        k.e.noteOn(0, 69, 127);
        auto [lo, hi] = k.swing();
        CHECK(lo == kA4Period);
        CHECK(hi == kA4Period);
        k.e.modWheel(0, 1);
        std::tie(lo, hi) = k.swing();
        CHECK(lo <= 269);
        CHECK(hi >= 300);
    }
}

TEST_CASE("Bend targets: Volume and Envelope leave the pitch, a new target lets go of it", "[engine][expression]") {
    SECTION("Volume: the centre leaves it, the whole Bend range up and down") {
        Rig k([](ymengine::GlobalParams& g) {
            g.bendTarget = BendTarget::Volume;
            g.bendRange = 2;
        });
        k.e.noteOn(0, 69, 127);
        k.run();
        k.e.pitchBend(0, 2);
        k.run();
        CHECK(k.e.voice(0).volume == 15);
        CHECK(k.e.voice(0).tonePeriod == kA4Period);
        k.e.pitchBend(0, -1);
        k.run();
        CHECK(k.e.voice(0).volume == 4);
        k.e.pitchBend(0, -2);
        k.run();
        CHECK(k.e.voice(0).volume == 0);
        k.e.pitchBend(0, 0);
        k.run();
        CHECK(k.e.voice(0).volume == 8);
    }
    SECTION("Envelope: the semitones of the bend on the envelope frequency, the note unchanged") {
        Rig k([](ymengine::GlobalParams& g) { g.bendTarget = BendTarget::Envelope; },
              [](ymengine::TrackParams& t) { t.patch.envelopeVolume = true; });
        k.e.noteOn(0, 69, 127);
        k.e.pitchBend(0, 12);
        k.run();
        CHECK(k.e.voice(0).envPeriod == 6400);
        CHECK(k.e.voice(0).tonePeriod == kA4Period);
        k.e.pitchBend(0, -12);
        k.run();
        CHECK(k.e.voice(0).envPeriod == 25600);
        // A note attacked while bent takes the bend on its envelope only.
        k.e.noteOn(0, 81, 127);
        k.run();
        const int v = k.voiceOf(81);
        REQUIRE(v >= 0);
        CHECK(k.e.voice(v).tonePeriod == int(ymengine::tonePeriodFor(ymengine::noteHz(81), ymcore::clocks::kDefault)));
    }
    SECTION("Changing the target while bent") {
        Rig k;
        k.e.noteOn(0, 69, 127);
        k.e.pitchBend(0, 2);
        k.run();
        const int bent = int(ymengine::tonePeriodFor(ymengine::noteHz(71), ymcore::clocks::kDefault));
        REQUIRE(k.e.voice(0).tonePeriod == bent);
        ymengine::GlobalParams g = k.e.global();
        g.bendTarget = BendTarget::Volume;
        k.e.setGlobal(g);
        k.run();
        CHECK(k.e.voice(0).tonePeriod == kA4Period);
        CHECK(k.e.voice(0).volume == 15);
        g.bendTarget = BendTarget::Pitch;
        k.e.setGlobal(g);
        k.run();
        CHECK(k.e.voice(0).tonePeriod == bent);
        CHECK(k.e.voice(0).volume == 8);
    }
}

TEST_CASE("MPE: each member channel bends, presses and slides its own note", "[engine][expression][mpe]") {
    Rig k([](ymengine::GlobalParams& g) { g.aftertouch = ExpressTarget::Volume; });
    // The same note on two member channels: two voices.
    k.e.noteOn(0, 69, 127, -1, 2);
    k.e.noteOn(0, 69, 127, -1, 3);
    k.run();
    int a = -1, b = -1;
    for (int i = 0; i < Engine::kVoices; ++i)
        if (k.e.voice(i).on) (a < 0 ? a : b) = i;
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    // Channel 2 attacked first: the oldest voice.
    if (k.e.voice(a).age > k.e.voice(b).age) std::swap(a, b);
    k.e.mpeBend(0, 2, 2);
    k.run();
    CHECK(k.e.voice(a).tonePeriod == int(ymengine::tonePeriodFor(ymengine::noteHz(71), ymcore::clocks::kDefault)));
    CHECK(k.e.voice(b).tonePeriod == kA4Period);
    k.e.mpePressure(0, 3, 1);
    k.run();
    CHECK(k.e.voice(a).volume == 8);
    CHECK(k.e.voice(b).volume == 15);
    // The note-off of a member releases its note only.
    k.e.noteOff(0, 69, -1, 3);
    k.run();
    CHECK(k.e.voice(a).on);
    CHECK_FALSE(k.e.voice(b).on);
    // A new note of a channel starts with the bend of that channel.
    k.e.noteOff(0, 69, -1, 2);
    k.e.noteOn(0, 64, 127, -1, 2);
    k.run();
    const int c = k.voiceOf(64);
    REQUIRE(c >= 0);
    CHECK(k.e.voice(c).tonePeriod == int(ymengine::tonePeriodFor(ymengine::noteHz(66), ymcore::clocks::kDefault)));
}

TEST_CASE("MPE slide (CC74): Volume, Envelope or Vibrato", "[engine][expression][mpe]") {
    SECTION("Volume: 0.5 leaves it, up towards 15, down towards 0") {
        Rig k;
        k.e.noteOn(0, 69, 127, -1, 1);
        k.e.mpeSlide(0, 1, 0.5);
        k.run();
        CHECK(k.e.voice(0).volume == 8);
        k.e.mpeSlide(0, 1, 1);
        k.run();
        CHECK(k.e.voice(0).volume == 15);
        k.e.mpeSlide(0, 1, 0.25);
        k.run();
        CHECK(k.e.voice(0).volume == 4);
        k.e.mpeSlide(0, 1, 0);
        k.run();
        CHECK(k.e.voice(0).volume == 0);
    }
    SECTION("Envelope: an octave either way") {
        Rig k([](ymengine::GlobalParams& g) { g.mpeSlide = SlideTarget::Envelope; },
              [](ymengine::TrackParams& t) { t.patch.envelopeVolume = true; });
        k.e.noteOn(0, 69, 127, -1, 1);
        k.e.mpeSlide(0, 1, 1);
        k.run();
        CHECK(k.e.voice(0).envPeriod == 6400);
        k.e.mpeSlide(0, 1, 0);
        k.run();
        CHECK(k.e.voice(0).envPeriod == 25600);
    }
    SECTION("Vibrato: from the middle up, the Wheel depth at the top") {
        Rig k([](ymengine::GlobalParams& g) {
            g.mpeSlide = SlideTarget::Vibrato;
            g.expressCents = 100;
        });
        k.e.noteOn(0, 69, 127, -1, 1);
        k.e.mpeSlide(0, 1, 0.2);
        auto [lo, hi] = k.swing();
        CHECK(lo == kA4Period);
        CHECK(hi == kA4Period);
        k.e.mpeSlide(0, 1, 1);
        std::tie(lo, hi) = k.swing();
        CHECK(lo <= 269);
        CHECK(hi >= 300);
    }
    SECTION("A note without MPE channel ignores the slides") {
        Rig k;
        k.e.mpeSlide(0, 1, 1);
        k.e.noteOn(0, 69, 127);
        k.run();
        CHECK(k.e.voice(0).volume == 8);
    }
}

TEST_CASE("Expression: drums have none", "[engine][expression]") {
    Rig k([](ymengine::GlobalParams& g) {
        g.aftertouch = ExpressTarget::Volume;
        g.wheelTarget = ExpressTarget::Volume;
    },
          [](ymengine::TrackParams& t) { t.patch.generator = ymengine::Generator::Chipdrum; });
    k.e.noteOn(0, 36, 127);
    k.e.render(k.l, k.r, 480);
    const int v = k.voiceOf(36);
    REQUIRE(v >= 0);
    Rig ref([](ymengine::GlobalParams&) {},
            [](ymengine::TrackParams& t) { t.patch.generator = ymengine::Generator::Chipdrum; });
    ref.e.noteOn(0, 36, 127);
    ref.e.render(ref.l, ref.r, 480);
    k.e.channelPressure(0, 1);
    k.e.modWheel(0, 1);
    for (int b = 0; b < 4; ++b) {
        k.e.render(k.l, k.r, 480);
        ref.e.render(ref.l, ref.r, 480);
        CHECK(k.e.voice(v).volume == ref.e.voice(v).volume);
    }
}
