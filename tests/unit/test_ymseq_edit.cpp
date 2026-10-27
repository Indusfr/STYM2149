// STYM2149 — sequencer track edits against the prototype: notes, ties, chord voices, moves,
// rectangular and note-by-note selections, rotation, transposition, blocks, step recording, Bjorklund.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Data: tests/data/proto/edit.txt, produced by tools/proto_golden/edit.mjs from YM2149_Prototype.html
// (intended deviations applied in the generator).
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ymseq/Edit.h"
#include "ymseq/Euclid.h"

using namespace stym::ymseq;

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, sep))
        if (!tok.empty()) out.push_back(tok);
    return out;
}

// Value of key k of LANEKEYS (16 rows, then pat2-pat6, oct2-oct6, dur2-dur6). The later rows are not in it:
// the prototype does not have them.
int keyValue(const Lane& l, int k, int s) {
    if (k < kProtoRows) return l.get(Row(k), s);
    k -= kProtoRows;
    const int v = k % kExtraVoices;
    if (k < kExtraVoices) return l.vpat[size_t(v)][size_t(s)];
    if (k < 2 * kExtraVoices) return l.voct[size_t(v)][size_t(s)];
    return l.vdur[size_t(v)][size_t(s)];
}
constexpr int kKeys = kProtoRows + 3 * kExtraVoices;
int keyDefault(int k) { return (k == 0 || (k >= kProtoRows && k < kProtoRows + kExtraVoices)) ? -1 : k == 4 ? 15 : 0; }

std::string hashOf(const Lane& l) {
    uint32_t h = 0x811c9dc5u;
    for (int k = 0; k < kKeys; ++k)
        for (int s = 0; s < kSteps; ++s) {
            const auto v = uint32_t(keyValue(l, k, s));
            for (int b = 0; b < 4; ++b) {
                h ^= (v >> (8 * b)) & 0xffu;
                h *= 0x01000193u;
            }
        }
    std::ostringstream o;
    o << std::hex << h;
    return o.str();
}

std::string dumpOf(const Lane& l) {
    std::string out;
    for (int k = 0; k < kKeys; ++k)
        for (int s = 0; s < kSteps; ++s)
            if (const int v = keyValue(l, k, s); v != keyDefault(k)) {
                if (!out.empty()) out += ' ';
                out += std::to_string(k) + ':' + std::to_string(s) + ':' + std::to_string(v);
            }
    return out;
}

std::vector<SelKey> parseKeys(const std::string& s) {
    std::vector<SelKey> keys;
    for (const auto& k : split(s, ',')) {
        const auto p = split(k, ':');
        keys.push_back({std::stoi(p[0]), std::stoi(p[1])});
    }
    return keys;
}

std::string keysText(const std::vector<SelKey>& keys) {
    if (keys.empty()) return "-";
    std::string out;
    for (const auto& k : keys) out += (out.empty() ? "" : ",") + std::to_string(k.s) + ':' + std::to_string(k.v);
    return out;
}

RectBounds rect(int a, int b, int r1, int r2) {
    return {std::min(a, b), std::max(a, b), std::min(r1, r2), std::max(r1, r2)};
}

std::optional<Row> blockRow(int idx) { return idx < 0 ? std::nullopt : std::optional<Row>(Row(idx)); }

// Replays an operation; returns the resulting value in the generator's format.
std::string replay(LaneEditor& e, Lane& l, const std::vector<std::string>& f) {
    auto I = [&](size_t i) { return std::stoi(f[i]); };
    const std::string& op = f[0];
    if (op == "click") {
        e.writeNoteAt(I(1), I(2));
        e.writeChord(I(1), I(2));
        e.writeFigure(I(1));
        return "0";
    }
    if (op == "note") { e.writeNoteAt(I(1), I(2)); return "0"; }
    if (op == "voice") return std::to_string(e.addVoiceAt(I(1), I(2)));
    if (op == "delv") { e.delVoiceAt(I(1), I(2)); return "0"; }
    if (op == "fig") { e.writeFigure(I(1)); return "0"; }
    if (op == "clrfig") { e.clearFigure(I(1)); return "0"; }
    if (op == "stretch") { e.stretchNote(I(1), I(2)); return "0"; }
    if (op == "len") { e.setNoteLen(I(1), I(2)); return "0"; }
    if (op == "start") return std::to_string(e.setNoteStart(I(1), I(2)));
    if (op == "split") return std::to_string(int(e.splitNote(I(1), I(2))));
    if (op == "join") return std::to_string(int(e.joinNote(I(1))));
    if (op == "erase") { e.eraseAt(I(1)); return "0"; }
    if (op == "erasenote") return std::to_string(int(e.eraseNoteAt(I(1), I(2))));
    if (op == "busyat") return std::to_string(int(e.stepBusyAt(I(1), I(2))));
    if (op == "busy") return std::to_string(int(e.stepBusy(I(1))));
    if (op == "demote") return std::to_string(int(e.demoteMain(I(1))));
    if (op == "promote") {
        const std::string before = hashOf(l);
        e.promoteVoice(I(1), VoiceHit{I(3), I(4)});
        return hashOf(l) != before ? "1" : "0";
    }
    if (op == "cp") {
        const NoteClip clip = e.copyNoteAt(I(1));
        if (I(2)) e.cutNoteAt(I(1));
        return std::to_string(int(e.pasteNoteAt(I(3), f[4] == "x" ? clip.row : I(4), clip)));
    }
    if (op == "move") {
        LaneEditor::Moving m;
        m.from = I(1);
        m.row = I(2);
        m.len = I(3);
        m.drum = I(4) != 0;
        m.voice = I(5);
        for (size_t i = 6; i + 1 < f.size(); i += 2) e.moveNoteTo(m, I(i), I(i + 1));
        return std::to_string(m.from) + ',' + std::to_string(m.row) + ',' + std::to_string(int(m.moved)) + ',' +
               std::to_string(m.voice);
    }
    if (op == "vsize") {
        const int s0 = I(2), v = I(3), row = I(4), c2 = I(5);
        if (f[1] == "L") {
            const auto hit = e.setVoiceStart(s0, v, row, c2);
            return hit ? "1," + std::to_string(hit->start) + ',' + std::to_string(hit->voice)
                       : "0," + std::to_string(s0) + ',' + std::to_string(v);
        }
        return std::to_string(int(e.setVoiceLen(s0, v, c2 - s0 + 1))) + ',' + std::to_string(s0) + ',' +
               std::to_string(v);
    }
    if (op == "rmove") {
        RectBounds r = rect(I(1), I(2), I(3), I(4));
        const bool moved = e.rectMove(r, I(5), I(6));
        return std::to_string(int(moved)) + ',' + std::to_string(r.a) + ',' + std::to_string(r.b) + ',' +
               std::to_string(r.r1) + ',' + std::to_string(r.r2);
    }
    if (op == "rclear") { e.rectClear(rect(I(1), I(2), I(3), I(4))); return "0"; }
    if (op == "rpaste") {
        const RectBounds r = rect(I(1), I(2), I(3), I(4));
        std::vector<RectNote> clip = e.rectNotes(r);
        for (auto& n : clip) n.s -= r.a;
        RectBounds at = r;
        at.a = I(5);
        e.rectPaste(at, clip);
        return std::to_string(clip.size());
    }
    if (op == "tsel") {
        if (!e.transposeSel(parseKeys(f[2]), I(1))) e.transpose(I(1));
        return "0";
    }
    if (op == "ssel") {
        std::vector<SelKey> keys = parseKeys(f[2]);
        if (!e.shiftSel(keys, I(1))) e.rotate(I(1));  // rotateLane: the selection takes priority over the track
        return keysText(keys);
    }
    if (op == "rot") { e.rotate(I(1)); return "0"; }
    if (op == "tr") { e.transpose(I(1)); return "0"; }
    if (op == "clear") { e.clear(); return "0"; }
    if (op == "bpaste") {
        const BlockClip clip = e.blockCopy(std::min(I(1), I(2)), std::max(I(1), I(2)), blockRow(I(3)));
        e.blockPaste(I(4), clip);
        return "0";
    }
    if (op == "bclear") { e.blockClear(std::min(I(1), I(2)), std::max(I(1), I(2)), blockRow(I(3))); return "0"; }
    if (op == "rec") { e.recordAt(I(1), I(2), I(3)); return "0"; }
    if (op == "query") {
        const int s = I(1);
        return std::to_string(int(e.slideRunsAt(s))) + ',' + std::to_string(e.noteStart(s)) + ',' +
               std::to_string(e.figLenAt(s)) + ',' + std::to_string(e.noteLenAt(s)) + ',' +
               std::to_string(e.nextNoteAfter(s)) + ',' + std::to_string(e.voiceCount(s)) + ',' +
               std::to_string(e.freeVoice(s));
    }
    if (op == "set") { l.set(Row(I(1)), I(2), I(3)); return "0"; }
    FAIL("unknown operation " << op);
    return "";
}

}  // namespace

TEST_CASE("Prototype: sequencer track edits", "[proto][ymseq]") {
    std::ifstream in(std::string(STYM_TEST_DATA) + "/proto/edit.txt");
    REQUIRE(in.good());
    std::string line, caseHead;
    Lane lane;
    EditContext ctx;
    int cases = 0, ops = 0;
    while (std::getline(in, line)) {
        if (line.rfind("case ", 0) == 0) {
            const auto f = split(line, ' ');
            REQUIRE(f.size() == 13);
            lane = Lane{};
            ctx = EditContext{};
            ctx.trackOctave = std::stoi(f[2]);
            ctx.drum = f[3] == "1";
            ctx.mono = f[4] == "1";
            ctx.threeVoice = f[5] == "1";
            const int fig = std::stoi(f[6]);
            ctx.figure = fig >= 7 ? fig + 1 : fig;  // the prototype has no 1/64, which comes after 1/32
            ctx.resolution = std::stoi(f[7]);
            ctx.chord = std::stoi(f[8]);
            ctx.scale = std::stoi(f[9]);
            ctx.scaleRoot = std::stoi(f[10]);
            ctx.snap = f[11] == "1";
            ctx.length = std::stoi(f[12]);
            caseHead = line;
            ++cases;
        } else if (line.rfind("op ", 0) == 0) {
            const auto eq = line.find(" = ");
            REQUIRE(eq != std::string::npos);
            const auto args = split(line.substr(3, eq - 3), ' ');
            const auto want = split(line.substr(eq + 3), ' ');
            REQUIRE(want.size() == 2);
            LaneEditor e(lane, ctx);
            const std::string got = replay(e, lane, args);
            INFO(caseHead << " | " << line);
            CHECK(got == want[0]);
            REQUIRE(hashOf(lane) == want[1]);
            ++ops;
        } else if (line.rfind("lane", 0) == 0) {
            INFO(caseHead);
            REQUIRE(dumpOf(lane) == (line.size() > 5 ? line.substr(5) : std::string()));
        }
    }
    CHECK(cases == 160);
    CHECK(ops > 6000);
}

TEST_CASE("Prototype: Bjorklund patterns", "[proto][ymseq]") {
    std::ifstream in(std::string(STYM_TEST_DATA) + "/proto/edit.txt");
    REQUIRE(in.good());
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        if (line.rfind("bjork ", 0) != 0) continue;
        const auto f = split(line, ' ');
        const auto got = bjorklund(std::stoi(f[1]), std::stoi(f[2]));
        std::string s;
        for (int x : got) s += char('0' + x);
        INFO(line);
        REQUIRE(s == (f.size() > 3 ? f[3] : std::string()));
        ++n;
    }
    CHECK(n == 560);
}
