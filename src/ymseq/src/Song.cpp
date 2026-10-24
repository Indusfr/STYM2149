// STYM2149 — ymseq: arrangement (Song)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymseq/Song.h"

#include <algorithm>

#include "ymseq/Pattern.h"

namespace stym::ymseq {

namespace {
using It = std::vector<SongEntry>::difference_type;
It d(int i) noexcept { return It(i); }
}  // namespace

std::string songLabel(const SongEntry& e) {
    std::string out = e.restart ? "|:" : "";
    switch (e.kind) {
        case SongKind::Pattern: {
            const int p = std::clamp(e.pat, 0, kPatterns - 1);
            out += std::string(1, bankLetter(p)) + std::to_string(p % 8 + 1);
            break;
        }
        case SongKind::Rest: out += "Rest" + std::to_string(e.arg); break;
        case SongKind::NoteOff: out += "Off" + std::to_string(e.arg); break;
        case SongKind::Loop: out += "Back" + std::to_string(e.arg); break;
    }
    return out + "x" + std::to_string(e.rep);
}

int Song::restartIndex() const noexcept {
    for (int i = 0; i < size(); ++i)
        if (entries[size_t(i)].restart) return i;
    return 0;
}

SongSel Song::normalize(const SongSel& sel) const {
    SongSel s;
    for (int x : sel)
        if (x >= 0 && x < size()) s.push_back(x);
    std::sort(s.begin(), s.end());
    s.erase(std::unique(s.begin(), s.end()), s.end());
    return s;
}

void Song::add(int pat, bool separate) {
    if (!entries.empty() && !entries.back().special() && entries.back().pat == pat && !separate)
        ++entries.back().rep;  // same pattern: increment
    else
        entries.push_back({pat, 1, 0});  // Shift+Add: separate entry
}

void Song::append(int pat) { entries.push_back({pat, 1, 0}); }

int Song::insertAfter(int cursor, int pat) {
    const int at = entries.empty() ? 0 : std::clamp(cursor + 1, 0, size());
    entries.insert(entries.begin() + d(at), SongEntry{pat, 1, 0});
    return at;
}

int Song::insertEntryAfter(int cursor, const SongEntry& e) {
    const int at = entries.empty() ? 0 : std::clamp(cursor + 1, 0, size());
    SongEntry n = e;
    n.restart = false;
    entries.insert(entries.begin() + d(at), n);
    return at;
}

int Song::remove(const SongSel& sel, int cursor) {
    if (entries.empty()) return 0;
    SongSel s = normalize(sel);
    if (s.empty() && cursor >= 0 && cursor < size()) s.push_back(cursor);
    erase(s);
    return std::max(0, std::min(cursor, size() - 1));
}

int Song::duplicateAt(int cursor) {
    if (cursor < 0 || cursor >= size()) return cursor;
    SongEntry e = entries[size_t(cursor)];
    e.restart = false;
    entries.insert(entries.begin() + d(cursor + 1), e);
    return cursor + 1;
}

SongSel Song::duplicate(const SongSel& sel) {
    const SongSel s = normalize(sel);
    if (s.empty()) return s;
    const std::vector<SongEntry> items = copy(s);
    const int at = s.back() + 1;  // the block after its end
    entries.insert(entries.begin() + d(at), items.begin(), items.end());
    SongSel out;
    for (int n = 0; n < int(items.size()); ++n) out.push_back(at + n);
    return out;
}

void Song::setRepeat(const SongSel& sel, int rep) {
    for (int x : normalize(sel)) entries[size_t(x)].rep = std::max(1, rep);
}

void Song::incRepeat(int i) {
    if (i >= 0 && i < size()) entries[size_t(i)].rep = std::min(kSongRepeatMax, entries[size_t(i)].rep + 1);
}

void Song::decRepeat(int i) {
    if (i >= 0 && i < size()) entries[size_t(i)].rep = std::max(1, entries[size_t(i)].rep - 1);
}

void Song::setTempo(const SongSel& sel, std::optional<int> bpm) {
    for (int x : normalize(sel))
        if (entries[size_t(x)].kind != SongKind::Loop)
            entries[size_t(x)].bpm = bpm ? std::clamp(*bpm, kSongBpmMin, kSongBpmMax) : 0;
}

void Song::setLength(const SongSel& sel, int steps) {
    for (int x : normalize(sel)) {
        SongEntry& e = entries[size_t(x)];
        if (e.kind == SongKind::Rest || e.kind == SongKind::NoteOff) e.arg = std::clamp(steps, 1, kSteps);
    }
}

void Song::setLoopBack(const SongSel& sel, int back) {
    for (int x : normalize(sel)) {
        SongEntry& e = entries[size_t(x)];
        if (e.kind == SongKind::Loop) e.arg = std::clamp(back, 1, kSongLoopMax);
    }
}

void Song::toggleRestart(int i) {
    if (i < 0 || i >= size()) return;
    // Only the mark that counts (the first one) is removed by a toggle; a later entry that also carries one keeps it
    // as the only mark. Either way, at most one mark is left.
    const bool was = entries[size_t(i)].restart && restartIndex() == i;
    for (SongEntry& e : entries) e.restart = false;
    entries[size_t(i)].restart = !was;
}

std::vector<SongEntry> Song::take(const SongSel& sel) const {
    std::vector<SongEntry> out;
    for (int x : normalize(sel)) out.push_back(entries[size_t(x)]);
    return out;
}

std::vector<SongEntry> Song::copy(const SongSel& sel) const {
    std::vector<SongEntry> out = take(sel);
    for (SongEntry& e : out) e.restart = false;
    return out;
}

void Song::erase(const SongSel& sel) {
    const SongSel s = normalize(sel);
    for (auto it = s.rbegin(); it != s.rend(); ++it) entries.erase(entries.begin() + d(*it));
}

SongSel Song::pasteAfter(int after, const std::vector<SongEntry>& clip) {
    SongSel out;
    if (clip.empty()) return out;
    const int at = std::clamp(after + 1, 0, size());
    entries.insert(entries.begin() + d(at), clip.begin(), clip.end());
    for (int n = 0; n < int(clip.size()); ++n) out.push_back(at + n);
    return out;
}

bool Song::moveLeft(SongSel& sel) {
    const SongSel s = normalize(sel);
    if (s.empty() || s.front() <= 0) return false;
    // Each selected entry, from the first, swaps with the entry before it: a run of selected entries moves as a block
    // and the entry it passes lands after it. moveRight does the same from the last.
    for (int x : s) {  // block to the left
        const SongEntry e = entries[size_t(x)];
        entries.erase(entries.begin() + d(x));
        entries.insert(entries.begin() + d(x - 1), e);
    }
    sel.clear();
    for (int x : s) sel.push_back(x - 1);
    return true;
}

bool Song::moveRight(SongSel& sel) {
    const SongSel s = normalize(sel);
    if (s.empty() || s.back() >= size() - 1) return false;
    for (auto it = s.rbegin(); it != s.rend(); ++it) {
        const SongEntry e = entries[size_t(*it)];
        entries.erase(entries.begin() + d(*it));
        entries.insert(entries.begin() + d(*it + 1), e);
    }
    sel.clear();
    for (int x : s) sel.push_back(x + 1);
    return true;
}

bool Song::dragMove(SongSel& sel, int from, int drop) {
    if (from < 0 || from >= size() || drop < 0 || drop >= size() || drop == from) return false;
    const SongSel cur = normalize(sel);
    const bool multi = std::find(cur.begin(), cur.end(), from) != cur.end() && cur.size() > 1;
    const SongSel s = multi ? cur : SongSel{from};
    if (std::find(s.begin(), s.end(), drop) != s.end()) return false;
    const std::vector<SongEntry> items = take(s);
    // Index of the drop target once the moved entries are erased (minus those in front of it); the entries land after
    // the target when dragged from before it, otherwise in front of it.
    const int before = int(std::count_if(s.begin(), s.end(), [drop](int x) { return x < drop; }));
    erase(s);
    const int at = std::clamp(drop - before + (from < drop ? 1 : 0), 0, size());
    entries.insert(entries.begin() + d(at), items.begin(), items.end());
    sel.clear();
    for (int n = 0; n < int(items.size()); ++n) sel.push_back(at + n);
    return true;
}

}  // namespace stym::ymseq
