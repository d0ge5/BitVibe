#include "StepEditor.h"

using namespace juce;
using namespace bitvibe;
using namespace pedalstyle;

namespace
{
constexpr float kDimAlpha = 0.35f; // steps past the length
constexpr float kFine = 0.2f;      // Shift-drag moves this much of the normal amount
} // namespace

StepEditor::StepEditor(BitVibeProcessor& p, PedalLookAndFeel& laf) : proc(p)
{
    setTitle("STEPS");
    setMouseCursor(MouseCursor::PointingHandCursor);
    loopButton.toggle = true;
    loopButton.setTooltip("LOOP: keep part of the sequence going. Drag across steps on the strip above TEMPO (or right-click a step here) to "
                          "choose them; the box says how many times they play in a row before the sequence goes on. A restart still begins "
                          "at step 1.");
    loopButton.onClick = [this]
    {
        if (auto* param = proc.apvts.getParameter(ids::loopOn))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
            param->endChangeGesture();
        }
    };
    addAndMakeVisible(loopButton);
    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(ids::loopRepeats)))
        repeatsBox.addItemList(choice->choices, 1);
    repeatsBox.setLookAndFeel(&laf);
    repeatsBox.setTooltip("LOOP REPEATS: how many times the loop section plays in a row each time the sequence reaches it; forever keeps it "
                          "going until the next restart.");
    repeatsAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, ids::loopRepeats, repeatsBox);
    addAndMakeVisible(repeatsBox);
}

StepEditor::~StepEditor()
{
    endGestures();
    repeatsBox.setLookAndFeel(nullptr);
}

void StepEditor::resized()
{
    loopButton.setBounds(loopButtonArea());
    repeatsBox.setBounds(repeatsBoxArea());
}

// ------------------------------------------------------------------------------------------
// Parameters

float StepEditor::value(const String& id) const
{
    auto* v = proc.apvts.getRawParameterValue(id);
    return v != nullptr ? v->load() : 0.0f;
}

void StepEditor::setValue(const String& id, float plain)
{
    if (auto* param = proc.apvts.getParameter(id))
    {
        const float normalised = param->convertTo0to1(plain);
        if (std::abs(param->getValue() - normalised) > 1.0e-6f)
            param->setValueNotifyingHost(normalised);
    }
}

// One drag is one change for the host and one undo step: every parameter it may touch is grabbed as it starts.
void StepEditor::beginGestures(const StringArray& paramIds)
{
    endGestures();
    for (const auto& id : paramIds)
        if (auto* param = proc.apvts.getParameter(id))
        {
            param->beginChangeGesture();
            held.push_back(param);
        }
}

void StepEditor::endGestures()
{
    for (auto* param : held)
        param->endChangeGesture();
    held.clear();
}

SeqType StepEditor::typeOf(int step) const
{
    const int own = roundToInt(value(ids::stepType(step)));
    return own <= 0 ? (SeqType) jlimit(0, 2, roundToInt(value(ids::seqType))) : (SeqType) jlimit(0, 2, own - 1);
}

int StepEditor::length() const { return jlimit(1, kNumSteps, roundToInt(value(ids::seqLength))); }

void StepEditor::setLength(int steps) { setValue(ids::seqLength, (float) jlimit(1, kNumSteps, steps)); }

const char* StepEditor::laneTitle(int laneIndex)
{
    static constexpr const char* titles[kNumLanes] { "CRUSH", "LEVEL", "TREM", "DELAY", "REVERB", "STUTTER" };
    return titles[jlimit(0, kNumLanes - 1, laneIndex)];
}

bool StepEditor::laneUsed(int laneIndex) const
{
    const int n = length();
    for (int i = 0; i < n; ++i)
        if (std::abs(value(ids::lane(laneIndex, i)) - kLaneNeutral[(size_t) laneIndex]) > 0.005f)
            return true;
    return false;
}

String StepEditor::stepText(float knob, SeqType type)
{
    const auto step = mapping::decodeStep(knob, type);
    if (step.kind == mapping::Step::Kind::Skip)
        return "SKIP";
    if (step.kind == mapping::Step::Kind::Mute)
        return "MUTE";
    if (type == SeqType::Pitch)
        return step.semitones == 0.0f ? String("DRY") : String(step.semitones > 0 ? "+" : "") + String((int) step.semitones) + " st";
    return String(roundToInt(step.factor * 100.0f)) + "%";
}

// ------------------------------------------------------------------------------------------
// Layout

Rectangle<float> StepEditor::column(int step, Rectangle<float> row)
{
    const float w = row.getWidth() / (float) kNumSteps;
    return { row.getX() + w * (float) step, row.getY(), w, row.getHeight() };
}

int StepEditor::stepAt(float x) { return jlimit(0, kNumSteps - 1, (int) std::floor(x / ((float) kWidth / (float) kNumSteps))); }

float StepEditor::barValueAt(int step, float y) const
{
    const auto bars = barsArea();
    const float v = jlimit(0.0f, 1.0f, (bars.getBottom() - 2.0f - y) / (bars.getHeight() - 4.0f));
    if (typeOf(step) == SeqType::Pitch) // the middle of the semitone it lands on
        return ((float) jlimit(0, 26, (int) (v * 27.0f)) + 0.5f) / 27.0f;
    return v;
}

void StepEditor::setFromY(int step, float y)
{
    if (drag == Drag::bars)
        setValue(ids::step(step), barValueAt(step, y));
    else if (drag == Drag::lane)
    {
        if (lane == (int) Lane::Stutter)
            setValue(ids::lane(lane, step), paintOn ? 1.0f : 0.0f);
        else
        {
            const auto area = laneArea();
            setValue(ids::lane(lane, step), jlimit(0.0f, 1.0f, (area.getBottom() - 2.0f - y) / (area.getHeight() - 4.0f)));
        }
    }
}

// A drag across steps sets every step it passes over, at the height the mouse was at as it crossed it.
void StepEditor::paintAlong(Point<float> from, Point<float> to)
{
    const int a = stepAt(from.x), b = stepAt(to.x);
    if (a == b)
    {
        setFromY(b, to.y);
        return;
    }
    const int dir = b > a ? 1 : -1;
    for (int s = a + dir;; s += dir)
    {
        const float cx = column(s, barsArea()).getCentreX();
        const float t = jlimit(0.0f, 1.0f, (cx - from.x) / (to.x - from.x));
        setFromY(s, from.y + (to.y - from.y) * t);
        if (s == b)
            break;
    }
}

// ------------------------------------------------------------------------------------------
// Live state

void StepEditor::refresh(int playingStep)
{
    playing = playingStep;
    std::vector<float> now;
    now.reserve(200);
    const int n = length();
    now.push_back((float) n);
    now.push_back((float) playing);
    now.push_back((float) lane);
    now.push_back(value(ids::seqType));
    now.push_back(value(ids::loopOn));
    now.push_back(value(ids::loopFrom));
    now.push_back(value(ids::loopTo));
    now.push_back(value(ids::morph));
    now.push_back(Time::getMillisecondCounterHiRes() < flashUntil ? (float) flashStep : -1.0f);
    for (int i = 0; i < kNumSteps; ++i)
    {
        now.push_back(value(ids::step(i)));
        now.push_back(value(ids::stepType(i)));
    }
    for (int l = 0; l < kNumLanes; ++l)
        for (int i = 0; i < kNumSteps; ++i)
            now.push_back(value(ids::lane(l, i)));
    const bool loopOn = value(ids::loopOn) > 0.5f;
    if (loopButton.lit != loopOn)
    {
        loopButton.lit = loopOn;
        loopButton.repaint();
    }
    repeatsBox.setAlpha(loopOn ? 1.0f : kOffAlpha);
    if (now != shown)
    {
        shown = std::move(now);
        repaint();
    }
}

void StepEditor::flash(int step)
{
    flashStep = jlimit(0, kNumSteps - 1, step);
    flashUntil = Time::getMillisecondCounterHiRes() + 1200.0;
    repaint();
}

void StepEditor::setLane(int newLane)
{
    lane = jlimit(0, kNumLanes - 1, newLane);
    repaint();
}

// ------------------------------------------------------------------------------------------
// Words

String StepEditor::describe(Point<float> p) const
{
    if (p.x < 0.0f || p.y < 0.0f)
        return {};
    const int s = stepAt(p.x);
    const String name = "STEP " + String(s + 1);
    if (barsArea().contains(p) || typesArea().contains(p))
    {
        static const char* typeNames[3] { "PITCH", "FILTER", "RATE" };
        const auto type = typeOf(s);
        return name + "  " + typeNames[(int) type] + "  " + stepText(value(ids::step(s)), type) + (s >= length() ? "  (past the end)" : "");
    }
    if (laneArea().contains(p))
    {
        const float v = value(ids::lane(lane, s));
        const String amount = lane == (int) Lane::Stutter ? String(v > 0.5f ? "ON" : "OFF") : String(roundToInt(v * 100.0f)) + "%";
        return name + "  " + laneTitle(lane) + "  " + amount;
    }
    if (numbersArea().contains(p))
        return "DRAG TO SET THE END: " + String(s + 1) + " STEPS";
    return {};
}

String StepEditor::getTooltip()
{
    const auto p = getMouseXYRelative().toFloat();
    const bool inside = getLocalBounds().toFloat().contains(p);
    if (inside && tabsArea().contains(p))
        return "LANES: what else each step does while it plays. CRUSH = fewer bits, LEVEL = how loud, TREM = how much tremolo, "
               "DELAY and REVERB = how much goes into them, STUTTER = fires the stutter. A dot marks a lane in use.";
    if (inside && laneArea().contains(p))
    {
        static const char* what[kNumLanes] { "how much it crushes the bits beyond the BITS knob (bottom = as the knob, top = one bit)",
                                             "how loud it is (top = as it is)",
                                             "how much of the tremolo it gets (top = as the TREMOLO panel says)",
                                             "how much of it goes into the delay (top = all)",
                                             "how much of it goes into the reverb (top = all)",
                                             "whether the stutter fires as it begins (click to switch)" };
        return String(laneTitle(lane)) + " LANE: for each step, " + what[lane] + ". Drag across steps to draw. Double-click a step to put it back.";
    }
    if (inside && (numbersArea().contains(p) || minusButton().contains(p) || plusButton().contains(p) || p.y < 22.0f))
        return "LENGTH: how many steps play, 1 to 16. Click - or +, or drag the END marker under the bars. Steps past the end keep their values.";
    if (inside && typesArea().contains(p))
        return "WHAT EACH STEP CONTROLS: P = pitch, F = filter, R = rate. A lit letter is the step's own choice; an outlined one follows "
               "SEQ TYPE. Click to go round them.";
    const auto type = (SeqType) jlimit(0, 2, roundToInt(value(ids::seqType)));
    const char* what = type == SeqType::Pitch    ? "pitch shift, -12 to +12 semitones (the middle = unshifted)"
                       : type == SeqType::Filter ? "filter cutoff, as a percent of the FILTER knob"
                                                 : "sample rate, as a percent of the SAMPLE RATE knob";
    return String("THE STEPS: drag each bar to set the step's ") + what
           + ". At the bottom it skips, at the top it mutes. Drag across to draw; Shift = fine; double-click = reset; right-click = menu.";
}

// ------------------------------------------------------------------------------------------
// Painting

void StepEditor::paint(Graphics& g)
{
    const int n = length();
    const bool morphOn = value(ids::morph) > 0.5f;
    const bool loopOn = value(ids::loopOn) > 0.5f;
    const int loopLo = jlimit(0, n - 1, jmin(roundToInt(value(ids::loopFrom)), roundToInt(value(ids::loopTo))));
    const int loopHi = jlimit(0, n - 1, jmax(roundToInt(value(ids::loopFrom)), roundToInt(value(ids::loopTo))));
    const int flashing = Time::getMillisecondCounterHiRes() < flashUntil ? flashStep : -1;
    const int hoverStep = hover.x >= 0.0f ? stepAt(hover.x) : -1;

    // header: LENGTH - n +, and what is under the mouse
    g.setColour(ink);
    g.setFont(bold(11.5f));
    g.drawText("LENGTH", Rectangle<float>(0.0f, 0.0f, 54.0f, 20.0f), Justification::centredLeft);
    for (const auto& [box, sign] : { std::pair { minusButton(), "-" }, std::pair { plusButton(), "+" } })
    {
        const bool enabled = sign[0] == '-' ? n > 1 : n < kNumSteps;
        g.setColour(theme.buttonBottom);
        g.fillRoundedRectangle(box, 3.0f);
        g.setColour(theme.buttonEdge);
        g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.0f);
        g.setColour(enabled ? ink : inkDim.withAlpha(0.5f));
        g.setFont(bold(13.0f));
        g.drawText(sign, box, Justification::centred);
    }
    g.setColour(accent);
    g.setFont(bold(14.0f));
    g.drawText(String(n), Rectangle<float>(74.0f, 0.0f, 30.0f, 20.0f), Justification::centred);
    // the loop section: its steps next to the LOOP button
    g.setColour(loopOn ? ink : inkDim.withAlpha(0.5f));
    g.setFont(plain(11.0f));
    g.drawText(loopOn ? String(loopLo + 1) + "-" + String(loopHi + 1) : String("off"), loopTextArea(), Justification::centredLeft);
    g.setColour(inkDim);
    g.setFont(plain(11.0f));
    const String caption = describe(hover);
    g.drawFittedText(caption.isNotEmpty() ? caption : (morphOn ? String("MORPH ON: the steps heard come from STORE A / B") : String()),
                     Rectangle<int>(336, 0, kWidth - 336, 20), Justification::centredRight, 1, 0.7f);

    // the bars
    const auto bars = barsArea();
    g.setColour(theme.well);
    g.fillRoundedRectangle(bars, 4.0f);
    if (loopOn) // the loop section, shaded behind its steps
    {
        g.setColour(accent.withAlpha(0.12f));
        g.fillRect(column(loopLo, bars).getUnion(column(loopHi, bars)));
    }
    const float midY = bars.getBottom() - 2.0f - (bars.getHeight() - 4.0f) * 0.5f;
    g.setColour(theme.gridLine);
    g.fillRect(bars.getX() + 2.0f, midY, bars.getWidth() - 4.0f, 1.0f);
    for (int i = 4; i < kNumSteps; i += 4)
    {
        g.setColour(theme.gridMajor);
        g.fillRect(column(i, bars).getX(), bars.getY() + 3.0f, 1.0f, bars.getHeight() - 6.0f);
    }
    for (int i = 0; i < kNumSteps; ++i)
    {
        const auto col = column(i, bars);
        const auto cell = col.reduced(3.0f, 2.0f);
        const auto type = typeOf(i);
        const float knob = value(ids::step(i));
        const auto decoded = mapping::decodeStep(knob, type);
        const float alpha = (i < n ? 1.0f : kDimAlpha) * (morphOn ? 0.55f : 1.0f);
        const Colour colour = stepTypeColour((int) type);
        if (i == playing)
        {
            g.setColour(theme.cellPlaying.withAlpha(0.5f));
            g.fillRect(col.reduced(1.0f, 2.0f));
        }
        else if (i == hoverStep && bars.contains(hover))
        {
            g.setColour(theme.cell.withAlpha(0.6f));
            g.fillRect(col.reduced(1.0f, 2.0f));
        }
        const float y = bars.getBottom() - 2.0f - (bars.getHeight() - 4.0f) * knob;
        g.setColour(colour.withAlpha(alpha * (i == playing ? 1.0f : 0.85f)));
        if (decoded.kind == mapping::Step::Kind::Skip)
        {
            g.fillRect(cell.getX() + 3.0f, cell.getBottom() - 6.0f, cell.getWidth() - 6.0f, 2.0f);
            g.setFont(plain(11.0f));
            g.drawText("-", cell.withTrimmedBottom(10.0f), Justification::centredBottom);
        }
        else if (decoded.kind == mapping::Step::Kind::Mute)
        {
            const auto x = Rectangle<float>(cell.getX() + 6.0f, cell.getY() + 4.0f, cell.getWidth() - 12.0f, cell.getWidth() - 12.0f);
            g.drawLine(x.getX(), x.getY(), x.getRight(), x.getBottom(), 2.0f);
            g.drawLine(x.getX(), x.getBottom(), x.getRight(), x.getY(), 2.0f);
        }
        else if (type == SeqType::Pitch) // up or down from the unshifted middle
        {
            const float top = jmin(y, midY), bottom = jmax(y, midY);
            g.fillRoundedRectangle(Rectangle<float>(cell.getX(), top - 1.5f, cell.getWidth(), jmax(3.0f, bottom - top + 3.0f)), 2.0f);
            if (decoded.semitones != 0.0f)
            {
                g.setColour(ink.withAlpha(alpha * 0.9f));
                g.setFont(plain(11.0f));
                const auto text = String(decoded.semitones > 0 ? "+" : "") + String((int) decoded.semitones);
                // above the bar (below it going down), or just inside it at the very top and bottom
                const auto label = decoded.semitones > 0 ? Rectangle<float>(col.getX(), jmax(top - 15.0f, bars.getY() + 2.0f), col.getWidth(), 13.0f)
                                                         : Rectangle<float>(col.getX(), jmin(bottom + 2.0f, bars.getBottom() - 15.0f), col.getWidth(), 13.0f);
                g.drawText(text, label, Justification::centred);
            }
        }
        else
        {
            g.fillRoundedRectangle(Rectangle<float>(cell.getX(), y, cell.getWidth(), jmax(3.0f, cell.getBottom() - y)), 2.0f);
        }
        if (i == flashing)
        {
            g.setColour(ink.withAlpha(0.9f));
            g.drawRoundedRectangle(col.reduced(1.0f, 1.0f), 3.0f, 2.0f);
        }
    }
    g.setColour(theme.outline);
    g.drawRoundedRectangle(bars.reduced(0.5f), 4.0f, 1.0f);
    if (loopOn)
    {
        g.setColour(accent.withAlpha(0.85f));
        g.drawRoundedRectangle(column(loopLo, bars).getUnion(column(loopHi, bars)).reduced(1.0f, 1.0f), 3.0f, 1.5f);
    }

    // step numbers and the END marker
    const auto numbers = numbersArea();
    for (int i = 0; i < kNumSteps; ++i)
    {
        g.setColour(i == playing ? ink : inkDim.withAlpha(i < n ? 1.0f : kDimAlpha));
        g.setFont(i == playing ? bold(11.0f) : plain(11.0f));
        g.drawText(String(i + 1), column(i, numbers), Justification::centred);
    }
    {
        const float x = column(n - 1, numbers).getRight();
        g.setColour(accent);
        g.fillRect(x - 1.0f, bars.getY() + 2.0f, 2.0f, numbers.getBottom() - bars.getY() - 2.0f);
        Path tab;
        tab.addTriangle(x - 5.0f, numbers.getBottom(), x + 5.0f, numbers.getBottom(), x, numbers.getBottom() - 6.0f);
        g.fillPath(tab);
    }

    // what each step controls
    const auto types = typesArea();
    static const char* letters[3] { "P", "F", "R" };
    for (int i = 0; i < kNumSteps; ++i)
    {
        const auto r = column(i, types).reduced(4.0f, 0.5f);
        const int own = roundToInt(value(ids::stepType(i)));
        const auto type = typeOf(i);
        const Colour colour = stepTypeColour((int) type).withMultipliedAlpha(i < n ? 1.0f : kDimAlpha + 0.15f);
        if (own > 0)
        {
            g.setColour(colour);
            g.fillRoundedRectangle(r, 3.0f);
        }
        g.setColour(colour);
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
        g.setColour(own > 0 ? theme.onAccent : colour);
        g.setFont(bold(11.0f));
        g.drawText(letters[(int) type], r, Justification::centred);
    }

    // the lane tabs
    const auto tabs = tabsArea();
    const float tabW = tabs.getWidth() / (float) kNumLanes;
    for (int l = 0; l < kNumLanes; ++l)
    {
        const auto r = Rectangle<float>(tabs.getX() + tabW * (float) l, tabs.getY(), tabW, tabs.getHeight()).reduced(2.0f, 0.0f);
        const bool chosen = l == lane;
        g.setColour(chosen ? accent : theme.buttonBottom);
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(chosen ? accent : theme.buttonEdge);
        g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);
        g.setColour(chosen ? theme.onAccent : inkLabel);
        g.setFont(bold(11.0f));
        g.drawText(laneTitle(l), r, Justification::centred);
        if (laneUsed(l))
        {
            g.setColour(chosen ? theme.onAccent : accent);
            g.fillEllipse(r.getRight() - 8.0f, r.getY() + 3.0f, 4.0f, 4.0f);
        }
    }

    // the chosen lane
    const auto area = laneArea();
    g.setColour(theme.well);
    g.fillRoundedRectangle(area, 4.0f);
    for (int i = 4; i < kNumSteps; i += 4)
    {
        g.setColour(theme.gridMajor);
        g.fillRect(column(i, area).getX(), area.getY() + 3.0f, 1.0f, area.getHeight() - 6.0f);
    }
    const float neutral = kLaneNeutral[(size_t) lane];
    if (lane != (int) Lane::Stutter && neutral < 1.0f)
    {
        g.setColour(theme.gridLine);
        g.fillRect(area.getX() + 2.0f, area.getBottom() - 3.0f, area.getWidth() - 4.0f, 1.0f);
    }
    for (int i = 0; i < kNumSteps; ++i)
    {
        const auto col = column(i, area);
        const auto cell = col.reduced(4.0f, 3.0f);
        const float v = value(ids::lane(lane, i));
        const float alpha = i < n ? 1.0f : kDimAlpha;
        if (i == playing)
        {
            g.setColour(theme.cellPlaying.withAlpha(0.5f));
            g.fillRect(col.reduced(1.0f, 2.0f));
        }
        if (lane == (int) Lane::Stutter)
        {
            const auto box = Rectangle<float>(cell.getWidth(), cell.getWidth()).withCentre(cell.getCentre());
            if (v > 0.5f)
            {
                g.setColour(accent.withAlpha(alpha));
                g.fillRoundedRectangle(box, 3.0f);
            }
            g.setColour((v > 0.5f ? accent : theme.cellEdge).withAlpha(alpha));
            g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.0f);
        }
        else
        {
            const float changed = std::abs(v - neutral) > 0.005f ? 1.0f : 0.45f; // a neutral step is drawn faintly
            const float h = jmax(2.0f, cell.getHeight() * v);
            g.setColour(accent.withAlpha(alpha * changed));
            g.fillRoundedRectangle(cell.withTop(cell.getBottom() - h), 2.0f);
        }
    }
    g.setColour(theme.outline);
    g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
}

// ------------------------------------------------------------------------------------------
// The mouse

void StepEditor::mouseDown(const MouseEvent& e)
{
    const auto p = e.position;
    const int s = stepAt(p.x);
    drag = Drag::none;
    lastDrag = p;

    if (e.mods.isPopupMenu())
    {
        if ((barsArea().contains(p) || numbersArea().contains(p) || typesArea().contains(p)) && onStepMenu)
            onStepMenu(s);
        return;
    }
    if (minusButton().contains(p) || plusButton().contains(p))
    {
        beginGestures({ ids::seqLength });
        setLength(length() + (plusButton().contains(p) ? 1 : -1));
        endGestures();
        return;
    }
    if (tabsArea().contains(p))
    {
        setLane((int) ((p.x - tabsArea().getX()) / (tabsArea().getWidth() / (float) kNumLanes)));
        return;
    }
    if (typesArea().contains(p))
    {
        const int own = jlimit(0, 3, roundToInt(value(ids::stepType(s))));
        beginGestures({ ids::stepType(s) });
        setValue(ids::stepType(s), (float) ((own + 1) % 4));
        endGestures();
        return;
    }
    if (numbersArea().contains(p))
    {
        drag = Drag::length;
        beginGestures({ ids::seqLength });
        setLength(s + 1);
        return;
    }
    if (barsArea().contains(p))
    {
        StringArray all;
        for (int i = 0; i < kNumSteps; ++i)
            all.add(ids::step(i));
        beginGestures(all);
        dragStep = s;
        if (e.mods.isShiftDown())
        {
            drag = Drag::fine;
            fineStartValue = value(ids::step(s));
            fineStartY = p.y;
            return;
        }
        drag = Drag::bars;
        setFromY(s, p.y);
        return;
    }
    if (laneArea().contains(p))
    {
        StringArray all;
        for (int i = 0; i < kNumSteps; ++i)
            all.add(ids::lane(lane, i));
        beginGestures(all);
        drag = Drag::lane;
        dragStep = s;
        paintOn = !(value(ids::lane(lane, s)) > 0.5f);
        setFromY(s, p.y);
    }
}

void StepEditor::mouseDrag(const MouseEvent& e)
{
    const auto p = e.position;
    hover = p;
    switch (drag)
    {
        case Drag::bars:
        case Drag::lane:
            paintAlong(lastDrag, p);
            break;
        case Drag::fine:
        {
            const float v = jlimit(0.0f, 1.0f, fineStartValue + (fineStartY - p.y) / (barsArea().getHeight() - 4.0f) * kFine);
            setValue(ids::step(dragStep), v);
            break;
        }
        case Drag::length:
            setLength(stepAt(p.x) + 1);
            break;
        case Drag::none:
            break;
    }
    lastDrag = p;
    repaint();
}

void StepEditor::mouseUp(const MouseEvent&)
{
    endGestures();
    drag = Drag::none;
    dragStep = -1;
}

void StepEditor::mouseDoubleClick(const MouseEvent& e)
{
    const auto p = e.position;
    const int s = stepAt(p.x);
    String id;
    if (barsArea().contains(p))
        id = ids::step(s);
    else if (laneArea().contains(p) && lane != (int) Lane::Stutter)
        id = ids::lane(lane, s);
    if (id.isEmpty())
        return;
    if (auto* param = proc.apvts.getParameter(id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->getDefaultValue());
        param->endChangeGesture();
    }
}

void StepEditor::mouseMove(const MouseEvent& e)
{
    hover = e.position;
    repaint();
}

void StepEditor::mouseExit(const MouseEvent&)
{
    hover = { -1.0f, -1.0f };
    repaint();
}
