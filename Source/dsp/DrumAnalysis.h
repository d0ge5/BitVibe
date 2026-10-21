#pragma once

// Built-in drum analysis: finds kick, snare, hi-hat, cymbal and tom hits in a drum recording.
// Header-only and free of JUCE; runs offline on a background thread, never on the audio thread.
//
// Method (onset detection + semi-adaptive NMF of the attacks, a classic drum-transcription design):
//  1. Magnitude spectrogram at 100 frames per second on log-spaced frequency bands.
//  2. Onsets from a SuperFlux-style onset function (positive log-spectral difference against a
//     frequency-smeared earlier frame) with adaptive peak picking. High recall; deciding *which*
//     drums started is the next step's job.
//  3. For every onset, the "attack spectrum": what the hit added on top of what was already sounding.
//     Ringing cymbals and decays therefore do not count again at the next hit.
//  4. Non-negative matrix factorisation of all attack spectra with two spectral templates per drum.
//     The templates start from hand-designed prior shapes and are then allowed to move part of the way
//     towards the actual kit ("semi-adaptive"), which keeps each template tied to its drum.
//  5. A drum's gain at an onset, compared with the typical gain of that drum in the same file and with
//     the other drums at that onset, gives the hit's confidence; the gain itself gives its level.
//  6. Onset times are refined to about 1 ms on the waveform's energy envelope.
// The priors are generic shapes, not learned from any dataset, so the plug-in carries no other licence;
// the scalar settings below were tuned against public evaluation recordings.

#include "DrumMap.h"
#include "Fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace bitvibe::drums
{
struct Options
{
    // spectrogram
    double fps = 100.0;
    double minFrequency = 27.5;
    double maxFrequency = 16000.0;
    int bandsPerOctave = 8;

    // onset detection
    float logScale = 30.0f;          // log10(1 + logScale * magnitude / reference)
    bool perBandReference = true;    // reference = each band's own loudest value (else the loudest of all)
    int fluxLag = 2;                 // frames back for the spectral difference
    float onsetThreshold = 0.03f;    // above the local average, relative to the 98th percentile of the function
    int onsetAverageFrames = 10;     // local average over this many frames before the candidate
    int peakWindowFrames = 3;        // an onset is the largest value within +-this many frames
    double minGapSeconds = 0.025;    // onsets closer than this are merged
    float minRiseDb = 4.0f;          // an onset must raise the energy of some frequency region by this much

    // attack spectrum: largest magnitude over [t, t+attackFrames) minus the smallest over [t-baseFrames, t)
    int attackFrames = 3;
    int baseFrames = 3;

    // NMF of the attack spectra
    int fixedIterations = 40;        // passes with the templates held at their priors
    int adaptiveIterations = 60;     // then passes that let the templates adapt to the kit
    float adaptation = 0.6f;         // how far (0..1) a template may move away from its prior

    // scoring: strength = (gain / typical gain of the drum) * share^shareExponent
    float shareExponent = 0.25f;
    // Hi-hat or cymbal: the template split is blended with a split by how long the highs ring
    // (highSustain): below the pivot a hi-hat, above it a cymbal.
    float sustainWeight = 0.3f;
    float sustainPivot = 0.3f;
    float sustainWidth = 0.05f;
    float typicalPercentile = 0.9f;  // the "typical" gain of a drum, among onsets it dominates
    float minTypicalFraction = 0.35f; // a drum's typical gain is at least this fraction of the file's typical onset
    std::array<float, kNumDrums> halfConfidence { 0.19f, 0.14f, 0.077f, 0.3f, 2.0f }; // strength giving confidence 0.5
    float reportFloor = 0.03f;       // candidates below this confidence are dropped

    // timing: the onset function peaks a little before the true onset (frames are 46 ms long and
    // centred), so frame times are moved by onsetDelay and then refined on the waveform within
    // [-refineBefore, +refineAfter] of that.
    double onsetDelaySeconds = 0.015;
    double refineBeforeSeconds = 0.02;
    double refineAfterSeconds = 0.02;
};

inline constexpr int kComponentsPerDrum = 2;
inline constexpr int kNumComponents = kNumDrums * kComponentsPerDrum;

// Magnitude on log-spaced bands, frame-major (mag[frame * numBands + band]). Frame t is centred on
// sample t * hop.
struct Spectrogram
{
    double sampleRate = 44100.0;
    double fps = 100.0;
    int hop = 441;
    int frameSize = 2048;
    int numFrames = 0;
    int numBands = 0;
    std::vector<double> bandCentres;
    std::vector<float> mag;

    float at(int frame, int band) const { return mag[(size_t) frame * (size_t) numBands + (size_t) band]; }
    double frameTime(int frame) const { return (double) frame * hop / sampleRate; }
    int frameAt(double seconds) const { return std::clamp((int) std::lround(seconds * sampleRate / hop), 0, std::max(0, numFrames - 1)); }
};

// Gain of every drum at every onset.
struct OnsetGains
{
    std::vector<int> frames;                        // onset frames
    std::vector<std::array<float, kNumDrums>> gain; // per onset
    std::vector<float> sustain;                     // highSustain() per onset
};

struct Result
{
    std::vector<DrumHit> hits; // levels normalised per drum (0 dB = loudest), sorted by time
    double durationSeconds = 0.0;
    double tempoBpm = 0.0;
};

namespace detail
{
    struct Anchor
    {
        double hz, db;
    };

    // Piecewise-linear in (log frequency, dB) between anchors; flat beyond the ends.
    template <size_t N>
    double shape(const std::array<Anchor, N>& a, double f)
    {
        if (f <= a.front().hz)
            return std::pow(10.0, a.front().db / 20.0);
        for (size_t i = 1; i < N; ++i)
            if (f <= a[i].hz)
            {
                const double x = std::log2(f / a[i - 1].hz) / std::log2(a[i].hz / a[i - 1].hz);
                return std::pow(10.0, (a[i - 1].db + x * (a[i].db - a[i - 1].db)) / 20.0);
            }
        return std::pow(10.0, a.back().db / 20.0);
    }

    // Prior attack-spectrum shapes: magnitude per log-spaced band (so white noise would rise 3 dB per
    // octave), two variants per drum. Coarse generic shapes of each instrument, set by hand.
    inline double prior(Drum drum, int variant, double f)
    {
        using A = std::array<Anchor, 10>;
        static const A kick0 { { { 30, -6 }, { 60, 0 }, { 100, 0 }, { 150, -8 }, { 250, -20 }, { 500, -26 }, { 1000, -29 }, { 3000, -29 }, { 8000, -38 }, { 16000, -48 } } };
        static const A kick1 { { { 30, -8 }, { 55, 0 }, { 90, -1 }, { 140, -8 }, { 250, -18 }, { 1000, -24 }, { 3000, -22 }, { 6000, -28 }, { 10000, -35 }, { 16000, -42 } } };
        static const A snare0 { { { 40, -27 }, { 100, -22 }, { 160, -8 }, { 200, 0 }, { 240, -4 }, { 300, -12 }, { 1000, -15 }, { 6000, -17 }, { 10000, -23 }, { 16000, -34 } } };
        static const A snare1 { { { 40, -25 }, { 150, -12 }, { 220, -4 }, { 400, -8 }, { 1000, -8 }, { 3000, -8 }, { 6000, -10 }, { 10000, -17 }, { 13000, -22 }, { 16000, -28 } } };
        static const A hat0 { { { 40, -32 }, { 300, -28 }, { 600, -20 }, { 1500, -14 }, { 3000, -10 }, { 5000, -6 }, { 8000, -3 }, { 13000, 0 }, { 15000, 0 }, { 16000, -1 } } };
        static const A hat1 { { { 40, -32 }, { 500, -22 }, { 1500, -12 }, { 3000, -8 }, { 6000, -3 }, { 8000, -1 }, { 10000, 0 }, { 13000, -1 }, { 15000, -3 }, { 16000, -4 } } };
        static const A cym0 { { { 40, -28 }, { 200, -20 }, { 400, -9 }, { 800, -11 }, { 1500, -11 }, { 3000, -6 }, { 6000, -1 }, { 9000, 0 }, { 13000, -5 }, { 16000, -11 } } };
        static const A cym1 { { { 40, -30 }, { 300, -18 }, { 400, -10 }, { 1000, -12 }, { 2500, -6 }, { 5000, -2 }, { 8000, 0 }, { 12000, -6 }, { 14000, -10 }, { 16000, -14 } } };
        static const A tom0 { { { 40, -20 }, { 80, -12 }, { 150, -2 }, { 180, 0 }, { 220, -5 }, { 300, -12 }, { 1000, -16 }, { 4000, -19 }, { 10000, -32 }, { 16000, -45 } } };
        static const A tom1 { { { 40, -14 }, { 70, -6 }, { 100, 0 }, { 130, -2 }, { 200, -10 }, { 400, -16 }, { 1000, -18 }, { 4000, -22 }, { 10000, -34 }, { 16000, -45 } } };
        switch (drum)
        {
            case Drum::Kick: return shape(variant == 0 ? kick0 : kick1, f);
            case Drum::Snare: return shape(variant == 0 ? snare0 : snare1, f);
            case Drum::HiHat: return shape(variant == 0 ? hat0 : hat1, f);
            case Drum::Cymbal: return shape(variant == 0 ? cym0 : cym1, f);
            case Drum::Tom: return shape(variant == 0 ? tom0 : tom1, f);
        }
        return 0.0;
    }

    // Index of (row, col) in a row-major table with `cols` columns.
    inline size_t cell(int row, int cols, int col) { return (size_t) row * (size_t) cols + (size_t) col; }

    inline int nextPowerOfTwo(int v)
    {
        int p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

    inline float percentile(std::vector<float> v, float q)
    {
        if (v.empty())
            return 0.0f;
        const size_t k = (size_t) std::clamp((double) q * (double) (v.size() - 1), 0.0, (double) (v.size() - 1));
        std::nth_element(v.begin(), v.begin() + (long) k, v.end());
        return v[k];
    }
} // namespace detail

inline Spectrogram computeSpectrogram(const float* x, size_t numSamples, double sampleRate, const Options& o = {})
{
    Spectrogram s;
    s.sampleRate = sampleRate;
    s.fps = o.fps;
    s.hop = std::max(1, (int) std::lround(sampleRate / o.fps));
    s.frameSize = detail::nextPowerOfTwo((int) std::lround(0.0464 * sampleRate));
    s.numFrames = (int) (numSamples / (size_t) s.hop) + 1;
    const int n = s.frameSize;

    // Bands: log-spaced edges; each band sums the power of its FFT bins. Low bands narrower than a bin
    // take the nearest bin, and bands that would repeat the previous one are dropped.
    const double top = std::min(o.maxFrequency, 0.45 * sampleRate);
    const double binHz = sampleRate / n;
    std::vector<std::pair<int, int>> ranges; // [first, last] bins
    const int numEdges = (int) std::ceil(std::log2(top / o.minFrequency) * o.bandsPerOctave) + 1;
    for (int i = 0; i + 1 < numEdges; ++i)
    {
        const double lo = o.minFrequency * std::exp2((double) i / o.bandsPerOctave);
        const double hi = o.minFrequency * std::exp2((double) (i + 1) / o.bandsPerOctave);
        int first = (int) std::ceil(lo / binHz), last = (int) std::ceil(hi / binHz) - 1;
        if (last < first)
            first = last = std::max(1, (int) std::lround(std::sqrt(lo * hi) / binHz));
        first = std::clamp(first, 1, n / 2);
        last = std::clamp(last, first, n / 2);
        if (!ranges.empty() && last <= ranges.back().second)
            continue;
        first = ranges.empty() ? first : std::max(first, ranges.back().second + 1);
        ranges.push_back({ first, last });
        s.bandCentres.push_back(0.5 * (first + last) * binHz);
    }
    s.numBands = (int) ranges.size();
    s.mag.assign((size_t) s.numFrames * (size_t) s.numBands, 0.0f);

    std::vector<float> window((size_t) n), frame((size_t) n);
    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < n; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * pi * i / n));
    const double norm = 2.0 / (0.5 * n); // a full-scale sine reads about 1

    Fft fft((size_t) n);
    std::vector<double> power;
    for (int t = 0; t < s.numFrames; ++t)
    {
        const long start = (long) t * s.hop - n / 2;
        for (int i = 0; i < n; ++i)
        {
            const long idx = start + i;
            frame[(size_t) i] = (idx >= 0 && idx < (long) numSamples) ? x[idx] * window[(size_t) i] : 0.0f;
        }
        fft.powerSpectrum(frame.data(), power);
        for (int b = 0; b < s.numBands; ++b)
        {
            double acc = 0.0;
            for (int k = ranges[(size_t) b].first; k <= ranges[(size_t) b].second; ++k)
                acc += power[(size_t) k];
            s.mag[(size_t) t * (size_t) s.numBands + (size_t) b] = (float) (std::sqrt(acc) * norm);
        }
    }
    return s;
}

// SuperFlux-style onset function: positive log-magnitude difference against a frame fluxLag earlier,
// maximum-filtered over three neighbouring bands (so a slightly moving partial is not a new onset).
inline std::vector<float> onsetFunction(const Spectrogram& s, const Options& o = {})
{
    const int B = s.numBands, T = s.numFrames;
    std::vector<float> odf((size_t) T, 0.0f);
    if (B == 0 || T == 0)
        return odf;
    // Each band against its own loudest moment, so quiet high-frequency hits (hi-hats) count as much
    // as loud low ones.
    std::vector<float> reference((size_t) B, 1.0e-9f);
    for (int t = 0; t < T; ++t)
        for (int b = 0; b < B; ++b)
            reference[(size_t) b] = std::max(reference[(size_t) b], s.at(t, b));
    if (!o.perBandReference)
        std::fill(reference.begin(), reference.end(), *std::max_element(reference.begin(), reference.end()));
    std::vector<float> logMag(s.mag.size());
    for (int t = 0; t < T; ++t)
        for (int b = 0; b < B; ++b)
            logMag[detail::cell(t, B, b)] = std::log10(1.0f + o.logScale * s.at(t, b) / reference[(size_t) b]);
    for (int t = o.fluxLag; t < T; ++t)
    {
        float acc = 0.0f;
        for (int b = 0; b < B; ++b)
        {
            float earlier = 0.0f;
            for (int bb = std::max(0, b - 1); bb <= std::min(B - 1, b + 1); ++bb)
                earlier = std::max(earlier, logMag[detail::cell(t - o.fluxLag, B, bb)]);
            acc += std::max(0.0f, logMag[detail::cell(t, B, b)] - earlier);
        }
        odf[(size_t) t] = acc;
    }
    return odf;
}

inline std::vector<int> detectOnsets(const Spectrogram& s, const Options& o = {})
{
    const std::vector<float> odf = onsetFunction(s, o);
    std::vector<int> onsets;
    std::vector<float> positive;
    for (float v : odf)
        if (v > 0.0f)
            positive.push_back(v);
    const float scale = detail::percentile(positive, 0.98f);
    if (scale <= 0.0f)
        return onsets;

    const int T = (int) odf.size();
    const int gap = std::max(1, (int) std::lround(o.minGapSeconds * s.fps));

    // Energy in five frequency regions, to require a real rise somewhere (steady noise has none).
    constexpr int kRegions = 5;
    static constexpr double kRegionTop[kRegions] { 150.0, 600.0, 3000.0, 8000.0, 1.0e9 };
    std::vector<std::array<double, kRegions>> energy((size_t) T);
    for (int t = 0; t < T; ++t)
    {
        auto& e = energy[(size_t) t];
        e.fill(1.0e-12);
        int region = 0;
        for (int b = 0; b < s.numBands; ++b)
        {
            while (s.bandCentres[(size_t) b] > kRegionTop[region])
                ++region;
            const double m = s.at(t, b);
            e[(size_t) region] += m * m;
        }
    }
    auto realRise = [&](int t)
    {
        for (int r = 0; r < kRegions; ++r)
        {
            double peak = 0.0, base = 1.0e30;
            for (int j = 0; j < 3 && t + j < T; ++j)
                peak = std::max(peak, energy[(size_t) (t + j)][(size_t) r]);
            for (int j = 1; j <= 3 && t - j >= 0; ++j)
                base = std::min(base, energy[(size_t) (t - j)][(size_t) r]);
            if (base > 1.0e29)
                base = 1.0e-12;
            if (10.0 * std::log10(peak / base) >= o.minRiseDb)
                return true;
        }
        return false;
    };
    for (int t = 0; t < T; ++t)
    {
        const float v = odf[(size_t) t];
        bool isMax = v > 0.0f;
        for (int j = -o.peakWindowFrames; j <= o.peakWindowFrames && isMax; ++j)
        {
            const int u = t + j;
            if (j != 0 && u >= 0 && u < T && (j < 0 ? odf[(size_t) u] >= v : odf[(size_t) u] > v))
                isMax = false;
        }
        if (!isMax)
            continue;
        double avg = 0.0;
        int count = 0;
        for (int u = std::max(0, t - o.onsetAverageFrames); u < t; ++u, ++count)
            avg += odf[(size_t) u];
        avg = count > 0 ? avg / count : 0.0;
        if (v < avg + o.onsetThreshold * scale || !realRise(t))
            continue;
        if (!onsets.empty() && t - onsets.back() < gap)
        {
            if (v <= odf[(size_t) onsets.back()])
                continue;
            onsets.pop_back();
        }
        onsets.push_back(t);
    }
    return onsets;
}

// What each onset added to the spectrum: the largest magnitude over its first attackFrames frames minus
// the smallest over the baseFrames frames before it. Onset-major: A[n * numBands + b].
inline std::vector<float> attackSpectra(const Spectrogram& s, const std::vector<int>& onsetFrames, const Options& o = {})
{
    const int B = s.numBands, N = (int) onsetFrames.size();
    std::vector<float> A((size_t) N * (size_t) B);
    for (int n = 0; n < N; ++n)
    {
        const int t = onsetFrames[(size_t) n];
        for (int b = 0; b < B; ++b)
        {
            float peak = 0.0f, base = 1.0e30f;
            for (int j = 0; j < o.attackFrames && t + j < s.numFrames; ++j)
                peak = std::max(peak, s.at(t + j, b));
            for (int j = 1; j <= o.baseFrames && t - j >= 0; ++j)
                base = std::min(base, s.at(t - j, b));
            if (base > 1.0e29f)
                base = 0.0f;
            A[detail::cell(n, B, b)] = std::max(0.0f, peak - base);
        }
    }
    return A;
}

// How much of an onset's high-frequency attack (above 4 kHz) is still sounding a little later: about 1 for
// a ringing cymbal, small for a closed hi-hat. Measured 80 ms after the onset, or just before the next
// onset if that comes sooner (but never earlier than 50 ms).
inline std::vector<float> highSustain(const Spectrogram& s, const std::vector<int>& onsetFrames)
{
    std::vector<float> out(onsetFrames.size(), 0.0f);
    const int first = (int) (std::lower_bound(s.bandCentres.begin(), s.bandCentres.end(), 4000.0) - s.bandCentres.begin());
    auto highAt = [&](int t)
    {
        double acc = 0.0;
        for (int b = first; b < s.numBands; ++b)
            acc += s.at(std::clamp(t, 0, s.numFrames - 1), b);
        return acc;
    };
    const int later = (int) std::lround(0.08 * s.fps), earliest = (int) std::lround(0.05 * s.fps);
    for (size_t n = 0; n < onsetFrames.size(); ++n)
    {
        const int t = onsetFrames[n];
        int at = t + later;
        if (n + 1 < onsetFrames.size())
            at = std::min(at, std::max(t + earliest, onsetFrames[n + 1] - 1));
        double peak = 0.0;
        for (int j = 0; j < 3; ++j)
            peak = std::max(peak, highAt(t + j));
        out[n] = peak > 0.0 ? (float) std::min(1.5, highAt(at) / peak) : 0.0f;
    }
    return out;
}

// Attack spectrum of every onset and semi-adaptive NMF of them.
inline OnsetGains factoriseOnsets(const Spectrogram& s, const std::vector<int>& onsetFrames, const Options& o = {})
{
    OnsetGains g;
    g.frames = onsetFrames;
    g.sustain = highSustain(s, onsetFrames);
    const int B = s.numBands, N = (int) onsetFrames.size(), K = kNumComponents;
    g.gain.assign((size_t) N, {});
    if (B == 0 || N == 0)
        return g;

    const std::vector<float> A = attackSpectra(s, onsetFrames, o);

    constexpr float eps = 1.0e-12f;
    std::vector<float> prior((size_t) K * (size_t) B), W, G((size_t) K * (size_t) N);
    for (int k = 0; k < K; ++k)
    {
        const Drum drum = (Drum) (k / kComponentsPerDrum);
        double sum = 0.0;
        for (int b = 0; b < B; ++b)
        {
            const double v = detail::prior(drum, k % kComponentsPerDrum, s.bandCentres[(size_t) b]) + 1.0e-4;
            prior[detail::cell(k, B, b)] = (float) v;
            sum += v;
        }
        for (int b = 0; b < B; ++b)
            prior[detail::cell(k, B, b)] = (float) (prior[detail::cell(k, B, b)] / sum);
    }
    W = prior;
    for (int k = 0; k < K; ++k)
        for (int n = 0; n < N; ++n)
        {
            double acc = 0.0;
            for (int b = 0; b < B; ++b)
                acc += W[detail::cell(k, B, b)] * A[detail::cell(n, B, b)];
            G[detail::cell(k, N, n)] = (float) (acc + eps);
        }

    std::vector<float> ratio((size_t) N * (size_t) B), num((size_t) B);
    auto computeRatio = [&]
    {
        for (int n = 0; n < N; ++n)
            for (int b = 0; b < B; ++b)
            {
                float acc = eps;
                for (int k = 0; k < K; ++k)
                    acc += W[detail::cell(k, B, b)] * G[detail::cell(k, N, n)];
                ratio[detail::cell(n, B, b)] = A[detail::cell(n, B, b)] / acc;
            }
    };

    const int total = o.fixedIterations + o.adaptiveIterations;
    for (int it = 0; it < total; ++it)
    {
        computeRatio();
        for (int k = 0; k < K; ++k) // columns of W sum to 1, so the KL denominator is 1
            for (int n = 0; n < N; ++n)
            {
                float acc = 0.0f;
                for (int b = 0; b < B; ++b)
                    acc += W[detail::cell(k, B, b)] * ratio[detail::cell(n, B, b)];
                G[detail::cell(k, N, n)] *= acc;
            }
        if (it < o.fixedIterations)
            continue;

        computeRatio();
        const float progress = (float) (it - o.fixedIterations + 1) / (float) std::max(1, o.adaptiveIterations);
        const float alpha = std::clamp(o.adaptation, 0.0f, 1.0f) * progress;
        for (int k = 0; k < K; ++k)
        {
            double gSum = eps;
            for (int n = 0; n < N; ++n)
                gSum += G[detail::cell(k, N, n)];
            for (int b = 0; b < B; ++b)
            {
                double acc = 0.0;
                for (int n = 0; n < N; ++n)
                    acc += ratio[detail::cell(n, B, b)] * G[detail::cell(k, N, n)];
                num[(size_t) b] = (float) (W[detail::cell(k, B, b)] * acc / gSum);
            }
            const double sum = std::accumulate(num.begin(), num.end(), (double) eps);
            double colSum = 0.0;
            for (int b = 0; b < B; ++b)
            {
                const float w = (1.0f - alpha) * prior[detail::cell(k, B, b)] + alpha * (float) (num[(size_t) b] / sum);
                W[detail::cell(k, B, b)] = w;
                colSum += w;
            }
            for (int b = 0; b < B; ++b)
                W[detail::cell(k, B, b)] = (float) (W[detail::cell(k, B, b)] / colSum);
        }
    }

    for (int n = 0; n < N; ++n)
        for (int k = 0; k < K; ++k)
            g.gain[(size_t) n][(size_t) (k / kComponentsPerDrum)] += G[detail::cell(k, N, n)];
    return g;
}

// Turns onset gains into scored hits (time = onset frame time, level = gain in dB).
inline std::vector<DrumHit> scoreHits(const Spectrogram& s, const OnsetGains& onsetGains, const Options& o = {})
{
    std::vector<DrumHit> hits;
    const size_t N = onsetGains.frames.size();
    if (N == 0)
        return hits;

    OnsetGains g = onsetGains;
    if (o.sustainWeight > 0.0f && g.sustain.size() == N)
        for (size_t n = 0; n < N; ++n)
        {
            auto& gain = g.gain[n];
            const float both = gain[(size_t) Drum::HiHat] + gain[(size_t) Drum::Cymbal];
            if (both <= 0.0f)
                continue;
            const float byTemplate = gain[(size_t) Drum::Cymbal] / both;
            const float bySustain = 1.0f / (1.0f + std::exp(-(g.sustain[n] - o.sustainPivot) / std::max(1.0e-3f, o.sustainWidth)));
            const float cymbal = (1.0f - o.sustainWeight) * byTemplate + o.sustainWeight * bySustain;
            gain[(size_t) Drum::Cymbal] = both * cymbal;
            gain[(size_t) Drum::HiHat] = both * (1.0f - cymbal);
        }

    // The file's typical onset, and each drum's typical gain among the onsets it dominates.
    std::vector<float> totals(N);
    for (size_t n = 0; n < N; ++n)
        totals[n] = std::accumulate(g.gain[n].begin(), g.gain[n].end(), 0.0f);
    const float fileTypical = detail::percentile(totals, o.typicalPercentile);
    std::array<float, kNumDrums> typical {};
    for (int d = 0; d < kNumDrums; ++d)
    {
        std::vector<float> dominant;
        for (size_t n = 0; n < N; ++n)
            if (g.gain[n][(size_t) d] >= 0.5f * totals[n])
                dominant.push_back(g.gain[n][(size_t) d]);
        const float t = detail::percentile(dominant, o.typicalPercentile);
        typical[(size_t) d] = std::max({ t, o.minTypicalFraction * fileTypical, 1.0e-9f });
    }

    for (size_t n = 0; n < N; ++n)
    {
        std::array<float, kNumDrums> rel {};
        float relSum = 1.0e-12f;
        for (int d = 0; d < kNumDrums; ++d)
        {
            rel[(size_t) d] = g.gain[n][(size_t) d] / typical[(size_t) d];
            relSum += rel[(size_t) d];
        }
        for (int d = 0; d < kNumDrums; ++d)
        {
            const float share = rel[(size_t) d] / relSum;
            const float strength = rel[(size_t) d] * std::pow(share, o.shareExponent);
            const float half = std::max(1.0e-6f, o.halfConfidence[(size_t) d]);
            const float confidence = strength / (strength + half);
            if (confidence < o.reportFloor)
                continue;
            DrumHit h;
            h.time = std::max(0.0, s.frameTime(g.frames[n]) + o.onsetDelaySeconds);
            h.drum = (Drum) d;
            h.confidence = confidence;
            h.levelDb = 20.0f * std::log10(std::max(g.gain[n][(size_t) d], 1.0e-9f));
            hits.push_back(h);
        }
    }
    return hits;
}

// Moves each hit to the sharpest energy rise of the waveform near its detected frame (about 1 ms
// resolution). Hits that already sit on an onset barely move.
inline void refineOnsets(std::vector<DrumHit>& hits, const float* x, size_t numSamples, double sampleRate,
                         const Options& o = {})
{
    const int sub = std::max(1, (int) std::lround(sampleRate / 1000.0)); // 1 ms frames
    const size_t frames = numSamples / (size_t) sub;
    if (frames < 4)
        return;
    std::vector<float> logEnergy(frames);
    for (size_t f = 0; f < frames; ++f)
    {
        double acc = 0.0;
        for (int i = 0; i < sub; ++i)
        {
            const float v = x[f * (size_t) sub + (size_t) i];
            acc += (double) v * v;
        }
        logEnergy[f] = (float) (10.0 * std::log10(acc / sub + 1.0e-12));
    }
    const double framesPerSecond = sampleRate / sub;
    for (auto& h : hits)
    {
        const long centre = (long) std::lround(h.time * framesPerSecond);
        const long from = std::max(3L, centre - (long) std::lround(o.refineBeforeSeconds * framesPerSecond));
        const long to = std::min((long) frames - 2, centre + (long) std::lround(o.refineAfterSeconds * framesPerSecond));
        float best = 0.0f;
        long bestFrame = -1;
        for (long f = from; f <= to; ++f)
        {
            const float before = std::min({ logEnergy[(size_t) f - 1], logEnergy[(size_t) f - 2], logEnergy[(size_t) f - 3] });
            const float rise = std::max(logEnergy[(size_t) f], logEnergy[(size_t) f + 1]) - before;
            if (rise > best)
            {
                best = rise;
                bestFrame = f;
            }
        }
        if (bestFrame >= 0 && best > 3.0f) // only move to a real onset (+3 dB)
            h.time = (double) bestFrame * sub / sampleRate;
    }
}

// Gives hits found by another detector (the neural helper) levels on the same scale as the built-in
// analysis: each hit's drum gain at its onset, in absolute dB.
inline void measureLevels(std::vector<DrumHit>& hits, const float* x, size_t numSamples, double sampleRate,
                          const Options& o = {})
{
    if (hits.empty())
        return;
    const Spectrogram s = computeSpectrogram(x, numSamples, sampleRate, o);
    std::vector<int> frames;
    for (const auto& h : hits)
        frames.push_back(s.frameAt(h.time));
    std::vector<int> unique = frames;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    const OnsetGains g = factoriseOnsets(s, unique, o);
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const size_t n = (size_t) (std::lower_bound(unique.begin(), unique.end(), frames[i]) - unique.begin());
        hits[i].levelDb = 20.0f * std::log10(std::max(g.gain[n][(size_t) hits[i].drum], 1.0e-9f));
    }
}

// The whole built-in analysis: mono input at any sample rate.
inline Result analyse(const float* x, size_t numSamples, double sampleRate, const Options& o = {})
{
    Result r;
    r.durationSeconds = (double) numSamples / sampleRate;
    if (numSamples < (size_t) (0.05 * sampleRate))
        return r;
    const Spectrogram s = computeSpectrogram(x, numSamples, sampleRate, o);
    const std::vector<int> onsets = detectOnsets(s, o);
    const OnsetGains g = factoriseOnsets(s, onsets, o);
    r.hits = scoreHits(s, g, o);
    refineOnsets(r.hits, x, numSamples, sampleRate, o);
    normaliseLevels(r.hits);
    r.tempoBpm = estimateTempo(r.hits, r.durationSeconds);
    return r;
}
} // namespace bitvibe::drums
