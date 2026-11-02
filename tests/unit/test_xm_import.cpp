// STYM2149 — plugincore: import of XM modules: notes, ties, volume, panning, the step
// effects (Axy, 9xx, EDx, ECx, E5x; 0xy, 1xx and 2xx left out), Song (jumps, pattern ends,
// restart), Ticks or BPM timing, instruments and DMA slots, render of the project.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "XmBuild.h"
#include "plugincore/Mapping.h"
#include "plugincore/Offline.h"
#include "plugincore/XmImport.h"

using namespace stym;
using plugin::importXm;
using plugin::NonParamState;
using plugin::ParamValues;
using plugin::XmImport;
using ymengine::SeqCmd;
using ymengine::VoiceMode;
using ymio::XmCell;
using ymio::XmModule;
using ymio::XmSample;

namespace {

XmSample tone(uint32_t frames, uint8_t loopType = 0, uint32_t loopStart = 0, uint32_t loopLength = 0,
              int8_t relative = 0) {
    XmSample s;
    s.name = "tone";
    s.volume = 48;
    s.relativeNote = relative;
    s.loopType = loopType;
    s.loopStart = loopStart;
    s.loopLength = loopLength;
    s.data.resize(frames);
    for (uint32_t i = 0; i < frames; ++i) s.data[i] = int16_t((i / 8 % 2 ? 64 : -64) * 256);
    return s;
}

ymio::XmInstrument instrument(const std::string& name, std::vector<XmSample> samples) {
    ymio::XmInstrument ins;
    ins.name = name;
    ins.samples = std::move(samples);
    return ins;
}

XmModule module(int channels, std::vector<int> rows) {
    XmModule m;
    m.name = "xm test";
    m.tracker = "FastTracker v2.00";
    m.channels = channels;
    m.tempo = 6;
    m.bpm = 125;
    m.order = {0};
    for (int n : rows) {
        ymio::XmPattern p;
        p.rows = n;
        p.cells.assign(size_t(n * channels), XmCell{});
        m.patterns.push_back(p);
    }
    return m;
}

void put(XmModule& m, int p, int row, int c, XmCell cell) {
    m.patterns[size_t(p)].cells[size_t(row * m.channels + c)] = cell;
}

XmImport run(const XmModule& m, VoiceMode mode, NonParamState& st) {
    plugin::XmImportResult r = importXm(test::buildXm(m), mode, st);
    INFO(r.error);
    REQUIRE(r.import);
    return *r.import;
}

bool has(const XmImport& r, const std::string& line) {
    return std::find(r.notes.begin(), r.notes.end(), line) != r.notes.end();
}

}  // namespace

TEST_CASE("XM import: notes, ties, release, volume, panning; 0xy left out", "[xmimport]") {
    XmModule m = module(3, {8});
    m.instruments = {instrument("lead", {tone(1000, 1, 0, 1000)})};
    put(m, 0, 0, 0, {49, 1, 0, 0, 0});           // C-4 at the sample's volume (48)
    put(m, 0, 3, 0, {ymio::kXmKeyOff, 0, 0, 0, 0});
    put(m, 0, 4, 0, {64, 1, 0x30, 0x0, 0x37});   // D#5, volume 32, arpeggio 037: left out
    put(m, 0, 1, 1, {41, 1, 0, 0x8, 0xFF});      // E-3 on the right
    put(m, 0, 5, 1, {0, 0, 0, 0xC, 0x20});       // volume during the note
    put(m, 0, 6, 1, {0, 0, 0, 0x3, 0x05});       // portamento: left out
    put(m, 0, 2, 2, {0, 0, 0, 0x1, 0x10});       // channel without notes: ignored
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    CHECK(r.channels == 2);
    CHECK(r.on[4]);  // tracks 5 and 6: 8 channels or fewer
    CHECK(r.on[5]);
    CHECK_FALSE(r.on[6]);
    CHECK_FALSE(r.on[0]);
    CHECK(r.patterns == 1);
    CHECK(r.instruments == 1);
    CHECK(r.title == "xm test");

    const ymseq::Pattern& p = st.seq.patterns[0];
    CHECK(p.steps == 8);
    const ymseq::Lane& a = p.lane(4);
    CHECK(a.pat[0] == 0);
    CHECK(a.oct[0] == 4);  // octave 4, track at octave 3
    CHECK(a.ins[0] == 1);
    CHECK(a.vol[0] == 11);  // 48 × 15 / 64
    CHECK((a.tie[0] && a.tie[1] && a.tie[2]));
    CHECK((a.vol[1] == 11 && a.vol[2] == 11));  // the volume of the note over its held lines
    CHECK(a.tie[3] == 0);  // release
    CHECK(a.pat[3] == -1);
    CHECK(a.pat[4] == 3);
    CHECK(a.oct[4] == 5);
    CHECK(a.vol[4] == 8);  // 32 × 15 / 64 rounded
    CHECK_FALSE(a.hasCmd(4));
    CHECK(a.cmdAt(SeqCmd::Arp, 4) == 0);
    CHECK((a.tie[5] && a.tie[6] && a.tie[7]));
    CHECK(std::all_of(a.pnr.begin(), a.pnr.end(), [](uint8_t v) { return v == 0; }));  // channel in the centre

    const ymseq::Lane& b = p.lane(5);
    CHECK(b.tie[0] == 1);  // note from the previous position: tied
    CHECK(b.pat[1] == 4);
    CHECK(b.oct[1] == 0);  // octave 3: that of the track
    CHECK(b.pnr[0] == ymseq::kPanCenter);
    CHECK(b.pnr[1] == 201);  // hard right
    CHECK(b.pnr[7] == 201);  // panning held over the lines of the note
    CHECK(b.vol[4] == 11);
    CHECK((b.vol[5] == 8 && b.vol[7] == 8));  // C20 during the note: VOL of the held lines
    CHECK(p.lane(6).empty());

    CHECK(r.ticks);
    CHECK(r.vblRate == 0);  // 125 BPM: 50 Hz
    CHECK(r.speed == 6);
    CHECK(st.global.timing == ymengine::SeqTiming::Ticks);
    CHECK(st.global.speed == 6);
    const plugin::Instrument& ins = st.instruments[0];
    CHECK(ins.used);
    CHECK(ins.name == "lead");
    CHECK(ins.patch.generator == ymengine::Generator::Dma);
    CHECK(ins.patch.dmaSlot == 0);
    CHECK(st.dma.slots[0] == ymengine::DmaSlot{0, false, 60, true});
    CHECK(st.dmaSamples[0].size() == 748);  // 1000 frames from 8363 Hz to 6258 Hz
    CHECK(st.dmaSamples[0][0] < 0);
    CHECK(st.dmaNames[0] == "tone");  // the name of the sample
    REQUIRE(st.seq.song.size() == 1);
    CHECK(st.seq.song.entries[0] == ymseq::SongEntry{0, 1, 0, ymseq::SongKind::Pattern, 0, false});
    CHECK(st.seq.used[0]);
    CHECK(has(r, "Effects left out: 0xx (1), 3xx (1)."));
    CHECK(r.notes.size() == 1);

    const std::vector<std::string> msg = plugin::xmImportMessage(r, "test.xm");
    REQUIRE(msg.size() >= 6);
    CHECK(msg[0] == "XM imported");
    CHECK(msg[2] == "test.xm");
    CHECK(msg[3] == "xm test");
    CHECK(msg[4] == "2 tracks, 1 pattern, 1 DMA instrument, 1 Song entry.");
    CHECK(msg[5] == "Ticks: speed 6 at 50 Hz, STE machine.");
    CHECK(msg[7] == "Not as in the module:");
    CHECK(msg[8] == "• Effects left out: 0xx (1), 3xx (1).");
}

TEST_CASE("XM import: Axy, 9xx, EDx, ECx and E5x as commands of the CMD row; 1xx and 2xx left out",
          "[xmimport]") {
    using ymengine::seqCmd;
    XmModule m = module(1, {10});
    m.instruments = {instrument("lead", {tone(1000, 1, 0, 1000)})};
    put(m, 0, 0, 0, {49, 1, 0, 0xA, 0x04});  // volume slide down 4
    put(m, 0, 1, 0, {0, 0, 0, 0xA, 0x00});   // A00: the previous parameter
    put(m, 0, 2, 0, {0, 0, 0, 0x0, 0x00});   // no slide: stopped
    put(m, 0, 3, 0, {0, 0, 0, 0x1, 0x20});   // portamento up 32: left out
    put(m, 0, 4, 0, {0, 0, 0, 0x2, 0x03});   // portamento down 3: left out
    put(m, 0, 5, 0, {49, 1, 0, 0x9, 0x10});  // offset 16 × 256 frames at 8363 Hz
    put(m, 0, 6, 0, {49, 1, 0, 0xE, 0xD3});  // note delay 3
    put(m, 0, 7, 0, {49, 1, 0, 0xE, 0x52});  // fine tune 2: -6/8 semitone
    put(m, 0, 8, 0, {0, 0, 0, 0xE, 0xC2});   // note cut 2
    put(m, 0, 9, 0, {49, 1, 0, 0x0, 0x00});  // a note without E5x: the sample's tune again
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    const ymseq::Lane& a = st.seq.patterns[0].lane(4);
    CHECK(a.cmdAt(SeqCmd::VolSlide, 0) == seqCmd(SeqCmd::VolSlide, 0x04));
    CHECK(a.cmdAt(SeqCmd::VolSlide, 1) == seqCmd(SeqCmd::VolSlide, 0x04));
    CHECK(a.cmdAt(SeqCmd::VolSlide, 2) == seqCmd(SeqCmd::VolSlide, 0x00));
    CHECK(a.cmdAt(SeqCmd::VolSlide, 3) == 0);
    CHECK_FALSE(a.hasCmd(3));
    CHECK_FALSE(a.hasCmd(4));
    CHECK(a.cmdAt(SeqCmd::PitchSlide, 3) == 0);
    CHECK(a.cmdAt(SeqCmd::Offset, 5) == seqCmd(SeqCmd::Offset, 12));  // at 6258 Hz: 16 × 6258 / 8363
    CHECK(a.cmdAt(SeqCmd::Delay, 6) == seqCmd(SeqCmd::Delay, 3));
    CHECK(a.cmdAt(SeqCmd::Fine, 7) == seqCmd(SeqCmd::Fine, 100 - 75));
    CHECK(a.cmdAt(SeqCmd::Cut, 8) == seqCmd(SeqCmd::Cut, 2));
    CHECK(a.cmdAt(SeqCmd::Fine, 9) == seqCmd(SeqCmd::Fine, 100));
    CHECK(has(r, "Effects left out: 1xx (1), 2xx (1)."));
}

TEST_CASE("XM import: tracks of the mode (STE DMA)", "[xmimport]") {
    CHECK(plugin::xmImportTracks(VoiceMode::Authentic3) == std::vector<int>{4, 5, 6, 7, 8, 9, 10, 11});
    CHECK(plugin::xmImportTracks(VoiceMode::Dual3) == std::vector<int>{8, 9, 10, 11, 12, 13, 14, 15});
    CHECK(plugin::xmImportTracks(VoiceMode::Studio16).size() == 16);
    CHECK(plugin::xmImportTracks(VoiceMode::Studio32).size() == 32);

    XmModule m = module(10, {4});
    m.instruments = {instrument("x", {tone(100)})};
    for (int c = 0; c < 10; ++c) put(m, 0, 0, c, {49, 1, 0, 0, 0});
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Authentic3, st);
    CHECK(r.channels == 8);
    for (int t = 0; t < 16; ++t) CHECK(r.on[size_t(t)] == (t >= 4 && t < 12));
    CHECK(st.seq.patterns[0].lane(4).ins[0] == 1);
    CHECK(has(r, "Channels beyond the 8 DMA tracks: 2 left out."));
}

TEST_CASE("XM import: a module of more channels than the mode has tracks asks for 16 or 32 ch",
          "[xmimport]") {
    XmModule m = module(12, {4});
    m.instruments = {instrument("x", {tone(100)})};
    for (int c = 0; c < 10; ++c) put(m, 0, 0, c, {49, 1, 0, 0, 0});  // 10 of the 12 channels play
    NonParamState a;
    const XmImport three = run(m, VoiceMode::Authentic3, a);
    CHECK(three.played == 10);
    CHECK(three.channels == 8);
    CHECK(three.mode == VoiceMode::Authentic3);
    CHECK(plugin::xmWantsStudio(three));
    CHECK(plugin::xmStudioMode(three) == VoiceMode::Studio16);  // 16 ch plays its 10 channels

    NonParamState b;
    const XmImport sixteen = run(m, VoiceMode::Studio16, b);
    CHECK(sixteen.played == 10);
    CHECK(sixteen.channels == 10);
    CHECK_FALSE(plugin::xmWantsStudio(sixteen));
    for (int t = 0; t < 16; ++t) CHECK(sixteen.on[size_t(t)] == (t < 10));

    // The settings follow the mode of the result, whatever the project was in.
    ParamValues v = plugin::defaultValues();
    v[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 1.0f;  // Authentic 3ch
    plugin::xmImportValues(sixteen, v);
    CHECK(plugin::voiceMode(v) == VoiceMode::Studio16);
    CHECK(plugin::intValue(v, 9, plugin::Track::Generator) == int(ymengine::Generator::Dma));
    plugin::xmImportValues(three, v);
    CHECK(plugin::voiceMode(v) == VoiceMode::Authentic3);

    // Eight channels or fewer: no question.
    XmModule eight = module(8, {4});
    eight.instruments = {instrument("x", {tone(100)})};
    for (int c = 0; c < 8; ++c) put(eight, 0, 0, c, {49, 1, 0, 0, 0});
    NonParamState c;
    CHECK_FALSE(plugin::xmWantsStudio(run(eight, VoiceMode::Dual3, c)));

    // More than 16: 16 ch asks too, for 32 ch; 32 ch never asks.
    XmModule wide = module(20, {4});
    wide.instruments = {instrument("x", {tone(100)})};
    for (int ch = 0; ch < 20; ++ch) put(wide, 0, 0, ch, {49, 1, 0, 0, 0});
    NonParamState d, e, f;
    const XmImport in16 = run(wide, VoiceMode::Studio16, d);
    CHECK(in16.channels == 16);
    CHECK(plugin::xmWantsStudio(in16));
    CHECK(plugin::xmStudioMode(in16) == VoiceMode::Studio32);
    CHECK(plugin::xmStudioMode(run(wide, VoiceMode::Authentic3, e)) == VoiceMode::Studio32);
    const XmImport in32 = run(wide, VoiceMode::Studio32, f);
    CHECK(in32.channels == 20);
    CHECK_FALSE(plugin::xmWantsStudio(in32));
    for (int t = 0; t < plugin::kTracks; ++t) CHECK(in32.on[size_t(t)] == (t < 20));
    plugin::xmImportValues(in32, v);
    CHECK(plugin::voiceMode(v) == VoiceMode::Studio32);
}

TEST_CASE("XM import: 8 channels or fewer in 16 or 32 ch go to tracks 5 to 12, those of the DMA in 3ch",
          "[xmimport]") {
    CHECK(plugin::xmImportTracks(VoiceMode::Studio16, 8) == std::vector<int>{4, 5, 6, 7, 8, 9, 10, 11});
    CHECK(plugin::xmImportTracks(VoiceMode::Studio16, 9).front() == 0);
    CHECK(plugin::xmImportTracks(VoiceMode::Authentic3, 4) == std::vector<int>{4, 5, 6, 7, 8, 9, 10, 11});

    XmModule m = module(4, {4});
    m.instruments = {instrument("x", {tone(100)})};
    for (int c = 0; c < 4; ++c) put(m, 0, 0, c, {49, 1, 0, 0, 0});
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    CHECK(r.channels == 4);
    for (int t = 0; t < plugin::kTracks; ++t) CHECK(r.on[size_t(t)] == (t >= 4 && t < 8));
    CHECK(st.seq.patterns[0].lane(4).ins[0] == 1);
    CHECK(st.seq.patterns[0].lane(7).ins[0] == 1);
    ParamValues v = plugin::defaultValues();
    plugin::xmImportValues(r, v);
    CHECK(plugin::intValue(v, 4, plugin::Track::Generator) == int(ymengine::Generator::Dma));
    CHECK(plugin::intValue(v, 0, plugin::Track::On) == 0);

    // Nine channels: from track 1.
    XmModule nine = module(9, {4});
    nine.instruments = {instrument("x", {tone(100)})};
    for (int c = 0; c < 9; ++c) put(nine, 0, 0, c, {49, 1, 0, 0, 0});
    NonParamState st9;
    const XmImport r9 = run(nine, VoiceMode::Studio16, st9);
    for (int t = 0; t < plugin::kTracks; ++t) CHECK(r9.on[size_t(t)] == (t < 9));
}

TEST_CASE("XM import: channels beyond the tracks share those of channels that never play at the same time",
          "[xmimport]") {
    // 32 channels play a short sample (100 frames: 12 ms, a line lasting 120 ms) on line 0, two more on line 2 (a
    // module of more than 32 channels: 64 at most in an XM).
    XmModule m = module(34, {4});
    m.instruments = {instrument("x", {tone(100)})};
    for (int c = 0; c < 32; ++c) put(m, 0, 0, c, {49, 1, 0, 0, 0});
    put(m, 0, 2, 32, {61, 1, 0, 0, 0});
    put(m, 0, 2, 33, {63, 1, 0, 0, 0});
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio32, st);
    CHECK(r.played == 34);
    CHECK(r.channels == 32);
    CHECK(r.shared == 2);
    // Channel 32 on the track of channel 0, channel 33 on that of channel 1 (channel 32 sounds on line 2 of track 1).
    const ymseq::Lane& a = st.seq.patterns[0].lane(0);
    const ymseq::Lane& b = st.seq.patterns[0].lane(1);
    CHECK(a.pat[0] == 0);
    CHECK(a.pat[2] == 0);  // C-5 (61): C of octave 5
    CHECK(a.tie[1] == 0);  // channel 0's sample has ended: the line is left to the other channel
    CHECK(b.pat[2] == 2);  // D-5 (63)
    CHECK(has(r, "Channels on the track of one that never plays at the same time: 2."));
    CHECK_FALSE(has(r, "Channels beyond the 32 DMA tracks: 2 left out."));

    // A looping sample sounds until its release: a channel that plays while every track sounds is left out.
    XmModule loop = module(33, {4});
    loop.instruments = {instrument("pad", {tone(1000, 1, 0, 1000)})};
    for (int c = 0; c < 32; ++c) put(loop, 0, 0, c, {49, 1, 0, 0, 0});
    put(loop, 0, 2, 32, {61, 1, 0, 0, 0});
    NonParamState st2;
    const XmImport r2 = run(loop, VoiceMode::Studio32, st2);
    CHECK(r2.channels == 32);
    CHECK(r2.shared == 0);
    CHECK(has(r2, "Channels beyond the 32 DMA tracks: 1 left out."));

    // Released on line 1, channel 0 leaves its track to channel 32 from line 2.
    put(loop, 0, 1, 0, {ymio::kXmKeyOff, 0, 0, 0, 0});
    NonParamState st3;
    const XmImport r3 = run(loop, VoiceMode::Studio32, st3);
    CHECK(r3.shared == 1);
    CHECK(st3.seq.patterns[0].lane(0).tie[0] == 1);
    CHECK(st3.seq.patterns[0].lane(0).tie[1] == 0);
    CHECK(st3.seq.patterns[0].lane(0).pat[2] == 0);

    // In 3ch, the 8 DMA tracks are shared the same way.
    XmModule ten = module(10, {4});
    ten.instruments = {instrument("x", {tone(100)})};
    for (int c = 0; c < 8; ++c) put(ten, 0, 0, c, {49, 1, 0, 0, 0});
    put(ten, 0, 2, 8, {49, 1, 0, 0, 0});
    put(ten, 0, 3, 9, {49, 1, 0, 0, 0});
    NonParamState st4;
    const XmImport r4 = run(ten, VoiceMode::Authentic3, st4);
    CHECK(r4.channels == 8);
    CHECK(r4.shared == 2);
    CHECK(st4.seq.patterns[0].lane(4).ins[2] == 1);
    CHECK(st4.seq.patterns[0].lane(4).ins[3] == 1);  // channel 9 sounds after channel 8's sample has ended
}

TEST_CASE("XM import: Song walked like Fasttracker II (jumps, pattern ends, restart)", "[xmimport]") {
    SECTION("backward jump: restart point") {
        XmModule m = module(1, {4, 8, 8});
        m.instruments = {instrument("x", {tone(100)})};
        m.order = {0, 0, 1, 2};
        for (int p = 0; p < 3; ++p) put(m, p, 0, 0, {49, 1, 0, 0, 0});
        put(m, 1, 1, 0, {0, 0, 0, 0xD, 0x00});  // end of the pattern at line 1
        put(m, 2, 2, 0, {0, 0, 0, 0xB, 0x01});  // back to position 1
        put(m, 2, 5, 0, {60, 1, 0, 0, 0});      // after the jump: never played
        NonParamState st;
        const XmImport r = run(m, VoiceMode::Studio16, st);
        CHECK(r.patterns == 3);
        CHECK(st.seq.patterns[0].steps == 4);
        CHECK(st.seq.patterns[1].steps == 2);
        CHECK(st.seq.patterns[2].steps == 3);
        const std::vector<ymseq::SongEntry> want = {{0, 1, 0, ymseq::SongKind::Pattern, 0, false},
                                                    {0, 1, 0, ymseq::SongKind::Pattern, 0, true},
                                                    {1, 1, 0, ymseq::SongKind::Pattern, 0, false},
                                                    {2, 1, 0, ymseq::SongKind::Pattern, 0, false}};
        CHECK(st.seq.song.entries == want);
        CHECK(st.seq.song.restartIndex() == 1);
        CHECK(r.notes.empty());
    }
    SECTION("end of the table: repetitions grouped, restart of the module") {
        XmModule m = module(1, {4, 4});
        m.instruments = {instrument("x", {tone(100)})};
        m.order = {1, 1, 0};
        m.restart = 0;
        put(m, 0, 0, 0, {49, 1, 0, 0, 0});
        put(m, 1, 0, 0, {49, 1, 0, 0, 0});
        put(m, 1, 3, 0, {0, 0, 0, 0xD, 0x10});  // to line 10: played from line 0
        NonParamState st;
        const XmImport r = run(m, VoiceMode::Studio16, st);
        CHECK(st.seq.song.entries ==
              std::vector<ymseq::SongEntry>{{0, 2, 0, ymseq::SongKind::Pattern, 0, false},
                                            {1, 1, 0, ymseq::SongKind::Pattern, 0, false}});
        CHECK(st.seq.patterns[0].steps == 4);  // pattern 1 of the XM, met first
        CHECK(has(r, "Pattern breaks to a later row (played from row 0): 2."));
    }
    SECTION("more than 32 patterns") {
        XmModule m = module(1, std::vector<int>(34, 1));
        m.instruments = {instrument("x", {tone(100)})};
        m.order.clear();
        for (int p = 0; p < 34; ++p) {
            m.order.push_back(uint8_t(p));
            put(m, p, 0, 0, {49, 1, 0, 0, 0});
        }
        NonParamState st;
        const XmImport r = run(m, VoiceMode::Studio16, st);
        CHECK(r.patterns == 32);
        CHECK(st.seq.song.size() == 32);
        CHECK(has(r, "Patterns beyond 32: 2 Song positions left out."));
    }
}

TEST_CASE("XM import: BPM timing, tempo of each entry, BPM row within a pattern (item 150)", "[xmimport]") {
    XmModule m = module(1, {4, 4, 4});
    m.instruments = {instrument("x", {tone(100)})};
    m.bpm = 140;
    m.order = {0, 1, 2};
    put(m, 0, 0, 0, {49, 1, 0, 0, 0});
    put(m, 0, 2, 0, {0, 0, 0, 0xF, 0x05});  // speed 5 in the middle: BPM row, 24 × 140 / (5 × 4) = 168 BPM
    put(m, 1, 0, 0, {49, 1, 0, 0xF, 0x03});
    put(m, 2, 0, 0, {49, 1, 0, 0xF, 0x01});
    put(m, 2, 1, 0, {0, 0, 0, 0xF, 0xFF});  // 1530 BPM: T FF
    NonParamState st;
    st.global.timing = ymengine::SeqTiming::Ticks;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    CHECK_FALSE(r.ticks);
    CHECK(st.global.timing == ymengine::SeqTiming::Bpm);
    CHECK(r.tempo == 140.0f);  // 6 × 140 / 6 in sixteenth notes
    // With Txx, every entry has its own tempo, which it starts again at.
    const std::vector<ymseq::SongEntry> want = {{0, 1, 140, ymseq::SongKind::Pattern, 0, false},
                                                {1, 1, 280, ymseq::SongKind::Pattern, 0, false},
                                                {2, 1, 300, ymseq::SongKind::Pattern, 0, false}};
    CHECK(st.seq.song.entries == want);
    CHECK(st.seq.patterns[0].bpm[2] == 0xA8);
    CHECK(st.seq.patterns[2].bpm[1] == 0xFF);
    CHECK(st.seq.patterns[0].lane(4).cmdAt(SeqCmd::Tempo, 2) == 0);
    CHECK_FALSE(st.seq.patterns[1].lane(4).hasCmd(0));  // no Sxx in BPM timing; the first line is the entry's tempo
    CHECK_FALSE(st.seq.patterns[2].lane(4).hasCmd(0));
    CHECK(has(r, "Tempos clamped to 20-300 BPM: 1."));
    CHECK(has(r, "BPM row tempos clamped to 32-255 BPM: 1."));
    const std::vector<std::string> msg = plugin::xmImportMessage(r, "t.xm");
    CHECK(msg[5] == "Tempo 140 BPM, STE machine.");
}

TEST_CASE("XM import: Ticks timing, Fxx as Sxx on the imported track", "[xmimport]") {
    XmModule m = module(2, {8});
    m.instruments = {instrument("x", {tone(100)})};
    m.bpm = 150;
    put(m, 0, 0, 0, {49, 1, 0, 0, 0});
    put(m, 0, 2, 1, {0, 0, 0, 0xF, 0x03});  // channel without notes: Sxx on track 1
    put(m, 0, 4, 0, {0, 0, 0, 0xF, 0x04});
    put(m, 0, 6, 0, {0, 0, 0, 0x0, 0x47});  // 0xy: left out
    put(m, 0, 6, 1, {0, 0, 0, 0xF, 0x02});
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    CHECK(r.ticks);
    CHECK(r.vblRate == 1);  // 150 BPM: 60 Hz
    const ymseq::Lane& l = st.seq.patterns[0].lane(4);
    CHECK(l.cmdAt(SeqCmd::Speed, 2) == ymengine::seqCmd(SeqCmd::Speed, 3));
    CHECK(l.cmdAt(SeqCmd::Speed, 4) == ymengine::seqCmd(SeqCmd::Speed, 4));
    CHECK(l.cmdAt(SeqCmd::Arp, 6) == 0);
    CHECK(l.cmdAt(SeqCmd::Speed, 6) == ymengine::seqCmd(SeqCmd::Speed, 2));
    CHECK(std::none_of(r.notes.begin(), r.notes.end(),
                       [](const std::string& n) { return n.starts_with("Speed changes without"); }));
}

TEST_CASE("XM import: 127 instruments at most, the most played; names of the DMA slots",
          "[xmimport]") {
    // 128 instruments, as many as an XM holds: the first played once, the others twice.
    XmModule m = module(1, {256});
    for (int i = 1; i <= 128; ++i) {
        XmSample s = tone(100);
        if (i % 2) s.name.clear();  // a sample without a name: that of its instrument
        m.instruments.push_back(instrument("ins" + std::to_string(i), {s}));
    }
    int row = 0;
    for (int i = 1; i <= 128; ++i)
        for (int k = 0; k < (i == 1 ? 1 : 2); ++k) put(m, 0, row++, 0, {49, uint8_t(i), 0, 0, 0});
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    CHECK(r.instruments == 127);  // the instruments of a project (8, the DMA slots, then 32, in earlier versions)
    for (int k = 0; k < 127; ++k) {
        CHECK(st.instruments[size_t(k)].name == "ins" + std::to_string(k + 2));
        CHECK(st.instruments[size_t(k)].patch.dmaSlot == k);
        CHECK(st.dmaNames[size_t(k)] == ((k + 2) % 2 ? "ins" + std::to_string(k + 2) : std::string("tone")));
    }
    CHECK(st.dmaSamples[126].size() == st.dmaSamples[0].size());  // past the former eight slots
    const ymseq::Lane& l = st.seq.patterns[0].lane(4);
    CHECK(l.pat[0] == -1);  // instrument 1: without a slot
    CHECK(l.ins[1] == 1);  // instrument 2: $01
    CHECK(l.ins[254] == 127);  // instrument 128: $7F
    CHECK(has(r, "Instruments beyond the 127 of a project: 1, 1 note left out."));
}

TEST_CASE("XM import: a note of an instrument without a sample is left out", "[xmimport]") {
    XmModule m = module(1, {16});
    m.instruments = {instrument("tone", {tone(100)}), instrument("empty", {})};
    put(m, 0, 0, 0, {49, 1, 0, 0, 0});
    put(m, 0, 1, 0, {49, 2, 0, 0, 0});
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    CHECK(r.instruments == 1);
    CHECK(st.seq.patterns[0].lane(4).pat[1] == -1);
    CHECK(has(r, "Notes without a sample: 1."));
}

TEST_CASE("XM import: samples, STE rates, loops, whole samples", "[xmimport]") {
    XmModule m = module(1, {6});
    m.instruments = {instrument("unrolled", {tone(200, 1, 100, 50)}), instrument("pingpong", {tone(100, 2, 0, 100)}),
                     instrument("fits", {tone(20000, 0, 0, 0, 19)}), instrument("lower", {tone(40000, 0, 0, 0, 19)}),
                     instrument("cut", {tone(300000, 1, 0, 300000)})};
    for (int i = 0; i < 5; ++i) put(m, 0, i, 0, {49, uint8_t(i + 1), 0, 0, 0});
    m.instruments[2].samples[0].finetune = 0;
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    REQUIRE(r.instruments == 5);
    CHECK(st.dma.slots[0] == ymengine::DmaSlot{0, false, 60, false});
    CHECK(st.dmaSamples[0].size() >= 32 * 1024);  // attack and loop turns unrolled to 32 KB
    CHECK(st.dmaSamples[0].size() < 32 * 1024 + 50);
    CHECK(st.dma.slots[1] == ymengine::DmaSlot{0, false, 60, true});
    CHECK(st.dmaSamples[1].size() == size_t(198 * 6258 / 8363));  // forward (100) and back (98)
    CHECK(st.dma.slots[2].rate == 2);  // 25 060 Hz: 25 033 Hz
    CHECK(st.dmaSamples[2].size() == 19977);
    CHECK(st.dma.slots[3].rate == 2);  // 39 955 bytes at 25 033 Hz, whole (once 12 517 Hz)
    CHECK(st.dmaSamples[3].size() == 39955);
    CHECK(st.dma.slots[4] == ymengine::DmaSlot{0, false, 60, true});  // whole, its loop kept (once cut)
    CHECK(st.dmaSamples[4].size() == size_t(300000.0 * 6258 / 8363));
    CHECK_FALSE(std::any_of(r.notes.begin(), r.notes.end(),
                            [](const std::string& n) { return n.find("cut") != std::string::npos; }));
    CHECK(has(r, "Loops unrolled to 32 KB (the STE loops whole samples): 1."));
}

TEST_CASE("XM import: refused modules, project unchanged", "[xmimport]") {
    NonParamState st;
    st.seq.song.entries = {{3, 2, 0, ymseq::SongKind::Pattern, 0, false}};
    const NonParamState before = st;
    CHECK(importXm(std::vector<uint8_t>(100, 0), VoiceMode::Studio16, st).error == "Not an XM module.");
    XmModule m = module(1, {4});
    m.instruments = {instrument("empty", {})};
    put(m, 0, 0, 0, {49, 1, 0, 0, 0});
    CHECK(importXm(test::buildXm(m), VoiceMode::Studio16, st).error == "Nothing to import: no notes with a sample.");
    m.order.clear();
    CHECK(importXm(test::buildXm(m), VoiceMode::Studio16, st).error == "Nothing to import: no notes with a sample.");
    CHECK(st.seq.song == before.seq.song);
    CHECK(st.instruments == before.instruments);
}

TEST_CASE("XM import: the imported project plays the DMA sound (16 voices and Authentic 3ch)", "[xmimport]") {
    for (const VoiceMode mode : {VoiceMode::Studio16, VoiceMode::Authentic3}) {
        XmModule m = module(2, {16});
        m.instruments = {instrument("x", {tone(2000, 1, 0, 2000)})};
        put(m, 0, 0, 0, {49, 1, 0, 0, 0});
        put(m, 0, 8, 1, {56, 1, 0, 0, 0});
        NonParamState st;
        const XmImport r = run(m, mode, st);
        ParamValues v = plugin::defaultValues();
        v[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = mode == VoiceMode::Studio16 ? 0.0f : 1.0f;
        v[size_t(plugin::paramIndex(plugin::Global::Computer))] = 1;
        v[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(r.vblRate);
        for (int t = 0; t < plugin::kTracks; ++t) {
            v[size_t(plugin::paramIndex(t, plugin::Track::On))] = r.on[size_t(t)] ? 1.0f : 0.0f;
            if (r.on[size_t(t)] && mode == VoiceMode::Studio16)
                v[size_t(plugin::paramIndex(t, plugin::Track::Generator))] = float(int(ymengine::Generator::Dma));
        }
        plugin::OfflineRender render(plugin::OfflineRender::Source::Song, 48000, v, st);
        std::vector<float> l(plugin::OfflineRender::kChunk), rr(plugin::OfflineRender::kChunk);
        ymengine::Outputs o;
        o.left = l.data();
        o.right = rr.data();
        float peak = 0;
        uint64_t total = 0;
        while (const uint32_t n = render.render(o, plugin::OfflineRender::kChunk)) {
            for (uint32_t i = 0; i < n; ++i) peak = std::max(peak, std::abs(l[i]));
            total += n;
        }
        INFO(int(mode));
        CHECK(peak > 0.05f);
        // 16 lines of 6 ticks at 50 Hz: 1.92 s.
        CHECK(std::abs(double(total) - 1.92 * 48000) < 48);
    }
}

TEST_CASE("XM import: a resolution change that fits the notes keeps the volume of held notes",
          "[xmimport]") {
    // Indus, 29/10/2026 15:46: on a DMA track, 1/16 to 1/8 then back to 1/16 (Fit both times) made the samples
    // stutter: the new held steps had no VOL and played at full level between two steps at the module's volume.
    XmModule m = module(1, {16});
    m.instruments = {instrument("x", {tone(40000)})};
    put(m, 0, 0, 0, {49, 1, 0, 0, 0});
    put(m, 0, 8, 0, {56, 1, 0, 0, 0});
    NonParamState st;
    const XmImport r = run(m, VoiceMode::Studio16, st);
    ParamValues v = plugin::defaultValues();
    v[size_t(plugin::paramIndex(plugin::Global::VoiceMode))] = 0.0f;
    v[size_t(plugin::paramIndex(plugin::Global::Computer))] = 1;
    v[size_t(plugin::paramIndex(plugin::Global::VblRate))] = float(r.vblRate);
    for (int t = 0; t < plugin::kTracks; ++t) {
        v[size_t(plugin::paramIndex(t, plugin::Track::On))] = r.on[size_t(t)] ? 1.0f : 0.0f;
        if (r.on[size_t(t)])
            v[size_t(plugin::paramIndex(t, plugin::Track::Generator))] = float(int(ymengine::Generator::Dma));
    }
    // Levels set on the held steps ('A' records of the sequencer log), and the attacks ('T').
    auto levels = [&] {
        std::vector<double> out;
        int attacks = 0;
        plugin::OfflineRender render(plugin::OfflineRender::Source::Song, 48000, v, st);
        std::vector<float> l(plugin::OfflineRender::kChunk), rr(plugin::OfflineRender::kChunk);
        ymengine::Outputs o;
        o.left = l.data();
        o.right = rr.data();
        while (render.render(o, plugin::OfflineRender::kChunk))
            for (const auto& rec : render.log()) {
                if (rec.op == 'A') out.push_back(rec.x);
                attacks += rec.op == 'T';
            }
        CHECK(attacks == 2);
        return out;
    };
    const std::vector<double> before = levels();
    REQUIRE(before.size() == 14);  // 7 held steps after each note
    // What SeqWindow::setResolution does with Fit: 1/16 to 1/8, then back.
    st.seq.fitResolution(1, 2);
    st.global.resolution = 1;
    st.global.timing = ymengine::SeqTiming::Bpm;
    st.seq.fitResolution(2, 1);
    st.global.resolution = 2;
    const std::vector<double> after = levels();
    REQUIRE(after.size() == 14);
    for (const double x : after) CHECK(x == before.front());
}
