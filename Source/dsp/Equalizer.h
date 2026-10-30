#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace bitvibe
{
// An eight-band parametric equalizer for the sequenced sound. Each band is a second-order filter (a biquad) of one of
// six kinds; the coefficients follow Robert Bristow-Johnson's "Audio EQ Cookbook" formulas.
enum class EqType
{
    LowCut = 0, // high-pass, 12 dB per octave
    LowShelf,
    Bell,
    Notch,
    HighShelf,
    HighCut     // low-pass, 12 dB per octave
};
inline constexpr int kNumEqTypes = 6;
inline constexpr int kNumEqBands = 8;
inline constexpr float kEqMinHz = 20.0f, kEqMaxHz = 20000.0f, kEqMaxGainDb = 15.0f, kEqMinQ = 0.1f, kEqMaxQ = 18.0f;

inline const char* eqTypeName(EqType t)
{
    static constexpr const char* names[kNumEqTypes] = { "Low Cut", "Low Shelf", "Bell", "Notch", "High Shelf", "High Cut" };
    return names[std::clamp((int) t, 0, kNumEqTypes - 1)];
}

// Does the band's gain do anything? (The cuts and the notch have no gain.)
inline bool eqTypeHasGain(EqType t) { return t == EqType::LowShelf || t == EqType::Bell || t == EqType::HighShelf; }

struct EqBand
{
    bool on = false;
    EqType type = EqType::Bell;
    float freq = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.71f;
};

// The bands a new EQ starts with: flat (every gain at 0 dB, the cuts off), spread over the range.
inline std::array<EqBand, kNumEqBands> defaultEqBands()
{
    return { {
        { false, EqType::LowCut, 40.0f, 0.0f, 0.71f },
        { true, EqType::LowShelf, 100.0f, 0.0f, 0.71f },
        { true, EqType::Bell, 400.0f, 0.0f, 0.71f },
        { true, EqType::Bell, 1000.0f, 0.0f, 0.71f },
        { true, EqType::Bell, 2500.0f, 0.0f, 0.71f },
        { false, EqType::Bell, 5000.0f, 0.0f, 0.71f },
        { false, EqType::HighShelf, 8000.0f, 0.0f, 0.71f },
        { false, EqType::HighCut, 16000.0f, 0.0f, 0.71f },
    } };
}

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; // normalised (a0 = 1)

    static Biquad design(const EqBand& band, double sampleRate)
    {
        const double f = std::clamp((double) band.freq, (double) kEqMinHz, 0.49 * sampleRate);
        const double q = std::clamp((double) band.q, (double) kEqMinQ, (double) kEqMaxQ);
        const double w0 = 2.0 * 3.14159265358979323846 * f / sampleRate;
        const double c = std::cos(w0), s = std::sin(w0);
        const double alpha = s / (2.0 * q);
        const double A = std::pow(10.0, std::clamp((double) band.gainDb, (double) -kEqMaxGainDb, (double) kEqMaxGainDb) / 40.0);
        const double rootA = std::sqrt(A);
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
        switch (band.type)
        {
            case EqType::LowCut:
                b0 = (1.0 + c) / 2.0, b1 = -(1.0 + c), b2 = (1.0 + c) / 2.0, a0 = 1.0 + alpha, a1 = -2.0 * c, a2 = 1.0 - alpha;
                break;
            case EqType::HighCut:
                b0 = (1.0 - c) / 2.0, b1 = 1.0 - c, b2 = (1.0 - c) / 2.0, a0 = 1.0 + alpha, a1 = -2.0 * c, a2 = 1.0 - alpha;
                break;
            case EqType::Bell:
                b0 = 1.0 + alpha * A, b1 = -2.0 * c, b2 = 1.0 - alpha * A, a0 = 1.0 + alpha / A, a1 = -2.0 * c, a2 = 1.0 - alpha / A;
                break;
            case EqType::Notch:
                b0 = 1.0, b1 = -2.0 * c, b2 = 1.0, a0 = 1.0 + alpha, a1 = -2.0 * c, a2 = 1.0 - alpha;
                break;
            case EqType::LowShelf:
                b0 = A * ((A + 1.0) - (A - 1.0) * c + 2.0 * rootA * alpha);
                b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * c);
                b2 = A * ((A + 1.0) - (A - 1.0) * c - 2.0 * rootA * alpha);
                a0 = (A + 1.0) + (A - 1.0) * c + 2.0 * rootA * alpha;
                a1 = -2.0 * ((A - 1.0) + (A + 1.0) * c);
                a2 = (A + 1.0) + (A - 1.0) * c - 2.0 * rootA * alpha;
                break;
            case EqType::HighShelf:
                b0 = A * ((A + 1.0) + (A - 1.0) * c + 2.0 * rootA * alpha);
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
                b2 = A * ((A + 1.0) + (A - 1.0) * c - 2.0 * rootA * alpha);
                a0 = (A + 1.0) - (A - 1.0) * c + 2.0 * rootA * alpha;
                a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c);
                a2 = (A + 1.0) - (A - 1.0) * c - 2.0 * rootA * alpha;
                break;
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    // The gain (dB) at a frequency, for drawing the curve.
    double magnitudeDb(double hz, double sampleRate) const
    {
        const double w = 2.0 * 3.14159265358979323846 * hz / sampleRate;
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
        const auto h = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
        return 20.0 * std::log10(std::max(1.0e-12, std::abs(h)));
    }
};

// The whole EQ's gain (dB) at a frequency: every band that is on, plus the output gain.
inline double eqResponseDb(const std::array<EqBand, kNumEqBands>& bands, float outputDb, double hz, double sampleRate)
{
    double db = outputDb;
    for (const auto& b : bands)
        if (b.on)
            db += Biquad::design(b, sampleRate).magnitudeDb(hz, sampleRate);
    return db;
}

// The EQ for one channel. Settings arrive once per block and glide to their new values over a few blocks, so moving a
// band does not click.
class Equalizer
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        current = defaultEqBands();
        primed = false;
        reset();
    }

    void reset()
    {
        for (auto& s : state)
            s = {};
    }

    void setBands(const std::array<EqBand, kNumEqBands>& target, float outputDb, int blockSamples)
    {
        if (!primed)
        {
            current = target;
            currentOutputDb = outputDb;
            primed = true;
        }
        const float k = 1.0f - std::exp(-(float) std::max(1, blockSamples) / (float) (0.02 * fs)); // about 20 ms
        for (int i = 0; i < kNumEqBands; ++i)
        {
            auto& c = current[(size_t) i];
            const auto& t = target[(size_t) i];
            if (c.type != t.type || c.on != t.on)
            {
                c = t; // a new kind of filter, or switched on: start from its own settings
                state[(size_t) i] = {};
            }
            else
            {
                c.freq = std::exp(std::log(c.freq) + (std::log(std::max(t.freq, 1.0f)) - std::log(c.freq)) * k);
                c.gainDb += (t.gainDb - c.gainDb) * k;
                c.q += (t.q - c.q) * k;
            }
            coeffs[(size_t) i] = Biquad::design(c, fs);
        }
        currentOutputDb += (outputDb - currentOutputDb) * k;
        outputGain = (float) std::pow(10.0, currentOutputDb / 20.0);
    }

    float process(float x)
    {
        double y = x;
        for (int i = 0; i < kNumEqBands; ++i)
        {
            if (!current[(size_t) i].on)
                continue;
            const auto& c = coeffs[(size_t) i];
            auto& s = state[(size_t) i];
            const double out = c.b0 * y + s.z1; // transposed direct form II
            s.z1 = c.b1 * y - c.a1 * out + s.z2;
            s.z2 = c.b2 * y - c.a2 * out;
            y = out;
        }
        return (float) y * outputGain;
    }

private:
    struct State
    {
        double z1 = 0.0, z2 = 0.0;
    };

    double fs = 48000.0;
    std::array<EqBand, kNumEqBands> current {};
    std::array<Biquad, kNumEqBands> coeffs {};
    std::array<State, kNumEqBands> state {};
    float currentOutputDb = 0.0f, outputGain = 1.0f;
    bool primed = false;
};
} // namespace bitvibe
