#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

class PedalLookAndFeel;
class PedalButton;

// BEAT DOG: the green wireframe husky headbanging in time, in the pane under the pedal. Two ways to show it:
//  - the pre-rendered animation (the default): one beat rendered ahead of time in Blender (Tools/beatdog) as a sprite
//    sheet of 32 frames built into the plug-in, so showing it costs one small image draw per frame;
//  - LIVE 3D: the same head drawn in real time from its mesh (BeatDogMesh.h, exported by the same Blender script), at
//    60, 30 or 15 frames a second. Only the lines facing the camera are drawn, each with a soft glow, into an image at
//    the frame rate chosen; the editor's own repaints just show that image.
// The head nods forward on the beats picked with the numbered buttons (all of them to start with) and pulls back smoothly
// before the next. SMOOTH sets how much quicker the nod is: all the way left a sudden headbang, all the way right an
// even bob. SWAY floats the head from side to side, landing on one side at a picked beat and on the other at the next.
// All of it is remembered.
class BeatDog : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    static constexpr int kFrames = 32, kColumns = 8, kTile = 224;
    static constexpr int kMaxBeats = 6; // the longest bar (12/8) has six quarter-note beats

    explicit BeatDog(PedalLookAndFeel&);
    ~BeatDog() override;

    // Where the beat is: phase 0..1 through the current quarter-note beat (0 = on the beat), which beat of the bar it
    // is, the tempo (shown on the left) and the length of the bar in sixteenths. Repaints only what changed.
    void setBeat(float phase, int beatInBar, float bpm = 120.0f, int barSteps = 16);
    // What the live version asks for at its own frame rate, straight from the engine.
    struct Position
    {
        float phase = 0.0f;
        int beatInBar = 0, barSteps = 16;
    };
    std::function<Position()> beatSource;

    void setLive(bool live);  // remembered for next time
    bool isLive() const { return live; }
    void setFrameRate(int fps); // 60, 30 or 15; remembered for next time
    int getFrameRate() const { return fps; }
    using juce::Timer::isTimerRunning;   // for tests: the live version's clock
    using juce::Timer::getTimerInterval;

    // Which beats of the bar the head hits bottom on: bit n = beat n + 1. Remembered.
    void setBeatMask(int mask);
    int getBeatMask() const { return beatMask; }
    void setSmoothness(float amount); // 0 = the headbang (the quickest nod), 1 = an even bob
    float getSmoothness() const { return smoothness; }
    void setSway(float amount);       // 0 = still, 1 = the widest sway
    float getSway() const { return sway; }
    void saveSettings();              // the knobs are saved when let go of, and when the editor closes

    // The motion at a moment of the bar: the beats since the last picked beat and to the next (gap: 0 = none picked), how
    // far from one to the other (0..1), and the float's side (-1..1) and lift (0..1).
    struct Motion
    {
        float since = 0.0f, gap = 1.0f, between = 0.0f, side = 1.0f, lift = 0.0f;
    };
    Motion motionAt(float phase, int beatInBar, int barSteps);
    // The head's pitch for a motion, in radians (snout up positive): the nod forward on the beat and the pull back.
    float headAngle(const Motion&) const;
    int frameFor(const Motion&) const; // the pre-rendered frame that shows it

    int getFrame() const { return frame; }
    bool hasSheet() const { return sheet.isValid(); }
    // Draws one live frame at this moment into the cached image (what the timer does); for tests.
    void renderLiveFrame(float phase, int beatInBar = 0, int barSteps = 16);
    const juce::Image& getLiveImage() const { return liveImage; }

    juce::String getTooltip() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> screenArea() const;
    juce::Rectangle<int> beatButton(int beat) const;
    int beatsInBar() const;
    void buildScanlines();
    void updateControls();
    void drawLiveFrame(const Motion&);
    const float* earsFor(float gap);

    PedalLookAndFeel& laf;
    juce::Image sheet;      // the 32 frames, decoded once
    juce::Image scanlines;  // a faint CRT look over the screen, drawn once per size
    juce::Image liveImage;  // the last live frame
    int frame = 0, beat = 0, barSteps = 16;
    int tempoTenths = 1200; // the tempo shown, in tenths of a BPM
    bool live = false;
    int fps = 60;
    int beatMask = (1 << kMaxBeats) - 1;
    float smoothness = 0.0f, sway = 0.5f;
    bool knobsChanged = false;
    Motion shown, liveShown; // what each version shows now
    float lastBetween = 0.0f;
    int swings = 0;          // picked beats passed: which side the sway heads for
    static constexpr int kEarSteps = 256;
    std::array<float, kEarSteps> ears {}; // the ears' flop through one headbang, for this gap and SMOOTH
    float earsGap = -1.0f, earsSmooth = -1.0f;

    std::unique_ptr<PedalButton> liveButton;
    juce::ComboBox fpsBox;
    juce::Slider smoothKnob, swayKnob;
};
