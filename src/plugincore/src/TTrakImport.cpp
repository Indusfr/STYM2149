// STYM2149 — plugincore: import of TTrak modules (.TPM) as editable patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/TTrakImport.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "plugincore/Digidrum.h"
#include "ymengine/Mfp68901.h"
#include "ymio/TTrak.h"
#include "ymio/TTrakPlayer.h"

namespace stym::plugin {

using ymengine::Generator;

namespace {

// A4 of TTrak's square periods: note 46 (MIDI 69, pitch 736) of the 2 MHz chip, about 438.84 Hz.
double ttrakA4() { return 2'000'000.0 / (16.0 * ymio::kTtrakPeriod0) * std::pow(2.0, 46.0 / 12.0); }

// Multiples of kFmMultiples (x0.5, x1, x1.5, x2, x3, x4, x5, x6) of TTrak's factors x1, x1/2, x1/3, x2/3 of the period.
constexpr std::array<uint8_t, 4> kTtrakFmMultiple{1, 3, 4, 2};

}  // namespace

uint8_t ttrakFmMultiple(uint8_t factor) noexcept { return kTtrakFmMultiple[factor & 3]; }

int ttrakFmFactor(uint8_t multiple) noexcept {
    for (size_t f = 0; f < kTtrakFmMultiple.size(); ++f)
        if (kTtrakFmMultiple[f] == multiple) return int(f);
    return -1;
}

TtrakTune ttrakTune(const ymio::TtrakModule& module) {
    TtrakTune out;
    RegisterTune& t = out.tune;
    RegisterLayout& l = out.layout;
    t.title = module.titleText();
    t.author = module.authorText();
    t.rate = module.rate ? module.rate : 50;
    t.clockHz = 2'000'000;
    const size_t max = size_t(kRegisterImportMaxSeconds) * t.rate;
    ymio::TtrakPlayer p(module);
    std::vector<int> stepAt(ymio::kTtrakPositions, -1);  // first step (row) of each position played
    std::array<double, 3> digiPos{};                     // samples of each channel's digidrum played
    std::array<bool, 3> digiStart{};                      // the sample starts when the timer next plays
    std::array<int, ymio::kTtrakSampleSlots> sampleOf;    // Digidrum slot of the import of each TTrak sample slot
    sampleOf.fill(-1);
    std::array<std::vector<uint8_t>, ymio::kTtrakSampleSlots> levels;
    for (size_t s = 0; s < levels.size(); ++s) levels[s] = ymio::ttrakSampleLevels(module.samples[s]);
    while (p.loops() == 0) {
        if (t.frames.size() == max) {
            ++t.cutFrames;  // counted up to the loop, or up to twice the limit
            if (t.cutFrames >= max) break;
            p.tick();
            continue;
        }
        const ymio::TtrakTick k = p.tick();
        const int frame = int(t.frames.size());
        if (k.rowStart) {
            if (k.row == 0 || l.partSteps.empty()) {
                if (stepAt[size_t(k.position)] < 0) stepAt[size_t(k.position)] = int(l.stepFrames.size());
                l.partSteps.push_back(0);
            }
            l.stepFrames.push_back(0);
            ++l.partSteps.back();
        }
        ++l.stepFrames.back();
        // The registers as the routine computed them (a timer effect plays over its volume or tone period), the shape
        // as the chip holds it.
        std::array<uint8_t, 14> r{};
        std::copy(k.buffer.begin(), k.buffer.end(), r.begin());
        r[13] = k.reg[13];
        t.frames.push_back(r);
        t.restart.push_back(k.shapes > 0);
        // SID, morph, sync buzzer, FM and digidrum timers become the engine's.
        bool fx = false;
        std::array<TimerEffect, 3>& effects = t.effects.emplace_back();
        for (int c = 0; c < 3; ++c) {
            const ymio::TtrakChannelTick& ch = k.ch[size_t(c)];
            // A note starts on each row that gives a note or an instrument, and where an instrument hands over to
            // the one it links to (which starts its own tables and timer).
            if (ch.note && (ch.noteRow || ch.instrumentRow || ch.attack))
                l.rows.push_back({c, frame, std::min(127, ch.note + 23)});
            if (ch.sampleStart) {
                digiStart[size_t(c)] = true;
                digiPos[size_t(c)] = 0;
            }
            if (!ch.timer || !ch.timerRuns) continue;
            TimerEffect& x = effects[size_t(c)];
            x.hz = ymengine::MfpSetting{ch.timerPrescaler, uint16_t(ch.timerData ? ch.timerData : 256)}.hz();
            switch (ymio::TtrakTimer(ch.timer)) {
            case ymio::TtrakTimer::Sid:
            case ymio::TtrakTimer::Sid2:
            case ymio::TtrakTimer::Sid16:
            case ymio::TtrakTimer::Morph:
                x.gen = Generator::Sid;
                x.steps = ch.timerSteps;
                for (int i = 0; i < ch.timerSteps; ++i)
                    x.wave[size_t(i)] = std::min<uint8_t>(15, ch.timerWave[size_t(i)]);
                break;
            case ymio::TtrakTimer::SyncBuzzer:
                x.gen = Generator::SyncBuzzer;
                x.steps = ch.timerSteps;
                for (int i = 0; i < ch.timerSteps; ++i) x.wave[size_t(i)] = uint8_t(ch.timerWave[size_t(i)] & 15);
                break;
            case ymio::TtrakTimer::Fm:
            case ymio::TtrakTimer::FmSync:
                // Each step divides the note's period by 1, 2, 3 or 3/2: multiples x1, x2, x3, x1.5 of FM Voice.
                x.gen = Generator::Fm;
                x.steps = ch.timerSteps;
                for (int i = 0; i < ch.timerSteps; ++i) x.wave[size_t(i)] = ttrakFmMultiple(ch.timerWave[size_t(i)]);
                break;
            case ymio::TtrakTimer::Digidrum: {
                // The interrupt plays the slot's volumes up to its end byte, then stops the timer.
                const int slot = ch.sampleSlot % ymio::kTtrakSampleSlots;
                const std::vector<uint8_t>& s = levels[size_t(slot)];
                double& pos = digiPos[size_t(c)];
                if (s.empty() || pos >= double(s.size())) break;
                int& sample = sampleOf[size_t(slot)];
                if (sample < 0 && out.sampleSlots.size() < t.samples.size()) {
                    sample = int(out.sampleSlots.size());
                    out.sampleSlots.push_back(slot);
                    t.samples[size_t(sample)] = digidrumSourceOfLevels(s);
                }
                pos += x.hz / t.rate;
                if (sample < 0) {
                    fx = true;  // beyond eight samples
                    break;
                }
                x.gen = Generator::Digidrum;
                x.sample = uint8_t(sample);
                x.start = digiStart[size_t(c)];
                digiStart[size_t(c)] = false;
                break;
            }
            default: fx = true; break;
            }
            if (x.gen == Generator::Chip) x = {};
        }
        t.effectFrames += fx;
    }
    if (p.loops() > 0 && p.row() == 0 && stepAt[size_t(p.position())] >= 0) l.loopStep = stepAt[size_t(p.position())];
    t.loopFrame = -1;
    l.a4Hz = ttrakA4();
    return out;
}

RegisterImportResult importTtrak(const ymio::TtrakModule& module, NonParamState& state) {
    const TtrakTune m = ttrakTune(module);
    return importRegisters(m.tune, m.layout, state);
}

std::vector<std::string> ttrakImportMessage(const RegisterImport& r, const std::string& fileName) {
    std::vector<std::string> lines = registerImportMessage(r, fileName, false);
    lines[0] = "TTrak module imported";
    return lines;
}

}  // namespace stym::plugin
