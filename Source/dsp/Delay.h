#pragma once

// A tempo-synced echo for the sequenced sound: the stutter's idea, but on what the sequence has become. The time is a
// note length of the tempo; REPEATS says how many echoes follow each sound (or that they go on forever), DECAY how
// much quieter each one is than the one before, and a low cut and a high cut shape the echoes only.
//
// With a set number of repeats the echoes are taps at 1, 2, ... n times the delay time, so the count is exact. Forever
// is a feedback loop instead. Header-only and free of JUCE so the tests can drive it.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bitvibe
{
struct DelayDivision
{
    const char* name;
    double beats; // in quarter notes
};

// The delay times, shortest first: straight, triplet (T) and dotted (D) note lengths of the tempo.
inline constexpr std::array<DelayDivision, 12> kDelayDivisions { {
    { "1/32", 0.125 }, { "1/16 T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16 D", 0.375 }, { "1/8 T", 1.0 / 3.0 }, { "1/8", 0.5 },
    { "1/8 D", 0.75 }, { "1/4 T", 2.0 / 3.0 }, { "1/4", 1.0 }, { "1/4 D", 1.5 }, { "1/2", 2.0 }, { "1 BAR", 4.0 },
} };
inline constexpr int kDefaultDelayDivision = 5; // 1/8
inline constexpr int kDelayMaxRepeats = 16;
inline constexpr int kDelayForever = kDelayMaxRepeats + 1; // the REPEATS setting past 16

struct DelaySettings
{
    double timeSamples = 12000.0; // one echo's delay
    float decay = 0.5f;           // 0..1: each echo is decayGain() times the one before
    int repeats = 4;              // 1..16, or kDelayForever
    float lowCutHz = 20.0f;       // high-pass on the echoes (20 Hz = off)
    float highCutHz = 20000.0f;   // low-pass on the echoes (20 kHz = off)
    float wobble = 0.0f;          // 0..1: tape wobble (0 = a clean digital delay)
};

// WOBBLE makes the delay behave like a worn tape echo: the delay time drifts slowly (wow, about 0.6 Hz, wandering) and
// shivers quickly (flutter, about 7 Hz), so the echoes waver in pitch, each later echo a little more; the echoes also get
// darker and saturate softly the more it is turned up. At 0 the delay is exactly as clean as without it.
class Delay
{
public:
    static constexpr double kWowHz = 0.6, kFlutterHz = 7.0;
    // the pitch swing at full wobble: wow about +/-0.6%, flutter about +/-0.15%
    static constexpr double kWowSeconds = 0.006 / (6.283185307179586 * kWowHz);
    static constexpr double kFlutterSeconds = 0.0015 / (6.283185307179586 * kFlutterHz);

    static constexpr double kMaxTimeSeconds = 4.0;  // one echo: up to a bar at 60 BPM
    static constexpr double kBufferSeconds = 16.0;  // all the taps together (echoes that would be later are left out)

    // How much quieter each echo is than the one before.
    static float decayGain(float decay) { return 0.95f * std::clamp(decay, 0.0f, 1.0f); }

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        buffer.assign((size_t) std::ceil(fs * kBufferSeconds) + 4, 0.0f);
        timeCoef = 1.0f - std::exp(-1.0f / (float) (fs * 0.05)); // tempo and time changes glide over about 50 ms
        reset();
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        write = 0;
        primed = false;
        lowState = { 0.0f, 0.0f };
        highState = { 0.0f, 0.0f };
        wowPhase = flutterPhase = 0.0;
        drift = driftTarget = 0.0f;
        driftCount = 0;
        wobbleNow = 0.0f;
        rng = 0x2545F491u;
    }

    // How far the wobble has moved the delay time now, in samples (for the tests).
    double getWobbleSamples() const { return lastWobble; }

    // One input sample in, the echoes out (the input itself is not in the output).
    float process(float in, const DelaySettings& s)
    {
        if (buffer.empty())
            return 0.0f;
        const double target = std::clamp(s.timeSamples, 1.0, kMaxTimeSeconds * fs);
        if (!primed)
        {
            time = target;
            primed = true;
        }
        time += (target - time) * (double) timeCoef;

        // the tape's wobble: wow (slow, its speed and depth wandering) plus flutter (fast)
        wobbleNow += 0.0005f * (std::clamp(s.wobble, 0.0f, 1.0f) - wobbleNow);
        if (wobbleNow < 1.0e-5f && s.wobble <= 0.0f)
            wobbleNow = 0.0f;
        if (--driftCount <= 0) // a new place for the wander to head for, every quarter of a second
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            driftTarget = (float) (rng >> 8) * (2.0f / 16777216.0f) - 1.0f;
            driftCount = (int) (0.25 * fs);
        }
        drift += (driftTarget - drift) * (float) (4.0 / fs);
        wowPhase += kWowHz * (1.0 + 0.35 * (double) drift) / fs;
        wowPhase -= std::floor(wowPhase);
        flutterPhase += kFlutterHz / fs;
        flutterPhase -= std::floor(flutterPhase);
        auto wobbleAt = [&](double offset) // the delay change for an echo, its wow a little out of step with the others
        {
            if (wobbleNow <= 0.0f)
                return 0.0;
            const double wow = std::sin(6.283185307179586 * (wowPhase + offset)) * (1.0 + 0.3 * (double) drift);
            const double flutter = std::sin(6.283185307179586 * (flutterPhase + 2.3 * offset));
            return (double) wobbleNow * fs * (kWowSeconds * wow + kFlutterSeconds * flutter);
        };
        lastWobble = wobbleAt(0.0);

        const float g = decayGain(s.decay);
        float out = 0.0f;
        if (s.repeats >= kDelayForever)
        {
            out = read(std::max(1.0, time + lastWobble)); // round the loop, the wobble builds up echo after echo
            buffer[write] = in + out * g; // each trip round the loop is one more echo
        }
        else
        {
            buffer[write] = in;
            const double longest = (double) buffer.size() - 2.0;
            float gain = 1.0f;
            for (int k = 1; k <= std::clamp(s.repeats, 1, kDelayMaxRepeats); ++k)
            {
                const double d = time * (double) k;
                if (d > longest)
                    break;
                // a later echo has been round the tape more often: it wobbles more
                out += read(std::clamp(d + wobbleAt(0.17 * (double) k) * std::sqrt((double) k), 1.0, longest)) * gain;
                gain *= g;
            }
        }
        if (++write >= buffer.size())
            write = 0;

        // worn tape: softer, darker echoes (the HIGH CUT closes towards 6 kHz at full wobble)
        float highCut = s.highCutHz;
        if (wobbleNow > 0.0f)
        {
            const float drive = 1.0f + 2.0f * wobbleNow;
            out = std::tanh(out * drive) / drive;
            highCut = std::min(highCut, 20000.0f * std::pow(0.3f, wobbleNow));
        }
        if (highCut < 19900.0f) // two poles each: 12 dB per octave
        {
            const float c = onePole(std::clamp(highCut, 200.0f, 19900.0f));
            highState[0] += c * (out - highState[0]);
            highState[1] += c * (highState[0] - highState[1]);
            out = highState[1];
        }
        if (s.lowCutHz > 20.5f) // two one-pole high-passes (each the signal minus its low-pass): 12 dB per octave
        {
            const float c = onePole(std::clamp(s.lowCutHz, 20.0f, 2000.0f));
            lowState[0] += c * (out - lowState[0]);
            out -= lowState[0];
            lowState[1] += c * (out - lowState[1]);
            out -= lowState[1];
        }
        return out;
    }

private:
    // The sample `delay` samples ago (fractional, linear interpolation).
    float read(double delay) const
    {
        double pos = (double) write - delay;
        const double size = (double) buffer.size();
        while (pos < 0.0)
            pos += size;
        const size_t i0 = (size_t) pos;
        const size_t i1 = i0 + 1 < buffer.size() ? i0 + 1 : 0;
        const float frac = (float) (pos - (double) i0);
        return buffer[i0] + (buffer[i1] - buffer[i0]) * frac;
    }

    float onePole(float hz) const { return 1.0f - std::exp(-6.2831853f * std::min(hz, 0.45f * (float) fs) / (float) fs); }

    std::vector<float> buffer;
    size_t write = 0;
    double fs = 48000.0, time = 0.0;
    float timeCoef = 0.001f;
    bool primed = false;
    std::array<float, 2> lowState {}, highState {};
    double wowPhase = 0.0, flutterPhase = 0.0, lastWobble = 0.0;
    float drift = 0.0f, driftTarget = 0.0f, wobbleNow = 0.0f;
    int driftCount = 0;
    uint32_t rng = 0x2545F491u;
};
} // namespace bitvibe
