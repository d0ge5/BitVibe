#pragma once

#include "PluginEditor.h"

// The SAMPLE BLEND panel: pick the list of sounds the sequencer chooses from at random (or load a saved sound
// bank), how much of the chosen sound is mixed into the sequence, and how often a step gets one. What is picked
// and where the knobs are set is remembered between sessions (see SampleBank).
//
// While the sequence has a loop section, MAIN and LOOP buttons choose which steps the panel sets up: the loop section
// can have a list of its own (until then it plays the main one) and, with OWN LEVELS, volumes and chance of its own.
class SamplePanel : public juce::Component
{
public:
    static constexpr int kWidth = 456, kHeight = 210;

    SamplePanel(BitVibeProcessor&, PedalLookAndFeel&);
    ~SamplePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Updates the buttons and the bank list (called by the editor's timer while the page is shown).
    void refresh();
    void parentHierarchyChanged() override;

    // SIMPLE hides the bank buttons, the sounds folder and the volume range row (and the panel gets shorter).
    void setAdvanced(bool advanced);
    int currentHeight() const { return advanced ? kHeight : 112; }

    // Which steps the panel sets up: SampleBank::kMain or kLoop (only while the sequence has a loop section).
    void showSection(int newSection);

    std::function<void()> onOpenSampler; // the SAMPLER button

    // With no sound files at all, the panel says so and shows OPEN SOUNDS FOLDER (in SIMPLE too). It looks in the
    // folders again every couple of seconds (or now), and the first sounds found are picked, as on a first run.
    bool hasNoSounds() const { return numAvailable == 0; }
    void lookForNewSounds(bool now = false);

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void showChooser();
    void openFolder();
    void askBankName();
    void askDeleteBank();
    void rememberKnobs();
    void previewBoth();
    void previewAt(float volumeDb);
    float rawValue(const char* id) const;
    bool editingLoop() const { return section == SampleBank::kLoop; }
    bool loopOwnLevels() const;
    // The parameter each knob is set with: the loop section's own ones while they are in use.
    const char* mixId() const;
    const char* chanceId() const;
    const char* baseId() const;
    const char* maxId() const;
    void attachKnobs();

    BitVibeProcessor& proc;
    SampleBank& bank;
    PedalLookAndFeel& laf;

    juce::ComboBox bankBox;
    PedalButton saveButton { "SAVE" }, deleteButton { "DELETE" };
    PedalButton chooseButton { "CHOOSE SOUNDS..." }, folderButton { "OPEN SOUNDS FOLDER" };
    KnobSlider mixSlider, chanceSlider, baseSlider, maxSlider, lengthSlider;
    std::unique_ptr<APVTS::SliderAttachment> lengthAtt;
    std::unique_ptr<APVTS::SliderAttachment> mixAtt, chanceAtt, baseAtt, maxAtt;
    PedalButton previewButton { "PREVIEW" };
    PedalButton mainButton { "MAIN" }, loopButton { "LOOP" };
    PedalButton samplerButton { "SAMPLER" };
    juce::ToggleButton ownLevelsButton { "OWN LEVELS FOR THE LOOP" };
    std::unique_ptr<APVTS::ButtonAttachment> ownLevelsAtt;
    int section = SampleBank::kMain;
    bool knobsOnLoop = false; // the knobs are attached to the loop section's own parameters
    bool loopShown = false;
    juce::ToggleButton onButton { "ON" };
    std::unique_ptr<APVTS::ButtonAttachment> onAtt;
    std::uint32_t previewPick = 0; // which of the picked sounds the preview plays (new each time the player starts a preview)
    int numAvailable = 0;
    double lastScanMs = 0.0;
    juce::String shownButtonText, shownBankState;
    juce::StringArray shownBanks;
    bool updatingControls = false;
    bool advanced = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePanel)
};
