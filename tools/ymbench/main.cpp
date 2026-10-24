// STYM2149 — ymbench: CPU load of the voice engine (ymengine), as a percentage of the real time of one core.
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Scene: the 16 tracks play in layers (Layer), each with a timer-effect generator (SID, Sync Buzzer, Digidrum,
// Chipdrum) or a modulated chip channel (ornament, vibrato, ADSR), and the notes are replayed at the chosen rate to
// keep all the voices busy. With --gen dma (STE machine), the tracks play the DMA bank: eight looped samples of
// 32 KB, every other one in stereo, at the four rates. With --seq, the sequencer plays a 16-step pattern where each
// track has a chord on every step (the track's note and chord voices 2 to 6), at a free tempo of 120 BPM in 1/16.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ymengine/Engine.h"

using namespace stym;
using ymengine::Engine;

namespace {
void usage() {
    std::fprintf(stderr,
                 "usage: ymbench [options]\n"
                 "  --mode M        16 | 3 | dual (16)\n"
                 "  --notes N       held notes per chord, each played by the 16 tracks; 0: idle engine (2)\n"
                 "  --rate N        output rate (48000)\n"
                 "  --output O      raw | rawbox | retro (raw)\n"
                 "  --timer T       mfp | free (mfp)\n"
                 "  --block N       block size (256)\n"
                 "  --seconds S     rendered duration (20)\n"
                 "  --retrigger S   interval between two chords, in seconds (0.25)\n"
                 "  --seq           the sequencer plays the chords: N voices per track and per step (--notes, 1 to 6)\n"
                 "  --gen G         a single generator for all the tracks: chip | sid | buzzer | digidrum | chipdrum | dma\n"
                 "  --ste           STE machine: in 3 channels, the eight DMA tracks play too (implied by --gen dma)\n"
                 "  --noise         noise on for all the tracks (otherwise every other track)\n"
                 "  --engines N     N independent engines rendered one after the other (1)\n"
                 "  --repeat N      repeated measurements, the fastest one is kept (5)\n"
                 "  --tracks        also renders the 16 track outputs and the 2 chip outputs\n"
                 "  --delay         delay on (D-299), every track sent at 50 %%\n"
                 "  --echo          echo of the notes on every track (D-299): 1/8, 3 repeats, drop of 3\n"
                 "  --hash          hash (FNV-1a 64) of everything rendered in the last pass, counted in the load\n");
}
}  // namespace

int main(int argc, char** argv) {
    ymengine::VoiceMode mode = ymengine::VoiceMode::Studio16;
    int notes = 2;
    uint32_t rate = 48000, block = 256;
    double seconds = 20, retrigger = 0.25;
    ymcore::OutputMode output = ymcore::OutputMode::Raw;
    ymengine::TimerMode timer = ymengine::TimerMode::Mfp;
    int gen = -1;
    bool allNoise = false;
    bool ste = false;
    bool seq = false;
    int engines = 1;
    int repeat = 5;
    bool tracks = false, hash = false, delay = false, echo = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--mode") {
            const std::string m = next();
            mode = m == "3" ? ymengine::VoiceMode::Authentic3 : m == "dual" ? ymengine::VoiceMode::Dual3 : ymengine::VoiceMode::Studio16;
        } else if (a == "--notes") notes = std::clamp(std::atoi(next().c_str()), 0, 8);
        else if (a == "--rate") rate = uint32_t(std::atoi(next().c_str()));
        else if (a == "--output") {
            const std::string o = next();
            output = o == "rawbox" ? ymcore::OutputMode::RawBox : o == "retro" ? ymcore::OutputMode::Retro : ymcore::OutputMode::Raw;
        } else if (a == "--timer") timer = next() == "free" ? ymengine::TimerMode::Free : ymengine::TimerMode::Mfp;
        else if (a == "--block") block = uint32_t(std::max(1, std::atoi(next().c_str())));
        else if (a == "--seconds") seconds = std::atof(next().c_str());
        else if (a == "--retrigger") retrigger = std::atof(next().c_str());
        else if (a == "--gen") {
            const std::string g = next();
            gen = g == "sid" ? 1 : g == "digidrum" ? 2 : g == "buzzer" ? 3 : g == "chipdrum" ? 4 : g == "dma" ? 5 : 0;
            if (gen == int(ymengine::Generator::Dma)) ste = true;
        } else if (a == "--ste") ste = true;
        else if (a == "--noise") allNoise = true;
        else if (a == "--seq") seq = true;
        else if (a == "--engines") engines = std::clamp(std::atoi(next().c_str()), 1, 16);
        else if (a == "--repeat") repeat = std::clamp(std::atoi(next().c_str()), 1, 100);
        else if (a == "--tracks") tracks = true;
        else if (a == "--delay") delay = true;
        else if (a == "--echo") echo = true;
        else if (a == "--hash") hash = true;
        else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    ymengine::EngineConfig cfg;
    cfg.render.hostRate = rate;
    cfg.render.mode = output;
    if (tracks) cfg.trackOutputs = 0xFFFF;  // the separate outputs are only rendered for the prepared tracks
    std::vector<uint8_t> sample(Engine::kMaxUserSample);
    for (size_t i = 0; i < sample.size(); ++i) sample[i] = uint8_t((i * 7 + (i >> 5)) & 15);
    // DMA bank: eight full samples (32 KB), looped so that held notes read to the end.
    std::vector<int8_t> pcm(ymengine::kDmaMaxBytes);
    for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = int8_t(int((i * 13) & 255) - 128);
    ymengine::DmaParams bank;
    for (int k = 0; k < ymengine::kDmaSlots; ++k) bank.slots[size_t(k)] = {uint8_t(k & 3), (k & 1) != 0, 60, true};
    const ymengine::Generator gens[] = {ymengine::Generator::Sid, ymengine::Generator::SyncBuzzer,
                                        ymengine::Generator::Digidrum, ymengine::Generator::Chipdrum,
                                        ymengine::Generator::Chip};
    // Sequencer pattern 0: 16 steps, on each track a chord of `notes` voices per step, shifted by a semitone every
    // other step (rows 0 to 7, also valid for the drums).
    if (seq) notes = std::clamp(notes, 1, 1 + ymengine::kSeqChordVoices);
    ymengine::SeqData data;
    {
        static constexpr int chord[1 + ymengine::kSeqChordVoices] = {0, 4, 2, 6, 3, 5};
        auto lane = std::make_shared<ymengine::SeqLane>();
        for (int s = 0; s < 16; ++s) {
            lane->pat[size_t(s)] = int16_t(chord[0] + (s & 1));
            for (int k = 1; k < notes; ++k) lane->vpat[size_t(k - 1)][size_t(s)] = int16_t(chord[k] + (s & 1));
        }
        data.patterns[0].steps = 16;
        data.patterns[0].lanes.fill(lane);
    }
    double elapsed = 1e300;
    uint64_t maxVoices = 0, voiceSamples = 0;
    uint64_t digest = 0;
    const uint64_t total = uint64_t(seconds * rate);
    for (int rep = 0; rep < repeat; ++rep) {
        std::vector<std::unique_ptr<Engine>> pool;
        for (int k = 0; k < engines; ++k) {
            auto e = std::make_unique<Engine>();
            e->prepare(cfg);
            ymengine::GlobalParams g;
            g.mode = mode;
            g.layer = !seq;
            g.timer = timer;
            g.tempo = 120;
            g.ste = ste;
            g.delay.on = delay;
            e->setGlobal(g);
            for (int s = 0; s < ymengine::kDigidrumSlots; ++s) e->setUserSample(s, sample);
            e->setDma(bank);
            for (int s = 0; s < ymengine::kDmaSlots; ++s) e->setDmaSample(s, pcm);
            for (int t = 0; t < ymengine::kTracks; ++t) {
                ymengine::TrackParams p;
                p.mix.enabled = true;
                p.mix.pan = (t % 5 - 2) * 0.4;
                p.mix.send = delay ? 0.5 : 0.0;
                p.patch.echo.on = echo;
                p.patch.generator = gen >= 0 ? ymengine::Generator(gen) : gens[t % 5];
                p.patch.sidRatio = 1 + t % 3;
                p.patch.noiseOn = allNoise || t % 2 == 0;
                p.patch.vibratoCents = 20;
                p.patch.ornamentOn = t % 3 == 0;
                p.patch.ornamentLength = 4;
                p.patch.ornamentPitch = {0, 12, 7, 3};
                p.patch.attackMs = 5;
                p.patch.decayMs = 100;
                p.patch.sustainPct = 70;
                p.patch.releaseMs = 50;
                p.patch.adsrChip = t % 4 == 1;
                p.patch.digidrumSlot = uint8_t(t & 7);
                p.patch.dmaSlot = int8_t(t & 7);
                p.seq.gate = 0.9;
                e->setTrack(t, p);
            }
            if (seq) {
                e->setSequence(&data);
                e->seqPlay(0);
            }
            pool.push_back(std::move(e));
        }

        const uint64_t every = std::max<uint64_t>(1, uint64_t(retrigger * rate));
        std::vector<float> l(block), r(block);
        std::vector<std::vector<float>> extra(tracks ? ymengine::kTracks + 2 : 0, std::vector<float>(block));
        ymengine::Outputs outs;
        outs.left = l.data();
        outs.right = r.data();
        for (int t = 0; t < (tracks ? ymengine::kTracks : 0); ++t) outs.track[size_t(t)] = extra[size_t(t)].data();
        for (int c = 0; c < (tracks ? 2 : 0); ++c) outs.chip[size_t(c)] = extra[size_t(ymengine::kTracks + c)].data();
        // FNV-1a 64 over the bytes of the samples, in rendering order.
        uint64_t h = 0xcbf29ce484222325ull;
        auto mix = [&h](const float* x, uint32_t count) {
            const auto* b = reinterpret_cast<const unsigned char*>(x);
            for (size_t k = 0; k < size_t(count) * sizeof(float); ++k) h = (h ^ b[k]) * 0x100000001b3ull;
        };
        int chord = 0;
        maxVoices = 0;
        voiceSamples = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (uint64_t at = 0; at < total; at += block) {
            const uint32_t n = uint32_t(std::min<uint64_t>(block, total - at));
            const uint64_t due = (at + every - 1) / every * every;
            const bool trig = due < at + n;
            uint64_t v = 0;
            for (int m = 0; m < engines; ++m) {
                Engine& e = *pool[size_t(m)];
                if (trig && !seq) {
                    const uint32_t off = uint32_t(due - at);
                    const int base = 48 + m;  // engines shifted by a semitone
                    for (int k = 0; k < notes; ++k) e.noteOff(off, base + 7 * k + 5 * ((chord + 1) & 1));
                    for (int k = 0; k < notes; ++k) e.noteOn(off, base + 7 * k + 5 * (chord & 1), 110);
                }
                if (tracks) e.render(outs, n);
                else e.render(l.data(), r.data(), n);
                if (hash) {
                    mix(l.data(), n);
                    mix(r.data(), n);
                    for (const auto& x : extra) mix(x.data(), n);
                }
                v += uint64_t(e.activeVoices());
            }
            if (trig) ++chord;
            maxVoices = std::max(maxVoices, v);
            voiceSamples += v * n;
        }
        elapsed = std::min(elapsed, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        digest = h;
    }
    const double load = 100.0 * elapsed / seconds;
    const double avgVoices = double(voiceSamples) / double(total);
    if (seq) std::printf("sequencer: %d voices per track and per step, 120 BPM in 1/16\n", notes);
    std::printf("%d engine(s), mode %s%s, %u Hz, output %s, timer %s, blocks of %u: %.0f voices average (max %llu)\n", engines,
                mode == ymengine::VoiceMode::Studio16 ? "16 voices" : mode == ymengine::VoiceMode::Dual3 ? "dual chip" : "3ch",
                ste ? " (STE)" : "",
                rate, output == ymcore::OutputMode::Raw ? "raw" : output == ymcore::OutputMode::RawBox ? "rawbox" : "retro",
                timer == ymengine::TimerMode::Mfp ? "mfp" : "free", block, avgVoices, (unsigned long long)maxVoices);
    std::printf("load: %.3f %% of a core (%.3f s for %.1f s of sound)", load, elapsed, seconds);
    if (avgVoices > 0) std::printf(", %.4f %% per voice", load / avgVoices);
    std::printf("\n");
    if (hash) std::printf("hash: %016llx\n", (unsigned long long)digest);
    return 0;
}
