#pragma once

// BitVibe engine: stereo-capable, header-only, no JUCE dependency.
//
// Signal chain (per channel):   in -> Stutter -> Pitch (sequencer) -> Sample-rate reducer
//                                  -> Bit-depth reducer (+ gate) -> Ladder filter -> Mute -> out
// One shared controller runs the pick-attack detector, the step sequencer (up to 16 steps), tempo and the
// stutter triggers, so both channels always stay in lock-step.
//
// ASSUMED (the published documentation does not specify): the order of the blocks in the chain, the fixed ladder
// resonance, the stutter snippet length (a note division of the tempo) and the detector design.

#include "Components.h"
#include "Delay.h"
#include "Distortion.h"
#include "EqReference.h"
#include "Equalizer.h"
#include "Fuzz.h"
#include "Mapping.h"
#include "Reverb.h"
#include "Samples.h"
#include "Scope.h"
#include "Tremolo.h"

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
    // The filter's type (FilterType) and the RESONANCE knob, 0..1 (see mapping::ladderFeedback and filterQ).
    int filterType = 0;
    float filterRes = mapping::kDefaultFilterRes;
    int seqPlay = 5;   // 0..5  (off, once, twice, 4, 8, continuous)
    int seqMult = 1;   // 0..7  (x1 .. x128)
    // Swing, 0..1: the odd steps (the 1st, 3rd, ...) lengthen and the even ones shorten by up to a third
    // (kMaxSwingRatio, a 2:1 triplet feel at full), so each pair of steps keeps the tempo. 0 = straight.
    float swing = 0.0f;
    // PING: a short click into the chain at the start of every step (muted steps stay silent), so a resonant
    // filter rings at its cutoff like a drum synth. It needs no input to sound. Off by default.
    bool pingOn = false;
    float pingLevel = 0.7f;

    // Second layer (Alt): the sequencer steps, normalised 0..1. The first seqLength of them play.
    std::array<float, kNumSteps> steps = mapping::defaultStepKnobs();
    int seqLength = kDefaultSeqLength; // 1..kNumSteps
    SeqType seqType = SeqType::Pitch;
    // What each step controls: -1 = the sequencer type above, 0..2 = its own (pitch, filter, sample rate).
    std::array<int, kNumSteps> stepTypes = mapping::followSeqType();
    // Each step's value in each lane (see Lane): what else the step does to the sequenced sound while it plays.
    LaneValues lanes = neutralLanes();

    // A loop section: steps loopFrom..loopTo (0..15, either order) play loopPlays times in a row (0 = forever) before
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
    // The preset's own level (dB, -18..+12): a gain on the sequenced sound before the dry/wet blend, so presets can
    // be matched in loudness. The pack's presets are levelled on a test pluck when they are written.
    float soundLevelDb = 0.0f;
    bool lineHeadroom = false;       // false = instrument level, true = line/synth level
    float triggerSensitivity = 0.5f; // attack detector
    int stutterDivision = 1;         // snippet length: 0 = 1/4 note, 1 = 1/8, 2 = 1/16
    StutterKind stutterKind = StutterKind::Repeat; // what the stutter does with its slice

    // Blend between the clean input (0) and the effect (1). The clean signal is the input as it
    // arrives, before the input gain; the output gain applies to the blend.
    float mix = 1.0f;

    // When on, the sample-rate, bit-depth and filter stage only acts while the stutter or the
    // sequencer is running (a sequencer set to "continuous" is always running); otherwise the
    // signal passes that stage untouched.
    bool fxOnlyOnTriggers = false;

    // When on, only the sequenced sound leaves the plug-in: the clean signal is cut (as if mix were fully wet) and the
    // effect side is silent except while a sequence or stutter plays (and while a sample preview sounds). Its reverb
    // and the EQ after it still ring out when the sequence stops.
    bool seqOnly = false;

    // Reverb on the sequenced / stuttered sound only (never on the clean signal that MIX blends back in).
    // reverbMix = how much reverb is added (0 = none, the default); tone 0 = dark .. 1 = bright; level trims the reverb's output.
    float reverbMix = 0.0f;
    float reverbTone = 0.5f;
    float reverbLevelDb = 0.0f;
    int reverbType = 0;                // ReverbType: room, hall or spring
    float reverbDecay = 0.5f;          // 0 = a quarter of the normal tail, 0.5 = normal, 1 = four times as long
    bool reverbReverse = false;        // the tail swells up after the sound instead of dying away (for the type chosen)
    float reverbLowCutHz = 20.0f;      // high-pass on the reverb only (20 Hz = off)
    // The delay on the sequenced sound, after the distortion and before the reverb (which hears its echoes too). Its
    // time is a note length of the tempo (kDelayDivisions); the echoes ring on after the sequence stops. Off by default.
    bool delayOn = false;
    int delayDivision = kDefaultDelayDivision;
    float delayMix = 0.4f;             // how loud the echoes are
    float delayDecay = 0.5f;           // how much quieter each echo is than the one before
    int delayRepeats = 4;              // 1..16 echoes, or kDelayForever
    float delayLowCutHz = 20.0f, delayHighCutHz = 20000.0f;
    float delayWobble = 0.0f;          // tape wobble on the echoes (0 = clean)
    float reverbHighCutHz = 20000.0f;  // low-pass on the reverb only (20 kHz = off)

    // Distortion on the sequenced sound (the sequence, its stutters and the random sounds; never the clean signal):
    // drive / tone / level, 0..1 each. distReverb also distorts the reverb. Off by default.
    bool distOn = false;
    int distType = 0; // 0 = overdrive, 1 = fuzz (drive = sustain, level = volume)
    float distDrive = 0.5f, distTone = 0.5f, distLevel = 0.8f;
    bool distReverb = false;

    // The tremolo on the sequenced sound, after the distortion and before the delay (so the echoes are chopped too).
    // Its rate curve starts again with each sequence or stutter. Off by default.
    bool tremOn = false;
    TremoloSettings trem;

    // The ring mod on the sequenced sound, after the distortion and before the tremolo (so the delay's echoes carry
    // it too): the sound times a sine at its frequency; MIX blends it in. Off by default.
    bool ringOn = false;
    float ringHz = 440.0f;
    float ringMix = 1.0f;

    // The phaser / flanger on the sequenced sound, after the ring mod and before the tremolo: modType 0 = phaser,
    // 1 = flanger. Its sweep takes modRateBeats beats of the tempo, locked to the beat clock. Off by default.
    bool modOn = false;
    int modType = 0;
    float modRateBeats = 4.0f;
    float modDepth = 0.7f, modFeedback = 0.5f, modMix = 1.0f;

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
    // The longest a random sound plays, in beats of the tempo (0 = all of it); then it fades out. Tames long sounds.
    float sampleMaxBeats = 0.0f;

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
        pingDecay = (float) std::exp(-1.0 / (0.0015 * fs));
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
        for (auto& ch : channels)
        {
            ch.delay.prepare(fs);
            ch.flanger.prepare(fs);
            ch.phaser.reset();
        }
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
        filterResSm.setTime(fs, 10.0f);
        bitsKnobSm.setTime(fs, 10.0f);
        filterEnvelope.prepare(fs);
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
        tremOnSm.setTime(fs, 10.0f);
        reverbOnSm.setTime(fs, 15.0f);
        delayOnSm.setTime(fs, 15.0f);
        delayMixSm.setTime(fs, 15.0f);
        delayLowSm.setTime(fs, 15.0f);
        delayHighSm.setTime(fs, 15.0f);
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
            ch.delay.reset();
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
        laneNow = kLaneNeutral;
        lanesPrimed = false;
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
        seqOnlyGate = 0.0f;
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
            // the playing step's lanes (neutral while no sequence plays), glided over the same short ramp as a mute
            float crush = 0.0f;
            for (int l = 0; l < kNumLanes; ++l)
            {
                const float target = seqRunning ? lanes[(size_t) l][(size_t) active[(size_t) seqIdx]] : kLaneNeutral[(size_t) l];
                if (l == (int) Lane::Crush)
                    crush = target; // the crusher moves in steps anyway
                else if (l != (int) Lane::Stutter)
                {
                    auto& v = laneNow[(size_t) l];
                    v += muteCoef * (target - v);
                    if (!lanesPrimed || std::abs(target - v) < 1.0e-5f) // the very first sample starts where the lanes are
                        v = target;
                }
            }
            lanesPrimed = true;
            const float laneLevel = laneNow[(size_t) Lane::Level], laneTrem = laneNow[(size_t) Lane::Trem];
            const float laneDelay = laneNow[(size_t) Lane::Delay], laneReverb = laneNow[(size_t) Lane::Reverb];
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
            const float knobBits = mapping::bitDepth(bitsKnobSm.next(blk.bits));
            const float bitsF = crush > 0.0f ? knobBits + (mapping::kMinBits - knobBits) * std::min(crush, 1.0f) : knobBits;
            const bool bitsBypass = bitsF >= 23.5f;
            const float levels = std::exp2(bitsF - 1.0f);
            updateGate(mono, bitsF);

            // Filter: the knob (by the type's range) times the step's factor is the cutoff; the ENVELOPE type opens it
            // further with each picked note. A change of type fades the filter out, swaps it and fades it back in.
            const float envelopeOpen = filterEnvelope.process(mono);
            const float fcKnob = mapping::filterHzFor(blk.filterType, filterKnobSm.next(blk.filter)) * filterFactor;
            const float fcWanted = blk.filterType == FilterType::Envelope
                                       ? fcKnob * std::exp2(mapping::kEnvelopeOctaves * envelopeOpen) : fcKnob;
            const float fcTarget = std::clamp(fcWanted, 24.0f, mapping::kFilterMaxHz);
            const float fc = std::exp2(cutoffLogSm.next(std::log2(fcTarget)));
            const bool filterOff = fcKnob >= mapping::filterOffAboveHz(blk.filterType);
            const bool filterSwitching = blk.filterType != filterShown;
            const float filterMix = filterMixSm.next((filterOff || filterSwitching) ? 0.0f : 1.0f);
            if (filterSwitching && filterMix <= 0.0f)
            {
                filterShown = blk.filterType;
                for (auto& ch : channels)
                {
                    ch.ladder.reset();
                    ch.svf.reset();
                    for (auto& f : ch.formants)
                        f.reset();
                }
            }
            const float resKnob = filterResSm.next(blk.filterRes);
            const float resonance = blk.filterType == FilterType::Bloom
                                        ? resKnob + std::max(0.0f, mapping::kBloomMaxRes - resKnob) * envelopeOpen : resKnob;
            const float ladderK = mapping::ladderFeedback(resonance), filterQ = mapping::filterQ(resonance);
            const float g = std::tan(kPi * std::min(fc, 0.45f * (float) fs) / (float) fs);
            const float G = g / (1.0f + g);
            std::array<float, 3> formantG {};
            float formantQ = 1.0f;
            if (filterShown == FilterType::Formant)
            {
                const auto vowel = mapping::vowelAt(mapping::vowelPosition(fc));
                const float limit = 0.45f * (float) fs;
                formantG = { std::tan(kPi * std::min(vowel.f1, limit) / (float) fs), std::tan(kPi * std::min(vowel.f2, limit) / (float) fs),
                             std::tan(kPi * std::min(vowel.f3, limit) / (float) fs) };
                formantQ = mapping::formantQ(resonance);
            }

            // Pitch
            const float ratio = std::exp2(semitones / 12.0f);
            const float pitchAmount = pitchAmountSm.next(semitones != 0.0f ? 1.0f : 0.0f);

            // Mute
            muteGain += muteCoef * ((mute ? 0.0f : 1.0f) - muteGain);
            if (std::abs(muteGain - (mute ? 0.0f : 1.0f)) < 1.0e-5f)
                muteGain = mute ? 0.0f : 1.0f;

            const float outGain = outGainSm.next(outGainTarget);
            const float soundGain = soundLevelSm.next(blk.soundGain);
            const float mixAmount = mixSm.next(blk.mix);
            const float reverbOnGain = reverbOnSm.next(blk.reverbOn ? 1.0f : 0.0f);
            const float delayOnGain = delayOnSm.next(blk.delayOn ? 1.0f : 0.0f);
            const bool delayRun = blk.delayOn || delayOnGain > 1.0e-4f;
            if (delayRun && delayIdle) // switched on again: no old echoes
                for (auto& ch : channels)
                    ch.delay.reset();
            delayIdle = !delayRun;
            DelaySettings delaySettings = blk.delay;
            delaySettings.lowCutHz = delayLowSm.next(blk.delay.lowCutHz);
            delaySettings.highCutHz = delayHighSm.next(blk.delay.highCutHz);
            const float delayGain = delayMixSm.next(blk.delayMix) * delayOnGain;
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
            // the tremolo, gated the same way; its rate curve starts again when a sequence or stutter starts
            const bool fxActive = seqRunning || channels[0].stutter.isActive();
            if (fxActive && !tremWasActive)
                tremolo.restart();
            tremWasActive = fxActive;
            const float tremAmount = tremOnSm.next(blk.tremOn ? 1.0f : 0.0f) * reverbSend * laneTrem;
            const float tremLevel = tremolo.next(blk.trem, fs, beatSamples);
            const float tremGain = 1.0f - (1.0f - tremLevel) * tremAmount;
            // the ring mod, gated the same way: the sound times a sine, blended in by its MIX
            ringPhase += (double) ringHzSm.next(blk.ringHz) / fs;
            if (ringPhase >= 1.0)
                ringPhase -= std::floor(ringPhase);
            const float ringAmount = ringOnSm.next(blk.ringOn ? 1.0f : 0.0f) * reverbSend * ringMixSm.next(blk.ringMix);
            const float ringGain = 1.0f + (std::sin(2.0f * kPi * (float) ringPhase) - 1.0f) * ringAmount;
            // the phaser / flanger, gated the same way; its sweep follows the beat clock
            const float modOnGain = modOnSm.next(blk.modOn ? 1.0f : 0.0f);
            const bool modRun = blk.modOn || modOnGain > 1.0e-4f;
            if (modRun && modIdle)
                for (auto& ch : channels)
                {
                    ch.phaser.reset();
                    ch.flanger.reset();
                }
            modIdle = !modRun;
            const float modAmount = modOnGain * reverbSend * modMixSm.next(blk.modMix);
            const float modFlange = modTypeSm.next(blk.modType == 1 ? 1.0f : 0.0f);
            const float modDepth = modDepthSm.next(blk.modDepth), modFeedback = modFeedbackSm.next(blk.modFeedback);
            double modPhase = ((double) beatsDone + beatPhase) / (double) blk.modRateBeats;
            modPhase -= std::floor(modPhase);
            const auto heard = samplePlayer.process();
            float ping = 0.0f;
            if (pingEnv > 1.0e-4f)
            {
                ping = pingEnv * ((float) (nextRandom() >> 8) * (2.0f / 16777216.0f) - 1.0f);
                pingEnv *= pingDecay;
            }
            const float sampleSignal = (heard.sequence * sampleMixSm.next(blk.sampleGain) + heard.loopSequence * loopSampleMixSm.next(blk.loopSampleGain))
                                           * sampleOnGain + heard.preview + ping;
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
            // SEQ ONLY: the sequenced sound only, with the same quick fade in and out as the reverb's send
            const float seqTarget = (seqRunning || channels[0].stutter.isActive() || samplePlayer.isPreviewing()) ? 1.0f : 0.0f;
            seqOnlyGate += (seqTarget - seqOnlyGate) * (seqTarget > seqOnlyGate ? fxAttack : fxRelease);
            const float seqGate = blk.seqOnly ? seqOnlyGate : 1.0f;
            const float wetAmount = blk.seqOnly ? 1.0f : mixAmount;

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

                float filtered;
                switch (filterShown)
                {
                    case FilterType::Low: filtered = ch.svf.process(y, g, filterQ, StateVariableFilter::Mode::Low); break;
                    case FilterType::Band: filtered = ch.svf.process(y, g, filterQ, StateVariableFilter::Mode::Band); break;
                    case FilterType::High: filtered = ch.svf.process(y, g, filterQ, StateVariableFilter::Mode::High); break;
                    case FilterType::Ladder:
                    case FilterType::Envelope:
                    case FilterType::Bloom: filtered = ch.ladder.process(y, G, ladderK); break;
                    case FilterType::Formant:
                    {
                        float voiced = 0.0f;
                        for (size_t f = 0; f < 3; ++f)
                            voiced += mapping::kFormantGains[f]
                                      * ch.formants[f].process(y, formantG[f], formantQ, StateVariableFilter::Mode::Band);
                        filtered = voiced * mapping::kFormantMakeup;
                        break;
                    }
                    default: filtered = y; break;
                }
                y = lerpExact(y, filtered, filterMix);
                y = lerpExact(beforeFx, y, fxMix);
                const float cleanSequence = y * laneLevel; // the reverb hears the sequence before the distortion
                if (distRun)
                {
                    const float distorted = blk.distType == 1 ? ch.fuzz.process(y) : ch.dist.process(y);
                    y += (distorted - y) * distGate;
                }
                y *= ringGain;
                if (modRun)
                {
                    // the right channel sweeps a quarter of a cycle later, for width
                    double phase = modPhase + (c == 1 ? 0.25 : 0.0);
                    phase -= std::floor(phase);
                    const float sine = 0.5f - 0.5f * std::cos(2.0f * kPi * (float) phase);
                    const float triangle = phase < 0.5 ? 2.0f * (float) phase : 2.0f - 2.0f * (float) phase;
                    const float phased = ch.phaser.process(y, sine, modDepth, modFeedback, fs);
                    const float flanged = ch.flanger.process(y, triangle, modDepth, modFeedback, fs);
                    const float swept = phased + (flanged - phased) * modFlange;
                    y += (swept - y) * modAmount;
                }
                y *= tremGain;
                y *= muteGain * seqGate * laneLevel;
                // the delay: echoes of the sequenced sound (after the distortion), which ring on after the sequence stops
                const float echoes = delayRun ? ch.delay.process(y * reverbSend * laneDelay, delaySettings) * delayGain : 0.0f;
                float reverbOut = ch.reverb.process((cleanSequence * reverbSend * laneReverb + echoes) * reverbOnGain, revSettings);
                if (distRun && blk.distReverb)
                    reverbOut += ((blk.distType == 1 ? ch.fuzzReverb.process(reverbOut) : ch.distReverb.process(reverbOut)) - reverbOut) * distAmount;
                y *= invHeadroom;
                y += echoes * invHeadroom;
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

                y *= soundGain; // the preset's own level, on everything the sequenced sound has become
                if (!audition) // the clip is heard on its own, without the clean signal
                    y = lerpExact(dry[(size_t) c], y, wetAmount);
                y *= outGain;

                io[c][n] = lerpExact(dry[(size_t) c], y, bypassMix);
            }

            eqReference.endSample(beforeEqSum / (float) nCh, afterEqSum / (float) nCh);
            {
                float inSum = 0.0f, outSum = 0.0f;
                for (int c = 0; c < nCh; ++c)
                {
                    inSum += dry[(size_t) c];
                    outSum += io[c][n];
                }
                scope.add((double) beatsDone + beatPhase, inSum / (float) nCh, outSum / (float) nCh,
                          seqRunning ? active[(size_t) seqIdx] : ScopeFeed::kNoStep);
            }

            ++sampleClock;
            beatPhase += 1.0 / beatSamples;
            if (beatPhase >= 1.0)
            {
                beatsDone += std::floor(beatPhase);
                beatPhase -= std::floor(beatPhase);
            }
        }

        while (nextEvent < numEvents)
            applyExternalTrigger(events[nextEvent++].action);

        beatPhaseOut.store((float) beatPhase, std::memory_order_relaxed);
        stepOut.store(seqRunning ? active[(size_t) seqIdx] : -1, std::memory_order_relaxed);
        patternStepOut.store(patternIdx, std::memory_order_relaxed);
        {
            // the quarter-note beat on the bar clock (which follows the host's position with SYNC on)
            const double beatSteps = (double) (patternIdx % 4) + patternPhase / std::max(1.0, beatSamples * 0.25);
            beatInBarPhaseOut.store((float) std::clamp(beatSteps / 4.0, 0.0, 0.99999), std::memory_order_relaxed);
            beatInBarOut.store(patternIdx / 4, std::memory_order_relaxed);
            barStepsOut.store(blk.patternBarSteps, std::memory_order_relaxed);
        }
        stutterOut.store(channels[0].stutter.isActive(), std::memory_order_relaxed);
        if (inputPeak > inputPeakOut.load(std::memory_order_relaxed))
            inputPeakOut.store(inputPeak, std::memory_order_relaxed);
        inputPeak = 0.0f;
        attackCountOut.store(attacksHeard, std::memory_order_relaxed);
        effectsActiveOut.store(reverbSend > 0.05f, std::memory_order_relaxed);
        tremRampOut.store(tremWasActive ? tremolo.rampPosition(blk.trem, beatSamples) : -1.0f, std::memory_order_relaxed);
    }

    // The clean input and the sound coming out, bucket by bucket on the beat clock, for the SCOPE.
    const ScopeFeed& getScope() const { return scope; }

    // How far along its rate curve the tremolo is (0..1, 1 = the end rate), or -1 while nothing plays; for the curve's
    // playhead.
    float getTremoloRampPosition() const { return tremRampOut.load(std::memory_order_relaxed); }

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
    // How far through the current quarter-note beat the bar clock is (0 = on the beat), and which beat of the bar it is.
    float getBeatInBarPhase() const { return beatInBarPhaseOut.load(std::memory_order_relaxed); }
    int getBeatInBar() const { return beatInBarOut.load(std::memory_order_relaxed); }
    int getBarSteps() const { return barStepsOut.load(std::memory_order_relaxed); } // sixteenths in the bar

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
        StateVariableFilter svf;
        std::array<StateVariableFilter, 3> formants; // the FORMANT type's three peaks
        Reverb reverb;
        Distortion dist, distReverb; // the second one distorts the reverb when the player asks for that
        Fuzz fuzz, fuzzReverb;       // the same two jobs for the fuzz setting
        Delay delay;
        Equalizer eq;
        Phaser phaser;
        Flanger flanger;
        float held = 0.0f;
    };

    // Values that stay constant for the duration of one block.
    struct Block
    {
        float sampleRate = 1.0f, filter = 1.0f, bits = 1.0f;
        FilterType filterType = FilterType::Ladder;
        float filterRes = mapping::kDefaultFilterRes;
        mapping::StutterMode stutter;
        bool bypass = false;
        bool fxOnlyOnTriggers = false;
        bool seqOnly = false;
        bool pickTriggers = true, stepsOnHits = false;
        float mix = 1.0f;
        float reverbMix = 0.0f, reverbTone = 0.5f, reverbLevel = 1.0f, reverbLowCut = 20.0f, reverbHighCut = 20000.0f, reverbDecay = 0.5f;
        bool delayOn = false;
        float delayMix = 0.4f;
        DelaySettings delay;
        int reverbType = 0;
        bool reverbReverse = false;
        bool distOn = false, distReverb = false;
        bool tremOn = false;
        TremoloSettings trem;
        bool ringOn = false;
        float ringHz = 440.0f, ringMix = 1.0f;
        float soundGain = 1.0f;
        bool modOn = false;
        int modType = 0;
        float modRateBeats = 4.0f, modDepth = 0.7f, modFeedback = 0.5f, modMix = 1.0f;
        float swing = 0.0f;
        bool pingOn = false;
        float pingLevel = 0.7f;
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
        int sampleMaxSamples = 0; // the length limit in samples (0 = none)
        const SampleSet* loopSampleSet = nullptr;
        int stutterLength = 0;
        StutterKind stutterKind = StutterKind::Repeat;
    };

    void beginBlock(const Params& p)
    {
        blk.sampleRate = p.sampleRate;
        blk.filter = p.filter;
        blk.filterType = (FilterType) std::clamp(p.filterType, 0, kNumFilterTypes - 1);
        blk.filterRes = std::isfinite(p.filterRes) ? std::clamp(p.filterRes, 0.0f, 1.0f) : mapping::kDefaultFilterRes;
        blk.bits = p.bits;
        blk.bypass = p.bypass;
        blk.fxOnlyOnTriggers = p.fxOnlyOnTriggers;
        blk.seqOnly = p.seqOnly;
        blk.pickTriggers = p.pickTriggers;
        blk.stepsOnHits = p.stepsOnHits;
        blk.mix = std::clamp(p.mix, 0.0f, 1.0f);
        blk.reverbMix = std::clamp(p.reverbMix, 0.0f, 1.0f);
        blk.reverbTone = std::clamp(p.reverbTone, 0.0f, 1.0f);
        blk.reverbLevel = dbToGain(std::clamp(p.reverbLevelDb, -60.0f, 12.0f));
        blk.reverbType = std::clamp(p.reverbType, 0, kNumReverbTypes - 1);
        blk.reverbDecay = std::clamp(p.reverbDecay, 0.0f, 1.0f);
        blk.delayOn = p.delayOn;
        blk.delayMix = std::clamp(p.delayMix, 0.0f, 1.0f);
        blk.delay.decay = std::clamp(p.delayDecay, 0.0f, 1.0f);
        blk.delay.repeats = std::clamp(p.delayRepeats, 1, kDelayForever);
        blk.delay.lowCutHz = std::clamp(p.delayLowCutHz, 20.0f, 2000.0f);
        blk.delay.highCutHz = std::clamp(p.delayHighCutHz, 200.0f, 20000.0f);
        blk.delay.wobble = std::isfinite(p.delayWobble) ? std::clamp(p.delayWobble, 0.0f, 1.0f) : 0.0f;
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
        blk.tremOn = p.tremOn;
        blk.trem = p.trem;
        blk.trem.rateHz = std::clamp(p.trem.rateHz, kTremoloMinHz, kTremoloMaxHz);
        blk.trem.endOctaves = std::clamp(p.trem.endOctaves, -kTremoloMaxOctaves, kTremoloMaxOctaves);
        blk.trem.rampBeats = std::max(1, p.trem.rampBeats);
        blk.ringOn = p.ringOn;
        blk.ringHz = std::isfinite(p.ringHz) ? std::clamp(p.ringHz, kRingMinHz, kRingMaxHz) : 440.0f;
        blk.ringMix = std::isfinite(p.ringMix) ? std::clamp(p.ringMix, 0.0f, 1.0f) : 1.0f;
        blk.modOn = p.modOn;
        blk.modType = std::clamp(p.modType, 0, 1);
        blk.modRateBeats = std::isfinite(p.modRateBeats) ? std::clamp(p.modRateBeats, 0.0625f, 64.0f) : 4.0f;
        blk.modDepth = std::isfinite(p.modDepth) ? std::clamp(p.modDepth, 0.0f, 1.0f) : 0.7f;
        blk.modFeedback = std::isfinite(p.modFeedback) ? std::clamp(p.modFeedback, 0.0f, 0.9f) : 0.5f;
        blk.modMix = std::isfinite(p.modMix) ? std::clamp(p.modMix, 0.0f, 1.0f) : 1.0f;
        blk.soundGain = dbToGain(std::isfinite(p.soundLevelDb) ? std::clamp(p.soundLevelDb, -18.0f, 12.0f) : 0.0f);
        blk.swing = std::isfinite(p.swing) ? std::clamp(p.swing, 0.0f, 1.0f) : 0.0f;
        blk.pingOn = p.pingOn;
        blk.pingLevel = std::isfinite(p.pingLevel) ? std::clamp(p.pingLevel, 0.0f, 1.0f) : 0.7f;
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
            filterResSm.snap(blk.filterRes);
            bitsKnobSm.snap(p.bits);
            filterShown = blk.filterType;
            const float fc = std::clamp(mapping::filterHzFor(blk.filterType, p.filter), 24.0f, mapping::kFilterMaxHz);
            cutoffLogSm.snap(std::log2(fc));
            filterMixSm.snap(fc >= mapping::filterOffAboveHz(blk.filterType) ? 0.0f : 1.0f);
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
            tremOnSm.snap(blk.tremOn ? 1.0f : 0.0f);
            ringOnSm.snap(blk.ringOn ? 1.0f : 0.0f);
            ringMixSm.snap(blk.ringMix);
            ringHzSm.snap(blk.ringHz);
            modOnSm.snap(blk.modOn ? 1.0f : 0.0f);
            modMixSm.snap(blk.modMix);
            modTypeSm.snap(blk.modType == 1 ? 1.0f : 0.0f);
            modDepthSm.snap(blk.modDepth);
            modFeedbackSm.snap(blk.modFeedback);
            soundLevelSm.snap(blk.soundGain);
            reverbOnSm.snap(blk.reverbOn ? 1.0f : 0.0f);
            delayOnSm.snap(blk.delayOn ? 1.0f : 0.0f);
            delayMixSm.snap(blk.delayMix);
            delayLowSm.snap(blk.delay.lowCutHz);
            delayHighSm.snap(blk.delay.highCutHz);
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
        blk.sampleMaxSamples = p.sampleMaxBeats > 0.0f ? std::max(1, (int) (beatSamples * (double) p.sampleMaxBeats)) : 0;
        blk.delay.timeSamples = beatSamples * kDelayDivisions[(size_t) std::clamp(p.delayDivision, 0, (int) kDelayDivisions.size() - 1)].beats;

        // Mute gets a short ramp; keep it proportionally shorter when the sequencer is in the audio band.
        const float ramp = (float) std::clamp(stepLength * 0.05, 1.0, 0.001 * fs);
        muteCoef = 1.0f - std::exp(-1.0f / ramp);

        seqType = p.seqType;
        seqRepeats = mapping::seqRepeats(p.seqPlay);
        seqLength = std::clamp(p.seqLength, 1, kNumSteps);
        for (int l = 0; l < kNumLanes; ++l)
            for (int i = 0; i < kNumSteps; ++i)
            {
                const float v = p.lanes[(size_t) l][(size_t) i];
                lanes[(size_t) l][(size_t) i] = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : kLaneNeutral[(size_t) l];
            }
        numActive = 0;
        for (int i = 0; i < seqLength; ++i)
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
            const int lo = std::clamp(std::min(p.loopFrom, p.loopTo), 0, seqLength - 1);
            const int hi = std::clamp(std::max(p.loopFrom, p.loopTo), 0, seqLength - 1);
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
        blk.stutterKind = (StutterKind) std::clamp((int) p.stutterKind, 0, kNumStutterKinds - 1);

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

    // forStep: fired by a step's STUTTER lane, which repeats twice at full speed when the stutter knob is off.
    void fireStutter(bool forStep = false)
    {
        if ((!blk.stutter.off || forStep) && !channels[0].stutter.isHeld())
        {
            stutterFiredAt = sampleClock;
            float speed = blk.stutter.off ? 1.0f : blk.stutter.speed;
            int repeats = blk.stutter.off ? 2 : blk.stutter.repeats;
            bool reversed = false;
            if (blk.stutter.random && !blk.stutter.off)
            {
                const uint32_t r = nextRandom();
                const size_t idx = (size_t) (r % 21u);
                speed = mapping::kStutterSpeeds[idx / 7];
                repeats = mapping::kStutterRepeats[idx % 7];
                reversed = ((r >> 16) & 1u) != 0;
            }
            const int preRoll = (int) (0.008 * fs);
            for (int c = 0; c < kMaxChannels; ++c)
                channels[(size_t) c].stutter.trigger(blk.stutterLength, preRoll, speed, repeats, reversed, holdNow, blk.stutterKind);
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
        // a step with its STUTTER lane on fires the stutter (unless it has just fired, as when a pick restarts the sequence)
        if (numActive > 0 && stutterFiredAt != sampleClock
            && lanes[(size_t) Lane::Stutter][(size_t) active[(size_t) seqIdx]] > 0.5f
            && steps[(size_t) active[(size_t) seqIdx]].kind != mapping::Step::Kind::Mute)
            fireStutter(true);
        // PING: a click into the chain, so a resonant filter rings at its cutoff
        if (blk.pingOn && numActive > 0 && steps[(size_t) active[(size_t) seqIdx]].kind != mapping::Step::Kind::Mute)
            pingEnv = blk.pingLevel;
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
        samplePlayer.trigger(&set->clips[(size_t) (nextRandom() >> 4) % set->clips.size()], 1.0f, false, inLoop, blk.sampleMaxSamples);
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
        for (int guard = 0; guard < 8; ++guard)
        {
            const double length = currentStepLength();
            if (seqPhase < length)
                break;
            seqPhase -= length;
            if (!stepForward())
                break;
        }
    }

    // The playing step's length: SWING stretches the odd steps (the 1st, 3rd, ..., even indices) and shortens the
    // even ones by the same amount, so each pair of steps keeps the tempo.
    double currentStepLength() const
    {
        if (blk.swing <= 0.0f)
            return stepLength;
        const double amount = (double) blk.swing * (double) kMaxSwingRatio;
        return std::max(1.0, stepLength * ((seqIdx & 1) == 0 ? 1.0 + amount : 1.0 - amount));
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
    FilterType filterShown = FilterType::Ladder; // the filter in use (the type changes once the old one has faded out)
    EnvelopeFollower filterEnvelope;
    Smoother srKnobSm, filterKnobSm, filterResSm, bitsKnobSm, pitchAmountSm, cutoffLogSm, filterMixSm, bypassSm, inGainSm, outGainSm, mixSm, reverbMixSm, reverbToneSm, reverbLevelSm, reverbLowSm, reverbHighSm, reverbDecaySm, sampleMixSm, loopSampleMixSm, distOnSm, tremOnSm, ringOnSm, ringMixSm, ringHzSm, modOnSm, modMixSm, modTypeSm, modDepthSm, modFeedbackSm, soundLevelSm, reverbOnSm, sampleOnSm, eqOnSm, delayOnSm, delayMixSm, delayLowSm, delayHighSm;
    bool delayIdle = true; // the delay was off: its buffer is cleared when it comes back
    Tremolo tremolo;
    bool tremWasActive = false;
    double ringPhase = 0.0; // the ring mod's sine, shared by the channels
    bool modIdle = true;    // the phaser / flanger was off: its state is cleared when it comes back
    float pingEnv = 0.0f, pingDecay = 0.97f; // the ping: a short noise burst, shared by the channels
    ScopeFeed scope;
    double beatsDone = 0.0; // whole beats since the start, so the scope's beat clock only moves on
    std::atomic<float> tremRampOut { -1.0f };
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
    std::atomic<float> beatInBarPhaseOut { 0.0f };
    std::atomic<int> beatInBarOut { 0 };
    std::atomic<int> barStepsOut { 16 };
    std::atomic<int> patternRestartRequests { 0 };
    int seenPatternRestarts = 0;
    const SampleSet* lastSampleSet = nullptr;
    const SampleSet* lastLoopSampleSet = nullptr;
    float reverbSend = 0.0f; // 1 while a sequence or stutter plays (smoothed): the reverb hears only that
    float seqOnlyGate = 0.0f; // the same, plus a sounding preview: what SEQ ONLY lets out

    // sequencer
    std::array<mapping::Step, kNumSteps> steps {};
    std::array<SeqType, kNumSteps> stepType {}; // what each step controls
    std::array<int, kNumSteps> active {};
    LaneValues lanes = neutralLanes();
    std::array<float, kNumLanes> laneNow = kLaneNeutral; // the lanes as heard now (glided)
    bool lanesPrimed = false;
    int numActive = 0, seqIdx = 0, seqCycles = 0, seqRepeats = 0, seqLength = kDefaultSeqLength;
    std::int64_t sampleClock = 0, stutterFiredAt = -1;
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
