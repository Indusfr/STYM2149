// STYM2149 — plugincore: WAV and stem exports (M7, A4, D-163, D-164, D-171)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "plugincore/AudioExport.h"
#include "plugincore/Mapping.h"
#include "ymio/Zip.h"

using namespace stym;
using namespace stym::plugin;
using ymio::WavFormat;

namespace {

float& param(ParamValues& v, int track, Track f) { return v[size_t(paramIndex(track, f))]; }
float& param(ParamValues& v, Global g) { return v[size_t(paramIndex(g))]; }

void note(ymseq::Lane& l, int step, int midi) {
    const int oc = midi / 12 - 1;
    l.pat[size_t(step)] = int16_t(midi - (oc + 1) * 12);
    l.oct[size_t(step)] = int8_t(ymseq::octaveCode(oc, 3));
}

struct Project {
    ParamValues v = defaultValues();
    NonParamState x;
    Project() {
        param(v, Global::FreeTempo) = 120;
        x.seq.cur().steps = 16;
        note(x.seq.cur().edit(0), 0, 60);
        note(x.seq.cur().edit(0), 8, 67);
    }
};

uint32_t rd32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}

// Frames of a float WAV (left, right).
std::vector<float> floats(const std::vector<uint8_t>& wav) {
    REQUIRE(wav.size() >= 58);
    const uint32_t data = rd32(wav, 54);
    REQUIRE(wav.size() == 58 + data);
    std::vector<float> out(data / 4);
    std::memcpy(out.data(), wav.data() + 58, data);
    return out;
}

// Raw render of the same project: what the export must give, shifted by the resampling delay.
std::vector<float> rawRender(OfflineRender::Source src, const ParamValues& v, const NonParamState& x, uint32_t rate,
                             uint32_t& latency, int64_t& stop) {
    OfflineRender r(src, rate, v, x);
    latency = r.latencySamples();
    std::vector<float> out;
    std::vector<float> l(OfflineRender::kChunk), rr(OfflineRender::kChunk);
    ymengine::Outputs o;
    o.left = l.data();
    o.right = rr.data();
    while (r.stopSample() < 0 || int64_t(r.position()) < r.stopSample() + latency) {
        const uint32_t n = r.renderRaw(o, OfflineRender::kChunk);
        for (uint32_t i = 0; i < n; ++i) {
            out.push_back(l[i]);
            out.push_back(rr[i]);
        }
    }
    stop = r.stopSample();
    return out;
}

// Entries of a ZIP (name, content), through its central directory.
std::vector<std::pair<std::string, std::vector<uint8_t>>> unzip(const std::vector<uint8_t>& b) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> out;
    REQUIRE(b.size() >= 22);
    const size_t eocd = b.size() - 22;
    REQUIRE(rd32(b, eocd) == 0x06054B50u);
    const size_t count = size_t(b[eocd + 10]) | size_t(b[eocd + 11]) << 8;
    size_t at = rd32(b, eocd + 16);
    for (size_t k = 0; k < count; ++k) {
        REQUIRE(rd32(b, at) == 0x02014B50u);
        const uint32_t crc = rd32(b, at + 16), size = rd32(b, at + 20);
        const size_t nameLen = size_t(b[at + 28]) | size_t(b[at + 29]) << 8;
        const std::string name(reinterpret_cast<const char*>(b.data() + at + 46), nameLen);
        const size_t local = rd32(b, at + 42);
        const size_t data = local + 30 + nameLen;
        std::vector<uint8_t> bytes(b.begin() + long(data), b.begin() + long(data + size));
        CHECK(ymio::crc32(bytes) == crc);
        out.emplace_back(name, std::move(bytes));
        at += 46 + nameLen;
    }
    return out;
}

class FailingSink final : public ymio::ByteSink {
public:
    bool write(std::span<const uint8_t> b) override {
        if (!ok_ || pos_ + b.size() > 1000) return ok_ = false;
        pos_ += b.size();
        size_ = std::max(size_, pos_);
        return true;
    }
    bool seek(uint64_t p) override {
        if (!ok_ || p > size_) return ok_ = false;
        pos_ = p;
        return true;
    }
    uint64_t position() const override { return pos_; }
    uint64_t size() const override { return size_; }
    bool ok() const override { return ok_; }
    bool commit() override { return false; }

private:
    uint64_t pos_ = 0, size_ = 0;
    bool ok_ = true;
};

}  // namespace

TEST_CASE("WAV export of a pattern: exact length, resampling delay compensated", "[audioexport]") {
    Project pr;
    for (const uint32_t rate : {44100u, 48000u, 96000u}) {
        CAPTURE(rate);
        ymio::MemorySink sink;
        const auto e = exportWav(OfflineRender::Source::Pattern, pr.v, pr.x, {WavFormat::Float32, rate}, sink);
        REQUIRE(e);
        CHECK(e->files == 1);
        CHECK_FALSE(e->truncated);
        CHECK_FALSE(e->failed);
        CHECK(e->seconds == Catch::Approx(2.0).margin(1.0 / rate));  // 16 sixteenth notes at 120 BPM
        const std::vector<float> wav = floats(sink.bytes);
        uint32_t lat = 0;
        int64_t stop = 0;
        const std::vector<float> raw = rawRender(OfflineRender::Source::Pattern, pr.v, pr.x, rate, lat, stop);
        CHECK(int64_t(wav.size() / 2) == stop);  // clean cut at the sequencer stop
        REQUIRE(raw.size() >= size_t(stop + lat) * 2);
        CHECK(std::equal(wav.begin(), wav.end(), raw.begin() + long(lat) * 2));
        // The first note sounds from the very start of the file.
        size_t first = 0;
        while (first < wav.size() && std::fabs(wav[first]) < 1e-4f) ++first;
        CHECK(first / 2 < 64);
    }
}

TEST_CASE("WAV export: formats, two-pass Song, cancel requested, write refused", "[audioexport]") {
    Project pr;
    ymio::MemorySink s16, s24;
    const auto a = exportWav(OfflineRender::Source::Pattern, pr.v, pr.x, {WavFormat::Pcm16, 48000}, s16);
    const auto b = exportWav(OfflineRender::Source::Pattern, pr.v, pr.x, {WavFormat::Pcm24, 48000}, s24);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(s16.bytes.size() == 44 + 96000 * 4);
    CHECK(s24.bytes.size() == 44 + 96000 * 6);
    CHECK(s16.bytes[34] == 16);
    CHECK(s24.bytes[34] == 24);

    pr.x.seq.song.entries = {{0, 2, 0}};
    ymio::MemorySink song;
    const auto c = exportWav(OfflineRender::Source::Song, pr.v, pr.x, {WavFormat::Pcm16, 48000}, song);
    REQUIRE(c);
    CHECK(c->seconds == Catch::Approx(4.0));
    CHECK(song.bytes.size() == 44 + 192000 * 4);

    std::atomic<bool> cancel{true};
    ymio::MemorySink none;
    CHECK_FALSE(exportWav(OfflineRender::Source::Song, pr.v, pr.x, {}, none, &cancel));

    FailingSink full;
    const auto d = exportWav(OfflineRender::Source::Pattern, pr.v, pr.x, {}, full);
    REQUIRE(d);
    CHECK(d->failed);
    CHECK(d->files == 0);
}

TEST_CASE("WAV export: Clock on MIDI ext, the sequencer plays at the free tempo (D-188)", "[audioexport]") {
    // Offline, no MIDI clock to follow: the sequencer keeps Free run, the arpeggio receives the pulses.
    Project pr;
    ymio::MemorySink a, b;
    REQUIRE(exportWav(OfflineRender::Source::Pattern, pr.v, pr.x, {WavFormat::Float32, 48000}, a));
    param(pr.v, Global::ArpClock) = 1;
    const auto e = exportWav(OfflineRender::Source::Pattern, pr.v, pr.x, {WavFormat::Float32, 48000}, b);
    REQUIRE(e);
    CHECK(e->seconds == Catch::Approx(2.0).margin(1.0 / 48000));
    CHECK(b.bytes == a.bytes);  // arpeggio off: same render as with the internal clock
}

TEST_CASE("Stems: one isolated track per WAV, tracks that play, names of the prototype", "[audioexport]") {
    Project pr;
    note(pr.x.seq.cur().edit(2), 4, 48);
    param(pr.v, 2, Track::On) = 1;
    param(pr.v, 2, Track::Mute) = 1;  // isolate(): a muted track still comes out
    param(pr.v, 0, Track::Pan) = -60;
    note(pr.x.seq.cur().edit(5), 0, 50);  // track 6 off: nothing plays
    ymio::MemorySink sink;
    const auto e = exportStems(OfflineRender::Source::Pattern, pr.v, pr.x, {WavFormat::Float32, 48000}, sink);
    REQUIRE(e);
    CHECK(e->files == 2);
    CHECK(e->tracks == std::vector<int>{0, 2});
    CHECK(e->seconds == Catch::Approx(2.0));
    const auto files = unzip(sink.bytes);
    REQUIRE(files.size() == 2);
    CHECK(files[0].first == "stym2149-track-01.wav");
    CHECK(files[1].first == "stym2149-track-03.wav");
    // Each stem is the WAV of the project where its track plays alone (solo), muted or not.
    for (size_t k = 0; k < 2; ++k) {
        const int t = e->tracks[k];
        ParamValues solo = pr.v;
        for (int i = 0; i < kTracks; ++i) param(solo, i, Track::Solo) = i == t ? 1.0f : 0.0f;
        param(solo, t, Track::Mute) = 0;
        ymio::MemorySink one;
        REQUIRE(exportWav(OfflineRender::Source::Pattern, solo, pr.x, {WavFormat::Float32, 48000}, one));
        CHECK(files[k].second == one.bytes);
    }
    // The pan of the track stays: left louder than right.
    const std::vector<float> s0 = floats(files[0].second);
    double l = 0, r = 0;
    for (size_t i = 0; i + 1 < s0.size(); i += 2) {
        l += std::fabs(s0[i]);
        r += std::fabs(s0[i + 1]);
    }
    CHECK(l > r * 1.5);

    // Nothing plays: no file, nothing written.
    Project empty;
    empty.x.seq.cur().lanes[0] = nullptr;
    ymio::MemorySink none;
    const auto z = exportStems(OfflineRender::Source::Pattern, empty.v, empty.x, {}, none);
    REQUIRE(z);
    CHECK(z->files == 0);
    CHECK(none.bytes.empty());
}

TEST_CASE("Stems: channel names in 3 channels, dual chip and DMA tracks", "[audioexport]") {
    ParamValues v = defaultValues();
    param(v, Global::VoiceMode) = 1;  // Authentic 3ch
    CHECK(stemName(v, 0) == "A");
    CHECK(stemName(v, 3) == "Digi");
    param(v, Global::VoiceMode) = 2;  // dual chip
    CHECK(stemName(v, 4) == "A2");
    CHECK(stemName(v, 7) == "Digi2");
    param(v, Global::VoiceMode) = 1;
    param(v, Global::Computer) = 1;  // STE: DMA tracks 5 to 12
    REQUIRE(dmaTrack(voiceMode(v), steMachine(v), 4));
    CHECK(stemName(v, 4) == "DMA1");
    CHECK(stemName(v, 11) == "DMA8");
    param(v, Global::VoiceMode) = 0;
    CHECK(stemName(v, 0) == "01");
    CHECK(stemName(v, 15) == "16");
}
