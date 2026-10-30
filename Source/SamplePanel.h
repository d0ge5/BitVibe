#pragma once

#include "PluginEditor.h"

// The SAMPLE BLEND panel: pick the list of sounds the sequencer chooses from at random, how much of the chosen
// sound is mixed into the sequence, and how often a step gets one.
class SamplePanel : public juce::Component
{
public:
    static constexpr int kWidth = 456, kHeight = 92;

    SamplePanel(BitVibeProcessor&, PedalLookAndFeel&);
    ~SamplePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Updates the button text (called by the editor's timer while the page is shown).
    void refresh();

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void showChooser();
    void openFolder();
    float rawValue(const char* id) const;

    BitVibeProcessor& proc;
    SampleBank& bank;
    PedalLookAndFeel& laf;

    PedalButton chooseButton { "CHOOSE SOUNDS..." }, folderButton { "OPEN SOUNDS FOLDER" };
    juce::Slider mixSlider, chanceSlider;
    std::unique_ptr<APVTS::SliderAttachment> mixAtt, chanceAtt;
    int numAvailable = 0;
    juce::String shownButtonText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePanel)
};
