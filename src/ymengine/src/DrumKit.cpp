// STYM2149 — ymengine: drum kit of the Chipdrum generator
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Transcription of the formulas of the prototype (worklet: buildDrums, buildChipDrums; page: KIT_BANKS), in the
// same order of evaluation, to obtain the same values (tests [kit]).
#include "ymengine/DrumKit.h"

#include <algorithm>
#include <cmath>

namespace stym::ymengine {

namespace {
constexpr double kPi = 3.141592653589793;

// JavaScript Math.round: rounds half-way towards +∞.
double jsRound(double x) noexcept { return std::floor(x + 0.5); }
double jsSign(double x) noexcept { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); }

// Linear congruential generator of the prototype (rnd of buildDrums), values in [-1, 1]. In JavaScript, the product
// is a double (rounded above 2^53) before the modulo 2^32 reduction of the & operator: reproduced as is, in two
// separate operations (no contraction into an FMA, see -ffp-contract=off in CMakeLists.txt).
struct ProtoRng {
    uint32_t ns = 12345;
    double next() noexcept {
        const double product = double(ns) * 1103515245.0;
        const double sum = product + 12345.0;  // < 2^62: exact integer, lossless conversion
        ns = uint32_t(uint64_t(sum) & 0x7fffffffu);
        return double(ns) / double(0x3fffffff) - 1.0;
    }
};

// Sample value from -1 to 1 → 4-bit value 0 to 15 (7.5 is the centre).
uint8_t quantize4(double x) noexcept { return uint8_t(std::clamp(jsRound(x * 7.5 + 7.5), 0.0, 15.0)); }
}  // namespace

// Banks of the prototype (KIT_BANKS), fields in the order of DrumParams.
const std::array<KitBank, 13> kKitBanks = {{
    KitBank{"Classic", KitParams{{{{45, 115, 9, 0, 100}, {180, 35, 16, 0, 100}, {10, 35, 26, 0, 100}, {78, 108, 45, 0, 100}, {72, 112, 9, 0, 100}, {90, 134, 95, 0, 100}, {110, 75, 9, 0, 100}, {150, 85, 11, 0, 100}}}}},
    KitBank{"TR-606-ish", KitParams{{{{50, 70, 13, 0, 100}, {220, 25, 20, 0, 100}, {8, 45, 30, 0, 95}, {95, 125, 58, 0, 90}, {92, 120, 12, 0, 90}, {97, 145, 120, 0, 90}, {130, 50, 12, 0, 100}, {175, 55, 13, 0, 100}}}}},
    KitBank{"TR-909-ish", KitParams{{{{48, 180, 11, 12, 100}, {200, 42, 13, 0, 100}, {9, 42, 38, 0, 100}, {86, 118, 48, 0, 92}, {82, 114, 6, 0, 95}, {92, 138, 105, 0, 92}, {105, 95, 11, 5, 100}, {150, 100, 12, 5, 100}}}}},
    KitBank{"Hard EBM", KitParams{{{{38, 220, 7, 45, 100}, {150, 45, 12, 0, 100}, {12, 25, 18, 0, 100}, {70, 88, 76, 0, 88}, {65, 92, 15, 0, 90}, {80, 120, 150, 0, 88}, {95, 110, 8, 35, 100}, {135, 100, 9, 35, 100}}}}},
    KitBank{"Industrial", KitParams{{{{33, 260, 6, 80, 100}, {120, 55, 9, 0, 100}, {14, 20, 14, 0, 100}, {45, 75, 60, 0, 85}, {40, 80, 11, 0, 85}, {55, 105, 190, 0, 85}, {80, 130, 6, 70, 100}, {115, 120, 7, 70, 100}}}}},
    KitBank{"Lo-Fi 4bit", KitParams{{{{55, 60, 14, 20, 90}, {260, 20, 22, 0, 88}, {7, 60, 45, 0, 85}, {60, 95, 35, 0, 75}, {55, 98, 8, 0, 78}, {68, 118, 80, 0, 75}, {140, 35, 15, 15, 90}, {190, 40, 16, 15, 90}}}}},
    KitBank{"Chiptune", KitParams{{{{62, 110, 16, 0, 100}, {300, 15, 24, 0, 95}, {6, 70, 50, 0, 90}, {100, 158, 100, 0, 85}, {100, 152, 20, 0, 85}, {100, 160, 230, 0, 85}, {150, 60, 17, 0, 100}, {210, 60, 18, 0, 100}}}}},
    KitBank{"TR-808-ish", KitParams{{{{30, 90, 5, 0, 100}, {180, 30, 16, 0, 95}, {10, 50, 40, 0, 100}, {90, 140, 52, 0, 82}, {88, 136, 7, 0, 85}, {94, 150, 130, 0, 82}, {75, 45, 7, 0, 100}, {110, 50, 8, 0, 100}}}}},
    KitBank{"House", KitParams{{{{48, 120, 10, 8, 100}, {210, 35, 17, 0, 92}, {9, 55, 45, 0, 100}, {84, 126, 50, 0, 88}, {80, 122, 6, 0, 92}, {90, 142, 115, 0, 86}, {110, 70, 10, 0, 100}, {155, 75, 11, 0, 100}}}}},
    KitBank{"Techno", KitParams{{{{45, 200, 12, 18, 100}, {200, 40, 20, 0, 95}, {11, 38, 26, 0, 98}, {88, 132, 70, 0, 88}, {84, 128, 10, 0, 90}, {93, 148, 180, 0, 88}, {100, 90, 12, 10, 100}, {145, 90, 13, 10, 100}}}}},
    KitBank{"Hip-Hop", KitParams{{{{42, 130, 9, 10, 100}, {190, 38, 14, 0, 100}, {10, 44, 34, 0, 95}, {76, 112, 56, 0, 85}, {72, 108, 9, 0, 88}, {82, 130, 125, 0, 85}, {95, 65, 9, 8, 100}, {135, 70, 10, 8, 100}}}}},
    KitBank{"Trap", KitParams{{{{28, 55, 4, 0, 100}, {245, 26, 18, 0, 98}, {8, 52, 36, 0, 100}, {95, 150, 90, 0, 84}, {92, 145, 5, 0, 88}, {98, 158, 220, 0, 84}, {70, 40, 6, 0, 100}, {105, 45, 7, 0, 100}}}}},
    KitBank{"Drum'n'Bass", KitParams{{{{52, 150, 15, 12, 100}, {285, 50, 12, 0, 100}, {8, 48, 22, 0, 95}, {90, 142, 85, 0, 86}, {86, 138, 11, 0, 88}, {95, 155, 200, 0, 86}, {120, 85, 14, 10, 100}, {170, 85, 15, 10, 100}}}}},
}};

std::span<const uint8_t> DrumKit::sample(int slot) const noexcept {
    if (slot < 0 || slot >= kDrumSlots) return {};
    return {samples_.data() + kOffsets[size_t(slot)], kLengths[size_t(slot)]};
}

std::span<const ChipDrumFrame> DrumKit::frames(int slot) const noexcept {
    if (slot < 0 || slot >= kDrumSlots) return {};
    return {frames_[size_t(slot)].data(), frameCount_[size_t(slot)]};
}

void DrumKit::build(const KitParams& kit, uint32_t vblHz, ymcore::MasterClock clock) noexcept {
    const DrumParams& kick = kit.inst[0];
    const DrumParams& snare = kit.inst[1];
    const DrumParams& clap = kit.inst[2];
    const DrumParams& tom = kit.inst[6];
    const DrumParams& tom2 = kit.inst[7];
    constexpr double R = kSampleRate;

    // ---- 4-bit samples (16-channel mode), in the order of the prototype: the generator and the state of the hats
    // are shared from one instrument to the next.
    ProtoRng rng;
    double ph = 0, ph2 = 0, ph3 = 0, prev = 0, lpC = 0;
    auto fill = [&](int slot, auto&& fn) {
        uint8_t* a = samples_.data() + kOffsets[size_t(slot)];
        for (uint32_t i = 0; i < kLengths[size_t(slot)]; ++i) a[i] = quantize4(fn(double(i) / R));
    };
    auto hat = [&](int slot) {
        const DrumParams& V = kit.inst[size_t(slot)];
        const double mt = V.a / 100, tn = V.b / 100;
        fill(slot, [&](double t) {
            // Cymbal: three inharmonic squares and a little breath.
            const double m = (jsSign(std::sin(6.2832 * 1583 * tn * t)) + jsSign(std::sin(6.2832 * 2145 * tn * t)) +
                              jsSign(std::sin(6.2832 * 2808 * tn * t))) /
                             3;
            const double r = rng.next(), d = r - prev;
            prev = r;
            return (m * mt + d * std::max(0.0, 1.1 - mt)) * std::exp(-t * V.dec) * V.vol / 100;
        });
    };
    const double s1 = clap.a / 1000, gp = 0.004;  // burst and silence of the flam
    fill(0, [&](double t) {                       // kick: swept sine, "noise" = saturation and grain
        const double f = kick.a + kick.b * std::exp(-t * 14);
        ph += 2 * kPi * f / R;
        const double dz = kick.noise / 100;
        const double body = std::max(-1.0, std::min(1.0, std::sin(ph) * (1 + dz * 3)));
        return (body + rng.next() * dz * 0.6) * std::exp(-t * kick.dec) * kick.vol / 100;
    });
    fill(1, [&](double t) {  // snare
        return (rng.next() * 0.75 + std::sin(2 * kPi * snare.a * t) * snare.b / 100) * std::exp(-t * snare.dec) * snare.vol / 100;
    });
    fill(2, [&](double t) {  // clap: three separate bursts, filtered noise
        const double b2 = s1 + gp, b3 = 2 * s1 + 2 * gp, e3 = 3 * s1 + 2 * gp;
        const double g = t < s1 ? 1 : t < b2 ? 0 : t < b2 + s1 ? 0.9 : t < b3 ? 0 : t < e3 ? 0.8 : std::exp(-(t - e3) * clap.dec);
        lpC += (clap.b / 100) * (rng.next() - lpC);
        return lpC * 3.2 * g * clap.vol / 100;
    });
    hat(3);
    hat(4);
    hat(5);
    auto drumTom = [&](const DrumParams& T, double& phase) {
        return [&](double t) {
            const double f = T.a + T.b * std::exp(-t * 12);
            phase += 2 * kPi * f / R;
            const double dz = T.noise / 100;
            const double body = std::max(-1.0, std::min(1.0, std::sin(phase) * (1 + dz * 3)));
            return (body * std::exp(-t * T.dec) + rng.next() * (0.2 + dz * 0.5) * std::exp(-t * (dz != 0 ? 30 : 80))) *
                   T.vol / 100;
        };
    };
    fill(6, drumTom(tom, ph2));
    fill(7, drumTom(tom2, ph3));

    // ---- VBL frames (3-channel mode): tone period, noise, mixer, volume.
    const double F = vblHz ? double(vblHz) : 50.0;
    const double toneClock = clock.hz() / 16;  // 125 000 Hz at 2 MHz, like the prototype
    // tp: tone period of f (20 Hz at least); q: 4-bit volume; nf: frames until exp(−t × dec) falls to exp(−3),
    // about 5 % (2 to 48 frames).
    auto tp = [&](double f) { return uint16_t(std::clamp(jsRound(toneClock / std::max(20.0, f)), 1.0, 4095.0)); };
    auto q = [](double x) { return uint8_t(std::clamp(jsRound(x), 0.0, 15.0)); };
    auto nf = [&](double dec) { return int(std::clamp(jsRound(F * 3 / dec), 2.0, 48.0)); };
    auto make = [&](int slot, int n, auto&& fn) {
        frameCount_[size_t(slot)] = uint8_t(n);
        for (int i = 0; i < n; ++i) frames_[size_t(slot)][size_t(i)] = fn(double(i) / F, i);
    };
    auto pitched = [&](int slot) {
        const DrumParams& P = kit.inst[size_t(slot)];
        make(slot, nf(P.dec), [&](double t, int) {
            const double f = P.a + P.b * std::exp(-t * 14);
            const double nz = P.noise / 100;
            ChipDrumFrame fr;
            fr.tonePeriod = tp(f);
            // Noise 0 to 100 % → noise period 31 down to 11; the noise is heard above 15 %.
            fr.noisePeriod = nz > 0 ? uint8_t(std::max(1.0, jsRound(31 - nz * 20))) : 0;
            fr.tone = true;
            fr.noise = nz > 0.15;
            fr.volume = q(15 * (P.vol / 100) * std::exp(-t * P.dec));
            return fr;
        });
    };
    auto noisy = [&](int slot, uint8_t np, double dec, double vol, int len) {
        make(slot, len, [&](double t, int) {
            ChipDrumFrame fr;
            fr.noisePeriod = np;
            fr.noise = true;
            fr.volume = q(15 * (vol / 100) * std::exp(-t * dec));
            return fr;
        });
    };
    auto chipHat = [&](int slot) {
        const DrumParams& V = kit.inst[size_t(slot)];
        const double dec = V.dec / 5;  // decay per second
        // Tune 0 to 100 % → noise period 31 down to 5.
        noisy(slot, uint8_t(std::max(1.0, jsRound(31 - (V.b / 100) * 26))), dec, V.vol, nf(dec));
    };
    pitched(0);
    make(1, nf(snare.dec), [&](double t, int) {
        ChipDrumFrame fr;
        fr.tonePeriod = tp(snare.a);
        fr.noisePeriod = 12;
        fr.tone = fr.noise = true;
        fr.volume = q(15 * (snare.vol / 100) * std::exp(-t * snare.dec));
        return fr;
    });
    make(2, nf(clap.dec), [&](double t, int i) {  // clap: bursts
        // Odd frames are silent until clap.a / 500 seconds (space): bursts, then a continuous tail.
        const bool on = (i % 2) == 0 || t > clap.a / 500;
        ChipDrumFrame fr;
        fr.noisePeriod = uint8_t(std::max(1.0, jsRound(clap.b / 5)));
        fr.noise = true;
        fr.volume = on ? q(15 * (clap.vol / 100) * std::exp(-t * clap.dec)) : 0;
        return fr;
    });
    chipHat(3);
    chipHat(4);
    chipHat(5);
    pitched(6);
    pitched(7);
}

}  // namespace stym::ymengine
