#pragma once

#include "Oversample.h"

#include <algorithm>
#include <cmath>

namespace bitvibe
{
inline float onePoleCoefficient(double hz, double rate)
{
    return (float) (1.0 - std::exp(-2.0 * 3.14159265358979 * std::min(hz, 0.45 * rate) / rate));
}

// An overdrive in the style of the classic green mid-hump pedal (the 808 circuit): DRIVE, TONE and LEVEL.
//
//  - Gain stage: an op-amp whose gain is 1 + Rf / Rg. Below about 720 Hz (a 4.7 k resistor in series with
//    47 nF) the gain is 1; above it, it is 1 + (51 k + DRIVE x 500 k) / 4.7 k, so the highs and mids are pushed
//    into the clipper much harder than the lows. That is the "mid hump". A 51 pF capacitor across Rf rolls the
//    highs off (6 kHz at full drive, 60 kHz at none).
//  - Clipper: a pair of silicon diodes in the feedback loop, a soft symmetric clip at about 0.6 V.
//  - Tone: a low-pass whose cutoff TONE moves from about 700 Hz to 6.5 kHz.
//  - Level: the output volume (-30 dB .. +6 dB).
// The gain stage, clipper and tone filter run at twice the sample rate so the clipper does not alias much.
// Mono: use one instance per channel.
class Distortion
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        lowSplitCoef = onePoleCoefficient(720.0, 2.0 * fs);
        inputBlockCoef = onePoleCoefficient(25.0, fs);
        reset();
        setParams(0.5f, 0.5f, 0.5f);
        gainSmoothed = gainTarget;
        levelSmoothed = levelTarget;
    }

    void reset()
    {
        oversampler.reset();
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
        driveCoef = onePoleCoefficient(1.0 / (2.0 * 3.14159265358979 * rf * 51.0e-12), 2.0 * fs);
        toneCoef = onePoleCoefficient(700.0 * std::pow(6500.0 / 700.0, (double) tone), 2.0 * fs);
        levelTarget = std::pow(10.0f, (-30.0f + 36.0f * level) / 20.0f);
    }

    float process(float in)
    {
        // slew the gains a little so turning a knob does not click
        gainSmoothed += (gainTarget - gainSmoothed) * 0.002f;
        levelSmoothed += (levelTarget - levelSmoothed) * 0.002f;

        blockState += inputBlockCoef * (in - blockState); // removes DC and the lowest rumble before the gain stage
        const float x = (in - blockState) * kInputScale;
        return oversampler.process(x, [this](float v) { return nonlinear(v); }) * (kOutputScale * levelSmoothed);
    }

private:
    static constexpr float kInputScale = 0.5f;  // a full-scale digital signal is treated as about half a volt
    static constexpr float kClip = 0.6f;        // diode knee, volts
    static constexpr float kOutputScale = 1.1f; // brings the clipped peak (about 0.6 V) back up to a useful level

    float nonlinear(float v)
    {
        // Gain: unity below 720 Hz, gainSmoothed above it.
        lowState += lowSplitCoef * (v - lowState);
        const float stage = lowState + (v - lowState) * gainSmoothed;

        // The capacitor across the feedback resistor: highs roll off, more so with more drive.
        driveLowpass += driveCoef * (stage - driveLowpass);

        // The diodes.
        const float clipped = kClip * std::tanh(driveLowpass / kClip);

        // Tone.
        toneState += toneCoef * (clipped - toneState);
        return toneState;
    }

    double fs = 48000.0;
    Oversampler2x oversampler;
    float lowSplitCoef = 0.0f, inputBlockCoef = 0.0f;
    float lowState = 0.0f, driveLowpass = 0.0f, toneState = 0.0f, blockState = 0.0f;
    float driveCoef = 0.5f, toneCoef = 0.5f;
    float gainTarget = 10.0f, gainSmoothed = 10.0f, levelTarget = 1.0f, levelSmoothed = 1.0f;
};
} // namespace bitvibe
