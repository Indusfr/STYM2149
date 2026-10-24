// STYM2149 — sequencer model: tables, patterns (copy on write), Song, bank, undo, Euclidean.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Behaviours written from the prototype's functions (refreshSong, songKey, selectPattern, transposePattern,
// pushUndo/doUndo/doRedo, euclidBtn); the track edits are compared with the prototype in test_ymseq_edit.cpp.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "ymseq/Commands.h"
#include "ymseq/Edit.h"
#include "ymseq/Euclid.h"
#include "ymseq/Project.h"
#include "ymseq/Tables.h"
#include "ymseq/Undo.h"

using namespace stym::ymseq;

TEST_CASE("ymseq: scales, figures, bars and names", "[ymseq]") {
    REQUIRE(kScales[0].name == "Off");
    REQUIRE(kScales[1].name == "Major (Ionian)");
    REQUIRE(kScales[25].name == "Chromatic");
    CHECK(scaleMask(0) == 0);
    CHECK(scaleMask(1) == 0xAB5);
    CHECK(inScale(61, 0, 0));
    CHECK_FALSE(inScale(61, 1, 0));
    CHECK(inScale(61, 1, 1));  // C# major
    // Snap: the nearest degree, downwards first; nothing without Snap or without a scale.
    CHECK(snapToScale(61, 1, 0, true) == 60);
    CHECK(snapToScale(66, 1, 0, true) == 65);
    CHECK(snapToScale(61, 1, 0, false) == 61);
    CHECK(snapToScale(61, 0, 0, true) == 61);
    CHECK(snapToScale(63, 15, 0, true) == 63);  // minor pentatonic: E flat belongs to it
    // Figures: 1 step for Step, otherwise round(beats × steps per quarter note), at least 1.
    CHECK(figureSteps(0, 2) == 1);
    CHECK(figureSteps(1, 2) == 16);  // whole note in 1/16
    CHECK(figureSteps(10, 2) == 3);  // quarter-note triplet in 1/16: round(2.67)
    CHECK(figureSteps(6, 0) == 1);   // thirty-second note in 1/4: round(0.125) → 1
    CHECK(figureSteps(9, 2) == 2);   // dotted sixteenth in 1/16: round(1.5) = 2 (JavaScript rounding)
    CHECK(beatSteps(2, 2) == 4);
    CHECK(beatSteps(7, 2) == 2);  // 6/8: eighth note
    CHECK(barSteps(7, 2) == 12);
    CHECK(barSteps(0, 4) == 4);   // 2/4 in quarter-note triplets: 2 × round(1.5) = 2 × 2
    CHECK(absNoteName(60, NoteNames::English) == "C4");
    CHECK(absNoteName(23, NoteNames::Latin) == "Si0");
    CHECK(absNoteName(11, NoteNames::English) == "B-1");
    // Typed back (sixth DAW test, item 66): either naming, case ignored, flats, a MIDI number.
    CHECK(parseAbsNote("C4") == 60);
    CHECK(parseAbsNote("c#4") == 61);
    CHECK(parseAbsNote("Sol3") == 55);
    CHECK(parseAbsNote("Si0") == 23);
    CHECK(parseAbsNote("Bb3") == 58);
    CHECK(parseAbsNote("B-1") == 11);
    CHECK(parseAbsNote("Do-1") == 0);
    CHECK(parseAbsNote("72") == 72);
    CHECK(parseAbsNote("G9") == 127);
    CHECK(parseAbsNote("G#9") == std::nullopt);  // 128
    CHECK(parseAbsNote("C") == std::nullopt);
    CHECK(parseAbsNote("H4") == std::nullopt);
    CHECK(parseAbsNote("128") == std::nullopt);
    CHECK(euclidName(3, 8) == "Tresillo");
    CHECK(euclidName(5, 16) == "Bossa-Nova");
    CHECK(euclidName(4, 16).empty());
}

TEST_CASE("ymseq: absolute notes and octave 0", "[ymseq]") {
    Lane l;
    l.pat[0] = 11;
    l.oct[0] = -1;  // octave 0: B0
    l.pat[1] = 0;   // octave of the track
    l.pat[2] = 4;
    l.oct[2] = 6;
    l.vpat[0][1] = 7;
    CHECK(absoluteNote(l, 1, 0, 3) == 23);
    CHECK(absoluteNote(l, 1, 1, 3) == 48);
    CHECK(absoluteNote(l, 1, 2, 3) == 88);
    CHECK(absoluteNote(l, 2, 1, 3) == 55);
    CHECK(absoluteNote(l, 1, 3, 3) == -1);
    CHECK(octaveCode(0, 3) == -1);
    CHECK(octaveCode(3, 3) == 0);
    CHECK(octaveCode(5, 3) == 5);
    CHECK(bankLetter(0) == 'A');
    CHECK(bankLetter(31) == 'D');
}

TEST_CASE("ymseq: shared tracks copied on write", "[ymseq]") {
    Pattern p;
    CHECK(p.lane(3).empty());
    p.edit(3).pat[5] = 7;
    const Pattern snapshot = p;  // undo snapshot: shares the tracks
    REQUIRE(snapshot.lanes[3] == p.lanes[3]);
    p.edit(3).pat[6] = 2;  // the shared track is copied, the snapshot does not change
    CHECK(snapshot.lane(3).pat[6] == -1);
    CHECK(p.lane(3).pat[6] == 2);
    CHECK(snapshot.lanes[3] != p.lanes[3]);
    CHECK_FALSE(snapshot.sameContent(p));
    const auto* before = p.lanes[3].get();
    p.edit(3).pat[7] = 1;  // sole owner: modified in place
    CHECK(p.lanes[3].get() == before);
    p.edit(3) = Lane{};
    p.compact();
    CHECK(p.lanes[3] == nullptr);
    Pattern q;
    CHECK(q.sameContent(p));
}

TEST_CASE("ymseq: INS row (M6), empty at 0, follows the note like GEN (D-147)", "[ymseq]") {
    Lane l;
    CHECK(Lane::emptyValue(Row::Ins) == 0);
    l.set(Row::Ins, 3, 32);
    CHECK(l.get(Row::Ins, 3) == 32);
    CHECK(l.ins[3] == 32);
    CHECK_FALSE(l.empty());
    l.clearStep(3);
    CHECK(l.empty());

    EditContext ctx;
    ctx.length = 16;
    LaneEditor e(l, ctx);
    e.writeNoteAt(0, 4);
    e.setNoteLen(0, 4);
    l.ins[0] = 7;
    l.fx[0] = 2;
    // Split: the second half keeps the instrument; new start: it follows the note.
    REQUIRE(e.splitNote(0, 2));
    CHECK(l.ins[2] == 7);
    CHECK(e.setNoteStart(2, 3) == 3);
    CHECK(l.ins[3] == 7);
    CHECK(l.fx[3] == 2);
    // Note copied and pasted.
    const NoteClip clip = e.copyNoteAt(0);
    CHECK(clip.ins == 7);
    REQUIRE(e.pasteNoteAt(8, 1, clip));
    CHECK(l.ins[8] == 7);
    // Rotation: INS rotates with GEN.
    e.rotate(1);
    CHECK(l.ins[9] == 7);
    CHECK(l.ins[1] == 7);
    CHECK(l.ins[0] == 0);
    // Step erased.
    e.eraseAt(9);
    CHECK(l.ins[9] == 0);
    // Block copied with all rows, or INS alone.
    const BlockClip all = e.blockCopy(1, 1, std::nullopt);
    const BlockClip ins = e.blockCopy(1, 1, Row::Ins);
    e.blockPaste(12, all);
    CHECK(l.ins[12] == 7);
    e.blockClear(12, 12, Row::Ins);
    CHECK(l.ins[12] == 0);
    CHECK(l.pat[12] >= 0);
    e.blockPaste(13, ins);
    CHECK(l.ins[13] == 7);
    CHECK(l.pat[13] < 0);
}

TEST_CASE("ymseq: Snap keeps every note written in the scale, chord voices included (D-280)", "[ymseq]") {
    Lane l;
    EditContext ctx;
    ctx.length = 16;
    ctx.scale = 1;  // Major (Ionian), on C
    ctx.snap = true;
    auto rows = [&](int s) {  // pitches written at step s: the root, then the chord voices
        std::vector<int> out;
        const LaneEditor e(l, ctx);
        for (int v = 1; v <= kPolyMax; ++v)
            if (const auto r = e.voiceRow(s, v)) out.push_back(*r);
        return out;
    };
    LaneEditor e(l, ctx);
    CHECK(e.snapRow(1) == 0);  // C# goes down to C, like the prototype's snapToScale
    CHECK(e.snapRow(6) == 5);  // F# to F
    CHECK(e.snapRow(-1) == -1);
    // A voice added on a greyed row goes to a degree, like the root.
    e.writeNoteAt(0, 1);
    REQUIRE(e.addVoiceAt(0, 6) == 2);
    CHECK(rows(0) == std::vector<int>{0, 5});
    // The chord of the input stands on the degree: a major triad clicked on D# (greyed) gives D, F, A.
    ctx.chord = 0;  // Major
    LaneEditor ec(l, ctx);
    ec.writeNoteAt(2, 3);
    CHECK(ec.writeChord(2, 3) == 2);
    CHECK(rows(2) == std::vector<int>{2, 5, 9});
    // A chord voice moved to a greyed row, then transposed with the selection, stays in the scale.
    LaneEditor::Moving m{2, 5, 1, false, false, 2, 0};
    ec.moveNoteTo(m, 4, 6);  // F# → F
    CHECK(m.from == 4);
    CHECK(m.row == 5);
    CHECK(rows(4) == std::vector<int>{5});
    CHECK(ec.transposeSel({{4, m.voice}}, 3));  // G# → G
    CHECK(rows(4) == std::vector<int>{7});
    // A chord voice that lands on a pitch already written at its step is left out: Sus2 on D# in Hirajoshi
    // (C, D, D#, G, G#) gives D# and G#, the F coming back to the root.
    REQUIRE(kScales[17].name == "Japanese (Hirajoshi)");
    ctx.scale = 17;
    ctx.chord = 2;  // Sus2
    LaneEditor eh(l, ctx);
    eh.writeNoteAt(6, 3);
    CHECK(eh.writeChord(6, 3) == 1);
    CHECK(rows(6) == std::vector<int>{3, 8});
    // Without Snap, the row clicked is kept (the scale only greys it); drums are never snapped (D-090).
    ctx.snap = false;
    LaneEditor ef(l, ctx);
    REQUIRE(ef.addVoiceAt(0, 8) == 3);
    CHECK(rows(0) == std::vector<int>{0, 5, 8});
    ctx.snap = true;
    ctx.drum = true;
    CHECK(LaneEditor(l, ctx).snapRow(1) == 1);
}

TEST_CASE("ymseq: CMD row (M6), attached to the line and not to the note (D-150)", "[ymseq][commands]") {
    using stym::ymengine::SeqCmd;
    using stym::ymengine::seqCmd;
    Lane l;
    CHECK(Lane::emptyValue(Row::CmdSync) == 0);
    l.set(Row::CmdSync, 3, seqCmd(SeqCmd::Sync, 0xff));
    CHECK(l.get(Row::CmdSync, 3) == 0x4ff);
    CHECK_FALSE(l.empty());
    l.clearStep(3);
    CHECK(l.empty());

    EditContext ctx;
    ctx.length = 16;
    LaneEditor e(l, ctx);
    e.writeNoteAt(0, 4);
    e.setNoteLen(0, 4);
    l.setCmd(0, seqCmd(SeqCmd::VolSlide, 0x37));
    l.setCmd(2, seqCmd(SeqCmd::Retrig, 0x02));
    l.setCmd(2, seqCmd(SeqCmd::Delay, 0x03));  // a second command on the same line (D-330)
    // Split, new start, note copy: the commands stay on their line.
    REQUIRE(e.splitNote(0, 2));
    CHECK(l.cmdAt(SeqCmd::Retrig, 2) == seqCmd(SeqCmd::Retrig, 0x02));
    CHECK(e.setNoteStart(2, 3) == 3);
    CHECK_FALSE(l.hasCmd(3));
    CHECK(l.cmdAt(SeqCmd::Retrig, 2) == seqCmd(SeqCmd::Retrig, 0x02));
    REQUIRE(e.pasteNoteAt(8, 1, e.copyNoteAt(0)));
    CHECK_FALSE(l.hasCmd(8));
    // Note erased: the command stays.
    e.eraseAt(0);
    CHECK(l.cmdAt(SeqCmd::VolSlide, 0) == seqCmd(SeqCmd::VolSlide, 0x37));
    // Rotation: the commands rotate with their line.
    e.rotate(1);
    CHECK(l.cmdAt(SeqCmd::VolSlide, 1) == seqCmd(SeqCmd::VolSlide, 0x37));
    CHECK(l.cmdAt(SeqCmd::Retrig, 3) == seqCmd(SeqCmd::Retrig, 0x02));
    CHECK(l.cmdAt(SeqCmd::Delay, 3) == seqCmd(SeqCmd::Delay, 0x03));
    CHECK_FALSE(l.hasCmd(0));
    // Block: CMD alone, all the kinds of its box together; Dxx has a row of its own (DLY, item 139, D-348), Rxx goes
    // with the ENV row (twelfth DAW test, item 146).
    const BlockClip cmd = e.blockCopy(1, 3, Row::CmdVolSlide);
    e.blockPaste(10, cmd);
    CHECK(l.cmdAt(SeqCmd::VolSlide, 10) == seqCmd(SeqCmd::VolSlide, 0x37));
    CHECK(l.cmdAt(SeqCmd::Retrig, 12) == 0);
    CHECK(l.cmdAt(SeqCmd::Delay, 12) == 0);
    e.blockPaste(10, e.blockCopy(1, 3, Row::CmdShape));
    CHECK(l.cmdAt(SeqCmd::Retrig, 12) == seqCmd(SeqCmd::Retrig, 0x02));
    e.blockPaste(10, e.blockCopy(1, 3, Row::CmdDelay));
    CHECK(l.cmdAt(SeqCmd::Delay, 12) == seqCmd(SeqCmd::Delay, 0x03));
    CHECK(l.cmdAt(SeqCmd::Retrig, 12) == seqCmd(SeqCmd::Retrig, 0x02));
    e.blockClear(10, 12, Row::CmdVolSlide);
    CHECK_FALSE(l.hasCmd(10));
    CHECK(l.cmdAt(SeqCmd::Retrig, 12) == seqCmd(SeqCmd::Retrig, 0x02));
    CHECK(l.cmdAt(SeqCmd::Delay, 12) == seqCmd(SeqCmd::Delay, 0x03));
    e.blockClear(10, 12, Row::CmdShape);
    CHECK(l.cmdAt(SeqCmd::Retrig, 12) == 0);
    e.blockClear(10, 12, Row::CmdDelay);
    CHECK_FALSE(l.hasCmd(12));
}

TEST_CASE("ymseq: one command of each kind per step, MUL in the cell (tenth DAW test, item 122, D-330)",
          "[ymseq][commands]") {
    using stym::ymengine::SeqCmd;
    using stym::ymengine::seqCmd;
    Lane l;
    CHECK(cmdCellText(l, 0) == "-");
    // The short names of the kinds of the Commands box.
    const std::array<std::pair<SeqCmd, const char*>, 5> tags = {{{SeqCmd::Speed, "TIK"},
                                                                 {SeqCmd::Sync, "SYN"},
                                                                 {SeqCmd::VolSlide, "VSL"},
                                                                 {SeqCmd::EnvSlide, "BSL"},
                                                                 {SeqCmd::NoiseSlide, "NSL"}}};
    for (size_t i = 0; i < tags.size(); ++i) {
        CAPTURE(i);
        const int s = 20 + int(i);
        l.setCmd(s, seqCmd(tags[i].first, 1));
        CHECK(cmdCellText(l, s) == tags[i].second);
        CHECK(cmdTag(tags[i].first) == tags[i].second);
        CHECK(stym::ymengine::seqCmdInBox(tags[i].first));
        CHECK(isCmdBoxRow(cmdRow(tags[i].first)));
    }
    // The kinds with a row of their own (VIB, BPM, ENV, DLY, CUT: items 132, 133 and 138 to 140; LFO, MOD, ARP, ENV
    // for Rxx, SMP, TUN: twelfth DAW test, items 142 to 146, 152 and 153) stay out of the cell.
    const std::array<std::pair<SeqCmd, const char*>, 11> own = {{{SeqCmd::Vibrato, "VIB"},
                                                                 {SeqCmd::Tempo, "BPM"},
                                                                 {SeqCmd::Shape, "SHP"},
                                                                 {SeqCmd::Delay, "DLY"},
                                                                 {SeqCmd::Cut, "CUT"},
                                                                 {SeqCmd::Retrig, "RST"},
                                                                 {SeqCmd::Offset, "OFS"},
                                                                 {SeqCmd::Fine, "FIN"},
                                                                 {SeqCmd::Lfo, "LFO"},
                                                                 {SeqCmd::Mod, "MOD"},
                                                                 {SeqCmd::Arpeggiator, "ARP"}}};
    for (size_t i = 0; i < own.size(); ++i) {
        CAPTURE(i);
        const int s = 30 + int(i);
        l.setCmd(s, seqCmd(own[i].first, 9));
        CHECK(cmdCellText(l, s) == "-");
        CHECK(l.hasCmd(s));
        CHECK(cmdTag(own[i].first) == own[i].second);
        CHECK(stym::ymengine::seqCmdOwnRow(own[i].first));
        CHECK_FALSE(stym::ymengine::seqCmdInBox(own[i].first));
        CHECK_FALSE(isCmdBoxRow(cmdRow(own[i].first)));
    }
    // Several on a step: MUL; each keeps its own value, a new one replaces only its kind.
    l.setCmd(0, seqCmd(SeqCmd::Sync, 0));
    l.setCmd(0, seqCmd(SeqCmd::VolSlide, 0x8c));
    l.setCmd(0, seqCmd(SeqCmd::Tempo, 0x8c));
    CHECK(cmdCellText(l, 0) == "MUL");
    l.setCmd(0, seqCmd(SeqCmd::VolSlide, 0x47));
    CHECK(l.cmdAt(SeqCmd::VolSlide, 0) == seqCmd(SeqCmd::VolSlide, 0x47));
    CHECK(l.cmdAt(SeqCmd::Sync, 0) == seqCmd(SeqCmd::Sync, 0));
    CHECK(l.cmdAt(SeqCmd::Tempo, 0) == seqCmd(SeqCmd::Tempo, 0x8c));
    l.clearCmd(SeqCmd::VolSlide, 0);
    CHECK(cmdCellText(l, 0) == "SYN");
    // The kinds of the box alone: emptied, copied.
    l.clearBoxCmds(0);
    CHECK(cmdCellText(l, 0) == "-");
    CHECK(l.cmdAt(SeqCmd::Tempo, 0) == seqCmd(SeqCmd::Tempo, 0x8c));
    Lane m;
    m.setCmd(5, seqCmd(SeqCmd::Speed, 3));
    m.setCmd(5, seqCmd(SeqCmd::Delay, 2));
    l.copyBoxCmds(0, m, 5);
    CHECK(l.cmdAt(SeqCmd::Speed, 0) == seqCmd(SeqCmd::Speed, 3));
    CHECK(l.cmdAt(SeqCmd::Delay, 0) == 0);
    CHECK(l.cmdAt(SeqCmd::Tempo, 0) == seqCmd(SeqCmd::Tempo, 0x8c));
    // Rows: a value goes to the row of its kind (the single CMD row of older states), 0 clears the row alone.
    l.set(Row::CmdArp, 7, seqCmd(SeqCmd::Speed, 3));
    CHECK(l.cmdAt(SeqCmd::Speed, 7) == seqCmd(SeqCmd::Speed, 3));
    CHECK(l.get(Row::CmdArp, 7) == 0);
    CHECK(l.get(Row::CmdSpeed, 7) == seqCmd(SeqCmd::Speed, 3));
    l.set(Row::CmdNoiseSlide, 7, seqCmd(SeqCmd::NoiseSlide, 0x37));
    l.set(Row::CmdNoiseSlide, 7, 0);
    CHECK(cmdCellText(l, 7) == "TIK");
    CHECK(cmdRow(SeqCmd::Vibrato) == Row::CmdVibrato);
    CHECK(cmdKind(Row::CmdTempo) == SeqCmd::Tempo);
    CHECK(cmdKind(Row::Ins) == SeqCmd::None);
}

TEST_CASE("ymseq: Xyz and Pxy are retired, a value written for them is dropped (items 131 and 135, D-340, D-344)",
          "[ymseq][commands]") {
    using stym::ymengine::SeqCmd;
    using stym::ymengine::seqCmd;
    Lane l;
    l.setCmd(9, seqCmd(SeqCmd::Arp, 0x47));
    l.set(Row::CmdPitchSlide, 9, seqCmd(SeqCmd::PitchSlide, 0x10));
    l.set(Row::CmdArp, 9, seqCmd(SeqCmd::Arp, 0x37));  // the single CMD row of older states
    CHECK(l.cmdAt(SeqCmd::Arp, 9) == 0);
    CHECK(l.cmdAt(SeqCmd::PitchSlide, 9) == 0);
    CHECK_FALSE(l.hasCmd(9));
    CHECK(l.empty());
    CHECK(cmdCellText(l, 9) == "-");
    CHECK(stym::ymengine::seqCmdRetired(SeqCmd::Arp));
    CHECK(stym::ymengine::seqCmdRetired(SeqCmd::PitchSlide));
}

TEST_CASE("ymseq: Song, additions, repeats and tempo", "[ymseq][song]") {
    Song s;
    s.add(0, false);
    s.add(0, false);  // same pattern: one more repeat
    s.add(0, true);   // Shift: separate block
    s.add(9, false);
    REQUIRE(s.size() == 3);
    CHECK(s.entries[0] == SongEntry{0, 2, 0});
    CHECK(s.entries[2].pat == 9);
    CHECK(songLabel(s.entries[0]) == "A1x2");
    CHECK(songLabel(s.entries[2]) == "B2x1");
    CHECK(songLabel({31, 16, 0}) == "D8x16");
    s.setRepeat({1, 2, 7}, 8);
    CHECK(s.entries[1].rep == 8);
    CHECK(s.entries[2].rep == 8);
    s.decRepeat(0);
    s.decRepeat(0);
    CHECK(s.entries[0].rep == 1);
    s.incRepeat(0);
    CHECK(s.entries[0].rep == 2);
    s.setTempo({0}, 500);
    CHECK(s.entries[0].bpm == 300);
    s.setTempo({0}, 0);  // "0" entered: clamped to 20
    CHECK(s.entries[0].bpm == 20);
    s.setTempo({0}, std::nullopt);
    CHECK(s.entries[0].bpm == 0);
    Song e;
    CHECK(e.insertAfter(5, 3) == 0);
    CHECK(e.insertAfter(0, 4) == 1);
    CHECK(e.insertAfter(0, 5) == 1);
    CHECK(e.entries[1].pat == 5);
    CHECK(e.duplicateAt(1) == 2);
    CHECK(e.entries[2].pat == 5);
    CHECK(e.remove({}, 3) == 2);  // no selection: the block under the cursor, then cursor clamped
    CHECK(e.size() == 3);
    CHECK(e.remove({0, 2, 9}, 2) == 0);
    REQUIRE(e.size() == 1);
    CHECK(e.entries[0].pat == 5);
}

TEST_CASE("ymseq: Song, blocks copied, moved and dragged", "[ymseq][song]") {
    auto make = [] {
        Song s;
        for (int p : {0, 1, 2, 3, 4}) s.append(p);
        return s;
    };
    auto pats = [](const Song& s) {
        std::string o;
        for (const auto& e : s.entries) o += char('A' + e.pat);
        return o;
    };
    Song s = make();
    CHECK(s.duplicate({1, 2}) == SongSel{3, 4});
    CHECK(pats(s) == "ABCBCDE");
    s = make();
    const auto clip = s.copy({3, 1});
    s.erase({1, 3});
    CHECK(pats(s) == "ACE");
    CHECK(s.pasteAfter(0, clip) == SongSel{1, 2});
    CHECK(pats(s) == "ABDCE");
    CHECK(s.pasteAfter(-1, clip) == SongSel{0, 1});
    s = make();
    SongSel sel{1, 3};
    CHECK(s.moveLeft(sel));
    CHECK(pats(s) == "BADCE");
    CHECK(sel == SongSel{0, 2});
    CHECK_FALSE(s.moveLeft(sel));  // at the limit
    CHECK(s.moveRight(sel));
    CHECK(pats(s) == "ABCDE");
    sel = {4};
    CHECK_FALSE(s.moveRight(sel));
    // Drag and drop: to the right, the block goes after the hovered block; to the left, before it.
    s = make();
    sel = {};
    CHECK(s.dragMove(sel, 0, 3));
    CHECK(pats(s) == "BCDAE");
    CHECK(sel == SongSel{3});
    s = make();
    CHECK(s.dragMove(sel, 4, 1));
    CHECK(pats(s) == "AEBCD");
    s = make();
    sel = {0, 1};  // the whole selection follows the grabbed block
    CHECK(s.dragMove(sel, 1, 3));
    CHECK(pats(s) == "CDABE");
    CHECK(sel == SongSel{2, 3});
    sel = {0, 1};
    CHECK_FALSE(s.dragMove(sel, 0, 1));  // dropped on the selection: nothing
    sel = {2, 4};
    CHECK(s.dragMove(sel, 3, 1));  // grabbed block outside the selection: alone
    CHECK(pats(s) == "CBDAE");
}

TEST_CASE("ymseq: pattern bank, wait during playback, cut and transposition", "[ymseq]") {
    Project p;
    p.setSteps(24);
    CHECK(p.used.count() == 1);
    CHECK(p.select(9, false));
    CHECK(p.current == 9);
    CHECK(p.steps() == 24);  // pattern created at the length of the current one
    CHECK(p.bankLast[1] == 9);
    CHECK_FALSE(p.select(9, false));
    CHECK(p.select(17, true));  // during playback: switches at the end of the loop
    CHECK(p.current == 9);
    CHECK(p.pending == 17);
    CHECK_FALSE(p.select(17, true));
    p.applied(17);
    CHECK(p.current == 17);
    CHECK(p.pending == -1);
    p.tracks[2].length = 5;
    CHECK(p.trackLength(2) == 5);
    CHECK(p.trackLength(3) == 24);

    // Pattern transposition: melodic tracks, octave 0 read back, chord voices, drums untouched.
    Lane& l = p.cur().edit(0);
    l.pat[0] = 11;
    l.oct[0] = -1;  // B0
    l.pat[1] = 0;   // C3 (octave of the track)
    l.vpat[0][1] = 4;
    l.pat[2] = 12;
    l.oct[2] = 9;  // C10: clipped
    p.cur().edit(1).pat[0] = 3;  // drum
    std::bitset<kTracks> drums;
    drums.set(1);
    p.transposePattern(17, 1, drums);
    const Lane& t = p.cur().lane(0);
    CHECK(absoluteNote(t, 1, 0, 3) == 24);
    CHECK(absoluteNote(t, 1, 1, 3) == 49);
    CHECK(absoluteNote(t, 2, 1, 3) == 53);
    CHECK(absoluteNote(t, 1, 2, 3) == 132);
    CHECK(p.cur().lane(1).pat[0] == 3);
    p.transposePattern(17, -2, drums);
    CHECK(absoluteNote(p.cur().lane(0), 1, 0, 3) == 22);
    CHECK(p.cur().lane(0).oct[0] == -1);

    const Pattern clip = p.copyPattern(17);
    p.cutPattern(17);
    CHECK(p.cur().lane(0).empty());
    CHECK(p.steps() == 24);
    p.pastePattern(3, clip);
    CHECK(p.used.test(3));
    CHECK(p.patterns[3].sameContent(clip));
}

TEST_CASE("ymseq: undo over 50 levels, identical levels skipped", "[ymseq][undo]") {
    UndoHistory<int> h;
    int state = 0;
    auto edit = [&](int v) {
        h.push(state);
        state = v;
    };
    auto current = [&](const int&) { return state; };
    edit(1);
    edit(1);  // level 1 is identical to the previous one: not pushed
    CHECK(h.undoSize() == 2);
    edit(2);
    auto e = h.undo(current);
    REQUIRE(e);
    state = *e;
    CHECK(state == 1);
    e = h.undo(current);  // level 1 (state before the second "1") is identical to the current state: skipped
    REQUIRE(e);
    state = *e;
    CHECK(state == 0);
    CHECK_FALSE(h.undo(current));
    e = h.redo(current);
    REQUIRE(e);
    state = *e;
    CHECK(state == 1);
    e = h.redo(current);
    REQUIRE(e);
    state = *e;
    CHECK(state == 2);
    CHECK_FALSE(h.canRedo());
    edit(3);
    h.push(99);
    h.push(98);
    CHECK_FALSE(h.canRedo());
    for (int i = 0; i < 80; ++i) edit(100 + i);
    CHECK(h.undoSize() == UndoHistory<int>::kMax);
}

TEST_CASE("ymseq: Euclidean generator", "[ymseq][euclid]") {
    const EuclidFigure f = euclidFigure(8, 3, 1, 2);
    CHECK(f.hits == std::vector<int>{0, 1, 0, 0, 1, 0, 0, 1});
    CHECK(f.accents == std::vector<int>{0, 1, 0, 0, 1, 0, 0, 0});  // E(2,3) = 110 on the hits
    const EuclidFigure g = euclidFigure(4, 9, 6, 0);  // hits clamped to the steps, rotation modulo
    CHECK(g.pulses == 4);
    CHECK(g.rotate == 2);
    CHECK(g.accents.empty());
    CHECK(euclidFigure(1, 1, 0, 0).steps == kEuclidMinSteps);  // 2 steps at least, like the window's S slider

    Lane l;
    l.pat[1] = 5;
    l.tie[1] = 1;
    l.vpat[0][1] = 2;
    EditContext ctx;
    ctx.length = 12;
    ctx.trackOctave = 3;
    LaneEditor e(l, ctx);
    euclidApply(e, l, f, EuclidTarget::Melodic, 7, false);
    CHECK(l.tie[1] == 0);
    CHECK(l.vpat[0][1] == -1);
    CHECK(l.pat[1] == 7);
    CHECK(l.vol[1] == 15);
    CHECK(l.pat[4] == 7);
    CHECK(l.vol[4] == 15);
    CHECK(l.vol[7] == 10);
    CHECK(l.pat[9] == 7);  // the rhythm repeats
    CHECK(l.pat[12] == -1);  // beyond the track

    Lane d;
    EditContext dc;
    dc.drum = true;
    LaneEditor de(d, dc);
    euclidApply(de, d, euclidFigure(4, 2, 0, 0), EuclidTarget::Digidrum, 3, false);
    CHECK(d.pat[0] == 2);  // D-103: Slot 3 = row 2
    CHECK(d.slt[0] == 3);
    CHECK(d.vol[0] == 15);  // without an accent, VOL is not written
    euclidApply(de, d, euclidFigure(4, 1, 1, 0), EuclidTarget::Chipdrum, 6, true);  // Merge: kept
    CHECK(d.pat[0] == 2);
    CHECK(d.pat[1] == 6);
    dc.digidrumSlot = 5;  // "Chan": the track's slot
    LaneEditor dk(d, dc);
    euclidApply(dk, d, euclidFigure(4, 1, 0, 0), EuclidTarget::Digidrum, 0, false);
    CHECK(d.pat[0] == 5);
    CHECK(d.slt[0] == 0);
    CHECK(d.pat[1] == -1);
}

TEST_CASE("ymseq: Euclidean Apply asks only when it replaces written notes (item 155)", "[ymseq][euclid]") {
    const EuclidFigure f = euclidFigure(8, 2, 0, 0);  // hits on steps 0 and 4
    EditContext ctx;
    ctx.length = 8;
    Lane empty;
    CHECK_FALSE(euclidReplacesNotes(empty, ctx, f, EuclidTarget::Melodic, 7, false));
    Lane same;  // the figure already written: Apply changes nothing
    LaneEditor se(same, ctx);
    euclidApply(se, same, f, EuclidTarget::Melodic, 7, false);
    CHECK_FALSE(euclidReplacesNotes(same, ctx, f, EuclidTarget::Melodic, 7, false));
    CHECK(euclidReplacesNotes(same, ctx, f, EuclidTarget::Melodic, 9, false));  // another pitch on the hits

    Lane off;  // a note off the hits: lost without Merge, kept with it
    off.pat[2] = 3;
    CHECK(euclidReplacesNotes(off, ctx, f, EuclidTarget::Melodic, 7, false));
    CHECK_FALSE(euclidReplacesNotes(off, ctx, f, EuclidTarget::Melodic, 7, true));
    off.pat[4] = 3;  // on a hit: Merge writes over it
    CHECK(euclidReplacesNotes(off, ctx, f, EuclidTarget::Melodic, 7, true));

    Lane chord;  // a chord voice alone counts as a written note
    chord.vpat[0][1] = 4;
    CHECK(euclidReplacesNotes(chord, ctx, f, EuclidTarget::Melodic, 7, false));
    Lane beyond;  // past the track length, Apply does not touch the note
    beyond.pat[9] = 3;
    CHECK_FALSE(euclidReplacesNotes(beyond, ctx, f, EuclidTarget::Melodic, 7, false));
}

TEST_CASE("ymseq edit: picked notes dragged together, all or nothing (item 147)", "[ymseq][pianoroll]") {
    Lane l;
    EditContext c;
    c.length = 16;
    c.trackOctave = 3;
    LaneEditor e(l, c);
    e.writeNoteAt(1, 7);
    l.vol[1] = 9;
    e.writeNoteAt(5, 4);
    e.setNoteLen(5, 3);
    const int v = e.addVoiceAt(5, 9);
    REQUIRE(v >= 2);
    std::vector<SelKey> keys{{1, 1}, {5, 1}, {5, v}};
    CHECK(e.moveSel(keys, 2, 1));
    CHECK(l.pat[1] == -1);
    CHECK(l.pat[5] == -1);
    CHECK(e.relNote(3) == 8);
    CHECK(l.vol[3] == 9);  // VOL goes with the root
    CHECK(e.relNote(7) == 5);
    CHECK(e.noteLenAt(7) == 3);  // and its hold
    const int v2 = e.voiceAt(7, 10);
    CHECK(v2 >= 2);
    CHECK(keys == std::vector<SelKey>{{3, 1}, {7, 1}, {7, v2}});  // the keys follow the notes

    e.writeNoteAt(12, 0);  // a note outside the selection
    const Lane before = l;
    CHECK_FALSE(e.moveSel(keys, 5, 0));  // the root would land on it
    CHECK_FALSE(e.moveSel(keys, -4, 0));  // outside the pattern
    CHECK_FALSE(e.moveSel(keys, 0, 0));
    CHECK(l.pat == before.pat);
    CHECK(l.tie == before.tie);
    CHECK(keys == std::vector<SelKey>{{3, 1}, {7, 1}, {7, v2}});
}

TEST_CASE("ymseq: table of the named Euclidean rhythms (item 157)", "[ymseq][euclid]") {
    for (const EuclidRhythm& r : kEuclidRhythms) {
        CHECK(r.pulses >= 1);
        CHECK(r.pulses < r.steps);
        CHECK(r.steps <= kEuclidMaxSteps);
        CHECK(euclidName(r.pulses, r.steps) == r.name);
    }
}

// ---------------------------------------------------------------- piano roll editing (D-282)

TEST_CASE("ymseq edit: the held length of a note stops at the next note (D-282)", "[ymseq][pianoroll]") {
    Lane l;
    EditContext c;
    c.figure = 4;  // 1/8: two steps at 1/16
    LaneEditor e(l, c);
    e.writeNoteAt(0, 0);
    e.writeFigure(0);
    e.writeNoteAt(2, 2);
    e.writeFigure(2);
    REQUIRE(e.noteLenAt(0) == 2);
    REQUIRE(e.noteLenAt(2) == 2);
    CHECK(e.figLenAt(0) == 2);
    e.clearFigure(0);  // the hold of the first note only
    CHECK(e.noteLenAt(0) == 1);
    CHECK(e.noteLenAt(2) == 2);
}

TEST_CASE("ymseq edit: under Snap, splitting or resizing a note does not change its pitch (D-282)",
          "[ymseq][pianoroll]") {
    EditContext c;
    c.scale = 1;  // C major
    c.snap = true;
    Lane l;  // C#3 written before Snap was switched on, steps 1-4
    l.pat[0] = 1;
    for (int k = 0; k < 4; ++k) l.tie[size_t(k)] = 1;
    Lane l2 = l;
    LaneEditor e(l, c);
    REQUIRE(e.splitNote(0, 2));
    CHECK(e.relNote(0) == 1);
    CHECK(e.relNote(2) == 1);
    LaneEditor e2(l2, c);
    REQUIRE(e2.setNoteStart(0, 1) == 1);  // start dragged one step to the right
    CHECK(e2.relNote(1) == 1);
}

TEST_CASE("ymseq edit: under Snap, a promoted voice keeps its octave (D-282)", "[ymseq][pianoroll]") {
    EditContext c;
    c.mono = true;
    c.scale = 16;  // Pentatonic major on C: no B
    c.snap = true;
    Lane l;
    l.pat[0] = 0;  // C3 root, B3 greyed voice written before Snap
    l.vp(2, 0) = 11;
    l.vdur[0][0] = 1;
    LaneEditor e(l, c);
    REQUIRE(e.promoteVoice(0, VoiceHit{0, 2}));
    CHECK(e.relNote(0) >= 11);  // B3 kept, or its nearest degree C4; not C3
    CHECK(e.voiceAt(0, 0) == 2);  // the old root became the greyed voice
}

TEST_CASE("ymseq edit: under Snap, +1 semitone moves the notes up (D-282)", "[ymseq][pianoroll]") {
    EditContext c;
    c.scale = 1;  // C major
    c.snap = true;
    SECTION("whole track") {
        Lane l;
        LaneEditor e(l, c);
        e.writeNoteAt(0, 0);  // C3
        e.writeNoteAt(1, 2);  // D3
        REQUIRE(e.transpose(1));
        CHECK(e.relNote(0) > 0);
        CHECK(e.relNote(1) > 2);
    }
    SECTION("note selection") {
        Lane l;
        LaneEditor e(l, c);
        e.writeNoteAt(0, 0);
        e.writeNoteAt(1, 2);
        REQUIRE(e.transposeSel({{0, 1}, {1, 1}}, 1));
        CHECK(e.relNote(0) > 0);
        CHECK(e.relNote(1) > 2);
    }
    SECTION("selection box: the notes stay inside the moved box") {
        Lane l;
        LaneEditor e(l, c);
        e.writeNoteAt(0, 0);
        RectBounds r{0, 0, 0, 0};
        REQUIRE(e.rectMove(r, 0, 1));
        CHECK(e.relNote(0) > 0);
        CHECK(e.rectNotes(r).size() == 1);
    }
}

TEST_CASE("ymseq edit: a track with a note above C8 can still be transposed down (D-282)", "[ymseq][pianoroll]") {
    Lane l;
    EditContext c;
    LaneEditor e(l, c);
    e.writeNoteAt(0, 62);  // D8 (110), written in the grid (the view goes up to C9)
    REQUIRE(absoluteNote(l, 1, 0, 3) == 110);
    CHECK(e.transpose(-1));
    CHECK(absoluteNote(l, 1, 0, 3) == 109);
}

TEST_CASE("ymseq edit: a selection box is not moved below B0 (D-282)", "[ymseq][pianoroll]") {
    Lane l;
    EditContext c;
    LaneEditor e(l, c);
    e.writeNoteAt(0, -18);  // F#1 (30)
    e.writeNoteAt(1, -12);  // C2 (36)
    RectBounds r{0, 1, -18, -12};
    CHECK_FALSE(e.rectMove(r, 0, -12));  // F#0 would be below B0: all or nothing, like the transpositions
    CHECK(absoluteNote(l, 1, 0, 3) == 30);
    CHECK(absoluteNote(l, 1, 1, 3) == 36);
}

TEST_CASE("ymseq edit: shifting a note selection keeps the notes it lands on (D-282)", "[ymseq][pianoroll]") {
    SECTION("root over another root") {
        Lane l;
        EditContext c;
        LaneEditor e(l, c);
        e.writeNoteAt(3, 0);  // C3 selected
        e.writeNoteAt(4, 7);  // G3 not selected
        std::vector<SelKey> keys = {{3, 1}};
        REQUIRE(e.shiftSel(keys, 1));
        CHECK(e.voiceAt(4, 7) != 0);  // the G3 is still there
        int c3 = 0;
        for (int s = 0; s < 16; ++s) c3 += e.voiceAt(s, 0) != 0;
        CHECK(c3 == 1);  // and the C3 is not lost
    }
    SECTION("chord voice onto a full step") {
        Lane l;
        EditContext c;
        LaneEditor e(l, c);
        e.writeNoteAt(3, 0);
        REQUIRE(e.addVoiceAt(3, 9) == 2);  // A3 voice selected
        e.writeNoteAt(4, 0);
        for (int row : {2, 4, 5, 7, 11}) REQUIRE(e.addVoiceAt(4, row) != 0);  // step 5 full
        std::vector<SelKey> keys = {{3, 2}};
        REQUIRE(e.shiftSel(keys, 1));
        int a3 = 0;
        for (int s = 0; s < 16; ++s) a3 += e.voiceAt(s, 9) != 0;
        CHECK(a3 == 1);  // the A3 is not lost
    }
}

TEST_CASE("ymseq edit: a chord voice held more than 64 steps is found over all its steps (D-282)",
          "[ymseq][pianoroll]") {
    Lane l;
    EditContext c;
    c.length = 256;
    LaneEditor e(l, c);
    e.writeNoteAt(0, 0);
    REQUIRE(e.addVoiceAt(0, 4) == 2);
    REQUIRE(e.setVoiceLen(0, 2, 100));
    CHECK(e.voiceStartCovering(80, 4).has_value());
    CHECK(e.stepBusy(80));
}

TEST_CASE("ymseq edit: a moved note takes its step rows along and gives back what it covered (D-282)",
          "[ymseq][pianoroll]") {
    SECTION("melodic note: VOL, SLD, CRV, SRC, GEN, INS") {
        Lane l;
        EditContext c;
        LaneEditor e(l, c);
        e.writeNoteAt(2, 5);  // F3 with its accent, slide and curve
        l.vol[2] = 6;
        l.slide[2] = 1;
        l.slc[2] = 3;
        l.mix[2] = 2;
        l.fx[2] = 1;
        l.ins[2] = 4;
        l.vol[3] = 9;  // VOL already set on the empty step 4
        LaneEditor::Moving m{2, 5, 1, false, false, 0, 0};
        e.moveNoteTo(m, 3, 5);
        e.moveNoteTo(m, 4, 5);
        REQUIRE(l.pat[4] == 5);
        CHECK(l.vol[4] == 6);
        CHECK(l.slide[4] == 1);
        CHECK(l.slc[4] == 3);
        CHECK(l.mix[4] == 2);
        CHECK(l.fx[4] == 1);
        CHECK(l.ins[4] == 4);
        CHECK(l.vol[3] == 9);  // given back once the note has passed
        CHECK(l.vol[2] == 15);
        CHECK(l.slide[2] == 0);
    }
    SECTION("drum hit: its OCT and TON") {
        Lane l;
        EditContext c;
        c.drum = true;
        LaneEditor e(l, c);
        l.pat[2] = 3;  // fourth instrument, two octaves up, a semitone down
        l.oct[2] = 2;
        l.ton[2] = -1;
        LaneEditor::Moving m{2, 3, 1, false, true, 0, 0};
        e.moveNoteTo(m, 5, 3);
        REQUIRE(l.pat[5] == 3);
        CHECK(l.oct[5] == 2);
        CHECK(l.ton[5] == -1);
        CHECK(l.oct[2] == 0);
        CHECK(l.ton[2] == 0);
    }
}

TEST_CASE("ymseq edit: a pasted box does not erase the notes its held notes cover (D-282)", "[ymseq][pianoroll]") {
    Lane l;
    EditContext c;
    LaneEditor e(l, c);
    e.writeNoteAt(0, 0);  // C3 held over steps 1-4, copied
    for (int k = 0; k < 4; ++k) l.tie[size_t(k)] = 1;
    const RectBounds box{0, 3, 0, 0};
    std::vector<RectNote> clip = e.rectNotes(box);
    REQUIRE(clip.size() == 1);
    e.writeNoteAt(10, 7);  // G3 at step 11
    e.rectPaste(RectBounds{8, 11, 0, 0}, clip);  // the C3 pasted at step 9
    CHECK(e.relNote(8) == 0);
    CHECK(l.pat[10] == 7);
}

TEST_CASE("ymseq: ARP flags become Jxx switches and back, Txx of the tracks the BPM row of the pattern (twelfth DAW "
          "test, items 144 and 150)",
          "[ymseq][commands]") {
    using stym::ymengine::SeqCmd;
    using stym::ymengine::seqCmd;
    const uint16_t on = seqCmd(SeqCmd::Arpeggiator, 1), off = seqCmd(SeqCmd::Arpeggiator, 0);
    Lane l;
    CHECK_FALSE(migrateArpFlags(l));
    l.afl[2] = l.afl[3] = l.afl[6] = 1;
    l.afl[255] = 1;
    REQUIRE(migrateArpFlags(l));
    CHECK(std::all_of(l.afl.begin(), l.afl.end(), [](uint8_t f) { return f == 0; }));
    CHECK(l.cmdAt(SeqCmd::Arpeggiator, 0) == off);  // each loop starts without the arpeggio, as before
    CHECK(l.cmdAt(SeqCmd::Arpeggiator, 1) == 0);
    CHECK(l.cmdAt(SeqCmd::Arpeggiator, 2) == on);
    CHECK(l.cmdAt(SeqCmd::Arpeggiator, 3) == 0);
    CHECK(l.cmdAt(SeqCmd::Arpeggiator, 4) == off);
    CHECK(l.cmdAt(SeqCmd::Arpeggiator, 6) == on);
    CHECK(l.cmdAt(SeqCmd::Arpeggiator, 7) == off);
    CHECK(l.cmdAt(SeqCmd::Arpeggiator, 255) == on);
    // Back to flags for the prototype: exactly the flags read.
    const Lane w = withArpFlags(l);
    CHECK(w.afl[2] == 1);
    CHECK(w.afl[3] == 1);
    CHECK(w.afl[4] == 0);
    CHECK(w.afl[255] == 1);
    CHECK(w.cmdAt(SeqCmd::Arpeggiator, 2) == 0);
    // A switch the flags cannot say (J01 twice in a row): the track as it is.
    l.setCmd(3, on);
    const Lane same = withArpFlags(l);
    CHECK(same == l);
    // A track that starts with the arpeggio: J01 at step 0, no J00 there.
    Lane f;
    f.afl[0] = 1;
    REQUIRE(migrateArpFlags(f));
    CHECK(f.cmdAt(SeqCmd::Arpeggiator, 0) == on);
    CHECK(f.cmdAt(SeqCmd::Arpeggiator, 1) == off);

    Pattern p;
    p.edit(0).setCmd(4, seqCmd(SeqCmd::Tempo, 0x8c));
    p.edit(0).setCmd(4, seqCmd(SeqCmd::Vibrato, 1));
    p.edit(1).setCmd(4, seqCmd(SeqCmd::Tempo, 0x60));  // the highest track wins, as in the engine
    p.edit(2).setCmd(9, seqCmd(SeqCmd::Tempo, 0x10));  // below 32: no effect, dropped
    REQUIRE(p.migrateTempo());
    CHECK(p.bpm[4] == 0x60);
    CHECK(p.bpm[9] == 0);
    CHECK(p.lane(0).cmdAt(SeqCmd::Tempo, 4) == 0);
    CHECK(p.lane(0).cmdAt(SeqCmd::Vibrato, 4) == seqCmd(SeqCmd::Vibrato, 1));
    CHECK(p.lanes[1] == nullptr);  // emptied
    CHECK(p.lanes[2] == nullptr);
    CHECK_FALSE(p.migrateTempo());
}
