#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

class DrumPage;

// Colours and fonts shared by the pedal page and the DRUMS page.
namespace pedalstyle
{
// Every colour the page is drawn in. The values here are the first theme (NIGHT); the others are in themes().
// The chrome of the footswitches and the expression thumb is the same in every theme.
struct Theme
{
    const char* name = "NIGHT";
    juce::Colour ink { 0xfff2f0e6 };    // engraved text
    juce::Colour inkDim { 0xff9a988f };
    juce::Colour inkLabel { 0xffc4c2b9 }; // a control's name: quieter than a panel's title, louder than its value
    juce::Colour accent { 0xffffa31a };   // Alt layer / lit LEDs
    juce::Colour onAccent { 0xff17171a }; // text on the accent (or on a step's colour)
    juce::Colour red { 0xffff3b30 };
    juce::Colour background { 0xff0d0d0f }; // behind everything
    juce::Colour faceTop { 0xff2a2a2e }, faceBottom { 0xff121214 }; // the pedal's enclosure
    juce::Colour flake { 0xffffffff };      // the metal flake in its paint
    juce::Colour rim { 0xcc000000 };        // the line around it
    juce::Colour panel { 0xff17171a }, panelEdge { 0xff2c2c31 };
    juce::Colour well { 0xff0e0e10 };       // sunk into a panel: read-outs, meters, graphs, text boxes
    juce::Colour outline { 0xff3c3c42 };    // around a box, a groove or an unlit button
    juce::Colour mid { 0xff6a6a72 };        // the mouse over an outline; a meter's unlit bars
    juce::Colour unlit { 0xff3c3c42 };      // a light that is off
    juce::Colour buttonTop { 0xff3a3a40 }, buttonBottom { 0xff17171a }, buttonPressed { 0xff26262a }, buttonEdge { 0xff000000 };
    juce::Colour litTop { 0xff6a4a12 }, litBottom { 0xff3d2a08 }; // a button that is on
    juce::Colour selection { 0xff4a3410 };  // the highlighted menu item and selected text
    juce::Colour popup { 0xff1a1a1d }, tooltip { 0xff26262b }, tooltipEdge { 0xff55555d };
    juce::Colour knobLight { 0xff5a5a60 }, knobDark { 0xff0e0e10 }, capTop { 0xff2a2a2e }, capBottom { 0xff161618 };
    juce::Colour pointer { 0xfff2f0e6 };    // a knob's pointer (on the knob, so it can differ from the ink)
    juce::Colour cell { 0xff1d1d21 }, cellStrong { 0xff28282d }, cellPlaying { 0xff4a4a52 }, cellEdge { 0xff0a0a0c }; // grids
    juce::Colour gridLine { 0xff222226 }, gridMajor { 0xff3a3a40 };
    juce::Colour screen { 0xff19ff3a }, screenEdge { 0xff1f3a26 }; // BEAT DOG's screen
    std::array<juce::Colour, 3> steps { juce::Colour(0xff5aa9ff), juce::Colour(0xff4fd17c), juce::Colour(0xffc58bff) }; // pitch, filter, rate
};

// The themes in the VIEW menu, the first one being the default.
const std::vector<Theme>& themes();
namespace detail
{
inline Theme shown;
inline int shownIndex = 0, serial = 0;
} // namespace detail
// The theme every editor paints in. It is one for the whole computer (kept with the window settings), so changing it
// changes every open window: an editor checks serial (which goes up with each change) and repaints.
inline const Theme& theme = detail::shown;
void setTheme(int index); // wraps around, so themeIndex() + 1 is always the next one
inline int themeIndex() { return detail::shownIndex; }
int findTheme(const juce::String& name); // its index, or 0 (the default) for a name that is not a theme
inline int themeSerial() { return detail::serial; }

inline const juce::Colour& ink = detail::shown.ink;
inline const juce::Colour& inkDim = detail::shown.inkDim;
inline const juce::Colour& inkLabel = detail::shown.inkLabel;
inline const juce::Colour& accent = detail::shown.accent;
inline const juce::Colour& red = detail::shown.red;
inline const juce::Colour& panel = detail::shown.panel;
inline const juce::Colour& panelEdge = detail::shown.panelEdge;
// No text is drawn smaller than kMinText: the window often opens smaller than it is designed at (to fit the screen),
// and the small print has to stay readable there.
inline constexpr float kMinText = 11.0f;
inline juce::Font bold(float h) { return juce::Font(juce::FontOptions(juce::jmax(h, kMinText), juce::Font::bold)); }
inline juce::Font plain(float h) { return juce::Font(juce::FontOptions(juce::jmax(h, kMinText), juce::Font::plain)); }

// The right-hand column's effect panels are numbered in the order the sequenced sound goes through them. Draws the
// number in a small badge at (x, y) and the title after it (y is the top of the title row).
void drawPanelTitle(juce::Graphics&, int x, int y, int number, const juce::String& title);
inline constexpr int kPanelTitleX = 64; // where a numbered title starts, right of the power button
// A panel's title without a number, in the same style (the loudest text in a panel), and a control's name and value
// under it (centred on cx, the name's top at y).
void drawSectionTitle(juce::Graphics&, int x, int y, const juce::String& title);
void drawControlLabel(juce::Graphics&, int cx, int y, const juce::String& name, const juce::String& value, float alpha = 1.0f);

// A panel's power button: a ToggleButton with this property set is drawn as a pill with a light (lit when on, glowing
// while the property "active" is set, i.e. while the effect is acting on the sound) and ON / OFF.
inline const juce::Identifier powerProperty { "power" }, activeProperty { "active" };
inline void makePowerButton(juce::ToggleButton& b) { b.getProperties().set(powerProperty, true); }
// Sets the glow; repaints only on a change.
inline void setPowerActive(juce::ToggleButton& b, bool active)
{
    if ((bool) b.getProperties().getWithDefault(activeProperty, false) != active)
    {
        b.getProperties().set(activeProperty, active);
        b.repaint();
    }
}
// A switched-off panel dims its knobs to kOffAlpha, but its text (the names and values, buttons and boxes) only to
// kOffTextAlpha, so it can still be read and set up before the panel is switched on.
inline constexpr float kOffAlpha = 0.4f, kOffTextAlpha = 0.7f;
inline float offAlphaFor(const juce::Component& c, bool on)
{
    return on ? 1.0f : (dynamic_cast<const juce::Slider*>(&c) != nullptr ? kOffAlpha : kOffTextAlpha);
}
// The light on a switch (centre c): orange when on, dark when off.
inline void drawSwitchLight(juce::Graphics& g, juce::Point<float> c, bool on)
{
    g.setColour(on ? accent : theme.well);
    g.fillEllipse(c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
    g.setColour(on ? accent.brighter(0.4f) : theme.outline);
    g.drawEllipse(c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f, 0.8f);
}
// Switching an effect on with its mix all the way down would change nothing you can hear, so the player's click also
// brings the mix up to a level that can be heard. (Only a click does this: a host or a project keeps its own values.)
inline void raiseMixIfSilent(juce::AudioProcessorValueTreeState& apvts, const juce::String& mixId, float audible)
{
    if (auto* param = apvts.getParameter(mixId); param != nullptr && param->getValue() <= 0.0f)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(audible));
        param->endChangeGesture();
    }
}

// What a sequencer step controls, in one colour each: pitch, filter, sample rate.
inline juce::Colour stepTypeColour(int type)
{
    return theme.steps[(size_t) juce::jlimit(0, 2, type)];
}
} // namespace pedalstyle

// Dark, brushed look shared by every control in the editor.
class PedalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel();
    void applyTheme(); // takes the colours of the theme now showing (menus, tooltips, text boxes)

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
};

// Round metal footswitch with separate press / release callbacks (the tap switch needs both).
// What a screen reader is told about a control drawn by hand: its role, a press action, its name (the component's
// title, or one worked out when it is asked for), its help (the tooltip) and, for a switch, whether it is on.
class PressableAccessibility : public juce::AccessibilityHandler
{
public:
    PressableAccessibility(juce::Component& c, juce::AccessibilityRole role, std::function<void()> press,
                           std::function<bool()> isOn = {}, std::function<juce::String()> title = {})
        : AccessibilityHandler(c, role, juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, std::move(press))),
          on(std::move(isOn)), name(std::move(title))
    {
    }
    juce::String getTitle() const override { return name ? name() : AccessibilityHandler::getTitle(); }
    juce::String getHelp() const override
    {
        if (auto* tip = dynamic_cast<juce::TooltipClient*>(const_cast<juce::Component*>(&getComponent()))) // getTooltip() is not const
            return tip->getTooltip();
        return {};
    }
    juce::AccessibleState getCurrentState() const override
    {
        auto state = AccessibilityHandler::getCurrentState();
        if (on)
        {
            state = state.withCheckable();
            if (on())
                state = state.withChecked();
        }
        return state;
    }

private:
    std::function<bool()> on;
    std::function<juce::String()> name;
};

class FootSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onPress, onRelease;
    bool isDown() const { return down; }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<PressableAccessibility>(*this, juce::AccessibilityRole::button, [this]
        {
            if (onPress)
                onPress();
            if (onRelease)
                onRelease();
        });
    }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    bool down = false;
};

// Small rectangular push button (ALT, sequencer type). A button that switches something on and off (toggle = true)
// has a light on its left, so it can be told from a button that does something once; arrow = true draws a small
// down arrow (a button that opens a menu).
class PedalButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PedalButton(juce::String label) : text(std::move(label)) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    std::function<void()> onClick;
    bool lit = false, toggle = false, arrow = false;
    void setText(juce::String newText)
    {
        text = std::move(newText);
        repaint();
    }
    const juce::String& getText() const { return text; }
    // a screen reader reads the text on it (or the title, for one without text) and can press it
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<PressableAccessibility>(
            *this, toggle ? juce::AccessibilityRole::toggleButton : juce::AccessibilityRole::button, [this] { if (onClick) onClick(); },
            toggle ? std::function<bool()>([this] { return lit; }) : std::function<bool()>(),
            [this] { return text.isNotEmpty() ? text : getTitle(); });
    }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::String text;
    bool pressed = false;
};

// The output level beside the OUTPUT knob, with a clip light on top: it lights when the output goes over 0 dB (full
// scale) and stays lit for a few seconds, or until it is clicked.
class OutputMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr double kClipHoldMs = 3000.0;
    void set(float levelDb, bool overFullScale);
    bool isClipLit() const;
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    float level = -100.0f;
    double clipUntil = 0.0;
    bool shownClip = false;
};

// The preset name: a click opens the preset menu (the slots, undo and redo, save, rename, export and import).
class PresetName : public juce::Label
{
public:
    std::function<void()> onClick;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<PressableAccessibility>(*this, juce::AccessibilityRole::button, [this] { if (onClick) onClick(); },
                                                        std::function<bool()>(), [this] { return "PRESET: " + getText(); });
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::Label::mouseUp(e);
        if (e.mouseWasClicked() && !isBeingEdited() && onClick)
            onClick();
    }
};

// The big tempo read-out: drag it up or down to change the tempo, double-click it to type one.
class TempoReadout : public juce::Label
{
public:
    std::function<void()> onDragStart, onDragEnd;
    std::function<void(float pixelsUp)> onDrag;

    void mouseDown(const juce::MouseEvent& e) override
    {
        juce::Label::mouseDown(e);
        dragging = false;
        pixelsUp = 0.0f;
        lastY = e.position.y;
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        // Shift turns it five times slower, for fine settings (and can be pressed or let go during the drag)
        pixelsUp -= (e.position.y - lastY) * (e.mods.isShiftDown() ? kFineSpeed : 1.0f);
        lastY = e.position.y;
        if (!dragging && std::abs(e.getDistanceFromDragStartY()) > 2)
        {
            dragging = true;
            if (onDragStart)
                onDragStart();
        }
        if (dragging && onDrag)
            onDrag(pixelsUp);
    }
    static constexpr float kFineSpeed = 0.2f;
    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::Label::mouseUp(e);
        if (dragging && onDragEnd)
            onDragEnd();
        dragging = false;
    }

private:
    bool dragging = false;
    float pixelsUp = 0.0f, lastY = 0.0f;
};

// A rotary knob that repaints when it is grabbed or let go, so the look-and-feel can draw it bigger while it is held.
// Holding Shift while dragging turns it five times slower, for fine settings (Shift can be pressed or let go mid-drag).
class KnobSlider : public juce::Slider
{
public:
    static constexpr float kFineSpeed = 0.2f;
    void mouseDown(const juce::MouseEvent& e) override
    {
        dragAt = lastAt = e.position;
        juce::Slider::mouseDown(e);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        dragAt += (e.position - lastAt) * (e.mods.isShiftDown() ? kFineSpeed : 1.0f);
        lastAt = e.position;
        juce::Slider::mouseDrag(e.withNewPosition(dragAt));
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp(e.withNewPosition(dragAt));
        repaint();
    }

private:
    juce::Point<float> dragAt, lastAt; // where the drag has got to (slowed by Shift), and the mouse's last position
};

// The last few seconds of your playing, next to the TRIGGER knob, newest on the right: a bar for each frame of the
// editor (how loud it was), the line across it is how hard a pick has to be to count, and a pick that was caught is
// lit, with a mark on top. Setting TRIGGER comes down to seeing which notes cross the line.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kColumns = 84; // about 2.8 seconds at the editor's 30 frames a second
    void push(float peakDb, float thresholdDb, bool picked); // once a frame
    void paint(juce::Graphics&) override;
    int countPicks() const; // for tests: the picks in the history

private:
    struct Column
    {
        float db = -100.0f;
        bool picked = false;
    };
    std::array<Column, kColumns> history {};
    int newest = 0;
    float threshold = -30.0f;
};

// A small picture of the six sequencer steps on the front of the pedal, and the quickest way to set the sequence up:
// one bar per step (its knob position) in the colour of what the step controls, skipped steps drawn as a dash, muted
// ones as a cross, the playing step outlined and the loop section framed. Click a step to see its knob, drag across
// steps to make them the loop section, right-click a step for its menu.
class StepStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Step
    {
        float knob = 0.5f;
        int kind = 2; // 0 = skip, 1 = mute, 2 = plays
        int type = 0; // what it controls: 0 = pitch, 1 = filter, 2 = sample rate
    };
    std::function<void(int step)> onStepClick;
    std::function<void(int first, int last)> onLoopDrag; // while dragging; first <= last
    std::function<void()> onLoopDragEnd;
    std::function<void(int step)> onStepMenu;

    void set(const std::array<Step, 6>& steps, int playing, int loopFrom = -1, int loopTo = -1); // loop: -1 = none
    int stepAt(float x) const; // the step under x (clamped to the strip)
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    std::array<Step, 6> shown {};
    int current = -1;
    int loopLo = -1, loopHi = -1;
    int downStep = -1;
    bool dragged = false, menuClick = false;
};

// On the Alt layer, next to each step's name: P, F and S, what the step controls. A lit letter is the step's own
// choice; with none lit the step follows the SEQ TYPE buttons, and the letter in use is outlined. Clicking the lit
// letter again goes back to following.
class StepTypeTags : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void(int choice)> onPick; // 0 = follow SEQ TYPE, 1..3 = pitch, filter, sample rate
    void set(int own, int effective);       // own: 0..3 as above; effective: 0..2
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    int own = 0, effective = 0;
};

// HELP, over the whole window, in two pages: the quick start (what to do first), and WHAT TRIGGERS WHAT, a flow chart of
// what sets each part off, what it starts, what acts while it plays, and where the sound goes. Opens by itself the
// first time, and with the HELP button at the top; the tabs switch pages, a click anywhere else closes it.
class HelpOverlay : public juce::Component
{
public:
    static constexpr int kQuickStart = 0, kTriggers = 1;
    void showPage(int newPage);
    int getPage() const { return page; }

    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> cardBounds() const;
    juce::Rectangle<int> tabBounds(int tab) const;
    void paintQuickStart(juce::Graphics&, juce::Rectangle<int> area);
    void paintTriggers(juce::Graphics&, juce::Rectangle<int> area);
    int page = kQuickStart;
};

class PatternPanel;
class EqPanel;
class SamplePanel;
class TremoloPanel;
class DistortionGraph;
class DelayGraph;
class ReverbGraph;
class SamplerPage;
class BeatDog;

class BitVibeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BitVibeEditor(BitVibeProcessor&);
    ~BitVibeEditor() override;

    void resized() override;
    bool keyPressed(const juce::KeyPress&) override; // Ctrl+Z undoes, Ctrl+Shift+Z (or Ctrl+Y) redoes; Cmd on a Mac
    void mouseDown(const juce::MouseEvent&) override;

    // Everything the editor shows lives in this pane, which is drawn at its designed size and scaled to the window,
    // so the window can be resized freely (the shape stays fixed).
    juce::Component& getContent() { return content; }

    // Swaps the six front-panel knobs for the six sequencer-step knobs (the pedal's Alt layer).
    void setAltLayer(bool on);
    // The SAMPLER page over the whole window (the sample blend panel's SAMPLER button opens it).
    void showSampler(bool show);
    SamplerPage* getSamplerPage() { return samplerPage.get(); }

    // Shows the DRUMS tab (true) or the pedal (false).
    void showDrums(bool on);

    // SIMPLE mode (the controls that matter most, on both sides) or ADVANCED mode (every control). Settings that SIMPLE
    // hides keep working; they are only out of the way, and the panels say when one of them is on. The window is
    // shorter in SIMPLE (BEAT DOG moves into the room left at the bottom of the right-hand column). Remembered.
    void setRightMode(bool advanced);
    bool isAdvancedRight() const { return advancedRight; }

    // The VIEW menu (top right): the theme, the window size and whether BEAT DOG is shown. All remembered; the theme
    // is shown in every open window.
    juce::PopupMenu viewMenu();
    void viewMenuChosen(int result);
    static constexpr int kViewTheme = 100, kViewSize = 200, kViewDog = 600; // + the theme's index, + the size in percent
    void setThemeFromPage(int index);
    void setWindowScale(float newScale); // 1.0 = the size the page is designed at
    void setBeatDogShown(bool shown);    // hidden, ADVANCED gets shorter (the panel under the pedal moves up)
    bool isBeatDogShown() const { return beatDogShown; }

    // The preset menu (a click on the preset name or its arrow): every slot, undo and redo, save, rename, export, import.
    juce::PopupMenu presetMenu();
    void presetMenuChosen(int result);
    static constexpr int kMenuSave = 1, kMenuRename = 2, kMenuExport = 3, kMenuImport = 4, kMenuUndo = 5, kMenuRedo = 6,
                         kMenuSlot = 100; // + the slot's index

private:
    static constexpr int kKnobs = 6;
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        KnobSlider front, step;
        std::unique_ptr<APVTS::SliderAttachment> frontAttachment, stepAttachment;
        const char* label = "";
    };

    class Content : public juce::Component
    {
    public:
        explicit Content(BitVibeEditor& o) : owner(o) {}
        void paint(juce::Graphics& g) override { owner.paintContent(g); }

    private:
        BitVibeEditor& owner;
    };

    void paintContent(juce::Graphics&);
    void addKnobHelpers(juce::Slider&, const juce::String& paramId); // double-click to reset, value bubble while turning
    void applySize(); // the window size for the page that is showing, at the player's scale
    void applyRightMode();
    // The right-hand column, top to bottom: the beat pattern (when the sequence fires), then the effects in the order the
    // sequenced sound goes through them: sample blend, distortion / fuzz, reverb and (ADVANCED) the EQ. The sample panel
    // changes height with the mode, so everything below it moves.
    int patternTop() const;
    int samplesTop() const;
    juce::Rectangle<int> distArea() const;
    juce::Rectangle<int> tremoloArea() const;
    juce::Rectangle<int> delayArea() const;
    juce::Rectangle<int> reverbArea() const;
    int eqTop() const;
    int columnBottom() const;
    // The panel under the pedal (input, output and, in ADVANCED, the stutter and loop settings), BEAT DOG (under the
    // pedal in ADVANCED, at the bottom of the right-hand column in SIMPLE), and the height of the page for the mode.
    int globalTop() const;
    int globalHeight() const;
    juce::Rectangle<int> beatDogArea() const;
    int pedalPageHeight() const;
    void timerCallback() override;
    void updateLiveState();
    void buildBackground();
    void setTooltips();        // plain-language hover text for every control
    void nameControls();       // a name for a screen reader on every knob and box (the first words of its tooltip)
    void updateStepTooltips(); // the text of the step knobs depends on the sequencer type
    void stepProgram(int direction);
    void refreshPresetName();
    void configureKnob(juce::Slider&, int ticks);
    juce::Rectangle<int> knobBounds(int index) const;
    juce::Point<float> knobCentre(int index) const;
    juce::String readoutText(int index) const;
    float rawValue(const char* id) const;
    bitvibe::SeqType seqType() const;
    bitvibe::SeqType stepType(int step) const; // what that step controls (its own type, or the sequencer type)

    Content content { *this };
    LevelMeter triggerMeter;
    OutputMeter outputMeter;
    float outputDb = -100.0f;
    StepStrip stepStrip;
    std::unique_ptr<BeatDog> beatDog; // the headbanging husky under the pedal
    PedalButton helpButton { "? HELP" };
    PedalButton presetMenuButton { "" }; // the arrow in the preset name box: save, rename, export, import
    std::unique_ptr<juce::FileChooser> presetChooser;
    void showPresetMenu();
    void undoOrRedo(bool undo);
    void exportPresetFile();
    void importPresetFile();
    PedalButton simpleButton { "SIMPLE" }, advancedButton { "ADVANCED" };
    bool advancedRight = false;
    PedalButton viewButton { "VIEW" };
    bool beatDogShown = true;
    int shownThemeSerial = -1;
    void applyTheme(); // repaints in the theme now showing
    HelpOverlay helpOverlay;
    int seenAttacks = 0;
    juce::ComponentBoundsConstrainer constrainer;
    int baseWidth = 0, baseHeight = 0;     // the size the page that is showing is designed at
    float scale = 1.0f;                    // the window size as a multiple of that
    float savedScale = 1.0f;
    double scaleChangedMs = 0.0;
    float backgroundScale = 0.0f;          // the pixel scale the background image was drawn at

    BitVibeProcessor& proc;
    PedalLookAndFeel laf;
    juce::Image background;

    std::array<Knob, kKnobs> knobs;
    // on the Alt layer, next to each step's name: what the step controls
    std::array<StepTypeTags, kKnobs> stepTypeTags;
    void setStepType(int step, int choice); // 0 = follow SEQ TYPE, 1..3 = pitch, filter, sample rate
    void showStepMenu(int step);
    void setLoopFromStrip(int first, int last);
    bool loopDragging = false;
    int flashStep = -1;        // the step whose knob the strip pointed at, ringed for a moment
    double flashStepUntil = 0.0;

    // global panel
    KnobSlider inSlider, mixSlider, outSlider, trigSlider;
    std::unique_ptr<APVTS::SliderAttachment> inAtt, mixAtt, outAtt, trigAtt;
    juce::ToggleButton syncButton { "SYNC" }, holdButton { "STUTTER HOLD" }, fxTrigButton { "FX ON TRIGGERS" };
    std::unique_ptr<APVTS::ButtonAttachment> syncAtt, holdAtt, fxTrigAtt;
    juce::ToggleButton seqOnlyButton { "SEQ ONLY" };
    std::unique_ptr<APVTS::ButtonAttachment> seqOnlyAtt;
    juce::ComboBox headroomBox, stutterLenBox;
    juce::ToggleButton loopButton { "LOOP" };
    std::unique_ptr<APVTS::ButtonAttachment> loopAtt;
    juce::ComboBox loopFromBox, loopToBox, loopRepeatsBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> loopFromAtt, loopToAtt, loopRepeatsAtt;

    // the delay on the sequenced sound: time, mix, decay, repeats, low and high cut, and its power button
    KnobSlider delayMixSlider, delayDecaySlider, delayRepeatsSlider, delayLowSlider, delayHighSlider;
    std::unique_ptr<APVTS::SliderAttachment> delayMixAtt, delayDecayAtt, delayRepeatsAtt, delayLowAtt, delayHighAtt;
    juce::ComboBox delayTimeBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> delayTimeAtt;
    juce::ToggleButton delayOnButton { "ON" };
    std::unique_ptr<APVTS::ButtonAttachment> delayOnAtt;

    // reverb on the sequenced sound, and the beat pattern panel below it
    KnobSlider reverbDecaySlider, reverbToneSlider, reverbMixSlider, reverbLevelSlider, reverbLowSlider, reverbHighSlider;
    std::unique_ptr<APVTS::SliderAttachment> reverbDecayAtt, reverbToneAtt, reverbMixAtt, reverbLevelAtt, reverbLowAtt, reverbHighAtt;
    juce::ComboBox reverbTypeBox;
    juce::ToggleButton reverbReverseButton { "REVERSE" }; // each type of reverb remembers its own tick
    juce::ToggleButton reverbOnButton { "ON" };
    std::unique_ptr<APVTS::ButtonAttachment> reverbOnAtt;

    // distortion on the sequenced sound: drive, tone and level, an on switch and a "reverb too" switch
    KnobSlider distDriveSlider, distToneSlider, distLevelSlider;
    std::unique_ptr<APVTS::SliderAttachment> distDriveAtt, distToneAtt, distLevelAtt;
    juce::ToggleButton distOnButton { "ON" }, distReverbButton { "REVERB TOO" };
    juce::ComboBox distTypeBox; // overdrive or fuzz
    std::unique_ptr<APVTS::ComboBoxAttachment> distTypeAtt;
    std::unique_ptr<APVTS::ButtonAttachment> distOnAtt, distReverbAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> reverbTypeAtt;
    std::unique_ptr<SamplePanel> samplePanel;
    std::unique_ptr<SamplerPage> samplerPage; // the SAMPLER, over the whole window when it is open
    std::unique_ptr<EqPanel> eqPanel;
    std::unique_ptr<TremoloPanel> tremoloPanel; // between the distortion and the delay
    // what the distortion, the delay and the reverb do, drawn in the left of their panels (the knobs share the rest)
    std::unique_ptr<DistortionGraph> distGraph;
    std::unique_ptr<DelayGraph> delayGraph;
    std::unique_ptr<ReverbGraph> reverbGraph;
    static constexpr int kGraphX = 10, kGraphW = 120, kKnobsX = 136; // in a panel
    std::unique_ptr<PatternPanel> patternPanel;
    std::unique_ptr<APVTS::ComboBoxAttachment> headroomAtt, stutterLenAtt;

    // footswitches and buttons
    FootSwitch bypassSwitch, tapSwitch;
    PedalButton altButton { "ALT" };
    PedalButton typePitchButton { "PITCH" }, typeFilterButton { "FILTER" }, typeRateButton { "RATE" }; // what the sequencer changes
    void showSeqType();

    // preset bar and expression morph strip (the panel above the pedal face)
    PresetName presetLabel;
    PedalButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" };
    PedalButton setUpButton { "STORE A" }, setDownButton { "STORE B" }; // the two sounds the morph goes between
    juce::Slider expressionSlider;
    std::unique_ptr<APVTS::SliderAttachment> expressionAtt;
    juce::ToggleButton morphButton { "MORPH" };
    std::unique_ptr<APVTS::ButtonAttachment> morphAtt;

    juce::ParameterAttachment bypassParam, holdParam, typeParam, tempoParam;
    TempoReadout tempoLabel; // the big BPM read-out: drag it to change the tempo, double-click it to type one
    bool tempoEditable = true;
    float dragStartBpm = 120.0f;

    int shownProgram = -1;
    juce::String shownName;
    double flashUpUntil = 0.0, flashDownUntil = 0.0, flashSaveUntil = 0.0;

    // tabs: the pedal and the DRUMS page
    PedalButton pedalTab { "PEDAL" }, drumsTab { "DRUMS" };
    std::unique_ptr<DrumPage> drumPage;
    std::vector<juce::Component*> pedalComponents; // hidden while the DRUMS page is shown
    bool drumsShown = false;

    juce::TooltipWindow tooltipWindow { this, 600 }; // shows setTooltip() text after a short hover
    int tooltipSeqType = -1;

    bool altLayer = false;
    bool tapDown = false, holdFromTap = false;
    double tapPressMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BitVibeEditor)
};
