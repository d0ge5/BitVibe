#pragma once

#include "Distortion.h"

namespace bitvibe
{
// A fuzz in the style of the classic three-knob "big" fuzz pedal: SUSTAIN, TONE and VOLUME (the same three knobs
// as Distortion: drive = sustain, level = volume).
//
//  - Input boost: a transistor gain stage ahead of the clipping stages.
//  - Two clipping stages in series, each a gain stage with a pair of silicon diodes in its feedback loop (a soft
//    symmetric clip at about 0.6 V) and a high-pass between them (the coupling capacitors). SUSTAIN sets the gain
//    into the first one, so more sustain means a longer, squarer, more compressed note.
//  - Tone stack: a low-pass (about 400 Hz) and a high-pass (about 1.6 kHz) crossfaded by TONE, which leaves the
//    famous scoop in the middle with TONE at noon. The scoop costs level, so there is make-up gain after it.
//  - Volume: the output level (-30 dB .. +6 dB).
// The clipping runs at twice the sample rate. Mono: use one instance per channel.
class Fuzz
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        const double rate2 = 2.0 * fs;
        couplingCoef = onePoleCoefficient(110.0, rate2);
        stageLowpassCoef = onePoleCoefficient(7000.0, rate2); // the capacitors across the feedback resistors
        toneLowCoef = onePoleCoefficient(400.0, rate2);
        toneHighCoef = onePoleCoefficient(1600.0, rate2);
        inputBlockCoef = onePoleCoefficient(25.0, fs);
        reset();
        setParams(0.5f, 0.5f, 0.5f);
        sustainGainSmoothed = sustainGainTarget;
        levelSmoothed = levelTarget;
        toneSmoothed = toneTarget;
    }

    void reset()
    {
        oversampler.reset();
        blockState = 0.0f;
        for (auto& s : stage)
            s = {};
        toneLow = toneHigh = 0.0f;
    }

    // sustain, tone, volume all 0..1.
    void setParams(float sustain, float tone, float volume)
    {
        sustain = std::clamp(sustain, 0.0f, 1.0f);
        sustainGainTarget = 0.6f * std::pow(100.0f, sustain); // 0.6 .. 60 into the first clipper
        toneTarget = std::clamp(tone, 0.0f, 1.0f);
        levelTarget = std::pow(10.0f, (-30.0f + 36.0f * std::clamp(volume, 0.0f, 1.0f)) / 20.0f);
    }

    float process(float in)
    {
        sustainGainSmoothed += (sustainGainTarget - sustainGainSmoothed) * 0.002f;
        levelSmoothed += (levelTarget - levelSmoothed) * 0.002f;
        toneSmoothed += (toneTarget - toneSmoothed) * 0.002f;

        blockState += inputBlockCoef * (in - blockState);
        const float x = (in - blockState) * kInputScale;
        return oversampler.process(x, [this](float v) { return nonlinear(v); }) * (kOutputScale * levelSmoothed);
    }

private:
    static constexpr float kInputScale = 0.5f;
    static constexpr float kClip = 0.6f;
    static constexpr float kBoost = 3.0f;       // the first transistor stage
    static constexpr float kSecondGain = 6.0f;  // gain into the second clipper
    static constexpr float kOutputScale = 3.0f; // make-up for the tone stack's scoop and the 0.6 V ceiling

    struct Stage
    {
        float coupling = 0.0f, lowpass = 0.0f;
    };

    float clipStage(Stage& s, float v, float gain)
    {
        s.coupling += couplingCoef * (v - s.coupling); // high-pass: the coupling capacitor
        const float amplified = (v - s.coupling) * gain;
        s.lowpass += stageLowpassCoef * (amplified - s.lowpass);
        return kClip * std::tanh(s.lowpass / kClip);
    }

    float nonlinear(float v)
    {
        float y = clipStage(stage[0], v * kBoost, sustainGainSmoothed);
        y = clipStage(stage[1], y, kSecondGain);

        toneLow += toneLowCoef * (y - toneLow);
        toneHigh += toneHighCoef * (y - toneHigh);
        const float highpassed = y - toneHigh;
        return toneLow * (1.0f - toneSmoothed) + highpassed * toneSmoothed;
    }

    double fs = 48000.0;
    Oversampler2x oversampler;
    float couplingCoef = 0.0f, stageLowpassCoef = 0.0f, toneLowCoef = 0.0f, toneHighCoef = 0.0f, inputBlockCoef = 0.0f;
    std::array<Stage, 2> stage {};
    float toneLow = 0.0f, toneHigh = 0.0f, blockState = 0.0f;
    float sustainGainTarget = 10.0f, sustainGainSmoothed = 10.0f, levelTarget = 1.0f, levelSmoothed = 1.0f;
    float toneTarget = 0.5f, toneSmoothed = 0.5f;
};
} // namespace bitvibe
