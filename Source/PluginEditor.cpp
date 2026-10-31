#include "PluginEditor.h"

#include "DrumPage.h"
#include "PatternPanel.h"
#include "SamplePanel.h"
#include "EqPanel.h"
#include "UiSettings.h"

#include <functional>

using namespace juce;
using pedalstyle::activeProperty;
using pedalstyle::drawPanelTitle;
using pedalstyle::kOffAlpha;
using pedalstyle::kPanelTitleX;
using pedalstyle::makePowerButton;
using pedalstyle::powerProperty;
using pedalstyle::setPowerActive;

namespace
{
// The pedal face and the global panel are laid out in their own coordinates; everything below the
// header panel is shifted down by kFaceDy. The whole pedal page sits below the tab strip (kTabsH).
constexpr int kTabsH = 36;
constexpr int kFaceDy = 100;
constexpr int kWidth = 480;
constexpr bool kShowDrumsTab = false;
constexpr int kHeight = kTabsH + 804 + kFaceDy;
// The pedal page has a second column to the right of the pedal: reverb, sample blend and beat pattern panels.
constexpr int kSideX = kWidth + 12, kPedalWidth = kWidth + 468;
const Rectangle<int> kHeader { 12, 12, 456, 88 };
const Rectangle<int> kFace { 12, 12, 456, 520 };
const Rectangle<int> kGlobal { 12, 544, 456, 248 };
// Second-column panels, in face coordinates (so the first one starts level with the preset header).
constexpr int kColY = 12 - kFaceDy;
constexpr int kPanelGap = 8;

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
    setColour(BubbleComponent::backgroundColourId, Colour(0xff26262b));
    setColour(BubbleComponent::outlineColourId, Colour(0xff55555d));
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
    const auto& props = slider.getProperties();
    const Colour ink = props.contains("inkColour") ? Colour((uint32) (int) props["inkColour"]) : (alt ? kAccent : kInk);
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

void pedalstyle::drawPanelTitle(Graphics& g, int x, int y, int number, const String& title)
{
    const Rectangle<float> badge((float) x, (float) y + 0.5f, 13.0f, 13.0f);
    g.setColour(kAccent.withAlpha(0.85f));
    g.fillEllipse(badge);
    g.setColour(Colour(0xff17171a));
    g.setFont(bold(9.5f));
    g.drawText(String(number), badge, Justification::centred);
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText(title, x + 18, y + 1, 300, 12, Justification::left);
}

void PedalLookAndFeel::drawToggleButton(Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    if ((bool) b.getProperties().getWithDefault(powerProperty, false))
    {
        // a power button: a light and ON / OFF
        g.setColour(Colour(0xff111113));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(highlighted ? Colour(0xff6a6a72) : Colour(0xff34343a));
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
        const Point<float> c(r.getX() + 10.0f, r.getCentreY());
        if (on && (bool) b.getProperties().getWithDefault(activeProperty, false))
        {
            g.setColour(kAccent.withAlpha(0.3f));
            g.fillEllipse(c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
        }
        g.setColour(on ? kAccent : Colour(0xff3c3c42));
        g.fillEllipse(c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
        g.setColour(on ? kInk : kInkDim);
        g.setFont(bold(10.0f));
        g.drawText(on ? "ON" : "OFF", r.withTrimmedLeft(18.0f), Justification::centred);
        return;
    }
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

// The text runs up to the arrow (drawn 10..18 px from the right edge), so narrow boxes still show a step number.
void PedalLookAndFeel::positionComboBoxText(ComboBox& box, Label& label)
{
    label.setBounds(1, 1, jmax(0, box.getWidth() - 20), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
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

void StepStrip::set(const std::array<Step, 6>& steps, int playing, int loopFrom, int loopTo)
{
    const int lo = loopFrom < 0 || loopTo < 0 ? -1 : jmin(loopFrom, loopTo);
    const int hi = loopFrom < 0 || loopTo < 0 ? -1 : jmax(loopFrom, loopTo);
    bool changed = playing != current || lo != loopLo || hi != loopHi;
    for (size_t i = 0; i < 6 && !changed; ++i)
        changed = std::abs(steps[i].knob - shown[i].knob) > 0.002f || steps[i].kind != shown[i].kind || steps[i].type != shown[i].type;
    if (changed)
    {
        shown = steps;
        current = playing;
        loopLo = lo;
        loopHi = hi;
        repaint();
    }
}

int StepStrip::stepAt(float x) const
{
    return jlimit(0, 5, (int) std::floor(x / ((float) getWidth() / 6.0f)));
}

void StepStrip::mouseDown(const MouseEvent& e)
{
    menuClick = e.mods.isPopupMenu();
    downStep = stepAt(e.position.x);
    dragged = false;
    if (menuClick && onStepMenu)
        onStepMenu(downStep);
}

void StepStrip::mouseDrag(const MouseEvent& e)
{
    if (menuClick || downStep < 0)
        return;
    const int s = stepAt(e.position.x);
    if (s != downStep || dragged)
    {
        dragged = true;
        if (onLoopDrag)
            onLoopDrag(jmin(downStep, s), jmax(downStep, s));
    }
}

void StepStrip::mouseUp(const MouseEvent& e)
{
    if (menuClick)
        return;
    if (dragged)
    {
        if (onLoopDragEnd)
            onLoopDragEnd();
    }
    else if (getLocalBounds().contains(e.getPosition()) && downStep >= 0 && onStepClick)
    {
        onStepClick(downStep);
    }
    downStep = -1;
    dragged = false;
}

void StepStrip::paint(Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour(Colour(0xff0e0e10).withAlpha(0.8f));
    g.fillRoundedRectangle(b, 3.0f);
    const float slot = b.getWidth() / 6.0f;
    if (loopLo >= 0) // the loop section: a frame around its steps
    {
        g.setColour(kAccent.withAlpha(0.14f));
        g.fillRoundedRectangle(Rectangle<float>(b.getX() + slot * (float) loopLo, b.getY(), slot * (float) (loopHi - loopLo + 1), b.getHeight()), 3.0f);
    }
    for (int i = 0; i < 6; ++i)
    {
        const auto cell = Rectangle<float>(b.getX() + slot * (float) i, b.getY(), slot, b.getHeight()).reduced(3.0f, 4.0f);
        const bool lit = i == current;
        const auto& s = shown[(size_t) i];
        const Colour colour = pedalstyle::stepTypeColour(s.type);
        g.setColour(lit ? colour.brighter(0.4f) : colour.withAlpha(0.7f));
        if (s.kind == 0) // skipped
            g.fillRect(cell.getX() + 1.0f, cell.getCentreY() - 0.75f, cell.getWidth() - 2.0f, 1.5f);
        else if (s.kind == 1) // muted
        {
            const auto x = cell.reduced(cell.getWidth() * 0.2f, cell.getHeight() * 0.2f);
            g.drawLine(x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
            g.drawLine(x.getX(), x.getBottom(), x.getRight(), x.getY(), 1.4f);
        }
        else
        {
            const float h = jmax(2.0f, cell.getHeight() * jlimit(0.0f, 1.0f, s.knob));
            g.fillRoundedRectangle(cell.withTop(cell.getBottom() - h), 1.5f);
        }
        if (lit) // the step playing now
        {
            g.setColour(Colours::white.withAlpha(0.85f));
            g.drawRoundedRectangle(cell.expanded(1.5f, 2.0f), 2.0f, 1.2f);
        }
    }
    g.setColour(Colour(0xff3c3c42));
    g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 1.0f);
    if (loopLo >= 0)
    {
        g.setColour(kAccent.withAlpha(0.8f));
        g.drawRoundedRectangle(Rectangle<float>(b.getX() + slot * (float) loopLo, b.getY(), slot * (float) (loopHi - loopLo + 1), b.getHeight()).reduced(1.0f),
                               3.0f, 1.5f);
    }
}

void StepTypeTags::set(int newOwn, int newEffective)
{
    if (newOwn != own || newEffective != effective)
    {
        own = newOwn;
        effective = newEffective;
        repaint();
    }
}

void StepTypeTags::paint(Graphics& g)
{
    static const char* letters[3] { "P", "F", "S" };
    const float w = ((float) getWidth() - 4.0f) / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        const auto r = Rectangle<float>((w + 2.0f) * (float) i, 0.0f, w, (float) getHeight()).reduced(0.5f);
        const Colour colour = pedalstyle::stepTypeColour(i);
        const bool chosen = own == i + 1, following = own == 0 && effective == i;
        if (chosen)
        {
            g.setColour(colour);
            g.fillRoundedRectangle(r, 3.0f);
        }
        g.setColour(chosen || following ? colour : kInkDim.withAlpha(0.45f));
        g.drawRoundedRectangle(r, 3.0f, following ? 1.4f : 1.0f);
        g.setColour(chosen ? Colour(0xff111113) : (following ? colour : kInkDim.withAlpha(0.6f)));
        g.setFont(bold(10.5f));
        g.drawText(letters[i], r, Justification::centred);
    }
}

void StepTypeTags::mouseUp(const MouseEvent& e)
{
    if (!getLocalBounds().contains(e.getPosition()) || !onPick)
        return;
    const int i = jlimit(0, 2, (int) std::floor(e.position.x / ((float) getWidth() / 3.0f)));
    onPick(own == i + 1 ? 0 : i + 1);
}

void HelpOverlay::paint(Graphics& g)
{
    g.fillAll(Colours::black.withAlpha(0.84f));
    const auto card = getLocalBounds().reduced(60, 185).toFloat();
    g.setColour(Colour(0xff17171a));
    g.fillRoundedRectangle(card, 12.0f);
    g.setColour(kAccent);
    g.drawRoundedRectangle(card.reduced(0.5f), 12.0f, 1.5f);

    auto area = card.reduced(28.0f, 22.0f).toNearestInt();
    g.setColour(kInk);
    g.setFont(bold(20.0f));
    g.drawText("QUICK START", area.removeFromTop(28), Justification::left);
    g.setColour(kInkDim);
    g.setFont(plain(11.0f));
    g.drawText("Click anywhere to close. Open it again with the HELP button at the top.", area.removeFromTop(18), Justification::left);
    area.removeFromTop(10);

    static const char* steps[] = {
        "1.  Play. A picked note starts the STUTTER and the SEQUENCER. TRIGGER sets how hard you must pick; the thin meter "
        "next to it shows your playing against that line and flashes when a pick is caught.",
        "2.  Turn up SEQUENCER (how many times the six-step sequence plays) and STUTTER (how many repeats). SEQ TYPE picks what "
        "the steps change: pitch, filter or sample rate. The coloured bars above TEMPO are the steps: click one to see its knob, "
        "drag across them to loop those steps.",
        "3.  SAMPLE, FILTER and BITS crush and darken the sound. DRY/WET blends it with your clean signal (SEQ ONLY cuts the clean "
        "signal and sends out only the sequenced sound); OUTPUT sets the level.",
        "4.  On the right, the BEAT PATTERN can start the sequence on chosen steps of a bar. Below it, the numbered panels work on "
        "the sequenced sound only, in that order: random sounds, distortion or fuzz, delay, reverb, EQ. Their ON lights glow while they act.",
        "5.  Hover over anything for a short description. Double-click a knob to reset it. Drag the bottom-right corner to resize the window.",
    };
    g.setFont(plain(12.5f));
    for (const auto* text : steps)
    {
        g.setColour(kInk);
        g.drawFittedText(text, area.removeFromTop(48), Justification::topLeft, 3, 1.0f); // wrap, never squeeze
        area.removeFromTop(4);
    }

    // where the sound goes
    area.removeFromTop(8);
    g.setColour(kInkDim);
    g.setFont(bold(11.0f));
    g.drawText("WHERE THE SOUND GOES", area.removeFromTop(18), Justification::left);
    area.removeFromTop(6);
    static const char* boxes[] = { "INPUT", "STUTTER +\nSEQUENCE", "CRUSH +\nFILTER", "DISTORTION\n/ FUZZ", "+ REVERB", "DRY/WET", "OUTPUT" };
    auto row = area.removeFromTop(48);
    const int n = (int) std::size(boxes);
    const float w = (float) row.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const auto box = Rectangle<float>((float) row.getX() + w * (float) i, (float) row.getY(), w, (float) row.getHeight()).reduced(8.0f, 4.0f);
        g.setColour(Colour(0xff26262b));
        g.fillRoundedRectangle(box, 6.0f);
        g.setColour(i == 0 || i == n - 1 ? kInkDim : kAccent);
        g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, 1.0f);
        g.setColour(kInk);
        g.setFont(bold(10.5f));
        g.drawFittedText(boxes[i], box.toNearestInt().reduced(3, 2), Justification::centred, 2);
        if (i + 1 < n)
        {
            g.setColour(kInkDim);
            const float y = box.getCentreY(), x0 = box.getRight() + 1.0f, x1 = box.getRight() + 15.0f;
            g.drawArrow(Line<float>(x0, y, x1, y), 1.5f, 6.0f, 6.0f);
        }
    }
    area.removeFromTop(8);
    g.setColour(kInkDim);
    g.setFont(plain(11.5f));
    g.drawFittedText("The random sounds of the SAMPLE BLEND join the sequence before the crush, so they are crushed with it. The reverb hears the "
                     "sequence before the distortion (tick REVERB TOO to distort the reverb as well). DRY/WET then blends all of it with your "
                     "clean signal, which never gets distortion or reverb.",
                     area.removeFromTop(48), Justification::topLeft, 3, 1.0f);
}

void LevelMeter::set(float levelDb, float thresholdDb, bool flash)
{
    if (std::abs(levelDb - level) > 0.2f || std::abs(thresholdDb - threshold) > 0.05f || flash != lit)
    {
        level = levelDb;
        threshold = thresholdDb;
        lit = flash;
        repaint();
    }
}

void LevelMeter::paint(Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    auto toY = [&b](float db) { return b.getBottom() - b.getHeight() * jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    g.setColour(Colour(0xff0e0e10));
    g.fillRoundedRectangle(b, 2.0f);
    const float top = toY(level);
    if (top < b.getBottom())
    {
        g.setColour(lit ? kAccent : (level >= threshold ? Colour(0xffd9b36a) : Colour(0xff6f9a5c)));
        g.fillRect(b.withTop(top).reduced(1.0f, 0.0f));
    }
    g.setColour(kInk);
    g.fillRect(b.getX(), toY(threshold) - 0.75f, b.getWidth(), 1.5f);
    g.setColour(Colour(0xff3c3c42));
    g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
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
        addKnobHelpers(k.front, frontIds[i]);
        addKnobHelpers(k.step, ids::step(i));
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
        addKnobHelpers(s, id);
        content.addAndMakeVisible(s);
    };
    triggerMeter.setTooltip("INPUT METER: how hard you are playing (after INPUT). The white line is how hard a pick has to be to start the "
                            "stutter and sequencer (set with TRIGGER); the bar lights orange when a pick is caught.");
    content.addAndMakeVisible(triggerMeter);
    stepStrip.setTooltip("THE SEQUENCE: one bar per step, coloured by what it controls (blue = pitch, green = filter, purple = sample rate; "
                         "a dash = skipped, a cross = muted). Click a step to show its knob. Drag across steps to loop them. Right-click a step for more.");
    stepStrip.onStepClick = [this](int step)
    {
        setAltLayer(true);
        flashStep = step;
        flashStepUntil = Time::getMillisecondCounterHiRes() + 1200.0;
    };
    stepStrip.onLoopDrag = [this](int first, int last) { setLoopFromStrip(first, last); };
    stepStrip.onLoopDragEnd = [this]
    {
        if (loopDragging)
            for (const char* id : { ids::loopOn, ids::loopFrom, ids::loopTo })
                proc.apvts.getParameter(id)->endChangeGesture();
        loopDragging = false;
    };
    stepStrip.onStepMenu = [this](int step) { showStepMenu(step); };
    content.addAndMakeVisible(stepStrip);
    smallKnob(tempoSlider, tempoAtt, ids::tempo);
    smallKnob(inSlider, inAtt, ids::inputGain);
    smallKnob(mixSlider, mixAtt, ids::mix);
    smallKnob(outSlider, outAtt, ids::outputGain);
    smallKnob(trigSlider, trigAtt, ids::sensitivity);
    smallKnob(reverbDecaySlider, reverbDecayAtt, ids::reverbDecay);
    smallKnob(reverbToneSlider, reverbToneAtt, ids::reverbTone);
    reverbReverseButton.setLookAndFeel(&laf);
    content.addAndMakeVisible(reverbReverseButton);
    reverbOnButton.setLookAndFeel(&laf);
    content.addAndMakeVisible(reverbOnButton);
    reverbOnAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::reverbOn, reverbOnButton);
    reverbReverseButton.onClick = [this]
    {
        // the tick belongs to the type of reverb that is showing
        static const char* tickIds[6] = { ids::reverbReverseRoom, ids::reverbReverseHall, ids::reverbReverseSpring,
                                          ids::reverbReversePlate, ids::reverbReverseShimmer, ids::reverbReverseGated };
        if (auto* param = proc.apvts.getParameter(tickIds[jlimit(0, 5, roundToInt(rawValue(ids::reverbType)))]))
            param->setValueNotifyingHost(reverbReverseButton.getToggleState() ? 1.0f : 0.0f);
    };
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
    smallKnob(delayMixSlider, delayMixAtt, ids::delayMix);
    smallKnob(delayDecaySlider, delayDecayAtt, ids::delayDecay);
    smallKnob(delayRepeatsSlider, delayRepeatsAtt, ids::delayRepeats);
    smallKnob(delayLowSlider, delayLowAtt, ids::delayLowCut);
    smallKnob(delayHighSlider, delayHighAtt, ids::delayHighCut);
    delayOnButton.setLookAndFeel(&laf);
    content.addAndMakeVisible(delayOnButton);
    delayOnAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::delayOn, delayOnButton);
    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(ids::delayTime)))
        delayTimeBox.addItemList(choice->choices, 1);
    delayTimeBox.setLookAndFeel(&laf);
    content.addAndMakeVisible(delayTimeBox);
    delayTimeAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::delayTime, delayTimeBox);
    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(ids::reverbType)))
        reverbTypeBox.addItemList(choice->choices, 1);
    reverbTypeBox.setLookAndFeel(&laf);
    content.addAndMakeVisible(reverbTypeBox);
    reverbTypeAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::reverbType, reverbTypeBox);
    samplePanel = std::make_unique<SamplePanel>(proc, laf);
    content.addAndMakeVisible(*samplePanel);
    eqPanel = std::make_unique<EqPanel>(proc, laf);
    content.addAndMakeVisible(*eqPanel);
    patternPanel = std::make_unique<PatternPanel>(proc, laf);
    content.addAndMakeVisible(*patternPanel);

    for (auto* b : { &syncButton, &holdButton, &fxTrigButton, &seqOnlyButton })
    {
        b->setLookAndFeel(&laf);
        content.addAndMakeVisible(*b);
    }
    syncAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::tempoSync, syncButton);
    holdAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::hold, holdButton);
    fxTrigAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::fxTriggers, fxTrigButton);
    seqOnlyAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::seqOnly, seqOnlyButton);

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

    // the loop section: the items are listed in the parameters' order, so the attachments match them up
    loopButton.setLookAndFeel(&laf);
    content.addAndMakeVisible(loopButton);
    loopAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::loopOn, loopButton);
    for (int i = 0; i < bitvibe::kNumSteps; ++i)
    {
        loopFromBox.addItem(String(i + 1), i + 1);
        loopToBox.addItem(String(i + 1), i + 1);
    }
    loopRepeatsBox.addItemList({ "forever", "2 times", "3 times", "4 times", "8 times", "16 times" }, 1);
    for (auto* box : { &loopFromBox, &loopToBox, &loopRepeatsBox })
    {
        box->setLookAndFeel(&laf);
        content.addAndMakeVisible(*box);
    }
    loopFromAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::loopFrom, loopFromBox);
    loopToAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::loopTo, loopToBox);
    loopRepeatsAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::loopRepeats, loopRepeatsBox);

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
    // switching the type remembers the step knobs of the type being left and brings back those of the new one
    typePitchButton.onClick = [this] { proc.switchSequencerType(0); showSeqType(); };
    typeFilterButton.onClick = [this] { proc.switchSequencerType(1); showSeqType(); };
    typeRateButton.onClick = [this] { proc.switchSequencerType(2); showSeqType(); };

    content.addAndMakeVisible(bypassSwitch);
    content.addAndMakeVisible(tapSwitch);
    content.addAndMakeVisible(altButton);
    for (auto* b : { &typePitchButton, &typeFilterButton, &typeRateButton })
        content.addAndMakeVisible(*b);
    showSeqType();

    // what each step controls (Alt layer): the sequencer type, or a type of its own
    for (int i = 0; i < kKnobs; ++i)
    {
        auto& tags = stepTypeTags[(size_t) i];
        tags.setTooltip("STEP " + String(i + 1) + " CONTROLS: P = pitch, F = filter, S = sample rate. A lit letter is this step's own choice; "
                        "with none lit it follows the SEQ TYPE buttons (outlined). Click the lit letter again to follow them.");
        tags.setMouseCursor(MouseCursor::PointingHandCursor);
        tags.onPick = [this, i](int choice) { setStepType(i, choice); };
        content.addChildComponent(tags);
    }

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

    pedalComponents = { &tempoSlider, &inSlider, &mixSlider, &outSlider, &trigSlider, &syncButton, &holdButton, &fxTrigButton, &seqOnlyButton,
                        &reverbDecaySlider, &reverbOnButton, &reverbReverseButton, &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider, &reverbLowSlider, &reverbHighSlider, &reverbTypeBox, &delayMixSlider, &delayDecaySlider, &delayRepeatsSlider, &delayLowSlider, &delayHighSlider, &delayTimeBox, &delayOnButton, &distDriveSlider, &distToneSlider, &distLevelSlider, &distOnButton, &distReverbButton, &distTypeBox, samplePanel.get(), eqPanel.get(), patternPanel.get(),
                        &headroomBox, &stutterLenBox, &loopButton, &loopFromBox, &loopToBox, &loopRepeatsBox, &bypassSwitch, &tapSwitch, &triggerMeter, &stepStrip, &altButton, &typePitchButton, &typeFilterButton, &typeRateButton, &presetLabel, &tempoLabel,
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

    // the quick-start guide: on top of everything, opens by itself the first time
    helpButton.setTooltip("HELP: a one-page quick start, and how the sound travels through BitVibe.");
    helpButton.onClick = [this]
    {
        helpOverlay.setVisible(true);
        helpOverlay.toFront(false);
    };
    content.addAndMakeVisible(helpButton);
    exportButton.setTooltip("EXPORT: save the sound you hear as a preset file (XML) to give to other players: the knobs, the sequence, "
                            "the effects, the beat pattern and the list of sounds. Your levels and trigger setting stay out.");
    importButton.setTooltip("IMPORT: load a preset file someone gave you into the preset slot that is showing (it replaces that slot's "
                            "name and sound; pick a free slot first if you want to keep it).");
    exportButton.onClick = [this] { exportPresetFile(); };
    importButton.onClick = [this] { importPresetFile(); };
    content.addAndMakeVisible(exportButton);
    content.addAndMakeVisible(importButton);
    simpleButton.setTooltip("SIMPLE: the right-hand side shows only the controls you need to make a sound. What it hides keeps its settings.");
    advancedButton.setTooltip("ADVANCED: the right-hand side shows every control: tone, cuts, levels, sound banks, sample volumes, time signature and more.");
    simpleButton.onClick = [this] { setRightMode(false); };
    advancedButton.onClick = [this] { setRightMode(true); };
    content.addAndMakeVisible(simpleButton);
    content.addAndMakeVisible(advancedButton);
    advancedRight = uisettings::advancedRight();
    applyRightMode();
    content.addChildComponent(helpOverlay);
    if (!uisettings::helpSeen())
    {
        helpOverlay.setVisible(true);
        uisettings::markHelpSeen();
    }
    updateLiveState();
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
    for (auto* s : { &tempoSlider, &inSlider, &mixSlider, &outSlider, &trigSlider, &reverbDecaySlider, &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider, &reverbLowSlider, &reverbHighSlider, &distDriveSlider, &distToneSlider, &distLevelSlider,
                      &delayMixSlider, &delayDecaySlider, &delayRepeatsSlider, &delayLowSlider, &delayHighSlider })
        s->setLookAndFeel(nullptr);
    delayTimeBox.setLookAndFeel(nullptr);
    delayOnButton.setLookAndFeel(nullptr);
    distOnButton.setLookAndFeel(nullptr);
    distTypeBox.setLookAndFeel(nullptr);
    distReverbButton.setLookAndFeel(nullptr);
    reverbTypeBox.setLookAndFeel(nullptr);
    reverbReverseButton.setLookAndFeel(nullptr);
    reverbOnButton.setLookAndFeel(nullptr);
    syncButton.setLookAndFeel(nullptr);
    holdButton.setLookAndFeel(nullptr);
    fxTrigButton.setLookAndFeel(nullptr);
    seqOnlyButton.setLookAndFeel(nullptr);
    morphButton.setLookAndFeel(nullptr);
    expressionSlider.setLookAndFeel(nullptr);
    presetLabel.setLookAndFeel(nullptr);
    tempoLabel.setLookAndFeel(nullptr);
    headroomBox.setLookAndFeel(nullptr);
    stutterLenBox.setLookAndFeel(nullptr);
    loopButton.setLookAndFeel(nullptr);
    loopFromBox.setLookAndFeel(nullptr);
    loopToBox.setLookAndFeel(nullptr);
    loopRepeatsBox.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// Every knob: double-click puts it back to its default, and while it is turned a bubble shows its value.
void BitVibeEditor::addKnobHelpers(Slider& s, const String& paramId)
{
    if (auto* param = proc.apvts.getParameter(paramId))
        s.setDoubleClickReturnValue(true, (double) param->convertFrom0to1(param->getDefaultValue()));
    s.setPopupDisplayEnabled(true, false, &content);
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
    for (auto& tags : stepTypeTags)
        tags.setVisible(on);
    updateLiveState();
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
    {
        setAltLayer(altLayer); // puts the right layer of knobs back
        applyRightMode();
    }
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

void BitVibeEditor::exportPresetFile()
{
    const auto folder = BitVibeProcessor::presetFolder();
    folder.createDirectory();
    const auto name = File::createLegalFileName(proc.getProgramName(proc.getCurrentProgram()).trim());
    presetChooser = std::make_unique<FileChooser>("Export preset", folder.getChildFile((name.isEmpty() ? String("BitVibe preset") : name) + ".xml"), "*.xml");
    Component::SafePointer<BitVibeEditor> safe(this);
    presetChooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                               [safe](const FileChooser& chooser)
                               {
                                   if (safe == nullptr)
                                       return;
                                   auto file = chooser.getResult();
                                   if (file == File())
                                       return;
                                   if (!file.hasFileExtension(".xml"))
                                       file = file.withFileExtension(".xml");
                                   const auto xml = safe->proc.exportPreset();
                                   const bool ok = xml->writeTo(file);
                                   AlertWindow::showMessageBoxAsync(ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon,
                                                                    ok ? "Preset exported" : "Could not export",
                                                                    ok ? "Saved to " + file.getFullPathName() + ". Give this file to another BitVibe player and they can load it with IMPORT."
                                                                       : "BitVibe could not write " + file.getFullPathName() + ".");
                               });
}

void BitVibeEditor::importPresetFile()
{
    const auto folder = BitVibeProcessor::presetFolder();
    folder.createDirectory();
    presetChooser = std::make_unique<FileChooser>("Import preset", folder, "*.xml");
    Component::SafePointer<BitVibeEditor> safe(this);
    presetChooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                               [safe](const FileChooser& chooser)
                               {
                                   if (safe == nullptr)
                                       return;
                                   const auto file = chooser.getResult();
                                   if (file == File())
                                       return;
                                   String message;
                                   bool ok = false;
                                   if (auto xml = XmlDocument::parse(file))
                                       ok = safe->proc.importPreset(*xml, message);
                                   else
                                       message = "This file could not be read as XML.";
                                   AlertWindow::showMessageBoxAsync(ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon,
                                                                    ok ? "Preset imported" : "Could not import", message);
                                   safe->refreshPresetName();
                                   safe->repaint();
                               });
}

void BitVibeEditor::setRightMode(bool advanced)
{
    advancedRight = advanced;
    uisettings::setAdvancedRight(advanced);
    applyRightMode();
}

// SIMPLE keeps: distortion type, on, drive and level; reverb type, reverse, decay and mix; the sound bank list, the
// sound chooser, sample mix and chance; the pattern's action, its ready-made patterns and its grid.
void BitVibeEditor::applyRightMode()
{
    const bool show = advancedRight && !drumsShown;
    for (Component* c : std::initializer_list<Component*> { &distToneSlider, &distReverbButton, &reverbToneSlider, &reverbLevelSlider,
                                                            &reverbLowSlider, &reverbHighSlider, &delayLowSlider, &delayHighSlider })
        c->setVisible(show);
    samplePanel->setAdvanced(advancedRight);
    eqPanel->setVisible(show); // the EQ is an ADVANCED control
    patternPanel->setAdvanced(advancedRight);
    simpleButton.lit = !advancedRight;
    advancedButton.lit = advancedRight;
    simpleButton.repaint();
    advancedButton.repaint();
    resized();
    repaint();
}

int BitVibeEditor::patternTop() const { return kColY; }
int BitVibeEditor::samplesTop() const { return patternTop() + PatternPanel::kHeight + kPanelGap; }
Rectangle<int> BitVibeEditor::distArea() const { return { kSideX, samplesTop() + samplePanel->currentHeight() + kPanelGap, 456, 100 }; }
Rectangle<int> BitVibeEditor::delayArea() const { return distArea().translated(0, 100 + kPanelGap); }
Rectangle<int> BitVibeEditor::reverbArea() const { return delayArea().translated(0, 100 + kPanelGap); }
int BitVibeEditor::eqTop() const { return reverbArea().getBottom() + kPanelGap; }
int BitVibeEditor::columnBottom() const { return advancedRight ? eqTop() + EqPanel::kHeight : reverbArea().getBottom(); }

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
        const auto type = stepType(i);
        const auto step = mapping::decodeStep(v, type);
        if (step.kind == mapping::Step::Kind::Skip)
            return "SKIP";
        if (step.kind == mapping::Step::Kind::Mute)
            return "MUTE";
        if (type == SeqType::Pitch)
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
            // how many repeats, and how fast they play (normal, twice as fast, half as fast)
            const String repeats = String(m.repeats) + (m.repeats == 1 ? " REPEAT" : " REPEATS");
            return m.speed > 1.5f ? repeats + ", FAST" : (m.speed < 0.75f ? repeats + ", SLOW" : repeats);
        }
        case 4:
        {
            static const char* names[] = { "OFF", "ONCE", "TWICE", "4 TIMES", "8 TIMES", "LOOP" };
            return names[jlimit(0, 5, roundToInt(rawValue(ids::seqPlay)))];
        }
        default: return "x" + String(mapping::seqMultiplier(roundToInt(rawValue(ids::seqMult)))) + " TEMPO";
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
    typePitchButton.setTooltip("SEQ TYPE: PITCH. The six step knobs set a pitch shift for each step, from -12 to +12 semitones. "
                              "Each type remembers its own step knobs: switch away and back and they are where you left them.");
    typeFilterButton.setTooltip("SEQ TYPE: FILTER. The six step knobs set the filter cutoff of each step, as a percent of the FILTER knob. "
                                "Each type remembers its own step knobs: switch away and back and they are where you left them.");
    typeRateButton.setTooltip("SEQ TYPE: SAMPLE RATE. The six step knobs set the sample rate of each step, as a percent of the SAMPLE knob. "
                              "Each type remembers its own step knobs: switch away and back and they are where you left them.");

    // global panel
    tempoSlider.setTooltip("TEMPO: the speed of the sequencer and stutter, 23.4 to 240 BPM. Turn the knob, click the big BPM number to type a tempo, tap, or turn on SYNC.");
    inSlider.setTooltip("INPUT: how hard your signal drives the effect. The clean signal used by MIX is not changed.");
    reverbDecaySlider.setTooltip("REVERB DECAY: how long the reverb rings. Left = short, middle = normal, right = long (up to four times as long). "
                                 "With REVERSE on it sets how long the swell takes.");
    makePowerButton(delayOnButton);
    delayOnButton.setTooltip("DELAY ON: echoes of the sequenced and stuttered sound, in time with the tempo. They ring on after the "
                             "sequence stops; your clean signal is never echoed. The light glows while it acts.");
    delayTimeBox.setTooltip("DELAY TIME: how far apart the echoes are, as a note length of the tempo (T = triplet, D = dotted). "
                            "It follows the tempo, and the host's tempo with SYNC on.");
    delayMixSlider.setTooltip("DELAY MIX: how loud the echoes are, from none (left) to as loud as the sound itself (right).");
    delayDecaySlider.setTooltip("DELAY DECAY: how much quieter each echo is than the one before. Left = one echo and done, right = they "
                                "hardly fade.");
    delayRepeatsSlider.setTooltip("DELAY REPEATS: how many echoes follow each sound, 1 to 16, or FOREVER (fully right) for echoes that "
                                  "keep coming, fading with DECAY.");
    delayLowSlider.setTooltip("DELAY LOW CUT: a high-pass filter on the echoes only. Turn right for thinner echoes. Fully left = off.");
    delayHighSlider.setTooltip("DELAY HIGH CUT: a low-pass filter on the echoes only. Turn left for darker echoes. Fully right = off.");
    makePowerButton(reverbOnButton);
    reverbOnButton.setTooltip("REVERB ON: switches the reverb on or off (it only ever hears the sequenced and stuttered sound). Off, there is no reverb and the tail stops; your settings stay. The light glows while it acts.");
    reverbReverseButton.setTooltip("REVERSE: tick this to play the reverb backwards: the sound swells up after the note instead of dying away. "
                                   "Every type of reverb remembers its own tick.");
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
    makePowerButton(distOnButton);
    distOnButton.setTooltip("DISTORTION ON: distorts the sequenced and stuttered sound (and the random sounds). "
                            "Your clean signal is never distorted.");
    distReverbButton.setTooltip("REVERB TOO: tick this to distort the reverb as well. Unticked, the reverb stays clean.");
    reverbLowSlider.setTooltip("LOW CUT: a high-pass filter on the reverb only. Turn right to remove the low end from the reverb. Fully left = off.");
    reverbHighSlider.setTooltip("HIGH CUT: a low-pass filter on the reverb only. Turn left to remove the highs from the reverb. Fully right = off.");
    reverbTypeBox.setTooltip("REVERB TYPE: Room is short and tight, Hall is big and slow, Spring is the splashy, dripping sound of a spring tank, "
                          "Plate is dense and bright, Shimmer climbs an octave as it fades, Gated is a big room chopped off short.");
    mixSlider.setTooltip("DRY/WET: blends your clean input (left) with the effect (right). Fully right = effect only.");
    outSlider.setTooltip("OUTPUT: the overall output level.");
    trigSlider.setTooltip("TRIGGER: how easily picked notes start the stutter and sequencer. Turn right if it misses notes, left if it fires too often.");
    syncButton.setTooltip("SYNC: follow the tempo of your DAW instead of the tempo knob.");
    holdButton.setTooltip("HOLD: freeze the stutter on the current (or next) note until you switch this off.");
    seqOnlyButton.setTooltip("SEQ ONLY: cut your clean signal and send out only the sequenced sound. Between sequences the plug-in is "
                             "silent (the reverb still rings out); DRY/WET has no effect while this is on.");
    fxTrigButton.setTooltip("FX ONLY ON TRIGGERS: when on, the sample-rate, bit and filter effects only act while the stutter or "
                            "sequencer is playing; the rest of the time your signal stays clean. A sequencer set to LOOP is always playing.");
    headroomBox.setTooltip("HEADROOM: Instrument for guitar level, Line for hotter signals such as synths.");
    loopButton.setTooltip("LOOP: keep part of the sequence going. The steps chosen with FROM and TO repeat (forever, or the number of "
                          "times chosen) before the sequence goes on; a restart still plays the steps before them first. Framed on the step strip.");
    loopFromBox.setTooltip("LOOP FROM: the first step of the loop section (set it and TO to the same step to hold one step).");
    loopToBox.setTooltip("LOOP TO: the last step of the loop section.");
    loopRepeatsBox.setTooltip("LOOP REPEATS: how many times the loop section plays in a row each time the sequence reaches it; forever keeps it "
                              "going until the sequence restarts.");
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

// The step knobs' descriptions follow what each step controls. tooltipSeqType remembers the types they were written
// for (one base-3 digit per step).
void BitVibeEditor::updateStepTooltips()
{
    int types = 0;
    for (int i = kKnobs - 1; i >= 0; --i)
    {
        const auto type = stepType(i);
        types = types * 3 + (int) type;
        const char* what = type == bitvibe::SeqType::Pitch
                               ? "its pitch shift, from -12 to +12 semitones (centre = unshifted)"
                               : (type == bitvibe::SeqType::Filter ? "its filter cutoff, as a percent of the FILTER knob"
                                                                   : "its sample rate, as a percent of the SAMPLE knob");
        knobs[(size_t) i].step.setTooltip("STEP " + String(i + 1) + ": " + what + ". Fully left skips this step, fully right mutes it.");
    }
    tooltipSeqType = types;
}

// A step's own type (0 = follow SEQ TYPE, 1..3 = pitch, filter, sample rate), from its P / F / S tags or the strip's menu.
void BitVibeEditor::setStepType(int step, int choice)
{
    if (auto* param = proc.apvts.getParameter(ids::stepType(step)))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1((float) jlimit(0, 3, choice)));
        param->endChangeGesture();
    }
}

// Dragging across the step strip makes those steps the loop section (and switches the loop on).
void BitVibeEditor::setLoopFromStrip(int first, int last)
{
    const std::array<std::pair<const char*, float>, 3> values { { { ids::loopOn, 1.0f }, { ids::loopFrom, (float) first }, { ids::loopTo, (float) last } } };
    for (const auto& [id, value] : values)
    {
        auto* param = proc.apvts.getParameter(id);
        if (!loopDragging)
            param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(value));
    }
    loopDragging = true;
}

// Right-click on a step of the strip: what the step controls, and the loop section.
void BitVibeEditor::showStepMenu(int step)
{
    const int own = jlimit(0, 3, roundToInt(rawValue(ids::stepType(step).toRawUTF8())));
    static const char* typeNames[3] { "PITCH", "FILTER", "SAMPLE RATE" };
    PopupMenu menu;
    menu.addSectionHeader("STEP " + String(step + 1));
    menu.addItem(1, "Follow SEQ TYPE (" + String(typeNames[(int) seqType()]) + ")", true, own == 0);
    menu.addItem(2, "Pitch", true, own == 1);
    menu.addItem(3, "Filter", true, own == 2);
    menu.addItem(4, "Sample rate", true, own == 3);
    menu.addSeparator();
    menu.addItem(10, "Show this step's knob");
    menu.addItem(11, "Loop this step only");
    const bool loopOn = rawValue(ids::loopOn) > 0.5f;
    menu.addItem(12, "Loop off", loopOn);
    Component::SafePointer<BitVibeEditor> safe(this);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&stepStrip), [safe, step](int result)
    {
        if (safe == nullptr || result == 0)
            return;
        if (result >= 1 && result <= 4)
            safe->setStepType(step, result - 1);
        else if (result == 10)
            safe->stepStrip.onStepClick(step);
        else if (result == 11)
        {
            safe->setLoopFromStrip(step, step);
            safe->stepStrip.onLoopDragEnd();
        }
        else if (result == 12)
            if (auto* param = safe->proc.apvts.getParameter(ids::loopOn))
                param->setValueNotifyingHost(0.0f);
    });
}

bitvibe::SeqType BitVibeEditor::stepType(int step) const
{
    const int own = roundToInt(rawValue(ids::stepType(step).toRawUTF8()));
    return own <= 0 ? seqType() : (bitvibe::SeqType) jlimit(0, 2, own - 1);
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
        const auto c = knobCentre(i).toInt();
        stepTypeTags[(size_t) i].setBounds(face({ c.x + 8, c.y - 70, 58, 16 }).translated(0, kTabsH)); // not in pedalComponents: shown with the Alt layer only
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
    triggerMeter.setBounds(face({ 450, 559, 9, 52 }));
    stepStrip.setBounds(face({ 186, 352, 140, 28 }));
    fxTrigButton.setBounds(face({ 24, 688, 200, 24 }));
    seqOnlyButton.setBounds(face({ 186, 480, 108, 24 })); // the middle of the pedal, under the tempo
    // the sequence's loop section, on a row of its own
    loopButton.setBounds(face({ 24, 738, 60, 24 }));
    loopFromBox.setBounds(face({ 92, 738, 56, 24 }));   // "TO" is painted between the two step boxes
    loopToBox.setBounds(face({ 176, 738, 56, 24 }));
    loopRepeatsBox.setBounds(face({ 240, 738, 110, 24 }));

    // reverb panel: the type on the caption row, then five knobs on the same grid as the global panel
    const auto dist = distArea(), delay = delayArea(), reverb = reverbArea();
    delayOnButton.setBounds(face({ kSideX + 8, delay.getY() + 3, 50, 19 }));
    delayTimeBox.setBounds(face({ kSideX + 344, delay.getY() + 3, 100, 20 }));
    reverbOnButton.setBounds(face({ kSideX + 8, reverb.getY() + 3, 50, 19 }));
    reverbTypeBox.setBounds(face({ kSideX + 344, reverb.getY() + 3, 100, 20 }));
    reverbReverseButton.setBounds(face({ kSideX + 250, reverb.getY() + 3, 88, 20 }));

    // distortion panel: the power button, the type and REVERB TOO on the caption row, three knobs below
    distOnButton.setBounds(face({ kSideX + 8, dist.getY() + 3, 50, 19 }));
    distTypeBox.setBounds(face({ kSideX + 206, dist.getY() + 3, 116, 20 }));
    distReverbButton.setBounds(face({ kSideX + 328, dist.getY() + 3, 120, 20 }));
    // the knobs that are showing (SIMPLE hides some) share the width of their panel evenly
    auto spread = [this](std::initializer_list<std::pair<Slider*, bool>> row, int size, int y)
    {
        int shown = 0;
        for (const auto& k : row)
            shown += (advancedRight || !k.second) ? 1 : 0;
        int index = 0;
        for (const auto& k : row)
            if (advancedRight || !k.second)
            {
                const int cx = kSideX + (int) std::lround(456.0 * ((double) index++ + 0.5) / (double) shown);
                k.first->setBounds(Rectangle<int>(size, size).withCentre({ cx, y }).translated(0, kFaceDy));
            }
    };
    spread({ { &distDriveSlider, false }, { &distToneSlider, true }, { &distLevelSlider, false } }, 48, dist.getY() + 48);
    spread({ { &reverbDecaySlider, false }, { &reverbToneSlider, true }, { &reverbMixSlider, false }, { &reverbLevelSlider, true },
             { &reverbLowSlider, true }, { &reverbHighSlider, true } },
           44, reverb.getY() + 46);
    spread({ { &delayMixSlider, false }, { &delayDecaySlider, false }, { &delayRepeatsSlider, false }, { &delayLowSlider, true }, { &delayHighSlider, true } },
           44, delay.getY() + 46);
    samplePanel->setBounds(face({ kSideX, samplesTop(), SamplePanel::kWidth, samplePanel->currentHeight() }));
    eqPanel->setBounds(face({ kSideX, eqTop(), EqPanel::kWidth, EqPanel::kHeight }));
    patternPanel->setBounds(face({ kSideX, patternTop(), PatternPanel::kWidth, PatternPanel::kHeight }));

    // Everything above was laid out as if the pedal started at the top: move it below the tab strip.
    for (auto* c : pedalComponents)
        c->setTopLeftPosition(c->getX(), c->getY() + kTabsH);

    helpButton.setBounds(kWidth - 140 - 76, 8, 70, 24);
    exportButton.setBounds(12, 8, 78, 24);
    importButton.setBounds(94, 8, 78, 24);
    simpleButton.setBounds(kSideX + 300, 8, 76, 24);
    advancedButton.setBounds(kSideX + 380, 8, 76, 24);
    helpOverlay.setBounds(0, 0, baseWidth, baseHeight);
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
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("CONTROLS ON THIS SIDE:", Rectangle<int>(kSideX + 130, 8, 164, 24), Justification::centredRight);
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
        // on the Alt layer a step is drawn in the colour of what it controls, its name moved left of its P / F / S tags
        const Colour ink = altLayer ? pedalstyle::stepTypeColour((int) stepType(i)) : kInk;

        g.setColour(ink);
        g.setFont(bold(12.5f));
        if (altLayer)
            g.drawText("STEP " + String(i + 1), Rectangle<int>((int) c.x - 66, (int) c.y - 70, 70, 16), Justification::centredRight);
        else
            g.drawText(knobs[(size_t) i].label, Rectangle<int>(112, 16).withCentre({ (int) c.x, (int) c.y - 62 }), Justification::centred);

        g.setColour(ink.withAlpha(morphOn ? 0.4f : 0.75f)); // with the morph on, the knob values are not what you hear
        g.setFont(plain(11.5f));
        g.drawText(readoutText(i), Rectangle<int>(120, 16).withCentre({ (int) c.x, (int) c.y + 56 }), Justification::centred);

        if (altLayer && currentStep == i)
        {
            g.setColour(ink.withAlpha(0.85f));
            g.drawEllipse(c.x - 49.0f, c.y - 49.0f, 98.0f, 98.0f, 2.0f);
        }
        if (altLayer && flashStep == i && Time::getMillisecondCounterHiRes() < flashStepUntil) // the step the strip pointed at
        {
            g.setColour(Colours::white.withAlpha(0.8f));
            g.drawEllipse(c.x - 53.0f, c.y - 53.0f, 106.0f, 106.0f, 2.5f);
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

    struct Entry
    {
        const char* name;
        String value;
        bool advancedOnly = false;
    };
    // where the n-th of the labels that are showing goes (the knobs above them are spread the same way)
    auto labelX = [this](const auto& list, size_t i)
    {
        int shown = 0, index = 0;
        for (size_t k = 0; k < list.size(); ++k)
            if (advancedRight || !list[k].advancedOnly)
            {
                if (k < i)
                    ++index;
                ++shown;
            }
        return kSideX + (int) std::lround(456.0 * ((double) index + 0.5) / (double) jmax(1, shown));
    };
    const std::array<Entry, 5> entries {{
        { "TEMPO", String(rawValue(ids::tempo), 1) },
        { "INPUT", String(rawValue(ids::inputGain), 1) + " dB" },
        { "DRY/WET", rawValue(ids::seqOnly) > 0.5f ? String("SEQ ONLY") : String(roundToInt(rawValue(ids::mix) * 100.0f)) + "% wet" },
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
    g.setColour(kInkDim);
    g.setFont(bold(9.5f));
    g.drawText("HEADROOM", Rectangle<int>(172, 641, 120, 11), Justification::left);
    g.drawText("STUTTER LENGTH", Rectangle<int>(306, 641, 144, 11), Justification::left);
    g.drawText("SEQUENCE LOOP", Rectangle<int>(26, 724, 200, 11), Justification::left);
    g.drawText("TO", Rectangle<int>(148, 738, 28, 24), Justification::centred);

    // The effect panels follow each other down the column in the order the sequenced sound goes through them:
    // an arrow in each gap.
    {
        std::vector<int> gaps { distArea().getY(), delayArea().getY(), reverbArea().getY() };
        if (advancedRight)
            gaps.push_back(eqTop());
        g.setColour(kAccent.withAlpha(0.7f));
        for (const int below : gaps)
        {
            const float cx = (float) kSideX + 228.0f, cy = (float) below - (float) kPanelGap * 0.5f;
            Path arrow;
            arrow.addTriangle(cx - 6.0f, cy - 3.0f, cx + 6.0f, cy - 3.0f, cx, cy + 3.5f);
            g.fillPath(arrow);
        }
    }

    // Under the right-hand column: what the numbers mean, or why those panels may be quiet.
    {
        const bool sequenceOff = roundToInt(rawValue(ids::seqPlay)) == 0;
        const bool stutterOff = bitvibe::mapping::stutterMode(roundToInt(rawValue(ids::stutter))).off;
        const Rectangle<int> note(kSideX + 8, columnBottom() + 10, 440, 40);
        g.setFont(plain(10.5f));
        if (advancedRight)
        {
            // ADVANCED fills the column: no room for the note
        }
        else if (sequenceOff && stutterOff)
        {
            g.setColour(kAccent);
            g.drawFittedText("The sequencer and the stutter are both off, so the effects on this side have nothing to act on. "
                             "Turn up the SEQUENCER or STUTTER knob on the pedal.", note, Justification::topLeft, 3);
        }
        else
        {
            g.setColour(kInkDim);
            g.drawFittedText("The numbered panels are in the order the sequenced sound goes through them. The light in an ON "
                             "button glows while the sequence or stutter plays, which is when that effect acts.", note, Justification::topLeft, 3);
        }
    }

    // Distortion and reverb panels (they move with the column, so they are drawn here rather than in the background)
    const auto dist = distArea(), delay = delayArea(), reverb = reverbArea();
    for (const auto& area : { dist, delay, reverb })
    {
        g.setColour(Colour(0xff17171a));
        g.fillRoundedRectangle(area.toFloat(), 10.0f);
        g.setColour(Colour(0xff2c2c31));
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 10.0f, 1.0f);
    }
    const float distAlpha = rawValue(ids::distOn) > 0.5f ? 1.0f : kOffAlpha;
    const float reverbAlpha = rawValue(ids::reverbOn) > 0.5f ? 1.0f : kOffAlpha;

    // Distortion panel labels
    drawPanelTitle(g, kSideX + kPanelTitleX, dist.getY() + 4, 2, "DISTORTION / FUZZ");
    const bool fuzz = roundToInt(rawValue(ids::distType)) == 1; // the big-fuzz style calls its knobs sustain / tone / volume
    const std::array<Entry, 3> distEntries {{
        { fuzz ? "SUSTAIN" : "DRIVE", String(roundToInt(rawValue(ids::distDrive) * 100.0f)) + "%" },
        { fuzz ? "FUZZ TONE" : "DIST TONE", String(roundToInt(rawValue(ids::distTone) * 100.0f)) + "%", true },
        { fuzz ? "VOLUME" : "DIST LEVEL", String(roundToInt(rawValue(ids::distLevel) * 100.0f)) + "%" },
    }};
    for (size_t i = 0; i < distEntries.size(); ++i)
    {
        if (!advancedRight && distEntries[i].advancedOnly)
            continue;
        const int cx = labelX(distEntries, i);
        g.setColour(kInk.withMultipliedAlpha(distAlpha));
        g.setFont(bold(11.0f));
        g.drawText(distEntries[i].name, Rectangle<int>(100, 14).withCentre({ cx, dist.getY() + 79 }), Justification::centred);
        g.setColour(kInkDim.withMultipliedAlpha(distAlpha));
        g.setFont(plain(10.5f));
        g.drawText(distEntries[i].value, Rectangle<int>(100, 13).withCentre({ cx, dist.getY() + 92 }), Justification::centred);
    }

    // Delay panel labels
    {
        const float delayAlpha = rawValue(ids::delayOn) > 0.5f ? 1.0f : kOffAlpha;
        drawPanelTitle(g, kSideX + kPanelTitleX, delay.getY() + 4, 3, "DELAY");
        g.setColour(kInkDim.withMultipliedAlpha(delayAlpha));
        g.setFont(bold(9.5f));
        g.drawText("TIME", Rectangle<int>(kSideX + 300, delay.getY() + 4, 40, 18), Justification::centredRight);
        const int repeats = roundToInt(rawValue(ids::delayRepeats));
        const float delayLow = rawValue(ids::delayLowCut), delayHigh = rawValue(ids::delayHighCut);
        const std::array<Entry, 5> delayEntries {{
            { "MIX", String(roundToInt(rawValue(ids::delayMix) * 100.0f)) + "%" },
            { "DECAY", String(roundToInt(rawValue(ids::delayDecay) * 100.0f)) + "%" },
            { "REPEATS", repeats >= bitvibe::kDelayForever ? String("FOREVER") : String(repeats) },
            { "LOW CUT", delayLow <= 20.5f ? String("OFF") : hzText(delayLow), true },
            { "HIGH CUT", delayHigh >= 19950.0f ? String("OFF") : hzText(delayHigh), true },
        }};
        for (size_t i = 0; i < delayEntries.size(); ++i)
        {
            if (!advancedRight && delayEntries[i].advancedOnly)
                continue;
            const int cx = labelX(delayEntries, i);
            g.setColour(kInk.withMultipliedAlpha(delayAlpha));
            g.setFont(bold(11.0f));
            g.drawText(delayEntries[i].name, Rectangle<int>(100, 14).withCentre({ cx, delay.getY() + 78 }), Justification::centred);
            g.setColour(kInkDim.withMultipliedAlpha(delayAlpha));
            g.setFont(plain(10.5f));
            g.drawText(delayEntries[i].value, Rectangle<int>(100, 13).withCentre({ cx, delay.getY() + 91 }), Justification::centred);
        }
    }

    // Reverb panel labels
    drawPanelTitle(g, kSideX + kPanelTitleX, reverb.getY() + 4, 4, "REVERB");
    const float lowCut = rawValue(ids::reverbLowCut), highCut = rawValue(ids::reverbHighCut);
    const std::array<Entry, 6> reverbEntries {{
        { "DECAY", String(roundToInt(rawValue(ids::reverbDecay) * 100.0f)) + "%" },
        { "TONE", String(roundToInt(rawValue(ids::reverbTone) * 100.0f)) + "%", true },
        { "MIX", String(roundToInt(rawValue(ids::reverbMix) * 100.0f)) + "%" },
        { "LEVEL", String(rawValue(ids::reverbLevel), 1) + " dB", true },
        { "LOW CUT", lowCut <= 20.5f ? String("OFF") : hzText(lowCut), true },
        { "HIGH CUT", highCut >= 19950.0f ? String("OFF") : hzText(highCut), true },
    }};
    for (size_t i = 0; i < reverbEntries.size(); ++i)
    {
        if (!advancedRight && reverbEntries[i].advancedOnly)
            continue;
        const int cx = labelX(reverbEntries, i);
        g.setColour(kInk.withMultipliedAlpha(reverbAlpha));
        g.setFont(bold(11.0f));
        g.drawText(reverbEntries[i].name, Rectangle<int>(100, 14).withCentre({ cx, reverb.getY() + 78 }), Justification::centred);
        g.setColour(kInkDim.withMultipliedAlpha(reverbAlpha));
        g.setFont(plain(10.5f));
        g.drawText(reverbEntries[i].value, Rectangle<int>(100, 13).withCentre({ cx, reverb.getY() + 91 }), Justification::centred);
    }
}

// What follows the parameters and the playing sequence: the step strip, the P / F / S tags, the step knobs' colours
// and descriptions, and the effect panels' power lights and dimming. Run by the timer, and straight away when the
// editor opens or the Alt layer changes, so nothing shows a stale state first.
void BitVibeEditor::updateLiveState()
{
    {
        std::array<StepStrip::Step, 6> steps {};
        for (int i = 0; i < 6; ++i)
        {
            const float knob = rawValue(ids::step(i).toRawUTF8());
            const auto type = stepType(i);
            const auto decoded = bitvibe::mapping::decodeStep(knob, type);
            steps[(size_t) i] = { knob, decoded.kind == bitvibe::mapping::Step::Kind::Skip ? 0 : (decoded.kind == bitvibe::mapping::Step::Kind::Mute ? 1 : 2),
                                  (int) type };
            stepTypeTags[(size_t) i].set(jlimit(0, 3, roundToInt(rawValue(ids::stepType(i).toRawUTF8()))), (int) type);
        }
        const bool loop = rawValue(ids::loopOn) > 0.5f;
        stepStrip.set(steps, proc.getEngine().getCurrentStep(), loop ? roundToInt(rawValue(ids::loopFrom)) : -1,
                      loop ? roundToInt(rawValue(ids::loopTo)) : -1);
    }
    {
        int types = 0;
        for (int i = kKnobs - 1; i >= 0; --i)
            types = types * 3 + (int) stepType(i);
        if (types != tooltipSeqType)
            updateStepTooltips();
    }
    for (int i = 0; i < kKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        const int ticks = stepType(i) == bitvibe::SeqType::Pitch ? 27 : 11;
        if ((int) k.step.getProperties().getWithDefault("ticks", 11) != ticks)
            k.step.getProperties().set("ticks", ticks);
        const int colour = (int) pedalstyle::stepTypeColour((int) stepType(i)).getARGB(); // the knob is drawn in its type's colour
        if ((int) k.step.getProperties().getWithDefault("inkColour", 0) != colour)
        {
            k.step.getProperties().set("inkColour", colour);
            k.step.repaint();
        }
    }
    {
        mixSlider.setAlpha(rawValue(ids::seqOnly) > 0.5f ? kOffAlpha : 1.0f); // SEQ ONLY makes DRY/WET do nothing
        // the power buttons glow while their effect acts; a switched-off panel's controls are dimmed
        const bool live = proc.getEngine().isEffectSideActive();
        setPowerActive(distOnButton, live);
        setPowerActive(reverbOnButton, live);
        setPowerActive(delayOnButton, live);
        const float delayAlpha = rawValue(ids::delayOn) > 0.5f ? 1.0f : kOffAlpha;
        for (Component* c : std::initializer_list<Component*> { &delayMixSlider, &delayDecaySlider, &delayRepeatsSlider, &delayLowSlider,
                                                                 &delayHighSlider, &delayTimeBox })
            c->setAlpha(delayAlpha);
        const float distAlpha = rawValue(ids::distOn) > 0.5f ? 1.0f : kOffAlpha;
        for (Component* c : std::initializer_list<Component*> { &distDriveSlider, &distToneSlider, &distLevelSlider, &distTypeBox, &distReverbButton })
            c->setAlpha(distAlpha);
        const float reverbAlpha = rawValue(ids::reverbOn) > 0.5f ? 1.0f : kOffAlpha;
        for (Component* c : std::initializer_list<Component*> { &reverbDecaySlider, &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider,
                                                                 &reverbLowSlider, &reverbHighSlider, &reverbTypeBox, &reverbReverseButton })
            c->setAlpha(reverbAlpha);
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
    {
        const int type = jlimit(0, 5, roundToInt(rawValue(ids::reverbType)));
        const std::array<const char*, 6> tickIds { ids::reverbReverseRoom, ids::reverbReverseHall, ids::reverbReverseSpring,
                                                   ids::reverbReversePlate, ids::reverbReverseShimmer, ids::reverbReverseGated };
        const bool ticked = rawValue(tickIds[(size_t) type]) > 0.5f;
        if (reverbReverseButton.getToggleState() != ticked)
            reverbReverseButton.setToggleState(ticked, dontSendNotification);
    }
    {
        // the input meter: falls back slowly so a pick can be read, and lights up when a pick is caught
        const float peak = proc.getEngine().takeInputPeak();
        const float db = peak > 1.0e-5f ? 20.0f * std::log10(peak) : -100.0f;
        meterDb = jmax(db, meterDb - 1.5f);
        const int attacks = proc.getEngine().getAttackCount();
        const double nowMs = Time::getMillisecondCounterHiRes();
        if (attacks != seenAttacks)
        {
            seenAttacks = attacks;
            meterFlashUntil = nowMs + 180.0;
        }
        triggerMeter.set(meterDb, bitvibe::AttackDetector::floorDb(rawValue(ids::sensitivity)), nowMs < meterFlashUntil);
    }
    updateLiveState();
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

    if (!drumsShown)
    {
        samplePanel->refresh();
        if (eqPanel->isVisible())
            eqPanel->refresh();
        patternPanel->refresh();
        repaint();
    }
}
