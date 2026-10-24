// M0 trial: Kaiser-windowed sinc resampler, any ratio (clk/8 -> host rate).
#pragma once
#include <cmath>
#include <vector>
#include <cstddef>

namespace m0 {
inline double besselI0(double x) { double s = 1, t = 1; for (int k = 1; k < 64; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-20 * s) break; } return s; }

class SincResampler {
public:
    // passHz: passband kept; stopHz: start of the stopband; attenDb: target attenuation.
    SincResampler(double fin, double fout, double passHz, double stopHz, double attenDb, int phases = 1024)
        : fin_(fin), fout_(fout), P_(phases) {
        const double dw = 2 * M_PI * (stopHz - passHz) / fin;
        const double beta = attenDb > 50 ? 0.1102 * (attenDb - 8.7) : 0.5842 * std::pow(attenDb - 21, 0.4) + 0.07886 * (attenDb - 21);
        int n = int(std::ceil((attenDb - 8) / (2.285 * dw)));
        K_ = (n + 1) / 2; if (K_ < 2) K_ = 2;
        const double fc = 0.5 * (passHz + stopHz) / fin;  // normalised cutoff frequency
        table_.assign(size_t(P_ + 1) * 2 * K_, 0.f);
        const double i0b = besselI0(beta);
        for (int p = 0; p <= P_; ++p)
            for (int k = 0; k < 2 * K_; ++k) {
                const double tau = double(p) / P_ + (K_ - 1) - k;
                const double r = tau / K_;
                const double w = std::fabs(r) >= 1 ? 0 : besselI0(beta * std::sqrt(1 - r * r)) / i0b;
                const double x = 2 * fc * tau;
                const double s = std::fabs(x) < 1e-12 ? 1 : std::sin(M_PI * x) / (M_PI * x);
                table_[size_t(p) * 2 * K_ + k] = float(2 * fc * s * w);
            }
    }
    int taps() const { return 2 * K_; }
    // Offline processing of a whole stream.
    std::vector<float> process(const std::vector<float>& x) const {
        std::vector<float> y;
        const double step = fin_ / fout_;
        y.reserve(size_t(x.size() / step) + 1);
        const int T = 2 * K_;
        for (double t = K_; t + K_ + 1 < double(x.size()); t += step) {
            const long it = long(t);
            const double frac = t - double(it);
            const double pf = frac * P_;
            const int p = int(pf);
            const float a = float(pf - p);
            const float* h0 = &table_[size_t(p) * T];
            const float* h1 = h0 + T;
            const float* xs = &x[size_t(it - (K_ - 1))];
            float acc0 = 0, acc1 = 0;
            for (int k = 0; k < T; ++k) { acc0 += xs[k] * h0[k]; acc1 += xs[k] * h1[k]; }
            y.push_back(acc0 + a * (acc1 - acc0));
        }
        return y;
    }

private:
    double fin_, fout_; int P_, K_ = 0; std::vector<float> table_;
};
}  // namespace m0
