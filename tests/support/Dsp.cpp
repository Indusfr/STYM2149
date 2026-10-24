// STYM2149 — analysis tools for the tests
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#include "Dsp.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace stym::test {

void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * kPi / double(len) * (inverse ? 1 : -1);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= double(n);
}

static double besselI0(double x) {
    double s = 1, t = 1;
    for (int k = 1; k < 300; ++k) {
        const double q = x / (2.0 * k);
        t *= q * q;
        s += t;
        if (t < 1e-21 * s) break;
    }
    return s;
}

std::vector<double> kaiserWindow(size_t n, double beta) {
    std::vector<double> w(n);
    const double i0 = besselI0(beta);
    for (size_t i = 0; i < n; ++i) {
        const double r = 2.0 * double(i) / double(n - 1) - 1.0;
        w[i] = besselI0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / i0;
    }
    return w;
}

std::vector<double> hannWindow(size_t n) {
    std::vector<double> w(n);
    for (size_t i = 0; i < n; ++i) w[i] = 0.5 - 0.5 * std::cos(2 * kPi * double(i) / double(n));
    return w;
}

std::vector<double> welchPsd(const std::vector<double>& x, size_t nfft) {
    std::vector<double> psd(nfft / 2 + 1, 0.0);
    if (x.size() < nfft) return psd;
    const double mean = std::accumulate(x.begin(), x.end(), 0.0) / double(x.size());
    const std::vector<double> w = hannWindow(nfft);
    std::vector<std::complex<double>> buf(nfft);
    size_t segments = 0;
    for (size_t start = 0; start + nfft <= x.size(); start += nfft / 2) {
        for (size_t i = 0; i < nfft; ++i) buf[i] = (x[start + i] - mean) * w[i];
        fft(buf);
        for (size_t k = 0; k <= nfft / 2; ++k) psd[k] += std::norm(buf[k]);
        ++segments;
    }
    for (auto& p : psd) p /= double(segments);
    return psd;
}

double logSpectralDistanceDb(const std::vector<double>& a, const std::vector<double>& b, double fs, double fLo, double fHi) {
    const size_t nfft = (a.size() - 1) * 2;
    double sum = 0;
    size_t count = 0;
    for (size_t k = 0; k < a.size() && k < b.size(); ++k) {
        const double f = double(k) * fs / double(nfft);
        if (f < fLo || f > fHi) continue;
        const double d = 10 * std::log10((a[k] + 1e-30) / (b[k] + 1e-30));
        sum += d * d;
        ++count;
    }
    return count ? std::sqrt(sum / double(count)) : 0.0;
}

std::vector<std::pair<double, double>> thirdOctaveDb(const std::vector<double>& psd, double fs) {
    const size_t nfft = (psd.size() - 1) * 2;
    std::vector<std::pair<double, double>> bands;
    for (int k = -16; k <= 13; ++k) {  // 25 Hz … 20 kHz
        const double fc = 1000.0 * std::pow(2.0, k / 3.0);
        const double lo = fc * std::pow(2.0, -1.0 / 6), hi = fc * std::pow(2.0, 1.0 / 6);
        double e = 0;
        for (size_t i = 0; i < psd.size(); ++i) {
            const double f = double(i) * fs / double(nfft);
            if (f >= lo && f < hi) e += psd[i];
        }
        bands.emplace_back(fc, 10 * std::log10(e + 1e-30));
    }
    return bands;
}

double correlation(const double* a, const double* b, size_t n) {
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= double(n);
    mb /= double(n);
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        const double x = a[i] - ma, y = b[i] - mb;
        sab += x * y;
        saa += x * x;
        sbb += y * y;
    }
    return sab / std::sqrt(saa * sbb + 1e-300);
}

std::pair<double, double> bestFractionalCorrelation(const std::vector<double>& a, const std::vector<double>& b, int maxLag) {
    const size_t n = std::min(a.size(), b.size());
    size_t m = 1;
    while (m < 2 * n) m <<= 1;
    std::vector<std::complex<double>> A(m), B(m);
    double ma = 0, mb = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= double(n);
    mb /= double(n);
    for (size_t i = 0; i < n; ++i) {
        A[i] = a[i] - ma;
        B[i] = b[i] - mb;
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    fft(A);
    fft(B);
    // C(k) = conj(A) B : r(τ) = Σ a[i] b[i + τ].
    std::vector<std::complex<double>> C(m);
    for (size_t k = 0; k < m; ++k) C[k] = std::conj(A[k]) * B[k];
    const double norm = std::sqrt(saa * sbb) + 1e-300;
    auto rAt = [&](double tau) {
        double s = C[0].real();
        const std::complex<double> step = std::polar(1.0, 2 * kPi * tau / double(m));
        std::complex<double> rot = step;
        for (size_t k = 1; k < m / 2; ++k) {
            s += 2 * (C[k] * rot).real();
            rot *= step;
        }
        s += C[m / 2].real() * std::cos(kPi * tau);
        return s / double(m) / norm;
    };
    // Integer search by inverse FFT, then fractional refinement around the maximum.
    std::vector<std::complex<double>> r = C;
    fft(r, true);
    int best = 0;
    double bestVal = -2;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        const double v = r[size_t((lag + int64_t(m)) % int64_t(m))].real() / norm;
        if (v > bestVal) {
            bestVal = v;
            best = lag;
        }
    }
    double bestTau = best, bestCorr = bestVal;
    for (int s = -64; s <= 64; ++s) {
        const double tau = best + s / 64.0;
        const double v = rAt(tau);
        if (v > bestCorr) {
            bestCorr = v;
            bestTau = tau;
        }
    }
    return {bestCorr, bestTau};
}

}  // namespace stym::test
