#pragma once

// Control-to-value mappings, taken from the hardware's published documentation wherever it
// states a value, and marked "ASSUMED" where the documentation is silent. Header-only and free of JUCE
// so the engine, the editor and the offline tests all share one definition.

#include <algorithm>
#include <array>
#include <cmath>

namespace bitvibe
{
inline constexpr int kNumSteps = 6;

// Exact equality of two floats, written so that the compiler's -Wfloat-equal stays quiet where an exact
// comparison is what is meant (change detection, identical settings).
inline bool sameFloat(float a, float b) { return !(a < b) && !(b < a); }

// Manual, section 9: "very wide tempo range that extends from 23.4 BPM to 6000 BPM".
inline constexpr float kMinTempoBpm = 23.4f;
inline constexpr float kMaxTempoBpm = 6000.0f;     // what the engine can run (host sync, audio-rate sequencing)
inline constexpr float kMaxTempoKnobBpm = 240.0f;  // the TEMPO knob and the typed tempo stop here

enum class SeqType
{
    Pitch = 0,
    Filter = 1,
    SampleRate = 2
};

namespace mapping
{
    // Manual, section 1.
    inline constexpr float kSampleRateMinHz = 48.0f;
    inline constexpr float kSampleRateMaxHz = 48000.0f;
    inline constexpr float kFilterMinHz = 96.0f;
    inline constexpr float kFilterMaxHz = 24000.0f;
    inline constexpr float kMinBits = 1.0f;
    inline constexpr float kMaxBits = 24.0f;

    // ASSUMED: the manual gives the end points only. Exponential (musical) tapers for the two
    // frequency controls, linear for bit depth.
    inline float sampleRateHz(float knob)
    {
        return kSampleRateMinHz * std::pow(kSampleRateMaxHz / kSampleRateMinHz, knob);
    }

    inline float filterHz(float knob)
    {
        return kFilterMinHz * std::pow(kFilterMaxHz / kFilterMinHz, knob);
    }

    inline float bitDepth(float knob) { return kMinBits + (kMaxBits - kMinBits) * knob; }

    // ---- Stutter (manual, section 3) -----------------------------------------------------
    // 23 positions: off, 3 speed ranges x 7 repeat counts, then random.
    inline constexpr int kStutterPositions = 23;
    inline constexpr std::array<int, 7> kStutterRepeats { 1, 2, 3, 4, 6, 8, 16 };
    inline constexpr std::array<float, 3> kStutterSpeeds { 1.0f, 2.0f, 0.5f }; // full, double, half

    struct StutterMode
    {
        bool off = true;
        bool random = false;
        int repeats = 0;
        float speed = 1.0f;
    };

    inline StutterMode stutterMode(int position)
    {
        position = std::clamp(position, 0, kStutterPositions - 1);
        StutterMode m;
        if (position == 0)
            return m;

        m.off = false;
        if (position == kStutterPositions - 1)
        {
            m.random = true;
            return m;
        }

        const int index = position - 1;
        m.speed = kStutterSpeeds[(size_t) (index / 7)];
        m.repeats = kStutterRepeats[(size_t) (index % 7)];
        return m;
    }

    // ---- Sequencer ----------------------------------------------------------------------
    // Manual, section 1: Play Once / Twice / 4 Times / 8 Times / Continuously.
    // ASSUMED: one extra position at the minimum that switches the sequencer off (a published
    // review says turning the stutter and sequencer knobs to zero isolates the filter/crusher).
    inline constexpr int kSeqPlayPositions = 6;

    // Returns the number of passes through the sequence: 0 = off, -1 = continuous.
    inline int seqRepeats(int position)
    {
        static constexpr std::array<int, kSeqPlayPositions> table { 0, 1, 2, 4, 8, -1 };
        return table[(size_t) std::clamp(position, 0, kSeqPlayPositions - 1)];
    }

    // A published review: the multiplier "doubles the rate across eight steps from 1x to 128x".
    inline constexpr int kSeqMultPositions = 8;

    inline int seqMultiplier(int position)
    {
        return 1 << std::clamp(position, 0, kSeqMultPositions - 1);
    }

    struct Step
    {
        enum class Kind { Skip, Mute, Active };
        Kind kind = Kind::Active;
        float semitones = 0.0f; // Pitch mode
        float factor = 1.0f;    // Filter / Sample-rate mode: multiplies the front-panel setting
    };

    // Manual, section 5. Knob at minimum = skip, at maximum = mute.
    //  Pitch:  27 zones -> skip, 24 semitones (-12..+12 without 0), dry (centre), mute.
    //  Filter / Sample rate: continuous 0..100% of the front-panel value.
    inline Step decodeStep(float knob, SeqType type)
    {
        Step s;
        if (type == SeqType::Pitch)
        {
            const int zone = std::clamp((int) (knob * 27.0f), 0, 26);
            if (zone == 0)
                s.kind = Step::Kind::Skip;
            else if (zone == 26)
                s.kind = Step::Kind::Mute;
            else
                s.semitones = (float) (zone - 13);
            return s;
        }

        // ASSUMED: small dead zones at the ends of the travel for skip and mute.
        constexpr float kEnd = 0.03f;
        if (knob < kEnd)
            s.kind = Step::Kind::Skip;
        else if (knob > 1.0f - kEnd)
            s.kind = Step::Kind::Mute;
        else
            s.factor = (knob - kEnd) / (1.0f - 2.0f * kEnd); // noon = 50%
        return s;
    }

    // Knob position (0..1) that lands in the middle of the zone for a given semitone shift.
    inline float pitchStepKnob(int semitones)
    {
        return ((float) (std::clamp(semitones, -12, 12) + 13) + 0.5f) / 27.0f;
    }
} // namespace mapping
} // namespace bitvibe
