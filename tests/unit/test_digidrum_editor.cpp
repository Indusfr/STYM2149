// STYM2149 — Digidrum editor: rate, root note and fine tune per slot, held loop (released at the end
// of the note), preview; DIGS chunk of the state, values brought back into their ranges; 4-bit Linear and YM levels
// conversion, 8-bit source, tools; named banks (JSON, base64).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "plugincore/Digidrum.h"
#include "plugincore/DigidrumBanks.h"
#include "plugincore/Driver.h"
#include "plugincore/Parameters.h"
#include "plugincore/State.h"
#include "ymcore/Ym2149.h"
#include "ymengine/Engine.h"
#include "ymengine/Mfp68901.h"

using namespace stym;
using namespace stym::plugin;
using Catch::Approx;
using ymengine::Engine;

namespace {

using Log = std::vector<Engine::WriteRecord>;

// 3-channel engine whose register writes are recorded; track 3 plays the Digidrums on channel C.
struct Rig {
    Engine e;
    Log log;
    std::vector<float> l = std::vector<float>(4096), r = std::vector<float>(4096);

    Rig() {
        ymengine::EngineConfig c;
        c.render.hostRate = 48000;
        e.prepare(c);
        ymengine::GlobalParams g;
        g.mode = ymengine::VoiceMode::Authentic3;
        g.focusTrack = 3;
        e.setGlobal(g);
        ymengine::TrackParams drum;
        drum.mix.enabled = true;
        drum.mix.level = 1;
        drum.patch.generator = ymengine::Generator::Digidrum;
        e.setTrack(3, drum);
        log.reserve(1 << 18);
        e.setWriteLog(&log);
    }
    void run(uint32_t n) {
        while (n > 0) {
            const uint32_t k = std::min<uint32_t>(n, uint32_t(l.size()));
            e.render(l.data(), r.data(), k);
            n -= k;
        }
    }
    // Volumes written on channel C (register 10), in order.
    std::vector<int> volumes(size_t from = 0) const {
        std::vector<int> out;
        for (size_t i = from; i < log.size(); ++i)
            if (log[i].reg == 10 && log[i].chip == 0) out.push_back(log[i].value);
        return out;
    }
    std::vector<uint64_t> cycles() const {
        std::vector<uint64_t> out;
        for (const auto& w : log)
            if (w.reg == 10 && w.chip == 0) out.push_back(w.cycle);
        return out;
    }
};

double peakOf(const std::vector<float>& x, size_t a, size_t b) {
    double p = 0;
    for (size_t i = a; i < b && i < x.size(); ++i) p = std::max(p, double(std::fabs(x[i])));
    return p;
}

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

// Normalised output of a single channel at volume n (measured mixing table, level 2n + 1).
double level(int n) {
    const double lo = ymcore::tables::kMixTable[1], hi = ymcore::tables::kMixTable[31];
    return (ymcore::tables::kMixTable[2 * n + 1] - lo) / (hi - lo);
}

}  // namespace

// --------------------------------------------------------------------------------------------------------- engine

TEST_CASE("Digidrum: rate, root note and fine tune of the slot", "[digidrum][engine]") {
    const uint8_t sample[] = {1, 2, 3, 4, 5, 6, 7, 8};
    auto spacing = [&](uint8_t note, const ymengine::DigidrumSlot& d) {
        Rig rig;
        rig.e.setUserSample(2, sample);
        rig.e.setDigidrumSlot(2, d);
        auto t = rig.e.track(3);
        t.patch.digidrumSlot = 2;
        rig.e.setTrack(3, t);
        rig.e.noteOn(0, note, 127);
        rig.run(4800);
        const std::vector<uint64_t> c = rig.cycles();
        REQUIRE(c.size() >= 6);
        return double(c[5] - c[2]) / 3;
    };
    // Default: 7800 Hz for C4 (the prototype).
    CHECK(std::fabs(spacing(60, {}) - 2e6 / ymengine::mfpQuantize(7800).hz()) <= 1.0);
    // 11025 Hz at root note 48: C3 plays at 11025 Hz, C4 one octave higher.
    ymengine::DigidrumSlot d;
    d.rateHz = 11025;
    d.root = 48;
    CHECK(std::fabs(spacing(48, d) - 2e6 / ymengine::mfpQuantize(11025).hz()) <= 1.0);
    CHECK(std::fabs(spacing(60, d) - 2e6 / ymengine::mfpQuantize(22050).hz()) <= 1.0);
    // Fine tune: -100 cents at the root note = one semitone lower.
    d.cents = -100;
    CHECK(std::fabs(spacing(48, d) - 2e6 / ymengine::mfpQuantize(11025 * std::exp2(-1 / 12.0)).hz()) <= 1.0);
}

TEST_CASE("Digidrum: slot settings brought back into their ranges", "[digidrum][engine]") {
    Engine e;
    ymengine::DigidrumSlot d;
    d.rateHz = 1e9;
    d.root = 200;
    d.cents = -500;
    d.start = 900000;
    d.end = 800000;
    e.setDigidrumSlot(1, d);
    const ymengine::DigidrumSlot& g = e.digidrumSlot(1);
    CHECK(g.rateHz == ymengine::kDigidrumRateMax);
    CHECK(g.root == 127);
    CHECK(g.cents == -ymengine::kDigidrumCentsMax);
    CHECK(g.end == Engine::kMaxUserSample);
    CHECK(g.start == g.end);
    d.rateHz = NAN;
    e.setDigidrumSlot(1, d);
    CHECK(e.digidrumSlot(1).rateHz == ymengine::kDigidrumRate);
    CHECK(e.digidrumSlot(0) == ymengine::DigidrumSlot{});
}

TEST_CASE("Digidrum: Start to End looped while the note is held, then played on to End",
          "[digidrum][engine]") {
    Rig rig;
    const uint8_t sample[] = {1, 2, 3, 4, 5, 6};
    rig.e.setUserSample(0, sample);
    ymengine::DigidrumSlot d;
    d.loop = true;
    d.start = 2;
    d.end = 5;  // values 3, 4, 5
    rig.e.setDigidrumSlot(0, d);
    rig.e.noteOn(0, 60, 127);
    rig.run(4800);  // about 780 timer steps
    const std::vector<int> held = rig.volumes();
    REQUIRE(held.size() > 100);
    CHECK(std::vector<int>(held.begin(), held.begin() + 7) == std::vector<int>{3, 4, 5, 3, 4, 5, 3});
    for (size_t i = 0; i < held.size(); ++i) {
        INFO(i);
        CHECK(held[i] >= 3);
        CHECK(held[i] <= 5);
    }
    // End of the note: the loop stops, the part plays on to End (5, not the 6 after it), then the channel goes silent.
    const size_t mark = rig.log.size();
    rig.e.noteOff(0, 60);
    rig.run(4800);
    const std::vector<int> tail = rig.volumes(mark);
    REQUIRE(tail.size() >= 2);
    REQUIRE(tail.size() <= 4);
    CHECK(tail[tail.size() - 2] == 5);
    CHECK(tail.back() == 0);
    CHECK(rig.e.activeVoices() == 0);
}

TEST_CASE("Digidrum: Start and End, the part played without Loop", "[digidrum][engine]") {
    const uint8_t sample[] = {1, 2, 3, 4};
    struct Case {
        uint32_t start, end;
        std::vector<int> played;
    };
    // End 0 or past the sample: to its end; Start not before End: from the beginning.
    const Case cases[] = {{1, 3, {2, 3, 0}}, {0, 0, {1, 2, 3, 4, 0}}, {2, 9, {3, 4, 0}}, {2, 2, {1, 2, 0}},
                          {3, 4, {4, 0}}};
    for (const Case& c : cases) {
        Rig rig;
        rig.e.setUserSample(0, sample);
        ymengine::DigidrumSlot d;
        d.start = c.start;
        d.end = c.end;
        rig.e.setDigidrumSlot(0, d);
        rig.e.noteOn(0, 60, 127);
        rig.run(4800);
        INFO(c.start << " " << c.end);
        CHECK(rig.volumes() == c.played);
    }
    // Loop on with End never set: the whole sample loops.
    Rig rig;
    rig.e.setUserSample(0, sample);
    ymengine::DigidrumSlot d;
    d.loop = true;
    rig.e.setDigidrumSlot(0, d);
    rig.e.noteOn(0, 60, 127);
    rig.run(4800);
    const std::vector<int> held = rig.volumes();
    REQUIRE(held.size() > 8);
    CHECK(std::vector<int>(held.begin(), held.begin() + 8) == std::vector<int>{1, 2, 3, 4, 1, 2, 3, 4});
}

TEST_CASE("Digidrum: preview at the slot rate from Start to End, through the chip level table", "[digidrum][engine]") {
    Engine e;
    ymengine::EngineConfig c;
    c.render.hostRate = 48000;
    e.prepare(c);
    std::vector<uint8_t> s(780);
    for (size_t i = 0; i < s.size(); ++i) s[i] = uint8_t(i % 2 ? 15 : 0);
    e.setUserSample(4, s);
    ymengine::DigidrumSlot d;
    d.rateHz = 7800;
    e.setDigidrumSlot(4, d);
    std::vector<float> l(9600), r(9600);
    e.digidrumPreview(0, 4);
    e.render(l.data(), r.data(), 9600);
    // 780 values at 7800 Hz: 0.1 s, i.e. 4800 samples at 48 kHz; peak ±0.55 give or take the gain.
    CHECK(peakOf(l, 0, 4700) > 0.2);
    CHECK(peakOf(l, 4900, 9600) < 1e-6);
    CHECK(l == r);
    // The preview plays the part from Start to End: 390 values, i.e. 2400 samples.
    d.start = 195;
    d.end = 585;
    e.setDigidrumSlot(4, d);
    std::fill(l.begin(), l.end(), 0.0f);
    e.digidrumPreview(0, 4);
    e.render(l.data(), r.data(), 9600);
    CHECK(peakOf(l, 0, 2300) > 0.2);
    CHECK(peakOf(l, 2500, 9600) < 1e-6);
    // Empty slot: nothing.
    std::fill(l.begin(), l.end(), 0.0f);
    e.digidrumPreview(0, 5);
    e.render(l.data(), r.data(), 9600);
    CHECK(peakOf(l, 0, 9600) < 1e-6);
}

TEST_CASE("Digidrum: the preview starts and ends at the level of silence, without a click (Indus, 26/10/2026)",
          "[digidrum][engine]") {
    Engine e;
    ymengine::EngineConfig c;
    c.render.hostRate = 48000;
    e.prepare(c);
    // A hit that ends silent: 8 is the Linear value of a source byte 0.
    std::vector<uint8_t> s(780, 8);
    for (size_t i = 0; i < 100; ++i) s[i] = uint8_t(i % 2 ? 15 : 0);
    e.setUserSample(4, s);
    ymengine::DigidrumSlot d;
    d.rateHz = 7800;
    e.setDigidrumSlot(4, d);
    std::vector<float> l(9600), r(9600);
    e.digidrumPreview(0, 4);
    e.render(l.data(), r.data(), 9600);
    CHECK(peakOf(l, 0, 600) > 0.2);
    CHECK(peakOf(l, 700, 9600) < 1e-6);  // the silent tail and the end: 0, no step
    // The rest value follows the conversion.
    plugin::DigidrumSlotState st;
    CHECK(plugin::digidrumEngineSlot(st).rest == 8);
    st.convert = plugin::DigiConvert::YmLevels;
    st.play.rateHz = 11025;
    const ymengine::DigidrumSlot ym = plugin::digidrumEngineSlot(st);
    const int8_t zero = 0;
    CHECK(ym.rest == plugin::digidrumNibbles(std::span<const int8_t>(&zero, 1), plugin::DigiConvert::YmLevels)[0]);
    CHECK(ym.rateHz == 11025);
}

TEST_CASE("Digidrum: with Loop on, the preview repeats the part until stopped or Loop goes off (Indus, 26/10/2026)", "[digidrum][engine]") {
    Engine e;
    ymengine::EngineConfig c;
    c.render.hostRate = 48000;
    e.prepare(c);
    std::vector<uint8_t> s(780);
    for (size_t i = 0; i < s.size(); ++i) s[i] = uint8_t(i % 2 ? 15 : 0);
    e.setUserSample(4, s);
    ymengine::DigidrumSlot d;
    d.rateHz = 7800;
    d.loop = true;
    d.start = 195;
    d.end = 585;  // 390 values: 2400 samples at 48 kHz
    e.setDigidrumSlot(4, d);
    std::vector<float> l(9600), r(9600);
    e.digidrumPreview(0, 4);
    e.render(l.data(), r.data(), 9600);
    CHECK(peakOf(l, 9000, 9600) > 0.2);  // four times the part later, it still sounds
    CHECK(e.digidrumPreviewSlot() == 4);
    e.digidrumStopPreview(0);
    std::fill(l.begin(), l.end(), 0.0f);
    e.render(l.data(), r.data(), 9600);
    CHECK(peakOf(l, 0, 9600) < 1e-6);
    CHECK(e.digidrumPreviewSlot() == -1);
    // Loop switched off while it loops: silent from the next block.
    e.digidrumPreview(0, 4);
    e.render(l.data(), r.data(), 4800);
    d.loop = false;
    e.setDigidrumSlot(4, d);
    std::fill(l.begin(), l.end(), 0.0f);
    e.render(l.data(), r.data(), 9600);
    CHECK(peakOf(l, 0, 9600) < 1e-6);
    CHECK(e.digidrumPreviewSlot() == -1);
    // Loop off: once, as before.
    e.digidrumPreview(0, 4);
    e.render(l.data(), r.data(), 9600);
    CHECK(e.digidrumPreviewSlot() == -1);
}

// ------------------------------------------------------------------------------------------------ plugin state

TEST_CASE("Digidrum: DIGS chunk of the state, absent by default, read back identically", "[digidrum][state]") {
    PluginState def;
    CHECK_FALSE(hasChunk(encodeState(def), "DIGS"));

    PluginState st;
    DigidrumSlotState& a = st.extras.digiSlots[1];
    a.name = "Snare 1";
    a.play.rateHz = 11025;
    a.play.root = 48;
    a.play.cents = -12.5;
    a.play.loop = true;
    a.play.start = 10;
    a.play.end = 300;
    a.convert = DigiConvert::YmLevels;
    a.source = {1, -2, 3, -128, 127};
    st.extras.digiSlots[7].name = "x";
    const std::vector<uint8_t> enc = encodeState(st);
    CHECK(hasChunk(enc, "DIGS"));
    const DecodeResult back = decodeState(enc);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.digiSlots == st.extras.digiSlots);
    CHECK(encodeState(back.state) == enc);
}

TEST_CASE("Digidrum: out-of-range settings brought back, name cleaned", "[digidrum][state]") {
    NonParamState n;
    DigidrumSlotState& s = n.digiSlots[3];
    s.name = "  A\tvery long drum name here ";
    s.play.rateHz = 100;
    s.play.root = 250;
    s.play.cents = NAN;
    s.play.start = 70000;
    s.play.end = 90000;
    s.convert = DigiConvert(7);
    s.source.assign(Engine::kMaxUserSample + 10, 1);
    sanitize(n);
    CHECK(s.name == "Avery long drum");  // first 16 characters, trailing space removed
    CHECK(s.play.rateHz == ymengine::kDigidrumRateMin);
    CHECK(s.play.root == 127);
    CHECK(s.play.cents == 0);
    CHECK(s.play.end == Engine::kMaxUserSample);
    CHECK(s.play.start == Engine::kMaxUserSample);
    CHECK(s.convert == DigiConvert::YmLevels);
    CHECK(s.source.size() == Engine::kMaxUserSample);
    CHECK(cleanDigidrumName("   ").empty());
}

TEST_CASE("Digidrum: slot settings passed to the engine, preview from the screen", "[digidrum][driver]") {
    Snapshot snap;
    snap.state.digidrums[2] = std::vector<uint8_t>(2000, 15);
    snap.state.digiSlots[2].play.rateHz = 11025;
    snap.state.digiSlots[2].play.root = 50;
    snap.sampleVersion.fill(1);
    Driver d;
    ParamValues v = defaultValues();
    d.prepare({48000, 0}, v, snap);
    CHECK(d.engine().digidrumSlot(2).rateHz == 11025);
    CHECK(d.engine().digidrumSlot(2).root == 50);
    std::vector<float> left(512), right(512);
    auto run = [&](std::vector<MidiEvent> midi = {}) {
        ymengine::Outputs o;
        o.left = left.data();
        o.right = right.data();
        d.process(v, midi, o, 512, false, nullptr);
    };
    run();
    CHECK(peakOf(left, 0, 512) < 1e-6);
    MidiEvent pv;
    pv.screen = true;
    pv.command = UiCommand::DigiPreview;
    pv.data = {2, 0, 0};
    run({pv});
    CHECK(peakOf(left, 0, 512) > 0.05);

    // A changed setting arrives with the next snapshot.
    snap.state.digiSlots[2].play.cents = 30;
    d.publish(snap);
    run();
    CHECK(d.engine().digidrumSlot(2).cents == 30);
}

// -------------------------------------------------------------------------------------------- conversion, tools

TEST_CASE("Digidrum: loading rate and maximum length", "[digidrum][sample]") {
    CHECK(digidrumMaxSamples(7800) == kDigidrumMaxSamples);
    CHECK(digidrumMaxSamples(20000) == 60000);
    CHECK(digidrumMaxSamples(1e6) == 60000);
    CHECK(digidrumMaxSamples(10) == 6000);
    CHECK(digidrumMaxSamples(NAN) == kDigidrumMaxSamples);
    // A one-second file at 48 kHz, loaded at 11025 Hz: 11025 values, the source at the same length.
    const std::vector<float> one(48000, 0.5f);
    CHECK(toDigidrum(one, 48000, 11025).size() == 11025);
    const std::vector<int8_t> src = toDigidrumSource(one, 48000, 11025);
    REQUIRE(src.size() == 11025);
    CHECK(src[0] == 64);  // round(0.5 × 127)
    CHECK(toDigidrumSource(std::vector<float>(4, 2.0f), 7800)[0] == 127);
    CHECK(toDigidrumSource(std::vector<float>(4, -2.0f), 7800)[0] == -128);
}

TEST_CASE("Digidrum: 4-bit conversion Linear (prototype) and YM levels (measured mixing table)", "[digidrum][sample]") {
    // Linear: the quantisation of the prototype; the source derived from the values gives them back identically.
    std::vector<uint8_t> all(16);
    for (int i = 0; i < 16; ++i) all[size_t(i)] = uint8_t(i);
    CHECK(digidrumNibbles(digidrumSourceOf(all), DigiConvert::Linear) == all);
    CHECK(digidrumSourceOf(all).front() == -127);
    CHECK(digidrumSourceOf(all).back() == 127);
    const std::vector<int8_t> s = {-128, -127, 0, 64, 127};
    CHECK(digidrumNibbles(s, DigiConvert::Linear) == std::vector<uint8_t>{0, 0, 8, 11, 15});

    // YM levels: the level whose output is closest; monotonic, bounds 0 and 15; the logarithmic law places silence
    // high (13) and brings the output error closer to zero than Linear.
    std::vector<int8_t> ramp;
    for (int v = -128; v <= 127; ++v) ramp.push_back(int8_t(v));
    const std::vector<uint8_t> ym = digidrumNibbles(ramp, DigiConvert::YmLevels);
    const std::vector<uint8_t> lin = digidrumNibbles(ramp, DigiConvert::Linear);
    CHECK(ym.front() == 0);
    CHECK(ym.back() == 15);
    CHECK(std::is_sorted(ym.begin(), ym.end()));
    CHECK(digidrumNibbles(std::vector<int8_t>{0}, DigiConvert::YmLevels)[0] == 13);
    double errYm = 0, errLin = 0;
    for (size_t i = 0; i < ramp.size(); ++i) {
        const double target = (std::max(-1.0, ramp[i] / 127.0) + 1) / 2;
        // The chosen level is the closest: no other does better.
        for (int n = 0; n < 16; ++n) CHECK(std::fabs(level(ym[i]) - target) <= std::fabs(level(n) - target) + 1e-12);
        errYm += std::fabs(level(ym[i]) - target);
        errLin += std::fabs(level(lin[i]) - target);
    }
    CHECK(errYm < errLin / 2);
}

TEST_CASE("Digidrum: Normalize, Reverse, Fade out tools", "[digidrum][sample]") {
    std::vector<int8_t> a = {10, -20, 5, 0};
    digiNormalize(a);
    CHECK(a == std::vector<int8_t>{64, -127, 32, 0});
    std::vector<int8_t> z = {0, 0};
    digiNormalize(z);
    CHECK(z == std::vector<int8_t>{0, 0});
    digiReverse(a);
    CHECK(a == std::vector<int8_t>{0, 32, -127, 64});
    std::vector<int8_t> f = {100, 100, 100, 100};
    digiFadeOut(f);
    CHECK(f == std::vector<int8_t>{100, 75, 50, 25});
    std::vector<int8_t> none;
    digiFadeOut(none);
    digiNormalize(none);
    CHECK(none.empty());
}

// -------------------------------------------------------------------------------------------------------- banks

TEST_CASE("Digidrum: base64 (RFC 4648)", "[digidrum][banks]") {
    auto enc = [](std::string_view s) {
        return base64Encode(std::vector<uint8_t>(s.begin(), s.end()));
    };
    CHECK(enc("") == "");
    CHECK(enc("M") == "TQ==");
    CHECK(enc("Ma") == "TWE=");
    CHECK(enc("Man") == "TWFu");
    CHECK(enc("foobar") == "Zm9vYmFy");
    CHECK(base64Decode("Zm9v\nYmFy") == std::vector<uint8_t>{'f', 'o', 'o', 'b', 'a', 'r'});
    CHECK(base64Decode("TQ==") == std::vector<uint8_t>{'M'});
    for (size_t n = 0; n < 9; ++n) {
        std::vector<uint8_t> b(n);
        for (size_t i = 0; i < n; ++i) b[i] = uint8_t(250 - 37 * i);
        CHECK(base64Decode(base64Encode(b)) == b);
    }
}

TEST_CASE("Digidrum: named banks, read back identically, tolerant reading", "[digidrum][banks]") {
    NonParamState st;
    st.digidrums[0] = {0, 15, 8, 3};
    st.digiSlots[0].name = "Kick";
    st.digiSlots[0].source = {-128, 127, 0, -70};
    st.digiSlots[0].convert = DigiConvert::YmLevels;
    st.digiSlots[0].play = {8000, 50, 25, true, 1, 3};
    st.digidrums[5] = {7};
    const DigidrumBank b = captureBank("Kit one", st);
    CHECK(b.name == "Kit one");
    CHECK(b.slots == st.digiSlots);
    CHECK(b.nibbles == st.digidrums);

    const std::string text = dumpDigidrumBanks({b, captureBank("Empty", NonParamState{})});
    const auto back = parseDigidrumBanks(text);
    REQUIRE(back);
    REQUIRE(back->size() == 2);
    CHECK((*back)[0] == b);
    CHECK((*back)[1].name == "Empty");
    CHECK((*back)[1].nibbles[0].empty());

    NonParamState other;
    applyBank((*back)[0], other);
    CHECK(other.digiSlots == st.digiSlots);
    CHECK(other.digidrums == st.digidrums);

    // Other file, broken JSON: refused.
    std::string error;
    CHECK_FALSE(parseDigidrumBanks(R"({"kind":"patchlib","banks":[]})", &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(parseDigidrumBanks("{", nullptr));
    // Out-of-range values brought back, unknown characters ignored, bank without a name dropped.
    const auto odd = parseDigidrumBanks(
        R"({"kind":"digidrumlib","version":1,"banks":[{"slots":[]},{"name":"  Odd  ","slots":[{"rate":99999,"root":-4,)"
        R"("cents":400,"loop":1,"loopStart":50,"loopEnd":20,"convert":"ym","data":"0z1F-f","source":"/w=="}, 7]}]})");
    REQUIRE(odd);
    REQUIRE(odd->size() == 1);
    const DigidrumBank& o = odd->front();
    CHECK(o.name == "Odd");
    CHECK(o.slots[0].play.rateHz == ymengine::kDigidrumRateMax);
    CHECK(o.slots[0].play.root == 0);
    CHECK(o.slots[0].play.cents == ymengine::kDigidrumCentsMax);
    CHECK(o.slots[0].play.loop);
    CHECK(o.slots[0].play.end == 20);
    CHECK(o.slots[0].play.start == 20);
    CHECK(o.slots[0].convert == DigiConvert::YmLevels);
    CHECK(o.nibbles[0] == std::vector<uint8_t>{0, 1, 15, 15});
    CHECK(o.slots[0].source == std::vector<int8_t>{-1});
    CHECK(o.slots[1] == DigidrumSlotState{});
}
