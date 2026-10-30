#pragma once

// The drum map: the hits found in a drum beat, the per-drum settings that decide which of them fire a
// sequencer / stutter trigger, and the scheduler that turns them into trigger events while the beat
// loops. Header-only and free of JUCE so the offline tests can check it.

#include "Engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace bitvibe
{
inline constexpr int kNumDrums = 5;

enum class Drum
{
    Kick = 0,
    Snare = 1,
    HiHat = 2,
    Cymbal = 3,
    Tom = 4
};

inline const char* drumName(Drum d)
{
    static constexpr const char* names[kNumDrums] = { "Kick", "Snare", "Hi-hat", "Cymbal", "Tom" };
    return names[std::clamp((int) d, 0, kNumDrums - 1)];
}

// Identifiers used in analysis files and the saved state.
inline const char* drumId(Drum d)
{
    static constexpr const char* names[kNumDrums] = { "kick", "snare", "hihat", "cymbal", "tom" };
    return names[std::clamp((int) d, 0, kNumDrums - 1)];
}

inline bool drumFromId(std::string_view id, Drum& out)
{
    for (int i = 0; i < kNumDrums; ++i)
        if (id == drumId((Drum) i))
        {
            out = (Drum) i;
            return true;
        }
    return false;
}

struct DrumHit
{
    double time = 0.0;       // seconds from the start of the beat
    Drum drum = Drum::Kick;
    float levelDb = 0.0f;    // after normaliseLevels(): dB below the loudest hit of the same drum (0 = loudest)
    float confidence = 1.0f; // how sure the analysis is, 0..1
    bool excluded = false;   // switched off by the user
};

// What a drum does when one of its hits passes its settings.
enum class DrumAction
{
    Off = 0,
    RestartSequence = 1,
    NextStep = 2,
    Stutter = 3,
    RestartAndStutter = 4
};
inline constexpr int kNumDrumActions = 5;

inline const char* drumActionName(DrumAction a)
{
    static constexpr const char* names[kNumDrumActions] = { "Off", "Restart sequence", "Next step", "Stutter",
                                                            "Restart + stutter" };
    return names[std::clamp((int) a, 0, kNumDrumActions - 1)];
}

inline TriggerAction toTriggerAction(DrumAction a)
{
    switch (a)
    {
        case DrumAction::NextStep: return TriggerAction::NextStep;
        case DrumAction::Stutter: return TriggerAction::Stutter;
        case DrumAction::RestartAndStutter: return TriggerAction::RestartAndStutter;
        case DrumAction::Off:
        case DrumAction::RestartSequence: break;
    }
    return TriggerAction::RestartSequence;
}

struct DrumSettings
{
    DrumAction action = DrumAction::Off;
    float minLevelDb = -12.0f;   // ignore hits quieter than this, in dB below the loudest hit of the drum
    float minConfidence = 0.3f;  // ignore hits the analysis is less sure about

    bool operator==(const DrumSettings& o) const
    {
        return action == o.action && sameFloat(minLevelDb, o.minLevelDb) && sameFloat(minConfidence, o.minConfidence);
    }
};

inline constexpr float kMinLevelFloorDb = -60.0f; // quietest level a hit is given (and the slider's end)

using DrumSettingsArray = std::array<DrumSettings, kNumDrums>;

// The detection threshold applies whatever the action, so the feed can show what was detected.
inline bool hitDetected(const DrumHit& h, const DrumSettings& s)
{
    return h.confidence >= s.minConfidence;
}

inline bool hitFires(const DrumHit& h, const DrumSettings& s)
{
    return !h.excluded && s.action != DrumAction::Off && hitDetected(h, s) && h.levelDb >= s.minLevelDb - 1.0e-4f;
}

// Sorts by time (then drum) and makes every level relative to the loudest hit of its drum. Levels come
// in as absolute dB; they leave as 0 (loudest) down to kMinLevelFloorDb.
inline void normaliseLevels(std::vector<DrumHit>& hits)
{
    std::array<float, kNumDrums> loudest;
    loudest.fill(-1.0e9f);
    for (const auto& h : hits)
        if (std::isfinite(h.levelDb))
            loudest[(size_t) h.drum] = std::max(loudest[(size_t) h.drum], h.levelDb);
    for (auto& h : hits)
    {
        const float rel = std::isfinite(h.levelDb) ? h.levelDb - loudest[(size_t) h.drum] : kMinLevelFloorDb;
        h.levelDb = std::clamp(rel, kMinLevelFloorDb, 0.0f);
        h.confidence = std::isfinite(h.confidence) ? std::clamp(h.confidence, 0.0f, 1.0f) : 0.0f;
    }
    std::stable_sort(hits.begin(), hits.end(), [](const DrumHit& a, const DrumHit& b)
                     { return a.time < b.time || (!(b.time < a.time) && (int) a.drum < (int) b.drum); });
}

// Turns the hits of a looping beat into trigger events for one audio block. Stateless: the caller keeps
// the play position, so consecutive blocks cover the loop without gaps or double triggers.
struct DrumScheduler
{
    // hitSamples: each hit's position in the loop in samples, ascending, same order as hits.
    // position: where in the loop this block starts (0 <= position < loopLength).
    // onFire(hitIndex, offset) is called for every hit that fires (also when out is full).
    // Returns the number of events written to out (at most maxEvents), sorted by offset.
    template <typename OnFire>
    static int collect(const DrumHit* hits, const int64_t* hitSamples, int numHits, int64_t loopLength,
                       int64_t position, int numSamples, const DrumSettingsArray& settings, TriggerEvent* out,
                       int maxEvents, OnFire&& onFire)
    {
        if (hits == nullptr || hitSamples == nullptr || numHits <= 0 || loopLength <= 0 || numSamples <= 0)
            return 0;

        int written = 0;
        int64_t segStart = std::clamp<int64_t>(position, 0, loopLength - 1);
        int64_t remaining = numSamples;
        int64_t offsetBase = 0;
        while (remaining > 0)
        {
            const int64_t segLength = std::min(remaining, loopLength - segStart);
            const int64_t segEnd = segStart + segLength;
            const int64_t* first = std::lower_bound(hitSamples, hitSamples + numHits, segStart);
            for (const int64_t* it = first; it != hitSamples + numHits && *it < segEnd; ++it)
            {
                const int index = (int) (it - hitSamples);
                const DrumHit& h = hits[index];
                const DrumSettings& s = settings[(size_t) h.drum];
                if (!hitFires(h, s))
                    continue;
                const int offset = (int) (offsetBase + (*it - segStart));
                onFire(index, offset);
                if (written < maxEvents)
                    out[written++] = { offset, toTriggerAction(s.action) };
            }
            remaining -= segLength;
            offsetBase += segLength;
            segStart = 0;
        }
        return written;
    }

    static int collect(const DrumHit* hits, const int64_t* hitSamples, int numHits, int64_t loopLength,
                       int64_t position, int numSamples, const DrumSettingsArray& settings, TriggerEvent* out,
                       int maxEvents)
    {
        return collect(hits, hitSamples, numHits, loopLength, position, numSamples, settings, out, maxEvents,
                       [](int, int) {});
    }
};

// ---- beat pattern -------------------------------------------------------------------------------------
//
// A trigger that does not wait for a drum. The player picks a time signature and lights steps of a one-bar grid
// of sixteenth notes; the engine fires the pattern's action at every lit step, bar after bar, from the plug-in's
// own tempo (or the host's position when SYNC is on). Nothing to do with the loudness of what is played.

inline constexpr int kMaxPatternSteps = 24; // the longest bar of the time signatures below (12/8)
inline constexpr int kPatternSteps = 16;    // a bar of 4/4
inline constexpr uint32_t kPatternMask = (1u << kMaxPatternSteps) - 1u;

// A time signature as the grid draws it: the bar is split into beats (groups of sixteenth-note steps).
struct TimeSignature
{
    const char* name;
    int numGroups;
    std::array<int, 5> groupSteps;
    int totalSteps;
};

inline constexpr int kNumTimeSignatures = 10;

// The ten most common signatures. Compound ones (6/8, 12/8, 9/8, 3/8) count dotted-quarter beats of six steps,
// 7/8 is counted 2 + 2 + 3 eighths, 2/2 has two half-note beats of eight steps.
inline const TimeSignature& timeSignature(int index)
{
    static const std::array<TimeSignature, kNumTimeSignatures> table {{
        { "4/4", 4, { 4, 4, 4, 4, 0 }, 16 },
        { "3/4", 3, { 4, 4, 4, 0, 0 }, 12 },
        { "2/4", 2, { 4, 4, 0, 0, 0 }, 8 },
        { "6/8", 2, { 6, 6, 0, 0, 0 }, 12 },
        { "5/4", 5, { 4, 4, 4, 4, 4 }, 20 },
        { "7/8", 3, { 4, 4, 6, 0, 0 }, 14 },
        { "12/8", 4, { 6, 6, 6, 6, 0 }, 24 },
        { "9/8", 3, { 6, 6, 6, 0, 0 }, 18 },
        { "2/2", 2, { 8, 8, 0, 0, 0 }, 16 },
        { "3/8", 1, { 6, 0, 0, 0, 0 }, 6 },
    }};
    return table[(size_t) std::clamp(index, 0, kNumTimeSignatures - 1)];
}

struct DrumPattern
{
    DrumAction action = DrumAction::Off;
    uint32_t steps = 0;          // bit n set = step n of the bar fires (step 0 = the first sixteenth of the bar)
    uint32_t randomBeats = 0x1Fu; // which beats RANDOM may change (bit 0 = beat 1); the audio ignores it
    int signature = 0;           // index into the time signatures

    bool operator==(const DrumPattern& o) const
    {
        return action == o.action && steps == o.steps && randomBeats == o.randomBeats && signature == o.signature;
    }
};

// Where each beat (group) of a bar starts, and the step in the middle of it (the "and").
inline int patternGroupStart(const TimeSignature& ts, int group)
{
    int start = 0;
    for (int g = 0; g < group && g < ts.numGroups; ++g)
        start += ts.groupSteps[(size_t) g];
    return start;
}

// RANDOM: a new random pattern inside the chosen beats (beatMask bit b = beat b+1); the other beats keep their
// steps. The first step of a beat is the likeliest to fire, then the "and" in the middle of it, then the steps in
// between. At least one step in the chosen beats is lit. A beat mask of 0 changes nothing. Pure and portable: the
// same seed always gives the same pattern.
inline uint32_t patternRandom(uint32_t steps, uint32_t beatMask, uint32_t seed, int signature = 0)
{
    const auto& ts = timeSignature(signature);
    beatMask &= (1u << ts.numGroups) - 1u;
    if (beatMask == 0)
        return steps & kPatternMask;
    uint32_t state = seed * 2654435761u + 0x9E3779B9u;
    auto next = [&state]
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (int) (state % 100u);
    };
    uint32_t result = steps & kPatternMask;
    uint32_t lit = 0;
    for (int group = 0; group < ts.numGroups; ++group)
    {
        if (((beatMask >> group) & 1u) == 0u)
            continue;
        const int size = ts.groupSteps[(size_t) group], start = patternGroupStart(ts, group);
        for (int s = 0; s < size; ++s)
            result &= ~(1u << (start + s));
        for (int s = 0; s < size; ++s)
        {
            const int chance = s == 0 ? 55 : (s == size / 2 ? 40 : 20); // percent
            if (next() < chance)
                lit |= 1u << (start + s);
        }
    }
    if (lit == 0) // never leave the chosen beats empty: light the first step of one of them
    {
        int pick = next() % ts.numGroups;
        while (((beatMask >> pick) & 1u) == 0u)
            pick = (pick + 1) % ts.numGroups;
        lit = 1u << patternGroupStart(ts, pick);
    }
    return (result | lit) & kPatternMask;
}

// Ready-made patterns for the buttons next to the grid (0 = clear): the first step of every beat, the "and" in the
// middle of every beat, and the backbeat (every second beat: 2 and 4 in 4/4).
inline uint32_t patternPreset(int which, int signature = 0)
{
    const auto& ts = timeSignature(signature);
    uint32_t bits = 0;
    for (int g = 0; g < ts.numGroups; ++g)
    {
        const int start = patternGroupStart(ts, g), size = ts.groupSteps[(size_t) g];
        switch (which)
        {
            case 1: bits |= 1u << start; break;
            case 2: bits |= 1u << (start + size / 2); break;
            case 3:
                if (g % 2 == 1 || ts.numGroups == 1)
                    bits |= 1u << start;
                break;
            default: break;
        }
    }
    return bits;
}

// Tempo of a drum loop from its hits: an autocorrelation of the onset train with a mild preference for
// 120 BPM, then snapped to a whole number of beats over the loop when that is close (loops are usually
// cut on the beat). Returns 0 when there is too little to go on.
inline double estimateTempo(const std::vector<DrumHit>& hits, double durationSeconds)
{
    constexpr double fps = 200.0;
    const int frames = (int) std::ceil(durationSeconds * fps);
    if (frames < 8 || hits.size() < 4)
        return 0.0;

    std::vector<double> train((size_t) frames, 0.0);
    for (const auto& h : hits)
    {
        if (h.excluded)
            continue;
        const int f = (int) std::lround(h.time * fps);
        if (f < 0 || f >= frames)
            continue;
        // Kick and snare carry the beat more than hats and cymbals.
        const double drumWeight = h.drum == Drum::Kick || h.drum == Drum::Snare ? 1.0 : 0.5;
        train[(size_t) f] += drumWeight * std::pow(10.0, h.levelDb / 20.0) * std::max(0.05f, h.confidence);
    }
    // A little smearing so slightly early / late hits still line up.
    std::vector<double> smooth(train.size(), 0.0);
    for (size_t i = 0; i < train.size(); ++i)
        for (int k = -2; k <= 2; ++k)
        {
            const long j = (long) i + k;
            if (j >= 0 && j < (long) train.size())
                smooth[i] += train[(size_t) j] * (3 - std::abs(k));
        }

    // Circular autocorrelation: the beat is a loop.
    double bestScore = -1.0, bestBpm = 0.0;
    for (double bpm = 50.0; bpm <= 220.0; bpm += 0.25)
    {
        const double lag = 60.0 * fps / bpm;
        double score = 0.0;
        for (int mult = 1; mult <= 4; ++mult) // the beat and its multiples (bars)
        {
            const double l = lag * mult;
            const int l0 = (int) std::floor(l);
            const double frac = l - l0;
            double acc = 0.0;
            for (size_t i = 0; i < smooth.size(); ++i)
            {
                const double a = smooth[(i + (size_t) l0) % smooth.size()];
                const double b = smooth[(i + (size_t) l0 + 1) % smooth.size()];
                acc += smooth[i] * (a * (1.0 - frac) + b * frac);
            }
            score += acc / mult;
        }
        const double octaves = std::log2(bpm / 120.0);
        score *= std::exp(-0.5 * octaves * octaves / (0.9 * 0.9));
        if (score > bestScore)
        {
            bestScore = score;
            bestBpm = bpm;
        }
    }
    if (bestScore <= 0.0)
        return 0.0;

    const double beats = durationSeconds * bestBpm / 60.0;
    const double whole = std::round(beats);
    if (whole >= 1.0 && std::abs(beats - whole) / whole < 0.04)
        return whole * 60.0 / durationSeconds;
    return bestBpm;
}
} // namespace bitvibe
