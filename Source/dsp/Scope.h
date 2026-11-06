#pragma once

// What the SCOPE on DOG5's screen shows: the clean input and the sound coming out, kBucketsPerBeat buckets a beat
// (each one the lowest and highest sample in it, and the step that was playing), in a ring the editor reads. The
// buckets follow the beat clock, so the screen can draw the beats where they fall. Written by the audio thread, read
// by the editor with relaxed atomics: a bucket read while it is being written is only drawn wrong for one frame.
// Header-only and free of JUCE.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace bitvibe
{
class ScopeFeed
{
public:
    static constexpr int kBucketsPerBeat = 64, kBuckets = 1024; // 16 beats kept
    static constexpr int kNoStep = -1;

    struct Bucket
    {
        float inLo = 0.0f, inHi = 0.0f, outLo = 0.0f, outHi = 0.0f;
        int step = kNoStep; // the sequencer step playing (0..5), or kNoStep
    };

    // Audio thread, once a sample. beatPosition counts beats and only moves on (a jump starts a new bucket).
    void add(double beatPosition, float in, float out, int step)
    {
        const auto index = (std::int64_t) std::floor(beatPosition * (double) kBucketsPerBeat);
        if (index != current)
        {
            if (current >= 0 && filled)
                publish();
            current = index;
            filled = false;
        }
        if (!filled)
        {
            inLo = inHi = in;
            outLo = outHi = out;
            filled = true;
        }
        else
        {
            inLo = std::min(inLo, in);
            inHi = std::max(inHi, in);
            outLo = std::min(outLo, out);
            outHi = std::max(outHi, out);
        }
        lastStep = step;
    }

    // The newest bucket that is complete (-1 = none yet), and a bucket by its index (one of the last kBuckets).
    std::int64_t newest() const { return written.load(std::memory_order_acquire); }
    Bucket get(std::int64_t index) const
    {
        const auto i = slot(index);
        Bucket b;
        b.inLo = ins[i][0].load(std::memory_order_relaxed);
        b.inHi = ins[i][1].load(std::memory_order_relaxed);
        b.outLo = outs[i][0].load(std::memory_order_relaxed);
        b.outHi = outs[i][1].load(std::memory_order_relaxed);
        b.step = steps[i].load(std::memory_order_relaxed);
        return b;
    }
    bool has(std::int64_t index) const
    {
        const auto last = newest();
        return index >= 0 && index <= last && index > last - kBuckets && indices[slot(index)].load(std::memory_order_relaxed) == index;
    }

private:
    static std::size_t slot(std::int64_t index) { return (std::size_t) (((index % kBuckets) + kBuckets) % kBuckets); }

    void publish()
    {
        const auto i = slot(current);
        ins[i][0].store(inLo, std::memory_order_relaxed);
        ins[i][1].store(inHi, std::memory_order_relaxed);
        outs[i][0].store(outLo, std::memory_order_relaxed);
        outs[i][1].store(outHi, std::memory_order_relaxed);
        steps[i].store(lastStep, std::memory_order_relaxed);
        indices[i].store(current, std::memory_order_relaxed);
        written.store(current, std::memory_order_release);
    }

    std::array<std::array<std::atomic<float>, 2>, kBuckets> ins {}, outs {};
    std::array<std::atomic<int>, kBuckets> steps {};
    std::array<std::atomic<std::int64_t>, kBuckets> indices {};
    std::atomic<std::int64_t> written { -1 };
    // the bucket being filled (audio thread only)
    std::int64_t current = -1;
    bool filled = false;
    float inLo = 0.0f, inHi = 0.0f, outLo = 0.0f, outHi = 0.0f;
    int lastStep = kNoStep;
};
} // namespace bitvibe
