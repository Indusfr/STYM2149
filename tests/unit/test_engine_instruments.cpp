// STYM2149 — instruments of the engine: INS row of the melodic tracks, sound and arpeggio of the
// instrument, ties, chords, RTG, GEN row first, drums and roles of the tracks in 3-channel mode.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

constexpr uint64_t kStep = 6000;  // 120 BPM, 1/16, 48 kHz

struct Rig {
    Engine e;
    SeqData data;
    InstrumentBank bank{};
    std::shared_ptr<SeqLane> lane = std::make_shared<SeqLane>();
    std::vector<Engine::SeqLogRecord> log;
    GlobalParams g;
    TrackParams t;

    explicit Rig(VoiceMode mode = VoiceMode::Studio16) {
        EngineConfig cfg;
        cfg.render.hostRate = 48000;
        e.prepare(cfg);
        g.mode = mode;
        g.tempo = 120;
        e.setGlobal(g);
        t.mix.enabled = true;
        e.setTrack(0, t);
        log.reserve(100000);
        e.setSeqLog(&log);
        // Instrument 1: SID, volume 9, noise alone.
        Instrument& a = bank[0];
        a.used = true;
        a.patch.generator = Generator::Sid;
        a.patch.volume = 9;
        a.patch.toneOn = false;
        a.patch.noiseOn = true;
        e.setInstruments(&bank);
    }
    void steps(int n) {
        data.patterns[0].steps = n;
        data.patterns[0].lanes[0] = lane;
        e.setSequence(&data);
    }
    void note(int s, int rel, int ins = 0) {
        lane->pat[size_t(s)] = int16_t(rel);
        lane->ins[size_t(s)] = uint8_t(ins);
    }
    void render(uint64_t n) {
        static std::vector<float> l(512), r(512);
        while (n > 0) {
            const uint32_t k = uint32_t(std::min<uint64_t>(n, l.size()));
            e.render(l.data(), r.data(), k);
            n -= k;
        }
    }
    // Attacks of the sequencer ('T'): generator, chord voice, note.
    std::vector<Engine::SeqLogRecord> triggers() const {
        std::vector<Engine::SeqLogRecord> out;
        for (const auto& r : log)
            if (r.op == 'T') out.push_back(r);
        return out;
    }
};

}  // namespace

TEST_CASE("Instruments: INS plays the sound of the instrument, 00 and an empty slot that of the track",
          "[engine][seq][instruments]") {
    Rig r;
    r.note(0, 0);
    r.note(1, 2, 1);
    r.note(2, 4, 5);  // empty slot
    r.note(3, 5, 1);
    r.lane->fx[3] = 1;  // GEN Chip Voice: takes precedence over the generator of the instrument
    r.steps(4);
    r.e.seqPlay(0);
    struct Want {
        Generator gen;
        uint8_t volume;
        bool noise;
    };
    const Want want[] = {{Generator::Chip, 13, false},
                         {Generator::Sid, 9, true},
                         {Generator::Chip, 13, false},
                         {Generator::Chip, 9, true}};
    for (int s = 0; s < 4; ++s) {
        r.render(kStep / 4);
        const Engine::VoiceInfo v = r.e.voice(0);
        INFO("step " << s);
        REQUIRE(v.on);
        CHECK(v.volume == want[s].volume);
        CHECK(v.noise == want[s].noise);
        CHECK((v.timerHz > 0) == (want[s].gen == Generator::Sid));
        r.render(kStep - kStep / 4);
    }
    const auto t = r.triggers();
    REQUIRE(t.size() == 4);
    for (int s = 0; s < 4; ++s) CHECK(t[size_t(s)].a == int(want[s].gen));

    // Without an instrument table: the sound of the track everywhere.
    r.e.seqStop(0);
    r.render(kStep);
    r.e.setInstruments(nullptr);
    r.log.clear();
    r.e.seqPlay(0);
    r.render(kStep / 4);
    r.render(kStep);
    CHECK(r.e.voice(0).volume == 13);
    CHECK(r.triggers()[1].a == int(Generator::Chip));
}

TEST_CASE("Instruments: a tie keeps the mix of the instrument, a slide its starting instrument",
          "[engine][seq][instruments]") {
    Rig r;
    r.note(0, 0, 1);
    r.lane->mix[0] = 1;  // SRC Tone on the attack
    r.lane->tie[0] = r.lane->tie[1] = 1;
    r.lane->slide[2] = 1;
    r.note(2, 0, 1);
    r.note(3, 7);         // reached by the slide: no new attack, instrument 1 continues
    r.steps(4);
    r.e.seqPlay(0);
    r.render(kStep / 2);
    CHECK_FALSE(r.e.voice(0).noise);  // SRC Tone
    r.render(kStep);
    // Tie without SRC: back to the mix of the instrument (noise alone), not to that of the track.
    std::vector<Engine::SeqLogRecord> mixes;
    for (const auto& x : r.log)
        if (x.op == 'M') mixes.push_back(x);
    REQUIRE(mixes.size() >= 2);
    CHECK(mixes.back().a == 0);
    CHECK(mixes.back().b == 1);
    CHECK(r.e.voice(0).noise);
    r.render(2 * kStep);
    const auto t = r.triggers();
    REQUIRE(t.size() == 2);  // step 0 and step 2; step 3 is the end of the slide
    CHECK(t[1].a == int(Generator::Sid));
    CHECK(r.e.voice(0).volume == 9);
    CHECK(r.e.voice(0).timerHz > 0);
}

TEST_CASE("Instruments: the arpeggio of the ARP step is that of the instrument", "[engine][seq][instruments]") {
    Rig r;
    Instrument& a = r.bank[1];  // instrument 2: arpeggio 0, +12 at 50 Hz
    a.used = true;
    a.arp.on = true;
    a.arp.rateHz = 50;
    a.arp.table = {0, 12};
    a.arp.length = 2;
    r.note(0, 0, 2);
    r.lane->setCmd(0, seqCmd(SeqCmd::Arpeggiator, 1));  // J01: the arpeggiator from step 0 on
    r.note(1, 0);  // sound of the track, whose arpeggio is off: ARP has no effect
    r.steps(2);
    r.e.seqPlay(0);
    r.render(kStep - 10);
    const auto t = r.triggers();
    REQUIRE(t.size() >= 5);  // 6 000 samples at 50 Hz: one tick every 960
    for (size_t k = 0; k < t.size(); ++k) CHECK(t[k].x == (k % 2 == 0 ? 48 : 60));
    r.log.clear();
    r.render(kStep);
    REQUIRE(r.triggers().size() == 1);
    CHECK(r.triggers()[0].x == 48);

    // The arpeggio of the track does not play an instrument step whose arpeggio is off.
    r.t.arp.on = true;
    r.t.arp.table = {0, 7};
    r.t.arp.length = 2;
    r.e.setTrack(0, r.t);
    a.arp.on = false;
    r.log.clear();
    r.render(kStep - 10);
    CHECK(r.triggers().size() == 1);
    r.log.clear();
    r.render(kStep);
    CHECK(r.triggers().size() > 2);  // step 2: sound of the track, its arpeggio runs
}

TEST_CASE("Instruments: chord voices and RTG hits take the instrument of the step", "[engine][seq][instruments]") {
    Rig r;
    r.note(0, 0, 1);
    r.lane->vpat[0][0] = 4;
    r.lane->rtg[0] = 1;  // two hits
    r.steps(1);
    r.e.seqPlay(0);
    r.render(kStep - 10);
    const auto t = r.triggers();
    REQUIRE(t.size() == 3);  // root, chord voice, second hit
    for (const auto& x : t) CHECK(x.a == int(Generator::Sid));
    CHECK(t[1].b == 1);
    CHECK(r.e.voice(Engine::kLaneVoices).volume == 9);

    // Monophonic instrument: no chord voice.
    r.e.seqStop(0);
    r.render(kStep);
    r.bank[0].patch.mono = true;
    r.log.clear();
    r.e.seqPlay(0);
    r.render(kStep / 4);
    for (const auto& x : r.triggers()) CHECK(x.b == 0);
}

TEST_CASE("Instruments: drums without INS, instrument Digidrum in 3-channel mode left to the track",
          "[engine][seq][instruments]") {
    SECTION("Chipdrum track") {
        Rig r;
        r.t.patch.generator = Generator::Chipdrum;
        r.e.setTrack(0, r.t);
        r.note(0, 0, 1);
        r.steps(1);
        r.e.seqPlay(0);
        r.render(100);
        REQUIRE(r.triggers().size() == 1);
        CHECK(r.triggers()[0].a == int(Generator::Chipdrum));
    }
    SECTION("3-channel") {
        Rig r(VoiceMode::Authentic3);
        r.bank[0].patch.generator = Generator::Digidrum;
        r.note(0, 0, 1);
        r.note(1, 0, 1);
        r.lane->fx[1] = 2;  // GEN SID Voice
        r.steps(2);
        r.e.seqPlay(0);
        r.render(100);
        REQUIRE(r.triggers().size() == 1);
        CHECK(r.triggers()[0].a == int(Generator::Chip));
        CHECK(r.e.voice(0).on);
        CHECK(r.e.voice(0).noise);  // the rest of the sound comes from the instrument
        r.render(kStep);
        REQUIRE(r.triggers().size() == 2);
        CHECK(r.triggers()[1].a == int(Generator::Sid));
    }
    SECTION("16-channel: instrument Digidrum on a melodic track") {
        Rig r;
        r.bank[0].patch.generator = Generator::Digidrum;
        const std::vector<uint8_t> smp(7800, 8);
        r.e.setUserSample(2, smp);
        r.note(0, 2, 1);  // D: row 2, slot 3
        r.note(1, 0);
        r.steps(2);
        r.e.seqPlay(0);
        r.render(100);
        REQUIRE(r.triggers().size() == 1);
        CHECK(r.triggers()[0].a == int(Generator::Digidrum));
        CHECK(r.e.voice(0).on);
        r.render(kStep);
        // The next step plays: the voice of the track is not a stolen channel.
        REQUIRE(r.triggers().size() == 2);
        CHECK(r.triggers()[1].a == int(Generator::Chip));
    }
}

namespace {
// Voice played live (MIDI) on track `track`, the most recent one.
Engine::VoiceInfo liveVoice(const Engine& e, int track) {
    Engine::VoiceInfo best;
    for (int i = 0; i < Engine::kVoices; ++i)
        if (const Engine::VoiceInfo v = e.voice(i); v.on && v.tag == VoiceTag::Manual && v.track == track &&
                                                    (!best.on || v.age > best.age))
            best = v;
    return best;
}
}  // namespace

TEST_CASE("Program Change: notes played live take the chosen instrument, 0 gives back the sound of the track",
          "[engine][instruments][program]") {
    Rig r;
    auto play = [&](int note) {
        r.e.noteOn(0, note, 100);
        r.render(256);
        const Engine::VoiceInfo v = liveVoice(r.e, 0);
        r.e.noteOff(0, note);
        r.render(4800);
        return v;
    };
    Engine::VoiceInfo v = play(60);
    REQUIRE(v.on);
    CHECK(v.volume == 13);
    CHECK_FALSE(v.noise);
    CHECK(r.e.program(0) == 0);

    r.e.programChange(0, 1);
    v = play(62);
    REQUIRE(v.on);
    CHECK(r.e.program(0) == 1);
    CHECK(v.volume == 9);
    CHECK(v.noise);
    CHECK_FALSE(v.tone);
    CHECK(v.timerHz > 0);  // SID

    r.e.programChange(0, 40);  // above 32: ignored
    CHECK(r.e.program(0) == 1);
    r.e.programChange(0, 5);  // empty slot: the sound of the track
    v = play(64);
    CHECK(v.volume == 13);
    r.e.programChange(0, 0);
    v = play(65);
    CHECK(r.e.program(0) == 0);
    CHECK(v.volume == 13);
    CHECK(v.timerHz == 0);

    // The notes that are sounding keep their sound.
    r.e.noteOn(0, 67, 100);
    r.render(256);
    r.e.programChange(0, 1);
    r.render(256);
    CHECK(liveVoice(r.e, 0).volume == 13);
}

TEST_CASE("Program Change: per track, sequencer steps with INS 00, INS of the step and drums",
          "[engine][seq][instruments][program]") {
    Rig r;
    r.bank[1].used = true;  // instrument 2: Sync Buzzer
    r.bank[1].patch.generator = Generator::SyncBuzzer;
    TrackParams other = r.t;
    r.e.setTrack(1, other);
    // Track 2 alone: track 1 keeps its sound.
    r.e.programChange(0, 1, 1);
    r.render(16);
    CHECK(r.e.program(0) == 0);
    CHECK(r.e.program(1) == 1);
    r.e.noteOn(0, 60, 100, 0);
    r.e.noteOn(0, 64, 100, 1);
    r.render(256);
    CHECK(liveVoice(r.e, 0).timerHz == 0);
    CHECK(liveVoice(r.e, 1).timerHz > 0);
    r.e.allNotesOff(0);
    r.render(4800);

    // Sequencer, track 1: INS 00 plays the chosen instrument; INS 02 of the step keeps precedence.
    r.e.programChange(0, 1, 0);
    r.note(0, 0);
    r.note(1, 2, 2);
    r.note(2, 4);
    r.steps(3);
    r.e.seqPlay(0);
    r.render(100);
    r.render(2 * kStep);
    auto t = r.triggers();
    REQUIRE(t.size() == 3);
    CHECK(t[0].a == int(Generator::Sid));
    CHECK(t[1].a == int(Generator::SyncBuzzer));
    CHECK(t[2].a == int(Generator::Sid));
    r.e.seqStop(0);
    r.render(kStep);

    // Drum track: no instrument (neither live nor in the sequencer).
    r.t.patch.generator = Generator::Chipdrum;
    r.e.setTrack(0, r.t);
    r.log.clear();
    r.e.seqPlay(0);
    r.render(100);
    t = r.triggers();
    REQUIRE_FALSE(t.empty());
    CHECK(t[0].a == int(Generator::Chipdrum));
    r.e.seqStop(0);
    r.render(kStep);
    r.e.noteOn(0, 60, 100, 0);
    r.render(256);
    CHECK(liveVoice(r.e, 0).gen == Generator::Chipdrum);
}

TEST_CASE("Program Change: keyboard arpeggio of the instrument, engine reset",
          "[engine][instruments][program]") {
    Rig r;
    r.bank[0].arp.on = true;
    r.bank[0].arp.rateHz = 20;
    r.e.programChange(0, 1);
    r.e.noteOn(0, 60, 100);
    r.e.noteOn(0, 64, 100);
    r.render(48000 / 4);
    bool arp = false;
    for (int i = 0; i < Engine::kVoices; ++i)
        if (const Engine::VoiceInfo v = r.e.voice(i); v.on && v.tag == VoiceTag::Arp) arp = v.timerHz > 0;
    CHECK(arp);  // the arpeggio of the instrument, with its sound (SID)
    r.e.reset();
    CHECK(r.e.program(0) == 0);
}
