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
#include "Distortion.h"
#include "EqReference.h"
#include "Equalizer.h"
#include "Fuzz.h"
#include "Mapping.h"
#include "Reverb.h"
#include "Samples.h"

#include <array>
#include <atomic>
#include <cstdint>

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
    // What each step controls: -1 = the sequencer type above, 0..2 = its own (pitch, filter, sample rate).
    std::array<int, kNumSteps> stepTypes { -1, -1, -1, -1, -1, -1 };

    // A loop section: steps loopFrom..loopTo (0..5, either order) play loopPlays times in a row (0 = forever) before
    // the sequence goes on to the steps after them. A restart still begins at step 1, so the steps before the
    // section play once each time the sequence restarts.
    bool loopOn = false;
    int loopFrom = 3, loopTo = 5;
    int loopPlays = 0;

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
    float reverbDecay = 0.5f;          // 0 = a quarter of the normal tail, 0.5 = normal, 1 = four times as long
    bool reverbReverse = false;        // the tail swells up after the sound instead of dying away (for the type chosen)
    float reverbLowCutHz = 20.0f;      // high-pass on the reverb only (20 Hz = off)
    float reverbHighCutHz = 20000.0f;  // low-pass on the reverb only (20 kHz = off)

    // Distortion on the sequenced sound (the sequence, its stutters and the random sounds; never the clean signal):
    // drive / tone / level, 0..1 each. distReverb also distorts the reverb. Off by default.
    bool distOn = false;
    int distType = 0; // 0 = overdrive, 1 = fuzz (drive = sustain, level = volume)
    float distDrive = 0.5f, distTone = 0.5f, distLevel = 0.8f;
    bool distReverb = false;

    // On / off switches (both on by default): with the reverb off nothing is fed to it and nothing is added; with the
    // sample blend off no random sounds start (the preview of the volume range still plays).
    bool reverbOn = true;
    bool sampleOn = true;

    // The EQ: the last thing the sequenced sound goes through, after the distortion and the reverb and before the
    // dry/wet blend (the clean signal never goes through it). Off by default.
    bool eqOn = false;
    float eqOutputDb = 0.0f;
    std::array<EqBand, kNumEqBands> eqBands = defaultEqBands();

    // Sample blend: at the start of every sequencer step (with this chance) one of the picked sounds, chosen at
    // random, is mixed into the sequenced sound. The volume is set by two levels and the MIX knob between them:
    // sampleMix 0 plays at sampleBaseDb, 1 at sampleMaxDb (a base at the bottom, -60 dB, means silence). The set is
    // owned by the caller and must stay alive while the engine may play from it (the plug-in keeps the previous set
    // until the next block).
    float sampleMix = 0.0f;
    float sampleBaseDb = -60.0f, sampleMaxDb = -9.0f;
    float sampleChance = 1.0f;
    const SampleSet* sampleSet = nullptr;
    // The steps of the loop section (see loopOn) can have sounds and volumes of their own: loopSampleSet (nullptr = the
    // same sounds as the other steps) and the four values below, which work like the ones above.
    const SampleSet* loopSampleSet = nullptr;
    float loopSampleMix = 0.0f;
    float loopSampleBaseDb = -60.0f, loopSampleMaxDb = -9.0f;
    float loopSampleChance = 1.0f;

    // Beat pattern: an action fired at the lit steps of a bar of sixteenth notes, from the plug-in's own tempo.
    // patternSteps has a bit per step (bit 0 = the first step of the bar) and patternBarSteps is the length of the
    // bar. hostPpq (quarter notes since the start of the host's timeline, or < 0 when not following the host) lines
    // the grid up with the host while it plays.
    bool patternOn = false;
    TriggerAction patternAction = TriggerAction::RestartSequence;
    std::uint32_t patternSteps = 0;
    int patternBarSteps = 16;
    double hostPpq = -1.0;
    // Pattern lock: only the beat pattern may restart or step the sequence. The level of the input (picked notes) and
    // the drum hits can still fire the stutter, but they no longer restart the sequence.
    bool patternLock = false;

    // Pick attacks on the input start the stutter and the sequence (off: only trigger events do).
    bool pickTriggers = true;
    // Steps move only on NextStep trigger events instead of with the tempo.
    bool stepsOnHits = false;
};

class Engine
{
public:
    static constexpr int kMaxChannels = 2;

    static constexpr float kSampleOffDb = -60.0f; // the bottom of the base and max volume knobs: silence

    // The sample blend's volume: the MIX knob moves between the base and the max volume (in dB, so the travel
    // sounds even). A volume at the bottom of its range is silence. The sounds are levelled to the same peak.
    static float sampleGain(float mix, float baseDb, float maxDb)
    {
        const float db = baseDb + (maxDb - baseDb) * std::clamp(mix, 0.0f, 1.0f);
        return db <= kSampleOffDb + 0.05f ? 0.0f : dbToGain(db);
    }

    // Plays one of the picked sounds (chosen by pick) at a volume of its own, outside the sequence: the preview the
    // player asks for while setting the base and max volumes. Call it on the audio thread before process(), with the
    // set the next block's Params bring (loopSection: the loop section's own set).
    void previewSample(const SampleSet* set, uint32_t pick, float gain, bool loopSection = false)
    {
        if (set == nullptr || set->clips.empty() || !(gain > 0.0f))
            return;
        auto& last = loopSection ? lastLoopSampleSet : lastSampleSet;
        if (set != last) // a new set: whatever is sounding still points into the old one
        {
            samplePlayer.reset();
            last = set;
        }
        samplePlayer.trigger(&set->clips[pick % set->clips.size()], gain, true);
    }

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        detector.prepare(fs);
        eqReference.prepare(fs);

        const int snippetCapacity = (int) (fs * 60.0 / kMinTempoBpm) + 64;
        for (auto& ch : channels)
        {
            ch.stutter.prepare(fs, snippetCapacity);
            ch.pitch.prepare(fs);
        }
        for (int c = 0; c < kMaxChannels; ++c)
            channels[(size_t) c].reverb.prepare(fs, c);
        samplePlayer.prepare(fs);
        for (auto& ch : channels)
        {
            ch.dist.prepare(fs);
            ch.distReverb.prepare(fs);
            ch.fuzz.prepare(fs);
            ch.fuzzReverb.prepare(fs);
            ch.eq.prepare(fs);
        }

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
        reverbDecaySm.setTime(fs, 40.0f);
        sampleMixSm.setTime(fs, 15.0f);
        loopSampleMixSm.setTime(fs, 15.0f);
        distOnSm.setTime(fs, 10.0f);
        reverbOnSm.setTime(fs, 15.0f);
        eqOnSm.setTime(fs, 10.0f);
        sampleOnSm.setTime(fs, 15.0f);
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
            ch.dist.reset();
            ch.distReverb.reset();
            ch.fuzz.reset();
            ch.fuzzReverb.reset();
            ch.eq.reset();
            ch.held = 0.0f;
        }
        primed = false;
        seqRunning = false;
        seqIdx = seqCycles = loopCount = 0;
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
        patternIdx = 0;
        patternPhase = 0.0;
        patternFresh = true;
        samplePlayer.reset();
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
        for (auto& ch : channels)
            ch.eq.setBands(p.eqBands, p.eqOutputDb, numSamples);
        eqReference.beginBlock(nCh);
        const bool audition = eqReference.isAuditioning(); // the captured clip plays through the EQ instead
        int nextEvent = 0;

        for (int n = 0; n < numSamples; ++n)
        {
            while (nextEvent < numEvents && events[nextEvent].offset <= n)
                applyExternalTrigger(events[nextEvent++].action);
            advancePattern();

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
            inputPeak = std::max(inputPeak, std::abs(mono));
            if (detector.process(mono))
            {
                ++attacksHeard; // shown on the input meter, whether or not picks are allowed to trigger
                if (blk.pickTriggers)
                    onAttack();
            }
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
                const auto type = stepType[(size_t) active[(size_t) seqIdx]];
                if (st.kind == mapping::Step::Kind::Mute)
                    mute = true;
                else if (type == SeqType::Pitch)
                    semitones = st.semitones;
                else if (type == SeqType::Filter)
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
            const float reverbOnGain = reverbOnSm.next(blk.reverbOn ? 1.0f : 0.0f);
            const float sampleOnGain = sampleOnSm.next(blk.sampleOn ? 1.0f : 0.0f);
            const float revMix = reverbMixSm.next(blk.reverbMix) * reverbOnGain;
            const float revTone = reverbToneSm.next(blk.reverbTone);
            const float revLevel = reverbLevelSm.next(blk.reverbLevel);
            // The random sounds belong to the sequence: they stop when it stops.
            if (!seqRunning)
                samplePlayer.release();
            if (!blk.sampleOn)
                samplePlayer.release(); // switched off: what is sounding fades out
            const float distAmount = distOnSm.next(blk.distOn ? 1.0f : 0.0f);
            const bool distRun = blk.distOn || distAmount > 1.0e-4f;
            const float eqAmount = eqOnSm.next(blk.eqOn ? 1.0f : 0.0f);
            const bool eqRun = blk.eqOn || eqAmount > 1.0e-4f;
            const float distGate = distAmount * reverbSend; // the distortion only touches the sound while a sequence or stutter plays
            const auto heard = samplePlayer.process();
            const float sampleSignal = (heard.sequence * sampleMixSm.next(blk.sampleGain) + heard.loopSequence * loopSampleMixSm.next(blk.loopSampleGain))
                                           * sampleOnGain + heard.preview;
            ReverbSettings revSettings;
            revSettings.type = (ReverbType) blk.reverbType;
            revSettings.tone = revTone;
            revSettings.decay = reverbDecaySm.next(blk.reverbDecay);
            revSettings.reverse = blk.reverbReverse;
            revSettings.lowCutHz = reverbLowSm.next(blk.reverbLowCut);
            revSettings.highCutHz = reverbHighSm.next(blk.reverbHighCut);
            // The reverb only hears the sound while a sequence or stutter is playing; its tail rings on afterwards.
            const float sendTarget = (seqRunning || channels[0].stutter.isActive()) ? 1.0f : 0.0f;
            reverbSend += (sendTarget - reverbSend) * (sendTarget > reverbSend ? fxAttack : fxRelease);
            const float bypassMix = bypassSm.next(blk.bypass ? 0.0f : 1.0f);

            // ---- per-channel audio path --------------------------------------------------
            float beforeEqSum = 0.0f, afterEqSum = 0.0f;
            for (int c = 0; c < nCh; ++c)
            {
                auto& ch = channels[(size_t) c];
                // The sound joins the signal before the pitch shifter and the crush / sample-rate / filter stage, so
                // it is shifted by the sequence and crushed with everything else.
                float y = ch.stutter.process(x[(size_t) c]) + sampleSignal;
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
                const float cleanSequence = y; // the reverb hears the sequence before the distortion
                if (distRun)
                {
                    const float distorted = blk.distType == 1 ? ch.fuzz.process(y) : ch.dist.process(y);
                    y += (distorted - y) * distGate;
                }
                float reverbOut = ch.reverb.process(cleanSequence * reverbSend * reverbOnGain, revSettings);
                if (distRun && blk.distReverb)
                    reverbOut += ((blk.distType == 1 ? ch.fuzzReverb.process(reverbOut) : ch.distReverb.process(reverbOut)) - reverbOut) * distAmount;
                y *= muteGain;
                y *= invHeadroom;
                y += reverbOut * (revMix * revLevel * invHeadroom); // added to the sequenced sound, which stays as it is
                // The EQ comes last, on everything the sequenced sound has become. What reaches it is kept as the
                // reference the player can capture; while the captured clip plays, the clip goes through it instead.
                if (audition)
                    y = eqReference.clipSample(c);
                else
                    eqReference.record(c, y);
                beforeEqSum += y;
                if (eqRun)
                {
                    const float equalized = ch.eq.process(y);
                    y += (equalized - y) * eqAmount;
                }
                afterEqSum += y;

                if (!std::isfinite(y))
                {
                    y = 0.0f;
                    ch.ladder.reset();
                }

                if (!audition) // the clip is heard on its own, without the clean signal
                    y = lerpExact(dry[(size_t) c], y, mixAmount);
                y *= outGain;

                io[c][n] = lerpExact(dry[(size_t) c], y, bypassMix);
            }

            eqReference.endSample(beforeEqSum / (float) nCh, afterEqSum / (float) nCh);

            beatPhase += 1.0 / beatSamples;
            if (beatPhase >= 1.0)
                beatPhase -= std::floor(beatPhase);
        }

        while (nextEvent < numEvents)
            applyExternalTrigger(events[nextEvent++].action);

        beatPhaseOut.store((float) beatPhase, std::memory_order_relaxed);
        stepOut.store(seqRunning ? active[(size_t) seqIdx] : -1, std::memory_order_relaxed);
        patternStepOut.store(patternIdx, std::memory_order_relaxed);
        stutterOut.store(channels[0].stutter.isActive(), std::memory_order_relaxed);
        if (inputPeak > inputPeakOut.load(std::memory_order_relaxed))
            inputPeakOut.store(inputPeak, std::memory_order_relaxed);
        inputPeak = 0.0f;
        attackCountOut.store(attacksHeard, std::memory_order_relaxed);
        effectsActiveOut.store(reverbSend > 0.05f, std::memory_order_relaxed);
    }

    // Read by the editor (relaxed atomics; purely cosmetic).
    float getBeatPhase() const { return beatPhaseOut.load(std::memory_order_relaxed); }
    // The loudest input (what the pick detector hears) since the last call, and how many picks it has caught.
    float takeInputPeak() { return inputPeakOut.exchange(0.0f, std::memory_order_relaxed); }
    int getAttackCount() const { return attackCountOut.load(std::memory_order_relaxed); }
    // True while a sequence or stutter plays, i.e. while the distortion, reverb and sample blend have something to act on.
    bool isEffectSideActive() const { return effectsActiveOut.load(std::memory_order_relaxed); }
    int getCurrentStep() const { return stepOut.load(std::memory_order_relaxed); }
    bool isStuttering() const { return stutterOut.load(std::memory_order_relaxed); }
    int getPatternStep() const { return patternStepOut.load(std::memory_order_relaxed); } // 0 .. bar length - 1

    // The EQ's reference sound: capture, the clip playing through the EQ, the spectrum feed (see EqReference.h).
    EqReference& getEqReference() { return eqReference; }
    const EqReference& getEqReference() const { return eqReference; }
    double getSampleRate() const { return fs; }

    // Starts the pattern's bar again from its first step, on the next sample (any thread). While the pattern is
    // following the host the host's position wins and puts the bar back.
    void restartPatternBar() { patternRestartRequests.fetch_add(1, std::memory_order_release); }

private:
    struct Channel
    {
        StutterChannel stutter;
        PitchShifter pitch;
        LadderFilter ladder;
        Reverb reverb;
        Distortion dist, distReverb; // the second one distorts the reverb when the player asks for that
        Fuzz fuzz, fuzzReverb;       // the same two jobs for the fuzz setting
        Equalizer eq;
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
        float reverbMix = 0.0f, reverbTone = 0.5f, reverbLevel = 1.0f, reverbLowCut = 20.0f, reverbHighCut = 20000.0f, reverbDecay = 0.5f;
        int reverbType = 0;
        bool reverbReverse = false;
        bool distOn = false, distReverb = false;
        bool reverbOn = true, sampleOn = true, eqOn = false;
        int distType = 0;
        bool patternOn = false;
        bool patternLock = false;
        TriggerAction patternAction = TriggerAction::RestartSequence;
        std::uint32_t patternSteps = 0;
        int patternBarSteps = 16;
        float distDrive = 0.5f, distTone = 0.5f, distLevel = 0.8f;
        float sampleGain = 0.0f, sampleChance = 1.0f;
        const SampleSet* sampleSet = nullptr;
        float loopSampleGain = 0.0f, loopSampleChance = 1.0f;
        const SampleSet* loopSampleSet = nullptr;
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
        blk.reverbDecay = std::clamp(p.reverbDecay, 0.0f, 1.0f);
        blk.reverbReverse = p.reverbReverse;
        if (const int requests = patternRestartRequests.load(std::memory_order_acquire); requests != seenPatternRestarts)
        {
            seenPatternRestarts = requests;
            patternFresh = true; // the first step of the bar fires on the next sample
        }
        blk.patternOn = p.patternOn;
        blk.patternLock = p.patternLock;
        blk.patternAction = p.patternAction;
        blk.patternSteps = p.patternSteps;
        blk.patternBarSteps = std::clamp(p.patternBarSteps, 1, 32);
        if (patternIdx >= blk.patternBarSteps)
            patternIdx = 0;
        blk.distOn = p.distOn;
        blk.reverbOn = p.reverbOn;
        blk.eqOn = p.eqOn;
        blk.sampleOn = p.sampleOn;
        blk.distType = std::clamp(p.distType, 0, 1);
        blk.distReverb = p.distReverb;
        blk.distDrive = p.distDrive;
        blk.distTone = p.distTone;
        blk.distLevel = p.distLevel;
        for (auto& ch : channels)
        {
            ch.dist.setParams(p.distDrive, p.distTone, p.distLevel);
            ch.distReverb.setParams(p.distDrive, p.distTone, p.distLevel);
            ch.fuzz.setParams(p.distDrive, p.distTone, p.distLevel);
            ch.fuzzReverb.setParams(p.distDrive, p.distTone, p.distLevel);
        }
        blk.sampleGain = sampleGain(p.sampleMix, p.sampleBaseDb, p.sampleMaxDb);
        blk.sampleChance = std::clamp(p.sampleChance, 0.0f, 1.0f);
        blk.sampleSet = p.sampleSet;
        blk.loopSampleGain = sampleGain(p.loopSampleMix, p.loopSampleBaseDb, p.loopSampleMaxDb);
        blk.loopSampleChance = std::clamp(p.loopSampleChance, 0.0f, 1.0f);
        blk.loopSampleSet = p.loopSampleSet;
        if (p.sampleSet != lastSampleSet || p.loopSampleSet != lastLoopSampleSet) // whatever is sounding still points into the old set
        {
            samplePlayer.reset();
            lastSampleSet = p.sampleSet;
            lastLoopSampleSet = p.loopSampleSet;
        }
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
            reverbDecaySm.snap(blk.reverbDecay);
            sampleMixSm.snap(blk.sampleGain);
            loopSampleMixSm.snap(blk.loopSampleGain);
            distOnSm.snap(blk.distOn ? 1.0f : 0.0f);
            reverbOnSm.snap(blk.reverbOn ? 1.0f : 0.0f);
            eqOnSm.snap(blk.eqOn ? 1.0f : 0.0f);
            sampleOnSm.snap(blk.sampleOn ? 1.0f : 0.0f);
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
            const int own = p.stepTypes[(size_t) i];
            stepType[(size_t) i] = own < 0 ? p.seqType : (SeqType) std::clamp(own, 0, 2);
            steps[(size_t) i] = mapping::decodeStep(p.steps[(size_t) i], stepType[(size_t) i]);
            if (steps[(size_t) i].kind != mapping::Step::Kind::Skip)
                active[(size_t) numActive++] = i;
        }

        loopFirst = loopLast = -1;
        loopPlays = std::max(0, p.loopPlays);
        if (p.loopOn)
        {
            const int lo = std::clamp(std::min(p.loopFrom, p.loopTo), 0, kNumSteps - 1);
            const int hi = std::clamp(std::max(p.loopFrom, p.loopTo), 0, kNumSteps - 1);
            for (int i = 0; i < numActive; ++i)
                if (active[(size_t) i] >= lo && active[(size_t) i] <= hi)
                {
                    if (loopFirst < 0)
                        loopFirst = i;
                    loopLast = i;
                }
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

        if (p.hostPpq >= 0.0)
            followHost(p.hostPpq);

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

    void onAttack() { applyExternalTrigger(TriggerAction::RestartAndStutter); }

    // A trigger that does not come from the beat pattern (a picked note, a drum hit). With the pattern lock on it can
    // still fire the stutter, but it cannot restart or step the sequence.
    void applyExternalTrigger(TriggerAction action)
    {
        if (blk.patternLock)
        {
            if (action == TriggerAction::RestartSequence || action == TriggerAction::NextStep)
                return;
            if (action == TriggerAction::RestartAndStutter)
                action = TriggerAction::Stutter;
        }
        applyTrigger(action);
    }

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

    // The pattern's clock: one step is a sixteenth note of the tempo. The first step of the bar fires as soon as the
    // engine starts; after that a step fires when the clock reaches it. While following the host, the bar is put
    // back in line with the host's position at the start of every block that has drifted away from it.
    void advancePattern()
    {
        const double stepSamples = std::max(1.0, beatSamples * 0.25);
        bool fire = false;
        if (patternFresh)
        {
            patternFresh = false;
            patternIdx = 0;
            patternPhase = 0.0;
            fire = true;
        }
        else
        {
            patternPhase += 1.0;
            if (patternPhase >= stepSamples)
            {
                patternPhase -= stepSamples;
                patternIdx = (patternIdx + 1) % blk.patternBarSteps;
                fire = true;
            }
        }
        if (fire && blk.patternOn && ((blk.patternSteps >> patternIdx) & 1u) != 0u)
            applyTrigger(blk.patternAction);
    }

    void followHost(double ppq)
    {
        const double stepSamples = std::max(1.0, beatSamples * 0.25);
        const double position = std::max(0.0, ppq) * 4.0; // in steps
        const double whole = std::floor(position);
        const int idx = (int) std::fmod(whole, (double) blk.patternBarSteps);
        const double phase = (position - whole) * stepSamples;
        const bool sameStep = idx == patternIdx && std::abs(phase - patternPhase) < 0.5 * stepSamples;
        if (patternFresh || !sameStep)
        {
            patternFresh = false;
            patternIdx = idx;
            patternPhase = phase;
            if (phase < 1.0 && blk.patternOn && ((blk.patternSteps >> patternIdx) & 1u) != 0u)
                applyTrigger(blk.patternAction); // landed exactly on a step
        }
    }

    void startSequence()
    {
        seqRunning = true;
        seqIdx = 0;
        seqCycles = 0;
        loopCount = 0;
        seqPhase = 0.0;
        stepStarted();
    }

    // A sequencer step has just begun: maybe bring in one of the picked sounds, chosen at random. Muted steps stay
    // silent. A step of the loop section uses the loop section's sounds, volume and chance.
    void stepStarted()
    {
        const bool inLoop = loopFirst >= 0 && seqIdx >= loopFirst && seqIdx <= loopLast;
        const SampleSet* set = inLoop && blk.loopSampleSet != nullptr ? blk.loopSampleSet : blk.sampleSet;
        const float gain = inLoop ? blk.loopSampleGain : blk.sampleGain;
        const float chance = inLoop ? blk.loopSampleChance : blk.sampleChance;
        if (!blk.sampleOn || gain <= 0.0f || set == nullptr || set->clips.empty() || numActive == 0)
            return;
        if (steps[(size_t) active[(size_t) seqIdx]].kind == mapping::Step::Kind::Mute)
            return;
        if (chance < 1.0f && (float) (nextRandom() >> 8) * (1.0f / 16777216.0f) >= chance)
            return;
        samplePlayer.trigger(&set->clips[(size_t) (nextRandom() >> 4) % set->clips.size()], 1.0f, false, inLoop);
    }

    // Moves to the next active step; counts passes and stops after the last one. False when stopped. At the end of
    // the loop section it goes back to the section's first step until the section has played often enough.
    bool stepForward()
    {
        if (loopFirst >= 0 && seqIdx == loopLast)
        {
            ++loopCount;
            if (loopPlays == 0 || loopCount < loopPlays)
            {
                seqIdx = loopFirst;
                stepStarted();
                return true;
            }
        }
        if (++seqIdx >= numActive)
        {
            seqIdx = 0;
            loopCount = 0;
            ++seqCycles;
            if (seqRepeats > 0 && seqCycles >= seqRepeats)
            {
                seqRunning = false;
                return false;
            }
        }
        stepStarted();
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
    Smoother srKnobSm, filterKnobSm, bitsKnobSm, pitchAmountSm, cutoffLogSm, filterMixSm, bypassSm, inGainSm, outGainSm, mixSm, reverbMixSm, reverbToneSm, reverbLevelSm, reverbLowSm, reverbHighSm, reverbDecaySm, sampleMixSm, loopSampleMixSm, distOnSm, reverbOnSm, sampleOnSm, eqOnSm;
    float inGainTarget = 1.0f, outGainTarget = 1.0f, headroomScale = 1.0f, invHeadroom = 1.0f;

    // crush / sample-rate / filter stage on-off blend (see Params::fxOnlyOnTriggers)
    float fxMix = 1.0f, fxAttack = 1.0f, fxRelease = 1.0f;
    bool fxInit = false;
    SamplePlayer samplePlayer;
    EqReference eqReference;

    // the beat pattern's clock: the step the bar is at and how far into it (in samples)
    int patternIdx = 0;
    double patternPhase = 0.0;
    bool patternFresh = true;
    std::atomic<int> patternStepOut { 0 };
    std::atomic<int> patternRestartRequests { 0 };
    int seenPatternRestarts = 0;
    const SampleSet* lastSampleSet = nullptr;
    const SampleSet* lastLoopSampleSet = nullptr;
    float reverbSend = 0.0f; // 1 while a sequence or stutter plays (smoothed): the reverb hears only that

    // sequencer
    std::array<mapping::Step, kNumSteps> steps {};
    std::array<SeqType, kNumSteps> stepType {}; // what each step controls
    std::array<int, kNumSteps> active {};
    int numActive = 0, seqIdx = 0, seqCycles = 0, seqRepeats = 0;
    int loopFirst = -1, loopLast = -1, loopPlays = 0, loopCount = 0; // the loop section, as indices into active
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
    std::atomic<float> inputPeakOut { 0.0f };
    std::atomic<int> attackCountOut { 0 };
    std::atomic<bool> effectsActiveOut { false };
    float inputPeak = 0.0f;
    int attacksHeard = 0;
    std::atomic<bool> stutterOut { false };
};
} // namespace bitvibe
