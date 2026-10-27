// STYM2149 — ymseq: editing of a pattern track
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Port of the prototype's editing functions, in the same order of operations. The intended deviations are marked
// with "Deviation". Reads outside the array of 256 steps (undefined in JavaScript) are
// bounded: a cell beyond it is neither a note nor a tie.
#include "ymseq/Edit.h"

#include <cmath>
#include <utility>

#include "ymengine/Pitch.h"
#include "ymseq/Tables.h"

namespace stym::ymseq {

namespace {
size_t u(int i) noexcept { return size_t(i); }
bool inLane(int s) noexcept { return s >= 0 && s < kSteps; }
// Division by 12 rounded down, also for a negative note (the prototype's Math.floor(A / 12)).
int floorDiv12(int a) noexcept { return a >= 0 ? a / 12 : -((-a + 11) / 12); }
int mod12(int v) noexcept { return ((v % 12) + 12) % 12; }
}  // namespace

// ---------------------------------------------------------------- notes and rows

int LaneEditor::relNote(int s) const noexcept {
    if (!inLane(s)) return 0;
    return (octaveOf(l_.oct[u(s)], c_.trackOctave) - c_.trackOctave) * 12 + l_.pat[u(s)];
}

int LaneEditor::snapRow(int row) const noexcept {
    // Deviation: no Snap on a percussion track; the prototype also brought the instrument row into the scale when
    // Snap stayed armed from a melodic track (button greyed out, state kept).
    if (!c_.snap || c_.scale <= 0 || c_.drum) return row;  // constraining mode: bring back to the degree
    const int abs = (c_.trackOctave + 1) * 12 + row;
    return row + snapToScale(abs, c_.scale, c_.scaleRoot, true) - abs;
}

int LaneEditor::transposedRow(int row, int semis) const noexcept {
    if (!c_.snap || c_.scale <= 0 || c_.drum) return row + semis;
    const int abs = (c_.trackOctave + 1) * 12 + row;
    return row + snapTransposed(abs, semis, c_.scale, c_.scaleRoot, true) - abs;
}

void LaneEditor::writeNoteAt(int s, int row) noexcept {
    if (!inLane(s)) return;
    const int T = c_.trackOctave;
    row = snapRow(row);
    // Row → absolute note A (60 = C4) → stored octave (clipped to 0-9) and note within it (clipped to 0-12): a note
    // below octave 0 becomes C0, a note above octave 9 becomes note 12 of octave 9 (C10).
    const int A = (T + 1) * 12 + row;
    const int no = std::clamp(floorDiv12(A) - 1, 0, 9);
    l_.pat[u(s)] = int16_t(std::clamp(A - (no + 1) * 12, 0, 12));
    l_.oct[u(s)] = int8_t(octaveCode(no, T));
}

void LaneEditor::writeVoiceAt(int s, int row, int v) noexcept {
    if (!inLane(s) || v < 1 || v > kPolyMax) return;
    if (c_.drum) {  // percussion: the row is the instrument
        l_.vp(v, s) = int16_t(row);
        l_.vo(v, s) = 0;
        return;
    }
    const int T = c_.trackOctave;
    const int A2 = (T + 1) * 12 + row;
    const int no = std::clamp(floorDiv12(A2) - 1, 0, 9);
    l_.vp(v, s) = int16_t(std::clamp(A2 - (no + 1) * 12, 0, 12));
    l_.vo(v, s) = int8_t(octaveCode(no, T));
}

int noteStartOf(const Lane& l, int c) noexcept {
    if (c < 0 || c >= kSteps) return -1;
    if (l.pat[u(c)] >= 0) return c;
    if (!l.tie[u(c)]) return -1;  // this step is not held
    // Back over the steps without a note whose previous step is tied: the note found there holds c.
    int k = c;
    while (k > 0 && l.pat[u(k)] < 0 && l.tie[u(k - 1)]) --k;
    return l.pat[u(k)] >= 0 ? k : -1;
}

int LaneEditor::noteStart(int c) const noexcept { return inLane(c) ? noteStartOf(l_, c) : -1; }

int LaneEditor::figLenAt(int c) const noexcept {
    if (!inLane(c) || !l_.tie[u(c)]) return 1;
    const int N = length();
    int n = 1;
    // Deviation: the hold stops at the next note; the prototype also counted the hold of a note that follows
    // at once, and a click then shortened that note instead of taking this one away.
    while (c + n < N && l_.pat[u(c + n)] < 0 && l_.tie[u(c + n)]) ++n;
    return n;
}

void LaneEditor::clearFigure(int c) noexcept {
    if (!inLane(c)) return;
    const int N = length();
    for (int k = c; k < N && l_.tie[u(k)]; ++k) {
        // Deviation: another note takes over and keeps its hold (the prototype removed its first tie).
        if (k > c && l_.pat[u(k)] >= 0) break;
        l_.tie[u(k)] = 0;
    }
}

void LaneEditor::stretchNote(int c, int to) noexcept {
    if (!inLane(c)) return;
    const int N = length();
    int end = std::min(to, N - 1);
    for (int k = c + 1; k <= end; ++k)  // do not eat a following note
        if (l_.pat[u(k)] >= 0) {
            end = k - 1;
            break;
        }
    for (int k = c; k <= end; ++k) {
        if (k > c) {
            l_.pat[u(k)] = -1;
            l_.oct[u(k)] = 0;
        }
        l_.tie[u(k)] = end > c ? 1 : 0;
    }
    for (int k = end + 1; k < N && l_.tie[u(k)]; ++k) {  // leftovers beyond
        if (l_.pat[u(k)] >= 0) break;
        l_.tie[u(k)] = 0;
    }
}

void LaneEditor::writeFigure(int c) noexcept {
    if (c_.drum || !inLane(c)) return;  // percussion: no duration
    const int N = length();
    int n = figureSteps(c_.figure, c_.resolution);
    for (int k = 1; k < n && c + k < N; ++k)  // do not eat the following note
        if (l_.pat[u(c + k)] >= 0) {
            n = k;
            break;
        }
    l_.tie[u(c)] = 0;
    if (n > 1)
        for (int k = 0; k < n && c + k < N; ++k) {
            if (k) {  // the covered steps have no note of their own
                l_.pat[u(c + k)] = -1;
                l_.oct[u(c + k)] = 0;
            }
            l_.tie[u(c + k)] = 1;  // held until the last covered step
        }
    for (int k = c + n; k < N && l_.tie[u(k)]; ++k) {  // leftovers of a longer tie
        if (l_.pat[u(k)] >= 0) break;
        l_.tie[u(k)] = 0;
    }
}

bool LaneEditor::slideRunsAt(int a) const noexcept {
    // Back from the step before a: the first note met decides (its SLD); on the way, a step that has neither SLD nor
    // a tie means no slide.
    for (int k = std::min(a, kSteps) - 1; k >= 0; --k) {
        if (l_.pat[u(k)] >= 0) return l_.slide[u(k)] != 0;
        if (!l_.slide[u(k)] && !l_.tie[u(k)]) return false;
    }
    return false;
}

int LaneEditor::noteLenAt(int s) const noexcept {
    if (!inLane(s)) return 1;
    const int N = length();
    int d = 1;
    while (s + d < N && l_.pat[u(s + d)] < 0 && l_.tie[u(s + d)]) ++d;
    return l_.tie[u(s)] ? d : 1;
}

void LaneEditor::setNoteLen(int s, int n) noexcept {
    if (!inLane(s)) return;
    const int N = length();
    // Deviation: not in the 3-channel modes either, which play no chord voice: the note met would
    // fall silent (the prototype demoted it there too).
    const bool poly = !c_.mono && !c_.threeVoice && !c_.drum;
    n = std::max(1, std::min(n, N - s));
    for (int k = 1; k < n; ++k)  // limit: next note
        if (l_.pat[u(s + k)] >= 0) {
            // In polyphonic mode, the note met becomes a voice: the extension goes on.
            if (poly && demoteMain(s + k)) continue;
            n = k;
            break;
        }
    // Ties rewritten over the longer of the old and the new length: the steps beyond the new length lose theirs.
    const int was = noteLenAt(s);
    for (int k = 0; k < std::max(n, was); ++k) {
        if (s + k >= N) break;
        l_.tie[u(s + k)] = (k < n && n > 1) ? 1 : 0;
        if (k && k < n) {
            l_.pat[u(s + k)] = -1;
            l_.oct[u(s + k)] = 0;
        }
    }
}

int LaneEditor::setNoteStart(int s, int ns) noexcept {
    if (!inLane(s)) return -1;
    const int end = s + noteLenAt(s) - 1;  // last step, kept
    ns = std::max(0, std::min(ns, end));
    // Limit: previous note. Deviation: the search starts from the current start backwards (the nearest note); the
    // prototype started from the new start and could jump over a note when dragging fast. Deviation: the
    // held part of the previous note stops it too (the prototype cut that note short).
    for (int k = s - 1; k >= ns; --k)
        if (l_.pat[u(k)] >= 0 || (!c_.drum && l_.tie[u(k)] && noteStart(k) >= 0)) {
            ns = k + 1;
            break;
        }
    if (ns == s) return -1;
    // Deviation: the pitch is kept as it is (writeNoteAt brought a note written before Snap to a degree).
    const auto pat = l_.pat[u(s)];
    const auto oct = l_.oct[u(s)];
    const auto vol = l_.vol[u(s)], mx = l_.mix[u(s)], fx = l_.fx[u(s)], ins = l_.ins[u(s)];
    const auto sld = l_.slide[u(s)], slc = l_.slc[u(s)];
    for (int k = s; k <= end; ++k) {
        l_.pat[u(k)] = -1;
        l_.oct[u(k)] = 0;
        l_.tie[u(k)] = 0;
    }
    l_.pat[u(ns)] = pat;
    l_.oct[u(ns)] = oct;
    l_.vol[u(ns)] = vol;
    l_.mix[u(ns)] = mx;
    l_.fx[u(ns)] = fx;
    l_.ins[u(ns)] = ins;
    l_.slide[u(ns)] = sld;
    l_.slc[u(ns)] = slc;
    setNoteLen(ns, end - ns + 1);
    return ns;
}

bool LaneEditor::splitNote(int s, int at) noexcept {
    if (!inLane(s)) return false;
    const int n = noteLenAt(s);
    if (at <= s || at >= s + n) return false;
    // Deviation: the second half keeps the pitch as it is (writeNoteAt brought a note written before Snap to
    // a degree).
    const auto pat = l_.pat[u(s)];
    const auto oct = l_.oct[u(s)];
    const auto vol = l_.vol[u(s)], mx = l_.mix[u(s)], fx = l_.fx[u(s)], ins = l_.ins[u(s)];
    setNoteLen(s, at - s);
    l_.pat[u(at)] = pat;
    l_.oct[u(at)] = oct;
    l_.vol[u(at)] = vol;
    l_.mix[u(at)] = mx;
    l_.fx[u(at)] = fx;
    l_.ins[u(at)] = ins;
    setNoteLen(at, s + n - at);
    return true;
}

int LaneEditor::nextNoteAfter(int s) const noexcept {
    if (!inLane(s)) return -1;
    const int N = length();
    for (int k = s + noteLenAt(s); k < N; ++k)
        if (l_.pat[u(k)] >= 0) return k;
    return -1;
}

bool LaneEditor::joinNote(int s) noexcept {
    const int nx = nextNoteAfter(s);
    if (nx < 0) return false;
    const int end = nx + noteLenAt(nx) - 1;  // end of the second note
    for (int k = s + 1; k <= end; ++k) {
        l_.pat[u(k)] = -1;
        l_.oct[u(k)] = 0;
        l_.tie[u(k)] = 0;
    }
    setNoteLen(s, end - s + 1);  // the first one keeps its pitch
    return true;
}

void LaneEditor::eraseAt(int c) noexcept {
    if (!inLane(c)) return;
    const size_t i = u(c);
    l_.pat[i] = -1;
    l_.tie[i] = l_.slide[i] = l_.afl[i] = l_.cut[i] = l_.mix[i] = l_.fx[i] = l_.slt[i] = l_.gat[i] = l_.ins[i] = 0;
    l_.oct[i] = 0;
    l_.vol[i] = 15;
}

bool LaneEditor::stepBusyAt(int c, int row) const noexcept {
    if (!inLane(c)) return false;
    if (c_.drum) return l_.pat[u(c)] == row;
    if (voiceStartCovering(c, row)) return true;
    const int s = l_.pat[u(c)] >= 0 ? c : noteStart(c);
    return s >= 0 && relNote(s) == row;
}

bool LaneEditor::eraseNoteAt(int c, int row) noexcept {
    if (!inLane(c)) return false;
    if (c_.drum) {
        if (l_.pat[u(c)] != row) return false;
        l_.pat[u(c)] = -1;
        l_.oct[u(c)] = 0;
        l_.tie[u(c)] = 0;
        return true;
    }
    if (const auto hit = voiceStartCovering(c, row)) {  // chord voice, even extended
        delVoiceAt(hit->start, hit->voice);
        return true;
    }
    const int s = l_.pat[u(c)] >= 0 ? c : noteStart(c);  // main note, even held
    if (s >= 0 && relNote(s) == row) {
        const int n = noteLenAt(s);
        for (int k = 0; k < n; ++k) {
            l_.pat[u(s + k)] = -1;
            l_.oct[u(s + k)] = 0;
            l_.tie[u(s + k)] = 0;
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- chord voices

int LaneEditor::voiceDur(int s, int v) const noexcept {
    if (v < 2 || v > kPolyMax || !inLane(s)) return 1;
    const int d = l_.vdur[u(v - 2)][u(s)];
    return std::max(1, d ? d : 1);
}

std::optional<int> LaneEditor::voiceRow(int s, int v) const noexcept {
    if (!inLane(s) || v < 1 || v > kPolyMax) return std::nullopt;
    const int p = l_.vp(v, s);
    if (p < 0) return std::nullopt;
    if (c_.drum) return p;  // percussion: the instrument
    return (octaveOf(l_.vo(v, s), c_.trackOctave) - c_.trackOctave) * 12 + p;
}

int LaneEditor::voiceAt(int s, int row) const noexcept {
    for (int v = 1; v <= kPolyMax; ++v)
        if (voiceRow(s, v) == row) return v;
    return 0;
}

int LaneEditor::voiceCount(int s) const noexcept {
    if (!inLane(s)) return 0;
    int n = 0;
    for (int v = 1; v <= kPolyMax; ++v)
        if (l_.vp(v, s) >= 0) ++n;
    return n;
}

std::optional<VoiceHit> LaneEditor::voiceStartCovering(int s, int row) const noexcept {
    if (!inLane(s)) return std::nullopt;
    // Deviation: every earlier step is searched; the prototype looked 64 steps back only, while a voice
    // can be held up to 256 steps (and is played that long).
    for (int k = s; k >= 0; --k)
        for (int v = 2; v <= kPolyMax; ++v) {
            if (voiceRow(k, v) != row) continue;
            if (k + voiceDur(k, v) > s) return VoiceHit{k, v};
        }
    return std::nullopt;
}

bool LaneEditor::stepBusy(int s) const noexcept {
    if (!inLane(s)) return false;
    if (l_.pat[u(s)] >= 0) return true;
    if (c_.drum) {  // percussion: no tie
        for (int v = 2; v <= kPolyMax; ++v)
            if (l_.vp(v, s) >= 0) return true;
        return false;
    }
    if (noteStart(s) >= 0) return true;
    for (int v = 2; v <= kPolyMax; ++v)  // chord voice that extends (all the way back)
        for (int k = 0; k <= s; ++k)
            if (voiceRow(k, v) && k + voiceDur(k, v) > s) return true;
    return false;
}

int LaneEditor::freeVoice(int s) const noexcept {
    if (!inLane(s)) return 0;
    for (int v = 2; v <= kPolyMax; ++v)
        if (l_.vp(v, s) < 0) return v;
    return 0;
}

int LaneEditor::addVoiceAt(int s, int row) noexcept {
    const int v = freeVoice(s);
    if (!v) return 0;
    // Deviation: Snap brings the added voice to a degree, like the root; the prototype wrote it on the row
    // that was clicked, greyed or not.
    writeVoiceAt(s, snapRow(row), v);
    // same duration as the chosen figure
    const int d = c_.drum ? 1 : std::max(1, std::min(figureSteps(c_.figure, c_.resolution), length() - s));
    l_.vdur[u(v - 2)][u(s)] = uint16_t(d);
    return v;
}

void LaneEditor::delVoiceAt(int s, int v) noexcept {
    if (v < 2 || v > kPolyMax || !inLane(s)) return;
    l_.vp(v, s) = -1;
    l_.vo(v, s) = 0;
    l_.vdur[u(v - 2)][u(s)] = 0;
}

int LaneEditor::writeChord(int s, int row) noexcept {
    if (c_.chord < 0 || c_.chord >= int(ymengine::kChords.size())) return 0;
    if (c_.drum || c_.threeVoice || c_.mono) return 0;
    for (int v = 2; v <= kPolyMax; ++v) delVoiceAt(s, v);  // start clean
    const ymengine::Chord& ch = ymengine::kChords[u(c_.chord)];
    // Deviation: under Snap, the chord stands on the root brought into the scale and each voice goes to the
    // nearest degree (a major triad on D in C major gives D, F, A); a voice that lands on a pitch already written at
    // this step is left out. The prototype stacked the intervals on the clicked row, outside the scale.
    const bool snaps = c_.snap && c_.scale > 0 && c_.scale < kScaleCount;
    row = snapRow(row);
    int n = 0;
    for (int i = 0; i < ch.count; ++i) {
        const int iv = ch.offsets[u(i)];
        if (iv == 0) continue;  // the root is already written
        if (voiceCount(s) >= kPolyMax) break;
        if (row + iv > 60) break;  // outside the playable range
        if (snaps && voiceAt(s, snapRow(row + iv))) continue;
        addVoiceAt(s, row + iv);
        ++n;
    }
    return n;
}

bool LaneEditor::demoteMain(int s) noexcept {
    if (!inLane(s)) return false;
    const int row = relNote(s), ln = noteLenAt(s);
    const int v = freeVoice(s);
    if (!v) return false;
    for (int k = 0; k < ln; ++k) {  // free the space taken
        l_.pat[u(s + k)] = -1;
        l_.oct[u(s + k)] = 0;
        l_.tie[u(s + k)] = 0;
    }
    writeVoiceAt(s, row, v);
    l_.vdur[u(v - 2)][u(s)] = uint16_t(ln);
    return true;
}

bool LaneEditor::promoteVoice(int c, const VoiceHit& hit) noexcept {
    if (!inLane(c) || !inLane(hit.start) || hit.voice < 2 || hit.voice > kPolyMax) return false;
    const int v = hit.voice, s0 = hit.start;
    const bool drum = c_.drum;
    const int mainAt = l_.pat[u(c)] >= 0 ? c : (drum ? -1 : noteStart(c));
    if (mainAt < 0) return false;
    const auto newRowOpt = voiceRow(s0, v);
    if (!newRowOpt) return false;
    const int oldRow = drum ? l_.pat[u(mainAt)] : relNote(mainAt);
    const int oldOct = l_.oct[u(mainAt)], oldLen = drum ? 1 : noteLenAt(mainAt);
    const int newLen = voiceDur(s0, v);
    const int16_t newPat = l_.vp(v, s0);
    const int8_t newOct = l_.vo(v, s0);
    for (int k = 0; k < oldLen; ++k) {  // free the old root
        l_.pat[u(mainAt + k)] = -1;
        l_.oct[u(mainAt + k)] = 0;
        l_.tie[u(mainAt + k)] = 0;
    }
    delVoiceAt(s0, v);
    // The chosen voice becomes the root. Deviation: its pitch is copied as it is; the prototype rewrote it
    // with writeNoteAt and then put back the voice's octave, which under Snap could move it by an octave.
    l_.pat[u(s0)] = newPat;
    l_.oct[u(s0)] = newOct;
    if (!drum) setNoteLen(s0, newLen);
    int v2 = freeVoice(mainAt);  // the old one goes down to a greyed voice
    if (!v2) v2 = v;
    writeVoiceAt(mainAt, oldRow, v2);
    l_.vo(v2, mainAt) = int8_t(oldOct);
    l_.vdur[u(v2 - 2)][u(mainAt)] = uint16_t(oldLen);
    return true;
}

bool LaneEditor::setVoiceLen(int s, int v, int n) noexcept {
    if (!inLane(s) || v < 2 || v > kPolyMax) return false;
    const int n2 = std::max(1, n);
    if (n2 == voiceDur(s, v)) return false;
    l_.vdur[u(v - 2)][u(s)] = uint16_t(std::min(n2, kSteps));
    return true;
}

std::optional<VoiceHit> LaneEditor::setVoiceStart(int s, int v, int row, int ns) noexcept {
    if (!inLane(s) || v < 2 || v > kPolyMax || l_.vp(v, s) < 0) return std::nullopt;
    const int end = s + voiceDur(s, v);  // the end does not move
    ns = std::max(0, std::min(end - 1, ns));
    if (ns == s || voiceAt(ns, row) != 0) return std::nullopt;
    // Deviation: if the voice with the same number is already taken at the new start, the voice goes to a free voice
    // (the prototype overwrote the note that was there); without a free voice, nothing changes.
    const int v2 = l_.vp(v, ns) < 0 ? v : freeVoice(ns);
    if (!v2) return std::nullopt;
    const int16_t pv = l_.vp(v, s);
    const int8_t ov = l_.vo(v, s);
    delVoiceAt(s, v);
    l_.vp(v2, ns) = pv;
    l_.vo(v2, ns) = ov;
    l_.vdur[u(v2 - 2)][u(ns)] = uint16_t(end - ns);
    return VoiceHit{ns, v2};
}

int LaneEditor::previewNote(int s) const noexcept {
    if (!inLane(s) || l_.pat[u(s)] < 0) return -1;
    if (c_.drum) return l_.pat[u(s)];  // percussion: the row (sequencer lane)
    return (octaveOf(l_.oct[u(s)], c_.trackOctave) + 1) * 12 + l_.pat[u(s)];
}

// ---------------------------------------------------------------- note clipboard

NoteClip LaneEditor::copyNoteAt(int s) const noexcept {
    NoteClip c;
    if (!inLane(s)) return c;
    c.row = relNote(s);
    c.len = noteLenAt(s);
    c.vol = l_.vol[u(s)];
    c.mix = l_.mix[u(s)];
    c.fx = l_.fx[u(s)];
    c.ins = l_.ins[u(s)];
    c.slide = l_.slide[u(s)];
    c.slc = l_.slc[u(s)];
    return c;
}

void LaneEditor::cutNoteAt(int s) noexcept {
    if (!inLane(s)) return;
    const int n = noteLenAt(s);
    for (int k = 0; k < n; ++k) {
        l_.pat[u(s + k)] = -1;
        l_.oct[u(s + k)] = 0;
        l_.tie[u(s + k)] = 0;
    }
}

bool LaneEditor::pasteNoteAt(int s, int row, const NoteClip& clip) noexcept {
    if (s < 0 || s >= length()) return false;
    writeNoteAt(s, row);
    l_.vol[u(s)] = uint8_t(clip.vol);
    l_.mix[u(s)] = uint8_t(clip.mix);
    l_.fx[u(s)] = uint8_t(clip.fx);
    l_.ins[u(s)] = uint8_t(clip.ins);
    l_.slide[u(s)] = uint8_t(clip.slide);
    l_.slc[u(s)] = uint8_t(clip.slc);
    setNoteLen(s, clip.len);
    return true;
}

// ---------------------------------------------------------------- moving

void LaneEditor::moveNoteTo(Moving& m, int c2, int row2) noexcept {
    const int N = length(), n = m.len;
    c2 = std::max(0, std::min(c2, N - n));  // the note stays whole inside the pattern
    if (c2 == m.from && row2 == m.row) return;
    // Deviation: Snap brings the target row to a degree before the tests, for a chord voice too (the prototype
    // tested the clicked row, then brought back the root alone).
    row2 = snapRow(row2);
    if (c2 == m.from && row2 == m.row) return;
    if (!inLane(m.from) || !inLane(c2)) return;
    if (m.voice) {  // chord voice: separate storage (moveVoiceTo)
        const int v = m.voice;
        if (v < 2 || v > kPolyMax) return;
        const int16_t pv = l_.vp(v, m.from);
        const int8_t ov = l_.vo(v, m.from);
        l_.vp(v, m.from) = -1;  // free before testing
        l_.vo(v, m.from) = 0;
        l_.vdur[u(v - 2)][u(m.from)] = 0;
        const int v2 = voiceAt(c2, row2) ? 0 : freeVoice(c2);
        if (!v2) {  // no room: put things back as they were
            l_.vp(v, m.from) = pv;
            l_.vo(v, m.from) = ov;
            l_.vdur[u(v - 2)][u(m.from)] = uint16_t(n);
            return;
        }
        writeVoiceAt(c2, row2, v2);
        l_.vdur[u(v2 - 2)][u(c2)] = uint16_t(n);
        m.from = c2;
        m.row = row2;
        m.voice = v2;
        m.moved = true;
        return;
    }
    for (int k = 0; k < n && c2 + k < N; ++k) {  // is the space free? (test before any change)
        const int t = c2 + k;
        if (l_.pat[u(t)] >= 0 && t != m.from) return;
        // Deviation: the moved note's own start is not in the way (the prototype took it for a voice that
        // holds the pitch, so a long note could not go back by less than its length), but the held part of another
        // note is (the prototype wrote over it and cut that note short).
        for (int v = 2; v <= kPolyMax; ++v)
            if (voiceRow(t, v) == row2) return;  // a voice already holds this pitch
        if (!c_.drum && l_.pat[u(t)] < 0 && l_.tie[u(t)]) {
            const int ns = noteStart(t);
            if (ns >= 0 && ns != m.from) return;
        }
    }
    // Deviation: a Digidrum hit held over several steps keeps its length, like a note.
    const bool ties = !m.drum || n > 1;
    // Deviation: the rows of the note's start (kNoteRows) go with it; the step it leaves gets back what it
    // held before the note came. The prototype left them behind (accent, slide, curve, source, generator).
    const size_t rows = c_.drum ? size_t(kDrumNoteRows) : size_t(kMelodicNoteRows);
    std::array<int, kNoteRows.size()> carried{};
    for (size_t i = 0; i < rows; ++i) carried[i] = l_.get(kNoteRows[i], m.from);
    if (ties) clearFigure(m.from);  // free the original slot
    l_.pat[u(m.from)] = -1;
    l_.oct[u(m.from)] = 0;
    for (size_t i = 0; i < rows; ++i) l_.set(kNoteRows[i], m.from, m.under[i]);
    for (size_t i = 0; i < rows; ++i) m.under[i] = l_.get(kNoteRows[i], c2);
    writeNoteAt(c2, row2);
    for (size_t i = 0; i < rows; ++i) l_.set(kNoteRows[i], c2, carried[i]);
    if (ties) {
        for (int k = 1; k < n && c2 + k < N; ++k) {
            l_.pat[u(c2 + k)] = -1;
            l_.oct[u(c2 + k)] = 0;
        }
        for (int k = 0; k < n && c2 + k < N; ++k) l_.tie[u(c2 + k)] = n > 1 ? 1 : 0;
        for (int k = c2 + n; k < N && l_.tie[u(k)]; ++k) {
            if (l_.pat[u(k)] >= 0) break;
            l_.tie[u(k)] = 0;
        }
    }
    m.from = c2;
    m.row = row2;
    m.moved = true;
}

// ---------------------------------------------------------------- rectangular selection

std::vector<RectNote> LaneEditor::rectNotes(const RectBounds& r) const {
    std::vector<RectNote> out;
    for (int s = std::max(0, r.a); s <= std::min(r.b, kSteps - 1); ++s) {
        if (l_.pat[u(s)] >= 0) {
            const int row = relNote(s);
            if (row >= r.r1 && row <= r.r2) {
                int d = 1;
                while (s + d < kSteps && l_.tie[u(s + d)] && l_.pat[u(s + d)] < 0) ++d;
                out.push_back({s, row, l_.tie[u(s)] ? d : 1, l_.vol[u(s)], l_.slide[u(s)], l_.slc[u(s)], 1});
            }
        }
        // Deviation: the chord voices that start in the box are part of it, as the grid shows them (the
        // prototype took the roots only: Delete, Cut, Copy and the moves left the voices behind).
        for (int v = 2; v <= kPolyMax; ++v) {
            const auto row = voiceRow(s, v);
            if (!row || *row < r.r1 || *row > r.r2) continue;
            RectNote n;
            n.s = s;
            n.row = *row;
            n.len = voiceDur(s, v);
            n.v = v;
            out.push_back(n);
        }
    }
    return out;
}

void LaneEditor::rectClear(const RectBounds& r) noexcept { clearNotes(rectNotes(r)); }

void LaneEditor::clearNotes(const std::vector<RectNote>& notes) noexcept {
    for (const RectNote& n : notes) {
        if (n.v >= 2) {
            delVoiceAt(n.s, n.v);
            continue;
        }
        for (int k = 0; k < n.len && n.s + k < kSteps; ++k) {
            l_.pat[u(n.s + k)] = -1;
            l_.oct[u(n.s + k)] = 0;
            l_.tie[u(n.s + k)] = 0;
        }
    }
}

namespace {
// Writes a note of a rectangular selection (rectMove, paste) at step s: pitch, VOL, SLD, CRV and tie for a root; a
// chord voice takes a free voice of the step. The hold of a root stops before the next root (deviation:
// the prototype erased the notes it covered). False if a chord voice finds no room (pitch already there, step full).
bool placeRectNote(LaneEditor& e, Lane& l, int s, const RectNote& n) {
    if (n.v >= 2) {
        const int row = e.snapRow(n.row);
        const int v = e.voiceAt(s, row) ? 0 : e.freeVoice(s);
        if (!v) return false;
        e.writeVoiceAt(s, row, v);
        l.vdur[u(v - 2)][u(s)] = uint16_t(std::max(1, n.len));
        return true;
    }
    e.writeNoteAt(s, n.row);
    l.vol[u(s)] = uint8_t(n.vol);
    l.slide[u(s)] = uint8_t(n.slide);
    l.slc[u(s)] = uint8_t(n.slc);
    int len = n.len;
    for (int k = 1; k < len; ++k)
        if (s + k >= kSteps || l.pat[u(s + k)] >= 0) {
            len = k;
            break;
        }
    if (len > 1)
        for (int k = 0; k < len && s + k < kSteps; ++k) {
            if (k) {
                l.pat[u(s + k)] = -1;
                l.oct[u(s + k)] = 0;
            }
            l.tie[u(s + k)] = 1;
        }
    return true;
}
}  // namespace

bool LaneEditor::rectMove(RectBounds& r, int ds, int dr) {
    if (!ds && !dr) return false;  // nothing to move
    const std::vector<RectNote> src = rectNotes(r);
    if (src.empty()) return false;
    std::vector<RectNote> dst;
    if (!moveNotes(src, ds, dr, dst)) return false;
    // The box follows the notes (under Snap, a note can go further than the box).
    int lo = r.r1 + dr, hi = r.r2 + dr;
    for (const RectNote& n : dst) {
        lo = std::min(lo, n.row);
        hi = std::max(hi, n.row);
    }
    r.a += ds;
    r.b += ds;
    r.r1 = lo;
    r.r2 = hi;
    return true;
}

bool LaneEditor::moveNotes(const std::vector<RectNote>& src, int ds, int dr, std::vector<RectNote>& dst) {
    const int N = length();
    // Deviations: under Snap, a note moved up or down never stays on its pitch (snapTransposed); all or
    // nothing, the notes do not leave the playable range (the prototype crushed them under B0) and do not land on a
    // note outside the box or on its held part (the prototype erased it).
    const int base = (c_.trackOctave + 1) * 12;
    dst = src;
    for (RectNote& n : dst) {
        n.s += ds;
        n.row = dr ? transposedRow(n.row, dr) : snapRow(n.row);
        if (n.s < 0 || n.s + (n.v >= 2 ? 1 : n.len) > N) return false;  // outside the pattern: cancel
        if (c_.drum ? n.row < 0 || n.row > 7 : n.row + base < kPlayLow || n.row + base > kMapHigh) return false;
    }
    const Lane before = l_;
    clearNotes(src);
    for (const RectNote& n : dst) {
        bool free = true;
        if (n.v >= 2) free = !voiceAt(n.s, n.row) && freeVoice(n.s);
        else
            for (int k = 0; k < n.len && free; ++k) {
                const int t = n.s + k;
                if (l_.pat[u(t)] >= 0 || (!c_.drum && l_.tie[u(t)] && noteStart(t) >= 0)) free = false;
                for (int v = 2; v <= kPolyMax && free; ++v)
                    if (voiceRow(t, v) == n.row) free = false;
            }
        if (!free || !placeRectNote(*this, l_, n.s, n)) {
            l_ = before;
            return false;
        }
    }
    return true;
}

void LaneEditor::rectPaste(const RectBounds& r, const std::vector<RectNote>& clip) noexcept {
    const int N = length();
    for (const RectNote& n : clip) {
        const int s = r.a + n.s;
        if (s < 0 || s + (n.v >= 2 ? 1 : n.len) > N) continue;
        placeRectNote(*this, l_, s, n);
    }
}

// ---------------------------------------------------------------- note-by-note selection

std::vector<SelNote> LaneEditor::resolve(const std::vector<SelKey>& keys) const {
    std::vector<SelNote> out;
    for (const SelKey& k : keys)
        if (const auto row = voiceRow(k.s, k.v)) out.push_back({k.s, k.v, *row});
    std::stable_sort(out.begin(), out.end(), [](const SelNote& a, const SelNote& b) { return a.s < b.s; });
    return out;
}

bool LaneEditor::transposeSel(const std::vector<SelKey>& keys, int semis) {
    const std::vector<SelNote> items = resolve(keys);
    if (items.empty()) return false;
    const int base = (c_.trackOctave + 1) * 12;
    // Deviation: Snap applies to the chord voices as to the roots. Deviation: a note never stays on
    // its pitch (snapTransposed: the nearest degree brought +1 back onto most notes), range checked after Snap.
    std::vector<int> rows;
    rows.reserve(items.size());
    for (const SelNote& it : items) {  // all or nothing
        rows.push_back(transposedRow(it.row, semis));
        const int abs = rows.back() + base;
        if (abs < kPlayLow || abs > kMapHigh) return true;
    }
    for (size_t i = 0; i < items.size(); ++i) {
        const SelNote& it = items[i];
        if (it.v == 1) writeNoteAt(it.s, rows[i]);
        else writeVoiceAt(it.s, rows[i], it.v);
    }
    return true;
}

bool LaneEditor::shiftSel(std::vector<SelKey>& keys, int dir) {
    const std::vector<SelNote> items = resolve(keys);
    if (items.empty()) return false;
    const int N = length();
    for (const SelNote& it : items) {
        const int ns = it.s + dir;
        if (ns < 0 || ns >= N) return true;  // limit: do nothing
    }
    const Lane before = l_;
    // Each note read before anything moves: row, voice, step, chord voice duration, root length (0 for a voice).
    struct Cached {
        int row, v, s, len, ln1;
    };
    std::vector<Cached> cache;
    cache.reserve(items.size());
    for (const SelNote& it : items)
        cache.push_back({it.row, it.v, it.s, voiceDur(it.s, it.v), it.v == 1 ? noteLenAt(it.s) : 0});
    for (const Cached& it : cache) {  // free the places
        if (it.v == 1) {
            for (int k = 0; k < it.ln1; ++k) {
                l_.pat[u(it.s + k)] = -1;
                l_.oct[u(it.s + k)] = 0;
                l_.tie[u(it.s + k)] = 0;
            }
        } else
            delVoiceAt(it.s, it.v);
    }
    // Deviation: all or nothing, a shifted note does not land on a note outside the selection (or on its held
    // part, or on a voice of the same pitch), and a chord voice does not get lost on a full step; the prototype wrote
    // over the roots and dropped the voice.
    std::vector<SelKey> next;
    for (const Cached& it : cache) {  // write them back shifted
        const int ns = it.s + dir;
        bool free = true;
        if (it.v == 1) {
            const int row = snapRow(it.row);
            for (int k = 0; k < std::max(1, it.ln1) && ns + k < N && free; ++k) {
                const int t = ns + k;
                if (l_.pat[u(t)] >= 0 || (!c_.drum && l_.tie[u(t)] && noteStart(t) >= 0)) free = false;
                for (int v = 2; v <= kPolyMax && free; ++v)
                    if (voiceRow(t, v) == row) free = false;
            }
            if (free) {
                writeNoteAt(ns, it.row);
                setNoteLen(ns, it.ln1);
                next.push_back({ns, 1});
            }
        } else if (voiceAt(ns, snapRow(it.row))) free = false;
        else if (const int v2 = addVoiceAt(ns, it.row)) {
            l_.vdur[u(v2 - 2)][u(ns)] = uint16_t(it.len);
            next.push_back({ns, v2});
        } else free = false;
        if (!free) {
            l_ = before;
            return true;
        }
    }
    keys = std::move(next);
    return true;
}

bool LaneEditor::moveSel(std::vector<SelKey>& keys, int ds, int dr) {
    if (!ds && !dr) return false;
    // The picked notes go as a box would carry them: a root keeps its hold, VOL, SLD and CRV, a voice its duration.
    std::vector<RectNote> src;
    for (const SelNote& it : resolve(keys)) {
        RectNote n;
        n.s = it.s;
        n.row = it.row;
        n.v = it.v;
        if (it.v == 1) {
            n.len = noteLenAt(it.s);
            n.vol = l_.vol[u(it.s)];
            n.slide = l_.slide[u(it.s)];
            n.slc = l_.slc[u(it.s)];
        } else
            n.len = voiceDur(it.s, it.v);
        src.push_back(n);
    }
    if (src.empty()) return false;
    std::vector<RectNote> dst;
    if (!moveNotes(src, ds, dr, dst)) return false;
    keys.clear();
    for (const RectNote& n : dst) keys.push_back({n.s, n.v >= 2 ? voiceAt(n.s, n.row) : 1});
    return true;
}

// ---------------------------------------------------------------- whole track

void LaneEditor::rotate(int dir) noexcept {
    const int N = length();
    for (Row r : kRotatedRows) {
        if (dir > 0) {
            const int last = l_.get(r, N - 1);
            for (int i = N - 1; i > 0; --i) l_.set(r, i, l_.get(r, i - 1));
            l_.set(r, 0, last);
        } else {
            const int first = l_.get(r, 0);
            for (int i = 0; i < N - 1; ++i) l_.set(r, i, l_.get(r, i + 1));
            l_.set(r, N - 1, first);
        }
    }
}

bool LaneEditor::transpose(int semis) noexcept {
    if (c_.drum) return false;  // percussion: fixed rows
    const int N = length(), T = c_.trackOctave;
    // All or nothing: if a note left the range, clipping would crush the intervals of the melody.
    // Deviations: octave 0 (stored as -1) is read and rewritten correctly, and the chord voices follow the root (the
    // prototype only transposed the root).
    std::array<std::array<int16_t, kSteps>, kPolyMax> next{};
    for (int v = 1; v <= kPolyMax; ++v)
        for (int st = 0; st < N; ++st) {
            int16_t& out = next[u(v - 1)][u(st)];
            out = -1;
            if (l_.vp(v, st) < 0) continue;
            const int from = (octaveOf(l_.vo(v, st), T) + 1) * 12 + l_.vp(v, st);
            // Stay in the scale. Deviations: a note never stays on its pitch (snapTransposed: the nearest
            // degree brought +1 back onto most notes), and the range is the grid's and transposeSel's, B0 to C9 (the
            // prototype stopped at C1 and C8, so a track holding a note above C8 could not be transposed at all).
            const int abs = snapTransposed(from, semis, c_.scale, c_.scaleRoot, c_.snap);
            if (abs < kPlayLow || abs > kMapHigh) return false;
            out = int16_t(abs);
        }
    for (int v = 1; v <= kPolyMax; ++v)
        for (int st = 0; st < N; ++st) {
            const int abs = next[u(v - 1)][u(st)];
            if (abs < 0) continue;
            const int no = std::clamp(abs / 12 - 1, 0, 9);
            l_.vp(v, st) = int16_t(abs - (no + 1) * 12);
            l_.vo(v, st) = int8_t(octaveCode(no, T));
        }
    return true;
}

void LaneEditor::clear() noexcept {
    // Deviation: the TON row is emptied too (the prototype forgot it in clearLane).
    l_ = Lane{};
}

bool LaneEditor::hasNotes() const noexcept { return l_.hasNotes(length()); }

namespace {
// A single row: the CMD row stands for every kind of the Commands box, shown as one row in the grid; the
// rows of Vxx, Txx, Exx, Dxx and Cxx are rows of their own, Exx with Rxx (ENV row,
// twelfth DAW test, item 146).
void copyStep(Lane& dst, int ds, const Lane& src, int ss, std::optional<Row> row) noexcept {
    if (row && isCmdBoxRow(*row)) {
        dst.copyBoxCmds(ds, src, ss);
        return;
    }
    if (row) {
        dst.set(*row, ds, src.get(*row, ss));
        dst.set(rowPartner(*row), ds, src.get(rowPartner(*row), ss));
        return;
    }
    for (int r = 0; r < kRowCount; ++r) dst.set(Row(r), ds, src.get(Row(r), ss));
    for (int v = 0; v < kExtraVoices; ++v) {
        dst.vpat[u(v)][u(ds)] = src.vpat[u(v)][u(ss)];
        dst.voct[u(v)][u(ds)] = src.voct[u(v)][u(ss)];
        dst.vdur[u(v)][u(ds)] = src.vdur[u(v)][u(ss)];
    }
}
}  // namespace

BlockClip LaneEditor::blockCopy(int a, int b, std::optional<Row> row) const noexcept {
    BlockClip clip;
    a = std::clamp(a, 0, kSteps - 1);
    b = std::clamp(b, a, kSteps - 1);
    clip.n = b - a + 1;
    clip.row = row;
    for (int i = 0; i < clip.n; ++i) copyStep(clip.data, i, l_, a + i, row);
    return clip;
}

void LaneEditor::blockClear(int a, int b, std::optional<Row> row) noexcept {
    a = std::clamp(a, 0, kSteps - 1);
    b = std::clamp(b, a, kSteps - 1);
    for (int s = a; s <= b; ++s) {
        if (row && isCmdBoxRow(*row))
            l_.clearBoxCmds(s);  // every kind of the CMD row
        else if (row) {
            l_.set(*row, s, Lane::emptyValue(*row));
            l_.set(rowPartner(*row), s, Lane::emptyValue(rowPartner(*row)));
        }
        else
            l_.clearStep(s);  // Deviation: the chord voices are emptied (-1); the prototype wrote 0 there (a C)
    }
}

void LaneEditor::blockPaste(int at, const BlockClip& clip) noexcept {
    const int N = length();
    for (int i = 0; i < clip.n && at + i < N; ++i)
        if (at + i >= 0) copyStep(l_, at + i, clip.data, i, clip.row);
}

// ---------------------------------------------------------------- recording

void LaneEditor::recordAt(int s, int note, int velocity) noexcept {
    if (!inLane(s)) return;
    if (c_.drum)  // pitch class folded onto the 8 percussion rows: C → 0 … G → 7, G# → 0 … B → 3
        l_.pat[u(s)] = int16_t(mod12(note) % 8);
    else {
        const int nn = std::clamp(snapToScale(note, c_.scale, c_.scaleRoot, c_.snap), 0, 127);  // constraining mode
        const int oct = std::clamp(nn / 12 - 1, 0, 9);
        l_.pat[u(s)] = int16_t(nn % 12);
        // Deviation: octave 0 is written as -1 (octCode); the prototype wrote 0, which reads as "track octave".
        l_.oct[u(s)] = int8_t(octaveCode(oct, c_.trackOctave));
    }
    l_.vol[u(s)] = uint8_t(std::max(1, int(std::floor(double(velocity) / 127.0 * 15.0 + 0.5))));  // velocity → accent
}

// ---------------------------------------------------------------- resolution change

Lane fitLane(const Lane& src, int num, int den) {
    Lane out;
    if (num <= 0 || den <= 0) return src;
    const int N = kSteps;
    auto at = [num, den](int s) { return fitStep(s, num, den); };
    // Length of the root at s, its hold included (LaneEditor::noteLenAt over the 256 steps).
    auto lenAt = [&src](int s) {
        int d = 1;
        while (s + d < N && src.pat[u(s + d)] < 0 && src.tie[u(s + d)]) ++d;
        return src.tie[u(s)] ? d : 1;
    };
    // End (excluded) of a note or voice of `len` steps from s, one step at least.
    auto endOf = [&](int s, int ns, int len) { return std::max(ns + 1, at(s + len)); };
    auto copyVoices = [&](int s, int ns, bool fill) {
        for (int v = 2; v <= kPolyMax; ++v) {
            if (src.vp(v, s) < 0 || (fill && out.vp(v, ns) >= 0)) continue;
            out.vp(v, ns) = src.vp(v, s);
            out.vo(v, ns) = src.vo(v, s);
            const int d = std::max(1, int(src.vdur[u(v - 2)][u(s)]));
            out.vdur[u(v - 2)][u(ns)] = uint16_t(std::min(endOf(s, ns, d), N) - ns);
        }
    };
    // Roots first, each with the rows of its step; their lengths once they are all placed.
    std::vector<std::pair<int, int>> ends;  // new start, new end (excluded)
    for (int s = 0; s < N; ++s) {
        if (src.pat[u(s)] < 0) continue;
        const int ns = at(s);
        if (ns >= N || out.pat[u(ns)] >= 0) continue;
        for (int r = 0; r < kRowCount; ++r)
            if (Row(r) != Row::Tie) out.set(Row(r), ns, src.get(Row(r), s));
        copyVoices(s, ns, false);
        ends.emplace_back(ns, endOf(s, ns, lenAt(s)));
    }
    // The other steps: their rows into the empty cells (a step held by a note keeps its VOL, ARP, RTG...).
    for (int s = 0; s < N; ++s) {
        if (src.pat[u(s)] >= 0) continue;
        const int ns = at(s);
        if (ns >= N) continue;
        for (int r = 0; r < kRowCount; ++r) {
            const Row row = Row(r);
            if (row == Row::Tie || row == Row::Pat) continue;
            const int v = src.get(row, s);
            if (v != Lane::emptyValue(row) && out.get(row, ns) == Lane::emptyValue(row)) out.set(row, ns, v);
        }
        copyVoices(s, ns, true);
    }
    // Holds: up to the new end, never over the next root (setNoteLen's ties).
    for (size_t k = 0; k < ends.size(); ++k) {
        const int ns = ends[k].first;
        const int next = k + 1 < ends.size() ? ends[k + 1].first : N;
        const int len = std::min(ends[k].second, next) - ns;
        if (len > 1)
            for (int j = 0; j < len; ++j) out.tie[u(ns + j)] = 1;
    }
    return out;
}

}  // namespace stym::ymseq
