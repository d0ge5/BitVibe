#pragma once

// REPLAY: a looper that takes over when the player stops. While they play, only their clean signal comes out. Once
// they have been quiet for the WAIT time, the last LOOK BACK seconds of what they played loop, through the sequencer
// and the effects, until they play again (which cuts it at once) or the LENGTH runs out.
//
// The input is recorded all the time into a ring buffer. When the loop starts, its piece is copied out into a loop
// buffer of its own, so recording can go on underneath it and the next phrase is caught from its very first note. The
// loop's seam is crossfaded with the audio that led into its start, so it wraps without a click.
//
// How the loop plays back: at its SPEED (tape style: half speed is an octave down), in its DIRECTION (forward, reverse
// or back and forth), STRETCHed (slower without a change of pitch: short overlapping grains read from a playhead that
// crawls through the loop, each new grain lined up with the one before it so they add up rather than cancel), SWELLing
// in over up to a few seconds, and with WEAR: every pass runs the loop through a
// gentle low-pass and takes a little off its level, so it darkens and dies away like a tape loop wearing out.

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
inline constexpr float kReplayMaxStretch = 4.0f, kReplayMaxSwell = 4.0f;
// SPEED: the loop's playback rate, by its position (normal, half = an octave down, quarter = two octaves down)
inline constexpr std::array<float, 3> kReplaySpeeds { 1.0f, 0.5f, 0.25f };

enum class ReplayDirection
{
    Forward = 0,
    Reverse,
    BackAndForth,
};
inline constexpr int kNumReplayDirections = 3;

struct ReplaySettings
{
    bool on = false;
    float lookBackSeconds = kReplayDefaultLookBack;
    float waitSeconds = kReplayDefaultWait;
    float lengthSeconds = kReplayForever; // kReplayForever or more = until the player plays again
    float dry = 0.0f;                     // how much of the loop is heard clean, 0..1 (crossfaded with the effected loop)
    float thresholdDb = kReplayDefaultThresholdDb;
    float speed = 1.0f;                   // playback rate (pitch moves with it), one of kReplaySpeeds
    ReplayDirection direction = ReplayDirection::Forward;
    float stretch = 1.0f;                 // 1 = as played .. kReplayMaxStretch = that many times slower, at the same pitch
    float swellSeconds = 0.0f;            // how long the loop takes to fade in (0 = at once)
    float wear = 0.0f;                    // 0..1: how much darker and quieter each pass of the loop gets
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
        grainLength = std::max(16, (int) (0.080 * fs));
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
        head = 0.0;
        dir = 1;
        played = 0;
        phraseStart = lastLoud = 0;
        ending = false;
        started = cut = false;
        wearIndex = -1;
        for (auto& w : wearState)
            w = {};
    }

    // What happened on the last sample (each true for that sample only).
    bool loopStarted() const { return started; }
    bool loopCut() const { return cut; }

    State getState() const { return state; }
    bool isLooping() const { return state == State::Looping; }
    // How loud the loop is in the output, 0..1: fades in when the loop starts, out when it is cut or ends.
    float getGate() const { return gate; }
    float loopSeconds() const { return (float) ((double) loopLength / fs); }
    float loopPosition() const { return loopLength > 0 ? (float) (head / (double) loopLength) : 0.0f; }
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
        if (!(s.swellSeconds == swellSeconds))
        {
            swellSeconds = s.swellSeconds;
            // an exponential rise that is nearly there after the swell (or in 8 ms with no swell)
            fadeIn = 1.0f - std::exp(-1.0f / (float) (fs * std::max(0.008, (double) std::clamp(swellSeconds, 0.0f, kReplayMaxSwell) / 3.0)));
        }
        if (!(s.wear == wear))
        {
            wear = std::clamp(s.wear, 0.0f, 1.0f);
            // each pass: a one-pole low-pass at 20 kHz (nothing) down to 1.6 kHz, and up to 1.1 dB less level
            const double fc = 20000.0 * std::exp(-2.5 * (double) wear);
            wearCoef = (float) (1.0 - std::exp(-2.0 * 3.14159265358979 * fc / fs));
            wearGain = 1.0f - 0.12f * wear;
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

        // the gate: the SWELL in, a very quick cut when the player plays, a gentler fade at the end of LENGTH
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
            playSample(s, out);
        else
            for (int c = 0; c < kChannels; ++c)
                out[c] = 0.0f;
    }

private:
    static constexpr int kGrains = 4; // overlapping grains for the STRETCH; their windows add up to a constant

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
        // the playhead starts at the end for a reversed loop. The grains start staggered through their windows, each
        // placed so that all of them read the playhead's sample right now (so the stretch begins as the plain loop does).
        dir = s.direction == ReplayDirection::Reverse ? -1 : 1;
        head = dir < 0 ? (double) (loopLength - 1) : 0.0;
        const double speed = (double) std::clamp(s.speed, 0.125f, 2.0f);
        for (int g = 0; g < kGrains; ++g)
        {
            const double pos = (double) g * (double) grainLength / (double) kGrains;
            grains[(size_t) g] = { head - pos * speed * (double) dir, pos };
        }
        lastStarted = 0;
        wearIndex = -1;
        for (auto& w : wearState)
            w = {};
        played = 0;
        ending = false;
        state = State::Looping;
        started = true;
    }

    // The loop at an integer position: its last crossfade samples blend into the audio that led into its start, so the
    // wrap from the end back to the start (either way round) carries straight on.
    float loopSample(int c, int pos) const
    {
        const auto& b = loop[(size_t) c];
        const float v = b[(size_t) (crossfade + pos)];
        const int intoFade = pos - (loopLength - crossfade);
        if (crossfade <= 0 || intoFade < 0)
            return v;
        const float w = 0.5f - 0.5f * std::cos(3.14159265f * ((float) intoFade + 0.5f) / (float) crossfade);
        return v + (b[(size_t) intoFade] - v) * w;
    }

    // The loop at a fractional position (wrapped into the loop), between its samples.
    float readAt(int c, double pos) const
    {
        pos = std::fmod(pos, (double) loopLength);
        if (pos < 0.0)
            pos += (double) loopLength;
        const int i = std::min((int) pos, loopLength - 1);
        const float f = (float) (pos - (double) i);
        const int j = i + 1 >= loopLength ? 0 : i + 1;
        const float a = loopSample(c, i);
        return a + (loopSample(c, j) - a) * f;
    }

    // Moves a position on by step in the loop's direction, wrapping (or, back and forth, turning at the ends).
    void advance(double& pos, int& direction, double step, ReplayDirection mode) const
    {
        pos += step * (double) direction;
        const double last = (double) (loopLength - 1);
        if (mode == ReplayDirection::BackAndForth)
        {
            if (pos > last)
            {
                pos = std::max(0.0, 2.0 * last - pos);
                direction = -1;
            }
            else if (pos < 0.0)
            {
                pos = std::min(last, -pos);
                direction = 1;
            }
        }
        else if (pos >= (double) loopLength)
            pos -= (double) loopLength;
        else if (pos < 0.0)
            pos += (double) loopLength;
    }

    void playSample(const ReplaySettings& s, float* out)
    {
        const double speed = (double) std::clamp(s.speed, 0.125f, 2.0f);
        const double stretch = (double) std::clamp(s.stretch, 1.0f, kReplayMaxStretch);
        if (stretch < 1.001)
        {
            // tape style: the playhead itself is read, at its speed
            for (int c = 0; c < kChannels; ++c)
                out[c] = readAt(c, head);
        }
        else
        {
            // STRETCH: grains read on from where the playhead was when they started, at the SPEED; the playhead itself
            // moves stretch times slower, so the loop takes longer without changing pitch. Four Hann windows a quarter
            // of a grain apart add up to two.
            for (int c = 0; c < kChannels; ++c)
                out[c] = 0.0f;
            for (int i = 0; i < kGrains; ++i)
            {
                auto& g = grains[(size_t) i];
                if (g.pos >= (double) grainLength)
                {
                    g.pos -= (double) grainLength;
                    g.start = alignedStart(head, grains[(size_t) lastStarted], speed);
                    lastStarted = i;
                }
                const float w = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * (float) (g.pos / (double) grainLength));
                const double at = g.start + g.pos * speed * (double) dir;
                for (int c = 0; c < kChannels; ++c)
                    out[c] += 0.5f * w * readAt(c, at);
                g.pos += 1.0;
            }
        }
        // WEAR: as the playhead crosses a sample of the loop, that sample (and the pre-roll it blends with near the
        // seam) is low-passed and turned down a little, once per pass
        if (wear > 0.0f)
        {
            const int idx = std::clamp((int) head, 0, loopLength - 1);
            if (idx != wearIndex)
            {
                wearIndex = idx;
                const int intoFade = idx - (loopLength - crossfade);
                for (int c = 0; c < kChannels; ++c)
                {
                    auto& w = wearState[(size_t) c];
                    auto& b = loop[(size_t) c];
                    float& v = b[(size_t) (crossfade + idx)];
                    w.loop += wearCoef * (v - w.loop);
                    v = w.loop * wearGain;
                    if (intoFade >= 0)
                    {
                        float& pre = b[(size_t) intoFade];
                        w.pre += wearCoef * (pre - w.pre);
                        pre = w.pre * wearGain;
                    }
                }
            }
        }
        advance(head, dir, speed / stretch, s.direction);
        ++played;
    }

    struct Grain
    {
        double start = 0.0, pos = 0.0; // where in the loop it started, and how far into itself it is
    };

    // The loop at an integer position wrapped into the loop (no seam blend: this is for the grain search).
    float wrapped(int c, long long pos) const
    {
        pos %= loopLength;
        if (pos < 0)
            pos += loopLength;
        return loop[(size_t) c][(size_t) (crossfade + (int) pos)];
    }

    // Where a new grain should start so that it carries on in step with the grain before it: within a few
    // milliseconds of the playhead, the offset whose samples best match what that grain is about to read (the left
    // channel decides; both channels take the same offset). Coarse steps first, then the neighbourhood of the best.
    double alignedStart(double at, const Grain& previous, double speed) const
    {
        const int range = std::min(loopLength / 4, std::max(1, (int) (0.005 * fs))), length = std::min(loopLength / 2, 256);
        const double step = speed * (double) dir;
        const auto refBase = (long long) std::llround(previous.start + previous.pos * step);
        const auto candBase = (long long) std::llround(at);
        auto score = [&](int d)
        {
            double dot = 0.0, energy = 0.0;
            for (int n = 0; n < length; ++n)
            {
                const auto k = (long long) std::llround((double) n * step);
                const float a = wrapped(0, refBase + k), b = wrapped(0, candBase + d + k);
                dot += (double) a * b;
                energy += (double) b * b;
            }
            return energy > 1.0e-12 ? dot / std::sqrt(energy) : 0.0;
        };
        int best = 0;
        double bestScore = -1.0e30;
        for (int d = -range; d <= range; d += 8)
            if (const double sc = score(d); sc > bestScore)
            {
                bestScore = sc;
                best = d;
            }
        const int coarse = best;
        for (int d = std::max(-range, coarse - 7); d <= std::min(range, coarse + 7); ++d)
            if (d != coarse)
                if (const double sc = score(d); sc > bestScore)
                {
                    bestScore = sc;
                    best = d;
                }
        return at + (double) best;
    }
    struct WearFilter
    {
        float loop = 0.0f, pre = 0.0f;
    };

    double fs = 48000.0;
    std::array<std::vector<float>, kChannels> ring, loop;
    std::int64_t written = 0; // samples recorded since the start (the ring's write position, unwrapped)
    std::int64_t phraseStart = 0, lastLoud = 0, waitSamples = 0, played = 0;
    float thresholdDb = 1.0e9f, threshold = 1.0f; // the threshold as a level, worked out when the knob moves
    float swellSeconds = -1.0f, wear = -1.0f;     // the SWELL and WEAR as last worked out
    float wearCoef = 1.0f, wearGain = 1.0f;
    State state = State::Idle;
    float gate = 0.0f;
    bool ending = false, started = false, cut = false;
    int loopLength = 0, crossfade = 0;
    double head = 0.0; // the playhead, in loop samples
    int dir = 1;       // which way it is going
    std::array<Grain, kGrains> grains {};
    int grainLength = 3840;
    int lastStarted = 0; // the grain that began most recently: the next one lines up with it
    int wearIndex = -1; // the loop sample the WEAR last touched
    std::array<WearFilter, kChannels> wearState {};
    // the gate's fade speeds (per-sample coefficients for the sample rate): in (the SWELL), cut by playing, and at the
    // end of LENGTH
    float fadeIn = 0.01f, fadeCut = 0.1f, fadeEnd = 0.001f;
};
} // namespace bitvibe
