#pragma once

#include "BeatRoute.h"
#include "DrumBeat.h"
#include "Engine.h"
#include "Morph.h"
#include "Presets.h"
#include "SampleBank.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
inline constexpr const char* sampleRate = "sample";
inline constexpr const char* filter = "filter";
inline constexpr const char* bits = "bits";
inline constexpr const char* stutter = "stutter";
inline constexpr const char* seqPlay = "seqPlay";
inline constexpr const char* seqMult = "seqMult";
inline constexpr const char* seqType = "seqType";
inline constexpr const char* loopOn = "loopOn";
inline constexpr const char* loopFrom = "loopFrom";
inline constexpr const char* loopTo = "loopTo";
inline constexpr const char* loopRepeats = "loopRepeats";
inline constexpr const char* tempo = "tempo";
inline constexpr const char* tempoSync = "tempoSync";
inline constexpr const char* bypass = "bypass";
inline constexpr const char* hold = "hold";
inline constexpr const char* inputGain = "inputGain";
inline constexpr const char* outputGain = "outputGain";
inline constexpr const char* headroom = "headroom";
inline constexpr const char* sensitivity = "sensitivity";
inline constexpr const char* stutterLen = "stutterLen";
inline constexpr const char* expression = "expression";
inline constexpr const char* morph = "morph";
inline constexpr const char* mix = "mix";
inline constexpr const char* fxTriggers = "fxTriggers";
inline constexpr const char* reverbTone = "reverbTone";
inline constexpr const char* reverbMix = "reverbMix";
inline constexpr const char* reverbLevel = "reverbLevel";
inline constexpr const char* reverbType = "reverbType";
inline constexpr const char* reverbOn = "reverbOn";
inline constexpr const char* sampleOn = "sampleOn";
inline constexpr const char* eqOn = "eqOn";
inline constexpr const char* eqOutput = "eqOutput";
// One set per EQ band (1..8): eq3Freq, eq3Gain, eq3Q, eq3Type, eq3On.
inline juce::String eqBand(int band, const char* what) { return "eq" + juce::String(band + 1) + what; }
inline constexpr const char* reverbDecay = "reverbDecay";
inline constexpr const char* reverbReverseRoom = "reverbRevRoom";
inline constexpr const char* reverbReverseHall = "reverbRevHall";
inline constexpr const char* reverbReverseSpring = "reverbRevSpring";
inline constexpr const char* reverbReversePlate = "reverbRevPlate";
inline constexpr const char* reverbReverseShimmer = "reverbRevShimmer";
inline constexpr const char* reverbReverseGated = "reverbRevGated";
inline constexpr const char* distOn = "distOn";
inline constexpr const char* distType = "distType";
inline constexpr const char* distDrive = "distDrive";
inline constexpr const char* distTone = "distTone";
inline constexpr const char* distLevel = "distLevel";
inline constexpr const char* distReverb = "distReverb";
inline constexpr const char* sampleMix = "sampleMix";
inline constexpr const char* sampleChance = "sampleChance";
inline constexpr const char* sampleBase = "sampleBase";
inline constexpr const char* sampleMax = "sampleMax";
inline constexpr const char* reverbLowCut = "reverbLowCut";
inline constexpr const char* reverbHighCut = "reverbHighCut";
inline constexpr const char* beatPlay = "beatPlay";
inline constexpr const char* beatLevel = "beatLevel";
inline constexpr const char* pickTriggers = "pickTriggers";
inline constexpr const char* patternLock = "patternLock";
inline constexpr const char* stepClock = "stepClock";
inline constexpr const char* beatOutput = "beatOutput";

inline juce::String step(int index) { return "step" + juce::String(index + 1); }
} // namespace ids

class BitVibeProcessor : public juce::AudioProcessor
{
public:
    BitVibeProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // A held or long stutter keeps playing after the input stops.
    double getTailLengthSeconds() const override { return 10.0; }

    // Sixteen preset slots. A slot holds the sound (knobs, tempo, stutter length, morph set-up) but not
    // the settings that belong to the player's rig or the moment: bypass, stutter hold, host sync, the
    // expression position, input / output gain, headroom, trigger sensitivity, the dry/wet mix, the reverb and the
    // "effects only on triggers" option keep their values when a preset is recalled.
    int getNumPrograms() override { return bitvibe::kNumPresets; }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    // Editor actions (message thread).
    void saveCurrentPreset();            // writes the live sound into the selected slot
    bool isCurrentPresetModified() const; // live sound differs from the selected slot

    // Preset files to share with other players (message thread). A file holds the sound as it is heard now: the
    // preset slot's knobs, steps, tempo and morph sets, the effects on the right-hand side (distortion, reverb, sample
    // blend, EQ, dry/wet, FX only on triggers), the beat pattern and the names of the picked sounds. Settings that
    // belong to one player's rig stay out: input and output level, headroom, trigger sensitivity, sync and bypass.
    std::unique_ptr<juce::XmlElement> exportPreset() const;
    // Loads a file into the selected slot (its name and sound) and applies its effects. False, with a reason, when the
    // file is not a BitVibe preset. Sounds the receiver does not have are left out of the list.
    bool importPreset(const juce::XmlElement&, juce::String& message);
    static juce::File presetFolder(); // Documents/BitVibe/Presets

    // Expression morph: the two knob sets the expression parameter sweeps between.
    void captureToeUp();   // stores the current knob values as the toe-up set
    void captureToeDown(); // stores the current knob values as the toe-down set
    bitvibe::KnobSet getToeUp() const;
    bitvibe::KnobSet getToeDown() const;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // Tap-tempo from the editor (message thread): sets the tempo parameter from the tap interval.
    void tapTempo();

    juce::AudioProcessorValueTreeState apvts;
    bitvibe::Engine& getEngine() { return engine; }

    // The drum beat of the DRUMS tab (its hits can trigger the sequencer and the stutter).
    DrumBeat& getDrumBeat() { return drumBeat; }

    // The sounds the sample blend picks from at random (the player chooses the list).
    SampleBank& getSampleBank() { return sampleBank; }

    // Plays one of the picked sounds (the one that pick selects) at a volume of its own, so the player can hear the
    // base and max volumes while setting them. Message thread; the audio thread picks the request up.
    void requestSamplePreview(float volumeDb, std::uint32_t pick)
    {
        previewDb.store(volumeDb, std::memory_order_relaxed);
        previewPick.store(pick, std::memory_order_relaxed);
        previewCounter.fetch_add(1, std::memory_order_release);
    }

    // Where the beat's sound goes: the main output, the optional "Beat Monitor" output (the second output
    // bus, which the host switches on and the player routes to their headphones) or nowhere. Updated every
    // block; for the DRUMS tab's readout.
    bitvibe::BeatRoute getBeatRoute() const { return (bitvibe::BeatRoute) beatRoute.load(std::memory_order_relaxed); }

    // Tempo actually in use (the host's when sync is on); for display only.
    float getEffectiveBpm() const { return effectiveBpm.load(std::memory_order_relaxed); }

private:
    // Everything a preset slot remembers: the normalised value of each preset parameter (in the order
    // of presetParams) and the two morph sets.
    struct PresetSlot
    {
        juce::String name;
        std::vector<float> values;
        bitvibe::KnobSet toeUp, toeDown;
    };

    struct PresetParam
    {
        juce::String id;
        juce::RangedAudioParameter* param = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    bitvibe::Params readParams();
    bitvibe::KnobSet liveKnobs() const;
    void refreshMorphCache();

    int presetIndex(const juce::String& id) const;
    PresetSlot makeFactorySlot(int index) const;
    PresetSlot snapshotCurrent() const;
    void applySlot(const PresetSlot&);
    void recallProgram(int index);
    juce::ValueTree slotToTree(const PresetSlot&) const;
    PresetSlot slotFromTree(const juce::ValueTree&, int index) const;
    void resetToFactoryBank();

    bitvibe::Engine engine;
    DrumBeat drumBeat;
    SampleBank sampleBank;
    std::atomic<int> previewCounter { 0 };
    std::atomic<float> previewDb { -60.0f };
    std::atomic<std::uint32_t> previewPick { 0 };
    int seenPreviewCounter = 0; // audio thread
    std::array<bitvibe::TriggerEvent, 256> triggerEvents {};

    juce::AudioProcessorParameter* bypassParam = nullptr;
    std::atomic<float>* pSample = nullptr;
    std::atomic<float>* pFilter = nullptr;
    std::atomic<float>* pBits = nullptr;
    std::atomic<float>* pStutter = nullptr;
    std::atomic<float>* pSeqPlay = nullptr;
    std::atomic<float>* pSeqMult = nullptr;
    std::atomic<float>* pSeqType = nullptr;
    std::atomic<float>* pLoopOn = nullptr;
    std::atomic<float>* pLoopFrom = nullptr;
    std::atomic<float>* pLoopTo = nullptr;
    std::atomic<float>* pLoopRepeats = nullptr;
    std::atomic<float>* pTempo = nullptr;
    std::atomic<float>* pTempoSync = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pInGain = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    std::atomic<float>* pHeadroom = nullptr;
    std::atomic<float>* pSensitivity = nullptr;
    std::atomic<float>* pStutterLen = nullptr;
    std::atomic<float>* pExpression = nullptr;
    std::atomic<float>* pMorph = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pFxTriggers = nullptr;
    std::atomic<float>* pReverbTone = nullptr;
    std::atomic<float>* pReverbMix = nullptr;
    std::atomic<float>* pReverbLevel = nullptr;
    std::atomic<float>* pReverbType = nullptr;
    std::atomic<float>* pReverbOn = nullptr;
    std::atomic<float>* pSampleOn = nullptr;
    std::atomic<float>* pEqOn = nullptr;
    std::atomic<float>* pEqOutput = nullptr;
    struct EqBandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
    };
    std::array<EqBandParams, bitvibe::kNumEqBands> pEqBands {};
    std::atomic<float>* pReverbDecay = nullptr;
    std::array<std::atomic<float>*, 6> pReverbReverse {}; // one per type of reverb, in the order of the types
    std::atomic<float>* pDistOn = nullptr;
    std::atomic<float>* pDistType = nullptr;
    std::atomic<float>* pDistDrive = nullptr;
    std::atomic<float>* pDistTone = nullptr;
    std::atomic<float>* pDistLevel = nullptr;
    std::atomic<float>* pDistReverb = nullptr;
    std::atomic<float>* pSampleMix = nullptr;
    std::atomic<float>* pSampleChance = nullptr;
    std::atomic<float>* pSampleBase = nullptr;
    std::atomic<float>* pSampleMax = nullptr;
    std::atomic<float>* pReverbLowCut = nullptr;
    std::atomic<float>* pReverbHighCut = nullptr;
    std::atomic<float>* pBeatPlay = nullptr;
    std::atomic<float>* pBeatLevel = nullptr;
    std::atomic<float>* pPickTriggers = nullptr;
    std::atomic<float>* pPatternLock = nullptr;
    std::atomic<float>* pStepClock = nullptr;
    std::atomic<float>* pBeatOutput = nullptr;
    std::array<std::atomic<float>*, bitvibe::kNumSteps> pSteps {};

    // Morph sets: written on the message thread, read by the audio thread through a try-lock copy.
    mutable juce::SpinLock morphLock;
    bitvibe::KnobSet toeUp, toeDown;
    bitvibe::KnobSet cachedToeUp, cachedToeDown; // audio thread only

    // Preset bank (message thread; the audio thread never touches it).
    mutable juce::CriticalSection presetLock;
    std::vector<PresetParam> presetParams;
    std::array<PresetSlot, bitvibe::kNumPresets> slots;
    std::atomic<int> currentProgram { 0 };

    std::atomic<float> effectiveBpm { 120.0f };
    std::atomic<int> beatRoute { (int) bitvibe::BeatRoute::Main };

    double lastTapMs = 0.0;
    double tapAverageMs = 0.0;
    int tapCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitVibeProcessor)
};
