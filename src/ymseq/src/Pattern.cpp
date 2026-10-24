// STYM2149 — ymseq: sequencer patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymseq/Pattern.h"

#include <algorithm>

namespace stym::ymseq {

int Lane::get(Row r, int s) const noexcept {
    const size_t i = size_t(s);
    switch (r) {
    case Row::Pat: return pat[i];
    case Row::Tie: return tie[i];
    case Row::Slide: return slide[i];
    case Row::Afl: return afl[i];
    case Row::Vol: return vol[i];
    case Row::Mix: return mix[i];
    case Row::Slt: return slt[i];
    case Row::Cut: return cut[i];
    case Row::Fx: return fx[i];
    case Row::Oct: return oct[i];
    case Row::Gat: return gat[i];
    case Row::Pnr: return pnr[i];
    case Row::Rtg: return rtg[i];
    case Row::Cnd: return cnd[i];
    case Row::Slc: return slc[i];
    case Row::Ton: return ton[i];
    case Row::Ins: return ins[i];
    default:
        if (isCmdRow(r)) return cmdAt(cmdKind(r), s);
        break;
    }
    return 0;
}

void Lane::set(Row r, int s, int v) noexcept {
    const size_t i = size_t(s);
    switch (r) {
    case Row::Pat: pat[i] = int16_t(v); break;
    case Row::Tie: tie[i] = uint8_t(v); break;
    case Row::Slide: slide[i] = uint8_t(v); break;
    case Row::Afl: afl[i] = uint8_t(v); break;
    case Row::Vol: vol[i] = uint8_t(v); break;
    case Row::Mix: mix[i] = uint8_t(v); break;
    case Row::Slt: slt[i] = uint8_t(v); break;
    case Row::Cut: cut[i] = uint8_t(v); break;
    case Row::Fx: fx[i] = uint8_t(v); break;
    case Row::Oct: oct[i] = int8_t(v); break;
    case Row::Gat: gat[i] = uint8_t(v); break;
    case Row::Pnr: pnr[i] = uint8_t(v); break;
    case Row::Rtg: rtg[i] = uint8_t(v); break;
    case Row::Cnd: cnd[i] = uint8_t(v); break;
    case Row::Slc: slc[i] = uint8_t(v); break;
    case Row::Ton: ton[i] = int8_t(v); break;
    case Row::Ins: ins[i] = uint8_t(v); break;
    default:
        if (!isCmdRow(r)) break;
        if (ymengine::seqCmdKind(uint16_t(v)) == ymengine::SeqCmd::None) clearCmd(cmdKind(r), s);
        else setCmd(s, uint16_t(v));
        break;
    }
}

void Lane::clearStep(int s) noexcept {
    for (int r = 0; r < kRowCount; ++r) set(Row(r), s, emptyValue(Row(r)));
    for (int v = 0; v < kExtraVoices; ++v) {
        vpat[size_t(v)][size_t(s)] = -1;
        voct[size_t(v)][size_t(s)] = 0;
        vdur[size_t(v)][size_t(s)] = 0;
    }
}

void Lane::copyBoxCmds(int s, const Lane& src, int ss) noexcept {
    for (int k = 1; k <= ymengine::kSeqCmdKinds; ++k)
        if (ymengine::seqCmdInBox(ymengine::SeqCmd(k)))
            cmd[size_t(k - 1)][size_t(s)] = src.cmd[size_t(k - 1)][size_t(ss)];
}

void Lane::clearBoxCmds(int s) noexcept {
    for (int k = 1; k <= ymengine::kSeqCmdKinds; ++k)
        if (ymengine::seqCmdInBox(ymengine::SeqCmd(k))) clearCmd(ymengine::SeqCmd(k), s);
}

bool Lane::empty() const noexcept {
    static const Lane kEmpty;
    return *this == kEmpty;
}

bool Lane::hasNotes(int n) const noexcept {
    n = std::clamp(n, 0, kSteps);
    for (int s = 0; s < n; ++s)
        for (int v = 1; v <= kPolyMax; ++v)
            if (vp(v, s) >= 0) return true;
    return false;
}

const LanePtr& emptyLane() {
    static const LanePtr kEmpty = std::make_shared<const Lane>();
    return kEmpty;
}

const Lane& Pattern::lane(int track) const noexcept {
    const LanePtr& p = lanes[size_t(track)];
    return p ? *p : *emptyLane();
}

Lane& Pattern::edit(int track) {
    LanePtr& p = lanes[size_t(track)];
    // Copy on write: a shared track (undo snapshot, copy published to the audio thread) is never modified; a track
    // that this pattern alone holds is modified in place. A count of 1 is reliable even with other threads around: a
    // new reference can only be copied from an existing one, and this pattern holds the only one.
    if (p && p.use_count() == 1) return const_cast<Lane&>(*p);
    auto copy = std::make_shared<Lane>(p ? *p : Lane{});
    p = copy;
    return *copy;
}

void Pattern::compact() noexcept {
    for (LanePtr& p : lanes)
        if (p && p->empty()) p = nullptr;
}

bool Pattern::sameContent(const Pattern& o) const noexcept {
    if (steps != o.steps || bpm != o.bpm) return false;
    for (int t = 0; t < kTracks; ++t) {
        const LanePtr& a = lanes[size_t(t)];
        const LanePtr& b = o.lanes[size_t(t)];
        if (a == b) continue;
        if (!(lane(t) == o.lane(t))) return false;
    }
    return true;
}

bool Pattern::migrateTempo() {
    bool any = false;
    for (int t = 0; t < kTracks; ++t) {
        if (!lanes[size_t(t)]) continue;
        const Lane& l = lane(t);
        bool has = false;
        for (int s = 0; s < kSteps && !has; ++s) has = l.cmdAt(ymengine::SeqCmd::Tempo, s) != 0;
        if (!has) continue;
        Lane& e = edit(t);
        for (int s = 0; s < kSteps; ++s) {
            const uint16_t c = e.cmdAt(ymengine::SeqCmd::Tempo, s);
            if (!c) continue;
            if (ymengine::seqCmdParam(c) >= ymengine::kTempoCmdMin) bpm[size_t(s)] = uint8_t(ymengine::seqCmdParam(c));
            e.clearCmd(ymengine::SeqCmd::Tempo, s);
        }
        any = true;
    }
    compact();
    return any;
}

bool migrateArpFlags(ymengine::SeqLane& l) {
    if (std::none_of(l.afl.begin(), l.afl.end(), [](uint8_t f) { return f != 0; })) return false;
    using ymengine::SeqCmd;
    if (!l.afl[0]) l.setCmd(0, ymengine::seqCmd(SeqCmd::Arpeggiator, 0));
    for (int s = 0; s < kSteps; ++s) {
        const bool on = l.afl[size_t(s)] != 0, before = s > 0 && l.afl[size_t(s - 1)] != 0;
        if (on && !before) l.setCmd(s, ymengine::seqCmd(SeqCmd::Arpeggiator, 1));
        if (!on && before) l.setCmd(s, ymengine::seqCmd(SeqCmd::Arpeggiator, 0));
    }
    l.afl.fill(0);
    return true;
}

Lane withArpFlags(const Lane& l) {
    using ymengine::SeqCmd;
    Lane out = l;
    bool on = false, any = false;
    for (int s = 0; s < kSteps; ++s) {
        if (const uint16_t c = l.cmdAt(SeqCmd::Arpeggiator, s)) {
            on = ymengine::seqCmdParam(c) != 0;
            any = true;
            out.clearCmd(SeqCmd::Arpeggiator, s);
        }
        out.afl[size_t(s)] = on;
    }
    if (!any) return l;
    Lane back = out;
    migrateArpFlags(back);
    for (int s = 0; s < kSteps; ++s)
        if (back.cmdAt(SeqCmd::Arpeggiator, s) != l.cmdAt(SeqCmd::Arpeggiator, s)) return l;
    return out;
}

int absoluteNote(const Lane& l, int v, int s, int trackOctave) noexcept {
    const int p = l.vp(v, s);
    if (p < 0) return -1;
    return (octaveOf(l.vo(v, s), trackOctave) + 1) * 12 + p;
}

char bankLetter(int pattern) noexcept { return char('A' + std::clamp(pattern, 0, kPatterns - 1) / 8); }

}  // namespace stym::ymseq
