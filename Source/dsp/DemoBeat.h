#pragma once

// A synthetic drum kit and a four-bar demo beat made with it: no samples or recordings involved, so it
// can ship with the plug-in (the DRUMS tab offers it as "Demo beat") and serve the tests, which know
// exactly where every hit is. Header-only and free of JUCE.

#include "DrumMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bitvibe::demo
{
struct Note
{
    double time = 0.0; // seconds
    Drum drum = Drum::Kick;
    float gain = 1.0f;
};

// Pitched-down sine kick with a beater click, tone + band-limited noise snare, high-passed metallic
// hi-hat, darker long crash, pitched tom.
class SynthKit
{
public:
    explicit SynthKit(double sampleRate, uint32_t seed = 21) : fs(sampleRate), state(seed != 0 ? seed : 1u) {}

    void play(std::vector<float>& out, const Note& n)
    {
        const size_t at = (size_t) std::max(0.0, n.time * fs);
        switch (n.drum)
        {
            case Drum::Kick: kick(out, at, n.gain); break;
            case Drum::Snare: snare(out, at, n.gain); break;
            case Drum::HiHat: noiseBurst(out, at, 7000.0, 25.0, 0.8f * n.gain, true); break;
            case Drum::Cymbal: noiseBurst(out, at, 1500.0, 700.0, 0.6f * n.gain, true, 0.0, 0.45); break;
            case Drum::Tom: tom(out, at, n.gain); break;
        }
    }

private:
    static constexpr double pi = 3.14159265358979323846;

    // Portable noise in [-1, 1) (the same numbers on every compiler).
    double noise()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (double) (float) ((double) state / 4294967296.0) * 2.0 - 1.0;
    }

    // A decaying noise burst through two one-pole high-passes and an optional two-pole low-pass; "ringy"
    // adds a few inharmonic partials (ringScale moves them down for a darker metal).
    void noiseBurst(std::vector<float>& out, size_t at, double cutoffHz, double decayMs, float gain, bool ringy = false,
                    double lowpassHz = 0.0, double ringScale = 1.0)
    {
        const double a = std::exp(-2.0 * pi * cutoffHz / fs);
        const double lp = lowpassHz > 0.0 ? 1.0 - std::exp(-2.0 * pi * lowpassHz / fs) : 1.0;
        double x1 = 0, y1 = 0, y2 = 0, z1 = 0, l1 = 0, l2 = 0;
        const double decay = decayMs * 0.001 * fs;
        const size_t len = (size_t) (decay * 9.0);
        const double r = 48000.0 / fs; // keeps the partials at the same pitch at any sample rate
        for (size_t i = 0; i < len && at + i < out.size(); ++i)
        {
            const double env = std::exp(-(double) i / decay);
            double v = noise();
            if (ringy)
            {
                const double k = ringScale * r * (double) i;
                v = 0.5 * v + 0.5 * (std::sin(0.71 * k) + std::sin(1.13 * k) + std::sin(1.67 * k)) / 3.0;
            }
            const double y = a * (y1 + v - x1);
            x1 = v;
            y1 = y;
            const double z = a * (z1 + y - y2);
            y2 = y;
            z1 = z;
            l1 += lp * (z - l1);
            l2 += lp * (l1 - l2);
            out[at + i] += (float) (gain * env * l2);
        }
    }

    void kick(std::vector<float>& out, size_t at, float gain)
    {
        double phase = 0.0;
        for (size_t i = 0; i < (size_t) (1.1 * fs) && at + i < out.size(); ++i)
        {
            const double t = (double) i / fs;
            phase += 2.0 * pi * (50.0 + 90.0 * std::exp(-t / 0.03)) / fs;
            out[at + i] += (float) (gain * std::exp(-t / 0.12) * std::sin(phase));
        }
        noiseBurst(out, at, 2000.0, 2.0, 0.15f * gain); // beater click
    }

    void snare(std::vector<float>& out, size_t at, float gain)
    {
        for (size_t i = 0; i < (size_t) (0.25 * fs) && at + i < out.size(); ++i)
        {
            const double t = (double) i / fs;
            out[at + i] += (float) (0.6 * gain * std::exp(-t / 0.05) * std::sin(2.0 * pi * 190.0 * t));
        }
        noiseBurst(out, at, 1000.0, 60.0, 0.8f * gain, false, 7000.0); // wires: little energy above 8 kHz
    }

    void tom(std::vector<float>& out, size_t at, float gain)
    {
        double phase = 0.0;
        for (size_t i = 0; i < (size_t) (1.8 * fs) && at + i < out.size(); ++i)
        {
            const double t = (double) i / fs;
            phase += 2.0 * pi * (150.0 + 40.0 * std::exp(-t / 0.05)) / fs;
            out[at + i] += (float) (gain * std::exp(-t / 0.2) * std::sin(phase));
        }
    }

    double fs;
    uint32_t state;
};

// Four bars of rock beat: kick on 1 and 3, snare on 2 and 4 (one quieter per two bars), eighth-note
// hi-hats with loud and quiet off-beats, a crash instead of the hat at bars 1 and 3, and a tom fill
// in the last bar.
inline std::vector<Note> demoPattern(double bpm = 100.0, int bars = 4, double start = 0.05)
{
    std::vector<Note> notes;
    const double beat = 60.0 / bpm;
    for (int b = 0; b < bars * 4; ++b)
    {
        const double t = start + b * beat;
        const bool lastBar = b >= (bars - 1) * 4;
        const bool crash = b % 8 == 0;
        if (b % 4 == 0 || b % 4 == 2)
            notes.push_back({ t, Drum::Kick, 0.8f });
        if (b % 4 == 1 || b % 4 == 3)
            notes.push_back({ t, Drum::Snare, b % 8 == 7 ? 0.35f : 0.7f });
        if (!lastBar)
        {
            if (!crash)
                notes.push_back({ t, Drum::HiHat, 0.5f });
            notes.push_back({ t + beat / 2, Drum::HiHat, b % 2 == 0 ? 0.4f : 0.12f });
        }
        if (crash)
            notes.push_back({ t, Drum::Cymbal, 0.6f });
        if (lastBar && b % 4 >= 2)
            notes.push_back({ t + beat / 2, Drum::Tom, 0.7f });
    }
    return notes;
}

struct Beat
{
    std::vector<float> audio; // mono
    double sampleRate = 48000.0;
    double bpm = 100.0;
    std::vector<Note> notes;
};

// The demo beat as an exact loop (bars x 4 beats long), peak-normalised to about -1 dBFS.
inline Beat renderDemoBeat(double sampleRate = 48000.0, double bpm = 100.0, int bars = 4)
{
    Beat b;
    b.sampleRate = sampleRate;
    b.bpm = bpm;
    b.notes = demoPattern(bpm, bars);
    b.audio.assign((size_t) std::lround(bars * 4 * 60.0 / bpm * sampleRate), 0.0f);
    SynthKit kit(sampleRate);
    for (const auto& n : b.notes)
        kit.play(b.audio, n);
    float peak = 0.0f;
    for (float v : b.audio)
        peak = std::max(peak, std::abs(v));
    if (peak > 0.0f)
        for (auto& v : b.audio)
            v *= 0.89f / peak;
    return b;
}
} // namespace bitvibe::demo
