// STYM2149 — plugincore: MIDI import and export of the File menu (M7, A3, D-170)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/MidiFiles.h"

#include <algorithm>
#include <cmath>

#include "plugincore/Mapping.h"

namespace stym::plugin {

using ymengine::Engine;
using ymengine::Generator;
using ymengine::VoiceMode;

namespace {

// Math.round of the prototype.
int64_t jsRound(double x) noexcept { return int64_t(std::floor(x + 0.5)); }

}  // namespace

// GM_SLOT of the prototype, test for test: a note outside the table goes to a kit row of the same kind. Some tests
// can no longer match after the table lookup; they are kept as the prototype wrote them.
int gmSlot(int n) noexcept {
    for (size_t i = 0; i < kGmDrum.size(); ++i)
        if (kGmDrum[i] == n) return int(i);
    if (n <= 37) return 0;                 // low: kick
    if (n == 39 || n == 40) return 2;      // clap / snare
    if (n <= 41) return 1;
    if (n == 42 || n == 44) return n == 44 ? 5 : 3;
    if (n == 46) return 4;
    if (n <= 50) return 6;
    return 7;
}

std::vector<int> midiImportTracks(VoiceMode mode) {
    switch (mode) {
    case VoiceMode::Authentic3: return {0, 1, 2};
    case VoiceMode::Dual3: return {0, 1, 2, 4, 5, 6};
    case VoiceMode::Studio16: break;
    }
    std::vector<int> all(kTracks);
    for (int t = 0; t < kTracks; ++t) all[size_t(t)] = t;
    return all;
}

std::optional<MidiImport> importMidi(std::span<const uint8_t> file, VoiceMode mode, ymseq::Project& project) {
    const std::optional<ymio::SmfSong> m = ymio::readSmf(file);
    if (!m || m->notes.empty()) return std::nullopt;
    const double spq = m->division / 4.0;  // ticks per sixteenth note
    auto st = [spq](int64_t t) { return jsRound(double(t) / spq); };
    int64_t total = 0;
    for (const ymio::SmfNote& n : m->notes) total = std::max(total, st(n.end));
    // Enough patterns of N steps for the last note end, 1 to 32.
    const int N = project.steps();
    const int nPat = int(std::clamp<int64_t>((total + N - 1) / N, 1, ymseq::kPatterns));
    // Channels → tracks, in order of appearance in the note list (readSmf adds a note when it closes): melodic ones,
    // then channel 10.
    std::vector<int> chans;
    for (const ymio::SmfNote& n : m->notes)
        if (std::find(chans.begin(), chans.end(), n.channel) == chans.end()) chans.push_back(n.channel);
    std::vector<int> order;
    for (int c : chans)
        if (c != 9) order.push_back(c);
    for (int c : chans)
        if (c == 9) order.push_back(c);
    const std::vector<int> tracks = midiImportTracks(mode);
    if (order.size() > tracks.size()) order.resize(tracks.size());

    // The rows of each imported channel over the whole file (nPat × N steps), cut into patterns afterwards. A step
    // without a note is -1 at VOL 15.
    struct Rows {
        std::vector<int16_t> pat;
        std::vector<int8_t> oct;
        std::vector<uint8_t> tie, vol;
    };
    const size_t T = size_t(nPat) * size_t(N);
    std::vector<Rows> lanes(order.size(), Rows{std::vector<int16_t>(T, -1), std::vector<int8_t>(T, 0),
                                               std::vector<uint8_t>(T, 0), std::vector<uint8_t>(T, 15)});
    for (const ymio::SmfNote& n : m->notes) {
        const auto it = std::find(order.begin(), order.end(), n.channel);
        if (it == order.end()) continue;
        const size_t i = size_t(it - order.begin());
        const int64_t a = std::max<int64_t>(0, st(n.start)), b = std::max(a + 1, st(n.end));
        if (a >= int64_t(T)) continue;
        Rows& L = lanes[i];
        const auto s0 = size_t(a);
        if (n.channel == 9) {
            L.pat[s0] = int16_t(gmSlot(n.note));
            L.oct[s0] = 0;
        } else {
            // MIDI note = 12 × (octave + 1) + semitone; a note below octave 0 becomes the C of octave 0.
            const int oc = std::clamp(n.note / 12 - 1, 0, 9);
            L.pat[s0] = int16_t(std::clamp(n.note - (oc + 1) * 12, 0, 12));
            // Deviation (D-170): octCode; the prototype wrote oc (0: octave of the track).
            L.oct[s0] = int8_t(ymseq::octaveCode(oc, project.tracks[size_t(tracks[i])].octave));
            // The steps the note holds get a tie unless a note already starts there.
            for (int64_t s = a + 1; s < std::min<int64_t>(b, int64_t(T)); ++s)
                if (L.pat[size_t(s)] < 0) L.tie[size_t(s)] = 1;
            // Deviation (D-170): TIE also on the first step of a held note (stretchNote), otherwise it did not hold.
            if (std::min<int64_t>(b, int64_t(T)) > a + 1) L.tie[s0] = 1;
        }
        L.vol[s0] = uint8_t(std::clamp<int64_t>(jsRound(n.velocity / 127.0 * 15), 1, 15));
        // The VOL of each step of a held note applies (D-345): the velocity over the whole note.
        for (int64_t s = a + 1; s < std::min<int64_t>(b, int64_t(T)) && n.channel != 9; ++s)
            if (L.pat[size_t(s)] < 0 && L.tie[size_t(s)]) L.vol[size_t(s)] = L.vol[s0];
    }

    MidiImport r;
    r.tracks = int(order.size());
    r.patterns = nPat;
    r.steps = N;
    r.bpm = m->bpm;
    r.truncated = total > int64_t(T);
    for (size_t i = 0; i < order.size(); ++i) {
        r.on[size_t(tracks[i])] = true;
        r.drum[size_t(tracks[i])] = order[i] == 9;
    }
    // Tempo outside the range of the free tempo: carried by the Song entries (deviation, D-170).
    const int entryBpm = r.bpm == std::clamp(r.bpm, 60, 240) ? 0 : r.bpm;
    project.song.entries.clear();
    for (int k = 0; k < nPat; ++k) {
        ymseq::Pattern p;
        p.steps = N;
        for (size_t i = 0; i < order.size(); ++i) {
            const Rows& L = lanes[i];
            ymseq::Lane& l = p.edit(tracks[i]);
            for (int s = 0; s < N; ++s) {
                const size_t g = size_t(k) * size_t(N) + size_t(s);
                l.pat[size_t(s)] = L.pat[g];
                l.oct[size_t(s)] = L.oct[g];
                l.tie[size_t(s)] = L.tie[g];
                l.vol[size_t(s)] = L.vol[g];
            }
        }
        p.compact();
        project.patterns[size_t(k)] = std::move(p);
        project.used.set(size_t(k));
        project.song.entries.push_back({k, 1, entryBpm, ymseq::SongKind::Pattern, 0, false});
    }
    return r;
}

float midiImportTempo(const MidiImport& r) noexcept { return float(std::clamp(r.bpm, 60, 240)); }

std::vector<std::string> midiImportMessage(const MidiImport& r, const std::string& fileName) {
    auto plural = [](int n, const char* word) { return std::to_string(n) + ' ' + word + (n > 1 ? "s" : ""); };
    std::vector<std::string> lines = {"MIDI imported", "", fileName,
                                      plural(r.tracks, "track") + ", " + plural(r.patterns, "pattern") + " of " +
                                          std::to_string(r.steps) + " steps, " + std::to_string(r.bpm) + " BPM."};
    if (r.truncated) lines.emplace_back("The file was longer and has been truncated.");
    return lines;
}

// ------------------------------------------------------------------------------------------------------- Export

MidiExportCapture::MidiExportCapture(const ParamValues& values, const NonParamState& extras, uint32_t sampleRate)
    : rate_(std::max<uint32_t>(1, sampleRate)) {
    const VoiceMode mode = voiceMode(values);
    const bool ste = steMachine(values);
    studio_ = mode == VoiceMode::Studio16;
    for (int t = 0; t < kTracks; ++t) {
        const Generator g = effectiveGenerator(values, t);
        drum_[size_t(t)] = g == Generator::Digidrum || g == Generator::Chipdrum;
        octave_[size_t(t)] = extras.tracks[size_t(t)].seq.octave;
        // Level of an attack: VOL / 15, multiplied in 3 channels by the track level (except DMA track).
        gain_[size_t(t)] = studio_ || dmaTrack(mode, ste, t) ? 1.0 : std::clamp(toMix(values, t).level, 0.0, 1.0);
        const std::string& n = extras.trackNames[size_t(t)];
        name_[size_t(t)] = n.empty() ? "STYM2149 ch" + std::to_string(t + 1) : n;
    }
    sigNum_ = extras.global.sigNum;
    sigDen_ = extras.global.sigDen;
    bpm_ = std::max(1.0, double(value(values, Global::FreeTempo)));
    slideTo_.fill(-1);
}

// Tick of a sample: the start of the current tempo segment plus the time since, at the segment tempo.
double MidiExportCapture::tickAt(uint64_t sample) const noexcept {
    return segTick_ + (double(sample) - double(segSample_)) * bpm_ * kMidiPpq / (60.0 * rate_);
}

int MidiExportCapture::keyFor(int track, double note) const noexcept {
    if (drum_[size_t(track)]) {  // kit row: note - (octave + 1) × 12 (seqNote of the engine)
        const int row = int(jsRound(note)) - (octave_[size_t(track)] + 1) * 12;
        return kGmDrum[size_t(((row % 8) + 8) % 8)];
    }
    return int(std::clamp<int64_t>(jsRound(note), 0, 127));
}

void MidiExportCapture::start(int voice, int track, int key, int velocity, uint64_t sample) {
    Open& o = open_[size_t(voice)];
    o.on = true;
    o.track = track;
    o.key = key;
    o.velocity = std::clamp(velocity, 1, 127);
    o.tick = uint32_t(std::max<int64_t>(0, jsRound(tickAt(sample))));
    o.drumOff = -1;
    if (drum_[size_t(track)]) {  // half step (the current step: no tempo change before its end)
        const double half = segTick_ + (double(sample) + stepSamples_ / 2 - double(segSample_)) * bpm_ * kMidiPpq /
                                           (60.0 * rate_);
        o.drumOff = jsRound(half);
    }
    notes_[size_t(track)].push_back(ymio::smfNoteOn(o.tick, 0, key, o.velocity));
}

// A percussion note ends at its half step at the latest; every note lasts one tick at least.
void MidiExportCapture::close(int voice, uint64_t sample) {
    Open& o = open_[size_t(voice)];
    if (!o.on) return;
    int64_t off = jsRound(tickAt(sample));
    if (o.drumOff >= 0) off = std::min(off, o.drumOff);
    off = std::max<int64_t>(off, int64_t(o.tick) + 1);
    notes_[size_t(o.track)].push_back(ymio::smfNoteOff(uint32_t(off), 0, o.key));
    o.on = false;
}

void MidiExportCapture::closeAll(uint64_t sample) {
    for (int v = 0; v < Engine::kVoices; ++v) close(v, sample);
}

void MidiExportCapture::feed(std::span<const Engine::SeqLogRecord> log) {
    for (const Engine::SeqLogRecord& r : log) {
        const int v = r.voice;
        const bool voice = v >= 0 && v < Engine::kVoices;
        switch (r.op) {
        case '@':
            stepSamples_ = r.y;
            // The tempo meta event holds microseconds per quarter note on 3 bytes. The segment takes the tempo of the
            // rounded value, so that the ticks follow the tempo written.
            if (r.bpm > 0) {
                const auto us = uint32_t(std::clamp<int64_t>(jsRound(60e6 / r.bpm), 1, 0xFFFFFF));
                if (us != us_) {  // new segment of the tempo map
                    segTick_ = tickAt(r.sample);
                    segSample_ = r.sample;
                    us_ = us;
                    bpm_ = 60e6 / us;
                    tempo_.push_back(ymio::smfTempo(uint32_t(std::max<int64_t>(0, jsRound(segTick_))), us));
                }
            }
            break;
        case 'T':
            if (!voice || r.track < 0 || r.track >= kTracks) break;
            // A new attack ends the previous note of the voice. The logged level includes the track level in
            // 3 channels: divided by it, it gives VOL / 15 back.
            close(v, r.sample);
            slideTo_[size_t(v)] = -1;
            start(v, r.track, keyFor(r.track, r.x),
                  int(jsRound(r.y / std::max(gain_[size_t(r.track)], 1e-9) * 127)), r.sample);
            break;
        case 'K':
        case 'R':
        case 'O':
            if (!voice) break;
            close(v, r.sample);
            slideTo_[size_t(v)] = -1;
            break;
        case 'S':
            if (voice) slideTo_[size_t(v)] = int(std::clamp<int64_t>(jsRound(r.x), 0, 127));
            break;
        case 'L':  // arrival of a slide: the arrival note, at the accent logged in x (16 channels)
            if (voice && open_[size_t(v)].on && slideTo_[size_t(v)] >= 0 && !drum_[size_t(open_[size_t(v)].track)] &&
                slideTo_[size_t(v)] != open_[size_t(v)].key) {
                // In 16 channels the arrival note takes its velocity from the record's z; otherwise it keeps the
                // velocity of the note it replaces.
                const Open o = open_[size_t(v)];
                close(v, r.sample);
                start(v, o.track, slideTo_[size_t(v)], studio_ ? int(jsRound(r.x * 127)) : o.velocity, r.sample);
            }
            if (voice) slideTo_[size_t(v)] = -1;
            break;
        case 'N':  // pitch changed without attack (held arpeggio or one that returns to its base)
            if (voice && open_[size_t(v)].on && !drum_[size_t(open_[size_t(v)].track)]) {
                const Open o = open_[size_t(v)];
                const int key = int(std::clamp<int64_t>(jsRound(r.x), 0, 127));
                if (key != o.key) {
                    close(v, r.sample);
                    start(v, o.track, key, o.velocity, r.sample);
                }
            }
            break;
        case 'U':  // Note-off entry of the Song: everything is released
        case '!':  // stop
            closeAll(r.sample);
            break;
        default: break;
        }
    }
}

void MidiExportCapture::finish(uint64_t endSample) { closeAll(endSample); }

int MidiExportCapture::tracks() const noexcept {
    int n = 0;
    for (const auto& e : notes_) n += e.empty() ? 0 : 1;
    return n;
}

std::vector<uint8_t> MidiExportCapture::file() const {
    std::vector<std::vector<ymio::SmfEvent>> out;
    std::vector<ymio::SmfEvent> head;
    // SMF writes the denominator as a power of two: another denominator gets no time signature.
    if (sigDen_ > 0 && (sigDen_ & (sigDen_ - 1)) == 0) head.push_back(ymio::smfTimeSignature(0, sigNum_, sigDen_));
    if (tempo_.empty()) head.push_back(ymio::smfTempo(0, uint32_t(jsRound(60e6 / bpm_))));
    head.insert(head.end(), tempo_.begin(), tempo_.end());
    out.push_back(std::move(head));
    // Melodic tracks take channels 1 to 9 then 11 to 16 in order (the last ones share channel 16).
    int mch = 0;
    for (int t = 0; t < kTracks; ++t) {
        if (notes_[size_t(t)].empty()) continue;
        const bool drum = drum_[size_t(t)];
        const int ch = drum ? 9 : (mch == 9 ? ++mch : mch);  // channel 10: GM percussion
        if (!drum) mch = std::min(15, mch + 1);
        std::vector<ymio::SmfEvent> ev = notes_[size_t(t)];
        for (ymio::SmfEvent& e : ev) e.bytes[0] = uint8_t((e.bytes[0] & 0xF0) | ch);
        // At the same tick, note offs (0x80) come before note ons (0x90): a note repeated at once is not cut by the
        // end of the previous one.
        std::stable_sort(ev.begin(), ev.end(), [](const ymio::SmfEvent& a, const ymio::SmfEvent& b) {
            return a.tick != b.tick ? a.tick < b.tick : (a.bytes[0] & 0xF0) < (b.bytes[0] & 0xF0);
        });
        ev.insert(ev.begin(), ymio::smfTrackName(0, name_[size_t(t)]));
        out.push_back(std::move(ev));
    }
    return ymio::writeSmf(kMidiPpq, out);
}

std::optional<MidiExport> exportMidi(OfflineRender::Source source, const ParamValues& values,
                                     const NonParamState& extras, const std::atomic<bool>* cancel) {
    // The rate of the render only sets the time resolution of the note positions.
    constexpr uint32_t kRate = 48000;
    OfflineRender render(source, kRate, values, extras);
    MidiExportCapture capture(render.values(), render.extras(), kRate);
    const auto max = uint64_t(kOfflineMaxSeconds * kRate);
    MidiExport out;
    while (!render.finished()) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return std::nullopt;
        if (render.position() >= max) {
            out.truncated = true;
            break;
        }
        const uint32_t n = render.advance(uint32_t(std::min<uint64_t>(OfflineRender::kChunk, max - render.position())));
        capture.feed(render.log());
        if (n == 0 && !render.finished()) break;  // no progress: stop rather than loop
    }
    capture.finish(render.position());
    out.bytes = capture.file();
    out.tracks = capture.tracks();
    out.seconds = double(render.position()) / kRate;
    return out;
}

}  // namespace stym::plugin
