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

// A rotary knob that repaints when it is grabbed or let go, so the look-and-feel can draw it bigger while it is held.
class KnobSlider : public juce::Slider
{
public:
    void mouseDown(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseDown(e);
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp(e);
        repaint();
    }
};

class PatternPanel;
class SamplePanel;

class BitVibeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BitVibeEditor(BitVibeProcessor&);
    ~BitVibeEditor() override;

    void resized() override;

    // Everything the editor shows lives in this pane, which is drawn at its designed size and scaled to the window,
    // so the window can be resized freely (the shape stays fixed).
    juce::Component& getContent() { return content; }

    // Swaps the six front-panel knobs for the six sequencer-step knobs (the pedal's Alt layer).
    void setAltLayer(bool on);

    // Shows the DRUMS tab (true) or the pedal (false).
    void showDrums(bool on);

private:
    static constexpr int kKnobs = 6;
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        KnobSlider front, step;
        std::unique_ptr<APVTS::SliderAttachment> frontAttachment, stepAttachment;
        const char* label = "";
    };

    class Content : public juce::Component
    {
    public:
        explicit Content(BitVibeEditor& o) : owner(o) {}
        void paint(juce::Graphics& g) override { owner.paintContent(g); }

    private:
        BitVibeEditor& owner;
    };

    void paintContent(juce::Graphics&);
    void applySize(); // the window size for the page that is showing, at the player's scale
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

    Content content { *this };
    juce::ComponentBoundsConstrainer constrainer;
    int baseWidth = 0, baseHeight = 0;     // the size the page that is showing is designed at
    float scale = 1.0f;                    // the window size as a multiple of that
    float savedScale = 1.0f;
    double scaleChangedMs = 0.0;
    float backgroundScale = 0.0f;          // the pixel scale the background image was drawn at

    BitVibeProcessor& proc;
    PedalLookAndFeel laf;
    juce::Image background;

    std::array<Knob, kKnobs> knobs;

    // global panel
    KnobSlider tempoSlider, inSlider, mixSlider, outSlider, trigSlider;
    std::unique_ptr<APVTS::SliderAttachment> tempoAtt, inAtt, mixAtt, outAtt, trigAtt;
    juce::ToggleButton syncButton { "SYNC" }, holdButton { "HOLD" }, fxTrigButton { "FX ONLY ON TRIGGERS" };
    std::unique_ptr<APVTS::ButtonAttachment> syncAtt, holdAtt, fxTrigAtt;
    juce::ComboBox headroomBox, stutterLenBox;

    // reverb on the sequenced sound, and the beat pattern panel below it
    KnobSlider reverbToneSlider, reverbMixSlider, reverbLevelSlider, reverbLowSlider, reverbHighSlider;
    std::unique_ptr<APVTS::SliderAttachment> reverbToneAtt, reverbMixAtt, reverbLevelAtt, reverbLowAtt, reverbHighAtt;
    juce::ComboBox reverbTypeBox;

    // distortion on the sequenced sound: drive, tone and level, an on switch and a "reverb too" switch
    KnobSlider distDriveSlider, distToneSlider, distLevelSlider;
    std::unique_ptr<APVTS::SliderAttachment> distDriveAtt, distToneAtt, distLevelAtt;
    juce::ToggleButton distOnButton { "ON" }, distReverbButton { "REVERB TOO" };
    juce::ComboBox distTypeBox; // overdrive or fuzz
    std::unique_ptr<APVTS::ComboBoxAttachment> distTypeAtt;
    std::unique_ptr<APVTS::ButtonAttachment> distOnAtt, distReverbAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> reverbTypeAtt;
    std::unique_ptr<SamplePanel> samplePanel;
    std::unique_ptr<PatternPanel> patternPanel;
    std::unique_ptr<APVTS::ComboBoxAttachment> headroomAtt, stutterLenAtt;

    // footswitches and buttons
    FootSwitch bypassSwitch, tapSwitch;
    PedalButton altButton { "ALT" };
    PedalButton typePitchButton { "PITCH" }, typeFilterButton { "FILTER" }, typeRateButton { "SAMPLE" }; // what the sequencer changes
    void showSeqType();

    // preset bar and expression morph strip (the panel above the pedal face)
    juce::Label presetLabel;
    PedalButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" };
    PedalButton setUpButton { "SET UP" }, setDownButton { "SET DOWN" };
    juce::Slider expressionSlider;
    std::unique_ptr<APVTS::SliderAttachment> expressionAtt;
    juce::ToggleButton morphButton { "MORPH" };
    std::unique_ptr<APVTS::ButtonAttachment> morphAtt;

    juce::ParameterAttachment bypassParam, holdParam, typeParam, tempoParam;
    juce::Label tempoLabel; // the big BPM read-out: click it to type a tempo
    bool tempoEditable = true;

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
