#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace bitvibe
{
// An overdrive in the style of the classic green mid-hump pedal (the 808 circuit): DRIVE, TONE and LEVEL.
//
//  - Gain stage: an op-amp whose gain is 1 + Rf / Rg. Below about 720 Hz (a 4.7 k resistor in series with
//    47 nF) the gain is 1; above it, it is 1 + (51 k + DRIVE x 500 k) / 4.7 k, so the highs and mids are pushed
//    into the clipper much harder than the lows. That is the "mid hump". A 51 pF capacitor across Rf rolls the
//    highs off (6 kHz at full drive, 60 kHz at none).
//  - Clipper: a pair of silicon diodes in the feedback loop, a soft symmetric clip at about 0.6 V.
//  - Tone: a low-pass whose cutoff TONE moves from about 700 Hz to 6.5 kHz.
//  - Level: the output volume (-30 dB .. +6 dB).
// The gain stage, clipper and tone filter run at twice the sample rate (halfband filters in and out) so the
// clipper does not alias much. Mono: use one instance per channel.
class Distortion
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        // Windowed-sinc halfband low-pass for the 2x conversions (15 taps, every other tap is zero).
        constexpr double pi = 3.14159265358979;
        for (int n = 0; n < kTaps; ++n)
        {
            const int k = n - kTaps / 2;
            const double sinc = k == 0 ? 0.5 : std::sin(pi * k / 2.0) / (pi * k);
            const double window = 0.42 - 0.5 * std::cos(2.0 * pi * (n + 0.5) / kTaps) + 0.08 * std::cos(4.0 * pi * (n + 0.5) / kTaps);
            taps[(size_t) n] = (float) (sinc * window);
        }
        lowSplitCoef = coefficient(720.0, 2.0 * fs);
        inputBlockCoef = coefficient(25.0, fs);
        reset();
        setParams(0.5f, 0.5f, 0.5f);
        gainSmoothed = gainTarget;
        levelSmoothed = levelTarget;
    }

    void reset()
    {
        upHistory.fill(0.0f);
        downHistory.fill(0.0f);
        histPos = 0;
        lowState = driveLowpass = toneState = blockState = 0.0f;
    }

    // drive, tone, level all 0..1. Cheap enough to call every block.
    void setParams(float drive, float tone, float level)
    {
        drive = std::clamp(drive, 0.0f, 1.0f);
        tone = std::clamp(tone, 0.0f, 1.0f);
        level = std::clamp(level, 0.0f, 1.0f);
        const double rf = 51000.0 + (double) drive * 500000.0;
        gainTarget = (float) (1.0 + rf / 4700.0);
        driveCoef = coefficient(1.0 / (2.0 * 3.14159265358979 * rf * 51.0e-12), 2.0 * fs);
        toneCoef = coefficient(700.0 * std::pow(6500.0 / 700.0, (double) tone), 2.0 * fs);
        levelTarget = std::pow(10.0f, (-30.0f + 36.0f * level) / 20.0f);
    }

    float process(float in)
    {
        // slew the gains a little so turning a knob does not click
        gainSmoothed += (gainTarget - gainSmoothed) * 0.002f;
        levelSmoothed += (levelTarget - levelSmoothed) * 0.002f;

        blockState += inputBlockCoef * (in - blockState); // removes DC and the lowest rumble before the gain stage
        const float x = (in - blockState) * kInputScale;

        // 2x: zero-stuff, halfband filter, run the nonlinear part on both samples, filter, keep one of every two
        float out = 0.0f;
        for (int phase = 0; phase < 2; ++phase)
        {
            histPos = (histPos + 1) % kTaps;
            upHistory[(size_t) histPos] = phase == 0 ? 2.0f * x : 0.0f;
            float v = 0.0f;
            for (int n = 0; n < kTaps; ++n)
                v += taps[(size_t) n] * upHistory[(size_t) ((histPos - n + kTaps) % kTaps)];

            downHistory[(size_t) histPos] = nonlinear(v);
            if (phase == 1)
            {
                float d = 0.0f;
                for (int n = 0; n < kTaps; ++n)
                    d += taps[(size_t) n] * downHistory[(size_t) ((histPos - n + kTaps) % kTaps)];
                out = d;
            }
        }
        return out * (kOutputScale * levelSmoothed);
    }

private:
    static constexpr int kTaps = 15;
    static constexpr float kInputScale = 0.5f;  // a full-scale digital signal is treated as about half a volt
    static constexpr float kClip = 0.6f;        // diode knee, volts
    static constexpr float kOutputScale = 1.1f; // brings the clipped peak (about 0.6 V) back up to a useful level

    static float coefficient(double hz, double rate)
    {
        return (float) (1.0 - std::exp(-2.0 * 3.14159265358979 * std::min(hz, 0.45 * rate) / rate));
    }

    float nonlinear(float v)
    {
        // Gain: unity below 720 Hz, gainSmoothed above it.
        lowState += lowSplitCoef * (v - lowState);
        float stage = lowState + (v - lowState) * gainSmoothed;

        // The capacitor across the feedback resistor: highs roll off, more so with more drive.
        driveLowpass += driveCoef * (stage - driveLowpass);

        // The diodes.
        const float clipped = kClip * std::tanh(driveLowpass / kClip);

        // Tone.
        toneState += toneCoef * (clipped - toneState);
        return toneState;
    }

    double fs = 48000.0;
    std::array<float, kTaps> taps {}, upHistory {}, downHistory {};
    int histPos = 0;
    float lowSplitCoef = 0.0f, inputBlockCoef = 0.0f;
    float lowState = 0.0f, driveLowpass = 0.0f, toneState = 0.0f, blockState = 0.0f;
    float driveCoef = 0.5f, toneCoef = 0.5f;
    float gainTarget = 10.0f, gainSmoothed = 10.0f, levelTarget = 1.0f, levelSmoothed = 1.0f;
};
} // namespace bitvibe
