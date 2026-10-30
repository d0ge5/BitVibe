#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
    float tone = 0.5f;        // 0 = dark .. 1 = bright (a low-pass on the reverb, 1.2 kHz to 16 kHz)
    float lowCutHz = 20.0f;   // high-pass on the reverb; 20 Hz or less = off
    float highCutHz = 20000.0f; // low-pass on the reverb; 19.9 kHz or more = off
};

// A basic reverb for one channel with three characters:
//  - Room: eight damped feedback combs into four allpasses (the classic Schroeder/Freeverb layout), short.
//  - Hall: the same with longer delays and more feedback, so a big, slow tail.
//  - Spring: two feedback loops, each with a chain of allpasses in the loop that smear the highs and lows
//    differently (the "drip" of a spring tank), plus a damping filter.
// Changing the type fades the reverb out, clears it and fades back in, so it does not click. The right
// channel uses slightly different delays so a stereo pair does not collapse to mono.
class Reverb
{
public:
    // channelIndex 0 = left, 1 = right.
    void prepare(double sampleRate, int channelIndex)
    {
        fs = sampleRate;
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
        current = ReverbType::Room;
        pending = ReverbType::Room;
        configure(current);
        fade = 1.0f;
        reset();
    }

    void reset()
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
        toneState = 0.0f;
        lowState = { 0.0f, 0.0f };
        highState = { 0.0f, 0.0f };
        fade = 1.0f;
        pending = current;
    }

    float process(float in, const ReverbSettings& s)
    {
        // A change of type: fade out (about 8 ms), switch and clear, fade in.
        if (s.type != pending)
            pending = s.type;
        if (pending != current)
        {
            fade -= fadeStep();
            if (fade <= 0.0f)
            {
                fade = 0.0f;
                current = pending;
                configure(current);
                for (auto& c : combs)
                    c.clear();
                for (auto& a : allpasses)
                    a.clear();
                for (auto& d : springDelay)
                    d.clear();
                for (auto& chain : springAllpass)
                    for (auto& a : chain)
                        a.clear();
            }
        }
        else if (fade < 1.0f)
        {
            fade = std::min(1.0f, fade + fadeStep());
        }

        float sum = current == ReverbType::Spring ? processSpring(in) : processRoom(in);

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

    struct Settings
    {
        double combScale, feedback, damp, inputGain;
    };
    // Input gains keep the three characters at about the same loudness (see the unit test).
    static constexpr Settings kRoom { 0.55, 0.80, 0.30, 0.040 };
    static constexpr Settings kHall { 1.70, 0.90, 0.18, 0.044 };
    static constexpr float kSpringFeedback = 0.80f, kSpringDamp = 0.35f, kSpringInput = 1.25f;

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

    void configure(ReverbType type)
    {
        if (type == ReverbType::Spring)
            return;
        const Settings& s = type == ReverbType::Hall ? kHall : kRoom;
        const double scale = fs / 44100.0;
        for (int i = 0; i < kNumCombs; ++i)
            combs[(size_t) i].setLength(std::max(1, (int) std::lround((kCombs[i] + spread) * scale * s.combScale)));
        roomFeedback = (float) s.feedback;
        roomDamp = (float) s.damp;
        roomInput = (float) s.inputGain;
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
            d.buffer[d.pos] = in * kSpringInput + springLow[l] * kSpringFeedback;
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
    double fs = 48000.0;
    int spread = 0;
    float roomFeedback = 0.8f, roomDamp = 0.3f, roomInput = 0.04f;
    ReverbType current = ReverbType::Room, pending = ReverbType::Room;
    float fade = 1.0f;
    float toneState = 0.0f;
    std::array<float, 2> lowState { 0.0f, 0.0f }, highState { 0.0f, 0.0f };
};
} // namespace bitvibe
