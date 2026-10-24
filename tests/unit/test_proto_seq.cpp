// STYM2149 — sequencer playback against the prototype: voice operations (triggers, releases, slides, MIX, FX, step
// panning, pattern switches) dated to the sample, with the internal and the MIDI clock, in 16-channel, 3-channel
// and dual-chip modes.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Data: tests/data/proto/seq.txt, produced by tools/proto_golden/seq.mjs from YM2149_Prototype.html (the
// prototype is not under version control), deviations D-097 to D-106, D-205, D-281, D-343 and D-345 applied to the
// worklet. Each log line: "sample operation voice a b x y z" (trailing zero fields omitted); operations:
//   @ step played (a = step, b = absolute step, x = pattern, y = duration of the step in samples)
//   T trigger (a = generator, b = 1 chord voice, x = note, y = level, z = drum transposition)
//   K cut of the track voice (a = 1: immediate)   R release of a chord voice   O chord silenced
//   S slide armed (a = steps, b = curve, x = target note)   H slide reshaped (a = steps, b = curve)
//   G end of slide (a = 1: note settled on its target, D-105)   N pitch adjusted (x = note)
//   L level of the target note of a slide (x)   M MIX (a = tone, b = noise)   F generator changed (a)
//   P step panning (voice = track, x = pan, 9 = that of the mixer)   Q pattern switch (a)   ! stop
//   A level of a held note (x, D-345)
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ymengine/Engine.h"
#include "ymseq/Pattern.h"

using namespace stym;
using namespace stym::ymengine;

namespace {

struct Msg {
    int block = 0;
    std::string type;
    int arg = 0;
};

struct Case {
    std::string header;
    std::map<std::string, std::string> kv;
    std::vector<double> table;
    std::vector<int> smp;
    std::vector<std::map<std::string, std::string>> tracks;
    std::vector<std::pair<int, int>> patterns;  // number, steps
    std::vector<std::string> lanes;
    std::vector<Msg> msgs;
    std::vector<std::string> log;
    int blocks = 0;
};

std::map<std::string, std::string> keyValues(std::istringstream& ls) {
    std::map<std::string, std::string> kv;
    std::string tok;
    while (ls >> tok) {
        const auto eq = tok.find('=');
        if (eq != std::string::npos) kv[tok.substr(0, eq)] = tok.substr(eq + 1);
    }
    return kv;
}

std::vector<Case> loadCases() {
    std::ifstream in(std::string(STYM_TEST_DATA) + "/proto/seq.txt");
    REQUIRE(in.good());
    std::vector<Case> cases;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string kind;
        ls >> kind;
        if (kind == "case") {
            cases.emplace_back();
            cases.back().header = line;
            std::string id;
            ls >> id;
            cases.back().kv = keyValues(ls);
            continue;
        }
        REQUIRE(!cases.empty());
        Case& c = cases.back();
        if (kind == "table") {
            double x = 0;
            while (ls >> x) c.table.push_back(x);
        } else if (kind == "smp") {
            int n = 0;
            while (ls >> n) c.smp.push_back(n);
        } else if (kind == "track") {
            int i = 0;
            ls >> i;
            c.tracks.push_back(keyValues(ls));
            c.tracks.back()["i"] = std::to_string(i);
        } else if (kind == "pattern") {
            int q = 0, steps = 0;
            ls >> q >> steps;
            c.patterns.emplace_back(q, steps);
        } else if (kind == "lane") {
            c.lanes.push_back(line.substr(5));
        } else if (kind == "msg") {
            Msg m;
            ls >> m.block >> m.type;
            ls >> m.arg;
            c.msgs.push_back(m);
        } else if (kind == "end") {
            ls >> c.blocks;
        } else {
            c.log.push_back(line);
        }
    }
    REQUIRE(!cases.empty());
    return cases;
}

std::vector<int> csvInts(const std::string& s) {
    std::vector<int> out;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) out.push_back(std::stoi(tok));
    return out;
}

// Row k of a track (order of the file: rows of SeqLane, then chord voices, octaves and durations).
void setCell(SeqLane& l, int k, int s, int v) {
    const auto at = size_t(s);
    switch (k) {
    case 0: l.pat[at] = int16_t(v); break;
    case 1: l.oct[at] = int8_t(v); break;
    case 2: l.tie[at] = uint8_t(v); break;
    case 3: l.slide[at] = uint8_t(v); break;
    case 4: l.afl[at] = uint8_t(v); break;
    case 5: l.vol[at] = uint8_t(v); break;
    case 6: l.mix[at] = uint8_t(v); break;
    case 7: l.slt[at] = uint8_t(v); break;
    case 8: l.cut[at] = uint8_t(v); break;
    case 9: l.fx[at] = uint8_t(v); break;
    case 10: l.gat[at] = uint8_t(v); break;
    case 11: l.pnr[at] = uint8_t(v); break;
    case 12: l.rtg[at] = uint8_t(v); break;
    case 13: l.cnd[at] = uint8_t(v); break;
    case 14: l.slc[at] = uint8_t(v); break;
    case 15: l.ton[at] = int8_t(v); break;
    default:
        if (k < 21) l.vpat[size_t(k - 16)][at] = int16_t(v);
        else if (k < 26) l.voct[size_t(k - 21)][at] = int8_t(v);
        else l.vdur[size_t(k - 26)][at] = uint16_t(v);
    }
}

struct Rec {
    uint64_t sample = 0;
    char op = 0;
    int voice = 0, a = 0, b = 0;
    double x = 0, y = 0, z = 0;
};

Rec parseRec(const std::string& line) {
    std::istringstream ls(line);
    Rec r;
    std::string op;
    ls >> r.sample >> op >> r.voice >> r.a >> r.b >> r.x >> r.y >> r.z;
    r.op = op.empty() ? 0 : op[0];
    return r;
}

std::string show(const Engine::SeqLogRecord& r) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%llu %c %d %d %d %.6f %.6f %.6f", static_cast<unsigned long long>(r.sample), r.op,
                  int(r.voice), int(r.a), int(r.b), r.x, r.y, r.z);
    return buf;
}

bool same(const Engine::SeqLogRecord& r, const Rec& g) {
    auto near = [](double a, double b) { return std::fabs(a - b) <= 2e-6 + 1e-9 * std::fabs(b); };
    return r.sample == g.sample && r.op == g.op && r.voice == g.voice && r.a == g.a && r.b == g.b && near(r.x, g.x) &&
           near(r.y, g.y) && near(r.z, g.z);
}

}  // namespace

TEST_CASE("Prototype: sequencer playback (voice log, 16-channel, 3-channel, MIDI clock)", "[proto][seq]") {
    for (const Case& c : loadCases()) {
        INFO(c.header);
        auto num = [&](const char* k) { return std::stod(c.kv.at(k)); };
        Engine e;
        EngineConfig cfg;
        cfg.render.hostRate = 48000;
        cfg.seed = 1;
        e.prepare(cfg);
        GlobalParams g;
        const int mode = int(num("mode"));
        g.mode = mode == 0 ? VoiceMode::Studio16 : mode == 1 ? VoiceMode::Authentic3 : VoiceMode::Dual3;
        g.vblHz = uint32_t(num("vbl"));
        g.timer = TimerMode::Free;
        g.tempo = num("bpm");
        g.swing = num("swing");
        g.resolution = uint8_t(num("res"));
        const bool midi = num("midi") != 0;
        g.seqClock = midi ? SeqClock::Midi : SeqClock::Internal;
        g.arpClock = midi ? ArpClock::MidiClock : ArpClock::Hz;
        g.scaleMask = uint16_t(num("scale"));
        g.scaleRoot = uint8_t(num("root"));
        REQUIRE(c.table.size() == g.slideTable.size());
        for (size_t i = 0; i < c.table.size(); ++i) g.slideTable[i] = float(c.table[i]);
        e.setGlobal(g);
        REQUIRE(c.smp.size() == size_t(kDigidrumSlots));
        for (int k = 0; k < kDigidrumSlots; ++k) {
            std::vector<uint8_t> d(size_t(c.smp[size_t(k)]));
            for (size_t i = 0; i < d.size(); ++i) d[i] = uint8_t((i * size_t(k + 3)) & 15);
            e.setUserSample(k, d);
        }
        for (const auto& kv : c.tracks) {
            auto tn = [&](const char* k) { return std::stod(kv.at(k)); };
            TrackParams t;
            t.patch.generator = Generator(int(tn("gen")));
            t.patch.volume = uint8_t(tn("vol"));
            t.patch.toneOn = tn("tone") != 0;
            t.patch.noiseOn = tn("noise") != 0;
            t.patch.mono = tn("mono") != 0;
            t.patch.sidRatio = tn("sid");
            t.patch.attackMs = tn("atk");
            t.patch.decayMs = tn("dcy");
            t.patch.sustainPct = tn("sus");
            t.patch.releaseMs = tn("rel");
            t.patch.adsrChip = tn("chip") != 0;
            t.mix.enabled = tn("on") != 0;
            t.mix.level = tn("level");
            t.seq.gate = tn("gate");
            t.seq.length = int(tn("plen"));
            t.seq.octave = int(tn("oct"));
            t.arp.on = tn("arp") != 0;
            t.arp.rateHz = tn("rate");
            t.arp.mode = ArpMode(int(tn("amode")));
            t.arp.octaves = uint8_t(tn("aoct"));
            t.arp.length = uint8_t(tn("alen"));
            t.arp.track = tn("track") != 0;
            t.arp.land = tn("land") != 0;
            const auto table = csvInts(kv.at("atable"));
            for (size_t k = 0; k < std::min(table.size(), t.arp.table.size()); ++k) t.arp.table[k] = int8_t(table[k]);
            e.setTrack(int(tn("i")), t);
        }
        SeqData data;
        for (const auto& [q, steps] : c.patterns) data.patterns[size_t(q)].steps = steps;
        for (const auto& ln : c.lanes) {
            std::istringstream ls(ln);
            int q = 0, t = 0;
            ls >> q >> t;
            auto lane = std::make_shared<SeqLane>();
            std::string cell;
            while (ls >> cell) {
                int k = 0, s = 0, v = 0;
                REQUIRE(std::sscanf(cell.c_str(), "%d:%d:%d", &k, &s, &v) == 3);
                setCell(*lane, k, s, v);
            }
            ymseq::migrateArpFlags(*lane);  // the ARP steps of the prototype: Jxx of the ARP row (item 144)
            data.patterns[size_t(q)].lanes[size_t(t)] = lane;
        }
        e.setSequence(&data);
        std::vector<Engine::SeqLogRecord> log;
        log.reserve(c.log.size() + 4096);
        e.setSeqLog(&log);

        std::vector<float> l(128), r(128);
        size_t mi = 0;
        for (int b = 0; b < c.blocks; ++b) {
            for (; mi < c.msgs.size() && c.msgs[mi].block == b; ++mi) {
                const Msg& m = c.msgs[mi];
                if (m.type == "play") e.seqPlay(0);
                else if (m.type == "stop") e.seqStop(0);
                else if (m.type == "queue") e.seqSelectPattern(0, m.arg);
                else if (m.type == "mclk") e.midiClock(0);
                else if (m.type == "mstart") e.midiStart(0);
                else if (m.type == "mstop") e.midiStop(0);
                else if (m.type == "mcont") e.midiContinue(0);
                else if (m.type == "mspp") e.midiSongPosition(0, m.arg);
                else FAIL("unknown message: " << m.type);
            }
            e.render(l.data(), r.data(), 128);
        }
        REQUIRE(mi == c.msgs.size());
        REQUIRE(log.size() < log.capacity());  // complete log
        // STYM_SEQ_DUMP=folder: engine log written per case (seq-<n>.txt), to compare with the reference file.
        if (const char* dir = std::getenv("STYM_SEQ_DUMP")) {
            std::istringstream hs(c.header);
            std::string kw, id;
            hs >> kw >> id;
            std::ofstream dump(std::string(dir) + "/seq-" + id + ".txt");
            for (const auto& rec : log) dump << show(rec) << '\n';
        }

        // First divergence, with the lines that precede it.
        size_t k = 0;
        const size_t n = std::min(log.size(), c.log.size());
        while (k < n && same(log[k], parseRec(c.log[k]))) ++k;
        if (k < n || log.size() != c.log.size()) {
            std::string ctx;
            for (size_t j = k >= 6 ? k - 6 : 0; j < k; ++j) ctx += "  = " + c.log[j] + "\n";
            ctx += "  prototype: " + (k < c.log.size() ? c.log[k] : std::string("(end)")) + "\n";
            ctx += "  engine:    " + (k < log.size() ? show(log[k]) : std::string("(end)")) + "\n";
            for (size_t j = k + 1; j < std::min(k + 4, c.log.size()); ++j) ctx += "  p+ " + c.log[j] + "\n";
            for (size_t j = k + 1; j < std::min(k + 4, log.size()); ++j) ctx += "  e+ " + show(log[j]) + "\n";
            INFO("line " << k << " of " << c.log.size() << " (engine: " << log.size() << ")\n" << ctx);
            FAIL_CHECK("log differs from the prototype");
        }
        SUCCEED();
    }
}
