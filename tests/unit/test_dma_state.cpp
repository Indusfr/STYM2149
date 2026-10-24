// STYM2149 — STE DMA sound in the plugin (M6, B8, D-153): DMAS chunk of the state (bank), DMA sample of the track
// (TRKX, Patches), values brought back into their ranges, Computer parameter (ST or STE machine), notes of the DMA
// tracks through the driver, machine change, preview; conversion of a file (rate, mono, stereo, 32 KB), raw bytes,
// Sign and Gain tools.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

#include "plugincore/DmaSample.h"
#include "plugincore/Driver.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "plugincore/Patches.h"
#include "plugincore/State.h"

using namespace stym;
using namespace stym::plugin;
using Catch::Approx;

namespace {

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

std::vector<int8_t> bytes(size_t n, int8_t v) { return std::vector<int8_t>(n, v); }

// DMA voices that sound.
int dmaVoices(const ymengine::Engine& e) {
    int n = 0;
    for (int i = 0; i < ymengine::Engine::kVoices; ++i) n += e.dmaVoice(i).on ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("DMA: DMAS chunk of the state, absent by default, read back identically", "[dma][state]") {
    PluginState def;
    CHECK_FALSE(hasChunk(encodeState(def), "DMAS"));

    PluginState st;
    st.extras.dma.slots[5] = {0, true, 48, true};
    st.extras.dma.slots[7].rate = 3;
    st.extras.dmaSamples[5] = {1, -2, 3, -4, 5, -6};
    st.extras.dmaSamples[0] = bytes(ymengine::kDmaMaxBytes, -128);
    const std::vector<uint8_t> enc = encodeState(st);
    CHECK(hasChunk(enc, "DMAS"));
    const DecodeResult back = decodeState(enc);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.dma == st.extras.dma);
    CHECK(back.state.extras.dmaSamples == st.extras.dmaSamples);
    CHECK(encodeState(back.state) == enc);

    // A single sample is enough to write the chunk.
    PluginState only;
    only.extras.dmaSamples[3] = {7};
    CHECK(hasChunk(encodeState(only), "DMAS"));
    CHECK(decodeState(encodeState(only)).state.extras.dmaSamples[3] == std::vector<int8_t>{7});
}

TEST_CASE("DMA: sample of the track in TRKX (when not default) and in the Patches", "[dma][state]") {
    PluginState def;
    PluginState st;
    st.extras.tracks[2].patch.dmaSlot = 6;
    const std::vector<uint8_t> a = encodeState(def), b = encodeState(st);
    CHECK(b.size() == a.size() + 1);  // one more byte for the only track set
    const DecodeResult back = decodeState(b);
    REQUIRE(back.ok);
    CHECK(back.state.extras.tracks[2].patch.dmaSlot == 6);
    CHECK(back.state.extras.tracks[3].patch.dmaSlot == -1);
    CHECK(decodeState(a).state.extras.tracks[2].patch.dmaSlot == -1);

    // Patches: "dmaSlot" key written only when not default (export of the prototype unchanged), read back on another
    // track.
    const ParamValues v = defaultValues();
    CHECK(dumpPatchLibrary({snapPatch("x", v, def.extras, 2)}).find("dmaSlot") == std::string::npos);
    const std::string text = dumpPatchLibrary({snapPatch("x", v, st.extras, 2)});
    CHECK(text.find("\"dmaSlot\"") != std::string::npos);
    const auto lib = parsePatchLibrary(text);
    REQUIRE(lib);
    REQUIRE(lib->size() == 1);
    CHECK(patchChange((*lib)[0], NonParamState{}, 9).track.patch.dmaSlot == 6);
    const auto bad = parsePatchLibrary(R"([{"name":"y","patch":{"dmaSlot":40}}])");
    REQUIRE(bad);
    CHECK(patchChange((*bad)[0], NonParamState{}, 0).track.patch.dmaSlot == 7);
}

TEST_CASE("DMA: out-of-range values brought back, sample truncated to 32 KB", "[dma][state]") {
    NonParamState n;
    n.dma.slots[2].rate = 7;
    n.dma.slots[2].root = 200;
    n.tracks[4].patch.dmaSlot = 12;
    n.tracks[5].patch.dmaSlot = -9;
    n.dmaSamples[1] = bytes(ymengine::kDmaMaxBytes + 100, 3);
    sanitize(n);
    CHECK(n.dma.slots[2].rate == ymengine::kDmaRates - 1);
    CHECK(n.dma.slots[2].root == 127);
    CHECK(n.tracks[4].patch.dmaSlot == ymengine::kDmaSlots - 1);
    CHECK(n.tracks[5].patch.dmaSlot == -1);
    CHECK(n.dmaSamples[1].size() == ymengine::kDmaMaxBytes);
}

TEST_CASE("DMA: Computer parameter (ST or STE machine), version 3, DMA type of the generator", "[dma][params]") {
    const ParamSpec& s = paramSpec(paramIndex(Global::Computer));
    CHECK(s.id == "computer");
    CHECK(s.name == "Computer");
    CHECK(s.version == 3);
    CHECK(s.def == 0.0f);
    REQUIRE(s.choices.size() == 2);
    CHECK(s.choices[0] == "ST");
    CHECK(s.choices[1] == "STE");
    const ParamSpec& gen = paramSpec(paramIndex(0, Track::Generator));
    REQUIRE(gen.choices.size() == size_t(ymengine::kGenerators));
    CHECK(gen.choices.back() == "DMA");
}

TEST_CASE("DMA: notes of a DMA track through the driver, machine change, preview", "[dma][driver]") {
    Snapshot snap;
    snap.state.dmaSamples[0] = bytes(20000, 100);
    snap.state.dmaSamples[4] = bytes(20000, 50);
    for (auto& sl : snap.state.dma.slots) sl.loop = true;
    snap.dmaVersion.fill(1);
    Driver d;
    ParamValues v = defaultValues();
    v[size_t(paramIndex(Global::Computer))] = 1;     // STE
    v[size_t(paramIndex(0, Track::Generator))] = 5;  // track 1: DMA
    d.prepare({48000, 0}, v, snap);
    std::vector<float> left(256), right(256);
    auto run = [&](std::vector<MidiEvent> midi = {}) {
        ymengine::Outputs o;
        o.left = left.data();
        o.right = right.data();
        d.process(v, midi, o, 256, false, nullptr);
    };
    auto msg = [](uint8_t st, uint8_t a, uint8_t b) {
        MidiEvent e;
        e.size = 3;
        e.data = {st, a, b};
        return e;
    };

    run({msg(0x90, 62, 100)});
    REQUIRE(dmaVoices(d.engine()) == 1);
    // Bend: ± Bend Range (2 semitones), up.
    run({msg(0xE0, 0x7F, 0x7F)});
    bool bent = false;
    for (int i = 0; i < ymengine::Engine::kVoices; ++i)
        if (const auto info = d.engine().dmaVoice(i); info.on)
            bent = info.step == Approx(std::exp2((2 + 2 * 8191 / 8192.0) / 12.0));
    CHECK(bent);
    run({msg(0xE0, 0x00, 0x40)});

    // ST machine: fade and settings applied again like a mode change; the track plays again as Chip Voice.
    v[size_t(paramIndex(Global::Computer))] = 0;
    for (int k = 0; k < 8; ++k) run();
    CHECK(dmaVoices(d.engine()) == 0);
    CHECK(d.engine().activeVoices() == 0);
    run({msg(0x90, 60, 100)});
    CHECK(d.engine().activeVoices() == 1);
    CHECK(dmaVoices(d.engine()) == 0);
    run({msg(0x80, 60, 0)});

    // Preview of a slot (DMA window): whatever the machine.
    MidiEvent pv;
    pv.screen = true;
    pv.command = UiCommand::DmaPreview;
    pv.data = {4, 0, 0};
    run({pv});
    CHECK(d.engine().dmaPreviewSlot() == 4);
    CHECK(d.engine().dmaPlayingSlots() == 1u << 4);
}

TEST_CASE("DMA: file converted to the slot rate, mono or stereo, 32 KB at most", "[dma][sample]") {
    // Constant 0.5 at 50066 Hz to 25033 Hz: half as many frames, round(0.5 × 127) = 64.
    const std::vector<float> half(1000, 0.5f);
    const std::vector<int8_t> m = toDmaSample(half, {}, 50066, 25033);
    CHECK(m.size() == 500);
    CHECK(m[0] == 64);
    CHECK(m[499] == 64);

    // Stereo: bytes interleaved left, right; clamped to -128..127.
    const std::vector<float> l(100, 2.0f), r(100, -2.0f);
    const std::vector<int8_t> s = toDmaSample(l, r, 12517, 12517);
    REQUIRE(s.size() == 200);
    CHECK(s[0] == 127);
    CHECK(s[1] == -128);

    // Linear interpolation between two samples of the file (double rate).
    const std::vector<float> ramp = {0.0f, 1.0f};
    const std::vector<int8_t> up = toDmaSample(ramp, {}, 1000, 2000);
    REQUIRE(up.size() == 4);
    CHECK(up[1] == 64);   // 0.5
    CHECK(up[3] == 64);   // the sample after the end counts as 0

    // 32 KB at most (16 KB per channel in stereo); non-finite values: 0.
    std::vector<float> longer(100000, 0.0f);
    longer[0] = NAN;
    CHECK(toDmaSample(longer, {}, 25033, 25033).size() == ymengine::kDmaMaxBytes);
    CHECK(toDmaSample(longer, {}, 25033, 25033)[0] == 0);
    CHECK(toDmaSample(longer, longer, 25033, 25033).size() == ymengine::kDmaMaxBytes);
    CHECK(toDmaSample({}, {}, 25033, 25033).empty());
    CHECK(toDmaSample(half, {}, 0, 25033).empty());
}

TEST_CASE("DMA: raw bytes, Sign and Gain", "[dma][sample]") {
    const std::vector<uint8_t> raw = {0x00, 0x7F, 0x80, 0xFF, 0x10};
    CHECK(dmaRaw(raw, false) == std::vector<int8_t>{0, 127, -128, -1, 16});
    CHECK(dmaRaw(raw, true).size() == 4);  // stereo: even number of bytes
    CHECK(dmaRaw(std::vector<uint8_t>(40000, 1), false).size() == ymengine::kDmaMaxBytes);

    std::vector<int8_t> b = {0, 127, -128, -1};
    dmaSign(b);
    CHECK(b == std::vector<int8_t>{-128, -1, 0, 127});
    dmaSign(b);
    CHECK(b == std::vector<int8_t>{0, 127, -128, -1});

    std::vector<int8_t> g = {100, -100, 10, 0};
    dmaGain(g, 6);
    CHECK(g == std::vector<int8_t>{127, -128, 20, 0});
    std::vector<int8_t> q = {100, -100, 10};
    dmaGain(q, -6.0206);
    CHECK(q == std::vector<int8_t>{50, -50, 5});
}
