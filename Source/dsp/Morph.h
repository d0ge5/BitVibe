#pragma once

// Expression morph: two complete sets of knob settings ("toe up" and "toe down") and the maths that
// blends them. Header-only and free of JUCE so the plug-in and the offline tests share it.
//
// A KnobSet is everything the twelve knobs of the two layers (front panel and Alt) can say, plus the
// sequencer type. Continuous knobs - the three effect amounts and the six steps - are interpolated
// linearly in knob position, so the frequency controls glide exponentially, just as turning the knob
// would. The discrete ones - stutter position, sequencer passes, multiplier and type - cannot be
// halfway between two settings, so they switch from the toe-up value to the toe-down value at the
// midpoint of the sweep. ASSUMED: the exact blend law (linear, one midpoint for every discrete knob)
// is a design choice of this project, not something the published documentation specifies.

#include "Engine.h"
#include "Mapping.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bitvibe
{
struct KnobSet
{
    float sampleRate = 1.0f;
    float filter = 1.0f;
    float bits = 1.0f;
    int stutter = 0; // 0..22
    int seqPlay = 5; // 0..5
    int seqMult = 1; // 0..7
    std::array<float, kNumSteps> steps {
        mapping::pitchStepKnob(-7), mapping::pitchStepKnob(-3), mapping::pitchStepKnob(-12),
        mapping::pitchStepKnob(7),  mapping::pitchStepKnob(4),  mapping::pitchStepKnob(0)
    };
    SeqType seqType = SeqType::Pitch;

    bool operator==(const KnobSet&) const = default;
};

namespace morph
{
    inline float clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

    // Brings every field into its legal range (state files and hosts can hand us anything).
    inline KnobSet sanitise(KnobSet k)
    {
        k.sampleRate = clamp01(k.sampleRate);
        k.filter = clamp01(k.filter);
        k.bits = clamp01(k.bits);
        for (auto& s : k.steps)
            s = clamp01(s);
        k.stutter = std::clamp(k.stutter, 0, mapping::kStutterPositions - 1);
        k.seqPlay = std::clamp(k.seqPlay, 0, mapping::kSeqPlayPositions - 1);
        k.seqMult = std::clamp(k.seqMult, 0, mapping::kSeqMultPositions - 1);
        k.seqType = (SeqType) std::clamp((int) k.seqType, 0, 2);
        return k;
    }

    // a at t = 0, b at t = 1 exactly (a*(1-t) + b*t has no rounding error at the end points).
    inline float lerp(float a, float b, float t)
    {
        return a == b ? a : clamp01(a * (1.0f - t) + b * t);
    }

    // Expression position at which the discrete knobs hop from the toe-up to the toe-down value.
    inline constexpr float kSwitchPoint = 0.5f;

    // expression: 0 = toe up, 1 = toe down. Values outside 0..1 are clamped, NaN counts as 0.
    inline KnobSet blend(const KnobSet& toeUp, const KnobSet& toeDown, float expression)
    {
        const float t = clamp01(expression);
        const KnobSet& discrete = t < kSwitchPoint ? toeUp : toeDown;

        KnobSet k = discrete; // takes stutter, seqPlay, seqMult and seqType
        k.sampleRate = lerp(toeUp.sampleRate, toeDown.sampleRate, t);
        k.filter = lerp(toeUp.filter, toeDown.filter, t);
        k.bits = lerp(toeUp.bits, toeDown.bits, t);
        for (size_t i = 0; i < k.steps.size(); ++i)
            k.steps[i] = lerp(toeUp.steps[i], toeDown.steps[i], t);
        return k;
    }
} // namespace morph

inline KnobSet knobsOf(const Params& p)
{
    KnobSet k;
    k.sampleRate = p.sampleRate;
    k.filter = p.filter;
    k.bits = p.bits;
    k.stutter = p.stutter;
    k.seqPlay = p.seqPlay;
    k.seqMult = p.seqMult;
    k.steps = p.steps;
    k.seqType = p.seqType;
    return k;
}

// Overwrites the knob-controlled fields of p; tempo, switches and global settings are left alone.
inline void applyKnobs(Params& p, const KnobSet& k)
{
    p.sampleRate = k.sampleRate;
    p.filter = k.filter;
    p.bits = k.bits;
    p.stutter = k.stutter;
    p.seqPlay = k.seqPlay;
    p.seqMult = k.seqMult;
    p.steps = k.steps;
    p.seqType = k.seqType;
}
} // namespace bitvibe
