// STYM2149 — reading SNDH files: header, refusals, timed capture, stream, resume, damaged data
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "RefData.h"
#include "SndhBuild.h"
#include "ymio/Ice.h"
#include "ymio/Sndh.h"

using namespace stym;
using namespace stym::test;

namespace {

// play: R8 = counter at $1000, incremented at each tick (1, 2, 3...).
SndhSource counterSource() {
    SndhSource s;
    s.play = {0x5238, 0x1000};  // addq.b #1,$1000.w
    moveByte(s.play, 8, 0x8800);
    s.play.insert(s.play.end(), {0x11f8, 0x1000, 0x8802});  // move.b $1000.w,$ff8802.w
    return s;
}

// Timer A that writes R9 and R0: writes timed between the ticks.
SndhSource timerSource() {
    SndhSource s = counterSource();
    ymWrite(s.init, 7, 0x3e);
    ymWrite(s.timerA, 9, 7);
    s.timerA.insert(s.timerA.end(), {0x5238, 0x1001});  // addq.b #1,$1001.w
    moveByte(s.timerA, 0, 0x8800);
    s.timerA.insert(s.timerA.end(), {0x11f8, 0x1001, 0x8802});  // R0 = timer counter
    return s;
}

std::shared_ptr<const ymio::SndhFile> parsed(const SndhSource& s) {
    ymio::SndhParseResult r = ymio::parseSndh(buildSndh(s));
    REQUIRE(r.file);
    return std::make_shared<const ymio::SndhFile>(std::move(*r.file));
}

std::vector<ymio::SndhEvent> drain(ymio::SndhStream& stream, uint64_t end) {
    REQUIRE(stream.waitFor(end, 20000));
    std::vector<ymio::SndhEvent> events;
    stream.consume(end, [&](const ymio::SndhEvent& e) { events.push_back(e); });
    return events;
}

bool same(const ymio::SndhEvent& a, const ymio::SndhEvent& b) {
    return a.cycle == b.cycle && a.kind == b.kind && a.reg == b.reg && a.value == b.value && a.dac == b.dac;
}

}  // namespace

TEST_CASE("SNDH: header of a built SNDH", "[sndh]") {
    SndhSource s = counterSource();
    s.title = "Square";
    s.subsongs = 3;
    s.tickRate = 200;
    const std::vector<uint8_t> data = buildSndh(s);
    CHECK(ymio::looksLikeSndh(data));
    const ymio::SndhParseResult r = ymio::parseSndh(data);
    REQUIRE(r.file);
    CHECK_FALSE(r.file->packed);
    CHECK(r.file->image == data);
    const ymio::SndhInfo& i = r.file->info;
    CHECK(i.title == "Square");
    CHECK(i.subsongs == 3);
    CHECK(i.defaultSubsong == 1);
    CHECK(i.tickRate == 200);
    CHECK(i.timer == 'C');
    CHECK(i.frames == std::vector<uint32_t>(3, 0));
    // Zero rate (broken tag): 50 Hz.
    s.tickRate = 0;
    const ymio::SndhParseResult z = ymio::parseSndh(buildSndh(s));
    REQUIRE(z.file);
    CHECK(z.file->info.tickRate == 50);
}

TEST_CASE("SNDH: refused files", "[sndh]") {
    CHECK(ymio::parseSndh({}).error == "Not an SNDH file.");
    const std::vector<uint8_t> text(64, 'x');
    CHECK_FALSE(ymio::looksLikeSndh(text));
    CHECK(ymio::parseSndh(text).error == "Not an SNDH file.");
    // Ice whose stream is empty or whose sizes are wrong.
    std::vector<uint8_t> ice = {'I', 'C', 'E', '!', 0, 0, 0, 20, 0, 0, 1, 0};
    ice.resize(20, 0);
    CHECK(ymio::looksLikeSndh(ice));
    CHECK(ymio::parseSndh(ice).error == "The packed data (Ice) is damaged.");
    CHECK_FALSE(ymio::iceDepack(ice, 1u << 20));
    ice[7] = 200;  // packed size beyond the data
    CHECK_FALSE(ymio::iceDepack(ice, 1u << 20));
    // Header without a tag: HDNS straight away.
    std::vector<uint8_t> bare = buildSndh(SndhSource{});
    const std::vector<uint8_t> hdns = {'H', 'D', 'N', 'S'};
    std::copy(hdns.begin(), hdns.end(), bare.begin() + 16);
    CHECK(ymio::parseSndh(bare).error == "The SNDH header has no tag.");
    // Too large for the memory of the machine (4 MB, load address included).
    std::vector<uint8_t> big = buildSndh(SndhSource{});
    big.resize(ymio::kSndhMaxSize, 0);
    CHECK(ymio::parseSndh(big).error == "The SNDH file is too large for the Atari memory.");
}

// At 250 kHz (one internal step of the chip per sample), the play is called every 4 994 samples (AtariAudio's
// ComputeHostSamplePerTick: 250 000 × 313 × 512 / 8 021 247) and its writes land at its tick.
TEST_CASE("SNDH: writes of the play timed at their tick", "[sndh]") {
    const auto file = parsed(counterSource());
    ymio::SndhCapture capture(*file, 1);
    REQUIRE(capture.ok());
    CHECK(capture.samplesPerTick() == 4994);
    CHECK(capture.cycleOf(4994) == 8 * 4994);
    std::vector<ymio::SndhEvent> events;
    REQUIRE(capture.run(250000, events));
    CHECK(capture.ticks() == 51);
    CHECK_FALSE(capture.dacUsed());
    CHECK(capture.droppedEvents() == 0);
    std::vector<ymio::SndhEvent> writes;
    size_t ticks = 0;
    for (const auto& e : events) {
        if (e.kind == ymio::SndhEvent::Kind::Tick) {
            CHECK(e.cycle == 8ull * 4994 * ticks);
            ++ticks;
        } else {
            REQUIRE(e.kind == ymio::SndhEvent::Kind::Write);
            writes.push_back(e);
        }
    }
    CHECK(ticks == 51);
    REQUIRE(writes.size() == 51);
    for (size_t k = 0; k < writes.size(); ++k) {
        CHECK(writes[k].cycle == 8ull * 4994 * k);
        CHECK(writes[k].reg == 8);
        CHECK(writes[k].value == k + 1);
    }
    // Subsong not in the list: nothing starts.
    ymio::SndhCapture none(*file, 2);
    CHECK_FALSE(none.ok());
}

TEST_CASE("SNDH: writes of a timer interrupt timed at the next sample", "[sndh]") {
    const auto file = parsed(timerSource());
    ymio::SndhCapture capture(*file, 1, 44100);
    std::vector<ymio::SndhEvent> events;
    REQUIRE(capture.run(44100, events));
    size_t timerWrites = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        if (i > 0) REQUIRE(events[i - 1].cycle <= events[i].cycle);
        const ymio::SndhEvent& e = events[i];
        if (e.kind != ymio::SndhEvent::Kind::Write || e.reg != 0) continue;
        ++timerWrites;
        // Time of a sample start, after the first one (the timer only runs after the init).
        const uint64_t k = e.cycle / 8 * 44100 / 250000;
        const uint64_t s = capture.cycleOf(k) == e.cycle ? k : k + 1;
        CHECK(s >= 1);
        CHECK(capture.cycleOf(s) == e.cycle);
    }
    // 2 457 600 / 4 / 200 = 3 072 interrupts per second.
    CHECK(timerWrites >= 3071);
    CHECK(timerWrites <= 3072);
}

TEST_CASE("SNDH: a crashed routine stops the capture", "[sndh]") {
    SndhSource crash = counterSource();
    crash.play.insert(crash.play.begin(), 0x4afc);  // illegal
    const auto file = parsed(crash);
    ymio::SndhCapture capture(*file, 1);
    std::vector<ymio::SndhEvent> events;
    CHECK_FALSE(capture.run(250000, events));
    CHECK_FALSE(capture.ok());
    CHECK(capture.ticks() == 0);

    SndhSource badInit = counterSource();
    badInit.init.push_back(0x4afc);
    ymio::SndhCapture none(*parsed(badInit), 1);
    CHECK_FALSE(none.ok());

    // The stream reports it, and no longer makes the reader wait.
    ymio::SndhStream stream(file, 1);
    REQUIRE(stream.waitFor(UINT64_MAX, 20000));
    CHECK(stream.failed());
    CHECK(stream.consume(1ull << 40, [](const ymio::SndhEvent&) {}));
}

// Each Musashi instance has its own state: two streams of the same tune, at the same time, capture the same thing.
TEST_CASE("SNDH: two simultaneous streams capture the same events", "[sndh]") {
    const auto file = parsed(timerSource());
    ymio::SndhStream::Options o;
    o.rate = 44100;
    o.lookahead = 4.0;
    ymio::SndhStream a(file, 1, o), b(file, 1, o);
    constexpr uint64_t kEnd = 2000000 * 3;  // 3 s
    const std::vector<ymio::SndhEvent> ea = drain(a, kEnd), eb = drain(b, kEnd);
    REQUIRE(ea.size() == eb.size());
    CHECK(ea.size() > 9000);
    CHECK(std::equal(ea.begin(), ea.end(), eb.begin(), same));
    CHECK_FALSE(a.failed());
}

// Change of the output settings: playback resumes on a new chip without replaying or skipping any write.
TEST_CASE("SNDH: resume on another renderer", "[sndh]") {
    const auto file = parsed(timerSource());
    ymcore::RenderConfig rc;
    rc.hostRate = 48000;
    rc.mode = ymcore::OutputMode::RawBox;
    ymcore::ChipRenderer first, second, straight;
    first.prepare(rc);
    second.prepare(rc);
    straight.prepare(rc);
    ymio::SndhPlayer moved(std::make_shared<ymio::SndhStream>(file, 1));
    ymio::SndhPlayer reference(std::make_shared<ymio::SndhStream>(file, 1));
    std::vector<float> buf(24000);
    moved.render(first, buf.data(), 24000, true);
    // The second renderer has already run: it does not start at cycle 0.
    std::vector<float> idle(1000);
    second.render(idle.data(), 1000);
    moved.moveTo(first, second);
    for (unsigned r = 0; r < 14; ++r) {
        second.flushDue();
        CHECK(second.chip().readRegister(r) == first.chip().readRegister(r));
    }
    moved.render(second, buf.data(), 24000, true);
    reference.render(straight, buf.data(), 24000, true);
    reference.render(straight, buf.data(), 24000, true);
    second.flushDue();
    straight.flushDue();
    for (unsigned r = 0; r < 14; ++r) CHECK(second.chip().readRegister(r) == straight.chip().readRegister(r));
    CHECK(moved.frames() == reference.frames());
    CHECK(moved.lostCycles() == 0);
}

// Bytes changed at random, cut files: reading refuses or reads, without access outside the data.
TEST_CASE("SNDH: damaged data", "[sndh]") {
    std::vector<std::vector<uint8_t>> seeds = {buildSndh(timerSource())};
    if (!refDataDir().empty())
        for (const RefSndh& t : refSndh())
            if (auto d = loadRefSndh(t.file); !d.empty()) seeds.push_back(std::move(d));
    std::mt19937 rng(2149);
    int parsedOk = 0, refused = 0;
    for (const std::vector<uint8_t>& seed : seeds) {
        for (int round = 0; round < 60; ++round) {
            std::vector<uint8_t> d = seed;
            if (round % 3 == 0) {
                d.resize(std::uniform_int_distribution<size_t>(0, d.size())(rng));
            } else {
                const int flips = std::uniform_int_distribution<int>(1, 16)(rng);
                for (int f = 0; f < flips && !d.empty(); ++f) {
                    // Mostly in the header (tags, Ice sizes) and at the end (Ice stream read backwards).
                    const size_t span = std::min<size_t>(d.size(), 256);
                    size_t at = std::uniform_int_distribution<size_t>(0, span - 1)(rng);
                    if (round % 2) at = d.size() - 1 - at;
                    d[at] = uint8_t(rng());
                }
            }
            const ymio::SndhParseResult r = ymio::parseSndh(d);
            if (r.file) {
                ++parsedOk;
                CHECK(r.file->info.subsongs >= 1);
                CHECK(r.file->info.subsongs <= ymio::kSndhMaxSubsongs);
                CHECK(r.file->info.tickRate >= 1);
                CHECK(r.file->info.tickRate <= 1000);
                CHECK(r.file->image.size() <= ymio::kSndhMaxSize);
            } else {
                ++refused;
                CHECK_FALSE(r.error.empty());
            }
            if (ymio::isIce(d)) (void)ymio::iceDepack(d, ymio::kSndhMaxSize);
        }
    }
    CHECK(parsedOk > 0);
    CHECK(refused > 0);
}
