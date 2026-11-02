// STYM2149 — mymimport: maxYMiser songs (.MYS + .MYV) played by our player (ymio/MaxYmPlayer.h), compared tick by
// tick with maxYMiser's own replay routine running in the emulated Atari; or imported as patterns (File > Import >
// maxYMiser) and played back by the engine, compared frame by frame with our player.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// The routine is not part of STYM2149: give the path of gwEm's binary replayer (MYM_REPL.BIN, built from
// github.com/gwEm303/maxYMiser_replay) with --replay, and the tool wraps it with the song and voiceset into an SNDH in
// memory, the way the example of the replayer does; or give an SNDH saved by maxYMiser with --snd, or alone: the song
// is then the one it holds (ymio::maxYmSongsIn). Each tick, the registers that the routine computed (its buffer of
// R0-R12, found in memory by its layout) and those it wrote to the chip are compared with ours. With --import, the
// comparison is the one of ImportCheck.h. With --export, the imported Song is exported again as maxYMiser plays it
// (File > Export song > maxYMiser) and our player's frames of the export are compared with those that the engine
// plays.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ImportCheck.h"
#include "ReadFile.h"
#include "plugincore/MaxYmExport.h"
#include "plugincore/MaxYmImport.h"
#include "ymio/MaxYm.h"
#include "ymio/MaxYmPlayer.h"
#include "ymio/Sndh.h"

using namespace stym;

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: mymimport [options] song.mys voices.myv\n"
                 "       mymimport [options] saved.snd (an SNDH saved by maxYMiser: its song, compared with it)\n"
                 "  --replay FILE   maxYMiser's binary replayer (MYM_REPL.BIN): compare with it\n"
                 "  --snd FILE      an SNDH saved by maxYMiser with this song: compare with it\n"
                 "  --subsong N     subsong of the SNDH (default of the file)\n"
                 "  --ticks N       ticks compared (default: the song once through, at most 30000)\n"
                 "  --dump A B      our ticks A to B\n"
                 "  --diff A B      ticks A to B side by side where they differ\n"
                 "  --import        import the song as patterns, play them back and compare with our player\n"
                 "  --export        import the song, export it again and compare the export played by our player\n"
                 "                  with the engine playing the import\n"
                 "  --out PREFIX    with --export: write the export to PREFIX.MYS and PREFIX.MYV\n"
                 "  --voice V A B   with --import or --export: voice V (0-2) of both, frames A to B\n"
                 "  --cells T A B   with --import: steps A to B of track T (0-2) along the Song\n"
                 "  --quiet         one line per song\n");
}

void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = uint8_t(v >> 8);
    b[at + 1] = uint8_t(v);
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    put16(b, at, uint16_t(v >> 16));
    put16(b, at + 2, uint16_t(v));
}
void even(std::vector<uint8_t>& b) {
    if (b.size() & 1) b.push_back(0);
}

// An SNDH holding the replayer, its three offsets and the files: init sets the offsets (voice and song, each from
// its own long, and the song's length) and jumps to the replayer's init; exit and play jump to its entries +4 and +8.
std::vector<uint8_t> wrap(const std::vector<uint8_t>& replay, const std::vector<uint8_t>& myv,
                          const std::vector<uint8_t>& mys, int hz) {
    std::vector<uint8_t> b(12, 0);
    for (const char* tag : {"SNDH", "TITL", "mymimport"}) {
        b.insert(b.end(), tag, tag + std::strlen(tag));
        if (std::strcmp(tag, "mymimport") == 0) b.push_back(0);
    }
    const std::string tc = "TC" + std::to_string(hz);
    b.insert(b.end(), tc.begin(), tc.end());
    b.push_back(0);
    even(b);
    b.insert(b.end(), {'H', 'D', 'N', 'S'});
    const size_t init = b.size();
    const size_t initLen = 4 + 6 + 3 * 6 + 4;
    const size_t exit = init + initLen, play = exit + 4, music = play + 4;
    const size_t offsets = music + replay.size() + (replay.size() & 1);
    const size_t voice = offsets + 12;
    const size_t song = voice + myv.size() + (myv.size() & 1);
    b.resize(music, 0);
    auto bra = [&](size_t at, size_t to) {
        put16(b, at, 0x6000);
        put16(b, at + 2, uint16_t(int(to) - int(at + 2)));
    };
    bra(0, init);
    bra(4, exit);
    bra(8, play);
    size_t at = init;
    put16(b, at, 0x41FA);  // lea music(pc),a0
    put16(b, at + 2, uint16_t(music - (at + 2)));
    at += 4;
    put16(b, at, 0xD1FC);  // adda.l #offsets-music,a0
    put32(b, at + 2, uint32_t(offsets - music));
    at += 6;
    for (uint32_t v : {uint32_t(voice - offsets), uint32_t(song - (offsets + 4)), uint32_t(mys.size())}) {
        put16(b, at, 0x20FC);  // move.l #v,(a0)+
        put32(b, at + 2, v);
        at += 6;
    }
    bra(at, music);
    bra(exit, music + 4);
    bra(play, music + 8);
    b.insert(b.end(), replay.begin(), replay.end());
    even(b);
    b.resize(voice, 0);
    b.insert(b.end(), myv.begin(), myv.end());
    even(b);
    b.insert(b.end(), mys.begin(), mys.end());
    even(b);
    return b;
}

// Registers that the timer effects of a tick drive from their interrupt: the square period of FM and sync square
// channels, the volume of SID, digidrum and pulse width channels, the buzzer period of buzzer FM and its shape under
// a sync buzzer.
uint16_t timerDriven(const ymio::MaxYmTick& t) {
    using T = ymio::MaxYmTimer;
    uint16_t m = 0;
    for (int c = 0; c < 3; ++c) {
        const T k = T(t.ch[size_t(c)].timer);
        if (k == T::SquareFm || k == T::SyncSquare) m = uint16_t(m | 3 << (2 * c));
        if (k == T::Sid || k == T::Digidrum || k == T::PulseWidth) m = uint16_t(m | 1 << (8 + c));
        if (k == T::BuzzerFm || k == T::SyncBuzzerFm) m = uint16_t(m | 3 << 11);
        if (k == T::SyncBuzzer || k == T::SyncBuzzerFm) m = uint16_t(m | 1 << 13);
    }
    return m;
}

// The routine's register buffer: 13 longs whose first bytes are 0 to 12 (register numbers), then its value byte.
long findBuffer(const std::vector<uint8_t>& image) {
    for (size_t o = 0; o + 52 <= image.size(); o += 2) {
        bool ok = true;
        for (int r = 0; r < 13 && ok; ++r) ok = image[o + size_t(4 * r)] == r && image[o + size_t(4 * r) + 1] == 0;
        if (ok) return long(o);
    }
    return -1;
}

std::string regs(const std::array<uint8_t, 14>& r, int n) {
    std::string s;
    char b[8];
    for (int i = 0; i < n; ++i) {
        std::snprintf(b, sizeof b, i ? " %02X" : "%02X", r[size_t(i)]);
        s += b;
    }
    return s;
}

// The song imported as File > Import > maxYMiser does, played back by the engine with the settings that the import
// applies, compared with the frames of our player.
int importMode(const char* name, const ymio::MysSong& song, const ymio::MyvVoiceset& voices, bool quiet, int voice,
               int cells, int from, int to) {
    const plugin::MaxYmTune tune = plugin::maxYmTune(song, voices);
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
    // The voices that a timer effect drives are scored apart: the same generator, and the pitch of its timer within
    // 1 % (a SID: the rate of its waveform; a digidrum, whose rate goes by semitones: within 3 %).
    using ymengine::Generator;
    struct FxScore {
        long frames = 0, gen = 0, hz = 0;
    };
    std::array<FxScore, 3> fxScore{};
    // Every voice that a timer drives is left out of the register score, the effects that STYM2149 does not play
    // (pulse width, buzzer FM, sync square) included.
    std::vector<std::array<bool, 3>> skip(tune.tune.effects.size());
    {
        ymio::MaxYmPlayer p(song, voices);
        for (size_t i = 0; i < skip.size(); ++i) {
            const ymio::MaxYmTick k = p.tick();
            for (int c = 0; c < 3; ++c) skip[i][size_t(c)] = k.ch[size_t(c)].timer && k.ch[size_t(c)].timerRuns;
        }
    }
    for (size_t i = 0; i < tune.tune.effects.size(); ++i)
        for (int c = 0; c < 3; ++c) {
            const plugin::TimerEffect& x = tune.tune.effects[i][size_t(c)];
            if (x.gen == Generator::Chip) continue;
            skip[i][size_t(c)] = true;
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
            if (size_t(i) < tune.tune.effects.size()) {
                const plugin::TimerEffect& t = tune.tune.effects[size_t(i)][size_t(voice)];
                if (t.gen != ymengine::Generator::Chip) {
                    std::snprintf(b, sizeof b, " [%d/%d %.1f]", int(t.gen), t.steps, t.hz);
                    ours = b;
                }
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
    for (const std::string& l : plugin::maxYmImportMessage(m, name)) std::printf("  %s\n", l.c_str());
    // The Song: pattern (steps) x times.
    long steps = 0;
    std::printf("Song:");
    for (const auto& e : state.seq.song.entries) {
        const int n = state.seq.patterns[size_t(e.pat)].steps;
        std::printf(" %d(%d)x%d%s", e.pat, n, e.rep, e.restart ? "|:" : "");
        steps += long(n) * e.rep;
    }
    long expect = 0;
    for (int n : tune.layout.stepFrames) expect += n;
    std::printf("  = %ld steps, %ld frames expected\n", steps, expect);
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

// The import exported again (File > Export song > maxYMiser), played by our player, against the engine playing the
// import: the registers of the voices without a timer effect, and the effect (kind and rate) of the others.
int exportMode(const char* name, const ymio::MysSong& song, const ymio::MyvVoiceset& voices, bool quiet, int voice,
               int from, int to, const char* out) {
    const plugin::MaxYmTune tune = plugin::maxYmTune(song, voices);
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
    const auto x = plugin::exportMaxYm(tools::importValues(*r.import), state);
    if (!x || !x->value) {
        std::fprintf(stderr, "%s: %s\n", name, x ? x->error.c_str() : "cancelled");
        return 1;
    }
    const plugin::MaxYmExport& e = *x->value;
    const auto mys = ymio::parseMys(ymio::writeMys(e.song));
    const auto myv = ymio::parseMyv(ymio::writeMyv(e.voices));
    if (!mys.value || !myv.value) {
        std::fprintf(stderr, "%s: the export does not read back: %s\n", name,
                     (!mys.value ? mys.error : myv.error).c_str());
        return 1;
    }
    if (out) {
        // The export as files, to open in maxYMiser.
        for (const auto& [ext, bytes] : {std::pair{".MYS", ymio::writeMys(e.song)}, {".MYV", ymio::writeMyv(e.voices)}}) {
            const std::string path = std::string(out) + ext;
            FILE* f = std::fopen(path.c_str(), "wb");
            if (!f || std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) {
                std::fprintf(stderr, "%s: cannot write\n", path.c_str());
                if (f) std::fclose(f);
                return 1;
            }
            std::fclose(f);
        }
    }
    ymio::MaxYmPlayer player(*mys.value, *myv.value);
    std::vector<tools::Frame> got;
    std::vector<std::array<std::string, 3>> timers(want.size());
    std::vector<std::array<bool, 3>> skip(want.size());
    using ymengine::Generator;
    long fxFrames = 0, fxKind = 0, fxHz = 0;
    for (size_t i = 0; i < want.size(); ++i) {
        const ymio::MaxYmTick k = player.tick();
        tools::Frame g{};
        std::copy(k.buffer.begin(), k.buffer.end(), g.begin());
        g[13] = k.reg[13];
        got.push_back(g);
        for (int c = 0; c < 3; ++c) {
            const ymengine::RegisterFrame::Effect& f = i < fx.size() ? fx[i][size_t(c)] : ymengine::RegisterFrame::Effect{};
            const ymio::MaxYmChannelTick& ch = k.ch[size_t(c)];
            const bool theirs = ch.timer && ch.timerRuns;
            skip[i][size_t(c)] = f.on || theirs;
            if (f.on || ch.timer) {
                char b[80];
                std::snprintf(b, sizeof b, " [%d %.1f | %X%s %.1f]", int(f.gen), f.on ? f.timer.hz() : 0.0, ch.timer,
                              theirs ? "" : "-", theirs ? ymengine::MfpSetting{ch.timerPrescaler, ch.timerData}.hz() : 0);
                timers[i][size_t(c)] = b;
            }
            if (!f.on) continue;
            ++fxFrames;
            const bool kind = (f.gen == Generator::Sid && ch.timer == uint8_t(ymio::MaxYmTimer::Sid)) ||
                              (f.gen == Generator::SyncBuzzer && ch.timer == uint8_t(ymio::MaxYmTimer::SyncBuzzer)) ||
                              (f.gen == Generator::Digidrum && ch.timer == uint8_t(ymio::MaxYmTimer::Digidrum)) ||
                              (f.gen == Generator::Fm && ch.timer == uint8_t(ymio::MaxYmTimer::SquareFm));
            if (!kind || !theirs) continue;
            ++fxKind;
            const double hz = ymengine::MfpSetting{ch.timerPrescaler, ch.timerData}.hz();
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
    std::printf("%-14s %6zu fr %3d steps %3d pat %2d ins %4d notes | exact %5.1f%%  snd %5.1f  vol %5.1f  mix %5.1f  "
                "per %5.1f  10c %5.1f  np %5.1f | fx %6ld kind %5.1f hz %5.1f\n",
                name, want.size(), e.steps, e.patterns, e.instruments, e.notes, pct(all.exact, all.frames),
                pct(all.agree, all.frames), pct(all.vol, all.both), pct(all.mix, all.both), pct(all.period, all.tonal),
                pct(all.cents, all.tonal), pct(all.noise, all.noisy), fxFrames, pct(fxKind, fxFrames),
                pct(fxHz, fxKind));
    if (!quiet)
        for (const std::string& l : plugin::maxYmExportMessage(e, "song.mys", "voices.myv"))
            std::printf("  %s\n", l.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const char* replayPath = nullptr;
    const char* sndPath = nullptr;
    const char* outPrefix = nullptr;
    std::vector<const char*> files;
    int subsong = 0, ticks = 0, dumpFrom = -1, dumpTo = -1;
    int voice = -1, cells = -1;
    bool diff = false, quiet = false, import = false, exportBack = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--replay") replayPath = next();
        else if (a == "--snd") sndPath = next();
        else if (a == "--subsong") subsong = std::atoi(next());
        else if (a == "--ticks") ticks = std::atoi(next());
        else if (a == "--dump" || a == "--diff") {
            diff = a == "--diff";
            dumpFrom = std::atoi(next());
            dumpTo = std::atoi(next());
        } else if (a == "--voice") {
            voice = std::atoi(next());
            dumpFrom = std::atoi(next());
            dumpTo = std::atoi(next());
        } else if (a == "--cells") {
            cells = std::atoi(next());
            dumpFrom = std::atoi(next());
            dumpTo = std::atoi(next());
        } else if (a == "--import") import = true;
        else if (a == "--export") exportBack = true;
        else if (a == "--out") outPrefix = next();
        else if (a == "--quiet") quiet = true;
        else if (a[0] == '-') {
            usage();
            return 2;
        } else files.push_back(argv[i]);
    }
    if (files.size() != 2 && files.size() != 1) {
        usage();
        return 2;
    }
    std::vector<uint8_t> mysBytes, myvBytes;
    if (files.size() == 1) {
        // An SNDH that maxYMiser saved: its song (the subsong's, in the order of the file) compared with its routine.
        const auto sndh = ymio::parseSndh(tools::readFile(files[0]));
        if (!sndh.file) {
            std::fprintf(stderr, "%s\n", sndh.error.c_str());
            return 1;
        }
        const std::vector<ymio::MaxYmSong> songs = ymio::maxYmSongsIn(sndh.file->image);
        const int k = (subsong ? subsong : sndh.file->info.defaultSubsong) - 1;
        if (k < 0 || size_t(k) >= songs.size()) {
            std::fprintf(stderr, "%s: %zu maxYMiser song(s) in the file, none for this subsong\n", files[0],
                         songs.size());
            return 1;
        }
        mysBytes = ymio::writeMys(songs[size_t(k)].song);
        myvBytes = ymio::writeMyv(songs[size_t(k)].voices);
        if (!replayPath && !sndPath) sndPath = files[0];
    } else {
        mysBytes = tools::readFile(files[0]);
        myvBytes = tools::readFile(files[1]);
    }
    const auto song = ymio::parseMys(mysBytes);
    const auto voices = ymio::parseMyv(myvBytes);
    if (!song.value || !voices.value) {
        std::fprintf(stderr, "%s\n", (!song.value ? song.error : voices.error).c_str());
        return 1;
    }
    const char* name = std::strrchr(files[0], '/') ? std::strrchr(files[0], '/') + 1 : files[0];
    if (exportBack) return exportMode(name, *song.value, *voices.value, quiet, voice, dumpFrom, dumpTo, outPrefix);
    if (import) return importMode(name, *song.value, *voices.value, quiet, voice, cells, dumpFrom, dumpTo);
    // The routine plays a song saved in jam mode in that mode: so does our player here.
    const bool jam = ymio::MaxYmPlayer(*song.value, *voices.value).savedInJamMode();
    if (jam && !quiet) std::printf("saved in jam mode: played in that mode\n");
    // Ticks of the song once through: until it goes back to its repeat step.
    if (!ticks) {
        ymio::MaxYmPlayer p(*song.value, *voices.value);
        p.setJamMode(jam);
        while (p.loops() == 0 && ticks < 30000) {
            p.tick();
            ++ticks;
        }
    }
    ymio::MaxYmPlayer ours(*song.value, *voices.value);
    ours.setJamMode(jam);
    std::vector<ymio::MaxYmTick> mine;
    for (int i = 0; i < ticks; ++i) mine.push_back(ours.tick());
    if (dumpFrom >= 0 && !diff) {
        for (int i = dumpFrom; i < dumpTo && i < ticks; ++i) {
            const ymio::MaxYmTick& t = mine[size_t(i)];
            std::printf("%5d %02X.%02X%c buf %s | chip %s%s", i, t.step, t.row, t.rowStart ? '*' : ' ',
                        regs([&] {
                            std::array<uint8_t, 14> x{};
                            std::copy(t.buffer.begin(), t.buffer.end(), x.begin());
                            return x;
                        }(), 13).c_str(),
                        regs(t.reg, 14).c_str(), t.shapeWritten ? " R" : "");
            for (const auto& c : t.ch)
                if (c.timer) std::printf(" [%X%s %d]", c.timer, c.timerRuns ? "" : "-", c.timerPeriod);
            std::printf("\n");
        }
        return 0;
    }
    if (!replayPath && !sndPath) {
        std::printf("%d ticks played (no routine to compare with: --replay or --snd)\n", ticks);
        return 0;
    }

    std::vector<uint8_t> image;
    int hz = song.value->header[14] ? song.value->header[14] : 50;
    if (replayPath) {
        const std::vector<uint8_t> bin = tools::readFile(replayPath);
        if (bin.empty()) {
            std::fprintf(stderr, "%s: cannot read\n", replayPath);
            return 1;
        }
        image = wrap(bin, myvBytes, mysBytes, hz);
    } else {
        image = tools::readFile(sndPath);
    }
    const auto sndh = ymio::parseSndh(image);
    if (!sndh.file) {
        std::fprintf(stderr, "%s\n", sndh.error.c_str());
        return 1;
    }
    const long buf = findBuffer(sndh.file->image);
    if (buf < 0) {
        std::fprintf(stderr, "the routine's register buffer is not found\n");
        return 1;
    }
    const uint32_t base = ymio::kSndhLoadAddress + uint32_t(buf);
    const int s = subsong ? subsong : sndh.file->info.defaultSubsong;
    ymio::SndhCapture cap(*sndh.file, s, 50000);
    const uint64_t spt = cap.samplesPerTick();
    std::array<uint8_t, 14> chip{};
    std::vector<ymio::SndhEvent> events;
    std::array<long, 14> badBuf{}, badChip{};
    long badShape = 0, compared = 0;
    int firstBad = -1;
    static constexpr uint8_t kMask[14] = {255, 15, 255, 15, 255, 15, 31, 255, 31, 31, 31, 255, 255, 15};
    for (int i = 0; i < ticks && cap.ok(); ++i) {
        events.clear();
        if (!cap.run(uint64_t(i) * spt + 1, events)) break;
        uint64_t tickCycle = 0;
        uint16_t wrote = 0;
        for (const ymio::SndhEvent& e : events) {
            if (e.kind == ymio::SndhEvent::Kind::Tick) {
                tickCycle = e.cycle;
                wrote = 0;
            } else if (e.kind == ymio::SndhEvent::Kind::Write && e.reg < 14 && e.cycle == tickCycle) {
                chip[e.reg] = uint8_t(e.value & kMask[e.reg]);
                wrote = uint16_t(wrote | 1 << e.reg);
            }
        }
        const bool shape = wrote >> 13 & 1;
        std::array<uint8_t, 14> theirs{};
        for (int r = 0; r < 13; ++r) theirs[size_t(r)] = cap.peek(base + uint32_t(4 * r + 2));
        const ymio::MaxYmTick& t = mine[size_t(i)];
        bool same = true;
        for (int r = 0; r < 13; ++r)
            if (theirs[size_t(r)] != t.buffer[size_t(r)]) {
                ++badBuf[size_t(r)];
                same = false;
            }
        // The registers that a timer effect drives are written by its interrupt (and set up by the routine at
        // other moments than ours): only the writes of the other registers are compared.
        const uint16_t driven = timerDriven(t);
        for (int r = 0; r < 14; ++r) {
            if (driven >> r & 1) continue;
            const bool weWrote = t.written >> r & 1, theyWrote = wrote >> r & 1;
            if (weWrote != theyWrote || (weWrote && chip[size_t(r)] != t.reg[size_t(r)])) {
                ++badChip[size_t(r)];
                same = false;
            }
        }
        if (!(driven >> 13 & 1) && shape != t.shapeWritten) {
            ++badShape;
            same = false;
        }
        ++compared;
        if (!same && firstBad < 0) firstBad = i;
        if (diff && i >= dumpFrom && i < dumpTo && !same) {
            std::array<uint8_t, 14> ob{};
            std::copy(t.buffer.begin(), t.buffer.end(), ob.begin());
            std::printf("%5d %02X.%02X buf ours   %s\n               theirs %s\n      chip ours   %s%s\n"
                        "           theirs %s%s\n",
                        i, t.step, t.row, regs(ob, 13).c_str(), regs(theirs, 13).c_str(), regs(t.reg, 14).c_str(),
                        t.shapeWritten ? " R" : "", regs(chip, 14).c_str(), shape ? " R" : "");
        }
    }
    long bufAll = 0, chipAll = 0;
    for (long x : badBuf) bufAll += x;
    for (long x : badChip) chipAll += x;
    std::printf("%-14s %6ld ticks  buffer diffs %6ld  chip diffs %6ld  R13 writes %5ld  first %d\n", name, compared,
                bufAll, chipAll, badShape, firstBad);
    if (!quiet) {
        std::printf("  buffer by register:");
        for (long x : badBuf) std::printf(" %ld", x);
        std::printf("\n  chip by register:  ");
        for (long x : badChip) std::printf(" %ld", x);
        std::printf("\n");
    }
    return 0;
}
