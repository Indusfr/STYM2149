// STYM2149 — voice engine: notes played live outside the chip's range and drum pads of a MIDI controller,
// keys that reach a drum track, the focused track played even when switched off.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <functional>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using ymengine::Engine;
using ymengine::Generator;
using ymengine::VoiceMode;

namespace {

// Engine at 48 kHz in `mode`, track 0 focused; `set` gives each track its generator and switch.
struct Rig {
    Engine e;
    float l[480], r[480];
    Rig(VoiceMode mode, const std::function<void(int, ymengine::TrackParams&)>& set,
        const std::function<void(ymengine::GlobalParams&)>& global = {}) {
        ymengine::EngineConfig c;
        c.render.hostRate = 48000;
        e.prepare(c);
        ymengine::GlobalParams g = e.global();
        g.mode = mode;
        if (global) global(g);
        e.setGlobal(g);
        for (int t = 0; t < ymengine::kTracks; ++t) {
            ymengine::TrackParams p;
            p.mix.level = 1;
            p.patch.volume = 12;
            set(t, p);
            e.setTrack(t, p);
        }
        // A Digidrum sample of 4000 values (half a second at 7 800 Hz) in each slot.
        const std::vector<uint8_t> sample(4000, 12);
        for (int slot = 0; slot < ymengine::kDigidrumPads; ++slot) e.setUserSample(slot, sample);
    }
    void run(int blocks = 2) {
        for (int k = 0; k < blocks; ++k) e.render(l, r, 480);
    }
    // Voices sounding, and the first one on `track` (-1: none).
    int sounding() const {
        int n = 0;
        for (int i = 0; i < Engine::kVoices; ++i) n += e.voice(i).on;
        return n;
    }
    int voiceOn(int track) const {
        for (int i = 0; i < Engine::kVoices; ++i)
            if (e.voice(i).on && e.voice(i).track == track) return i;
        return -1;
    }
};

}  // namespace

TEST_CASE("Live notes: below B0 and above C9 silent on a melodic track, heard on a drum track",
          "[engine][pads]") {
    for (const auto& [note, heard] : {std::pair{22, false}, std::pair{23, true}, std::pair{120, true},
                                      std::pair{121, false}, std::pair{0, false}, std::pair{127, false}}) {
        CAPTURE(note);
        Rig k(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) { p.mix.enabled = t == 0; });
        k.e.noteOn(0, note, 100);
        k.run();
        CHECK((k.sounding() > 0) == heard);
        // A drum track plays every key, as on the on-screen keyboard.
        Rig d(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) {
            p.mix.enabled = t == 0;
            p.patch.generator = Generator::Chipdrum;
        });
        d.e.noteOn(0, note, 100);
        d.run();
        CHECK(d.voiceOn(0) >= 0);
    }

    // Layers: out of range, the key sounds on the drum track alone.
    Rig layers(
        VoiceMode::Studio16,
        [](int t, ymengine::TrackParams& p) {
            p.mix.enabled = t < 2;
            if (t == 1) p.patch.generator = Generator::Chipdrum;
        },
        [](ymengine::GlobalParams& g) { g.layer = true; });
    layers.e.noteOn(0, 20, 100);
    layers.run();
    CHECK(layers.voiceOn(0) < 0);
    CHECK(layers.voiceOn(1) >= 0);
    layers.e.noteOn(0, 60, 100);
    layers.run();
    CHECK(layers.voiceOn(0) >= 0);

    // The arpeggio of a melodic track skips a key out of range and plays the others.
    Rig arp(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) {
        p.mix.enabled = t == 0;
        p.arp.on = true;
    });
    arp.e.noteOn(0, 121, 100);
    arp.run(4);
    CHECK(arp.sounding() == 0);
    arp.e.noteOn(0, 60, 100);
    arp.run(4);
    CHECK(arp.voiceOn(0) >= 0);
}

TEST_CASE("Drum pads: Chipdrum sounds and Digidrum samples on the track that plays them", "[engine][pads]") {
    // Track 0 melodic, 1 Chipdrum, 2 Digidrum: the bottom row on track 1, the top row on track 2.
    Rig k(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) {
        p.mix.enabled = t < 3;
        if (t == 1) p.patch.generator = Generator::Chipdrum;
        if (t == 2) p.patch.generator = Generator::Digidrum;
    });
    k.e.drumPad(0, 3, 100);
    k.e.drumPad(0, 9, 100);
    k.run(1);
    const int chip = k.voiceOn(1), digi = k.voiceOn(2);
    REQUIRE(chip >= 0);
    REQUIRE(digi >= 0);
    CHECK(k.e.voice(chip).gen == Generator::Chipdrum);
    CHECK(k.e.voice(chip).slot == 3);
    CHECK(k.e.voice(chip).note == ymengine::kPadNote + 3);
    CHECK(k.e.voice(digi).gen == Generator::Digidrum);
    CHECK(k.e.voice(digi).slot == 1);
    CHECK(k.e.voice(digi).note == ymengine::kPadNote + 1);
    CHECK(k.voiceOn(0) < 0);
    // The release of a Chipdrum pad leaves the hit to its end.
    k.e.drumPadOff(0, 3);
    k.e.render(k.l, k.r, 48);
    CHECK(k.voiceOn(1) >= 0);

    // A looped Digidrum sample sounds while its pad is held; its release lets the sample play to its end.
    Rig loop(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) {
        p.mix.enabled = t == 0;
        p.patch.generator = Generator::Digidrum;
    });
    ymengine::DigidrumSlot looped;
    looped.loop = true;
    loop.e.setDigidrumSlot(2, looped);
    loop.e.drumPad(0, 10, 100);
    loop.run(200);  // two seconds: twice the sample, played at note 50 against its root 60 (0.9 s)
    CHECK(loop.voiceOn(0) >= 0);
    loop.e.drumPadOff(0, 10);
    loop.run(120);
    CHECK(loop.voiceOn(0) < 0);

    // No track with the sound: the first active drum track plays it with that sound.
    Rig drum(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) {
        p.mix.enabled = t < 2;
        if (t == 1) p.patch.generator = Generator::Chipdrum;
    });
    drum.e.drumPad(0, 12, 100);
    drum.run(1);
    const int v = drum.voiceOn(1);
    REQUIRE(v >= 0);
    CHECK(drum.e.voice(v).gen == Generator::Digidrum);
    CHECK(drum.e.voice(v).slot == 4);

    // No drum track at all: the focused track, with the pad's sound; an inactive track with the sound comes first.
    Rig none(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) { p.mix.enabled = t == 0; });
    none.e.drumPad(0, 5, 100);
    none.run(1);
    REQUIRE(none.voiceOn(0) >= 0);
    CHECK(none.e.voice(none.voiceOn(0)).gen == Generator::Chipdrum);
    CHECK(none.e.voice(none.voiceOn(0)).slot == 5);
    Rig off(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) {
        p.mix.enabled = t == 0;
        if (t == 6) p.patch.generator = Generator::Chipdrum;
    });
    off.e.drumPad(0, 0, 100);
    off.run(1);
    CHECK(off.voiceOn(6) >= 0);
    CHECK(off.voiceOn(0) < 0);

    // 3 channels: the Digidrum samples on the Digidrum track (track 4).
    Rig three(VoiceMode::Authentic3, [](int t, ymengine::TrackParams& p) {
        p.mix.enabled = t == 0 || t == 3;
        if (t == 3) p.patch.generator = Generator::Digidrum;
    });
    three.e.drumPad(0, 8, 100);
    three.run(1);
    REQUIRE(three.voiceOn(3) >= 0);
    CHECK(three.e.voice(three.voiceOn(3)).slot == 0);
    CHECK(three.voiceOn(0) < 0);

    // Velocity 0 plays nothing; a pad beyond 15 neither.
    Rig zero(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) {
        p.mix.enabled = t == 0;
        p.patch.generator = Generator::Chipdrum;
    });
    zero.e.drumPad(0, 2, 0);
    zero.e.drumPad(0, 16, 100);
    zero.e.drumPad(0, -1, 100);
    zero.run(1);
    CHECK(zero.sounding() == 0);
}

TEST_CASE("Drum pads: the focused drum track first, a given track alone", "[engine][pads]") {
    // Tracks 1 and 3 Chipdrum, track 3 focused: the pads play on track 3.
    Rig k(
        VoiceMode::Studio16,
        [](int t, ymengine::TrackParams& p) {
            p.mix.enabled = t < 3;
            if (t == 0 || t == 2) p.patch.generator = Generator::Chipdrum;
        },
        [](ymengine::GlobalParams& g) { g.focusTrack = 2; });
    k.e.drumPad(0, 1, 100);
    k.run(1);
    CHECK(k.voiceOn(2) >= 0);
    CHECK(k.voiceOn(0) < 0);
    // A track given (routing by channel) plays the pad alone, active or not, with the pad's sound.
    Rig given(VoiceMode::Studio16, [](int t, ymengine::TrackParams& p) {
        p.mix.enabled = t == 0;
        if (t == 0) p.patch.generator = Generator::Chipdrum;
    });
    given.e.drumPad(0, 9, 100, 5);
    given.run(1);
    REQUIRE(given.voiceOn(5) >= 0);
    CHECK(given.e.voice(given.voiceOn(5)).gen == Generator::Digidrum);
    CHECK(given.e.voice(given.voiceOn(5)).slot == 1);
    CHECK(given.voiceOn(0) < 0);
}

// A selected track switched off: the keys still play it, with its own sound (Indus, 01/11/2026 18:06-18:13).
// They used to go to every active track, so track 1 played its square and the selected track's sound, ADSR and vibrato
// were never heard.
TEST_CASE("Live notes: the focused track plays even when switched off, Layer adds the active tracks",
          "[engine][pads]") {
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3}) {
        INFO("mode " << int(mode));
        auto set = [](int t, ymengine::TrackParams& p) {
            p.mix.enabled = t == 0;
            if (t == 1) p.patch.generator = Generator::Sid;
        };
        // Track 2 (SID Voice, off) focused: the key plays track 2 alone.
        Rig solo(mode, set, [](ymengine::GlobalParams& g) { g.focusTrack = 1; });
        solo.e.noteOn(0, 60, 100);
        solo.run();
        REQUIRE(solo.voiceOn(1) >= 0);
        CHECK(solo.e.voice(solo.voiceOn(1)).gen == Generator::Sid);
        CHECK(solo.voiceOn(0) < 0);
        solo.e.noteOff(0, 60);
        solo.run(40);
        CHECK(solo.sounding() == 0);
        // With Layer: the focused track and the active track 1.
        Rig layer(mode, set, [](ymengine::GlobalParams& g) {
            g.focusTrack = 1;
            g.layer = true;
        });
        layer.e.noteOn(0, 60, 100);
        layer.run();
        CHECK(layer.voiceOn(1) >= 0);
        CHECK(layer.voiceOn(0) >= 0);
    }
    // A drum pad goes to the focused drum track, on or off, before an active drum track.
    Rig pads(
        VoiceMode::Studio16,
        [](int t, ymengine::TrackParams& p) {
            p.mix.enabled = t == 0;
            p.patch.generator = Generator::Chipdrum;
        },
        [](ymengine::GlobalParams& g) { g.focusTrack = 2; });
    pads.e.drumPad(0, 1, 100);
    pads.run(1);
    CHECK(pads.voiceOn(2) >= 0);
    CHECK(pads.voiceOn(0) < 0);
}
