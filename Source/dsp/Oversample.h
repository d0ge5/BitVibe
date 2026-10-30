#pragma once

#include <array>
#include <cmath>

namespace bitvibe
{
// 2x oversampling with a 15-tap windowed-sinc halfband filter on the way up and on the way down, for the
// nonlinear effects (distortion, fuzz) so their clipping does not alias much. Mono.
class Oversampler2x
{
public:
    Oversampler2x()
    {
        constexpr double pi = 3.14159265358979;
        for (int n = 0; n < kTaps; ++n)
        {
            const int k = n - kTaps / 2;
            const double sinc = k == 0 ? 0.5 : std::sin(pi * k / 2.0) / (pi * k);
            const double window = 0.42 - 0.5 * std::cos(2.0 * pi * (n + 0.5) / kTaps) + 0.08 * std::cos(4.0 * pi * (n + 0.5) / kTaps);
            taps[(size_t) n] = (float) (sinc * window);
        }
    }

    void reset()
    {
        upHistory.fill(0.0f);
        downHistory.fill(0.0f);
        pos = 0;
    }

    // Runs nonlinear(float) on two samples at twice the rate and returns one sample at the original rate.
    template <typename Nonlinear>
    float process(float x, Nonlinear&& nonlinear)
    {
        float out = 0.0f;
        for (int phase = 0; phase < 2; ++phase)
        {
            pos = (pos + 1) % kTaps;
            upHistory[(size_t) pos] = phase == 0 ? 2.0f * x : 0.0f;
            float v = 0.0f;
            for (int n = 0; n < kTaps; ++n)
                v += taps[(size_t) n] * upHistory[(size_t) ((pos - n + kTaps) % kTaps)];

            downHistory[(size_t) pos] = nonlinear(v);
            if (phase == 1)
            {
                float d = 0.0f;
                for (int n = 0; n < kTaps; ++n)
                    d += taps[(size_t) n] * downHistory[(size_t) ((pos - n + kTaps) % kTaps)];
                out = d;
            }
        }
        return out;
    }

private:
    static constexpr int kTaps = 15;
    std::array<float, kTaps> taps {}, upHistory {}, downHistory {};
    int pos = 0;
};
} // namespace bitvibe
