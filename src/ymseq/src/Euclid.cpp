// STYM2149 — ymseq: Euclidean generator
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymseq/Euclid.h"

#include <algorithm>

namespace stym::ymseq {

std::vector<int> bjorklund(int K, int N) {
    if (K <= 0 || N <= 0) return std::vector<int>(size_t(std::max(0, N)), 0);
    if (K >= N) return std::vector<int>(size_t(N), 1);
    // Bjorklund's grouping, as the prototype writes it: a starts as K groups [1], b (the remainder) as N - K groups
    // [0]. Each pass appends b[i] to a[i] for the first min(|a|, |b|) groups; the groups left over on either side
    // become the new remainder. It stops when at most one remainder group is left; a then b, concatenated, give the
    // figure.
    std::vector<std::vector<int>> a, b;
    for (int i = 0; i < K; ++i) a.push_back({1});
    for (int i = 0; i < N - K; ++i) b.push_back({0});
    while (b.size() > 1) {
        const size_t n = std::min(a.size(), b.size());
        std::vector<std::vector<int>> na, nb;
        for (size_t i = 0; i < n; ++i) {
            std::vector<int> x = a[i];
            x.insert(x.end(), b[i].begin(), b[i].end());
            na.push_back(std::move(x));
        }
        if (a.size() > n)
            for (size_t i = n; i < a.size(); ++i) nb.push_back(a[i]);
        else
            for (size_t i = n; i < b.size(); ++i) nb.push_back(b[i]);
        a = std::move(na);
        b = std::move(nb);
    }
    std::vector<int> out;
    for (const auto& x : a) out.insert(out.end(), x.begin(), x.end());
    for (const auto& x : b) out.insert(out.end(), x.begin(), x.end());
    return out;
}

EuclidFigure euclidFigure(int steps, int pulses, int rotate, int accents) {
    EuclidFigure f;
    const int S = std::clamp(steps, kEuclidMinSteps, kEuclidMaxSteps);
    const int K = std::clamp(pulses, 0, S), R = ((rotate % S) + S) % S;
    f.steps = S;
    f.pulses = K;
    f.rotate = R;
    const std::vector<int> base = bjorklund(K, S);
    // Rotation: cell i shows cell i - R of the base figure (modulo S), so the hits move R steps later.
    f.hits.resize(size_t(S));
    for (int i = 0; i < S; ++i) f.hits[size_t(i)] = base[size_t((((i - R) % S) + S) % S)];
    const int A = std::min(accents, K);
    if (A > 0) {  // accents spread over the hits
        // E(A, K) over the hits in their order: the j-th hit (after rotation) takes cell j of that figure.
        const std::vector<int> am = bjorklund(A, K);
        f.accents.assign(size_t(S), 0);
        size_t j = 0;
        for (int i = 0; i < S; ++i)
            if (f.hits[size_t(i)]) {
                f.accents[size_t(i)] = j < am.size() ? am[j] : 0;
                ++j;
            }
    }
    return f;
}

void euclidApply(LaneEditor& e, Lane& l, const EuclidFigure& f, EuclidTarget target, int value, bool merge) {
    const int N0 = e.length();
    const bool digid = target == EuclidTarget::Digidrum;
    if (!merge)
        for (int s = 0; s < N0; ++s) {
            l.pat[size_t(s)] = -1;
            l.tie[size_t(s)] = 0;
            l.oct[size_t(s)] = 0;
            if (digid) l.slt[size_t(s)] = 0;
            for (int v = 2; v <= kPolyMax; ++v) e.delVoiceAt(s, v);
        }
    if (f.steps <= 0 || f.hits.empty()) return;
    for (int s = 0; s < N0; ++s) {  // the pattern repeats over the track
        const size_t i = size_t(s % f.steps);
        if (!f.hits[i]) continue;
        if (target == EuclidTarget::Melodic)
            e.writeNoteAt(s, value);
        else if (digid) {  // D-103: the row played is the slot (Chan: the track's own); SLT as in the prototype
            l.pat[size_t(s)] = int16_t(std::clamp(value > 0 ? value - 1 : e.context().digidrumSlot, 0, 7));
            l.slt[size_t(s)] = uint8_t(value);
        } else {
            l.pat[size_t(s)] = int16_t(value);
        }
        if (!f.accents.empty()) l.vol[size_t(s)] = f.accents[i] ? 15 : 10;  // accents in the VOL row
    }
}

bool euclidReplacesNotes(const Lane& lane, const EditContext& ctx, const EuclidFigure& f, EuclidTarget target,
                         int value, bool merge) {
    Lane after = lane;
    LaneEditor ed(after, ctx);
    euclidApply(ed, after, f, target, value, merge);
    const int n = ed.length();
    for (int s = 0; s < n; ++s) {
        const size_t u = size_t(s);
        bool voices = false, changed = false;
        for (int v = 0; v < kExtraVoices; ++v) {
            voices = voices || lane.vpat[size_t(v)][u] >= 0;
            changed = changed || lane.vpat[size_t(v)][u] != after.vpat[size_t(v)][u] ||
                      lane.voct[size_t(v)][u] != after.voct[size_t(v)][u] ||
                      lane.vdur[size_t(v)][u] != after.vdur[size_t(v)][u];
        }
        if (!(lane.pat[u] >= 0 || lane.tie[u] || voices)) continue;  // nothing written at this step
        changed = changed || lane.pat[u] != after.pat[u] || lane.oct[u] != after.oct[u] || lane.tie[u] != after.tie[u];
        if (changed) return true;
    }
    return false;
}

}  // namespace stym::ymseq
