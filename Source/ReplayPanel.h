#pragma once

#include "PluginEditor.h"

// The REPLAY panel on the pedal page, under BEAT PATTERN: while the player plays only their clean signal comes out;
// when they stop, what they just played loops through the sequencer and the effects until they play again. The title
// row says what the looper is doing (and shows how far through the loop it is); the knobs set how much is looped
// (LOOK BACK), how long a stop is (WAIT), how long the loop plays (LENGTH), how much of it is heard clean (DRY) and how
// loud playing has to be to count (THRESHOLD). It folds to its title row while it is off (its settings stay).
class ReplayPanel : public juce::Component
{
public:
    static constexpr int kWidth = 456, kHeight = 104, kCollapsedHeight = 30;
    int currentHeight() const { return isOn() ? kHeight : kCollapsedHeight; }
    std::function<void()> onHeightChange; // the editor lays the column out again

    ReplayPanel(BitVibeProcessor&, PedalLookAndFeel&);
    ~ReplayPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(); // the status and the loop's playhead (the editor's timer), and the fold

    bool isOn() const;
    // What the title row says now (also for the tests).
    juce::String statusText() const;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    void applyCollapse();

    BitVibeProcessor& proc;
    PedalLookAndFeel& laf;
    juce::ToggleButton onButton { "ON" };
    std::unique_ptr<APVTS::ButtonAttachment> onAtt;
    KnobSlider backSlider, waitSlider, lengthSlider, drySlider, thresholdSlider;
    std::unique_ptr<APVTS::SliderAttachment> backAtt, waitAtt, lengthAtt, dryAtt, thresholdAtt;
    juce::String shownStatus;
    float shownPosition = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReplayPanel)
};
