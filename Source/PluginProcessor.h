#pragma once

#include "Engine.h"

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
inline constexpr const char* tempo = "tempo";
inline constexpr const char* tempoSync = "tempoSync";
inline constexpr const char* bypass = "bypass";
inline constexpr const char* hold = "hold";
inline constexpr const char* inputGain = "inputGain";
inline constexpr const char* outputGain = "outputGain";
inline constexpr const char* headroom = "headroom";
inline constexpr const char* sensitivity = "sensitivity";
inline constexpr const char* stutterLen = "stutterLen";

inline juce::String step(int index) { return "step" + juce::String(index + 1); }
} // namespace ids

class OttobitProcessor : public juce::AudioProcessor
{
public:
    OttobitProcessor();

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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Factory 1"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // Tap-tempo from the editor (message thread): sets the tempo parameter from the tap interval.
    void tapTempo();

    juce::AudioProcessorValueTreeState apvts;
    ottobit::Engine& getEngine() { return engine; }

    // Tempo actually in use (the host's when sync is on); for display only.
    float getEffectiveBpm() const { return effectiveBpm.load(std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ottobit::Params readParams();

    ottobit::Engine engine;

    juce::AudioProcessorParameter* bypassParam = nullptr;
    std::atomic<float>* pSample = nullptr;
    std::atomic<float>* pFilter = nullptr;
    std::atomic<float>* pBits = nullptr;
    std::atomic<float>* pStutter = nullptr;
    std::atomic<float>* pSeqPlay = nullptr;
    std::atomic<float>* pSeqMult = nullptr;
    std::atomic<float>* pSeqType = nullptr;
    std::atomic<float>* pTempo = nullptr;
    std::atomic<float>* pTempoSync = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pInGain = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    std::atomic<float>* pHeadroom = nullptr;
    std::atomic<float>* pSensitivity = nullptr;
    std::atomic<float>* pStutterLen = nullptr;
    std::array<std::atomic<float>*, ottobit::kNumSteps> pSteps {};

    std::atomic<float> effectiveBpm { 120.0f };

    double lastTapMs = 0.0;
    double tapAverageMs = 0.0;
    int tapCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OttobitProcessor)
};
