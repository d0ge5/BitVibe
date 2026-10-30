#pragma once

#include "PluginEditor.h"

// The BEAT PATTERN panel on the pedal page: what the pattern does, the time signature, the sixteenth-note steps of
// one bar to light (the grid follows the signature), one-click patterns, and RANDOM (which re-rolls the steps of
// the beats you have chosen). The step the bar is at right now is highlighted, so it is easy to see where a step
// sits in time. The pattern itself lives in the DrumBeat; the panel only edits it and draws it.
class PatternPanel : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int kWidth = 456, kHeight = 134;

    PatternPanel(BitVibeProcessor&, PedalLookAndFeel&);
    ~PatternPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    // Pulls the pattern again and repaints (called by the editor's timer while the page is shown).
    void refresh();

private:
    void layoutGrid();
    int cellAt(juce::Point<float>) const;
    int beatLabelAt(juce::Point<float>) const;
    void setSteps(std::uint32_t steps);
    void randomise();

    BitVibeProcessor& proc;
    DrumBeat& beat;
    PedalLookAndFeel& laf;

    juce::ComboBox actionBox, signatureBox;
    PedalButton restartButton { "RESTART BAR" };
    juce::ToggleButton lockButton { "PATTERN LOCK" }; // only the pattern restarts the sequence
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAtt;
    PedalButton clearButton { "CLEAR" }, beatsButton { "BEATS" }, offbeatsButton { "OFFBEATS" },
        backbeatButton { "BACKBEAT" }, randomButton { "RANDOM" };
    bitvibe::DrumPattern pattern;
    std::array<juce::Rectangle<float>, bitvibe::kMaxPatternSteps> cells;
    std::array<juce::Rectangle<float>, 5> beatLabels; // the beat numbers under the grid (they also pick the beats RANDOM may change)
    int shownSignature = -1;
    bool updatingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatternPanel)
};
