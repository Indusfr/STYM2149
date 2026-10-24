// STYM2149 — ymio: writing register dumps (M7, A5, B1, STYM2149_SPEC.md §8)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ymio/YmWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace stym::ymio {

using ymengine::Generator;
using ymengine::RegisterFrame;

namespace {

// Effects that a timer drives: YM6 effects, frozen in VBL and YM3. The other generators (Chipdrum included) are
// written as their recorded register values.
bool timerEffect(const RegisterFrame::Effect& e) noexcept {
    return e.on && (e.gen == Generator::Sid || e.gen == Generator::SyncBuzzer || e.gen == Generator::Digidrum);
}

void be16(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v));
}
void be32(std::vector<uint8_t>& b, uint32_t v) {
    be16(b, v >> 16);
    be16(b, v & 0xffff);
}
void cstr(std::vector<uint8_t>& b, const std::string& s) {
    for (const char c : s)
        if (c) b.push_back(uint8_t(c));
    b.push_back(0);
}

}  // namespace

std::array<uint8_t, 14> frozenFrame(const RegisterFrame& f) {
    std::array<uint8_t, 14> r = f.reg;
    bool buzzer = false, buzzerStart = false;
    for (size_t v = 0; v < 3; ++v) {
        const RegisterFrame::Effect& e = f.fx[v];
        if (!timerEffect(e)) continue;
        if (e.gen == Generator::Sid) r[8 + v] = uint8_t(e.value & 15);  // high volume, like the prototype
        if (e.gen == Generator::Digidrum) r[8 + v] = 0;                // sampled percussion: silent
        if (e.gen == Generator::SyncBuzzer) {
            buzzer = true;
            buzzerStart = buzzerStart || e.restart;
        }
    }
    // Sync Buzzer: its timer rewrites R13 constantly; frozen, R13 restarts only at the attack.
    const bool write13 = buzzer ? buzzerStart : f.r13;
    r[13] = write13 ? uint8_t(f.reg[13] & 15) : uint8_t(0xff);
    return r;
}

std::string vblSource(std::span<const RegisterFrame> frames, const VblInfo& info) {
    std::string s;
    char buf[160];
    std::snprintf(buf, sizeof buf, "; STYM2149F - VBL register dump (%u Hz)\n", unsigned(info.frameHz));
    s += buf;
    if (info.bpm > 0)
        std::snprintf(buf, sizeof buf, "; %s @ %.2f BPM -> %zu frames\n", info.label.c_str(), info.bpm, frames.size());
    else  // recorder take: unknown tempo
        std::snprintf(buf, sizeof buf, "; %s -> %zu frames\n", info.label.c_str(), frames.size());
    s += buf;
    s += "; 14 bytes/frame: R0-R13. R13 = $FF -> do not write (envelope not retriggered).\n";
    s += "; channels A/B/C = voices 1-3. Timer effects (SID/Digidrum/Sync) run faster than\n";
    s += "; the VBL and are frozen to their base state in this dump.\n";
    s += "\n";
    // The prototype's dc.w as long as the frame count fits in it (more than an hour at 50 Hz: dc.l).
    std::snprintf(buf, sizeof buf, "ym_frames:\t%s\t%zu\n", frames.size() > 0xffff ? "dc.l" : "dc.w", frames.size());
    s += buf;
    s += "ym_data:\n";
    static const char* kHex = "0123456789ABCDEF";
    for (const RegisterFrame& f : frames) {
        const std::array<uint8_t, 14> r = frozenFrame(f);
        s += "\tdc.b\t";
        for (size_t i = 0; i < r.size(); ++i) {
            if (i) s += ',';
            s += '$';
            s += kHex[r[i] >> 4];
            s += kHex[r[i] & 15];
        }
        s += '\n';
    }
    return s;
}

std::vector<uint8_t> ym3File(std::span<const RegisterFrame> frames) {
    const size_t n = frames.size();
    std::vector<uint8_t> out(4 + 14 * n);
    out[0] = 'Y';
    out[1] = 'M';
    out[2] = '3';
    out[3] = '!';
    for (size_t f = 0; f < n; ++f) {
        const std::array<uint8_t, 14> r = frozenFrame(frames[f]);
        for (size_t k = 0; k < 14; ++k) out[4 + k * n + f] = r[k];  // register by register
    }
    return out;
}

namespace {

// Effect in progress on one of the two effect slots of the YM6, as the player plays it.
struct Slot {
    int voice = -1;
    Generator gen = Generator::Chip;
    double drumEnd = 0;  // Digidrum: time (s) at which the player stops the timer, counted from the start of the song
};

// YM6 effect codes (bits 6-7 of R1 / R3): 00 SID, 01 Digidrum, 11 Sync Buzzer.
uint8_t fxCode(Generator g) noexcept { return g == Generator::Sid ? 0x00 : g == Generator::Digidrum ? 0x40 : 0xc0; }

}  // namespace

std::vector<uint8_t> ym6File(std::span<const RegisterFrame> frames, const Ym6Info& info, Ym6Report* report) {
    Ym6Report rep;
    const size_t n = frames.size();
    const double hz = std::max<uint32_t>(1, info.frameHz);
    // Digidrums of the file: the engine slots played, then the "cuts" (one sample at a 4-bit level, which stops a
    // digidrum before its end).
    std::vector<std::vector<uint8_t>> drums;
    std::array<int, 8> drumOf{};
    drumOf.fill(-1);
    std::array<int, 16> cutOf{};
    cutOf.fill(-1);
    auto slotDrum = [&](uint8_t slot) {
        const size_t k = slot & 7;
        if (drumOf[k] < 0) {
            drumOf[k] = int(drums.size());
            drums.emplace_back(info.digidrums[k].begin(), info.digidrums[k].end());
        }
        return drumOf[k];
    };
    auto cutDrum = [&](uint8_t level) {
        const size_t k = level & 15;
        if (cutOf[k] < 0) {
            cutOf[k] = int(drums.size());
            drums.push_back({uint8_t(k)});
        }
        return cutOf[k];
    };

    std::vector<std::array<uint8_t, 16>> out(n);
    std::array<Slot, 2> slots{};
    struct Code {
        int voice = -1;
        Generator gen = Generator::Chip;
        ymengine::MfpSetting timer{};
    };
    // Length of a digidrum in the player: one sample per interrupt, timer stopped at the one after the last.
    auto drumLength = [](const ymengine::MfpSetting& t, size_t samples) {
        return t.running() ? double(samples + 1) / t.hz() : 0.0;
    };
    // Per frame, the two effect slots are given out as the player will play them: a slot stays with its channel while
    // the effect goes on (a digidrum until the player reaches its end), then new effects take the free slots in
    // channel order. `codes` is what the frame writes into R1/R3, R6/R8 and R14/R15.
    for (size_t f = 0; f < n; ++f) {
        const RegisterFrame& fr = frames[f];
        const double now = double(f) / hz;
        const std::array<uint8_t, 14> frozen = frozenFrame(fr);
        // Volume registers R8-R10. A coded effect puts its value there: SID high volume, Sync Buzzer R13 shape, or
        // number of the digidrum in the file (5 bits).
        std::array<uint8_t, 3> vol{frozen[8], frozen[9], frozen[10]};
        std::array<Code, 2> codes{};
        std::array<bool, 3> done{false, false, false};  // channel settled (coded, continued or cut)
        auto code = [&](int s, int voice, Generator gen, uint8_t value, const ymengine::MfpSetting& t) {
            codes[size_t(s)] = {voice, gen, t};
            vol[size_t(voice)] = uint8_t(value & (gen == Generator::Digidrum ? 31 : 15));
            done[size_t(voice)] = true;
        };
        // Digidrums finished in the player.
        for (Slot& sl : slots)
            if (sl.voice >= 0 && sl.gen == Generator::Digidrum && now > sl.drumEnd) sl.voice = -1;

        // 1. Channels that already have a slot.
        for (int s = 0; s < 2; ++s) {
            Slot& sl = slots[size_t(s)];
            if (sl.voice < 0) continue;
            const int v = sl.voice;
            const RegisterFrame::Effect& e = fr.fx[size_t(v)];
            if (timerEffect(e) && e.gen == Generator::Digidrum && sl.gen == Generator::Digidrum && !e.restart) {
                vol[size_t(v)] = 0;  // the digidrum continues: the player does not touch the channel volume
                done[size_t(v)] = true;
            } else if (timerEffect(e)) {
                const uint8_t value = e.gen == Generator::Digidrum ? uint8_t(slotDrum(e.value)) : e.value;
                code(s, v, e.gen, value, e.timer);
                sl.gen = e.gen;
                if (e.gen == Generator::Digidrum)
                    sl.drumEnd = now + drumLength(e.timer, info.digidrums[e.value & 7].size());
            } else if (sl.gen == Generator::Digidrum) {
                // The engine stopped the digidrum before its end: a sample at the channel level replaces it and the
                // timer stops at once (one timer period of deviation). A channel in envelope mode (bit 4 of its volume
                // register) gives level 0. MfpSetting{1, 1}: the shortest timer period.
                const uint8_t reg = fr.reg[size_t(8 + v)];
                const uint8_t level = (reg & 0x10) ? 0 : uint8_t(reg & 15);
                code(s, v, Generator::Digidrum, uint8_t(cutDrum(level)), ymengine::MfpSetting{1, 1});
                sl.voice = -1;
                ++rep.cutDrums;
            } else {
                sl.voice = -1;  // without a code, the player stops the timer
            }
        }
        // 2. New effects, in channel order, on a free slot.
        for (int v = 0; v < 3; ++v) {
            const RegisterFrame::Effect& e = fr.fx[size_t(v)];
            if (!timerEffect(e) || done[size_t(v)]) continue;
            if (e.gen == Generator::Digidrum && !e.restart) {
                ++rep.frozenDrums;  // a digidrum only starts at the beginning of its sample: frozen
                continue;
            }
            int free = -1;
            for (int s = 0; s < 2 && free < 0; ++s)
                if (slots[size_t(s)].voice < 0 && codes[size_t(s)].voice < 0) free = s;
            if (free < 0) {
                ++rep.droppedEffects;
                continue;
            }
            const uint8_t value = e.gen == Generator::Digidrum ? uint8_t(slotDrum(e.value)) : e.value;
            code(free, v, e.gen, value, e.timer);
            Slot& sl = slots[size_t(free)];
            sl.voice = v;
            sl.gen = e.gen;
            if (e.gen == Generator::Digidrum) sl.drumEnd = now + drumLength(e.timer, info.digidrums[e.value & 7].size());
        }

        std::array<uint8_t, 16>& r = out[f];
        for (size_t k = 0; k < 14; ++k) r[k] = frozen[k];
        for (size_t v = 0; v < 3; ++v) r[8 + v] = vol[v];
        r[14] = r[15] = 0;
        // Effect k: code and channel in R1 (R3), prescaler in bits 5-7 of R6 (R8), data in R14 (R15).
        for (size_t s = 0; s < 2; ++s) {
            const Code& c = codes[s];
            if (c.voice < 0) continue;
            const size_t codeReg = s == 0 ? 1 : 3, predivReg = s == 0 ? 6 : 8;
            r[codeReg] = uint8_t((r[codeReg] & 0x0f) | fxCode(c.gen) | uint8_t((c.voice + 1) << 4));
            r[predivReg] = uint8_t((r[predivReg] & 0x1f) | uint8_t((c.timer.prescaler & 7) << 5));
            r[14 + s] = uint8_t(c.timer.data & 0xff);  // 256 is written 0
        }
    }

    // Header, big-endian: frame count, attributes, digidrum count, master clock (Hz), frame rate (Hz), loop frame,
    // size of additional data; then each digidrum (32-bit length and its values), title, author and comment
    // (zero-terminated), the frames register by register (interleaved), and "End!".
    std::vector<uint8_t> b;
    const char* id = "YM6!LeOnArD!";
    b.insert(b.end(), id, id + 12);
    be32(b, uint32_t(n));
    be32(b, 1u | 4u);  // interleaved frames, digidrums as 4-bit values
    be16(b, uint32_t(drums.size()));
    be32(b, uint32_t(std::llround(info.clock.hz())));
    be16(b, info.frameHz);
    be32(b, n ? std::min<uint32_t>(info.loopFrame, uint32_t(n - 1)) : 0);
    be16(b, 0);  // additional data
    for (const std::vector<uint8_t>& d : drums) {
        be32(b, uint32_t(d.size()));
        b.insert(b.end(), d.begin(), d.end());
    }
    cstr(b, info.title);
    cstr(b, info.author);
    cstr(b, info.comment);
    const size_t data = b.size();
    b.resize(data + 16 * n);
    for (size_t k = 0; k < 16; ++k)
        for (size_t f = 0; f < n; ++f) b[data + k * n + f] = out[f][k];
    const char* end = "End!";
    b.insert(b.end(), end, end + 4);
    if (report) *report = rep;
    return b;
}

}  // namespace stym::ymio
