// STYM2149 — voice engine (ymengine::Engine): 16-channel mode against the chip, 3-channel modes through registers,
// determinism, independence from the block size.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "ymcore/ChipRenderer.h"
#include "ymcore/Ym2149.h"
#include "ymengine/Engine.h"
#include "ymengine/Mfp68901.h"
#include "ymengine/Pitch.h"

using namespace stym;
using ymengine::Engine;

namespace {

ymengine::EngineConfig config(ymcore::OutputMode mode = ymcore::OutputMode::Raw, uint32_t rate = 48000) {
    ymengine::EngineConfig c;
    c.render.hostRate = rate;
    c.render.mode = mode;
    return c;
}

struct Stereo {
    std::vector<float> l, r;
};

// Renders n samples in blocks of `block`; schedule(engine, start, size) places the events of the block.
Stereo renderBlocks(Engine& e, uint32_t n, uint32_t block,
                    const std::function<void(Engine&, uint32_t, uint32_t)>& schedule = {}) {
    Stereo s;
    s.l.resize(n);
    s.r.resize(n);
    for (uint32_t at = 0; at < n; at += block) {
        const uint32_t len = std::min(block, n - at);
        if (schedule) schedule(e, at, len);
        e.render(s.l.data() + at, s.r.data() + at, len);
    }
    return s;
}

// Event dated in absolute samples, placed in the block that contains it.
struct Ev {
    uint32_t at;
    std::function<void(Engine&, uint32_t)> fire;
};
std::function<void(Engine&, uint32_t, uint32_t)> sequence(std::vector<Ev> evs) {
    return [evs](Engine& e, uint32_t start, uint32_t len) {
        for (const Ev& ev : evs)
            if (ev.at >= start && ev.at < start + len) ev.fire(e, ev.at - start);
    };
}

double peak(const std::vector<float>& x) {
    double p = 0;
    for (float v : x) p = std::max(p, double(std::fabs(v)));
    return p;
}

}  // namespace

TEST_CASE("16-channel engine: one voice alone = channel A of the chip, up to the gain", "[engine]") {
    using ymcore::BoardChain;
    using ymcore::OutputMode;
    struct Case {
        OutputMode mode;
        BoardChain board;
    };
    for (const Case c : {Case{OutputMode::Raw, BoardChain::Raw}, Case{OutputMode::Raw, BoardChain::ST},
                         Case{OutputMode::Raw, BoardChain::STE}, Case{OutputMode::RawBox, BoardChain::ST},
                         Case{OutputMode::Retro, BoardChain::STE}, Case{OutputMode::RawBox, BoardChain::Raw},
                         Case{OutputMode::Retro, BoardChain::Raw}})
    for (const bool softClip : {false, true}) {
        auto cfg = config(c.mode);
        cfg.render.board.chain = c.board;
        cfg.render.board.bassDb = 6;
        cfg.render.board.trebleDb = -4;
        Engine e;
        e.prepare(cfg);
        ymengine::GlobalParams g;
        g.softClip = softClip;
        e.setGlobal(g);
        ymengine::TrackParams t;
        t.patch.noiseOn = true;  // tone + noise: the LFSR of the virtual channel must follow that of the chip
        t.patch.noisePeriod = 5;
        e.setTrack(0, t);
        e.noteOn(0, 69, 127);
        const Stereo got = renderBlocks(e, 9600, 480);

        ymcore::ChipRenderer chip;
        chip.prepare(cfg.render);
        const uint16_t tp = 284;  // round(2 MHz / 16 / 440)
        chip.write(0, 0, uint8_t(tp & 0xff));
        chip.write(0, 1, uint8_t(tp >> 8));
        chip.write(0, 6, 5);
        chip.write(0, 7, 0x36);  // channel A: tone and noise
        chip.write(0, 8, 13);
        std::vector<float> ref(9600);
        chip.render(ref.data(), 9600);

        // Level of one channel alone normalised to index 31, velocity 127, track level 0.75, centre
        // (cos π/4 × √2 = 1), master volume 0.7 × 1.2, then tanh if the saturation of the prototype is on
        // (|tanh a − tanh b| ≤ |a − b|: same tolerance).
        const double top = ymcore::tables::kMixTable[31];
        const double k = 32768.0 / top * 0.75 * 0.7 * 1.2;
        // The chip without a board filter in RawBox / Retro follows AtariAudio in integers (truncated average, integer
        // moving average): a deviation of 2 steps of 1 / 32 768 at most. Elsewhere, both paths are floating point.
        const bool integer = c.mode != OutputMode::Raw && c.board == BoardChain::Raw;
        const double tol = (integer ? 2.0 / 32768 : 0) * k + 2e-6 * peak(ref) * k;
        double worst = 0;
        for (size_t i = 0; i < ref.size(); ++i) {
            const double want = softClip ? std::tanh(double(ref[i]) * k) : double(ref[i]) * k;
            worst = std::max(worst, std::fabs(double(got.l[i]) - want));
            REQUIRE(got.l[i] == got.r[i]);
        }
        INFO("mode " << int(c.mode) << " board " << int(c.board) << " tanh " << softClip << " max deviation " << worst);
        REQUIRE(peak(ref) > 0.05);
        REQUIRE(worst <= tol);
    }
}

TEST_CASE("16-channel engine: released note → voice freed, output exactly zero", "[engine]") {
    Engine e;
    e.prepare(config());
    e.noteOn(0, 60, 100);
    std::vector<float> l(48000), r(48000);
    e.render(l.data(), r.data(), 4800);
    REQUIRE(e.activeVoices() == 1);
    e.noteOff(0, 60);
    e.render(l.data(), r.data(), 480);
    REQUIRE(e.activeVoices() == 0);  // fade of 64 steps at 48 kHz
    // Moving average (2 048 samples) and tail of the resampler elapsed: exact zero.
    e.render(l.data(), r.data(), 4096);
    e.render(l.data(), r.data(), 1000);
    for (uint32_t i = 0; i < 1000; ++i) {
        REQUIRE(l[i] == 0.0f);
        REQUIRE(r[i] == 0.0f);
    }
}

TEST_CASE("Engine: identical output whatever the block size", "[engine]") {
    auto scene = [](ymengine::VoiceMode mode) {
        std::vector<Ev> evs;
        auto on = [&](uint32_t at, int note, int vel) {
            evs.push_back({at, [note, vel](Engine& e, uint32_t o) { e.noteOn(o, note, vel); }});
        };
        auto off = [&](uint32_t at, int note) { evs.push_back({at, [note](Engine& e, uint32_t o) { e.noteOff(o, note); }}); };
        on(10, 48, 110);
        on(500, 55, 90);
        on(1301, 60, 127);
        off(4000, 48);
        evs.push_back({5000, [](Engine& e, uint32_t o) { e.pitchBend(o, 0.5); }});
        on(6100, 67, 70);
        off(9000, 55);
        off(9001, 60);
        off(12000, 67);
        on(13000, 72, 127);
        evs.push_back({16000, [](Engine& e, uint32_t o) { e.allNotesOff(o); }});
        (void)mode;
        return evs;
    };
    auto setup = [](Engine& e, ymengine::VoiceMode mode) {
        ymengine::GlobalParams g;
        g.mode = mode;
        g.layer = true;
        e.setGlobal(g);
        ymengine::TrackParams t0;  // tone, smooth ADSR, vibrato
        t0.patch.attackMs = 20;
        t0.patch.decayMs = 80;
        t0.patch.sustainPct = 60;
        t0.patch.releaseMs = 150;
        t0.patch.vibratoCents = 40;
        t0.mix.enabled = true;
        t0.mix.pan = -0.5;
        e.setTrack(0, t0);
        ymengine::TrackParams t1;  // SID
        t1.patch.generator = ymengine::Generator::Sid;
        t1.patch.sidRatio = 2;
        t1.mix.enabled = true;
        t1.mix.pan = 0.7;
        e.setTrack(1, t1);
        ymengine::TrackParams t2;  // Sync Buzzer
        t2.patch.generator = ymengine::Generator::SyncBuzzer;
        t2.patch.envShape = 10;
        t2.mix.enabled = true;
        e.setTrack(2, t2);
        ymengine::TrackParams t3;  // ornament and noise, "Chip" ADSR
        t3.patch.ornamentOn = true;
        t3.patch.ornamentLength = 4;
        t3.patch.ornamentPitch = {0, 12, 7, 3};
        t3.patch.ornamentNoise = {1, 0, 0, 1};
        t3.patch.ornamentVolume = {15, 12, 10, 8};
        t3.patch.attackMs = 10;
        t3.patch.releaseMs = 60;
        t3.patch.adsrChip = true;
        t3.mix.enabled = true;
        e.setTrack(3, t3);
    };
    for (const auto mode : {ymengine::VoiceMode::Studio16, ymengine::VoiceMode::Authentic3, ymengine::VoiceMode::Dual3}) {
        Stereo ref;
        for (const uint32_t block : {4096u, 1u, 37u, 256u, 480u, 1024u}) {
            Engine e;
            e.prepare(config());
            setup(e, mode);
            const Stereo s = renderBlocks(e, 24000, block, sequence(scene(mode)));
            if (ref.l.empty()) {
                ref = s;
                REQUIRE(peak(ref.l) > 0.05);
                continue;
            }
            INFO("mode " << int(mode) << " block " << block);
            REQUIRE(s.l == ref.l);
            REQUIRE(s.r == ref.r);
        }
    }
}

namespace {
using Log = std::vector<Engine::WriteRecord>;

struct Rig {
    Engine e;
    Log log;
    uint64_t at = 0;
    std::vector<float> l = std::vector<float>(1 << 16), r = std::vector<float>(1 << 16);

    explicit Rig(ymengine::VoiceMode mode, ymengine::EngineConfig cfg = config()) {
        e.prepare(cfg);
        ymengine::GlobalParams g;
        g.mode = mode;
        e.setGlobal(g);
        log.reserve(1 << 20);
        e.setWriteLog(&log);
    }
    void global(const std::function<void(ymengine::GlobalParams&)>& f) {
        ymengine::GlobalParams g = e.global();
        f(g);
        e.setGlobal(g);
    }
    void run(uint32_t n) {
        while (n > 0) {
            const uint32_t k = std::min<uint32_t>(n, uint32_t(l.size()));
            e.render(l.data(), r.data(), k);
            at += k;
            n -= k;
        }
    }
    // Writes of a register (chip k), in order.
    Log writes(uint8_t reg, uint8_t chip = 0, size_t from = 0) const {
        Log out;
        for (size_t i = from; i < log.size(); ++i)
            if (log[i].reg == reg && log[i].chip == chip) out.push_back(log[i]);
        return out;
    }
};

ymengine::TrackParams track(bool enabled = true) {
    ymengine::TrackParams t;
    t.mix.enabled = enabled;
    t.mix.level = 1;
    return t;
}
}  // namespace

TEST_CASE("3-channel: writes of a note, release fade", "[engine][3ch]") {
    Rig rig(ymengine::VoiceMode::Authentic3);
    rig.e.setTrack(0, track());
    rig.e.noteOn(0, 69, 127);
    rig.run(480);
    REQUIRE(rig.log.size() == 4);
    const Engine::WriteRecord want[] = {{0, 0, 0, 0x1c}, {0, 0, 1, 0x01}, {0, 0, 7, 0x3e}, {0, 0, 8, 13}};
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(rig.log[i].cycle == want[i].cycle);
        REQUIRE(rig.log[i].reg == want[i].reg);
        REQUIRE(rig.log[i].value == want[i].value);
    }
    // Release without ADSR: fade of 64 steps at 48 kHz on the 4-bit volume, then channel off.
    const size_t mark = rig.log.size();
    const uint64_t c0 = rig.e.chip(0).cycleAtSample(rig.at);
    rig.e.noteOff(0, 69);
    rig.run(480);
    REQUIRE(rig.e.activeVoices() == 0);
    const Log vol = rig.writes(8, 0, mark);
    REQUIRE(vol.size() >= 10);
    for (size_t i = 1; i < vol.size(); ++i) REQUIRE(vol[i].value < vol[i - 1].value);
    REQUIRE(vol.back().value == 0);  // round(13 × 2/64) = 0 from step 62
    const Log mixer = rig.writes(7, 0, mark);
    REQUIRE(mixer.size() == 1);
    REQUIRE(mixer[0].value == 0x3f);
    REQUIRE(mixer[0].cycle == c0 + (64 * 2'000'000 + 47'999) / 48'000);  // 64th step: channel off
}

TEST_CASE("3-channel: the velocity and the track level go through the 4-bit volume", "[engine][3ch]") {
    Rig rig(ymengine::VoiceMode::Authentic3);
    auto t = track();
    t.mix.level = 0.8;
    rig.e.setTrack(0, t);
    // Level 0.8 × velocity 64/127: −7.9 dB under volume 13 (−7.4 dB), that is −15.4 dB; the nearest 4-bit level is
    // volume 10 (−16.5 dB), not round(13 × 0.8 × 64/127) = 5 (−32.3 dB) as in the prototype (D-200).
    rig.e.noteOn(0, 60, 64);
    rig.run(100);
    const Log vol = rig.writes(8);
    REQUIRE(vol.size() == 1);
    REQUIRE(vol[0].value == 10);
}

TEST_CASE("3-channel and 16-channel: a track level and a velocity lower a note alike (D-200)", "[engine][3ch]") {
    // RMS of a held note alone, after its attack. The two modes stay within half of the largest 4-bit step (3.9 dB
    // from volume 13 to 14); the prototype's rounding of the 4-bit index made the 3-channel mode 10.6 dB quieter at
    // the plugin's defaults (level 0.75, velocity 100).
    auto level = [](ymengine::VoiceMode mode, double trackLevel, int velocity) {
        Rig rig(mode);
        auto t = track();
        t.mix.level = trackLevel;
        rig.e.setTrack(0, t);
        rig.e.noteOn(0, 60, velocity);
        rig.run(4800);
        rig.run(48000);
        double sum = 0;
        for (size_t i = 0; i < 48000; ++i) sum += double(rig.l[i]) * double(rig.l[i]);
        return std::sqrt(sum / 48000);
    };
    for (const auto& [trackLevel, velocity] : {std::pair{0.75, 100}, {1.0, 64}, {0.5, 127}, {1.0, 127}, {0.3, 90}}) {
        INFO("level " << trackLevel << ", velocity " << velocity);
        const double db = 20 * std::log10(level(ymengine::VoiceMode::Studio16, trackLevel, velocity) /
                                          level(ymengine::VoiceMode::Authentic3, trackLevel, velocity));
        CHECK(std::fabs(db) < 1.95);
    }
}

TEST_CASE("3-channel: the track level and the velocity lower the frame volumes of a Chipdrum hit (D-286)",
          "[engine][3ch][kit]") {
    // Kick of the Classic kit: its first frame is at volume 15. Level 0.5 (-6 dB): the nearest 4-bit level is 13
    // (-7.4 dB), not 14 (-3.5 dB); at level 1 the frame plays as it is.
    for (const auto& [trackLevel, want] : {std::pair{1.0, 15}, {0.5, 13}}) {
        Rig rig(ymengine::VoiceMode::Authentic3);
        rig.e.setKit(ymengine::kKitBanks[0].kit);
        auto t = track();
        t.patch.generator = ymengine::Generator::Chipdrum;
        t.mix.level = trackLevel;
        rig.e.setTrack(0, t);
        rig.e.noteOn(0, 60, 127);
        rig.run(4800);
        const Log vol = rig.writes(8);
        REQUIRE(!vol.empty());
        int top = 0;
        for (const auto& w : vol) top = std::max<int>(top, w.value);
        INFO("level " << trackLevel);
        CHECK(top == want);
    }
}

TEST_CASE("3-channel and 16-channel: a Digidrum and a Chipdrum hit play alike (D-286)", "[engine][3ch][kit]") {
    // RMS of the left output over the first 250 ms of a hit, track level and velocity as the plugin's defaults and
    // at full level. A Digidrum is a chip channel playing its 4-bit values in both modes (no gain in 16 channels:
    // it was 5, +14 dB); the 16-channel Chipdrum sample (gain 1.8) follows the register drums of the 3 channels.
    std::vector<uint8_t> nibbles(7800);
    for (size_t i = 0; i < nibbles.size(); ++i) {  // 260 Hz sine, linear 4-bit values, decaying
        const double x = std::exp(-double(i) / 2000) * std::sin(6.283185307179586 * double(i) / 30);
        nibbles[i] = uint8_t(std::lround(7.5 + 7.5 * x));
    }
    auto level = [&](ymengine::VoiceMode mode, ymengine::Generator gen, int note, double trackLevel, int velocity) {
        Rig rig(mode);
        rig.e.setKit(ymengine::kKitBanks[0].kit);
        rig.e.setUserSample(0, nibbles);
        const int tr = mode == ymengine::VoiceMode::Studio16 || gen != ymengine::Generator::Digidrum ? 0 : 3;
        auto t = track();
        t.patch.generator = gen;
        t.mix.level = trackLevel;
        rig.e.setTrack(tr, t);
        rig.global([tr](ymengine::GlobalParams& g) { g.focusTrack = tr; });
        rig.e.noteOn(0, note, velocity);
        rig.run(12000);
        double sum = 0;
        for (size_t i = 0; i < 12000; ++i) sum += double(rig.l[i]) * double(rig.l[i]);
        return std::sqrt(sum / 12000);
    };
    struct Case {
        ymengine::Generator gen;
        int note;
        double tol;
    };
    for (const Case c : {Case{ymengine::Generator::Digidrum, 60, 1.95}, Case{ymengine::Generator::Chipdrum, 60, 2.5},
                         Case{ymengine::Generator::Chipdrum, 66, 2.5}})
        for (const auto& [trackLevel, velocity] : {std::pair{0.75, 100}, {0.75, 127}, {1.0, 127}}) {
            INFO("generator " << int(c.gen) << ", note " << c.note << ", level " << trackLevel << ", velocity "
                              << velocity);
            using ymengine::VoiceMode;
            const double db = 20 * std::log10(level(VoiceMode::Studio16, c.gen, c.note, trackLevel, velocity) /
                                              level(VoiceMode::Authentic3, c.gen, c.note, trackLevel, velocity));
            CHECK(std::fabs(db) < c.tol);
        }
}

TEST_CASE("3-channel: R6 follows the last noise switched on, then returns to the remaining noise", "[engine][3ch]") {
    Rig rig(ymengine::VoiceMode::Authentic3);
    auto a = track();
    a.patch.noiseOn = true;
    a.patch.noisePeriod = 5;
    auto b = track();
    b.patch.noiseOn = true;
    b.patch.noisePeriod = 20;
    rig.e.setTrack(0, a);
    rig.e.setTrack(1, b);
    rig.e.noteOn(0, 60, 127);
    rig.run(100);
    rig.global([](auto& g) { g.focusTrack = 1; });
    rig.e.noteOn(0, 64, 127);
    rig.run(100);
    rig.e.noteOff(0, 64);
    rig.run(480);
    const Log r6 = rig.writes(6);
    REQUIRE(r6.size() == 3);
    REQUIRE(r6[0].value == 5);
    REQUIRE(r6[1].value == 20);
    REQUIRE(r6[2].value == 5);
    REQUIRE(rig.e.activeVoices() == 1);
    // Mixer: channel A tone + noise, channel B the same, then channel B off.
    const Log r7 = rig.writes(7);
    REQUIRE(r7.size() == 3);
    REQUIRE(r7[0].value == 0x36);
    REQUIRE(r7[1].value == 0x24);
    REQUIRE(r7[2].value == 0x36);
}

TEST_CASE("3-channel: R11-R13 to the Sync Buzzer first, then back to the envelope voice", "[engine][3ch]") {
    Rig rig(ymengine::VoiceMode::Authentic3);
    auto env = track();
    env.patch.envelopeVolume = true;
    env.patch.envShape = 12;
    env.patch.envPeriod = 640;
    auto buzz = track();
    buzz.patch.generator = ymengine::Generator::SyncBuzzer;
    rig.e.setTrack(0, env);
    rig.e.setTrack(1, buzz);
    rig.e.noteOn(0, 48, 127);
    rig.run(100);
    REQUIRE(rig.writes(13).size() == 1);
    REQUIRE(rig.writes(13)[0].value == 12);
    REQUIRE(rig.writes(11)[0].value == 0x80);
    REQUIRE(rig.writes(12)[0].value == 0x02);

    rig.global([](auto& g) { g.focusTrack = 1; });
    const size_t mark = rig.log.size();
    rig.e.noteOn(0, 69, 127);
    rig.run(4800);  // 0.1 s of buzzer at 440 Hz
    const Log r13 = rig.writes(13, 0, mark);
    const Log r11 = rig.writes(11, 0, mark);
    REQUIRE(r11.size() == 1);
    REQUIRE(r11[0].value == 32);  // period 400 × 32 > 3 200: 32 ("EP > 100 → 1" of the prototype)
    const int buzzer = [&] {
        for (int i = 0; i < Engine::kVoices; ++i)
            if (rig.e.voice(i).on && rig.e.voice(i).note == 69) return i;
        return -1;
    }();
    REQUIRE(buzzer >= 0);
    const double hz = rig.e.voice(buzzer).timerActualHz;
    REQUIRE(hz == ymengine::mfpQuantize(440).hz());
    REQUIRE(r13.size() >= 44);
    REQUIRE(r13.size() <= 46);
    for (size_t i = 0; i < r13.size(); ++i) REQUIRE(r13[i].value == 8);
    for (size_t i = 2; i < r13.size(); ++i)
        REQUIRE(std::fabs(double(r13[i].cycle - r13[i - 1].cycle) - 2e6 / hz) <= 1.0);

    // End of the buzzer: the envelope voice takes R11-R13 back (shape restarted).
    const size_t mark2 = rig.log.size();
    rig.e.noteOff(0, 69);
    rig.run(480);
    const Log back13 = rig.writes(13, 0, mark2);
    REQUIRE(back13.size() == 1);
    REQUIRE(back13[0].value == 12);
    REQUIRE(rig.writes(11, 0, mark2).back().value == 0x80);
    REQUIRE(rig.writes(12, 0, mark2).back().value == 0x02);
}

TEST_CASE("3-channel: Digidrum track steals the designated channel, plays the sample on the timer", "[engine][3ch]") {
    Rig rig(ymengine::VoiceMode::Authentic3);
    const uint8_t sample[] = {15, 0, 8, 4, 12};
    rig.e.setUserSample(0, sample);
    auto chipTrack = track();
    auto drum = track();
    drum.patch.generator = ymengine::Generator::Digidrum;
    rig.e.setTrack(0, chipTrack);
    rig.e.setTrack(1, chipTrack);
    rig.e.setTrack(2, chipTrack);
    rig.e.setTrack(3, drum);
    rig.global([](auto& g) { g.layer = true; });
    rig.e.noteOn(0, 60, 127);
    rig.run(4800);
    // A and B: tone; C: stolen by the digidrum (mixer off, volume = sample).
    // End of the sample: the channel falls silent one timer step after the last value.
    const Log r10 = rig.writes(10);
    REQUIRE(r10.size() == 7);
    const uint8_t want[] = {13, 15, 0, 8, 4, 12, 0};
    for (size_t i = 0; i < 7; ++i) REQUIRE(r10[i].value == want[i]);
    REQUIRE(r10[0].cycle == r10[1].cycle);  // the voice of track 3 is replaced at the attack
    const double hz = ymengine::mfpQuantize(7800).hz();
    for (size_t i = 2; i < 7; ++i) REQUIRE(std::fabs(double(r10[i].cycle - r10[i - 1].cycle) - 2e6 / hz) <= 1.0);
    REQUIRE(rig.e.activeVoices() == 2);
    const Log r7 = rig.writes(7);
    REQUIRE(r7.back().value == 0x3c);
}

TEST_CASE("Dual chip: tracks 5 to 8 play on the second chip", "[engine][3ch]") {
    Rig rig(ymengine::VoiceMode::Dual3);
    rig.e.setTrack(5, track());
    rig.global([](auto& g) { g.focusTrack = 5; });
    rig.e.noteOn(0, 69, 127);
    rig.run(480);
    REQUIRE(rig.writes(8, 0).empty());
    REQUIRE(rig.writes(8, 1).size() == 1);
    REQUIRE(rig.e.chip(1).chip().readRegister(8) == 13);
    float l[480], r[480];
    rig.e.render(l, r, 480);
    REQUIRE(peak(std::vector<float>(l, l + 480)) > 0.01);
}

TEST_CASE("3-channel: live mute of a track", "[engine][3ch]") {
    Rig rig(ymengine::VoiceMode::Authentic3);
    rig.e.setTrack(0, track());
    rig.e.noteOn(0, 69, 127);
    rig.run(4800);
    REQUIRE(peak(std::vector<float>(rig.l.begin(), rig.l.begin() + 4800)) > 0.05);
    auto mix = track().mix;
    mix.mute = true;
    rig.e.setTrackMix(0, 0, mix);
    rig.run(8192);  // moving average and resampler elapsed
    rig.run(1000);
    for (int i = 0; i < 1000; ++i) REQUIRE(rig.l[size_t(i)] == 0.0f);
    REQUIRE(rig.e.activeVoices() == 1);
}

TEST_CASE("3-channel: SID effect with the MFP timer or continuous", "[engine][3ch][timer]") {
    for (const auto mode : {ymengine::TimerMode::Mfp, ymengine::TimerMode::Free}) {
        Rig rig(ymengine::VoiceMode::Authentic3);
        rig.global([&](auto& g) { g.timer = mode; });
        auto sid = track();
        sid.patch.generator = ymengine::Generator::Sid;
        sid.patch.sidRatio = 1.5;
        sid.patch.sidMinVolume = 3;
        rig.e.setTrack(0, sid);
        rig.e.noteOn(0, 57, 127);  // 220 Hz × 1.5 = 330 Hz
        rig.run(4800);
        const auto info = rig.e.voice(0);
        REQUIRE(info.timerHz == ymengine::noteHz(57) * 1.5);
        const double hz = info.timerActualHz;
        if (mode == ymengine::TimerMode::Mfp) REQUIRE(hz == ymengine::mfpQuantize(330).hz());
        else REQUIRE(std::fabs(hz - 330) < 1e-9);
        const Log r8 = rig.writes(8);
        REQUIRE(r8.size() >= 33);
        for (size_t i = 0; i < r8.size(); ++i) REQUIRE(r8[i].value == (i % 2 ? 3 : 13));
        for (size_t i = 2; i < r8.size(); ++i)
            REQUIRE(std::fabs(double(r8[i].cycle - r8[i - 1].cycle) - 2e6 / hz) <= 1.0);
    }
}

TEST_CASE("Timer: mode frozen at the attack, changing it only affects the following notes", "[engine][3ch][timer]") {
    Rig rig(ymengine::VoiceMode::Authentic3);
    auto sid = track();
    sid.patch.generator = ymengine::Generator::Sid;
    sid.patch.sidMinVolume = 3;
    rig.e.setTrack(0, sid);
    rig.e.noteOn(0, 57, 127);
    rig.run(2400);
    const double mfpHz = ymengine::mfpQuantize(ymengine::noteHz(57)).hz();
    REQUIRE(rig.e.voice(0).timerActualHz == mfpHz);
    rig.global([](auto& g) { g.timer = ymengine::TimerMode::Free; });
    rig.run(2400);
    REQUIRE(rig.e.voice(0).timerActualHz == mfpHz);
    const Log r8 = rig.writes(8);
    REQUIRE(r8.size() >= 20);
    for (size_t i = 2; i < r8.size(); ++i)  // always the MFP period, without a jump at the change
        REQUIRE(std::fabs(double(r8[i].cycle - r8[i - 1].cycle) - 2e6 / mfpHz) <= 1.0);
    rig.e.noteOn(0, 60, 127);  // new note: continuous mode
    rig.run(2400);
    const int v = [&] {
        for (int i = 0; i < ymengine::Engine::kVoices; ++i)
            if (rig.e.voice(i).on && rig.e.voice(i).note == 60 && !rig.e.voice(i).fading) return i;
        return -1;
    }();
    REQUIRE(v >= 0);
    REQUIRE(std::fabs(rig.e.voice(v).timerActualHz - ymengine::noteHz(60)) < 1e-9);
}

TEST_CASE("16-channel: smooth ADSR, stages and sustain level", "[engine][adsr]") {
    auto cfg = config();
    cfg.render.dc = ymcore::DcFilter::None;  // linear and local output: exact ratio in sustain
    Rig with(ymengine::VoiceMode::Studio16, cfg), without(ymengine::VoiceMode::Studio16, cfg);
    with.global([](auto& g) { g.softClip = false; });
    without.global([](auto& g) { g.softClip = false; });
    auto t = track();
    without.e.setTrack(0, t);
    t.patch.attackMs = 10;
    t.patch.decayMs = 20;
    t.patch.sustainPct = 50;
    t.patch.releaseMs = 30;
    with.e.setTrack(0, t);
    with.e.noteOn(0, 69, 127);
    without.e.noteOn(0, 69, 127);
    with.run(470);
    REQUIRE(with.e.voice(0).adsrStage == 1);
    REQUIRE(with.e.voice(0).adsrLevel == 0);
    with.run(30);  // 10 ms = 480 samples
    REQUIRE(with.e.voice(0).adsrStage == 2);
    REQUIRE(with.e.voice(0).adsrLevel == 1);
    with.run(1000);  // 30 ms = 1 440 samples
    REQUIRE(with.e.voice(0).adsrStage == 3);
    REQUIRE(with.e.voice(0).adsrLevel == 0.5);
    // Sustain: half of the voice without ADSR, sample by sample.
    std::vector<float> a(2400), b(2400);
    with.run(1900);
    without.run(3400);
    with.e.render(a.data(), with.r.data(), 2400);
    without.e.render(b.data(), without.r.data(), 2400);
    REQUIRE(peak(b) > 0.1);
    for (size_t i = 0; i < a.size(); ++i) REQUIRE(std::fabs(double(a[i]) - 0.5 * double(b[i])) <= 1e-6);
    // Release: 30 ms, then voice freed.
    with.e.noteOff(0, 69);
    with.run(1);
    REQUIRE(with.e.voice(0).adsrStage == 4);
    REQUIRE(with.e.voice(0).adsrLevel == 0.5);
    with.run(1430);
    REQUIRE(with.e.activeVoices() == 1);
    with.run(20);
    REQUIRE(with.e.activeVoices() == 0);
}

TEST_CASE("The ADSR of a drum track has no effect: Chipdrum and Digidrum, 16 and 3 channels (D-205)",
          "[engine][adsr]") {
    std::vector<uint8_t> sample(2000);
    for (size_t i = 0; i < sample.size(); ++i) sample[i] = uint8_t((i * 7) % 16);
    for (const auto mode : {ymengine::VoiceMode::Studio16, ymengine::VoiceMode::Authentic3})
        for (const auto gen : {ymengine::Generator::Chipdrum, ymengine::Generator::Digidrum})
            for (const bool chipAdsr : {false, true}) {
                CAPTURE(int(mode), int(gen), chipAdsr);
                auto cfg = config();
                cfg.render.dc = ymcore::DcFilter::None;
                Rig with(mode, cfg), without(mode, cfg);
                for (Rig* r : {&with, &without}) r->e.setUserSample(0, sample);
                auto t = track();
                t.patch.generator = gen;
                t.patch.adsrChip = chipAdsr;
                without.e.setTrack(0, t);
                t.patch.attackMs = 10;
                t.patch.decayMs = 20;
                t.patch.sustainPct = 30;
                t.patch.releaseMs = 40;
                with.e.setTrack(0, t);
                // Kick (slot 1) twice; the second hit, a note-off in between, on a sustained shape.
                for (Rig* r : {&with, &without}) {
                    r->e.noteOn(0, 60, 127);
                    r->run(2400);
                    r->e.noteOff(0, 60);
                    r->run(600);
                    r->e.noteOn(0, 60, 100);
                }
                std::vector<float> a(9600), b(9600), ar(9600), br(9600);
                with.e.render(a.data(), ar.data(), 9600);
                without.e.render(b.data(), br.data(), 9600);
                REQUIRE(peak(b) > 0.01);
                CHECK(a == b);
                CHECK(ar == br);
                CHECK(with.log.size() == without.log.size());
            }
}

TEST_CASE("Voice observation: attack rank, period of the note, current ADSR level", "[engine][adsr]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    auto t = track();
    t.patch.attackMs = 10;
    rig.e.setTrack(0, t);
    rig.e.noteOn(0, 69, 127);
    rig.run(240);  // 5 ms: mid-attack
    const auto a = rig.e.voice(0);
    REQUIRE(a.notePeriod == 284);  // 2 MHz / (16 × 440 Hz)
    REQUIRE(a.adsrStage == 1);
    REQUIRE(a.adsrLevel == 0);
    REQUIRE(std::fabs(a.adsrNow - 0.5) <= 1.0 / 480);
    rig.e.noteOn(0, 72, 127);
    rig.run(1);
    int newest = -1;
    uint64_t age = 0;
    for (int i = 0; i < Engine::kVoices; ++i)
        if (rig.e.voice(i).on && (newest < 0 || rig.e.voice(i).age > age)) {
            newest = i;
            age = rig.e.voice(i).age;
        }
    REQUIRE(newest >= 0);
    REQUIRE(rig.e.voice(newest).note == 72);
}

TEST_CASE("16-channel: pitch bend on the sounding voices", "[engine]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    rig.e.setTrack(0, track());
    rig.e.noteOn(0, 69, 127);
    rig.run(100);
    REQUIRE(rig.e.voice(0).tonePeriod == 284);
    rig.e.pitchBend(0, 2);
    rig.run(100);
    REQUIRE(rig.e.voice(0).tonePeriod == ymengine::tonePeriodFor(ymengine::noteHz(71), ymcore::clocks::kDefault));
    REQUIRE(rig.e.voice(0).note == 69);
}

TEST_CASE("Arpeggio: the last note keeps cycling during the release", "[engine][arp]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    auto t = track();
    t.arp.on = true;
    t.arp.mode = ymengine::ArpMode::Up;
    t.arp.octaves = 2;
    t.arp.rateHz = 48000.0 / 1024;
    t.patch.releaseMs = 200;
    rig.e.setTrack(0, t);
    rig.e.noteOn(0, 60, 100);
    rig.run(1024);
    const int v = [&] {
        for (int i = 0; i < Engine::kVoices; ++i)
            if (rig.e.voice(i).on && !rig.e.voice(i).fading) return i;
        return -1;
    }();
    REQUIRE(v >= 0);
    REQUIRE(rig.e.voice(v).note == 60);
    rig.e.noteOff(0, 60);
    rig.run(1024);  // release: same voice, next pitch, without a new attack
    REQUIRE(rig.e.voice(v).on);
    REQUIRE(rig.e.voice(v).adsrStage == 4);
    REQUIRE(rig.e.voice(v).note == 72);
    rig.run(1024);
    REQUIRE(rig.e.voice(v).note == 60);
    REQUIRE(rig.e.activeVoices() == 1);
    rig.run(48000);
    REQUIRE(rig.e.activeVoices() == 0);
}

// Per-channel routing: the held key belongs to the channel that pressed it; a track focused in between (tab click,
// automation, CC 84) does not keep it held.
TEST_CASE("Arpeggio: a key released on its channel after a focus change stops the arpeggio", "[engine][arp]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    auto t = track();
    t.arp.on = true;
    t.arp.mode = ymengine::ArpMode::Up;
    t.arp.rateHz = 48000.0 / 1024;
    t.patch.releaseMs = 50;
    SECTION("the newly focused track arpeggiates too") {
        rig.e.setTrack(0, t);
        rig.e.setTrack(1, t);
        rig.global([](auto& g) { g.focusTrack = 0; });
        rig.e.noteOn(0, 60, 100, 0);
        rig.run(4096);
        REQUIRE(rig.e.activeVoices() > 0);
        rig.global([](auto& g) { g.focusTrack = 1; });
        rig.run(1024);
        rig.e.noteOff(0, 60, 0);
        rig.run(48000);
        CHECK(rig.e.activeVoices() == 0);
    }
    SECTION("back on the first track after the release, nothing restarts") {
        rig.e.setTrack(0, t);
        rig.e.setTrack(1, track());
        rig.global([](auto& g) { g.focusTrack = 0; });
        rig.e.noteOn(0, 60, 100, 0);
        rig.run(4096);
        rig.global([](auto& g) { g.focusTrack = 1; });
        rig.run(1024);
        rig.e.noteOff(0, 60, 0);
        rig.run(4096);
        rig.global([](auto& g) { g.focusTrack = 0; });
        rig.run(48000);
        CHECK(rig.e.activeVoices() == 0);
    }
}

TEST_CASE("Arpeggio: step of the table played (astep), -1 outside Table mode or without a held note", "[engine][arp]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    auto t = track();
    t.arp.on = true;
    t.arp.mode = ymengine::ArpMode::Table;
    t.arp.table = {0, 4, 7};
    t.arp.length = 3;
    t.arp.rateHz = 48000.0 / 1024;
    rig.e.setTrack(0, t);
    REQUIRE(rig.e.arpStep() == -1);
    rig.e.noteOn(0, 60, 100);
    std::vector<int> steps, notes;
    for (int k = 0; k < 6; ++k) {
        rig.run(1024);
        steps.push_back(rig.e.arpStep());
        for (int i = 0; i < Engine::kVoices; ++i)
            if (rig.e.voice(i).on && !rig.e.voice(i).fading && rig.e.voice(i).tag == ymengine::VoiceTag::Arp)
                notes.push_back(rig.e.voice(i).note);
    }
    // The step shown is the one whose note sounds.
    REQUIRE(notes.size() == steps.size());
    for (size_t k = 0; k < steps.size(); ++k) {
        REQUIRE(steps[k] >= 0);
        REQUIRE(steps[k] < 3);
        REQUIRE(notes[k] == 60 + t.arp.table[size_t(steps[k])]);
        if (k) REQUIRE(steps[k] == (steps[k - 1] + 1) % 3);
    }
    t.arp.mode = ymengine::ArpMode::Up;
    rig.e.setTrack(0, t);
    REQUIRE(rig.e.arpStep() == -1);
    t.arp.mode = ymengine::ArpMode::Table;
    rig.e.setTrack(0, t);
    REQUIRE(rig.e.arpStep() >= 0);
    rig.e.noteOff(0, 60);
    rig.run(16);
    REQUIRE(rig.e.arpStep() == -1);
}

TEST_CASE("Arpeggio: MIDI clock, one step every `division` pulses", "[engine][arp]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    rig.global([](auto& g) {
        g.arpClock = ymengine::ArpClock::MidiClock;
        g.arpDivision = 12;  // half pulses (D-289): one sixteenth note, six clock pulses
    });
    auto t = track();
    t.arp.on = true;
    t.arp.mode = ymengine::ArpMode::Up;
    rig.e.setTrack(0, t);
    rig.e.noteOn(0, 60, 100);
    rig.e.noteOn(0, 64, 100);
    rig.e.midiStart(0);
    std::vector<int> notes;
    for (int pulse = 0; pulse < 24; ++pulse) {
        rig.e.midiClock(0);
        rig.run(100);
        for (int i = 0; i < Engine::kVoices; ++i)
            if (rig.e.voice(i).on && !rig.e.voice(i).fading && rig.e.voice(i).tag == ymengine::VoiceTag::Arp) {
                if (notes.empty() || pulse % 6 == 0) notes.push_back(rig.e.voice(i).note);
                break;
            }
    }
    // After MIDI Start, the first step falls on the first pulse.
    REQUIRE(notes == std::vector<int>{60, 64, 60, 64});
}

TEST_CASE("Arpeggio: 1/32T and 1/64 on the MIDI clock, a half pulse between two clocks (D-289)", "[engine][arp]") {
    for (const int division : {3, 4}) {  // half pulses: 1/64, 1/32T
        INFO("division " << division);
        Rig rig(ymengine::VoiceMode::Studio16);
        rig.global([division](auto& g) {
            g.arpClock = ymengine::ArpClock::MidiClock;
            g.arpDivision = uint8_t(division);
        });
        auto t = track();
        t.arp.on = true;
        t.arp.mode = ymengine::ArpMode::Up;
        rig.e.setTrack(0, t);
        rig.e.noteOn(0, 60, 100);
        rig.e.noteOn(0, 64, 100);
        rig.e.midiStart(0);
        // One clock every 1000 samples (125 BPM at 48 kHz): a step every 1500 samples (1/64) or 2000 (1/32T).
        auto arpNote = [&] {
            for (int i = 0; i < Engine::kVoices; ++i)
                if (rig.e.voice(i).on && !rig.e.voice(i).fading && rig.e.voice(i).tag == ymengine::VoiceTag::Arp)
                    return rig.e.voice(i).note;
            return -1;
        };
        std::vector<int> changes;
        int last = -1;
        for (int k = 0; k < 24000; k += 10) {
            if (k % 1000 == 0) rig.e.midiClock(0);
            rig.run(10);
            if (const int n = arpNote(); n != last) {
                changes.push_back(k);
                last = n;
            }
        }
        REQUIRE(changes.size() >= 10);
        CHECK(changes[0] == 0);  // the first step on the first pulse
        for (size_t i = 1; i < changes.size(); ++i)  // to the 10 samples of a run
            CHECK(std::abs(changes[i] - changes[i - 1] - division * 500) <= 10);
    }
}

TEST_CASE("Engine: change of mode during the render", "[engine]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    rig.e.setTrack(0, track());
    rig.e.noteOn(0, 60, 127);
    rig.e.noteOn(0, 67, 127);
    rig.run(10000);
    REQUIRE(rig.e.activeVoices() == 2);
    for (const auto mode : {ymengine::VoiceMode::Authentic3, ymengine::VoiceMode::Dual3, ymengine::VoiceMode::Studio16}) {
        rig.global([&](auto& g) { g.mode = mode; });
        REQUIRE(rig.e.activeVoices() == 0);
        rig.e.noteOn(0, 64, 127);
        rig.run(10000);
        REQUIRE(rig.e.activeVoices() == 1);
        REQUIRE(peak(std::vector<float>(rig.l.begin(), rig.l.begin() + 10000)) > 0.05);
        for (size_t i = 0; i < 10000; ++i) REQUIRE(std::isfinite(rig.l[i]));
        rig.e.allNotesOff(0);
        rig.run(1000);
        REQUIRE(rig.e.activeVoices() == 0);
    }
    REQUIRE(rig.e.samplesRendered() == rig.at);
}

TEST_CASE("Engine: beyond kMaxEvents per block, events are counted and ignored", "[engine]") {
    Rig rig(ymengine::VoiceMode::Studio16);
    for (uint32_t i = 0; i < Engine::kMaxEvents + 76; ++i) rig.e.noteOn(i % 64, 60, 100);
    rig.run(64);
    REQUIRE(rig.e.droppedEvents() == 76);
    // A MIDI note played again without release is played again: 16 track voices and 16 free voices taken.
    REQUIRE(rig.e.activeVoices() == Engine::kLaneVoices + Engine::kPoolVoices);
}

TEST_CASE("Master of the stereo outputs: right level unlinked, balance; mono outputs on Master (D-284)", "[engine]") {
    auto cfg = config();
    cfg.render.dc = ymcore::DcFilter::None;  // linear output: exact ratios
    auto render = [&](ymengine::VoiceMode mode, double master, double master2, double pan) {
        Rig rig(mode, cfg);
        rig.global([&](auto& g) {
            g.softClip = false;
            g.master = master;
            g.master2 = master2;
            g.masterPan = pan;
        });
        rig.e.setTrack(0, track());
        rig.e.noteOn(0, 69, 127);
        Stereo s;
        s.l.resize(4800);
        s.r.resize(4800);
        rig.e.render(s.l.data(), s.r.data(), 4800);
        return s;
    };
    auto scaled = [](const std::vector<float>& x, const std::vector<float>& ref, double k) {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::fabs(double(x[i]) - k * ref[i]) > 1e-6) return false;
        return true;
    };
    using ymengine::VoiceMode;
    const Stereo ref = render(VoiceMode::Studio16, 0.7, 0.7, 0);
    REQUIRE(peak(ref.l) > 0.01);
    REQUIRE(peak(ref.r) > 0.01);
    // Master 2 at half: the right output alone at half.
    const Stereo half = render(VoiceMode::Studio16, 0.7, 0.35, 0);
    CHECK(scaled(half.l, ref.l, 1));
    CHECK(scaled(half.r, ref.r, 0.5));
    // Balance: halfway to the right lowers the left output by half, fully left silences the right one.
    const Stereo right = render(VoiceMode::Studio16, 0.7, 0.7, 0.5);
    CHECK(scaled(right.l, ref.l, 0.5));
    CHECK(scaled(right.r, ref.r, 1));
    const Stereo left = render(VoiceMode::Studio16, 0.7, 0.7, -1);
    CHECK(scaled(left.l, ref.l, 1));
    CHECK(peak(left.r) == 0);
    // 3 channels with the ST: one mono output, on Master alone.
    const Stereo mono = render(VoiceMode::Authentic3, 0.7, 0.1, 1);
    const Stereo monoRef = render(VoiceMode::Authentic3, 0.7, 0.7, 0);
    REQUIRE(peak(monoRef.l) > 0.01);
    CHECK(mono.l == monoRef.l);
    CHECK(mono.r == mono.l);
}

TEST_CASE("Master per chip in 2x3 channels: chip 2 on Master 2, no balance (D-284)", "[engine]") {
    auto cfg = config();
    cfg.render.dc = ymcore::DcFilter::None;  // linear output: exact ratios
    auto render = [&](int tr, double master, double master2, double pan) {
        Rig rig(ymengine::VoiceMode::Dual3, cfg);
        rig.global([&](auto& g) {
            g.softClip = false;
            g.master = master;
            g.master2 = master2;
            g.masterPan = pan;
        });
        rig.e.setTrack(tr, track());
        rig.e.noteOn(0, 69, 127, tr);
        Stereo s;
        s.l.resize(4800);
        s.r.resize(4800);
        rig.e.render(s.l.data(), s.r.data(), 4800);
        return s;
    };
    auto scaled = [](const std::vector<float>& x, const std::vector<float>& ref, double k) {
        for (size_t i = 0; i < x.size(); ++i)
            if (std::fabs(double(x[i]) - k * ref[i]) > 1e-6) return false;
        return true;
    };
    // Track 1 plays on chip 1, track 5 on chip 2.
    const Stereo one = render(0, 0.7, 0.7, 0), two = render(4, 0.7, 0.7, 0);
    REQUIRE(peak(one.l) > 0.01);
    REQUIRE(peak(two.l) > 0.01);
    CHECK(one.r == one.l);  // the ST: mono
    CHECK(scaled(render(4, 0.7, 0.35, 0).l, two.l, 0.5));
    CHECK(scaled(render(0, 0.7, 0.35, 0).l, one.l, 1));
    CHECK(scaled(render(0, 0.35, 0.7, 0).l, one.l, 0.5));
    CHECK(scaled(render(4, 0.35, 0.7, 0).l, two.l, 1));
    // No balance: the pan of the stereo outputs leaves both chips as they are.
    CHECK(scaled(render(4, 0.7, 0.7, 1).l, two.l, 1));
}

TEST_CASE("Pitch bend during a slide: the voice stays at its place in the slide, bent", "[engine][bend]") {
    // Legato: the second note slides from the first one over 45 ms.
    auto rig = [] {
        auto r = std::make_unique<Rig>(ymengine::VoiceMode::Studio16);
        ymengine::TrackParams t = track();
        t.patch.mono = true;
        r->e.setTrack(0, t);
        return r;
    };
    auto sounding = [](const Engine& e) {
        for (int i = 0; i < Engine::kVoices; ++i)
            if (e.voice(i).on) return e.voice(i);
        return Engine::VoiceInfo{};
    };
    const auto r = rig();
    r->e.noteOn(0, 57, 127);
    r->run(480);
    r->e.noteOn(0, 69, 127);
    r->run(96);
    const double before = sounding(r->e).tonePeriod;
    REQUIRE(before > 0);
    r->e.pitchBend(0, 2.0);
    r->run(1);
    // Two semitones higher than a moment ago, far from the bent target note (71).
    const double after = sounding(r->e).tonePeriod;
    CHECK(std::abs(after * std::pow(2.0, 2.0 / 12) / before - 1) < 0.03);

    // At the end of the slide: the bent target note, as a note played there at once.
    r->run(4800);
    const auto plain = rig();
    plain->e.pitchBend(0, 2.0);
    plain->e.noteOn(0, 69, 127);
    plain->run(480);
    CHECK(std::abs(int(sounding(r->e).tonePeriod) - int(sounding(plain->e).tonePeriod)) <= 1);
}
