// STYM2149 — STE DMA sound in the plugin: DMAS chunk of the state (bank), DMA sample of the track
// (TRKX, Patches), values brought back into their ranges, Computer parameter (ST or STE machine), notes of the DMA
// tracks through the driver, machine change, preview; conversion of a file (rate, mono, stereo, 32 KB), raw bytes,
// Sign and Gain tools; named DMA banks.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "plugincore/Digidrum.h"
#include "plugincore/DmaBanks.h"
#include "plugincore/DmaSample.h"
#include "plugincore/Driver.h"
#include "plugincore/EngineStatus.h"
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

TEST_CASE("DMA: DMAS chunk of the state, absent by default, read back identically, Start, End and name",
          "[dma][state]") {
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

    // After the eight slots: Start and End of each slot, then its name, then its fine tune. A
    // state from before the fine tune reads 0; one from before the names reads none; one from before Start and End
    // also reads the whole samples.
    st.extras.dma.slots[5].start = 1;
    st.extras.dma.slots[5].end = 5;
    st.extras.dma.slots[5].cents = -37;
    st.extras.dmaNames[5] = "Pad loop";
    const std::vector<uint8_t> full = encodeState(st);
    const DecodeResult part = decodeState(full);
    REQUIRE(part.ok);
    CHECK(part.state.extras.dma == st.extras.dma);
    CHECK(part.state.extras.dmaNames == st.extras.dmaNames);
    CHECK(encodeState(part.state) == full);
    // The last `n` bytes of the DMAS chunk removed.
    const auto cut = [&full](size_t n) {
        std::vector<uint8_t> b = full;
        size_t at = 0;
        while (at + 8 <= b.size() && std::memcmp(b.data() + at, "DMAS", 4) != 0) ++at;
        uint32_t len = 0;
        for (int i = 0; i < 4; ++i) len |= uint32_t(b[at + 4 + size_t(i)]) << (8 * i);
        const size_t from = at + 8 + len - n;
        b.erase(b.begin() + std::ptrdiff_t(from), b.begin() + std::ptrdiff_t(from + n));
        len -= uint32_t(n);
        for (int i = 0; i < 4; ++i) b[at + 4 + size_t(i)] = uint8_t(len >> (8 * i));
        return b;
    };
    const size_t cents = 8 * 8;                               // one f64 per slot
    const size_t names = 8 + std::string("Pad loop").size();  // one length byte per slot, then the name
    const DecodeResult noCents = decodeState(cut(cents));
    REQUIRE(noCents.ok);
    CHECK(noCents.state.extras.dma.slots[5].cents == 0);
    CHECK(noCents.state.extras.dma.slots[5].end == 5);
    CHECK(noCents.state.extras.dmaNames == st.extras.dmaNames);
    const DecodeResult noNames = decodeState(cut(cents + names));
    REQUIRE(noNames.ok);
    CHECK(noNames.state.extras.dma.slots[5].start == 1);
    CHECK(noNames.state.extras.dma.slots[5].end == 5);
    CHECK(noNames.state.extras.dmaNames == std::array<std::string, ymengine::kDmaSlots>{});
    const DecodeResult old = decodeState(cut(cents + names + 64));
    REQUIRE(old.ok);
    CHECK(old.state.extras.dma.slots[5].start == 0);
    CHECK(old.state.extras.dma.slots[5].end == 0);
    CHECK(old.state.extras.dma.slots[5].loop);
    CHECK(old.state.extras.dmaSamples == st.extras.dmaSamples);
    CHECK(old.state.extras.dmaNames == std::array<std::string, ymengine::kDmaSlots>{});

    // A name alone is enough to write the chunk.
    PluginState named;
    named.extras.dmaNames[2] = "Bell";
    CHECK(hasChunk(encodeState(named), "DMAS"));
    CHECK(decodeState(encodeState(named)).state.extras.dmaNames[2] == "Bell");

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

TEST_CASE("DMA: out-of-range values brought back, sample truncated to 32 KB, name cleaned", "[dma][state]") {
    NonParamState n;
    n.dma.slots[2].rate = 7;
    n.dma.slots[2].root = 200;
    n.tracks[4].patch.dmaSlot = 12;
    n.tracks[5].patch.dmaSlot = -9;
    n.dmaSamples[1] = bytes(ymengine::kDmaMaxBytes + 100, 3);
    n.dma.slots[3] = {2, false, 60, false, 900, 400};  // Start past End
    n.dma.slots[4].end = ymengine::kDmaMaxBytes + 5;
    n.dma.slots[6].start = 300;                          // End 0: to the end of the sample, Start kept
    n.dma.slots[5].cents = 250;                          // fine tune: ±100 cents
    n.dma.slots[7].cents = std::numeric_limits<double>::quiet_NaN();
    n.dmaNames[0] = "  A name far too long\tfor a slot ";  // 16 GEM characters, edges trimmed
    sanitize(n);
    CHECK(n.dmaNames[0] == "A name far too l");
    CHECK(n.dma.slots[3].start == 400);
    CHECK(n.dma.slots[3].end == 400);
    CHECK(n.dma.slots[4].end == ymengine::kDmaMaxBytes);
    CHECK(n.dma.slots[6].start == 300);
    CHECK(n.dma.slots[2].rate == ymengine::kDmaRates - 1);
    CHECK(n.dma.slots[2].root == 127);
    CHECK(n.dma.slots[5].cents == 100);
    CHECK(n.dma.slots[7].cents == 0);
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
    // The same command with data[1] 1 stops it (Play of a looping preview).
    pv.data = {4, 1, 0};
    run({pv});
    CHECK(d.engine().dmaPreviewSlot() == -1);
}

TEST_CASE("DMA: the right side of a stereo sample through the driver, its LED (Indus, 26/10/2026)",
          "[dma][driver]") {
    // Track 1 plays slot 0, stereo, both sides the same square wave.
    Snapshot snap;
    std::vector<int8_t> square;
    for (int i = 0; i < 10000; ++i) square.insert(square.end(), 2, int8_t((i / 8) % 2 ? 100 : -100));
    snap.state.dmaSamples[0] = square;
    snap.state.dma.slots[0].stereo = true;
    snap.state.dma.slots[0].loop = true;
    snap.dmaVersion.fill(1);
    Driver d;
    ParamValues v = defaultValues();
    v[size_t(paramIndex(Global::Computer))] = 1;     // STE
    v[size_t(paramIndex(0, Track::Generator))] = 5;  // track 1: DMA
    d.prepare({48000, 0}, v, snap);
    EngineStatusTap tap;
    tap.prepare(48000);
    std::vector<float> left(4800), right(4800);
    auto run = [&](std::vector<MidiEvent> midi = {}) {
        ymengine::Outputs o;
        o.left = left.data();
        o.right = right.data();
        d.process(v, midi, o, uint32_t(left.size()), false, nullptr);
        tap.capture(d.engine(), v, left.data(), right.data(), uint32_t(left.size()));
        tap.update();
    };
    auto rms = [](const std::vector<float>& x) {
        double e = 0;
        for (float f : x) e += double(f) * f;
        return std::sqrt(e / double(x.size()));
    };
    MidiEvent on;
    on.size = 3;
    on.data = {0x90, 60, 100};
    run({on});
    run();
    REQUIRE(d.engine().trackStereo(0));
    const double l0 = rms(left), r0 = rms(right);
    CHECK(l0 > 0.01);
    CHECK(r0 == Approx(l0).epsilon(0.01));
    CHECK(tap.latest().active[0]);
    // Link off, Level R at a third of the Level: the right side drops, live.
    v[size_t(paramIndex(0, Track::Link))] = 0;
    v[size_t(paramIndex(0, Track::LevelR))] = 25;
    run();
    run();
    CHECK(rms(left) == Approx(l0).epsilon(0.01));
    CHECK(rms(right) == Approx(r0 / 3).epsilon(0.03));
    // Left side muted: the right one alone, the LED stays lit.
    v[size_t(paramIndex(0, Track::Mute))] = 1;
    for (int k = 0; k < 4; ++k) run();
    CHECK(rms(left) < l0 * 1e-3);
    CHECK(rms(right) > 0.01);
    CHECK(tap.latest().active[0]);
    // Both sides muted: the LED goes out.
    v[size_t(paramIndex(0, Track::MuteR))] = 1;
    run();
    CHECK_FALSE(tap.latest().active[0]);
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

TEST_CASE("DMA: the Digidrums tools, on the left, right or both channels of a stereo sample (Indus, 26/10/2026)",
          "[dma][sample]") {
    // Mono: the whole sample, whatever the channels.
    std::vector<int8_t> m = {1, 2, 3, 4};
    dmaTool(m, false, 1, DmaTool::Reverse);
    CHECK(m == std::vector<int8_t>{4, 3, 2, 1});

    // Stereo, frames (L, R): Reverse turns the frames round without swapping the channels.
    const std::vector<int8_t> st = {1, -1, 2, -2, 3, -3};
    std::vector<int8_t> b = st;
    dmaTool(b, true, 3, DmaTool::Reverse);
    CHECK(b == std::vector<int8_t>{3, -3, 2, -2, 1, -1});
    b = st;
    dmaTool(b, true, 1, DmaTool::Reverse);  // left only
    CHECK(b == std::vector<int8_t>{3, -1, 2, -2, 1, -3});
    b = st;
    dmaTool(b, true, 2, DmaTool::Sign);  // right only
    CHECK(b == std::vector<int8_t>{1, int8_t(-1 ^ 0x80), 2, int8_t(-2 ^ 0x80), 3, int8_t(-3 ^ 0x80)});
    b = st;
    dmaTool(b, true, 1, DmaTool::Gain, 6.0206);
    CHECK(b == std::vector<int8_t>{2, -1, 4, -2, 6, -3});

    // Normalize: both channels share one gain (balance kept); one channel alone reaches full scale.
    const std::vector<int8_t> q = {50, 10, -25, -5};
    b = q;
    dmaTool(b, true, 3, DmaTool::Normalize);
    std::vector<int8_t> whole = q;
    plugin::digiNormalize(whole);
    CHECK(b == whole);
    CHECK(std::abs(b[1]) < 127);
    b = q;
    dmaTool(b, true, 2, DmaTool::Normalize);
    CHECK(b[0] == 50);
    CHECK(b[2] == -25);
    CHECK(std::max(std::abs(int(b[1])), std::abs(int(b[3]))) >= 127);

    // Fade out per channel: each side fades over the frames as a mono sample would; one side alone leaves the other.
    b = {100, 100, 100, 100, 100, 100, 100, 100};
    dmaTool(b, true, 3, DmaTool::FadeOut);
    CHECK(b[0] == b[1]);
    CHECK(b[6] == b[7]);
    std::vector<int8_t> one = {100, 100, 100, 100};
    plugin::digiFadeOut(one);
    CHECK(b[2] == one[1]);
    CHECK(b[7] == one[3]);
    b = {100, 100, 100, 100};
    dmaTool(b, true, 1, DmaTool::FadeOut);
    CHECK(b[1] == 100);
    CHECK(b[3] == 100);
}

TEST_CASE("Gain steps in a row: a step down gives back the peaks a step up clipped (Indus, 26/10/2026)",
          "[dma][sample]") {
    const std::vector<int8_t> orig = {100, -100, 10, 0};
    GainRun run;
    std::vector<int8_t> b = orig;
    for (int i = 0; i < 2; ++i) {  // +3 dB twice: 100 clipped to 127
        b = run.step(0, b, 3);
        dmaGain(b, run.total());
        run.gave(b);
    }
    CHECK(b[0] == 127);
    CHECK(b[1] == -128);
    for (int i = 0; i < 2; ++i) {  // -3 dB twice: back to the bytes before the run
        b = run.step(0, b, -3);
        dmaGain(b, run.total());
        run.gave(b);
    }
    CHECK(b == orig);

    // Other bytes (another tool, an undo) or another key: a new run from them.
    b = run.step(0, std::vector<int8_t>{50}, 6.0206);
    CHECK(b == std::vector<int8_t>{50});
    CHECK(run.total() == Approx(6.0206));
    run.gave({100});
    CHECK(run.step(1, std::vector<int8_t>{100}, 3) == std::vector<int8_t>{100});
    CHECK(run.total() == Approx(3));
}

TEST_CASE("Sample part mirrored by Reverse", "[dma][sample]") {
    uint32_t start = 10, end = 30;
    ymengine::mirrorSpan(start, end, 100);
    CHECK(start == 70);
    CHECK(end == 90);
    start = 25;
    end = 0;  // End 0: the end of the sample
    ymengine::mirrorSpan(start, end, 100);
    CHECK(start == 0);
    CHECK(end == 75);
    start = 0;
    end = 0;  // the whole sample stays the whole sample
    ymengine::mirrorSpan(start, end, 100);
    CHECK(start == 0);
    CHECK(end == 0);
    start = 3;
    end = 0;
    ymengine::mirrorSpan(start, end, 0);  // empty
    CHECK(start == 3);
    CHECK(end == 0);
}

TEST_CASE("Resample: the sample keeps its length in time at the new rate, 32 KB at most", "[dma][sample]") {
    // Same rate: the bytes come back as they were.
    const std::vector<int8_t> mono = {0, 64, 127, -128, -64, 5};
    CHECK(plugin::dmaResample(mono, false, 25033, 25033) == mono);
    // Twice the rate: twice the frames, the old ones at the even places, the odd ones between them.
    const std::vector<int8_t> up = plugin::dmaResample(std::vector<int8_t>{0, 100, -100}, false, 12517, 25034);
    REQUIRE(up.size() == 6);
    CHECK(up[0] == 0);
    CHECK(up[1] == 50);
    CHECK(up[2] == 100);
    CHECK(up[3] == 0);
    CHECK(up[4] == -100);
    // Half the rate: every other frame, both channels of a stereo frame together.
    const std::vector<int8_t> st = {10, -10, 20, -20, 30, -30, 40, -40};
    CHECK(plugin::dmaResample(st, true, 50066, 25033) == std::vector<int8_t>{10, -10, 30, -30});
    // Eight times the rate: cut to 32 KB (16 KB per channel in stereo).
    const std::vector<int8_t> big(8192, 1);
    CHECK(plugin::dmaResample(big, false, 6258, 50066).size() == ymengine::kDmaMaxBytes);
    CHECK(plugin::dmaResample(big, true, 6258, 50066).size() == ymengine::kDmaMaxBytes);
    CHECK(plugin::dmaResample({}, false, 6258, 50066).empty());
}

TEST_CASE("DMA: named banks, read back identically, tolerant reading (Indus, 26/10/2026)", "[dma][banks]") {
    NonParamState st;
    st.dmaSamples[0] = {-128, 127, 0, -70, 5, 6};
    st.dmaNames[0] = "Break";
    st.dma.slots[0] = {3, true, 48, true, 1, 2, -25};
    st.dmaSamples[6] = {1};
    st.dma.slots[6].rate = 0;
    const DmaBank b = captureDmaBank("Breaks", st);
    CHECK(b.name == "Breaks");
    CHECK(b.dma == st.dma);
    CHECK(b.samples == st.dmaSamples);
    CHECK(b.names == st.dmaNames);

    const std::string text = dumpDmaBanks({b, captureDmaBank("Empty", NonParamState{})});
    const auto back = parseDmaBanks(text);
    REQUIRE(back);
    REQUIRE(back->size() == 2);
    CHECK((*back)[0] == b);
    CHECK((*back)[1].name == "Empty");
    CHECK((*back)[1].samples[0].empty());
    CHECK((*back)[1].dma == ymengine::DmaParams{});

    NonParamState other;
    applyDmaBank((*back)[0], other);
    CHECK(other.dma == st.dma);
    CHECK(other.dmaSamples == st.dmaSamples);
    CHECK(other.dmaNames == st.dmaNames);

    // Another library (the Digidrum one), broken JSON: refused.
    std::string error;
    CHECK_FALSE(parseDmaBanks(R"({"kind":"digidrumlib","banks":[]})", &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(parseDmaBanks("{", nullptr));
    // Rate brought to the nearest of the four, out-of-range values brought back, bank without a name dropped.
    const auto odd = parseDmaBanks(
        R"({"kind":"dmalib","version":1,"banks":[{"slots":[]},{"name":"  Odd  ","slots":[{"rate":13000,"root":200,)"
        R"("cents":-400,"stereo":1,"loop":1,"start":50,"end":20,"data":"/wA="},{"rate":99999},7]}]})");
    REQUIRE(odd);
    REQUIRE(odd->size() == 1);
    const DmaBank& o = odd->front();
    CHECK(o.name == "Odd");
    CHECK(o.dma.slots[0].rate == 1);
    CHECK(o.dma.slots[0].root == 127);
    CHECK(o.dma.slots[0].cents == -ymengine::kDmaCentsMax);
    CHECK(o.dma.slots[0].stereo);
    CHECK(o.dma.slots[0].loop);
    CHECK(o.dma.slots[0].end == 20);
    CHECK(o.dma.slots[0].start == 20);
    CHECK(o.samples[0] == std::vector<int8_t>{-1, 0});
    CHECK(o.dma.slots[1].rate == ymengine::kDmaRates - 1);
    CHECK(o.dma.slots[2] == ymengine::DmaSlot{});
}
