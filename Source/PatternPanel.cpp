#include "PatternPanel.h"

using namespace juce;
using namespace bitvibe;
using namespace pedalstyle;

namespace
{
constexpr float kGridX = 12.0f, kGridWidth = 432.0f, kCellGap = 3.0f, kGroupGap = 6.0f, kMaxCellWidth = 23.0f;
constexpr int kCellY = 84, kCellH = 28;
constexpr int kLabelY = 115, kLabelH = 14;
constexpr int kRowY = 20, kRowH = 24, kSignatureY = 50; // the two rows of controls

// What is written under a step: the beat number is a button of its own; the others are the counting syllables.
const char* syllable(int groupSize, int offset)
{
    static const char* four[4] = { "", "e", "&", "a" };
    static const char* six[6] = { "", "", "la", "", "li", "" };
    static const char* eight[8] = { "", "", "e", "", "&", "", "a", "" };
    if (groupSize == 4)
        return four[offset];
    if (groupSize == 6)
        return six[offset];
    return eight[offset % 8];
}
} // namespace

PatternPanel::PatternPanel(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel) : proc(p), beat(p.getDrumBeat()), laf(lookAndFeel)
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

    for (int i = 0; i < kNumTimeSignatures; ++i)
        signatureBox.addItem(timeSignature(i).name, i + 1);
    signatureBox.setLookAndFeel(&laf);
    signatureBox.onChange = [this]
    {
        if (updatingControls)
            return;
        auto current = beat.getPattern();
        current.signature = jlimit(0, kNumTimeSignatures - 1, signatureBox.getSelectedId() - 1);
        beat.setPattern(current);
        pattern = current;
        layoutGrid();
        repaint();
    };
    addAndMakeVisible(signatureBox);

    clearButton.onClick = [this] { setSteps(patternPreset(0, pattern.signature)); };
    beatsButton.onClick = [this] { setSteps(patternPreset(1, pattern.signature)); };
    offbeatsButton.onClick = [this] { setSteps(patternPreset(2, pattern.signature)); };
    backbeatButton.onClick = [this] { setSteps(patternPreset(3, pattern.signature)); };
    randomButton.onClick = [this] { randomise(); };
    restartButton.onClick = [this] { proc.getEngine().restartPatternBar(); };
    addAndMakeVisible(restartButton);
    lockButton.setLookAndFeel(&laf);
    lockAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, ids::patternLock, lockButton);
    addAndMakeVisible(lockButton);
    lockButton.setTooltip("PATTERN LOCK: tick this and only the beat pattern restarts the sequence. How loud or hard you play (and any drum hits) "
                          "no longer restarts it, so the sequence stays in time with the bar. Picked notes still fire the stutter.");
    for (auto* b : { &clearButton, &beatsButton, &offbeatsButton, &backbeatButton, &randomButton })
        addAndMakeVisible(*b);

    actionBox.setTooltip("PATTERN ACTION: what the pattern does at each lit step: restart the sequence, move it one step, "
                         "fire the stutter, or both. Off = the pattern does nothing.");
    signatureBox.setTooltip("TIME SIGNATURE: the shape of the bar. The grid below follows it: one cell per sixteenth note, grouped into beats. "
                            "The pattern repeats every bar, at the tempo of the TEMPO knob (or your DAW's, with SYNC on).");
    restartButton.setTooltip("RESTART BAR: start the bar again from its first step right now (the pattern then carries on from there). "
                             "Handy to line the pattern up with what you are playing. Not used while SYNC follows your DAW, which sets the bar itself.");
    clearButton.setTooltip("CLEAR: switch every step of the pattern off.");
    beatsButton.setTooltip("BEATS: light the first step of every beat.");
    offbeatsButton.setTooltip("OFFBEATS: light the 'and' in the middle of every beat.");
    backbeatButton.setTooltip("BACKBEAT: light every second beat (2 and 4 in 4/4).");
    randomButton.setTooltip("RANDOM: make a new random pattern. Only the beats you have chosen (the numbers under the steps) change; "
                            "press it again for another one.");
    refresh();
}

PatternPanel::~PatternPanel()
{
    actionBox.setLookAndFeel(nullptr);
    signatureBox.setLookAndFeel(nullptr);
    lockButton.setLookAndFeel(nullptr);
}

void PatternPanel::setAdvanced(bool on)
{
    advanced = on;
    for (Component* c : std::initializer_list<Component*> { &signatureBox, &restartButton, &lockButton })
        c->setVisible(on);
    repaint();
}

void PatternPanel::resized()
{
    // first row: what the pattern does, the time signature, restart the bar
    actionBox.setBounds(64, kRowY, 132, kRowH);
    signatureBox.setBounds(262, kRowY, 66, kRowH);
    restartButton.setBounds(336, kRowY, 108, kRowH);
    // second row: ready-made patterns, and the lock
    int x = 12;
    for (auto* b : { &clearButton, &beatsButton, &offbeatsButton, &backbeatButton, &randomButton })
    {
        b->setBounds(x, kSignatureY, 62, 22);
        x += 64;
    }
    lockButton.setBounds(336, kSignatureY, 108, 22);
}

// One cell per sixteenth note of the bar, grouped into the beats of the time signature, as wide as the room allows.
void PatternPanel::layoutGrid()
{
    const auto& ts = timeSignature(pattern.signature);
    shownSignature = pattern.signature;
    const int n = ts.totalSteps;
    const float width = jmin(kMaxCellWidth, (kGridWidth - (float) (n - ts.numGroups) * kCellGap - (float) (ts.numGroups - 1) * kGroupGap) / (float) n);
    float x = kGridX;
    int step = 0;
    for (auto& c : cells)
        c = {};
    for (auto& l : beatLabels)
        l = {};
    for (int g = 0; g < ts.numGroups; ++g)
    {
        const int size = ts.groupSteps[(size_t) g];
        const float groupStart = x;
        for (int s = 0; s < size; ++s, ++step)
        {
            cells[(size_t) step] = { x, (float) kCellY, width, (float) kCellH };
            x += width + (s + 1 < size ? kCellGap : 0.0f);
        }
        beatLabels[(size_t) g] = { groupStart - 1.0f, (float) kLabelY, jmax(width, 18.0f) + 2.0f, (float) kLabelH };
        x += kGroupGap;
    }
}

int PatternPanel::cellAt(Point<float> p) const
{
    const int n = timeSignature(pattern.signature).totalSteps;
    for (int i = 0; i < n; ++i)
        if (cells[(size_t) i].expanded(kCellGap * 0.5f, 4.0f).contains(p))
            return i;
    return -1;
}

int PatternPanel::beatLabelAt(Point<float> p) const
{
    for (int g = 0; g < timeSignature(pattern.signature).numGroups; ++g)
        if (beatLabels[(size_t) g].contains(p))
            return g;
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
    pattern.steps = patternRandom(pattern.steps, pattern.randomBeats, (uint32_t) Random::getSystemRandom().nextInt(), pattern.signature);
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
    const int g = beatLabelAt(e.position);
    if (g >= 0)
    {
        pattern = beat.getPattern();
        pattern.randomBeats ^= 1u << g; // click a beat number to let RANDOM change it or leave it alone
        beat.setPattern(pattern);
        repaint();
    }
}

String PatternPanel::getTooltip()
{
    const auto p = getMouseXYRelative().toFloat();
    if (cellAt(p) >= 0)
        return "Click a step to switch it on or off. The bright outline is the step the bar is at right now. The pattern repeats every bar.";
    if (beatLabelAt(p) >= 0)
        return "Click a beat number to include it in RANDOM (lit) or keep it as it is (dim).";
    return {};
}

void PatternPanel::refresh()
{
    pattern = beat.getPattern();
    if (pattern.signature != shownSignature)
        layoutGrid();
    if (actionBox.getSelectedId() != (int) pattern.action + 1 || signatureBox.getSelectedId() != pattern.signature + 1)
    {
        const ScopedValueSetter<bool> guard(updatingControls, true);
        actionBox.setSelectedId((int) pattern.action + 1, dontSendNotification);
        signatureBox.setSelectedId(pattern.signature + 1, dontSendNotification);
    }
    repaint();
}

void PatternPanel::paint(Graphics& g)
{
    if (pattern.signature != shownSignature)
        layoutGrid();

    const auto bounds = getLocalBounds();
    g.setColour(panel);
    g.fillRoundedRectangle(bounds.toFloat(), 10.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 10.0f, 1.0f);
    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText("BEAT PATTERN", 14, 4, 200, 12, Justification::left);

    const auto& ts = timeSignature(pattern.signature);
    const bool active = pattern.action != DrumAction::Off;

    String text;
    Colour textColour = inkDim;
    if (!active)
    {
        text = "Choose what the pattern does";
        textColour = accent;
    }
    else
    {
        // The pattern only does something if there is a sequence (or a stutter) for it to start.
        const bool sequenceOff = roundToInt(proc.apvts.getRawParameterValue(ids::seqPlay)->load()) == 0;
        const bool stutterOff = mapping::stutterMode(roundToInt(proc.apvts.getRawParameterValue(ids::stutter)->load())).off;
        const bool wantsSequence = pattern.action != DrumAction::Stutter;
        const bool wantsStutter = pattern.action == DrumAction::Stutter || pattern.action == DrumAction::RestartAndStutter;
        if (wantsSequence && sequenceOff && !(wantsStutter && !stutterOff))
        {
            text = "Turn up the SEQUENCER knob: there is no sequence to start";
            textColour = accent;
        }
        else if (pattern.action == DrumAction::Stutter && stutterOff)
        {
            text = "Turn up the STUTTER knob: there is no stutter to fire";
            textColour = accent;
        }
        else
            text = String(proc.getEffectiveBpm(), 1) + " BPM, " + ts.name + ", one bar = " + String(ts.totalSteps) + " steps";
    }
    g.setColour(textColour);
    g.setFont(plain(10.0f));
    g.drawText(text, 130, 4, kWidth - 144, 12, Justification::centredRight, true);

    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText("ACTION", 14, kRowY, 50, kRowH, Justification::centredLeft);
    if (advanced)
        g.drawText("TIME SIG.", 204, kRowY, 58, kRowH, Justification::centredLeft);
    else
    {
        // what the hidden controls are set to, when it matters
        String hidden = String(ts.name) + (ts.totalSteps != 16 ? " bar" : "");
        if (proc.apvts.getRawParameterValue(ids::patternLock)->load() > 0.5f)
            hidden << ", PATTERN LOCK on";
        g.setColour(inkDim);
        g.setFont(plain(10.0f));
        g.drawText(hidden + " (ADVANCED to change)", 204, kRowY, 240, kRowH, Justification::centredLeft, true);
    }

    // The step the bar is at right now (the engine's own clock), so it is easy to see where a step sits in time.
    const int playing = jlimit(0, ts.totalSteps - 1, proc.getEngine().getPatternStep());

    int step = 0;
    for (int group = 0; group < ts.numGroups; ++group)
    {
        const int size = ts.groupSteps[(size_t) group];
        for (int s = 0; s < size; ++s, ++step)
        {
            const auto c = cells[(size_t) step];
            const bool on = ((pattern.steps >> step) & 1u) != 0u;
            const bool current = step == playing;
            Colour fill = on ? (active ? accent : accent.withAlpha(0.35f)) : (s == 0 ? Colour(0xff28282d) : Colour(0xff1d1d21));
            if (current)
                fill = on ? fill.brighter(0.5f) : Colour(0xff4a4a52);
            g.setColour(fill);
            g.fillRoundedRectangle(c, 4.0f);
            g.setColour(Colour(0xff0a0a0c));
            g.drawRoundedRectangle(c.reduced(0.5f), 4.0f, 1.0f);
            if (current)
            {
                g.setColour(ink);
                g.drawRoundedRectangle(c.expanded(1.5f), 5.0f, 2.0f);
            }
            if (s > 0)
            {
                g.setColour(inkDim);
                g.setFont(plain(9.5f));
                g.drawText(syllable(size, s), (int) c.getX() - 3, kLabelY, (int) c.getWidth() + 6, kLabelH, Justification::centred);
            }
        }
    }

    // Beat numbers double as the RANDOM switches: lit = RANDOM may change this beat.
    for (int b = 0; b < ts.numGroups; ++b)
    {
        const auto r = beatLabels[(size_t) b];
        const bool included = ((pattern.randomBeats >> b) & 1u) != 0u;
        g.setColour(included ? accent.withAlpha(0.22f) : Colour(0xff1d1d21));
        g.fillRoundedRectangle(r.reduced(0.0f, 0.5f), 4.0f);
        g.setColour(included ? ink : inkDim);
        g.setFont(bold(10.5f));
        g.drawText(String(b + 1), r.toNearestInt(), Justification::centred);
    }
}
