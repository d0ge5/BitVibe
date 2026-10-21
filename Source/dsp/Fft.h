#pragma once

// A small radix-2 FFT for the offline drum analysis (not used on the audio thread). Header-only and
// free of JUCE so the analysis and its tests build without it.

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace bitvibe
{
class Fft
{
public:
    // size must be a power of two (>= 2).
    explicit Fft(size_t size) : n(size), twiddles(size / 2), bitReversed(size)
    {
        int bits = 0;
        while (((size_t) 1 << bits) < n)
            ++bits;
        for (size_t i = 0; i < n; ++i)
        {
            size_t r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & ((size_t) 1 << b))
                    r |= (size_t) 1 << (bits - 1 - b);
            bitReversed[i] = r;
        }
        constexpr double pi = 3.14159265358979323846;
        for (size_t k = 0; k < n / 2; ++k)
            twiddles[k] = std::polar(1.0, -2.0 * pi * (double) k / (double) n);
    }

    size_t size() const { return n; }

    // In-place forward transform (no scaling).
    void forward(std::vector<std::complex<double>>& x) const
    {
        for (size_t i = 0; i < n; ++i)
            if (bitReversed[i] > i)
                std::swap(x[i], x[bitReversed[i]]);
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const size_t half = len / 2, step = n / len;
            for (size_t start = 0; start < n; start += len)
                for (size_t k = 0; k < half; ++k)
                {
                    const auto t = twiddles[k * step] * x[start + k + half];
                    x[start + k + half] = x[start + k] - t;
                    x[start + k] += t;
                }
        }
    }

    // Power spectrum (|X|^2) of a real frame, bins 0..n/2. frame must hold n samples.
    void powerSpectrum(const float* frame, std::vector<double>& power)
    {
        work.assign(n, {});
        for (size_t i = 0; i < n; ++i)
            work[i] = { (double) frame[i], 0.0 };
        forward(work);
        power.resize(n / 2 + 1);
        for (size_t k = 0; k <= n / 2; ++k)
            power[k] = std::norm(work[k]);
    }

private:
    size_t n;
    std::vector<std::complex<double>> twiddles;
    std::vector<size_t> bitReversed;
    std::vector<std::complex<double>> work;
};
} // namespace bitvibe
