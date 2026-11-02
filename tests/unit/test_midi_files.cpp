// STYM2149 — MIDI import and export: import against the prototype (tools/proto_golden/midi.mjs,
// intended deviations included); export rendered by the offline sequencer (pattern, Song, tempos, swing, 3 voices,
// percussion).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
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
        // The prototype's 16 tracks (tracks 17 to 32 keep their default octave).
        for (size_t t = 0; t < c.oct.size() && t < size_t(kTracks); ++t) p.tracks[t].octave = c.oct[t];
        // A pattern created beyond those of the import stays.
        p.patterns[31].steps = 5;
        p.used.set(31);
        const ymseq::Project before = p;
        const ymengine::VoiceMode mode = !c.mode3ch ? ymengine::VoiceMode::Studio16
                                         : c.chips > 1 ? ymengine::VoiceMode::Dual3
                                                       : ymengine::VoiceMode::Authentic3;
        // The patterns at the current length, as the prototype made them (the file's length, tested below).
        const std::optional<MidiImport> r = importMidi(c.file, mode, p, {4, 16, false});
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
            // The prototype's 16 tracks; tracks 17 to 32 stay off, a MIDI file has 16 channels.
            CHECK(r->on[size_t(t)] == (size_t(t) < c.enabled.size() && c.enabled[size_t(t)] != 0));
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

namespace {

// A format 1 file at 96 ticks per quarter note: one note of channel 1 per (start, end) in sixteenth notes.
std::vector<uint8_t> sixteenths(const std::vector<std::pair<int, int>>& notes, int channel = 0) {
    std::vector<ymio::SmfEvent> t;
    for (const auto& [a, b] : notes) {
        t.push_back(ymio::smfNoteOn(uint32_t(a * 24), channel, 60, 100));
        t.push_back(ymio::smfNoteOff(uint32_t(b * 24), channel, 60));
    }
    const std::vector<std::vector<ymio::SmfEvent>> tracks = {t};
    return ymio::writeSmf(96, tracks);
}

}  // namespace

TEST_CASE("MIDI import: the patterns take the file's length in whole bars", "[midi]") {
    struct Case {
        int lastEnd;   // end of the last note, in sixteenth notes
        int barSteps;  // steps per bar
        double perQuarter;
        int patterns, steps, lastSteps;
        bool truncated;
    };
    for (const Case c : {Case{20, 16, 4, 1, 32, 32, false},       // 1 bar and a quarter: 2 bars
                         Case{64, 16, 4, 1, 64, 64, false},       // 4 bars exactly
                         Case{3, 16, 4, 1, 16, 16, false},        // shorter than a bar: one bar
                         Case{40, 12, 4, 1, 48, 48, false},       // 3/4: 4 bars of 12
                         Case{64, 32, 8, 1, 128, 128, false},     // 1/32: twice the steps
                         Case{256, 16, 4, 1, 256, 256, false},    // 16 bars: one pattern of 256 steps
                         Case{600, 16, 4, 3, 256, 96, false},     // 38 bars: 16 + 16 + 6
                         Case{600, 12, 4, 3, 252, 96, false},     // 3/4: 21 bars of 12 per pattern, then 8
                         Case{9000, 16, 4, 32, 256, 256, true}})  // beyond 32 patterns: cut
    {
        INFO("end " << c.lastEnd << ", bar " << c.barSteps << ", steps per quarter " << c.perQuarter);
        ymseq::Project p;
        p.setSteps(16);
        p.tracks[0].length = 12;  // an own length: the imported track plays the whole pattern
        const std::vector<uint8_t> f = sixteenths({{0, 1}, {c.lastEnd - 1, c.lastEnd}});
        const std::optional<MidiImport> r = importMidi(f, ymengine::VoiceMode::Studio16, p, {c.perQuarter, c.barSteps});
        REQUIRE(r);
        CHECK(r->patterns == c.patterns);
        CHECK(r->steps == c.steps);
        CHECK(r->lastSteps == c.lastSteps);
        CHECK(r->truncated == c.truncated);
        REQUIRE(int(p.song.entries.size()) == c.patterns);
        for (int k = 0; k < c.patterns; ++k)
            CHECK(p.patterns[size_t(k)].steps == (k == c.patterns - 1 ? c.lastSteps : c.steps));
        CHECK(p.tracks[0].length == 0);
        // The last note on its step: in the last pattern, at its place in it.
        if (!c.truncated) {
            const int at = int(std::lround((c.lastEnd - 1) * c.perQuarter / 4));
            const int k = at / c.steps;
            CHECK(p.patterns[size_t(k)].lane(0).pat[size_t(at - k * c.steps)] == 0);
        }
    }
    // The alert says the last pattern's length when it differs.
    MidiImport r;
    r.tracks = 1;
    r.patterns = 3;
    r.steps = 256;
    r.lastSteps = 96;
    CHECK(midiImportMessage(r, "a.mid")[3] == "1 track, 3 patterns of 256 steps (the last of 96), 120 BPM.");
    r.patterns = 1;
    r.steps = r.lastSteps = 64;
    CHECK(midiImportMessage(r, "a.mid")[3] == "1 track, 1 pattern of 64 steps, 120 BPM.");
}

namespace {

// Notes C4 on channels 1 to `melodic`, then a GM snare on channel 10, all on step 0.
std::vector<uint8_t> channels(int melodic) {
    std::vector<ymio::SmfEvent> t;  // in time order
    for (int c = 0; c < melodic; ++c) t.push_back(ymio::smfNoteOn(0, c, 60, 100));
    t.push_back(ymio::smfNoteOn(0, 9, 38, 100));
    t.push_back(ymio::smfNoteOff(12, 9, 38));
    for (int c = 0; c < melodic; ++c) t.push_back(ymio::smfNoteOff(24, c, 60));
    const std::vector<std::vector<ymio::SmfEvent>> tracks = {t};
    return ymio::writeSmf(96, tracks);
}

}  // namespace

TEST_CASE("MIDI import: channel 10 played by Chipdrum or Digidrum, the Digidrum track in 3 ch", "[midi]") {
    using ymengine::Generator;
    using ymengine::VoiceMode;
    struct Case {
        VoiceMode mode;
        int melodic;
        Generator drums;
        int drumTrack;  // -1: channel 10 left out (no track left)
        int filled;
    };
    for (const Case c : {Case{VoiceMode::Studio16, 1, Generator::Chipdrum, 1, 2},
                         Case{VoiceMode::Studio16, 1, Generator::Digidrum, 1, 2},  // the same track in 32 ch
                         Case{VoiceMode::Authentic3, 1, Generator::Chipdrum, 1, 2},
                         Case{VoiceMode::Authentic3, 1, Generator::Digidrum, 3, 2},
                         Case{VoiceMode::Authentic3, 3, Generator::Chipdrum, -1, 3},  // the chip is full
                         Case{VoiceMode::Authentic3, 3, Generator::Digidrum, 3, 4},   // all four placed
                         Case{VoiceMode::Dual3, 6, Generator::Digidrum, 3, 7}}) {
        CAPTURE(int(c.mode), c.melodic, int(c.drums));
        ymseq::Project p;
        MidiImportOptions o;
        o.drums = c.drums;
        const std::optional<MidiImport> r = importMidi(channels(c.melodic), c.mode, p, o);
        REQUIRE(r);
        CHECK(r->percussion);
        CHECK(r->tracks == c.filled);
        for (int t = 0; t < kTracks; ++t) CHECK(r->drum[size_t(t)] == (t == c.drumTrack));
        if (c.drumTrack >= 0) {
            CHECK(r->on[size_t(c.drumTrack)]);
            CHECK(p.patterns[0].lane(c.drumTrack).pat[0] == gmSlot(38));
        }
        // The melodic channels on the tracks of the mode, in order.
        const std::vector<int> tracks = midiImportTracks(c.mode);
        for (int i = 0; i < std::min(c.melodic, int(tracks.size())); ++i) {
            CHECK(r->on[size_t(tracks[size_t(i)])]);
            CHECK(p.patterns[0].lane(tracks[size_t(i)]).pat[0] == 0);
        }
    }
    // Without channel 10.
    ymseq::Project p;
    const std::optional<MidiImport> r = importMidi(sixteenths({{0, 4}}), VoiceMode::Authentic3, p, {});
    REQUIRE(r);
    CHECK_FALSE(r->percussion);
    for (bool d : r->drum) CHECK_FALSE(d);
}

TEST_CASE("MIDI import: a drum part alone, on any channel, on the focused drum track", "[midi]") {
    using ymengine::VoiceMode;
    // A drum loop on channel 1 (GM kick, snare, toms), as a DAW writes it.
    std::vector<ymio::SmfEvent> t;
    for (const auto& [step, note] : {std::pair{0, 36}, std::pair{4, 38}, std::pair{8, 43}, std::pair{12, 36}}) {
        t.push_back(ymio::smfNoteOn(uint32_t(step * 24), 0, note, 100));
        t.push_back(ymio::smfNoteOff(uint32_t(step * 24 + 12), 0, note));
    }
    const std::vector<uint8_t> loop = ymio::writeSmf(96, std::vector<std::vector<ymio::SmfEvent>>{t});
    for (const auto& [mode, focus] : {std::pair{VoiceMode::Studio16, 9}, std::pair{VoiceMode::Authentic3, 3},
                                      std::pair{VoiceMode::Authentic3, 0}, std::pair{VoiceMode::Dual3, 7}}) {
        CAPTURE(int(mode), focus);
        ymseq::Project p;
        MidiImportOptions o;
        o.track = focus;
        o.trackDrums = true;
        const std::optional<MidiImport> r = importMidi(loop, mode, p, o);
        REQUIRE(r);
        CHECK(r->percussion);
        CHECK(r->tracks == 1);
        for (int k = 0; k < kTracks; ++k) {
            CHECK(r->drum[size_t(k)] == (k == focus));
            CHECK(r->on[size_t(k)] == (k == focus));
        }
        // The rows of the kit (GM_SLOT), without ties.
        const ymseq::Lane& l = p.patterns[0].lane(focus);
        CHECK(l.pat[0] == gmSlot(36));
        CHECK(l.pat[4] == gmSlot(38));
        CHECK(l.pat[8] == gmSlot(43));
        CHECK(l.pat[12] == gmSlot(36));
        CHECK(l.pat[1] < 0);
        for (int s = 0; s < 16; ++s) CHECK(l.tie[size_t(s)] == 0);
    }
    // Not a drum track focused: the melodic import of the channel, as before.
    ymseq::Project p;
    MidiImportOptions melodic;
    melodic.track = 4;
    std::optional<MidiImport> r = importMidi(loop, VoiceMode::Studio16, p, melodic);
    REQUIRE(r);
    CHECK_FALSE(r->percussion);
    CHECK_FALSE(r->drum[4]);
    CHECK(r->on[4]);
    CHECK(p.patterns[0].lane(4).pat[0] == 0);  // C
    // Several channels: channel 10 alone is the drums, the others melodic, whatever the focused track.
    MidiImportOptions o;
    o.track = 9;
    o.trackDrums = true;
    r = importMidi(channels(1), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK(r->drum[1]);
    CHECK_FALSE(r->drum[9]);
}

TEST_CASE("MIDI import: the other tracks keep their notes, the Song and the tempo stay", "[midi]") {
    using ymengine::VoiceMode;
    // Notes C on tracks 0 and 1 at the steps given, in a pattern of `steps`.
    auto project = [](int steps, std::initializer_list<int> at) {
        ymseq::Project p;
        p.setSteps(steps);
        for (int t = 0; t < 2; ++t)
            for (int s : at) p.patterns[0].edit(t).pat[size_t(s)] = int16_t(t);
        p.patterns[0].bpm[1] = 140;
        return p;
    };
    MidiImportOptions o;
    o.track = 2;

    // A bar on track 3 (index 2) in a pattern of one bar: tracks 1 and 2 stay, the Song gets one entry.
    ymseq::Project p = project(16, {0, 4});
    const ymseq::Project before = p;
    std::optional<MidiImport> r = importMidi(sixteenths({{0, 1}, {8, 9}}), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK(r->first == 0);
    CHECK(r->tracks == 1);
    CHECK(r->kept);
    CHECK(r->song);
    for (int t = 0; t < kTracks; ++t) CHECK(r->on[size_t(t)] == (t == 2));
    CHECK(p.patterns[0].steps == 16);
    CHECK(p.patterns[0].lane(0) == before.patterns[0].lane(0));
    CHECK(p.patterns[0].lane(1) == before.patterns[0].lane(1));
    CHECK(p.patterns[0].lane(2).pat[0] == 0);
    CHECK(p.patterns[0].lane(2).pat[8] == 0);
    REQUIRE(p.song.entries.size() == 1);
    CHECK(p.song.entries[0].pat == 0);

    // Again over it: track 3 replaced, not doubled; the Song, no longer empty, stays.
    p.song.entries.push_back({0, 2, 0, ymseq::SongKind::Pattern, 0, false});
    const ymseq::Song song = p.song;
    r = importMidi(sixteenths({{4, 5}}), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK_FALSE(r->song);
    CHECK(p.song == song);
    CHECK(p.patterns[0].lane(2).pat[0] < 0);
    CHECK(p.patterns[0].lane(2).pat[4] == 0);
    CHECK(p.patterns[0].lane(0) == before.patterns[0].lane(0));

    // A shorter part repeats over a longer pattern; the pattern keeps its length.
    p = project(64, {0, 40});
    r = importMidi(sixteenths({{0, 1}, {8, 9}}), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK(p.patterns[0].steps == 64);
    for (int s : {0, 8, 16, 24, 32, 40, 48, 56}) CHECK(p.patterns[0].lane(2).pat[size_t(s)] == 0);
    CHECK(p.patterns[0].lane(2).pat[4] < 0);
    CHECK(p.patterns[0].lane(0).pat[40] == 0);

    // A longer part lengthens the pattern; the other tracks and the BPM row repeat their steps over the new ones.
    p = project(16, {0, 4});
    r = importMidi(sixteenths({{0, 1}, {31, 32}}), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK(p.patterns[0].steps == 32);
    for (int t = 0; t < 2; ++t) {
        CHECK(p.patterns[0].lane(t).pat[16] == t);
        CHECK(p.patterns[0].lane(t).pat[20] == t);
        CHECK(p.patterns[0].lane(t).pat[17] < 0);
    }
    CHECK(p.patterns[0].bpm[17] == 140);
    CHECK(p.patterns[0].lane(2).pat[31] == 0);

    // From the current pattern; the patterns before it stay.
    p = project(16, {0});
    p.current = 5;
    p.used.set(5);
    r = importMidi(sixteenths({{0, 1}, {40, 41}}), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK(r->first == 5);
    CHECK(r->patterns == 1);
    CHECK(p.patterns[5].steps == 48);
    CHECK(p.patterns[5].lane(2).pat[40] == 0);
    CHECK(p.patterns[0].lane(2).empty());
    CHECK(p.patterns[0].lane(0).pat[0] == 0);
    REQUIRE(p.song.entries.size() == 1);
    CHECK(p.song.entries[0].pat == 5);
    // Up to the last pattern only: a file longer than the patterns left is cut there.
    p = ymseq::Project{};
    p.current = 31;
    r = importMidi(sixteenths({{0, 1}, {600, 601}}), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK(r->first == 31);
    CHECK(r->patterns == 1);
    CHECK(r->truncated);

    // Other tracks empty: nothing kept (the tempo of the file applies).
    p = ymseq::Project{};
    r = importMidi(sixteenths({{0, 1}}), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK_FALSE(r->kept);

    // Several channels: their tracks of the mode replaced, the others kept.
    p = project(16, {0});
    p.patterns[0].edit(5).pat[3] = 7;
    r = importMidi(channels(1), VoiceMode::Studio16, p, o);
    REQUIRE(r);
    CHECK(r->on[0]);
    CHECK(r->on[1]);
    CHECK_FALSE(r->on[5]);
    CHECK(r->kept);
    CHECK(p.patterns[0].lane(5).pat[3] == 7);
    CHECK(p.patterns[0].lane(1).pat[0] == gmSlot(38));
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
    CHECK(n[5].velocity == 76);  // in 32 channels the arrival takes its own level: 9 / 15 × 127
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
    // The render stops at the sample whose phase reaches the end of the last step.
    CHECK(e->seconds == Catch::Approx(0.48).margin(1.0 / 48000));
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
