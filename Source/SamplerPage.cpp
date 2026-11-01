#include "SamplerPage.h"

using namespace juce;
using namespace pedalstyle;

namespace
{
constexpr int kPageW = 948;
const Rectangle<int> kRecordBox { 12, 44, 924, 66 }, kPickBox { 12, 120, 924, 252 }, kShapeBox { 12, 382, 456, 196 },
    kSaveBox { 12, 588, 924, 76 };
constexpr int kEqX = 480, kEqY = 382;
constexpr double kMaxSoundSeconds = 20.0; // the sample blend uses at most this much of a sound

void drawBox(Graphics& g, Rectangle<int> r, int number, const String& title)
{
    g.setColour(panel);
    g.fillRoundedRectangle(r.toFloat(), 10.0f);
    g.setColour(panelEdge);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10.0f, 1.0f);
    drawPanelTitle(g, r.getX() + 12, r.getY() + 5, number, title);
}
} // namespace

// ------------------------------------------------------------------------------------------
// The waveform: the recording (or the zoomed part of it), the picked part shaded, its edges draggable, and where PLAY is.

class SamplerPage::Waveform : public Component, public SettableTooltipClient
{
public:
    explicit Waveform(SamplerPage& p) : page(p) { setMouseCursor(MouseCursor::IBeamCursor); }

    int64 viewStart = 0, viewEnd = 0;

    void showAll()
    {
        viewStart = 0;
        viewEnd = (int64) page.recL.size();
        repaint();
    }

    void zoomTo(Range<int64> r)
    {
        const auto pad = jmax<int64>(1, r.getLength() / 10);
        viewStart = jmax<int64>(0, r.getStart() - pad);
        viewEnd = jmin<int64>((int64) page.recL.size(), r.getEnd() + pad);
        repaint();
    }

    void paint(Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour(Colour(0xff0e0e10));
        g.fillRoundedRectangle(b, 4.0f);
        const auto total = (int64) page.recL.size();
        if (total == 0 || viewEnd <= viewStart)
        {
            g.setColour(inkDim);
            g.setFont(plain(12.0f));
            g.drawText("Pick a program above and press RECORD. The recording shows here; then drag across it to pick the part you want.",
                       getLocalBounds().reduced(20), Justification::centred);
            return;
        }
        // the picked part
        const float x0 = xFor(page.selStart), x1 = xFor(page.selEnd);
        g.setColour(accent.withAlpha(0.16f));
        g.fillRect(Rectangle<float>(x0, b.getY(), jmax(1.0f, x1 - x0), b.getHeight()));
        // the sound: the loudest and quietest sample under each pixel, both channels together
        const float mid = b.getCentreY(), half = b.getHeight() * 0.46f;
        for (int px = 0; px < getWidth(); ++px)
        {
            const auto a = sampleAt((float) px), z = jmax(a + 1, sampleAt((float) px + 1.0f));
            float lo = 0.0f, hi = 0.0f;
            const auto step = jmax<int64>(1, (z - a) / 64); // long views: a sparse look is enough to draw
            for (int64 i = a; i < z && i < total; i += step)
            {
                const float v = 0.5f * (page.recL[(size_t) i] + page.recR[(size_t) i]);
                lo = jmin(lo, v);
                hi = jmax(hi, v);
            }
            const bool picked = a >= page.selStart && a < page.selEnd;
            g.setColour(picked ? accent : inkDim.withAlpha(0.55f));
            g.drawVerticalLine(px, mid - hi * half, mid - lo * half + 1.0f);
        }
        // the edges of the picked part
        g.setColour(ink);
        for (const float x : { x0, x1 })
        {
            g.fillRect(x - 1.0f, b.getY(), 2.0f, b.getHeight());
            g.fillRect(x - 5.0f, b.getY(), 10.0f, 6.0f);
        }
        // where PLAY is
        const float played = page.proc.getSamplerPreviewSeconds();
        if (played >= 0.0f)
        {
            const float x = xFor(page.selStart + (int64) ((double) played * page.rate));
            g.setColour(Colours::white.withAlpha(0.9f));
            g.fillRect(x - 0.75f, b.getY(), 1.5f, b.getHeight());
        }
    }

    void mouseDown(const MouseEvent& e) override
    {
        if (page.recL.empty())
            return;
        const float x = e.position.x;
        if (std::abs(x - xFor(page.selStart)) < 7.0f)
            dragging = Edge::start;
        else if (std::abs(x - xFor(page.selEnd)) < 7.0f)
            dragging = Edge::end;
        else
        {
            dragging = Edge::end; // a new part, from here
            anchor = sampleAt(x);
            page.setSelection(anchor, anchor);
            return;
        }
        anchor = dragging == Edge::start ? page.selEnd : page.selStart;
    }

    void mouseDrag(const MouseEvent& e) override
    {
        if (page.recL.empty() || dragging == Edge::none)
            return;
        const auto s = sampleAt(e.position.x);
        page.setSelection(jmin(anchor, s), jmax(anchor, s));
    }

    void mouseUp(const MouseEvent&) override
    {
        dragging = Edge::none;
        if (page.selEnd - page.selStart < 64) // a click without a drag: pick it all again
            page.setSelection(0, (int64) page.recL.size());
    }

    void mouseDoubleClick(const MouseEvent&) override { page.setSelection(0, (int64) page.recL.size()); }

private:
    enum class Edge { none, start, end };

    float xFor(int64 sample) const
    {
        return (float) getWidth() * (float) ((double) (sample - viewStart) / (double) jmax<int64>(1, viewEnd - viewStart));
    }

    int64 sampleAt(float x) const
    {
        const double t = jlimit(0.0, 1.0, (double) x / (double) jmax(1, getWidth()));
        return jlimit<int64>(0, (int64) page.recL.size(), viewStart + (int64) (t * (double) (viewEnd - viewStart)));
    }

    SamplerPage& page;
    Edge dragging = Edge::none;
    int64 anchor = 0;
};

// ------------------------------------------------------------------------------------------

SamplerPage::SamplerPage(BitVibeProcessor& p, PedalLookAndFeel& lookAndFeel) : proc(p), laf(lookAndFeel)
{
    setOpaque(true);
    backButton.onClick = [this]
    {
        stopPlaying();
        if (onClose)
            onClose();
    };
    backButton.setTooltip("Back to the pedal. What you recorded stays here until you record again or close the plug-in.");
    addAndMakeVisible(backButton);

    // 1 RECORD
    sourceBox.setLookAndFeel(&laf);
    sourceBox.setTooltip("RECORD FROM: the program whose sound is recorded (only programs that have sound open are listed; press "
                         "REFRESH after starting one). \"Everything except\" records every program but the one BitVibe runs in.");
    addAndMakeVisible(sourceBox);
    refreshButton.onClick = [this] { refreshSources(); };
    refreshButton.setTooltip("REFRESH: look again for programs that are playing sound.");
    recordButton.onClick = [this] { toggleRecording(); };
    recordButton.setTooltip("RECORD: start recording the program's sound; press again to stop (it stops by itself after a minute).");
    for (auto* b : { &refreshButton, &recordButton })
        addAndMakeVisible(*b);

    // 2 PICK
    waveform = std::make_unique<Waveform>(*this);
    waveform->setTooltip("Drag across the recording to pick the part for your sound; drag the white edges to adjust it. "
                         "A click or a double-click picks everything again.");
    addAndMakeVisible(*waveform);
    playButton.onClick = [this] { play(); };
    loopButton.toggle = true;
    loopButton.onClick = [this]
    {
        setLooping(!loopButton.lit);
        if (loopButton.lit && !previewing)
            play();
    };
    loopButton.setTooltip("REPEAT: play the picked part over and over, so you can turn the knobs and the EQ while you listen. "
                          "Press again (or STOP) to stop looping.");
    stopButton.onClick = [this] { stopPlaying(); };
    allButton.onClick = [this] { setSelection(0, (int64) recL.size()); };
    zoomButton.onClick = [this]
    {
        const bool zoomed = waveform->viewStart != 0 || waveform->viewEnd != (int64) recL.size();
        if (zoomed)
            waveform->showAll();
        else
            waveform->zoomTo({ selStart, selEnd });
        zoomButton.setText(zoomed ? "ZOOM IN" : "SHOW ALL");
    };
    playButton.setTooltip("PLAY: hear the picked part as it will be saved, with the effects below (changes are heard as you make them).");
    stopButton.setTooltip("STOP: stop playing.");
    allButton.setTooltip("PICK ALL: the whole recording.");
    zoomButton.setTooltip("ZOOM IN: show the picked part bigger, to set its edges exactly. SHOW ALL goes back.");
    for (auto* b : { &playButton, &stopButton, &loopButton, &allButton, &zoomButton })
        addAndMakeVisible(*b);

    // 3 SHAPE: the pedal's effects, and an EQ of its own
    if (auto* choice = dynamic_cast<AudioParameterChoice*>(proc.apvts.getParameter(ids::smpDistType)))
        distBox.addItemList(choice->choices, 1);
    distBox.setLookAndFeel(&laf);
    distBox.setTooltip("DISTORTION: none, the overdrive or the fuzz from the right-hand column, on this sound. The sound goes through "
                       "the distortion, the sample rate, the bits and the EQ (right) in that order; then it is levelled, and VOLUME sets "
                       "how loud it sits next to your other sounds.");
    distAtt = std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, ids::smpDistType, distBox);
    addAndMakeVisible(distBox);
    const std::array<std::tuple<KnobSlider*, std::unique_ptr<APVTS::SliderAttachment>*, const char*, const char*>, 6> knobs { {
        { &volumeSlider, &volumeAtt, ids::smpVolume, "VOLUME: how loud this sound sits next to your other sounds. 0 dB = as loud as any other, -6 dB = half as loud." },
        { &driveSlider, &driveAtt, ids::smpDrive, "DRIVE: how hard the distortion (SUSTAIN for the fuzz) works." },
        { &toneSlider, &toneAtt, ids::smpTone, "TONE: darker (left) or brighter (right) distortion." },
        { &levelSlider, &levelAtt, ids::smpLevel, "LEVEL: the distortion's output, before the sound is levelled." },
        { &rateSlider, &rateAtt, ids::smpSample, "SAMPLE RATE: like the pedal's RATE knob. Fully right = untouched, left = grittier and lower." },
        { &bitsSlider, &bitsAtt, ids::smpBits, "BITS: like the pedal's BITS knob. Fully right = untouched (24 bit), left = fewer bits, more crush." },
    } };
    for (const auto& [slider, att, id, tip] : knobs)
    {
        slider->setSliderStyle(Slider::RotaryVerticalDrag);
        slider->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        slider->setRotaryParameters(MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
        slider->setMouseDragSensitivity(180);
        slider->setLookAndFeel(&laf);
        slider->setTooltip(tip);
        *att = std::make_unique<APVTS::SliderAttachment>(proc.apvts, id, *slider);
        if (auto* param = proc.apvts.getParameter(id))
            slider->setDoubleClickReturnValue(true, (double) param->convertFrom0to1(param->getDefaultValue()));
        addAndMakeVisible(*slider);
    }
    eq = std::make_unique<EqPanel>(proc, laf, ids::smpEqPrefix, false);
    eq->scopeSource = [this](float* before, float* after, int n) { proc.readSamplerScope(before, after, n); }; // what PLAY plays
    addAndMakeVisible(*eq);

    // 4 SAVE
    nameBox.setText("My sample", false);
    nameBox.setFont(bold(13.0f));
    nameBox.setColour(TextEditor::backgroundColourId, Colour(0xff0e0e10));
    nameBox.setColour(TextEditor::outlineColourId, panelEdge);
    nameBox.setColour(TextEditor::textColourId, ink);
    nameBox.setTooltip("NAME: what the sound is called (the file name in Sounds/Sampled; a number is added if the name is taken).");
    addAndMakeVisible(nameBox);
    bankBox.setLookAndFeel(&laf);
    bankBox.setTooltip("ADD TO BANK: also put the sound in one of your saved sound banks.");
    addAndMakeVisible(bankBox);
    saveButton.onClick = [this]
    {
        String message;
        const bool ok = save(nameBox.getText(), bankBox.getSelectedId() > 1 ? bankBox.getText() : String(), message);
        status = message;
        statusIsError = !ok;
        repaint();
    };
    saveButton.setTooltip("SAVE SOUND: save the picked part, with the effects, as a sound for the sample blend.");
    addAndMakeVisible(saveButton);

    refreshSources();
    refreshBanks();
    setSize(kPageW, 940);
}

SamplerPage::~SamplerPage()
{
    stopTimer();
    if (proc.getAppCapture().isRecording())
        proc.getAppCapture().stop();
    proc.stopSamplerPreview();
    for (Component* c : std::initializer_list<Component*> { &sourceBox, &distBox, &bankBox, &volumeSlider, &driveSlider, &toneSlider,
                                                            &levelSlider, &rateSlider, &bitsSlider })
        c->setLookAndFeel(nullptr);
}

void SamplerPage::visibilityChanged()
{
    if (isVisible())
    {
        refreshBanks();
        startTimerHz(30);
    }
    else
    {
        stopPlaying();
        stopTimer();
    }
}

void SamplerPage::resized()
{
    backButton.setBounds(12, 8, 160, 26);
    sourceBox.setBounds(kRecordBox.getX() + 110, kRecordBox.getY() + 28, 380, 26);
    refreshButton.setBounds(kRecordBox.getX() + 496, kRecordBox.getY() + 28, 86, 26);
    recordButton.setBounds(kRecordBox.getX() + 590, kRecordBox.getY() + 28, 150, 26);

    waveform->setBounds(kPickBox.getX() + 12, kPickBox.getY() + 26, kPickBox.getWidth() - 24, 180);
    const int row = kPickBox.getY() + 214;
    playButton.setBounds(kPickBox.getX() + 12, row, 80, 26);
    stopButton.setBounds(kPickBox.getX() + 96, row, 80, 26);
    loopButton.setBounds(kPickBox.getX() + 180, row, 80, 26);
    allButton.setBounds(kPickBox.getX() + 268, row, 96, 26);
    zoomButton.setBounds(kPickBox.getX() + 368, row, 110, 26);

    distBox.setBounds(kShapeBox.getX() + 12, kShapeBox.getY() + 30, 132, 24);
    const int knobY1 = kShapeBox.getY() + 60, knobY2 = kShapeBox.getY() + 136;
    const std::array<std::pair<KnobSlider*, Point<int>>, 6> places { {
        { &driveSlider, { kShapeBox.getX() + 200, knobY1 } }, { &toneSlider, { kShapeBox.getX() + 290, knobY1 } },
        { &levelSlider, { kShapeBox.getX() + 380, knobY1 } }, { &rateSlider, { kShapeBox.getX() + 60, knobY2 } },
        { &bitsSlider, { kShapeBox.getX() + 150, knobY2 } }, { &volumeSlider, { kShapeBox.getX() + 380, knobY2 } },
    } };
    for (const auto& [slider, at] : places)
        slider->setBounds(Rectangle<int>(44, 44).withCentre(at));

    eq->setTopLeftPosition(kEqX, kEqY);

    nameBox.setBounds(kSaveBox.getX() + 70, kSaveBox.getY() + 28, 300, 26);
    bankBox.setBounds(kSaveBox.getX() + 490, kSaveBox.getY() + 28, 220, 26);
    saveButton.setBounds(kSaveBox.getX() + 724, kSaveBox.getY() + 28, 188, 26);
}

void SamplerPage::paint(Graphics& g)
{
    g.fillAll(Colour(0xff111113));
    g.setColour(ink);
    g.setFont(bold(18.0f));
    g.drawText("SAMPLER", 190, 8, 300, 26, Justification::centredLeft);
    g.setColour(inkDim);
    g.setFont(plain(11.0f));
    g.drawText("Record another program, pick a part, shape it, save it as a sound for the sample blend.", 300, 8, 636, 26,
               Justification::centredRight);

    // 1 RECORD
    drawBox(g, kRecordBox, 1, "RECORD");
    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText("RECORD FROM", kRecordBox.getX() + 12, kRecordBox.getY() + 28, 96, 26, Justification::centredLeft);
    const auto& capture = proc.getAppCapture();
    const bool recording = capture.isRecording();
    g.setFont(plain(11.0f));
    if (!AppAudioCapture::isAvailable())
    {
        g.setColour(accent);
        g.drawText("Recording a program needs Windows 10 (version 2004) or later.", kRecordBox.getX() + 750, kRecordBox.getY() + 28, 170, 26,
                   Justification::centredLeft, true);
    }
    else
    {
        const double secs = recording ? capture.getRecordedSeconds() : seconds((int64) recL.size());
        g.setColour(recording ? red : inkDim);
        g.drawText(String(secs, 1) + " s" + (recording ? "  recording" : ""), kRecordBox.getX() + 750, kRecordBox.getY() + 28, 90, 26,
                   Justification::centredLeft);
        // the level of what is coming in
        const Rectangle<float> meter((float) kRecordBox.getX() + 844.0f, (float) kRecordBox.getY() + 36.0f, 68.0f, 10.0f);
        g.setColour(Colour(0xff0e0e10));
        g.fillRoundedRectangle(meter, 2.0f);
        g.setColour(meterLevel > 0.97f ? red : accent);
        g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * jlimit(0.0f, 1.0f, meterLevel)), 2.0f);
    }

    // 2 PICK
    drawBox(g, kPickBox, 2, "PICK THE PART YOU WANT");
    g.setColour(inkDim);
    g.setFont(plain(11.0f));
    if (!recL.empty())
    {
        const double len = seconds(selEnd - selStart);
        String text = "Picked " + String(seconds(selStart), 2) + " s to " + String(seconds(selEnd), 2) + " s  (" + String(len, 2) + " s)";
        if (len > kMaxSoundSeconds)
            text << ".  The sample blend uses the first 20 s of a sound.";
        g.setColour(len > kMaxSoundSeconds ? accent : inkDim);
        g.drawText(text, kPickBox.getX() + 488, kPickBox.getY() + 214, kPickBox.getWidth() - 500, 26, Justification::centredRight);
    }

    // 3 SHAPE
    drawBox(g, kShapeBox, 3, "SHAPE IT");
    struct Label { const char* name; String value; Point<int> at; };
    const auto fx = proc.getSamplerFx();
    const bool fuzz = fx.distType == 2, distOn = fx.distType != 0;
    auto percent = [](float v) { return String(roundToInt(v * 100.0f)) + "%"; };
    const float srHz = bitvibe::mapping::sampleRateHz(fx.sampleRateKnob);
    const Label labels[] = {
        { fuzz ? "SUSTAIN" : "DRIVE", percent(fx.drive), { kShapeBox.getX() + 200, kShapeBox.getY() + 96 } },
        { "TONE", percent(fx.tone), { kShapeBox.getX() + 290, kShapeBox.getY() + 96 } },
        { "DIST LEVEL", percent(fx.level), { kShapeBox.getX() + 380, kShapeBox.getY() + 96 } },
        { "SAMPLE RATE", srHz >= 47000.0f ? String("OFF") : (srHz >= 1000.0f ? String(srHz / 1000.0f, 1) + " kHz" : String(roundToInt(srHz)) + " Hz"),
          { kShapeBox.getX() + 60, kShapeBox.getY() + 172 } },
        { "BITS", fx.bitsKnob >= 0.99f ? String("OFF") : String(bitvibe::mapping::bitDepth(fx.bitsKnob), 1) + " bit", { kShapeBox.getX() + 150, kShapeBox.getY() + 172 } },
        { "VOLUME", String(fx.volumeDb, 1) + " dB", { kShapeBox.getX() + 380, kShapeBox.getY() + 172 } },
    };
    for (size_t i = 0; i < std::size(labels); ++i)
    {
        const bool dim = i < 3 && !distOn;
        g.setColour(inkLabel.withAlpha(dim ? 0.4f : 1.0f));
        g.setFont(bold(10.5f));
        g.drawText(labels[i].name, Rectangle<int>(100, 13).withCentre(labels[i].at), Justification::centred);
        g.setColour(inkDim.withAlpha(dim ? 0.4f : 1.0f));
        g.setFont(plain(10.0f));
        g.drawText(labels[i].value, Rectangle<int>(100, 12).withCentre(labels[i].at.translated(0, 13)), Justification::centred);
    }

    // 4 SAVE
    drawBox(g, kSaveBox, 4, "SAVE");
    g.setColour(inkDim);
    g.setFont(bold(10.0f));
    g.drawText("NAME", kSaveBox.getX() + 12, kSaveBox.getY() + 28, 56, 26, Justification::centredLeft);
    g.drawText("ADD TO BANK", kSaveBox.getX() + 390, kSaveBox.getY() + 28, 96, 26, Justification::centredRight);
    if (status.isNotEmpty())
    {
        g.setColour(statusIsError ? red : inkDim);
        g.setFont(plain(11.0f));
        g.drawText(status, kSaveBox.getX() + 12, kSaveBox.getY() + 56, kSaveBox.getWidth() - 24, 16, Justification::centredLeft, true);
    }

    // how to
    g.setColour(inkDim);
    g.setFont(plain(11.5f));
    g.drawFittedText("1  Start the program you want to sample and let it play, press REFRESH, pick it and press RECORD. Press it again to stop.\n"
                     "2  Drag across the waveform to pick the part you want. PLAY lets you hear it as it will be saved; REPEAT keeps it going.\n"
                     "3  Shape it: distortion, sample rate and bits, the EQ and the volume. PLAY again to hear the change.\n"
                     "4  Name it, choose a bank if you like, and SAVE SOUND. It is in CHOOSE SOUNDS, in the Sampled folder, straight away.\n"
                     "Only record what you have the right to use.",
                     Rectangle<int>(24, kSaveBox.getBottom() + 16, 900, 110), Justification::topLeft, 6);
}

void SamplerPage::refreshSources()
{
    const auto previous = sourceBox.getText();
    sources = AppAudioCapture::listSources();
    sourceBox.clear(dontSendNotification);
    for (int i = 0; i < sources.size(); ++i)
        sourceBox.addItem(sources[i].name, i + 1);
    int chosen = sources.size() > 1 ? 2 : 1; // the first program, if one is playing
    for (int i = 0; i < sources.size(); ++i)
        if (sources[i].name == previous)
            chosen = i + 1;
    if (sources.size() > 0)
        sourceBox.setSelectedId(chosen, dontSendNotification);
    sourceBox.setEnabled(AppAudioCapture::isAvailable());
    recordButton.setEnabled(AppAudioCapture::isAvailable());
    recordButton.setAlpha(AppAudioCapture::isAvailable() ? 1.0f : 0.4f);
}

void SamplerPage::refreshBanks()
{
    const auto previous = bankBox.getText();
    bankBox.clear(dontSendNotification);
    bankBox.addItem("No bank", 1);
    const auto names = proc.getSampleBank().getBankNames();
    for (int i = 0; i < names.size(); ++i)
        bankBox.addItem(names[i], i + 2);
    const int index = names.indexOf(previous);
    bankBox.setSelectedId(index >= 0 ? index + 2 : 1, dontSendNotification);
}

void SamplerPage::toggleRecording()
{
    auto& capture = proc.getAppCapture();
    if (capture.isRecording())
    {
        capture.stop();
        finishRecording();
        return;
    }
    stopPlaying();
    const int index = sourceBox.getSelectedId() - 1;
    if (!isPositiveAndBelow(index, sources.size()))
    {
        status = "Pick a program to record first.";
        statusIsError = true;
        repaint();
        return;
    }
    String error;
    if (!capture.start(sources[index], error))
    {
        status = error;
        statusIsError = true;
    }
    else
    {
        status = "Recording " + sources[index].name + "...";
        statusIsError = false;
        recordButton.setText("STOP RECORDING");
        recordButton.lit = true;
        recordButton.repaint();
    }
    repaint();
}

void SamplerPage::finishRecording()
{
    recordButton.setText("RECORD");
    recordButton.lit = false;
    recordButton.repaint();
    std::vector<float> l, r;
    proc.getAppCapture().takeRecording(l, r);
    const auto error = proc.getAppCapture().getLastError();
    if (l.empty())
    {
        status = error.isNotEmpty() ? error : String("Nothing was recorded: was the program playing?");
        statusIsError = true;
        repaint();
        return;
    }
    const bool silent = std::all_of(l.begin(), l.end(), [](float v) { return std::abs(v) < 1.0e-4f; })
                        && std::all_of(r.begin(), r.end(), [](float v) { return std::abs(v) < 1.0e-4f; });
    setRecording(std::move(l), std::move(r));
    status = silent ? String("The recording is silent: was the program playing, and is its sound on?")
                    : "Recorded " + String(seconds((int64) recL.size()), 1) + " s. Drag across the waveform to pick the part you want.";
    statusIsError = silent;
    repaint();
}

void SamplerPage::setRecording(std::vector<float> left, std::vector<float> right)
{
    stopPlaying();
    const auto n = jmin(left.size(), right.size());
    left.resize(n);
    right.resize(n);
    recL = std::move(left);
    recR = std::move(right);
    rate = AppAudioCapture::kSampleRate;
    selStart = 0;
    selEnd = (int64) n;
    waveform->showAll();
    zoomButton.setText("ZOOM IN");
    repaint();
}

void SamplerPage::setSelection(int64 start, int64 end)
{
    const auto n = (int64) recL.size();
    selStart = jlimit<int64>(0, n, jmin(start, end));
    selEnd = jlimit<int64>(0, n, jmax(start, end));
    if (previewing)
    {
        if (loopButton.lit)
            restartPending = true; // LOOP goes on with the new part once the mouse is let go
        else
            stopPlaying();
    }
    waveform->repaint();
    repaint(kPickBox);
}

void SamplerPage::setLooping(bool loop)
{
    loopButton.lit = loop;
    loopButton.repaint();
    proc.setSamplerPreviewLoop(loop);
}

void SamplerPage::renderSelection(std::vector<float>& left, std::vector<float>& right, std::vector<float>* beforeEq) const
{
    const auto a = (size_t) jlimit<int64>(0, (int64) recL.size(), selStart), z = (size_t) jlimit<int64>((int64) a, (int64) recL.size(), selEnd);
    const std::vector<float> l(recL.begin() + (long) a, recL.begin() + (long) z), r(recR.begin() + (long) a, recR.begin() + (long) z);
    bitvibe::renderSample(l, r, rate, proc.getSamplerFx(), left, right, beforeEq);
}

bool SamplerPage::save(const String& name, const String& bank, String& message)
{
    if (recL.empty() || selEnd - selStart < 16)
    {
        message = "There is nothing to save: record something and pick the part you want.";
        return false;
    }
    std::vector<float> l, r;
    renderSelection(l, r);
    const auto maxFrames = (size_t) (kMaxSoundSeconds * rate);
    const bool cut = l.size() > maxFrames;
    if (cut)
    {
        l.resize(maxFrames);
        r.resize(maxFrames);
    }
    const bool ok = proc.saveSampledSound(l, r, rate, name, bank, message);
    if (ok && cut)
        message << " It was longer than 20 s, so only the first 20 s were kept.";
    if (ok)
        refreshBanks();
    return ok;
}

String SamplerPage::fxSignature() const
{
    const auto fx = proc.getSamplerFx();
    String s;
    s << fx.distType << ' ' << fx.drive << ' ' << fx.tone << ' ' << fx.level << ' ' << fx.sampleRateKnob << ' ' << fx.bitsKnob << ' '
      << (int) fx.eqOn << ' ' << fx.eqOutputDb << ' ' << fx.volumeDb;
    for (const auto& b : fx.eqBands)
        s << ' ' << (int) b.on << (int) b.type << ' ' << b.freq << ' ' << b.gainDb << ' ' << b.q;
    return s;
}

void SamplerPage::play()
{
    if (recL.empty() || selEnd - selStart < 16)
        return;
    std::vector<float> l, r, before;
    renderSelection(l, r, &before);
    proc.setSamplerPreview(std::move(l), std::move(r), rate, std::move(before));
    proc.setSamplerPreviewLoop(loopButton.lit);
    proc.playSamplerPreview();
    previewing = true;
    playStartMs = Time::getMillisecondCounterHiRes();
    previewSignature = fxSignature();
}

void SamplerPage::stopPlaying()
{
    proc.stopSamplerPreview();
    previewing = false;
    if (loopButton.lit)
        setLooping(false);
}

void SamplerPage::timerCallback()
{
    auto& capture = proc.getAppCapture();
    const float peak = capture.takePeak();
    meterLevel = jmax(peak, meterLevel * 0.85f);
    if (recordButton.lit && !capture.isRecording()) // it stopped by itself: a minute is up, or it failed
        finishRecording();

    if (restartPending && !ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        restartPending = false;
        play();
    }

    // while PLAY sounds, the effects are heard as they change (a new render a moment after the last change)
    if (previewing)
    {
        // it has played to the end (the first moments after PLAY do not count: the audio thread starts it a little later)
        if (proc.getSamplerPreviewSeconds() < 0.0f && Time::getMillisecondCounterHiRes() - playStartMs > 400.0)
            previewing = false;
        const auto signature = fxSignature();
        if (signature != previewSignature)
        {
            previewSignature = signature;
            signatureCountdown = 4;
        }
        else if (signatureCountdown > 0 && --signatureCountdown == 0)
        {
            std::vector<float> l, r, before;
            renderSelection(l, r, &before);
            proc.setSamplerPreview(std::move(l), std::move(r), rate, std::move(before));
        }
    }
    eq->refresh();
    waveform->repaint();
    repaint(kRecordBox);
    repaint(kShapeBox);
}
