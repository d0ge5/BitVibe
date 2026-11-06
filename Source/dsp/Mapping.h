#pragma once

// Control-to-value mappings, taken from the hardware's published documentation wherever it
// states a value, and marked "ASSUMED" where the documentation is silent. Header-only and free of JUCE
// so the engine, the editor and the offline tests all share one definition.

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace bitvibe
{
// The sequence has up to sixteen steps; LENGTH says how many of them play (six, as on the pedal, unless changed).
inline constexpr int kNumSteps = 16;
inline constexpr int kDefaultSeqLength = 6;

// Besides what its main value controls (pitch, filter or sample rate), each step has a value in each of these lanes,
// which act on the sequenced sound while that step plays:
//   CRUSH   fewer bits than the BITS knob gives: 0 = as the knob, 1 = one bit
//   LEVEL   how loud the step is: 1 = as it is, 0 = silent
//   TREM    how much of the tremolo the step gets: 1 = as its panel says, 0 = none
//   DELAY   how much of the step goes into the delay: 1 = all of it, 0 = none
//   REVERB  how much of the step goes into the reverb: 1 = all of it, 0 = none
//   STUTTER on (above half) = the stutter fires as the step begins
// The neutral value of each lane changes nothing.
enum class Lane
{
    Crush = 0,
    Level,
    Trem,
    Delay,
    Reverb,
    Stutter
};
inline constexpr int kNumLanes = 6;
inline constexpr std::array<float, kNumLanes> kLaneNeutral { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f };
using LaneValues = std::array<std::array<float, kNumSteps>, kNumLanes>;

inline LaneValues neutralLanes()
{
    LaneValues v {};
    for (int l = 0; l < kNumLanes; ++l)
        v[(size_t) l].fill(kLaneNeutral[(size_t) l]);
    return v;
}

// Exact equality of two floats, written so that the compiler's -Wfloat-equal stays quiet where an exact
// comparison is what is meant (change detection, identical settings).
inline bool sameFloat(float a, float b) { return !(a < b) && !(b < a); }

// Manual, section 9: "very wide tempo range that extends from 23.4 BPM to 6000 BPM".
inline constexpr float kMinTempoBpm = 23.4f;
inline constexpr float kMaxTempoBpm = 6000.0f;     // what the engine can run (host sync, audio-rate sequencing)
inline constexpr float kMaxTempoKnobBpm = 240.0f;  // the TEMPO knob and the typed tempo stop here

// SWING at full stretches every other step to twice its partner (a 2:1 triplet feel); each pair of steps keeps the tempo.
inline constexpr float kMaxSwingRatio = 1.0f / 3.0f;
// The ring mod's frequency; at the bottom it is slow enough to hear as a wobble rather than a new tone.
inline constexpr float kRingMinHz = 1.0f, kRingMaxHz = 6000.0f;

// The phaser / flanger's sweep: one cycle takes this many beats of the tempo (4 bars down to a 32nd note), locked to
// the beat clock so the sweep lands with the music.
inline constexpr int kNumModRates = 8;
inline constexpr std::array<float, kNumModRates> kModRateBeats { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };
inline constexpr int kDefaultModRate = 2; // one bar

enum class SeqType
{
    Pitch = 0,
    Filter = 1,
    SampleRate = 2
};

// What the FILTER knob is a cutoff of. The RESONANCE knob applies to all of them.
//   Ladder    the four-pole ladder low-pass (24 dB / oct), the pedal's own
//   Low       a two-pole state-variable low-pass (12 dB / oct): cleaner, more like a synth
//   Band      the same filter's band-pass: only the sound around the cutoff is left
//   High      its high-pass: the sound above the cutoff is left, thin
//   Envelope  the ladder, opened up to three octaves by each picked note (an auto-wah); the knob is where it rests
//   Bloom     the ladder again, but each picked note pushes the RESONANCE up instead, relaxing as the note dies
//   Formant   three band-pass peaks at the formants of a vowel; the cutoff picks the vowel (U, O, A, E, I), so a
//             sweep or the filter steps make the sound talk
enum class FilterType
{
    Ladder = 0,
    Low,
    Band,
    High,
    Envelope,
    Bloom,
    Formant
};
inline constexpr int kNumFilterTypes = 7;

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

    // The band-pass and the high-pass would leave nothing at 24 kHz, so their knob stops at 8 kHz, and the last bit of
    // its travel switches them off (as it does the others, above 22 kHz).
    inline constexpr float kFilterBandMaxHz = 8000.0f;
    inline bool filterHasBandRange(FilterType t) { return t == FilterType::Band || t == FilterType::High; }
    inline float filterHzFor(FilterType t, float knob)
    {
        return filterHasBandRange(t) ? kFilterMinHz * std::pow(kFilterBandMaxHz / kFilterMinHz, knob) : filterHz(knob);
    }
    inline float filterOffAboveHz(FilterType t) { return filterHasBandRange(t) ? 0.975f * kFilterBandMaxHz : 22000.0f; }

    // The RESONANCE knob, 0..1. The ladder's feedback (4 would self-oscillate) and the state-variable filter's Q
    // (0.5, no peak, up to 20). The default is the feedback the ladder had before the knob existed.
    inline constexpr float kLadderMaxFeedback = 3.8f;
    inline constexpr float kDefaultFilterRes = 1.4f / kLadderMaxFeedback;
    inline float ladderFeedback(float res) { return kLadderMaxFeedback * std::clamp(res, 0.0f, 1.0f); }
    inline float filterQ(float res) { return 0.5f * std::pow(10.0f, 1.6f * std::clamp(res, 0.0f, 1.0f)); }
    // How far the ENVELOPE type opens the cutoff, in octaves, at the peak of a picked note.
    inline constexpr float kEnvelopeOctaves = 3.0f;
    // Where the RES BLOOM type pushes the resonance at the peak of a picked note (just short of whistling).
    inline constexpr float kBloomMaxRes = 0.95f;

    // FORMANT: the vowel is chosen by the cutoff, on a log scale from U (the bottom of the knob) through O, A and E
    // to I (at kFormantTopHz); above that the knob runs on to off, as for the other types. Each vowel is three
    // formant frequencies (an adult voice, rounded), sounded at the gains in kFormantGains.
    inline constexpr float kFormantTopHz = 16000.0f;
    struct Vowel
    {
        float f1, f2, f3;
    };
    inline constexpr std::array<Vowel, 5> kVowels { { { 300.0f, 870.0f, 2240.0f },  // U
                                                      { 450.0f, 800.0f, 2830.0f },  // O
                                                      { 730.0f, 1090.0f, 2440.0f }, // A
                                                      { 530.0f, 1840.0f, 2480.0f }, // E
                                                      { 270.0f, 2290.0f, 3010.0f } } }; // I
    inline constexpr std::array<const char*, 5> kVowelNames { "U", "O", "A", "E", "I" };
    inline constexpr std::array<float, 3> kFormantGains { 1.0f, 0.63f, 0.4f };
    inline constexpr float kFormantMakeup = 1.6f; // the three narrow peaks pass less than a low-pass does

    // Where along the vowels a cutoff is: 0 = U .. 4 = I.
    inline float vowelPosition(float fc)
    {
        const float t = std::log(std::max(fc, kFilterMinHz) / kFilterMinHz) / std::log(kFormantTopHz / kFilterMinHz);
        return 4.0f * std::clamp(t, 0.0f, 1.0f);
    }
    // The formants at a position between vowels, glided on a log scale (as a voice moves between them).
    inline Vowel vowelAt(float position)
    {
        position = std::clamp(position, 0.0f, 4.0f);
        const int i = std::min(3, (int) position);
        const float t = position - (float) i;
        auto glide = [t](float a, float b) { return a * std::pow(b / a, t); };
        const auto& a = kVowels[(size_t) i];
        const auto& b = kVowels[(size_t) i + 1];
        return { glide(a.f1, b.f1), glide(a.f2, b.f2), glide(a.f3, b.f3) };
    }
    // How sharp the formant peaks are: RESONANCE 0 = a breathy 3, 1 = a nasal 20.
    inline float formantQ(float res) { return 3.0f + 17.0f * std::clamp(res, 0.0f, 1.0f); }

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

    // The step knobs as they start: the first six as the pedal ships them, the rest unshifted (DRY), so lengthening
    // the sequence adds steps that play the note as it is.
    inline std::array<float, kNumSteps> defaultStepKnobs()
    {
        std::array<float, kNumSteps> s {};
        s.fill(pitchStepKnob(0));
        const int first[6] { -7, -3, -12, 7, 4, 0 };
        for (int i = 0; i < 6; ++i)
            s[(size_t) i] = pitchStepKnob(first[i]);
        return s;
    }

    // The first steps as given, the rest unshifted.
    inline std::array<float, kNumSteps> stepKnobs(std::initializer_list<float> first)
    {
        std::array<float, kNumSteps> s {};
        s.fill(pitchStepKnob(0));
        size_t i = 0;
        for (float v : first)
            if (i < s.size())
                s[i++] = v;
        return s;
    }

    // Each step of the sequence follows the SEQ TYPE (-1) unless given a type of its own.
    inline std::array<int, kNumSteps> followSeqType()
    {
        std::array<int, kNumSteps> t {};
        t.fill(-1);
        return t;
    }
} // namespace mapping
} // namespace bitvibe
