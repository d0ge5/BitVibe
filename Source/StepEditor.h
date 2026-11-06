#pragma once

#include "PluginEditor.h"

// The sequence on the pedal's Alt layer: up to sixteen steps, each a bar to drag, and under them the lanes, the other
// things a step can do while it plays. From the top:
//   LENGTH [-] 6 [+]      how many steps play (the END marker under the bars can be dragged too)
//   LOOP 13-16 [forever]  the loop section: on or off, which steps (drag across the bars on the strip above TEMPO, or
//                         right-click a step), and how many times it plays in a row
//   the bars              each step's main value, in the colour of what it controls (pitch, filter or rate). Drag up and
//                         down, or across several steps to draw them. Right at the bottom skips the step, right at the
//                         top mutes it. Shift for fine control, a double-click puts a step back, a right-click opens its menu
//   step numbers          with the END marker after the last step that plays
//   P / F / R             what each step controls: a lit letter is its own choice, an outlined one follows SEQ TYPE.
//                         A click moves on to the next (follow, pitch, filter, rate)
//   lane tabs             CRUSH, LEVEL, TREM, DELAY, REVERB, STUTTER; a dot marks a lane that does something
//   the lane              the chosen lane's value for each step (STUTTER: on or off), drawn and dragged like the bars
// Steps past the length are dimmed; they keep their values for when the sequence is made longer.
class StepEditor : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int kWidth = 448, kHeight = 276;

    StepEditor(BitVibeProcessor&, PedalLookAndFeel&);
    ~StepEditor() override;
    void resized() override;

    // From the editor's timer: what the parameters and the playing sequence say now. Repaints only on a change.
    void refresh(int playingStep);
    void flash(int step); // ring a step for a moment (the step strip pointed at it)

    void setLane(int lane); // 0 .. kNumLanes - 1
    int getLane() const { return lane; }

    std::function<void(int step)> onStepMenu; // a right-click on a step

    juce::String getTooltip() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // The layout, for the editor's tests.
    static juce::Rectangle<float> barsArea() { return { 0.0f, 24.0f, (float) kWidth, 124.0f }; }
    static juce::Rectangle<float> numbersArea() { return { 0.0f, 150.0f, (float) kWidth, 14.0f }; }
    static juce::Rectangle<float> typesArea() { return { 0.0f, 166.0f, (float) kWidth, 16.0f }; }
    static juce::Rectangle<float> tabsArea() { return { 0.0f, 190.0f, (float) kWidth, 20.0f }; }
    static juce::Rectangle<float> laneArea() { return { 0.0f, 214.0f, (float) kWidth, 60.0f }; }
    static juce::Rectangle<float> minusButton() { return { 54.0f, 1.0f, 20.0f, 18.0f }; }
    static juce::Rectangle<float> plusButton() { return { 104.0f, 1.0f, 20.0f, 18.0f }; }
    static juce::Rectangle<int> loopButtonArea() { return { 140, 1, 50, 18 }; }
    static juce::Rectangle<float> loopTextArea() { return { 194.0f, 0.0f, 50.0f, 20.0f }; }
    static juce::Rectangle<int> repeatsBoxArea() { return { 246, 1, 84, 18 }; }
    static juce::Rectangle<float> column(int step, juce::Rectangle<float> row);
    static int stepAt(float x); // the step under x, clamped to 0 .. kNumSteps - 1

    // What a step's main value means, for its read-out: SKIP, MUTE, DRY, +7 st, 50%.
    static juce::String stepText(float knob, bitvibe::SeqType type);
    static const char* laneTitle(int lane); // CRUSH, LEVEL, TREM, DELAY, REVERB, STUTTER

private:
    enum class Drag { none, bars, fine, lane, length };

    float value(const juce::String& id) const;
    void setValue(const juce::String& id, float plain);
    void beginGestures(const juce::StringArray& paramIds);
    void endGestures();
    bitvibe::SeqType typeOf(int step) const;
    int length() const;
    void setLength(int steps);
    void setFromY(int step, float y);             // the bars or the lane, whichever is being dragged
    void paintAlong(juce::Point<float> from, juce::Point<float> to);
    float barValueAt(int step, float y) const;    // a knob position for a height in the bars, snapped for pitch steps
    juce::String describe(juce::Point<float>) const; // the caption: what is under the mouse
    bool laneUsed(int laneIndex) const;

    BitVibeProcessor& proc;
    PedalButton loopButton { "LOOP" };
    juce::ComboBox repeatsBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> repeatsAtt;
    int lane = (int) bitvibe::Lane::Level;
    Drag drag = Drag::none;
    int dragStep = -1;
    float fineStartValue = 0.0f, fineStartY = 0.0f;
    bool paintOn = true; // a STUTTER drag sets steps on (or off, when it began on a step that was on)
    juce::Point<float> lastDrag;
    std::vector<juce::RangedAudioParameter*> held;
    juce::Point<float> hover { -1.0f, -1.0f };
    int playing = -1, flashStep = -1;
    double flashUntil = 0.0;
    std::vector<float> shown; // everything painted, to repaint only on a change
};
