#pragma once

#include "EqPanel.h"
#include "PluginEditor.h"

// The SAMPLER: a page over the whole window, opened from the sample blend panel, for making new sounds out of what
// another program on this computer is playing.
//   1 RECORD:   pick the program, RECORD, STOP (up to a minute).
//   2 PICK:     the recording as a waveform; drag across it to pick the part you want (drag its edges to adjust),
//               PLAY to hear that part as it will be saved.
//   3 SHAPE:    distortion or fuzz, sample rate and bits (the pedal's own effects), the EQ (the same panel as the main
//               EQ, with settings of its own) and a volume.
//   4 SAVE:     a name and, if you like, a bank to add it to. It goes in the sounds folder's "Sampled" subfolder.
class SamplerPage : public juce::Component, private juce::Timer
{
public:
    SamplerPage(BitVibeProcessor&, PedalLookAndFeel&);
    ~SamplerPage() override;

    std::function<void()> onClose; // BACK TO THE PEDAL

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    // Uses this audio as the recording, all of it picked (the RECORD button's result; tests call it directly).
    void setRecording(std::vector<float> left, std::vector<float> right);
    void setSelection(juce::int64 start, juce::int64 end); // in samples of the recording
    juce::Range<juce::int64> getSelection() const { return { selStart, selEnd }; }
    // The picked part through the effects, as it would be saved.
    void renderSelection(std::vector<float>& left, std::vector<float>& right, std::vector<float>* beforeEq = nullptr) const;
    void setLooping(bool loop); // LOOP: PLAY goes round and round the picked part
    bool isLooping() const { return loopButton.lit; }
    bool save(const juce::String& name, const juce::String& bank, juce::String& message);

private:
    class Waveform;
    using APVTS = juce::AudioProcessorValueTreeState;

    void timerCallback() override;
    void refreshSources();
    void refreshBanks();
    void toggleRecording();
    void finishRecording();
    void play();
    void stopPlaying();
    juce::String fxSignature() const;
    double seconds(juce::int64 samples) const { return (double) samples / rate; }

    BitVibeProcessor& proc;
    PedalLookAndFeel& laf;

    PedalButton backButton { "BACK TO THE PEDAL" };
    juce::ComboBox sourceBox;
    juce::Array<AppAudioCapture::Source> sources;
    PedalButton refreshButton { "REFRESH" }, recordButton { "RECORD" };
    float meterLevel = 0.0f;

    std::unique_ptr<Waveform> waveform;
    PedalButton playButton { "PLAY" }, stopButton { "STOP" }, loopButton { "REPEAT" }, allButton { "PICK ALL" }, zoomButton { "ZOOM IN" };

    juce::ComboBox distBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> distAtt;
    KnobSlider volumeSlider, driveSlider, toneSlider, levelSlider, rateSlider, bitsSlider;
    std::unique_ptr<APVTS::SliderAttachment> volumeAtt, driveAtt, toneAtt, levelAtt, rateAtt, bitsAtt;
    std::unique_ptr<EqPanel> eq;

    juce::TextEditor nameBox;
    juce::ComboBox bankBox;
    PedalButton saveButton { "SAVE SOUND" };
    juce::String status;
    bool statusIsError = false;

    std::vector<float> recL, recR;
    double rate = 48000.0;
    juce::int64 selStart = 0, selEnd = 0;
    bool previewing = false;
    juce::String previewSignature;
    int signatureCountdown = 0;
    double playStartMs = 0.0;
    bool restartPending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerPage)
};
