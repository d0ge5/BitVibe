#include "DrumPage.h"

using namespace juce;
using namespace bitvibe;
using namespace pedalstyle;

namespace
{
// Page coordinates (the page sits below the editor's tab strip).
const Rectangle<int> kBeatPanel { 12, 6, 456, 130 }, kPlayPanel { 12, 144, 456, 88 }, kFeedPanel { 12, 240, 456, 280 },
    kMapPanel { 12, 528, 456, 296 };
const Rectangle<int> kDropZone { 24, 26, 432, 46 };
const Rectangle<int> kOverview { 84, 260, 372, 12 };
constexpr int kLaneX = 84, kLaneY = 278, kLaneW = 372, kLaneH = 40;
constexpr int kRowY = 564, kRowH = 50;
constexpr float kLevelFloorDb = -48.0f; // bottom of the lanes and of the Min level slider
constexpr double kScrollAhead = 3.0, kScrollBehind = 1.0, kShortLoop = 12.0;

const std::array<Colour, kNumDrums> kDrumColours { Colour(0xffff6a3d), Colour(0xffffc23d), Colour(0xff3dd6ff),
                                                   Colour(0xff8fb0ff), Colour(0xffd38cff) };

// Short action names for the lane labels.
const char* shortActionName(DrumAction a)
{
    static constexpr const char* names[kNumDrumActions] = { "off", "restart", "next step", "stutter", "restart + stutter" };
    return names[jlimit(0, kNumDrumActions - 1, (int) a)];
}

void drawPanel(Graphics& g, Rectangle<int> r, const String& caption)
{
    g.setColour(panel);
    g.fillRoundedRectangle(r.toFloat(), 10.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10.0f, 1.0f);
    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText(caption, r.getX() + 14, r.getY() + 4, 200, 12, Justification::left);
}

void drawLed(Graphics& g, Point<float> c, float r, Colour col, bool on)
{
    if (on)
    {
        g.setColour(col.withAlpha(0.25f));
        g.fillEllipse(c.x - r * 2.2f, c.y - r * 2.2f, r * 4.4f, r * 4.4f);
    }
    g.setColour(on ? col : col.darker(0.9f));
    g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour(Colours::black.withAlpha(0.75f));
    g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);
}
} // namespace

DrumPage::DrumPage(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel)
    : proc(p), beat(p.getDrumBeat()), laf(lookAndFeel),
      playParam(*p.apvts.getParameter(ids::beatPlay), [this](float) { refresh(); })
{
    setOpaque(true);

    for (auto* b : { &loadButton, &demoButton, &reanalyseButton, &playButton, &halfButton, &doubleButton, &useTempoButton })
        addAndMakeVisible(*b);
    loadButton.onClick = [this] { chooseFile(); };
    demoButton.onClick = [this] { beat.loadDemo(); };
    reanalyseButton.onClick = [this]
    {
        if (!model.loaded)
            return;
        AlertWindow::showOkCancelBox(MessageBoxIconType::QuestionIcon, "Re-analyse the beat?",
                                     "Your changes to the hits (excluded, moved or deleted hits) will be lost.", "Re-analyse",
                                     "Cancel", this,
                                     ModalCallbackFunction::create([this](int result) {
                                         if (result != 0)
                                             beat.reanalyse();
                                     }));
    };
    playButton.onClick = [this]
    {
        playParam.setValueAsCompleteGesture(proc.apvts.getRawParameterValue(ids::beatPlay)->load() > 0.5f ? 0.0f : 1.0f);
    };
    halfButton.onClick = [this] { beat.setTempoEstimate(model.tempoBpm / 2.0); };
    doubleButton.onClick = [this] { beat.setTempoEstimate(model.tempoBpm * 2.0); };
    useTempoButton.onClick = [this]
    {
        if (model.tempoBpm <= 0.0)
            return;
        if (auto* tempo = proc.apvts.getParameter(ids::tempo))
            tempo->setValueNotifyingHost(tempo->convertTo0to1((float) jlimit((double) kMinTempoBpm, (double) kMaxTempoBpm, model.tempoBpm)));
    };

    library.setLookAndFeel(&laf);
    library.setTextWhenNothingSelected("Beat library");
    library.onChange = [this]
    {
        const int index = library.getSelectedId() - 1;
        if (isPositiveAndBelow(index, libraryList.size()))
            beat.loadFile(libraryList[index]);
    };
    addAndMakeVisible(library);

    for (auto* b : { &neuralButton, &pickButton })
    {
        b->setLookAndFeel(&laf);
        addAndMakeVisible(*b);
    }
    neuralButton.onClick = [this]
    {
        beat.setAnalysisMode(neuralButton.getToggleState() ? DrumBeat::AnalysisMode::Auto : DrumBeat::AnalysisMode::BuiltInOnly);
    };
    pickAtt = std::make_unique<APVTS::ButtonAttachment>(proc.apvts, ids::pickTriggers, pickButton);

    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(ids::stepClock)))
        stepClockBox.addItemList(choice->choices, 1);
    stepClockBox.setLookAndFeel(&laf);
    addAndMakeVisible(stepClockBox);
    stepClockAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::stepClock, stepClockBox);

    levelKnob.setSliderStyle(Slider::RotaryVerticalDrag);
    levelKnob.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
    levelKnob.setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    levelKnob.setMouseDragSensitivity(180);
    levelKnob.setLookAndFeel(&laf);
    levelAtt = std::make_unique<APVTS::SliderAttachment>(proc.apvts, ids::beatLevel, levelKnob);
    addAndMakeVisible(levelKnob);

    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(ids::beatOutput)))
        beatOutBox.addItemList(choice->choices, 1);
    beatOutBox.setLookAndFeel(&laf);
    addAndMakeVisible(beatOutBox);
    beatOutAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::beatOutput, beatOutBox);

    for (int d = 0; d < kNumDrums; ++d)
    {
        auto& r = rows[(size_t) d];
        for (int a = 0; a < kNumDrumActions; ++a)
            r.action.addItem(drumActionName((DrumAction) a), a + 1);
        r.action.setLookAndFeel(&laf);
        r.action.onChange = [this, d] { applySettings(d); };
        addAndMakeVisible(r.action);

        for (auto* s : { &r.minLevel, &r.detect })
        {
            s->setSliderStyle(Slider::LinearHorizontal);
            s->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
            s->setLookAndFeel(&laf);
            s->onValueChange = [this, d] { applySettings(d); };
            addAndMakeVisible(*s);
        }
        r.minLevel.setRange(kLevelFloorDb, 0.0, 1.0);
        r.minLevel.setDoubleClickReturnValue(true, -12.0);
        r.detect.setRange(0.0, 1.0, 0.01);
        r.detect.setDoubleClickReturnValue(true, 0.3);
    }

    setTooltips();
    refreshLibrary();
    refresh();
    startTimerHz(30);
}

DrumPage::~DrumPage()
{
    stopTimer();
    for (auto& r : rows)
    {
        r.action.setLookAndFeel(nullptr);
        r.minLevel.setLookAndFeel(nullptr);
        r.detect.setLookAndFeel(nullptr);
    }
    for (Component* c : std::initializer_list<Component*> { &library, &neuralButton, &pickButton, &stepClockBox, &levelKnob, &beatOutBox })
        c->setLookAndFeel(nullptr);
}

void DrumPage::setTooltips()
{
    loadButton.setTooltip("LOAD: open a drum beat (WAV, MP3, AIFF, FLAC or OGG). You can also drag a file onto the box above.");
    demoButton.setTooltip("DEMO: load the built-in demo beat, four bars of a synthetic rock kit at 100 BPM.");
    reanalyseButton.setTooltip("RE-ANALYSE: find the hits again, for example after installing the neural helper. "
                               "Your edits to the hits are lost.");
    library.setTooltip("LIBRARY: the beats in Documents/BitVibe/Beats (and in the beats folder of the source code).");
    neuralButton.setTooltip("NEURAL HELPER: analyse new beats with the neural drum transcriber (ADTOF) when it is installed. "
                            "More accurate, especially for cymbals. Off = always the built-in analysis.");
    playButton.setTooltip("PLAY: start or stop the beat. It loops from the top at its own speed, independent of your DAW.");
    levelKnob.setTooltip("BEAT LEVEL: how loud you hear the beat (it never goes through the effect). Fully down = silent, "
                         "but its hits still trigger.");
    beatOutBox.setTooltip("BEAT OUT: where you hear the beat. Auto = on the separate Beat Monitor output when your DAW has switched it on, "
                          "so the beat stays out of the main output and everything after it; otherwise in the main output. "
                          "Monitor = only on that output. Main = always in the main output.");
    pickButton.setTooltip("PICK TRIGGERS: picked notes on your guitar still start the stutter and the sequence. "
                          "Off = only the drum hits trigger them.");
    stepClockBox.setTooltip("STEPS MOVE WITH: Tempo = the steps move with the tempo. Drum hits = only hits set to "
                            "\"Next step\" move them.");
    halfButton.setTooltip("/2: halve the detected beat tempo (if it was detected twice too fast).");
    doubleButton.setTooltip("x2: double the detected beat tempo (if it was detected half as fast).");
    useTempoButton.setTooltip("USE: set BitVibe's tempo (sequencer steps, stutter length) to the beat's tempo.");
    for (int d = 0; d < kNumDrums; ++d)
    {
        const String name = drumName((Drum) d), lower = name.toLowerCase(), upper = name.toUpperCase();
        rows[(size_t) d].action.setTooltip(upper + ": what a " + lower + " hit does: restart the sequence, move it one step, "
                                           "fire the stutter, or both like a pick.");
        rows[(size_t) d].minLevel.setTooltip(upper + " MIN LEVEL: only hits at least this loud trigger, in dB below the loudest "
                                             + lower + " of the beat. Double-click = -12 dB.");
        rows[(size_t) d].detect.setTooltip(upper + " DETECT: how sure the analysis must be that a hit is a " + lower
                                           + ". Lower finds more, and more mistakes. Double-click = 0.30.");
    }
}

// ---- data ---------------------------------------------------------------------------------------

void DrumPage::visibilityChanged()
{
    if (isVisible())
    {
        refreshLibrary();
        refresh();
    }
}

void DrumPage::timerCallback()
{
    if (isVisible())
        refresh();
}

void DrumPage::refresh()
{
    if (beat.getGeneration() != model.generation || !model.loaded)
    {
        model = beat.getModel();
        viewStart = jlimit(0.0, jmax(0.0, model.durationSeconds - kScrollAhead), viewStart);
    }
    status = beat.getStatus();

    const bool playing = proc.apvts.getRawParameterValue(ids::beatPlay)->load() > 0.5f;
    if (playButton.lit != playing)
    {
        playButton.lit = playing;
        playButton.setText(playing ? "STOP" : "PLAY");
    }
    neuralButton.setToggleState(beat.getAnalysisMode() == DrumBeat::AnalysisMode::Auto, dontSendNotification);
    reanalyseButton.setEnabled(model.loaded && model.audioAvailable && !beat.isBusy());

    const auto settings = beat.getSettings();
    if (settings != shownSettings)
        showSettings(settings);
    repaint();
}

void DrumPage::showSettings(const DrumSettingsArray& settings)
{
    const ScopedValueSetter<bool> guard(updatingControls, true);
    for (int d = 0; d < kNumDrums; ++d)
    {
        const auto& s = settings[(size_t) d];
        auto& r = rows[(size_t) d];
        r.action.setSelectedId((int) s.action + 1, dontSendNotification);
        if (!r.minLevel.isMouseButtonDown())
            r.minLevel.setValue(jmax(kLevelFloorDb, s.minLevelDb), dontSendNotification);
        if (!r.detect.isMouseButtonDown())
            r.detect.setValue(s.minConfidence, dontSendNotification);
        r.minLevel.getProperties().set("active", s.action != DrumAction::Off);
        r.detect.getProperties().set("active", s.action != DrumAction::Off);
    }
    shownSettings = settings;
}

void DrumPage::applySettings(int d)
{
    if (updatingControls)
        return;
    const auto& r = rows[(size_t) d];
    DrumSettings s;
    s.action = (DrumAction) jlimit(0, kNumDrumActions - 1, r.action.getSelectedId() - 1);
    s.minLevelDb = (float) r.minLevel.getValue();
    s.minConfidence = (float) r.detect.getValue();
    beat.setSettings((Drum) d, s);
    refresh();
}

void DrumPage::refreshLibrary()
{
    const auto files = DrumBeat::libraryFiles();
    if (files == libraryList && library.getNumItems() == files.size())
        return;
    libraryList = files;
    library.clear(dontSendNotification);
    for (int i = 0; i < libraryList.size(); ++i)
        library.addItem(libraryList[i].getFileName(), i + 1);
    library.setTextWhenNothingSelected(libraryList.isEmpty() ? "Library is empty" : "Beat library (" + String(libraryList.size()) + ")");
}

void DrumPage::chooseFile()
{
    chooser = std::make_unique<FileChooser>("Open a drum beat", DrumBeat::libraryFolders()[0],
                                            "*.wav;*.wave;*.mp3;*.aif;*.aiff;*.flac;*.ogg;*.m4a;*.caf");
    chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                         [this](const FileChooser& fc) {
                             const auto file = fc.getResult();
                             if (file.existsAsFile())
                                 beat.loadFile(file);
                         });
}

// ---- drag and drop -------------------------------------------------------------------------------

bool DrumPage::isInterestedInFileDrag(const StringArray& files)
{
    for (const auto& f : files)
        if (DrumBeat::isSupportedAudioFile(File(f)))
            return true;
    return false;
}

void DrumPage::fileDragEnter(const StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void DrumPage::fileDragExit(const StringArray&)
{
    dragOver = false;
    repaint();
}

void DrumPage::filesDropped(const StringArray& files, int, int)
{
    dragOver = false;
    for (const auto& f : files)
        if (DrumBeat::isSupportedAudioFile(File(f)))
        {
            beat.loadFile(File(f));
            break;
        }
    repaint();
}

// ---- the feed ------------------------------------------------------------------------------------

Rectangle<float> DrumPage::laneBounds(int drum) const
{
    return Rectangle<float>((float) kLaneX, (float) (kLaneY + drum * kLaneH), (float) kLaneW, (float) kLaneH);
}

Range<double> DrumPage::viewRange() const
{
    const double length = jmax(0.1, model.durationSeconds);
    if (length <= kShortLoop)
        return { 0.0, length };
    if (beat.isPlaying() && beat.getPlayingGeneration() == model.generation)
    {
        const double start = jmax(0.0, beat.getPositionSeconds() - kScrollBehind);
        return { start, start + kScrollBehind + kScrollAhead };
    }
    return { viewStart, viewStart + kScrollBehind + kScrollAhead };
}

float DrumPage::timeToX(double seconds) const
{
    const auto view = viewRange();
    return (float) kLaneX + (float) ((seconds - view.getStart()) / view.getLength()) * (float) kLaneW;
}

bool DrumPage::firedRecently(const DrumHit& h) const
{
    if (!beat.isPlaying() || beat.getPlayingGeneration() != model.generation || model.durationSeconds <= 0.0)
        return false;
    double since = beat.getPositionSeconds() - h.time;
    if (since < 0.0)
        since += model.durationSeconds;
    return since < 0.12;
}

int DrumPage::hitAt(Point<float> p) const
{
    for (int d = 0; d < kNumDrums; ++d)
    {
        if (!laneBounds(d).contains(p))
            continue;
        int best = -1;
        float bestDistance = 6.0f;
        for (int i = 0; i < (int) model.hits.size(); ++i)
        {
            const auto& h = model.hits[(size_t) i];
            const float distance = std::abs(timeToX(h.time) - p.x);
            if ((int) h.drum == d && distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }
    return -1;
}

void DrumPage::mouseDown(const MouseEvent& e)
{
    const auto p = e.position;
    if (kOverview.toFloat().expanded(0.0f, 4.0f).contains(p) && model.durationSeconds > kShortLoop)
    {
        const double t = (p.x - (float) kOverview.getX()) / (float) kOverview.getWidth() * model.durationSeconds;
        viewStart = jlimit(0.0, jmax(0.0, model.durationSeconds - kScrollAhead - kScrollBehind), t - kScrollBehind);
        repaint();
        return;
    }
    const int index = hitAt(p);
    if (index >= 0)
        editHit(index, e.mods.isPopupMenu());
}

void DrumPage::editHit(int index, bool menu)
{
    const auto hit = model.hits[(size_t) index];
    if (!menu)
    {
        beat.setHitExcluded(index, !hit.excluded);
        refresh();
        return;
    }
    PopupMenu m;
    m.addSectionHeader(String(drumName(hit.drum)) + " at " + String(hit.time, 3) + " s (" + String(roundToInt(hit.levelDb))
                       + " dB, confidence " + String(hit.confidence, 2) + ")");
    m.addItem(1, hit.excluded ? "Include this hit" : "Exclude this hit (it never triggers)");
    PopupMenu move;
    for (int d = 0; d < kNumDrums; ++d)
        move.addItem(10 + d, drumName((Drum) d), (int) hit.drum != d);
    m.addSubMenu("It is really a...", move);
    m.addItem(2, "Delete this hit");
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                    [this, index, hit](int result) {
                        if (result == 1)
                            beat.setHitExcluded(index, !hit.excluded);
                        else if (result == 2)
                            beat.deleteHit(index);
                        else if (result >= 10 && result < 10 + kNumDrums)
                            beat.setHitDrum(index, (Drum) (result - 10));
                        refresh();
                    });
}

// ---- layout and painting ---------------------------------------------------------------------------

void DrumPage::resized()
{
    loadButton.setBounds(24, 78, 72, 24);
    library.setBounds(102, 78, 206, 24);
    demoButton.setBounds(314, 78, 60, 24);
    reanalyseButton.setBounds(380, 78, 76, 24);
    neuralButton.setBounds(24, 108, 128, 22);

    playButton.setBounds(24, 158, 78, 28);
    beatOutBox.setBounds(24, 203, 78, 22);
    levelKnob.setBounds(112, 156, 52, 52);
    pickButton.setBounds(178, 160, 140, 22);
    stepClockBox.setBounds(178, 200, 140, 24);
    halfButton.setBounds(330, 200, 36, 24);
    doubleButton.setBounds(370, 200, 36, 24);
    useTempoButton.setBounds(410, 200, 46, 24);

    for (int d = 0; d < kNumDrums; ++d)
    {
        const int y = kRowY + d * kRowH;
        auto& r = rows[(size_t) d];
        r.action.setBounds(90, y + 8, 138, 24);
        r.minLevel.setBounds(236, y + 4, 104, 24);
        r.detect.setBounds(348, y + 4, 66, 24);
    }
}

void DrumPage::paint(Graphics& g)
{
    g.fillAll(Colour(0xff0d0d0f));
    drawPanel(g, kBeatPanel, "BEAT");
    drawPanel(g, kPlayPanel, "PLAYBACK");
    drawPanel(g, kFeedPanel, "LIVE FEED");
    drawPanel(g, kMapPanel, "DRUM MAP");

    // Drop zone: the beat's name and what is happening.
    const auto zone = kDropZone.toFloat();
    g.setColour(dragOver ? accent.withAlpha(0.18f) : Colour(0xff0e0e10));
    g.fillRoundedRectangle(zone, 6.0f);
    g.setColour(dragOver ? accent : Colour(0xff3c3c42));
    const float dash[] { 5.0f, 4.0f };
    Path outline;
    outline.addRoundedRectangle(zone.reduced(0.5f), 6.0f);
    PathStrokeType(1.0f).createDashedStroke(outline, outline, dash, 2);
    g.fillPath(outline);
    g.setColour(model.loaded ? ink : inkDim);
    g.setFont(bold(13.5f));
    const String title = model.loaded ? model.name : String("Drop a drum beat here");
    g.drawText(title, kDropZone.reduced(12, 0).withHeight(24).translated(0, 2), Justification::centredLeft, true);
    g.setColour(beat.isBusy() ? accent : inkDim);
    g.setFont(plain(10.5f));
    g.drawText(status, kDropZone.reduced(12, 0).withTrimmedTop(24).withHeight(18), Justification::centredLeft, true);

    g.setColour(inkDim);
    g.setFont(plain(10.0f));
    const String neural = DrumBeat::isNeuralHelperInstalled()
                              ? "Installed: new beats are analysed with ADTOF (most accurate)."
                              : "Not installed: using the built-in analysis (see the README).";
    g.drawText(neural, 160, 108, 296, 22, Justification::centredLeft, true);

    // Playback panel captions and readouts.
    const float level = proc.apvts.getRawParameterValue(ids::beatLevel)->load();
    g.setColour(ink);
    g.setFont(bold(10.0f));
    g.drawText("BEAT LEVEL", 104, 206, 68, 12, Justification::centred);
    g.drawText("BEAT OUT", 24, 190, 78, 11, Justification::centred);
    g.setColour(inkDim);
    g.setFont(plain(10.0f));
    g.drawText(level <= DrumBeat::kBeatOffDb ? String("OFF") : String(level, 1) + " dB", 104, 217, 68, 12, Justification::centred);
    g.setFont(bold(9.5f));
    g.drawText("STEPS MOVE WITH", 178, 186, 140, 12, Justification::centredLeft);
    g.drawText("BEAT TEMPO", 330, 160, 126, 12, Justification::centredLeft);
    g.setColour(model.tempoBpm > 0.0 ? ink : inkDim);
    g.setFont(bold(17.0f));
    g.drawText(model.tempoBpm > 0.0 ? String(model.tempoBpm, 1) + " BPM" : String("-"), 330, 174, 126, 22, Justification::centredLeft);

    // Where the beat's sound goes right now (the BEAT OUT setting and whether a monitor output is connected).
    {
        const auto route = proc.getBeatRoute();
        const bool automatic = roundToInt(proc.apvts.getRawParameterValue(ids::beatOutput)->load()) == (int) BeatOutput::Auto;
        String text;
        Colour colour = inkDim;
        if (route == BeatRoute::Monitor)
        {
            text = "Beat sound: Beat Monitor output only, not in the main output";
            colour = accent;
        }
        else if (route == BeatRoute::Main)
        {
            text = automatic ? "Beat sound: main output (no Beat Monitor output connected)" : "Beat sound: main output";
        }
        else
        {
            text = "Beat sound: silent (the Beat Monitor output is not connected)";
            colour = accent;
        }
        g.setColour(colour);
        g.setFont(plain(10.0f));
        g.drawText(text, kPlayPanel.getX() + 96, kPlayPanel.getY() + 4, kPlayPanel.getWidth() - 110, 12, Justification::centredRight, true);
    }

    paintFeed(g);
    paintRows(g);
}

void DrumPage::paintFeed(Graphics& g)
{
    const auto settings = beat.getSettings();
    const auto view = viewRange();
    const bool playingThis = beat.isPlaying() && beat.getPlayingGeneration() == model.generation;

    // Overview of the whole loop with the visible window.
    const auto ov = kOverview.toFloat();
    g.setColour(Colour(0xff0e0e10));
    g.fillRoundedRectangle(ov, 3.0f);
    if (model.durationSeconds > 0.0)
    {
        for (const auto& h : model.hits)
            if (hitFires(h, settings[(size_t) h.drum]))
            {
                const float x = ov.getX() + (float) (h.time / model.durationSeconds) * ov.getWidth();
                g.setColour(kDrumColours[(size_t) h.drum].withAlpha(0.8f));
                g.fillRect(x, ov.getY() + 2.0f, 1.0f, ov.getHeight() - 4.0f);
            }
        if (model.durationSeconds > kShortLoop)
        {
            const float x0 = ov.getX() + (float) (view.getStart() / model.durationSeconds) * ov.getWidth();
            const float x1 = ov.getX() + (float) (view.getEnd() / model.durationSeconds) * ov.getWidth();
            g.setColour(ink.withAlpha(0.5f));
            g.drawRect(Rectangle<float>(x0, ov.getY(), jmax(2.0f, x1 - x0), ov.getHeight()), 1.0f);
        }
        if (playingThis)
        {
            g.setColour(accent);
            g.fillRect(ov.getX() + (float) (beat.getPositionSeconds() / model.durationSeconds) * ov.getWidth() - 1.0f, ov.getY(),
                       2.0f, ov.getHeight());
        }
    }

    // Lanes.
    for (int d = 0; d < kNumDrums; ++d)
    {
        const auto lane = laneBounds(d);
        const auto& s = settings[(size_t) d];
        const Colour colour = kDrumColours[(size_t) d];
        g.setColour(Colour(d % 2 == 0 ? 0xff121215 : 0xff101012));
        g.fillRect(lane);

        g.setColour(colour);
        g.setFont(bold(11.0f));
        g.drawText(drumName((Drum) d), 22, (int) lane.getY(), 58, (int) lane.getHeight() - 14, Justification::centredLeft);
        g.setColour(inkDim);
        g.setFont(plain(9.0f));
        g.drawFittedText(shortActionName(s.action), 22, (int) lane.getBottom() - 18, 58, 14, Justification::centredLeft, 1, 0.8f);

        const float barBottom = lane.getBottom() - 4.0f, barRange = lane.getHeight() - 10.0f;
        auto levelY = [&](float db) { return barBottom - barRange * jlimit(0.0f, 1.0f, (db - kLevelFloorDb) / -kLevelFloorDb); };

        if (s.action != DrumAction::Off)
        {
            g.setColour(colour.withAlpha(0.35f));
            const float y = levelY(s.minLevelDb);
            const float dashes[] { 3.0f, 3.0f };
            g.drawDashedLine(Line<float>(lane.getX(), y, lane.getRight(), y), dashes, 2, 1.0f);
        }

        Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(lane.toNearestInt());
        for (const auto& h : model.hits)
        {
            if ((int) h.drum != d || h.time < view.getStart() - 0.05 || h.time > view.getEnd() + 0.05)
                continue;
            const float x = timeToX(h.time);
            const float top = levelY(h.levelDb);
            if (!hitDetected(h, s))
            {
                g.setColour(inkDim.withAlpha(0.18f));
                g.fillRect(x - 0.5f, top, 1.0f, barBottom - top);
                continue;
            }
            if (h.excluded)
            {
                g.setColour(inkDim.withAlpha(0.6f));
                g.fillRect(x - 1.0f, top, 2.0f, barBottom - top);
                g.drawLine(x - 3.0f, top - 3.0f, x + 3.0f, top + 3.0f, 1.2f);
                g.drawLine(x - 3.0f, top + 3.0f, x + 3.0f, top - 3.0f, 1.2f);
                continue;
            }
            const bool fires = hitFires(h, s);
            g.setColour(fires ? colour : colour.withAlpha(0.3f));
            g.fillRect(x - 1.5f, top, 3.0f, barBottom - top);
            if (fires && firedRecently(h))
            {
                g.setColour(colour.withAlpha(0.35f));
                g.fillEllipse(x - 7.0f, top - 7.0f, 14.0f, 14.0f);
                g.setColour(colour.brighter(0.6f));
                g.fillEllipse(x - 3.0f, top - 3.0f, 6.0f, 6.0f);
            }
        }
    }

    // Time ruler and playhead.
    const float lanesBottom = (float) (kLaneY + kNumDrums * kLaneH);
    g.setColour(inkDim);
    g.setFont(plain(9.0f));
    const double span = view.getLength();
    const double tick = span <= 4.5 ? 0.5 : span <= 9.0 ? 1.0 : 2.0;
    for (double t = std::ceil(view.getStart() / tick) * tick; t <= view.getEnd() + 1e-9; t += tick)
    {
        const float x = timeToX(t);
        g.fillRect(x, lanesBottom, 1.0f, 4.0f);
        g.drawText(String(t, tick < 1.0 ? 1 : 0) + "s", (int) x - 20, (int) lanesBottom + 3, 40, 10, Justification::centred);
    }
    if (playingThis)
    {
        const float x = timeToX(beat.getPositionSeconds());
        g.setColour(accent);
        g.fillRect(x - 0.75f, (float) kLaneY, 1.5f, lanesBottom - (float) kLaneY);
    }
    if (!model.loaded)
    {
        g.setColour(inkDim);
        g.setFont(plain(11.0f));
        g.drawText("No beat loaded", kLaneX, kLaneY, kLaneW, kNumDrums * kLaneH, Justification::centred);
    }

    // The sequencer's steps, so you can see the hits drive it.
    const int current = proc.getEngine().getCurrentStep();
    g.setColour(inkDim);
    g.setFont(bold(9.5f));
    g.drawText("SEQUENCER", 22, 498, 62, 14, Justification::centredLeft);
    for (int i = 0; i < kNumSteps; ++i)
        drawLed(g, { (float) (kLaneX + 8 + i * 22), 505.0f }, 4.5f, accent, current == i);
    g.setFont(plain(9.5f));
    const bool pickOn = proc.apvts.getRawParameterValue(ids::pickTriggers)->load() > 0.5f;
    g.drawText(pickOn ? "picks on your guitar also trigger" : "only the drum hits trigger", kLaneX + 140, 498, 232, 14,
               Justification::centredRight);
}

void DrumPage::paintRows(Graphics& g)
{
    const auto settings = beat.getSettings();
    g.setColour(inkDim);
    g.setFont(bold(9.5f));
    g.drawText("DRUM", 24, 548, 60, 12, Justification::centredLeft);
    g.drawText("WHEN IT HITS", 90, 548, 138, 12, Justification::centredLeft);
    g.drawText("MIN LEVEL", 236, 548, 104, 12, Justification::centredLeft);
    g.drawText("DETECT", 348, 548, 66, 12, Justification::centredLeft);
    g.drawText("FIRES", 416, 548, 42, 12, Justification::centredRight);

    for (int d = 0; d < kNumDrums; ++d)
    {
        const int y = kRowY + d * kRowH;
        const auto& s = settings[(size_t) d];
        const Colour colour = kDrumColours[(size_t) d];

        int total = 0, firing = 0;
        bool lit = false;
        for (const auto& h : model.hits)
            if (h.drum == (Drum) d && hitDetected(h, s) && !h.excluded)
            {
                ++total;
                if (hitFires(h, s))
                {
                    ++firing;
                    lit = lit || firedRecently(h);
                }
            }

        g.setColour(Colour(0xff26262b));
        g.fillRect(24, y + kRowH - 2, 432, 1);
        drawLed(g, { 30.0f, (float) y + 20.0f }, 4.5f, colour, lit);
        g.setColour(colour);
        g.setFont(bold(12.0f));
        g.drawText(drumName((Drum) d), 40, y + 12, 50, 16, Justification::centredLeft);

        g.setColour(s.action == DrumAction::Off ? inkDim.withAlpha(0.5f) : inkDim);
        g.setFont(plain(9.5f));
        g.drawText(String(roundToInt(s.minLevelDb)) + " dB", 236, y + 29, 104, 12, Justification::centred);
        g.drawText(String(s.minConfidence, 2), 348, y + 29, 66, 12, Justification::centred);
        g.setColour(s.action == DrumAction::Off ? inkDim.withAlpha(0.5f) : ink);
        g.setFont(bold(11.0f));
        g.drawText(model.loaded ? String(firing) + "/" + String(total) : String("-"), 414, y + 12, 44, 16, Justification::centredRight);
    }
}
