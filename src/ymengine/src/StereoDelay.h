// STYM2149 — ymengine: stereo delay of the audio output (eighth DAW test, item 98, D-299)
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
//
// Delay line per side at the host rate, read with linear interpolation at a time that glides towards the one asked
// (a change of time or tempo bends the pitch of the echoes instead of clicking). The input loses its DC component
// (one-pole high-pass at 10 Hz: the chip outputs are unipolar); what enters the line, the input and the feedback,
// goes through a one-pole low-pass at hiCutHz, so that each repeat is a little darker. Ping-pong: the input enters on
// the left, each side feeds back into the other. Mono (right side null): one line.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ymengine/Params.h"

namespace stym::ymengine::detail {

// Held by the engine (Engine::Impl::delay): prepare() allocates outside the audio thread, set() and process() run on
// it once per render slice of the 16-channel mode.
class StereoDelay {
public:
    // Allocates the lines (kDelayMaxMs and a margin), outside the audio thread.
    void prepare(uint32_t hostRate) {
        rate_ = double(std::max<uint32_t>(1, hostRate));
        // Ring buffers of a power of two (index & mask_).
        uint32_t size = 1;
        while (size < uint32_t(kDelayMaxMs / 1000 * rate_) + 4) size <<= 1;
        mask_ = size - 1;
        lineL_.assign(size, 0.0f);
        lineR_.assign(size, 0.0f);
        // One-pole coefficients: the glide of the time (time constant 60 ms) and the DC blocker (10 Hz).
        glide_ = 1 - std::exp(-1 / (0.06 * rate_));  // about 60 ms
        dcA_ = std::exp(-2 * 3.141592653589793 * 10 / rate_);
        reset();
    }
    void reset() noexcept {
        std::fill(lineL_.begin(), lineL_.end(), 0.0f);
        std::fill(lineR_.begin(), lineR_.end(), 0.0f);
        w_ = 0;
        d_ = -1;
        lpL_ = lpR_ = 0;
        dcInL_ = dcInR_ = dcOutL_ = dcOutR_ = 0;
        quiet_ = mask_ + 1;
    }
    // Settings of the next blocks: time in ms, feedback, low-pass and level of the echoes, ping-pong.
    void set(double ms, double feedback, double hiCutHz, double level, bool pingPong) noexcept {
        target_ = std::clamp(ms, kDelayMinMs, kDelayMaxMs) / 1000 * rate_;
        if (d_ < 0) d_ = target_;  // first block: no glide
        fb_ = std::clamp(feedback, 0.0, kDelayFeedbackMax);
        lpA_ = 1 - std::exp(-2 * 3.141592653589793 * std::clamp(hiCutHz, kDelayHiCutMin, kDelayHiCutMax) / rate_);
        level_ = std::clamp(level, 0.0, 1.0);
        pingPong_ = pingPong;
    }
    // Adds the echoes of the send (inL, inR) to the output (outL, outR) over n samples. Mono: inR and outR null.
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t n) noexcept {
        bool silentIn = true;
        for (uint32_t i = 0; i < n && silentIn; ++i) silentIn = inL[i] == 0 && (!inR || inR[i] == 0);
        // Nothing has entered the lines for longer than they last: they only hold zeros.
        if (silentIn && quiet_ > mask_) {
            w_ = (w_ + n) & mask_;
            d_ = target_;
            return;
        }
        const bool stereo = inR && outR;
        for (uint32_t i = 0; i < n; ++i) {
            // Read d_ samples behind the write position, interpolated between the two nearest samples.
            d_ += (target_ - d_) * glide_;
            double pos = double(w_) - d_;
            if (pos < 0) pos += double(mask_ + 1);
            const auto k = uint32_t(pos);
            const double f = pos - double(k);
            const uint32_t k1 = (k + 1) & mask_;
            const double yL = lineL_[k] + (lineL_[k1] - lineL_[k]) * f;
            const double yR = stereo ? lineR_[k] + (lineR_[k1] - lineR_[k]) * f : 0.0;
            // Input without its DC component (y[n] = x[n] − x[n − 1] + a × y[n − 1]).
            const double xL = inL[i], xR = stereo ? double(inR[i]) : 0.0;
            dcOutL_ = xL - dcInL_ + dcA_ * dcOutL_;
            dcInL_ = xL;
            if (stereo) {
                dcOutR_ = xR - dcInR_ + dcA_ * dcOutR_;
                dcInR_ = xR;
            }
            // What enters each line: the input and the feedback of its own line, or, in ping-pong, both inputs on the
            // left and each line fed back into the other.
            double toL = 0, toR = 0;
            if (!stereo) toL = dcOutL_ + fb_ * yL;
            else if (pingPong_) {
                toL = dcOutL_ + dcOutR_ + fb_ * yR;
                toR = fb_ * yL;
            } else {
                toL = dcOutL_ + fb_ * yL;
                toR = dcOutR_ + fb_ * yR;
            }
            lpL_ += (toL - lpL_) * lpA_;
            if (std::fabs(lpL_) < 1e-12) lpL_ = 0;  // no denormals in the decaying tail
            lineL_[w_] = float(lpL_);
            if (stereo) {
                lpR_ += (toR - lpR_) * lpA_;
                if (std::fabs(lpR_) < 1e-12) lpR_ = 0;
                lineR_[w_] = float(lpR_);
            }
            quiet_ = (lpL_ == 0 && lpR_ == 0) ? std::min(quiet_ + 1, mask_ + 1) : 0;
            w_ = (w_ + 1) & mask_;
            outL[i] += float(yL * level_);
            if (stereo) outR[i] += float(yR * level_);
        }
    }

private:
    std::vector<float> lineL_, lineR_;
    uint32_t mask_ = 0, w_ = 0;
    uint32_t quiet_ = 0;  // samples written as zeros in a row
    double rate_ = 48000;
    double d_ = -1, target_ = 0, glide_ = 0;  // time in samples, its target, glide per sample
    double fb_ = 0, lpA_ = 1, level_ = 1;  // feedback, low-pass coefficient, level of the echoes
    double lpL_ = 0, lpR_ = 0;             // states of the low-pass filters
    double dcA_ = 0, dcInL_ = 0, dcInR_ = 0, dcOutL_ = 0, dcOutR_ = 0;
    bool pingPong_ = false;
};

}  // namespace stym::ymengine::detail
