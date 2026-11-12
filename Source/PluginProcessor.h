#pragma once

#include "BeatRoute.h"
#include "DrumBeat.h"
#include "HiddenHost.h"
#include "Engine.h"
#include "Morph.h"
#include "Presets.h"
#include "AppAudioCapture.h"
#include "SampleBank.h"
#include "dsp/SampleRender.h"
#include "ParamUndo.h"

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
// HEAR, what comes out: 0 = guitar + effect, 1 = clean in between (the crush and filter act only while a stutter or
// sequence plays), 2 = effect only (the clean guitar is cut).
inline constexpr const char* hear = "hear";
// The sampler's settings (hidden from the host, not in presets; saved with the project): smpEq... is its own EQ, set
// with the same panel as the main EQ (prefix "smpEq": smpEqOn, smpEqOutput, smpEq1Freq ...).
inline constexpr const char* smpVolume = "smpVolume";
inline constexpr const char* smpDistType = "smpDistType";
inline constexpr const char* smpDrive = "smpDrive";
inline constexpr const char* smpTone = "smpTone";
inline constexpr const char* smpLevel = "smpLevel";
inline constexpr const char* smpSample = "smpSample";
inline constexpr const char* smpBits = "smpBits";
inline constexpr const char* smpEqPrefix = "smpEq";
inline constexpr const char* delayOn = "delayOn";
inline constexpr const char* delayTime = "delayTime";
inline constexpr const char* delayMix = "delayMix";
inline constexpr const char* delayDecay = "delayDecay";
inline constexpr const char* delayRepeats = "delayRepeats";
inline constexpr const char* delayLowCut = "delayLowCut";
inline constexpr const char* delayHighCut = "delayHighCut";
inline constexpr const char* delayWobble = "delayWobble";
inline constexpr const char* reverbTone = "reverbTone";
inline constexpr const char* reverbMix = "reverbMix";
inline constexpr const char* reverbType = "reverbType";
inline constexpr const char* reverbOn = "reverbOn";
inline constexpr const char* sampleOn = "sampleOn";
inline constexpr const char* eqOn = "eqOn";
inline constexpr const char* eqOutput = "eqOutput";
// One set per EQ band (1..8): eq3Freq, eq3Gain, eq3Q, eq3Type, eq3On.
inline juce::String eqBand(int band, const char* what) { return "eq" + juce::String(band + 1) + what; }
inline constexpr const char* reverbDecay = "reverbDecay";
inline constexpr const char* reverbReverse = "reverbReverse";
inline constexpr const char* distOn = "distOn";
inline constexpr const char* distType = "distType";
inline constexpr const char* distDrive = "distDrive";
inline constexpr const char* distTone = "distTone";
inline constexpr const char* distLevel = "distLevel";
inline constexpr const char* distReverb = "distReverb";
inline constexpr const char* tremOn = "tremOn";
inline constexpr const char* tremRate = "tremRate";
inline constexpr const char* tremDepth = "tremDepth";
inline constexpr const char* tremEdge = "tremEdge";
inline constexpr const char* tremEnd = "tremEnd";
inline constexpr const char* tremBend = "tremBend";
inline constexpr const char* tremRamp = "tremRamp";
inline constexpr const char* sampleMix = "sampleMix";
inline constexpr const char* sampleChance = "sampleChance";
inline constexpr const char* sampleBase = "sampleBase";
inline constexpr const char* sampleMax = "sampleMax";
inline constexpr const char* sampleMaxBeats = "sampleMaxBeats";
inline constexpr const char* reverbLowCut = "reverbLowCut";
inline constexpr const char* reverbHighCut = "reverbHighCut";
// The drum beat of the DRUMS tab (hidden from the host, like the sampler's settings; patternLock is the beat pattern's).
inline constexpr const char* beatPlay = "beatPlay";
inline constexpr const char* beatLevel = "beatLevel";
inline constexpr const char* pickTriggers = "pickTriggers";
inline constexpr const char* patternLock = "patternLock";
inline constexpr const char* stepClock = "stepClock";
inline constexpr const char* beatOutput = "beatOutput";

inline constexpr const char* seqLength = "seqLength";
inline constexpr const char* stutterType = "stutterType";
inline constexpr const char* filterType = "filterType";
inline constexpr const char* filterRes = "filterRes";
inline constexpr const char* swing = "swing";
inline constexpr const char* pingOn = "pingOn";
inline constexpr const char* pingLevel = "pingLevel";
inline constexpr const char* modOn = "modOn";
inline constexpr const char* modType = "modType";
inline constexpr const char* modRate = "modRate";
inline constexpr const char* modDepth = "modDepth";
inline constexpr const char* modFeedback = "modFeedback";
inline constexpr const char* modMix = "modMix";
inline constexpr const char* soundLevel = "soundLevel";
inline constexpr const char* ringOn = "ringOn";
inline constexpr const char* ringHz = "ringHz";
inline constexpr const char* ringMix = "ringMix";
inline constexpr const char* spliceOn = "spliceOn";
inline constexpr const char* spliceWhen = "spliceWhen";
inline constexpr const char* spliceChance = "spliceChance";
inline constexpr const char* spliceJump = "spliceJump";
inline constexpr const char* spliceGap = "spliceGap";
inline constexpr const char* replayOn = "replayOn";
inline constexpr const char* replayBack = "replayBack";
inline constexpr const char* replayWait = "replayWait";
inline constexpr const char* replayLength = "replayLength";
inline constexpr const char* replayDry = "replayDry";
inline constexpr const char* replayThreshold = "replayThreshold";
inline constexpr const char* replaySpeed = "replaySpeed";
inline constexpr const char* replayDirection = "replayDirection";
inline constexpr const char* replayStretch = "replayStretch";
inline constexpr const char* replaySwell = "replaySwell";
inline constexpr const char* replayWear = "replayWear";

inline juce::String step(int index) { return "step" + juce::String(index + 1); }
// What a step controls: 0 = the sequencer type, 1..3 = pitch, filter, sample rate.
inline juce::String stepType(int index) { return "stepType" + juce::String(index + 1); }
// Where each sequencer type (0 = pitch, 1 = filter, 2 = sample rate) keeps the step knobs while another type is in use:
// stepMem0_1 .. stepMem2_16. -1 = that type has not been used yet in this preset. Hidden from the host.
inline juce::String stepMem(int type, int index) { return "stepMem" + juce::String(type) + "_" + juce::String(index + 1); }
// A step's value in a lane (bitvibe::Lane): laneCrush1 .. laneStutter16.
inline const char* laneName(int lane)
{
    static constexpr const char* names[bitvibe::kNumLanes] { "Crush", "Level", "Trem", "Delay", "Reverb", "Stutter" };
    return names[juce::jlimit(0, bitvibe::kNumLanes - 1, lane)];
}
inline juce::String lane(int laneIndex, int index) { return "lane" + juce::String(laneName(laneIndex)) + juce::String(index + 1); }
} // namespace ids

// The nearest note to a frequency ("A4", "E5 +23c"; empty below A0), for tuning the ring mod to the song.
inline juce::String noteNameForHz(float hz)
{
    if (!(hz >= 27.0f))
        return {};
    static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
    const int nearest = (int) std::lround(midi);
    const int cents = (int) std::lround((midi - (float) nearest) * 100.0f);
    juce::String s = juce::String(names[((nearest % 12) + 12) % 12]) + juce::String(nearest / 12 - 1);
    if (cents != 0)
        s += (cents > 0 ? " +" : " ") + juce::String(cents) + "c";
    return s;
}

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

    // One bank of preset slots: the twelve factory sounds, eight empty slots for the player's own (13 to 20), then the
    // preset pack (from slot 21), as many slots as the pack has presets. A slot holds the whole sound, as a preset file
    // does: the knobs, steps, lanes, tempo and morph sets, the effects on the right-hand side (distortion, tremolo,
    // delay, reverb, the sample blend switch, EQ, REPLAY) and the beat pattern. What belongs to the player's rig or the moment is
    // left alone when a slot is recalled: bypass, stutter hold, host sync, the expression position, input / output
    // gain, headroom, trigger sensitivity, dry/wet, HEAR and the sample blend's levels (remembered per player). (A slot saved by an older version holds no effects; recalling
    // it leaves the effects as they are, as it did then.)
    static int numSlots() { return bitvibe::kFirstPackSlot + numPackPresets(); }
    int getNumPrograms() override { return numSlots(); }
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

    // The preset pack that ships inside the plug-in (the files in Presets/), in file-name order: slots kFirstPackSlot on.
    static int numPackPresets();
    static juce::String packPresetName(int index);
    // The sections after the pack itself (LOOPER, DREAMY LOOPS): a pack preset's section ("" for the pack itself), the
    // first slot of a section by name (numSlots() when there is none), and the first of the LOOPER section, where REPLAY
    // presets begin.
    static juce::String packSection(int index);
    static int firstSlotOfSection(const juce::String& section);
    static int firstLooperSlot();

    // What the sound in a slot makes use of, for the preset menu's groups: a bit per feature (1u << feature).
    enum PresetFeature
    {
        featPitchSteps, featFilterSteps, featRateSteps, featOnItsOwn, featAfterPicks, featLongSequence, featLanes, featLoop,
        featSwing,
        featBeatPattern,
        featStutter, featStutterTypes,
        featCrush, featFilterTypes, featFormant, featPing, featSplice,
        featDistortion, featRingMod, featPhaser, featFlanger, featTremolo, featDelay, featTapeWobble, featReverb, featEq,
        featMorph,
        featReplay,
        kNumPresetFeatures
    };
    static const char* presetFeatureName(int feature);  // "Pitch steps", "Tape wobble", ...
    static const char* presetFeatureGroup(int feature); // "SEQUENCER", "STUTTER", "SOUND", "EFFECTS", "MORPH" or "LOOPER"
    uint32_t presetFeatures(int slot) const;

    // FAVOURITES: the presets the player has starred (message thread). They are remembered for the player, beside the
    // window size (UiSettings.h), not in the project. A factory or pack preset is known by the name it ships with, so
    // it stays a favourite when a project renames or overwrites it, or a newer version moves it to another slot; one of
    // your own slots (13 to 20) by its number, whatever is saved in it.
    juce::String favouriteKey(int slot) const;
    bool isFavourite(int slot) const;
    void setFavourite(int slot, bool on);
    std::vector<int> favouriteSlots() const; // in slot order

    // ---- the sampler ----------------------------------------------------------------------------------------
    // Records programs on this computer (see AppAudioCapture).
    AppAudioCapture& getAppCapture() { return appCapture; }
    // The sampler's effect settings as they stand.
    bitvibe::SampleFx getSamplerFx() const;
    // The sound the sampler's PLAY button plays (already processed), at its own rate, and the same sound before its EQ
    // (mono) for the sampler EQ's spectrum. Message thread.
    void setSamplerPreview(std::vector<float> left, std::vector<float> right, double rate, std::vector<float> beforeEq = {});
    // LOOP: the preview starts again from the top when it reaches the end.
    void setSamplerPreviewLoop(bool loop) { previewLoop.store(loop, std::memory_order_relaxed); }
    // The latest n samples (n <= kScopeSize) of the preview before and after the sampler's EQ, as it plays; false (and
    // silence) while it does not.
    bool readSamplerScope(float* beforeEq, float* afterEq, int n) const;
    void playSamplerPreview();
    void stopSamplerPreview();
    // How far into the preview the playback is, in seconds (-1 while it is not playing).
    float getSamplerPreviewSeconds() const { return previewSecondsOut.load(std::memory_order_relaxed); }
    // Saves a processed sound as a WAV in the sounds folder's "Sampled" subfolder, under the name given (made unique
    // in the library), marked so the sample blend keeps its level, and adds it to a bank when one is named. Returns
    // false, with a reason, when it cannot be written.
    bool saveSampledSound(const std::vector<float>& left, const std::vector<float>& right, double rate, const juce::String& name,
                          const juce::String& bank, juce::String& message, juce::String* savedFileName = nullptr);
    static juce::File sampledFolder();

    // The EQ's captured reference clip as a WAV file (24 bit, at the plug-in's sample rate). False, with a reason,
    // when there is no clip yet or the file cannot be written.
    bool saveCapturedClip(const juce::File&, juce::String& message);

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

    // Switches the sequencer type (0 = pitch, 1 = filter, 2 = sample rate) the way the SEQ TYPE buttons do: the step knobs
    // of the type being left are remembered and the ones the new type was last left with come back. A type that has not been
    // used yet in this preset keeps the knobs as they are, so nothing jumps. Steps with a type of their own are not
    // touched (they do not follow the sequencer type). Message thread.
    void switchSequencerType(int newType);

    juce::AudioProcessorValueTreeState apvts;
    // Settings that are saved (with the project; the step memories in the preset slots too) but are no business of the
    // host's: the step knobs each sequencer type was left with, the sampler's effects and its EQ, and the drum beat of
    // the DRUMS tab. They are parameters of a second processor the host never meets (HiddenHost.h), so the pages'
    // attachments, the preset slots and undo treat them like any other, and the host lists only what a player can turn.
    HiddenHost hiddenHost;
    juce::AudioProcessorValueTreeState hidden;
    // A parameter by its id, whichever of the two holds it (nullptr when neither does), and the state that holds it
    // (the plug-in's own for an id neither knows).
    juce::RangedAudioParameter* parameter(const juce::String& id) const;
    juce::AudioProcessorValueTreeState& stateFor(const juce::String& id);
    bitvibe::Engine& getEngine() { return engine; }

    // Undo and redo for the changes made on the page (not for the stutter hold, which is held down and let go).
    ParamUndo& getUndo() { return undo; }

    // The drum beat of the DRUMS tab (its hits can trigger the sequencer and the stutter).
    DrumBeat& getDrumBeat() { return drumBeat; }

    // The sounds the sample blend picks from at random (the player chooses the list).
    SampleBank& getSampleBank() { return sampleBank; }

    // Plays one of the picked sounds (the one that pick selects) at a volume of its own, so the player can hear the
    // base and max volumes while setting them. Message thread; the audio thread picks the request up.
    void requestSamplePreview(float volumeDb, std::uint32_t pick, int section = SampleBank::kMain)
    {
        previewDb.store(volumeDb, std::memory_order_relaxed);
        previewPick.store(pick, std::memory_order_relaxed);
        previewSection.store(section, std::memory_order_relaxed);
        previewCounter.fetch_add(1, std::memory_order_release);
    }

    // Where the beat's sound goes: the main output, the optional "Beat Monitor" output (the second output
    // bus, which the host switches on and the player routes to their headphones) or nowhere. Updated every
    // block; for the DRUMS tab's readout.
    bitvibe::BeatRoute getBeatRoute() const { return (bitvibe::BeatRoute) beatRoute.load(std::memory_order_relaxed); }

    // The loudest output sample since the last call (the output meter under OUT). Message thread.
    float takeOutputPeak() { return outputPeak.exchange(0.0f, std::memory_order_relaxed); }

    // Tempo actually in use (the host's when sync is on); for display only.
    float getEffectiveBpm() const { return effectiveBpm.load(std::memory_order_relaxed); }

private:
    ParamUndo undo { *this, juce::StringArray { ids::hold } };

    // Everything a preset slot remembers: the normalised value of each preset parameter (in the order
    // of presetParams), the two morph sets, and the effects (normalised, in the order of effectParams) and beat pattern.
    // A slot saved before the slots held the effects has none: recalling it leaves the effects and pattern alone.
    struct PresetSlot
    {
        juce::String name;
        std::vector<float> values;
        bitvibe::KnobSet toeUp, toeDown;
        std::vector<float> effects;
        bitvibe::DrumPattern pattern;
        bool hasEffects() const { return !effects.empty(); }
    };

    struct PresetParam
    {
        juce::String id;
        juce::RangedAudioParameter* param = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::AudioProcessorValueTreeState::ParameterLayout createHiddenLayout();
    bitvibe::Params readParams();
    bitvibe::KnobSet liveKnobs() const;
    void refreshMorphCache();

    int presetIndex(const juce::String& id) const;
    PresetSlot makeFactorySlot(int index) const; // the slot as it ships (from the cache once it is built)
    PresetSlot buildFactorySlot(int index) const;
    PresetSlot snapshotCurrent() const;
    // Where the pack preset that shipped with this name is now (LIGHT TOUCH moved to the front of the pack, and new
    // presets move the ones after them): slot itself when it is not a pack slot, or no pack preset has the name.
    int packSlotNamed(const juce::String& shippedName, int slot) const;
    void applySlot(const PresetSlot&);
    void recallProgram(int index);
    // withEffects: also the effects and the pattern, as children (the project state); a preset file keeps them apart.
    juce::ValueTree slotToTree(const PresetSlot&, bool withEffects = true) const;
    PresetSlot slotFromTree(const juce::ValueTree&, int index) const;
    void readSlotValues(const juce::ValueTree&, PresetSlot&) const;
    // A preset file (BITVIBE_PRESET) as a slot, on top of base: its name, sound, effects (plain values; those it does
    // not mention at their defaults) and pattern.
    PresetSlot slotFromFile(const juce::XmlElement&, PresetSlot base) const;
    // The same sound (the effects only when the slot holds some).
    static bool sameSound(const PresetSlot& slot, const PresetSlot& other);
    void resetToFactoryBank();

    bitvibe::Engine engine;
    DrumBeat drumBeat;
    SampleBank sampleBank;
    AppAudioCapture appCapture;

    // the sampler's preview player (the audio thread mixes it into the main output)
    struct PreviewHolder
    {
        std::vector<float> left, right, beforeEq;
        double rate = 48000.0;
        PreviewHolder* nextRetired = nullptr;
    };
    HandOver<PreviewHolder> samplerPreview;
    // PLAY and STOP: the last one pressed counts (the audio thread sees a new command number and what it wants)
    std::atomic<int> previewCommands { 0 };
    std::atomic<bool> previewWanted { false };
    int seenPreviewCommand = 0;
    const PreviewHolder* playingPreview = nullptr;
    double previewPosition = 0.0, deviceRate = 48000.0;
    bool previewPlaying = false;
    std::atomic<float> previewSecondsOut { -1.0f };
    std::atomic<bool> previewLoop { false };
    // the preview before and after its EQ, for the sampler EQ's spectrum (written on the audio thread)
    static constexpr int kScopeSize = bitvibe::EqReference::kScopeSize;
    std::vector<std::atomic<float>> previewScopeBefore = std::vector<std::atomic<float>>(kScopeSize);
    std::vector<std::atomic<float>> previewScopeAfter = std::vector<std::atomic<float>>(kScopeSize);
    std::atomic<int> previewScopeWritten { 0 };
    int previewScopePos = 0;
    void mixSamplerPreview(juce::AudioBuffer<float>& out, int numChannels, int numSamples);
    std::atomic<int> previewCounter { 0 };
    std::atomic<float> previewDb { -60.0f };
    std::atomic<std::uint32_t> previewPick { 0 };
    std::atomic<int> previewSection { 0 };
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
    std::atomic<float>* pSpliceOn = nullptr;
    std::atomic<float>* pSpliceWhen = nullptr;
    std::atomic<float>* pSpliceChance = nullptr;
    std::atomic<float>* pSpliceJump = nullptr;
    std::atomic<float>* pSpliceGap = nullptr;
    std::atomic<float>* pReplayOn = nullptr;
    std::atomic<float>* pReplayBack = nullptr;
    std::atomic<float>* pReplayWait = nullptr;
    std::atomic<float>* pReplayLength = nullptr;
    std::atomic<float>* pReplayDry = nullptr;
    std::atomic<float>* pReplayThreshold = nullptr;
    std::atomic<float>* pReplaySpeed = nullptr;
    std::atomic<float>* pReplayDirection = nullptr;
    std::atomic<float>* pReplayStretch = nullptr;
    std::atomic<float>* pReplaySwell = nullptr;
    std::atomic<float>* pReplayWear = nullptr;
    std::atomic<float>* pStutterLen = nullptr;
    std::atomic<float>* pExpression = nullptr;
    std::atomic<float>* pMorph = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pHear = nullptr;
    std::atomic<float>* pDelayOn = nullptr;
    std::atomic<float>* pDelayTime = nullptr;
    std::atomic<float>* pDelayMix = nullptr;
    std::atomic<float>* pDelayDecay = nullptr;
    std::atomic<float>* pDelayRepeats = nullptr;
    std::atomic<float>* pDelayLowCut = nullptr;
    std::atomic<float>* pDelayHighCut = nullptr;
    std::atomic<float>* pDelayWobble = nullptr;
    std::atomic<float>* pReverbTone = nullptr;
    std::atomic<float>* pReverbMix = nullptr;
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
    std::atomic<float>* pReverbReverse = nullptr;
    std::atomic<float>* pDistOn = nullptr;
    std::atomic<float>* pDistType = nullptr;
    std::atomic<float>* pDistDrive = nullptr;
    std::atomic<float>* pDistTone = nullptr;
    std::atomic<float>* pDistLevel = nullptr;
    std::atomic<float>* pDistReverb = nullptr;
    std::atomic<float>* pTremOn = nullptr;
    std::atomic<float>* pTremRate = nullptr;
    std::atomic<float>* pTremDepth = nullptr;
    std::atomic<float>* pTremEdge = nullptr;
    std::atomic<float>* pTremEnd = nullptr;
    std::atomic<float>* pTremBend = nullptr;
    std::atomic<float>* pTremRamp = nullptr;
    std::atomic<float>* pSampleMix = nullptr;
    std::atomic<float>* pSampleChance = nullptr;
    std::atomic<float>* pSampleBase = nullptr;
    std::atomic<float>* pSampleMax = nullptr;
    std::atomic<float>* pSampleMaxBeats = nullptr;
    std::atomic<float>* pReverbLowCut = nullptr;
    std::atomic<float>* pReverbHighCut = nullptr;
    std::atomic<float>* pBeatPlay = nullptr;
    std::atomic<float>* pBeatLevel = nullptr;
    std::atomic<float>* pPickTriggers = nullptr;
    std::atomic<float>* pPatternLock = nullptr;
    std::atomic<float>* pStepClock = nullptr;
    std::atomic<float>* pBeatOutput = nullptr;
    std::array<std::atomic<float>*, bitvibe::kNumSteps> pSteps {};
    std::array<std::atomic<float>*, bitvibe::kNumSteps> pStepTypes {};
    std::array<std::array<std::atomic<float>*, bitvibe::kNumSteps>, bitvibe::kNumLanes> pLanes {};
    std::atomic<float>* pSeqLength = nullptr;
    std::atomic<float>* pStutterType = nullptr;
    std::atomic<float>* pFilterType = nullptr;
    std::atomic<float>* pFilterRes = nullptr;
    std::atomic<float>* pSwing = nullptr;
    std::atomic<float>* pPingOn = nullptr;
    std::atomic<float>* pPingLevel = nullptr;
    std::atomic<float>* pModOn = nullptr;
    std::atomic<float>* pModType = nullptr;
    std::atomic<float>* pModRate = nullptr;
    std::atomic<float>* pModDepth = nullptr;
    std::atomic<float>* pModFeedback = nullptr;
    std::atomic<float>* pModMix = nullptr;
    std::atomic<float>* pSoundLevel = nullptr;
    std::atomic<float>* pRingOn = nullptr;
    std::atomic<float>* pRingHz = nullptr;
    std::atomic<float>* pRingMix = nullptr;

    // Morph sets: written on the message thread, read by the audio thread through a try-lock copy.
    mutable juce::SpinLock morphLock;
    bitvibe::KnobSet toeUp, toeDown;
    bitvibe::KnobSet cachedToeUp, cachedToeDown; // audio thread only

    // Preset bank (message thread; the audio thread never touches it).
    mutable juce::CriticalSection presetLock;
    std::vector<PresetParam> presetParams;
    std::vector<PresetParam> effectParams; // the shared effects a slot holds (see isSharedEffect)
    std::vector<PresetSlot> slots;
    std::vector<PresetSlot> factoryBank;   // every slot as it ships, built once
    std::atomic<int> currentProgram { 0 };

    std::atomic<float> effectiveBpm { 120.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<int> beatRoute { (int) bitvibe::BeatRoute::Main };

    double lastTapMs = 0.0;
    double tapAverageMs = 0.0;
    int tapCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitVibeProcessor)
};
