#include "PluginEditor.h"

using namespace juce;

namespace
{
constexpr int kWidth = 480, kHeight = 700;
const Rectangle<int> kFace { 12, 12, 456, 520 };
const Rectangle<int> kGlobal { 12, 544, 456, 144 };

constexpr std::array<int, 3> kColX { 88, 240, 392 };
constexpr std::array<int, 2> kRowY { 142, 288 };
constexpr int kKnobSize = 88;

const Colour kInk { 0xfff2f0e6 };       // engraved text
const Colour kInkDim { 0xff9a988f };
const Colour kAccent { 0xffffa31a };    // Alt layer / lit LEDs
const Colour kRed { 0xffff3b30 };

Font bold(float h) { return Font(FontOptions(h, Font::bold)); }
Font plain(float h) { return Font(FontOptions(h, Font::plain)); }

void drawLed(Graphics& g, Point<float> c, float r, Colour col, bool on)
{
    if (on)
    {
        g.setColour(col.withAlpha(0.22f));
        g.fillEllipse(c.x - r * 2.4f, c.y - r * 2.4f, r * 4.8f, r * 4.8f);
    }
    g.setColour(on ? col : col.darker(0.9f));
    g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    if (on)
    {
        g.setColour(Colours::white.withAlpha(0.55f));
        g.fillEllipse(c.x - r * 0.45f, c.y - r * 0.6f, r * 0.7f, r * 0.7f);
    }
    g.setColour(Colours::black.withAlpha(0.75f));
    g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);
}

String hzText(float hz)
{
    return hz >= 1000.0f ? String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : String(roundToInt(hz)) + " Hz";
}
} // namespace

// ------------------------------------------------------------------------------------------
// Look and feel

PedalLookAndFeel::PedalLookAndFeel()
{
    setColour(ComboBox::backgroundColourId, Colour(0xff1a1a1d));
    setColour(ComboBox::textColourId, kInk);
    setColour(ComboBox::outlineColourId, Colour(0xff3c3c42));
    setColour(ComboBox::arrowColourId, kInkDim);
    setColour(PopupMenu::backgroundColourId, Colour(0xff1a1a1d));
    setColour(PopupMenu::textColourId, kInk);
    setColour(PopupMenu::highlightedBackgroundColourId, Colour(0xff4a3410));
    setColour(PopupMenu::highlightedTextColourId, kInk);
}

void PedalLookAndFeel::drawRotarySlider(Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, Slider& slider)
{
    const auto area = Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(2.0f);
    const float radius = jmin(area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const bool alt = (bool) slider.getProperties().getWithDefault("alt", false);
    const int ticks = (int) slider.getProperties().getWithDefault("ticks", 11);
    const Colour ink = alt ? kAccent : kInk;
    const bool enabled = slider.isEnabled();

    // scale
    const float r0 = radius * 0.80f, r1 = radius * (ticks > 16 ? 0.89f : 0.95f);
    for (int i = 0; i < ticks; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / (float) jmax(1, ticks - 1);
        g.setColour(ink.withAlpha(0.6f));
        g.drawLine(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a), c.x + r1 * std::sin(a), c.y - r1 * std::cos(a),
                   ticks > 16 ? 1.0f : 1.4f);
    }

    // knob body
    const float R = radius * 0.68f;
    g.setColour(Colours::black.withAlpha(0.55f));
    g.fillEllipse(c.x - R + 1.0f, c.y - R + 3.5f, R * 2.0f, R * 2.0f);

    g.setGradientFill(ColourGradient(Colour(0xff5a5a60), c.x - R * 0.55f, c.y - R * 0.8f, Colour(0xff0e0e10),
                                     c.x + R * 0.6f, c.y + R * 0.9f, false));
    g.fillEllipse(c.x - R, c.y - R, R * 2.0f, R * 2.0f);
    g.setColour(Colours::black.withAlpha(0.85f));
    g.drawEllipse(c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.6f);

    const float Ri = R * 0.80f;
    g.setGradientFill(ColourGradient(Colour(0xff2a2a2e), c.x, c.y - Ri, Colour(0xff161618), c.x, c.y + Ri, false));
    g.fillEllipse(c.x - Ri, c.y - Ri, Ri * 2.0f, Ri * 2.0f);
    g.setColour(Colours::white.withAlpha(0.07f));
    g.drawEllipse(c.x - Ri, c.y - Ri, Ri * 2.0f, Ri * 2.0f, 1.0f);

    // pointer
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    Path pointer;
    pointer.addRoundedRectangle(-1.8f, -R * 0.86f, 3.6f, R * 0.5f, 1.6f);
    g.setColour(enabled ? ink : ink.withAlpha(0.3f));
    g.fillPath(pointer, AffineTransform::rotation(angle).translated(c.x, c.y));
}

void PedalLookAndFeel::drawToggleButton(Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    g.setGradientFill(ColourGradient(on ? Colour(0xff6a4a12) : Colour(0xff34343a), r.getX(), r.getY(),
                                     on ? Colour(0xff3d2a08) : Colour(0xff1a1a1d), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(highlighted ? Colour(0xff6a6a72) : Colour(0xff0c0c0e));
    g.drawRoundedRectangle(r, 4.0f, 1.2f);
    g.setColour(on ? kInk : kInkDim);
    g.setFont(bold(11.0f));
    g.drawText(b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

void PedalLookAndFeel::drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(1.0f);
    g.setColour(findColour(ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(findColour(ComboBox::outlineColourId));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(box.findColour(ComboBox::arrowColourId));
    g.fillPath(arrow);
}

Font PedalLookAndFeel::getComboBoxFont(ComboBox&)
{
    return bold(11.0f);
}

// ------------------------------------------------------------------------------------------
// Small controls

void FootSwitch::paint(Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced(2.0f);
    const float d = jmin(b.getWidth(), b.getHeight());
    const auto c = b.getCentre();

    g.setColour(Colours::black.withAlpha(0.5f));
    g.fillEllipse(c.x - d * 0.5f + 1.0f, c.y - d * 0.5f + 4.0f, d, d);

    g.setGradientFill(ColourGradient(Colour(0xffe2e2e6), c.x, c.y - d * 0.5f, Colour(0xff55555c), c.x, c.y + d * 0.5f, false));
    g.fillEllipse(c.x - d * 0.5f, c.y - d * 0.5f, d, d);

    const float di = d * 0.76f;
    g.setGradientFill(ColourGradient(down ? Colour(0xff6c6c72) : Colour(0xffc9c9ce), c.x, c.y - di * 0.5f,
                                     down ? Colour(0xffa9a9ae) : Colour(0xff6a6a70), c.x, c.y + di * 0.5f, false));
    g.fillEllipse(c.x - di * 0.5f, c.y - di * 0.5f, di, di);
    g.setColour(Colours::black.withAlpha(0.6f));
    g.drawEllipse(c.x - di * 0.5f, c.y - di * 0.5f, di, di, 1.2f);
}

void FootSwitch::mouseDown(const MouseEvent&)
{
    down = true;
    repaint();
    if (onPress)
        onPress();
}

void FootSwitch::mouseUp(const MouseEvent&)
{
    down = false;
    repaint();
    if (onRelease)
        onRelease();
}

void PedalButton::paint(Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setGradientFill(ColourGradient(lit ? Colour(0xff6a4a12) : Colour(pressed ? 0xff26262a : 0xff3a3a40), b.getX(), b.getY(),
                                     lit ? Colour(0xff3d2a08) : Colour(0xff17171a), b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(Colours::black);
    g.drawRoundedRectangle(b, 4.0f, 1.2f);
    g.setColour(lit ? kInk : kInkDim);
    g.setFont(bold(11.0f));
    g.drawText(text, getLocalBounds(), Justification::centred);
}

void PedalButton::mouseDown(const MouseEvent&)
{
    pressed = true;
    repaint();
}

void PedalButton::mouseUp(const MouseEvent& e)
{
    pressed = false;
    repaint();
    if (contains(e.getPosition()) && onClick)
        onClick();
}

// ------------------------------------------------------------------------------------------
// Editor

OttobitEditor::OttobitEditor(OttobitProcessor& p)
    : AudioProcessorEditor(&p),
      proc(p),
      bypassParam(*p.apvts.getParameter(ids::bypass), [](float) {}),
      holdParam(*p.apvts.getParameter(ids::hold), [](float) {}),
      typeParam(*p.apvts.getParameter(ids::seqType), [](float) {})
{
    setLookAndFeel(&laf);

    static constexpr const char* frontIds[kKnobs] = { ids::sampleRate, ids::filter, ids::bits,
                                                      ids::stutter,    ids::seqPlay, ids::seqMult };
    static constexpr const char* labels[kKnobs] = { "SAMPLE", "FILTER", "BITS", "STUTTER", "SEQUENCER", "SEQ MULT" };
    const int ticks[kKnobs] = { 11, 11, 11, ottobit::mapping::kStutterPositions, ottobit::mapping::kSeqPlayPositions,
                                ottobit::mapping::kSeqMultPositions };

    for (int i = 0; i < kKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.label = labels[i];
        configureKnob(k.front, ticks[i]);
        configureKnob(k.step, 11);
        k.step.getProperties().set("alt", true);
        k.frontAttachment = std::make_unique<APVTS::SliderAttachment>(proc.apvts, frontIds[i], k.front);
        k.stepAttachment = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::step(i), k.step);
        addAndMakeVisible(k.front);
        addChildComponent(k.step);
    }

    // global panel
    auto smallKnob = [this](Slider& s, std::unique_ptr<APVTS::SliderAttachment>& att, const char* id)
    {
        s.setSliderStyle(Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        s.setMouseDragSensitivity(180);
        s.setLookAndFeel(&laf);
        att = std::make_unique<APVTS::SliderAttachment>(proc.apvts, id, s);
        addAndMakeVisible(s);
    };
    smallKnob(tempoSlider, tempoAtt, ids::tempo);
    smallKnob(inSlider, inAtt, ids::inputGain);
    smallKnob(outSlider, outAtt, ids::outputGain);
    smallKnob(trigSlider, trigAtt, ids::sensitivity);

    for (auto* b : { &syncButton, &holdButton })
    {
        b->setLookAndFeel(&laf);
        addAndMakeVisible(*b);
    }
    syncAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::tempoSync, syncButton);
    holdAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::hold, holdButton);

    auto fillCombo = [this](ComboBox& box, const char* id)
    {
        if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(id)))
            box.addItemList(choice->choices, 1);
        box.setLookAndFeel(&laf);
        addAndMakeVisible(box);
    };
    fillCombo(headroomBox, ids::headroom);
    fillCombo(stutterLenBox, ids::stutterLen);
    headroomAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::headroom, headroomBox);
    stutterLenAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::stutterLen, stutterLenBox);

    // footswitches
    bypassSwitch.setMouseCursor(MouseCursor::PointingHandCursor);
    tapSwitch.setMouseCursor(MouseCursor::PointingHandCursor);
    bypassSwitch.onPress = [this] { bypassParam.setValueAsCompleteGesture(rawValue(ids::bypass) > 0.5f ? 0.0f : 1.0f); };

    tapSwitch.onPress = [this]
    {
        tapDown = true;
        holdFromTap = false;
        tapPressMs = Time::getMillisecondCounterHiRes();
    };
    tapSwitch.onRelease = [this]
    {
        const bool wasHold = holdFromTap;
        tapDown = holdFromTap = false;
        if (wasHold)
        {
            holdParam.setValueAsPartOfGesture(0.0f);
            holdParam.endGesture();
        }
        else
        {
            proc.tapTempo();
        }
    };

    altButton.onClick = [this] { setAltLayer(!altLayer); };
    typeButton.onClick = [this]
    {
        const int next = (roundToInt(rawValue(ids::seqType)) + 1) % 3;
        typeParam.setValueAsCompleteGesture((float) next);
    };

    addAndMakeVisible(bypassSwitch);
    addAndMakeVisible(tapSwitch);
    addAndMakeVisible(altButton);
    addAndMakeVisible(typeButton);

    setSize(kWidth, kHeight);
    buildBackground();
    startTimerHz(30);
}

OttobitEditor::~OttobitEditor()
{
    stopTimer();
    for (auto& k : knobs)
    {
        k.front.setLookAndFeel(nullptr);
        k.step.setLookAndFeel(nullptr);
    }
    for (auto* s : { &tempoSlider, &inSlider, &outSlider, &trigSlider })
        s->setLookAndFeel(nullptr);
    syncButton.setLookAndFeel(nullptr);
    holdButton.setLookAndFeel(nullptr);
    headroomBox.setLookAndFeel(nullptr);
    stutterLenBox.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void OttobitEditor::configureKnob(Slider& s, int ticks)
{
    s.setSliderStyle(Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    s.setMouseDragSensitivity(220);
    s.setLookAndFeel(&laf);
    s.getProperties().set("ticks", ticks);
}

void OttobitEditor::setAltLayer(bool on)
{
    altLayer = on;
    for (auto& k : knobs)
    {
        k.front.setVisible(!on);
        k.step.setVisible(on);
    }
    altButton.lit = on;
    altButton.repaint();
    repaint();
}

float OttobitEditor::rawValue(const char* id) const
{
    return proc.apvts.getRawParameterValue(id)->load();
}

ottobit::SeqType OttobitEditor::seqType() const
{
    return (ottobit::SeqType) jlimit(0, 2, roundToInt(rawValue(ids::seqType)));
}

Rectangle<int> OttobitEditor::knobBounds(int index) const
{
    const auto c = knobCentre(index);
    return Rectangle<int>(kKnobSize, kKnobSize).withCentre(c.toInt());
}

Point<float> OttobitEditor::knobCentre(int index) const
{
    return { (float) kColX[(size_t) (index % 3)], (float) kRowY[(size_t) (index / 3)] };
}

String OttobitEditor::readoutText(int i) const
{
    using namespace ottobit;

    if (altLayer)
    {
        const float v = rawValue(ids::step(i).toRawUTF8());
        const auto step = mapping::decodeStep(v, seqType());
        if (step.kind == mapping::Step::Kind::Skip)
            return "SKIP";
        if (step.kind == mapping::Step::Kind::Mute)
            return "MUTE";
        if (seqType() == SeqType::Pitch)
            return step.semitones == 0.0f ? String("DRY") : String(step.semitones > 0 ? "+" : "") + String((int) step.semitones) + " st";
        return String(roundToInt(step.factor * 100.0f)) + "%";
    }

    switch (i)
    {
        case 0: return hzText(mapping::sampleRateHz(rawValue(ids::sampleRate)));
        case 1: return rawValue(ids::filter) >= 0.99f ? String("OPEN") : hzText(mapping::filterHz(rawValue(ids::filter)));
        case 2: return String(mapping::bitDepth(rawValue(ids::bits)), 1) + " bit";
        case 3:
        {
            const auto m = mapping::stutterMode(roundToInt(rawValue(ids::stutter)));
            if (m.off)
                return "OFF";
            if (m.random)
                return "RANDOM";
            const char* speed = m.speed > 1.5f ? "DBL" : (m.speed < 0.75f ? "HALF" : "FULL");
            return String(speed) + " x" + String(m.repeats);
        }
        case 4:
        {
            static const char* names[] = { "OFF", "ONCE", "TWICE", "4 TIMES", "8 TIMES", "LOOP" };
            return names[jlimit(0, 5, roundToInt(rawValue(ids::seqPlay)))];
        }
        default: return "x" + String(mapping::seqMultiplier(roundToInt(rawValue(ids::seqMult))));
    }
}

void OttobitEditor::buildBackground()
{
    background = Image(Image::ARGB, kWidth, kHeight, true);
    Graphics g(background);

    g.fillAll(Colour(0xff0d0d0f));

    // Enclosure: black powder coat with a fine metal flake.
    const auto face = kFace.toFloat();
    g.setGradientFill(ColourGradient(Colour(0xff2a2a2e), face.getX(), face.getY(), Colour(0xff121214), face.getRight(),
                                     face.getBottom(), false));
    g.fillRoundedRectangle(face, 16.0f);

    {
        Graphics::ScopedSaveState save(g);
        Path clip;
        clip.addRoundedRectangle(face, 16.0f);
        g.reduceClipRegion(clip);
        Random rng(1234);
        for (int i = 0; i < 5200; ++i)
        {
            const float b = rng.nextFloat();
            g.setColour(Colours::white.withAlpha(0.015f + 0.13f * b * b * b));
            g.fillRect(face.getX() + rng.nextFloat() * face.getWidth(), face.getY() + rng.nextFloat() * face.getHeight(), 1.0f, 1.0f);
        }
    }
    g.setColour(Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(face.reduced(0.5f), 16.0f, 1.0f);
    g.setColour(Colours::black.withAlpha(0.8f));
    g.drawRoundedRectangle(face.expanded(0.5f), 16.0f, 1.5f);

    // Global panel
    const auto glob = kGlobal.toFloat();
    g.setColour(Colour(0xff17171a));
    g.fillRoundedRectangle(glob, 10.0f);
    g.setColour(Colour(0xff2c2c31));
    g.drawRoundedRectangle(glob.reduced(0.5f), 10.0f, 1.0f);
}

void OttobitEditor::resized()
{
    for (int i = 0; i < kKnobs; ++i)
    {
        knobs[(size_t) i].front.setBounds(knobBounds(i));
        knobs[(size_t) i].step.setBounds(knobBounds(i));
    }

    typeButton.setBounds(Rectangle<int>(78, 22).withCentre({ 88, 383 }));
    altButton.setBounds(Rectangle<int>(78, 22).withCentre({ 392, 383 }));
    bypassSwitch.setBounds(Rectangle<int>(84, 84).withCentre({ 88, 462 }));
    tapSwitch.setBounds(Rectangle<int>(84, 84).withCentre({ 392, 462 }));

    // global panel: four knobs, then switches
    const std::array<Slider*, 4> small { &tempoSlider, &inSlider, &outSlider, &trigSlider };
    for (size_t i = 0; i < small.size(); ++i)
        small[i]->setBounds(Rectangle<int>(52, 52).withCentre({ 69 + (int) i * 114, 585 }));

    syncButton.setBounds(24, 654, 62, 24);
    holdButton.setBounds(92, 654, 62, 24);
    headroomBox.setBounds(170, 654, 124, 24);
    stutterLenBox.setBounds(304, 654, 148, 24);
}

void OttobitEditor::paint(Graphics& g)
{
    g.drawImageAt(background, 0, 0);

    // Title
    g.setColour(kInk);
    g.setFont(bold(26.0f));
    g.drawText("OTTOCLONE", Rectangle<int>(kFace.getX(), 22, kFace.getWidth(), 32), Justification::centred);
    g.setColour(kInkDim);
    g.setFont(plain(10.5f));
    g.drawText("UNOFFICIAL  -  NOT AFFILIATED WITH MERIS", Rectangle<int>(kFace.getX(), 54, kFace.getWidth(), 14), Justification::centred);

    const int currentStep = proc.getEngine().getCurrentStep();

    // Knob labels and readouts
    for (int i = 0; i < kKnobs; ++i)
    {
        const auto c = knobCentre(i);
        const Colour ink = altLayer ? kAccent : kInk;

        g.setColour(ink);
        g.setFont(bold(12.5f));
        const String label = altLayer ? "STEP " + String(i + 1) : String(knobs[(size_t) i].label);
        g.drawText(label, Rectangle<int>(112, 16).withCentre({ (int) c.x, (int) c.y - 62 }), Justification::centred);

        g.setColour(ink.withAlpha(0.75f));
        g.setFont(plain(11.5f));
        g.drawText(readoutText(i), Rectangle<int>(120, 16).withCentre({ (int) c.x, (int) c.y + 56 }), Justification::centred);

        if (altLayer && currentStep == i)
        {
            g.setColour(kAccent.withAlpha(0.85f));
            g.drawEllipse(c.x - 49.0f, c.y - 49.0f, 98.0f, 98.0f, 2.0f);
        }
    }

    // Footswitch area
    const bool bypassed = rawValue(ids::bypass) > 0.5f;
    const auto type = seqType();
    drawLed(g, { 88.0f - 52.0f, 383.0f }, 4.5f, kAccent, type == ottobit::SeqType::Pitch);
    drawLed(g, { 88.0f + 52.0f, 383.0f }, 4.5f, kAccent, type == ottobit::SeqType::Filter);
    drawLed(g, { 88.0f, 410.0f }, 6.0f, kRed, !bypassed);

    const bool tapBlink = proc.getEngine().getBeatPhase() < 0.18f;
    drawLed(g, { 392.0f, 410.0f }, 6.0f, kAccent, tapBlink);
    drawLed(g, { 392.0f - 52.0f, 383.0f }, 4.5f, kAccent, altLayer);

    g.setColour(kInk);
    g.setFont(bold(11.0f));
    g.drawText("BYPASS", Rectangle<int>(100, 14).withCentre({ 88, 512 }), Justification::centred);
    g.drawText("TAP / HOLD", Rectangle<int>(100, 14).withCentre({ 392, 512 }), Justification::centred);

    // Centre read-out: tempo
    const float bpm = proc.getEffectiveBpm();
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("TEMPO", Rectangle<int>(100, 12).withCentre({ 240, 392 }), Justification::centred);
    g.setColour(kInk);
    g.setFont(bold(24.0f));
    g.drawText(String(bpm, 1), Rectangle<int>(130, 30).withCentre({ 240, 418 }), Justification::centred);
    g.setColour(kInkDim);
    g.setFont(plain(10.0f));
    g.drawText(rawValue(ids::tempoSync) > 0.5f ? "BPM  (HOST)" : "BPM", Rectangle<int>(130, 12).withCentre({ 240, 442 }), Justification::centred);
    if (rawValue(ids::hold) > 0.5f)
    {
        g.setColour(kAccent);
        g.setFont(bold(11.0f));
        g.drawText("STUTTER HOLD", Rectangle<int>(130, 14).withCentre({ 240, 468 }), Justification::centred);
    }

    // Global panel labels
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("GLOBAL", Rectangle<int>(26, 548, 80, 12), Justification::left);

    struct Entry { const char* name; String value; };
    const std::array<Entry, 4> entries {{
        { "TEMPO", String(rawValue(ids::tempo), 1) },
        { "INPUT", String(rawValue(ids::inputGain), 1) + " dB" },
        { "OUTPUT", String(rawValue(ids::outputGain), 1) + " dB" },
        { "TRIGGER", String(roundToInt(rawValue(ids::sensitivity) * 100.0f)) + "%" },
    }};
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const int cx = 69 + (int) i * 114;
        g.setColour(kInk);
        g.setFont(bold(11.0f));
        g.drawText(entries[i].name, Rectangle<int>(100, 14).withCentre({ cx, 619 }), Justification::centred);
        g.setColour(kInkDim);
        g.setFont(plain(10.5f));
        g.drawText(entries[i].value, Rectangle<int>(100, 13).withCentre({ cx, 633 }), Justification::centred);
    }
}

void OttobitEditor::timerCallback()
{
    // Holding the tap switch freezes the stutter buffer (manual, section 4).
    if (tapDown && !holdFromTap && Time::getMillisecondCounterHiRes() - tapPressMs > 300.0)
    {
        holdFromTap = true;
        holdParam.beginGesture();
        holdParam.setValueAsPartOfGesture(1.0f);
    }

    const auto type = seqType();
    for (auto& k : knobs)
    {
        const int ticks = type == ottobit::SeqType::Pitch ? 27 : 11;
        if ((int) k.step.getProperties().getWithDefault("ticks", 11) != ticks)
            k.step.getProperties().set("ticks", ticks);
    }
    repaint();
}
