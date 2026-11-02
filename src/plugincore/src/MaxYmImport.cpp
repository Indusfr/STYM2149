// STYM2149 — plugincore: import of maxYMiser songs (.MYS + .MYV) as editable patterns
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "plugincore/MaxYmImport.h"

#include <algorithm>

#include "ymengine/Mfp68901.h"
#include "ymio/MaxYm.h"
#include "ymio/MaxYmPlayer.h"

namespace stym::plugin {

using ymengine::Generator;

MaxYmTune maxYmTune(const ymio::MysSong& song, const ymio::MyvVoiceset& voices) {
    MaxYmTune out;
    RegisterTune& t = out.tune;
    RegisterLayout& l = out.layout;
    t.rate = song.header[14] ? song.header[14] : 50;
    t.clockHz = 2'000'000;
    const size_t max = size_t(kRegisterImportMaxSeconds) * t.rate;
    ymio::MaxYmPlayer p(song, voices);
    std::vector<int> stepAt(ymio::kMysSteps, -1);  // first step (row) of each song step played
    std::array<double, 3> digiPos{};                // samples of each channel's digidrum played
    while (p.loops() == 0) {
        if (t.frames.size() == max) {
            ++t.cutFrames;  // counted up to the loop, or up to twice the limit
            if (t.cutFrames >= max) break;
            p.tick();
            continue;
        }
        const ymio::MaxYmTick k = p.tick();
        const int frame = int(t.frames.size());
        if (k.rowStart) {
            if (k.row == 0 || l.partSteps.empty()) {
                if (stepAt[size_t(k.step)] < 0) stepAt[size_t(k.step)] = int(l.stepFrames.size());
                l.partSteps.push_back(0);
            }
            l.stepFrames.push_back(0);
            ++l.partSteps.back();
        }
        ++l.stepFrames.back();
        // The registers as the routine computed them (a timer effect plays over its volume or period), the shape as
        // the chip holds it.
        std::array<uint8_t, 14> r{};
        std::copy(k.buffer.begin(), k.buffer.end(), r.begin());
        r[13] = k.reg[13];
        t.frames.push_back(r);
        t.restart.push_back(k.shapeWritten);
        // The SID, sync buzzer, square FM and digidrum effects become those of the engine; the others (pulse
        // width, sync square, buzzer FM, sync buzzer FM) are left out.
        bool fx = false;
        std::array<TimerEffect, 3>& effects = t.effects.emplace_back();
        for (int c = 0; c < 3; ++c) {
            const ymio::MaxYmChannelTick& ch = k.ch[size_t(c)];
            if (ch.attack) l.rows.push_back({c, frame, ch.note});
            else if (ch.cut) l.rows.push_back({c, frame, -1});
            if (!ch.timer || !ch.timerRuns) continue;
            TimerEffect& x = effects[size_t(c)];
            x.hz = ymengine::MfpSetting{ch.timerPrescaler, ch.timerData}.hz();
            switch (ymio::MaxYmTimer(ch.timer)) {
            case ymio::MaxYmTimer::Sid:
                x.gen = Generator::Sid;
                x.steps = ch.timerSteps;
                for (int i = 0; i < ch.timerSteps; ++i)
                    x.wave[size_t(i)] = uint8_t(15 - std::min<int>(15, ch.timerWave[size_t(i)]));
                break;
            case ymio::MaxYmTimer::SyncBuzzer:
                if (!ch.timerSteps) {
                    fx = true;
                    break;
                }
                x.gen = Generator::SyncBuzzer;
                x.steps = ch.timerSteps;
                for (int i = 0; i < ch.timerSteps; ++i) x.wave[size_t(i)] = uint8_t(ch.timerWave[size_t(i)] & 15);
                break;
            case ymio::MaxYmTimer::SquareFm:
                // An FM Voice in semitones: two steps per period of the note, whatever their count.
                if (!ch.timerSteps) {
                    fx = true;
                    break;
                }
                x.gen = Generator::Fm;
                x.semis = true;
                x.steps = ch.timerSteps;
                for (int i = 0; i < ch.timerSteps; ++i) x.wave[size_t(i)] = uint8_t(ch.timerWave[size_t(i)] & 0x7F);
                break;
            case ymio::MaxYmTimer::Digidrum: {
                // The interrupt stops the timer at the end of the sample.
                x.gen = Generator::Digidrum;
                x.sample = uint8_t((ch.digidrum - 1) & 7);
                x.start = ch.digidrumStart;
                double& pos = digiPos[size_t(c)];
                if (x.start) pos = 0;
                if (pos >= double(voices.digidrums[x.sample].size())) x.gen = Generator::Chip;
                pos += x.hz / t.rate;
                break;
            }
            default: fx = true; break;
            }
            if (x.gen == Generator::Chip) x = {};
        }
        t.effectFrames += fx;
        // The STe DMA column is not played; it sounds when the song sends it to the DMA (header byte 49: 1 or 2
        // channels, or the native ones; 0 off, 4 MIDI).
        const ymio::MysStep& e = song.steps[size_t(k.step)];
        const bool dma = song.header[49] >= 1 && song.header[49] <= 3;
        if (dma && k.rowStart && e.dma < ymio::kMysMaxPatterns && e.dma < song.patterns.size() &&
            song.patterns[e.dma].rows[size_t(k.row)].note >= 2)
            t.dac = true;
    }
    if (p.loops() > 0 && p.row() == 0 && stepAt[size_t(p.step())] >= 0) l.loopStep = stepAt[size_t(p.step())];
    for (size_t i = 0; i < t.samples.size() && i < voices.digidrums.size(); ++i)
        for (uint8_t b : voices.digidrums[i]) t.samples[i].push_back(int8_t(b));
    t.loopFrame = -1;
    l.a4Hz = 440;
    return out;
}

RegisterImportResult importMaxYm(const ymio::MysSong& song, const ymio::MyvVoiceset& voices, NonParamState& state) {
    const MaxYmTune m = maxYmTune(song, voices);
    return importRegisters(m.tune, m.layout, state);
}

std::vector<std::string> maxYmImportMessage(const RegisterImport& r, const std::string& fileName) {
    std::vector<std::string> lines = registerImportMessage(r, fileName, false);
    lines[0] = "maxYMiser song imported";
    return lines;
}

}  // namespace stym::plugin
