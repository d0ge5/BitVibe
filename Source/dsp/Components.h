#pragma once

// DSP building blocks for BitVibe. Header-only, no JUCE dependency.
// Nothing here allocates after prepare(), so it is safe to call from the audio thread.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bitvibe
{
inline constexpr float kPi = 3.14159265358979323846f;

// Linear blend that is exact at both ends. (a + (b - a) * t is off by a rounding error even at
// t = 1, which would add noise to a signal that should pass through untouched.)
inline float lerpExact(float a, float b, float t)
{
    return t <= 0.0f ? a : (t >= 1.0f ? b : a + (b - a) * t);
}

// One-pole smoother. Snaps to the target once it is close enough, which avoids denormals and
// lets "neutral" settings reproduce the input bit-exactly.
struct Smoother
{
    float value = 0.0f;
    float coeff = 1.0f;

    void setTime(double sampleRate, float milliseconds)
    {
        coeff = 1.0f - std::exp(-1.0f / (float) (sampleRate * 0.001 * milliseconds));
    }

    void snap(float v) { value = v; }

    float next(float target)
    {
        value += coeff * (target - value);
        if (std::abs(target - value) < 1.0e-5f)
            value = target;
        return value;
    }
};

// Pick-attack detector: a fast and a slow envelope follower; an attack is a fast jump above the
// slow envelope. The manual says the pedal does "pick attack detection" but not how, so this is
// a conventional design with a sensitivity control (0 = hard to trigger, 1 = very sensitive).
class AttackDetector
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        fastAttack = coeffFor(0.3f);
        fastRelease = coeffFor(8.0f);
        slowAttack = coeffFor(30.0f);
        slowRelease = coeffFor(150.0f);
        holdoffSamples = (int) (0.07 * sampleRate);
        reset();
    }

    void reset()
    {
        fast = slow = 0.0f;
        holdoff = 0;
        armed = true;
    }

    void setSensitivity(float s)
    {
        s = std::clamp(s, 0.0f, 1.0f);
        ratio = 4.0f + (1.8f - 4.0f) * s;
        floorLevel = std::pow(10.0f, floorDb(s) / 20.0f);
    }

    // How loud a pick must be (dBFS, after the input gain) to count, for a sensitivity (the editor draws it as a line
    // on the input meter).
    static float floorDb(float sensitivity) { return -40.0f + (-62.0f + 40.0f) * std::clamp(sensitivity, 0.0f, 1.0f); }

    bool process(float x)
    {
        const float a = std::abs(x);
        fast += (a > fast ? fastAttack : fastRelease) * (a - fast);
        slow += (a > slow ? slowAttack : slowRelease) * (a - slow);

        if (holdoff > 0)
            --holdoff;

        if (armed)
        {
            if (fast > floorLevel && fast > slow * ratio)
            {
                armed = false;
                holdoff = holdoffSamples;
                return true;
            }
        }
        else if (holdoff == 0 && fast < slow * 1.5f)
        {
            armed = true;
        }
        return false;
    }

private:
    float coeffFor(float ms) const { return 1.0f - std::exp(-1.0f / (float) (fs * 0.001 * ms)); }

    double fs = 48000.0;
    float fastAttack = 0, fastRelease = 0, slowAttack = 0, slowRelease = 0;
    float fast = 0, slow = 0;
    float ratio = 2.8f;
    float floorLevel = 0.01f;
    int holdoffSamples = 0, holdoff = 0;
    bool armed = true;
};

// Two-tap crossfading delay-line pitch shifter. Characteristically grainy/warbly, which suits
// the lo-fi context. A shift of exactly zero is a perfect dry pass-through.
class PitchShifter
{
public:
    void prepare(double sampleRate)
    {
        window = std::max(64.0f, (float) (0.04 * sampleRate));
        int n = 1;
        while (n < (int) window + 16)
            n <<= 1;
        buffer.assign((size_t) n, 0.0f);
        mask = n - 1;
        reset();
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        phase = 0.0f;
    }

    // ratio = playback-speed ratio (2^(semitones/12)); amount blends dry (0) -> shifted (1).
    float process(float in, float ratio, float amount)
    {
        buffer[(size_t) writePos] = in;
        writePos = (writePos + 1) & mask;

        float wet = 0.0f;
        if (amount > 0.0f)
        {
            const float p2 = phase + 0.5f >= 1.0f ? phase - 0.5f : phase + 0.5f;
            const float g1 = 0.5f - 0.5f * std::cos(2.0f * kPi * phase);
            const float g2 = 1.0f - g1;
            wet = g1 * tap(phase * window) + g2 * tap(p2 * window);

            phase += (1.0f - ratio) / window;
            phase -= std::floor(phase);
        }
        return lerpExact(in, wet, amount);
    }

private:
    // 4-point Hermite interpolation at a fractional delay (in samples).
    float tap(float delay) const
    {
        const float pos = (float) writePos - 1.0f - std::max(delay, 2.0f);
        const float fl = std::floor(pos);
        const float t = pos - fl;
        const int i = (int) fl;
        const float y0 = buffer[(size_t) ((i - 1) & mask)];
        const float y1 = buffer[(size_t) (i & mask)];
        const float y2 = buffer[(size_t) ((i + 1) & mask)];
        const float y3 = buffer[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    std::vector<float> buffer;
    int mask = 0, writePos = 0;
    float window = 1920.0f;
    float phase = 0.0f;
};

// Four-pole transistor-ladder-style low-pass (zero-delay-feedback form) with a soft clipper in
// the loop. The hardware's documentation describes its filter as a ladder-style low-pass and a
// review describes it as resonant, but the resonance amount is not documented.
// ASSUMED: fixed moderate resonance.
class LadderFilter
{
public:
    static constexpr float kResonance = 1.4f; // 4.0 would self-oscillate

    void reset() { s[0] = s[1] = s[2] = s[3] = 0.0f; }

    // G = g / (1 + g), g = tan(pi * fc / fs)
    float process(float x, float G)
    {
        const float oneMinusG = 1.0f - G;
        const float G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        const float S = oneMinusG * (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]);
        const float y4 = (G4 * x + S) / (1.0f + kResonance * G4);

        float u = x - kResonance * y4;
        u = std::tanh(u);

        float in = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = G * (in - s[i]);
            const float y = v + s[i];
            s[i] = y + v;
            in = y;
        }
        // Make up the passband loss caused by the feedback so the filter is ~unity at low frequencies.
        return in * (1.0f + kResonance);
    }

private:
    float s[4] {};
};

// "Stuck buffer" stutter, one instance per channel. On a trigger it captures a snippet of the
// live signal (starting a few ms before the detected attack so the pick transient is included),
// passes the live signal through while recording, then repeats the snippet N times at 1x, 2x or
// 0.5x speed (forward or reversed) and returns to the live signal. Hold keeps repeating the
// snippet until released.
// What the stutter does with the slice it has caught. The STUTTER knob's repeat count and speed mean, for each:
//   Repeat    plays the slice that many times, at that speed (what the pedal does)
//   Reverse   the same, backwards
//   Rise      each repeat a whole tone higher than the one before (up to two octaves)
//   TapeStop  the slice slows down to a stop, like a tape machine switched off; it takes that many slices to stop
//   Scratch   the slice pushed back and forth like a record under a hand, one push and pull per slice
//   Freeze    the end of the slice held as a steady cloud of tiny overlapping grains, for that many slices
enum class StutterKind
{
    Repeat = 0,
    Reverse,
    Rise,
    TapeStop,
    Scratch,
    Freeze
};
inline constexpr int kNumStutterKinds = 6;

class StutterChannel
{
public:
    static constexpr float kRiseStep = 1.122462048f; // a whole tone
    static constexpr float kRiseMost = 4.0f;          // two octaves above the start
    void prepare(double sampleRate, int snippetCapacity)
    {
        capacity = snippetCapacity;
        snippet.assign((size_t) capacity, 0.0f);
        int n = 1;
        while (n < (int) (0.05 * sampleRate))
            n <<= 1;
        ring.assign((size_t) n, 0.0f);
        ringMask = n - 1;
        fadeLen = std::max(8, (int) (0.001 * sampleRate));
        xfadeLen = std::max(16, (int) (0.003 * sampleRate));
        grainMax = std::max(64, (int) (0.06 * sampleRate));
        reset();
    }

    void reset()
    {
        std::fill(ring.begin(), ring.end(), 0.0f);
        ringPos = 0;
        state = State::Idle;
        xfadeLeft = 0;
        lastOut = 0.0f;
        heldByUser = false;
    }

    bool isHeld() const { return heldByUser && state != State::Idle; }
    bool isActive() const { return state != State::Idle; }

    bool isPlaying() const { return state == State::Playing; }
    float getSpeed() const { return speed; } // the playback rate now (for the tests)

    // repeats < 0 means "until released".
    void trigger(int length, int preRoll, float speedRatio, int repeats, bool reversed, bool hold, StutterKind newKind = StutterKind::Repeat)
    {
        kind = newKind;
        if (kind == StutterKind::Reverse)
            reversed = !reversed;
        else if (kind != StutterKind::Repeat && kind != StutterKind::Rise)
            reversed = false;
        length = std::clamp(length, 32, capacity);
        preRoll = std::clamp(preRoll, 0, length / 4);
        for (int i = 0; i < preRoll; ++i)
            snippet[(size_t) i] = ring[(size_t) ((ringPos - preRoll + i) & ringMask)];

        recPos = preRoll;
        snippetLen = length;
        speed = baseSpeed = speedRatio;
        repeatsTotal = std::max(1, repeats);
        elapsed = 0;
        infinite = repeats < 0 || hold;
        heldByUser = hold;
        reverse = reversed;
        passes = 0;

        if (state == State::Playing)
            startXfade();
        state = State::Recording;
    }

    // The hold switch went down while a stutter was already in progress.
    void engageHold()
    {
        if (state != State::Idle)
        {
            heldByUser = true;
            infinite = true;
        }
    }

    // The hold switch was released: stop immediately.
    void releaseHold()
    {
        if (heldByUser && state != State::Idle)
        {
            startXfade();
            state = State::Idle;
        }
        heldByUser = false;
    }

    float process(float in)
    {
        ring[(size_t) (ringPos & ringMask)] = in;
        ++ringPos;

        float out = in;
        switch (state)
        {
            case State::Idle:
                break;

            case State::Recording:
                snippet[(size_t) recPos++] = in;
                if (recPos >= snippetLen)
                {
                    state = State::Playing;
                    pos = reverse ? (float) (snippetLen - 1) : 0.0f;
                    passes = 0;
                    elapsed = 0;
                    grainPhase = 0.0f;
                }
                break;

            case State::Playing:
                switch (kind)
                {
                    case StutterKind::TapeStop: out = tapeStopSample(); break;
                    case StutterKind::Scratch: out = scratchSample(); break;
                    case StutterKind::Freeze: out = freezeSample(); break;
                    default: out = playbackSample(); break;
                }
                break;
        }

        if (xfadeLeft > 0)
        {
            const float t = (float) xfadeLeft / (float) xfadeLen;
            out += (xfadeFrom - out) * t;
            --xfadeLeft;
        }
        lastOut = out;
        return out;
    }

private:
    enum class State { Idle, Recording, Playing };

    void startXfade()
    {
        xfadeFrom = lastOut;
        xfadeLeft = xfadeLen;
    }

    float readAt(float at) const
    {
        const int i0 = std::clamp((int) at, 0, snippetLen - 1);
        const int i1 = std::min(i0 + 1, snippetLen - 1);
        const float frac = std::clamp(at - (float) i0, 0.0f, 1.0f);
        return snippet[(size_t) i0] + (snippet[(size_t) i1] - snippet[(size_t) i0]) * frac;
    }

    // The effect has run its course (Tape stop, Scratch, Freeze): back to the live sound, crossfaded.
    void finish()
    {
        startXfade();
        state = State::Idle;
    }

    // How long a Tape stop, Scratch or Freeze lasts, in samples: the repeat count in slices played at the speed.
    double runLength() const { return (double) repeatsTotal * (double) snippetLen / (double) std::max(0.05f, baseSpeed); }

    float tapeStopSample()
    {
        const double total = runLength();
        const double u = std::min(1.0, (double) elapsed / total);
        speed = baseSpeed * (float) ((1.0 - u) * (1.0 - u)); // slows quickly at first, then crawls to a stop
        float s = readAt(pos);
        const float edge = std::min(pos, (float) snippetLen - pos);
        s *= std::clamp(edge / (float) fadeLen, 0.0f, 1.0f);
        const double tail = std::clamp((1.0 - u) / 0.2, 0.0, 1.0);
        s *= (float) (tail * tail); // the last moments fade out, as a stopping tape does
        pos += speed;
        if (pos >= (float) snippetLen)
            pos -= (float) snippetLen;
        if (++elapsed >= (std::int64_t) total)
        {
            if (infinite)
            {
                elapsed = 0; // held: stop, then start again
                pos = 0.0f;
            }
            else
                finish();
        }
        return s;
    }

    float scratchSample()
    {
        // one push and pull per slice: the position swings from a tenth of the way in to eight tenths and back
        const double cycle = (double) snippetLen / (double) std::max(0.05f, baseSpeed);
        const double phase = (double) elapsed / cycle;
        const float swing = 0.35f * (float) snippetLen;
        const float at = 0.1f * (float) snippetLen + swing * (1.0f - (float) std::cos(6.283185307179586 * phase));
        speed = baseSpeed * 2.2f * (float) std::sin(6.283185307179586 * phase); // the record's speed, backwards on the pull
        const float s = readAt(at);
        if (++elapsed >= (std::int64_t) runLength() && !infinite)
            finish();
        return s;
    }

    float freezeSample()
    {
        // two grains half a grain apart, each faded in and out (Hann), read from the end of the slice
        const int grain = std::min(grainMax, std::max(32, snippetLen / 2));
        const float start = (float) (snippetLen - grain);
        float s = 0.0f;
        for (int g = 0; g < 2; ++g)
        {
            float phase = grainPhase + 0.5f * (float) g;
            phase -= std::floor(phase);
            const float window = 0.5f - 0.5f * (float) std::cos(6.283185307179586 * phase);
            s += window * readAt(start + phase * (float) (grain - 1));
        }
        grainPhase += baseSpeed / (float) grain;
        grainPhase -= std::floor(grainPhase);
        speed = baseSpeed;
        if (++elapsed >= (std::int64_t) runLength() && !infinite)
            finish();
        return s;
    }

    float playbackSample()
    {
        float s = readAt(pos);

        // Short fades at both ends of every pass stop each repeat from clicking.
        const float edge = std::min(pos, (float) snippetLen - pos);
        s *= std::clamp(edge / (float) fadeLen, 0.0f, 1.0f);

        pos += reverse ? -speed : speed;
        if (pos >= (float) snippetLen || pos < 0.0f)
        {
            ++passes;
            if (!infinite && passes >= repeatsTotal)
            {
                state = State::Idle;
            }
            else
            {
                if (kind == StutterKind::Rise) // each repeat a whole tone higher, up to two octaves
                    speed = std::min(speed * kRiseStep, baseSpeed * kRiseMost);
                pos = reverse ? pos + (float) snippetLen : pos - (float) snippetLen;
                pos = std::clamp(pos, 0.0f, (float) (snippetLen - 1));
            }
        }
        return s;
    }

    std::vector<float> snippet, ring;
    int capacity = 0, ringMask = 0, ringPos = 0;
    int snippetLen = 0, recPos = 0, passes = 0, repeatsTotal = 1;
    int fadeLen = 48, xfadeLen = 144, xfadeLeft = 0, grainMax = 2880;
    float pos = 0.0f, speed = 1.0f, baseSpeed = 1.0f, xfadeFrom = 0.0f, lastOut = 0.0f, grainPhase = 0.0f;
    std::int64_t elapsed = 0;
    StutterKind kind = StutterKind::Repeat;
    bool reverse = false, infinite = false, heldByUser = false;
    State state = State::Idle;
};
} // namespace bitvibe
