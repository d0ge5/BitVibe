#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class PedalLookAndFeel;
class PedalButton;

// BEAT DOG: the green wireframe husky headbanging in time, in the pane under the pedal. Two ways to show it:
//  - the pre-rendered animation (the default): one beat rendered ahead of time in Blender (Tools/beatdog) as a sprite
//    sheet of 32 frames built into the plug-in, so showing it costs one small image draw per frame;
//  - LIVE 3D: the same head drawn in real time from its mesh (BeatDogMesh.h, exported by the same Blender script), at
//    60, 30 or 15 frames a second. Only the lines facing the camera are drawn, each with a soft glow, into an image at
//    the frame rate chosen; the editor's own repaints just show that image.
// The head hits bottom on every beat. Four lights count the beats of the bar.
class BeatDog : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    static constexpr int kFrames = 32, kColumns = 8, kTile = 224;

    explicit BeatDog(PedalLookAndFeel&);
    ~BeatDog() override;

    // Where the beat is: phase 0..1 through the current beat (0 = on the beat), which beat of the bar it is, and the
    // tempo it runs at (shown on the left). Repaints only what changed.
    void setBeat(float phase, int beatInBar, float bpm = 120.0f);
    // What the live version asks for at its own frame rate: the phase and the beat of the bar, straight from the engine.
    std::function<std::pair<float, int>()> beatSource;

    void setLive(bool live);  // remembered for next time
    bool isLive() const { return live; }
    void setFrameRate(int fps); // 60, 30 or 15; remembered for next time
    int getFrameRate() const { return fps; }
    using juce::Timer::isTimerRunning;   // for tests: the live version's clock
    using juce::Timer::getTimerInterval;

    int getFrame() const { return frame; }
    bool hasSheet() const { return sheet.isValid(); }
    // Draws one live frame at this phase into the cached image (what the timer does); for tests.
    void renderLiveFrame(float phase);
    const juce::Image& getLiveImage() const { return liveImage; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Rectangle<int> screenArea() const;
    void buildScanlines();
    void updateControls();

    PedalLookAndFeel& laf;
    juce::Image sheet;      // the 32 frames, decoded once
    juce::Image scanlines;  // a faint CRT look over the screen, drawn once per size
    juce::Image liveImage;  // the last live frame
    int frame = 0, beat = 0;
    int tempoTenths = 1200; // the tempo shown, in tenths of a BPM
    bool live = false;
    int fps = 60;
    float livePhase = 0.0f;

    std::unique_ptr<PedalButton> liveButton;
    juce::ComboBox fpsBox;
};
