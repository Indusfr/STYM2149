// STYM2149 — S1, S2 and S9 on the reference tunes (tools/fetch_refdata.sh), against the YM player of AtariAudio 1.26.
// Skipped (SKIP) if the tunes are missing, unless STYM_REQUIRE_REFDATA is set (CI).
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

#include "Dsp.h"
#include "Metrics.h"
#include "OracleCommon.h"
#include "RefData.h"
#include "ymcore/ChipRenderer.h"
#include "ymio/YmPlayer.h"

using namespace stym;
using namespace stym::test;

namespace {
constexpr double kSeconds = 60.0;

struct Ours {
    ymio::YmFile file;
    ymcore::ChipRenderer renderer;
    std::unique_ptr<ymio::YmPlayer> player;
};

// Prepares ymcore + ymio with the seed that AtariAudio uses for its Reset.
// Timer effects: AtariAudio model by default (bit-exact comparison), exact model on request (S9c).
std::unique_ptr<Ours> makeOurs(const std::vector<uint8_t>& data, uint32_t rate, ymcore::OutputMode mode, ymcore::NoiseLfsr noise,
                               ymcore::DcFilter dc, uint32_t seed, ymio::YmFxModel fx = ymio::YmFxModel::AtariAudio) {
    auto parsed = ymio::parseYm(data);
    if (!parsed.file) return nullptr;
    auto o = std::make_unique<Ours>();
    o->file = std::move(*parsed.file);
    ymcore::RenderConfig rc;
    rc.hostRate = rate;
    rc.mode = mode;
    rc.dc = dc;
    rc.chip.clock = o->file.clock;
    rc.chip.noise = noise;
    rc.chip.edgeSeed = seed;
    o->renderer.prepare(rc);
    ymio::YmPlayerOptions po;
    po.timing = ymio::YmTiming::AtariAudio;
    po.fx = fx;
    o->player = std::make_unique<ymio::YmPlayer>(o->file, po, rate);
    return o;
}

size_t lengthFor(const Ours& o, uint32_t rate) {
    return size_t(std::min<uint64_t>(o.player->songLengthSamples(), uint64_t(kSeconds * rate)));
}
}  // namespace

TEST_CASE("S1 and S2 on the reference tunes, timer effects included", "[S1][S2][oracle][refdata]") {
    if (refDataDir().empty()) {
        if (refDataRequired()) FAIL("reference tunes missing although STYM_REQUIRE_REFDATA is set");
        SKIP("reference tunes missing: run tools/fetch_refdata.sh");
    }
    uint64_t ticks = 0, tickBad = 0, samples = 0, sampleBad = 0;
    int tunes = 0, fxTunes = 0;
    for (const RefTune& t : refTunes()) {
        const std::vector<uint8_t> data = loadRefTune(t.file);
        REQUIRE_FALSE(data.empty());
        ++tunes;
        fxTunes += t.effects;
        // S1: both players at 250 kHz (one internal step per sample), state compared at each step.
        {
            const uint32_t seed = takeAtariAudioSeed();  // consumed by the AtariAudio Reset below
            auto ours = makeOurs(data, 250000, ymcore::OutputMode::RawBox, ymcore::NoiseLfsr::AtariAudio, ymcore::DcFilter::None, seed);
            REQUIRE(ours);
            YmRenderer* aa = YmRenderer::Create(data.data(), uint32_t(data.size()), 250000, 2000000);
            REQUIRE(aa);
            aa->InitSubSong(1);
            const uint8_t* envBase = aa->m_ym2149.m_pCurrentEnv;
            const size_t n = lengthFor(*ours, 250000);
            std::vector<int16_t> aaOut(1);
            std::vector<float> got(1);
            uint64_t bad = 0;
            std::string first;
            for (size_t i = 0; i < n; ++i) {
                aa->AudioRender(aaOut.data(), 1);
                ours->player->render(ours->renderer, got.data(), 1);
                // AtariAudio writes the timer effects just after the step of the sample; ymcore times them at the next
                // step. Same sound; to compare the state, these writes are applied before the comparison.
                ours->renderer.flushDue();
                std::string d = compareChipState(ours->renderer.chip(), aa->m_ym2149, envBase, true);
                const int ref = atariAudioLastTick(aa->m_ym2149);
                if (d.empty() && int(got[0] * 32768.0f) != ref) d = "output";
                if (!d.empty()) {
                    if (!bad) first = "step " + std::to_string(i) + ": " + d;
                    ++bad;
                }
            }
            AtariAudioRenderer::Destroy(aa);
            ticks += n;
            tickBad += bad;
            INFO(t.file << " (S1) " << first);
            CHECK(bad == 0);
        }
        // S2: RawBox mode against the AtariAudio player, three rates.
        for (uint32_t rate : {44100u, 48000u, 96000u}) {
            const uint32_t seed = takeAtariAudioSeed();  // consumed by the AtariAudio Reset below
            auto ours = makeOurs(data, rate, ymcore::OutputMode::RawBox, ymcore::NoiseLfsr::AtariAudio, ymcore::DcFilter::MovingAverage, seed);
            YmRenderer* aa = YmRenderer::Create(data.data(), uint32_t(data.size()), rate, 2000000);
            REQUIRE(aa);
            aa->InitSubSong(1);
            const size_t n = lengthFor(*ours, rate);
            std::vector<int16_t> ref(n);
            std::vector<float> got(n);
            aa->AudioRender(ref.data(), uint32_t(n));
            for (size_t done = 0; done < n;) {
                const uint32_t k = uint32_t(std::min<size_t>(1024, n - done));
                ours->player->render(ours->renderer, got.data() + done, k);
                done += k;
            }
            AtariAudioRenderer::Destroy(aa);
            uint64_t bad = 0;
            for (size_t i = 0; i < n; ++i) bad += int(got[i] * 32768.0f) != ref[i];
            samples += n;
            sampleBad += bad;
            INFO(t.file << " (S2) at " << rate << " Hz");
            CHECK(bad == 0);
        }
    }
    const std::string set = std::to_string(tunes) + " tunes including " + std::to_string(fxTunes) + " with timer effects";
    metric("S1", "internal state step by step, " + set + " (60 s, " + std::to_string(ticks) + " steps)", std::to_string(tickBad) + " deviation(s)",
           "0", tickBad == 0);
    metric("S2", "RawBox against the AtariAudio YM player, " + set + ", 3 rates (" + std::to_string(samples) + " samples)",
           std::to_string(sampleBad) + " deviation(s)", "0", sampleBad == 0);
}

TEST_CASE("S9 HQ chain against the AtariAudio YM player", "[S9][oracle][refdata]") {
    if (refDataDir().empty()) {
        if (refDataRequired()) FAIL("reference tunes missing although STYM_REQUIRE_REFDATA is set");
        SKIP("reference tunes missing: run tools/fetch_refdata.sh");
    }
    constexpr uint32_t rate = 48000;
    for (const RefTune& t : refTunes()) {
        const std::vector<uint8_t> data = loadRefTune(t.file);
        REQUIRE_FALSE(data.empty());
        // Reference: AtariAudio player (box average, its own LFSR).
        std::vector<double> ref;
        const uint32_t seed = takeAtariAudioSeed();
        {
            YmRenderer* aa = YmRenderer::Create(data.data(), uint32_t(data.size()), rate, 2000000);
            REQUIRE(aa);
            aa->InitSubSong(1);
            auto probe = ymio::parseYm(data);
            REQUIRE(probe.file);
            ymio::YmPlayer len(*probe.file, ymio::YmTiming::AtariAudio, rate);
            const size_t n = size_t(std::min<uint64_t>(len.songLengthSamples(), uint64_t(kSeconds * rate)));
            std::vector<int16_t> pcm(n);
            aa->AudioRender(pcm.data(), uint32_t(n));
            AtariAudioRenderer::Destroy(aa);
            ref.assign(pcm.begin(), pcm.end());
            for (auto& v : ref) v /= 32768.0;
        }
        auto renderHq = [&](ymcore::NoiseLfsr noise, ymio::YmFxModel fx) {
            auto ours = makeOurs(data, rate, ymcore::OutputMode::Raw, noise, ymcore::DcFilter::MovingAverage, seed, fx);
            std::vector<float> y(ref.size() + ours->renderer.latencySamples());
            for (size_t done = 0; done < y.size();) {
                const uint32_t k = uint32_t(std::min<size_t>(1024, y.size() - done));
                ours->player->render(ours->renderer, y.data() + done, k);
                done += k;
            }
            const uint32_t L = ours->renderer.latencySamples();
            return std::vector<double>(y.begin() + L, y.begin() + L + long(ref.size()));
        };
        // The first 0.5 seconds are discarded (start-up of the moving average of the DC component).
        const size_t skip = rate / 2;
        const std::vector<double> refCut(ref.begin() + long(skip), ref.end());

        // S9a: same LFSR as AtariAudio, only the decimation differs → correlation and log-spectral distance.
        const std::vector<double> hqAa = renderHq(ymcore::NoiseLfsr::AtariAudio, ymio::YmFxModel::AtariAudio);
        const std::vector<double> hqAaCut(hqAa.begin() + long(skip), hqAa.end());
        // Correlation over the first 2^20 samples (21.8 s): search for the fractional lag by FFT.
        const size_t nc = std::min<size_t>(size_t(1) << 20, refCut.size());
        const auto [corr, lag] = bestFractionalCorrelation(std::vector<double>(hqAaCut.begin(), hqAaCut.begin() + long(nc)),
                                                           std::vector<double>(refCut.begin(), refCut.begin() + long(nc)), 4);
        // Log-spectral distance over 20 Hz-15 kHz: above that, the AtariAudio box average sags (-1.4 dB at
        // 15 kHz, -2.5 dB at 20 kHz at 48 kHz) and lets what exceeds half the output rate fold back, which the fast
        // writes of the timer effects make audible. The 15-20 kHz band and the full band are published as
        // diagnostics, without a threshold.
        const auto psdHq = welchPsd(hqAaCut, 4096), psdRef = welchPsd(refCut, 4096);
        const double lsd = logSpectralDistanceDb(psdHq, psdRef, rate, 20, 15000);
        metric("S9", std::string(t.file) + " (" + t.role + "): correlation, lag " + fmt(lag, 3), fmt(corr, 6), "≥ 0.99", corr >= 0.99);
        metric("S9", std::string(t.file) + ": log-spectral distance 20 Hz-15 kHz", fmt(lsd, 3) + " dB", "≤ 1 dB", lsd <= 1.0);
        note("S9", std::string(t.file) + ": log-spectral distance against AtariAudio, 15-20 kHz (box decimation) and 20 Hz-20 kHz",
             fmt(logSpectralDistanceDb(psdHq, psdRef, rate, 15000, 20000), 3) + " dB, " +
                 fmt(logSpectralDistanceDb(psdHq, psdRef, rate, 20, 20000), 3) + " dB");

        // S9b (option A): effect of the noise alone. The default HQ mode (maximal LFSR) is compared with the HQ
        // mode with the AtariAudio LFSR: same decimation on both sides, only the noise sequence differs.
        const std::vector<double> hqMax = renderHq(ymcore::NoiseLfsr::Maximal, ymio::YmFxModel::AtariAudio);
        const std::vector<double> hqMaxCut(hqMax.begin() + long(skip), hqMax.end());
        const auto bandsMax = thirdOctaveDb(welchPsd(hqMaxCut, 16384), rate);
        const auto bandsHqAa = thirdOctaveDb(welchPsd(hqAaCut, 16384), rate);
        const auto bandsRef = thirdOctaveDb(welchPsd(refCut, 16384), rate);
        double loudest = -1e9;
        for (const auto& b : bandsHqAa) loudest = std::max(loudest, b.second);
        double worst = 0, worstFc = 0, decim = 0, decimFc = 0;
        for (size_t i = 0; i < bandsHqAa.size(); ++i) {
            if (bandsHqAa[i].second < loudest - 60) continue;  // near-empty bands ignored
            const double d = std::fabs(bandsMax[i].second - bandsHqAa[i].second);
            if (d > worst) {
                worst = d;
                worstFc = bandsHqAa[i].first;
            }
            // Diagnostic (no threshold): deviation due to the decimation, HQ against the AtariAudio player at equal
            // LFSR.
            const double d1 = std::fabs(bandsHqAa[i].second - bandsRef[i].second);
            if (d1 > decim) {
                decim = d1;
                decimFc = bandsHqAa[i].first;
            }
        }
        metric("S9", std::string(t.file) + ": max deviation per third octave, maximal LFSR against AtariAudio LFSR, band " +
                         fmt(worstFc, 4) + " Hz",
               fmt(worst, 3) + " dB", "≤ 1 dB", worst <= 1.0);
        note("S9", std::string(t.file) + ": third octave, HQ against the AtariAudio player at equal LFSR (decimation), band " +
                       fmt(decimFc, 4) + " Hz",
             fmt(decim, 3) + " dB");
        if (t.effects) {
            // S9c: timer effects timed at the MFP cycle (exact model, default) against the AtariAudio model, same HQ
            // chain and same LFSR: only the placement of the effect writes differs.
            const std::vector<double> hqExact = renderHq(ymcore::NoiseLfsr::AtariAudio, ymio::YmFxModel::Exact);
            const std::vector<double> hqExactCut(hqExact.begin() + long(skip), hqExact.end());
            const auto [corrFx, lagFx] = bestFractionalCorrelation(
                std::vector<double>(hqExactCut.begin(), hqExactCut.begin() + long(nc)),
                std::vector<double>(hqAaCut.begin(), hqAaCut.begin() + long(nc)), 4);
            const double lsdFx = logSpectralDistanceDb(welchPsd(hqExactCut, 4096), welchPsd(hqAaCut, 4096), rate, 20, 20000);
            note("S9", std::string(t.file) + ": exact effects against AtariAudio effects, correlation (lag " + fmt(lagFx, 3) + ")",
                 fmt(corrFx, 6));
            note("S9", std::string(t.file) + ": exact effects against AtariAudio effects, log-spectral distance", fmt(lsdFx, 3) + " dB");
        }
        INFO(t.file);
        CHECK(corr >= 0.99);
        CHECK(lsd <= 1.0);
        CHECK(worst <= 1.0);
    }
}
