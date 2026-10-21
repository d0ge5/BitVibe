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
