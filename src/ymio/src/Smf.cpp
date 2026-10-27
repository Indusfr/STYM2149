// STYM2149 — ymio: standard MIDI files (SMF), reading formats 0 and 1 and writing format 1
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/Smf.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace stym::ymio {

namespace {

uint32_t be32(std::span<const uint8_t> b, size_t at) {
    return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | uint32_t(b[at + 3]);
}

bool tag(std::span<const uint8_t> b, size_t at, const char (&t)[5]) {
    return at + 4 <= b.size() && b[at] == uint8_t(t[0]) && b[at + 1] == uint8_t(t[1]) && b[at + 2] == uint8_t(t[2]) &&
           b[at + 3] == uint8_t(t[3]);
}

// Reader of an MTrk chunk: every read beyond the end of the chunk fails (the track stops there).
struct Track {
    std::span<const uint8_t> b;
    size_t q, end;

    bool byte(uint8_t& v) {
        if (q >= end) return false;
        v = b[q++];
        return true;
    }
    // Variable-length quantity, computed like the prototype (32-bit integer).
    bool varLen(uint32_t& v) {
        v = 0;
        uint8_t c = 0;
        do {
            if (!byte(c)) return false;
            v = (v << 7) | (c & 127u);
        } while (c & 128u);
        return true;
    }
    void skip(uint32_t n) { q = n >= end - q ? end : q + n; }
};

}  // namespace

// MThd chunk: its length at byte 4, track count at 10, division at 12 (big-endian); the MTrk chunks follow it.
std::optional<SmfSong> readSmf(std::span<const uint8_t> bytes) {
    if (bytes.size() < 14 || !tag(bytes, 0, "MThd")) return std::nullopt;
    const int ntrk = bytes[10] << 8 | bytes[11];
    const int div = bytes[12] << 8 | bytes[13];
    if (div & 0x8000) return std::nullopt;  // SMPTE: out of scope
    if (div == 0) return std::nullopt;      // the prototype divided by zero
    SmfSong song;
    song.division = div;
    uint64_t p = 8 + uint64_t(be32(bytes, 4));
    for (int t = 0; t < ntrk && p + 8 <= bytes.size(); ++t) {
        if (!tag(bytes, size_t(p), "MTrk")) break;
        const uint64_t len = be32(bytes, size_t(p) + 4);
        Track tr{bytes, size_t(p) + 8, size_t(std::min<uint64_t>(p + 8 + len, bytes.size()))};
        int64_t tick = 0;
        uint8_t run = 0;
        std::map<int, std::pair<int64_t, int>> open;  // notes in progress: channel × 128 + note → (tick, velocity)
        while (tr.q < tr.end) {
            uint32_t delta = 0;
            if (!tr.varLen(delta)) break;
            tick += int32_t(delta);
            if (tr.q >= tr.end) break;
            // A status byte (bit 7 set) is consumed and becomes the running status; a data byte reuses the last one.
            uint8_t st = bytes[tr.q];
            if (st & 128u) {
                ++tr.q;
                run = st;
            } else {
                st = run;
            }
            const int cmd = st & 0xF0, ch = st & 0x0F;
            if (cmd == 0x90 || cmd == 0x80) {
                uint8_t note = 0, vel = 0;
                if (!tr.byte(note) || !tr.byte(vel)) break;
                note &= 127u;
                vel &= 127u;
                const int key = ch * 128 + note;
                if (cmd == 0x90 && vel > 0) {
                    open[key] = {tick, vel};
                } else if (auto it = open.find(key); it != open.end()) {
                    if (song.notes.size() < kSmfMaxNotes)
                        song.notes.push_back({ch, note, it->second.second, it->second.first, tick});
                    else
                        song.notesDropped = true;
                    open.erase(it);
                }
            } else if (cmd == 0xA0 || cmd == 0xB0 || cmd == 0xE0) {
                tr.skip(2);
            } else if (cmd == 0xC0 || cmd == 0xD0) {
                tr.skip(1);
            } else if (st == 0xFF) {
                uint8_t meta = 0;
                uint32_t ml = 0;
                if (!tr.byte(meta) || !tr.varLen(ml)) break;
                if (meta == 0x51 && !song.tempo && ml >= 3 && tr.end - tr.q >= 3) {
                    const uint32_t us = uint32_t(bytes[tr.q]) << 16 | uint32_t(bytes[tr.q + 1]) << 8 | bytes[tr.q + 2];
                    if (us > 0) {
                        song.bpm = std::clamp(int(std::floor(60000000.0 / us + 0.5)), 20, 300);  // Math.round
                        song.tempo = true;
                    }
                }
                tr.skip(ml);
            } else if (st == 0xF0 || st == 0xF7) {
                uint32_t ml = 0;
                if (!tr.varLen(ml)) break;
                tr.skip(ml);
            } else {
                tr.skip(1);
            }
        }
        p = p + 8 + len;
    }
    return song;
}

// Groups of 7 bits, most significant first; bit 7 is set on every byte but the last.
void appendVarLen(std::vector<uint8_t>& out, uint32_t v) {
    v = std::min<uint32_t>(v, 0x0FFFFFFF);
    uint8_t tmp[4];
    int n = 0;
    tmp[n++] = uint8_t(v & 0x7F);
    v >>= 7;
    while (v > 0) {
        tmp[n++] = uint8_t((v & 0x7F) | 0x80);
        v >>= 7;
    }
    while (n > 0) out.push_back(tmp[--n]);
}

// MThd: length 6, format 1, track count, division; then one MTrk chunk per track.
std::vector<uint8_t> writeSmf(uint16_t division, std::span<const std::vector<SmfEvent>> tracks) {
    std::vector<uint8_t> out = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1};
    const size_t nt = std::min<size_t>(tracks.size(), 0xFFFF);
    out.push_back(uint8_t(nt >> 8));
    out.push_back(uint8_t(nt));
    out.push_back(uint8_t(division >> 8));
    out.push_back(uint8_t(division));
    std::vector<uint8_t> by;
    for (size_t t = 0; t < nt; ++t) {
        by.clear();
        uint32_t last = 0;
        for (const SmfEvent& e : tracks[t]) {
            appendVarLen(by, e.tick > last ? e.tick - last : 0);
            by.insert(by.end(), e.bytes.begin(), e.bytes.end());
            last = std::max(last, e.tick);
        }
        by.insert(by.end(), {0x00, 0xFF, 0x2F, 0x00});  // end of track
        const auto len = uint32_t(by.size());
        out.insert(out.end(), {'M', 'T', 'r', 'k', uint8_t(len >> 24), uint8_t(len >> 16), uint8_t(len >> 8), uint8_t(len)});
        out.insert(out.end(), by.begin(), by.end());
    }
    return out;
}

SmfEvent smfTempo(uint32_t tick, uint32_t us) {
    us = std::clamp<uint32_t>(us, 1, 0xFFFFFF);
    return {tick, {0xFF, 0x51, 0x03, uint8_t(us >> 16), uint8_t(us >> 8), uint8_t(us)}};
}

SmfEvent smfTimeSignature(uint32_t tick, int numerator, int denominator) {
    int pow2 = 0;
    while ((1 << pow2) < denominator && pow2 < 7) ++pow2;
    // 24 MIDI clock pulses per metronome click, 8 thirty-second notes per quarter note.
    return {tick, {0xFF, 0x58, 0x04, uint8_t(std::clamp(numerator, 1, 255)), uint8_t(pow2), 24, 8}};
}

SmfEvent smfTrackName(uint32_t tick, const std::string& name) {
    SmfEvent e{tick, {0xFF, 0x03}};
    appendVarLen(e.bytes, uint32_t(name.size()));
    e.bytes.insert(e.bytes.end(), name.begin(), name.end());
    return e;
}

SmfEvent smfNoteOn(uint32_t tick, int channel, int note, int velocity) {
    return {tick, {uint8_t(0x90 | (channel & 15)), uint8_t(note & 127), uint8_t(std::clamp(velocity, 1, 127))}};
}

SmfEvent smfNoteOff(uint32_t tick, int channel, int note) {
    return {tick, {uint8_t(0x80 | (channel & 15)), uint8_t(note & 127), 0}};
}

}  // namespace stym::ymio
