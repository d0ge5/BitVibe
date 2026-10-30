#include "PluginEditor.h"

#include "DrumPage.h"
#include "PatternPanel.h"
#include "SamplePanel.h"
#include "UiSettings.h"

using namespace juce;

namespace
{
// The pedal face and the global panel are laid out in their own coordinates; everything below the
// header panel is shifted down by kFaceDy. The whole pedal page sits below the tab strip (kTabsH).
constexpr int kTabsH = 36;
constexpr int kFaceDy = 100;
constexpr int kWidth = 480;
constexpr bool kShowDrumsTab = false;
constexpr int kHeight = kTabsH + 736 + kFaceDy;
// The pedal page has a second column to the right of the pedal: reverb, sample blend and beat pattern panels.
constexpr int kSideX = kWidth + 12, kPedalWidth = kWidth + 468;
constexpr int kColDx = kSideX - 12; // how far the second column is moved right of the pedal's own coordinates
const Rectangle<int> kHeader { 12, 12, 456, 88 };
const Rectangle<int> kFace { 12, 12, 456, 520 };
const Rectangle<int> kGlobal { 12, 544, 456, 180 };
// Second-column panels, in face coordinates (so the first one starts level with the preset header).
constexpr int kColY = 12 - kFaceDy;
const Rectangle<int> kReverb { kSideX, kColY + 108, 456, 100 };
const Rectangle<int> kDist { kSideX, kColY, 456, 100 };
constexpr int kSamplesY = kColY + 216;
constexpr int kPatternY = kSamplesY + SamplePanel::kHeight + 8;

// Header rows (window coordinates).
const Rectangle<int> kPrevBounds { 24, 20, 30, 26 }, kNameBox { 60, 20, 266, 26 }, kNextBounds { 332, 20, 30, 26 },
    kSaveBounds { 372, 20, 84, 26 };
const Rectangle<int> kSetUpBounds { 24, 52, 62, 26 }, kExpressionBounds { 94, 52, 204, 26 },
    kSetDownBounds { 306, 52, 72, 26 }, kMorphBounds { 386, 52, 70, 26 };
const Rectangle<int> kCaptionBounds { 24, 82, 432, 12 };

constexpr std::array<int, 3> kColX { 88, 240, 392 };
constexpr std::array<int, 2> kRowY { 142, 288 };
constexpr int kKnobSize = 88;

// The five global knobs share one row: centre of the first and the spacing (face coordinates).
constexpr int kGlobalKnobX0 = 58, kGlobalKnobDx = 91;

const Colour kInk = pedalstyle::ink;
const Colour kInkDim = pedalstyle::inkDim;
const Colour kAccent = pedalstyle::accent;
const Colour kRed = pedalstyle::red;

using pedalstyle::bold;
using pedalstyle::plain;

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
    setColour(TooltipWindow::backgroundColourId, Colour(0xff26262b));
    setColour(TooltipWindow::textColourId, kInk);
    setColour(TooltipWindow::outlineColourId, Colour(0xff55555d));
    setColour(Label::textColourId, kInk);
    setColour(Label::textWhenEditingColourId, kInk);
    setColour(Label::backgroundWhenEditingColourId, Colour(0xff0e0e10));
    setColour(Label::outlineWhenEditingColourId, kAccent);
    setColour(TextEditor::highlightColourId, Colour(0xff4a3410));
    setColour(TextEditor::highlightedTextColourId, kInk);
    setColour(CaretComponent::caretColourId, kAccent);
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

    // knob body: 15% bigger while it is held, so the player can see they have a grip on it (it still fits inside the tick marks)
    const float R = radius * 0.68f * (slider.isMouseButtonDown() ? 1.15f : 1.0f);
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

void PedalLookAndFeel::drawLinearSlider(Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                        float, Slider::SliderStyle, Slider& slider)
{
    // Expression bar: a groove with a lit portion up to a small metal thumb. Dim while the morph is off.
    const bool active = (bool) slider.getProperties().getWithDefault("active", true);
    const auto area = Rectangle<float>((float) x, (float) y, (float) width, (float) height);
    const float cy = area.getCentreY();
    const float left = area.getX() + 8.0f, right = area.getRight() - 8.0f;
    const float pos = jlimit(left, right, sliderPos);

    const auto groove = Rectangle<float>(left, cy - 3.0f, right - left, 6.0f);
    g.setColour(Colour(0xff0a0a0c));
    g.fillRoundedRectangle(groove, 3.0f);
    g.setColour(Colour(0xff34343a));
    g.drawRoundedRectangle(groove, 3.0f, 1.0f);

    g.setColour(active ? kAccent : Colour(0xff5a5a60));
    g.fillRoundedRectangle(Rectangle<float>(left, cy - 3.0f, jmax(0.0f, pos - left), 6.0f), 3.0f);

    for (int i = 0; i <= 4; ++i)
    {
        const float tx = left + (right - left) * (float) i / 4.0f;
        g.setColour(kInkDim.withAlpha(0.5f));
        g.drawLine(tx, cy + 8.0f, tx, cy + 11.0f, 1.0f);
    }

    const auto thumb = Rectangle<float>(14.0f, 20.0f).withCentre({ pos, cy });
    g.setColour(Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(thumb.translated(0.0f, 2.0f), 4.0f);
    g.setGradientFill(ColourGradient(Colour(0xffd8d8dc), thumb.getX(), thumb.getY(), Colour(0xff55555c),
                                     thumb.getX(), thumb.getBottom(), false));
    g.fillRoundedRectangle(thumb, 4.0f);
    g.setColour(Colours::black.withAlpha(0.7f));
    g.drawRoundedRectangle(thumb, 4.0f, 1.0f);
    g.setColour(active ? kAccent : Colour(0xff2a2a2e));
    g.fillRect(Rectangle<float>(2.0f, 12.0f).withCentre(thumb.getCentre()));
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

BitVibeEditor::BitVibeEditor(BitVibeProcessor& p)
    : AudioProcessorEditor(&p),
      proc(p),
      bypassParam(*p.apvts.getParameter(ids::bypass), [](float) {}),
      holdParam(*p.apvts.getParameter(ids::hold), [](float) {}),
      typeParam(*p.apvts.getParameter(ids::seqType), [](float) {}),
      tempoParam(*p.apvts.getParameter(ids::tempo), [](float) {})
{
    setLookAndFeel(&laf);

    static constexpr const char* frontIds[kKnobs] = { ids::sampleRate, ids::filter, ids::bits,
                                                      ids::stutter,    ids::seqPlay, ids::seqMult };
    static constexpr const char* labels[kKnobs] = { "SAMPLE", "FILTER", "BITS", "STUTTER", "SEQUENCER", "SEQ MULT" };
    const int ticks[kKnobs] = { 11, 11, 11, bitvibe::mapping::kStutterPositions, bitvibe::mapping::kSeqPlayPositions,
                                bitvibe::mapping::kSeqMultPositions };

    for (int i = 0; i < kKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.label = labels[i];
        configureKnob(k.front, ticks[i]);
        configureKnob(k.step, 11);
        k.step.getProperties().set("alt", true);
        k.frontAttachment = std::make_unique<APVTS::SliderAttachment>(proc.apvts, frontIds[i], k.front);
        k.stepAttachment = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::step(i), k.step);
        content.addAndMakeVisible(k.front);
        content.addChildComponent(k.step);
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
        content.addAndMakeVisible(s);
    };
    smallKnob(tempoSlider, tempoAtt, ids::tempo);
    smallKnob(inSlider, inAtt, ids::inputGain);
    smallKnob(mixSlider, mixAtt, ids::mix);
    smallKnob(outSlider, outAtt, ids::outputGain);
    smallKnob(trigSlider, trigAtt, ids::sensitivity);
    smallKnob(reverbToneSlider, reverbToneAtt, ids::reverbTone);
    smallKnob(reverbMixSlider, reverbMixAtt, ids::reverbMix);
    smallKnob(reverbLevelSlider, reverbLevelAtt, ids::reverbLevel);
    smallKnob(reverbLowSlider, reverbLowAtt, ids::reverbLowCut);
    smallKnob(distDriveSlider, distDriveAtt, ids::distDrive);
    smallKnob(distToneSlider, distToneAtt, ids::distTone);
    smallKnob(distLevelSlider, distLevelAtt, ids::distLevel);
    for (auto* b : { &distOnButton, &distReverbButton })
    {
        b->setLookAndFeel(&laf);
        content.addAndMakeVisible(*b);
    }
    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(ids::distType)))
        distTypeBox.addItemList(choice->choices, 1);
    distTypeBox.setLookAndFeel(&laf);
    content.addAndMakeVisible(distTypeBox);
    distTypeAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::distType, distTypeBox);
    distOnAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::distOn, distOnButton);
    distReverbAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::distReverb, distReverbButton);
    smallKnob(reverbHighSlider, reverbHighAtt, ids::reverbHighCut);
    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(ids::reverbType)))
        reverbTypeBox.addItemList(choice->choices, 1);
    reverbTypeBox.setLookAndFeel(&laf);
    content.addAndMakeVisible(reverbTypeBox);
    reverbTypeAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::reverbType, reverbTypeBox);
    samplePanel = std::make_unique<SamplePanel>(proc, laf);
    content.addAndMakeVisible(*samplePanel);
    patternPanel = std::make_unique<PatternPanel>(proc, laf);
    content.addAndMakeVisible(*patternPanel);

    for (auto* b : { &syncButton, &holdButton, &fxTrigButton })
    {
        b->setLookAndFeel(&laf);
        content.addAndMakeVisible(*b);
    }
    syncAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::tempoSync, syncButton);
    holdAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::hold, holdButton);
    fxTrigAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::fxTriggers, fxTrigButton);

    auto fillCombo = [this](ComboBox& box, const char* id)
    {
        if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(id)))
            box.addItemList(choice->choices, 1);
        box.setLookAndFeel(&laf);
        content.addAndMakeVisible(box);
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
    typePitchButton.onClick = [this] { typeParam.setValueAsCompleteGesture(0.0f); showSeqType(); };
    typeFilterButton.onClick = [this] { typeParam.setValueAsCompleteGesture(1.0f); showSeqType(); };
    typeRateButton.onClick = [this] { typeParam.setValueAsCompleteGesture(2.0f); showSeqType(); };

    content.addAndMakeVisible(bypassSwitch);
    content.addAndMakeVisible(tapSwitch);
    content.addAndMakeVisible(altButton);
    for (auto* b : { &typePitchButton, &typeFilterButton, &typeRateButton })
        content.addAndMakeVisible(*b);
    showSeqType();

    // preset bar: previous / next slot, the name (double-click to rename) and SAVE
    // the tempo read-out in the middle of the pedal: click it and type a BPM (Enter to set, Esc to cancel)
    tempoLabel.setLookAndFeel(&laf);
    tempoLabel.setFont(bold(24.0f));
    tempoLabel.setJustificationType(Justification::centred);
    tempoLabel.setEditable(true, true, false);
    tempoLabel.onTextChange = [this]
    {
        const auto text = tempoLabel.getText();
        if (text.containsAnyOf("0123456789"))
            tempoParam.setValueAsCompleteGesture(jlimit(bitvibe::kMinTempoBpm, bitvibe::kMaxTempoKnobBpm, text.getFloatValue()));
        tempoLabel.setText(String(proc.getEffectiveBpm(), 1), dontSendNotification);
    };
    tempoLabel.setText(String(proc.getEffectiveBpm(), 1), dontSendNotification);
    content.addAndMakeVisible(tempoLabel);

    presetLabel.setLookAndFeel(&laf);
    presetLabel.setFont(bold(13.5f));
    presetLabel.setJustificationType(Justification::centred);
    presetLabel.setEditable(false, true, false);
    presetLabel.onTextChange = [this]
    {
        proc.changeProgramName(proc.getCurrentProgram(), presetLabel.getText());
        refreshPresetName();
    };
    prevButton.onClick = [this] { stepProgram(-1); };
    nextButton.onClick = [this] { stepProgram(1); };
    saveButton.onClick = [this]
    {
        proc.saveCurrentPreset();
        flashSaveUntil = Time::getMillisecondCounterHiRes() + 500.0;
    };
    for (auto* b : { &prevButton, &nextButton, &saveButton, &setUpButton, &setDownButton })
        content.addAndMakeVisible(*b);
    content.addAndMakeVisible(presetLabel);

    // expression morph strip: capture the two sets, sweep between them, switch the morph on
    setUpButton.onClick = [this]
    {
        proc.captureToeUp();
        flashUpUntil = Time::getMillisecondCounterHiRes() + 400.0;
    };
    setDownButton.onClick = [this]
    {
        proc.captureToeDown();
        flashDownUntil = Time::getMillisecondCounterHiRes() + 400.0;
    };
    expressionSlider.setSliderStyle(Slider::LinearHorizontal);
    expressionSlider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
    expressionSlider.setLookAndFeel(&laf);
    expressionAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::expression, expressionSlider);
    content.addAndMakeVisible(expressionSlider);
    morphButton.setLookAndFeel(&laf);
    morphAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::morph, morphButton);
    content.addAndMakeVisible(morphButton);
    refreshPresetName();
    setTooltips();

    pedalComponents = { &tempoSlider, &inSlider, &mixSlider, &outSlider, &trigSlider, &syncButton, &holdButton, &fxTrigButton,
                        &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider, &reverbLowSlider, &reverbHighSlider, &reverbTypeBox, &distDriveSlider, &distToneSlider, &distLevelSlider, &distOnButton, &distReverbButton, &distTypeBox, samplePanel.get(), patternPanel.get(),
                        &headroomBox, &stutterLenBox, &bypassSwitch, &tapSwitch, &altButton, &typePitchButton, &typeFilterButton, &typeRateButton, &presetLabel, &tempoLabel,
                        &prevButton, &nextButton, &saveButton, &setUpButton, &setDownButton, &expressionSlider, &morphButton };
    for (auto& k : knobs)
    {
        pedalComponents.push_back(&k.front);
        pedalComponents.push_back(&k.step);
    }

    // tabs
    pedalTab.onClick = [this] { showDrums(false); };
    drumsTab.onClick = [this] { showDrums(true); };
    pedalTab.setTooltip("PEDAL: the knobs, footswitches, presets and the expression morph.");
    drumsTab.setTooltip("DRUMS: load a drum beat and let its kick, snare, hi-hat, cymbal and tom hits trigger the sequencer and the stutter.");
    pedalTab.lit = true;
    // The DRUMS tab is switched off for now (the drum map and beat playback underneath are all still there):
    // set kShowDrumsTab to bring the tab strip back.
    if (kShowDrumsTab)
    {
        content.addAndMakeVisible(pedalTab);
        content.addAndMakeVisible(drumsTab);
    }
    drumPage = std::make_unique<DrumPage>(proc, laf);
    content.addChildComponent(*drumPage);

    // The window can be resized (the shape stays): the pane is drawn at its designed size and scaled to fit.
    addAndMakeVisible(content);
    content.toBack();
    baseWidth = kPedalWidth;
    baseHeight = kHeight;
    scale = savedScale = uisettings::loadWindowScale();
    constrainer.setFixedAspectRatio((double) baseWidth / (double) baseHeight);
    setConstrainer(&constrainer);
    setResizable(true, true);
    applySize();
    buildBackground();
    startTimerHz(30);
}

BitVibeEditor::~BitVibeEditor()
{
    stopTimer();
    for (auto& k : knobs)
    {
        k.front.setLookAndFeel(nullptr);
        k.step.setLookAndFeel(nullptr);
    }
    for (auto* s : { &tempoSlider, &inSlider, &mixSlider, &outSlider, &trigSlider, &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider, &reverbLowSlider, &reverbHighSlider, &distDriveSlider, &distToneSlider, &distLevelSlider })
        s->setLookAndFeel(nullptr);
    distOnButton.setLookAndFeel(nullptr);
    distTypeBox.setLookAndFeel(nullptr);
    distReverbButton.setLookAndFeel(nullptr);
    reverbTypeBox.setLookAndFeel(nullptr);
    syncButton.setLookAndFeel(nullptr);
    holdButton.setLookAndFeel(nullptr);
    fxTrigButton.setLookAndFeel(nullptr);
    morphButton.setLookAndFeel(nullptr);
    expressionSlider.setLookAndFeel(nullptr);
    presetLabel.setLookAndFeel(nullptr);
    tempoLabel.setLookAndFeel(nullptr);
    headroomBox.setLookAndFeel(nullptr);
    stutterLenBox.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void BitVibeEditor::configureKnob(Slider& s, int ticks)
{
    s.setSliderStyle(Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    s.setMouseDragSensitivity(220);
    s.setLookAndFeel(&laf);
    s.getProperties().set("ticks", ticks);
}

// The button of the sequencer type in use is lit.
void BitVibeEditor::showSeqType()
{
    const int type = roundToInt(rawValue(ids::seqType));
    const std::array<std::pair<PedalButton*, int>, 3> buttons { { { &typePitchButton, 0 }, { &typeFilterButton, 1 }, { &typeRateButton, 2 } } };
    for (const auto& [button, index] : buttons)
        if (button->lit != (type == index))
        {
            button->lit = type == index;
            button->repaint();
        }
}

void BitVibeEditor::setAltLayer(bool on)
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

void BitVibeEditor::showDrums(bool on)
{
    drumsShown = on;
    for (auto* c : pedalComponents)
        c->setVisible(!on);
    if (!on)
        setAltLayer(altLayer); // puts the right layer of knobs back
    drumPage->setVisible(on);
    baseWidth = on ? kWidth : kPedalWidth;
    baseHeight = kHeight;
    constrainer.setFixedAspectRatio((double) baseWidth / (double) baseHeight);
    applySize();
    pedalTab.lit = !on;
    drumsTab.lit = on;
    pedalTab.repaint();
    drumsTab.repaint();
    repaint();
}

void BitVibeEditor::stepProgram(int direction)
{
    const int count = proc.getNumPrograms();
    proc.setCurrentProgram((proc.getCurrentProgram() + direction + count) % count);
    refreshPresetName();
}

void BitVibeEditor::refreshPresetName()
{
    const int program = proc.getCurrentProgram();
    const String name = proc.getProgramName(program);
    if (program == shownProgram && name == shownName)
        return;
    shownProgram = program;
    shownName = name;
    if (!presetLabel.isBeingEdited())
        presetLabel.setText(name, dontSendNotification);
    repaint();
}

float BitVibeEditor::rawValue(const char* id) const
{
    return proc.apvts.getRawParameterValue(id)->load();
}

bitvibe::SeqType BitVibeEditor::seqType() const
{
    return (bitvibe::SeqType) jlimit(0, 2, roundToInt(rawValue(ids::seqType)));
}

Rectangle<int> BitVibeEditor::knobBounds(int index) const
{
    const auto c = knobCentre(index);
    return Rectangle<int>(kKnobSize, kKnobSize).withCentre(c.toInt());
}

Point<float> BitVibeEditor::knobCentre(int index) const
{
    return { (float) kColX[(size_t) (index % 3)], (float) kRowY[(size_t) (index / 3)] };
}

String BitVibeEditor::readoutText(int i) const
{
    using namespace bitvibe;

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

// ------------------------------------------------------------------------------------------
// Hover descriptors. Plain language, one or two short sentences each.

void BitVibeEditor::setTooltips()
{
    static const char* frontTips[kKnobs] = {
        "SAMPLE: lowers the sample rate for a gritty, aliased sound. Turn left for more. Fully right = off.",
        "FILTER: a low-pass filter that darkens the sound. Turn left to close it. Fully right = off.",
        "BITS: lowers the bit depth for digital distortion. Turn left for more. Fully right = off.",
        "STUTTER: after each picked note, repeats a short slice of it. Left = off, then 1 to 16 repeats at normal, "
        "double or half speed. Fully right = random.",
        "SEQUENCER: how many times the 6-step sequence plays after each picked note: off, once, twice, 4, 8 or looping.",
        "SEQ MULT: how fast the sequencer runs, as a multiple of the tempo (x1 to x128)."
    };
    for (int i = 0; i < kKnobs; ++i)
        knobs[(size_t) i].front.setTooltip(frontTips[i]);
    updateStepTooltips();

    // footswitches and buttons
    bypassSwitch.setTooltip("BYPASS: switches the effect on or off. The red light shows it is on.");
    tapSwitch.setTooltip("TAP: tap to set the tempo. Press and hold to freeze the stutter.");
    altButton.setTooltip("ALT: switch the six knobs to the six step settings of the sequencer.");
    typePitchButton.setTooltip("SEQ TYPE: PITCH. The six step knobs set a pitch shift for each step, from -12 to +12 semitones.");
    typeFilterButton.setTooltip("SEQ TYPE: FILTER. The six step knobs set the filter cutoff of each step, as a percent of the FILTER knob.");
    typeRateButton.setTooltip("SEQ TYPE: SAMPLE RATE. The six step knobs set the sample rate of each step, as a percent of the SAMPLE knob.");

    // global panel
    tempoSlider.setTooltip("TEMPO: the speed of the sequencer and stutter, 23.4 to 240 BPM. Turn the knob, click the big BPM number to type a tempo, tap, or turn on SYNC.");
    inSlider.setTooltip("INPUT: how hard your signal drives the effect. The clean signal used by MIX is not changed.");
    reverbToneSlider.setTooltip("REVERB TONE: the colour of the reverb, from dark (left) to bright (right).");
    reverbMixSlider.setTooltip("REVERB MIX: how much reverb is added to the sequenced and stuttered sound. Fully left = no reverb. "
                               "Your clean signal never gets reverb.");
    reverbLevelSlider.setTooltip("REVERB LEVEL: the volume of the reverb, from -30 dB to +6 dB. Works together with REVERB MIX.");
    distDriveSlider.setTooltip("DRIVE (overdrive) / SUSTAIN (fuzz): how hard the signal is pushed into the clippers. Turn right for more. "
                               "The overdrive pushes the mids and highs harder than the lows; the fuzz gets longer, squarer and more compressed.");
    distToneSlider.setTooltip("TONE: overdrive = a low-pass after the clipper (left darker, right brighter). Fuzz = a blend from a dark, bassy sound (left) "
                              "to a bright, thin one (right) with the middle scooped out.");
    distLevelSlider.setTooltip("LEVEL (overdrive) / VOLUME (fuzz): the volume of the distorted sound. About 80% is roughly the level it came in at.");
    distTypeBox.setTooltip("DISTORTION TYPE: Overdrive is a smooth mid-boosting overdrive; Fuzz is a thick, sustaining fuzz. Both only act on the sequenced "
                           "and stuttered sound (and the random sounds), never on your clean signal.");
    distOnButton.setTooltip("DISTORTION ON: distorts the sequenced and stuttered sound (and the random sounds). "
                            "Your clean signal is never distorted.");
    distReverbButton.setTooltip("REVERB TOO: tick this to distort the reverb as well. Unticked, the reverb stays clean.");
    reverbLowSlider.setTooltip("LOW CUT: a high-pass filter on the reverb only. Turn right to remove the low end from the reverb. Fully left = off.");
    reverbHighSlider.setTooltip("HIGH CUT: a low-pass filter on the reverb only. Turn left to remove the highs from the reverb. Fully right = off.");
    reverbTypeBox.setTooltip("REVERB TYPE: Room is short and tight, Hall is big and slow, Spring is the splashy, dripping sound of a spring tank.");
    mixSlider.setTooltip("MIX: blends your clean input (left) with the effect (right). Fully right = effect only.");
    outSlider.setTooltip("OUTPUT: the overall output level.");
    trigSlider.setTooltip("TRIGGER: how easily picked notes start the stutter and sequencer. Turn right if it misses notes, left if it fires too often.");
    syncButton.setTooltip("SYNC: follow the tempo of your DAW instead of the tempo knob.");
    holdButton.setTooltip("HOLD: freeze the stutter on the current (or next) note until you switch this off.");
    fxTrigButton.setTooltip("FX ONLY ON TRIGGERS: when on, the sample-rate, bit and filter effects only act while the stutter or "
                            "sequencer is playing; the rest of the time your signal stays clean. A sequencer set to LOOP is always playing.");
    headroomBox.setTooltip("HEADROOM: Instrument for guitar level, Line for hotter signals such as synths.");
    stutterLenBox.setTooltip("STUTTER LENGTH: how long the repeated slice is, as a note length of the tempo.");

    // preset bar and expression morph strip
    prevButton.setTooltip("Previous preset.");
    nextButton.setTooltip("Next preset.");
    saveButton.setTooltip("SAVE: store the current sound in this preset slot.");
    tempoLabel.setTooltip("TEMPO: the speed of the sequencer and stutter. Click the number and type a BPM (23.4 to 240), then press Enter. "
                          "While SYNC is on it shows the host tempo.");
    presetLabel.setTooltip("The preset name. Double-click to rename it. An orange dot means the sound has unsaved changes.");
    setUpButton.setTooltip("SET UP: store the current knobs as the 'toe up' sound (expression slider fully left).");
    setDownButton.setTooltip("SET DOWN: store the current knobs as the 'toe down' sound (expression slider fully right).");
    expressionSlider.setTooltip("EXPRESSION: sweeps from the SET UP sound to the SET DOWN sound. Needs MORPH switched on.");
    morphButton.setTooltip("MORPH: when on, the sound follows the expression slider instead of the knobs.");
}

void BitVibeEditor::updateStepTooltips()
{
    const auto type = seqType();
    const char* what = type == bitvibe::SeqType::Pitch
                           ? "its pitch shift, from -12 to +12 semitones (centre = unshifted)"
                           : (type == bitvibe::SeqType::Filter ? "its filter cutoff, as a percent of the FILTER knob"
                                                               : "its sample rate, as a percent of the SAMPLE knob");
    for (int i = 0; i < kKnobs; ++i)
        knobs[(size_t) i].step.setTooltip("STEP " + String(i + 1) + ": " + what + ". Fully left skips this step, fully right mutes it.");
    tooltipSeqType = (int) type;
}

void BitVibeEditor::buildBackground()
{
    const auto* display = Desktop::getInstance().getDisplays().getPrimaryDisplay();
    backgroundScale = jlimit(1.0f, 4.0f, scale * (float) (display != nullptr ? jmax(1.0, display->scale) : 1.0));
    background = Image(Image::ARGB, roundToInt((float) kPedalWidth * backgroundScale), roundToInt((float) kHeight * backgroundScale), true);
    Graphics g(background);
    g.addTransform(AffineTransform::scale(backgroundScale));

    g.fillAll(Colour(0xff0d0d0f));
    g.setColour(kInkDim);
    g.setFont(bold(12.0f));
    g.drawText("BITVIBE", Rectangle<int>(kWidth - 140, 8, 128, 24), Justification::centredRight);
    g.addTransform(AffineTransform::translation(0.0f, (float) kTabsH));

    const auto header = kHeader.toFloat();
    g.setColour(Colour(0xff17171a));
    g.fillRoundedRectangle(header, 10.0f);
    g.setColour(Colour(0xff2c2c31));
    g.drawRoundedRectangle(header.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(Colour(0xff0e0e10));
    g.fillRoundedRectangle(kNameBox.toFloat(), 4.0f);
    g.setColour(Colour(0xff3c3c42));
    g.drawRoundedRectangle(kNameBox.toFloat().reduced(0.5f), 4.0f, 1.0f);

    g.addTransform(AffineTransform::translation(0.0f, (float) kFaceDy));

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

    const auto dist = kDist.toFloat();
    g.setColour(Colour(0xff17171a));
    g.fillRoundedRectangle(dist, 10.0f);
    g.setColour(Colour(0xff2c2c31));
    g.drawRoundedRectangle(dist.reduced(0.5f), 10.0f, 1.0f);

    const auto reverb = kReverb.toFloat();
    g.setColour(Colour(0xff17171a));
    g.fillRoundedRectangle(reverb, 10.0f);
    g.setColour(Colour(0xff2c2c31));
    g.drawRoundedRectangle(reverb.reduced(0.5f), 10.0f, 1.0f);
}

void BitVibeEditor::applySize()
{
    constrainer.setMinimumSize(roundToInt((float) baseWidth * uisettings::kMinScale), roundToInt((float) baseHeight * uisettings::kMinScale));
    constrainer.setMaximumSize(roundToInt((float) baseWidth * uisettings::kMaxScale), roundToInt((float) baseHeight * uisettings::kMaxScale));
    setSize(roundToInt((float) baseWidth * scale), roundToInt((float) baseHeight * scale));
}

void BitVibeEditor::resized()
{
    AudioProcessorEditor::resized(); // keeps the resize corner in place

    // The pane keeps its designed size and is scaled to the window.
    if (baseWidth > 0)
    {
        const float newScale = jlimit(uisettings::kMinScale, uisettings::kMaxScale, (float) getWidth() / (float) baseWidth);
        if (std::abs(newScale - scale) > 1.0e-4f)
        {
            scale = newScale;
            scaleChangedMs = Time::getMillisecondCounterHiRes();
        }
        content.setBounds(0, 0, baseWidth, baseHeight);
        content.setTransform(AffineTransform::scale((float) getWidth() / (float) baseWidth, (float) getHeight() / (float) baseHeight));
    }

    // header: preset bar and morph strip
    prevButton.setBounds(kPrevBounds);
    nextButton.setBounds(kNextBounds);
    saveButton.setBounds(kSaveBounds);
    presetLabel.setBounds(kNameBox.reduced(40, 1));
    setUpButton.setBounds(kSetUpBounds);
    expressionSlider.setBounds(kExpressionBounds);
    setDownButton.setBounds(kSetDownBounds);
    morphButton.setBounds(kMorphBounds);

    // pedal face and global panel (laid out in face coordinates, then moved below the header)
    auto face = [](Rectangle<int> r) { return r.translated(0, kFaceDy); };

    for (int i = 0; i < kKnobs; ++i)
    {
        knobs[(size_t) i].front.setBounds(face(knobBounds(i)));
        knobs[(size_t) i].step.setBounds(face(knobBounds(i)));
    }

    typePitchButton.setBounds(face({ 18, 371, 50, 22 }));
    typeFilterButton.setBounds(face({ 71, 371, 50, 22 }));
    typeRateButton.setBounds(face({ 124, 371, 50, 22 }));
    tempoLabel.setBounds(face(Rectangle<int>(130, 30).withCentre({ 240, 418 })));
    altButton.setBounds(face(Rectangle<int>(78, 22).withCentre({ 392, 383 })));
    bypassSwitch.setBounds(face(Rectangle<int>(84, 84).withCentre({ 88, 462 })));
    tapSwitch.setBounds(face(Rectangle<int>(84, 84).withCentre({ 392, 462 })));

    // global panel: five knobs, then two rows of switches
    const std::array<Slider*, 5> small { &tempoSlider, &inSlider, &mixSlider, &outSlider, &trigSlider };
    for (size_t i = 0; i < small.size(); ++i)
        small[i]->setBounds(face(Rectangle<int>(52, 52).withCentre({ kGlobalKnobX0 + (int) i * kGlobalKnobDx, 585 })));

    syncButton.setBounds(face({ 24, 654, 62, 24 }));
    holdButton.setBounds(face({ 92, 654, 62, 24 }));
    headroomBox.setBounds(face({ 170, 654, 124, 24 }));
    stutterLenBox.setBounds(face({ 304, 654, 148, 24 }));
    fxTrigButton.setBounds(face({ 24, 686, 200, 24 }));

    // reverb panel: the type on the caption row, then five knobs on the same grid as the global panel
    reverbTypeBox.setBounds(face({ kSideX + 344, kReverb.getY() + 4, 100, 22 }));

    // distortion panel: the two switches on the caption row, three knobs below
    distTypeBox.setBounds(face({ kSideX + 136, kDist.getY() + 3, 118, 20 }));
    distOnButton.setBounds(face({ kSideX + 266, kDist.getY() + 3, 56, 20 }));
    distReverbButton.setBounds(face({ kSideX + 328, kDist.getY() + 3, 120, 20 }));
    const std::array<Slider*, 3> distKnobs { &distDriveSlider, &distToneSlider, &distLevelSlider };
    for (size_t i = 0; i < distKnobs.size(); ++i)
        distKnobs[i]->setBounds(face(Rectangle<int>(48, 48).withCentre({ kSideX + 76 + (int) i * 152, kDist.getY() + 48 })));
    const std::array<Slider*, 5> reverbKnobs { &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider, &reverbLowSlider, &reverbHighSlider };
    for (size_t i = 0; i < reverbKnobs.size(); ++i)
        reverbKnobs[i]->setBounds(face(Rectangle<int>(48, 48).withCentre({ kGlobalKnobX0 + kColDx + (int) i * kGlobalKnobDx, kReverb.getY() + 40 })));
    samplePanel->setBounds(face({ kSideX, kSamplesY, SamplePanel::kWidth, SamplePanel::kHeight }));
    patternPanel->setBounds(face({ kSideX, kPatternY, PatternPanel::kWidth, PatternPanel::kHeight }));

    // Everything above was laid out as if the pedal started at the top: move it below the tab strip.
    for (auto* c : pedalComponents)
        c->setTopLeftPosition(c->getX(), c->getY() + kTabsH);

    pedalTab.setBounds(12, 8, 96, 24);
    drumsTab.setBounds(114, 8, 96, 24);
    drumPage->setBounds(0, kTabsH, kWidth, baseHeight - kTabsH);
}

void BitVibeEditor::paintContent(Graphics& g)
{
    // The background is drawn at the window's own pixel size, so it stays sharp when the window is made bigger.
    g.drawImage(background, 0, 0, baseWidth, baseHeight, 0, 0, roundToInt((float) baseWidth * backgroundScale), roundToInt((float) baseHeight * backgroundScale));
    if (drumsShown)
        return; // the DRUMS page paints everything below the tabs
    g.addTransform(AffineTransform::translation(0.0f, (float) kTabsH));

    // Header: slot number and modified mark beside the preset name, state line under the morph strip
    {
        const bool morphOn = rawValue(ids::morph) > 0.5f;
        g.setColour(kInkDim);
        g.setFont(bold(11.0f));
        g.drawText(String(proc.getCurrentProgram() + 1).paddedLeft('0', 2),
                   kNameBox.withWidth(40).translated(2, 0), Justification::centred);
        if (proc.isCurrentPresetModified())
        {
            g.setColour(kAccent);
            g.fillEllipse(Rectangle<float>(7.0f, 7.0f).withCentre({ (float) kNameBox.getRight() - 16.0f, (float) kNameBox.getCentreY() }));
        }

        g.setColour(morphOn ? kAccent : kInkDim);
        g.setFont(plain(10.0f));
        const String caption = morphOn ? "MORPH ON: THE SOUND FOLLOWS EXPRESSION " + String(roundToInt(rawValue(ids::expression) * 100.0f))
                                             + "%, THE KNOBS ONLY FILL THE SETS"
                                       : "MORPH OFF: THE KNOBS ARE HEARD";
        g.drawText(caption, kCaptionBounds, Justification::centred);
    }

    g.addTransform(AffineTransform::translation(0.0f, (float) kFaceDy));

    // Title
    g.setColour(kInk);
    g.setFont(bold(26.0f));
    g.drawText("BITVIBE", Rectangle<int>(kFace.getX(), 22, kFace.getWidth(), 32), Justification::centred);
    g.setColour(kInkDim);
    g.setFont(plain(10.5f));
    g.drawText("UNOFFICIAL  -  INDEPENDENT PROJECT", Rectangle<int>(kFace.getX(), 54, kFace.getWidth(), 14), Justification::centred);

    const int currentStep = proc.getEngine().getCurrentStep();
    const bool morphOn = rawValue(ids::morph) > 0.5f;

    // Knob labels and readouts
    for (int i = 0; i < kKnobs; ++i)
    {
        const auto c = knobCentre(i);
        const Colour ink = altLayer ? kAccent : kInk;

        g.setColour(ink);
        g.setFont(bold(12.5f));
        const String label = altLayer ? "STEP " + String(i + 1) : String(knobs[(size_t) i].label);
        g.drawText(label, Rectangle<int>(112, 16).withCentre({ (int) c.x, (int) c.y - 62 }), Justification::centred);

        g.setColour(ink.withAlpha(morphOn ? 0.4f : 0.75f)); // with the morph on, the knob values are not what you hear
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
    drawLed(g, { 88.0f, 410.0f }, 6.0f, kRed, !bypassed);

    const bool tapBlink = proc.getEngine().getBeatPhase() < 0.18f;
    drawLed(g, { 392.0f, 410.0f }, 6.0f, kAccent, tapBlink);
    drawLed(g, { 392.0f - 52.0f, 383.0f }, 4.5f, kAccent, altLayer);

    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("SEQ TYPE", Rectangle<int>(18, 354, 156, 14), Justification::centred);

    g.setColour(kInk);
    g.setFont(bold(11.0f));
    g.drawText("BYPASS", Rectangle<int>(100, 14).withCentre({ 88, 512 }), Justification::centred);
    g.drawText("TAP / HOLD", Rectangle<int>(100, 14).withCentre({ 392, 512 }), Justification::centred);

    // Centre read-out: tempo
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("TEMPO", Rectangle<int>(100, 12).withCentre({ 240, 392 }), Justification::centred);
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
    const std::array<Entry, 5> entries {{
        { "TEMPO", String(rawValue(ids::tempo), 1) },
        { "INPUT", String(rawValue(ids::inputGain), 1) + " dB" },
        { "MIX", String(roundToInt(rawValue(ids::mix) * 100.0f)) + "%" },
        { "OUTPUT", String(rawValue(ids::outputGain), 1) + " dB" },
        { "TRIGGER", String(roundToInt(rawValue(ids::sensitivity) * 100.0f)) + "%" },
    }};
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const int cx = kGlobalKnobX0 + (int) i * kGlobalKnobDx;
        g.setColour(kInk);
        g.setFont(bold(11.0f));
        g.drawText(entries[i].name, Rectangle<int>(100, 14).withCentre({ cx, 619 }), Justification::centred);
        g.setColour(kInkDim);
        g.setFont(plain(10.5f));
        g.drawText(entries[i].value, Rectangle<int>(100, 13).withCentre({ cx, 633 }), Justification::centred);
    }

    // Distortion panel labels
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("DISTORTION / FUZZ", Rectangle<int>(kSideX + 14, kDist.getY() + 4, 120, 12), Justification::left);
    const bool fuzz = roundToInt(rawValue(ids::distType)) == 1; // the big-fuzz style calls its knobs sustain / tone / volume
    const std::array<Entry, 3> distEntries {{
        { fuzz ? "SUSTAIN" : "DRIVE", String(roundToInt(rawValue(ids::distDrive) * 100.0f)) + "%" },
        { "TONE", String(roundToInt(rawValue(ids::distTone) * 100.0f)) + "%" },
        { fuzz ? "VOLUME" : "LEVEL", String(roundToInt(rawValue(ids::distLevel) * 100.0f)) + "%" },
    }};
    for (size_t i = 0; i < distEntries.size(); ++i)
    {
        const int cx = kSideX + 76 + (int) i * 152;
        g.setColour(kInk);
        g.setFont(bold(11.0f));
        g.drawText(distEntries[i].name, Rectangle<int>(100, 14).withCentre({ cx, kDist.getY() + 79 }), Justification::centred);
        g.setColour(kInkDim);
        g.setFont(plain(10.5f));
        g.drawText(distEntries[i].value, Rectangle<int>(100, 13).withCentre({ cx, kDist.getY() + 92 }), Justification::centred);
    }

    // Reverb panel labels
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("REVERB (SEQUENCED SOUND ONLY)", Rectangle<int>(kSideX + 14, kReverb.getY() + 4, 260, 12), Justification::left);
    const float lowCut = rawValue(ids::reverbLowCut), highCut = rawValue(ids::reverbHighCut);
    const std::array<Entry, 5> reverbEntries {{
        { "TONE", String(roundToInt(rawValue(ids::reverbTone) * 100.0f)) + "%" },
        { "MIX", String(roundToInt(rawValue(ids::reverbMix) * 100.0f)) + "%" },
        { "LEVEL", String(rawValue(ids::reverbLevel), 1) + " dB" },
        { "LOW CUT", lowCut <= 20.5f ? String("OFF") : hzText(lowCut) },
        { "HIGH CUT", highCut >= 19950.0f ? String("OFF") : hzText(highCut) },
    }};
    for (size_t i = 0; i < reverbEntries.size(); ++i)
    {
        const int cx = kGlobalKnobX0 + kColDx + (int) i * kGlobalKnobDx;
        g.setColour(kInk);
        g.setFont(bold(11.0f));
        g.drawText(reverbEntries[i].name, Rectangle<int>(100, 14).withCentre({ cx, kReverb.getY() + 74 }), Justification::centred);
        g.setColour(kInkDim);
        g.setFont(plain(10.5f));
        g.drawText(reverbEntries[i].value, Rectangle<int>(100, 13).withCentre({ cx, kReverb.getY() + 88 }), Justification::centred);
    }
}

void BitVibeEditor::timerCallback()
{
    // Holding the tap switch freezes the stutter buffer (manual, section 4).
    if (tapDown && !holdFromTap && Time::getMillisecondCounterHiRes() - tapPressMs > 300.0)
    {
        holdFromTap = true;
        holdParam.beginGesture();
        holdParam.setValueAsPartOfGesture(1.0f);
    }

    {
        const double nowMs = Time::getMillisecondCounterHiRes();
        const bool settled = scaleChangedMs > 0.0 && nowMs - scaleChangedMs > 400.0;
        if (settled && std::abs(scale - savedScale) > 1.0e-3f)
        {
            savedScale = scale;
            uisettings::saveWindowScale(scale);
        }
        const float wanted = scale * (float) jmax(1.0, Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                                          ? Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale : 1.0);
        if (settled && std::abs(wanted - backgroundScale) > 0.02f)
            buildBackground();
    }
    refreshPresetName();
    showSeqType();
    {
        const bool sync = rawValue(ids::tempoSync) > 0.5f;
        if (sync == tempoEditable) // typing a tempo does nothing while the host tempo is followed
        {
            tempoEditable = !sync;
            tempoLabel.setEditable(tempoEditable, tempoEditable, false);
        }
        if (!tempoLabel.isBeingEdited())
            tempoLabel.setText(String(proc.getEffectiveBpm(), 1), dontSendNotification);
    }
    const double now = Time::getMillisecondCounterHiRes();
    auto flash = [now](PedalButton& b, double until)
    {
        const bool on = now < until;
        if (b.lit != on)
        {
            b.lit = on;
            b.repaint();
        }
    };
    flash(setUpButton, flashUpUntil);
    flash(setDownButton, flashDownUntil);
    flash(saveButton, flashSaveUntil);
    expressionSlider.getProperties().set("active", rawValue(ids::morph) > 0.5f);

    const auto type = seqType();
    if ((int) type != tooltipSeqType)
        updateStepTooltips();
    for (auto& k : knobs)
    {
        const int ticks = type == bitvibe::SeqType::Pitch ? 27 : 11;
        if ((int) k.step.getProperties().getWithDefault("ticks", 11) != ticks)
            k.step.getProperties().set("ticks", ticks);
    }
    if (!drumsShown)
    {
        samplePanel->refresh();
        patternPanel->refresh();
        repaint();
    }
}
