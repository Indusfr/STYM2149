// STYM2149 — ttrakimport: TTrak modules (.TPM) played by our player (ymio/TTrakPlayer.h), compared tick by tick with
// the replay routine of TTrak running in the emulated Atari.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The routine is not part of STYM2149: give the path of TTrak 1.04's program (TTRAK.PRG) with --prg. The tool takes
// the replay routine from it in memory and makes the SNDH that TTrak's "save SNDH" makes (routine, then the module),
// runs it, and compares each tick the registers that the routine computed (its buffer of R0-R12) and wrote to the chip,
// its R13 writes, and its variables (song and channels) with ours. Our player then plays with the routine's own tables
// (period, MFP and wow), read from the program, so that the comparison is exact; the tool also tells whether our
// period and MFP tables give the same values.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "ImportCheck.h"
#include "ReadFile.h"
#include "plugincore/TTrakExport.h"
#include "plugincore/TTrakImport.h"
#include "ymengine/Mfp68901.h"
#include "ymengine/Pitch.h"
#include "ymio/Sndh.h"
#include "ymio/TTrak.h"
#include "ymio/TTrakPlayer.h"

using namespace stym;

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: ttrakimport [options] module.tpm\n"
                 "       ttrakimport --prg TTRAK.PRG --fuzz N [options] instrument.tpi... [sample.4bi...]\n"
                 "  --prg FILE      TTrak 1.04's program (TTRAK.PRG): compare with its replay routine\n"
                 "  --ticks N       ticks compared (default: the song once through, at most 30000)\n"
                 "  --dump A B      our ticks A to B\n"
                 "  --diff A B      ticks A to B where they differ: registers and variables\n"
                 "  --our-tables    with --prg: play with our tables instead of the routine's\n"
                 "  --import        import the module as patterns (File > Import), play them back and compare\n"
                 "                  with our player\n"
                 "  --a4 HZ         with --import: tune the import to A4 = HZ instead of TTrak's\n"
                 "  --voice V A B   with --import: voice V (0-2) of both, frames A to B\n"
                 "  --cells T A B   with --import: steps A to B of track T (0-2) along the Song\n"
                 "  --export        import the module, export the patterns as a TTrak module (File > Export) and\n"
                 "                  compare what our player plays of it with the import's playback\n"
                 "  --out FILE      with --export: write the exported module\n"
                 "  --fuzz N        with --prg: N random modules made from the instruments (.TPI) and samples (.4BI)\n"
                 "                  given instead of a module, compared with the routine (with --import: imported)\n"
                 "  --seed S        first seed of --fuzz (default 1)\n"
                 "  --quiet         one line per module\n");
}

// Where the replay routine lies in TTrak 1.04's program (text and data, from its start), and what it holds.
constexpr size_t kBlobAt = 0x1F04C, kBlobSize = 0x3CA6;
constexpr size_t kBraWordsAt = 0x1F0BC;  // three words added to the routine's branches by the SNDH save
constexpr size_t kTcAt = 0x66;           // replay rate digits after "TC", then "HDNS"
constexpr uint32_t kSong = 0x16F6, kBuffer = 0x1710, kChannels = 0x175C, kChannelSize = 0x96;
constexpr uint32_t kPeriods = 0x191E, kMfp = 0x26BE, kWow = 0x345E;

uint16_t be16(const std::vector<uint8_t>& b, size_t at) { return uint16_t(b[at] << 8 | b[at + 1]); }
uint32_t be32(const std::vector<uint8_t>& b, size_t at) { return uint32_t(be16(b, at)) << 16 | be16(b, at + 2); }

// The routine, out of the program: the text and data segments after the 28-byte header.
std::vector<uint8_t> routineFrom(const std::vector<uint8_t>& prg, std::string& error) {
    if (prg.size() < 0x1C || be16(prg, 0) != 0x601A) {
        error = "not an Atari program";
        return {};
    }
    const size_t text = be32(prg, 2), data = be32(prg, 6);
    if (0x1C + text + data > prg.size() || text + data < kBlobAt + kBlobSize) {
        error = "not TTrak 1.04's program (too short)";
        return {};
    }
    std::vector<uint8_t> image(prg.begin() + 0x1C, prg.begin() + std::ptrdiff_t(0x1C + text + data));
    std::vector<uint8_t> blob(image.begin() + std::ptrdiff_t(kBlobAt),
                              image.begin() + std::ptrdiff_t(kBlobAt + kBlobSize));
    if (be16(blob, 0) != 0x6000 || std::memcmp(blob.data() + 12, "SNDH", 4) != 0) {
        error = "not TTrak 1.04's program (no replay routine where expected)";
        return {};
    }
    // The branches of init, exit and play, as the SNDH save sets them.
    static constexpr std::array<uint16_t, 3> kAdd{0x6E, 0x6A, 0x66};
    for (size_t k = 0; k < 3; ++k) {
        const auto w = uint16_t(be16(image, kBraWordsAt + 2 * k) + kAdd[k]);
        blob[2 + 4 * k] = uint8_t(w >> 8);
        blob[3 + 4 * k] = uint8_t(w);
    }
    return blob;
}

std::vector<uint8_t> sndhOf(std::vector<uint8_t> blob, const std::vector<uint8_t>& tpm, int rate) {
    const std::string digits = std::to_string(rate ? rate : 50);
    size_t at = kTcAt;
    for (char d : digits) blob[at++] = uint8_t(d);
    blob[at++] = 0;
    for (char d : std::string("HDNS")) blob[at++] = uint8_t(d);
    blob[at] = 0;
    blob.insert(blob.end(), tpm.begin(), tpm.end());
    if (blob.size() & 1) blob.push_back(0);
    return blob;
}

// The routine's tables, from the routine itself.
ymio::TtrakPlayer::Tables tablesFrom(const std::vector<uint8_t>& blob) {
    ymio::TtrakPlayer::Tables t;
    for (size_t i = 0; i < size_t(ymio::kTtrakPitches); ++i) {
        t.period[i] = be16(blob, kPeriods + 2 * i);
        t.mfpData[i] = blob[kMfp + 2 * i];
        t.mfpPrescaler[i] = blob[kMfp + 2 * i + 1];
    }
    for (size_t i = kWow; i + 1 < blob.size(); i += 2) {
        t.wow.push_back(blob[i]);
        if (blob[i] & 0x80) break;
        t.wow.push_back(blob[i + 1]);
    }
    return t;
}

std::string hex(const uint8_t* r, int n) {
    std::string s;
    char b[8];
    for (int i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, i ? " %02X" : "%02X", r[i]);
        s += b;
    }
    return s;
}

// A variable of the routine and ours.
struct Var {
    std::string name;
    long theirs, ours;
};

// The replay routine taken out of TTrak's program, and its tables.
struct Routine {
    std::vector<uint8_t> blob;
    ymio::TtrakPlayer::Tables tables;
};

struct Options {
    int ticks = 0, from = -1, to = -1;
    bool diff = false, ourTables = false;
};

struct Result {
    std::string error;
    std::array<long, 14> badBuf{}, badChip{};
    long badShape = 0, badVars = 0, compared = 0, beyondTable = 0;
    int firstBad = -1;
    std::vector<std::pair<std::string, long>> varCounts;
    long bufAll() const {
        long n = 0;
        for (long x : badBuf) n += x;
        return n;
    }
    long chipAll() const {
        long n = 0;
        for (long x : badChip) n += x;
        return n;
    }
    bool exact() const { return error.empty() && !bufAll() && !chipAll() && !badShape && !badVars; }
};

// Our player against the routine, tick by tick, on a module (its bytes, as the routine reads them).
Result compare(const Routine& routine, const std::vector<uint8_t>& tpm, const ymio::TtrakModule& module,
               const Options& o) {
    Result res;
    const auto sndh = ymio::parseSndh(sndhOf(routine.blob, tpm, module.rate));
    if (!sndh.file) {
        res.error = sndh.error;
        return res;
    }
    ymio::SndhCapture cap(*sndh.file, 1, 50000);
    if (!cap.ok()) {
        res.error = "the routine's init failed";
        return res;
    }
    ymio::TtrakPlayer ours(module);
    if (!o.ourTables) ours.setTables(routine.tables);
    const uint32_t base = ymio::kSndhLoadAddress;
    const uint32_t moduleAt = base + uint32_t(kBlobSize);
    auto peek8 = [&](uint32_t a) { return cap.peek(a); };
    auto peek16 = [&](uint32_t a) { return uint16_t(cap.peek(a) << 8 | cap.peek(a + 1)); };
    auto peek32 = [&](uint32_t a) { return uint32_t(peek16(a)) << 16 | peek16(a + 2); };
    static constexpr std::array<uint8_t, 13> kValueAt{0x0A, 0x0E, 0x1A, 0x1E, 0x2A, 0x2E, 0x32,
                                                      0x36, 0x3A, 0x3E, 0x42, 0x46, 0x4A};
    static constexpr std::array<uint8_t, 14> kMask{0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F,
                                                   0xFF, 0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F};
    const uint64_t spt = cap.samplesPerTick();
    std::array<uint8_t, 14> chip{};
    std::vector<ymio::SndhEvent> events;
    for (int i = 0; i < o.ticks; ++i) {
        events.clear();
        if (!cap.ok() || !cap.run(uint64_t(i) * spt + 1, events)) {
            res.error = "the routine crashed at tick " + std::to_string(i);
            break;
        }
        uint64_t tickCycle = 0;
        uint16_t wrote = 0;
        std::vector<uint8_t> shapes;
        for (const ymio::SndhEvent& e : events) {
            if (e.kind == ymio::SndhEvent::Kind::Tick) {
                tickCycle = e.cycle;
                wrote = 0;
                shapes.clear();
            } else if (e.kind == ymio::SndhEvent::Kind::Write && e.reg < 14 && e.cycle == tickCycle) {
                chip[e.reg] = uint8_t(e.value & kMask[e.reg]);
                wrote = uint16_t(wrote | 1 << e.reg);
                if (e.reg == 13) shapes.push_back(e.value);
            }
        }
        const ymio::TtrakTick t = ours.tick();
        std::array<uint8_t, 13> theirs{};
        for (size_t r = 0; r < 13; ++r) theirs[r] = peek8(base + kBuffer + kValueAt[r]);
        bool same = true;
        for (size_t r = 0; r < 13; ++r)
            if (theirs[r] != t.buffer[r]) {
                ++res.badBuf[r];
                same = false;
            }
        // R0-R12: what was written; R13: each write (the trigger writes it at once, not from the buffer).
        for (size_t r = 0; r < 13; ++r) {
            const bool weWrote = t.written >> r & 1, theyWrote = wrote >> r & 1;
            if (weWrote != theyWrote || (weWrote && chip[r] != t.reg[r])) {
                ++res.badChip[r];
                same = false;
            }
        }
        const std::vector<uint8_t> ourShapes(t.shape.begin(), t.shape.begin() + t.shapes);
        if (shapes != ourShapes) {
            ++res.badShape;
            same = false;
        }
        // Variables.
        std::vector<Var> vars;
        const uint32_t song = base + kSong;
        vars.push_back({"counter", int8_t(peek8(song + 0)), ours.counter()});
        vars.push_back({"speed", peek8(song + 1), t.speed});
        vars.push_back({"row", peek8(song + 2), ours.row()});
        vars.push_back({"position", peek8(song + 3), ours.position()});
        vars.push_back({"sidechain", peek8(song + 0x0F), t.sidechain});
        vars.push_back({"lastShape", peek8(song + 0x10), ours.lastShape()});
        for (int c = 0; c < 3; ++c) {
            const uint32_t at = base + kChannels + uint32_t(c) * kChannelSize;
            const ymio::TtrakPlayer::Channel& v = ours.channel(c);
            const uint32_t rec = peek32(at + 4);
            const long ins = rec ? long(rec - moduleAt - 0x206) / 0x416 + 1 : 0;
            const char p = char('A' + c);
            auto add = [&](const char* n, long th, long ou) { vars.push_back({std::string(1, p) + "." + n, th, ou}); };
            add("note", peek8(at + 0), v.note);
            add("effect", peek8(at + 2), v.effect);
            add("instrument", ins, v.instrument);
            add("atten", peek8(at + 8), v.atten);
            add("type", peek8(at + 9), v.type);
            add("detune", peek16(at + 0x0E), v.detune);
            add("fixed", peek16(at + 0x10), v.fixed);
            add("fmSpeed", peek8(at + 0x12), v.fmSpeed);
            add("fmDepth", peek8(at + 0x13), v.fmDepth);
            add("fmValue", peek16(at + 0x14), v.fmValue);
            add("fmPos", peek16(at + 0x16), v.fmPos);
            add("morphValue", peek16(at + 0x1A), v.morphValue);
            add("morphPos", peek16(at + 0x1C), v.morphPos);
            add("ampCount", int8_t(peek8(at + 0x1E)), v.ampCount);
            add("ampSpeed", peek8(at + 0x1F), v.ampSpeed);
            add("ampPos", rec ? long(peek32(at + 0x20)) - long(rec + 0x4C) : 0, v.ampPos);
            add("arpCount", int8_t(peek8(at + 0x2C)), v.arpCount);
            add("arpPos", long(peek32(at + 0x3E)) - long(at + 0x2E), rec ? v.arpPos : long(peek32(at + 0x3E)) -
                                                                                    long(at + 0x2E));
            add("noiseCount", int8_t(peek8(at + 0x46)), v.noiseCount);
            add("noisePos", rec ? long(peek32(at + 0x48)) - long(rec) : 0, v.noisePos);
            add("vibDepth", peek8(at + 0x60), v.vibDepth);
            add("vibSpeed", int8_t(peek8(at + 0x61)), v.vibSpeed);
            add("vibOffset", int8_t(peek8(at + 0x62)), v.vibOffset);
            add("vibCount", int8_t(peek8(at + 0x63)), v.vibCount);
            add("porta", peek8(at + 0x65) != 0, v.porta);
            add("slide", int16_t(peek16(at + 0x66)), v.slide);
            add("slideRate", int16_t(peek16(at + 0x68)), v.slideRate);
            add("lastPitch", int16_t(peek16(at + 0x6A)), v.lastPitch);
            add("mfpPitch", peek16(at + 0x6C), v.mfpPitch);
            add("step", peek8(at + 0x6E), v.step);
            add("timerOn", peek8(at + 0x7C) != 0, v.timerOn);
            const uint16_t ctrl = peek16(at + 0x7E);
            add("mfpTimer", ctrl == 0xFA19 ? 1 : ctrl == 0xFA1D ? 2 : ctrl ? 9 : 0, v.mfpTimer);
            add("wowIndex", peek16(at + 0x86), v.wowIndex);
            add("wowValue", peek8(at + 0x88), v.wowValue);
            add("wowCount", int8_t(peek8(at + 0x89)), v.wowCount);
            // The wave that the routine left in the interrupt's code for this tick: SID volumes, sync buzzer
            // shapes, FM tone periods (low and high bytes).
            const ymio::TtrakChannelTick& oc = t.ch[size_t(c)];
            const uint32_t h = peek32(at + 0x78);
            // (A channel whose timer another one took later in the tick no longer plays its wave.)
            for (int k = 0; k < oc.timerSteps && oc.timerRuns; ++k) {
                // A SID level past the routine's table of 32 (a volume with bit 4, the buzzer's, and a level): the
                // routine reads what follows its table; not compared.
                const int index = oc.volume + oc.timerWave[size_t(k)];
                if (oc.timer >= 2 && oc.timer <= 4 && (index < 0 || index >= 32)) {
                    ++res.beyondTable;
                    continue;
                }
                const auto ku = uint32_t(k);
                long theirsK = 0;
                if (oc.timer == 6) theirsK = peek8(h + 4 + 0x18 * ku) | peek8(h + 0x0C + 0x18 * ku) << 8;
                else if (oc.timer == 8) theirsK = peek8(h + 0x18 + 0x2C * ku) | peek8(h + 0x20 + 0x2C * ku) << 8;
                else theirsK = peek8(h + 4 + 0x10 * ku);
                add(("wave" + std::to_string(k)).c_str(), theirsK, oc.timerOut[size_t(k)]);
            }
        }
        std::string bad;
        for (const Var& x : vars) {
            if (x.theirs == x.ours) continue;
            same = false;
            ++res.badVars;
            auto it = std::find_if(res.varCounts.begin(), res.varCounts.end(),
                                   [&](const auto& e) { return e.first == x.name; });
            if (it == res.varCounts.end()) res.varCounts.emplace_back(x.name, 1);
            else ++it->second;
            char b[64];
            std::snprintf(b, sizeof b, " %s %ld/%ld", x.name.c_str(), x.ours, x.theirs);
            bad += b;
        }
        ++res.compared;
        if (!same && res.firstBad < 0) res.firstBad = i;
        if (o.diff && i >= o.from && i < o.to && !same) {
            std::printf("%5d %02X.%02X buf ours   %s\n               theirs %s\n      chip ours   %s",
                        i, t.position, t.row, hex(t.buffer.data(), 13).c_str(), hex(theirs.data(), 13).c_str(),
                        hex(t.reg.data(), 13).c_str());
            for (uint8_t s : ourShapes) std::printf(" R13=%X", s);
            std::printf(" w%04X\n           theirs %s", t.written, hex(chip.data(), 13).c_str());
            for (uint8_t s : shapes) std::printf(" R13=%X", s);
            std::printf(" w%04X\n      vars (ours/theirs)%s\n", wrote, bad.c_str());
            for (int c = 0; c < 3; ++c) {
                const ymio::TtrakPlayer::Channel& v = ours.channel(c);
                const ymio::TtrakChannelTick& oc = t.ch[size_t(c)];
                std::printf("      %c: note %02X ins %d type %d timer %d step %02X vol %02X pitch %X wave", 'A' + c,
                            v.note, v.instrument, v.type, v.mfpTimer, v.step, oc.volume, oc.timerPitch);
                for (int k = 0; k < oc.timerSteps; ++k) std::printf(" %d", oc.timerWave[size_t(k)]);
                std::printf("\n");
            }
        }
    }
    return res;
}

// Ticks of the song once through: until it goes back to its restart position (at most `limit`).
int songTicks(const ymio::TtrakModule& module, int limit) {
    ymio::TtrakPlayer p(module);
    int n = 0;
    while (p.loops() == 0 && n < limit) {
        p.tick();
        ++n;
    }
    return n;
}

void printResult(const char* name, const Result& r, bool quiet) {
    std::printf("%-12s %6ld ticks  buffer diffs %6ld  chip diffs %6ld  R13 diffs %5ld  variable diffs %6ld  "
                "first %d%s%s\n",
                name, r.compared, r.bufAll(), r.chipAll(), r.badShape, r.badVars, r.firstBad,
                r.error.empty() ? "" : "  ", r.error.c_str());
    if (r.beyondTable) std::printf("  %ld SID level(s) past the routine's table, not compared\n", r.beyondTable);
    if (quiet) return;
    std::printf("  buffer by register:");
    for (long x : r.badBuf) std::printf(" %ld", x);
    std::printf("\n  chip by register:  ");
    for (long x : r.badChip) std::printf(" %ld", x);
    std::printf("\n  variables:");
    for (const auto& [n, k] : r.varCounts) std::printf(" %s %ld", n.c_str(), k);
    std::printf("\n");
}

// A random module for the comparison: the instruments of the given files (TPI) in slots 1 on, the others copies of
// them with their settings changed at random (within what the routine reads inside the record), random patterns
// whose row 0 gives a note and an instrument to each channel, random effects; samples from the TPI and 4BI files.
ymio::TtrakModule randomModule(uint32_t seed, const std::vector<ymio::TtrakInstrumentFile>& instruments,
                               const std::vector<std::vector<uint8_t>>& samples) {
    std::mt19937 rng(seed);
    auto pick = [&](int lo, int hi) { return int(std::uniform_int_distribution<int>(lo, hi)(rng)); };
    auto chance = [&](int percent) { return pick(0, 99) < percent; };
    using I = ymio::TtrakInstrument;
    ymio::TtrakModule m = ymio::TtrakModule::blank();
    static constexpr std::array<uint8_t, 4> kRates{50, 100, 150, 200};
    m.rate = kRates[size_t(pick(0, 3))];
    for (uint8_t& g : m.groove) g = uint8_t(chance(70) ? 0 : pick(0, 3));
    // Samples: those of the TPI files, then the 4BI files.
    int slots = 0;
    std::vector<int> slotOf(instruments.size(), -1);
    for (size_t k = 0; k < instruments.size() && slots < ymio::kTtrakSampleSlots; ++k)
        if (!instruments[k].sample.empty()) {
            slotOf[k] = slots;
            m.samples[size_t(slots++)] = instruments[k].sample;
        }
    for (size_t k = 0; k < samples.size() && slots < ymio::kTtrakSampleSlots; ++k)
        m.samples[size_t(slots++)] = samples[k];
    for (int k = 0; k < ymio::kTtrakInstruments && !instruments.empty(); ++k) {
        const size_t src = size_t(k) < instruments.size() ? size_t(k) : size_t(pick(0, int(instruments.size()) - 1));
        I r = instruments[src].instrument;
        if (slotOf[src] >= 0) r.at(I::kSampleSlot) = uint8_t(slotOf[src]);
        else if (slots) r.at(I::kSampleSlot) = uint8_t(pick(0, slots - 1));
        if (size_t(k) >= instruments.size()) {
            // Settings changed at random.
            if (chance(50)) {
                // Another timer type, with a timer table that TTrak's editor allows for it: SID levels 0-15, sync
                // buzzer shapes, FM multipliers 0-3.
                const int type = pick(0, 8);
                r.at(I::kTimerType) = uint8_t(type);
                for (int x = 0; x < 16; ++x)
                    r.at(I::kTimerTable + x) = uint8_t(type == 6 || type == 8 ? pick(0, 3) : pick(0, 15));
            }
            if (chance(30)) r.at(I::kAttenuation) = uint8_t(pick(0, 15));
            if (chance(30)) r.at(I::kBuzzerShape) = uint8_t(chance(30) ? 0 : pick(8, 15));
            if (chance(30)) r.at(I::kWowDepth) = uint8_t(chance(30) ? 0 : pick(1, 40));
            if (chance(20)) r.at(I::kHardSync) = uint8_t(chance(50) ? 0 : 0xFF);
            if (chance(20)) r.at(I::kLink) = uint8_t(chance(50) ? 0 : pick(1, 32));
            if (chance(30)) r.setWord(I::kDetune, uint16_t(pick(0, 64)));
            if (chance(30)) r.setWord(I::kFixedPitch, uint16_t(pick(0, 0x690)));
            if (chance(30)) r.at(I::kFmSpeed) = uint8_t(pick(0, 8));
            if (chance(30)) r.at(I::kFmDepth) = uint8_t(pick(0, 255));
            if (chance(20)) r.at(I::kYmPad) = uint8_t(chance(50) ? 0 : 1);
            if (chance(30)) r.at(I::kSidechainSend) = uint8_t(pick(0, 15));
            if (chance(30)) r.at(I::kSidechainDuck) = uint8_t(chance(50) ? 0 : 1);
            if (chance(30)) r.at(I::kMorphSpeed) = uint8_t(pick(0, 8));
            for (int x = I::kMatrixArp; x < I::kMatrixWow + 4; ++x)
                if (x != I::kSampleSlot && chance(15)) r.at(x) = uint8_t(chance(50) ? 0 : 1);
            if (chance(30)) r.at(I::kAmpSpeed) = uint8_t(pick(0, 4));
            if (chance(30)) {
                r.at(I::kAmpLast) = uint8_t(pick(0, I::kAmpSteps - 1));
                r.at(I::kAmpLoop) = uint8_t(pick(0, r.at(I::kAmpLast)));
            }
            if (chance(30))
                for (int x = 0; x < I::kAmpSteps; ++x) r.at(I::kAmpEnv + x) = uint8_t(pick(0, 255));
        }
        m.instruments[size_t(k)] = r;
    }
    // Song: a few positions over a few patterns.
    const int patterns = pick(1, 6);
    m.length = uint8_t(pick(1, 8));
    m.restart = uint8_t(pick(0, m.length - 1));
    for (int k = 0; k < m.length; ++k) m.order[size_t(k)] = uint8_t(pick(0, patterns - 1));
    m.order[size_t(m.length)] = uint8_t(pick(0, patterns - 1));
    static constexpr std::array<uint8_t, 18> kEffects{0x01, 0x02, 0x03, 0x04, 0x07, 0x08, 0x0A, 0x0B, 0x0D,
                                                      0x0F, 0x12, 0x13, 0x14, 0x15, 0x1C, 0x1F, 0x20, 0x05};
    m.patterns.resize(size_t(ymio::ttrakPatternCount(m.order, m.length)));
    for (ymio::TtrakPattern& p : m.patterns)
        for (int row = 0; row < ymio::kTtrakRows; ++row)
            for (int c = 0; c < 3; ++c) {
                ymio::TtrakCell& cell = p.rows[size_t(row)][size_t(c)];
                const bool first = row == 0;
                if (first || chance(25)) cell.note = uint8_t(chance(95) ? pick(1, 100) : pick(101, 255));
                if (first || (cell.note && chance(60)) || chance(5)) cell.instrument = uint8_t(pick(1, 32));
                if (chance(12)) {
                    cell.effect = kEffects[size_t(pick(0, int(kEffects.size()) - 1))];
                    cell.param = uint8_t(pick(0, 255));
                    if (cell.effect == 0x13) cell.param = uint8_t(pick(0, m.length + 1));
                    if (cell.effect == 0x1C) cell.param = uint8_t(pick(0, 12));
                    if (cell.effect == 0x15) cell.param = uint8_t(pick(0, 32));
                    if (cell.effect == 0x12) cell.param = uint8_t(pick(0, 8));
                    if (cell.effect == 0x0B && chance(70)) cell.effect = 0;  // breaks keep the song short
                }
            }
    return m;
}

// The module imported as File > Import does, played back by the engine with the settings that the import applies,
// compared with the frames of our player.
int importMode(const char* name, const ymio::TtrakModule& module, bool quiet, double a4, int voice, int cells, int from,
               int to) {
    plugin::TtrakTune tune = plugin::ttrakTune(module);
    if (a4 > 0) tune.layout.a4Hz = a4;
    plugin::NonParamState state;
    const plugin::RegisterImportResult r = plugin::importRegisters(tune.tune, tune.layout, state);
    if (!r.import) {
        std::fprintf(stderr, "%s: %s\n", name, r.error.c_str());
        return 1;
    }
    const plugin::RegisterImport& m = *r.import;
    if (cells >= 0) {
        tools::printCells(state, cells, from, to);
        return 0;
    }
    std::vector<tools::Effects> fx;
    const auto playback = tools::playImport(m, state, &fx);
    if (!playback) return 1;
    const std::vector<tools::Frame>& frames = *playback;
    // The voices that a timer drives are scored apart: the same generator, and the pitch of its timer within 1 % (a
    // SID: the rate of its waveform; a digidrum, whose rate goes by semitones: within 3 %); tone FM is left out.
    using ymengine::Generator;
    struct FxScore {
        long frames = 0, gen = 0, hz = 0;
    };
    std::array<FxScore, 3> fxScore{};
    std::vector<std::array<bool, 3>> skip(tune.tune.effects.size());
    {
        ymio::TtrakPlayer p(module);
        for (size_t i = 0; i < skip.size(); ++i) {
            const ymio::TtrakTick k = p.tick();
            for (int c = 0; c < 3; ++c) skip[i][size_t(c)] = k.ch[size_t(c)].timer && k.ch[size_t(c)].timerRuns;
        }
    }
    for (size_t i = 0; i < tune.tune.effects.size(); ++i)
        for (int c = 0; c < 3; ++c) {
            const plugin::TimerEffect& x = tune.tune.effects[i][size_t(c)];
            if (x.gen == Generator::Chip) continue;
            FxScore& s = fxScore[size_t(c)];
            ++s.frames;
            if (i >= fx.size()) continue;
            const ymengine::RegisterFrame::Effect& e = fx[i][size_t(c)];
            if (!e.on || e.gen != x.gen) continue;
            ++s.gen;
            const double ours = x.gen == Generator::Sid ? x.hz / std::max<int>(1, x.steps) : x.hz;
            const double theirs = x.gen == Generator::Sid ? e.timer.hz() / 2 : e.timer.hz();
            s.hz += ours > 0 && std::abs(theirs / ours - 1) < (x.gen == Generator::Digidrum ? 0.03 : 0.01);
        }
    FxScore fxAll;
    for (const FxScore& s : fxScore) {
        fxAll.frames += s.frames;
        fxAll.gen += s.gen;
        fxAll.hz += s.hz;
    }
    if (voice >= 0) {
        // One voice, frame by frame: our player, then the playback; * where they differ.
        tools::Frame silent{};
        silent[7] = 0x3f;
        for (int i = from; i < to && i < int(tune.tune.frames.size()); ++i) {
            const tools::Frame& played = i < int(frames.size()) ? frames[size_t(i)] : silent;
            const auto x = tools::scoreVoices(tune.tune.frames[size_t(i)], 1.0)[size_t(voice)];
            const auto y = tools::scoreVoices(played, 1.0)[size_t(voice)];
            std::string ours, theirs;
            char b[64];
            const plugin::TimerEffect& t = tune.tune.effects[size_t(i)][size_t(voice)];
            if (t.gen != Generator::Chip) {
                std::snprintf(b, sizeof b, " [%d/%d %.1f]", int(t.gen), t.steps, t.hz);
                ours = b;
            }
            if (size_t(i) < fx.size() && fx[size_t(i)][size_t(voice)].on) {
                const ymengine::RegisterFrame::Effect& e = fx[size_t(i)][size_t(voice)];
                std::snprintf(b, sizeof b, " [%d %d/%d %.1f]", int(e.gen), e.timer.prescaler, e.timer.data,
                              e.timer.hz());
                theirs = b;
            }
            std::printf("%6d  %s%s  |  %s%s %s\n", i, tools::showVoice(x).c_str(), ours.c_str(),
                        tools::showVoice(y).c_str(), theirs.c_str(), tools::sameVoice(x, y) ? "" : "*");
        }
        return 0;
    }
    const std::array<tools::Score, 3> score = tools::scoreFrames(tune.tune.frames, 1.0, frames, 0, &skip);
    const tools::Score all = tools::total(score);
    const auto pct = tools::percent;
    if (quiet) {
        std::printf("%-14s %6zu fr %2d pat %2d ins %4d notes | exact %5.1f%%  snd %5.1f  vol %5.1f  mix %5.1f  "
                    "per %5.1f  10c %5.1f  np %5.1f | fx %6ld gen %5.1f hz %5.1f\n",
                    name, tune.tune.frames.size(), m.patterns, m.instruments, m.notes, pct(all.exact, all.frames),
                    pct(all.agree, all.frames), pct(all.vol, all.both), pct(all.mix, all.both),
                    pct(all.period, all.tonal), pct(all.cents, all.tonal), pct(all.noise, all.noisy), fxAll.frames,
                    pct(fxAll.gen, fxAll.frames), pct(fxAll.hz, fxAll.gen));
        return 0;
    }
    const auto& rows = tune.layout.stepFrames;
    std::printf("%s: %zu frames at %u Hz, %zu rows of %d to %d ticks in %zu patterns, loop at row %d, captured %zu\n",
                name, tune.tune.frames.size(), tune.tune.rate, rows.size(),
                rows.empty() ? 0 : *std::min_element(rows.begin(), rows.end()),
                rows.empty() ? 0 : *std::max_element(rows.begin(), rows.end()), tune.layout.partSteps.size(),
                tune.layout.loopStep, frames.size());
    for (const std::string& l : plugin::ttrakImportMessage(m, name)) std::printf("  %s\n", l.c_str());
    std::printf("Song:");
    long steps = 0;
    for (const auto& e : state.seq.song.entries) {
        const int n = state.seq.patterns[size_t(e.pat)].steps;
        std::printf(" %d(%d)x%d%s", e.pat, n, e.rep, e.restart ? "|:" : "");
        steps += long(n) * e.rep;
    }
    std::printf("  = %ld steps\n", steps);
    for (int c = 0; c < 3; ++c) {
        const tools::Score& x = score[size_t(c)];
        std::printf("voice %c: exact %5.1f%%  sound agrees %5.1f%%  vol %5.1f%%  mix %5.1f%%  period %5.1f%%  "
                    "<10c %5.1f%%  noise %5.1f%%\n",
                    'A' + c, pct(x.exact, x.frames), pct(x.agree, x.frames), pct(x.vol, x.both), pct(x.mix, x.both),
                    pct(x.period, x.tonal), pct(x.cents, x.tonal), pct(x.noise, x.noisy));
        const FxScore& f = fxScore[size_t(c)];
        if (f.frames)
            std::printf("         timer effects: %ld frames, generator %5.1f%%, pitch %5.1f%%\n", f.frames,
                        pct(f.gen, f.frames), pct(f.hz, f.gen));
    }
    return 0;
}

// The module imported as patterns, exported back (File > Export song > TTrak), and the export played by our player,
// compared frame by frame with the engine playing the patterns.
int exportMode(const char* name, const ymio::TtrakModule& module, bool quiet, int voice, int from, int to,
               const char* out) {
    const plugin::TtrakTune tune = plugin::ttrakTune(module);
    plugin::NonParamState state;
    const plugin::RegisterImportResult r = plugin::importRegisters(tune.tune, tune.layout, state);
    if (!r.import) {
        std::fprintf(stderr, "%s: %s\n", name, r.error.c_str());
        return 1;
    }
    std::vector<tools::Effects> fx;
    const auto playback = tools::playImport(*r.import, state, &fx);
    if (!playback) return 1;
    const std::vector<tools::Frame>& want = *playback;
    const auto x = plugin::exportTtrak(tools::importValues(*r.import), state, module.titleText());
    if (!x || !x->value) {
        std::fprintf(stderr, "%s: %s\n", name, x ? x->error.c_str() : "cancelled");
        return 1;
    }
    const plugin::TtrakExport& e = *x->value;
    const std::vector<uint8_t> bytes = ymio::writeTtrakModule(e.module);
    const auto back = ymio::parseTtrakModule(bytes);
    if (!back.value) {
        std::fprintf(stderr, "%s: the export does not read back: %s\n", name, back.error.c_str());
        return 1;
    }
    if (out) {
        FILE* f = std::fopen(out, "wb");
        if (!f || std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
            std::fprintf(stderr, "%s: cannot write\n", out);
            if (f) std::fclose(f);
            return 1;
        }
        std::fclose(f);
    }
    // The timer effects are scored apart: the same generator, its pitch within 1 % (a digidrum within 3 %).
    using ymengine::Generator;
    ymio::TtrakPlayer player(*back.value);
    std::vector<tools::Frame> got;
    std::vector<std::array<bool, 3>> skip(want.size());
    std::vector<std::array<std::string, 3>> timers(want.size());
    long fxFrames = 0, fxKind = 0, fxHz = 0;
    for (size_t i = 0; i < want.size(); ++i) {
        const ymio::TtrakTick k = player.tick();
        tools::Frame g{};
        std::copy(k.buffer.begin(), k.buffer.end(), g.begin());
        g[13] = k.reg[13];
        got.push_back(g);
        for (int c = 0; c < 3; ++c) {
            const ymengine::RegisterFrame::Effect none{};
            const ymengine::RegisterFrame::Effect& f = i < fx.size() ? fx[i][size_t(c)] : none;
            const ymio::TtrakChannelTick& ch = k.ch[size_t(c)];
            const bool theirs = ch.timer && ch.timerRuns;
            const ymengine::MfpSetting mfp{ch.timerPrescaler, uint16_t(ch.timerData ? ch.timerData : 256)};
            const double hz = theirs ? mfp.hz() : 0;
            skip[i][size_t(c)] = f.on || theirs;
            if (f.on || ch.timer) {
                char b[80];
                std::snprintf(b, sizeof b, " [%d %.1f | %X%s %.1f]", int(f.gen), f.on ? f.timer.hz() : 0.0, ch.timer,
                              theirs ? "" : "-", hz);
                timers[i][size_t(c)] = b;
            }
            if (!f.on) continue;
            ++fxFrames;
            // A Sync Buzzer at a whole multiple of its rate plays as a plain buzzer: the envelope's own rate.
            const int shape = g[13] & 15;
            const bool buzzer = (g[size_t(8 + c)] & 16) && shape >= 8 && !(shape & 1);
            if (f.gen == Generator::SyncBuzzer && !ch.timer && buzzer) {
                ++fxKind;
                const double env = 2e6 / (256.0 * ymengine::envelopeRamps(shape) * std::max(1, g[11] | g[12] << 8));
                const double n = std::round(env / f.timer.hz());
                fxHz += n >= 1 && std::abs(env / (n * f.timer.hz()) - 1) < 0.01;
                continue;
            }
            const bool kind = (f.gen == Generator::Sid && ch.timer >= 1 && ch.timer <= 4) ||
                              (f.gen == Generator::SyncBuzzer && ch.timer == 5) ||
                              (f.gen == Generator::Digidrum && ch.timer == 7);
            if (!kind || !theirs) continue;
            ++fxKind;
            fxHz += std::abs(hz / f.timer.hz() - 1) < (f.gen == Generator::Digidrum ? 0.03 : 0.01);
        }
    }
    const auto pct = tools::percent;
    if (voice >= 0) {
        for (int i = from; i < to && i < int(want.size()); ++i) {
            const auto a = tools::scoreVoices(want[size_t(i)], 1.0)[size_t(voice)];
            const auto b = tools::scoreVoices(got[size_t(i)], 1.0)[size_t(voice)];
            std::printf("%6d  %s  |  %s%s %s\n", i, tools::showVoice(a).c_str(), tools::showVoice(b).c_str(),
                        timers[size_t(i)][size_t(voice)].c_str(), tools::sameVoice(a, b) ? "" : "*");
        }
        return 0;
    }
    const std::array<tools::Score, 3> score = tools::scoreFrames(want, 1.0, got, 0, &skip);
    const tools::Score all = tools::total(score);
    std::printf("%-14s %6zu fr %3d pos %3d pat %2d ins %4d notes | exact %5.1f%%  snd %5.1f  vol %5.1f  mix %5.1f  "
                "per %5.1f  10c %5.1f  np %5.1f | fx %6ld kind %5.1f hz %5.1f\n",
                name, want.size(), e.positions, e.patterns, e.instruments, e.notes, pct(all.exact, all.frames),
                pct(all.agree, all.frames), pct(all.vol, all.both), pct(all.mix, all.both), pct(all.period, all.tonal),
                pct(all.cents, all.tonal), pct(all.noise, all.noisy), fxFrames, pct(fxKind, fxFrames),
                pct(fxHz, fxKind));
    if (!quiet)
        for (const std::string& l : plugin::ttrakExportMessage(e, "song.tpm")) std::printf("  %s\n", l.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const char* prgPath = nullptr;
    std::vector<const char*> files;
    Options o;
    int fuzz = 0;
    uint32_t seed = 1;
    bool quiet = false, import = false, exportBack = false;
    const char* outPath = nullptr;
    double a4 = 0;
    int voice = -1, cells = -1, cellsFrom = 0, cellsTo = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--prg") prgPath = next();
        else if (a == "--ticks") o.ticks = std::atoi(next());
        else if (a == "--dump" || a == "--diff") {
            o.diff = a == "--diff";
            o.from = std::atoi(next());
            o.to = std::atoi(next());
        } else if (a == "--our-tables") o.ourTables = true;
        else if (a == "--fuzz") fuzz = std::atoi(next());
        else if (a == "--seed") seed = uint32_t(std::strtoul(next(), nullptr, 10));
        else if (a == "--quiet") quiet = true;
        else if (a == "--import") import = true;
        else if (a == "--export") exportBack = true;
        else if (a == "--out") outPath = next();
        else if (a == "--a4") a4 = std::atof(next());
        else if (a == "--voice" || a == "--cells") {
            (a == "--voice" ? voice : cells) = std::atoi(next());
            cellsFrom = std::atoi(next());
            cellsTo = std::atoi(next());
        }
        else if (a[0] == '-') {
            usage();
            return 2;
        } else files.push_back(argv[i]);
    }
    if (files.empty() || (files.size() > 1 && !fuzz)) {
        usage();
        return 2;
    }
    Routine routine;
    if (prgPath) {
        std::string error;
        routine.blob = routineFrom(tools::readFile(prgPath), error);
        if (routine.blob.empty()) {
            std::fprintf(stderr, "%s: %s\n", prgPath, error.c_str());
            return 1;
        }
        routine.tables = tablesFrom(routine.blob);
        const ymio::TtrakPlayer::Tables& ours = ymio::TtrakPlayer::defaultTables();
        int periods = 0, mfp = 0;
        for (size_t i = 0; i < size_t(ymio::kTtrakPitches); ++i) {
            periods += ours.period[i] != routine.tables.period[i];
            mfp += ours.mfpData[i] != routine.tables.mfpData[i] ||
                   ours.mfpPrescaler[i] != routine.tables.mfpPrescaler[i];
        }
        if (!quiet)
            std::printf("our tables: %d period(s) and %d MFP setting(s) differ from the routine's; its wow walk has "
                        "%zu bytes\n",
                        periods, mfp, routine.tables.wow.size());
    }

    if (fuzz) {
        if (!prgPath && !import && !exportBack) {
            std::fprintf(stderr, "--fuzz needs --prg, --import or --export\n");
            return 2;
        }
        std::vector<ymio::TtrakInstrumentFile> instruments;
        std::vector<std::vector<uint8_t>> samples;
        for (const char* f : files) {
            const std::vector<uint8_t> bytes = tools::readFile(f);
            if (ymio::isTtrakInstrument(bytes)) {
                auto r = ymio::parseTtrakInstrument(bytes);
                if (r.value) instruments.push_back(std::move(*r.value));
                else std::fprintf(stderr, "%s: %s\n", f, r.error.c_str());
            } else if (!bytes.empty()) {
                samples.push_back(ymio::parseTtrak4bi(bytes));
            }
        }
        if (instruments.empty()) {
            std::fprintf(stderr, "--fuzz: no instrument file (.TPI)\n");
            return 2;
        }
        int exact = 0;
        for (int k = 0; k < fuzz; ++k) {
            const uint32_t s = seed + uint32_t(k);
            const ymio::TtrakModule m = randomModule(s, instruments, samples);
            if (import || exportBack) {
                // Imported (and exported) without a fault; the scores are printed.
                const std::string name = "seed " + std::to_string(s);
                exact += (exportBack ? exportMode(name.c_str(), m, true, -1, 0, 0, nullptr)
                                     : importMode(name.c_str(), m, true, a4, -1, -1, 0, 0)) == 0;
                continue;
            }
            const std::vector<uint8_t> bytes = ymio::writeTtrakModule(m);
            Options x = o;
            if (!x.ticks) x.ticks = songTicks(m, 6000);
            const Result r = compare(routine, bytes, m, x);
            exact += r.exact();
            if (!r.exact() || !quiet) {
                const std::string name = "seed " + std::to_string(s);
                printResult(name.c_str(), r, quiet);
            }
        }
        if (exportBack) std::printf("%d of %d random modules imported and exported\n", exact, fuzz);
        else if (import) std::printf("%d of %d random modules imported\n", exact, fuzz);
        else std::printf("%d of %d random modules played exactly as the routine plays them\n", exact, fuzz);
        return exact == fuzz ? 0 : 1;
    }

    const char* file = files[0];
    const std::vector<uint8_t> tpm = tools::readFile(file);
    const auto parsed = ymio::parseTtrakModule(tpm);
    if (!parsed.value) {
        std::fprintf(stderr, "%s: %s\n", file, parsed.error.c_str());
        return 1;
    }
    const ymio::TtrakModule& module = *parsed.value;
    const char* name = std::strrchr(file, '/') ? std::strrchr(file, '/') + 1 : file;
    if (exportBack) return exportMode(name, module, quiet, voice, cellsFrom, cellsTo, outPath);
    if (import) return importMode(name, module, quiet, a4, voice, cells, cellsFrom, cellsTo);
    if (!o.ticks) o.ticks = songTicks(module, 30000);
    if (o.from >= 0 && !o.diff) {
        ymio::TtrakPlayer ours(module);
        if (prgPath && !o.ourTables) ours.setTables(routine.tables);
        for (int i = 0; i < o.to && i < o.ticks; ++i) {
            const ymio::TtrakTick t = ours.tick();
            if (i < o.from) continue;
            std::printf("%5d %02X.%02X%c buf %s | chip %s", i, t.position, t.row, t.rowStart ? '*' : ' ',
                        hex(t.buffer.data(), 13).c_str(), hex(t.reg.data(), 14).c_str());
            for (int k = 0; k < t.shapes; ++k) std::printf(" R13=%X", t.shape[size_t(k)]);
            for (const auto& c : t.ch)
                if (c.note)
                    std::printf(" [%02X i%d s%02X%s]", c.note, c.instrument, c.step,
                                c.timer ? (c.timerRuns ? " T" : " t") : "");
            std::printf("\n");
        }
        return 0;
    }
    if (!prgPath) {
        std::printf("%s: %d ticks played (no routine to compare with: --prg)\n", name, o.ticks);
        return 0;
    }
    const Result r = compare(routine, tpm, module, o);
    printResult(name, r, quiet);
    return r.error.empty() ? 0 : 1;
}
