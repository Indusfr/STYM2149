// STYM2149 — ymengine: playback of the sequencer tracks
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Transcription of the sequencer of the worklet (seqTick, swingF, laneStep, laneTrigger, polyTrigger, armSlide,
// armArpSlide, reshapeSlide, laneArpTick, cndPass, the pattern queue and the MIDI clock messages) and of the Song
// walk of the page (songArm, songOnWrap, songOnPa), which moves to the audio thread. The countdowns (gate, CUT, RTG,
// track arpeggios) are in host samples, as in the prototype; what they trigger is dated at the cycle of the sample
// (at()). Deliberate deviations: D-097 to D-105 (DECISIONS.md).
#include <cmath>
#include <limits>

#include "EngineImpl.h"

namespace stym::ymengine {

namespace {
// FX row: 0 = generator of the track, then Chip Voice, SID Voice, Sync Buzzer, Chipdrum.
constexpr std::array<int, 5> kFxGen = {-1, 0, 1, 3, 4};
constexpr double kNoPan = std::numeric_limits<double>::quiet_NaN();
// Lag of the track countdowns caught up at the latest after 2^30 samples (dt of laneSample on 32 bits).
constexpr int64_t kMaxLaneLag = int64_t(1) << 30;

int64_t floorDiv(int64_t a, int64_t b) noexcept { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int64_t modPos(int64_t a, int64_t b) noexcept {
    const int64_t r = a % b;
    return r < 0 ? r + b : r;
}
// 32-bit mix (lowbias32) of the draws tied to the playback position (D-104).
uint32_t mix32(uint32_t x) noexcept {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
double xorshiftUnit(uint32_t& s) noexcept {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return double(s) / 4294967296.0;
}
// Track put back at rest (stop, track switched off); full: also the chord voices and the slides of the arpeggio
// (D-102).
void clearLane(LaneState& L, bool full) noexcept {
    L.offIn = L.cutIn = 0;
    L.arpNote = -1;
    L.tiedHold = L.slidePend = false;
    L.rtLeft = 0;
    L.vibOff = false;  // the track's vibrato again (D-254)
    L.lfoOff = L.modOff = L.arp = false;  // its LFO and table, no arpeggiator (items 142 to 144)
    L.switchLane = nullptr;
    L.envIn = 0;
    // Effects of D-338: no slide, fine tune nor shape any more; no step delayed nor cut to come.
    L.volSpeed = L.envSpeed = L.noiseSpeed = 0;
    L.fine = 0;
    L.shape = 0;
    L.offset = 0;
    L.delayIn = L.cutCmdIn = 0;
    if (full) {
        L.arpGl = L.arpGlN = L.arpGlLeft = 0;
        L.arpGlNow = 0;
        L.polyLeft.fill(0);
        L.polyOff.fill(0);
    }
}
}  // namespace

using Impl = Engine::Impl;

// Renders the slice [rSeg, rI) of the block first, so that what the caller does next applies from the current
// sample.
uint64_t Impl::at() noexcept {
    if (rOut && rSeg < rI) {
        flush(*rOut, rSeg, rI);
        rSeg = rI;
    }
    return eventCycle();
}

void Impl::seqLogPush(char op, int voice, int a, int b, double x, double y, double z, int track, double bpm) noexcept {
    if (seqLog && seqLog->size() < seqLog->capacity())
        seqLog->push_back({rStart + rI, op, int16_t(voice), a, b, x, y, z, int16_t(track), bpm});
}

// ---------------------------------------------------------------------------------------------------- Data
const SeqLane& Impl::seqLane(int track) const noexcept {
    if (!seqData || seq.pattern >= kSeqRestPattern) return emptySeqLane();  // Rest or NoteOff entry (D-158)
    const auto& l = seqData->patterns[size_t(std::clamp(seq.pattern, 0, kSeqPatterns - 1))].lanes[size_t(track)];
    return l ? *l : emptySeqLane();
}

int Impl::patternSteps(int pattern) const noexcept {
    if (pattern >= kSeqRestPattern) return std::clamp(pattern - kSeqRestPattern, 1, kSeqSteps);
    if (!seqData) return 16;
    return std::clamp(seqData->patterns[size_t(std::clamp(pattern, 0, kSeqPatterns - 1))].steps, 1, kSeqSteps);
}

// Own length of the track (polymetry), otherwise that of the pattern.
int Impl::trackLen(int track) const noexcept {
    const int plen = tracks[size_t(track)].seq.length;
    return plen > 0 ? std::min(plen, kSeqSteps) : seq.len;
}

// Absolute note of the voice (0 = root, 1-5 = chord voices) at step s, -1 if nothing (chanPayload, polyAbs).
// Drums: (octave of the track + 1) × 12 + row, OCT and TON transposing the hit (D-098).
double Impl::seqNote(const SeqLane& l, int track, int voice, int s) const noexcept {
    const int rel = voice == 0 ? l.pat[size_t(s)] : l.vpat[size_t(voice - 1)][size_t(s)];
    if (rel < 0) return -1;
    const TrackSeq& ts = tracks[size_t(track)].seq;
    if (isDrum(tracks[size_t(track)].patch.generator)) return (ts.octave + 1) * 12 + rel;
    const int code = voice == 0 ? l.oct[size_t(s)] : l.voct[size_t(voice - 1)][size_t(s)];
    return (seqOctave(code, ts.octave) + 1) * 12 + rel;
}

double Impl::seqBpm() const noexcept {
    if (global.seqClock == SeqClock::Host) return std::max(1.0, host.bpm);  // tempo of the host, Song blocks included
    if (seq.cmdBpm > 0) return seq.cmdBpm;  // Txx (D-243)
    if (songActive()) {
        const auto& e = seqData->song[size_t(std::clamp<int>(seq.songPos, 0, int(seqData->song.size()) - 1))];
        if (e.bpm > 0) return e.bpm;
    }
    return std::max(1.0, global.tempo);
}

double Impl::stepsPerBeat() const noexcept { return kStepsPerBeat[std::min<size_t>(global.resolution, kSeqResolutions - 1)]; }
int Impl::clocksPerStep() const noexcept { return kClocksPerStep[std::min<size_t>(global.resolution, kSeqResolutions - 1)]; }

bool Impl::ticksTiming() const noexcept { return global.timing == SeqTiming::Ticks && global.seqClock != SeqClock::Host; }
int Impl::lineTicks(int step) const noexcept {
    const int n = std::clamp<int>(global.grooveLength, 1, kGrooveSteps);
    return std::clamp(seq.speed, 1, kSpeedMax) + std::min<int>(global.groove[size_t(modPos(step, n))], kGrooveMax);
}
int Impl::lineClocks(int step) const noexcept { return ticksTiming() ? lineTicks(step) : clocksPerStep(); }

// Swing of the prototype: 50 % = straight, 66 % = triplet shuffle, 75 % at most; ignored with the MIDI clock.
double Impl::swingFactor(int step) const noexcept {
    if (global.seqClock == SeqClock::Midi) return 1;
    const double a = swingAmount();
    return step % 2 == 0 ? a : 2 - a;
}
double Impl::swingAmount() const noexcept { return std::clamp(global.swing, 50.0, 75.0) / 50; }

// Host sync: start of step k in quarter notes (the swing lengthens the even steps and shortens the odd ones), and
// step played at a position (-1 before the start).
double Impl::stepPpq(int64_t k) const noexcept {
    const int64_t pair = floorDiv(k, 2);
    return (double(pair) * 2 + (modPos(k, 2) ? swingAmount() : 0)) / stepsPerBeat();
}
int64_t Impl::stepAt(double ppq) const noexcept {
    if (!(ppq >= 0)) return -1;
    const double u = ppq * stepsPerBeat();
    const int64_t pair = int64_t(std::floor(u / 2));
    return 2 * pair + (u - 2.0 * double(pair) >= swingAmount() ? 1 : 0);
}

// Draw tied to the playback position (D-104): the absolute step, a track or voice index and a salt that keeps the
// uses apart (1: CND, 2: Random track arpeggio, 3: seed of the slide armed by SLD, 4: seed of a slide reshaped by
// SLC), with the seed of the engine.
uint32_t Impl::seqHash(int64_t stepAbs, int a, uint32_t salt) const noexcept {
    return mix32(uint32_t(uint64_t(stepAbs)) * 0x9E3779B1u + uint32_t(a) * 0x85EBCA77u + salt * 0xC2B2AE3Du +
                 uint32_t(config.seed));
}

// CND: the step only plays if its condition passes (current cycle of the track, or draw tied to the step: D-104).
bool Impl::cndPass(int cv, int64_t cycle, int track) const noexcept {
    const double r = double(seqHash(seq.stepAbs, track, 1)) / 4294967296.0;
    switch (cv) {
    case 1: return (cycle & 1) == 0;  // 1:2
    case 2: return (cycle & 1) == 1;  // 2:2
    case 3: return (cycle & 3) == 0;  // 1:4
    case 4: return (cycle & 3) == 3;  // 4:4
    case 5: return r < 0.25;
    case 6: return r < 0.5;
    case 7: return r < 0.75;
    default: return true;
    }
}

// Level given to the voice for a step VOL (0-15): level of the track × VOL / 15, in the order of computation of the
// prototype (the 4-bit volume depends on it in 3-channel mode); in 16-channel mode, the level of the track applies
// live (mixer) and the voice only receives the accent.
double Impl::seqLevel(int track, double vol) const noexcept {
    // A DMA track in 3-channel mode takes its level from the mixer, as in 16-channel mode (D-153).
    return (studio() || dmaTrack(track) ? 1.0 : std::clamp(tracks[size_t(track)].mix.level, 0.0, 1.0)) * vol / 15.0;
}

// --------------------------------------------------------------------------------------------- Transport
void Impl::resetSeq() noexcept {
    const int pattern = seq.pattern;
    seq = SeqState{};
    seq.pattern = pattern;
    seq.len = patternSteps(pattern);
    seq.step = seq.len - 1;
    // 48 pulses per second (24 per quarter note at 120 BPM) until the MIDI clock measures the real duration.
    seq.spt = double(std::max<uint32_t>(1, hostRate())) / 48;
    lanes.fill(LaneState{});
    laneLag = laneDue = 0;
    panStep.fill(kNoPan);
    hostPulse = kNoPulse;
    arpHalfIn = 0;
    clockChanged = false;
    click = Click{};
    clickCount = 0;
}

// Tracks put back at rest (stop); full: also the chord voices and the slides of the arpeggio (D-102).
void Impl::clearLanes(bool full) noexcept {
    lanesCatchUp();
    for (LaneState& L : lanes) clearLane(L, full);
}

// Track switched off (laneOn 1 → 0 of the prototype's chan message: killLane, offIn and arpNote cleared). Its steps
// no longer run (laneStep), so nothing would end a note held by TIE or SLD, the chord voices of several steps, the
// retriggers (RTG) or the arpeggio of the step: its sequencer voices are released and the track put back at rest,
// as on Stop. In 3 channels, the voice of the channel is released only if it plays this track.
void Impl::laneOff(int i, uint64_t c) noexcept {
    lanesCatchUp();
    forLaneVoices(i, [&](int vi, Voice& v) {
        if (v.tag == VoiceTag::Sequencer) {
            killLane(vi, false, c);
        } else if (!v.fading() && v.stage != 4) {  // chord voice (16-channel)
            relVoice(v, 64, c);
            seqLogPush('R', vi);
            touch(vi, c);
        }
    });
    clearLane(lanes[size_t(i)], true);
}

void Impl::seqStart(uint64_t c) noexcept {
    if (seq.on) return;
    restartLfoRuns(c);  // free LFOs from the start of the playback (D-279)
    const bool song = songActive();
    if (song) {
        songLoopReset();
        seq.songPos = songFirst;
        seq.pattern = songEntryPattern(songFirst);
        seq.queued = -1;
    } else if (seq.pattern >= kSeqRestPattern) {
        seq.pattern = restFallback();  // Song left on a Rest or NoteOff entry (D-158)
    }
    seq.len = patternSteps(seq.pattern);
    seq.on = true;
    seq.step = seq.len - 1;
    seq.stepAbs = -1;
    seq.phase = 1;
    seq.dur = 1;
    seq.speed = std::clamp<int>(global.speed, 1, kSpeedMax);
    seq.cmdBpm = 0;  // Play starts again from the tempo of the song (D-243)
    seq.clockCount = lineClocks(seq.step) - 1;
    for (LaneState& L : lanes) {
        L.polyDone = -1;  // start afresh on the chords
        L.vibOff = false;  // Play starts with the vibrato of the tracks (D-254)
        L.lfoOff = L.modOff = L.arp = false;  // their LFO and table, no arpeggiator (items 142 to 144)
        L.switchLane = nullptr;
    }
    if (song) songArm();
}

void Impl::seqHalt(uint64_t c) noexcept {
    if (!seq.on) return;
    lanesCatchUp();
    seq.on = false;
    if (seq.queued >= 0) applyQueue();
    killTag(VoiceTag::Sequencer, false, c);
    killTag(VoiceTag::Chord, false, c);  // D-102: the chord voices are released too
    clearLanes(true);
    for (int i = 0; i < kTracks; ++i) setStepPan(i, kNoPan, c);
    seqLogPush('!', -1);
}

void Impl::selectPattern(int pattern) noexcept {
    pattern = std::clamp(pattern, 0, kSeqPatterns - 1);
    if (!seq.on) {
        seq.pattern = pattern;
        seq.queued = -1;
        seq.len = patternSteps(pattern);
        seq.step = seq.len - 1;
        return;
    }
    seq.queued = pattern == seq.pattern ? -1 : pattern;
}

// Pattern switch at step 0 (applyPatQ): the whole pattern, all rows included (D-100).
void Impl::applyQueue() noexcept {
    seq.pattern = seq.queued;
    seq.queued = -1;
    seq.len = patternSteps(seq.pattern);
    if (seq.step >= seq.len) seq.step = 0;
    seqLogPush('Q', -1, seq.pattern);
}

// Walk of the Song (page of the prototype), on the audio thread.
// Start of the current entry: its repeat count restarts; an entry played once queues the next one at once.
void Impl::songArm() noexcept {
    const auto& song = seqData->song;
    seq.songPos = std::clamp<int>(seq.songPos, 0, int(song.size()) - 1);
    if (song[size_t(seq.songPos)].bpm > 0) seq.cmdBpm = 0;  // an entry with its own tempo starts at it (D-243)
    seq.songRep = 0;
    seq.songQueued = -1;
    seq.songWraps = false;
    if (song[size_t(seq.songPos)].repeats <= 1) songQueueNext(seq.songPos + 1);
}

void Impl::songQueue(int pos) noexcept {
    seq.queued = songEntryPattern(pos);
    seq.songQueued = pos;
}

// End of one repeat of the current entry: the next entry is queued for the start of the last repeat.
void Impl::songOnWrap() noexcept {
    const auto& song = seqData->song;
    seq.songPos = std::clamp<int>(seq.songPos, 0, int(song.size()) - 1);
    if (song[size_t(seq.songPos)].bpm > 0) seq.cmdBpm = 0;  // each repeat too
    ++seq.songRep;
    if (seq.songRep >= int(song[size_t(seq.songPos)].repeats) - 1 && seq.songQueued < 0)
        songQueueNext(seq.songPos + 1);
}

// --------------------------------------------------------------------------------- Extended Song (D-158)
// A playable Song has an entry that is not a jump.
bool Impl::songActive() const noexcept { return global.songMode && seqData && songFirst >= 0; }

// Pattern played by an entry: its own, or a pattern without tracks of the length of a Rest or NoteOff entry.
int Impl::songEntryPattern(int entry) const noexcept {
    const SeqSongEntry& e = seqData->song[size_t(entry)];
    if (e.kind == SeqSongKind::Rest || e.kind == SeqSongKind::NoteOff)
        return kSeqRestPattern + std::clamp<int>(e.arg, 1, kSeqSteps);
    return std::clamp<int>(e.pattern, 0, kSeqPatterns - 1);
}

// Steps of one repeat of the entry (a jump has none).
int Impl::songEntrySteps(int entry) const noexcept {
    return seqData->song[size_t(entry)].kind == SeqSongKind::Loop ? 0 : patternSteps(songEntryPattern(entry));
}

// First entry replayed by jump `entry`: arg entries back, without going back past the previous jump
// (no nesting); -1 if nothing is left to replay.
int Impl::songLoopTarget(int entry) const noexcept {
    const auto& song = seqData->song;
    int floor = 0;
    for (int k = entry - 1; k >= 0; --k)
        if (song[size_t(k)].kind == SeqSongKind::Loop) {
            floor = k + 1;
            break;
        }
    const int t = std::max(floor, entry - std::max<int>(1, song[size_t(entry)].arg));
    return t < entry ? t : -1;
}

// Entry played after those before `from`: the jumps met are taken (repeats times, the count restarts when they
// are passed); after the last one, the Song resumes at its restart point (wrapped). -1: no playable entry.
int Impl::songNext(int from, bool& wrapped) noexcept {
    const auto& song = seqData->song;
    const int size = int(song.size());
    int pos = from;
    // Each jump sends back at most once before a playable entry: 2 rounds are enough to find one.
    for (int guard = 0; guard < 2 * size + 2; ++guard) {
        if (pos >= size) {
            wrapped = true;
            songLoopReset();
            pos = std::clamp<int>(seqData->songRestart, 0, size - 1);
            continue;
        }
        const SeqSongEntry& e = song[size_t(pos)];
        if (e.kind != SeqSongKind::Loop) return pos;
        const int target = songLoopTarget(pos);
        if (target >= 0 && seq.loopAt != pos) {
            seq.loopAt = pos;
            seq.loopCount = 0;
        }
        if (target >= 0 && seq.loopCount < std::max<int>(1, e.repeats)) {
            ++seq.loopCount;
            pos = target;
        } else {
            if (seq.loopAt == pos) songLoopReset();
            ++pos;
        }
    }
    return -1;
}

void Impl::songQueueNext(int from) noexcept {
    bool wrapped = false;
    const int next = songNext(from, wrapped);
    if (next < 0) return;
    songQueue(next);
    seq.songWraps = wrapped;
}

// Pattern played outside the Song when the Song stopped on a Rest or NoteOff entry: that of its first Pattern
// entry, otherwise the first one.
int Impl::restFallback() const noexcept {
    if (seqData)
        for (const SeqSongEntry& e : seqData->song)
            if (e.kind == SeqSongKind::Pattern) return std::clamp<int>(e.pattern, 0, kSeqPatterns - 1);
    return 0;
}

void Impl::songLoopReset() noexcept {
    seq.loopAt = -1;
    seq.loopCount = 0;
}

// NoteOff entry: the sequencer notes (chords included) are released, as at stop.
void Impl::songNoteOff(uint64_t c) noexcept {
    killTag(VoiceTag::Sequencer, false, c);
    killTag(VoiceTag::Chord, false, c);
    clearLanes(true);
    seqLogPush('U', -1, seq.songPos);
}

// songStep: the chosen entry becomes the entry played. Its pattern replaces the pattern played at once
// (loadPattern of the prototype), the current step kept as on a change of length (seq message); during playback,
// the entry restarts at its first repeat (songArm). The pattern that the old entry had queued is dropped: the
// prototype left it to the worklet when the new entry repeated, and the Song replayed the old sequence
// (D-126).
void Impl::songJump(int entry) noexcept {
    if (!seqData || songFirst < 0) return;
    if (seq.on && global.seqClock == SeqClock::Host) return;  // the host places the Song (D-110)
    const auto& song = seqData->song;
    // A jump is not played: the playable entry that follows it, jump passed (D-158); the jumps restart from zero.
    entry = std::clamp(entry, 0, int(song.size()) - 1);
    songLoopReset();
    bool wrapped = false;
    seq.songPos = songNext(song[size_t(entry)].kind == SeqSongKind::Loop ? entry + 1 : entry, wrapped);
    songLoopReset();
    if (seq.songPos < 0) seq.songPos = songFirst;
    const int pattern = songEntryPattern(seq.songPos);
    if (!seq.on) {
        if (pattern < kSeqPatterns) selectPattern(pattern);
        return;
    }
    if (song[size_t(seq.songPos)].kind == SeqSongKind::NoteOff) songNoteOff(at());
    seq.pattern = pattern;
    seq.queued = -1;
    const int len = patternSteps(pattern);
    if (len != seq.len && seq.stepAbs >= 0) {
        const int64_t st = seq.stepAbs % seq.len, cyc = seq.stepAbs / seq.len;
        seq.stepAbs = cyc * len + std::min<int64_t>(st, len - 1);
        seq.step = int(seq.stepAbs % len);
    } else if (seq.step >= len) {
        seq.step = len - 1;
    }
    seq.len = len;
    if (global.songMode) songArm();
}

// The queued pattern has just been applied: move on in the Song. False: end of the song without loop.
bool Impl::songOnPattern(uint64_t c) noexcept {
    const int size = int(seqData->song.size());
    const bool wrapped = seq.songWraps;
    // Clamped like songArm and songOnWrap: the Song can shrink under a repeat that has queued nothing (a pattern
    // picked by hand then reaches here with the position of an entry that no longer exists).
    seq.songPos = std::clamp(seq.songQueued >= 0 ? seq.songQueued : seq.songPos, 0, size - 1);
    if (wrapped && !global.loop) {
        seq.songPos = songFirst;
        songLoopReset();
        return false;
    }
    if (seqData->song[size_t(seq.songPos)].kind == SeqSongKind::NoteOff) songNoteOff(c);
    songArm();
    return true;
}

// One global step: all the tracks advance (seqTick).
void Impl::seqTick(uint64_t c) noexcept {
    lanesCatchUp();
    // A Dxx not played yet (the host moved the next row earlier): its step plays now, before the next row.
    for (int i = 0; i < kTracks; ++i)
        if (LaneState& L = lanes[size_t(i)]; L.delayIn > 0) {
            L.delayIn = 0;
            laneStep(i, 1, c);
        }
    const bool first = seq.stepAbs < 0;
    seq.step = (seq.step + 1) % seq.len;
    ++seq.stepAbs;
    if (seq.step == 0) {
        const bool song = songActive();
        // Song: the next entry, queued at the start (songArm), waits for the end of the first one. The prototype
        // applied it from the first step and skipped the first entry.
        if (seq.queued >= 0 && !(song && first)) {
            applyQueue();
            if (song && !first && !songOnPattern(c)) {
                if (global.seqClock == SeqClock::Host) seq.parkedAt = seq.stepAbs;
                seqHalt(c);
                return;
            }
        } else if (!first) {
            if (song) songOnWrap();
            else if (!global.loop) {  // end of the pattern without loop: stop before replaying step 0
                if (global.seqClock == SeqClock::Host) seq.parkedAt = seq.stepAbs;
                seqHalt(c);
                return;
            }
        }
    }
    // Real duration of the step (swing included): gate, slides, retriggers (D-099: according to the resolution).
    // Host sync: the parity of the swing follows the position of the host (absolute step).
    // Ticks (D-149): the row lasts its replay ticks (Free run) or its pulses (MIDI clock), without swing.
    if (ticksTiming()) seqSpeedCmd();  // Sxx of the row (D-150): its duration follows the new speed
    else if (global.seqClock == SeqClock::Internal) seqTempoCmd();  // Txx of the row (D-243): likewise
    const int swingStep = global.seqClock == SeqClock::Host ? int(modPos(seq.stepAbs, 2)) : seq.step;
    const double stepSamp = global.seqClock == SeqClock::Midi ? lineClocks(seq.step) * seq.spt
                            : ticksTiming() ? double(hostRate()) * lineTicks(seq.step) / std::max<uint32_t>(1, global.vblHz)
                                            : double(hostRate()) * 60 / (seqBpm() * stepsPerBeat()) * swingFactor(swingStep);
    if (seqLog) {  // nominal tempo of the step (MIDI exports, D-170)
        const double bpm = ticksTiming() ? 60.0 * std::max<uint32_t>(1, global.vblHz) /
                                               (std::clamp(seq.speed, 1, kSpeedMax) * stepsPerBeat())
                                         : seqBpm();
        seqLogPush('@', -1, seq.step, int(seq.stepAbs), seq.pattern, stepSamp, 0, -1, bpm);
    }
    if (global.metronome) metroTick();
    const int max = trackCount();
    for (int i = 0; i < max; ++i) {
        // VIB, LFO, MOD and ARP are known before the notes of the row (their VBL frames play them from the attack).
        LaneState& L = lanes[size_t(i)];
        laneSwitches(i);
        laneLineCmds(i, c);
        // Dxx (D-338): the step of the row starts at tick xx, the rest of the row being its length; a tick beyond
        // the row is ignored.
        const uint16_t delayCmd = lineCmd(i, SeqCmd::Delay);
        const double late = double(seqCmdParam(delayCmd)) * double(hostRate()) / std::max<uint32_t>(1, global.vblHz);
        if (late > 0 && late < stepSamp) {
            L.delayIn = std::max<int64_t>(1, int64_t(std::floor(late)));
            L.delayRest = stepSamp - double(L.delayIn);
        } else {
            laneStep(i, stepSamp, c);
        }
        laneCmd(i, stepSamp, c);
    }
}

// One sample: clock (internal or host), then countdowns and arpeggios of each track (process of the
// prototype). dt: samples elapsed since the last call (more than one with Quantize to VBL, which only calls at
// frame boundaries).
void Impl::seqSample(uint32_t dt) noexcept {
    if (arpHalfIn > 0 && (arpHalfIn -= double(dt)) <= 0) {  // MIDI clock: the half pulse between two clocks
        arpHalfIn = 0;
        arpPulse(at());
    }
    if (global.seqClock == SeqClock::Host) {
        hostClock(dt);
    } else if (seq.on && global.seqClock == SeqClock::Internal) {
        // BPM: phase in steps (duration of the step: its swing factor); Ticks: phase in replay ticks.
        const bool ticks = ticksTiming();
        seq.phase += (ticks ? double(std::max<uint32_t>(1, global.vblHz)) : seqBpm() / 60 * stepsPerBeat()) /
                     double(hostRate()) * dt;
        while (seq.phase >= seq.dur) {
            seq.phase -= seq.dur;
            seqTick(at());
            if (!seq.on) break;
            seq.dur = ticks ? lineTicks(seq.step) : swingFactor(seq.step);
        }
    }
    laneLag += dt;
    if (laneLag < laneDue && laneLag < kMaxLaneLag) return;  // no countdown reaches zero, no track arpeggiates
    const uint32_t lag = uint32_t(laneLag);
    laneLag = 0;
    const int max = trackCount();
    for (int i = 0; i < max; ++i) laneSample(i, lag);
    laneDue = lanesDue();
}

// Deadline of the tracks: samples until the first countdown that reaches zero (CUT, retrigger, gate, gate of the
// chord voices), 0 if a track arpeggiates (phase and slide advance at each sample, as in the prototype; the
// release of the last note of an arpeggio depends on the render of its voice).
int64_t Impl::lanesDue() const noexcept {
    int64_t due = std::numeric_limits<int64_t>::max();
    const int max = trackCount();
    for (int i = 0; i < max; ++i) {
        const LaneState& L = lanes[size_t(i)];
        if (L.arpNote >= 0 || L.arpLastSet || (L.arpGl && L.arpGlLeft > 0)) return 0;
        if (L.cutIn > 0) due = std::min(due, L.cutIn);
        if (L.rtLeft > 0) due = std::min(due, L.rtClk);
        if (L.envIn > 0) due = std::min(due, L.envIn);
        if (L.delayIn > 0) due = std::min(due, L.delayIn);
        if (L.cutCmdIn > 0) due = std::min(due, L.cutCmdIn);
        if (L.offIn > 0) due = std::min(due, L.offIn);
        for (const int64_t p : L.polyOff)
            if (p > 0) due = std::min(due, p);
    }
    return due;
}

// Tracks up to date before they are read or changed: the samples elapsed since the last run, where no countdown
// reached zero, are counted down; the run of the current sample is forced (deadline to be reviewed).
void Impl::lanesCatchUp() noexcept {
    laneDue = 0;
    if (laneLag == 0) return;
    const uint32_t lag = uint32_t(laneLag);
    laneLag = 0;
    const int max = trackCount();
    for (int i = 0; i < max; ++i) laneSample(i, lag);
}

// Host sync, at each sample: pulses of the arpeggios, then sequencer steps at the step boundaries of the position
// of the host. A jump of the host (start, loop, move) relocates the sequencer (hostLocate).
void Impl::hostClock(uint32_t dt) noexcept {
    if (!host.playing) {
        if (seq.on) seqHalt(at());
        seq.parkedAt = -1;
        hostPulse = kNoPulse;
        return;
    }
    const double pps = std::max(1.0, host.bpm) / 60 / double(hostRate());  // quarter notes per sample
    const double ppq = host.ppq + (double(rI) + 0.5) * pps;  // a boundary falls on the nearest sample
    // Half pulses (48 per quarter note, D-289) of the arpeggios set to the clock. Start or jump: the keyboard
    // arpeggio restarts on the grid of the host (half pulse played only if the position falls on it); the tracks,
    // relocated just after by hostLocate, do not receive any.
    const int64_t q = int64_t(std::floor(ppq * 48));
    if (q != hostPulse) {
        const bool fresh = hostPulse == kNoPulse;
        if (!fresh && q > hostPulse && q - hostPulse <= int64_t(double(dt) * pps * 48) + 1) {
            // Tracks: every half pulse for an odd division, the whole pulses otherwise (D-289).
            for (int64_t j = hostPulse + 1; j <= q; ++j)
                if (arpHalves() || j % 2 == 0) arpPulse(at(), j, true, arpHalves() ? 1 : 2);
        } else {
            if (fresh) arp.idx = -1;  // start of the host: like MIDI Start
            if (ppq * 48 - double(q) < pps * 48) arpPulse(at(), q, false);
        }
        hostPulse = q;
    }
    const int64_t k = stepAt(ppq);
    if (!seq.on) {
        if (seq.parkedAt >= 0 && k >= seq.parkedAt) return;  // end without loop passed: silence
        hostLocate(k, ppq, pps);
        return;
    }
    if (k == seq.stepAbs) return;
    const int64_t ahead = k - seq.stepAbs;
    // Steps the host can have moved on in dt samples, plus one: a larger leap is a jump.
    const int64_t maxAhead = 1 + int64_t(std::ceil(double(dt) * pps * stepsPerBeat()));
    if (ahead > 0 && ahead <= maxAhead) {
        for (int64_t j = 0; j < ahead && seq.on; ++j) seqTick(at());
        return;
    }
    if (ahead == -1 && stepPpq(seq.stepAbs) - ppq < 2 * pps) return;  // jitter of the host around a boundary
    hostLocate(k, ppq, pps);
}

// Start or jump of the host: the sequencer notes are released, the state becomes that after step k - 1, and step
// k plays at once if the position is on its start (within one sample), otherwise playback resumes at the next
// step (no note begun).
void Impl::hostLocate(int64_t k, double ppq, double pps) noexcept {
    lanesCatchUp();
    const uint64_t c = at();
    if (seq.on) {
        killTag(VoiceTag::Sequencer, false, c);
        killTag(VoiceTag::Chord, false, c);
        clearLanes(true);
        for (int i = 0; i < kTracks; ++i) setStepPan(i, kNoPan, c);
    }
    for (LaneState& L : lanes) L.polyDone = -1;
    seq.parkedAt = -1;
    const bool onStart = k >= 0 && ppq - stepPpq(k) < pps;
    if (!locateTo(onStart ? k - 1 : k)) {  // Song without loop: beyond the end
        seq.on = false;
        seq.parkedAt = std::max<int64_t>(0, k);
        return;
    }
    seq.on = true;
    if (onStart) seqTick(c);
}

// State of the sequencer after absolute step last (-1: before the start): step of the pattern, or entry, repeat and
// step of the Song, with the next entry queued as the walk of the Song would have done. False: beyond the end of a
// Song without loop. Extended Song (D-158): the jumps unrolled in order, then the looped restart.
bool Impl::locateTo(int64_t last) noexcept {
    seq.stepAbs = std::max<int64_t>(last, -1);
    const bool song = songActive();
    if (!song) {  // without loop, the pattern plays once from the start of the host
        if (seq.pattern >= kSeqRestPattern) seq.pattern = restFallback();
        seq.len = patternSteps(seq.pattern);
        if (!global.loop && last >= seq.len) return false;
        seq.step = last < 0 ? seq.len - 1 : int(modPos(last, seq.len));
        return true;
    }
    const auto& entries = seqData->song;
    const int size = int(entries.size());
    songLoopReset();
    seq.songWraps = false;
    if (last < 0) {
        seq.songPos = songFirst;
        seq.pattern = songEntryPattern(songFirst);
        seq.queued = -1;
        seq.len = patternSteps(seq.pattern);
        seq.step = seq.len - 1;
        songArm();
        return true;
    }
    auto span = [&](int i) { return int64_t(songEntrySteps(i)) * std::max<int>(1, entries[size_t(i)].repeats); };
    // Steps of the entries replayed by jump i (once), 0 without a section.
    auto section = [&](int i, int target) {
        int64_t n = 0;
        for (int j = target; j < i; ++j) n += span(j);
        return n;
    };
    // Steps played before the first arrival at entry `entry` (size: the whole first pass).
    auto offset = [&](int entry) {
        int64_t n = 0;
        for (int k = 0; k < entry; ++k) {
            if (entries[size_t(k)].kind != SeqSongKind::Loop) {
                n += span(k);
                continue;
            }
            const int t = songLoopTarget(k);
            if (t >= 0) n += int64_t(std::max<int>(1, entries[size_t(k)].repeats)) * section(k, t);
        }
        return n;
    };
    const int64_t total = offset(size);
    int64_t pos = last;
    if (pos >= total) {
        if (!global.loop) return false;
        // After the first pass, the Song restarts from its restart point, jumps reset: one cycle.
        int64_t from = offset(std::clamp<int>(seqData->songRestart, 0, size - 1));
        if (total - from <= 0) from = 0;
        pos = from + (pos - total) % (total - from);
    }
    int i = 0;
    while (i < size) {
        const SeqSongEntry& e = entries[size_t(i)];
        if (e.kind == SeqSongKind::Loop) {
            const int t = songLoopTarget(i);
            const int64_t S = t >= 0 ? section(i, t) : 0, R = std::max<int>(1, e.repeats);
            if (S > 0 && pos < R * S) {  // in a repeat: jump in progress, the replayed section
                seq.loopAt = i;
                seq.loopCount = int(pos / S) + 1;
                pos %= S;
                i = t;
                continue;
            }
            pos -= R * S;
            ++i;
            continue;
        }
        if (pos < span(i)) break;
        pos -= span(i);
        ++i;
    }
    if (i >= size) return false;
    const int steps = songEntrySteps(i);
    seq.songPos = i;
    seq.pattern = songEntryPattern(i);
    seq.len = steps;
    seq.songRep = int(pos / steps);
    seq.step = int(pos % steps);
    seq.queued = seq.songQueued = -1;
    if (seq.songRep >= std::max<int>(1, entries[size_t(i)].repeats) - 1) songQueueNext(i + 1);
    return true;
}

// Metronome: one beep per beat, higher on the first beat of the bar. Beats are counted from the start of the
// pattern (prototype), or from the bar of the host in Host sync.
void Impl::metroTick() noexcept {
    const bool hostSig = global.seqClock == SeqClock::Host;
    const int num = std::clamp<int>(hostSig ? host.sigNum : global.sigNum, 1, 32);
    const int den = std::clamp<int>(hostSig ? host.sigDen : global.sigDen, 1, 32);
    // Steps per beat (a 1/den note) and position in steps from the start of the count.
    const int beat = std::max(1, int(jsRound(stepsPerBeat() * 4 / den)));
    const int64_t bar = int64_t(num) * beat;
    int64_t pos = seq.step;
    if (hostSig) pos = seq.stepAbs - (host.hasBar ? int64_t(jsRound(host.barPpq * stepsPerBeat())) : 0);
    if (modPos(pos, beat) != 0) return;
    if (clickCount < int(clickAt.size())) clickAt[size_t(clickCount++)] = {rI, modPos(pos, bar) == 0};
}

// Beeps of the metronome queued by metroTick during this block, each started at its sample (see Click).
void Impl::renderClicks(const Outputs& out, uint32_t n) noexcept {
    int next = 0;
    const double sr = double(hostRate());
    const double level = std::min<int>(global.metronomeLevel, 100) / 100.0;  // D-288
    for (uint32_t i = 0; i < n; ++i) {
        while (next < clickCount && clickAt[size_t(next)].first <= i) {
            const bool first = clickAt[size_t(next++)].second;
            click.on = true;
            click.phase = 0;
            click.inc = 2 * 3.141592653589793 * (first ? 1760 : 880) / sr;
            click.gain = first ? 0.4 : 0.24;
            click.ramp = std::max<uint32_t>(1, uint32_t(jsRound(0.045 * sr)));
            click.len = std::max<uint32_t>(click.ramp, uint32_t(jsRound(0.05 * sr)));
            click.mul = std::pow(0.0001 / click.gain, 1.0 / click.ramp);
            click.t = 0;
        }
        if (!click.on) continue;
        const float x = float(level * click.gain * std::sin(click.phase));
        if (out.click) {
            out.click[i] += x;
        } else {
            out.left[i] += x;
            out.right[i] += x;
        }
        click.phase += click.inc;
        if (click.t < click.ramp) click.gain *= click.mul;
        if (++click.t >= click.len) click.on = false;
    }
    clickCount = 0;
}

// ----------------------------------------------------------------------------------------------- Voices
// ins: instrument of the step (INS row, D-147), 0 for the sound of the track.
void Impl::laneTrigger(int i, double note, double level, int gen, double drumTr, int ins, uint64_t c) noexcept {
    const int vi = laneVoice(i);
    Voice& v = voices[size_t(vi)];
    if (!studio() && !dmaTrack(i)) {
        v.chip = vi / 3;
        v.channel = vi % 3;
    }
    const Instrument* x = instrument(ins);
    setupVoice(v, note, lanePatch(i, ins).volume, VoiceTag::Sequencer, i, level, c, laneGen(i, ins, gen), drumTr,
               x ? &x->patch : nullptr);
    seqLogPush('T', vi, int(v.gen), 0, note, level, v.drumTr, i);
    touch(vi, c);
    if (!studio()) updateMutes(c);
}

void Impl::killLane(int vi, bool hard, uint64_t c) noexcept {
    Voice& v = voices[size_t(vi)];
    if (!v.on || v.tag != VoiceTag::Sequencer || v.fading() || (!hard && v.stage == 4)) return;
    if (hard) hardFade(v, 8, c);
    else relVoice(v, 32, c);
    seqLogPush('K', vi, hard ? 1 : 0);
    touch(vi, c);
}

// MIX of the step (SRC row): 1 Tone, 2 Noise, 3 Tone+Noise with the volume mode of the note; 4 Env, 5 Tone+Env,
// 6 Noise+Env, 7 Tone+Noise+Env with the volume on the envelope (sixth DAW test, item 79, D-277). Not applicable to
// the drums, the Sync Buzzer and DMA.
void Impl::applyMix(int vi, int m, uint64_t c) noexcept {
    Voice& v = voices[size_t(vi)];
    if (!m || isDrum(v.gen) || v.gen == Generator::SyncBuzzer || v.gen == Generator::Dma) return;
    const int bits = m >= kSeqMixEnv ? m - kSeqMixEnv : m;
    v.toneOn = (bits & 1) != 0;
    v.noiseOn = (bits & 2) != 0;
    v.envVolume = m >= kSeqMixEnv || v.baseEnv;
    seqLogPush('M', vi, v.toneOn, v.noiseOn);
    touch(vi, c);
}

// On a tie: MIX of the step if it is set, otherwise back to the mix of the track (or of the sounding instrument).
void Impl::applyMixDef(int vi, int track, int ins, int m, uint64_t c) noexcept {
    Voice& v = voices[size_t(vi)];
    if (isDrum(v.gen) || v.gen == Generator::SyncBuzzer || v.gen == Generator::Dma) return;
    if (m) {
        applyMix(vi, m, c);
        return;
    }
    v.toneOn = lanePatch(track, ins).toneOn;
    v.noiseOn = lanePatch(track, ins).noiseOn;
    v.envVolume = v.baseEnv;
    seqLogPush('M', vi, v.toneOn, v.noiseOn);
    touch(vi, c);
}

// FX on a tie or a slide: change of generator without retriggering.
void Impl::liveFx(int vi, int track, int ins, int fxv, uint64_t c) noexcept {
    if (!fxv) return;
    Voice& v = voices[size_t(vi)];
    const int eff = kFxGen[size_t(std::min(fxv, 4))];
    if (eff < 0 || eff == int(Generator::Chipdrum) || eff == int(v.gen) || isDrum(v.gen) || v.gen == Generator::Dma) return;
    v.gen = Generator(eff);
    if (v.gen == Generator::SyncBuzzer) {
        v.envVolume = true;
        v.toneOn = v.noiseOn = false;
        if (v.envPeriod > 3200 && v.envTune <= 0) v.envPeriod = 32;
    } else {
        const TrackPatch& p = lanePatch(track, ins);
        v.envVolume = p.envelopeVolume;
        v.toneOn = p.toneOn;
        v.noiseOn = p.noiseOn;
        v.envPeriod = p.envPeriod;
    }
    v.sidHigh = true;
    v.wavePos = 0;
    v.shape = v.trackShape;
    primeBuzz(v);
    v.baseGen = v.gen;  // the GEN row of the sequences comes back to it (D-148)
    v.genOv = false;
    tuneEnv(v, applyPitch(v));
    startTimer(v, c);
    seqLogPush('F', vi, eff);
    touch(vi, c);
}

// -------------------------------------------------------------------------------------- Chord voices
bool Impl::polyAny(const SeqLane& l, int st) const noexcept {
    for (const auto& vp : l.vpat)
        if (vp[size_t(st)] >= 0) return true;
    return false;
}

// D-102: each chord voice lasts its steps (rows dur2-dur6), counted at the start of each step of the track.
void Impl::polyAge(int i, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    for (int k = 0; k < kSeqChordVoices; ++k) {
        if (L.polyLeft[size_t(k)] <= 0 || --L.polyLeft[size_t(k)] > 0) continue;
        const int vi = kLaneVoices + i * kSeqChordVoices + k;
        Voice& v = voices[size_t(vi)];
        if (!v.on) continue;
        relVoice(v, 64, c);
        seqLogPush('R', vi);
        touch(vi, c);
    }
}

// Voices 2 to 6 of the step (16-channel, polyphonic tracks): five voices per track, one per rank, lent to another
// rank while their own note is still held (D-281).
void Impl::polyTrigger(int i, const SeqLane& l, int st, double level, int mix, int64_t gateHold, double drumTr,
                       int ins, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    if (L.polyDone == seq.stepAbs) return;  // already handled for this step
    L.polyDone = seq.stepAbs;
    if (!studio()) return;  // 3-channel: only one channel per track
    const Instrument* x = instrument(ins);
    const TrackPatch& p = x ? x->patch : tracks[size_t(i)].patch;
    if (p.mono) return;
    const bool drum = isDrum(tracks[size_t(i)].patch.generator);
    // Deviation (D-281): the note of rank k takes the voice of rank k while it is free; when an earlier note of the
    // track still holds it (its steps are not over), it takes another free rank, so that overlapping notes all sound
    // to their end. The prototype gave each rank one voice, and the new note cut the one still held. Without a free
    // rank, the note takes its own, as before; the hits of a kit keep their rank.
    std::array<double, kSeqChordVoices> notes{};
    std::array<int, kSeqChordVoices> rank{};
    std::array<bool, kSeqChordVoices> taken{};
    for (int k = 0; k < kSeqChordVoices; ++k) {
        notes[size_t(k)] = seqNote(l, i, k + 1, st);
        rank[size_t(k)] = -1;
        if (notes[size_t(k)] >= 0 && (drum || L.polyLeft[size_t(k)] <= 0)) {  // its own rank is free
            rank[size_t(k)] = k;
            taken[size_t(k)] = true;
        }
    }
    for (int k = 0; k < kSeqChordVoices; ++k) {
        if (notes[size_t(k)] < 0 || rank[size_t(k)] >= 0) continue;
        rank[size_t(k)] = k;
        for (int j = 0; j < kSeqChordVoices; ++j)
            if (!taken[size_t(j)] && L.polyLeft[size_t(j)] <= 0) {
                rank[size_t(k)] = j;
                break;
            }
        taken[size_t(rank[size_t(k)])] = true;
    }
    for (int r = 0; r < kSeqChordVoices; ++r) {
        const double note = notes[size_t(r)];
        if (note < 0) continue;
        const int k = rank[size_t(r)];
        const int vi = kLaneVoices + i * kSeqChordVoices + k;
        Voice& v = voices[size_t(vi)];
        setupVoice(v, note, p.volume, VoiceTag::Chord, i, level, c, laneGen(i, ins, -1), drum ? drumTr : 1,
                   x ? &x->patch : nullptr);
        seqLogPush('T', vi, int(v.gen), 1, note, level, v.drumTr, i);
        touch(vi, c);
        applyMixDef(vi, i, ins, mix, c);
        // The hits of a kit play to the end; a one-step melodic voice follows the gate, otherwise lasts its steps.
        const int d = std::max(1, int(l.vdur[size_t(r)][size_t(st)]));
        L.polyLeft[size_t(k)] = drum ? 0 : d;
        L.polyOff[size_t(k)] = !drum && d == 1 ? gateHold : 0;
    }
}

// Immediate silencing of the chord voices of the track (CUT of the drums, MIDI Start).
void Impl::hardPoly(int i, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    for (int k = 0; k < kSeqChordVoices; ++k) {
        const int vi = kLaneVoices + i * kSeqChordVoices + k;
        Voice& v = voices[size_t(vi)];
        if (v.on) {
            v.on = false;
            seqLogPush('O', vi);
            touch(vi, c);
        }
        L.polyLeft[size_t(k)] = 0;
        L.polyOff[size_t(k)] = 0;
    }
}

// ------------------------------------------------------------------------------------------------ Slides
// Target of an SLD set at step st: the next note of the track, k steps away (empty steps held or slid in between).
int Impl::slideTarget(int i, const SeqLane& l, int st, int& k) const noexcept {
    const int len = trackLen(i);
    k = 1;
    while (k < len) {
        const size_t s = size_t((st + k) % len);
        if (l.pat[s] >= 0 || !(l.slide[s] || l.tie[s])) break;
        ++k;
    }
    return k < len ? int(seqNote(l, i, 0, (st + k) % len)) : -1;
}

void Impl::armSlide(int vi, int i, const SeqLane& l, int st, double stepSamp, uint64_t c) noexcept {
    if (!l.slide[size_t(st)]) return;
    int k = 1;
    const int tgt = slideTarget(i, l, st, k);
    Voice& v = voices[size_t(vi)];
    if (tgt < 0 || double(tgt) == double(v.note) + v.fine) return;
    slideTo(v, tgt, std::max(1.0, stepSamp * k), c, seqHash(seq.stepAbs, vi, 3));
    seqLogPush('S', vi, int(v.glideN), int(v.curve), tgt);
    touch(vi, c);
}

// SLD on an arpeggiated step: the base of the arpeggio slides towards the next note.
void Impl::armArpSlide(int i, const SeqLane& l, int st, double stepSamp) noexcept {
    LaneState& L = lanes[size_t(i)];
    L.arpGl = 0;
    L.arpGlNow = 0;
    L.arpGlN = 0;
    if (!l.slide[size_t(st)]) return;
    int k = 1;
    const int tgt = slideTarget(i, l, st, k);
    if (tgt < 0 || tgt == L.arpNote) return;
    const double vbl = std::max<uint32_t>(1, global.vblHz);
    const double hz = studio() ? vbl * 16 : vbl;
    L.arpGl = tgt - L.arpNote;
    L.arpGlN = std::max(1, int(jsRound(stepSamp * k / double(hostRate()) * hz)));
    L.arpGlLeft = L.arpGlN;
    L.arpGlHz = hz;
    L.arpGlP = 0;
    L.arpGlCv = l.slc[size_t(st)];
}

// Change of curve in the middle of a slide: same target, new profile from the current pitch.
void Impl::reshapeSlide(int vi, int cv) noexcept {
    Voice& v = voices[size_t(vi)];
    if (!(v.fCur > 0) || !(v.fTarget > 0)) return;
    const uint32_t left = std::max<uint32_t>(1, v.glide);
    v.f0 = v.fCur;
    v.glideN = v.glide = left;
    v.curve = SlideCurve(std::min(cv, kSlideCurves - 1));
    v.seed = seqHash(seq.stepAbs, vi, 4);
    seqLogPush('H', vi, int(left), cv);
}

// End of the slide in progress. snap: the note has arrived, it settles on its target (D-105).
void Impl::holdGlide(int vi, bool snap, uint64_t c) noexcept {
    Voice& v = voices[size_t(vi)];
    stopGlide(v, c);
    if (snap && v.fTarget > 0 && v.fCur != v.fTarget) {
        v.fCur = v.fTarget;
        v.tp = tonePeriodFor(v.fCur, clock());
        if (v.gen == Generator::Sid) v.timerHz = v.fCur * sidRate(v);
        else if (v.gen == Generator::SyncBuzzer) v.timerHz = v.fCur;
        retuneTimer(v, c);
    }
    seqLogPush('G', vi, snap ? 1 : 0);
    touch(vi, c);
}

// Panning of the step (PAN row): replaces that of the mixer for the voices of the track; NaN: that of the mixer.
void Impl::setStepPan(int i, double pan, uint64_t c) noexcept {
    const double old = panStep[size_t(i)];
    if ((std::isnan(old) && std::isnan(pan)) || old == pan) return;
    panStep[size_t(i)] = pan;
    seqLogPush('P', i, 0, 0, std::isnan(pan) ? 9 : pan);
    refreshLive();
    if (!studio() && !dmaTrack(i)) return;  // 3-channel: mono, except the DMA tracks
    for (int v = 0; v < kVoices; ++v)
        if (voices[size_t(v)].on && voices[size_t(v)].track == i) touch(v, c);
}

// --------------------------------------------------------------------------------------------- One step
// Step of track i at cycle c (stepSamp: duration of the step in samples): chords, CND, RTG and PAN, then the hits of
// a drum track or the notes, ties, slides and arpeggio of a melodic track, with their gate countdowns.
void Impl::laneStep(int i, double stepSamp, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    const TrackParams& tp = tracks[size_t(i)];
    if (!tp.mix.enabled) return;
    const SeqLane& l = seqLane(i);
    const int vi = laneVoice(i);
    const int len = trackLen(i);
    const int st = int(modPos(seq.stepAbs, len));
    const size_t s = size_t(st);
    const bool drumTrack = isDrum(tp.patch.generator);
    // INS of the step (D-147), not applicable to the drums; 00: the instrument chosen by Program Change (D-156).
    const int insStep = drumTrack ? 0 : l.ins[s] ? l.ins[s] : program[size_t(i)];
    L.rand = seqHash(seq.stepAbs, i, 2) | 1u;  // state of the Random track arpeggio (xorshift: never 0)
    if (!drumTrack) polyAge(i, c);
    double n = seqNote(l, i, 0, st);
    const double lvl = seqLevel(i, l.vol[s]);
    const double drumTr = drumTrack ? std::pow(2.0, (l.oct[s] * 12 + l.ton[s]) / 12.0) : 1.0;  // D-098
    if (n < 0 && polyAny(l, st))  // chord without a root
        polyTrigger(i, l, st, lvl, l.mix[s], std::max<int64_t>(1, int64_t(std::floor(stepSamp))), drumTr, insStep, c);
    Voice* v = &voices[size_t(vi)];
    if (n >= 0 && l.cnd[s] && !cndPass(l.cnd[s], floorDiv(seq.stepAbs, len), i)) n = -1;
    // RTG: retriggers of the step (D-101: the sound of the step, level × 0.92).
    L.rtLeft = 0;
    if (n >= 0 && l.rtg[s] > 0) {
        const int hits = l.rtg[s] + 1;
        L.rtLeft = hits - 1;
        L.rtPeriod = std::max<int64_t>(1, int64_t(std::floor(stepSamp / hits)));
        L.rtClk = L.rtPeriod;
        L.rtNote = n;
        L.rtLevel = lvl * 0.92;
        L.rtGen = drumTrack || !l.fx[s] ? -1 : kFxGen[size_t(std::min<int>(l.fx[s], 4))];
        L.rtMix = drumTrack ? 0 : l.mix[s];
        L.rtDrumTr = drumTr;
        L.rtIns = insStep;
    }
    // PAN row: v × 10 + 101 for v from −10 to +10, so −1 to 1; 0 leaves the panning of the mixer.
    setStepPan(i, l.pnr[s] ? (l.pnr[s] - 101) / 100.0 : kNoPan, c);
    // 3-channel: a Digidrum occupies the channel of the track (stolen channel); the melodic track gives up its step.
    // (In 16-channel mode, the voice of the track is its own: a Digidrum there comes from an instrument of the track,
    // D-147.)
    if (!studio() && v->on && v->gen == Generator::Digidrum && tp.patch.generator != Generator::Digidrum) {
        L.offIn = 0;
        return;
    }
    // ---- Drums (Digidrum, Chipdrum): notes = hits, CUT, VOL, OCT and TON ----
    if (drumTrack) {
        if (tp.patch.generator == Generator::Chipdrum && l.tie[s] && v->on) return;  // TIE: no retriggering
        if (l.cut[s] && n < 0 && v->on && isDrum(v->gen)) {  // CUT alone: stops the current hit
            killLane(vi, false, c);
            hardPoly(i, c);
        }
        if (n >= 0) {
            laneTrigger(i, n, lvl, -1, drumTr, 0, c);
            polyTrigger(i, l, st, lvl, l.mix[s], 0, drumTr, 0, c);
            int hold = 1;  // duration of the note in the grid (one step at least)
            if (l.tie[s])
                while (hold < len && l.tie[size_t((st + hold) % len)] && l.pat[size_t((st + hold) % len)] < 0) ++hold;
            L.cutIn = std::max<int64_t>(1, int64_t(std::floor(stepSamp * hold)));
        }
        L.offIn = 0;  // no gate: the hits play to the end
        return;
    }
    // ---- Melodic track ----
    const bool sounding = v->on && v->tag == VoiceTag::Sequencer && !v->fading() && v->stage != 4;
    // Instrument (D-147): that of the step for an attacked note; ties, slides and a note reached by a slide
    // keep that of the sounding note.
    if (n >= 0 && !(L.slidePend && sounding)) L.ins = insStep;
    if (n >= 0) L.heldNote = n;
    const bool arpOn = laneArp(i, L.ins).on;
    const double gPct = l.gat[s] ? l.gat[s] / 100.0 : tp.seq.gate;
    const int64_t gateHold = std::max<int64_t>(1, int64_t(std::floor(gPct * stepSamp)));
    const int mix = l.mix[s];
    if (n < 0) {
        if (L.slidePend && (l.slide[s] || l.tie[s]) && sounding) {  // SLD/TIE: the tie continues
            const int cv = l.slc[s];
            if (cv && v->glide > 0 && cv != int(v->curve)) reshapeSlide(vi, cv);
            if (arpOn && L.arpGl) {  // ARP = 0: root alone, ARP = 1: resume
                const bool was = L.arpHold;
                L.arpHold = !L.arp;
                if (was && !L.arpHold) {
                    L.arpIdx = -1;
                    L.arpDir = 1;
                    L.arpPhase = 1;
                }
            }
            applyMixDef(vi, i, L.ins, mix, c);
            liveFx(vi, i, L.ins, l.fx[s], c);
            forLaneVoices(i, [&](int k, Voice&) { relevel(k, i, L.ins, lvl, c); });  // VOL (D-345)
            L.arpLvl = lvl;
            return;
        }
        if (l.tie[s] && L.tiedHold && sounding) {
            applyMixDef(vi, i, L.ins, mix, c);
            liveFx(vi, i, L.ins, l.fx[s], c);
            heldStep(i, l, s, lvl, stepSamp, arpOn, c);
            return;
        }
        L.tiedHold = L.slidePend = false;
        if (!(v->on && isDrum(v->gen))) killLane(vi, false, c);  // a drum (FX row) plays to the end
        L.arpNote = -1;
        L.offIn = 0;
        return;
    }
    if (L.slidePend && sounding) {
        // The previous note carried SLD: the slide is already armed towards this one.
        const bool arpHere = L.arp && arpOn;
        const bool arpWas = L.arpNote >= 0 || L.arpGl != 0;
        if (arpHere) holdGlide(vi, false, c);  // the arpeggio imposes the pitches
        else if (arpWas) {                      // it stops: the target note starts afresh
            holdGlide(vi, false, c);
            killLane(vi, true, c);
            laneTrigger(i, n, lvl, -1, 1, L.ins, c);
            v = &voices[size_t(vi)];
        } else if (double(v->note) + v->fine != n) {  // unexpected target: short catch-up
            slideTo(*v, int(n), std::max(1.0, std::min(double(gateHold), stepSamp) * 0.85), c,
                    seqHash(seq.stepAbs, vi, 3));
            seqLogPush('S', vi, int(v->glideN), int(v->curve), n);
            touch(vi, c);
        } else {
            holdGlide(vi, true, c);
        }
        v->slcOv = l.slc[s];
        armSlide(vi, i, l, st, stepSamp, c);
        // Hold of the target note: the duration consumed by the slide does not shorten it.
        L.offIn = std::max(L.offIn, L.arp && arpOn ? std::max<int64_t>(1, int64_t(std::floor(stepSamp))) : gateHold);
        v->accent = studio() ? lvl : 1;
        if (v->hardSync) syncRestart(*v, c);  // Hard sync (D-148): the target note restarts square and timer
        seqLogPush('L', vi, 0, 0, v->accent);
        touch(vi, c);
        applyMixDef(vi, i, L.ins, mix, c);
        liveFx(vi, i, L.ins, l.fx[s], c);
        if (L.arp && arpOn) {  // the arpeggio engages on the slid note
            L.arpNote = int(n);
            L.arpIdx = -1;
            L.arpDir = 1;
            L.arpLvl = lvl;
            L.arpMix = uint8_t(mix);
            L.arpFx = -1;
            L.arpIns = L.ins;
            L.arpPhase = 0;
            L.arpClkT = 0;
            L.arpHold = false;
            armArpSlide(i, l, st, stepSamp);
        } else {
            L.arpNote = -1;
            L.arpGl = 0;
            L.arpGlNow = 0;
            L.arpGlN = 0;
            L.arpLastSet = false;
            L.arpHold = false;
        }
    } else {
        killLane(vi, false, c);
        L.arpNote = -1;
        const int fxv = std::min<int>(l.fx[s], 4);
        const int effOv = fxv ? kFxGen[size_t(fxv)] : -1;
        if (L.arp && arpOn) {  // step played by the arpeggio of the track
            L.arpNote = int(n);
            L.arpIdx = -1;
            L.arpDir = 1;
            L.arpLvl = lvl;
            L.arpMix = uint8_t(mix);
            L.arpFx = effOv;
            L.arpIns = L.ins;
            L.arpHold = false;
            armArpSlide(i, l, st, stepSamp);
            L.arpPhase = 1;  // first tick at once
            L.arpClkT = std::max<int>(1, global.arpDivision) - 1;
        } else {
            L.arpLastSet = false;
            L.arpGl = 0;
            L.arpGlNow = 0;
            L.arpGlN = 0;
            L.arpHold = false;
            laneTrigger(i, n, lvl, effOv, 1, L.ins, c);
            applyMix(vi, mix, c);
            voices[size_t(vi)].slcOv = l.slc[s];
            armSlide(vi, i, l, st, stepSamp, c);
            polyTrigger(i, l, st, lvl, mix, gateHold, 1, L.ins, c);
        }
    }
    // SLD: slide to the next note (implicit tie); TIE: tie.
    L.slidePend = l.slide[s] != 0;
    if (l.slide[s] || l.tie[s]) {
        L.tiedHold = true;
        L.offIn = 0;
    } else {
        L.tiedHold = false;
        // An ARP step fills the whole step: the arpeggio is a continuous buzz, not a gated note.
        L.offIn = L.arp && arpOn ? std::max<int64_t>(1, int64_t(std::floor(stepSamp))) : gateHold;
        if (L.arpGl && L.arpGlN > 0)  // ARP + SLD: held over the whole slide
            L.offIn = std::max(L.offIn, int64_t(std::floor(L.arpGlN / L.arpGlHz * double(hostRate()))));
    }
}

// A step that a held note crosses (TIE, eleventh DAW test, items 134 and 136, D-343, D-345): its rows act on the
// note as on an attacked one. VOL sets the level of the sounding note, RTG strikes it again 2, 3 or 4 times in the
// step (from its start), ARP starts the arpeggio of the track on it, or resumes it (an unlit ARP leaves a running
// arpeggio as it is), GAT on the last step of the note releases it at that share of the step. PAN, SRC, GEN and CRV
// were already read at each step; during a slide (SLD) only VOL acts.
void Impl::heldStep(int i, const SeqLane& l, size_t s, double level, double stepSamp, bool arpOn, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    const size_t next = size_t((int(s) + 1) % std::max(1, trackLen(i)));
    if (l.gat[s] && !(l.tie[next] && l.pat[next] < 0))
        L.offIn = std::max<int64_t>(1, int64_t(std::floor(l.gat[s] / 100.0 * stepSamp)));
    forLaneVoices(i, [&](int k, Voice&) { relevel(k, i, L.ins, level, c); });
    L.arpLvl = level;
    const int fxv = std::min<int>(l.fx[s], 4);
    if (arpOn && L.arp) {
        if (L.arpNote < 0) {  // the arpeggio starts on the held note, as on an ARP step
            L.arpNote = int(L.heldNote);
            L.arpIdx = -1;
            L.arpDir = 1;
            L.arpMix = l.mix[s];
            L.arpFx = fxv ? kFxGen[size_t(fxv)] : -1;
            L.arpIns = L.ins;
            L.arpHold = false;
            L.arpPhase = 1;  // first tick at once
            L.arpClkT = std::max<int>(1, global.arpDivision) - 1;
        } else if (L.arpHold) {
            L.arpHold = false;
            L.arpIdx = -1;
            L.arpDir = 1;
            L.arpPhase = 1;
        }
    }
    L.rtLeft = 0;
    if (l.rtg[s] > 0 && L.arpNote < 0) {
        const int hits = l.rtg[s] + 1;
        L.rtLeft = hits - 1;
        L.rtPeriod = std::max<int64_t>(1, int64_t(std::floor(stepSamp / hits)));
        L.rtClk = L.rtPeriod;
        L.rtNote = L.heldNote;
        L.rtLevel = level * 0.92;
        L.rtGen = fxv ? kFxGen[size_t(fxv)] : -1;
        L.rtMix = l.mix[s];
        L.rtDrumTr = 1;
        L.rtIns = L.ins;
        laneTrigger(i, L.heldNote, level, L.rtGen, 1, L.ins, c);  // first hit: the start of the step
        applyMix(laneVoice(i), L.rtMix, c);
    }
}

// VOL of a held step (D-345): the level of a sounding voice of track i, as setupVoice gives it at the attack (the
// 4-bit volume of the sound lowered in 3-channel mode, the audio gain in 16-channel mode and for DMA).
void Impl::relevel(int vi, int track, int ins, double level, uint64_t c) noexcept {
    Voice& v = voices[size_t(vi)];
    if (!v.on || v.fading() || v.stage == 4) return;
    if (studio() || v.gen == Generator::Dma) {
        v.accent = level;
    } else {
        v.volBase = v.vol4 = uint8_t(attenuated(lanePatch(track, ins).volume, level));
        v.digiLevel = level;
        if (v.digiLevel < 1)
            for (int k = 0; k < 16; ++k) v.digiMap[size_t(k)] = uint8_t(attenuated(k, v.digiLevel));
    }
    seqLogPush('A', vi, 0, 0, level);
    touch(vi, c);
}

// -------------------------------------------------------------------------------- Commands (CMD row, D-150)
// Command of kind k on the current row of an active track (0: none).
uint16_t Impl::lineCmd(int i, SeqCmd k) const noexcept {
    if (!tracks[size_t(i)].mix.enabled) return 0;
    return seqLane(i).cmdAt(k, int(modPos(seq.stepAbs, trackLen(i))));
}

// Switches of the row: Vxx, Lxx, Mxx and Jxx hold from their step until the next one of their kind (D-254; twelfth
// DAW test, items 142 to 144). Each pattern starts as Play does (vibrato, LFO and table of the track, no arpeggiator):
// a track that enters another pattern takes the state of the last switch of each kind before its step in it (the
// step counter of the prototype runs on from one pattern to the next). ARP flags turned into Jxx play as the flags did.
void Impl::laneSwitches(int i) noexcept {
    if (!tracks[size_t(i)].mix.enabled) return;
    LaneState& L = lanes[size_t(i)];
    const SeqLane& l = seqLane(i);
    const int s = int(modPos(seq.stepAbs, trackLen(i)));
    const bool chase = &l != L.switchLane;
    L.switchLane = &l;
    if (chase) L.vibOff = L.lfoOff = L.modOff = L.arp = false;
    for (const SeqCmd k : {SeqCmd::Vibrato, SeqCmd::Lfo, SeqCmd::Mod, SeqCmd::Arpeggiator}) {
        uint16_t c = l.cmdAt(k, s);
        for (int b = s - 1; chase && !c && b >= 0; --b) c = l.cmdAt(k, b);
        if (!c) continue;
        const bool on = seqCmdParam(c) != 0;
        switch (k) {
        case SeqCmd::Vibrato: L.vibOff = !on; break;
        case SeqCmd::Lfo: L.lfoOff = !on; break;
        case SeqCmd::Mod: L.modOff = !on; break;
        default: L.arp = on; break;
        }
    }
}

// Sxx: ticks per row from this row on (Ticks cadence only). Several tracks: the last one wins, as in a tracker;
// S00 has no effect.
void Impl::seqSpeedCmd() noexcept {
    const int max = trackCount();
    for (int i = 0; i < max; ++i) {
        const uint16_t cmd = lineCmd(i, SeqCmd::Speed);
        if (seqCmdParam(cmd) == 0) continue;
        seq.speed = std::min(seqCmdParam(cmd), kSpeedMax);
        seqLogPush('V', -1, seq.speed);
    }
}

// BPM row of the pattern (Txx of the tracks until the twelfth DAW test, item 150; D-243): tempo in BPM from this step
// on (BPM cadence, internal clock), until the next one, a Song entry with its own tempo or a change of the song's
// tempo; below 32, no effect.
void Impl::seqTempoCmd() noexcept {
    if (!seqData || seq.pattern >= kSeqRestPattern) return;
    const SeqPattern& p = seqData->patterns[size_t(std::clamp(seq.pattern, 0, kSeqPatterns - 1))];
    if (const int bpm = p.bpm[size_t(std::clamp(seq.step, 0, kSeqSteps - 1))]; bpm >= kTempoCmdMin) seq.cmdBpm = bpm;
}

// Sequencer voices that play track i: its voice, then its chord voices (16-channel).
template <class F>
void Impl::forLaneVoices(int i, F&& f) noexcept {
    auto visit = [&](int vi) {
        Voice& v = voices[size_t(vi)];
        if (v.on && v.track == i && (v.tag == VoiceTag::Sequencer || v.tag == VoiceTag::Chord)) f(vi, v);
    };
    visit(laneVoice(i));
    if (studio())
        for (int k = 0; k < kSeqChordVoices; ++k) visit(kLaneVoices + i * kSeqChordVoices + k);
}

// After the notes of the row: Rxx arms the envelope retrigger at tick xx (R00: at once; a tick beyond the row is
// ignored). Zxx plays nothing: its value goes to the next frame of the register recorder (the render is run up
// to the row: the frames before are already taken), for the SNDH export (D-150); several tracks on the same row:
// the last one wins.
void Impl::laneCmd(int i, double stepSamp, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    L.envIn = 0;
    L.cutCmdIn = 0;
    // Slides (D-338): the notes that sound slide from the next frame on.
    if (L.volSpeed || L.envSpeed || L.noiseSpeed)
        forLaneVoices(i, [&](int, Voice& v) {
            if (!isDrum(v.gen)) wakeVbl(v, c);
        });
    // Cxx (D-338): the notes of the row stop at tick xx (C00: at once); a tick beyond the row is ignored.
    if (const uint16_t cut = lineCmd(i, SeqCmd::Cut)) {
        const int xx = seqCmdParam(cut);
        const double when = double(xx) * double(hostRate()) / std::max<uint32_t>(1, global.vblHz);
        if (xx == 0) cutLine(i, c);
        else if (when < stepSamp) L.cutCmdIn = std::max<int64_t>(1, int64_t(std::floor(when)));
    }
    if (const uint16_t sync = lineCmd(i, SeqCmd::Sync)) cap.sync = int16_t(seqCmdParam(sync));
    if (const uint16_t retrig = lineCmd(i, SeqCmd::Retrig)) {
        const int xx = seqCmdParam(retrig);
        const double when = double(xx) * double(hostRate()) / std::max<uint32_t>(1, global.vblHz);
        if (xx == 0) retrigEnv(i, c);
        else if (when < stepSamp) L.envIn = std::max<int64_t>(1, int64_t(std::floor(when)));
    }
}

// Rxx: R13 rewritten for the voices of the track (the envelope restarts, without retriggering the note or its tone).
void Impl::retrigEnv(int i, uint64_t c) noexcept {
    forLaneVoices(i, [&](int vi, Voice& v) {
        v.envRetrig = true;
        seqLogPush('E', vi);
        touch(vi, c);
    });
}

// VBL frames of a voice from cycle c on (a voice without modulation has none), the position of the frames
// starting there.
void Impl::wakeVbl(Voice& v, uint64_t c) const noexcept {
    if (v.vblOn) return;
    v.vblOn = true;
    v.vblHz = std::max<uint32_t>(1, global.vblHz);
    v.vblStart = c;
    v.vblIndex = 0;
    v.nextVbl = c;
}

// Effects of D-338 read at the start of the row, before its notes: the speeds of the slides, the fine tune and the
// envelope shape (kept until the next command of their kind) and the DMA offset of the row's attacks. A new fine
// tune or shape reaches the notes that sound: the shape is written to R13, which restarts the envelope.
void Impl::laneLineCmds(int i, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    if (const uint16_t a = lineCmd(i, SeqCmd::VolSlide)) L.volSpeed = slideSpeed(a);
    if (const uint16_t b = lineCmd(i, SeqCmd::EnvSlide)) L.envSpeed = slideSpeed(b);
    if (const uint16_t n = lineCmd(i, SeqCmd::NoiseSlide)) L.noiseSpeed = slideSpeed(n);
    const uint16_t offset = lineCmd(i, SeqCmd::Offset);
    L.offset = uint32_t(seqCmdParam(offset)) * kOffsetCmdFrames;
    if (const uint16_t f = lineCmd(i, SeqCmd::Fine)) {
        const double fine = (std::min(seqCmdParam(f), kFineCmdMax) - kFineCmdCenter) / 100.0;
        L.fine = fine;
        forLaneVoices(i, [&](int vi, Voice& v) {
            if (isDrum(v.gen) || v.cmdFine == fine) return;
            const double d = fine - v.cmdFine;
            v.cmdFine = fine;
            bendVoice(v, d, c);
            touch(vi, c);
        });
    }
    if (const uint16_t e = lineCmd(i, SeqCmd::Shape)) {
        const int shape = seqCmdParam(e);
        if (shape < kShapeCmdMin || shape > kShapeCmdMax) return;
        L.shape = uint8_t(shape);
        forLaneVoices(i, [&](int vi, Voice& v) {
            if (isDrum(v.gen) || v.gen == Generator::Dma) return;
            v.trackShape = v.shape = L.shape;
            v.envRetrig = true;
            bendVoice(v, 0, c);  // a tuned envelope follows its new shape
            seqLogPush('E', vi);
            touch(vi, c);
        });
    }
}

// Cxx (D-338): the notes of the track stop at once (a drum hit too) and the row plays nothing more: no retrigger,
// no arpeggio, the next note attacks.
void Impl::cutLine(int i, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    killLane(laneVoice(i), true, c);
    hardPoly(i, c);
    L.offIn = 0;
    L.rtLeft = 0;
    L.arpNote = -1;
    L.tiedHold = L.slidePend = false;
}

// ----------------------------------------------------------------------------------------- One sample
// Track i after dt samples: its countdowns and its arpeggio (laneCount), then the cut and the delayed step of the CMD
// row (D-338), after them so that the countdowns of a step started here are not shortened by this run.
void Impl::laneSample(int i, uint32_t dt) noexcept {
    laneCount(i, dt);
    LaneState& L = lanes[size_t(i)];
    if (L.cutCmdIn > 0 && (L.cutCmdIn -= dt) <= 0) {  // Cxx
        L.cutCmdIn = 0;
        cutLine(i, at());
    }
    if (L.delayIn > 0 && (L.delayIn -= dt) <= 0) {  // Dxx: the step of the row, late
        L.delayIn = 0;
        laneStep(i, L.delayRest, at());
    }
}

// Countdowns of track i (CUT, retriggers, Rxx, gate, gates of the chord voices): each acts at the cycle of the current
// sample when it reaches zero; then the slid base and the cadence of the track arpeggio advance.
void Impl::laneCount(int i, uint32_t dt) noexcept {
    LaneState& L = lanes[size_t(i)];
    if (L.cutIn > 0 && (L.cutIn -= dt) <= 0) {  // CUT: cuts even a one-shot
        L.cutIn = 0;
        killLane(laneVoice(i), false, at());
    }
    if (L.rtLeft > 0 && (L.rtClk -= dt) <= 0) {
        const uint64_t c = at();
        laneTrigger(i, L.rtNote, L.rtLevel, L.rtGen, L.rtDrumTr, L.rtIns, c);
        L.ins = L.rtIns;
        applyMix(laneVoice(i), L.rtMix, c);
        while (L.rtLeft > 0 && L.rtClk <= 0)  // Quantize to VBL: the hits of the same frame make only one
            if (--L.rtLeft > 0) L.rtClk += L.rtPeriod;
    }
    if (L.envIn > 0 && (L.envIn -= dt) <= 0) {  // Rxx (D-150)
        L.envIn = 0;
        retrigEnv(i, at());
    }
    if (L.offIn > 0 && (L.offIn -= dt) <= 0) {
        L.offIn = 0;
        const int vi = laneVoice(i);
        if (!isDrum(voices[size_t(vi)].gen)) killLane(vi, false, at());  // a drum plays to the end
        else voices[size_t(vi)].looping = false;                          // sustain loop of a Digidrum (D-154)
        L.arpNote = -1;
    }
    for (int k = 0; k < kSeqChordVoices; ++k) {  // gate of the chord voices of a step (D-102)
        if (L.polyOff[size_t(k)] <= 0 || (L.polyOff[size_t(k)] -= dt) > 0) continue;
        L.polyOff[size_t(k)] = 0;
        const int vi = kLaneVoices + i * kSeqChordVoices + k;
        const uint64_t c = at();
        Voice& v = voices[size_t(vi)];
        if (!v.on) continue;
        relVoice(v, 64, c);
        seqLogPush('R', vi);
        touch(vi, c);
    }
    // Track arpeggio: slid base (ARP + SLD), then cadence (Hz; with the MIDI clock, by pulses).
    const Voice& lv = voices[size_t(laneVoice(i))];
    auto releasing = [&] { return L.arpNote < 0 && L.arpLastSet && lv.on && lv.stage == 4 && !lv.fading(); };
    // The end of the release is only known once the render has been run up to here (only a render can end the voice).
    const bool laneRel = releasing() && (lv.nextEvent() > pendingCycle() || (at(), releasing()));
    const bool arpRun = (L.arpNote >= 0 && (L.offIn > 0 || L.tiedHold)) || laneRel;
    if (!arpRun && !(L.arpGl && L.arpGlLeft > 0)) return;
    if (L.arpGl && L.arpGlLeft > 0) {  // moving base under the arpeggio (D-097: also with the MIDI clock)
        L.arpGlP += L.arpGlHz / double(hostRate()) * dt;
        while (L.arpGlP >= 1 && L.arpGlLeft > 0) {
            L.arpGlP -= 1;
            --L.arpGlLeft;
            const double t = 1 - double(L.arpGlLeft) / L.arpGlN;
            const int cv = std::min(L.arpGlCv, kSlideCurves - 1);
            double d = L.arpGl * slideShape(t, SlideCurve(cv), 1, global.slideTable);
            if (cv == int(SlideCurve::Chromatic)) {
                d = jsRound(d);
            } else if (cv == int(SlideCurve::ScaleLocked)) {
                // Same search as slideQuantize: the nearest degree of the scale, below first at the same distance.
                const unsigned m = global.scaleMask;
                int n2 = int(jsRound(L.arpNote + d));
                if (m) {
                    const int root = global.scaleRoot;
                    for (int q = 0; q <= 6; ++q) {
                        if (m & (1u << (((n2 - q - root) % 12 + 12) % 12))) {
                            n2 -= q;
                            break;
                        }
                        if (m & (1u << (((n2 + q - root) % 12 + 12) % 12))) {
                            n2 += q;
                            break;
                        }
                    }
                }
                d = n2 - L.arpNote;
            }
            L.arpGlNow = d;
        }
    }
    // D-097: the prototype left the whole audio block here (return) when the arpeggio was suspended.
    if (!arpRun || arpOnPulses()) return;
    const TrackArp& a = laneArp(i, L.arpIns);
    const double cadence = arpRate(a.rateHz);  // Rate, or the tempo (D-256)
    double rate = cadence;
    if (a.track && L.arpGl) {  // Track: the cadence rises with the pitch
        // In proportion to the slide, by the frequency ratio of its interval at most, and not past 200 Hz.
        const double want = std::pow(2.0, L.arpGl / 12.0);
        const double room = std::max(1.0, 200 / std::max(1.0, cadence));
        const double top = std::min(want, room);
        const double t = std::clamp(L.arpGlNow / L.arpGl, 0.0, 1.0);
        rate = cadence * (1 + (top - 1) * t);
    }
    L.arpPhase += rate / double(hostRate()) * dt;
    while (L.arpPhase >= 1) {
        L.arpPhase -= 1;
        laneArpTick(i, at());
    }
}

// One step of the track arpeggio (ARP flag): offsets of the table of the track on the note of the step.
void Impl::laneArpTick(int i, uint64_t c) noexcept {
    LaneState& L = lanes[size_t(i)];
    const TrackArp& a = laneArp(i, L.arpIns);  // arpeggio of the track, or of the instrument of the step (D-147)
    const int vi = laneVoice(i);
    const bool relL = L.arpNote < 0 && voices[size_t(vi)].on && voices[size_t(vi)].stage == 4 &&
                      !voices[size_t(vi)].fading();
    if (L.arpNote < 0 && !(relL && L.arpLastSet)) return;
    std::array<int, kArpSteps * 3> list{};
    int n = 0;
    const int octN = a.mode == ArpMode::Table ? 1 : std::clamp<int>(a.octaves, 1, 3);  // Table: literal
    const int tlen = std::clamp<int>(a.length, 1, kArpSteps);
    for (int o = 0; o < octN; ++o)
        for (int k = 0; k < tlen; ++k) list[size_t(n++)] = a.table[size_t(k)] + 12 * o;
    // Land: at the last tick of a slide, go back to the root to join the target.
    if (a.land && L.arpGl && L.arpGlN > 0 && !L.arpHold &&
        L.arpGlLeft <= L.arpGlHz / std::max(1.0, arpRate(a.rateHz))) {
        const double bl = (L.arpNote >= 0 ? L.arpNote : L.arpLastN) + L.arpGlNow;
        if (voices[size_t(vi)].on) {
            retune(voices[size_t(vi)], bl, c);
            seqLogPush('N', vi, 0, 0, bl);
            touch(vi, c);
        }
        L.arpIdx = -1;
        return;
    }
    if (L.arpHold) {  // suspended: held, no retriggering
        L.arpIdx = -1;
        const double bh = (L.arpNote >= 0 ? L.arpNote : L.arpLastN) + (L.arpGl ? L.arpGlNow : 0);
        if (voices[size_t(vi)].on) {
            retune(voices[size_t(vi)], bh, c);
            seqLogPush('N', vi, 0, 0, bh);
            touch(vi, c);
        }
        return;
    }
    int off = 0;
    switch (a.mode) {
    case ArpMode::Random: off = list[size_t(int(xorshiftUnit(L.rand) * n))]; break;
    case ArpMode::Down:
        L.arpIdx = (L.arpIdx + 1) % n;
        off = list[size_t(n - 1 - L.arpIdx)];
        break;
    case ArpMode::UpDown:
        if (n < 2) {
            L.arpIdx = 0;
            off = list[0];
        } else {
            L.arpIdx += L.arpDir;
            if (L.arpIdx >= n) {
                L.arpIdx = n - 2;
                L.arpDir = -1;
            }
            if (L.arpIdx < 0) {
                L.arpIdx = 1;
                L.arpDir = 1;
            }
            off = list[size_t(L.arpIdx)];
        }
        break;
    default:  // Up, Order, Table
        L.arpIdx = (L.arpIdx + 1) % n;
        off = list[size_t(L.arpIdx)];
        break;
    }
    double root = L.arpNote >= 0 ? L.arpNote : L.arpLastN;
    L.arpLastN = root;
    L.arpLastSet = true;
    if (L.arpGl && L.arpGlN > 0) root += L.arpGlNow;  // glissando under the arpeggio
    if (relL) {  // release: the pitch alone
        retune(voices[size_t(vi)], root + off, c);
        seqLogPush('N', vi, 0, 0, root + off);
        touch(vi, c);
        return;
    }
    // The envelope continues from one note of the arpeggio to the next (grabEnv / putEnv), the modulation too.
    const Voice& was = voices[size_t(vi)];
    const bool haveEnv = was.on && was.adsrOn && was.stage != 4 && !was.fading();
    const uint8_t envStage = was.stage;
    const double envVal = haveEnv ? adsrAt(was, c) : 0;
    const ModCarry mod = grabMod(was, c);
    killLane(vi, true, c);
    laneTrigger(i, root + off, L.arpLvl, L.arpFx, 1, L.arpIns, c);
    Voice& nv = voices[size_t(vi)];
    bool put = false;
    if (haveEnv && nv.adsrOn) {
        enterStage(nv, envStage, envVal, c);
        put = true;
    }
    put |= putMod(nv, mod, c);
    if (put) touch(vi, c);
    applyMix(vi, L.arpMix, c);
}

// ------------------------------------------------------------------------------------------ MIDI clock
// One half pulse (48 per quarter note, D-289), from the MIDI clock (at each clock and halfway to the next) or the
// transport of the host, for the arpeggios set to the clock or synced to the tempo of the host (D-256): that of the
// keyboard, then those of the tracks (lanes). The keyboard arpeggio plays every `division` half pulses: since MIDI
// Start with the MIDI clock (count of the prototype), on the multiples of the division since the start of the host in
// Host sync (pulse: half pulse of the host). weight: half pulses it stands for (2 for a whole pulse).
void Impl::arpPulse(uint64_t c, int64_t pulse, bool lanesToo, int weight) noexcept {
    if (!arpOnPulses()) return;
    const int div = std::max<int>(1, global.arpDivision);
    if (arpEnabled() && heldCount > 0 &&
        (pulse != kNoPulse ? modPos(pulse, div) == 0 : (arp.clockTicks += weight) >= div)) {
        arp.clockTicks = 0;
        arpTick(c);
    }
    if (!lanesToo) return;
    lanesCatchUp();
    const int max = trackCount();
    for (int i = 0; i < max; ++i) {
        LaneState& L = lanes[size_t(i)];
        if (L.arpNote >= 0 && (L.offIn > 0 || L.tiedHold) && (L.arpClkT += weight) >= div) {
            L.arpClkT = 0;
            laneArpTick(i, c);
        }
    }
}

// MIDI clock pulse: its duration is measured (an interval of a second or more is ignored), then the sequencer moves
// on when it follows the MIDI clock and the row has received all its pulses.
void Impl::seqMidiClock(uint64_t c) noexcept {
    const uint64_t pos = rStart + rI;
    const uint64_t dt = pos - seq.lastClock;
    if (dt > 0 && dt < hostRate()) seq.spt = 0.9 * seq.spt + 0.1 * double(dt);  // smoothed duration of a pulse
    seq.lastClock = pos;
    if (global.seqClock == SeqClock::Midi && seq.on && seq.run && ++seq.clockCount >= lineClocks(seq.step)) {
        seq.clockCount = 0;
        seqTick(c);
    }
}

void Impl::seqMidiStart() noexcept {
    if (global.seqClock != SeqClock::Midi) return;
    seq.run = true;
    seq.step = seq.len - 1;
    seq.stepAbs = -1;
    seq.speed = std::clamp<int>(global.speed, 1, kSpeedMax);
    seq.clockCount = lineClocks(seq.step) - 1;
    const uint64_t c = at();
    lanesCatchUp();
    for (int i = 0; i < kTracks; ++i) hardPoly(i, c);
    clearLanes(false);
    for (LaneState& L : lanes) L.arpIdx = -1;
}

void Impl::seqMidiStop(uint64_t c) noexcept {
    if (global.seqClock != SeqClock::Midi) return;
    seq.run = false;
    killTag(VoiceTag::Sequencer, false, c);
    killTag(VoiceTag::Chord, false, c);
    clearLanes(true);
}

// SPP: position in sixteenth notes (6 pulses); the next pulse is the one at this position.
void Impl::seqSongPosition(int sixteenths) noexcept {
    if (global.seqClock != SeqClock::Midi) return;
    const int64_t p = int64_t(std::max(0, sixteenths)) * 6;
    if (ticksTiming()) {
        // Ticks: rows of unequal durations (groove); position searched row by row, at the current speed.
        int64_t k = 0, start = 0;  // current row and its first pulse
        const int64_t left = p - 1;  // last pulse counted before the position
        if (left < 0) {
            seq.stepAbs = -1;
            seq.step = int(modPos(-1, seq.len));
            seq.clockCount = lineClocks(seq.step) - 1;
            return;
        }
        int64_t cycle = 0;  // pulses of one pass of the pattern
        for (int st = 0; st < seq.len; ++st) cycle += lineTicks(st);
        const int64_t laps = left / cycle;
        start = laps * cycle;
        for (k = laps * seq.len;; ++k) {
            const int d = lineTicks(int(modPos(k, seq.len)));
            if (left < start + d) break;
            start += d;
        }
        seq.stepAbs = k;
        seq.step = int(modPos(k, seq.len));
        seq.clockCount = int(left - start);
        return;
    }
    const int cps = clocksPerStep();
    seq.stepAbs = floorDiv(p - 1, cps);
    seq.clockCount = int(modPos(p - 1, cps));
    seq.step = int(modPos(seq.stepAbs, seq.len));
}

}  // namespace stym::ymengine
