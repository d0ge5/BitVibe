#include "PluginEditor.h"

#include "DrumPage.h"
#include "PatternPanel.h"
#include "SamplePanel.h"
#include "SamplerPage.h"
#include "BeatDog.h"
#include "EqPanel.h"
#include "TremoloPanel.h"
#include "EffectGraphs.h"
#include "StepEditor.h"
#include "UiSettings.h"

#include <functional>

using namespace juce;
using pedalstyle::activeProperty;
using pedalstyle::drawPanelTitle;
using pedalstyle::kOffAlpha;
using pedalstyle::kOffTextAlpha;
using pedalstyle::offAlphaFor;
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
constexpr int kHeight = kTabsH + 976 + kFaceDy;
// The pedal page has a second column to the right of the pedal: reverb, sample blend and beat pattern panels.
constexpr int kSideX = kWidth + 12, kPedalWidth = kWidth + 468;
const Rectangle<int> kHeader { 12, 12, 456, 88 };
const Rectangle<int> kFace { 12, 12, 456, 520 };
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

// the theme's colours (they change with the theme, so these are references)
const Colour& kInk = pedalstyle::ink;
const Colour& kInkDim = pedalstyle::inkDim;
const Colour& kAccent = pedalstyle::accent;
const Colour& kRed = pedalstyle::red;
const pedalstyle::Theme& kTheme = pedalstyle::theme;

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
    applyTheme();
}

void PedalLookAndFeel::applyTheme()
{
    setColour(ComboBox::backgroundColourId, kTheme.popup);
    setColour(ComboBox::textColourId, kInk);
    setColour(ComboBox::outlineColourId, kTheme.outline);
    setColour(ComboBox::arrowColourId, kInkDim);
    setColour(PopupMenu::backgroundColourId, kTheme.popup);
    setColour(PopupMenu::textColourId, kInk);
    setColour(PopupMenu::highlightedBackgroundColourId, kTheme.selection);
    setColour(PopupMenu::highlightedTextColourId, kInk);
    setColour(TooltipWindow::backgroundColourId, kTheme.tooltip);
    setColour(BubbleComponent::backgroundColourId, kTheme.tooltip);
    setColour(BubbleComponent::outlineColourId, kTheme.tooltipEdge);
    setColour(TooltipWindow::textColourId, kInk);
    setColour(TooltipWindow::outlineColourId, kTheme.tooltipEdge);
    setColour(Label::textColourId, kInk);
    setColour(Label::textWhenEditingColourId, kInk);
    setColour(Label::backgroundWhenEditingColourId, kTheme.well);
    setColour(Label::outlineWhenEditingColourId, kAccent);
    setColour(TextEditor::backgroundColourId, kTheme.well);
    setColour(TextEditor::textColourId, kInk);
    setColour(TextEditor::outlineColourId, kTheme.panelEdge);
    setColour(TextEditor::highlightColourId, kTheme.selection);
    setColour(TextEditor::highlightedTextColourId, kInk);
    setColour(CaretComponent::caretColourId, kAccent);
    setColour(AlertWindow::backgroundColourId, kTheme.panel);
    setColour(AlertWindow::textColourId, kInk);
    setColour(AlertWindow::outlineColourId, kTheme.panelEdge);
    setColour(TextButton::buttonColourId, kTheme.buttonTop);
    setColour(TextButton::buttonOnColourId, kTheme.litTop);
    setColour(TextButton::textColourOffId, kInk);
    setColour(TextButton::textColourOnId, kInk);
}

// ------------------------------------------------------------------------------------------
// Themes

namespace
{
// A dark theme in another colour: every neutral of the first theme in the given hue and saturation, as bright as it was
// (times lift), so the page keeps its depth. The knobs take half the colour.
pedalstyle::Theme tinted(pedalstyle::Theme t, float hue, float saturation, float lift)
{
    for (auto* c : { &t.background, &t.faceTop, &t.faceBottom, &t.panel, &t.panelEdge, &t.well, &t.outline, &t.mid, &t.unlit,
                     &t.buttonTop, &t.buttonBottom, &t.buttonPressed, &t.popup, &t.tooltip, &t.tooltipEdge, &t.cell, &t.cellStrong,
                     &t.cellPlaying, &t.cellEdge, &t.gridLine, &t.gridMajor })
        *c = Colour::fromHSV(hue, saturation, jmin(1.0f, c->getBrightness() * lift), c->getFloatAlpha());
    for (auto* c : { &t.knobLight, &t.knobDark, &t.capTop, &t.capBottom })
        *c = Colour::fromHSV(hue, saturation * 0.5f, c->getBrightness(), 1.0f);
    return t;
}

// The accent, and the colours that go with it: a button that is on, the highlighted menu item.
void setAccent(pedalstyle::Theme& t, Colour accent)
{
    t.accent = accent;
    t.litTop = accent.withMultipliedBrightness(0.42f);
    t.litBottom = accent.withMultipliedBrightness(0.24f);
    t.selection = accent.withMultipliedBrightness(0.29f);
}

std::vector<pedalstyle::Theme> makeThemes()
{
    using pedalstyle::Theme;
    std::vector<Theme> all;
    all.push_back(Theme {}); // NIGHT: black powder coat, orange lights

    {
        // DAYLIGHT: a cream pedal with black knobs, for a bright room
        Theme t;
        t.name = "DAYLIGHT";
        t.ink = Colour(0xff1f1e1b);
        t.inkDim = Colour(0xff625f57);
        t.inkLabel = Colour(0xff3b3a35);
        t.accent = Colour(0xffc4410c);
        t.onAccent = Colours::white;
        t.red = Colour(0xffd92b20);
        t.background = Colour(0xffcdc9bf);
        t.faceTop = Colour(0xfff6f3ea);
        t.faceBottom = Colour(0xffd8d2c4);
        t.flake = Colour(0xff6b5f48);
        t.rim = Colour(0x66000000);
        t.panel = Colour(0xfff3f1eb);
        t.panelEdge = Colour(0xffbfbaae);
        t.well = Colour(0xffe2ded4);
        t.outline = Colour(0xffa8a396);
        t.mid = Colour(0xff86817a);
        t.unlit = Colour(0xffc4bfb3);
        t.buttonTop = Colour(0xfffdfcf8);
        t.buttonBottom = Colour(0xffe0dcd2);
        t.buttonPressed = Colour(0xffd3cec3);
        t.buttonEdge = Colour(0xff9c978b);
        t.litTop = Colour(0xffffd6b5);
        t.litBottom = Colour(0xfff4a56b);
        t.selection = Colour(0xffffd6b5);
        t.popup = Colour(0xfff8f6f1);
        t.tooltip = Colour(0xfffdfcf8);
        t.tooltipEdge = Colour(0xff9c978b);
        t.cell = Colour(0xffe0dcd2);
        t.cellStrong = Colour(0xffd2cdc1);
        t.cellPlaying = Colour(0xffb0aa9c);
        t.cellEdge = Colour(0xffbfbaae);
        t.gridLine = Colour(0xffd6d2c8);
        t.gridMajor = Colour(0xffaca698);
        t.screenEdge = Colour(0xff9c978b);
        t.steps = { Colour(0xff1a62c4), Colour(0xff0f7a3c), Colour(0xff8a4bd6) };
        all.push_back(t);
    }
    {
        // MIDNIGHT: navy, with cyan lights and a cyan dog
        Theme t = tinted({}, 0.61f, 0.42f, 1.15f);
        t.name = "MIDNIGHT";
        t.ink = t.pointer = Colour(0xffeef3ff);
        t.inkDim = Colour(0xff8f9cb8);
        t.inkLabel = Colour(0xffbdc7dd);
        setAccent(t, Colour(0xff3fd0ff));
        t.onAccent = Colour(0xff0a1020);
        t.screen = Colour(0xff3fd0ff);
        t.screenEdge = Colour(0xff16384a);
        t.steps = { Colour(0xffa591ff), Colour(0xff93e36e), Colour(0xffff80b8) };
        all.push_back(t);
    }
    {
        // SYNTHWAVE: deep purple, hot pink lights, a cyan dog
        Theme t = tinted({}, 0.76f, 0.55f, 1.2f);
        t.name = "SYNTHWAVE";
        t.ink = t.pointer = Colour(0xfffff0fb);
        t.inkDim = Colour(0xffb593bd);
        t.inkLabel = Colour(0xffe2c6e8);
        setAccent(t, Colour(0xffff3d9a));
        t.onAccent = Colour(0xff1a0a1f);
        t.faceTop = Colour(0xff3a2152);
        t.faceBottom = Colour(0xff120a24);
        t.screen = Colour(0xff35e0ff);
        t.screenEdge = Colour(0xff2a1f52);
        t.steps = { Colour(0xff35d4ff), Colour(0xffc6f04a), Colour(0xffffa94d) };
        all.push_back(t);
    }
    {
        // SURPLUS: olive drab, with amber lights and an amber monitor
        Theme t = tinted({}, 0.2f, 0.38f, 1.1f);
        t.name = "SURPLUS";
        t.ink = t.pointer = Colour(0xfff1ecd6);
        t.inkDim = Colour(0xffa7a186);
        t.inkLabel = Colour(0xffd0caaf);
        setAccent(t, Colour(0xffffb000));
        t.onAccent = Colour(0xff1a1708);
        t.screen = Colour(0xffffb000);
        t.screenEdge = Colour(0xff4a3a10);
        t.steps = { Colour(0xff72b8ff), Colour(0xff5fe0c0), Colour(0xffd89eff) };
        all.push_back(t);
    }
    {
        // CONTRAST: black and white, yellow lights, outlines you cannot miss
        Theme t;
        t.name = "CONTRAST";
        t.ink = t.pointer = Colours::white;
        t.inkDim = Colour(0xffd0d0d0);
        t.inkLabel = Colour(0xffededed);
        setAccent(t, Colour(0xffffe000));
        t.onAccent = Colours::black;
        t.red = Colour(0xffff4d4d);
        t.background = Colours::black;
        t.faceTop = Colour(0xff161616);
        t.faceBottom = Colours::black;
        t.rim = Colour(0xffbbbbbb);
        t.panel = Colours::black;
        t.panelEdge = Colour(0xffbbbbbb);
        t.well = Colours::black;
        t.outline = Colour(0xffa0a0a0);
        t.mid = Colour(0xffd0d0d0);
        t.unlit = Colour(0xff5a5a5a);
        t.buttonTop = Colour(0xff2a2a2a);
        t.buttonBottom = Colour(0xff141414);
        t.buttonPressed = Colour(0xff3a3a3a);
        t.buttonEdge = Colour(0xffbbbbbb);
        t.popup = Colours::black;
        t.tooltip = Colours::black;
        t.tooltipEdge = Colours::white;
        t.knobLight = Colour(0xff6a6a6a);
        t.knobDark = Colours::black;
        t.capTop = Colour(0xff222222);
        t.capBottom = Colours::black;
        t.cell = Colour(0xff1a1a1a);
        t.cellStrong = Colour(0xff2e2e2e);
        t.cellPlaying = Colour(0xff6a6a6a);
        t.cellEdge = Colour(0xff8a8a8a);
        t.gridLine = Colour(0xff333333);
        t.gridMajor = Colour(0xff8a8a8a);
        t.screen = Colour(0xffeeeeee);
        t.screenEdge = Colour(0xffbbbbbb);
        t.steps = { Colour(0xff6cc0ff), Colour(0xff5cff8a), Colour(0xffe2a8ff) };
        all.push_back(t);
    }
    return all;
}
} // namespace

const std::vector<pedalstyle::Theme>& pedalstyle::themes()
{
    static const std::vector<Theme> all = makeThemes();
    return all;
}

void pedalstyle::setTheme(int index)
{
    const int count = (int) themes().size();
    const int i = ((index % count) + count) % count;
    if (i == detail::shownIndex)
        return;
    detail::shown = themes()[(size_t) i];
    detail::shownIndex = i;
    ++detail::serial;
}

int pedalstyle::findTheme(const String& name)
{
    for (size_t i = 0; i < themes().size(); ++i)
        if (name == themes()[i].name)
            return (int) i;
    return 0;
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

    g.setGradientFill(ColourGradient(kTheme.knobLight, c.x - R * 0.55f, c.y - R * 0.8f, kTheme.knobDark,
                                     c.x + R * 0.6f, c.y + R * 0.9f, false));
    g.fillEllipse(c.x - R, c.y - R, R * 2.0f, R * 2.0f);
    g.setColour(Colours::black.withAlpha(0.85f));
    g.drawEllipse(c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.6f);

    const float Ri = R * 0.80f;
    g.setGradientFill(ColourGradient(kTheme.capTop, c.x, c.y - Ri, kTheme.capBottom, c.x, c.y + Ri, false));
    g.fillEllipse(c.x - Ri, c.y - Ri, Ri * 2.0f, Ri * 2.0f);
    g.setColour(Colours::white.withAlpha(0.07f));
    g.drawEllipse(c.x - Ri, c.y - Ri, Ri * 2.0f, Ri * 2.0f, 1.0f);

    // pointer: in the knob's own colour (a step's type, the Alt layer), else in the theme's pointer colour
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    Path pointer;
    pointer.addRoundedRectangle(-1.8f, -R * 0.86f, 3.6f, R * 0.5f, 1.6f);
    Colour pointerColour = props.contains("inkColour") || alt ? ink : kTheme.pointer;
    if (std::abs(pointerColour.getPerceivedBrightness() - kTheme.capTop.getPerceivedBrightness()) < 0.35f)
        pointerColour = pointerColour.interpolatedWith(kTheme.pointer, 0.5f); // a colour that would be lost on the knob
    g.setColour(enabled ? pointerColour : pointerColour.withAlpha(0.3f));
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
    g.setColour(kTheme.well);
    g.fillRoundedRectangle(groove, 3.0f);
    g.setColour(kTheme.outline);
    g.drawRoundedRectangle(groove, 3.0f, 1.0f);

    g.setColour(active ? kAccent : kTheme.mid);
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
    g.setColour(kTheme.onAccent);
    g.setFont(bold(9.5f));
    g.drawText(String(number), badge, Justification::centred);
    g.setColour(kInk);
    g.setFont(bold(12.5f));
    g.drawText(title, x + 18, y - 1, 300, 16, Justification::left);
}

void pedalstyle::drawSectionTitle(Graphics& g, int x, int y, const String& title)
{
    g.setColour(kInk);
    g.setFont(bold(12.5f));
    g.drawText(title, x, y - 1, 300, 16, Justification::left);
}

void pedalstyle::drawControlLabel(Graphics& g, int cx, int y, const String& name, const String& value, float alpha)
{
    alpha = jmax(alpha, kOffTextAlpha); // readable on a switched-off panel
    g.setColour(pedalstyle::inkLabel.withMultipliedAlpha(alpha));
    g.setFont(bold(10.5f));
    g.drawText(name, Rectangle<int>(100, 14).withCentre({ cx, y + 7 }), Justification::centred);
    g.setColour(kInkDim.withMultipliedAlpha(alpha));
    g.setFont(plain(10.5f));
    g.drawText(value, Rectangle<int>(100, 13).withCentre({ cx, y + 21 }), Justification::centred);
}

void PedalLookAndFeel::drawToggleButton(Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = b.getToggleState();
    if ((bool) b.getProperties().getWithDefault(powerProperty, false))
    {
        // a power button: a light and ON / OFF
        g.setColour(kTheme.well);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(highlighted ? kTheme.mid : kTheme.outline);
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
        const Point<float> c(r.getX() + 10.0f, r.getCentreY());
        if (on && (bool) b.getProperties().getWithDefault(activeProperty, false))
        {
            g.setColour(kAccent.withAlpha(0.3f));
            g.fillEllipse(c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
        }
        g.setColour(on ? kAccent : kTheme.unlit);
        g.fillEllipse(c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
        g.setColour(on ? kInk : kInkDim);
        g.setFont(bold(10.0f));
        g.drawText(on ? "ON" : "OFF", r.withTrimmedLeft(18.0f), Justification::centred);
        return;
    }
    // a switch: a light on the left, so it reads as on / off rather than as a button that does something once
    g.setGradientFill(ColourGradient(on ? kTheme.litTop : kTheme.buttonTop, r.getX(), r.getY(),
                                     on ? kTheme.litBottom : kTheme.buttonBottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(highlighted ? kTheme.mid : kTheme.buttonEdge);
    g.drawRoundedRectangle(r, 4.0f, 1.2f);
    pedalstyle::drawSwitchLight(g, { r.getX() + 9.0f, r.getCentreY() }, on);
    g.setColour(on ? kInk : kInkDim);
    g.setFont(bold(11.0f));
    g.drawText(b.getButtonText(), b.getLocalBounds().withTrimmedLeft(14), Justification::centred);
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

void StepStrip::set(const std::array<Step, bitvibe::kNumSteps>& steps, int newCount, int playing, int loopFrom, int loopTo)
{
    newCount = jlimit(1, bitvibe::kNumSteps, newCount);
    const int lo = loopFrom < 0 || loopTo < 0 ? -1 : jlimit(0, newCount - 1, jmin(loopFrom, loopTo));
    const int hi = loopFrom < 0 || loopTo < 0 ? -1 : jlimit(0, newCount - 1, jmax(loopFrom, loopTo));
    bool changed = playing != current || lo != loopLo || hi != loopHi || newCount != count;
    for (size_t i = 0; i < (size_t) newCount && !changed; ++i)
        changed = std::abs(steps[i].knob - shown[i].knob) > 0.002f || steps[i].kind != shown[i].kind || steps[i].type != shown[i].type;
    if (changed)
    {
        shown = steps;
        count = newCount;
        current = playing;
        loopLo = lo;
        loopHi = hi;
        repaint();
    }
}

int StepStrip::stepAt(float x) const
{
    return jlimit(0, count - 1, (int) std::floor(x / ((float) getWidth() / (float) count)));
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
    g.setColour(kTheme.well.withAlpha(0.8f));
    g.fillRoundedRectangle(b, 3.0f);
    const float slot = b.getWidth() / (float) count;
    const float inset = count > 8 ? 1.0f : 3.0f;
    if (loopLo >= 0) // the loop section: a frame around its steps
    {
        g.setColour(kAccent.withAlpha(0.14f));
        g.fillRoundedRectangle(Rectangle<float>(b.getX() + slot * (float) loopLo, b.getY(), slot * (float) (loopHi - loopLo + 1), b.getHeight()), 3.0f);
    }
    for (int i = 0; i < count; ++i)
    {
        const auto cell = Rectangle<float>(b.getX() + slot * (float) i, b.getY(), slot, b.getHeight()).reduced(inset, 4.0f);
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
            g.setColour(kInk.withAlpha(0.85f));
            g.drawRoundedRectangle(cell.expanded(jmin(1.5f, inset), 2.0f), 2.0f, 1.2f);
        }
    }
    g.setColour(kTheme.outline);
    g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 1.0f);
    if (loopLo >= 0)
    {
        g.setColour(kAccent.withAlpha(0.8f));
        g.drawRoundedRectangle(Rectangle<float>(b.getX() + slot * (float) loopLo, b.getY(), slot * (float) (loopHi - loopLo + 1), b.getHeight()).reduced(1.0f),
                               3.0f, 1.5f);
    }
}

void EffectTabs::set(int newChosen, int shownCount, const std::array<bool, kCount>& on, bool active)
{
    if (newChosen != chosen || shownCount != count || on != lit || active != glowing)
    {
        chosen = newChosen;
        count = jlimit(1, kCount, shownCount);
        lit = on;
        glowing = active;
        repaint();
    }
}

Rectangle<float> EffectTabs::tabBounds(int tab) const
{
    const float w = (float) getWidth() / (float) count;
    return Rectangle<float>(w * (float) tab, 0.0f, w, (float) getHeight()).reduced(2.0f, 0.0f);
}

int EffectTabs::tabAt(float x) const { return jlimit(0, count - 1, (int) std::floor(x / ((float) getWidth() / (float) count))); }

void EffectTabs::paint(Graphics& g)
{
    static const char* names[kCount] { "SAMPLES", "DIST", "TREMOLO", "DELAY", "REVERB", "EQ" };
    for (int i = 0; i < count; ++i)
    {
        const auto r = tabBounds(i);
        const bool isChosen = i == chosen;
        g.setColour(isChosen ? kTheme.litTop : kTheme.buttonBottom);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(isChosen ? kAccent : kTheme.buttonEdge);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, isChosen ? 1.2f : 1.0f);
        // the light: on when the effect is on, bright while it acts
        const auto light = Rectangle<float>(7.0f, 7.0f).withCentre({ r.getX() + 11.0f, r.getCentreY() });
        g.setColour(lit[(size_t) i] ? (glowing ? kAccent : kAccent.withAlpha(0.55f)) : kTheme.unlit);
        g.fillEllipse(light);
        g.setColour(isChosen ? kInk : pedalstyle::inkLabel);
        g.setFont(bold(10.5f));
        g.drawFittedText(String(i + 1) + "  " + names[i], r.withTrimmedLeft(19.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}

void EffectTabs::mouseUp(const MouseEvent& e)
{
    if (getLocalBounds().contains(e.getPosition()) && onPick)
        onPick(tabAt(e.position.x));
}

void HelpOverlay::showPage(int newPage)
{
    page = jlimit(kQuickStart, kTriggers, newPage);
    repaint();
}

Rectangle<int> HelpOverlay::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre(getWidth() - 120, jmin(getHeight() - 40, page == kTriggers ? 700 : 370));
}

Rectangle<int> HelpOverlay::tabBounds(int tab) const
{
    const auto card = cardBounds();
    const Rectangle<int> triggers(card.getRight() - 28 - 176, card.getY() + 22, 176, 24);
    return tab == kTriggers ? triggers : Rectangle<int>(triggers.getX() - 6 - 116, triggers.getY(), 116, 24);
}

void HelpOverlay::mouseUp(const MouseEvent& e)
{
    for (const int tab : { kQuickStart, kTriggers })
        if (tabBounds(tab).contains(e.getPosition()))
        {
            showPage(tab);
            return;
        }
    setVisible(false);
}

void HelpOverlay::paint(Graphics& g)
{
    g.fillAll(Colours::black.withAlpha(0.84f));
    const auto card = cardBounds().toFloat();
    g.setColour(kTheme.panel);
    g.fillRoundedRectangle(card, 12.0f);
    g.setColour(kAccent);
    g.drawRoundedRectangle(card.reduced(0.5f), 12.0f, 1.5f);

    auto area = card.reduced(28.0f, 22.0f).toNearestInt();
    g.setColour(kInk);
    g.setFont(bold(20.0f));
    g.drawText(page == kTriggers ? "WHAT TRIGGERS WHAT" : "QUICK START", area.removeFromTop(28), Justification::left);
    g.setColour(kInkDim);
    g.setFont(plain(11.0f));
    g.drawText("The tabs switch pages; click anywhere else to close. Open it again with the HELP button at the top.", area.removeFromTop(18),
               Justification::left);
    area.removeFromTop(10);

    // the tabs, drawn like the page's own buttons
    for (const int tab : { kQuickStart, kTriggers })
    {
        const auto r = tabBounds(tab).toFloat().reduced(1.0f);
        const bool lit = tab == page;
        g.setGradientFill(ColourGradient(lit ? kTheme.litTop : kTheme.buttonTop, r.getX(), r.getY(),
                                         lit ? kTheme.litBottom : kTheme.buttonBottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(kTheme.buttonEdge);
        g.drawRoundedRectangle(r, 4.0f, 1.2f);
        g.setColour(lit ? kInk : kInkDim);
        g.setFont(bold(11.0f));
        g.drawText(tab == kTriggers ? "WHAT TRIGGERS WHAT" : "QUICK START", r.toNearestInt(), Justification::centred);
    }

    if (page == kTriggers)
        paintTriggers(g, area);
    else
        paintQuickStart(g, area);
}

void HelpOverlay::paintQuickStart(Graphics& g, Rectangle<int> area)
{
    static const char* steps[] = {
        "1.  Play. A picked note starts the STUTTER and the SEQUENCER. TRIGGER sets how hard you must pick; the meter "
        "next to it shows your playing against that line and lights up when a pick is caught. Set HEADROOM first.",
        "2.  Turn up SEQUENCER (how many times the sequence plays) and STUTTER (how many repeats). SEQ TYPE picks what "
        "the steps change: pitch, filter or rate. STEPS shows the steps (up to 16) to drag, with their lanes; the coloured bars "
        "above TEMPO are the steps at a glance: drag across them to loop those steps.",
        "3.  SAMPLE RATE, FILTER and BITS crush and darken the sound. DRY/WET blends it with your clean signal and HEAR picks what "
        "comes out (guitar and effect, clean in between, or the effect only); OUTPUT sets the level. Drag the big tempo number to change the tempo.",
        "4.  On the right, the BEAT PATTERN can start the sequence on chosen steps of a bar. Below it, the numbered tabs are the effects on the "
        "sequenced sound only, in the order it goes through them: random sounds, distortion or fuzz, tremolo, delay, reverb, EQ. One panel shows at "
        "a time; a tab's light glows while its effect acts.",
        "5.  Hover over anything for a short description. Double-click a knob to reset it. SIMPLE / ADVANCED (top right) shows fewer or all "
        "controls. Drag the bottom-right corner to resize the window.",
    };
    g.setFont(plain(12.5f));
    for (const auto* text : steps)
    {
        g.setColour(kInk);
        g.drawFittedText(text, area.removeFromTop(42), Justification::topLeft, 3, 1.0f); // wrap, never squeeze
        area.removeFromTop(6);
    }
    area.removeFromTop(8);
    g.setColour(kAccent);
    g.setFont(bold(12.0f));
    g.drawText("WHAT TRIGGERS WHAT (the tab at the top) shows when each part acts and where the sound goes.", area.removeFromTop(18),
               Justification::left);
}

namespace
{
// A box of the flow chart, in the page's own panel style: an orange edge when it acts only while a stutter or sequence
// plays, a title (with the panel's number badge when it has one) and a few lines of plain text.
void chartBox(Graphics& g, Rectangle<float> r, const String& title, const String& body, bool lit, int badge = 0)
{
    g.setColour(kTheme.cell);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(lit ? kAccent : kTheme.outline);
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, lit ? 1.5f : 1.0f);
    auto inner = r.reduced(10.0f, 7.0f).toNearestInt();
    if (badge > 0)
        pedalstyle::drawPanelTitle(g, inner.getX(), inner.getY(), badge, title);
    else
        pedalstyle::drawSectionTitle(g, inner.getX(), inner.getY(), title);
    inner.removeFromTop(18);
    g.setColour(pedalstyle::inkLabel);
    g.setFont(plain(11.0f));
    g.drawFittedText(body, inner, Justification::topLeft, 8, 1.0f);
}

void chartArrow(Graphics& g, Point<float> from, Point<float> to, const String& label = {})
{
    g.setColour(kAccent.withAlpha(0.85f));
    g.drawArrow({ from, to }, 1.6f, 7.0f, 7.0f);
    if (label.isNotEmpty()) // above the arrow, ending at its head (a line of its own for each line of the label)
    {
        const int lines = StringArray::fromLines(label).size();
        const float h = 14.0f * (float) lines;
        g.setColour(kInkDim);
        g.setFont(plain(11.0f));
        g.drawFittedText(label, Rectangle<float>(to.x - 64.0f, jmin(from.y, to.y) - 2.0f - h, 62.0f, h).toNearestInt(),
                         Justification::bottomRight, lines);
    }
}
} // namespace

// The flow chart: the clock across the top; what starts things, what they start and what acts while they play, left
// to right; and where the sound goes along the bottom, with the effect panels' numbers.
void HelpOverlay::paintTriggers(Graphics& g, Rectangle<int> area)
{
    const float ax = (float) area.getX(), w = (float) area.getWidth();
    float y = (float) area.getY();

    chartBox(g, { ax, y, w, 58.0f }, "THE CLOCK: TEMPO",
             "The big number on the pedal (drag it, or double-click and type), TAP, or SYNC to follow your DAW. It sets the speed of the "
             "steps (tempo x SPEED), the stutter's slice (STUTTER LENGTH), the beat pattern's sixteenths and the delay's echoes.", false);
    y += 58.0f + 20.0f;

    const float c1 = ax, c2 = ax + w * 0.36f, c3 = ax + w * 0.72f, cw = w * 0.28f;
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("WHAT STARTS IT", Rectangle<float>(c1, y, cw, 14.0f).toNearestInt(), Justification::left);
    g.drawText("WHAT IT STARTS", Rectangle<float>(c2, y, cw, 14.0f).toNearestInt(), Justification::left);
    g.drawText("WHILE ONE PLAYS", Rectangle<float>(c3, y, cw, 14.0f).toNearestInt(), Justification::left);
    y += 20.0f;

    // what starts it
    const Rectangle<float> pick(c1, y, cw, 104.0f), pattern(c1, y + 116.0f, cw, 104.0f), continuous(c1, y + 232.0f, cw, 92.0f);
    chartBox(g, pick, "A PICKED NOTE",
             "Louder than the TRIGGER line (the meter lights up): restarts the sequence and fires the stutter. With PATTERN LOCK on it "
             "only fires the stutter.", false);
    chartBox(g, pattern, "THE BEAT PATTERN",
             "Each lit sixteenth of the bar does the pattern's ACTION: restart the sequence, move it one step, fire the stutter, or "
             "restart and stutter. Switched off: nothing.", false);
    chartBox(g, continuous, "CONTINUOUS",
             "With SEQUENCER on CONTINUOUS the sequence runs on its own; a restart sends it back to step 1.", false);

    // what it starts
    const Rectangle<float> stutter(c2, y, cw, 96.0f), sequencer(c2, y + 108.0f, cw, 124.0f), sounds(c2, y + 252.0f, cw, 72.0f);
    chartBox(g, stutter, "STUTTER",
             "Repeats a slice of what you just played: STUTTER sets 1 to 16 repeats at normal, fast or slow speed, or random; "
             "STUTTER TYPE plays it reversed, rising, tape-stopped, scratched or frozen. HOLD holds it.", true);
    chartBox(g, sequencer, "SEQUENCER",
             "Plays its steps (LENGTH, 1 to 16) from step 1, SEQUENCER times (once to 8 times, or continuously). Skipped steps are "
             "left out and the SEQUENCE LOOP repeats its steps. Each step, plus its lanes:", true);
    {
        // what a step can do, in the colours of the step strip
        static const char* chips[4] { "PITCH", "FILTER", "RATE", "MUTE" };
        const float chipW = (cw - 20.0f - 3.0f * 4.0f) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const Rectangle<float> chip(c2 + 10.0f + (float) i * (chipW + 4.0f), sequencer.getBottom() - 25.0f, chipW, 17.0f);
            const Colour colour = i < 3 ? pedalstyle::stepTypeColour(i) : kInkDim;
            g.setColour(colour.withAlpha(0.18f));
            g.fillRoundedRectangle(chip, 3.0f);
            g.setColour(colour);
            g.drawRoundedRectangle(chip.reduced(0.5f), 3.0f, 1.0f);
            g.setFont(bold(9.5f));
            g.drawText(chips[i], chip.toNearestInt(), Justification::centred);
        }
    }
    chartBox(g, sounds, "SAMPLE BLEND",
             "At each step that is not muted, CHANCE decides; then a random sound plays (up to MAX BEATS).", true, 1);

    // while one plays
    const Rectangle<float> acting(c3, y, cw, 232.0f), between(c3, y + 252.0f, cw, 72.0f);
    chartBox(g, acting, "THE EFFECTS ACT",
             "The ON lights glow, and:\n"
             "- RATE, BITS and FILTER crush the sound (always, or only now with HEAR on CLEAN IN BETWEEN)\n"
             "- 2 DISTORTION distorts it\n"
             "- 3 TREMOLO cuts it in and out, at a rate that can speed up or slow down\n"
             "- 4 DELAY echoes it, and the echoes ring on after\n"
             "- 5 REVERB adds reverb, and the tail rings on after\n"
             "- HEAR on EFFECT ONLY lets only this out\n"
             "- the random sounds stop when the sequence stops",
             true);
    chartBox(g, between, "IN BETWEEN",
             "Your clean signal, through DRY/WET, and the crush, unless HEAR says otherwise (clean in between, or the effect only).", false);

    // the arrows: every trigger reaches the stutter and the sequencer; both open the effects
    const float bus1 = c1 + cw + (c2 - c1 - cw) * 0.25f, bus2 = c2 + cw + (c3 - c2 - cw) * 0.45f;
    g.setColour(kAccent.withAlpha(0.85f));
    for (const auto& box : { pick, pattern, continuous })
        g.drawLine(box.getRight(), box.getCentreY(), bus1, box.getCentreY(), 1.6f);
    g.drawLine(bus1, stutter.getCentreY(), bus1, continuous.getCentreY(), 1.6f);
    chartArrow(g, { bus1, stutter.getCentreY() }, { c2 - 2.0f, stutter.getCentreY() }, "stutter");
    chartArrow(g, { bus1, sequencer.getCentreY() }, { c2 - 2.0f, sequencer.getCentreY() }, "restart,\nstep");
    chartArrow(g, { sequencer.getCentreX(), sequencer.getBottom() + 1.0f }, { sounds.getCentreX(), sounds.getY() - 2.0f });
    g.setColour(kAccent.withAlpha(0.85f));
    g.drawLine(stutter.getRight(), stutter.getCentreY(), bus2, stutter.getCentreY(), 1.6f);
    g.drawLine(sequencer.getRight(), sequencer.getCentreY(), bus2, sequencer.getCentreY(), 1.6f);
    g.drawLine(bus2, stutter.getCentreY(), bus2, sequencer.getCentreY(), 1.6f);
    chartArrow(g, { bus2, acting.getCentreY() }, { c3 - 2.0f, acting.getCentreY() });
    g.setColour(kInkDim);
    g.setFont(plain(10.0f));
    g.drawText("each step", Rectangle<float>(sequencer.getCentreX() + 6.0f, sequencer.getBottom() + 3.0f, 80.0f, 14.0f).toNearestInt(),
               Justification::left);
    y += 324.0f + 24.0f;

    // where the sound goes
    g.setColour(kInkDim);
    g.setFont(bold(10.0f));
    g.drawText("WHERE THE SOUND GOES", Rectangle<float>(ax, y, w, 14.0f).toNearestInt(), Justification::left);
    y += 20.0f;
    struct Stage
    {
        const char* name;
        int badge;
        bool onlyWhilePlaying;
    };
    static const Stage stages[] { { "INPUT", 0, false },  { "STUTTER\n+ SOUNDS", 1, true }, { "PITCH", 0, true },  { "RATE, BITS,\nFILTER", 0, false },
                                  { "DISTORTION", 2, true }, { "TREMOLO", 3, true },        { "DELAY", 4, true },  { "REVERB", 5, true },
                                  { "EQ", 6, false },      { "DRY/WET,\nOUTPUT", 0, false } };
    const int n = (int) std::size(stages);
    const float slot = w / (float) n, gap = 14.0f;
    for (int i = 0; i < n; ++i)
    {
        const Rectangle<float> box(ax + slot * (float) i, y, slot - gap, 48.0f);
        g.setColour(kTheme.cell);
        g.fillRoundedRectangle(box, 6.0f);
        g.setColour(stages[i].onlyWhilePlaying ? kAccent : kTheme.outline);
        g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, stages[i].onlyWhilePlaying ? 1.4f : 1.0f);
        auto text = box.reduced(3.0f, 3.0f);
        if (stages[i].badge > 0)
        {
            const auto badge = Rectangle<float>(13.0f, 13.0f).withCentre({ box.getCentreX(), box.getY() + 10.0f });
            g.setColour(kAccent.withAlpha(0.85f));
            g.fillEllipse(badge);
            g.setColour(kTheme.onAccent);
            g.setFont(bold(9.5f));
            g.drawText(String(stages[i].badge), badge, Justification::centred);
            text.removeFromTop(14.0f);
        }
        g.setColour(kInk);
        g.setFont(bold(9.5f));
        g.drawFittedText(stages[i].name, text.toNearestInt(), Justification::centred, 2, 0.8f);
        if (i + 1 < n)
            chartArrow(g, { box.getRight() + 1.0f, box.getCentreY() }, { box.getRight() + gap - 1.0f, box.getCentreY() });
    }
    y += 48.0f + 10.0f;
    {
        const Rectangle<float> key(ax, y + 3.0f, 14.0f, 9.0f);
        g.setColour(kAccent);
        g.drawRoundedRectangle(key, 2.0f, 1.4f);
        g.setColour(kInkDim);
        g.setFont(plain(10.5f));
        g.drawFittedText("acts only while a stutter or sequence plays (echoes and tails ring on). The reverb hears the sequence before the "
                         "distortion (REVERB TOO distorts it as well). Your clean signal never gets distortion, delay or reverb; BYPASS "
                         "sends it straight out.",
                         Rectangle<float>(ax + 20.0f, y, w - 20.0f, 30.0f).toNearestInt(), Justification::topLeft, 2, 1.0f);
    }
}

void LevelMeter::push(float peakDb, float thresholdDb, bool picked)
{
    newest = (newest + 1) % kColumns;
    history[(size_t) newest] = { peakDb, picked };
    threshold = thresholdDb;
    repaint();
}

int LevelMeter::countPicks() const
{
    return (int) std::count_if(history.begin(), history.end(), [](const Column& c) { return c.picked; });
}

void LevelMeter::paint(Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto bars = b.reduced(2.0f, 1.0f).withTrimmedTop(6.0f); // room on top for the pick marks
    auto toY = [&bars](float db) { return bars.getBottom() - bars.getHeight() * jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    g.setColour(kTheme.well);
    g.fillRoundedRectangle(b, 2.0f);
    const float width = bars.getWidth() / (float) kColumns;
    for (int age = 0; age < kColumns; ++age)
    {
        const auto& c = history[(size_t) ((newest - age + kColumns) % kColumns)];
        const float x = bars.getRight() - (float) (age + 1) * width;
        const float top = toY(c.db);
        if (top < bars.getBottom())
        {
            g.setColour(c.picked ? kAccent : (c.db >= threshold ? kAccent.withAlpha(0.45f) : kTheme.mid));
            g.fillRect(x, top, jmax(1.0f, width - 0.2f), bars.getBottom() - top);
        }
        if (c.picked)
        {
            Path mark; // a small arrow on top, so a pick stands out even when it is quiet
            mark.addTriangle(x + width * 0.5f - 3.0f, b.getY() + 1.0f, x + width * 0.5f + 3.0f, b.getY() + 1.0f, x + width * 0.5f, b.getY() + 6.0f);
            g.setColour(kAccent);
            g.fillPath(mark);
        }
    }
    g.setColour(kInk);
    g.fillRect(bars.getX(), toY(threshold) - 0.75f, bars.getWidth(), 1.5f);
    g.setColour(kTheme.outline);
    g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 1.0f);
}

void OutputMeter::set(float levelDb, bool overFullScale)
{
    const double now = Time::getMillisecondCounterHiRes();
    if (overFullScale)
        clipUntil = now + kClipHoldMs;
    const bool clip = now < clipUntil;
    if (std::abs(levelDb - level) > 0.2f || clip != shownClip)
    {
        level = levelDb;
        shownClip = clip;
        repaint();
    }
}

bool OutputMeter::isClipLit() const { return Time::getMillisecondCounterHiRes() < clipUntil; }

void OutputMeter::mouseUp(const MouseEvent&)
{
    clipUntil = 0.0;
    shownClip = false;
    repaint();
}

void OutputMeter::paint(Graphics& g)
{
    // the clip light, then the bar: -48 dB at the bottom, +6 dB at the top, with a line at 0 dB
    const auto b = getLocalBounds().toFloat();
    const Point<float> light(b.getCentreX(), 5.0f);
    if (shownClip)
    {
        g.setColour(kRed.withAlpha(0.3f));
        g.fillEllipse(light.x - 7.0f, light.y - 7.0f, 14.0f, 14.0f);
    }
    g.setColour(shownClip ? kRed : kTheme.unlit);
    g.fillEllipse(light.x - 4.0f, light.y - 4.0f, 8.0f, 8.0f);

    const auto bar = b.withTrimmedTop(14.0f);
    auto toY = [&bar](float db) { return bar.getBottom() - bar.getHeight() * jlimit(0.0f, 1.0f, (db + 48.0f) / 54.0f); };
    g.setColour(kTheme.well);
    g.fillRoundedRectangle(bar, 2.0f);
    const float top = toY(level);
    if (top < bar.getBottom())
    {
        g.setColour(level > 0.0f ? kRed : (level > -6.0f ? kAccent : kTheme.mid));
        g.fillRect(bar.withTop(top).reduced(1.0f, 0.0f));
    }
    g.setColour(kInk);
    g.fillRect(bar.getX(), toY(0.0f) - 0.75f, bar.getWidth(), 1.5f);
    g.setColour(kTheme.outline);
    g.drawRoundedRectangle(bar.reduced(0.5f), 2.0f, 1.0f);
}

void PedalButton::paint(Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced(1.0f);
    g.setGradientFill(ColourGradient(lit ? kTheme.litTop : (pressed ? kTheme.buttonPressed : kTheme.buttonTop), b.getX(), b.getY(),
                                     lit ? kTheme.litBottom : kTheme.buttonBottom, b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(kTheme.buttonEdge);
    g.drawRoundedRectangle(b, 4.0f, 1.2f);
    auto textArea = getLocalBounds();
    if (toggle)
    {
        pedalstyle::drawSwitchLight(g, { b.getX() + 9.0f, b.getCentreY() }, lit);
        textArea.removeFromLeft(14);
    }
    if (arrow)
    {
        const float ax = text.isEmpty() ? b.getCentreX() : b.getRight() - 10.0f, ay = b.getCentreY();
        Path p;
        p.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour(lit ? kInk : kInkDim);
        g.fillPath(p);
        if (!text.isEmpty())
            textArea.removeFromRight(16);
    }
    g.setColour(lit ? kInk : kInkDim);
    g.setFont(bold(11.0f));
    g.drawText(text, textArea, Justification::centred);
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
    pedalstyle::setTheme(pedalstyle::findTheme(uisettings::themeName()));
    laf.applyTheme();
    shownThemeSerial = pedalstyle::themeSerial();
    setLookAndFeel(&laf);

    static constexpr const char* frontIds[kKnobs] = { ids::sampleRate, ids::filter, ids::bits,
                                                      ids::stutter,    ids::seqPlay, ids::seqMult };
    static constexpr const char* labels[kKnobs] = { "SAMPLE RATE", "FILTER", "BITS", "STUTTER", "SEQUENCER", "SPEED" };
    const int ticks[kKnobs] = { 11, 11, 11, bitvibe::mapping::kStutterPositions, bitvibe::mapping::kSeqPlayPositions,
                                bitvibe::mapping::kSeqMultPositions };

    for (int i = 0; i < kKnobs; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.label = labels[i];
        configureKnob(k.front, ticks[i]);
        k.frontAttachment = std::make_unique<APVTS::SliderAttachment>(proc.apvts, frontIds[i], k.front);
        addKnobHelpers(k.front, frontIds[i]);
        content.addAndMakeVisible(k.front);
    }
    stepEditor = std::make_unique<StepEditor>(proc, laf);
    stepEditor->onStepMenu = [this](int step) { showStepMenu(step, stepEditor.get()); };
    content.addChildComponent(*stepEditor);

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
    triggerMeter.setTooltip("INPUT METER: the last few seconds of your playing (after INPUT), newest on the right. The white line is TRIGGER: "
                            "a pick that crosses it is caught, lit orange with a mark on top, and starts the stutter and sequencer.");
    content.addAndMakeVisible(triggerMeter);
    outputMeter.setTooltip("OUTPUT METER: how loud the sound coming out is. The white line is 0 dB, the most the output can take; the "
                           "light on top turns red when it goes over (click the light to put it out). If it does, turn OUTPUT down.");
    content.addAndMakeVisible(outputMeter);
    stepStrip.setTooltip("THE SEQUENCE: one bar per step, coloured by what it controls (blue = pitch, green = filter, purple = rate; "
                         "a dash = skipped, a cross = muted). Click a step to show it on ALT. Drag across steps to loop them. Right-click for more.");
    stepStrip.onStepClick = [this](int step)
    {
        setAltLayer(true);
        stepEditor->flash(step);
    };
    stepStrip.onLoopDrag = [this](int first, int last) { setLoopFromStrip(first, last); };
    stepStrip.onLoopDragEnd = [this]
    {
        if (loopDragging)
            for (const char* id : { ids::loopOn, ids::loopFrom, ids::loopTo })
                proc.apvts.getParameter(id)->endChangeGesture();
        loopDragging = false;
    };
    stepStrip.onStepMenu = [this](int step) { showStepMenu(step, &stepStrip); };
    content.addAndMakeVisible(stepStrip);
    effectTab = uisettings::effectTab();
    effectTabs.setTooltip("EFFECTS: the numbered tabs are the effects on the sequenced and stuttered sound, in the order it goes through "
                          "them; one panel shows at a time. A tab's light is on when its effect is on and glows while it acts.");
    effectTabs.onPick = [this](int tab) { setEffectTab(tab); };
    content.addAndMakeVisible(effectTabs);
    beatDog = std::make_unique<BeatDog>(laf);
    beatDog->beatSource = [this] {
        const auto& engine = proc.getEngine();
        return BeatDog::Position { engine.getBeatInBarPhase(), engine.getBeatInBar(), engine.getBarSteps() };
    };
    beatDog->scopeFeed = &proc.getEngine().getScope();
    beatDog->stepColour = [this](int step) { return pedalstyle::stepTypeColour((int) stepType(step)); };
    content.addAndMakeVisible(*beatDog);
    smallKnob(inSlider, inAtt, ids::inputGain);
    smallKnob(mixSlider, mixAtt, ids::mix);
    smallKnob(outSlider, outAtt, ids::outputGain);
    smallKnob(trigSlider, trigAtt, ids::sensitivity);
    smallKnob(filterResSlider, filterResAtt, ids::filterRes);
    smallKnob(reverbDecaySlider, reverbDecayAtt, ids::reverbDecay);
    smallKnob(reverbToneSlider, reverbToneAtt, ids::reverbTone);
    reverbReverseButton.setLookAndFeel(&laf);
    content.addAndMakeVisible(reverbReverseButton);
    reverbOnButton.setLookAndFeel(&laf);
    content.addAndMakeVisible(reverbOnButton);
    reverbOnAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::reverbOn, reverbOnButton);
    reverbOnButton.onClick = [this]
    {
        if (reverbOnButton.getToggleState())
            pedalstyle::raiseMixIfSilent(proc.apvts, ids::reverbMix, 0.3f);
    };
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
    smallKnob(delayWobbleSlider, delayWobbleAtt, ids::delayWobble);
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
    samplePanel->onOpenSampler = [this] { showSampler(true); };
    samplerPage = std::make_unique<SamplerPage>(proc, laf);
    samplerPage->onClose = [this] { showSampler(false); };
    content.addChildComponent(*samplerPage);
    content.addAndMakeVisible(*samplePanel);
    eqPanel = std::make_unique<EqPanel>(proc, laf);
    content.addAndMakeVisible(*eqPanel);
    tremoloPanel = std::make_unique<TremoloPanel>(proc, laf);
    content.addAndMakeVisible(*tremoloPanel);
    distGraph = std::make_unique<DistortionGraph>(proc);
    delayGraph = std::make_unique<DelayGraph>(proc);
    reverbGraph = std::make_unique<ReverbGraph>(proc);
    for (EffectGraph* graph : std::initializer_list<EffectGraph*> { distGraph.get(), delayGraph.get(), reverbGraph.get() })
    {
        graph->refresh();
        content.addAndMakeVisible(*graph);
    }
    patternPanel = std::make_unique<PatternPanel>(proc, laf);
    content.addAndMakeVisible(*patternPanel);

    for (auto* b : { &syncButton, &holdButton })
    {
        b->setLookAndFeel(&laf);
        content.addAndMakeVisible(*b);
    }
    syncAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::tempoSync, syncButton);
    holdAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::hold, holdButton);

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
    fillCombo(stutterTypeBox, ids::stutterType);
    stutterTypeAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::stutterType, stutterTypeBox);
    fillCombo(filterTypeBox, ids::filterType);
    filterTypeAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::filterType, filterTypeBox);
    hearBox.addItemList({ "Guitar + effect", "Clean in between", "Effect only" }, 1);
    hearBox.setLookAndFeel(&laf);
    hearBox.onChange = [this] { setHear(hearBox.getSelectedId() - 1); };
    content.addAndMakeVisible(hearBox);

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

    altButton.toggle = true;
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

    // preset bar: previous / next slot, the name (a click opens the preset menu) and SAVE
    // the tempo read-out in the middle of the pedal: drag it up or down, or double-click it and type a BPM (Enter to set,
    // Esc to cancel). It is the one tempo control on the page (with TAP and SYNC).
    tempoLabel.setLookAndFeel(&laf);
    tempoLabel.setFont(bold(24.0f));
    tempoLabel.setJustificationType(Justification::centred);
    tempoLabel.setEditable(false, true, false);
    tempoLabel.setMouseCursor(MouseCursor::UpDownResizeCursor);
    tempoLabel.onDragStart = [this]
    {
        if (!tempoEditable)
            return;
        dragStartBpm = rawValue(ids::tempo);
        tempoParam.beginGesture();
    };
    tempoLabel.onDrag = [this](float pixelsUp)
    {
        if (tempoEditable) // half a BPM a pixel, in tenths
            tempoParam.setValueAsPartOfGesture(jlimit(bitvibe::kMinTempoBpm, bitvibe::kMaxTempoKnobBpm,
                                                      std::round((dragStartBpm + pixelsUp * 0.5f) * 10.0f) / 10.0f));
    };
    tempoLabel.onDragEnd = [this]
    {
        if (tempoEditable)
            tempoParam.endGesture();
    };
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
    presetLabel.setEditable(false, false, false); // a click opens the preset menu, which can rename it
    presetLabel.setMouseCursor(MouseCursor::PointingHandCursor);
    presetLabel.onClick = [this] { showPresetMenu(); };
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
    presetMenuButton.arrow = true;
    presetMenuButton.onClick = [this] { showPresetMenu(); };
    for (auto* b : { &prevButton, &nextButton, &saveButton, &setUpButton, &setDownButton, &presetMenuButton })
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
    nameControls();

    pedalComponents = { &inSlider, &mixSlider, &outSlider, &trigSlider, &syncButton, &holdButton,
                        &reverbDecaySlider, &reverbOnButton, &reverbReverseButton, &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider, &reverbLowSlider, &reverbHighSlider, &reverbTypeBox, &delayMixSlider, &delayDecaySlider, &delayRepeatsSlider, &delayWobbleSlider, &delayLowSlider, &delayHighSlider, &delayTimeBox, &delayOnButton, &distDriveSlider, &distToneSlider, &distLevelSlider, &distOnButton, &distReverbButton, &distTypeBox, samplePanel.get(), eqPanel.get(), tremoloPanel.get(), distGraph.get(), delayGraph.get(), reverbGraph.get(), patternPanel.get(),
                        &headroomBox, &stutterLenBox, &stutterTypeBox, &filterTypeBox, &filterResSlider, &hearBox, &bypassSwitch, &tapSwitch, &triggerMeter, &outputMeter, beatDog.get(), &stepStrip, &altButton, &typePitchButton, &typeFilterButton, &typeRateButton, &presetLabel, &tempoLabel, stepEditor.get(), &effectTabs,
                        &prevButton, &nextButton, &saveButton, &setUpButton, &setDownButton, &expressionSlider, &morphButton, &presetMenuButton };
    for (auto& k : knobs)
        pedalComponents.push_back(&k.front);

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
    advancedRight = uisettings::advancedRight();
    beatDogShown = uisettings::beatDogShown();
    beatDog->setVisible(beatDogShown);
    samplePanel->setAdvanced(advancedRight); // its height sets where the column ends
    baseWidth = kPedalWidth;
    baseHeight = pedalPageHeight();
    savedScale = uisettings::loadWindowScale();
    scale = uisettings::fittingScale(savedScale, baseWidth, baseHeight, uisettings::screenArea()); // the whole window on screen
    constrainer.setFixedAspectRatio((double) baseWidth / (double) baseHeight);
    setConstrainer(&constrainer);
    setResizable(true, true);
    applySize();
    buildBackground();

    // the quick-start guide: on top of everything, opens by itself the first time
    helpButton.setTooltip("HELP: a quick start, and WHAT TRIGGERS WHAT: a chart of what sets each part off, what acts while it plays "
                          "and where the sound goes.");
    helpButton.onClick = [this]
    {
        helpOverlay.setVisible(true);
        helpOverlay.toFront(false);
    };
    content.addAndMakeVisible(helpButton);
    simpleButton.setTooltip("SIMPLE: shows only the controls you need to make a sound, and a shorter window. What it hides keeps its "
                            "settings, and the panels say when one of them is on.");
    advancedButton.setTooltip("ADVANCED: shows every control: tone, cuts, levels, sound banks, sample volumes, the EQ, the time signature, "
                              "the stutter length and hold, the sequence loop and more.");
    simpleButton.onClick = [this] { setRightMode(false); };
    advancedButton.onClick = [this] { setRightMode(true); };
    viewButton.setTooltip("VIEW: the theme (the colours of the page), the window size, and whether BEAT DOG is shown. "
                          "All remembered; every open BitVibe window follows the theme.");
    viewButton.arrow = true;
    viewButton.onClick = [this]
    {
        Component::SafePointer<BitVibeEditor> safe(this);
        viewMenu().showMenuAsync(PopupMenu::Options().withTargetComponent(&viewButton).withMinimumWidth(180), [safe](int result)
        {
            if (safe != nullptr)
                safe->viewMenuChosen(result);
        });
    };
    content.addAndMakeVisible(viewButton);
    content.addAndMakeVisible(simpleButton);
    content.addAndMakeVisible(advancedButton);
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
        k.front.setLookAndFeel(nullptr);
    for (auto* s : { &inSlider, &mixSlider, &outSlider, &trigSlider, &reverbDecaySlider, &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider, &reverbLowSlider, &reverbHighSlider, &distDriveSlider, &distToneSlider, &distLevelSlider,
                      &delayMixSlider, &delayDecaySlider, &delayRepeatsSlider, &delayWobbleSlider, &delayLowSlider, &delayHighSlider })
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
    hearBox.setLookAndFeel(nullptr);
    morphButton.setLookAndFeel(nullptr);
    expressionSlider.setLookAndFeel(nullptr);
    presetLabel.setLookAndFeel(nullptr);
    tempoLabel.setLookAndFeel(nullptr);
    headroomBox.setLookAndFeel(nullptr);
    stutterLenBox.setLookAndFeel(nullptr);
    stutterTypeBox.setLookAndFeel(nullptr);
    filterTypeBox.setLookAndFeel(nullptr);
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

void BitVibeEditor::showSampler(bool show)
{
    samplerPage->setBounds(0, 0, kPedalWidth, baseHeight);
    samplerPage->setVisible(show);
    if (show)
        samplerPage->toFront(false);
}

void BitVibeEditor::setAltLayer(bool on)
{
    altLayer = on;
    for (auto& k : knobs)
        k.front.setVisible(!on);
    stepEditor->setVisible(on);
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
    baseHeight = on ? kHeight : pedalPageHeight();
    constrainer.setFixedAspectRatio((double) baseWidth / (double) baseHeight);
    applySize();
    pedalTab.lit = !on;
    drumsTab.lit = on;
    pedalTab.repaint();
    drumsTab.repaint();
    repaint();
}

// The arrow in the preset name box: what can be done with the preset that is showing.
// The preset menu: every slot to jump to, then undo and redo, save and rename, export and import.
PopupMenu BitVibeEditor::presetMenu()
{
    PopupMenu menu;
    const int current = proc.getCurrentProgram();
    menu.addSectionHeader("PRESETS");
    for (int i = 0; i < proc.getNumPrograms(); ++i)
    {
        if (i == bitvibe::kNumFactoryPresets)
            menu.addSeparator(); // the factory sounds, then the blank slots
        menu.addItem(kMenuSlot + i, String(i + 1).paddedLeft('0', 2) + "   " + proc.getProgramName(i), true, i == current);
    }
    menu.addSeparator();
    auto& undo = proc.getUndo();
    menu.addItem(PopupMenu::Item("Undo").setID(kMenuUndo).setEnabled(undo.canUndo()));
    menu.addItem(PopupMenu::Item("Redo").setID(kMenuRedo).setEnabled(undo.canRedo()));
    menu.addSeparator();
    menu.addItem(kMenuSave, "Save to slot " + String(current + 1).paddedLeft('0', 2));
    menu.addItem(kMenuRename, "Rename...");
    menu.addSeparator();
    menu.addItem(kMenuExport, "Export to a file...");
    menu.addItem(kMenuImport, "Import from a file...");
    if (const int packSize = BitVibeProcessor::numPackPresets(); packSize > 0)
    {
        PopupMenu pack;
        pack.addSectionHeader("LOADS INTO SLOT " + String(current + 1).paddedLeft('0', 2));
        for (int i = 0; i < packSize; ++i)
            pack.addItem(kMenuPack + i, BitVibeProcessor::packPresetName(i));
        menu.addSubMenu("Preset pack", pack);
    }
    return menu;
}

void BitVibeEditor::presetMenuChosen(int result)
{
    if (result >= kMenuSlot && result < kMenuSlot + proc.getNumPrograms())
    {
        if (result - kMenuSlot != proc.getCurrentProgram())
        {
            proc.getUndo().remember();
            proc.setCurrentProgram(result - kMenuSlot);
            refreshPresetName();
        }
    }
    else if (result == kMenuUndo)
        undoOrRedo(true);
    else if (result == kMenuRedo)
        undoOrRedo(false);
    else if (result == kMenuSave)
        saveButton.onClick();
    else if (result == kMenuRename)
        presetLabel.showEditor();
    else if (result == kMenuExport)
        exportPresetFile();
    else if (result == kMenuImport)
        importPresetFile();
    else if (result >= kMenuPack && result < kMenuPack + BitVibeProcessor::numPackPresets())
    {
        proc.getUndo().remember();
        String message;
        if (!proc.loadPackPreset(result - kMenuPack, message))
            AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Could not load the preset", message);
        refreshPresetName();
        repaint();
    }
}

void BitVibeEditor::showPresetMenu()
{
    Component::SafePointer<BitVibeEditor> safe(this);
    presetMenu().showMenuAsync(PopupMenu::Options().withTargetComponent(&presetLabel).withMinimumWidth(260), [safe](int result)
    {
        if (safe != nullptr)
            safe->presetMenuChosen(result);
    });
}

void BitVibeEditor::undoOrRedo(bool undo)
{
    auto& history = proc.getUndo();
    if (undo)
        history.undo();
    else
        history.redo();
    refreshPresetName();
    repaint();
}

bool BitVibeEditor::keyPressed(const KeyPress& key)
{
    if (key == KeyPress('z', ModifierKeys::commandModifier, 0))
    {
        undoOrRedo(true);
        return true;
    }
    if (key == KeyPress('z', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0) || key == KeyPress('y', ModifierKeys::commandModifier, 0))
    {
        undoOrRedo(false);
        return true;
    }
    return false; // the host's own keys (play, stop) go on to the host
}

void BitVibeEditor::mouseDown(const MouseEvent&)
{
    // a click anywhere on the page lets it hear Ctrl+Z (unless a control that takes keys, like a text box, has them)
    if (isShowing() && !hasKeyboardFocus(true))
        grabKeyboardFocus();
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
                                                                    ok ? "Saved to " + file.getFullPathName() + ". Give this file to another BitVibe player: they load it with Import "
                                                                            "from a file, in the menu by the preset name."
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
                                   safe->proc.getUndo().remember();
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

PopupMenu BitVibeEditor::viewMenu()
{
    PopupMenu menu;
    menu.addSectionHeader("THEME");
    const auto& all = pedalstyle::themes();
    for (int i = 0; i < (int) all.size(); ++i)
        menu.addItem(kViewTheme + i, all[(size_t) i].name, true, i == pedalstyle::themeIndex());
    menu.addSectionHeader("WINDOW SIZE");
    for (const int percent : { 75, 100, 125, 150 })
    {
        const float wanted = (float) percent / 100.0f;
        const bool fits = uisettings::fittingScale(wanted, baseWidth, baseHeight, uisettings::screenArea()) >= wanted - 0.005f;
        menu.addItem(kViewSize + percent, String(percent) + "%" + (fits ? String() : String("  (bigger than the screen)")), fits,
                     std::abs(scale - wanted) < 0.005f);
    }
    menu.addSeparator();
    menu.addItem(kViewDog, "Show BEAT DOG", true, beatDogShown);
    return menu;
}

void BitVibeEditor::viewMenuChosen(int result)
{
    if (result >= kViewTheme && result < kViewTheme + (int) pedalstyle::themes().size())
        setThemeFromPage(result - kViewTheme);
    else if (result > kViewSize && result < kViewDog)
        setWindowScale((float) (result - kViewSize) / 100.0f);
    else if (result == kViewDog)
        setBeatDogShown(!beatDogShown);
}

void BitVibeEditor::setThemeFromPage(int index)
{
    pedalstyle::setTheme(index);
    uisettings::setThemeName(pedalstyle::theme.name);
    applyTheme();
}

void BitVibeEditor::setWindowScale(float newScale)
{
    scale = savedScale = jlimit(uisettings::kMinScale, uisettings::kMaxScale, newScale);
    uisettings::saveWindowScale(scale);
    applySize();
}

void BitVibeEditor::setBeatDogShown(bool shown)
{
    beatDogShown = shown;
    uisettings::setBeatDogShown(shown);
    applyRightMode(); // the layout and the window's height change with it
}

void BitVibeEditor::applyTheme()
{
    shownThemeSerial = pedalstyle::themeSerial();
    laf.applyTheme();
    buildBackground();
    sendLookAndFeelChange(); // every control takes the new colours and repaints
}

void BitVibeEditor::setRightMode(bool advanced)
{
    advancedRight = advanced;
    uisettings::setAdvancedRight(advanced);
    applyRightMode();
}

// SIMPLE keeps: distortion type, on, drive and level; reverb type, reverse, decay and mix; the sound bank list, the
// sound chooser, sample volume and chance; the pattern's action, its ready-made patterns and its grid; under the pedal,
// input, trigger, headroom, dry/wet, output and HEAR. It hides the expression morph (the header gets shorter), the
// stutter's length, type and hold, and the EQ.
void BitVibeEditor::applyRightMode()
{
    const bool show = advancedRight && !drumsShown;
    for (Component* c : std::initializer_list<Component*> { &holdButton, &stutterLenBox, &stutterTypeBox, &filterTypeBox, &filterResSlider,
                                                            &setUpButton, &expressionSlider, &setDownButton, &morphButton })
        c->setVisible(show);
    if (!advancedRight && effectTab == kTabEq)
        effectTab = kTabSamples; // the EQ is an ADVANCED control
    applyEffectTab();
    if (backgroundScale > 0.0f)
        buildBackground(); // the header is shorter without the morph row
    samplePanel->setAdvanced(advancedRight);
    patternPanel->setAdvanced(advancedRight);
    patternPanel->onHeightChange = [this]
    {
        resized();
        repaint();
    };
    simpleButton.lit = !advancedRight;
    advancedButton.lit = advancedRight;
    simpleButton.repaint();
    advancedButton.repaint();
    beatDog->setVisible(beatDogShown && !drumsShown);
    if (!drumsShown && pedalPageHeight() != baseHeight) // SIMPLE has a shorter window, and so has a page without BEAT DOG
    {
        baseHeight = pedalPageHeight();
        constrainer.setFixedAspectRatio((double) baseWidth / (double) baseHeight);
        applySize();
        if (samplerPage->isVisible())
            showSampler(true);
    }
    resized();
    repaint();
}

int BitVibeEditor::patternTop() const { return kColY; }
int BitVibeEditor::samplesTop() const { return patternTop() + patternPanel->currentHeight() + kPanelGap; }
Rectangle<int> BitVibeEditor::effectsArea() const { return { kSideX, samplesTop() + EffectTabs::kHeight + 6, 456, kEffectsH }; }
Rectangle<int> BitVibeEditor::distArea() const { return effectsArea(); }
Rectangle<int> BitVibeEditor::tremoloArea() const { return effectsArea(); }
Rectangle<int> BitVibeEditor::delayArea() const { return effectsArea(); }
Rectangle<int> BitVibeEditor::reverbArea() const { return effectsArea(); }
int BitVibeEditor::eqTop() const { return effectsArea().getY(); }
int BitVibeEditor::columnBottom() const { return globalTop() + globalHeight(); }
int BitVibeEditor::globalTop() const { return (beatDogShown ? beatDogArea().getBottom() : effectsArea().getBottom()) + kPanelGap; }
int BitVibeEditor::globalHeight() const { return advancedRight ? 218 : 162; }
Rectangle<int> BitVibeEditor::beatDogArea() const { return { kSideX, effectsArea().getBottom() + kPanelGap, 456, 164 }; }
// Tall enough for the longer column (the right-hand one, as a rule), and for the note that can show under it.
int BitVibeEditor::pedalPageHeight() const
{
    return kTabsH + kFaceDy + jmax(kFace.getBottom(), columnBottom() + 46) + 12;
}

void BitVibeEditor::setEffectTab(int tab)
{
    effectTab = jlimit(0, EffectTabs::kCount - 1, tab);
    if (!advancedRight && effectTab == kTabEq)
        effectTab = kTabSamples;
    uisettings::setEffectTab(effectTab);
    applyEffectTab();
    updateLiveState();
    repaint();
}

void BitVibeEditor::applyEffectTab()
{
    const bool page = !drumsShown;
    auto show = [&](Component& c, int tab, bool advancedOnly = false) { c.setVisible(page && effectTab == tab && (advancedRight || !advancedOnly)); };
    show(*samplePanel, kTabSamples);
    for (Component* c : std::initializer_list<Component*> { &distOnButton, &distTypeBox, &distDriveSlider, &distLevelSlider, distGraph.get() })
        show(*c, kTabDist);
    for (Component* c : std::initializer_list<Component*> { &distToneSlider, &distReverbButton })
        show(*c, kTabDist, true);
    show(*tremoloPanel, kTabTremolo);
    for (Component* c : std::initializer_list<Component*> { &delayOnButton, &delayTimeBox, &delayMixSlider, &delayDecaySlider, &delayRepeatsSlider,
                                                            &delayWobbleSlider, delayGraph.get() })
        show(*c, kTabDelay);
    for (Component* c : std::initializer_list<Component*> { &delayLowSlider, &delayHighSlider })
        show(*c, kTabDelay, true);
    for (Component* c : std::initializer_list<Component*> { &reverbOnButton, &reverbReverseButton, &reverbTypeBox, &reverbDecaySlider, &reverbMixSlider,
                                                            reverbGraph.get() })
        show(*c, kTabReverb);
    for (Component* c : std::initializer_list<Component*> { &reverbToneSlider, &reverbLevelSlider, &reverbLowSlider, &reverbHighSlider })
        show(*c, kTabReverb, true);
    show(*eqPanel, kTabEq, true);
    effectTabs.setVisible(page);
}

void BitVibeEditor::stepProgram(int direction)
{
    proc.getUndo().remember();
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

    switch (i)
    {
        case 0: return hzText(mapping::sampleRateHz(rawValue(ids::sampleRate)));
        case 1: return rawValue(ids::filter) >= 0.99f ? String("OPEN")
                                                      : hzText(mapping::filterHzFor((FilterType) roundToInt(rawValue(ids::filterType)), rawValue(ids::filter)));
        case 2: return String(mapping::bitDepth(rawValue(ids::bits)), 1) + " bit";
        case 3:
        {
            const auto m = mapping::stutterMode(roundToInt(rawValue(ids::stutter)));
            if (m.off)
                return "OFF";
            if (m.random)
                return "RANDOM";
            // how many repeats, and how fast they play (normal, twice as fast, half as fast); another STUTTER TYPE goes first
            const int type = jlimit(0, kNumStutterKinds - 1, roundToInt(rawValue(ids::stutterType)));
            const String speed = m.speed > 1.5f ? ", FAST" : (m.speed < 0.75f ? ", SLOW" : "");
            if (type != 0)
            {
                static const char* typeNames[kNumStutterKinds] { "REPEAT", "REVERSE", "RISE", "TAPE STOP", "SCRATCH", "FREEZE" };
                return String(typeNames[type]) + " x" + String(m.repeats) + speed;
            }
            return String(m.repeats) + (m.repeats == 1 ? " REPEAT" : " REPEATS") + speed;
        }
        case 4:
        {
            static const char* names[] = { "OFF", "ONCE", "TWICE", "4 TIMES", "8 TIMES", "CONTINUOUS" };
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
        "SAMPLE RATE: lowers the sample rate for a gritty, aliased sound. Turn left for more. Fully right = off.",
        "FILTER: the filter's cutoff. Turn left to close it (darker; thinner for a high-pass). Fully right = off. ADVANCED has "
        "its type and RESONANCE.",
        "BITS: lowers the bit depth for digital distortion. Turn left for more. Fully right = off.",
        "STUTTER: after each picked note, repeats a short slice of it. Left = off, then 1 to 16 repeats at normal, "
        "double or half speed. Fully right = random.",
        "SEQUENCER: how many times the sequence (its LENGTH is on STEPS) plays after each picked note: off, once, twice, 4 or 8 "
        "times, or continuously.",
        "SPEED: how fast the sequencer runs, as a multiple of the tempo (x1 to x128)."
    };
    for (int i = 0; i < kKnobs; ++i)
        knobs[(size_t) i].front.setTooltip(frontTips[i]);

    // footswitches and buttons
    bypassSwitch.setTooltip("BYPASS: switches the effect on or off. The red light shows it is on.");
    tapSwitch.setTooltip("TAP: tap to set the tempo. Press and hold to freeze the stutter.");
    altButton.setTooltip("STEPS: swap the six knobs for the step editor: the steps of the sequence (up to 16), their lanes, LENGTH and "
                         "the loop. The light is on while it shows.");
    typePitchButton.setTooltip("SEQ TYPE: PITCH. The steps (on STEPS) set a pitch shift for each step, from -12 to +12 semitones. "
                              "Each type remembers its own steps: switch away and back and they are where you left them.");
    typeFilterButton.setTooltip("SEQ TYPE: FILTER. The steps (on STEPS) set the filter cutoff of each step, as a percent of the FILTER knob. "
                                "Each type remembers its own steps: switch away and back and they are where you left them.");
    typeRateButton.setTooltip("SEQ TYPE: RATE. The steps (on STEPS) set the sample rate of each step, as a percent of the SAMPLE RATE knob. "
                              "Each type remembers its own steps: switch away and back and they are where you left them.");

    // global panel
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
    delayWobbleSlider.setTooltip("DELAY WOBBLE: makes it a worn tape echo. The echoes waver in pitch (slow wow, fast flutter), each later "
                                 "one more, and get darker and softer. Fully left = a clean digital delay.");
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
    syncButton.setTooltip("SYNC: follow the tempo of your DAW instead of the tempo set here.");
    hearBox.setTooltip("HEAR: what comes out. GUITAR + EFFECT: both, blended with DRY/WET. CLEAN IN BETWEEN: the crush and filter act only "
                       "while a stutter or sequence plays. EFFECT ONLY: the clean guitar is cut, silence between sequences.");
    holdButton.setTooltip("STUTTER HOLD: freeze the stutter on the current (or next) note until you switch this off. Holding the TAP "
                          "switch does the same for as long as you hold it.");
    headroomBox.setTooltip("HEADROOM: Instrument for guitar level, Line for hotter signals such as synths. Set it first, then INPUT and TRIGGER.");
    stutterLenBox.setTooltip("STUTTER LENGTH: how long the repeated slice is, as a note length of the tempo.");
    stutterTypeBox.setTooltip("STUTTER TYPE: what the stutter does with its slice. REPEAT, REVERSE (backwards), RISE (each repeat a tone "
                              "higher), TAPE STOP (slows to a stop), SCRATCH (back and forth), FREEZE (holds it as a cloud).");
    filterTypeBox.setTooltip("FILTER TYPE: LADDER (the pedal's own low-pass, 24 dB/oct), LOW PASS (a cleaner 12 dB/oct one), BAND PASS "
                             "(only the sound around the cutoff), HIGH PASS (only the sound above it), ENVELOPE (an auto-wah: each pick "
                             "opens the filter from where FILTER sets it).");
    filterResSlider.setTooltip("RESONANCE: a peak at the filter's cutoff. Turn right for a sharper, more vocal sweep; fully right it almost "
                               "whistles. Double-click for the pedal's own setting.");

    // preset bar and expression morph strip
    prevButton.setTooltip("Previous preset.");
    nextButton.setTooltip("Next preset.");
    saveButton.setTooltip("SAVE: store the current sound in this preset slot.");
    tempoLabel.setTooltip("TEMPO: the speed of the sequencer and stutter, 23.4 to 240 BPM. Drag the number up or down, double-click it "
                          "to type one, tap the TAP switch, or turn on SYNC to follow your DAW (the number then shows the host tempo).");
    presetLabel.setTooltip("The preset name: click it (or the arrow) for the list of presets, undo and redo (Ctrl+Z, Ctrl+Shift+Z), save, "
                           "rename, export and import. An orange dot means the sound has unsaved changes.");
    presetMenuButton.setTooltip("PRESET MENU: save to this slot, rename, export the sound as a file to give to other players, or import "
                                "a file someone gave you (it replaces the slot that is showing).");
    setUpButton.setTooltip("STORE A: keep the knobs as they are now as sound A, the 'toe up' end (expression slider fully left).");
    setDownButton.setTooltip("STORE B: keep the knobs as they are now as sound B, the 'toe down' end (expression slider fully right).");
    expressionSlider.setTooltip("EXPRESSION: sweeps from sound A to sound B. Needs MORPH switched on.");
    morphButton.setTooltip("MORPH: when on, the sound follows the expression slider instead of the knobs.");
}

void BitVibeEditor::nameControls()
{
    // The knobs and boxes have their names painted beside them, so a screen reader would only hear "slider" or
    // "combo box": each takes the name its tooltip starts with ("TRIGGER: how easily ..." gives TRIGGER).
    std::function<void(Component&)> name = [&name](Component& parent)
    {
        for (auto* c : parent.getChildren())
        {
            if (dynamic_cast<Slider*>(c) != nullptr || dynamic_cast<ComboBox*>(c) != nullptr || dynamic_cast<LevelMeter*>(c) != nullptr
                || dynamic_cast<OutputMeter*>(c) != nullptr || dynamic_cast<FootSwitch*>(c) != nullptr)
                if (auto* tip = dynamic_cast<TooltipClient*>(c))
                    if (const auto text = tip->getTooltip(); text.containsChar(':'))
                        c->setTitle(text.upToFirstOccurrenceOf(":", false, false).trim());
            name(*c);
        }
    };
    name(content);
    presetMenuButton.setTitle("PRESET MENU");
}

int BitVibeEditor::hearChoice() const
{
    if (rawValue(ids::seqOnly) > 0.5f)
        return 2;
    return rawValue(ids::fxTriggers) > 0.5f ? 1 : 0;
}

void BitVibeEditor::setHear(int choice)
{
    if (choice == hearChoice())
        return;
    auto* seqOnly = proc.apvts.getParameter(ids::seqOnly);
    auto* fxTriggers = proc.apvts.getParameter(ids::fxTriggers);
    for (auto* p : { seqOnly, fxTriggers })
        p->beginChangeGesture();
    seqOnly->setValueNotifyingHost(choice == 2 ? 1.0f : 0.0f);
    fxTriggers->setValueNotifyingHost(choice == 1 ? 1.0f : 0.0f);
    for (auto* p : { seqOnly, fxTriggers })
        p->endChangeGesture();
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

// Right-click on a step (of the strip or the step editor): what the step controls, the loop section and the length.
void BitVibeEditor::showStepMenu(int step, Component* target)
{
    const int own = jlimit(0, 3, roundToInt(rawValue(ids::stepType(step).toRawUTF8())));
    static const char* typeNames[3] { "PITCH", "FILTER", "RATE" };
    PopupMenu menu;
    menu.addSectionHeader("STEP " + String(step + 1));
    menu.addItem(1, "Follow SEQ TYPE (" + String(typeNames[(int) seqType()]) + ")", true, own == 0);
    menu.addItem(2, "Pitch", true, own == 1);
    menu.addItem(3, "Filter", true, own == 2);
    menu.addItem(4, "Rate (sample rate)", true, own == 3);
    menu.addSeparator();
    menu.addSeparator();
    const int length = roundToInt(rawValue(ids::seqLength));
    menu.addItem(13, "End the sequence here (" + String(step + 1) + (step == 0 ? " step)" : " steps)"), step + 1 != length);
    menu.addItem(14, "Put this step back");
    if (target != stepEditor.get() || !altLayer)
        menu.addItem(10, "Show this step");
    menu.addItem(11, "Loop this step only");
    const bool loopOn = rawValue(ids::loopOn) > 0.5f;
    menu.addItem(12, "Loop off", loopOn);
    Component::SafePointer<BitVibeEditor> safe(this);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(target), [safe, step](int result)
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
        {
            if (auto* param = safe->proc.apvts.getParameter(ids::loopOn))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost(0.0f);
                param->endChangeGesture();
            }
        }
        else if (result == 13 || result == 14)
        {
            auto* param = safe->proc.apvts.getParameter(result == 13 ? String(ids::seqLength) : ids::step(step));
            if (param != nullptr)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost(result == 13 ? param->convertTo0to1((float) (step + 1)) : param->getDefaultValue());
                param->endChangeGesture();
            }
        }
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

    g.fillAll(kTheme.background);
    g.addTransform(AffineTransform::translation(0.0f, (float) kTabsH));

    const auto header = kHeader.withHeight(advancedRight ? kHeader.getHeight() : 56).toFloat();
    g.setColour(kTheme.panel);
    g.fillRoundedRectangle(header, 10.0f);
    g.setColour(kTheme.panelEdge);
    g.drawRoundedRectangle(header.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(kTheme.well);
    g.fillRoundedRectangle(kNameBox.toFloat(), 4.0f);
    g.setColour(kTheme.outline);
    g.drawRoundedRectangle(kNameBox.toFloat().reduced(0.5f), 4.0f, 1.0f);

    g.addTransform(AffineTransform::translation(0.0f, (float) kFaceDy));

    // Enclosure: powder coat (black in the first theme) with a fine metal flake.
    const auto face = kFace.toFloat();
    g.setGradientFill(ColourGradient(kTheme.faceTop, face.getX(), face.getY(), kTheme.faceBottom, face.getRight(),
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
            g.setColour(kTheme.flake.withAlpha(0.015f + 0.13f * b * b * b));
            g.fillRect(face.getX() + rng.nextFloat() * face.getWidth(), face.getY() + rng.nextFloat() * face.getHeight(), 1.0f, 1.0f);
        }
    }
    g.setColour(Colours::white.withAlpha(0.10f));
    g.drawRoundedRectangle(face.reduced(0.5f), 16.0f, 1.0f);
    g.setColour(kTheme.rim);
    g.drawRoundedRectangle(face.expanded(0.5f), 16.0f, 1.5f);
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
    presetMenuButton.setBounds(kNameBox.getRight() - 26, kNameBox.getY() + 2, 24, 22);
    setUpButton.setBounds(kSetUpBounds);
    expressionSlider.setBounds(kExpressionBounds);
    setDownButton.setBounds(kSetDownBounds);
    morphButton.setBounds(kMorphBounds);

    // pedal face and global panel (laid out in face coordinates, then moved below the header)
    auto face = [](Rectangle<int> r) { return r.translated(0, kFaceDy); };

    for (int i = 0; i < kKnobs; ++i)
        knobs[(size_t) i].front.setBounds(face(knobBounds(i)));
    stepEditor->setBounds(face({ 16, 70, StepEditor::kWidth, StepEditor::kHeight }));

    typePitchButton.setBounds(face({ 18, 371, 50, 22 }));
    typeFilterButton.setBounds(face({ 71, 371, 50, 22 }));
    typeRateButton.setBounds(face({ 124, 371, 50, 22 }));
    tempoLabel.setBounds(face(Rectangle<int>(130, 30).withCentre({ 240, 418 })));
    altButton.setBounds(face(Rectangle<int>(78, 22).withCentre({ 392, 383 })));
    bypassSwitch.setBounds(face(Rectangle<int>(84, 84).withCentre({ 88, 462 })));
    tapSwitch.setBounds(face(Rectangle<int>(84, 84).withCentre({ 392, 462 })));

    stepStrip.setBounds(face({ 186, 352, 140, 28 }));
    syncButton.setBounds(face({ 194, 480, 92, 24 })); // the middle of the pedal, under the tempo it changes
    beatDog->setBounds(face(beatDogArea()));

    // the in / out panel at the bottom of the right-hand column (laid out as if at x = 0, then moved across): your signal in
    // (input, trigger and its meter, headroom) on the left, what comes out (dry/wet, output, HEAR) on the right, then in
    // ADVANCED the stutter's length, type and hold
    const int gy = globalTop();
    auto side = [&](Rectangle<int> r) { return face(r.translated(kSideX, 0)); };
    inSlider.setBounds(side(Rectangle<int>(52, 52).withCentre({ 50, gy + 46 })));
    trigSlider.setBounds(side(Rectangle<int>(52, 52).withCentre({ 112, gy + 46 })));
    triggerMeter.setBounds(side({ 146, gy + 16, 82, 86 }));
    mixSlider.setBounds(side(Rectangle<int>(52, 52).withCentre({ 300, gy + 46 })));
    outSlider.setBounds(side(Rectangle<int>(52, 52).withCentre({ 390, gy + 46 })));
    outputMeter.setBounds(side({ 434, gy + 12, 14, 88 }));
    headroomBox.setBounds(side({ 98, gy + 110, 128, 24 }));
    hearBox.setBounds(side({ 296, gy + 110, 160, 24 }));
    stutterLenBox.setBounds(side({ 98, gy + 144, 96, 24 }));
    stutterTypeBox.setBounds(side({ 200, gy + 144, 110, 24 }));
    holdButton.setBounds(side({ 318, gy + 144, 76, 24 }));
    filterTypeBox.setBounds(side({ 98, gy + 178, 110, 24 }));
    filterResSlider.setBounds(side(Rectangle<int>(36, 36).withCentre({ 318, gy + 190 })));

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
    // the knobs of a panel: up to three a row, two rows (a single row sits in the middle), right of the graph
    auto spread = [this](std::initializer_list<std::pair<Slider*, bool>> row, int size, int top)
    {
        int shown = 0;
        for (const auto& k : row)
            shown += (advancedRight || !k.second) ? 1 : 0;
        const int rows = (shown + 2) / 3;
        int index = 0;
        for (const auto& k : row)
            if (advancedRight || !k.second)
            {
                const int cx = kSideX + kKnobsX + (int) std::lround((456.0 - kKnobsX) * ((double) (index % 3) + 0.5) / 3.0);
                const int cy = top + (rows == 1 ? kKnobMid : (index < 3 ? kKnobRow1 : kKnobRow2));
                k.first->setBounds(Rectangle<int>(size, size).withCentre({ cx, cy }).translated(0, kFaceDy));
                ++index;
            }
    };
    spread({ { &distDriveSlider, false }, { &distToneSlider, true }, { &distLevelSlider, false } }, 48, dist.getY());
    spread({ { &reverbDecaySlider, false }, { &reverbToneSlider, true }, { &reverbMixSlider, false }, { &reverbLevelSlider, true },
             { &reverbLowSlider, true }, { &reverbHighSlider, true } },
           44, reverb.getY());
    spread({ { &delayMixSlider, false }, { &delayDecaySlider, false }, { &delayRepeatsSlider, false }, { &delayWobbleSlider, false },
             { &delayLowSlider, true }, { &delayHighSlider, true } },
           44, delay.getY());
    effectTabs.setBounds(face({ kSideX, samplesTop(), 456, EffectTabs::kHeight }));
    samplePanel->setBounds(face(effectsArea()));
    eqPanel->setBounds(face(effectsArea()));
    tremoloPanel->setBounds(face(tremoloArea()));
    for (EffectGraph* graph : std::initializer_list<EffectGraph*> { distGraph.get(), delayGraph.get(), reverbGraph.get() })
        graph->setBounds(face({ kSideX + kGraphX, dist.getY() + 28, kGraphW, kEffectsH - 40 }));
    patternPanel->setBounds(face({ kSideX, patternTop(), PatternPanel::kWidth, patternPanel->currentHeight() }));

    // Everything above was laid out as if the pedal started at the top: move it below the tab strip.
    for (auto* c : pedalComponents)
        c->setTopLeftPosition(c->getX(), c->getY() + kTabsH);

    helpButton.setBounds(12, 8, 70, 24);
    viewButton.setBounds(kSideX + 212, 8, 80, 24);
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
            g.fillEllipse(Rectangle<float>(7.0f, 7.0f).withCentre({ (float) kNameBox.getRight() - 34.0f, (float) kNameBox.getCentreY() }));
        }

        g.setColour(morphOn ? kAccent : kInkDim);
        g.setFont(plain(10.0f));
        const String caption = morphOn ? "MORPH ON:  " + String(roundToInt(rawValue(ids::expression) * 100.0f)) + "% OF THE WAY FROM A TO B"
                                       : "MORPH OFF";
        if (advancedRight) // SIMPLE hides the morph row
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

    const bool morphOn = rawValue(ids::morph) > 0.5f;

    // Knob labels and readouts (on the Alt layer the step editor draws itself)
    for (int i = 0; i < kKnobs && !altLayer; ++i)
    {
        const auto c = knobCentre(i);
        g.setColour(kInk);
        g.setFont(bold(12.5f));
        g.drawText(knobs[(size_t) i].label, Rectangle<int>(112, 16).withCentre({ (int) c.x, (int) c.y - 62 }), Justification::centred);
        g.setColour(kInk.withAlpha(morphOn ? 0.4f : 0.75f)); // with the morph on, the knob values are not what you hear
        g.setFont(plain(11.5f));
        g.drawText(readoutText(i), Rectangle<int>(120, 16).withCentre({ (int) c.x, (int) c.y + 56 }), Justification::centred);
    }

    // Footswitch area
    const bool bypassed = rawValue(ids::bypass) > 0.5f;
    drawLed(g, { 88.0f, 410.0f }, 6.0f, kRed, !bypassed);

    const bool tapBlink = proc.getEngine().getBeatPhase() < 0.18f;
    drawLed(g, { 392.0f, 410.0f }, 6.0f, kAccent, tapBlink);

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

    // The panel under the pedal (it moves with the mode, so it is drawn here rather than in the background)
    {
        const int gy = globalTop();
        Graphics::ScopedSaveState inOutState(g);
        g.addTransform(AffineTransform::translation((float) kSideX, 0.0f)); // the in / out panel sits in the right-hand column
        const Rectangle<float> panelArea(12.0f, (float) gy, 456.0f, (float) globalHeight());
        g.setColour(pedalstyle::panel);
        g.fillRoundedRectangle(panelArea, 10.0f);
        g.setColour(pedalstyle::panelEdge);
        g.drawRoundedRectangle(panelArea.reduced(0.5f), 10.0f, 1.0f);
        g.drawVerticalLine(240, (float) gy + 10.0f, (float) gy + 136.0f);
        pedalstyle::drawSectionTitle(g, 26, gy + 6, "YOUR SIGNAL IN");
        pedalstyle::drawSectionTitle(g, 252, gy + 6, "WHAT COMES OUT");
        const bool seqOnly = rawValue(ids::seqOnly) > 0.5f;
        pedalstyle::drawControlLabel(g, 50, gy + 76, "INPUT", String(rawValue(ids::inputGain), 1) + " dB");
        pedalstyle::drawControlLabel(g, 112, gy + 76, "TRIGGER", String(roundToInt(rawValue(ids::sensitivity) * 100.0f)) + "%");
        pedalstyle::drawControlLabel(g, 300, gy + 76, "DRY/WET", seqOnly ? String("EFFECT ONLY") : String(roundToInt(rawValue(ids::mix) * 100.0f)) + "% wet");
        pedalstyle::drawControlLabel(g, 390, gy + 76, "OUTPUT", String(rawValue(ids::outputGain), 1) + " dB");
        g.setColour(kInkDim);
        g.setFont(bold(9.5f));
        g.drawText("HEADROOM", Rectangle<int>(24, gy + 110, 72, 24), Justification::centredLeft);
        g.drawText("HEAR", Rectangle<int>(252, gy + 110, 40, 24), Justification::centredLeft);
        if (advancedRight)
        {
            g.drawText("STUTTER", Rectangle<int>(24, gy + 144, 72, 24), Justification::centredLeft);
            g.drawText("FILTER", Rectangle<int>(24, gy + 178, 72, 24), Justification::centredLeft);
            g.drawText("RESONANCE", Rectangle<int>(222, gy + 178, 74, 24), Justification::centredRight);
            g.drawText(String(roundToInt(rawValue(ids::filterRes) * 100.0f)) + "%", Rectangle<int>(342, gy + 178, 50, 24), Justification::centredLeft);
        }
        else
        {
            // what SIMPLE hides, when it is changing the sound
            StringArray on;
            if (rawValue(ids::morph) > 0.5f)
                on.add("MORPH");
            if (rawValue(ids::hold) > 0.5f)
                on.add("STUTTER HOLD");
            if (roundToInt(rawValue(ids::stutterLen)) != 1)
                on.add("STUTTER " + stutterLenBox.getText().toUpperCase());
            if (roundToInt(rawValue(ids::stutterType)) != 0)
                on.add("STUTTER TYPE " + stutterTypeBox.getText().toUpperCase());
            if (roundToInt(rawValue(ids::filterType)) != 0)
                on.add("FILTER " + filterTypeBox.getText().toUpperCase());
            if (std::abs(rawValue(ids::filterRes) - bitvibe::mapping::kDefaultFilterRes) > 0.02f)
                on.add("RESONANCE " + String(roundToInt(rawValue(ids::filterRes) * 100.0f)) + "%");
            if (!on.isEmpty())
            {
                g.setColour(kAccent);
                g.setFont(plain(10.0f));
                g.drawFittedText("Also on: " + on.joinIntoString(", ") + "  (ADVANCED to change)", Rectangle<int>(24, gy + 140, 432, 14),
                                 Justification::centredLeft, 1, 0.9f);
            }
        }
    }

    struct Entry
    {
        const char* name;
        String value;
        bool advancedOnly = false;
    };
    // where the n-th of the labels that are showing goes (the knobs above them are spread the same way)
    // where a knob's name and value go: under the knob, laid out like spread() in resized()
    auto labelPos = [this](const auto& list, size_t i, int top)
    {
        int shown = 0, index = 0;
        for (size_t k = 0; k < list.size(); ++k)
            if (advancedRight || !list[k].advancedOnly)
            {
                if (k < i)
                    ++index;
                ++shown;
            }
        const int rows = (shown + 2) / 3;
        const int cx = kSideX + kKnobsX + (int) std::lround((456.0 - kKnobsX) * ((double) (index % 3) + 0.5) / 3.0);
        const int cy = top + (rows == 1 ? kKnobMid : (index < 3 ? kKnobRow1 : kKnobRow2)) + 26;
        return Point<int>(cx, cy);
    };
    // Under the right-hand column: why those panels may be quiet.
    {
        const bool sequenceOff = roundToInt(rawValue(ids::seqPlay)) == 0;
        const bool stutterOff = bitvibe::mapping::stutterMode(roundToInt(rawValue(ids::stutter))).off;
        if (sequenceOff && stutterOff)
        {
            g.setColour(kAccent);
            g.setFont(plain(10.5f));
            g.drawFittedText("The sequencer and the stutter are both off, so the effects on this side have nothing to act on. "
                             "Turn up the SEQUENCER or STUTTER knob on the pedal.", Rectangle<int>(kSideX + 8, columnBottom() + 10, 440, 30),
                             Justification::topLeft, 2);
        }
    }

    // The distortion, delay and reverb panels are drawn by the editor (the other effects draw themselves): the one showing.
    const auto dist = distArea(), delay = delayArea(), reverb = reverbArea();
    if (effectTab == kTabDist || effectTab == kTabDelay || effectTab == kTabReverb)
    {
        g.setColour(pedalstyle::panel);
        g.fillRoundedRectangle(dist.toFloat(), 10.0f);
        g.setColour(pedalstyle::panelEdge);
        g.drawRoundedRectangle(dist.toFloat().reduced(0.5f), 10.0f, 1.0f);
    }
    const float distAlpha = rawValue(ids::distOn) > 0.5f ? 1.0f : kOffAlpha;
    const float reverbAlpha = rawValue(ids::reverbOn) > 0.5f ? 1.0f : kOffAlpha;

    // Distortion panel labels
    if (effectTab == kTabDist)
    {
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
        pedalstyle::drawControlLabel(g, labelPos(distEntries, i, dist.getY()).x, labelPos(distEntries, i, dist.getY()).y, distEntries[i].name, distEntries[i].value, distAlpha);
    }
    }

    // Delay panel labels
    if (effectTab == kTabDelay)
    {
        const float delayAlpha = rawValue(ids::delayOn) > 0.5f ? 1.0f : kOffAlpha;
        drawPanelTitle(g, kSideX + kPanelTitleX, delay.getY() + 4, 4, "DELAY");
        g.setColour(kInkDim.withMultipliedAlpha(jmax(delayAlpha, kOffTextAlpha)));
        g.setFont(bold(9.5f));
        g.drawText("TIME", Rectangle<int>(kSideX + 300, delay.getY() + 4, 40, 18), Justification::centredRight);
        const int repeats = roundToInt(rawValue(ids::delayRepeats));
        const float delayLow = rawValue(ids::delayLowCut), delayHigh = rawValue(ids::delayHighCut);
        const float wobble = rawValue(ids::delayWobble);
        const std::array<Entry, 6> delayEntries {{
            { "MIX", String(roundToInt(rawValue(ids::delayMix) * 100.0f)) + "%" },
            { "DECAY", String(roundToInt(rawValue(ids::delayDecay) * 100.0f)) + "%" },
            { "REPEATS", repeats >= bitvibe::kDelayForever ? String("FOREVER") : String(repeats) },
            { "WOBBLE", wobble < 0.005f ? String("OFF") : String(roundToInt(wobble * 100.0f)) + "%" },
            { "LO CUT", delayLow <= 20.5f ? String("OFF") : hzText(delayLow), true },
            { "HI CUT", delayHigh >= 19950.0f ? String("OFF") : hzText(delayHigh), true },
        }};
        for (size_t i = 0; i < delayEntries.size(); ++i)
        {
            if (!advancedRight && delayEntries[i].advancedOnly)
                continue;
            pedalstyle::drawControlLabel(g, labelPos(delayEntries, i, delay.getY()).x, labelPos(delayEntries, i, delay.getY()).y, delayEntries[i].name, delayEntries[i].value, delayAlpha);
        }
    }

    // Reverb panel labels
    if (effectTab != kTabReverb)
        return;
    drawPanelTitle(g, kSideX + kPanelTitleX, reverb.getY() + 4, 5, "REVERB");
    const float lowCut = rawValue(ids::reverbLowCut), highCut = rawValue(ids::reverbHighCut);
    const std::array<Entry, 6> reverbEntries {{
        { "DECAY", String(roundToInt(rawValue(ids::reverbDecay) * 100.0f)) + "%" },
        { "TONE", String(roundToInt(rawValue(ids::reverbTone) * 100.0f)) + "%", true },
        { "MIX", String(roundToInt(rawValue(ids::reverbMix) * 100.0f)) + "%" },
        { "LEVEL", String(rawValue(ids::reverbLevel), 1) + " dB", true },
        { "LO CUT", lowCut <= 20.5f ? String("OFF") : hzText(lowCut), true },
        { "HI CUT", highCut >= 19950.0f ? String("OFF") : hzText(highCut), true },
    }};
    for (size_t i = 0; i < reverbEntries.size(); ++i)
    {
        if (!advancedRight && reverbEntries[i].advancedOnly)
            continue;
        pedalstyle::drawControlLabel(g, labelPos(reverbEntries, i, reverb.getY()).x, labelPos(reverbEntries, i, reverb.getY()).y, reverbEntries[i].name, reverbEntries[i].value, reverbAlpha);
    }
}

// What follows the parameters and the playing sequence: the step strip, the step editor (on the Alt layer), and the
// effect panels' power lights and dimming. Run by the timer, and straight away when the
// editor opens or the Alt layer changes, so nothing shows a stale state first.
void BitVibeEditor::updateLiveState()
{
    beatDog->setBeat(proc.getEngine().getBeatInBarPhase(), proc.getEngine().getBeatInBar(), proc.getEffectiveBpm(), proc.getEngine().getBarSteps()); // repaints only on a change
    {
        std::array<StepStrip::Step, bitvibe::kNumSteps> steps {};
        for (int i = 0; i < bitvibe::kNumSteps; ++i)
        {
            const float knob = rawValue(ids::step(i).toRawUTF8());
            const auto type = stepType(i);
            const auto decoded = bitvibe::mapping::decodeStep(knob, type);
            steps[(size_t) i] = { knob, decoded.kind == bitvibe::mapping::Step::Kind::Skip ? 0 : (decoded.kind == bitvibe::mapping::Step::Kind::Mute ? 1 : 2),
                                  (int) type };
        }
        const bool loop = rawValue(ids::loopOn) > 0.5f;
        const int playing = proc.getEngine().getCurrentStep();
        stepStrip.set(steps, roundToInt(rawValue(ids::seqLength)), playing, loop ? roundToInt(rawValue(ids::loopFrom)) : -1,
                      loop ? roundToInt(rawValue(ids::loopTo)) : -1);
        if (altLayer)
            stepEditor->refresh(playing);
    }
    {
        mixSlider.setAlpha(rawValue(ids::seqOnly) > 0.5f ? kOffAlpha : 1.0f); // EFFECT ONLY makes DRY/WET do nothing
        if (hearBox.getSelectedId() != hearChoice() + 1)
            hearBox.setSelectedId(hearChoice() + 1, dontSendNotification);
        // the power buttons glow while their effect acts; a switched-off panel's controls are dimmed
        const bool live = proc.getEngine().isEffectSideActive();
        effectTabs.set(effectTab, advancedRight ? EffectTabs::kCount : EffectTabs::kCount - 1,
                       { rawValue(ids::sampleOn) > 0.5f, rawValue(ids::distOn) > 0.5f, rawValue(ids::tremOn) > 0.5f, rawValue(ids::delayOn) > 0.5f,
                         rawValue(ids::reverbOn) > 0.5f, rawValue(ids::eqOn) > 0.5f },
                       live);
        setPowerActive(distOnButton, live);
        setPowerActive(reverbOnButton, live);
        setPowerActive(delayOnButton, live);
        const bool delayOn = rawValue(ids::delayOn) > 0.5f;
        for (Component* c : std::initializer_list<Component*> { &delayMixSlider, &delayDecaySlider, &delayRepeatsSlider, &delayWobbleSlider,
                                                                 &delayLowSlider, &delayHighSlider, &delayTimeBox })
            c->setAlpha(offAlphaFor(*c, delayOn));
        const bool distOn = rawValue(ids::distOn) > 0.5f;
        for (Component* c : std::initializer_list<Component*> { &distDriveSlider, &distToneSlider, &distLevelSlider, &distTypeBox, &distReverbButton })
            c->setAlpha(offAlphaFor(*c, distOn));
        const bool reverbOn = rawValue(ids::reverbOn) > 0.5f;
        for (Component* c : std::initializer_list<Component*> { &reverbDecaySlider, &reverbToneSlider, &reverbMixSlider, &reverbLevelSlider,
                                                                 &reverbLowSlider, &reverbHighSlider, &reverbTypeBox, &reverbReverseButton })
            c->setAlpha(offAlphaFor(*c, reverbOn));
        distGraph->setAlpha(offAlphaFor(*distGraph, distOn));
        delayGraph->setAlpha(offAlphaFor(*delayGraph, delayOn));
        reverbGraph->setAlpha(offAlphaFor(*reverbGraph, reverbOn));
        for (EffectGraph* graph : std::initializer_list<EffectGraph*> { distGraph.get(), delayGraph.get(), reverbGraph.get() })
            if (graph->isShowing())
                graph->refresh();
    }
}

void BitVibeEditor::timerCallback()
{
    if (pedalstyle::themeSerial() != shownThemeSerial) // another window changed the theme
        applyTheme();

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
        // the input history: this frame's loudest moment, and whether a pick was caught in it
        const float peak = proc.getEngine().takeInputPeak();
        const int attacks = proc.getEngine().getAttackCount();
        triggerMeter.push(peak > 1.0e-5f ? 20.0f * std::log10(peak) : -100.0f, bitvibe::AttackDetector::floorDb(rawValue(ids::sensitivity)),
                          attacks != seenAttacks);
        seenAttacks = attacks;
        const float outPeak = proc.takeOutputPeak();
        outputDb = jmax(outPeak > 1.0e-5f ? 20.0f * std::log10(outPeak) : -100.0f, outputDb - 1.5f);
        outputMeter.set(outputDb, outPeak > 1.0f);
    }
    updateLiveState();
    refreshPresetName();
    showSeqType();
    {
        const bool sync = rawValue(ids::tempoSync) > 0.5f;
        if (sync == tempoEditable) // typing a tempo does nothing while the host tempo is followed
        {
            tempoEditable = !sync;
            tempoLabel.setEditable(false, tempoEditable, false);
            tempoLabel.setMouseCursor(tempoEditable ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
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
        if (tremoloPanel->isVisible())
            tremoloPanel->refresh();
        patternPanel->refresh();
        repaint();
    }
}
