#pragma once

#include "PluginEditor.h"

// The DRUMS tab: load a drum beat, watch its hits go by, and decide which drums trigger the sequencer or
// the stutter and how loud a hit must be to do so.
class DrumPage : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    DrumPage(BitVibeProcessor&, PedalLookAndFeel&);
    ~DrumPage() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void visibilityChanged() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

    // Pulls the beat, the status and the settings again (also done by the page's timer).
    void refresh();

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Row
    {
        juce::ComboBox action;
        juce::Slider minLevel, detect;
    };

    void timerCallback() override;
    void refreshLibrary();
    void chooseFile();
    void applySettings(int drum);
    void showSettings(const bitvibe::DrumSettingsArray&);
    void setTooltips();

    juce::Rectangle<float> laneBounds(int drum) const;
    juce::Range<double> viewRange() const;
    float timeToX(double seconds) const;
    int hitAt(juce::Point<float>) const;
    void editHit(int index, bool menu);
    bool firedRecently(const bitvibe::DrumHit&) const;
    void paintFeed(juce::Graphics&);
    void paintRows(juce::Graphics&);
    void paintPattern(juce::Graphics&);
    juce::Rectangle<float> patternCell(int step) const;
    int patternCellAt(juce::Point<float>) const;
    void setPatternSteps(std::uint32_t steps);

    BitVibeProcessor& proc;
    DrumBeat& beat;
    PedalLookAndFeel& laf;

    PedalButton loadButton { "LOAD..." }, demoButton { "DEMO" }, reanalyseButton { "RE-ANALYSE" };
    PedalButton playButton { "PLAY" }, halfButton { "/2" }, doubleButton { "x2" }, useTempoButton { "USE" };
    juce::ComboBox library;
    juce::ToggleButton neuralButton { "NEURAL HELPER" }, pickButton { "PICK TRIGGERS" };
    std::unique_ptr<APVTS::ButtonAttachment> pickAtt;
    juce::ComboBox stepClockBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> stepClockAtt;
    juce::Slider levelKnob;
    std::unique_ptr<APVTS::SliderAttachment> levelAtt;
    juce::ComboBox beatOutBox; // where the beat's sound goes: Auto / Monitor / Main
    std::unique_ptr<APVTS::ComboBoxAttachment> beatOutAtt;

    // the beat pattern: what it does, one-click presets, and a copy of what the beat holds (the grid is drawn from it)
    juce::ComboBox patternActionBox;
    PedalButton patternClearButton { "CLEAR" }, patternBeatsButton { "BEATS" }, patternAndButton { "OFFBEATS" },
        patternBackbeatButton { "BACKBEAT" };
    bitvibe::DrumPattern pattern;
    juce::ParameterAttachment playParam;
    std::array<Row, bitvibe::kNumDrums> rows;

    DrumBeat::Model model;
    juce::String status;
    juce::Array<juce::File> libraryList;
    std::unique_ptr<juce::FileChooser> chooser;
    bitvibe::DrumSettingsArray shownSettings {};
    double viewStart = 0.0;
    bool dragOver = false, updatingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPage)
};
