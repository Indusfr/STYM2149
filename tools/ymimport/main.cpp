// STYM2149 — ymimport: a YM or SNDH tune imported as patterns, then played back by the engine and compared
// frame by frame with the tune.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The import is the one of File > Import > YM / SNDH (plugincore/RegisterImport.h); the playback, the capture of the
// register exports (captureRegisters: the Song once, in Authentic 3ch); the comparison is the one of ImportCheck.h.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ImportCheck.h"
#include "ReadFile.h"
#include "plugincore/RegisterImport.h"
#include "ymio/Sndh.h"
#include "ymio/YmFile.h"

using namespace stym;

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: ymimport [options] file.ym|file.sndh\n"
                 "  --subsong N   SNDH subsong (default of the file)\n"
                 "  --step N      frames per step (guessed)\n"
                 "  --pattern N   steps per pattern (guessed)\n"
                 "  --offset N    frame of the first step (guessed with the step)\n"
                 "  --quiet       one line per file\n"
                 "  --grids       the share of the note starts on a step for each step length\n"
                 "  --frames A B  frames A to B of the tune\n"
                 "  --diff V A B  voice V (0-2) of the tune and of the playback, frames A to B\n"
                 "  --cells T A B steps A to B of track T (0-2) along the Song\n");
}

}  // namespace

int main(int argc, char** argv) {
    int subsong = 0, step = 0, pattern = 0, offset = INT32_MIN;
    bool quiet = false, grids = false;
    int dumpFrom = -1, dumpTo = -1, diffVoice = -1, cellTrack = -1;
    const char* path = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> int {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return std::atoi(argv[++i]);
        };
        if (a == "--subsong") subsong = next();
        else if (a == "--step") step = next();
        else if (a == "--pattern") pattern = next();
        else if (a == "--offset") offset = next();
        else if (a == "--quiet") quiet = true;
        else if (a == "--grids") grids = true;
        else if (a == "--diff") {
            diffVoice = next();
            dumpFrom = next();
            dumpTo = next();
        } else if (a == "--cells") {
            cellTrack = next();
            dumpFrom = next();
            dumpTo = next();
        } else if (a == "--frames") {
            dumpFrom = next();
            dumpTo = next();
        }
        else if (a[0] == '-') {
            usage();
            return 2;
        } else path = argv[i];
    }
    if (!path) {
        usage();
        return 2;
    }
    const std::vector<uint8_t> bytes = tools::readFile(path);
    plugin::RegisterTune tune;
    bool sndh = false;
    if (ymio::looksLikeSndh(bytes)) {
        sndh = true;
        const ymio::SndhParseResult p = ymio::parseSndh(bytes);
        if (!p.file) {
            std::fprintf(stderr, "%s: %s\n", path, p.error.c_str());
            return 1;
        }
        const int s = subsong ? subsong : p.file->info.defaultSubsong;
        auto t = plugin::registerTune(*p.file, s, plugin::sndhImportFrames(*p.file, s));
        if (!t) {
            std::fprintf(stderr, "%s: the routine crashed\n", path);
            return 1;
        }
        tune = std::move(*t);
    } else {
        const ymio::YmParseResult p = ymio::parseYm(bytes);
        if (!p.file) {
            std::fprintf(stderr, "%s: %s\n", path, p.error.c_str());
            return 1;
        }
        tune = plugin::registerTune(*p.file);
    }
    if (dumpFrom >= 0 && diffVoice < 0 && cellTrack < 0) {
        // The frames of the tune: per voice, mixer (t tone, n noise), volume (e envelope), tone period.
        for (int i = dumpFrom; i < dumpTo && i < int(tune.frames.size()); ++i) {
            const auto& r = tune.frames[size_t(i)];
            const auto v = tools::scoreVoices(r, 2'000'000.0 / tune.clockHz);
            std::printf("%6d", i);
            for (const tools::ScoreVoice& x : v)
                std::printf("  %c%c %2s%c %4d", x.tone ? 't' : '.', x.noise ? 'n' : '.',
                            x.env ? "e" : std::to_string(x.vol).c_str(), x.audible ? ' ' : '-', x.tp);
            std::printf("  np %2d ep %5d sh %2d%s\n", r[6] & 31, r[11] | r[12] << 8, r[13],
                        size_t(i) < tune.restart.size() && tune.restart[size_t(i)] ? " R" : "");
        }
        return 0;
    }
    plugin::RegisterGrid grid = plugin::guessRegisterGrid(tune, step);
    if (grids) {
        // The share of the note starts on a step, for each step length.
        for (int s = 1; s <= plugin::kGridMaxFrames; ++s) {
            const plugin::RegisterGrid g = plugin::guessRegisterGrid(tune, s);
            std::printf("S=%-2d offset %-3d %3d%%\n", s, g.offset, g.aligned);
        }
        return 0;
    }
    if (pattern) grid.stepsPerPattern = pattern;
    if (offset != INT32_MIN) grid.offset = offset;

    plugin::NonParamState state;
    const plugin::RegisterImportResult r = plugin::importRegisters(tune, grid, state);
    if (!r.import) {
        std::fprintf(stderr, "%s: %s\n", path, r.error.c_str());
        return 1;
    }
    const plugin::RegisterImport& m = *r.import;
    if (cellTrack >= 0) {
        tools::printCells(state, cellTrack, dumpFrom, dumpTo);
        return 0;
    }
    const auto playback = tools::playImport(m, state);
    if (!playback) return 1;
    const std::vector<tools::Frame>& frames = *playback;

    // Frame i of the tune is frame i - offset of the playback.
    const double k = 2'000'000.0 / tune.clockHz;
    if (diffVoice >= 0) {
        // One voice, frame by frame: the tune, then the playback; * where they differ.
        for (int i = dumpFrom; i < dumpTo && i < int(tune.frames.size()); ++i) {
            const long j = i - grid.offset;
            tools::Frame silent{};
            silent[7] = 0x3f;
            const tools::Frame& played = j >= 0 && j < long(frames.size()) ? frames[size_t(j)] : silent;
            const auto x = tools::scoreVoices(tune.frames[size_t(i)], k)[size_t(diffVoice)];
            const auto y = tools::scoreVoices(played, 1.0)[size_t(diffVoice)];
            std::printf("%6d %4d.%d  %s  |  %s %s\n", i, (i - grid.offset) / grid.framesPerStep,
                        (i - grid.offset) % grid.framesPerStep, tools::showVoice(x).c_str(),
                        tools::showVoice(y).c_str(), tools::sameVoice(x, y) ? "" : "*");
        }
        return 0;
    }
    const std::array<tools::Score, 3> score = tools::scoreFrames(tune.frames, k, frames, grid.offset);
    const tools::Score all = tools::total(score);
    const auto pct = tools::percent;
    const char* name = std::strrchr(path, '/') ? std::strrchr(path, '/') + 1 : path;
    if (quiet) {
        std::printf("%-28s %5zu fr S=%-2d off=%-3d L=%-3d %3d%% al  %2d pat %2d ins %4d notes | exact %5.1f%%  "
                    "snd %5.1f  vol %5.1f  mix %5.1f  per %5.1f  10c %5.1f  np %5.1f\n",
                    name, tune.frames.size(), grid.framesPerStep, grid.offset, grid.stepsPerPattern, grid.aligned,
                    m.patterns, m.instruments, m.notes, pct(all.exact, all.frames), pct(all.agree, all.frames),
                    pct(all.vol, all.both), pct(all.mix, all.both), pct(all.period, all.tonal),
                    pct(all.cents, all.tonal), pct(all.noise, all.noisy));
        return 0;
    }
    std::printf("%s (%s): %zu frames at %u Hz, clock %.0f Hz, loop %d, captured %zu\n", name, sndh ? "SNDH" : "YM",
                tune.frames.size(), tune.rate, tune.clockHz, tune.loopFrame, frames.size());
    std::printf("grid: %d frames per step, offset %d, %d steps per pattern, %d%% aligned, %d notes\n",
                grid.framesPerStep, grid.offset, grid.stepsPerPattern, grid.aligned, grid.notes);
    for (const std::string& l : plugin::registerImportMessage(m, name, sndh)) std::printf("  %s\n", l.c_str());
    for (int c = 0; c < 3; ++c) {
        const tools::Score& s = score[size_t(c)];
        std::printf("voice %c: exact %5.1f%%  sound agrees %5.1f%%  vol %5.1f%%  mix %5.1f%%  period %5.1f%%  "
                    "<10c %5.1f%%  noise %5.1f%%\n",
                    'A' + c, pct(s.exact, s.frames), pct(s.agree, s.frames), pct(s.vol, s.both), pct(s.mix, s.both),
                    pct(s.period, s.tonal), pct(s.cents, s.tonal), pct(s.noise, s.noisy));
    }
    return 0;
}
