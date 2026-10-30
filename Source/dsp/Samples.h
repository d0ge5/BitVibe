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

// Plays a few overlapping clips (linear interpolation, so any sample rate works). A new clip fades the ones of its
// own kind that are still sounding out over a few milliseconds, so retriggering at speed does not click. There are
// two kinds: the sounds the sequencer plays, and the preview the player asks for while setting the volumes.
class SamplePlayer
{
public:
    static constexpr int kVoices = 8;

    struct Output
    {
        float sequence = 0.0f;     // what the sequencer's sounds add up to (before the sample mix is applied)
        float loopSequence = 0.0f; // the same for the sounds started in the sequence's loop section (its own mix)
        float preview = 0.0f;      // the preview, already at its own volume
    };

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

    // Starts a clip from its beginning. The clip must outlive its playback (the caller keeps the set alive). loopSection
    // puts a sequence sound in the loop section's output.
    void trigger(const SampleClip* clip, float level = 1.0f, bool preview = false, bool loopSection = false)
    {
        if (clip == nullptr || clip->data.empty())
            return;
        for (auto& v : voices)
            if (v.clip != nullptr && v.preview == preview)
                v.releasing = true;
        for (auto& v : voices)
        {
            if (v.clip == nullptr)
            {
                v.clip = clip;
                v.position = 0.0;
                v.step = clip->rate / fs;
                v.gain = 0.0f;
                v.level = level;
                v.preview = preview;
                v.loopSection = loopSection;
                v.releasing = false;
                return;
            }
        }
        // Every voice is busy fading out (very fast retriggering): drop this one.
    }

    // Fades out the sequencer's sounds (the sequence has stopped). A preview plays on.
    void release()
    {
        for (auto& v : voices)
            if (v.clip != nullptr && !v.preview)
                v.releasing = true;
    }

    // The next output sample, with every sounding clip summed.
    Output process()
    {
        Output out;
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
            const float value = (a + (b - a) * frac) * v.gain;
            if (v.preview)
                out.preview += value * v.level;
            else if (v.loopSection)
                out.loopSequence += value;
            else
                out.sequence += value;

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
        return out;
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
        float gain = 0.0f, level = 1.0f;
        bool releasing = false, preview = false, loopSection = false;
    };

    std::array<Voice, kVoices> voices;
    double fs = 48000.0;
    float fadeOutStep = 0.01f, fadeInStep = 0.01f;
};
} // namespace bitvibe
