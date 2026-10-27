// STYM2149 — reading SNDH files against the SndhRenderer of AtariAudio 1.26: Ice decompression,
// header tags, then the sound, bit for bit, of the write capture replayed by ymcore. Reference tunes skipped (SKIP)
// if they are missing, unless STYM_REQUIRE_REFDATA is set (CI); the "square-sync" is checked on an SNDH built here.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Metrics.h"
#include "OracleCommon.h"
#include "RefData.h"
#include "SndhBuild.h"
#include "AtariMachine.h"
#include "SndhRenderer.h"
#include "external/ice_24.h"
#include "ymcore/ChipRenderer.h"
#include "ymio/Ice.h"
#include "ymio/Sndh.h"
#include "ymio/SndhWriter.h"

using namespace stym;
using namespace stym::test;

namespace {

bool needRefData() {
    if (!refDataDir().empty()) return true;
    if (refDataRequired()) FAIL("reference tunes missing although STYM_REQUIRE_REFDATA is set");
    SKIP("reference tunes missing: run tools/fetch_refdata.sh");
    return false;
}

std::string text(const char* s) { return s ? std::string(s) : std::string(); }

}  // namespace

TEST_CASE("SNDH: Ice and header like AtariAudio", "[sndh][oracle][refdata]") {
    if (!needRefData()) return;
    int packed = 0, mismatches = 0;
    for (const RefSndh& t : refSndh()) {
        INFO(t.file);
        std::vector<uint8_t> data = loadRefSndh(t.file);
        REQUIRE_FALSE(data.empty());
        const ymio::SndhParseResult ours = ymio::parseSndh(data);
        REQUIRE(ours.file);
        if (ymio::isIce(data)) {
            ++packed;
            const long size = ice_24_origsize(data.data());
            std::vector<uint8_t> ref(static_cast<size_t>(size));
            REQUIRE(ice_24_depack(data.data(), ref.data()) == size);
            const bool same = ours.file->image == ref;
            CHECK(same);
            mismatches += !same;
        }
        SndhRenderer* aa = SndhRenderer::Create(data.data(), uint32_t(data.size()), 44100);
        REQUIRE(aa);
        const auto& si = static_cast<const AtariAudioRenderer*>(aa)->GetSongInfo();  // SndhRenderer does not define it
        const ymio::SndhInfo& info = ours.file->info;
        CHECK(info.title == text(si.musicName));
        CHECK(info.composer == text(si.musicAuthor));
        CHECK(info.ripper == text(si.ripper));
        CHECK(info.converter == text(si.converter));
        CHECK(info.year == text(si.year));
        CHECK(info.subsongs == si.subsongCount);
        CHECK(info.defaultSubsong == si.defaultSubsong);
        CHECK(info.tickRate == si.playerTickRate);
        AtariAudioRenderer::Destroy(aa);
    }
    CHECK(packed > 0);
    metric("SNDH-ICE", "bounded Ice decompression against AtariAudio's ice_24, " + std::to_string(packed) + " packed SNDH files",
           std::to_string(mismatches) + " deviation(s)", "0", mismatches == 0);
}

// The capture runs at the output rate: its writes fall on the same samples as in AtariAudio, and ymcore (RawBox,
// AtariAudio LFSR, dcAdjust) replays the chip bit for bit; the STE DAC is added the same way.
TEST_CASE("SNDH: capture replayed by ymcore, bit-exact against SndhRenderer", "[sndh][oracle][refdata]") {
    if (!needRefData()) return;
    constexpr uint32_t kRate = 44100;
    constexpr double kSeconds = 20.0;
    const size_t n = size_t(kSeconds * kRate);
    int tunes = 0;
    uint64_t badSamples = 0, samples = 0;
    for (const RefSndh& t : refSndh()) {
        INFO(t.file);
        const std::vector<uint8_t> data = loadRefSndh(t.file);
        REQUIRE_FALSE(data.empty());
        ymio::SndhParseResult parsed = ymio::parseSndh(data);
        REQUIRE(parsed.file);
        auto file = std::make_shared<const ymio::SndhFile>(std::move(*parsed.file));
        const int subsong = file->info.defaultSubsong;
        // Our capture starts its machine (one draw of AtariAudio's global rand()) before the oracle's.
        ymio::SndhStream::Options so;
        so.rate = kRate;
        auto stream = std::make_shared<ymio::SndhStream>(file, subsong, so);
        REQUIRE(stream->waitFor(1, 20000));
        (void)takeAtariAudioSeed();
        const uint32_t seed = takeAtariAudioSeed();
        SndhRenderer* aa = SndhRenderer::Create(data.data(), uint32_t(data.size()), kRate);
        REQUIRE(aa);
        REQUIRE(aa->InitSubSong(subsong));
        std::vector<int16_t> ref(n);
        aa->AudioRender(ref.data(), uint32_t(n));
        AtariAudioRenderer::Destroy(aa);

        ymcore::RenderConfig rc;
        rc.hostRate = kRate;
        rc.mode = ymcore::OutputMode::RawBox;
        rc.dc = ymcore::DcFilter::MovingAverage;
        rc.chip.clock = ymcore::clocks::kDefault;
        rc.chip.noise = ymcore::NoiseLfsr::AtariAudio;
        rc.chip.edgeSeed = seed;
        ymcore::ChipRenderer renderer;
        renderer.prepare(rc);
        ymio::SndhPlayer player(stream);
        std::vector<float> got(n);
        for (size_t done = 0; done < n;) {
            const uint32_t k = uint32_t(std::min<size_t>(1000, n - done));
            player.render(renderer, got.data() + done, k, true);
            done += k;
        }
        CHECK(player.lostCycles() == 0);
        CHECK_FALSE(stream->failed());
        uint64_t bad = 0;
        size_t first = n;
        for (size_t i = 0; i < n; ++i) {
            const long v = std::clamp(std::lround(double(got[i]) * 32768.0), -32768L, 32767L);
            if (v != ref[i]) {
                ++bad;
                first = std::min(first, i);
            }
        }
        INFO("first deviation at sample " << first);
        CHECK(bad == 0);
        CHECK(player.frames() > 0);
        badSamples += bad;
        samples += n;
        ++tunes;
    }
    metric("SNDH-S2", "SNDH capture replayed by ymcore against SndhRenderer, " + std::to_string(tunes) +
                          " tunes of " + std::to_string(int(kSeconds)) + " s at 44.1 kHz",
           std::to_string(badSamples) + " differing sample(s) out of " + std::to_string(samples), "0",
           badSamples == 0);
}

namespace {

// ymcore render of the capture, set up like AtariAudio (edge seed: that of the oracle's machine).
std::vector<int16_t> renderCapture(const std::vector<ymio::SndhEvent>& events, uint32_t rate, uint32_t seed, size_t n,
                                   bool edgeResets) {
    ymcore::RenderConfig rc;
    rc.hostRate = rate;
    rc.mode = ymcore::OutputMode::RawBox;
    rc.dc = ymcore::DcFilter::MovingAverage;
    rc.chip.clock = ymcore::clocks::kDefault;
    rc.chip.noise = ymcore::NoiseLfsr::AtariAudio;
    rc.chip.edgeSeed = seed;
    ymcore::ChipRenderer r;
    r.prepare(rc);
    std::vector<int16_t> out(n);
    size_t e = 0;
    for (size_t i = 0; i < n; ++i) {
        for (; e < events.size() && events[e].cycle < r.cycleAtSample(i + 1); ++e) {
            const ymio::SndhEvent& x = events[e];
            if (x.kind != ymio::SndhEvent::Kind::Write) continue;
            if (!edgeResets && x.reg == ymcore::Ym2149::kToneEdgeReset) continue;
            r.write(x.cycle, x.reg, x.value);
        }
        float v = 0.0f;
        r.render(&v, 1);
        out[i] = int16_t(std::clamp(std::lround(double(v) * 32768.0), -32768L, 32767L));
    }
    return out;
}

}  // namespace

// Period 0 or 1 written in a timer interrupt: AtariAudio resets the voice's edge on exit (Ym2149c::InsideTimerIrq).
// Timer A at 3 072 Hz (2 457 600 / 4 / 200); voices A and B go through a period of 0 or 1 at each interrupt, voice C
// does not.
TEST_CASE("SNDH: square-sync of timer interrupts, bit-exact against SndhRenderer", "[sndh][oracle]") {
    SndhSource src;
    ymWrite(src.init, 7, 0x38);  // tones A, B, C
    ymWrite(src.init, 8, 15);
    ymWrite(src.init, 9, 12);
    ymWrite(src.init, 10, 10);
    ymWrite(src.init, 0, 0);
    ymWrite(src.init, 1, 1);     // A: 256
    ymWrite(src.init, 2, 0x40);  // B: 64
    ymWrite(src.init, 4, 0x55);  // C: 85
    ymWrite(src.timerA, 1, 0);   // A: 0 (edge to reset)
    ymWrite(src.timerA, 1, 1);   // A: 256
    ymWrite(src.timerA, 2, 1);   // B: 1 (edge to reset)
    ymWrite(src.timerA, 2, 0x40);
    ymWrite(src.timerA, 5, 0);   // C: 85, unchanged
    // play: R8 = 8 + (counter & 7), so that there are writes at the ticks too.
    src.play = {0x5238, 0x1000, 0x1038, 0x1000, 0x0200, 0x0007, 0x5000, 0x13c0, 0x00ff, 0x8802};
    src.play.insert(src.play.begin(), {0x11fc, 0x0008, 0x8800});
    const std::vector<uint8_t> data = buildSndh(src);
    ymio::SndhParseResult parsed = ymio::parseSndh(data);
    REQUIRE(parsed.file);

    constexpr uint32_t kRate = 44100;
    const size_t n = kRate * 2;
    ymio::SndhCapture capture(*parsed.file, 1, kRate);
    REQUIRE(capture.ok());
    std::vector<ymio::SndhEvent> events;
    REQUIRE(capture.run(n + 1, events));
    size_t resets = 0, ticks = 0;
    for (const auto& e : events) {
        resets += e.kind == ymio::SndhEvent::Kind::Write && e.reg == ymcore::Ym2149::kToneEdgeReset;
        ticks += e.kind == ymio::SndhEvent::Kind::Tick;
    }
    // Two voices reset per interrupt, 3 072 interrupts per second.
    CHECK(resets >= 2 * 3000 * 2);
    CHECK(ticks == 101);  // 50 Hz for 2 s, plus the first tick
    for (size_t i = 1; i < events.size(); ++i) REQUIRE(events[i - 1].cycle <= events[i].cycle);

    (void)takeAtariAudioSeed();  // machine of the capture
    const uint32_t seed = takeAtariAudioSeed();
    SndhRenderer* aa = SndhRenderer::Create(data.data(), uint32_t(data.size()), kRate);
    REQUIRE(aa);
    REQUIRE(aa->InitSubSong(1));
    std::vector<int16_t> ref(n);
    aa->AudioRender(ref.data(), uint32_t(n));
    AtariAudioRenderer::Destroy(aa);

    const std::vector<int16_t> got = renderCapture(events, kRate, seed, n, true);
    size_t bad = 0;
    for (size_t i = 0; i < n; ++i) bad += got[i] != ref[i];
    CHECK(bad == 0);
    // Without the resets, the render deviates: the test does see the "square-sync".
    const std::vector<int16_t> without = renderCapture(events, kRate, seed, n, false);
    size_t differ = 0;
    for (size_t i = 0; i < n; ++i) differ += without[i] != ref[i];
    CHECK(differ > n / 10);
    metric("SNDH-SYNC", "square-sync in timer interrupt (built SNDH, 2 s at 44.1 kHz) against SndhRenderer",
           std::to_string(bad) + " differing sample(s), " + std::to_string(resets) + " edge resets", "0",
           bad == 0);
}

namespace {

uint32_t read32(AtariMachine& m, uint32_t a) { return uint32_t(m.memRead16(a)) << 16 | m.memRead16(a + 2); }

uint8_t ymRead(AtariMachine& m, uint8_t reg) {
    m.memWrite8(0xff8800, reg);
    return uint8_t(m.memRead8(0xff8800));
}

}  // namespace

// The init and exit of an exported SNDH on the AtariAudio machine, MFP set beforehand as by the system:
// out-of-range subsong brought back to the first, MFP registers saved at the first init only, then restored by the
// exit (bits of timers A, B and D), vectors restored, sound cut.
TEST_CASE("Exported SNDH: init (out-of-range subsong) and exit (MFP restored, sound cut)", "[sndh][oracle]") {
    // Subsong 1: SID on channel A, Sync Buzzer on B, Digidrum on C; 2: registers only.
    std::vector<ymengine::RegisterFrame> fx(8), plain(8);
    for (auto& f : fx) {
        f.reg = {0x40, 0, 0x80, 0, 0, 0, 0, 0x3E, 12, 0x10, 5, 0x20, 0, 0x0A};
        f.fx[0] = {true, ymengine::Generator::Sid, 12, {4, 49}, &f == &fx[0]};
        f.fx[1] = {true, ymengine::Generator::SyncBuzzer, 10, {5, 40}, &f == &fx[0]};
        f.fx[2] = {true, ymengine::Generator::Digidrum, 0, {1, 77}, &f == &fx[0]};
    }
    for (auto& f : plain) f.reg = {0x40, 0, 0x80, 0, 0, 0, 0, 0x38, 12, 11, 10, 0, 0, 0};
    fx[0].r13 = true;
    std::vector<uint8_t> drum(4000);  // 0.5 s at 7 979 Hz: still playing at the end of the 4 frames
    for (size_t i = 0; i < drum.size(); ++i) drum[i] = uint8_t(i % 16);
    const ymio::SndhSubsong subs[2] = {{fx, {}}, {plain, {}}};
    ymio::SndhWriteInfo info;
    info.digidrums[0] = drum;
    const ymio::SndhWriteResult w = ymio::sndhFile(subs, info);
    INFO(w.error);
    REQUIRE(!w.data.empty());

    struct Run {
        uint32_t init, tacr, tbcr, tcdcr;
    };
    for (const Run r : {Run{9, 4, 5, 0x51}, Run{0, 4, 5, 0x51}, Run{2, 0, 0, 0x50}}) {
        INFO("init " << r.init);
        auto m = std::make_unique<AtariMachine>();
        m->Startup(250000);
        // The system: timer B enabled but stopped, timer C running, vectors on an RTE.
        const uint8_t vec[4] = {0x00, 0x00, 0x05, 0x00};
        for (uint32_t a : {0x134u, 0x120u, 0x110u}) REQUIRE(m->Upload(vec, a, 4));
        m->memWrite8(0xfffa07, 0x81);
        m->memWrite8(0xfffa13, 0x80);
        m->memWrite8(0xfffa09, 0x60);
        m->memWrite8(0xfffa15, 0x20);
        m->memWrite8(0xfffa1d, 0x50);
        REQUIRE(m->Upload(w.data.data(), ymio::kSndhLoadAddress, uint32_t(w.data.size())));
        REQUIRE(m->Jsr(ymio::kSndhLoadAddress, r.init));
        for (int t = 0; t < 4; ++t) {
            REQUIRE(m->PlayerTick(ymio::kSndhLoadAddress + 8));
            for (int i = 0; i < 5000; ++i) (void)m->ComputeNextSample();
        }
        // Effect timers: prescalers of subsong 1; timer C keeps its own.
        CHECK((m->memRead8(0xfffa19) & 0x0F) == r.tacr);
        CHECK((m->memRead8(0xfffa1b) & 0x0F) == r.tbcr);
        CHECK((m->memRead8(0xfffa1d) & 0x77) == r.tcdcr);
        CHECK((m->memRead8(0xfffa07) & 0x21) == 0x21);
        CHECK((m->memRead8(0xfffa09) & 0x70) == 0x70);
        CHECK(ymRead(*m, 8) + ymRead(*m, 9) + ymRead(*m, 10) > 0);
        if (r.init == 2) {
            // Second init without exit: nothing new is saved, the exit restores the state from before the first.
            REQUIRE(m->Jsr(ymio::kSndhLoadAddress, 1));
            REQUIRE(m->PlayerTick(ymio::kSndhLoadAddress + 8));
            CHECK((m->memRead8(0xfffa19) & 0x0F) == 4);
        }
        REQUIRE(m->Jsr(ymio::kSndhLoadAddress + 4, 0));
        CHECK(m->memRead8(0xfffa07) == 0x81);
        CHECK(m->memRead8(0xfffa13) == 0x80);
        CHECK(m->memRead8(0xfffa09) == 0x60);
        CHECK(m->memRead8(0xfffa15) == 0x20);
        CHECK(m->memRead8(0xfffa19) == 0);
        CHECK(m->memRead8(0xfffa1b) == 0);
        CHECK(m->memRead8(0xfffa1d) == 0x50);
        for (uint32_t a : {0x134u, 0x120u, 0x110u}) CHECK(read32(*m, a) == 0x500);
        CHECK(ymRead(*m, 8) == 0);
        CHECK(ymRead(*m, 9) == 0);
        CHECK(ymRead(*m, 10) == 0);
        CHECK((ymRead(*m, 7) & 0x3F) == 0x3F);
        // Nothing plays any more: the timer interrupts no longer touch the chip.
        for (int i = 0; i < 5000; ++i) (void)m->ComputeNextSample();
        CHECK(ymRead(*m, 8) + ymRead(*m, 9) + ymRead(*m, 10) == 0);
    }
}
