#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// A processor the host never meets. BitVibe keeps some settings that are saved with the project but are no business of
// the host's (see BitVibeProcessor::hidden): they are parameters of this one, so the pages' attachments, the preset
// slots and undo can treat them like any other parameter, while the host's own list holds only what a player can turn.
// It makes no sound and has no state of its own; BitVibeProcessor saves its parameters inside the plug-in's state.
class HiddenHost : public juce::AudioProcessor
{
public:
    const juce::String getName() const override { return "BitVibe settings"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};
