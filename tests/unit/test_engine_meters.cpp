// STYM2149 — meters and oscilloscopes of the tracks (Engine::takeMeters, Mixer window of the prototype: chPk, scopeBuf)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "ymcore/Ym2149.h"
#include "ymengine/Engine.h"

using namespace stym;
using ymengine::Engine;

namespace {

// Level of one channel alone at 5-bit index i, normalised to index 31 (D-049).
double level(int i) { return double(ymcore::tables::kMixTable[i]) / ymcore::tables::kMixTable[31]; }

struct Rig {
    Engine e;
    Engine::TrackMeters m;
    std::vector<float> l = std::vector<float>(480), r = std::vector<float>(480);

    explicit Rig(ymengine::VoiceMode mode) {
        ymengine::EngineConfig c;
        c.render.hostRate = 48000;
        e.prepare(c);
        ymengine::GlobalParams g;
        g.mode = mode;
        e.setGlobal(g);
    }
    // Renders n blocks of 10 ms; returns the number of oscilloscope points written during the last one.
    uint32_t run(int blocks) {
        uint32_t points = 0;
        for (int b = 0; b < blocks; ++b) {
            const uint32_t before = m.scopePos;
            e.render(l.data(), r.data(), 480);
            e.takeMeters(m);
            points = (m.scopePos + Engine::kScopePoints - before) % Engine::kScopePoints;
        }
        return points;
    }
    float peakSince(int blocks, int track) {
        float p = 0;
        for (int b = 0; b < blocks; ++b) {
            run(1);
            p = std::max(p, m.peak[size_t(track)]);
        }
        return p;
    }
};

}  // namespace

TEST_CASE("16-channel meters: peak of a voice, oscilloscope of the track, 6 000 points per second", "[engine][mixer]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    rig.e.noteOn(0, 69, 127, 2);  // volume 13 of the default patch, track level 0.75
    const float want = float(level(2 * 13 + 1) * 0.75);
    // 10 ms per block: 60 points (the steps computed ahead for the resampler shift by one point at most).
    for (int b = 0; b < 20; ++b) {
        const uint32_t points = rig.run(1);
        CHECK(points >= 59);
        CHECK(points <= 61);
        CHECK(rig.m.peak[2] == want);
        for (int t = 0; t < ymengine::kTracks; ++t)
            if (t != 2) CHECK(rig.m.peak[size_t(t)] == 0.0f);
    }
    // Oscilloscope: the square of the voice (low level: index 0), nothing on the other tracks.
    int high = 0, low = 0;
    for (float v : rig.m.scope[2]) {
        const bool atHigh = std::abs(v - want) < 1e-6f, atLow = std::abs(v - float(level(0) * 0.75)) < 1e-6f;
        CHECK((atHigh || atLow));
        high += atHigh;
        low += atLow;
    }
    CHECK(high > 20);
    CHECK(low > 20);
    for (int t = 0; t < ymengine::kTracks; ++t)
        if (t != 2) CHECK(std::all_of(rig.m.scope[size_t(t)].begin(), rig.m.scope[size_t(t)].end(), [](float v) { return v == 0.0f; }));

    // The peaks start again from zero at each read: track muted, nothing any more.
    ymengine::TrackMix mix;
    mix.mute = true;
    rig.e.setTrackMix(0, 2, mix);
    rig.run(1);
    CHECK(rig.peakSince(5, 2) == 0.0f);
    CHECK(std::all_of(rig.m.scope[2].begin(), rig.m.scope[2].end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("16-channel meters: chord on a track, peak of one voice and sum in the oscilloscope", "[engine][mixer]") {
    Rig rig(ymengine::VoiceMode::Studio16);  // polyphonic track (default setting)
    rig.e.noteOn(0, 60, 127, 0);
    rig.e.noteOn(0, 64, 127, 0);
    const float one = float(level(27) * 0.75);
    CHECK(rig.peakSince(10, 0) == one);  // peak of one voice (chPk of the prototype), not of the sum
    const float most = *std::max_element(rig.m.scope[0].begin(), rig.m.scope[0].end());
    CHECK(std::abs(most - 2 * one) < 1e-5f);  // the two voices high together
}

TEST_CASE("3-channel meters: level of the chip channel, 60 points per 10 ms block", "[engine][mixer][3ch]") {
    Rig rig(ymengine::VoiceMode::Authentic3);
    rig.e.noteOn(0, 69, 127, 1);
    for (int b = 0; b < 10; ++b) CHECK(rig.run(1) == 60);
    int ch = 0;  // chip channel taken by the note (voices 0 to 2: channels A to C)
    while (ch < 3 && !(rig.e.voice(ch).on && rig.e.voice(ch).track == 1)) ++ch;
    REQUIRE(ch < 3);
    const int vol = rig.e.chip(0).chip().readRegister(unsigned(8 + ch));
    REQUIRE(vol > 0);
    CHECK(rig.m.peak[1] == float(level(2 * vol + 1)));
    for (int t = 0; t < ymengine::kTracks; ++t)
        if (t != 1) CHECK(rig.m.peak[size_t(t)] == 0.0f);
    int high = 0;
    for (float v : rig.m.scope[1]) {
        CHECK((v == float(level(2 * vol + 1)) || v == float(level(0))));
        high += v > 0.5f * float(level(2 * vol + 1));
    }
    CHECK(high > 20);
    CHECK(std::all_of(rig.m.scope[0].begin(), rig.m.scope[0].end(), [](float v) { return v == 0.0f; }));
}
