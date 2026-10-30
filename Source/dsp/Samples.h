#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bitvibe
{
// One short sound: mono, at its own sample rate, already normalised by whoever loaded it.
struct SampleClip
{
    std::vector<float> data;
    double rate = 44100.0;
};

// The sounds the player has picked (the sequencer chooses among these at random). Immutable once built, so the
// audio thread can read it while the message thread prepares the next one.
struct SampleSet
{
    std::vector<SampleClip> clips;
};

// Plays a few overlapping clips (linear interpolation, so any sample rate works). A new clip fades the ones
// still sounding out over a few milliseconds, so retriggering at speed does not click.
class SamplePlayer
{
public:
    static constexpr int kVoices = 8;

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        fadeOutStep = (float) (1.0 / std::max(1.0, 0.004 * fs));
        fadeInStep = (float) (1.0 / std::max(1.0, 0.0015 * fs));
        reset();
    }

    void reset()
    {
        for (auto& v : voices)
            v = Voice {};
    }

    // Starts a clip from its beginning. The clip must outlive its playback (the caller keeps the set alive).
    void trigger(const SampleClip* clip)
    {
        if (clip == nullptr || clip->data.empty())
            return;
        for (auto& v : voices)
            if (v.clip != nullptr)
                v.releasing = true;
        for (auto& v : voices)
        {
            if (v.clip == nullptr)
            {
                v.clip = clip;
                v.position = 0.0;
                v.step = clip->rate / fs;
                v.gain = 0.0f;
                v.releasing = false;
                return;
            }
        }
        // Every voice is busy fading out (very fast retriggering): drop this one.
    }

    // The next output sample, with every sounding clip summed.
    float process()
    {
        float sum = 0.0f;
        for (auto& v : voices)
        {
            if (v.clip == nullptr)
                continue;
            const auto& d = v.clip->data;
            const size_t i = (size_t) v.position;
            if (i >= d.size())
            {
                v = Voice {};
                continue;
            }
            const float frac = (float) (v.position - (double) i);
            const float a = d[i], b = i + 1 < d.size() ? d[i + 1] : 0.0f;
            sum += (a + (b - a) * frac) * v.gain;

            if (v.releasing)
            {
                v.gain -= fadeOutStep;
                if (v.gain <= 0.0f)
                {
                    v = Voice {};
                    continue;
                }
            }
            else if (v.gain < 1.0f)
            {
                v.gain = std::min(1.0f, v.gain + fadeInStep);
            }
            v.position += v.step;
        }
        return sum;
    }

    bool isPlaying() const
    {
        for (const auto& v : voices)
            if (v.clip != nullptr)
                return true;
        return false;
    }

private:
    struct Voice
    {
        const SampleClip* clip = nullptr;
        double position = 0.0, step = 1.0;
        float gain = 0.0f;
        bool releasing = false;
    };

    std::array<Voice, kVoices> voices;
    double fs = 48000.0;
    float fadeOutStep = 0.01f, fadeInStep = 0.01f;
};
} // namespace bitvibe
