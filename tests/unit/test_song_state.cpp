// STYM2149 — extended Song (M6, B12, D-158): ymseq model (Rest, Note-off and Loop entries, restart point), SNGX chunk
// of the state, bounds on reading back, engine snapshot.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "plugincore/Mapping.h"
#include "plugincore/State.h"
#include "ymseq/Song.h"

using namespace stym;
using namespace stym::plugin;
using ymseq::Song;
using ymseq::SongEntry;
using ymseq::SongKind;

namespace {
bool hasChunk(const std::vector<uint8_t>& b, const char* tag) {
    for (size_t i = 0; i + 4 <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, 4) == 0) return true;
    return false;
}
}  // namespace

TEST_CASE("Extended Song: labels, insertion, length, jumps and restart point", "[ymseq][song]") {
    Song s;
    s.add(0, false);
    s.insertEntryAfter(0, SongEntry::rest(SongKind::Rest));
    s.insertEntryAfter(1, SongEntry::rest(SongKind::NoteOff, 4));
    s.insertEntryAfter(2, SongEntry::loop(3, 2));
    REQUIRE(s.size() == 4);
    CHECK(songLabel(s.entries[1]) == "Rest16x1");
    CHECK(songLabel(s.entries[2]) == "Off4x1");
    CHECK(songLabel(s.entries[3]) == "Back3x2");
    // Add: a special block at the end does not take a repeat.
    s.add(0, false);
    CHECK(s.size() == 5);
    s.add(0, false);
    CHECK(s.entries[4].rep == 2);

    s.setLength({0, 1, 2, 3}, 300);
    CHECK(s.entries[0].arg == 0);  // pattern: no length
    CHECK(s.entries[1].arg == ymseq::kSteps);
    CHECK(s.entries[2].arg == ymseq::kSteps);
    CHECK(s.entries[3].arg == 3);
    s.setLength({1}, 0);
    CHECK(s.entries[1].arg == 1);
    s.setLoopBack({2, 3}, 999);
    CHECK(s.entries[2].arg == ymseq::kSteps);
    CHECK(s.entries[3].arg == ymseq::kSongLoopMax);
    s.setTempo({1, 3}, 140);
    CHECK(s.entries[1].bpm == 140);
    CHECK(s.entries[3].bpm == 0);  // a jump plays nothing

    // Restart point: only one; on the one that already is, back to the start.
    CHECK(s.restartIndex() == 0);
    s.toggleRestart(2);
    CHECK(s.restartIndex() == 2);
    CHECK(songLabel(s.entries[2]) == "|:Off256x1");
    s.toggleRestart(4);
    CHECK(s.restartIndex() == 4);
    CHECK_FALSE(s.entries[2].restart);
    s.toggleRestart(4);
    CHECK(s.restartIndex() == 0);
    CHECK_FALSE(s.entries[4].restart);

    // Copies and duplications without the restart point; a move keeps it.
    s.toggleRestart(1);
    const ymseq::SongSel dup = s.duplicate({1});
    REQUIRE(dup == ymseq::SongSel{2});
    CHECK_FALSE(s.entries[2].restart);
    CHECK(s.entries[2].kind == SongKind::Rest);
    CHECK(s.duplicateAt(1) == 2);
    CHECK_FALSE(s.entries[2].restart);
    for (const SongEntry& e : s.copy({0, 1, 2})) CHECK_FALSE(e.restart);
    ymseq::SongSel sel = {1};
    REQUIRE(s.dragMove(sel, 1, 4));
    CHECK(s.restartIndex() == sel.front());
    CHECK(s.entries[size_t(sel.front())].kind == SongKind::Rest);
    REQUIRE(s.moveLeft(sel));
    CHECK(s.restartIndex() == sel.front());
}

TEST_CASE("Extended Song: SNGX chunk written when needed, read back, clamped, engine snapshot", "[song][state]") {
    PluginState plain;
    plain.extras.seq.song.entries = {{0, 2, 0}, {3, 1, 120}};
    CHECK_FALSE(hasChunk(encodeState(plain), "SNGX"));

    PluginState st;
    auto& e = st.extras.seq.song.entries;
    e = {{0, 2, 0}, SongEntry::rest(SongKind::Rest, 8), SongEntry::rest(SongKind::NoteOff, 2), SongEntry::loop(3, 4),
         {5, 1, 0}};
    e[4].restart = true;
    const std::vector<uint8_t> bytes = encodeState(st);
    CHECK(hasChunk(bytes, "SNGX"));
    const DecodeResult back = decodeState(bytes);
    REQUIRE(back.ok);
    CHECK(back.unknownChunks == 0);
    CHECK(back.state.extras.seq.song == st.extras.seq.song);
    CHECK(encodeState(back.state) == bytes);

    // The restart point alone is enough to write the chunk.
    PluginState r = plain;
    r.extras.seq.song.entries[1].restart = true;
    CHECK(hasChunk(encodeState(r), "SNGX"));

    // Bounds: lengths, jumps, a single restart, no pattern or tempo for the special entries.
    NonParamState n;
    n.seq.song.entries = {SongEntry::rest(SongKind::Rest, 0), SongEntry::rest(SongKind::NoteOff, 999),
                          SongEntry::loop(0, 1), SongEntry::loop(400, 1), {7, 1, 0}};
    n.seq.song.entries[0].pat = 9;
    n.seq.song.entries[2].bpm = 150;
    n.seq.song.entries[4].arg = 5;
    n.seq.song.entries[1].restart = n.seq.song.entries[4].restart = true;
    sanitize(n);
    const auto& s = n.seq.song.entries;
    CHECK(s[0].arg == 1);
    CHECK(s[0].pat == 0);
    CHECK(s[1].arg == ymseq::kSteps);
    CHECK(s[2].arg == 1);
    CHECK(s[2].bpm == 0);
    CHECK(s[3].arg == ymseq::kSongLoopMax);
    CHECK(s[4].arg == 0);
    CHECK(s[1].restart);
    CHECK_FALSE(s[4].restart);

    // Engine snapshot: kind, argument and restart entry.
    ymengine::SeqData data;
    fillSequence(back.state.extras.seq, data);
    REQUIRE(data.song.size() == 5);
    CHECK(data.song[1].kind == ymengine::SeqSongKind::Rest);
    CHECK(data.song[1].arg == 8);
    CHECK(data.song[2].kind == ymengine::SeqSongKind::NoteOff);
    CHECK(data.song[3].kind == ymengine::SeqSongKind::Loop);
    CHECK(data.song[3].repeats == 4);
    CHECK(data.song[3].arg == 3);
    CHECK(data.song[4].pattern == 5);
    CHECK(data.songRestart == 4);
}

TEST_CASE("Extended Song: SNGX of another Song ignored", "[song][state]") {
    PluginState st;
    st.extras.seq.song.entries = {{1, 1, 0}, SongEntry::rest(SongKind::Rest, 4)};
    const std::vector<uint8_t> bytes = encodeState(st);
    // The entry count of SNGX (u16 after the chunk header) no longer matches that of SONG: larger than what the
    // chunk contains, or smaller.
    for (const uint8_t count : {uint8_t(3), uint8_t(1)}) {
        INFO("entries announced: " << int(count));
        std::vector<uint8_t> b = bytes;
        for (size_t i = 0; i + 10 <= b.size(); ++i)
            if (std::memcmp(b.data() + i, "SNGX", 4) == 0) b[i + 8] = count;
        const DecodeResult back = decodeState(b);
        REQUIRE(back.ok);
        REQUIRE(back.state.extras.seq.song.size() == 2);
        for (const SongEntry& e : back.state.extras.seq.song.entries) CHECK(e.kind == SongKind::Pattern);
    }
}
