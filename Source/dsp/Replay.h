#pragma once

// REPLAY: a looper that takes over when the player stops. While they play, only their clean signal comes out. Once
// they have been quiet for the WAIT time, the last LOOK BACK seconds of what they played loop, through the sequencer
// and the effects, until they play again (which cuts it at once) or the LENGTH runs out.
//
// The input is recorded all the time into a ring buffer. When the loop starts, its piece is copied out into a loop
// buffer of its own, so recording can go on underneath it and the next phrase is caught from its very first note. The
// loop's seam is crossfaded with the audio that led into its start, so it wraps without a click.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bitvibe
{
inline constexpr float kReplayMinLookBack = 0.25f, kReplayMaxLookBack = 8.0f, kReplayDefaultLookBack = 2.0f; // seconds
inline constexpr float kReplayMinWait = 0.05f, kReplayMaxWait = 2.0f, kReplayDefaultWait = 0.5f;              // seconds
// LENGTH, in seconds; at the top of its range (kReplayForever) the loop plays until the player plays again
inline constexpr float kReplayMinLength = 1.0f, kReplayForever = 64.0f;
inline constexpr float kReplayMinThresholdDb = -70.0f, kReplayMaxThresholdDb = -20.0f, kReplayDefaultThresholdDb = -45.0f;

struct ReplaySettings
{
    bool on = false;
    float lookBackSeconds = kReplayDefaultLookBack;
    float waitSeconds = kReplayDefaultWait;
    float lengthSeconds = kReplayForever; // kReplayForever or more = until the player plays again
    float dry = 0.0f;                     // how much of the loop is heard clean, 0..1 (crossfaded with the effected loop)
    float thresholdDb = kReplayDefaultThresholdDb;
};

class ReplayLooper
{
public:
    static constexpr int kChannels = 2;

    enum class State
    {
        Off,     // REPLAY is switched off
        Idle,    // listening: nothing played yet, or the last loop has ended
        Live,    // the player is playing (or has just stopped, for less than the WAIT)
        Looping, // the loop is playing
    };

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        const int ringLength = (int) std::ceil((kReplayMaxLookBack + kReplayMaxWait + 0.5f) * fs);
        const int loopCapacity = (int) std::ceil(kReplayMaxLookBack * fs) + maxCrossfade() + 1;
        for (int c = 0; c < kChannels; ++c)
        {
            ring[(size_t) c].assign((size_t) ringLength, 0.0f);
            loop[(size_t) c].assign((size_t) loopCapacity, 0.0f);
        }
        fadeIn = 1.0f - std::exp(-1.0f / (float) (fs * 0.008));
        fadeCut = 1.0f - std::exp(-1.0f / (float) (fs * 0.002));
        fadeEnd = 1.0f - std::exp(-1.0f / (float) (fs * 0.060));
        reset();
    }

    void reset()
    {
        for (auto& r : ring)
            std::fill(r.begin(), r.end(), 0.0f);
        written = 0;
        state = State::Idle;
        gate = 0.0f;
        loopLength = crossfade = 0;
        loopPos = 0;
        played = 0;
        phraseStart = lastLoud = 0;
        ending = false;
        started = cut = false;
    }

    // What happened on the last sample (each true for that sample only).
    bool loopStarted() const { return started; }
    bool loopCut() const { return cut; }

    State getState() const { return state; }
    bool isLooping() const { return state == State::Looping; }
    // How loud the loop is in the output, 0..1: fades in when the loop starts, out when it is cut or ends.
    float getGate() const { return gate; }
    float loopSeconds() const { return (float) ((double) loopLength / fs); }
    float loopPosition() const { return loopLength > 0 ? (float) loopPos / (float) loopLength : 0.0f; }
    // How long since the player was last heard while waiting for the loop to start (0..1 of the WAIT), else 0.
    float waitProgress() const { return state == State::Live && waitSamples > 0 ? std::min(1.0f, (float) (written - lastLoud) / (float) waitSamples) : 0.0f; }

    // REPLAY switched off: the loop stops and the looper forgets it (it listens afresh when switched on again).
    void switchOff()
    {
        if (state == State::Off)
            return;
        state = State::Off;
        gate = 0.0f;
        loopLength = 0;
        ending = started = cut = false;
    }

    // One sample, while REPLAY is on: in = the player's clean input (numChannels of it), level = what the detector
    // hears (the input after its gain, as the pick detector hears it). out receives the loop (silence while none
    // plays), before the gate.
    void process(const float* in, int numChannels, float level, const ReplaySettings& s, float* out)
    {
        started = cut = false;
        if (state == State::Off)
            state = State::Idle;

        waitSamples = (std::int64_t) ((double) std::clamp(s.waitSeconds, kReplayMinWait, kReplayMaxWait) * fs);
        if (!(s.thresholdDb == thresholdDb))
        {
            thresholdDb = s.thresholdDb;
            threshold = std::pow(10.0f, std::clamp(thresholdDb, kReplayMinThresholdDb, kReplayMaxThresholdDb) / 20.0f);
        }

        // playing = the input over the threshold. Between the peaks of a note it dips below, but never for as long as
        // the shortest WAIT, so a note counts as played until it has died away below the threshold.
        const bool loud = std::abs(level) >= threshold;

        // record (always, so the next phrase is caught from its first note)
        const auto ringSize = (std::int64_t) ring[0].size();
        const size_t at = (size_t) (written % ringSize);
        for (int c = 0; c < kChannels; ++c)
            ring[(size_t) c][at] = in[std::min(c, numChannels - 1)];
        ++written;

        switch (state)
        {
            case State::Off:
            case State::Idle:
                if (loud)
                    beginPhrase();
                break;
            case State::Live:
                if (loud)
                    lastLoud = written;
                else if (written - lastLoud >= waitSamples)
                    startLoop(s);
                break;
            case State::Looping:
                if (loud)
                {
                    cut = true;
                    ending = false;
                    beginPhrase(); // playing cuts the loop at once
                }
                else
                {
                    const auto limit = s.lengthSeconds >= kReplayForever ? (std::int64_t) -1
                                                                          : (std::int64_t) ((double) std::max(s.lengthSeconds, kReplayMinLength) * fs);
                    if (!ending && limit >= 0 && played >= limit)
                        ending = true; // LENGTH is up: fade out, then listen again
                }
                break;
        }

        // the gate: a quick fade in, a very quick cut when the player plays, a gentler fade at the end of LENGTH
        const float target = state == State::Looping && !ending ? 1.0f : 0.0f;
        const float coef = target > gate ? fadeIn : (state == State::Looping && ending ? fadeEnd : fadeCut);
        gate += (target - gate) * coef;
        if (std::abs(target - gate) < 1.0e-5f)
            gate = target;
        if (state == State::Looping && ending && gate <= 0.0f)
        {
            state = State::Idle;
            ending = false;
        }

        // the loop's next sample (it keeps playing while it fades)
        if (loopLength > 0 && (state == State::Looping || gate > 0.0f))
        {
            for (int c = 0; c < kChannels; ++c)
                out[c] = loopSample(c);
            if (++loopPos >= loopLength)
                loopPos = 0;
            ++played;
        }
        else
        {
            for (int c = 0; c < kChannels; ++c)
                out[c] = 0.0f;
        }
    }

private:
    int maxCrossfade() const { return std::max(1, (int) (0.020 * fs)); }

    void beginPhrase()
    {
        state = State::Live;
        // a few milliseconds before the note was heard, so its attack is in the phrase
        phraseStart = std::max<std::int64_t>(0, written - 1 - (std::int64_t) (0.010 * fs));
        lastLoud = written;
    }

    void startLoop(const ReplaySettings& s)
    {
        const auto ringSize = (std::int64_t) ring[0].size();
        const auto lookBack = (std::int64_t) ((double) std::clamp(s.lookBackSeconds, kReplayMinLookBack, kReplayMaxLookBack) * fs);
        const std::int64_t end = lastLoud;
        // what the player played since they started this phrase, LOOK BACK at the most
        const std::int64_t start = std::max({ end - lookBack, phraseStart, written - ringSize + (std::int64_t) maxCrossfade() + 1 });
        const auto length = end - start;
        if (length < (std::int64_t) (0.030 * fs)) // a click or a bump, not a phrase
        {
            state = State::Idle;
            return;
        }
        loopLength = (int) length;
        crossfade = std::min(maxCrossfade(), loopLength / 4);
        // the loop buffer: the crossfade's worth of audio leading into the start, then the loop itself
        for (int c = 0; c < kChannels; ++c)
            for (std::int64_t i = 0; i < (std::int64_t) crossfade + length; ++i)
                loop[(size_t) c][(size_t) i] = ring[(size_t) c][(size_t) ((start - crossfade + i + ringSize) % ringSize)];
        loopPos = 0;
        played = 0;
        ending = false;
        state = State::Looping;
        started = true;
    }

    // The loop at loopPos: its last crossfade samples blend into the audio that led into its start, so the wrap back
    // to the start carries straight on.
    float loopSample(int c) const
    {
        const auto& b = loop[(size_t) c];
        const float v = b[(size_t) (crossfade + loopPos)];
        const int intoFade = loopPos - (loopLength - crossfade);
        if (crossfade <= 0 || intoFade < 0)
            return v;
        const float w = 0.5f - 0.5f * std::cos(3.14159265f * ((float) intoFade + 0.5f) / (float) crossfade);
        return v + (b[(size_t) intoFade] - v) * w;
    }

    double fs = 48000.0;
    std::array<std::vector<float>, kChannels> ring, loop;
    std::int64_t written = 0; // samples recorded since the start (the ring's write position, unwrapped)
    std::int64_t phraseStart = 0, lastLoud = 0, waitSamples = 0, played = 0;
    float thresholdDb = 1.0e9f, threshold = 1.0f; // the threshold as a level, worked out when the knob moves
    State state = State::Idle;
    float gate = 0.0f;
    bool ending = false, started = false, cut = false;
    int loopLength = 0, crossfade = 0, loopPos = 0;
    // the gate's fade speeds (per-sample coefficients for the sample rate): in, cut by playing, and at the end of LENGTH
    float fadeIn = 0.01f, fadeCut = 0.1f, fadeEnd = 0.001f;
};
} // namespace bitvibe
