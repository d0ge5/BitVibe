#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bitvibe
{
enum class ReverbType
{
    Room = 0,
    Hall,
    Spring
};
inline constexpr int kNumReverbTypes = 3;

inline const char* reverbTypeName(ReverbType t)
{
    static constexpr const char* names[kNumReverbTypes] = { "Room", "Hall", "Spring" };
    return names[std::clamp((int) t, 0, kNumReverbTypes - 1)];
}

// What the reverb is told every sample (all smoothed by the caller).
struct ReverbSettings
{
    ReverbType type = ReverbType::Room;
    float tone = 0.5f;          // 0 = dark .. 1 = bright (a low-pass on the reverb, 1.2 kHz to 16 kHz)
    float decay = 0.5f;         // 0 = a quarter of the normal tail, 0.5 = normal, 1 = four times as long
    bool reverse = false;       // the tail is backwards: it swells up after the sound instead of dying away
    float lowCutHz = 20.0f;     // high-pass on the reverb; 20 Hz or less = off
    float highCutHz = 20000.0f; // low-pass on the reverb; 19.9 kHz or more = off
};

// A basic reverb for one channel with three characters:
//  - Room: eight damped feedback combs into four allpasses (the classic Schroeder/Freeverb layout), short.
//  - Hall: the same with longer delays and more feedback, so a big, slow tail.
//  - Spring: two feedback loops, each with a chain of allpasses in the loop that smear the highs and lows
//    differently (the "drip" of a spring tank), plus a damping filter.
// DECAY stretches or shrinks the tail of any of them (a quarter to four times as long as normal).
//
// REVERSE turns the tail around: the sound swells up after it has been played and stops at the end, as if a tape of
// the reverb had been played backwards. It is built the way a reversed reverb behaves statistically: a tapped delay
// line whose echoes get louder and sparser towards the end (a reversed room's echoes are dense and quiet at first and
// sparse and loud at the end), smoothed by the same allpass diffusers. Room, Hall and Spring differ in the length of
// the swell (and how dense the echoes are); DECAY sets how long it takes.
//
// Changing the type, REVERSE or (in reverse) DECAY fades the reverb out, clears it and fades back in, so it does
// not click. The right channel uses slightly different delays so a stereo pair does not collapse to mono.
class Reverb
{
public:
    // channelIndex 0 = left, 1 = right.
    void prepare(double sampleRate, int channelIndex)
    {
        fs = sampleRate;
        channel = channelIndex;
        spread = channelIndex == 0 ? 0 : 23;
        const double scale = fs / 44100.0;
        for (int i = 0; i < kNumCombs; ++i)
            combs[(size_t) i].allocate(std::max(1, (int) std::lround((kCombs[i] + spread) * scale * kMaxCombScale)));
        for (int i = 0; i < kNumAllpasses; ++i)
            allpasses[(size_t) i].allocate(std::max(1, (int) std::lround((kAllpasses[i] + spread) * scale)));
        for (int l = 0; l < 2; ++l)
        {
            const double ms = (l == 0 ? 29.0 : 37.0) + (channelIndex == 0 ? 0.0 : 1.7);
            springDelay[(size_t) l].allocate(std::max(1, (int) std::lround(ms * 0.001 * fs)));
            for (int a = 0; a < kSpringAllpasses; ++a)
                springAllpass[(size_t) l][(size_t) a].allocate(std::max(1, (int) std::lround((kSpring[a] + (l == 1 ? 17 : 0)) * scale)));
        }
        revLine.assign((size_t) std::lround(kMaxReverseSeconds * fs) + 2, 0.0f);
        current = pending = ReverbType::Room;
        currentReverse = pendingReverse = false;
        currentBucket = pendingBucket = -1;
        configure();
        fade = 1.0f;
        reset();
    }

    void reset()
    {
        clearState();
        toneState = 0.0f;
        lowState = { 0.0f, 0.0f };
        highState = { 0.0f, 0.0f };
        fade = 1.0f;
        pending = current;
        pendingReverse = currentReverse;
        pendingBucket = currentBucket;
    }

    float process(float in, const ReverbSettings& s)
    {
        // A change of type, direction or (reversed) length: fade out (about 8 ms), switch and clear, fade in.
        pending = s.type;
        pendingReverse = s.reverse;
        pendingBucket = s.reverse ? (int) std::lround(std::clamp(s.decay, 0.0f, 1.0f) * 20.0f) : -1;
        if (pending != current || pendingReverse != currentReverse || pendingBucket != currentBucket)
        {
            fade -= fadeStep();
            if (fade <= 0.0f)
            {
                fade = 0.0f;
                current = pending;
                currentReverse = pendingReverse;
                currentBucket = pendingBucket;
                configure();
                clearState();
            }
        }
        else if (fade < 1.0f)
        {
            fade = std::min(1.0f, fade + fadeStep());
        }

        float sum;
        if (currentReverse)
        {
            sum = processReverse(in);
        }
        else
        {
            applyDecay(s.decay);
            sum = current == ReverbType::Spring ? processSpring(in) : processRoom(in);
        }

        const float hz = 1200.0f * std::pow(16000.0f / 1200.0f, std::clamp(s.tone, 0.0f, 1.0f));
        toneState += onePole(hz) * (sum - toneState);
        sum = toneState;

        if (s.highCutHz < 19900.0f) // two poles: 12 dB per octave
        {
            const float c = onePole(std::clamp(s.highCutHz, 200.0f, 19900.0f));
            highState[0] += c * (sum - highState[0]);
            highState[1] += c * (highState[0] - highState[1]);
            sum = highState[1];
        }
        if (s.lowCutHz > 20.5f) // high-pass = input minus a two-pole low-pass
        {
            const float c = onePole(std::clamp(s.lowCutHz, 20.0f, 2000.0f));
            lowState[0] += c * (sum - lowState[0]);
            lowState[1] += c * (lowState[0] - lowState[1]);
            sum -= lowState[1];
        }
        return sum * fade;
    }

private:
    static constexpr int kNumCombs = 8, kNumAllpasses = 4, kSpringAllpasses = 6;
    static constexpr int kCombs[kNumCombs] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static constexpr int kAllpasses[kNumAllpasses] { 556, 441, 341, 225 };
    static constexpr int kSpring[kSpringAllpasses] { 113, 167, 211, 263, 317, 373 };
    static constexpr double kMaxCombScale = 1.7;
    static constexpr float kAllpassGain = 0.5f, kSpringAllpassGain = 0.62f;
    static constexpr double kMaxReverseSeconds = 9.0;

    struct Settings
    {
        double combScale, feedback, damp, inputGain;
    };
    // Input gains keep the three characters at about the same loudness (see the unit test).
    static constexpr Settings kRoom { 0.55, 0.80, 0.30, 0.040 };
    static constexpr Settings kHall { 1.70, 0.90, 0.18, 0.044 };
    static constexpr float kSpringFeedback = 0.80f, kSpringDamp = 0.35f, kSpringInput = 1.25f;
    // How long the swell of a reversed reverb takes at DECAY = 0.5, per type, in seconds.
    static constexpr double kReverseSeconds[kNumReverbTypes] { 0.7, 2.2, 1.2 };
    static constexpr int kReverseTaps[kNumReverbTypes] { 160, 224, 192 };

    struct Delay
    {
        std::vector<float> buffer;
        size_t length = 1, pos = 0;
        void allocate(int n) { buffer.assign((size_t) n, 0.0f); length = (size_t) n; pos = 0; }
        void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); pos = 0; }
        void setLength(int n) { length = (size_t) std::clamp(n, 1, (int) buffer.size()); pos = 0; }
        void advance() { if (++pos >= length) pos = 0; }
    };

    struct Comb : Delay
    {
        float lowpass = 0.0f;
        void clear() { Delay::clear(); lowpass = 0.0f; }
        float process(float in, float feedback, float damp)
        {
            const float out = buffer[pos];
            lowpass = out * (1.0f - damp) + lowpass * damp;
            buffer[pos] = in + lowpass * feedback;
            advance();
            return out;
        }
    };

    struct Allpass : Delay
    {
        float process(float in, float gain)
        {
            const float delayed = buffer[pos];
            const float out = delayed - in;
            buffer[pos] = in + delayed * gain;
            advance();
            return out;
        }

        // A true allpass (gain 1 at every frequency), needed inside the spring's feedback loop.
        float processUnity(float in, float gain)
        {
            const float delayed = buffer[pos];
            const float v = in + gain * delayed;
            buffer[pos] = v;
            advance();
            return delayed - gain * v;
        }
    };

    struct Tap
    {
        size_t delay;
        float gain;
    };

    // The decay knob as a stretch factor: 0.25x .. 4x, 1x in the middle.
    static float decayFactor(float decay) { return std::pow(2.0f, (std::clamp(decay, 0.0f, 1.0f) - 0.5f) * 4.0f); }

    void clearState()
    {
        for (auto& c : combs)
            c.clear();
        for (auto& a : allpasses)
            a.clear();
        for (auto& d : springDelay)
            d.clear();
        for (auto& chain : springAllpass)
            for (auto& a : chain)
                a.clear();
        springLow = { 0.0f, 0.0f };
        std::fill(revLine.begin(), revLine.end(), 0.0f);
        revPos = 0;
    }

    void configure()
    {
        if (currentReverse)
        {
            buildTaps();
            return;
        }
        if (current == ReverbType::Spring)
        {
            baseFeedback = kSpringFeedback;
            decayApplied = -1.0f;
            return;
        }
        const Settings& s = current == ReverbType::Hall ? kHall : kRoom;
        const double scale = fs / 44100.0;
        for (int i = 0; i < kNumCombs; ++i)
            combs[(size_t) i].setLength(std::max(1, (int) std::lround((kCombs[i] + spread) * scale * s.combScale)));
        baseFeedback = (float) s.feedback;
        roomDamp = (float) s.damp;
        roomInput = (float) s.inputGain;
        decayApplied = -1.0f; // the feedback is worked out again for this type
    }

    // The feedback that gives a tail 'factor' times as long: the loop gain g falls by 10^(-3 d / T), so stretching T
    // by a factor means raising g to the power 1 / factor. Done only when the knob has moved.
    void applyDecay(float decay)
    {
        if (std::abs(decay - decayApplied) < 0.002f)
            return;
        decayApplied = decay;
        const float g = std::min(0.985f, std::pow(baseFeedback, 1.0f / decayFactor(decay)));
        roomFeedback = springFeedback = g;
    }

    // Builds the echoes of the reversed reverb. A forward reverb has echoes whose density grows with the square of
    // the time since the sound and whose level falls 60 dB over its length; turned around, the echoes at the start of
    // the swell are dense and 60 dB down, and the ones at the end are sparse and loud. Positions are stratified
    // random (the same for every run, different for the two channels), levels are normalised so that noise passes at
    // about the loudness of the forward reverb.
    void buildTaps()
    {
        const double seconds = std::clamp(kReverseSeconds[(int) current] * (double) decayFactor((float) currentBucket / 20.0f), 0.15, kMaxReverseSeconds);
        const int n = kReverseTaps[(int) current];
        const size_t length = (size_t) std::lround(seconds * fs);
        std::uint32_t state = 0x9E3779B9u * (std::uint32_t) (1 + channel * 7 + (int) current * 13);
        auto random01 = [&state]
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) (state >> 8) * (1.0f / 16777216.0f);
        };
        taps.clear();
        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float u = ((float) i + random01()) / (float) n;           // 0..1, one per slice
            const float age = std::cbrt(std::max(u, 1.0e-4f));              // forward-time position: density grows as age squared
            const size_t delay = std::clamp<size_t>((size_t) std::lround((1.0 - (double) age) * (double) length) + 1, 1, revLine.size() - 1);
            const float level = std::exp(-6.9078f * age);                   // 0 dB at the start of the forward tail, -60 dB at its end
            const float gain = random01() < 0.5f ? level : -level;
            taps.push_back({ delay, gain });
            energy += (double) level * level;
        }
        const float normal = energy > 0.0 ? (float) (1.0 / std::sqrt(energy)) : 1.0f;
        for (auto& t : taps)
            t.gain *= normal * 0.9f;
    }

    float processReverse(float in)
    {
        revLine[revPos] = in;
        float sum = 0.0f;
        for (const auto& t : taps)
        {
            const size_t index = revPos >= t.delay ? revPos - t.delay : revPos + revLine.size() - t.delay;
            sum += revLine[index] * t.gain;
        }
        if (++revPos >= revLine.size())
            revPos = 0;
        for (auto& a : allpasses) // the diffusers blur the echoes into a swell (true allpasses: they do not change the level)
            sum = a.processUnity(sum, kAllpassGain);
        return sum;
    }

    float processRoom(float in)
    {
        const float input = in * roomInput;
        float sum = 0.0f;
        for (auto& c : combs)
            sum += c.process(input, roomFeedback, roomDamp);
        for (auto& a : allpasses)
            sum = a.process(sum, kAllpassGain);
        return sum;
    }

    float processSpring(float in)
    {
        float sum = 0.0f;
        for (size_t l = 0; l < 2; ++l)
        {
            auto& d = springDelay[l];
            const float out = d.buffer[d.pos];
            float loop = out;
            for (auto& a : springAllpass[l])
                loop = a.processUnity(loop, kSpringAllpassGain); // the chain smears the loop: the drip of a spring
            springLow[l] = loop * (1.0f - kSpringDamp) + springLow[l] * kSpringDamp;
            d.buffer[d.pos] = in * kSpringInput + springLow[l] * springFeedback;
            d.advance();
            sum += out;
        }
        return sum * 0.5f;
    }

    float onePole(float hz) const { return 1.0f - std::exp(-6.2831853f * std::min(hz, 0.45f * (float) fs) / (float) fs); }
    float fadeStep() const { return 1.0f / std::max(1.0f, 0.008f * (float) fs); }

    std::array<Comb, kNumCombs> combs;
    std::array<Allpass, kNumAllpasses> allpasses;
    std::array<Delay, 2> springDelay;
    std::array<std::array<Allpass, kSpringAllpasses>, 2> springAllpass;
    std::array<float, 2> springLow { 0.0f, 0.0f };
    std::vector<float> revLine;
    std::vector<Tap> taps;
    size_t revPos = 0;
    double fs = 48000.0;
    int spread = 0, channel = 0;
    float baseFeedback = 0.8f, roomFeedback = 0.8f, springFeedback = 0.8f, roomDamp = 0.3f, roomInput = 0.04f, decayApplied = -1.0f;
    ReverbType current = ReverbType::Room, pending = ReverbType::Room;
    bool currentReverse = false, pendingReverse = false;
    int currentBucket = -1, pendingBucket = -1;
    float fade = 1.0f;
    float toneState = 0.0f;
    std::array<float, 2> lowState { 0.0f, 0.0f }, highState { 0.0f, 0.0f };
};
} // namespace bitvibe
