#include "PatternPanel.h"

using namespace juce;
using namespace bitvibe;
using namespace pedalstyle;

namespace
{
// The 16 cells sit in four groups of four (one per beat).
constexpr int kCellX = 12, kCellW = 23, kCellGap = 3, kGroupGap = 5, kCellY = 52, kCellH = 28;
constexpr int kLabelY = 83, kLabelH = 14;
constexpr int kRowY = 20, kRowH = 24;
} // namespace

PatternPanel::PatternPanel(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel) : beat(p.getDrumBeat()), laf(lookAndFeel)
{
    setSize(kWidth, kHeight);

    for (int a = 0; a < kNumDrumActions; ++a)
        actionBox.addItem(drumActionName((DrumAction) a), a + 1);
    actionBox.setLookAndFeel(&laf);
    actionBox.onChange = [this]
    {
        if (updatingControls)
            return;
        auto current = beat.getPattern();
        current.action = (DrumAction) jlimit(0, kNumDrumActions - 1, actionBox.getSelectedId() - 1);
        beat.setPattern(current);
        pattern = current;
        repaint();
    };
    addAndMakeVisible(actionBox);

    clearButton.onClick = [this] { setSteps(patternPreset(0)); };
    beatsButton.onClick = [this] { setSteps(patternPreset(1)); };
    offbeatsButton.onClick = [this] { setSteps(patternPreset(2)); };
    backbeatButton.onClick = [this] { setSteps(patternPreset(3)); };
    randomButton.onClick = [this] { randomise(); };
    for (auto* b : { &clearButton, &beatsButton, &offbeatsButton, &backbeatButton, &randomButton })
        addAndMakeVisible(*b);

    actionBox.setTooltip("PATTERN ACTION: what the pattern does at each lit step: restart the sequence, move it one step, "
                         "fire the stutter, or both. Off = the pattern does nothing.");
    clearButton.setTooltip("CLEAR: switch every step of the pattern off.");
    beatsButton.setTooltip("BEATS: light the first step of every beat (1 2 3 4).");
    offbeatsButton.setTooltip("OFFBEATS: light the 'and' between the beats (1 & 2 & 3 & 4 &).");
    backbeatButton.setTooltip("BACKBEAT: light beats 2 and 4.");
    randomButton.setTooltip("RANDOM: make a new random pattern. Only the beats you have chosen (the numbers under the steps) change; "
                            "press it again for another one.");
    refresh();
}

PatternPanel::~PatternPanel()
{
    actionBox.setLookAndFeel(nullptr);
}

void PatternPanel::resized()
{
    actionBox.setBounds(12, kRowY, 112, kRowH);
    int x = 128;
    for (auto* b : { &clearButton, &beatsButton, &offbeatsButton, &backbeatButton, &randomButton })
    {
        b->setBounds(x, kRowY, 62, kRowH);
        x += 64;
    }
}

Rectangle<float> PatternPanel::cell(int step) const
{
    return { (float) (kCellX + step * (kCellW + kCellGap) + (step / 4) * kGroupGap), (float) kCellY, (float) kCellW, (float) kCellH };
}

Rectangle<float> PatternPanel::beatLabel(int beatIndex) const
{
    const auto first = cell(beatIndex * 4); // under the first step of the beat; the other three show e & a
    return { first.getX(), (float) kLabelY, first.getWidth(), (float) kLabelH };
}

int PatternPanel::cellAt(Point<float> p) const
{
    for (int i = 0; i < kPatternSteps; ++i)
        if (cell(i).expanded((float) kCellGap * 0.5f, 4.0f).contains(p))
            return i;
    return -1;
}

int PatternPanel::beatLabelAt(Point<float> p) const
{
    for (int b = 0; b < 4; ++b)
        if (beatLabel(b).contains(p))
            return b;
    return -1;
}

void PatternPanel::setSteps(std::uint32_t steps)
{
    pattern = beat.getPattern();
    pattern.steps = steps & kPatternMask;
    beat.setPattern(pattern);
    repaint();
}

void PatternPanel::randomise()
{
    pattern = beat.getPattern();
    pattern.steps = patternRandom(pattern.steps, pattern.randomBeats, (uint32_t) Random::getSystemRandom().nextInt());
    beat.setPattern(pattern);
    repaint();
}

void PatternPanel::mouseDown(const MouseEvent& e)
{
    const int step = cellAt(e.position);
    if (step >= 0)
    {
        setSteps(pattern.steps ^ (1u << step)); // click a step to switch it on or off
        return;
    }
    const int b = beatLabelAt(e.position);
    if (b >= 0)
    {
        pattern = beat.getPattern();
        pattern.randomBeats ^= 1u << b; // click a beat number to let RANDOM change it or leave it alone
        beat.setPattern(pattern);
        repaint();
    }
}

String PatternPanel::getTooltip()
{
    const auto p = getMouseXYRelative().toFloat();
    if (cellAt(p) >= 0)
        return "Click a step to switch it on or off. The pattern repeats every bar of the beat and fires at the lit steps.";
    if (beatLabelAt(p) >= 0)
        return "Click a beat number to include it in RANDOM (lit) or keep it as it is (dim).";
    return {};
}

void PatternPanel::refresh()
{
    if (beat.getGeneration() != model.generation || !model.loaded)
        model = beat.getModel();
    pattern = beat.getPattern();
    if (actionBox.getSelectedId() != (int) pattern.action + 1)
    {
        const ScopedValueSetter<bool> guard(updatingControls, true);
        actionBox.setSelectedId((int) pattern.action + 1, dontSendNotification);
    }
    repaint();
}

void PatternPanel::paint(Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setColour(panel);
    g.fillRoundedRectangle(bounds.toFloat(), 10.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 10.0f, 1.0f);
    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText("BEAT PATTERN", 14, 4, 200, 12, Justification::left);

    const int64_t numSteps = patternNumSteps(model.durationSeconds, model.tempoBpm);
    const bool active = pattern.action != DrumAction::Off;

    String text;
    Colour textColour = inkDim;
    if (!model.loaded)
        text = "Load a beat on the DRUMS tab: the pattern follows its tempo";
    else if (numSteps == 0)
        text = "The beat's tempo is not known yet";
    else if (!active)
    {
        text = "Choose what the pattern does";
        textColour = accent;
    }
    else
        text = String(model.tempoBpm, 1) + " BPM, one bar = 16 steps";
    g.setColour(textColour);
    g.setFont(plain(10.0f));
    g.drawText(text, 130, 4, kWidth - 144, 12, Justification::centredRight, true);

    // The step the beat is at, so you can see the pattern go by.
    int playing = -1;
    if (numSteps > 0 && beat.isPlaying() && beat.getPlayingGeneration() == model.generation)
    {
        const double stepSeconds = model.durationSeconds / (double) numSteps;
        playing = (int) (std::floor(beat.getPositionSeconds() / stepSeconds)) % kPatternSteps;
    }

    static constexpr const char* sixteenths[4] = { "", "e", "&", "a" };
    for (int i = 0; i < kPatternSteps; ++i)
    {
        const auto c = cell(i);
        const bool on = ((pattern.steps >> i) & 1u) != 0u;
        const bool beatStart = i % 4 == 0;
        g.setColour(on ? (active ? accent : accent.withAlpha(0.35f)) : (beatStart ? Colour(0xff28282d) : Colour(0xff1d1d21)));
        g.fillRoundedRectangle(c, 4.0f);
        g.setColour(Colour(0xff0a0a0c));
        g.drawRoundedRectangle(c.reduced(0.5f), 4.0f, 1.0f);
        if (i == playing)
        {
            g.setColour(ink);
            g.drawRoundedRectangle(c.expanded(1.5f), 5.0f, 1.6f);
        }
        if (!beatStart)
        {
            g.setColour(inkDim);
            g.setFont(plain(9.5f));
            g.drawText(sixteenths[i % 4], (int) c.getX() - 3, (int) c.getBottom() + 3, (int) c.getWidth() + 6, 12, Justification::centred);
        }
    }

    // Beat numbers double as the RANDOM switches: lit = RANDOM may change this beat.
    for (int b = 0; b < 4; ++b)
    {
        const auto r = beatLabel(b);
        const bool included = ((pattern.randomBeats >> b) & 1u) != 0u;
        g.setColour(included ? accent.withAlpha(0.22f) : Colour(0xff1d1d21));
        g.fillRoundedRectangle(r.reduced(0.0f, 0.5f), 4.0f);
        g.setColour(included ? ink : inkDim);
        g.setFont(bold(10.5f));
        g.drawText(String(b + 1), r.toNearestInt(), Justification::centred);
    }
}
