// M0 test bench (§3.0): option A (Rust crate via C ABI) against option B (C++ port),
// plus the AtariAudio 1.26 reference and an alias-free chain. Outputs: text report + raw files.
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "port_crate.hpp"
#include "ym_tick.hpp"
#include "resampler.hpp"
#include "ym2149c.h"

extern "C" {
struct RustYm;
RustYm* ymffi_new(uint32_t clk, uint32_t sr);
void ymffi_free(RustYm*);
void ymffi_write(RustYm*, uint8_t, uint8_t);
void ymffi_render_i16(RustYm*, int16_t*, size_t);
void* __libc_malloc(size_t);
void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t);
}

// malloc interception, to count the allocations during rendering.
static volatile int g_watch = 0;
static long g_allocs = 0;
extern "C" void* malloc(size_t n) { if (g_watch) ++g_allocs; return __libc_malloc(n); }
extern "C" void* calloc(size_t a, size_t b) { if (g_watch) ++g_allocs; return __libc_calloc(a, b); }
extern "C" void* realloc(void* p, size_t n) { if (g_watch) ++g_allocs; return __libc_realloc(p, n); }

using Regs = std::vector<std::pair<int, int>>;
struct Scenario { const char* name; Regs regs; double seconds; };

static const uint32_t kClk = 2000000;

static std::vector<Scenario> scenarios() {
    return {
        {"tone3", {{0,0x1C},{1,0x01},{2,0xD5},{3,0},{4,0x7B},{5,0x01},{7,0x38},{8,15},{9,12},{10,10}}, 4},
        {"env_saw", {{7,0x3F},{8,0x10},{11,0x90},{12,0x01},{13,0x08}}, 4},
        {"sq10k", {{0,12},{1,0},{7,0x3E},{8,15}}, 4},
        {"hihat", {{6,1},{7,0x37},{8,15}}, 4},
        {"buzzer", {{7,0x3F},{8,0x10},{11,30},{12,0},{13,0x0A}}, 4},
        {"full", {{0,0x1C},{1,0x01},{2,0xD5},{3,0},{4,0x8E},{5,0},{6,5},{7,0x30},{8,15},{9,0x10},{10,13},{11,0xE8},{12,0x03},{13,0x0E}}, 4},
    };
}

static void dump(const std::string& path, const void* data, size_t bytes) {
    FILE* f = std::fopen(path.c_str(), "wb"); std::fwrite(data, 1, bytes, f); std::fclose(f);
}

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    const uint32_t rates[2] = {48000, 44100};
    std::printf("== Fidelity: C++ port of the crate against the Rust crate (FFI), sample by sample ==\n");
    for (uint32_t sr : rates)
        for (auto& sc : scenarios()) {
            const size_t n = size_t(sc.seconds * sr);
            std::vector<int16_t> a(n), b(n), c(n), d(n);
            RustYm* r = ymffi_new(kClk, sr);
            m0::CrateYm2149 p(kClk, sr);
            for (auto [reg, v] : sc.regs) { ymffi_write(r, uint8_t(reg), uint8_t(v)); p.write(reg, uint8_t(v)); }
            ymffi_render_i16(r, a.data(), n);
            for (size_t i = 0; i < n; ++i) b[i] = p.nextSample();
            ymffi_free(r);
            size_t diff = 0, first = n;
            for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) { if (!diff) first = i; ++diff; }
            // Original AtariAudio against our tick port (original box decimation).
            Ym2149c* aa = new Ym2149c();  // zero-initialised (Reset does not clear everything)
            m0::YmTick* t = new m0::YmTick();
            aa->Reset(sr, kClk); t->reset(sr, kClk);
            for (auto [reg, v] : sc.regs) { aa->WritePort(0, uint8_t(reg)); aa->WritePort(2, uint8_t(v)); t->write(reg, uint8_t(v)); }
            for (size_t i = 0; i < n; ++i) { c[i] = aa->ComputeNextSample(); d[i] = t->nextSampleBox(); }
            size_t diff2 = 0; for (size_t i = 0; i < n; ++i) diff2 += c[i] != d[i];
            std::printf("%-8s %6u Hz: crate vs C++ port = %zu deviations / %zu%s; AtariAudio vs tick port = %zu deviations\n",
                        sc.name, sr, diff, n, diff ? (" (first at " + std::to_string(first) + ")").c_str() : "", diff2);
            if (sr == 48000) {
                dump(out + "/" + sc.name + "_crate48.i16", a.data(), n * 2);
                dump(out + "/" + sc.name + "_aabox48.i16", c.data(), n * 2);
                // Proposed chain B: 250 kHz stream -> Kaiser sinc -> 48 kHz.
                const uint32_t savedSeed = m0::YmTick::seed();
                m0::YmTick* u = new m0::YmTick(); u->reset(sr, kClk);
                m0::YmTick::seed() = savedSeed;
                for (auto [reg, v] : sc.regs) u->write(reg, uint8_t(v));
                std::vector<float> ticks(size_t(sc.seconds * kClk / 8));
                for (auto& x : ticks) x = float(u->tick());
                double mean = 0; for (float x : ticks) mean += x; mean /= double(ticks.size());
                for (auto& x : ticks) x = float(x - mean);
                m0::SincResampler rs(kClk / 8.0, sr, 20000, sr - 20000, 100);
                auto y = rs.process(ticks);
                dump(out + "/" + sc.name + "_hq48.f32", y.data(), y.size() * 4);
                delete u;
            }
            delete aa; delete t;
        }

    std::printf("\n== CPU: 60 s of audio, scenario 'full', 48 kHz (best of 3 passes) ==\n");
    const uint32_t sr = 48000; const size_t n = 60 * sr;
    std::vector<int16_t> buf(4096);
    auto bench = [&](const char* label, const std::function<void()>& setup, const std::function<void(size_t)>& render) {
        double best = 1e9; long allocs = 0;
        for (int pass = 0; pass < 3; ++pass) {
            setup();
            g_allocs = 0; g_watch = 1;
            const double t0 = now();
            for (size_t done = 0; done < n; done += buf.size()) render(std::min(buf.size(), n - done));
            const double dt = now() - t0;
            g_watch = 0; allocs += g_allocs;
            if (dt < best) best = dt;
        }
        std::printf("%-44s %8.2f ms for 60 s  -> %6.3f %% of one core; allocations during rendering: %ld\n",
                    label, best * 1000, best / 60 * 100, allocs);
    };
    const Regs full = scenarios()[5].regs;
    RustYm* r = nullptr; m0::CrateYm2149* pc = nullptr; Ym2149c* aa = nullptr; m0::YmTick* tk = nullptr;
    bench("A. Rust crate via C ABI (OR decimation)", [&] { if (r) ymffi_free(r); r = ymffi_new(kClk, sr); for (auto [g, v] : full) ymffi_write(r, uint8_t(g), uint8_t(v)); },
          [&](size_t k) { ymffi_render_i16(r, buf.data(), k); });
    bench("B. C++ port of the crate (OR decimation)", [&] { delete pc; pc = new m0::CrateYm2149(kClk, sr); for (auto [g, v] : full) pc->write(g, uint8_t(v)); },
          [&](size_t k) { for (size_t i = 0; i < k; ++i) buf[i] = pc->nextSample(); });
    bench("Original AtariAudio 1.26 (box average)", [&] { delete aa; aa = new Ym2149c(); aa->Reset(sr, kClk); for (auto [g, v] : full) { aa->WritePort(0, uint8_t(g)); aa->WritePort(2, uint8_t(v)); } },
          [&](size_t k) { for (size_t i = 0; i < k; ++i) buf[i] = aa->ComputeNextSample(); });
    bench("B'. C++ tick port (box average)", [&] { delete tk; tk = new m0::YmTick(); tk->reset(sr, kClk); for (auto [g, v] : full) tk->write(g, uint8_t(v)); },
          [&](size_t k) { for (size_t i = 0; i < k; ++i) buf[i] = tk->nextSampleBox(); });

    // Alias-free chain: cost of the resampler alone, on a precomputed 250 kHz stream.
    {
        m0::YmTick u; u.reset(sr, kClk); for (auto [g, v] : full) u.write(g, uint8_t(v));
        std::vector<float> ticks(size_t(10.0 * kClk / 8)); for (auto& x : ticks) x = float(u.tick());
        for (uint32_t fo : {48000u, 44100u, 96000u}) {
            m0::SincResampler rs(kClk / 8.0, fo, 20000, fo - 20000 > 28000 ? 28000 : fo - 20000, 100);
            double best = 1e9;
            for (int pass = 0; pass < 3; ++pass) { const double t0 = now(); auto y = rs.process(ticks); const double dt = now() - t0; if (dt < best) best = dt; if (y.empty()) return 1; }
            std::printf("Kaiser sinc resampler 100 dB -> %6u Hz (%3d taps)  %6.3f %% of one core per mono channel\n", fo, rs.taps(), best / 10 * 100);
        }
    }
    // 16-channel mode: 96 virtual voices = 32 tick cores at 250 kHz.
    {
        std::vector<m0::YmTick> chips(32);
        for (auto& c : chips) { c.reset(sr, kClk); for (auto [g, v] : full) c.write(g, uint8_t(v)); }
        const int secs = 5; volatile int sink = 0; const double t0 = now();
        for (int s = 0; s < secs * int(kClk / 8); ++s) { int acc = 0; for (auto& c : chips) acc += c.tick(); sink = sink + acc; }
        const double dt = now() - t0;
        std::printf("16 channels x 6 voices (32 tick cores at 250 kHz, no resampling): %6.2f %% of one core\n", dt / secs * 100);
    }
    ymffi_free(r); delete pc; delete aa; delete tk;
    return 0;
}
