#include "TremoloPanel.h"

using namespace juce;
using namespace bitvibe;
using namespace pedalstyle;

namespace
{
constexpr float kHandleRadius = 5.0f;

RangedAudioParameter& param(BitVibeProcessor& p, const char* id) { return *p.apvts.getParameter(id); }

String hzText(float hz) { return String(hz, hz < 10.0f ? 2 : 1) + " Hz"; }
} // namespace

// ------------------------------------------------------------------------------------------
// The curve

TremoloCurve::TremoloCurve(BitVibeProcessor& p)
    : proc(p),
      rateAtt(param(p, ids::tremRate), [this](float) { repaint(); }),
      endAtt(param(p, ids::tremEnd), [this](float) { repaint(); }),
      bendAtt(param(p, ids::tremBend), [this](float) { repaint(); })
{
    setTooltip("TREMOLO CURVE: the tremolo's rate over the CHANGE OVER time from the start of a sequence or stutter (up = faster, "
               "a shorter period). Drag the left point for the start rate, the right point for the end rate (level = constant), "
               "the middle point to bend the change. Double-click a point to put it back.");
    setMouseCursor(MouseCursor::PointingHandCursor);
}

float TremoloCurve::value(const char* id) const { return proc.apvts.getRawParameterValue(id)->load(); }

TremoloSettings TremoloCurve::settings() const
{
    TremoloSettings s;
    s.rateHz = value(ids::tremRate);
    s.depth = value(ids::tremDepth);
    s.edge = value(ids::tremEdge);
    s.endOctaves = value(ids::tremEnd);
    s.bend = value(ids::tremBend);
    s.rampBeats = kTremoloRampBeats[(size_t) jlimit(0, (int) kTremoloRampBeats.size() - 1, roundToInt(value(ids::tremRamp)))];
    return s;
}

Rectangle<float> TremoloCurve::plot() const { return getLocalBounds().toFloat().reduced(kHandleRadius + 2.0f, kHandleRadius + 2.0f); }

float TremoloCurve::yForHz(float hz) const
{
    const auto r = plot();
    const float t = (std::log2(jlimit(kLowHz, kHighHz, hz)) - std::log2(kLowHz)) / (std::log2(kHighHz) - std::log2(kLowHz));
    return r.getBottom() - t * r.getHeight();
}

float TremoloCurve::hzForY(float y) const
{
    const auto r = plot();
    const float t = jlimit(0.0f, 1.0f, (r.getBottom() - y) / r.getHeight());
    return std::exp2(std::log2(kLowHz) + t * (std::log2(kHighHz) - std::log2(kLowHz)));
}

Point<float> TremoloCurve::handlePosition(int handle) const
{
    const auto r = plot();
    const auto s = settings();
    const float u = handle == start ? 0.0f : (handle == middle ? 0.5f : 1.0f);
    return { r.getX() + u * r.getWidth(), yForHz(tremoloRateHz(s, u)) };
}

int TremoloCurve::handleAt(Point<float> at) const
{
    const bool changes = std::abs(settings().endOctaves) > 0.02f; // a constant rate has no bend to drag
    int best = none;
    float bestDistance = 10.0f;
    for (const int h : { start, end, middle })
    {
        if (h == middle && !changes)
            continue;
        const float d = handlePosition(h).getDistanceFrom(at);
        if (d < bestDistance)
        {
            best = h;
            bestDistance = d;
        }
    }
    return best;
}

void TremoloCurve::setPlayhead(float rampPosition)
{
    if (std::abs(rampPosition - playhead) > 0.002f)
    {
        playhead = rampPosition;
        repaint();
    }
}

void TremoloCurve::paint(Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto r = plot();
    const auto s = settings();
    g.setColour(theme.well);
    g.fillRoundedRectangle(bounds, 4.0f);

    // a line at 1, 4 and 16 Hz, and one at each beat
    g.setFont(plain(11.0f));
    for (const float hz : { 1.0f, 4.0f, 16.0f })
    {
        const float y = yForHz(hz);
        g.setColour(theme.gridLine);
        g.drawHorizontalLine(roundToInt(y), bounds.getX(), bounds.getRight());
        g.setColour(inkDim.withAlpha(0.6f));
        g.drawText(String((int) hz) + " Hz", Rectangle<float>(bounds.getRight() - 42.0f, y - 13.0f, 38.0f, 12.0f).toNearestInt(),
                   Justification::right);
    }
    g.setColour(theme.gridLine);
    for (int beat = 1; beat < s.rampBeats; ++beat)
        g.drawVerticalLine(roundToInt(r.getX() + r.getWidth() * (float) beat / (float) s.rampBeats), bounds.getY(), bounds.getBottom());

    // the tremolo itself over the change time, at the tempo: where the volume dips, the cuts bunch up or spread out
    {
        const double bpm = jmax(20.0, (double) proc.getEffectiveBpm());
        const double seconds = (double) s.rampBeats * 60.0 / bpm;
        const int columns = jmax(1, roundToInt(r.getWidth()));
        constexpr int kPerColumn = 12;
        const double dt = seconds / (double) (columns * kPerColumn);
        double phase = 0.0;
        Path wave; // the top of the volume in each column, filled down to the bottom
        wave.startNewSubPath(r.getX(), r.getBottom());
        for (int x = 0; x < columns; ++x)
        {
            float hi = 0.0f;
            for (int k = 0; k < kPerColumn; ++k)
            {
                const float u = (float) (x * kPerColumn + k) / (float) (columns * kPerColumn);
                const float gain = tremoloGain((float) phase, s.depth, s.edge);
                hi = jmax(hi, gain);
                phase += (double) tremoloRateHz(s, u) * dt;
                phase -= std::floor(phase);
            }
            wave.lineTo(r.getX() + (float) x + 0.5f, r.getBottom() - hi * r.getHeight() * 0.8f);
        }
        wave.lineTo(r.getRight(), r.getBottom());
        wave.closeSubPath();
        g.setColour(accent.withAlpha(0.13f));
        g.fillPath(wave);
        g.setColour(accent.withAlpha(0.35f));
        g.strokePath(wave, PathStrokeType(1.0f));
    }

    // where a playing sequence is
    if (playhead >= 0.0f)
    {
        g.setColour(ink.withAlpha(0.7f));
        g.drawVerticalLine(roundToInt(r.getX() + r.getWidth() * playhead), bounds.getY() + 2.0f, bounds.getBottom() - 2.0f);
    }

    // the rate
    Path line;
    for (int i = 0; i <= 64; ++i)
    {
        const float u = (float) i / 64.0f;
        const Point<float> pt(r.getX() + u * r.getWidth(), yForHz(tremoloRateHz(s, u)));
        if (i == 0)
            line.startNewSubPath(pt);
        else
            line.lineTo(pt);
    }
    g.setColour(accent);
    g.strokePath(line, PathStrokeType(2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // what it does, in words
    const String change = std::abs(s.endOctaves) < 0.02f
                              ? String("CONSTANT")
                              : (s.endOctaves > 0.0f ? "SPEEDS UP " : "SLOWS DOWN ") + String(std::exp2(std::abs(s.endOctaves)), 1) + "x";
    g.setColour(ink);
    g.setFont(bold(11.0f));
    g.drawText(change, Rectangle<float>(r.getX(), bounds.getY() + 2.0f, r.getWidth(), 13.0f).toNearestInt(), Justification::centred);

    // the points to drag
    for (const int h : { start, middle, end })
    {
        if (h == middle && std::abs(s.endOctaves) <= 0.02f)
            continue;
        const auto c = handlePosition(h);
        const bool lit = h == dragging || h == hover;
        const float rad = kHandleRadius + (lit ? 1.5f : 0.0f);
        g.setColour(h == dragging ? accent : theme.well);
        g.fillEllipse(c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour(accent);
        g.drawEllipse(c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, 1.6f);
    }
    g.setColour(theme.outline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
}

void TremoloCurve::mouseMove(const MouseEvent& e)
{
    const int h = handleAt(e.position);
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void TremoloCurve::mouseExit(const MouseEvent&)
{
    hover = none;
    repaint();
}

void TremoloCurve::mouseDown(const MouseEvent& e)
{
    dragging = handleAt(e.position);
    if (dragging == start)
    {
        rateAtt.beginGesture();
        endAtt.beginGesture(); // the end point stays where it is
    }
    else if (dragging == end)
        endAtt.beginGesture();
    else if (dragging == middle)
        bendAtt.beginGesture();
    repaint();
}

void TremoloCurve::mouseDrag(const MouseEvent& e)
{
    const auto s = settings();
    const float hz = hzForY(e.position.y);
    if (dragging == start)
    {
        const float endHz = tremoloRateHz(s, 1.0f);
        const float rate = jlimit(kTremoloMinHz, kTremoloMaxHz, hz);
        rateAtt.setValueAsPartOfGesture(rate);
        endAtt.setValueAsPartOfGesture(jlimit(-kTremoloMaxOctaves, kTremoloMaxOctaves, std::log2(endHz / rate)));
    }
    else if (dragging == end)
    {
        float octaves = jlimit(-kTremoloMaxOctaves, kTremoloMaxOctaves, std::log2(hz / s.rateHz));
        if (std::abs(octaves) < 0.08f)
            octaves = 0.0f; // level with the start: constant
        endAtt.setValueAsPartOfGesture(octaves);
    }
    else if (dragging == middle && std::abs(s.endOctaves) > 0.02f)
    {
        // the bend that takes the curve through the mouse half-way along: 0.5 ^ 2^(-2 bend) = how far the change has got
        const float along = jlimit(0.03f, 0.97f, std::log2(hz / s.rateHz) / s.endOctaves);
        const float bend = -0.5f * std::log2(std::log(along) / std::log(0.5f));
        bendAtt.setValueAsPartOfGesture(jlimit(-1.0f, 1.0f, bend));
    }
    repaint();
}

void TremoloCurve::mouseUp(const MouseEvent&)
{
    if (dragging == start)
    {
        rateAtt.endGesture();
        endAtt.endGesture();
    }
    else if (dragging == end)
        endAtt.endGesture();
    else if (dragging == middle)
        bendAtt.endGesture();
    dragging = none;
    repaint();
}

void TremoloCurve::mouseDoubleClick(const MouseEvent& e)
{
    // a point goes back to where it started; elsewhere, the rate becomes constant again
    const int h = handleAt(e.position);
    if (h == start)
        rateAtt.setValueAsCompleteGesture(param(proc, ids::tremRate).convertFrom0to1(param(proc, ids::tremRate).getDefaultValue()));
    else if (h == middle)
        bendAtt.setValueAsCompleteGesture(0.0f);
    else
    {
        endAtt.setValueAsCompleteGesture(0.0f);
        bendAtt.setValueAsCompleteGesture(0.0f);
    }
}

// ------------------------------------------------------------------------------------------
// The panel

TremoloPanel::TremoloPanel(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel) : proc(p), laf(lookAndFeel), curve(p)
{
    onButton.setLookAndFeel(&laf);
    makePowerButton(onButton);
    onAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::tremOn, onButton);
    onButton.setTooltip("TREMOLO ON: cuts the sequenced sound in and out while a sequence or stutter plays; your clean signal never. "
                        "The light glows while it acts.");
    addAndMakeVisible(onButton);

    for (int i = 0; i < (int) kTremoloRampBeats.size(); ++i)
        rampBox.addItem(String(kTremoloRampBeats[(size_t) i]) + (kTremoloRampBeats[(size_t) i] == 1 ? " beat" : " beats"), i + 1);
    rampBox.setLookAndFeel(&laf);
    rampAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::tremRamp, rampBox);
    rampBox.onChange = [this] { repaint(); };
    rampBox.setTooltip("TREMOLO CHANGE OVER: how long the rate takes to get from the start of the curve to its end, in beats of the "
                       "tempo, from the start of each sequence or stutter. After that it stays at the end rate.");
    addAndMakeVisible(rampBox);

    struct Knob
    {
        KnobSlider* slider;
        std::unique_ptr<APVTS::SliderAttachment>* att;
        const char* id;
        const char* tip;
    };
    for (const auto& k : { Knob { &rateSlider, &rateAtt, ids::tremRate,
                                  "TREMOLO RATE: how fast it cuts in and out, 0.5 to 20 times a second. It moves the whole curve; the "
                                  "curve's left point moves only the start." },
                           Knob { &depthSlider, &depthAtt, ids::tremDepth,
                                  "TREMOLO DEPTH: how far the volume drops each time; all the way right, it cuts right out." },
                           Knob { &edgeSlider, &edgeAtt, ids::tremEdge,
                                  "TREMOLO EDGE: how hard it cuts: left, a smooth swell; right, a hard chop on and off." } })
    {
        auto& s = *k.slider;
        s.setSliderStyle(Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        s.setMouseDragSensitivity(180);
        s.setLookAndFeel(&laf);
        *k.att = std::make_unique<APVTS::SliderAttachment>(proc.apvts, k.id, s);
        auto& prm = param(proc, k.id);
        s.setDoubleClickReturnValue(true, (double) prm.convertFrom0to1(prm.getDefaultValue()));
        s.setTooltip(k.tip);
        s.onValueChange = [this] { repaint(); };
        addAndMakeVisible(s);
    }
    addAndMakeVisible(curve);
}

TremoloPanel::~TremoloPanel()
{
    for (Component* c : std::initializer_list<Component*> { &onButton, &rampBox, &rateSlider, &depthSlider, &edgeSlider })
        c->setLookAndFeel(nullptr);
}

void TremoloPanel::resized()
{
    onButton.setBounds(8, 3, 50, 19);
    rampBox.setBounds(344, 3, 100, 20);
    curve.setBounds(12, 27, 256, kHeight - 39);
    rateSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 302, 92 }));
    depthSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 364, 92 }));
    edgeSlider.setBounds(Rectangle<int>(44, 44).withCentre({ 426, 92 }));
}

void TremoloPanel::paint(Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(panel);
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);
    drawPanelTitle(g, kPanelTitleX, 4, 4, "TREMOLO");
    const bool on = proc.apvts.getRawParameterValue(ids::tremOn)->load() > 0.5f;
    const float alpha = on ? 1.0f : kOffTextAlpha;
    g.setColour(inkDim.withMultipliedAlpha(alpha));
    g.setFont(bold(10.0f));
    g.drawText("CHANGE OVER", Rectangle<int>(244, 4, 96, 18), Justification::centredRight);

    const float edge = (float) edgeSlider.getValue();
    drawControlLabel(g, 302, 118, "RATE", hzText((float) rateSlider.getValue()), alpha);
    drawControlLabel(g, 364, 118, "DEPTH", String(roundToInt(depthSlider.getValue() * 100.0)) + "%", alpha);
    drawControlLabel(g, 426, 118, "EDGE", edge < 0.05f ? String("SMOOTH") : (edge > 0.95f ? String("CHOP") : String(roundToInt(edge * 100.0f)) + "%"), alpha);
}

void TremoloPanel::refresh()
{
    const bool on = proc.apvts.getRawParameterValue(ids::tremOn)->load() > 0.5f;
    const bool acting = proc.getEngine().isEffectSideActive();
    setPowerActive(onButton, acting);
    for (Component* c : std::initializer_list<Component*> { &rampBox, &rateSlider, &depthSlider, &edgeSlider, &curve })
        if (const float alpha = offAlphaFor(*c, on); !sameFloat(c->getAlpha(), alpha))
            c->setAlpha(alpha);
    curve.setPlayhead(on ? proc.getEngine().getTremoloRampPosition() : -1.0f);
}
