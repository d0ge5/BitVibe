#pragma once

// The tremolo on the sequenced sound: it cuts the volume up and down at a rate that can stay the same or speed up or
// slow down while a sequence or stutter plays. The rate starts at rateHz when the sequence starts and moves to
// rateHz x 2^endOctaves over rampBeats beats, along a curve that bend shapes, then stays there. Header-only and free of
// JUCE, so the engine, the editor's curve and the offline tests share one definition.

#include <algorithm>
#include <array>
#include <cmath>

namespace bitvibe
{
inline constexpr float kTremoloMinHz = 0.5f, kTremoloMaxHz = 20.0f;
inline constexpr float kTremoloMaxOctaves = 3.0f;                  // the end rate: up to 8 times faster or slower
inline constexpr std::array<int, 5> kTremoloRampBeats { 1, 2, 4, 8, 16 }; // how long the change takes
inline constexpr int kDefaultTremoloRamp = 2;                      // 4 beats

struct TremoloSettings
{
    float rateHz = 6.0f;      // at the start
    float depth = 0.8f;       // 0 = no change, 1 = cuts right out
    float edge = 0.3f;        // 0 = a smooth swell (sine), 1 = a hard chop (nearly square)
    float endOctaves = 0.0f;  // where the rate ends up: + faster (shorter period), - slower, 0 = constant
    float bend = 0.0f;        // -1..1: the change comes late (-), evenly (0) or early (+)
    int rampBeats = 4;
};

// How far along the change the rate is, 0..1, at u (0..1 of the ramp).
inline float tremoloRampShape(float u, float bend)
{
    u = std::clamp(u, 0.0f, 1.0f);
    return std::pow(u, std::exp2(-2.0f * std::clamp(bend, -1.0f, 1.0f)));
}

// The rate at u (0..1 of the ramp, past 1 it stays at the end rate).
inline float tremoloRateHz(const TremoloSettings& s, float u)
{
    const float hz = s.rateHz * std::exp2(s.endOctaves * tremoloRampShape(u, s.bend));
    return std::clamp(hz, kTremoloMinHz / 8.0f, kTremoloMaxHz * 8.0f);
}

// The volume at a point of the tremolo's cycle (phase 0..1, full volume at 0).
inline float tremoloGain(float phase, float depth, float edge)
{
    const float c = std::cos(6.28318530718f * phase);
    const float hardness = 1.0f + 24.0f * edge * edge;
    const float up = 0.5f + 0.5f * std::clamp(c * hardness, -1.0f, 1.0f); // 1 = full volume, 0 = cut
    return 1.0f - std::clamp(depth, 0.0f, 1.0f) * (1.0f - up);
}

// Runs the cycle: restart() when a sequence or stutter starts, next() once a sample.
class Tremolo
{
public:
    void restart()
    {
        phase = 0.0;
        samples = 0.0;
    }

    // The volume for this sample. beatSamples is the length of a beat at the tempo.
    float next(const TremoloSettings& s, double sampleRate, double beatSamples)
    {
        const double rampSamples = std::max(1.0, beatSamples * (double) s.rampBeats);
        const float u = (float) std::min(1.0, samples / rampSamples);
        const float gain = tremoloGain((float) phase, s.depth, s.edge);
        phase += (double) tremoloRateHz(s, u) / sampleRate;
        phase -= std::floor(phase);
        samples += 1.0;
        return gain;
    }

    double getPhase() const { return phase; }
    // How far through the ramp it is (0..1).
    float rampPosition(const TremoloSettings& s, double beatSamples) const
    {
        return (float) std::min(1.0, samples / std::max(1.0, beatSamples * (double) s.rampBeats));
    }

private:
    double phase = 0.0, samples = 0.0;
};
} // namespace bitvibe
