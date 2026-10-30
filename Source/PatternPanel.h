#pragma once

#include "PluginEditor.h"

// The BEAT PATTERN panel on the pedal page: what the pattern does, the 16 sixteenth-note steps of one bar to
// light, one-click patterns, and RANDOM (which re-rolls the steps of the beats you have chosen). The pattern
// itself lives in the DrumBeat; the panel only edits it and draws it.
class PatternPanel : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int kWidth = 456, kHeight = 100;

    PatternPanel(BitVibeProcessor&, PedalLookAndFeel&);
    ~PatternPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    // Pulls the pattern and the beat's tempo again (called by the editor's timer while the page is shown).
    void refresh();

private:
    juce::Rectangle<float> cell(int step) const;
    juce::Rectangle<float> beatLabel(int beat) const;
    int cellAt(juce::Point<float>) const;
    int beatLabelAt(juce::Point<float>) const;
    void setSteps(std::uint32_t steps);
    void randomise();

    DrumBeat& beat;
    PedalLookAndFeel& laf;

    juce::ComboBox actionBox;
    PedalButton clearButton { "CLEAR" }, beatsButton { "BEATS" }, offbeatsButton { "OFFBEATS" },
        backbeatButton { "BACKBEAT" }, randomButton { "RANDOM" };
    bitvibe::DrumPattern pattern;
    DrumBeat::Model model;
    bool updatingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternPanel)
};
