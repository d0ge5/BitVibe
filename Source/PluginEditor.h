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

// The right-hand column's effect panels are numbered in the order the sequenced sound goes through them. Draws the
// number in a small badge at (x, y) and the title after it (y is the top of the title row).
void drawPanelTitle(juce::Graphics&, int x, int y, int number, const juce::String& title);
inline constexpr int kPanelTitleX = 64; // where a numbered title starts, right of the power button

// A panel's power button: a ToggleButton with this property set is drawn as a pill with a light (lit when on, glowing
// while the property "active" is set, i.e. while the effect is acting on the sound) and ON / OFF.
inline const juce::Identifier powerProperty { "power" }, activeProperty { "active" };
inline void makePowerButton(juce::ToggleButton& b) { b.getProperties().set(powerProperty, true); }
// Sets the glow; repaints only on a change.
inline void setPowerActive(juce::ToggleButton& b, bool active)
{
    if ((bool) b.getProperties().getWithDefault(activeProperty, false) != active)
    {
        b.getProperties().set(activeProperty, active);
        b.repaint();
    }
}
inline constexpr float kOffAlpha = 0.4f; // a switched-off panel's controls
// Switching an effect on with its mix all the way down would change nothing you can hear, so the player's click also
// brings the mix up to a level that can be heard. (Only a click does this: a host or a project keeps its own values.)
inline void raiseMixIfSilent(juce::AudioProcessorValueTreeState& apvts, const juce::String& mixId, float audible)
{
    if (auto* param = apvts.getParameter(mixId); param != nullptr && param->getValue() <= 0.0f)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(audible));
        param->endChangeGesture();
    }
}

// What a sequencer step controls, in one colour each: pitch, filter, sample rate.
inline juce::Colour stepTypeColour(int type)
{
    static const juce::Colour colours[3] { juce::Colour(0xff5aa9ff), juce::Colour(0xff4fd17c), juce::Colour(0xffc58bff) };
    return colours[juce::jlimit(0, 2, type)];
}
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
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
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
    const juce::String& getText() const { return text; }

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

// The input level next to the TRIGGER knob: the bar is how hard you are playing, the line is how hard a pick has to
// be to count, and the bar lights up when a pick is caught.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    void set(float levelDb, float thresholdDb, bool flash);
    void paint(juce::Graphics&) override;

private:
    float level = -100.0f, threshold = -30.0f;
    bool lit = false;
};

// A small picture of the six sequencer steps on the front of the pedal, and the quickest way to set the sequence up:
// one bar per step (its knob position) in the colour of what the step controls, skipped steps drawn as a dash, muted
// ones as a cross, the playing step outlined and the loop section framed. Click a step to see its knob, drag across
// steps to make them the loop section, right-click a step for its menu.
class StepStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Step
    {
        float knob = 0.5f;
        int kind = 2; // 0 = skip, 1 = mute, 2 = plays
        int type = 0; // what it controls: 0 = pitch, 1 = filter, 2 = sample rate
    };
    std::function<void(int step)> onStepClick;
    std::function<void(int first, int last)> onLoopDrag; // while dragging; first <= last
    std::function<void()> onLoopDragEnd;
    std::function<void(int step)> onStepMenu;

    void set(const std::array<Step, 6>& steps, int playing, int loopFrom = -1, int loopTo = -1); // loop: -1 = none
    int stepAt(float x) const; // the step under x (clamped to the strip)
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    std::array<Step, 6> shown {};
    int current = -1;
    int loopLo = -1, loopHi = -1;
    int downStep = -1;
    bool dragged = false, menuClick = false;
};

// On the Alt layer, next to each step's name: P, F and S, what the step controls. A lit letter is the step's own
// choice; with none lit the step follows the SEQ TYPE buttons, and the letter in use is outlined. Clicking the lit
// letter again goes back to following.
class StepTypeTags : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void(int choice)> onPick; // 0 = follow SEQ TYPE, 1..3 = pitch, filter, sample rate
    void set(int own, int effective);       // own: 0..3 as above; effective: 0..2
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    int own = 0, effective = 0;
};

// The quick-start guide: what to do first and where the sound goes. Opens by itself the first time, and with the
// HELP button at the top; a click anywhere closes it.
class HelpOverlay : public juce::Component
{
public:
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override { setVisible(false); }
};

class PatternPanel;
class EqPanel;
class SamplePanel;
class SamplerPage;
class BeatDog;

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
    // The SAMPLER page over the whole window (the sample blend panel's SAMPLER button opens it).
    void showSampler(bool show);
    SamplerPage* getSamplerPage() { return samplerPage.get(); }

    // Shows the DRUMS tab (true) or the pedal (false).
    void showDrums(bool on);

    // The right-hand column in SIMPLE mode (the controls that matter most) or ADVANCED mode (every control).
    // Settings that SIMPLE hides keep working; they are only out of the way. The choice is remembered.
    void setRightMode(bool advanced);
    bool isAdvancedRight() const { return advancedRight; }

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
    void addKnobHelpers(juce::Slider&, const juce::String& paramId); // double-click to reset, value bubble while turning
    void applySize(); // the window size for the page that is showing, at the player's scale
    void applyRightMode();
    // The right-hand column, top to bottom: the beat pattern (when the sequence fires), then the effects in the order the
    // sequenced sound goes through them: sample blend, distortion / fuzz, reverb and (ADVANCED) the EQ. The sample panel
    // changes height with the mode, so everything below it moves.
    int patternTop() const;
    int samplesTop() const;
    juce::Rectangle<int> distArea() const;
    juce::Rectangle<int> delayArea() const;
    juce::Rectangle<int> reverbArea() const;
    int eqTop() const;
    int columnBottom() const;
    void timerCallback() override;
    void updateLiveState();
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
    bitvibe::SeqType stepType(int step) const; // what that step controls (its own type, or the sequencer type)

    Content content { *this };
    LevelMeter triggerMeter;
    StepStrip stepStrip;
    std::unique_ptr<BeatDog> beatDog; // the headbanging husky under the pedal
    PedalButton helpButton { "? HELP" };
    PedalButton exportButton { "EXPORT" }, importButton { "IMPORT" };
    std::unique_ptr<juce::FileChooser> presetChooser;
    void exportPresetFile();
    void importPresetFile();
    PedalButton simpleButton { "SIMPLE" }, advancedButton { "ADVANCED" };
    bool advancedRight = false;
    HelpOverlay helpOverlay;
    float meterDb = -100.0f;
    int seenAttacks = 0;
    double meterFlashUntil = 0.0;
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
    // on the Alt layer, next to each step's name: what the step controls
    std::array<StepTypeTags, kKnobs> stepTypeTags;
    void setStepType(int step, int choice); // 0 = follow SEQ TYPE, 1..3 = pitch, filter, sample rate
    void showStepMenu(int step);
    void setLoopFromStrip(int first, int last);
    bool loopDragging = false;
    int flashStep = -1;        // the step whose knob the strip pointed at, ringed for a moment
    double flashStepUntil = 0.0;

    // global panel
    KnobSlider tempoSlider, inSlider, mixSlider, outSlider, trigSlider;
    std::unique_ptr<APVTS::SliderAttachment> tempoAtt, inAtt, mixAtt, outAtt, trigAtt;
    juce::ToggleButton syncButton { "SYNC" }, holdButton { "HOLD" }, fxTrigButton { "FX ONLY ON TRIGGERS" };
    std::unique_ptr<APVTS::ButtonAttachment> syncAtt, holdAtt, fxTrigAtt;
    juce::ToggleButton seqOnlyButton { "SEQ ONLY" };
    std::unique_ptr<APVTS::ButtonAttachment> seqOnlyAtt;
    juce::ComboBox headroomBox, stutterLenBox;
    juce::ToggleButton loopButton { "LOOP" };
    std::unique_ptr<APVTS::ButtonAttachment> loopAtt;
    juce::ComboBox loopFromBox, loopToBox, loopRepeatsBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> loopFromAtt, loopToAtt, loopRepeatsAtt;

    // the delay on the sequenced sound: time, mix, decay, repeats, low and high cut, and its power button
    KnobSlider delayMixSlider, delayDecaySlider, delayRepeatsSlider, delayLowSlider, delayHighSlider;
    std::unique_ptr<APVTS::SliderAttachment> delayMixAtt, delayDecayAtt, delayRepeatsAtt, delayLowAtt, delayHighAtt;
    juce::ComboBox delayTimeBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> delayTimeAtt;
    juce::ToggleButton delayOnButton { "ON" };
    std::unique_ptr<APVTS::ButtonAttachment> delayOnAtt;

    // reverb on the sequenced sound, and the beat pattern panel below it
    KnobSlider reverbDecaySlider, reverbToneSlider, reverbMixSlider, reverbLevelSlider, reverbLowSlider, reverbHighSlider;
    std::unique_ptr<APVTS::SliderAttachment> reverbDecayAtt, reverbToneAtt, reverbMixAtt, reverbLevelAtt, reverbLowAtt, reverbHighAtt;
    juce::ComboBox reverbTypeBox;
    juce::ToggleButton reverbReverseButton { "REVERSE" }; // each type of reverb remembers its own tick
    juce::ToggleButton reverbOnButton { "ON" };
    std::unique_ptr<APVTS::ButtonAttachment> reverbOnAtt;

    // distortion on the sequenced sound: drive, tone and level, an on switch and a "reverb too" switch
    KnobSlider distDriveSlider, distToneSlider, distLevelSlider;
    std::unique_ptr<APVTS::SliderAttachment> distDriveAtt, distToneAtt, distLevelAtt;
    juce::ToggleButton distOnButton { "ON" }, distReverbButton { "REVERB TOO" };
    juce::ComboBox distTypeBox; // overdrive or fuzz
    std::unique_ptr<APVTS::ComboBoxAttachment> distTypeAtt;
    std::unique_ptr<APVTS::ButtonAttachment> distOnAtt, distReverbAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> reverbTypeAtt;
    std::unique_ptr<SamplePanel> samplePanel;
    std::unique_ptr<SamplerPage> samplerPage; // the SAMPLER, over the whole window when it is open
    std::unique_ptr<EqPanel> eqPanel;
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
