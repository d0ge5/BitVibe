#pragma once

// The sixteen preset slots as they ship. Each slot is a complete sound: the twelve knobs, the tempo,
// the stutter length and the expression-morph set-up (the toe-up / toe-down knob sets and whether the
// morph is on; the expression position itself is a performance control and is not stored). Slot 1 is the sound the plug-in has always started with.
// Header-only and free of JUCE so the factory data can be checked by the offline tests.

#include "Morph.h"

namespace bitvibe
{
inline constexpr int kNumPresets = 16;

struct FactoryPreset
{
    const char* name = "Init";
    KnobSet knobs;
    float tempoBpm = 120.0f;
    int stutterDivision = 1; // 0 = 1/4 note, 1 = 1/8, 2 = 1/16
    bool morphOn = false;
    KnobSet toeUp, toeDown; // only heard while morphOn
};

namespace presets
{
    // Everything off: a clean signal, ready to be overwritten.
    inline KnobSet clean()
    {
        KnobSet k;
        k.seqPlay = 0;
        return k;
    }

    inline std::array<float, kNumSteps> pitchSteps(int a, int b, int c, int d, int e, int f)
    {
        return { mapping::pitchStepKnob(a), mapping::pitchStepKnob(b), mapping::pitchStepKnob(c),
                 mapping::pitchStepKnob(d), mapping::pitchStepKnob(e), mapping::pitchStepKnob(f) };
    }
} // namespace presets

// slot is 0-based; out-of-range slots give the last one.
inline FactoryPreset factoryPreset(int slot)
{
    using namespace presets;
    FactoryPreset f;
    f.knobs = clean();
    f.name = "Init";

    switch (std::clamp(slot, 0, kNumPresets - 1))
    {
        case 0: // the default sound: pitch sequencer running
            f.name = "Pitch Sequence";
            f.knobs = KnobSet {};
            break;
        case 1:
            f.name = "Clean";
            break;
        case 2:
            f.name = "Lo-Fi Radio";
            f.knobs.sampleRate = 0.62f;
            f.knobs.bits = 0.45f;
            f.knobs.filter = 0.55f;
            break;
        case 3:
            f.name = "Crunch";
            f.knobs.sampleRate = 0.85f;
            f.knobs.bits = 0.18f;
            f.knobs.filter = 0.9f;
            break;
        case 4:
            f.name = "Stutter x4";
            f.knobs.stutter = 4; // full speed, four repeats
            break;
        case 5:
            f.name = "Double Stutter";
            f.knobs.stutter = 11; // double speed, four repeats
            break;
        case 6:
            f.name = "Octave Hop";
            f.knobs.seqPlay = 5;
            f.knobs.seqMult = 2;
            f.knobs.steps = pitchSteps(0, 12, 0, 12, -12, 12);
            break;
        case 7:
            f.name = "Filter Sweeps";
            f.knobs.seqPlay = 5;
            f.knobs.seqMult = 1;
            f.knobs.seqType = SeqType::Filter;
            f.knobs.filter = 0.8f;
            f.knobs.steps = { 0.93f, 0.15f, 0.45f, 0.80f, 0.25f, 0.60f };
            break;
        case 8:
            f.name = "Rate Hops";
            f.knobs.seqPlay = 5;
            f.knobs.seqMult = 1;
            f.knobs.seqType = SeqType::SampleRate;
            f.knobs.sampleRate = 0.7f;
            f.knobs.steps = { 0.95f, 0.30f, 0.65f, 0.10f, 0.80f, 0.45f };
            break;
        case 9:
            f.name = "Ring Mod";
            f.tempoBpm = 6000.0f;
            f.knobs.seqPlay = 5;
            f.knobs.seqMult = 7;
            f.knobs.steps = { 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f }; // dry / mute alternating
            break;
        case 10: // morph: clean at toe up, heavily crushed with stutter at toe down
            f.name = "Morph Clean-Crush";
            f.morphOn = true;
            f.toeUp = clean();
            f.toeDown = clean();
            f.toeDown.sampleRate = 0.45f;
            f.toeDown.bits = 0.12f;
            f.toeDown.filter = 0.6f;
            f.toeDown.stutter = 11;
            f.knobs = f.toeUp;
            break;
        case 11: // morph: two different pitch patterns
            f.name = "Morph Pitch Pair";
            f.morphOn = true;
            f.toeUp = KnobSet {};
            f.toeDown = KnobSet {};
            f.toeDown.seqMult = 3;
            f.toeDown.steps = pitchSteps(12, 7, 12, 5, 0, -12);
            f.knobs = f.toeUp;
            break;
        default: // slots 13 to 16: clean and unnamed, ready to be overwritten
        {
            static constexpr const char* names[] = { "Init 13", "Init 14", "Init 15", "Init 16" };
            f.name = names[std::clamp(slot, 12, kNumPresets - 1) - 12];
            break;
        }
    }

    if (!f.morphOn)
        f.toeUp = f.toeDown = f.knobs;
    return f;
}
} // namespace bitvibe
