#pragma once

// The sampler's processing: what a recorded clip goes through before it is saved as a sound for the sample blend.
// The same distortion and fuzz as the right-hand column, the pedal's sample-rate reducer and bit crusher, the 8-band EQ,
// and a volume. Runs offline on the whole clip. Header-only and free of JUCE so the tests can drive it.
//
// Order: distortion -> sample rate -> bits -> EQ -> level -> volume. "Level" brings the result to the same peak the
// sample blend gives every sound it loads (kSoundPeak), so VOLUME then says how loud this sound sits next to the
// others: 0 dB = as loud as any other sound, -6 dB = half as loud. Sounds saved by the sampler are marked so the
// sample blend keeps that level instead of levelling them again.

#include "Distortion.h"
#include "Equalizer.h"
#include "Fuzz.h"
#include "Mapping.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bitvibe
{
inline constexpr float kSoundPeak = 0.8f; // the peak the sample blend levels every sound to

struct SampleFx
{
    int distType = 0; // 0 = off, 1 = overdrive, 2 = fuzz
    float drive = 0.5f, tone = 0.5f, level = 0.8f;
    float sampleRateKnob = 1.0f; // like the SAMPLE knob: 0 = 48 Hz .. 1 = 48 kHz (off)
    float bitsKnob = 1.0f;       // like the BITS knob: 0 = 1 bit .. 1 = 24 bit (off)
    bool eqOn = false;
    float eqOutputDb = 0.0f;
    std::array<EqBand, kNumEqBands> eqBands = defaultEqBands();
    float volumeDb = 0.0f;       // -24 .. 0 dB: how loud the sound sits next to the others
};

// Renders a stereo clip (left and right the same length) through the chain into outL / outR. beforeEq, if given, gets
// the sound as it reaches the EQ (mono, at the same final gain), for a spectrum of what the EQ is working on.
inline void renderSample(const std::vector<float>& inL, const std::vector<float>& inR, double rate, const SampleFx& fx,
                         std::vector<float>& outL, std::vector<float>& outR, std::vector<float>* beforeEq = nullptr)
{
    const size_t n = std::min(inL.size(), inR.size());
    outL.assign(inL.begin(), inL.begin() + (long) n);
    outR.assign(inR.begin(), inR.begin() + (long) n);
    if (beforeEq != nullptr)
        beforeEq->assign(n, 0.0f);
    if (n == 0)
        return;

    std::vector<float>* channels[2] { &outL, &outR };
    const float srHz = mapping::sampleRateHz(std::clamp(fx.sampleRateKnob, 0.0f, 1.0f));
    const bool srOn = srHz < 47000.0f;
    const float bits = mapping::bitDepth(std::clamp(fx.bitsKnob, 0.0f, 1.0f));
    const bool bitsOn = bits < 23.5f;
    const float levels = std::exp2(bits - 1.0f);

    for (auto* ch : channels)
    {
        auto& x = *ch;
        if (fx.distType == 1)
        {
            Distortion d;
            d.prepare(rate);
            d.setParams(fx.drive, fx.tone, fx.level);
            for (auto& v : x)
                v = d.process(v);
        }
        else if (fx.distType == 2)
        {
            Fuzz f;
            f.prepare(rate);
            f.setParams(fx.drive, fx.tone, fx.level);
            for (auto& v : x)
                v = f.process(v);
        }
        if (srOn) // hold each sample for 1/srHz seconds
        {
            double phase = 1.0;
            float held = 0.0f;
            for (auto& v : x)
            {
                phase += (double) srHz / rate;
                if (phase >= 1.0)
                {
                    phase -= std::floor(phase);
                    held = v;
                }
                v = held;
            }
        }
        if (bitsOn)
            for (auto& v : x)
                v = (std::floor(std::clamp(v, -1.0f, 1.0f - 1.0e-6f) * levels) + 0.5f) / levels;
        if (beforeEq != nullptr)
            for (size_t i = 0; i < n; ++i)
                (*beforeEq)[i] += 0.5f * x[i];
        if (fx.eqOn)
        {
            Equalizer eq;
            eq.prepare(rate);
            eq.setBands(fx.eqBands, fx.eqOutputDb, (int) n); // the first call sets the bands straight away
            for (auto& v : x)
                v = eq.process(v);
        }
    }

    float peak = 0.0f;
    for (auto* ch : channels)
        for (const float v : *ch)
            if (std::isfinite(v))
                peak = std::max(peak, std::abs(v));
    const float gain = peak > 1.0e-6f ? kSoundPeak / peak * std::pow(10.0f, std::clamp(fx.volumeDb, -60.0f, 0.0f) / 20.0f) : 0.0f;
    for (auto* ch : channels)
        for (auto& v : *ch)
            v = std::isfinite(v) ? v * gain : 0.0f;
    if (beforeEq != nullptr)
        for (auto& v : *beforeEq)
            v = std::isfinite(v) ? v * gain : 0.0f;
}
} // namespace bitvibe
