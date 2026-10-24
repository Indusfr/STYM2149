// STYM2149 — plugin with JUCE: stress test (M8, A3) and cost of an editor frame (M8, A2).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Stress: an audio thread renders blocks of variable size while the message thread changes the voice mode, the
// machine, the clock, the output chain and the resampling, starts and stops the sequencer, opens and closes the
// editor; at several sample rates and block sizes, in real time then offline.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "PluginEditor.h"
#include "PluginProcessor.h"

using namespace stym;
using namespace stym::plugin;

namespace {

void setPlain(StymProcessor& p, const char* id, float v) {
    const int i = findParam(id);
    REQUIRE(i >= 0);
    auto& param = p.parameter(i);
    param.setValueNotifyingHost(param.convertTo0to1(v));
}

std::unique_ptr<juce::PropertiesFile> preferencesIn(const juce::File& file) {
    return std::make_unique<juce::PropertiesFile>(file, juce::PropertiesFile::Options{});
}

// Painting by zones: the editor's image on a display at `scale`, kept up to date area by area (frame()), must be a
// new drawing of the whole desktop, with no zone forgotten.
bool sameAsFullRepaint(StymEditor& editor, float scale = 1.0f) {
    const auto shot = [&editor](float s) { return editor.createComponentSnapshot(editor.getLocalBounds(), true, s); };
    const juce::Image zones = shot(scale);
    shot(scale * 2);  // another display scale: the image is drawn again, whole
    const juce::Image whole = shot(scale);
    const juce::Image::BitmapData a(zones, juce::Image::BitmapData::readOnly), b(whole, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < zones.getHeight(); ++y)
        for (int x = 0; x < zones.getWidth(); ++x)
            if (a.getPixelColour(x, y) != b.getPixelColour(x, y)) return false;
    return true;
}

struct Rng {
    uint32_t s;
    uint32_t operator()() noexcept {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

// Test audio thread: blocks of 1 to `maxBlock` samples and random notes, until stopped. Counts the non-finite
// samples (all outputs) and those of the main output outside [−1, 1] (tanh, D-049).
class AudioThread {
public:
    AudioThread(StymProcessor& p, int maxBlock, uint32_t seed) : p_(p), maxBlock_(maxBlock), rng_{seed} {
        thread_ = std::thread([this] { run(); });
    }
    ~AudioThread() { join(); }
    void join() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }
    long blocks() const noexcept { return blocks_.load(); }
    long bad() const noexcept { return bad_.load(); }

private:
    void run() {
        juce::AudioBuffer<float> buffer(p_.getTotalNumOutputChannels(), maxBlock_);
        juce::MidiBuffer midi;
        midi.ensureSize(1024);
        while (!stop_.load()) {
            const int n = 1 + int(rng_() % uint32_t(maxBlock_));
            midi.clear();
            const int ch = 1 + int(rng_() % 16), note = 36 + int(rng_() % 48);
            if (rng_() % 3 == 0) midi.addEvent(juce::MidiMessage::noteOn(ch, note, uint8_t(1 + rng_() % 127)), int(rng_() % uint32_t(n)));
            if (rng_() % 4 == 0) midi.addEvent(juce::MidiMessage::noteOff(ch, 36 + int(rng_() % 48)), int(rng_() % uint32_t(n)));
            buffer.setSize(buffer.getNumChannels(), n, false, false, true);
            p_.processBlock(buffer, midi);
            long bad = 0;
            for (int c = 0; c < buffer.getNumChannels(); ++c) {
                const float* x = buffer.getReadPointer(c);
                for (int i = 0; i < n; ++i)
                    if (!std::isfinite(x[i]) || (c < 2 && std::fabs(x[i]) > 1.0f)) ++bad;
            }
            bad_ += bad;
            ++blocks_;
        }
    }

    StymProcessor& p_;
    const int maxBlock_;
    Rng rng_;
    std::atomic<bool> stop_{false};
    std::atomic<long> blocks_{0}, bad_{0};
    std::thread thread_;
};

}  // namespace

TEST_CASE("Plugin: stress test, settings changed and editor opened during playback (M8, A3)", "[processor][editor][stress]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    REQUIRE(p.enableAllBuses());
    p.editExtras([](NonParamState& s) {
        s.global.seqClock = ymengine::SeqClock::Internal;
        ymseq::Lane& l = s.seq.patterns[0].edit(0);
        for (int k = 0; k < 16; k += 2) l.pat[size_t(k)] = 0;
    });
    struct Segment {
        double rate;
        int maxBlock;
        bool offline;
    };
    const Segment segments[] = {{44100, 4096, false}, {48000, 512, false}, {96000, 1024, false},
                                {192000, 64, false},  {48000, 333, true}};
    Rng rng{2149};
    std::unique_ptr<StymEditor> editor;
    int opened = 0;
    for (const Segment& s : segments) {
        INFO("rate " << s.rate << ", blocks of 1 to " << s.maxBlock << (s.offline ? ", offline" : ""));
        p.setNonRealtime(s.offline);
        p.prepareToPlay(s.rate, s.maxBlock);
        AudioThread audio(p, s.maxBlock, rng());
        for (int k = 0; k < 60; ++k) {
            switch (rng() % 6) {
            case 0: setPlain(p, "voiceMode", float(rng() % 3)); break;
            case 1: setPlain(p, "computer", float(rng() % 2)); break;
            case 2: setPlain(p, "chipClock", float(rng() % 4)); break;
            case 3: setPlain(p, "resampling", float(rng() % 3)); break;
            case 4: setPlain(p, "outputChain", float(rng() % 3)); break;
            default: p.pushSeqCommand(rng() % 2 ? UiCommand::SeqPlay : UiCommand::SeqStop); break;
            }
            p.serviceMessageThread();
            // Editor opened then closed during playback; when open, it reads the engine status and repaints each turn.
            if (k % 12 == 0) {
                if (editor) {
                    editor->frame();
                    CHECK(sameAsFullRepaint(*editor));
                    editor.reset();
                } else {
                    editor = std::make_unique<StymEditor>(p, preferencesIn(prefs.getFile()));
                    editor->window().mixer().show();
                    editor->window().openScope(int(rng() % 16));
                    editor->frame();
                    CHECK(sameAsFullRepaint(*editor));  // first image, then redrawn by areas
                    ++opened;
                }
            }
            if (editor) editor->frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        audio.join();
        p.releaseResources();
        CHECK(audio.blocks() > 0);
        CHECK(audio.bad() == 0);
    }
    editor.reset();
    CHECK(opened >= 5);
}

// The message thread services the driver without pause while the audio thread swaps engines one after the other:
// service() must not read what the audio thread owns, such as its engine slot (ThreadSanitizer in the CI, D-184).
TEST_CASE("Driver: message thread servicing during engine swaps on the audio thread", "[processor][stress]") {
    Driver d;
    ParamValues values = defaultValues();
    d.prepare({48000, 0}, values, Snapshot{});
    const size_t clock = size_t(findParam("chipClock"));
    std::atomic<int> want{0}, blocks{0};
    std::atomic<bool> stop{false};
    // The audio thread's copy of the values is made here, before the thread starts.
    std::thread audio([&, v = values]() mutable {
        std::vector<float> l(256), r(256);
        ymengine::Outputs o;
        o.left = l.data();
        o.right = r.data();
        while (!stop.load(std::memory_order_acquire)) {
            v[clock] = float(want.load(std::memory_order_relaxed));
            d.process(v, {}, o, 256, false, nullptr);
            blocks.fetch_add(1, std::memory_order_release);
        }
    });
    for (int k = 0; k < 200; ++k) {
        values[clock] = float(k % 4);
        want.store(k % 4, std::memory_order_relaxed);
        // At least 100 services, and until the audio thread has rendered two blocks with this clock, however loaded
        // the machine is: otherwise the loop can end before the audio thread swaps a single engine.
        const int from = blocks.load(std::memory_order_acquire);
        for (int i = 0; i < 100 || blocks.load(std::memory_order_acquire) < from + 2; ++i) d.service(values);
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    d.service(values);
    CHECK(d.engineSwaps() > 0);
}

TEST_CASE("Editor: painting by zones identical to a full paint, animated windows open", "[editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    p.editExtras([](NonParamState& s) { s.global.seqClock = ymengine::SeqClock::Internal; });
    StymEditor editor(p, preferencesIn(prefs.getFile()));
    auto& w = editor.window();
    w.doDemo();
    w.sequencer().show();
    w.mixer().show();
    w.openScope(0);
    w.openScope(5);
    p.pushSeqCommand(UiCommand::SeqPlay);
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 800);
    juce::MidiBuffer midi;
    size_t zones = 0;
    for (int k = 0; k < 40; ++k) {
        for (int b = 0; b < 2; ++b) p.processBlock(buffer, midi);
        p.serviceMessageThread();
        editor.frame();
        zones = std::max(zones, w.desktop().painted().size());
        REQUIRE(sameAsFullRepaint(editor, 1.25f));  // anti-aliased glyphs (D-201)
    }
    CHECK(zones > 1);  // several animated zones at once
}

// Measurement, not a test: `stym_plugin_tests "[bench]"`. Time of an editor frame (30 Hz timer: window tick, drawing
// of the changed areas into the JUCE image) while the sequencer plays, common windows opened one after the other; then
// JUCE paint of the whole editor at the zoom (upper bound: the host repaints only the changed area), on a display at
// 100 and 125 %.
TEST_CASE("Editor: cost of a frame (M8, A2)", "[.][bench][editor]") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::TemporaryFile prefs(".settings");
    StymProcessor p;
    p.prepareToPlay(48000, 512);
    p.editExtras([](NonParamState& s) { s.global.seqClock = ymengine::SeqClock::Internal; });
    juce::AudioBuffer<float> buffer(p.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    for (const auto& [zoom, scale] : {std::pair{100, 1.0f}, {200, 1.0f}, {100, 1.25f}, {200, 1.25f}}) {
        auto f = preferencesIn(prefs.getFile());
        f->setValue("zoom", zoom);
        StymEditor editor(p, std::move(f));
        auto& w = editor.window();
        w.doDemo();
        p.pushSeqCommand(UiCommand::SeqPlay);
        juce::Image image(juce::Image::ARGB, int(std::lround(float(editor.getWidth()) * scale)),
                          int(std::lround(float(editor.getHeight()) * scale)), false);
        const std::pair<const char*, std::function<void()>> steps[] = {
            {"main window", [] {}},
            {"+ Sequencer", [&] { w.sequencer().show(); }},
            {"+ Mixer", [&] { w.mixer().show(); }},
            {"+ 2 oscilloscopes", [&] {
                 w.openScope(0);
                 w.openScope(1);
             }}};
        for (const auto& [name, open] : steps) {
            open();
            std::vector<double> frames, paints;
            const char* env = std::getenv("STYM_BENCH_FRAMES");  // profiling: fewer frames
            const int count = env ? std::max(20, std::atoi(env)) : 130;
            for (int k = 0; k < count; ++k) {
                // 1600 samples at 48 kHz: one frame at 30 Hz.
                for (const int n : {512, 512, 512, 64}) {
                    buffer.setSize(buffer.getNumChannels(), n, false, false, true);
                    p.processBlock(buffer, midi);
                }
                p.serviceMessageThread();
                const auto t0 = Clock::now();
                editor.frame();
                const auto t1 = Clock::now();
                {
                    juce::Graphics g(image);
                    g.addTransform(juce::AffineTransform::scale(scale));
                    editor.paintEntireComponent(g, false);
                }
                const auto t2 = Clock::now();
                if (k < 10) continue;  // warm-up
                frames.push_back(ms(t1 - t0));
                paints.push_back(ms(t2 - t1));
            }
            auto stats = [](std::vector<double> v) {
                std::sort(v.begin(), v.end());
                double sum = 0;
                for (const double x : v) sum += x;
                return std::array<double, 3>{sum / double(v.size()), v[v.size() * 95 / 100], v.back()};
            };
            const auto fr = stats(frames), pa = stats(paints);
            std::printf("zoom %3d %% display %3d %%  %-20s frame %6.3f ms (p95 %6.3f, max %6.3f)  full JUCE paint %6.3f ms "
                        "(p95 %6.3f)\n",
                        zoom, int(std::lround(scale * 100)), name, fr[0], fr[1], fr[2], pa[0], pa[1]);
        }
        p.pushSeqCommand(UiCommand::SeqStop);
    }
    SUCCEED();
}
