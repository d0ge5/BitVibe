#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

// Dark, brushed look shared by every control in the editor.
class PedalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
};

// Round metal footswitch with separate press / release callbacks (the tap switch needs both).
class FootSwitch : public juce::Component
{
public:
    std::function<void()> onPress, onRelease;
    bool isDown() const { return down; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    bool down = false;
};

// Small rectangular push button (ALT, sequencer type).
class PedalButton : public juce::Component
{
public:
    explicit PedalButton(juce::String label) : text(std::move(label)) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    std::function<void()> onClick;
    bool lit = false;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::String text;
    bool pressed = false;
};

class BitVibeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BitVibeEditor(BitVibeProcessor&);
    ~BitVibeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Swaps the six front-panel knobs for the six sequencer-step knobs (the pedal's Alt layer).
    void setAltLayer(bool on);

private:
    static constexpr int kKnobs = 6;
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider front, step;
        std::unique_ptr<APVTS::SliderAttachment> frontAttachment, stepAttachment;
        const char* label = "";
    };

    void timerCallback() override;
    void buildBackground();
    void configureKnob(juce::Slider&, int ticks);
    juce::Rectangle<int> knobBounds(int index) const;
    juce::Point<float> knobCentre(int index) const;
    juce::String readoutText(int index) const;
    float rawValue(const char* id) const;
    bitvibe::SeqType seqType() const;

    BitVibeProcessor& proc;
    PedalLookAndFeel laf;
    juce::Image background;

    std::array<Knob, kKnobs> knobs;

    // global panel
    juce::Slider tempoSlider, inSlider, outSlider, trigSlider;
    std::unique_ptr<APVTS::SliderAttachment> tempoAtt, inAtt, outAtt, trigAtt;
    juce::ToggleButton syncButton { "SYNC" }, holdButton { "HOLD" };
    std::unique_ptr<APVTS::ButtonAttachment> syncAtt, holdAtt;
    juce::ComboBox headroomBox, stutterLenBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> headroomAtt, stutterLenAtt;

    // footswitches and buttons
    FootSwitch bypassSwitch, tapSwitch;
    PedalButton altButton { "ALT" }, typeButton { "SEQ TYPE" };

    juce::ParameterAttachment bypassParam, holdParam, typeParam;

    bool altLayer = false;
    bool tapDown = false, holdFromTap = false;
    double tapPressMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitVibeEditor)
};
