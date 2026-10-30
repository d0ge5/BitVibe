#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace bitvibe
{
// A basic room reverb for one channel: eight damped feedback combs in parallel into four allpasses
// (the classic Schroeder/Freeverb layout). Fixed room size and decay; the player only has a tone control.
// The right channel uses slightly longer delays so a stereo pair does not collapse to mono.
class Reverb
{
public:
    // channelIndex 0 = left, 1 = right (longer delays for a wider image).
    void prepare(double sampleRate, int channelIndex)
    {
        static constexpr int kCombs[kNumCombs] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr int kAllpasses[kNumAllpasses] { 556, 441, 341, 225 };
        const double scale = sampleRate / 44100.0;
        const int spread = channelIndex == 0 ? 0 : 23;
        for (int i = 0; i < kNumCombs; ++i)
            combs[(size_t) i].resize(std::max(1, (int) std::lround((kCombs[i] + spread) * scale)));
        for (int i = 0; i < kNumAllpasses; ++i)
            allpasses[(size_t) i].resize(std::max(1, (int) std::lround((kAllpasses[i] + spread) * scale)));
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& c : combs)
            c.clear();
        for (auto& a : allpasses)
            a.clear();
        toneState = 0.0f;
    }

    // tone 0..1: 0 is dark (a low-pass around 1.2 kHz), 1 is bright (around 16 kHz).
    float process(float in, float tone)
    {
        const float input = in * kInputGain;
        float sum = 0.0f;
        for (auto& c : combs)
            sum += c.process(input);
        for (auto& a : allpasses)
            sum = a.process(sum);

        const float hz = 1200.0f * std::pow(16000.0f / 1200.0f, std::clamp(tone, 0.0f, 1.0f));
        const float coef = 1.0f - std::exp(-6.2831853f * std::min(hz, 0.45f * (float) fs) / (float) fs);
        toneState += coef * (sum - toneState);
        return toneState;
    }

private:
    static constexpr int kNumCombs = 8, kNumAllpasses = 4;
    static constexpr float kInputGain = 0.03f;
    static constexpr float kFeedback = 0.84f, kDamp = 0.25f, kAllpassGain = 0.5f;

    struct Comb
    {
        std::vector<float> buffer;
        size_t pos = 0;
        float lowpass = 0.0f;

        void resize(int n) { buffer.assign((size_t) n, 0.0f); pos = 0; lowpass = 0.0f; }
        void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); pos = 0; lowpass = 0.0f; }

        float process(float in)
        {
            const float out = buffer[pos];
            lowpass = out * (1.0f - kDamp) + lowpass * kDamp;
            buffer[pos] = in + lowpass * kFeedback;
            if (++pos >= buffer.size())
                pos = 0;
            return out;
        }
    };

    struct Allpass
    {
        std::vector<float> buffer;
        size_t pos = 0;

        void resize(int n) { buffer.assign((size_t) n, 0.0f); pos = 0; }
        void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); pos = 0; }

        float process(float in)
        {
            const float delayed = buffer[pos];
            const float out = delayed - in;
            buffer[pos] = in + delayed * kAllpassGain;
            if (++pos >= buffer.size())
                pos = 0;
            return out;
        }
    };

    std::array<Comb, kNumCombs> combs;
    std::array<Allpass, kNumAllpasses> allpasses;
    double fs = 48000.0;
    float toneState = 0.0f;
};
} // namespace bitvibe
