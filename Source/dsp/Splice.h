#pragma once

// SPLICE: the sound of clips butted together in a DAW with no fades. At each edit the audio stops dead mid-waveform and
// the next clip starts dead at a point of its own, so the waveform jumps from one value to another in a single sample
// (the click), sometimes with a sliver of digital silence between the two clips.
//
// It works on a short history of the sound: between edits the output is the history read at a fixed delay (0 = the
// sound as it is). An edit moves the read point to a new delay at random, up to the JUMP, in one sample, after up to
// GAP of silence. Stopped (the sequence ends, or SPLICE is switched off), the read point glides back to no delay over a
// few milliseconds, so it never clicks by itself.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bitvibe
{
inline constexpr float kSpliceMaxJumpMs = 250.0f, kSpliceMaxGapMs = 40.0f;

// When the edits can fall: at the start of each sequencer step, on every sixteenth note of the bar, or anywhere (at
// random moments, on average eight a beat at full CHANCE).
enum class SpliceWhen
{
    Steps = 0,
    Sixteenths,
    Anywhere,
};
inline constexpr int kNumSpliceWhen = 3;

struct SpliceSettings
{
    bool on = false;
    SpliceWhen when = SpliceWhen::Steps;
    float chance = 0.5f; // 0..1: how likely each chance of an edit is to become one
    float jumpMs = 40.0f; // how far back the next clip may start (0..kSpliceMaxJumpMs)
    float gapMs = 0.0f;   // the most silence between two clips (0..kSpliceMaxGapMs)
};

class Splicer
{
public:
    static constexpr int kChannels = 2;

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        size = (int) std::ceil((kSpliceMaxJumpMs + 20.0f) * 0.001 * fs);
        for (auto& h : history)
            h.assign((size_t) size, 0.0f);
        glideLength = std::max(1, (int) (0.006 * fs));
        reset();
    }

    void reset()
    {
        for (auto& h : history)
            std::fill(h.begin(), h.end(), 0.0f);
        write = 0;
        delay = 0;
        gapLeft = glideLeft = 0;
    }

    // Nothing to undo: no delay, no gap, no glide.
    bool idle() const { return delay == 0 && gapLeft == 0 && glideLeft == 0; }
    int currentDelay() const { return delay; }

    // An edit: the next clip starts newDelay samples back, after gap samples of silence. Abrupt by design.
    void cut(int newDelay, int gap)
    {
        delay = std::clamp(newDelay, 0, size - 1);
        gapLeft = std::max(0, gap);
        glideLeft = 0;
    }

    // Back to the sound as it is, smoothly (once any gap has run out).
    void release()
    {
        if (delay > 0 && glideLeft == 0 && gapLeft == 0)
            glideLeft = glideLength;
    }

    // One channel's sample: records it and returns what the edit makes of it. Call endSample() after the channels.
    float process(int c, float x)
    {
        auto& h = history[(size_t) c];
        h[(size_t) write] = x;
        if (gapLeft > 0)
            return 0.0f;
        if (delay == 0)
            return x;
        int at = write - delay;
        if (at < 0)
            at += size;
        const float delayed = h[(size_t) at];
        if (glideLeft > 0) // gliding back: from the delayed clip to the sound as it is
        {
            const float t = (float) glideLeft / (float) glideLength;
            const float w = 0.5f - 0.5f * std::cos(3.14159265f * t);
            return x + (delayed - x) * w;
        }
        return delayed;
    }

    void endSample()
    {
        if (++write >= size)
            write = 0;
        if (gapLeft > 0)
            --gapLeft;
        else if (glideLeft > 0 && --glideLeft == 0)
            delay = 0;
    }

private:
    double fs = 48000.0;
    std::array<std::vector<float>, kChannels> history;
    int size = 1, write = 0;
    int delay = 0;            // how far back the clip playing now reads, in samples
    int gapLeft = 0;          // samples of silence left before the next clip starts
    int glideLeft = 0, glideLength = 288;
};
} // namespace bitvibe
