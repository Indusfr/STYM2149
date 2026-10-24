// STYM2149 — analysis tools for the tests
// Programming: Pascal "Indus" Nowak (with Claude's help).
// Copyright © 2026 Indus / PRISM. Licence : AGPLv3 (voir LICENSE).
#pragma once
#include <complex>
#include <cstddef>
#include <vector>

namespace stym::test {

constexpr double kPi = 3.14159265358979323846;

// In-place radix-2 FFT (size a power of 2).
void fft(std::vector<std::complex<double>>& a, bool inverse = false);

std::vector<double> kaiserWindow(size_t n, double beta);
std::vector<double> hannWindow(size_t n);

// Welch spectral density: segments of nfft points, 50 % overlap, Hann window, mean removed.
// Returns nfft / 2 + 1 values (power per bin).
std::vector<double> welchPsd(const std::vector<double>& x, size_t nfft);

// Log-spectral distance (RMS dB) between two PSDs over [fLo, fHi].
double logSpectralDistanceDb(const std::vector<double>& a, const std::vector<double>& b, double fs, double fLo, double fHi);

// Energy per third octave (ISO centres from 25 Hz to 20 kHz), in dB. Returns {centre, dB}.
std::vector<std::pair<double, double>> thirdOctaveDb(const std::vector<double>& psd, double fs);

// Normalised correlation (means removed).
double correlation(const double* a, const double* b, size_t n);

// Maximum normalised correlation over a fractional lag of b relative to a, searched in [-maxLag, maxLag] samples
// (step of 1/64). Returns {correlation, lag}.
std::pair<double, double> bestFractionalCorrelation(const std::vector<double>& a, const std::vector<double>& b, int maxLag);

}  // namespace stym::test
