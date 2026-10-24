// STYM2149 — project instruments (M6, B1, D-147): capture of the sound of a track, exchange with the Patches library,
// loading into a track, INST chunk of the state, table played by the engine.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "plugincore/Driver.h"
#include "plugincore/Instruments.h"
#include "plugincore/Json.h"
#include "plugincore/Mapping.h"

using namespace stym;
using namespace stym::plugin;
using json::Value;
using ymengine::Generator;

namespace {

float& param(ParamValues& v, int track, Track f) { return v[size_t(paramIndex(track, f))]; }

// Track 3 set: SID in 3 channels (the parameter keeps the user's choice), ornament, arpeggio, level, gate.
void setTrack3(ParamValues& v, NonParamState& s) {
    v[size_t(paramIndex(Global::VoiceMode))] = 1;  // Authentic 3ch
    param(v, 2, Track::Generator) = float(Generator::Sid);
    param(v, 2, Track::SidRatio) = 2.37f;
    param(v, 2, Track::Volume) = 11;
    param(v, 2, Track::Noise) = 1;
    param(v, 2, Track::Attack) = 120;
    param(v, 2, Track::ArpOn) = 1;
    param(v, 2, Track::ArpRate) = 25;
    param(v, 2, Track::Level) = 40;
    param(v, 2, Track::Gate) = 80;
    s.tracks[2].patch.ornamentOn = true;
    s.tracks[2].patch.ornamentPitch[1] = -7;
    s.tracks[2].arp.mode = ymengine::ArpMode::Table;
    s.tracks[2].arp.table = {0, 12, 7};
    s.tracks[2].arp.length = 3;
}

}  // namespace

TEST_CASE("Instruments: sound of a track captured, library entry and back", "[instruments]") {
    ParamValues v = defaultValues();
    NonParamState s;
    setTrack3(v, s);
    const Instrument a = captureInstrument("  Lead SID  ", v, s, 2);
    CHECK(a.used);
    CHECK(a.name == "Lead SID");
    CHECK(a.patch.generator == Generator::Sid);  // not the imposed role in 3 channels (Chip Voice)
    CHECK(effectiveGenerator(v, 2) == Generator::Sid);
    CHECK(a.patch.sidRatio == double(2.37f));
    CHECK(a.patch.volume == 11);
    CHECK(a.patch.noiseOn);
    CHECK(a.patch.attackMs == 120);
    CHECK(a.patch.ornamentOn);
    CHECK(a.patch.ornamentPitch[1] == -7);
    CHECK(a.arp.on);
    CHECK(a.arp.rateHz == 25);
    CHECK(a.arp.mode == ymengine::ArpMode::Table);
    CHECK(a.arp.length == 3);
    CHECK(a.level == 0.4);
    CHECK(a.gate == 0.8);

    // Library entry: the patch of the prototype; read back, the same instrument.
    const PatchEntry e = instrumentEntry(a);
    CHECK(patchName(e) == "Lead SID");
    CHECK(e.find("patch")->find("effect")->number() == 1);
    CHECK(e.find("patch")->find("sidRatio")->number() == 2.37);
    CHECK(e.find("level")->number() == 0.4);
    CHECK(instrumentFromEntry(e) == a);
    // Same entry as a patch saved from the track.
    CHECK(json::dump(e) == json::dump(snapPatch("Lead SID", v, s, 2)));

    // Loaded into another track: the values of the source track.
    const PatchChange c = instrumentChange(a, s, 9);
    for (const auto& [i, x] : c.params) {
        const ParamSpec& sp = paramSpec(i);
        INFO(sp.id);
        CHECK(x == param(v, 2, Track(sp.field)));
    }
    CHECK(c.track.patch.ornamentPitch == s.tracks[2].patch.ornamentPitch);
    CHECK(c.track.arp.table == s.tracks[2].arp.table);
}

TEST_CASE("Instruments: names, incomplete entry, engine table", "[instruments]") {
    CHECK(cleanInstrumentName("Bass\t\xc3\xa9 01") == "Bass 01");
    CHECK(cleanInstrumentName("A very long instrument name") == "A very long inst");
    CHECK(cleanInstrumentName("Fifteen chars  X") == "Fifteen chars  X");
    CHECK(cleanInstrumentName("Fifteen chars   X") == "Fifteen chars");
    CHECK(cleanInstrumentName("   ") == "");
    Instrument blank;
    CHECK(instrumentLabel(blank, 0) == "Instrument 1");
    blank.name = "Kick";
    CHECK(instrumentLabel(blank, 31) == "Kick");

    // Missing keys: a new track (mkCh), generator included.
    Value e = Value::object();
    e.set("name", Value("Thin"));
    Value p = Value::object();
    p.set("vol4", Value(4));
    p.set("effect", Value(9));  // out of range: brought back into its own
    e.set("patch", std::move(p));
    const Instrument a = instrumentFromEntry(e);
    CHECK(a.used);
    CHECK(a.patch.volume == 4);
    CHECK(a.patch.generator == Generator::Dma);  // last type (M6), kept whatever the machine
    CHECK(a.patch.envPeriod == 12800);
    CHECK(a.arp.table == ymengine::TrackArp{}.table);
    CHECK(a.level == 0.75);
    CHECK(a.gate == 0.5);

    NonParamState s;
    s.instruments[4] = a;
    ymengine::InstrumentBank bank{};
    bank[0].used = true;
    fillInstruments(s, bank);
    CHECK_FALSE(bank[0].used);
    CHECK(bank[4].used);
    CHECK(bank[4].patch == a.patch);
    CHECK(bank[4].arp == a.arp);
}

TEST_CASE("Instruments: INST chunk of the state, written only if there are instruments", "[instruments][state]") {
    PluginState st;
    const std::vector<uint8_t> none = encodeState(st);
    auto hasInst = [](const std::vector<uint8_t>& b) {
        for (size_t i = 0; i + 4 <= b.size(); ++i)
            if (std::memcmp(b.data() + i, "INST", 4) == 0) return true;
        return false;
    };
    CHECK_FALSE(hasInst(none));

    ParamValues v = defaultValues();
    setTrack3(v, st.extras);
    st.extras.instruments[0] = captureInstrument("Lead", v, st.extras, 2);
    st.extras.instruments[31] = captureInstrument("", v, st.extras, 0);
    const std::vector<uint8_t> bytes = encodeState(st);
    CHECK(hasInst(bytes));
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.instruments == st.extras.instruments);
    CHECK(encodeState(back.state) == bytes);

    // Reading: slot outside the table, text that is not an instrument: ignored; a slot without a name stays used; an
    // unused instrument has kept nothing.
    std::vector<uint8_t> s = {'S', 'T', 'Y', 'M', 1, 0, 0, 0, 'I', 'N', 'S', 'T', 0, 0, 0, 0};
    auto put32 = [&](uint32_t x) {
        for (int i = 0; i < 4; ++i) s.push_back(uint8_t(x >> (8 * i)));
    };
    auto entry = [&](uint8_t slot, const std::string& text) {
        s.push_back(slot);
        put32(uint32_t(text.size()));
        s.insert(s.end(), text.begin(), text.end());
    };
    s.push_back(4);
    entry(40, R"({"name":"x","patch":{}})");
    entry(3, "[1,2]");
    entry(5, R"({"name":"éé","patch":{"vol4":99}})");
    entry(6, "{");
    const uint32_t len = uint32_t(s.size() - 16);
    for (int i = 0; i < 4; ++i) s[12 + size_t(i)] = uint8_t(len >> (8 * i));
    const DecodeResult r = decodeState(s);
    REQUIRE(r.ok);
    const auto& ins = r.state.extras.instruments;
    CHECK(std::count_if(ins.begin(), ins.end(), [](const Instrument& i) { return i.used; }) == 1);
    CHECK(ins[5].used);
    CHECK(ins[5].name.empty());
    CHECK(ins[5].patch.volume == 15);

    Instrument stale;
    stale.name = "gone";
    stale.patch.volume = 2;
    NonParamState n;
    n.instruments[7] = stale;
    sanitize(n);
    CHECK(n.instruments[7] == Instrument{});
}

TEST_CASE("Instruments: the driver passes the table to the engine, an INS step plays it", "[instruments][driver]") {
    Snapshot snap;
    ymseq::Lane& l = snap.state.seq.patterns[0].edit(0);
    l.pat[0] = 0;
    l.ins[0] = 2;
    snap.state.seq.patterns[0].steps = 1;
    Instrument& a = snap.state.instruments[1];
    a.used = true;
    a.patch.generator = Generator::Sid;
    a.patch.volume = 5;
    fillSequence(snap.state.seq, snap.seq);
    snap.patternSerial = 1;

    Driver d;
    ParamValues v = defaultValues();
    d.prepare({48000, 0}, v, snap);
    std::vector<float> left(512), right(512);
    auto run = [&](uint32_t n, std::vector<MidiEvent> midi = {}) {
        ymengine::Outputs o;
        o.left = left.data();
        o.right = right.data();
        d.process(v, midi, o, n, false, nullptr);
    };
    MidiEvent play{0, 0, {}};
    play.screen = true;
    play.command = UiCommand::SeqPlay;
    run(256, {play});
    REQUIRE(d.engine().voice(0).on);
    CHECK(d.engine().voice(0).volume == 5);
    CHECK(d.engine().voice(0).timerHz > 0);

    // Instrument deleted, copy published: the next step plays the sound of the track.
    snap.state.instruments[1] = Instrument{};
    d.publish(snap);
    for (int k = 0; k < 30; ++k) run(256);
    CHECK(d.engine().voice(0).volume == 13);
    CHECK(d.engine().voice(0).timerHz == 0);
}
