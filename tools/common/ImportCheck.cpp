// STYM2149 — development tools: a tune imported as patterns, played back by the engine and compared with the tune
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "ImportCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "plugincore/Parameters.h"
#include "plugincore/RegisterExport.h"
#include "ymengine/Sequence.h"

namespace stym::tools {

std::array<ScoreVoice, 3> scoreVoices(const Frame& r, double k) {
    std::array<ScoreVoice, 3> v{};
    for (int c = 0; c < 3; ++c) {
        ScoreVoice& x = v[size_t(c)];
        x.tone = !(r[7] >> c & 1);
        x.noise = !(r[7] >> (3 + c) & 1);
        x.env = (r[size_t(8 + c)] & 16) != 0;
        x.vol = r[size_t(8 + c)] & 15;
        x.tp = int(std::lround((r[size_t(2 * c)] | (r[size_t(2 * c + 1)] & 15) << 8) * k));
        x.np = int(std::lround(std::max(1, r[6] & 31) * k));
        x.audible = x.env || (x.vol > 0 && ((x.tone && x.tp >= 8) || x.noise));
    }
    return v;
}

bool sameVoice(const ScoreVoice& x, const ScoreVoice& y) {
    return x.audible == y.audible &&
           (!x.audible || (x.env == y.env && (x.env || x.vol == y.vol) && x.tone == y.tone && x.noise == y.noise &&
                           (!x.tone || x.tp == y.tp) && (!x.noise || x.np == y.np)));
}

std::string showVoice(const ScoreVoice& v) {
    char b[40];
    std::snprintf(b, sizeof b, "%c%c %2s%c %4d np %2d", v.tone ? 't' : '.', v.noise ? 'n' : '.',
                  v.env ? "e" : std::to_string(v.vol).c_str(), v.audible ? ' ' : '-', v.tp, v.np);
    return b;
}

void Score::add(const Score& s) {
    frames += s.frames;
    agree += s.agree;
    both += s.both;
    vol += s.vol;
    mix += s.mix;
    period += s.period;
    cents += s.cents;
    tonal += s.tonal;
    noise += s.noise;
    noisy += s.noisy;
    exact += s.exact;
}

std::array<Score, 3> scoreFrames(const std::vector<Frame>& tune, double k, const std::vector<Frame>& played,
                                 int offset, const std::vector<std::array<bool, 3>>* skip) {
    std::array<Score, 3> score{};
    Frame silent{};
    silent[7] = 0x3f;
    const long n = long(tune.size());
    for (long i = std::max(0, offset); i < n; ++i) {
        const long j = i - offset;
        const std::array<ScoreVoice, 3> a = scoreVoices(tune[size_t(i)], k);
        const std::array<ScoreVoice, 3> b = scoreVoices(j < long(played.size()) ? played[size_t(j)] : silent, 1.0);
        for (int c = 0; c < 3; ++c) {
            if (skip && size_t(i) < skip->size() && (*skip)[size_t(i)][size_t(c)]) continue;
            Score& s = score[size_t(c)];
            const ScoreVoice& x = a[size_t(c)];
            const ScoreVoice& y = b[size_t(c)];
            ++s.frames;
            if (x.audible != y.audible) continue;
            ++s.agree;
            if (!x.audible) {
                ++s.exact;
                continue;
            }
            ++s.both;
            const bool vol = x.env ? y.env : (!y.env && x.vol == y.vol);
            const bool mix = x.tone == y.tone && x.noise == y.noise;
            bool period = true;
            if (x.tone && x.tp >= 8) {
                ++s.tonal;
                period = x.tp == y.tp;
                if (y.tp > 0 && std::abs(1200 * std::log2(double(x.tp) / y.tp)) < 10) ++s.cents;
            }
            bool noise = true;
            if (x.noise) {
                ++s.noisy;
                noise = x.np == y.np;
            }
            s.vol += vol;
            s.mix += mix;
            s.period += period && x.tone && x.tp >= 8;
            s.noise += noise && x.noise;
            s.exact += vol && mix && period && noise;
        }
    }
    return score;
}

Score total(const std::array<Score, 3>& s) {
    Score all;
    for (const Score& x : s) all.add(x);
    return all;
}

double percent(long a, long b) { return b ? 100.0 * double(a) / double(b) : 100.0; }

plugin::ParamValues importValues(const plugin::RegisterImport& m) {
    plugin::ParamValues values = plugin::defaultValues();
    using plugin::Global;
    using plugin::Track;
    values[size_t(plugin::paramIndex(Global::VoiceMode))] = 1;  // Authentic 3ch
    values[size_t(plugin::paramIndex(Global::VblRate))] = float(m.vblRate);
    values[size_t(plugin::paramIndex(Global::A4Hz))] = m.a4Hz;
    for (int t = 0; t < plugin::kTracks; ++t) {
        values[size_t(plugin::paramIndex(t, Track::On))] = t < 3 ? 1.0f : 0.0f;
        if (t < 3) {
            values[size_t(plugin::paramIndex(t, Track::Generator))] = float(int(ymengine::Generator::Chip));
            values[size_t(plugin::paramIndex(t, Track::Level))] = 100;
        }
    }
    if (m.digidrums) {
        values[size_t(plugin::paramIndex(3, Track::On))] = 1;
        values[size_t(plugin::paramIndex(3, Track::Generator))] = float(int(ymengine::Generator::Digidrum));
        values[size_t(plugin::paramIndex(3, Track::Level))] = 100;
    }
    return values;
}

std::optional<std::vector<Frame>> playImport(const plugin::RegisterImport& m, const plugin::NonParamState& state,
                                             std::vector<Effects>* fx) {
    const auto cap = plugin::captureRegisters(plugin::OfflineRender::Source::Song, importValues(m), state);
    if (!cap) return std::nullopt;
    std::vector<Frame> frames;
    frames.reserve(cap->take.frames.size());
    for (const auto& f : cap->take.frames) frames.push_back(f.reg);
    if (fx) {
        fx->clear();
        for (const auto& f : cap->take.frames) fx->push_back(f.fx);
    }
    return frames;
}

void printCells(const plugin::NonParamState& state, int t, int from, int to) {
    int at = 0;
    for (const auto& e : state.seq.song.entries)
        for (int rep = 0; rep < e.rep; ++rep) {
            const ymseq::Pattern& pat = state.seq.patterns[size_t(e.pat)];
            const ymseq::Lane& l = pat.lane(t);
            for (int k = 0; k < pat.steps; ++k, ++at) {
                if (at < from || at >= to) continue;
                std::printf("%5d p%02d.%03d ", at, e.pat, k);
                if (l.pat[size_t(k)] >= 0)
                    std::printf("n%2d o%2d i%2d m%d ", l.pat[size_t(k)], l.oct[size_t(k)], l.ins[size_t(k)],
                                l.mix[size_t(k)]);
                else std::printf("                ");
                std::printf("%s g%3d", l.tie[size_t(k)] ? "T" : ".", l.gat[size_t(k)]);
                for (int row = int(ymseq::Row::CmdArp); row <= int(ymseq::Row::CmdNoiseCurve); ++row)
                    if (const int v = l.get(ymseq::Row(row), k))
                        std::printf(" %c%02X", ymengine::kSeqCmdLetters[size_t(v >> 8)], v & 255);
                std::printf("\n");
            }
        }
}

}  // namespace stym::tools
