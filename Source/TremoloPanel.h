#pragma once

#include "PluginEditor.h"
#include "dsp/Tremolo.h"

// The tremolo's rate over the time it plays, drawn and edited in one graph. Left to right is the CHANGE OVER time
// from the start of a sequence (or stutter); up is a faster rate (a shorter period). The line is the rate, and behind
// it is the tremolo itself, so the cuts can be seen bunching up or spreading out. Drag the left point for the rate at
// the start, the right point for the rate it ends at (level with the left = constant), the middle point to bend the
// curve (the change early or late); a double-click on a point puts it back. A line shows where a playing sequence is.
class TremoloCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TremoloCurve(BitVibeProcessor&);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // the graph's scale: the rate at a height and back (log, kLowHz at the bottom, kHighHz at the top)
    static constexpr float kLowHz = 0.25f, kHighHz = 64.0f;
    float yForHz(float hz) const;
    float hzForY(float y) const;
    enum Handle { none = -1, start = 0, middle = 1, end = 2 };
    juce::Point<float> handlePosition(int handle) const;
    bitvibe::TremoloSettings settings() const; // what the parameters say now

    void setPlayhead(float rampPosition); // -1 = nothing playing

private:
    juce::Rectangle<float> plot() const;
    int handleAt(juce::Point<float>) const;
    float value(const char* id) const;

    BitVibeProcessor& proc;
    juce::ParameterAttachment rateAtt, endAtt, bendAtt;
    int dragging = none, hover = none;
    float playhead = -1.0f;
};

// The TREMOLO panel on the right-hand side: the power button and CHANGE OVER on the title row, the curve on the left
// and RATE, DEPTH and EDGE on the right.
class TremoloPanel : public juce::Component
{
public:
    static constexpr int kWidth = 456, kHeight = 210;

    TremoloPanel(BitVibeProcessor&, PedalLookAndFeel&);
    ~TremoloPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void refresh(); // dims the panel when it is off, lights the power button, moves the playhead (the editor's timer)

    TremoloCurve& getCurve() { return curve; }

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    BitVibeProcessor& proc;
    PedalLookAndFeel& laf;
    juce::ToggleButton onButton { "ON" };
    std::unique_ptr<APVTS::ButtonAttachment> onAtt;
    juce::ComboBox rampBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> rampAtt;
    KnobSlider rateSlider, depthSlider, edgeSlider;
    std::unique_ptr<APVTS::SliderAttachment> rateAtt, depthAtt, edgeAtt;
    TremoloCurve curve;
};
