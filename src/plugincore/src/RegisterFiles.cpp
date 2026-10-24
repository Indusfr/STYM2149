// STYM2149 — plugincore: files of the register exports (M7, A5, B1 to B3, STYM2149_SPEC.md §8, D-160 to D-162)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/RegisterFiles.h"

#include <cmath>
#include <cstdio>
#include <span>

#include "plugincore/Mapping.h"
#include "plugincore/RegisterExport.h"
#include "ymio/SndhWriter.h"
#include "ymio/YmWriter.h"

namespace stym::plugin {

namespace {

using ymengine::DmaFrame;
using ymengine::Generator;
using ymengine::RegisterFrame;

// Chip clock of the ST, which the formats without a clock field assume.
constexpr ymcore::MasterClock kAtariClock{2'000'000, 1};

std::string count(uint32_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string mhz(const ymcore::MasterClock& c) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4g MHz", c.hz() / 1e6);
    return buf;
}

// Clocks are fractions num / den: compared by cross-multiplication.
bool sameClock(const ymcore::MasterClock& a, const ymcore::MasterClock& b) {
    return uint64_t(a.num) * b.den == uint64_t(b.num) * a.den;
}

// Voice frames that use a timer effect (SID, Sync Buzzer, Digidrum): VBL and YM3 cannot carry them, so they are
// frozen there (D-172).
uint32_t timerFrames(std::span<const RegisterFrame> frames) {
    uint32_t n = 0;
    for (const RegisterFrame& f : frames)
        for (const RegisterFrame::Effect& e : f.fx)
            n += e.on && (e.gen == Generator::Sid || e.gen == Generator::SyncBuzzer || e.gen == Generator::Digidrum);
    return n;
}

uint32_t dmaFrames(std::span<const DmaFrame> dma) {
    uint32_t n = 0;
    for (const DmaFrame& d : dma) n += d.on;
    return n;
}

std::array<std::span<const uint8_t>, 8> digidrums(const NonParamState& x) {
    std::array<std::span<const uint8_t>, 8> out{};
    for (size_t k = 0; k < out.size() && k < x.digidrums.size(); ++k) out[k] = x.digidrums[k];
    return out;
}

// The frames to write, from a Song capture or from a recorder take.
struct Source {
    std::span<const RegisterFrame> frames;
    std::span<const DmaFrame> dma;
    std::span<const RegisterFrame> pattern;  // SNDH of the Song: subsong 2
    std::span<const DmaFrame> patternDma;
    uint32_t frameHz = 50;
    ymcore::MasterClock clock = kAtariClock;
    bool ste = false;
    bool take = false;
};

// Writes one format, and lists for the alert what the file does not render like the engine (D-172).
RegisterFile write(RegisterFormat format, const Source& s, const ParamValues& values, const NonParamState& x,
                   const RegisterFileOptions& o) {
    RegisterFile out;
    out.frames = uint32_t(s.frames.size());
    out.frameHz = s.frameHz;
    if (s.frames.empty()) {
        out.error = "Nothing to export: no frames.";
        return out;
    }
    const bool atari = sameClock(s.clock, kAtariClock);
    const uint32_t frozen = timerFrames(s.frames), dma = dmaFrames(s.dma);
    if (format != RegisterFormat::Sndh && dma)
        out.notes.push_back("DMA sound of the STE is not in this format: " + count(dma, "frame", "frames") + ".");
    if (s.take && s.ste && format == RegisterFormat::Sndh)
        out.notes.push_back("The Recorder keeps no DMA sound: the SNDH has none.");
    switch (format) {
    case RegisterFormat::Vbl: {
        ymio::VblInfo info;
        info.frameHz = s.frameHz;
        if (s.take) {
            info.label = "recorder take";
            info.bpm = 0;
        } else {
            info.label = "song: " + count(uint32_t(x.seq.song.entries.size()), "entry", "entries");
            info.bpm = double(value(values, Global::FreeTempo));
        }
        const std::string text = ymio::vblSource(s.frames, info);
        out.bytes.assign(text.begin(), text.end());
        if (frozen) out.notes.push_back("Timer effects frozen: " + count(frozen, "voice frame", "voice frames") + ".");
        if (!atari) out.notes.push_back("Periods are for the " + mhz(s.clock) + " chip clock (the ST runs at 2 MHz).");
        break;
    }
    case RegisterFormat::Ym3: {
        out.bytes = ymio::ym3File(s.frames);
        if (frozen) out.notes.push_back("Timer effects frozen: " + count(frozen, "voice frame", "voice frames") + ".");
        if (!atari) out.notes.push_back("YM3 has no clock: players assume 2 MHz, not " + mhz(s.clock) + ".");
        if (s.frameHz != 50)
            out.notes.push_back("YM3 has no replay rate: players assume 50 Hz, not " + std::to_string(s.frameHz) + " Hz.");
        break;
    }
    case RegisterFormat::Ym6: {
        ymio::Ym6Info info;
        info.frameHz = s.frameHz;
        info.clock = s.clock;
        info.title = s.take ? "STYM2149 take" : "STYM2149 song";  // "PRISM song" of sndhBlob (D-060)
        info.comment = "Made with STYM2149";
        info.digidrums = digidrums(x);
        ymio::Ym6Report rep;
        const std::vector<uint8_t> ym6 = ymio::ym6File(s.frames, info, &rep);
        out.bytes = ymio::packLzh(o.name.empty() ? registerFileName(format, s.take) : o.name, ym6, o.dosTime);
        if (rep.droppedEffects)
            out.notes.push_back("Timer effects frozen (two per frame at most): " +
                                count(rep.droppedEffects, "voice frame", "voice frames") + ".");
        if (rep.frozenDrums)
            out.notes.push_back("Digidrums a YM player cannot resume: " +
                                count(rep.frozenDrums, "voice frame", "voice frames") + ".");
        break;
    }
    case RegisterFormat::Sndh: {
        ymio::SndhWriteInfo info;
        info.frameHz = s.frameHz;
        info.clock = s.clock;
        info.ste = s.ste && !s.take;  // a take has no DMA sound to play
        info.title = s.take ? "STYM2149 take" : "STYM2149 song";  // "PRISM song" of sndhBlob (D-060)
        info.digidrums = digidrums(x);
        for (size_t k = 0; k < info.dma.size() && k < x.dmaSamples.size(); ++k) {
            const ymengine::DmaSlot& d = x.dma.slots[k];
            info.dma[k] = {x.dmaSamples[k], d.rate, d.stereo, d.loop};
        }
        std::vector<ymio::SndhSubsong> subs = {{s.frames, s.dma}};
        if (!s.pattern.empty()) subs.push_back({s.pattern, s.patternDma});
        ymio::SndhWriteResult w = ymio::sndhFile(subs, info);
        if (!w.error.empty()) {  // no file: the notes would have nothing to describe
            out.error = w.error;
            out.notes.clear();
            return out;
        }
        out.bytes = std::move(w.data);
        const ymio::SndhReport& r = w.report;
        if (r.clampedPeriods)
            out.notes.push_back("Periods rescaled to 2 MHz and clamped: " + std::to_string(r.clampedPeriods) + ".");
        if (r.droppedEffects)
            out.notes.push_back("Timer effects without sample or timer: " +
                                count(r.droppedEffects, "voice frame", "voice frames") + ".");
        if (r.heldDrums)
            out.notes.push_back("Digidrum loops end with their sample: " +
                                count(r.heldDrums, "voice frame", "voice frames") + ".");
        if (r.dmaOverlap)
            out.notes.push_back("Overlapping DMA notes (one channel, the latest plays): " +
                                count(r.dmaOverlap, "frame", "frames") + ".");
        if (r.dmaChanges)
            out.notes.push_back("DMA pitch or volume kept from the attack: " + count(r.dmaChanges, "frame", "frames") +
                                ".");
        if (r.dmaDropped)
            out.notes.push_back("DMA notes not played (empty slot): " + count(r.dmaDropped, "frame", "frames") + ".");
        if (r.dmaTruncated)
            out.notes.push_back("DMA samples cut at 512 KB: " + std::to_string(r.dmaTruncated) + ".");
        break;
    }
    }
    return out;
}

}  // namespace

std::string registerFileName(RegisterFormat format, bool take) {
    // prism-ym2149-song.s, .ym, .sndh of the prototype (D-060).
    std::string name = take ? "stym2149-take" : "stym2149-song";
    switch (format) {
    case RegisterFormat::Vbl: return name + ".s";
    case RegisterFormat::Ym3:
    case RegisterFormat::Ym6: return name + ".ym";
    case RegisterFormat::Sndh: return name + ".sndh";
    }
    return name;
}

std::optional<RegisterFile> exportSongRegisters(RegisterFormat format, const ParamValues& values,
                                                const NonParamState& extras, const RegisterFileOptions& options,
                                                const std::atomic<bool>* cancel) {
    const std::optional<RegisterCapture> song = captureRegisters(OfflineRender::Source::Song, values, extras, cancel);
    if (!song) return std::nullopt;
    if (song->take.empty()) {  // the capture is empty outside Authentic 3ch
        RegisterFile out;
        out.error = "Register dumps carry three YM voices: switch to Authentic 3ch.";
        return out;
    }
    std::optional<RegisterCapture> pattern;
    if (format == RegisterFormat::Sndh) {  // exportSongSndh: 1 = tune, 2 = current pattern
        pattern = captureRegisters(OfflineRender::Source::Pattern, values, extras, cancel);
        if (!pattern) return std::nullopt;
    }
    Source s;
    s.frames = song->take.frames;
    s.dma = song->dma;
    if (pattern) {
        s.pattern = pattern->take.frames;
        s.patternDma = pattern->dma;
    }
    s.frameHz = song->take.frameHz;
    s.clock = {song->take.clockNum, song->take.clockDen};
    s.ste = steMachine(values);
    RegisterFile out = write(format, s, values, extras, options);
    out.truncated = song->truncated;
    return out;
}

RegisterFile exportTake(RegisterFormat format, const RegisterTake& take, const ParamValues& values,
                        const NonParamState& extras, const RegisterFileOptions& options) {
    Source s;
    s.frames = take.frames;
    s.frameHz = take.frameHz;
    s.clock = {take.clockNum, take.clockDen};
    s.ste = steMachine(values);
    s.take = true;
    return write(format, s, values, extras, options);
}

}  // namespace stym::plugin
