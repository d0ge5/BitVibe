#pragma once

#include "PluginEditor.h"
#include "dsp/Fft.h"

// The EQ panel (ADVANCED mode, above the beat pattern): a graph of the eight bands and the curve they make, with a
// numbered point per band to drag (left/right = frequency, up/down = gain, the mouse wheel = Q, a double-click switches
// the band on or off), and below it the settings of the band that is selected and the EQ's output level. Behind the
// curve is the spectrum of the sound before the EQ (grey) and after it (orange). CAPTURE keeps the last few seconds
// of the sound reaching the EQ as a clip, the length box sets how much of its end is used, PLAY CLIP loops it through
// the EQ in place of the live sound, and SAVE CLIP writes it to a WAV file.
class EqPanel : public juce::Component
{
public:
    static constexpr int kWidth = 456, kHeight = 196;

    EqPanel(BitVibeProcessor&, PedalLookAndFeel&);
    ~EqPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    // Updates the spectrum and repaints (called by the editor's timer while the panel is shown).
    void refresh();

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void selectBand(int band);
    std::array<bitvibe::EqBand, bitvibe::kNumEqBands> currentBands() const;
    juce::Rectangle<float> graphArea() const;
    float xForHz(float hz) const;
    float hzForX(float x) const;
    float yForDb(float db) const;
    float dbForY(float y) const;
    juce::Point<float> bandPoint(const bitvibe::EqBand&) const;
    int bandAt(juce::Point<float>) const;
    void setParam(const juce::String& id, float value);
    float rawValue(const juce::String& id) const;
    void updateSpectrum();
    void updateClipButtons();
    void saveClip();
    float yForLevel(float dbfs) const;

    BitVibeProcessor& proc;
    PedalLookAndFeel& laf;

    juce::ToggleButton onButton { "ON" }, bandOnButton { "BAND ON" };
    std::unique_ptr<APVTS::ButtonAttachment> onAtt, bandOnAtt;
    juce::ComboBox typeBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> typeAtt;
    std::array<std::unique_ptr<PedalButton>, bitvibe::kNumEqBands> bandButtons;
    KnobSlider freqSlider, gainSlider, qSlider, outputSlider;
    std::unique_ptr<APVTS::SliderAttachment> freqAtt, gainAtt, qAtt, outputAtt;

    PedalButton captureButton { "CAPTURE" }, playButton { "PLAY CLIP" }, saveClipButton { "SAVE CLIP" };
    juce::ComboBox lengthBox;
    std::unique_ptr<juce::FileChooser> clipChooser;
    juce::String clipStatus;

    // the spectrum: one level (dBFS) per column of the graph, before and after the EQ
    static constexpr int kFftSize = 4096, kColumns = 144;
    bitvibe::Fft fft { (size_t) kFftSize };
    std::vector<float> scopeBefore, scopeAfter, frame;
    std::vector<double> power;
    std::array<float, kColumns> levelBefore {}, levelAfter {};

    int selected = 3;
    int dragging = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqPanel)
};
