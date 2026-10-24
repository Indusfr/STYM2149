// STYM2149 — voice engine against the prototype: tone periods, slide curves and quantisation, chords, arpeggio
// played on the keyboard, VBL frames (ornament, vibrato, "Chip" ADSR, SID, Sync Buzzer), legato.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Data: tests/data/proto/engine.txt, produced by tools/proto_golden/engine.mjs from YM2149_Prototype.html (the
// prototype is not under version control).
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "ymengine/Engine.h"
#include "ymengine/Pitch.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

std::vector<std::string> goldenLines(const std::string& kind) {
    std::ifstream in(std::string(STYM_TEST_DATA) + "/proto/engine.txt");
    REQUIRE(in.good());
    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(kind + " ", 0) == 0) out.push_back(line.substr(kind.size() + 1));
    REQUIRE(!out.empty());
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, sep)) out.push_back(tok);
    return out;
}
std::vector<double> numbers(const std::string& s) {
    std::vector<double> out;
    for (const auto& t : split(s, ',')) out.push_back(std::stod(t));
    return out;
}

// Deviation in units in the last place (same sign).
double ulps(double a, double b) {
    if (a == b) return 0;
    return std::fabs(a - b) / std::max(std::numeric_limits<double>::denorm_min(),
                                        std::nextafter(std::fabs(b), INFINITY) - std::fabs(b));
}

EngineConfig config48() {
    EngineConfig c;
    c.render.hostRate = 48000;
    return c;
}

// Most recent played voice (without fade) of a given tag.
int liveVoice(const Engine& e, VoiceTag tag) {
    for (int i = 0; i < Engine::kVoices; ++i) {
        const auto v = e.voice(i);
        if (v.on && v.tag == tag && !v.fading) return i;
    }
    return -1;
}

void renderTo(Engine& e, uint64_t& at, uint64_t target) {
    static std::vector<float> l(65536), r(65536);
    while (at < target) {
        const uint32_t n = uint32_t(std::min<uint64_t>(target - at, l.size()));
        e.render(l.data(), r.data(), n);
        at += n;
    }
}

}  // namespace

TEST_CASE("Prototype: tone periods of the 128 notes, with pitch bend", "[proto][pitch]") {
    for (const auto& line : goldenLines("tp")) {
        std::istringstream ls(line);
        double bend = 0;
        ls >> bend;
        for (int n = 0; n < 128; ++n) {
            int tp = 0;
            ls >> tp;
            INFO("note " << n << " bend " << bend);
            REQUIRE(tonePeriodFor(noteHz(n + bend), ymcore::clocks::kDefault) == tp);
        }
    }
}

TEST_CASE("Prototype: 13 slide curves", "[proto][pitch]") {
    const auto tableLine = goldenLines("table").front();
    std::vector<float> table;
    for (const auto& t : split(tableLine, ' ')) table.push_back(float(std::stod(t)));
    REQUIRE(table.size() == size_t(kSlideTablePoints));
    double worst = 0;
    for (const auto& line : goldenLines("shape")) {
        const auto f = split(line, ' ');
        REQUIRE(f.size() == 4);
        const auto curve = SlideCurve(std::stoi(f[0]));
        const auto seed = uint32_t(std::stoul(f[1]));
        const auto ts = numbers(f[2]), want = numbers(f[3]);
        REQUIRE(ts.size() == want.size());
        for (size_t i = 0; i < ts.size(); ++i) {
            const double got = slideShape(ts[i], curve, seed, table);
            INFO("curve " << int(curve) << " seed " << seed << " t " << ts[i] << ": " << got << " / " << want[i]);
            // pow, exp, sin and cos of V8 and of the C library can differ by one unit in the last place.
            const double e = std::fabs(got - want[i]) <= 1e-15 ? 0 : ulps(got, want[i]);
            worst = std::max(worst, e);
            REQUIRE(e <= 4);
        }
    }
    INFO("max deviation " << worst << " ulp");
    SUCCEED();
}

TEST_CASE("Prototype: slide quantisation (Chromatic, Scale-locked)", "[proto][pitch]") {
    for (const auto& line : goldenLines("quant")) {
        const auto f = split(line, ' ');
        REQUIRE(f.size() == 5);
        const auto curve = SlideCurve(std::stoi(f[0]));
        const auto mask = uint16_t(std::stoi(f[1]));
        const auto root = uint8_t(std::stoi(f[2]));
        const auto fs = numbers(f[3]), want = numbers(f[4]);
        for (size_t i = 0; i < fs.size(); ++i) {
            INFO("curve " << int(curve) << " scale " << mask << " tonic " << int(root) << " f " << fs[i]);
            REQUIRE(ulps(slideQuantize(fs[i], curve, mask, root), want[i]) <= 2);
        }
    }
}

TEST_CASE("Prototype: 39 chords", "[proto][pitch]") {
    const auto lines = goldenLines("chord");
    REQUIRE(lines.size() == kChords.size());
    for (const auto& line : lines) {
        std::istringstream ls(line);
        size_t i = 0, n = 0;
        ls >> i >> n;
        const Chord& c = kChords[i];
        REQUIRE(c.count == n);
        for (size_t k = 0; k < n; ++k) {
            int o = 0;
            ls >> o;
            REQUIRE(c.offsets[k] == o);
        }
        std::string name;
        std::getline(ls >> std::ws, name);
        REQUIRE(c.name == name);
    }
}

TEST_CASE("Prototype: arpeggio played on the keyboard, 6 modes", "[proto][arp]") {
    for (const auto& line : goldenLines("arp")) {
        const auto f = split(line, ' ');
        REQUIRE(f.size() == 6);
        Engine e;
        e.prepare(config48());
        TrackParams t;
        t.mix.enabled = true;
        t.arp.on = true;
        t.arp.mode = ArpMode(std::stoi(f[0]));
        t.arp.octaves = uint8_t(std::stoi(f[1]));
        t.arp.length = uint8_t(std::stoi(f[2]));
        const auto table = numbers(f[3]);
        REQUIRE(table.size() == t.arp.table.size());
        for (size_t i = 0; i < t.arp.table.size(); ++i) t.arp.table[i] = int8_t(table[i]);
        t.arp.rateHz = 48000.0 / 1024;  // one step every 1 024 samples, exact phase
        e.setTrack(0, t);
        for (double n : numbers(f[4])) e.noteOn(0, int(n), 100);
        const auto want = numbers(f[5]);
        uint64_t at = 0;
        for (size_t k = 0; k < want.size(); ++k) {
            renderTo(e, at, (k + 1) * 1024);  // step k falls on the last sample of the block
            const int v = liveVoice(e, VoiceTag::Arp);
            INFO("arp " << line << " step " << k);
            REQUIRE(v >= 0);
            REQUIRE(e.voice(v).note == int(want[k]));
        }
    }
}

TEST_CASE("Prototype: VBL frames (ornament, vibrato, Chip ADSR, SID, Sync Buzzer)", "[proto][vbl]") {
    for (const auto& line : goldenLines("vbl")) {
        std::map<std::string, std::string> kv;
        for (const auto& tok : split(line, ' ')) {
            const auto eq = tok.find('=');
            kv[tok.substr(0, eq)] = tok.substr(eq + 1);
        }
        auto num = [&](const char* k) { return std::stod(kv.at(k)); };
        Engine e;
        e.prepare(config48());
        GlobalParams g;
        g.vblHz = uint32_t(num("vbl"));
        e.setGlobal(g);
        TrackParams t;
        t.mix.enabled = true;
        TrackPatch& p = t.patch;
        p.generator = Generator(int(num("effect")));
        p.sidRatio = num("sidRatio");
        p.volume = uint8_t(num("vol4"));
        p.vibratoCents = num("vibDepth");
        p.vibratoHz = num("vibRate");
        p.ornamentOn = num("ornOn") != 0;
        p.ornamentLength = uint8_t(num("ornLen"));
        p.ornamentLoop = num("ornLoop") != 0;
        p.ornamentRate = uint8_t(num("ornRate"));
        const auto op = numbers(kv.at("ornTable")), on = numbers(kv.at("ornNse")), ov = numbers(kv.at("ornVol"));
        REQUIRE(op.size() <= p.ornamentPitch.size());
        REQUIRE(on.size() <= p.ornamentNoise.size());
        REQUIRE(ov.size() <= p.ornamentVolume.size());
        for (size_t i = 0; i < std::min(op.size(), p.ornamentPitch.size()); ++i) p.ornamentPitch[i] = int8_t(op[i]);
        for (size_t i = 0; i < std::min(on.size(), p.ornamentNoise.size()); ++i) p.ornamentNoise[i] = uint8_t(on[i]);
        for (size_t i = 0; i < std::min(ov.size(), p.ornamentVolume.size()); ++i) p.ornamentVolume[i] = uint8_t(ov[i]);
        p.attackMs = num("atk");
        p.decayMs = num("dcy");
        p.sustainPct = num("sus");
        p.releaseMs = num("rel");
        p.adsrChip = num("adsrChip") != 0;
        e.setTrack(0, t);
        e.noteOn(0, int(num("note")), 127);

        const auto frames = split(kv.at("frames"), ',');
        const double frame = 48000.0 / num("vbl");
        uint64_t at = 0;
        for (size_t k = 0; k < frames.size(); ++k) {
            renderTo(e, at, uint64_t((double(k) + 0.5) * frame));  // middle of frame k
            std::vector<double> w;
            for (const auto& x : split(frames[k], ':')) w.push_back(std::stod(x));
            REQUIRE(w.size() == 4);
            const int v = liveVoice(e, VoiceTag::Manual);
            INFO(line.substr(0, 160) << "\nframe " << k);
            REQUIRE(v >= 0);
            const auto info = e.voice(v);
            REQUIRE(info.tonePeriod == int(w[0]));
            REQUIRE(info.volume == int(w[1]));
            REQUIRE(info.noise == (w[2] != 0));
            if (w[3] != 0) REQUIRE(std::fabs(info.timerHz - w[3]) <= 1e-12 * w[3]);
        }
    }
}

TEST_CASE("Prototype: legato of a monophonic track (45 ms slide)", "[proto][glide]") {
    for (const auto& line : goldenLines("glide")) {
        std::istringstream ls(line);
        int from = 0, to = 0, vbl = 0;
        std::string list;
        ls >> from >> to >> vbl >> list;
        const auto want = numbers(list);
        Engine e;
        e.prepare(config48());
        GlobalParams g;
        g.vblHz = uint32_t(vbl);
        e.setGlobal(g);
        TrackParams t;
        t.mix.enabled = true;
        t.patch.mono = true;
        e.setTrack(0, t);
        e.noteOn(0, from, 127);
        uint64_t at = 0;
        renderTo(e, at, 100);
        e.noteOn(0, to, 127);
        const double step = 48000.0 / (16.0 * vbl);  // one slide step, in samples
        const size_t steps = size_t(want[0]);
        REQUIRE(want.size() == steps + 1);
        for (size_t j = 1; j <= steps; ++j) {
            renderTo(e, at, 100 + uint64_t((double(j) + 0.5) * step));
            const int v = liveVoice(e, VoiceTag::Manual);
            INFO(line.substr(0, 40) << " step " << j);
            REQUIRE(v >= 0);
            REQUIRE(e.activeVoices() == 1);
            REQUIRE(e.voice(v).note == to);
            REQUIRE(e.voice(v).tonePeriod == int(want[j]));
        }
    }
}
