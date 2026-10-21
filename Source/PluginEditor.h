#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class DrumPage;

// Colours and fonts shared by the pedal page and the DRUMS page.
namespace pedalstyle
{
inline const juce::Colour ink { 0xfff2f0e6 };    // engraved text
inline const juce::Colour inkDim { 0xff9a988f };
inline const juce::Colour accent { 0xffffa31a }; // Alt layer / lit LEDs
inline const juce::Colour red { 0xffff3b30 };
inline const juce::Colour panel { 0xff17171a };
inline const juce::Colour panelEdge { 0xff2c2c31 };
inline juce::Font bold(float h) { return juce::Font(juce::FontOptions(h, juce::Font::bold)); }
inline juce::Font plain(float h) { return juce::Font(juce::FontOptions(h, juce::Font::plain)); }
} // namespace pedalstyle

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
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
};

// Round metal footswitch with separate press / release callbacks (the tap switch needs both).
class FootSwitch : public juce::Component, public juce::SettableTooltipClient
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
class PedalButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PedalButton(juce::String label) : text(std::move(label)) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    std::function<void()> onClick;
    bool lit = false;
    void setText(juce::String newText)
    {
        text = std::move(newText);
        repaint();
    }

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

    // Shows the DRUMS tab (true) or the pedal (false).
    void showDrums(bool on);

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
    void setTooltips();        // plain-language hover text for every control
    void updateStepTooltips(); // the text of the step knobs depends on the sequencer type
    void stepProgram(int direction);
    void refreshPresetName();
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
    juce::Slider tempoSlider, inSlider, mixSlider, outSlider, trigSlider;
    std::unique_ptr<APVTS::SliderAttachment> tempoAtt, inAtt, mixAtt, outAtt, trigAtt;
    juce::ToggleButton syncButton { "SYNC" }, holdButton { "HOLD" }, fxTrigButton { "FX ONLY ON TRIGGERS" };
    std::unique_ptr<APVTS::ButtonAttachment> syncAtt, holdAtt, fxTrigAtt;
    juce::ComboBox headroomBox, stutterLenBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> headroomAtt, stutterLenAtt;

    // footswitches and buttons
    FootSwitch bypassSwitch, tapSwitch;
    PedalButton altButton { "ALT" }, typeButton { "SEQ TYPE" };

    // preset bar and expression morph strip (the panel above the pedal face)
    juce::Label presetLabel;
    PedalButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" };
    PedalButton setUpButton { "SET UP" }, setDownButton { "SET DOWN" };
    juce::Slider expressionSlider;
    std::unique_ptr<APVTS::SliderAttachment> expressionAtt;
    juce::ToggleButton morphButton { "MORPH" };
    std::unique_ptr<APVTS::ButtonAttachment> morphAtt;

    juce::ParameterAttachment bypassParam, holdParam, typeParam;

    int shownProgram = -1;
    juce::String shownName;
    double flashUpUntil = 0.0, flashDownUntil = 0.0, flashSaveUntil = 0.0;

    // tabs: the pedal and the DRUMS page
    PedalButton pedalTab { "PEDAL" }, drumsTab { "DRUMS" };
    std::unique_ptr<DrumPage> drumPage;
    std::vector<juce::Component*> pedalComponents; // hidden while the DRUMS page is shown
    bool drumsShown = false;

    juce::TooltipWindow tooltipWindow { this, 600 }; // shows setTooltip() text after a short hover
    int tooltipSeqType = -1;

    bool altLayer = false;
    bool tapDown = false, holdFromTap = false;
    double tapPressMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitVibeEditor)
};
