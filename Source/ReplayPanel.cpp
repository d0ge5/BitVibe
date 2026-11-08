#include "ReplayPanel.h"

using namespace juce;
using namespace bitvibe;
using namespace pedalstyle;

namespace
{
constexpr int kKnobY = 50, kLabelY = 74, kKnobSize = 40;
constexpr std::array<int, 5> kKnobX { 52, 140, 228, 316, 404 };
constexpr int kStatusX = 150; // the status text and the loop's playhead, right of the title

float value(BitVibeProcessor& p, const char* id) { return p.apvts.getRawParameterValue(id)->load(); }

String secondsText(float v) { return v < 1.0f ? String(roundToInt(v * 1000.0f)) + " ms" : String(v, v < 10.0f ? 2 : 1) + " s"; }
} // namespace

ReplayPanel::ReplayPanel(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel) : proc(p), laf(lookAndFeel)
{
    setSize(kWidth, kCollapsedHeight);
    onButton.setLookAndFeel(&laf);
    makePowerButton(onButton);
    onAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::replayOn, onButton);
    onButton.onClick = [this] { refresh(); };
    onButton.setTooltip("REPLAY ON: while you play you hear only your clean guitar. Stop, and what you just played loops through "
                        "the sequence and the effects until you play again. MIX and HEAR have no say while it is on.");
    addAndMakeVisible(onButton);

    struct Knob
    {
        KnobSlider* slider;
        std::unique_ptr<APVTS::SliderAttachment>* att;
        const char* id;
        const char* tip;
    };
    for (const auto& k : { Knob { &backSlider, &backAtt, ids::replayBack,
                                  "LOOK BACK: how much of what you just played is looped, counting back from your last note (never "
                                  "further back than the start of the phrase)." },
                           Knob { &waitSlider, &waitAtt, ids::replayWait,
                                  "WAIT: how long you have to stop for before the loop starts. Longer lets you pause between notes "
                                  "without starting it." },
                           Knob { &lengthSlider, &lengthAtt, ids::replayLength,
                                  "LENGTH: how long the loop plays before it fades away. All the way right (FOREVER) it plays until you "
                                  "play again; playing always cuts it." },
                           Knob { &drySlider, &dryAtt, ids::replayDry,
                                  "DRY: how much of the loop you hear as you played it, against the loop through the sequence and "
                                  "effects. Left = only the sequenced loop." },
                           Knob { &thresholdSlider, &thresholdAtt, ids::replayThreshold,
                                  "THRESHOLD: how loud the guitar has to be to count as playing. Lower it if the loop starts while you "
                                  "still play softly; raise it if hum or noise keeps cutting the loop." } })
    {
        auto& s = *k.slider;
        s.setSliderStyle(Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        s.setMouseDragSensitivity(180);
        s.setLookAndFeel(&laf);
        *k.att = std::make_unique<APVTS::SliderAttachment>(proc.apvts, k.id, s);
        auto& prm = *proc.apvts.getParameter(k.id);
        s.setDoubleClickReturnValue(true, (double) prm.convertFrom0to1(prm.getDefaultValue()));
        s.setTooltip(k.tip);
        s.onValueChange = [this] { repaint(); };
        addChildComponent(s);
    }
    refresh();
}

ReplayPanel::~ReplayPanel()
{
    for (Component* c : std::initializer_list<Component*> { &onButton, &backSlider, &waitSlider, &lengthSlider, &drySlider, &thresholdSlider })
        c->setLookAndFeel(nullptr);
}

bool ReplayPanel::isOn() const { return proc.apvts.getRawParameterValue(ids::replayOn)->load() > 0.5f; }

void ReplayPanel::resized()
{
    onButton.setBounds(8, 3, 50, 19);
    int i = 0;
    for (auto* s : { &backSlider, &waitSlider, &lengthSlider, &drySlider, &thresholdSlider })
        s->setBounds(Rectangle<int>(kKnobSize, kKnobSize).withCentre({ kKnobX[(size_t) i++], kKnobY }));
}

void ReplayPanel::applyCollapse()
{
    const bool open = isOn();
    for (Component* c : std::initializer_list<Component*> { &backSlider, &waitSlider, &lengthSlider, &drySlider, &thresholdSlider })
        c->setVisible(open);
    if (getHeight() != currentHeight())
    {
        setSize(kWidth, currentHeight());
        if (onHeightChange)
            onHeightChange();
    }
}

String ReplayPanel::statusText() const
{
    if (!isOn())
        return "Off: loops your last phrase when you stop playing";
    const auto& engine = proc.getEngine();
    switch (engine.getReplayState())
    {
        case ReplayLooper::State::Live:
        {
            // stopped for a moment: count down to the loop
            const float wait = value(proc, ids::replayWait), waited = engine.getReplayPosition();
            if (waited > 0.15f)
                return "Stopped: the loop starts in " + secondsText(jmax(0.0f, wait * (1.0f - waited)));
            return "Playing: you hear your clean guitar";
        }
        case ReplayLooper::State::Looping:
        {
            String text = "Looping " + secondsText(engine.getReplayLoopSeconds()) + ": play to stop it";
            if (roundToInt(value(proc, ids::seqPlay)) == 0)
                text = "Looping, no sequence: turn up SEQUENCER";
            return text;
        }
        case ReplayLooper::State::Off:
        case ReplayLooper::State::Idle:
        default: return "Listening: play a phrase, then stop";
    }
}

void ReplayPanel::refresh()
{
    applyCollapse();
    const bool looping = isOn() && proc.getEngine().getReplayState() == ReplayLooper::State::Looping;
    setPowerActive(onButton, looping);
    const auto status = statusText();
    const float position = looping ? proc.getEngine().getReplayPosition() : -1.0f;
    if (status != shownStatus || std::abs(position - shownPosition) > 0.002f)
    {
        shownStatus = status;
        shownPosition = position;
        repaint();
    }
}

void ReplayPanel::paint(Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    panelFrame(g, bounds);
    drawSectionTitle(g, kPanelTitleX, 3, "REPLAY");

    const bool on = isOn();
    const bool alert = shownStatus.contains("turn up");
    g.setColour(alert ? accent : inkDim);
    g.setFont(plain(10.0f));
    g.drawText(shownStatus, kStatusX, 4, kWidth - kStatusX - 12, 12, Justification::centredRight, true);
    if (!on)
        return; // folded: the title row only

    // the loop's playhead, under the status: how far through the loop it is
    const Rectangle<float> track((float) kStatusX + 140.0f, 20.0f, (float) (kWidth - kStatusX - 152), 3.0f);
    g.setColour(theme.well);
    g.fillRoundedRectangle(track, 1.5f);
    if (shownPosition >= 0.0f)
    {
        g.setColour(accent);
        g.fillRoundedRectangle(track.withWidth(jmax(3.0f, track.getWidth() * shownPosition)), 1.5f);
    }

    const float length = value(proc, ids::replayLength);
    drawControlLabel(g, kKnobX[0], kLabelY, "LOOK BACK", secondsText(value(proc, ids::replayBack)));
    drawControlLabel(g, kKnobX[1], kLabelY, "WAIT", secondsText(value(proc, ids::replayWait)));
    drawControlLabel(g, kKnobX[2], kLabelY, "LENGTH", length >= kReplayForever - 0.005f ? String("FOREVER") : secondsText(length));
    drawControlLabel(g, kKnobX[3], kLabelY, "DRY", String(roundToInt(value(proc, ids::replayDry) * 100.0f)) + "%");
    drawControlLabel(g, kKnobX[4], kLabelY, "THRESHOLD", String(roundToInt(value(proc, ids::replayThreshold))) + " dB");
}
