// STYM2149 — MIDI import and export: import against the prototype (tools/proto_golden/midi.mjs,
// intended deviations included); export rendered by the offline sequencer (pattern, Song, tempos, swing, 3 voices,
// percussion).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "plugincore/MidiFiles.h"
#include "plugincore/Offline.h"

using namespace stym;
using namespace stym::plugin;

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<uint8_t> unhex(const std::string& h) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(uint8_t(std::stoi(h.substr(i, 2), nullptr, 16)));
    return out;
}

struct GoldenCase {
    std::string id;
    int steps = 16, mode3ch = 0, chips = 1;
    std::vector<int> oct;
    std::vector<uint8_t> file;
    bool null = false;
    int bpm = 0;
    std::vector<std::pair<int, int>> song;
    std::vector<int> enabled, effect;
    std::map<std::pair<int, int>, std::map<int, std::array<int, 4>>> lanes;  // (pattern, track) → step → cells
    std::string msg;
};

std::vector<GoldenCase> loadGolden() {
    std::ifstream f(std::string(STYM_TEST_DATA) + "/proto/midi.txt");
    REQUIRE(f);
    std::vector<GoldenCase> out;
    GoldenCase c;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t sp = line.find(' ');
        const std::string key = line.substr(0, sp), rest = sp == std::string::npos ? "" : line.substr(sp + 1);
        std::istringstream ls(rest);
        if (key == "case") {
            c = GoldenCase{};
            c.id = rest;
        } else if (key == "ctx") {
            std::string octs;
            ls >> c.steps >> c.mode3ch >> c.chips >> octs;
            for (const std::string& o : split(octs, ',')) c.oct.push_back(std::stoi(o));
        } else if (key == "file") {
            c.file = unhex(rest);
        } else if (key == "result") {
            c.null = true;
        } else if (key == "bpm") {
            ls >> c.bpm;
        } else if (key == "song") {
            std::string e;
            while (ls >> e) {
                const auto parts = split(e, ':');
                c.song.emplace_back(std::stoi(parts[0]), std::stoi(parts[1]));
            }
        } else if (key == "enabled" || key == "effect") {
            for (const std::string& v : split(rest, ',')) (key == "enabled" ? c.enabled : c.effect).push_back(std::stoi(v));
        } else if (key == "lane") {
            int pat = 0, track = 0;
            ls >> pat >> track;
            std::string cell;
            auto& m = c.lanes[{pat, track}];
            while (ls >> cell) {
                const auto parts = split(cell, ':');
                m[std::stoi(parts[0])] = {std::stoi(parts[1]), std::stoi(parts[2]), std::stoi(parts[3]), std::stoi(parts[4])};
            }
        } else if (key == "msg") {
            c.msg = rest;
        } else if (key == "end") {
            out.push_back(c);
        }
    }
    return out;
}

float& param(ParamValues& v, int track, Track f) { return v[size_t(paramIndex(track, f))]; }
float& param(ParamValues& v, Global g) { return v[size_t(paramIndex(g))]; }

// Relative note and octave of an absolute note, for a track at octave 3.
void note(ymseq::Lane& l, int step, int midi, int vol = 15, bool tie = false) {
    const int oc = midi / 12 - 1;
    l.pat[size_t(step)] = int16_t(midi - (oc + 1) * 12);
    l.oct[size_t(step)] = int8_t(ymseq::octaveCode(oc, 3));
    l.vol[size_t(step)] = uint8_t(vol);
    l.tie[size_t(step)] = tie ? 1 : 0;
}

struct Project {
    ParamValues v = defaultValues();
    NonParamState x;
};

// Notes of an exported file, by channel.
std::vector<ymio::SmfNote> notesOf(const std::vector<uint8_t>& file, int channel) {
    const std::optional<ymio::SmfSong> s = ymio::readSmf(file);
    REQUIRE(s);
    REQUIRE(s->division == kMidiPpq);
    std::vector<ymio::SmfNote> out;
    for (const ymio::SmfNote& n : s->notes)
        if (n.channel == channel) out.push_back(n);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return out;
}

// Meta events of one type (tick, data) in the exported file, all tracks.
std::vector<std::pair<uint32_t, std::vector<uint8_t>>> metas(const std::vector<uint8_t>& f, uint8_t type) {
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> out;
    size_t p = 14;
    while (p + 8 <= f.size()) {
        const size_t len = size_t(f[p + 4]) << 24 | size_t(f[p + 5]) << 16 | size_t(f[p + 6]) << 8 | f[p + 7];
        size_t q = p + 8;
        const size_t end = q + len;
        uint32_t tick = 0;
        while (q < end) {
            uint32_t d = 0;
            uint8_t b = 0;
            do {
                b = f[q++];
                d = d << 7 | (b & 127u);
            } while (b & 128u);
            tick += d;
            const uint8_t st = f[q];
            if (st == 0xFF) {
                const uint8_t t = f[q + 1];
                const size_t ml = f[q + 2];
                if (t == type) out.emplace_back(tick, std::vector<uint8_t>(f.begin() + long(q + 3), f.begin() + long(q + 3 + ml)));
                q += 3 + ml;
            } else {
                q += 3;  // the export writes only notes (3 bytes, full status)
            }
        }
        p = end;
    }
    return out;
}

}  // namespace

TEST_CASE("MIDI import: patterns, Song, tracks and warning like the prototype (intended deviations)", "[midi][proto]") {
    const std::vector<GoldenCase> cases = loadGolden();
    REQUIRE(cases.size() >= 120);
    for (const GoldenCase& c : cases) {
        INFO("case " << c.id);
        ymseq::Project p;
        p.setSteps(c.steps);
        for (int t = 0; t < kTracks; ++t) p.tracks[size_t(t)].octave = c.oct[size_t(t)];
        // A pattern created beyond those of the import stays.
        p.patterns[31].steps = 5;
        p.used.set(31);
        const ymseq::Project before = p;
        const ymengine::VoiceMode mode = !c.mode3ch ? ymengine::VoiceMode::Studio16
                                         : c.chips > 1 ? ymengine::VoiceMode::Dual3
                                                       : ymengine::VoiceMode::Authentic3;
        const std::optional<MidiImport> r = importMidi(c.file, mode, p);
        if (c.null) {
            CHECK_FALSE(r);
            CHECK(p.song == before.song);
            CHECK(p.used == before.used);
            continue;
        }
        REQUIRE(r);
        CHECK(r->bpm == c.bpm);
        CHECK(r->steps == c.steps);
        CHECK(r->patterns == int(c.song.size()));
        REQUIRE(p.song.size() == int(c.song.size()));
        const int entryBpm = c.bpm >= 60 && c.bpm <= 240 ? 0 : c.bpm;
        for (size_t k = 0; k < c.song.size(); ++k) {
            CHECK(p.song.entries[k].pat == c.song[k].first);
            CHECK(p.song.entries[k].rep == c.song[k].second);
            CHECK(p.song.entries[k].bpm == entryBpm);
            CHECK(p.song.entries[k].kind == ymseq::SongKind::Pattern);
        }
        CHECK(midiImportTempo(*r) == float(std::clamp(c.bpm, 60, 240)));
        int on = 0;
        for (int t = 0; t < kTracks; ++t) {
            CHECK(r->on[size_t(t)] == (c.enabled[size_t(t)] != 0));
            if (r->on[size_t(t)]) {
                ++on;
                CHECK(r->drum[size_t(t)] == (c.effect[size_t(t)] == 4));
            }
        }
        CHECK(r->tracks == on);
        for (const auto& [pat, rep] : c.song) {
            const ymseq::Pattern& q = p.patterns[size_t(pat)];
            CHECK(p.used.test(size_t(pat)));
            CHECK(q.steps == c.steps);
            for (int t = 0; t < kTracks; ++t) {
                INFO("pattern " << pat << ", track " << t);
                const auto it = c.lanes.find({pat, t});
                const ymseq::Lane& l = q.lane(t);
                for (int s = 0; s < ymseq::kSteps; ++s) {
                    std::array<int, 4> want = {-1, 0, 0, 15};
                    if (it != c.lanes.end() && it->second.count(s)) want = it->second.at(s);
                    const std::array<int, 4> got = {l.pat[size_t(s)], l.oct[size_t(s)], l.tie[size_t(s)], l.vol[size_t(s)]};
                    if (got != want) FAIL_CHECK("step " << s);
                }
                ymseq::Lane other = l;  // the other rows stay empty
                for (int s = 0; s < ymseq::kSteps; ++s) {
                    other.pat[size_t(s)] = -1;
                    other.oct[size_t(s)] = 0;
                    other.tie[size_t(s)] = 0;
                    other.vol[size_t(s)] = 15;
                }
                CHECK(other.empty());
            }
        }
        if (c.song.size() < 32) {
            CHECK(p.used.test(31));
            CHECK(p.patterns[31].steps == 5);
        }
        std::string msg;
        for (const std::string& line : midiImportMessage(*r, "test.mid")) msg += (msg.empty() ? "" : "|") + line;
        CHECK(msg == c.msg);
    }
}

TEST_CASE("MIDI export of a pattern: sequencer notes, gate, tie, velocity, tempo", "[midi][export]") {
    Project pr;
    param(pr.v, Global::FreeTempo) = 120;
    pr.x.trackNames[0] = "Lead";
    ymseq::Pattern& p = pr.x.seq.cur();
    p.steps = 16;
    ymseq::Lane& l = p.edit(0);
    note(l, 0, 60);                 // one step, gate 50 %
    note(l, 4, 64, 9);              // VOL 9
    note(l, 8, 67, 15, true);       // tied over 8, 9 and 10
    l.tie[9] = l.tie[10] = 1;
    // Track 2 off: nothing of it in the file; track 3 on.
    ymseq::Lane& l2 = p.edit(1);
    note(l2, 0, 50);
    param(pr.v, 2, Track::On) = 1;
    ymseq::Lane& l3 = p.edit(2);
    note(l3, 2, 72);
    const std::optional<MidiExport> e = exportMidi(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(e);
    CHECK(e->tracks == 2);
    CHECK_FALSE(e->truncated);
    CHECK(e->seconds == Catch::Approx(2.0));  // 16 sixteenth notes at 120 BPM
    const auto n0 = notesOf(e->bytes, 0);
    REQUIRE(n0.size() == 3);
    CHECK((n0[0].note == 60 && n0[0].start == 0 && n0[0].end == 12 && n0[0].velocity == 127));
    CHECK((n0[1].note == 64 && n0[1].start == 96 && n0[1].end == 108 && n0[1].velocity == 76));
    CHECK((n0[2].note == 67 && n0[2].start == 192 && n0[2].end == 264 && n0[2].velocity == 127));
    const auto n1 = notesOf(e->bytes, 1);  // track 3: second channel
    REQUIRE(n1.size() == 1);
    CHECK((n1[0].note == 72 && n1[0].start == 48 && n1[0].end == 60));
    // Tempo track: 4/4 time signature and 120 BPM; track names.
    const auto tempos = metas(e->bytes, 0x51);
    REQUIRE(tempos.size() == 1);
    CHECK(tempos[0] == std::pair<uint32_t, std::vector<uint8_t>>{0, {0x07, 0xA1, 0x20}});
    CHECK(metas(e->bytes, 0x58).size() == 1);
    const auto names = metas(e->bytes, 0x03);
    REQUIRE(names.size() == 2);
    CHECK(std::string(names[0].second.begin(), names[0].second.end()) == "Lead");
    CHECK(std::string(names[1].second.begin(), names[1].second.end()) == "STYM2149 ch3");
}

TEST_CASE("MIDI export: swing and tempo off the prototype's grid", "[midi][export]") {
    Project pr;
    param(pr.v, Global::FreeTempo) = 120;
    param(pr.v, Global::Swing) = 66;
    ymseq::Pattern& p = pr.x.seq.cur();
    p.steps = 4;
    ymseq::Lane& l = p.edit(0);
    for (int s = 0; s < 4; ++s) note(l, s, 60 + s);
    const std::optional<MidiExport> e = exportMidi(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(e);
    const auto n = notesOf(e->bytes, 0);
    REQUIRE(n.size() == 4);
    // Even steps lengthened by 32 %: 24 × 1.32 = 31.68 ticks.
    CHECK(n[0].start == 0);
    CHECK(n[1].start == 32);
    CHECK(n[2].start == 48);
    CHECK(n[3].start == 80);
    CHECK(e->seconds == Catch::Approx(0.5));
}

TEST_CASE("MIDI export of a Song: block tempos, GM percussion, 3 voices, end at the last entry",
          "[midi][export]") {
    Project pr;
    param(pr.v, Global::VoiceMode) = 1;  // Authentic 3ch
    param(pr.v, Global::FreeTempo) = 120;
    param(pr.v, 0, Track::Level) = 50;  // the track level does not enter the velocity
    param(pr.v, 1, Track::On) = 1;
    param(pr.v, 1, Track::Generator) = 4;  // Chipdrum
    ymseq::Project& s = pr.x.seq;
    for (int k = 0; k < 2; ++k) {
        s.patterns[size_t(k)].steps = 16;
        s.used.set(size_t(k));
        ymseq::Lane& l = s.patterns[size_t(k)].edit(0);
        note(l, 0, 60 + k, 12);
        ymseq::Lane& d = s.patterns[size_t(k)].edit(1);
        d.pat[0] = 0;  // Kick
        d.pat[8] = 1;  // Snare
    }
    s.song.entries = {{0, 2, 0, ymseq::SongKind::Pattern, 0, false}, {1, 1, 150, ymseq::SongKind::Pattern, 0, false}};
    const std::optional<MidiExport> e = exportMidi(OfflineRender::Source::Song, pr.v, pr.x);
    REQUIRE(e);
    CHECK(e->seconds == Catch::Approx(4.0 + 16 * 0.1));  // 2 × 2 s at 120 BPM, then 16 steps of 0.1 s at 150 BPM
    CHECK(e->tracks == 2);
    const auto tempos = metas(e->bytes, 0x51);
    REQUIRE(tempos.size() == 2);
    CHECK(tempos[0] == std::pair<uint32_t, std::vector<uint8_t>>{0, {0x07, 0xA1, 0x20}});    // 500 000 µs
    CHECK(tempos[1] == std::pair<uint32_t, std::vector<uint8_t>>{768, {0x06, 0x1A, 0x80}});  // 400 000 µs
    const auto mel = notesOf(e->bytes, 0);
    REQUIRE(mel.size() == 3);
    CHECK((mel[0].note == 60 && mel[0].start == 0 && mel[0].velocity == 102));  // VOL 12
    CHECK((mel[1].note == 60 && mel[1].start == 384));
    CHECK((mel[2].note == 61 && mel[2].start == 768));
    const auto drums = notesOf(e->bytes, 9);
    REQUIRE(drums.size() == 6);
    for (size_t k = 0; k < drums.size(); ++k) {
        CHECK(drums[k].note == (k % 2 ? 38 : 36));
        CHECK(drums[k].start == int64_t(k / 2 * 384 + (k % 2) * 192));
        CHECK(drums[k].end - drums[k].start == 12);  // half a step
    }
}

TEST_CASE("MIDI export: cancel requested, empty Song played like the pattern", "[midi][export]") {
    Project pr;
    ymseq::Lane& l = pr.x.seq.cur().edit(0);
    note(l, 0, 60);
    std::atomic<bool> cancel{true};
    CHECK_FALSE(exportMidi(OfflineRender::Source::Pattern, pr.v, pr.x, &cancel));
    // The offline render stops dead at the end of the pattern.
    OfflineRender r(OfflineRender::Source::Pattern, 48000, pr.v, pr.x);
    uint64_t total = 0;
    while (uint32_t n = r.advance(OfflineRender::kChunk)) total += n;
    CHECK(r.finished());
    CHECK(total == 96000);
    CHECK(r.position() == 96000);
}

TEST_CASE("MIDI export: chords, repeated hits, slide", "[midi][export]") {
    Project pr;
    param(pr.v, Global::FreeTempo) = 120;
    ymseq::Pattern& p = pr.x.seq.cur();
    p.steps = 16;
    ymseq::Lane& l = p.edit(0);
    note(l, 0, 60);
    l.vpat[0][0] = 4;  // chord voice: E of the track's octave
    l.voct[0][0] = int8_t(ymseq::octaveCode(4, 3));
    note(l, 4, 62);
    l.rtg[4] = 1;   // two hits
    l.gat[4] = 90;  // gate of 90 %: the second hit sounds
    note(l, 8, 60);
    l.slide[8] = 1;  // slide to the next note
    note(l, 9, 67, 9);  // the arrival at volume 9
    const std::optional<MidiExport> e = exportMidi(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(e);
    const auto n = notesOf(e->bytes, 0);
    REQUIRE(n.size() == 6);
    CHECK((n[0].start == 0 && n[1].start == 0 && n[0].end == 12 && n[1].end == 12));
    CHECK(((n[0].note == 60 && n[1].note == 64) || (n[0].note == 64 && n[1].note == 60)));
    CHECK((n[2].note == 62 && n[2].start == 96 && n[2].end == 108 && n[2].velocity == 127));
    CHECK((n[3].note == 62 && n[3].start == 108 && n[3].end == 118 && n[3].velocity == 117));  // level × 0.92
    CHECK((n[4].note == 60 && n[4].start == 192 && n[4].end == 216));  // the slide leads to the next note
    CHECK((n[5].note == 67 && n[5].start == 216 && n[5].end == 228));
    CHECK(n[4].velocity == 127);
    CHECK(n[5].velocity == 76);  // in 16 channels the arrival takes its own level: 9 / 15 × 127
}

TEST_CASE("MIDI export of a Song: Loop jumps unrolled, Note-off entry", "[midi][export]") {
    Project pr;
    param(pr.v, Global::FreeTempo) = 120;
    ymseq::Project& s = pr.x.seq;
    s.patterns[0].steps = 4;
    note(s.patterns[0].edit(0), 0, 60);
    s.patterns[1].steps = 4;
    note(s.patterns[1].edit(0), 0, 62, 15, true);  // tied up to the Note-off entry
    s.patterns[1].edit(0).tie[1] = s.patterns[1].edit(0).tie[2] = s.patterns[1].edit(0).tie[3] = 1;
    s.used.set(1);
    s.song.entries = {{0, 1, 0, ymseq::SongKind::Pattern, 0, false}, ymseq::SongEntry::loop(1, 2),
                      {1, 1, 0, ymseq::SongKind::Pattern, 0, false}, ymseq::SongEntry::rest(ymseq::SongKind::NoteOff, 4),
                      {0, 1, 0, ymseq::SongKind::Pattern, 0, false}};
    const std::optional<MidiExport> e = exportMidi(OfflineRender::Source::Song, pr.v, pr.x);
    REQUIRE(e);
    // A1 three times (the jump replays it twice), A2, four Note-off steps, A1: 24 steps of 0.125 s.
    CHECK(e->seconds == Catch::Approx(3.0));
    const auto n = notesOf(e->bytes, 0);
    REQUIRE(n.size() == 5);
    for (int k = 0; k < 3; ++k) CHECK((n[size_t(k)].note == 60 && n[size_t(k)].start == k * 96));
    CHECK((n[3].note == 62 && n[3].start == 288 && n[3].end == 384));  // released by the Note-off entry
    CHECK((n[4].note == 60 && n[4].start == 480));
}

TEST_CASE("MIDI export: Ticks timing, equivalent tempo", "[midi][export]") {
    Project pr;
    pr.x.global.timing = ymengine::SeqTiming::Ticks;
    pr.x.global.speed = 6;  // 6 ticks of 50 Hz per line: 0.12 s, 125 BPM in sixteenth notes
    ymseq::Pattern& p = pr.x.seq.cur();
    p.steps = 4;
    for (int st = 0; st < 4; ++st) note(p.edit(0), st, 60);
    const std::optional<MidiExport> e = exportMidi(OfflineRender::Source::Pattern, pr.v, pr.x);
    REQUIRE(e);
    CHECK(e->seconds == Catch::Approx(0.48));
    const auto tempos = metas(e->bytes, 0x51);
    REQUIRE(tempos.size() == 1);
    CHECK(tempos[0] == std::pair<uint32_t, std::vector<uint8_t>>{0, {0x07, 0x53, 0x00}});  // 480 000 µs
    const auto n = notesOf(e->bytes, 0);
    REQUIRE(n.size() == 4);
    for (int k = 0; k < 4; ++k) CHECK(n[size_t(k)].start == k * 24);
}

TEST_CASE("MIDI export: track arpeggio, also on the MIDI clock (pulses produced offline)",
          "[midi][export]") {
    for (bool clock : {false, true}) {
        INFO((clock ? "MIDI clock" : "Hz"));
        Project pr;
        param(pr.v, Global::FreeTempo) = 120;
        param(pr.v, 0, Track::ArpOn) = 1;
        param(pr.v, 0, Track::ArpRate) = 48;
        if (clock) {
            param(pr.v, Global::ArpClock) = 1;
            param(pr.v, Global::ArpDivision) = 6;  // one pulse per note: 48 per second at 120 BPM
        }
        ymseq::Pattern& p = pr.x.seq.cur();
        p.steps = 2;
        ymseq::Lane& l = p.edit(0);
        note(l, 0, 60);
        l.setCmd(0, ymengine::seqCmd(ymengine::SeqCmd::Arpeggiator, 1));  // the arpeggio on step 0 alone
        l.setCmd(1, ymengine::seqCmd(ymengine::SeqCmd::Arpeggiator, 0));
        const std::optional<MidiExport> e = exportMidi(OfflineRender::Source::Pattern, pr.v, pr.x);
        REQUIRE(e);
        const auto n = notesOf(e->bytes, 0);
        // 0.125 s of arpeggio at 48 Hz: 6 notes, C E G of the default table.
        REQUIRE(n.size() >= 5);
        REQUIRE(n.size() <= 7);
        for (size_t k = 0; k < n.size(); ++k) {
            CHECK(n[k].note == std::array<int, 3>{60, 64, 67}[k % 3]);
            CHECK(n[k].start < 24);
        }
    }
}
