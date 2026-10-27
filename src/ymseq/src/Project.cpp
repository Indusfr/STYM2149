// STYM2149 — ymseq: pattern bank, Song and sequencer settings of the tracks
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymseq/Project.h"

#include <algorithm>
#include <memory>

#include "ymseq/Edit.h"

namespace stym::ymseq {

void Project::setSteps(int n) noexcept { cur().steps = std::clamp(n, 1, kSteps); }

void Project::fitResolution(int num, int den) {
    if (num <= 0 || den <= 0 || num == den) return;
    for (int i = 0; i < kPatterns; ++i) {
        if (!used.test(size_t(i))) continue;
        Pattern& p = patterns[size_t(i)];
        for (auto& l : p.lanes)
            if (l) l = std::make_shared<const Lane>(fitLane(*l, num, den));
        std::array<uint8_t, kSteps> bpm{};
        for (int s = 0; s < kSteps; ++s)
            if (const int ns = fitStep(s, num, den); p.bpm[size_t(s)] && ns < kSteps && !bpm[size_t(ns)])
                bpm[size_t(ns)] = p.bpm[size_t(s)];
        p.bpm = bpm;
        p.steps = std::clamp(fitStep(p.steps, num, den), 1, kSteps);
        p.compact();
    }
    for (TrackSeq& t : tracks)
        if (t.length > 0) t.length = std::clamp(fitStep(t.length, num, den), 1, 64);
    for (SongEntry& e : song.entries)
        if (e.kind == SongKind::Rest || e.kind == SongKind::NoteOff)
            e.arg = std::clamp(fitStep(e.arg, num, den), 1, kSteps);
}

bool Project::hasNotes() const noexcept {
    for (int i = 0; i < kPatterns; ++i)
        if (used.test(size_t(i)))
            for (const auto& l : patterns[size_t(i)].lanes)
                if (l && l->hasNotes()) return true;
    return false;
}

int Project::trackLength(int t) const noexcept {
    const int plen = tracks[size_t(std::clamp(t, 0, kTracks - 1))].length;
    return plen > 0 ? plen : steps();
}

void Project::ensure(int idx) {
    if (idx < 0 || idx >= kPatterns || used.test(size_t(idx))) return;
    Pattern p;
    p.steps = steps();
    patterns[size_t(idx)] = p;
    used.set(size_t(idx));
}

bool Project::select(int idx, bool playing) {
    if (idx < 0 || idx >= kPatterns) return false;
    bankLast[size_t(idx / kBankSize)] = idx;  // back to this pattern in its bank
    if (idx == current && pending < 0) return false;
    if (idx == pending) return false;
    ensure(idx);
    if (playing)
        pending = idx;  // switch at the end of the loop (303; the prototype's "303 style" pattern banks)
    else {
        current = idx;
        pending = -1;
    }
    return true;
}

void Project::applied(int idx) {
    if (idx < 0 || idx >= kPatterns) return;
    ensure(idx);
    current = idx;
    pending = -1;
}

Pattern Project::copyPattern(int idx) {
    ensure(idx);
    return patterns[size_t(std::clamp(idx, 0, kPatterns - 1))];
}

void Project::cutPattern(int idx) {
    if (idx < 0 || idx >= kPatterns) return;
    Pattern p;
    p.steps = steps();  // emptyPattern
    patterns[size_t(idx)] = p;
    used.set(size_t(idx));
}

void Project::pastePattern(int idx, const Pattern& p) {
    if (idx < 0 || idx >= kPatterns) return;
    patterns[size_t(idx)] = p;
    used.set(size_t(idx));
}

void Project::transposePattern(int idx, int semis, const std::bitset<kTracks>& drumTracks) {
    if (idx < 0 || idx >= kPatterns) return;
    ensure(idx);
    Pattern& p = patterns[size_t(idx)];
    for (int t = 0; t < kTracks; ++t) {
        if (drumTracks.test(size_t(t)) || !p.lanes[size_t(t)]) continue;  // percussion tracks keep their slots
        const int defOct = tracks[size_t(t)].octave;
        Lane& l = p.edit(t);
        for (int v = 1; v <= kPolyMax; ++v)
            for (int st = 0; st < kSteps; ++st) {
                if (l.vp(v, st) < 0) continue;
                // Absolute note (60 = C4) moved by semis, then split again into an octave clipped to 0-9 (division
                // rounded down for a negative note) and a note within it, clipped to 0-12.
                const int abs = (octaveOf(l.vo(v, st), defOct) + 1) * 12 + l.vp(v, st) + semis;
                const int no = std::clamp((abs >= 0 ? abs / 12 : -((-abs + 11) / 12)) - 1, 0, 9);
                l.vp(v, st) = int16_t(std::clamp(abs - (no + 1) * 12, 0, 12));
                l.vo(v, st) = int8_t(octaveCode(no, defOct));
            }
    }
}

}  // namespace stym::ymseq
