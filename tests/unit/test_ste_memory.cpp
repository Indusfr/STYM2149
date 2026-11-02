// STYM2149 — memory of the Atari for the samples (Indus, 29/10/2026): count and budget, samples past it silent
// in the 3-channel modes and kept in 32-channel mode, through the driver; STEM chunk, Digidrum of each drum row and
// DMA samples past the eighth in the state and the Patches.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "plugincore/Driver.h"
#include "plugincore/Mapping.h"
#include "plugincore/Parameters.h"
#include "plugincore/Patches.h"
#include "plugincore/State.h"
#include "plugincore/SteMemory.h"

using namespace stym;
using namespace stym::plugin;
using ymengine::VoiceMode;

namespace {

bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}

constexpr uint32_t kKb = 1024;

}  // namespace

TEST_CASE("STE memory: budget of each machine, the reserve kept", "[stemem]") {
    CHECK(steSampleBudget(0) == 512 * kKb - kSteReserve);
    CHECK(steSampleBudget(1) == 1024 * kKb - kSteReserve);
    CHECK(steSampleBudget(2) == 2048 * kKb - kSteReserve);
    CHECK(steSampleBudget(3) == 4096 * kKb - kSteReserve);
    CHECK(steSampleBudget(200) == steSampleBudget(3));  // out of range: the largest
    CHECK(NonParamState{}.steRam == 3);                  // 4 MB by default
}

TEST_CASE("STE memory: Digidrums then DMA samples by number, those past the first that does not fit silent",
          "[stemem]") {
    NonParamState s;
    s.steRam = 0;  // 384 KB for the samples
    s.digidrums[0] = std::vector<uint8_t>(100 * kKb, 1);
    s.digidrums[40] = std::vector<uint8_t>(100 * kKb, 2);
    s.dmaSamples[0] = std::vector<int8_t>(150 * kKb, 3);
    s.dmaSamples[1] = std::vector<int8_t>(10 * kKb, 4);  // would fit, but after one that does not
    s.dmaSamples[9] = std::vector<int8_t>(kKb, 5);

    // STE, Authentic 3ch: 100 + 100 + 150 + 10 + 1 KB, all within the 384 KB.
    SteMemory m = steMemory(s, VoiceMode::Authentic3, true);
    CHECK(m.limited);
    CHECK(m.budget == 384 * kKb);
    CHECK(m.total == 361 * kKb);
    CHECK(m.used == 361 * kKb);
    CHECK_FALSE(m.over());
    CHECK(m.digiMuted.none());
    CHECK(m.dmaMuted.none());

    // A larger DMA sample 0: it does not fit, nor those after it, the small ones included.
    s.dmaSamples[0] = std::vector<int8_t>(200 * kKb, 3);
    m = steMemory(s, VoiceMode::Authentic3, true);
    CHECK(m.over());
    CHECK(m.used == 200 * kKb);
    CHECK(m.total == 411 * kKb);
    CHECK(m.digiMuted.none());
    CHECK(m.dmaMuted.count() == 3);
    CHECK(m.dmaMuted[0]);
    CHECK(m.dmaMuted[1]);
    CHECK(m.dmaMuted[9]);

    // The ST does not play the DMA samples: they do not count.
    m = steMemory(s, VoiceMode::Dual3, false);
    CHECK_FALSE(m.over());
    CHECK(m.total == 200 * kKb);
    CHECK(m.dmaMuted.none());

    // 2x3ch, two Ataris: twice the memory, everything heard on the STE.
    m = steMemory(s, VoiceMode::Dual3, true);
    CHECK(m.limited);
    CHECK(m.budget == 2 * 384 * kKb);
    CHECK_FALSE(m.over());
    CHECK(m.used == 411 * kKb);
    CHECK(m.dmaMuted.none());

    // 32-channel mode: no limit.
    m = steMemory(s, VoiceMode::Studio16, true);
    CHECK_FALSE(m.limited);
    CHECK_FALSE(m.over());
    CHECK(m.used == m.total);
    CHECK(m.dmaMuted.none());

    // A Digidrum past the budget: it and every sample after it silent.
    s.digidrums[40] = std::vector<uint8_t>(300 * kKb, 2);
    m = steMemory(s, VoiceMode::Authentic3, true);
    CHECK(m.digiMuted.count() == 1);
    CHECK(m.digiMuted[40]);
    CHECK(m.dmaMuted.count() == 3);
    // More memory: everything heard again.
    s.steRam = 1;
    m = steMemory(s, VoiceMode::Authentic3, true);
    CHECK_FALSE(m.over());
    CHECK(m.digiMuted.none());
    CHECK(m.dmaMuted.none());
}

TEST_CASE("STE memory: the driver silences the samples past the memory, and gives them back",
          "[stemem][driver]") {
    Snapshot snap;
    snap.state.steRam = 0;
    snap.state.digidrums[0] = std::vector<uint8_t>(300 * kKb, 7);
    snap.state.digidrums[100] = std::vector<uint8_t>(10 * kKb, 7);
    snap.state.dmaSamples[0] = std::vector<int8_t>(100 * kKb, 50);  // 410 KB in all on the STE
    Driver d;
    ParamValues v = defaultValues();
    v[size_t(paramIndex(Global::Computer))] = 1;  // STE
    v[size_t(paramIndex(Global::VoiceMode))] = float(int(VoiceMode::Authentic3));
    REQUIRE(voiceMode(v) == VoiceMode::Authentic3);
    d.prepare({48000, 0}, v, snap);
    std::vector<float> left(256), right(256);
    auto run = [&] {
        ymengine::Outputs o;
        o.left = left.data();
        o.right = right.data();
        d.process(v, {}, o, 256, false, nullptr);
    };
    run();
    const ymengine::Engine& e = d.engine();
    CHECK(e.userSample(0).size() == 300 * kKb);
    CHECK(e.userSample(100).size() == 10 * kKb);
    CHECK(e.dmaSample(0).empty());  // past the 384 KB
    // The engine reads the bytes the copies of the state share, without a copy of its own.
    CHECK(e.userSample(0).data() == snap.state.digidrums[0].data());

    // 1 MB: heard.
    snap.state.steRam = 1;
    d.publish(snap);
    run();
    CHECK(d.engine().dmaSample(0).size() == 100 * kKb);

    // 32-channel mode with 512 KB: no limit.
    snap.state.steRam = 0;
    d.publish(snap);
    v[size_t(paramIndex(Global::VoiceMode))] = float(int(VoiceMode::Studio16));
    for (int k = 0; k < 8; ++k) run();
    CHECK(d.engine().dmaSample(0).size() == 100 * kKb);
    // Back to Authentic 3ch: silent again.
    v[size_t(paramIndex(Global::VoiceMode))] = float(int(VoiceMode::Authentic3));
    for (int k = 0; k < 8; ++k) run();
    CHECK(d.engine().dmaSample(0).empty());
    // The ST machine does not count the DMA samples: the Digidrums all fit.
    v[size_t(paramIndex(Global::Computer))] = 0;
    for (int k = 0; k < 8; ++k) run();
    CHECK(d.engine().userSample(100).size() == 10 * kKb);
}

TEST_CASE("STE memory: STEM chunk, Digidrum of each drum row and wide DMA sample in TRKX and the Patches",
          "[stemem][state]") {
    PluginState def;
    const std::vector<uint8_t> a = encodeState(def);
    CHECK_FALSE(hasChunk(a, "STEM"));
    CHECK(decodeState(a).state.extras.steRam == kSteRamDefault);

    PluginState st;
    st.extras.steRam = 1;
    st.extras.tracks[3].patch.digiPads = {9, 1, 2, 3, 4, 5, 6, 126};
    st.extras.tracks[5].patch.dmaSlot = 120;
    st.extras.digidrums[126] = std::vector<uint8_t>{1, 2, 3};
    const std::vector<uint8_t> b = encodeState(st);
    CHECK(hasChunk(b, "STEM"));
    const DecodeResult back = decodeState(b);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.steRam == 1);
    CHECK(back.state.extras.tracks[3].patch.digiPads == st.extras.tracks[3].patch.digiPads);
    CHECK(back.state.extras.tracks[4].patch.digiPads == ymengine::TrackPatch{}.digiPads);
    CHECK(back.state.extras.tracks[5].patch.dmaSlot == 120);
    CHECK(back.state.extras.digidrums[126] == std::vector<uint8_t>{1, 2, 3});
    CHECK(encodeState(back.state) == b);

    // Patches: "digiPads" only when not the default, read back on another track.
    const ParamValues v = defaultValues();
    CHECK(dumpPatchLibrary({snapPatch("x", v, def.extras, 3)}).find("digiPads") == std::string::npos);
    const std::string text = dumpPatchLibrary({snapPatch("x", v, st.extras, 3)});
    CHECK(text.find("\"digiPads\"") != std::string::npos);
    const auto lib = parsePatchLibrary(text);
    REQUIRE(lib);
    REQUIRE(lib->size() == 1);
    CHECK(patchChange((*lib)[0], NonParamState{}, 7).track.patch.digiPads == st.extras.tracks[3].patch.digiPads);
}
