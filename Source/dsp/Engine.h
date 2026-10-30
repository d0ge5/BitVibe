#pragma once

// BitVibe engine: stereo-capable, header-only, no JUCE dependency.
//
// Signal chain (per channel):   in -> Stutter -> Pitch (sequencer) -> Sample-rate reducer
//                                  -> Bit-depth reducer (+ gate) -> Ladder filter -> Mute -> out
// One shared controller runs the pick-attack detector, the 6-step sequencer, tempo and the
// stutter triggers, so both channels always stay in lock-step.
//
// ASSUMED (the published documentation does not specify): the order of the blocks in the chain, the fixed ladder
// resonance, the stutter snippet length (a note division of the tempo) and the detector design.

#include "Components.h"
#include "Mapping.h"
#include "Reverb.h"

#include <array>
#include <atomic>

namespace bitvibe
{
// Triggers that come from outside the input audio (the drum map). Pick attacks detected on the input
// are the other source; a pick attack does RestartAndStutter.
enum class TriggerAction
{
    RestartSequence,   // start the sequence again from its first step
    NextStep,          // move to the next step (starts the sequence if it is not running)
    Stutter,           // fire the stutter as set on the Stutter knob
    RestartAndStutter, // both, like a pick attack
};

struct TriggerEvent
{
    int offset = 0; // sample within the block
    TriggerAction action = TriggerAction::RestartSequence;
};

struct Params
{
    // Front panel. Knobs are normalised 0..1; the discrete ones are integer positions.
    float sampleRate = 1.0f;
    float filter = 1.0f;
    float bits = 1.0f;
    int stutter = 0;   // 0..22
    int seqPlay = 5;   // 0..5  (off, once, twice, 4, 8, continuous)
    int seqMult = 1;   // 0..7  (x1 .. x128)

    // Second layer (Alt): the six sequencer step knobs, normalised 0..1.
    std::array<float, kNumSteps> steps {
        mapping::pitchStepKnob(-7), mapping::pitchStepKnob(-3), mapping::pitchStepKnob(-12),
        mapping::pitchStepKnob(7),  mapping::pitchStepKnob(4),  mapping::pitchStepKnob(0)
    };
    SeqType seqType = SeqType::Pitch;

    float tempoBpm = 120.0f;
    bool bypass = false;
    bool stutterHold = false;

    // Global settings (extras that suit a plugin; the hardware has equivalents in its global menu).
    float inputGainDb = 0.0f;
    float outputGainDb = 0.0f;
    bool lineHeadroom = false;       // false = instrument level, true = line/synth level
    float triggerSensitivity = 0.5f; // attack detector
    int stutterDivision = 1;         // snippet length: 0 = 1/4 note, 1 = 1/8, 2 = 1/16

    // Blend between the clean input (0) and the effect (1). The clean signal is the input as it
    // arrives, before the input gain; the output gain applies to the blend.
    float mix = 1.0f;

    // When on, the sample-rate, bit-depth and filter stage only acts while the stutter or the
    // sequencer is running (a sequencer set to "continuous" is always running); otherwise the
    // signal passes that stage untouched.
    bool fxOnlyOnTriggers = false;

    // Reverb on the sequenced / stuttered sound only (never on the clean signal that MIX blends back in).
    // reverbMix = how much reverb is added (0 = none, the default); tone 0 = dark .. 1 = bright; level trims the reverb's output.
    float reverbMix = 0.0f;
    float reverbTone = 0.5f;
    float reverbLevelDb = 0.0f;
    int reverbType = 0;                // ReverbType: room, hall or spring
    float reverbLowCutHz = 20.0f;      // high-pass on the reverb only (20 Hz = off)
    float reverbHighCutHz = 20000.0f;  // low-pass on the reverb only (20 kHz = off)

    // Pick attacks on the input start the stutter and the sequence (off: only trigger events do).
    bool pickTriggers = true;
    // Steps move only on NextStep trigger events instead of with the tempo.
    bool stepsOnHits = false;
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        detector.prepare(fs);

        const int snippetCapacity = (int) (fs * 60.0 / kMinTempoBpm) + 64;
        for (auto& ch : channels)
        {
            ch.stutter.prepare(fs, snippetCapacity);
            ch.pitch.prepare(fs);
        }
        for (int c = 0; c < kMaxChannels; ++c)
            channels[(size_t) c].reverb.prepare(fs, c);

        srKnobSm.setTime(fs, 10.0f);
        filterKnobSm.setTime(fs, 10.0f);
        bitsKnobSm.setTime(fs, 10.0f);
        pitchAmountSm.setTime(fs, 4.0f);
        cutoffLogSm.setTime(fs, 3.0f);
        filterMixSm.setTime(fs, 5.0f);
        bypassSm.setTime(fs, 10.0f);
        inGainSm.setTime(fs, 20.0f);
        outGainSm.setTime(fs, 20.0f);
        mixSm.setTime(fs, 10.0f);
        reverbMixSm.setTime(fs, 15.0f);
        reverbToneSm.setTime(fs, 15.0f);
        reverbLevelSm.setTime(fs, 15.0f);
        reverbLowSm.setTime(fs, 15.0f);
        reverbHighSm.setTime(fs, 15.0f);
        fxAttack = 1.0f - std::exp(-1.0f / (float) (fs * 0.002));
        fxRelease = 1.0f - std::exp(-1.0f / (float) (fs * 0.025));

        gateAttack = 1.0f - std::exp(-1.0f / (float) (fs * 0.001));
        gateRelease = 1.0f - std::exp(-1.0f / (float) (fs * 0.030));
        gateDecay = std::exp(-1.0f / (float) (fs * 0.100));
        reset();
    }

    void reset()
    {
        detector.reset();
        for (auto& ch : channels)
        {
            ch.stutter.reset();
            ch.pitch.reset();
            ch.ladder.reset();
            ch.reverb.reset();
            ch.held = 0.0f;
        }
        primed = false;
        seqRunning = false;
        seqIdx = seqCycles = 0;
        seqPhase = 0.0;
        srPhase = 1.0;
        gateEnv = 0.0f;
        gateOpen = true;
        gateGain = 1.0f;
        muteGain = 1.0f;
        holdPrev = false;
        beatPhase = 0.0;
        fxMix = 1.0f;
        fxInit = false;
        reverbSend = 0.0f;
    }

    // Processes in place. io points at numChannels channel buffers (1 or 2). events (sorted by offset)
    // are applied at their sample; offsets before the block count as 0, those after it run at its end.
    void process(float* const* io, int numChannels, int numSamples, const Params& p,
                 const TriggerEvent* events = nullptr, int numEvents = 0)
    {
        const int nCh = std::clamp(numChannels, 0, kMaxChannels);
        if (nCh == 0 || numSamples <= 0)
            return;

        beginBlock(p);
        int nextEvent = 0;

        for (int n = 0; n < numSamples; ++n)
        {
            while (nextEvent < numEvents && events[nextEvent].offset <= n)
                applyTrigger(events[nextEvent++].action);

            // ---- input stage -------------------------------------------------------------
            const float inGain = inGainSm.next(inGainTarget);
            std::array<float, kMaxChannels> dry {}, x {};
            for (int c = 0; c < nCh; ++c)
            {
                dry[(size_t) c] = io[c][n];
                x[(size_t) c] = std::clamp(dry[(size_t) c] * inGain * headroomScale, -1.0f, 1.0f);
            }
            const float mono = nCh == 2 ? 0.5f * (x[0] + x[1]) : x[0];

            // ---- control: attack, sequencer ---------------------------------------------
            if (detector.process(mono) && blk.pickTriggers)
                onAttack();
            advanceSequencer();

            // The crush / sample-rate / filter stage: always on, or only while a trigger effect plays.
            const float fxTarget = (!blk.fxOnlyOnTriggers || seqRunning || channels[0].stutter.isActive()) ? 1.0f : 0.0f;
            if (!fxInit)
            {
                fxMix = fxTarget;
                fxInit = true;
            }
            fxMix += (fxTarget - fxMix) * (fxTarget > fxMix ? fxAttack : fxRelease);
            if (std::abs(fxTarget - fxMix) < 1.0e-5f)
                fxMix = fxTarget;

            float semitones = 0.0f, filterFactor = 1.0f, srFactor = 1.0f;
            bool mute = false;
            if (seqRunning)
            {
                const auto& st = steps[(size_t) active[(size_t) seqIdx]];
                if (st.kind == mapping::Step::Kind::Mute)
                    mute = true;
                else if (seqType == SeqType::Pitch)
                    semitones = st.semitones;
                else if (seqType == SeqType::Filter)
                    filterFactor = st.factor;
                else
                    srFactor = st.factor;
            }

            // ---- derive per-sample processing values -------------------------------------
            // Sample rate
            const float srBase = mapping::sampleRateHz(srKnobSm.next(blk.sampleRate));
            const float srEff = std::clamp(srBase * srFactor, mapping::kSampleRateMinHz,
                                           mapping::kSampleRateMaxHz);
            const bool srBypass = srEff >= 47000.0f;
            bool srTick = true;
            if (!srBypass)
            {
                srPhase += (double) srEff / fs;
                srTick = srPhase >= 1.0;
                if (srTick)
                    srPhase -= std::floor(srPhase);
            }

            // Bit depth (+ the gate that the hardware engages below 8 bits)
            const float bitsF = mapping::bitDepth(bitsKnobSm.next(blk.bits));
            const bool bitsBypass = bitsF >= 23.5f;
            const float levels = std::exp2(bitsF - 1.0f);
            updateGate(mono, bitsF);

            // Filter
            const float fcTarget = std::clamp(mapping::filterHz(filterKnobSm.next(blk.filter)) * filterFactor,
                                              24.0f, mapping::kFilterMaxHz);
            const float fc = std::exp2(cutoffLogSm.next(std::log2(fcTarget)));
            const float filterMix = filterMixSm.next(fc >= 22000.0f ? 0.0f : 1.0f);
            const float g = std::tan(kPi * std::min(fc, 0.45f * (float) fs) / (float) fs);
            const float G = g / (1.0f + g);

            // Pitch
            const float ratio = std::exp2(semitones / 12.0f);
            const float pitchAmount = pitchAmountSm.next(semitones != 0.0f ? 1.0f : 0.0f);

            // Mute
            muteGain += muteCoef * ((mute ? 0.0f : 1.0f) - muteGain);
            if (std::abs(muteGain - (mute ? 0.0f : 1.0f)) < 1.0e-5f)
                muteGain = mute ? 0.0f : 1.0f;

            const float outGain = outGainSm.next(outGainTarget);
            const float mixAmount = mixSm.next(blk.mix);
            const float revMix = reverbMixSm.next(blk.reverbMix);
            const float revTone = reverbToneSm.next(blk.reverbTone);
            const float revLevel = reverbLevelSm.next(blk.reverbLevel);
            ReverbSettings revSettings;
            revSettings.type = (ReverbType) blk.reverbType;
            revSettings.tone = revTone;
            revSettings.lowCutHz = reverbLowSm.next(blk.reverbLowCut);
            revSettings.highCutHz = reverbHighSm.next(blk.reverbHighCut);
            // The reverb only hears the sound while a sequence or stutter is playing; its tail rings on afterwards.
            const float sendTarget = (seqRunning || channels[0].stutter.isActive()) ? 1.0f : 0.0f;
            reverbSend += (sendTarget - reverbSend) * (sendTarget > reverbSend ? fxAttack : fxRelease);
            const float bypassMix = bypassSm.next(blk.bypass ? 0.0f : 1.0f);

            // ---- per-channel audio path --------------------------------------------------
            for (int c = 0; c < nCh; ++c)
            {
                auto& ch = channels[(size_t) c];
                float y = ch.stutter.process(x[(size_t) c]);
                y = ch.pitch.process(y, ratio, pitchAmount);
                const float beforeFx = y;

                if (srTick)
                    ch.held = y;
                if (!srBypass)
                    y = ch.held;

                if (!bitsBypass)
                {
                    const float clipped = std::min(y, 1.0f - 1.0e-6f);
                    y = (std::floor(clipped * levels) + 0.5f) / levels * gateGain;
                }

                const float filtered = ch.ladder.process(y, G);
                y = lerpExact(y, filtered, filterMix);
                y = lerpExact(beforeFx, y, fxMix);
                const float reverbOut = ch.reverb.process(y * reverbSend, revSettings);
                y *= muteGain;
                y *= invHeadroom;
                y += reverbOut * (revMix * revLevel * invHeadroom); // added to the sequenced sound, which stays as it is

                if (!std::isfinite(y))
                {
                    y = 0.0f;
                    ch.ladder.reset();
                }

                y = lerpExact(dry[(size_t) c], y, mixAmount);
                y *= outGain;

                io[c][n] = lerpExact(dry[(size_t) c], y, bypassMix);
            }

            beatPhase += 1.0 / beatSamples;
            if (beatPhase >= 1.0)
                beatPhase -= std::floor(beatPhase);
        }

        while (nextEvent < numEvents)
            applyTrigger(events[nextEvent++].action);

        beatPhaseOut.store((float) beatPhase, std::memory_order_relaxed);
        stepOut.store(seqRunning ? active[(size_t) seqIdx] : -1, std::memory_order_relaxed);
        stutterOut.store(channels[0].stutter.isActive(), std::memory_order_relaxed);
    }

    // Read by the editor (relaxed atomics; purely cosmetic).
    float getBeatPhase() const { return beatPhaseOut.load(std::memory_order_relaxed); }
    int getCurrentStep() const { return stepOut.load(std::memory_order_relaxed); }
    bool isStuttering() const { return stutterOut.load(std::memory_order_relaxed); }

private:
    struct Channel
    {
        StutterChannel stutter;
        PitchShifter pitch;
        LadderFilter ladder;
        Reverb reverb;
        float held = 0.0f;
    };

    // Values that stay constant for the duration of one block.
    struct Block
    {
        float sampleRate = 1.0f, filter = 1.0f, bits = 1.0f;
        mapping::StutterMode stutter;
        bool bypass = false;
        bool fxOnlyOnTriggers = false;
        bool pickTriggers = true, stepsOnHits = false;
        float mix = 1.0f;
        float reverbMix = 0.0f, reverbTone = 0.5f, reverbLevel = 1.0f, reverbLowCut = 20.0f, reverbHighCut = 20000.0f;
        int reverbType = 0;
        int stutterLength = 0;
    };

    void beginBlock(const Params& p)
    {
        blk.sampleRate = p.sampleRate;
        blk.filter = p.filter;
        blk.bits = p.bits;
        blk.bypass = p.bypass;
        blk.fxOnlyOnTriggers = p.fxOnlyOnTriggers;
        blk.pickTriggers = p.pickTriggers;
        blk.stepsOnHits = p.stepsOnHits;
        blk.mix = std::clamp(p.mix, 0.0f, 1.0f);
        blk.reverbMix = std::clamp(p.reverbMix, 0.0f, 1.0f);
        blk.reverbTone = std::clamp(p.reverbTone, 0.0f, 1.0f);
        blk.reverbLevel = dbToGain(std::clamp(p.reverbLevelDb, -60.0f, 12.0f));
        blk.reverbType = std::clamp(p.reverbType, 0, kNumReverbTypes - 1);
        blk.reverbLowCut = std::clamp(p.reverbLowCutHz, 20.0f, 2000.0f);
        blk.reverbHighCut = std::clamp(p.reverbHighCutHz, 200.0f, 20000.0f);
        blk.stutter = mapping::stutterMode(p.stutter);

        if (!primed)
        {
            srKnobSm.snap(p.sampleRate);
            filterKnobSm.snap(p.filter);
            bitsKnobSm.snap(p.bits);
            const float fc = mapping::filterHz(p.filter);
            cutoffLogSm.snap(std::log2(fc));
            filterMixSm.snap(fc >= 22000.0f ? 0.0f : 1.0f);
            bypassSm.snap(p.bypass ? 0.0f : 1.0f);
            inGainSm.snap(dbToGain(p.inputGainDb));
            outGainSm.snap(dbToGain(p.outputGainDb));
            mixSm.snap(blk.mix);
            reverbMixSm.snap(blk.reverbMix);
            reverbToneSm.snap(blk.reverbTone);
            reverbLevelSm.snap(blk.reverbLevel);
            reverbLowSm.snap(blk.reverbLowCut);
            reverbHighSm.snap(blk.reverbHighCut);
            primed = true;
        }

        inGainTarget = dbToGain(p.inputGainDb);
        outGainTarget = dbToGain(p.outputGainDb);
        // Line level leaves ~3.5 dB more headroom before the converters clip (manual, specs).
        headroomScale = p.lineHeadroom ? 0.668f : 1.0f;
        invHeadroom = 1.0f / headroomScale;
        detector.setSensitivity(p.triggerSensitivity);

        const float bpm = std::clamp(p.tempoBpm, kMinTempoBpm, kMaxTempoBpm);
        beatSamples = fs * 60.0 / (double) bpm;
        stepLength = std::max(1.0, beatSamples / (double) mapping::seqMultiplier(p.seqMult));

        // Mute gets a short ramp; keep it proportionally shorter when the sequencer is in the audio band.
        const float ramp = (float) std::clamp(stepLength * 0.05, 1.0, 0.001 * fs);
        muteCoef = 1.0f - std::exp(-1.0f / ramp);

        seqType = p.seqType;
        seqRepeats = mapping::seqRepeats(p.seqPlay);
        numActive = 0;
        for (int i = 0; i < kNumSteps; ++i)
        {
            steps[(size_t) i] = mapping::decodeStep(p.steps[(size_t) i], p.seqType);
            if (steps[(size_t) i].kind != mapping::Step::Kind::Skip)
                active[(size_t) numActive++] = i;
        }

        if (seqRepeats == 0 || numActive == 0)
            seqRunning = false;
        else if (seqRepeats < 0 && !seqRunning)
            startSequence();
        if (seqIdx >= numActive)
            seqIdx = 0;

        static constexpr float kDivision[3] { 1.0f, 0.5f, 0.25f };
        const int div = std::clamp(p.stutterDivision, 0, 2);
        blk.stutterLength = (int) (beatSamples * kDivision[div]);

        if (p.stutterHold != holdPrev)
        {
            for (auto& ch : channels)
            {
                if (p.stutterHold)
                    ch.stutter.engageHold();
                else
                    ch.stutter.releaseHold();
            }
            holdPrev = p.stutterHold;
        }
        holdNow = p.stutterHold;
    }

    void onAttack() { applyTrigger(TriggerAction::RestartAndStutter); }

    void applyTrigger(TriggerAction action)
    {
        switch (action)
        {
            case TriggerAction::RestartSequence: restartSequence(); break;
            case TriggerAction::NextStep: nextStep(); break;
            case TriggerAction::Stutter: fireStutter(); break;
            case TriggerAction::RestartAndStutter:
                fireStutter();
                restartSequence();
                break;
        }
    }

    void fireStutter()
    {
        if (!blk.stutter.off && !channels[0].stutter.isHeld())
        {
            float speed = blk.stutter.speed;
            int repeats = blk.stutter.repeats;
            bool reversed = false;
            if (blk.stutter.random)
            {
                const uint32_t r = nextRandom();
                const size_t idx = (size_t) (r % 21u);
                speed = mapping::kStutterSpeeds[idx / 7];
                repeats = mapping::kStutterRepeats[idx % 7];
                reversed = ((r >> 16) & 1u) != 0;
            }
            const int preRoll = (int) (0.008 * fs);
            for (int c = 0; c < kMaxChannels; ++c)
                channels[(size_t) c].stutter.trigger(blk.stutterLength, preRoll, speed, repeats, reversed, holdNow);
        }
    }

    void restartSequence()
    {
        if (seqRepeats != 0 && numActive > 0)
            startSequence();
    }

    // One step on: starts the sequence when it is idle, otherwise moves on and restarts the step timer.
    void nextStep()
    {
        if (seqRepeats == 0 || numActive == 0)
            return;
        if (!seqRunning)
        {
            startSequence();
            return;
        }
        seqPhase = 0.0;
        stepForward();
    }

    void startSequence()
    {
        seqRunning = true;
        seqIdx = 0;
        seqCycles = 0;
        seqPhase = 0.0;
    }

    // Moves to the next active step; counts passes and stops after the last one. False when stopped.
    bool stepForward()
    {
        if (++seqIdx >= numActive)
        {
            seqIdx = 0;
            ++seqCycles;
            if (seqRepeats > 0 && seqCycles >= seqRepeats)
            {
                seqRunning = false;
                return false;
            }
        }
        return true;
    }

    void advanceSequencer()
    {
        if (!seqRunning || blk.stepsOnHits)
            return;

        seqPhase += 1.0;
        for (int guard = 0; seqPhase >= stepLength && guard < 8; ++guard)
        {
            seqPhase -= stepLength;
            if (!stepForward())
                break;
        }
    }

    void updateGate(float mono, float bitsF)
    {
        float target = 1.0f;
        if (bitsF < 8.0f)
        {
            // Manual/review: a noise gate engages below 8 bits. Threshold rises as the word shrinks.
            const float thrDb = -72.0f + (8.0f - bitsF) / 7.0f * 32.0f;
            const float thr = std::pow(10.0f, thrDb / 20.0f);
            gateEnv = std::max(std::abs(mono), gateEnv * gateDecay);
            if (!gateOpen && gateEnv > thr)
                gateOpen = true;
            else if (gateOpen && gateEnv < thr * 0.7f)
                gateOpen = false;
            target = gateOpen ? 1.0f : 0.0f;
        }
        else
        {
            gateOpen = true;
        }
        gateGain += (target - gateGain) * (target > gateGain ? gateAttack : gateRelease);
        if (std::abs(gateGain - target) < 1.0e-5f)
            gateGain = target;
    }

    uint32_t nextRandom()
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    static float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

    double fs = 48000.0;
    std::array<Channel, kMaxChannels> channels;
    AttackDetector detector;
    Block blk;
    bool primed = false;

    // smoothers
    Smoother srKnobSm, filterKnobSm, bitsKnobSm, pitchAmountSm, cutoffLogSm, filterMixSm, bypassSm, inGainSm, outGainSm, mixSm, reverbMixSm, reverbToneSm, reverbLevelSm, reverbLowSm, reverbHighSm;
    float inGainTarget = 1.0f, outGainTarget = 1.0f, headroomScale = 1.0f, invHeadroom = 1.0f;

    // crush / sample-rate / filter stage on-off blend (see Params::fxOnlyOnTriggers)
    float fxMix = 1.0f, fxAttack = 1.0f, fxRelease = 1.0f;
    bool fxInit = false;
    float reverbSend = 0.0f; // 1 while a sequence or stutter plays (smoothed): the reverb hears only that

    // sequencer
    std::array<mapping::Step, kNumSteps> steps {};
    std::array<int, kNumSteps> active {};
    int numActive = 0, seqIdx = 0, seqCycles = 0, seqRepeats = 0;
    bool seqRunning = false;
    double seqPhase = 0.0, stepLength = 1.0, beatSamples = 24000.0, beatPhase = 0.0;
    SeqType seqType = SeqType::Pitch;
    float muteGain = 1.0f, muteCoef = 1.0f;

    // sample-rate reducer / gate / hold
    double srPhase = 1.0;
    float gateEnv = 0.0f, gateGain = 0.0f, gateAttack = 0.0f, gateRelease = 0.0f, gateDecay = 0.0f;
    bool gateOpen = false;
    bool holdPrev = false, holdNow = false;
    uint32_t rng = 0x9E3779B9u;

    std::atomic<float> beatPhaseOut { 0.0f };
    std::atomic<int> stepOut { -1 };
    std::atomic<bool> stutterOut { false };
};
} // namespace bitvibe
