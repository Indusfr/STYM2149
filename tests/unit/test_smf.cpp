// STYM2149 — standard MIDI files: bounded reading (the prototype's parseMidiFile) and SMF 1 writing
// (the prototype's midiVar, midiTrack, midiBlob)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <random>
#include <vector>

#include "ymio/Smf.h"

using namespace stym;
using namespace stym::ymio;

namespace {

std::vector<uint8_t> be(uint32_t v, int n) {
    std::vector<uint8_t> out;
    for (int i = n - 1; i >= 0; --i) out.push_back(uint8_t(v >> (8 * i)));
    return out;
}

std::vector<uint8_t> chunk(const char* id, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> out(id, id + 4);
    const auto len = be(uint32_t(data.size()), 4);
    out.insert(out.end(), len.begin(), len.end());
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<uint8_t> smf(int format, int division, const std::vector<std::vector<uint8_t>>& tracks) {
    std::vector<uint8_t> out = chunk("MThd", {0, uint8_t(format), 0, uint8_t(tracks.size()), uint8_t(division >> 8),
                                              uint8_t(division)});
    for (const auto& t : tracks) {
        const auto c = chunk("MTrk", t);
        out.insert(out.end(), c.begin(), c.end());
    }
    return out;
}

std::vector<uint8_t> varLen(uint32_t v) {
    std::vector<uint8_t> out;
    appendVarLen(out, v);
    return out;
}

}  // namespace

TEST_CASE("SMF: variable-length quantities (midiVar)", "[smf]") {
    CHECK(varLen(0) == std::vector<uint8_t>{0x00});
    CHECK(varLen(0x7F) == std::vector<uint8_t>{0x7F});
    CHECK(varLen(0x80) == std::vector<uint8_t>{0x81, 0x00});
    CHECK(varLen(0x2000) == std::vector<uint8_t>{0xC0, 0x00});
    CHECK(varLen(0x3FFF) == std::vector<uint8_t>{0xFF, 0x7F});
    CHECK(varLen(0x4000) == std::vector<uint8_t>{0x81, 0x80, 0x00});
    CHECK(varLen(0x0FFFFFFF) == std::vector<uint8_t>{0xFF, 0xFF, 0xFF, 0x7F});
    CHECK(varLen(0xFFFFFFFF) == std::vector<uint8_t>{0xFF, 0xFF, 0xFF, 0x7F});  // clamped to four bytes
}

TEST_CASE("SMF: format 1 writing like the prototype's midiBlob", "[smf]") {
    // Tempo track (120 BPM) and one track: name, note 60 from 0 to 36 ticks, note 62 from 36 to 48.
    std::vector<std::vector<SmfEvent>> tracks(2);
    tracks[0].push_back(smfTempo(0, 500000));
    tracks[1] = {smfTrackName(0, "ab"), smfNoteOn(0, 2, 60, 127), smfNoteOff(36, 2, 60), smfNoteOn(36, 2, 62, 64),
                 smfNoteOff(48, 2, 62)};
    const std::vector<uint8_t> want = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0, 96,
        'M', 'T', 'r', 'k', 0, 0, 0, 11, 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xFF, 0x2F, 0x00,
        'M', 'T', 'r', 'k', 0, 0, 0, 26, 0x00, 0xFF, 0x03, 2, 'a', 'b', 0x00, 0x92, 60, 127, 36, 0x82, 60, 0,
        0x00, 0x92, 62, 64, 12, 0x82, 62, 0, 0x00, 0xFF, 0x2F, 0x00};
    CHECK(writeSmf(96, tracks) == want);
    // 3/4 time signature: FF 58 04 nn dd 24 8.
    CHECK(smfTimeSignature(0, 3, 4).bytes == std::vector<uint8_t>{0xFF, 0x58, 0x04, 3, 2, 24, 8});
    CHECK(smfTimeSignature(0, 6, 8).bytes == std::vector<uint8_t>{0xFF, 0x58, 0x04, 6, 3, 24, 8});
    // Velocity of a note on clamped to 1-127; channel and note on their bits.
    CHECK(smfNoteOn(0, 9, 36, 0).bytes == std::vector<uint8_t>{0x99, 36, 1});
    CHECK(smfNoteOn(0, 9, 36, 300).bytes == std::vector<uint8_t>{0x99, 36, 127});
}

TEST_CASE("SMF: reading back a written file", "[smf]") {
    std::vector<std::vector<SmfEvent>> tracks(3);
    tracks[0].push_back(smfTempo(0, 600000));  // 100 BPM
    tracks[1] = {smfNoteOn(0, 0, 60, 100), smfNoteOn(10, 0, 64, 90), smfNoteOff(20, 0, 60), smfNoteOff(30, 0, 64)};
    tracks[2] = {smfNoteOn(5, 9, 36, 127), smfNoteOff(17, 9, 36)};
    const std::optional<SmfSong> s = readSmf(writeSmf(480, tracks));
    REQUIRE(s);
    CHECK(s->division == 480);
    CHECK(s->bpm == 100);
    CHECK(s->tempo);
    REQUIRE(s->notes.size() == 3);
    // In the order in which they close, track after track.
    CHECK((s->notes[0].channel == 0 && s->notes[0].note == 60 && s->notes[0].velocity == 100 && s->notes[0].start == 0 &&
           s->notes[0].end == 20));
    CHECK((s->notes[1].note == 64 && s->notes[1].start == 10 && s->notes[1].end == 30));
    CHECK((s->notes[2].channel == 9 && s->notes[2].note == 36 && s->notes[2].start == 5 && s->notes[2].end == 17));
}

TEST_CASE("SMF: reading like parseMidiFile (running status, velocity 0, open notes, tempo)", "[smf]") {
    // Running status on the note ons, note on of velocity 0 as note off, note reopened before its end (the first is
    // lost), note never closed (lost), controller, program, pitch bend, SysEx, meta.
    const std::vector<uint8_t> t1 = {
        0x00, 0xFF, 0x03, 0x01, 'x',        // track name
        0x00, 0x90, 60, 100,                // note 60
        0x05, 62, 80,                       // running status: note 62
        0x05, 60, 0,                        // note 60 closed (velocity 0)
        0x00, 0xB0, 7, 100,                 // controller
        0x00, 0xC0, 3,                      // program
        0x00, 0xE0, 0, 64,                  // pitch bend
        0x00, 0xF0, 0x02, 0x7E, 0xF7,       // SysEx
        0x02, 0x91, 70, 90,                 // note 70 channel 1
        0x02, 0x91, 70, 91,                 // reopened: the first start is lost
        0x03, 0x81, 70, 0,                  // closed
        0x01, 0x80, 62, 0,                  // note 62 closed
        0x00, 0x99, 40, 50,                 // never closed
        0x00, 0xFF, 0x2F, 0x00};
    // Two tempos: only the first counts (in the middle of a second track).
    const std::vector<uint8_t> t2 = {0x10, 0xFF, 0x51, 0x03, 0x00, 0x2D, 0xC6,  // 11718 µs : 5120 BPM → 300
                                     0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xFF, 0x2F, 0x00};
    const std::optional<SmfSong> s = readSmf(smf(1, 96, {t1, t2}));
    REQUIRE(s);
    CHECK(s->bpm == 300);
    REQUIRE(s->notes.size() == 3);
    CHECK((s->notes[0].note == 60 && s->notes[0].start == 0 && s->notes[0].end == 10 && s->notes[0].velocity == 100));
    CHECK((s->notes[1].channel == 1 && s->notes[1].note == 70 && s->notes[1].start == 14 && s->notes[1].end == 17 &&
           s->notes[1].velocity == 91));
    CHECK((s->notes[2].note == 62 && s->notes[2].start == 5 && s->notes[2].end == 18 && s->notes[2].velocity == 80));
    // Slow tempo: 20 BPM at least; without tempo: 120.
    const std::vector<uint8_t> slow = {0x00, 0xFF, 0x51, 0x03, 0xFF, 0xFF, 0xFF, 0x00, 0x90, 60, 1, 0x01, 0x80, 60, 0};
    CHECK(readSmf(smf(0, 96, {slow}))->bpm == 20);
    const std::vector<uint8_t> none = {0x00, 0x90, 60, 1, 0x01, 0x80, 60, 0};
    CHECK(readSmf(smf(0, 96, {none}))->bpm == 120);
    CHECK_FALSE(readSmf(smf(0, 96, {none}))->tempo);
}

TEST_CASE("SMF: refused files", "[smf]") {
    const std::vector<uint8_t> t = {0x00, 0x90, 60, 1, 0x01, 0x80, 60, 0};
    CHECK(readSmf(smf(0, 96, {t})));
    CHECK_FALSE(readSmf(smf(0, 0xE728, {t})));  // SMPTE
    CHECK_FALSE(readSmf(smf(0, 0, {t})));       // zero division
    std::vector<uint8_t> bad = smf(0, 96, {t});
    bad[0] = 'X';
    CHECK_FALSE(readSmf(bad));
    CHECK_FALSE(readSmf(std::vector<uint8_t>(bad.begin(), bad.begin() + 13)));
    // Unknown chunk in place of a track: reading stops there (no notes).
    std::vector<uint8_t> other = chunk("MThd", {0, 1, 0, 2, 0, 96});
    const auto junk = chunk("XFIH", {1, 2, 3});
    const auto trk = chunk("MTrk", t);
    other.insert(other.end(), junk.begin(), junk.end());
    other.insert(other.end(), trk.begin(), trk.end());
    REQUIRE(readSmf(other));
    CHECK(readSmf(other)->notes.empty());
}

TEST_CASE("SMF: truncated or damaged files read without going past their bounds", "[smf][fuzz]") {
    // Valid reference file, then cuts at every length and random bytes: never a read outside the buffer (sanitizers
    // in CI), bounded notes.
    std::vector<uint8_t> t;
    for (int k = 0; k < 40; ++k) {
        const std::vector<uint8_t> ev = {uint8_t(k % 3), uint8_t(0x90 | (k % 4)), uint8_t(40 + k), 100,
                                         uint8_t(1 + k % 5), uint8_t(0x80 | (k % 4)), uint8_t(40 + k), 0};
        t.insert(t.end(), ev.begin(), ev.end());
        if (k % 7 == 0) t.insert(t.end(), {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xF0, 0x01, 0xF7});
    }
    const std::vector<uint8_t> good = smf(1, 96, {t, t});
    REQUIRE(readSmf(good)->notes.size() == 80);
    for (size_t cut = 0; cut <= good.size(); ++cut) {
        const std::vector<uint8_t> part(good.begin(), good.begin() + std::ptrdiff_t(cut));
        const std::optional<SmfSong> s = readSmf(part);
        if (s) CHECK(s->notes.size() <= 80);
    }
    std::mt19937 rng(2149);
    for (int round = 0; round < 3000; ++round) {
        std::vector<uint8_t> b = good;
        const int flips = 1 + int(rng() % 8);
        for (int k = 0; k < flips; ++k) b[14 + rng() % (b.size() - 14)] = uint8_t(rng());
        if (rng() % 4 == 0) b.resize(14 + rng() % (b.size() - 14));
        const std::optional<SmfSong> s = readSmf(b);
        if (!s) continue;
        for (const SmfNote& n : s->notes) {
            CHECK((n.channel >= 0 && n.channel < 16 && n.note >= 0 && n.note < 128 && n.velocity >= 1 &&
                   n.velocity < 128));
        }
        CHECK((s->bpm >= 20 && s->bpm <= 300));
    }
}
