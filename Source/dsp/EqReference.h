#pragma once

// The EQ's reference sound. The sequenced sound as it reaches the EQ is kept for the last few seconds, so the player can
// capture it as a clip and loop it through the EQ (and save it), and a short feed of the sound before and after the EQ
// lets the editor draw its spectrum. A capture keeps up to kMaxClipSeconds; the clip length the player sets decides
// how much of its end is used (played and saved), so it can be changed after capturing. Header-only and free of JUCE
// so the tests can drive it.
//
// Threads: record / endSample / clipSample run on the audio thread; requestCapture, setAudition, the clip queries,
// copyClip and readScope on the message thread. A capture is done by the audio thread (it swaps the history and clip
// buffers at the start of a block), so the message thread only reads the clip while no capture is pending.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace bitvibe
{
class EqReference
{
public:
    static constexpr double kMaxClipSeconds = 8.0;
    static constexpr float kDefaultClipSeconds = 4.0f;
    static constexpr int kScopeSize = 8192; // a power of two
    static constexpr int kMaxChannels = 2;

    // Allocates the buffers (not on the audio thread) and starts a fresh history. The clip is kept when the sample rate
    // stays the same (hosts prepare again on many occasions), and forgotten when it changes.
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        const int newLength = std::max(1, (int) std::lround(sampleRate * kMaxClipSeconds));
        writePos = filled = playPos = 0;
        recordChannels = 1;
        served = captureRequests.load(std::memory_order_acquire);
        if (newLength == length && history[0].size() == (size_t) length)
        {
            servedOut.store(served, std::memory_order_release);
            return;
        }
        length = newLength;
        for (int c = 0; c < kMaxChannels; ++c)
        {
            history[(size_t) c].assign((size_t) length, 0.0f);
            clip[(size_t) c].assign((size_t) length, 0.0f);
        }
        clipStart = clipLength = 0;
        clipChannels = 1;
        servedOut.store(served, std::memory_order_release);
        clipSamples.store(0, std::memory_order_release);
        for (auto& v : preScope)
            v.store(0.0f, std::memory_order_relaxed);
        for (auto& v : postScope)
            v.store(0.0f, std::memory_order_relaxed);
    }

    // ---- audio thread ---------------------------------------------------------------------------------------------

    // Start of a block: carries out a capture that was asked for, and reads the audition switch.
    void beginBlock(int numChannels)
    {
        recordChannels = std::clamp(numChannels, 1, kMaxChannels);
        const int requests = captureRequests.load(std::memory_order_acquire);
        if (requests != served)
        {
            served = requests;
            capture();
        }
        useLength = clipLength > 0 ? std::clamp(lengthFor(useSeconds.load(std::memory_order_relaxed)), 1, clipLength) : 0;
        if (playPos >= useLength)
            playPos = 0;
        playing = auditionOn.load(std::memory_order_relaxed) && useLength > 0;
    }

    // True while the clip plays through the EQ instead of the live sound.
    bool isAuditioning() const { return playing; }

    void record(int channel, float value)
    {
        if (!history[(size_t) channel].empty())
            history[(size_t) channel][(size_t) writePos] = value;
    }

    float clipSample(int channel) const
    {
        const int c = channel < clipChannels ? channel : 0;
        return clip[(size_t) c][(size_t) ((clipStart + (clipLength - useLength) + playPos) % length)];
    }

    // End of a sample: moves the recording or the playback on, and feeds the spectrum (mono, before and after the EQ).
    void endSample(float beforeEq, float afterEq)
    {
        if (playing)
        {
            if (++playPos >= useLength)
                playPos = 0;
        }
        else if (!history[0].empty())
        {
            writePos = (writePos + 1) % length;
            filled = std::min(filled + 1, length);
        }
        preScope[(size_t) scopePos].store(beforeEq, std::memory_order_relaxed);
        postScope[(size_t) scopePos].store(afterEq, std::memory_order_relaxed);
        scopePos = (scopePos + 1) & (kScopeSize - 1);
        scopeWritten.store(scopePos, std::memory_order_release);
    }

    // ---- message thread -------------------------------------------------------------------------------------------

    // Keeps the last few seconds (up to kClipSeconds) as the clip, without the silence at either end.
    void requestCapture() { captureRequests.fetch_add(1, std::memory_order_acq_rel); }
    bool isCapturePending() const { return captureRequests.load(std::memory_order_acquire) != servedOut.load(std::memory_order_acquire); }
    void setAudition(bool on) { auditionOn.store(on, std::memory_order_relaxed); }
    bool getAudition() const { return auditionOn.load(std::memory_order_relaxed); }
    // The whole captured clip's length in samples (0 = none, or the last capture heard only silence).
    int getClipSamples() const { return clipSamples.load(std::memory_order_acquire); }
    // How much of the end of the clip is played and saved: the clip length the player has set, or all of it when it is shorter.
    void setClipSeconds(float seconds) { useSeconds.store(std::clamp(seconds, 0.1f, (float) kMaxClipSeconds), std::memory_order_relaxed); }
    float getClipSeconds() const { return useSeconds.load(std::memory_order_relaxed); }
    int getUsedSamples() const { return std::min(getClipSamples(), lengthFor(getClipSeconds())); }

    // Copies the part of the clip in use, one vector per channel. False (and nothing copied) while there is none or a
    // capture is pending.
    bool copyClip(std::vector<std::vector<float>>& out) const
    {
        const int n = getClipSamples(), used = getUsedSamples();
        if (n <= 0 || used <= 0 || isCapturePending())
            return false;
        out.assign((size_t) clipChannelsOut.load(std::memory_order_acquire), {});
        for (size_t c = 0; c < out.size(); ++c)
        {
            out[c].resize((size_t) used);
            for (int i = 0; i < used; ++i)
                out[c][(size_t) i] = clip[c][(size_t) ((clipStart + (n - used) + i) % length)];
        }
        return true;
    }

    // The latest n samples (n <= kScopeSize) of the sound before and after the EQ, oldest first.
    void readScope(float* before, float* after, int n) const
    {
        n = std::clamp(n, 0, kScopeSize);
        const int end = scopeWritten.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const int idx = (end - n + i + kScopeSize) & (kScopeSize - 1);
            before[i] = preScope[(size_t) idx].load(std::memory_order_relaxed);
            after[i] = postScope[(size_t) idx].load(std::memory_order_relaxed);
        }
    }

private:
    int lengthFor(float seconds) const { return std::max(1, (int) std::lround((double) seconds * rate)); }

    // The history becomes the clip (a swap, no copying), trimmed to where the sound is louder than -60 dBFS.
    void capture()
    {
        std::swap(history, clip);
        const int start = (writePos - filled + length) % length;
        auto loud = [&](int i)
        {
            for (int c = 0; c < recordChannels; ++c)
                if (std::abs(clip[(size_t) c][(size_t) ((start + i) % length)]) > 1.0e-3f)
                    return true;
            return false;
        };
        int first = 0, last = filled - 1;
        while (first <= last && !loud(first))
            ++first;
        while (last >= first && !loud(last))
            --last;
        clipStart = (start + first) % length;
        clipLength = std::max(0, last - first + 1);
        clipChannels = recordChannels;
        playPos = 0;
        writePos = filled = 0; // the new history starts empty
        clipChannelsOut.store(clipChannels, std::memory_order_release);
        clipSamples.store(clipLength, std::memory_order_release);
        servedOut.store(served, std::memory_order_release);
    }

    std::array<std::vector<float>, kMaxChannels> history, clip;
    int length = 1, writePos = 0, filled = 0;
    double rate = 48000.0;
    int clipStart = 0, clipLength = 0, useLength = 0, clipChannels = 1, playPos = 0, recordChannels = 1;
    int served = 0;
    bool playing = false;

    std::atomic<int> captureRequests { 0 }, servedOut { 0 }, clipSamples { 0 }, clipChannelsOut { 1 };
    std::atomic<bool> auditionOn { false };
    std::atomic<float> useSeconds { kDefaultClipSeconds };

    std::vector<std::atomic<float>> preScope = std::vector<std::atomic<float>>(kScopeSize); // on the heap: engines live on the stack in tests
    std::vector<std::atomic<float>> postScope = std::vector<std::atomic<float>>(kScopeSize);
    int scopePos = 0;
    std::atomic<int> scopeWritten { 0 };
};
} // namespace bitvibe
